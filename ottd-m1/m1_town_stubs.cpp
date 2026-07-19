/*
 * m1_town_stubs.cpp — link-only no-op stubs for the M1 headless build.
 *
 * town_cmd.cpp is pulled in whole (--gc-sections is a no-op in this XCOFF flow),
 * dragging references to ~79 functions from road/house/station/command/news/GRF/
 * viewport/draw subsystems. NONE of these run at runtime: the monthly/yearly town
 * loops only iterate empty pools and do arithmetic. Every definition here exists
 * ONLY to satisfy the linker. Signatures mirror the real headers so the compiler
 * type-checks them for us.
 */
#include "stdafx.h"

#include "animated_tile_func.h"
#include "viewport_func.h"
#include "viewport_kdtree.h"
#include "news_func.h"
#include "news_type.h"
#include "newgrf_house.h"
#include "newgrf_commons.h"
#include "newgrf_cargo.h"
#include "newgrf_config.h"
#include "newgrf_text.h"
#include "newgrf_debug.h"
#include "newgrf_townname.h"
#include "object.h"
#include "object_base.h"
#include "cargopacket.h"
#include "cargotype.h"
#include "road_internal.h"
#include "road_cmd.h"
#include "road_map.h"
#include "industry.h"
#include "station_func.h"
#include "station_base.h"
#include "station_type.h"
#include "window_func.h"
#include "tunnelbridge_cmd.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "terraform_cmd.h"
#include "command_func.h"
#include "subsidy_func.h"
#include "genworld.h"
#include "gfx_func.h"
#include "error.h"
#include "economy_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "townname_func.h"
#include "tilearea_type.h"
#include "company_base.h"
#include "misc/countedptr.hpp"
#include "ai/ai.hpp"
#include "game/game.hpp"

/* ---- C++ static-member / vtable storage (NOT globals in m1_deadpools.c) ---- */

CargoSpec CargoSpec::array[NUM_CARGO];
/* RecursiveCommandCounter::_counter now REAL (command.cpp, build 7). */

/* Out-of-line key virtuals -> emits vtable + typeinfo for SimpleCountedObject. */
int32 SimpleCountedObject::AddRef() { return 0; }
int32 SimpleCountedObject::Release() { return 0; }

/* ---- animated tiles ---- */
void AddAnimatedTile(TileIndex) {}
void DeleteAnimatedTile(TileIndex) {}

/* ---- viewport / draw ---- */
/* DrawFoundation: REAL in landscape.cpp (build 8). */
#ifndef R1_MERGE  /* the real viewport.cpp (+ gfx.o) provide all of these in the render-merge build */
void AddChildSpriteScreen(SpriteID, PaletteID, int, int, bool, const SubSprite *, bool, bool) {}
void AddSortableSpriteToDraw(SpriteID, PaletteID, int, int, int, int, int, int, bool, int, int, int, const SubSprite *) {}
void DrawGroundSprite(SpriteID, PaletteID, const SubSprite *, int, int) {}
void MarkTileDirtyByTile(TileIndex, int, int) {}
void MarkWholeScreenDirty() {}
void ViewportSign::UpdatePosition(int, int, StringID, StringID) {}
ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeTown(TownID) { return {}; }
#endif

/* ---- news ---- */
void AddNewsItem(StringID, NewsType, NewsFlag, NewsReferenceType, uint32, NewsReferenceType, uint32, const NewsAllocatedData *) {}
CompanyNewsInformation::CompanyNewsInformation(const Company *, const Company *) {}

/* ---- houses / NewGRF ---- */
void AnimateNewHouseConstruction(TileIndex) {}
void AnimateNewHouseTile(TileIndex) {}
void DrawNewHouseTile(TileInfo *, HouseID) {}
bool NewHouseTileLoop(TileIndex) { return false; }
bool CanDeleteHouse(TileIndex) { return false; }
void IncreaseBuildingCount(Town *, HouseID) {}
void DecreaseBuildingCount(Town *, HouseID) {}
uint16 GetHouseCallback(CallbackID, uint32, uint32, HouseID, Town *, TileIndex, bool, uint8, CargoTypes) { return 0; }
bool Convert8bitBooleanCallback(const GRFFile *, uint16, uint16) { return false; }
bool ConvertBooleanCallback(const GRFFile *, uint16, uint16) { return false; }
void ErrorUnknownCallbackResult(uint32, uint16, uint16) {}
CargoID GetCargoTranslation(uint8, const GRFFile *, bool) { return 0; }
GRFConfig *GetGRFConfig(uint32, uint32) { return nullptr; }
StringID GetGRFStringID(uint32, StringID) { return 0; }
uint32 GetGRFTownNameId(int) { return 0; }
uint16 GetGRFTownNameType(int) { return 0; }
const char *GRFConfig::GetName() const { return ""; }
void OverrideManagerBase::ResetOverride() {}
void DeleteNewGRFInspectWindow(GrfSpecFeature, uint) {}

/* ---- objects ---- */
void BuildObject(ObjectType, TileIndex, CompanyID, Town *, uint8) {}
Object *Object::GetByTile(TileIndex) { return nullptr; }

/* ---- cargo / economy ---- */
void CargoPacket::InvalidateAllFrom(SourceType, SourceID) {}
uint MoveGoodsToStation(CargoID, uint, SourceType, SourceID, const StationList *, Owner) { return 0; }
void DeleteSubsidyWith(SourceType, SourceID) {}

