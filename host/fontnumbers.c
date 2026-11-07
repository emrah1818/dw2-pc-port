/* Numeric sprite frames (HP/MP, EXP, BITs, damage, shop prices, etc.) even when they bypass
 * text.c. Their cells, palettes and animation stay the game's.
 *
 * The original digits are a body in one palette index and a drop shadow one pixel right and down
 * in another (0x6B5: index 1 and 3). The modern digits keep both: Exo 2 digits (a plain zero; the
 * former Roboto Mono zero has a dot and read as 8 at this size), the body in the original body index,
 * the shadow in the original shadow index, so every number keeps its colours and palette effects.
 *
 *   - HD (scale >= 2): drawn at the output resolution by a runtime overlay on the sheet
 *     (backend/texrep.c TexRep_AddIndexOverlay); VRAM keeps the original cells (a 1x write there
 *     would mark the sheet dirty and the numbers would stay 1x pixels).
 *   - 1x: the cell in VRAM gets the 1x digit, as before, now with the shadow. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend/psxgpu.h"
#include "backend/texrep.h"
#include "host/fonts.h"
#include "host/picturetable.h"

extern void* Host_PakReadFileLayered(int id, int* size);

static int ids[512], nids;
static struct {
    int      used, page, u, v, w, h, digit, clut;
    uint16_t pixels[64 * 17];
} cache[128];
static unsigned cursor;

void Fonts_RegisterNumber(int id)
{
    if (!Fonts_Enabled())
        return;
    for (int i = 0; i < nids; i++)
        if (ids[i] == id)
            return;
    if (nids < 512)
        ids[nids++] = id;
}

/* ---- the sheets (one per texture file) and their digit cells ---- */
typedef struct {
    uint8_t u, v, w, h, digit, body, shadow;
} NumCell;
typedef struct {
    int       file, gen, bad;
    uint16_t* words;
    int       ww, h, bits;
    int       ncell;
    NumCell   cell[96];
} NumSheet;
static NumSheet sheets[16];
static int      nsheets;

static int sheet_texel(const NumSheet* s, int x, int y)
{
    int W = s->ww * (s->bits == 4 ? 4 : 2);
    if (x < 0 || y < 0 || x >= W || y >= s->h)
        return 0;
    const uint8_t* b = (const uint8_t*)s->words;
    return s->bits == 8 ? b[y * s->ww * 2 + x] : (b[y * s->ww * 2 + x / 2] >> ((x & 1) * 4)) & 15;
}

static NumSheet* sheet_of(int file)
{
    for (int i = 0; i < nsheets; i++)
        if (sheets[i].file == file)
            return sheets[i].bad ? NULL : &sheets[i];
    if (nsheets == 16)
        return NULL;
    NumSheet* s = &sheets[nsheets++];
    memset(s, 0, sizeof(*s));
    s->file = file;
    s->bad  = 1;
    int      size = 0;
    uint8_t* d    = Host_PakReadFileLayered(file, &size);
    uint32_t o    = d ? PictureTable_Entry(d, size, 0) : 0;
    if (!d || o == 0 || o + 20 > (uint32_t)size || d[o] != 0x10 || (d[o + 4] & 7) > 1) {
        free(d);
        return NULL;
    }
    uint32_t flags = d[o + 4], p = o + 8;
    if (flags & 8)
        p += (uint32_t)(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | (uint32_t)d[p + 3] << 24);
    if (p + 12 > (uint32_t)size) {
        free(d);
        return NULL;
    }
    int ww = d[p + 8] | d[p + 9] << 8, h = d[p + 10] | d[p + 11] << 8;
    if (ww <= 0 || h <= 0 || p + 12 + (uint32_t)(ww * h * 2) > (uint32_t)size || !(s->words = malloc((size_t)ww * h * 2))) {
        free(d);
        return NULL;
    }
    memcpy(s->words, d + p + 12, (size_t)ww * h * 2);
    s->ww   = ww;
    s->h    = h;
    s->bits = (flags & 7) ? 8 : 4;
    s->bad  = 0;
    free(d);
    return s;
}

