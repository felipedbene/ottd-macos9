// B1 link shims: satisfy the text/window/error/global surface that OpenTTD's real
// spritecache.cpp + gfx.cpp reference but that never executes on a plain ground blit.
// The only real seam is FioFOpenFile -> fopen. Everything else is a no-op/abort stub
// or a zero-init global. If any abort() fires, the trace shows we hit an unexpected path.
#include "stdafx.h"
#include "fileio_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "error.h"
#include "settings_type.h"
#include "gfx_func.h"
#include "gfx_layout.h"
#include "fontcache.h"
#include "window_gui.h"
#include "newgrf_debug.h"
#include "core/alloc_func.hpp"
#ifdef R1_MERGE
#include "town.h"                  // Town::GetIfValid, TownID
#include "townname_func.h"         // GenerateTownNameString (no language pack needed)
#include "table/strings.h"         // STR_VIEWPORT_TOWN* ids
#include "table/control_codes.h"   // SCC_WHITE/SCC_BLACK colour control codes
#endif
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <memory>

extern "C" void ottd_log(const char *fmt, ...);

// --- the one real seam ---
FILE *FioFOpenFile(const std::string &filename, const char *mode, Subdirectory, size_t *filesize)
{
    FILE *f = fopen(filename.c_str(), mode);
    if (f && filesize) { long c = ftell(f); fseek(f, 0, SEEK_END); *filesize = (size_t)ftell(f); fseek(f, c, SEEK_SET); }
    return f;
}

#ifdef R1_STRINGS
// R1-57: ReadLanguagePack loads the .lng through this (real fileio.cpp not compiled).
// Verbatim behaviour, minus the FileCloser RAII helper.
std::unique_ptr<char[]> ReadFileToMem(const std::string &filename, size_t &lenp, size_t maxsize)
{
    FILE *in = fopen(filename.c_str(), "rb");
    if (in == nullptr) return nullptr;
    fseek(in, 0, SEEK_END);
    size_t len = (size_t)ftell(in);
    fseek(in, 0, SEEK_SET);
    if (len > maxsize) { fclose(in); return nullptr; }
    std::unique_ptr<char[]> mem = std::make_unique<char[]>(len + 1);
    mem.get()[len] = 0;
    if (fread(mem.get(), len, 1, in) != 1) { fclose(in); return nullptr; }
    fclose(in);
    lenp = len;
    return mem;
}
#endif

// --- fatal stubs (should never fire for a ground blit) ---
void NORETURN CDECL usererror(const char *s, ...)
{
	/* stderr goes nowhere on classic Mac: push the message to the trace sink
	 * BEFORE aborting, or every usererror is a silent Type-2. */
	char buf[256]; va_list a; va_start(a, s); vsnprintf(buf, sizeof(buf), s, a); va_end(a);
	ottd_log("USERERROR: %s", buf);
	abort();
}
void NORETURN CDECL error(const char *s, ...)
{
	char buf[256]; va_list a; va_start(a, s); vsnprintf(buf, sizeof(buf), s, a); va_end(a);
	ottd_log("ERROR: %s", buf);
	abort();
}
void MallocError(size_t)  { abort(); }
void ReallocError(size_t) { abort(); }

// --- misc referenced helpers ---
void DebugPrint(const char *level, const std::string &msg) { ottd_log("dbg %s: %s", level ? level : "?", msg.c_str()); }
#ifndef R1_STRINGS  /* the real strings.cpp owns SetDParamStr when the string system is linked */
void SetDParamStr(uint, const std::string &) {}
#endif
bool strtolower(std::string &str, std::string::size_type offs) { for (; offs < str.size(); offs++) str[offs] = (char)tolower((unsigned char)str[offs]); return true; }

// --- error-message subsystem (never reached) ---
void ShowErrorMessage(StringID, StringID, WarningLevel, int, int, const GRFFile *, uint, const uint32 *) {}
void ScheduleErrorMessage(const ErrorMessageData &) {}
ErrorMessageData::ErrorMessageData(StringID, StringID, uint, int, int, const GRFFile *, uint, const uint32 *) {}
ErrorMessageData::~ErrorMessageData() {}
void ErrorMessageData::SetDParam(uint, uint64) {}

// --- text/font/window subsystem (never reached on a ground blit) ---
#ifndef R1_MERGE  /* the REAL gfx_layout.cpp/fontcache.cpp provide these in the render-merge build (text rendering) */
Layouter::Layouter(const char *, int, TextColour, FontSize) {}
Dimension Layouter::GetBounds() { return Dimension{0, 0}; }
Point Layouter::GetCharPosition(const char *) const { return Point{0, 0}; }
const char *Layouter::GetCharAtPosition(int) const { return nullptr; }
int GetCharacterHeight(FontSize) { return 0; }
#endif
#ifndef R1_STRINGS   /* the real strings.cpp GetString(char*,StringID,const char*) takes over — town
                        names then render via the langpack's "{WHITE}{TOWN}" -> SCC_TOWN_NAME -> GetTownName */
