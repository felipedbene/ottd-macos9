/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/smallmap_gui.cpp (the SmallMapWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1-87: the SMALLMAP (minimap) window, extracted M1-style (mirrors m1_town_directory_gui.cpp).
// The canonical toolbar's "map of the world" button opens the real overview map so the player
// can see our live-growing towns / roads / water from above — instead of the no-op ShowSmallMap
// stub in m1_toolbar_stubs.cpp.
//
// The stock SmallMapWindow (src/smallmap_gui.cpp / smallmap_gui.h) is enormous: it drags in the
// LinkGraphOverlay, GUITimer, seven legend tables, the heightmap colour schemes, per-tile
// industry/vehicle/link-graph overlays, isometric remapping, and eight mode buttons — a cascade
// of TUs that are NOT compiled in this port. So this TU does NOT reuse the stock class. Instead
// it defines a lean, self-contained SmallMapWindow that draws a recognizable top-down minimap by
// scanning the REAL map with GetTileType() and colouring each tile (water=blue, clear=green,
// trees=dark-green, houses=grey, road=dark, rail=black, station=red, industry=dark-red). The
// legend/overlay/mode-button machinery is dropped entirely — the MAP draws, which is the point.
//
// TITLE-BAR rule (R1-84): keep ONLY WWT_CLOSEBOX + WWT_CAPTION; the SHADEBOX/DEFSIZEBOX/STICKYBOX/
// RESIZEBOX sprites live in the un-loaded extra GRF and would render as the missing-sprite "?".

#include "stdafx.h"
#include "window_gui.h"
#include "window_func.h"
#include "strings_func.h"
#include "gfx_func.h"                    /* GfxFillRect + PC_* palette colours */
#include "map_func.h"                    /* MapSizeX/MapSizeY, TileXY */
#include "tile_map.h"                    /* GetTileType, TileType */
#include "widgets/smallmap_widget.h"     /* WID_SM_CAPTION, WID_SM_MAP, WID_SM_MAP_BORDER */
#include "table/strings.h"

#include "safeguards.h"

// ----------------------------------------------------------------------------
// Simplified title-bar + map panel (stock tree at smallmap_gui.cpp:1834, with the whole legend
// bar + button rows + ?-boxes removed; only the closebox, caption, and the map panel survive).
// ----------------------------------------------------------------------------
static const NWidgetPart _nested_smallmap_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_SM_CAPTION), SetDataTip(STR_SMALLMAP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1-84: dropped SHADEBOX/DEFSIZEBOX/STICKYBOX — their GUI sprites aren't in the loaded
		// base GRF, so they rendered as the missing-sprite "?". Closebox + caption stay.
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_SM_MAP_BORDER),
		NWidget(WWT_INSET, COLOUR_BROWN, WID_SM_MAP), SetMinimalSize(346, 160), SetPadding(2, 2, 2, 2), EndContainer(),
	EndContainer(),
};

/**
 * Class managing the smallmap window.
 *
 * NOTE: this is a DELIBERATELY reduced re-implementation. The stock SmallMapWindow carries state
 * for seven map modes, a link-graph overlay, zoom, scrolling and blinking industry highlights;
 * none of that is present here. What remains is a single fixed top-down "contour-ish" view of the
 * whole map, redrawn on every paint.
 */
struct SmallMapWindow : public Window {
	SmallMapWindow(WindowDesc *desc, int window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_SM_CAPTION) {
			/* The caption reads "Contours" (STR_SMALLMAP_TYPE_CONTOURS is the first of the mode
			 * sub-strings); we only ever show the contour-style view. */
			SetDParam(0, STR_SMALLMAP_TYPE_CONTOURS);
		}
	}

	/** Pick a palette colour for a tile, keyed only on its coarse type. */
	static uint8 ColourForTile(TileIndex tile)
	{
		switch (GetTileType(tile)) {
			case MP_WATER:        return PC_WATER;        // blue
			case MP_CLEAR:        return PC_GRASS_LAND;   // green
			case MP_TREES:        return PC_GREEN;        // dark green
			case MP_HOUSE:        return PC_GREY;         // town buildings
			case MP_ROAD:         return PC_DARK_GREY;    // roads (dark)
			case MP_RAILWAY:      return PC_BLACK;        // rail
			case MP_STATION:      return PC_RED;          // stations
			case MP_INDUSTRY:     return PC_DARK_RED;     // industries
			case MP_TUNNELBRIDGE: return PC_GREY;
			case MP_OBJECT:       return PC_DARK_RED;
			default:              return PC_BLACK;        // MP_VOID and anything else
		}
	}

	/**
	 * Draw the minimap into the map panel rectangle. Each output pixel is sampled from the tile at
	 * the proportional position in the world — a straight top-down (non-isometric) scaling. This is
	 * a simplification of the stock isometric DrawSmallMap()/DrawSmallMapColumn() column scan.
	 */
	void DrawMinimap(const Rect &r) const
	{
		int w = r.Width();
		int h = r.Height();
		if (w <= 0 || h <= 0) return;

		uint msx = MapSizeX();
		uint msy = MapSizeY();

		for (int py = 0; py < h; py++) {
			uint ty = (uint)py * msy / (uint)h;
			if (ty >= msy) ty = msy - 1;
			int abs_y = r.top + py;
			for (int px = 0; px < w; px++) {
				uint tx = (uint)px * msx / (uint)w;
				if (tx >= msx) tx = msx - 1;
				uint8 colour = ColourForTile(TileXY(tx, ty));
				int abs_x = r.left + px;
				GfxFillRect(abs_x, abs_y, abs_x, abs_y, colour);
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget == WID_SM_MAP) this->DrawMinimap(r);
	}
};

static WindowDesc _smallmap_desc(
	WDP_AUTO, "smallmap", 484, 314,
	WC_SMALLMAP, WC_NONE,
	0,
	_nested_smallmap_widgets, lengthof(_nested_smallmap_widgets)
);

/**
 * Show the smallmap window. (Replaces the no-op stub in m1_toolbar_stubs.cpp.)
 */
void ShowSmallMap()
{
	AllocateWindowDescFront<SmallMapWindow>(&_smallmap_desc, 0);
}
