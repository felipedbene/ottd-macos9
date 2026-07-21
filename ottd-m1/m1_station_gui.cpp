/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/station_gui.cpp (the CompanyStationsWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1: the REAL CompanyStationsWindow, extracted M1-style (mirrors m1_finance_gui.cpp /
// m1_town_directory_gui.cpp) so the canonical toolbar's STATION-LIST button opens the genuine
// scrollable/sortable/filterable per-company station list — real station names, facility/cargo
// filter buttons, mini rating graphs — instead of the no-op ShowCompanyStations stub. The slice
// below is verbatim OpenTTD station_gui.cpp:158-764 (the CompanyStationsWindow half only). Unlike
// the town directory this window has NO name-filter editbox, so it needs NO Textbuf/StringFilter
// stubs. The only things this TU adds are:
//   1. a no-op HasStationInUse (station_cmd.cpp isn't compiled) — OWNER_NONE stations are simply
//      excluded, which is correct for the single-company R1 world.
//   2. a local no-op Debug() macro (debug.cpp/fmt aren't dragged for two trace lines).
// _sorted_standard_cargo_specs / _sorted_cargo_specs / _cargo_mask / CargoSpec::Get resolve from
// cargotype.cpp (compiled); cargo.TotalCount()/HasRating() are header-inline. strnatcmp is provided
// by b1_shims.o — do NOT define it here.

#include "stdafx.h"
#include "station_base.h"
#include "cargotype.h"
#include "company_base.h"
#include "town.h"                         /* Station::GetCachedName pulls town cached name */
#include "strings_func.h"
#include "string_func.h"                 /* strnatcmp (defined in b1_shims.o) */
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"                     /* GetContrastColour, PC_RED/PC_GREEN, CenterBounds, GetCharacterHeight */
#include "zoom_func.h"                    /* ScaleGUITrad */
#include "sortlist_type.h"
#include "viewport_func.h"                /* ScrollMainWindowToTile */
#include "gui.h"                          /* ShowExtraViewportWindow */
#include "core/geometry_func.hpp"         /* maxdim */
#include "widgets/dropdown_func.h"
#include "widgets/station_widget.h"
#include "table/strings.h"

#include "safeguards.h"

// debug.cpp / fmt are not compiled into the R1 merge; neutralise the two trace calls in the slice.
#ifndef Debug
#define Debug(name, level, ...) ((void)0)
#endif

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/station_gui.cpp:158-764 (CompanyStationsWindow).
// Title bar keeps only WWT_CLOSEBOX + WWT_CAPTION (SHADEBOX/DEFSIZEBOX/STICKYBOX dropped — their
// GUI sprites aren't in the loaded base GRF and would render as the missing-sprite "?").
// ----------------------------------------------------------------------------

/**
 * Draw small boxes of cargo amount and ratings data at the given
 * coordinates. If amount exceeds 576 units, it is shown 'full', same
 * goes for the rating: at above 90% orso (224) it is also 'full'
 *
 * @param left   left most coordinate to draw the box at
 * @param right  right most coordinate to draw the box at
 * @param y      coordinate to draw the box at
 * @param type   Cargo type
 * @param amount Cargo amount
 * @param rating ratings data for that particular cargo
 */
static void StationsWndShowStationRating(int left, int right, int y, CargoID type, uint amount, byte rating)
{
	static const uint units_full  = 576; ///< number of units to show station as 'full'
	static const uint rating_full = 224; ///< rating needed so it is shown as 'full'

	const CargoSpec *cs = CargoSpec::Get(type);
	if (!cs->IsValid()) return;

	int padding = ScaleGUITrad(1);
	int width = right - left;
	int colour = cs->rating_colour;
	TextColour tc = GetContrastColour(colour);
	uint w = std::min(amount + 5, units_full) * width / units_full;

	int height = GetCharacterHeight(FS_SMALL) + padding - 1;

	if (amount > 30) {
		/* Draw total cargo (limited) on station */
		GfxFillRect(left, y, left + w - 1, y + height, colour);
	} else {
		/* Draw a (scaled) one pixel-wide bar of additional cargo meter, useful
		 * for stations with only a small amount (<=30) */
		uint rest = ScaleGUITrad(amount) / 5;
		if (rest != 0) {
			GfxFillRect(left, y + height - rest, left + padding - 1, y + height, colour);
		}
	}

	DrawString(left + padding, right, y, cs->abbrev, tc);

	/* Draw green/red ratings bar (fits under the waiting bar) */
	y += height + padding + 1;
	GfxFillRect(left + padding, y, right - padding - 1, y + padding - 1, PC_RED);
	w = std::min<uint>(rating, rating_full) * (width - padding - padding) / rating_full;
	if (w != 0) GfxFillRect(left + padding, y, left + w - 1, y + padding - 1, PC_GREEN);
}

typedef GUIList<const Station*> GUIStationList;

/**
 * The list of stations per company.
 */
class CompanyStationsWindow : public Window
{
protected:
	/* Runtime saved values */
	static Listing last_sorting;
	static byte facilities;               // types of stations of interest
	static bool include_empty;            // whether we should include stations without waiting cargo
	static const CargoTypes cargo_filter_max;
	static CargoTypes cargo_filter;           // bitmap of cargo types to include

	/* Constants for sorting stations */
	static const StringID sorter_names[];
	static GUIStationList::SortFunction * const sorter_funcs[];

	GUIStationList stations;
	Scrollbar *vscroll;
	uint rating_width;

	/**
	 * (Re)Build station list
	 *
	 * @param owner company whose stations are to be in list
	 */
	void BuildStationsList(const Owner owner)
	{
		if (!this->stations.NeedRebuild()) return;

		Debug(misc, 3, "Building station list for company {}", owner);

		this->stations.clear();

		for (const Station *st : Station::Iterate()) {
			if (st->owner == owner || (st->owner == OWNER_NONE && HasStationInUse(st->index, true, owner))) {
				if (this->facilities & st->facilities) { // only stations with selected facilities
					int num_waiting_cargo = 0;
					for (CargoID j = 0; j < NUM_CARGO; j++) {
						if (st->goods[j].HasRating()) {
							num_waiting_cargo++; // count number of waiting cargo
							if (HasBit(this->cargo_filter, j)) {
								this->stations.push_back(st);
								break;
							}
						}
					}
					/* stations without waiting cargo */
					if (num_waiting_cargo == 0 && this->include_empty) {
						this->stations.push_back(st);
					}
				}
			}
		}

		this->stations.shrink_to_fit();
		this->stations.RebuildDone();

		this->vscroll->SetCount((uint)this->stations.size()); // Update the scrollbar
	}

	/** Sort stations by their name */
	static bool StationNameSorter(const Station * const &a, const Station * const &b)
	{
		int r = strnatcmp(a->GetCachedName(), b->GetCachedName()); // Sort by name (natural sorting).
		if (r == 0) return a->index < b->index;
		return r < 0;
	}

	/** Sort stations by their type */
	static bool StationTypeSorter(const Station * const &a, const Station * const &b)
	{
		return a->facilities < b->facilities;
	}

	/** Sort stations by their waiting cargo */
	static bool StationWaitingTotalSorter(const Station * const &a, const Station * const &b)
	{
		int diff = 0;

		for (CargoID j : SetCargoBitIterator(cargo_filter)) {
			diff += a->goods[j].cargo.TotalCount() - b->goods[j].cargo.TotalCount();
		}

		return diff < 0;
	}

	/** Sort stations by their available waiting cargo */
	static bool StationWaitingAvailableSorter(const Station * const &a, const Station * const &b)
	{
		int diff = 0;

		for (CargoID j : SetCargoBitIterator(cargo_filter)) {
			diff += a->goods[j].cargo.AvailableCount() - b->goods[j].cargo.AvailableCount();
		}

		return diff < 0;
	}

	/** Sort stations by their rating */
	static bool StationRatingMaxSorter(const Station * const &a, const Station * const &b)
	{
		byte maxr1 = 0;
		byte maxr2 = 0;

		for (CargoID j : SetCargoBitIterator(cargo_filter)) {
			if (a->goods[j].HasRating()) maxr1 = std::max(maxr1, a->goods[j].rating);
			if (b->goods[j].HasRating()) maxr2 = std::max(maxr2, b->goods[j].rating);
		}

		return maxr1 < maxr2;
	}

	/** Sort stations by their rating */
	static bool StationRatingMinSorter(const Station * const &a, const Station * const &b)
	{
		byte minr1 = 255;
		byte minr2 = 255;

		for (CargoID j = 0; j < NUM_CARGO; j++) {
			if (!HasBit(cargo_filter, j)) continue;
			if (a->goods[j].HasRating()) minr1 = std::min(minr1, a->goods[j].rating);
			if (b->goods[j].HasRating()) minr2 = std::min(minr2, b->goods[j].rating);
		}

		return minr1 > minr2;
	}

	/** Sort the stations list */
	void SortStationsList()
	{
		if (!this->stations.Sort()) return;

		/* Set the modified widget dirty */
		this->SetWidgetDirty(WID_STL_LIST);
	}

public:
	CompanyStationsWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->stations.SetListing(this->last_sorting);
		this->stations.SetSortFuncs(this->sorter_funcs);
		this->stations.ForceRebuild();
		this->stations.NeedResort();
		this->SortStationsList();

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_STL_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->owner = (Owner)this->window_number;

		uint8 index = 0;
		for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
			if (HasBit(this->cargo_filter, cs->Index())) {
				this->LowerWidget(WID_STL_CARGOSTART + index);
			}
			index++;
		}

		if (this->cargo_filter == this->cargo_filter_max) this->cargo_filter = _cargo_mask;

		for (uint i = 0; i < 5; i++) {
			if (HasBit(this->facilities, i)) this->LowerWidget(i + WID_STL_TRAIN);
		}
		this->SetWidgetLoweredState(WID_STL_NOCARGOWAITING, this->include_empty);

		this->GetWidget<NWidgetCore>(WID_STL_SORTDROPBTN)->widget_data = this->sorter_names[this->stations.SortType()];
	}

	~CompanyStationsWindow()
	{
		this->last_sorting = this->stations.GetListing();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_STL_SORTBY: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_STL_SORTDROPBTN: {
				Dimension d = {0, 0};
				for (int i = 0; this->sorter_names[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(this->sorter_names[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_STL_LIST:
				resize->height = std::max(FONT_HEIGHT_NORMAL, FONT_HEIGHT_SMALL + ScaleGUITrad(3));
				size->height = padding.height + 5 * resize->height;

				/* Determine appropriate width for mini station rating graph */
				this->rating_width = 0;
				for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
					this->rating_width = std::max(this->rating_width, GetStringBoundingBox(cs->abbrev).width);
				}
				/* Approximately match original 16 pixel wide rating bars by multiplying string width by 1.6 */
				this->rating_width = this->rating_width * 16 / 10;
				break;

			default:
				if (widget >= WID_STL_CARGOSTART) {
					Dimension d = GetStringBoundingBox(_sorted_cargo_specs[widget - WID_STL_CARGOSTART]->abbrev);
					d.width  += padding.width + 2;
					d.height += padding.height;
					*size = maxdim(*size, d);
				}
				break;
		}
	}

	void OnPaint() override
	{
		this->BuildStationsList((Owner)this->window_number);
		this->SortStationsList();

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_STL_SORTBY:
				/* draw arrow pointing up/down for ascending/descending sorting */
				this->DrawSortButtonState(WID_STL_SORTBY, this->stations.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_STL_LIST: {
				bool rtl = _current_text_dir == TD_RTL;
				int max = std::min<size_t>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->stations.size());
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				uint line_height = this->GetWidget<NWidgetBase>(widget)->resize_y;
				/* Spacing between station name and first rating graph. */
				int text_spacing = WidgetDimensions::scaled.hsep_wide;
				/* Spacing between additional rating graphs. */
				int rating_spacing = WidgetDimensions::scaled.hsep_normal;

				for (int i = this->vscroll->GetPosition(); i < max; ++i) { // do until max number of stations of owner
					const Station *st = this->stations[i];
					assert(st->xy != INVALID_TILE);

					/* Do not do the complex check HasStationInUse here, it may be even false
					 * when the order had been removed and the station list hasn't been removed yet */
					assert(st->owner == owner || st->owner == OWNER_NONE);

					SetDParam(0, st->index);
					SetDParam(1, st->facilities);
					int x = DrawString(tr.left, tr.right, tr.top + (line_height - FONT_HEIGHT_NORMAL) / 2, STR_STATION_LIST_STATION);
					x += rtl ? -text_spacing : text_spacing;

					/* show cargo waiting and station ratings */
					for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
						CargoID cid = cs->Index();
						if (st->goods[cid].cargo.TotalCount() > 0) {
							/* For RTL we work in exactly the opposite direction. So
							 * decrement the space needed first, then draw to the left
							 * instead of drawing to the left and then incrementing
							 * the space. */
							if (rtl) {
								x -= rating_width + rating_spacing;
								if (x < tr.left) break;
							}
							StationsWndShowStationRating(x, x + rating_width, tr.top, cid, st->goods[cid].cargo.TotalCount(), st->goods[cid].rating);
							if (!rtl) {
								x += rating_width + rating_spacing;
								if (x > tr.right) break;
							}
						}
					}
					tr.top += line_height;
				}

				if (this->vscroll->GetCount() == 0) { // company has no stations
					DrawString(tr.left, tr.right, tr.top + (line_height - FONT_HEIGHT_NORMAL) / 2, STR_STATION_LIST_NONE);
					return;
				}
				break;
			}

			default:
				if (widget >= WID_STL_CARGOSTART) {
					Rect br = r.Shrink(WidgetDimensions::scaled.bevel);
					const CargoSpec *cs = _sorted_cargo_specs[widget - WID_STL_CARGOSTART];
					int cg_ofst = HasBit(this->cargo_filter, cs->Index()) ? WidgetDimensions::scaled.pressed : 0;
					br = br.Translate(cg_ofst, cg_ofst);
					GfxFillRect(br, cs->rating_colour);
					TextColour tc = GetContrastColour(cs->rating_colour);
					DrawString(br.left, br.right, CenterBounds(br.top, br.bottom, FONT_HEIGHT_SMALL), cs->abbrev, tc, SA_HOR_CENTER);
				}
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_STL_CAPTION) {
			SetDParam(0, this->window_number);
			SetDParam(1, this->vscroll->GetCount());
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_STL_LIST: {
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_STL_LIST);
				if (id_v >= this->stations.size()) return; // click out of list bound

				const Station *st = this->stations[id_v];
				/* do not check HasStationInUse - it is slow and may be invalid */
				assert(st->owner == (Owner)this->window_number || st->owner == OWNER_NONE);

				if (_ctrl_pressed) {
					ShowExtraViewportWindow(st->xy);
				} else {
					ScrollMainWindowToTile(st->xy);
				}
				break;
			}

			case WID_STL_TRAIN:
			case WID_STL_TRUCK:
			case WID_STL_BUS:
			case WID_STL_AIRPLANE:
			case WID_STL_SHIP:
				if (_ctrl_pressed) {
					ToggleBit(this->facilities, widget - WID_STL_TRAIN);
					this->ToggleWidgetLoweredState(widget);
				} else {
					for (uint i : SetBitIterator(this->facilities)) {
						this->RaiseWidget(i + WID_STL_TRAIN);
					}
					this->facilities = 1 << (widget - WID_STL_TRAIN);
					this->LowerWidget(widget);
				}
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			case WID_STL_FACILALL:
				for (uint i = WID_STL_TRAIN; i <= WID_STL_SHIP; i++) {
					this->LowerWidget(i);
				}

				this->facilities = FACIL_TRAIN | FACIL_TRUCK_STOP | FACIL_BUS_STOP | FACIL_AIRPORT | FACIL_DOCK;
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			case WID_STL_CARGOALL: {
				for (uint i = 0; i < _sorted_standard_cargo_specs.size(); i++) {
					this->LowerWidget(WID_STL_CARGOSTART + i);
				}
				this->LowerWidget(WID_STL_NOCARGOWAITING);

				this->cargo_filter = _cargo_mask;
				this->include_empty = true;
				this->stations.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_STL_SORTBY: // flip sorting method asc/desc
				this->stations.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_STL_SORTDROPBTN: // select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->sorter_names, this->stations.SortType(), WID_STL_SORTDROPBTN, 0, 0);
				break;

			case WID_STL_NOCARGOWAITING:
				if (_ctrl_pressed) {
					this->include_empty = !this->include_empty;
					this->ToggleWidgetLoweredState(WID_STL_NOCARGOWAITING);
				} else {
					for (uint i = 0; i < _sorted_standard_cargo_specs.size(); i++) {
						this->RaiseWidget(WID_STL_CARGOSTART + i);
					}

					this->cargo_filter = 0;
					this->include_empty = true;

					this->LowerWidget(WID_STL_NOCARGOWAITING);
				}
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			default:
				if (widget >= WID_STL_CARGOSTART) { // change cargo_filter
					/* Determine the selected cargo type */
					const CargoSpec *cs = _sorted_cargo_specs[widget - WID_STL_CARGOSTART];

					if (_ctrl_pressed) {
						ToggleBit(this->cargo_filter, cs->Index());
						this->ToggleWidgetLoweredState(widget);
					} else {
						for (uint i = 0; i < _sorted_standard_cargo_specs.size(); i++) {
							this->RaiseWidget(WID_STL_CARGOSTART + i);
						}
						this->RaiseWidget(WID_STL_NOCARGOWAITING);

						this->cargo_filter = 0;
						this->include_empty = false;

						SetBit(this->cargo_filter, cs->Index());
						this->LowerWidget(widget);
					}
					this->stations.ForceRebuild();
					this->SetDirty();
				}
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		if (this->stations.SortType() != index) {
			this->stations.SetSortType(index);

			/* Display the current sort variant */
			this->GetWidget<NWidgetCore>(WID_STL_SORTDROPBTN)->widget_data = this->sorter_names[this->stations.SortType()];

			this->SetDirty();
		}
	}

	void OnGameTick() override
	{
		if (this->stations.NeedResort()) {
			Debug(misc, 3, "Periodic rebuild station list company {}", this->window_number);
			this->SetDirty();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_STL_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->stations.ForceRebuild();
		} else {
			this->stations.ForceResort();
		}
	}
};

Listing CompanyStationsWindow::last_sorting = {false, 0};
byte CompanyStationsWindow::facilities = FACIL_TRAIN | FACIL_TRUCK_STOP | FACIL_BUS_STOP | FACIL_AIRPORT | FACIL_DOCK;
bool CompanyStationsWindow::include_empty = true;
const CargoTypes CompanyStationsWindow::cargo_filter_max = ALL_CARGOTYPES;
CargoTypes CompanyStationsWindow::cargo_filter = ALL_CARGOTYPES;

/* Available station sorting functions */
GUIStationList::SortFunction * const CompanyStationsWindow::sorter_funcs[] = {
	&StationNameSorter,
	&StationTypeSorter,
	&StationWaitingTotalSorter,
	&StationWaitingAvailableSorter,
	&StationRatingMaxSorter,
	&StationRatingMinSorter
};

/* Names of the sorting functions */
const StringID CompanyStationsWindow::sorter_names[] = {
	STR_SORT_BY_NAME,
	STR_SORT_BY_FACILITY,
	STR_SORT_BY_WAITING_TOTAL,
	STR_SORT_BY_WAITING_AVAILABLE,
	STR_SORT_BY_RATING_MAX,
	STR_SORT_BY_RATING_MIN,
	INVALID_STRING_ID
};

/**
 * Make a horizontal row of cargo buttons, starting at widget #WID_STL_CARGOSTART.
 * @param biggest_index Pointer to store biggest used widget number of the buttons.
 * @return Horizontal row.
 */
static NWidgetBase *CargoWidgets(int *biggest_index)
{
	NWidgetHorizontal *container = new NWidgetHorizontal();

	for (uint i = 0; i < _sorted_standard_cargo_specs.size(); i++) {
		NWidgetBackground *panel = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, WID_STL_CARGOSTART + i);
		panel->SetMinimalSize(14, 0);
		panel->SetMinimalTextLines(1, 0, FS_NORMAL);
		panel->SetResize(0, 0);
		panel->SetFill(0, 1);
		panel->SetDataTip(0, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE);
		container->Add(panel);
	}
	*biggest_index = WID_STL_CARGOSTART + static_cast<int>(_sorted_standard_cargo_specs.size());
	return container;
}

static const NWidgetPart _nested_company_stations_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_STL_CAPTION), SetDataTip(STR_STATION_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1: dropped SHADEBOX/DEFSIZEBOX/STICKYBOX — their GUI sprites aren't in the loaded base
		// GRF, so they rendered as the missing-sprite "?". Closebox + caption stay.
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_TRAIN), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_TRAIN, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_TRUCK), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_LORRY, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_BUS), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_BUS, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_SHIP), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_SHIP, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_AIRPLANE), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_PLANE, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STL_FACILALL), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_ABBREV_ALL, STR_STATION_LIST_SELECT_ALL_FACILITIES), SetFill(0, 1),
		NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(5, 0), SetFill(0, 1), EndContainer(),
		NWidgetFunction(CargoWidgets),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_STL_NOCARGOWAITING), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_ABBREV_NONE, STR_STATION_LIST_NO_WAITING_CARGO), SetFill(0, 1),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STL_CARGOALL), SetMinimalSize(14, 0), SetMinimalTextLines(1, 0), SetDataTip(STR_ABBREV_ALL, STR_STATION_LIST_SELECT_ALL_TYPES), SetFill(0, 1),
		NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_STL_SORTBY), SetMinimalSize(81, 12), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_STL_SORTDROPBTN), SetMinimalSize(163, 12), SetDataTip(STR_SORT_BY_NAME, STR_TOOLTIP_SORT_CRITERIA), // widget_data gets overwritten.
		NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_STL_LIST), SetMinimalSize(346, 125), SetResize(1, 10), SetDataTip(0x0, STR_STATION_LIST_TOOLTIP), SetScrollbar(WID_STL_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_STL_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _company_stations_desc(
	WDP_AUTO, "list_stations", 358, 162,
	WC_STATION_LIST, WC_NONE,
	0,
	_nested_company_stations_widgets, lengthof(_nested_company_stations_widgets)
);

/**
 * Opens window with list of company's stations
 *
 * @param company whose stations' list show
 */
void ShowCompanyStations(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<CompanyStationsWindow>(&_company_stations_desc, company);
}

// ----------------------------------------------------------------------------
// R1 stubs: OWNER_NONE "in use" stations are excluded (station_cmd.cpp isn't compiled). In the
// single-company R1 world every real station is owned by the company, so this loses nothing.
// ----------------------------------------------------------------------------
bool HasStationInUse(StationID, bool, CompanyID) { return false; }
