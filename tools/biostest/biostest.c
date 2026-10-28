/*
 * biostest: runs the VGPU98 BIOS on the host under the Unicorn CPU emulator.
 *
 * Models just enough of a PC for a VGA BIOS: 640 KB of RAM, an interrupt table
 * whose entries point at IRET, the VGA registers and planar video memory (see
 * vga_model.c, independent of 86Box), the DAC, and the PIT/speaker ports. It
 * runs POST and INT 10h calls and checks registers, video memory and the BIOS
 * data area against the BIOS's own tables, the generated fonts and palettes,
 * and expectations written into this file.
 *
 * The card's display engine is modelled too, for the VBE tests: its registers
 * (from the specification), its own video memory behind the A0000h window, and
 * the monitor's EDID. The expected VBE modes come from the mode table.
 *
 * Usage: biostest ROM GEN_DIR MODES
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "modes.h"
#include "palettes.h"
#include "util.h"
#include "vga_model.h"
#include "vid_vgpu98_card.h"

#define STUB_POST  0x0500
#define STUB_INT10 0x0600
#define STACK_TOP  0x7000
#define IRET_ADDR  0xfff53
#define MAX_INSNS  40000000 /* a mode set clears every image page: 32M stores at 128 MB */
#define VRAM_SIZE  ((uint32_t) VGPU98_VRAM_DEFAULT_MB << 20) /* 86Box's default for the card */
#define STACK_BUDGET 256u       /* spec/bios.md: most stack an INT 10h call may use */

typedef struct regs_t {
    uint16_t ax, bx, cx, dx, si, di, bp, ds, es, fs, gs;
} regs_t;

static uc_engine  *uc;
static vga_model_t vga;
static uint8_t    *rom;
static size_t      rom_len;
static uint8_t     font8[256 * 8];
static uint8_t     font14[256 * 14];
static uint8_t     font16[256 * 16];
static char        signon[128];
static uint8_t     pal64[3][64][3];
static uint8_t     pal256[256][3];
static int         checks;
static int         failures;
static uint16_t    high_word; /* upper half of EAX-EBP on entry to emulated code */
static uint32_t    stack_low;  /* lowest stack address written since the last reset */
static uint32_t    deepest;    /* most stack any INT 10h call used, in bytes */
static uint16_t    deepest_ax;

#define CHECK(cond, ...)                \
    do {                                \
        checks++;                       \
        if (!(cond))                    \
            fail(__LINE__, __VA_ARGS__); \
    } while (0)

/* CHECK as an expression: evaluates to the condition. */
#define CHECK_RESULT(cond, ...) ((cond) ? (checks++, 1) : (checks++, fail(__LINE__, __VA_ARGS__), 0))

static void
fail(int line, const char *fmt, ...)
{
    va_list ap;

    failures++;
    fprintf(stderr, "biostest.c:%d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* --- Machine ------------------------------------------------------------- */

static uint32_t
hook_in(uc_engine *u, uint32_t port, int size, void *user)
{
    (void) u;
    (void) user;
    if (size == 1)
        return vga_in(&vga, (uint16_t) port);
    if (size == 2)
        return vga_in(&vga, (uint16_t) port) | ((uint32_t) vga_in(&vga, (uint16_t) (port + 1)) << 8);
    return 0xffffffffu;
}

static void
hook_out(uc_engine *u, uint32_t port, int size, uint32_t value, void *user)
{
    int i;

    (void) u;
    (void) user;
    for (i = 0; i < size; i++)
        vga_out(&vga, (uint16_t) (port + i), (uint8_t) (value >> (8 * i)));
}

static uint64_t
mmio_read(uc_engine *u, uint64_t offset, unsigned size, void *user)
{
    uint64_t v = 0;
    unsigned i;

    (void) u;
    (void) user;
    for (i = 0; i < size; i++)
        v |= (uint64_t) vga_mem_read(&vga, (uint32_t) (offset + i)) << (8 * i);
    return v;
}

static void
mmio_write(uc_engine *u, uint64_t offset, unsigned size, uint64_t value, void *user)
{
    unsigned i;

    (void) u;
    (void) user;
    for (i = 0; i < size; i++)
        vga_mem_write(&vga, (uint32_t) (offset + i), (uint8_t) (value >> (8 * i)));
}

static void
hook_stack(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user)
{
    (void) u;
    (void) type;
    (void) size;
    (void) value;
    (void) user;
    if (addr < stack_low)
        stack_low = (uint32_t) addr;
}

static uint8_t
rd8(uint32_t addr)
{
    uint8_t v = 0;

    uc_mem_read(uc, addr, &v, 1);
    return v;
}

static uint16_t
rd16(uint32_t addr)
{
    return (uint16_t) (rd8(addr) | (rd8(addr + 1) << 8));
}

static void
wr8(uint32_t addr, uint8_t v)
{
    uc_mem_write(uc, addr, &v, 1);
}

static void
wr16(uint32_t addr, uint16_t v)
{
    wr8(addr, (uint8_t) v);
    wr8(addr + 1, (uint8_t) (v >> 8));
}

static uint32_t
far_ptr(uint32_t addr)
{
    return ((uint32_t) rd16(addr + 2) << 4) + rd16(addr);
}

static void
setup_machine(void)
{
    uc_hook  h_in;
    uc_hook  h_out;
    uc_hook  h_stack;
    uint8_t *zero = xcalloc(1, 0xa0000);
    uint8_t  iret = 0xcf;
    int      i;

    if (uc_open(UC_ARCH_X86, UC_MODE_16, &uc) != UC_ERR_OK)
        die("cannot start Unicorn");
    if ((uc_mem_map(uc, 0, 0xa0000, UC_PROT_ALL) != UC_ERR_OK) ||
        (uc_mmio_map(uc, 0xa0000, 0x20000, mmio_read, NULL, mmio_write, NULL) != UC_ERR_OK) ||
        (uc_mem_map(uc, 0xc0000, 0x40000, UC_PROT_ALL) != UC_ERR_OK))
        die("cannot map memory");
    uc_hook_add(uc, &h_in, UC_HOOK_INSN, (void *) hook_in, NULL, 1, 0, UC_X86_INS_IN);
    uc_hook_add(uc, &h_out, UC_HOOK_INSN, (void *) hook_out, NULL, 1, 0, UC_X86_INS_OUT);
    uc_hook_add(uc, &h_stack, UC_HOOK_MEM_WRITE, (void *) hook_stack, NULL, STACK_TOP - 0x1000, STACK_TOP - 1);

    uc_mem_write(uc, 0, zero, 0xa0000);
    free(zero);
    uc_mem_write(uc, 0xc0000, rom, rom_len);
    if (uc_mem_protect(uc, 0xc0000, 0x8000, UC_PROT_READ | UC_PROT_EXEC) != UC_ERR_OK)
        die("cannot write-protect the ROM segment");
    uc_mem_write(uc, IRET_ADDR, &iret, 1);
    for (i = 0; i < 256; i++) {
        wr16((uint32_t) i * 4, (uint16_t) (IRET_ADDR & 0xffff));
        wr16((uint32_t) i * 4 + 2, 0xf000);
    }
    wr16(0x410, 0x0021); /* equipment: floppy, 80x25 colour */

    vga_reset(&vga, VRAM_SIZE);
}

static void
set_regs(const regs_t *r)
{
    int zero = 0;
    int v;

#define SETR(id, val) \
    v = (val);        \
    uc_reg_write(uc, id, &v)
#define SETR32(id, val) SETR(id, (int) (((uint32_t) high_word << 16) | (val)))
    SETR32(UC_X86_REG_EAX, r->ax);
    SETR32(UC_X86_REG_EBX, r->bx);
    SETR32(UC_X86_REG_ECX, r->cx);
    SETR32(UC_X86_REG_EDX, r->dx);
    SETR32(UC_X86_REG_ESI, r->si);
    SETR32(UC_X86_REG_EDI, r->di);
    SETR32(UC_X86_REG_EBP, r->bp);
#undef SETR32
    SETR(UC_X86_REG_DS, r->ds);
    SETR(UC_X86_REG_ES, r->es);
    SETR(UC_X86_REG_FS, r->fs);
    SETR(UC_X86_REG_GS, r->gs);
    SETR(UC_X86_REG_SP, STACK_TOP);
#undef SETR
    uc_reg_write(uc, UC_X86_REG_SS, &zero);
    uc_reg_write(uc, UC_X86_REG_CS, &zero);
}

static uint16_t
get_reg(int id)
{
    uint32_t v = 0;

    uc_reg_read(uc, id, &v);
    return (uint16_t) v;
}

/* 1 if the upper halves of EAX-EBP are all `high` after emulated code ran. */
static int
high_halves_are(uint16_t high)
{
    static const int ids[] = { UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP };
    size_t           i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        uint32_t v = 0;

        uc_reg_read(uc, ids[i], &v);
        if ((v >> 16) != high)
            return 0;
    }
    return 1;
}

static void
get_regs(regs_t *r)
{
    r->ax = get_reg(UC_X86_REG_AX);
    r->bx = get_reg(UC_X86_REG_BX);
    r->cx = get_reg(UC_X86_REG_CX);
    r->dx = get_reg(UC_X86_REG_DX);
    r->si = get_reg(UC_X86_REG_SI);
    r->di = get_reg(UC_X86_REG_DI);
    r->bp = get_reg(UC_X86_REG_BP);
    r->ds = get_reg(UC_X86_REG_DS);
    r->es = get_reg(UC_X86_REG_ES);
    r->fs = get_reg(UC_X86_REG_FS);
    r->gs = get_reg(UC_X86_REG_GS);
}

/* Runs the stub at `start` (CS = 0) until it reaches start + len. Returns 1 if it did. */
static int
run(uint32_t start, uint32_t len, regs_t *r, const char *what)
{
    uc_err err;

    set_regs(r);
    err = uc_emu_start(uc, start, start + len, 0, MAX_INSNS);
    get_regs(r);
    if (err != UC_ERR_OK) {
        fail(__LINE__, "%s: emulation error: %s at %04X:%04X", what, uc_strerror(err), get_reg(UC_X86_REG_CS),
             get_reg(UC_X86_REG_IP));
        return 0;
    }
    if ((get_reg(UC_X86_REG_IP) != start + len) || (get_reg(UC_X86_REG_CS) != 0)) {
        fail(__LINE__, "%s: did not return (stopped at %04X:%04X)", what, get_reg(UC_X86_REG_CS),
             get_reg(UC_X86_REG_IP));
        return 0;
    }
    if (get_reg(UC_X86_REG_SP) != STACK_TOP) {
        fail(__LINE__, "%s: stack pointer %04X after return, expected %04X", what, get_reg(UC_X86_REG_SP), STACK_TOP);
        return 0;
    }
    return 1;
}

static int
int10(regs_t *r)
{
    /* PUSHF + CALL FAR [CS:0040h] (CS = 0): what INT 10h does. Unicorn does not dispatch INT
       instructions through the guest interrupt table, so the test does it. */
    static const uint8_t stub[6] = { 0x9c, 0x2e, 0xff, 0x1e, 0x40, 0x00 };
    char                 what[32];

    uint16_t             ax = r->ax;
    int                  ok;

    snprintf(what, sizeof(what), "INT 10h AX=%04X", r->ax);
    uc_mem_write(uc, STUB_INT10, stub, sizeof(stub));
    stack_low = STACK_TOP;
    ok        = run(STUB_INT10, sizeof(stub), r, what);
    /* The caller's stack, less the 6 bytes INT 10h itself pushes. */
    if (STACK_TOP - stack_low - 6 > deepest) {
        deepest    = STACK_TOP - stack_low - 6;
        deepest_ax = ax;
    }
    return ok;
}

static regs_t
R(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx)
{
    regs_t r;

    memset(&r, 0, sizeof(r));
    r.ax = ax;
    r.bx = bx;
    r.cx = cx;
    r.dx = dx;
    r.si = 0x5151;
    r.di = 0xd1d1;
    r.bp = 0xb0b0;
    r.ds = 0x1234;
    r.es = 0x2345;
    r.fs = 0x3456;
    r.gs = 0x4567;
    return r;
}

static void
set_mode(uint8_t mode)
{
    regs_t r = R(mode, 0, 0, 0);

    int10(&r);
}

/* --- Inputs ----------------------------------------------------------------- */

/* Reads every 0xNN byte that follows "db" in a generated include file. */
static size_t
read_inc_bytes(const char *dir, const char *name, uint8_t *out, size_t max)
{
    char    path[1024];
    char   *text;
    char   *p;
    size_t  n = 0;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    text = (char *) read_file(path, NULL);
    for (p = text; (p = strstr(p, "db ")) != NULL;) {
        char *end = strchr(p, '\n');

        p += 3;
        while ((p < end) && (*p != ';')) {
            if ((p[0] == '0') && (p[1] == 'x')) {
                if (n < max)
                    out[n] = (uint8_t) strtoul(p, &p, 16);
                n++;
            } else
                p++;
        }
    }
    free(text);
    return n;
}

static void
read_signon(const char *dir)
{
    char  path[1024];
    char *text;
    char *p;
    char *q;

    snprintf(path, sizeof(path), "%s/card.inc", dir);
    text = (char *) read_file(path, NULL);
    p    = strstr(text, "CARD_STR_SIGNON");
    if ((p == NULL) || ((p = strchr(p, '`')) == NULL) || ((q = strchr(p + 1, '`')) == NULL))
        die("cannot find CARD_STR_SIGNON in card.inc");
    snprintf(signon, sizeof(signon), "%.*s", (int) (q - p - 1), p + 1);
    free(text);
}

/* --- Expectations ------------------------------------------------------------ */

/* Parameter table entry for a mode at 200, 350 or 400 lines (independent of the ROM's tables). */
static int
expected_param(int mode, int lines)
{
    switch (mode) {
        case 0x00:
        case 0x01:
            return (lines == 400) ? 0x17 : (lines == 350) ? 0x13 + mode : mode;
        case 0x02:
        case 0x03:
            return (lines == 400) ? 0x18 : (lines == 350) ? 0x13 + mode : mode;
        case 0x07:
            return (lines == 400) ? 0x19 : 0x07;
        case 0x0f:
            return 0x11;
        case 0x10:
            return 0x12;
        case 0x11:
            return 0x1a;
        case 0x12:
            return 0x1b;
        case 0x13:
            return 0x1c;
        default:
            return mode;
    }
}

static int
expected_palette(int mode, int lines)
{
    switch (mode) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            return (lines == 200) ? PAL_CGA64 : PAL_EGA64;
        case 0x07:
        case 0x0f:
            return PAL_MONO64;
        case 0x10:
        case 0x11:
        case 0x12:
            return PAL_EGA64;
        case 0x13:
            return PAL_VGA256;
        default:
            return PAL_CGA64;
    }
}

