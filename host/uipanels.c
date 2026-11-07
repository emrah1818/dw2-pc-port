/* dw2-pc-port: Modern UI panels. The game builds its windows from pieces of UI texture sheets
 * (window frames, battle status panels, the result boxes, the talk box). In Modern UI those
 * sheets get the port's own panel style, drawn by the renderer at the output resolution
 * (backend/texrep.c style_panels): the outline of each panel is smoothed from its 1x shape, the
 * edge is a cyan line over a dark blue gradient (the general design direction of the
 * "partner status" reference: blue / cyan accents, clear frames, tidy panels). Text, bars and
 * icons on the panels stay as the game draws them; the game's layout, colours of text and the
 * order of drawing do not change. Classic UI keeps the PS1 sheets.
 *
 * A panel piece is found in the 1x sheet, not listed by hand (classify below). Its fill takes the
 * new style; on its detail the renderer turns grey frame lines and labels into cyan lines and the
 * old blue shading into fill, and keeps bars, value fields and icons. The edge is drawn where a
 * piece meets transparency; where two panel pieces meet (one window made of several pieces) there
 * is none, so windows have no seams. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/texrep.h"
#include "host/picturetable.h"

extern void* Host_PakReadFileLayered(int id, int* size);

typedef struct {
    int       id;
    uint16_t* words; /* the sheet's pixels as uploaded */
    int       ww, h, bits;
    uint8_t*  cls;
    uint16_t* cid;
    uint16_t* comp;
    int       ncomp;
} Sheet;

/* the UI sheets with window and panel pieces: battle status and commands (0x13B), battle result
   (0x25C), talk box (0x315), menu windows (0x457, 0x6B5, 0xE0A), area windows (0x3D9). Not the
   backgrounds of the same screens (0x515 grid, 0xCD8 circuit lines): they are no panels. */
static const int sheet_ids[] = { 0x13B, 0x25C, 0x315, 0x457, 0x6B5, 0xE0A, 0x3D9 };
#define NSHEETS (int)(sizeof(sheet_ids) / sizeof(sheet_ids[0]))
static Sheet sheets[NSHEETS];
static int   panels_total;

static int texel(const Sheet* s, int x, int y)
{
    int W = s->ww * (s->bits == 4 ? 4 : 2);
    if (x < 0 || y < 0 || x >= W || y >= s->h)
        return 0;
    const uint8_t* b = (const uint8_t*)s->words;
    if (s->bits == 8)
        return b[y * s->ww * 2 + x];
    return (b[y * s->ww * 2 + x / 2] >> ((x & 1) * 4)) & 15;
}

/* the 1x texels k times larger (a sheet without a drawing of its own) */
static void plain_index(void* ctx, int k, int x0, int y0, int w, int h, uint8_t* idx)
{
    const Sheet* s = ctx;
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < w * k; x++)
            idx[y * w * k + x] = (uint8_t)texel(s, x0 + x / k, y0 + y / k);
}

/* The pieces are the sheet's cell rectangles (the part layouts of the file): the sheets are
 * packed, pieces touch each other without a transparent gap. A piece is a panel piece when it has
 * a stippled fill (the most common pair of horizontally alternating colours takes at least an
 * eighth of its neighbouring texel pairs: those two colours are its fill) or a flat one (one colour
 * covers at least half of it). The rest of the piece is detail. */
