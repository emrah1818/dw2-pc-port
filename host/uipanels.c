/* dw2-pc-port: Modern UI panels. The game builds its windows from pieces of UI texture sheets
 * (window frames, battle status panels, the result boxes, the talk box). In Modern UI those
 * sheets get the port's own panel style, drawn by the renderer at the output resolution
 * (backend/texrep.c style_panels): the outline of each panel is smoothed from its 1x shape, the
 * edge is a cyan line over a dark blue gradient (the general design direction of the
 * "partner status" reference: blue / cyan accents, clear frames, tidy panels). Text, bars and
 * icons on the panels stay as the game draws them; the game's layout, colours of text and the
 * order of drawing do not change. Classic UI keeps the PS1 sheets.
 *
 * A panel is found in the 1x sheet, not listed by hand: a connected piece of opaque texels at
 * least 24 x 10 texels, at least 60 % of its box opaque, its one or two (stippled) most common
 * colours at least 40 % of it (its fill). The fill, and the ring within 2 texels of the transparent outside (the
 * old bevel), take the new style; the rest of the piece is detail. Open ends of a piece (it
 * continues in another piece on screen) touch the sheet's or the piece's opaque neighbours, not
 * transparency, so they get no edge: a window made of several pieces has no seams. */
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

/* the UI sheets with window and panel pieces (battle status and commands, battle result, talk box) */
static const int sheet_ids[] = { 0x13B, 0x25C, 0x315 };
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

static void classify(Sheet* s)
{
    int  W = s->ww * (s->bits == 4 ? 4 : 2), H = s->h, n = W * H;
    int* lab = malloc(sizeof(int) * n);
    int* stack = malloc(sizeof(int) * n);
    s->cls = calloc(n, 1);
    s->cid = calloc(n, sizeof(uint16_t));
    s->comp = calloc(2 * 512, sizeof(uint16_t));
    s->ncomp = 0;
    if (!lab || !stack || !s->cls || !s->cid || !s->comp) {
        free(lab);
        free(stack);
        return;
    }
    for (int i = 0; i < n; i++)
        lab[i] = -1;
    int ncomp = 0;
    for (int start = 0; start < n; start++) {
        if (lab[start] >= 0 || texel(s, start % W, start / W) == 0)
            continue;
        /* the piece: 4-connected opaque texels */
        int sp = 0, area = 0, x0 = W, x1 = -1, y0 = H, y1 = -1, hist[256] = { 0 };
        stack[sp++] = start;
        lab[start]  = ncomp;
        while (sp) {
            int i = stack[--sp], x = i % W, y = i / W;
            area++;
            hist[texel(s, x, y)]++;
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
            y0 = y < y0 ? y : y0;
            y1 = y > y1 ? y : y1;
            static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) {
                int X = x + dx[d], Y = y + dy[d];
                if (X < 0 || Y < 0 || X >= W || Y >= H)
                    continue;
                int j = Y * W + X;
                if (lab[j] < 0 && texel(s, X, Y) != 0) {
                    lab[j]      = ncomp;
                    stack[sp++] = j;
                }
            }
        }
        int bw = x1 - x0 + 1, bh = y1 - y0 + 1, first = 1, second = 0;
        for (int v = 1; v < 256; v++)
            if (hist[v] > hist[first])
                first = v;
        for (int v = 1; v < 256; v++)
            if (v != first && hist[v] > hist[second])
                second = v;
        int fill2 = second && hist[second] * 5 >= hist[first];
        int panel = bw >= 24 && bh >= 10 && area >= 200 && area * 10 >= bw * bh * 6 &&
                    (hist[first] + (fill2 ? hist[second] : 0)) * 10 >= area * 4 && s->ncomp < 512;
        if (panel) {
            int pc = s->ncomp++;
            s->comp[2 * pc]     = (uint16_t)y0;
            s->comp[2 * pc + 1] = (uint16_t)y1;
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int i = y * W + x;
                    if (lab[i] != ncomp)
                        continue;
                    int v = texel(s, x, y), ring = 0;
                    for (int dy = -2; dy <= 2 && !ring; dy++)
                        for (int dx = -2; dx <= 2 && !ring; dx++) {
                            int X = x + dx, Y = y + dy;
                            ring = X >= 0 && Y >= 0 && X < W && Y < H && texel(s, X, Y) == 0;
                        }
                    s->cls[i] = (uint8_t)(v == first || (fill2 && v == second) || ring ? 1 : 2);
                    s->cid[i] = (uint16_t)pc;
                }
        }
        ncomp++;
    }
    free(lab);
    free(stack);
}

void UiPanels_Apply(int modern)
{
    /* in development: on with DW2_MODERN_PANELS=1 until it is checked on all covered screens */
    const char* env = getenv("DW2_MODERN_PANELS");
    modern = modern && env != NULL && env[0] == '1';
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
            classify(s);
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
