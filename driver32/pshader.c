/*
 * vgpu98dx.dll: a DirectX 8 context's pixel shaders (spec/directx-hal.md,
 * Shaders): each kept (its tokens in the heap, without their comments) and,
 * with the GPU's PROFILE.PROGRAMS (command set 2.2), translated into the
 * GPU's program (xlate.c, prog.c), which draws it; its def constants load at
 * every SETPIXELSHADER.
 *
 * Shader capture: with the card's "DirectX 8
 * shaders: Capture" values and the log on at a full level (not Minimal),
 * each distinct shader is logged once a session disassembled and named at
 * its first draw with what it
 * draws with (the stages' textures and coordinates, the constants it reads,
 * the blend, alpha test and fog).
 *
 * The token format is the DirectX 8 DDK's and SDK's (d3d8types.h): a
 * version token (ps.1.n FFFF010nh), instruction tokens (the opcode in the
 * low word, bit 30 co-issue, bit 31 clear), each followed by its parameter
 * tokens (bit 31 set: the register number in bits 0-12, its file in bits
 * 28-30, a destination's write mask in bits 16-19, its saturation in bits
 * 20-23 and its shift in bits 24-27; a source's swizzle in bits 16-23 and
 * its modifier in bits 24-27); DEF's four values are raw floats; comments
 * (FFFEh, their length in dwords in bits 16-30) and END (FFFFh). Pixel
 * shaders 1.4 call opcodes 66 and 64 texld and texcrd (two parameters).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"
#include "xlate.h"

/* Register files (D3DSPR_*) */
#define PSR_TEMP    0
#define PSR_INPUT   1
#define PSR_CONST   2
#define PSR_TEXTURE 3

/* Opcodes (D3DSIO_*) */
#define PSO_MOV      1
#define PSO_ADD      2
#define PSO_MUL      5
#define PSO_TEXCOORD 64
#define PSO_TEX      66
#define PSO_TEXBEM   67
#define PSO_TEXBEML  68
#define PSO_DEF      81
#define PSO_BEM      89
#define PSO_PHASE    0xFFFD
#define PSO_COMMENT  0xFFFE
#define PSO_END      0xFFFF

#define PS_PARAM   0x80000000u
#define PS_COISSUE 0x40000000u

#define PS_LOGGED_MAX  128  /* distinct shaders a session's log names */
#define PS_LINES_MAX   128  /* instruction lines a shader's log takes at most (a vertex shader 1.1 has 128) */
#define PS_DRAWN_MAX   256  /* shaders named at their first draw, a session */
#define PS_REFUSED_MAX 16   /* refused shaders named, a session */
#define PS_GONE_MAX    64   /* named shaders' draws when their handle goes, a session */
#define PROG_LINES_MAX 256  /* a GPU program's lines (the capture of programs) */
#define PS_TOKENS_MAX  1024 /* a shader's tokens at most, comments left out */
#define PS_CTX_MAX     512  /* shaders a context keeps at most, capture alone */

/* Shared by every process (the DLL's data is): their hashes and counts,
   claimed with an atomic add. */
static DWORD ps_logged[PS_LOGGED_MAX];
static DWORD ps_n_logged;
static DWORD ps_n_drawn;
static DWORD ps_n_refused;
static DWORD ps_n_gone; /* the "handle gone" lines */

/* --- The disassembly ------------------------------------------------------------ */

static const struct {
    DWORD       op;
    const char *name;
} ps_names[] = {
    { 0, "nop" },           { 1, "mov" },            { 2, "add" },           { 3, "sub" },
    { 4, "mad" },           { 5, "mul" },            { 6, "rcp" },           { 7, "rsq" },
    { 8, "dp3" },           { 9, "dp4" },            { 10, "min" },          { 11, "max" },
    { 12, "slt" },          { 13, "sge" },           { 14, "exp" },          { 15, "log" },
    { 16, "lit" },          { 17, "dst" },           { 18, "lrp" },          { 19, "frc" },
    { 20, "m4x4" },         { 21, "m4x3" },          { 22, "m3x4" },         { 23, "m3x3" },
    { 24, "m3x2" },         { 64, "texcoord" },      { 65, "texkill" },      { 66, "tex" },
    { 67, "texbem" },       { 68, "texbeml" },       { 69, "texreg2ar" },    { 70, "texreg2gb" },
    { 71, "texm3x2pad" },   { 72, "texm3x2tex" },    { 73, "texm3x3pad" },   { 74, "texm3x3tex" },
    { 75, "texm3x3diff" },  { 76, "texm3x3spec" },   { 77, "texm3x3vspec" }, { 78, "expp" },
    { 79, "logp" },         { 80, "cnd" },           { 81, "def" },          { 82, "texreg2rgb" },
    { 83, "texdp3tex" },    { 84, "texm3x2depth" },  { 85, "texdp3" },       { 86, "texm3x3" },
    { 87, "texdepth" },     { 88, "cmp" },           { 89, "bem" },          { PSO_PHASE, "phase" },
};

