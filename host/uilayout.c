/* dw2-pc-port: PC-side text layout for the game's own windows (no game data here).
 *
 * The game draws its windows as ready-made sprite layouts ("parts") and its texts as separate text
 * boxes that know nothing about the window around them; with longer (German) texts a line can run
 * past the window. This file connects the two, in one place for every window and text:
 *
 *   - text metrics (UiText_*): the ink of each glyph of both fonts, read from the font files the
 *     game uses (after the German layer). Modern UI sets letters with their own width (+1 pixel)
 *     instead of the fixed cell; digits, symbols and word pictures keep the cell (columns of
 *     numbers stay aligned).
 *   - windows (UiLayout_NotePart): every visible part drawn in a frame is noted as a rectangle.
 *   - fitting (UiLayout_FitLine): text.c measures each line before drawing it. When the line would
 *     end past the inner edge of the window it starts in, Modern UI grows that window to the right
 *     (as far as the screen and the next window allow); what is still missing is taken from the
 *     letter spacing of that line, in both modes. Classic UI keeps the PS1 picture otherwise.
 *   - growing (UiLayout_PartCells): a grown window is drawn with its middle tile repeated and its
 *     right edge moved; the same for its shadow (a second part of the same size next to it).
 * The rectangles of the previous frame are used (windows stay put between frames).
 * DW2_PARTS_LOG=1 prints each part layout once, DW2_PARTS_CELLS=<file id> also its cells. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"
#include "host/settings.h"
#include "host/uilayout.h"

/* ---- text metrics ---- */

#define TIM_IMG (4 + 8 + 44 + 12) /* both font files: 4-bit 128x256 TIM image (host/lang.c) */
static uint8_t ink_l[2][256], ink_w[2][256];
static int have_metrics;

static int font_px(const uint8_t *d, int size, int x, int y) {
    int o = TIM_IMG + y * 64 + x / 2;
    return o < size ? (d[o] >> ((x & 1) * 4)) & 15 : 0;
}

static void measure_font(int big) {
    static const int cols[2] = { 17, 13 }, cw[2] = { 7, 9 }, ch[2] = { 10, 14 };
    int size = 0;
    uint8_t *d = Host_PakReadFileLayered(big ? 0x110 : 0x13A, &size);

    for (int c = 0; c < 256; c++) {
        ink_l[big][c] = 0;
        ink_w[big][c] = (uint8_t)cw[big];
    }
    if (d == NULL || size < TIM_IMG + 256 * 64) {
        free(d);
        return;
    }
    for (int c = 0; c < 0xEF; c++) {
        int cc = big && c >= 0x88 ? c - 0x88 : c, x0 = (cc % cols[big]) * cw[big], y0 = (cc / cols[big]) * ch[big];
        int l = 99, r = -1;
        if (y0 + ch[big] > 256) {
            continue;
        }
        for (int x = 0; x < cw[big]; x++) {
            for (int y = 0; y < ch[big]; y++) {
                if (font_px(d, size, x0 + x, y0 + y)) {
                    l = x < l ? x : l;
                    r = x > r ? x : r;
                }
            }
        }
        if (r >= 0) {
            ink_l[big][c] = (uint8_t)l;
            ink_w[big][c] = (uint8_t)(r - l + 1);
        }
    }
    free(d);
}

void UiText_Init(void) {
    measure_font(0);
    measure_font(1);
    have_metrics = 1;
}

int UiText_Modern(void) {
    return have_metrics && Settings_Get(SET_UI_STYLE) == SET_UI_MODERN;
}

/* a letter or a punctuation mark: its own width; everything else keeps the cell */
static int proportional(int big, int c) {
    if (c >= 0x0A && c <= 0x3D) {
        return 1; /* A-Z a-z */
    }
    if (c >= 0xE6 && c <= 0xEC) {
        return 1; /* the German layer's umlauts */
    }
    if (big) {
        return c == 0x42 || c == 0x44 || c == 0x45 || c == 0x46 || c == 0x49; /* & ? ! / - */
    }
    /* & ? ! / - , . ' " ; : % + = # (host/lang.c encode) */
    return c == 0x42 || c == 0x44 || c == 0x45 || c == 0x46 || c == 0x49 || (c >= 0x54 && c <= 0x5D);
}

