# BSS / deadpool silent-merge audit

Date: 2026-08-02. Compile-only; no harness run. Disjoint from the tgp/genworld
wire-up (does not touch `build.sh` or `r1_scene.cpp`).

## Why this exists

XCOFF ld **silently merges** same-named BSS symbols instead of erroring. A
size-mismatched pair (`char foo[8192]` vs a real object) links clean, then the
real constructor writes past the smaller storage → Type 2 crash before `main`
logs a line. Detect with:

```
nm -A --defined-only ottd-r1/obj/*.o | grep ' [BD] ' | awk '{print $3}' | sort | uniq -c | awk '$1>1'
```

Sizes on this ABI often omit `--print-size`; infer slot size from sorted BSS
address gaps within one `.o`.

## Probe sizeof (PowerPC / this toolchain)

| Type / object | Slot (bytes) |
|---|---:|
| `bool` | 1 (BSS slot before next sym often 8) |
| `GenWorldInfo` (`_gw`) | 28 |
| `RoadTypeInfo` | 280 |
| `RoadTypeInfo[ROADTYPE_END]` (`ROADTYPE_END=63`) | 17640 |
| `RoadTypes` (`uint64`, `_roadtypes_type`) | 8 |
| `int` (`_debug_map_level`) | 4 |

Measured via `/tmp/sizeof_probe2.o` and `/tmp/genworld_probe.o` /
`/tmp/tgp_probe.o` (project flags, not wired into the link).

## Claude wire-up: `_generating_world` / `_gw`

| Symbol | Today | Real (genworld.cpp) | Risk |
|---|---|---|---|
| `_generating_world` | deadpool `char[16]` | `bool` (BSS slot 8 before `_gw`) | Silent merge if both linked. Deadpool **oversized** → survivable, but unclean. |
| `_gw` | **not** in deadpool | `GenWorldInfo` (28 B) | Clean — only genworld.o defines it. **Do not** add a deadpool `char[]` for `_gw`. |

**Required when wiring genworld.cpp:**

1. Compile `m1_deadpools.c` with `-DR1_REAL_TERRAIN` (same lesson as
   `R1_REAL_VEHICLE_STACK` / `_engine_pool` — the C line is separate from
   `CXXFLAGS`).
2. Deadpool already guards `_generating_world` behind `#ifndef R1_REAL_TERRAIN`
   (applied in this audit).
3. Do **not** invent a deadpool blob for `_gw`.

`tgp.cpp` defines no conflicting globals (only `GenerateTerrainPerlin` /
`GetEstimationTGPMapHeight`).

## Live silent merges in current `ottd-r1/obj` (before this audit's guards)

All of these were **strong `B`/`D` in two TUs**. None were undersized vs the
real type (so the link survives today), but they are landmines.

### Fixed in `m1_deadpools.c` (this change)

| Symbol | Deadpool | Real owner | Real size | Action |
|---|---|---|---:|---|
| `_roadtypes` | `char[65536]` | `road_cmd.cpp` | 17640 | `#ifndef R1_MERGE` |
| `_roadtypes_type` | `char[64]` | `road_cmd.cpp` | 8 | `#ifndef R1_MERGE` |
| `_special_mouse_mode` | `char[16]` | `window.cpp` | 4 | `#ifndef R1_MERGE` |
| `_toolbar_width` | `uint` (4) | `toolbar_gui.cpp` | 4 | `#ifndef R1_MERGE` |
| `_generating_world` | `char[16]` | `genworld.cpp` (soon) | 1/8 | `#ifndef R1_REAL_TERRAIN` |

### Still open (survivable; cleanup backlog — not terrain)

| Symbol | TU A | TU B | Notes |
|---|---|---|---|
| `_settings_game` / `_settings_client` | `b1_shims.o` | `m1_shims.o` | Same slot size (592 / 548). Both define under R1_MERGE. |
| `_networking` | `b1_shims.o` | `m1_shims.o` | Both `bool`. |
| `_debug_map_level` | `b1_shims.o` | `m1_shims.o` | Both `int`; m1 should be under `#ifndef R1_MERGE`. |
| `_dirty_block_colour` | `b1_shims.o` | `viewport.o` | Same size 4. Guard b1 under `#ifndef R1_MERGE`. |
| `_z_windows` | `b1_shims.o` | `window.o` | `WindowList` — b1 unguarded. |
| `FontCache::caches` | `b1_shims.o` | `fontcache.o` | `FontCache*[FS_END]` — b1 unguarded. |
| `WidgetDimensions::scaled` | `m1_viewport_stubs.o` | `widget.o` | Stub unguarded; widget.cpp owns the real one. |

## Method notes

- TOC entries (lowercase `d`) are **not** definitions — ignore them when
  hunting dups.
- `--print-size` often yields nothing useful on this XCOFF nm; use `-n` + gaps.
- Last BSS symbol in a file has unknown size (`None`) — treat with care.

## Out of scope / left alone

- `build.sh`, `r1_scene.cpp`, `tgp.cpp`, `genworld.cpp`, `m1_gwstub.cpp`
  (Claude owns the terrain wire-up).
- No harness deploy.
