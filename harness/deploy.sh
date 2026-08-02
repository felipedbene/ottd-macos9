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
SHARE_DEPLOY=1
SET_TAG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-bump) BUMP=0 ;;
    # --no-share: bump + build only, skip the AFP copy. The slot pool carries the
    # build on a per-slot payload CD instead (harness/payload.sh), so the share is
    # not in the loop at all. Prints "TAG=<tag>" for the caller to read.
    --no-share) SHARE_DEPLOY=0 ;;
    # --tag <TAG>: build under an exact tag instead of bumping. Fan-out needs this:
    # concurrent slots all report to one sink that NATs every sender to the same
    # IP, so the tag is the ONLY thing telling two live runs apart. fanout.sh gives
    # each slot "<base>-s<N>". assert.py's banner regex already allows the suffix.
    --tag) shift; [ $# -gt 0 ] || die "--tag needs a value"; SET_TAG="$1" ;;
    *) die "unknown argument: $1 (supported: --no-bump, --no-share, --tag <TAG>)" ;;
  esac
  shift
done

# --- tag ---------------------------------------------------------------------
# Any tag shape is readable, not just R1-<n>: once fan-out writes "R1-125-s3" the
# old digits-only pattern would fail to read the file it just wrote.
cur_tag=$(sed -n 's/^#define B2_BUILD_TAG "\([^"]*\)".*/\1/p' "$MAIN")
[ -n "$cur_tag" ] || die "could not read B2_BUILD_TAG from $MAIN"

set_tag() { # set_tag <new>
  sed -i '' "s/#define B2_BUILD_TAG \"$cur_tag\"/#define B2_BUILD_TAG \"$1\"/" "$MAIN"
  grep -q "#define B2_BUILD_TAG \"$1\"" "$MAIN" || die "tag sed failed in $MAIN"
  # Drop r1main's object AND the link products so the tag change cannot be missed.
  # make compares whole seconds and treats "same second" as up-to-date, so two
  # builds inside one second (exactly what fanout.sh does) silently kept the OLD
  # tag: first the stale .obj, then — once that was deleted — a stale link, whose
  # .xcoff was written in the same second as the fresh .obj. Three "different"
  # fan-out builds came out byte identical. Deleting both ends of the chain makes
  # the rebuild unconditional; the post-build tag gate proves it worked.
  # Every output of the chain, not just some: the APPL step emits .pef/.ad/%.ad
  # alongside .bin/.APPL, and make skipped the whole custom command while any one
  # of them still looked current.
  rm -f "$R1/build/CMakeFiles/ottdr1.dir/r1main.c.obj" \
        "$R1/build/ottdr1.xcoff" "$R1/build/ottdr1.pef" "$R1/build/ottdr1.bin" \
        "$R1/build/ottdr1.APPL" "$R1/build/ottdr1.ad" "$R1/build/%ottdr1.ad"
}

if [ -n "$SET_TAG" ]; then
  if [ "$SET_TAG" = "$cur_tag" ]; then
    echo "== tag: $cur_tag (--tag, unchanged)"
  else
    set_tag "$SET_TAG"
    echo "== tag: $cur_tag -> $SET_TAG (--tag)"
  fi
  TAG=$SET_TAG
elif [ "$BUMP" = 1 ]; then
  # Bump the numeric part only, dropping ANY suffix (fan-out writes "-s3", ad-hoc
  # builds write whatever), so a normal iteration after a fan-out returns to the
  # plain R1-<n+1> series instead of choking on a non-numeric tail.
  n=$(printf '%s' "$cur_tag" | sed -E 's/^R1-([0-9]+).*/\1/')
  case "$n" in
    ''|*[!0-9]*) die "cannot read a numeric build number from tag '$cur_tag' — pass --tag explicitly" ;;
  esac
  new_tag="R1-$((n + 1))"
  set_tag "$new_tag"
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

# The tag is the harness's only handle on a run: assert.py slices the sink by the
# banner, and concurrent slots are told apart by nothing else. A binary carrying
# the wrong tag is therefore worse than a build failure — it reports green under
# someone else's name. Prove the tag we just set is the tag that got compiled.
# Count rather than `grep -q`: under `set -o pipefail`, grep -q closes the pipe on
# the first match, `strings` dies of SIGPIPE, and the pipeline reports failure —
# which made this gate fail every build it was supposed to pass.
tag_hits=$(strings "$APP" 2>/dev/null | grep -cxF "$TAG" || true)
[ "${tag_hits:-0}" -gt 0 ] || die "built app does NOT contain tag '$TAG' — the compile did not pick up the tag change"
echo "== build carries tag $TAG (verified in the binary)"

# --- deploy ------------------------------------------------------------------
if [ "$SHARE_DEPLOY" = 0 ]; then
  echo "== build OK (--no-share: skipping the AFP copy)"
  echo "   APP: $APP"
  echo "TAG=$TAG"
  exit 0
fi

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
