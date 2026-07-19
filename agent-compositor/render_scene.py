#!/usr/bin/env python3
"""
render_scene.py -- HOST-side compositor proof.

Reads the ordered draw list emitted by ./harness_dump (the portable C++ core)
and composites the corresponding OpenGFX ground sprites into a single PNG,
using each sprite's OWN xoff/yoff (which is how OpenTTD visually raises slope
tiles). This validates the projection + slope selection + back-to-front order
produced by landscape_render.cpp against real sprite pixels.

Decoder is the RLE + DOS-palette pattern from /tmp/grfdecode.py (OpenTTD's
sprite container format). No Mac / OpenTTD build required.
"""
import struct, re, sys, zlib, subprocess, os

HERE = os.path.dirname(os.path.abspath(__file__))
GRF  = '/private/tmp/claude-501/-Users-felipe/ab1a35e1-2df4-4903-b156-bc7b1362f482/scratchpad/opengfx/opengfx-8.0/ogfx1_base.grf'
PALH = '/Users/felipe/ottd-macos9/openttd-13.4/src/table/palettes.h'

# ---- OpenTTD DOS palette (first 256 M()/Colour() triples) ----
txt = open(PALH).read()
nums = re.findall(r'(?:M|Colour)\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)', txt)
PAL = [(int(r), int(g), int(b)) for r, g, b in nums[:256]]
assert len(PAL) == 256, len(PAL)

# ---- GRF sprite index ----
f = open(GRF, 'rb').read()
data_offset = struct.unpack('<I', f[10:14])[0]
sec = 14 + data_offset
_offsets = {}
p, prev, cur = sec, 0, 0
while p < len(f):
    idv = struct.unpack('<I', f[p:p+4])[0]
    if idv == 0:
        break
    if idv != prev:
        _offsets[prev] = cur
        cur = p
    prev = idv
    length = struct.unpack('<I', f[p+4:p+8])[0]
    p += 8 + length
_offsets[prev] = cur

def decode(idv):
    """Return (w, h, xoff, yoff, pixels[w*h] palette-index, 0=transparent)."""
    pos = _offsets[idv]
    assert struct.unpack('<I', f[pos:pos+4])[0] == idv
    q = pos + 4
    struct.unpack('<I', f[q:q+4])[0]; q += 4          # num bytes
    typ = f[q]; q += 1
    q += 1                                             # zoom
    h = struct.unpack('<H', f[q:q+2])[0]; q += 2
    w = struct.unpack('<H', f[q:q+2])[0]; q += 2
    xoff = struct.unpack('<h', f[q:q+2])[0]; q += 2
    yoff = struct.unpack('<h', f[q:q+2])[0]; q += 2
    t = typ & ~7
    transparent = bool(t & 0x08)
    # For transparent sprites the container stores a total decomp size dword.
    decomp = struct.unpack('<I', f[q:q+4])[0] if transparent else w*h
    if transparent:
        q += 4
    # ---- OpenTTD sprite RLE (memcpy / back-reference), grf.cpp ----
    out = bytearray()
    need = decomp
    while need > 0:
        code = f[q]; q += 1
        if code < 128:
            size = 128 if code == 0 else code
            out += f[q:q+size]; q += size; need -= size
        else:
            code -= 256
            data_off = ((code & 7) << 8) | f[q]; q += 1
            size = -(code >> 3)
            for _ in range(size):
                out.append(out[len(out)-data_off])
            need -= size
    out = bytes(out)

    if not transparent:
        return w, h, xoff, yoff, out

    # ---- Expand chunked-transparency layout into a full w*h index buffer ----
    # Per OpenTTD src/spriteloader/grf.cpp Decode(): the decompressed buffer
    # begins with an offset table (one entry per scanline) pointing at that
    # line's chunk list. Table entries are 32-bit if w>255 else 16-bit.
    # Each chunk: [skip][len(&0x7f, high bit=last)] then `len` pixel indices.
    wide = w > 255
    esz = 4 if wide else 2
    full = bytearray(w * h)  # 0 = transparent
    for y in range(h):
        if wide:
            lo = struct.unpack('<I', out[y*4:y*4+4])[0]
        else:
            lo = struct.unpack('<H', out[y*2:y*2+2])[0]
        pos = lo
        while True:
            n = out[pos]; pos += 1        # length byte, high bit = last chunk
            last = bool(n & 0x80)
            n &= 0x7f
            skip = out[pos]; pos += 1      # transparent pixels to skip
            for i in range(n):
                x = skip + i
                if 0 <= x < w:
                    full[y*w + x] = out[pos + i]
            pos += n
            if last:
                break
    return w, h, xoff, yoff, bytes(full)

def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y*w*3:(y+1)*w*3]
    def chunk(tag, d):
        c = tag + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)
    open(path, 'wb').write(sig + chunk(b'IHDR', ihdr)
                           + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
                           + chunk(b'IEND', b''))

def main():
    # 1. run the portable C++ core to get the ordered draw list
    dump = subprocess.check_output([os.path.join(HERE, 'harness_dump')]).decode()
    tiles = []
    for line in dump.splitlines():
        if line.startswith('#') or not line.strip():
            continue
        mx, my, slope, base_h, sprite, sx, sy = map(int, line.split())
        tiles.append((mx, my, slope, base_h, sprite, sx, sy))

    # 2. The C++ RemapCoords works in ZOOM_LVL_BASE(=4) sub-units. Normalise to
    #    1:1 pixels by dividing by ZOOM_LVL_BASE. That turns the world step
    #    (TILE_SIZE=16) into the on-screen TILE_PIXELS/2 = 32/16 diamond grid:
    #    +1 map x -> screen (+32,+16); +1 map y -> (-32,+16); -1 z -> (0,+... )
    ZB = 4

    # 3. compute canvas bounds from placed sprite rects (anchor + own offsets)
    placed = []
    minx = miny = 10**9
    maxx = maxy = -10**9
    for (mx, my, slope, base_h, sprite, sx, sy) in tiles:
        w, h, xoff, yoff, px = decode(sprite)
        px0 = sx // ZB + xoff
        py0 = sy // ZB + yoff
        placed.append((px0, py0, w, h, px))
        minx = min(minx, px0);      miny = min(miny, py0)
        maxx = max(maxx, px0 + w);  maxy = max(maxy, py0 + h)

    pad = 4
    W = (maxx - minx) + 2*pad
    H = (maxy - miny) + 2*pad
    # sky-blue background so transparent (index 0) reads as sky
    bg = (108, 156, 224)
    canvas = bytearray(bytes(bg) * (W * H))

    def put(x, y, rgb):
        if 0 <= x < W and 0 <= y < H:
            i = (y*W + x) * 3
            canvas[i:i+3] = bytes(rgb)

    # 4. paint back-to-front (list is already ordered by the C++ core)
    for (px0, py0, w, h, px) in placed:
        ox = px0 - minx + pad
        oy = py0 - miny + pad
        for yy in range(h):
            row = yy*w
            for xx in range(w):
                m = px[row + xx]
                if m == 0:
                    continue          # transparent
                put(ox+xx, oy+yy, PAL[m])

    out = os.path.join(HERE, 'preview', 'scene.png')
    write_png(out, W, H, bytes(canvas))
    print("wrote %s  (%dx%d, %d tiles)" % (out, W, H, len(tiles)))

if __name__ == '__main__':
    main()
