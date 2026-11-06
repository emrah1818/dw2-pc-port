/* Modern typefaces for ready-made word pictures. Current labels are rasterized
 * from embedded fonts into the player's in-memory texture layer. Cell layouts,
 * palette locations, animation and current DE/EN spelling are retained.
 * Four-bit palettes live in the texture itself; eight-bit palettes come from
 * the TIM CLUT. No original letter shapes are sampled for the replacements.
 * Battle status messages retain their existing German letter-cell lists. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"
#include "host/fonts.h"
#include "host/devdata.h"
#include "host/picturetable.h"

void* Host_PakReadFileLayered(int id, int* size);
void  Host_PakSetLayer(int id, const uint8_t* data, int size);
void  Lang_Note(const char* fmt, const char* a, const char* b);
int   Lang_Encode(const char* s, uint8_t* out, int max); /* host/lang.c: UTF-8 -> game glyph codes */

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int16_t rd16(const uint8_t* p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

/* ---- the texture of a file (entry 0: a TIM, 4 or 8 bit) ---- */

typedef struct {
    uint8_t* d;
    int      size;
    uint32_t palette, palette_bytes;
    uint32_t img;    /* first pixel byte */
    int      stride; /* bytes per row */
    int      w, h;   /* in pixels */
    int      bits;   /* 4 or 8 */
} Tex;

static int entries(const uint8_t* d, int size)
{
    return PictureTable_Count(d, size);
}
static uint32_t entry(const uint8_t* d, int size, int i)
{
    return PictureTable_Entry(d, size, i);
}

static int tex_open(Tex* t, uint8_t* d, int size)
{
    uint32_t o = entry(d, size, 0), p, flags, len;
    int      iw;

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
        t->palette       = p + 12;
        t->palette_bytes = len - 12;
        p               += len;
    }
    if (p + 12 > (uint32_t)size) {
        return 0;
    }
    iw        = (uint16_t)rd16(d + p + 8);
    t->h      = (uint16_t)rd16(d + p + 10);
    t->stride = iw * 2;
    t->bits   = (flags & 7) ? 8 : 4;
    t->w      = (flags & 7) ? iw * 2 : iw * 4;
    t->img    = p + 12;
    t->d      = d;
    t->size   = size;
    if (t->img + (uint32_t)(t->stride * t->h) > (uint32_t)size || t->w <= 0 || t->h <= 0) {
        return 0;
    }
    return 1;
}

static int tget(const Tex* t, int x, int y)
{
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) {
        return 0;
    }
    if (t->bits == 8) {
        return t->d[t->img + y * t->stride + x];
    }
    return (t->d[t->img + y * t->stride + x / 2] >> ((x & 1) * 4)) & 15;
}

static void tset(Tex* t, int x, int y, int v)
{
    uint8_t* b;
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) {
        return;
    }
    if (t->bits == 8) {
        t->d[t->img + y * t->stride + x] = (uint8_t)v;
        return;
    }
    b  = &t->d[t->img + y * t->stride + x / 2];
    *b = (uint8_t)((*b & ~(15 << ((x & 1) * 4))) | (v & 15) << ((x & 1) * 4));
}

/* ---- the rectangles of a part layout (cells 0x10: u v x y clutX clutY w h row blend frame) ---- */

typedef struct {
    int u, v, w, h; /* in the texture */
    int x, y;       /* on screen, relative to the part */
    int clut_x, clut_y, clut_row;
} Cell;

#define MAX_CELLS 24

