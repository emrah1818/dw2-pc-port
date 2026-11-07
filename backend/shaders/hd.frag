#version 450

/* HD surface pixels (backend/psxgpu_hw.c): the PS1 shading of backend/psxgpu_hd.c per surface
 * pixel. Textures and CLUTs are read from the 1x VRAM (a storage buffer, two 16-bit pixels per
 * word); the result is a 5-bit colour per channel (stored as 8 bits) with the mask bit in alpha.
 *
 * v_par.x texture: 0-1 mode (4 / 8 / 15 bit), 2 raw, 3 textured, 4-7 page x / 64, 8 page y / 256,
 *         9-14 CLUT x / 16, 15-23 CLUT y
 * v_par.y texture window: mask x / 8, mask y / 8, offset x / 8, offset y / 8 (5 bits each)
 * v_par.z flags: 0 dither, 1 sprite-like (sample at the 1x pixel), 2-3 pass (0 all, 1 opaque
 *         texels only, 2 semi-transparent texels / pixels only), 4-5 abr, 6-7 kind (0 primitive,
 *         1 fill: colour = 5-bit value << 3, 2 VRAM copy: uv = VRAM position), 8 set mask,
 *         9 clamp u to the limits, 10 bilinear (dw2-pc-port texture filtering), 11 2D smoothing
 * v_par.w u limits: min | max << 8; with bit 10 or 11 also v limits: min << 16 | max << 24 */

layout(location = 0) in vec2 v_uv;
layout(location = 1) noperspective in vec3 v_col;
layout(location = 2) flat in uvec4 v_par;
/* dw2-pc-port texture packs (backend/texrep.c): v_rep.y = 0: none. Else x = the level's first word in
 * the Rep buffer, y = width | height << 16 (replacement pixels), z = k (replacement pixels per
 * texel) | n << 5 (block edge of the averaging) | (du + 512) << 8 | (dv + 512) << 18 (page texel ->
 * upload texel) | 1 << 28 (palette index bytes, coloured through the primitive's CLUT: runtime font
 * tiles), w = the primitive's texel box umin | umax << 8 | vmin << 16 | vmax << 24 */
layout(location = 3) flat in uvec4 v_rep;

layout(location = 0) out vec4 o_col;

layout(std430, set = 2, binding = 0) readonly buffer Vram {
    uint px[];
} vram;

layout(std430, set = 2, binding = 1) readonly buffer Rep {
    uint px[];
} rep;

layout(set = 3, binding = 0) uniform Params {
    int scale;
    int ox, oy; /* PR.34: the target's origin in surface pixels (0 for a surface) */
} params;

