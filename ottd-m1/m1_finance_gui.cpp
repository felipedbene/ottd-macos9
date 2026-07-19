/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/company_gui.cpp (the CompanyFinancesWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1-79: the REAL CompanyFinancesWindow, extracted M1-style so the canonical toolbar's
// FINANCES button opens the genuine finances window (real bank balance / loan / max-loan /
// current-year expense columns) instead of the no-op stub. This is the "own the real object,
// stub the cascade" pattern again: the whole slice below is verbatim OpenTTD company_gui.cpp
// (helpers + widget tree + window + ShowCompanyFinances), and the only things this TU adds are:
//   1. a real `CurrencySpec _currency_specs[CURRENCY_END]` (the char[] deadpool is guarded out
//      in m1_deadpools.c under R1_MERGE) — WITHOUT this the {CURRENCY_LONG} formatter bus-errors
//      on a zeroed std::string prefix (strecpy has no null guard). r1_economy_startup seeds slot 0.
//   2. a local no-op ShowCompanyInfrastructure (its window TU isn't compiled).
//   3. no-op CmdIncreaseLoan/CmdDecreaseLoan procs (economy_cmd/misc_cmd not compiled) so the
//      borrow/repay buttons are harmless no-ops via the already-stubbed command Post backend.
// company_gui.cpp's heavy includes (network, engine, company_cmd, group_cmd, object_cmd) are NOT
// needed by the finance slice, so this stays light and drags no new subsystem.

#include "stdafx.h"
#include "currency.h"
#include "window_gui.h"
#include "company_func.h"
#include "command_func.h"
#include "strings_func.h"
#include "date_func.h"
#include "company_base.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "window_func.h"
#include "misc_cmd.h"
#include "core/geometry_func.hpp"
#include "company_gui.h"
#include "table/sprites.h"    /* SPR_LARGE_SMALL_WINDOW */

#include "widgets/company_widget.h"

#include "safeguards.h"

// The real currency table (replaces the zeroed char[] deadpool in the R1_MERGE build). A typed
// array default-constructs its std::string members to valid empty strings, so FormatGenericCurrency's
// prefix.c_str() is safe. r1_economy_startup (m1_economy.cpp) seeds slot 0 (rate=1, "£" prefix).
CurrencySpec _currency_specs[CURRENCY_END];

// company_gui.cpp's infrastructure window isn't compiled; the FINANCES "infrastructure" button
// is a harmless no-op here.
static void ShowCompanyInfrastructure(CompanyID company);

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/company_gui.cpp:55-554 (finance window).
// ----------------------------------------------------------------------------

/** List of revenues. */
static ExpensesType _expenses_list_revenue[] = {
	EXPENSES_TRAIN_REVENUE,
	EXPENSES_ROADVEH_REVENUE,
	EXPENSES_AIRCRAFT_REVENUE,
	EXPENSES_SHIP_REVENUE,
};

/** List of operating expenses. */
static ExpensesType _expenses_list_operating_costs[] = {
	EXPENSES_TRAIN_RUN,
	EXPENSES_ROADVEH_RUN,
	EXPENSES_AIRCRAFT_RUN,
	EXPENSES_SHIP_RUN,
	EXPENSES_PROPERTY,
	EXPENSES_LOAN_INTEREST,
};

/** List of capital expenses. */
static ExpensesType _expenses_list_capital_costs[] = {
	EXPENSES_CONSTRUCTION,
	EXPENSES_NEW_VEHICLES,
	EXPENSES_OTHER,
};

/** Expense list container. */
struct ExpensesList {
	const ExpensesType *et;   ///< Expenses items.
	const uint length;        ///< Number of items in list.

	ExpensesList(ExpensesType *et, int length) : et(et), length(length)
	{
	}

	uint GetHeight() const
	{
		/* Add up the height of all the lines.  */
		return this->length * FONT_HEIGHT_NORMAL;
	}

	/** Compute width of the expenses categories in pixels. */
	uint GetListWidth() const
	{
		uint width = 0;
		for (uint i = 0; i < this->length; i++) {
			ExpensesType et = this->et[i];
			width = std::max(width, GetStringBoundingBox(STR_FINANCES_SECTION_CONSTRUCTION + et).width);
		}
		return width;
	}
};

/** Types of expense lists */
static const ExpensesList _expenses_list_types[] = {
	ExpensesList(_expenses_list_revenue, lengthof(_expenses_list_revenue)),
	ExpensesList(_expenses_list_operating_costs, lengthof(_expenses_list_operating_costs)),
	ExpensesList(_expenses_list_capital_costs, lengthof(_expenses_list_capital_costs)),
};

/**
 * Get the total height of the "categories" column.
 * @return The total height in pixels.
 */
static uint GetTotalCategoriesHeight()
{
	/* There's an empty line and blockspace on the year row */
	uint total_height = FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Title + expense list + total line + total + blockspace after category */
		total_height += FONT_HEIGHT_NORMAL + _expenses_list_types[i].GetHeight() + WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;
	}

	/* Total income */
	total_height += WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	return total_height;
}

