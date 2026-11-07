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
    int         role;   /* 0 display, 1 interface, 2 telemetry, 3 interface bold (the title words) */
    int         border; /* preserve the enclosing one-pixel frame */
    int         squeeze; /* dw2-pc-port: up to this many tenths narrower instead of smaller (25: the
                            title's code, set in a very narrow face) */
    int         tight;   /* the whole cell, no margin (the tiny stats labels HP / MP / EL) */
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
/* The letters of a picture as coverage (0..255), k times the picture's size: the same layout at
 * every k (k = 1: the painted texture; k = the HD renderer's scale: the runtime picture). The
 * picture spans the cells' screen box; each line is fitted into the cells it crosses. */
/* pad: output pixels kept free around the letters (the outline rings drawn outside them) */
static int words_mask_pad(const Cell* c, int n, const Words* w, int k, uint8_t* mask, int stride, int pad);
static int words_mask(const Cell* c, int n, const Words* w, int k, uint8_t* mask, int stride)
{
    return words_mask_pad(c, n, w, k, mask, stride, 0);
}

static int words_mask_pad(const Cell* c, int n, const Words* w, int k, uint8_t* mask, int stride, int pad)
{
    int x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999;
    if (n <= 0 || k < 1)
        return 0;
    for (int i = 0; i < n; i++) {
        x0 = c[i].x < x0 ? c[i].x : x0;
        x1 = c[i].x + c[i].w > x1 ? c[i].x + c[i].w : x1;
        y0 = c[i].y < y0 ? c[i].y : y0;
        y1 = c[i].y + c[i].h > y1 ? c[i].y + c[i].h : y1;
    }
    int cw = x1 - x0, ch = y1 - y0;
    if ((w->tight ? cw < 3 || ch < 5 : cw <= 8 || ch <= 6) || cw > CW || ch > CH || cw * k > stride)
        return 0;
    memset(mask, 0, (size_t)stride * ch * k);
    int role  = w->role == 1 ? FONT_INTERFACE : w->role == 2 ? FONT_TELEMETRY : w->role == 3 ? FONT_INTERFACE_BOLD
                                                                                     : FONT_DISPLAY;
    int lines = 1;
    for (const char* p = w->text; *p; p++)
        if (*p == '\n')
            lines++;
    int m2 = w->tight ? 0 : 2, line_h = (ch - m2) / lines, LH = line_h * k - 2 * pad, LS = line_h * k;
    /* the big battle and title words in Orbitron Black: the originals are heavy letters with
       outline rings and a bevel (their colours by depth, depth_colour), Bold left no body inside
       the rings; small lines and the narrow title code keep Orbitron Bold */
    if (role == FONT_DISPLAY && line_h >= 14 && w->squeeze <= 10)
        role = FONT_DISPLAY_HEAVY;
    if (line_h < 1 || LH < 2)
        return 0;
    uint8_t* row  = malloc((size_t)stride * LH);
    uint8_t* wide = malloc((size_t)stride * LH);
    int      ok   = row != NULL && wide != NULL;
    const char* p = w->text;
    for (int line = 0; line < lines && ok; line++) {
        const char* end = strchr(p, '\n');
        size_t      len = end ? (size_t)(end - p) : strlen(p);
        char        label[512];
        if (len >= sizeof(label)) {
            ok = 0;
            break;
        }
        memcpy(label, p, len);
        label[len] = 0;
        /* The second copyright line has only one cell, while the first spans
         * three. Fit each line to its actual mapped texture coverage. */
        int ymid = y0 + m2 / 2 + line * line_h + line_h / 2, lo = cw, hi = 0;
        for (int i = 0; i < n; i++)
            if (ymid >= c[i].y && ymid < c[i].y + c[i].h) {
                int a = c[i].x - x0, b = a + c[i].w;
                if (a < lo)
                    lo = a;
                if (b > hi)
                    hi = b;
            }
        int available = (hi - lo - m2) * k - 2 * pad;
        if (available < 1) {
            ok = 0;
            break;
        }
        int width;
        memset(row, 0, (size_t)stride * LH);
        if (w->squeeze > 10) {
            /* full height, then narrower: each output column is half the mean, half the strongest
               coverage of the columns it covers (the strongest alone merged capitals such as WORK
               into blocks, the mean alone lost the thin I of NIEDERLAGE) */
            int room = available * w->squeeze / 10, ws = room > stride ? room : stride;
            uint8_t* wbuf = ws > stride ? malloc((size_t)ws * LH) : wide; /* the squeeze's own width */
            if (!wbuf) {
                ok = 0;
                break;
            }
            memset(wbuf, 0, (size_t)ws * LH);
            width = Fonts_Mask(role, label, wbuf, ws, room, LH);
            if (width < 0) {
                if (wbuf != wide)
                    free(wbuf);
                ok = 0;
                break;
            }
            if (width > available) {
                for (int y = 0; y < LH; y++)
                    for (int x = 0; x < available; x++) {
                        int a = x * width / available, b = (x + 1) * width / available, m = 0, nk = 0, mx = 0;
                        for (int q = a; q < (b > a ? b : a + 1); q++, nk++) {
                            m += wbuf[y * ws + q];
                            mx = wbuf[y * ws + q] > mx ? wbuf[y * ws + q] : mx;
                        }
                        row[y * stride + x] = (uint8_t)((m / nk + mx) / 2);
                    }
                width = available;
            } else {
                for (int y = 0; y < LH; y++)
                    memcpy(row + y * stride, wbuf + y * ws, (size_t)width);
            }
            if (wbuf != wide)
                free(wbuf);
        } else {
            width = Fonts_Mask(role, label, row, stride, available, LH);
        }
        if (width < 0) {
            ok = 0;
            break;
        }
        int left = lo * k + pad + (w->align < 0 ? 0 : w->align > 0 ? available - width
                                                                   : (available - width) / 2);
        for (int y = 0; y < LH; y++)
            memcpy(mask + (line * LS + pad + y) * stride + left, row + y * stride, (size_t)width);
        if (end)
            p = end + 1;
    }
    free(row);
    free(wide);
    return ok;
}

