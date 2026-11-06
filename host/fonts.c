/* Modern typefaces, independent of the language/text layer. Static instances of
 * Exo 2 Medium, Orbitron Bold and Roboto Mono Medium (SIL OFL, misc/fonts/).
 * stb_truetype is already vendored by Dear ImGui; keep this instance private. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/fonts.h"
#include "include/include_asm.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "host/imgui/imstb_truetype.h"

extern const unsigned char Dw2Interface[], Dw2Display[], Dw2Telemetry[];
INCLUDE_BIN(Dw2Interface, "misc/fonts/DW2Interface.ttf");
INCLUDE_BIN(Dw2Display, "misc/fonts/DW2Display.ttf");
INCLUDE_BIN(Dw2Telemetry, "misc/fonts/DW2Telemetry.ttf");
/* Generated from the committed binaries by tools/fonts/build_fonts.py. */
#include "misc/fonts/sizes.h"
static stbtt_fontinfo faces[FONT_COUNT];
static int            ready;

const unsigned char* Fonts_Data(int role, int* size)
{
    static const unsigned char* const data[]  = { Dw2Interface, Dw2Display, Dw2Telemetry };
    static const int                  sizes[] = { DW2_INTERFACE_SIZE, DW2_DISPLAY_SIZE, DW2_TELEMETRY_SIZE };
    if (role < 0 || role >= FONT_COUNT)
        return NULL;
    if (size)
        *size = sizes[role];
    return data[role];
}

int Fonts_Init(void)
{
    if (ready)
        return ready > 0;
    ready = -1;
    for (int i = 0; i < FONT_COUNT; i++) {
        const unsigned char* data   = Fonts_Data(i, NULL);
        int                  offset = stbtt_GetFontOffsetForIndex(data, 0);
        if (offset < 0 || !stbtt_InitFont(&faces[i], data, offset))
            return 0;
    }
    ready = 1;
    return 1;
}