static char *
ps_str(char *p, char *end, const char *s)
{
    while (*s && (p < end))
        *p++ = *s++;
    return p;
}

static char *
ps_num(char *p, char *end, DWORD v)
{
    char  t[12];
    DWORD n = 0;

    do {
        t[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v && (n < sizeof(t)));
    while (n && (p < end))
        *p++ = t[--n];
    return p;
}

static char *
ps_float(char *p, char *end, DWORD bits)
{
    union {
        DWORD u;
        float f;
    } v;
    char f[24];

    v.u = bits;
    return ps_str(p, end, fmt_float(f, v.f));
}

/* A register: a pixel shader's r, v, c, t; a vertex shader's r, v, c (or
   c[a0.x+n]), a, oPos, oFog, oPts, oDn, oTn. */
static char *
ps_reg(char *p, char *end, DWORD tok, int vertex)
{
    static const char        pfiles[8] = { 'r', 'v', 'c', 't', '?', '?', '?', '?' };
    static const char *const vfiles[8] = { "r", "v", "c", "a", "o", "oD", "oT", "?" };
    static const char *const rast[3]   = { "oPos", "oFog", "oPts" };
    const DWORD              type      = (tok >> 28) & 7;
    const DWORD              reg       = tok & 0x1FFF;
    char                     f[2]      = { 0, 0 };

    if (!vertex) {
        f[0] = pfiles[type];
        p    = ps_str(p, end, f);
        return ps_num(p, end, reg);
    }
    if ((type == 2) && (tok & 0x2000u)) {
        p = ps_str(p, end, "c[a0.x+");
        p = ps_num(p, end, reg);
        return ps_str(p, end, "]");
    }
    if (type == 4)
        return ps_str(p, end, (reg < 3) ? rast[reg] : "o?");
    p = ps_str(p, end, vfiles[type]);
    return ps_num(p, end, reg);
}

static char *
ps_dst(char *p, char *end, DWORD tok, int vertex)
{
    const char *const comp = vertex ? "xyzw" : "rgba";
    const DWORD       mask = (tok >> 16) & 15;
    DWORD             k;

    p = ps_reg(p, end, tok, vertex);
    if (mask != 15) {
        p = ps_str(p, end, ".");
        for (k = 0; k < 4; k++) {
            if ((mask >> k) & 1) {
                char c[2] = { comp[k], 0 };

                p = ps_str(p, end, c);
            }
        }
    }
    return p;
}

static char *
ps_src(char *p, char *end, DWORD tok, int vertex)
{
    const char *const comp = vertex ? "xyzw" : "rgba";
    const DWORD       mod  = (tok >> 24) & 15;
    const DWORD       sw   = (tok >> 16) & 0xFF;
    DWORD             k;

    if ((mod == 1) || (mod == 3) || (mod == 5) || (mod == 8))
        p = ps_str(p, end, "-");
    else if (mod == 6)
        p = ps_str(p, end, "1-");
    p = ps_reg(p, end, tok, vertex);
    if ((mod == 2) || (mod == 3))
        p = ps_str(p, end, "_bias");
    else if ((mod == 4) || (mod == 5))
        p = ps_str(p, end, "_bx2");
    else if ((mod == 7) || (mod == 8))
        p = ps_str(p, end, "_x2");
    else if (mod == 9)
        p = ps_str(p, end, "_dz");
    else if (mod == 10)
        p = ps_str(p, end, "_dw");
    else if (mod > 10)
        p = ps_str(p, end, "_?"); /* reserved: a malformed token */
    if (sw != 0xE4) {
        /* A replicated component, or the four in their order. */
        const int same = ((sw & 3) == ((sw >> 2) & 3)) && ((sw & 3) == ((sw >> 4) & 3)) && ((sw & 3) == ((sw >> 6) & 3));

        p = ps_str(p, end, ".");
        for (k = 0; k < (same ? 1u : 4u); k++) {
            char c[2] = { comp[(sw >> (2 * k)) & 3], 0 };

            p = ps_str(p, end, c);
        }
    }
    return p;
}

static const char *
ps_name(DWORD op, int v14)
{
    DWORD i;

    if (v14 && (op == PSO_TEX))
        return "texld";
    if (v14 && (op == PSO_TEXCOORD))
        return "texcrd";
    for (i = 0; i < ARRAY_SIZE(ps_names); i++) {
        if (ps_names[i].op == op)
            return ps_names[i].name;
    }
    return NULL;
}

/* One instruction (at tok[0], `np` parameters after it) as text; `v14`:
   a pixel shader 1.4's, `vertex`: a vertex shader's. */
static void
ps_text(char *buf, DWORD size, const DWORD *tok, DWORD np, int v14, int vertex)
{
    char       *p   = buf;
    char       *end = buf + size - 1;
    const DWORD op  = tok[0] & 0xFFFF;
    const char *nm  = ps_name(op, v14);
    DWORD       k;

    if (tok[0] & PS_COISSUE)
        p = ps_str(p, end, "+");
    if (nm)
        p = ps_str(p, end, nm);
    else {
        p = ps_str(p, end, "op");
        p = ps_num(p, end, op);
    }
    if ((op == PSO_DEF) && (np == 5)) {
        p = ps_str(p, end, " ");
        p = ps_reg(p, end, tok[1], vertex);
        for (k = 2; k < 6; k++) {
            p = ps_str(p, end, ", ");
            p = ps_float(p, end, tok[k]);
        }
        *p = 0;
        return;
    }
    if (np) {
        /* The destination's shift and saturation go with the opcode
           (shift 4-12 is reserved). */
        static const char *const shift[16] = { "",   "_x2", "_x4", "_x8", "_?", "_?",  "_?",  "_?",
                                               "_?", "_?",  "_?",  "_?",  "_?", "_d8", "_d4", "_d2" };
        const DWORD              sat       = (tok[1] >> 20) & 15;

        p = ps_str(p, end, shift[(tok[1] >> 24) & 15]);
        if (sat == 1)
            p = ps_str(p, end, "_sat");
        else if (sat)
            p = ps_str(p, end, "_?");
        p = ps_str(p, end, " ");
        p = ps_dst(p, end, tok[1], vertex);
        for (k = 2; k <= np; k++) {
            p = ps_str(p, end, ", ");
            p = ps_src(p, end, tok[k], vertex);
        }
    }
    *p = 0;
}

/* --- The GPU's programs as text (shader capture with its programs) ----------- */

static const char *
prog_op_name(DWORD op)
{
    static const char *const a[0x19] = { "?",   "MOV", "ADD", "SUB",  "MUL",  "MAD", "DP3", "DP4", "LRP",
                                         "CND", "CMP", "MIN", "MAX",  "SLT",  "SGE", "RCP", "RSQ", "EXP",
                                         "LOG", "EXPP", "LOGP", "FRC", "LIT", "DST", "MOVA" };
    static const char *const b[6]    = { "SAMPLE", "BEM", "LUM", "RFL", "KILL", "DEPTH" };

    if (op <= VGPU98_PI_MOVA)
        return a[op];
    if ((op >= VGPU98_PI_SAMPLE) && (op <= VGPU98_PI_DEPTH))
        return b[op - VGPU98_PI_SAMPLE];
    return "?";
}

/* A file and register: r TEMP, v INPUT, c CONST, a ADDR, t COORD, o OUTPUT,
   s STAGE, f FIXED. */
static char *
prog_reg(char *p, char *end, DWORD file, DWORD reg)
{
    static const char files[9] = { '-', 'r', 'v', 'c', 'a', 't', 'o', 's', 'f' };
    char              f[2]     = { (file < 9) ? files[file] : '?', 0 };

    p = ps_str(p, end, f);
    return ps_num(p, end, reg);
}

static char *
prog_src(char *p, char *end, DWORD s)
{
    static const char *const mods[11] = { "", "-", "_bias", "-_bias", "_bx2", "-_bx2", "1-", "_x2", "-_x2", "_dz", "_dw" };
    const DWORD              mod      = VGPU98_SRC_MOD(s);
    const DWORD              sw       = VGPU98_SRC_SWZ(s);
    DWORD                    k;

    if ((mod == 1) || (mod == 3) || (mod == 5) || (mod == 8))
        p = ps_str(p, end, "-");
    else if (mod == 6)
        p = ps_str(p, end, "1-");
    p = prog_reg(p, end, VGPU98_SRC_FILE(s), VGPU98_SRC_REG(s));
    if (s & VGPU98_SRC_RELATIVE)
        p = ps_str(p, end, "[a0]");
    if ((mod == 2) || (mod == 3) || (mod == 4) || (mod == 5) || (mod == 7) || (mod == 8) || (mod == 9) || (mod == 10))
        p = ps_str(p, end, mods[mod] + ((mods[mod][0] == '-') ? 1 : 0));
    if (sw != VGPU98_SWZ_XYZW) {
        p = ps_str(p, end, ".");
        for (k = 0; k < 4; k++) {
            char c[2] = { "xyzw"[(sw >> (2 * k)) & 3], 0 };

            p = ps_str(p, end, c);
        }
    }
    return p;
}

void
d3d8_prog_log(DWORD hash, const d3d_prog_t *g)
{
    DWORD k;

    for (k = 0; (k < g->n) && (k < PROG_LINES_MAX); k++) {
        static const char *const shifts[8] = { "", "_x2", "_x4", "_x8", "_?", "_d8", "_d4", "_d2" };
        const DWORD             *in        = g->ins + VGPU98_PROG_INS_DWORDS * k;
        const DWORD              d         = in[0];
        const DWORD              desc      = vgpu98_prog_op(VGPU98_INS_OP(d));
        const DWORD              nsrc      = (desc >> 2) & 3;
        char                     line[112];
        char                    *p   = line;
        char                    *end = line + sizeof(line) - 1;
        DWORD                    i;

        p = ps_str(p, end, prog_op_name(VGPU98_INS_OP(d)));
        p = ps_str(p, end, shifts[(d >> 25) & 7]);
        if (d & VGPU98_INS_SAT)
            p = ps_str(p, end, "_sat");
        if (d & VGPU98_INS_PROJECT)
            p = ps_str(p, end, "_proj");
        if (d & VGPU98_INS_NOCLAMP)
            p = ps_str(p, end, "_nc");
        p = ps_str(p, end, " ");
        if (((desc >> 6) & 3) != 2) {
            p = prog_reg(p, end, VGPU98_INS_DFILE(d), VGPU98_INS_DREG(d));
            if (VGPU98_INS_MASK(d) != VGPU98_WM_ALL) {
                p = ps_str(p, end, ".");
                for (i = 0; i < 4; i++) {
                    char c[2] = { "xyzw"[i], 0 };

                    if ((VGPU98_INS_MASK(d) >> i) & 1)
                        p = ps_str(p, end, c);
                }
            }
            p = ps_str(p, end, ", ");
        }
        for (i = 0; i < nsrc; i++) {
            if (i)
                p = ps_str(p, end, ", ");
            p = prog_src(p, end, in[1 + i]);
        }
        *p = 0;
        dbg(DLL_TAG " shader %x program: %s", hash, line);
    }
    if (k < g->n)
        dbg(DLL_TAG " shader %x program: (more not shown)", hash);
}

/* The parameters after the instruction at tok[i] (DEF: a register and
   four raw values; others: the tokens with bit 31 set). */
static DWORD
ps_params(const DWORD *tok, DWORD n, DWORD i)
{
    DWORD j;

    if ((tok[i] & 0xFFFF) == PSO_DEF)
        return (i + 5 < n) ? 5 : n - i - 1;
    for (j = i + 1; (j < n) && (tok[j] & PS_PARAM); j++)
        ;
    return j - i - 1;
}

/* The next instruction from tok[i]: comments stepped over; n at END or
   past the end. */
static DWORD
ps_next(const DWORD *tok, DWORD n, DWORD i)
{
    while (i < n) {
        const DWORD op = tok[i] & 0xFFFF;

        if (op == PSO_END)
            return n;
        if (op != PSO_COMMENT)
            return i;
        i += 1 + ((tok[i] >> 16) & 0x7FFF);
    }
    return n;
}

/* --- What a shader reads ---------------------------------------------------------- */

static DWORD
ps_file(DWORD tok)
{
    return (tok >> 28) & 7;
}

/* The registers it reads (constants c0-c7, textures t0-t7), the textures
   it samples (tex/texld and the texture-addressing opcodes), whether it
   bumps (texbem, texbeml), and the constants DEF defines. */
static void
ps_scan(d3d_ps_t *p)
{
    const DWORD *tok = p->tokens;
    const DWORD  n   = p->n_tokens;
    DWORD        i;
    DWORD        k;

    for (i = ps_next(tok, n, 1); i < n; i = ps_next(tok, n, i + 1 + ps_params(tok, n, i))) {
        const DWORD op = tok[i] & 0xFFFF;
        const DWORD np = ps_params(tok, n, i);

        if ((op == PSO_DEF) && (np == 5)) {
            const DWORD r = tok[i + 1] & 0x1FFF;

            if (r < D3D_PS_CONSTS) {
                p->def_mask |= 1u << r;
                for (k = 0; k < 4; k++)
                    p->def_val[r][k] = tok[i + 2 + k];
            }
            continue;
        }
        if (op == PSO_PHASE)
            continue;
        if ((op != PSO_DEF) && (op != PSO_PHASE))
            p->n_instr++;
        /* A texture opcode's destination is its stage (1.4: its source
           names the coordinates; the stage is the destination's number). */
        if ((((op >= PSO_TEX) && (op <= 77)) || ((op >= 82) && (op <= 86))) && np &&
            ((ps_file(tok[i + 1]) == PSR_TEXTURE) || ((p->version & 0xFF) >= 4)) && ((tok[i + 1] & 0x1FFF) < 8))
            p->tsampled |= 1u << (tok[i + 1] & 0x1FFF);
        if (((op == PSO_TEXBEM) || (op == PSO_TEXBEML) || (op == PSO_BEM)) && np && ((tok[i + 1] & 0x1FFF) < 8))
            p->bump |= 1u << (tok[i + 1] & 0x1FFF);
        for (k = 2; k <= np; k++) {
            const DWORD r = tok[i + k] & 0x1FFF;

            if ((ps_file(tok[i + k]) == PSR_CONST) && (r < D3D_PS_CONSTS))
                p->cread |= 1u << r;
            else if ((ps_file(tok[i + k]) == PSR_TEXTURE) && (r < 8))
                p->tread |= 1u << r;
        }
    }
}

/* --- The shaders ---------------------------------------------------------------------- */

d3d_ps_t *
d3d8_ps_find(const d3d_ctx_t *c, DWORD handle)
{
    DWORD i;

    for (i = 0; i < c->n_pshaders; i++) {
        if (c->pshaders[i].handle == handle)
            return &c->pshaders[i];
    }
    return NULL;
}

/* Every stage looked at again by the next flush: a pixel program's stages
   (its textures, whatever their COLOROP: d3d8_ps_stages), or the fixed
   function's. */
static void
ps_stages_again(d3d_ctx_t *c)
{
    c->dirty |= DIRTY_STAGES;
    memset(c->tss_dirty, 0xFF, sizeof(c->tss_dirty));
    c->clean = 0;
}

/* The bound shader, or none, into the context. */
static void
ps_take(d3d_ctx_t *c)
{
    const int on = c->ps && d3d8_ps_find(c, c->ps);

    if (on || c->ps_on)
        ps_stages_again(c);
    c->ps_on = on;
}

/* A shared counter's next value, or ~0 past `max`. */
static DWORD
ps_claim(DWORD *counter, DWORD max)
{
    const DWORD i = __atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);

    return (i < max) ? i : ~0u;
}