int UiText_Advance(int big, int c, int cell) {
    if (!UiText_Modern() || c < 0 || c > 255 || !proportional(big, c)) {
        return cell;
    }
    return ink_w[big][c] + 1;
}

/* a line that does not fit: letters set close (their ink only), in both modes */
int UiText_TightAdvance(int big, int c, int cell) {
    if (!have_metrics || c < 0 || c > 255 || !proportional(big, c)) {
        return cell;
    }
    return ink_w[big][c];
}

int UiText_TightInkLeft(int big, int c) {
    if (!have_metrics || c < 0 || c > 255 || !proportional(big, c)) {
        return 0;
    }
    return ink_l[big][c];
}

int UiText_SpaceAdvance(int big, int cell) {
    return UiText_Modern() ? (big ? 5 : 4) : cell;
}

int UiText_InkLeft(int big, int c) {
    if (!UiText_Modern() || c < 0 || c > 255 || !proportional(big, c)) {
        return 0;
    }
    return ink_l[big][c];
}

/* ---- windows ---- */

typedef struct {
    int x, y, w, h;
    int seg; /* cannot be grown: one of several windows in a part, or not made of columns */
} Rect;

#define MAX_RECTS 256
static Rect rects[2][MAX_RECTS];
static int nrect[2];
#define MAX_GROW 32
static struct {
    Rect r;
    int grow;
} grows[2][MAX_GROW];
static int ngrow[2];
static int nshift[2]; /* column shifts noted this / last frame (below) */
static int cur;
static int log_parts = -1;
static int fit_lines, grown_lines, squeezed_lines; /* stats for the log */

void UiLayout_Flip(void) {
    cur ^= 1;
    nrect[cur] = 0;
    ngrow[cur] = 0;
    nshift[cur] = 0;
}

/* GfxPartSprite (include/main/156C.h): fileId 0, x 4, y 6, partGroup 0xD; cells 0x10 each:
   u 0, v 1, x 2, y 4, w 8, h 9, frame 0xC, list end u == 0xFF */
static int part_rect(const uint8_t *p, const uint8_t *e, Rect *out) {
    int16_t px, py;
    int x0 = 99999, y0 = 99999, x1 = -99999, y1 = -99999, xs[64], ws[64], nx = 0, right = -99999, stretch = -99999;
    int sw = 0;

    memcpy(&px, p + 4, 2);
    memcpy(&py, p + 6, 2);
    for (int n = 0; e[0] != 0xFF && n < 512; e += 0x10, n++) {
        int16_t ex, ey;
        int w = e[8], h = e[9] ? e[9] : 255, k;
        if (e[0xC] != p[0xD]) {
            continue;
        }
        memcpy(&ex, e + 2, 2);
        memcpy(&ey, e + 4, 2);
        for (k = 0; k < nx && xs[k] != ex; k++) {
        }
        if (k == nx && nx < 64) {
            xs[nx] = ex;
            ws[nx++] = w;
        }
        right = ex > right ? ex : right;
        x0 = ex < x0 ? ex : x0;
        y0 = ey < y0 ? ey : y0;
        x1 = ex + w > x1 ? ex + w : x1;
        y1 = ey + h > y1 ? ey + h : y1;
    }
    if (x1 <= x0) {
        return 0;
    }
    for (int k = 0; k < nx; k++) {
        if (xs[k] < right && xs[k] > stretch) {
            stretch = xs[k];
            sw = ws[k];
        }
    }
    /* grown the way UiLayout_PartCells does it: left edge, middle columns, right edge */
    *out = (Rect){ px + x0, py + y0, x1 - x0, y1 - y0, nx < 3 || sw < 4 };
    return 1;
}

/* One part can hold several windows side by side, joined by an opaque connector piece (save
   screen header: tab, connector, slot box). The translucent cells (blend bit 0x80) that touch each
   other in a row are each noted as a window of their own. */
