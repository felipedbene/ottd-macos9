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

// R1 minimal industries: a custom _tile_type_industry_procs so the real viewport can
// draw MP_INDUSTRY tiles WITHOUT the Industry pool / NewGRF / production machinery of
// industry_cmd.cpp. We index _industry_draw_tile_data[gfx] exactly like DrawTile_Town
// indexes _town_draw_tile_data — always the COMPLETED construction stage, fixed
// palette (no per-industry random colour, since we keep no Industry objects). Only
// original industries (gfx < NEW_INDUSTRYTILEOFFSET) are placed. Zeroed common
// `_tile_type_industry_procs` in m1_deadpools.c merges into this. (R1 render-merge only.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "industry_map.h"

#include "table/strings.h"
#include "table/sprites.h"
#include "table/industry_land.h"   // _industry_draw_tile_data

static void DrawTile_Industry(TileInfo *ti)
{
	// GetCleanIndustryGfx reads the raw stored gfx (no NewGRF translation, which would
	// pull _industry_tile_specs); our tiles always store an original gfx.
	IndustryGfx gfx = GetCleanIndustryGfx(ti->tile);
	if (gfx >= NEW_INDUSTRYTILEOFFSET) return;   // original industries only
	const DrawBuildingsTileStruct *dits = &_industry_draw_tile_data[(gfx << 2) | 3]; // completed

	if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

	DrawGroundSprite(dits->ground.sprite, dits->ground.pal);
	if (dits->building.sprite != 0)
		AddSortableSpriteToDraw(dits->building.sprite, dits->building.pal,
			ti->x + dits->subtile_x, ti->y + dits->subtile_y,
			dits->width, dits->height, dits->dz, ti->z, false, 0, 0, 0);
}

static int GetSlopePixelZ_Industry(TileIndex tile, uint x, uint y)
{
	return 0;   // our industries sit on flat, height-0 pads
}

static Foundation GetFoundation_Industry(TileIndex tile, Slope tileh)
{
	return FlatteningFoundation(tileh);
}

static CommandCost ClearTile_Industry(TileIndex tile, DoCommandFlag flags)
{
	return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
}

static void GetTileDesc_Industry(TileIndex tile, TileDesc *td)
{
	td->str = STR_EMPTY;
	td->owner[0] = OWNER_NONE;
}

static void TileLoop_Industry(TileIndex tile) {}
static void ChangeTileOwner_Industry(TileIndex tile, Owner old_owner, Owner new_owner) {}

static TrackStatus GetTileTrackStatus_Industry(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static CommandCost TerraformTile_Industry(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
}

extern const TileTypeProcs _tile_type_industry_procs = {
	DrawTile_Industry,           // draw_tile_proc
	GetSlopePixelZ_Industry,     // get_slope_z_proc
	ClearTile_Industry,          // clear_tile_proc
	nullptr,                     // add_accepted_cargo_proc
	GetTileDesc_Industry,        // get_tile_desc_proc
	GetTileTrackStatus_Industry, // get_tile_track_status_proc
	nullptr,                     // click_tile_proc
	nullptr,                     // animate_tile_proc
	TileLoop_Industry,           // tile_loop_proc
	ChangeTileOwner_Industry,    // change_tile_owner_proc
	nullptr,                     // add_produced_cargo_proc
	nullptr,                     // vehicle_enter_tile_proc
	GetFoundation_Industry,      // get_foundation_proc
	TerraformTile_Industry,      // terraform_tile_proc
};
