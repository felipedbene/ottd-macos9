#!/usr/bin/env python3
"""Decode OpenGFX ground/landscape tiles to PNG and emit a mapping table.
Reuses the RLE decoder + DOS palette logic from /tmp/grfdecode.py."""
import struct, re, zlib, os

GRF='/private/tmp/claude-501/-Users-felipe/ab1a35e1-2df4-4903-b156-bc7b1362f482/scratchpad/opengfx/opengfx-8.0/ogfx1_base.grf'
PALH='/Users/felipe/ottd-macos9/openttd-13.4/src/table/palettes.h'
OUT='/Users/felipe/ottd-macos9/agent-tiles/previews'
os.makedirs(OUT, exist_ok=True)

# --- DOS palette ---
txt=open(PALH).read()
nums=re.findall(r'(?:M|Colour)\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)',txt)
pal=[(int(r),int(g),int(b)) for r,g,b in nums[:256]]
assert len(pal)==256, len(pal)

f=open(GRF,'rb').read()
data_offset=struct.unpack('<I',f[10:14])[0]
sec=14+data_offset
offsets={}
p=sec; prev=0; cur=0
while p<len(f):
    idv=struct.unpack('<I',f[p:p+4])[0]
    if idv==0: break
    if idv!=prev:
        offsets[prev]=cur; cur=p
    prev=idv
    length=struct.unpack('<I',f[p+4:p+8])[0]
    p+=8+length
offsets[prev]=cur

def decode(idv):
    """Decode a container-v2 palette (8bpp) sprite following OpenTTD
    src/spriteloader/grf.cpp DecodeSingleSprite. Returns w,h,xoff,yoff, and
    a w*h buffer of palette indices (0 = transparent)."""
    pos=offsets[idv]
    idr=struct.unpack('<I',f[pos:pos+4])[0]; assert idr==idv
    q=pos+4
    struct.unpack('<I',f[q:q+4])[0]; q+=4  # chunk length (unused)
    typ=f[q]; q+=1
    zoom=f[q]; q+=1
    h=struct.unpack('<H',f[q:q+2])[0]; q+=2
    w=struct.unpack('<H',f[q+0:q+2])[0]; q+=2
    xoff=struct.unpack('<h',f[q:q+2])[0]; q+=2
    yoff=struct.unpack('<h',f[q:q+2])[0]; q+=2
    # bpp: SCC_PAL only -> 1 byte/pixel
    bpp=1
    if typ & 0x08:
        decomp=struct.unpack('<I',f[q:q+4])[0]; q+=4
    else:
        decomp=w*h*bpp
    # --- RLE decompress into dest_orig ---
    dest=bytearray(); need=decomp
    while need>0:
        code=f[q]; q+=1
        if code<128:                       # code >= 0
            size=0x80 if code==0 else code
            dest+=f[q:q+size]; q+=size; need-=size
        else:                              # code < 0 (int8)
            code-=256
            data_off=((code&7)<<8)|f[q]; q+=1
            size=-(code>>3)
            base=len(dest)-data_off
            for i in range(size):
                dest.append(dest[base+i])
            need-=size
    dest=bytes(dest)
    # --- output buffer, 0 = transparent ---
    px=bytearray(w*h)   # zero-filled == transparent
    if typ & 0x08:
        # chunked/transparency format
        wide = (w > 256)   # container_format>=2 && width>256 uses 4-byte headers
        big  = (decomp > 0xFFFF)  # offset table entry width
        for y in range(h):
            if big:
                off=dest[y*4]|(dest[y*4+1]<<8)|(dest[y*4+2]<<16)|(dest[y*4+3]<<24)
            else:
                off=dest[y*2]|(dest[y*2+1]<<8)
            d=off
            last=False
            while not last:
                if wide:
                    last=(dest[d+1]&0x80)!=0
                    length=((dest[d+1]&0x7F)<<8)|dest[d]
                    skip=(dest[d+3]<<8)|dest[d+2]
                    d+=4
                else:
                    last=(dest[d]&0x80)!=0
                    length=dest[d]&0x7F; d+=1
                    skip=dest[d]; d+=1
                for x in range(length):
                    px[y*w+skip+x]=dest[d]; d+=1
    else:
        px=bytearray(dest[:w*h])
    return w,h,xoff,yoff,bytes(px)

def png(idv,path):
    w,h,xoff,yoff,px=decode(idv)
    raw=bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            m=px[y*w+x]
            if m==0: raw+=bytes((0,180,180))
            else: raw+=bytes(pal[m])
    def chunk(tp,d):
        c=tp+d; return struct.pack('>I',len(d))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
    sig=b'\x89PNG\r\n\x1a\n'
    ihdr=struct.pack('>IIBBBBB',w,h,8,2,0,0,0)
    open(path,'wb').write(sig+chunk(b'IHDR',ihdr)+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b''))
    return w,h,xoff,yoff