static void note_segments(const uint8_t *p, const uint8_t *e) {
    struct { int x0, x1, y0, y1, glass; } c[64];
    int n = 0, breaks = 0;
    int16_t px, py;

    memcpy(&px, p + 4, 2);
    memcpy(&py, p + 6, 2);
    for (int k = 0; e[0] != 0xFF && k < 512; e += 0x10, k++) {
        int16_t ex, ey;
        if (e[0xC] != p[0xD]) {
            continue;
        }
        if (n == 64) {
            return;
        }
        memcpy(&ex, e + 2, 2);
        memcpy(&ey, e + 4, 2);
        c[n].x0 = ex;
        c[n].x1 = ex + e[8];
        c[n].y0 = ey;
        c[n].y1 = ey + (e[9] ? e[9] : 255);
        c[n].glass = (e[0xB] & 0x80) != 0;
        breaks += !c[n].glass;
        n++;
    }
    if (breaks == 0 || n < 3) {
        return;
    }
    for (int i = 1; i < n; i++) { /* by x */
        for (int j = i; j > 0 && c[j].x0 < c[j - 1].x0; j--) {
            __typeof__(c[0]) t = c[j];
            c[j] = c[j - 1];
            c[j - 1] = t;
        }
    }
    for (int i = 0; i < n;) {
        int j = i, x1, y0, y1;
        if (!c[i].glass) {
            i++;
            continue;
        }
        x1 = c[i].x1;
        y0 = c[i].y0;
        y1 = c[i].y1;
        while (j + 1 < n && c[j + 1].glass && c[j + 1].x0 <= x1 + 1) {
            j++;
            x1 = c[j].x1 > x1 ? c[j].x1 : x1;
            y0 = c[j].y0 < y0 ? c[j].y0 : y0;
            y1 = c[j].y1 > y1 ? c[j].y1 : y1;
        }
        if (j > i && x1 - c[i].x0 >= 24 && nrect[cur] < MAX_RECTS) {
            rects[cur][nrect[cur]++] = (Rect){ px + c[i].x0, py + y0, x1 - c[i].x0, y1 - y0, 1 };
        }
        i = j + 1;
    }
}

void UiLayout_NotePart(const void *part, const void *cells) {
    const uint8_t *p = part;
    Rect r;

    if (p == NULL || cells == NULL || !part_rect(p, cells, &r)) {
        return;
    }
    if (log_parts < 0) {
        const char *v = getenv("DW2_PARTS_LOG");
        log_parts = v != NULL && v[0] == '1';
    }
    if (log_parts) {
        static uint32_t seen[512];
        static int nseen;
        uint32_t id, k;
        int known = 0;
        memcpy(&id, p, 4);
        k = id ^ (uint32_t)p[0xD] * 0x9E3779B9u;
        for (int i = 0; i < nseen && !known; i++) {
            known = seen[i] == k;
        }
        if (!known && nseen < 512) {
            const char *want = getenv("DW2_PARTS_CELLS");
            seen[nseen++] = k;
            printf("[parts] file %08X group %d: box %d,%d..%d,%d\n", id, p[0xD], r.x, r.y, r.x + r.w, r.y + r.h);
            if (want != NULL && strtoul(want, NULL, 16) == id) {
                for (const uint8_t *c = cells; c[0] != 0xFF; c += 0x10) {
                    int16_t cx, cy;
                    memcpy(&cx, c + 2, 2);
                    memcpy(&cy, c + 4, 2);
                    printf("[parts]   cell uv %d,%d xy %d,%d wh %d,%d frame %d blend %02X\n", c[0], c[1], cx, cy, c[8],
                           c[9], c[0xC], c[0xB]);
                }
            }
        }
    }
    if (nrect[cur] < MAX_RECTS) {
        rects[cur][nrect[cur]++] = r;
    }
    note_segments(p, cells);
}

/* a window a text can be in: big enough for a line, not a full-screen picture */
static int is_window(const Rect *r) {
    return r->w >= 24 && r->h >= 12 && r->w < 300 && r->h < 200;
}

static int same_window(const Rect *a, const Rect *b) {
    /* the window itself or its shadow (same size, a few pixels off) */
    return a->w == b->w && a->h == b->h && abs(a->x - b->x) <= 3 && abs(a->y - b->y) <= 3;
}

