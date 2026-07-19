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

// B1: render the landscape through OpenTTD's REAL pipeline.
//   OpenCachedSpriteFile + ReadGRFSpriteOffsets + LoadNextSprite  (real sprite registration)
//   GetSprite  -> real decode + ResizeSprites + PadSprites + Encode
//   DrawSpriteViewport -> real GfxMainBlitterViewport blit into _cur_dpi
// No from-scratch geometry: tiles are placed by OpenTTD's own RemapCoords and drawn
// by OpenTTD's own viewport blitter, so slopes seam exactly like the real game.
#include "stdafx.h"
#include "gfx_func.h"
#include "gfx_type.h"
#include "spritecache.h"
#include "blitter/factory.hpp"
#include "landscape.h"
#include "slope_func.h"
#include "table/sprites.h"
#include "zoom_type.h"
#include "fileio_type.h"
#include <cstring>

extern "C" void ottd_log(const char *fmt, ...);
extern DrawPixelInfo *_cur_dpi;

// OpenTTD's real DOS palette for the Mac CopyBits CTab.
#include "table/palettes.h"
extern "C" void ottd_get_palette(unsigned char *rgb)
{
    for (int i = 0; i < 256; i++) {
        rgb[i*3+0] = _palette.palette[i].r;
        rgb[i*3+1] = _palette.palette[i].g;
        rgb[i*3+2] = _palette.palette[i].b;
    }
}

// Copy of gfxinit.cpp's LoadGrfFile (it's static there); uses only public spritecache APIs.
static uint LoadGrfFileB1(const char *filename, uint load_index, bool needs_palette_remap)
{
    uint sprite_id = 0;
    SpriteFile &file = OpenCachedSpriteFile(filename, BASESET_DIR, needs_palette_remap);
    byte cv = file.GetContainerVersion();
    ottd_log("B1: grf '%s' container=%d", filename, (int)cv);
    if (cv == 0) return 0;
    ottd_log("B1: calling ReadGRFSpriteOffsets...");
    ReadGRFSpriteOffsets(file);
    ottd_log("B1: ReadGRFSpriteOffsets done");
    if (cv >= 2) { byte comp = file.ReadByte(); if (comp != 0) { ottd_log("B1: bad compression"); return 0; } }
    ottd_log("B1: entering LoadNextSprite loop");
    while (true) {
        // crash window last seen: id 768..896 — log every sprite there, sparse elsewhere
        if ((sprite_id >= 768 && sprite_id <= 900) || (sprite_id & 0x7F) == 0)
            ottd_log("B1:   loop at id=%u pos=%lu", sprite_id, (unsigned long)file.GetPos());
        bool more = LoadNextSprite((int)load_index, file, sprite_id);
        if (!more) break;
        load_index++; sprite_id++;
    }
    ottd_log("B1: registered up to sprite %u", load_index);
    return load_index;
}

// gentle smooth heightmap -> mostly flat + single-corner slopes (no all-steep)
static int slope_from(int hn, int hw, int he, int hs, int *base)
{
    int hmin = hn; if (hw<hmin)hmin=hw; if (he<hmin)hmin=he; if (hs<hmin)hmin=hs;
    int hmax = hn; if (hw>hmax)hmax=hw; if (he>hmax)hmax=he; if (hs>hmax)hmax=hs;
    *base = hmin; int r = 0;
    if (hn!=hmin) r|=SLOPE_N; if (hw!=hmin) r|=SLOPE_W;
    if (he!=hmin) r|=SLOPE_E; if (hs!=hmin) r|=SLOPE_S;
    if (hmax-hmin==2) r|=SLOPE_STEEP;
    return r;
}

extern uint _sprite_cache_size;   // MB; default 4 -> ~6MB alloc. Trim for the 16MB partition.

extern "C" int b1_render(unsigned char *fb, int pitch, int w, int h)
{
    _sprite_cache_size = 2;
    ottd_log("B1 render begin %dx%d (pitch=%d)", w, h, pitch);
    std::memset(fb, 0xC9, (size_t)pitch * h);           // sky

    Blitter *bl = BlitterFactory::SelectBlitter("8bpp-simple");   // sets the ACTIVE blitter
    if (bl == nullptr) { ottd_log("B1: SelectBlitter failed -> abort"); return -3; }

    // CRITICAL (root cause of the intermittent Type-2s): allocate the sprite-cache pool.
    // Without this _spritecache_ptr is NULL, and on classic Mac address 0 is mapped low
    // memory — AllocSprite (first hit: recolour sprites ~id 775) happily WALKS AND WRITES
    // low-memory system globals instead of faulting. gfxinit.cpp calls this before loading.
    GfxInitSpriteMem();
    ottd_log("B1: sprite cache pool up (%u MB)", _sprite_cache_size);

    DrawPixelInfo dpi;
    dpi.dst_ptr = fb; dpi.pitch = pitch; dpi.width = w; dpi.height = h;
    dpi.zoom = ZOOM_LVL_OUT_4X;
    dpi.left = 0; dpi.top = 0;
    _cur_dpi = &dpi;

    if (LoadGrfFileB1("ogfx1_base.grf", 0, false) == 0) { ottd_log("B1: load failed"); return -1; }

    // smooth dome, 12x12 corners -> 11x11 tiles
    const int N = 12;
    static unsigned char H[N*N];
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        int d = (i-6>0?i-6:6-i) + (j-6>0?j-6:6-j);
        int th = 4 - d/3; if (th < 0) th = 0; H[j*N+i] = (unsigned char)th;
    }

    // Origin so the field centres: RemapCoords is at ZOOM_LVL_BASE scale; dpi->left is
    // subtracted at that scale. Shift the view by setting dpi.left/top.
    dpi.left = -(w/2) * ZOOM_LVL_BASE;
    dpi.top  = -(40) * ZOOM_LVL_BASE;

    int drawn = 0;
    for (int ty = 0; ty < N-1; ty++) {
        for (int tx = 0; tx < N-1; tx++) {
            int base;
            int slope = slope_from(H[ty*N+tx], H[ty*N+tx+1], H[(ty+1)*N+tx], H[(ty+1)*N+tx+1], &base);
            SpriteID sid = SPR_FLAT_GRASS_TILE + SlopeToSpriteOffset((Slope)slope);
            Point pt = RemapCoords(tx * TILE_SIZE, ty * TILE_SIZE, base * TILE_HEIGHT);
            DrawSpriteViewport(sid, PAL_NONE, pt.x, pt.y, nullptr);
            drawn++;
        }
    }
    ottd_log("B1: drew %d tiles via real pipeline", drawn);
    _cur_dpi = nullptr;
    return drawn;
}
