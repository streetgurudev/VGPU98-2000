/*
 * vgpu98dx.dll: the DirectX 7 driver interface's bookkeeping
 * (spec/directx-hal.md): surface handles, texture palettes, state blocks,
 * texture loads (TEXBLT), and the Miscellaneous2 callbacks (CreateSurfaceEx,
 * GetDriverState, DestroyDDLocal).
 *
 * A DirectX 7 runtime names surfaces by small handles it assigns per
 * DirectDraw local object and announces through CreateSurfaceEx, for the
 * root of whatever it created (a texture's mip chain, a cube map, a flipping
 * chain). The DLL maps (local, handle) to the surface object holding the
 * handle. A flip moves handles, memory and the driver's reserved fields
 * together around its chain, so a handle keeps naming the same memory; the
 * map follows a handle around the chain when the surface it recorded no
 * longer holds it.
 *
 * A DirectX 8 runtime's records of the resources it makes for a device
 * (textures, buffers, targets) live for one call (it reuses them at once),
 * so on a DirectDraw object a DirectX 8 context was
 * made on, the DLL records copies of its own instead: at CreateSurfaceEx,
 * the root and everything attached, in DirectDraw's own structure shapes,
 * in the DLL's heap. The code that reads surfaces reads the copies as it
 * read the runtime's. What the device's creation made before (the
 * flipping chain, its Z buffer), the flipping chain always, and the
 * runtime's own vertex and command buffers keep the runtime's records,
 * which last. An application's vertex or index buffer of the DLL's pool is
 * copied too, its global record the pool's own (pool_adopt): the pool moves
 * a buffer's memory, and must never write a record the runtime reused.
 *
 * The tables are shared by every process; each has its lock (dll.h's
 * dlock_t), held only for table work.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

int d3d7_on; /* a DirectX 7 runtime asked for the Miscellaneous2 callbacks */

/* --- Surface handles ----------------------------------------------------------- */

/* An open-addressed table: a slot is free when its local object is NULL, and
   a deleted entry pulls later ones of its run back, so lookups stop at the
   first free slot. */
#define SH_SLOTS 131072 /* a power of two; DirectX 8's buffers, levels and faces have handles too */

/* One surface's copy (DirectX 8), in DirectDraw's shapes: what the DLL reads
   of a surface, its own pointers between them; the rest cleared. */
typedef struct rec_surf {
    DDRAWI_DDRAWSURFACE_LCL  lcl;
    DDRAWI_DDRAWSURFACE_GBL  gbl;
    DDRAWI_DDRAWSURFACE_MORE more;
} rec_surf_t;

/* A tree: one CreateSurfaceEx's root and everything attached to it, copied
   into one heap block (this header, `n` surfaces, then their attachment
   links). It goes when the last handle recorded into it goes. */
typedef struct rec_tree {
    DWORD      refs;  /* handles recorded into it (and its builder's, while it builds) */
    DWORD      n;
    DWORD      bytes; /* the block's */
    DWORD      pad;
    rec_surf_t s[];
} rec_tree_t;

typedef struct sh_slot {
    void       *ddl; /* the DirectDraw local object; NULL: free */
    DWORD       handle;
    LPSURF      lcl;     /* the surface holding the handle: the DLL's copy, or the runtime's own when last seen */
    DWORD       pid;     /* the process that recorded it */
    DWORD       palette; /* its palette's handle (SETPALETTE), 0 none */
    DWORD       gen;     /* new whenever the handle names another surface */
    int         buffer;  /* a vertex or index buffer (DirectX 8): no texture, target or Z buffer */
    rec_tree_t *tree;    /* the copies `lcl` is one of; NULL: the runtime's record */
} sh_slot_t;

static sh_slot_t sh[SH_SLOTS];
static DWORD     sh_live;
static DWORD     sh_gen;
static dlock_t   sh_lock;

/* The DirectDraw objects a DirectX 8 context was made on (ContextCreate),
   with their processes; forgotten with their handles (d3d7_forget). The
   DirectX 8.1 runtime gives no DXVERSION, so this is how the DLL knows. An
   entry counts only in its own process: one that outlived its object
   (never destroyed) cannot mark another process's that reuses its address. */
#define DX8_DDLS 64

static struct {
    void *ddl;
    DWORD pid;
} dx8_ddls[DX8_DDLS];
static DWORD dx8_marked; /* entries in use: none, no scan */

/* The copies (under the lock): handles recorded into trees, trees, their
   heap bytes; the trees refused for room so far (the first three logged). */
static DWORD rec_copies;
static DWORD rec_trees;
static DWORD rec_bytes;
static DWORD rec_refused_told;

/* A handle's record let go (under the lock): its tree goes with its last. */
static void
rec_release(rec_tree_t *t)
{
    if (!t)
        return;
    rec_copies--;
    if (--t->refs)
        return;
    rec_trees--;
    rec_bytes -= t->bytes;
    heap_free(t);
}

/* Moved whenever a buffer's handle is recorded or forgotten (DirectX 8): a
   DrawPrimitives2 call's resolved streams hold while it stays. Buffers
   move the world epoch not, for nothing a context's flush builds names
   them. */
DWORD buf_epoch;

static DWORD
sh_hash(const void *ddl, DWORD handle)
{
    DWORD x = (((DWORD) ddl >> 4) * 0x9e3779b1u) ^ (handle * 0x85ebca6bu);

    x ^= x >> 15;
    return x & (SH_SLOTS - 1);
}

/* The slot of (ddl, handle), or NULL (under the lock). */
static sh_slot_t *
sh_find(const void *ddl, DWORD handle)
{
    DWORD i = sh_hash(ddl, handle);
    DWORD n;

    for (n = 0; (n < SH_SLOTS) && sh[i].ddl; n++, i = (i + 1) & (SH_SLOTS - 1)) {
        if ((sh[i].ddl == ddl) && (sh[i].handle == handle))
            return &sh[i];
    }
    return NULL;
}

/* Deletes slot i (under the lock): the entries after it in its run that
   hash at or before it move back, so none is cut off from its home. */
static void
sh_delete(DWORD i)
{
    const int   buffer = sh[i].buffer;
    rec_tree_t *tree   = sh[i].tree;
    DWORD       j      = i;

    for (;;) {
        DWORD k;

        j = (j + 1) & (SH_SLOTS - 1);
        if (!sh[j].ddl)
            break;
        k = sh_hash(sh[j].ddl, sh[j].handle);
        /* Stays if its home lies cyclically in (i, j]. */
        if ((i <= j) ? ((i < k) && (k <= j)) : ((i < k) || (k <= j)))
            continue;
        sh[i] = sh[j];
        i     = j;
    }
    memset(&sh[i], 0, sizeof(sh[i]));
    sh_live--;
    rec_release(tree);
    if (buffer)
        __atomic_add_fetch(&buf_epoch, 1, __ATOMIC_RELEASE);
    else
        world_moved();
}

/* Records that `s` holds (ddl, handle): the runtime's surface, or (with
   `tree`) the DLL's copy in it, traced with its tree instead. 0 if the
   table is full. */
static int
sh_set(void *ddl, DWORD handle, LPSURF s, int buffer, rec_tree_t *tree)
{
    sh_slot_t *e;
    DWORD      i;
    LPSURF     old = NULL;

    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle);
    if (!e && (sh_live < SH_SLOTS - 1)) {
        for (i = sh_hash(ddl, handle); sh[i].ddl; i = (i + 1) & (SH_SLOTS - 1))
            ;
        e          = &sh[i];
        e->ddl     = ddl;
        e->handle  = handle;
        e->palette = 0;
        e->lcl     = NULL;
        e->tree    = NULL;
        sh_live++;
    }
    if (e) {
        /* A surface recorded anew (a new handle, a restored or swapped one)
           is one the texture cache has not seen. */
        if (e->lcl != s) {
            e->gen = ++sh_gen;
            if (!e->gen)
                e->gen = ++sh_gen;
        }
        if (e->tree != tree) {
            /* A handle recorded again into a new tree (a restored surface,
               swapped handles): what the GPU may still do with the memory
               it names is not known, so its fence is the last one issued;
               the flag the GPU wrote it goes along when the memory is the
               same. Not a buffer's: the pool keeps buffers' fences, and an
               adopted buffer's dwReserved1 is the pool's index. */
            if (tree && e->lcl && !buffer) {
                s->lpGbl->dwReserved1 = sb ? sb->fence_issued : 0;
                if (e->tree && (e->lcl->lpGbl->fpVidMem == s->lpGbl->fpVidMem))
                    s->dwReserved1 |= e->lcl->dwReserved1 & SURF_GPU_WRITES;
            }
            rec_release(e->tree);
            if (tree) {
                tree->refs++;
                rec_copies++;
            }
            e->tree = tree;
        }
        old       = e->lcl;
        e->lcl    = s;
        e->pid    = GetCurrentProcessId();
        e->buffer = buffer;
    }
    dlock_give(&sh_lock);
    if (buffer)
        __atomic_add_fetch(&buf_epoch, 1, __ATOMIC_RELEASE);
    else
        world_moved();
    if (!e)
        dbg_event(DLL_TAG ": no room for surface handle %u", handle);
    else if ((old != s) && !tree && TRACE(TRACE_HANDLE)) {
        if (old)
            dbg(DLL_TAG " trace: handle %x is %s %x (%s memory, at %x) under %x, was %x", handle, trace_kind(s), (DWORD) s,
                trace_where(s), s->lpGbl->fpVidMem, (DWORD) ddl, (DWORD) old);
        else
            dbg(DLL_TAG " trace: handle %x is %s %x (%s memory, at %x) under %x", handle, trace_kind(s), (DWORD) s, trace_where(s),
                s->lpGbl->fpVidMem, (DWORD) ddl);
    }
    return e != NULL;
}

