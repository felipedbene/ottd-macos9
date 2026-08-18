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

/* RUNG 6: the REAL railtype registry. rail_cmd.cpp (its true home) is not
 * compiled, and the zeroed `char _railtypes[131072]` deadpool made
 * HasPowerOnRail() reject every engine/track pair — measured on the R1-192
 * QEMU run as "CmdBuildRailVehicle built nothing". This TU owns the real
 * array plus a ResetRailTypes-alike seeding it from the original table,
 * exactly the ResetRoadTypes lesson from rung 2 (m1 deadpool entry guarded
 * out under R1_REAL_TRAIN_STACK). */
#include "stdafx.h"
#include "rail.h"
#include "core/math_func.hpp"

#include "table/strings.h"
#include "table/sprites.h"
#include "table/railtypes.h"   /* _original_railtypes */

#include "safeguards.h"

RailtypeInfo _railtypes[RAILTYPE_END];

/* Faithful copy of rail_cmd.cpp's ResetRailTypes minus the NewGRF resolver
 * hooks (none load in R1). */
extern "C" void r1_reset_railtypes(void)
{
	static_assert(lengthof(_original_railtypes) <= lengthof(_railtypes), "table too big");

	uint i = 0;
	for (; i < lengthof(_original_railtypes); i++) _railtypes[i] = _original_railtypes[i];

	static const RailtypeInfo empty_railtype = {};
	for (; i < lengthof(_railtypes); i++) _railtypes[i] = empty_railtype;
}
