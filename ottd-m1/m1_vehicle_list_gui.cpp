/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/vehicle_gui.cpp (the VehicleListWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1: a TRIMMED VehicleListWindow, extracted M1-style (mirrors m1_finance_gui.cpp /
// m1_town_directory_gui.cpp) so the canonical toolbar can open a real list of the company's
// road vehicles — real unit numbers, real per-vehicle profit icon + profit text — instead of a
// no-op. The full vehicle_gui.cpp VehicleListWindow drags THREE cascades we cannot link here:
//   * the ENGINE cascade (DrawVehicleImage -> GetVehiclePalette/GetEngineColourMap/
//     GetDisplayImageWidth, GetVehicleListHeight -> GetVehicleHeight is fine but the sorter
//     arrays reference engine_type/reliability/cached_max_speed helpers),
//   * the ORDER cascade (GenerateVehicleSortList, DrawSmallOrderList, GB_SHARED_ORDERS),
//   * the COMMAND cascade (CMD_MASS_START_STOP, CMD_SEND_VEHICLE_TO_DEPOT, ShowBuildVehicleWindow,
//     ShowCompanyGroup, ShowVehicleViewWindow).
// So this is a deliberately SMALLER window: closebox + caption + a single-column matrix of
// GB_NONE (one-vehicle-per-row) groups + a scrollbar. The heavy pieces are cut, not stubbed:
//   - widget tree: ONLY closebox + caption + matrix + vscrollbar (no shade/defsize/sticky/resize,
//     no sort/group/filter dropdowns, no manage/available/start-stop buttons, no SPR_FLAG btns).
//   - BuildVehicleList(): a LOCAL VL_STANDARD loop over Vehicle::Iterate() (NOT
//     GenerateVehicleSortList, which drags order iteration).
//   - DrawVehicleListItems(): the profit text + profit icon + unit-number columns are kept, but
//     the DrawVehicleImage() call is replaced by a direct DrawSprite() of the bus sprite
//     (0xCD4 + DIR_W) — DrawVehicleImage would drag the engine colour-map cascade.
//   - sorter table: a single VehicleNumberSorter entry (the full sorter arrays reference engine
//     helpers that are undefined at link).
//   - OnClick(WID_VL_LIST): centres the main viewport on the vehicle's tile via
//     ScrollMainWindowToTile (NOT ShowVehicleViewWindow — its window TU isn't compiled).
//   - ShowVehicleListWindow(CompanyID, VehicleType): builds the window UNCONDITIONALLY (never
//     touches ShowCompanyGroup / the advanced-list branch).
// The base BaseVehicleListWindow ctor + BuildVehicleList + DrawVehicleListItems are (re)defined
// here with trimmed bodies; their real homes (vehicle_gui.cpp) are NOT compiled, so no ODR clash.

#include "stdafx.h"
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "zoom_func.h"
#include "company_func.h"
#include "company_base.h"
#include "cargotype.h"
#include "viewport_func.h"               /* ScrollMainWindowToTile */
#include "vehicle_func.h"                /* VEHICLE_PROFIT_MIN_AGE / _THRESHOLD */
#include "vehicle_gui.h"
#include "vehicle_gui_base.h"
#include "vehicle_base.h"
#include "core/geometry_func.hpp"        /* maxdim */
#include "widgets/vehicle_widget.h"
#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

// ----------------------------------------------------------------------------
// Free helpers extracted from vehicle_gui.cpp (verbatim except where noted).
// ----------------------------------------------------------------------------

/** vehicle_gui.cpp:153 — number of digits of space required for the given number. */
static uint CountDigitsForAllocatingSpace(uint number)
{
	if (number >= 10000) return 5;
	if (number >= 1000) return 4;
	if (number >= 100) return 3;
	return 2;
}

/** vehicle_gui.cpp:410 — draw the vehicle profit button in the vehicle list window. */
static void DrawVehicleProfitButton(Date age, Money display_profit_last_year, uint num_vehicles, int x, int y)
{
	SpriteID spr;

	/* draw profit-based coloured icons */
	if (age <= VEHICLE_PROFIT_MIN_AGE) {
		spr = SPR_PROFIT_NA;
	} else if (display_profit_last_year < 0) {
		spr = SPR_PROFIT_NEGATIVE;
	} else if (display_profit_last_year < VEHICLE_PROFIT_THRESHOLD * num_vehicles) {
		spr = SPR_PROFIT_SOME;
	} else {
		spr = SPR_PROFIT_LOT;
	}
	DrawSprite(spr, PAL_NONE, x, y);
}

/** vehicle_gui.cpp:1625 — height of a vehicle in the vehicle list GUIs. */
uint GetVehicleListHeight(VehicleType type, uint divisor)
{
	/* Name + vehicle + profit */
	uint base = ScaleGUITrad(GetVehicleHeight(type)) + 2 * FONT_HEIGHT_SMALL + WidgetDimensions::scaled.matrix.Vertical();
	/* Drawing of the 4 small orders + profit*/
	if (type >= VEH_SHIP) base = std::max(base, 5U * FONT_HEIGHT_SMALL + WidgetDimensions::scaled.matrix.Vertical());

	if (divisor == 1) return base;

	/* Make sure the height is dividable by divisor */
	uint rem = base % divisor;
	return base + (rem == 0 ? 0 : divisor - rem);
}

/** vehicle_gui.cpp:1326 — sort vehicles by their number (the ONLY sorter we ship). */
static bool VehicleNumberSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	return a->unitnumber < b->unitnumber;
}

// ----------------------------------------------------------------------------
// BaseVehicleListWindow (trimmed): the ctor + BuildVehicleList + DrawVehicleListItems.
// Real homes are vehicle_gui.cpp, which is NOT compiled in this build → no ODR clash.
// ----------------------------------------------------------------------------

// Trimmed ctor: build a fixed VL_STANDARD/VEH_ROAD identifier straight from the window number
// (which we set to the company id in ShowVehicleListWindow), skipping VehicleListIdentifier::UnPack
// and the _grouping[][] global / UpdateSortingFromGrouping (they drag the sorter machinery).
BaseVehicleListWindow::BaseVehicleListWindow(WindowDesc *desc, WindowNumber wno) : Window(desc)
{
	this->vli                   = VehicleListIdentifier(VL_STANDARD, VEH_ROAD, (CompanyID)wno, (uint32)wno);
	this->grouping              = GB_NONE;
	this->vehicle_sel           = INVALID_VEHICLE;
	this->unitnumber_digits     = 2;
	this->cargo_filter_criteria = 0;
	this->order_arrow_width     = 0;
	this->sorting               = nullptr;
}

// OnInit is BaseVehicleListWindow's key function (its only declared virtual), so defining it
// out-of-line HERE anchors _ZTV21BaseVehicleListWindow / _ZTI21BaseVehicleListWindow in this TU
// (their real home vehicle_gui.cpp is not compiled). Trimmed body: the real one calls
// SetCargoFilterArray() which drags _sorted_cargo_specs; we only need order_arrow_width. The
// derived VehicleListWindow inherits this (it does not override OnInit).
void BaseVehicleListWindow::OnInit()
{
	this->order_arrow_width = 0;
}

// Trimmed BuildVehicleList: ONLY the VL_STANDARD loop, GB_NONE grouping (one vehicle per group).
// Does NOT call GenerateVehicleSortList (order iteration cascade); we sort with the single local
// VehicleNumberSorter instead.
void BaseVehicleListWindow::BuildVehicleList()
{
	if (!this->vehgroups.NeedRebuild()) return;

	this->vehicles.clear();
	this->vehgroups.clear();

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == this->vli.vtype && v->owner == this->vli.company && v->IsPrimaryVehicle()) {
			this->vehicles.push_back(v);
		}
	}

	std::sort(this->vehicles.begin(), this->vehicles.end(), &VehicleNumberSorter);

	uint max_unitnumber = 0;
	for (auto it = this->vehicles.begin(); it != this->vehicles.end(); ++it) {
		this->vehgroups.emplace_back(it, it + 1);
		max_unitnumber = std::max<uint>(max_unitnumber, (*it)->unitnumber);
	}
	this->unitnumber_digits = CountDigitsForAllocatingSpace(max_unitnumber);

	this->vehgroups.RebuildDone();
	this->vscroll->SetCount(static_cast<int>(this->vehgroups.size()));
}