static DWORD
handle_of(LPSURF s)
{
    return s->lpSurfMore ? s->lpSurfMore->dwSurfaceHandle : 0;
}

static DWORD
caps2(LPSURF s)
{
    return s->lpSurfMore ? s->lpSurfMore->ddsCapsEx.dwCaps2 : 0;
}

/* A copy whose global record is the pool's (an adopted buffer's): not the
   copy's own, so its dwReserved1 is the pool's index, not a fence. */
static int
rec_pooled(LPSURF c)
{
    return c->lpGbl != &((rec_surf_t *) c)->gbl;
}

/* The next buffer of a flipping chain (its ring of attachments); a Z buffer
   attached to a buffer is not one. */
static LPSURF
flip_next(LPSURF s)
{
    ATTACHLIST *al;

    for (al = s->lpAttachList; al; al = al->lpLink) {
        LPSURF a = al->lpAttached;

        if (a && (a->ddsCaps.dwCaps & DDSCAPS_FLIP) && !(a->ddsCaps.dwCaps & DDSCAPS_ZBUFFER))
            return a;
    }
    return NULL;
}

#define FLIP_MEMBERS 16 /* a flipping chain's walk goes no further */

/* Whether a chain member's memory is the one the last FLIP showed. */
static int
flip_shown(LPSURF s)
{
    return s->lpGbl->fpVidMem - sb->fb_linear == sb->flip_start;
}

/* With s the member of a primary flipping chain (one of its members is the
   primary surface) on display, the member the next flip shows: the one
   attached to it. s is on
   display when its memory is the one the last FLIP showed, or, when no
   member's is, when it is the front buffer. NULL otherwise: s in no such
   chain, or not on display (a back buffer, the first or any other). */
LPSURF
d3d7_flip_next(LPSURF s)
{
    LPSURF next;
    LPSURF t;
    int    primary = 0;
    int    shown   = 0;
    int    n;

    if (!s || ((s->ddsCaps.dwCaps & (DDSCAPS_FLIP | DDSCAPS_ZBUFFER)) != DDSCAPS_FLIP) || !(next = flip_next(s)))
        return NULL;
    for (t = s, n = 0; t && (n < FLIP_MEMBERS); n++) {
        primary |= (t->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
        shown |= flip_shown(t);
        t = flip_next(t);
        if (t == s)
            break;
    }
    if (!primary)
        return NULL;
    if (shown ? !flip_shown(s) : !(s->ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER | DDSCAPS_PRIMARYSURFACE)))
        return NULL;
    return next;
}

DWORD
d3d7_surface_gone(LPSURF s)
{
    const DWORD handle = handle_of(s);
    sh_slot_t  *e;
    LPSURF      held  = NULL;
    int         found = 0;
    int         copy  = 0;
    DWORD       fence = s->lpGbl->dwReserved1;

    if (!d3d7_on || !handle) {
        if ((s->ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_EXECUTEBUFFER)) &&
            TRACE(TRACE_GONE))
            dbg(DLL_TAG " trace: %s %x gone without a handle", trace_kind(s), (DWORD) s);
        return fence;
    }
    /* The record's fence read as it goes, in the same hold (in any process:
       DDHELP cleaning up after one ends finds the copy too). */
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(s->lpSurfMore->lpDD_lcl, handle);
    if (e) {
        found = 1;
        held  = e->lcl;
        copy  = e->tree != NULL;
        if (copy)
            fence = rec_pooled(held) ? 0 : held->lpGbl->dwReserved1; /* a pool buffer's fence is the pool's */
        sh_delete((DWORD) (e - sh));
    }
    dlock_give(&sh_lock);
    if (!TRACE(TRACE_GONE))
        return fence;
    if (copy)
        dbg(DLL_TAG " trace: handle %x of %s %x gone (the DLL's copy %x)", handle, trace_kind(s), (DWORD) s, (DWORD) held);
    else if (!found)
        dbg(DLL_TAG " trace: handle %x of %s %x gone (not in the map)", handle, trace_kind(s), (DWORD) s);
    else if (held != s)
        dbg(DLL_TAG " trace: handle %x of %s %x gone (the map held another record, %x)", handle, trace_kind(s), (DWORD) s,
            (DWORD) held);
    else
        dbg(DLL_TAG " trace: handle %x of %s %x gone", handle, trace_kind(s), (DWORD) s);
    return fence;
}

LPSURF
d3d7_surface_gen(void *ddl, DWORD handle, DWORD *gen, DWORD *palette)
{
    sh_slot_t *e;
    LPSURF     s = NULL;

    if (!handle)
        return NULL;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle);
    if (e && e->lcl) {
        LPSURF r = e->lcl;

        if (gen)
            *gen = e->gen;
        if (palette)
            *palette = e->palette;
        if (handle_of(r) == handle)
            s = r;
        else if (r->ddsCaps.dwCaps & DDSCAPS_FLIP) {
            /* Flips moved the handle along the chain. */
            LPSURF t = r;
            int    n;

            for (n = 0; n < FLIP_MEMBERS; n++) {
                t = flip_next(t);
                if (!t || (t == r))
                    break;
                if (handle_of(t) == handle) {
                    s      = t;
                    e->lcl = t;
                    break;
                }
            }
        }
    }
    dlock_give(&sh_lock);
    return s;
}

LPSURF
d3d7_surface(void *ddl, DWORD handle)
{
    return d3d7_surface_gen(ddl, handle, NULL, NULL);
}

LPSURF
d3d7_buffer(void *ddl, DWORD handle)
{
    sh_slot_t *e;
    LPSURF     s            = NULL;
    LPSURF     other        = NULL;
    DWORD      other_handle = 0;

    if (!handle)
        return NULL;

    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle);
    if (e && e->buffer && e->lcl) {
        if (handle_of(e->lcl) == handle)
            s = e->lcl;
        else {
            other        = e->lcl;
            other_handle = handle_of(e->lcl);
        }
    }
    dlock_give(&sh_lock);
    if (!s && TRACE(TRACE_LOOKUP)) {
        if (other)
            dbg(DLL_TAG " trace: buffer handle %x under %x: its record %x holds handle %x now", handle, (DWORD) ddl, (DWORD) other,
                other_handle);
        else
            dbg(DLL_TAG " trace: buffer handle %x under %x: not in the map%s", handle, (DWORD) ddl, e ? " as a buffer" : "");
    }
    return s;
}

void
d3d7_buffer_gone(const void *ddl, DWORD handle, const DDRAWI_DDRAWSURFACE_GBL *gbl)
{
    sh_slot_t *e;
    LPSURF     held = NULL;

    if (!handle || !ddl)
        return;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle);
    if (e && e->buffer && e->tree && e->lcl && (e->lcl->lpGbl == gbl)) {
        held = e->lcl;
        sh_delete((DWORD) (e - sh));
    }
    dlock_give(&sh_lock);
    if (TRACE(TRACE_GONE)) {
        if (held)
            dbg(DLL_TAG " trace: handle %x of %s gone (the DLL's copy %x)", handle, trace_kind(held), (DWORD) held);
        else
            dbg(DLL_TAG " trace: handle %x of an adopted buffer gone (not in the map under %x)", handle, (DWORD) ddl);
    }
}

DDRAWI_DDRAWSURFACE_GBL *
d3d7_buffer_gbl(const void *ddl, DWORD handle)
{
    sh_slot_t               *e;
    DDRAWI_DDRAWSURFACE_GBL *g = NULL;

    if (!handle || !ddl)
        return NULL;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle);
    if (e && e->buffer && e->tree && e->lcl && rec_pooled(e->lcl))
        g = e->lcl->lpGbl;
    dlock_give(&sh_lock);
    return g;
}

/* A DirectX 8 vertex or index buffer (an execute buffer the runtime gave a
   handle): not the runtime's command buffer, which no command names. */
static int
is_buffer(LPSURF s)
{
    return (s->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) && !(caps2(s) & DDSCAPS2_COMMANDBUFFER);
}

/* A surface a DirectX 7 command can name: a texture's top level (or a cube
   map's root), a render target, a Z buffer; a mip level or another cube
   face only if it can be a render target. DirectX 8's vertex and index
   buffers too (SETSTREAMSOURCE, SETINDICES, BUFFERBLT name them); the
   runtime's command buffers no command names. */
static int
sh_wanted(LPSURF s, LPSURF root)
{
    const DWORD caps = s->ddsCaps.dwCaps;

    if (!handle_of(s))
        return 0;
    if (caps & DDSCAPS_EXECUTEBUFFER)
        return d3d8_on && is_buffer(s);
    if (!(caps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER)))
        return 0;
    if ((s != root) && ((caps2(s) & DDSCAPS2_MIPMAPSUBLEVEL) || (caps2(s) & DDSCAPS2_CUBEMAP)))
        return (caps & DDSCAPS_3DDEVICE) != 0;
    return 1;
}

