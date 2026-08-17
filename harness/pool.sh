#!/usr/bin/env bash
# pool.sh — Mac-side driver for the Mac OS 9 slot pool on mactrash-can (10.0.1.53).
#
# Replaces the retired CT-100 vm.sh single-VM model: every command takes a slot
# number, and slots are fully independent (own overlay, own payload disk, own
# QMP/VNC port), so N builds can be under test at once.
#
#   pool.sh start <slot>              boot the slot in tmux session "slot<N>"
#   pool.sh stop <slot>               QMP quit, then kill the tmux session
#   pool.sh status <slot>             QMP query-status, raw JSON
#   pool.sh launch <slot>             drive the Finder over QMP to run the payload CD
#   pool.sh key <slot> <spec>         send raw key chords, e.g. "ret,meta_l+w"
#   pool.sh screendump <slot> <png>   QMP screendump -> scp -> sips PPM->PNG
#   pool.sh insert <slot>             load the slot's payload CD over QMP
#   pool.sh warm-create               build golden-warm.qcow2 (booted-desktop snapshot)
#   pool.sh savevm <slot> <name>      QMP snapshot-save (warm-boot checkpoint)
#   pool.sh loadvm <slot> <name>      QMP snapshot-load (skip the cold boot)
#   pool.sh list                      which slots are up
#
# WARM=1 pool.sh start <slot>  resumes the booted-desktop snapshot instead of
# cold-booting — ~1s instead of ~85s. Needs `pool.sh warm-create` first. Use it
# for ONE slot at a time: several concurrent resumes wedge the guests (see the
# warning in fanout.sh).
#
# Unlike CT-100 (busybox nc, no python), mactrash-can has real python3 — the QMP
# helper is a proper socket client, so replies are parsed instead of scraped.
set -euo pipefail

SSH_KEY="$HOME/.ssh/id_openclaw"
SSH_OPTS=(-i "$SSH_KEY" -o IdentitiesOnly=yes)
REMOTE="felipe@10.0.1.53"
REMOTE_HOME="/home/felipe"

HARNESS_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCAL_RUNNER="$HARNESS_DIR/remote/run-slot.sh"

ssh_() { ssh "${SSH_OPTS[@]}" "$REMOTE" "$@"; }

qmp_port() { echo $((4444 + $1)); }

