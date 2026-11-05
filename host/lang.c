/* dw2-pc-port German language layer for the game's own texts (setting "language" = de).
 *
 * At start (after the pack is open and verified), the disc files with texts are copied, patched and
 * put into the mod layer of host/pak.c, so the game reads German where a German text exists. The
 * pack and its checksums stay as they are; English mode changes nothing.
 *
 *   fonts 0x13A (small) and 0x110 (big): the glyphs Ä Ö Ü ä ö ü ß are built from the player's own
 *     A O U a o u B glyphs (dots added) in cells no text uses (codes 0xE6..0xEC, checked empty)
 *   WAZADATA 0x25B: attack names from Languages/German/attacks.csv (english;german;...), matched
 *     by the decoded English name; a German name that does not fit the old place goes into the
 *     file's free tail. Descriptions stay English (no verified German texts).
 *   SYSTEXT 0x1FD: Digimon types Vaccine/Data/Virus -> Languages/German/types.csv (Serum/Datei/Virus)
 *
 * Every decision is logged ([lang]) and written to Languages/German/report.txt next to the exe:
 * which attacks were found in the game, which stayed English and why. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "host/devdata.h"
#include "host/host.h"
#include "host/settings.h"

#define F_SMALLFONT 0x13A
#define F_BIGFONT 0x110
#define F_WAZADATA 0x25B
#define F_SYSTEXT 0x1FD

/* the codes of the added glyphs (devdata.c decodes them back to UTF-8) */
enum { G_AE = 0xE6, G_OE, G_UE, G_ae, G_oe, G_ue, G_ss };

void Host_PakSetLayer(int id, const uint8_t *data, int size); /* host/pak.c */

static FILE *rep;

