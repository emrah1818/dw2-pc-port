/* dw2-pc-port: German words in the game's ready-made word pictures (no game data here).
 *
 * Battle banners ("BATTLE", "Attack", "Damage", "WIN" ...), the title menu ("PRESS START BUTTON",
 * "NEW GAME" ...) and the big attack names in the battle effects are pictures in the textures of
 * the game, drawn by part layouts (cell lists: which rectangle of the texture goes where). With
 * German on, each such picture is painted again at start, in the player's own data (pak layer,
 * nothing stored or shipped):
 *
 *   - the rectangles come from the picture's own part layout (file, entry), in screen order,
 *   - the look is learned from the original picture: for every row and every distance from the
 *     picture's outer edge (0 = edge pixel, 1, 2 ...) the most common palette index is taken (the
 *     outline, the gradient inside and the drop shadow are all such distance / row patterns),
 *   - the German text is set from the big game font (the German layer's umlauts included), scaled
 *     to the picture's height, squeezed to its width when needed, slanted for italic pictures, and
 *     grown by the learned outline width,
 *   - every pixel gets the learned index for its row and distance.
 * The timing, the animation and the palette of the pictures stay as they are.
 *
 * Status messages of the battle (file 0x22B: "Poisoned", "Attack power up" ...) are not pictures
 * but rows of letter cells; they get German cell lists instead (Lang_PartCells, at draw time).
 * Its font has no umlauts: they are written ae / oe / ue / ss there. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"

void *Host_PakReadFileLayered(int id, int *size);
void Host_PakSetLayer(int id, const uint8_t *data, int size);
void Lang_Note(const char *fmt, const char *a, const char *b);
int Lang_Encode(const char *s, uint8_t *out, int max); /* host/lang.c: UTF-8 -> game glyph codes */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int16_t rd16(const uint8_t *p) {
    return (int16_t)(p[0] | p[1] << 8);
}

/* ---- the texture of a file (entry 0: a TIM, 4 or 8 bit) ---- */

typedef struct {
    uint8_t *d;
    int size;
    uint32_t img; /* first pixel byte */
    int stride;   /* bytes per row */
    int w, h;     /* in pixels */
    int bits;     /* 4 or 8 */
} Tex;

static int entries(const uint8_t *d, int size) {
    uint32_t first;
    if (size < 8) {
        return 0;
    }
    first = rd32(d);
    if (first < 4 || first >= (uint32_t)size || first % 4) {
        return 0;
    }
    return (int)(first / 4);
}

static uint32_t entry(const uint8_t *d, int size, int i) {
    int n = entries(d, size);
    uint32_t lo = 0xFFFFFFFFu;
    for (int k = 0; k < n; k++) {
        uint32_t o = rd32(d + 4 * k);
        if (o != 0 && o < lo) {
            lo = o; /* entries may be 0 (unused) */
        }
    }
    if (lo == 0xFFFFFFFFu || lo > (uint32_t)size) {
        return 0;
    }
    n = (int)(lo / 4); /* the table ends where the first entry starts */
    if (i < 0 || i >= n) {
        return 0;
    }
    return rd32(d + 4 * i);
}

static int tex_open(Tex *t, uint8_t *d, int size) {
    uint32_t o = entry(d, size, 0), p, flags, len;
    int iw;

    memset(t, 0, sizeof(*t));
    if (o == 0 || o + 20 > (uint32_t)size || rd32(d + o) != 0x10) {
        return 0;
    }
    flags = rd32(d + o + 4);
    if ((flags & 7) > 1) {
        return 0;
    }
    p = o + 8;
    if (flags & 8) {
        len = rd32(d + p);
        if (len < 12 || p + len >= (uint32_t)size) {
            return 0;
        }
        p += len;
    }
    if (p + 12 > (uint32_t)size) {
        return 0;
    }
    iw = (uint16_t)rd16(d + p + 8);
    t->h = (uint16_t)rd16(d + p + 10);
    t->stride = iw * 2;
    t->bits = (flags & 7) ? 8 : 4;
    t->w = (flags & 7) ? iw * 2 : iw * 4;
    t->img = p + 12;
    t->d = d;
    t->size = size;
    if (t->img + (uint32_t)(t->stride * t->h) > (uint32_t)size || t->w <= 0 || t->h <= 0) {
        return 0;
    }
    return 1;
}

static int tget(const Tex *t, int x, int y) {
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) {
        return 0;
    }
    if (t->bits == 8) {
        return t->d[t->img + y * t->stride + x];
    }
    return (t->d[t->img + y * t->stride + x / 2] >> ((x & 1) * 4)) & 15;
}

static void tset(Tex *t, int x, int y, int v) {
    uint8_t *b;
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) {
        return;
    }
    if (t->bits == 8) {
        t->d[t->img + y * t->stride + x] = (uint8_t)v;
        return;
    }
    b = &t->d[t->img + y * t->stride + x / 2];
    *b = (uint8_t)((*b & ~(15 << ((x & 1) * 4))) | (v & 15) << ((x & 1) * 4));
}

/* ---- the rectangles of a part layout (cells 0x10: u v x y clutX clutY w h row blend frame) ---- */

typedef struct {
    int u, v, w, h; /* in the texture */
    int x, y;       /* on screen, relative to the part */
} Cell;

#define MAX_CELLS 24

static int cells_of(const uint8_t *d, int size, int e, Cell *out) {
    uint32_t o = entry(d, size, e);
    int n = 0;

    if (o == 0) {
        return 0;
    }
    for (int k = 0; o + 16 <= (uint32_t)size && d[o] != 0xFF && k < 64; k++, o += 16) {
        Cell c = { d[o], d[o + 1], d[o + 8], d[o + 9] ? d[o + 9] : 256, rd16(d + o + 2), rd16(d + o + 4) };
        int dup = 0;
        if (d[o + 12] != 0) {
            continue; /* frame 0 only */
        }
        for (int i = 0; i < n && !dup; i++) {
            dup = out[i].u == c.u && out[i].v == c.v && out[i].w == c.w && out[i].x == c.x && out[i].y == c.y;
        }
        if (!dup && n < MAX_CELLS) {
            out[n++] = c;
        }
    }
    /* screen order */
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && out[j].x < out[j - 1].x; j--) {
            Cell t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}

/* ---- painting ---- */

#define CW 512
#define CH 64
#define MAXD 10

