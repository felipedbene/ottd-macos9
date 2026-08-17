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

# -frandom-seed=r1: without it g++ salts per-compile random bits into generated
# symbol names (measured: ground_vehicle.o's _GLOBAL__F_..._0x<hash> differed on
# every rebuild), so "same tree -> same binary" never held. A fixed seed is safe
# here: every seeded name already embeds the TU's own path, so TUs can't collide.
CXXFLAGS=(-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS -DR1_REAL_VEHICLE_STACK -DR1_REAL_ECONOMY -DNDEBUG -include "$COMPAT/libc_compat.h" -Os -frandom-seed=r1)
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
# REAL VEHICLE STACK (R1-139). roadveh_cmd/vehicle/engine/order_cmd/roadstop/rail/
# ground_vehicle replace the hand-rolled RoadVehicle/Order/Engine stubs that stood in
# for them (those are now behind #ifndef R1_REAL_VEHICLE_STACK). YAPF is NOT linked:
# roadveh_cmd reaches the pathfinder through exactly two functions, both backed by the
# BFS in m1_pathfind.cpp -- see ottd-m1/m1_realveh_stubs.cpp.
# NOTE: do NOT add cargopacket.cpp here. It drags in the CargoAction operators and
# FlowStat and collides with the hand-rolled CargoPacket specializations in
# m1_station.cpp -- measured: it makes the link surface worse, not better.
# economy.cpp (R1-176 rung 4): measured surface was 1 game symbol
# (_story_page_element_pool -> deadpool) + libstdc++-resolved EH runtime; the 11
# hand-rolled duplicates moved behind #ifndef R1_REAL_ECONOMY. cargopacket.cpp
# stays OUT (the warning above still holds); economy's cargo-list calls resolve
# to m1_station.cpp's specializations.
SRC_TUS=(roadveh_cmd.cpp vehicle.cpp engine.cpp order_cmd.cpp roadstop.cpp rail.cpp ground_vehicle.cpp \
         core/pool_func.cpp town_cmd.cpp landscape.cpp clear_cmd.cpp road_cmd.cpp road_map.cpp void_cmd.cpp gfx_layout.cpp fontcache.cpp fontcache/spritefontcache.cpp tree_cmd.cpp townname.cpp widgets/dropdown.cpp toolbar_gui.cpp cargotype.cpp economy.cpp)
# viewport.cpp is compiled from a PATCHED copy (below): DoSetViewportPosition calls macsys_scroll
# (windowport CopyBits + RAM memmove) instead of GfxScroll. GfxScroll's MakeDirty(retained) forced a
# near-full CPU present and tore on OS9's single QuickDraw buffer; macsys_scroll scrolls the window
# in place (VRAM→VRAM when on a hw GD) and only the exposed edge strips are redrawn.
# M1 support TUs (shims/stubs/pools) — engine-calibrated, minus the gfx-owned dups.
# m1_viewport_stubs = no-op window/vehicle/sign surface viewport.cpp links against.
# m1_text_stubs = the 4 symbols the real font/layout TUs need (config/utf8/glyphs).
M1_TUS=(m1_shims aa_mark ac_mark b1z_mark b2z_mark bz_mark cb_mark cz_mark dz_mark ee_mark mm_mark sz_mark vv_mark m1_ecostub m1_methods m1_pools m1_profiling_stub m1_town_stubs m1_cmd_stubs m1_land_stubs m1_road_stubs m1_viewport_stubs m1_text_stubs m1_world_stubs m1_water_draw m1_industry_draw m1_window_stubs m1_strings_stubs m1_toolbar_stubs m1_company m1_vehicle m1_economy m1_finance_gui m1_industry m1_depot m1_town_directory_gui m1_station m1_company_gui m1_station_gui m1_vehicle_list_gui m1_graph_gui m1_station_draw m1_order m1_pathfind m1_town_gui m1_industry_gui m1_smallmap_gui m1_subsidy_gui m1_rail_draw m1_train m1_realveh_stubs m1_rvstub_airsea m1_rvstub_group m1_rvstub_econ m1_rvstub_infra zz_marker)

# --- parallel compile --------------------------------------------------------
# The 60 TUs are independent single `g++ -c src -o obj` calls, and they used to run
# one at a time: 41s of wall clock with 11 of 12 cores idle.
#
# /bin/bash on macOS is 3.2, which has no `wait -n`, so the job pool is xargs -P.
# Each queued unit is one fully-quoted shell string (printf %q over the real argv,
# so a path with a space can never re-split), run as `sh -c 'eval "$0"'`. xargs
# exits 123 when ANY child fails; that is turned into a hard exit so a compile
# error still aborts the build the way `set -e` did when this was serial.
#
# JOBS=1 restores serial, ordered output for debugging.
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
QFILE=$(mktemp /tmp/r1build.XXXXXX)
trap 'rm -f "$QFILE"' EXIT

quote_argv() { local q="" a; for a in "$@"; do q="$q $(printf '%q' "$a")"; done; printf '%s' "${q# }"; }
fsize() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null || echo 0; }

