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
 * landscape_render.cpp -- Isometric landscape compositor core.
 *
 * Plain, portable C++ (no Mac Toolbox, no OpenTTD headers). Every formula
 * below is a faithful reimplementation of OpenTTD 13.4 source, cited inline
 * as file:line against /Users/felipe/ottd-macos9/openttd-13.4/src/.
 */
#include "landscape_render.h"

namespace iso {

/* --------------------------------------------------------------------------
 * Slope from four corner heights.
 *
 * Verbatim port of GetTileSlopeGivenHeight(), src/tile_map.cpp:24-50.
 *
 *   int hminnw = min(hnorth, hwest);
 *   int hmines = min(heast, hsouth);
 *   int hmin   = min(hminnw, hmines);          // base height = lowest corner
 *   int hmax   = max(max(hn,hw), max(he,hs));
 *   Slope r = SLOPE_FLAT;
 *   if (hnorth != hmin) r |= SLOPE_N;
 *   if (hwest  != hmin) r |= SLOPE_W;
 *   if (heast  != hmin) r |= SLOPE_E;
 *   if (hsouth != hmin) r |= SLOPE_S;
 *   if (hmax - hmin == 2) r |= SLOPE_STEEP;
 * ------------------------------------------------------------------------ */
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

int SlopeFromCorners(int hnorth, int hwest, int heast, int hsouth, int *base_h)
{
	int hmin = imin(imin(hnorth, hwest), imin(heast, hsouth));
	int hmax = imax(imax(hnorth, hwest), imax(heast, hsouth));

	if (base_h != 0) *base_h = hmin;

	int r = SLOPE_FLAT;
	if (hnorth != hmin) r |= SLOPE_N;
	if (hwest  != hmin) r |= SLOPE_W;
	if (heast  != hmin) r |= SLOPE_E;
	if (hsouth != hmin) r |= SLOPE_S;
	if (hmax - hmin == 2) r |= SLOPE_STEEP;

	return r;
}

/* --------------------------------------------------------------------------
 * World -> screen projection.
 *
 * Verbatim port of RemapCoords(), src/landscape.h:82-89:
 *   pt.x = (y - x) * 2 * ZOOM_LVL_BASE;
 *   pt.y = (y + x - z) * ZOOM_LVL_BASE;
 *
 * x,y run in world units (tile coord * TILE_SIZE); z in world height units
 * (height level * TILE_HEIGHT). ZOOM_LVL_BASE = 4 (zoom_type.h:16).
 *
 * Consequence per +1 tile in map X: wx += TILE_SIZE(16) ->
 *   dsx = (0 - 16)*2*4 = -128 ... but the sprite grid uses TILE_PIXELS(32)
 *   spacing at ZOOM_LVL_BASE; the harness divides screen coords by
 *   ZOOM_LVL_BASE to get 1:1 pixels, giving the standard (+32,+16)/(-32,+16)
 *   tile steps quoted in the task. Kept exact here; harness normalizes.
 * ------------------------------------------------------------------------ */
void RemapCoords(int wx, int wy, int wz, int *sx, int *sy)
{
	*sx = (wy - wx) * 2 * ZOOM_LVL_BASE;
	*sy = (wy + wx - wz) * ZOOM_LVL_BASE;
}

/* --------------------------------------------------------------------------
 * Grass slope -> sprite id.
 *
 * OpenTTD draws grass ground as:
 *     SPR_FLAT_GRASS_TILE + SlopeToSpriteOffset(slope)
 *   SPR_FLAT_GRASS_TILE = 3981                    (table/sprites.h:581)
 *   SlopeToSpriteOffset(s) = _slope_to_sprite_offset[s]
 *                                                 (slope_func.h:415-419)
 *
 * _slope_to_sprite_offset[32], verbatim from src/landscape.cpp:79-82:
 *   { 0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
 *     0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0 }
 *
 * Index is the raw Slope value (0..31). Entries 15 and 16..31 that are 0
 * are unreachable non-continuous combos; the meaningful steep entries are:
 *   [23]=SLOPE_STEEP_W ->16, [27]=SLOPE_STEEP_S ->17,
 *   [29]=SLOPE_STEEP_E ->15, [30]=SLOPE_STEEP_N ->18.
 * ------------------------------------------------------------------------ */
static const int SPR_FLAT_GRASS_TILE = 3981;

static const unsigned char _slope_to_sprite_offset[32] = {
	0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
	0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0
};

int GroundSpriteForSlopeFallback(int slope)
{
	return SPR_FLAT_GRASS_TILE + _slope_to_sprite_offset[slope & 0x1F];
}

/*
 * Weak default for the "authoritative atlas" symbol so the core links and
 * runs standalone on the host. The real port supplies a strong definition.
 */
extern "C" int GroundSpriteForSlope(int slope)
{
	return GroundSpriteForSlopeFallback(slope);
}

/* --------------------------------------------------------------------------
 * Scene builder: heightmap -> ordered draw list.
 *
 * Corner->tile mapping (src/tile_map.cpp:65-70):
 *   hnorth = H(x,   y  )
 *   hwest  = H(x+1, y  )
 *   heast  = H(x,   y+1)
 *   hsouth = H(x+1, y+1)
 *
 * Painter's order for iso: the screen-y of a tile increases with (x+y) and
 * with base height decreasing (z subtracts). To guarantee farther tiles are
 * drawn first we iterate by increasing (map_x + map_y); ties by increasing
 * map_x. This is the standard OpenTTD viewport tile scan order and yields a
 * correct back-to-front overdraw for overlapping raised tiles.
 * ------------------------------------------------------------------------ */
int BuildScene(const unsigned char *heights, int size,
               DrawTile *out, bool use_atlas)
{
	int tiles = size - 1;
	if (tiles <= 0) return 0;

	int n = 0;
	/* diagonal sweep: d = x + y goes 0 .. 2*(tiles-1) */
	for (int d = 0; d <= 2 * (tiles - 1); d++) {
		for (int x = 0; x < tiles; x++) {
			int y = d - x;
			if (y < 0 || y >= tiles) continue;

			int hn = heights[(y)     * size + (x)];
			int hw = heights[(y)     * size + (x + 1)];
			int he = heights[(y + 1) * size + (x)];
			int hs = heights[(y + 1) * size + (x + 1)];

			int base_h = 0;
			int slope = SlopeFromCorners(hn, hw, he, hs, &base_h);

			DrawTile *t = &out[n++];
			t->map_x  = x;
			t->map_y  = y;
			t->slope  = slope;
			t->base_h = base_h;
			t->sprite = use_atlas ? GroundSpriteForSlope(slope)
			                      : GroundSpriteForSlopeFallback(slope);

			int sx, sy;
			RemapCoords(x * TILE_SIZE, y * TILE_SIZE,
			            base_h * TILE_HEIGHT, &sx, &sy);
			t->screen_x = sx;
			t->screen_y = sy;
		}
	}
	return n;
}

} /* namespace iso */
