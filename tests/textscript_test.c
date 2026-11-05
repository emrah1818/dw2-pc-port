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

/* a made-up event file in the disc's layout: header (entries, positions, conditions), texts from
 * 12 (4-byte aligned, zero padding), position block, condition block, entries + end entry, zeros */
static const uint8_t s0[] = { 0x0A, 0x0B, 0xFE, 0xF6, 0, 1, 2, 0, 0, 5, 0, 0, 7, 0xFB, 0xFF };      /* 15 */
static const uint8_t s1[] = { 0x0C, 0xF4, 0x20, 0xF3, 0xFF, 0x01, 0xFF };                           /* 7 */
static const uint8_t s2[] = { 0x0D, 0xF8, 0x00, 0xFF };                                             /* 4 */
#define T0 12
#define T1 (T0 + 16)
#define T2 (T1 + 8)
#define POS (T2 + 4)
#define COND (POS + 12)
#define ENT (COND + 0x18)

static void make_table(uint8_t *f, int size) {
    memset(f, 0, (size_t)size);
    w32(f, ENT);
    w32(f + 4, POS);
    w32(f + 8, COND);
    memcpy(f + T0, s0, sizeof(s0));
    memcpy(f + T1, s1, sizeof(s1));
    memcpy(f + T2, s2, sizeof(s2));
    memset(f + POS, 0x11, 12);
    memset(f + COND, 0x22, 0x18);
    f[ENT] = 5;                                         /* entry 0: branches 0 and 2 */
    f[ENT + 5] = 0x33;
    w32(f + ENT + TS_FLAG_BRANCHES, T0);
    w32(f + ENT + TS_FLAG_BRANCHES + 8, T1);
    f[ENT + TS_FLAG_STRIDE] = 6;                        /* entry 1: branch 0 shares s0, branch 1 = s2 */
    w32(f + ENT + TS_FLAG_STRIDE + TS_FLAG_BRANCHES, T0);
    w32(f + ENT + TS_FLAG_STRIDE + TS_FLAG_BRANCHES + 4, T2);
    /* end entry (digiId 0) at ENT + 2*0x2C, zeros after it */
}

static void test_table(void) {
    uint8_t f[0x200], g[0x200];
    uint32_t slots[16];
    int n;
    TextScriptFlagLayout L;

    make_table(f, sizeof(f));
    CHECK(TextScript_FlagTable(f, sizeof(f), slots, 16, &n) == 2);
    CHECK(n == 4);
    CHECK(TextScript_FlagLayout(f, sizeof(f), slots, n, &L));
    CHECK(L.text_end == POS && L.chunk_start == POS && L.chunk_end == ENT + 3 * TS_FLAG_STRIDE);

    /* not a table: a branch offset that is no text */
    memcpy(g, f, sizeof(f));
    w32(g + ENT + TS_FLAG_BRANCHES + 8, POS); /* points into the position block */
    CHECK(TextScript_FlagTable(g, sizeof(g), slots, 16, &n) == 0);
    /* not a table: no end entry */
    memcpy(g, f, sizeof(f));
    w32(g, sizeof(g) - TS_FLAG_STRIDE);
    memset(g + sizeof(g) - TS_FLAG_STRIDE, 1, TS_FLAG_STRIDE);
    CHECK(TextScript_FlagTable(g, sizeof(g), slots, 16, &n) == 0);
    /* layout refused: unknown bytes between the texts, or data behind the entries */
    memcpy(g, f, sizeof(f));
    TextScript_FlagTable(g, sizeof(g), slots, 16, &n);
    g[T0 + 15] = 0x42;
    CHECK(!TextScript_FlagLayout(g, sizeof(g), slots, n, &L));
    memcpy(g, f, sizeof(f));
    g[sizeof(g) - 1] = 1;
    CHECK(!TextScript_FlagLayout(g, sizeof(g), slots, n, &L));
    /* layout refused: blocks in another order */
    memcpy(g, f, sizeof(f));
    w32(g + 4, COND);
    w32(g + 8, POS);
    CHECK(!TextScript_FlagLayout(g, sizeof(g), slots, n, &L));
}

