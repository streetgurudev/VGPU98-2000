/*
 * vgpu98dx.dll: the Direct3D HAL's contexts and texture handles, shared by
 * d3d.c (the callbacks), dp2.c (DrawPrimitives2), d3d7.c (the DirectX 7
 * interface's surface handles, palettes, state blocks and TEXBLT) and d3d8.c
 * (the DirectX 8 interface's query, caps, formats and vertex shaders).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_D3D_H
#define VGPU98_D3D_H

#include "dll.h"
#include "ddi8.h"
#include "vid_vgpu98_prog.h"

#define MAX_CONTEXTS   32
#define MAX_TEXTURES   4096
#define D3D_TSS_COUNT  29 /* DirectX 8's stage states 0-28 (DirectX 7 sends 0-24) */
#define D3D_STAGES     8
#define D3D_STREAMS    16 /* DirectX 8's streams */
#define FLIGHT_CMDS    32 /* the DrawPrimitives2 commands a context's flight recorder keeps (a power of two) */
#define D3D8_MAX_STRIDE 256 /* D3DCAPS8's MaxStreamStride */
#define D3D_PALETTE    256 /* DirectX 8's world matrices for indexed vertex blending */
/* A TEXTURE packet's payload at most: a cube map's six faces of levels. */
#define TEX_DESC_MAX   (4 + 2 * VGPU98_TEX_MAX_LEVELS * VGPU98_CUBE_FACES)

/* The stage-0 alpha operation of the legacy MODULATE blend: alpha from the
   texture if it has alpha, else from the diffuse colour. Resolved when the
   state goes to the GPU. */
#define TOP_LEGACY_ALPHAOVR 0x100

/* What changed in a context since its last flush. */
#define DIRTY_RS       0x1u            /* render states: those rs_dirty marks */
#define DIRTY_VIEW     0x2u            /* the viewport or the W range */
#define DIRTY_STAGE(s) (0x100u << (s)) /* a stage's states: those its tss_dirty marks */
#define DIRTY_STAGES   0xFF00u         /* any stage's */
#define DIRTY_ALL      0xffffu

/* What of the transform and lighting state changed (d3d_ctx_t.tl_dirty). */
#define TL_XFORM(n)  (1u << (n)) /* a matrix, by the GPU's TRANSFORM number (0-15) */
#define TL_LIGHTS    0x10000u
#define TL_MATERIAL  0x20000u
#define TL_PLANES    0x40000u
#define TL_ZRANGE    0x80000u
#define TL_PALETTE   0x100000u /* DirectX 8: a world matrix from 4 up */

/* A stream's binding (DirectX 8's SETSTREAMSOURCE): a vertex buffer's
   surface handle (0 none) and the stride. */
typedef struct d3d_stream {
    DWORD handle;
    DWORD stride;
} d3d_stream_t;

/* A shader's GPU program (command set 2.2, prog.c): its id from the table
   every process shares, its instructions in the DLL's heap, the stages a
   pixel program samples, and the state epoch the GPU was last sent it in. */
typedef struct d3d_prog {
    DWORD  id;     /* 1-4095; 0 none */
    DWORD  kind;   /* VGPU98_PROG_VERTEX, VGPU98_PROG_PIXEL */
    DWORD  range;  /* PROGRAM's range (a float's bits; 0 for a vertex program) */
    DWORD  n;      /* instructions, 4 dwords each ... */
    DWORD *ins;    /* ... in the heap */
    DWORD  stages; /* a pixel program's STAGE operands, a bit each */
    DWORD  epoch;  /* sb->state_epoch when the GPU was sent it */
} d3d_prog_t;

/* A vertex shader a DirectX 8 context made (CREATEVERTEXSHADER): its
   declaration as the GPU's DECLARE elements and the streams it reads. One
   with a function is, with PROFILE.PROGRAMS, the GPU's program (its
   declaration INPUT elements, the constants its declaration loads kept);
   without, unusable, as is one with tokens the GPU lacks, so its draws are
   skipped and counted rather than refused by the GPU. */
typedef struct d3d_vs {
    DWORD      handle;
    DWORD      n;       /* elements */
    DWORD      streams; /* bit s: it reads stream s */
    DWORD      unusable; /* 0, or why: VS_FUNCTION, VS_TOKENS */
    DWORD      decl[2 * VGPU98_DECL_MAX];
    d3d_prog_t prog;     /* its function's program (id 0: a declaration alone, the fixed function) */
    DWORD     *consts;   /* the constants loaded at every SETVERTEXSHADER (CONSTMEM, def): register and four values each, in the heap */
    DWORD      n_consts;
    DWORD     *tokens;   /* its function's tokens, in the heap */
    DWORD      n_tokens;
    DWORD      n_in;     /* its function's instructions, for the log ... */
    DWORD      hash;     /* ... a hash of its tokens ... */
    DWORD      drawn;    /* ... its draws (with Diagnostics) ... */
    DWORD      inputs;   /* ... and its program's input registers read and outputs written */
    DWORD      outputs;
} d3d_vs_t;