static int
is_text(int mode)
{
    return (mode <= 3) || (mode == 7);
}

static const uint8_t *
font_for(int height)
{
    return (height == 16) ? font16 : (height == 14) ? font14 : font8;
}

/* Offset of the BIOS's parameter table in the ROM, found through the save pointer chain. */
static uint32_t
param_table(void)
{
    return far_ptr(far_ptr(0x4a8));
}

/* --- Tests -------------------------------------------------------------------- */

static void
check_regs_match_ex(const uint8_t *p, const char *what, int cursor_moved)
{
    int i;

    for (i = 0; i < 4; i++)
        CHECK(vga.seq[i + 1] == p[5 + i], "%s: SR%X = %02X, expected %02X", what, i + 1, vga.seq[i + 1], p[5 + i]);
    CHECK(vga.misc == p[9], "%s: misc = %02X, expected %02X", what, vga.misc, p[9]);
    for (i = 0; i < 25; i++) {
        if (cursor_moved && ((i == 0x0e) || (i == 0x0f)))
            continue; /* cursor location: checked separately */
        CHECK(vga.crtc[i] == p[10 + i], "%s: CR%02X = %02X, expected %02X", what, i, vga.crtc[i], p[10 + i]);
    }
    for (i = 0; i < 20; i++)
        CHECK(vga.attr[i] == p[35 + i], "%s: AR%02X = %02X, expected %02X", what, i, vga.attr[i], p[35 + i]);
    CHECK(vga.attr[0x14] == 0, "%s: AR14 = %02X, expected 00", what, vga.attr[0x14]);
    for (i = 0; i < 9; i++)
        CHECK(vga.gc[i] == p[55 + i], "%s: GR%X = %02X, expected %02X", what, i, vga.gc[i], p[55 + i]);
    CHECK(vga.attr_index & 0x20, "%s: screen left blanked (palette address source off)", what);
    CHECK(vga.protect_violations == 0, "%s: %d writes to protected CRTC registers", what, vga.protect_violations);
    CHECK(vga.wrong_port_accesses == 0, "%s: %d accesses to ports the mode does not decode", what,
          vga.wrong_port_accesses);
}

static void
check_regs_match(const uint8_t *p, const char *what)
{
    check_regs_match_ex(p, what, 0);
}

static void
check_dac(int palette, const char *what)
{
    int count = (palette == PAL_VGA256) ? 256 : 64;
    int i;
    int bad = 0;

    for (i = 0; i < 256; i++) {
        const uint8_t *want;
        static const uint8_t black[3] = { 0, 0, 0 };

        if (i >= count)
            want = black;
        else if (palette == PAL_VGA256)
            want = pal256[i];
        else
            want = pal64[palette][i];
        if (memcmp(vga.dac[i], want, 3) != 0)
            bad++;
    }
    CHECK(bad == 0, "%s: %d DAC entries differ from palette %d", what, bad, palette);
    CHECK(vga.pel_mask == 0xff, "%s: PEL mask %02X", what, vga.pel_mask);
}

static void
check_font_in_plane2(int height, const char *what)
{
    const uint8_t *font = font_for(height);
    static const int sample[] = { 0x00, 0x41, 0x61, 0xb3, 0xc4, 0xdb, 0xfe };
    size_t         s;
    int            r;
    int            bad = 0;

    for (s = 0; s < sizeof(sample) / sizeof(sample[0]); s++) {
        for (r = 0; r < height; r++) {
            if (vga.planes[2][sample[s] * 32 + r] != font[sample[s] * height + r])
                bad++;
        }
    }
    CHECK(bad == 0, "%s: %d font bytes in plane 2 differ from the 8x%d font", what, bad, height);
}

static void
test_post(void)
{
    static const uint8_t stub[5] = { 0x9a, 0x03, 0x00, 0x00, 0xc0 }; /* call far C000:0003 */
    regs_t               r       = R(0x0102, 0x0304, 0x0506, 0x0708);
    regs_t               before  = r;
    const uint8_t       *p;
    uint32_t             t;
    size_t               i;
    uint32_t             int43;

    uc_mem_write(uc, STUB_POST, stub, sizeof(stub));
    if (!run(STUB_POST, sizeof(stub), &r, "POST"))
        return;

    CHECK(memcmp(&r, &before, sizeof(r)) == 0, "POST changed caller registers");
    CHECK(vga.gpu_resets == 1, "POST wrote GPU_RESET %d times, expected once", vga.gpu_resets);
    CHECK(!vga.disp_unlocked, "POST left the display engine's registers unlocked");
    CHECK(rd16(0x42) == 0xc000, "INT 10h not pointing at the ROM (segment %04X)", rd16(0x42));
    CHECK((rd16(0x108) == (IRET_ADDR & 0xffff)) && (rd16(0x10a) == 0xf000), "INT 42h does not hold the old INT 10h");
    CHECK(rd16(0x1b6) == 0xc000, "INT 6Dh not pointing at the ROM");
    int43 = far_ptr(0x10c);
    CHECK((int43 >= 0xc0000) && (memcmp(rom + (int43 - 0xc0000), font8, sizeof(font8)) == 0),
          "INT 43h does not point at the 8x8 font");
    CHECK(far_ptr(0x7c) == int43 + 128 * 8, "INT 1Fh does not point at the upper half of the 8x8 font");

    CHECK(rd8(0x449) == 0x03, "mode %02X after POST, expected 03", rd8(0x449));
    CHECK(rd8(0x487) == 0x60, "40:87 = %02X, expected 60", rd8(0x487));
    CHECK(rd8(0x488) == 0x09, "40:88 = %02X, expected 09", rd8(0x488));
    CHECK(rd8(0x489) == 0x11, "40:89 = %02X, expected 11", rd8(0x489));
    CHECK(rd8(0x48a) == 0x0b, "40:8A = %02X, expected 0B", rd8(0x48a));
    CHECK((rd16(0x410) & 0x30) == 0x20, "equipment word video bits %02X", rd16(0x410) & 0x30);

    t = param_table();
    CHECK((t >= 0xc0000) && (t < 0xc0000 + rom_len), "save pointer chain does not lead into the ROM");
    p = rom + (t - 0xc0000) + 0x18 * 64;
    check_regs_match_ex(p, "POST mode 03h", 1);
    check_dac(PAL_EGA64, "POST");
    check_font_in_plane2(16, "POST");

    /* Sign-on on the first row, then the copyright, a blank line, and the cursor below. */
    /* Text memory is odd/even: character N is at offset 2N of plane 0, its attribute at 2N of plane 1. */
    for (i = 0; signon[i]; i++) {
        if (vga.planes[0][2 * i] != (uint8_t) signon[i])
            break;
    }
    CHECK(signon[i] == 0, "sign-on text not on screen (differs at column %u)", (unsigned) i);
    CHECK(vga.planes[1][0] == 0x07, "sign-on attribute %02X, expected 07", vga.planes[1][0]);
    CHECK(rd16(0x450) == 0x0300, "cursor at %04X after POST, expected 0300 (row 3)", rd16(0x450));
    CHECK((vga.crtc[0x0e] == 0x00) && (vga.crtc[0x0f] == 0xf0), "hardware cursor at %02X%02X, expected 00F0",
          vga.crtc[0x0e], vga.crtc[0x0f]);
    CHECK(strstr(vga.debug, "POST: start\n") && strstr(vga.debug, "mode 03h\n") && strstr(vga.debug, "POST: done\n"),
          "debug log missing POST lines: \"%s\"", vga.debug);
}

static void
test_modes(void)
{
    static const int lines_list[3] = { 400, 350, 200 };
    static const int modes[]       = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13 };
    size_t           m;
    int              l;

    for (l = 0; l < 3; l++) {
        for (m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
            int            mode  = modes[m];
            int            lines = lines_list[l];
            const uint8_t *p     = rom + (param_table() - 0xc0000) + expected_param(mode, lines) * 64;
            char           what[64];
            int            mono  = !(p[9] & 1);
            regs_t         r;
            int            plane;
            int            i;

            snprintf(what, sizeof(what), "mode %02Xh at %d lines", mode, lines);
            wr8(0x489, (uint8_t) ((rd8(0x489) & 0x6f) | ((lines == 400) ? 0x10 : (lines == 200) ? 0x80 : 0x00)));
            for (plane = 0; plane < 4; plane++)
                memset(vga.planes[plane], 0xaa, sizeof(vga.planes[plane]));
            vga.protect_violations  = 0;
            vga.wrong_port_accesses = 0;
            set_mode((uint8_t) mode);

            check_regs_match(p, what);
            check_dac(expected_palette(mode, lines), what);

            CHECK(rd8(0x449) == mode, "%s: 40:49 = %02X", what, rd8(0x449));
            CHECK(rd16(0x44a) == p[0], "%s: columns %u, expected %u", what, rd16(0x44a), p[0]);
            CHECK(rd16(0x44c) == (p[3] | (p[4] << 8)), "%s: page size %04X", what, rd16(0x44c));
            CHECK(rd16(0x44e) == 0, "%s: page start %04X", what, rd16(0x44e));
            CHECK(rd8(0x462) == 0, "%s: active page %u", what, rd8(0x462));
            CHECK(rd16(0x463) == (mono ? 0x3b4 : 0x3d4), "%s: CRTC port %03X", what, rd16(0x463));
            CHECK(rd8(0x484) == p[1], "%s: rows-1 %u, expected %u", what, rd8(0x484), p[1]);
            CHECK(rd16(0x485) == p[2], "%s: character height %u, expected %u", what, rd16(0x485), p[2]);
            CHECK((rd16(0x410) & 0x30) == (mono ? 0x30 : 0x20), "%s: equipment video bits %02X", what,
                  rd16(0x410) & 0x30);
            for (i = 0; i < 8; i++)
                CHECK(rd16(0x450 + 2 * i) == 0, "%s: cursor of page %d not reset", what, i);
            CHECK(!(rd8(0x487) & 0x80), "%s: 40:87 bit 7 set without bit 7 in AL", what);

            /* Video memory cleared. */
            if (is_text(mode)) {
                int bad = 0;

                for (i = 0; i < 0x8000; i += 0x7fe) { /* 32 KB, odd/even: even offsets */
                    if ((vga.planes[0][i] != 0x20) || (vga.planes[1][i] != 0x07))
                        bad++;
                }
                CHECK(bad == 0, "%s: text memory not cleared to 0720h", what);
                check_font_in_plane2(p[2], what);
            } else {
                int bad = 0;
                /* Where a 32 KB or 64 KB clear lands in the planes: modes 04h/05h use
                   odd/even (planes 0-1, even offsets), mode 06h plane 0 linearly, mode
                   13h chain-4 (every plane, offsets that are multiples of 4), the planar
                   modes all four planes linearly. */
                int limit  = (mode <= 6) ? 0x8000 : 0x10000;
                int step   = (mode <= 5) ? 0x7fe : (mode == 0x13) ? 0xffc : 0xfff;
                int planes = (mode == 6) ? 1 : (mode <= 5) ? 2 : 4;

                for (plane = 0; plane < planes; plane++) {
                    for (i = 0; i < limit; i += step) {
                        if (vga.planes[plane][i] != 0)
                            bad++;
                    }
                }
                CHECK(bad == 0, "%s: graphics memory not cleared", what);
                {
                    uint32_t f = far_ptr(0x10c);

                    CHECK((f >= 0xc0000) && (memcmp(rom + (f - 0xc0000), font_for(p[2]), (size_t) 256 * p[2]) == 0),
                          "%s: INT 43h does not point at the 8x%u font", what, p[2]);
                }
            }

            r = R(0x0f00, 0xff00, 0, 0);
            int10(&r);
            CHECK(((r.ax & 0xff) == mode) && ((r.ax >> 8) == p[0]) && ((r.bx >> 8) == 0),
                  "%s: AH=0Fh returned AX=%04X BX=%04X", what, r.ax, r.bx);
        }
    }
    wr8(0x489, (uint8_t) ((rd8(0x489) & 0x6f) | 0x10)); /* back to 400 lines */

    /* Bit 7 keeps video memory. */
    set_mode(0x03);
    vga.planes[0][0] = 'Z';
    set_mode(0x83);
    CHECK(vga.planes[0][0] == 'Z', "mode 83h cleared video memory");
    CHECK(rd8(0x487) & 0x80, "mode 83h did not set 40:87 bit 7");
    {
        regs_t r = R(0x0f00, 0, 0, 0);

        int10(&r);
        CHECK((r.ax & 0xff) == 0x83, "AH=0Fh after mode 83h returned AL=%02X", r.ax & 0xff);
    }
    set_mode(0x03);
    CHECK(!(rd8(0x487) & 0x80), "mode 03h left 40:87 bit 7 set");

    /* Modes VGA does not have are ignored. */
    {
        regs_t r = R(0x0008, 0, 0, 0);

        int10(&r);
        CHECK(rd8(0x449) == 0x03, "mode 08h changed the mode to %02X", rd8(0x449));
    }
}

static uint8_t
cell_char(int row, int col)
{
    return vga.planes[0][2 * (row * 80 + col)];
}

static uint8_t
cell_attr(int row, int col)
{
    return vga.planes[1][2 * (row * 80 + col)];
}

static void
tty_string(const char *s)
{
    for (; *s; s++) {
        regs_t r = R((uint16_t) (0x0e00 | (uint8_t) *s), 0x0007, 0, 0);

        int10(&r);
    }
}

