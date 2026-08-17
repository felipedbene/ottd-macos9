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

// R1 minimal station draw (companion to m1_station.cpp's INVISIBLE Station pool):
// a custom _tile_type_station_procs so the real viewport can DRAW an MP_STATION
// bus-stop tile WITHOUT pulling station_cmd.cpp's NewGRF/catchment/link-graph
// machinery. Our world is flat at height 0 and every MP_STATION tile we write is a
// DRIVE-THROUGH bus stop: the road runs straight through the tile along its axis
// (so the scripted bus drives ONTO it — correct) and we set a small shelter beside
// the carriageway. The real drive-through sprites (SPR_BUS_STOP_DT_*) live in the
// OpenTTD-EXTRA GRF, which is NOT on this disk (would render "?"), so we FAKE the
// look with base ogfx1_base.grf sprites only: the classic straight-road ground
// sprite (SPR_ROAD_X 1333 / SPR_ROAD_Y 1332) plus ONE base bus-stop BUILD sprite
// (2696/2697) offset to the roadside as the shelter — every SpriteID < 4896.
// The zeroed common `_tile_type_station_procs` in m1_deadpools.c merges into this
// strong definition, exactly like m1_water_draw.cpp. Also exposes r1_place_bus_stop()
// which writes the real MP_STATION drive-through road-stop tile (via
// MakeDriveThroughRoadStop). (R1 render-merge.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "tile_map.h"
#include "station_map.h"       /* MakeDriveThroughRoadStop, GetStationGfx, MP_STATION accessors */
#include "station_base.h"      /* Station::GetIfValid (R1-164: link the stop into the chain) */
#include "roadstop_base.h"     /* RoadStop pool object at the stop tile */
#include "roadveh.h"           /* RoadVehicle, RVSB_* (R1-168: real stop entry proc) */
#include "track_func.h"        /* IsReversingRoadTrackdir */

extern "C" void ottd_log(const char *fmt, ...);   /* netlog tee (sink + file) */
#include "road_map.h"          /* GetRoadOwner, RTT_ROAD */
#include "company_func.h"      /* _local_company */

#include "sprite.h"            /* GENERAL_SPRITE_COLOUR (recolour palette for the building) */
#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/* One building line of the classic bus-stop shelter (subset of DrawTileSeqStruct: the fields we
 * use). We draw the FULL 3-part bay-stop BUILDING (A+B+C) over a through-road ground, so it reads
 * as a real, VISIBLE shelter (a single thin part was near-invisible). Offsets/dims transcribed
 * VERBATIM from table/station_land.h (_station_display_datas_71 = NE bay, _72 = SE bay). All sprites
 * 2696-2705 are classic ogfx1_base.grf (< 4896). They are PALETTE_MODIFIER_COLOUR recolour sprites,
 * so they MUST be drawn with a recolour palette (drawing them PAL_NONE renders index garbage — the
 * cause of the "invisible stop" + on-scroll smearing). Index by Axis: [0]=AXIS_X, [1]=AXIS_Y. */
struct R1SeqLine { int8 dx, dy, dz; uint8 sx, sy, sz; SpriteID img; };

/* TWO shelter walls per axis, flanking the carriageway on the two edges PARALLEL to the road (the
 * road band stays clear in the middle) — mimics the real drive-through layout (station_land.h
 * _station_display_datas for DT: two side shelters, clear centre) using base ogfx1_base.grf BUILD_A
 * wall sprites (2696-2699, all < 4896). Two walls read clearly as a stop (one thin wall was
 * invisible) WITHOUT covering the road (all 3 bay parts boxed the tile and covered it). RECOLOURED —
 * these are PALETTE_MODIFIER_COLOUR sprites, so PAL_NONE renders index garbage (the invisible bug). */
static const R1SeqLine _r1_stop_shelter[2][2] = {
	{	/* AXIS_X (road SW<->NE, ground SPR_ROAD_X): the SUBSTANTIAL BUILD_C walls on the N (y=0)
		 * and S (y=13) edges, road band clear between (y=3..13). NOT the thin BUILD_A (invisible). */
		{  3,  0, 0, 13,  3, 10, SPR_BUS_STOP_SW_BUILD_C },
		{  0, 13, 0, 13,  3, 10, SPR_BUS_STOP_NE_BUILD_C },
	},
	{	/* AXIS_Y (road SE<->NW, ground SPR_ROAD_Y): the BUILD_B walls on the W (x=0) and E (x=13)
		 * edges, road band clear between (x=3..13). */
		{  0,  0, 0,  3, 16, 10, SPR_BUS_STOP_SW_BUILD_B },
		{ 13,  0, 0,  3, 16, 10, SPR_BUS_STOP_NE_BUILD_B },
	},
};