#define VS_FUNCTION 1 /* a programmable shader without PROFILE.PROGRAMS */
#define VS_TOKENS   2 /* a declaration the GPU's fixed function cannot read */
#define D3D_VS_CONSTS 192 /* the vertex shaders' constants (D3DCAPS8's MaxVertexShaderConst) */

/* A pixel shader a context made (DirectX 8: pshader.c): its tokens, what
   shader capture's log names, and its program. */
#define D3D_PS_CONSTS 8 /* c0-c7, as pixel shaders 1.1 have them */

typedef struct d3d_ps {
    DWORD  handle;
    DWORD  version; /* its version token */
    DWORD  n_instr;
    DWORD  hash;
    DWORD  drawn;    /* draws with it (with Diagnostics) ... */
    DWORD  named;    /* ... its first named (shader capture) */
    DWORD *tokens;   /* in the DLL's heap */
    DWORD  n_tokens;
    DWORD  cread;    /* the constants (c0-c7) it reads, a bit each ... */
    DWORD  tread;    /* ... the texture registers (t0-t7) ... */
    DWORD  tsampled; /* ... the stages it samples ... */
    DWORD  bump;     /* ... the stages it bumps (texbem, texbeml, bem) ... */
    DWORD  def_mask; /* ... and the constants DEF defines, with their values */
    DWORD  def_val[D3D_PS_CONSTS][4];
    d3d_prog_t prog; /* with PROFILE.PROGRAMS, its program */
} d3d_ps_t;

/* An entry of a context's light table (DirectX 7: CREATELIGHT, SETLIGHT):
   the lights CREATELIGHT made, by index, in index order. */
typedef struct d3d_light {
    DWORD index;
    DWORD data[D3DLIGHT7_DWORDS]; /* the D3DLIGHT7 */
    int   enabled;
} d3d_light_t;

/* d3d_ctx_t.used: a slot is claimed whole (compare and exchange), so two
   processes never take the same one and one caller alone tears it down. */
#define CTX_FREE 0
#define CTX_LIVE 1
#define CTX_BUSY 2 /* being made or torn down */