// Trimmed DrawVehicleListItems: keeps the profit text, profit icon and unit-number columns of
// vehicle_gui.cpp:1645, but for GB_NONE only, and REPLACES the DrawVehicleImage() call with a
// direct DrawSprite() of the bus sprite (0xCD4 + DIR_W) — DrawVehicleImage drags GetVehiclePalette
// / GetEngineColourMap / GetDisplayImageWidth (the engine cascade). The cargo/name/group text and
// the small-order list are dropped.
void BaseVehicleListWindow::DrawVehicleListItems(VehicleID, int line_height, const Rect &r) const
{
	Rect ir = r.WithHeight(line_height).Shrink(WidgetDimensions::scaled.matrix, RectPadding::zero);
	bool rtl = _current_text_dir == TD_RTL;

	Dimension profit = GetSpriteSize(SPR_PROFIT_LOT);
	int text_offset = std::max<int>(profit.width, GetDigitWidth() * this->unitnumber_digits) + WidgetDimensions::scaled.hsep_normal;
	Rect tr = ir.Indent(text_offset, rtl);

	int image_left  = rtl ? tr.left : tr.left;
	int vehicle_button_x = rtl ? ir.right - profit.width : ir.left;

	uint max = static_cast<uint>(std::min<size_t>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->vehgroups.size()));
	for (uint i = this->vscroll->GetPosition(); i < max; ++i) {
		const GUIVehicleGroup &vehgroup = this->vehgroups[i];

		SetDParam(0, vehgroup.GetDisplayProfitThisYear());
		SetDParam(1, vehgroup.GetDisplayProfitLastYear());
		DrawString(tr.left, tr.right, ir.bottom - FONT_HEIGHT_SMALL - WidgetDimensions::scaled.framerect.bottom, STR_VEHICLE_LIST_PROFIT_THIS_YEAR_LAST_YEAR);

		DrawVehicleProfitButton(vehgroup.GetOldestVehicleAge(), vehgroup.GetDisplayProfitLastYear(), vehgroup.NumVehicles(), vehicle_button_x, ir.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal);

		const Vehicle *v = vehgroup.GetSingleVehicle();

		/* R1: draw the bus sprite directly — DrawVehicleImage would drag the engine cascade. */
		DrawSprite((SpriteID)(0xCD4 + DIR_W), PAL_NONE, image_left, ir.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal);

		StringID str;
		if (v->IsChainInDepot()) {
			str = STR_BLUE_COMMA;
		} else {
			str = (v->age > v->max_age - DAYS_IN_LEAP_YEAR) ? STR_RED_COMMA : STR_BLACK_COMMA;
		}

		SetDParam(0, v->unitnumber);
		DrawString(ir.left, ir.right, ir.top + WidgetDimensions::scaled.framerect.top, str);

		ir = ir.Translate(0, line_height);
	}
}

