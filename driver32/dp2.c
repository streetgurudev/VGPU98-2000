/*
 * vgpu98dx.dll: DrawPrimitives2 (spec/directx-hal.md): the runtime's command
 * buffer, read command by command, becomes GPU packets. State commands
 * (DirectX 7's transform and lighting ones among them) update the context,
 * or the state block being recorded; drawing commands send what state the
 * GPU lacks, the call's vertex declaration, where the call's vertices are,
 * the draw's indices, then a DRAW (command set 2.0, spec/gpu-commands.md).
 * DirectX 7's CLEAR and TEXBLT go into the same batch, in order.
 *
 * A DirectX 8 context's draws (command set 2.1) read the streams and index
 * buffer its commands bound, through its vertex shader's declaration: a
 * buffer in the pool where it is, anything else through the inline buffer
 * (the x8_ functions below). Its DirectX 6 and 7 drawing commands draw from
 * the same bindings.
 *
 * A context keeps its state from one call to the next while nothing it was
 * built from changed outside its state commands (ctx_unchanged), and the
 * GPU keeps the declaration, the index buffer and stream 0 between batches
 * until a reset: each goes only when it differs from what the GPU holds.
 *
 * The call's vertices in a pool buffer (pool.c) the GPU reads where they
 * are: STREAM names the buffer from the call's vertices to its end, at the
 * buffer's version, and the buffer keeps the batch's fence. Others (in user memory, a buffer the
 * application put in system memory, or one of the DLL's there) go through
 * the ring: the ones a draw
 * uses that the GPU does not have yet, in chunks of 64 as the runtime holds
 * them, into the GPU's inline buffer from its start (vertex n at n times
 * the stride). The inline vertices of the _IMM commands go above them, and
 * the indices of the draw being sent in its last megabyte.
 *
 * The command layouts and the order of an edge-flagged fan follow the
 * reference rasterizer (DirectX 7 DDK).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

/* The largest packet payload the DLL writes, in dwords: half the ring. */
#define MAX_PAYLOAD (SB_RING_SIZE / 2 / 4 - 1)

/* The inline buffer: vertices, then the index area. */
#define IDX_BYTES (1u << 20)
#define IDX_AT    (VGPU98_INLINE_BYTES - IDX_BYTES)
#define VTX_BYTES IDX_AT

/* A call's vertices go to the GPU in chunks of 64, as its draws need them;
   at most this many vertices of a call. */
#define CALL_VERTICES 65536
#define CHUNK_SHIFT   6
#define CHUNKS        (CALL_VERTICES >> CHUNK_SHIFT)
#define CHUNK_WORDS   (CHUNKS / 32)

#define MAX_ELEMENTS VGPU98_DECL_MAX /* a DirectX 8 declaration's at most; an FVF code has 15 */

/* Where the GPU's stream 0 is (dp2_t.stream). */
#define STREAM_NONE   0
#define STREAM_INLINE 1 /* the inline buffer */
#define STREAM_POOL   2 /* the call's vertices in the pool */

/* Where a stream's vertices, or the index buffer's indices, are for a draw:
   a buffer in the pool the GPU reads where it is (at its GPU address and
   version), or memory the DLL reads into the inline buffer (user memory, a
   buffer in system memory, the runtime's own buffers, which it may write
   without a Lock). */
#define X8_POOL 1
#define X8_RING 2

typedef struct x8_src {
    int            kind;
    pbuf_t        *pool;
    DWORD          addr;   /* X8_POOL: the GPU address */
    DWORD          version;
    const uint8_t *mem;    /* where the DLL reads it */
    DWORD          size;   /* bytes */
    DWORD          stride;
    int            from;      /* X8_RING: where `size` came from (X8_FROM_*, the window trace's) */
    DWORD          allocated; /* X8_RING: its record's MORE.dwBytesAllocated (the window trace's) */
    int            unusable;  /* x8_buffer found none: why (X8_NO_*, BUFFERBLT's trace) */
    pbuf_t        *rt;        /* X8_RING, with diag_on: the runtime's own buffer in the pool (pool_rt_window) */
} x8_src_t;

#define X8_NO_RECORD 1 /* the handle names no buffer's record (the lookup's trace says more, but for handle 0) */
#define X8_NO_MEMORY 2 /* its record has no memory */
#define X8_NO_SIZE   3 /* nor any size the DLL can read */

#define X8_FROM_POOL  0 /* the pool's own record of a buffer in system memory */
#define X8_FROM_PITCH 1 /* lPitch: DirectDraw's own buffer */
#define X8_FROM_UM    2 /* the call's user memory: dwVertexLength vertices */

/* The largest index buffer a DRAWINDEXEDPRIMITIVE2 marks as the
   runtime's for good (its own is 4 KB). */
#define RT_IB_MAX 65536

typedef struct dp2 {
    d3d_ctx_t     *c;
    const uint8_t *vtx;        /* the call's vertices, when they go through the ring */
    DWORD          nvtx;       /* their number (0 when the GPU reads them from the pool): a buffer of the DLL's from
                                  dwVertexOffset to its end, others' those the call names; at most `room` */
    DWORD          named;      /* the call's dwVertexLength, at most `room` (the report's figures) */
    DWORD          limit;      /* the vertices sent from: those the call names, nvtx once a draw reaches past them */
    DWORD          sent_top;   /* the end of the highest chunk of them in the inline buffer: _IMM vertices go above */
    DWORD          room;       /* vertices the inline buffer's vertex area holds at this stride */
    DWORD          stride;
    DWORD          fvf;
    DWORD          decl[2 * MAX_ELEMENTS];
    DWORD          n_decl;     /* elements */
    pbuf_t        *pool;       /* the pool buffer the GPU reads the call's vertices from, or NULL */
    const uint8_t *pool_mem;   /* where the DLL reads them (the texture audit's VSUM notes) */
    DWORD          pool_addr;  /* their GPU address */
    DWORD          pool_size;  /* the bytes STREAM names: the buffer from dwVertexOffset to its end */
    DWORD          pool_used;  /* the bytes of the call's vertex range (the report's figures) */
    DWORD          pool_version;
    int            pool_read;  /* a draw read them */
    int            pool_kept;  /* from the buffer's kept block (its contents from before its Lock) */
    int            stream;     /* STREAM_*: what the GPU's stream 0 is */
    int            setup;      /* the GPU holds the call's declaration and index buffer */
    DWORD          setup_epoch; /* sb->state_epoch then: a GPU reset since drops them */
    int            tex_listed; /* the context's textures are in the batch's list */
    DWORD          sent[CHUNK_WORDS]; /* chunks of the call's vertices in the GPU's inline buffer */
    DWORD          want[CHUNK_WORDS]; /* chunks the draw being sent lacks */
    int            imm;        /* drawing inline vertices: the call's are not involved */
    int            ok;         /* the batch is still good: the GPU is not held, and no packet failed */
    int            drawable;   /* the call's vertex format is one the GPU takes */
    int            drew;
    /* Diagnostics: vertices sent from the call's buffer, and sent again;
       indices sent. */
    DWORD          sent_n;
    DWORD          again_n;
    DWORD          idx_n;
    DWORD          ever[CHUNK_WORDS]; /* chunks sent at least once */
    uint64_t       cyc_state;  /* rdtsc in the state flushes */
    /* The vertex audit (diag_audit): the command being read, the vertices
       the call names (dwVertexLength, from dwVertexOffset), and those the
       GPU can read from there (the stream's, or nvtx through the ring). */
    DWORD          op;
    DWORD          call_vertices;
    DWORD          call_offset;
    DWORD          gpu_vertices;

    /* A DirectX 8 context's call (x8_ functions) */
    int            x8;
    int            held;        /* no batch is open (the GPU held, the context stopped) */
    const uint8_t *um;          /* the call's user memory (USERMEMVERTICES): stream 0 after SETSTREAMSOURCEUM ... */
    DWORD          um_vertices; /* ... its vertices (dwVertexLength) */
    int            x8_decl_ok;  /* decl[] holds the declaration of vertex shader x8_vs ... */
    DWORD          x8_vs;
    int            x8_indexed;  /* ... with indexed vertex blending on or off (an FVF code's last weight) ... */
    int            x8_usable;   /* ... one the GPU can read ... */
    DWORD          x8_streams;  /* ... from these streams ... */
    DWORD          x8_blend_from; /* ... its matrix indices from (X8_IDX_*, the dx8 blending line) ... */
    int            x8_has_w;      /* ... and whether it has BLENDWEIGHT */
    int            x8_pieces;   /* drawing a split draw's pieces */
    int            x8_window;   /* the draw command names a vertex window (the stream audit's): MinIndex ... */
    DWORD          x8_min;
    DWORD          x8_nverts;   /* ... and NumVertices */
    int            x8_dip2;     /* drawing a DRAWINDEXEDPRIMITIVE2: its index buffer is the runtime's (pbuf_t.runtime_ib) */
    DWORD          x8_raw_op;   /* the DRAWINDEXEDPRIMITIVE(2) being drawn (0 none) and its record's dwords (the window trace's) */
    DWORD          x8_raw[6];
    int            x8_raw_told; /* that record was traced ... */
    int            x8_trace_range; /* ... so the stream audit names the draw's indices' range */
    DWORD          x8_top;      /* bytes of the inline buffer's vertex area the call's DirectX 8 vertices fill */
    DWORD          x8_epoch;    /* sb->state_epoch when they went there (a reset loses them) */
    struct {
        const uint8_t *mem;     /* a stream's last window there: its source, stride, vertices [lo, hi) at byte `at` (hi 0: none) */
        DWORD          stride;
        DWORD          lo;
        DWORD          hi;
        DWORD          at;
    } x8_sent[D3D_STREAMS];
    /* The buffers the call's draws resolved, by stream and (D3D_STREAMS) the
       index buffer: the handle and stride bound (handle 0: none) at buf_epoch
       `epoch`. A buffer's place moves only when one is recorded or forgotten,
       or by BUFFERBLT (which drops these). */
    struct {
        DWORD    handle;
        DWORD    stride;
        DWORD    epoch;
        x8_src_t x;
    } x8_buf[D3D_STREAMS + 1];
} dp2_t;

/* What the GPU holds of the DLL's setup, kept between batches until a reset
   (under the ring lock, shared by every process): the vertex declaration,
   the index buffer and stream 0 as last sent, in the GPU state epoch
   `gpu_epoch`. */
static DWORD gpu_epoch;
static int   gpu_have_decl;
static int   gpu_have_indices;
static DWORD gpu_have_streams; /* bit s: gpu_streams[s] holds what the GPU has */
static DWORD gpu_n_decl;
static DWORD gpu_decl[2 * MAX_ELEMENTS];
static DWORD gpu_streams[D3D_STREAMS][VGPU98_LEN_STREAM];
static DWORD gpu_indices[VGPU98_LEN_INDICES];
/* Command set 2.2's (with PROFILE.PROGRAMS): the programs BIND last named,
   and the contexts (their tokens) whose vertex and pixel constants the GPU
   holds. After a reset neither is known (a ring rewound may have left a
   BIND it ran). */
static int   gpu_have_bind;
static DWORD gpu_bind[2];
static DWORD gpu_vconst_ctx;
static DWORD gpu_pconst_ctx;
/* Past the highest vertex constant the GPU may hold non-zero (all of them
   after a reset: a ring rewound may have left any). */
static DWORD gpu_vconst_hi = D3D_VS_CONSTS;

/* A reset since the record was made: the GPU holds none of it. */
static void
gpu_setup_current(void)
{
    if (gpu_epoch != sb->state_epoch) {
        gpu_epoch        = sb->state_epoch;
        gpu_have_decl    = 0;
        gpu_have_indices = 0;
        gpu_have_streams = 0;
        gpu_have_bind    = 0;
        gpu_vconst_ctx   = 0;
        gpu_pconst_ctx   = 0;
        gpu_vconst_hi    = D3D_VS_CONSTS;
    }
}

/* DECLARE, STREAM and INDICES, each only when the GPU holds another. */
static int
send_declare(const DWORD *decl, DWORD n)
{
    volatile DWORD *p;
    DWORD           i;

    gpu_setup_current();
    if (gpu_have_decl && (gpu_n_decl == n) && !dwords_differ(gpu_decl, decl, 2 * n))
        return 1;
    if (!(p = gpu_packet(VGPU98_OP_DECLARE, 1 + 2 * n)))
        return 0;
    pkt_put(&p, n);
    for (i = 0; i < 2 * n; i++)
        pkt_put(&p, decl[i]);
    memcpy(gpu_decl, decl, 2 * n * sizeof(DWORD));
    gpu_n_decl    = n;
    gpu_have_decl = 1;
    DIAG(diag.sent_declare++);
    return 1;
}

static int
send_stream_words(const DWORD *w)
{
    const DWORD     s = w[0];
    volatile DWORD *p;
    DWORD           i;

    gpu_setup_current();
    if (((gpu_have_streams >> s) & 1) && !dwords_differ(gpu_streams[s], w, VGPU98_LEN_STREAM))
        return 1;
    if (!(p = gpu_packet(VGPU98_OP_STREAM, VGPU98_LEN_STREAM)))
        return 0;
    for (i = 0; i < VGPU98_LEN_STREAM; i++)
        pkt_put(&p, w[i]);
    memcpy(gpu_streams[s], w, sizeof(gpu_streams[s]));
    gpu_have_streams |= 1u << s;
    DIAG(diag.sent_stream++);
    return 1;
}

/* A program the GPU lacks (never sent, or lost to a reset): PROGRAM. The
   GPU unbinds a replaced program of the other kind, so BIND goes again. */
static int
send_program(d3d_prog_t *g)
{
    volatile DWORD *p;
    DWORD           i;

    if (g->epoch == sb->state_epoch)
        return 1;
    if (!(p = gpu_packet(VGPU98_OP_PROGRAM, VGPU98_LEN_PROGRAM + VGPU98_PROG_INS_DWORDS * g->n)))
        return 0;
    pkt_put(&p, g->id);
    pkt_put(&p, g->kind);
    pkt_put(&p, g->range);
    for (i = 0; i < VGPU98_PROG_INS_DWORDS * g->n; i++)
        pkt_put(&p, g->ins[i]);
    g->epoch      = sb->state_epoch;
    DIAG({
        diag.prog_sent++;
        diag.prog_sent_dw += VGPU98_LEN_PROGRAM + VGPU98_PROG_INS_DWORDS * g->n;
    });
    gpu_have_bind = 0;
    return 1;
}

/* CONSTANTS for the registers `dirty` marks (a bit each) of [0, count), or
   all of them, a packet a run of consecutive ones. */
static int
send_const_runs(DWORD kind, const DWORD *dirty, DWORD count, int all, DWORD (*v)[4])
{
    volatile DWORD *p;
    DWORD           r = 0;
    DWORD           e;
    DWORD           i;
    DWORD           k;

    while (r < count) {
        if (!all && !((dirty[r / 32] >> (r % 32)) & 1)) {
            r++;
            continue;
        }
        for (e = r + 1; (e < count) && (all || ((dirty[e / 32] >> (e % 32)) & 1)); e++)
            ;
        DIAG({
            diag.prog_consts++;
            diag.prog_const_regs += e - r;
        });
        if (!(p = gpu_packet(VGPU98_OP_CONSTANTS, 2 + 4 * (e - r))))
            return 0;
        pkt_put(&p, kind);
        pkt_put(&p, r);
        for (i = r; i < e; i++) {
            for (k = 0; k < 4; k++)
                pkt_put(&p, v[i][k]);
        }
        r = e;
    }
    return 1;
}

/* Every vertex constant the context's program may read, when the GPU holds
   another context's: the context's own, then zeros up to the highest the
   GPU may hold non-zero (a register the context never set reads 0, not
   another context's value). */
static int
send_vconst_all(const d3d_ctx_t *c)
{
    const DWORD     hi = (c->vconst_hi > gpu_vconst_hi) ? c->vconst_hi : gpu_vconst_hi;
    volatile DWORD *p;
    DWORD           r;
    DWORD           k;

    if (hi) {
        DIAG({
            diag.prog_consts++;
            diag.prog_const_regs += hi;
        });
        if (!(p = gpu_packet(VGPU98_OP_CONSTANTS, 2 + 4 * hi)))
            return 0;
        pkt_put(&p, VGPU98_PROG_VERTEX);
        pkt_put(&p, 0);
        for (r = 0; r < hi; r++) {
            for (k = 0; k < 4; k++)
                pkt_put(&p, (c->vconst && (r < c->vconst_hi)) ? c->vconst[r][k] : 0);
        }
    }
    gpu_vconst_hi = c->vconst_hi;
    return 1;
}

static int
send_indices_words(const DWORD *w)
{
    volatile DWORD *p;
    DWORD           i;

    gpu_setup_current();
    if (gpu_have_indices && !dwords_differ(gpu_indices, w, VGPU98_LEN_INDICES))
        return 1;
    if (!(p = gpu_packet(VGPU98_OP_INDICES, VGPU98_LEN_INDICES)))
        return 0;
    for (i = 0; i < VGPU98_LEN_INDICES; i++)
        pkt_put(&p, w[i]);
    memcpy(gpu_indices, w, sizeof(gpu_indices));
    gpu_have_indices = 1;
    DIAG(diag.sent_indices++);
    return 1;
}

/* Before every DRAW, with PROFILE.PROGRAMS: the programs the context's
   shaders draw with (a DirectX 8 context's vertex shader with a function,
   its pixel shader; none for every other draw) sent when the GPU lacks
   them, BIND when the GPU holds another pair (another context's draws
   bound theirs), and the constants of a kind a bound program reads: all
   of them when the GPU holds another context's, else those set since they
   went. */
static int
send_programs(dp2_t *d)
{
    d3d_ctx_t      *c       = d->c;
    const DWORD     tok     = ctx_token(c);
    d3d_vs_t       *vs      = NULL;
    d3d_ps_t       *ps      = NULL;
    DWORD           want[2] = { 0, 0 };
    volatile DWORD *p;
    DWORD           i;

    if (!d3d8_programs)
        return 1;
    gpu_setup_current();
    if (d->x8) {
        if ((c->vs & 1) && (vs = d3d8_vs_find(c, c->vs)) && vs->prog.id)
            want[0] = vs->prog.id;
        if (c->ps && (ps = d3d8_ps_find(c, c->ps)) && ps->prog.id)
            want[1] = ps->prog.id;
    }
    DIAG({
        diag.prog_draws[0] += want[0] != 0;
        diag.prog_draws[1] += want[1] != 0;
        if (want[0])
            vs->drawn++;
        if (want[1])
            ps->drawn++;
        if (d->x8 && c->ps && !want[1])
            diag.ps_draws_unknown++; /* a handle bound that names no shader kept */
    });
    if ((want[0] && !send_program(&vs->prog)) || (want[1] && !send_program(&ps->prog)))
        return 0;
    if (!gpu_have_bind || (gpu_bind[0] != want[0]) || (gpu_bind[1] != want[1])) {
        if (!(p = gpu_packet(VGPU98_OP_BIND, VGPU98_LEN_BIND)))
            return 0;
        pkt_put(&p, want[0]);
        pkt_put(&p, want[1]);
        gpu_bind[0]   = want[0];
        gpu_bind[1]   = want[1];
        gpu_have_bind = 1;
        DIAG(diag.prog_binds++);
    }
    if (want[0]) {
        DWORD dirty = 0;

        for (i = 0; i < ARRAY_SIZE(c->vconst_dirty); i++)
            dirty |= c->vconst_dirty[i];
        if (gpu_vconst_ctx != tok) {
            if (!send_vconst_all(c))
                return 0;
            memset(c->vconst_dirty, 0, sizeof(c->vconst_dirty));
            gpu_vconst_ctx = tok;
        } else if (dirty && c->vconst) {
            if (!send_const_runs(VGPU98_PROG_VERTEX, c->vconst_dirty, D3D_VS_CONSTS, 0, c->vconst))
                return 0;
            memset(c->vconst_dirty, 0, sizeof(c->vconst_dirty));
            if (c->vconst_hi > gpu_vconst_hi)
                gpu_vconst_hi = c->vconst_hi;
        }
    }
    if (want[1] && ((gpu_pconst_ctx != tok) || c->pconst_dirty)) {
        if (!send_const_runs(VGPU98_PROG_PIXEL, &c->pconst_dirty, D3D_PS_CONSTS, gpu_pconst_ctx != tok, c->ps_const))
            return 0;
        c->pconst_dirty = 0;
        gpu_pconst_ctx  = tok;
    }
    return 1;
}

/* An index list as the runtime holds it: 16-bit indices one after another,
   or INDEXEDTRIANGLELIST's records (wV1, wV2, wV3, wFlags). */
typedef struct idx_src {
    const uint8_t *p;
    int            records;
} idx_src_t;

static DWORD
rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static DWORD
rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD) p[3] << 24);
}

/* A float's bits: a NaN, an infinity. */
static int
f32_nan(DWORD b)
{
    return ((b & 0x7f800000u) == 0x7f800000u) && (b & 0x007fffffu);
}

static int
f32_inf(DWORD b)
{
    return (b & 0x7fffffffu) == 0x7f800000u;
}

static float
rdf(const uint8_t *p)
{
    union {
        DWORD u;
        float f;
    } c;

    c.u = rd32(p);
    return c.f;
}

static DWORD
idx_get(const idx_src_t *s, DWORD i)
{
    if (s->records)
        return rd16(s->p + 8 * (i / 3) + 2 * (i % 3));
    return rd16(s->p + 2 * i);
}

/* --- Vertices ---------------------------------------------------------------- */

/* A vertex format code as a declaration of stream 0: the runtime's
   vertices go to the GPU byte for byte, and the GPU reads the fields where
   they are. Transformed vertices (XYZRHW) take the pass-through program;
   untransformed ones (XYZ, or XYZB1-5 with 1-5 blending weights) the
   transform, with a normal if they have one. DirectX 8 (`dx8`) adds the
   point size (DirectX 7's reserved dword, in the same place) and, with
   LASTBETA_UBYTE4 or with indexed vertex blending on (`indexed`: the SDK
   has the matrix indices "immediately after the last vertex weight"
   whenever INDEXEDVERTEXBLENDENABLE is), the last weight dword as four
   matrix indices. Returns the elements (0 for a format the GPU cannot read)
   and the stride. */
