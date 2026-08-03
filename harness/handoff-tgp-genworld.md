# Handoff: link surface for real terrain generation (`tgp.cpp` + `genworld.cpp`)

<!-- agent: 2026-08-02 — starting. Will create ottd-m1/m1_gwstub.cpp (24 symbols),
     guard GenerateTerrainPerlin in m1_land_stubs.cpp, compile+nm verify. No harness. -->

<!-- agent: wrote ottd-m1/m1_gwstub.cpp + guarded GenerateTerrainPerlin in
     m1_land_stubs.cpp under #ifndef R1_REAL_TERRAIN. Compiling next. -->

<!-- agent: compile CLEAN (exit 0). nm filtered to game-level T/D/B (no
     .-twins, no fmt::v7, no lowercase locals) shows exactly the 22 symbols
     listed in the brief. Brief says "24" but the enumerated list is 22 —
     counting the listed names, not inventing two more. SaveOrLoad demangles
     as std::__cxx11::basic_string (ABI); same symbol. No missing symbols. -->

<!-- agent: signatures grepped. Notable:
     - SetLocalCompany(CompanyID) — CompanyID is typedef Owner; m1_company.cpp assigns
       _local_company/_current_company directly (deadpool char[16] storage).
     - InitializeGame/GenerateIndustries/Objects/Startup* have no public .h decls —
       only forward decls in genworld.cpp / defs in .cpp. Match those signatures.
     - Game::instance is private static in game.hpp; define like game_core.cpp.
     - Game::NewEvent already stubbed in m1_town_stubs.cpp — do NOT redefine.
     - _debug_net_level: int extern in debug.h; not in deadpools yet (only _debug_misc/_desync).
     - SaveOrLoad returns SaveOrLoadResult; absent → SL_ERROR. -->

Task brief for an external coding agent. Everything here is verifiable by **compiling**;
no hardware, VM or emulator is needed. Do not run the test harness.

## Context

`ottd-macos9` is a port of OpenTTD 13.4 to Mac OS 9 / PowerPC, at `/Users/felipe/ottd-macos9`.
It compiles a growing subset of the game's REAL translation units and supplies the rest of the
link surface with small, deliberate stub TUs in `ottd-m1/`.

We are adding the game's own terrain generator. Both TUs already compile clean with the project
flags — this task is only about closing their link surface.

**The payoff:** `GenerateTerrainPerlin()` is currently a hand-rolled stub in
`ottd-m1/m1_land_stubs.cpp`, and `tgp.cpp` *is* the real implementation of that exact function.
Landing this replaces R1's hand-written value-noise map with OpenTTD's actual TerraGenesis
perlin terrain.

## Deliverable

**Exactly one new file:** `ottd-m1/m1_gwstub.cpp`

It must define **exactly** the 24 game symbols listed below and nothing else. (Four entries in
the raw scan — `_restf21`, `_restf29`, `_savef21`, `_savef29` — are compiler runtime float
save/restore helpers, and `sin`/`sqrtf` are libm. Those resolve at link; **do not define them.**)

```
_debug_net_level
BasePersistentStorageArray::SwitchMode(PersistentStorageMode, bool)
FlatEmptyWorld(unsigned char)
Game::GameLoop()
Game::instance
Game::StartNew()
GenerateIndustries()
GenerateObjects()
GfxLoadSprites()
InitializeGame(unsigned int, unsigned int, bool, bool)
PrepareGenerateWorldProgress()
SaveOrLoad(std::string const&, SaveLoadOperation, DetailedFileType, Subdirectory, bool)
SetLocalCompany(Owner)
SetModalProgress(bool)
SetupColoursAndInitialWindow()
ShowGenerateWorldProgress()
ShowNewGRFError()
ShowVitalWindows()
StartupCompanies()
StartupDisasters()
StartupEconomy()
SwitchToMode(SwitchMode)
```

All of these are honest stubs: this port has no script/Game VM, no savegame layer, no NewGRF
error UI, no modal progress dialog and no disasters. Return the value that means "this subsystem
is absent" (`false`, `0`, `nullptr`, do nothing for voids) and say so in a comment.

`SetLocalCompany(Owner)` is the one to be careful with — grep how `_local_company` is already
handled in `ottd-m1/` and stay consistent with it rather than inventing a second owner path.

## Two collisions — do NOT define these, they need handover instead

1. **`GenerateTerrainPerlin()`** — defined today in `ottd-m1/m1_land_stubs.cpp`. `tgp.cpp` owns
   the real one. Guard the stub out with `#ifndef R1_REAL_TERRAIN` (mirror the existing
   `#ifndef R1_REAL_VEHICLE_STACK` blocks in `ottd-m1/m1_vehicle.cpp` for style, including the
   explanatory comment).
2. **`_generating_world`** — already defined in `ottd-m1/m1_deadpools.c` and merged with several
   real TUs. Leave it exactly as it is; do not add another definition.

