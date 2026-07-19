#include "stdafx.h"
#include "blitter/8bpp_simple.hpp"
#include "spriteloader/spriteloader.hpp"
#include "spriteloader/grf.hpp"
#include "spriteloader/sprite_file_type.hpp"
#include "fileio_type.h"
#include "gfx_type.h"
#include "zoom_type.h"
#include <cstdlib>
#include <cstring>
#include <string>

DrawPixelInfo _screen;
int _debug_driver_level;
void *GetRawSprite(SpriteID, SpriteType, AllocatorProc *, SpriteEncoder *) { return nullptr; }

// Debug log sink (implemented Mac-side in maclog.cpp -> Open Transport UDP, the
// same sink MiniVNC uses). Lets us watch the GRF pipeline run on real hardware.
extern "C" void ottd_log(const char *fmt, ...);

// Route OpenTTD's own Debug()/DebugPrint through the sink too.
void DebugPrint(const char *level, const std::string &msg) { ottd_log("dbg %s: %s", level ? level : "?", msg.c_str()); }
static void *sprite_alloc(size_t n) { return std::malloc(n); }

// OpenTTD's genuine DOS palette (256 RGB triples), straight from src/table/palettes.h.
#include "table/palettes.h"
extern "C" void ottd_get_palette(unsigned char *rgb /* 256*3 */)
{
    for (int i = 0; i < 256; i++) {
        rgb[i * 3 + 0] = _palette.palette[i].r;
        rgb[i * 3 + 1] = _palette.palette[i].g;
        rgb[i * 3 + 2] = _palette.palette[i].b;
    }
}

// Build id -> file position map for a container-v2 GRF, mirroring OpenTTD's own
// ReadGRFSpriteOffsets (src/spritecache.cpp). file_pos points at the sprite's id dword.
#include <map>
static std::map<uint32, size_t> BuildSpriteOffsets(SpriteFile &file)
{
    std::map<uint32, size_t> offsets;
    file.SeekToBegin();
    size_t data_offset = file.ReadDword();
    size_t old_pos = file.GetPos();
    file.SeekTo(data_offset, SEEK_CUR);

    uint32 id, prev_id = 0;
    size_t prev_pos = 0;
    while ((id = file.ReadDword()) != 0) {
        if (id != prev_id) {
            if (prev_id != 0) offsets[prev_id] = prev_pos;
            prev_pos = file.GetPos() - 4;
        }
        prev_id = id;
        uint32 length = file.ReadDword();
        file.SkipBytes((int)length);
    }
    if (prev_id != 0) offsets[prev_id] = prev_pos;

    file.SeekTo(old_pos, SEEK_SET);
    return offsets;
}

// Load one real sprite from the GRF through OpenTTD's genuine decoder, then hand it
// to the real 8bpp blitter's Encode(). Returns nullptr if the sprite isn't present.
static Sprite *LoadRealSprite(SpriteFile &file, const std::map<uint32, size_t> &offsets,
                              uint32 id, Blitter_8bppSimple &blit)
{
    auto it = offsets.find(id);
    if (it == offsets.end()) return nullptr;

    SpriteLoader::Sprite spr[ZOOM_LVL_COUNT];
    SpriteLoaderGrf loader(2);
    uint8 avail = loader.LoadSprite(spr, file, it->second, ST_NORMAL, /*load_32bpp*/ false, 0);
    if (avail == 0) return nullptr;

    for (int zl = 0; zl < ZOOM_LVL_COUNT; zl++) {
        if (avail & (1 << zl)) return blit.Encode(&spr[zl], sprite_alloc);
    }
    return nullptr;
}

extern "C" int ottd_render(unsigned char *fb, int pitch, int w, int h)
{
    ottd_log("render begin: fb=%dx%d pitch=%d", w, h, pitch);
    _screen.pitch = pitch; _screen.width = w; _screen.height = h;
    Blitter_8bppSimple blit;

    // sky/background
    blit.DrawRect(fb, w, h, 0xC9);

    // Open the real OpenGFX base GRF sitting next to the app on the Mac disk.
    ottd_log("opening ogfx1_base.grf ...");
    SpriteFile file("ogfx1_base.grf", NO_DIRECTORY, /*palette_remap*/ false);
    byte cv = file.GetContainerVersion();
    ottd_log("container version = %d", (int)cv);
    if (cv != 2) { ottd_log("ERROR: not container v2 -> abort"); return -1; }

    std::map<uint32, size_t> offsets = BuildSpriteOffsets(file);
    ottd_log("indexed %d real sprites", (int)offsets.size());

    // SPR_FLAT_GRASS_TILE == 3981 in OpenTTD's sprite numbering (64x31 ground tile).
    ottd_log("loading sprite id 3981 (SPR_FLAT_GRASS_TILE) ...");
    Sprite *grass = LoadRealSprite(file, offsets, 3981, blit);
    if (grass == nullptr) { ottd_log("ERROR: sprite 3981 decode failed"); return -2; }
    ottd_log("decoded+encoded grass: %dx%d offs=(%d,%d)",
             (int)grass->width, (int)grass->height, (int)grass->x_offs, (int)grass->y_offs);

    // Lay the real grass tile out as an isometric field.
    int originX = w / 2;
    int originY = 32;
    int drawn = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int sx = originX + (col - row) * 32 + grass->x_offs;
            int sy = originY + (col + row) * 16 + grass->y_offs;
            // Draw() does not clip; only blit tiles that fit entirely on screen.
            if (sx < 0 || sy < 0 || sx + grass->width > w || sy + grass->height > h) continue;

            Blitter::BlitterParams bp;
            std::memset(&bp, 0, sizeof(bp));
            bp.sprite = grass->data;
            bp.sprite_width = grass->width; bp.sprite_height = grass->height;
            bp.width = grass->width; bp.height = grass->height;
            bp.left = sx; bp.top = sy;
            bp.dst = fb; bp.pitch = pitch;
            blit.Draw(&bp, BM_NORMAL, ZOOM_LVL_NORMAL);
            drawn++;
        }
    }
    ottd_log("drew %d grass tiles -> done", drawn);
    return (int)offsets.size();   // >0 sprites indexed => success
}
