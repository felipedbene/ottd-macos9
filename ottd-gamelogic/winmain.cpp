// Windowed front-end for the OpenTTD map-subsystem demo (Mac Toolbox side).
// Uses the same shell as the proven gfx demo so launch + trace-writing are known-good.
// Keeps Toolbox code OUT of the game TU (Point/Rect/Random clash).
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <TextEdit.h>
#include <string.h>

extern "C" void ottd_log_init(const char *path);
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);
extern "C" int  run_map_logic(char *summary, int cap);

// Draw a multi-line C string into the window starting at (x,y).
static void DrawLines(const char *s, short x, short y)
{
    short lineH = 14; MoveTo(x, y);
    const char *p = s; char buf[128]; int n = 0;
    for (;; p++) {
        if (*p == '\n' || *p == '\0') {
            DrawText(buf, 0, n); y += lineH; MoveTo(x, y); n = 0;
            if (*p == '\0') break;
        } else if (n < 127) buf[n++] = *p;
    }
}

int main()
{
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); InitCursor();

    ottd_log_init("ottd-maplog.txt");
    ottd_log("=== windowed map demo: main() reached ===");

    Rect wr; SetRect(&wr, 30, 50, 30 + 460, 50 + 260);
    WindowPtr win = NewCWindow(NULL, &wr, "\pOpenTTD map subsystem on Mac OS 9",
                               true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);
    ottd_log("window opened; calling run_map_logic ...");

    char summary[512]; summary[0] = '\0';
    int tiles = run_map_logic(summary, (int)sizeof(summary));
    ottd_log("run_map_logic returned: %d tiles", tiles);

    TextFont(4); TextSize(9);   // Monaco 9
    DrawLines("OpenTTD 13.4 REAL map code on PowerPC/Mac OS 9:", 12, 20);
    DrawLines(summary, 12, 44);
    DrawLines("Every height + slope computed by OpenTTD's own map.cpp.", 12, 190);
    DrawLines("Trace: ottd-maplog.txt   (click to quit)", 12, 210);

    ottd_log("drew summary; entering event loop");
    while (!Button()) {
        EventRecord ev;
        if (WaitNextEvent(everyEvent, &ev, 10, NULL) && (ev.what == keyDown || ev.what == mouseDown)) break;
    }
    ottd_log("=== exiting ===");
    ottd_log_close();
    return 0;
}
