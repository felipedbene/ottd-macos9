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

// Authoritative ground-sprite atlas function (from agent-tiles/tile_sprites.h).
// Kept in its own TU: tile_sprites.h's global SLOPE_* enum would collide with
// OpenTTD's slope_type.h, so this must NOT see any OpenTTD header.
#include "tile_sprites.h"

// grass ground sprite for a slope: base + SlopeToSpriteOffset(slope).
// (src/landscape.cpp:79 _slope_to_sprite_offset; verified against the real GRF.)
extern "C" int GroundSpriteForSlope(int slope)
{
    return SPR_FLAT_GRASS_TILE + SLOPE_TO_SPRITE_OFFSET[slope & 0x1F];
}