static void
test_text(void)
{
    regs_t r;
    int    row;
    int    i;

    set_mode(0x03);

    /* Cursor position and shape */
    r = R(0x0200, 0x0000, 0, 0x050a);
    int10(&r);
    CHECK(rd16(0x450) == 0x050a, "AH=02h did not store the cursor");
    CHECK((vga.crtc[0x0e] == 0x01) && (vga.crtc[0x0f] == 0x9a), "AH=02h hardware cursor %02X%02X, expected 019A",
          vga.crtc[0x0e], vga.crtc[0x0f]);
    r = R(0x0300, 0x0000, 0, 0);
    int10(&r);
    CHECK((r.dx == 0x050a) && (r.cx == 0x0d0e), "AH=03h returned DX=%04X CX=%04X (mode 03h's own shape)", r.dx, r.cx);

    r = R(0x0100, 0, 0x0607, 0);
    int10(&r);
    CHECK((vga.crtc[0x0a] == 0x0d) && (vga.crtc[0x0b] == 0x0e), "AH=01h 0607h gave CRTC %02X/%02X, expected 0D/0E",
          vga.crtc[0x0a], vga.crtc[0x0b]);
    r = R(0x0100, 0, 0x2000, 0);
    int10(&r);
    CHECK(vga.crtc[0x0a] == 0x20, "AH=01h 2000h did not hide the cursor");
    CHECK(rd16(0x460) == 0x2000, "AH=01h did not store the shape");
    r = R(0x0100, 0, 0x0607, 0);
    int10(&r);

    /* Write and read characters */
    r = R(0x0200, 0x0000, 0, 0x050a);
    int10(&r);
    r = R(0x0958, 0x001e, 3, 0);
    int10(&r);
    for (i = 10; i < 13; i++)
        CHECK((cell_char(5, i) == 'X') && (cell_attr(5, i) == 0x1e), "AH=09h cell (5,%d) = %02X/%02X", i,
              cell_char(5, i), cell_attr(5, i));
    CHECK(cell_char(5, 13) == ' ', "AH=09h wrote past CX");
    CHECK(rd16(0x450) == 0x050a, "AH=09h moved the cursor");
    r = R(0x0800, 0x0000, 0, 0);
    int10(&r);
    CHECK(r.ax == 0x1e58, "AH=08h returned %04X, expected 1E58", r.ax);
    r = R(0x0a59, 0x0007, 2, 0);
    int10(&r);
    CHECK((cell_char(5, 10) == 'Y') && (cell_attr(5, 10) == 0x1e) && (cell_char(5, 12) == 'X'),
          "AH=0Ah did not keep attributes or wrote too far");

    /* Teletype */
    set_mode(0x03);
    tty_string("AB\r\nC");
    CHECK((cell_char(0, 0) == 'A') && (cell_char(0, 1) == 'B') && (cell_char(1, 0) == 'C'), "teletype text misplaced");
    CHECK(rd16(0x450) == 0x0101, "teletype cursor %04X, expected 0101", rd16(0x450));
    tty_string("\b\bZ");
    CHECK((cell_char(1, 0) == 'Z') && (rd16(0x450) == 0x0101), "teletype backspace failed");

    /* Wrap and scroll */
    set_mode(0x03);
    for (row = 0; row < 26; row++) {
        char line[8];

        snprintf(line, sizeof(line), "L%02d\r\n", row);
        tty_string(line);
    }
    CHECK((cell_char(0, 0) == 'L') && (cell_char(0, 1) == '0') && (cell_char(0, 2) == '2'),
          "after scrolling row 0 shows %c%c%c, expected L02", cell_char(0, 0), cell_char(0, 1), cell_char(0, 2));
    CHECK((cell_char(23, 1) == '2') && (cell_char(23, 2) == '5'), "after scrolling row 23 does not show L25");
    CHECK((cell_char(24, 0) == ' ') && (cell_attr(24, 0) == 0x07), "new bottom row not blank");
    CHECK(rd16(0x450) == 0x1800, "cursor %04X after scrolling, expected 1800", rd16(0x450));
    set_mode(0x03);
    for (i = 0; i < 81; i++)
        tty_string("w");
    CHECK((cell_char(1, 0) == 'w') && (rd16(0x450) == 0x0101), "teletype did not wrap at column 80");

    /* Window scroll up and down */
    set_mode(0x03);
    for (row = 0; row < 12; row++) {
        r = R(0x0200, 0, 0, (uint16_t) (row << 8));
        int10(&r);
        r = R((uint16_t) (0x0900 | ('a' + row)), 0x0007, 80, 0);
        int10(&r);
    }
    r = R(0x0602, 0x4f00, 0x0202, 0x0a14);
    int10(&r);
    CHECK((cell_char(2, 2) == 'e') && (cell_char(8, 20) == 'k'), "AH=06h did not move rows up");
    CHECK((cell_char(9, 5) == ' ') && (cell_attr(9, 5) == 0x4f) && (cell_attr(10, 20) == 0x4f),
          "AH=06h did not blank the bottom rows");
    CHECK((cell_char(2, 1) == 'c') && (cell_char(2, 21) == 'c') && (cell_char(11, 5) == 'l'),
          "AH=06h touched cells outside the window");
    r = R(0x0701, 0x1700, 0x0202, 0x0a14);
    int10(&r);
    CHECK((cell_char(3, 2) == 'e') && (cell_char(2, 2) == ' ') && (cell_attr(2, 2) == 0x17), "AH=07h failed");
    r = R(0x0600, 0x0700, 0x0000, 0x184f);
    int10(&r);
    CHECK((cell_char(11, 5) == ' ') && (cell_attr(24, 79) == 0x07), "AH=06h AL=0 did not clear the screen");
    r = R(0x0601, 0x0700, 0x0000, 0xffff);
    int10(&r); /* oversized window is clipped to the screen */

    /* Pages */
    r = R(0x0501, 0, 0, 0);
    int10(&r);
    CHECK((rd8(0x462) == 1) && (rd16(0x44e) == 0x1000), "AH=05h page 1: page %u start %04X", rd8(0x462),
          rd16(0x44e));
    CHECK((vga.crtc[0x0c] == 0x08) && (vga.crtc[0x0d] == 0x00), "AH=05h start address %02X%02X, expected 0800",
          vga.crtc[0x0c], vga.crtc[0x0d]);
    r = R(0x0500, 0, 0, 0);
    int10(&r);

    /* Write string */
    set_mode(0x03);
    {
        static const char hello[] = "Hello";
        static const char pairs[] = { 'A', 0x1f, 'B', 0x2e };

        uc_mem_write(uc, 0x2000, hello, 5);
        uc_mem_write(uc, 0x2010, pairs, 4);
        r    = R(0x1301, 0x002f, 5, 0x0300);
        r.es = 0x0200;
        r.bp = 0x0000;
        int10(&r);
        CHECK((cell_char(3, 0) == 'H') && (cell_char(3, 4) == 'o') && (cell_attr(3, 4) == 0x2f), "AH=13h AL=1 text");
        CHECK(rd16(0x450) == 0x0305, "AH=13h AL=1 cursor %04X, expected 0305", rd16(0x450));
        r    = R(0x1302, 0x0000, 2, 0x0400);
        r.es = 0x0201;
        r.bp = 0x0000;
        int10(&r);
        CHECK((cell_char(4, 0) == 'A') && (cell_attr(4, 0) == 0x1f) && (cell_char(4, 1) == 'B') &&
                  (cell_attr(4, 1) == 0x2e),
              "AH=13h AL=2 character/attribute pairs");
        CHECK(rd16(0x450) == 0x0305, "AH=13h AL=2 moved the cursor");
    }

    /* Display combination code */
    r = R(0x1a00, 0, 0, 0);
    int10(&r);
    CHECK(((r.ax & 0xff) == 0x1a) && (r.bx == 0x0008), "AH=1Ah AL=00h returned AX=%04X BX=%04X", r.ax, r.bx);
    r = R(0x1a01, 0x0108, 0, 0);
    int10(&r);
    CHECK(rd8(0x48a) == 0x0c, "AH=1Ah AL=01h stored index %02X, expected 0C", rd8(0x48a));
    r = R(0x1a00, 0, 0, 0);
    int10(&r);
    CHECK(r.bx == 0x0108, "AH=1Ah AL=00h after set returned BX=%04X", r.bx);
    r = R(0x1a01, 0x0008, 0, 0);
    int10(&r);

    /* EGA information */
    r = R(0x1200, 0x0010, 0, 0);
    int10(&r);
    CHECK((r.bx == 0x0003) && (r.cx == 0x0009), "AH=12h BL=10h returned BX=%04X CX=%04X", r.bx, r.cx);

    /* Light pen */
    r = R(0x0400, 0, 0, 0);
    int10(&r);
    CHECK((r.ax >> 8) == 0, "AH=04h returned AH=%02X", r.ax >> 8);
}

/* --- Graphics services and the remaining functions -------------------------- */

/* Colour of a pixel as the model holds it, for the current mode's memory layout. */
static int
model_pixel(int mode, int x, int y)
{
    if (mode == 0x13) {
        uint32_t a = (uint32_t) (y * 320 + x);

        return vga.planes[a & 3][a & ~3u];
    }
    if ((mode == 0x04) || (mode == 0x05) || (mode == 0x06)) {
        /* CGA layout: even lines at 0, odd lines at 2000h. Modes 04h/05h use odd/even
           addressing across planes 0 and 1; mode 06h is linear in plane 0. */
        uint32_t a = (uint32_t) ((y >> 1) * 80 + ((y & 1) ? 0x2000 : 0) + ((mode == 6) ? x / 8 : x / 4));

        if (mode == 6)
            return (vga.planes[0][a] >> (7 - (x & 7))) & 1;
        return (vga.planes[a & 1][a & ~1u] >> ((3 - (x & 3)) * 2)) & 3;
    }
    {
        int      cols = rd16(0x44a);
        uint32_t off  = (uint32_t) (y * cols + x / 8);
        int      bit  = 0x80 >> (x & 7);
        int      c    = 0;
        int      p;

        for (p = 0; p < 4; p++) {
            if (vga.planes[p][off] & bit)
                c |= 1 << p;
        }
        return c;
    }
}

/* Checks the character cell at (row, col) against glyph `ch` drawn in `colour` on 0. */
static int
cell_matches(int mode, int row, int col, int ch, int colour, int height)
{
    const uint8_t *font = font_for(height);
    int            r;
    int            x;

    for (r = 0; r < height; r++) {
        for (x = 0; x < 8; x++) {
            int want = (font[ch * height + r] & (0x80 >> x)) ? colour : 0;

            if (model_pixel(mode, col * 8 + x, row * height + r) != want)
                return 0;
        }
    }
    return 1;
}

static void
put_cursor(int row, int col)
{
    regs_t r = R(0x0200, 0, 0, (uint16_t) ((row << 8) | col));

    int10(&r);
}

static int
read_char_at(int row, int col)
{
    regs_t r;

    put_cursor(row, col);
    r = R(0x0800, 0, 0, 0);
    int10(&r);
    CHECK((r.fs == 0x3456) && (r.gs == 0x4567), "AH=08h in mode %02Xh changed FS or GS", rd8(0x449));
    return r.ax & 0xff;
}

static void
test_graphics(void)
{
    static const struct {
        int mode, height, colour, xor_colour;
    } char_modes[] = { { 0x12, 16, 0x0e, 0x8e }, { 0x10, 14, 0x0b, 0x8b }, { 0x0d, 8, 0x0c, 0x8c },
                       { 0x13, 8, 0x2a, -1 },     { 0x04, 8, 0x03, 0x83 }, { 0x06, 8, 0x01, 0x81 } };
    size_t m;
    regs_t r;
    int    pix;
    int    y;

    for (m = 0; m < sizeof(char_modes) / sizeof(char_modes[0]); m++) {
        int  mode = char_modes[m].mode;
        int  h    = char_modes[m].height;
        char what[32];

        snprintf(what, sizeof(what), "mode %02Xh", mode);
        set_mode((uint8_t) mode);

        /* Pixels */
        pix = (mode == 6) ? 1 : 2;
        r   = R((uint16_t) (0x0c00 | pix), 0, 13, 7);
        int10(&r);
        CHECK(model_pixel(mode, 13, 7) == pix, "%s: AH=0Ch pixel is %d, expected %d", what, model_pixel(mode, 13, 7),
              pix);
        r = R(0x0d00, 0, 13, 7);
        int10(&r);
        CHECK((r.ax & 0xff) == pix, "%s: AH=0Dh returned %d, expected %d", what, r.ax & 0xff, pix);
        if (mode != 0x13) {
            r = R(0x0c00 | 0x83, 0, 13, 7);
            int10(&r);
            CHECK(model_pixel(mode, 13, 7) == ((mode == 6) ? 0 : 1), "%s: XOR pixel is %d", what,
                  model_pixel(mode, 13, 7));
        }

        /* Characters */
        put_cursor(1, 2);
        r = R(0x0941, (uint16_t) char_modes[m].colour, 1, 0);
        int10(&r);
        CHECK(cell_matches(mode, 1, 2, 'A', (mode == 6) ? 1 : (char_modes[m].colour & ((mode == 4) ? 3 : 0xff)), h),
              "%s: AH=09h did not draw 'A' in colour %02Xh", what, char_modes[m].colour);
        if (mode >= 0x0d) {
            vga.gc[2] = 0x05;
            vga.gc[7] = 0x0a;
        }
        CHECK(read_char_at(1, 2) == 'A', "%s: AH=08h did not read back 'A'", what);
        if (mode >= 0x0d) {
            CHECK((vga.gc[2] == 0x05) && (vga.gc[7] == 0x0a) && (vga.gc[5] == ((mode == 0x13) ? 0x40 : 0x00)),
                  "%s: AH=08h did not restore GR2/GR5/GR7 (%02X %02X %02X)", what, vga.gc[2], vga.gc[5], vga.gc[7]);
            vga.gc[2] = 0x00;
            vga.gc[7] = 0x0f;
        }
        if (char_modes[m].xor_colour >= 0) {
            put_cursor(1, 2);
            r = R(0x0941, (uint16_t) char_modes[m].xor_colour, 1, 0);
            int10(&r);
            CHECK(cell_matches(mode, 1, 2, 0, 0, h), "%s: drawing 'A' again with XOR did not erase it", what);
        }

        /* Teletype and scrolling */
        set_mode((uint8_t) mode);
        put_cursor(1, 0);
        r = R(0x0e58, 0x0001, 0, 0);
        int10(&r);
        CHECK(read_char_at(1, 0) == 'X', "%s: teletype 'X' not readable", what);
        r = R(0x0601, 0x0000, 0x0000, 0xffff);
        int10(&r);
        CHECK(read_char_at(0, 0) == 'X', "%s: AH=06h did not scroll 'X' up a row", what);
        CHECK(read_char_at(1, 0) == 0, "%s: AH=06h did not blank the vacated row", what);
        r = R(0x0701, 0x0000, 0x0000, 0xffff);
        int10(&r);
        CHECK(read_char_at(1, 0) == 'X', "%s: AH=07h did not scroll 'X' down a row", what);
    }

    /* Characters 80h-FFh in the CGA modes come from INT 1Fh. */
    set_mode(0x04);
    put_cursor(0, 0);
    r = R(0x0980, 0x0003, 1, 0);
    int10(&r);
    CHECK(cell_matches(4, 0, 0, 0x80, 3, 8), "mode 04h: character 80h not drawn from the upper 8x8 half");

    /* CGA palette */
    r = R(0x0b00, 0x0100, 0, 0);
    int10(&r);
    CHECK((vga.attr[1] == 0x12) && (vga.attr[2] == 0x14) && (vga.attr[3] == 0x16),
          "AH=0Bh palette 0 gave %02X %02X %02X", vga.attr[1], vga.attr[2], vga.attr[3]);
    r = R(0x0b00, 0x0004, 0, 0);
    int10(&r);
    CHECK((vga.attr[0] == 0x04) && (vga.attr[0x11] == 0x04) && (vga.attr[1] == 0x02),
          "AH=0Bh background 04h gave AR0=%02X AR11=%02X AR1=%02X", vga.attr[0], vga.attr[0x11], vga.attr[1]);

    /* Mode 06h: one bit per pixel */
    set_mode(0x06);
    for (y = 0; y < 2; y++) {
        r = R(0x0c01, 0, 100, (uint16_t) y);
        int10(&r);
        CHECK(model_pixel(6, 100, y) == 1, "mode 06h: pixel on line %d not set", y);
    }
}