static int
ps_was_logged(DWORD hash)
{
    const DWORD n = __atomic_load_n(&ps_n_logged, __ATOMIC_RELAXED);
    DWORD       i;

    for (i = 0; (i < n) && (i < PS_LOGGED_MAX); i++) {
        if (ps_logged[i] == hash)
            return 1;
    }
    return 0;
}

int
d3d8_shader_was_logged(DWORD hash)
{
    return ps_was_logged(hash);
}

int
d3d8_shader_once(DWORD hash)
{
    DWORD slot;

    if (!diag_on || ps_was_logged(hash) || ((slot = ps_claim(&ps_n_logged, PS_LOGGED_MAX)) == ~0u))
        return 0;
    ps_logged[slot] = hash;
    return 1;
}

int
d3d8_shader_gone_line(DWORD hash)
{
    return diag_on && ps_was_logged(hash) && (ps_claim(&ps_n_gone, PS_GONE_MAX) != ~0u);
}

void
d3d8_shader_text(DWORD hash, const DWORD *tok, DWORD n, int vertex)
{
    const int v14   = !vertex && n && ((tok[0] & 0xFF) >= 4);
    DWORD     lines = 0;
    DWORD     i;

    for (i = ps_next(tok, n, 1); (i < n) && (lines < PS_LINES_MAX); i = ps_next(tok, n, i + 1 + ps_params(tok, n, i))) {
        char text[112];

        ps_text(text, sizeof(text), tok + i, ps_params(tok, n, i), v14, vertex);
        dbg(DLL_TAG " shader %x: %s", hash, text);
        lines++;
    }
    if (i < n)
        dbg(DLL_TAG " shader %x: (more not shown)", hash);
}

