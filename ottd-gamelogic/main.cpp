// Console front-end for the OpenTTD map-subsystem demo on Mac OS 9.
#include "retro/Console.h"
#include <cstdio>
#include <string>

namespace retro { void InitConsole(); }

extern "C" void ottd_log_init(const char *path);
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);
extern "C" int  run_map_logic(char *summary, int cap);

int main()
{
    retro::InitConsole();
    ottd_log_init("ottd-maplog.txt");     // trace lands on the AFP share

    printf("\033]0;OpenTTD map subsystem on Mac OS 9\007");
    printf("Running OpenTTD 13.4's REAL map subsystem\n");
    printf("(map.cpp + tile_map.cpp) on PowerPC / Mac OS 9...\n\n");

    ottd_log("=== OpenTTD game-logic demo starting ===");
    char summary[512];
    int tiles = run_map_logic(summary, (int)sizeof(summary));
    ottd_log("=== done: %d tiles ===", tiles);
    ottd_log_close();

    fputs(summary, stdout);
    printf("\nEvery tile height + slope above was computed by OpenTTD's own\n");
    printf("map code (AllocateMap / GetTileSlope), unmodified, on Mac OS 9.\n");
    printf("Full trace written to ottd-maplog.txt next to this app.\n\n");
    printf("Type 'exit' + Return to quit.\n");

    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
