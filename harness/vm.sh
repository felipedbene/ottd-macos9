#!/usr/bin/env bash
# vm.sh — Mac-side driver for the Mac OS 9 QEMU VM on CT 100 "shellvirt".
#
# Usage:
#   vm.sh install                  push harness/remote/run-os9.sh to the CT
#   vm.sh start                    start QEMU in tmux session "os9" (as felipe)
#   vm.sh stop                     QMP quit, falling back to tmux kill-session
#   vm.sh halt                     stop + wait for qemu to die + release AFP lock (VM left OFF)
#   vm.sh reset                    QMP system_reset (guest reboots -> fresh run)
#   vm.sh status                   QMP query-status, raw JSON
#   vm.sh screendump <local.png>   QMP screendump -> scp -> sips PPM->PNG
#
# QMP is served by QEMU at 127.0.0.1:4444 inside the CT (see remote/run-os9.sh).
# Remote has only busybox nc (no socat/python3): nc closes on stdin EOF, so the
# QMP helper keeps stdin open with a trailing `sleep 2` to collect replies.

set -euo pipefail

SSH_KEY="$HOME/.ssh/id_openclaw"
SSH_OPTS=(-i "$SSH_KEY" -o IdentitiesOnly=yes)
REMOTE="root@10.0.2.8"
SSH="ssh ${SSH_OPTS[*]} $REMOTE"

HARNESS_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCAL_RUNNER="$HARNESS_DIR/remote/run-os9.sh"
REMOTE_RUNNER="/home/felipe/run-os9.sh"
TMUX_SESSION="os9"
QMP_ADDR="127.0.0.1 4444"

# FreeNAS/netatalk that serves the AFP "vintage" share. A power-cut QEMU never
# logs out of AFP, so netatalk keeps a zombie afpd holding the resource-fork
# locks on the deployed app -> the next boot's launch fails "in use". reset
# clears it by killing exactly the afpd(s) whose open fds point at the app's
# .AppleDouble entry (our Mac never runs the app, so it never matches -> safe).
FREENAS="root@10.0.1.148"
SHARE_APP=".AppleDouble/openttd - latest"

# qmp <json-command> — run one QMP command, print the raw JSON reply lines.
# The capabilities handshake is required before any command is accepted.
qmp() {
    local cmd="$1"
    local cap='{"execute":"qmp_capabilities"}'
    # busybox nc exits on stdin EOF; the sleep keeps the pipe open long enough
    # for QEMU to answer (screendump can take a moment).
    $SSH "( printf '%s\n%s\n' '$cap' '$cmd'; sleep 2 ) | nc $QMP_ADDR"
}

session_exists() {
    $SSH "su felipe -c 'tmux has-session -t $TMUX_SESSION' 2>/dev/null"
}

cmd_install() {
    scp "${SSH_OPTS[@]}" "$LOCAL_RUNNER" "$REMOTE:$REMOTE_RUNNER"
    $SSH "chown felipe:felipe $REMOTE_RUNNER && chmod +x $REMOTE_RUNNER"
    echo "installed $LOCAL_RUNNER -> $REMOTE:$REMOTE_RUNNER"
}

cmd_start() {
    if session_exists; then
        echo "tmux session '$TMUX_SESSION' already exists on remote; not starting another QEMU."
        return 0
    fi
    $SSH "su felipe -c \"tmux new-session -d -s $TMUX_SESSION 'cd /home/felipe && ./run-os9.sh'\""
    echo "started QEMU in tmux session '$TMUX_SESSION'"
}

cmd_stop() {
    if qmp '{"execute":"quit"}' | grep -q '"return"'; then
        echo "QEMU quit via QMP"
    else
        echo "QMP quit failed; killing tmux session '$TMUX_SESSION'"
        $SSH "su felipe -c 'tmux kill-session -t $TMUX_SESSION' 2>/dev/null" || true
    fi
}

# Release the stale AFP lock on the deployed app left by the power-cut guest.
# Kills only afpd processes with an open fd on the app's .AppleDouble entry.
release_share_lock() {
    ssh "${SSH_OPTS[@]}" "$FREENAS" '
        app=".AppleDouble/openttd - latest"
        killed=0
        for p in $(pgrep afpd 2>/dev/null); do
            if ls -la /proc/$p/fd 2>/dev/null | grep -qF -- "$app"; then
                kill "$p" 2>/dev/null && killed=$((killed+1))
            fi
        done
        echo "release_share_lock: killed $killed stale AFP session(s)"
    ' 2>&1 || echo "release_share_lock: WARN could not reach $FREENAS (continuing)"
}

# cmd_halt — stop the guest, wait for the tmux/qemu to die, release the AFP
# lock. Leaves the VM OFF. This is the prerequisite for deploying a new app:
# a running guest holds `openttd - latest` open over AFP, so the Mac-side mv
# fails "Resource busy" until the guest is gone AND its (now zombie) afpd is
# reaped. Used by both `reset` and the loop's pre-deploy step.
cmd_halt() {
    cmd_stop
    for _ in 1 2 3 4 5; do
        session_exists || break
        sleep 2
    done
    if session_exists; then
        echo "tmux session still alive after quit; killing it"
        $SSH "su felipe -c 'tmux kill-session -t $TMUX_SESSION' 2>/dev/null" || true
        sleep 2
    fi
    release_share_lock
}

cmd_reset() {
    # QMP system_reset does NOT actually reboot the mac99 guest (verified: the
    # RESET event fires but the OS 9 screen is untouched). A real fresh run
    # needs a cold QEMU restart: quit -> wait for the tmux session (which holds
    # the exec'd qemu) to die -> release the AFP lock -> start again.
    cmd_halt
    cmd_start
}

cmd_status() {
    qmp '{"execute":"query-status"}'
}

cmd_screendump() {
    local out="${1:?usage: vm.sh screendump <local.png>}"
    local remote_ppm="/home/felipe/shot.ppm"
    qmp "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$remote_ppm\"}}"
    sleep 2
    local tmp_base tmp_ppm
    tmp_base="$(mktemp /tmp/os9-shot.XXXXXX)"
    tmp_ppm="$tmp_base.ppm"
    scp "${SSH_OPTS[@]}" "$REMOTE:$remote_ppm" "$tmp_ppm"
    if sips -s format png "$tmp_ppm" --out "$out" >/dev/null 2>&1; then
        rm -f "$tmp_ppm" "$tmp_base"
        echo "screendump written to $out"
    else
        local ppm_out="${out%.png}.ppm"
        mv "$tmp_ppm" "$ppm_out"
        rm -f "$tmp_base"
        echo "sips could not convert the PPM; raw dump kept at $ppm_out"
    fi
}

usage() {
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

case "${1:-}" in
    install)    cmd_install ;;
    start)      cmd_start ;;
    stop)       cmd_stop ;;
    halt)       cmd_halt ;;
    reset)      cmd_reset ;;
    status)     cmd_status ;;
    screendump) shift; cmd_screendump "$@" ;;
    *)          usage ;;
esac