static void note(const char *fmt, const char *a, const char *b) {
    printf("[lang] ");
    printf(fmt, a, b);
    printf("\n");
    if (rep != NULL) {
        fprintf(rep, fmt, a, b);
        fprintf(rep, "\n");
    }
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ---- fonts ---- */

#define TIM_IMG (4 + 8 + 44 + 12) /* both font files: TIM at 4, 4-bit 128x256 (stride 64 bytes) */

static int px(const uint8_t *d, int x, int y) {
    return (d[TIM_IMG + y * 64 + x / 2] >> ((x & 1) * 4)) & 15;
}

static void setpx(uint8_t *d, int x, int y, int v) {
    uint8_t *b = &d[TIM_IMG + y * 64 + x / 2];
    *b = (uint8_t)((*b & ~(15 << ((x & 1) * 4))) | (v & 15) << ((x & 1) * 4));
}

typedef struct {
    int cols, cw, ch, gw, gh, page_split;
} Font;

static void cell_xy(const Font *f, int c, int *x, int *y) {
    if (f->page_split && c >= 0x88) {
        c -= 0x88; /* text.c: the big font's second page (same texture) */
    }
    *x = (c % f->cols) * f->cw;
    *y = (c / f->cols) * f->ch;
}

static int cell_empty(const uint8_t *d, const Font *f, int c) {
    int x0, y0;

    cell_xy(f, c, &x0, &y0);
    for (int y = y0; y < y0 + f->ch && y < 224; y++) {
        for (int x = x0; x < x0 + f->cw; x++) {
            if (px(d, x, y) != 0) {
                return 0;
            }
        }
    }
    return y0 + f->ch <= 224;
}

/* glyph `src` copied to cell `dst`, with two dots above it (umlaut) when dots */
static void make_glyph(uint8_t *d, const Font *f, int src, int dst, int dots) {
    int sx, sy, dx, dy, top = -1, xl = 99, xr = -1, cnt[16] = { 0 }, ink = 1, edge = 1, big = f->gw > 7;

    cell_xy(f, src, &sx, &sy);
    cell_xy(f, dst, &dx, &dy);
    for (int y = 0; y < f->ch; y++) {
        for (int x = 0; x < f->cw; x++) {
            int v = px(d, sx + x, sy + y);

            setpx(d, dx + x, dy + y, v);
            if (v) {
                cnt[v]++;
                if (top < 0) {
                    top = y;
                }
                xl = x < xl ? x : xl;
                xr = x > xr ? x : xr;
            }
        }
    }
    if (top < 0) {
        return;
    }
    if (!dots) {
        /* ß from B: the top left corner rounded, the bottom stroke opened next to the stem, so the
           stem runs down on its own (as a sharp s) */
        int last = -1;
        for (int y = 0; y < f->gh; y++) {
            for (int x = 0; x < f->cw; x++) {
                if (px(d, dx + x, dy + y) == 1) {
                    last = y;
                }
            }
        }
        setpx(d, dx + xl, dy + top, 0);
        for (int y = last - (big ? 1 : 0); y <= last + 1 && y < f->ch; y++) {
            for (int x = xl + 1; x <= xl + (big ? 2 : 1); x++) {
                setpx(d, dx + x, dy + y, 0);
            }
        }
        return;
    }
    for (int k = 1; k < 16; k++) {
        if (cnt[k] > cnt[ink]) {
            ink = k;
        }
    }
    edge = ink;
    for (int k = 1; k < 16; k++) {
        if (k != ink && cnt[k] > 0) {
            edge = k; /* the outline colour when the font has one */
        }
    }
    int dot_y = top - (big ? 3 : 2);
    if (dot_y < 0) {
        /* a capital letter at the top of its cell: make room by removing rows that repeat the row
           above (the straight middle of A, O, U), so the letter keeps its round top */
        int need = -dot_y;
        for (int k = 0; k < need; k++) {
            int bottom = f->gh - 1, cut = -1;

            for (int y = bottom; y > top && cut < 0; y--) {
                int same = 1;
                for (int x = 0; x < f->cw && same; x++) {
                    same = (px(d, dx + x, dy + y) != 0) == (px(d, dx + x, dy + y - 1) != 0); /* shape, not shade */
                }
                if (same) {
                    int empty = 1;
                    for (int x = 0; x < f->cw && empty; x++) {
                        empty = px(d, dx + x, dy + y) == 0;
                    }
                    if (!empty) {
                        cut = y;
                    }
                }
            }
            if (cut < 0) {
                cut = top; /* nothing repeats: the first row goes */
            }
            for (int y = cut; y > top; y--) {
                for (int x = 0; x < f->cw; x++) {
                    setpx(d, dx + x, dy + y, px(d, dx + x, dy + y - 1));
                }
            }
            for (int x = 0; x < f->cw; x++) {
                setpx(d, dx + x, dy + top, 0);
            }
            top++;
        }
        dot_y = top - (big ? 3 : 2);
        dot_y = dot_y < 0 ? 0 : dot_y;
    }
    int w = big ? 2 : 1;
    int a = xl + (big ? 1 : 1), b = xr - (big ? 2 : 1);
    if (b <= a + w) {
        a = xl;
        b = xr - w + 1;
    }
    for (int y = 0; y < w; y++) {
        for (int x = 0; x < w; x++) {
            setpx(d, dx + a + x, dy + dot_y + y, ink);
            setpx(d, dx + b + x, dy + dot_y + y, ink);
        }
    }
    (void)edge;
}

static int patch_font(uint8_t *d, int size, const Font *f, const char *name) {
    static const struct { int dst, src, dots; } g[7] = {
        { G_AE, 0x0A, 1 }, { G_OE, 0x18, 1 }, { G_UE, 0x1E, 1 },
        { G_ae, 0x24, 1 }, { G_oe, 0x32, 1 }, { G_ue, 0x38, 1 }, { G_ss, 0x0B, 0 },
    };

    if (size < TIM_IMG + 256 * 64 || rd32(d + 4) != 0x10 || rd32(d + 8) != 8) {
        note("%s font: unexpected format, umlauts off%s", name, "");
        return 0;
    }
    for (int i = 0; i < 7; i++) {
        if (!cell_empty(d, f, g[i].dst)) {
            note("%s font: cell for the umlauts is not empty, umlauts off%s", name, "");
            return 0;
        }
    }
    for (int i = 0; i < 7; i++) {
        make_glyph(d, f, g[i].src, g[i].dst, g[i].dots);
    }
    note("%s font: glyphs for Ä Ö Ü ä ö ü ß added (from the game's A O U a o u B)%s", name, "");
    return 1;
}

/* ---- text ---- */

/* UTF-8 -> game text (0xFF ends); -1 when a character has no glyph */
static int encode(const char *s, uint8_t *out, int max, int umlauts) {
    int n = 0;

    while (*s && n < max - 1) {
        unsigned char c = (unsigned char)*s;
        int g = -1;

        if (c >= '0' && c <= '9') {
            g = c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            g = 0x0A + c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            g = 0x24 + c - 'a';
        } else if (c == ' ') {
            g = 0xFD;
        } else if (c < 0x80) {
            static const char punct[] = "&?!/-,.'\";:%+=#";
            static const int codes[] = { 0x42, 0x44, 0x45, 0x46, 0x49, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D };
            const char *q = strchr(punct, c);
            g = q != NULL && c != 0 ? codes[q - punct] : -1;
        } else if (c == 0xC3 && s[1] != 0) {
            unsigned char c2 = (unsigned char)s[1];
            const char *plain = NULL;

            switch (c2) {
            case 0x84: g = G_AE; plain = "Ae"; break;
            case 0x96: g = G_OE; plain = "Oe"; break;
            case 0x9C: g = G_UE; plain = "Ue"; break;
            case 0xA4: g = G_ae; plain = "ae"; break;
            case 0xB6: g = G_oe; plain = "oe"; break;
            case 0xBC: g = G_ue; plain = "ue"; break;
            case 0x9F: g = G_ss; plain = "ss"; break;
            default: break;
            }
            s++;
            if (g >= 0 && !umlauts) {
                /* no umlaut glyphs (font check failed): the usual German spelling without them */
                for (int k = 0; k < 2 && n < max - 1; k++) {
                    char p = plain[k];
                    out[n++] = (uint8_t)(p >= 'a' ? 0x24 + p - 'a' : 0x0A + p - 'A');
                }
                s++;
                continue;
            }
        }
        if (g < 0) {
            return -1;
        }
        out[n++] = (uint8_t)g;
        s++;
    }
    out[n] = 0xFF;
    return *s ? -1 : n + 1;
}

/* ';' separated UTF-8 file: column 0 and 1 of each line after the header */
typedef struct {
    char en[64], de[64];
    int used;
} Pair;

static int load_pairs(const char *path, Pair *out, int max) {
    size_t size = 0;
    char *data = SDL_LoadFile(path, &size), *p, *line;
    int n = 0, first = 1;

    if (data == NULL) {
        return -1;
    }
    for (line = data; line != NULL && *line && n < max; line = p) {
        char *f1, *f2;

        p = strchr(line, '\n');
        if (p != NULL) {
            *p++ = 0;
        }
        if (first) {
            first = 0;
            continue;
        }
        f1 = strchr(line, ';');
        if (f1 == NULL) {
            continue;
        }
        *f1++ = 0;
        f2 = strchr(f1, ';');
        if (f2 != NULL) {
            *f2 = 0;
        }
        for (char *e = f1 + strlen(f1); e > f1 && (e[-1] == '\r' || e[-1] == ' '); e--) {
            e[-1] = 0;
        }
        if (line[0] == '"' || f1[0] == '"') {
            /* quoted fields (a name with ';' in it) are not used: none in the table */
            continue;
        }
        SDL_strlcpy(out[n].en, line, sizeof(out[n].en));
        SDL_strlcpy(out[n].de, f1, sizeof(out[n].de));
        out[n].used = 0;
        n++;
    }
    SDL_free(data);
    return n;
}

static int same_name(const char *a, const char *b) {
    /* case and spacing do not count ("Fist of Fate" = "Fist Of Fate") */
    while (*a || *b) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) {
            return 0;
        }
        if (*a) a++;
        if (*b) b++;
    }
    return 1;
}