typedef struct {
    const char *text;
    int italic;  /* slant to the right */
    int align;   /* 0 centre, -1 left, 1 right */
    int narrow;  /* letters of the small game font (narrow, like the title's pictures) */
    int grow;    /* width (texture pixels) the picture may grow to, 0 = keep */
    int move;    /* 1: when it cannot grow in place, move it to an empty part of the texture */
} Words;

/* ---- letter shapes: the font of the battle status messages (file 0x22B, 12 x 15 cells, its
   letter body in palette indices 3 and 4), with umlauts and a few signs added ---- */

static uint8_t *font22;
static int font22_size;
static Tex font22_tex;
static int status_glyph(int ch, int *u, int *v);

/* glyph g of the text as a 12 x 15 mask; 0 when there is none */
static int letter(const char *g, uint8_t out[15][16]) {
    int ch = (unsigned char)g[0], u, v, dots = 0;

    memset(out, 0, 15 * 16);
    if (ch == 0xC3) { /* UTF-8 umlauts */
        static const char base[] = { 'A', 'O', 'U', 'a', 'o', 'u' };
        static const unsigned char cod[] = { 0x84, 0x96, 0x9C, 0xA4, 0xB6, 0xBC };
        for (int k = 0; k < 6; k++) {
            if ((unsigned char)g[1] == cod[k]) {
                ch = base[k];
                dots = 1;
            }
        }
        if (!dots) {
            return 0;
        }
    }
    if (ch == '!' || ch == '-' || ch == '.' || ch == '?' || ch == ':') {
        for (int y = 2; y < 13; y++) {
            for (int x = 4; x < 8; x++) {
                int on = 0;
                if (ch == '!') on = y < 9 || y >= 11;
                if (ch == '-') on = y >= 6 && y < 9;
                if (ch == '.') on = y >= 10;
                if (ch == ':') on = (y >= 4 && y < 6) || y >= 10;
                if (ch == '?') on = (y < 4) || (x >= 6 && y < 7) || (y >= 6 && y < 8 && x < 7) || y >= 10;
                out[y][x] = (uint8_t)on;
            }
        }
        return 1;
    }
    if (font22_tex.d == NULL || !status_glyph(ch, &u, &v)) {
        return 0;
    }
    {
        /* the letter body (indices 3, 4) connected to the middle of its cell: wide letters (M, W)
           run past the 12-column cell, the neighbours stay out */
        uint8_t b[15][20];
        int changed = 1;
        memset(b, 0, sizeof(b));
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 20; x++) {
                int k = tget(&font22_tex, u - 2 + x, v + y);
                b[y][x] = (uint8_t)(k == 3 || k == 4);
            }
        }
        for (int y = 0; y < 15; y++) for (int x = 4; x < 12; x++) if (b[y][x]) b[y][x] = 2;
        while (changed) {
            changed = 0;
            for (int y = 0; y < 15; y++) for (int x = 0; x < 20; x++) {
                if (b[y][x] != 1) continue;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < 15 && xx >= 0 && xx < 20 && b[yy][xx] == 2 && b[y][x] == 1) { b[y][x] = 2; changed = 1; }
                }
            }
        }
        for (int y = 0; y < 15; y++) for (int x = 0; x < 16; x++) out[y][x] = (uint8_t)(b[y][x + 2] == 2);
    }
    if (ch == 'M' || ch == 'N') {
        /* this font's M (a closed arch with a short middle stem) and N (a one-pixel diagonal) read
           as A and H once enlarged: the same 2-pixel strokes as the other capitals, with the
           diagonals drawn out (rows 2..12 like the font's capitals) */
        static const char *const gm[11] = {
            ".##......##.", ".###....###.", ".####..####.", ".##.####.##.", ".##..##..##.", ".##......##.",
            ".##......##.", ".##......##.", ".##......##.", ".##......##.", ".##......##." };
        static const char *const gn[11] = {
            ".##....##...", ".###...##...", ".####..##...", ".##.#..##...", ".##.##.##...", ".##..#.##...",
            ".##..####...", ".##...###...", ".##....##...", ".##....##...", ".##....##..." };
        const char *const *g2 = ch == 'M' ? gm : gn;
        memset(out, 0, 15 * 16);
        for (int y = 0; y < 11; y++) {
            for (int x = 0; x < 12; x++) {
                out[y + 2][x] = (uint8_t)(g2[y][x] == '#');
            }
        }
    }

    if (dots) {
        /* the letter moved down to make room where needed, two dots above it */
        int top = 15, l = 12, r = -1;
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 16; x++) {
                if (out[y][x]) {
                    top = y < top ? y : top;
                    l = x < l ? x : l;
                    r = x > r ? x : r;
                }
            }
        }
        if (top < 4) { /* a capital: squeeze it into the rows below 4 */
            uint8_t t[15][16];
            int bottom = 0;
            for (int y = 0; y < 15; y++) {
                for (int x = 0; x < 16; x++) {
                    if (out[y][x]) bottom = y;
                }
            }
            memset(t, 0, sizeof(t));
            for (int y = 4; y <= bottom; y++) {
                int sy = top + (y - 4) * (bottom - top + 1) / (bottom - 3);
                memcpy(t[y], out[sy > bottom ? bottom : sy], 12);
            }
            memcpy(out, t, sizeof(t));
            top = 4;
        }
        for (int y = top - 3; y < top - 1; y++) {
            for (int x = 0; x < 16; x++) {
                if (y >= 0 && ((x >= l + 1 && x < l + 3) || (x > r - 3 && x <= r - 1))) {
                    out[y][x] = 1;
                }
            }
        }
    }
    return 1;
}

static uint8_t *smallfont;
static int smallfont_size;

/* a letter of the small game font (7 x 10 cells, German layer included) as a 12 x 15 mask in its
   top left 7 x 10 */
