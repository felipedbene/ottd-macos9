# Handoff: link surface for the train-purchase GUI (`depot_gui.cpp` + `build_vehicle_gui.cpp`)

<!-- agent: 2026-08-18 — pure MEASUREMENT pass, method of handoff-train-stack.md.
     Probe-compiled both TUs with the full R1 flag set (incl. R1_REAL_TRAIN_STACK
     / R1_REAL_ECONOMY), built have.txt from ottd-r1/obj + ottd-b1 + ottd-b2
     (18467 defined symbols), computed GAME NEED and COLLISIONS. NOTHING wired,
     NOTHING edited. -->

<!-- agent: depot_gui.cpp compiles CLEAN (exit 0, 216213-byte .o). Raw U = 156.
     GAME NEED = 21, COLLISIONS = 3 (all three are empty one-line stubs). -->

<!-- agent: build_vehicle_gui.cpp compiles CLEAN (exit 0, 231940-byte .o).
     Raw U = 172. GAME NEED = 13, COLLISIONS = 0. -->

Task brief for an external coding agent. Everything here is verified by **compiling**;
no hardware, VM or emulator is needed. Do not run the test harness.

## Context

`ottd-macos9` is a port of OpenTTD 13.4 to Mac OS 9 / PowerPC, at `/Users/felipe/ottd-macos9`.
It compiles a growing subset of the game's REAL translation units and closes the rest of the
link surface with small stub TUs in `ottd-m1/`.

**Where the build is today (R1-177 era).** `ottd-r1/build.sh` `SRC_TUS` already compiles the
real `roadveh_cmd.cpp`, `vehicle.cpp`, `engine.cpp`, `order_cmd.cpp`, `economy.cpp`,
**`train_cmd.cpp`** and friends; `m1_trainstub.o` is linked. So the two icon painters the
purchase GUI leans on hardest — `DrawTrainEngine` and `DrawRoadVehEngine` — are **already in
have.txt** (they live in `train_cmd.cpp` / `roadveh_cmd.cpp`, not in the `*_gui.cpp` files).
That makes this GUI wave far cheaper than it looks.

**Goal measured here.** What it takes to link the real train-purchase GUI:
- `openttd-13.4/src/depot_gui.cpp` — the depot window (vehicle matrix, sell/start/stop, Build button)
- `openttd-13.4/src/build_vehicle_gui.cpp` — the "New Vehicles" purchase window (engine list, Buy)

**Related prepared-but-unlinked file.** `ottd-m1/m1_vguistub.cpp` exists (link surface for the
real `vehicle_gui.cpp` lane) but is **not** in `M1_TUS` / obj. It already contains verbatim or
honest-absence definitions for **7 of depot_gui's 21 needs** (see below). It probe-compiles
clean with today's flags.

## Per-TU verdict

| TU | Compile (full R1 flags) | Raw `nm -u` | GAME NEED | Collisions | Verdict |
|---|---|---|---|---|---|
| `depot_gui.cpp` | **CLEAN** (exit 0, 216213 B) | 156 | **21** | **3** (all trivial empty stubs) | **GO** |
| `build_vehicle_gui.cpp` | **CLEAN** (exit 0, 231940 B) | 172 | **13** | **0** | **GO** |

Cross-satisfaction when linked **together** (they should ship as one bundle):
- depot_gui's `ShowBuildVehicleWindow(TileIndex, VehicleType)` ← defined by build_vehicle_gui.
- build_vehicle_gui's `GetVehicleImageCellSize(VehicleType, EngineImageType)` ← defined by depot_gui.

The two miss-sets share **no** other game symbol, so the combined bundle needs
**32 distinct symbols** (21 + 13 − 2). Of those, 7 are already written in `m1_vguistub.cpp`,
2 are already-linked-TU homework (`vehicle_cmd.cpp` decision), and most of the rest are
one-liners. For comparison, `train_cmd.cpp` cost 27 needs + 19 collisions and landed fine.

Dot-twin note (same as the train-stack handoff): `nm -u` on XCOFF surfaces plain calls as
`.`-prefixed text undefs only; address-taken symbols (command lambdas via `Command<>::Post`,
the `CcBuild*` callbacks, data tables) surface as plain descriptor undefs only. A normal C++
definition provides both halves — the split below is informational, not extra work.

