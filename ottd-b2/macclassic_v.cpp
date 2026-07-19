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

/*
 * macclassic_v.cpp — OpenTTD side of the classic Mac OS video driver.
 * Talks to the Toolbox only through the macsys.h C bridge (TU-separation rule).
 * Permanent port file; destined for src/video/. Pacing comes from the REAL
 * VideoDriver::Tick()/SleepTillNextTick() in video_driver.cpp (single-threaded
 * path — StartNewThread fails under NO_THREADS) on the TickClock patch.
 */
#include "stdafx.h"
#include "openttd.h"
#include "gfx_func.h"
#include "blitter/factory.hpp"
#include "video/video_driver.hpp"
#include "window_func.h"

#include "macsys.h"

extern "C" void ottd_log(const char *fmt, ...);

static Palette _local_palette;

/** The classic Mac OS (QuickDraw) video driver. */
class VideoDriver_MacClassic : public VideoDriver {
	static const int MAX_RECTS = 16;
	struct { int left, top, width, height; } rects[MAX_RECTS];
	int num_rects = 0; // > MAX_RECTS means "everything dirty"
	int last_mx = -1, last_my = -1;

public:
	const char *Start(const StringList &param) override;
	void Stop() override { macsys_destroy_window(); }
	void MakeDirty(int left, int top, int width, int height) override;
	void MainLoop() override;
	bool ChangeResolution(int w, int h) override { return false; } // fixed window for now
	bool ToggleFullscreen(bool fullscreen) override { return false; }
	bool UseSystemCursor() override { return true; } // Mac arrow; OpenTTD sw-cursor later
	const char *GetName() const override { return "macclassic"; }

protected:
	void InputLoop() override;
	void Paint() override;
	void CheckPaletteAnim() override;
	bool PollEvent() override;
	Dimension GetScreenSize() const override { return { 640, 480 }; }

private:
	void PushPalette(int first, int count);
	uint MapKeycode(const MacSysEvent &ev, WChar *character);
};

/** Factory for the classic Mac video driver. */
class FVideoDriver_MacClassic : public DriverFactoryBase {
public:
	FVideoDriver_MacClassic() : DriverFactoryBase(Driver::DT_VIDEO, 10, "macclassic", "Classic Mac OS QuickDraw Video Driver") {}
	Driver *CreateInstance() const override { return new VideoDriver_MacClassic(); }
};
static FVideoDriver_MacClassic iFVideoDriver_MacClassic;

const char *VideoDriver_MacClassic::Start(const StringList &param)
{
	ottd_log("macclassic: Start()"); // bring-up: localize crashes between select and window
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	if (blitter == nullptr || blitter->GetScreenDepth() != 8) return "macclassic driver requires an 8bpp blitter";

	int w = 640, h = 480;
	if (!macsys_create_window(w, h, "OpenTTD")) return "could not create window/framebuffer";

	int pitch;
	_screen.dst_ptr = macsys_fb(&pitch);
	_screen.width = w;
	_screen.height = h;
	_screen.pitch = pitch;
	_cur_resolution.width = w;
	_cur_resolution.height = h;
	_fullscreen = false;

	blitter->PostResize();

	/* _cur_palette must already hold the game palette (scene init does this;
	 * the full game's GfxInitPalettes does the same). */
	CopyPalette(_local_palette, true);
	this->PushPalette(0, 256);

	GameSizeChanged();
	this->MakeDirty(0, 0, w, h);
	ottd_log("macclassic: started %dx%d pitch=%d", w, h, pitch);
	return nullptr;
}

void VideoDriver_MacClassic::PushPalette(int first, int count)
{
	static uint8 rgb[256 * 3];
	for (int i = 0; i < count; i++) {
		const Colour &c = _local_palette.palette[first + i];
		rgb[i * 3 + 0] = c.r;
		rgb[i * 3 + 1] = c.g;
		rgb[i * 3 + 2] = c.b;
	}
	macsys_set_palette(first, count, rgb);
}

void VideoDriver_MacClassic::MakeDirty(int left, int top, int width, int height)
{
	if (this->num_rects < MAX_RECTS) {
		this->rects[this->num_rects].left = left;
		this->rects[this->num_rects].top = top;
		this->rects[this->num_rects].width = width;
		this->rects[this->num_rects].height = height;
	}
	this->num_rects++;
}

void VideoDriver_MacClassic::Paint()
{
	int n = this->num_rects;
	if (n == 0) return;
	this->num_rects = 0;

	if (n > MAX_RECTS) {
		macsys_blit(0, 0, _screen.width, _screen.height);
	} else {
		for (int i = 0; i < n; i++) {
			macsys_blit(this->rects[i].left, this->rects[i].top, this->rects[i].width, this->rects[i].height);
		}
	}
}

void DoPaletteAnimations(); // gfx.cpp — advances the animated palette range (water shimmer)

