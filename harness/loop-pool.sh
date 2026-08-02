#!/bin/bash
# loop-pool.sh <slot> [--no-bump] — one autonomous iteration on a pool slot.
#
#   deploy.sh --no-share  bump B2_BUILD_TAG, incremental build (no AFP anywhere)
#   payload.sh <slot>     put the build on slot <slot>'s HFS CD and ship it
#   pool.sh stop/start    roll the slot back to the golden image and cold-boot it
#   pool.sh launch        drive the OS 9 Finder over QMP to run the CD's app
#   assert.py             follow the UDP sink, gate the 12 assertions on THIS tag
#   pool.sh screendump    visual record
#
# Differences from loop.sh (the CT-100 single-VM loop this replaces):
#   * no halt-the-world — a slot's build lives on its own CD, so deploying to
#     slot 3 cannot disturb slots 1, 2 and 4, and no AFP lock is ever taken
#   * no share mount/unmount dance and no release_share_lock
#   * slots are disposable: every start recreates the overlay from the golden
#     base, so no iteration can inherit state from the last one
#
# Artifacts land in harness/artifacts/<TAG>/. Exit code = the assert verdict.
set -euo pipefail
cd "$(dirname "$0")/.."
H=harness

SLOT="${1:?usage: loop-pool.sh <slot> [--no-bump]}"
shift || true
case "$SLOT" in
    ''|*[!0-9]*) echo "loop-pool.sh: slot must be a number, got '$SLOT'" >&2; exit 2 ;;
esac

# Cold boot to the OS 9 desktop measured ~65-85s on mactrash-can (vs 4-5 min on
# CT-100's single core). Allow generous headroom: launching into a Finder that
# is still populating loses the keystrokes.
BOOT_WAIT="${BOOT_WAIT:-120}"
# WARM=1 resumes the booted-desktop snapshot instead (a desktop in ~1s), then
# inserts the payload CD, which OS 9 mounts in ~10-25s. Needs `pool.sh warm-create`.
WARM_SETTLE="${WARM_SETTLE:-40}"

echo "== loop[$SLOT]: build (no AFP) =="
bash "$H/deploy.sh" --no-share "$@"

TAG=$(grep -o 'R1-[0-9]*' ottd-r1/r1main.c | head -1)
ART="$H/artifacts/$TAG"; mkdir -p "$ART"
echo "== loop[$SLOT]: tag $TAG =="

echo "== loop[$SLOT]: building + shipping payload CD =="
bash "$H/payload.sh" "$SLOT"

bash "$H/pool.sh" stop "$SLOT" || true
WARM="${WARM:-0}" bash "$H/pool.sh" start "$SLOT"

if [ "${WARM:-0}" = 1 ]; then
    echo "== loop[$SLOT]: warm resume — inserting the payload CD =="
    bash "$H/pool.sh" insert "$SLOT"
    echo "== loop[$SLOT]: waiting ${WARM_SETTLE}s for the mount (+ the dead-AFP dialog) =="
    sleep "$WARM_SETTLE"
    # The snapshot's AFP session is dead on resume, so the Finder raises "the file
    # server's connection has unexpectedly closed down". Launching into that dialog
    # wedged the guest mid-GRF; dismissing it first is 12/12.
    bash "$H/pool.sh" key "$SLOT" ret
    sleep 5
else
    echo "== loop[$SLOT]: cold boot (fresh overlay off the golden base) =="
    echo "== loop[$SLOT]: waiting ${BOOT_WAIT}s for the OS 9 desktop =="
    sleep "$BOOT_WAIT"
fi

bash "$H/pool.sh" launch "$SLOT"

set +e
python3 "$H/assert.py" --tag "$TAG" --follow --boot-timeout 420 --beats 3 \
    --save-log "$ART/sink.log" | tee "$ART/verdict.txt"
rc=${PIPESTATUS[0]}
set -e

bash "$H/pool.sh" screendump "$SLOT" "$ART/screen.png" || echo "loop[$SLOT]: screendump failed (non-fatal)"

echo "== loop[$SLOT]: $TAG done, rc=$rc, artifacts in $ART =="
exit "$rc"
