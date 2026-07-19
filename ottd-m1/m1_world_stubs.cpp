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

// R1 world-tiles seam: the handful of off-path symbols the real tree_cmd.cpp (and
// later water_cmd.cpp) reference that we don't compile. All no-ops — never hit on
// the draw/grow path for plain grass-ground trees. (R1 render-merge only.)
#include "stdafx.h"
#include "water.h"

// Referenced by tree_cmd for shore/water-ground trees, which we never place.
void DrawShoreTile(Slope) {}
void TileLoop_Water(TileIndex) {}
