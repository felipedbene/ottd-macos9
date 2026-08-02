#!/bin/bash
# fanout.sh <n> [--no-bump] — test n builds concurrently across n pool slots.
#
# One wave instead of n serial iterations. Slots 1..n each get their own payload
# CD, their own cold-booted overlay off the golden image, and their own tag; all
# n boot at once, all n are asserted at once, and a summary table lands at the end.
#
# Tagging: every slot reports to the SAME sink, and the sink NATs every sender to
# one IP (10.0.10.5, the intel5 node), so the source address cannot tell two live
# runs apart — the build tag is the only discriminator. Each slot therefore builds
# under "<base>-s<N>" (R1-125-s1, R1-125-s2, ...). Same source here; to fan out
# n DIFFERENT features, build each in its own git worktree and pass its .APPL:
#
#     fanout.sh --apps /path/a.APPL,/path/b.APPL
#
# Concurrency: each TCG guest is ~1 host core and mactrash-can has 6 real cores
# (12 threads), so n<=6 runs at full speed. Higher n still works, just slower.
set -euo pipefail
cd "$(dirname "$0")/.."
H=harness

BOOT_WAIT="${BOOT_WAIT:-120}"
# WARM=1 resumes every slot from golden-warm.qcow2 instead of cold-booting: a
# desktop in ~1s, then the payload CD is inserted and OS 9 mounts it. Needs
# `pool.sh warm-create` once.
WARM_SETTLE="${WARM_SETTLE:-40}"
LAUNCH_STAGGER="${LAUNCH_STAGGER:-20}"
APPS=""
N=""
BUMP_ARG=""

while [ $# -gt 0 ]; do
  case "$1" in
    --apps) shift; APPS="${1:?--apps needs a comma-separated list}" ;;
    --no-bump) BUMP_ARG="--no-bump" ;;
    ''|*[!0-9]*) echo "fanout.sh: unknown argument '$1'" >&2; exit 2 ;;
    *) N="$1" ;;
  esac
  shift
done

MAIN=ottd-r1/r1main.c
read_tag() { sed -n 's/^#define B2_BUILD_TAG "\([^"]*\)".*/\1/p' "$MAIN"; }

# --- decide the per-slot (tag, app) pairs ------------------------------------
declare -a SLOT_TAG SLOT_APP