/**
 * Get the required width of the "categories" column, equal to the widest element.
 * @return The required width in pixels.
 */
static uint GetMaxCategoriesWidth()
{
	uint max_width = 0;

	/* Loop through categories to check max widths. */
	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Title of category */
		max_width = std::max(max_width, GetStringBoundingBox(STR_FINANCES_REVENUE_TITLE + i).width);
		/* Entries in category */
		max_width = std::max(max_width, _expenses_list_types[i].GetListWidth() + WidgetDimensions::scaled.hsep_indent);
	}

	return max_width;
}

/**
 * Draw a category of expenses (revenue, operating expenses, capital expenses).
 */
static void DrawCategory(const Rect &r, int start_y, ExpensesList list)
{
	Rect tr = r.Indent(WidgetDimensions::scaled.hsep_indent, _current_text_dir == TD_RTL);

	tr.top = start_y;
	ExpensesType et;

	for (uint i = 0; i < list.length; i++) {
		et = list.et[i];
		DrawString(tr, STR_FINANCES_SECTION_CONSTRUCTION + et);
		tr.top += FONT_HEIGHT_NORMAL;
	}
}

/**
 * Draw the expenses categories.
 * @param r Available space for drawing.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawCategories(const Rect &r)
{
	/* Start with an empty space in the year row, plus the blockspace under the year. */
	int y = r.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Draw category title and advance y */
		DrawString(r.left, r.right, y, (STR_FINANCES_REVENUE_TITLE + i), TC_FROMSTRING, SA_LEFT);
		y += FONT_HEIGHT_NORMAL;

		/* Draw category items and advance y */
		DrawCategory(r, y, _expenses_list_types[i]);
		y += _expenses_list_types[i].GetHeight();

		/* Advance y by the height of the horizontal line between amounts and subtotal */
		y += WidgetDimensions::scaled.vsep_normal;

		/* Draw category total and advance y */
		DrawString(r.left, r.right, y, STR_FINANCES_TOTAL_CAPTION, TC_FROMSTRING, SA_RIGHT);
		y += FONT_HEIGHT_NORMAL;

		/* Advance y by a blockspace after this category block */
		y += WidgetDimensions::scaled.vsep_wide;
	}

	/* Draw total profit/loss */
	y += WidgetDimensions::scaled.vsep_normal;
	DrawString(r.left, r.right, y, STR_FINANCES_PROFIT, TC_FROMSTRING, SA_LEFT);
}

/**
 * Draw an amount of money.
 */
static void DrawPrice(Money amount, int left, int right, int top, TextColour colour)
{
	StringID str = STR_FINANCES_NEGATIVE_INCOME;
	if (amount == 0) {
		str = STR_FINANCES_ZERO_INCOME;
	} else if (amount < 0) {
		amount = -amount;
		str = STR_FINANCES_POSITIVE_INCOME;
	}
	SetDParam(0, amount);
	DrawString(left, right, top, str, colour, SA_RIGHT);
}

/**
 * Draw a category of expenses/revenues in the year column.
 * @return The income sum of the category.
 */
static Money DrawYearCategory (const Rect &r, int start_y, ExpensesList list, const Money(*tbl)[EXPENSES_END])
{
	int y = start_y;
	ExpensesType et;
	Money sum = 0;

	for (uint i = 0; i < list.length; i++) {
		et = list.et[i];
		Money cost = (*tbl)[et];
		sum += cost;
		if (cost != 0) DrawPrice(cost, r.left, r.right, y, TC_BLACK);
		y += FONT_HEIGHT_NORMAL;
	}

	/* Draw the total at the bottom of the category. */
	GfxFillRect(r.left, y, r.right, y, PC_BLACK);
	y += WidgetDimensions::scaled.vsep_normal;
	if (sum != 0) DrawPrice(sum, r.left, r.right, y, TC_WHITE);

	/* Return the sum for the yearly total. */
	return sum;
}


