#!/bin/bash
# R1 (render-merge) build: M1's real engine + B2/b1's sprite/render pipeline in
# ONE binary, so the grown town renders on screen. gfx.o owns the 10 shared
# game-state/draw symbols (M1 sources compiled with -DR1_MERGE drop their dups).
# -DNDEBUG disables the debug asserts (build-10 lesson). Game-side .o built here;
# mac-side TUs (r1main.c etc.) compiled by cmake with the multiversal headers.
set -e
TC=/Users/felipe/ottd-macos9/Retro68-build/toolchain/bin
GXX=$TC/powerpc-apple-macos-g++
GCC=$TC/powerpc-apple-macos-gcc
NM=$TC/powerpc-apple-macos-nm
COMPAT=/Users/felipe/ottd-macos9/compat
OTTD=/Users/felipe/ottd-macos9/openttd-13.4
GEN=$OTTD/build-native/generated
M1=/Users/felipe/ottd-macos9/ottd-m1
B1=/Users/felipe/ottd-macos9/ottd-b1
B2=/Users/felipe/ottd-macos9/ottd-b2
R1=/Users/felipe/ottd-macos9/ottd-r1
TCLIB=/Users/felipe/ottd-macos9/Retro68-build/toolchain/powerpc-apple-macos/lib

CXXFLAGS=(-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS -DNDEBUG -include "$COMPAT/libc_compat.h" -Os)
INCS=(-I"$COMPAT" -I"$OTTD/src" -I"$OTTD/src/3rdparty" -I"$OTTD/src/3rdparty/squirrel/include" -I"$OTTD/src/script/api" -I"$GEN" -I"$GEN/script/api")

# --- mtime gating -----------------------------------------------------------
# up_to_date OUT DEP...  -> success (0) iff OUT exists and is newer than every
# DEP, this script, and libc_compat.h (the -include header every TU sees).
# FORCE=1 bash build.sh all  bypasses all gating.
# NOTE: used only as an explicit `if` condition so `set -e` still aborts on a
# real compile failure; compiles are never piped through anything.
BUILD_SH="${BASH_SOURCE[0]}"
up_to_date() {
  [ "${FORCE:-0}" = "1" ] && return 1
  local out="$1"; shift
  [ -f "$out" ] || return 1
  local d
  for d in "$@" "$BUILD_SH" "$COMPAT/libc_compat.h"; do
    [ "$out" -nt "$d" ] || return 1
  done
  return 0
}

# Real OpenTTD engine TUs (recompiled with the merge flags).
# viewport.cpp = the game's real renderer; void_cmd.cpp = the MP_VOID tile proc it
# dereferences for off-map/border tiles (replaces the zeroed void stub in deadpools).
SRC_TUS=(date.cpp core/pool_func.cpp town_cmd.cpp landscape.cpp clear_cmd.cpp road_cmd.cpp road_map.cpp void_cmd.cpp gfx_layout.cpp fontcache.cpp fontcache/spritefontcache.cpp tree_cmd.cpp townname.cpp widgets/dropdown.cpp toolbar_gui.cpp cargotype.cpp)
# viewport.cpp is compiled from a PATCHED copy (below): DoSetViewportPosition forced to full-redraw
# instead of GfxScroll's in-place _screen memmove (which tears the Mac's single QuickDraw buffer on
# drag-to-pan, vertical especially). Handled separately so the sed patch applies.
# M1 support TUs (shims/stubs/pools) — engine-calibrated, minus the gfx-owned dups.
# m1_viewport_stubs = no-op window/vehicle/sign surface viewport.cpp links against.
# m1_text_stubs = the 4 symbols the real font/layout TUs need (config/utf8/glyphs).
M1_TUS=(m1_shims m1_methods m1_pools m1_profiling_stub m1_town_stubs m1_cmd_stubs m1_land_stubs m1_road_stubs m1_viewport_stubs m1_text_stubs m1_world_stubs m1_water_draw m1_industry_draw m1_window_stubs m1_strings_stubs m1_toolbar_stubs m1_company m1_vehicle m1_economy m1_finance_gui m1_industry m1_town_directory_gui m1_station m1_company_gui m1_station_gui m1_vehicle_list_gui m1_graph_gui m1_station_draw m1_order m1_pathfind m1_town_gui m1_industry_gui m1_smallmap_gui m1_subsidy_gui m1_rail_draw m1_train)

