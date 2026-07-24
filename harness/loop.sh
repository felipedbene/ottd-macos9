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

# Halt the VM BEFORE deploy: a running guest holds `openttd - latest` open over
# AFP, so deploy's mv-over-the-app fails "Resource busy" until the guest is gone
# and its afpd lock is released. halt = stop + wait + release_share_lock (VM off).
echo "== loop: halting VM to release the AFP lock on the deployed app =="
bash "$H/vm.sh" halt

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