static int cells_of(const uint8_t* d, int size, int e, Cell* out)
{
    uint32_t o = entry(d, size, e);
    int      n = 0;

    if (o == 0) {
        return 0;
    }
    for (int k = 0; o + 16 <= (uint32_t)size && d[o] != 0xFF && k < 64; k++, o += 16) {
        Cell c   = { d[o], d[o + 1], d[o + 8], d[o + 9] ? d[o + 9] : 256, rd16(d + o + 2), rd16(d + o + 4), d[o + 6], d[o + 7], d[o + 10] };
        int  dup = 0;
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
            Cell t     = out[j];
            out[j]     = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}

/* ---- painting ---- */

#define CW 512
#define CH 64

typedef struct {
    const char* text;
    int         italic; /* slant to the right */
    int         align;  /* 0 centre, -1 left, 1 right */
    int         narrow; /* letters of the small game font (narrow, like the title's pictures) */
    int         grow;   /* width (texture pixels) the picture may grow to, 0 = keep */
    int         move;   /* 1: when it cannot grow in place, move it to an empty part of the texture */
    int         role;   /* 0 display, 1 interface, 2 telemetry */
    int         border; /* preserve the enclosing one-pixel frame */
} Words;

/* ---- battle status letter cells (file 0x22B, 12 x 15) ---- */

static uint8_t* font22;
static int      font22_size;
static Tex      font22_tex;
static int      status_glyph(int ch, int* u, int* v);

/* Four-bit palettes live inside the image, at the cell's CLUT coordinates.
 * Their nominal TIM palette is not the one the game uses. */
static int picture_ink(const Tex* t, const Cell* c)
{
    int used[256] = { 0 }, best = 1, brightness = -1, max = t->bits == 8 ? 255 : 15;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            used[tget(t, c->u + x, c->v + y)]++;
    uint32_t p     = t->bits == 4 ? t->img + (c->clut_y + c->clut_row) * t->stride + c->clut_x * 2 : t->palette;
    uint32_t limit = t->bits == 4 ? (uint32_t)t->size : t->palette + t->palette_bytes;
    for (int k = 1; k <= max; k++)
        if (used[k] && p + 2 * k + 2 <= limit) {
            unsigned color = (uint16_t)rd16(t->d + p + 2 * k);
            int      b     = 3 * (color & 31) + 6 * ((color >> 5) & 31) + ((color >> 10) & 31);
            if (b > brightness) {
                brightness = b;
                best       = k;
            }
        }
    return best;
}

/* Rasterize each label without the legacy outline dilation. At small sizes
 * that dilation fills the counters of Orbitron. The current cell palette
 * supplies a visible ink colour, including palettes stored inside the TIM. */
static int paint1(Tex* t, const Cell* c, int n, const Words* w, double min_sq)
{
    (void)min_sq;
    static uint8_t mask[CW * CH];
    int            x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999;
    if (n <= 0)
        return 0;
    for (int i = 0; i < n; i++) {
        x0 = c[i].x < x0 ? c[i].x : x0;
        x1 = c[i].x + c[i].w > x1 ? c[i].x + c[i].w : x1;
        y0 = c[i].y < y0 ? c[i].y : y0;
        y1 = c[i].y + c[i].h > y1 ? c[i].y + c[i].h : y1;
    }
    int cw = x1 - x0, ch = y1 - y0;
    if (cw <= 8 || ch <= 6 || cw > CW || ch > CH)
        return 0;
    memset(mask, 0, sizeof(mask));
    int role  = w->role == 1 ? FONT_INTERFACE : w->role == 2 ? FONT_TELEMETRY
                                                             : FONT_DISPLAY;
    int lines = 1;
    for (const char* p = w->text; *p; p++)
        if (*p == '\n')
            lines++;
    int line_h = (ch - 2) / lines;
    if (line_h < 1)
        return 0;
    const char* p = w->text;
    for (int line = 0; line < lines; line++) {
        const char* end = strchr(p, '\n');
        size_t      len = end ? (size_t)(end - p) : strlen(p);
        char        label[512];
        uint8_t     row[CW * CH];
        if (len >= sizeof(label))
            return 0;
        memcpy(label, p, len);
        label[len] = 0;
        /* The second copyright line has only one cell, while the first spans
         * three. Fit each line to its actual mapped texture coverage. */
        int ymid = y0 + 1 + line * line_h + line_h / 2, lo = cw, hi = 0;
        for (int i = 0; i < n; i++)
            if (ymid >= c[i].y && ymid < c[i].y + c[i].h) {
                int a = c[i].x - x0, b = a + c[i].w;
                if (a < lo)
                    lo = a;
                if (b > hi)
                    hi = b;
            }
        int available = hi - lo - 2;
        if (available < 1)
            return 0;
        int width = Fonts_Mask(role, label, row, CW, available, line_h);
        if (width < 0)
            return 0;
        int left = lo + (w->align < 0 ? 0 : w->align > 0 ? available - width
                                                         : (available - width) / 2);
        for (int y = 0; y < line_h; y++)
            memcpy(mask + (line * line_h + y) * CW + left, row + y * CW, (size_t)width);
        if (end)
            p = end + 1;
    }
    int shift = 1;
    /* dw2-pc-port: the colours of the original picture. The original is a gradient with an outline
     * (bright letter body, darker rim): learn its colour per row and per depth from the original
     * pixels (a flat "brightest palette entry" came out nearly black on the dark battle floor),
     * and give tall pictures a one-pixel outline like the original's. Small pictures get the body
     * colour only: an outline fills the counters of the small letters. */
    {
        enum { MAXD = 10 };
        static uint8_t orig[CW * CH], dist[CW * CH], tot[CW * CH];
        static int     hist[CH][MAXD + 1][256];
        int            color[CH][MAXD + 1], all[MAXD + 1], maxd = 0, bottom = -1, nmax = 0;
        int            tall = 0; /* an outline ring closes the counters of the new letters (e, a): body colour only */

        memset(orig, 0, sizeof(orig));
        for (int i = 0; i < n; i++)
            for (int y = 0; y < c[i].h; y++)
                for (int x = 0; x < c[i].w; x++)
                    orig[(c[i].y - y0 + y) * CW + c[i].x - x0 + x] = (uint8_t)tget(t, c[i].u + x, c[i].v + y);
        for (int k = 0; k < CW * CH; k++)
            dist[k] = orig[k] ? 1 : 0;
        for (int d = 1; d <= MAXD; d++)
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    int k = y * CW + x, in = dist[k] == d;
                    for (int dy = -1; dy <= 1 && in; dy++)
                        for (int dx = -1; dx <= 1 && in; dx++) {
                            int xx = x + dx, yy = y + dy;
                            in = xx >= 0 && yy >= 0 && xx < cw && yy < ch && dist[yy * CW + xx] >= d;
                        }
                    if (in)
                        dist[k] = (uint8_t)(d + 1);
                }
        memset(hist, 0, sizeof(hist));
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int k = y * CW + x, d = dist[k];
                if (d) {
                    d = d - 1 > MAXD ? MAXD : d - 1;
                    hist[y][d][orig[k]]++;
                    maxd   = d > maxd ? d : maxd;
                    bottom = y > bottom ? y : bottom;
                }
            }
        if (bottom < 0) { /* nothing to learn from: the brightest used palette entry */
            int inks[MAX_CELLS];
            for (int i = 0; i < n; i++)
                inks[i] = picture_ink(t, &c[i]);
            for (int i = 0; i < n; i++)
                for (int y = 0; y < c[i].h; y++)
                    for (int x = 0; x < c[i].w; x++) {
                        int Y = c[i].y - y0 + y, X = c[i].x - x0 + x, sx = X - shift, sy = Y - 1, v = 0;
                        if (!(w->border && (X == 0 || X == cw - 1 || Y == 0 || Y == ch - 1)) && sx >= 0 && sx < cw - 2 &&
                            sy >= 0 && sy < ch - 2 && mask[sy * CW + sx] >= 80)
                            v = inks[i];
                        tset(t, c[i].u + x, c[i].v + y, v);
                    }
            return 1;
        }
        for (int d = 0; d <= MAXD; d++) {
            int sum[256] = { 0 }, best = 0;
            for (int y = 0; y < ch; y++)
                for (int v = 0; v < 256; v++)
                    sum[v] += hist[y][d][v];
            for (int v = 1; v < 256; v++)
                best = sum[v] > sum[best] ? v : best;
            all[d] = best ? best : (d ? all[d - 1] : 1);
            for (int y = 0; y < ch; y++) {
                int b = 0, cnt = 0;
                for (int v = 1; v < 256; v++) {
                    b = hist[y][d][v] > hist[y][d][b] ? v : b;
                    cnt += hist[y][d][v];
                }
                color[y][d] = cnt >= 2 && b ? b : -1;
            }
        }
        /* the new picture: letters (the font's mask), plus a ring for tall pictures; then the depth
           from the outside decides the colour level, as in the original */
        memset(tot, 0, sizeof(tot));
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int sx = x - shift, sy = y - 1, on = 0;
                if (w->border && (x == 0 || x == cw - 1 || y == 0 || y == ch - 1))
                    continue;
                for (int dy = tall ? -1 : 0; dy <= (tall ? 1 : 0) && !on; dy++)
                    for (int dx = tall ? -1 : 0; dx <= (tall ? 1 : 0); dx++) {
                        int xx = sx + dx, yy = sy + dy;
                        if (xx >= 0 && xx < cw - 2 && yy >= 0 && yy < ch - 2 && mask[yy * CW + xx] >= 80) {
                            on = 1;
                            break;
                        }
                    }
                tot[y * CW + x] = (uint8_t)on;
            }
        for (int k = 0; k < CW * CH; k++)
            dist[k] = tot[k] ? 1 : 0;
        for (int d = 1; d <= MAXD; d++)
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    int k = y * CW + x, in = dist[k] == d;
                    for (int dy = -1; dy <= 1 && in; dy++)
                        for (int dx = -1; dx <= 1 && in; dx++) {
                            int xx = x + dx, yy = y + dy;
                            in = xx >= 0 && yy >= 0 && xx < cw && yy < ch && dist[yy * CW + xx] >= d;
                        }
                    if (in)
                        dist[k] = (uint8_t)(d + 1);
                }
        for (int k = 0; k < CW * CH; k++)
            nmax = dist[k] - 1 > nmax ? dist[k] - 1 : nmax;
        for (int i = 0; i < n; i++)
            for (int y = 0; y < c[i].h; y++)
                for (int x = 0; x < c[i].w; x++) {
                    int Y = c[i].y - y0 + y, X = c[i].x - x0 + x, k = Y * CW + X, v = 0;
                    if (dist[k]) {
                        /* the letter body of the original's row (depth 3 from its rim, else the deepest
                           level it has), never its dark rim: the new letters are thin */
                        int dd = maxd < 3 ? maxd : 3;
                        for (; dd >= 0 && color[Y][dd] < 0; dd--)
                            ;
                        v = dd >= 0 ? color[Y][dd] : all[maxd < 3 ? maxd : 3];
                        (void)nmax;
                    }
                    tset(t, c[i].u + x, c[i].v + y, v);
                }
    }
    return 1;
}

