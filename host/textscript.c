/* dw2-pc-port: text/script string layout, event tables and safe re-layout (see textscript.h).
 *
 * Command lengths, from Text_DrawAll in src/main/text.c (the path a command takes once it has run,
 * which is the one every later frame walks, so the data must agree with it):
 *   0x00..0xEE  glyph (0x5E..0xE4 in the small font are drawn words)          1
 *   EF k        glyph 0xF0+k                                                 2
 *   F0 k        player / beetle name, argument 1..4, dictionary word k>=6    2
 *   F1 a b c    Digimon name (3 decimal digits)                              4
 *   F2 a b c    item name                                                    4
 *   F3 m a      fade out, then game mode m (FC..FF special) with argument a  3  (ends the text)
 *   F4 k ...    k<0x10 walker animation k + 3 digits (Digimon id)            5
 *               k<0x20 turn walker to (k-0x10)*0x400 + 3 digits              5
 *               k<0x30 run task 0x16 with k&15                               2
 *               k<0x40 text colour k&15                                      2
 *               else   sound effect k&15 (4 also shakes the screen)          2
 *   F5          pause 30 frames                                              1
 *   F6/F7 9 dig three 3-digit numbers: walker id, cell x, y (F6 waits)       10
 *   F8 k        yes/no choice (0 = wait for the choice, 1/2 cursor marks)    2
 *   F9 k ...    portrait: k odd = close (1 arg), k even = 3 digits image     2 / 5
 *   FA k        message window position / portrait box                       2
 *   FB          wait for the button                                          1
 *   FC          new page                                                     1
 *   FD          space                                                        1
 *   FE          new line                                                     1
 *   FF          end                                                          1 */
#include <stdlib.h>
#include <string.h>

#include "host/textscript.h"

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int TextScript_ElemLen(const uint8_t *d, int avail) {
    int n;

    if (avail <= 0) {
        return 0;
    }
    switch (d[0]) {
    case 0xEF: case 0xF0: case 0xF8: case 0xFA: n = 2; break;
    case 0xF1: case 0xF2: n = 4; break;
    case 0xF3: n = 3; break;
    case 0xF4:
        if (avail < 2) return 0;
        n = d[1] < 0x20 ? 5 : 2;
        break;
    case 0xF6: case 0xF7: n = 10; break;
    case 0xF9:
        if (avail < 2) return 0;
        n = (d[1] & 1) ? 2 : 5;
        break;
    default: n = 1; break;
    }
    return n <= avail ? n : 0;
}

int TextScript_Len(const uint8_t *d, int size, uint32_t o) {
    uint32_t i = o;

    while ((int)i < size) {
        int k = TextScript_ElemLen(d + i, size - (int)i);
        if (k == 0) {
            return 0;
        }
        if (d[i] == 0xFF) {
            return (int)(i - o) + 1;
        }
        i += (uint32_t)k;
    }
    return 0;
}

int TextScript_Valid(const uint8_t *s, int len) {
    return len > 0 && TextScript_Len(s, len, 0) == len;
}

int TextScript_FlagTable(const uint8_t *d, int size, uint32_t *slots, int max, int *nslots) {
    uint32_t base, r;
    int entries = 0, n = 0;

    *nslots = 0;
    if (size < 16) {
        return 0;
    }
    base = rd32(d);
    if (base < 12 || (base & 3) || base + TS_FLAG_STRIDE > (uint32_t)size ||
        rd32(d + 4) >= (uint32_t)size || rd32(d + 8) >= (uint32_t)size) {
        return 0;
    }
    for (r = base; ; r += TS_FLAG_STRIDE) {
        if (r + 2 > (uint32_t)size) {
            return 0; /* the game needs the terminating entry */
        }
        if ((d[r] | d[r + 1] << 8) == 0) {
            break;
        }
        if (r + TS_FLAG_STRIDE > (uint32_t)size) {
            return 0;
        }
        if (rd32(d + r + TS_FLAG_BRANCHES) == 0) {
            return 0; /* branch 0 is the fallback of Flag_SelectBranch: always used */
        }
        for (int b = 0; b < 6; b++) {
            uint32_t at = r + TS_FLAG_BRANCHES + 4u * (uint32_t)b, o = rd32(d + at);
            int len;
            if (o == 0) {
                continue;
            }
            if (o < base || o >= (uint32_t)size || (len = TextScript_Len(d, size, o)) < 2) {
                return 0;
            }
            if (n >= max) {
                return 0; /* every pointer must be known, or a re-layout would leave one behind */
            }
            slots[n++] = at;
        }
        entries++;
    }
    *nslots = n;
    return entries;
}

typedef struct { uint32_t a, b; } Span;

static int span_cmp(const void *x, const void *y) {
    const Span *p = x, *q = y;
    return p->a < q->a ? -1 : p->a > q->a;
}