/* Each distinct shader once a session: a line at any Diagnostics level but
   Minimal; its instructions with shader capture, and its program with the
   capture of programs too (the setting's value 2). */
static void
ps_log(const d3d_ctx_t *c, const d3d_ps_t *p)
{
    if (!d3d8_shader_once(p->hash))
        return;
    dbg(DLL_TAG " shader: context %u handle %x: ps.%u.%u, %u instructions, hash %x; its program %u instructions, stages %x",
        ctx_handle(c), p->handle, (p->version >> 8) & 0xFF, p->version & 0xFF, p->n_instr, p->hash, p->prog.n, p->prog.stages);
    if (diag_shaders)
        d3d8_shader_text(p->hash, p->tokens, p->n_tokens, 0);
    if (diag_shaders == 2)
        d3d8_prog_log(p->hash, &p->prog);
}

void
d3d8_shader_refused(const d3d_ctx_t *c, DWORD handle, DWORD version, const char *why, DWORD at)
{
    if (diag_on && (ps_claim(&ps_n_refused, PS_REFUSED_MAX) != ~0u))
        dbg(DLL_TAG " shader refused: context %u handle %x, %s.%u.%u: %s (token %u)", ctx_handle(c), handle,
            ((version >> 16) == 0xFFFE) ? "vs" : ((version >> 16) == 0xFFFF) ? "ps" : "?", (version >> 8) & 0xFF,
            version & 0xFF, why, at);
}