static const Rect *find_window(int x, int y) {
    const int prev = cur ^ 1;
    const Rect *best = NULL;
    int area = 1 << 30;

    for (int i = 0; i < nrect[prev]; i++) {
        const Rect *r = &rects[prev][i];
        if (!is_window(r) || x < r->x || x >= r->x + r->w - 8 || y < r->y || y + 6 > r->y + r->h) {
            continue;
        }
        if (r->w * r->h < area) {
            area = r->w * r->h;
            best = r;
        }
    }
    return best;
}

int UiLayout_FindWindow(int x, int y, int *left, int *right, int *top, int *bottom) {
    const Rect *r = find_window(x, y);

    if (r == NULL) {
        return 0;
    }
    *left = r->x;
    *right = r->x + r->w;
    *top = r->y;
    *bottom = r->y + r->h;
    return 1;
}

#define PAD_RIGHT 8 /* frame and a little air on the right of the text */

/* how far the window `w` can grow to the right: the screen edge and the next window */
static int room_right(const Rect *w) {
    const int prev = cur ^ 1;
    int room = 160 - 4 - (w->x + w->w);

    for (int i = 0; i < nrect[prev]; i++) {
        const Rect *r = &rects[prev][i];
        if (!is_window(r) || same_window(r, w) || r->x <= w->x + w->w / 2) {
            continue;
        }
        if (r->x <= w->x && r->x + r->w >= w->x + w->w) {
            continue; /* a window around this one */
        }
        if (r->y + r->h <= w->y || r->y >= w->y + w->h) {
            continue; /* not beside it */
        }
        if (r->x - 4 - (w->x + w->w) < room) {
            room = r->x - 4 - (w->x + w->w);
        }
    }
    return room < 0 ? 0 : room;
}

int UiLayout_FitLine(int x, int y, int width, int limit) {
    const Rect *w;
    int need, grow = 0, right;

    if (width <= 0) {
        return 0x10000;
    }
    w = find_window(x + 1, y + 1);
    if (w == NULL && limit >= 0x7FFF) {
        return 0x10000;
    }
    fit_lines++;
    right = w != NULL ? w->x + w->w - PAD_RIGHT : 0x7FFF;
    if (limit - 4 < right) {
        right = limit - 4; /* the next text on this line: no growing helps there */
        w = NULL;
    }
    need = x + width - right;
    {
        static int dbg = -1;
        if (dbg < 0) {
            dbg = getenv("DW2_LAYOUT_LOG") != NULL;
        }
        if (dbg && need > -4) {
            printf("[layout] line x %d y %d w %d: window %d..%d, right %d, need %d\n", x, y, width,
                   w ? w->x : 0, w ? w->x + w->w : 0, right, need);
        }
    }
    if (need <= 0) {
        return 0x10000;
    }
    if (w != NULL && !w->seg && UiText_Modern()) {
        int room = room_right(w);
        grow = need < room ? need : room;
        if (grow > 0) {
            int i;
            for (i = 0; i < ngrow[cur]; i++) {
                if (same_window(&grows[cur][i].r, w)) {
                    break;
                }
            }
            if (i < ngrow[cur] || ngrow[cur] < MAX_GROW) {
                if (i == ngrow[cur]) {
                    grows[cur][ngrow[cur]++].r = *w;
                    grows[cur][i].grow = 0;
                }
                if (grow > grows[cur][i].grow) {
                    grows[cur][i].grow = grow;
                }
            }
            grown_lines++;
        }
    }
    need -= grow;
    if (need <= 0) {
        return 0x10000;
    }
    squeezed_lines++;
    {
        int scale = (int)(((int64_t)(width - need) << 16) / width);
        return scale < 0xB000 ? 0xB000 : scale; /* at most about 30 % narrower */
    }
}

static int grow_for(const Rect *r) {
    int g = 0;

    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < ngrow[b]; i++) {
            if (same_window(&grows[b][i].r, r) && grows[b][i].grow > g) {
                g = grows[b][i].grow;
            }
        }
    }
    return g;
}

/* The cells to draw for a part: the game's, or for a grown window a copy with the last middle
   column repeated over the new width and everything right of it moved. Only windows made of at
   least three columns (left edge, middle, right edge) are grown. */
