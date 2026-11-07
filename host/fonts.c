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

extern const unsigned char Dw2Interface[], Dw2Display[], Dw2Telemetry[], Dw2InterfaceBold[], Dw2DisplayHeavy[];
INCLUDE_BIN(Dw2Interface, "misc/fonts/DW2Interface.ttf");
INCLUDE_BIN(Dw2Display, "misc/fonts/DW2Display.ttf");
INCLUDE_BIN(Dw2Telemetry, "misc/fonts/DW2Telemetry.ttf");
INCLUDE_BIN(Dw2InterfaceBold, "misc/fonts/DW2InterfaceBold.ttf"); /* Exo 2 Bold: emphasis, numbers */
INCLUDE_BIN(Dw2DisplayHeavy, "misc/fonts/DW2DisplayHeavy.ttf");   /* Orbitron Black: big battle words */
/* Generated from the committed binaries by tools/fonts/build_fonts.py. */
#include "misc/fonts/sizes.h"
static stbtt_fontinfo faces[FONT_COUNT];
static int            ready;

const unsigned char* Fonts_Data(int role, int* size)
{
    static const unsigned char* const data[]  = { Dw2Interface, Dw2Display, Dw2Telemetry, Dw2InterfaceBold, Dw2DisplayHeavy };
    static const int                  sizes[] = { DW2_INTERFACE_SIZE, DW2_DISPLAY_SIZE, DW2_TELEMETRY_SIZE,
                                                  DW2_INTERFACE_BOLD_SIZE, DW2_DISPLAY_HEAVY_SIZE };
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
        width < 1 || height < 1 || width > 8192 || height > 2048 || stride < width)
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
        uint8_t mask[14 * 9], cell[14 * 9];
        int     cc = big && c >= 0x88 ? c - 0x88 : c, x0 = (cc % cols) * cw, y0 = (cc / cols) * ch;
        memset(cell, 0, sizeof(cell));
        if (c == 0x43 || (c >= 0x4A && c <= 0x4E)) {
            char label[3] = { 'L', (char)('1' + c - 0x4A), 0 };
            if (c == 0x43) {
                label[0] = 'I';
                label[1] = 'I';
            }
            if (Fonts_Mask(FONT_TELEMETRY, label, mask, gw, gw, gh) < 0)
                return 0;
            for (int y = 0; y < gh; y++)
                for (int x = 0; x < gw; x++)
                    cell[y * cw + x] = mask[y * gw + x] >= 80;
        } else {
            if (!cp)
                continue;
            if (!Fonts_GameGlyph(big, c, 1, mask))
                return 0;
            Fonts_GameShadow(big, mask, cw, ch, 96, 1, cell, cw);
        }
        if ((cc / cols + 1) * ch > 256)
            continue;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int v  = cell[y * cw + x];
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

/* ---- the game atlas glyphs, version 2 (7 November 2026) ----
 * One rule for the 1x atlas and every runtime size k, so the layout (text.c, 1/256 pixel
 * advances) and the pictures agree:
 *   - size from the original font's cap height (small 7x10 cells: 6.6 px, big 9x14: 9.8 px), not
 *     from the widest letter: before, the whole alphabet was shrunk until "W" fitted the 7 pixel
 *     cell and capitals were about 5 px high, smaller than the PS1 letters (7 px);
 *   - the pen at 0.5 px from the cell's left edge (glyphs were centred in the cell: uneven
 *     spacing); a letter wider than its cell minus the shadow (W, M, m, w) is set narrower;
 *   - advances from the font (in 1/256 pixel, with kerning and a little tracking for the shadow);
 *     digits keep the cell (columns of numbers) and are centred; Exo 2's digits (plain zero: Roboto
 *     Mono's dotted zero read as 8 at this size).
 * The shadow of the original letters (one pixel right / down in the palette's shadow colours) is
 * added by the callers (Fonts_GameShadow). */
static const float g_cap[2] = { 6.4f, 9.4f }, g_base[2] = { 7.1f, 10.4f }, g_track[2] = { 0.45f, 0.55f };
/* cells; the part text.c draws of each (glyph): small 7 x 9 of 7 x 10, big 8 x 13 of 9 x 14 */
static const int g_cw[2] = { 7, 9 }, g_ch[2] = { 10, 14 }, g_gw[2] = { 7, 8 }, g_gh[2] = { 9, 13 };
#define G_ORIGIN 0.5f