static void
ps_refuse(const d3d_ctx_t *c, DWORD handle, const uint8_t *code, DWORD bytes, const char *why, DWORD at)
{
    DIAG(diag.ps_refused++);
    d3d8_shader_refused(c, handle, (bytes >= 4) ? (code[0] | (code[1] << 8) | (code[2] << 16) | ((DWORD) code[3] << 24)) : 0, why,
                        at);
}

static void ps_gone(d3d_ps_t *p);

int
d3d8_ps_create(d3d_ctx_t *c, DWORD handle, const uint8_t *code, DWORD bytes)
{
    DWORD       n    = bytes / 4;
    d3d_ps_t   *slot = d3d8_ps_find(c, handle);
    d3d_ps_t    p;
    DWORD      *raw  = NULL;
    DWORD       h    = 0x811c9dc5u;
    DWORD       kept = 0;
    DWORD       i;

    memset(&p, 0, sizeof(p));
    p.handle = handle;
    /* The tokens kept without their comments (a shader's debug information
       may make it long), hashed as they came; capture alone keeps at most
       PS_CTX_MAX a context. */
    if ((n >= 2) && (d3d8_programs || slot || (c->n_pshaders < PS_CTX_MAX)) && (raw = heap_alloc(n * sizeof(DWORD)))) {
        for (i = 0; i < n; i++) {
            raw[i] = code[4 * i] | (code[4 * i + 1] << 8) | (code[4 * i + 2] << 16) | ((DWORD) code[4 * i + 3] << 24);
            h      = (h ^ raw[i]) * 16777619u;
        }
        kept = xl_strip(raw, n);
        if ((kept <= PS_TOKENS_MAX) && (p.tokens = heap_alloc(kept * sizeof(DWORD))))
            memcpy(p.tokens, raw, kept * sizeof(DWORD));
    }
    heap_free(raw);
    if (!p.tokens) {
        ps_refuse(c, handle, code, bytes,
                  (n < 2) ? "no tokens" : (kept > PS_TOKENS_MAX) ? "too long" : "no heap room, or too many shaders", 0);
        return 0;
    }
    n = kept;
    p.n_tokens = n;
    p.version  = p.tokens[0];
    p.hash     = h;
    ps_scan(&p);
    /* With PROFILE.PROGRAMS, the GPU's program: a shader the translator
       cannot take, or one with no id or heap left for it, is refused. */
    if (d3d8_programs) {
        xl_out_t   *o   = prog_scratch();
        const char *why = NULL;
        DWORD       at  = 0;

        if (!o)
            why = "no heap room";
        else if (!xl_pixel(p.tokens, n, d3d8_ps_range(), o)) {
            why = xl_why(o->why);
            at  = o->at;
        } else if (!prog_make(&p.prog, o, d3d8_ps_range()))
            why = "no program id or heap room";
        else
            p.n_instr = o->n_in; /* the translator's count (nop not counted) */
        heap_free(o);
        if (why) {
            heap_free(p.tokens);
            ps_refuse(c, handle, code, bytes, why, at);
            return 0;
        }
    }

    if (slot) {
        ps_gone(slot); /* its draws named, as a vertex shader's */
        *slot = p;
    } else {
        if (c->n_pshaders == c->cap_pshaders) {
            const DWORD cap = c->cap_pshaders ? 2 * c->cap_pshaders : 8;
            d3d_ps_t   *t   = heap_grow(c->pshaders, c->n_pshaders * sizeof(d3d_ps_t), cap * sizeof(d3d_ps_t));

            if (!t) {
                heap_free(p.tokens);
                prog_free(&p.prog);
                ps_refuse(c, handle, code, bytes, "no heap room", 0);
                return 0;
            }
            c->pshaders     = t;
            c->cap_pshaders = cap;
        }
        c->pshaders[c->n_pshaders++] = p;
    }
    DIAG(diag.ps_made++);
    ps_log(c, &p);
    if (c->ps == handle)
        ps_take(c);
    return 1;
}