typedef struct d3d_ctx {
    DWORD  used; /* CTX_*; first */
    DWORD  pid;
    LPSURF target;
    LPSURF zbuf;
    LPSURF follow_named; /* ctx_target_follow's: the target as it last saw it named ... */
    DWORD  follow_flip;  /* ... and sb->fence_flip then */

    /* The DirectX 7 interface: surfaces are named by handles, in the scope
       of a DirectDraw local object. target and zbuf are resolved from them
       when they are named (ContextCreate, SETRENDERTARGET), then kept as
       surface objects: a flip moves handles and memory together, and the
       runtime names the back buffer by the handle it then holds (DDK,
       "D3dCreateSurfaceEx Handles and Flip"), which the surface object
       follows either way. A target that turns out the member on display is
       drawn as the member the next flip shows (ctx_target_follow): the
       record and flip count it was named with. */
    int    dx7;
    void  *ddl;
    DWORD  iface;      /* the Direct3D interface, as ContextCreate's dwhContext says: 3 the DirectX 7 one, 2 and below older ones */
    DWORD  trace_cmds; /* commands the handshake trace still names (a new DirectX 8 context's first 64) */
    DWORD  trace_dip2; /* DRAWINDEXEDPRIMITIVE2 records the DIP2 trace still names (a new DirectX 8 context's first 8) */
    /* The texture trace (diag_textures): lines of each per-context kind
       still to write, and each stage's handle, surface and memory at its
       last flush (tt_bind). */
    DWORD  tt_pal;
    DWORD  tt_nopal;
    DWORD  tt_texblt;
    DWORD  tt_mx;
    char   exe[24]; /* the creating process's executable (trace_process): the context line and PROC notes */
    DWORD  tt_handle[D3D_STAGES];
    LPSURF tt_surf[D3D_STAGES];
    DWORD  tt_mem[D3D_STAGES];
    DWORD  draws;      /* DirectX 8 draws sent (with Diagnostics): one destroyed before it drew is shown */
    DWORD  rec_epoch;  /* the world epoch its target and Z buffer copies were last checked at (ctx_recheck) */
    int    legacy_tex; /* the runtime gave this context's textures TextureCreate handles (an older interface's) */
    DWORD  serial;     /* of its creation (ctx_token) */
    int    stopped;    /* the GPU kept refusing its packets: it draws no more (ctx_refused) */
    DWORD  refused_n;  /* its last refused packet, how many times in a row ... */
    DWORD  refused_op; /* ... its opcode, first dword and error ... */
    DWORD  refused_arg;
    DWORD  refused_code;
    DWORD  refused_tick; /* ... and when last */
    DWORD  h_target;
    DWORD  h_zbuf;
    DWORD  sb_rec; /* the state block being recorded (d3d7.c's record plus one), 0 none, SB_DISCARD one with no room */
    /* The flight recorder (trace.c): its last FLIGHT_CMDS DrawPrimitives2
       commands (opcode | count << 8 | outcome << 24, and the first data
       dword), and whether they were shown. */
    DWORD  fl_head;
    DWORD  fl_cmd[FLIGHT_CMDS][2];
    int    fl_dumped;

    /* The DirectX 8 interface (ContextCreate's dwhContext 4, with the
       GPU's PROFILE.FIXED8): vertex input by shader handle, streams and an
       index buffer, all set by commands and kept until changed. */
    int          dx8;
    DWORD        vs;                  /* SETVERTEXSHADER: an FVF code (bit 0 clear) or a shader's handle; 0 none */
    d3d_stream_t stream[D3D_STREAMS];
    int          um;                  /* stream 0 is the call's user memory (SETSTREAMSOURCEUM) ... */
    DWORD        um_stride;           /* ... at this stride */
    DWORD        ib;                  /* SETINDICES: the index buffer's handle, 0 none ... */
    DWORD        ib_stride;           /* ... and its index size, 2 or 4 */
    d3d_vs_t    *shaders;             /* the vertex shaders it made, in the DLL's heap */
    DWORD        n_shaders;
    DWORD        cap_shaders;
    float      (*pal)[16];            /* world matrices 4-255 (D3D_PALETTE of them; 0-3 are xform's), in the heap; NULL until one is set */
    DWORD        pal_set[D3D_PALETTE / 32];   /* those set ... */
    DWORD        pal_dirty[D3D_PALETTE / 32]; /* ... and changed since the last flush */
    DWORD        pal_hi;              /* past the highest set */
    d3d_ps_t    *pshaders;            /* the pixel shaders it made, in the heap ... */
    DWORD        n_pshaders;
    DWORD        cap_pshaders;
    DWORD        ps;                  /* ... SETPIXELSHADER's handle (0 none) ... */
    int          ps_on;               /* ... a shader of it bound (its program draws) ... */
    DWORD        ps_const[D3D_PS_CONSTS][4]; /* ... and SETPIXELSHADERCONST's values */
    /* Command set 2.2: the vertex shaders' constants as set (in the heap,
       D3D_VS_CONSTS; NULL until one is), past the highest set, and the
       registers of both kinds set since the GPU was last sent them (dp2.c's
       send_programs). */
    DWORD      (*vconst)[4];
    DWORD        vconst_hi;
    DWORD        vconst_dirty[D3D_VS_CONSTS / 32];
    DWORD        pconst_dirty;

    DWORD  rs[D3DRS_MAX];                    /* Direct3D render states as set */
    DWORD  tss[D3D_STAGES][D3D_TSS_COUNT];   /* texture stage states as set (0: the handle) */
    DWORD  vp[4];                            /* VIEWPORTINFO */
    float  wnear;
    float  wfar;
    DWORD  dirty;                            /* DIRTY_*: what the next flush must translate again ... */
    DWORD  rs_dirty[D3DRS_MAX / 32];         /* ... the render states set since, a bit each (DIRTY_RS) ... */
    DWORD  tss_dirty[D3D_STAGES];            /* ... and each stage's states, a bit each, ~0 all (DIRTY_STAGE) */
    /* What the last flush described the target and Z buffer from: while
       none of it changed (and the GPU holds the context's state), they are
       as sent (sent_target). */
    LPSURF flush_target;                     /* NULL: none described */
    LPSURF flush_zbuf;
    DWORD  flush_mem[2];
    DWORD  flush_world;
    int    clean;                            /* the GPU holds all of it, textures included: a flush would send nothing */

    /* What its last flush with textures was built from besides its state:
       a DrawPrimitives2 call keeps the context clean from the call before
       while none of it changed (ctx_unchanged). */
    DWORD  dep_world;                        /* world_epoch then */
    int    dep_always;                       /* a DirectX 6 stage drew an 8-bit texture: checked at every call */
    int    dep_valid;                        /* that flush completed: the stages' texture descriptions may be reused */
    DWORD  dep_tex_alpha[D3D_STAGES];        /* their textures' alpha (stage 0's decides the legacy MODULATE) */
    DWORD  tex_changed;                      /* stages whose texture handle was set since that flush (a bit each) */
    DWORD  dep_mem[2];                       /* the target's and the Z buffer's memory */
    DWORD  dep_n;                            /* stages looked at */
    LPSURF dep_tex[D3D_STAGES];              /* their textures (top levels), NULL none usable */
    DWORD  dep_tex_mem[D3D_STAGES];          /* their memory */
    DWORD  dep_tex_ckf[D3D_STAGES];          /* DDRAWISURF_HASCKEYSRCBLT */
    DWORD  dep_tex_ck[D3D_STAGES];           /* the source colour key */
    BYTE   dep_p8[D3D_STAGES];               /* a DirectX 7 or 8 stage's 8-bit texture: its palette checked by version */
    DWORD  dep_pal_epoch;                    /* d3d7_palette_epoch then: where it is, no stage's palette changed */

    /* What the GPU was last sent for this context; valid while the GPU holds
       this context's state (state_owner, state_epoch). */
    DWORD  sent_epoch;
    DWORD  sent_target[6];
    DWORD  sent_vp[4];
    float  sent_w[2];
    DWORD  sent_rs[VGPU98_RS_GLOBAL_COUNT8];
    DWORD  sent_tss[D3D_STAGES][VGPU98_TSS_COUNT8];
    DWORD  sent_tex[D3D_STAGES][TEX_DESC_MAX];
    DWORD  sent_tex_len[D3D_STAGES];
    DWORD  sent_pal[D3D_STAGES];             /* the palette sent: DirectX 6, its colours' signature; DirectX 7 and 8, its
                                                version (PAL_UNBOUND none); 0 nothing sent */
    int    sent_alpha0;                      /* stage 0's texture alpha the stage states were translated with */
    DWORD  sent_active;                      /* the stages that blended at the last flush */

    /* Transform and lighting (DirectX 7), as set: matrices by the GPU's
       numbers, the light table (in the DLL's heap), the material, the clip
       planes, the Z range ... */
    float        xform[VGPU98_XFORM_COUNT][16];
    d3d_light_t *lights;
    DWORD        n_lights;
    DWORD        cap_lights;
    DWORD        material[D3DMATERIAL7_DWORDS];
    float        plane[VGPU98_MAX_CLIPPLANES][4];
    float        zrange[2];
    DWORD        tl_dirty; /* TL_*: what changed since the last flush */
    /* ... and as last sent */
    float        sent_xform[VGPU98_XFORM_COUNT][16];
    DWORD        sent_light[VGPU98_MAX_LIGHTS][D3DLIGHT7_DWORDS];
    DWORD        sent_light_mask;
    DWORD        sent_material[D3DMATERIAL7_DWORDS];
    float        sent_plane[VGPU98_MAX_CLIPPLANES][4];
    float        sent_zrange[2];
} d3d_ctx_t;

