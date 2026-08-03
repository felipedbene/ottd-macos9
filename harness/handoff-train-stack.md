# Handoff: link surface for the real train stack (`train_cmd.cpp`)

<!-- agent: 2026-08-02 — starting. Study R1_REAL_VEHICLE_STACK / m1_realveh_stubs,
     probe-compile train_cmd.cpp, classify unresolved, deliver stub TU + this
     handoff. Stay OFF tgp/genworld/command / m1_gwstub / m1_cmdstub / r1_scene. -->

<!-- agent: train_cmd.cpp compiles CLEAN (exit 0, 269290-byte .o). -->

<!-- agent: measured. Raw U = 225. After excluding C++/libc/libgcc runtime AND
     symbols already present as weak defs in vehicle.o / ground_vehicle.o
     (Pool<Vehicle>::GetNew/FreeItem, GroundVehicle<Train>::GetAcceleration),
     GAME NEED = 27. COLLISIONS with project-defined symbols = 19 (12 Train::*
     in m1_train.cpp, 2 in m1_rvstub_airsea.cpp, 4 level-crossing in
     m1_road_stubs.cpp, plus demangle twins). -->

<!-- agent: wrote ottd-m1/m1_trainstub.cpp (27 symbols). Compile CLEAN (exit 0).
     nm shows exactly 27 game T/D symbols. NOT wiring train_cmd into build.sh —
     collisions need R1_REAL_TRAIN_STACK guards first. Cosmetic train stays via
     m1_train.cpp. -->
Task brief for an external coding agent. Everything here is verified by **compiling**;
no hardware, VM or emulator is needed. Do not run the test harness.

## Context

`ottd-macos9` is a port of OpenTTD 13.4 to Mac OS 9 / PowerPC, at `/Users/felipe/ottd-macos9`.
It compiles a growing subset of the game's REAL translation units and closes the rest of the
link surface with small, deliberate stub TUs in `ottd-m1/`.

**Road vehicle precedent (already landed).** `R1_REAL_VEHICLE_STACK` flips the build to
compile `roadveh_cmd.cpp` + `vehicle.cpp` + `engine.cpp` + `order_cmd.cpp` + friends. Hand-rolled
RoadVehicle/Order/Engine stubs sit behind `#ifndef R1_REAL_VEHICLE_STACK`. The missing air/sea/
rail/group/econ/infra symbols are closed by `m1_realveh_stubs.cpp` + `m1_rvstub_*.cpp`. YAPF is
**not** linked: roadveh reaches the pathfinder through two functions backed by the BFS in
`m1_pathfind.cpp`.

**Train today.** Cosmetic only: `ottd-m1/m1_train.cpp` owns the Train vtable + `r1_make_train`
factory; `r1_scene.cpp` drives/renders it. `train_cmd.cpp` is deliberately **not** in
`ottd-r1/build.sh` `SRC_TUS`. `m1_rvstub_airsea.cpp` stubs the two train leftovers that
`order_cmd`/`vehicle` reference (`Train::ConsistChanged`, `CmdReverseTrainDirection`).

**Why this matters.** Landing `train_cmd.cpp` is the road-stack equivalent for rail: real
consist changes, tick/movement, depot/PBS/pathfinder hooks — instead of a hand-driven sprite.

## What is already done (this handoff's deliverable)

**New file: `ottd-m1/m1_trainstub.cpp`** — defines exactly the **27** game symbols `train_cmd.cpp`
needs that the project does not yet provide. Compiles standalone. **Not** added to `build.sh`
`M1_TUS` yet (safe to add alone; useless until `train_cmd` is linked).

### The 27 symbols (do NOT redefine elsewhere)

```
BaseConsist::CopyConsistPropertiesFrom(BaseConsist const*)
CheckCargoCapacity(Vehicle*)
FollowTrainReservation(Train const*, Vehicle**)
GetReservedTrackbits(TileIndex)
GetVehicleCallbackParent(CallbackID, unsigned int, unsigned int, unsigned short, Vehicle const*, Vehicle const*)
GetWagonOverrideSpriteSet(unsigned short, unsigned char, unsigned short)
InvalidateNewGRFInspectWindow(GrfSpecFeature, unsigned int)
IsSafeWaitingPosition(Train const*, TileIndex, Trackdir, bool, bool)
IsStationTileBlocked(TileIndex)
IsWaitingPositionFree(Train const*, TileIndex, Trackdir, bool)
NPFTrainCheckReverse(Train const*)
NPFTrainChooseTrack(Train const*, bool&, bool, PBSTileInfo*)
NPFTrainFindNearestDepot(Train const*, int)
NPFTrainFindNearestSafeTile(Train const*, TileIndex, Trackdir, bool)
OrderBackup::Backup(Vehicle const*, unsigned int)
RemoveVehicleFromGroup(Vehicle const*)
SetRailStationPlatformReservation(TileIndex, DiagDirection, bool)
SetSignalsOnBothDir(TileIndex, Track, Owner)
SetTrainGroupID(Train*, unsigned short)
TicksToLeaveDepot(Train const*)
TryReserveRailTrack(TileIndex, Track, bool)
UnreserveRailTrack(TileIndex, Track)
UpdateTrainGroupID(Train*)
YapfTrainCheckReverse(Train const*)
YapfTrainChooseTrack(Train const*, TileIndex, DiagDirection, TrackBits, bool&, bool, PBSTileInfo*, TileIndex*)
YapfTrainFindNearestDepot(Train const*, int)
YapfTrainFindNearestSafeTile(Train const*, TileIndex, Trackdir, bool)
```