/* A shader going: its draws, when its disassembly was logged; its program
   and tokens. */
static void
ps_gone(d3d_ps_t *p)
{
    if (p->drawn && d3d8_shader_gone_line(p->hash))
        dbg(DLL_TAG " shader %x: %u draws, the handle %x gone", p->hash, p->drawn, p->handle);
    heap_free(p->tokens);
    prog_free(&p->prog);
}

void
d3d8_ps_delete(d3d_ctx_t *c, DWORD handle)
{
    d3d_ps_t *p = d3d8_ps_find(c, handle);

    if (!p)
        return;
    DIAG(diag.ps_deleted++);
    ps_gone(p);
    *p = c->pshaders[--c->n_pshaders];
    /* The runtime may give the handle again: deleted, it is unbound. */
    if (c->ps == handle) {
        c->ps = 0;
        ps_take(c);
    }
}

/* One register's values into the context's constants, to go to the GPU. */
static void
pconst_put(d3d_ctx_t *c, DWORD reg, const DWORD *v)
{
    if (!dwords_differ(c->ps_const[reg], v, 4))
        return; /* no change: the GPU holds it, or it is marked still */
    memcpy(c->ps_const[reg], v, sizeof(c->ps_const[reg]));
    c->pconst_dirty |= 1u << reg;
}

