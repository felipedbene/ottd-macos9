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

// Minimal environment so OpenTTD's REAL GRF decode sources (random_access_file.cpp,
// spriteloader/sprite_file.cpp, spriteloader/grf.cpp) link and run standalone on
// Mac OS 9. The only real substitution is FioFOpenFile -> plain fopen(); everything
// else is a tiny stub for error/settings machinery the decoder never meaningfully hits.
#include "stdafx.h"
#include "fileio_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "error.h"
#include "settings_type.h"
#include "spriteloader/spriteloader.hpp"
#include "core/alloc_func.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

// --- the one genuine seam: open a GRF straight off the Mac disk by name ---
FILE *FioFOpenFile(const std::string &filename, const char *mode, Subdirectory, size_t *filesize)
{
    FILE *f = fopen(filename.c_str(), mode);
    if (f != nullptr && filesize != nullptr) {
        long cur = ftell(f);
        fseek(f, 0, SEEK_END);
        *filesize = (size_t)ftell(f);
        fseek(f, cur, SEEK_SET);
    }
    return f;
}

// --- error/reporting stubs (only fire on corrupt/missing data, which we pre-check) ---
void NORETURN CDECL usererror(const char *str, ...)
{
    va_list ap; va_start(ap, str); vfprintf(stderr, str, ap); va_end(ap);
    abort();
}
void MallocError(size_t) { abort(); }
void ShowErrorMessage(StringID, StringID, WarningLevel, int, int, const GRFFile *, uint, const uint32 *) {}
void SetDParamStr(uint, const std::string &) {}

bool strtolower(std::string &str, std::string::size_type offs)
{
    for (; offs < str.size(); offs++) str[offs] = (char)tolower((unsigned char)str[offs]);
    return true;
}

// --- globals the decode path references ---
int _debug_sprite_level = 0;
extern const byte _palmap_w2d[256] = {0};       // extern: const has internal linkage by default in C++
ClientSettings _settings_client;                // zero-init -> sprite_zoom_min = ZOOM_LVL_NORMAL

// The reusable scratch buffer OpenTTD's Sprite::AllocateData draws from.
ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_COUNT];
