/* dw2-pc-port: byte layout of the game's text and event-script strings (src/main/text.c,
 * Text_DrawAll), the event tables that point at dialog texts (src/main/flagtable.c) and a safe
 * re-layout of string areas. No SDL, no game data: tested on its own (tests/textscript_test.c). */
#ifndef HOST_TEXTSCRIPT_H
#define HOST_TEXTSCRIPT_H

#include <stdint.h>

/* Length in bytes of the element (glyph or command with its arguments) at d, avail bytes readable.
 * 0xFF (end) counts 1. Returns 0 when the element is cut off by the end of the data. */
int TextScript_ElemLen(const uint8_t *d, int avail);

/* Length of the string at o including its 0xFF, walking command arguments (an argument may be
 * 0xFF, e.g. F3 FF xx). Returns 0 when no end is found inside the data. */
int TextScript_Len(const uint8_t *d, int size, uint32_t o);

/* 1 when s[0..len) is a whole number of elements and only its last byte is the end 0xFF. */
int TextScript_Valid(const uint8_t *s, int len);

/* Event (flag) table, Flag_SelectBranch: file header u32 offsets; entries of 0x2C bytes at
 * header[0], ending with digiId 0; each entry has 6 text offsets (relative to the file start)
 * at +0x14. Writes the positions of all non-zero text offsets into slots. Returns the number of
 * entries, or 0 when the file is not such a table (every offset must hold a valid text). */
#define TS_FLAG_STRIDE 0x2C
#define TS_FLAG_BRANCHES 0x14
int TextScript_FlagTable(const uint8_t *d, int size, uint32_t *slots, int max, int *nslots);

/* The string area of a set of text pointers: [*start, *end) must be covered completely by the
 * strings they point at (gaps of at most 3 bytes of 0x00/0xFF padding), and no header offset
 * (the first `hdr_words` u32 of the file) may point inside it. *tail is the first free byte after
 * the area when the area is the last thing in the file (all zeros after it), else size (no tail).
 * Returns 1 when the area is understood, 0 otherwise (then nothing may be moved). */
int TextScript_Area(const uint8_t *d, int size, const uint32_t *slots, int n, int hdr_words,
                    uint32_t *start, uint32_t *end, uint32_t *tail);

/* Lays the strings of `slots` out again in [start, end) and then [tail, size): trans[i] (tlen[i]
 * bytes, NULL = keep the original). Equal strings share space. A translation that does not fit
 * falls back to the original string; if even that does not fit, nothing is changed and -1 is
 * returned. Otherwise the file is rewritten and the number of translations that fell back is
 * returned (*placed = translations used). */
int TextScript_Repack(uint8_t *d, int size, const uint32_t *slots, int n, uint32_t start, uint32_t end,
                      uint32_t tail, uint8_t *const *trans, const int *tlen, int *placed);

#endif
