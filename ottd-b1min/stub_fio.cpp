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

// Bisect build B: replaces spritecache.o with trivial (no dynamic-init) stubs.
// Functions are never executed (minimal main never calls b1_render's body); they only
// need to link. If the app now reaches main() and writes its trace, spritecache.o's
// static init was the crash.
#include "stdafx.h"
#include "spritecache.h"
#include "spriteloader/spriteloader.hpp"

uint _sprite_cache_size = 2;                         // POD
extern const byte _palmap_w2d[256] = {};             // const -> needs extern for external linkage

// spritecache.cpp normally defines this static member array (ReusableBuffer has a trivial
// ctor, so this contributes only benign init identical to what spritecache did).
ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_COUNT];

uint  GetMaxSpriteID() { return 0; }
void *GetRawSprite(SpriteID, SpriteType, AllocatorProc *, SpriteEncoder *) { return nullptr; }
void  GfxClearSpriteCache() {}
bool  LoadNextSprite(int, SpriteFile &, uint) { return false; }
void  ReadGRFSpriteOffsets(SpriteFile &) {}
SpriteFile &OpenCachedSpriteFile(const std::string &, Subdirectory, bool)
{
    SpriteFile *p = nullptr; return *p;              // never called; links only
}