typedef struct d3d_tex {
    DDRAWI_DDRAWSURFACE_INT *surf; /* 0: free */
    DWORD                    pid;
} d3d_tex_t;

extern d3d_ctx_t d3d_ctx[MAX_CONTEXTS];

/* Are the context's textures named by surface handles (DirectX 7), or by
   TextureCreate's (DirectX 6, and an older interface's context that the
   runtime gives them to)? */
static inline int
ctx_surface_handles(const d3d_ctx_t *c)
{
    return c->dx7 && !c->legacy_tex;
}
extern d3d_tex_t d3d_tex[MAX_TEXTURES];

d3d_ctx_t *ctx_get(DWORD handle);
DWORD      ctx_handle(const d3d_ctx_t *c);
DWORD      ctx_token(const d3d_ctx_t *c);
void       ctx_set_rs(d3d_ctx_t *c, DWORD state, DWORD value);
void       ctx_set_tss(d3d_ctx_t *c, DWORD stage, DWORD n, DWORD value);
/* Sends what of the context's state the GPU lacks, in the current batch.
   Returns 1; -1 (with nothing sent) if drawing is impossible (no usable
   target); 0 if the ring stopped. */
int        ctx_flush(d3d_ctx_t *c, int with_textures);
/* Is everything the context's last flush with textures was built from,
   besides its state, as it was: the world epoch, the target's, Z buffer's
   and textures' memory, the textures' colour keys, a DirectX 7 or 8 8-bit
   texture's palette version (looked up only when the palette epoch moved)? */
int        ctx_unchanged(d3d_ctx_t *c);
/* The textures of the context's last flush, into the batch's list (a call
   that kept the state from the call before). */
void       ctx_batch_textures(const d3d_ctx_t *c);
/* Records a fence as the last use of every level of a texture (and every
   face of a cube map). */
void       tex_used(LPSURF t, DWORD fence);
/* Starts a batch's record of the textures it binds (under the ring lock). */
void       ctx_batch_begin(void);
/* Records the batch's fence on the context's target and Z buffer and on
   every texture the batch's draws used (under the ring lock). */
void       ctx_used(d3d_ctx_t *c, DWORD fence);
/* A surface the batch's packets read or write besides those (TEXBLT). */
void       ctx_batch_surface(LPSURF s);
/* DirectX 7: the target and Z buffer from their handles (SETRENDERTARGET). */
void       ctx_resolve(d3d_ctx_t *c);
/* DirectX 8, at each DrawPrimitives2 after the world epoch moved: a target
   or Z buffer that is the DLL's copy is looked up again by its handle (its
   surface may have gone, and its copy with it); the flipping chain's are
   kept, as a flip moves their handles. */
