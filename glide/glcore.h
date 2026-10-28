/*
 * The Glide core both DLLs share: the session on
 * the card's Glide device, the batch the DLL records into and copies into
 * the ring, Glide's state as the device's registers, the screen through
 * DirectDraw, the frame buffer through the staging surface, drawing,
 * textures and the utility library's files. glide2x and glide3x give
 * Glide 2's and Glide 3's calls on top of it.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLIDE_GLCORE_H
#define VGPU98_GLIDE_GLCORE_H

#include "gpuenv.h"
#include "vid_vgpu98_glcmd.h"

/* Glide's numbers the core reads (the SDK headers', read for facts). */
#define GR_COLORFORMAT_ARGB 0
#define GR_COLORFORMAT_ABGR 1
#define GR_COLORFORMAT_RGBA 2
#define GR_COLORFORMAT_BGRA 3

#define GR_ORIGIN_UPPER_LEFT 0
#define GR_ORIGIN_LOWER_LEFT 1
#define GR_ORIGIN_ANY        0xff

#define GR_BUFFER_FRONTBUFFER  0
#define GR_BUFFER_BACKBUFFER   1
#define GR_BUFFER_AUXBUFFER    2
#define GR_BUFFER_DEPTHBUFFER  3
#define GR_BUFFER_ALPHABUFFER  4
#define GR_BUFFER_TRIPLEBUFFER 5

#define GR_LFB_READ_ONLY  0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFB_NOIDLE     0x10

#define GR_LFBWRITEMODE_ANY 0xff
#define GR_LFB_SRC_FMT_RLE16 0x80

/* GrLfbInfo_t, as programs pass it. */
typedef struct glc_lfb_info {
    int    size;
    void  *lfbPtr;
    FxU32  strideInBytes;
    FxI32  writeMode;
    FxI32  origin;
} glc_lfb_info_t;

_Static_assert((sizeof(void *) != 4) || (sizeof(glc_lfb_info_t) == 20), "GrLfbInfo_t is not 20 bytes");

/* --- The session --------------------------------------------------------- */

/* grSstWinOpen: the screen and the session. 1 when open. Refused (0, with a
   line) without the card's Glide device, for arguments a Voodoo3 refuses,
   or with the GPU held. */
int   glc_open(FxU32 hwnd, FxU32 res, FxU32 refresh, FxU32 color_format, FxU32 origin, int color_buffers,
               int aux_buffers);
/* grSstWinClose, grGlideShutdown: the session ends, the desktop comes back.
   Nothing without one. */
void  glc_close(void);
/* The DLL's unload: a FreeLibrary closes whole; the process's end closes
   the session on the device alone, waiting for nothing. */
void  glc_detach(int process_ending);
int   glc_is_open(void);
FxU32 glc_width(void);
FxU32 glc_height(void);
int   glc_color_buffers(void);
int   glc_aux_buffers(void);
int   glc_has_buffer(FxI32 buffer);

/* --- Batches ---------------------------------------------------------------- */

/* Room for a Glide command of `len` payload dwords in the batch (sent first
   if full); the session's dword is written: the rest of the payload follows
   it. NULL without a session. */
DWORD *glc_cmd(DWORD op, DWORD len);
/* The batch to the ring, ended by a FENCE: its value (the last one when
   there was nothing to send, 0 when the batch was dropped). */
DWORD glc_flush(void);
/* Until the device has done everything sent up to fence v: 0 if the GPU is
   held (the wait ended without it). */
int   glc_wait(DWORD v);
/* Every command sent and done (grSstIdle, grFinish). */
void  glc_finish(void);
/* Is the device still at work on what was sent (the batch sent first)? 0
   without the card. */
int   glc_busy(void);
/* Drawing into the front buffer: the batch goes now and then, so it shows
   without a swap. */
void  glc_front_written(void);
int   glc_drawing_front(void);

/* --- Glide's state, the device's registers ---------------------------------- */

