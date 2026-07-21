/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/graph_gui.cpp (the BaseGraphWindow +
 * OperatingProfitGraphWindow + IncomeGraphWindow slice), Copyright (c) the
 * OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1-86: the REAL operating-profit + income graph windows, extracted M1-style (mirrors
// m1_finance_gui.cpp / m1_town_directory_gui.cpp) so the canonical toolbar's GRAPH buttons open
// the genuine performance graphs (real per-company quarterly history, currency Y axis, date X
// axis) instead of no-op stubs. The slice below is verbatim OpenTTD graph_gui.cpp
// (helpers + BaseGraphWindow + the two currency graphs + their widget trees / descs / Show fns).
// The only things this TU adds are:
//   1. a no-op `ShowGraphLegend()` — the GraphLegendWindow (the "Key" button target) is NOT
//      extracted (it drags cargo machinery), and the WID_CV_KEY_BUTTON widget is dropped from the
//      title bars, so BaseGraphWindow::OnClick's ShowGraphLegend() branch is unreachable but still
//      needs a symbol to link.
// The runtime data feeder (the quarterly history-roll in m1_economy.cpp) is the integrator's job;
// until it exists the graph renders an empty grid (num_valid_stat_ent == 0), which is correct.
//
// graph_gui.cpp's heavier includes (cargotype.h and the delivered-cargo / performance-history /
// company-value / payment-rate windows) are NOT needed by the two currency graphs, so this stays
// light and drags no cargo/score subsystem.

#include "stdafx.h"
#include "graph_gui.h"
#include "window_gui.h"
#include "company_base.h"
#include "company_gui.h"
#include "economy_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "gfx_func.h"
#include "core/geometry_func.hpp"
#include "currency.h"
#include "zoom_func.h"

#include "widgets/graph_widget.h"

#include "table/strings.h"
#include "table/sprites.h"
#include <math.h>

#include "safeguards.h"

// graph_gui.cpp's OnClick "Key" button target. The GraphLegendWindow is not extracted (it drags
// cargo machinery) and the WID_CV_KEY_BUTTON widget is dropped from the title bars below, so this
// is an unreachable no-op that only satisfies the linker.
static void ShowGraphLegend() {}

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/graph_gui.cpp (BaseGraphWindow + operating-profit + income).
// ----------------------------------------------------------------------------

/* Bitmasks of company and cargo indices that shouldn't be drawn. */
static CompanyMask _legend_excluded_companies;
static CargoTypes _legend_excluded_cargo;

/* Apparently these don't play well with enums. */
static const OverflowSafeInt64 INVALID_DATAPOINT(INT64_MAX); // Value used for a datapoint that shouldn't be drawn.
static const uint INVALID_DATAPOINT_POS = UINT_MAX;  // Used to determine if the previous point was drawn.

/** Contains the interval of a graph's data. */
struct ValuesInterval {
	OverflowSafeInt64 highest; ///< Highest value of this interval. Must be zero or greater.
	OverflowSafeInt64 lowest;  ///< Lowest value of this interval. Must be zero or less.
};

/******************/
/* BASE OF GRAPHS */
/*****************/

struct BaseGraphWindow : Window {
protected:
	static const int GRAPH_MAX_DATASETS     =  64;
	static const int GRAPH_BASE_COLOUR      =  GREY_SCALE(2);
	static const int GRAPH_GRID_COLOUR      =  GREY_SCALE(3);
	static const int GRAPH_AXIS_LINE_COLOUR =  GREY_SCALE(1);
	static const int GRAPH_ZERO_LINE_COLOUR =  GREY_SCALE(8);
	static const int GRAPH_YEAR_LINE_COLOUR =  GREY_SCALE(5);
	static const int GRAPH_NUM_MONTHS       =  24; ///< Number of months displayed in the graph.

	static const TextColour GRAPH_AXIS_LABEL_COLOUR = TC_BLACK; ///< colour of the graph axis label.

	static const int MIN_GRAPH_NUM_LINES_Y  =   9; ///< Minimal number of horizontal lines to draw.
	static const int MIN_GRID_PIXEL_SIZE    =  20; ///< Minimum distance between graph lines.

	uint64 excluded_data; ///< bitmask of the datasets that shouldn't be displayed.
	byte num_dataset;
	byte num_on_x_axis;
	byte num_vert_lines;

	/* The starting month and year that values are plotted against. If month is
	 * 0xFF, use x_values_start and x_values_increment below instead. */
	byte month;
	Year year;

	/* These values are used if the graph is being plotted against values
	 * rather than the dates specified by month and year. */
	uint16 x_values_start;
	uint16 x_values_increment;

	int graph_widget;
	StringID format_str_y_axis;
	byte colours[GRAPH_MAX_DATASETS];
	OverflowSafeInt64 cost[GRAPH_MAX_DATASETS][GRAPH_NUM_MONTHS]; ///< Stored costs for the last #GRAPH_NUM_MONTHS months

	/**
	 * Get the interval that contains the graph's data. Excluded data is ignored to show smaller values in
	 * better detail when disabling higher ones.
	 * @param num_hori_lines Number of horizontal lines to be drawn.
	 * @return Highest and lowest values of the graph (ignoring disabled data).
	 */
	ValuesInterval GetValuesInterval(int num_hori_lines) const
	{
		assert(num_hori_lines > 0);

		ValuesInterval current_interval;
		current_interval.highest = INT64_MIN;
		current_interval.lowest  = INT64_MAX;

		for (int i = 0; i < this->num_dataset; i++) {
			if (HasBit(this->excluded_data, i)) continue;
			for (int j = 0; j < this->num_on_x_axis; j++) {
				OverflowSafeInt64 datapoint = this->cost[i][j];

				if (datapoint != INVALID_DATAPOINT) {
					current_interval.highest = std::max(current_interval.highest, datapoint);
					current_interval.lowest  = std::min(current_interval.lowest, datapoint);
				}
			}
		}

		/* Prevent showing values too close to the graph limits. */
		current_interval.highest = (11 * current_interval.highest) / 10;
		current_interval.lowest =  (11 * current_interval.lowest) / 10;

		/* Always include zero in the shown range. */
		double abs_lower  = (current_interval.lowest > 0) ? 0 : (double)abs(current_interval.lowest);
		double abs_higher = (current_interval.highest < 0) ? 0 : (double)current_interval.highest;

		int num_pos_grids;
		int64 grid_size;

		if (abs_lower != 0 || abs_higher != 0) {
			/* The number of grids to reserve for the positive part is: */
			num_pos_grids = (int)floor(0.5 + num_hori_lines * abs_higher / (abs_higher + abs_lower));

			/* If there are any positive or negative values, force that they have at least one grid. */
			if (num_pos_grids == 0 && abs_higher != 0) num_pos_grids++;
			if (num_pos_grids == num_hori_lines && abs_lower != 0) num_pos_grids--;

			/* Get the required grid size for each side and use the maximum one. */
			int64 grid_size_higher = (abs_higher > 0) ? ((int64)abs_higher + num_pos_grids - 1) / num_pos_grids : 0;
			int64 grid_size_lower = (abs_lower > 0) ? ((int64)abs_lower + num_hori_lines - num_pos_grids - 1) / (num_hori_lines - num_pos_grids) : 0;
			grid_size = std::max(grid_size_higher, grid_size_lower);
		} else {
			/* If both values are zero, show an empty graph. */
			num_pos_grids = num_hori_lines / 2;
			grid_size = 1;
		}

		current_interval.highest = num_pos_grids * grid_size;
		current_interval.lowest = -(num_hori_lines - num_pos_grids) * grid_size;
		return current_interval;
	}

	/**
	 * Get width for Y labels.
	 * @param current_interval Interval that contains all of the graph data.
	 * @param num_hori_lines Number of horizontal lines to be drawn.
	 */
	uint GetYLabelWidth(ValuesInterval current_interval, int num_hori_lines) const
	{
		/* draw text strings on the y axis */
		int64 y_label = current_interval.highest;
		int64 y_label_separation = (current_interval.highest - current_interval.lowest) / num_hori_lines;

		uint max_width = 0;

		for (int i = 0; i < (num_hori_lines + 1); i++) {
			SetDParam(0, this->format_str_y_axis);
			SetDParam(1, y_label);
			Dimension d = GetStringBoundingBox(STR_GRAPH_Y_LABEL);
			if (d.width > max_width) max_width = d.width;

			y_label -= y_label_separation;
		}

		return max_width;
	}

	/**
	 * Actually draw the graph.
	 * @param r the rectangle of the data field of the graph
	 */
	void DrawGraph(Rect r) const
	{
		uint x, y;               ///< Reused whenever x and y coordinates are needed.
		ValuesInterval interval; ///< Interval that contains all of the graph data.
		int x_axis_offset;       ///< Distance from the top of the graph to the x axis.

		/* the colours and cost array of GraphDrawer must accommodate
		 * both values for cargo and companies. So if any are higher, quit */
		static_assert(GRAPH_MAX_DATASETS >= (int)NUM_CARGO && GRAPH_MAX_DATASETS >= (int)MAX_COMPANIES);
		assert(this->num_vert_lines > 0);

		/* Rect r will be adjusted to contain just the graph, with labels being
		 * placed outside the area. */
		r.top    += ScaleGUITrad(5) + GetCharacterHeight(FS_SMALL) / 2;
		r.bottom -= (this->month == 0xFF ? 1 : 2) * GetCharacterHeight(FS_SMALL) + ScaleGUITrad(4);
		r.left   += ScaleGUITrad(9);
		r.right  -= ScaleGUITrad(5);

		/* Initial number of horizontal lines. */
		int num_hori_lines = 160 / ScaleGUITrad(MIN_GRID_PIXEL_SIZE);
		/* For the rest of the height, the number of horizontal lines will increase more slowly. */
		int resize = (r.bottom - r.top - 160) / (2 * ScaleGUITrad(MIN_GRID_PIXEL_SIZE));
		if (resize > 0) num_hori_lines += resize;

		interval = GetValuesInterval(num_hori_lines);

		int label_width = GetYLabelWidth(interval, num_hori_lines);

		r.left += label_width;

		int x_sep = (r.right - r.left) / this->num_vert_lines;
		int y_sep = (r.bottom - r.top) / num_hori_lines;

		/* Redetermine right and bottom edge of graph to fit with the integer
		 * separation values. */
		r.right = r.left + x_sep * this->num_vert_lines;
		r.bottom = r.top + y_sep * num_hori_lines;

		OverflowSafeInt64 interval_size = interval.highest + abs(interval.lowest);
		/* Where to draw the X axis. Use floating point to avoid overflowing and results of zero. */
		x_axis_offset = (int)((r.bottom - r.top) * (double)interval.highest / (double)interval_size);

		/* Draw the background of the graph itself. */
		GfxFillRect(r.left, r.top, r.right, r.bottom, GRAPH_BASE_COLOUR);

		/* Draw the vertical grid lines. */

		/* Don't draw the first line, as that's where the axis will be. */
		x = r.left + x_sep;

		for (int i = 0; i < this->num_vert_lines; i++) {
			GfxFillRect(x, r.top, x, r.bottom, GRAPH_GRID_COLOUR);
			x += x_sep;
		}

		/* Draw the horizontal grid lines. */
		y = r.bottom;

		for (int i = 0; i < (num_hori_lines + 1); i++) {
			GfxFillRect(r.left - ScaleGUITrad(3), y, r.left - 1, y, GRAPH_AXIS_LINE_COLOUR);
			GfxFillRect(r.left, y, r.right, y, GRAPH_GRID_COLOUR);
			y -= y_sep;
		}

		/* Draw the y axis. */
		GfxFillRect(r.left, r.top, r.left, r.bottom, GRAPH_AXIS_LINE_COLOUR);

		/* Draw the x axis. */
		y = x_axis_offset + r.top;
		GfxFillRect(r.left, y, r.right, y, GRAPH_ZERO_LINE_COLOUR);

		/* Find the largest value that will be drawn. */
		if (this->num_on_x_axis == 0) return;

		assert(this->num_on_x_axis > 0);
		assert(this->num_dataset > 0);

		/* draw text strings on the y axis */
		int64 y_label = interval.highest;
		int64 y_label_separation = abs(interval.highest - interval.lowest) / num_hori_lines;

		y = r.top - GetCharacterHeight(FS_SMALL) / 2;

		for (int i = 0; i < (num_hori_lines + 1); i++) {
			SetDParam(0, this->format_str_y_axis);
			SetDParam(1, y_label);
			DrawString(r.left - label_width - ScaleGUITrad(4), r.left - ScaleGUITrad(4), y, STR_GRAPH_Y_LABEL, GRAPH_AXIS_LABEL_COLOUR, SA_RIGHT);

			y_label -= y_label_separation;
			y += y_sep;
		}

		/* Draw x-axis labels and markings for graphs based on financial quarters and years.  */
		if (this->month != 0xFF) {
			x = r.left;
			y = r.bottom + ScaleGUITrad(2);
			byte month = this->month;
			Year year  = this->year;
			for (int i = 0; i < this->num_on_x_axis; i++) {
				SetDParam(0, month + STR_MONTH_ABBREV_JAN);
				SetDParam(1, year);
				DrawStringMultiLine(x, x + x_sep, y, this->height, month == 0 ? STR_GRAPH_X_LABEL_MONTH_YEAR : STR_GRAPH_X_LABEL_MONTH, GRAPH_AXIS_LABEL_COLOUR, SA_LEFT);

				month += 3;
				if (month >= 12) {
					month = 0;
					year++;

					/* Draw a lighter grid line between years. Top and bottom adjustments ensure we don't draw over top and bottom horizontal grid lines. */
					GfxFillRect(x + x_sep, r.top + 1, x + x_sep, r.bottom - 1, GRAPH_YEAR_LINE_COLOUR);
				}
				x += x_sep;
			}
		} else {
			/* Draw x-axis labels for graphs not based on quarterly performance (cargo payment rates). */
			x = r.left;
			y = r.bottom + ScaleGUITrad(2);
			uint16 label = this->x_values_start;

			for (int i = 0; i < this->num_on_x_axis; i++) {
				SetDParam(0, label);
				DrawString(x + 1, x + x_sep - 1, y, STR_GRAPH_Y_LABEL_NUMBER, GRAPH_AXIS_LABEL_COLOUR, SA_HOR_CENTER);

				label += this->x_values_increment;
				x += x_sep;
			}
		}

		/* draw lines and dots */
		uint linewidth = _settings_client.gui.graph_line_thickness;
		uint pointoffs1 = (linewidth + 1) / 2;
		uint pointoffs2 = linewidth + 1 - pointoffs1;
		for (int i = 0; i < this->num_dataset; i++) {
			if (!HasBit(this->excluded_data, i)) {
				/* Centre the dot between the grid lines. */
				x = r.left + (x_sep / 2);

				byte colour  = this->colours[i];
				uint prev_x = INVALID_DATAPOINT_POS;
				uint prev_y = INVALID_DATAPOINT_POS;

				for (int j = 0; j < this->num_on_x_axis; j++) {
					OverflowSafeInt64 datapoint = this->cost[i][j];

					if (datapoint != INVALID_DATAPOINT) {
						/*
						 * Check whether we need to reduce the 'accuracy' of the
						 * datapoint value and the highest value to split overflows.
						 * And when 'drawing' 'one million' or 'one million and one'
						 * there is no significant difference, so the least
						 * significant bits can just be removed.
						 *
						 * If there are more bits needed than would fit in a 32 bits
						 * integer, so at about 31 bits because of the sign bit, the
						 * least significant bits are removed.
						 */
						int mult_range = FindLastBit(x_axis_offset) + FindLastBit(abs(datapoint));
						int reduce_range = std::max(mult_range - 31, 0);

						/* Handle negative values differently (don't shift sign) */
						if (datapoint < 0) {
							datapoint = -(abs(datapoint) >> reduce_range);
						} else {
							datapoint >>= reduce_range;
						}
						y = r.top + x_axis_offset - ((r.bottom - r.top) * datapoint) / (interval_size >> reduce_range);

						/* Draw the point. */
						GfxFillRect(x - pointoffs1, y - pointoffs1, x + pointoffs2, y + pointoffs2, colour);

						/* Draw the line connected to the previous point. */
						if (prev_x != INVALID_DATAPOINT_POS) GfxDrawLine(prev_x, prev_y, x, y, colour, linewidth);

						prev_x = x;
						prev_y = y;
					} else {
						prev_x = INVALID_DATAPOINT_POS;
						prev_y = INVALID_DATAPOINT_POS;
					}

					x += x_sep;
				}
			}
		}
	}


	BaseGraphWindow(WindowDesc *desc, int widget, StringID format_str_y_axis) :
			Window(desc),
			format_str_y_axis(format_str_y_axis)
	{
		SetWindowDirty(WC_GRAPH_LEGEND, 0);
		this->num_vert_lines = 24;
		this->graph_widget = widget;
	}

	void InitializeWindow(WindowNumber number)
	{
		/* Initialise the dataset */
		this->UpdateStatistics(true);

		this->InitNested(number);
	}

public:
	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget != this->graph_widget) return;

		uint x_label_width = 0;

		/* Draw x-axis labels and markings for graphs based on financial quarters and years.  */
		if (this->month != 0xFF) {
			byte month = this->month;
			Year year  = this->year;
			for (int i = 0; i < this->num_on_x_axis; i++) {
				SetDParam(0, month + STR_MONTH_ABBREV_JAN);
				SetDParam(1, year);
				x_label_width = std::max(x_label_width, GetStringBoundingBox(month == 0 ? STR_GRAPH_X_LABEL_MONTH_YEAR : STR_GRAPH_X_LABEL_MONTH).width);

				month += 3;
				if (month >= 12) {
					month = 0;
					year++;
				}
			}
		} else {
			/* Draw x-axis labels for graphs not based on quarterly performance (cargo payment rates). */
			SetDParamMaxValue(0, this->x_values_start + this->num_on_x_axis * this->x_values_increment, 0, FS_SMALL);
			x_label_width = GetStringBoundingBox(STR_GRAPH_Y_LABEL_NUMBER).width;
		}

		SetDParam(0, this->format_str_y_axis);
		SetDParam(1, INT64_MAX);
		uint y_label_width = GetStringBoundingBox(STR_GRAPH_Y_LABEL).width;

		size->width  = std::max<uint>(size->width,  ScaleGUITrad(5) + y_label_width + this->num_on_x_axis * (x_label_width + ScaleGUITrad(5)) + ScaleGUITrad(9));
		size->height = std::max<uint>(size->height, ScaleGUITrad(5) + (1 + MIN_GRAPH_NUM_LINES_Y * 2 + (this->month != 0xFF ? 3 : 1)) * FONT_HEIGHT_SMALL + ScaleGUITrad(4));
		size->height = std::max<uint>(size->height, size->width / 3);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != this->graph_widget) return;

		DrawGraph(r);
	}

	virtual OverflowSafeInt64 GetGraphData(const Company *c, int j)
	{
		return INVALID_DATAPOINT;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		/* Clicked on legend? */
		if (widget == WID_CV_KEY_BUTTON) ShowGraphLegend();
	}

	void OnGameTick() override
	{
		this->UpdateStatistics(false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateStatistics(true);
	}

	/**
	 * Update the statistics.
	 * @param initialize Initialize the data structure.
	 */
	void UpdateStatistics(bool initialize)
	{
		CompanyMask excluded_companies = _legend_excluded_companies;

		/* Exclude the companies which aren't valid */
		for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
			if (!Company::IsValidID(c)) SetBit(excluded_companies, c);
		}

		byte nums = 0;
		for (const Company *c : Company::Iterate()) {
			nums = std::min(this->num_vert_lines, std::max(nums, c->num_valid_stat_ent));
		}

		int mo = (_cur_month / 3 - nums) * 3;
		int yr = _cur_year;
		while (mo < 0) {
			yr--;
			mo += 12;
		}

		if (!initialize && this->excluded_data == excluded_companies && this->num_on_x_axis == nums &&
				this->year == yr && this->month == mo) {
			/* There's no reason to get new stats */
			return;
		}

		this->excluded_data = excluded_companies;
		this->num_on_x_axis = nums;
		this->year = yr;
		this->month = mo;

		int numd = 0;
		for (CompanyID k = COMPANY_FIRST; k < MAX_COMPANIES; k++) {
			const Company *c = Company::GetIfValid(k);
			if (c != nullptr) {
				this->colours[numd] = _colour_gradient[c->colour][6];
				for (int j = this->num_on_x_axis, i = 0; --j >= 0;) {
					this->cost[numd][i] = (j >= c->num_valid_stat_ent) ? INVALID_DATAPOINT : GetGraphData(c, j);
					i++;
				}
			}
			numd++;
		}

		this->num_dataset = numd;
	}
};