static int letter_small(const char *g, uint8_t out[15][16], int *len) {
    uint8_t codes[8];
    int n, c, ink = 1;
    const int TIM_IMG = 4 + 8 + 44 + 12;
    char one[8] = { 0 };

    *len = ((unsigned char)*g & 0xE0) == 0xC0 ? 2 : 1;
    memcpy(one, g, (size_t)*len);
    memset(out, 0, 15 * 16);
    n = Lang_Encode(one, codes, 8);
    if (n != 2 || smallfont == NULL) {
        return 0;
    }
    c = codes[0];
    {
        static int found = -1;
        if (found < 0) {
            int cnt[16] = { 0 };
            for (int k = 0x0A; k < 0x24; k++) for (int y = 0; y < 10; y++) for (int x = 0; x < 7; x++) {
                int gx = (k % 17) * 7 + x, gy = (k / 17) * 10 + y, o = TIM_IMG + gy * 64 + gx / 2;
                if (o < smallfont_size) cnt[(smallfont[o] >> ((gx & 1) * 4)) & 15]++;
            }
            /* the letter colour, not its shadow: the top-left corner of E (code 0x0E) */
            {
                int gx = (0x0E % 17) * 7, gy = (0x0E / 17) * 10 + 1, o = TIM_IMG + gy * 64 + gx / 2;
                found = o < smallfont_size ? (smallfont[o] >> ((gx & 1) * 4)) & 15 : 1;
                if (found == 0) found = 1;
            }
            (void)cnt;
        }
        ink = found;
    }
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 7; x++) {
            int gx = (c % 17) * 7 + x, gy = (c / 17) * 10 + y, o = TIM_IMG + gy * 64 + gx / 2;
            out[y][x] = (uint8_t)(o < smallfont_size && ((smallfont[o] >> ((gx & 1) * 4)) & 15) == ink);
        }
    }
    if (c == 10 + 18 || c == 10 + 6) {
        /* S and G: the font's corner pixels join with the outline into 8 and 0 once enlarged */
        static const char *const gs[7] = { ".###.", "#....", "#....", ".###.", "....#", "....#", "####." };
        static const char *const gg[7] = { ".###.", "#....", "#....", "#.###", "#...#", "#...#", ".###." };
        const char *const *g2 = c == 10 + 18 ? gs : gg;
        memset(out, 0, 15 * 16);
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 5; x++) {
                out[y + 1][x] = (uint8_t)(g2[y][x] == '#');
            }
        }
    }
    return 1;
}

/* text mask into m (cw x ch): letters scaled so the font's 15-row cell is cell_h rows, starting at
   row `top`; squeezed to maxw; returns the width used or -1 */
static int set_text(const char *text, uint8_t *m, int cw, int ch, int top, int cell_h, int maxw, int italic, int gap_px, double min_sq, int narrow) {
    uint8_t g[48][15][16];
    int x0[48], x1[48], sp[48], n = 0, total = 0;
    double s, sx;

    for (const char *p = text; *p && n < 48;) {
        int len = ((unsigned char)*p & 0xE0) == 0xC0 ? 2 : 1;
        sp[n] = *p == ' ';
        if (narrow && !sp[n]) {
            if (letter_small(p, g[n], &len)) {
                x0[n] = 99;
                x1[n] = -1;
                for (int y = 0; y < 10; y++) for (int x = 0; x < 7; x++) if (g[n][y][x]) { x0[n] = x < x0[n] ? x : x0[n]; x1[n] = x > x1[n] ? x : x1[n]; }
                n++;
            }
            p += len;
            continue;
        }
        if (!sp[n] && !letter(p, g[n])) {
            if (*p == (char)0xC3 && (unsigned char)p[1] == 0x9F) { /* ß: ss */
                letter("s", g[n]);
                x0[n] = 99;
                x1[n] = -1;
                for (int y = 0; y < 15; y++) for (int x = 0; x < 16; x++) if (g[n][y][x]) { x0[n] = x < x0[n] ? x : x0[n]; x1[n] = x > x1[n] ? x : x1[n]; }
                memcpy(g[n + 1], g[n], sizeof(g[n]));
                x0[n + 1] = x0[n];
                x1[n + 1] = x1[n];
                sp[n + 1] = 0;
                n += 2;
                p += len;
                continue;
            }
            p += len;
            continue;
        }
        x0[n] = 99;
        x1[n] = -1;
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 16; x++) {
                if (g[n][y][x]) {
                    x0[n] = x < x0[n] ? x : x0[n];
                    x1[n] = x > x1[n] ? x : x1[n];
                }
            }
        }
        n++;
        p += len;
    }
    for (int i = 0; i < n; i++) {
        total += sp[i] ? 5 : (x1[i] >= 0 ? x1[i] - x0[i] + 2 : 0);
    }
    if (total == 0) {
        return -1;
    }
    s = (double)cell_h / (narrow ? 10.0 : 15.0);
    sx = narrow ? 1.0 : s; /* the small font: tall narrow letters, its own width */
    {
        int slant = italic ? cell_h / 4 : 0;
        if (total * sx + n * gap_px + slant > maxw) {
            sx = (double)(maxw - slant - n * gap_px) / total;
        }
    }
    if (sx < 0.3 || sx < s * min_sq) {
        return -1;
    }
    memset(m, 0, (size_t)cw * ch);
    {
        double x = 0;
        int right = 0;
        for (int i = 0; i < n; i++) {
            int gw;
            if (sp[i]) {
                x += 5 * sx;
                continue;
            }
            if (x1[i] < 0) {
                continue;
            }
            gw = x1[i] - x0[i] + 1;
            for (int py = 0; py < cell_h; py++) {
                int sy0 = (int)(py / s), sy1 = (int)((py + 1) / s - 1e-6);
                int slant = italic ? (cell_h - 1 - py) / 4 : 0;
                for (int px = 0; px < (int)(gw * sx + 0.999); px++) {
                    /* every source pixel the output pixel covers counts (thin strokes stay) */
                    int a0 = x0[i] + (int)(px / sx), a1 = x0[i] + (int)((px + 1) / sx - 1e-6), on = 0;
                    int X = (int)(x + 0.5) + px + slant, Y = top + py;
                    for (int yy = sy0; yy <= sy1 && yy < (narrow ? 10 : 15) && !on; yy++) {
                        for (int xx = a0; xx <= a1 && xx <= x1[i] && !on; xx++) {
                            on = g[i][yy][xx];
                        }
                    }
                    if (on && X >= 0 && X < cw && Y >= 0 && Y < ch) {
                        m[Y * cw + X] = 1;
                        right = X + 1 > right ? X + 1 : right;
                    }
                }
            }
            x += (gw + 1) * sx + gap_px;
        }
        return right;
    }
}