/* Records a surface and what is attached to it, as CreateSurfaceEx asks: a
   texture's levels and a cube map's faces are reached through its root; a
   flipping chain's buffers each hold a handle of their own. Two surfaces
   whose handles the runtime swapped (CREATESURFACEEX_SWAPHANDLES) come in
   two calls, each recorded under the handle it holds now. 0 if the table
   is full. */
#define SH_WALK 128 /* a cube map's six faces of twelve levels, and then some */

static int
sh_record(void *ddl, LPSURF root)
{
    LPSURF seen[SH_WALK];
    LPSURF stack[SH_WALK];
    int    n_seen = 0;
    int    n      = 0;
    int    ok     = 1;

    stack[n++] = root;
    while (n && (n_seen < SH_WALK)) {
        LPSURF      s = stack[--n];
        ATTACHLIST *al;
        int         i;

        for (i = 0; (i < n_seen) && (seen[i] != s); i++)
            ;
        if (i < n_seen)
            continue; /* a ring comes back to its start */
        seen[n_seen++] = s;
        if (sh_wanted(s, root) && !sh_set(ddl, handle_of(s), s, (s->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) != 0, NULL))
            ok = 0;
        for (al = s->lpAttachList; al && (n < SH_WALK); al = al->lpLink) {
            if (al->lpAttached)
                stack[n++] = al->lpAttached;
        }
    }
    return ok;
}

/* --- The DLL's copies (DirectX 8) ----------------------------------------------- */

#define FLIP_CAPS (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER)

void
d3d7_mark_dx8(void *ddl)
{
    const DWORD pid    = GetCurrentProcessId();
    DWORD       copies = 0;
    int         free   = -1;
    int         i;

    dlock_take(&sh_lock, "surface handle");
    for (i = 0; i < DX8_DDLS; i++) {
        if (dx8_ddls[i].ddl == ddl) {
            /* One left by an ended process whose object had this address:
               its handles under it go, so none of its trees is taken for
               this process's. */
            if (dx8_ddls[i].pid != pid) {
                const DWORD old = dx8_ddls[i].pid;
                DWORD       j;

                copies = rec_copies;
                for (j = 0; j < SH_SLOTS;) {
                    if ((sh[j].ddl == ddl) && (sh[j].pid == old))
                        sh_delete(j);
                    else
                        j++;
                }
                copies -= rec_copies;
                dx8_ddls[i].pid = pid;
            }
            break;
        }
        if (!dx8_ddls[i].ddl && (free < 0))
            free = i;
    }
    if ((i == DX8_DDLS) && (free >= 0)) {
        dx8_ddls[free].ddl = ddl;
        dx8_ddls[free].pid = pid;
        dx8_marked++;
    }
    dlock_give(&sh_lock);
    if (copies && sb)
        gpu_wait(sb->fence_issued);
    if ((i == DX8_DDLS) && (free < 0))
        dbg_event(DLL_TAG ": no room to mark DirectDraw object %x as DirectX 8's", (DWORD) ddl);
}

static int rec_wanted(void *ddl, LPSURF root);
static int rec_record(void *ddl, LPSURF root);

void
d3d7_copy_target(void *ddl, LPSURF s)
{
    sh_slot_t *e;
    int        runtime;

    if (!s || !handle_of(s) || !rec_wanted(ddl, s))
        return;
    dlock_take(&sh_lock, "surface handle");
    e       = sh_find(ddl, handle_of(s));
    runtime = !e || !e->tree;
    dlock_give(&sh_lock);
    if (!runtime || !rec_record(ddl, s))
        return;
    /* What the GPU wrote through the runtime's record goes with the copy. */
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, handle_of(s));
    if (e && e->tree)
        e->lcl->dwReserved1 |= s->dwReserved1 & SURF_GPU_WRITES;
    dlock_give(&sh_lock);
}

/* A DirectX 8 context was made on the DirectDraw object, in this process (a
   read without the lock: an entry changes only at a mark or a forget of its
   own object). */
static int
ddl_dx8(const void *ddl)
{
    DWORD pid;
    int   i;

    if (!ddl || !dx8_marked)
        return 0;
    pid = GetCurrentProcessId();
    for (i = 0; i < DX8_DDLS; i++) {
        if ((dx8_ddls[i].ddl == ddl) && (dx8_ddls[i].pid == pid))
            return 1;
    }
    return 0;
}

/* A CreateSurfaceEx root to copy: on a DirectX 8 runtime's DirectDraw
   object, but for the flipping chain (whose handles move with its memory at
   each flip, the runtime's records lasting) and the runtime's own vertex
   and command buffers of the pool (DrawPrimitives2 hands their records in
   at each call). The pool's application vertex and index buffers are
   copied and adopted (rec_build). */
static int
rec_wanted(void *ddl, LPSURF root)
{
    if (!d3d8_on || !ddl_dx8(ddl) || (root->ddsCaps.dwCaps & FLIP_CAPS))
        return 0;
    if ((root->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) && pool_has(root))
        return (caps2(root) & (DDSCAPS2_VERTEXBUFFER | DDSCAPS2_INDEXBUFFER)) != 0;
    return 1;
}

static int
rec_index(LPSURF const *list, int n, LPSURF s)
{
    int i;

    for (i = 0; i < n; i++) {
        if (list[i] == s)
            return i;
    }
    return -1;
}

/* One surface's copy: its size, pitch, format, caps, colour keys, memory
   (a video memory address, or the process's own system memory, which the
   surface keeps until destroyed), handle and the DirectDraw object; its
   fence and flags 0. A level in video memory of a volume (`vol`: the
   root's caps, as CreateSurface's rule has it, or its own) is one
   CreateSurface laid out (every one a DirectX 8 runtime makes): its slices
   its rows' height apart. */
