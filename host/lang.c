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
#include "host/fonts.h"
#include "host/host.h"
#include "host/settings.h"
#include "host/textscript.h"

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

static int encode(const char *s, uint8_t *out, int max, int umlauts);
static int g_umlauts;
/* host/picwords.c */
void PicWords_AddSkill(int32_t parts_entry, const char *de);
void PicWords_Apply(void);
void PicWords_SetGerman(int on);
int Lang_Encode(const char *s, uint8_t *out, int max) {
    return encode(s, out, max, g_umlauts);
}
void Lang_Note(const char *fmt, const char *a, const char *b) {
    note(fmt, a, b);
}

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
        } else if (c == '{') {
            /* tokens of the text inventory (Lang_ExportTexts): control codes kept as they were */
            const char *e = strchr(s, '}');
            unsigned v = 0;
            int t = -1;

            if (e == NULL) {
                return -1;
            }
            if (!strncmp(s, "{NL}", 4)) t = 0xFE;
            else if (!strncmp(s, "{PAGE}", 6)) t = 0xFC;
            else if (!strncmp(s, "{WAIT}", 6)) t = 0xFB;
            else if (!strncmp(s, "{TAMER}", 7)) { out[n++] = 0xF0; t = 0x00; }
            else if (!strncmp(s, "{BEETLE}", 8)) { out[n++] = 0xF0; t = 0x05; }
            else if (sscanf(s, "{ARG%u}", &v) == 1 && v < 5) { out[n++] = 0xF0; t = (int)v; }
            else if (sscanf(s, "{G:%x}", &v) == 1 && v < 0xEF) t = (int)v;
            else if (!strncmp(s, "{B:", 3)) {
                /* raw command bytes kept from the original (hex pairs) */
                const char *h = s + 3;
                while (h + 1 < e && n < max - 2) {
                    unsigned b;
                    if (sscanf(h, "%2x", &b) != 1) return -1;
                    out[n++] = (uint8_t)b;
                    h += 2;
                }
                s = e + 1;
                continue;
            }
            else if ((sscanf(s, "{DIGI:%u}", &v) == 1 || sscanf(s, "{ITEM:%u}", &v) == 1) && v < 1000 && n + 4 < max) {
                out[n++] = s[1] == 'D' ? 0xF1 : 0xF2;
                out[n++] = (uint8_t)(v / 100);
                out[n++] = (uint8_t)(v / 10 % 10);
                t = (int)(v % 10);
            }
            if (t < 0 || n >= max - 1) {
                return -1;
            }
            out[n++] = (uint8_t)t;
            s = e + 1;
            continue;
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
    if (*s || !TextScript_Valid(out, n + 1)) {
        return -1; /* cut off, or {B:..} bytes that are no whole command */
    }
    return n + 1;
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
    int n = TextScript_Len(d, size, o); /* walks command arguments (they may be 0xFF) */

    if (n > 0) {
        return n;
    }

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
        PicWords_AddSkill((int32_t)rd32(d + r + 0x0C), pairs[k].de); /* the big name in the effect */
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

/* ---- text inventory and the generic translation file ---- */

/* game text at `o` -> UTF-8 with tokens; returns 1 = plain text, 2 = contains script commands
 * (exported, not translatable yet), 0 = not text */
static int tokenize(const uint8_t *d, int size, uint32_t o, char *out, int n) {
    int len = 0, glyphs = 0, script = 0;

#define PUT(str) do { const char *q_ = (str); while (*q_ && len < n - 1) out[len++] = *q_++; } while (0)
    out[0] = 0;
    for (uint32_t i = o; (int)i < size; i++) {
        int c = d[i];
        char tmp[48];

        if (c == 0xFF) {
            out[len] = 0;
            return glyphs > 0 ? (script ? 2 : 1) : 0;
        }
        if (c < 10) { tmp[0] = (char)('0' + c); tmp[1] = 0; PUT(tmp); glyphs++; continue; }
        if (c < 0x24) { tmp[0] = (char)('A' + c - 10); tmp[1] = 0; PUT(tmp); glyphs++; continue; }
        if (c < 0x3E) { tmp[0] = (char)('a' + c - 0x24); tmp[1] = 0; PUT(tmp); glyphs++; continue; }
        if (c <= 0x5D || (c >= 0x5E && c <= 0xE5) || c == 0xED || c == 0xEE) {
            static const char punct[] = "&?!/-,.'\";:%+=#";
            static const int codes[] = { 0x42, 0x44, 0x45, 0x46, 0x49, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D };
            int k;
            for (k = 0; k < 15 && codes[k] != c; k++) {
            }
            if (k < 15) { tmp[0] = punct[k]; tmp[1] = 0; } else { snprintf(tmp, sizeof(tmp), "{G:%02X}", c); }
            PUT(tmp);
            glyphs++;
            continue;
        }
        switch (c) {
        case 0xFE: PUT("{NL}"); break;
        case 0xFD: PUT(" "); break;
        case 0xFC: PUT("{PAGE}"); break;
        case 0xFB: PUT("{WAIT}"); break;
        case 0xF0:
            if ((int)i + 1 >= size) return 0;
            c = d[++i];
            if (c == 0) PUT("{TAMER}");
            else if (c == 5) PUT("{BEETLE}");
            else if (c < 5) { snprintf(tmp, sizeof(tmp), "{ARG%d}", c); PUT(tmp); }
            else {
                uint8_t w[3] = { 0xF0, (uint8_t)c, 0xFF };
                DevData_Text(w, 3, NULL, tmp, sizeof(tmp)); /* the game's dictionary word, as text */
                PUT(tmp);
            }
            glyphs++;
            break;
        case 0xF1:
        case 0xF2:
            if ((int)i + 3 >= size) return 0;
            snprintf(tmp, sizeof(tmp), "{%s:%d}", c == 0xF1 ? "DIGI" : "ITEM", d[i + 1] * 100 + d[i + 2] * 10 + d[i + 3]);
            PUT(tmp);
            i += 3;
            glyphs++;
            break;
        case 0xEF:
            if ((int)i + 1 >= size) return 0;
            snprintf(tmp, sizeof(tmp), "{G:%02X}", 0xF0 + d[++i]);
            PUT(tmp);
            break;
        default:
            if (c >= 0xE6 && c <= 0xEC) { /* umlaut glyphs (German layer) */
                static const char *const um[] = { "\xC3\x84", "\xC3\x96", "\xC3\x9C", "\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F" };
                PUT(um[c - 0xE6]);
                glyphs++;
                break;
            }
            if (c == 0xF5) { PUT("{B:F5}"); break; } /* text.c: pause, no argument */
            if ((c == 0xF8 || c == 0xFA) && (int)i + 1 < size) { /* choice / window: one argument */
                snprintf(tmp, sizeof(tmp), "{B:%02X%02X}", c, d[i + 1]);
                PUT(tmp);
                i++;
                break;
            }
            if (c == 0xF9 && (int)i + 1 < size) { /* model: k, and 3 digits when k is even */
                int na = (d[i + 1] & 1) ? 1 : 4, k;
                if ((int)i + na >= size) return 0;
                PUT("{B:F9");
                for (k = 1; k <= na; k++) { snprintf(tmp, sizeof(tmp), "%02X", d[i + k]); PUT(tmp); }
                PUT("}");
                i += (uint32_t)na;
                break;
            }
            if (c == 0xF3 || c == 0xF4 || c == 0xF6 || c == 0xF7) {
                /* event commands (textscript.c): kept byte for byte, so a translation keeps them */
                int na = TextScript_ElemLen(d + i, size - (int)i), k;
                if (na == 0) return 0;
                PUT("{B:");
                for (k = 0; k < na; k++) { snprintf(tmp, sizeof(tmp), "%02X", d[i + k]); PUT(tmp); }
                PUT("}");
                i += (uint32_t)na - 1;
                script = 1;
                break;
            }
            return 0; /* a byte text never has */
        }
    }
    return 0;
#undef PUT
}

static void dump(int id, const uint8_t *d, int size);

typedef struct { int file; const char *what; int first, stride, ptr0, ptr1; } Table;

/* the text tables found so far: record tables (pointers inside records) and pointer lists */
static const Table k_tables[] = {
    { 0x45E, "item", -1, 0x10, 8, 12 },        /* ITEMDATA: name, description */
    { 0x25B, "attack", -1, 0x44, -1, 0x28 },   /* WAZADATA: description (names: attacks.csv) */
    { 0x1FD, "system", 0, 0, 0, -1 },          /* SYSTEXT: pointer list at the start */
};

/* calls fn(ptr_at, category) for every text pointer of table t in file d */
static void walk(const Table *t, const uint8_t *d, int size, void (*fn)(void *, const uint8_t *, int, uint32_t, const char *), void *ctx) {
    char cat[32];

    if (t->stride == 0) {
        uint32_t first = rd32(d);
        for (uint32_t a = 0; a + 4 <= first && a + 4 <= (uint32_t)size; a += 4) {
            fn(ctx, d, size, a, t->what);
        }
        return;
    }
    for (uint32_t r = rd32(d); r + (uint32_t)t->stride <= (uint32_t)size; r += (uint32_t)t->stride) {
        if ((int16_t)(d[r] | d[r + 1] << 8) == 0) {
            break;
        }
        if (t->ptr0 >= 0) {
            snprintf(cat, sizeof(cat), "%s name", t->what);
            fn(ctx, d, size, r + (uint32_t)t->ptr0, cat);
        }
        if (t->ptr1 >= 0) {
            snprintf(cat, sizeof(cat), "%s description", t->what);
            fn(ctx, d, size, r + (uint32_t)t->ptr1, cat);
        }
    }
}

typedef struct { FILE *f; int file, n, script; } ExportCtx;

static void export_one(void *vctx, const uint8_t *d, int size, uint32_t at, const char *cat) {
    ExportCtx *x = vctx;
    static char text[65536]; /* the longest dialogs are over 4 KB as tokens */
    uint32_t o = rd32(d + at);
    int k;

    if (o >= (uint32_t)size || (k = tokenize(d, size, o, text, sizeof(text))) == 0) {
        return;
    }
    for (char *c = text; *c; c++) {
        if (*c == '\t' || *c == '\n') *c = ' ';
    }
    fprintf(x->f, "%04X:%06X\t%s\t%s\t%s\n", x->file, at, cat, k == 2 ? "script" : "text", text);
    x->n++;
    x->script += k == 2;
}

static int is_known_table(int id) {
    for (unsigned t = 0; t < sizeof(k_tables) / sizeof(k_tables[0]); t++) {
        if (k_tables[t].file == id) return 1;
    }
    return 0;
}

#define MAX_DIALOG_SLOTS 4096

/* --export-texts DIR: DIR/texts_en.tsv (key, category, kind, English) and DIR/dialog_files.tsv
 * (every event table found: entries, texts, whether its string area is understood, free bytes).
 * From the player's disc: stays on the player's PC. */
int Lang_ExportTexts(const char *dir) {
    char path[1024];
    ExportCtx x = { 0 };
    FILE *ff;
    static uint32_t slots[MAX_DIALOG_SLOTS];
    int tables = 0, dialogs = 0, unclear = 0, lba, sectors, form2;
    unsigned long long fsize;

    SDL_CreateDirectory(dir);
    snprintf(path, sizeof(path), "%s/texts_en.tsv", dir);
    x.f = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/dialog_files.tsv", dir);
    ff = fopen(path, "w");
    if (x.f == NULL || ff == NULL) {
        if (x.f) fclose(x.f);
        if (ff) fclose(ff);
        return 1;
    }
    fprintf(x.f, "key\tcategory\tkind\tenglish\n");
    fprintf(ff, "file\tentries\ttexts\tlayout\ttext_bytes\tfree_bytes\n");
    for (unsigned t = 0; t < sizeof(k_tables) / sizeof(k_tables[0]); t++) {
        int size = 0;
        uint8_t *d = Host_PakReadFile(k_tables[t].file, &size);

        if (d == NULL) {
            continue;
        }
        x.file = k_tables[t].file;
        walk(&k_tables[t], d, size, export_one, &x);
        free(d);
    }
    /* event (dialog) tables: every data file that has the layout of Flag_SelectBranch's table */
    for (int id = 0; Host_PakFileInfo(id, &lba, &sectors, &form2, &fsize); id++) {
        int size = 0, n = 0, entries, area;
        uint8_t *d;
        TextScriptFlagLayout L;

        if (form2 || fsize < 16 || is_known_table(id) || (d = Host_PakReadFile(id, &size)) == NULL) {
            continue;
        }
        entries = TextScript_FlagTable(d, size, slots, MAX_DIALOG_SLOTS, &n);
        if (entries > 0) {
            int before = x.n;
            area = TextScript_FlagLayout(d, size, slots, n, &L);
            x.file = id;
            for (int i = 0; i < n && area; i++) { /* a file with another layout: no dialog (checked) */
                uint32_t rel = slots[i] - rd32(d);
                char cat[64];
                snprintf(cat, sizeof(cat), "dialog e%u b%u digi%d", rel / TS_FLAG_STRIDE,
                         (rel % TS_FLAG_STRIDE - TS_FLAG_BRANCHES) / 4,
                         (int16_t)(d[slots[i] - (rel % TS_FLAG_STRIDE)] | d[slots[i] - (rel % TS_FLAG_STRIDE) + 1] << 8));
                export_one(&x, d, size, slots[i], cat);
            }
            fprintf(ff, "%04X\t%d\t%d\t%s\t%u\t%u\n", id, entries, x.n - before,
                    area ? "ok" : "rejected (not the event table layout: no texts exported, file never changed)",
                    area ? L.text_end - TS_FLAG_HEADER : 0,
                    area ? (((uint32_t)size - (L.chunk_end - L.chunk_start)) & ~3u) - L.text_end : 0);
            tables += area;
            dialogs += x.n - before;
            unclear += !area;
        }
        free(d);
    }
    fclose(x.f);
    fclose(ff);
    printf("[lang] %d texts (%d with script commands; %d dialog texts in %d event tables, %d files "
           "rejected) -> %s/texts_en.tsv, dialog_files.tsv\n",
           x.n, x.script, dialogs, tables, unclear, dir);
    return 0;
}

/* All text pointers of table t (names too), into slots; returns the count. The end of the string
 * area: the records' start for record tables, the end of the last string for pointer lists. */
static int table_slots(const Table *t, const uint8_t *d, int size, uint32_t *slots, int max, uint32_t *area_end) {
    int n = 0;

    if (t->stride == 0) {
        uint32_t first = rd32(d), end = 0;
        for (uint32_t a = 0; a + 4 <= first && a + 4 <= (uint32_t)size && n < max; a += 4) {
            uint32_t o = rd32(d + a);
            if (o < (uint32_t)size) {
                slots[n++] = a;
                uint32_t e = o + (uint32_t)slot_len(d, size, o);
                end = e > end ? e : end;
            }
        }
        *area_end = end;
        return n;
    }
    *area_end = rd32(d);
    for (uint32_t r = rd32(d); r + (uint32_t)t->stride <= (uint32_t)size && n + 2 <= max; r += (uint32_t)t->stride) {
        if ((int16_t)(d[r] | d[r + 1] << 8) == 0) {
            break;
        }
        int name = t->file == 0x25B ? 0x24 : t->ptr0; /* WAZADATA names (attacks.csv) move too */
        if (name >= 0) slots[n++] = r + (uint32_t)name;
        if (t->ptr1 >= 0) slots[n++] = r + (uint32_t)t->ptr1;
    }
    return n;
}

/* The slots and string area of file `id` (a known table or an event table). 0 = not understood. */
static int file_area(int id, const uint8_t *d, int size, uint32_t *slots, int max, int *n,
                     uint32_t *start, uint32_t *end, uint32_t *tail) {
    for (unsigned t = 0; t < sizeof(k_tables) / sizeof(k_tables[0]); t++) {
        if (k_tables[t].file == id) {
            *n = table_slots(&k_tables[t], d, size, slots, max, end);
            *start = 0xFFFFFFFF;
            for (int i = 0; i < *n; i++) {
                uint32_t o = rd32(d + slots[i]);
                *start = o < *start ? o : *start;
            }
            *tail = tail_start(d, size);
            if (*tail > (uint32_t)size) *tail = (uint32_t)size;
            return *n > 0 && *start < *end && *tail >= *end;
        }
    }
    return 0; /* event tables: TextScript_FlagLayout */
}

/* ---- German word dictionary (F0 k, k = 73..255): text.c Text_BuiltinStrings[67..249] ---- */

extern uint8_t *Text_BuiltinStrings[];
#define DICT_GAME 67   /* the game's own words (English, k = 6..72) */
#define DICT_MAX 250
static uint8_t dict_word[DICT_MAX][48];
static int dict_len[DICT_MAX], dict_count;

/* Languages/German/dictionary.txt: one word (or word with a trailing space, written "_") per
 * line; they become dictionary entries 67.. for this run. Texts in the event tables are then
 * stored with F0 k instead of these letters (2 bytes for a word), like the game does in English. */
static void load_dict(const char *dir, int umlauts) {
    char path[1024];
    size_t sz = 0;
    char *data, *line, *next;
    int n = 0;

    for (int i = 0; i < DICT_GAME; i++) { /* the game's words count for compression too */
        int l = TextScript_Len(Text_BuiltinStrings[i], 48, 0);
        dict_len[i] = l > 1 ? l - 1 : 0;
        if (dict_len[i]) memcpy(dict_word[i], Text_BuiltinStrings[i], (size_t)dict_len[i]);
    }
    dict_count = DICT_GAME;
    snprintf(path, sizeof(path), "%sdictionary.txt", dir);
    data = SDL_LoadFile(path, &sz);
    if (data == NULL) {
        note("no %s: dialogs stored without German word dictionary%s", path, "");
        return;
    }
    for (line = data; line != NULL && *line && dict_count < DICT_MAX; line = next) {
        char w[64];
        uint8_t enc[64];
        int len;
        next = strchr(line, '\n');
        if (next != NULL) *next++ = 0;
        if (line[0] == '#' || line[0] == 0 || line[0] == '\r') continue;
        SDL_strlcpy(w, line, sizeof(w));
        for (char *c = w; *c; c++) {
            if (*c == '\r') *c = 0;
            else if (*c == '_') *c = ' ';
        }
        len = encode(w, enc, sizeof(enc), umlauts);
        if (len < 4 || len > 47) continue; /* at least 3 letters, or it saves nothing */
        memcpy(dict_word[dict_count], enc, (size_t)len); /* with its 0xFF end */
        dict_len[dict_count] = len - 1;
        Text_BuiltinStrings[dict_count] = dict_word[dict_count];
        dict_count++;
        n++;
    }
    SDL_free(data);
    {
        char a[32];
        snprintf(a, sizeof(a), "%d", n);
        note("dictionary.txt: %s German words%s", a, "");
    }
}

/* the words in the runs of glyphs (and spaces) of s become F0 k (longest match first);
 * commands and their arguments are never touched. Returns the new length. */
static int compress(uint8_t *s, int len) {
    static uint8_t out[16384];
    int i = 0, o = 0;

    if (dict_count == 0 || len > (int)sizeof(out)) return len;
    while (i < len) {
        int k = TextScript_ElemLen(s + i, len - i), best = -1, bl = 0;
        int glyph = k == 1 && (s[i] < 0xEF || s[i] == 0xFD);

        if (glyph) {
            for (int w = 0; w < dict_count; w++) {
                int l = dict_len[w], ok = l > 2 && l > bl && i + l <= len;
                for (int j = 0; ok && j < l; j++) {
                    ok = s[i + j] == dict_word[w][j] && (s[i + j] < 0xEF || s[i + j] == 0xFD);
                }
                if (ok) {
                    best = w;
                    bl = l;
                }
            }
        }
        if (best >= 0) {
            out[o++] = 0xF0;
            out[o++] = (uint8_t)(best + 6);
            i += bl;
        } else {
            if (k <= 0) k = 1;
            memcpy(out + o, s + i, (size_t)k);
            o += k;
            i += k;
        }
    }
    memcpy(s, out, (size_t)o);
    return o;
}

/* texts.tsv: key<TAB>German (lines starting with # are comments). Every file named by a key is
 * laid out again (textscript.c): a text that does not fit keeps its English string, and a file
 * whose layout is not understood is not touched at all. */
static void patch_texts(const char *dir, int umlauts) {
    char path[1024];
    size_t sz = 0;
    char *data, *copy, *line, *next;
    int ok = 0, bad = 0, nfiles = 0;
    static int files[512];
    static uint32_t slots[MAX_DIALOG_SLOTS];

    snprintf(path, sizeof(path), "%stexts.tsv", dir);
    data = SDL_LoadFile(path, &sz);
    if (data == NULL) {
        note("no %s: game texts stay English%s", path, "");
        return;
    }
    if (rep) fprintf(rep, "\n# texts.tsv\n");
    /* the files the keys name, in order of first use */
    copy = SDL_strdup(data);
    for (line = copy; line != NULL && *line; line = next) {
        unsigned file = 0, at = 0;
        int k;
        next = strchr(line, '\n');
        if (next != NULL) *next++ = 0;
        if (line[0] == '#' || sscanf(line, "%x:%x", &file, &at) != 2) continue;
        for (k = 0; k < nfiles && files[k] != (int)file; k++) {
        }
        if (k == nfiles && nfiles < 512) files[nfiles++] = (int)file;
    }
    SDL_free(copy);
    for (int fi = 0; fi < nfiles; fi++) {
        int id = files[fi], size = 0, count = 0, n = 0, placed = 0, fell;
        uint8_t *d = Host_PakReadFileLayered(id, &size);
        uint8_t **trans;
        int *tlen;
        uint32_t start, end, tail;

        if (d == NULL) {
            continue;
        }
        trans = calloc((size_t)size, sizeof(*trans));
        tlen = calloc((size_t)size, sizeof(*tlen));
        copy = SDL_strdup(data);
        for (line = copy; line != NULL && *line; line = next) {
            unsigned file = 0, at = 0;
            char *tab, *de;
            static uint8_t enc[16384];
            int len;

            next = strchr(line, '\n');
            if (next != NULL) *next++ = 0;
            if (line[0] == '#' || sscanf(line, "%x:%x", &file, &at) != 2 || (int)file != id ||
                (tab = strchr(line, '\t')) == NULL || at + 4 > (unsigned)size) {
                continue;
            }
            de = tab + 1;
            for (char *e = de + strlen(de); e > de && e[-1] == '\r'; e--) e[-1] = 0;
            len = encode(de, enc, sizeof(enc), umlauts);
            if (len > 0 && !is_known_table(id)) {
                len = compress(enc, len); /* event tables (dialogs) only: tables keep plain text */
            }
            if (len < 0) {
                bad++;
                if (rep) fprintf(rep, "%04X:%06X FAILED (a character without glyph or a bad token): %s\n", file, at, de);
                continue;
            }
            free(trans[at]);
            trans[at] = malloc((size_t)len);
            memcpy(trans[at], enc, (size_t)len);
            tlen[at] = len;
            count++;
        }
        SDL_free(copy);
        if (count > 0) {
            int flag = !is_known_table(id);
            TextScriptFlagLayout L;
            if (flag ? !(TextScript_FlagTable(d, size, slots, MAX_DIALOG_SLOTS, &n) > 0 &&
                         TextScript_FlagLayout(d, size, slots, n, &L))
                     : !file_area(id, d, size, slots, MAX_DIALOG_SLOTS, &n, &start, &end, &tail)) {
                bad += count;
                if (rep) fprintf(rep, "%04X: string area not understood, %d texts left English\n", id, count);
            } else {
                static uint8_t *tr[MAX_DIALOG_SLOTS];
                static int tl[MAX_DIALOG_SLOTS];
                int unknown = count;
                for (int i = 0; i < n; i++) {
                    tr[i] = trans[slots[i]];
                    tl[i] = tlen[slots[i]];
                    unknown -= tr[i] != NULL;
                }
                fell = flag ? TextScript_RepackFlag(d, size, slots, n, &L, tr, tl, &placed)
                            : TextScript_Repack(d, size, slots, n, start, end, tail, tr, tl, &placed);
                if (fell < 0) {
                    bad += count;
                    if (rep) fprintf(rep, "%04X: could not lay out the strings, %d texts left English\n", id, count);
                } else {
                    ok += placed;
                    bad += count - placed;
                    if (rep && fell) fprintf(rep, "%04X: %d texts did not fit (file full) or are invalid, English kept\n", id, fell);
                    if (rep && unknown) fprintf(rep, "%04X: %d keys are no text pointer of this file, ignored\n", id, unknown);
                    Host_PakSetLayer(id, d, size);
                    dump(id, d, size);
                }
            }
        }
        for (int i = 0; i < size; i++) free(trans[i]);
        free(trans);
        free(tlen);
        free(d);
    }
    SDL_free(data);
    char a[32], b[32];
    snprintf(a, sizeof(a), "%d", ok);
    snprintf(b, sizeof(b), "%d", bad);
    note("texts.tsv: %s texts German, %s failed (see report.txt)", a, b);
}

/* ---- word graphics ----
 * The small font holds some menu words as ready drawn pictures over several cells (codes 0x5E..0xE2,
 * e.g. "Counter Attack" in 10 cells). They are drawn again here with German words, made of the
 * font's own glyphs the way the game's pictures are made: each glyph trimmed to its ink, no gap,
 * a space 2 pixels. A word wider than its cells is squeezed by letting neighbours share a column
 * where no ink meets ink; when it still does not fit the English picture stays.
 * Languages/German/words.tsv (optional): lines "CODE<TAB>word" (CODE = first cell, hex) replace
 * the built-in words, an empty word keeps the English picture. */

static const struct { int code, cells; const char *de; } WORDS[] = {
    { 0x6D, 7, "Zustand" },       /* Condition */
    { 0x88, 7, "Digitieren" },    /* Digivolve */
    { 0x8F, 9, "DNA-Digit." },    /* DNA Digivolve */
    { 0x99, 7, "Autopilot" },     /* Auto Pilot */
    { 0xA0, 5, "Taste" },         /* Button */
    { 0xA5, 5, "Eltern" },        /* Parents */
    { 0xB1, 7, "Spezial." },      /* Specialty */
    { 0xBB, 7, "Wichtig" },       /* Important */
    { 0xC2, 10, "Gegenangriff" }, /* Counter Attack */
    { 0xCC, 5, "Kampf" },         /* Battle */
    { 0xD1, 5, "Kanone" },        /* Cannon */
    { 0xD6, 5, "Wählen" },        /* Choose */
    { 0xDD, 6, "Montage" },       /* Assemble */
    /* kept as they are German too: Digi-Beetle 0x5E, Digimon 0x66, Transfer 0x77, Digi-Line 0x7E,
       Generation 0xAA */
};
#define NWORDS ((int)(sizeof(WORDS) / sizeof(WORDS[0])))
#define WG_MAXW (10 * 7)
#define WG_MAXC 32

/* the word as a picture of `w` columns into img (ch rows of WG_MAXW); 1 when it fits */
static int compose_word(const uint8_t *d, const Font *f, const uint8_t *codes, int n, int w,
                        uint8_t img[][WG_MAXW]) {
    int x0[WG_MAXC], gw[WG_MAXC], over[WG_MAXC] = { 0 }, total = 0;

    for (int i = 0; i < n; i++) {
        x0[i] = 0;
        gw[i] = 2; /* space */
        if (codes[i] != 0xFD) {
            int cx, cy, l = 99, r = -1;
            cell_xy(f, codes[i], &cx, &cy);
            for (int x = 0; x < f->cw; x++) {
                for (int y = 0; y < f->ch; y++) {
                    if (px(d, cx + x, cy + y)) {
                        l = x < l ? x : l;
                        r = x > r ? x : r;
                    }
                }
            }
            if (r < 0) {
                return 0; /* an empty glyph */
            }
            x0[i] = l;
            gw[i] = r - l + 1;
        }
        total += gw[i];
    }
    /* squeeze: joints without ink on ink, the middle ones first */
    while (total > w) {
        int best = -1, bd = 999;
        for (int j = 1; j < n; j++) {
            int cx, cy, ax, ay, hit = 0;
            if (over[j] || codes[j] == 0xFD || codes[j - 1] == 0xFD) {
                continue;
            }
            cell_xy(f, codes[j - 1], &ax, &ay);
            cell_xy(f, codes[j], &cx, &cy);
            for (int y = 0; y < f->ch && !hit; y++) {
                hit = px(d, ax + x0[j - 1] + gw[j - 1] - 1, ay + y) && px(d, cx + x0[j], cy + y);
            }
            int dist = 2 * j > n ? 2 * j - n : n - 2 * j;
            if (!hit && dist < bd) {
                bd = dist;
                best = j;
            }
        }
        if (best < 0) {
            return 0;
        }
        over[best] = 1;
        total--;
    }
    memset(img, 0, sizeof(uint8_t) * WG_MAXW * (size_t)f->ch);
    for (int i = 0, x = 0; i < n; i++) {
        x -= over[i];
        if (codes[i] != 0xFD) {
            int cx, cy;
            cell_xy(f, codes[i], &cx, &cy);
            for (int y = 0; y < f->ch; y++) {
                for (int k = 0; k < gw[i]; k++) {
                    int v = px(d, cx + x0[i] + k, cy + y);
                    if (v && (img[y][x + k] == 0 || v == 1)) {
                        img[y][x + k] = (uint8_t)v; /* ink over the neighbour's shadow */
                    }
                }
            }
        }
        x += gw[i];
    }
    return 1;
}

static const char *word_override(const char *dir, int code, char *buf, int size) {
    char path[1024], line[256];
    const char *res = NULL;
    FILE *fp;

    snprintf(path, sizeof(path), "%swords.tsv", dir);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return NULL;
    }
    while (res == NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *tab = strchr(line, '\t');
        if (line[0] == '#' || tab == NULL || (int)strtol(line, NULL, 16) != code) {
            continue;
        }
        tab++;
        tab[strcspn(tab, "\r\n")] = 0;
        snprintf(buf, (size_t)size, "%s", tab);
        res = buf;
    }
    fclose(fp);
    return res;
}