/* paint `w` over the rectangles `c[0..n)` of texture t */
static int paint1(Tex *t, const Cell *c, int n, const Words *w, double min_sq) {
    static uint8_t orig[CW * CH], mask[CW * CH], dist[CW * CH], text[CW * CH];
    static int hist[CH][MAXD + 1][256];
    int x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999, cw, ch, color[CH][MAXD + 1], all[MAXD + 1];
    int r = 1, maxd = 0, top = CH, bottom = -1, width, nmax = 0;

    if (n <= 0) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        x0 = c[i].x < x0 ? c[i].x : x0;
        x1 = c[i].x + c[i].w > x1 ? c[i].x + c[i].w : x1;
        y0 = c[i].y < y0 ? c[i].y : y0;
        y1 = c[i].y + c[i].h > y1 ? c[i].y + c[i].h : y1;
    }
    cw = x1 - x0;
    ch = y1 - y0;
    if (cw <= 8 || ch <= 6 || cw > CW || ch > CH) {
        return 0;
    }
    /* the original picture on the screen canvas */
    memset(orig, 0, sizeof(orig));
    for (int i = 0; i < n; i++) {
        for (int y = 0; y < c[i].h; y++) {
            for (int x = 0; x < c[i].w; x++) {
                orig[(c[i].y - y0 + y) * CW + c[i].x - x0 + x] = (uint8_t)tget(t, c[i].u + x, c[i].v + y);
            }
        }
    }
    /* distance from the outside (chebyshev, up to MAXD) of the original */
    for (int k = 0; k < CW * CH; k++) {
        dist[k] = orig[k] ? 1 : 0;
    }
    for (int d = 1; d <= MAXD; d++) {
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int k = y * CW + x, in = dist[k] == d;
                for (int dy = -1; dy <= 1 && in; dy++) {
                    for (int dx = -1; dx <= 1 && in; dx++) {
                        int xx = x + dx, yy = y + dy;
                        in = xx >= 0 && yy >= 0 && xx < cw && yy < ch && dist[yy * CW + xx] >= d;
                    }
                }
                if (in) {
                    dist[k] = (uint8_t)(d + 1);
                }
            }
        }
    }
    memset(hist, 0, sizeof(hist));
    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            int k = y * CW + x, d = dist[k];
            if (d) {
                d = d - 1 > MAXD ? MAXD : d - 1;
                hist[y][d][orig[k]]++;
                maxd = d > maxd ? d : maxd;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
        }
    }
    if (bottom < 0) {
        return 0;
    }
    for (int d = 0; d <= MAXD; d++) {
        int tot[256] = { 0 }, best = 0;
        for (int y = 0; y < ch; y++) {
            for (int v = 0; v < 256; v++) {
                tot[v] += hist[y][d][v];
            }
        }
        for (int v = 1; v < 256; v++) {
            best = tot[v] > tot[best] ? v : best;
        }
        all[d] = best ? best : (d ? all[d - 1] : 1);
        for (int y = 0; y < ch; y++) {
            int b = 0, s = 0;
            for (int v = 1; v < 256; v++) {
                b = hist[y][d][v] > hist[y][d][b] ? v : b;
                s += hist[y][d][v];
            }
            color[y][d] = s >= 2 && b ? b : -1;
        }
    }
    /* the new letters: as tall as the picture's content minus the outline, grown by r (the
       original strokes' thickness), with r free pixels on both sides of each letter; a long
       text gets a thinner outline first, then narrower letters */
    {
        double sc = (double)(bottom - top + 1) / 15.0;
        int r0 = 1; /* a thicker outline closes the letters' counters */
        (void)sc;
        width = -1;
        for (int pass = 0; pass < 2 && width <= 0; pass++) {
            for (r = r0; r >= 1 && width <= 0; r--) {
                int cell_h = bottom - top + 1 - 2 * r;
                if (cell_h < 7) {
                    continue;
                }
                width = set_text(w->text, text, CW, CH, top + r, cell_h, cw - 2 * r - 2, w->italic, pass ? r : 2 * r,
                                 pass ? (w->narrow ? 0.0 : min_sq) : (w->narrow ? 0.3 : 0.6), w->narrow);
                if (width > 0) {
                    break;
                }
            }
        }
        if (width <= 0) {
            return 0;
        }
        if (getenv("DW2_PICMASK") != NULL && strcmp(getenv("DW2_PICMASK"), w->text) == 0) {
            printf("[picmask] %s r %d maxd %d cw %d ch %d top %d bottom %d\n", w->text, r, maxd, cw, ch, top, bottom);
            for (int y = 0; y < ch; y++) {
                char line[CW + 1];
                for (int x = 0; x < cw; x++) line[x] = text[y * CW + x] ? '#' : '.';
                line[cw] = 0;
                printf("[picmask] %s\n", line);
            }
        }
    }
    {
        int shift = w->align < 0 ? r : w->align > 0 ? cw - r - width : (cw - width) / 2;
        memset(mask, 0, sizeof(mask));
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < CW; x++) {
                if (text[y * CW + x]) {
                    for (int dy = -r; dy <= r; dy++) {
                        for (int dx = -r; dx <= r; dx++) {
                            int xx = x + shift + dx, yy = y + dy;
                            if (xx >= 0 && yy >= 0 && xx < cw && yy < ch) {
                                mask[yy * CW + xx] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    for (int k = 0; k < CW * CH; k++) {
        dist[k] = mask[k] ? 1 : 0;
    }
    for (int d = 1; d <= MAXD; d++) {
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int k = y * CW + x, in = dist[k] == d;
                for (int dy = -1; dy <= 1 && in; dy++) {
                    for (int dx = -1; dx <= 1 && in; dx++) {
                        int xx = x + dx, yy = y + dy;
                        in = xx >= 0 && yy >= 0 && xx < cw && yy < ch && dist[yy * CW + xx] >= d;
                    }
                }
                if (in) {
                    dist[k] = (uint8_t)(d + 1);
                }
            }
        }
    }
    {
        nmax = 0;
        for (int k = 0; k < CW * CH; k++) {
            nmax = dist[k] - 1 > nmax ? dist[k] - 1 : nmax;
        }
    }
    if (w->narrow) {
        /* thin letters: bold by one column, the letter in the picture's body colour of its row,
           a one-pixel ring in its outline colour */
        static uint8_t bold[CW * CH];
        memset(bold, 0, sizeof(bold));
        for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
            int sx0 = x - (w->align < 0 ? r : w->align > 0 ? cw - r - width : (cw - width) / 2);
            /* not into a one-column gap (N, M, W keep their inner strokes apart) */
            int on = sx0 >= 0 && sx0 < CW && text[y * CW + sx0];
            int left = sx0 >= 1 && sx0 - 1 < CW && text[y * CW + sx0 - 1];
            int right = sx0 + 1 >= 0 && sx0 + 1 < CW && text[y * CW + sx0 + 1];
            bold[y * CW + x] = (uint8_t)(on || (left && !right));
        }
        for (int i = 0; i < n; i++) {
            for (int y = 0; y < c[i].h; y++) {
                for (int x = 0; x < c[i].w; x++) {
                    int Y = c[i].y - y0 + y, X = c[i].x - x0 + x, v = 0;
                    if (bold[Y * CW + X]) {
                        v = color[Y][maxd] >= 0 ? color[Y][maxd] : all[maxd];
                    } else {
                        for (int dy = -1; dy <= 1 && !v; dy++) for (int dx = -1; dx <= 1 && !v; dx++) {
                            int xx = X + dx, yy = Y + dy;
                            if (xx >= 0 && yy >= 0 && xx < cw && yy < ch && bold[yy * CW + xx]) v = all[0];
                        }
                    }
                    tset(t, c[i].u + x, c[i].v + y, v);
                }
            }
        }
        return 1;
    }
    /* write back into the rectangles */
    for (int i = 0; i < n; i++) {
        for (int y = 0; y < c[i].h; y++) {
            for (int x = 0; x < c[i].w; x++) {
                int Y = c[i].y - y0 + y, X = c[i].x - x0 + x, k = Y * CW + X, v = 0;
                if (dist[k]) {
                    int d = (dist[k] - 1) * maxd / (nmax > 0 ? nmax : 1);
                    d = d > maxd ? maxd : d;
                    v = color[Y][d] >= 0 ? color[Y][d] : all[d];
                }
                tset(t, c[i].u + x, c[i].v + y, v);
            }
        }
    }
    return 1;
}

/* the wide letters when they fit without much squeezing, else the narrow ones */
static int paint(Tex *t, const Cell *c, int n, const Words *w) {
    if (!w->narrow) {
        Words nw = *w;
        if (paint1(t, c, n, w, 0.7)) {
            return 1;
        }
        nw.narrow = 1;
        return paint1(t, c, n, &nw, 0.0);
    }
    return paint1(t, c, n, w, 0.0);
}

/* ---- the pictures ---- */

typedef struct {
    int file, entry;   /* part layout whose cells are the picture */
    int first, count;  /* which of its cells (screen order); count 0 = all */
    Words w;
} Pic;

/* English -> German as the player sees them; the user's terms: Target Ziel, Win Sieg, Battle Kampf,
 * Attack Attacke, Damage Schaden, Level Up Levelaufstieg, Press Start Button Starttaste drücken */
static const Pic pics[] = {
    /* battle words (file 0x1A0, italic) */
    { 0x1A0, 3, 0, 1, { "Verfehlt", 1, 0 } },           /* Missed */
    { 0x1A0, 4, 0, 1, { "Attacke", 1, 0 } },            /* Attack (also the right half of Counter Attack) */
    { 0x1A0, 5, 0, 1, { "Konter", 1, 1 } },             /* Counter (Counter Attack: "Konter Attacke") */
    { 0x1A0, 6, 0, 1, { "Unterbrechen", 1, 0 } },       /* Interrupt */
    { 0x1A0, 7, 0, 1, { "Hilfe", 1, 0 } },              /* Assist */
    { 0x1A0, 8, 0, 1, { "Abwehr", 1, 0 } },             /* Guard */
    { 0x1A0, 10, 0, 1, { "Willst du", 1, 0, 1 } },         /* Do you want to */
    { 0x1A0, 10, 1, 1, { "unterbrechen?", 1, 0 } },     /* interrupt? */
    { 0x1A0, 1, 0, 1, { "Ja", 1, 0 } },                 /* Yes */
    { 0x1A0, 2, 0, 1, { "Nein", 1, 0 } },               /* No */
    /* battle words (file 0x13B) */
    { 0x13B, 2, 0, 1, { "Schaden", 1, 0, 1 } },            /* Damage */
    { 0x13B, 3, 0, 1, { "ZIEL", 0, 0 } },               /* TARGET */
    { 0x13B, 22, 0, 1, { "WÄHLE", 0, 0 } },             /* SELECT (of SELECT TARGET) */
    /* battle start / end (file 0x313) */
    { 0x313, 1, 0, 1, { "KAMPF", 0, 0 } },              /* BATTLE */
    { 0x313, 2, 0, 1, { "Flucht", 0, 0 } },             /* Run Away (alone: fled) */
    { 0x313, 3, 0, 1, { "Keine", 0, 0 } },              /* Unable to (+ Run Away: "Keine Flucht") */
    { 0x313, 4, 0, 1, { "LOS!", 0, 0 } },               /* FIGHT */
    { 0x313, 5, 0, 0, { "SPIEL VORBEI", 0, 0 } },       /* GAME OVER (two pictures) */
    { 0x313, 6, 0, 1, { "NIEDERLAGE", 0, 0 } },         /* LOSE */
    /* battle result (file 0x25C) */
    { 0x25C, 10, 0, 1, { "SIEG", 0, 0 } },              /* WIN */
    { 0x25C, 7, 0, 1, { "LEVELAUFSTIEG", 0, 0, 1, 124, 1 } },      /* LEVEL UP */
    /* title (file 0xB64) */
    { 0xB64, 1, 0, 0, { "STARTTASTE DRÜCKEN", 0, 0, 1 } }, /* PRESS START BUTTON (three pictures) */
    { 0xB64, 2, 0, 1, { "NEUES SPIEL", 0, 0, 1, 88 } },        /* NEW GAME */
    { 0xB64, 3, 0, 1, { "FORTSETZEN", 0, 0, 1, 100 } },         /* CONTINUE GAME */
    { 0xB64, 4, 0, 1, { "KAMPFMODUS", 0, 0, 1, 100 } },         /* BATTLE MODE */
};

/* the attack names of the battle effects, set by host/lang.c (skill table) */
#define MAX_SKILLPICS 256
static struct {
    int file, entry;
    char de[64];
} skillpics[MAX_SKILLPICS];
static int nskillpics;

void PicWords_AddSkill(int32_t parts_entry, const char *de) {
    if (nskillpics < MAX_SKILLPICS && parts_entry != 0) {
        skillpics[nskillpics].file = (parts_entry >> 16) & 0xFFFF;
        skillpics[nskillpics].entry = parts_entry & 0xFFFF;
        snprintf(skillpics[nskillpics].de, sizeof(skillpics[nskillpics].de), "%s", de);
        nskillpics++;
    }
}

typedef struct {
    int id, size, changed;
    uint8_t *d;
} Open;

#define MAX_OPEN 192
static Open open_files[MAX_OPEN];
static int nopen;

static Open *get_file(int id) {
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].id == id) {
            return &open_files[i];
        }
    }
    if (nopen == MAX_OPEN) {
        return NULL;
    }
    open_files[nopen].id = id;
    open_files[nopen].d = Host_PakReadFileLayered(id, &open_files[nopen].size);
    open_files[nopen].changed = 0;
    return &open_files[nopen++];
}