/* dw2-pc-port: what a painted picture needs to be painted again at the output resolution
 * (registered as a runtime index source per file by flush_files) */
typedef struct {
    const uint8_t* file; /* Tex.d of the painting (matched to its file at the flush) */
    int            file_id; /* set when its file registers (register_hifile) */
    Cell           c[MAX_CELLS];
    int            n, x0, y0, cw, ch;
    uint8_t        col[MAX_CELLS]; /* the palette index of each cell's letters */
    Words          w;
    char           text[512];
    /* the original's layers (paint1): outline rings from the outside in, the body, a bevel (top /
       left, bottom / right; 0 none); nring < 0: no layers, the body colour col (small pictures) */
    int            nring;
    uint8_t        ring[4], body, bevel_tl, bevel_br;
    int            box;          /* the letters sit in a box (title menu): the box stays */
    uint8_t        rowcol[CH];   /* the letters' colour per row of the original (its vertical gradient) */
    int            oy0, oy1;     /* the rows of the original's letters */
    int            gradient;     /* body colour by row (else flat: body) */
    uint8_t*       hmask[2]; /* runtime pictures at k = hk[i] (the zooming words use two sizes at once):
                                with ndepth the palette index of each pixel, else the coverage */
    int            hk[2];
    size_t         hbytes[2];
} HiPic;
static size_t hmask_bytes; /* all runtime masks; past HMASK_LIMIT they are made again when needed */
#define HMASK_LIMIT ((size_t)64 << 20)
#define MAX_HIPICS 1024
static HiPic hipics[MAX_HIPICS];
static int   nhipics;

typedef struct {
    int nring;
    uint8_t ring[4], body, bevel_tl, bevel_br;
    int box;
    uint8_t rowcol[CH];
    int oy0, oy1;
    uint8_t fill; /* a box picture's fill */
    int gradient; /* the letters' colour changes by row (four or more bands: the title words) */
} Layers;

/* the original's letter colour at row y of a new letter band [y0, y1] (the same relative row) */
static int row_colour(const uint8_t* rowcol, int oy0, int oy1, int body, int y, int y0, int y1)
{
    if (oy0 < 0 || y1 <= y0)
        return body;
    int r = oy0 + (int)((long)(y - y0) * (oy1 - oy0) / (y1 - y0) + 0.5);
    r     = r < oy0 ? oy0 : r > oy1 ? oy1 : r;
    for (int d = 0; d <= oy1 - oy0; d++) { /* the nearest row with letters */
        if (r - d >= oy0 && rowcol[r - d])
            return rowcol[r - d];
        if (r + d <= oy1 && rowcol[r + d])
            return rowcol[r + d];
    }
    return body;
}

