#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "backend/psxgpu.h"
#include "backend/psxgpu_hd_int.h"
#include "backend/psxgpu_hw_spv.h"
#include "backend/texrep.h"

/* GPU renderer for the HD surfaces (PG.10b), the DuckStation GPU_HW model on SDL_GPU (Vulkan,
 * SPIR-V from backend/shaders/). The queue of backend/psxgpu_hd.c is turned into triangles: each
 * surface is an RGBA8 render texture ((w + 2m) S x h S, alpha = mask bit), the fragment shader
 * shades every surface pixel by the PS1 rules with texels and CLUTs read from a copy of the 1x
 * VRAM (a storage buffer; rows changed since the last batch are uploaded first). Semi-
 * transparency by blend states: abr 0 constant 0.5 + 0.5, 1 add, 2 reverse subtract, 3 add (the
 * shader quarters the colour); a textured semi-transparent primitive is drawn twice (opaque texels
 * without blending, then semi-transparent texels blended), so each pixel is written once as on
 * the PS1. Positions: a surface pixel X samples coordinate X (as in the software HD rasterizer),
 * so vertices go to (p - origin) * S + 0.5 in framebuffer pixels.
 *
 * PR.34 smooth frames: the vertex building and submit state is per thread (Ctx): the game thread
 * draws the HD surfaces (game_ctx), the window thread draws the smooth pictures (win_ctx,
 * PsxHw_Replay: a logged frame drawn again into a picture texture whose origin is a point of the
 * surface, positions blended between the frame before and the frame by alpha). Pipelines, the
 * sharpen pipeline and the device are shared (SDL_GPU command buffers per thread). */

typedef struct {
    float x, y, w;
    float u, v;
    uint8_t r, g, b, a;
    uint32_t par[4];
    uint32_t rep[4]; /* dw2-pc-port texture pack (hd.frag v_rep), rep[1] = 0: none */
} Vert;

typedef struct {
    PsxHdSurf *s;
    int pipe; /* 0 opaque, 1 + abr blended */
    SDL_Rect scissor;
    Uint32 first, count;
} Draw;

#define VRAM_BYTES (PSXGPU_VRAM_W * PSXGPU_VRAM_H * 2)
#define FENCES 8 /* PR.36: submissions in flight per thread before a submit waits */
#define ROW_BYTES (PSXGPU_VRAM_W * 2)

enum { F_DITHER = 1, F_BLOCK = 2, F_CLAMP = 0x200, F_SETMASK = 0x100, F_FILTER = 0x400, F_SMOOTH2D = 0x800 };
#define F_PASS(p) ((uint32_t)(p) << 2)
#define F_ABR(a) ((uint32_t)(a) << 4)
#define F_KIND(k) ((uint32_t)(k) << 6)

typedef struct {
    Vert *verts;
    int nverts, verts_cap;
    Draw *draws;
    int ndraws, draws_cap;
    SDL_GPUBuffer *vbuf;
    SDL_GPUTransferBuffer *vtb;
    Uint32 vcap; /* bytes of vbuf / vtb */
    SDL_GPUFence *fences[FENCES]; /* PR.36: submissions not known done, oldest first */
    int nfences;
    int ox, oy;  /* PR.34: the target's origin in surface pixels */
    float alpha; /* PR.34: blend of logged positions, 1 = as drawn */
} Ctx;

static SDL_GPUDevice *dev;
static SDL_GPUGraphicsPipeline *pipes[5];
static SDL_GPUBuffer *vram_buf;
static SDL_GPUTransferBuffer *vram_tb;
static SDL_GPUTransferBuffer *read_tb;
static Uint32 read_cap;
static SDL_GPUGraphicsPipeline *sharpen_pipe; /* PR.2b */
static SDL_GPUSampler *sharpen_sampler;
static int dirty_y0, dirty_y1 = PSXGPU_VRAM_H; /* VRAM rows to upload before the next batch */
static Ctx game_ctx = { .alpha = 1.0f }, win_ctx = { .alpha = 1.0f };

/* dw2-pc-port texture packs on the GPU: see rep_lookup */
#define REP_BYTES (128u << 20)
#define REP_FRAME_BYTES (32u << 20)
#define REP_TABLE 8192

typedef struct {
    const uint32_t *px;
    uint32_t off; /* words */
    uint32_t words;
} RepEnt;

typedef struct {
    const uint32_t *px;
    uint32_t off, bytes;
} RepUp;

static SDL_GPUBuffer *rep_buf, *rep_dummy;
static SDL_GPUTransferBuffer *rep_tb; /* rep_upload */
static uint32_t rep_tb_size;
static RepEnt rep_tab[REP_TABLE];
static int rep_count, rep_dead, rep_reset_req, rep_full_logged;
static uint32_t rep_top; /* bytes used */
static RepUp rep_up[256];
static int rep_nup;
static uint32_t rep_frame;


static void drain_ctx(Ctx *x);

static SDL_GPUShader *make_shader(const uint32_t *code, size_t size, SDL_GPUShaderStage stage, int storage) {
    SDL_GPUShaderCreateInfo si;

    SDL_zero(si);
    si.code = (const Uint8 *)code;
    si.code_size = size;
    si.entrypoint = "main";
    si.format = SDL_GPU_SHADERFORMAT_SPIRV;
    si.stage = stage;
    si.num_storage_buffers = (Uint32)storage;
    si.num_uniform_buffers = 1;
    return SDL_CreateGPUShader(dev, &si);
}