/* free tail of a file: after its last nonzero byte, 4-aligned */
static uint32_t tail_start(const uint8_t *d, int size) {
    int e = size;

    while (e > 0 && d[e - 1] == 0) {
        e--;
    }
    return (uint32_t)((e + 8) & ~3);
}

/* string length incl. 0xFF at o */
static int slot_len(const uint8_t *d, int size, uint32_t o) {
    int n = 0;

    while ((int)(o + n) < size && d[o + n] != 0xFF) {
        n++;
    }
    return n + 1;
}

/* put text `t` (len bytes) for the pointer at `ptr_at`: in place when it fits, else in the tail */
static int place(uint8_t *d, int size, uint32_t ptr_at, const uint8_t *t, int len, uint32_t *tail) {
    uint32_t o = rd32(d + ptr_at);

    if (o < (uint32_t)size && len <= slot_len(d, size, o)) {
        memcpy(d + o, t, (size_t)len);
        return 1;
    }
    if (*tail + (uint32_t)len > (uint32_t)size) {
        return 0;
    }
    memcpy(d + *tail, t, (size_t)len);
    wr32(d + ptr_at, *tail);
    *tail += (uint32_t)len;
    return 1;
}

static void patch_attacks(uint8_t *d, int size, const char *dir, int umlauts) {
    static Pair pairs[400];
    char path[1024];
    int n, done = 0, kept = 0, failed = 0, game_skills = 0;
    uint32_t p0 = rd32(d), tail = tail_start(d, size);

    snprintf(path, sizeof(path), "%sattacks.csv", dir);
    n = load_pairs(path, pairs, 400);
    if (n < 0) {
        note("no %s: attack names stay English%s", path, "");
        return;
    }
    if (rep) fprintf(rep, "\n# Attacks in the game (id: English -> German, source)\n");
    for (uint32_t r = p0; r + 0x44 <= (uint32_t)size; r += 0x44) {
        int id = (int16_t)(d[r] | d[r + 1] << 8);
        uint32_t np = rd32(d + r + 0x24);
        char en[64];
        uint8_t enc[96];
        int k, len;

        if (id == 0) {
            break;
        }
        game_skills++;
        if (np >= (uint32_t)size) {
            continue;
        }
        DevData_Text(d + np, size - (int)np, NULL, en, sizeof(en));
        for (k = 0; k < n && !same_name(pairs[k].en, en); k++) {
        }
        if (k == n) {
            if (rep) fprintf(rep, "0x%02X: %s -> (not in attacks.csv, stays English)\n", id, en);
            kept++;
            continue;
        }
        pairs[k].used = 1;
        if (strcmp(pairs[k].de, pairs[k].en) == 0) {
            if (rep) fprintf(rep, "0x%02X: %s -> %s (no verified German name)\n", id, en, pairs[k].de);
            kept++;
            continue;
        }
        len = encode(pairs[k].de, enc, sizeof(enc), umlauts);
        if (len < 0 || !place(d, size, r + 0x24, enc, len, &tail)) {
            if (rep) fprintf(rep, "0x%02X: %s -> %s FAILED (%s), stays English\n", id, en, pairs[k].de,
                             len < 0 ? "a character the game font has no glyph for" : "no room in the file");
            failed++;
            continue;
        }
        if (rep) fprintf(rep, "0x%02X: %s -> %s\n", id, en, pairs[k].de);
        done++;
    }
    if (rep) {
        fprintf(rep, "\n# attacks.csv names not found in the game's attack table\n");
        for (int k = 0; k < n; k++) {
            if (!pairs[k].used) {
                fprintf(rep, "%s -> %s\n", pairs[k].en, pairs[k].de);
            }
        }
    }
    char a[64], b[64];
    snprintf(a, sizeof(a), "%d of %d", done, game_skills);
    snprintf(b, sizeof(b), "%d stay English, %d failed", kept, failed);
    note("attacks: %s German (%s)", a, b);
}