// ----------------------------------------------------------------------------
// Trimmed widget tree: closebox + caption + matrix + vscrollbar. Everything else is cut.
// ----------------------------------------------------------------------------
static const NWidgetPart _nested_vehicle_list[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VL_CAPTION),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_VL_LIST), SetMinimalSize(248, 0), SetFill(1, 0), SetResize(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_VL_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VL_SCROLLBAR),
	EndContainer(),
};

/** Trimmed VehicleListWindow: list a company's road vehicles. */
struct VehicleListWindow : public BaseVehicleListWindow {
	VehicleListWindow(WindowDesc *desc, WindowNumber window_number) : BaseVehicleListWindow(desc, window_number)
	{
		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_VL_SCROLLBAR);

		this->vehgroups.ForceRebuild();
		this->BuildVehicleList();

		this->GetWidget<NWidgetCore>(WID_VL_LIST)->tool_tip   = STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP + this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_VL_CAPTION)->widget_data = STR_VEHICLE_LIST_TRAIN_CAPTION + this->vli.vtype;

		this->FinishInitNested(window_number);
		if (this->vli.company != OWNER_NONE) this->owner = this->vli.company;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &, Dimension *, Dimension *resize) override
	{
		switch (widget) {
			case WID_VL_LIST:
				resize->height = GetVehicleListHeight(this->vli.vtype, 1);
				size->height   = 6 * resize->height;
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_VL_CAPTION:
				SetDParam(0, STR_COMPANY_NAME);
				SetDParam(1, this->vli.index);
				SetDParam(3, this->vehicles.size());
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_VL_LIST:
				this->DrawVehicleListItems(INVALID_VEHICLE, this->resize.step_height, r);
				break;
		}
	}

	void OnPaint() override
	{
		this->BuildVehicleList();
		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int) override
	{
		switch (widget) {
			case WID_VL_LIST: { // Click a row → centre the main viewport on that vehicle's tile.
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_VL_LIST);
				if (id_v >= this->vehgroups.size()) return; // click out of list bound

				const GUIVehicleGroup &vehgroup = this->vehgroups[id_v];
				const Vehicle *v = vehgroup.GetSingleVehicle();
				ScrollMainWindowToTile(v->tile);
				break;
			}
		}
	}

	void OnHundredthTick() override
	{
		this->vehgroups.ForceRebuild();
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_VL_LIST);
	}
};

static WindowDesc _vehicle_list_other_desc(
	WDP_AUTO, "list_vehicles", 260, 246,
	WC_INVALID, WC_NONE,
	0,
	_nested_vehicle_list, lengthof(_nested_vehicle_list)
);

/**
 * Open the (road) vehicle list of a company. Built UNCONDITIONALLY — the advanced-list branch
 * (ShowCompanyGroup) is deliberately never referenced.
 */
void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type)
{
	if (!Company::IsValidID(company) && company != OWNER_NONE) return;

	_vehicle_list_other_desc.cls = WC_ROADVEH_LIST;
	AllocateWindowDescFront<VehicleListWindow>(&_vehicle_list_other_desc, company);
}
