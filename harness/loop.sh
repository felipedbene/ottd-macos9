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
AFP_URL='afp://;AUTH=No User Authent@10.0.1.148/vintage'   # guest, password-less

# ensure_share — (re)mount the deploy target if it's absent. vm.sh halt's
# release_share_lock kills afpd sessions holding an fd on the app's .AppleDouble;
# because THIS Mac writes the app during deploy, its own afpd session can hold
# such an fd and get swept up, dropping /Volumes/vintage. Remount idempotently.
ensure_share() {
    mount | grep -q " on $SHARE " && return 0
    echo "== loop: $SHARE not mounted (halt/release-lock dropped it); remounting =="
    osascript -e "mount volume \"$AFP_URL\"" >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5; do mount | grep -q " on $SHARE " && return 0; sleep 1; done
    echo "== loop: WARN could not remount $SHARE — deploy will fail with a clear error" >&2
    return 1
}

# Halt the VM BEFORE deploy: a running guest holds `openttd - latest` open over
# AFP, so deploy's mv-over-the-app fails "Resource busy" until the guest is gone
# and its afpd lock is released. halt = stop + wait + release_share_lock (VM off).
echo "== loop: halting VM to release the AFP lock on the deployed app =="
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