void  glc_reg(DWORD reg, DWORD value);
DWORD glc_reg_get(DWORD reg);
/* A TMU whose own combine uses its texel (state.c). */
int   glc_tmu_active(int tmu);
/* The registers changed since they were last sent, as one STATE: before
   every command that reads them. */
void  glc_sync(void);
/* A GrColor_t in the session's colour format as 0xAARRGGBB. */
DWORD glc_argb(FxU32 color);

/* --- The screen ---------------------------------------------------------- */

void  glc_clear(FxU32 color, FxU32 alpha, FxU32 depth);
void  glc_swap(FxU32 interval);
int   glc_pending(void);
void  glc_set_pending_max(int n);
/* A gamma table, 256 entries a channel (0-255 each). */
void  glc_gamma_table(const uint8_t *r, const uint8_t *g, const uint8_t *b);
/* grGammaCorrectionValue's curve for each channel. */
void  glc_gamma(float r, float g, float b);
/* Vertical retrace: the GPU's count, and whether it is in one now. */
DWORD glc_vretrace(void);

/* --- The frame buffer (lfb.c) ------------------------------------------- */

FxBool glc_lfb_lock(FxU32 type, FxI32 buffer, FxI32 write_mode, FxI32 origin, FxBool pipeline, glc_lfb_info_t *info);
FxBool glc_lfb_unlock(FxU32 type, FxI32 buffer);
FxBool glc_lfb_read(FxI32 buffer, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 stride, void *dst);
FxBool glc_lfb_write(FxI32 buffer, FxU32 x, FxU32 y, FxI32 format, FxU32 w, FxU32 h, FxBool pipeline, FxI32 stride,
                     const void *src);
void   glc_lfb_color_format(FxI32 format);
void   glc_lfb_swizzle(FxBool bytes, FxBool words);
/* grLfbWriteColorFormat's and grLfbWriteColorSwizzle's flags, for the
   saved state. */
DWORD  glc_lfb_flags(void);
void   glc_lfb_flags_set(DWORD flags);
/* The staging surface (screen.c): its rows for writes and for reads. */
uint8_t *glc_staging(int for_read, DWORD *offset, DWORD *stride, DWORD *bytes);
/* The last frame buffer transfer's fence: the staging surface is the
   program's again once it is done (glc_lfb_settle: 0 if the GPU is held). */
void   glc_lfb_fence(DWORD fence);
void   glc_lfb_reset(void);
int    glc_lfb_settle(void);

/* --- Drawing (draw.c) ----------------------------------------------------- */

/* A vertex in the device's form (vid_vgpu98_glcmd.h's VGPU98_GLV_*). */
typedef struct glc_vtx {
    float f[VGPU98_GL_VERTEX_DWORDS];
} glc_vtx_t;

void   glc_triangle(const glc_vtx_t *a, const glc_vtx_t *b, const glc_vtx_t *c);
void   glc_point(const glc_vtx_t *p);
void   glc_line(const glc_vtx_t *a, const glc_vtx_t *b);
/* Room for n vertices of triangles in the batch (screen.c): the open DRAW
   goes on while nothing came between, else a new one begins (the state
   sent first). NULL without a session. */
float *glc_vertices(DWORD n);
/* Glide 2's delta0 mode (guColorCombineFunction's ITRGB_DELTA0 presets):
   the iterated colour is the flat one given, not the vertices'. */
void   glc_delta0(int on, const float *rgba);
int    glc_delta0_on(void);
void   glc_delta0_get(float *rgb);

/* --- Counters and what is deferred (screen.c; the diagnostics) ------------ */

