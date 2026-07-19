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
 * m1_viewport_stubs.cpp — link-only no-op stubs for the real viewport.cpp
 * (the game's renderer) in the R1 render-merge build.
 *
 * viewport.cpp is compiled for real so the grown town's tiles/sprites are laid
 * out and blitted on PPC. It drags ~two dozen references into the GUI/window,
 * vehicle-GUI, sign, link-graph and text-effect subsystems whose TUs are NOT
 * compiled. NONE of these run on the town-render draw path — the renderer walks
 * the map and emits ground/foundation sprites; it never opens a window, clicks a
 * vehicle, draws link-graph overlays, or shows a tooltip. The two exceptions
 * (ViewportAddVehicles / DrawTextEffects) ARE called every frame and are correct
 * as empty no-ops: the vehicle pool and the text-effect list are both empty, so
 * there is nothing to add/draw.
 *
 * Signatures mirror the real headers (included below) so the compiler type-checks
 * every stub for us. Globals / typeinfo storage go in m1_deadpools.c as raw
 * zeroed storage (unmangled or C-identifier symbol names).
 *
 * CRITICAL: this XCOFF ld SEGFAULTS on a duplicate ("multiple definition")
 * symbol. Everything defined here is provided by NO other object in the R1 link
 * (verified with nm against viewport.o + ottd-r1/obj + ottd-b1 + ottd-b2).
 */
#include "stdafx.h"

#include "vehicle_func.h"   /* ViewportAddVehicles */
#include "vehicle_gui.h"    /* ShowVehicleViewWindow, VehicleClicked, StartStopVehicle, CheckClickOnVehicle */
#include "texteff.hpp"      /* DrawTextEffects */
#include "window_func.h"    /* FindWindowById */
#include "window_gui.h"     /* FindWindowFromPt, DrawFrameRect, GuiShowTooltips, Window::SetDirty/SetWidgetDirty, WidgetDimensions */
#include "town.h"           /* ShowTownViewWindow */
#include "station_func.h"   /* ShowStationViewWindow */
#include "waypoint_func.h"  /* ShowWaypointWindow */
#include "viewport_func.h"  /* DoZoomInOutWindow, ScrollMainWindowTo */
#include "signs_func.h"     /* HandleClickOnSign */
#include "linkgraph/linkgraph_gui.h" /* LinkGraphOverlay::Draw */

/* ================================================================= *
 * ON the draw path — MUST be safe empty no-ops.                     *
 * Empty vehicle pool / empty text-effect list => nothing to draw.   *
 * ================================================================= */
/* ViewportAddVehicles is now REAL in r1_scene.cpp (draws the moving bus). */
void DrawTextEffects(DrawPixelInfo *) {}

/* ================================================================= *
 * OFF the draw path — return-defaults / no-ops (never run rendering *
 * a grown town: no window/vehicle/sign/tooltip interaction).        *
 * ================================================================= */

/* ---- window lookup ---- */
#ifndef R1_MERGE  /* real window.cpp provides these when the window system is linked */
Window *FindWindowById(WindowClass, WindowNumber) { return nullptr; }
Window *FindWindowFromPt(int, int) { return nullptr; }
#endif

/* ---- "open a GUI window" entry points ---- */
void ShowTownViewWindow(TownID) {}
void ShowStationViewWindow(StationID) {}
void ShowVehicleViewWindow(const Vehicle *) {}
void ShowWaypointWindow(const Waypoint *) {}

/* ---- viewport zoom / scroll ---- */
/* R1-61: real zoom (verbatim from main_gui.cpp, which isn't linked) so the canonical
 * toolbar's zoom-in/out buttons actually zoom the main viewport. w->viewport is a
 * ViewportData* (scrollpos_* live there, not on the Viewport base). */
bool DoZoomInOutWindow(ZoomStateChange how, Window *w)
{
	if (w == nullptr || w->viewport == nullptr) return false;
	ViewportData *vp = w->viewport;
	switch (how) {
		case ZOOM_NONE:
			break;
		case ZOOM_IN:
			if (vp->zoom <= _settings_client.gui.zoom_min) return false;
			vp->zoom = (ZoomLevel)((int)vp->zoom - 1);
			vp->virtual_width >>= 1;
			vp->virtual_height >>= 1;
			vp->scrollpos_x += vp->virtual_width >> 1;
			vp->scrollpos_y += vp->virtual_height >> 1;
			vp->dest_scrollpos_x = vp->scrollpos_x;
			vp->dest_scrollpos_y = vp->scrollpos_y;
			vp->follow_vehicle = INVALID_VEHICLE;
			break;
		case ZOOM_OUT:
			if (vp->zoom >= _settings_client.gui.zoom_max) return false;
			vp->zoom = (ZoomLevel)((int)vp->zoom + 1);
			vp->scrollpos_x -= vp->virtual_width >> 1;
			vp->scrollpos_y -= vp->virtual_height >> 1;
			vp->dest_scrollpos_x = vp->scrollpos_x;
			vp->dest_scrollpos_y = vp->scrollpos_y;
			vp->virtual_width <<= 1;
			vp->virtual_height <<= 1;
			vp->follow_vehicle = INVALID_VEHICLE;
			break;
	}
	vp->virtual_left = vp->scrollpos_x;
	vp->virtual_top  = vp->scrollpos_y;
	w->InvalidateData();
	MarkWholeScreenDirty();
	return true;
}
bool ScrollMainWindowTo(int, int, int, bool) { return false; }

/* ---- window-chrome drawing / tooltips ---- */
#ifndef R1_MERGE  /* real widget.cpp provides DrawFrameRect when linked */
void DrawFrameRect(int, int, int, int, Colours, FrameFlags) {}
#endif
void GuiShowTooltips(Window *, StringID, uint, const uint64 *, TooltipCloseCondition) {}

/* ---- click handlers (viewport hit-testing; off the render path) ---- */
void HandleClickOnSign(const Sign *) {}
Vehicle *CheckClickOnVehicle(const Viewport *, int, int) { return nullptr; }
bool VehicleClicked(const Vehicle *) { return false; }
void StartStopVehicle(const Vehicle *, bool) {}

/* ---- Window members (regular, non-virtual): out-of-line no-ops ---- */
#ifndef R1_MERGE  /* real window.cpp provides these members when linked */
void Window::SetDirty() const {}
void Window::SetWidgetDirty(byte) const {}
#endif

/* ---- link-graph overlay (non-virtual Draw): empty overlay ---- */
void LinkGraphOverlay::Draw(const DrawPixelInfo *) {}

/* ---- static data member storage: zero-initialized dimensions ---- */
WidgetDimensions WidgetDimensions::scaled = {};