#ifdef R1_MERGE
/* R1-51: town-name signs WITHOUT a language pack. The real viewport sign path
 * (viewport.cpp ViewportSign::UpdatePosition for width + ViewportDrawStrings for render)
 * funnels every town label through THIS overload with the town index in DParam(0). No .lng
 * is loaded so we can't format the "{WHITE}{TOWN}" wrapper — but the town-name GENERATOR
 * (GenerateTownNameString, townname.cpp) builds names from compiled C tables. So special-
 * case the four town StringIDs (0x8818..0x881B) -> the real generated name; every other
 * StringID stays empty (unchanged). This lights up town names on the map. */
char *GetString(char *buffr, StringID string, const char *last)
{
    if (string >= STR_VIEWPORT_TOWN_POP && string <= STR_VIEWPORT_TOWN_TINY_WHITE) {
        const Town *t = Town::GetIfValid((TownID)GetDParam(0));
        if (t != nullptr) {
            /* Prepend the colour control code the real strings carry ("{WHITE}{TOWN}"),
             * else the label draws in ViewportDrawStrings' default TC_BLACK (invisible-ish
             * on the map). TINY_BLACK is the drop-shadow layer -> black; the rest -> white.
             * The layouter (gfx_layout.cpp) consumes SCC_BLUE..SCC_BLACK as the text colour
             * and the width calc skips it, so geometry stays correct. */
            buffr += Utf8Encode(buffr, string == STR_VIEWPORT_TOWN_TINY_BLACK ? SCC_BLACK : SCC_WHITE);
            return GenerateTownNameString(buffr, last,
                       _settings_game.game_creation.town_name, t->townnameparts);
        }
    }
    if (buffr < last) *buffr = '\0';
    return buffr;
}
#else
char *GetString(char *buffr, StringID, const char *) { return buffr; }
#endif
#endif  /* R1_STRINGS */

#ifdef R1_STRINGS
/* --- string helpers strings.cpp needs (string.cpp is NOT compiled — it would dup
 *     Utf8Decode/Encode/strecpy/seprintf). StrValid must be correct: LanguagePackHeader::
 *     IsValid() runs it on every header field, so a wrong impl fails the langpack load. --- */
char *strecat(char *dst, const char *src, const char *last)
{
    while (*dst != '\0') { if (dst == last) return dst; dst++; }
    while ((*dst = *src++) != '\0') { if (dst == last) { *dst = '\0'; break; } dst++; }
    return dst;
}
char *stredup(const char *s, const char *last)
{
    size_t len = 0;
    while (s[len] != '\0' && (last == nullptr || s + len < last + 1)) len++;
    char *tmp = (char *)malloc(len + 1);
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    return tmp;
}
/* Verbatim from string.cpp — the langpack header validation depends on this being correct. */
bool StrValid(const char *str, const char *last)
{
    while (str <= last && *str != '\0') {
        size_t len = Utf8EncodedCharLen(*str);
        if (len == 0 || str + len > last) return false;
        WChar c;
        len = Utf8Decode(&c, str);
        if (!IsPrintable(c) || (c >= SCC_SPRITE_START && c <= SCC_SPRITE_END)) return false;
        str += len;
    }
    return *str == '\0';
}
int strnatcmp(const char *s1, const char *s2, bool) { return strcmp(s1, s2); }

/* _settings_newgame: only reached via GetGameSettings() on SCC_CURRENCY_* (never in captions). */
GameSettings _settings_newgame;
#endif  /* R1_STRINGS */
#ifndef R1_MERGE  /* real window.cpp provides these when the window system is linked */
void DrawOverlappedWindowForAll(int, int, int, int) {}
void ReInitAllWindows(bool) {}
#endif
void UpdateAllVirtCoords() {}
void NetworkUndrawChatMessage() {}

// --- globals referenced by the real sources ---
FontCache *FontCache::caches[FS_END] = {};
WindowList _z_windows;
std::vector<Dimension> _resolutions;
NewGrfDebugSpritePicker _newgrf_debug_sprite_picker;
#ifndef R1_STRINGS  /* real strings.cpp owns _current_text_dir when the string system is linked */
TextDirection _current_text_dir = TD_LTR;
#endif
uint _dirty_block_colour = 0;
bool _networking = false;
int _debug_sprite_level = 0, _debug_map_level = 0, _debug_driver_level = 0;
ClientSettings _settings_client;
GameSettings   _settings_game;

// The real grass slope->sprite offset table (src/landscape.cpp:79), verbatim.
extern const byte _slope_to_sprite_offset[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0,
    0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 17, 0, 15, 18, 0,
};