## Build command (use verbatim)

```
cd /Users/felipe/ottd-macos9 && \
Retro68-build/toolchain/bin/powerpc-apple-macos-g++ -std=c++17 -DNO_THREADS \
  -DTTD_ENDIAN=TTD_BIG_ENDIAN -DR1_MERGE -DR1_STRINGS -DR1_REAL_VEHICLE_STACK -DNDEBUG \
  -include compat/libc_compat.h -Os \
  -Icompat -Iopenttd-13.4/src -Iopenttd-13.4/src/3rdparty \
  -Iopenttd-13.4/src/3rdparty/squirrel/include -Iopenttd-13.4/src/script/api \
  -Iopenttd-13.4/build-native/generated -Iopenttd-13.4/build-native/generated/script/api \
  -c ottd-m1/m1_gwstub.cpp -o /tmp/m1_gwstub.o
```

Verify with:

```
Retro68-build/toolchain/bin/powerpc-apple-macos-nm --defined-only /tmp/m1_gwstub.o \
  | Retro68-build/toolchain/bin/powerpc-apple-macos-c++filt
```

Iterate until it compiles with **zero errors** and `nm` shows exactly the 24 symbols. Ignore
`.`-prefixed twins (XCOFF transition vectors), weak `fmt::v7` template data from `stdafx.h`, and
file-local (lowercase `b`/`d`) symbols — all normal on this ABI.

## Rules

1. **Grep the headers for every real declaration** in `openttd-13.4/src/*.h` and match the
   signature exactly. Do not guess. Do not repeat default arguments in a definition.
2. `#include "stdafx.h"` **first**, `#include "safeguards.h"` **last**. Hard project convention.
3. Start the file with the GPL header copied verbatim from the top of `ottd-m1/m1_vehicle.cpp`.
4. Then a file comment explaining *why* these are stubs. Match the commenting voice of
   `ottd-m1/m1_vehicle.cpp` — explain the reasoning, not the mechanics.
5. **Do not modify** `build.sh`, `CMakeLists.txt`, or any existing `.cpp` — except the single
   guard in `m1_land_stubs.cpp` described above.

## Two traps that have already bitten this project

Both produce a **clean link** and a crash, with no diagnostic. Check for them before assuming a
stub is needed:

- **A clean XCOFF link does not prove there is no duplicate.** Same-named **BSS** symbols merge
  *silently*. A size-mismatched pair (`char foo[8192]` vs a real object) links fine, then the real
  constructor writes into undersized storage — we got a Type 2 crash before `main` could log one
  line. Detect with:
  `nm --defined-only ottd-r1/obj/*.o | awk '$2=="B"' | sort | uniq -d`
- **A strong definition silently beats a weak template instantiation.** `ground_vehicle.cpp` ends
  with `template struct GroundVehicle<RoadVehicle, VEH_ROAD>;`, so the real `GetAcceleration` was
  already in the link as a weak symbol — and our stub silently overrode it for a whole release.
  Before stubbing anything template-shaped, check whether an explicit instantiation already
  provides it.

## Report back

- The file path
- The exact symbol list `nm` reports as defined
- Any symbol you could not define, and why
- Anything you found that contradicts this brief (the surface was measured at commit `294b129`;
  if it has moved, say so rather than forcing the list)

<!-- agent DONE 2026-08-02
     Path: ottd-m1/m1_gwstub.cpp
     Also: ottd-m1/m1_land_stubs.cpp — GenerateTerrainPerlin guarded #ifndef R1_REAL_TERRAIN
     Compile: zero errors with the verbatim build command.
     Could not define: none of the listed symbols.
     Contradictions:
       1. Brief says "24" symbols; the enumerated list has 22. Delivered the 22 listed.
       2. Surface list itself still matches current headers/forward decls; no moved names.
     nm game-level defined (T/D/B, no .-twins / fmt / locals):
       _debug_net_level
       BasePersistentStorageArray::SwitchMode(PersistentStorageMode, bool)
       FlatEmptyWorld(unsigned char)
       Game::GameLoop()
       Game::instance
       Game::StartNew()
       GenerateIndustries()
       GenerateObjects()
       GfxLoadSprites()
       InitializeGame(unsigned int, unsigned int, bool, bool)
       PrepareGenerateWorldProgress()
       SaveOrLoad(std::__cxx11::basic_string<...> const&, SaveLoadOperation, DetailedFileType, Subdirectory, bool)
       SetLocalCompany(Owner)
       SetModalProgress(bool)
       SetupColoursAndInitialWindow()
       ShowGenerateWorldProgress()
       ShowNewGRFError()
       ShowVitalWindows()
       StartupCompanies()
       StartupDisasters()
       StartupEconomy()
       SwitchToMode(SwitchMode)
-->