static void
rec_copy(rec_surf_t *r, LPSURF s, void *ddl, int vol)
{
    const DDRAWI_DDRAWSURFACE_GBL  *g = s->lpGbl;
    const DDRAWI_DDRAWSURFACE_MORE *m = s->lpSurfMore;

    memset(r, 0, sizeof(*r));
    r->lcl.lpSurfMore        = &r->more;
    r->lcl.lpGbl             = &r->gbl;
    r->lcl.dwLocalRefCnt     = 1;
    r->lcl.dwProcessId       = s->dwProcessId;
    r->lcl.dwFlags           = s->dwFlags;
    r->lcl.ddsCaps           = s->ddsCaps;
    r->lcl.dwModeCreatedIn   = s->dwModeCreatedIn;
    r->lcl.dwBackBufferCount = s->dwBackBufferCount;
    r->lcl.ddckCKDestBlt     = s->ddckCKDestBlt;
    r->lcl.ddckCKSrcBlt      = s->ddckCKSrcBlt;
    r->gbl.dwRefCnt          = 1;
    r->gbl.dwGlobalFlags     = g->dwGlobalFlags;
    r->gbl.dwBlockSizeY      = g->dwBlockSizeY;
    r->gbl.dwBlockSizeX      = g->dwBlockSizeX;
    r->gbl.fpVidMem          = g->fpVidMem;
    r->gbl.lPitch            = g->lPitch;
    r->gbl.wHeight           = g->wHeight;
    r->gbl.wWidth            = g->wWidth;
    r->gbl.ddpfSurface       = g->ddpfSurface;
    r->more.dwSize           = sizeof(r->more);
    r->more.lpDD_lcl         = ddl;
    if (m) {
        r->more.dwMipMapCount    = m->dwMipMapCount;
        r->more.dwBytesAllocated = m->dwBytesAllocated;
        r->more.ddsCapsEx        = m->ddsCapsEx;
        r->more.dwTextureStage  = m->dwTextureStage;
        r->more.dwSurfaceHandle = m->dwSurfaceHandle;
        r->more.dwFVF           = m->dwFVF;
    }
    if ((vol || (r->more.ddsCapsEx.dwCaps2 & DDSCAPS2_VOLUME)) && !(s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && r->gbl.fpVidMem) {
        r->lcl.dwReserved1  = SURF_SLICES;
        r->gbl.dwBlockSizeY = (DWORD) r->gbl.lPitch * r->gbl.wHeight;
    }
}

/* The copies of a root and everything attached to it (outside the lock: the
   heap has its own), their attachments rebuilt between them in the order the
   runtime's lists have them, the builder's reference held. NULL if the heap
   has no room, or (*flip) the walk reached a flipping chain. */
static rec_tree_t *
rec_build(void *ddl, LPSURF root, int *flip)
{
    LPSURF      list[SH_WALK];
    LPSURF      stack[SH_WALK];
    int         n     = 0;
    int         top   = 0;
    int         links = 0;
    int         k     = 0;
    int         i;
    DWORD       bytes;
    rec_tree_t *t;
    ATTACHLIST *link;

    *flip        = 0;
    stack[top++] = root;
    while (top && (n < SH_WALK)) {
        LPSURF      s = stack[--top];
        ATTACHLIST *al;

        if (rec_index(list, n, s) >= 0)
            continue;
        if (s->ddsCaps.dwCaps & FLIP_CAPS) {
            *flip = 1;
            return NULL;
        }
        list[n++] = s;
        for (al = s->lpAttachList; al && (top < SH_WALK); al = al->lpLink) {
            if (al->lpAttached)
                stack[top++] = al->lpAttached;
        }
    }
    for (i = 0; i < n; i++) {
        const ATTACHLIST *al;

        for (al = list[i]->lpAttachList; al; al = al->lpLink)
            links += rec_index(list, n, al->lpAttached) >= 0;
    }
    bytes = sizeof(rec_tree_t) + (DWORD) n * sizeof(rec_surf_t) + (DWORD) links * sizeof(ATTACHLIST);
    t     = heap_alloc(bytes);
    if (!t)
        return NULL;
    t->refs  = 1;
    t->n     = (DWORD) n;
    t->bytes = bytes;
    t->pad   = 0;
    link     = (ATTACHLIST *) &t->s[n];
    for (i = 0; i < n; i++) {
        DDRAWI_DDRAWSURFACE_GBL *own;

        rec_copy(&t->s[i], list[i], ddl, (caps2(root) & DDSCAPS2_VOLUME) != 0);
        /* The pool's application buffer: its memory in the pool record's
           own global record, which the copy reads through. One of video
           memory left without it (the pool did not know its record) is
           counted and traced: its copy keeps the memory it has now. */
        if (!(list[i]->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER))
            continue;
        own = pool_adopt(list[i], ddl, handle_of(list[i]));
        if (own) {
            t->s[i].lcl.lpGbl = own;
            DIAG(diag.rec_adopted++);
        } else if (!(list[i]->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) &&
                   (caps2(list[i]) & (DDSCAPS2_VERTEXBUFFER | DDSCAPS2_INDEXBUFFER))) {
            DIAG(diag.rec_unadopted++);
            if (TRACE(TRACE_LOOKUP))
                dbg(DLL_TAG " trace: handle %x: %s %x copied without its pool record (memory %x as now)", handle_of(list[i]),
                    trace_kind(list[i]), (DWORD) list[i], list[i]->lpGbl->fpVidMem);
        }
    }
    for (i = 0; i < n; i++) {
        ATTACHLIST      **tail = &t->s[i].lcl.lpAttachList;
        const ATTACHLIST *al;

        for (al = list[i]->lpAttachList; al && (k < links); al = al->lpLink) {
            const int j = rec_index(list, n, al->lpAttached);

            if (j < 0)
                continue;
            link[k].dwFlags     = al->dwFlags;
            link[k].lpLink      = NULL;
            link[k].lpAttached  = &t->s[j].lcl;
            link[k].lpIAttached = NULL;
            *tail               = &link[k];
            tail                = &link[k].lpLink;
            k++;
        }
    }
    return t;
}

/* The caps a copy and the runtime's surface must agree on. */
#define REC_CAPS_MASK                                                                                                    \
    (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY | DDSCAPS_MIPMAP | \
     DDSCAPS_EXECUTEBUFFER)

/* The trees a new tree's handles leave (held while it records), to count
   those left split: some of their handles moved, others stayed. */
#define REC_OLD 8

/* CreateSurfaceEx for a root rec_wanted takes: the copies, and every one
   with a handle (levels and faces too: a Lock or a destroy notice names
   them) recorded; the runtime's command buffers never. 0 if the heap or
   the table has no room. */
static int
rec_record(void *ddl, LPSURF root)
{
    int         flip = 0;
    rec_tree_t *t;
    rec_tree_t *old[REC_OLD];
    int         n_old = 0;
    DWORD       handles = 0;
    DWORD       i;
    int         ok = 1;
    int         kept;
    int         j;
    sh_slot_t  *e;

    /* A level (or a cube face but the first) announced on its own after its
       chain's root: its handle already names its copy in the root's tree,
       the same memory, shape, caps and format. That copy stays, so the
       chain's fences and the level's Lock meet in one record. */
    dlock_take(&sh_lock, "surface handle");
    e    = handle_of(root) ? sh_find(ddl, handle_of(root)) : NULL;
    kept = e && e->tree && (e->lcl != &e->tree->s[0].lcl) &&
           ((caps2(root) & DDSCAPS2_MIPMAPSUBLEVEL) ||
            ((caps2(root) & DDSCAPS2_CUBEMAP) && !(caps2(root) & DDSCAPS2_CUBEMAP_POSITIVEX))) &&
           (e->lcl->lpGbl->fpVidMem == root->lpGbl->fpVidMem) && (e->lcl->lpGbl->lPitch == root->lpGbl->lPitch) &&
           (e->lcl->lpGbl->wWidth == root->lpGbl->wWidth) && (e->lcl->lpGbl->wHeight == root->lpGbl->wHeight) &&
           !((e->lcl->ddsCaps.dwCaps ^ root->ddsCaps.dwCaps) & REC_CAPS_MASK) && (caps2(e->lcl) == caps2(root)) &&
           !memcmp(&e->lcl->lpGbl->ddpfSurface, &root->lpGbl->ddpfSurface, sizeof(DDPIXELFORMAT));
    dlock_give(&sh_lock);
    if (kept) {
        DIAG(diag.rec_kept++);
        return 1;
    }
    t = rec_build(ddl, root, &flip);
    if (flip)
        return sh_record(ddl, root);
    if (!t) {
        DIAG(diag.rec_refused++);
        if (rec_refused_told < 3) {
            rec_refused_told++;
            dbg_event_own(DLL_TAG ": no room in the heap for the records of surface handle %x", handle_of(root));
        }
        /* A handle recorded before (a restored surface) must not keep naming
           what it named: it names nothing now. */
        dlock_take(&sh_lock, "surface handle");
        e = handle_of(root) ? sh_find(ddl, handle_of(root)) : NULL;
        if (e)
            sh_delete((DWORD) (e - sh));
        dlock_give(&sh_lock);
        return 0;
    }
    dlock_take(&sh_lock, "surface handle");
    rec_trees++;
    rec_bytes += t->bytes;
    for (i = 0; i < t->n; i++) {
        const DWORD h = handle_of(&t->s[i].lcl);

        e = h ? sh_find(ddl, h) : NULL;
        if (!e || !e->tree || (n_old == REC_OLD))
            continue;
        for (j = 0; (j < n_old) && (old[j] != e->tree); j++)
            ;
        if (j == n_old) {
            old[n_old++] = e->tree;
            e->tree->refs++;
        }
    }
    dlock_give(&sh_lock);
    for (i = 0; i < t->n; i++) {
        LPSURF      c = &t->s[i].lcl;
        const DWORD h = handle_of(c);
        const int   x = (c->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) != 0;

        if (!h || (x && !is_buffer(c)))
            continue;
        if (sh_set(ddl, h, c, x, t))
            handles++;
        else
            ok = 0;
    }
    if (handles && TRACE(TRACE_HANDLE))
        dbg(DLL_TAG " trace: handle %x is the DLL's copy of %s %x (%s, at %x), %u surfaces, %u handles, under %x",
            handle_of(root), trace_kind(root), (DWORD) root, trace_where(root), root->lpGbl->fpVidMem, t->n, handles,
            (DWORD) ddl);
    /* A mip chain whose levels its copy does not reach as the texture's
       walk follows them (attached later, or recorded apart): its texture
       samples without mipmaps. A cube map's are counted on its first face. */
    if ((root->ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_MIPMAP)) == (DDSCAPS_TEXTURE | DDSCAPS_MIPMAP) && root->lpSurfMore) {
        DWORD  levels = 1;
        LPSURF l;

        for (l = tex_mip_next(&t->s[0].lcl); l && (levels < VGPU98_TEX_MAX_LEVELS); l = tex_mip_next(l))
            levels++;
        if (levels < root->lpSurfMore->dwMipMapCount) {
            DIAG(diag.rec_levelless++);
            if (TRACE(TRACE_LOOKUP))
                dbg(DLL_TAG " trace: handle %x: the copy of %s %x reaches %u of its %u levels", handle_of(root),
                    trace_kind(root), (DWORD) root, levels, root->lpSurfMore->dwMipMapCount);
        }
    }
    /* The builder's reference: a tree no handle went into goes now. The
       old trees' holds too: one still holding handles was left split. */
    dlock_take(&sh_lock, "surface handle");
    if (!--t->refs) {
        rec_trees--;
        rec_bytes -= t->bytes;
        heap_free(t);
    }
    for (j = 0; j < n_old; j++) {
        if (old[j]->refs > 1)
            DIAG(diag.rec_moved++);
        if (!--old[j]->refs) {
            rec_trees--;
            rec_bytes -= old[j]->bytes;
            heap_free(old[j]);
        }
    }
    dlock_give(&sh_lock);
    return ok;
}

/* The self-check's mismatches, the first three of each kind logged. */
#define REC_MEMORY  0
#define REC_SHAPE   1
#define REC_CAPS    2
#define REC_MOVED   3
#define REC_MISSING 4

static DWORD rec_told[REC_KINDS];

