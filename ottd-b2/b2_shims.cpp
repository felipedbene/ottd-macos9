// B2 demo shims: the "fake game" behind the REAL video_driver.cpp Tick().
// Everything here is replaced by real OpenTTD TUs (openttd.cpp, window.cpp,
// progress.cpp) when the full game links. b1_shims.o still provides the rest
// of the stub surface (error/Debug/FioFOpenFile/settings/font stubs...).
#include "stdafx.h"
#include "openttd.h"
#include "gfx_func.h"
#include "driver.h"
#include "blitter/factory.hpp"
#include "video/video_driver.hpp"
#include "string_func.h"
#include "fileio_func.h"
#include <cstdio>
#include <cstdarg>

extern "C" void ottd_log(const char *fmt, ...);
extern "C" int  b2_scene_init(void);
extern "C" void r1_start_window_system(void);   // R1_MERGE: real main window (created after SelectDriver)
extern "C" void b2_scene_draw(int scroll_x, int scroll_y);
extern "C" void b2_scene_set_zoom(int z);
extern "C" int  b2_scene_get_zoom(void);
extern "C" void b2_scene_pick(int screen_x, int screen_y, int scroll_x, int scroll_y);
extern "C" void b2_scene_zoom_at(int cx, int cy, int old_sx, int old_sy, int *new_sx, int *new_sy);
#ifdef R1_MERGE
/* R1 render-merge: the real engine ticks live. r1_tick() advances the calendar +
 * grows the town one game tick; r1_is_live() is true once the town is seeded, which
 * makes UpdateWindows redraw every frame so the growth animates. Provided by
 * r1_scene.o; unused (and undeclared) in the plain B2 static demo build. */
extern "C" void r1_tick(void);
extern "C" int  r1_is_live(void);
extern "C" int  r1_take_dirty(void);   // 2 full redraw, 1 bus-only, 0 nothing
extern "C" int  r1_bus_draw(int sx, int sy, int *ox, int *oy, int *ow, int *oh);  // partial bus redraw
#endif

/* Game-state globals normally defined in openttd.cpp / progress.cpp. */
std::atomic<bool> _exit_game;
GameMode _game_mode;
SwitchMode _switch_mode;
PauseMode _pause_mode;
/* _game_speed + ChangeGameSpeed live in gfx.cpp (already linked). */
bool _in_modal_progress = false;
bool _rightclick_emulate = false;

void HandleExitGameRequest() { _exit_game = true; }
void GameSizeChanged() {}
void GameLoop() // real game tick: drives the live engine in the R1 merge
{
#ifdef R1_MERGE
	r1_tick();
#endif
}

#ifndef R1_MERGE  /* the REAL window.cpp owns UpdateWindows + the input loop in the render-merge build */
/* window.cpp's global input hooks — demo behaviour. */
void InputLoop() {}
void HandleCtrlChanged() {}

void HandleKeypress(uint keycode, WChar key)
{
	uint bare = keycode & ~WKC_SPECIAL_KEYS;
	ottd_log("B2: key 0x%x ('%c')", keycode, (key >= 32 && key < 127) ? (char)key : '?');
	if (bare == WKC_ESC || bare == 'Q') _exit_game = true;
}

void HandleMouseEvents()
{
	if (_left_button_down || _right_button_down) {
		ottd_log("B2: %s-click at %d,%d", _right_button_down ? "right" : "left", _cursor.pos.x, _cursor.pos.y);
	}
}

/* The demo "window system", all on the mouse (arrow keys don't survive VNC):
 *   drag            -> grab-and-throw pan, with inertia glide on release
 *   single tap      -> pick the tile under the cursor (log tx,ty/height/slope)
 *   double tap      -> cycle zoom NORMAL..OUT_8X
 * Arrow keys are still wired below, harmless when they never arrive. */
