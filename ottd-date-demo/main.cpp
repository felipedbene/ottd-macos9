#include "retro/Console.h"
#include <cstdio>
namespace retro { void InitConsole(); }
extern "C" void ottd_math_report(char *buf, int buflen);
int main()
{
    retro::InitConsole();
    printf("\033]0;OpenTTD math engine on Mac OS 9\007");
    printf("OpenTTD 13.4's REAL functions (src/core/math_func.cpp)\n");
    printf("running on PowerPC / Mac OS 9:\n\n");
    char buf[1024]; ottd_math_report(buf, sizeof buf);
    fputs(buf, stdout);
    printf("\nEvery number above was computed by OpenTTD's own code, unmodified.\n");
    printf("Type 'exit' + Return to quit.\n");
    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