static int next_cp(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    int                  c = *s++;
    if (c >= 0xC2 && c <= 0xDF && (*s & 0xC0) == 0x80) {
        c = ((c & 31) << 6) | (*s++ & 63);
    } else if (c >= 0xE0 && c <= 0xEF && s[0] && s[1] &&
               (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c  = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
        s += 2;
        if (c < 0x800 || (c >= 0xD800 && c <= 0xDFFF))
            return -1;
    } else if (c >= 0x80)
        return -1;
    *p = (const char*)s;
    return c;
}

/* Fit using actual outline bounds including accents, descenders, bearings and
 * kerning. All glyphs use the same scale and baseline (also in the game atlas). */
int Fonts_Mask(int role, const char* text, uint8_t* dst, int stride, int width, int height)
{
    int cps[512], n = 0, ascent, descent, gap;
    if (!Fonts_Init() || role < 0 || role >= FONT_COUNT || !text || !dst ||
        width < 1 || height < 1 || width > 1024 || height > 256 || stride < width)
        return -1;
    stbtt_fontinfo* f = &faces[role];
    for (const char* p = text; *p;) {
        int cp = next_cp(&p);
        if (cp < 0 || n == 512)
            return -1;
        if (!stbtt_FindGlyphIndex(f, cp))
            f = &faces[FONT_INTERFACE];
        if (!stbtt_FindGlyphIndex(f, cp))
            return -1;
        cps[n++] = cp;
    }
    stbtt_GetFontVMetrics(f, &ascent, &descent, &gap);
    float scale = (float)height / (ascent - descent);
    for (int attempt = 0; attempt < 160; attempt++, scale *= 0.97f) {
        float pen  = 0;
        int   left = 0, right = 0, top = 0, bottom = 0;
        for (int i = 0; i < n; i++) {
            int x0, y0, x1, y1, advance, bearing;
            stbtt_GetCodepointBitmapBoxSubpixel(f, cps[i], scale, scale, pen - floorf(pen), 0, &x0, &y0, &x1, &y1);
            int x = (int)floorf(pen);
            if (x0 + x < left)
                left = x0 + x;
            if (x1 + x > right)
                right = x1 + x;
            if (y0 < top)
                top = y0;
            if (y1 > bottom)
                bottom = y1;
            stbtt_GetCodepointHMetrics(f, cps[i], &advance, &bearing);
            pen += advance * scale;
            if (i + 1 < n)
                pen += stbtt_GetCodepointKernAdvance(f, cps[i], cps[i + 1]) * scale;
        }
        if (right - left > width || bottom - top > height)
            continue;
        for (int y = 0; y < height; y++)
            memset(dst + y * stride, 0, (size_t)width);
        pen          = 0;
        int baseline = (height - (bottom - top)) / 2 - top;
        for (int i = 0; i < n; i++) {
            int   x0, y0, x1, y1, advance, bearing;
            float frac = pen - floorf(pen);
            stbtt_GetCodepointBitmapBoxSubpixel(f, cps[i], scale, scale, frac, 0, &x0, &y0, &x1, &y1);
            if (x1 > x0 && y1 > y0) {
                uint8_t* bitmap = malloc((size_t)(x1 - x0) * (y1 - y0));
                if (!bitmap)
                    return -1;
                stbtt_MakeCodepointBitmapSubpixel(f, bitmap, x1 - x0, y1 - y0, x1 - x0, scale, scale, frac, 0, cps[i]);
                int x = (int)floorf(pen) + x0 - left, y = baseline + y0;
                for (int yy = 0; yy < y1 - y0; yy++)
                    for (int xx = 0; xx < x1 - x0; xx++) {
                        uint8_t  v = bitmap[yy * (x1 - x0) + xx];
                        uint8_t* d = dst + (y + yy) * stride + x + xx;
                        if (v > *d)
                            *d = v;
                    }
                free(bitmap);
            }
            stbtt_GetCodepointHMetrics(f, cps[i], &advance, &bearing);
            pen += advance * scale;
            if (i + 1 < n)
                pen += stbtt_GetCodepointKernAdvance(f, cps[i], cps[i + 1]) * scale;
        }
        return right - left;
    }
    return -1;
}

int Fonts_GameCodepoint(int c)
{
    static const int punct[]   = { 0, 0, 0, 0, '&', 0, '?', '!', '/', 0, 0xB7, '-', 0, 0, 0, 0, 0, 0, 0, 0, 0, '>',
                                   ',', '.', '\'', '"', ';', ':', '%', '+', '=', '#' };
    static const int umlauts[] = { 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0xDF };
    if (c >= 0 && c < 10)
        return '0' + c;
    if (c >= 10 && c < 0x24)
        return 'A' + c - 10;
    if (c >= 0x24 && c < 0x3E)
        return 'a' + c - 0x24;
    if (c >= 0x3E && c <= 0x5D)
        return punct[c - 0x3E];
    if (c >= 0xE6 && c <= 0xEC)
        return umlauts[c - 0xE6];
    return 0; /* controller icons and control codes are not letters */
}

int Fonts_PatchGameFont(uint8_t* data, int size, int big)
{
    const int img = 68, cw = big ? 9 : 7, ch = big ? 14 : 10, cols = big ? 13 : 17, gw = big ? 8 : 7, gh = big ? 13 : 9;
    int       count = 0;
    if (!data || size < img + 256 * 64 || data[4] != 0x10 || data[8] != 8 || !Fonts_Init())
        return 0;
    for (int c = 0; c <= 0xEC; c++) {
        int     cp = Fonts_GameCodepoint(c);
        uint8_t mask[13 * 8];
        if (c == 0x43 || (c >= 0x4A && c <= 0x4E)) {
            char label[3] = { 'L', (char)('1' + c - 0x4A), 0 };
            if (c == 0x43) {
                label[0] = 'I';
                label[1] = 'I';
            }
            if (Fonts_Mask(FONT_TELEMETRY, label, mask, gw, gw, gh) < 0)
                return 0;
        } else {
            if (!cp)
                continue;
            /* A shared sample fixes baseline/size for all individual atlas letters.
             * Each glyph is cropped from a uniformly scaled line by the helper below. */
            if (!Fonts_Cell(c < 10 ? FONT_TELEMETRY : FONT_INTERFACE, cp, mask, gw, gh))
                return 0;
        }
        int cc = big && c >= 0x88 ? c - 0x88 : c, x0 = (cc % cols) * cw, y0 = (cc / cols) * ch;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int v  = x < gw && y < gh && mask[y * gw + x] >= 80 ? 1 : 0;
                int gx = x0 + x, off = img + (y0 + y) * 64 + gx / 2, shift = (gx & 1) * 4;
                data[off] = (uint8_t)((data[off] & ~(15 << shift)) | (v << shift));
            }
        count++;
    }
    return count;
}

