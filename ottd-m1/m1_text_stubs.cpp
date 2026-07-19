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

// R1 text-rendering seam: the 4 symbols the real gfx_layout.cpp / fontcache.cpp /
// spritefontcache.cpp reference that nothing else in the link provides. Everything
// else they need is satisfied by the real font TUs + the already-linked gfx/sprite
// pipeline. (Only compiled into the R1 render-merge build.)
#include "stdafx.h"
#include "string_func.h"
#include "strings_func.h"
#include "openttd.h"

// Config is never persisted in the port.
bool _save_config = false;
void SaveToConfig() {}

// Our sprite font (from the base GRF) has the ASCII glyph range; report none missing
// so InitFontCache doesn't try to swap fonts. We KEEP this no-op even with the real
// string system linked: strings.cpp's real CheckForMissingGlyphs scans every langpack
// string (slow on PPC) and hits the no-fallback error path — so build.sh renames the
// real definition to CheckForMissingGlyphs_R1UNUSED and fontcache.cpp's call binds here.
void CheckForMissingGlyphs(bool, MissingGlyphSearcher *) {}

// Real UTF-8 decode (the layouter needs correct decoding) — copied verbatim from
// string.cpp so text lays out correctly rather than stubbing to a no-op.
size_t Utf8Decode(WChar *c, const char *s)
{
	if (!HasBit(s[0], 7)) {
		*c = s[0];
		return 1;
	} else if (GB(s[0], 5, 3) == 6) {
		if (IsUtf8Part(s[1])) {
			*c = GB(s[0], 0, 5) << 6 | GB(s[1], 0, 6);
			if (*c >= 0x80) return 2;
		}
	} else if (GB(s[0], 4, 4) == 14) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2])) {
			*c = GB(s[0], 0, 4) << 12 | GB(s[1], 0, 6) << 6 | GB(s[2], 0, 6);
			if (*c >= 0x800) return 3;
		}
	} else if (GB(s[0], 3, 5) == 30) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2]) && IsUtf8Part(s[3])) {
			*c = GB(s[0], 0, 3) << 18 | GB(s[1], 0, 6) << 12 | GB(s[2], 0, 6) << 6 | GB(s[3], 0, 6);
			if (*c >= 0x10000 && *c <= 0x10FFFF) return 4;
		}
	}
	*c = '?';
	return 1;
}

// Real UTF-8 encode (string.cpp's non-template overload lives in the un-linked string.cpp).
// Used by the town-name GetString shim to prepend the {WHITE}/{BLACK} colour control code.
size_t Utf8Encode(char *buf, WChar c)
{
	if (c < 0x80) {
		*buf = c;
		return 1;
	} else if (c < 0x800) {
		*buf++ = 0xC0 + GB(c, 6, 5);
		*buf   = 0x80 + GB(c, 0, 6);
		return 2;
	} else if (c < 0x10000) {
		*buf++ = 0xE0 + GB(c, 12, 4);
		*buf++ = 0x80 + GB(c, 6, 6);
		*buf   = 0x80 + GB(c, 0, 6);
		return 3;
	} else {
		*buf++ = 0xF0 + GB(c, 18, 3);
		*buf++ = 0x80 + GB(c, 12, 6);
		*buf++ = 0x80 + GB(c, 6, 6);
		*buf   = 0x80 + GB(c, 0, 6);
		return 4;
	}
}
