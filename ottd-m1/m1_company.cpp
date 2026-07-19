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
 * m1_company.cpp — a real single player Company WITHOUT compiling company_cmd.cpp
 * (which drags the whole network stack via network/network_base.h). This follows the
 * Town model: town_cmd.cpp owns Town's ctor + the real TownPool; here (company_cmd.cpp
 * not compiled → no ODR clash) this TU owns Company's minimal ctor/dtor + the real
 * CompanyPool. The pool machinery is header-template (core/pool_func.hpp) + the already
 * compiled core/pool_func.cpp (PoolBase), exactly like _town_pool.
 *
 * r1_make_company() allocates one company at index 0 (COMPANY_FIRST) and points
 * _local_company/_current_company at it. That flips the canonical toolbar's gates
 * (_local_company != COMPANY_SPECTATOR + Company::GetNumItems() > 0, toolbar_gui.cpp
 * :2022/2024) so the build/station/finance/vehicle buttons light up, and gives
 * construction commands a real owner. No faces, no AI, no network, no _settings_game.
 */
#include "stdafx.h"
#include "company_base.h"
#include "company_func.h"     /* _local_company, _current_company, _company_colours */
#include "group.h"            /* GroupStatistics (Company::group_all[]/group_default[]) */
#include "core/pool_func.hpp" /* Pool template method bodies */

#include "safeguards.h"

/* The REAL Company pool — replaces the fake `char _company_pool[8192]` deadpool
 * (removed from m1_deadpools.c). PoolBase (compiled in pool_func.cpp) registers it. */
CompanyPool _company_pool("Company");
INSTANTIATE_POOL_METHODS(Company)

/* Minimal Company ctor: the real one (company_cmd.cpp:61) also reads _settings_game
 * construction limits + calls InvalidateWindowData — both skipped. CompanyProperties()
 * (inline, company_base.h) already zeroed money/loan/colour/etc.; the pool calloc'd the
 * whole object (Tzero=true). */
Company::Company(uint16 name_1, bool is_ai)
{
	this->name_1 = name_1;
	this->location_of_HQ = INVALID_TILE;
	this->is_ai = is_ai;
	for (auto &o : this->share_owners) o = INVALID_OWNER;
}

/* Empty dtor (real one closes company windows). Empty avoids the CleaningPool() dep and
 * is safe: nothing this minimal company allocates needs teardown here. */
Company::~Company()
{
}

/* static */ void Company::PostDestructor(size_t)
{
}

/* GroupStatistics ctor/dtor live in group_cmd.cpp (not compiled). Company embeds 8 of
 * them (group_all[VEH_COMPANY_END] + group_default[VEH_COMPANY_END]); their ctor/dtor are
 * emitted into Company's ctor/dtor above. The real ctor caches num_engines from the
 * Engine pool (absent) — leave it null. */
GroupStatistics::GroupStatistics()
{
	this->num_engines = nullptr;
}

GroupStatistics::~GroupStatistics()
{
	free(this->num_engines);
}

/* Stand up one player company at index 0 and make it local + current. Idempotent. */
void r1_make_company()
{
	if (Company::GetNumItems() > 0) return;

	Company *c = new Company(0, false);     /* plain new -> pool GetNew -> index 0 */
	c->colour = COLOUR_BLUE;
	_company_colours[c->index] = (Colours)c->colour;
	c->money = c->current_loan = 100000;    /* a starting balance */
	c->inaugurated_year = 1950;
	c->avail_railtypes = (RailTypes)0;
	c->avail_roadtypes = (RoadTypes)0;

	_local_company = _current_company = COMPANY_FIRST;
}