static void
test_palette_functions(void)
{
    static const uint8_t regs17[17] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38,
                                        0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x05 };
    static const uint8_t two[6]     = { 1, 2, 3, 4, 5, 6 };
    uint8_t              back[17];
    regs_t               r;

    set_mode(0x03);
    r = R(0x1000, 0x2a05, 0, 0);
    int10(&r);
    CHECK(vga.attr[5] == 0x2a, "AH=10h AL=00h");
    r = R(0x1007, 0x0005, 0, 0);
    int10(&r);
    CHECK((r.bx >> 8) == 0x2a, "AH=10h AL=07h returned %02X", r.bx >> 8);
    CHECK(vga.attr_index & 0x20, "AH=10h left the screen blanked");
    r = R(0x1001, 0x0700, 0, 0);
    int10(&r);
    r = R(0x1008, 0, 0, 0);
    int10(&r);
    CHECK((vga.attr[0x11] == 7) && ((r.bx >> 8) == 7), "AH=10h AL=01h/08h border");

    uc_mem_write(uc, 0x3000, regs17, sizeof(regs17));
    r    = R(0x1002, 0, 0, 0x0000);
    r.es = 0x0300;
    int10(&r);
    r    = R(0x1009, 0, 0, 0x0020);
    r.es = 0x0300;
    int10(&r);
    uc_mem_read(uc, 0x3020, back, sizeof(back));
    CHECK(memcmp(back, regs17, sizeof(regs17)) == 0, "AH=10h AL=02h/09h round trip");

    r = R(0x1003, 0x0000, 0, 0);
    int10(&r);
    CHECK(!(vga.attr[0x10] & 0x08), "AH=10h AL=03h BL=0 left blinking on");
    r = R(0x1003, 0x0001, 0, 0);
    int10(&r);
    CHECK(vga.attr[0x10] & 0x08, "AH=10h AL=03h BL=1 did not turn blinking on");

    r = R(0x1010, 0x0020, 0x0203, 0x0100);
    int10(&r);
    CHECK((vga.dac[0x20][0] == 1) && (vga.dac[0x20][1] == 2) && (vga.dac[0x20][2] == 3), "AH=10h AL=10h");
    r = R(0x1015, 0x0020, 0, 0);
    int10(&r);
    CHECK(((r.dx >> 8) == 1) && (r.cx == 0x0203), "AH=10h AL=15h returned DH=%02X CX=%04X", r.dx >> 8, r.cx);

    uc_mem_write(uc, 0x3040, two, sizeof(two));
    r    = R(0x1012, 0x0030, 2, 0x0040);
    r.es = 0x0300;
    int10(&r);
    CHECK((vga.dac[0x31][0] == 4) && (vga.dac[0x31][2] == 6), "AH=10h AL=12h");
    r    = R(0x1017, 0x0030, 2, 0x0050);
    r.es = 0x0300;
    int10(&r);
    uc_mem_read(uc, 0x3050, back, 6);
    CHECK(memcmp(back, two, 6) == 0, "AH=10h AL=17h");

    r = R(0x1013, 0x0100, 0, 0);
    int10(&r);
    r = R(0x1013, 0x0501, 0, 0);
    int10(&r);
    CHECK((vga.attr[0x10] & 0x80) && (vga.attr[0x14] == 0x05), "AH=10h AL=13h paging");
    r = R(0x101a, 0, 0, 0);
    int10(&r);
    CHECK(r.bx == 0x0501, "AH=10h AL=1Ah returned BX=%04X, expected 0501", r.bx);

    r = R(0x1018, 0x007f, 0, 0);
    int10(&r);
    r = R(0x1019, 0, 0, 0);
    int10(&r);
    CHECK((vga.pel_mask == 0x7f) && ((r.bx & 0xff) == 0x7f), "AH=10h AL=18h/19h");

    r = R(0x101b, 0x0020, 1, 0);
    int10(&r);
    CHECK((vga.dac[0x20][0] == 2) && (vga.dac[0x20][1] == 2) && (vga.dac[0x20][2] == 2),
          "AH=10h AL=1Bh gray of (1,2,3) is %d,%d,%d, expected 2,2,2", vga.dac[0x20][0], vga.dac[0x20][1],
          vga.dac[0x20][2]);
}

static void
test_font_functions(void)
{
    static uint8_t user[32];
    regs_t         r;
    uint32_t       p;
    int            i;

    set_mode(0x03);
    vga.protect_violations = 0;
    r = R(0x1103, 0x0005, 0, 0);
    int10(&r);
    CHECK(vga.seq[3] == 0x05, "AH=11h AL=03h");

    r = R(0x1112, 0x0000, 0, 0);
    int10(&r);
    CHECK((rd8(0x484) == 49) && (rd16(0x485) == 8), "AH=11h AL=12h: rows-1 %u, height %u", rd8(0x484), rd16(0x485));
    CHECK((vga.crtc[9] & 0x1f) == 7, "AH=11h AL=12h: CR09 %02X", vga.crtc[9]);
    CHECK(rd16(0x44c) == 0x2040, "AH=11h AL=12h: page size %04X, expected 2040 (8000 + 256)", rd16(0x44c));
    check_font_in_plane2(8, "AH=11h AL=12h");

    r = R(0x1130, 0x0600, 0, 0);
    int10(&r);
    p = ((uint32_t) r.es << 4) + r.bp;
    CHECK((p >= 0xc0000) && (memcmp(rom + (p - 0xc0000), font16, sizeof(font16)) == 0),
          "AH=11h AL=30h BH=6 does not point at the 8x16 font");
    CHECK((r.cx == 8) && ((r.dx & 0xff) == 49), "AH=11h AL=30h returned CX=%u DL=%u", r.cx, r.dx & 0xff);

    r = R(0x1114, 0x0000, 0, 0);
    int10(&r);
    CHECK((rd8(0x484) == 24) && (rd16(0x485) == 16) && (vga.crtc[0x12] == 0x8f) && (vga.crtc[7] & 0x02),
          "AH=11h AL=14h did not return to 25 rows");

    r = R(0x1111, 0x0000, 0, 0);
    int10(&r);
    CHECK((rd8(0x484) == 27) && (vga.crtc[0x12] == 0x87) && (vga.crtc[7] & 0x02) && !(vga.crtc[7] & 0x40),
          "AH=11h AL=11h: rows-1 %u, CR12 %02X, CR07 %02X", rd8(0x484), vga.crtc[0x12], vga.crtc[7]);
    CHECK((vga.crtc[0x0a] == 11) && (vga.crtc[0x0b] == 12), "AH=11h AL=11h cursor %02X/%02X", vga.crtc[0x0a],
          vga.crtc[0x0b]);
    CHECK(vga.protect_violations == 0, "AH=11h wrote protected CRTC registers");

    for (i = 0; i < 32; i++)
        user[i] = (uint8_t) (0x80 | i);
    uc_mem_write(uc, 0x5000, user, sizeof(user));
    set_mode(0x03);
    r    = R(0x1100, 0x1001, 2, 0x0041);
    r.es = 0x0500;
    r.bp = 0x0000;
    int10(&r);
    CHECK((vga.planes[2][0x4000 + 0x41 * 32] == 0x80) && (vga.planes[2][0x4000 + 0x42 * 32 + 15] == 0x9f),
          "AH=11h AL=00h user font not in block 1");

    r    = R(0x1120, 0, 0, 0);
    r.es = 0x0500;
    r.bp = 0x0010;
    int10(&r);
    CHECK(far_ptr(0x7c) == 0x5010, "AH=11h AL=20h did not set INT 1Fh");
    r    = R(0x1121, 0x0000, 12, 20);
    r.es = 0x0500;
    r.bp = 0x0000;
    int10(&r);
    CHECK((far_ptr(0x10c) == 0x5000) && (rd16(0x485) == 12) && (rd8(0x484) == 19), "AH=11h AL=21h");
    r = R(0x1122, 0x0002, 0, 0);
    int10(&r);
    p = far_ptr(0x10c);
    CHECK((p >= 0xc0000) && (memcmp(rom + (p - 0xc0000), font14, sizeof(font14)) == 0) && (rd8(0x484) == 24),
          "AH=11h AL=22h");
    r = R(0x1123, 0x0003, 0, 0);
    int10(&r);
    CHECK(rd8(0x484) == 42, "AH=11h AL=23h rows-1 %u, expected 42", rd8(0x484));
    set_mode(0x03);
}

static void
test_alt_select(void)
{
    regs_t r;

    set_mode(0x03);
    r = R(0x1201, 0x0030, 0, 0);
    int10(&r);
    CHECK(((r.ax & 0xff) == 0x12) && ((rd8(0x489) & 0x90) == 0x00), "AH=12h BL=30h AL=1 (350 lines)");
    set_mode(0x03);
    CHECK(vga.crtc[0x12] == 0x5d, "350-line mode 03h not used after BL=30h");
    r = R(0x1200, 0x0030, 0, 0);
    int10(&r);
    CHECK((rd8(0x489) & 0x90) == 0x80, "AH=12h BL=30h AL=0 (200 lines)");
    r = R(0x1202, 0x0030, 0, 0);
    int10(&r);
    CHECK((rd8(0x489) & 0x90) == 0x10, "AH=12h BL=30h AL=2 (400 lines)");

    vga.dac[1][0] = vga.dac[1][1] = vga.dac[1][2] = 1;
    r = R(0x1201, 0x0031, 0, 0);
    int10(&r);
    set_mode(0x03);
    CHECK(vga.dac[1][2] == 1, "AH=12h BL=31h AL=1 did not stop palette loading");
    r = R(0x1200, 0x0031, 0, 0);
    int10(&r);
    set_mode(0x03);
    CHECK(vga.dac[1][2] == 0x2a, "AH=12h BL=31h AL=0 did not restore palette loading");

    r = R(0x1201, 0x0032, 0, 0);
    int10(&r);
    CHECK(!(vga.misc & 0x02), "AH=12h BL=32h AL=1 did not disable video addressing");
    r = R(0x1200, 0x0032, 0, 0);
    int10(&r);
    CHECK(vga.misc & 0x02, "AH=12h BL=32h AL=0 did not enable video addressing");

    r = R(0x1200, 0x0033, 0, 0);
    int10(&r);
    set_mode(0x03);
    CHECK((vga.dac[1][0] == 5) && (vga.dac[1][2] == 5), "gray summing: blue is %d,%d,%d, expected 5,5,5",
          vga.dac[1][0], vga.dac[1][1], vga.dac[1][2]);
    r = R(0x1201, 0x0033, 0, 0);
    int10(&r);
    set_mode(0x03);

    r = R(0x1201, 0x0034, 0, 0);
    int10(&r);
    r = R(0x0100, 0, 0x0607, 0);
    int10(&r);
    CHECK((vga.crtc[0x0a] == 6) && (vga.crtc[0x0b] == 7), "cursor emulation off still scaled the cursor");
    r = R(0x1200, 0x0034, 0, 0);
    int10(&r);

    r = R(0x1201, 0x0036, 0, 0);
    int10(&r);
    CHECK((vga.seq[1] & 0x20) && ((r.ax & 0xff) == 0x12), "AH=12h BL=36h AL=1 did not blank");
    r = R(0x1200, 0x0036, 0, 0);
    int10(&r);
    CHECK(!(vga.seq[1] & 0x20), "AH=12h BL=36h AL=0 did not unblank");
}