void       ctx_recheck(d3d_ctx_t *c);
/* The context's target, when it is a flipping chain's member on display:
   the member the next flip shows. */
void       ctx_target_follow(d3d_ctx_t *c);
/* CLEAR inside a batch (the ring lock held); nothing without a usable
   target. 0 if the ring stopped. */
int        ctx_clear(d3d_ctx_t *c, DWORD flags, DWORD color, float depth, DWORD stencil, const D3DRECT *rects, DWORD n);
/* The transform and lighting state (DirectX 7). */
void       ctx_set_transform(d3d_ctx_t *c, DWORD type, const void *matrix);
void       ctx_get_transform(const d3d_ctx_t *c, DWORD type, void *matrix);
int        ctx_create_light(d3d_ctx_t *c, DWORD index);
/* A light of the table by its index; NULL if CREATELIGHT never made it. */
d3d_light_t *ctx_light(const d3d_ctx_t *c, DWORD index);
void       ctx_set_light(d3d_ctx_t *c, DWORD index, DWORD type, const void *data);
void       ctx_set_material(d3d_ctx_t *c, const void *material);
void       ctx_set_clipplane(d3d_ctx_t *c, DWORD index, const void *plane);
void       ctx_set_zrange(d3d_ctx_t *c, float minz, float maxz);
/* A texture's next mip level (a cube face's within the face). */
LPSURF     tex_mip_next(LPSURF s);

/* --- d3d7.c: the DirectX 7 interface's bookkeeping --- */

extern int                             d3d7_on;
extern DDHAL_DDMISCELLANEOUS2CALLBACKS d3d7_misc2;
void   d3d7_init(void);
/* The surface holding a handle of a DirectDraw local now; NULL if none.
   With the handle's generation, which changes whenever it names another
   surface (the texture cache's key), and the palette SETPALETTE bound to
   it (0 none), from one lookup. */
LPSURF d3d7_surface(void *ddl, DWORD handle);
LPSURF d3d7_surface_gen(void *ddl, DWORD handle, DWORD *gen, DWORD *palette);
/* With s the member of a primary flipping chain on display, the member the
   next flip shows; NULL if s is in no such chain or not on display. */
LPSURF d3d7_flip_next(LPSURF s);
/* A surface destroyed or lost (DestroySurface): its handle goes. Returns
   the last-use fence of its record (the DLL's copy's when it had one). */
DWORD  d3d7_surface_gone(LPSURF s);
/* Forgets a local object's handles and palettes, or (ddl NULL) a process's. */
void   d3d7_forget(const void *ddl, DWORD pid);
DWORD  d3d7_live_handles(void);
/* SETPALETTE; 1 if it changed anything (the binding, the flags, a palette
   made), so the context needs its state checked again. */
int    d3d7_set_palette(void *ddl, DWORD surface, DWORD handle, DWORD flags);
/* UPDATEPALETTE; 1 if it changed the palette (the same entries again: 0). */
int    d3d7_update_palette(void *ddl, DWORD handle, DWORD first, DWORD count, const uint8_t *entries);
/* A texture's palette as D3DCOLOR dwords; 0 if it has none. */
int    d3d7_palette(void *ddl, DWORD surface, DWORD *colors, int *alpha);
/* The handle of the palette a texture uses (0 none), and its alpha flag. */
DWORD  d3d7_palette_of(void *ddl, DWORD surface, int *alpha);
/* A palette's alpha flag (DDRAWIPAL_ALPHA). */
int    d3d7_palette_alpha(void *ddl, DWORD palette);
/* The palette SETPALETTE bound to a surface (0 none) and its version (0:
   none recorded), which changes with its colours and flags.
   One version names one palette's
   contents: no two palettes share one. */
void   d3d7_palette_state(void *ddl, DWORD surface, DWORD *handle, DWORD *version);
/* A version no palette has: a stage's 8-bit texture with no palette. */
#define PAL_UNBOUND 0xFFFFFFFFu
/* A palette's alpha flag and version (0, 0: none recorded) from one
   lookup: a texture description's flag and the version it goes with. */
void   d3d7_palette_flags(void *ddl, DWORD palette, int *alpha, DWORD *version);
/* Moves with every change of what a stage's palette is (a binding, flags,
   a palette made, colours, palettes forgotten). */
DWORD  d3d7_palette_epoch(void);
/* The palettes recorded now, and how many may be (the report's). */
DWORD  d3d7_palettes_live(DWORD *cap);
/* A context made: the palettes' "no room" lines may be written again (their
   first three). */