All are honest absences: no YAPF/NPF train pathfinder, no PBS reservation state, no NewGRF
wagon overrides, no group_cmd, no order-backup pool, no signal.cpp. Return the value that
means "subsystem not present".

### Do NOT define these (already provided — defining them BREAKS the link)

| Symbol | Where |
|---|---|
| `Pool<Vehicle,...>::GetNew` / `FreeItem` | weak in `vehicle.o` (and mirrored in `m1_vehicle`/`m1_train`) |
| `GroundVehicle<Train>::GetAcceleration` | weak in `ground_vehicle.o` |
| C++/libc/libgcc (`std::`, `__cxa*`, `_Unwind*`, `operator new`/`delete`, `mem*`, `strlen`, `snprintf`, `__divdi3`, …) | toolchain |
| Anything in `m1_realveh_stubs.cpp` / `m1_rvstub_*.cpp` already | road-stack stubs — keep symbol-disjoint |
| `GroupStatistics::*`, autoreplace cmds | `m1_rvstub_group.cpp` |

## Collisions — need `#ifndef R1_REAL_TRAIN_STACK` before linking `train_cmd`

When `train_cmd.cpp` is added to `SRC_TUS`, these project definitions **collide**. Guard them
out (mirror `#ifndef R1_REAL_VEHICLE_STACK` style in `m1_vehicle.cpp` / `m1_road_stubs.cpp`).

### 1. `ottd-m1/m1_train.cpp` — entire Train vtable body (12 methods)

`train_cmd.cpp` is the real home of every out-of-line `Train::` override that
`m1_train.cpp` currently owns for the cosmetic train:

```
Train::MarkDirty
Train::UpdateDeltaXY
Train::PlayLeaveStationSound
Train::GetImage
Train::GetRunningCost
Train::GetMaxWeight
Train::Tick
Train::OnNewDay
Train::Crash
Train::GetVehicleTrackdir
Train::GetOrderStationLocation
Train::FindClosestDepot
Train::GetCurrentMaxSpeed
```

**Handover options when flipping the flag:**

- Guard the 12 method bodies (and possibly `GroundVehicle<Train>::IsChainInDepot` /
  `CargoChanged` template specialisations) under `#ifndef R1_REAL_TRAIN_STACK`.
- Keep `r1_make_train` (the factory) — `train_cmd` does **not** define it; or migrate
  creation to a real buy-vehicle path later.
- Decision for the agent who wires: either drop the cosmetic train driver in
  `r1_scene.cpp` and let `Train::Tick` run, or keep scene-driven motion and accept that
  real `Tick` is linked but unused. Do **not** edit `r1_scene` from a parallel terrain lane.

### 2. `ottd-m1/m1_rvstub_airsea.cpp` — 2 symbols

```
Train::ConsistChanged(ConsistChangeFlags)
CmdReverseTrainDirection(DoCommandFlag, VehicleID, bool)
```

Both are defined for real in `train_cmd.cpp`. Wrap in `#ifndef R1_REAL_TRAIN_STACK`.

### 3. `ottd-m1/m1_road_stubs.cpp` — 4 level-crossing symbols

```
UpdateLevelCrossing(TileIndex, bool, bool)
MarkDirtyAdjacentLevelCrossingTiles(TileIndex, Axis)
UpdateAdjacentLevelCrossingTilesOnLevelCrossingRemoval(TileIndex, Axis)
TrainOnCrossing(TileIndex)
```

`train_cmd.cpp` defines the real ones. Guard under `#ifndef R1_REAL_TRAIN_STACK` (or a
shared rail-crossing flag if you prefer; keep the name consistent with the build flag).

## Wiring checklist (NOT done yet — next agent)