# --- OpenTTD tables (verbatim from source) ---
# src/landscape.cpp:79
SLOPE_TO_SPRITE_OFFSET=[
    0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
    0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0,
]
# src/slope_type.h: valid slope value -> name
SLOPE_NAMES={
 0:"SLOPE_FLAT",1:"SLOPE_W",2:"SLOPE_S",3:"SLOPE_SW",4:"SLOPE_E",5:"SLOPE_EW",
 6:"SLOPE_SE",7:"SLOPE_WSE",8:"SLOPE_N",9:"SLOPE_NW",10:"SLOPE_NS",11:"SLOPE_NWS",
 12:"SLOPE_NE",13:"SLOPE_ENW",14:"SLOPE_SEN",
 23:"SLOPE_STEEP_W",27:"SLOPE_STEEP_S",29:"SLOPE_STEEP_N",30:"SLOPE_STEEP_E",
}
# base sprite constants from src/table/sprites.h
BASES={
 "GRASS":3981,"BARE":3924,"GRASS_1_3":3943,"GRASS_2_3":3962,
 "ROUGH":4000,"ROCKY_1":4023,"ROCKY_2":4042,"WATER":4061,
 "SNOW_DESERT_0Q":4550,"SNOW_DESERT_1Q":4493,"SNOW_DESERT_2Q":4512,"SNOW_DESERT_3Q":4531,
}

# valid slopes in ascending numeric order
VALID_SLOPES=sorted(SLOPE_NAMES.keys())

results={}
print("=== Grass flat + all slopes ===")
for s in VALID_SLOPES:
    off=SLOPE_TO_SPRITE_OFFSET[s]
    idv=BASES["GRASS"]+off
    name=SLOPE_NAMES[s]
    w,h,xo,yo=png(idv,f"{OUT}/grass_{s:02d}_{name}.png")
    results[(s,name)]=(idv,off,w,h,xo,yo)
    print(f"slope={s:2d} {name:14s} off={off:2d} id={idv}  {w}x{h} off=({xo},{yo})")

# Other terrains: flat only + a couple slope samples to prove same rule
print("\n=== Other terrains (flat) ===")
other=[("BARE",3924),("ROUGH",4000),("ROCKY_1",4023),("ROCKY_2",4042),
       ("WATER",4061),("SNOW_DESERT_0Q",4550),("SNOW_DESERT_1Q",4493),
       ("SNOW_DESERT_2Q",4512),("SNOW_DESERT_3Q",4531),
       ("GRASS_1_3",3943),("GRASS_2_3",3962)]
for nm,base in other:
    w,h,xo,yo=png(base,f"{OUT}/{nm.lower()}_flat.png")
    print(f"{nm:16s} flat id={base}  {w}x{h} off=({xo},{yo})")

# NOTE: In OpenGFX 8.0 the 1/4 snow-desert *flat* tile is at 4494, NOT the stock
# constant 4493 (which decodes to a building). 0Q/2Q/3Q match their constants.
w,h,xo,yo=png(4494,f"{OUT}/snow_desert_1q_flat_OPENGFX_4494.png")
print(f"SNOW_DESERT_1Q(OpenGFX) flat id=4494  {w}x{h} off=({xo},{yo})")

# --- optional: a few tree sprites (temperate). Base tree sprites are raw ---
# sprite numbers used in src/table/tree_land.h (e.g. 0x652=1618). Trees are a
# 7-frame growth sequence per type; not a slope formula. Render a couple as proof.
for tid in (0x628, 0x652, 0x660, 0x66e):
    w,h,xo,yo=png(tid,f"{OUT}/tree_{tid:04x}.png")
    print(f"TREE {tid:#06x}={tid} {w}x{h} off=({xo},{yo})")
# prove slope rule on rough with a steep slope (SLOPE_STEEP_W=23 -> off 16)
idv=4000+SLOPE_TO_SPRITE_OFFSET[23]
w,h,xo,yo=png(idv,f"{OUT}/rough_slope23.png")
print(f"ROUGH            SLOPE_STEEP_W id={idv} {w}x{h} off=({xo},{yo})")
# water: only FLAT + the 14 simple (non-steep) slopes are valid tiles.
# Prove with a simple slope SLOPE_NW (9 -> off 9).
idv=4061+SLOPE_TO_SPRITE_OFFSET[9]
w,h,xo,yo=png(idv,f"{OUT}/water_slope_nw.png")
print(f"WATER            SLOPE_NW      id={idv} {w}x{h} off=({xo},{yo})")

print("\nDONE. previews in", OUT)