static void patch_types(uint8_t *d, int size, const char *dir, int umlauts) {
    static Pair pairs[8];
    char path[1024];
    int n;
    uint32_t tail = tail_start(d, size);

    snprintf(path, sizeof(path), "%stypes.csv", dir);
    n = load_pairs(path, pairs, 8);
    if (n < 0 || size <= 0xD0 * 4) {
        note("no %s: Digimon types stay English%s", path, "");
        return;
    }
    if (rep) fprintf(rep, "\n# Digimon types\n");
    for (int i = 0; i < 3; i++) {
        uint32_t at = (uint32_t)(0xC3 + i) * 4; /* devdata.c load_labels: entries 0xC3 + type */
        char en[32];
        uint8_t enc[32];

        DevData_Text(d + rd32(d + at), size - (int)rd32(d + at), NULL, en, sizeof(en));
        for (int k = 0; k < n; k++) {
            if (same_name(pairs[k].en, en)) {
                int len = encode(pairs[k].de, enc, sizeof(enc), umlauts);
                int ok = len > 0 && place(d, size, at, enc, len, &tail);
                if (rep) fprintf(rep, "%s -> %s%s\n", en, pairs[k].de, ok ? "" : " FAILED");
            }
        }
    }
    note("Digimon types: %s%s", "German names set (types.csv)", "");
}