/* the body (most texels) and shadow (next) index of an original digit cell */
static void cell_style(const NumSheet* s, NumCell* c)
{
    int hist[256] = { 0 }, b = 0, sh = 0;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            hist[sheet_texel(s, c->u + x, c->v + y)]++;
    for (int i = 1; i < 256; i++)
        if (hist[i] > hist[b] || !b)
            b = hist[i] ? i : b;
    for (int i = 1; i < 256; i++)
        if (i != b && hist[i] && (!sh || hist[i] > hist[sh]))
            sh = i;
    c->body   = (uint8_t)(b ? b : 1);
    c->shadow = (uint8_t)sh; /* 0: none */
}

/* the original ink box of a cell (the digit's height, its top row) */
static void ink_box(const NumSheet* s, const NumCell* c, int* top, int* rows)
{
    int t = -1, b = -1;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            if (sheet_texel(s, c->u + x, c->v + y)) {
                if (t < 0)
                    t = y;
                b = y;
            }
    *top  = t < 0 ? 0 : t;
    *rows = t < 0 ? c->h : b - t + 1;
}

/* the digit of cell c at k: coverage of the body (glyph rows = the original body height) */
static int digit_mask(const NumSheet* s, const NumCell* c, int k, uint8_t* cov, int* oy)
{
    int top, rows;
    ink_box(s, c, &top, &rows);
    int bodyrows = c->shadow ? rows - 1 : rows, W = c->w * k, H = c->h * k;
    if (bodyrows < 3)
        return 0;
    memset(cov, 0, (size_t)W * H);
    int      gw = (c->w - (c->shadow ? 1 : 0)) * k, gh = bodyrows * k;
    uint8_t* tmp = malloc((size_t)gw * gh);
    if (!tmp || !Fonts_Digit(c->digit, tmp, gw - k / 2 > 0 ? gw - k / 2 : gw, gh)) {
        free(tmp);
        return 0;
    }
    int dw = gw - k / 2 > 0 ? gw - k / 2 : gw;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < dw; x++)
            cov[(top * k + y) * W + x] = tmp[y * dw + x];
    free(tmp);
    *oy = top;
    return 1;
}

static void num_overlay(void* ctx, int k, int x0, int y0, int w, int h, uint8_t* idx)
{
    NumSheet* s = ctx;
    int       W = w * k;
    for (int i = 0; i < s->ncell; i++) {
        const NumCell* c = &s->cell[i];
        if (c->u >= x0 + w || c->u + c->w <= x0 || c->v >= y0 + h || c->v + c->h <= y0)
            continue;
        int      CW = c->w * k, CH = c->h * k, oy;
        uint8_t* cov = malloc((size_t)CW * CH);
        if (!cov || !digit_mask(s, c, k, cov, &oy)) {
            free(cov);
            continue;
        }
        int sh = (3 * k + 2) / 4; /* the shadow: 0.75 texel (the PS1's one pixel, a little finer) */
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++) {
                int X = (c->u - x0) * k + x, Y = (c->v - y0) * k + y, v = 0;
                if (X < 0 || Y < 0 || X >= W || Y >= h * k)
                    continue;
                if (cov[y * CW + x] >= 80)
                    v = c->body;
                else if (c->shadow && x >= sh && y >= sh && cov[(y - sh) * CW + x - sh] >= 80)
                    v = c->shadow;
                idx[Y * W + X] = (uint8_t)v;
            }
        free(cov);
    }
}

/* the cell at HD: noted on its sheet; 1 = drawn at the output resolution (no VRAM write) */
static int note_hd(int id, const uint8_t* e, int vx, int vy)
{
    NumSheet* s = sheet_of(id >> 16);
    if (!s)
        return 0;
    int u = e[0], v = e[1], w = e[8], h = e[9], d = e[12], i;
    for (i = 0; i < s->ncell; i++)
        if (s->cell[i].u == u && s->cell[i].v == v && s->cell[i].w == w && s->cell[i].h == h)
            break;
    if (i == s->ncell) {
        if (s->ncell == 96)
            return 0;
        NumCell* c = &s->cell[s->ncell++];
        c->u = (uint8_t)u;
        c->v = (uint8_t)v;
        c->w = (uint8_t)w;
        c->h = (uint8_t)h;
        c->digit = (uint8_t)d;
        cell_style(s, c);
        s->gen = 0; /* the overlay draws one more cell: its tiles again */
    }
    if (s->gen != TexRep_SourceGeneration()) {
        TexRep_AddIndexOverlay(s->words, s->ww, s->h, s->bits == 4 ? 0 : 1, num_overlay, s);
        TexRep_NoteUploadAt(s->words, s->ww, s->h, vx, vy);
        s->gen = TexRep_SourceGeneration();
    }
    return 1;
}