# qmp <slot> <json-command> — one QMP command, prints the reply JSON.
# Runs a python3 client on the remote: connect, greet, qmp_capabilities, send,
# read until a line carries "return" or "error".
qmp() {
    local slot="$1" cmd="$2" port
    port=$(qmp_port "$slot")
    ssh_ "python3 -c '
import json, socket, sys
port, cmd = int(sys.argv[1]), sys.argv[2]
s = socket.create_connection((\"127.0.0.1\", port), timeout=10)
s.settimeout(15)
f = s.makefile(\"rw\", encoding=\"utf-8\", newline=\"\n\")
f.readline()                                   # greeting
f.write(json.dumps({\"execute\": \"qmp_capabilities\"}) + \"\n\"); f.flush()
while True:                                    # swallow the handshake reply
    line = f.readline()
    if not line: sys.exit(\"qmp: closed during handshake\")
    if \"return\" in json.loads(line) or \"error\" in json.loads(line): break
f.write(cmd + \"\n\"); f.flush()
while True:
    line = f.readline()
    if not line: sys.exit(\"qmp: closed awaiting reply\")
    msg = json.loads(line)
    if \"return\" in msg or \"error\" in msg:
        print(json.dumps(msg)); break
' $port '$cmd'"
}

session_exists() { ssh_ "tmux has-session -t slot$1" 2>/dev/null; }

cmd_install() {
    scp "${SSH_OPTS[@]}" "$LOCAL_RUNNER" "$REMOTE:$REMOTE_HOME/run-slot.sh"
    ssh_ "chmod +x $REMOTE_HOME/run-slot.sh"
    echo "installed $LOCAL_RUNNER -> $REMOTE:$REMOTE_HOME/run-slot.sh"
}

# insert_cd <slot> — load this slot's payload ISO over QMP.
#
# This is the WARM path's deploy step: a resumed slot has no medium, so the build
# arrives by insertion instead of by boot. OS 9 mounts an inserted CD in ~10-25s.
#
# "device":"pay0", NOT "id":"pay0". pay0 is the -drive alias; the ide-cd device
# was declared without a qdev id, so the "id" form answers DeviceNotFound. That
# error, discarded to /dev/null by the first version of this function, is the
# whole reason the pool was believed to need a cold boot per build — the insert
# had simply never happened.
insert_cd() {
    local slot="$1" iso="$REMOTE_HOME/payload-slot$1.iso" out
    if ! ssh_ "test -f $iso"; then
        echo "slot $slot: no payload CD at $iso — nothing inserted" >&2
        return 1
    fi
    out=$(qmp "$slot" "{\"execute\":\"blockdev-change-medium\",\"arguments\":{\"device\":\"pay0\",\"filename\":\"$iso\",\"format\":\"raw\",\"read-only-mode\":\"retain\"}}")
    case "$out" in
        *'"error"'*) echo "slot $slot: insert FAILED: $out" >&2; return 1 ;;
    esac
    echo "slot $slot: payload CD inserted, waiting for the tray to close"
    for _ in $(seq 1 20); do
        case "$(qmp "$slot" '{"execute":"query-block"}')" in
            *'"tray_open": false'*|*'"tray_open":false'*) echo "slot $slot: tray closed"; return 0 ;;
        esac
        sleep 1
    done
    echo "slot $slot: WARN tray still open after 20s" >&2
    return 1
}

cmd_start() {
    local slot="${1:?usage: pool.sh start <slot>}"
    if session_exists "$slot"; then
        echo "slot $slot already running (tmux session slot$slot)"
        return 0
    fi
    # ALWAYS pass WARM explicitly (default 0). The remote tmux SERVER keeps the
    # environment of whichever call first started it — after one WARM=1 start,
    # a plain start inherited WARM=1 from the server and silently warm-resumed
    # with an empty CD drive (a sweep lost a whole run to a banner that could
    # never come). Explicit-on-the-command-line beats both server env and
    # tmux's own env inheritance.
    ssh_ "WARM=${WARM:-0} tmux new-session -d -s slot$slot 'WARM=${WARM:-0} QEMU_EXTRA=\"${QEMU_EXTRA:-}\" $REMOTE_HOME/run-slot.sh $slot 2>&1 | tee $REMOTE_HOME/slot$slot.log'"
    echo "slot $slot started (QMP $(qmp_port "$slot"), VNC :$slot)"
    # Wait for QMP to bind so callers can drive the slot immediately after start.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        qmp "$slot" '{"execute":"query-status"}' >/dev/null 2>&1 && break
        sleep 1
    done
}

cmd_insert() { insert_cd "${1:?usage: pool.sh insert <slot>}"; }

cmd_stop() {
    local slot="${1:?usage: pool.sh stop <slot>}"
    if ! session_exists "$slot"; then
        echo "slot $slot is not running"
        return 0
    fi
    qmp "$slot" '{"execute":"quit"}' >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5; do
        session_exists "$slot" || { echo "slot $slot stopped"; return 0; }
        sleep 2
    done
    ssh_ "tmux kill-session -t slot$slot" 2>/dev/null || true
    echo "slot $slot killed (QMP quit did not take)"
}

cmd_status() { qmp "${1:?usage: pool.sh status <slot>}" '{"execute":"query-status"}'; }

# Raw QMP passthrough — for probing (query-block, eject, ...) without adding a
# subcommand for every one-off.
cmd_qmp() { local slot="${1:?usage: pool.sh qmp <slot> <json>}"; shift; qmp "$slot" "$1"; }

# sendkeys <slot> <spec> — spec is comma-separated chords, "+" joins a chord:
#   "ret,meta_l+w,s,l"          Return, Cmd-W, then type s l
# meta_l is Command on this guest (verified: meta_l+w closed a Finder window).
#
# hold-time is explicit because QEMU's default is 100ms of GUEST time and a slot
# sharing the host with other guests can go that long without its USB HID poll —
# the press/release lands between polls and the key is simply never seen. That is
# how a concurrent wave lost whole launches: one slot's CD window never opened,
# another's app was selected but never started, while the same slot run alone was
# flawless.
sendkeys() {
    local slot="$1" spec="$2" port
    port=$(qmp_port "$slot")
    ssh_ "python3 -c '
import json, socket, sys, time
port, spec = int(sys.argv[1]), sys.argv[2]
s = socket.create_connection((\"127.0.0.1\", port), timeout=10); s.settimeout(15)
f = s.makefile(\"rw\", encoding=\"utf-8\", newline=\"\n\")
f.readline()
f.write(json.dumps({\"execute\": \"qmp_capabilities\"}) + \"\n\"); f.flush(); f.readline()
for combo in spec.split(\",\"):
    keys = [{\"type\": \"qcode\", \"data\": k} for k in combo.split(\"+\")]
    f.write(json.dumps({\"execute\": \"send-key\", \"arguments\": {\"keys\": keys, \"hold-time\": 200}}) + \"\n\"); f.flush()
    f.readline()
    time.sleep(0.3)    # OS 9 Finder drops keystrokes sent back-to-back
' $port '$spec'"
}

cmd_key() { local slot="${1:?usage: pool.sh key <slot> <spec>}"; shift; sendkeys "$slot" "$1"; echo "slot $slot: sent $1"; }

# cmd_launch — start THIS slot's build straight from its payload CD, by driving
# the OS 9 Finder over QMP send-key. No alias, no Startup Items edit, so every
# slot runs the untouched golden image.
#
#   ret        dismiss the launch-failure dialog the stale AFP alias raises
#   Cmd-W x4   close every Finder window so keystrokes go to the desktop
#   s l        type-select the SLOT volume, Cmd-O to open it
#   o p        type-select "openttd - latest" on the CD, Cmd-O to launch
#
# Type-select prefixes are as SHORT as uniqueness allows, because OS 9 resets the
# type-select buffer after ~1.3s and sendkeys spaces chords 0.35s apart. "slot"
# (1.4s) and "openttd" (2.45s) are already at or past that budget when the guest
# has a whole core; with several slots contending the guest stretches, the buffer
# resets mid-word, and the selection lands somewhere else — which is exactly how a
# concurrent warm wave failed: one slot never opened the CD window, another opened
# it but never launched. "sl" is unique among share/Sherlock/SLOT/StuffIt and "op"
# among english.lng/ogfx1_base.grf/openttd, so both fit in 0.7s.
cmd_launch() {
    local slot="${1:?usage: pool.sh launch <slot>}"
    echo "slot $slot: dismissing any startup dialog + clearing Finder windows"
    sendkeys "$slot" "ret,meta_l+w,meta_l+w,meta_l+w,meta_l+w"
    echo "slot $slot: selecting the SLOT volume"
    sendkeys "$slot" "s,l,meta_l+o"
    sleep 3   # the CD window has to open before type-select can hit the app
    echo "slot $slot: launching 'openttd - latest'"
    sendkeys "$slot" "o,p,meta_l+o"
    echo "slot $slot: launch keys sent"
}

cmd_screendump() {
    local slot="${1:?usage: pool.sh screendump <slot> <out.png>}"
    local out="${2:?usage: pool.sh screendump <slot> <out.png>}"
    local remote_ppm="$REMOTE_HOME/shot-slot$slot.ppm"
    qmp "$slot" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$remote_ppm\"}}" >/dev/null
    sleep 2   # QMP returns before the PPM is fully written
    local tmp_ppm
    tmp_ppm="$(mktemp /tmp/os9-slot$slot.XXXXXX).ppm"
    scp "${SSH_OPTS[@]}" "$REMOTE:$remote_ppm" "$tmp_ppm" >/dev/null
    if sips -s format png "$tmp_ppm" --out "$out" >/dev/null 2>&1; then
        rm -f "$tmp_ppm"
        echo "slot $slot screendump -> $out"
    else
        mv "$tmp_ppm" "${out%.png}.ppm"
        echo "slot $slot: sips could not convert; raw PPM kept at ${out%.png}.ppm"
    fi
}

# The root disk carries an explicit node-name (see run-slot.sh) because
# snapshot-save addresses block NODES, not -drive ids. Passing "hd0" here created
# a job that concluded with error "No block device node 'hd0'" — and since
# "concluded" is also how success looks, the old poll called that a saved
# snapshot. warm-create then produced a golden-warm.qcow2 with no snapshot in it.
VMSTATE_NODE=os9root

# job_wait <slot> <job-id> — poll until the job concludes, then FAIL if it
# concluded with an error. Always dismisses so the job-id can be reused.
job_wait() {
    local slot="$1" job="$2" out err
    for _ in $(seq 1 60); do
        out=$(qmp "$slot" '{"execute":"query-jobs"}')
        case "$out" in
            *"\"id\": \"$job\""*|*"\"id\":\"$job\""*) ;;
            *) sleep 2; continue ;;
        esac
        case "$out" in
            *'"status": "concluded"'*|*'"status":"concluded"'*)
                err=$(printf '%s' "$out" | python3 -c '
import json, sys
job = sys.argv[1]
for j in json.load(sys.stdin)["return"]:
    if j.get("id") == job: print(j.get("error") or "")
' "$job")
                qmp "$slot" "{\"execute\":\"job-dismiss\",\"arguments\":{\"id\":\"$job\"}}" >/dev/null 2>&1 || true
                [ -z "$err" ] || { echo "slot $slot: job $job FAILED: $err" >&2; return 1; }
                return 0 ;;
        esac
        sleep 2
    done
    echo "slot $slot: job $job did not conclude within 120s" >&2
    return 1
}

cmd_savevm() {
    local slot="${1:?usage: pool.sh savevm <slot> <name>}" name="${2:?usage: pool.sh savevm <slot> <name>}"
    qmp "$slot" "{\"execute\":\"snapshot-save\",\"arguments\":{\"job-id\":\"save$name\",\"tag\":\"$name\",\"vmstate\":\"$VMSTATE_NODE\",\"devices\":[\"$VMSTATE_NODE\"]}}" >/dev/null
    job_wait "$slot" "save$name" || return 1
    echo "slot $slot: snapshot '$name' saved"
}

cmd_loadvm() {
    local slot="${1:?usage: pool.sh loadvm <slot> <name>}" name="${2:?usage: pool.sh loadvm <slot> <name>}"
    qmp "$slot" "{\"execute\":\"snapshot-load\",\"arguments\":{\"job-id\":\"load$name\",\"tag\":\"$name\",\"vmstate\":\"$VMSTATE_NODE\",\"devices\":[\"$VMSTATE_NODE\"]}}" >/dev/null
    job_wait "$slot" "load$name" || return 1
    echo "slot $slot: snapshot '$name' loaded"
}

# cmd_warm_create — build golden-warm.qcow2: a copy of the golden image carrying a
# `warm` vmstate snapshot of an already-booted, window-free OS 9 desktop. Slots
# started with WARM=1 reflink-copy it and resume in seconds instead of booting.
#
# The seed boots WITH a payload CD and then ejects it from the Finder, so the
# snapshot captures a drive the CD driver has already bound with its tray open.
# Every resume inserts its own ISO into that drive — one warm image serves every
# slot and every build.
#
# The AFP share is unmounted before the snapshot too: a restored mount is a dead
# TCP connection that N resumed guests would all try to talk to at once.
cmd_warm_create() {
    local boot_wait="${BOOT_WAIT:-150}" slot=9   # slot 9: out of the way of the pool
    echo "== warm-create: seeding golden-warm.qcow2 from the golden image"
    ssh_ "rm -f $REMOTE_HOME/golden-warm.qcow2 && \
          cp --reflink=auto $REMOTE_HOME/golden-os9.qcow2 $REMOTE_HOME/golden-warm.qcow2"
    ssh_ "test -f $REMOTE_HOME/payload-slot$slot.iso" \
        || { echo "warm-create: need $REMOTE_HOME/payload-slot$slot.iso (run: payload.sh $slot)" >&2; return 2; }
    # Boot slot 9 COLD but off the warm image, so savevm writes into that file.
    ssh_ "tmux kill-session -t slot$slot" 2>/dev/null || true
    ssh_ "tmux new-session -d -s slot$slot 'cd $REMOTE_HOME && qemu-system-ppc \
        -machine mac99,via=pmu -cpu g4 -accel tcg -m 512 -name os9-warmseed \
        -device ide-hd,bus=ide.0,unit=0,drive=hd0,bootindex=0 \
        -drive if=none,media=disk,id=hd0,node-name=os9root,file=$REMOTE_HOME/golden-warm.qcow2 \
        -netdev user,id=net0 -device sungem,netdev=net0,mac=52:54:00:9a:bc:09 \
        -usb -device usb-mouse -device usb-kbd \
        -device ide-cd,bus=ide.1,unit=0,drive=pay0 \
        -drive if=none,media=cdrom,id=pay0,readonly=on,file=$REMOTE_HOME/payload-slot$slot.iso \
        -qmp tcp:127.0.0.1:$((4444 + slot)),server=on,wait=off -vnc :$slot 2>&1 \
        | tee $REMOTE_HOME/warmseed.log'"
    echo "== warm-create: waiting ${boot_wait}s for the desktop"
    sleep "$boot_wait"
    # Same tidy-up the launch recipe expects: dismiss the stale-alias dialog and
    # clear every Finder window, so each resume starts from an identical screen.
    sendkeys "$slot" "ret,meta_l+w,meta_l+w,meta_l+w,meta_l+w"
    sleep 2
    echo "== warm-create: unmounting the AFP share, then ejecting the CD"
    # "v", not "vintage": sendkeys spaces chords 0.35s apart, so a 7-character
    # type-select spans 2.45s and blows past OS 9's ~1.3s type-select timeout —
    # the buffer resets mid-word and the selection lands somewhere else. The first
    # attempt at this used "vintage" and silently left the share mounted, which is
    # what made warm resumes flaky: three guests resuming the same dead AFP session
    # each raise "the file server's connection has unexpectedly closed down" at
    # their own moment, and whichever one fires during the run wedges that guest.
    # "v" is unique among the desktop icons; "sl" is unique among share/Sherlock/SLOT.
    #
    # Cmd-Y (Put Away) for the share, Cmd-E (Eject) for the CD: in the OS 9 Finder
    # Eject only applies to ejectable media, so Cmd-E on a server volume is a no-op
    # — the second thing that left the share mounted in the warm image.
    sendkeys "$slot" "v,meta_l+y"
    sleep 5
    sendkeys "$slot" "s,l,meta_l+e"
    sleep 3
    case "$(qmp "$slot" '{"execute":"query-block"}')" in
        *'"tray_open": true'*|*'"tray_open":true'*) echo "== warm-create: CD ejected (tray open)" ;;
        *) echo "warm-create: FATAL the Finder did not eject the CD — snapshot would pin a stale medium" >&2
           qmp "$slot" '{"execute":"quit"}' >/dev/null 2>&1 || true; return 1 ;;
    esac
    cmd_screendump "$slot" /tmp/warm-seed-before-snapshot.png >/dev/null 2>&1 \
        && echo "== warm-create: pre-snapshot screen at /tmp/warm-seed-before-snapshot.png (no 'vintage' icon = clean)"
    echo "== warm-create: saving the 'warm' snapshot"
    cmd_savevm "$slot" warm
    qmp "$slot" '{"execute":"quit"}' >/dev/null 2>&1 || true
    sleep 3
    ssh_ "tmux kill-session -t slot$slot" 2>/dev/null || true
    ssh_ "qemu-img snapshot -l $REMOTE_HOME/golden-warm.qcow2"
    echo "== warm-create: golden-warm.qcow2 ready (start slots with WARM=1)"
}

cmd_list() {
    echo "slots up on $REMOTE:"
    ssh_ "tmux ls 2>/dev/null | grep '^slot' || echo '  (none)'"
}

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

case "${1:-}" in
    install)    cmd_install ;;
    start)      shift; cmd_start "$@" ;;
    stop)       shift; cmd_stop "$@" ;;
    status)     shift; cmd_status "$@" ;;
    qmp)        shift; cmd_qmp "$@" ;;
    key)        shift; cmd_key "$@" ;;
    launch)     shift; cmd_launch "$@" ;;
    screendump) shift; cmd_screendump "$@" ;;
    savevm)      shift; cmd_savevm "$@" ;;
    loadvm)      shift; cmd_loadvm "$@" ;;
    insert)      shift; cmd_insert "$@" ;;
    warm-create) cmd_warm_create ;;
    list)       cmd_list ;;
    *)          usage ;;
esac