/**
 * Draw a column with prices.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawYearColumn(const Rect &r, int year, const Money (*tbl)[EXPENSES_END])
{
	int y = r.top;
	Money sum;

	/* Year header */
	SetDParam(0, year);
	DrawString(r.left, r.right, y, STR_FINANCES_YEAR, TC_FROMSTRING, SA_RIGHT, true);
	y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	/* Categories */
	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		y += FONT_HEIGHT_NORMAL;
		sum += DrawYearCategory(r, y, _expenses_list_types[i], tbl);
		/* Expense list + expense category title + expense category total + blockspace after category */
		y += _expenses_list_types[i].GetHeight() + WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;
	}

	/* Total income. */
	GfxFillRect(r.left, y, r.right, y, PC_BLACK);
	y += WidgetDimensions::scaled.vsep_normal;
	DrawPrice(sum, r.left, r.right, y, TC_WHITE);
}

static const NWidgetPart _nested_company_finances_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CF_CAPTION), SetDataTip(STR_FINANCES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_CF_TOGGLE_SIZE), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_TOOLTIP_TOGGLE_LARGE_SMALL_WINDOW),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_PANEL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(NWID_HORIZONTAL), SetPadding(WidgetDimensions::unscaled.framerect), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_CATEGORY), SetMinimalSize(120, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE1), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE2), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE3), SetMinimalSize(86, 0), SetFill(0, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(NWID_VERTICAL), // Vertical column with 'bank balance', 'loan'
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_OWN_FUNDS_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_LOAN_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_BANK_BALANCE_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_SPACER), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(0, 0), SetMinimalSize(30, 0),
			NWidget(NWID_VERTICAL), // Vertical column with bank balance amount, loan amount, and total.
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_OWN_VALUE), SetDataTip(STR_FINANCES_TOTAL_CURRENCY, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_LOAN_VALUE), SetDataTip(STR_FINANCES_TOTAL_CURRENCY, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_BALANCE_LINE), SetMinimalSize(0, 2), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_BALANCE_VALUE), SetDataTip(STR_FINANCES_BANK_BALANCE, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_MAXLOAN),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(0, 1), SetMinimalSize(25, 0),
					NWidget(NWID_VERTICAL), // Max loan information
						NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_INTEREST_RATE), SetDataTip(STR_FINANCES_INTEREST_RATE, STR_NULL),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_MAXLOAN_VALUE), SetDataTip(STR_FINANCES_MAX_LOAN, STR_NULL),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INCREASE_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_BORROW_BUTTON, STR_FINANCES_BORROW_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_REPAY_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_REPAY_BUTTON, STR_FINANCES_REPAY_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INFRASTRUCTURE), SetFill(1, 0), SetDataTip(STR_FINANCES_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

/** Window class displaying the company finances. */
struct CompanyFinancesWindow : Window {
	static Money max_money; ///< The maximum amount of money a company has had this 'run'
	bool small;             ///< Window is toggled to 'small'.

	CompanyFinancesWindow(WindowDesc *desc, CompanyID company) : Window(desc)
	{
		this->small = false;
		this->CreateNestedTree();
		this->SetupWidgets();
		this->FinishInitNested(company);

		this->owner = (Owner)this->window_number;
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CF_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_CF_BALANCE_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money);
				break;
			}

			case WID_CF_LOAN_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->current_loan);
				break;
			}

			case WID_CF_OWN_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money - c->current_loan);
				break;
			}

			case WID_CF_INTEREST_RATE:
				SetDParam(0, _settings_game.difficulty.initial_interest);
				break;

			case WID_CF_MAXLOAN_VALUE:
				SetDParam(0, _economy.max_loan);
				break;

			case WID_CF_INCREASE_LOAN:
			case WID_CF_REPAY_LOAN:
				SetDParam(0, LOAN_INTERVAL);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				size->width  = GetMaxCategoriesWidth();
				size->height = GetTotalCategoriesHeight();
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3:
				size->height = GetTotalCategoriesHeight();
				FALLTHROUGH;

			case WID_CF_BALANCE_VALUE:
			case WID_CF_LOAN_VALUE:
			case WID_CF_OWN_VALUE:
				SetDParamMaxValue(0, CompanyFinancesWindow::max_money);
				size->width = std::max(GetStringBoundingBox(STR_FINANCES_NEGATIVE_INCOME).width, GetStringBoundingBox(STR_FINANCES_POSITIVE_INCOME).width) + padding.width;
				break;

			case WID_CF_INTEREST_RATE:
				size->height = FONT_HEIGHT_NORMAL;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				DrawCategories(r);
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				int age = std::min(_cur_year - c->inaugurated_year, 2);
				int wid_offset = widget - WID_CF_EXPS_PRICE1;
				if (wid_offset <= age) {
					DrawYearColumn(r, _cur_year - (age - wid_offset), c->yearly_expenses + (age - wid_offset));
				}
				break;
			}

			case WID_CF_BALANCE_LINE:
				GfxFillRect(r.left, r.top, r.right, r.top, PC_BLACK);
				break;
		}
	}

	void SetupWidgets()
	{
		int plane = this->small ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_PANEL)->SetDisplayedPlane(plane);
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_MAXLOAN)->SetDisplayedPlane(plane);

		CompanyID company = (CompanyID)this->window_number;
		plane = (company != _local_company) ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->SetDisplayedPlane(plane);
	}

	void OnPaint() override
	{
		if (!this->IsShaded()) {
			if (!this->small) {
				/* Check that the expenses panel height matches the height needed for the layout. */
				if (GetTotalCategoriesHeight() != this->GetWidget<NWidgetBase>(WID_CF_EXPS_CATEGORY)->current_y) {
					this->SetupWidgets();
					this->ReInit();
					return;
				}
			}

			/* Check that the loan buttons are shown only when the user owns the company. */
			CompanyID company = (CompanyID)this->window_number;
			int req_plane = (company != _local_company) ? SZSP_NONE : 0;
			if (req_plane != this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->shown_plane) {
				this->SetupWidgets();
				this->ReInit();
				return;
			}

			const Company *c = Company::Get(company);
			this->SetWidgetDisabledState(WID_CF_INCREASE_LOAN, c->current_loan == _economy.max_loan); // Borrow button only shows when there is any more money to loan.
			this->SetWidgetDisabledState(WID_CF_REPAY_LOAN, company != _local_company || c->current_loan == 0); // Repay button only shows when there is any more money to repay.
		}

		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_CF_TOGGLE_SIZE: // toggle size
				this->small = !this->small;
				this->SetupWidgets();
				if (this->IsShaded()) {
					this->SetShaded(false);
				} else {
					this->ReInit();
				}
				break;

			case WID_CF_INCREASE_LOAN: // increase loan
				Command<CMD_INCREASE_LOAN>::Post(STR_ERROR_CAN_T_BORROW_ANY_MORE_MONEY, _ctrl_pressed ? LoanCommand::Max : LoanCommand::Interval, 0);
				break;

			case WID_CF_REPAY_LOAN: // repay loan
				Command<CMD_DECREASE_LOAN>::Post(STR_ERROR_CAN_T_REPAY_LOAN, _ctrl_pressed ? LoanCommand::Max : LoanCommand::Interval, 0);
				break;

			case WID_CF_INFRASTRUCTURE: // show infrastructure details
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;
		}
	}

	void OnHundredthTick() override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		if (c->money > CompanyFinancesWindow::max_money) {
			CompanyFinancesWindow::max_money = std::max(c->money * 2, CompanyFinancesWindow::max_money * 4);
			this->SetupWidgets();
			this->ReInit();
		}
	}
};

/** First conservative estimate of the maximum amount of money */
Money CompanyFinancesWindow::max_money = INT32_MAX;

static WindowDesc _company_finances_desc(
	WDP_AUTO, "company_finances", 0, 0,
	WC_FINANCES, WC_NONE,
	0,
	_nested_company_finances_widgets, lengthof(_nested_company_finances_widgets)
);

/**
 * Open the finances window of a company.
 * @param company Company to show finances of.
 * @pre is company a valid company.
 */
void ShowCompanyFinances(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	if (BringWindowToFrontById(WC_FINANCES, company)) return;

	new CompanyFinancesWindow(&_company_finances_desc, company);
}

// ----------------------------------------------------------------------------
// R1 stubs: the infrastructure window + the two loan command procs (their real
// TUs — company_gui infrastructure window, misc_cmd/economy_cmd — are not compiled).
// The borrow/repay buttons therefore no-op via the already-stubbed command Post backend.
// ----------------------------------------------------------------------------
static void ShowCompanyInfrastructure(CompanyID) {}

CommandCost CmdIncreaseLoan(DoCommandFlag, LoanCommand, Money) { return CommandCost(); }
CommandCost CmdDecreaseLoan(DoCommandFlag, LoanCommand, Money) { return CommandCost(); }