static DWORD
fvf_decl(DWORD fvf, int dx8, int indexed, DWORD *decl, DWORD *stride)
{
    const DWORD pos  = fvf & D3DFVF_POSITION_MASK;
    DWORD       sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    DWORD       off;
    DWORD       n = 0;
    DWORD       i;

    if (sets > 8)
        return 0;
    if (pos == D3DFVF_XYZRHW) {
        decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT4, VGPU98_DU_POSITIONT, 0);
        decl[n++] = 0;
        off       = 16;
        if (fvf & D3DFVF_NORMAL)
            off += 12; /* unused: the vertices are lit already */
    } else if ((pos == D3DFVF_XYZ) || ((pos >= D3DFVF_XYZB1) && (pos <= D3DFVF_XYZB5))) {
        /* XYZB1-5 carry 1-5 weight dwords after the position; the transform
           uses at most three weights (four matrices). */
        const DWORD betas   = (pos == D3DFVF_XYZ) ? 0 : (pos - D3DFVF_XYZB1) / 2 + 1;
        const DWORD indices = dx8 && betas && ((fvf & D3DFVF_LASTBETA_UBYTE4) || indexed);
        const DWORD weights = betas - indices;

        decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT3, VGPU98_DU_POSITION, 0);
        decl[n++] = 0;
        off       = 12;
        if (weights) {
            decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT1 + ((weights > 4) ? 4 : weights) - 1, VGPU98_DU_BLENDWEIGHT, 0);
            decl[n++] = off;
            off += 4 * weights;
        }
        if (indices) {
            decl[n++] = VGPU98_DECL(0, VGPU98_DT_UBYTE4, VGPU98_DU_BLENDINDICES, 0);
            decl[n++] = off;
            off += 4;
        }
        if (fvf & D3DFVF_NORMAL) {
            decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT3, VGPU98_DU_NORMAL, 0);
            decl[n++] = off;
            off += 12;
        }
    } else
        return 0;
    if (fvf & D3DFVF_RESERVED1) {
        /* DirectX 8's point size; D3DLVERTEX's reserved dword before it. */
        if (dx8) {
            decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT1, VGPU98_DU_PSIZE, 0);
            decl[n++] = off;
        }
        off += 4;
    }
    if (fvf & D3DFVF_DIFFUSE) {
        decl[n++] = VGPU98_DECL(0, VGPU98_DT_D3DCOLOR, VGPU98_DU_COLOR, 0);
        decl[n++] = off;
        off += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        decl[n++] = VGPU98_DECL(0, VGPU98_DT_D3DCOLOR, VGPU98_DU_COLOR, 1);
        decl[n++] = off;
        off += 4;
    }
    for (i = 0; i < sets; i++) {
        /* DirectX 7's coordinate sizes: 0 two floats, 1 three, 2 four, 3 one. */
        static const DWORD sizes[4] = { 2, 3, 4, 1 };
        const DWORD        size     = sizes[(fvf >> (16 + 2 * i)) & 3];

        decl[n++] = VGPU98_DECL(0, VGPU98_DT_FLOAT1 + size - 1, VGPU98_DU_TEXCOORD, i);
        decl[n++] = off;
        off += 4 * size;
    }
    *stride = off;
    return n / 2;
}

/* The call's vertex format (DirectX 6 and 7). */
static int
fvf_parse(dp2_t *d, DWORD fvf)
{
    DWORD stride;

    d->n_decl = fvf_decl(fvf, 0, 0, d->decl, &stride);
    if (!d->n_decl)
        return 0;
    d->stride = stride;
    d->fvf    = fvf;
    d->room   = VTX_BYTES / stride;
    if (d->room > CALL_VERTICES)
        d->room = CALL_VERTICES;
    return 1;
}

/* Writes `bytes` (a multiple of 4) into the GPU's inline buffer at `offset`,
   in packets of at most MAX_PAYLOAD dwords. */
static int
send_inline(DWORD offset, const void *src, DWORD bytes)
{
    const uint8_t *s = src;

    while (bytes) {
        const DWORD     n = (bytes > 4 * (MAX_PAYLOAD - 1)) ? 4 * (MAX_PAYLOAD - 1) : bytes;
        volatile DWORD *p = gpu_packet(VGPU98_OP_INLINE, 1 + n / 4);

        if (!p)
            return 0;
        pkt_put(&p, offset);
        copy_dwords(p, s, n / 4);
        s += n;
        offset += n;
        bytes -= n;
    }
    return 1;
}

/* Vertices as the runtime holds them, into the inline buffer as vertex
   `first` onward. */
static int
send_vertices(dp2_t *d, DWORD first, const uint8_t *src, DWORD count)
{
    return send_inline(first * d->stride, src, count * d->stride);
}

/* The call's declaration and the index area, before its first draw and
   again after a GPU reset (which also loses the stream and the inline
   buffer's vertices); each packet only when the GPU does not hold it from
   an earlier batch already. */
/* INDICES over the inline buffer's index area, 16-bit. */
static const DWORD idx_area[VGPU98_LEN_INDICES] = { 2, IDX_AT, IDX_BYTES, 0 };

static int
send_setup(dp2_t *d)
{
    if (d->setup && (d->setup_epoch == sb->state_epoch))
        return 1;
    if (d->setup) {
        memset(d->sent, 0, sizeof(d->sent));
        d->sent_top = 0;
    }
    d->stream = STREAM_NONE;
    if (!send_declare(d->decl, d->n_decl) || !send_indices_words(idx_area))
        return 0;
    d->setup       = 1;
    d->setup_epoch = sb->state_epoch;
    return 1;
}

/* Stream 0 for the next draw: the call's vertices in the pool (the buffer
   from them to its end, at its version), or the inline buffer (the call's vertices
   sent through the ring, or a command's own); sent only when the GPU's
   differs. send_setup comes first. */
static int
send_stream(dp2_t *d, int which)
{
    DWORD w[VGPU98_LEN_STREAM];

    if (d->stream == which)
        return 1;
    w[0] = 0;
    if (which == STREAM_POOL) {
        w[1] = VGPU98_SRC_GPUVM;
        w[2] = d->pool_addr;
        w[3] = d->pool_size;
        w[4] = d->stride;
        w[5] = d->pool_version;
    } else {
        w[1] = VGPU98_SRC_INLINE;
        w[2] = 0;
        w[3] = VTX_BYTES;
        w[4] = d->stride;
        w[5] = 0;
    }
    if (!send_stream_words(w))
        return 0;
    d->stream = which;
    return 1;
}

/* Before a drawing command: the context's state, unless the GPU holds all
   of it already (the context is clean, still owns the GPU's state, and the
   GPU was not reset since). The vertices go with each draw (send_needed).
   The textures the context draws with are the batch's, kept from an earlier
   call or listed by the flush. */
static int
prepare(dp2_t *d)
{
    d3d_ctx_t *c = d->c;
    uint64_t   t0;
    int        r;

    if (!d->ok || !d->drawable)
        return 0;
    if (c->clean && (sb->state_owner == ctx_handle(c)) && (c->sent_epoch == sb->state_epoch)) {
        DIAG(diag.flushes_skipped++);
        if (!d->tex_listed) {
            /* The call's first draw: its state kept from the call before. */
            DIAG(diag.calls_kept++);
            ctx_batch_textures(c);
            d->tex_listed = 1;
        }
        d->drew = 1;
        return 1;
    }
    /* Without a usable target this draw is skipped; the call's other
       commands (a later SETRENDERTARGET, a CLEAR, a TEXBLT) still count. */
    t0 = diag_on ? rdtsc() : 0;
    GUARD_STEP(GUARD_STEP_STATE, 0, 0);
    r  = ctx_flush(c, 1);
    GUARD_STEP(GUARD_STEP_NONE, 0, 0);
    DIAG(d->cyc_state += rdtsc() - t0);
    if (r <= 0) {
        if (!r)
            d->ok = 0;
        return 0;
    }
    d->tex_listed = 1;
    d->drew       = 1;
    return 1;
}

/* --- DRAW packets ---------------------------------------------------------------- */

static int
chunk_wanted(const dp2_t *d, DWORD c)
{
    return (d->want[c >> 5] >> (c & 31)) & 1;
}

/* Sends the chunks of the call's vertices that a draw uses and the GPU's
   inline buffer lacks: for an indexed draw, the chunks its indices touch;
   otherwise its range. Indices past the call's vertices are left to the GPU
   (Direct3D leaves them undefined). */
static int
send_needed(dp2_t *d, DWORD first, DWORD count, const idx_src_t *ind)
{
    DWORD nchunks;
    DWORD nwords;
    DWORD c;
    DWORD i;

    DWORD top = 0; /* past the draw's furthest vertex the call has */

    if (d->imm || !d->nvtx || !count)
        return 1;
    nchunks = ((d->nvtx - 1) >> CHUNK_SHIFT) + 1;
    nwords  = ((nchunks - 1) >> 5) + 1;
    if (ind) {
        for (i = 0; i < count; i++) {
            const DWORD v = first + idx_get(ind, i);

            if (v < d->nvtx) {
                c = v >> CHUNK_SHIFT;
                d->want[c >> 5] |= 1u << (c & 31);
                if (v >= top)
                    top = v + 1;
            }
        }
    } else {
        DWORD hi = first + count - 1;

        if (first >= d->nvtx)
            return 1;
        if (hi >= d->nvtx)
            hi = d->nvtx - 1;
        for (c = first >> CHUNK_SHIFT; c <= (hi >> CHUNK_SHIFT); c++)
            d->want[c >> 5] |= 1u << (c & 31);
        top = hi + 1;
    }
    /* A draw past the vertices the call names: the rest of the buffer's
       become the call's, and the chunk cut short at them goes again. */
    if (top > d->limit) {
        if (d->limit & ((1u << CHUNK_SHIFT) - 1)) {
            c = d->limit >> CHUNK_SHIFT;
            d->sent[c >> 5] &= ~(1u << (c & 31));
        }
        d->limit = d->nvtx;
    }
    for (i = 0; i < nwords; i++) {
        d->want[i] &= ~d->sent[i];
        d->sent[i] |= d->want[i];
    }

    /* Runs of chunks, each a few INLINE packets. */
    for (c = 0; c < nchunks;) {
        DWORD run;
        DWORD start;
        DWORD end;

        if (!(c & 31) && !d->want[c >> 5]) {
            c += 32;
            continue;
        }
        if (!chunk_wanted(d, c)) {
            c++;
            continue;
        }
        run = c;
        while ((c < nchunks) && chunk_wanted(d, c))
            c++;
        start = run << CHUNK_SHIFT;
        end   = c << CHUNK_SHIFT;
        if (end > d->limit)
            end = d->limit;
        if (!send_vertices(d, start, d->vtx + start * d->stride, end - start))
            return 0;
        if (end > d->sent_top)
            d->sent_top = end;
        if (diag_on) {
            DWORD k;

            d->sent_n += end - start;
            for (k = run; k < c; k++) {
                if ((d->ever[k >> 5] >> (k & 31)) & 1)
                    d->again_n += ((k + 1) << CHUNK_SHIFT <= d->limit) ? (1u << CHUNK_SHIFT) : d->limit - (k << CHUNK_SHIFT);
                d->ever[k >> 5] |= 1u << (k & 31);
            }
        }
    }
    for (i = 0; i < nwords; i++)
        d->want[i] = 0;
    return 1;
}

/* The draw's indices into the index area, as 16-bit values. */
static int
send_indices(dp2_t *d, const idx_src_t *ind, DWORD count)
{
    DWORD at = 0;

    DIAG(d->idx_n += count);
    if (!ind->records) {
        /* As the runtime holds them: pairs of indices are dwords. */
        if (!send_inline(IDX_AT, ind->p, 2 * (count & ~1u)))
            return 0;
        if (count & 1) {
            volatile DWORD *p = gpu_packet(VGPU98_OP_INLINE, 2);

            if (!p)
                return 0;
            pkt_put(&p, IDX_AT + 2 * (count - 1));
            pkt_put(&p, rd16(ind->p + 2 * (count - 1)));
        }
        return 1;
    }
    while (at < count) {
        const DWORD     per = 2 * (MAX_PAYLOAD - 1);
        const DWORD     n   = (count - at > per) ? per : count - at;
        volatile DWORD *p   = gpu_packet(VGPU98_OP_INLINE, 1 + (n + 1) / 2);
        DWORD           i;

        if (!p)
            return 0;
        pkt_put(&p, IDX_AT + 2 * at);
        for (i = 0; i < n; i += 2) {
            const DWORD lo = idx_get(ind, at + i);
            const DWORD hi = (i + 1 < n) ? idx_get(ind, at + i + 1) : 0;

            pkt_put(&p, lo | (hi << 16));
        }
        at += n;
    }
    return 1;
}

/* The vertex audit (Diagnostics "DirectX 7: vertex audit"): a draw's
   furthest vertex against the vertices its call names (dwVertexLength) and
   those the GPU can read (past them it reads 0 from the pool, and whatever
   the inline buffer still holds through the ring). Indices are read only
   here. Logged the first three times, and again after 10 seconds without
   one; all counted. */
#define AUDIT_LOGGED   3
#define AUDIT_QUIET_MS 10000

static DWORD audit_logged;
static DWORD audit_ms;

static void
audit_draw(const dp2_t *d, DWORD first, DWORD count, const idx_src_t *ind)
{
    DWORD hi = 0;
    DWORD now;
    DWORD i;

    if (d->imm || !count)
        return;
    if (ind) {
        for (i = 0; i < count; i++) {
            const DWORD x = idx_get(ind, i);

            if (x > hi)
                hi = x;
        }
        hi += first;
    } else
        hi = first + count - 1;
    diag.audit_draws++;
    if (hi < d->call_vertices)
        return;
    diag.audit_past_call++;
    if (hi >= d->gpu_vertices)
        diag.audit_past_gpu++;
    if (!d->pool)
        diag.audit_ring++;
    if (hi + 1 - d->call_vertices > diag.audit_furthest)
        diag.audit_furthest = hi + 1 - d->call_vertices;
    now = GetTickCount();
    if (audit_logged && (now - audit_ms >= AUDIT_QUIET_MS))
        audit_logged = 0;
    audit_ms = now;
    if (audit_logged >= AUDIT_LOGGED)
        return;
    audit_logged++;
    /* Short: a line holds 160 characters (the least telling part last). */
    dbg(DLL_TAG ": vertex audit: command %u, FVF %x, stride %u: %u from %u%s read vertex %u; call %u, GPU %u from %u (%s)",
        d->op, d->fvf, d->stride, count, first, ind ? ", indexed" : "", hi, d->call_vertices, d->gpu_vertices,
        d->call_offset, d->pool ? "pool" : "ring");
}

/* The depth test a draw makes, for the report: by the ZENABLE the GPU has
   where it has a depth buffer (the TARGET sent), and by that buffer's
   bits. */
static void
diag_depth(const d3d_ctx_t *c)
{
    const DWORD zf   = c->sent_target[5] >> 16;
    const DWORD mode = zf ? c->sent_rs[VGPU98_RS_ZENABLE] : 0;

    if (mode > 2)
        return;
    diag.draws_depth[mode]++;
    if (mode && (zf == VGPU98_FMT_D16))
        diag.draws_d16++;
    else if (mode)
        diag.draws_d24++;
}

/* A WINFO taken, for the report. */
static void
diag_winfo(float wn, float wf, int infinite)
{
    if (!diag.winfo++)
        diag.wnear_lo = diag.wnear_hi = wn;
    else if (wn < diag.wnear_lo)
        diag.wnear_lo = wn;
    else if (wn > diag.wnear_hi)
        diag.wnear_hi = wn;
    if (infinite)
        diag.winfo_infinite++;
    else if (!diag.winfo_finite++)
        diag.wfar_lo = diag.wfar_hi = wf;
    else if (wf < diag.wfar_lo)
        diag.wfar_lo = wf;
    else if (wf > diag.wfar_hi)
        diag.wfar_hi = wf;
}

/* --- The texture audit's vertex race detector ---

   A draw that reads a stream where its buffer is (by GPU address: a pool
   buffer, not the ring's copy) has its vertex window hashed as the DLL
   sends it, into a VSUM note (a NOP) just before the DRAW: eight sums of
   the window in eight vertex ranges, and the buffer's last Lock. The GPU
   hashes the same bytes when it reads them; a difference is a write the
   draw did not wait for. At most VSUM_BUDGET bytes a second (every process
   together) in at most VSUM_NOTES notes, a window's first
   VGPU98_NOTE_VSUM_MAX bytes, for the buffers a write may race (dynamic,
   Locked since their last note, locked at the draw, the runtime's own);
   quiet buffers (none of these) have VSUM_QUIET_BUDGET and
   VSUM_QUIET_NOTES of their own, so static meshes drawn first in a frame
   leave the budget to the buffers that change. */
#define VSUM_BUDGET       (1u << 20)
#define VSUM_NOTES        4096
#define VSUM_QUIET_BUDGET (1u << 17)
#define VSUM_QUIET_NOTES  512

_Static_assert(VGPU98_LEN_NOTE_VSUM == 7 + VGPU98_NOTE_VSUM_PARTS + 1, "VSUM: seven words, the sums, the check");

static DWORD vsum_second;   /* GetTickCount at the budget's second ... */
static DWORD vsum_bytes[2]; /* ... the bytes hashed in it ... */
static DWORD vsum_count[2]; /* ... and its notes ([1]: quiet buffers') */

static DWORD proc_note_pid;   /* the process the last PROC note named (0: none, or a new context's) ... */
static DWORD proc_note_epoch; /* ... and sb->state_epoch then (a GPU reset may have forgotten it) */

void
dp2_proc_again(void)
{
    __atomic_store_n(&proc_note_pid, 0, __ATOMIC_RELAXED);
}

/* A note's sequence, never 0, shared with the VSUM notes. */
static DWORD
note_seq(void)
{
    DWORD s = __atomic_add_fetch(&sb->note_seq, 1, __ATOMIC_RELAXED);

    return s ? s : __atomic_add_fetch(&sb->note_seq, 1, __ATOMIC_RELAXED);
}

/* The check word: a note's other dwords hashed (VGPU98_NOTE_HASH). */
static DWORD
note_check(const DWORD *w, DWORD n)
{
    DWORD h = VGPU98_NOTE_HASH_SEED;
    DWORD k;

    for (k = 0; k + 1 < n; k++)
        h = (h ^ w[k]) * VGPU98_NOTE_HASH_PRIME;
    return h;
}

/* With diag_textures, at a batch's start and before a draw: the drawing
   process's name (c's, found at ContextCreate) in a PROC note when it is
   not the one the last named, or a GPU reset came since,
   so the GPU's texture audit
   lines name it. 0 if a packet failed. */
static int
proc_note(const d3d_ctx_t *c)
{
    const DWORD     pid = GetCurrentProcessId();
    DWORD           w[VGPU98_LEN_NOTE_PROC];
    char            name[17];
    volatile DWORD *p;
    DWORD           k;

    if ((__atomic_load_n(&proc_note_pid, __ATOMIC_RELAXED) == pid) && (proc_note_epoch == sb->state_epoch))
        return 1;
    memset(name, 0, sizeof(name));
    for (k = 0; c->exe[k] && (k + 1 < sizeof(name)); k++)
        name[k] = c->exe[k];
    w[0] = VGPU98_NOTE_PROC;
    w[1] = note_seq();
    for (k = 0; k < 4; k++)
        w[2 + k] = (BYTE) name[4 * k] | ((DWORD) (BYTE) name[4 * k + 1] << 8) | ((DWORD) (BYTE) name[4 * k + 2] << 16) |
                   ((DWORD) (BYTE) name[4 * k + 3] << 24);
    w[VGPU98_LEN_NOTE_PROC - 1] = note_check(w, VGPU98_LEN_NOTE_PROC);
    if (!(p = gpu_packet(VGPU98_OP_NOP, VGPU98_LEN_NOTE_PROC)))
        return 0;
    for (k = 0; k < VGPU98_LEN_NOTE_PROC; k++)
        pkt_put(&p, w[k]);
    __atomic_store_n(&proc_note_pid, pid, __ATOMIC_RELAXED);
    proc_note_epoch = sb->state_epoch;
    return 1;
}

/* VGPU98_NOTE_HASH over n bytes: FNV-1a over little-endian dwords, then each
   byte past the last whole dword as a dword of its own. */
static DWORD
vsum_hash(const uint8_t *p, DWORD n, DWORD h)
{
    DWORD i;

    for (i = 0; i + 4 <= n; i += 4)
        h = (h ^ rd32(p + i)) * VGPU98_NOTE_HASH_PRIME;
    for (; i < n; i++)
        h = (h ^ p[i]) * VGPU98_NOTE_HASH_PRIME;
    return h;
}

/* A buffer no write is expected to race: static, not Locked since its last
   note, not locked at the draw, an application's. */
static int
vsum_quiet(const pbuf_t *b)
{
    return b && !b->dynamic && !b->tt_since && !b->locked && (b->kind != PB_IMPLICIT);
}

/* Whether this second's budget (a quiet buffer's, or the others') has room
   for `len` more bytes in a note; asked with 0 before a draw's indices are
   read for its window. A window without is counted. */
static int
vsum_room(int quiet, DWORD len)
{
    const DWORD now = GetTickCount();

    if ((DWORD) (now - vsum_second) >= 1000) {
        vsum_second   = now;
        vsum_bytes[0] = vsum_bytes[1] = 0;
        vsum_count[0] = vsum_count[1] = 0;
    }
    if ((vsum_bytes[quiet] + len <= (quiet ? VSUM_QUIET_BUDGET : VSUM_BUDGET)) &&
        (vsum_count[quiet] < (quiet ? VSUM_QUIET_NOTES : VSUM_NOTES)))
        return 1;
    DIAG({
        diag.vsum_skipped++;
        diag.vsum_skipped_quiet += (DWORD) quiet;
    });
    return 0;
}

/* A VSUM note for stream s's vertices [vlo, vhi): `mem` is where the DLL
   reads the byte the stream's address names, `size` the stream's bytes
   from there; b the buffer, whose Locks since its last note the note
   carries (lock_given, pool.c): the caller clears them once the draw's
   notes are sent (a buffer in two streams has two). 2 if sent, 1 if not
   (the budget, an empty window), 0 if a packet failed. */
static int
vsum_note(DWORD s, const uint8_t *mem, DWORD size, DWORD vlo, DWORD vhi, DWORD stride, const pbuf_t *b)
{
    const int       quiet = vsum_quiet(b);
    DWORD           w[VGPU98_LEN_NOTE_VSUM];
    DWORD           offset;
    DWORD           len;
    DWORD           per;
    DWORD           nv;
    DWORD           k;
    volatile DWORD *p;

    if (!stride || (vhi <= vlo) || ((uint64_t) vlo * stride >= size))
        return 1;
    offset = vlo * stride;
    nv     = vhi - vlo;
    if ((uint64_t) nv * stride > size - offset)
        nv = (size - offset) / stride;
    if ((uint64_t) nv * stride > VGPU98_NOTE_VSUM_MAX)
        nv = VGPU98_NOTE_VSUM_MAX / stride;
    if (!nv)
        return 1;
    len = nv * stride;
    if (!vsum_room(quiet, len))
        return 1;
    vsum_bytes[quiet] += len;
    vsum_count[quiet]++;
    per  = (nv + VGPU98_NOTE_VSUM_PARTS - 1) / VGPU98_NOTE_VSUM_PARTS;
    w[0] = VGPU98_NOTE_VSUM;
    w[1] = note_seq();
    w[2] = s | ((b ? b->tt_id : 0) << VGPU98_NOTE_VSUM_BUF_SHIFT);
    w[3] = offset;
    w[4] = len;
    w[5] = stride;
    w[6] = (b ? b->tt_since : 0) | ((b && b->locked) ? VGPU98_NOTE_LOCK_OPEN : 0);
    for (k = 0; k < VGPU98_NOTE_VSUM_PARTS; k++) {
        const DWORD v0 = k * per;
        const DWORD v1 = ((k + 1) * per < nv) ? (k + 1) * per : nv;

        w[7 + k] = (v0 < v1) ? vsum_hash(mem + offset + v0 * stride, (v1 - v0) * stride, VGPU98_NOTE_HASH_SEED)
                             : VGPU98_NOTE_HASH_SEED;
    }
    w[VGPU98_LEN_NOTE_VSUM - 1] = note_check(w, VGPU98_LEN_NOTE_VSUM);
    if (!(p = gpu_packet(VGPU98_OP_NOP, VGPU98_LEN_NOTE_VSUM)))
        return 0;
    for (k = 0; k < VGPU98_LEN_NOTE_VSUM; k++)
        pkt_put(&p, w[k]);
    DIAG({
        diag.vsum_notes++;
        diag.vsum_bytes += len;
        diag.vsum_index += (s == VGPU98_NOTE_VSUM_INDEX);
    });
    return 2;
}

