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
 * m1_economy.cpp — a MINIMAL real economy: the company balance moves each game-month,
 * WITHOUT compiling economy.cpp (network/ai/station/linkgraph/subsidy cascade) or
 * company_cmd.cpp (network_base.h — the reason m1_company.cpp exists). Same trick: own the
 * real Economy/Prices + the money mutators here, stub the rest. Fires monthly via the live
 * OnNewMonth -> CompaniesMonthlyLoop (date.cpp already runs). See [[r1-vehicle-pool]].
 */
#include "stdafx.h"
#include "economy_func.h"     /* SubtractMoneyFromCompany, _economy, _price (externs) */
#include "economy_type.h"     /* Economy, Prices, PR_STATION_VALUE, EXPENSES_* */
#include "economy_base.h"     /* CargoPaymentPool / CargoPayment (R1 rung-b cargo-payment pool) */
#include "core/pool_func.hpp" /* INSTANTIATE_POOL_METHODS (R1 rung-b) */
#include "company_base.h"     /* Company::Iterate / GetIfValid */
#include "vehicle_base.h"     /* Vehicle::Iterate, VEH_ROAD (R1-87 daily running cost) */
#include "company_func.h"     /* _current_company */
#include "command_func.h"     /* CommandCost */
#include "date_func.h"        /* _cur_month */
#include "currency.h"         /* CurrencySpec _currency_specs (R1-79 finance window) */
#include "settings_type.h"    /* GameSettings::difficulty.initial_interest */
#include "cargotype.h"        /* CargoSpec, SetupCargoForClimate (R1-80 real cargo) */
#include "cargo_type.h"       /* CT_PASSENGERS, NUM_CARGO, CargoID */
#include "landscape_type.h"   /* LT_TEMPERATE */
#include "gfx_type.h"         /* SpriteID */
#include "window_func.h"      /* SetWindowDirty (R1-86 graph history feeder) */
#include "window_type.h"      /* WC_OPERATING_PROFIT / WC_INCOME_GRAPH */
#include <algorithm>          /* std::copy_backward (R1-86 graph history feeder) */

#include "safeguards.h"

extern GameSettings   _settings_game;
extern ClientSettings _settings_client;   /* gui.graph_line_thickness (R1-86 graph) */

/* R1-80: newgrf_cargo.cpp isn't compiled; cargotype.cpp's CargoSpec::GetCargoIcon references
 * GetCustomCargoSprite for GRF-overridden cargo icons. We draw no cargo icons, so a no-op (0 =
 * "use the default sprite") is safe. */
SpriteID GetCustomCargoSprite(const CargoSpec *) { return 0; }

/* R1-80: real transported-goods income, copied from economy.cpp:977-1023 (that TU is the heavy
 * station/linkgraph/subsidy cascade we don't compile). The NewGRF CBM_CARGO_PROFIT_CALC branch
 * is DROPPED — our cargo never sets that callback, and keeping it would drag GetCargoCallback
 * (newgrf_cargo.cpp). BigMulS is the file-local helper from economy.cpp:76. */
static inline int32 BigMulS(const int32 a, const int32 b, const uint8 shift)
{
	return (int32)((int64)a * (int64)b >> shift);
}

Money GetTransportedGoodsIncome(uint num_pieces, uint dist, byte transit_days, CargoID cargo_type)
{
	const CargoSpec *cs = CargoSpec::Get(cargo_type);
	if (!cs->IsValid()) return 0;

	static const int MIN_TIME_FACTOR = 31;
	static const int MAX_TIME_FACTOR = 255;

	const int days1 = cs->transit_days[0];
	const int days2 = cs->transit_days[1];
	const int days_over_days1 = std::max(   transit_days - days1, 0);
	const int days_over_days2 = std::max(days_over_days1 - days2, 0);

	const int time_factor = std::max(MAX_TIME_FACTOR - days_over_days1 - days_over_days2, MIN_TIME_FACTOR);

	return BigMulS(dist * time_factor * num_pieces, cs->current_payment, 21);
}

/* Real Economy/Prices — replace the fake `char _economy[4096]`/`char _price[2048]` deadpools
 * (guarded out of m1_deadpools.c under R1_MERGE). Plain PODs; economy.cpp (which also defines
 * them) is NOT compiled, so no ODR clash. */
Economy _economy;
Prices  _price;

