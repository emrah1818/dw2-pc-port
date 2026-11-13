#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "backend/psxgpu_hd.h"

/* dw2-pc-port: the filter pass of the HD picture on the CPU (software renderer), the same steps
 * and formulas as backend/shaders/sharpen.frag: anti-aliasing, contrast adaptive sharpening with
 * its weights from the unfiltered neighbours, colour (contrast, brightness, saturation, gamma).
 * Nothing but sharpening wanted: backend/sharpen.c as before (its SSE2 code, the same bytes as
 * in earlier versions). Rows are split between the HD drawing threads (PsxHd_Parallel). Edges
 * clamp, the 4th byte is copied. */

typedef struct {
    const uint32_t *in;
    uint32_t *out;
    int w, h;
    int fxaa, sharp, colour;
    float peak, brightness, contrast, saturation, inv_gamma;
} Job;

static inline float minf(float a, float b) {
    return a < b ? a : b;
}

static inline float maxf(float a, float b) {
    return a > b ? a : b;
}

static inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

static inline void rgb(const Job *j, int x, int y, float *c) {
    uint32_t p;

    x = x < 0 ? 0 : x >= j->w ? j->w - 1 : x;
    y = y < 0 ? 0 : y >= j->h ? j->h - 1 : y;
    p = j->in[(size_t)y * j->w + x];
    c[0] = (float)((p >> 16) & 255) * (1.0f / 255.0f);
    c[1] = (float)((p >> 8) & 255) * (1.0f / 255.0f);
    c[2] = (float)(p & 255) * (1.0f / 255.0f);
}