# queue_cc SRC LABEL compiler args... — the label prints when the unit STARTS, so
# output interleaves but every line stays whole. Each record is "cost<TAB>command";
# SRC's byte size is the cost, used to schedule longest-first (see run_queue).
queue_cc() {
  local src="$1" label="$2"; shift 2
  printf '%s\t%s\n' "$(fsize "$src")" \
    "printf '  %s\n' $(printf '%q' "$label"); $(quote_argv "$@")" >> "$QFILE"
}

# Longest-processing-time-first. Two TUs dominate the build -- town_cmd.cpp and
# r1_scene.cpp at ~4.5s each, against <=2s for everything else -- so the critical
# path IS the longest TU, and whether we hit it depends entirely on when that TU
# starts. In source order r1_scene is queued 57th of 60, so with -j12 it began
# only after ~48 others and then ran alone at the tail: 9s wall for 41s of work
# that has a 4.5s floor. Sorting by source size (the top 3 by size include both
# poles) starts them in the first wave instead.
#
# Records are newline-delimited so sort can order them, then converted to NUL for
# xargs -0 -- printf %q renders any newline as $'\n', so no command can span lines.
run_queue() {
  local n
  n=$(wc -l < "$QFILE" | tr -d ' ')
  if [ "${n:-0}" -eq 0 ]; then echo "== nothing to compile (all up-to-date) =="; return 0; fi
  echo "== compiling $n TU(s) with -j$JOBS (longest-first) =="
  if ! sort -rn "$QFILE" | cut -f2- | tr '\n' '\0' | xargs -0 -P "$JOBS" -n1 /bin/sh -c 'eval "$0"'; then
    echo "build.sh: FATAL a compile failed (see the CXX lines above)" >&2
    exit 1
  fi
  : > "$QFILE"
}

