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
#include "train.h"             /* Train, GetTrainStopLocation (rung 6 phase B) */
#include "newgrf_station.h"    /* IsStationTileBlocked declaration */
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
	/* RUNG 6 phase B: rail platform tile — leveled foundation on slopes, the
	 * through track, and the classic platform edge pair (rear + front). */
	if (HasStationRail(ti->tile)) {
		if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);
		bool x_axis = GetRailStationAxis(ti->tile) == AXIS_X;
		DrawGroundSprite(x_axis ? SPR_RAIL_TRACK_X : SPR_RAIL_TRACK_Y, PAL_NONE);
		PaletteID ppal = GENERAL_SPRITE_COLOUR(COLOUR_GREY);
		if (x_axis) {
			AddSortableSpriteToDraw(SPR_RAIL_PLATFORM_X_REAR,  ppal, ti->x,     ti->y,      16,  5, 2, ti->z);
			AddSortableSpriteToDraw(SPR_RAIL_PLATFORM_X_FRONT, ppal, ti->x,     ti->y + 11, 16,  5, 2, ti->z);
		} else {
			AddSortableSpriteToDraw(SPR_RAIL_PLATFORM_Y_REAR,  ppal, ti->x,      ti->y,      5, 16, 2, ti->z);
			AddSortableSpriteToDraw(SPR_RAIL_PLATFORM_Y_FRONT, ppal, ti->x + 11, ti->y,      5, 16, 2, ti->z);
		}
		return;
	}

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
	/* RUNG 6 phase B: real terrain z, leveled (station tiles always sit on a
	 * flat surface — mirrors station_cmd's GetSlopePixelZ_Station). The old
	 * `return 0` was another flat-world relic: it would sink a train at the
	 * platform exactly like the rail proc's did on the open line. */
	int z;
	Slope tileh = GetTilePixelSlope(tile, &z);
	if (tileh != SLOPE_FLAT) z += TILE_HEIGHT;   /* leveled foundation top */
	return z;
}

static Foundation GetFoundation_Station(TileIndex tile, Slope tileh)
{
	return FlatteningFoundation(tileh);
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
	} else if (mode == TRANSPORT_RAIL && HasStationRail(tile) && !IsStationTileBlocked(tile)) {
		/* RUNG 6 phase B: rail platforms are track (the rail twin of the road
		 * branch above — the same `return 0` here would be a trackless wall). */
		trackbits = TrackToTrackBits(GetRailStationTrack(tile));
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
static VehicleEnterTileStatus R1VehicleEnter_Station(Vehicle *v, TileIndex tile, int x, int y)
{
	if (v->type == VEH_TRAIN) {
		/* RUNG 6 phase B: the VEH_TRAIN branch of the real VehicleEnter_Station
		 * (station_cmd.cpp:3295), verbatim — slows the train toward the platform
		 * stop point and fires VETSB_ENTERED_STATION -> TrainEnterStation ->
		 * BeginLoading at the exact stop location. */
		StationID station_id = GetStationIndex(tile);
		if (!v->current_order.ShouldStopAtStation(v, station_id)) return VETSB_CONTINUE;
		if (!IsRailStation(tile) || !v->IsFrontEngine()) return VETSB_CONTINUE;

		int station_ahead;
		int station_length;
		int stop = GetTrainStopLocation(station_id, tile, Train::From(v), &station_ahead, &station_length);

		if (stop + station_ahead - (int)TILE_SIZE >= station_length) return VETSB_CONTINUE;

		DiagDirection dir = DirToDiagDir(v->direction);

		x &= 0xF;
		y &= 0xF;

		if (DiagDirToAxis(dir) != AXIS_X) Swap(x, y);
		if (y == TILE_SIZE / 2) {
			if (dir != DIAGDIR_SE && dir != DIAGDIR_SW) x = TILE_SIZE - 1 - x;
			stop &= TILE_SIZE - 1;

			if (x == stop) {
				return VETSB_ENTERED_STATION | (VehicleEnterTileStatus)(station_id << VETS_STATION_ID_OFFSET);
			} else if (x < stop) {
				v->vehstatus |= VS_TRAIN_SLOWING;
				uint16 spd = std::max(0, (stop - x) * 20 - 15);
				if (spd < v->cur_speed) v->cur_speed = spd;
			}
		}
	} else if (v->type == VEH_ROAD) {
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

/* ============================================================================
 * RUNG 6 phase B: write a REAL MP_STATION rail platform tile and attach it to
 * an EXISTING town Station (a Station may have spread-out parts, so the rail
 * halt joins the town's bus station: same cargo pool, same fares, same HUD).
 *   tile          — plain-rail line tile to convert (its X/Y track is kept).
 *   station_index — owning Station::index (see r1_town_station_index).
 *   axis          — 0 = AXIS_X platform (line runs SW<->NE), 1 = AXIS_Y.
 * Mirrors what CmdBuildRailStation leaves behind for a 1x1 platform: the map
 * tile (section 0), the train_station TileArea and FACIL_TRAIN.
 * ============================================================================ */
extern "C" void r1_place_rail_station(unsigned tile, unsigned station_index, int axis)
{
	TileIndex t = (TileIndex)tile;
	if (t >= MapSize()) return;
	if (IsTileType(t, MP_VOID)) return;

	Station *st = Station::GetIfValid((StationID)station_index);
	if (st == nullptr) return;

	MakeRailStation(t, _local_company, (StationID)station_index,
	                (Axis)(((unsigned)axis) & 1), 0 /* section */, (RailType)0);
	MarkTileDirtyByTile(t);

	/* Each R1 station carries exactly ONE 1x1 platform (tile_area.cpp is not
	 * linked, so no TileArea::Add; and the zeroed pool leaves train_station.tile
	 * at 0, not INVALID_TILE, so an emptiness test would lie anyway). */
	st->train_station = TileArea(t, 1, 1);
	st->facilities |= FACIL_TRAIN;
}