const void *UiLayout_PartCells(const void *part, const void *cells) {
    static uint8_t buf[4][128 * 0x10];
    static int ring;
    const uint8_t *p = part, *e = cells;
    Rect r;
    int g, n = 0, xs[64], nx = 0, stretch = -99999, sw = 0, rightmost = -99999;
    uint8_t *out;

    if (p == NULL || e == NULL || !UiText_Modern() || ngrow[0] + ngrow[1] == 0 || !part_rect(p, e, &r) ||
        !is_window(&r) || (g = grow_for(&r)) <= 0) {
        return cells;
    }
    for (const uint8_t *c = e; c[0] != 0xFF && n < 96; c += 0x10, n++) {
        int16_t cx;
        int k;
        if (c[0xC] != p[0xD]) {
            continue;
        }
        memcpy(&cx, c + 2, 2);
        for (k = 0; k < nx && xs[k] != cx; k++) {
        }
        if (k == nx && nx < 64) {
            xs[nx++] = cx;
        }
        rightmost = cx > rightmost ? cx : rightmost;
    }
    if (nx < 3 || n >= 96) {
        return cells;
    }
    for (int k = 0; k < nx; k++) {
        if (xs[k] < rightmost && xs[k] > stretch) {
            stretch = xs[k]; /* the middle column next to the right edge */
        }
    }
    out = buf[ring];
    ring = (ring + 1) & 3;
    {
        int m = 0;
        for (const uint8_t *c = e; c[0] != 0xFF; c += 0x10) {
            int16_t cx;
            memcpy(out + m * 0x10, c, 0x10);
            memcpy(&cx, c + 2, 2);
            if (c[0xC] == p[0xD]) {
                if (cx == stretch) {
                    sw = c[8];
                } else if (cx > stretch) {
                    int16_t nxp = (int16_t)(cx + g);
                    memcpy(out + m * 0x10 + 2, &nxp, 2);
                }
            }
            m++;
        }
        if (sw < 4) {
            return cells;
        }
        /* copies of the stretch column's cells over the new width */
        for (const uint8_t *c = e; c[0] != 0xFF; c += 0x10) {
            int16_t cx;
            memcpy(&cx, c + 2, 2);
            if (c[0xC] != p[0xD] || cx != stretch) {
                continue;
            }
            for (int done = 0; done < g && m < 127; done += sw) {
                int16_t nxp = (int16_t)(stretch + c[8] + done);
                memcpy(out + m * 0x10, c, 0x10);
                memcpy(out + m * 0x10 + 2, &nxp, 2);
                out[m * 0x10 + 8] = (uint8_t)(g - done < c[8] ? g - done : c[8]);
                m++;
            }
        }
        out[m * 0x10] = 0xFF; /* end of the list */
    }
    return out;
}

/* ---- columns: a text box pushed right so the text left of it has room (Modern) ---- */

#define MAX_SHIFT 16
static struct {
    int x, top, bottom, shift;
} shifts[2][MAX_SHIFT];

void UiLayout_RequestShift(int x, int top, int bottom, int shift) {
    int i;

    for (i = 0; i < nshift[cur]; i++) {
        if (shifts[cur][i].x == x && shifts[cur][i].top == top) {
            break;
        }
    }
    if (i == nshift[cur]) {
        if (i == MAX_SHIFT) {
            return;
        }
        shifts[cur][i].x = x;
        shifts[cur][i].top = top;
        shifts[cur][i].bottom = bottom;
        shifts[cur][i].shift = 0;
        nshift[cur]++;
    }
    if (shift > shifts[cur][i].shift) {
        shifts[cur][i].shift = shift;
    }
}

int UiLayout_ColumnShift(int x, int y) {
    int best = 0;

    if (!UiText_Modern()) {
        return 0;
    }
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < nshift[b]; i++) {
            if (shifts[b][i].x == x && y >= shifts[b][i].top && y < shifts[b][i].bottom && shifts[b][i].shift > best) {
                best = shifts[b][i].shift;
            }
        }
    }
    return best;
}

int UiLayout_MaxRight(int x, int y) {
    const Rect *w = find_window(x + 1, y + 1);

    if (w == NULL) {
        return 0xA0; /* the screen edge */
    }
    return w->x + w->w + (!w->seg && UiText_Modern() ? room_right(w) : 0);
}

void UiLayout_Stats(int *lines, int *grown, int *squeezed) {
    *lines = fit_lines;
    *grown = grown_lines;
    *squeezed = squeezed_lines;
}