int TextScript_Area(const uint8_t *d, int size, const uint32_t *slots, int n, int hdr_words,
                    uint32_t *start, uint32_t *end, uint32_t *tail) {
    Span *sp;
    uint32_t cur;
    int ok = 1;

    if (n <= 0) {
        return 0;
    }
    sp = malloc(sizeof(*sp) * (size_t)n);
    if (sp == NULL) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        uint32_t o = rd32(d + slots[i]);
        int len = o < (uint32_t)size ? TextScript_Len(d, size, o) : 0;
        if (len == 0) {
            free(sp);
            return 0;
        }
        sp[i].a = o;
        sp[i].b = o + (uint32_t)len;
    }
    qsort(sp, (size_t)n, sizeof(*sp), span_cmp);
    *start = cur = sp[0].a;
    for (int i = 0; i < n && ok; i++) {
        if (sp[i].a > cur) {
            if (sp[i].a - cur > 3) {
                ok = 0;
            }
            for (uint32_t k = cur; k < sp[i].a; k++) {
                if (d[k] != 0x00 && d[k] != 0xFF) ok = 0;
            }
        }
        if (sp[i].b > cur) {
            cur = sp[i].b;
        }
    }
    free(sp);
    *end = cur;
    /* nothing the file points at may live inside the area: header offsets, the slots themselves */
    for (int h = 0; h < hdr_words && ok && (uint32_t)(h * 4 + 4) <= (uint32_t)size; h++) {
        uint32_t o = rd32(d + h * 4);
        if (o > *start && o < *end) ok = 0;
        if ((uint32_t)h * 4 >= *start && (uint32_t)h * 4 < *end) ok = 0;
    }
    for (int i = 0; i < n && ok; i++) {
        if (slots[i] + 4 > *start && slots[i] < *end) ok = 0;
    }
    if (!ok) {
        return 0;
    }
    /* free tail: only when every header block starts before the area and all after it is zero;
     * else *tail = size (no tail) */
    *tail = (uint32_t)size;
    {
        int last = 1;
        for (int h = 0; h < hdr_words && (uint32_t)(h * 4 + 4) <= (uint32_t)size; h++) {
            if (rd32(d + h * 4) >= *end) last = 0;
        }
        for (uint32_t k = *end; k < (uint32_t)size && last; k++) {
            if (d[k] != 0) last = 0;
        }
        if (last) {
            *tail = (*end + 3) & ~3u;
            if (*tail > (uint32_t)size) *tail = (uint32_t)size;
        }
    }
    return 1;
}

int TextScript_Repack(uint8_t *d, int size, const uint32_t *slots, int n, uint32_t start, uint32_t end,
                      uint32_t tail, uint8_t *const *trans, const int *tlen, int *placed) {
    uint8_t *out, **str;
    int *len, *orig_len, fell = 0, used = 0, maxlen = 0;
    uint32_t at = start, rest = 0, cap_tail = tail < (uint32_t)size ? (uint32_t)size - tail : 0;
    uint32_t *pos;

    *placed = 0;
    if (n <= 0 || start >= end || end > (uint32_t)size || tail < end || tail > (uint32_t)size) {
        return -1;
    }
    out = malloc((size_t)size);
    str = calloc((size_t)n, sizeof(*str));
    len = calloc((size_t)n, sizeof(*len));
    orig_len = calloc((size_t)n, sizeof(*orig_len));
    pos = calloc((size_t)n, sizeof(*pos));
    if (out == NULL || str == NULL || len == NULL || orig_len == NULL || pos == NULL) {
        free(out); free(str); free(len); free(orig_len); free(pos);
        return -1;
    }
    memcpy(out, d, (size_t)size);
    for (int i = 0; i < n; i++) {
        orig_len[i] = TextScript_Len(d, size, rd32(d + slots[i]));
        if (orig_len[i] == 0) {
            free(out); free(str); free(len); free(orig_len); free(pos);
            return -1;
        }
        rest += (uint32_t)orig_len[i];
        maxlen = orig_len[i] > maxlen ? orig_len[i] : maxlen;
    }
    memset(out + start, 0xFF, end - start);
    if (tail < (uint32_t)size) memset(out + tail, 0, (size_t)size - tail);
    for (int i = 0; i < n; i++) {
        const uint8_t *orig = d + rd32(d + slots[i]);
        int dup = -1;

        rest -= (uint32_t)orig_len[i];
        for (int pass = 0; pass < 2; pass++) {
            const uint8_t *s = pass == 0 && trans[i] != NULL ? trans[i] : orig;
            int l = pass == 0 && trans[i] != NULL ? tlen[i] : orig_len[i];
            uint32_t p = at, free_after;

            if (s == trans[i] && !TextScript_Valid(s, l)) {
                continue; /* a broken translation never goes in */
            }
            dup = -1;
            for (int k = 0; k < i && dup < 0; k++) {
                if (str[k] != NULL && len[k] == l && memcmp(str[k], s, (size_t)l) == 0) dup = k;
            }
            if (dup >= 0) {
                str[i] = (uint8_t *)s;
                len[i] = l;
                pos[i] = pos[dup];
                if (s == trans[i]) used++; else if (trans[i] != NULL) fell++;
                break;
            }
            if (p < end && p + (uint32_t)l > end) p = tail; /* area full: on in the tail */
            if (p + (uint32_t)l > (p >= tail ? (uint32_t)size : end)) {
                if (s == orig) break; /* not even the original fits */
                continue;
            }
            /* a translation must leave room for the originals still to come */
            free_after = (p < end ? end - (p + (uint32_t)l) + cap_tail : (uint32_t)size - (p + (uint32_t)l));
            if (s == trans[i] && s != orig && free_after < rest + (cap_tail ? (uint32_t)maxlen : 0)) {
                continue;
            }
            memcpy(out + p, s, (size_t)l);
            str[i] = (uint8_t *)s;
            len[i] = l;
            pos[i] = p;
            at = p + (uint32_t)l;
            if (s == trans[i] && trans[i] != NULL) used++; else if (trans[i] != NULL) fell++;
            break;
        }
        if (str[i] == NULL) {
            free(out); free(str); free(len); free(orig_len); free(pos);
            return -1;
        }
    }
    for (int i = 0; i < n; i++) {
        wr32(out + slots[i], pos[i]);
    }
    memcpy(d, out, (size_t)size);
    free(out); free(str); free(len); free(orig_len); free(pos);
    *placed = used;
    return fell;
}