void Fonts_NumberCell(int id, const void* cell, int tpage, int uoffset, int clut)
{
    const uint8_t* c     = cell;
    int            known = 0;
    for (int i = 0; i < nids; i++)
        if (ids[i] == id) {
            known = 1;
            break;
        }
    if (!known || !c || c[12] > 9)
        return;
    int u = c[0] + uoffset, v = c[1], w = c[8], h = c[9], digit = c[12], bits = (tpage >> 7) & 3;
    if (w < 2 || w > 32 || h < 2 || h > 64 || bits > 1)
        return;
    int      per = bits ? 2 : 4, mask = bits ? 255 : 15;
    int      bx = (tpage & 15) * 64, by = ((tpage & 16) << 4) | ((tpage & 0x800) >> 2);
    if (TexRep_RuntimeScale() >= 2 && note_hd(id, c, bx + uoffset / per, by))
        return;
    int      first = u / per, words = ((u + w + per - 1) / per) - first;
    uint16_t pixels[64 * 17];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < words; x++)
            pixels[y * words + x] = PsxGpu_Vram[(by + v + y) & 511][(bx + first + x) & 1023];
    for (unsigned i = 0; i < 128; i++) {
        if (cache[i].used && cache[i].page == tpage && cache[i].u == u && cache[i].v == v &&
            cache[i].w == w && cache[i].h == h && cache[i].digit == digit && cache[i].clut == clut &&
            !memcmp(cache[i].pixels, pixels, (size_t)words * h * 2))
            return;
    }
    /* 1x: body and shadow index of the original cell (most texels, next); the digit in the body
       index, its shadow one pixel right and down. Never overwrite palette or adjacent pixels. */
    int used[256] = { 0 }, body = 0, shadow = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int px = u % per + x, k = (pixels[y * words + px / per] >> ((px % per) * (bits ? 8 : 4))) & mask;
            used[k]++;
        }
    for (int k = 1; k <= mask; k++)
        if (used[k] && (!body || used[k] > used[body]))
            body = k;
    for (int k = 1; k <= mask; k++)
        if (k != body && used[k] && (!shadow || used[k] > used[shadow]))
            shadow = k;
    if (!body)
        return;
    int top = -1, bot = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int px = u % per + x, k = (pixels[y * words + px / per] >> ((px % per) * (bits ? 8 : 4))) & mask;
            if (k) {
                if (top < 0)
                    top = y;
                bot = y;
            }
        }
    int     rows = (bot - top + 1) - (shadow ? 1 : 0), gw = w - (shadow ? 1 : 0);
    uint8_t alpha[32 * 64];
    int     ox = 0;
    if (rows < 3 || !Fonts_Digit(digit, alpha, gw, rows))
        return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int gx = x - ox, gy = y - top, k = 0;
            if (gx >= 0 && gx < gw && gy >= 0 && gy < rows && alpha[gy * gw + gx] >= 96)
                k = body;
            else if (shadow && gx >= 1 && gx - 1 < gw && gy >= 1 && gy - 1 < rows && alpha[(gy - 1) * gw + gx - 1] >= 96)
                k = shadow;
            int       px = u % per + x, shift = (px % per) * (bits ? 8 : 4);
            uint16_t* p = &pixels[y * words + px / per];
            *p          = (uint16_t)((*p & ~(mask << shift)) | (k << shift));
        }
    PsxGpu_LoadImage(bx + first, by + v, words, h, pixels);
    unsigned i     = cursor++ % 128;
    cache[i].used  = 1;
    cache[i].page  = tpage;
    cache[i].u     = u;
    cache[i].v     = v;
    cache[i].w     = w;
    cache[i].h     = h;
    cache[i].digit = digit;
    cache[i].clut  = clut;
    memcpy(cache[i].pixels, pixels, (size_t)words * h * 2);
}