static void
rec_mismatch(int kind, DWORD handle, const void *ddl, DWORD theirs, DWORD ours)
{
    static const char *const what[REC_KINDS] = { "memory", "pitch or size", "caps", "record", "surface without a record" };

    DIAG(diag.rec_mismatches[kind]++);
    if (rec_told[kind] >= 3)
        return;
    rec_told[kind]++;
    dbg(DLL_TAG ": record check, handle %x under %x: the runtime's %s %x, the DLL's %x", handle, (DWORD) ddl, what[kind], theirs,
        ours);
}

LPSURF
surf_own(LPSURF s)
{
    sh_slot_t *e;
    LPSURF     r      = s;
    int        kind   = -1;
    DWORD      theirs = 0;
    DWORD      ours   = 0;
    DWORD      h;
    void      *ddl;

    if (!s || heap_owns(s) || !d3d8_on || !s->lpSurfMore || !s->lpSurfMore->dwSurfaceHandle || !ddl_dx8(s->lpSurfMore->lpDD_lcl))
        return s;
    h   = s->lpSurfMore->dwSurfaceHandle;
    ddl = s->lpSurfMore->lpDD_lcl;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, h);
    if (e && e->tree) {
        const LPSURF c = e->lcl;

        r = c;
        if (diag_audit8) {
            DIAG(diag.rec_checks++);
            /* An adopted buffer's memory moves with renames, which the
               runtime's record learns of only at its next Lock: its size
               and caps are compared. */
            if (!rec_pooled(c) && (c->lpGbl->fpVidMem != s->lpGbl->fpVidMem)) {
                kind   = REC_MEMORY;
                theirs = s->lpGbl->fpVidMem;
                ours   = c->lpGbl->fpVidMem;
            } else if (c->lpGbl->lPitch != s->lpGbl->lPitch) {
                kind   = REC_SHAPE;
                theirs = (DWORD) s->lpGbl->lPitch;
                ours   = (DWORD) c->lpGbl->lPitch;
            } else if ((c->lpGbl->wWidth != s->lpGbl->wWidth) || (c->lpGbl->wHeight != s->lpGbl->wHeight)) {
                kind   = REC_SHAPE;
                theirs = ((DWORD) s->lpGbl->wWidth << 16) | s->lpGbl->wHeight;
                ours   = ((DWORD) c->lpGbl->wWidth << 16) | c->lpGbl->wHeight;
            } else if (((c->ddsCaps.dwCaps ^ s->ddsCaps.dwCaps) & REC_CAPS_MASK) || (caps2(c) != caps2(s))) {
                kind   = REC_CAPS;
                theirs = s->ddsCaps.dwCaps;
                ours   = c->ddsCaps.dwCaps;
            }
        }
    } else if (e && diag_audit8) {
        /* The runtime's own record: a buffer the pool did not adopt (the
           runtime's own vertex and command buffers) and one made before
           the object was marked stay put; a flipping chain's follows its
           handle around the chain. */
        DIAG(diag.rec_checks++);
        if ((e->buffer || !(s->ddsCaps.dwCaps & FLIP_CAPS)) && (e->lcl != s)) {
            kind   = REC_MOVED;
            theirs = (DWORD) s;
            ours   = (DWORD) e->lcl;
        }
    } else if (!e && diag_audit8 && !(s->ddsCaps.dwCaps & FLIP_CAPS) &&
               ((s->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) ? is_buffer(s)
                                                            : (s->ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER)) != 0)) {
        /* A surface the map should hold but lacks: a texture, target, Z
           buffer or vertex or index buffer (not the runtime's command
           buffers, not a plain or primary surface, never recorded). */
        DIAG(diag.rec_checks++);
        kind   = REC_MISSING;
        theirs = (DWORD) s;
    }
    dlock_give(&sh_lock);
    if (kind >= 0)
        rec_mismatch(kind, h, ddl, theirs, ours);
    return r;
}

int
surf_dx8(LPSURF s)
{
    return d3d8_on && s && s->lpSurfMore && s->lpSurfMore->dwSurfaceHandle && ddl_dx8(s->lpSurfMore->lpDD_lcl);
}

void
d3d7_record_figures(DWORD *handles, DWORD *copies, DWORD *trees, DWORD *kb)
{
    *handles = sh_live;
    *copies  = rec_copies;
    *trees   = rec_trees;
    *kb      = rec_bytes >> 10;
}

/* The video memory surfaces a local object has handles for: a fence as their
   last use (a batch that bound more textures than it lists). */
void
d3d7_fence_all(void *ddl, DWORD fence)
{
    gsurf_t g;
    DWORD   i;

    dlock_take(&sh_lock, "surface handle");
    for (i = 0; i < SH_SLOTS; i++) {
        if ((sh[i].ddl == ddl) && sh[i].lcl && !sh[i].buffer && surf_describe(sh[i].lcl, &g))
            tex_used(sh[i].lcl, fence);
    }
    dlock_give(&sh_lock);
}

DWORD
d3d7_live_handles(void)
{
    return sh_live;
}

/* --- Palettes (SETPALETTE, UPDATEPALETTE) ----------------------------------- */

/* DirectX 8 games number a palette a texture (GTA3 past 1500), and keep them
   for the device's life: 8192 records, three quarters of them live at most
   so a probe stays short. */
#define PALETTES      8192
#define PALETTES_LIVE (PALETTES / 4 * 3)

typedef struct pal {
    void  *ddl; /* NULL: a free record */
    DWORD  handle;
    DWORD  flags;   /* DDRAWIPAL_ALPHA */
    DWORD *colors;  /* 256 PALETTEENTRY dwords (red, green, blue, flags), in the heap; NULL until UPDATEPALETTE */
    DWORD  pid;
    DWORD  version; /* new at its creation, each UPDATEPALETTE and a change of flags; never 0 or PAL_UNBOUND */
} pal_t;

static pal_t   pals[PALETTES];
static DWORD   pal_live;
static DWORD   pal_versions; /* the versions' counter */
static dlock_t pal_lock;

/* A version no palette has had (under the lock). */
static DWORD
pal_version_new(void)
{
    do
        pal_versions++;
    while (!pal_versions || (pal_versions == PAL_UNBOUND));
    return pal_versions;
}

static DWORD
pal_hash(const void *ddl, DWORD handle)
{
    DWORD x = (((DWORD) ddl >> 4) * 0x9e3779b1u) ^ (handle * 0x85ebca6bu);

    x ^= x >> 15;
    return x & (PALETTES - 1);
}

/* The record of (ddl, handle), made if `make` (under the lock): an
   open-addressed hash, as the handle map is. A quarter of the records
   always stays free, so a probe ends soon; records go in d3d7_forget,
   each by pal_delete. */
static pal_t *
pal_find(void *ddl, DWORD handle, int make)
{
    DWORD i = pal_hash(ddl, handle);
    DWORD n;

    if (!ddl)
        return NULL; /* a record's NULL object marks it free */
    for (n = 0; (n < PALETTES) && pals[i].ddl; n++, i = (i + 1) & (PALETTES - 1)) {
        if ((pals[i].ddl == ddl) && (pals[i].handle == handle))
            return &pals[i];
    }
    if (!make || (n == PALETTES) || (pal_live >= PALETTES_LIVE))
        return NULL;
    pals[i].ddl     = ddl;
    pals[i].handle  = handle;
    pals[i].flags   = 0;
    pals[i].colors  = NULL;
    pals[i].pid     = GetCurrentProcessId();
    pals[i].version = pal_version_new();
    pal_live++;
    return &pals[i];
}

/* Record i goes, the records after it in its run moved back where a probe
   still finds them, as the handle map deletes (under the lock; its colours
   already freed). A record from further on may come into slot i. */
static void
pal_delete(DWORD i)
{
    DWORD j = i;

    for (;;) {
        DWORD k;

        j = (j + 1) & (PALETTES - 1);
        if (!pals[j].ddl)
            break;
        k = pal_hash(pals[j].ddl, pals[j].handle);
        /* Stays if its home lies cyclically in (i, j]. */
        if ((i <= j) ? ((i < k) && (k <= j)) : ((i < k) || (k <= j)))
            continue;
        pals[i] = pals[j];
        i       = j;
    }
    memset(&pals[i], 0, sizeof(pals[i]));
    pal_live--;
}

/* Moved by every change of what a stage's palette is: a binding, flags, a
   palette made, colours, palettes forgotten. A flush that finds it where it
   was skips its stages' palette lookups. */
static DWORD pal_epoch_v;

static void
pal_epoch_move(void)
{
    __atomic_add_fetch(&pal_epoch_v, 1, __ATOMIC_RELEASE);
}

DWORD
d3d7_palette_epoch(void)
{
    return __atomic_load_n(&pal_epoch_v, __ATOMIC_ACQUIRE);
}

/* A palette, or its colours, with no room: the first three lines from each
   context's creation on (d3d7_lines_again), the rest counted in the report
   (a game past the table's live cap would otherwise write a line at each
   palette it sets). */
static DWORD pal_refused_told;

void
d3d7_lines_again(void)
{
    __atomic_store_n(&pal_refused_told, 0, __ATOMIC_RELAXED);
}

static void
pal_no_room(DWORD handle)
{
    DIAG(diag.pal_refused++);
    if (__atomic_fetch_add(&pal_refused_told, 1, __ATOMIC_RELAXED) < 3)
        dbg_event_own(DLL_TAG ": no room for palette %u (more counted in the report's palettes)", handle);
}