## GAME NEED — `depot_gui.cpp` (21 symbols)

Legend: **[V]** = already defined in `ottd-m1/m1_vguistub.cpp` (uncompiled — add it to the
build and these cost nothing). **STUB** = honest-absence stub candidate. **XPLANT** = tiny
verbatim transplant from the real TU. **DRAG?** = the real TU is the honest answer.

| Symbol | Real home TU | Classification |
|---|---|---|
| `_veh_sell_msg_table` (data) | `vehicle_cmd.cpp` | XPLANT — 4-entry `const StringID[]`, copy verbatim (guard if vehicle_cmd.cpp ever lands) |
| `CmdDepotSellAllVehicles(DoCommandFlag, TileIndex, VehicleType)` | `vehicle_cmd.cpp` | STUB (`return CMD_ERROR`) or DRAG? vehicle_cmd — sell-all button honest-inert until then |
| `CmdDepotMassAutoReplace(DoCommandFlag, TileIndex, VehicleType)` | `vehicle_cmd.cpp` | STUB — autoreplace subsystem absent (matches `m1_rvstub_group.cpp` pattern) |
| `CmdSellVehicle(DoCommandFlag, VehicleID, bool, bool, ClientID)` | `vehicle_cmd.cpp` | **DRAG?/XPLANT** — the depot window's Sell button; a lying stub makes the window cosmetic. See "vehicle_cmd.cpp question" below |
| `CmdCloneVehicle(DoCommandFlag, TileIndex, VehicleID, bool)` | `vehicle_cmd.cpp` | **[V]** STUB (clone absent) |
| `CmdRenameDepot(DoCommandFlag, DepotID, std::string const&)` | `depot_cmd.cpp` | STUB (`return CMD_ERROR`) — no rename path |
| `CmdMassStartStopVehicle(DoCommandFlag, TileIndex, bool, bool, VehicleListIdentifier const&)` | `vehicle_cmd.cpp` | **[V]** — real transplant already written there |
| `DepotSortList(VehicleList*)` | `vehicle_gui.cpp` | XPLANT — 3 lines (`std::sort` by `VehicleNumberSorter`; bring the comparator) |
| `BuildDepotVehicleList(VehicleType, TileIndex, VehicleList*, VehicleList*, bool)` | `vehiclelist.cpp` | XPLANT — one loop over `Vehicle::Iterate()`; mentioned in a vguistub comment but NOT defined there |
| `GetUnitNumberDigits(VehicleList&)` | `vehicle_gui.cpp` | XPLANT — tiny |
| `DrawTrainImage(Train const*, Rect const&, VehicleID, EngineImageType, int, int)` | `train_gui.cpp` | **[V]** — verbatim transplant already written |
| `DrawRoadVehImage(Vehicle const*, Rect const&, VehicleID, EngineImageType, int)` | `roadveh_gui.cpp` | **[V]** — verbatim transplant already written |
| `DrawShipImage(Vehicle const*, Rect const&, VehicleID, EngineImageType)` | `ship_gui.cpp` | **[V]** STUB (no ships) |
| `DrawAircraftImage(Vehicle const*, Rect const&, VehicleID, EngineImageType)` | `aircraft_gui.cpp` | **[V]** STUB (no aircraft) |
| `GetShipSpriteSize(EngineID, uint&, uint&, int&, int&, EngineImageType)` | `ship_cmd.cpp` | STUB — return a fixed cell size (no ships) |
| `GetAircraftSpriteSize(EngineID, uint&, uint&, int&, int&, EngineImageType)` | `aircraft_cmd.cpp` | STUB — same |
| `SetMouseCursorVehicle(Vehicle const*, EngineImageType)` | `vehicle_gui.cpp` | STUB no-op — drag-ghost cursor cosmetic only |
| `ShowVehicleListWindow(Owner, VehicleType, TileIndex)` | `vehicle_gui.cpp` | STUB — forward to the 2-arg `ShowVehicleListWindow(Owner, VehicleType)` overload `m1_vehicle_list_gui.o` already defines (different mangled name — no collision) |
| `ShowBuildVehicleWindow(TileIndex, VehicleType)` | `build_vehicle_gui.cpp` | **cross-satisfied** by linking build_vehicle_gui (the empty version in `m1_vguistub.cpp:520` must then be guarded out) |
| `ShowQuery(StringID, StringID, Window*, QueryCallbackProc*)` | `misc_gui.cpp` | STUB — confirmation dialog absent. Recommend NOT invoking the callback (inert = sell-all unreachable) rather than auto-confirming `true`, which would silently mass-sell |
| `OrderBackup::Reset(TileIndex, bool)` | `order_backup.cpp` | STUB no-op — order-backup pool absent; exact sibling of `OrderBackup::Backup` already in `m1_trainstub.cpp` |

