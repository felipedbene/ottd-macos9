# Depot pool rung

Date: 2026-08-02. Compile-only; no harness run. Disjoint from tgp/genworld.

## What landed

- `ottd-m1/m1_depot.cpp` — real `DepotPool _depot_pool`, `INSTANTIATE_POOL_METHODS(Depot)`,
  empty `Depot::~Depot`, factory `r1_make_depot(tile, dir)`, count `r1_depot_count()`,
  nearest helper `r1_find_nearest_road_depot` (Manhattan).
- Deadpool `char _depot_pool[8192]` guarded `#ifndef R1_MERGE` (silent BSS merge trap).
- `m1_road_stubs.cpp` no longer owns `~Depot` / `DepotPool::{GetNew,FreeItem}`.
- `YapfRoadVehicleFindNearestDepot` / NPF twin in `m1_realveh_stubs.cpp` forward to the helper
  so real `RoadVehicle::FindClosestDepot` (roadveh_cmd.cpp) can see a placed depot.
- `build.sh` `M1_TUS` += `m1_depot` only.

## Place from R1 (not wired into `r1_scene` yet)

```c
extern "C" unsigned r1_make_depot(unsigned tile, int dir); /* DIAGDIR_* exit */
unsigned id = r1_make_depot(tile, DIAGDIR_NE);
```

Writes an `MP_ROAD` depot cell via `MakeRoadDepot` + `MakeDefaultName`. Pick a clear tile
whose exit faces a connected road bit if you want buses to path into it.