static void hipic_add(const Tex* t, const Cell* c, int n, const Words* w, const uint8_t* col, const Layers* ly)
{
    if (nhipics == MAX_HIPICS || n > MAX_CELLS)
        return;
    HiPic* h = &hipics[nhipics];
    int    x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999;
    for (int i = 0; i < n; i++) {
        x0 = c[i].x < x0 ? c[i].x : x0;
        x1 = c[i].x + c[i].w > x1 ? c[i].x + c[i].w : x1;
        y0 = c[i].y < y0 ? c[i].y : y0;
        y1 = c[i].y + c[i].h > y1 ? c[i].y + c[i].h : y1;
        h->c[i]   = c[i];
        h->col[i] = col[i];
    }
    h->file    = t->d;
    h->file_id = 0;
    h->n    = n;
    h->x0   = x0;
    h->y0   = y0;
    h->cw   = x1 - x0;
    h->ch   = y1 - y0;
    h->w    = *w;
    snprintf(h->text, sizeof(h->text), "%s", w->text);
    h->w.text = h->text;
    h->nring = ly ? ly->nring : -1;
    if (ly) {
        memcpy(h->ring, ly->ring, sizeof(h->ring));
        h->body     = ly->body;
        h->bevel_tl = ly->bevel_tl;
        h->bevel_br = ly->bevel_br;
        h->box      = ly->box;
        memcpy(h->rowcol, ly->rowcol, sizeof(h->rowcol));
        h->oy0 = ly->oy0;
        h->oy1 = ly->oy1;
        h->gradient = ly->gradient;
    }
    memset(h->hmask, 0, sizeof(h->hmask));
    memset(h->hk, 0, sizeof(h->hk));
    memset(h->hbytes, 0, sizeof(h->hbytes));
    nhipics++;
}

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
    if (!words_mask(c, n, w, 1, mask, CW))
        return 0;
    int shift = 1;
    /* dw2-pc-port: the colours of the original picture. The original is a gradient with an outline
     * (bright letter body, darker rim): learn its colour per row and per depth from the original
     * pixels (a flat "brightest palette entry" came out nearly black on the dark battle floor),
     * and give tall pictures a one-pixel outline like the original's. Small pictures get the body
     * colour only: an outline fills the counters of the small letters. */
    {
        enum { MAXD = 10 };
        static uint8_t orig[CW * CH], dist[CW * CH], tot[CW * CH], odist[CW * CH], core[CW * CH];
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
        memcpy(odist, dist, sizeof(odist)); /* the original's depths (its layers, below) */
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
            {
                uint8_t col[MAX_CELLS];
                for (int i = 0; i < n && i < MAX_CELLS; i++)
                    col[i] = (uint8_t)inks[i];
                hipic_add(t, c, n, w, col, NULL);
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
        Layers ly;
        memset(&ly, 0, sizeof(ly));
        {
            /* the body: the most common colour at depths 4..7 (deeper ones cross into the
               neighbouring letters' rims); the rings: the depths before it; the bevel: the
               colours at the first body depth whose outside is up / left, or down / right */
            /* the body from the strokes' cores (pixels at least two deep whose depth is a local
               maximum: the middle of each stroke, thin letters included); a colour taking at least a
               tenth of the cores belongs to the body; the rings: the depths before the first body
               colour */
            int sum[256] = { 0 }, tl[256] = { 0 }, br[256] = { 0 }, b = 0, cores = 0;
            memset(core, 0, sizeof(core));
            for (int y = 1; y + 1 < ch; y++)
                for (int x = 1; x + 1 < cw; x++) {
                    int kk = y * CW + x, d0 = odist[kk];
                    if (d0 < 3 || odist[kk - 1] > d0 || odist[kk + 1] > d0 || odist[kk - CW] > d0 || odist[kk + CW] > d0)
                        continue;
                    core[kk] = 1;
                    sum[orig[kk]]++;
                    cores++;
                }
            for (int v = 1; v < 256; v++)
                b = sum[v] > sum[b] ? v : b;
            if (!b)
                b = all[maxd];
            ly.body = (uint8_t)b;
            {
                /* a box picture's fill: the most common colour at depths 4..7 (its inside) */
                int fs[256] = { 0 }, f = 0;
                for (int d = 4; d <= 7 && d <= maxd; d++)
                    for (int y = 0; y < ch; y++)
                        for (int v = 1; v < 256; v++)
                            fs[v] += hist[y][d][v];
                for (int v = 1; v < 256; v++)
                    f = fs[v] > fs[f] ? v : f;
                ly.fill = (uint8_t)(f ? f : b);
            }
            /* at most three rings: the fourth depth is the bevel (a mix of its colours) */
            while (ly.nring < 3 && ly.nring <= maxd && all[ly.nring] != b && !(cores && sum[all[ly.nring]] * 10 >= cores))
                ly.nring++;
            for (int d = 0; d < ly.nring; d++)
                ly.ring[d] = (uint8_t)all[d];
            for (int y = 1; y < ch; y++)
                for (int x = 1; x < cw; x++) {
                    int kk = y * CW + x;
                    if (odist[kk] - 1 != ly.nring)
                        continue;
                    if (odist[kk - 1] - 1 < ly.nring || odist[kk - CW] - 1 < ly.nring)
                        tl[orig[kk]]++;
                    else
                        br[orig[kk]]++;
                }
            int t1 = 0, b1 = 0;
            for (int v = 1; v < 256; v++) {
                t1 = tl[v] > tl[t1] ? v : t1;
                b1 = br[v] > br[b1] ? v : b1;
            }
            ly.bevel_tl = (uint8_t)(t1 && t1 != b ? t1 : 0);
            ly.bevel_br = (uint8_t)(b1 && b1 != b ? b1 : 0);
        }
            {
            /* a box picture (the title menu): most of its rectangle is opaque; its letters are the
               colours that are neither the box's fill nor its frame. Otherwise the letters' body
               is the depth past the rings. Their colour per row: the vertical gradient. */
            int opaque = 0, rows[CH][16], rn[CH], bx0 = cw, bx1 = -1;
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++)
                    if (orig[y * CW + x]) {
                        opaque++;
                        bx0 = x < bx0 ? x : bx0;
                        bx1 = x > bx1 ? x : bx1;
                    }
            /* over the original's columns (a picture widened for its German text has new empty
               columns beside its box) */
            ly.box = bx1 - bx0 > 8 && opaque * 100 >= (bx1 - bx0 + 1) * ch * 95;
            if (ly.box) {
                ly.body  = ly.fill; /* the box: rings, fill; its letters by row */
                ly.nring = 0;
            }
            if (ly.box && (bx0 > 0 || bx1 < cw - 1)) {
                /* the box stretched over the whole width: its frame columns at both ends, a column of
                   its fill between */
                static uint8_t wide[CW * CH];
                int            bw = bx1 - bx0 + 1;
                for (int y = 0; y < ch; y++)
                    for (int x = 0; x < cw; x++) {
                        int sx = x < 3 ? x : x >= cw - 3 ? bw - (cw - x) : x - bx0 >= 3 && x - bx0 < bw - 3 ? x - bx0 : 3;
                        wide[y * CW + x] = orig[y * CW + bx0 + (sx < 0 ? 0 : sx >= bw ? bw - 1 : sx)];
                    }
                for (int y = 0; y < ch; y++)
                    memcpy(orig + y * CW, wide + y * CW, (size_t)cw);
            }
            ly.oy0 = ly.oy1 = -1;
            memset(rn, 0, sizeof(rn));
            for (int y = 0; y < ch; y++) {
                int hst[256] = { 0 }, best = 0;
                for (int x = 0; x < cw; x++) {
                    int kk = y * CW + x, v = orig[kk];
                    if (!v)
                        continue;
                    if (ly.box ? (v == ly.fill || v == all[0] || v == all[1]) : !core[kk])
                        continue;
                    hst[v]++;
                }
                for (int v = 1; v < 256; v++)
                    best = hst[v] > hst[best] ? v : best;
                ly.rowcol[y] = (uint8_t)best;
                if (best) {
                    if (ly.oy0 < 0)
                        ly.oy0 = y;
                    ly.oy1 = y;
                }
            }
            (void)rows;
            {
                /* distinct colours of bands at least two rows high, each new one below the last: a
                   gradient (the title words: 5-6 bands); noise between core rows is no band */
                int seen[256] = { 0 }, bands = 0;
                for (int y = 0; y + 1 < ch; y++) {
                    int v = ly.rowcol[y];
                    if (v && ly.rowcol[y + 1] == v && !seen[v]) {
                        seen[v] = 1;
                        bands++;
                    }
                }
                ly.gradient = ly.box || bands >= 4;
            }
        }
        int ny0 = -1, ny1 = -1;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                if (tot[y * CW + x]) {
                    if (ny0 < 0)
                        ny0 = y;
                    ny1 = y;
                }
        for (int i = 0; i < n; i++)
            for (int y = 0; y < c[i].h; y++)
                for (int x = 0; x < c[i].w; x++) {
                    int Y = c[i].y - y0 + y, X = c[i].x - x0 + x, k = Y * CW + X, v = 0;
                    if (dist[k]) {
                        /* one colour: the letter body of the original (its most common colour at
                           depth 3 from its rim), never the dark rim. A per-row colour put the rim's
                           rows of the original (top and bottom of its letters) as dark bands across
                           the thin new letters ("Verfehlt" read "Verfehit"). */
                        v = all[maxd < 3 ? maxd : 3];
                        if (ly.box)
                            v = row_colour(ly.rowcol, ly.oy0, ly.oy1, v, Y, ny0, ny1);
                        (void)nmax;
                        (void)color;
                    } else if (ly.box) {
                        /* the box (fill and frame) stays; the old letters in it become fill */
                        v = orig[k];
                        if (v && v != ly.fill && v != all[0] && v != all[1])
                            v = ly.fill;
                    }
                    tset(t, c[i].u + x, c[i].v + y, v);
                }
        {
            uint8_t col[MAX_CELLS];
            for (int i = 0; i < n && i < MAX_CELLS; i++)
                col[i] = (uint8_t)all[maxd < 3 ? maxd : 3];
            /* the original's colour per depth from its rim (outline rings, bevels, the gradient
               to the bright core): drawn at the output resolution by relative depth in the new
               strokes (hifile_index); the 1x picture above keeps the body colour */
            {
                /* layers only for the big pictures (battle words, attack names, title menu): the small
                   ones (title code, copyright, small labels) keep their body colour, their rings
                   would fill the letters */
                int lines = 1;
                for (const char* q = w->text; *q; q++)
                    lines += *q == '\n';
                hipic_add(t, c, n, w, col, (ch - 2) / lines >= 14 && w->squeeze <= 25 ? &ly : NULL);
            }
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
    { 0x1A0, 10, 0, 2, { "Willst du unterbrechen?", 1, 0, 1 } }, /* Do you want to interrupt? (one line over its two
                                                                    cells: painted one cell each, the long "unterbrechen?"
                                                                    came out much smaller than "Willst du") */
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
    { 0x313, 6, 0, 1, { "NIEDERLAGE", 0, 0 } },   /* LOSE (squeezed narrower, the 1-pixel gap of Orbitron between N and I closed: "NEDERLAGE") */
    /* battle result (file 0x25C) */
    { 0x25C, 10, 0, 1, { "SIEG", 0, 0 } },                   /* WIN */
    { 0x25C, 7, 0, 1, { "LEVELAUFSTIEG", 0, 1, 1, 78, 1 } }, /* LEVEL UP: the value of "Next Level" in the result box; right of the label, so
                                                              right-aligned at its old right edge, as wide as the room after "Nächstes Level" */
    /* title (file 0xB64) */
    /* the title words: tall and narrow like the originals (squeezed instead of set smaller: the
       German words are as long as the English ones, Orbitron is a wide face) */
    { 0xB64, 1, 0, 0, { "STARTTASTE DRÜCKEN", 0, 0, 1, 0, 0, 3, 0, 25 } },  /* PRESS START BUTTON (three pictures) */
    { 0xB64, 2, 0, 1, { "NEUES SPIEL", 0, 0, 1, 100, 1, 3, 0, 14 } }, /* NEW GAME */
    { 0xB64, 3, 0, 1, { "FORTSETZEN", 0, 0, 1, 100, 0, 3, 0, 14 } },  /* CONTINUE GAME */
    { 0xB64, 4, 0, 1, { "KAMPFMODUS", 0, 0, 1, 100, 1, 3, 0, 14 } },  /* BATTLE MODE */
};

/* the attack names of the battle effects, set by host/lang.c (skill table) */
#define MAX_SKILLPICS 256
static struct {
    int  file, entry;
    char de[64];
} skillpics[MAX_SKILLPICS];
static int nskillpics;

/* the texture rectangles of file `file` under painted word pictures (host/uipanels.c: no panel
   style there); returns how many (at most max), as u, v, w, h */
int PicWords_Rects(int file, int* r, int max)
{
    int n = 0;
    for (int i = 0; i < nhipics; i++) {
        const HiPic* p = &hipics[i];
        if (p->file_id != file)
            continue;
        for (int j = 0; j < p->n && n < max; j++, n++) {
            r[4 * n]     = p->c[j].u;
            r[4 * n + 1] = p->c[j].v;
            r[4 * n + 2] = p->c[j].w;
            r[4 * n + 3] = p->c[j].h;
        }
    }
    return n;
}

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

/* dw2-pc-port: the painted pictures at the output resolution. Each changed file's texture is a
 * runtime index source (backend/texrep.h): its texels k times larger, and over them every picture
 * painted into it, its letters rasterized again at k times the size (words_mask: the same layout,
 * the same palette index per cell). Pictures the painter did not record stay the 1x texels. */
#include "backend/texrep.h"
typedef struct {
    uint8_t* pix; /* the texture as uploaded */
    int      stride, w, h, bits;
    int      ids[MAX_HIPICS > 256 ? 256 : MAX_HIPICS], nids;
} HiFile;
static HiFile hifiles[MAX_OPEN];
static int    nhifiles;

static int hifile_texel(const HiFile* f, int x, int y)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h)
        return 0;
    if (f->bits == 8)
        return f->pix[y * f->stride + x];
    return (f->pix[y * f->stride + x / 2] >> ((x & 1) * 4)) & 15;
}