## GAME NEED — `build_vehicle_gui.cpp` (13 symbols)

| Symbol | Real home TU | Classification |
|---|---|---|
| `_veh_build_msg_table` (data) | `vehicle_cmd.cpp` | XPLANT — 4-entry `const StringID[]` |
| `CmdBuildVehicle(DoCommandFlag, TileIndex, EngineID, bool, CargoID, ClientID)` | `vehicle_cmd.cpp` | **DRAG?/XPLANT** — THE Buy button. Stub = a purchase window that cannot purchase. See "vehicle_cmd.cpp question" |
| `CcBuildPrimaryVehicle(Commands, CommandCost const&, VehicleID, ...)` | `vehicle_gui.cpp` | XPLANT — small: on success open the vehicle view / start; a minimal version can just no-op |
| `CcBuildWagon(Commands, CommandCost const&, ...)` | `train_gui.cpp` | XPLANT/STUB — post-build wagon move-to-train; no-op acceptable at first |
| `EngList_Sort(GUIEngineList*, EngList_SortTypeFunction)` | `engine_gui.cpp` | XPLANT — 3 lines (`std::sort`) |
| `EngList_SortPartial(GUIEngineList*, ..., size_t, size_t)` | `engine_gui.cpp` | XPLANT — 5 lines |
| `DrawVehicleEngine(int, int, int, int, EngineID, PaletteID, EngineImageType)` | `engine_gui.cpp` | XPLANT — a 4-way switch. **`DrawTrainEngine` and `DrawRoadVehEngine` are ALREADY LINKED** (`train_cmd.o` / `roadveh_cmd.o`); only the `DrawShipEngine`/`DrawAircraftEngine` arms need honest-absence stubs |
| `GetGroupNumEngines(Owner, GroupID, EngineID)` | `group_cmd.cpp` | STUB — no groups; return `GetGroupNumEngines`-style count 0 / fall back to company engine count (matches `m1_rvstub_group.cpp` pattern) |
| `ShowRefitOptionsList(int, int, int, VehicleID)` | `vehicle_gui.cpp` | STUB no-op — refit subsystem absent |
| `GetVehicleImageCellSize(VehicleType, EngineImageType)` | `depot_gui.cpp` | **cross-satisfied** by linking depot_gui |
| `GetCapacityOfArticulatedParts(EngineID)` | `articulated_vehicles.cpp` | STUB — no articulated vehicles: return the engine's own base capacity |
| `GetTotalCapacityOfArticulatedParts(EngineID)` | `engine_gui.cpp` | STUB — same (base capacity as `CargoArray`-summed uint) |
| `IsArticulatedVehicleRefittable(EngineID)` | `articulated_vehicles.cpp` | STUB — return false |

### The vehicle_cmd.cpp question (the only real judgment call)

`CmdBuildVehicle` + `CmdSellVehicle` are the two symbols where a stub makes the GUI a lie.
Their home, `vehicle_cmd.cpp`, also owns 4 more of the needs (`CmdCloneVehicle`,
`CmdMassStartStopVehicle`, `CmdDepotSellAllVehicles`, `CmdDepotMassAutoReplace`) plus both
msg tables — dragging it would collapse 8 needs into one TU, but it was NOT probe-measured
here (it pulls autoreplace/refit/news surfaces; measure it with this same recipe before
deciding). The cheap alternative that keeps the Buy button honest: transplant `CmdBuildVehicle`
(it dispatches to the already-linked `CmdBuildRailVehicle`-path inside `train_cmd.cpp` /
`roadveh_cmd.cpp` build functions) and `CmdSellVehicle` into an `m1_buystub.cpp`, and keep
the other four as honest-absence stubs. Either way works; measure before dragging.

