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
 * m1_industry.cpp — a REAL pooled Industry (R1-81), the next rung after
 * Town → Company → Vehicle → Economy → CargoSpec. Same "own the object, stub the
 * cascade" move as m1_company.cpp / m1_vehicle.cpp: own the real IndustryPool + a
 * minimal Industry (empty dtor/PostDestructor, no vtable — Industry is NOT polymorphic,
 * so this is even simpler than the Vehicle rung) WITHOUT compiling industry_cmd.cpp
 * (NewGRF callbacks / news / subsidy / station-catchment cascade).
 *
 * The monthly production tick is REAL: IndustryMonthlyLoop (a no-op stub until now,
 * m1_shims.cpp) is defined here and already called by the live date.cpp OnNewMonth
 * (same block as the proven-firing CompaniesMonthlyLoop). Each industry's
 * produced_cargo_waiting[] stockpile climbs every game-month — a real number on a real
 * pooled object, observable in the info window exactly like town pop / company money.
 * Turning that stockpile into transported INCOME needs a real Station (deferred: the
 * station_cmd.cpp draw-proc seam); the production rung stands alone.
 */
#include "stdafx.h"
#include "industry.h"
#include "core/pool_func.hpp"

#include "safeguards.h"

/* Real IndustryPool — replaces the fake `char _industry_pool[8192]` deadpool (guarded out of
 * m1_deadpools.c under R1_MERGE). industry_cmd.cpp (which also defines these) is NOT compiled,
 * so no ODR clash. PoolBase machinery is already compiled (core/pool_func.cpp). */
IndustryPool _industry_pool("Industry");
INSTANTIATE_POOL_METHODS(Industry)

/* Static per-type industry count (real home industry_cmd.cpp:63). The inline
 * IncIndustryTypeCount/GetIndustryTypeCount in industry.h touch it. */
/* static */ uint16 Industry::counts[NUM_INDUSTRYTYPES];

/* Minimal dtor / PostDestructor — the real bodies (industry_cmd.cpp) drag GetIndustrySpec +
 * subsidy + news + stations_near teardown. Empty is safe (exactly like Company::~Company): the
 * pool is Tzero and we never delete industries at runtime in R1. */
Industry::~Industry() {}
/* static */ void Industry::PostDestructor(size_t) {}

/* The REAL production tick. R1-85: moved to the DAILY loop (was monthly) — days roll ~30x more
 * often than months (DAY_TICKS=74 vs ~2220/month), so the stockpile climbs visibly within a
 * session instead of sitting at ~0 until the first month rolls over. Spec-free (hardcoded on the
 * object, never touches the stubbed GetIndustrySpec): each produced cargo grows by production_rate
 * per day, clamped to the uint16 field max. Nothing consumes it yet (no station delivery of
 * industry cargo), so it accumulates toward the cap — fine for a production demo. date.cpp's
 * OnNewDay already calls this (its m1_shims stub is removed). */
void IndustryDailyLoop()
{
	for (Industry *ind : Industry::Iterate()) {
		for (size_t j = 0; j < INDUSTRY_NUM_OUTPUTS; j++) {
			if (ind->produced_cargo[j] == CT_INVALID) continue;
			uint16 rate = ind->production_rate[j];
			uint32 stock = (uint32)ind->produced_cargo_waiting[j] + rate;
			ind->produced_cargo_waiting[j] = (uint16)(stock > 0xFFFF ? 0xFFFF : stock);
			uint32 mprod = (uint32)ind->this_month_production[j] + rate;
			ind->this_month_production[j] = (uint16)(mprod > 0xFFFF ? 0xFFFF : mprod);
		}
	}
}

/* Monthly: roll this-month's production total into last-month and reset (real OpenTTD stat). */
void IndustryMonthlyLoop()
{
	for (Industry *ind : Industry::Iterate()) {
		for (size_t j = 0; j < INDUSTRY_NUM_OUTPUTS; j++) {
			ind->last_month_production[j] = ind->this_month_production[j];
			ind->this_month_production[j] = 0;
		}
	}
}

/* Stand up ONE real industry in the pool. Returns its IndustryID (so r1_scene can hand the real
 * id to MakeIndustry and the map tiles belong to the real object), or INVALID_INDUSTRY on a full
 * pool. The pool is Tzero, so produced_cargo[]/accepts_cargo[] come up 0 (== CT_PASSENGERS, since
 * CT_INVALID is 0xFF) — they MUST be explicitly reset to CT_INVALID. town is left nullptr: the
 * only compiled reader (town_cmd.cpp GetByTile(...)->town, on the never-hit town-deletion path)
 * only pointer-compares it. */
extern "C" unsigned r1_make_industry(unsigned tile, unsigned w, unsigned h, int type,
                                     unsigned char produced, unsigned char rate)
{
	if (!Industry::CanAllocateItem()) return 0xFFFF;
	Industry *ind = new Industry((TileIndex)tile);
	ind->location  = TileArea((TileIndex)tile, (uint8)(w < 1 ? 1 : w), (uint8)(h < 1 ? 1 : h));
	ind->town      = nullptr;
	ind->type      = (IndustryType)type;
	ind->owner     = OWNER_NONE;
	ind->prod_level = PRODLEVEL_DEFAULT;
	for (size_t j = 0; j < INDUSTRY_NUM_OUTPUTS; j++) { ind->produced_cargo[j] = CT_INVALID; ind->production_rate[j] = 0; ind->produced_cargo_waiting[j] = 0; }
	for (size_t j = 0; j < INDUSTRY_NUM_INPUTS;  j++) { ind->accepts_cargo[j]  = CT_INVALID; }
	ind->produced_cargo[0]  = (CargoID)produced;
	ind->production_rate[0] = rate;
	ind->produced_cargo_waiting[0] = (uint16)(rate * 2);   /* seed so Cargo is non-zero immediately */
	Industry::IncIndustryTypeCount((IndustryType)type);
	return (unsigned)ind->index;
}

/* Number of real Industry objects in the pool (diagnostic: distinguishes "no industries created"
 * from "industries exist but production stuck" when Cargo reads 0 on HW). */
extern "C" unsigned r1_industry_count(void)
{
	return (unsigned)Industry::GetNumItems();
}

/* Total produced-cargo stockpile across all industries — for the info-window HUD readout so the
 * monthly production is observable (like the population/money lines). */
extern "C" unsigned long r1_industry_stockpile(void)
{
	unsigned long total = 0;
	for (const Industry *ind : Industry::Iterate())
		for (size_t j = 0; j < INDUSTRY_NUM_OUTPUTS; j++)
			if (ind->produced_cargo[j] != CT_INVALID) total += ind->produced_cargo_waiting[j];
	return total;
}
