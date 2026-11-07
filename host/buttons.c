/* Modern PlayStation input badges. Procedural artwork, no downloaded icon/font assets.
 * Four new cells in verified empty atlas space and a separate empty CLUT bank per colour row.
 * Letters, level glyphs, original palettes and controller mappings remain untouched. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "host/buttons.h"
#include "host/fonts.h"

static int enabled, active[2];
static const uint32_t colors[16] = {
    0, 0xFF102033, 0xFF203249, 0xFFB5CBDD,
    0xFF623044, 0xFFAF4054, 0xFFFF5367, /* circle */
    0xFF3D496C, 0xFF6D7DAF, 0xFF9BAAFA, /* cross */
    0xFF0D575D, 0xFF078F8D, 0xFF00D0C0, /* triangle */
    0xFF594363, 0xFFA4679E, 0xFFEF89C8  /* square */
};
uint32_t Buttons_Color(int index) { return index >= 0 && index < 16 ? colors[index] : 0; }
void Buttons_SetEnabled(int on) { enabled = on != 0; active[0] = active[1] = 0; }
int Buttons_AtlasActive(int big) { return big >= 0 && big < 2 && enabled && active[big]; }
int Buttons_FontBit(int c) {
    static const int bits[] = {13, 14, 12, 15};
    return c >= 0x3E && c <= 0x41 ? bits[c - 0x3E] : -1;
}
int Buttons_SourceCode(int big, int cell) {
    int slot=cell-(big ? 0x80:0xF0);
    return Buttons_AtlasActive(big) && slot>=0 && slot<4 ? 0x3E + slot : -1;
}
int Buttons_FontUV(int big, int c, int *u, int *v) {
    if (!Buttons_AtlasActive(big) || Buttons_FontBit(c)<0 || !u || !v) return 0;
    int cc=(big ? 0x80:0xF0)+c-0x3E;
    *u=cc%(big ? 13:17)*(big ? 9:7);
    *v=cc/(big ? 13:17)*(big ? 14:10);
    return 1;
}
int Buttons_FontClut(int big, int c, int clut) {
    if (!Buttons_AtlasActive(big) || Buttons_FontBit(c) < 0 || (clut & 63) == 63) return clut;
    return (clut & ~63) | ((clut & 63) + 1);
}
static float segment(float x, float y, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(x - ax - t * dx, y - ay - t * dy);
}
static int face_shape(int bit, float x, float y, float r) {
    float a = r * 0.52f, stroke = fmaxf(0.65f, r * 0.15f), d;
    if (bit == 13) return fabsf(hypotf(x, y) - r * 0.44f) < stroke * 0.65f;
    if (bit == 14) return segment(x,y,-a,-a,a,a) < stroke * 0.5f || segment(x,y,-a,a,a,-a) < stroke * 0.5f;
    if (bit == 15) return fabsf(fmaxf(fabsf(x),fabsf(y)) - a) < stroke * 0.5f;
    d = segment(x,y,0,-a*1.15f,-a,a*0.8f);
    d = fminf(d,segment(x,y,-a,a*0.8f,a,a*0.8f));
    d = fminf(d,segment(x,y,a,a*0.8f,0,-a*1.15f));
    return d < stroke * 0.5f;
}
int Buttons_Draw(int bit, int w, int h, uint8_t *out) {
    static const char *labels[12] = {"SELECT","L3","R3","START",NULL,NULL,NULL,NULL,"L2","R2","L1","R1"};
    if (bit < 0 || bit > 15 || !out || w < 3 || h < 3 || w > 256 || h > 256) return 0;
    memset(out,0,(size_t)w*h);
    float r = fminf(w,h) * 0.5f - 0.25f, cx = w * 0.5f, cy = h * 0.5f;
    int base = bit == 13 ? 4 : bit == 14 ? 7 : bit == 12 ? 10 : 13;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        float X=x+0.5f-cx,Y=y+0.5f-cy;
        if (bit >= 12) {
            int covered=0;
            for (int sy=0;sy<4;sy++) for (int sx=0;sx<4;sx++) {
                float px=x+(sx+0.5f)/4-cx,py=y+(sy+0.5f)/4-cy,d=hypotf(px,py);
                covered += d <= r && (d >= r-fmaxf(0.5f,r*0.065f) || face_shape(bit,px,py,r));
            }
            if (hypotf(X,Y)<r) out[y*w+x]=Y < -r*0.4f ? 2 : 1;
            if (covered) out[y*w+x]=(uint8_t)(base+(covered>=11 ? 2 : covered>=5 ? 1 : 0));
        } else if (bit>=4 && bit<=7) {
            /* Neutral directional keycap, preserving the input bit's direction. */
            if (fabsf(X)<w*0.44f && fabsf(Y)<h*0.44f) {
                out[y*w+x]=fabsf(X)>w*0.39f || fabsf(Y)>h*0.39f ? 3 : 1;
                float u=X/r,v=Y/r;
                if (bit==5) { float t=u;u=v;v=-t; }
                if (bit==6) v=-v;
                if (bit==7) { float t=u;u=-v;v=t; }
                if (v>=-0.45f && v<=0.30f && fabsf(u)<(v+0.45f)*0.65f) out[y*w+x]=3;
            }
        } else {
            float dx=fabsf(X)-(w*0.46f-3),dy=fabsf(Y)-(h*0.43f-3);
            float d=hypotf(fmaxf(dx,0),fmaxf(dy,0))+fminf(fmaxf(dx,dy),0)-3;
            if (d<=0) out[y*w+x]=d>=-fmaxf(0.7f,h*0.04f) ? 3 : 1;
        }
    }
    if (bit<12 && labels[bit]) {
        uint8_t mask[256*256];
        int mw=w*3/4,mh=h/2,ink=mw>0 && mh>0 ? Fonts_Mask(FONT_INTERFACE_BOLD,labels[bit],mask,mw,mw,mh):-1;
        int left=(w-(ink>=0 ? ink:mw))/2,top=(h-mh)/2;
        if (ink>=0)
            for (int y=0;y<mh;y++) for (int x=0;x<mw;x++)
                if (mask[y*mw+x]>=96 && left+x<w) out[(y+top)*w+x+left]=3;
    }
    return 1;
}
static int le16(const uint8_t *p) { return p[0] | p[1]<<8; }
static void put16(uint8_t *p, int v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
int Buttons_PatchAtlas(uint8_t *d,int size,int big) {
    const int image=68,cw=big ? 9:7,ch=big ? 14:10,cols=big ? 13:17,gw=big ? 8:7,gh=big ? 13:9;
    if (big<0 || big>1) return 0;
    active[big]=0;
    if (!enabled || !d || size<image+256*64 || d[4]!=0x10 || d[8]!=8 || le16(d+64)!=32 || le16(d+66)!=256) return 0;
    uint8_t palettes[8][32], glyphs[4][9*14];
    int base=le16(d+image+248*64+2),br=base&31,bg=(base>>5)&31,bb=(base>>10)&31;
    int bright=br>bg ? (br>bb ? br:bb):(bg>bb ? bg:bb);
    if (!bright) return 0;
    for (int y=248;y<256;y++) {
        int ink=le16(d+image+y*64+2),r=ink&31,g=(ink>>5)&31,b=(ink>>10)&31;
        int lum=r>g ? (r>b ? r:b):(g>b ? g:b);
        if (lum>bright) lum=bright;
        for (int i=0;i<16;i++) {
            uint32_t c=colors[i];
            int R=((c>>16)&255)*lum/(bright*8),G=((c>>8)&255)*lum/(bright*8),B=(c&255)*lum/(bright*8);
            put16(palettes[y-248]+i*2,i && lum ? ((ink&0x8000)|R|(G<<5)|(B<<10)):0);
        }
    }
    for (int c=0x3E;c<=0x41;c++) {
        uint8_t glyph[8*13];
        Buttons_Draw(Buttons_FontBit(c),gw,gh,glyph);
        for (int y=0;y<ch;y++) for (int x=0;x<cw;x++)
            glyphs[c-0x3E][y*cw+x]=x<gw && y<gh ? glyph[y*gw+x]:0;
    }
    /* Empty slots/banks or our exact previous rendering: do not overwrite conflicting mods. */
    for (int y=248;y<256;y++) {
        int nonzero=0;
        for (int x=32;x<64;x++) nonzero|=d[image+y*64+x];
        if (nonzero && memcmp(d+image+y*64+32,palettes[y-248],32)) return 0;
    }
    for (int c=0x3E;c<=0x41;c++) {
        int cc=(big ? 0x80:0xF0)+c-0x3E,x0=cc%cols*cw,y0=cc/cols*ch;
        int nonzero=0,same=1;
        for (int y=0;y<ch;y++) for (int x=0;x<cw;x++) {
            int gx=x0+x,v=(d[image+(y0+y)*64+gx/2]>>((gx&1)*4))&15;
            nonzero|=v;same&=v==glyphs[c-0x3E][y*cw+x];
        }
        if (nonzero && !same) return 0;
    }
    for (int y=248;y<256;y++) memcpy(d+image+y*64+32,palettes[y-248],32);
    for (int c=0x3E;c<=0x41;c++) {
        int cc=(big ? 0x80:0xF0)+c-0x3E,x0=cc%cols*cw,y0=cc/cols*ch;
        for (int y=0;y<ch;y++) for (int x=0;x<cw;x++) {
            int v=glyphs[c-0x3E][y*cw+x],gx=x0+x,off=image+(y0+y)*64+gx/2,shift=(gx&1)*4;
            d[off]=(uint8_t)((d[off]&~(15<<shift))|(v<<shift));
        }
    }
    active[big]=1;
    return 4;
}

extern void *Host_PakReadFileLayered(int id,int *size);
extern void Host_PakSetLayer(int id,const uint8_t *data,int size);
extern void Fonts_RegisterHd(int big,const uint8_t *data,int size);
void Buttons_ApplyGame(void) {
    for (int big=0;big<2;big++) {
        int size=0,id=big ? 0x110:0x13A;
        uint8_t *d=Host_PakReadFileLayered(id,&size);
        if (Buttons_PatchAtlas(d,size,big)) {
            Host_PakSetLayer(id,d,size);
            Fonts_RegisterHd(big,d,size);
        }
        free(d);
    }
}