/* The original's layers at the output resolution (paint1 learned them): rings of ring_w output
 * pixels each around the new letters (outermost = ring[0]), a bevel of the same width just inside
 * (top / left and bottom / right colours), the body. Ring width: half a texel, at least one pixel:
 * the PS1 rings are one texel each; at the output resolution half of it reads as a clean outline
 * without filling the letters. */
static int ring_w(int k) { return k >= 2 ? (k + 1) / 2 : 1; }
static int layer_pad(const HiPic* p, int k) { return p->nring > 0 && !p->box ? p->nring * ring_w(k) + 1 : 0; }

/* chamfer (3-4) distance to the nearest pixel where in != inside, in 1/3 pixels (0 there) */
static void chamfer(const uint8_t* m, int w, int h, int inside, uint16_t* d)
{
    for (int i = 0; i < w * h; i++)
        d[i] = ((m[i] >= 80) == inside) ? 0xFFFF : 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t* q = &d[y * w + x];
            if (!*q)
                continue;
            int b = *q;
            if (x > 0 && d[y * w + x - 1] + 3 < b) b = d[y * w + x - 1] + 3;
            if (y > 0) {
                if (d[(y - 1) * w + x] + 3 < b) b = d[(y - 1) * w + x] + 3;
                if (x > 0 && d[(y - 1) * w + x - 1] + 4 < b) b = d[(y - 1) * w + x - 1] + 4;
                if (x + 1 < w && d[(y - 1) * w + x + 1] + 4 < b) b = d[(y - 1) * w + x + 1] + 4;
            }
            *q = (uint16_t)b;
        }
    for (int y = h - 1; y >= 0; y--)
        for (int x = w - 1; x >= 0; x--) {
            uint16_t* q = &d[y * w + x];
            if (!*q)
                continue;
            int b = *q;
            if (x + 1 < w && d[y * w + x + 1] + 3 < b) b = d[y * w + x + 1] + 3;
            if (y + 1 < h) {
                if (d[(y + 1) * w + x] + 3 < b) b = d[(y + 1) * w + x] + 3;
                if (x + 1 < w && d[(y + 1) * w + x + 1] + 4 < b) b = d[(y + 1) * w + x + 1] + 4;
                if (x > 0 && d[(y + 1) * w + x - 1] + 4 < b) b = d[(y + 1) * w + x - 1] + 4;
            }
            *q = (uint16_t)b;
        }
}