if [ -n "$APPS" ]; then
  IFS=',' read -r -a app_list <<< "$APPS"
  N=${#app_list[@]}
  for i in "${!app_list[@]}"; do
    app="${app_list[$i]}"
    [ -f "$app" ] || { echo "fanout.sh: missing app $app" >&2; exit 2; }
    # A prebuilt .APPL already has its tag compiled in; read it back out of the
    # binary rather than trusting the working tree, which has moved on.
    tag=$(strings "$app" 2>/dev/null | grep -oE '^R1-[0-9A-Za-z.-]+$' | head -1)
    [ -n "$tag" ] || { echo "fanout.sh: could not read a build tag out of $app" >&2; exit 2; }
    SLOT_TAG[$((i + 1))]="$tag"
    SLOT_APP[$((i + 1))]="$app"
  done
else
  [ -n "$N" ] || { echo "usage: fanout.sh <n> [--no-bump] | fanout.sh --apps a.APPL,b.APPL" >&2; exit 2; }
  # Bump once for the wave, then build the same source under one suffixed tag per
  # slot. Each build only redefines B2_BUILD_TAG, so it is an incremental relink.
  echo "== fanout: preparing $N builds from the current tree =="
  bash "$H/deploy.sh" --no-share ${BUMP_ARG:+$BUMP_ARG} >/dev/null
  BASE=$(read_tag)
  for s in $(seq 1 "$N"); do
    tag="$BASE-s$s"
    echo "== fanout: building $tag for slot $s =="
    bash "$H/deploy.sh" --no-share --tag "$tag" >/dev/null
    staged="/tmp/fanout-$tag.APPL"
    ditto ottd-r1/build/ottdr1.APPL "$staged"
    SLOT_TAG[$s]="$tag"
    SLOT_APP[$s]="$staged"
  done
  # Leave the tree on the plain base tag, not the last slot's suffixed one.
  bash "$H/deploy.sh" --no-share --tag "$BASE" >/dev/null
fi

echo "== fanout: $N slots =="
for s in $(seq 1 "$N"); do echo "   slot $s -> ${SLOT_TAG[$s]}"; done

# --- ship payloads and cold-boot every slot ----------------------------------
for s in $(seq 1 "$N"); do
  bash "$H/payload.sh" "$s" "${SLOT_APP[$s]}" >/dev/null
  echo "== fanout: slot $s payload ready (${SLOT_TAG[$s]})"
done

for s in $(seq 1 "$N"); do
  bash "$H/pool.sh" stop "$s" >/dev/null 2>&1 || true
done
for s in $(seq 1 "$N"); do
  WARM="${WARM:-0}" bash "$H/pool.sh" start "$s"
done

if [ "${WARM:-0}" = 1 ]; then
  # NOT SUPPORTED for a wave. Warm resume is reliable with ONE slot (12/12, three
  # separate runs) but wedges the guest when several slots resume together: the
  # framebuffer freezes within ~a minute, the menu clock stops, an inserted CD
  # never appears, and QEMU still burns 95% CPU. Reproduced on every 3-slot warm
  # wave; slot 1 only survives because it is launched before the freeze and the
  # running app has its own event loop. Root cause not found. Cold waves are
  # unaffected (3/3, twice) — boot jitter may be what keeps them out of lockstep.
  [ "$N" -le 1 ] || echo "== fanout: WARNING WARM=1 with n>1 wedges guests — use the default cold path" >&2
  # Resumed slots are already at the desktop; they just have no medium yet.
  for s in $(seq 1 "$N"); do bash "$H/pool.sh" insert "$s" >/dev/null; done
  echo "== fanout: waiting ${WARM_SETTLE}s for $N mounts (+ the dead-AFP dialog) =="
  sleep "$WARM_SETTLE"
  # The restored guest still holds the snapshot's AFP session, which is dead by
  # the time it resumes, so the Finder raises "the file server's connection has
  # unexpectedly closed down". Launching into that dialog wedged the guest mid-GRF
  # (verified: 12/12 with this dismissal, hard hang without it). Clear it first.
  for s in $(seq 1 "$N"); do bash "$H/pool.sh" key "$s" ret >/dev/null; done
  sleep 5
else
  echo "== fanout: waiting ${BOOT_WAIT}s for $N OS 9 desktops =="
  sleep "$BOOT_WAIT"
fi

# Stagger the launches. Cold slots desynchronise on their own (boot timing jitter),
# but warm slots resume from one identical vmstate and are driven by the same
# keystrokes, so they run in near-lockstep and emit the same log burst at the same
# instant. netlog's Open Transport send is non-blocking and drops under burst, so
# the synchronised bursts cost log lines — and a run whose telemetry stops looks
# exactly like a hang to assert.py. (Verified: a "HANG" slot's screendump showed
# OpenTTD alive at population 1632, hundreds of ticks past its last heartbeat.)
for s in $(seq 1 "$N"); do
  [ "$s" -gt 1 ] && sleep "$LAUNCH_STAGGER"
  bash "$H/pool.sh" launch "$s" >/dev/null && echo "== fanout: slot $s launched"
done

# --- assert every slot in parallel -------------------------------------------
echo "== fanout: asserting $N runs concurrently =="
pids=()
for s in $(seq 1 "$N"); do
  tag="${SLOT_TAG[$s]}"
  art="$H/artifacts/$tag"; mkdir -p "$art"
  (
    python3 "$H/assert.py" --tag "$tag" --follow --boot-timeout 420 --beats 3 \
        --save-log "$art/sink.log" > "$art/verdict.txt" 2>&1
    echo $? > "$art/rc"
  ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p" || true; done

for s in $(seq 1 "$N"); do
  bash "$H/pool.sh" screendump "$s" "$H/artifacts/${SLOT_TAG[$s]}/screen.png" >/dev/null 2>&1 \
    || echo "fanout: slot $s screendump failed (non-fatal)"
done

# --- summary ------------------------------------------------------------------
echo
echo "== fanout summary =="
worst=0
for s in $(seq 1 "$N"); do
  tag="${SLOT_TAG[$s]}"
  rc=$(cat "$H/artifacts/$tag/rc" 2>/dev/null || echo 2)
  case "$rc" in
    0) verdict="PASS" ;;
    1) verdict="FAIL" ;;
    *) verdict="ERROR" ;;
  esac
  printf "   slot %-2s %-16s %s   (%s)\n" "$s" "$tag" "$verdict" "$H/artifacts/$tag"
  [ "$rc" -gt "$worst" ] && worst=$rc
done
exit "$worst"