void   d3d7_lines_again(void);
/* A fence as the last use of every surface a local object has handles for. */
void   d3d7_fence_all(void *ddl, DWORD fence);
int    d3d7_texblt(d3d_ctx_t *c, DWORD dst, DWORD src, LONG dx, LONG dy, const RECTL *rect, DWORD flags);
/* DirectX 8's VOLUMEBLT: `box` is a D3DBOX (left, top, right, bottom,
   front, back). */
int    d3d7_volblt(d3d_ctx_t *c, DWORD dst, DWORD src, LONG dx, LONG dy, LONG dz, const DWORD *box);
/* State blocks: begin recording, add a recorded command, a block's commands
   (EXECUTE, CAPTURE), delete, a context's go with it. A block BEGIN found
   no room for still takes its commands (sb_rec SB_DISCARD), into nothing:
   they are never applied. */
#define SB_DISCARD 0xffffffffu
int      d3d7_sb_begin(d3d_ctx_t *c, DWORD handle);
void     d3d7_sb_record(d3d_ctx_t *c, const uint8_t *cmd, DWORD bytes);
uint8_t *d3d7_sb_data(d3d_ctx_t *c, DWORD handle, DWORD *len);
void     d3d7_sb_delete(d3d_ctx_t *c, DWORD handle);
void     d3d7_sb_forget(DWORD ctx);
DWORD    d3d7_sb_live(void);
/* DirectX 8's vertex and index buffers: the surface holding a buffer's
   handle (CreateSurfaceEx records them apart from the world epoch, which
   they do not affect); buf_epoch moves when one is recorded or forgotten. */
LPSURF d3d7_buffer(void *ddl, DWORD handle);
/* The global record of the DLL's copy holding a buffer's handle (an adopted
   pool buffer's own record), NULL when the map holds none or the runtime's
   record; no trace (pool.c finds buffers by it). */
DDRAWI_DDRAWSURFACE_GBL *d3d7_buffer_gbl(const void *ddl, DWORD handle);
/* An adopted buffer destroyed: its handle's entry goes, if it is still the
   DLL's copy reading the buffer's own record `gbl` (DestroyExecuteBuffer,
   which may come from another process, or without the handle). */
void d3d7_buffer_gone(const void *ddl, DWORD handle, const DDRAWI_DDRAWSURFACE_GBL *gbl);
extern DWORD buf_epoch;
/* A DirectX 8 context was made on the DirectDraw object: the surfaces made
   on it from now on get the DLL's copies (ContextCreate). */
void   d3d7_mark_dx8(void *ddl);
/* A DirectX 8 context's target or Z buffer the call names, made before its
   object was marked (a windowed back buffer, the device's Z buffer): copied
   now from the call's own records, unless the flipping chain's or copied
   already. */
void   d3d7_copy_target(void *ddl, LPSURF s);
/* The report's figures of the records: handles live, those that are the
   DLL's copies (DirectX 8), their trees and the heap KB they take. */
void   d3d7_record_figures(DWORD *handles, DWORD *copies, DWORD *trees, DWORD *kb);

/* --- d3d8.c: the DirectX 8 interface --- */

/* PROFILE.FIXED8 with 3D: the DLL answers GetDriverInfo2, takes DirectX 8
   contexts and sends command set 2.1's values. */
extern int d3d8_on;
/* Command set 2.2's formats (V16U16, G16R16, A2W10V10U10): with PROFILE.PROGRAMS
   (a GPU without it is exactly command set 2.1). */
extern int d3d8_fmt22;
void   d3d8_init(DWORD profile);
/* GetDriverInfo with the stereo-mode GUID: 1 if it was GetDriverInfo2 (then
   answered), 0 if it was a stereo-mode query. */
int    d3d8_get_driver_info2(DDHAL_GETDRIVERINFODATA *d);
/* The application runtime's version DXVERSION last gave the calling
   process (0: none yet). */
DWORD  d3d8_runtime(void);

/* The DirectX 8 handshake trace (trace.c): on with
   the stream audit. TRACE(kind) is true when a line of that kind may be
   written (each kind bounded per process; the rest counted). */
#define TRACE_QUERY   0 /* GetDriverInfo, GetDriverInfo2 */
#define TRACE_SURFACE 1 /* CreateSurface, CreateExecuteBuffer, CanCreateSurface's refusals */
#define TRACE_HANDLE  2 /* handles recorded (CreateSurfaceEx) */
#define TRACE_GONE    3 /* surfaces destroyed, DirectDraw objects gone */
#define TRACE_CONTEXT 4 /* ContextCreate, ContextDestroy */
#define TRACE_LOOKUP  5 /* handle lookups that found nothing, or another record */
#define TRACE_LOCK    6 /* Locks of buffers */
#define TRACE_POINTS  7 /* point states a DirectX 8 context sets, its point-list draws once for each kind (what they draw
                           with) not among the last 8 traced */