/* Uniform cell metrics: fit the entire supported alphabet, not each glyph's ink.
 * Digits are centred in a fixed-width cell. No individual vertical stretching. */
int Fonts_Cell(int role, int cp, uint8_t* out, int w, int h)
{
    if (!Fonts_Init() || role < 0 || role >= FONT_COUNT || !out || w < 1 || h < 1)
        return 0;
    stbtt_fontinfo* f = &faces[role];
    if (!stbtt_FindGlyphIndex(f, cp))
        f = &faces[FONT_INTERFACE];
    if (!stbtt_FindGlyphIndex(f, cp))
        return 0;
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(f, &ascent, &descent, &gap);
    float scale = (float)h / (ascent - descent);
    for (;;) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(f, 'W', scale, scale, &x0, &y0, &x1, &y1);
        if (x1 - x0 <= w || scale < 0.0001f)
            break;
        scale *= 0.97f;
    }
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(f, cp, scale, scale, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    if (bw > w || bh > h)
        return 0;
    memset(out, 0, (size_t)w * h);
    if (bw && bh) {
        int y = (int)floorf(ascent * scale) + y0;
        if (y < 0)
            y = 0;
        if (y + bh > h)
            y = h - bh;
        stbtt_MakeCodepointBitmap(f, out + y * w + (w - bw) / 2, bw, bh, w, scale, scale, cp);
    }
    return 1;
}

extern void* Host_PakReadFileLayered(int id, int* size);
/* The development scene packs 64 ASCII cells as eight one-bit scanlines each.
 * Build a private replacement; never modify the verified disc table. */
static int fonts_on = 1;
void Fonts_SetEnabled(int on) { fonts_on = on; }
int  Fonts_Enabled(void) { return fonts_on; }

int Fonts_DevBitmap(uint8_t* out, int size)
{
    if (!fonts_on)
        return 0;
    uint8_t bitmap[512], alpha[64];
    if (!out || size < 512)
        return 0;
    memset(bitmap, 0, sizeof(bitmap));
    for (int c = 0; c < 64; c++) {
        if (!Fonts_Cell(FONT_TELEMETRY, c + 32, alpha, 8, 8))
            return 0;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (alpha[y * 8 + x] >= 80)
                    bitmap[c * 8 + y] |= (uint8_t)(128 >> x);
    }
    memcpy(out, bitmap, sizeof(bitmap));
    return 1;
}

extern void Host_PakSetLayer(int id, const uint8_t* data, int size);
extern void Lang_RestyleWords(uint8_t* data, int size);
void        Fonts_ApplyGame(void)
{
    for (int big = 0; big < 2; big++) {
        int      size = 0, id = big ? 0x110 : 0x13A;
        uint8_t* data  = Host_PakReadFileLayered(id, &size);
        int      count = Fonts_PatchGameFont(data, size, big);
        if (count) {
            if (!big)
                Lang_RestyleWords(data, size);
            Host_PakSetLayer(id, data, size);
        }
        printf("[fonts] %s atlas: %d modern glyphs%s\n", big ? "large" : "small", count, count ? "" : " (FAILED)");
        free(data);
    }
}