static void layer_colour(const HiPic* p, int k, uint8_t* m, int stride, int h)
{
    int       w = stride, rw = ring_w(k);
    uint16_t* din  = malloc((size_t)w * h * sizeof(uint16_t));
    uint16_t* dout = malloc((size_t)w * h * sizeof(uint16_t));
    if (!din || !dout) {
        free(din);
        free(dout);
        return;
    }
    chamfer(m, w, h, 1, din);
    chamfer(m, w, h, 0, dout);
    int ny0 = -1, ny1 = -1; /* the new letters' rows (their band, for the original's gradient) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (din[y * w + x]) {
                if (ny0 < 0)
                    ny0 = y;
                ny1 = y;
            }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = y * w + x, v = 0;
            if (din[i] != 0) {
                /* the body: the original letters' colour of the same relative row (the title words'
                   vertical gradient; flat for most battle words) */
                v = p->gradient ? row_colour(p->rowcol, p->oy0, p->oy1, p->body, y, ny0, ny1) : p->body;
                if (p->box) {
                    m[i] = (uint8_t)v;
                    continue;
                }
                if ((p->bevel_tl || p->bevel_br) && din[i] <= rw * 3) {
                    /* the bevel: is the outside nearer above / left or below / right? */
                    int up = 0;
                    for (int s = 1; s <= rw + 1 && !up; s++)
                        up = (x - s >= 0 && din[i - s] == 0) || (y - s >= 0 && din[i - s * w] == 0);
                    v = up ? (p->bevel_tl ? p->bevel_tl : p->body) : (p->bevel_br ? p->bevel_br : p->body);
                }
            } else if (p->nring > 0 && !p->box && dout[i] <= p->nring * rw * 3) {
                int r = (dout[i] - 1) / (rw * 3); /* 0: next to the letter = the innermost ring */
                r     = r >= p->nring ? p->nring - 1 : r;
                v     = p->ring[p->nring - 1 - r];
            }
            m[i] = (uint8_t)v;
        }
    free(din);
    free(dout);
}

