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
 *     end past the inner edge of the window it starts in, that window grows to the right (as far
 *     as the screen and the next window allow); a text box right of the line is moved aside when
 *     there is room; what is still missing is taken from the letter spacing of that line (set
 *     close, then narrower). Both modes do this; English texts fit the PS1 windows, so with the
 *     Classic UI an English game looks exactly like the PS1 one.
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
#include "host/fonts.h"

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

/* Advances and offsets in 1/256 pixel (src/main/text.c). Modern UI: the font's metrics
   (host/fonts.c Fonts_Game*, the same rule that draws the glyphs); Classic: the cell. */
int UiText_Advance(int big, int c, int cell) {
    if (!UiText_Modern() || c < 0 || c > 255 || !proportional(big, c)) {
        return cell * 256;
    }
    return Fonts_Enabled() ? Fonts_GameAdvance(big, c) : (ink_w[big][c] + 1) * 256;
}

int UiText_Kern(int big, int a, int b) {
    if (!UiText_Modern() || !Fonts_Enabled() || a < 0 || a > 255 || b < 0 || b > 255 || !proportional(big, a) ||
        !proportional(big, b)) {
        return 0;
    }
    return Fonts_GameKern(big, a, b);
}

/* a line that does not fit: letters set close (their ink only), in both modes */
int UiText_TightAdvance(int big, int c, int cell) {
    if (!have_metrics || c < 0 || c > 255 || !proportional(big, c)) {
        return cell * 256;
    }
    return ink_w[big][c] * 256;
}

int UiText_TightInkLeft(int big, int c) {
    if (!have_metrics || c < 0 || c > 255 || !proportional(big, c)) {
        return 0;
    }
    return ink_l[big][c] * 256;
}

int UiText_SpaceAdvance(int big, int cell) {
    if (!UiText_Modern()) {
        return cell * 256;
    }
    return Fonts_Enabled() ? Fonts_GameSpace(big) : (big ? 5 : 4) * 256;
}

