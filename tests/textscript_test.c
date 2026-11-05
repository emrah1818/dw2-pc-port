/* Tests for host/textscript.c with made-up bytes (no game data).
 *   cc -I. tests/textscript_test.c host/textscript.c -o textscript_test && ./textscript_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/textscript.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void w32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t r32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_lengths(void) {
    static const struct { uint8_t b[12]; int n; } t[] = {
        { { 0x0A }, 1 }, { { 0x5E }, 1 }, { { 0xEF, 0x02 }, 2 }, { { 0xF0, 0x07 }, 2 },
        { { 0xF1, 1, 2, 3 }, 4 }, { { 0xF2, 0, 0, 9 }, 4 }, { { 0xF3, 0xFF, 0xFF }, 3 },
        { { 0xF4, 0x03, 0, 1, 2 }, 5 }, { { 0xF4, 0x1F, 0, 1, 2 }, 5 }, { { 0xF4, 0x20 }, 2 },
        { { 0xF4, 0x35 }, 2 }, { { 0xF4, 0x44 }, 2 }, { { 0xF5 }, 1 },
        { { 0xF6, 1, 2, 3, 4, 5, 6, 7, 8, 9 }, 10 }, { { 0xF7, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 10 },
        { { 0xF8, 0 }, 2 }, { { 0xF9, 1 }, 2 }, { { 0xF9, 2, 1, 2, 3 }, 5 }, { { 0xFA, 6 }, 2 },
        { { 0xFB }, 1 }, { { 0xFC }, 1 }, { { 0xFD }, 1 }, { { 0xFE }, 1 }, { { 0xFF }, 1 },
    };
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        CHECK(TextScript_ElemLen(t[i].b, 12) == t[i].n);
        CHECK(TextScript_ElemLen(t[i].b, t[i].n - 1) == 0); /* cut off */
    }
    /* an argument 0xFF does not end the string */
    {
        uint8_t s[] = { 0x0A, 0xF3, 0xFF, 0x02, 0xFF };
        uint8_t u[] = { 0x0A, 0xF6, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFB, 0xFF, 0x0B };
        uint8_t v[] = { 0x0A, 0xF4, 0x05, 0, 1 }; /* no end */
        CHECK(TextScript_Len(s, sizeof(s), 0) == 5);
        CHECK(TextScript_Len(u, sizeof(u), 0) == 13);
        CHECK(TextScript_Len(v, sizeof(v), 0) == 0);
        CHECK(TextScript_Valid(s, 5));
        CHECK(!TextScript_Valid(u, sizeof(u))); /* bytes after the end */
        CHECK(!TextScript_Valid(v, sizeof(v)));
    }
}

/* a made-up event file: header (3 offsets), 2 entries + terminator, pos/cond blocks, strings */
static int make_table(uint8_t *f, int size, int strings_last) {
    const uint32_t ent = 0x10, pos = ent + 3 * TS_FLAG_STRIDE, cond = pos + 0x18;
    uint32_t txt = strings_last ? cond + 0x30 : 0x100, after = 0x180;
    static const uint8_t s0[] = { 0x0A, 0x0B, 0xFE, 0xF6, 0, 1, 2, 0, 0, 5, 0, 0, 7, 0xFB, 0xFF };
    static const uint8_t s1[] = { 0x0C, 0xF4, 0x20, 0xF3, 0xFF, 0x01, 0xFF };
    static const uint8_t s2[] = { 0x0D, 0xF8, 0x00, 0xFF };

    memset(f, 0, (size_t)size);
    w32(f, ent);
    w32(f + 4, pos);
    w32(f + 8, strings_last ? cond : after);
    f[ent] = 5;                                 /* entry 0: digiId 5, branches 0 and 2 */
    w32(f + ent + TS_FLAG_BRANCHES, txt);
    w32(f + ent + TS_FLAG_BRANCHES + 8, txt + sizeof(s0));
    f[ent + TS_FLAG_STRIDE] = 6;                /* entry 1: branch 0 shares s0, branch 1 = s2 */
    w32(f + ent + TS_FLAG_STRIDE + TS_FLAG_BRANCHES, txt);
    w32(f + ent + TS_FLAG_STRIDE + TS_FLAG_BRANCHES + 4, txt + sizeof(s0) + sizeof(s1) + 1);
    memcpy(f + txt, s0, sizeof(s0));
    memcpy(f + txt + sizeof(s0), s1, sizeof(s1));
    f[txt + sizeof(s0) + sizeof(s1)] = 0xFF;    /* 1 byte padding */
    memcpy(f + txt + sizeof(s0) + sizeof(s1) + 1, s2, sizeof(s2));
    if (!strings_last) {
        memset(f + after, 0x77, 0x20);          /* other data after the strings */
    }
    return (int)txt;
}