/* ---- road ---- */
/* CleanUpRoadBits "makes roads look nicer" by trimming unconnectable bits. The
 * real impl is in road.cpp (uncompiled; compiling it would make ValParamRoadType
 * real too and regress on zeroed road-types). Return the bits UNCHANGED
 * (identity): GrowTownInTile bails with `if (CleanUpRoadBits(...)==ROAD_NONE)
 * return;` (town_cmd.cpp 1567), so the old ROAD_NONE stub aborted ALL town growth
 * -> 0 houses. Identity = no beautification but growth proceeds. */
RoadBits CleanUpRoadBits(const TileIndex, RoadBits org_rb) { return org_rb; }
/* GetAnyRoadBits: REAL now (road_map.cpp, build 10). The ROAD_NONE stub made
 * GrowTown blind to roads it laid -> never reached GrowTownAtRoad -> 0 houses. */
/* UpdateNearestTownForRoadTiles: REAL in road_cmd.cpp (build 9). */

/* ---- commands ---- */
CommandCost CmdBuildBridge(DoCommandFlag, TileIndex, TileIndex, TransportType, BridgeType, byte) { return CommandCost(); }
/* CmdBuildRoad: REAL in road_cmd.cpp (build 9). */
CommandCost CmdBuildTunnel(DoCommandFlag, TileIndex, TransportType, byte) { return CommandCost(); }
/* CmdLandscapeClear: REAL in landscape.cpp (build 8). */
std::tuple<CommandCost, Money, TileIndex> CmdTerraformLand(DoCommandFlag, TileIndex, Slope, bool) { return {}; }
Money GetAvailableMoneyForCommand() { return 0; }
/* CommandHelperBase::InternalDoBefore/InternalDoAfter + RecursiveCommandCounter::
 * _counter live in m1_cmd_stubs.cpp (build 7 command dispatch). */

/* ---- landscape ---- REAL in landscape.cpp (build 8): DoClearSquare,
 * GetFoundationSlope, GetSlopePixelZ, GetSnowLine, HighestSnowLine. */

/* ---- tunnel / bridge map ---- */
TileIndex GetOtherBridgeEnd(TileIndex t) { return t; }
TileIndex GetOtherTunnelEnd(TileIndex t) { return t; }

/* ---- stations ---- */
void ClearAllStationCachedNames() {}
void ClearAllIndustryCachedNames() {}
void ModifyStationRatingAround(TileIndex, Owner, int, uint) {}
void UpdateAllStationVirtCoords() {}
void UpdateAirportsNoise() {}
bool Station::CatchmentCoversTown(TownID) const { return false; }
bool StationCompare::operator()(const Station *, const Station *) const { return false; }
const StationList *StationFinder::GetStations() { return nullptr; }

/* ---- windows ---- */
#ifndef R1_MERGE  /* real window.cpp provides these when the window system is linked */
void CloseWindowById(WindowClass, WindowNumber, bool) {}
void InvalidateWindowData(WindowClass, WindowNumber, int, bool) {}
#endif

/* ---- gen world / progress ---- */
void IncreaseGeneratingWorldProgress(GenWorldProgress) {}
void SetGeneratingWorldProgress(GenWorldProgress, uint) {}

/* ---- strings ---- */
#ifndef R1_MERGE  /* real b1_shims.o owns SetDParamStr in the render-merge build */
void SetDParamStr(uint, const std::string &) {}
#endif
#ifndef R1_STRINGS  /* real strings.cpp owns GetString(StringID) when the string system is linked */
std::string GetString(StringID) { return {}; }
#endif
size_t Utf8StringLength(const std::string &) { return 0; }
#ifndef R1_MERGE  /* real b1_shims.o owns ShowErrorMessage in the render-merge build */
void ShowErrorMessage(StringID, StringID, WarningLevel, int, int, const GRFFile *, uint, const uint32 *) {}
#endif

/* ---- town names ---- */
#ifndef R1_MERGE  /* the REAL townname.cpp provides these in the render-merge build (town-name signs) */
char *GetTownName(char *buff, const Town *, const char *) { return buff; }
bool GenerateTownName(uint32 *, TownNames *) { return false; }
bool VerifyTownName(uint32, const TownNameParams *, TownNames *) { return false; }
#else
/* townname.cpp is linked (real GenerateTownNameString). It references these GRF-townname
 * + string entry points only in code paths our built-in (grfid 0) towns never reach, so
 * stub them just to satisfy the link. GetStringWithArgs is likewise unreached (we shim the
 * GetString(char*) overload directly for town signs — see b1_shims.cpp). */
struct GRFTownName;
GRFTownName *GetGRFTownName(uint32) { return nullptr; }
char *GRFTownNameGenerate(char *buf, uint32, uint16, uint32, const char *) { return buf; }
#ifndef R1_STRINGS  /* real strings.cpp owns GetStringWithArgs when the string system is linked */
char *GetStringWithArgs(char *buffr, StringID, StringParameters *, const char *, uint, bool) { return buffr; }
#endif
size_t Utf8StringLength(const char *s) { size_t n = 0; if (s) while (*s) { if (((unsigned char)*s & 0xC0) != 0x80) n++; s++; } return n; }
#endif

/* ---- tile area ---- */
/* OrthogonalTileArea::begin/end/Contains/Expand: REAL in landscape.cpp (build 8). */

/* ---- script events (class statics) ---- */
void AI::BroadcastNewEvent(ScriptEvent *, CompanyID) {}
void Game::NewEvent(ScriptEvent *) {}