1. Add `-DR1_REAL_TRAIN_STACK` to `ottd-r1/build.sh` `CXXFLAGS` (or fold into an expanded
   vehicle-stack flag — prefer a **separate** flag so road stays independent).
2. Guard the three collision sites above.
3. Add `train_cmd.cpp` to `SRC_TUS`.
4. Add `m1_trainstub` to `M1_TUS`.
5. Re-measure: `nm -u train_cmd.o` against full `have.txt` must show **zero** new game
   unresolved (runtime excluded).
6. Link the R1 binary; fix any XCOFF surprises.
7. Do **not** drag YAPF (`yapf_*.cpp`), `pbs.cpp`, `signal.cpp`, `group_cmd.cpp`,
   `order_backup.cpp`, `base_consist.cpp`, `newgrf_engine.cpp` unless a later measurement
   proves a stub is lying on a hot path — same discipline as the road stack.

## Measure yourself (tree moves)

```
cd /Users/felipe/ottd-macos9
TC=Retro68-build/toolchain/bin
FLAGS="-std=c++17 -DNO_THREADS -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS -DR1_REAL_VEHICLE_STACK -DNDEBUG -include compat/libc_compat.h -Os -Icompat -Iopenttd-13.4/src -Iopenttd-13.4/src/3rdparty -Iopenttd-13.4/src/3rdparty/squirrel/include -Iopenttd-13.4/src/script/api -Iopenttd-13.4/build-native/generated -Iopenttd-13.4/build-native/generated/script/api"

$TC/powerpc-apple-macos-g++ $FLAGS -c openttd-13.4/src/train_cmd.cpp -o /tmp/train_cmd.o

# Include weak (W) and both cases of T/D/B — XCOFF emits Pool/GetAcceleration as W.
for f in ottd-r1/obj/*.o ottd-b1/*.o ottd-b2/*.o; do
  $TC/powerpc-apple-macos-nm --defined-only "$f" 2>/dev/null | awk '$2 ~ /^[TtDdBbGgRrVvWw]$/ {print $3}'
done | sort -u > /tmp/have.txt

$TC/powerpc-apple-macos-nm -u /tmp/train_cmd.o | sed 's/^ *U //' | sort -u \
  | comm -23 - /tmp/have.txt | $TC/powerpc-apple-macos-c++filt | sed 's/^\.//' | sort -u

# Collisions
$TC/powerpc-apple-macos-nm --defined-only /tmp/train_cmd.o | awk '$2 ~ /^[TDBGV]$/{print $3}' \
  | sort -u | comm -12 - /tmp/have.txt | grep -v '^\.' | $TC/powerpc-apple-macos-c++filt
```

**EXCLUDE from stubs:** `std::`, `__cxa*`, `_Unwind*`, `operator new`/`delete`, vtables for
`__cxxabiv1`, `mem*`, `str*`, `*printf`, libgcc helpers (`__divdi3`, `__ashldi3`, …), and the
already-weak Pool/GetAcceleration symbols above.

## Stub compile + verify (verbatim)

```
cd /Users/felipe/ottd-macos9 && \
Retro68-build/toolchain/bin/powerpc-apple-macos-g++ -std=c++17 -DNO_THREADS \
  -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS -DR1_REAL_VEHICLE_STACK -DNDEBUG \
  -include compat/libc_compat.h -Os \
  -Icompat -Iopenttd-13.4/src -Iopenttd-13.4/src/3rdparty \
  -Iopenttd-13.4/src/3rdparty/squirrel/include -Iopenttd-13.4/src/script/api \
  -Iopenttd-13.4/build-native/generated -Iopenttd-13.4/build-native/generated/script/api \
  -c ottd-m1/m1_trainstub.cpp -o /tmp/m1_trainstub.o

Retro68-build/toolchain/bin/powerpc-apple-macos-nm --defined-only /tmp/m1_trainstub.o \
  | Retro68-build/toolchain/bin/powerpc-apple-macos-c++filt
```

Expect the 27 game symbols (plus XCOFF `.`-prefixed twins and any file-local noise). Zero
compile errors.

## Rules

1. Grep headers for every declaration; match signatures; do not repeat default arguments.
2. `#include "stdafx.h"` first, `#include "safeguards.h"` last.
3. Stay OFF `m1_gwstub.cpp`, `m1_cmdstub.cpp`, tgp/genworld, `r1_scene` heightmap — other agents.
4. Do not add `train_cmd.cpp` to `build.sh` until the collision guards land.
5. Keep `m1_trainstub.cpp` symbol-disjoint from `m1_realveh_stubs` / `m1_rvstub_*`.