static stbtt_fontinfo* game_face(int c)
{
    (void)c;
    return &faces[FONT_INTERFACE];
}

static float game_scale(int big) /* font units -> 1x pixels */
{
    stbtt_fontinfo* f = &faces[FONT_INTERFACE];
    int             x0, y0, x1, y1;
    if (!stbtt_GetCodepointBox(f, 'H', &x0, &y0, &x1, &y1) || y1 <= 0)
        return 0;
    return g_cap[big] / (float)y1;
}

/* horizontal factor for a letter whose ink would not fit its cell (1 = as designed) */
static float game_condense(int big, int cp, float sc)
{
    int x0, y0, x1, y1;
    if (!stbtt_GetCodepointBox(game_face(0), cp, &x0, &y0, &x1, &y1) || x1 <= x0)
        return 1;
    float room = g_gw[big] - G_ORIGIN - 0.75f - (x0 > 0 ? x0 * sc : 0);
    float ink  = (x1 - (x0 > 0 ? x0 : 0)) * sc;
    return ink > room && ink > 0 ? room / ink : 1;
}

static int game_digit(int c) { return c >= 0 && c < 10; }

int Fonts_GameAdvance(int big, int c)
{
    int cp = Fonts_GameCodepoint(c), adv, lsb;
    if (!Fonts_Init() || !cp || game_digit(c))
        return g_cw[big ? 1 : 0] * 256;
    big       = big ? 1 : 0;
    float sc  = game_scale(big);
    stbtt_GetCodepointHMetrics(game_face(c), cp, &adv, &lsb);
    float a = adv * sc * game_condense(big, cp, sc) + g_track[big];
    return (int)(a * 256 + 0.5f);
}

int Fonts_GameSpace(int big)
{
    int adv, lsb;
    big = big ? 1 : 0;
    if (!Fonts_Init())
        return (big ? 5 : 4) * 256;
    stbtt_GetCodepointHMetrics(game_face(0), ' ', &adv, &lsb);
    return (int)((adv * game_scale(big) + g_track[big]) * 256 + 0.5f);
}

int Fonts_GameKern(int big, int a, int b)
{
    int ca = Fonts_GameCodepoint(a), cb = Fonts_GameCodepoint(b);
    if (!Fonts_Init() || !ca || !cb || game_digit(a) || game_digit(b))
        return 0;
    return (int)(stbtt_GetCodepointKernAdvance(game_face(a), ca, cb) * game_scale(big ? 1 : 0) * 256);
}

int Fonts_GameOrigin(int big, int c)
{
    (void)big;
    return game_digit(c) || !Fonts_GameCodepoint(c) ? 0 : (int)(G_ORIGIN * 256);
}