void VideoDriver_MacClassic::CheckPaletteAnim()
{
	/* Advance the animated palette every draw tick. On an 8bpp CLUT screen the
	 * VIDEO_BACKEND path below re-pushes only the changed range via SetEntries, so
	 * water/lights shimmer for free — no framebuffer redraw needed. */
	DoPaletteAnimations();

	if (!CopyPalette(_local_palette)) return;

	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	switch (blitter->UsePaletteAnimation()) {
		case Blitter::PALETTE_ANIMATION_VIDEO_BACKEND:
			this->PushPalette(_local_palette.first_dirty, _local_palette.count_dirty);
			break;

		case Blitter::PALETTE_ANIMATION_BLITTER:
			blitter->PaletteAnimate(_local_palette);
			break;

		case Blitter::PALETTE_ANIMATION_NONE:
			break;

		default:
			NOT_REACHED();
	}
}

/** Mac virtual keycodes are layout-independent; map the navigation set. */
uint VideoDriver_MacClassic::MapKeycode(const MacSysEvent &ev, WChar *character)
{
	uint key = 0;
	switch (ev.key_code) {
		case 0x7B: key = WKC_LEFT; break;
		case 0x7C: key = WKC_RIGHT; break;
		case 0x7D: key = WKC_DOWN; break;
		case 0x7E: key = WKC_UP; break;
		case 0x35: key = WKC_ESC; break;
		case 0x24: key = WKC_RETURN; break;
		case 0x30: key = WKC_TAB; break;
		case 0x33: key = WKC_BACKSPACE; break;
		case 0x31: key = WKC_SPACE; break;
		case 0x75: key = WKC_DELETE; break;
		case 0x73: key = WKC_HOME; break;
		case 0x77: key = WKC_END; break;
		case 0x74: key = WKC_PAGEUP; break;
		case 0x79: key = WKC_PAGEDOWN; break;
		default:
			if (ev.key_char >= 'a' && ev.key_char <= 'z') key = 'A' + (ev.key_char - 'a');
			else if (ev.key_char >= 'A' && ev.key_char <= 'Z') key = (uint)ev.key_char;
			else if (ev.key_char >= '0' && ev.key_char <= '9') key = (uint)ev.key_char;
			break;
	}
	if (ev.modifiers & MACSYS_MOD_SHIFT)  key |= WKC_SHIFT;
	if (ev.modifiers & MACSYS_MOD_CTRL)   key |= WKC_CTRL;
	if (ev.modifiers & MACSYS_MOD_OPTION) key |= WKC_ALT;
	if (ev.modifiers & MACSYS_MOD_CMD)    key |= WKC_META;

	*character = (WChar)ev.key_char; // MacRoman ~= ASCII for the keys we map
	return key;
}

bool VideoDriver_MacClassic::PollEvent()
{
	MacSysEvent ev;
	if (!macsys_poll_event(&ev)) return false;

	switch (ev.what) {
		case MACSYS_EVT_QUIT:
			HandleExitGameRequest();
			break;

		case MACSYS_EVT_UPDATE:
			this->MakeDirty(0, 0, _screen.width, _screen.height);
			break;

		case MACSYS_EVT_KEYDOWN: {
			WChar character;
			uint keycode = this->MapKeycode(ev, &character);
			if (keycode != 0 || character != 0) HandleKeypress(keycode, character);
			break;
		}

		case MACSYS_EVT_MOUSEDOWN:
			_cursor.UpdateCursorPosition(ev.mx, ev.my);
			if (ev.modifiers & MACSYS_MOD_CTRL) {
				/* One-button mouse: ctrl+click = right click (OpenTTD's own emulation idiom). */
				_right_button_down = true;
				_right_button_clicked = true;
			} else {
				_left_button_down = true;
			}
			HandleMouseEvents();
			break;

		case MACSYS_EVT_MOUSEUP:
			_left_button_down = false;
			_left_button_clicked = false;
			_right_button_down = false;
			HandleMouseEvents();
			break;

		default:
			break;
	}
	return true;
}

void VideoDriver_MacClassic::InputLoop()
{
	_dirkeys = (byte)macsys_get_dirkeys();

	/* No mouse-motion events on classic Mac: poll at draw rate (the native idiom). */
	int x, y, down;
	macsys_get_mouse(&x, &y, &down);
	if (x != this->last_mx || y != this->last_my) {
		this->last_mx = x;
		this->last_my = y;
		_cursor.UpdateCursorPosition(x, y);
		HandleMouseEvents();
	}
}

void VideoDriver_MacClassic::MainLoop()
{
	this->StartGameThread(); // no-op: NO_THREADS makes StartNewThread fail -> inline game loop

	uint iter = 0;
	for (;;) {
		if (_exit_game) break;

		if (iter < 3 || (iter & 0x3FF) == 0) ottd_log("macclassic: loop iter %u", iter); // bring-up heartbeat
		iter++;
		this->Tick();
		this->SleepTillNextTick();
	}

	this->StopGameThread();
}
