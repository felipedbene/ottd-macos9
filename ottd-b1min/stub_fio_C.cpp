// Bisect C: the WORKING bisectB stub, PLUS exactly the two globals that real spritecache.o
// constructs and the stub omits: _sprite_files (std::vector) and _grf_sprite_offsets (std::map).
// If C bombs, those container globals' static construction is the crash.
#include "stdafx.h"
#include "spritecache.h"
#include "spriteloader/spriteloader.hpp"
#include "spriteloader/sprite_file_type.hpp"
#include <vector>
#include <map>
#include <memory>

uint _sprite_cache_size = 2;
extern const byte _palmap_w2d[256] = {};
ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_COUNT];

// >>> the two suspects, constructed at static-init exactly as spritecache.cpp does <<<
struct GrfSpriteOffset { size_t file_pos; byte control_flags; };
std::vector<std::unique_ptr<SpriteFile>> _probe_sprite_files;
std::map<uint32, GrfSpriteOffset>        _probe_grf_sprite_offsets;
void *volatile keep_probe = (void *)&_probe_sprite_files;

uint  GetMaxSpriteID() { return 0; }
void *GetRawSprite(SpriteID, SpriteType, AllocatorProc *, SpriteEncoder *) { return nullptr; }
void  GfxClearSpriteCache() {}
bool  LoadNextSprite(int, SpriteFile &, uint) { return false; }
void  ReadGRFSpriteOffsets(SpriteFile &) {}
SpriteFile &OpenCachedSpriteFile(const std::string &, Subdirectory, bool)
{
    SpriteFile *p = nullptr; return *p;
}