/* R1 rung-b: stand up the cargo-payment pool (brand-new symbol; economy.cpp / cargopacket.cpp are
 * not compiled, so no ODR clash). CleanPool odr-uses ~CargoPayment, so provide an EMPTY stub: the
 * simplified deliver path (r1_deliver_cargo, below) never ALLOCATES a CargoPayment — the pool is
 * always empty and there is nothing to finalize. This keeps the real ~CargoPayment (which drags the
 * Vehicle / cost-animation cascade) out of the fragile XCOFF link. */
CargoPaymentPool _cargo_payment_pool("CargoPayment");
INSTANTIATE_POOL_METHODS(CargoPayment)
CargoPayment::~CargoPayment() {}

/* Real one lives in company_cmd.cpp (uncompiled). The finance/company windows aren't open in
 * R1, and the R1InfoWindow repaints itself a few times/sec, so a no-op is fine. */
void InvalidateCompanyWindows(const Company *) {}

/* Verbatim from company_cmd.cpp:218-241 (that TU drags network_base.h). Debits the company +
 * the expense buckets. */
static void SubtractMoneyFromAnyCompany(Company *c, const CommandCost &cost)
{
	if (cost.GetCost() == 0) return;

	c->money -= cost.GetCost();
	c->yearly_expenses[0][cost.GetExpensesType()] += cost.GetCost();

	if (HasBit(1 << EXPENSES_TRAIN_REVENUE    |
	           1 << EXPENSES_ROADVEH_REVENUE  |
	           1 << EXPENSES_AIRCRAFT_REVENUE |
	           1 << EXPENSES_SHIP_REVENUE, cost.GetExpensesType())) {
		c->cur_economy.income -= cost.GetCost();
	} else if (HasBit(1 << EXPENSES_TRAIN_RUN    |
	                  1 << EXPENSES_ROADVEH_RUN  |
	                  1 << EXPENSES_AIRCRAFT_RUN |
	                  1 << EXPENSES_SHIP_RUN     |
	                  1 << EXPENSES_PROPERTY     |
	                  1 << EXPENSES_LOAN_INTEREST, cost.GetExpensesType())) {
		c->cur_economy.expenses -= cost.GetCost();
	}

	InvalidateCompanyWindows(c);
}

/* Exported (economy_func.h). A negative cost credits the company — that's how income arrives. */
void SubtractMoneyFromCompany(const CommandCost &cost)
{
	Company *c = Company::GetIfValid(_current_company);
	if (c != nullptr) SubtractMoneyFromAnyCompany(c, cost);
}

/* R1-87: real DAILY expenses (replaces the no-op EnginesDailyLoop stub; date.cpp's OnNewDay
 * already calls it, same ~74-tick cadence as the industry daily production). The interest/upkeep
 * used to live in CompaniesMonthlyLoop, but the monthly loop fires too rarely (~2220 ticks) to be
 * visible in a short session — so money only ever climbed from bus fares and the finance window's
 * expense columns stayed BLANK. Charging per game-day makes expenses visibly bite: 1/360 of the
 * annual loan interest + a small daily property upkeep per company, plus a per-bus running cost.
 * The finance window's Operating Expenses (Loan Interest / Infrastructure / Road Vehicles) now
 * populate, and the graphs' cur_economy.expenses accumulates. Income still exceeds it (profitable). */
void EnginesDailyLoop()
{
	CompanyID save = _current_company;
	for (const Company *c : Company::Iterate()) {
		_current_company = c->index;
		Money annual_interest = c->current_loan * _economy.interest_rate / 100;
		if (c->money < 0) annual_interest += -c->money * _economy.interest_rate / 100;
		SubtractMoneyFromCompany(CommandCost(EXPENSES_LOAN_INTEREST, annual_interest / 360 + 1));
		SubtractMoneyFromCompany(CommandCost(EXPENSES_PROPERTY, 2));   /* small daily property upkeep */
	}
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type != VEH_ROAD) continue;
		_current_company = v->owner;
		SubtractMoneyFromCompany(CommandCost(EXPENSES_ROADVEH_RUN, 3));  /* per-bus daily running cost */
	}
	_current_company = save;
}

/* Replaces the empty CompaniesMonthlyLoop stub (removed from m1_shims.cpp). R1-87: interest/upkeep
 * moved to the DAILY EnginesDailyLoop above (visible in short sessions); this loop now only does the
 * quarterly history roll for the graphs. Wired to fire monthly by the live OnNewMonth. */