/* The glyph of game code c into a (cw*k) x (ch*k) coverage mask (the whole cell). 0: no letter. */
int Fonts_GameGlyph(int big, int c, int k, uint8_t* out)
{
    int cp = Fonts_GameCodepoint(c);
    big    = big ? 1 : 0;
    if (!Fonts_Init() || !cp || k < 1 || k > 16)
        return 0;
    int   W = g_cw[big] * k, H = g_ch[big] * k;
    float sc = game_scale(big) * k, cx = game_condense(big, cp, game_scale(big));
    float sx = sc * cx, ox;
    int   x0, y0, x1, y1;
    memset(out, 0, (size_t)W * H);
    stbtt_GetCodepointBitmapBoxSubpixel(game_face(c), cp, sx, sc, 0, 0, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return 1; /* a space-like glyph */
    if (game_digit(c)) {
        ox = (g_gw[big] * k - 0.75f * k - (x1 - x0)) / 2.0f - x0; /* centred, the shadow beside */
    } else {
        ox = G_ORIGIN * k;
        if (ox + x0 < 0)
            ox = (float)-x0;
    }
    float frac = ox - floorf(ox);
    stbtt_GetCodepointBitmapBoxSubpixel(game_face(c), cp, sx, sc, frac, 0, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0, px = (int)floorf(ox) + x0, py = (int)floorf(g_base[big] * k + 0.5f) + y0;
    uint8_t* bm = malloc((size_t)bw * bh);
    if (!bm)
        return 0;
    stbtt_MakeCodepointBitmapSubpixel(game_face(c), bm, bw, bh, bw, sx, sc, frac, 0, cp);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int X = px + x, Y = py + y;
            if (X >= 0 && Y >= 0 && X < g_gw[big] * k && Y < g_gh[big] * k && Y < H)
                out[Y * W + X] = bm[y * bw + x];
        }
    free(bm);
    return 1;
}

/* A digit whose ink fills h rows exactly (the original sprite's body height), at most w wide,
 * centred, into a w x h coverage mask (numeric sprites, host/fontnumbers.c). Exo 2: a plain zero. */
int Fonts_Digit(int d, uint8_t* out, int w, int h)
{
    int x0, y0, x1, y1, cp = '0' + d;
    if (!Fonts_Init() || d < 0 || d > 9 || w < 2 || h < 2)
        return 0;
    stbtt_fontinfo* f = &faces[FONT_INTERFACE_BOLD]; /* bold: the numbers read at a glance */
    if (!stbtt_GetCodepointBox(f, '0', &x0, &y0, &x1, &y1) || y1 <= y0)
        return 0;
    float sc = (float)h / (float)(y1 - y0); /* digits share the height of '0' (lining figures) */
    float sx = sc;
    stbtt_GetCodepointBox(f, cp, &x0, &y0, &x1, &y1);
    if ((x1 - x0) * sx > w - 0.25f)
        sx = (w - 0.25f) / (float)(x1 - x0);
    int bx0, by0, bx1, by1;
    float fx = (w - (x1 - x0) * sx) / 2.0f - x0 * sx;
    float fy = 0; /* the top of '0' at row 0 */
    int   top0;
    {
        int a, b, c, e;
        stbtt_GetCodepointBitmapBox(f, '0', sc, sc, &a, &top0, &c, &e);
        (void)a; (void)b; (void)c; (void)e;
    }
    float sub = fx - floorf(fx);
    stbtt_GetCodepointBitmapBoxSubpixel(f, cp, sx, sc, sub, 0, &bx0, &by0, &bx1, &by1);
    memset(out, 0, (size_t)w * h);
    int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0)
        return 1;
    uint8_t* bm = malloc((size_t)bw * bh);
    if (!bm)
        return 0;
    stbtt_MakeCodepointBitmapSubpixel(f, bm, bw, bh, bw, sx, sc, sub, 0, cp);
    int px = (int)floorf(fx) + bx0 - (int)floorf(x0 * sx), py = by0 - top0 + (int)fy;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int X = px + x, Y = py + y;
            if (X >= 0 && Y >= 0 && X < w && Y < h)
                out[Y * w + X] = bm[y * bw + x];
        }
    free(bm);
    return 1;
}

/* Body (index 1) and the original shadow: small font index 2 one pixel right and down; big font
 * index 2 right, 3 below (as the PS1 letters). s = the shadow's offset in output pixels. */
void Fonts_GameShadow(int big, const uint8_t* cov, int W, int H, int thresh, int s, uint8_t* idx, int stride)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int v = 0;
            if (cov[y * W + x] >= thresh)
                v = 1;
            else if (x >= s && y >= s && cov[(y - s) * W + x - s] >= thresh)
                v = big ? 3 : 2;
            else if (x >= s && cov[y * W + x - s] >= thresh)
                v = 2;
            else if (y >= s && cov[(y - s) * W + x] >= thresh)
                v = big ? 3 : 2;
            idx[y * stride + x] = (uint8_t)v;
        }
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

/* dw2-pc-port: the game atlases at the output resolution. The renderer asks for a glyph cell k times
 * larger (k = its scale: 9 at 2160 lines); the letter is rasterized from the TrueType outline at
 * exactly that size, with the same scale, baseline and centring rule as the 1x atlas cell
 * (Fonts_Cell is size independent), so the text keeps the game's layout and only gets sharper.
 * Index 1 (the game's ink) where at least ~28 % of the output pixel is covered (the stroke weight of the 1x cells, which take 31 %). Cells without a
 * letter (controller icons, the multi-cell words) are the 1x texels, k times larger. */
#include "backend/texrep.h"
typedef struct {
    int      big, w, h; /* w in 16-bit words */
    uint8_t* pix;       /* the atlas pixels as uploaded (4 bits each) */
    int16_t  cell_c[256];
} HdAtlas;
static HdAtlas hd_atlas[2];

static int atlas_nibble(const HdAtlas* a, int x, int y)
{
    if (x < 0 || y < 0 || x >= a->w * 4 || y >= a->h)
        return 0;
    return (a->pix[y * a->w * 2 + x / 2] >> ((x & 1) * 4)) & 15;
}

