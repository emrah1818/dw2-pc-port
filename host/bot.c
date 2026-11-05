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
 *   dung                   the dungeon floor (stag4000 loaded, Dung_StatePtr set) -> "dung cols rows px py
 *                          floor CELLS ENTS": CELLS one char per cell, row by row ('.' walkable,
 *                          '#' not), ENTS "kind,x,y;..." of the live entities (2 / 3 exits)
 *   devedit SPEC           dev builds: queue a PD.4 state edit (host/devedit.c, e.g. warp=0x200,3) for
 *                          the next VBlank wait; it is applied before the game reads its state
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

#include "main/156C.h"
#if DW2_DEV || DW2_CHEATS
#include "host/devedit.h"
#endif

extern SysState Sys_State;
extern DungState *Dung_StatePtr;
extern GameState Save_GameState;
extern s32 Ovl_CurrentId;

/* include/stag4000/stag4000.h Stg40Cell (no pointers: same layout natively) */
typedef struct {
    u16 flags;
    u8 roomId;
    u8 wallBits;
} BotCell;

/* include/stag2000/stag2000.h Stg20MapFile (the fields the town command reads) */
typedef struct {
    s32 loaded, bgFileId, startRecs, exits;
} Stg20MapFileView;

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
            reply("base 0x%llx wait %u vb %llu", (unsigned long long)(uintptr_t)&Sys_State, wait,
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
        } else if (strcmp(cmd, "dung") == 0) {
            DungState *d = Dung_StatePtr;
            static char cells[256 * 256 + 1];
            static char ents[41 * 16 + 1];
            int cols, rows, x, y, n = 0, k;

            if (Ovl_CurrentId != 1 || d == NULL || d->floorHdr == NULL || d->cells == NULL || d->playerLoc == NULL) {
                reply("err no dungeon");
                continue;
            }
            cols = d->floorHdr->cols;
            rows = d->floorHdr->rows;
            if (cols <= 0 || rows <= 0 || cols > 256 || rows > 256) {
                reply("err floor size %d x %d", cols, rows);
                continue;
            }
            for (y = 0; y < rows; y++) {
                for (x = 0; x < cols; x++) {
                    u16 f = ((const BotCell *)d->cells)[cols * y + x].flags;
                    cells[n++] = (f & 0x8000) && (f & 0x30) != 0x20 ? '.' : '#';
                }
            }
            cells[n] = 0;
            n = 0;
            for (k = 0; k < 41 && n < (int)sizeof(ents) - 16; k++) {
                const Stg40Ent48 *e = &d->ents[k];

                if (e->flags & 0x8000) {
                    n += snprintf(ents + n, sizeof(ents) - (size_t)n, "%d,%d,%d;", e->kind, e->loc.u0.pair.field_0,
                                  e->loc.u0.pair.field_2);
                }
            }
            ents[n] = 0;
            {
                /* the player is the live entity with flag 1 (stag4000 obj.c); playerLoc as a fallback */
                const Stg40Loc *pl = d->playerLoc;

                for (k = 0; k < 41; k++) {
                    if ((d->ents[k].flags & 0x8001) == 0x8001) {
                        pl = &d->ents[k].loc;
                        break;
                    }
                }
                reply("dung %d %d %d %d %d %s %s", cols, rows, pl->u0.pair.field_0, pl->u0.pair.field_2, d->floor, cells,
                      n ? ents : "-");
            }
        } else if (strcmp(cmd, "devedit") == 0) {
#if DW2_DEV || DW2_CHEATS /* release: only with cheats on (DevEdit_Push refuses otherwise) */
            char spec[160];

            if (sscanf(line, "%*s %127s", spec + 32) != 1) {
                reply("err syntax");
                continue;
            }
            snprintf(spec, 32, "%u:", wait + 1); /* DevEdit_ScriptTick counts VBlank waits */
            memmove(spec + strlen(spec), spec + 32, strlen(spec + 32) + 1);
            reply(DevEdit_Script(spec) ? "ok" : "err bad spec");
#else
            reply("err devedit needs a dev build");
#endif
        } else if (strcmp(cmd, "town") == 0) {
            /* a city walking scene (table 3, stag2000): "town GRID PX PY EXITS": GRID 24x24 chars,
             * index [x * 24 + y] like Stg20_MapGrid[x][y] ('.' free, '#' wall, 'n' a walker,
             * 'P' the player), EXITS "x,y,mode,arg;..." (mode + 0x300 is the scene it leads to) */
            extern u8 Stg20_MapGrid[24][24];
            /* Cd_GetFileEntry: main/156C.h (EntA0 *) */
            static char grid[24 * 24 + 1];
            char exits[1024];
            int x, y, n = 0, px = -1, py = -1;
            const Stg20MapFileView *mi;

            /* the map file the scene already set up (Stg20_GetMapInfo without its first-use relocation) */
            if ((Sys_State.gameMode >> 8) != 3 || (Sys_State.gameMode & 0xFF) >= 0x20 ||
                (mi = (const Stg20MapFileView *)Cd_GetFileEntry((Sys_State.gameMode & 0xFF) + 0x308FFFF)) == NULL || !mi->loaded || mi->exits == 0) {
                reply("err no town");
                continue;
            }
            for (x = 0; x < 24; x++) {
                for (y = 0; y < 24; y++) {
                    u8 v = Stg20_MapGrid[x][y];

                    if (v & 0x80) {
                        px = x;
                        py = y;
                    }
                    grid[n++] = (v & 0x80) ? 'P' : (v & 0x40) ? 'n' : (v & 0x3F) ? '#' : '.';
                }
            }
            grid[n] = 0;
            n = 0;
            exits[0] = 0;
            for (const u8 *e = (const u8 *)(uintptr_t)(uint32_t)mi->exits; e[0] != 0 && n < (int)sizeof(exits) - 32; e += 4) {
                n += snprintf(exits + n, sizeof(exits) - (size_t)n, "%d,%d,0x%X,%d;", e[0], e[1], e[2] + 0x300, e[3]);
            }
            reply("town %s %d %d %s", grid, px, py, n ? exits : "-");
        } else if (strcmp(cmd, "nav") == 0) {
            /* the open city / area map pick: "nav INDEX id,mode,arg,x,y;..." (shown destinations) */
            extern void *Host_BotNav;
            const unsigned char *w = Host_BotNav;
            char recs[4096];
            int len = 0, i;

            if (w == NULL) {
                reply("nav none");
                continue;
            }
            recs[0] = 0;
            for (i = 0; i < 64; i++) {
                const unsigned char *r = w + 0x0C + i * 0x18;
                short id, mode, x, y;

                memcpy(&id, r, 2);
                if (id == -1) {
                    break;
                }
                memcpy(&mode, r + 0x10, 2);
                memcpy(&x, r + 0x14, 2);
                memcpy(&y, r + 0x16, 2);
                len += snprintf(recs + len, sizeof(recs) - len, "%s%d,0x%X,%d,%d,%d", i ? ";" : "", id,
                                (unsigned short)mode, r[0x12], x, y);
                if (len >= (int)sizeof(recs) - 64) {
                    break;
                }
            }
            reply("nav %d %s", *(const int *)(w + 8), i ? recs : "-");
        } else if (strcmp(cmd, "cheatundo") == 0) {
            extern void Cheat_Undo(void);
            Cheat_Undo(); /* dw2-pc-port: the F1 "Undo all cheat changes" (tests) */
            reply("ok");
        } else if (strcmp(cmd, "quit") == 0) {
            reply("bye");
            Host_Quit("--bot quit");
            return;
        } else {
            reply("err unknown %s", cmd);
        }
    }
}