static void classify(Sheet* s, const uint8_t* d, int size)
{
    int W = s->ww * (s->bits == 4 ? 4 : 2), H = s->h, n = W * H, count = PictureTable_Count(d, size);
    s->cls  = calloc(n, 1);
    s->cid  = calloc(n, sizeof(uint16_t));
    s->comp = calloc(2 * 1024, sizeof(uint16_t));
    s->ncomp = 0;
    if (!s->cls || !s->cid || !s->comp)
        return;
    static int rects[4096][4];
    int        nrect = 0, pairs_a[16], pairs_b[16], npairs = 0;
    for (int e = 1; e < count; e++) {
        uint32_t o = PictureTable_Entry(d, size, e);
        for (int k = 0; o != 0 && o + 16 <= (uint32_t)size && d[o] != 0xFF && k < 64; k++, o += 16) {
            int u = d[o], v = d[o + 1], w = d[o + 8], h = d[o + 9] ? d[o + 9] : 256, dup = 0;
            if (w < 4 || h < 4 || u + w > W || v + h > H)
                continue;
            for (int i = 0; i < nrect && !dup; i++)
                dup = rects[i][0] == u && rects[i][1] == v && rects[i][2] == w && rects[i][3] == h;
            if (!dup && nrect < 4096) {
                rects[nrect][0] = u;
                rects[nrect][1] = v;
                rects[nrect][2] = w;
                rects[nrect][3] = h;
                nrect++;
            }
        }
    }
    for (int r = 0; r < nrect && s->ncomp < 1024; r++) {
        int u = rects[r][0], v = rects[r][1], w = rects[r][2], h = rects[r][3], pairs = 0, best = 0, ba = 0, bb = 0;
        static int pc[256][256];
        if (w * h < 64)
            continue;
        memset(pc, 0, sizeof(pc));
        for (int y = v; y < v + h; y++)
            for (int x = u; x + 1 < u + w; x++) {
                int a = texel(s, x, y), b = texel(s, x + 1, y);
                if (a == 0 || b == 0)
                    continue;
                pairs++;
                if (a != b) {
                    int lo = a < b ? a : b, hi = a < b ? b : a;
                    if (++pc[lo][hi] > best) {
                        best = pc[lo][hi];
                        ba   = lo;
                        bb   = hi;
                    }
                }
            }
        int hist[256] = { 0 }, opaque = 0, dom = 0;
        for (int y = v; y < v + h; y++)
            for (int x = u; x < u + w; x++) {
                int t = texel(s, x, y);
                if (t) {
                    hist[t]++;
                    opaque++;
                }
            }
        for (int t = 1; t < 256; t++)
            if (hist[t] > hist[dom])
                dom = t;
        if (pairs >= 32 && best * 8 >= pairs) {
            /* stippled: the pair */
        } else if (opaque >= 64 && dom && hist[dom] * 2 >= opaque) {
            ba = bb = dom; /* flat: the dominant colour */
        } else {
            continue;
        }
        if (npairs < 16) { /* a stippled or flat fill: also searched outside the cells below (a flat one
                              as the pair of its colour with itself: a texel of it next to another) */
            int dup = 0;
            for (int q = 0; q < npairs; q++)
                dup |= pairs_a[q] == ba && pairs_b[q] == bb;
            if (dup)
                goto known;
            pairs_a[npairs] = ba;
            pairs_b[npairs] = bb;
            npairs++;
        }
    known:;
        int id = s->ncomp++;
        s->comp[2 * id]     = (uint16_t)v;
        s->comp[2 * id + 1] = (uint16_t)(v + h - 1);
        for (int y = v; y < v + h; y++)
            for (int x = u; x < u + w; x++) {
                int t = texel(s, x, y), i = y * W + x;
                if (t == 0 || s->cls[i] == 1)
                    continue;
                s->cls[i] = (uint8_t)(t == ba || t == bb ? 1 : 2);
                s->cid[i] = (uint16_t)id;
            }
    }
    /* Pieces whose layouts are in other files (the result's per-Digimon boxes are drawn from
       0x25C by layouts elsewhere): a texel in one of the stippled fills found above, next to its
       partner colour, is fill too; the opaque texels within 2 of it are detail on that panel. */
    if (npairs > 0 && s->ncomp < 1024) {
        int id = s->ncomp++;
        s->comp[2 * id]     = 0;
        s->comp[2 * id + 1] = 0; /* no gradient span: the middle tone */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x, t = texel(s, x, y), hit = 0;
                if (s->cls[i] != 0 || t == 0)
                    continue;
                for (int q = 0; q < npairs && !hit; q++) {
                    int partner = t == pairs_a[q] ? pairs_b[q] : t == pairs_b[q] ? pairs_a[q] : -1;
                    if (partner < 0)
                        continue;
                    hit = texel(s, x + 1, y) == partner || texel(s, x - 1, y) == partner || texel(s, x, y + 1) == partner ||
                          texel(s, x, y - 1) == partner;
                }
                if (hit) {
                    s->cls[i] = 3; /* marked; turned into 1 below */
                    s->cid[i] = (uint16_t)id;
                }
            }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (s->cls[i] != 0 || texel(s, x, y) == 0)
                    continue;
                for (int dy = -2; dy <= 2 && s->cls[i] == 0; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int X = x + dx, Y = y + dy;
                        if (X >= 0 && Y >= 0 && X < W && Y < H && s->cls[Y * W + X] == 3) {
                            s->cls[i] = 4; /* detail of such a panel */
                            s->cid[i] = (uint16_t)id;
                            break;
                        }
                    }
            }
        for (int i = 0; i < n; i++)
            s->cls[i] = s->cls[i] == 3 ? 1 : s->cls[i] == 4 ? 2 : s->cls[i];
    }
    /* the word pictures on the sheet (ZIEL, Schaden, HP / MP labels) keep their own colours */
    {
        extern int PicWords_Rects(int file, int* r, int max);
        static int r[4 * 512];
        int        nr = PicWords_Rects(s->id, r, 512);
        for (int q = 0; q < nr; q++)
            for (int y = r[4 * q + 1]; y < r[4 * q + 1] + r[4 * q + 3] && y < H; y++)
                for (int x = r[4 * q]; x < r[4 * q] + r[4 * q + 2] && x < W; x++)
                    s->cls[y * W + x] = 0;
    }
}