/* A DRAW: `count` vertices from vertex `first`, or with `ind`, `count`
   indices, each naming vertex `first` plus its value; edge flags per
   triangle from `edges` (a function of the triangle number), or none. */
typedef DWORD (*edge_fn)(const void *ctx, DWORD tri);

static int
draw(dp2_t *d, DWORD prim, DWORD first, DWORD count, const idx_src_t *ind, edge_fn edges, const void *ectx)
{
    DWORD           ntri    = 0;
    DWORD           control = prim;
    volatile DWORD *p;
    DWORD           i;

    /* A failed packet (the GPU was reset meanwhile) ends the batch's
       drawing: its state is gone from the GPU. */
    if (!d->ok)
        return 0;
    if (!count)
        return 1;
    if (diag_audit)
        audit_draw(d, first, count, ind);
    DIAG(diag_depth(d->c));
    if (prim == VGPU98_PRIM_TRIANGLES)
        ntri = count / 3;
    else if (prim >= VGPU98_PRIM_TRIANGLE_STRIP)
        ntri = (count >= 3) ? count - 2 : 0;
    if (ind)
        control |= VGPU98_DRAW_INDEXED;
    if (edges && ntri)
        control |= VGPU98_DRAW_EDGES;
    if (!send_setup(d) || !send_programs(d) ||
        !send_stream(d, (d->pool && !d->imm) ? STREAM_POOL : STREAM_INLINE) ||
        !send_needed(d, first, count, ind))
        return 0;
    if (ind && !send_indices(d, ind, count))
        return 0;
    if (d->stream == STREAM_POOL)
        d->pool_read = 1;
    /* The texture audit: the drawing process named, and the pool's
       vertices the draw reads hashed (a VSUM note before the DRAW). */
    if (diag_textures && !proc_note(d->c))
        return 0;
    if (diag_textures && (d->stream == STREAM_POOL) && d->pool_mem && vsum_room(vsum_quiet(d->pool), 0)) {
        DWORD vlo = first;
        DWORD vhi = first + count;

        if (ind) {
            vlo = 0xffffffffu;
            vhi = 0;
            for (i = 0; i < count; i++) {
                const DWORD v = first + idx_get(ind, i);

                if (v < vlo)
                    vlo = v;
                if (v >= vhi)
                    vhi = v + 1;
            }
        }
        switch (vsum_note(0, d->pool_mem, d->pool_size, vlo, vhi, d->stride, d->pool)) {
        case 0:
            return 0;
        case 2:
            if (d->pool)
                d->pool->tt_since = 0; /* the next note names the Locks from here */
            break;
        }
    }
    /* A command's primitives are counted in 16 bits, so its edge flags fit
       in one packet. */
    if (!(p = gpu_packet(VGPU98_OP_DRAW, VGPU98_LEN_DRAW + ((control & VGPU98_DRAW_EDGES) ? (ntri + 7) / 8 : 0))))
        return 0;
    pkt_put(&p, control);
    pkt_put(&p, ind ? 0 : first);
    pkt_put(&p, count);
    pkt_put(&p, ind ? first : 0);
    if (control & VGPU98_DRAW_EDGES) {
        for (i = 0; i < ntri; i += 8) {
            DWORD w = 0;
            DWORD k;

            for (k = 0; (k < 8) && (i + k < ntri); k++)
                w |= (edges(ectx, i + k) & 7) << (4 * k);
            pkt_put(&p, w);
        }
    }
    return 1;
}

/* Edge flags of an indexed triangle list: wFlags bits 8-10. */
static DWORD
tri_edges(const void *ctx, DWORD tri)
{
    const uint8_t *p = (const uint8_t *) ctx + 8 * tri + 6;

    return ((p[0] | (p[1] << 8)) >> 8) & 7;
}

/* An edge-flagged fan (TRIANGLEFAN_IMM): one bit per outer edge of the fan;
   the reference rasterizer's mapping onto its triangles (i+1, i+2, 0). */
typedef struct fan_edges {
    DWORD flags;
    DWORD ntri;
} fan_edges_t;

static DWORD
fan_edge(const void *ctx, DWORD tri)
{
    const fan_edges_t *f = ctx;
    DWORD              e = 0;

    if (f->ntri == 1)
        return ((f->flags & 2) ? 1 : 0) | ((f->flags & 4) ? 2 : 0) | ((f->flags & 1) ? 4 : 0);
    if (tri == 0)
        return ((f->flags & 2) ? 1 : 0) | ((f->flags & 1) ? 4 : 0);
    if ((tri + 1 < 32) && (f->flags & (1u << (tri + 1))))
        e |= 1;
    if ((tri == f->ntri - 1) && (tri + 2 < 32) && (f->flags & (1u << (tri + 2))))
        e |= 2;
    return e;
}

/* --- DirectX 8: streams, index buffers and their draws ------------------------ */

/* Why a DirectX 8 draw was skipped (the report) */
#define X8_SKIP_SHADER  0 /* no vertex shader, or one the GPU cannot read */
#define X8_SKIP_STREAM  1 /* a stream it reads has no buffer */
#define X8_SKIP_STRIDE  2 /* a stride the GPU lacks (a multiple of 4 up to 1024) */
#define X8_SKIP_INDICES 3 /* an indexed draw without an index buffer */
#define X8_SKIP_SIZE    4 /* more than a DRAW or the inline buffer takes */

static void
x8_skip(int why)
{
    DIAG({
        diag.x8_skipped++;
        diag.x8_skip_why[why]++;
    });
}

/* The pool buffers the open batch read: their fence at its end. Marked by
   the batch's number (pbuf_t.batch), each listed once (again after a
   BUFFERBLT renamed it). A batch that read more than the list holds fences
   every buffer in the pool. */
#define X8_READ 4096

static pbuf_t *x8_read[X8_READ];
static DWORD   x8_n_read;
static int     x8_read_full;
static DWORD   x8_serial;

static void
x8_mark(pbuf_t *b)
{
    if (b->batch == x8_serial)
        return;
    b->batch = x8_serial;
    if (x8_n_read < X8_READ)
        x8_read[x8_n_read++] = b;
    else
        x8_read_full = 1;
}

/* A new batch's number (never 0, a buffer's "none"). */
static void
x8_new_batch(void)
{
    if (!++x8_serial)
        x8_serial = 1;
    x8_n_read    = 0;
    x8_read_full = 0;
}

static void
x8_fence(DWORD fence)
{
    DWORD i;

    if (x8_read_full)
        pool_drawn_all(fence);
    else {
        for (i = 0; i < x8_n_read; i++)
            pool_drawn(x8_read[i], fence);
    }
    x8_n_read    = 0;
    x8_read_full = 0;
}

/* The vertices or indices of `n` primitives of a type (D3DPRIMITIVETYPE's
   numbers are the GPU's); 0 for an unknown type. */
static DWORD
prim_vertices(DWORD prim, DWORD n)
{
    if (!n)
        return 0;
    switch (prim) {
        case VGPU98_PRIM_POINTS:
            return n;
        case VGPU98_PRIM_LINES:
            return (n > VGPU98_DRAW_MAX_COUNT) ? ~0u : 2 * n;
        case VGPU98_PRIM_LINE_STRIP:
            return (n > VGPU98_DRAW_MAX_COUNT) ? ~0u : n + 1;
        case VGPU98_PRIM_TRIANGLES:
            return (n > VGPU98_DRAW_MAX_COUNT) ? ~0u : 3 * n;
        case VGPU98_PRIM_TRIANGLE_STRIP:
        case VGPU98_PRIM_TRIANGLE_FAN:
            return (n > VGPU98_DRAW_MAX_COUNT) ? ~0u : n + 2;
        default:
            return 0;
    }
}

/* Where a declaration's matrix indices come from, for indexed vertex
   blending (diag.x8_blend's first four). */
#define X8_IDX_FVF_UBYTE4 0 /* an FVF code with LASTBETA_UBYTE4 */
#define X8_IDX_FVF_BETA   1 /* an FVF code's last weight, without it */
#define X8_IDX_DECL       2 /* a declaration's BLENDINDICES */
#define X8_IDX_NONE       3 /* a declaration without them: the GPU takes 0-3 */
#define X8_IDX_UNBLENDED  6 /* no untransformed position: nothing blends (not counted) */

/* The context's vertex shader as the GPU's declaration: an FVF code's (of
   stream 0, its last weight the matrix indices while indexed vertex
   blending is on), or a declaration CREATEVERTEXSHADER made. 0 if there is
   none the GPU can read. */