static void DrawTile_Station(TileInfo *ti)
{
	/* Drive-through bus stop: StationGfx (m5) is GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET (4) + Axis,
	 * so bit 0 of the gfx is the road Axis (0 = AXIS_X, 1 = AXIS_Y). See MakeDriveThroughRoadStop. */
	Axis axis = (Axis)(GetStationGfx(ti->tile) & 1);

	/* 1) Straight-road GROUND so the road runs THROUGH the tile, UNBLOCKED (drive-through: the bus
	 *    drives ONTO it). AXIS_X -> SPR_ROAD_X (1333), AXIS_Y -> SPR_ROAD_Y (1332). */
	DrawGroundSprite(axis == AXIS_X ? SPR_ROAD_X : SPR_ROAD_Y, PAL_NONE);

	/* 2) Two shelter walls flanking the carriageway (road clear between), RECOLOURED so they show. */
	PaletteID pal = GENERAL_SPRITE_COLOUR(COLOUR_ORANGE);
	for (int i = 0; i < 2; i++) {
		const R1SeqLine &s = _r1_stop_shelter[axis][i];
		AddSortableSpriteToDraw(s.img, pal, ti->x + s.dx, ti->y + s.dy, s.sx, s.sy, s.sz, ti->z + s.dz);
	}
}

static int GetSlopePixelZ_Station(TileIndex tile, uint x, uint y)
{
	return 0;   // our world is flat at height 0
}

static Foundation GetFoundation_Station(TileIndex tile, Slope tileh)
{
	return FOUNDATION_NONE;
}

static CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags)
{
	return_cmd_error(STR_EMPTY);   // never on the live path (we never clear stations)
}

static void GetTileDesc_Station(TileIndex tile, TileDesc *td)
{
	td->str = STR_EMPTY;
	td->owner[0] = GetTileOwner(tile);
}

static void TileLoop_Station(TileIndex tile) {}
static void ChangeTileOwner_Station(TileIndex tile, Owner old_owner, Owner new_owner) {}

/* R1-170: the ROAD branch of the real GetTileTrackStatus_Station
 * (station_cmd.cpp:3220), verbatim. The old `return 0` made every stop tile
 * TRACKLESS: the real controller (and the pathfinder's trackdir filter) saw a
 * WALL where the drive-through stop is, chose the reversing trackdir at the
 * adjacent junction, and orbited the block forever — the actual root of the
 * R1-149→169 "bus never arrives" saga. Rail/water stay 0: no rail stations or
 * buoys exist in R1. */
static TrackStatus GetTileTrackStatus_Station(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TrackBits trackbits = TRACK_BIT_NONE;

	if (mode == TRANSPORT_ROAD && IsRoadStop(tile)) {
		RoadTramType rtt = (RoadTramType)sub_mode;
		if (HasTileRoadType(tile, rtt)) {
			DiagDirection dir = GetRoadStopDir(tile);
			Axis axis = DiagDirToAxis(dir);
			if (side == INVALID_DIAGDIR ||
			    (axis == DiagDirToAxis(side) && !(IsStandardRoadStopTile(tile) && dir != side))) {
				trackbits = AxisToTrackBits(axis);
			}
		}
	}

	return CombineTrackStatus(TrackBitsToTrackdirBits(trackbits), TRACKDIR_BIT_NONE);
}

static CommandCost TerraformTile_Station(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	return_cmd_error(STR_EMPTY);
}

/* R1-168: the VEH_ROAD branch of the real VehicleEnter_Station
 * (station_cmd.cpp:3330), verbatim. With this slot nullptr the rung-2 bus DROVE
 * STRAIGHT THROUGH its stop: no RoadStop::Enter, no bay, no state change, no
 * arrival — and then orbited the block forever chasing the unsatisfied order
 * (R1-166/167 traces). Trains stay out: the cosmetic train never stops. */
static VehicleEnterTileStatus R1VehicleEnter_Station(Vehicle *v, TileIndex tile, int, int)
{
	if (v->type == VEH_ROAD) {
		RoadVehicle *rv = RoadVehicle::From(v);
		static int s_enter_logs = 0;
		if (s_enter_logs < 12) {
			s_enter_logs++;
			ottd_log("R1 ENTER: tile=%u state=%u frame=%u front=%d isstop=%d",
			         (unsigned)tile, (unsigned)rv->state, (unsigned)rv->frame,
			         (int)rv->IsFrontEngine(), (int)IsRoadStop(tile));
		}
		if (rv->state < RVSB_IN_ROAD_STOP && !IsReversingRoadTrackdir((Trackdir)rv->state) && rv->frame == 0) {
			if (IsRoadStop(tile) && rv->IsFrontEngine()) {
				/* Attempt to allocate a parking bay in a road stop */
				return RoadStop::GetByTile(tile, GetRoadStopType(tile))->Enter(rv) ? VETSB_CONTINUE : VETSB_CANNOT_ENTER;
			}
		}
	}
	return VETSB_CONTINUE;
}

