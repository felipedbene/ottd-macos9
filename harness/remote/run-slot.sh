#!/bin/bash
# run-slot.sh <slot> — boot one Mac OS 9 pool slot on mactrash-can.
#
# Each slot is an independent QEMU:
#   disk 0   slot<N>.qcow2             the slot's root disk (see COLD vs WARM)
#   cd  0    payload-slot<N>.iso       HFS CD carrying THIS slot's build
#   QMP      127.0.0.1:$((4444 + N))
#   VNC      :$N  (port 5900 + N)
#   MAC      52:54:00:9a:bc:<N>        slots stay distinguishable on the wire
#
# The payload CD is what makes slots independent: without it every guest would
# race for the single `openttd - latest` on the AFP share and hold its lock. With
# it a slot's build travels on its own medium, so N slots run N different tags at
# once and no deploy has to halt anything but its own slot.
#
# COLD (default): the root disk is a fresh COW overlay on golden-os9.qcow2, so the
# slot rolls back to the golden image every start and OS 9 cold-boots (~65-85s).
#
# WARM=1: the root disk is an instant btrfs reflink copy of golden-warm.qcow2 and
# QEMU resumes its `warm` vmstate snapshot — an already-booted OS 9 desktop, no
# boot at all. Requires golden-warm.qcow2 (build it with `pool.sh warm-create`).
#
# COLD attaches the payload ISO at QEMU start (it is there before OS 9 scans the
# bus, so it is simply on the desktop at the end of boot).
#
# WARM must NOT attach it: the snapshot was taken with the tray OPEN, and the
# restored guest state has to agree with the host's. The ISO is inserted after the
# resume with `pool.sh insert`, which OS 9 mounts in ~10-25s.
set -e

SLOT="${1:?usage: run-slot.sh <slot-number>}"
case "$SLOT" in
    ''|*[!0-9]*) echo "run-slot.sh: slot must be a number, got '$SLOT'" >&2; exit 2 ;;
esac

HOME_DIR="/home/felipe"
GOLDEN="$HOME_DIR/golden-os9.qcow2"
GOLDEN_WARM="$HOME_DIR/golden-warm.qcow2"
DISK="$HOME_DIR/slot$SLOT.qcow2"
PAYLOAD_CD="$HOME_DIR/payload-slot$SLOT.iso"
MEM="512"
QMP_PORT=$((4444 + SLOT))
VNC_DISPLAY=":$SLOT"
WARM="${WARM:-0}"
WARM_TAG="${WARM_TAG:-warm}"

cd_args=(-device ide-cd,bus=ide.1,unit=0,drive=pay0)
if [ "$WARM" = 1 ]; then
    cd_args+=(-drive if=none,media=cdrom,id=pay0,readonly=on)
    echo "slot $SLOT: WARM — drive left empty, insert the payload after the resume"
elif [ -f "$PAYLOAD_CD" ]; then
    cd_args+=(-drive if=none,media=cdrom,id=pay0,file="$PAYLOAD_CD",readonly=on)
    echo "slot $SLOT: payload CD $PAYLOAD_CD attached at start"
else
    cd_args+=(-drive if=none,media=cdrom,id=pay0,readonly=on)
    echo "slot $SLOT: no payload CD ($PAYLOAD_CD absent) — drive present but empty"
fi

args=(
    -machine mac99,via=pmu
    -cpu g4
    -accel tcg
    -m "$MEM"
    -name "os9-slot$SLOT"
    -device ide-hd,bus=ide.0,unit=0,drive=hd0,bootindex=0
    -netdev user,id=net0
    -device sungem,netdev=net0,mac=52:54:00:9a:bc:0"$SLOT"
    -usb -device usb-mouse -device usb-kbd
    "${cd_args[@]}"
    -qmp tcp:127.0.0.1:$QMP_PORT,server=on,wait=off
    -vnc "$VNC_DISPLAY"
)

if [ "$WARM" = 1 ]; then
    [ -f "$GOLDEN_WARM" ] || { echo "run-slot.sh: WARM=1 but $GOLDEN_WARM is missing (run pool.sh warm-create)" >&2; exit 2; }
    # A reflink copy, not an overlay: vmstate lives in the top image, and an
    # overlay does not inherit its backing file's snapshots. On btrfs this is a
    # CoW clone, so copying 2.5GB costs nothing.
    rm -f "$DISK"
    cp --reflink=auto "$GOLDEN_WARM" "$DISK"
    args+=(-drive if=none,media=disk,id=hd0,node-name=os9root,file="$DISK" -loadvm "$WARM_TAG")
    echo "slot $SLOT: WARM resume of '$WARM_TAG' from $GOLDEN_WARM"
else
    [ -f "$GOLDEN" ] || { echo "run-slot.sh: missing golden base $GOLDEN" >&2; exit 2; }
    # Disposable overlay: a slot can never carry state (a half-written app, a
    # crashed Finder) from a previous iteration into the next one.
    rm -f "$DISK"
    qemu-img create -f qcow2 -b "$GOLDEN" -F qcow2 "$DISK" >/dev/null
    args+=(-drive if=none,media=disk,id=hd0,node-name=os9root,file="$DISK")
    echo "slot $SLOT: COLD boot from $GOLDEN"
fi

echo "slot $SLOT: QMP 127.0.0.1:$QMP_PORT, VNC $VNC_DISPLAY, disk $DISK"
exec qemu-system-ppc "${args[@]}"