static void atlas_index(void* ctx, int k, int x0, int y0, int w, int h, uint8_t* idx)
{
    HdAtlas*  a  = ctx;
    const int cw = a->big ? 9 : 7, ch = a->big ? 14 : 10, cols = a->big ? 13 : 17, gw = a->big ? 8 : 7, gh = a->big ? 13 : 9;
    const int W = w * k, CW = cw * k, CH = ch * k;
    static uint8_t mask[16 * 9 * 16 * 14], cell[16 * 9 * 16 * 14];
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < W; x++)
            idx[y * W + x] = (uint8_t)atlas_nibble(a, x0 + x / k, y0 + y / k);
    for (int cy = y0 / ch; cy * ch < y0 + h; cy++)
        for (int cx = x0 / cw; cx * cw < x0 + w && cx < cols; cx++) {
            int cc = cy * cols + cx, c = cc < 256 ? a->cell_c[cc] : -1, ok;
            if (c < 0 || k > 16)
                continue;
            if (c == 0x43 || (c >= 0x4A && c <= 0x4E)) {
                char label[3] = { 'L', (char)('1' + c - 0x4A), 0 };
                if (c == 0x43)
                    label[0] = label[1] = 'I';
                ok = Fonts_Mask(FONT_TELEMETRY, label, mask, gw * k, gw * k, gh * k) >= 0;
                if (ok)
                    for (int yy = 0; yy < CH; yy++)
                        for (int xx = 0; xx < CW; xx++)
                            cell[yy * CW + xx] = xx < gw * k && yy < gh * k && mask[yy * gw * k + xx] >= 72;
            } else {
                /* the letter and the original shadow (0.75 texel: the PS1's one pixel, a little finer) */
                ok = Fonts_GameGlyph(a->big, c, k, mask);
                if (ok)
                    Fonts_GameShadow(a->big, mask, CW, CH, 72, (3 * k + 2) / 4, cell, CW);
            }
            if (!ok)
                continue;
            for (int yy = 0; yy < CH; yy++)
                for (int xx = 0; xx < CW; xx++) {
                    int X = (cx * cw - x0) * k + xx, Y = (cy * ch - y0) * k + yy;
                    if (X < 0 || Y < 0 || X >= W || Y >= h * k)
                        continue;
                    idx[Y * W + X] = cell[yy * CW + xx];
                }
        }
}

static void hd_register(int big, const uint8_t* data, int size)
{
    HdAtlas* a = &hd_atlas[big];
    if (size < 68 || data[4] != 0x10 || data[8] != 8)
        return;
    int w = data[56 + 8] | data[56 + 9] << 8, h = data[56 + 10] | data[56 + 11] << 8;
    if (w <= 0 || h <= 0 || 68 + w * h * 2 > size)
        return;
    free(a->pix);
    a->pix = malloc((size_t)w * h * 2);
    if (!a->pix)
        return;
    memcpy(a->pix, data + 68, (size_t)w * h * 2);
    a->big = big;
    a->w   = w;
    a->h   = h;
    for (int i = 0; i < 256; i++)
        a->cell_c[i] = -1;
    const int cols = big ? 13 : 17;
    for (int c = 0; c <= 0xEC; c++) { /* the cell each code was written to by Fonts_PatchGameFont (last wins) */
        int cc = big && c >= 0x88 ? c - 0x88 : c;
        if (Fonts_GameCodepoint(c) || c == 0x43 || (c >= 0x4A && c <= 0x4E))
            if (cc < 256 && cc / cols * (big ? 14 : 10) < h)
                a->cell_c[cc] = (int16_t)c;
    }
    TexRep_AddIndexSource((const uint16_t*)a->pix, w, h, 0, big ? 9 : 7, big ? 14 : 10, atlas_index, a);
}

void Fonts_RegisterHd(int big, const uint8_t* data, int size) { hd_register(big, data, size); }
void        Fonts_ApplyGame(void)
{
    TexRep_ClearIndexSources(); /* the atlases and word pictures of this language register again */
    for (int big = 0; big < 2; big++) {
        int      size = 0, id = big ? 0x110 : 0x13A;
        uint8_t* data  = Host_PakReadFileLayered(id, &size);
        int      count = Fonts_PatchGameFont(data, size, big);
        if (count) {
            if (!big)
                Lang_RestyleWords(data, size);
            Host_PakSetLayer(id, data, size);
            hd_register(big, data, size);
        }
        printf("[fonts] %s atlas: %d modern glyphs%s\n", big ? "large" : "small", count, count ? "" : " (FAILED)");
        free(data);
    }
}