/********************/
/* OPERATING PROFIT */
/********************/

struct OperatingProfitGraphWindow : BaseGraphWindow {
	OperatingProfitGraphWindow(WindowDesc *desc, WindowNumber window_number) :
			BaseGraphWindow(desc, WID_CV_GRAPH, STR_JUST_CURRENCY_SHORT)
	{
		this->InitializeWindow(window_number);
	}

	OverflowSafeInt64 GetGraphData(const Company *c, int j) override
	{
		return c->old_economy[j].income + c->old_economy[j].expenses;
	}
};

static const NWidgetPart _nested_operating_profit_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_GRAPH_OPERATING_PROFIT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1-86: dropped the WID_CV_KEY_BUTTON "Key" PUSHTXTBTN (its GraphLegendWindow isn't
		// extracted) and the SHADEBOX/DEFSIZEBOX/STICKYBOX/RESIZEBOX (their GUI sprites aren't in
		// the loaded base GRF, so they rendered as the missing-sprite "?"). Closebox + caption stay.
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_CV_BACKGROUND),
		NWidget(WWT_EMPTY, COLOUR_BROWN, WID_CV_GRAPH), SetMinimalSize(576, 160), SetFill(1, 1),
	EndContainer(),
};

static WindowDesc _operating_profit_desc(
	WDP_AUTO, "graph_operating_profit", 0, 0,
	WC_OPERATING_PROFIT, WC_NONE,
	0,
	_nested_operating_profit_widgets, lengthof(_nested_operating_profit_widgets)
);