static void
test_state_functions(void)
{
    uint8_t  buf[64];
    regs_t   r;
    uint32_t p;
    int      i;
    uint8_t  saved_regs[64];
    int      bad = 0;

    set_mode(0x03);
    r    = R(0x1b00, 0x0000, 0, 0);
    r.es = 0x0300;
    r.di = 0x0000;
    int10(&r);
    uc_mem_read(uc, 0x3000, buf, sizeof(buf));
    p = ((uint32_t) (buf[2] | (buf[3] << 8)) << 4) + (buf[0] | (buf[1] << 8));
    CHECK((r.ax & 0xff) == 0x1b, "AH=1Bh returned AL=%02X", r.ax & 0xff);
    CHECK((p >= 0xc0000) && (rom[p - 0xc0000] == 0xff) && (rom[p - 0xc0000 + 1] == 0xe0),
          "AH=1Bh static functionality pointer");
    CHECK((buf[4] == 3) && (buf[5] == 80) && (buf[0x22] == 25) && (buf[0x23] == 16) && (buf[0x25] == 8) &&
              (buf[0x27] == 16) && (buf[0x29] == 8) && (buf[0x2a] == 2) && (buf[0x31] == 3),
          "AH=1Bh state fields");

    r = R(0x1c00, 0, 7, 0);
    int10(&r);
    CHECK(((r.ax & 0xff) == 0x1c) && (r.bx == 15), "AH=1Ch AL=00h returned AL=%02X BX=%u, expected 15", r.ax & 0xff,
          r.bx);

    memcpy(saved_regs, rom + (param_table() - 0xc0000) + 0x18 * 64, 64);
    vga.protect_violations  = 0;
    vga.wrong_port_accesses = 0;
    r    = R(0x1c01, 0x0000, 7, 0);
    r.es = 0x0400;
    int10(&r);
    set_mode(0x13);
    r    = R(0x1c02, 0x0000, 7, 0);
    r.es = 0x0400;
    int10(&r);
    CHECK(rd8(0x449) == 0x03, "AH=1Ch restore: data area mode %02X", rd8(0x449));
    check_regs_match(saved_regs, "AH=1Ch restore");
    for (i = 0; i < 64; i++) {
        if (memcmp(vga.dac[i], pal64[PAL_EGA64][i], 3) != 0)
            bad++;
    }
    CHECK(bad == 0, "AH=1Ch restore: %d DAC entries differ", bad);
    set_mode(0x03);
}

/* --- Audit 2 regressions and IBM-compatible results -------------------------- */

/* Register values for modes 03h (400 lines) and 12h written out here, from the
   standard VGA register tables, so a wrong entry in the ROM's own table fails. */
static void
check_standard_regs(int mode, const char *what)
{
    static const uint8_t seq03[4]   = { 0x00, 0x03, 0x00, 0x02 };
    static const uint8_t crtc03[25] = { 0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f, 0x00, 0x4f, 0x0d, 0x0e, 0x00,
                                        0x00, 0x00, 0x00, 0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff };
    static const uint8_t attr03[20] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39,
                                        0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x0c, 0x00, 0x0f, 0x08 };
    static const uint8_t gc03[9]    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff };
    static const uint8_t seq12[4]   = { 0x01, 0x0f, 0x00, 0x06 };
    static const uint8_t crtc12[25] = { 0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e, 0x00, 0x40, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3, 0xff };
    static const uint8_t attr12[20] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39,
                                        0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x01, 0x00, 0x0f, 0x00 };
    static const uint8_t gc12[9]    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff };
    const uint8_t       *seq        = (mode == 3) ? seq03 : seq12;
    const uint8_t       *crtc       = (mode == 3) ? crtc03 : crtc12;
    const uint8_t       *attr       = (mode == 3) ? attr03 : attr12;
    const uint8_t       *gc         = (mode == 3) ? gc03 : gc12;
    uint8_t              misc       = (mode == 3) ? 0x67 : 0xe3;
    int                  i;
    int                  bad = 0;

    for (i = 0; i < 4; i++)
        bad += vga.seq[i + 1] != seq[i];
    for (i = 0; i < 25; i++) {
        if ((mode == 3) && ((i == 0x0e) || (i == 0x0f)))
            continue;
        bad += vga.crtc[i] != crtc[i];
    }
    for (i = 0; i < 20; i++)
        bad += vga.attr[i] != attr[i];
    for (i = 0; i < 9; i++)
        bad += vga.gc[i] != gc[i];
    bad += vga.misc != misc;
    CHECK(bad == 0, "%s: %d registers differ from the standard values", what, bad);
}

static void
test_audit2(void)
{
    regs_t r;

    /* Standard register values, independent of the ROM's table. */
    wr8(0x489, (uint8_t) ((rd8(0x489) & 0x6f) | 0x10));
    set_mode(0x03);
    check_standard_regs(3, "mode 03h");
    set_mode(0x12);
    check_standard_regs(0x12, "mode 12h");

    /* AL after a mode set: 30h, 3Fh for mode 06h, 20h above 07h. */
    r = R(0x0003, 0, 0, 0);
    int10(&r);
    CHECK((r.ax & 0xff) == 0x30, "mode 03h returned AL=%02X, expected 30", r.ax & 0xff);
    r = R(0x0006, 0, 0, 0);
    int10(&r);
    CHECK((r.ax & 0xff) == 0x3f, "mode 06h returned AL=%02X, expected 3F", r.ax & 0xff);
    r = R(0x0012, 0, 0, 0);
    int10(&r);
    CHECK((r.ax & 0xff) == 0x20, "mode 12h returned AL=%02X, expected 20", r.ax & 0xff);
    r = R(0x0008, 0, 0, 0);
    int10(&r);
    CHECK(r.ax == 0x0008, "unknown mode 08h changed AX to %04X", r.ax);

    /* The cursor shape in 40:60h is the mode's own after a mode set. */
    set_mode(0x03);
    CHECK(rd16(0x460) == 0x0d0e, "40:60 after mode 03h = %04X, expected 0D0E", rd16(0x460));

    /* Cursor emulation on an 8x16 font. */
    {
        static const struct {
            uint16_t in, out;
        } shapes[] = { { 0x0607, 0x0d0e }, { 0x0007, 0x000f }, { 0x0307, 0x080f }, { 0x0407, 0x080f },
                       { 0x0d0e, 0x0d0e }, { 0x0e0f, 0x0e0f }, { 0x0001, 0x0001 }, { 0x2000, 0x2000 },
                       { 0x0706, 0x060f }, { 0x0707, 0x0e0e } };
        size_t k;

        for (k = 0; k < sizeof(shapes) / sizeof(shapes[0]); k++) {
            r = R(0x0100, 0, shapes[k].in, 0);
            int10(&r);
            CHECK(((vga.crtc[0x0a] << 8) | vga.crtc[0x0b]) == shapes[k].out,
                  "cursor %04X on 8x16 gave %02X%02X, expected %04X", shapes[k].in, vga.crtc[0x0a], vga.crtc[0x0b],
                  shapes[k].out);
        }
        r = R(0x1112, 0, 0, 0); /* 8x8: the shapes keep their meaning */
        int10(&r);
        r = R(0x0100, 0, 0x0d0e, 0);
        int10(&r);
        CHECK((vga.crtc[0x0a] == 0x06) && (vga.crtc[0x0b] == 0x07), "cursor 0D0E on 8x8 gave %02X%02X, expected 0607",
              vga.crtc[0x0a], vga.crtc[0x0b]);
    }

    /* Font loads in sequence keep the screen's full height. */
    {
        static const struct {
            int      lines;
            uint16_t first, second;
            int      rows, cursor;
        } seqs[] = { { 0x10, 0x1111, 0x1114, 25, 0x0d0e }, { 0x10, 0x1111, 0x1112, 50, 0x0607 },
                     { 0x00, 0x1112, 0x1111, 25, 0x0b0c } };
        size_t k;

        for (k = 0; k < sizeof(seqs) / sizeof(seqs[0]); k++) {
            wr8(0x489, (uint8_t) ((rd8(0x489) & 0x6f) | seqs[k].lines));
            set_mode(0x03);
            r = R(seqs[k].first, 0, 0, 0);
            int10(&r);
            r = R(seqs[k].second, 0, 0, 0);
            int10(&r);
            CHECK((rd8(0x484) + 1 == seqs[k].rows) && (rd16(0x460) == seqs[k].cursor),
                  "AX=%04X then %04X: %d rows, cursor %04X; expected %d rows, cursor %04X", seqs[k].first,
                  seqs[k].second, rd8(0x484) + 1, rd16(0x460), seqs[k].rows, seqs[k].cursor);
        }
        wr8(0x489, (uint8_t) ((rd8(0x489) & 0x6f) | 0x10));
    }

    /* Monochrome text: the underline follows the font. */
    wr16(0x410, (uint16_t) ((rd16(0x410) & ~0x30) | 0x30));
    set_mode(0x07);
    r = R(0x1111, 0, 0, 0);
    int10(&r);
    CHECK(vga.crtc[0x14] == 0x0d, "mode 07h with 8x14 font: CR14 = %02X, expected 0D", vga.crtc[0x14]);
    wr16(0x410, (uint16_t) ((rd16(0x410) & ~0x30) | 0x20));

    /* 2-colour modes draw text whatever the colour (bit 7 still means XOR). */
    set_mode(0x06);
    put_cursor(1, 2);
    r = R(0x0941, 0x0000, 1, 0);
    int10(&r);
    CHECK(cell_matches(6, 1, 2, 'A', 1, 8), "mode 06h: 'A' in colour 00h not drawn");
    set_mode(0x11);
    put_cursor(1, 2);
    r = R(0x0941, 0x0002, 1, 0);
    int10(&r);
    CHECK(cell_matches(0x11, 1, 2, 'A', 0x0f, 16), "mode 11h: 'A' in colour 02h not drawn in every plane");

    /* AH=0Bh in a 16-colour mode sets the background and palette entries 1-3. */
    set_mode(0x04);
    set_mode(0x12);
    r = R(0x0b00, 0x0005, 0, 0);
    int10(&r);
    CHECK((vga.attr[0] == 0x05) && (vga.attr[0x11] == 0x05) && (vga.attr[1] == 0x03) && (vga.attr[2] == 0x05) &&
              (vga.attr[3] == 0x07),
          "mode 12h AH=0Bh BL=05h: AR00 %02X AR11 %02X AR1-3 %02X %02X %02X", vga.attr[0], vga.attr[0x11],
          vga.attr[1], vga.attr[2], vga.attr[3]);
    set_mode(0x03);
    r = R(0x0b00, 0x0101, 0, 0);
    int10(&r);
    CHECK((vga.attr[1] == 0x01) && (rd8(0x466) == 0x30), "text mode AH=0Bh BH=1 changed something");

    /* AH=13h with character/attribute pairs: control characters take no attribute byte. */
    {
        static const char pairs[] = { 'A', 0x1f, '\n', 'B', 0x2e };

        set_mode(0x03);
        uc_mem_write(uc, 0x2200, pairs, sizeof(pairs));
        r    = R(0x1302, 0, 3, 0x0000);
        r.es = 0x0220;
        r.bp = 0;
        int10(&r);
        CHECK((cell_char(0, 0) == 'A') && (cell_attr(0, 0) == 0x1f) && (cell_char(1, 1) == 'B') &&
                  (cell_attr(1, 1) == 0x2e),
              "AH=13h AL=2 with a line feed misplaced the text");
        r = R(0x1301, 0x0007, 0, 0x0505);
        int10(&r);
        CHECK(rd16(0x450) == 0x0000, "AH=13h with CX=0 moved the cursor to %04X", rd16(0x450));
    }

    /* Debug output only while the card's log listens. */
    vga.debug_on     = 0;
    vga.debug_writes = 0;
    set_mode(0x03);
    CHECK(vga.debug_writes == 0, "%d debug writes with the log off", vga.debug_writes);
    vga.debug_on = 1;
    set_mode(0x03);
    CHECK(vga.debug_writes > 0, "no debug output with the log on");

    /* AH=1Ah and AH=1Ch return AH = 0. */
    r = R(0x1a00, 0, 0, 0);
    int10(&r);
    CHECK(r.ax == 0x001a, "AH=1Ah returned AX=%04X, expected 001A", r.ax);
    r = R(0x1c00, 0, 7, 0);
    int10(&r);
    CHECK(r.ax == 0x001c, "AH=1Ch returned AX=%04X, expected 001C", r.ax);
    set_mode(0x03);
}

/* --- VBE 2.0 and VBE/DDC (AH=4Fh) ------------------------------------------- */

#define LFB_BASE    0xe0000000u
#define BUF_SEG     0x0300      /* 512-byte blocks at 0300:0000 and 0300:0200 */
#define BUF         0x3000u

typedef struct vbe_mode_t {
    uint16_t number, width, height, pitch;
    uint8_t  bpp, refresh;
} vbe_mode_t;

static vbe_mode_t vbe_modes[MODES_MAX * MODE_DEPTHS];
static size_t     n_vbe_modes;

/* The VBE modes, from the mode table through the shared loader (not the BIOS's generated table). */
static void
load_vbe_modes(const char *path)
{
    static const mode_limits_t limits = { VGPU98_DISP_MAX_WIDTH, VGPU98_DISP_MAX_HEIGHT, VGPU98_DISP_REFRESH_MIN,
                                          VGPU98_DISP_REFRESH_MAX };
    static dmodes_t            modes;
    size_t                     i;
    int                        d;

    modes_load(path, &limits, &modes);
    for (i = 0; i < modes.count; i++) {
        const dmode_t *m = &modes.mode[i];

        if (!m->enabled)
            continue;
        for (d = 0; d < MODE_DEPTHS; d++) {
            vbe_mode_t *v = &vbe_modes[n_vbe_modes];

            if (m->vbe[d] == 0)
                continue;
            v->number  = m->vbe[d];
            v->width   = m->width;
            v->height  = m->height;
            v->bpp     = mode_bpp[d];
            v->pitch   = (uint16_t) (m->width * mode_bytes_per_pixel(mode_bpp[d]));
            v->refresh = m->refresh[0];
            n_vbe_modes++;
        }
    }
}

static const vbe_mode_t *
find_vbe(uint16_t number)
{
    size_t i;

    for (i = 0; i < n_vbe_modes; i++) {
        if (vbe_modes[i].number == number)
            return &vbe_modes[i];
    }
    return NULL;
}