/* the wide letters when they fit without much squeezing, else the narrow ones */
static int paint(Tex* t, const Cell* c, int n, const Words* w)
{
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
    int   file, entry;  /* part layout whose cells are the picture */
    int   first, count; /* which of its cells (screen order); count 0 = all */
    Words w;
} Pic;

/* English -> German as the player sees them; the user's terms: Target Ziel, Win Sieg, Battle Kampf,
 * Attack Attacke, Damage Schaden, Level Up Levelaufstieg, Press Start Button Starttaste drücken */
static const Pic pics[] = {
    /* battle words (file 0x1A0, italic) */
    { 0x1A0, 3, 0, 1, { "Verfehlt", 1, 0 } },          /* Missed */
    { 0x1A0, 4, 0, 1, { "Attacke", 1, 0 } },           /* Attack (also the right half of Counter Attack) */
    { 0x1A0, 5, 0, 1, { "Konter", 1, 1 } },            /* Counter (Counter Attack: "Konter Attacke") */
    { 0x1A0, 6, 0, 1, { "Unterbrechen", 1, 0, 1 } },   /* Interrupt */
    { 0x1A0, 7, 0, 1, { "Hilfe", 1, 0 } },             /* Assist */
    { 0x1A0, 8, 0, 1, { "Abwehr", 1, 0 } },            /* Guard */
    { 0x1A0, 10, 0, 1, { "Willst du", 1, 0, 1 } },     /* Do you want to */
    { 0x1A0, 10, 1, 1, { "unterbrechen?", 1, 0, 1 } }, /* interrupt? */
    { 0x1A0, 1, 0, 1, { "Ja", 1, 0, 1 } },             /* Yes */
    { 0x1A0, 2, 0, 1, { "Nein", 1, 0, 1 } },           /* No */
    /* battle words (file 0x13B) */
    { 0x13B, 2, 0, 1, { "Schaden", 1, 0, 1 } }, /* Damage */
    { 0x13B, 3, 0, 1, { "ZIEL", 0, 0 } },       /* TARGET */
    { 0x13B, 22, 0, 1, { "WÄHLE", 0, 0 } },     /* SELECT (of SELECT TARGET) */
    /* battle start / end (file 0x313) */
    { 0x313, 1, 0, 1, { "KAMPF", 0, 0 } },        /* BATTLE */
    { 0x313, 2, 0, 1, { "Flucht", 0, 0 } },       /* Run Away (alone: fled) */
    { 0x313, 3, 0, 1, { "Keine", 0, 0 } },        /* Unable to (+ Run Away: "Keine Flucht") */
    { 0x313, 4, 0, 1, { "LOS!", 0, 0 } },         /* FIGHT */
    { 0x313, 5, 0, 0, { "SPIEL VORBEI", 0, 0 } }, /* GAME OVER (two pictures) */
    { 0x313, 6, 0, 1, { "NIEDERLAGE", 0, 0 } },   /* LOSE */
    /* battle result (file 0x25C) */
    { 0x25C, 10, 0, 1, { "SIEG", 0, 0 } },                   /* WIN */
    { 0x25C, 7, 0, 1, { "LEVELAUFSTIEG", 0, 1, 1, 78, 1 } }, /* LEVEL UP: the value of "Next Level" in the result box; right of the label, so
                                                              right-aligned at its old right edge, as wide as the room after "Nächstes Level" */
    /* title (file 0xB64) */
    { 0xB64, 1, 0, 0, { "STARTTASTE DRÜCKEN", 0, 0, 1 } },  /* PRESS START BUTTON (three pictures) */
    { 0xB64, 2, 0, 1, { "NEUES SPIEL", 0, 0, 1, 100, 1 } }, /* NEW GAME */
    { 0xB64, 3, 0, 1, { "FORTSETZEN", 0, 0, 1, 100 } },     /* CONTINUE GAME */
    { 0xB64, 4, 0, 1, { "KAMPFMODUS", 0, 0, 1, 100, 1 } },  /* BATTLE MODE */
};

