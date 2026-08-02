#!/usr/bin/env bash
# payload.sh <slot> [app] — build slot <slot>'s payload CD and ship it to the pool.
#
# A slot gets its build on a private HFS CD-ROM image:
#
#     SLOT:openttd - latest     the app (data fork + resource fork + APPL FinderInfo)
#     SLOT:ogfx1_base.grf       game data the app opens relative to itself
#     SLOT:english.lng
#
# Why a CD and not a hard disk: OS 9 will not mount a bare hdiutil-made HFS+ hard
# disk (it lacks the Apple_Driver_ATA partition that Drive Setup writes), but it
# mounts an HFS CD-ROM with no driver at all — verified on slot 2. The CD is also
# swappable at runtime via QMP, so a slot can take a new build without a restart.
#
# Every slot's volume is named SLOT, so the in-guest launch recipe is identical
# for all slots while the build behind it differs per slot. That is what lets N
# slots run N different tags at once with no shared AFP app and no lock to release.
#
# Built on this Mac because this is where the real forks live — `ditto` carries
# the resource fork and FinderInfo into the HFS volume verbatim (`cp` does not).
set -euo pipefail

SLOT="${1:?usage: payload.sh <slot> [path/to/app.APPL]}"
case "$SLOT" in
    ''|*[!0-9]*) echo "payload.sh: slot must be a number, got '$SLOT'" >&2; exit 2 ;;
esac

ROOT=/Users/felipe/ottd-macos9
HARNESS="$ROOT/harness"
APP="${2:-$ROOT/ottd-r1/build/ottdr1.APPL}"
APP_NAME="openttd - latest"
ASSETS="$HARNESS/assets"
VOLNAME=SLOT
APPL_FINDERINFO="4150504C 3F3F3F3F 00000000 00000000 00000000 00000000 00000000 00000000"

SSH_KEY="$HOME/.ssh/id_openclaw"
SSH_OPTS=(-i "$SSH_KEY" -o IdentitiesOnly=yes)
REMOTE="felipe@10.0.1.53"
REMOTE_ISO="/home/felipe/payload-slot$SLOT.iso"

die() { echo "payload.sh: FATAL: $*" >&2; exit 1; }

[ -f "$APP" ] || die "missing app $APP (build first)"

# Static game data the app opens by name from its own directory. Cached in
# harness/assets/ (gitignored) so a payload build needs no AFP share at all —
# the whole point of moving off it. Seed once from the share if absent.
for f in ogfx1_base.grf english.lng; do
    [ -f "$ASSETS/$f" ] || die "missing $ASSETS/$f — seed it once with: cp /Volumes/vintage/$f $ASSETS/"
done

WORK=$(mktemp -d "/tmp/payload-slot$SLOT.XXXXXX")
STAGE="$WORK/stage"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$STAGE"

echo "== payload slot $SLOT: staging $(basename "$APP") + game data"
ditto "$APP" "$STAGE/$APP_NAME"
xattr -wx com.apple.FinderInfo "$APPL_FINDERINFO" "$STAGE/$APP_NAME"
cp -f "$ASSETS/ogfx1_base.grf" "$ASSETS/english.lng" "$STAGE/"

# makehybrid -hfs writes a real HFS filesystem, carrying both forks. It always
# appends .dmg to -o, so name the target accordingly rather than fighting it.
ISO="$WORK/payload.dmg"
hdiutil makehybrid -hfs -hfs-volume-name "$VOLNAME" -o "$WORK/payload" "$STAGE" >/dev/null

# Verify by mounting what we actually built, not what we intended to build.
DEV=$(hdiutil attach -nobrowse "$ISO" | head -1 | awk '{print $1}')
[ -n "$DEV" ] || die "could not attach the built image"
MP="/Volumes/$VOLNAME"
trap 'hdiutil detach "$DEV" >/dev/null 2>&1 || true; rm -rf "$WORK"' EXIT

fail=0
check() { if [ "$2" = "$3" ]; then echo "  ✓ $1: $3"; else echo "  ✗ $1: expected $2, got $3"; fail=1; fi; }
check "data fork" "$(stat -f %z "$APP")" "$(stat -f %z "$MP/$APP_NAME")"
check "rsrc fork" "$(stat -f %z "$APP/..namedfork/rsrc" 2>/dev/null || echo 0)" \
                  "$(stat -f %z "$MP/$APP_NAME/..namedfork/rsrc" 2>/dev/null || echo 0)"
FINFO=$(xattr -px com.apple.FinderInfo "$MP/$APP_NAME" 2>/dev/null | head -1 | tr -d ' \n' | cut -c1-8 || true)
check "FinderInfo APPL" "4150504C" "${FINFO:-<missing>}"
check "ogfx1_base.grf" "$(stat -f %z "$ASSETS/ogfx1_base.grf")" "$(stat -f %z "$MP/ogfx1_base.grf")"
check "english.lng"    "$(stat -f %z "$ASSETS/english.lng")"    "$(stat -f %z "$MP/english.lng")"

hdiutil detach "$DEV" >/dev/null
trap 'rm -rf "$WORK"' EXIT
[ "$fail" = 0 ] || die "payload verification FAILED"

echo "== payload slot $SLOT: shipping to $REMOTE:$REMOTE_ISO"
scp "${SSH_OPTS[@]}" "$ISO" "$REMOTE:$REMOTE_ISO" >/dev/null
ssh "${SSH_OPTS[@]}" "$REMOTE" "ls -la $REMOTE_ISO"
echo "== payload slot $SLOT ready"