static void hifile_index(void* ctx, int k, int x0, int y0, int w, int h, uint8_t* idx)
{
    HiFile* f = ctx;
    int     W = w * k;
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < W; x++)
            idx[y * W + x] = (uint8_t)hifile_texel(f, x0 + x / k, y0 + y / k);
    for (int q = 0; q < f->nids; q++) {
        HiPic* p = &hipics[f->ids[q]];
        int    stride = p->cw * k, touch = 0, slot = -1;
        /* only pictures with a cell in this tile; their mask at this k from the cache (each was
           painted again for every tile whenever k changed: all pictures of the file, at k up to 16) */
        for (int i = 0; i < p->n && !touch; i++)
            touch = p->c[i].u < x0 + w && p->c[i].u + p->c[i].w > x0 && p->c[i].v < y0 + h && p->c[i].v + p->c[i].h > y0;
        if (!touch)
            continue;
        for (int j = 0; j < 2; j++)
            if (p->hmask[j] && p->hk[j] == k)
                slot = j;
        if (slot < 0) {
            size_t need = (size_t)stride * p->ch * k;
            if (hmask_bytes + need > HMASK_LIMIT) {
                for (int i = 0; i < nhipics; i++) /* over the budget: all made again when needed */
                    for (int j = 0; j < 2; j++) {
                        free(hipics[i].hmask[j]);
                        hipics[i].hmask[j]  = NULL;
                        hipics[i].hbytes[j] = 0;
                    }
                hmask_bytes = 0;
            }
            free(p->hmask[1]); /* the older size goes, the newer moves to slot 1 */
            hmask_bytes -= p->hbytes[1];
            p->hmask[1]  = p->hmask[0];
            p->hk[1]     = p->hk[0];
            p->hbytes[1] = p->hbytes[0];
            slot         = 0;
            p->hmask[0]  = malloc(need);
            p->hk[0]     = k;
            if (p->hmask[0] && !words_mask_pad(p->c, p->n, &p->w, k, p->hmask[0], stride, layer_pad(p, k))) {
                free(p->hmask[0]);
                p->hmask[0] = NULL;
            }
            if (p->hmask[0] && p->nring >= 0)
                layer_colour(p, k, p->hmask[0], stride, p->ch * k);
            p->hbytes[0] = p->hmask[0] ? need : 0;
            hmask_bytes += p->hbytes[0];
        }
        const uint8_t* hm = p->hmask[slot];
        if (!hm)
            continue;
        for (int i = 0; i < p->n; i++) {
            const Cell* c  = &p->c[i];
            int         u0 = c->u > x0 ? c->u : x0, u1 = c->u + c->w < x0 + w ? c->u + c->w : x0 + w;
            int         v0 = c->v > y0 ? c->v : y0, v1 = c->v + c->h < y0 + h ? c->v + c->h : y0 + h;
            for (int tv = v0 * k; tv < v1 * k; tv++)
                for (int tu = u0 * k; tu < u1 * k; tu++) {
                    int X = (c->x - p->x0) * k + tu - c->u * k, Y = (c->y - p->y0) * k + tv - c->v * k, v = 0;
                    int sh = p->w.tight ? 0 : k, m2 = p->w.tight ? 0 : 2;
                    int X1 = X / k, Y1 = Y / k, mx = X - sh, my = Y - sh; /* shift 1 texel right, 1 down */
                    if (!(p->w.border && (X1 == 0 || X1 == p->cw - 1 || Y1 == 0 || Y1 == p->ch - 1)) && mx >= 0 &&
                        mx < (p->cw - m2) * k && my >= 0 && my < (p->ch - m2) * k) {
                        if (p->nring >= 0) {
                            v = hm[my * stride + mx]; /* the palette index of its layer (layer_colour) */
                            if (p->box && v == 0)
                                continue; /* the box of the 1x picture stays */
                        }
                        else if (hm[my * stride + mx] >= 80)
                            v = p->col[i];
                    }
                    idx[(tv - y0 * k) * W + tu - x0 * k] = (uint8_t)v;
                }
        }
    }
}