/* the attack names of the battle effects, set by host/lang.c (skill table) */
#define MAX_SKILLPICS 256
static struct {
    int  file, entry;
    char de[64];
} skillpics[MAX_SKILLPICS];
static int nskillpics;

void PicWords_AddSkill(int32_t parts_entry, const char* de)
{
    if (nskillpics < MAX_SKILLPICS && parts_entry != 0) {
        skillpics[nskillpics].file  = (parts_entry >> 16) & 0xFFFF;
        skillpics[nskillpics].entry = parts_entry & 0xFFFF;
        snprintf(skillpics[nskillpics].de, sizeof(skillpics[nskillpics].de), "%s", de);
        nskillpics++;
    }
}

typedef struct {
    int      id, size, changed;
    uint8_t* d;
} Open;

#define MAX_OPEN 512
static Open open_files[MAX_OPEN];
static int  nopen;

static Open* get_file(int id)
{
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].id == id) {
            return &open_files[i];
        }
    }
    if (nopen == MAX_OPEN) {
        return NULL;
    }
    open_files[nopen].id      = id;
    open_files[nopen].d       = Host_PakReadFileLayered(id, &open_files[nopen].size);
    open_files[nopen].changed = 0;
    return &open_files[nopen++];
}

static void flush_files(void)
{
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
static void grow_rect(Open* f, const Tex* t, Cell* c, int need)
{
    int      el = 0, er = 0, want = need - c->w;
    uint32_t first;
    int      n;

    if (want <= 0) {
        return;
    }
    n     = entries(f->d, f->size);
    first = entry(f->d, f->size, 0);
    (void)first;
    for (int side = 0; side < 2; side++) {
        for (int k = 1; k <= want && el + er < want; k++) {
            int col = side == 0 ? c->u + c->w - 1 + er + 1 : c->u - el - 1, ok = col >= 0 && col < t->w;
            for (int e = 1; e < 4096 && ok; e++) {
                uint32_t o = entry(f->d, f->size, e);
                if (o == 0) {
                    if (e >= n)
                        break;
                    continue;
                }
                if (rd32(f->d + o) == 0x10)
                    continue;
                for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64 && ok; q++, o += 16) {
                    int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
                    if (u == c->u && v == c->v && w == c->w && h == c->h)
                        continue;
                    ok = !(col >= u && col < u + w && c->v < v + h && v < c->v + c->h);
                }
            }
            if (!ok)
                break;
            if (side == 0)
                er++;
            else
                el++;
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
                if (e >= n)
                    break;
                continue;
            }
            if (rd32(f->d + o) == 0x10)
                continue;
            for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
                int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
                if (u == c->u && v == c->v && w == c->w && h == c->h) {
                    int16_t x   = (int16_t)(rd16(f->d + o + 2) + dx);
                    f->d[o]     = (uint8_t)(u + du);
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
static int cell_covers(Open* f, const Cell* c, int x, int y)
{
    int n = entries(f->d, f->size);
    for (int e = 1; e < 4096; e++) {
        uint32_t o = entry(f->d, f->size, e);
        if (o == 0) {
            if (e >= n)
                break;
            continue;
        }
        if (rd32(f->d + o) == 0x10)
            continue;
        for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
            int u = f->d[o], v = f->d[o + 1], w = f->d[o + 8], h = f->d[o + 9] ? f->d[o + 9] : 256;
            if (u == c->u && v == c->v && w == c->w && h == c->h)
                continue;
            if (x >= u && x < u + w && y >= v && y < v + h)
                return 1;
        }
    }
    return 0;
}

/* when widening in place was not enough: move the picture to an empty part of the same texture
   (all pixels transparent and under no other cell, one pixel of room around it), as wide as
   `need`, centred where it was on screen */
static void move_rect(Open* f, const Tex* t, Cell* c, int need, int align)
{
    int n = entries(f->d, f->size), W = need, H = c->h;

    if (getenv("DW2_PICDEBUG"))
        printf("[picwords] move %d,%d %dx%d -> %d wide, texture %dx%d\n", c->u, c->v, c->w, c->h, need, t->w, t->h);
    if (c->w >= need || W + 2 > t->w || W > 255) {
        return;
    }
    /* taken: a pixel that is not transparent or lies under another cell (computed once) */
    static uint8_t taken[256 * 512];
    int            tw = t->w > 256 ? 256 : t->w, th = t->h > 512 ? 512 : t->h;
    memset(taken, 0, sizeof(taken));
    for (int y = 0; y < th; y++) {
        for (int x = 0; x < tw; x++) {
            taken[y * 256 + x] = (uint8_t)(tget(t, x, y) != 0);
        }
    }
    for (int e = 1; e < 4096; e++) {
        uint32_t o = entry(f->d, f->size, e);
        if (o == 0) {
            if (e >= n)
                break;
            continue;
        }
        if (rd32(f->d + o) == 0x10)
            continue;
        for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
            int cu = f->d[o], cv = f->d[o + 1], cw = f->d[o + 8], chh = f->d[o + 9] ? f->d[o + 9] : 256;
            if (cu == c->u && cv == c->v && cw == c->w && chh == c->h)
                continue;
            for (int y = cv; y < cv + chh && y < th; y++) {
                for (int x = cu; x < cu + cw && x < tw; x++) {
                    taken[y * 256 + x] = 1;
                }
            }
        }
    }
    (void)cell_covers;
    for (int v = th - H - 1; v >= 1; v--) {
        for (int u = 1; u + W + 1 <= tw && u + W <= 255; u++) {
            int ok = 1;
            for (int y = v - 1; y <= v + H && ok; y++) {
                for (int x = u - 1; x <= u + W && ok; x++) {
                    ok = !taken[y * 256 + x];
                }
            }
            if (!ok)
                continue;
            for (int e = 1; e < 4096; e++) {
                uint32_t o = entry(f->d, f->size, e);
                if (o == 0) {
                    if (e >= n)
                        break;
                    continue;
                }
                if (rd32(f->d + o) == 0x10)
                    continue;
                for (int q = 0; o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
                    int cu = f->d[o], cv = f->d[o + 1], cw = f->d[o + 8], chh = f->d[o + 9] ? f->d[o + 9] : 256;
                    if (cu == c->u && cv == c->v && cw == c->w && chh == c->h) {
                        int16_t x   = (int16_t)(rd16(f->d + o + 2) - (align > 0 ? W - c->w : (W - c->w) / 2));
                        f->d[o]     = (uint8_t)u;
                        f->d[o + 1] = (uint8_t)v;
                        f->d[o + 8] = (uint8_t)W;
                        memcpy(f->d + o + 2, &x, 2);
                    }
                }
            }
            if (getenv("DW2_PICDEBUG"))
                printf("[picwords] moved to %d,%d\n", u, v);
            /* the original pixels come along (centred): the painter learns its colours from them */
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < c->w; x++) {
                    tset((Tex*)t, u + (align > 0 ? W - c->w : (W - c->w) / 2) + x, v + y, tget(t, c->u + x, c->v + y));
                }
            }
            c->x -= align > 0 ? W - c->w : (W - c->w) / 2;
            c->u  = u;
            c->v  = v;
            c->w  = W;
            return;
        }
    }
}

