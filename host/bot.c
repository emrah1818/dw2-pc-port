/* dw2-pc-port test harness: a lock-step control channel for an external test driver
 * (tools/autoplay/). Off unless --bot IN OUT is given; then the game thread stops at each VBlank
 * wait whose budget has run out, reports, and takes commands until one lets it run on.
 *
 *   --bot IN OUT   read commands from file IN, write replies to file OUT (named pipes on Linux)
 *
 * Commands (one line each; every command gets one reply line):
 *   hello                  -> "base 0x<addr of Sys_State> wait <n> vb <n>"
 *   run N [BUTTONS]        hold BUTTONS (Host_PadParseButtons names, "-" = none) on port 0 for
 *                          N waits, then stop again       -> "ok wait <n> vb <n>" when stopped
 *   peek ADDR LEN          bytes at native address ADDR -> "hex <bytes>" (LEN <= 65536)
 *   poke ADDR HEX          write bytes (test set-up only) -> "ok"
 *   shot DIR TAG           display + VRAM screenshot pair -> "ok"
 *   quit                   quit the program
 * Addresses must lie inside the program's 16 MB game window (psyq/ps1mem.h): the driver turns a
 * symbol from `nm` into a run-time address with the Sys_State base. Nothing here changes the game
 * unless the driver pokes; buttons go through the normal input latch like --press. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "host/host.h"
#include "psyq/ps1mem.h"
#include "backend/psxgpu.h"

int Host_WritePngPacked(const char *path, const uint32_t *pixels, int w, int h);

extern u8 Sys_State[];

static FILE *in, *out;
static unsigned int budget;     /* waits left before the next stop */
static unsigned short bits;     /* buttons held while running */
static int active;

int Host_BotOpen(const char *in_path, const char *out_path) {
    in = fopen(in_path, "r");
    if (in == NULL) {
        fprintf(stderr, "[bot] cannot open %s\n", in_path);
        return -1;
    }
    out = fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "[bot] cannot open %s\n", out_path);
        return -1;
    }
    active = 1;
    printf("[bot] control channel open\n");
    return 0;
}

int Host_BotActive(void) {
    return active;
}

unsigned short Host_BotButtons(void) {
    return active ? bits : 0;
}

static int in_window(uintptr_t a, size_t n) {
    uintptr_t base = (uintptr_t)Ps1_Ram & ~(uintptr_t)0xFFFFFF;

    return n <= 65536 && a >= base && a + n <= base + 0x1000000 && a + n >= a;
}

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void reply(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Game thread, at each VBlank wait (Host_WaitVBlank), before the input latch. */
void Host_BotTick(unsigned int wait) {
    static char line[1 << 18];
    static char hex[2 * 65536 + 8];

    if (!active) {
        return;
    }
    if (budget > 0) {
        budget--;
        if (budget > 0) {
            return;
        }
        reply("ok wait %u vb %llu", wait, Host_VBlankCount());
    }
    bits = 0;
    for (;;) {
        char cmd[16] = "";
        if (fgets(line, sizeof(line), in) == NULL) {
            printf("[bot] channel closed\n");
            Host_Quit("--bot channel closed");
            return;
        }
        if (sscanf(line, "%15s", cmd) != 1) {
            continue;
        }
        if (strcmp(cmd, "hello") == 0) {
            reply("base 0x%llx wait %u vb %llu", (unsigned long long)(uintptr_t)Sys_State, wait,
                  Host_VBlankCount());
        } else if (strcmp(cmd, "run") == 0) {
            unsigned int n = 0;
            char names[96] = "-";

            sscanf(line, "%*s %u %95s", &n, names);
            bits = strcmp(names, "-") == 0 ? 0 : Host_PadParseButtons(names);
            if (n == 0) {
                n = 1;
            }
            budget = n;
            return;
        } else if (strcmp(cmd, "peek") == 0) {
            unsigned long long a = 0;
            unsigned int n = 0;
            const unsigned char *p;
            unsigned int i;

            sscanf(line, "%*s %llx %u", &a, &n);
            if (!in_window((uintptr_t)a, n)) {
                reply("err outside window");
                continue;
            }
            p = (const unsigned char *)(uintptr_t)a;
            for (i = 0; i < n; i++) {
                static const char d[] = "0123456789abcdef";
                hex[2 * i] = d[p[i] >> 4];
                hex[2 * i + 1] = d[p[i] & 15];
            }
            hex[2 * n] = 0;
            reply("hex %s", hex);
        } else if (strcmp(cmd, "poke") == 0) {
            unsigned long long a = 0;
            char *h;
            size_t n, i;
            unsigned char *p;

            if (sscanf(line, "%*s %llx", &a) != 1 || (h = strrchr(line, ' ')) == NULL) {
                reply("err syntax");
                continue;
            }
            h++;
            n = strcspn(h, "\r\n") / 2;
            if (!in_window((uintptr_t)a, n)) {
                reply("err outside window");
                continue;
            }
            p = (unsigned char *)(uintptr_t)a;
            for (i = 0; i < n; i++) {
                int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
                if (hi < 0 || lo < 0) {
                    break;
                }
                p[i] = (unsigned char)(hi << 4 | lo);
            }
            printf("[bot] poke 0x%llx %u bytes\n", a, (unsigned int)i);
            reply("ok");
        } else if (strcmp(cmd, "shot") == 0) {
            char dir[512], tag[64];

            if (sscanf(line, "%*s %511s %63s", dir, tag) != 2) {
                reply("err syntax");
                continue;
            }
            Host_SaveShot(dir, tag);
            reply("ok");
        } else if (strcmp(cmd, "hdshot") == 0) {
            /* the HD picture as the window would get it (soft renderer), margins included */
            char path[600];
            int w = 0, h = 0;
            uint32_t *px;

            if (sscanf(line, "%*s %599s", path) != 1) {
                reply("err syntax");
                continue;
            }
            if (!PsxGpu_ReadDisplayHd(NULL, &w, &h) || w <= 0 || h <= 0) {
                reply("err no HD picture (scale 1 and 4:3?)");
                continue;
            }
            px = malloc((size_t)w * h * 4);
            if (px == NULL || !PsxGpu_ReadDisplayHd(px, &w, &h) || !Host_WritePngPacked(path, px, w, h)) {
                free(px);
                reply("err hdshot failed");
                continue;
            }
            free(px);
            reply("ok %dx%d", w, h);
        } else if (strcmp(cmd, "quit") == 0) {
            reply("bye");
            Host_Quit("--bot quit");
            return;
        } else {
            reply("err unknown %s", cmd);
        }
    }
}