static const vbe_mode_t *
find_vbe_res(uint16_t width, uint16_t height, uint8_t bpp)
{
    size_t i;

    for (i = 0; i < n_vbe_modes; i++) {
        if ((vbe_modes[i].width == width) && (vbe_modes[i].height == height) && (vbe_modes[i].bpp == bpp))
            return &vbe_modes[i];
    }
    return NULL;
}

static int
vbe_fits(const vbe_mode_t *m)
{
    return (uint32_t) m->pitch * m->height <= vga.vram_size;
}

static uint32_t
rd32(uint32_t addr)
{
    return rd16(addr) | ((uint32_t) rd16(addr + 2) << 16);
}

static void
read_str(uint32_t addr, char *out, size_t max)
{
    size_t i;

    for (i = 0; i + 1 < max; i++) {
        out[i] = (char) rd8(addr + (uint32_t) i);
        if (out[i] == 0)
            return;
    }
    out[i] = 0;
}

static regs_t
vbe(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx, uint16_t di)
{
    regs_t r = R(ax, bx, cx, dx);

    r.es = BUF_SEG;
    r.di = di;
    int10(&r);
    return r;
}

static uint32_t
disp(uint8_t sr, int bytes)
{
    return vga_disp_value(&vga, sr, bytes);
}

/* After every VBE call the engine registers are locked again, and the BIOS never
   wrote to them while locked. */
static void
check_locked(const char *what)
{
    CHECK(!vga.disp_unlocked, "%s: engine registers left unlocked", what);
    CHECK(vga.disp_locked_writes == 0, "%s: %d writes to locked engine registers", what, vga.disp_locked_writes);
    vga.disp_locked_writes = 0;
}

static regs_t
vbe_set(uint16_t mode)
{
    return vbe(0x4f02, mode, 0, 0, 0);
}

static void
test_vbe_info(void)
{
    uint8_t  blk[512];
    char     s[64];
    regs_t   r;
    uint32_t list;
    size_t   i;
    size_t   n      = 0;
    int      bad    = 0;
    int      sorted = 1;
    int      prev   = -1;

    set_mode(0x03);
    memset(blk, 0x5a, sizeof(blk));
    memcpy(blk, "VBE2", 4);
    uc_mem_write(uc, BUF, blk, sizeof(blk));
    r = vbe(0x4f00, 0, 0, 0, 0);
    uc_mem_read(uc, BUF, blk, sizeof(blk));
    CHECK(r.ax == 0x004f, "4F00h returned AX=%04X", r.ax);
    CHECK((r.es == BUF_SEG) && (r.di == 0), "4F00h changed ES:DI");
    CHECK(memcmp(blk, "VESA", 4) == 0, "4F00h: signature %.4s", (const char *) blk);
    CHECK(rd16(BUF + 4) == 0x0200, "4F00h: version %04X, expected 0200", rd16(BUF + 4));
    CHECK(rd32(BUF + 10) == 0, "4F00h: capabilities %08X, expected 0 (6-bit DAC, VGA compatible)", rd32(BUF + 10));
    CHECK(rd16(BUF + 18) == vga.vram_size >> 16, "4F00h: total memory %u x 64 KB", rd16(BUF + 18));
    CHECK(rd16(BUF + 20) == 0x0100, "4F00h: OEM software revision %04X", rd16(BUF + 20));

    /* Mode list: every mode that fits, in ascending order, in the block. */
    list = far_ptr(BUF + 14);
    CHECK((list >= BUF + 34) && (list < BUF + 256), "4F00h: mode list at %05X, outside the reserved area", list);
    for (i = 0; (i < 200) && (rd16(list + 2 * i) != 0xffff); i++) {
        const vbe_mode_t *m = find_vbe(rd16(list + 2 * i));

        if ((m == NULL) || !vbe_fits(m))
            bad++;
        if ((int) rd16(list + 2 * i) <= prev)
            sorted = 0;
        prev = rd16(list + 2 * i);
        n++;
    }
    CHECK(list + 2 * n + 2 <= BUF + 256, "4F00h: mode list runs past the reserved area");
    for (i = 0; i < n_vbe_modes; i++) {
        if (vbe_fits(&vbe_modes[i]))
            n--;
    }
    CHECK((bad == 0) && (n == 0) && sorted, "4F00h: mode list wrong (%d unexpected, count off by %d, sorted %d)", bad,
          (int) n, sorted);

    /* VBE 2.0 caller: the strings are copied into the OEM area of the block. */
    read_str(far_ptr(BUF + 6), s, sizeof(s));
    CHECK((rd16(BUF + 8) == BUF_SEG) && (rd16(BUF + 6) >= 256) && (strcmp(s, "VGPU98") == 0),
          "4F00h VBE2: OEM string %04X:%04X \"%s\"", rd16(BUF + 8), rd16(BUF + 6), s);
    for (i = 22; i <= 30; i += 4) {
        read_str(far_ptr(BUF + (uint32_t) i), s, sizeof(s));
        CHECK((rd16(BUF + (uint32_t) i + 2) == BUF_SEG) && (rd16(BUF + (uint32_t) i) >= 256) && (s[0] != 0),
              "4F00h VBE2: string pointer at offset %u", (unsigned) i);
    }

    /* VBE 1.x caller: 256 bytes only, strings in the ROM. */
    memset(blk, 0x5a, sizeof(blk));
    uc_mem_write(uc, BUF, blk, sizeof(blk));
    r = vbe(0x4f00, 0, 0, 0, 0);
    uc_mem_read(uc, BUF, blk, sizeof(blk));
    read_str(far_ptr(BUF + 6), s, sizeof(s));
    CHECK((r.ax == 0x004f) && (rd16(BUF + 8) == 0xc000) && (strcmp(s, "VGPU98") == 0),
          "4F00h VBE 1.x: AX=%04X, OEM string at %04X \"%s\"", r.ax, rd16(BUF + 8), s);
    for (i = 256, bad = 0; i < 512; i++)
        bad += blk[i] != 0x5a;
    CHECK(bad == 0, "4F00h VBE 1.x: wrote %d bytes past the 256-byte block", bad);

    /* 8 MB: the 32 bpp modes above 8 MB leave the list. */
    vga.vram_size = 8u << 20;
    memcpy(blk, "VBE2", 4);
    uc_mem_write(uc, BUF, blk, 4);
    vbe(0x4f00, 0, 0, 0, 0);
    list = far_ptr(BUF + 14);
    {
        const vbe_mode_t *big  = find_vbe_res(1920, 1440, 32);
        const vbe_mode_t *fits = find_vbe_res(1920, 1080, 32);
        int               has_big = 0, has_fits = 0;

        for (i = 0; (i < 200) && (rd16(list + 2 * i) != 0xffff); i++) {
            has_big |= big && (rd16(list + 2 * i) == big->number);
            has_fits |= fits && (rd16(list + 2 * i) == fits->number);
        }
        CHECK(big && fits && !has_big && has_fits, "4F00h with 8 MB: 1920x1440x32 listed %d, 1920x1080x32 listed %d",
              has_big, has_fits);
        CHECK(rd16(BUF + 18) == 128, "4F00h with 8 MB: total memory %u", rd16(BUF + 18));
        r = vbe(0x4f01, 0, big ? big->number : 0, 0, 0x200);
        CHECK(r.ax == 0x014f, "4F01h for a mode that does not fit returned AX=%04X", r.ax);
        r = vbe_set(big ? big->number : 0);
        CHECK((r.ax == 0x014f) && !vga_disp_enabled(&vga), "4F02h for a mode that does not fit: AX=%04X", r.ax);
    }
    vga.vram_size = VRAM_SIZE;
    check_locked("4F00h");
}

static void
test_vbe_mode_info(void)
{
    size_t i;
    regs_t r;

    for (i = 0; i < n_vbe_modes; i++) {
        const vbe_mode_t *m = &vbe_modes[i];
        const uint32_t    b = BUF + 0x200;
        uint32_t          pages;
        int               ok;

        r = vbe(0x4f01, 0x1234, m->number, 0, 0x200);
        if (!CHECK_RESULT(r.ax == 0x004f, "4F01h %03Xh returned AX=%04X", m->number, r.ax))
            continue;
        pages = VRAM_SIZE / ((uint32_t) m->pitch * m->height) - 1;
        if (pages > 255)
            pages = 255;
        ok = (rd16(b) == 0x00bb) && (rd8(b + 2) == 0x07) && (rd8(b + 3) == 0) && (rd16(b + 4) == 64) &&
             (rd16(b + 6) == 64) && (rd16(b + 8) == 0xa000) && (rd16(b + 10) == 0) && (rd16(b + 14) == 0xc000) &&
             (rd16(b + 16) == m->pitch) && (rd16(b + 18) == m->width) && (rd16(b + 20) == m->height) &&
             (rd8(b + 24) == 1) && (rd8(b + 25) == m->bpp) && (rd8(b + 26) == 1) && (rd8(b + 28) == 0) &&
             (rd8(b + 29) == pages) && (rd8(b + 30) == 1) && (rd32(b + 40) == LFB_BASE) && (rd32(b + 44) == 0) &&
             (rd16(b + 48) == 0) && (r.bx == 0x1234);
        CHECK(ok, "4F01h %03Xh (%ux%ux%u): fields wrong", m->number, m->width, m->height, m->bpp);
        switch (m->bpp) {
            case 8:
                ok = (rd8(b + 27) == 4) && (rd32(b + 31) == 0) && (rd32(b + 35) == 0);
                break;
            case 15:
                ok = (rd8(b + 27) == 6) && (rd32(b + 31) == 0x05050a05) && (rd32(b + 35) == 0x0f010005);
                break;
            case 16:
                ok = (rd8(b + 27) == 6) && (rd32(b + 31) == 0x05060b05) && (rd32(b + 35) == 0x00000005);
                break;
            default:
                ok = (rd8(b + 27) == 6) && (rd32(b + 31) == 0x08081008) && (rd32(b + 35) == 0x18080008);
                break;
        }
        CHECK(ok, "4F01h %03Xh: memory model or colour fields wrong", m->number);
    }

    r = vbe(0x4f01, 0, 0x0013, 0, 0x200);
    CHECK(r.ax == 0x014f, "4F01h for VGA mode 13h returned AX=%04X", r.ax);
    r = vbe(0x4f01, 0, 0x01ff, 0, 0x200);
    CHECK(r.ax == 0x014f, "4F01h for mode 1FFh returned AX=%04X", r.ax);
    r = vbe(0x4f01, 0, 0x4117, 0, 0x200);
    CHECK(r.ax == 0x004f, "4F01h with the LFB flag in CX returned AX=%04X", r.ax);

    vga.lfb_base = 0;
    r            = vbe(0x4f01, 0, 0x0117, 0, 0x200);
    CHECK((r.ax == 0x004f) && (rd16(BUF + 0x200) == 0x003b) && (rd32(BUF + 0x200 + 40) == 0),
          "4F01h without BAR1: attributes %04X, base %08X", rd16(BUF + 0x200), rd32(BUF + 0x200 + 40));
    vga.lfb_base = LFB_BASE;
    check_locked("4F01h");
}

static void
test_vbe_set_mode(void)
{
    const vbe_mode_t *m = find_vbe(0x117);
    uint32_t          size;
    uint32_t          i;
    int               bad = 0;
    regs_t            r;

    CHECK(m && (m->width == 1024) && (m->height == 768) && (m->bpp == 16), "mode 117h is not 1024x768x16");
    CHECK(find_vbe(0x101) && (find_vbe(0x101)->width == 640) && (find_vbe(0x101)->bpp == 8),
          "mode 101h is not 640x480x8");
    if (!m || !find_vbe(0x101))
        return;

    size = (uint32_t) m->pitch * m->height;
    size *= (VRAM_SIZE / size > 256) ? 256 : VRAM_SIZE / size; /* every image page */
    memset(vga.vram, 0xaa, VRAM_SIZE);
    wr8(0x487, (uint8_t) (rd8(0x487) | 0x80));
    r = vbe_set(0x4117);
    CHECK(r.ax == 0x004f, "4F02h 4117h returned AX=%04X", r.ax);
    CHECK(vga_disp_enabled(&vga) && (disp(VGPU98_SR_DISP_FORMAT, 1) == 16) &&
              (disp(VGPU98_SR_DISP_WIDTH, 2) == 1024) && (disp(VGPU98_SR_DISP_HEIGHT, 2) == 768) &&
              (disp(VGPU98_SR_DISP_PITCH, 2) == 2048) && (disp(VGPU98_SR_DISP_START, 4) == 0) &&
              (disp(VGPU98_SR_DISP_REFRESH, 1) == m->refresh) && (disp(VGPU98_SR_DISP_BANK, 2) == 0) &&
              (disp(VGPU98_SR_DISP_SCRATCH, 2) == 0x4117),
          "4F02h 4117h: engine registers wrong");
    for (i = 0; i < size; i++)
        bad += vga.vram[i] != 0;
    CHECK(bad == 0, "4F02h 4117h: %d bytes of the image pages not cleared", bad);
    CHECK((size < VRAM_SIZE) && (vga.vram[size] == 0xaa) && (vga.vram[VRAM_SIZE - 1] == 0xaa),
          "4F02h 4117h: cleared past the image pages");
    CHECK(!(rd8(0x487) & 0x80), "4F02h 4117h: 40:87h bit 7 set");
    CHECK(rd8(0x449) == 0x13, "4F02h: data area mode %02X, expected 13h", rd8(0x449));
    check_dac(PAL_VGA256, "4F02h 4117h");
    check_locked("4F02h");
    r = vbe(0x4f03, 0, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 0x4117), "4F03h after 4117h: AX=%04X BX=%04X", r.ax, r.bx);

    /* Keep memory; the caller's unlocked registers stay unlocked. */
    memset(vga.vram, 0x55, 640 * 480);
    r = vbe_set(0x8101);
    CHECK((r.ax == 0x004f) && (vga.vram[0] == 0x55) && (vga.vram[640 * 480 - 1] == 0x55),
          "4F02h 8101h: AX=%04X, memory cleared", r.ax);
    CHECK(rd8(0x487) & 0x80, "4F02h 8101h: 40:87h bit 7 clear");
    vga.disp_unlocked = 1;
    r                 = vbe(0x4f03, 0, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 0x8101), "4F03h after 8101h: AX=%04X BX=%04X", r.ax, r.bx);
    CHECK(vga.disp_unlocked, "4F03h locked registers the caller had unlocked");
    vga.disp_unlocked = 0;

    /* VGA modes through AH=00h and 4F02h turn the engine off. */
    set_mode(0x03);
    CHECK(!vga_disp_enabled(&vga), "AH=00h mode 03h left the engine on");
    check_locked("AH=00h");
    vbe_set(0x0101);
    r = vbe_set(0x0003);
    CHECK((r.ax == 0x004f) && !vga_disp_enabled(&vga) && (rd8(0x449) == 0x03), "4F02h 0003h: AX=%04X, mode %02X",
          r.ax, rd8(0x449));
    r = vbe(0x4f03, 0, 0, 0, 0);
    CHECK(r.bx == 0x0003, "4F03h in mode 03h: BX=%04X", r.bx);
    r = vbe_set(0x8012);
    CHECK((r.ax == 0x004f) && (rd8(0x449) == 0x12) && (rd8(0x487) & 0x80), "4F02h 8012h: AX=%04X mode %02X", r.ax,
          rd8(0x449));
    r = vbe(0x4f03, 0, 0, 0, 0);
    CHECK(r.bx == 0x8012, "4F03h after 8012h: BX=%04X", r.bx);
    r = vbe_set(0x0083);
    CHECK((r.ax == 0x014f) && (rd8(0x449) == 0x12), "4F02h 0083h: AX=%04X", r.ax);
    r = vbe_set(0x4013);
    CHECK((r.ax == 0x024f) && (rd8(0x449) == 0x12), "4F02h 4013h (VGA mode, linear): AX=%04X", r.ax);
    r = vbe_set(0x01ff);
    CHECK(r.ax == 0x014f, "4F02h 01FFh: AX=%04X", r.ax);

    /* No BAR1: linear framebuffer requests fail, windowed ones work. */
    vga.lfb_base = 0;
    r            = vbe_set(0x4117);
    CHECK((r.ax == 0x024f) && !vga_disp_enabled(&vga), "4F02h 4117h without BAR1: AX=%04X", r.ax);
    r = vbe_set(0x0117);
    CHECK((r.ax == 0x004f) && vga_disp_enabled(&vga), "4F02h 0117h without BAR1: AX=%04X", r.ax);
    vga.lfb_base = LFB_BASE;
    set_mode(0x03);
    check_locked("4F02h");
}

