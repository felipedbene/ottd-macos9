/*
 * macclassic_sys.c — Toolbox side of the classic Mac video driver (see macsys.h).
 * Owns the window, framebuffer, PixMap/CTab, event queue. No OpenTTD headers here.
 * Grown from B1's winmain.cpp; destined for src/os/macosclassic/.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <MacMemory.h>
#include <string.h>

#include "macsys.h"

extern void ottd_log(const char *fmt, ...);

static WindowPtr g_win;
static unsigned char *g_fb;
static int g_w, g_h, g_pitch;
static CTabHandle g_ctab;
static PixMap g_pm;

/* ---- event queue (translate on arrival, drain via macsys_poll_event) ---- */
#define QCAP 32
static MacSysEvent g_q[QCAP];
static int g_qn, g_qhead;

static void push_event(int what, int key_char, int key_code, unsigned mods, int mx, int my)
{
    MacSysEvent *e;
    if (g_qn == QCAP) return; /* drop newest on overflow; input, not data */
    e = &g_q[(g_qhead + g_qn) % QCAP];
    e->what = what; e->key_char = key_char; e->key_code = key_code;
    e->modifiers = mods; e->mx = mx; e->my = my;
    g_qn++;
}

static unsigned mac_mods(unsigned short m)
{
    unsigned r = 0;
    if (m & cmdKey)     r |= MACSYS_MOD_CMD;
    if (m & shiftKey)   r |= MACSYS_MOD_SHIFT;
    if (m & optionKey)  r |= MACSYS_MOD_OPTION;
    if (m & ControlKey) r |= MACSYS_MOD_CTRL; /* multiversal spells it ControlKey */
    return r;
}

static void translate_event(EventRecord *e)
{
    switch (e->what) {
        case mouseDown: {
            WindowPtr w;
            short part = FindWindow(e->where, &w);
            switch (part) {
                case inDrag:
                    DragWindow(w, e->where, &qd.screenBits.bounds);
                    break;
                case inGoAway:
                    if (TrackGoAway(w, e->where)) push_event(MACSYS_EVT_QUIT, 0, 0, 0, 0, 0);
                    break;
                case inContent:
                    if (w == g_win) {
                        Point p = e->where;
                        SetPort(g_win);
                        GlobalToLocal(&p);
                        push_event(MACSYS_EVT_MOUSEDOWN, 0, 0, mac_mods(e->modifiers), p.h, p.v);
                    }
                    break;
                default: break;
            }
            break;
        }
        case mouseUp: {
            Point p = e->where;
            if (g_win != NULL) { SetPort(g_win); GlobalToLocal(&p); }
            push_event(MACSYS_EVT_MOUSEUP, 0, 0, mac_mods(e->modifiers), p.h, p.v);
            break;
        }
        case keyDown:
        case autoKey: {
            int ch   = (int)(e->message & charCodeMask);
            int code = (int)((e->message & keyCodeMask) >> 8);
            if ((e->modifiers & cmdKey) && (ch == 'q' || ch == 'Q')) {
                push_event(MACSYS_EVT_QUIT, 0, 0, 0, 0, 0);
            } else {
                push_event(MACSYS_EVT_KEYDOWN, ch, code, mac_mods(e->modifiers), 0, 0);
            }
            break;
        }
        case updateEvt: {
            WindowPtr w = (WindowPtr)e->message;
            BeginUpdate(w);
            EndUpdate(w);
            if (w == g_win) push_event(MACSYS_EVT_UPDATE, 0, 0, 0, 0, 0);
            break;
        }
        default: break; /* activate/os events: nothing yet (B2.1) */
    }
}

static void pump_one(unsigned long sleep_ticks)
{
    EventRecord e;
    if (WaitNextEvent(everyEvent, &e, sleep_ticks, NULL)) translate_event(&e);
}

/* ---------------------------------- API ---------------------------------- */

int macsys_create_window(int w, int h, const char *title)
{
    Rect wr;
    Str255 pt;
    long rowBytes;
    int i, n;

    n = (int)strlen(title); if (n > 255) n = 255;
    pt[0] = (unsigned char)n;
    memcpy(pt + 1, title, (size_t)n);

    SetRect(&wr, 10, 44, 10 + w, 44 + h);
    g_win = NewCWindow(NULL, &wr, pt, true, documentProc, (WindowPtr)-1, true, 0);
    if (g_win == NULL) { ottd_log("macsys: NewCWindow failed"); return 0; }
    SetPort(g_win);

    rowBytes = (long)((w + 3) & ~3);
    g_fb = (unsigned char *)NewPtr(rowBytes * h);
    if (g_fb == NULL) { ottd_log("macsys: fb NewPtr(%ld) failed", rowBytes * (long)h); return 0; }
    g_w = w; g_h = h; g_pitch = (int)rowBytes;

    /* CTab starts as the system 8-bit table; macsys_set_palette overwrites it. */
    g_ctab = GetCTable(8);
    for (i = 0; i < 256; i++) (*g_ctab)->ctTable[i].value = i;
    CTabChanged(g_ctab);

    memset(&g_pm, 0, sizeof(g_pm));
    g_pm.baseAddr = (Ptr)g_fb;
    g_pm.rowBytes = (short)(rowBytes | 0x8000);
    SetRect(&g_pm.bounds, 0, 0, (short)w, (short)h);
    g_pm.hRes = 0x00480000; g_pm.vRes = 0x00480000;
    g_pm.pixelType = 0; g_pm.pixelSize = 8; g_pm.cmpCount = 1; g_pm.cmpSize = 8;
    g_pm.pmTable = g_ctab;

    ottd_log("macsys: window %dx%d pitch=%d fb=%p", w, h, g_pitch, (void *)g_fb);
    return 1;
}