static int
x8_declare(dp2_t *d)
{
    d3d_ctx_t *c       = d->c;
    const int  indexed = c->rs[VGPU98_RS_INDEXEDVERTEXBLENDENABLE] != 0;
    DWORD      stride;
    DWORD      i;

    if (d->x8_decl_ok && (d->x8_vs == c->vs) && (d->x8_indexed == indexed))
        return d->x8_usable;
    d->x8_decl_ok = 1;
    d->x8_vs      = c->vs;
    d->x8_indexed = indexed;
    d->x8_usable  = 0;
    if (!c->vs)
        return 0;
    if (!(c->vs & 1)) {
        d->n_decl = fvf_decl(c->vs, 1, indexed, d->decl, &stride);
        if (!d->n_decl)
            return 0;
        d->x8_streams = 1;
        if ((c->vs & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
            d->x8_blend_from = X8_IDX_UNBLENDED;
        else if ((c->vs & D3DFVF_POSITION_MASK) < D3DFVF_XYZB1)
            d->x8_blend_from = X8_IDX_NONE; /* XYZ: no weights, so no indices */
        else
            d->x8_blend_from = (c->vs & D3DFVF_LASTBETA_UBYTE4) ? X8_IDX_FVF_UBYTE4 : X8_IDX_FVF_BETA;
    } else {
        const d3d_vs_t *v = d3d8_vs_find(c, c->vs);

        if (!v || v->unusable || !v->n)
            return 0;
        memcpy(d->decl, v->decl, 2 * v->n * sizeof(DWORD));
        d->n_decl     = v->n;
        d->x8_streams = v->streams;
        d->x8_blend_from = X8_IDX_UNBLENDED;
        for (i = 0; i < d->n_decl; i++) {
            if ((VGPU98_DECL_USE(d->decl[2 * i]) == VGPU98_DU_POSITION) && !VGPU98_DECL_INDEX(d->decl[2 * i]) &&
                (d->x8_blend_from == X8_IDX_UNBLENDED))
                d->x8_blend_from = X8_IDX_NONE;
        }
        for (i = 0; i < d->n_decl; i++) {
            if ((VGPU98_DECL_USE(d->decl[2 * i]) == VGPU98_DU_BLENDINDICES) && (d->x8_blend_from == X8_IDX_NONE))
                d->x8_blend_from = X8_IDX_DECL;
        }
    }
    d->x8_has_w = 0;
    for (i = 0; i < d->n_decl; i++)
        d->x8_has_w |= VGPU98_DECL_USE(d->decl[2 * i]) == VGPU98_DU_BLENDWEIGHT;
    d->x8_usable = 1;
    return 1;
}

/* A sent draw's blending, for the dx8 blending line, with VERTEXBLEND as
   the GPU takes it (1-3 weights, tweening, one matrix by index; any other
   value is 0): with indexed blending on, by where its indices come from;
   with it off, 1-3 weights; tweened. A pre-transformed draw blends
   nothing, nor one without weights or indices (but under one indexed
   matrix, 0WEIGHTS, which takes index 0), nor one without weights with
   indexing off. */
static void
x8_count_blend(const dp2_t *d)
{
    const DWORD vb = d->c->rs[VGPU98_RS_VERTEXBLEND];

    if ((d->x8_blend_from == X8_IDX_UNBLENDED) ||
        !(((vb >= 1) && (vb <= 3)) || (vb == VGPU98_VBF_TWEENING) || (vb == VGPU98_VBF_0WEIGHTS)))
        return;
    if (vb == VGPU98_VBF_TWEENING)
        diag.x8_blend[5]++;
    else if (d->c->rs[VGPU98_RS_INDEXEDVERTEXBLENDENABLE]) {
        if ((d->x8_blend_from != X8_IDX_NONE) || d->x8_has_w || (vb == VGPU98_VBF_0WEIGHTS))
            diag.x8_blend[d->x8_blend_from]++;
    } else if ((vb != VGPU98_VBF_0WEIGHTS) && d->x8_has_w)
        diag.x8_blend[4]++;
}

/* The blending trace (TRACE_BLEND): a draw whose FVF code's last weight is
   taken as its matrix indices (indexed blending on, no LASTBETA_UBYTE4),
   with vertex v's indices dword and first weight as its buffer holds them:
   indices are small bytes, a weight a float. */
static void
x8_trace_blend(const dp2_t *d, const x8_src_t *src0, DWORD v)
{
    const uint64_t at = (uint64_t) v * src0->stride;
    DWORD          wo = ~0u;
    DWORD          io = ~0u;
    DWORD          i;

    for (i = 0; i < d->n_decl; i++) {
        const DWORD use = VGPU98_DECL_USE(d->decl[2 * i]);

        if (use == VGPU98_DU_BLENDWEIGHT)
            wo = d->decl[2 * i + 1];
        else if (use == VGPU98_DU_BLENDINDICES)
            io = d->decl[2 * i + 1];
    }
    if ((io == ~0u) || !src0->mem || (at + io + 4 > src0->size))
        return;
    dbg(DLL_TAG " trace: context %u, an FVF's last weight as matrix indices: vertex %u's %x (its first weight %x), VERTEXBLEND %u",
        ctx_handle(d->c), v, rd32(src0->mem + at + io),
        ((wo != ~0u) && (at + wo + 4 <= src0->size)) ? rd32(src0->mem + at + wo) : 0, d->c->rs[VGPU98_RS_VERTEXBLEND]);
}

/* The inline vertices' stride (TRIANGLEFAN_IMM, LINELIST_IMM): the vertex
   shader's FVF code's; 0 with a declaration. */
static void
x8_imm_stride(dp2_t *d)
{
    DWORD decl[2 * MAX_ELEMENTS];
    DWORD stride = 0;

    d->stride = (d->c->vs && !(d->c->vs & 1) && fvf_decl(d->c->vs, 1, 0, decl, &stride)) ? stride : 0;
}

/* A vertex or index buffer by its handle. */
static int
x8_buffer(const dp2_t *d, DWORD handle, DWORD stride, x8_src_t *x, int drawing)
{
    LPSURF  s = d3d7_buffer(d->c->ddl, handle);
    pbuf_t *b;

    memset(x, 0, sizeof(*x));
    if (!s || !s->lpGbl->fpVidMem) {
        x->unusable = s ? X8_NO_MEMORY : X8_NO_RECORD;
        return 0;
    }
    x->stride = stride;
    x->mem    = (const uint8_t *) s->lpGbl->fpVidMem;
    b         = pool_buffer(s);
    if (b && (b->kind != PB_IMPLICIT) && (b->kind != PB_COMMAND)) {
        /* Still locked (its Unlock not come yet): what is written under
           the Lock may change again, so each call that draws from it names
           a new version (not a BUFFERBLT's, nor the runtime's index buffer,
           whose version no draw names). */
        if (drawing && b->locked && !(b->lock_flags & DDLOCK_READONLY) && !b->runtime_ib) {
            b->version = pool_version_new();
            DIAG(diag.buf_drawn_locked++);
        }
        x->kind    = X8_POOL;
        x->pool    = b;
        x->addr    = pool_gpu(b);
        x->size    = b->size;
        x->version = b->version;
        return 1;
    }
    /* Windows 98 keeps DirectDraw's own buffers' size in lPitch. */
    x->kind      = X8_RING;
    x->size      = exebuf_bytes(s);
    x->from      = X8_FROM_POOL;
    x->rt        = (diag_on && b && (b->kind == PB_IMPLICIT) && !b->sys) ? b : NULL;
    x->allocated = s->lpSurfMore ? s->lpSurfMore->dwBytesAllocated : 0;
    if (!x->size && (s->lpGbl->lPitch > 0)) {
        x->size = (DWORD) s->lpGbl->lPitch;
        x->from = X8_FROM_PITCH;
    }
    if (!x->size)
        x->unusable = X8_NO_SIZE;
    return x->size != 0;
}

/* x8_buffer for binding k (a stream, or D3D_STREAMS: the index buffer),
   once per call while nothing moves it. */
static int
x8_bound(dp2_t *d, DWORD k, DWORD handle, DWORD stride, x8_src_t *x)
{
    const DWORD epoch = __atomic_load_n(&buf_epoch, __ATOMIC_ACQUIRE);

    GUARD_STEP((k == D3D_STREAMS) ? GUARD_STEP_INDICES : GUARD_STEP_STREAM, k, handle);
    if (d->x8_buf[k].handle && (d->x8_buf[k].handle == handle) && (d->x8_buf[k].stride == stride) &&
        (d->x8_buf[k].epoch == epoch)) {
        *x = d->x8_buf[k].x;
        return 1;
    }
    d->x8_buf[k].handle = 0;
    if (!x8_buffer(d, handle, stride, x, 1))
        return 0;
    d->x8_buf[k].handle = handle;
    d->x8_buf[k].stride = stride;
    d->x8_buf[k].epoch  = epoch;
    d->x8_buf[k].x      = *x;
    return 1;
}

/* Stream s for a draw: the call's user memory (SETSTREAMSOURCEUM), or the
   buffer SETSTREAMSOURCE bound. */
static int
x8_stream(dp2_t *d, DWORD s, x8_src_t *x)
{
    const d3d_ctx_t *c = d->c;

    if ((s == 0) && c->um) {
        memset(x, 0, sizeof(*x));
        if (!d->um)
            return 0;
        x->kind   = X8_RING;
        x->mem    = d->um;
        x->stride = c->um_stride;
        x->size   = d->um_vertices * c->um_stride;
        x->from   = X8_FROM_UM;
        return 1;
    }
    return c->stream[s].handle && x8_bound(d, s, c->stream[s].handle, c->stream[s].stride, x);
}

/* The inline buffer's vertex area: the call's DirectX 8 vertices go there
   one above another (x8_top), each ring stream's last window remembered
   (x8_sent), from the start again when full (the GPU reads packets in order,
   so a later INLINE never changes what an earlier DRAW read). A reset loses
   them. */
static void
x8_area(dp2_t *d)
{
    if (d->x8_epoch != sb->state_epoch) {
        d->x8_epoch = sb->state_epoch;
        d->x8_top   = 0;
        memset(d->x8_sent, 0, sizeof(d->x8_sent));
    }
}

static void
x8_wrap(dp2_t *d)
{
    d->x8_top = 0;
    memset(d->x8_sent, 0, sizeof(d->x8_sent));
}

/* A ring stream's window, vertices [lo, hi): its bytes (a stride of 0, every
   vertex the same bytes: the stream's first 256). */
static DWORD
x8_window_bytes(const x8_src_t *x, DWORD lo, DWORD hi)
{
    if (!x->stride)
        return (x->size < 256) ? x->size & ~3u : 256;
    return (hi - lo) * x->stride;
}

/* Whether the call sent the window already; *at is where vertex lo is. */
static int
x8_window_sent(const dp2_t *d, DWORD s, const x8_src_t *x, DWORD lo, DWORD hi, DWORD *at)
{
    if (!x->stride) {
        lo = 0;
        hi = 1;
    }
    if (d->x8_sent[s].hi && (d->x8_sent[s].mem == x->mem) && (d->x8_sent[s].stride == x->stride) &&
        (d->x8_sent[s].lo <= lo) && (hi <= d->x8_sent[s].hi)) {
        *at = d->x8_sent[s].at + (lo - d->x8_sent[s].lo) * x->stride;
        return 1;
    }
    return 0;
}

/* Sends it above the call's others (the caller made room); *at is where
   vertex lo is. 0 if the ring stopped. */
static int
x8_window_send(dp2_t *d, DWORD s, const x8_src_t *x, DWORD lo, DWORD hi, DWORD *at)
{
    const DWORD bytes = x8_window_bytes(x, lo, hi);

    if (!x->stride) {
        lo = 0;
        hi = 1;
    }
    if (bytes && !send_inline(d->x8_top, x->mem + lo * x->stride, bytes))
        return 0;
    if (x->rt)
        pool_rt_window(x->rt, x->mem + lo * x->stride, bytes); /* the bytes sent, a byte offset's shift included */
    d->x8_sent[s].mem    = x->mem;
    d->x8_sent[s].stride = x->stride;
    d->x8_sent[s].lo     = lo;
    d->x8_sent[s].hi     = (hi > lo) ? hi : 0;
    d->x8_sent[s].at     = d->x8_top;
    *at                  = d->x8_top;
    d->x8_top += bytes;
    DIAG(diag.x8_ring_bytes += bytes);
    return 1;
}

static DWORD
x8_index(const x8_src_t *ib, DWORD i)
{
    const DWORD off = i * ib->stride;

    if ((off >= ib->size) || (ib->size - off < ib->stride))
        return 0; /* past the buffer: as the GPU reads it */
    return (ib->stride == 2) ? rd16(ib->mem + off) : rd32(ib->mem + off);
}

static int x8_pieces(dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, DWORD shift0, DWORD max);

/* The stream audit (Diagnostics "DirectX 8: stream audit"): a DirectX 8
   DRAW's indices within its index buffer and within the command's
   MinIndex/NumVertices window, the vertices it reaches (and the window's)
   within each stream's buffer, every element of the declaration within its
   stream's stride (a stride-0 stream's within its buffer), every stream the
   declaration reads bound, and the draws the GPU cannot take. Indices are
   read only here. Each fault counted once a draw; a draw with one named,
   for each kind of fault, the first three times and again after 10 seconds
   without one of that kind. */
#define A8_INDICES 0 /* indices past the index buffer */
#define A8_WINDOW  1 /* an index outside MinIndex/NumVertices */
#define A8_STREAM  2 /* a vertex past a stream's buffer */
#define A8_STRIDE  3 /* an element past its stream's stride */
#define A8_UNBOUND 4 /* a stream the declaration reads, or an indexed draw's index buffer, not bound */
#define A8_UNTAKEN 5 /* a stride or an offset the GPU cannot take */
#define A8_DECLWIN 6 /* the command's MinIndex/NumVertices window past a stream's buffer, its indices within it */
#define A8_KINDS   7

/* A8_UNTAKEN's causes */
#define A8U_STRIDE 0 /* a stride not a multiple of 4 up to 1024 */
#define A8U_SHIFT  1 /* a "2" form's byte offset not a multiple of 4 within a vertex */
#define A8U_START  2 /* DRAWINDEXEDPRIMITIVE2's StartIndexOffset not a multiple of the index size */
#define A8U_NONE   3 /* a "2" form on stream 0 without a stride */

static DWORD audit8_logged[A8_KINDS];
static DWORD audit8_ms[A8_KINDS];

/* The index ranges the audit scanned of applications' index buffers in
   the pool, not locked: by version (new at every change of contents,
   unique), GPU address, first index, count and index size. Under the
   ring lock. */
#define A8_MEMO 4096

static struct {
    DWORD version;
    DWORD addr;
    DWORD first;
    DWORD n;
    DWORD isize;
    DWORD imin;
    DWORD imax;
} audit8_memo[A8_MEMO];

/* The window trace (TRACE_WINDOW): the draw command's record as the
   runtime wrote it, once a draw. */
static void
x8_trace_raw(dp2_t *d, int kind)
{
    if (!d->x8_raw_op || d->x8_raw_told || !TRACE(kind))
        return;
    d->x8_raw_told = 1;
    dbg(DLL_TAG " trace: context %u, the draw's command %u: %x %x %x %x %x %x (stream 0's stride %u, index size %u)",
        ctx_handle(d->c), d->x8_raw_op, d->x8_raw[0], d->x8_raw[1], d->x8_raw[2], d->x8_raw[3], d->x8_raw[4], d->x8_raw[5],
        d->c->um ? d->c->um_stride : d->c->stream[0].stride, d->c->ib_stride);
    if ((d->x8_raw_op == D3DDP2OP_DRAWINDEXEDPRIMITIVE2) && TRACE(kind)) {
        /* What x8_draw_bytes makes of it: the base vertex and stream 0's
           shift from BaseVertexOffset, the first index from
           StartIndexOffset. */
        const LONG  stride = (LONG) (d->c->um ? d->c->um_stride : d->c->stream[0].stride);
        const DWORD isize  = (d->c->ib_stride == 4) ? 4 : 2;
        LONG        q      = stride ? (LONG) d->x8_raw[1] / stride : 0;
        LONG        r      = stride ? (LONG) d->x8_raw[1] % stride : 0;

        if (r < 0) {
            q--;
            r += stride;
        }
        dbg(DLL_TAG " trace: context %u, read as base vertex %d, stream 0 from byte %d, first index %u, %u primitives",
            ctx_handle(d->c), q, r, d->x8_raw[4] / isize, d->x8_raw[5]);
    }
}

/* Whether a fault of kind k is named now (each kind's own budget). */
static int
audit8_budget(int k)
{
    const DWORD now = GetTickCount();

    if (audit8_logged[k] && (now - audit8_ms[k] >= AUDIT_QUIET_MS))
        audit8_logged[k] = 0;
    audit8_ms[k] = now;
    if (audit8_logged[k] >= AUDIT_LOGGED)
        return 0;
    audit8_logged[k]++;
    return 1;
}

/* Short: a line holds 160 characters. */
static DWORD
audit8_name(const dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, int fault, DWORD a, DWORD b, DWORD c,
            DWORD e)
{
    const char *ix = indexed ? " indexed" : "";

    if (!audit8_budget(fault))
        return 0;
    switch (fault) {
        case A8_DECLWIN:
            dbg(DLL_TAG ": stream audit: command %u, type %u%s from %u, %u, base %d: its window to %d past stream %u's %u (stride %u)",
                d->op, prim, ix, first, count, base, a, b, c, e);
            break;
        case A8_INDICES:
            dbg(DLL_TAG ": stream audit: command %u, type %u%s from %u, %u, base %d: indices to %u, the buffer's %u", d->op, prim,
                ix, first, count, base, a, b);
            break;
        case A8_WINDOW:
            dbg(DLL_TAG ": stream audit: command %u, type %u%s from %u, %u, base %d: index %u outside %u + %u", d->op, prim, ix,
                first, count, base, a, b, c);
            break;
        case A8_STREAM:
            if (e)
                dbg(DLL_TAG ": stream audit: command %u, type %u%s from %u, %u, base %d: vertex %d past stream %u's %u (stride %u)",
                    d->op, prim, ix, first, count, base, a, b, c, e);
            else
                dbg(DLL_TAG ": stream audit: command %u, type %u%s from %u, %u, base %d: stream %u (stride 0) holds %u of %u bytes",
                    d->op, prim, ix, first, count, base, b, c, a);
            break;
        case A8_STRIDE:
            dbg(DLL_TAG ": stream audit: command %u, shader %x: element at %u (%u bytes) past stream %u's stride %u", d->op,
                d->c->vs, a, b, c, e);
            break;
        case A8_UNBOUND:
            if (a == D3D_STREAMS)
                dbg(DLL_TAG ": stream audit: command %u, shader %x: no index buffer of 16 or 32 bits bound", d->op, d->c->vs);
            else
                dbg(DLL_TAG ": stream audit: command %u, shader %x: unbound stream %u", d->op, d->c->vs, a);
            break;
        default:
            if (a == A8U_STRIDE)
                dbg(DLL_TAG ": stream audit: command %u, shader %x: stream %u's stride %u, which the GPU cannot take", d->op,
                    d->c->vs, b, c);
            else if (a == A8U_SHIFT)
                dbg(DLL_TAG ": stream audit: command %u: stream 0 from %u bytes into a vertex, not a multiple of 4", d->op, b);
            else if (a == A8U_START)
                dbg(DLL_TAG ": stream audit: command %u: StartIndexOffset %u, not a multiple of %u", d->op, b, c);
            else
                dbg(DLL_TAG ": stream audit: command %u: a byte offset into stream 0, which has no stride", d->op);
            break;
    }
    return 1;
}

static void
audit8_draw(dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, const idx_src_t *ind, const x8_src_t *ib,
            const x8_src_t *src, DWORD streams, const DWORD *range)
{
    static const BYTE bytes[9] = { 0, 4, 8, 12, 16, 4, 4, 4, 8 }; /* by the GPU's element type */
    DWORD             reach[D3D_STREAMS]; /* the bytes the declaration reads of each stream's vertex */
    DWORD             faults = 0;
    DWORD             named  = 0; /* the faults whose line was written */
    int64_t           vlo;
    int64_t           vhi;
    int64_t           wlo    = 0; /* the command's window, when it names one */
    int64_t           whi    = -1;
    DWORD             i;
    DWORD             s;

#define A8_FAULT(k, a, b, c, e)                                                            \
    do {                                                                                   \
        if (!((faults >> (k)) & 1))                                                        \
            named |= audit8_name(d, prim, base, first, count, indexed, (k), (a), (b), (c), (e)) << (k); \
        faults |= 1u << (k);                                                               \
    } while (0)

    diag.audit8_draws++;
    /* The vertices it reaches: its range, or its indices' (those within the
       index buffer). */
    if (indexed) {
        DWORD imin = 0xffffffffu;
        DWORD imax = 0;
        DWORD n    = count;

        if (!ind) {
            const DWORD have = ib->size / ib->stride;

            if ((first > have) || (count > have - first)) {
                const uint64_t to = (uint64_t) first + count;

                A8_FAULT(A8_INDICES, (to > 0xffffffffu) ? 0xffffffffu : (DWORD) to, have, 0, 0);
                n = (first < have) ? have - first : 0;
            }
        }
        /* The indices' range: remembered for an application's buffer in
           the pool not locked, scanned otherwise (the runtime's, system
           memory, a command's own). Every fault needs only the range: an
           index outside the window is the smallest or the largest. */
        if (n && !ind && (ib->kind == X8_POOL) && ib->pool && !ib->pool->locked && ib->version) {
            const DWORD k = ((ib->version * 0x9E3779B1u) ^ (first * 0x85EBCA6Bu) ^ n ^ (ib->addr >> 5)) & (A8_MEMO - 1);

            static DWORD hits;

            if ((audit8_memo[k].version == ib->version) && (audit8_memo[k].addr == ib->addr) && (audit8_memo[k].first == first) &&
                (audit8_memo[k].n == n) && (audit8_memo[k].isize == ib->stride) && (++hits & 63)) {
                imin = audit8_memo[k].imin;
                imax = audit8_memo[k].imax;
                diag.audit8_reused++;
            } else {
                /* Every 64th reuse is scanned again: indices written
                   without a new version (a pointer kept after Unlock) show. */
                const int again = (audit8_memo[k].version == ib->version) && (audit8_memo[k].addr == ib->addr) &&
                                  (audit8_memo[k].first == first) && (audit8_memo[k].n == n) && (audit8_memo[k].isize == ib->stride);

                if (range && (n == count)) {
                    /* Read for the ring's windows just now. */
                    imin = range[0];
                    imax = range[1];
                } else {
                    for (i = 0; i < n; i++) {
                        const DWORD v = x8_index(ib, first + i);

                        imin = (v < imin) ? v : imin;
                        imax = (v > imax) ? v : imax;
                    }
                    diag.audit8_scanned += n;
                }
                if (again && ((audit8_memo[k].imin != imin) || (audit8_memo[k].imax != imax))) {
                    static DWORD told;

                    diag.audit8_changed++;
                    if (told++ < 3)
                        dbg(DLL_TAG ": stream audit: indices at %x (version %x, from %u, %u) changed without a new version: %u-%u, were %u-%u",
                            ib->addr, ib->version, first, n, imin, imax, audit8_memo[k].imin, audit8_memo[k].imax);
                }
                audit8_memo[k].version = ib->version;
                audit8_memo[k].addr    = ib->addr;
                audit8_memo[k].first   = first;
                audit8_memo[k].n       = n;
                audit8_memo[k].isize   = ib->stride;
                audit8_memo[k].imin    = imin;
                audit8_memo[k].imax    = imax;
            }
        } else if (range && (n == count)) {
            /* Read for the ring's windows already (the audit lighter by
               that scan). */
            imin = range[0];
            imax = range[1];
        } else {
            for (i = 0; i < n; i++) {
                const DWORD v = ind ? idx_get(ind, i) : x8_index(ib, first + i);

                imin = (v < imin) ? v : imin;
                imax = (v > imax) ? v : imax;
            }
            diag.audit8_scanned += n;
        }
        if (!n)
            imin = imax = 0;
        else if (d->x8_window && ((imin < d->x8_min) || (imax - d->x8_min >= d->x8_nverts)))
            A8_FAULT(A8_WINDOW, (imin < d->x8_min) ? imin : imax, d->x8_min, d->x8_nverts, 0);
        vlo = (int64_t) base + imin;
        vhi = (int64_t) base + imax;
        if (d->x8_trace_range) {
            d->x8_trace_range = 0;
            if (TRACE(TRACE_DIP2))
                dbg(DLL_TAG " trace: context %u, the draw's %u indices read %u to %u, so vertices %d to %d", ctx_handle(d->c), n,
                    imin, imax, (LONG) vlo, (LONG) vhi);
        }
        /* The window the command names: checked on its own (the GPU reads
           the vertices the indices reach). */
        if (d->x8_window && d->x8_nverts) {
            wlo = (int64_t) base + d->x8_min;
            whi = wlo + d->x8_nverts - 1;
        }
    } else {
        vlo = first;
        vhi = (int64_t) first + count - 1;
    }
    memset(reach, 0, sizeof(reach));
    for (i = 0; i < d->n_decl; i++) {
        const DWORD el   = d->decl[2 * i];
        const DWORD type = VGPU98_DECL_TYPE(el);
        const DWORD st   = VGPU98_DECL_STREAM(el);
        const DWORD to   = d->decl[2 * i + 1] + ((type < 9) ? bytes[type] : 0);

        if (to > reach[st])
            reach[st] = to;
        if ((type < 9) && ((streams >> st) & 1) && src[st].stride && (to > src[st].stride))
            A8_FAULT(A8_STRIDE, d->decl[2 * i + 1], bytes[type], st, src[st].stride);
    }
    for (s = 0; s < D3D_STREAMS; s++) {
        if (!((streams >> s) & 1))
            continue;
        if (!src[s].stride) {
            /* Every vertex the same bytes: its elements within the buffer. */
            if (src[s].size < reach[s])
                A8_FAULT(A8_STREAM, reach[s], s, src[s].size, 0);
            continue;
        }
        if ((vlo < 0) || (vhi >= (int64_t) (src[s].size / src[s].stride))) {
            /* The figure as a signed 32-bit number, clamped. */
            const int64_t v = (vlo < 0) ? ((vlo < -0x80000000LL) ? -0x80000000LL : vlo) : ((vhi > 0x7fffffffLL) ? 0x7fffffffLL : vhi);

            A8_FAULT(A8_STREAM, (DWORD) (int32_t) v, s, src[s].size / src[s].stride, src[s].stride);
        } else if ((whi >= wlo) && ((wlo < 0) || (whi >= (int64_t) (src[s].size / src[s].stride)))) {
            const int64_t v = (wlo < 0) ? ((wlo < -0x80000000LL) ? -0x80000000LL : wlo) : ((whi > 0x7fffffffLL) ? 0x7fffffffLL : whi);

            A8_FAULT(A8_DECLWIN, (DWORD) (int32_t) v, s, src[s].size / src[s].stride, src[s].stride);
        }
    }
#undef A8_FAULT
    for (i = 0; i < A8_KINDS; i++)
        diag.audit8_faults[i] += (faults >> i) & 1;
    /* A draw named for its vertices past a buffer: the command as the
       runtime wrote it (not for a window fault alone, which sloppy
       MinIndex/NumVertices make common). */
    if (named & ((1u << A8_INDICES) | (1u << A8_STREAM) | (1u << A8_DECLWIN)))
        x8_trace_raw(d, TRACE_WINDOW);
}

/* The point-sprite trace (TRACE_POINTS): a point-list draw, its shader
   (and whether it carries PSIZE) and stage 0's texture handle, then the
   point states it draws with; once for each of those not among the last 8
   traced (test 17's draws repeated it past its bound within seconds;
   kinds of point draws alternating in a frame stay quiet), with the first
   such draw's count. `declared`: the draw's declaration was built (a draw
   skipped for its shader has none to carry PSIZE). */
#define POINTS_KEY  12
#define POINTS_SEEN 8
static DWORD points_traced[POINTS_SEEN][POINTS_KEY]; /* context, shader, PSIZE, texture, the point states */
static DWORD points_next;

static void
x8_trace_points(const dp2_t *d, DWORD first, DWORD count, int declared)
{
    const d3d_ctx_t *c     = d->c;
    int              psize = 0;
    DWORD            key[POINTS_KEY];
    DWORD            i;

    for (i = 0; declared && (i < d->n_decl); i++)
        psize |= VGPU98_DECL_USE(d->decl[2 * i]) == VGPU98_DU_PSIZE;
    key[0]  = ctx_handle(c);
    key[1]  = c->vs;
    key[2]  = (DWORD) psize;
    key[3]  = c->tss[0][D3DTSS_TEXTUREMAP];
    key[4]  = c->rs[VGPU98_RS_POINTSIZE];
    key[5]  = c->rs[VGPU98_RS_POINTSIZE_MIN];
    key[6]  = c->rs[VGPU98_RS_POINTSIZE_MAX];
    key[7]  = c->rs[VGPU98_RS_POINTSPRITEENABLE];
    key[8]  = c->rs[VGPU98_RS_POINTSCALEENABLE];
    key[9]  = c->rs[VGPU98_RS_POINTSCALE_A];
    key[10] = c->rs[VGPU98_RS_POINTSCALE_B];
    key[11] = c->rs[VGPU98_RS_POINTSCALE_C];
    for (i = 0; i < POINTS_SEEN; i++) {
        if (!dwords_differ(key, points_traced[i], POINTS_KEY))
            return;
    }
    memcpy(points_traced[points_next], key, sizeof(key));
    points_next = (points_next + 1) % POINTS_SEEN;
    if (!TRACE(TRACE_POINTS))
        return;
    dbg(DLL_TAG " trace: context %u points: %u from %u, shader %x%s, stage 0's texture %x", ctx_handle(c), count, first, c->vs,
        psize ? " with PSIZE" : "", c->tss[0][D3DTSS_TEXTUREMAP]);
    if (TRACE(TRACE_POINTS))
        dbg(DLL_TAG " trace: context %u point states: size %x (%x to %x), sprites %u, scale %u (%x, %x, %x)", ctx_handle(c),
            c->rs[VGPU98_RS_POINTSIZE], c->rs[VGPU98_RS_POINTSIZE_MIN], c->rs[VGPU98_RS_POINTSIZE_MAX],
            c->rs[VGPU98_RS_POINTSPRITEENABLE], c->rs[VGPU98_RS_POINTSCALEENABLE], c->rs[VGPU98_RS_POINTSCALE_A],
            c->rs[VGPU98_RS_POINTSCALE_B], c->rs[VGPU98_RS_POINTSCALE_C]);
}

/* A ring stream's window cut at its buffer's end: vertices the draw's
   indices (or range) reach past what the buffer holds as the DLL sized it.
   Counted; with the stream audit, traced (TRACE_WINDOW) with where the
   size came from and the record's allocation, then the command. */
static DWORD cut_seen[16]; /* the cut windows traced lately (their keys), so a draw each frame repeats is traced once */
static DWORD cut_next;

void
audit8_reset(void)
{
    memset(audit8_logged, 0, sizeof(audit8_logged));
    memset(cut_seen, 0, sizeof(cut_seen));
    memset(points_traced, 0, sizeof(points_traced));
}

static void
x8_clamped(dp2_t *d, DWORD s, const x8_src_t *x, LONG lo, LONG hi, DWORD avail, DWORD prim, DWORD start, DWORD count,
           LONG dbase)
{
    static const char *const from[3] = { "the pool's", "lPitch", "user memory" };
    const DWORD              lacks = (DWORD) hi - (((DWORD) lo > avail) ? (DWORD) lo : avail);
    DWORD                    key   = 0x811c9dc5u;
    DWORD                    i;

    diag.x8_ring_clamped++;
    if (!diag_audit8)
        return;
    key = (key ^ (DWORD) (uintptr_t) x->mem) * 16777619u;
    key = (key ^ (DWORD) lo) * 16777619u;
    key = (key ^ (DWORD) hi) * 16777619u;
    key = ((key ^ count) * 16777619u) | 1u;
    for (i = 0; i < ARRAY_SIZE(cut_seen); i++) {
        if (cut_seen[i] == key)
            return;
    }
    if (!TRACE(TRACE_WINDOW))
        return;
    cut_seen[cut_next++ % ARRAY_SIZE(cut_seen)] = key;
    dbg(DLL_TAG " trace: context %u stream %u: window %d-%d cut to %u vertices (%u bytes, stride %u; %s, %u allocated)",
        ctx_handle(d->c), s, lo, hi - 1, avail, x->size, x->stride, from[x->from], x->allocated);
    /* As the host's "lacks" line names the DRAW (the window's first vertex
       the GPU's 0). */
    if (TRACE(TRACE_WINDOW))
        dbg(DLL_TAG " trace: context %u, the cut draw's DRAW: type %u, start %u, count %u, base %d; it lacks %u of %u vertices",
            ctx_handle(d->c), prim, start, count, dbase, lacks, (DWORD) (hi - lo));
    x8_trace_raw(d, TRACE_WINDOW);
}

/* A draw the audit sees skipped: a stream the declaration reads with no
   buffer, or (s D3D_STREAMS) an indexed draw with no index buffer
   (A8_UNBOUND, figure s), or (A8_UNTAKEN) a stride or offset the GPU cannot
   take (its cause and figures). */
static void
audit8_skipped(dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, int fault, DWORD a, DWORD b, DWORD c)
{
    diag.audit8_draws++;
    diag.audit8_faults[fault]++;
    audit8_name(d, prim, base, first, count, indexed, fault, a, b, c, 0);
}

/* A DirectX 8 context's draw, from the streams its vertex shader reads:
   `count` vertices from vertex `first`, or with `indexed`, `count` indices
   from index `first` (of the index buffer, or of `ind`, a command's own
   16-bit indices), each naming vertex `base` plus its value. Stream 0 starts
   `shift0` bytes into its buffer (the "2" forms' byte offsets). When a ring
   stream is among them, every stream is named from the draw's first vertex
   (`lo`), so the inline buffer holds just the vertices the draw reads. One
   too large for the ring at once goes in pieces (x8_pieces). A draw the GPU
   could not take is skipped, and counted. 0 if the ring stopped. */
static int
x8_draw(dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, const idx_src_t *ind, DWORD shift0,
        edge_fn edges, const void *ectx)
{
    x8_src_t        src[D3D_STREAMS];
    x8_src_t        ib;
    DWORD           at[D3D_STREAMS];
    DWORD           bytes[D3D_STREAMS];
    DWORD           streams;
    DWORD           start   = first;
    LONG            lo      = 0;
    int             ring    = 0;
    int             ranged  = 0;               /* range holds an indexed draw's smallest and largest index */
    DWORD           range[2] = { 0, 0 };
    DWORD           ntri    = 0;
    DWORD           control = prim;
    DWORD           n       = 0;
    volatile DWORD *p;
    DWORD           s;
    DWORD           i;

    if (!d->ok)
        return 0;
    if (!count)
        return 1;
    if ((count > VGPU98_DRAW_MAX_COUNT) || (prim < VGPU98_PRIM_POINTS) || (prim > VGPU98_PRIM_TRIANGLE_FAN)) {
        x8_skip(X8_SKIP_SIZE);
        return 1;
    }
    if (!x8_declare(d)) {
        if ((prim == VGPU98_PRIM_POINTS) && diag_audit8)
            x8_trace_points(d, first, count, 0);
        x8_skip(X8_SKIP_SHADER);
        return 1;
    }
    if ((prim == VGPU98_PRIM_POINTS) && diag_audit8 && !d->x8_pieces)
        x8_trace_points(d, first, count, 1);
    streams = d->x8_streams;
    for (s = 0; s < D3D_STREAMS; s++) {
        if (!((streams >> s) & 1))
            continue;
        if (!x8_stream(d, s, &src[s])) {
            if (diag_audit8)
                audit8_skipped(d, prim, base, first, count, indexed, A8_UNBOUND, s, 0, 0);
            x8_skip(X8_SKIP_STREAM);
            return 1;
        }
        if ((src[s].stride & 3) || (src[s].stride > 1024) || ((s == 0) && (shift0 & 3))) {
            if (diag_audit8) {
                if ((src[s].stride & 3) || (src[s].stride > 1024))
                    audit8_skipped(d, prim, base, first, count, indexed, A8_UNTAKEN, A8U_STRIDE, s, src[s].stride);
                else
                    audit8_skipped(d, prim, base, first, count, indexed, A8_UNTAKEN, A8U_SHIFT, shift0, 0);
            }
            x8_skip(X8_SKIP_STRIDE);
            return 1;
        }
        if ((s == 0) && shift0) {
            src[0].size = (shift0 < src[0].size) ? src[0].size - shift0 : 0;
            src[0].addr += shift0;
            src[0].mem += shift0;
        }
        ring |= src[s].kind == X8_RING;
        n++;
    }
    if (indexed && !ind) {
        if (!d->c->ib || !x8_bound(d, D3D_STREAMS, d->c->ib, d->c->ib_stride, &ib) || ((ib.stride != 2) && (ib.stride != 4))) {
            if (diag_audit8)
                audit8_skipped(d, prim, base, first, count, indexed, A8_UNBOUND, D3D_STREAMS, 0, 0);
            x8_skip(X8_SKIP_INDICES);
            return 1;
        }
        /* The runtime's own index buffer (its user-pointer draws, which come
           as DRAWINDEXEDPRIMITIVE2) is rewritten without Locks, so it never
           gets a new version: its indices go through the ring's index area,
           read now, as the runtime's vertices do. */
        if ((ib.kind == X8_POOL) && (d->x8_dip2 || ib.pool->runtime_ib)) {
            /* The runtime's is small (4 KB): one up to RT_IB_MAX is taken
               for it for good; a larger one's DRAWINDEXEDPRIMITIVE2s go
               through the ring, its other draws by its address. */
            if (d->x8_dip2 && !ib.pool->runtime_ib && (ib.pool->size <= RT_IB_MAX)) {
                ib.pool->runtime_ib = 1;
                DIAG({
                    diag.x8_rt_ib_flagged++;
                    if (ib.pool->size > diag.x8_rt_ib_largest)
                        diag.x8_rt_ib_largest = ib.pool->size;
                });
            }
            ib.kind = X8_RING;
        }
    }
    if (diag_audit8 && (d->x8_blend_from == X8_IDX_FVF_BETA) && d->c->rs[VGPU98_RS_INDEXEDVERTEXBLENDENABLE] &&
        d->c->rs[VGPU98_RS_VERTEXBLEND] && TRACE(TRACE_BLEND))
        x8_trace_blend(d, &src[0], indexed ? (DWORD) (base + (LONG) (ind ? idx_get(ind, 0) : x8_index(&ib, first))) : first);
    /* The caps promise any draw one DRAW: one whose indices from a ring
       index buffer, or whose vertices from ring streams (not indexed), are
       more than the inline buffer's areas take goes in pieces. */
    if (!edges) {
        DWORD max = ~0u;

        if (indexed && !ind && (ib.kind == X8_RING))
            max = IDX_BYTES / ib.stride;
        else if (!indexed && ring) {
            DWORD per   = 0;
            DWORD fixed = 0;

            for (s = 0; s < D3D_STREAMS; s++) {
                if (((streams >> s) & 1) && (src[s].kind == X8_RING)) {
                    per += src[s].stride;
                    fixed += src[s].stride ? 0 : 256;
                }
            }
            if (per)
                max = (VTX_BYTES - fixed) / per;
        }
        if (count > max)
            return x8_pieces(d, prim, base, first, count, indexed, shift0, max);
    }
    /* An indexed draw from ring streams: its indices' range, read once for
       the ring's windows (below) and the stream audit. */
    if (indexed && ring) {
        range[0] = 0xffffffffu;
        range[1] = 0;
        for (i = 0; i < count; i++) {
            const DWORD v = ind ? idx_get(ind, i) : x8_index(&ib, first + i);

            if (v < range[0])
                range[0] = v;
            if (v > range[1])
                range[1] = v;
        }
        ranged = 1;
    }
    /* The stream audit: each DRAW (a piece of a draw split above is one). */
    if (diag_audit8) {
        GUARD_STEP(GUARD_STEP_AUDIT, 0, 0);
        const uint64_t t_a8 = rdtsc();

        audit8_draw(d, prim, base, first, count, indexed, ind, &ib, src, streams, ranged ? range : NULL);
        diag.audit8_cycles += rdtsc() - t_a8;
    }
    GUARD_STEP(GUARD_STEP_PACKETS, 0, 0);

    /* The vertices the ring sends: those the draw reaches (its indices
       read for it), each stream's within its buffer. */
    if (ring) {
        LONG hi;

        if (!indexed) {
            lo = (LONG) first;
            hi = (LONG) (first + count);
        } else {
            DWORD imin = range[0];
            DWORD imax = range[1];

            if (imax > VGPU98_VERTEX_LIMIT)
                imax = VGPU98_VERTEX_LIMIT;
            if (imin > imax)
                imin = imax;
            lo = base + (LONG) imin;
            hi = base + (LONG) imax + 1;
        }
        if (lo < 0)
            lo = 0;
        if (hi < lo)
            hi = lo;
        {
            DWORD    whi[D3D_STREAMS];
            DWORD    have = 0; /* streams whose window the call sent already */
            uint64_t need = 0; /* the bytes of those it lacks */

            x8_area(d);
            for (s = 0; s < D3D_STREAMS; s++) {
                DWORD avail;

                if (!((streams >> s) & 1) || (src[s].kind != X8_RING))
                    continue;
                avail  = src[s].stride ? src[s].size / src[s].stride : 1;
                whi[s] = ((DWORD) hi < avail) ? (DWORD) hi : avail;
                if (diag_on && src[s].stride && ((DWORD) hi > avail))
                    x8_clamped(d, s, &src[s], lo, hi, avail, prim,
                               !indexed ? first - (DWORD) lo : (ind || (ib.kind == X8_RING)) ? 0 : first, count,
                               indexed ? base - lo : 0);
                if (whi[s] < (DWORD) lo)
                    whi[s] = (DWORD) lo;
                bytes[s] = x8_window_bytes(&src[s], (DWORD) lo, whi[s]);
                if (x8_window_sent(d, s, &src[s], (DWORD) lo, whi[s], &at[s]))
                    have |= 1u << s;
                else
                    need += bytes[s];
            }
            /* The windows it lacks above the others; when they do not fit,
               all of its windows again from the area's start (wrapping
               between two of them would overwrite the first). */
            if (need > VTX_BYTES - d->x8_top) {
                x8_wrap(d);
                have = 0;
                need = 0;
                for (s = 0; s < D3D_STREAMS; s++) {
                    if (((streams >> s) & 1) && (src[s].kind == X8_RING))
                        need += bytes[s];
                }
                if (need > VTX_BYTES) {
                    x8_skip(X8_SKIP_SIZE);
                    return 1;
                }
            }
            for (s = 0; s < D3D_STREAMS; s++) {
                if (((streams >> s) & 1) && (src[s].kind == X8_RING) && !((have >> s) & 1) &&
                    !x8_window_send(d, s, &src[s], (DWORD) lo, whi[s], &at[s]))
                    return 0;
            }
        }
    }

    if (!send_programs(d) || !send_declare(d->decl, d->n_decl))
        return 0;
    for (s = 0; s < D3D_STREAMS; s++) {
        DWORD w[VGPU98_LEN_STREAM];

        if (!((streams >> s) & 1))
            continue;
        w[0] = s;
        w[4] = src[s].stride;
        if (src[s].kind == X8_POOL) {
            const uint64_t skip = (uint64_t) (DWORD) lo * src[s].stride;

            w[1] = VGPU98_SRC_GPUVM;
            w[2] = src[s].addr + (DWORD) skip;
            w[3] = (skip < src[s].size) ? src[s].size - (DWORD) skip : 0;
            w[5] = src[s].version;
            x8_mark(src[s].pool);
            DIAG({
                /* The vertices it reads: an indexed draw's window when the
                   command names one. */
                const DWORD nv = (indexed && d->x8_window && d->x8_nverts && !d->x8_pieces) ? d->x8_nverts : count;

                if (src[s].pool->dynamic)
                    diag.buf_dynamic += (uint64_t) nv * src[s].stride;
                else
                    diag.buf_static += (uint64_t) nv * src[s].stride;
            });
        } else {
            w[1] = VGPU98_SRC_INLINE;
            w[2] = at[s];
            w[3] = bytes[s];
            w[5] = 0;
        }
        if (!send_stream_words(w))
            return 0;
    }
    if (indexed) {
        DWORD w[VGPU98_LEN_INDICES];

        if (ind || (ib.kind == X8_RING)) {
            /* Into the index area, from index 0. */
            if (ind) {
                if (2 * count > IDX_BYTES) {
                    x8_skip(X8_SKIP_SIZE);
                    return 1;
                }
                if (!send_indices(d, ind, count))
                    return 0;
                w[0] = 2;
            } else {
                const DWORD isize = ib.stride;
                DWORD       have  = ((DWORD) first < ib.size / isize) ? ib.size / isize - first : 0;
                DWORD       n_in  = (count < have) ? count : have; /* past the buffer, none: the draw ends there */

                if (isize * n_in > IDX_BYTES) {
                    x8_skip(X8_SKIP_SIZE);
                    return 1;
                }
                if (isize == 2) {
                    if (!send_inline(IDX_AT, ib.mem + 2 * first, 2 * (n_in & ~1u)))
                        return 0;
                    if (n_in & 1) {
                        if (!(p = gpu_packet(VGPU98_OP_INLINE, 2)))
                            return 0;
                        pkt_put(&p, IDX_AT + 2 * (n_in - 1));
                        pkt_put(&p, rd16(ib.mem + 2 * (first + n_in - 1)));
                    }
                } else if (!send_inline(IDX_AT, ib.mem + 4 * first, 4 * n_in))
                    return 0;
                count = n_in;
                if (!count)
                    return 1;
                w[0] = isize;
                DIAG({
                    diag.x8_ring_idx += (uint64_t) isize * n_in;
                    diag.x8_rt_idx_draws += ib.pool != NULL; /* a pool buffer read in place: the runtime's */
                });
            }
            w[1]  = IDX_AT;
            w[2]  = IDX_BYTES;
            w[3]  = 0;
            start = 0;
        } else {
            w[0]  = ib.stride | VGPU98_IDX_GPUVM;
            w[1]  = ib.addr;
            w[2]  = ib.size;
            w[3]  = ib.version;
            start = first;
            x8_mark(ib.pool);
            DIAG(diag.x8_pool_idx += (uint64_t) ib.stride * count);
        }
        if (!send_indices_words(w))
            return 0;
        control |= VGPU98_DRAW_INDEXED;
    } else
        start = first - (DWORD) lo;
    if (diag_on) {
        /* A draw sent (those skipped above are counted so). */
        diag.x8_draws++;
        d->c->draws++;
        diag.x8_draws_indexed += indexed != 0;
        diag.x8_streams[(n <= 2) ? ((n == 2) ? 1 : 0) : ((n <= 4) ? 2 : 3)]++;
        x8_count_blend(d);
        if (d->c->ps_on)
            d3d8_ps_drawn(d->c);
        diag_depth(d->c);
    }

    if (prim == VGPU98_PRIM_TRIANGLES)
        ntri = count / 3;
    else if (prim >= VGPU98_PRIM_TRIANGLE_STRIP)
        ntri = (count >= 3) ? count - 2 : 0;
    if (edges && ntri)
        control |= VGPU98_DRAW_EDGES;
    /* The texture audit: each pool stream's vertices the draw reads, hashed
       (a VSUM note each before the DRAW), and a pool index buffer's indices.
       The vertex window in the draw's own numbers (its indices' range, or
       the window it declares when no pool stream is dynamic and it is near
       the draw's size: a game appending with NOOVERWRITE may declare the
       whole buffer), plus base, from the stream's address (lo's vertex);
       found at the first pool stream, once the budget has room. */
    if (diag_textures && !proc_note(d->c))
        return 0;
    if (diag_textures) {
        int64_t vlo     = -1;
        int64_t vhi     = 0;
        DWORD   noted   = 0; /* the streams whose note was sent */
        int     dynamic = 0;
        int     quiet   = 1; /* every pool stream's buffer is (vsum_quiet) */

        for (s = 0; s < D3D_STREAMS; s++) {
            if (((streams >> s) & 1) && (src[s].kind == X8_POOL)) {
                dynamic |= src[s].pool && src[s].pool->dynamic;
                quiet &= vsum_quiet(src[s].pool);
            }
        }

        for (s = 0; s < D3D_STREAMS; s++) {
            const uint64_t skip = (uint64_t) (DWORD) lo * src[s].stride;

            if (!((streams >> s) & 1) || (src[s].kind != X8_POOL) || !src[s].mem || !src[s].stride || (skip >= src[s].size))
                continue;
            if (vlo < 0) {
                if (!vsum_room(quiet, 0))
                    break;
                if (!indexed) {
                    vlo = first;
                    vhi = (int64_t) first + count;
                } else if (d->x8_window && d->x8_nverts && !d->x8_pieces && !dynamic &&
                           ((uint64_t) d->x8_nverts <= (uint64_t) count + 64)) {
                    vlo = (int64_t) base + d->x8_min;
                    vhi = vlo + d->x8_nverts;
                    if (vlo < (int64_t) lo)
                        vlo = lo;
                } else {
                    DWORD imin = ranged ? range[0] : 0xffffffffu;
                    DWORD imax = ranged ? range[1] : 0;

                    for (i = 0; !ranged && (i < count); i++) {
                        const DWORD v = ind ? idx_get(ind, i) : x8_index(&ib, first + i);

                        if (v < imin)
                            imin = v;
                        if (v > imax)
                            imax = v;
                    }
                    if (imin > imax)
                        break;
                    vlo = (int64_t) base + imin;
                    vhi = (int64_t) base + imax + 1;
                    if (vlo < (int64_t) lo)
                        vlo = lo;
                }
            }
            if ((vhi <= vlo) || (vlo - lo > 0xffffffffLL))
                break;
            switch (vsum_note(s, src[s].mem + (DWORD) skip, src[s].size - (DWORD) skip, (DWORD) (vlo - lo),
                              (DWORD) ((vhi - lo > 0xffffffffLL) ? 0xffffffffLL : vhi - lo), src[s].stride, src[s].pool)) {
            case 0:
                return 0;
            case 2:
                noted |= 1u << s;
                break;
            }
        }
        /* The index buffer read where it is: the draw's indices. */
        if (indexed && !ind && (ib.kind == X8_POOL) && ib.pool && ib.mem && ib.stride) {
            switch (vsum_note(VGPU98_NOTE_VSUM_INDEX, ib.mem, ib.size, first, first + count, ib.stride, ib.pool)) {
            case 0:
                return 0;
            case 2:
                ib.pool->tt_since = 0;
                break;
            }
        }
        /* The next notes name the Locks from here. */
        for (s = 0; s < D3D_STREAMS; s++) {
            if (((noted >> s) & 1) && src[s].pool)
                src[s].pool->tt_since = 0;
        }
    }
    if (!(p = gpu_packet(VGPU98_OP_DRAW, VGPU98_LEN_DRAW + ((control & VGPU98_DRAW_EDGES) ? (ntri + 7) / 8 : 0))))
        return 0;
    pkt_put(&p, control);
    pkt_put(&p, start);
    pkt_put(&p, count);
    pkt_put(&p, indexed ? (DWORD) (base - lo) : 0);
    if (control & VGPU98_DRAW_EDGES) {
        for (i = 0; i < ntri; i += 8) {
            DWORD w = 0;
            DWORD k;

            for (k = 0; (k < 8) && (i + k < ntri); k++)
                w |= (edges(ectx, i + k) & 7) << (4 * k);
            pkt_put(&p, w);
        }
    }
    return 1;
}

/* A DirectX 8 context's inline vertices (TRIANGLEFAN_IMM, LINELIST_IMM), in
   its vertex shader's FVF format: into the inline buffer, read as stream 0
   for this draw. */
static int
x8_imm(dp2_t *d, DWORD prim, const uint8_t *verts, DWORD n, edge_fn edges, const void *ectx)
{
    const DWORD     bytes = n * d->stride;
    DWORD           w[VGPU98_LEN_STREAM];
    DWORD           ntri = (prim >= VGPU98_PRIM_TRIANGLE_STRIP) ? ((n >= 3) ? n - 2 : 0) : 0;
    DWORD           control = prim | ((edges && ntri) ? VGPU98_DRAW_EDGES : 0);
    volatile DWORD *p;
    DWORD           i;

    if (!d->ok)
        return 0;
    if (!n)
        return 1;
    if (!x8_declare(d) || (d->c->vs & 1) || !d->stride) {
        x8_skip(X8_SKIP_SHADER);
        return 1;
    }
    if (bytes > VTX_BYTES) {
        x8_skip(X8_SKIP_SIZE);
        return 1;
    }
    x8_area(d);
    if (d->x8_top + bytes > VTX_BYTES)
        x8_wrap(d);
    w[0] = 0;
    w[1] = VGPU98_SRC_INLINE;
    w[2] = d->x8_top;
    w[3] = bytes;
    w[4] = d->stride;
    w[5] = 0;
    if (!send_inline(d->x8_top, verts, bytes) || !send_programs(d) || !send_declare(d->decl, d->n_decl) || !send_stream_words(w))
        return 0;
    d->x8_top += bytes;
    DIAG({
        diag.x8_ring_bytes += bytes;
        x8_count_blend(d);
        d->c->draws++;
        if (d->c->ps_on)
            d3d8_ps_drawn(d->c);
    });
    if (!(p = gpu_packet(VGPU98_OP_DRAW, VGPU98_LEN_DRAW + ((control & VGPU98_DRAW_EDGES) ? (ntri + 7) / 8 : 0))))
        return 0;
    pkt_put(&p, control);
    pkt_put(&p, 0);
    pkt_put(&p, n);
    pkt_put(&p, 0);
    if (control & VGPU98_DRAW_EDGES) {
        for (i = 0; i < ntri; i += 8) {
            DWORD e = 0;
            DWORD k;

            for (k = 0; (k < 8) && (i + k < ntri); k++)
                e |= (edges(ectx, i + k) & 7) << (4 * k);
            pkt_put(&p, e);
        }
    }
    return 1;
}

/* A draw command's record, for the window trace. */
static void
x8_raw_record(dp2_t *d, DWORD op, const uint8_t *r)
{
    DWORD k;

    d->x8_raw_op   = op;
    d->x8_raw_told = 0;
    for (k = 0; k < 6; k++)
        d->x8_raw[k] = rd32(r + 4 * k);
}

/* A draw of the "2" forms (DRAWPRIMITIVE2, DRAWINDEXEDPRIMITIVE2,
   CLIPPEDTRIANGLEFAN): stream 0 only, its vertex numbers from a byte offset
   (signed for the indexed one): the whole vertices of it, then the rest as
   stream 0's shift. */
static int
x8_draw_bytes(dp2_t *d, DWORD prim, LONG offset, DWORD first, DWORD count, int indexed, edge_fn edges, const void *ectx)
{
    const d3d_ctx_t *c      = d->c;
    const LONG       stride = (LONG) (c->um ? c->um_stride : c->stream[0].stride);
    LONG             q;
    LONG             r;

    if (stride <= 0) {
        if (diag_audit8)
            audit8_skipped(d, prim, offset, first, count, indexed, A8_UNTAKEN, A8U_NONE, 0, 0);
        x8_skip(X8_SKIP_STRIDE);
        return 1;
    }
    q = offset / stride;
    r = offset % stride;
    if (r < 0) {
        q--;
        r += stride;
    }
    if (indexed)
        return x8_draw(d, prim, q, first, count, 1, NULL, (DWORD) r, edges, ectx);
    if (q < 0) {
        x8_skip(X8_SKIP_SIZE);
        return 1;
    }
    return x8_draw(d, prim, 0, (DWORD) q, count, 0, NULL, (DWORD) r, edges, ectx);
}

/* A draw too large for the ring at once, in pieces of at most `max`
   vertices or indices, each its own DRAW: lists in whole primitives, strips
   overlapping (a triangle strip's pieces from an even vertex, so their
   triangles keep their winding). Fans are skipped. */
static int
x8_pieces(dp2_t *d, DWORD prim, LONG base, DWORD first, DWORD count, int indexed, DWORD shift0, DWORD max)
{
    DWORD step = 0;
    DWORD keep = 0; /* vertices a piece shares with the next */

    switch (prim) {
        case VGPU98_PRIM_POINTS:
            step = max;
            break;
        case VGPU98_PRIM_LINES:
            step = max & ~1u;
            break;
        case VGPU98_PRIM_LINE_STRIP:
            step = max;
            keep = 1;
            break;
        case VGPU98_PRIM_TRIANGLES:
            step = max - max % 3;
            break;
        case VGPU98_PRIM_TRIANGLE_STRIP:
            step = max & ~1u;
            keep = 2;
            break;
        default:
            break;
    }
    if (step <= keep) {
        x8_skip(X8_SKIP_SIZE);
        return 1;
    }
    DIAG(diag.x8_split++);
    d->x8_pieces = 1;
    for (;;) {
        const DWORD n = (count < step) ? count : step;

        if (!x8_draw(d, prim, base, first, n, indexed, NULL, shift0, NULL, NULL)) {
            d->x8_pieces = 0;
            return 0;
        }
        if (n == count) {
            d->x8_pieces = 0;
            return 1;
        }
        first += n - keep;
        count -= n - keep;
    }
}

/* Ends the open batch (its fence on what it used) and starts another, for a
   BUFFERBLT whose destination's fresh memory could not be had: once the GPU
   passed the batch, the destination is written in place. 0 if the GPU is
   held now (the call then draws no more). */
static int
x8_break(dp2_t *d)
{
    DWORD fence = gpu_finish();

    if (d->drew)
        ctx_used(d->c, fence);
    x8_fence(fence);
    gpu_release();
    gpu_wait(fence);
    if (gpu_begin() != DD_OK) {
        d->ok   = 0;
        d->held = 1;
        return 0;
    }
    /* The restart in gpu_begin may have stopped this very context. */
    if (d->c->stopped) {
        gpu_release();
        d->ok   = 0;
        d->held = 1;
        return 0;
    }
    gpu_batch_owner(ctx_token(d->c));
    ctx_batch_begin();
    x8_new_batch();
    d->tex_listed = 0;
    /* Without the ring lock, another context's call may have written the
       inline buffer: the windows this call sent there are gone. */
    x8_wrap(d);
    return 1;
}

/* BUFFERBLT: a range of one buffer into another, in order with the draws: a
   pool destination through pool_write (fresh memory when a queued packet
   reads it), one in system memory, which the ring reads at each draw,
   written as it is. */
static int
x8_bufblt(dp2_t *d, DWORD hdst, DWORD hsrc, DWORD offset, DWORD soff, DWORD size)
{
    x8_src_t  dst;
    x8_src_t  src;
    const int have_dst = x8_buffer(d, hdst, 0, &dst, 0);
    const int have_src = x8_buffer(d, hsrc, 0, &src, 0);

    if (!have_dst || !have_src) {
        /* A buffer its handle names no usable one of: no record, no memory
           or no size, each end named. */
        static const char *const why[4] = { "unusable", "without a record", "without memory", "without a size" };

        DIAG(diag.bufblts_refused++);
        if (TRACE(TRACE_LOOKUP))
            dbg(DLL_TAG " trace: BUFFERBLT %x to %x: the source %s, the destination %s", hsrc, hdst,
                have_src ? "usable" : why[src.unusable & 3], have_dst ? "usable" : why[dst.unusable & 3]);
        return 1;
    }
    if ((soff >= src.size) || (offset >= dst.size)) {
        /* A range past either buffer's end (or a buffer of no size):
           refused as a buffer not found is, and named. */
        DIAG(diag.bufblts_refused++);
        if (TRACE(TRACE_LOOKUP))
            dbg(DLL_TAG " trace: BUFFERBLT %x to %x: from %u of the source's %u bytes to %u of the destination's %u", hsrc, hdst,
                soff, src.size, offset, dst.size);
        return 1;
    }
    if (size > src.size - soff)
        size = src.size - soff;
    if (size > dst.size - offset)
        size = dst.size - offset;
    DIAG(diag.bufblts++);
    /* A pool destination gets a new version, perhaps fresh memory: the
       call's draws resolve their buffers again. */
    memset(d->x8_buf, 0, sizeof(d->x8_buf));
    if (dst.kind != X8_POOL) {
        /* What the ring sent of it is stale now. */
        memset(d->x8_sent, 0, sizeof(d->x8_sent));
        memmove((uint8_t *) dst.mem + offset, src.mem + soff, size);
        return 1;
    }
    if (!d->held) {
        if (pool_write(dst.pool, offset, src.mem + soff, size, (dst.pool->batch == x8_serial) ? POOL_WRITE_BATCH : 0))
            return 1;
        DIAG(diag.bufblt_waits++);
        if (x8_break(d)) {
            /* The GPU passed every batch that read it (another process's
               draw still reading it gets the new bytes). */
            if (!pool_write(dst.pool, offset, src.mem + soff, size, 0))
                pool_write(dst.pool, offset, src.mem + soff, size, POOL_WRITE_IN_PLACE);
            return 1;
        }
    }
    /* No batch is open (the GPU held, or the context stopped): in place, as
       a Lock writes it, so the buffer is never left stale. */
    pool_write(dst.pool, offset, src.mem + soff, size, POOL_WRITE_IN_PLACE);
    return 0;
}

/* Row-major 4x4 products: r = a b (MULTIPLYTRANSFORM). */
static void
mat_mul(float *r, const float *a, const float *b)
{
    int i;
    int j;
    int k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += a[4 * i + k] * b[4 * k + j];
            r[4 * i + j] = sum;
        }
    }
}