static void
test_vbe_window(void)
{
    regs_t  r;
    uint8_t stub[5] = { 0x9a, 0, 0, 0, 0 }; /* call far WinFuncPtr */

    vbe_set(0x0117);
    r = vbe(0x4f05, 0x0000, 0, 3, 0);
    CHECK((r.ax == 0x004f) && (disp(VGPU98_SR_DISP_BANK, 2) == 3), "4F05h set bank 3: AX=%04X bank %u", r.ax,
          disp(VGPU98_SR_DISP_BANK, 2));
    wr8(0xa0005, 0x77);
    CHECK(vga.vram[3 * 65536 + 5] == 0x77, "window write did not reach bank 3");
    CHECK(rd8(0xb0005) == 0xff, "B0000h decoded in extended mode");
    r = vbe(0x4f05, 0x0100, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.dx == 3), "4F05h get: AX=%04X DX=%04X", r.ax, r.dx);
    r = vbe(0x4f05, 0x0001, 0, 1, 0);
    CHECK(r.ax == 0x014f, "4F05h window B: AX=%04X", r.ax);
    r = vbe(0x4f05, 0x0200, 0, 1, 0);
    CHECK(r.ax == 0x014f, "4F05h BH=2: AX=%04X", r.ax);

    /* The far-call entry from the mode information. */
    vbe(0x4f01, 0, 0x0117, 0, 0x200);
    stub[1] = rd8(BUF + 0x200 + 12);
    stub[2] = rd8(BUF + 0x200 + 13);
    stub[3] = rd8(BUF + 0x200 + 14);
    stub[4] = rd8(BUF + 0x200 + 15);
    CHECK(rd16(BUF + 0x200 + 14) == 0xc000, "WinFuncPtr segment %04X", rd16(BUF + 0x200 + 14));
    uc_mem_write(uc, STUB_INT10, stub, sizeof(stub));
    r = R(0, 0x0000, 0x1111, 5);
    if (run(STUB_INT10, sizeof(stub), &r, "WinFuncPtr"))
        CHECK((disp(VGPU98_SR_DISP_BANK, 2) == 5) && (r.bx == 0) && (r.cx == 0x1111) && (r.si == 0x5151),
              "WinFuncPtr set bank 5: bank %u, BX=%04X", disp(VGPU98_SR_DISP_BANK, 2), r.bx);
    check_locked("WinFuncPtr");

    vbe_set(0x4117);
    r = vbe(0x4f05, 0x0000, 0, 1, 0);
    CHECK(r.ax == 0x034f, "4F05h in a linear mode: AX=%04X, expected 034F", r.ax);
    set_mode(0x03);
    r = vbe(0x4f05, 0x0000, 0, 1, 0);
    CHECK(r.ax == 0x034f, "4F05h in mode 03h: AX=%04X, expected 034F", r.ax);
    check_locked("4F05h");
}

static void
test_vbe_scan_line(void)
{
    uint32_t max = (VRAM_SIZE / 480) & ~3u;
    regs_t   r;

    if (max > 65532)
        max = 65532;
    vbe_set(0x0101);
    r = vbe(0x4f06, 0x0001, 0, 0, 0);
    /* The lines memory holds, at most FFFFh (DX's 16 bits: 64 MB holds more) */
    CHECK((r.ax == 0x004f) && (r.bx == 640) && (r.cx == 640) &&
              (r.dx == ((VRAM_SIZE / 640 > 0xffff) ? 0xffff : VRAM_SIZE / 640)),
          "4F06h get: AX=%04X BX=%u CX=%u DX=%u", r.ax, r.bx, r.cx, r.dx);
    r = vbe(0x4f06, 0x0000, 1000, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 1000) && (r.cx == 1000) && (disp(VGPU98_SR_DISP_PITCH, 2) == 1000),
          "4F06h set 1000 pixels: BX=%u CX=%u", r.bx, r.cx);
    r = vbe(0x4f06, 0x0000, 1001, 0, 0);
    CHECK((r.bx == 1004) && (r.cx == 1004), "4F06h set 1001 pixels: BX=%u CX=%u", r.bx, r.cx);
    r = vbe(0x4f06, 0x0002, 2001, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 2004), "4F06h set 2001 bytes: BX=%u", r.bx);
    r = vbe(0x4f06, 0x0000, 100, 0, 0);
    CHECK(r.bx == 640, "4F06h set below the width: BX=%u", r.bx);
    r = vbe(0x4f06, 0x0003, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == max) && (r.cx == max) && (r.dx == VRAM_SIZE / max),
          "4F06h max: AX=%04X BX=%u CX=%u DX=%u", r.ax, r.bx, r.cx, r.dx);
    r = vbe(0x4f06, 0x0002, 65535, 0, 0);
    CHECK((r.ax == 0x024f) && (disp(VGPU98_SR_DISP_PITCH, 2) == 640), "4F06h too long: AX=%04X pitch %u", r.ax,
          disp(VGPU98_SR_DISP_PITCH, 2));
    r = vbe(0x4f06, 0x0004, 0, 0, 0);
    CHECK(r.ax == 0x014f, "4F06h BL=4: AX=%04X", r.ax);
    vbe_set(0x0111);
    r = vbe(0x4f06, 0x0000, 641, 0, 0);
    CHECK((r.bx == 1284) && (r.cx == 642), "4F06h 16 bpp set 641 pixels: BX=%u CX=%u", r.bx, r.cx);
    set_mode(0x03);
    r = vbe(0x4f06, 0x0001, 0, 0, 0);
    CHECK(r.ax == 0x034f, "4F06h in mode 03h: AX=%04X", r.ax);
    check_locked("4F06h");
}

static void
test_vbe_display_start(void)
{
    regs_t r;

    vbe_set(0x0101);
    r = vbe(0x4f07, 0x0000, 8, 10, 0);
    CHECK((r.ax == 0x004f) && (disp(VGPU98_SR_DISP_START, 4) == 6408), "4F07h set 8,10: AX=%04X start %u", r.ax,
          disp(VGPU98_SR_DISP_START, 4));
    r = vbe(0x4f07, 0xff01, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.cx == 8) && (r.dx == 10) && (r.bx == 0x0001), "4F07h get: CX=%u DX=%u BX=%04X",
          r.cx, r.dx, r.bx);
    vga.status_reads_after_start = 0;
    r = vbe(0x4f07, 0x0000, 0, 2, 0);
    CHECK(vga.status_reads_after_start == 0, "4F07h BL=00h waited for a retrace");
    r = vbe(0x4f07, 0x0080, 0, 1, 0);
    CHECK((r.ax == 0x004f) && (disp(VGPU98_SR_DISP_START, 4) == 640), "4F07h set in retrace: start %u",
          disp(VGPU98_SR_DISP_START, 4));
    CHECK(vga.status_reads_after_start > 0, "4F07h BL=80h: no retrace wait after writing the start");
    /* Line FFFFh of a 2048-byte line lies past any memory the card has (of
       a 640-byte one, inside 64 MB). */
    vbe(0x4f06, 0x0000, 2048, 0, 0);
    r = vbe(0x4f07, 0x0000, 0, 0xffff, 0);
    CHECK((r.ax == 0x014f) && (disp(VGPU98_SR_DISP_START, 4) == 640), "4F07h past memory: AX=%04X", r.ax);
    vbe(0x4f06, 0x0000, 640, 0, 0);
    r = vbe(0x4f07, 0x0002, 0, 0, 0);
    CHECK(r.ax == 0x014f, "4F07h BL=2: AX=%04X", r.ax);
    {
        const vbe_mode_t *small = find_vbe_res(320, 200, 8);

        /* A long line and the last line number: line x pitch passes 32 bits. */
        vbe_set(small ? small->number : 0);
        r = vbe(0x4f06, 0x0002, 65532, 0, 0);
        CHECK(r.ax == 0x004f, "4F06h 65532 bytes in 320x200x8: AX=%04X", r.ax);
        r = vbe(0x4f07, 0x0000, 0, 0xffff, 0);
        CHECK((r.ax == 0x014f) && (disp(VGPU98_SR_DISP_START, 4) == 0), "4F07h line FFFFh: AX=%04X start %X",
              r.ax, disp(VGPU98_SR_DISP_START, 4));
    }
    vbe_set(0x0111);
    vbe(0x4f07, 0x0000, 3, 0, 0);
    r = vbe(0x4f07, 0x0001, 0, 0, 0);
    CHECK((disp(VGPU98_SR_DISP_START, 4) == 4) && (r.cx == 2), "4F07h 16 bpp pixel 3: start %u, get CX=%u",
          disp(VGPU98_SR_DISP_START, 4), r.cx);
    vbe_set(0x0101);
    vga.disp[VGPU98_SR_DISP_WIDTH - VGPU98_SR_DISP_FIRST]     = 0;
    vga.disp[VGPU98_SR_DISP_WIDTH + 1 - VGPU98_SR_DISP_FIRST] = 0;
    vga.disp[VGPU98_SR_DISP_PITCH - VGPU98_SR_DISP_FIRST]     = 0;
    vga.disp[VGPU98_SR_DISP_PITCH + 1 - VGPU98_SR_DISP_FIRST] = 0;
    r = vbe(0x4f07, 0x0001, 0, 0, 0);
    CHECK(r.ax == 0x004f, "4F07h get with WIDTH and PITCH 0: AX=%04X", r.ax);
    r = vbe(0x4f06, 0x0001, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 8), "4F06h get with WIDTH and PITCH 0: AX=%04X BX=%u (expected 8)", r.ax,
          r.bx);
    set_mode(0x03);
    r = vbe(0x4f07, 0x0001, 0, 0, 0);
    CHECK(r.ax == 0x034f, "4F07h in mode 03h: AX=%04X", r.ax);
    check_locked("4F07h");
}

static void
test_vbe_palette(void)
{
    static const uint8_t entries[8] = { 1, 2, 3, 0, 0x3f, 0x20, 0x10, 0 };
    uint8_t              back[8];
    regs_t               r;

    set_mode(0x03);
    r = vbe(0x4f08, 0x0001, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx >> 8 == 6), "4F08h get in mode 03h: AX=%04X BH=%u", r.ax, r.bx >> 8);
    r = vbe(0x4f08, 0x0800, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx >> 8 == 6), "4F08h set 8 bits: AX=%04X BH=%u", r.ax, r.bx >> 8);
    r = vbe(0x4f08, 0x0002, 0, 0, 0);
    CHECK(r.ax == 0x014f, "4F08h BL=2: AX=%04X", r.ax);
    vbe_set(0x0111);
    r = vbe(0x4f08, 0x0001, 0, 0, 0);
    CHECK(r.ax == 0x034f, "4F08h in a direct colour mode: AX=%04X", r.ax);

    vbe_set(0x0101);
    uc_mem_write(uc, BUF, entries, sizeof(entries));
    r = vbe(0x4f09, 0x0000, 2, 10, 0);
    CHECK((r.ax == 0x004f) && (vga.dac[10][0] == 3) && (vga.dac[10][1] == 2) && (vga.dac[10][2] == 1) &&
              (vga.dac[11][0] == 0x10) && (vga.dac[11][2] == 0x3f),
          "4F09h set: AX=%04X", r.ax);
    memset(back, 0xee, sizeof(back));
    uc_mem_write(uc, BUF + 0x100, back, sizeof(back));
    r = vbe(0x4f09, 0x0001, 2, 10, 0x100);
    uc_mem_read(uc, BUF + 0x100, back, sizeof(back));
    CHECK((r.ax == 0x004f) && (memcmp(back, entries, sizeof(back)) == 0), "4F09h get: AX=%04X", r.ax);
    r = vbe(0x4f09, 0x0080, 1, 12, 0);
    CHECK((r.ax == 0x004f) && (vga.dac[12][0] == 3), "4F09h set in retrace: AX=%04X", r.ax);
    r = vbe(0x4f09, 0x0002, 1, 0, 0);
    CHECK(r.ax == 0x024f, "4F09h secondary palette: AX=%04X", r.ax);
    r = vbe(0x4f09, 0x0004, 1, 0, 0);
    CHECK(r.ax == 0x014f, "4F09h BL=4: AX=%04X", r.ax);
    set_mode(0x03);
    check_locked("4F08h/4F09h");
}

