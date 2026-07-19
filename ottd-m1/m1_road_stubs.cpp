/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from and/or built against OpenTTD, Copyright (c) the OpenTTD
 * Development Team. Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

/*
 * m1_road_stubs.cpp — link-only no-op stubs for M1 build 9 (real CmdBuildRoad).
 *
 * road_cmd.cpp is compiled for real so Command<CMD_BUILD_ROAD>::Do can lay a
 * road on PPC. It drags ~40 refs from vehicle/depot/draw/sound/rail-crossing/
 * ownership/economy subsystems. Building one road piece on a plain grass tile
 * with EMPTY vehicle/depot/company pools touches almost none of them; the few
 * on-path ones (vehicle-presence & ownership checks) are correct as no-ops
 * because the pools are empty / the tile is unowned.
 *
 * CRITICAL: this XCOFF ld SEGFAULTS on a duplicate/"multiple definition" symbol
 * instead of erroring cleanly. A symbol defined for real by road_cmd.o /
 * landscape.o / clear_cmd.o / town_cmd.o must NOT be stubbed here. (Globals/
 * tables go in m1_deadpools.c as raw storage.)
 */
#include "stdafx.h"

#include "company_func.h"
#include "company_gui.h"
#include "vehicle_func.h"
#include "vehicle_base.h"
#include "rail_cmd.h"
#include "bridge_map.h"
#include "tunnelbridge.h"
#include "depot_func.h"
#include "depot_base.h"
#include "sound_func.h"
#include "road_func.h"
#include "newgrf_roadtype.h"
#include "newgrf_railtype.h"
#include "elrail_func.h"
#include "gfx_func.h"
#include "sprite.h"
#include "viewport_func.h"
#include "train.h"
#include "roadveh.h"
#include "ground_vehicle.hpp"
#include "core/pool_func.hpp"
#include "pathfinder/yapf/yapf_cache.h"
#include "order_base.h"
#include "cargopacket.h"

/* ~Vehicle (below) destroys a VehicleCargoList member, implicitly instantiating
 * this destructor (real body lives in the uncompiled cargopacket.cpp). Provide
 * an empty specialization; it MUST precede ~Vehicle's definition, else the
 * compiler rejects "specialization after instantiation". */
template <> CargoList<VehicleCargoList, CargoPacketList>::~CargoList() {}

/* ================================================================= *
 * RETURN VALUES THAT MATTER (on the real CmdBuildRoad-on-grass path) *
 * ================================================================= */

/* Ownership: tile is unowned (OWNER_NONE) and _current_company == OWNER_DEITY,
 * so ownership passes. Empty CommandCost == success / zero cost. */
CommandCost CheckOwnership(Owner, TileIndex) { return CommandCost(); }
CommandCost CheckTileOwnership(TileIndex) { return CommandCost(); }

/* Vehicle-presence over the EMPTY vehicle pool: "no vehicle here" == success.
 * FindVehicleOnPos returns void; leaving `proc` uncalled means nothing found. */
CommandCost EnsureNoVehicleOnGround(TileIndex) { return CommandCost(); }
CommandCost TunnelBridgeIsFree(TileIndex, TileIndex, const Vehicle *) { return CommandCost(); }
void FindVehicleOnPos(TileIndex, void *, VehicleFromPosProc *) {}

/* Road-type validity: ON the CmdBuildRoad exec path — MUST accept ROADTYPE_ROAD
 * (road.cpp is not compiled, so we own this). */
bool ValParamRoadType(RoadType roadtype) { return roadtype < ROADTYPE_END; }

/* ================================================================= *
 * OFF-PATH no-ops / return-defaults (never run building one road    *
 * piece on a plain grass tile with empty pools).                    *
 * ================================================================= */

/* ---- ownership / company infra ---- */
void GetNameOfOwner(Owner, TileIndex) {}
void DirtyCompanyInfrastructureWindows(CompanyID) {}

/* ---- depots / vehicles ---- */
void ShowDepotWindow(TileIndex, VehicleType) {}
void VehicleEnterDepot(Vehicle *) {}

/* ---- rail-crossing / rail ---- */
void UpdateLevelCrossing(TileIndex, bool, bool) {}
void MarkDirtyAdjacentLevelCrossingTiles(TileIndex, Axis) {}
void UpdateAdjacentLevelCrossingTilesOnLevelCrossingRemoval(TileIndex, Axis) {}
bool TrainOnCrossing(TileIndex) { return false; }
CommandCost CmdRemoveSingleRail(DoCommandFlag, TileIndex, Track) { return CommandCost(); }

/* ---- yapf ---- */
void YapfNotifyTrackLayoutChange(TileIndex, Track) {}

/* ---- sound ---- */
void SndPlayTileFx(SoundID, TileIndex) {}

/* ---- bridges ---- */
TileIndex GetNorthernBridgeEnd(TileIndex t) { return t; }
int GetBridgeHeight(TileIndex) { return 0; }
void MarkBridgeDirty(TileIndex) {}

/* ---- draw / sprites ---- */
SpriteID GetCustomRoadSprite(const RoadTypeInfo *, TileIndex, RoadTypeSpriteGroup, TileContext, uint *) { return 0; }
SpriteID GetCustomRailSprite(const RailtypeInfo *, TileIndex, RailTypeSpriteGroup, TileContext, uint *) { return 0; }
void DrawRailCatenary(const TileInfo *) {}
#ifndef R1_MERGE  /* real gfx.o provides DrawSprite in the render-merge build */
void DrawSprite(SpriteID, PaletteID, int, int, const SubSprite *, ZoomLevel) {}
#endif
#ifndef R1_MERGE  /* real viewport.cpp provides DrawGroundSpriteAt in the render-merge build */
void DrawGroundSpriteAt(SpriteID, PaletteID, int32, int32, int, const SubSprite *, int, int) {}
#endif
void DrawCommonTileSeq(const TileInfo *, const DrawTileSprites *, TransparencyOption, int32, uint32, PaletteID, bool) {}
void DrawCommonTileSeqInGUI(int, int, const DrawTileSprites *, int32, uint32, PaletteID, bool) {}

/* ---- Vehicle / Depot destructors (define ~Vehicle -> emits Vehicle vtable +
 *      typeinfo; Crash fills the remaining non-inline virtual slot) ---- */
Vehicle::~Vehicle() {}
uint Vehicle::Crash(bool) { return 0; }
Depot::~Depot() {}

/* ---- member-subobject destructor pulled in by ~Vehicle (orders) ----
 *      (the VehicleCargoList destructor is specialized above the includes) ---- */
Order::~Order() {}

/* ---- ground-vehicle cache recompute for the road-vehicle instantiation ---- */
template <> void GroundVehicle<RoadVehicle, VEH_ROAD>::CargoChanged() {}

/* ---- Pool<> methods for the Depot / Vehicle pools (real bodies live in the
 *      uncompiled depot.cpp / vehicle.cpp; explicitly instantiate here) ---- */
template void *DepotPool::GetNew(size_t size);
template void DepotPool::FreeItem(size_t index);
template void VehiclePool::FreeItem(size_t index);
