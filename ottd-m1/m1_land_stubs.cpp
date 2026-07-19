/*
 * m1_land_stubs.cpp — link-only no-op stubs for M1 build 8 (real tile commands).
 *
 * landscape.cpp (the _tile_type_procs master dispatch table + CmdLandscapeClear
 * + terrain) and clear_cmd.cpp (real ClearTile_Clear) are compiled for real so
 * Command<CMD_LANDSCAPE_CLEAR>::Do mutates a tile on PPC. They drag ~50 refs
 * from draw/sound/water/vehicle/tick/effect/GRF subsystems. NONE run when
 * clearing a single MP_CLEAR tile headless — every definition here exists only
 * to satisfy the linker.
 *
 * CRITICAL: this XCOFF ld SEGFAULTS on a duplicate/"multiple definition" symbol
 * instead of erroring cleanly. So a symbol defined for real by landscape.o /
 * clear_cmd.o / town_cmd.o must NOT be stubbed here. (Globals/tables go in
 * m1_deadpools.c as raw storage.)
 */
#include "stdafx.h"

#include "tile_type.h"
#include "viewport_func.h"
#include "bridge.h"
#include "water.h"
#include "spritecache.h"
#include "station_func.h"
#include "object_base.h"
#include "effectvehicle_func.h"
#include "framerate_type.h"
#include "command_type.h"
#include "tilearea_type.h"
#include "heightmap.h"
#include "tgp.h"
#include "pathfinder/npf/aystar.h"

/* ---- viewport / draw (spritecombine, ground sprite, bridge) ---- */
#ifndef R1_MERGE  /* real viewport.cpp provides these in the render-merge build */
void OffsetGroundSprite(int, int) {}
void StartSpriteCombine() {}
void EndSpriteCombine() {}
#endif
void DrawBridgeMiddle(const TileInfo *) {}

/* ---- sound (NewGRF ambient callback; declared extern inside an inline fn) ---- */
void AmbientSoundEffectCallback(TileIndex) {}

/* ---- water ---- */
void DoFloodTile(TileIndex) {}
bool RiverModifyDesertZone(TileIndex, void *) { return false; }
void ConvertGroundTilesIntoWaterTiles() {}

/* ---- sprite cache ---- (returns void*, AllocatorProc/SpriteEncoder defaults) */
#ifndef R1_MERGE  /* real spritecache.o provides GetRawSprite in the render-merge build */
void *GetRawSprite(SpriteID, SpriteType, AllocatorProc *, SpriteEncoder *) { return nullptr; }
#endif

/* ---- stations ---- */
void RemoveDockingTile(TileIndex) {}

/* ---- objects (on the CmdLandscapeClear path, but null is correct for a plain
 *      grass tile: it is not part of any cleared-object area) ---- */
ClearedObjectArea *FindClearedObject(TileIndex) { return nullptr; }

/* ---- effect vehicles ---- */
EffectVehicle *CreateEffectVehicleAbove(int, int, int, EffectVehicleType) { return nullptr; }

/* ---- framerate (RAII perf accumulator ctor/dtor) ---- */
PerformanceAccumulator::PerformanceAccumulator(PerformanceElement) {}
PerformanceAccumulator::~PerformanceAccumulator() {}

/* ---- CommandCost (on the CmdLandscapeClear path; accumulating a zero cost into
 *      the running total is a no-op for the single-tile clear) ---- */
void CommandCost::AddCost(const CommandCost &) {}

/* ---- per-subsystem OnTick hooks (declared locally in landscape.cpp) ---- */
#ifndef R1_MERGE  /* real tree_cmd.cpp provides OnTick_Trees in the render-merge build (forests) */
void OnTick_Trees() {}
#endif
void OnTick_Station() {}
void OnTick_Industry() {}
void OnTick_Companies() {}
void OnTick_LinkGraph() {}

/* ---- tile area (ctors + query/iterate helpers; all off the single-tile path) ---- */
OrthogonalTileArea::OrthogonalTileArea(TileIndex, TileIndex) {}
DiagonalTileArea::DiagonalTileArea(TileIndex, TileIndex) {}
bool OrthogonalTileArea::Contains(TileIndex) const { return false; }
OrthogonalTileArea &OrthogonalTileArea::Expand(int) { return *this; }
OrthogonalTileIterator OrthogonalTileArea::begin() const { return OrthogonalTileIterator(*this); }
OrthogonalTileIterator OrthogonalTileArea::end() const { return OrthogonalTileIterator(*this); }
/* Out-of-line key virtual -> emits the vtable for DiagonalTileIterator. */
TileIterator &DiagonalTileIterator::operator++() { return *this; }

/* ---- world generation (heightmap / terrain / water conversion) ---- */
void LoadHeightmap(DetailedFileType, const char *) {}
void FixSlopes() {}
void GenerateTerrainPerlin() {}

/* ---- AyStar pathfinder (river routing during map gen only) ---- */
void AyStar::Init(Hash_HashProc, uint) {}
void AyStar::AddStartNode(AyStarNode *, uint) {}
int AyStar::Main() { return 0; }
void AyStar::Free() {}