static inline float luma(const float *c) {
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

/* the picture at pixel (x, y) plus (ox, oy) pixels, bilinear */
static void bil(const Job *j, int x, int y, float ox, float oy, float *o) {
    float fx = floorf(ox), fy = floorf(oy), tx = ox - fx, ty = oy - fy;
    int ix = x + (int)fx, iy = y + (int)fy, k;
    float a[3], b[3], c[3], d[3];

    rgb(j, ix, iy, a);
    rgb(j, ix + 1, iy, b);
    rgb(j, ix, iy + 1, c);
    rgb(j, ix + 1, iy + 1, d);
    for (k = 0; k < 3; k++) {
        float top = a[k] + (b[k] - a[k]) * tx, bot = c[k] + (d[k] - c[k]) * tx;

        o[k] = top + (bot - top) * ty;
    }
}

static uint32_t pixel(const Job *j, int x, int y) {
    float n[9][3], o[3];
    int k, q;

    for (k = 0; k < 9; k++) {
        rgb(j, x + k % 3 - 1, y + k / 3 - 1, n[k]); /* a b c / d e f / g h i */
    }
    o[0] = n[4][0];
    o[1] = n[4][1];
    o[2] = n[4][2];
    if (j->fxaa) {
        float la = luma(n[0]), lb = luma(n[1]), lc = luma(n[2]), ld = luma(n[3]), le = luma(n[4]);
        float lf = luma(n[5]), lg = luma(n[6]), lh = luma(n[7]), li = luma(n[8]);
        float lmin = minf(le, minf(minf(lb, lh), minf(ld, lf)));
        float lmax = maxf(le, maxf(maxf(lb, lh), maxf(ld, lf)));

        if (lmax - lmin >= maxf(0.0312f, lmax * 0.125f)) {
            float dx = -((la + lc) - (lg + li)), dy = (la + lg) - (lc + li);
            float red = maxf((la + lc + lg + li) * 0.03125f, 0.0078125f);
            float rcp = 1.0f / (minf(fabsf(dx), fabsf(dy)) + red);
            float s1[3], s2[3], s3[3], s4[3], ra[3], rb[3], lr;

            dx = maxf(-8.0f, minf(8.0f, dx * rcp));
            dy = maxf(-8.0f, minf(8.0f, dy * rcp));
            bil(j, x, y, dx * (1.0f / 3.0f - 0.5f), dy * (1.0f / 3.0f - 0.5f), s1);
            bil(j, x, y, dx * (2.0f / 3.0f - 0.5f), dy * (2.0f / 3.0f - 0.5f), s2);
            bil(j, x, y, dx * -0.5f, dy * -0.5f, s3);
            bil(j, x, y, dx * 0.5f, dy * 0.5f, s4);
            for (k = 0; k < 3; k++) {
                ra[k] = 0.5f * (s1[k] + s2[k]);
                rb[k] = ra[k] * 0.5f + 0.25f * (s3[k] + s4[k]);
            }
            lr = luma(rb);
            for (k = 0; k < 3; k++) {
                o[k] = lr < lmin || lr > lmax ? ra[k] : rb[k];
            }
        }
    }
    if (j->sharp) {
        for (k = 0; k < 3; k++) {
            float a = n[0][k], b = n[1][k], c = n[2][k], d = n[3][k], e = n[4][k], f = n[5][k], g = n[6][k], h = n[7][k], i = n[8][k];
            float cmn = minf(minf(minf(b, d), minf(e, f)), h), cmx = maxf(maxf(maxf(b, d), maxf(e, f)), h);
            float mn = cmn + minf(cmn, minf(minf(a, c), minf(g, i)));
            float mx = cmx + maxf(cmx, maxf(maxf(a, c), maxf(g, i)));
            float r = minf(mn, 2.0f - mx) / maxf(mx, 1.0f / 512.0f);
            float w = sqrtf(clamp01(r)) * j->peak;

            o[k] = (o[k] + w * (b + d + f + h)) / (1.0f + 4.0f * w);
        }
    }
    for (k = 0; k < 3; k++) {
        o[k] = clamp01(o[k]);
    }
    if (j->colour) {
        float l;

        for (k = 0; k < 3; k++) {
            o[k] = (o[k] - 0.5f) * j->contrast + 0.5f + j->brightness;
        }
        l = luma(o);
        for (k = 0; k < 3; k++) {
            o[k] = clamp01(l + (o[k] - l) * j->saturation);
            if (j->inv_gamma != 1.0f) {
                o[k] = powf(o[k], j->inv_gamma);
            }
        }
    }
    q = (int)(o[0] * 255.0f + 0.5f) << 16 | (int)(o[1] * 255.0f + 0.5f) << 8 | (int)(o[2] * 255.0f + 0.5f);
    return (j->in[(size_t)y * j->w + x] & 0xFF000000u) | (uint32_t)q;
}

static void run(int part, int parts, void *arg) {
    const Job *j = arg;
    int y0 = j->h * part / parts, y1 = j->h * (part + 1) / parts;
    int x, y;

    for (y = y0; y < y1; y++) {
        uint32_t *o = j->out + (size_t)y * j->w;

        for (x = 0; x < j->w; x++) {
            o[x] = pixel(j, x, y);
        }
    }
}

void PsxPost_Cpu(const uint32_t *in, uint32_t *out, int w, int h, const PsxHdPost *p) {
    Job j = { in, out, w, h, 0, 0, 0, 0, 0, 1, 1, 1 };

    if (PsxHdPost_Plain(p)) {
        PsxSharpen_Cpu(in, out, w, h, p->sharpen);
        return;
    }
    j.fxaa = p->fxaa;
    j.sharp = p->sharpen > 0;
    j.peak = -1.0f / (8.0f + (5.0f - 8.0f) * (p->sharpen / 100.0f));
    j.brightness = (p->brightness - 50) / 200.0f;
    j.contrast = p->contrast / 100.0f;
    j.saturation = p->saturation / 100.0f;
    j.inv_gamma = 100.0f / p->gamma;
    j.colour = p->brightness != 50 || p->contrast != 100 || p->saturation != 100 || p->gamma != 100;
    PsxHd_Parallel(run, &j);
}