void UiPanels_Apply(int modern)
{
    /* on in Modern UI (checked: field menu, status, Digi-Line, load / save, battle HUD and commands,
       result); DW2_MODERN_PANELS=0 keeps the game's own window sheets */
    const char* env = getenv("DW2_MODERN_PANELS");
    modern = modern && !(env != NULL && env[0] == '0');
    panels_total = 0;
    for (int i = 0; i < NSHEETS; i++) {
        Sheet* s = &sheets[i];
        /* the previous call's sheet: its source may still be registered (Classic in German keeps
           the runtime fonts): detach the style, keep its buffers (a few KB per language or style
           switch; a registered drawing may still read them) */
        if (s->words)
            TexRep_SetSourceStyle(s->words, s->ww, s->h, NULL, NULL, NULL, 0);
        memset(s, 0, sizeof(*s));
        if (!modern)
            continue;
        int      size = 0;
        uint8_t* d    = Host_PakReadFileLayered(sheet_ids[i], &size);
        uint32_t o    = d ? PictureTable_Entry(d, size, 0) : 0;
        if (!d || o == 0 || o + 20 > (uint32_t)size || d[o] != 0x10) {
            free(d);
            continue;
        }
        uint32_t flags = d[o + 4], p = o + 8;
        if ((flags & 7) > 1) {
            free(d);
            continue;
        }
        if (flags & 8)
            p += (uint32_t)(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | (uint32_t)d[p + 3] << 24);
        if (p + 12 > (uint32_t)size) {
            free(d);
            continue;
        }
        int ww = d[p + 8] | d[p + 9] << 8, h = d[p + 10] | d[p + 11] << 8;
        if (ww <= 0 || h <= 0 || p + 12 + (uint32_t)(ww * h * 2) > (uint32_t)size) {
            free(d);
            continue;
        }
        s->id    = sheet_ids[i];
        s->ww    = ww;
        s->h     = h;
        s->bits  = (flags & 7) ? 8 : 4;
        s->words = malloc((size_t)ww * h * 2);
        if (s->words) {
            memcpy(s->words, d + p + 12, (size_t)ww * h * 2);
            classify(s, d, size);
            /* a sheet the word pictures did not register yet gets the plain 1x texels (a newer
               registration of the same pixels keeps its own drawing) */
            TexRep_AddIndexSourceIfNew(s->words, ww, h, s->bits == 4 ? 0 : 1, 16, 16, plain_index, s);
            TexRep_SetSourceStyle(s->words, ww, h, s->cls, s->cid, s->comp, s->ncomp);
            panels_total += s->ncomp;
        }
        free(d);
    }
    if (modern)
        printf("[ui] modern panels: %d in %d sheets\n", panels_total, NSHEETS);
}