/* A legacy drawing command's draw: the call's vertices (DirectX 6 and 7),
   or a DirectX 8 context's streams, `first` the base of indexed ones. */
static int
ldraw(dp2_t *d, DWORD prim, DWORD first, DWORD count, const idx_src_t *ind, edge_fn edges, const void *ectx)
{
    if (d->x8)
        return x8_draw(d, prim, ind ? (LONG) first : 0, ind ? 0 : first, count, ind != NULL, ind, 0, edges, ectx);
    return draw(d, prim, first, count, ind, edges, ectx);
}

/* --- The command buffer ------------------------------------------------------------ */

/* An indexed draw from `data`: 16-bit indices one after another. */
static int
draw_indexed(dp2_t *d, DWORD prim, DWORD first, DWORD count, const uint8_t *data)
{
    idx_src_t s;

    s.p       = data;
    s.records = 0;
    return ldraw(d, prim, first, count, &s, NULL, NULL);
}

/* Inline vertices (the _IMM commands) go into the vertex area above the
   highest chunk of the call's own sent so far when they fit, else at its
   top; chunks of the call's that they overwrite are sent again when a later
   draw needs them. */
static DWORD
imm_base(dp2_t *d, DWORD count)
{
    const DWORD base = (d->sent_top + count <= d->room) ? d->sent_top : d->room - count;
    DWORD       c;

    /* The chunks whose vertices of the call's they overwrite. */
    for (c = base >> CHUNK_SHIFT; (c < CHUNKS) && ((c << CHUNK_SHIFT) < base + count); c++) {
        const DWORD top = ((c + 1) << CHUNK_SHIFT < d->limit) ? (c + 1) << CHUNK_SHIFT : d->limit;

        if (top > base)
            d->sent[c >> 5] &= ~(1u << (c & 31));
    }
    return base;
}

