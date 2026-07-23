/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/subsidy_gui.cpp (the SubsidyListWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1: the REAL SubsidyListWindow, extracted M1-style (mirrors m1_town_directory_gui.cpp) so the
// canonical toolbar's SUBSIDIES button opens the genuine scrollable subsidy list. The slice below
// is verbatim OpenTTD subsidy_gui.cpp:28-248. In R1 the _subsidy_pool is EMPTY (nothing awards
// subsidies), so the window renders its "None" state under both the "Offered" and "Subsidised"
// titles — that is the correct, graceful, no-crash display. Subsidy::Iterate() over an empty pool
// simply yields nothing, so SetupSubsidyDecodeParam (stubbed below) is never actually called.
//
// This TU OWNS the real _subsidy_pool (subsidy.cpp is NOT compiled — its award/monthly/news/cargo
// cascade is the "subsidy seam"), exactly the way m1_station.cpp owns _station_pool. The only added
// symbols are that pool + a thin inert SetupSubsidyDecodeParam stub (its real home subsidy.cpp is
// uncompiled). Clicks are live (HandleClick scrolls the map) but never fire on the empty pool.

#include "stdafx.h"
#include "industry.h"
#include "town.h"
#include "window_gui.h"
#include "strings_func.h"
#include "date_func.h"
#include "viewport_func.h"
#include "gui.h"                          /* ShowExtraViewportWindow */
#include "subsidy_func.h"
#include "subsidy_base.h"
#include "core/geometry_func.hpp"
#include "core/pool_func.hpp"             /* INSTANTIATE_POOL_METHODS */

#include "widgets/subsidy_widget.h"

#include "table/strings.h"

#include "safeguards.h"

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/subsidy_gui.cpp:28-248 (subsidy list window).
// ----------------------------------------------------------------------------

struct SubsidyListWindow : Window {
	Scrollbar *vscroll;

	SubsidyListWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SUL_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->OnInvalidateData(0);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		if (widget != WID_SUL_PANEL) return;

		int y = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_SUL_PANEL, WidgetDimensions::scaled.framerect.top);
		int num = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				y--;
				if (y == 0) {
					this->HandleClick(s);
					return;
				}
				num++;
			}
		}

		if (num == 0) {
			y--; // "None"
			if (y < 0) return;
		}

		y -= 2; // "Services already subsidised:"
		if (y < 0) return;

		for (const Subsidy *s : Subsidy::Iterate()) {
			if (s->IsAwarded()) {
				y--;
				if (y == 0) {
					this->HandleClick(s);
					return;
				}
			}
		}
	}

	void HandleClick(const Subsidy *s)
	{
		/* determine src coordinate for subsidy and try to scroll to it */
		TileIndex xy;
		switch (s->src_type) {
			case ST_INDUSTRY: xy = Industry::Get(s->src)->location.tile; break;
			case ST_TOWN:     xy =     Town::Get(s->src)->xy; break;
			default: NOT_REACHED();
		}

		if (_ctrl_pressed || !ScrollMainWindowToTile(xy)) {
			if (_ctrl_pressed) ShowExtraViewportWindow(xy);

			/* otherwise determine dst coordinate for subsidy and scroll to it */
			switch (s->dst_type) {
				case ST_INDUSTRY: xy = Industry::Get(s->dst)->location.tile; break;
				case ST_TOWN:     xy =     Town::Get(s->dst)->xy; break;
				default: NOT_REACHED();
			}

			if (_ctrl_pressed) {
				ShowExtraViewportWindow(xy);
			} else {
				ScrollMainWindowToTile(xy);
			}
		}
	}

	/**
	 * Count the number of lines in this window.
	 * @return the number of lines
	 */
	uint CountLines()
	{
		/* Count number of (non) awarded subsidies */
		uint num_awarded = 0;
		uint num_not_awarded = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				num_not_awarded++;
			} else {
				num_awarded++;
			}
		}

		/* Count the 'none' lines */
		if (num_awarded     == 0) num_awarded = 1;
		if (num_not_awarded == 0) num_not_awarded = 1;

		/* Offered, accepted and an empty line before the accepted ones. */
		return 3 + num_awarded + num_not_awarded;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget != WID_SUL_PANEL) return;
		Dimension d = maxdim(GetStringBoundingBox(STR_SUBSIDIES_OFFERED_TITLE), GetStringBoundingBox(STR_SUBSIDIES_SUBSIDISED_TITLE));

		resize->height = FONT_HEIGHT_NORMAL;

		d.height *= 5;
		d.width += WidgetDimensions::scaled.framerect.Horizontal();
		d.height += WidgetDimensions::scaled.framerect.Vertical();
		*size = maxdim(*size, d);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_SUL_PANEL) return;

		YearMonthDay ymd;
		ConvertDateToYMD(_date, &ymd);

		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

		int pos = -this->vscroll->GetPosition();
		const int cap = this->vscroll->GetCapacity();

		/* Section for drawing the offered subsidies */
		if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_OFFERED_TITLE);
		pos++;

		uint num = 0;
		for (const Subsidy *s : Subsidy::Iterate()) {
			if (!s->IsAwarded()) {
				if (IsInsideMM(pos, 0, cap)) {
					/* Displays the two offered towns */
					SetupSubsidyDecodeParam(s, SubsidyDecodeParamType::Gui);
					SetDParam(7, _date - ymd.day + s->remaining * 32);
					DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_OFFERED_FROM_TO);
				}
				pos++;
				num++;
			}
		}

		if (num == 0) {
			if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_NONE);
			pos++;
		}

		/* Section for drawing the already granted subsidies */
		pos++;
		if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_SUBSIDISED_TITLE);
		pos++;
		num = 0;

		for (const Subsidy *s : Subsidy::Iterate()) {
			if (s->IsAwarded()) {
				if (IsInsideMM(pos, 0, cap)) {
					SetupSubsidyDecodeParam(s, SubsidyDecodeParamType::Gui);
					SetDParam(7, s->awarded);
					SetDParam(8, _date - ymd.day + s->remaining * 32);

					/* Displays the two connected stations */
					DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_SUBSIDISED_FROM_TO);
				}
				pos++;
				num++;
			}
		}

		if (num == 0) {
			if (IsInsideMM(pos, 0, cap)) DrawString(tr.left, tr.right, tr.top + pos * FONT_HEIGHT_NORMAL, STR_SUBSIDIES_NONE);
			pos++;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SUL_PANEL);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->vscroll->SetCount(this->CountLines());
	}
};