static int paint_pic(int file, int e, int first, int count, const Words* w)
{
    Open* f = get_file(file);
    Cell  c[MAX_CELLS];
    Tex   t;
    int   n;

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
            move_rect(f, &t, &c[first], w->grow, w->align);
        }
    }
    {
        /* several separate pictures (with gaps between them on screen) and as many words: one word
           each; otherwise the text runs over all of them */
        char words[8][64];
        int  nw = 0, gaps = 0;
        for (const char* p = w->text; *p && nw < 8;) {
            int k = 0;
            while (*p == ' ')
                p++;
            while (*p && *p != ' ' && k < 63)
                words[nw][k++] = *p++;
            words[nw][k] = 0;
            if (k)
                nw++;
        }
        for (int i = first + 1; i < first + count; i++) {
            gaps += c[i].x > c[i - 1].x + c[i - 1].w + 2;
        }
        if (count > 1 && nw == count && gaps && !strchr(w->text, '\n')) {
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

/* These image labels were never translated by the old layer; preserve their
 * existing wording, including the legal notices and controller instruction. */
static void restyle_title_notices(void)
{
    Words copyright  = { "TM & ©AKIYOSHI HONGO, TOEI ANIMATION CO.,LTD.\n©2000 BANDAI CO.,LTD.", 0, -1, 0, 0, 0, 1, 0 };
    Words controller = { "Please, plug a Controller\nin Controller port 2.", 0, -1, 0, 0, 0, 1, 1 };
    int   a          = paint_pic(0xB64, 5, 0, 0, &copyright);
    int   b          = paint_pic(0xB64, 6, 0, 0, &controller);
    printf("[fonts] title notices: %d of 2\n", a + b);
}

/* Stats labels share their texture rectangles across several windows. Repaint
 * those exact rectangles, so every alias changes without touching bars/icons. */
static void restyle_stats(void)
{
    static const struct {
        int         u, v, w, h;
        const char* text;
        int         part;
    } labels[] = {
        { 48, 208, 16, 7, "HP", 73 }, { 32, 180, 16, 7, "MP", 75 }, { 88, 100, 16, 11, "MP", 62 }, { 48, 180, 8, 7, "EL", 5 }, { 84, 168, 4, 7, "/", 75 }
    };
    Open* f = get_file(0x13B);
    Tex   t;
    int   done = 0;
    if (!f || !f->d || !tex_open(&t, f->d, f->size))
        return;
    for (size_t k = 0; k < sizeof(labels) / sizeof(labels[0]); k++) {
        Cell cells[MAX_CELLS];
        int  n = cells_of(f->d, f->size, labels[k].part, cells);
        if (!n)
            continue;
        Cell c      = cells[0];
        c.u         = labels[k].u;
        c.v         = labels[k].v;
        c.w         = labels[k].w;
        c.h         = labels[k].h;
        int     ink = picture_ink(&t, &c);
        uint8_t mask[16 * 11];
        if (Fonts_Mask(FONT_TELEMETRY, labels[k].text, mask, c.w, c.w, c.h) < 0)
            continue;
        for (int y = 0; y < c.h; y++)
            for (int x = 0; x < c.w; x++)
                tset(&t, c.u + x, c.v + y, mask[y * c.w + x] >= 80 ? ink : 0);
        done++;
        f->changed = 1;
    }
    printf("[fonts] stats image labels: %d of 5\n", done);
}

/* the battle effect name: a part list (file, entry) whose parts each draw a picture */
static int paint_skill(int file, int e, const char* de)
{
    Open*    f = get_file(file);
    uint32_t o;
    int      done = 0;
    Words    w    = { de, 1, 0, 0, 0 };

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
static void dump_files(void)
{
    const char* dir = getenv("DW2_PICDUMP");
    char        path[1024];
    if (dir == NULL) {
        return;
    }
    if (font22 != NULL) { /* the letter font as read (to check the glyph positions) */
        FILE* f;
        snprintf(path, sizeof(path), "%s/022B_font.bin", dir);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(font22, 1, (size_t)font22_size, f);
            fclose(f);
        }
    }
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].d != NULL && open_files[i].changed) {
            FILE* f;
            snprintf(path, sizeof(path), "%s/%04X.bin", dir, open_files[i].id);
            f = fopen(path, "wb");
            if (f != NULL) {
                fwrite(open_files[i].d, 1, (size_t)open_files[i].size, f);
                fclose(f);
            }
        }
    }
}

