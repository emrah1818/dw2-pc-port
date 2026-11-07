#ifndef DW2_HOST_FONTS_H
#define DW2_HOST_FONTS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { FONT_INTERFACE,
       FONT_DISPLAY,
       FONT_TELEMETRY,
       FONT_INTERFACE_BOLD, /* Exo 2 Bold */
       FONT_DISPLAY_HEAVY,  /* Orbitron Black */
       FONT_COUNT };
/* Embedded, licensed fonts; no files from the player's disc are embedded. */
const unsigned char* Fonts_Data(int role, int* size);
int                  Fonts_Init(void);
/* UTF-8 alpha mask, aspect ratio preserved by reducing the font size to fit.
 * Returns ink width, or -1 for unsupported text/invalid bounds. No substitution. */
int  Fonts_Mask(int role, const char* text, uint8_t* dst, int stride, int width, int height);
int  Fonts_GameCodepoint(int code);
int  Fonts_Cell(int role, int codepoint, uint8_t* out, int width, int height);
/* dw2-pc-port: 0 = the game keeps its original pictures and fonts (Classic UI in English: bit-exact
 * to the PS1, tests/referenz_md5.txt). Set by Lang_Apply before the layers are built. */
void Fonts_SetEnabled(int on);
int  Fonts_Enabled(void);
int  Fonts_DevBitmap(uint8_t* out, int size);
int  Fonts_PatchGameFont(uint8_t* data, int size, int big);
/* the game atlas glyphs (version 2): metrics in 1/256 pixel of the 1x screen, the glyph of code c
   at k times the cell as coverage, body and the original shadow as palette indices */
int  Fonts_GameAdvance(int big, int code);
int  Fonts_GameSpace(int big);
int  Fonts_GameKern(int big, int a, int b);
int  Fonts_GameOrigin(int big, int code);
int  Fonts_GameGlyph(int big, int code, int k, uint8_t* out);
int  Fonts_Digit(int digit, uint8_t* out, int w, int h);
int  Fonts_GameWord(const char* text, int cells, int k, uint8_t* out);
void Fonts_SetAtlasWord(int code, int cells, const char* text);
void Fonts_GameShadow(int big, const uint8_t* cov, int w, int h, int thresh, int s, uint8_t* idx, int stride);
void Fonts_ApplyGame(void);
/* Numeric sprite frames are identified by the game's numeric setter, never by
 * guessing whether a texture looks like text. Applied on the game thread. */
void Fonts_RegisterNumber(int file_id);
void Fonts_NumberCell(int file_id, const void* cell, int tpage, int uoffset, int clut);
#ifdef __cplusplus
}
#endif
#endif