/* The call's vertices in a pool buffer: the GPU reads them where they are,
   at the buffer's version. STREAM names the buffer from the call's
   dwVertexOffset to its end, not just the dwVertexLength vertices the call
   names: real hardware reads a vertex buffer itself, so a draw that reaches
   past the count its call names still finds its vertices (a game that
   names too few goes unnoticed there), and nothing outside the buffer is
   read. 0 if they are not in the pool, or not where the GPU can read them;
   they then go through the ring.

   The runtime writes its own vertex buffers (and the old API's execute
   buffers) without a Lock, and may write them again as soon as the call
   returns. Their vertices stay where they are only when the call lets the
   driver swap the buffer (SWAPVERTEXBUFFER): it then gets fresh memory, and
   the GPU reads the old once the runtime has moved on (pool_swap). Without
   the swap they go through the ring, read before the call returns. Each
   call is a new version of them. */
static int
pool_source(dp2_t *d, const D3DHAL_DRAWPRIMITIVES2DATA *dd)
{
    const uint64_t want = (uint64_t) dd->dwVertexLength * d->stride;
    pbuf_t        *b;
    DWORD          avail;

    if ((dd->dwFlags & D3DHALDP2_USERMEMVERTICES) || !(b = pool_buffer(dd->lpDDVertex)) || !d->stride ||
        (dd->dwVertexOffset & 3))
        return 0;
    if ((b->kind != PB_VERTEX) && !(dd->dwFlags & D3DHALDP2_SWAPVERTEXBUFFER)) {
        DIAG(diag.buf_unswapped++);
        return 0;
    }
    /* An application's buffer locked for writing:
       DirectX 6 and 7 draw no locked buffer, so this call's
       draws were issued before the Lock and read the contents from before
       it: the block the Lock's rename left, read where it is, or else a copy
       through the ring now (the game writes under its Lock only once its
       Lock call returns). A NOOVERWRITE Lock promises to leave what queued
       draws read: they read in place, under a new version. A kept block is
       read first, whatever a Lock nested in the one that left it asks. */
    if ((b->kind == PB_VERTEX) && b->locked && !b->kept_on && (b->lock_flags & DDLOCK_NOOVERWRITE)) {
        DIAG(diag.buf_drawn_locked++);
        b->version = pool_version_new();
    } else if ((b->kind == PB_VERTEX) && b->locked && (b->kept_on || !(b->lock_flags & DDLOCK_READONLY))) {
        const uint8_t *mem;
        DWORD          gpu;
        DWORD          size;
        DWORD          version;

        DIAG(diag.buf_drawn_locked++);
        b->drawn_locked = 1; /* its later plain write Locks copy */
        if (!pool_kept(b, &mem, &gpu, &size, &version))
            return 0;
        avail           = (dd->dwVertexOffset < size) ? size - dd->dwVertexOffset : 0;
        d->pool         = b;
        d->pool_kept    = 1;
        d->pool_mem     = mem + dd->dwVertexOffset;
        d->pool_addr    = gpu + dd->dwVertexOffset;
        d->pool_size    = avail;
        d->pool_used    = (want < avail) ? (DWORD) want : avail;
        d->pool_version = version;
        return 1;
    }
    if (b->kind != PB_VERTEX)
        b->version = pool_version_new();
    avail           = (dd->dwVertexOffset < b->size) ? b->size - dd->dwVertexOffset : 0;
    d->pool         = b;
    d->pool_mem     = (const uint8_t *) b->gbl->fpVidMem + dd->dwVertexOffset;
    d->pool_addr    = pool_gpu(b) + dd->dwVertexOffset;
    d->pool_size    = avail;
    d->pool_used    = (want < avail) ? (DWORD) want : avail;
    d->pool_version = b->version;
    return 1;
}

/* The bytes a drawing command takes, its header included (its inline
   vertices too, DWORD aligned as the runtime lays them out): 0 if it is not
   one, ~0 if its inline vertices cannot be stepped over (the call's vertex
   format is not one the GPU takes). The DDK has the driver stay within the
   command buffer, and with the old execute-buffer API the counts are the
   program's. */
static DWORD
draw_bytes(const dp2_t *d, const uint8_t *at)
{
    const DWORD    count = rd16(at + 2);
    const uint8_t *data  = at + 4;
    const uint8_t *verts;

    switch (at[0]) {
        case D3DDP2OP_POINTS:
        case D3DDP2OP_INDEXEDLINELIST:
            return 4 + 4 * count;
        case D3DDP2OP_INDEXEDTRIANGLELIST:
            return 4 + 8 * count;
        case D3DDP2OP_LINELIST:
        case D3DDP2OP_LINESTRIP:
        case D3DDP2OP_TRIANGLELIST:
        case D3DDP2OP_TRIANGLESTRIP:
        case D3DDP2OP_TRIANGLEFAN:
            return 4 + 2;
        case D3DDP2OP_INDEXEDLINELIST2:
            return 4 + 2 + 4 * count;
        case D3DDP2OP_INDEXEDLINESTRIP:
            return 4 + 2 + 2 * (count + 1);
        case D3DDP2OP_INDEXEDTRIANGLELIST2:
            return 4 + 2 + 6 * count;
        case D3DDP2OP_INDEXEDTRIANGLESTRIP:
        case D3DDP2OP_INDEXEDTRIANGLEFAN:
            return 4 + 2 + 2 * (count + 2);
        case D3DDP2OP_TRIANGLEFAN_IMM:
            if (!d->drawable || !d->stride)
                return ~0u;
            verts = (const uint8_t *) (((uintptr_t) (data + 4) + 3) & ~(uintptr_t) 3);
            return (DWORD) (verts - at) + (count + 2) * d->stride;
        case D3DDP2OP_LINELIST_IMM:
            if (!d->drawable || !d->stride)
                return ~0u;
            verts = (const uint8_t *) (((uintptr_t) data + 3) & ~(uintptr_t) 3);
            return (DWORD) (verts - at) + 2 * count * d->stride;
        default:
            return 0;
    }
}

/* --- State commands ---------------------------------------------------------------- */