void UpdateWindows()
{
	static int sx = 0, sy = 0;
	static bool first = true;
	static bool dragging = false;
	static int drag_lx = 0, drag_ly = 0, moved = 0;
	static int last_mdx = 0, last_mdy = 0;   // last per-tick drag delta (fling seed)
	static int vx = 0, vy = 0;               // inertia velocity
	static unsigned frame = 0, last_tap = 0; // for double-tap detection
	frame++;

	int dx = 0, dy = 0;
	if (_dirkeys & 1) dx -= 8; // left
	if (_dirkeys & 4) dx += 8; // right
	if (_dirkeys & 2) dy -= 8; // up
	if (_dirkeys & 8) dy += 8; // down

	bool zoom_changed = false;
	bool btn = _left_button_down || _right_button_down;
	int mx = _cursor.pos.x, my = _cursor.pos.y;
	if (btn) {
		if (!dragging) { dragging = true; drag_lx = mx; drag_ly = my; moved = 0; vx = vy = 0; last_mdx = last_mdy = 0; } // press kills any glide
		else {
			int mdx = drag_lx - mx, mdy = drag_ly - my;
			dx += mdx; dy += mdy;
			moved += (mdx < 0 ? -mdx : mdx) + (mdy < 0 ? -mdy : mdy);
			last_mdx = mdx; last_mdy = mdy;
		}
		drag_lx = mx; drag_ly = my;
	} else if (dragging) {
		dragging = false;
		if (moved < 4) {                       // a tap
			b2_scene_pick(mx, my, sx, sy);     // #1: select tile under cursor
			if (frame - last_tap < 20) {       // within ~0.6s -> double tap = zoom at cursor
				int nsx, nsy;
				b2_scene_zoom_at(mx, my, sx, sy, &nsx, &nsy);
				sx = nsx; sy = nsy;
				zoom_changed = true;
			}
			last_tap = frame;
		} else {                               // a real drag -> fling with its exit velocity
			vx = last_mdx; vy = last_mdy;
		}
	} else if (vx != 0 || vy != 0) {           // inertia glide, decays ~1/8 per tick
		dx += vx; dy += vy;
		vx = (vx * 7) / 8; vy = (vy * 7) / 8;
		if (vx > -1 && vx < 1) vx = 0;
		if (vy > -1 && vy < 1) vy = 0;
	}

	/* R1: while the town is growing live, repaint the current view periodically so
	 * new houses/roads/trees appear over time. THROTTLED to ~1-in-8 frames: a full
	 * real-viewport redraw of the whole map (+ ~500 tree tiles + the HUD text layout)
	 * is heavy on PPC, and growth is slow (a house every ~12 ticks), so ~4 fps looks
	 * identical but cuts render load ~8x. Input (dx/dy/zoom) still redraws IMMEDIATELY
	 * below, so pan/drag/zoom stay fully responsive. */
	int dirt = 0;   // 2 = full redraw (a town grew), 1 = bus moved (partial), 0 = nothing
#ifdef R1_MERGE
	dirt = r1_take_dirty();
#endif
	bool full = first || dx != 0 || dy != 0 || zoom_changed || dirt == 2;
#ifdef R1_MERGE
	if (!full && dirt == 1) {   // bus-only frame: repaint just the region around it
		int ox, oy, ow, oh;
		if (r1_bus_draw(sx, sy, &ox, &oy, &ow, &oh) && ow > 0)
			VideoDriver::GetInstance()->MakeDirty(ox, oy, ow, oh);
	}
#endif
	if (full) {
		first = false;
		sx += dx; sy += dy;
		/* clamp scales with zoom: the field's on-screen extent is (virtual >> zoom),
		 * so bounds tuned at OUT_4X (z=2, ~+-650px) scale as 2600>>z / -400>>z. */
		int zc = b2_scene_get_zoom();
		int limx = 2600 >> zc, limy_hi = 2600 >> zc, limy_lo = -400 >> zc;
		if (sx < -limx) { sx = -limx; vx = 0; } else if (sx > limx) { sx = limx; vx = 0; }
		if (sy < limy_lo) { sy = limy_lo; vy = 0; } else if (sy > limy_hi) { sy = limy_hi; vy = 0; }
		static int logged = 0;
		if ((dx != 0 || dy != 0) && logged < 8) { logged++; ottd_log("B2: scroll d=(%d,%d) -> (%d,%d) dirkeys=%u", dx, dy, sx, sy, (unsigned)_dirkeys); }
		b2_scene_draw(sx, sy);
		VideoDriver::GetInstance()->MakeDirty(0, 0, _screen.width, _screen.height);
	}
}
#endif  /* !R1_MERGE — real window.cpp provides UpdateWindows/InputLoop/HandleMouseEvents/HandleKeypress/HandleCtrlChanged */