static void flush_files(void) {
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].d != NULL && open_files[i].changed) {
            Host_PakSetLayer(open_files[i].id, open_files[i].d, open_files[i].size);
        }
        free(open_files[i].d);
    }
    nopen = 0;
}

/* A picture that is too narrow for its German text gets wider in its texture when the columns
   beside it are used by no other cell of the file: every cell of the file showing exactly this
   rectangle then takes the wider one, moved left by half the growth (the text stays centred). */
static void grow_rect(Open *f, const Tex *t, Cell *c, int need) {
    int el = 0, er = 0, want = need - c->w;
    uint32_t first;
    int n;

    if (want <= 0) {
        return;
    }
    n = entries(f->d, f->size);
    first = entry(f->d, f->size, 0);
    (void)first;
    for (int side = 0; side < 2; side++) {
        for (int k = 1; k <= want && el + er < want; k++) {
            int col = side == 0 ? c->u + c->w - 1 + er + 1 : c->u - el - 1, ok = col >= 0 && col < t->w;
            for (int e = 1; e < 4096 && ok; e++) {
                uint32_t o = entry(f->d, f->size, e);
                if (o == 0) {
                    if (e >= n) break;
                    continue;
                }
                if (rd32(f->d + o) == 0x10) continue;
                for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64 && ok; q++, o += 16) {
                    int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
                    if (u == c->u && v == c->v && w == c->w && h == c->h) continue;
                    ok = !(col >= u && col < u + w && c->v < v + h && v < c->v + c->h);
                }
            }
            if (!ok) break;
            if (side == 0) er++; else el++;
        }
    }
    if (el + er == 0) {
        return;
    }
    {
        int du = -el, dw = el + er, dx = -(el + er) / 2;
        for (int e = 1; e < 4096; e++) {
            uint32_t o = entry(f->d, f->size, e);
            if (o == 0) {
                if (e >= n) break;
                continue;
            }
            if (rd32(f->d + o) == 0x10) continue;
            for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
                int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
                if (u == c->u && v == c->v && w == c->w && h == c->h) {
                    int16_t x = (int16_t)(rd16(f->d + o + 2) + dx);
                    f->d[o] = (uint8_t)(u + du);
                    f->d[o + 8] = (uint8_t)(w + dw);
                    memcpy(f->d + o + 2, &x, 2);
                }
            }
        }
        c->u += du;
        c->w += dw;
        c->x += dx;
    }
}