int UiText_InkLeft(int big, int c) {
    if (!UiText_Modern() || c < 0 || c > 255 || !proportional(big, c)) {
        return 0;
    }
    return Fonts_Enabled() ? Fonts_GameOrigin(big, c) : ink_l[big][c] * 256;
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

static int nwwin[2], nwpart[2]; /* wide HUD: windows moved / parts moved this frame (below) */
/* the technique-learning screen after a battle (layouts 0x1A1 entries 0x1B-0x1E): a 4:3 screen of
   its own over the battle; while it is up the battle HUD does not move into the wide margin (its
   cursor and the highlighted text did, away from the lists; player report "Screen Issue 69219") */
static int skilllearn_frame[2];

/* src/stag3000/skilllearn.c Stg30_SkillLearnDraw: the screen is drawn this frame */
void UiLayout_NoteSkillLearn(void) {
    skilllearn_frame[cur] = 1;
}
/* the enemy boxes (0x13B entry 0x48): the PS1 places them in a stair; in the wide HUD they
   share the top-most one's height (the player's own boxes are in one row too) */
static int enemy_top[2] = { 99999, 99999 };

void UiLayout_Flip(void) {
    cur ^= 1;
    nrect[cur] = 0;
    ngrow[cur] = 0;
    nshift[cur] = 0;
    nwwin[cur] = 0;
    nwpart[cur] = 0;
    enemy_top[cur] = 99999;
    skilllearn_frame[cur] = 0;
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

/* ---- wide HUD (Modern UI, 16:9 / 21:9, battle): the battle windows use the width ----
 *
 * The PS1 battle screen puts its windows into the 4:3 picture; in a wide picture they would sit in
 * the middle band over the Digimon. With the Modern UI their frames move into the side margins:
 * a window left of the middle moves left by the margin, one right of it moves right, one in the
 * middle stays (enemy name box, attack description row). Everything drawn inside a moved window
 * (its texts, digits, cursor, highlight) moves with it; what belongs to the 3D scene (target
 * marker, damage numbers, word banners) never does. The windows of a frame decide for the next
 * (they stay put between frames, like the rest of this file). Classic UI and 4:3: no change. */
extern int PsxHd_Wide(void);
extern int PsxHd_Margin(int w);

#define MAX_WWIN 32
#define MAX_WPART 256
static struct {
    Rect r;
    int dx, dy;
} wwin[2][MAX_WWIN];
static struct {
    const void *part;
    int dx, dy;
} wpart[2][MAX_WPART];

static int wide_table = -1; /* the game mode's table (gameMode >> 8): 5 battle */

void UiLayout_SetWideScene(int table) {
    wide_table = table;
}

static int wide_on(void) {
    return wide_table >= 0 && PsxHd_Wide() > 0 && UiText_Modern() && !skilllearn_frame[0] && !skilllearn_frame[1];
}

/* outside battles: the talk portrait (file 0x315, entries 1-4: frame and picture above the
   message window) moves into the margin on its side; the message window (5-8) stays */
static int portrait_dx; /* the shift of the portrait frame drawn last (portrait.c draws the face after it) */

int UiLayout_PortraitShift(void) {
    return wide_on() ? portrait_dx : 0;
}

static int wide_portrait_entry(int file, int entry) {
    return file == 0x315 && entry >= 1 && entry <= 4;
}

/* the battle window frames of file 0x13B that may move (command menus, attack lists, party
   status boxes); the attack description row (3D, 3F, 41, 43, 44, 45) and the enemy box stay */
static int wide_window_entry(int entry) {
    switch (entry) {
    case 0x01: case 0x2A: case 0x32: case 0x33: case 0x3B: case 0x3C: case 0x40: case 0x42: case 0x46: case 0x4A: case 0x4B:
        return 1;
    }
    return 0;
}

/* parts tied to the 3D scene: target / select markers, damage number, damage digits */
static int wide_scene_entry(int entry) {
    return entry == 2 || entry == 3 || entry == 22 || (entry >= 0x0B && entry <= 0x0F);
}

static int wide_dx_at(const Rect *r, int *dy) {
    const int prev = cur ^ 1;
    int best = 0, bdy = 0, area = 1 << 30;

    for (int b = 0; b < 2; b++) {
        int k = b == 0 ? prev : cur;
        for (int i = 0; i < nwwin[k]; i++) {
            const Rect *w = &wwin[k][i].r;
            if (r->x >= w->x - 2 && r->y >= w->y - 2 && r->x + r->w <= w->x + w->w + 2 && r->y + r->h <= w->y + w->h + 2 &&
                w->w * w->h < area) {
                area = w->w * w->h;
                best = wwin[k][i].dx;
                bdy = wwin[k][i].dy;
            }
        }
    }
    if (dy != NULL) *dy = bdy;
    return best;
}

static void wide_note(const uint8_t *p, const Rect *r) {
    uint32_t id;
    int file, entry, dx = 0, dy = 0;

    if (!wide_on() || nwpart[cur] >= MAX_WPART) {
        return;
    }
    memcpy(&id, p, 4);
    file = (int)(id >> 16);
    entry = (int)(id & 0xFFFF);
    if (wide_table != 5) {
        if (wide_portrait_entry(file, entry)) {
            int cx = r->x + r->w / 2, m = PsxHd_Margin(320);
            dx = cx < -40 ? -m : cx > 40 ? m : 0;
            if (entry == 1) {
                portrait_dx = dx;
                if (getenv("DW2_WIDE_LOG")) printf("[wide] portrait %d,%d %dx%d dx %d\n", r->x, r->y, r->w, r->h, dx);
            }
            if (dx != 0 && entry == 1 && nwwin[cur] < MAX_WWIN) {
                wwin[cur][nwwin[cur]].r = *r;
                wwin[cur][nwwin[cur]].dx = dx;
                wwin[cur][nwwin[cur]].dy = 0;
                nwwin[cur]++;
            }
        } else if (file != 0x315) {
            dx = wide_dx_at(r, NULL); /* the face picture inside the portrait frame */
        }
        if (dx != 0) {
            wpart[cur][nwpart[cur]].part = p;
            wpart[cur][nwpart[cur]].dx = dx;
            wpart[cur][nwpart[cur]].dy = 0;
            nwpart[cur]++;
        }
        return;
    }
    if (file != 0x13B || wide_scene_entry(entry)) {
        return;
    }
    if (wide_window_entry(entry) || entry == 0x48) {
        int cx = r->x + r->w / 2, m = PsxHd_Margin(320);
        dx = cx < -40 ? -m : cx > 40 ? m : 0;
        if (entry == 0x48) {
            /* an enemy box: spread like the party's boxes, at the top-most enemy box's height (last
               frame's) */
            enemy_top[cur] = r->y < enemy_top[cur] ? r->y : enemy_top[cur];
            if (enemy_top[cur ^ 1] < 99999 && enemy_top[cur ^ 1] < r->y) dy = enemy_top[cur ^ 1] - r->y;
        }
        if ((dx != 0 || dy != 0) && nwwin[cur] < MAX_WWIN) {
            wwin[cur][nwwin[cur]].r = *r;
            wwin[cur][nwwin[cur]].dx = dx;
            wwin[cur][nwwin[cur]].dy = dy;
            nwwin[cur]++;
        }
    } else {
        dx = wide_dx_at(r, &dy);
    }
    if ((dx != 0 || dy != 0) && nwpart[cur] < MAX_WPART) {
        wpart[cur][nwpart[cur]].part = p;
        wpart[cur][nwpart[cur]].dx = dx;
        wpart[cur][nwpart[cur]].dy = dy;
        nwpart[cur]++;
    }
}

/* parts.c: how far this part is drawn moved (0: where the game puts it) */
int UiLayout_PartShift(const void *part) {
    for (int i = nwpart[cur] - 1; i >= 0; i--) {
        if (wpart[cur][i].part == part) {
            return wpart[cur][i].dx;
        }
    }
    return 0;
}

int UiLayout_PartShiftY(const void *part) {
    for (int i = nwpart[cur] - 1; i >= 0; i--) {
        if (wpart[cur][i].part == part) {
            return wpart[cur][i].dy;
        }
    }
    return 0;
}

/* text.c: how far a text box starting at x, y is drawn moved (the window it is in) */
int UiLayout_TextShift(int x, int y) {
    Rect r = { x, y, 1, 1, 0 };

    if (!wide_on()) {
        return 0;
    }
    return wide_dx_at(&r, NULL);
}

int UiLayout_TextShiftY(int x, int y) {
    Rect r = { x, y, 1, 1, 0 };
    int dy = 0;

    if (!wide_on()) {
        return 0;
    }
    wide_dx_at(&r, &dy);
    return dy;
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
    wide_note(p, &r);
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
    if (w != NULL) {
        /* a part inside the window, right of the line and at its height (a value bar, a field):
           the line ends before it, as before a text (result box: "Nächstes Level" and its bar) */
        const int prev = cur ^ 1;
        for (int i = 0; i < nrect[prev]; i++) {
            const Rect *b = &rects[prev][i];
            if (b == w || same_window(b, w) || b->w < 16 || b->x <= x + 8 || b->x >= limit ||
                b->x < w->x || b->x + b->w > w->x + w->w || b->y < w->y || b->y + b->h > w->y + w->h ||
                b->w * b->h >= w->w * w->h || b->y > y + 5 || b->y + b->h < y + 5) {
                continue;
            }
            limit = b->x;
        }
    }
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
    if (w != NULL && !w->seg) {
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
/* ---- wide backgrounds: a full-width tiled picture continues into the margins ----
 * Menu backgrounds (the field menu grid, its scrolling glass layers ...) are rows of the same tile
 * across the 4:3 picture; in 16:9 / 21:9 the margins stayed black. A part that spans the whole
 * 4:3 width as a grid of equal columns made of a few tiles (menu grid, shop circuit board,
 * DNA lab number rain) continues into the margins: margin column -k repeats column k, mirrored
 * around the edge, so checkerboards and staggered columns go on as they are. A picture made of
 * many different pieces is never repeated. */
static const void *wide_fill(const uint8_t *p, const uint8_t *e) {
    static uint8_t buf[4][320 * 0x10];
    static int ring;
    int16_t px;
    int left = 99999, right = -99999, n = 0, m, margin, tw = -1, ncols, k, cols_needed;
    int uvs[64], nuv = 0, ng = 0;
    uint8_t *out;

    if (PsxHd_Wide() <= 0 || e == NULL) {
        return e;
    }
    memcpy(&px, p + 4, 2);
    for (const uint8_t *c = e; c[0] != 0xFF && n < 240; c += 0x10, n++) {
        int16_t cx;
        int uv, q;
        if (c[0xC] != p[0xD]) {
            continue;
        }
        memcpy(&cx, c + 2, 2);
        if (tw < 0) {
            tw = c[8];
        } else if (c[8] != tw) {
            return e; /* not a grid of equal columns */
        }
        left = cx < left ? cx : left;
        right = cx + c[8] > right ? cx + c[8] : right;
        uv = c[0] | c[1] << 8 | c[9] << 16;
        for (q = 0; q < nuv && uvs[q] != uv; q++) {
        }
        if (q == nuv && nuv < 64) {
            uvs[nuv++] = uv;
        }
        ng++;
    }
    if (n >= 240 || tw < 8 || px + left > -160 || px + right < 160 || nuv * 2 > ng) {
        return e; /* not across the whole 4:3 width, or a picture (many different pieces) */
    }
    ncols = (right - left) / tw;
    margin = PsxHd_Margin(320);
    cols_needed = (margin + tw - 1) / tw;
    if (ncols < 4 || ncols * tw != right - left || cols_needed + 1 > ncols) {
        return e;
    }
    for (const uint8_t *c = e; c[0] != 0xFF; c += 0x10) {
        int16_t cx;
        memcpy(&cx, c + 2, 2);
        if (c[0xC] == p[0xD] && (cx - left) % tw != 0) {
            return e; /* columns not on one grid */
        }
    }
    out = buf[ring];
    ring = (ring + 1) & 3;
    memcpy(out, e, (size_t)n * 0x10);
    m = n;
    /* margin column -k shows column k, column ncols-1+k shows column ncols-1-k (mirrored order
       around the edge column: alternating and staggered patterns stay as they are) */
    for (k = 1; k <= cols_needed; k++) {
        for (int side = 0; side < 2; side++) {
            int src = side == 0 ? k : ncols - 1 - k;
            int dst = side == 0 ? -k : ncols - 1 + k;
            for (int i = 0; i < n && m < 319; i++) {
                const uint8_t *c = e + i * 0x10;
                int16_t cx, nx;
                if (c[0xC] != p[0xD]) {
                    continue;
                }
                memcpy(&cx, c + 2, 2);
                if ((cx - left) / tw != src) {
                    continue;
                }
                nx = (int16_t)(left + dst * tw);
                memcpy(out + m * 0x10, c, 0x10);
                memcpy(out + m * 0x10 + 2, &nx, 2);
                m++;
            }
        }
    }
    if (m == n) {
        return e;
    }
    out[m * 0x10] = 0xFF;
    return out;
}

static const void *part_cells_grown(const void *part, const void *cells);

const void *UiLayout_PartCells(const void *part, const void *cells) {
    const void *c = part_cells_grown(part, cells);
    return part != NULL ? wide_fill(part, c) : c;
}

static const void *part_cells_grown(const void *part, const void *cells) {
    static uint8_t buf[4][128 * 0x10];
    static int ring;
    const uint8_t *p = part, *e = cells;
    Rect r;
    int g, n = 0, xs[64], nx = 0, stretch = -99999, sw = 0, rightmost = -99999;
    uint8_t *out;

    if (p == NULL || e == NULL || ngrow[0] + ngrow[1] == 0 || !part_rect(p, e, &r) ||
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
    return w->x + w->w + (!w->seg ? room_right(w) : 0);
}

void UiLayout_Stats(int *lines, int *grown, int *squeezed) {
    *lines = fit_lines;
    *grown = grown_lines;
    *squeezed = squeezed_lines;
}