static void
wr32(uint8_t *p, DWORD v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static void
wrf(uint8_t *p, float f)
{
    union {
        float f;
        DWORD u;
    } c;

    c.f = f;
    wr32(p, c.u);
}

/* The bytes a state command takes, its header included: 0 if it is not one,
   ~0 if it runs past `end`. The state commands are what a state block
   records: render and stage states, the viewport, the W and Z ranges, the
   material, lights, transforms and clip planes. */
static DWORD
state_bytes(const uint8_t *at, const uint8_t *end)
{
    const DWORD    count = rd16(at + 2);
    const uint8_t *p     = at + 4;
    DWORD          unit;
    DWORD          i;

    switch (at[0]) {
        case D3DDP2OP_RENDERSTATE:
        case D3DDP2OP_TEXTURESTAGESTATE:
        case D3DDP2OP_WINFO:
        case D3DDP2OP_ZRANGE:
        case D3DDP2OP_EXT:
            unit = 8;
            break;
        case D3DDP2OP_VIEWPORTINFO:
            unit = 16;
            break;
        case D3DDP2OP_SETMATERIAL:
            unit = 4 * D3DMATERIAL7_DWORDS;
            break;
        case D3DDP2OP_CREATELIGHT:
            unit = 4;
            break;
        case D3DDP2OP_SETTRANSFORM:
            unit = 4 + 64;
            break;
        case D3DDP2OP_SETCLIPPLANE:
            unit = 20;
            break;
        /* DirectX 8's bindings */
        case D3DDP2OP_SETVERTEXSHADER:
            unit = 4;
            break;
        case D3DDP2OP_SETSTREAMSOURCE:
            unit = 12;
            break;
        case D3DDP2OP_SETINDICES:
            unit = 8;
            break;
        case D3DDP2OP_SETPIXELSHADER:
            unit = 4;
            break;
        case D3DDP2OP_SETVERTEXSHADERCONST:
        case D3DDP2OP_SETPIXELSHADERCONST:
            /* Each: the first register and the count, then the float4s. */
            for (i = 0; i < count; i++) {
                if (((DWORD) (end - p) < 8) || (rd32(p + 4) > ((DWORD) (end - p) - 8) / 16))
                    return ~0u;
                p += 8 + 16 * rd32(p + 4);
            }
            return (DWORD) (p - at);
        case D3DDP2OP_SETLIGHT:
            /* Each: the index and data type, then a D3DLIGHT7 for DATA. */
            for (i = 0; i < count; i++) {
                if (end - p < 8)
                    return ~0u;
                p += 8 + ((rd32(p + 4) == D3DHAL_SETLIGHT_DATA) ? 4 * D3DLIGHT7_DWORDS : 0);
                if (p > end)
                    return ~0u;
            }
            return (DWORD) (p - at);
        default:
            return 0;
    }
    if ((DWORD) (end - p) < count * unit)
        return ~0u;
    return 4 + count * unit;
}

/* Applies a state command to the context; render states also go into the
   runtime's array when there is one (execute buffers). */
static void
state_apply(d3d_ctx_t *c, const uint8_t *at, DWORD *rstates)
{
    const DWORD    count = rd16(at + 2);
    const uint8_t *p     = at + 4;
    DWORD          i;

    switch (at[0]) {
        case D3DDP2OP_RENDERSTATE:
            for (i = 0; i < count; i++, p += 8) {
                const DWORD s = rd32(p);
                const DWORD v = rd32(p + 4);

                ctx_set_rs(c, s, v);
                if (rstates && (s < D3DRS_MAX))
                    rstates[s] = v;
            }
            break;
        case D3DDP2OP_TEXTURESTAGESTATE:
            for (i = 0; i < count; i++, p += 8)
                ctx_set_tss(c, rd16(p), rd16(p + 2), rd32(p + 4));
            break;
        case D3DDP2OP_VIEWPORTINFO:
            /* The last of them counts. */
            if (count) {
                const uint8_t *v = p + 16 * (count - 1);

                for (i = 0; i < 4; i++)
                    c->vp[i] = rd32(v + 4 * i);
                c->dirty |= DIRTY_VIEW;
                c->clean = 0;
            }
            break;
        case D3DDP2OP_WINFO:
            /* The runtime's W range, from the projection matrix. An infinite
               far value (an infinite far plane) goes to the GPU as
               +infinity, which WRANGE takes: the W-buffer keeps its
               precision whatever the range. A NaN or an infinite near value
               leaves the range as it was, counted. */
            if (count) {
                const DWORD wn = rd32(p + 8 * (count - 1));
                const DWORD wf = rd32(p + 8 * (count - 1) + 4);

                if (f32_nan(wn) || f32_inf(wn) || f32_nan(wf))
                    DIAG(diag.winfo_refused++);
                else {
                    c->wnear = rdf(p + 8 * (count - 1));
                    c->wfar  = f32_inf(wf) ? __builtin_inff() : rdf(p + 8 * (count - 1) + 4);
                    c->dirty |= DIRTY_VIEW;
                    c->clean = 0;
                    DIAG(diag_winfo(c->wnear, c->wfar, f32_inf(wf)));
                }
            }
            break;
        case D3DDP2OP_ZRANGE:
            if (count)
                ctx_set_zrange(c, rdf(p + 8 * (count - 1)), rdf(p + 8 * (count - 1) + 4));
            break;
        case D3DDP2OP_SETMATERIAL:
            if (count)
                ctx_set_material(c, p + 4 * D3DMATERIAL7_DWORDS * (count - 1));
            break;
        case D3DDP2OP_CREATELIGHT:
            for (i = 0; i < count; i++, p += 4) {
                if (!ctx_create_light(c, rd32(p)))
                    dbg_event(DLL_TAG ": no room for light %u", rd32(p));
            }
            break;
        case D3DDP2OP_SETLIGHT:
            for (i = 0; i < count; i++) {
                const DWORD type = rd32(p + 4);

                ctx_set_light(c, rd32(p), type, p + 8);
                p += 8 + ((type == D3DHAL_SETLIGHT_DATA) ? 4 * D3DLIGHT7_DWORDS : 0);
            }
            break;
        case D3DDP2OP_SETTRANSFORM:
            for (i = 0; i < count; i++, p += 68)
                ctx_set_transform(c, rd32(p), p + 4);
            break;
        case D3DDP2OP_SETCLIPPLANE:
            for (i = 0; i < count; i++, p += 20)
                ctx_set_clipplane(c, rd32(p), p + 4);
            break;
        case D3DDP2OP_SETVERTEXSHADER:
            /* An FVF code or a shader's handle (0: none until the next),
               a shader's constants loaded at each; the last counts. */
            for (i = 0; i < count; i++)
                d3d8_vs_set(c, rd32(p + 4 * i));
            break;
        case D3DDP2OP_SETVERTEXSHADERCONST:
            for (i = 0; i < count; i++) {
                if (d3d8_programs)
                    d3d8_vs_const(c, rd32(p), rd32(p + 4), p + 8);
                p += 8 + 16 * rd32(p + 4);
            }
            break;
        case D3DDP2OP_SETSTREAMSOURCE:
            for (i = 0; i < count; i++, p += 12) {
                const DWORD s = rd32(p);

                if (s >= D3D_STREAMS)
                    continue;
                c->stream[s].handle = rd32(p + 4);
                c->stream[s].stride = rd32(p + 8);
                if (s == 0)
                    c->um = 0;
            }
            break;
        case D3DDP2OP_SETINDICES:
            if (count) {
                c->ib        = rd32(p + 8 * (count - 1));
                c->ib_stride = rd32(p + 8 * (count - 1) + 4);
            }
            break;
        case D3DDP2OP_SETPIXELSHADER:
            /* A shader's def constants loaded at each; the last counts
               (no shaders without programs or capture). */
            for (i = 0; (diag_shaders || d3d8_programs) && (i < count); i++)
                d3d8_ps_bind(c, rd32(p + 4 * i));
            break;
        case D3DDP2OP_SETPIXELSHADERCONST:
            for (i = 0; i < count; i++) {
                if (diag_shaders || d3d8_programs)
                    d3d8_ps_const(c, rd32(p), rd32(p + 4), p + 8);
                p += 8 + 16 * rd32(p + 4);
            }
            break;
        default: /* EXT: no extensions here */
            break;
    }
}

/* CAPTURE: a recorded state command takes the context's current values. */
static void
state_capture(d3d_ctx_t *c, uint8_t *at)
{
    const DWORD count = rd16(at + 2);
    uint8_t    *p     = at + 4;
    DWORD       i;
    DWORD       k;

    switch (at[0]) {
        case D3DDP2OP_RENDERSTATE:
            for (i = 0; i < count; i++, p += 8) {
                const DWORD s = rd32(p);

                if (s < D3DRS_MAX)
                    wr32(p + 4, c->rs[s]);
            }
            break;
        case D3DDP2OP_TEXTURESTAGESTATE:
            for (i = 0; i < count; i++, p += 8) {
                const DWORD s = rd16(p);
                const DWORD n = rd16(p + 2);

                if ((s < D3D_STAGES) && (n < D3D_TSS_COUNT))
                    wr32(p + 4, c->tss[s][n]);
            }
            break;
        case D3DDP2OP_VIEWPORTINFO:
            for (i = 0; i < count; i++, p += 16) {
                for (k = 0; k < 4; k++)
                    wr32(p + 4 * k, c->vp[k]);
            }
            break;
        case D3DDP2OP_WINFO:
            for (i = 0; i < count; i++, p += 8) {
                wrf(p, c->wnear);
                wrf(p + 4, c->wfar);
            }
            break;
        case D3DDP2OP_ZRANGE:
            for (i = 0; i < count; i++, p += 8) {
                wrf(p, c->zrange[0]);
                wrf(p + 4, c->zrange[1]);
            }
            break;
        case D3DDP2OP_SETMATERIAL:
            for (i = 0; i < count; i++, p += 4 * D3DMATERIAL7_DWORDS)
                memcpy(p, c->material, sizeof(c->material));
            break;
        case D3DDP2OP_SETLIGHT:
            for (i = 0; i < count; i++) {
                const DWORD        index = rd32(p);
                const DWORD        type  = rd32(p + 4);
                const d3d_light_t *l     = ctx_light(c, index);

                if (l && (type == D3DHAL_SETLIGHT_DATA))
                    memcpy(p + 8, l->data, sizeof(l->data));
                else if (l && (type <= D3DHAL_SETLIGHT_DISABLE))
                    wr32(p + 4, l->enabled ? D3DHAL_SETLIGHT_ENABLE : D3DHAL_SETLIGHT_DISABLE);
                p += 8 + ((type == D3DHAL_SETLIGHT_DATA) ? 4 * D3DLIGHT7_DWORDS : 0);
            }
            break;
        case D3DDP2OP_SETTRANSFORM:
            for (i = 0; i < count; i++, p += 68)
                ctx_get_transform(c, rd32(p), p + 4);
            break;
        case D3DDP2OP_SETCLIPPLANE:
            for (i = 0; i < count; i++, p += 20) {
                const DWORD index = rd32(p);

                if (index < VGPU98_MAX_CLIPPLANES)
                    memcpy(p + 4, c->plane[index], sizeof(c->plane[index]));
            }
            break;
        case D3DDP2OP_SETVERTEXSHADER:
            for (i = 0; i < count; i++, p += 4)
                wr32(p, c->vs);
            break;
        case D3DDP2OP_SETSTREAMSOURCE:
            for (i = 0; i < count; i++, p += 12) {
                const DWORD s = rd32(p);

                if (s < D3D_STREAMS) {
                    wr32(p + 4, c->stream[s].handle);
                    wr32(p + 8, c->stream[s].stride);
                }
            }
            break;
        case D3DDP2OP_SETINDICES:
            for (i = 0; i < count; i++, p += 8) {
                wr32(p, c->ib);
                wr32(p + 4, c->ib_stride);
            }
            break;
        case D3DDP2OP_SETPIXELSHADER:
            for (i = 0; i < count; i++, p += 4)
                wr32(p, c->ps);
            break;
        case D3DDP2OP_SETPIXELSHADERCONST:
            for (i = 0; i < count; i++) {
                const DWORD reg = rd32(p);
                const DWORD n   = rd32(p + 4);
                DWORD       r;
                DWORD       k;

                for (r = 0; (r < n) && (reg < D3D_PS_CONSTS) && (r < D3D_PS_CONSTS - reg); r++) {
                    for (k = 0; k < 4; k++)
                        wr32(p + 8 + 16 * r + 4 * k, c->ps_const[reg + r][k]);
                }
                p += 8 + 16 * n;
            }
            break;
        case D3DDP2OP_SETVERTEXSHADERCONST:
            for (i = 0; i < count; i++) {
                const DWORD reg = rd32(p);
                const DWORD n   = rd32(p + 4);
                DWORD       r;
                DWORD       k;

                for (r = 0; (r < n) && (reg < D3D_VS_CONSTS) && (r < D3D_VS_CONSTS - reg); r++) {
                    for (k = 0; k < 4; k++)
                        wr32(p + 8 + 16 * r + 4 * k, c->vconst ? c->vconst[reg + r][k] : 0);
                }
                p += 8 + 16 * n;
            }
            break;
        default: /* CREATELIGHT and EXT hold no values */
            break;
    }
}

/* STATESET: state blocks are recorded and replayed here (d3d7.c keeps
   them). Between BEGIN and END the state commands are recorded instead of
   applied, as the reference rasterizer does. */
static void
stateset(d3d_ctx_t *c, DWORD op, DWORD handle)
{
    uint8_t *data;
    uint8_t *q;
    DWORD    len;
    DWORD    n;

    switch (op) {
        case D3DHAL_STATESETBEGIN:
            d3d7_sb_begin(c, handle);
            break;
        case D3DHAL_STATESETEND:
            c->sb_rec = 0;
            break;
        case D3DHAL_STATESETDELETE:
            d3d7_sb_delete(c, handle);
            break;
        case D3DHAL_STATESETEXECUTE:
        case D3DHAL_STATESETCAPTURE:
            data = d3d7_sb_data(c, handle, &len);
            for (q = data; q && (q + 4 <= data + len); q += n) {
                n = state_bytes(q, data + len);
                if (!n || (n == ~0u))
                    break;
                if (op == D3DHAL_STATESETEXECUTE)
                    state_apply(c, q, NULL);
                else
                    state_capture(c, q);
            }
            DIAG({
                if (op == D3DHAL_STATESETEXECUTE)
                    diag.sb_executed++;
                else
                    diag.sb_captured++;
            });
            break;
        default:
            break;
    }
}

/* --- The commands ------------------------------------------------------------- */

/* One command of the buffer at `at` (DrawPrimitives2's loop runs each inside
   the fault guard): the next command's start, or, with *status set, the
   command's own (the call ends there, D3DERR_COMMAND_UNPARSED). */
#define DP2_UNPARSED()   \
    do {                 \
        *status = 1;     \
        return at0;      \
    } while (0)
/* A shader the DLL cannot take: the call ends there with
   D3DERR_DRIVERINVALIDCALL, so the runtime fails the program's call. */
#define DP2_REFUSED()    \
    do {                 \
        *status = 2;     \
        return at0;      \
    } while (0)

static const uint8_t *
dp2_command(dp2_t *dp, const uint8_t *at, const uint8_t *end, DWORD *rstates, int *status)
{
    d3d_ctx_t *const     c   = dp->c;
    const uint8_t *const at0 = at;
    const DWORD    op    = at[0];
    const DWORD    count = rd16(at + 2);
    const uint8_t *data  = at + 4;
    const DWORD    state = state_bytes(at, end);
    const DWORD    drawn = draw_bytes(dp, at);
    DWORD          i;

    if ((state == ~0u) || (drawn > (DWORD) (end - at)))
        DP2_UNPARSED();
    dp->op = op;
    DIAG({
        if ((op >= D3DDP2OP_CREATEVERTEXSHADER) && (op <= D3DDP2OP_ADDDIRTYBOX))
            diag.x8_cmds[op - D3DDP2OP_CREATEVERTEXSHADER]++;
        /* Its handles, as the command gives them (state_bytes checked
           its length): FVF codes and shaders. */
        if (op == D3DDP2OP_SETVERTEXSHADER) {
            DWORD k;

            for (k = 0; k < count; k++) {
                const DWORD h = rd32(data + 4 * k);

                if (h & 1)
                    diag.x8_vs_decl++;
                else if (h)
                    diag.x8_vs_fvf++;
            }
        }
    });
    if (state) {
        /* Recorded into the state block being recorded, else applied. */
        if (c->sb_rec)
            d3d7_sb_record(c, at, state);
        else {
            state_apply(c, at, rstates);
            if (dp->x8 && (op == D3DDP2OP_SETVERTEXSHADER))
                x8_imm_stride(dp);
        }
        at += state;
        return at;
    }
    switch (op) {
        case D3DDP2OP_POINTS:
            if (prepare(dp)) {
                for (i = 0; i < count; i++)
                    if (!ldraw(dp, VGPU98_PRIM_POINTS, rd16(data + 4 * i + 2), rd16(data + 4 * i), NULL, NULL, NULL))
                        dp->ok = 0;
            }
            at = data + 4 * count;
            return at;
        case D3DDP2OP_LINELIST:
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_LINES, rd16(data), 2 * count, NULL, NULL, NULL))
                dp->ok = 0;
            at = data + 2;
            return at;
        case D3DDP2OP_LINESTRIP:
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_LINE_STRIP, rd16(data), count + 1, NULL, NULL, NULL))
                dp->ok = 0;
            at = data + 2;
            return at;
        case D3DDP2OP_TRIANGLELIST:
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_TRIANGLES, rd16(data), 3 * count, NULL, NULL, NULL))
                dp->ok = 0;
            at = data + 2;
            return at;
        case D3DDP2OP_TRIANGLESTRIP:
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_TRIANGLE_STRIP, rd16(data), count + 2, NULL, NULL, NULL))
                dp->ok = 0;
            at = data + 2;
            return at;
        case D3DDP2OP_TRIANGLEFAN:
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_TRIANGLE_FAN, rd16(data), count + 2, NULL, NULL, NULL))
                dp->ok = 0;
            at = data + 2;
            return at;
        case D3DDP2OP_INDEXEDLINELIST:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_LINES, 0, 2 * count, data))
                dp->ok = 0;
            at = data + 4 * count;
            return at;
        case D3DDP2OP_INDEXEDTRIANGLELIST: {
            /* wV1, wV2, wV3, wFlags: the indices are 3 of every 4 words,
               the edge flags in wFlags. */
            idx_src_t s;

            s.p       = data;
            s.records = 1;
            if (prepare(dp) && !ldraw(dp, VGPU98_PRIM_TRIANGLES, 0, 3 * count, &s, tri_edges, data))
                dp->ok = 0;
            at = data + 8 * count;
            return at;
        }
        case D3DDP2OP_INDEXEDLINELIST2:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_LINES, rd16(data), 2 * count, data + 2))
                dp->ok = 0;
            at = data + 2 + 4 * count;
            return at;
        case D3DDP2OP_INDEXEDLINESTRIP:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_LINE_STRIP, rd16(data), count + 1, data + 2))
                dp->ok = 0;
            at = data + 2 + 2 * (count + 1);
            return at;
        case D3DDP2OP_INDEXEDTRIANGLELIST2:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_TRIANGLES, rd16(data), 3 * count, data + 2))
                dp->ok = 0;
            at = data + 2 + 6 * count;
            return at;
        case D3DDP2OP_INDEXEDTRIANGLESTRIP:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_TRIANGLE_STRIP, rd16(data), count + 2, data + 2))
                dp->ok = 0;
            at = data + 2 + 2 * (count + 2);
            return at;
        case D3DDP2OP_INDEXEDTRIANGLEFAN:
            if (prepare(dp) && !draw_indexed(dp, VGPU98_PRIM_TRIANGLE_FAN, rd16(data), count + 2, data + 2))
                dp->ok = 0;
            at = data + 2 + 2 * (count + 2);
            return at;
        case D3DDP2OP_TRIANGLEFAN_IMM: {
            const DWORD    n    = count + 2;
            const uint8_t *verts = (const uint8_t *) (((uintptr_t) (data + 4) + 3) & ~(uintptr_t) 3);
            fan_edges_t    fe;

            fe.flags = rd32(data);
            fe.ntri  = count;
            if (dp->x8) {
                if (prepare(dp) && !x8_imm(dp, VGPU98_PRIM_TRIANGLE_FAN, verts, n, fan_edge, &fe))
                    dp->ok = 0;
            } else if (prepare(dp) && dp->ok && (n <= dp->room)) {
                DWORD base = imm_base(dp, n);

                dp->imm = 1;
                if (!send_setup(dp) || !send_vertices(dp, base, verts, n) ||
                    !draw(dp, VGPU98_PRIM_TRIANGLE_FAN, base, n, NULL, fan_edge, &fe))
                    dp->ok = 0;
                dp->imm = 0;
            }
            at = verts + n * dp->stride;
            return at;
        }
        case D3DDP2OP_LINELIST_IMM: {
            const DWORD    n     = 2 * count;
            const uint8_t *verts = (const uint8_t *) (((uintptr_t) data + 3) & ~(uintptr_t) 3);

            if (dp->x8) {
                if (prepare(dp) && !x8_imm(dp, VGPU98_PRIM_LINES, verts, n, NULL, NULL))
                    dp->ok = 0;
            } else if (prepare(dp) && dp->ok && (n <= dp->room)) {
                DWORD base = imm_base(dp, n);

                dp->imm = 1;
                if (!send_setup(dp) || !send_vertices(dp, base, verts, n) ||
                    !draw(dp, VGPU98_PRIM_LINES, base, n, NULL, NULL, NULL))
                    dp->ok = 0;
                dp->imm = 0;
            }
            at = verts + n * dp->stride;
            return at;
        }
        /* The DirectX 7 commands */
        case D3DDP2OP_SETRENDERTARGET:
            /* The last of them counts. DirectX 7 keeps the viewport (and
               the material) across it, unlike the older interfaces. */
            if ((DWORD) (end - data) < 8 * count)
                DP2_UNPARSED();
            if (count) {
                /* What the batch drew so far keeps its fence. */
                if (dp->drew && c->target)
                    ctx_batch_surface(c->target);
                if (dp->drew && c->zbuf)
                    ctx_batch_surface(c->zbuf);
                c->h_target = rd32(data + 8 * (count - 1));
                c->h_zbuf   = rd32(data + 8 * (count - 1) + 4);
                ctx_resolve(c);
                c->clean = 0;
            }
            at = data + 8 * count;
            return at;
        case D3DDP2OP_CLEAR:
            /* The flags, colour, depth and stencil, then the RECTs. */
            if ((DWORD) (end - data) < 16 + 16 * count)
                DP2_UNPARSED();
            if (dp->ok && count) {
                if (!ctx_clear(c, rd32(data), rd32(data + 4), rdf(data + 8), rd32(data + 12),
                               (const D3DRECT *) (data + 16), count))
                    dp->ok = 0;
                dp->drew = 1;
            }
            at = data + 16 + 16 * count;
            return at;
        case D3DDP2OP_TEXBLT:
            if ((DWORD) (end - data) < 36 * count)
                DP2_UNPARSED();
            for (i = 0; dp->ok && (i < count); i++) {
                const uint8_t *t = data + 36 * i;
                RECTL          r;

                r.left   = (LONG) rd32(t + 16);
                r.top    = (LONG) rd32(t + 20);
                r.right  = (LONG) rd32(t + 24);
                r.bottom = (LONG) rd32(t + 28);
                if (diag_textures)
                    tt_texblt(c, rd32(t), rd32(t + 4), (LONG) rd32(t + 8), (LONG) rd32(t + 12), &r, rd32(t + 32));
                if (!d3d7_texblt(c, rd32(t), rd32(t + 4), (LONG) rd32(t + 8), (LONG) rd32(t + 12), &r, rd32(t + 32)))
                    dp->ok = 0;
                dp->drew = 1;
            }
            at = data + 36 * count;
            return at;
        case D3DDP2OP_SETPALETTE:
            if ((DWORD) (end - data) < 12 * count)
                DP2_UNPARSED();
            /* The same binding again leaves the context clean. */
            for (i = 0; i < count; i++) {
                const int changed = d3d7_set_palette(c->ddl, rd32(data + 12 * i + 8), rd32(data + 12 * i), rd32(data + 12 * i + 4));

                if (changed)
                    c->clean = 0;
                if (diag_textures)
                    tt_palette(c, 0, rd32(data + 12 * i), rd32(data + 12 * i + 8), rd32(data + 12 * i + 4), 0, changed);
            }
            at = data + 12 * count;
            return at;
        case D3DDP2OP_UPDATEPALETTE:
            /* One update and its entries, whatever the count. */
            if (((DWORD) (end - data) < 8) || ((DWORD) (end - data) < 8 + 4 * rd16(data + 6)))
                DP2_UNPARSED();
            /* The same entries again leave the context clean. */
            {
                const int changed = d3d7_update_palette(c->ddl, rd32(data), rd16(data + 4), rd16(data + 6), data + 8);

                if (changed)
                    c->clean = 0;
                if (diag_textures)
                    tt_palette(c, 1, rd32(data), rd16(data + 4), rd16(data + 6), rd16(data + 6) ? rd32(data + 8) : 0, changed);
            }
            at = data + 8 + 4 * rd16(data + 6);
            return at;
        case D3DDP2OP_STATESET:
            if ((DWORD) (end - data) < 12 * count)
                DP2_UNPARSED();
            for (i = 0; i < count; i++)
                stateset(c, rd32(data + 12 * i), rd32(data + 12 * i + 4));
            if (dp->x8)
                x8_imm_stride(dp); /* EXECUTE may have set a vertex shader */
            at = data + 12 * count;
            return at;
        case D3DDP2OP_SETPRIORITY:
        case D3DDP2OP_SETTEXLOD:
            /* Hints for textures a driver manages itself: none here. */
            if ((DWORD) (end - data) < 8 * count)
                DP2_UNPARSED();
            at = data + 8 * count;
            return at;
        /* The DirectX 8 commands (a DirectX 8 context's; the header's
           count is the number of records). */
        case D3DDP2OP_CREATEVERTEXSHADER: {
            const uint8_t *q = data;

            for (i = 0; i < count; i++) {
                DWORD ds;
                DWORD cs;
                int   made;

                if ((DWORD) (end - q) < 12)
                    DP2_UNPARSED();
                ds = rd32(q + 4);
                cs = rd32(q + 8);
                if ((ds > (DWORD) (end - q) - 12) || (cs > (DWORD) (end - q) - 12 - ds))
                    DP2_UNPARSED();
                made = d3d8_vs_create(c, rd32(q), q + 12, ds, q + 12 + ds, cs);
                if (made < 0) {
                    dp->x8_decl_ok = 0;
                    DP2_REFUSED();
                }
                if (!made)
                    dbg_event(DLL_TAG ": no room for vertex shader %x", rd32(q));
                DIAG({
                    const d3d_vs_t *v = d3d8_vs_find(c, rd32(q));

                    if (v && v->unusable)
                        diag.x8_shaders_bad++;
                    else
                        diag.x8_shaders++;
                    /* A function's version token: vs 1.0 FFFE0100h, 1.1 FFFE0101h. */
                    if (cs >= 4) {
                        const DWORD ver = rd32(q + 12 + ds);

                        diag.vs_versions[(ver == 0xFFFE0100u) ? 0 : (ver == 0xFFFE0101u) ? 1 : 2]++;
                    }
                });
                q += 12 + ds + cs;
            }
            dp->x8_decl_ok = 0;
            at           = q;
            return at;
        }
        case D3DDP2OP_DELETEVERTEXSHADER:
            if ((DWORD) (end - data) < 4 * count)
                DP2_UNPARSED();
            for (i = 0; i < count; i++)
                d3d8_vs_delete(c, rd32(data + 4 * i));
            dp->x8_decl_ok = 0;
            at           = data + 4 * count;
            return at;
        case D3DDP2OP_CREATEPIXELSHADER: {
            const uint8_t *q = data;

            for (i = 0; i < count; i++) {
                if (((DWORD) (end - q) < 8) || (rd32(q + 4) > (DWORD) (end - q) - 8))
                    DP2_UNPARSED();
                DIAG({
                    diag.x8_pshaders++;
                    /* Its version token: ps 1.n FFFF010nh. */
                    if (rd32(q + 4) >= 4) {
                        const DWORD ver = rd32(q + 8);

                        diag.ps_versions[((ver & 0xFFFFFF00u) == 0xFFFF0100u) && ((ver & 0xff) <= 4) ? (ver & 0xff) : 5]++;
                    }
                });
                /* With programs, one not made fails the call. */
                if ((diag_shaders || d3d8_programs) && !d3d8_ps_create(c, rd32(q), q + 8, rd32(q + 4)) && d3d8_programs)
                    DP2_REFUSED();
                q += 8 + rd32(q + 4);
            }
            at = q;
            return at;
        }
        case D3DDP2OP_DELETEPIXELSHADER:
            if ((DWORD) (end - data) < 4 * count)
                DP2_UNPARSED();
            /* The shaders kept, with programs or capture (SETPIXELSHADER is
               a state command: state_apply). */
            for (i = 0; (diag_shaders || d3d8_programs) && (i < count); i++)
                d3d8_ps_delete(c, rd32(data + 4 * i));
            at = data + 4 * count;
            return at;
        case D3DDP2OP_SETSTREAMSOURCEUM:
            /* Stream 0 is the call's user memory, at this stride. */
            if ((DWORD) (end - data) < 8 * count)
                DP2_UNPARSED();
            for (i = 0; i < count; i++) {
                if (rd32(data + 8 * i) == 0) {
                    c->um        = 1;
                    c->um_stride = rd32(data + 8 * i + 4);
                }
            }
            at = data + 8 * count;
            return at;
        case D3DDP2OP_DRAWPRIMITIVE:
            if ((DWORD) (end - data) < 12 * count)
                DP2_UNPARSED();
            if (prepare(dp)) {
                for (i = 0; i < count; i++) {
                    const uint8_t *r = data + 12 * i;
                    const DWORD    prim = rd32(r);

                    if (!x8_draw(dp, prim, 0, rd32(r + 4), prim_vertices(prim, rd32(r + 8)), 0, NULL, 0, NULL, NULL))
                        dp->ok = 0;
                }
            }
            at = data + 12 * count;
            return at;
        case D3DDP2OP_DRAWINDEXEDPRIMITIVE:
            if ((DWORD) (end - data) < 24 * count)
                DP2_UNPARSED();
            if (prepare(dp)) {
                for (i = 0; i < count; i++) {
                    const uint8_t *r    = data + 24 * i;
                    const DWORD    prim = rd32(r);

                    dp->x8_window = 1;
                    dp->x8_min    = rd32(r + 8);
                    dp->x8_nverts = rd32(r + 12);
                    if (diag_audit8)
                        x8_raw_record(dp, op, r);
                    if (!x8_draw(dp, prim, (LONG) rd32(r + 4), rd32(r + 16), prim_vertices(prim, rd32(r + 20)), 1, NULL, 0,
                                 NULL, NULL))
                        dp->ok = 0;
                    dp->x8_window      = 0;
                    dp->x8_raw_op      = 0;
                    dp->x8_trace_range = 0;
                }
            }
            at = data + 24 * count;
            return at;
        case D3DDP2OP_DRAWPRIMITIVE2:
            if ((DWORD) (end - data) < 12 * count)
                DP2_UNPARSED();
            if (prepare(dp)) {
                for (i = 0; i < count; i++) {
                    const uint8_t *r    = data + 12 * i;
                    const DWORD    prim = rd32(r);

                    if (!x8_draw_bytes(dp, prim, (LONG) rd32(r + 4), 0, prim_vertices(prim, rd32(r + 8)), 0, NULL, NULL))
                        dp->ok = 0;
                }
            }
            at = data + 12 * count;
            return at;
        case D3DDP2OP_DRAWINDEXEDPRIMITIVE2:
            if ((DWORD) (end - data) < 24 * count)
                DP2_UNPARSED();
            if (prepare(dp)) {
                for (i = 0; i < count; i++) {
                    const uint8_t *r     = data + 24 * i;
                    const DWORD    prim  = rd32(r);
                    const DWORD    isize = (c->ib_stride == 4) ? 4 : 2;

                    /* StartIndexOffset is in bytes. */
                    dp->x8_window = 1;
                    dp->x8_min    = rd32(r + 8);
                    dp->x8_nverts = rd32(r + 12);
                    if (diag_audit8) {
                        x8_raw_record(dp, op, r);
                        dp->x8_trace_range = 0;
                        /* The first of a context's, as they come. */
                        if (c->trace_dip2) {
                            c->trace_dip2--;
                            x8_trace_raw(dp, TRACE_DIP2);
                            dp->x8_trace_range = 1;
                        }
                    }
                    if (rd32(r + 16) % isize) {
                        if (diag_audit8 && rd32(r + 20)) /* an empty one draws nothing either way */
                            audit8_skipped(dp, prim, (LONG) rd32(r + 4), rd32(r + 16), prim_vertices(prim, rd32(r + 20)), 1,
                                           A8_UNTAKEN, A8U_START, rd32(r + 16), isize);
                        x8_skip(X8_SKIP_INDICES);
                    }
                    else {
                        dp->x8_dip2 = 1;
                        if (!x8_draw_bytes(dp, prim, (LONG) rd32(r + 4), rd32(r + 16) / isize, prim_vertices(prim, rd32(r + 20)),
                                           1, NULL, NULL))
                            dp->ok = 0;
                        dp->x8_dip2 = 0;
                    }
                    dp->x8_window      = 0;
                    dp->x8_raw_op      = 0;
                    dp->x8_trace_range = 0;
                }
            }
            at = data + 24 * count;
            return at;
        case D3DDP2OP_CLIPPEDTRIANGLEFAN:
            /* A fan the runtime clipped, its edges flagged as
               TRIANGLEFAN_IMM's. */
            if ((DWORD) (end - data) < 12 * count)
                DP2_UNPARSED();
            if (prepare(dp)) {
                for (i = 0; i < count; i++) {
                    const uint8_t *r = data + 12 * i;
                    fan_edges_t    fe;

                    fe.flags = rd32(r + 4);
                    fe.ntri  = rd32(r + 8);
                    /* Its edge flags go in the DRAW: a count of 32 bits
                       could make one longer than the ring takes. */
                    if (fe.ntri > 0xffff)
                        x8_skip(X8_SKIP_SIZE);
                    else if (fe.ntri && !x8_draw_bytes(dp, VGPU98_PRIM_TRIANGLE_FAN, (LONG) rd32(r), 0,
                                                       prim_vertices(VGPU98_PRIM_TRIANGLE_FAN, fe.ntri), 0, fan_edge, &fe))
                        dp->ok = 0;
                }
            }
            at = data + 12 * count;
            return at;
        case D3DDP2OP_DRAWRECTPATCH:
        case D3DDP2OP_DRAWTRIPATCH: {
            /* Patches, which the caps do not offer: stepped over. */
            const uint8_t *q     = data;
            const DWORD    segs  = (op == D3DDP2OP_DRAWRECTPATCH) ? 16 : 12;
            const DWORD    info  = (op == D3DDP2OP_DRAWRECTPATCH) ? 28 : 16;

            for (i = 0; i < count; i++) {
                DWORD size;

                if ((DWORD) (end - q) < 8)
                    DP2_UNPARSED();
                size = 8 + ((rd32(q + 4) & RTPATCHFLAG_HASSEGS) ? segs : 0) + ((rd32(q + 4) & RTPATCHFLAG_HASINFO) ? info : 0);
                if ((DWORD) (end - q) < size)
                    DP2_UNPARSED();
                q += size;
                DIAG(diag.x8_patches++);
            }
            at = q;
            return at;
        }
        case D3DDP2OP_VOLUMEBLT:
            if ((DWORD) (end - data) < 48 * count)
                DP2_UNPARSED();
            for (i = 0; dp->ok && (i < count); i++) {
                const uint8_t *t = data + 48 * i;
                DWORD          box[6];
                DWORD          k;

                for (k = 0; k < 6; k++)
                    box[k] = rd32(t + 20 + 4 * k);
                if (!d3d7_volblt(c, rd32(t), rd32(t + 4), (LONG) rd32(t + 8), (LONG) rd32(t + 12), (LONG) rd32(t + 16),
                                 box))
                    dp->ok = 0;
                dp->drew = 1;
            }
            at = data + 48 * count;
            return at;
        case D3DDP2OP_BUFFERBLT:
            if ((DWORD) (end - data) < 24 * count)
                DP2_UNPARSED();
            for (i = 0; i < count; i++) {
                const uint8_t *t = data + 24 * i;

                if (!x8_bufblt(dp, rd32(t), rd32(t + 4), rd32(t + 8), rd32(t + 12), rd32(t + 16)))
                    dp->ok = 0;
            }
            at = data + 24 * count;
            return at;
        case D3DDP2OP_MULTIPLYTRANSFORM:
            /* The matrix times the one set: DirectX 8's "pMatrix times
               State". */
            if ((DWORD) (end - data) < 68 * count)
                DP2_UNPARSED();
            for (i = 0; i < count; i++) {
                const uint8_t *t = data + 68 * i;
                float          m[16];
                float          cur[16];
                float          r[16];
                int            k;

                for (k = 0; k < 16; k++) {
                    m[k]   = rdf(t + 4 + 4 * k);
                    cur[k] = ((k % 5) == 0) ? 1.0f : 0.0f;
                }
                ctx_get_transform(c, rd32(t), cur);
                mat_mul(r, m, cur);
                ctx_set_transform(c, rd32(t), r);
            }
            at = data + 68 * count;
            return at;
        case D3DDP2OP_ADDDIRTYRECT:
        case D3DDP2OP_ADDDIRTYBOX: {
            /* Hints for resources a driver manages itself: none here. */
            const DWORD unit = (op == D3DDP2OP_ADDDIRTYRECT) ? 20 : 28;

            if ((DWORD) (end - data) < unit * count)
                DP2_UNPARSED();
            at = data + unit * count;
            return at;
        }
        default: {
            /* Not a DirectX 6 or 7 command: the runtime's own parser may
               know it (the old execute-buffer opcodes). */
            void   *next = NULL;
            HRESULT r    = d3d_parse_unknown ? d3d_parse_unknown((void *) at, &next) : D3DERR_COMMAND_UNPARSED;

            if ((r != D3D_OK) || !next || ((const uint8_t *) next <= at))
                DP2_UNPARSED();
            at = next;
            return at;
        }
    }
    return at;
}