enum {
    GLC_TRIANGLES_IN,   /* triangles asked for */
    GLC_TRIANGLES,      /* triangles sent */
    GLC_POINTS,
    GLC_LINES,
    GLC_DROPPED,        /* primitives with a vertex not a number, or at or past +-2^27 */
    GLC_DRAWS,          /* DRAW commands */
    GLC_DOWNLOADS,
    GLC_DOWNLOAD_BYTES,
    GLC_PALETTES,
    GLC_LOCKS,
    GLC_SWAPS,
    GLC_CULLED,         /* triangles of zero area, or culled by the cull mode */
    GLC_WRAPPED,        /* vertices whose x or y wrapped as a Voodoo's setup unit wraps them */
    GLC_APART,          /* triangles whose corners wrapped by different multiples of 4096 (not drawn, as on a Voodoo) */
    GLC_T1W_OWN,        /* vertices for an active TMU1: its 1/w its own */
    GLC_T1W_TMU0,       /*   TMU0's */
    GLC_T1W_VERTEX,     /*   the vertex's */
    GLC_T1ST_OWN,       /*   its s and t its own */
    GLC_T1ST_TMU0,      /*   TMU0's */
    GLC_COUNTERS
};
void   glc_count(int which);
void   glc_count_bytes(int which, DWORD n);
DWORD  glc_counter(int which);
/* The window's own count (since its open). */
DWORD  glc_window_counter(int which);
/* draw.c's notes and finding said again for a new window. */
void   glc_draw_open(void);
void   glc_counter_reset(int which);

/* Features a Voodoo3 has that the device does not do yet: each said once a
   process when a program first uses it. */
#define GLC_DEFER_AA              0x01 /* edge antialiasing: drawn plain */
#define GLC_DEFER_COMPARE_TO_BIAS 0x02 /* depth compared to the bias: compared as usual */
#define GLC_DEFER_YIQ             0x04 /* YIQ (NCC) textures: their Y as grey */
#define GLC_DEFER_MULTIBASE       0x08 /* a texture's levels in more than one place: the first */
#define GLC_DEFER_LFB_PIPELINE    0x10 /* frame buffer writes through the pixel pipeline: plain writes */
#define GLC_DEFER_ALPHA_BUFFER    0x20 /* the aux buffer as an alpha buffer */
#define GLC_DEFER_MULTIPASS       0x40 /* the utility library's multipass helpers: one pass, the first texture */
void   glc_deferred(DWORD what);

/* --- The pixel pipeline's state (state.c) -------------------------------- */

/* grGlideGetState and grGlideSetState: the registers and what the core
   keeps besides, GLC_STATE_DWORDS of them (308 bytes: a Glide 2 GrState
   has room for them and the DLL's own dword). Set again, every register
   goes to the device before the next command. */
#define GLC_STATE_DWORDS (VGPU98_GS_COUNT + 5)
void   glc_state_save(DWORD *out);
void   glc_state_load(const DWORD *in);
/* The registers whole (screen.c): read, and set with every one sent. */
const DWORD *glc_regs(void);
void   glc_regs_load(const DWORD *r);
/* The bytes of the frame buffer's memory a Voodoo3 gives the window's
   buffers (grGet's GR_MEMORY_FB). */
DWORD  glc_fb_bytes(void);

/* The registers of a new session, as a Voodoo3's Glide leaves them. */
void   glc_state_defaults(DWORD *r, int color_buffers);
void   glc_color_combine(FxI32 fn, FxI32 factor, FxI32 local, FxI32 other, FxBool invert);
void   glc_alpha_combine(FxI32 fn, FxI32 factor, FxI32 local, FxI32 other, FxBool invert);
void   glc_constant(FxU32 color);
void   glc_itrgb(FxBool on);
void   glc_blend(FxI32 rgb_sf, FxI32 rgb_df, FxI32 alpha_sf, FxI32 alpha_df);
void   glc_alpha_test(FxI32 fn);
void   glc_alpha_ref(FxU32 ref);
void   glc_depth_mode(FxI32 mode);
void   glc_depth_func(FxI32 fn);
void   glc_depth_bias(FxI32 level);
void   glc_chroma_mode(FxI32 mode);
void   glc_chroma_value(FxU32 color);
void   glc_cull(FxI32 mode);
void   glc_dither(FxI32 mode);
/* The device's fog modes: 0 off, 1 iterated alpha, 2 the table at 1/w, 3
   iterated Z, 4 the table at the fog coordinate; | MULT2 100h, ADD2 200h. */
