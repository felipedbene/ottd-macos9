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
 * m1_window_stubs.cpp — link-only no-op stubs for the real window.cpp + widget.cpp
 * in the R1 render-merge build.
 *
 * window.cpp (clock-patched) + widget.cpp are compiled for real so OpenTTD's own
 * window manager / widget tree can drive the on-screen GUI on PPC. They drag in a
 * grab-bag of GUI subsystems whose TUs are NOT compiled: the console, the network
 * chat box, the on-screen query/edit box (QueryString/Textbuf), news, hotkeys,
 * the depot-window block sizes, the framerate performance measurers, the .ini
 * window-settings (de)serialiser and the critical-error dialog. NONE of these run
 * on the town-render / basic-window path (no console open, no chat, no text entry,
 * no depot window, no ini load, no news, no error popup), so each is a safe no-op
 * returning a "nothing happened" default. The two trivial geometry/math helpers
 * (maxdim / LeastCommonMultiple) ARE on the widget-sizing path, so they are
 * implemented for real (returning 0/garbage there could break layout math).
 *
 * Signatures mirror the real headers (included below) so the compiler type-checks
 * every stub. Globals go in m1_deadpools.c as raw zeroed storage.
 *
 * CRITICAL: this XCOFF ld SEGFAULTS on a duplicate ("multiple definition") symbol.
 * Everything defined here is provided by NO other object in the R1 link (verified
 * with nm against window.o + widget.o + ottd-r1/obj + ottd-b1 + ottd-b2). The
 * strong defs window.cpp/widget.cpp themselves REPLACE (UpdateWindows, DrawFrameRect,
 * FindWindowById, ...) are handled at integration time, NOT here.
 */
#include "stdafx.h"

#include "console_func.h"        /* IConsoleClose */
#include "console_gui.h"         /* IConsoleResize */
#include "error.h"               /* ShowFirstError, UnshowCriticalError */
#include "texteff.hpp"           /* MoveAllTextEffects */
#include "hotkeys.h"             /* HandleGlobalHotkeys, HotkeyList::CheckMatch */
#include "news_func.h"           /* InitNewsItemStructs */
#include "core/math_func.hpp"    /* LeastCommonMultiple */
#include "core/geometry_func.hpp"/* maxdim */
#include "settings_func.h"       /* IniLoadWindowSettings, IniSaveWindowSettings */
#include "network/network_func.h"/* NetworkChatMessageLoop */
#include "network/network.h"     /* NetworkDrawChatMessage, NetworkReInitChatBoxSize */
#include "framerate_type.h"      /* PerformanceMeasurer, PerformanceAccumulator */
#include "querystring_gui.h"     /* QueryString (pulls textbuf_type.h) */
#include "ini_type.h"            /* IniFile, IniLoadFile */

/* ================================================================= *
 * ON the widget-sizing path — implemented for real (pure helpers).  *
 * ================================================================= */

/* R1-59: the widgets/dropdown.cpp shrink helpers reference this static (defined in the
 * un-compiled core/geometry_func.cpp, which would dup maxdim below). Just provide it. */
const RectPadding RectPadding::zero = {0, 0, 0, 0};

/* max of two Dimensions, componentwise (widget "smallest size" reduction). */
Dimension maxdim(const Dimension &d1, const Dimension &d2)
{
	Dimension d;
	d.width  = d1.width  > d2.width  ? d1.width  : d2.width;
	d.height = d1.height > d2.height ? d1.height : d2.height;
	return d;
}

/* LCM used to compute the resize step; 0 return would risk a later divide-by-0. */
int LeastCommonMultiple(int a, int b)
{
	if (a == 0 || b == 0) return 0;
	int x = a < 0 ? -a : a;
	int y = b < 0 ? -b : b;
	while (y != 0) { int t = x % y; x = y; y = t; } /* x = gcd(a,b) */
	return a * b / x;
}

/* ================================================================= *
 * OFF the runtime path — no-ops / return "nothing happened".         *
 * ================================================================= */

/* ---- console ---- */
void IConsoleClose() {}
void IConsoleResize(Window *) {}

/* ---- critical-error dialog ---- */
void ShowFirstError() {}
void UnshowCriticalError() {}

/* ---- text effects (floating cost labels): empty list, nothing to move ---- */
void MoveAllTextEffects(uint) {}

/* ---- hotkeys ---- */
void HandleGlobalHotkeys(WChar, uint16) {}
int HotkeyList::CheckMatch(uint16, bool) const { return -1; } /* -1 == no hotkey matched */

/* ---- news ---- */
void InitNewsItemStructs() {}

/* ---- depot window ---- */
void InitDepotWindowBlockSizes() {}

/* ---- network chat box (no networking) ---- */
void NetworkChatMessageLoop() {}
void NetworkDrawChatMessage() {}
void NetworkReInitChatBoxSize() {}

/* ---- framerate performance measurers (RAII no-ops) ---- */
PerformanceMeasurer::PerformanceMeasurer(PerformanceElement) {}
PerformanceMeasurer::~PerformanceMeasurer() {}
void PerformanceAccumulator::Reset(PerformanceElement) {}
void ProcessPendingPerformanceMeasurements(); /* local extern in window.cpp */
void ProcessPendingPerformanceMeasurements() {}

/* ---- .ini window-settings (de)serialise: no config file on the render path ---- */
void IniLoadWindowSettings(IniFile &, const char *, void *) {}
void IniSaveWindowSettings(IniFile &, const char *, void *) {}

/* ---- QueryString / Textbuf (on-screen edit box; no text entry) ---- */
void QueryString::DrawEditBox(const Window *, int) const {}
void QueryString::ClickEditBox(Window *, Point, int, int, bool) {}
void QueryString::HandleEditBox(Window *, int) {}
Point QueryString::GetCaretPosition(const Window *, int) const { Point pt = {0, 0}; return pt; }
Rect QueryString::GetBoundingRect(const Window *, int, const char *, const char *) const { Rect r = {0, 0, 0, 0}; return r; }
const char *QueryString::GetCharAtPosition(const Window *, int, const Point &) const { return nullptr; }

void Textbuf::UpdateSize() {}
bool Textbuf::InsertString(const char *, bool, const char *, const char *, const char *) { return false; }
HandleKeyPressResult Textbuf::HandleKeyPress(WChar, uint16) { return HKPR_NOT_HANDLED; }
void Textbuf::DeleteAll() {}

/* ---- IniFile / IniLoadFile: no config load/save on the render path.
 *      Defining IniFile's virtual overrides (OpenFile/ReportFileError) makes the
 *      compiler emit `vtable for IniFile` (_ZTV7IniFile) here; the abstract-base
 *      ~IniLoadFile emits `vtable for IniLoadFile`. The base ctor/dtor are also
 *      stubbed so the derived ctor has something to chain to. ---- */
IniLoadFile::IniLoadFile(const char * const *, const char * const *) {}
IniLoadFile::~IniLoadFile() {}
void IniLoadFile::LoadFromDisk(const std::string &, Subdirectory) {}

IniFile::IniFile(const char * const *list_group_names) : IniLoadFile(list_group_names) {}
bool IniFile::SaveToDisk(const std::string &) { return false; }
FILE *IniFile::OpenFile(const std::string &, Subdirectory, size_t *) { return nullptr; }
void IniFile::ReportFileError(const char * const, const char * const, const char * const) {}