static void register_hifile(const Open* o)
{
    Tex t;
    if (nhifiles == MAX_OPEN || !tex_open(&t, o->d, o->size))
        return;
    HiFile* f = &hifiles[nhifiles];
    f->nids   = 0;
    for (int i = 0; i < nhipics && f->nids < (int)(sizeof(f->ids) / sizeof(f->ids[0])); i++)
        if (hipics[i].file == o->d) {
            f->ids[f->nids++] = i;
            hipics[i].file_id = o->id;
        }
    if (f->nids == 0)
        return;
    f->pix = malloc((size_t)t.stride * t.h);
    if (!f->pix)
        return;
    memcpy(f->pix, o->d + t.img, (size_t)t.stride * t.h);
    f->stride = t.stride;
    f->w      = t.w;
    f->h      = t.h;
    f->bits   = t.bits;
    TexRep_AddIndexSource((const uint16_t*)f->pix, t.stride / 2, t.h, t.bits == 4 ? 0 : 1, 16, 16, hifile_index, f);
    nhifiles++;
}

static void flush_files(void)
{
    for (int i = 0; i < nopen; i++) {
        if (open_files[i].d != NULL && open_files[i].changed) {
            Host_PakSetLayer(open_files[i].id, open_files[i].d, open_files[i].size);
            register_hifile(&open_files[i]);
        }
        free(open_files[i].d);
    }
    nopen = 0;
    /* the recorded pictures pointed into these buffers: the files keep what they need */
    for (int i = 0; i < nhipics; i++)
        hipics[i].file = NULL;
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
            /* the original pixels come along (centred): the painter learns its colours from them. A
               box picture (the title menu: at least 95 % opaque) is stretched to the new width (its
               frame columns at both ends, a column of its fill between): the box stays a box */
            {
                int off = align > 0 ? W - c->w : (W - c->w) / 2, opaque = 0;
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < c->w; x++)
                        opaque += tget(t, c->u + x, c->v + y) != 0;
                int box = opaque * 100 >= c->w * H * 95 && c->w > 8;
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        int sx = x - off;
                        if (box)
                            sx = x < 3 ? x : x >= W - 3 ? c->w - (W - x) : sx >= 3 && sx < c->w - 3 ? sx : 3;
                        if (sx < 0 || sx >= c->w)
                            continue;
                        tset((Tex*)t, u + x, v + y, tget(t, c->u + sx, c->v + y));
                    }
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

/* dw2-pc-port: the scrolling pseudo code behind the title (file 0x269, layout 1, 25 cells). Each cell
 * holds one line or a few lines of the original text, cut at the cell edges; the same text, as the
 * cells hold it, in Exo 2 at full line height and up to 2.5 times narrower (the original face is very
 * narrow; Roboto Mono, fitted into the same cells, merged capitals such as WORK). The fragments narrower than 16 texels
 * (a bracket, "t);", "ge") stay as they are: a font fitted into 4 to 8 texels is a smear. Not
 * translated: it is program code. The logo (0xB5E..0xB62) is the trademark artwork and stays. */
