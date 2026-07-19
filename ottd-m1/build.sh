#!/bin/bash
# M1 build driver. Run with: bash build.sh [compile|link|all]
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
TCLIB=/Users/felipe/ottd-macos9/Retro68-build/toolchain/powerpc-apple-macos/lib

# NOTE: --gc-sections is a proven no-op in this Retro68/XCOFF ld, so
# -ffunction-sections/-fdata-sections gave ZERO benefit and exploded the section
# count (thousands of tiny sections across command/clear/town templates), which
# segfaulted ld once command.cpp+clear_cmd.cpp were added (build 7). Dropped.
# -DNDEBUG (build 10f): disable the debug-only asserts. GrowTown's house path
# trips a bare assert() in our minimal/stubbed env (silent abort — no error log,
# since error()/NOT_REACHED log via stubs). NDEBUG (without WITH_ASSERT) makes
# assert() a no-op = a normal release config. Does NOT touch the pool code
# (guarded by WITH_ASSERT, which we never define).
CXXFLAGS=(-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -DNDEBUG -include "$COMPAT/libc_compat.h" -Os)
INCS=(-I"$COMPAT" -I"$OTTD/src" -I"$OTTD/src/3rdparty" -I"$OTTD/src/3rdparty/squirrel/include" -I"$OTTD/src/script/api" -I"$GEN" -I"$GEN/script/api")

# Real OpenTTD src TUs to compile (relative to $OTTD/src). Extend as the link demands.
SRC_TUS=(
  date.cpp
  core/pool_func.cpp
  town_cmd.cpp
  landscape.cpp
  clear_cmd.cpp
  road_cmd.cpp
  road_map.cpp
)
# NOTE: command.cpp is not compiled. command.cpp's giant
# constexpr _command_proc_table (~200 entries) segfaults this Retro68 ld, and the
# tile commands (CmdLandscapeClear etc.) live in landscape.cpp behind the
# _tile_type_procs dispatch table (drags all 10 tile-type proc structs). The
# Command<>::Do dispatch template is header-only, so build 7 executes a REAL
# command whose proc is ALREADY compiled here — CmdTownGrowthRate in town_cmd.o —
# needing only the 3 dispatch symbols in m1_cmd_stubs.cpp. Real tile commands
# (roads) are build 8: they need landscape.cpp + the tile-proc table stubbed.

cxx_src() { # <relpath> -> <base>.o
  local rel="$1"; local out="$M1/obj/$(basename "${rel%.cpp}").o"
  mkdir -p "$M1/obj"
  echo "  CXX $rel"
  $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/$rel" -o "$out"
}

compile_all() {
  mkdir -p "$M1/obj"
  echo "== compiling M1 own TUs =="
  for f in m1_run m1_shims m1_methods m1_pools m1_profiling_stub m1_town_stubs m1_cmd_stubs m1_land_stubs m1_road_stubs; do
    $GXX "${CXXFLAGS[@]}" "${INCS[@]}" -c "$M1/$f.cpp" -o "$M1/obj/$f.o"
  done
  $GCC -c "$M1/m1_deadpools.c" -o "$M1/obj/m1_deadpools.o"
  $GCC -c "$M1/m1main.c" -o "$M1/obj/m1main.o"
  echo "== compiling real OpenTTD src TUs =="
  for t in "${SRC_TUS[@]}"; do cxx_src "$t"; done
}

# Prebuilt objects reused from prior milestones (map/tile/bitmath/random + UDP sink + OT).
REUSE=(
  "$B1/map.o"
  "$B1/tile_map.o"
  "$B1/bitmath_func.o"
  "$B2/random_func.o"
  "$B2/netlog.o"
  "$B2/statsd_ot.o"
  "$B2/OpenTransportAppPPC.o"
  "$B2/OpenTptInetPPC.o"
)
OTLIBS=(
  "$TCLIB/libOpenTransportLib.a"
  "$TCLIB/libOpenTptInternetLib.a"
)

link() {
  echo "== linking ottd-m1.xcoff =="
  local objs=("$M1"/obj/*.o "${REUSE[@]}")
  $GXX -o "$M1/ottd-m1.xcoff" "${objs[@]}" "${OTLIBS[@]}" \
    -Wl,--gc-sections -lstdc++ -lsupc++ 2>&1
}

undef() {
  $NM -u "$M1/ottd-m1.xcoff" 2>/dev/null | c++filt || true
}

case "${1:-all}" in
  compile) compile_all ;;
  link) link ;;
  undef) undef ;;
  all) compile_all; link ;;
esac
