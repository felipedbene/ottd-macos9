/*
 * m1_strings_stubs.cpp — off-path surface the REAL strings.cpp (R1-57 string system)
 * references but which basic window captions / dropdown menu text never reach. All are
 * no-op / return-empty: they are only hit via NewGRF-string control codes, GameScript
 * strings, industry/cargo/engine name codes, or ReadLanguagePack's post-load rebuilds —
 * none of which our built-in (grfid 0) content emits. Exact signatures from the headers
 * so the mangled names bind. See the string-system scope note in the render-merge memory.
 */
#include "stdafx.h"
#include "newgrf_text.h"
#include "game/game_text.hpp"
#include "industrytype.h"
#include "smallmap_gui.h"
#include "cargotype.h"
#include "network/network_content_gui.h"
#include "newgrf_engine.h"
#include "fileio_func.h"
#include "engine_base.h"

#include "safeguards.h"

/* --- NewGRF text stack (newgrf_text.h) — only via NewGRF-string tabs --- */
void SetCurrentGrfLangID(byte) {}
const char *GetGRFStringPtr(uint16) { return ""; }
bool UsingNewGRFTextStack() { return false; }
void StartTextRefStackUsage(const struct GRFFile *, byte, const uint32 *) {}
void StopTextRefStackUsage() {}
struct TextRefStack *CreateTextRefStackBackup() { return nullptr; }
void RestoreTextRefStackBackup(struct TextRefStack *) {}
uint RemapNewGRFStringControlCode(uint, char *, char **, const char **, int64 *, uint, bool) { return 0; }

/* --- GameScript strings (game/game_text.hpp) — TEXT_TAB_GAMESCRIPT only --- */
const char *GetGameStringPtr(uint) { return ""; }
void ReconsiderGameScriptLanguage() {}

/* --- industry / cargo (called by ReadLanguagePack rebuilds + SCC_INDUSTRY/CARGO codes) --- */
const IndustrySpec *GetIndustrySpec(IndustryType) { return nullptr; }
void SortIndustryTypes() {}
void BuildIndustriesLegend() {}
void InitializeSortedCargoSpecs() {}
void BuildContentTypeStringList() {}

/* --- vehicle (newgrf_engine.h) — SCC_ENGINE_NAME only --- */
uint16 GetVehicleCallback(CallbackID, uint32, uint32, EngineID, const Vehicle *) { return 0; }
bool Engine::IsEnabled() const { return false; }   /* only via Engine::GetIfValid over the empty pool */

/* --- file (fileio_func.h) — InitializeLanguagePacks path we bypass (link-only) --- */
std::string FioGetDirectory(Searchpath, Subdirectory) { return std::string(); }