/* SETPALETTE: the palette a surface draws with. The surface's record alone
   changes (and the palette's version, with new flags): a stage drawing it
   compares the palette's version with what it sent (d3d7_palette_state),
   so nothing else is checked again. 1 if anything changed: a
   runtime that sends the same SETPALETTE before every draw leaves its
   context clean. */
int
d3d7_set_palette(void *ddl, DWORD surface, DWORD handle, DWORD flags)
{
    sh_slot_t *e;
    pal_t     *p       = NULL;
    int        changed = 0;

    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, surface);
    if (e && (e->palette != handle)) {
        e->palette = handle;
        changed    = 1;
    }
    dlock_give(&sh_lock);
    if (handle) {
        DWORD live;

        dlock_take(&pal_lock, "palette");
        live = pal_live;
        p    = pal_find(ddl, handle, 1);
        if (p && (pal_live != live))
            changed = 1; /* made now: a stage bound to it had none */
        if (p && (p->flags != flags)) {
            p->flags   = flags;
            p->version = pal_version_new();
            changed    = 1;
        }
        dlock_give(&pal_lock);
        if (!p)
            pal_no_room(handle);
    }
    if (changed)
        pal_epoch_move();
    return changed;
}

/* UPDATEPALETTE; 1 if the palette changed (entries that differ, colours or a
   record made now): the same entries again change nothing. */
int
d3d7_update_palette(void *ddl, DWORD handle, DWORD first, DWORD count, const uint8_t *entries)
{
    pal_t *p;
    DWORD  live;
    int    changed = 0;

    if (first >= 256)
        return 0;
    if (count > 256 - first)
        count = 256 - first;
    dlock_take(&pal_lock, "palette");
    live = pal_live;
    p    = pal_find(ddl, handle, 1);
    if (p && (pal_live != live))
        changed = 1; /* made now */
    if (p && !p->colors && (p->colors = heap_alloc(1024))) {
        memset(p->colors, 0, 1024);
        changed = 1;
    }
    /* The same entries again change nothing (no version, no epoch). */
    if (p && p->colors && memcmp(p->colors + first, entries, count * 4)) {
        memcpy(p->colors + first, entries, count * 4);
        changed = 1;
    }
    if (p && changed)
        p->version = pal_version_new();
    dlock_give(&pal_lock);
    if (changed)
        pal_epoch_move();
    if (!p || !p->colors)
        pal_no_room(handle);
    return changed;
}

void
d3d7_palette_flags(void *ddl, DWORD palette, int *alpha, DWORD *version)
{
    pal_t *p;

    *alpha   = 0;
    *version = 0;
    if (!palette)
        return;
    dlock_take(&pal_lock, "palette");
    p = pal_find(ddl, palette, 0);
    if (p) {
        *alpha   = (p->flags & DDRAWIPAL_ALPHA) != 0;
        *version = p->version;
    }
    dlock_give(&pal_lock);
}

void
d3d7_palette_state(void *ddl, DWORD surface, DWORD *handle, DWORD *version)
{
    sh_slot_t *e;
    pal_t     *p;

    *handle  = 0;
    *version = 0;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, surface);
    if (e)
        *handle = e->palette;
    dlock_give(&sh_lock);
    if (!*handle)
        return;
    dlock_take(&pal_lock, "palette");
    p = pal_find(ddl, *handle, 0);
    if (p)
        *version = p->version;
    dlock_give(&pal_lock);
}

DWORD
d3d7_palettes_live(DWORD *cap)
{
    *cap = PALETTES_LIVE;
    return pal_live;
}

DWORD
d3d7_palette_of(void *ddl, DWORD surface, int *alpha)
{
    sh_slot_t *e;
    pal_t     *p;
    DWORD      handle = 0;

    *alpha = 0;
    dlock_take(&sh_lock, "surface handle");
    e = sh_find(ddl, surface);
    if (e)
        handle = e->palette;
    dlock_give(&sh_lock);
    if (!handle)
        return 0;
    dlock_take(&pal_lock, "palette");
    p = pal_find(ddl, handle, 0);
    if (p)
        *alpha = (p->flags & DDRAWIPAL_ALPHA) != 0;
    dlock_give(&pal_lock);
    return handle;
}

int
d3d7_palette_alpha(void *ddl, DWORD palette)
{
    pal_t *p;
    int    alpha = 0;

    dlock_take(&pal_lock, "palette");
    p = pal_find(ddl, palette, 0);
    if (p)
        alpha = (p->flags & DDRAWIPAL_ALPHA) != 0;
    dlock_give(&pal_lock);
    return alpha;
}

/* PALETTEENTRY holds red, green, blue and flags (alpha) in that byte order;
   the GPU takes D3DCOLOR. */
int
d3d7_palette(void *ddl, DWORD surface, DWORD *colors, int *alpha)
{
    const DWORD handle = d3d7_palette_of(ddl, surface, alpha);
    pal_t      *p;
    int         ok = 0;
    int         i;

    if (!handle)
        return 0;
    dlock_take(&pal_lock, "palette");
    p = pal_find(ddl, handle, 0);
    if (p && p->colors) {
        for (i = 0; i < 256; i++) {
            const DWORD e = p->colors[i];

            colors[i] = (e & 0xff000000u) | ((e & 0xff) << 16) | (e & 0xff00) | ((e >> 16) & 0xff);
        }
        ok = 1;
    }
    dlock_give(&pal_lock);
    return ok;
}

void
d3d7_forget(const void *ddl, DWORD pid)
{
    DWORD copies;
    DWORD i;
    DWORD gone;

    dlock_take(&sh_lock, "surface handle");
    copies = rec_copies;
    for (i = 0; i < SH_SLOTS;) {
        const sh_slot_t *e = &sh[i];

        /* A deletion may pull another entry into this slot: look again. */
        if (e->ddl && (ddl ? (e->ddl == ddl) : (e->pid == pid)))
            sh_delete(i);
        else
            i++;
    }
    for (i = 0; i < DX8_DDLS; i++) {
        if (dx8_ddls[i].ddl && (ddl ? (dx8_ddls[i].ddl == ddl) : (dx8_ddls[i].pid == pid))) {
            dx8_ddls[i].ddl = NULL;
            dx8_ddls[i].pid = 0;
            dx8_marked--;
        }
    }
    gone = copies - rec_copies;
    dlock_give(&sh_lock);
    /* Copies went with their handles: their fences with them, so a
       DestroySurface after this finds none to wait for. The GPU finishes
       everything issued, which may read their memory, now. */
    if (gone && sb)
        gpu_wait(sb->fence_issued);
    dlock_take(&pal_lock, "palette");
    gone = 0;
    for (i = 0; i < PALETTES;) {
        /* A deletion may pull another record into this slot: look again. */
        if (pals[i].ddl && (ddl ? (pals[i].ddl == ddl) : (pals[i].pid == pid))) {
            heap_free(pals[i].colors);
            pal_delete(i);
            gone++;
        } else
            i++;
    }
    dlock_give(&pal_lock);
    if (gone)
        pal_epoch_move();
    world_moved();
}

/* --- The Miscellaneous2 callbacks --------------------------------------------- */

/* After DirectDraw created surfaces (DirectDraw's emulation too, for system
   memory ones), or restored lost ones: the root and everything attached
   take their handles; DDERR_OUTOFMEMORY when the table is full. A system
   memory surface without memory is being destroyed; a video memory one is
   being destroyed or lost, which DestroySurface handles. */