compile_all() {
  mkdir -p "$R1/obj"
  : > "$QFILE"

  echo "== engine src TUs =="
  for t in "${SRC_TUS[@]}"; do
    local out="$R1/obj/$(basename ${t%.cpp}).o"
    if up_to_date "$out" "$OTTD/src/$t"; then
      echo "  skip (up-to-date) $t"
    else
      queue_cc "$OTTD/src/$t" "CXX $t" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/$t" -o "$out"
    fi
  done

  echo "== M1 support TUs (from ottd-m1) =="
  for f in "${M1_TUS[@]}"; do
    if up_to_date "$R1/obj/$f.o" "$M1/$f.cpp"; then
      echo "  skip (up-to-date) $f"
    else
      queue_cc "$M1/$f.cpp" "CXX $f" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$M1/$f.cpp" -o "$R1/obj/$f.o"
    fi
  done

  if up_to_date "$R1/obj/m1_deadpools.o" "$M1/m1_deadpools.c"; then
    echo "  skip (up-to-date) m1_deadpools"
  else
    queue_cc "$M1/m1_deadpools.c" "CC  m1_deadpools" "$GCC" -DR1_MERGE -DR1_STRINGS -DR1_REAL_VEHICLE_STACK -c "$M1/m1_deadpools.c" -o "$R1/obj/m1_deadpools.o"
  fi

  # The three sed-patched TUs write distinct *_patched.cpp files, so the rewrite is
  # done here (it costs milliseconds) and only the compile is queued.
  # date.cpp calls EnginesDailyLoop() once per game-day, and that name now belongs to the
  # real engine.cpp (engine availability/previews). R1's own daily expense charge used to BE
  # that function; it is now m1_economy.cpp's r1_daily_expenses and would simply stop running
  # -- silently flattening the finance window and breaking the fin_invariant gate. date.cpp is
  # already ours to compile, so patch the one call site to run both.
  echo "== date.cpp (also call R1's daily expenses) =="
  if up_to_date "$R1/obj/date.o" "$OTTD/src/date.cpp"; then
    echo "  skip (up-to-date) date"
  else
    sed -e 's|^extern void EnginesDailyLoop();|extern void EnginesDailyLoop();\nextern "C" void r1_daily_expenses();|' \
        -e 's|^\tEnginesDailyLoop();|\tEnginesDailyLoop();\n\tr1_daily_expenses();|' \
        "$OTTD/src/date.cpp" > "$R1/obj/date_patched.cpp"
    queue_cc "$R1/obj/date_patched.cpp" "CXX date" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/date_patched.cpp" -o "$R1/obj/date.o"
  fi

  echo "== viewport.cpp (Step 4: macsys_scroll windowport CopyBits + edge redraw; no GfxScroll) =="
  # Also depends on macsys.h (macsys_scroll decl) so a bridge change rebuilds this TU.
  if up_to_date "$R1/obj/viewport.o" "$OTTD/src/viewport.cpp" "$B2/macsys.h"; then
    echo "  skip (up-to-date) viewport"
  else
    # File-scope extern "C" after stdafx.h (block-scope linkage specs fail under this g++);
    # replace GfxScroll with macsys_scroll, falling back to full RedrawScreenRect on failure.
    sed -e '/^#include "stdafx.h"/a\
extern "C" int macsys_scroll(int, int, int, int, int, int); /* Step4: ottd-b2/macsys.h */
' \
        -e 's#GfxScroll(left, top, width, height, xo, yo);#if (!macsys_scroll(left, top, width, height, xo, yo)) { RedrawScreenRect(left, top, left + width, top + height); return; } /* Step4: windowport CopyBits scroll */#' \
        "$OTTD/src/viewport.cpp" > "$R1/obj/viewport_patched.cpp"
    queue_cc "$R1/obj/viewport_patched.cpp" "CXX viewport" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/viewport_patched.cpp" -o "$R1/obj/viewport.o"
  fi

  echo "== window system (widget.cpp; window.cpp with the broken steady_clock sed'd to R1SteadyClock) =="
  if up_to_date "$R1/obj/widget.o" "$OTTD/src/widget.cpp"; then
    echo "  skip (up-to-date) widget"
  else
    queue_cc "$OTTD/src/widget.cpp" "CXX widget" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$OTTD/src/widget.cpp" -o "$R1/obj/widget.o"
  fi
  if up_to_date "$R1/obj/window.o" "$OTTD/src/window.cpp" "$R1/r1_winclock.h"; then
    echo "  skip (up-to-date) window"
  else
    sed 's/std::chrono::steady_clock/R1SteadyClock/g' "$OTTD/src/window.cpp" > "$R1/obj/window_patched.cpp"
    queue_cc "$R1/obj/window_patched.cpp" "CXX window" "$GXX" "${CXXFLAGS[@]}" -include "$R1/r1_winclock.h" "${INCS[@]}" -c "$R1/obj/window_patched.cpp" -o "$R1/obj/window.o"
  fi

  echo "== strings.cpp (real string system; ReadLanguagePack strrchr made null-safe for bare filenames) =="
  if up_to_date "$R1/obj/strings.o" "$OTTD/src/strings.cpp"; then
    echo "  skip (up-to-date) strings"
  else
    sed -e 's|strrchr(_current_language->file, PATHSEPCHAR) + 1|(strrchr(_current_language->file, PATHSEPCHAR) ? strrchr(_current_language->file, PATHSEPCHAR) + 1 : _current_language->file)|' \
        -e 's|void CheckForMissingGlyphs(bool base_font, MissingGlyphSearcher \*searcher)|void CheckForMissingGlyphs_R1UNUSED(bool base_font, MissingGlyphSearcher *searcher)|' \
        "$OTTD/src/strings.cpp" > "$R1/obj/strings_patched.cpp"
    queue_cc "$R1/obj/strings_patched.cpp" "CXX strings" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/obj/strings_patched.cpp" -o "$R1/obj/strings.o"
  fi

  echo "== R1 scene (real-map render + engine world-build) =="
  if up_to_date "$R1/obj/r1_scene.o" "$R1/r1_scene.cpp"; then
    echo "  skip (up-to-date) r1_scene"
  else
    queue_cc "$R1/r1_scene.cpp" "CXX r1_scene" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$R1/r1_scene.cpp" -o "$R1/obj/r1_scene.o"
  fi

  # R1-local b2_shims.o: same B2 source, but -DR1_MERGE (in CXXFLAGS) turns on the
  # live-engine hook (GameLoop->r1_tick) + per-frame redraw. Replaces ${B2}/b2_shims.o
  # in the R1 link (CMakeLists no longer lists that one — it comes from obj/*.o).
  echo "== R1-local b2_shims (live-tick hook) =="
  if up_to_date "$R1/obj/b2_shims.o" "$B2/b2_shims.cpp"; then
    echo "  skip (up-to-date) b2_shims"
  else
    queue_cc "$B2/b2_shims.cpp" "CXX b2_shims" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B2/b2_shims.cpp" -o "$R1/obj/b2_shims.o"
  fi

  # R1-local b1_shims: -DR1_MERGE drops its Layouter/GetCharacterHeight stubs so the
  # REAL gfx_layout.cpp/fontcache.cpp own them (text rendering). Replaces ${B1}/b1_shims.o.
  echo "== R1-local b1_shims (real-text dedup) =="
  if up_to_date "$R1/obj/b1_shims.o" "$B1/b1_shims.cpp"; then
    echo "  skip (up-to-date) b1_shims"
  else
    queue_cc "$B1/b1_shims.cpp" "CXX b1_shims" "$GXX" "${CXXFLAGS[@]}" "${INCS[@]}" -c "$B1/b1_shims.cpp" -o "$R1/obj/b1_shims.o"
  fi

  run_queue

  # R1-182 Classic-forensics probe: rename economy.o so its static ctor runs at
  # the TAIL of the (reverse-alphabetical) default-ctor chain instead of where
  # the lamp trace shows the death. If the death follows the TU, its ctor is
  # guilty by CONTENT; if death stays at the old chain position, the mechanism
  # is cumulative/positional. Costs one redundant recompile per build while the
  # probe lives (mtime gate misses the renamed obj). Remove after the hunt.
  if [ -f "$R1/obj/economy.o" ]; then mv -f "$R1/obj/economy.o" "$R1/obj/ab_econ.o"; fi
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
