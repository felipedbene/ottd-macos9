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

#include "safeguards.h"

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
}