/* any cell of the file (other than c itself) over texture pixel (x, y)? */
static int cell_covers(Open *f, const Cell *c, int x, int y) {
    int n = entries(f->d, f->size);
    for (int e = 1; e < 4096; e++) {
        uint32_t o = entry(f->d, f->size, e);
        if (o == 0) {
            if (e >= n) break;
            continue;
        }
        if (rd32(f->d + o) == 0x10) continue;
        for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
            int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
            if (u == c->u && v == c->v && w == c->w && h == c->h) continue;
            if (x >= u && x < u + w && y >= v && y < v + h) return 1;
        }
    }
    return 0;
}

/* when widening in place was not enough: move the picture to an empty part of the same texture
   (all pixels transparent and under no other cell, one pixel of room around it), as wide as
   `need`, centred where it was on screen */
static void move_rect(Open *f, const Tex *t, Cell *c, int need) {
    int n = entries(f->d, f->size), W = need, H = c->h;

    if (getenv("DW2_PICDEBUG")) printf("[picwords] move %d,%d %dx%d -> %d wide, texture %dx%d\n", c->u, c->v, c->w, c->h, need, t->w, t->h);
    if (c->w >= need || W + 2 > t->w || W > 255) {
        return;
    }
    for (int v = t->h - H - 1; v >= 1; v--) {
        for (int u = 1; u + W + 1 <= t->w && u + W <= 255; u++) {
            int ok = 1;
            for (int y = v - 1; y <= v + H && ok; y++) {
                for (int x = u - 1; x <= u + W && ok; x++) {
                    ok = tget(t, x, y) == 0 && !cell_covers(f, c, x, y);
                }
            }
            if (!ok) continue;
            for (int e = 1; e < 4096; e++) {
                uint32_t o = entry(f->d, f->size, e);
                if (o == 0) {
                    if (e >= n) break;
                    continue;
                }
                if (rd32(f->d + o) == 0x10) continue;
                for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
                    int cu = f->d[o], cv = f->d[o + 1], cw = f->d[o + 8], chh = f->d[o + 9] ? f->d[o + 9] : 256;
                    if (cu == c->u && cv == c->v && cw == c->w && chh == c->h) {
                        int16_t x = (int16_t)(rd16(f->d + o + 2) - (W - c->w) / 2);
                        f->d[o] = (uint8_t)u;
                        f->d[o + 1] = (uint8_t)v;
                        f->d[o + 8] = (uint8_t)W;
                        memcpy(f->d + o + 2, &x, 2);
                    }
                }
            }
            if (getenv("DW2_PICDEBUG")) printf("[picwords] moved to %d,%d\n", u, v);
            /* the original pixels come along (centred): the painter learns its colours from them */
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < c->w; x++) {
                    tset((Tex *)t, u + (W - c->w) / 2 + x, v + y, tget(t, c->u + x, c->v + y));
                }
            }
            c->x -= (W - c->w) / 2;
            c->u = u;
            c->v = v;
            c->w = W;
            return;
        }
    }
}

