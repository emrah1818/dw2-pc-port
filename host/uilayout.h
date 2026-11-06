#ifndef DW2_HOST_UILAYOUT_H
#define DW2_HOST_UILAYOUT_H
/* dw2-pc-port: PC-side text layout for the game's windows (host/uilayout.c) */
#ifdef __cplusplus
extern "C" {
#endif
void UiText_Init(void);  /* after the German layer: glyph metrics of both fonts */
int UiText_Modern(void); /* Modern UI on (setting ui_style) */
/* advance of glyph code c (small / big font) when the cell advance is `cell` */
int UiText_Advance(int big, int c, int cell);
int UiText_SpaceAdvance(int big, int cell);
int UiText_TightAdvance(int big, int c, int cell); /* close setting for a line that does not fit */
int UiText_TightInkLeft(int big, int c);
int UiText_InkLeft(int big, int c); /* empty columns left of the glyph (drawn that far left) */

void UiLayout_Flip(void);
void UiLayout_NotePart(const void *part, const void *cells);
const void *UiLayout_PartCells(const void *part, const void *cells);
/* the window (part rectangle of the last frame) a text starting at x, y is in; 0 when none */
int UiLayout_FindWindow(int x, int y, int *left, int *right, int *top, int *bottom);
/* a line of `width` pixels starting at x, y, with the next text of the line at `limit` (0x7FFF:
   none): grows its window (Modern) and returns the spacing scale (16.16, 0x10000 = as measured)
   that makes it fit */
int UiLayout_FitLine(int x, int y, int width, int limit);
/* a column of text boxes (same x, same window) moved right by the layout (Modern) */
int UiLayout_ColumnShift(int x, int y);
void UiLayout_RequestShift(int x, int top, int bottom, int shift); /* boxes at x starting in [top, bottom) */
/* how far right a text starting at x, y may reach: its window's edge, after growing it (Modern) */
int UiLayout_MaxRight(int x, int y);
void UiLayout_Stats(int *lines, int *grown, int *squeezed);
#ifdef __cplusplus
}
#endif
#endif
