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

# Real OpenTTD engine TUs (recompiled with the merge flags).
# viewport.cpp = the game's real renderer; void_cmd.cpp = the MP_VOID tile proc it
# dereferences for off-map/border tiles (replaces the zeroed void stub in deadpools).
SRC_TUS=(date.cpp core/pool_func.cpp town_cmd.cpp landscape.cpp clear_cmd.cpp road_cmd.cpp road_map.cpp viewport.cpp void_cmd.cpp gfx_layout.cpp fontcache.cpp fontcache/spritefontcache.cpp tree_cmd.cpp townname.cpp widgets/dropdown.cpp toolbar_gui.cpp cargotype.cpp)
# M1 support TUs (shims/stubs/pools) — engine-calibrated, minus the gfx-owned dups.
# m1_viewport_stubs = no-op window/vehicle/sign surface viewport.cpp links against.
# m1_text_stubs = the 4 symbols the real font/layout TUs need (config/utf8/glyphs).
M1_TUS=(m1_shims m1_methods m1_pools m1_profiling_stub m1_town_stubs m1_cmd_stubs m1_land_stubs m1_road_stubs m1_viewport_stubs m1_text_stubs m1_world_stubs m1_water_draw m1_industry_draw m1_window_stubs m1_strings_stubs m1_toolbar_stubs m1_company m1_vehicle m1_economy m1_finance_gui)

compile_all() {
  mkdir -p "$R1/obj"
  echo "== engine src TUs =="
  for t in "${SRC_TUS[@]}"; do
    echo "  CXX $t"; $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/$t" -o "$R1/obj/$(basename ${t%.cpp}).o"
  done
  echo "== M1 support TUs (from ottd-m1) =="
  for f in "${M1_TUS[@]}"; do
    echo "  CXX $f"; $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$M1/$f.cpp" -o "$R1/obj/$f.o"
  done
  echo "  CC  m1_deadpools"; $GCC -DR1_MERGE -DR1_STRINGS -c "$M1/m1_deadpools.c" -o "$R1/obj/m1_deadpools.o"
  echo "== window system (widget.cpp; window.cpp with the broken steady_clock sed'd to R1SteadyClock) =="
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/widget.cpp" -o "$R1/obj/widget.o"
  sed 's/std::chrono::steady_clock/R1SteadyClock/g' "$OTTD/src/window.cpp" > "$R1/obj/window_patched.cpp"
  $GXX "${CXXFLAGS[@]}" -include "$R1/r1_winclock.h" "${INCS[@]}" -c "$R1/obj/window_patched.cpp" -o "$R1/obj/window.o"
  echo "== strings.cpp (real string system; ReadLanguagePack strrchr made null-safe for bare filenames) =="
  sed -e 's|strrchr(_current_language->file, PATHSEPCHAR) + 1|(strrchr(_current_language->file, PATHSEPCHAR) ? strrchr(_current_language->file, PATHSEPCHAR) + 1 : _current_language->file)|' \
      -e 's|void CheckForMissingGlyphs(bool base_font, MissingGlyphSearcher \*searcher)|void CheckForMissingGlyphs_R1UNUSED(bool base_font, MissingGlyphSearcher *searcher)|' \
      "$OTTD/src/strings.cpp" > "$R1/obj/strings_patched.cpp"
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/strings_patched.cpp" -o "$R1/obj/strings.o"
  echo "== R1 scene (real-map render + engine world-build) =="
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/r1_scene.cpp" -o "$R1/obj/r1_scene.o"
  # R1-local b2_shims.o: same B2 source, but -DR1_MERGE (in CXXFLAGS) turns on the
  # live-engine hook (GameLoop->r1_tick) + per-frame redraw. Replaces ${B2}/b2_shims.o
  # in the R1 link (CMakeLists no longer lists that one — it comes from obj/*.o).
  echo "== R1-local b2_shims (live-tick hook) =="
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B2/b2_shims.cpp" -o "$R1/obj/b2_shims.o"
  # R1-local b1_shims: -DR1_MERGE drops its Layouter/GetCharacterHeight stubs so the
  # REAL gfx_layout.cpp/fontcache.cpp own them (text rendering). Replaces ${B1}/b1_shims.o.
  echo "== R1-local b1_shims (real-text dedup) =="
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B1/b1_shims.cpp" -o "$R1/obj/b1_shims.o"
}

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
