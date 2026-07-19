// Mac Toolbox front-end for the integrated OpenTTD landscape demo.
// Installs OpenTTD's real palette, paints the composited framebuffer via CopyBits.
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <MacMemory.h>
#include <string.h>

#define FB_W 520
#define FB_H 392

extern "C" int  ottd_landscape_render(unsigned char *fb, int pitch, int w, int h);
extern "C" void ottd_get_palette(unsigned char *rgb);
extern "C" void ottd_log_init(const char *path);
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);

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
    GDHandle gd = GetMainDevice();
    if (gd && (*(*gd)->gdPMap)->pixelSize <= 8) SetEntries(0, 255, specs);
    CTabHandle cth = GetCTable(8);
    for (int i = 0; i < 256; i++) { (*cth)->ctTable[i].value = i; (*cth)->ctTable[i].rgb = specs[i].rgb; }
    CTabChanged(cth);
    return cth;
}

int main()
{
    ottd_log_init("ottd-landscape.txt");                 // FIRST thing: catch any reach-of-main
    ottd_log("=== integrated landscape demo: main() reached ===");
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); InitCursor();
    ottd_log("Toolbox inited; free heap = %ld bytes", (long)FreeMem());

    Rect wr; SetRect(&wr, 20, 40, 20 + FB_W, 40 + FB_H);
    WindowPtr win = NewCWindow(NULL, &wr, "\pOpenTTD landscape: real map + real GRF on Mac OS 9",
                               true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);

    CTabHandle palTab = install_ottd_palette();
    long rowBytes = (FB_W + 3) & ~3;
    ottd_log("allocating framebuffer %ld bytes (free heap=%ld)", rowBytes * FB_H, (long)FreeMem());
    unsigned char *fb = (unsigned char *)NewPtr(rowBytes * FB_H);
    if (fb == NULL) { ottd_log("ERROR: framebuffer NewPtr failed (MemError=%d)", (int)MemError()); ottd_log_close(); return 1; }
    ottd_log("framebuffer ok; calling ottd_landscape_render ...");

    int rc = ottd_landscape_render(fb, (int)rowBytes, FB_W, FB_H);
    ottd_log("render rc=%d", rc);

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

    ottd_log("blit complete -> event loop");
    while (!Button()) {
        EventRecord ev;
        if (WaitNextEvent(everyEvent, &ev, 10, NULL) && (ev.what == keyDown || ev.what == mouseDown)) break;
    }
    ottd_log("=== exiting ===");
    ottd_log_close();
    return 0;
}