static void check_file(const uint8_t *f, int size, const uint8_t *keep, int ents) {
    uint32_t h0 = r32(f), h1 = r32(f + 4), h2 = r32(f + 8), slots[16];
    int n;
    /* blocks moved intact, header follows them, every branch reaches a valid string */
    CHECK(h1 % 4 == 0 && h2 - h1 == COND - POS && h0 - h2 == ENT - COND);
    CHECK(memcmp(f + h1, keep + POS, COND - POS) == 0);
    CHECK(memcmp(f + h2, keep + COND, ENT - COND) == 0);
    CHECK(f[h0] == 5 && f[h0 + 5] == 0x33 && f[h0 + TS_FLAG_STRIDE] == 6);
    CHECK(TextScript_FlagTable(f, size, slots, 16, &n) == ents);
    for (int i = 0; i < n; i++) CHECK(TextScript_Len(f, size, r32(f + slots[i])) > 0);
}

static void test_repack(void) {
    uint8_t f[0x200], keep[0x200];
    uint32_t slots[16];
    uint8_t *tr[16] = { 0 };
    int tl[16] = { 0 }, n, placed, r;
    TextScriptFlagLayout L;
    /* German for entry 0 branch 0 (longer, same commands); a broken one for entry 1 branch 1 */
    static uint8_t de0[] = { 0x0A, 0x0B, 0x0C, 0x0D, 0xFE, 0xF6, 0, 1, 2, 0, 0, 5, 0, 0, 7, 0xFB, 0x0E, 0x0F, 0x10, 0x11, 0xFF };
    static uint8_t bad[] = { 0x0D, 0xF6, 0, 1, 0xFF }; /* F6 cut off */
    static uint8_t big[0x1F0];

    make_table(f, sizeof(f));
    memcpy(keep, f, sizeof(f));
    TextScript_FlagTable(f, sizeof(f), slots, 16, &n);
    TextScript_FlagLayout(f, sizeof(f), slots, n, &L);
    tr[0] = de0; tl[0] = sizeof(de0);
    tr[3] = bad; tl[3] = sizeof(bad);
    r = TextScript_RepackFlag(f, sizeof(f), slots, n, &L, tr, tl, &placed);
    CHECK(r == 1 && placed == 1);
    check_file(f, sizeof(f), keep, 2);
    CHECK(memcmp(f + r32(f + ENT + (r32(f) - ENT) + TS_FLAG_BRANCHES), de0, sizeof(de0)) == 0);
    /* entry 1 branch 0 still English s0, entry 1 branch 1 still s2 (bad translation refused) */
    CHECK(memcmp(f + r32(f + r32(f) + TS_FLAG_STRIDE + TS_FLAG_BRANCHES), s0, sizeof(s0)) == 0);
    CHECK(memcmp(f + r32(f + r32(f) + TS_FLAG_STRIDE + TS_FLAG_BRANCHES + 4), s2, sizeof(s2)) == 0);
    CHECK(r32(f) > ENT); /* the blocks moved back: the longer text needed room */

    /* shorter German: the blocks move forward (a "negative" shift) */
    {
        static uint8_t de_short[] = { 0x0A, 0xF6, 0, 1, 2, 0, 0, 5, 0, 0, 7, 0xFF }; /* 12 bytes, s0 needs 16 */
        make_table(f, sizeof(f));
        TextScript_FlagTable(f, sizeof(f), slots, 16, &n);
        TextScript_FlagLayout(f, sizeof(f), slots, n, &L);
        memset(tr, 0, sizeof(tr));
        tr[0] = de_short; tl[0] = sizeof(de_short);
        tr[2] = de_short; tl[2] = sizeof(de_short);
        r = TextScript_RepackFlag(f, sizeof(f), slots, n, &L, tr, tl, &placed);
        CHECK(r == 0 && placed == 2);
        CHECK(r32(f) < ENT && r32(f + 4) < POS);
        check_file(f, sizeof(f), keep, 2);
    }

    /* a translation larger than the whole file: falls back, file still valid and unchanged */
    make_table(f, sizeof(f));
    TextScript_FlagTable(f, sizeof(f), slots, 16, &n);
    TextScript_FlagLayout(f, sizeof(f), slots, n, &L);
    memset(big, 0x0A, sizeof(big));
    big[sizeof(big) - 1] = 0xFF;
    memset(tr, 0, sizeof(tr));
    tr[1] = big; tl[1] = sizeof(big);
    r = TextScript_RepackFlag(f, sizeof(f), slots, n, &L, tr, tl, &placed);
    CHECK(r == 1 && placed == 0);
    CHECK(memcmp(f, keep, sizeof(f)) == 0); /* the same layout again, byte for byte */
}

int main(void) {
    test_lengths();
    test_table();
    test_repack();
    printf("textscript: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
