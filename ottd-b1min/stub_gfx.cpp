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

// Bisect build A: replaces gfx.o with trivial (POD, no dynamic-init) stubs.
// If the app now reaches main() and writes its trace, gfx.o's static init was the crash.
#include "stdafx.h"
#include "gfx_type.h"
#include "gfx_func.h"
#include "zoom_type.h"

DrawPixelInfo  _screen;             // POD, zero-init  (no ctor -> no static init)
DrawPixelInfo *_cur_dpi = nullptr;
ZoomLevel      _gui_zoom = ZOOM_LVL_OUT_4X;

void DrawSpriteViewport(SpriteID, PaletteID, int, int, const SubSprite *) {}
