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

// R1 minimal rail draw (the "trains" arc, step 1): a custom _tile_type_rail_procs
// so the real viewport can DRAW an MP_RAILWAY straight-track tile WITHOUT pulling
// rail_cmd.cpp's signal/depot/electrification/NewGRF/YAPF machinery. Our world is
// flat at height 0 and every rail tile we lay is a simple straight X or Y piece
// (we build a plain loop), so the draw is just the CLASSIC combined track+ground
// sprite: SPR_RAIL_TRACK_Y (1011) for a Y piece, SPR_RAIL_TRACK_X (1012) for an X
// piece — both base ogfx1_base.grf sprites (< 4896, no extra GRF, no "?"). These
// are exactly base_sprites.track_y (+0 / +1) that DrawTrackBits picks for a single
// TRACK_BIT_Y / TRACK_BIT_X on flat ground, so it matches the real look. The zeroed
// common `_tile_type_rail_procs` in m1_deadpools.c merges into this strong
// definition, exactly like m1_water_draw.cpp / m1_station_draw.cpp. Also exposes
// r1_place_rail() which writes the real MP_RAILWAY normal-track tile (via
// MakeRailNormal, base rail type 0). (R1 render-merge.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "tile_map.h"
#include "rail_map.h"          /* MakeRailNormal, GetTrackBits, TrackBits, RailType */
#include "company_func.h"      /* _local_company */

#include "table/strings.h"
#include "table/sprites.h"

/* rail_cmd.cpp's _track_sloped_sprites: offset from SPR_RAIL_TRACK_Y for
 * track drawn on a (foundation-adjusted) non-flat slope, indexed tileh-1. */
static const byte _r1_track_sloped_sprites[14] = {
	14, 15, 22, 13,
	 0, 21, 17, 12,
	23,  0, 18, 20,
	19, 16
};

/* GetRailFoundation reduced to the straight X/Y pieces R1 lays (no junction,
 * halftile or overlap cases). Natural ramps along the axis carry sloped track
 * with no foundation; a single raised corner gets an inclined foundation;
 * everything else is leveled. The R1-104 flat-world drawer ignored slope
 * entirely — on the organic terrain that cut the line flat through hillsides
 * and (via get_slope_z_proc returning 0) sank the train INTO the hill. */
static Foundation GetFoundation_Rail(TileIndex tile, Slope tileh)
{
	if (tileh == SLOPE_FLAT) return FOUNDATION_NONE;
	bool x_piece = (GetTrackBits(tile) & TRACK_BIT_X) != 0;
	if (!IsSteepSlope(tileh)) {
		if (x_piece  && (tileh == SLOPE_NE || tileh == SLOPE_SW)) return FOUNDATION_NONE;
		if (!x_piece && (tileh == SLOPE_NW || tileh == SLOPE_SE)) return FOUNDATION_NONE;
		if (IsSlopeWithOneCornerRaised(tileh)) return x_piece ? FOUNDATION_INCLINED_X : FOUNDATION_INCLINED_Y;
		return FOUNDATION_LEVELED;
	}
	return x_piece ? FOUNDATION_INCLINED_X : FOUNDATION_INCLINED_Y;
}

static void DrawTile_Rail(TileInfo *ti)
{
	TrackBits track = GetTrackBits(ti->tile);
	Foundation f = GetFoundation_Rail(ti->tile, ti->tileh);
	if (f != FOUNDATION_NONE) DrawFoundation(ti, f);   /* adjusts ti->tileh/ti->z */
	SpriteID image;
	if (ti->tileh != SLOPE_FLAT) {
		image = SPR_RAIL_TRACK_Y + _r1_track_sloped_sprites[ti->tileh - 1];
	} else {
		image = (track & TRACK_BIT_X) ? SPR_RAIL_TRACK_X : SPR_RAIL_TRACK_Y;
	}
	DrawGroundSprite(image, PAL_NONE);
}

/* Mirror of rail_cmd's GetSlopePixelZ_Track: real terrain z + foundation. */
static int GetSlopePixelZ_Rail(TileIndex tile, uint x, uint y)
{
	int z;
	Slope tileh = GetTilePixelSlope(tile, &z);
	if (tileh == SLOPE_FLAT) return z;
	z += ApplyPixelFoundationToSlope(GetFoundation_Rail(tile, tileh), &tileh);
	return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
}

static CommandCost ClearTile_Rail(TileIndex tile, DoCommandFlag flags)
{
	return_cmd_error(STR_EMPTY);   // never on the live path (we never clear rail)
}

static void GetTileDesc_Rail(TileIndex tile, TileDesc *td)
{
	td->str = STR_EMPTY;
	td->owner[0] = GetTileOwner(tile);
}

static void TileLoop_Rail(TileIndex tile) {}
static void ChangeTileOwner_Rail(TileIndex tile, Owner old_owner, Owner new_owner) {}

static TrackStatus GetTileTrackStatus_Rail(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static CommandCost TerraformTile_Rail(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	return_cmd_error(STR_EMPTY);
}

extern const TileTypeProcs _tile_type_rail_procs = {
	DrawTile_Rail,           // draw_tile_proc
	GetSlopePixelZ_Rail,     // get_slope_z_proc
	ClearTile_Rail,          // clear_tile_proc
	nullptr,                 // add_accepted_cargo_proc
	GetTileDesc_Rail,        // get_tile_desc_proc
	GetTileTrackStatus_Rail, // get_tile_track_status_proc
	nullptr,                 // click_tile_proc
	nullptr,                 // animate_tile_proc
	TileLoop_Rail,           // tile_loop_proc
	ChangeTileOwner_Rail,    // change_tile_owner_proc
	nullptr,                 // add_produced_cargo_proc
	nullptr,                 // vehicle_enter_tile_proc
	GetFoundation_Rail,      // get_foundation_proc
	TerraformTile_Rail,      // terraform_tile_proc
};

/* ============================================================================
 * R1 driver glue: write a REAL MP_RAILWAY normal straight-track tile so
 * DrawTile_Rail above actually fires in the viewport. Call to lay one piece.
 *   tile — the tile to convert to rail; must be in range (not MP_VOID).
 *   axis — 0 = X piece (TRACK_BIT_X, runs SW<->NE), 1 = Y piece (TRACK_BIT_Y,
 *          runs SE<->NW).
 * Owner is _local_company (so the viewport uses the company palette); rail type
 * is 0 (the first/base rail — plain non-electrified track). Defensive: bounds-
 * checks the tile and refuses the map border.
 * ============================================================================ */
extern "C" void r1_place_rail(unsigned tile, int axis)
{
	TileIndex t = (TileIndex)tile;
	if (t >= MapSize()) return;              // out of range
	if (IsTileType(t, MP_VOID)) return;      // never write over the map border

	TrackBits bits = (axis & 1) ? TRACK_BIT_Y : TRACK_BIT_X;
	MakeRailNormal(t, _local_company, bits, (RailType)0);
}

#include "safeguards.h"