static void test_table(void) {
    uint8_t f[0x200], g[0x200];
    uint32_t slots[16], start, end, tail;
    int n, txt = make_table(f, sizeof(f), 1);

    CHECK(TextScript_FlagTable(f, sizeof(f), slots, 16, &n) == 2);
    CHECK(n == 4);
    CHECK(TextScript_Area(f, sizeof(f), slots, n, 3, &start, &end, &tail));
    CHECK(start == (uint32_t)txt);
    CHECK(end == (uint32_t)txt + 15 + 7 + 1 + 4);
    CHECK(tail >= end && tail <= end + 3);

    /* not a table: a branch offset that is no text */
    memcpy(g, f, sizeof(f));
    w32(g + 0x10 + TS_FLAG_BRANCHES + 8, 0x1F0);
    memset(g + 0x1F0, 0xF4, 0x10); /* never terminated */
    CHECK(TextScript_FlagTable(g, sizeof(g), slots, 16, &n) == 0);
    /* not a table: no terminating entry */
    memcpy(g, f, sizeof(f));
    w32(g, sizeof(g) - TS_FLAG_STRIDE);
    memset(g + sizeof(g) - TS_FLAG_STRIDE, 1, TS_FLAG_STRIDE);
    CHECK(TextScript_FlagTable(g, sizeof(g), slots, 16, &n) == 0);

    /* area with unknown bytes inside: refused */
    memcpy(g, f, sizeof(f));
    TextScript_FlagTable(g, sizeof(g), slots, 16, &n);
    g[txt + 15 + 7] = 0x42; /* the padding byte becomes data */
    CHECK(!TextScript_Area(g, sizeof(g), slots, n, 3, &start, &end, &tail));

    /* strings followed by other data: area ok, but no tail */
    make_table(g, sizeof(g), 0);
    TextScript_FlagTable(g, sizeof(g), slots, 16, &n);
    CHECK(TextScript_Area(g, sizeof(g), slots, n, 3, &start, &end, &tail));
    CHECK(tail == sizeof(g));
}

static void test_repack(void) {
    uint8_t f[0x200], keep[0x200];
    uint32_t slots[16], start, end, tail;
    uint8_t *tr[16] = { 0 };
    int tl[16] = { 0 }, n, placed, r;
    /* longer German for branch 0 of entry 0 (same commands), a broken one for entry 1 branch 1 */
    static uint8_t de0[] = { 0x0A, 0x0B, 0x0C, 0x0D, 0xFE, 0xF6, 0, 1, 2, 0, 0, 5, 0, 0, 7, 0xFB, 0x0E, 0xFF };
    static uint8_t bad[] = { 0x0D, 0xF6, 0, 1, 0xFF }; /* F6 cut off */

    make_table(f, sizeof(f), 1);
    memcpy(keep, f, sizeof(f));
    TextScript_FlagTable(f, sizeof(f), slots, 16, &n);
    TextScript_Area(f, sizeof(f), slots, n, 3, &start, &end, &tail);
    tr[0] = de0; tl[0] = sizeof(de0);
    tr[3] = bad; tl[3] = sizeof(bad);
    r = TextScript_Repack(f, sizeof(f), slots, n, start, end, tail, tr, tl, &placed);
    CHECK(r == 1 && placed == 1);
    /* every pointer reaches a valid string; slot 0 the German, slot 3 the old original */
    for (int i = 0; i < n; i++) {
        uint32_t o = r32(f + slots[i]);
        CHECK(TextScript_Len(f, sizeof(f), o) > 0);
    }
    CHECK(memcmp(f + r32(f + slots[0]), de0, sizeof(de0)) == 0);
    CHECK(memcmp(f + r32(f + slots[3]), keep + r32(keep + slots[3]), 4) == 0);
    /* entry 1 branch 0 kept the English s0 (no translation for that slot) */
    CHECK(memcmp(f + r32(f + slots[2]), keep + r32(keep + slots[2]), 15) == 0);
    /* header and entries outside the area untouched (except the pointers) */
    CHECK(memcmp(f, keep, 0x10) == 0);
    CHECK(f[0x10] == 5 && f[0x10 + TS_FLAG_STRIDE] == 6);

    /* no room at all (strings followed by data, translation longer): falls back, file valid */
    make_table(f, sizeof(f), 0);
    memcpy(keep, f, sizeof(f));
    TextScript_FlagTable(f, sizeof(f), slots, 16, &n);
    TextScript_Area(f, sizeof(f), slots, n, 3, &start, &end, &tail);
    memset(tr, 0, sizeof(tr));
    tr[0] = de0; tl[0] = sizeof(de0);
    r = TextScript_Repack(f, sizeof(f), slots, n, start, end, tail, tr, tl, &placed);
    /* German does not fit the area: the original stays, every pointer still valid */
    CHECK(r >= 0);
    for (int i = 0; i < n; i++) {
        CHECK(TextScript_Len(f, sizeof(f), r32(f + slots[i])) > 0);
        CHECK(r32(f + slots[i]) >= start && r32(f + slots[i]) < end);
    }
    CHECK(memcmp(f + 0x180, keep + 0x180, 0x20) == 0); /* data after the area untouched */
}

int main(void) {
    test_lengths();
    test_table();
    test_repack();
    printf("textscript: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