/* SETPIXELSHADER: the DDK's rule, a shader's own constants (def) load
   whenever it is set. */
void
d3d8_ps_bind(d3d_ctx_t *c, DWORD handle)
{
    const d3d_ps_t *p = handle ? d3d8_ps_find(c, handle) : NULL;
    DWORD           r;

    for (r = 0; p && (r < D3D_PS_CONSTS); r++) {
        if ((p->def_mask >> r) & 1)
            pconst_put(c, r, p->def_val[r]);
    }
    if (handle == c->ps)
        return;
    DIAG(diag.ps_bound++);
    c->ps = handle;
    ps_take(c);
}

void
d3d8_ps_const(d3d_ctx_t *c, DWORD reg, DWORD count, const uint8_t *values)
{
    DWORD i;
    DWORD k;

    for (i = 0; (i < count) && (reg < D3D_PS_CONSTS) && (i < D3D_PS_CONSTS - reg); i++) {
        DWORD v[4];

        for (k = 0; k < 4; k++) {
            const uint8_t *b = values + 16 * i + 4 * k;

            v[k] = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD) b[3] << 24);
        }
        pconst_put(c, reg + i, v);
    }
}

DWORD
d3d8_ps_stages(const d3d_ctx_t *c)
{
    const d3d_ps_t *p;

    if (!d3d8_programs || !c->dx8 || !c->ps || !(p = d3d8_ps_find(c, c->ps)) || !p->prog.id)
        return 0;
    return p->prog.stages;
}