void ShowOperatingProfitGraph()
{
	AllocateWindowDescFront<OperatingProfitGraphWindow>(&_operating_profit_desc, 0);
}


/****************/
/* INCOME GRAPH */
/****************/

struct IncomeGraphWindow : BaseGraphWindow {
	IncomeGraphWindow(WindowDesc *desc, WindowNumber window_number) :
			BaseGraphWindow(desc, WID_CV_GRAPH, STR_JUST_CURRENCY_SHORT)
	{
		this->InitializeWindow(window_number);
	}

	OverflowSafeInt64 GetGraphData(const Company *c, int j) override
	{
		return c->old_economy[j].income;
	}
};

static const NWidgetPart _nested_income_graph_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_GRAPH_INCOME_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1-86: dropped WID_CV_KEY_BUTTON + SHADEBOX/DEFSIZEBOX/STICKYBOX/RESIZEBOX (see above).
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_CV_BACKGROUND),
		NWidget(WWT_EMPTY, COLOUR_BROWN, WID_CV_GRAPH), SetMinimalSize(576, 128), SetFill(1, 1),
	EndContainer(),
};

static WindowDesc _income_graph_desc(
	WDP_AUTO, "graph_income", 0, 0,
	WC_INCOME_GRAPH, WC_NONE,
	0,
	_nested_income_graph_widgets, lengthof(_nested_income_graph_widgets)
);

void ShowIncomeGraph()
{
	AllocateWindowDescFront<IncomeGraphWindow>(&_income_graph_desc, 0);
}
