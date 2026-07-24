#!/bin/sh
# Wrapper for booting the Mac OS 9.2 qemu-system-ppc guest.
#
# Usage:
#   ./run-os9.sh install   -> boots the boot-kit CD image + target disk,
#                              use this once to run Drive Setup + install/restore
#   ./run-os9.sh           -> normal boot, straight into the installed target disk

set -e

BOOTKIT="2GB-OS9-USB-Boot-Kit-v2.dsk"
TARGET="os9-target.qcow2"
MEM="512"
VNC_DISPLAY=":0"

if [ ! -f "$TARGET" ]; then
    echo "Target disk $TARGET not found -- creating a 4G qcow2..."
    qemu-img create -f qcow2 "$TARGET" 4G
fi

if [ "$1" = "install" ]; then
    echo "Starting in INSTALL mode (booting from $BOOTKIT as CD-ROM)..."
    exec qemu-system-ppc \
        -machine mac99,via=pmu \
        -cpu g4 \
        -accel tcg \
        -m "$MEM" \
        -device ide-cd,bus=ide.0,unit=0,drive=cd0,bootindex=0 \
        -drive if=none,media=cdrom,id=cd0,file="$BOOTKIT",readonly=on \
        -device ide-hd,bus=ide.0,unit=1,drive=hd0,bootindex=1 \
        -drive if=none,media=disk,id=hd0,file="$TARGET" \
        -netdev user,id=net0 -device sungem,netdev=net0 \
        -usb -device usb-mouse -device usb-kbd \
        -qmp tcp:127.0.0.1:4444,server=on,wait=off \
        -vnc "$VNC_DISPLAY"
else
    echo "Starting normal boot (from $TARGET)..."
    exec qemu-system-ppc \
        -machine mac99,via=pmu \
        -cpu g4 \
        -accel tcg \
        -m "$MEM" \
        -device ide-hd,bus=ide.0,unit=0,drive=hd0,bootindex=0 \
        -drive if=none,media=disk,id=hd0,file="$TARGET" \
        -netdev user,id=net0 -device sungem,netdev=net0 \
        -usb -device usb-mouse -device usb-kbd \
        -qmp tcp:127.0.0.1:4444,server=on,wait=off \
        -vnc "$VNC_DISPLAY"
fi
