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
#include "company_base.h"     /* Company::Iterate / GetIfValid */
#include "company_func.h"     /* _current_company */
#include "command_func.h"     /* CommandCost */
#include "date_func.h"        /* _cur_month */
#include "currency.h"         /* CurrencySpec _currency_specs (R1-79 finance window) */
#include "settings_type.h"    /* GameSettings::difficulty.initial_interest */
#include "cargotype.h"        /* CargoSpec, SetupCargoForClimate (R1-80 real cargo) */
#include "cargo_type.h"       /* CT_PASSENGERS, NUM_CARGO, CargoID */
#include "landscape_type.h"   /* LT_TEMPERATE */
#include "gfx_type.h"         /* SpriteID */

#include "safeguards.h"

extern GameSettings _settings_game;

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

/* Replaces the empty CompaniesMonthlyLoop stub (removed from m1_shims.cpp). Minimal money
 * movement — loan interest + a flat property upkeep per company, from economy.cpp's
 * CompaniesPayInterest (economy.cpp:825). Wired to fire monthly by the live OnNewMonth. */
void CompaniesMonthlyLoop()
{
	for (const Company *c : Company::Iterate()) {
		CompanyID save = _current_company;
		_current_company = c->index;
		Money yearly_fee = c->current_loan * _economy.interest_rate / 100;
		if (c->money < 0) yearly_fee += -c->money * _economy.interest_rate / 100;
		Money up_to_prev = yearly_fee * _cur_month / 12;
		Money up_to_this = yearly_fee * (_cur_month + 1) / 12;
		SubtractMoneyFromCompany(CommandCost(EXPENSES_LOAN_INTEREST, up_to_this - up_to_prev));
		SubtractMoneyFromCompany(CommandCost(EXPENSES_OTHER, _price[PR_STATION_VALUE] >> 2));
		_current_company = save;
	}
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
