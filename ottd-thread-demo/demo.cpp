#include "stdafx.h"
#include "thread.h"
#include "retro/Console.h"
#include <cstdio>

namespace retro { void InitConsole(); }

static int g_counter = 0;
static void heavy_job() { for (int i = 0; i < 5; i++) g_counter += i; }  // the "background" work

int main()
{
    retro::InitConsole();
    printf("\033]0;OpenTTD 13.4 thread.h on Mac OS 9\007");
    printf("OpenTTD 13.4 src/thread.h, compiled for PowerPC.\n\n");

    // This is EXACTLY OpenTTD's real pattern (e.g. linkgraphjob.cpp:63):
    //   if (!StartNewThread(&thr, name, fn)) { run synchronously }
    std::thread thr;
    bool threaded = StartNewThread(&thr, "ottd:demo", heavy_job);
    if (!threaded) {
        printf("StartNewThread -> false (no preemptive threads on OS 9)\n");
        printf("Cooperative fallback: running the job synchronously...\n");
        heavy_job();
    } else {
        printf("(threaded path -- not taken here)\n");
    }
    printf("job result g_counter = %d  (expected 10)\n\n", g_counter);
    printf("OpenTTD's threading seam runs cooperatively on real hardware.\n");
    printf("Type 'exit' + Return to quit.\n");

    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