/* Restyle word cells without changing their spelling. Honour words.tsv, including
 * an empty override (which previously meant keep the English word image). */
void Lang_RestyleWords(uint8_t *d, int size) {
    static const struct { int code, cells; const char *en; } words[] = {
        {0x5E,8,"Digi-Beetle"},{0x66,7,"Digimon"},{0x6D,7,"Condition"},
        {0x77,7,"Transfer"},{0x7E,10,"Digi-Line"},{0x88,7,"Digivolve"},
        {0x8F,9,"DNA Digivolve"},{0x99,7,"Auto Pilot"},{0xA0,5,"Button"},
        {0xA5,5,"Parents"},{0xAA,7,"Generation"},{0xB1,7,"Specialty"},
        {0xBB,7,"Important"},{0xC2,10,"Counter Attack"},{0xCC,5,"Battle"},
        {0xD1,5,"Cannon"},{0xD6,5,"Choose"},{0xDD,6,"Assemble"},
    };
    const char *base=SDL_GetBasePath();
    char dir[1024];
    snprintf(dir,sizeof(dir),"%sLanguages/German/",base?base:"");
    if(size<TIM_IMG+256*64)return;
    for(size_t i=0;i<sizeof(words)/sizeof(words[0]);i++) {
        uint8_t mask[70*9];
        char override[128];
        const char *text=words[i].en;
        if(Settings_Get(SET_LANGUAGE)==SET_LANG_DE) {
            for(int k=0;k<NWORDS;k++) if(WORDS[k].code==words[i].code)text=WORDS[k].de;
            const char *custom=word_override(dir,words[i].code,override,sizeof(override));
            if(custom)text=*custom?custom:words[i].en;
        }
        int width=words[i].cells*7;
        if(Fonts_Mask(0,text,mask,width,width,9)<0) {
            printf("[fonts] word %02X not restyled: %s\n",words[i].code,text);
            continue;
        }
        for(int c=0;c<words[i].cells;c++) {
            int code=words[i].code+c, x0=(code%17)*7,y0=(code/17)*10;
            for(int y=0;y<10;y++)for(int x=0;x<7;x++)
                setpx(d,x0+x,y0+y,y<9 && mask[y*width+c*7+x]>=80?1:0);
        }
    }
}