compile_all() {
  mkdir -p "$R1/obj"
  echo "== engine src TUs =="
  for t in "${SRC_TUS[@]}"; do
    local out="$R1/obj/$(basename ${t%.cpp}).o"
    if up_to_date "$out" "$OTTD/src/$t"; then
      echo "  skip (up-to-date) $t"
    else
      echo "  CXX $t"; $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/$t" -o "$out"
    fi
  done
  echo "== M1 support TUs (from ottd-m1) =="
  for f in "${M1_TUS[@]}"; do
    if up_to_date "$R1/obj/$f.o" "$M1/$f.cpp"; then
      echo "  skip (up-to-date) $f"
    else
      echo "  CXX $f"; $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$M1/$f.cpp" -o "$R1/obj/$f.o"
    fi
  done
  if up_to_date "$R1/obj/m1_deadpools.o" "$M1/m1_deadpools.c"; then
    echo "  skip (up-to-date) m1_deadpools"
  else
    echo "  CC  m1_deadpools"; $GCC -DR1_MERGE -DR1_STRINGS -c "$M1/m1_deadpools.c" -o "$R1/obj/m1_deadpools.o"
  fi
  echo "== viewport.cpp (DoSetViewportPosition forced to full-redraw: no GfxScroll in-place _screen"
  echo "   memmove, which tears the Mac's single QuickDraw buffer on drag-to-pan, vertical especially) =="
  if up_to_date "$R1/obj/viewport.o" "$OTTD/src/viewport.cpp"; then
    echo "  skip (up-to-date) viewport"
  else
    echo "  CXX viewport"
    # Force the 'fully_outside' full-redraw branch for EVERY scroll by making its guard always true.
    sed 's#if (abs(xo) >= width || abs(yo) >= height) {#if (true) { /* R1: always full-redraw, no GfxScroll tearing */#' \
        "$OTTD/src/viewport.cpp" > "$R1/obj/viewport_patched.cpp"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/viewport_patched.cpp" -o "$R1/obj/viewport.o"
  fi
  echo "== window system (widget.cpp; window.cpp with the broken steady_clock sed'd to R1SteadyClock) =="
  if up_to_date "$R1/obj/widget.o" "$OTTD/src/widget.cpp"; then
    echo "  skip (up-to-date) widget"
  else
    echo "  CXX widget"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/widget.cpp" -o "$R1/obj/widget.o"
  fi
  if up_to_date "$R1/obj/window.o" "$OTTD/src/window.cpp" "$R1/r1_winclock.h"; then
    echo "  skip (up-to-date) window"
  else
    echo "  CXX window"
    sed 's/std::chrono::steady_clock/R1SteadyClock/g' "$OTTD/src/window.cpp" > "$R1/obj/window_patched.cpp"
    $GXX "${CXXFLAGS[@]}" -include "$R1/r1_winclock.h" "${INCS[@]}" -c "$R1/obj/window_patched.cpp" -o "$R1/obj/window.o"
  fi
  echo "== strings.cpp (real string system; ReadLanguagePack strrchr made null-safe for bare filenames) =="
  if up_to_date "$R1/obj/strings.o" "$OTTD/src/strings.cpp"; then
    echo "  skip (up-to-date) strings"
  else
    echo "  CXX strings"
    sed -e 's|strrchr(_current_language->file, PATHSEPCHAR) + 1|(strrchr(_current_language->file, PATHSEPCHAR) ? strrchr(_current_language->file, PATHSEPCHAR) + 1 : _current_language->file)|' \
        -e 's|void CheckForMissingGlyphs(bool base_font, MissingGlyphSearcher \*searcher)|void CheckForMissingGlyphs_R1UNUSED(bool base_font, MissingGlyphSearcher *searcher)|' \
        "$OTTD/src/strings.cpp" > "$R1/obj/strings_patched.cpp"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/strings_patched.cpp" -o "$R1/obj/strings.o"
  fi
  echo "== R1 scene (real-map render + engine world-build) =="
  if up_to_date "$R1/obj/r1_scene.o" "$R1/r1_scene.cpp"; then
    echo "  skip (up-to-date) r1_scene"
  else
    echo "  CXX r1_scene"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/r1_scene.cpp" -o "$R1/obj/r1_scene.o"
  fi
  # R1-local b2_shims.o: same B2 source, but -DR1_MERGE (in CXXFLAGS) turns on the
  # live-engine hook (GameLoop->r1_tick) + per-frame redraw. Replaces ${B2}/b2_shims.o
  # in the R1 link (CMakeLists no longer lists that one — it comes from obj/*.o).
  echo "== R1-local b2_shims (live-tick hook) =="
  if up_to_date "$R1/obj/b2_shims.o" "$B2/b2_shims.cpp"; then
    echo "  skip (up-to-date) b2_shims"
  else
    echo "  CXX b2_shims"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B2/b2_shims.cpp" -o "$R1/obj/b2_shims.o"
  fi
  # R1-local b1_shims: -DR1_MERGE drops its Layouter/GetCharacterHeight stubs so the
  # REAL gfx_layout.cpp/fontcache.cpp own them (text rendering). Replaces ${B1}/b1_shims.o.
  echo "== R1-local b1_shims (real-text dedup) =="
  if up_to_date "$R1/obj/b1_shims.o" "$B1/b1_shims.cpp"; then
    echo "  skip (up-to-date) b1_shims"
  else
    echo "  CXX b1_shims"
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B1/b1_shims.cpp" -o "$R1/obj/b1_shims.o"
  fi
}

