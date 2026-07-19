/*
 * macsys.h — neutral C bridge between OpenTTD TUs and Mac Toolbox TUs.
 *
 * Mac Toolbox headers and OpenTTD headers CANNOT share a translation unit
 * (Point/Rect/Random clash), so the video driver is split in two:
 *   macclassic_v.cpp   (OpenTTD side)  — includes video_driver.hpp, calls these
 *   macclassic_sys.c   (Toolbox side)  — includes Quickdraw/Windows/Events, implements these
 * Only plain C types cross this boundary.
 */
#ifndef MACSYS_H
#define MACSYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Event kinds delivered by macsys_poll_event. Window-manager business
 * (drag, close-box tracking, BeginUpdate/EndUpdate) is handled entirely on
 * the Toolbox side; only game-relevant events cross the bridge. */
enum {
    MACSYS_EVT_NONE = 0,
    MACSYS_EVT_QUIT,      /* close box / cmd-Q */
    MACSYS_EVT_UPDATE,    /* window needs full re-blit */
    MACSYS_EVT_KEYDOWN,
    MACSYS_EVT_MOUSEDOWN, /* in content area; mx/my window-local */
    MACSYS_EVT_MOUSEUP
};

#define MACSYS_MOD_CMD    0x01u
#define MACSYS_MOD_SHIFT  0x02u
#define MACSYS_MOD_OPTION 0x04u
#define MACSYS_MOD_CTRL   0x08u

typedef struct MacSysEvent {
    int what;           /* MACSYS_EVT_* */
    int key_char;       /* Mac charCode (layout-dependent) */
    int key_code;       /* Mac virtual keycode (layout-independent; use for WKC map) */
    unsigned modifiers; /* MACSYS_MOD_* */
    int mx, my;         /* window-local mouse position */
} MacSysEvent;

/* Window + framebuffer (8bpp indexed). Returns 0 on failure. */
int  macsys_create_window(int w, int h, const char *title);
void macsys_destroy_window(void);
unsigned char *macsys_fb(int *pitch);

/* CopyBits one framebuffer rect to the window. */
void macsys_blit(int left, int top, int width, int height);

/* Install palette entries [first, first+count): rgb = count*3 bytes.
 * SetEntries on an 8-bit CLUT screen (palette animation is free there),
 * always updates the PixMap CTab so CopyBits maps correctly at any depth. */
void macsys_set_palette(int first, int count, const unsigned char *rgb);

/* Drain one event. Returns 0 when the queue is empty. */
int  macsys_poll_event(MacSysEvent *ev);

/* Polled input (classic Mac has no mouse-motion events). Mouse is window-local. */
void macsys_get_mouse(int *x, int *y, int *button_down);
unsigned macsys_get_dirkeys(void); /* OpenTTD order: 1=left 2=up 4=right 8=down */

unsigned long macsys_ticks(void);  /* TickCount(): 60.15 Hz */

/* Cooperative sleep: WaitNextEvent loop until the deadline; events that arrive
 * meanwhile are queued for macsys_poll_event. Also the hook the compat <thread>
 * shim's std::this_thread::sleep_for resolves to. */
void ottd_cooperative_sleep_us(unsigned long us);

#ifdef __cplusplus
}
#endif

#endif /* MACSYS_H */
