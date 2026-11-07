#ifndef HOST_BUTTONS_H
#define HOST_BUTTONS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native PS1 pad bit numbers, shared with Settings_ButtonName. No binding changes. */
void Buttons_SetEnabled(int on); /* game thread, before rebuilding language/font layers */
int Buttons_FontBit(int code); /* only verified font codes 3E..41, -1 otherwise */
int Buttons_AtlasActive(int big);
int Buttons_FontClut(int big, int code, int clut);
int Buttons_FontUV(int big, int code, int *u, int *v);
int Buttons_SourceCode(int big, int cell);
int Buttons_PatchAtlas(uint8_t *data, int size, int big);
void Buttons_ApplyGame(void); /* prepare hidden cells even when booting in Classic */
/* Original, procedural artwork: indexed pixels and its fixed ARGB palette. */
int Buttons_Draw(int bit, int width, int height, uint8_t *out);
uint32_t Buttons_Color(int index);
#ifdef __cplusplus
}
#endif
#endif
