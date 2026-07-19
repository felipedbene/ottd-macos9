// File-read probe: prove we can read a real GRF off the Mac OS 9 disk through
// Retro68's newlib File Manager glue. This is the prerequisite for loading real
// OpenGFX sprites. It opens the GRF, dumps the container header, validates the
// NewGRF container-v2 magic, and reports the byte count.
#include "retro/Console.h"
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace retro { void InitConsole(); }

// NewGRF container v2 signature: "\0\0GRF\x82\r\n\x1a\n"
static const unsigned char kGRFMagic[10] = {0x00,0x00,'G','R','F',0x82,0x0D,0x0A,0x1A,0x0A};

// The GRF may sit next to the app or one folder over; try a few path spellings so
// we learn which convention Retro68's open() honours on this Mac in one run.
static const char *kCandidates[] = {
    "ogfx1_base.grf",
    ":ogfx1_base.grf",
    "::ogfx1_base.grf",
    "opengfx-8.0:ogfx1_base.grf",
};

int main()
{
    retro::InitConsole();
    printf("\033]0;OpenTTD GRF file-read probe\007");
    printf("Reading a real OpenGFX GRF off the Mac disk...\n\n");

    FILE *f = nullptr;
    const char *used = nullptr;
    for (unsigned i = 0; i < sizeof(kCandidates)/sizeof(kCandidates[0]); i++) {
        printf("  open(\"%s\") -> ", kCandidates[i]);
        f = fopen(kCandidates[i], "rb");
        if (f) { printf("OK\n"); used = kCandidates[i]; break; }
        printf("fail (errno=%d)\n", errno);
    }

    if (!f) {
        printf("\nCould not open the GRF by any path.\n");
        printf("Put ogfx1_base.grf in the SAME folder as this app and retry.\n");
    } else {
        unsigned char hdr[16];
        size_t n = fread(hdr, 1, sizeof(hdr), f);
        printf("\nRead %d header bytes from \"%s\":\n  ", (int)n, used);
        for (size_t i = 0; i < n; i++) printf("%02X ", hdr[i]);
        printf("\n");

        bool magic_ok = (n >= 10) && (memcmp(hdr, kGRFMagic, 10) == 0);
        printf("\nNewGRF container-v2 magic: %s\n", magic_ok ? "VALID  <-- real GRF!" : "NOT FOUND");

        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            printf("File size: %ld bytes (expected 2693610 for OpenGFX 8.0 base)\n", sz);
        }
        fclose(f);
        printf("\nIf the magic is VALID, the File Manager read seam WORKS:\n");
        printf("OpenTTD can now load real sprites off disk on Mac OS 9.\n");
    }

    printf("\nType 'exit' + Return to quit.\n");
    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
