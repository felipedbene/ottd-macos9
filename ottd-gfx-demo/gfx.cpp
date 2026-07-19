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

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <MacMemory.h>
#include <string.h>

#define FB_W 320
#define FB_H 240
extern "C" int ottd_render(unsigned char *fb, int pitch, int w, int h);   // OpenTTD's real blitter (returns sprite count, or <0 on error)
extern "C" void ottd_get_palette(unsigned char *rgb);                     // OpenTTD's real DOS palette
extern "C" void ottd_log_init(const char *path);                          // debug trace file (File Manager)
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);

#define LOG_FILE "ottd-log.txt"     // written next to the app; lands on the AFP share

// Install OpenTTD's 256-colour palette: drive the screen CLUT (so an 8-bit display can
// actually show these colours) AND build a matching CTab for the offscreen PixMap, so
// CopyBits maps every index to the identical OpenTTD colour. Returns the PixMap CTab.
static CTabHandle install_ottd_palette(void)
{
    unsigned char pal[256 * 3];
    ottd_get_palette(pal);

    ColorSpec specs[256];
    for (int i = 0; i < 256; i++) {
        specs[i].value = i;
        specs[i].rgb.red   = (pal[i * 3 + 0] << 8) | pal[i * 3 + 0];
        specs[i].rgb.green = (pal[i * 3 + 1] << 8) | pal[i * 3 + 1];
        specs[i].rgb.blue  = (pal[i * 3 + 2] << 8) | pal[i * 3 + 2];
    }

    // Only meaningful on an indexed (<=8bpp) display; on a direct display CopyBits shows
    // the exact colours anyway, so skip the CLUT poke to avoid disturbing the screen.
    GDHandle gd = GetMainDevice();
    if (gd && (*(*gd)->gdPMap)->pixelSize <= 8) SetEntries(0, 255, specs);

    CTabHandle cth = GetCTable(8);
    for (int i = 0; i < 256; i++) {
        (*cth)->ctTable[i].value = i;
        (*cth)->ctTable[i].rgb   = specs[i].rgb;
    }
    CTabChanged(cth);
    return cth;
}

int main()
{
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); InitCursor();

    ottd_log_init(LOG_FILE);   // open the trace file (File Manager write seam)
    ottd_log("=== OpenTTD gfx demo starting on Mac OS 9 ===");

    Rect wr; SetRect(&wr, 40, 60, 40 + FB_W, 60 + FB_H);
    WindowPtr win = NewCWindow(NULL, &wr, "\pOpenTTD real GRF sprites -> CopyBits",
                               true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);

    CTabHandle palTab = install_ottd_palette();   // OpenTTD's real DOS palette -> screen + PixMap

    long rowBytes = (FB_W + 3) & ~3;
    unsigned char *fb = (unsigned char *)NewPtr(rowBytes * FB_H);

    int rc = ottd_render(fb, (int)rowBytes, FB_W, FB_H);   // load+decode+draw a real OpenGFX tile

    // Report status in the window title: sprite count on success, error code otherwise.
    Str255 title; long v = rc;
    if (rc >= 0) { BlockMoveData("\pOpenGFX loaded: 0000 sprites", title, 28); }
    else         { BlockMoveData("\pGRF load FAILED (rc=00)", title, 23); v = -rc; }
    for (int i = title[0]; v > 0 && i > 0; i--) {
        if (title[i] >= '0' && title[i] <= '9') { title[i] = '0' + (v % 10); v /= 10; }
    }
    SetWTitle(win, title);

    PixMap pm; memset(&pm, 0, sizeof(pm));
    pm.baseAddr = (Ptr)fb;
    pm.rowBytes = (short)(rowBytes | 0x8000);
    SetRect(&pm.bounds, 0, 0, FB_W, FB_H);
    pm.hRes = 0x00480000; pm.vRes = 0x00480000;
    pm.pixelType = 0; pm.pixelSize = 8; pm.cmpCount = 1; pm.cmpSize = 8;
    pm.pmTable = palTab;

    Rect src; SetRect(&src, 0, 0, FB_W, FB_H);
    Rect dst = win->portRect;
    CopyBits((BitMap *)&pm, &win->portBits, &src, &dst, srcCopy, NULL);

    ottd_log("blit complete, rc=%d -> entering event loop", rc);
    while (!Button()) {
        EventRecord ev;
        if (WaitNextEvent(everyEvent, &ev, 10, NULL) && (ev.what == keyDown || ev.what == mouseDown)) break;
    }
    ottd_log("=== exiting ===");
    ottd_log_close();
    return 0;
}