GUARDED_FIG(static, CreateSurfaceEx, DDHAL_CREATESURFACEEXDATA, ddRVal, d->lpDDSLcl, d->dwFlags)
{
    LPSURF s = d->lpDDSLcl;

    d->ddRVal = DD_OK;
    if (!s || !d->lpDDLcl)
        return DDHAL_DRIVER_HANDLED;
    if (!s->lpGbl->fpVidMem) {
        if (s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
            d3d7_surface_gone(s);
        return DDHAL_DRIVER_HANDLED;
    }
    DIAG(diag.surfaces_ex++);
    /* A volume texture's levels (DirectX 8): the slice pitch the runtime
       locks them with, rows times height (ddraw.c's CreateSurface laid
       them out so: SURF_SLICES). */
    if (caps2(s) & DDSCAPS2_VOLUME) {
        LPSURF l = s;
        DWORD  n;

        for (n = 0; l && (n < VGPU98_TEX_VOLUME_LEVELS); n++, l = tex_mip_next(l)) {
            if (l->lpGbl->fpVidMem && !(l->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && (l->dwReserved1 & SURF_SLICES))
                l->lpGbl->dwBlockSizeY = (DWORD) l->lpGbl->lPitch * l->lpGbl->wHeight;
        }
    }
    if (!(rec_wanted(d->lpDDLcl, s) ? rec_record(d->lpDDLcl, s) : sh_record(d->lpDDLcl, s)))
        d->ddRVal = DDERR_OUTOFMEMORY;
    return DDHAL_DRIVER_HANDLED;
}

/* The runtime's queries of driver statistics (GetInfo): none kept. */
GUARDED_FIG(static, GetDriverState, DDHAL_GETDRIVERSTATEDATA, ddRVal, d->dwFlags, d->dwhContext)
{
    d->ddRVal = DDERR_UNSUPPORTED;
    return DDHAL_DRIVER_NOTHANDLED;
}

/* A DirectDraw local object goes (and before a context is made): its
   handles and palettes with it. */
GUARDED_FIG(static, DestroyDDLocal, DDHAL_DESTROYDDLOCALDATA, ddRVal, d->pDDLcl, 0)
{
    const DWORD live = sh_live;

    d3d7_forget(d->pDDLcl, 0);
    if (TRACE(TRACE_GONE))
        dbg(DLL_TAG " trace: DirectDraw object %x gone: %u handles forgotten", (DWORD) d->pDDLcl, live - sh_live);
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DDHAL_DDMISCELLANEOUS2CALLBACKS d3d7_misc2;

void
d3d7_init(void)
{
    memset(&d3d7_misc2, 0, sizeof(d3d7_misc2));
    d3d7_misc2.dwSize  = sizeof(d3d7_misc2);
    d3d7_misc2.dwFlags = DDHAL_MISC2CB32_CREATESURFACEEX | DDHAL_MISC2CB32_GETDRIVERSTATE | DDHAL_MISC2CB32_DESTROYDDLOCAL;
    d3d7_misc2.CreateSurfaceEx = (DWORD) CreateSurfaceEx;
    d3d7_misc2.GetDriverState  = (DWORD) GetDriverState;
    d3d7_misc2.DestroyDDLocal  = (DWORD) DestroyDDLocal;
}

/* --- State blocks (STATESET) --------------------------------------------------- */

/* A state block is the state commands recorded between its BEGIN and END,
   kept as the runtime sent them and replayed through DrawPrimitives2's
   state commands at EXECUTE; CAPTURE writes the context's current values
   into them, as the reference rasterizer does. Blocks belong to a context;
   a context's calls come one at a time, so only finding and freeing records
   takes the lock. */
#define STATE_BLOCKS 1024

typedef struct sblock {
    DWORD    ctx; /* the context's handle; 0: a free record */
    DWORD    handle;
    uint8_t *data; /* in the heap */
    DWORD    len;
    DWORD    cap;
} sblock_t;

static sblock_t sblocks[STATE_BLOCKS];
static dlock_t  sb_lock;

static sblock_t *
sb_find(DWORD ctx, DWORD handle)
{
    DWORD i;

    for (i = 0; i < STATE_BLOCKS; i++) {
        if ((sblocks[i].ctx == ctx) && (sblocks[i].handle == handle))
            return &sblocks[i];
    }
    return NULL;
}

static void
sb_free(sblock_t *b)
{
    heap_free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    b->ctx          = 0;
}

int
d3d7_sb_begin(d3d_ctx_t *c, DWORD handle)
{
    const DWORD ctx = ctx_handle(c);
    sblock_t   *b;
    DWORD       i;

    dlock_take(&sb_lock, "state block");
    b = sb_find(ctx, handle);
    if (b) {
        /* Recorded again: the old commands go. */
        b->len = 0;
    } else {
        for (i = 0; i < STATE_BLOCKS; i++) {
            if (!sblocks[i].ctx) {
                b         = &sblocks[i];
                b->ctx    = ctx;
                b->handle = handle;
                break;
            }
        }
    }
    dlock_give(&sb_lock);
    if (!b) {
        dbg_event(DLL_TAG ": no room for state block %u", handle);
        c->sb_rec = SB_DISCARD;
        return 0;
    }
    c->sb_rec = (DWORD) (b - sblocks) + 1;
    DIAG(diag.sb_recorded++);
    return 1;
}

void
d3d7_sb_record(d3d_ctx_t *c, const uint8_t *cmd, DWORD bytes)
{
    sblock_t *b;

    if (c->sb_rec == SB_DISCARD)
        return;
    b = &sblocks[c->sb_rec - 1];

    if (b->len + bytes > b->cap) {
        const DWORD cap = (b->len + bytes + 255) & ~255u;
        uint8_t    *n   = heap_grow(b->data, b->len, cap);

        if (!n) {
            dbg_event(DLL_TAG ": no room to record state block %u", b->handle);
            return;
        }
        b->data = n;
        b->cap  = cap;
    }
    memcpy(b->data + b->len, cmd, bytes);
    b->len += bytes;
}

uint8_t *
d3d7_sb_data(d3d_ctx_t *c, DWORD handle, DWORD *len)
{
    sblock_t *b;

    dlock_take(&sb_lock, "state block");
    b = sb_find(ctx_handle(c), handle);
    dlock_give(&sb_lock);
    *len = b ? b->len : 0;
    return b ? b->data : NULL;
}

void
d3d7_sb_delete(d3d_ctx_t *c, DWORD handle)
{
    sblock_t *b;

    dlock_take(&sb_lock, "state block");
    b = sb_find(ctx_handle(c), handle);
    if (b) {
        if (c->sb_rec == (DWORD) (b - sblocks) + 1)
            c->sb_rec = 0;
        sb_free(b);
    }
    dlock_give(&sb_lock);
}

void
d3d7_sb_forget(DWORD ctx)
{
    DWORD i;

    dlock_take(&sb_lock, "state block");
    for (i = 0; i < STATE_BLOCKS; i++) {
        if (sblocks[i].ctx == ctx)
            sb_free(&sblocks[i]);
    }
    dlock_give(&sb_lock);
}

DWORD
d3d7_sb_live(void)
{
    DWORD i;
    DWORD n = 0;

    for (i = 0; i < STATE_BLOCKS; i++)
        n += sblocks[i].ctx != 0;
    return n;
}

/* --- TEXBLT ----------------------------------------------------------------- */

/* A face of a cube map by its DDSCAPS2 bit, from its root; NULL if missing. */
static LPSURF
cube_face(LPSURF root, DWORD bit)
{
    ATTACHLIST *al;

    if (caps2(root) & bit)
        return root;
    for (al = root->lpAttachList; al; al = al->lpLink) {
        LPSURF a = al->lpAttached;

        if (a && (caps2(a) & bit) && !(caps2(a) & DDSCAPS2_MIPMAPSUBLEVEL))
            return a;
    }
    return NULL;
}

/* One rectangle: `rs` of the source (in video memory, or with `mem` in
   system memory) to (dx, dy) in the destination, clipped to both. From
   video memory a BLT, from system memory an UPLOAD of its bytes; the formats
   must match (the runtime guarantees it). 0 if the ring stopped. */
static int
texblt_rect(const gsurf_t *dst, const gsurf_t *src, const uint8_t *mem, LONG dx, LONG dy, RECTL rs)
{
    gsurf_t gd = *dst;
    gsurf_t gs = *src;
    RECTL   rd;
    int     ok;

    if (gs.format != gd.format)
        return 1;
    if (rs.left < 0) {
        dx -= rs.left;
        rs.left = 0;
    }
    if (rs.top < 0) {
        dy -= rs.top;
        rs.top = 0;
    }
    if (dx < 0) {
        rs.left -= dx;
        dx = 0;
    }
    if (dy < 0) {
        rs.top -= dy;
        dy = 0;
    }
    if (rs.right > (LONG) gs.width)
        rs.right = (LONG) gs.width;
    if (rs.bottom > (LONG) gs.height)
        rs.bottom = (LONG) gs.height;
    if (rs.right - rs.left > (LONG) gd.width - dx)
        rs.right = rs.left + (LONG) gd.width - dx;
    if (rs.bottom - rs.top > (LONG) gd.height - dy)
        rs.bottom = rs.top + (LONG) gd.height - dy;
    if ((rs.left >= rs.right) || (rs.top >= rs.bottom))
        return 1;
    rd.left   = dx;
    rd.top    = dy;
    rd.right  = dx + rs.right - rs.left;
    rd.bottom = dy + rs.bottom - rs.top;
    if (mem) {
        const DWORD sent = upload_rect(&gd, &rd, &gs, mem, &rs, &ok);

        DIAG(diag.texblt_bytes += sent);
        return ok;
    }
    /* DXT copies are of whole blocks, and both rectangles stay the same
       size (they share their alignment in practice: whole levels). */
    surf_blocks(&gs, &rs);
    surf_blocks(&gd, &rd);
    if ((rs.right - rs.left != rd.right - rd.left) || (rs.bottom - rs.top != rd.bottom - rd.top))
        return 1;
    DIAG(diag.texblt_copies++);
    return blt_packet(&gs, &rs, &gd, &rd);
}

/* One level: rectangle `rs` of `src` to (dx, dy) in `dst`. */
static int
texblt_level(LPSURF dst, LPSURF src, LONG dx, LONG dy, RECTL rs)
{
    const uint8_t *mem = NULL;
    const int      sys = (src->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) != 0;
    gsurf_t        gd;
    gsurf_t        gs;

    if (!surf_describe(dst, &gd))
        return 1;
    if (sys ? !surf_describe_sys(src, &gs, &mem) : !surf_describe(src, &gs))
        return 1;
    return texblt_rect(&gd, &gs, sys ? mem : NULL, dx, dy, rs);
}

/* A texture's levels from the one `skip` below the top. */
static LPSURF
level_at(LPSURF s, DWORD skip)
{
    while (s && skip--)
        s = tex_mip_next(s);
    return s;
}

/* A level's size: the top's halved, never below 1. */
static DWORD
level_size(DWORD top, DWORD level)
{
    return (top >> level) ? top >> level : 1;
}

/* TEXBLT (spec/directx-hal.md): every level the two textures share, the
   rectangle and point halved at each. The rectangle is in the source's top
   level, the point in the destination's; when the destination's top is
   smaller, the source's levels start at the one of its size. For cube maps
   the faces dwFlags names (all six when none): one face as a texture, two
   or more whole. A destination of 0 asks to preload a texture the driver
   manages: none here. The batch's fence goes to the destination (and a
   source in video memory) with all its levels and faces. 0 if the ring
   stopped. */
int
d3d7_texblt(d3d_ctx_t *c, DWORD hdst, DWORD hsrc, LONG dx, LONG dy, const RECTL *rsrc, DWORD flags)
{
    LPSURF dst;
    LPSURF src;
    DWORD  skip = 0;
    int    cube;
    int    whole;
    DWORD  face;

    if (!hdst) {
        DIAG(diag.texblt_preloads++);
        return 1;
    }
    GUARD_STEP(GUARD_STEP_BLT, hsrc, hdst);
    dst = d3d7_surface(c->ddl, hdst);
    src = d3d7_surface(c->ddl, hsrc);
    if (!dst || !src) {
        /* A texture whose handle names nothing. */
        DIAG(diag.texblts_refused++);
        if (TRACE(TRACE_LOOKUP))
            dbg(DLL_TAG " trace: TEXBLT %x to %x under %x: the source %s, the destination %s", hsrc, hdst, (DWORD) c->ddl,
                src ? "found" : "not found", dst ? "found" : "not found");
        return 1;
    }
    DIAG(diag.texblts++);
    ctx_batch_surface(dst);
    if (!(src->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
        ctx_batch_surface(src);
    while ((skip < VGPU98_TEX_MAX_LEVELS) && ((level_size(src->lpGbl->wWidth, skip) > dst->lpGbl->wWidth) ||
                                              (level_size(src->lpGbl->wHeight, skip) > dst->lpGbl->wHeight)))
        skip++;
    cube = (caps2(dst) & DDSCAPS2_CUBEMAP) != 0;
    flags &= DDSCAPS2_CUBEMAP_ALLFACES;
    if (cube && !flags)
        flags = DDSCAPS2_CUBEMAP_ALLFACES;
    /* One face: a rectangle of it; more: whole faces. */
    whole = cube && (flags & (flags - 1));
    for (face = 0; face < (cube ? (DWORD) VGPU98_CUBE_FACES : 1u); face++) {
        const DWORD bit = DDSCAPS2_CUBEMAP_POSITIVEX << face;
        LPSURF      d   = dst;
        LPSURF      s   = src;
        RECTL       r   = *rsrc;
        LONG        x   = dx;
        LONG        y   = dy;
        DWORD       level;

        if (cube) {
            if (!(flags & bit))
                continue;
            d = cube_face(dst, bit);
            s = cube_face(src, bit);
            if (!d || !s)
                continue;
        }
        s = level_at(s, skip);
        if (!s)
            continue;
        if (whole) {
            x = y  = 0;
            r.left = r.top = 0;
            r.right        = (LONG) s->lpGbl->wWidth;
            r.bottom       = (LONG) s->lpGbl->wHeight;
        } else {
            r.left >>= skip;
            r.top >>= skip;
            r.right  = (r.right + (1 << skip) - 1) >> skip;
            r.bottom = (r.bottom + (1 << skip) - 1) >> skip;
        }
        for (level = 0; d && s && (level < VGPU98_TEX_MAX_LEVELS); level++) {
            if (!texblt_level(d, s, x, y, r))
                return 0;
            d = tex_mip_next(d);
            s = tex_mip_next(s);
            x /= 2;
            y /= 2;
            r.left /= 2;
            r.top /= 2;
            r.right  = (r.right + 1) / 2;
            r.bottom = (r.bottom + 1) / 2;
        }
    }
    return 1;
}

/* --- VOLUMEBLT (DirectX 8) ------------------------------------------------- */

/* A volume level's depth (the top's, halved) and slice pitch: what
   CreateSurfaceEx gave a video memory level, what DirectDraw's emulation
   gave a system memory one, never less than its rows. */
static DWORD
volume_slice(LPSURF s, const gsurf_t *g)
{
    const DWORD rows = g->pitch * g->height;

    return (s->lpGbl->dwBlockSizeY >= rows) ? s->lpGbl->dwBlockSizeY : rows;
}

static DWORD
volume_depth8(LPSURF s)
{
    const DWORD d = s->lpSurfMore ? (s->lpSurfMore->ddsCapsEx.dwCaps4 & 0xffff) : 0;

    return d ? d : 1;
}

/* VOLUMEBLT (spec/directx-hal.md): the box of the source volume to (dx, dy,
   dz) in the destination, for every level the two share, the box and point
   halved at each; each slice as TEXBLT copies a rectangle. The box is in the
   source's top level, the point in the destination's; when the
   destination's top is smaller, the source's levels start at the one of its
   size (as TEXBLT's). The batch's fence goes to the destination (and a
   source in video memory) with all their levels. 0 if the ring stopped. */
int
d3d7_volblt(d3d_ctx_t *c, DWORD hdst, DWORD hsrc, LONG dx, LONG dy, LONG dz, const DWORD *box)
{
    LPSURF dst;
    LPSURF src;
    DWORD  ddepth;
    DWORD  sdepth;
    LONG   front = (LONG) box[4];
    LONG   back  = (LONG) box[5];
    DWORD  skip  = 0;
    RECTL  r;
    DWORD  level;

    GUARD_STEP(GUARD_STEP_BLT, hsrc, hdst);
    dst = d3d7_surface(c->ddl, hdst);
    src = d3d7_surface(c->ddl, hsrc);
    if (!dst || !src || !(caps2(dst) & DDSCAPS2_VOLUME) || !(caps2(src) & DDSCAPS2_VOLUME)) {
        DIAG(diag.volblts_refused++);
        if (TRACE(TRACE_LOOKUP))
            dbg(DLL_TAG " trace: VOLUMEBLT %x to %x under %x: the source %s, the destination %s", hsrc, hdst, (DWORD) c->ddl,
                !src ? "not found" : (caps2(src) & DDSCAPS2_VOLUME) ? "found" : "no volume",
                !dst ? "not found" : (caps2(dst) & DDSCAPS2_VOLUME) ? "found" : "no volume");
        return 1;
    }
    DIAG(diag.volblts++);
    ctx_batch_surface(dst);
    if (!(src->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
        ctx_batch_surface(src);
    ddepth = volume_depth8(dst);
    sdepth = volume_depth8(src);
    while ((skip < VGPU98_TEX_VOLUME_LEVELS) && ((level_size(src->lpGbl->wWidth, skip) > dst->lpGbl->wWidth) ||
                                                 (level_size(src->lpGbl->wHeight, skip) > dst->lpGbl->wHeight) ||
                                                 (level_size(sdepth, skip) > ddepth)))
        skip++;
    src = level_at(src, skip);
    if (!src)
        return 1;
    r.left   = (LONG) box[0] >> skip;
    r.top    = (LONG) box[1] >> skip;
    r.right  = ((LONG) box[2] + (1 << skip) - 1) >> skip;
    r.bottom = ((LONG) box[3] + (1 << skip) - 1) >> skip;
    front >>= skip;
    back = (back + (1 << skip) - 1) >> skip;
    for (level = 0; dst && src && (level < VGPU98_TEX_VOLUME_LEVELS); level++) {
        const int      sys = (src->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) != 0;
        const uint8_t *mem = NULL;
        const LONG     dd  = (LONG) level_size(ddepth, level);
        const LONG     sd  = (LONG) level_size(sdepth, skip + level);
        gsurf_t        gd;
        gsurf_t        gs;
        LONG           z;

        if (!(dst->dwReserved1 & SURF_SLICES) || !surf_describe(dst, &gd) ||
            (sys ? !surf_describe_sys(src, &gs, &mem) : (!(src->dwReserved1 & SURF_SLICES) || !surf_describe(src, &gs))))
            break;
        for (z = (front < 0) ? 0 : front; (z < back) && (z < sd); z++) {
            const LONG to = dz + (z - front);
            gsurf_t    ds = gd;
            gsurf_t    ss = gs;

            if ((to < 0) || (to >= dd))
                continue;
            ds.offset += (DWORD) to * volume_slice(dst, &gd);
            if (sys) {
                if (!texblt_rect(&ds, &ss, mem + (DWORD) z * volume_slice(src, &gs), dx, dy, r))
                    return 0;
            } else {
                ss.offset += (DWORD) z * volume_slice(src, &gs);
                if (!texblt_rect(&ds, &ss, NULL, dx, dy, r))
                    return 0;
            }
        }
        dst = tex_mip_next(dst);
        src = tex_mip_next(src);
        dx /= 2;
        dy /= 2;
        dz /= 2;
        front /= 2;
        back = (back + 1) / 2;
        r.left /= 2;
        r.top /= 2;
        r.right  = (r.right + 1) / 2;
        r.bottom = (r.bottom + 1) / 2;
    }
    return 1;
}
