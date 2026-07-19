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
