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

// M1: stub the NewGRF profiler. date.cpp's OnNewDay checks
// !_newgrf_profilers.empty() (default-empty -> dead branch) and may call
// NewGRFProfiler::FinishAll(). No NewGRFs are loaded headless.
#include "stdafx.h"
#include "newgrf_profiling.h"

std::vector<NewGRFProfiler> _newgrf_profilers;
Date _newgrf_profile_end_date;
uint32 NewGRFProfiler::FinishAll() { return 0; }
NewGRFProfiler::~NewGRFProfiler() {}   // vector<NewGRFProfiler> dtor refs it; vector is always empty
