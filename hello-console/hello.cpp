#include "retro/Console.h"
#include <string>
#include <vector>
#include <cstdio>

namespace retro { void InitConsole(); }

int main()
{
    retro::InitConsole();

    // Exercise the C++17 stdlib on PowerPC classic Mac OS
    std::vector<std::string> words{"OpenTTD", "on", "Mac", "OS", "9"};
    std::string line;
    for (auto& w : words) { line += w; line += ' '; }

    printf("\033]0;OpenTTD PPC smoke test\007");   // window title
    printf("Hello from a PowerPC PEF built on arm64!\n\n");
    printf("std::vector<std::string> joined -> \"%s\"\n", line.c_str());
    printf("chars: %d   words: %d\n\n", (int)line.size(), (int)words.size());
    printf("If you can read this, the toolchain + libstdc++ work.\n");
    printf("Type 'exit' and press Return to quit.\n");

    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
