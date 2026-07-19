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