## Collisions

### `depot_gui.cpp` — 3 (each an empty one-liner in ottd-m1; guard with `#ifndef R1_REAL_DEPOT_GUI` or your chosen flag)

| Colliding symbol | Current owner | Line |
|---|---|---|
| `ShowDepotWindow(TileIndex, VehicleType)` | `ottd-m1/m1_road_stubs.cpp` | 92 (`{}` body) |
| `InitDepotWindowBlockSizes()` | `ottd-m1/m1_window_stubs.cpp` | 106 (`{}` body) |
| `DeleteDepotHighlightOfVehicle(Vehicle const*)` | `ottd-m1/m1_rvstub_group.cpp` | 93 (`{}` body) |

### `build_vehicle_gui.cpp` — ZERO

Nothing in obj defines any strong symbol build_vehicle_gui defines. The only latent conflict
is **prospective**: `m1_vguistub.cpp:520` defines an empty `ShowBuildVehicleWindow` — if/when
m1_vguistub joins the build alongside the real build_vehicle_gui, guard that one stub out.

## Window-system hooks — where R1 would invoke these (NOT wired; observations only)

- `ShowDepotWindow` is **already called today**: the real `road_cmd.cpp` (linked) routes road
  depot tile clicks through `ClickTile_Road` → `ShowDepotWindow` — currently swallowed by the
  empty stub in `m1_road_stubs.cpp:92`. Guard that stub out and clicking a road depot opens
  the real window with ZERO new plumbing.
- Rail depots: `ottd-m1/m1_rail_draw.cpp:239` owns the rail tile-proc table with
  `click_tile_proc = nullptr` (rail_cmd.cpp is not linked). Fill that slot with a proc that
  calls `ShowDepotWindow(tile, VEH_TRAIN)` for `IsRailDepotTile`.
- `ShowBuildVehicleWindow` is invoked from **inside depot_gui's own widget tree** (the
  "New Vehicles" button) — no external caller needed once both TUs link. An optional direct
  toolbar entry (`ShowBuildVehicleWindow(INVALID_TILE, VEH_TRAIN)` style) would follow the
  existing R1 toolbar-button pattern in the patched `toolbar_gui.cpp` lane.
- `InitDepotWindowBlockSizes()` is already called at startup by whoever calls the
  `m1_window_stubs` version today — the real one just starts computing real block sizes.
- All `_nested_*_widgets` arrays and `WindowDesc`s for both windows are TU-internal (verified:
  neither appears in the miss lists). String IDs resolve through the linked `strings.o`.

## Wiring checklist (NOT done — next agent)

1. Pick the flag: suggest `-DR1_REAL_DEPOT_GUI` covering the pair (they cross-satisfy; ship together).
2. Guard the 3 collision one-liners (`m1_road_stubs.cpp:92`, `m1_window_stubs.cpp:106`,
   `m1_rvstub_group.cpp:93`) under `#ifndef R1_REAL_DEPOT_GUI`.
3. Decide the vehicle_cmd.cpp question (measure it first with this recipe), then write
   `m1_buystub.cpp` (or equivalent) for the remaining STUB/XPLANT rows above. Keep it
   symbol-disjoint from `m1_vguistub.cpp` — 7 of the depot needs belong to vguistub already,
   so either add `m1_vguistub` to `M1_TUS` in the same wave (guarding its
   `ShowBuildVehicleWindow`) or lift those 7 definitions into the new stub TU, not both.
4. Add `depot_gui.cpp` + `build_vehicle_gui.cpp` to `SRC_TUS`, the stub TU(s) to `M1_TUS`.
5. Re-measure: `nm -u` of both new .o against full have.txt must show zero new game
   unresolved (runtime excluded).
6. Link; watch for the known xcofflink landmines (see MEMORY: `core/math_func.cpp`-style
   solo-.o segfault — if a dragged TU trips it, transplant instead).
