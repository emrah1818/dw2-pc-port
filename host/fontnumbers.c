/* Replace numeric sprite frames (HP/MP, EXP, damage, shop prices, etc.) even
 * when they bypass text.c. Keep their original cells, palettes and animation. */
#include <stdint.h>
#include <string.h>
#include "host/fonts.h"
#include "backend/psxgpu.h"

static int ids[512], nids;
static struct {
    int      used, page, u, v, w, h, digit, clut;
    uint16_t pixels[64 * 17];
} cache[128];
static unsigned cursor;

void Fonts_RegisterNumber(int id)
{
    for (int i = 0; i < nids; i++)
        if (ids[i] == id)
            return;
    if (nids < 512)
        ids[nids++] = id;
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
    /* The brightest used palette colour is the ink, independent of the
     * game's palette index order. Never overwrite palette or adjacent pixels. */
    int used[256] = { 0 }, ink = 0, brightness = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int px = u % per + x, k = (pixels[y * words + px / per] >> ((px % per) * (bits ? 8 : 4))) & mask;
            used[k]++;
        }
    for (int k = 1; k <= mask; k++)
        if (used[k]) {
            int color = PsxGpu_Vram[(clut >> 6) & 511][(((clut & 63) * 16) + k) & 1023];
            int b     = 3 * (color & 31) + 6 * ((color >> 5) & 31) + ((color >> 10) & 31);
            if (b > brightness) {
                brightness = b;
                ink        = k;
            }
        }
    if (!ink)
        return;
    uint8_t alpha[32 * 64];
    if (!Fonts_Cell(FONT_TELEMETRY, '0' + digit, alpha, w, h))
        return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int       px = u % per + x, shift = (px % per) * (bits ? 8 : 4), k = alpha[y * w + x] >= 80 ? ink : 0;
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