/* Main thread, after Host_PakOpen and DevData_InitConst, before the game thread. */
/* --lang-dump DIR (tests): the patched font files as the game gets them */
static const char *dump_dir;
void Lang_SetDump(const char *dir) {
    dump_dir = dir;
}

static void dump(int id, const uint8_t *d, int size) {
    char path[1024];
    FILE *f;

    if (dump_dir == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%04X.bin", dump_dir, id);
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(d, 1, (size_t)size, f);
        fclose(f);
    }
}

void Lang_Apply(void) {
    const char *base = SDL_GetBasePath();
    char dir[1024], path[1024];
    int size = 0, umlauts = 0;
    uint8_t *d;

    if (Settings_Get(SET_LANGUAGE) != SET_LANG_DE) {
        return;
    }
    snprintf(dir, sizeof(dir), "%sLanguages/German/", base != NULL ? base : "");
    snprintf(path, sizeof(path), "%sreport.txt", dir);
    rep = fopen(path, "w");
    if (rep) fprintf(rep, "DW2 PC-Port: German language layer report (written at each start in German)\n");
    note("German game texts from %s%s", dir, "");

    {
        static const Font small = { 17, 7, 10, 7, 9, 0 }, big = { 13, 9, 14, 8, 13, 1 }; /* text.c */
        int ok = 1;

        d = Host_PakReadFileLayered(F_SMALLFONT, &size);
        if (d != NULL && (ok = patch_font(d, size, &small, "small"))) {
            Host_PakSetLayer(F_SMALLFONT, d, size);
            dump(F_SMALLFONT, d, size);
        }
        free(d);
        if (ok) {
            d = Host_PakReadFileLayered(F_BIGFONT, &size);
            if (d != NULL && (ok = patch_font(d, size, &big, "big"))) {
                Host_PakSetLayer(F_BIGFONT, d, size);
                dump(F_BIGFONT, d, size);
            }
            free(d);
        }
        umlauts = ok;
    }
    d = Host_PakReadFileLayered(F_WAZADATA, &size);
    if (d != NULL) {
        patch_attacks(d, size, dir, umlauts);
        Host_PakSetLayer(F_WAZADATA, d, size);
        free(d);
    }
    d = Host_PakReadFileLayered(F_SYSTEXT, &size);
    if (d != NULL) {
        patch_types(d, size, dir, umlauts);
        Host_PakSetLayer(F_SYSTEXT, d, size);
        free(d);
    }
    if (rep) {
        fclose(rep);
        rep = NULL;
    }
    fflush(stdout);
}