#define TRACE_ANSWER  8 /* callbacks that answered an error */
#define TRACE_WINDOW  9 /* ring windows clamped to their buffer, DRAWINDEXEDPRIMITIVE(2)'s raw fields */
#define TRACE_BLEND   10 /* draws whose FVF code's last weight is taken as matrix indices */
#define TRACE_DIP2    11 /* a DirectX 8 context's first DRAWINDEXEDPRIMITIVE2 records, read and drawn */
/* The texture trace, on with the texture audit
   (diag_textures): TTRACE. Its per-context kinds (palettes, 8-bit textures
   with no palette, TEXBLTs) count down in the context's record instead. */
#define TRACE_TEXBIND 12 /* a stage's handle naming another surface, or another address, than at its last flush */
#define TRACE_TEXLOCK 13 /* Locks of textures */
#define TRACE_KINDS   14
#define TRACE(kind)   (diag_audit8 && trace_take(kind))
#define TTRACE(kind)  (diag_textures && trace_take(kind))
/* The texture trace's budgets a context, set at ContextCreate. */
#define TT_PALETTES   64 /* SETPALETTE and UPDATEPALETTE */
#define TT_NOPAL      3  /* 8-bit textures drawn with no palette bound */
#define TT_TEXBLTS    16
#define TT_MATRICES   32 /* texture matrices set */

int         trace_take(int kind);
/* A DirectX 8 context begins: its process gets the full budgets again. */
void        trace_reset(void);
const char *trace_kind(LPSURF s);
const char *trace_where(LPSURF s);
/* A surface's offset in video memory, as the host's lines name it (the
   host's texture audit); FFFFFFFF for one in system memory. */
DWORD       trace_vmem(LPSURF s);
/* The calling process's executable's file name, at most size - 1
   characters ("?" if Windows does not say). */
const char *trace_process(char *buf, DWORD size);
/* With diag_textures: the PROC note goes again at the next draw (a new
   context). */
void        dp2_proc_again(void);
/* The texture trace's lines. */
void        tt_bind(d3d_ctx_t *c, DWORD stage, LPSURF top);
void        tt_nopal(d3d_ctx_t *c, DWORD stage, LPSURF top);
void        tt_palette(d3d_ctx_t *c, int update, DWORD palette, DWORD a, DWORD b, DWORD first, int changed);
void        tt_texblt(d3d_ctx_t *c, DWORD hdst, DWORD hsrc, LONG x, LONG y, const RECTL *r, DWORD flags);
void        tt_lock(LPSURF s, const DDHAL_LOCKDATA *d, int direct);
/* A texture matrix set (SETTRANSFORM, MULTIPLYTRANSFORM): its first
   TT_MATRICES a context, with the values u and v take. */
void        tt_matrix(d3d_ctx_t *c, DWORD stage, const float *m);
void        trace_command(const d3d_ctx_t *c, const uint8_t *at, const uint8_t *end);
void        trace_report(void);
/* The flight recorder's lines (with Diagnostics): why, then process pid's
   last callbacks (once a process, unless `again`) and, with a context, its
   last commands (once a context). */
void        flight_dump(const char *why, DWORD pid, d3d_ctx_t *c, int again);
/* After a fault the guard caught in this process (the first two). */
void        flight_fault(d3d_ctx_t *c);
/* A process leaving without live contexts (`ending`: not a FreeLibrary;
   its slot freed, or shown after an error answer); processes that ended
   without one shown (ctx_reap, the report). */
void        flight_leaving(DWORD pid, int ending);
/* A process attaching (DLL_PROCESS_ATTACH): its ID's old slot starts
   again; a line. */
void        flight_attach(DWORD pid);
/* The stream audit's lines (each kind's first three) and the cut-window
   trace's repeats begin again (a DirectX 8 context's start or end). */
void        audit8_reset(void);
void        flight_reap(void);
/* A DrawPrimitives2 command of a context begins (its entry, NULL without
   Diagnostics), and ends: FLIGHT_DONE, FLIGHT_FAULTED, FLIGHT_UNPARSED. */
#define FLIGHT_DONE     0
#define FLIGHT_RUNNING  1
#define FLIGHT_FAULTED  2
#define FLIGHT_UNPARSED 3
DWORD      *flight_command(d3d_ctx_t *c, const uint8_t *at, const uint8_t *end);
static inline void
flight_command_end(DWORD *e, DWORD outcome)
{
    if (e)
        e[0] = (e[0] & 0x00FFFFFFu) | (outcome << 24);
}
/* Vertex shaders of a context: made from CREATEVERTEXSHADER's declaration
   and function (with PROFILE.PROGRAMS translated into the GPU's program,
   else unusable), found, deleted, all freed. 1 made; 0 when the DLL's heap
   is full; -1 refused (a function the translator cannot take, or no
   program id or heap for it: the call fails). */
int    d3d8_vs_create(d3d_ctx_t *c, DWORD handle, const uint8_t *decl, DWORD decl_bytes, const uint8_t *code,
                      DWORD code_bytes);