static void
test_vbe_state(void)
{
    regs_t r;
    regs_t r7;

    r7 = R(0x1c00, 0, 7, 0);
    int10(&r7);
    r = vbe(0x4f04, 0, 0x000f, 0x0000, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 15) && (r7.bx == 15), "4F04h size: AX=%04X BX=%u (AH=1Ch: %u), expected 15", r.ax,
          r.bx, r7.bx);
    r = R(0x1c00, 0, 0x000f, 0);
    int10(&r);
    CHECK(r.bx == r7.bx, "AH=1Ch with CX bit 3 changed the size to %u", r.bx);

    /* Save in an extended mode, restore over mode 03h. */
    vbe_set(0x0117);
    vbe(0x4f07, 0x0000, 0, 2, 0);
    r    = R(0x4f04, 0, 0x000f, 0x0001);
    r.es = 0x0400;
    int10(&r);
    CHECK((r.ax == 0x004f) && (rd16(0x4006) != 0), "4F04h save: AX=%04X, engine offset %04X", r.ax, rd16(0x4006));
    set_mode(0x03);
    r    = R(0x4f04, 0, 0x000f, 0x0002);
    r.es = 0x0400;
    int10(&r);
    CHECK((r.ax == 0x004f) && vga_disp_enabled(&vga) && (disp(VGPU98_SR_DISP_FORMAT, 1) == 16) &&
              (disp(VGPU98_SR_DISP_WIDTH, 2) == 1024) && (disp(VGPU98_SR_DISP_START, 4) == 4096) &&
              (disp(VGPU98_SR_DISP_SCRATCH, 2) == 0x0117) && (rd8(0x449) == 0x13),
          "4F04h restore of mode 117h: AX=%04X enabled %d", r.ax, vga_disp_enabled(&vga));
    r = vbe(0x4f03, 0, 0, 0, 0);
    CHECK(r.bx == 0x0117, "4F03h after the restore: BX=%04X", r.bx);

    /* Restoring a VGA state turns the engine off. */
    set_mode(0x03);
    r    = R(0x4f04, 0, 0x0008, 0x0001);
    r.es = 0x0400;
    int10(&r);
    vbe_set(0x0117);
    r    = R(0x4f04, 0, 0x0008, 0x0002);
    r.es = 0x0400;
    int10(&r);
    CHECK((r.ax == 0x004f) && !vga_disp_enabled(&vga), "4F04h restore of an engine-off state: AX=%04X", r.ax);
    r = vbe(0x4f04, 0, 0x000f, 0x0003, 0);
    CHECK(r.ax == 0x014f, "4F04h DL=3: AX=%04X", r.ax);
    set_mode(0x03);
    check_locked("4F04h");
}

/* Runs the protected mode functions from 4F0Ah as 32-bit code: copies the table
   to flat memory and calls each entry with a near CALL. */
static void
run_pm(const uint8_t *table, uint16_t len, uint16_t entry, uint32_t ebx, uint32_t ecx, uint32_t edx, uint32_t edi,
       const uint8_t *data, size_t data_len, const char *what)
{
    uc_engine *pm;
    uc_hook    h_in, h_out;
    uint8_t    stub[5] = { 0xe8, 0, 0, 0, 0 };
    uint32_t   rel     = 0x10000u + entry - (0x1000u + 5);
    uint32_t   esp     = 0x8000;
    uc_err     err;

    if (uc_open(UC_ARCH_X86, UC_MODE_32, &pm) != UC_ERR_OK) {
        fail(__LINE__, "%s: cannot start Unicorn in 32-bit mode", what);
        return;
    }
    uc_mem_map(pm, 0, 0x100000, UC_PROT_ALL);
    uc_hook_add(pm, &h_in, UC_HOOK_INSN, (void *) hook_in, NULL, 1, 0, UC_X86_INS_IN);
    uc_hook_add(pm, &h_out, UC_HOOK_INSN, (void *) hook_out, NULL, 1, 0, UC_X86_INS_OUT);
    memcpy(stub + 1, &rel, 4);
    uc_mem_write(pm, 0x1000, stub, sizeof(stub));
    uc_mem_write(pm, 0x10000, table, len);
    if (data)
        uc_mem_write(pm, 0x20000, data, data_len);
    uc_reg_write(pm, UC_X86_REG_ESP, &esp);
    uc_reg_write(pm, UC_X86_REG_EBX, &ebx);
    uc_reg_write(pm, UC_X86_REG_ECX, &ecx);
    uc_reg_write(pm, UC_X86_REG_EDX, &edx);
    uc_reg_write(pm, UC_X86_REG_EDI, &edi);
    {
        uint32_t esi = 0x5151a5a5, ebp = 0xb0b05a5a;

        uc_reg_write(pm, UC_X86_REG_ESI, &esi);
        uc_reg_write(pm, UC_X86_REG_EBP, &ebp);
    }
    err = uc_emu_start(pm, 0x1000, 0x1005, 0, 10000000);
    uc_reg_read(pm, UC_X86_REG_ESP, &esp);
    CHECK((err == UC_ERR_OK) && (esp == 0x8000), "%s: %s, ESP %08X", what, uc_strerror(err), esp);
    {
        static const int ids[]  = { UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EDI,
                                    UC_X86_REG_ESI, UC_X86_REG_EBP };
        uint32_t         want[] = { ebx, ecx, edx, edi, 0x5151a5a5, 0xb0b05a5a };
        size_t           k;

        for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            uint32_t v = 0;

            uc_reg_read(pm, ids[k], &v);
            CHECK(v == want[k], "%s: register %d changed from %08X to %08X", what, (int) k, want[k], v);
        }
    }
    uc_close(pm);
}

static void
test_vbe_pm(void)
{
    static const uint8_t entry[4] = { 5, 6, 7, 0 };
    uint8_t              table[256];
    uint16_t             len;
    regs_t               r;
    int                  ok;

    r = vbe(0x4f0a, 0x0000, 0, 0, 0);
    len = r.cx;
    CHECK((r.ax == 0x004f) && (r.es == 0xc000) && (len >= 8) && (len <= sizeof(table)), "4F0Ah: AX=%04X ES=%04X CX=%u",
          r.ax, r.es, r.cx);
    if ((r.ax != 0x004f) || (len < 8) || (len > sizeof(table)))
        return;
    memcpy(table, rom + r.di, len);
    ok = (table[0] | (table[1] << 8)) < len && (table[2] | (table[3] << 8)) < len &&
         (table[4] | (table[5] << 8)) < len && (table[6] | (table[7] << 8)) == 0;
    CHECK(ok, "4F0Ah: table offsets outside the table");
    r = vbe(0x4f0a, 0x0001, 0, 0, 0);
    CHECK(r.ax == 0x014f, "4F0Ah BL=1: AX=%04X", r.ax);
    if (!ok)
        return;

    vbe_set(0x0101);
    vga.seq_index = 0x02;
    run_pm(table, len, (uint16_t) (table[0] | (table[1] << 8)), 0, 0, 7, 0, NULL, 0, "PM set window");
    CHECK(disp(VGPU98_SR_DISP_BANK, 2) == 7, "PM set window: bank %u", disp(VGPU98_SR_DISP_BANK, 2));
    vga.status_reads_after_start = 0;
    run_pm(table, len, (uint16_t) (table[2] | (table[3] << 8)), 0x80, 0x8d14, 4, 0, NULL, 0, "PM display start");
    CHECK(disp(VGPU98_SR_DISP_START, 4) == 0x123450, "PM display start: %08X", disp(VGPU98_SR_DISP_START, 4));
    CHECK(vga.status_reads_after_start > 0, "PM display start BL=80h: no retrace wait after writing the start");
    run_pm(table, len, (uint16_t) (table[4] | (table[5] << 8)), 0, 1, 20, 0x20000, entry, sizeof(entry),
           "PM palette");
    CHECK((vga.dac[20][0] == 7) && (vga.dac[20][1] == 6) && (vga.dac[20][2] == 5), "PM palette: %u %u %u",
          vga.dac[20][0], vga.dac[20][1], vga.dac[20][2]);
    CHECK(vga.seq_index == 0x02, "PM functions changed the sequencer index to %02X", vga.seq_index);
    set_mode(0x03);
    check_locked("4F0Ah");
}

static void
test_vbe_ddc(void)
{
    uint8_t blk[128];
    uint8_t sum = 0;
    size_t  i;
    regs_t  r;

    r = vbe(0x4f15, 0x0000, 0, 0, 0);
    CHECK((r.ax == 0x004f) && (r.bx == 0x0102), "4F15h BL=0: AX=%04X BX=%04X", r.ax, r.bx);
    r = vbe(0x4f15, 0x0001, 0, 0, 0);
    uc_mem_read(uc, BUF, blk, sizeof(blk));
    for (i = 0; i < sizeof(blk); i++)
        sum = (uint8_t) (sum + blk[i]);
    CHECK((r.ax == 0x004f) && (memcmp(blk, vga.edid, sizeof(blk)) == 0) && (sum == 0),
          "4F15h read EDID: AX=%04X, block differs or bad checksum", r.ax);
    CHECK(r.di == 0, "4F15h changed DI to %04X", r.di);
    r = vbe(0x4f15, 0x0001, 0, 1, 0);
    CHECK(r.ax == 0x014f, "4F15h block 1: AX=%04X", r.ax);
    r = vbe(0x4f15, 0x0002, 0, 0, 0);
    CHECK(r.ax == 0x014f, "4F15h BL=2: AX=%04X", r.ax);
    check_locked("4F15h");
}

static void
test_vbe_misc(void)
{
    static const uint16_t unsupported[] = { 0x4f0b, 0x4f10, 0x4f14, 0x4fff };
    size_t                i;
    regs_t                r;

    for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        r = vbe(unsupported[i], 0, 0, 0, 0);
        CHECK(r.ax == unsupported[i], "AX=%04X returned AX=%04X, expected it unchanged", unsupported[i], r.ax);
    }

    /* The upper halves of the 32-bit registers survive VBE calls. */
    high_word = 0xbeef;
    vbe(0x4f01, 0, 0x0117, 0, 0x200);
    CHECK(high_halves_are(0xbeef), "4F01h changed the upper half of a 32-bit register");
    vbe_set(0x0101);
    CHECK(high_halves_are(0xbeef), "4F02h changed the upper half of a 32-bit register");
    vbe(0x4f06, 0x0003, 0, 0, 0);
    vbe(0x4f07, 0x0000, 0, 1, 0);
    CHECK(high_halves_are(0xbeef), "4F06h/4F07h changed the upper half of a 32-bit register");
    high_word = 0;
    set_mode(0x03);
    check_locked("VBE");
}

static void
test_vbe(void)
{
    vga.lfb_base = LFB_BASE;
    test_vbe_info();
    test_vbe_mode_info();
    test_vbe_set_mode();
    test_vbe_window();
    test_vbe_scan_line();
    test_vbe_display_start();
    test_vbe_palette();
    test_vbe_state();
    test_vbe_pm();
    test_vbe_ddc();
    test_vbe_misc();
}

/* Every function number returns, keeps the stack balanced and preserves the
 * registers it does not report results in. */
static void
test_all_functions(void)
{
    int ah;

    for (ah = 0; ah < 256; ah++) {
        regs_t r;
        regs_t in;

        set_mode(0x03);
        r  = R((uint16_t) ((ah << 8) | (ah == 0 ? 0x03 : 0x00)), 0x0010, 0, 0);
        in = r;
        if (!int10(&r))
            continue;
        CHECK((r.si == in.si) && (r.di == in.di) && (r.ds == in.ds), "AH=%02Xh changed SI, DI or DS", ah);
        CHECK((r.fs == in.fs) && (r.gs == in.gs), "AH=%02Xh changed FS or GS", ah);
        if ((ah != 0x11) && (ah != 0x1b) && (ah != 0x1c))
            CHECK((r.bp == in.bp) && (r.es == in.es), "AH=%02Xh changed BP or ES", ah);
    }
}

int
main(int argc, char **argv)
{
    set_progname("biostest");
    if (argc != 4)
        die("usage: biostest ROM GEN_DIR MODES");

    rom = read_file(argv[1], &rom_len);
    if (read_inc_bytes(argv[2], "font8x8.inc", font8, sizeof(font8)) != sizeof(font8) ||
        read_inc_bytes(argv[2], "font8x14.inc", font14, sizeof(font14)) != sizeof(font14) ||
        read_inc_bytes(argv[2], "font8x16.inc", font16, sizeof(font16)) != sizeof(font16))
        die("font include files have the wrong size");
    read_signon(argv[2]);
    build_palettes(pal64, pal256);
    load_vbe_modes(argv[3]);

    setup_machine();
    test_post();
    if (failures == 0) {
        test_modes();
        test_text();
        test_graphics();
        test_palette_functions();
        test_font_functions();
        test_alt_select();
        test_state_functions();
        test_audit2();
        test_vbe();
        test_all_functions();
    }

    printf("biostest: deepest INT 10h call: %u bytes of the caller's stack (AX=%04X)\n", deepest, deepest_ax);
    CHECK(deepest <= STACK_BUDGET, "an INT 10h call used %u bytes of stack, more than the %u the spec allows", deepest,
          STACK_BUDGET);
    printf("biostest: %d checks, %d failed\n", checks, failures);
    uc_close(uc);
    return failures ? 1 : 0;
}