/* Its first draw: what it draws with, a few lines. */
static void
ps_first(d3d_ctx_t *c, const d3d_ps_t *p)
{
    static const char *const types[4] = { "2D", "cube", "volume", "?" };
    char                     line[120];
    char                    *q;
    char                    *end;
    DWORD                    s;
    DWORD                    r;
    DWORD                    k;
    DWORD                    used;

    dbg(DLL_TAG " shader: context %u handle %x (hash %x) first drawn; blend %u (%u, %u), alpha test %u (%u, %x), fog %u",
        ctx_handle(c), p->handle, p->hash, c->rs[VGPU98_RS_ALPHABLENDENABLE], c->rs[VGPU98_RS_SRCBLEND],
        c->rs[VGPU98_RS_DESTBLEND], c->rs[VGPU98_RS_ALPHATESTENABLE], c->rs[VGPU98_RS_ALPHAFUNC], c->rs[VGPU98_RS_ALPHAREF],
        c->rs[VGPU98_RS_FOGENABLE]);
    dbg(DLL_TAG " shader %x: specular %u, texture factor %x, wrap %x %x %x %x", p->hash, c->rs[VGPU98_RS_SPECULARENABLE],
        c->rs[VGPU98_RS_TEXTUREFACTOR], c->rs[VGPU98_RS_WRAP0], c->rs[VGPU98_RS_WRAP0 + 1], c->rs[VGPU98_RS_WRAP0 + 2],
        c->rs[VGPU98_RS_WRAP0 + 3]);
    /* The stages it samples or reads: their textures as the GPU was given
       them, coordinates and transforms; with texbem, the bump states. */
    used = (p->tsampled | p->tread | p->bump) & 0xFF;
    for (s = 0; s < D3D_STAGES; s++) {
        DWORD t[TEX_DESC_MAX];

        if (!((used >> s) & 1))
            continue;
        /* From its handle now: the flush builds the stages up to the last
           the program samples. */
        if ((ctx_tex_describe(c, s, t) >= 3) && (t[2] & 0xFF))
            dbg(DLL_TAG " shader %x stage %u: texture %x, %s %ux%u format %x, %u levels; coordinates %x, transform %x", p->hash, s,
                c->tss[s][D3DTSS_TEXTUREMAP], types[(t[0] >> VGPU98_TEX_TYPE_SHIFT) & 3], t[1] & 0xFFFF, t[1] >> 16, t[2] & 0xFF,
                (t[2] >> 8) & 0xFF, c->tss[s][VGPU98_TSS_TEXCOORDINDEX], c->tss[s][VGPU98_TSS_TEXTURETRANSFORMFLAGS]);
        else
            dbg(DLL_TAG " shader %x stage %u: texture %x, none usable; coordinates %x, transform %x", p->hash, s,
                c->tss[s][D3DTSS_TEXTUREMAP], c->tss[s][VGPU98_TSS_TEXCOORDINDEX],
                c->tss[s][VGPU98_TSS_TEXTURETRANSFORMFLAGS]);
        if ((p->tsampled >> s) & 1)
            dbg(DLL_TAG " shader %x stage %u: address %u/%u/%u, filters %u/%u/%u, anisotropy %u", p->hash, s,
                c->tss[s][VGPU98_TSS_ADDRESSU], c->tss[s][VGPU98_TSS_ADDRESSV], c->tss[s][VGPU98_TSS_ADDRESSW],
                c->tss[s][VGPU98_TSS_MAGFILTER], c->tss[s][VGPU98_TSS_MINFILTER], c->tss[s][VGPU98_TSS_MIPFILTER],
                c->tss[s][VGPU98_TSS_MAXANISOTROPY]);
        if ((p->bump >> s) & 1) {
            q   = line;
            end = line + sizeof(line) - 1;
            q   = ps_str(q, end, "bump matrix");
            for (k = 0; k < 4; k++) {
                q = ps_str(q, end, " ");
                q = ps_float(q, end, c->tss[s][VGPU98_TSS_BUMPENVMAT00 + k]);
            }
            q  = ps_str(q, end, ", scale ");
            q  = ps_float(q, end, c->tss[s][VGPU98_TSS_BUMPENVLSCALE]);
            q  = ps_str(q, end, ", offset ");
            q  = ps_float(q, end, c->tss[s][VGPU98_TSS_BUMPENVLOFFSET]);
            *q = 0;
            dbg(DLL_TAG " shader %x stage %u: %s", p->hash, s, line);
        }
    }
    /* The constants it reads, as it draws with them (DEF's over
       SETPIXELSHADERCONST's), two a line. */
    q   = line;
    end = line + sizeof(line) - 1;
    for (r = 0; r < D3D_PS_CONSTS; r++) {
        const DWORD *v = ((p->def_mask >> r) & 1) ? p->def_val[r] : c->ps_const[r];

        if (!((p->cread >> r) & 1))
            continue;
        if (q > line)
            q = ps_str(q, end, "; ");
        q = ps_str(q, end, "c");
        q = ps_num(q, end, r);
        q = ps_str(q, end, ((p->def_mask >> r) & 1) ? " (def)" : "");
        for (k = 0; k < 4; k++) {
            q = ps_str(q, end, " ");
            q = ps_float(q, end, v[k]);
        }
        if (q - line > 50) {
            *q = 0;
            dbg(DLL_TAG " shader %x: %s", p->hash, line);
            q = line;
        }
    }
    if (q > line) {
        *q = 0;
        dbg(DLL_TAG " shader %x: %s", p->hash, line);
    }
}

void
d3d8_ps_drawn(d3d_ctx_t *c)
{
    d3d_ps_t *p = d3d8_ps_find(c, c->ps);

    if (!p || !diag_shaders)
        return;
    if (!p->named) {
        p->named = 1;
        if (ps_claim(&ps_n_drawn, PS_DRAWN_MAX) != ~0u)
            ps_first(c, p);
    }
}

void
d3d8_ps_forget(d3d_ctx_t *c)
{
    DWORD i;

    for (i = 0; i < c->n_pshaders; i++)
        ps_gone(&c->pshaders[i]);
    heap_free(c->pshaders);
    c->pshaders     = NULL;
    c->n_pshaders   = 0;
    c->cap_pshaders = 0;
    c->ps           = 0;
    c->ps_on        = 0;
}