void macsys_destroy_window(void)
{
    if (g_win != NULL) { DisposeWindow(g_win); g_win = NULL; }
    if (g_fb != NULL) { DisposePtr((Ptr)g_fb); g_fb = NULL; }
}

unsigned char *macsys_fb(int *pitch)
{
    if (pitch != NULL) *pitch = g_pitch;
    return g_fb;
}

void macsys_blit(int left, int top, int width, int height)
{
    Rect r;
    if (g_win == NULL) return;
    if (left < 0) { width += left; left = 0; }
    if (top < 0) { height += top; top = 0; }
    if (left + width > g_w) width = g_w - left;
    if (top + height > g_h) height = g_h - top;
    if (width <= 0 || height <= 0) return;
    SetRect(&r, (short)left, (short)top, (short)(left + width), (short)(top + height));
    SetPort(g_win);
    CopyBits((BitMap *)&g_pm, &g_win->portBits, &r, &r, srcCopy, NULL);
}

void macsys_set_palette(int first, int count, const unsigned char *rgb)
{
    ColorSpec specs[256];
    GDHandle gd;
    int i;
    if (first < 0 || count <= 0 || first + count > 256) return;
    for (i = 0; i < count; i++) {
        unsigned char r = rgb[i * 3 + 0], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        specs[i].value = (short)(first + i);
        specs[i].rgb.red   = (unsigned short)((r << 8) | r);
        specs[i].rgb.green = (unsigned short)((g << 8) | g);
        specs[i].rgb.blue  = (unsigned short)((b << 8) | b);
        (*g_ctab)->ctTable[first + i].value = (short)(first + i);
        (*g_ctab)->ctTable[first + i].rgb = specs[i].rgb;
    }
    CTabChanged(g_ctab);
    /* Drive the hardware CLUT directly on an 8-bit screen: this is what makes
     * palette animation (water) free — no re-blit needed. */
    gd = GetMainDevice();
    if (gd && (*(*gd)->gdPMap)->pixelSize <= 8) SetEntries((short)first, (short)(count - 1), specs);
}

int macsys_poll_event(MacSysEvent *ev)
{
    if (g_qn == 0) pump_one(0);
    if (g_qn == 0) return 0;
    *ev = g_q[g_qhead];
    g_qhead = (g_qhead + 1) % QCAP;
    g_qn--;
    return 1;
}

void macsys_get_mouse(int *x, int *y, int *button_down)
{
    Point p;
    if (g_win != NULL) SetPort(g_win);
    GetMouse(&p); /* already port-local */
    if (x != NULL) *x = p.h;
    if (y != NULL) *y = p.v;
    if (button_down != NULL) *button_down = Button() ? 1 : 0;
}

unsigned macsys_get_dirkeys(void)
{
    /* Arrow virtual keycodes: left 0x7B, right 0x7C, down 0x7D, up 0x7E.
     * DO NOT use GetKeys() here: multiversal declares it as a 68K inline trap
     * (M68K_INLINE 0xA976) whose PPC path returned garbage -> phantom held-down
     * arrows -> the landscape scrolled itself off-screen (B2 build 8 screenshot).
     * Read the KeyMap low-memory global (0x0174) directly instead; bit order
     * calibrated from the change-log below. */
    const unsigned char *b = (const unsigned char *)0x0174;
    unsigned r = 0;
    static unsigned char last[16];
    if (memcmp(b, last, 16) != 0) {
        memcpy(last, b, 16);
        ottd_log("keymap: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                 b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
    }
#define KDOWN(k) ((b[(k) >> 3] >> ((k) & 7)) & 1)
    if (KDOWN(0x7B)) r |= 1; /* left */
    if (KDOWN(0x7E)) r |= 2; /* up */
    if (KDOWN(0x7C)) r |= 4; /* right */
    if (KDOWN(0x7D)) r |= 8; /* down */
#undef KDOWN
    return r;
}

unsigned long macsys_ticks(void)
{
    return (unsigned long)TickCount();
}

void ottd_cooperative_sleep_us(unsigned long us)
{
    static unsigned long n = 0;
    if (n < 3 || (n & 0x3F) == 0) ottd_log("B2: coop sleep %luus (SleepTillNextTick yields)", us);
    n++;
    unsigned long deadline = TickCount() + (us + 16624) / 16625; /* round up: never undersleep to 0 */
    for (;;) {
        unsigned long now = TickCount();
        if (now >= deadline) break;
        pump_one(deadline - now); /* WNE sleep: yields CPU to the rest of the system */
    }
}
