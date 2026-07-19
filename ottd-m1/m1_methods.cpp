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

// M1: out-of-line no-op definitions for two member functions that date.cpp
// references only from the MAX_YEAR calendar-wrap dead branch (never taken for
// the 1950s run). Defining them here avoids compiling vehicle.cpp / linkgraph.
#include "stdafx.h"
#include "vehicle_base.h"
#include "linkgraph/linkgraph.h"

void Vehicle::ShiftDates(int) {}
void LinkGraph::ShiftDates(int) {}