void   glc_fog_mode(DWORD mode);
void   glc_fog_color(FxU32 color);
void   glc_fog_table(const FxU8 *table);
void   glc_disable_all_effects(void);

/* --- Textures (tex.c) ------------------------------------------------------ */

/* A texture, Glide 3's numbers (log2): format, smallest and largest level,
   aspect (log2 of width over height) and the levels held (1 even, 2 odd, 3
   both). */
typedef struct glc_tex {
    int format;
    int small, large;
    int aspect;
    int evenodd;
} glc_tex_t;

/* Each TMU's memory now (2 MB before a window opens). */
DWORD  glc_tmu_bytes(void);
DWORD  glc_tex_mem_required(const glc_tex_t *t);
void   glc_tex_download(FxI32 tmu, DWORD start, const glc_tex_t *t, int lod, DWORD first, DWORD last, const void *data);
void   glc_tex_download_all(FxI32 tmu, DWORD start, const glc_tex_t *t, const void *data);
/* grTexSource: 0 when the TMU or the description is not one. */
int    glc_tex_source(FxI32 tmu, DWORD start, const glc_tex_t *t);
void   glc_palette(const DWORD *data, int first, int last, int is_6666);
int    glc_palette_6666(void);
void   glc_palette_6666_set(int on);
void   glc_tex_combine(FxI32 tmu, FxI32 rgb_fn, FxI32 rgb_factor, FxI32 alpha_fn, FxI32 alpha_factor, FxBool rgb_invert,
                       FxBool alpha_invert);
void   glc_tex_filter(FxI32 tmu, FxI32 min, FxI32 mag);
void   glc_tex_clamp(FxI32 tmu, FxI32 s, FxI32 t);
void   glc_tex_mipmap(FxI32 tmu, FxI32 mode, FxBool trilinear);
void   glc_mipmap_dither(int on);
int    glc_mipmap_dither_allowed(void);
void   glc_tex_lod_bias(FxI32 tmu, float bias);
/* The bias's quarter steps as a Voodoo3's Glide rounds a float to them,
   and a bias set in steps. */
int    glc_lod_steps(float bias);
void   glc_tex_lod_bias_steps(FxI32 tmu, int steps);
void   glc_tex_detail(FxI32 tmu, int lod_bias, FxU8 scale, float detail_max);

/* --- The utility library's .3df files (g3df.c) ------------------------------ */

/* Gu3dfInfo, 1056 bytes: the header in the program's numbers (Glide 2's
   levels and aspects, or Glide 3's log2), the NCC table or palette, the
   caller's buffer for the texels, and the bytes they take. */
typedef struct glc_3df_info {
    FxU32 width, height;
    int   small_lod, large_lod;
    int   aspect_ratio;
    int   format;
    union {
        struct {
            FxU8  yRGB[16];
            FxI16 iRGB[4][3];
            FxI16 qRGB[4][3];
            FxU32 packed_data[12];
        } ncc;
        FxU32 palette[256];
    } table;
    void *data;
    FxU32 mem_required;
} glc_3df_info_t;

_Static_assert((sizeof(void *) != 4) || (sizeof(glc_3df_info_t) == 1056), "Gu3dfInfo is not 1056 bytes");

FxBool glc_3df_info(const char *name, glc_3df_info_t *info, int glide3);
FxBool glc_3df_load(const char *name, glc_3df_info_t *info, int glide3);

/* The files' calls (KERNEL32). */
DLLIMPORT DWORD STDCALL CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD how, DWORD flags,
                                    DWORD template_file);
DLLIMPORT BOOL STDCALL  ReadFile(DWORD file, void *buffer, DWORD bytes, DWORD *got, void *overlapped);
DLLIMPORT DWORD STDCALL SetFilePointer(DWORD file, LONG distance, LONG *high, DWORD method);
DLLIMPORT BOOL STDCALL  CloseHandle(DWORD handle);

#endif /* VGPU98_GLIDE_GLCORE_H */
