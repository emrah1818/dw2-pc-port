#version 450

/* The filter pass of the HD picture: the rect origin, size of the source texture into the
 * target's top-left corner. Per pixel, in this order:
 *
 * 1. dw2-pc-port anti-aliasing (par.fxaa): where the luma of the 3x3 neighbourhood has an edge,
 *    the pixel becomes a blend of samples along that edge (fast approximate anti-aliasing after
 *    T. Lottes' FXAA; bilinear samples taken with texelFetch, so the rect's edges clamp).
 * 2. PR.2b contrast adaptive sharpening (par.sharpness >= 0; the formula of AMD FidelityFX CAS,
 *    no scaling). Per channel: the 3x3 neighbourhood's minimum and maximum (cross plus corners)
 *    give how much room the pixel has before it would clip; the sharpening weight shrinks where
 *    that room is small, so edges get crisper without halos. The weights come from the
 *    unfiltered neighbours; the centre is the anti-aliased pixel of step 1.
 * 3. dw2-pc-port colour: contrast around mid grey, brightness, saturation, gamma.
 *
 * Alpha (the PS1 mask bit) is kept. With fxaa off and neutral colour values this is the plain
 * sharpening. The same pass runs on the CPU for the software renderer (backend/post.c,
 * backend/sharpen.c). */

layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform sampler2D src;

layout(set = 3, binding = 0) uniform Params {
    ivec2 origin;
    ivec2 size;
    float sharpness;  /* 0..1, below 0: no sharpening */
    float fxaa;       /* above 0.5: on */
    float brightness; /* added: -0.25..0.25 */
    float contrast;   /* factor: 0.5..1.5 */
    float saturation; /* factor: 0..2 */
    float inv_gamma;  /* 1 / gamma */
} par;

const vec3 LUMA = vec3(0.299, 0.587, 0.114);

vec4 tap(ivec2 p, int dx, int dy) {
    ivec2 q = clamp(p + ivec2(dx, dy), ivec2(0), par.size - 1);
    return texelFetch(src, par.origin + q, 0);
}

/* the picture at pixel p plus off (in pixels), bilinear */
vec3 bil(ivec2 p, vec2 off) {
    vec2 f = floor(off);
    vec2 t = off - f;
    ivec2 i = ivec2(f);
    vec3 a = tap(p, i.x, i.y).rgb, b = tap(p, i.x + 1, i.y).rgb;
    vec3 c = tap(p, i.x, i.y + 1).rgb, d = tap(p, i.x + 1, i.y + 1).rgb;

    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 a = tap(p, -1, -1).rgb, b = tap(p, 0, -1).rgb, c = tap(p, 1, -1).rgb;
    vec3 d = tap(p, -1, 0).rgb;
    vec4 e4 = tap(p, 0, 0);
    vec3 e = e4.rgb, f = tap(p, 1, 0).rgb;
    vec3 g = tap(p, -1, 1).rgb, h = tap(p, 0, 1).rgb, i = tap(p, 1, 1).rgb;
    vec3 o = e;

    if (par.fxaa > 0.5) {
        float la = dot(a, LUMA), lc = dot(c, LUMA), lg = dot(g, LUMA), li = dot(i, LUMA);
        float lb = dot(b, LUMA), ld = dot(d, LUMA), le = dot(e, LUMA), lf = dot(f, LUMA), lh = dot(h, LUMA);
        float lmin = min(le, min(min(lb, lh), min(ld, lf)));
        float lmax = max(le, max(max(lb, lh), max(ld, lf)));

        if (lmax - lmin >= max(0.0312, lmax * 0.125)) {
            vec2 dir = vec2(-((la + lc) - (lg + li)), (la + lg) - (lc + li));
            float red = max((la + lc + lg + li) * 0.03125, 0.0078125);
            float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);
            dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0));
            vec3 ra = 0.5 * (bil(p, dir * (1.0 / 3.0 - 0.5)) + bil(p, dir * (2.0 / 3.0 - 0.5)));
            vec3 rb = ra * 0.5 + 0.25 * (bil(p, dir * -0.5) + bil(p, dir * 0.5));
            float lr = dot(rb, LUMA);

            o = lr < lmin || lr > lmax ? ra : rb;
        }
    }
    if (par.sharpness >= 0.0) {
        vec3 mn = min(min(min(d, e), min(f, b)), h);
        vec3 mn2 = min(mn, min(min(a, c), min(g, i)));
        vec3 mx = max(max(max(d, e), max(f, b)), h);
        vec3 mx2 = max(mx, max(max(a, c), max(g, i)));
        mn += mn2;
        mx += mx2;

        vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, 1.0 / 512.0), 0.0, 1.0));
        vec3 w = amp * (-1.0 / mix(8.0, 5.0, par.sharpness));
        o = (o + w * (b + d + f + h)) / (1.0 + 4.0 * w);
    }
    o = clamp(o, 0.0, 1.0);
    if (par.contrast != 1.0 || par.brightness != 0.0 || par.saturation != 1.0 || par.inv_gamma != 1.0) {
        o = (o - 0.5) * par.contrast + 0.5 + par.brightness;
        o = mix(vec3(dot(o, LUMA)), o, par.saturation);
        o = pow(clamp(o, 0.0, 1.0), vec3(par.inv_gamma));
    }
    o_col = vec4(o, e4.a);
}