void CompaniesMonthlyLoop()
{
	/* R1-86: quarterly history roll so the operating-profit / income graphs have data. Mirrors
	 * economy.cpp's CompaniesGenStatistics (economy.cpp:692-701): every 3rd month, snapshot the
	 * quarter's cur_economy into old_economy[0] and reset. UpdateCompanyRatingAndValue is dropped
	 * (drags station/score machinery; the operating-profit/income graphs read only income+expenses).
	 * Without this, num_valid_stat_ent stays 0 and the graphs draw an empty grid. */
	if ((_cur_month % 3) == 0) {
		for (Company *c : Company::Iterate()) {
			std::copy_backward(c->old_economy, c->old_economy + MAX_HISTORY_QUARTERS - 1,
			                   c->old_economy + MAX_HISTORY_QUARTERS);
			c->old_economy[0] = c->cur_economy;
			c->cur_economy = {};
			if (c->num_valid_stat_ent != MAX_HISTORY_QUARTERS) c->num_valid_stat_ent++;
		}
		SetWindowDirty(WC_OPERATING_PROFIT, 0);
		SetWindowDirty(WC_INCOME_GRAPH, 0);
	}
}

/* R1 rung-b: the simplified DeliverGoods. Computes the fare for `pax` passengers carried `dist`
 * tiles via the real GetTransportedGoodsIncome (above), then credits COMPANY_FIRST through the real
 * SubtractMoneyFromCompany (negative EXPENSES_ROADVEH_REVENUE == income). Drops the whole
 * subsidy / link-graph / industry-acceptance / station-rating cascade of economy.cpp's DeliverGoods.
 * Returns the income so the caller can log it. transit_days mirrors r1_scene's old inline fare
 * (dist/4+1) so the mild time-factor penalty on long routes is unchanged. This replaces the R1-80
 * inline GetTransportedGoodsIncome + SubtractMoneyFromCompany block in r1_bus_arrive. */
extern "C" long r1_deliver_cargo(unsigned pax, unsigned dist)
{
	if (pax == 0) return 0;
	byte transit_days = (byte)std::min<uint>(dist / 4 + 1, 255);
	Money income = GetTransportedGoodsIncome((uint)pax, (uint)dist, transit_days, CT_PASSENGERS);
	if (income < 1) income = 1;
	CompanyID save = _current_company;
	_current_company = COMPANY_FIRST;
	SubtractMoneyFromCompany(CommandCost(EXPENSES_ROADVEH_REVENUE, -income));
	_current_company = save;
	return (long)income;
}

/* Seed the economy constants the mini loop needs (avoids the heavy StartupEconomy/
 * RecomputePrices path). Called once after r1_make_company(). */
extern "C" void r1_economy_startup(void)
{
	_economy.interest_rate    = 2;
	_economy.max_loan         = 300000;
	_price[PR_STATION_VALUE]  = 200;   /* -> a flat ~50/month property upkeep */

	/* R1-79: seed the currency table the finance window's {CURRENCY_LONG} widgets format
	 * against. GetGameSettings().locale.currency is 0, so slot 0 is what _currency resolves to.
	 * rate=1 (money prints 1:1), "£" prefix, empty separator (FormatGenericCurrency falls back
	 * to the loaded langpack separator). A typed CurrencySpec has valid empty std::strings, so
	 * .c_str() is safe — the zeroed char[] deadpool would have bus-errored here. */
	_currency_specs[0] = CurrencySpec(1, "", CF_NOEURO, "\xC2\xA3", "", 0, STR_NULL);
	/* Make the finance window's "Loan Interest" widget show 2% (it reads initial_interest). */
	_settings_game.difficulty.initial_interest = 2;

	/* R1-86: the graph plots points/lines of this width; 0 (zeroed _settings_client) = invisible. */
	_settings_client.gui.graph_line_thickness = 3;

	/* R1-80: stand up the REAL cargo table (temperate climate) so GetTransportedGoodsIncome has
	 * genuine per-cargo payment/transit params — replaces the R1-77 magic-number fare. We don't
	 * run economy.cpp's StartupEconomy (which sets current_payment from inflation), so seed it
	 * directly: no inflation => current_payment == initial_payment. */
	SetupCargoForClimate(LT_TEMPERATE);
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		CargoSpec *cs = CargoSpec::Get(i);
		cs->current_payment = cs->initial_payment;
	}
}
