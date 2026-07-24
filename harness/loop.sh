#!/bin/bash
# One autonomous iteration of the build→deploy→run→assert loop (Bun-blog style):
#   deploy.sh  — bump B2_BUILD_TAG, incremental build, cp .bin + ottdr1-latest app to the share
#   vm.sh reset — QMP system_reset → OS 9 reboots → Startup Items alias launches ottdr1-latest
#   assert.py  — follow the UDP log sink, gate 12 assertions on THIS tag's run
#   vm.sh screendump — visual record
# Artifacts (raw sink slice + verdict + screenshot) land in harness/artifacts/<TAG>/.
# Exit code = the assert verdict (0 PASS, 1 FAIL, 2 harness error).
#
# Usage: harness/loop.sh [--no-bump]
set -euo pipefail
cd "$(dirname "$0")/.."
H=harness

SHARE=/Volumes/vintage
AFP_HOST=10.0.1.148
AFP_URL='afp://;AUTH=No User Authent@10.0.1.148/vintage'   # guest, password-less

share_mounted() { mount | grep -q " on $SHARE "; }

# unmount_share — cleanly log this Mac out of the AFP share. Done BEFORE halt so
# release_share_lock (which kills afpd sessions holding an fd on the app) can't
# kill OUR session and drop the mount mid-deploy — and so no stray /Volumes/
# vintage-N remount is left behind. Idempotent; unmounts every mount of the share.
unmount_share() {
    local mp
    for mp in $(mount | awk -v h="$AFP_HOST/vintage" 'index($0,h){print $3}'); do
        diskutil unmount "$mp" >/dev/null 2>&1 || umount "$mp" 2>/dev/null || true
    done
    [ -d "$SHARE" ] && rmdir "$SHARE" 2>/dev/null || true
}

# ensure_share — mount the deploy target at EXACTLY $SHARE. Clears any stray
# vintage-N mounts + a leftover empty mountpoint first, else osascript auto-renames
# the mount to /Volumes/vintage-1 (which broke deploy: it needs $SHARE by exact path).
ensure_share() {
    share_mounted && return 0
    echo "== loop: mounting $SHARE (clearing any stray vintage-N mounts first) =="
    unmount_share
    osascript -e "mount volume \"$AFP_URL\"" >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5; do share_mounted && return 0; sleep 1; done
    echo "== loop: WARN could not mount $SHARE — deploy will fail with a clear error" >&2
    return 1
}

# Halt the VM BEFORE deploy: a running guest holds `openttd - latest` open over
# AFP, so deploy's mv-over-the-app fails "Resource busy" until the guest is gone
# and its afpd lock is released. halt = stop + wait + release_share_lock (VM off).
# Unmount our AFP session FIRST so release_share_lock can't sweep it up (see above).
echo "== loop: unmounting AFP share + halting VM to release the app lock =="
unmount_share
bash "$H/vm.sh" halt

ensure_share

bash "$H/deploy.sh" "$@"

TAG=$(grep -o 'R1-[0-9]*' ottd-r1/r1main.c | head -1)
ART="$H/artifacts/$TAG"; mkdir -p "$ART"

echo "== loop: starting VM (OS 9 boot + auto-launch of the new build, ~4-5 min under TCG) =="
bash "$H/vm.sh" start

set +e
python3 "$H/assert.py" --tag "$TAG" --follow --boot-timeout 420 --beats 3 \
    --save-log "$ART/sink.log" | tee "$ART/verdict.txt"
rc=${PIPESTATUS[0]}
set -e

bash "$H/vm.sh" screendump "$ART/screen.png" || echo "loop: screendump failed (non-fatal)"

echo "== loop: $TAG done, rc=$rc, artifacts in $ART =="
exit "$rc"