extern const TileTypeProcs _tile_type_station_procs = {
	DrawTile_Station,           // draw_tile_proc
	GetSlopePixelZ_Station,     // get_slope_z_proc
	ClearTile_Station,          // clear_tile_proc
	nullptr,                    // add_accepted_cargo_proc
	GetTileDesc_Station,        // get_tile_desc_proc
	GetTileTrackStatus_Station, // get_tile_track_status_proc
	nullptr,                    // click_tile_proc
	nullptr,                    // animate_tile_proc
	TileLoop_Station,           // tile_loop_proc
	ChangeTileOwner_Station,    // change_tile_owner_proc
	nullptr,                    // add_produced_cargo_proc
	R1VehicleEnter_Station,     // vehicle_enter_tile_proc (R1-168: real road-stop entry)
	GetFoundation_Station,      // get_foundation_proc
	TerraformTile_Station,      // terraform_tile_proc
};

/* ============================================================================
 * R1 driver glue: write a REAL MP_STATION DRIVE-THROUGH road-stop (bus) tile so
 * DrawTile_Station above actually fires in the viewport. Call at the end of a bus route.
 *   tile          — MP_ROAD (or clear) tile to convert; typically the route's last tile.
 *   station_index — the owning Station::index (StationID). See r1_town_station_index.
 *   axis          — the ROAD AXIS the stop sits on: 0 = AXIS_X (road runs SW<->NE),
 *                   1 = AXIS_Y (road runs SE<->NW). A drive-through stop PRESERVES the
 *                   road along this axis in BOTH directions, so it does NOT block the
 *                   street — pass the same axis the underlying road runs.
 * Station owner is _local_company (so the viewport uses the company palette); the ROAD
 * keeps its existing owner (read BEFORE converting) so through-traffic ownership is intact,
 * falling back to OWNER_TOWN for our town-associated inter-town roads. tram roadtype is
 * INVALID_ROADTYPE (no tram). Defensive: bounds-checks the tile.
 * ============================================================================ */
extern "C" void r1_place_bus_stop(unsigned tile, unsigned station_index, int axis)
{
	TileIndex t = (TileIndex)tile;
	if (t >= MapSize()) return;              // out of range
	if (IsTileType(t, MP_VOID)) return;      // never write over the map border

	/* Preserve the road's existing owner if this is already a normal road tile; else the
	 * road is town-associated (our inter-town network) so OWNER_TOWN is the right owner. */
	Owner road_owner = IsTileType(t, MP_ROAD) ? GetRoadOwner(t, RTT_ROAD) : OWNER_TOWN;

	Axis a = (Axis)(((unsigned)axis) & 1);
	MakeDriveThroughRoadStop(t, _local_company /*station*/, road_owner /*road*/,
	                         INVALID_OWNER /*tram*/, (StationID)station_index,
	                         ROADSTOP_BUS, ROADTYPE_ROAD, INVALID_ROADTYPE, a);

	/* R1-164: the map tile alone is only half a stop. The real controller's arrival
	 * path (roadveh_cmd.cpp:1496) does RoadStop::GetByTile(v->tile,...) — a walk of
	 * the owning Station's bus_stops chain comparing rs->xy — and the BFS pathfinder
	 * shim aims at chain entries. Without a pool object AT THIS TILE the walk finds
	 * nothing (null deref on arrival) and the shim can only aim at the Rung-1
	 * sign-tile placeholder (the R1-149/151 "bus orbits the sign" trace). Append a
	 * real RoadStop exactly like CmdBuildRoadStop does. */
	Station *st = Station::GetIfValid((StationID)station_index);
	if (st != nullptr && RoadStop::CanAllocateItem()) {
		RoadStop *rs = new RoadStop(t);
		/* R1-166: a DRIVE-THROUGH stop needs its Entry bookkeeping built (the
		 * real CmdBuildRoadStop calls this right after MakeDriveThroughRoadStop,
		 * station_cmd.cpp:1914). Without it RoadStop::Enter refuses the vehicle
		 * and the controller diverts around the stop forever (R1-166 trace: bus
		 * chose the stopward trackdir, got turned away, looped the block). Must
		 * run AFTER the map tile is written — MakeDriveThrough scans it. */
		rs->MakeDriveThrough();
		RoadStop **tail = &st->bus_stops;
		while (*tail != nullptr) tail = &(*tail)->next;
		*tail = rs;
	}
}