const int dither_tbl[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uint vram16(int x, int y) {
    uint i = uint((y & 511) * 1024 + (x & 1023));
    return (vram.px[i >> 1] >> ((i & 1u) * 16u)) & 0xFFFFu;
}

ivec3 to5(ivec3 c8, int d) {
    return clamp(c8 + d, 0, 255) >> 3;
}

vec4 out_col(ivec3 c5, bool mask) {
    return vec4(vec3((c5 << 3) | (c5 >> 2)) / 255.0, mask ? 1.0 : 0.0);
}

uint fetch(int u, int v, uint tex, uint win) {
    int mx = int(win & 31u) * 8, my = int((win >> 5) & 31u) * 8;
    int ox = int((win >> 10) & 31u) * 8, oy = int((win >> 15) & 31u) * 8;
    int tx = int((tex >> 4) & 15u) * 64, ty = int((tex >> 8) & 1u) * 256;
    int cx = int((tex >> 9) & 63u) * 16, cy = int((tex >> 15) & 511u);

    u = ((u & ~mx) | (ox & mx)) & 255;
    v = ((v & ~my) | (oy & my)) & 255;
    switch (tex & 3u) {
    case 0u:
        return vram16(cx + int((vram16(tx + (u >> 2), ty + v) >> ((u & 3) * 4)) & 15u), cy);
    case 1u:
        return vram16(cx + int((vram16(tx + (u >> 1), ty + v) >> ((u & 1) * 8)) & 255u), cy);
    default:
        return vram16(tx + u, ty + v);
    }
}

/* dw2-pc-port 2D smoothing: as same2d in backend/psxgpu_hd.c */
bool same2d(uint a, uint b) {
    if (a == 0u || b == 0u) {
        return a == b;
    }
    ivec3 d = abs(ivec3(a & 31u, (a >> 5) & 31u, (a >> 10) & 31u) - ivec3(b & 31u, (b >> 5) & 31u, (b >> 10) & 31u));
    return d.x + d.y + d.z <= 7;
}

uint fetch_box(int u, int v, uint tex, uint win, ivec4 box) {
    return u >= box.x && u <= box.y && v >= box.z && v <= box.w ? fetch(u, v, tex, win) : 0u;
}

/* dw2-pc-port: TexRep_Sample (backend/texrep.c) at (f.x, f.y) = texture page texels, 8.8 fixed
 * point. Returns the PS1 15-bit colour (R | G << 5 | B << 10), or -1 where the replacement is
 * transparent. */
int rep_sample(ivec2 f, uint tex) {
    int k = int(v_rep.z & 31u), n = int((v_rep.z >> 5) & 7u);
    int du = int((v_rep.z >> 8) & 1023u) - 512, dv = int((v_rep.z >> 18) & 1023u) - 512;
    int W = int(v_rep.y & 0xFFFFu), H = int(v_rep.y >> 16), base = int(v_rep.x);
    int umin = int(v_rep.w & 255u), umax = int((v_rep.w >> 8) & 255u);
    int vmin = int((v_rep.w >> 16) & 255u), vmax = int((v_rep.w >> 24) & 255u);
    int hx = (f.x * k) >> 8, hy = (f.y * k) >> 8;
    int x0 = umin * k, x1 = (umax + 1) * k - 1, y0 = vmin * k, y1 = (vmax + 1) * k - 1;

    hx = clamp(hx, x0, x1); /* inside the primitive's texels: no atlas bleeding */
    hy = clamp(hy, y0, y1);
    if ((v_rep.z & 0x10000000u) != 0u) {
        /* a runtime tile: one index per pixel, the CLUT's colour (TexRep_Sample) */
        hx += du * k;
        hy += dv * k;
        if (hx < 0 || hy < 0 || hx >= W || hy >= H) {
            return -1;
        }
        uint a = uint(hy * W + hx);
        uint i = (rep.px[uint(base) + (a >> 2)] >> ((a & 3u) * 8u)) & 255u;
        uint c = i != 0u ? vram16(int((tex >> 9) & 63u) * 16 + int(i), int((tex >> 15) & 511u)) : 0u;
        return c == 0u ? -1 : int(c & 0x7FFFu);
    }
    if (n >= 2) {
        uint r = 0u, g = 0u, b = 0u, cnt = 0u, all = 0u;

        hx -= min(hx - x0, n / 2);
        hy -= min(hy - y0, n / 2);
        for (int dy = 0; dy < n; dy++) {
            for (int dx = 0; dx < n; dx++) {
                int sx = min(hx + dx, x1) + du * k, sy = min(hy + dy, y1) + dv * k;
                if (sx < 0 || sy < 0 || sx >= W || sy >= H) {
                    continue;
                }
                uint p = rep.px[base + sy * W + sx];
                all++;
                if ((p >> 24) < 128u) {
                    continue;
                }
                r += (p >> 16) & 255u;
                g += (p >> 8) & 255u;
                b += p & 255u;
                cnt++;
            }
        }
        if (cnt == 0u || cnt * 2u < all) {
            return -1; /* mostly transparent there */
        }
        return int((r / cnt) >> 3 | ((g / cnt) >> 3) << 5 | ((b / cnt) >> 3) << 10);
    }
    hx += du * k;
    hy += dv * k;
    if (hx < 0 || hy < 0 || hx >= W || hy >= H) {
        return -1;
    }
    uint p = rep.px[base + hy * W + hx];
    if ((p >> 24) < 128u) {
        return -1;
    }
    return int(((p >> 19) & 31u) | ((p >> 11) & 31u) << 5 | ((p >> 3) & 31u) << 10);
}

void main() {
    uint tex = v_par.x, win = v_par.y, fl = v_par.z;
    int S = params.scale;
    ivec2 P = ivec2(gl_FragCoord.xy) + ivec2(params.ox, params.oy);
    vec2 dx = dFdx(v_uv), dy = dFdy(v_uv);
    vec2 uv = v_uv;
    int kind = int((fl >> 6) & 3u);
    int pass = int((fl >> 2) & 3u);
    int abr = int((fl >> 4) & 3u);
    bool mask = (fl & 0x100u) != 0u;
    ivec3 col, c5;
    ivec2 iuv;

    if ((fl & 2u) != 0u && v_rep.y == 0u) {
        /* sprite-like: the texel of the 1x pixel's first surface pixel */
        ivec2 m = P % S;
        uv -= dx * float(m.x) + dy * float(m.y);
    }
    iuv = ivec2(floor(uv + 1.0 / 512.0));
    if (kind == 2) {
        uint c = vram16(iuv.x, iuv.y);
        o_col = out_col(ivec3(c & 31u, (c >> 5) & 31u, (c >> 10) & 31u), (c & 0x8000u) != 0u);
        return;
    }
    col = clamp(ivec3(floor(v_col + 1.0 / 256.0)), 0, 255);
    if (kind == 1) {
        o_col = out_col(col >> 3, false);
        return;
    }
    int d = (fl & 1u) != 0u ? dither_tbl[((P.y / S) & 3) * 4 + ((P.x / S) & 3)] : 0;
    if ((tex & 8u) != 0u && v_rep.y != 0u) {
        /* dw2-pc-port texture pack (rep_texel in backend/psxgpu_hd.c): the replacement colour at the
         * HD pixel's position inside the original texel, with the original texel's mask bit (semi-
         * transparency as in the game); sprite-like pieces keep their sub-texel position too */
        ivec2 f = ivec2(floor(v_uv * 256.0));
        int rc = rep_sample(f, tex);

        if (rc < 0) {
            discard;
        }
        uint t = uint(rc) | (fetch(f.x >> 8, f.y >> 8, tex, win) & 0x8000u);
        if (t == 0u) {
            t = 0x0421u; /* opaque black must not read as transparent */
        }
        bool tsemi = (t & 0x8000u) != 0u;
        if ((pass == 1 && tsemi) || (pass == 2 && !tsemi)) {
            discard;
        }
        c5 = ivec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u);
        if ((tex & 4u) == 0u) {
            c5 = to5((c5 * col) >> 4, d);
        }
        mask = mask || tsemi;
    } else if ((tex & 8u) != 0u && (fl & 0x800u) != 0u) {
        /* dw2-pc-port 2D smoothing (smooth2d_texel in backend/psxgpu_hd.c) */
        ivec2 f = ivec2(floor(uv * 256.0));
        ivec2 b0 = f >> 8;
        ivec2 q = ivec2((f.x & 255) >= 128 ? 1 : -1, (f.y & 255) >= 128 ? 1 : -1);
        vec2 pp = vec2(f & 255) / 256.0 - 0.5;
        ivec4 box = ivec4(int(v_par.w & 255u), int((v_par.w >> 8) & 255u), int((v_par.w >> 16) & 255u),
                          int((v_par.w >> 24) & 255u));
        uint E = fetch_box(b0.x, b0.y, tex, win, box);
        uint Fn = fetch_box(b0.x + q.x, b0.y, tex, win, box), Hn = fetch_box(b0.x, b0.y + q.y, tex, win, box);
        uint Bo = fetch_box(b0.x, b0.y - q.y, tex, win, box), Do = fetch_box(b0.x - q.x, b0.y, tex, win, box);
        uint t = E;

        if (same2d(Fn, Hn) && !same2d(Fn, E) && !same2d(Fn, Bo) && !same2d(Hn, Do) &&
            (q.x > 0 ? pp.x : -pp.x) + (q.y > 0 ? pp.y : -pp.y) > 0.5) {
            t = Fn;
        }
        if (t == 0u) {
            discard;
        }
        bool tsemi = (t & 0x8000u) != 0u;
        if ((pass == 1 && tsemi) || (pass == 2 && !tsemi)) {
            discard;
        }
        c5 = ivec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u);
        if ((tex & 4u) == 0u) {
            c5 = to5((c5 * col) >> 4, d);
        }
        mask = mask || tsemi;
    } else     if ((tex & 8u) != 0u && (fl & 0x400u) != 0u) {
        /* dw2-pc-port bilinear: as hshade_bilinear in backend/psxgpu_hd.c (8.8 weights, UV box
         * clamp, transparent texels carry no colour, semi bit of the heaviest opaque texel) */
        ivec2 f = ivec2(floor(uv * 256.0 + 0.5)) - 128;
        ivec2 b0 = f >> 8;
        ivec2 a = f & 255;
        int umin = int(v_par.w & 255u), umax = int((v_par.w >> 8) & 255u);
        int vmin = int((v_par.w >> 16) & 255u), vmax = int((v_par.w >> 24) & 255u);
        int us0 = clamp(b0.x, umin, umax), us1 = clamp(b0.x + 1, umin, umax);
        int vs0 = clamp(b0.y, vmin, vmax), vs1 = clamp(b0.y + 1, vmin, vmax);
        uint t4[4] = uint[4](fetch(us0, vs0, tex, win), fetch(us1, vs0, tex, win), fetch(us0, vs1, tex, win),
                             fetch(us1, vs1, tex, win));
        int w4[4] = int[4]((256 - a.x) * (256 - a.y), a.x * (256 - a.y), (256 - a.x) * a.y, a.x * a.y);
        int W = 0, best = 0, bestw = -1;
        ivec3 sum = ivec3(0);

        for (int i = 0; i < 4; i++) {
            if (t4[i] == 0u) {
                continue;
            }
            W += w4[i];
            sum += w4[i] * ivec3(t4[i] & 31u, (t4[i] >> 5) & 31u, (t4[i] >> 10) & 31u);
            if (w4[i] > bestw) {
                bestw = w4[i];
                best = i;
            }
        }
        if (W < 32768) {
            discard;
        }
        bool tsemi = (t4[best] & 0x8000u) != 0u;
        if ((pass == 1 && tsemi) || (pass == 2 && !tsemi)) {
            discard;
        }
        /* texel colour x 8, rounded: (sum * 8 + W / 2) / W (sum < 2^21, so sum * 8 fits in 32 bits) */
        ivec3 t8 = (sum * 8 + W / 2) / W;
        if ((tex & 4u) == 0u) {
            c5 = to5((t8 * col) >> 7, d);
        } else {
            c5 = t8 >> 3;
        }
        mask = mask || tsemi;
    } else if ((tex & 8u) != 0u) {
        int u, v, tx, ty, cx, cy;
        uint t;
        int mx = int(win & 31u) * 8, my = int((win >> 5) & 31u) * 8;
        int ox = int((win >> 10) & 31u) * 8, oy = int((win >> 15) & 31u) * 8;

        if ((fl & 0x200u) != 0u) {
            iuv.x = clamp(iuv.x, int(v_par.w & 255u), int((v_par.w >> 8) & 255u));
        }
        u = ((iuv.x & ~mx) | (ox & mx)) & 255;
        v = ((iuv.y & ~my) | (oy & my)) & 255;
        tx = int((tex >> 4) & 15u) * 64;
        ty = int((tex >> 8) & 1u) * 256;
        cx = int((tex >> 9) & 63u) * 16;
        cy = int((tex >> 15) & 511u);
        switch (tex & 3u) {
        case 0u:
            t = vram16(cx + int((vram16(tx + (u >> 2), ty + v) >> ((u & 3) * 4)) & 15u), cy);
            break;
        case 1u:
            t = vram16(cx + int((vram16(tx + (u >> 1), ty + v) >> ((u & 1) * 8)) & 255u), cy);
            break;
        default:
            t = vram16(tx + u, ty + v);
            break;
        }
        if (t == 0u) {
            discard;
        }
        bool tsemi = (t & 0x8000u) != 0u;
        if ((pass == 1 && tsemi) || (pass == 2 && !tsemi)) {
            discard;
        }
        c5 = ivec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u);
        if ((tex & 4u) == 0u) {
            c5 = to5((c5 * col) >> 4, d);
        }
        mask = mask || tsemi;
    } else {
        c5 = to5(col, d);
    }
    if (pass == 2 && abr == 3) {
        c5 >>= 2;
    }
    o_col = out_col(c5, mask);
}