static void restyle_title_code(void)
{
    static const struct {
        int         u, v, w, h;
        const char* text;
    } code[] = {
        { 0, 0, 128, 15, "static int AttackControl(WORK *p)" },
        { 0, 16, 124, 15, "int    _Attack,_Defense,_Target;" },
        { 0, 32, 104, 59, "=  GetDigiMonAttack(p);\n=  GetDigiMonDefense(p);\n=  FindTargetDigiMon(p);" },
        { 0, 92, 36, 35, "_Attack\n_Defense" },
        { 0, 128, 96, 15, "k(p,_Attack,_Defense,_Ta" },
        { 0, 144, 56, 15, "DigimonVersus" },
        { 0, 160, 80, 15, "//  an all-out attack" },
        { 0, 176, 96, 15, "(p,_Attack,_Defense,_Tar" },
        { 0, 192, 56, 35, "run_away(p);\nreturn(FALSE);" },
        { 36, 92, 64, 19, "switch(_Target){" },
        { 36, 112, 72, 15, "DigimonAllOutAttac" },
        { 56, 144, 32, 15, "_Target" },
        { 56, 192, 56, 15, "return(TRUE);" },
        { 80, 160, 36, 11, "case   0:" },
        { 88, 144, 36, 11, "case   1:" },
        { 96, 128, 32, 11, "default:" },
        { 104, 32, 16, 15, "rget" },
        { 104, 48, 24, 15, "break;" },
        { 104, 80, 24, 15, "break;" },
    };
    Open* f = get_file(0x269);
    Tex   t;
    int   done = 0, want = (int)(sizeof(code) / sizeof(code[0]));
    if (f == NULL || f->d == NULL || !tex_open(&t, f->d, f->size)) {
        printf("[fonts] title code: file 0x269 not readable\n");
        return;
    }
    for (int k = 0; k < want; k++) {
        uint32_t o = entry(f->d, f->size, 1);
        for (int q = 0; o != 0 && o + 16 <= (uint32_t)f->size && f->d[o] != 0xFF && q < 64; q++, o += 16) {
            Cell c = { f->d[o], f->d[o + 1], f->d[o + 8], f->d[o + 9] ? f->d[o + 9] : 256, rd16(f->d + o + 2), rd16(f->d + o + 4),
                       f->d[o + 6], f->d[o + 7], f->d[o + 10] };
            if (f->d[o + 12] == 0 && c.u == code[k].u && c.v == code[k].v && c.w == code[k].w && c.h == code[k].h) {
                Words w = { code[k].text, 0, -1, 0, 0, 0, 1, 0, 25 };
                if (paint1(&t, &c, 1, &w, 0.0)) {
                    done++;
                    f->changed = 1;
                }
                break;
            }
        }
    }
    printf("[fonts] title code: %d of %d\n", done, want);
}

/* Stats labels share their texture rectangles across several windows. Repaint
 * those exact rectangles, so every alias changes without touching bars/icons. */
static int german; /* defined below (PicWords_SetGerman) */
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
        {
            /* the original label's ink box: the new one takes no more room (the bars beside HP / MP
               are drawn over the rest of the cell) */
            int bx0 = c.w, bx1 = -1, by0 = c.h, by1 = -1;
            for (int y = 0; y < c.h; y++)
                for (int x = 0; x < c.w; x++)
                    if (tget(&t, c.u + x, c.v + y)) {
                        bx0 = x < bx0 ? x : bx0;
                        bx1 = x > bx1 ? x : bx1;
                        by0 = y < by0 ? y : by0;
                        by1 = y > by1 ? y : by1;
                    }
            if (bx1 >= bx0 + 2 && by1 >= by0 + 3) {
                c.u += bx0;
                c.v += by0;
                c.w = bx1 - bx0 + 1;
                c.h = by1 - by0 + 1;
            }
        }
        int     ink = picture_ink(&t, &c);
        uint8_t mask[16 * 11];
        /* Exo 2 Bold, the whole cell; drawn again at the output resolution (a 1x label of 7 pixels
           enlarged was unreadable at 4K) */
        /* German: the level label LV (the game's EL reads oddly; player's choice) */
        const char* text = german == 1 && strcmp(labels[k].text, "EL") == 0 ? "LV" : labels[k].text;
        if (Fonts_Mask(FONT_INTERFACE_BOLD, text, mask, c.w, c.w, c.h) < 0)
            continue;
        for (int y = 0; y < c.h; y++)
            for (int x = 0; x < c.w; x++)
                tset(&t, c.u + x, c.v + y, mask[y * c.w + x] >= 80 ? ink : 0);
        {
            Words   w   = { text, 0, 0, 0, 0, 0, 3, 0, 0, 1 };
            uint8_t col = (uint8_t)ink;
            c.x = c.y = 0;
            hipic_add(&t, &c, 1, &w, &col, NULL);
        }
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

static int  german = -1; /* declared above restyle_stats */
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

    /* the runtime pictures of an earlier call (language switch): Fonts_ApplyGame cleared their
       sources; tiles already made keep their own pixels */
    for (int i = 0; i < nhipics; i++)
        for (int j = 0; j < 2; j++) {
            free(hipics[i].hmask[j]);
            hipics[i].hmask[j] = NULL;
            hipics[i].hk[j]    = 0;
            hipics[i].hbytes[j] = 0;
        }
    hmask_bytes = 0;
    nhipics = 0;
    for (int i = 0; i < nhifiles; i++)
        free(hifiles[i].pix);
    nhifiles = 0;
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
        "Missed", "Attack", "Counter", "Interrupt", "Assist", "Guard", "Do you want to interrupt?", "Yes", "No",
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
    restyle_title_code();
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