static const NWidgetPart _nested_subsidies_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_SUBSIDIES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1-84: dropped SHADEBOX/DEFSIZEBOX/STICKYBOX — their GUI sprites aren't in the loaded
		// base GRF, so they rendered as the missing-sprite "?". Closebox + caption stay.
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_SUL_PANEL), SetDataTip(0x0, STR_SUBSIDIES_TOOLTIP_CLICK_ON_SERVICE_TO_CENTER), SetResize(1, 1), SetScrollbar(WID_SUL_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_SUL_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _subsidies_list_desc(
	WDP_AUTO, "list_subsidies", 500, 127,
	WC_SUBSIDIES_LIST, WC_NONE,
	0,
	_nested_subsidies_list_widgets, lengthof(_nested_subsidies_list_widgets)
);


void ShowSubsidiesList()
{
	AllocateWindowDescFront<SubsidyListWindow>(&_subsidies_list_desc, 0);
}

// ----------------------------------------------------------------------------
// R1 stubs: subsidy.cpp is NOT compiled (its award/monthly/news/cargo cascade is the "subsidy
// seam"). This TU therefore owns the real _subsidy_pool — brand-new symbol, no ODR clash — exactly
// the way m1_station.cpp owns _station_pool. The pool starts EMPTY, so the window renders "None".
// SetupSubsidyDecodeParam's real home is the uncompiled subsidy.cpp; it is never reached on an empty
// pool, so an inert stub returning a default pair is all that is needed.
// ----------------------------------------------------------------------------
SubsidyPool _subsidy_pool("Subsidy");
INSTANTIATE_POOL_METHODS(Subsidy)

std::pair<NewsReferenceType, NewsReferenceType> SetupSubsidyDecodeParam(const Subsidy *, SubsidyDecodeParamType, uint)
{
	return std::pair<NewsReferenceType, NewsReferenceType>(NR_NONE, NR_NONE);
}