/* PR.2b: the sharpen pipeline (full-screen triangle, the source texture through a sampler). */
static int init_sharpen(void) {
    SDL_GPUShaderCreateInfo si;
    SDL_GPUShader *vs, *fs;
    SDL_GPUColorTargetDescription ctd;
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_GPUSamplerCreateInfo smp;

    SDL_zero(si);
    si.code = (const Uint8 *)spv_sharpen_vert;
    si.code_size = sizeof(spv_sharpen_vert);
    si.entrypoint = "main";
    si.format = SDL_GPU_SHADERFORMAT_SPIRV;
    si.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vs = SDL_CreateGPUShader(dev, &si);
    si.code = (const Uint8 *)spv_sharpen_frag;
    si.code_size = sizeof(spv_sharpen_frag);
    si.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    si.num_samplers = 1;
    si.num_uniform_buffers = 1;
    fs = SDL_CreateGPUShader(dev, &si);
    if (vs == NULL || fs == NULL) {
        printf("[gpu] GPU renderer: sharpen shaders failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_zero(ctd);
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_zero(pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.num_color_targets = 1;
    sharpen_pipe = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    SDL_ReleaseGPUShader(dev, vs);
    SDL_ReleaseGPUShader(dev, fs);
    SDL_zero(smp);
    smp.min_filter = SDL_GPU_FILTER_NEAREST;
    smp.mag_filter = SDL_GPU_FILTER_NEAREST;
    smp.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    smp.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    smp.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    smp.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sharpen_sampler = SDL_CreateGPUSampler(dev, &smp);
    if (sharpen_pipe == NULL || sharpen_sampler == NULL) {
        printf("[gpu] GPU renderer: sharpen pipeline failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
}

int PsxHw_Init(void *device) {
    SDL_GPUShader *vs, *fs;
    SDL_GPUVertexBufferDescription vbd;
    SDL_GPUVertexAttribute va[5];
    SDL_GPUColorTargetDescription ctd;
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_GPUBufferCreateInfo bi;
    SDL_GPUTransferBufferCreateInfo ti;
    int i;

    dev = device;
    vs = make_shader(spv_hd_vert, sizeof(spv_hd_vert), SDL_GPU_SHADERSTAGE_VERTEX, 0);
    fs = make_shader(spv_hd_frag, sizeof(spv_hd_frag), SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    if (vs == NULL || fs == NULL) {
        printf("[gpu] GPU renderer: shaders failed: %s\n", SDL_GetError());
        dev = NULL;
        return 0;
    }
    SDL_zero(vbd);
    vbd.slot = 0;
    vbd.pitch = sizeof(Vert);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(va);
    va[0].location = 0;
    va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    va[0].offset = 0;
    va[1].location = 1;
    va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    va[1].offset = 12;
    va[2].location = 2;
    va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    va[2].offset = 20;
    va[3].location = 3;
    va[3].format = SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
    va[3].offset = 24;
    va[4].location = 4;
    va[4].format = SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
    va[4].offset = 40;
    for (i = 0; i < 5; i++) {
        SDL_GPUColorTargetBlendState *b = &ctd.blend_state;

        SDL_zero(ctd);
        ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (i > 0) {
            b->enable_blend = true;
            b->src_color_blendfactor = i == 1 ? SDL_GPU_BLENDFACTOR_CONSTANT_COLOR : SDL_GPU_BLENDFACTOR_ONE;
            b->dst_color_blendfactor = i == 1 ? SDL_GPU_BLENDFACTOR_CONSTANT_COLOR : SDL_GPU_BLENDFACTOR_ONE;
            b->color_blend_op = i == 3 ? SDL_GPU_BLENDOP_REVERSE_SUBTRACT : SDL_GPU_BLENDOP_ADD;
            b->src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b->dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b->alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }
        SDL_zero(pi);
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = va;
        pi.vertex_input_state.num_vertex_attributes = 5;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pipes[i] = SDL_CreateGPUGraphicsPipeline(dev, &pi);
        if (pipes[i] == NULL) {
            printf("[gpu] GPU renderer: pipeline failed: %s\n", SDL_GetError());
            dev = NULL;
            return 0;
        }
    }
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);
    SDL_zero(bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bi.size = VRAM_BYTES;
    vram_buf = SDL_CreateGPUBuffer(dev, &bi);
    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = VRAM_BYTES;
    vram_tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    {
        SDL_GPUBufferCreateInfo di;

        SDL_zero(di);
        di.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        di.size = 256;
        rep_dummy = SDL_CreateGPUBuffer(dev, &di); /* bound while no replacement exists */
        if (rep_dummy == NULL) {
            vram_buf = NULL;
        }
    }
    if (vram_buf == NULL || vram_tb == NULL) {
        printf("[gpu] GPU renderer: VRAM buffer failed: %s\n", SDL_GetError());
        dev = NULL;
        return 0;
    }
    if (!init_sharpen()) {
        dev = NULL;
        return 0;
    }
    dirty_y0 = 0;
    dirty_y1 = PSXGPU_VRAM_H;
    printf("[gpu] GPU renderer: SDL_GPU %s\n", SDL_GetGPUDeviceDriver(dev));
    return 1;
}

void PsxHw_Shutdown(void) {
    int i;

    if (dev == NULL) {
        return;
    }
    for (i = 0; i < 2; i++) {
        Ctx *x = i == 0 ? &game_ctx : &win_ctx;

        drain_ctx(x);
        if (x->vbuf != NULL) {
            SDL_ReleaseGPUBuffer(dev, x->vbuf);
            SDL_ReleaseGPUTransferBuffer(dev, x->vtb);
        }
        x->vbuf = NULL;
        x->vtb = NULL;
        x->vcap = 0;
    }
    for (i = 0; i < 5; i++) {
        SDL_ReleaseGPUGraphicsPipeline(dev, pipes[i]);
    }
    SDL_ReleaseGPUGraphicsPipeline(dev, sharpen_pipe);
    SDL_ReleaseGPUSampler(dev, sharpen_sampler);
    sharpen_pipe = NULL;
    sharpen_sampler = NULL;
    SDL_ReleaseGPUBuffer(dev, vram_buf);
    SDL_ReleaseGPUTransferBuffer(dev, vram_tb);
    if (rep_buf != NULL) {
        SDL_ReleaseGPUBuffer(dev, rep_buf);
    }
    if (rep_dummy != NULL) {
        SDL_ReleaseGPUBuffer(dev, rep_dummy);
    }
    rep_buf = rep_dummy = NULL;
    if (rep_tb != NULL) {
        SDL_ReleaseGPUTransferBuffer(dev, rep_tb);
    }
    rep_tb = NULL;
    rep_tb_size = 0;
    memset(rep_tab, 0, sizeof(rep_tab));
    rep_count = rep_nup = 0;
    rep_top = 0;
    rep_dead = rep_reset_req = rep_full_logged = 0;
    if (read_tb != NULL) {
        SDL_ReleaseGPUTransferBuffer(dev, read_tb);
    }
    read_tb = NULL;
    read_cap = 0;
    dev = NULL;
}

int PsxHw_Active(void) {
    return dev != NULL;
}

void *PsxHw_Create(int w, int h) {
    SDL_GPUTextureCreateInfo tci;

    if (dev == NULL) {
        return NULL;
    }
    SDL_zero(tci);
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = (Uint32)w;
    tci.height = (Uint32)h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    return SDL_CreateGPUTexture(dev, &tci);
}

void PsxHw_Release(void *tex) {
    if (dev != NULL) {
        SDL_ReleaseGPUTexture(dev, tex);
    }
}

void PsxHw_MarkDirty(int y, int h) {
    int y1 = y + h;

    if (h <= 0) {
        return;
    }
    if (y < 0 || y1 > PSXGPU_VRAM_H) {
        y = 0;
        y1 = PSXGPU_VRAM_H;
    }
    if (dirty_y0 >= dirty_y1) {
        dirty_y0 = y;
        dirty_y1 = y1;
    } else {
        dirty_y0 = y < dirty_y0 ? y : dirty_y0;
        dirty_y1 = y1 > dirty_y1 ? y1 : dirty_y1;
    }
}

/* ---- queue to triangles ---- */

static void emit(Ctx *x, PsxHdSurf *s, int pipe, const SDL_Rect *scin, const Vert *v, int n) {
    Draw *d = x->ndraws > 0 ? &x->draws[x->ndraws - 1] : NULL;
    SDL_Rect scr = *scin, *sc = &scr;
    int i;

    if (x->ox != 0 || x->oy != 0) {
        /* PR.34: a target whose origin is surface pixel (ox, oy) */
        sc->x -= x->ox;
        sc->y -= x->oy;
        if (sc->x < 0) {
            sc->w += sc->x;
            sc->x = 0;
        }
        if (sc->y < 0) {
            sc->h += sc->y;
            sc->y = 0;
        }
        if (sc->w <= 0 || sc->h <= 0) {
            return;
        }
    }
    if (x->nverts + n > x->verts_cap) {
        int cap = x->verts_cap ? x->verts_cap * 2 : 1 << 16;
        Vert *nv;
        while (cap < x->nverts + n) {
            cap *= 2;
        }
        nv = realloc(x->verts, (size_t)cap * sizeof(Vert));
        if (nv == NULL) {
            return;
        }
        x->verts = nv;
        x->verts_cap = cap;
    }
    if (d == NULL || d->s != s || d->pipe != pipe || d->scissor.x != sc->x || d->scissor.y != sc->y ||
        d->scissor.w != sc->w || d->scissor.h != sc->h) {
        if (x->ndraws == x->draws_cap) {
            int cap = x->draws_cap ? x->draws_cap * 2 : 1024;
            Draw *nd = realloc(x->draws, (size_t)cap * sizeof(Draw));
            if (nd == NULL) {
                return;
            }
            x->draws = nd;
            x->draws_cap = cap;
        }
        d = &x->draws[x->ndraws++];
        d->s = s;
        d->pipe = pipe;
        d->scissor = *sc;
        d->first = (Uint32)x->nverts;
        d->count = 0;
    }
    memcpy(&x->verts[x->nverts], v, (size_t)n * sizeof(Vert));
    for (i = 0; i < n && (x->ox != 0 || x->oy != 0); i++) {
        x->verts[x->nverts + i].x -= (float)x->ox;
        x->verts[x->nverts + i].y -= (float)x->oy;
    }
    x->nverts += n;
    d->count += (Uint32)n;
}

/* Emits n vertices once, or twice for a textured semi-transparent primitive (opaque texels, then
 * semi-transparent texels blended). */
static void emit_prim(Ctx *x, const PsxHdCmd *c, int scale, Vert *v, int n, int textured) {
    SDL_Rect sc;
    int x0, y0, x1, y1, i;

    PsxHd_CmdClipAt(c, scale, &x0, &y0, &x1, &y1);
    if (x0 > x1 || y0 > y1) {
        return;
    }
    sc.x = x0;
    sc.y = y0;
    sc.w = x1 - x0 + 1;
    sc.h = y1 - y0 + 1;
    if (!c->semi) {
        emit(x, c->s, 0, &sc, v, n);
        return;
    }
    if (textured) {
        for (i = 0; i < n; i++) {
            v[i].par[2] |= F_PASS(1);
        }
        emit(x, c->s, 0, &sc, v, n);
        for (i = 0; i < n; i++) {
            v[i].par[2] ^= F_PASS(1) ^ F_PASS(2);
        }
    } else {
        for (i = 0; i < n; i++) {
            v[i].par[2] |= F_PASS(2);
        }
    }
    emit(x, c->s, 1 + c->abr, &sc, v, n);
}

/* dw2-pc-port texture packs on the GPU. The replacement pixels (0xAARRGGBB, the level of the
 * picture TexRep_GpuLevel chose for the current scale) live in one storage buffer, REP_BYTES (the
 * 128 MiB every Vulkan device must allow in a storage buffer), filled by a bump allocator and
 * addressed by the vertices (Vert.rep). Uploads go through the game thread's copy pass of a flush
 * (at most REP_FRAME_BYTES per flush: a texture that does not fit yet is drawn as the original for
 * a few frames, like one still loading). When the pool or its table is full, or the scale changes,
 * it starts over at the next flush (a smooth-frame replay of an older flush may show a wrong
 * texture for one picture then). The window thread only looks up what is resident. */
void PsxHw_RepReset(void) {
    rep_reset_req = 1;
}

static uint32_t rep_hash(const uint32_t *px) {
    uintptr_t h = (uintptr_t)px;
    h ^= h >> 17;
    h *= 0x9E3779B1u;
    return (uint32_t)(h >> 7) % REP_TABLE;
}

/* the word offset of a level's pixels in the pool, or UINT32_MAX (not resident, not allowed or no room) */
static uint32_t rep_lookup(const uint32_t *px, uint32_t bytes, int alloc) {
    uint32_t i = rep_hash(px);

    while (rep_tab[i].px != NULL) {
        if (rep_tab[i].px == px) {
            return rep_tab[i].off;
        }
        i = (i + 1) % REP_TABLE;
    }
    if (!alloc || rep_dead || bytes == 0 || bytes > REP_BYTES) {
        return UINT32_MAX;
    }
    texrep_perf.gpu_refused++; /* undone below when it is taken */
    if (rep_top + bytes > REP_BYTES || rep_count >= REP_TABLE / 2 || rep_nup >= 256) {
        if (rep_nup < 256 && !rep_full_logged) {
            rep_full_logged = 1;
            printf("[gpu] texture pack: the replacement memory (%u MiB) is full, it starts over at the next picture\n",
                   REP_BYTES >> 20);
        }
        rep_reset_req = 1;
        return UINT32_MAX;
    }
    if (rep_frame + bytes > REP_FRAME_BYTES && rep_frame > 0) {
        return UINT32_MAX; /* the next flush */
    }
    if (rep_buf == NULL) {
        SDL_GPUBufferCreateInfo bi;

        SDL_zero(bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bi.size = REP_BYTES;
        rep_buf = SDL_CreateGPUBuffer(dev, &bi);
        if (rep_buf == NULL) {
            printf("[gpu] texture pack: no GPU memory for replacements (%s), originals are used\n", SDL_GetError());
            rep_dead = 1;
            return UINT32_MAX;
        }
    }
    texrep_perf.gpu_refused--;
    rep_tab[i].px = px;
    rep_tab[i].off = rep_top / 4u;
    rep_tab[i].words = bytes / 4u;
    rep_up[rep_nup].px = px;
    rep_up[rep_nup].off = rep_top;
    rep_up[rep_nup].bytes = bytes;
    rep_nup++;
    rep_count++;
    rep_top += bytes;
    rep_frame += bytes;
    return rep_tab[i].off;
}

/* a flush's start (game thread): a pending reset happens here, between two batches */
static void rep_begin(void) {
    rep_frame = 0;
    if (rep_reset_req) {
        memset(rep_tab, 0, sizeof(rep_tab));
        rep_count = 0;
        rep_top = 0;
        rep_nup = 0;
        rep_reset_req = 0;
        rep_full_logged = 0;
    }
}

/* the pending replacement pixels into the pool (game thread, inside the copy pass): one transfer
 * buffer for all of them, kept and cycled (SDL gives a free one when the GPU still reads the last):
 * a buffer per texture meant a GPU memory allocation and release for each new runtime font tile */
static void rep_upload(SDL_GPUCopyPass *copy) {
    Uint64 t0 = SDL_GetTicksNS();
    uint32_t total = 0, at = 0;
    uint8_t *m;

    for (int i = 0; i < rep_nup; i++) {
        total += rep_up[i].bytes;
    }
    if (total == 0) {
        rep_nup = 0;
        return;
    }
    if (total > rep_tb_size) {
        SDL_GPUTransferBufferCreateInfo ti;

        if (rep_tb != NULL) {
            SDL_ReleaseGPUTransferBuffer(dev, rep_tb);
        }
        SDL_zero(ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = total > (8u << 20) ? total : (8u << 20);
        rep_tb = SDL_CreateGPUTransferBuffer(dev, &ti);
        rep_tb_size = rep_tb != NULL ? ti.size : 0;
    }
    m = rep_tb != NULL ? SDL_MapGPUTransferBuffer(dev, rep_tb, true) : NULL;
    if (m == NULL) {
        printf("[gpu] texture pack: upload failed (%s)\n", SDL_GetError());
        rep_reset_req = 1; /* the entries are in the table, the pixels are not: start over */
        rep_nup = 0;
        return;
    }
    for (int i = 0; i < rep_nup; i++) {
        memcpy(m + at, rep_up[i].px, rep_up[i].bytes);
        at += rep_up[i].bytes;
    }
    SDL_UnmapGPUTransferBuffer(dev, rep_tb);
    at = 0;
    for (int i = 0; i < rep_nup; i++) {
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;

        src.transfer_buffer = rep_tb;
        src.offset = at;
        dst.buffer = rep_buf;
        dst.offset = rep_up[i].off;
        dst.size = rep_up[i].bytes;
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        at += rep_up[i].bytes;
        texrep_perf.gpu_ups++;
        texrep_perf.gpu_up_bytes += rep_up[i].bytes;
    }
    texrep_perf.gpu_up_ns += SDL_GetTicksNS() - t0;
    rep_nup = 0;
}

/* the replacement of command c for its vertices (Vert.rep); stays zero = the original */
static void set_rep(Ctx *x, Vert *v, int n, const PsxHdCmd *c) {
    int kl, nb, w, h, du, dv, idx8;
    const uint32_t *px;
    uint32_t off;
    uint32_t r[4];

    if (c->rep.img == NULL || !c->textured || rep_dead) {
        return;
    }
    px = TexRep_GpuLevel(&c->rep, &kl, &nb, &w, &h, &idx8);
    du = c->rep.du;
    dv = c->rep.dv;
    if (kl < 1 || kl > 16 || w > 0xFFFF || h > 0xFFFF || du < -512 || du > 511 || dv < -512 || dv > 511) {
        return;
    }
    off = rep_lookup(px, idx8 ? ((uint32_t)w * (uint32_t)h + 3u) & ~3u : (uint32_t)w * (uint32_t)h * 4u, x == &game_ctx);
    if (off == UINT32_MAX) {
        return;
    }
    r[0] = off;
    r[1] = (uint32_t)w | (uint32_t)h << 16;
    r[2] = (uint32_t)kl | (uint32_t)nb << 5 | (uint32_t)(du + 512) << 8 | (uint32_t)(dv + 512) << 18 | (uint32_t)(idx8 != 0) << 28;
    r[3] = (uint32_t)(c->rep.umin & 255) | (uint32_t)(c->rep.umax & 255) << 8 | (uint32_t)(c->rep.vmin & 255) << 16 |
           (uint32_t)(c->rep.vmax & 255) << 24;
    for (int i = 0; i < n; i++) {
        memcpy(v[i].rep, r, sizeof(r));
    }
}

static uint32_t tex_bits(const PsxTex *t) {
    return (uint32_t)t->mode | (t->raw ? 4u : 0u) | 8u | (uint32_t)(t->tx / 64) << 4 | (uint32_t)(t->ty / 256) << 8 |
           (uint32_t)(t->cx / 16) << 9 | (uint32_t)t->cy << 15;
}

static uint32_t win_bits(const PsxGpuState *st) {
    return (uint32_t)(st->tw_mask_x / 8) | (uint32_t)(st->tw_mask_y / 8) << 5 | (uint32_t)(st->tw_off_x / 8) << 10 |
           (uint32_t)(st->tw_off_y / 8) << 15;
}

static void set_par(Vert *v, int n, uint32_t tex, uint32_t win, uint32_t fl, uint32_t lim) {
    int i;

    for (i = 0; i < n; i++) {
        v[i].par[0] = tex;
        v[i].par[1] = win;
        v[i].par[2] = fl;
        v[i].par[3] = lim;
        memset(v[i].rep, 0, sizeof(v[i].rep));
    }
}

static void tri(Ctx *x, const PsxHdCmd *c, int scale) {
    const PsxGpuState *st = &c->st;
    const PsxTex *t = c->textured ? &c->tex : NULL;
    PsxHdSurf *s = c->s;
    float ox = (float)(s->x - s->m), oy = (float)s->y, S = (float)scale;
    const PsxVtx *p = c->v;
    int persp = t != NULL && c->precise && p[0].pz > 0 && p[1].pz > 0 && p[2].pz > 0;
    int block = 0;
    uint32_t fl = 0, lim = 0;
    Vert v[3];
    float a = x->alpha, k = 1.0f - x->alpha;
    int blend = a < 1.0f && c->blend, shift = a < 1.0f && !c->precise && (c->dx != 0 || c->dy != 0);
    int i;

    if (t != NULL && !c->precise) {
        /* no cross terms in the mapping (du / dy = dv / dx = 0): sprite-like, read at the 1x pixel */
        int64_t dudy = (int64_t)p[0].u * (p[2].x - p[1].x) + (int64_t)p[1].u * (p[0].x - p[2].x) +
                       (int64_t)p[2].u * (p[1].x - p[0].x);
        int64_t dvdx = (int64_t)p[0].v * (p[2].y - p[1].y) + (int64_t)p[1].v * (p[0].y - p[2].y) +
                       (int64_t)p[2].v * (p[1].y - p[0].y);
        block = dudy == 0 && dvdx == 0;
    }
    if (st->dither && (c->gouraud || (t != NULL && !t->raw))) {
        fl |= F_DITHER;
    }
    if (block) {
        int umin = p[0].u < p[1].u ? p[0].u : p[1].u, umax = p[0].u > p[1].u ? p[0].u : p[1].u;
        /* PR.34: a moved 2D layer triangle: no 1x block rule (see rect), the clamp stays */
        umin = p[2].u < umin ? p[2].u : umin;
        umax = p[2].u > umax ? p[2].u : umax;
        fl |= (shift ? 0 : F_BLOCK) | F_CLAMP;
        lim = (uint32_t)umin | (uint32_t)umax << 8;
        if (!shift && scale >= 2 && PsxHd_Smooth2d()) {
            /* dw2-pc-port 2D smoothing (backend/psxgpu_hd.c smooth2d_texel), UV box exclusive at
             * its far edges like the soft path */
            int vmin = p[0].v < p[1].v ? p[0].v : p[1].v, vmax = p[0].v > p[1].v ? p[0].v : p[1].v;

            vmin = p[2].v < vmin ? p[2].v : vmin;
            vmax = p[2].v > vmax ? p[2].v : vmax;
            fl = (fl & ~(uint32_t)(F_BLOCK | F_CLAMP)) | F_SMOOTH2D;
            lim = (uint32_t)umin | (uint32_t)(umax > umin ? umax - 1 : umin) << 8 | (uint32_t)vmin << 16 |
                  (uint32_t)(vmax > vmin ? vmax - 1 : vmin) << 24;
        }
    }
    if (st->set_mask) {
        fl |= F_SETMASK;
    }
    if (t != NULL && !block && PsxHd_TexFilter()) {
        /* dw2-pc-port: bilinear inside the triangle's UV box */
        int u0 = p[0].u, u1 = p[0].u, v0 = p[0].v, v1 = p[0].v;

        for (i = 1; i < 3; i++) {
            u0 = p[i].u < u0 ? p[i].u : u0;
            u1 = p[i].u > u1 ? p[i].u : u1;
            v0 = p[i].v < v0 ? p[i].v : v0;
            v1 = p[i].v > v1 ? p[i].v : v1;
        }
        fl |= F_FILTER;
        lim = (uint32_t)u0 | (uint32_t)u1 << 8 | (uint32_t)v0 << 16 | (uint32_t)v1 << 24;
    }
    for (i = 0; i < 3; i++) {
        const PsxVtx *q = c->gouraud ? &p[i] : &p[0];
        if (blend) {
            /* PR.34: between the frame before (q) and this frame (p) */
            v[i].x = (p[i].qx * k + p[i].px * a - ox) * S + 0.5f;
            v[i].y = (p[i].qy * k + p[i].py * a - oy) * S + 0.5f;
        } else if (c->precise) {
            v[i].x = (p[i].px - ox) * S + 0.5f;
            v[i].y = (p[i].py - oy) * S + 0.5f;
        } else if (shift) {
            /* PR.34: a 2D layer moved by its scroll (backend/psxgpu_hd.c PsxHd_SmoothLayer) */
            v[i].x = ((float)p[i].x + c->dx * k - ox) * S + 0.5f;
            v[i].y = ((float)p[i].y + c->dy * k - oy) * S + 0.5f;
        } else {
            v[i].x = ((float)p[i].x - ox) * S + 0.5f;
            v[i].y = ((float)p[i].y - oy) * S + 0.5f;
        }
        v[i].w = persp ? (blend ? p[i].qz * k + p[i].pz * a : p[i].pz) : 1.0f;
        v[i].u = (float)p[i].u;
        v[i].v = (float)p[i].v;
        v[i].r = (uint8_t)q->r;
        v[i].g = (uint8_t)q->g;
        v[i].b = (uint8_t)q->b;
        v[i].a = 0;
    }
    set_par(v, 3, t != NULL ? tex_bits(t) : 0, win_bits(st), fl, lim);
    set_rep(x, v, 3, c);
    emit_prim(x, c, scale, v, 3, t != NULL);
}

/* Two triangles of the quad a b c d (a b on one side, c d on the other). */
static void quad(Vert *o, const Vert *a, const Vert *b, const Vert *c, const Vert *d) {
    o[0] = *a;
    o[1] = *b;
    o[2] = *c;
    o[3] = *b;
    o[4] = *c;
    o[5] = *d;
}

static void corner(Vert *v, float x, float y, float u, float t, int r, int g, int b) {
    memset(v, 0, sizeof(*v));
    v->x = x;
    v->y = y;
    v->w = 1.0f;
    v->u = u;
    v->v = t;
    v->r = (uint8_t)r;
    v->g = (uint8_t)g;
    v->b = (uint8_t)b;
}

static void rect(Ctx *x, const PsxHdCmd *c, int scale) {
    const PsxGpuState *st = &c->st;
    const PsxTex *t = c->textured ? &c->tex : NULL;
    PsxHdSurf *s = c->s;
    float back = x->alpha < 1.0f ? 1.0f - x->alpha : 0.0f; /* PR.34: a 2D layer moved by its scroll */
    float x0 = (float)((c->x0 - (s->x - s->m)) * scale) + c->dx * back * (float)scale;
    float y0 = (float)((c->y0 - s->y) * scale) + c->dy * back * (float)scale;
    float x1 = x0 + (float)(c->w * scale), y1 = y0 + (float)(c->h * scale);
    /* the texel of 1x column xx is u0 + xx (u0 - xx flipped: the left edge one texel up) */
    float ul = st->flip_x ? (float)(c->u0 + 1) : (float)c->u0;
    float ur = st->flip_x ? (float)(c->u0 + 1 - c->w) : (float)(c->u0 + c->w);
    float vt = st->flip_y ? (float)(c->v0 + 1) : (float)c->v0;
    float vb = st->flip_y ? (float)(c->v0 + 1 - c->h) : (float)(c->v0 + c->h);
    Vert k[4], v[6];

    uint32_t fl = F_BLOCK, lim = 0;

    corner(&k[0], x0, y0, ul, vt, c->r, c->g, c->b);
    corner(&k[1], x1, y0, ur, vt, c->r, c->g, c->b);
    corner(&k[2], x0, y1, ul, vb, c->r, c->g, c->b);
    corner(&k[3], x1, y1, ur, vb, c->r, c->g, c->b);
    quad(v, &k[0], &k[1], &k[2], &k[3]);
    if (back > 0.0f && (c->dx != 0 || c->dy != 0)) {
        /* PR.34: moved by a fraction of a 1x pixel. The block rule reads the texel of the 1x
         * pixel grid of the surface, which is not the rect's grid any more: at the edges it read
         * one texel outside the rect (black seams between the city map strips). The texels move
         * with the rect instead, kept inside its columns. */
        fl = 0;
        if (!st->flip_x) {
            fl = F_CLAMP;
            lim = (uint32_t)(c->u0 & 255) | (uint32_t)((c->u0 + c->w - 1) & 255) << 8;
        }
    }
    if (fl == F_BLOCK && t != NULL && scale >= 2 && !st->flip_x && !st->flip_y && PsxHd_Smooth2d()) {
        /* dw2-pc-port 2D smoothing inside the rect's texels */
        fl = F_SMOOTH2D;
        lim = (uint32_t)(c->u0 & 255) | (uint32_t)((c->u0 + c->w - 1) & 255) << 8 | (uint32_t)(c->v0 & 255) << 16 |
              (uint32_t)((c->v0 + c->h - 1) & 255) << 24;
    }
    set_par(v, 6, t != NULL ? tex_bits(t) : 0, win_bits(st), fl | (st->set_mask ? F_SETMASK : 0), lim);
    set_rep(x, v, 6, c);
    emit_prim(x, c, scale, v, 6, t != NULL);
}

/* A line S pixels thick: a parallelogram from the first end pixel's block to the last one's
 * along the major axis, S wide across it (backend/psxgpu_hd.c draw_line). */
static void line(Ctx *x, const PsxHdCmd *c, int scale) {
    const PsxGpuState *st = &c->st;
    PsxHdSurf *s = c->s;
    const PsxVtx *a = &c->v[0], *b = &c->v[1];
    int dx = b->x - a->x, dy = b->y - a->y;
    int xmajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    float S = (float)scale;
    float ax, ay, bx, by;
    const PsxVtx *ca, *cb;
    Vert k[4], v[6];

    if ((xmajor && dx < 0) || (!xmajor && dy < 0)) {
        const PsxVtx *tmp = a;
        a = b;
        b = tmp;
    }
    ca = a;
    cb = c->gouraud ? b : &c->v[0];
    if (!c->gouraud) {
        ca = &c->v[0];
    }
    ax = (float)(a->x - (s->x - s->m)) * S;
    ay = (float)(a->y - s->y) * S;
    bx = (float)(b->x - (s->x - s->m)) * S;
    by = (float)(b->y - s->y) * S;
    if (x->alpha < 1.0f && c->blend) {
        /* PR.34: moved back by the end points' move since the frame before */
        float k = 1.0f - x->alpha;

        ax += (a->qx - a->px) * k * S;
        ay += (a->qy - a->py) * k * S;
        bx += (b->qx - b->px) * k * S;
        by += (b->qy - b->py) * k * S;
    }
    if (xmajor) {
        corner(&k[0], ax, ay, 0, 0, ca->r, ca->g, ca->b);
        corner(&k[1], ax, ay + S, 0, 0, ca->r, ca->g, ca->b);
        corner(&k[2], bx + S, by, 0, 0, cb->r, cb->g, cb->b);
        corner(&k[3], bx + S, by + S, 0, 0, cb->r, cb->g, cb->b);
    } else {
        corner(&k[0], ax, ay, 0, 0, ca->r, ca->g, ca->b);
        corner(&k[1], ax + S, ay, 0, 0, ca->r, ca->g, ca->b);
        corner(&k[2], bx, by + S, 0, 0, cb->r, cb->g, cb->b);
        corner(&k[3], bx + S, by + S, 0, 0, cb->r, cb->g, cb->b);
    }
    quad(v, &k[0], &k[1], &k[2], &k[3]);
    set_par(v, 6, 0, 0, (st->dither && c->gouraud ? F_DITHER : 0) | (st->set_mask ? F_SETMASK : 0), 0);
    emit_prim(x, c, scale, v, 6, 0);
}

/* Fill (kind 1, colour in VRAM format) or VRAM copy (kind 2) of the surface pixel rect x0, y0,
 * w, h; the copy reads VRAM from u0, v0. */
static void transfer(Ctx *x, const PsxHdCmd *c, int scale) {
    PsxHdSurf *s = c->s;
    float x0 = (float)c->x0, y0 = (float)c->y0, x1 = x0 + (float)c->w, y1 = y0 + (float)c->h;
    int r = (c->r & 31) << 3, g = ((c->r >> 5) & 31) << 3, b = ((c->r >> 10) & 31) << 3;
    float u0 = (float)c->u0, v0 = (float)c->v0;
    float u1 = u0 + (float)c->w / (float)scale, v1 = v0 + (float)c->h / (float)scale;
    SDL_Rect sc;
    Vert k[4], v[6];

    sc.x = 0;
    sc.y = 0;
    sc.w = (s->w + 2 * s->m) * scale;
    sc.h = s->h * scale;
    corner(&k[0], x0, y0, u0, v0, r, g, b);
    corner(&k[1], x1, y0, u1, v0, r, g, b);
    corner(&k[2], x0, y1, u0, v1, r, g, b);
    corner(&k[3], x1, y1, u1, v1, r, g, b);
    quad(v, &k[0], &k[1], &k[2], &k[3]);
    set_par(v, 6, 0, 0, F_KIND(c->type == PSXHD_FILL ? 1 : 2) | F_BLOCK, 0);
    emit(x, s, 0, &sc, v, 6);
}

/* ---- batch ---- */

/* PG.10 b3: submits cmd, then waits for the previous submission (normally done long ago). The
 * wait lets SDL_GPU recycle finished command buffers and cycled transfer buffers; without it they
 * piled up whenever the SDL renderer did not present (headless runs, a window held by a drag):
 * about 115 MB per second at 2x.
 * PR.36: the wait is for the submission FENCES - 1 back, not the one just before. With smooth
 * frames a game frame submits three times (draw, base copy, VRAM copy), behind the window
 * thread's pictures on the same GPU queue: waiting for the previous one made every game frame wait
 * for its own drawing and the window's queued pictures, and the game slowed down on slower GPUs and
 * with the speed-up. Fences are released only after a wait: releasing one early once
 * SDL_QueryGPUFence said done crashed SDL's Vulkan backend (uniform buffer pool) in long runs. */
static void submit_ctx(Ctx *x, SDL_GPUCommandBuffer *cmd) {
    Uint64 start = x == &game_ctx ? SDL_GetTicksNS() : 0;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);

    if (x == &game_ctx) {
        texrep_perf.gpu_submit_ns += SDL_GetTicksNS() - start;
        texrep_perf.gpu_submits++;
    }

    if (x->nfences == FENCES) {
        start = x == &game_ctx ? SDL_GetTicksNS() : 0;
        SDL_WaitForGPUFences(dev, true, &x->fences[0], 1);
        if (x == &game_ctx) {
            texrep_perf.gpu_wait_ns += SDL_GetTicksNS() - start;
            texrep_perf.gpu_waits++;
        }
        SDL_ReleaseGPUFence(dev, x->fences[0]);
        memmove(&x->fences[0], &x->fences[1], (FENCES - 1) * sizeof(x->fences[0]));
        x->nfences--;
    }
    if (fence != NULL) {
        x->fences[x->nfences++] = fence;
    }
}

/* every submission of x done (shutdown) */
static void drain_ctx(Ctx *x) {
    if (x->nfences > 0) {
        SDL_WaitForGPUFences(dev, true, x->fences, (Uint32)x->nfences);
    }
    while (x->nfences > 0) {
        SDL_ReleaseGPUFence(dev, x->fences[--x->nfences]);
    }
}

static void submit(SDL_GPUCommandBuffer *cmd) {
    submit_ctx(&game_ctx, cmd);
}

static SDL_GPUCommandBuffer *acquire_ctx(Ctx *x) {
    Uint64 start = x == &game_ctx ? SDL_GetTicksNS() : 0;
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (x == &game_ctx) {
        texrep_perf.gpu_acquire_ns += SDL_GetTicksNS() - start;
    }
    return cmd;
}

/* The dirty VRAM rows (game thread only, with_vram) and x's vertices to the GPU. */
static int upload(Ctx *x, SDL_GPUCopyPass *copy, int with_vram) {
    if (with_vram && dirty_y0 < dirty_y1) {
        Uint8 *m = SDL_MapGPUTransferBuffer(dev, vram_tb, true);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;

        if (m == NULL) {
            return 0;
        }
        memcpy(m + (size_t)dirty_y0 * ROW_BYTES, PsxGpu_Vram[dirty_y0], (size_t)(dirty_y1 - dirty_y0) * ROW_BYTES);
        SDL_UnmapGPUTransferBuffer(dev, vram_tb);
        src.transfer_buffer = vram_tb;
        src.offset = (Uint32)dirty_y0 * ROW_BYTES;
        dst.buffer = vram_buf;
        dst.offset = (Uint32)dirty_y0 * ROW_BYTES;
        dst.size = (Uint32)(dirty_y1 - dirty_y0) * ROW_BYTES;
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        dirty_y0 = dirty_y1 = 0;
    }
    if (x->nverts > 0) {
        Uint32 bytes = (Uint32)x->nverts * sizeof(Vert);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        void *m;

        if (bytes > x->vcap) {
            SDL_GPUBufferCreateInfo bi;
            SDL_GPUTransferBufferCreateInfo ti;
            Uint32 cap = x->vcap ? x->vcap : 1u << 20;

            while (cap < bytes) {
                cap *= 2;
            }
            if (x->vbuf != NULL) {
                SDL_ReleaseGPUBuffer(dev, x->vbuf);
                SDL_ReleaseGPUTransferBuffer(dev, x->vtb);
            }
            SDL_zero(bi);
            bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bi.size = cap;
            x->vbuf = SDL_CreateGPUBuffer(dev, &bi);
            SDL_zero(ti);
            ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            ti.size = cap;
            x->vtb = SDL_CreateGPUTransferBuffer(dev, &ti);
            x->vcap = cap;
            if (x->vbuf == NULL || x->vtb == NULL) {
                printf("[gpu] GPU renderer: vertex buffer failed: %s\n", SDL_GetError());
                x->vcap = 0;
                return 0;
            }
        }
        m = SDL_MapGPUTransferBuffer(dev, x->vtb, true);
        if (m == NULL) {
            return 0;
        }
        memcpy(m, x->verts, bytes);
        SDL_UnmapGPUTransferBuffer(dev, x->vtb);
        src.transfer_buffer = x->vtb;
        src.offset = 0;
        dst.buffer = x->vbuf;
        dst.offset = 0;
        dst.size = bytes;
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    }
    return 1;
}

/* The commands as triangles into x (vertices and draws). */
static void build(Ctx *x, const PsxHdCmd *q, int n, int scale) {
    static int warned;
    int i;

    x->nverts = 0;
    x->ndraws = 0;
    for (i = 0; i < n; i++) {
        const PsxHdCmd *c = &q[i];

        if (c->st.check_mask && !warned) {
            warned = 1;
            printf("[gpu] GPU renderer: mask check not supported (logged once)\n");
        }
        switch (c->type) {
        case PSXHD_TRI:
            tri(x, c, scale);
            break;
        case PSXHD_RECT:
            rect(x, c, scale);
            break;
        case PSXHD_LINE:
            line(x, c, scale);
            break;
        default:
            transfer(x, c, scale);
            break;
        }
    }
}

/* x's draws into their surfaces' textures (target NULL) or all into target (tw x th, the
 * window thread's smooth picture), textures from vram. */
static void draw_built(Ctx *x, SDL_GPUCommandBuffer *cmd, int scale, SDL_GPUBuffer *vram, void *target, int tw, int th) {
    SDL_GPURenderPass *pass = NULL;
    PsxHdSurf *cur = NULL;
    int curpipe = -1, i;

    for (i = 0; i < x->ndraws; i++) {
        const Draw *d = &x->draws[i];

        if (pass == NULL || (target == NULL && d->s != cur)) {
            SDL_GPUColorTargetInfo ct;
            SDL_GPUBufferBinding vb;
            SDL_FColor half = { 0.5f, 0.5f, 0.5f, 0.5f };
            float size[2];
            int par[4] = { scale, x->ox, x->oy, 0 };

            if (pass != NULL) {
                SDL_EndGPURenderPass(pass);
            }
            cur = d->s;
            SDL_zero(ct);
            ct.texture = target != NULL ? target : cur->tex;
            ct.load_op = SDL_GPU_LOADOP_LOAD;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
            vb.buffer = x->vbuf;
            vb.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            {
                SDL_GPUBuffer *sb[2] = { vram, rep_buf != NULL ? rep_buf : rep_dummy };
                SDL_BindGPUFragmentStorageBuffers(pass, 0, sb, 2);
            }
            SDL_SetGPUBlendConstants(pass, half);
            size[0] = target != NULL ? (float)tw : (float)((cur->w + 2 * cur->m) * scale);
            size[1] = target != NULL ? (float)th : (float)(cur->h * scale);
            SDL_PushGPUVertexUniformData(cmd, 0, size, sizeof(size));
            SDL_PushGPUFragmentUniformData(cmd, 0, par, sizeof(par));
            curpipe = -1;
        }
        if (d->pipe != curpipe) {
            curpipe = d->pipe;
            SDL_BindGPUGraphicsPipeline(pass, pipes[curpipe]);
        }
        SDL_SetGPUScissor(pass, &d->scissor);
        SDL_DrawGPUPrimitives(pass, d->count, 1, d->first, 0);
    }
    if (pass != NULL) {
        SDL_EndGPURenderPass(pass);
    }
}

void PsxHw_Flush(const PsxHdCmd *q, int n, int scale) {
    Ctx *x = &game_ctx;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    Uint64 start;

    if (dev == NULL) {
        return;
    }
    rep_begin();
    start = SDL_GetTicksNS();
    build(x, q, n, scale);
    texrep_perf.gpu_build_ns += SDL_GetTicksNS() - start;
    cmd = acquire_ctx(x);
    if (cmd == NULL) {
        printf("[gpu] GPU renderer: no command buffer: %s\n", SDL_GetError());
        rep_nup = 0;
        rep_reset_req = 1;
        return;
    }
    start = SDL_GetTicksNS();
    copy = SDL_BeginGPUCopyPass(cmd);
    rep_upload(copy);
    if (!upload(x, copy, 1)) {
        x->ndraws = 0;
    }
    SDL_EndGPUCopyPass(copy);
    draw_built(x, cmd, scale, vram_buf, NULL, 0, 0);
    texrep_perf.gpu_record_ns += SDL_GetTicksNS() - start;
    submit(cmd);
}

void *PsxHw_VramCopy(void *buf) {
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    SDL_GPUBufferLocation from, to;

    if (dev == NULL) {
        return NULL;
    }
    if (buf == NULL) {
        SDL_GPUBufferCreateInfo bi;

        SDL_zero(bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bi.size = VRAM_BYTES;
        buf = SDL_CreateGPUBuffer(dev, &bi);
        if (buf == NULL) {
            return NULL;
        }
    }
    cmd = acquire_ctx(&game_ctx);
    if (cmd == NULL) {
        return buf;
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    from.buffer = vram_buf;
    from.offset = 0;
    to.buffer = buf;
    to.offset = 0;
    SDL_CopyGPUBufferToBuffer(copy, &from, &to, VRAM_BYTES, false);
    SDL_EndGPUCopyPass(copy);
    submit(cmd);
    return buf;
}

void PsxHw_ReleaseBuffer(void *buf) {
    if (dev != NULL && buf != NULL) {
        SDL_ReleaseGPUBuffer(dev, buf);
    }
}

/* SDL_CopyGPUTextureToTexture of the rect x, y, w, h of src to dx, dy of dst in cmd */
static void copy_tex(SDL_GPUCommandBuffer *cmd, void *src, int x, int y, int w, int h, void *dst, int dx, int dy) {
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureLocation from, to;

    SDL_zero(from);
    from.texture = src;
    from.x = (Uint32)x;
    from.y = (Uint32)y;
    SDL_zero(to);
    to.texture = dst;
    to.x = (Uint32)dx;
    to.y = (Uint32)dy;
    SDL_CopyGPUTextureToTexture(copy, &from, &to, (Uint32)w, (Uint32)h, 1, false);
    SDL_EndGPUCopyPass(copy);
}

void PsxHw_Replay(const PsxHdCmd *q, int n, int scale, int ox, int oy, float alpha, void *vram, void *base, void *dst,
                  int w, int h) {
    Ctx *x = &win_ctx;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;

    if (dev == NULL || vram == NULL || base == NULL || dst == NULL) {
        return;
    }
    x->ox = ox;
    x->oy = oy;
    x->alpha = alpha;
    build(x, q, n, scale);
    cmd = acquire_ctx(x);
    if (cmd == NULL) {
        return;
    }
    copy_tex(cmd, base, 0, 0, w, h, dst, 0, 0);
    copy = SDL_BeginGPUCopyPass(cmd);
    if (!upload(x, copy, 0)) {
        x->ndraws = 0;
    }
    SDL_EndGPUCopyPass(copy);
    draw_built(x, cmd, scale, vram, dst, w, h);
    submit_ctx(x, cmd);
}

void PsxHw_Copy(void *src, int x, int y, int w, int h, void *dst) {
    SDL_GPUCommandBuffer *cmd;

    if (dev == NULL || src == NULL || dst == NULL) {
        return;
    }
    cmd = acquire_ctx(&game_ctx);
    if (cmd == NULL) {
        return;
    }
    copy_tex(cmd, src, x, y, w, h, dst, 0, 0);
    submit(cmd);
}

/* PR.2b: the rect x, y, w, h of src sharpened (strength 1..100) into the top-left corner of dst. */
static void sharpen_ctx(Ctx *cx, void *src, int x, int y, int w, int h, void *dst, int strength) {
    SDL_GPUCommandBuffer *cmd;
    SDL_GPURenderPass *pass;
    SDL_GPUColorTargetInfo ct;
    SDL_GPUTextureSamplerBinding tsb;
    SDL_GPUViewport vp;
    SDL_Rect sc = { 0, 0, w, h };
    /* dw2-pc-port: the pass's values come from PsxHd_Post (sharpening and the other picture
     * filters); `strength` only says that the pass is wanted */
    PsxHdPost pp;
    struct {
        int32_t origin[2], size[2];
        float sharpness, fxaa, brightness, contrast, saturation, inv_gamma, pad[2];
    } par = { { x, y }, { w, h }, 0, 0, 0, 1, 1, 1, { 0, 0 } };

    (void)strength;
    PsxHd_Post(&pp);
    par.sharpness = pp.sharpen > 0 ? pp.sharpen / 100.0f : -1.0f;
    par.fxaa = pp.fxaa ? 1.0f : 0.0f;
    par.brightness = (pp.brightness - 50) / 200.0f;
    par.contrast = pp.contrast / 100.0f;
    par.saturation = pp.saturation / 100.0f;
    par.inv_gamma = 100.0f / pp.gamma;

    if (dev == NULL || src == NULL || dst == NULL) {
        return;
    }
    cmd = acquire_ctx(cx);
    if (cmd == NULL) {
        return;
    }
    SDL_zero(ct);
    ct.texture = dst;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    vp.x = 0;
    vp.y = 0;
    vp.w = (float)w;
    vp.h = (float)h;
    vp.min_depth = 0;
    vp.max_depth = 1;
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    SDL_BindGPUGraphicsPipeline(pass, sharpen_pipe);
    tsb.texture = src;
    tsb.sampler = sharpen_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    SDL_PushGPUFragmentUniformData(cmd, 0, &par, sizeof(par));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    submit_ctx(cx, cmd);
}

void PsxHw_Sharpen(void *src, int x, int y, int w, int h, void *dst, int strength) {
    sharpen_ctx(&game_ctx, src, x, y, w, h, dst, strength);
}

void PsxHw_SharpenWin(void *src, int x, int y, int w, int h, void *dst, int strength) {
    sharpen_ctx(&win_ctx, src, x, y, w, h, dst, strength);
}

int PsxHw_Read(void *tex, int x, int y, int w, int h, uint32_t *out) {
    Uint32 bytes = (Uint32)w * (Uint32)h * 4;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUFence *fence;
    const Uint8 *m;
    int i;

    if (dev == NULL || tex == NULL) {
        return 0;
    }
    if (bytes > read_cap) {
        SDL_GPUTransferBufferCreateInfo ti;

        if (read_tb != NULL) {
            SDL_ReleaseGPUTransferBuffer(dev, read_tb);
        }
        SDL_zero(ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = bytes;
        read_tb = SDL_CreateGPUTransferBuffer(dev, &ti);
        read_cap = read_tb != NULL ? bytes : 0;
        if (read_tb == NULL) {
            return 0;
        }
    }
    cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (cmd == NULL) {
        return 0;
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src);
    src.texture = tex;
    src.x = (Uint32)x;
    src.y = (Uint32)y;
    src.w = (Uint32)w;
    src.h = (Uint32)h;
    src.d = 1;
    SDL_zero(dst);
    dst.transfer_buffer = read_tb;
    dst.pixels_per_row = (Uint32)w;
    dst.rows_per_layer = (Uint32)h;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return 0;
    }
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    m = SDL_MapGPUTransferBuffer(dev, read_tb, false);
    if (m == NULL) {
        return 0;
    }
    for (i = 0; i < w * h; i++) {
        out[i] = (uint32_t)m[i * 4] << 16 | (uint32_t)m[i * 4 + 1] << 8 | m[i * 4 + 2];
    }
    SDL_UnmapGPUTransferBuffer(dev, read_tb);
    return 1;
}