/* Minimal string/fileio utility shims pulled in by driver.cpp/video_driver.cpp
 * (real impls live in string.cpp/fileio.cpp, not linked in the demo). */
char *strecpy(char *dst, const char *src, const char *last)
{
	while (dst < last && *src != '\0') *dst++ = *src++;
	*dst = '\0';
	return dst;
}

int CDECL seprintf(char *str, const char *last, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int ret = vsnprintf(str, last - str + 1, format, ap);
	va_end(ap);
	return ret < 0 ? 0 : ret;
}

void FioFCloseFile(FILE *f) { fclose(f); }

std::string FioFindFullPath(Subdirectory subdir, const char *filename)
{
	/* Demo: files sit next to the app (CWD). Only claim a path if it exists —
	 * MarkVideoDriverOperational unlink()s whatever this returns for hwaccel.dat. */
	FILE *f = fopen(filename, "rb");
	if (f == nullptr) return {};
	fclose(f);
	return filename;
}

/* Diagnostics for the intermittent static-init anomaly (3rd sighting: empty
 * blitter registry in one run, populated in the next, same binary). */
static char s_bss_sentinel[2048]; // must be all-zero if .bss zeroing worked
static int s_data_sentinel = 0x5AA5C33C; // must hold its init value

/* Ctor-order telemetry: prove MaxApplZone (priority-101 ctor in b2main.c) ran
 * before the normal-priority static ctors that build the registries. */
extern "C" long b2_maxblock(void);
extern "C" long b2_mb_early_before, b2_mb_early_after;
static long s_mb_at_normal_ctor;
static struct CtorProbe {
	CtorProbe() { s_mb_at_normal_ctor = b2_maxblock(); }
} s_ctor_probe;

static void log_startup_state()
{
	int nz = 0;
	for (int i = 0; i < (int)sizeof(s_bss_sentinel); i++) {
		if (s_bss_sentinel[i] != 0) nz++;
	}
	char buf[300];
	char *end = BlitterFactory::GetBlittersInfo(buf, buf + sizeof(buf) - 1);
	for (char *p = buf; p != end; p++) {
		if (*p == '\n') *p = ' '; // keep it one UDP line
	}
	ottd_log("B2 diag: bss nonzero=%d/2048 data=%s blitters=[%s]",
	         nz, s_data_sentinel == 0x5AA5C33C ? "ok" : "CORRUPT", buf);
	ottd_log("B2 diag: ctor order: early MaxBlock %ld->%ld, at normal ctor %ld",
	         b2_mb_early_before, b2_mb_early_after, s_mb_at_normal_ctor);

	char dbuf[300];
	end = DriverFactoryBase::GetDriversInfo(dbuf, dbuf + sizeof(dbuf) - 1);
	for (char *p = dbuf; p != end; p++) {
		if (*p == '\n') *p = ' ';
	}
	ottd_log("B2 diag: drivers=[%s]", dbuf);
}

/* Game-side entry, called from the Mac-side main() after Toolbox init. */
extern "C" int b2_run(void)
{
	log_startup_state();
	if (b2_scene_init() != 0) return -1;

	ottd_log("B2: b2_run continues (setting refresh rate)");
	_settings_client.gui.refresh_rate = 30; // GetDrawInterval divides by this!

	/* Empty name = probe branch: finds macclassic by priority WITHOUT going
	 * through the named branch's std::istringstream (iostreams+locale are
	 * unproven on Retro68/newlib — don't gamble the demo on them). */
	ottd_log("B2: probing video driver");
	DriverFactoryBase::SelectDriver("", Driver::DT_VIDEO);
	VideoDriver *drv = VideoDriver::GetInstance();
	if (drv == nullptr) { ottd_log("B2: no video driver instance"); return -2; }

#ifdef R1_MERGE
	/* Create the real main window HERE — AFTER the video driver's Start() has sized
	 * _screen (640x480). Doing it in b2_scene_init (before SelectDriver) left _screen
	 * at 0x0, so the window/viewport came up 0x0 and painted nothing (noise). */
	r1_start_window_system();
#endif

	ottd_log("B2: entering MainLoop");
	drv->MainLoop();
	ottd_log("B2: MainLoop exited");
	drv->Stop();
	return 0;
}