# netlog.o is not produced by any cmake target and is covered by .gitignore's *.o,
# so nothing regenerates it: a fresh clone links against a file that does not exist,
# and an edit to netlog.c silently links the previous object (or fails with
# "undefined reference to .ottd_log_set_tag" if the edit added a symbol). It needs
# Apple's Universal Interfaces for <OpenTransport.h>, which the cmake build does not
# have, hence its own script. Rebuild it whenever it is missing or stale.
if [ ! -f "$B2/netlog.o" ] || [ "$B1/netlog.c" -nt "$B2/netlog.o" ]; then
  echo "== netlog.o missing or stale -> ottd-b1/build-netlog.sh =="
  bash "$B1/build-netlog.sh"
fi

# render objects reused from b1/b2 (already compiled).
RENDER=(
  "$B2/macclassic_v.o" "$B2/b2_scene.o" "$B2/video_driver.o" "$B2/driver.o" "$B2/random_func.o"
  "$B1/gfx.o" "$B1/spritecache.o" "$B1/grf.o" "$B1/sprite_file.o" "$B1/random_access_file.o"
  "$B1/8bpp_simple.o" "$B1/8bpp_base.o" "$B1/tile_map.o" "$B1/map.o" "$B1/bitmath_func.o"
  "$B2/netlog.o" "$B2/statsd_ot.o" "$B2/OpenTransportAppPPC.o" "$B2/OpenTptInetPPC.o"
)
OTLIBS=("$TCLIB/libOpenTransportLib.a" "$TCLIB/libOpenTptInternetLib.a")

link() {   # probe link: reveal dup (multiple def) + undefined surface (may segfault on dup)
  echo "== probe link r1.xcoff =="
  $GXX -o "$R1/r1.xcoff" "$R1"/obj/*.o "${RENDER[@]}" "${OTLIBS[@]}" -lstdc++ -lsupc++ 2>&1
}

case "${1:-all}" in
  compile) compile_all ;;
  link) link ;;
  all) compile_all; link ;;
esac
