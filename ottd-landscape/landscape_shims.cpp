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

// Consolidated link-shim TU for the integrated landscape demo: the union of the
// GRF-decode shims and the map-subsystem shims (deduplicated). The only real
// substitution is FioFOpenFile -> fopen(); the rest are no-op error/settings
// stubs plus the globals the real OpenTTD sources reference.
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
    if (f && filesize) { long c = ftell(f); fseek(f, 0, SEEK_END); *filesize = (size_t)ftell(f); fseek(f, c, SEEK_SET); }
    return f;
}

// --- error/reporting stubs (only fire on corrupt/missing data) ---
void NORETURN CDECL usererror(const char *str, ...) { va_list ap; va_start(ap, str); vfprintf(stderr, str, ap); va_end(ap); abort(); }
void NORETURN CDECL error(const char *str, ...)     { va_list ap; va_start(ap, str); vfprintf(stderr, str, ap); va_end(ap); abort(); }
void MallocError(size_t) { abort(); }
void ShowErrorMessage(StringID, StringID, WarningLevel, int, int, const GRFFile *, uint, const uint32 *) {}
void SetDParamStr(uint, const std::string &) {}
bool strtolower(std::string &str, std::string::size_type offs)
{
    for (; offs < str.size(); offs++) str[offs] = (char)tolower((unsigned char)str[offs]);
    return true;
}

// --- globals referenced by the real map + GRF sources ---
int _debug_sprite_level = 0;
int _debug_map_level = 0;
extern const byte _palmap_w2d[256] = {0};       // unused (DOS-palette GRF, no remap)
ClientSettings _settings_client;                 // GRF loader reads sprite_zoom_min (0 = fine)
GameSettings   _settings_game;                   // map subsystem; zero-init is fine

// OpenTTD's reusable sprite-decode scratch buffer.
ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_COUNT];