/* returns the number of words drawn */
static int patch_words(uint8_t *d, const Font *f, const char *dir, int umlauts) {
    int done = 0;

    for (int i = 0; i < NWORDS; i++) {
        char buf[128];
        const char *de = word_override(dir, WORDS[i].code, buf, sizeof(buf));
        uint8_t codes[WG_MAXC + 1], img[16][WG_MAXW];
        int n, w = WORDS[i].cells * f->cw, empty = 1;

        de = de != NULL ? de : WORDS[i].de;
        if (de[0] == 0) {
            continue;
        }
        for (int c = WORDS[i].code; c < WORDS[i].code + WORDS[i].cells; c++) {
            empty = empty && cell_empty(d, f, c);
        }
        n = encode(de, codes, WG_MAXC + 1, umlauts);
        if (empty || n <= 1) {
            note("word graphic %s: %s, English kept", de, empty ? "cells empty (unknown font)" : "a character without glyph");
            continue;
        }
        n--; /* the end code */
        for (int k = 0; k < n; k++) {
            if (codes[k] >= 0x5E && codes[k] != 0xFD && (codes[k] < G_AE || codes[k] > G_ss)) {
                n = 0; /* a command or a word graphic: no plain glyph */
            }
        }
        if (n == 0) {
            note("word graphic %s: only letters, digits and punctuation, English kept%s", de, "");
            continue;
        }
        if (!compose_word(d, f, codes, n, w, img)) {
            note("word graphic %s: too wide for its cells, English kept%s", de, "");
            continue;
        }
        for (int c = 0; c < WORDS[i].cells; c++) {
            int cx, cy;
            cell_xy(f, WORDS[i].code + c, &cx, &cy);
            for (int y = 0; y < f->ch; y++) {
                for (int x = 0; x < f->cw; x++) {
                    setpx(d, cx + x, cy + y, img[y][c * f->cw + x]);
                }
            }
        }
        done++;
    }
    return done;
}

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

    {
        /* Classic UI in English is the PS1 picture, bit for bit: no modern fonts there */
        Fonts_SetEnabled(!(Settings_Get(SET_LANGUAGE) != SET_LANG_DE && Settings_Get(SET_UI_STYLE) == SET_UI_CLASSIC));
    }
    if (Settings_Get(SET_LANGUAGE) != SET_LANG_DE) {
        if (Fonts_Enabled()) {
            Fonts_ApplyGame();
        }
        PicWords_SetGerman(0);
        if (Fonts_Enabled()) {
            PicWords_Apply();
        }
        { extern void UiPanels_Apply(int modern);
          UiPanels_Apply(Fonts_Enabled() && Settings_Get(SET_UI_STYLE) == SET_UI_MODERN); }
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
            char num[16];
            snprintf(num, sizeof(num), "%d", patch_words(d, &small, dir, 1));
            note("small font: %s word graphics in German", num, "");
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
    g_umlauts = umlauts;
    {
        Fonts_ApplyGame();
    }
    PicWords_SetGerman(1);
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
    load_dict(dir, umlauts);
    patch_texts(dir, umlauts); /* after the types: texts.tsv may not touch the type entries */
    PicWords_Apply(); /* after all text changes: render exactly the current names */
    { extern void UiPanels_Apply(int modern);
      UiPanels_Apply(Settings_Get(SET_UI_STYLE) == SET_UI_MODERN); } /* after the word pictures: their sheets keep their drawing */
    if (rep) {
        fclose(rep);
        rep = NULL;
    }
    fflush(stdout);
}

/* dw2-pc-port: a texture slot (src/main/texslot.c) gets a picture; DW2_TEXSLOT_LOG=1 prints them */
void Lang_TexSlotLoaded(int32_t id, int32_t slot, int32_t vx, int32_t vy) {
    static int log = -1;
    if (log < 0) {
        log = getenv("DW2_TEXSLOT_LOG") != NULL;
    }
    if (log) {
        printf("[texslot] %08X -> slot %d at %d,%d\n", (unsigned)id, (int)slot, (int)vx, (int)vy);
    }
}
