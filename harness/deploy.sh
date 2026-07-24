#!/bin/bash
# harness/deploy.sh — build + deploy leg of the automated test harness.
#
#   deploy.sh            bump B2_BUILD_TAG (R1-NNN -> R1-NNN+1), build, deploy
#   deploy.sh --no-bump  build + deploy under the current tag
#
# Deploys to the /Volumes/vintage AFP share (FreeNAS; preserves resource forks
# + FinderInfo — verified):
#   /Volumes/vintage/ottdr1-<TAG>.bin   MacBinary archive copy
#   /Volumes/vintage/ottdr1-latest      double-clickable app (fixed name for the
#                                       guest auto-launcher): data fork = PEF,
#                                       resource fork + APPL FinderInfo intact.
set -euo pipefail

ROOT=/Users/felipe/ottd-macos9
R1=$ROOT/ottd-r1
MAIN=$R1/r1main.c
SHARE=/Volumes/vintage
APP_NAME="openttd - latest"   # name chosen on the OS 9 side; Startup Items alias points here

die() { echo "deploy.sh: FATAL: $*" >&2; exit 1; }

BUMP=1
for arg in "$@"; do
  case "$arg" in
    --no-bump) BUMP=0 ;;
    *) die "unknown argument: $arg (only --no-bump is supported)" ;;
  esac
done

# --- tag ---------------------------------------------------------------------
cur_tag=$(sed -n 's/^#define B2_BUILD_TAG "\(R1-[0-9]*\)".*/\1/p' "$MAIN")
[ -n "$cur_tag" ] || die "could not read B2_BUILD_TAG from $MAIN"

if [ "$BUMP" = 1 ]; then
  n=${cur_tag#R1-}
  new_tag="R1-$((n + 1))"
  sed -i '' "s/#define B2_BUILD_TAG \"$cur_tag\"/#define B2_BUILD_TAG \"$new_tag\"/" "$MAIN"
  grep -q "#define B2_BUILD_TAG \"$new_tag\"" "$MAIN" || die "tag bump sed failed in $MAIN"
  echo "== tag: $cur_tag -> $new_tag"
  TAG=$new_tag
else
  echo "== tag: $cur_tag (--no-bump)"
  TAG=$cur_tag
fi

# --- build -------------------------------------------------------------------
# NOTE: `build.sh compile`, NOT `all`: the `link` step of `all` is only a PROBE
# and segfaults on the EXPECTED b2_scene_init dup (b2_scene.o vs r1_scene.o;
# CMakeLists excludes b2_scene.o so the REAL cmake link below is clean). Any
# real compile error still aborts here (build.sh runs under set -e).
echo "== build: bash build.sh compile"
bash "$R1/build.sh" compile || die "build.sh compile FAILED"
echo "== build: cmake + make ottdr1_APPL"
( cd "$R1/build" && cmake . >/dev/null && make ottdr1_APPL ) || die "cmake/make ottdr1_APPL FAILED"

BIN=$R1/build/ottdr1.bin
APP=$R1/build/ottdr1.APPL
[ -f "$BIN" ] || die "missing build product $BIN"
[ -f "$APP" ] || die "missing build product $APP"

# --- deploy ------------------------------------------------------------------
mount | grep -q " on $SHARE " || die "$SHARE is not mounted — mount the vintage AFP share first"
[ -d "$SHARE" ] || die "$SHARE is not a directory"

# a) archive .bin under the tag
ARCHIVE=$SHARE/ottdr1-$TAG.bin
cp -f "$BIN" "$ARCHIVE"
echo "== archived $ARCHIVE"

# b) fixed-name app, atomically-ish: stage as .new, then mv over.
DST=$SHARE/$APP_NAME
STAGE=$SHARE/.$APP_NAME.new
rm -f "$STAGE"
cp -f "$APP" "$STAGE"   # macOS cp carries xattrs/rsrc/FinderInfo to AFP natively

rsrc_size() { stat -f %z "$1/..namedfork/rsrc" 2>/dev/null || echo 0; }
SRC_RSRC=$(rsrc_size "$APP")

if [ "$(rsrc_size "$STAGE")" != "$SRC_RSRC" ] || ! xattr "$STAGE" 2>/dev/null | grep -q com.apple.FinderInfo; then
  echo "== plain cp lost forks/FinderInfo on the share; falling back to explicit fork copy"
  rm -f "$STAGE"
  cp "$APP" "$STAGE"                                          # data fork
  cp "$APP/..namedfork/rsrc" "$STAGE/..namedfork/rsrc"        # resource fork
  xattr -wx com.apple.FinderInfo "4150504C 3F3F3F3F 00000000 00000000 00000000 00000000 00000000 00000000" "$STAGE"
fi

if ! mv -f "$STAGE" "$DST"; then
  echo "== mv over $DST failed (OS 9 may hold it open); retrying in 5s"
  sleep 5
  if ! mv -f "$STAGE" "$DST"; then
    echo "deploy.sh: WARNING: could not replace $DST — new app left at $STAGE" >&2
    exit 1
  fi
fi

# --- verify on the share -----------------------------------------------------
fail=0
check() { # label expected actual
  if [ "$2" = "$3" ]; then echo "  ✓ $1: $3"; else echo "  ✗ $1: expected $2, got $3"; fail=1; fi
}
check "archive .bin size"      "$(stat -f %z "$BIN")" "$(stat -f %z "$ARCHIVE")"
check "app data-fork size"     "$(stat -f %z "$APP")" "$(stat -f %z "$DST")"
check "app rsrc-fork size"     "$SRC_RSRC"            "$(rsrc_size "$DST")"
FINFO=$(xattr -px com.apple.FinderInfo "$DST" 2>/dev/null | head -1 | tr -d ' \n' | cut -c1-8 || true)
check "app FinderInfo APPL"    "4150504C"             "${FINFO:-<missing>}"
[ "$fail" = 0 ] || die "share verification FAILED"

# --- summary -----------------------------------------------------------------
echo "== deploy OK"
echo "   TAG:  $TAG"
echo "   bin:  $ARCHIVE ($(stat -f %z "$ARCHIVE") bytes)"
echo "   app:  $DST (data $(stat -f %z "$DST") bytes, rsrc $(rsrc_size "$DST") bytes, FinderInfo APPL)"