7. Fill `m1_rail_draw.cpp`'s `click_tile_proc` for rail depot tiles (road depots need nothing).
8. Do NOT drag `vehicle_gui.cpp`, `group_cmd.cpp`, `autoreplace_cmd.cpp`, `order_backup.cpp`,
   `articulated_vehicles.cpp`, `misc_gui.cpp` unless a measurement proves a stub lies on a
   hot path — same discipline as the road/train stacks.

## Measure yourself (tree moves)

```
cd /Users/felipe/ottd-macos9
TC=Retro68-build/toolchain/bin
FLAGS="-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS \
  -DR1_REAL_VEHICLE_STACK -DR1_REAL_ECONOMY -DR1_REAL_TRAIN_STACK -DNDEBUG \
  -include compat/libc_compat.h -Os -frandom-seed=r1 \
  -Icompat -Iopenttd-13.4/src -Iopenttd-13.4/src/3rdparty \
  -Iopenttd-13.4/src/3rdparty/squirrel/include -Iopenttd-13.4/src/script/api \
  -Iopenttd-13.4/build-native/generated -Iopenttd-13.4/build-native/generated/script/api"

$TC/powerpc-apple-macos-g++ $FLAGS -c openttd-13.4/src/depot_gui.cpp -o /tmp/depot_gui.o
$TC/powerpc-apple-macos-g++ $FLAGS -c openttd-13.4/src/build_vehicle_gui.cpp -o /tmp/build_vehicle_gui.o

for f in ottd-r1/obj/*.o ottd-b1/*.o ottd-b2/*.o; do
  $TC/powerpc-apple-macos-nm --defined-only "$f" 2>/dev/null | awk '$2 ~ /^[TtDdBbGgRrVvWw]$/ {print $3}'
done | sort -u > /tmp/have.txt

for o in depot_gui build_vehicle_gui; do
  $TC/powerpc-apple-macos-nm -u /tmp/$o.o | sed 's/^ *U //' | sort -u \
    | comm -23 - /tmp/have.txt | $TC/powerpc-apple-macos-c++filt | sed 's/^\.//' | sort -u
  # collisions
  $TC/powerpc-apple-macos-nm --defined-only /tmp/$o.o | awk '$2 ~ /^[TDBGV]$/{print $3}' \
    | sort -u | comm -12 - /tmp/have.txt | grep -v '^\.' | $TC/powerpc-apple-macos-c++filt
done
```

**EXCLUDE from stubs:** `std::`, `__cxa*`, `_Unwind*`, `__gxx_personality_v0`,
`operator new`/`delete`, `__dynamic_cast`, typeinfo/vtables for `std::runtime_error` /
`__cxxabiv1`, locale/numpunct, `mem*`, `str*`, `*printf`, `calloc`/`free`, libgcc helpers
(`__ashldi3`, `__ctzdi2`, `__lshrdi3`, `__udivdi3`, `__umoddi3`, `__floatdidf`).

## Recommendation

- **`build_vehicle_gui.cpp`: GO.** Clean compile, 13 game needs, ZERO collisions, and the two
  heavy painters it needs are already linked. Cheapest real-GUI TU measured so far. Sole
  caveat: decide `CmdBuildVehicle` (transplant or vehicle_cmd.cpp) or the Buy button lies.
- **`depot_gui.cpp`: GO.** Clean compile, 21 game needs but 7 pre-written in `m1_vguistub.cpp`
  and most of the rest are one-liners; 3 collisions, each an empty `{}` stub that exists only
  because this window was absent — guarding them out is the point. Road-depot click plumbing
  already reaches `ShowDepotWindow` today.
- Ship them **together** (cross-satisfaction removes 2 symbols and the depot window's
  "New Vehicles" button is the natural entry point for the purchase GUI).

## Rules

1. Grep headers for every declaration; match signatures exactly; do not repeat default arguments.
2. `#include "stdafx.h"` first, `#include "safeguards.h"` last in any new stub TU.
3. Keep new stub TUs symbol-disjoint from `m1_vguistub.cpp`, `m1_realveh_stubs.cpp`,
   `m1_rvstub_*.cpp`, `m1_trainstub.cpp`.
4. Do not add either GUI TU to `build.sh` until the 3 collision guards land.
5. Measure `vehicle_cmd.cpp` with this same recipe before dragging it.