static int paint_pic(int file, int e, int first, int count, const Words *w) {
    Open *f = get_file(file);
    Cell c[MAX_CELLS];
    Tex t;
    int n;

    if (f == NULL || f->d == NULL || !tex_open(&t, f->d, f->size)) {
        return 0;
    }
    n = cells_of(f->d, f->size, e, c);
    if (count == 0) {
        count = n - first;
    }
    if (first < 0 || first + count > n || count <= 0) {
        return 0;
    }
    if (count == 1 && w->grow > 0) {
        grow_rect(f, &t, &c[first], w->grow);
        if (w->move) {
            move_rect(f, &t, &c[first], w->grow);
        }
    }
    {
        /* several separate pictures (with gaps between them on screen) and as many words: one word
           each; otherwise the text runs over all of them */
        char words[8][64];
        int nw = 0, gaps = 0;
        for (const char *p = w->text; *p && nw < 8;) {
            int k = 0;
            while (*p == ' ') p++;
            while (*p && *p != ' ' && k < 63) words[nw][k++] = *p++;
            words[nw][k] = 0;
            if (k) nw++;
        }
        for (int i = first + 1; i < first + count; i++) {
            gaps += c[i].x > c[i - 1].x + c[i - 1].w + 2;
        }
        if (count > 1 && nw == count && gaps) {
            for (int i = 0; i < count; i++) {
                Words one = { words[i], w->italic, i == 0 ? 1 : -1, w->narrow, 0 };
                if (!paint(&t, c + first + i, 1, &one)) {
                    return 0;
                }
            }
        } else if (!paint(&t, c + first, count, w)) {
            return 0;
        }
    }
    f->changed = 1;
    return 1;
}

/* the battle effect name: a part list (file, entry) whose parts each draw a picture */
static int paint_skill(int file, int e, const char *de) {
    Open *f = get_file(file);
    uint32_t o;
    int done = 0;
    Words w = { de, 1, 0, 0, 0 };

    if (f == NULL || f->d == NULL || (o = entry(f->d, f->size, e)) == 0) {
        return 0;
    }
    /* GfxPart records 0x28 each, file id first, list end file id 0; the name is the first part */
    for (int k = 0; o + 0x28 <= (uint32_t)f->size && k < 8; k++, o += 0x28) {
        uint32_t id = rd32(f->d + o);
        if (id == 0) {
            break;
        }
        if (k == 0) {
            done = paint_pic((int)(id >> 16), (int)(id & 0xFFFF), 0, 0, &w);
        }
    }
    return done;
}

/* DW2_PICDUMP=DIR: the changed files as they are used (tests) */
static void dump_files(void) {
    const char *dir = getenv("DW2_PICDUMP");
    char path[1024];
    if (dir == NULL) {
        return;
    }
    if (font22 != NULL) { /* the letter font as read (to check the glyph positions) */
        FILE *f;
        snprintf(path, sizeof(path), "%s/022B_font.bin", dir);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(font22, 1, (size_t)font22_size, f);
            fclose(f);
        }
    }
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].d != NULL && open_files[i].changed) {
            FILE *f;
            snprintf(path, sizeof(path), "%s/%04X.bin", dir, open_files[i].id);
            f = fopen(path, "wb");
            if (f != NULL) {
                fwrite(open_files[i].d, 1, (size_t)open_files[i].size, f);
                fclose(f);
            }
        }
    }
}

void PicWords_Apply(void) {
    int ok = 0, bad = 0, sk_ok = 0, sk_bad = 0;
    char a[64], b[64];

    smallfont = Host_PakReadFileLayered(0x13A, &smallfont_size);
    font22 = Host_PakReadFileLayered(0x22B, &font22_size);
    if (font22 == NULL || !tex_open(&font22_tex, font22, font22_size)) {
        free(font22);
        font22 = NULL;
        Lang_Note("word pictures: no letter font (file 0x22B), they stay English%s", "", "");
        return;
    }
    for (size_t i = 0; i < sizeof(pics) / sizeof(pics[0]); i++) {
        if (paint_pic(pics[i].file, pics[i].entry, pics[i].first, pics[i].count, &pics[i].w)) {
            ok++;
        } else {
            bad++;
            printf("[lang] word picture %X:%d (%s) not changed\n", pics[i].file, pics[i].entry, pics[i].w.text);
        }
    }
    for (int i = 0; i < nskillpics; i++) {
        if (paint_skill(skillpics[i].file, skillpics[i].entry, skillpics[i].de)) {
            sk_ok++;
        } else {
            sk_bad++;
        }
    }
    dump_files();
    flush_files();
    free(font22);
    font22 = NULL;
    free(smallfont);
    smallfont = NULL;
    memset(&font22_tex, 0, sizeof(font22_tex));
    snprintf(a, sizeof(a), "%d of %d", ok, ok + bad);
    snprintf(b, sizeof(b), "%d of %d attack names", sk_ok, sk_ok + sk_bad);
    Lang_Note("word pictures German: %s, %s", a, b);
}

/* ---- status messages of the battle: German letter-cell lists (file 0x22B) ---- */