static int  german = -1;
static void restyle_status(Tex* t);
static void collect_skill_names(void)
{
    int      size = 0;
    uint8_t* d    = Host_PakReadFileLayered(0x25B, &size);
    nskillpics    = 0;
    if (!d || size < 0x44) {
        free(d);
        return;
    }
    for (uint32_t r = rd32(d); r <= (uint32_t)size - 0x44; r += 0x44) {
        if (rd16(d + r) == 0)
            break;
        uint32_t np = rd32(d + r + 0x24);
        char     name[64];
        if (np >= (uint32_t)size)
            continue;
        DevData_Text(d + np, size - (int)np, NULL, name, sizeof(name));
        /* Decode the name from the current layer, never infer a translation. */
        if (*name && !strchr(name, '{'))
            PicWords_AddSkill((int32_t)rd32(d + r + 0x0C), name);
    }
    free(d);
}

void PicWords_Apply(void)
{
    int  ok = 0, bad = 0, sk_ok = 0, sk_bad = 0;
    char a[64], b[64];

    collect_skill_names();
    font22 = Host_PakReadFileLayered(0x22B, &font22_size);
    if (font22 == NULL || !tex_open(&font22_tex, font22, font22_size)) {
        free(font22);
        font22 = NULL;
        memset(&font22_tex, 0, sizeof(font22_tex));
        Lang_Note("word pictures: no file 0x22B, restyling unavailable%s", "", "");
        return;
    }
    static const char* const en[] = {
        "Missed", "Attack", "Counter", "Interrupt", "Assist", "Guard", "Do you want to", "interrupt?", "Yes", "No",
        "Damage", "TARGET", "SELECT", "BATTLE", "Run Away", "Unable to", "FIGHT", "GAME OVER", "LOSE",
        "WIN", "LEVEL UP", "PRESS START BUTTON", "NEW GAME", "CONTINUE GAME", "BATTLE MODE"
    };
    _Static_assert(sizeof(en) / sizeof(en[0]) == sizeof(pics) / sizeof(pics[0]), "picture texts");
    for (size_t i = 0; i < sizeof(pics) / sizeof(pics[0]); i++) {
        Words word = pics[i].w;
        if (german != 1)
            word.text = en[i];
        if (paint_pic(pics[i].file, pics[i].entry, pics[i].first, pics[i].count, &word)) {
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
    restyle_stats();
    restyle_title_notices();
    dump_files();
    flush_files();
    restyle_status(&font22_tex);
    Host_PakSetLayer(0x22B, font22, font22_size);
    free(font22);
    font22 = NULL;
    memset(&font22_tex, 0, sizeof(font22_tex));
    snprintf(a, sizeof(a), "%d of %d", ok, ok + bad);
    snprintf(b, sizeof(b), "%d of %d attack names", sk_ok, sk_ok + sk_bad);
    Lang_Note("modern word pictures: %s, %s", a, b);
}

/* ---- status messages of the battle: German letter-cell lists (file 0x22B) ---- */

static const char* const status_de[] = {
    /* 1.. in file order (English in the comments) */
    "Vergiftet",                     /* Poisoned */
    "Gift geheilt",                  /* Poison cured */
    "Gelaehmt",                      /* Paralyzed */
    "Laehmung geheilt",              /* Paralysis cured */
    "Verwirrt",                      /* Confused */
    "Verwirrung geheilt",            /* Confusion cured */
    "Starke Attacke",                /* Powerful attack */
    "Kein Konter moeglich",          /* Unable to counter */
    "Angriff gesenkt",               /* Attack power down */
    "Abwehr gesenkt",                /* Defense down */
    "Tempo gesenkt",                 /* Quickness down */
    "Motivation gesenkt",            /* Motivation down */
    "Motivation zurueck",            /* Motivation back */
    "Benommen",                      /* Dizzy */
    "Keine HP-Heilung",              /* Can't recover HP */
    "HP-Heilung wieder moeglich",    /* Can recover HP */
    "Keine Zustandsheilung",         /* Can't recover status */
    "Zustandsheilung moeglich",      /* Can recover status */
    "HP gesaugt",                    /* Suck HP */
    "Angriff & Abwehr gesenkt",      /* Attack&Defense down */
    "Angriff erhoeht",               /* Attack power up */
    "Abwehr erhoeht",                /* Defense Power up */
    "Tempo erhoeht",                 /* Quickness is up */
    "Wiederbelebt",                  /* Revived */
    "Attacke wirkungslos",           /* Attack ineffective */
    "Attacke wirksam",               /* Attack effective */
    "Giftkraft erhalten",            /* Gained poisoning power */
    "Giftkraft verloren",            /* Lost poisoning power */
    "Laehmkraft erhalten",           /* Gained paralysis power */
    "Laehmkraft verloren",           /* Lost paralysis power */
    "Verwirrkraft erhalten",         /* Gained confusion power */
    "Verwirrkraft verloren",         /* Lost confusion power */
    "Wasserkraft erhalten",          /* Gained water power */
    "Wasserkraft verloren",          /* Lost water power */
    "Feuerkraft erhalten",           /* Gained fire power */
    "Feuerkraft verloren",           /* Lost fier power */
    "Naturkraft erhalten",           /* Gained nature power */
    "Naturkraft verloren",           /* Lost nature power */
    "Maschinenkraft erhalten",       /* Gained machine power */
    "Maschinenkraft verloren",       /* Lost machine power */
    "Dunkelkraft erhalten",          /* Gained darkness power */
    "Dunkelkraft verloren",          /* Lost darkness power */
    "Wurde giftig",                  /* Became poisonous */
    "Kann nicht zielen",             /* Unable to aim */
    "Kann wieder zielen",            /* Able to aim */
    NULL,                            /* A (placeholder of the game) */
    "Zustand geheilt",               /* Recover status */
    "Werte geheilt",                 /* Recover parameter */
    "Technikkraft gesenkt",          /* Lower tech power */
    "Technik wird normaler Angriff", /* Turn a tech to normal attack */
    "Technik weggefegt",             /* Blow away a tech */
    "Technik verzoegert",            /* Delay a tech */
    "Technik uebernommen",           /* Take over a tect */
    "MP wiederverwendet",            /* Reuse MP */
    "Technik gestohlen",             /* Steal a tech */
    "Attacke abgeprallt",            /* Bounced off an attack */
    "Wurde zum Zombie",              /* Became a zombi */
    "Werte zurueckgesetzt",          /* Initialize parameters */
    "Werte zurueckgesetzt",          /* (the same, misspelt in the game) */
    "Heilung & Abwehr erhoeht",      /* Increased recovery&defense */
    "Panzer verstaerkt",             /* Strengthen armor */
    "Unbekannte Attacke",            /* Unknown attack */
    "Wurde zum Schild",              /* Be a shield */
    "MP gesenkt",                    /* Lower MP */
    "MP gesaugt",                    /* Suck MP */
    "Zufallsattacke",                /* Random attack */
    "Giftkoerper verloren",          /* Lost poisonous body */
    "Voll geheilt",                  /* fully recovered */
};
#define NSTATUS ((int)(sizeof(status_de) / sizeof(status_de[0])))

/* glyph positions of the font of file 0x22B (12 x 15 cells) */
static int status_glyph(int ch, int* u, int* v)
{
    static const struct {
        const char* s;
        int         x, y;
    } rows[] = {
        { "ABCD", 68, 64 },
        { "EFGH", 68, 80 },
        { "IJKLM", 64, 96 },
        { "NOPQR", 0, 104 },
        { "zyxwv", 60, 112 },
        { "STTUu", 0, 120 },
        { "tsrqp", 60, 128 },
        { "onmlk", 0, 136 },
        { "jihgf", 60, 144 },
        { "edcba", 0, 152 },
        { "ZYXWV", 60, 160 },
    };
    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        const char* p = strchr(rows[r].s, ch);
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

static void restyle_status(Tex* t)
{
    for (int cp = 'A'; cp <= 'z'; cp++) {
        int     u, v;
        uint8_t mask[12 * 15];
        if (!status_glyph(cp, &u, &v) || !Fonts_Cell(FONT_DISPLAY, cp, mask, 12, 15))
            continue;
        for (int y = 0; y < 15; y++)
            for (int x = 0; x < 12; x++)
                tset(t, u + x, v + y, mask[y * 12 + x] >= 80 ? 3 : 0);
    }
    int     u, v;
    uint8_t mask[12 * 15];
    if (status_glyph('&', &u, &v) && Fonts_Cell(FONT_DISPLAY, '&', mask, 12, 15))
        for (int y = 0; y < 15; y++)
            for (int x = 0; x < 12; x++)
                tset(t, u + x, v + y, mask[y * 12 + x] >= 80 ? 3 : 0);
}

static uint8_t status_cells[NSTATUS + 1][64 * 16 + 16];
static uint8_t status_built[NSTATUS + 1];
static int     status_ready = -1;

static void build_status(void)
{
    int      size = 0;
    uint8_t* d    = Host_PakReadFileLayered(0x22B, &size);

    status_ready = 0;
    if (d == NULL) {
        return;
    }
    for (int e = 1; e <= NSTATUS; e++) {
        uint32_t    o = entry(d, size, e);
        const char* s = status_de[e - 1];
        uint8_t     proto[16];
        int         len, n = 0, x;

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
            uint8_t* c  = status_cells[e] + n * 16;
            int16_t  xx = (int16_t)x, yy = -8; /* modern font cells share one baseline */
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
        status_built[e]         = n > 0;
    }
    free(d);
    status_ready = 1;
}

void PicWords_SetGerman(int on)
{
    german = on;
}

const void* Lang_PartCells(int32_t file_id, const void* cells)
{
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