d3d_vs_t *d3d8_vs_find(const d3d_ctx_t *c, DWORD handle);
void   d3d8_vs_delete(d3d_ctx_t *c, DWORD handle);
void   d3d8_vs_forget(d3d_ctx_t *c);
/* SETVERTEXSHADER: the vertex input, and a shader's own constants
   (CONSTMEM, def) loaded over the context's. */
void   d3d8_vs_set(d3d_ctx_t *c, DWORD handle);
/* SETVERTEXSHADERCONST: `count` registers from `reg` (four floats each). */
void   d3d8_vs_const(d3d_ctx_t *c, DWORD reg, DWORD count, const uint8_t *values);
/* Pixel shaders (pshader.c, with PROFILE.PROGRAMS or shader capture):
   made (with PROFILE.PROGRAMS translated; 0 when refused: one the
   translator cannot take, too long, or no id or heap room), deleted, bound
   (its def constants loaded), their constants, a draw with one bound
   (shader capture: counted, its first named), all freed. */
int    d3d8_ps_create(d3d_ctx_t *c, DWORD handle, const uint8_t *code, DWORD bytes);
void   d3d8_ps_delete(d3d_ctx_t *c, DWORD handle);
void   d3d8_ps_bind(d3d_ctx_t *c, DWORD handle);
void   d3d8_ps_const(d3d_ctx_t *c, DWORD reg, DWORD count, const uint8_t *values);
void   d3d8_ps_drawn(d3d_ctx_t *c);
void   d3d8_ps_forget(d3d_ctx_t *c);
d3d_ps_t *d3d8_ps_find(const d3d_ctx_t *c, DWORD handle);
/* The stages the bound pixel shader's program samples, a bit each (0: no
   program bound, the fixed function's stages). */
DWORD  d3d8_ps_stages(const d3d_ctx_t *c);
/* PROGRAM's range for pixel programs: the caps' MaxPixelShaderValue. */
DWORD  d3d8_ps_range(void);
/* The shaders' lines (pshader.c): a shader's
   summary claimed once a session, both kinds (1: log it now); whether it
   was; its instructions, a line each (shader capture); its GPU program, a
   line each (the capture of programs); a shader refused and why (`at` its
   token, of the tokens as kept), 16 a session. */
int    d3d8_shader_once(DWORD hash);
int    d3d8_shader_was_logged(DWORD hash);
/* A named shader's draws may be logged as its handle goes (64 a session). */
int    d3d8_shader_gone_line(DWORD hash);
void   d3d8_shader_text(DWORD hash, const DWORD *tok, DWORD n, int vertex);
void   d3d8_prog_log(DWORD hash, const d3d_prog_t *g);
void   d3d8_shader_refused(const d3d_ctx_t *c, DWORD handle, DWORD version, const char *why, DWORD at);

/* --- prog.c: the GPU's programs (command set 2.2) --- */

/* PROFILE.PROGRAMS (with the DirectX 8 interface): shaders are translated
   into the GPU's programs. */
extern int d3d8_programs;
/* The largest program made of each kind (0 vertex, 1 pixel), a session. */
extern DWORD prog_largest[2];
/* The translator's output (an xl_out_t) in the heap; NULL without room. */
void  *prog_scratch(void);
/* A program from the translator's output: an id and the heap for its
   instructions (0 without either). */
int    prog_make(d3d_prog_t *p, const void *translated, DWORD range);
/* Its id and instructions given back. */
void   prog_free(d3d_prog_t *p);
/* A stage's texture description as tex_build makes it from the stage's
   handle now (shader capture's first draw: the flush builds only the
   stages that blend). Its length; format 0 without a usable texture. */
DWORD  ctx_tex_describe(d3d_ctx_t *c, DWORD stage, DWORD *out);
/* The texture descriptions cached now, of TEX_CACHE_SIZE (the report's). */
DWORD  tex_cache_held(void);
#define TEX_CACHE_SIZE 4096
/* The textures a batch lists for its fence, past which every texture of
   its context is fenced (the report counts those batches). */
#define BATCH_TEX_SIZE 256

/* --- heap.c: the DLL's heap, shared by every process --- */

void *heap_alloc(DWORD bytes);
void  heap_free(void *p);
/* A larger block holding the first `old` bytes of `p`, which is freed; NULL
   (and `p` kept) if there is no room. */
void *heap_grow(void *p, DWORD old, DWORD bytes);
/* `p` points into the heap (one of the DLL's surface records). */
int   heap_owns(const void *p);
/* The blocks in use and their kilobytes, the free blocks and the largest's
   kilobytes (the report's). */
void  heap_figures(DWORD *used_blocks, DWORD *used_kb, DWORD *free_blocks, DWORD *largest_kb);
/* A context made: the heap's "no bytes left" lines may be written again (their
   first three). */
void  heap_lines_again(void);

#endif /* VGPU98_D3D_H */