/* Where the command at `at` ends, by its kind and counts, as dp2_command
   reads it: the command after one that faulted. NULL when it cannot be told
   (the runtime's own commands, a count past the buffer): the call ends
   there. */
static const uint8_t *
dp2_next(const dp2_t *dp, const uint8_t *at, const uint8_t *end)
{
    const DWORD    op    = at[0];
    const DWORD    count = rd16(at + 2);
    const uint8_t *data  = at + 4;
    const DWORD    state = state_bytes(at, end);
    const uint8_t *q     = NULL;
    DWORD          unit  = 0;
    DWORD          i;

    if (state == ~0u)
        return NULL;
    if (state)
        return at + state;
    switch (op) {
        case D3DDP2OP_POINTS:
        case D3DDP2OP_INDEXEDLINELIST:
        case D3DDP2OP_DELETEVERTEXSHADER:
        case D3DDP2OP_DELETEPIXELSHADER:
        case D3DDP2OP_SETPIXELSHADER:
            unit = 4;
            break;
        case D3DDP2OP_LINELIST:
        case D3DDP2OP_LINESTRIP:
        case D3DDP2OP_TRIANGLELIST:
        case D3DDP2OP_TRIANGLESTRIP:
        case D3DDP2OP_TRIANGLEFAN:
            q = data + 2;
            break;
        case D3DDP2OP_INDEXEDTRIANGLELIST:
        case D3DDP2OP_SETRENDERTARGET:
        case D3DDP2OP_SETPRIORITY:
        case D3DDP2OP_SETTEXLOD:
        case D3DDP2OP_SETSTREAMSOURCEUM:
            unit = 8;
            break;
        case D3DDP2OP_INDEXEDLINELIST2:
            q = data + 2 + 4 * (uint64_t) count;
            break;
        case D3DDP2OP_INDEXEDLINESTRIP:
            q = data + 2 + 2 * ((uint64_t) count + 1);
            break;
        case D3DDP2OP_INDEXEDTRIANGLELIST2:
            q = data + 2 + 6 * (uint64_t) count;
            break;
        case D3DDP2OP_INDEXEDTRIANGLESTRIP:
        case D3DDP2OP_INDEXEDTRIANGLEFAN:
            q = data + 2 + 2 * ((uint64_t) count + 2);
            break;
        case D3DDP2OP_TRIANGLEFAN_IMM:
            q = (const uint8_t *) (((uintptr_t) (data + 4) + 3) & ~(uintptr_t) 3) + ((uint64_t) count + 2) * dp->stride;
            break;
        case D3DDP2OP_LINELIST_IMM:
            q = (const uint8_t *) (((uintptr_t) data + 3) & ~(uintptr_t) 3) + 2 * (uint64_t) count * dp->stride;
            break;
        case D3DDP2OP_CLEAR:
            q = data + 16 + 16 * (uint64_t) count;
            break;
        case D3DDP2OP_TEXBLT:
            unit = 36;
            break;
        case D3DDP2OP_SETPALETTE:
        case D3DDP2OP_STATESET:
        case D3DDP2OP_DRAWPRIMITIVE:
        case D3DDP2OP_DRAWPRIMITIVE2:
        case D3DDP2OP_CLIPPEDTRIANGLEFAN:
            unit = 12;
            break;
        case D3DDP2OP_UPDATEPALETTE:
            if ((DWORD) (end - data) < 8)
                return NULL;
            q = data + 8 + 4 * (uint64_t) rd16(data + 6);
            break;
        case D3DDP2OP_DRAWINDEXEDPRIMITIVE:
        case D3DDP2OP_DRAWINDEXEDPRIMITIVE2:
        case D3DDP2OP_BUFFERBLT:
            unit = 24;
            break;
        case D3DDP2OP_ADDDIRTYRECT:
            unit = 20;
            break;
        case D3DDP2OP_ADDDIRTYBOX:
            unit = 28;
            break;
        case D3DDP2OP_VOLUMEBLT:
            unit = 48;
            break;
        case D3DDP2OP_MULTIPLYTRANSFORM:
            unit = 68;
            break;
        case D3DDP2OP_CREATEVERTEXSHADER:
            for (q = data, i = 0; i < count; i++) {
                DWORD ds;
                DWORD cs;

                if ((DWORD) (end - q) < 12)
                    return NULL;
                ds = rd32(q + 4);
                cs = rd32(q + 8);
                if ((ds > (DWORD) (end - q) - 12) || (cs > (DWORD) (end - q) - 12 - ds))
                    return NULL;
                q += 12 + ds + cs;
            }
            break;
        case D3DDP2OP_CREATEPIXELSHADER:
            for (q = data, i = 0; i < count; i++) {
                if (((DWORD) (end - q) < 8) || (rd32(q + 4) > (DWORD) (end - q) - 8))
                    return NULL;
                q += 8 + rd32(q + 4);
            }
            break;
        case D3DDP2OP_SETVERTEXSHADERCONST:
        case D3DDP2OP_SETPIXELSHADERCONST:
            for (q = data, i = 0; i < count; i++) {
                if (((DWORD) (end - q) < 8) || (rd32(q + 4) > ((DWORD) (end - q) - 8) / 16))
                    return NULL;
                q += 8 + 16 * rd32(q + 4);
            }
            break;
        case D3DDP2OP_DRAWRECTPATCH:
        case D3DDP2OP_DRAWTRIPATCH: {
            const DWORD segs = (op == D3DDP2OP_DRAWRECTPATCH) ? 16 : 12;
            const DWORD info = (op == D3DDP2OP_DRAWRECTPATCH) ? 28 : 16;

            for (q = data, i = 0; i < count; i++) {
                DWORD size;

                if ((DWORD) (end - q) < 8)
                    return NULL;
                size = 8 + ((rd32(q + 4) & RTPATCHFLAG_HASSEGS) ? segs : 0) + ((rd32(q + 4) & RTPATCHFLAG_HASINFO) ? info : 0);
                if ((DWORD) (end - q) < size)
                    return NULL;
                q += size;
            }
            break;
        }
        default:
            return NULL;
    }
    if (unit) {
        if ((uint64_t) unit * count > (DWORD) (end - data))
            return NULL;
        q = data + unit * count;
    }
    return (q && (q > at) && (q <= end)) ? q : NULL;
}

/* A command that faulted: its line (with its place in the buffer), this
   thread's locks released, its packets dropped (the ring back to where it
   began, or to the last doorbell: the state epoch moves, so every context
   sends its whole state again), the call's windows and chunks in the inline
   buffer, its resolved buffers and the command's own flags forgotten; 86Box
   asked for a capture. A fault while a BUFFERBLT had let the ring lock go
   (x8_break) leaves the rest of the call drawing nothing. */
static void
dp2_fault(dp2_t *dp, const guard_fault_t *f, DWORD offset, DWORD number, const gpu_mark_t *m)
{
    char  cmd[48];
    char *p   = cmd;
    char *e   = cmd + sizeof(cmd) - 1;
    DWORD v[3];
    DWORD k;

    /* " cmd <op> #<number> @<offset>" (short: the line holds 160 characters) */
    v[0] = dp->op;
    v[1] = number;
    v[2] = offset;
    for (k = 0; k < 3; k++) {
        static const char *const lead[3] = { " cmd ", " #", " @" };
        const char              *l       = lead[k];
        char                     t[12];
        DWORD                    n = 0;
        DWORD                    x = v[k];

        while (*l && (p < e))
            *p++ = *l++;
        do {
            const DWORD dg = (k == 2) ? (x & 15) : (x % 10);

            t[n++] = (char) ((dg < 10) ? '0' + dg : 'A' + dg - 10);
            x      = (k == 2) ? (x >> 4) : (x / 10);
        } while (x && (n < sizeof(t)));
        while (n && (p < e))
            *p++ = t[--n];
    }
    *p = 0;
    guard_report("DrawPrimitives2", cmd, dp->op, f);
    dlock_release_thread();
    flight_fault(dp->c);
    if (m)
        gpu_unwind(m);
    if (!dp->held && !gpu_holds()) {
        dp->held = 1;
        dp->ok   = 0;
    }
    x8_wrap(dp);
    memset(dp->sent, 0, sizeof(dp->sent));
    dp->x8_raw_op = 0;
    memset(dp->x8_buf, 0, sizeof(dp->x8_buf));
    dp->setup     = 0;
    dp->stream    = STREAM_NONE;
    dp->imm       = 0;
    dp->x8_pieces = 0;
    dp->x8_window = 0;
    dp->x8_dip2   = 0;
    guard_capture();
}

typedef struct dp2_cmd {
    dp2_t         *d;
    const uint8_t *at;
    const uint8_t *end;
    DWORD         *rstates;
    const uint8_t *next;
    int            status;
} dp2_cmd_t;

static DWORD STDCALL
dp2_command_g(void *p)
{
    dp2_cmd_t *k = (dp2_cmd_t *) p;

    k->next = dp2_command(k->d, k->at, k->end, k->rstates, &k->status);
    return 1;
}

void
dp2_fence_read(DWORD fence)
{
    x8_fence(fence);
}

/* A bound buffer settled before the ring lock, inside a guard of its own: a
   fault resolving its handle costs the settling, and the draws that use it
   then fault and are named one by one. */
typedef struct dp2_settle {
    void *ddl;
    DWORD slot;
    DWORD handle;
} dp2_settle_t;

static DWORD STDCALL
dp2_settle_g(void *p)
{
    const dp2_settle_t *k = (const dp2_settle_t *) p;

    GUARD_STEP(GUARD_STEP_SETTLE, k->slot, k->handle);
    pool_settle(d3d7_buffer(k->ddl, k->handle));
    return 1;
}

static void
dp2_settle(d3d_ctx_t *c, DWORD slot, DWORD handle)
{
    dp2_settle_t  k;
    guard_fault_t f;

    k.ddl    = c->ddl;
    k.slot   = slot;
    k.handle = handle;
    guard_run(dp2_settle_g, &k, &f);
    if (f.faulted)
        guard_recover("DrawPrimitives2", &f);
}

/* The call's time (rdtsc, with Diagnostics) goes into phases: from `t0` to
   its first command the setup (the vertex format, the buffers, the ring
   lock), the state flushes, the draws (the rest of the commands: indices,
   vertices, packets); the end (the fence, the records) is what is left of
   DrawPrimitives2's whole time. */
static DWORD
draw_primitives2(D3DHAL_DRAWPRIMITIVES2DATA *dd, uint64_t t0)
{
    d3d_ctx_t     *c = ctx_get(dd->dwhContext);
    dp2_t          d;
    const uint8_t *cmds;
    const uint8_t *at;
    const uint8_t *end;
    DWORD         *rstates = (dd->dwFlags & D3DHALDP2_EXECUTEBUFFER) ? dd->lpdwRStates : NULL;
    DWORD          fence;
    HRESULT        hr;
    int            held;
    uint64_t       t_cmds;
    DWORD          n_cmd = 0; /* the commands read (a fault's line numbers them) */

    dd->dwErrorOffset = 0;
    if (!c) {
        dd->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    DIAG(diag.batches++);
    if (c->dx8)
        ctx_recheck(c);
    /* A target in a flipping chain: the member the next flip shows. */
    ctx_target_follow(c);
    /* The context's state as the call before left it, unless something it
       was built from changed outside its state commands: only then does
       the first draw check everything. */
    if (c->clean && !ctx_unchanged(c))
        c->clean = 0;
    memset(&d, 0, sizeof(d));
    d.c  = c;
    d.ok = 1;
    d.x8 = c->dx8;
    DIAG(diag.batches8 += d.x8 != 0);
    if (d.x8) {
        /* No call vertices: the streams the commands bound, user memory
           for SETSTREAMSOURCEUM's stream 0 (the call's pointer: the DDK's
           sample takes no dwVertexOffset). Buffers in system memory move
           into the pool now if they can, outside the ring lock. */
        DWORD s;

        d.drawable    = 1;
        d.um          = (dd->dwFlags & D3DHALDP2_USERMEMVERTICES) ? (const uint8_t *) dd->lpVertices : NULL;
        d.um_vertices = dd->dwVertexLength;
        x8_imm_stride(&d);
        for (s = 0; s < D3D_STREAMS; s++) {
            if (c->stream[s].handle)
                dp2_settle(c, s, c->stream[s].handle);
        }
        if (c->ib)
            dp2_settle(c, D3D_STREAMS, c->ib);
    } else
        d.drawable = fvf_parse(&d, dd->dwVertexType);
    if (!d.x8 && dd->lpVertices && !(dd->dwFlags & D3DHALDP2_USERMEMVERTICES))
        pool_settle(dd->lpDDVertex);
    if (!d.x8 && dd->lpVertices && !pool_source(&d, dd))
        d.vtx = (dd->dwFlags & D3DHALDP2_USERMEMVERTICES)
                    ? (const uint8_t *) dd->lpVertices + dd->dwVertexOffset
                    : (const uint8_t *) dd->lpDDVertex->lpGbl->fpVidMem + dd->dwVertexOffset;
    /* Through the ring: a buffer of the DLL's every vertex from
       dwVertexOffset to its end, as the pool's stream has them (a draw that
       reaches past the count its call names still finds its vertices, and
       none past the buffer is read); user memory's and DirectDraw's own
       buffers' only those the call names, the rest being no memory the
       driver knows the size of. Only the chunks a draw uses are sent, and
       only to the count the call names until a draw reaches past it. */
    d.named = d.nvtx = d.vtx ? dd->dwVertexLength : 0;
    if (d.vtx && !(dd->dwFlags & D3DHALDP2_USERMEMVERTICES) && d.stride) {
        const DWORD bytes = exebuf_bytes(dd->lpDDVertex);

        if (bytes)
            d.nvtx = (bytes > dd->dwVertexOffset) ? (bytes - dd->dwVertexOffset) / d.stride : 0;
    }
    if (d.nvtx > d.room)
        d.nvtx = d.room;
    if (d.named > d.room)
        d.named = d.room;
    d.limit = (d.named < d.nvtx) ? d.named : d.nvtx;
    if (diag_audit && dd->lpVertices && d.stride) {
        d.call_vertices = dd->dwVertexLength;
        d.call_offset   = dd->dwVertexOffset;
        d.gpu_vertices  = d.pool ? d.pool_size / d.stride : d.nvtx;
    }

    cmds = (const uint8_t *) dd->lpDDCommands->lpGbl->fpVidMem;
    at   = cmds + dd->dwCommandOffset;
    end  = at + dd->dwCommandLength;

    /* The GPU held (a mode change, a DOS session): nothing draws, but the
       state commands still go into the context. The runtime sends a state
       only when it changes, so one dropped here would never come again. */
    /* A context that stopped (the GPU kept refusing its packets) sends
       nothing either, but answers D3D_OK: the program runs on and can be
       closed. */
    hr   = D3D_OK;
    held = 1;
    if (!c->stopped) {
        hr   = gpu_begin();
        held = hr != DD_OK;
        /* The restart in gpu_begin may have stopped this very context. */
        if (!held && c->stopped) {
            gpu_release();
            held = 1;
        }
    }
    if (c->stopped)
        DIAG(diag.calls_dropped++);
    if (held)
        d.ok = 0;
    else {
        gpu_batch_owner(ctx_token(c));
        ctx_batch_begin();
        x8_new_batch();
        /* The texture audit: the process named before anything of the
           batch, so the GPU's lines about it name it. */
        if (diag_textures && !proc_note(c))
            d.ok = 0;
    }
    d.held = held;

    dd->ddrval = hr;
    t_cmds     = diag_on ? rdtsc() : 0;
    while (at + 4 <= end) {
        dp2_cmd_t     k;
        guard_fault_t f;
        gpu_mark_t    m;
        DWORD        *fl;

        if (!d.held)
            gpu_mark(&m);
        fl = flight_command(c, at, end);
        if (c->trace_cmds && diag_audit8) {
            c->trace_cmds--;
            trace_command(c, at, end);
        }
        k.d       = &d;
        k.at      = at;
        k.end     = end;
        k.rstates = rstates;
        k.next    = at;
        k.status  = 0;
        guard_run(dp2_command_g, &k, &f);
        n_cmd++;
        flight_command_end(fl, f.faulted ? FLIGHT_FAULTED : (k.status == 1) ? FLIGHT_UNPARSED : FLIGHT_DONE);
        if (f.faulted) {
            /* Skipped, and the call carries on. */
            const uint8_t *next = dp2_next(&d, at, end);

            dp2_fault(&d, &f, (DWORD) (at - cmds), n_cmd, d.held ? NULL : &m);
            if (!next)
                goto unparsed;
            at = next;
            continue;
        }
        if (k.status == 2)
            goto refused;
        if (k.status)
            goto unparsed;
        at = k.next;
    }
    goto done;

refused:
    dd->ddrval        = D3DERR_DRIVERINVALIDCALL;
    dd->dwErrorOffset = (DWORD) (at - cmds);
    goto done;
unparsed:
    dd->ddrval        = D3DERR_COMMAND_UNPARSED;
    dd->dwErrorOffset = (DWORD) (at - cmds);
done:
    DIAG({
        const uint64_t t_end = rdtsc();

        diag.cyc_dp2_setup += t_cmds - t0;
        diag.cyc_dp2_state += d.cyc_state;
        diag.cyc_dp2_draws += t_end - t_cmds - d.cyc_state;
        diag.vtx_sent += (uint64_t) d.sent_n * d.stride;
        diag.vtx_again += (uint64_t) d.again_n * d.stride;
        diag.idx_sent += (uint64_t) d.idx_n * 2;
        if (d.drew)
            diag.vtx_buffer += (uint64_t) d.named * d.stride;
        if (d.pool_read) {
            if (d.pool->kind != PB_VERTEX)
                diag.buf_runtime += d.pool_used;
            else if (d.pool->dynamic)
                diag.buf_dynamic += d.pool_used;
            else
                diag.buf_static += d.pool_used;
        }
    });
    /* The driver's buffers are all in main memory. */
    dd->dwFlags &= ~(D3DHALDP2_VIDMEMVERTEXBUF | D3DHALDP2_VIDMEMCOMMANDBUF);
    if (!d.held) {
        fence = gpu_finish();
        if (d.drew)
            ctx_used(c, fence);
        if (d.pool_read && d.pool_kept)
            pool_kept_drawn(d.pool, fence);
        else if (d.pool_read)
            pool_drawn(d.pool, fence);
        x8_fence(fence);
        gpu_release();
    }
    /* SWAPVERTEXBUFFER: the runtime's own vertex buffer gets fresh memory now
       (of at least dwReqVertexBufSize bytes with REQVERTEXBUFSIZE, which the
       DDK has the driver honour), so the runtime fills that while the GPU
       still reads this call's vertices (the command buffer the DLL has read
       already). When it cannot have that much, the runtime is told there
       was no swap. */
    if (!d.x8 && dd->lpDDVertex && !(dd->dwFlags & D3DHALDP2_USERMEMVERTICES) && (dd->dwFlags & D3DHALDP2_SWAPVERTEXBUFFER) &&
        !pool_swap(dd->lpDDVertex, (dd->dwFlags & D3DHALDP2_REQVERTEXBUFSIZE) ? dd->dwReqVertexBufSize : 0))
        dd->dwFlags &= ~(D3DHALDP2_SWAPVERTEXBUFFER | D3DHALDP2_REQVERTEXBUFSIZE);
    /* Outside every lock: the pool's tidy when due, and the chunks queued
       to go back handed to the mini-VDD. */
    pool_idle();
    return DDHAL_DRIVER_HANDLED;
}

typedef struct dp2_call {
    D3DHAL_DRAWPRIMITIVES2DATA *dd;
    uint64_t                    t0;
} dp2_call_t;

static DWORD STDCALL
dp2_call_g(void *p)
{
    const dp2_call_t *k = (const dp2_call_t *) p;

    return draw_primitives2(k->dd, k->t0);
}

/* DrawPrimitives2 inside the fault guard: a fault in a command is caught
   inside (the command skipped); one in the call's setup or end fails the
   call. */
DWORD STDCALL
DrawPrimitives2(D3DHAL_DRAWPRIMITIVES2DATA *dd)
{
    dp2_call_t    k;
    guard_fault_t f;
    DWORD         r;

    flight_call_t *fl = flight_enter("DrawPrimitives2");

    k.dd = dd;
    k.t0 = diag_on ? rdtsc() : 0;
    r    = guard_run(dp2_call_g, &k, &f);
    if (f.faulted) {
        guard_recover("DrawPrimitives2", &f);
        dd->ddrval = DDERR_GENERIC;
        r          = DDHAL_DRIVER_HANDLED;
    }
    /* The flight recorder: the context, and where an unparsed call stopped
       (else its commands' length). */
    if (fl)
        flight_leave(fl, r, (DWORD) dd->ddrval, dd->dwhContext,
                     (dd->ddrval == D3DERR_COMMAND_UNPARSED) ? dd->dwErrorOffset : dd->dwCommandLength);
    DIAG(diag.cyc_dp2 += rdtsc() - k.t0);
    return r;
}