static const char *const status_de[] = {
    /* 1.. in file order (English in the comments) */
    "Vergiftet",                       /* Poisoned */
    "Gift geheilt",                    /* Poison cured */
    "Gelaehmt",                        /* Paralyzed */
    "Laehmung geheilt",                /* Paralysis cured */
    "Verwirrt",                        /* Confused */
    "Verwirrung geheilt",              /* Confusion cured */
    "Starke Attacke",                  /* Powerful attack */
    "Kein Konter moeglich",            /* Unable to counter */
    "Angriff gesenkt",                 /* Attack power down */
    "Abwehr gesenkt",                  /* Defense down */
    "Tempo gesenkt",                   /* Quickness down */
    "Motivation gesenkt",              /* Motivation down */
    "Motivation zurueck",              /* Motivation back */
    "Benommen",                        /* Dizzy */
    "Keine HP-Heilung",                /* Can't recover HP */
    "HP-Heilung wieder moeglich",      /* Can recover HP */
    "Keine Zustandsheilung",           /* Can't recover status */
    "Zustandsheilung moeglich",        /* Can recover status */
    "HP gesaugt",                      /* Suck HP */
    "Angriff & Abwehr gesenkt",        /* Attack&Defense down */
    "Angriff erhoeht",                 /* Attack power up */
    "Abwehr erhoeht",                  /* Defense Power up */
    "Tempo erhoeht",                   /* Quickness is up */
    "Wiederbelebt",                    /* Revived */
    "Attacke wirkungslos",             /* Attack ineffective */
    "Attacke wirksam",                 /* Attack effective */
    "Giftkraft erhalten",              /* Gained poisoning power */
    "Giftkraft verloren",              /* Lost poisoning power */
    "Laehmkraft erhalten",             /* Gained paralysis power */
    "Laehmkraft verloren",             /* Lost paralysis power */
    "Verwirrkraft erhalten",           /* Gained confusion power */
    "Verwirrkraft verloren",           /* Lost confusion power */
    "Wasserkraft erhalten",            /* Gained water power */
    "Wasserkraft verloren",            /* Lost water power */
    "Feuerkraft erhalten",             /* Gained fire power */
    "Feuerkraft verloren",             /* Lost fier power */
    "Naturkraft erhalten",             /* Gained nature power */
    "Naturkraft verloren",             /* Lost nature power */
    "Maschinenkraft erhalten",         /* Gained machine power */
    "Maschinenkraft verloren",         /* Lost machine power */
    "Dunkelkraft erhalten",            /* Gained darkness power */
    "Dunkelkraft verloren",            /* Lost darkness power */
    "Wurde giftig",                    /* Became poisonous */
    "Kann nicht zielen",               /* Unable to aim */
    "Kann wieder zielen",              /* Able to aim */
    NULL,                              /* A (placeholder of the game) */
    "Zustand geheilt",                 /* Recover status */
    "Werte geheilt",                   /* Recover parameter */
    "Technikkraft gesenkt",            /* Lower tech power */
    "Technik wird normaler Angriff",   /* Turn a tech to normal attack */
    "Technik weggefegt",               /* Blow away a tech */
    "Technik verzoegert",              /* Delay a tech */
    "Technik uebernommen",             /* Take over a tect */
    "MP wiederverwendet",              /* Reuse MP */
    "Technik gestohlen",               /* Steal a tech */
    "Attacke abgeprallt",              /* Bounced off an attack */
    "Wurde zum Zombie",                /* Became a zombi */
    "Werte zurueckgesetzt",            /* Initialize parameters */
    "Werte zurueckgesetzt",            /* (the same, misspelt in the game) */
    "Heilung & Abwehr erhoeht",        /* Increased recovery&defense */
    "Panzer verstaerkt",               /* Strengthen armor */
    "Unbekannte Attacke",              /* Unknown attack */
    "Wurde zum Schild",                /* Be a shield */
    "MP gesenkt",                      /* Lower MP */
    "MP gesaugt",                      /* Suck MP */
    "Zufallsattacke",                  /* Random attack */
    "Giftkoerper verloren",            /* Lost poisonous body */
    "Voll geheilt",                    /* fully recovered */
};
#define NSTATUS ((int)(sizeof(status_de) / sizeof(status_de[0])))

/* glyph positions of the font of file 0x22B (12 x 15 cells) */
static int status_glyph(int ch, int *u, int *v) {
    static const struct { const char *s; int x, y; } rows[] = {
        { "ABCD", 68, 64 }, { "EFGH", 68, 80 }, { "IJKLM", 64, 96 }, { "NOPQR", 0, 104 }, { "zyxwv", 60, 112 },
        { "STTUu", 0, 120 }, { "tsrqp", 60, 128 }, { "onmlk", 0, 136 }, { "jihgf", 60, 144 }, { "edcba", 0, 152 },
        { "ZYXWV", 60, 160 },
    };
    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        const char *p = strchr(rows[r].s, ch);
        if (p != NULL && ch) {
            *u = rows[r].x + 12 * (int)(p - rows[r].s);
            *v = rows[r].y;
            return 1;
        }
    }
    if (ch == '&') {
        *u = 8;
        *v = 168;
        return 1;
    }
    return 0;
}

static uint8_t status_cells[NSTATUS + 1][64 * 16 + 16];
static uint8_t status_built[NSTATUS + 1];
static int status_ready = -1;

static void build_status(void) {
    int size = 0;
    uint8_t *d = Host_PakReadFileLayered(0x22B, &size);

    status_ready = 0;
    if (d == NULL) {
        return;
    }
    for (int e = 1; e <= NSTATUS; e++) {
        uint32_t o = entry(d, size, e);
        const char *s = status_de[e - 1];
        uint8_t proto[16];
        int len, n = 0, x;

        status_built[e] = 0;
        if (s == NULL || o == 0 || o + 16 > (uint32_t)size || d[o] == 0xFF) {
            continue;
        }
        memcpy(proto, d + o, 16);
        len = (int)strlen(s);
        /* right-aligned at x = 0 like the game's lists (last letter at -12), a space is 12 more */
        x = 0;
        for (int i = len - 1; i >= 0 && n < 63; i--) {
            int u, v;
            x -= 12;
            if (s[i] == ' ') {
                continue;
            }
            if (!status_glyph(s[i], &u, &v)) {
                continue;
            }
            uint8_t *c = status_cells[e] + n * 16;
            int16_t xx = (int16_t)x, yy = (int16_t)(strchr("gjpqy", s[i]) ? -4 : -8);
            memcpy(c, proto, 16);
            c[0] = (uint8_t)u;
            c[1] = (uint8_t)v;
            memcpy(c + 2, &xx, 2);
            memcpy(c + 4, &yy, 2);
            c[8] = 12;
            c[9] = 15;
            n++;
        }
        status_cells[e][n * 16] = 0xFF;
        status_built[e] = n > 0;
    }
    free(d);
    status_ready = 1;
}

static int german = -1;
void PicWords_SetGerman(int on) {
    german = on;
}

const void *Lang_PartCells(int32_t file_id, const void *cells) {
    int f = (file_id >> 16) & 0xFFFF, e = file_id & 0xFFFF;

    if (german != 1 || f != 0x22B || e < 1 || e > NSTATUS) {
        return cells;
    }
    if (status_ready < 0) {
        build_status();
    }
    if (status_ready != 1 || !status_built[e]) {
        return cells;
    }
    return status_cells[e];
}
