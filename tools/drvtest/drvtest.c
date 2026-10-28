/*
 * drvtest: runs the VGPU98 display driver (vgpu98.drv) on the host.
 *
 * Loads the NE file with its own loader into an emulated PC (Unicorn, real
 * mode; NE selectors become paragraphs), stands in for the parts of Windows
 * the driver talks to -- KERNEL, DIBENG, USER, the VDD, DPMI, INT 2Fh and
 * INT 21h -- with recording stubs, and connects the card's ports to the
 * display engine model from biostest. Then it calls the driver's exports as
 * GDI and USER would and checks what the driver did: the engine registers,
 * the GDIINFO and PDEVICE it built, the DAC, the framebuffer, the VDD calls
 * and their order, screen switching, mode changes, ValidateMode, the
 * hardware cursor and the gamma table.
 *
 * Usage: drvtest DRIVER DISPLAY_MODES_CARD
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
#include "shared.h"
#include "stamp.h"
#include "util.h"
#include "vga_model.h"
#include "vid_vgpu98_card.h"

/* Memory map (paragraphs). */
#define SEG_STUBS   0x0100u /* import stubs, VDD entry, call stub */
#define SEG_FIRST   0x1000u /* NE segment n at SEG_FIRST + (n - 1) * 0x1000 */
#define SEG_BUFS    0x5000u /* GDIINFO, PDEVICE and other test buffers */
#define SEG_STACK   0x6000u
#define STACK_TOP   0xfff0u
#define SCREEN_SEL  0xffffu /* the selector DPMI hands out: base FFFF0h */
#define FB_LINEAR   0xffff0u
#define REGS_SEL    0x7000u /* the register window's, when the card has one: base 70000h */
#define REGS_LINEAR 0x70000u
#define REGS_PHYS   0xfe000000u
#define MEM_SIZE    0x2200000u
#define MAX_INSNS   50000000

#define LIN(seg, off) (((uint32_t) (seg) << 4) + (uint32_t) (off))

#define BUF_GDIINFO 0x0000u /* offsets in SEG_BUFS */
#define BUF_PDEVICE 0x0200u
#define BUF_GDI2    0x0800u
#define BUF_VALMODE 0x0a00u
#define BUF_PALETTE 0x0b00u
#define BUF_ARGS    0x0c00u
#define BUF_SHAPE   0x1000u /* cursor shapes */
#define BUF_SHAPE2  0x4000u
#define BUF_RAMP    0x9000u /* gamma ramps: 768 words each */
#define BUF_RAMP2   0x9800u

#define CALL_STUB   0x0f00u /* in SEG_STUBS: call far target */

/* Stub offsets in SEG_STUBS. */
#define VDD_STUB      0x0e00u /* int FDh; retf */
#define OLD2F_STUB    0x0e10u /* int FCh; iret */
#define REPAINT_STUB  0x0e20u /* int FBh; retf */

static uc_engine  *uc;
static vga_model_t vga;
static uint8_t    *ne_file;
static size_t      ne_size;
static int         checks, failures;

#define CHECK(cond, ...)                 \
    do {                                 \
        checks++;                        \
        if (!(cond))                     \
            fail(__LINE__, __VA_ARGS__); \
    } while (0)

static void
fail(int line, const char *fmt, ...)
{
    va_list ap;

    failures++;
    fprintf(stderr, "drvtest.c:%d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* --- Memory helpers --------------------------------------------------------- */

static uint8_t
rd8(uint32_t a)
{
    uint8_t v = 0;

    uc_mem_read(uc, a, &v, 1);
    return v;
}

static uint16_t
rd16(uint32_t a)
{
    return (uint16_t) (rd8(a) | (rd8(a + 1) << 8));
}

static uint32_t
rd32(uint32_t a)
{
    return rd16(a) | ((uint32_t) rd16(a + 2) << 16);
}

static void
wr8(uint32_t a, uint8_t v)
{
    uc_mem_write(uc, a, &v, 1);
}

static void
wr16(uint32_t a, uint16_t v)
{
    wr8(a, (uint8_t) v);
    wr8(a + 1, (uint8_t) (v >> 8));
}

static void
wr32(uint32_t a, uint32_t v)
{
    wr16(a, (uint16_t) v);
    wr16(a + 2, (uint16_t) (v >> 16));
}

static uint32_t
far_lin(uint32_t farptr)
{
    return LIN(farptr >> 16, farptr & 0xffff);
}

static uint32_t
reg(int id)
{
    uint32_t v = 0;

    uc_reg_read(uc, id, &v);
    return v;
}

static void
set_reg(int id, uint32_t v)
{
    uc_reg_write(uc, id, &v);
}

/* --- What the stubs record ----------------------------------------------------- */

typedef struct log_t {
    int      vdd[64];       /* VDD functions called, in order */
    int      n_vdd;
    uint32_t register_bytes; /* VDD_DRIVER_REGISTER: ECX */
    uint32_t restore_cb;     /* and ES:DI */
    uint32_t info_vram;      /* VDD_REGISTER_DISPLAY_DRIVER_INFO: ECX */
    int      int2f[32];
    int      n_int2f;
    int      int10_ax;
    int      trap_off_engine_state; /* at INT 2Fh 4000h: 1 if the engine was untouched */
    int      trap_on_engine_on;     /* at INT 2Fh 4007h: 1 if the engine was still on */
    int      chained_2f;     /* calls that reached the previous INT 2Fh handler */
    uint32_t hooked_2f;      /* INT 21h AH=25h AL=2Fh: the new handler */
    int      set_2f_count;
    int      repaints;
    int      page_locks;
    /* DPMI */
    uint32_t map_phys, map_size, sel_base, sel_limit;
    int      dpmi_allocs;
    /* DIB engine */
    int      dib_enable[2];  /* calls per style */
    uint32_t cdp_info, cdp_device, cdp_bits;
    uint16_t cdp_flags;
    int      cdp_calls;
    int      begin_access, end_access;
    uint16_t access_flags;
    int      translate_calls;
    int      setpal_calls;
    int      checkcursor_calls;
    uint32_t checkcursor_pdev;
    /* Thunk checks: the arguments a forwarded call arrived with. */
    uint8_t  fwd_args[64];
    int      fwd_id;
    /* The DIB engine's cursor, and the register window's mapping */
    int      setcursor_calls;
    uint32_t setcursor_shape; /* the last DIB_SetCursorExt's (0: none) */
    int      movecursor_calls;
    uint16_t movecursor_x, movecursor_y;
    int      regs_mapped;
} log_t;

static log_t L;

/* The configuration the VDD reports. */
static struct {
    uint16_t x, y, dpi, refresh, high_vert;
    uint8_t  bpp;
    int      fail;
} config;

/* --- Imports ---------------------------------------------------------------------- */

enum {
    IMP_NONE,
    IMP_GLOBALSMARTPAGELOCK,
    IMP_ALLOCCSTODSALIAS,
    IMP_FREESELECTOR,
    IMP_GETMODULEHANDLE,
    IMP_GETPROCADDRESS,
    IMP_DIB_ENABLE,
    IMP_CREATEDIBPDEVICE,
    IMP_DIB_BEGINACCESS,
    IMP_DIB_ENDACCESS,
    IMP_DIB_SETPALETTEEXT,
    IMP_DIB_SETPALETTETRANSLATEEXT,
    IMP_DIB_CHECKCURSOREXT,
    IMP_FORWARDED /* a DIB engine routine reached through a thunk */
};

typedef struct import_t {
    const char *module;
    uint16_t    ordinal;
    int         id;
    uint16_t    arg_bytes;
    const char *name;
} import_t;

/* Argument bytes are what the test pushes for the forwarded routines it calls,
   plus the extra argument the thunk adds for the Ext ones. */
static const import_t imports[] = {
    { "KERNEL", 230, IMP_GLOBALSMARTPAGELOCK, 2, "GlobalSmartPageLock" },
    { "KERNEL", 170, IMP_ALLOCCSTODSALIAS, 2, "AllocCStoDSAlias" },
    { "KERNEL", 176, IMP_FREESELECTOR, 2, "FreeSelector" },
    { "KERNEL", 47, IMP_GETMODULEHANDLE, 4, "GetModuleHandle" },
    { "KERNEL", 50, IMP_GETPROCADDRESS, 6, "GetProcAddress" },
    { "DIBENG", 5, IMP_DIB_ENABLE, 18, "DIB_Enable" },
    { "DIBENG", 300, IMP_CREATEDIBPDEVICE, 14, "CreateDIBPDevice" },
    { "DIBENG", 105, IMP_DIB_BEGINACCESS, 14, "DIB_BeginAccess" },
    { "DIBENG", 106, IMP_DIB_ENDACCESS, 6, "DIB_EndAccess" },
    { "DIBENG", 405, IMP_DIB_SETPALETTEEXT, 12, "DIB_SetPaletteExt" },
    { "DIBENG", 407, IMP_DIB_SETPALETTETRANSLATEEXT, 8, "DIB_SetPaletteTranslateExt" },
    { "DIBENG", 104, IMP_DIB_CHECKCURSOREXT, 4, "DIB_CheckCursorExt" },
    /* Forwarded routines the test calls through their exports. */
    { "DIBENG", 9, IMP_FORWARDED, 16, "DIB_Pixel" },
    { "DIBENG", 400, IMP_FORWARDED, 22, "DIB_RealizeObjectExt" },
    { "DIBENG", 401, IMP_FORWARDED, 12, "DIB_DibBltExt" },
    { "DIBENG", 102, IMP_FORWARDED, 8, "DIB_SetCursorExt" },
    { "DIBENG", 103, IMP_FORWARDED, 8, "DIB_MoveCursorExt" },
    { "DIBENG", 3, IMP_FORWARDED, 14, "DIB_Control" },
};

#define N_IMPORTS (sizeof(imports) / sizeof(imports[0]))

/* Each import gets an 8-byte stub: mov ax, index; int FEh; retf N. Imports the
   table does not know get a stub that fails the test when called. */
static uint16_t
stub_for(const char *module, uint16_t ordinal)
{
    static int next_unknown = 0x100;
    size_t     i;

    for (i = 0; i < N_IMPORTS; i++) {
        if (!strcmp(imports[i].module, module) && (imports[i].ordinal == ordinal))
            return (uint16_t) (i * 8);
    }
    return (uint16_t) (0x0800 + (next_unknown++ & 0xff) * 8);
}

static void
write_stubs(void)
{
    static const uint8_t vdd[]     = { 0xcd, 0xfd, 0xcb };       /* int FDh; retf */
    static const uint8_t old2f[]   = { 0xcd, 0xfc, 0xcf };       /* int FCh; iret */
    static const uint8_t repaint[] = { 0xcd, 0xfb, 0xcb };       /* int FBh; retf */
    size_t               i;

    for (i = 0; i < N_IMPORTS; i++) {
        uint8_t s[8] = { 0xb8, (uint8_t) i, 0, 0xcd, 0xfe, 0xca, (uint8_t) imports[i].arg_bytes,
                         (uint8_t) (imports[i].arg_bytes >> 8) };

        uc_mem_write(uc, LIN(SEG_STUBS, i * 8), s, 8);
    }
    for (i = 0; i < 256; i++) { /* unknown imports: mov ax, FFFFh; int FEh; retf */
        uint8_t s[8] = { 0xb8, 0xff, 0xff, 0xcd, 0xfe, 0xcb, 0x90, 0x90 };

        uc_mem_write(uc, LIN(SEG_STUBS, 0x0800 + i * 8), s, 8);
    }
    uc_mem_write(uc, LIN(SEG_STUBS, VDD_STUB), vdd, sizeof(vdd));
    uc_mem_write(uc, LIN(SEG_STUBS, OLD2F_STUB), old2f, sizeof(old2f));
    uc_mem_write(uc, LIN(SEG_STUBS, REPAINT_STUB), repaint, sizeof(repaint));
}

/* --- The NE loader -------------------------------------------------------------------- */

static uint32_t ne_hdr;
static int      n_segs;
static uint16_t seg_para[16];

static uint32_t
f16(size_t at)
{
    return (at + 1 < ne_size) ? (uint32_t) (ne_file[at] | (ne_file[at + 1] << 8)) : 0;
}

static uint32_t
f32(size_t at)
{
    return f16(at) | (f16(at + 2) << 16);
}

static const char *
module_name(uint32_t index)
{
    static char name[16];
    size_t      at = ne_hdr + f16(ne_hdr + 0x2a) + f16(ne_hdr + f16(ne_hdr + 0x28) + 2 * (index - 1));
    size_t      n  = ne_file[at];

    if (n >= sizeof(name))
        n = sizeof(name) - 1;
    memcpy(name, ne_file + at + 1, n);
    name[n] = 0;
    return name;
}

static void
load_ne(void)
{
    uint32_t shift;
    int      i;

    ne_hdr = f32(0x3c);
    if ((ne_hdr + 0x40 > ne_size) || (ne_file[ne_hdr] != 'N') || (ne_file[ne_hdr + 1] != 'E'))
        die("not an NE file");
    shift  = f16(ne_hdr + 0x32);
    n_segs = (int) f16(ne_hdr + 0x1c);
    if ((n_segs < 1) || (n_segs > 4))
        die("unexpected segment count %d", n_segs);
    for (i = 0; i < n_segs; i++) {
        size_t   st  = ne_hdr + f16(ne_hdr + 0x22) + 8 * (size_t) i;
        size_t   off = (size_t) f16(st) << shift;
        uint32_t len = f16(st + 2);

        seg_para[i + 1] = (uint16_t) (SEG_FIRST + 0x1000u * (uint32_t) i);
        uc_mem_write(uc, LIN(seg_para[i + 1], 0), ne_file + off, len);
    }
    /* Relocations, as the Windows loader applies them. */
    for (i = 0; i < n_segs; i++) {
        size_t   st    = ne_hdr + f16(ne_hdr + 0x22) + 8 * (size_t) i;
        size_t   r     = ((size_t) f16(st) << shift) + f16(st + 2);
        uint32_t flags = f16(st + 4);
        uint32_t n, k;

        if (!(flags & 0x100))
            continue;
        n = f16(r);
        for (k = 0; k < n; k++) {
            size_t   p     = r + 2 + 8 * k;
            uint32_t src   = ne_file[p];
            uint32_t rf    = ne_file[p + 1];
            uint32_t loc   = f16(p + 2);
            uint16_t sel   = 0, off = 0;

            if ((rf & 3) == 0) {
                sel = seg_para[ne_file[p + 4]];
                off = (uint16_t) f16(p + 6);
            } else if ((rf & 3) == 1) {
                sel = SEG_STUBS;
                off = stub_for(module_name(f16(p + 4)), (uint16_t) f16(p + 6));
            } else
                die("relocation flags %u not supported", rf);
            for (;;) {
                uint32_t a    = LIN(seg_para[i + 1], loc);
                uint16_t next = rd16(a);

                if (src == 2)
                    wr16(a, sel);
                else if (src == 3) {
                    wr16(a, off);
                    wr16(a + 2, sel);
                } else if (src == 5)
                    wr16(a, off);
                else
                    die("relocation source %u not supported", src);
                if ((rf & 4) || (next == 0xffff))
                    break;
                loc = next;
            }
        }
    }
}

/* An export's address (CS:IP as a 32-bit far pointer), by ordinal. */
static uint32_t
export_addr(uint32_t ordinal)
{
    size_t   at  = ne_hdr + f16(ne_hdr + 0x04);
    uint32_t ord = 1;

    while (ne_file[at]) {
        uint32_t count = ne_file[at];
        uint32_t type  = ne_file[at + 1];
        uint32_t k;

        at += 2;
        for (k = 0; k < count; k++, ord++) {
            if (type == 0)
                continue;
            if (ord == ordinal)
                return ((uint32_t) seg_para[type] << 16) | f16(at + 1);
            at += 3;
        }
    }
    die("no export @%u", ordinal);
}

/* An export's ordinal by name from the resident names table, 0 if absent. */
static uint32_t
resident_ordinal(const char *name)
{
    size_t at = ne_hdr + f16(ne_hdr + 0x26);

    while (ne_file[at]) {
        size_t n = ne_file[at];

        if ((n == strlen(name)) && !memcmp(ne_file + at + 1, name, n))
            return f16(at + 1 + n);
        at += 3 + n;
    }
    return 0;
}

/* --- Interrupts and stubs -------------------------------------------------------------------- */

static uint32_t
arg_base(void)
{
    /* SS:SP points at the stub's caller's return address; arguments follow. */
    return LIN(reg(UC_X86_REG_SS), reg(UC_X86_REG_SP) & 0xffff) + 4;
}

static void
set_ax(uint16_t v)
{
    set_reg(UC_X86_REG_AX, v);
}

static void
dib_enable(uint32_t a)
{
    uint32_t dev   = far_lin(rd32(a + 14));
    uint16_t style = rd16(a + 12);

    L.dib_enable[style & 1]++;
    if (style & 1) {
        uint32_t i;

        /* The DIB engine's drawing capabilities and its PDEVICE size. */
        for (i = 28; i <= 46; i += 2)
            wr16(dev + i, 0x1110 + (uint16_t) i);
        wr16(dev + 38, 0x0001); /* dpRaster: RC_BITBLT */
        wr16(dev + 26, 48);     /* dpDEVICEsize: a DIBENGINE */
        wr16(dev + 94, 0x0010); /* dpCaps1: C1_DIBENGINE */
    }
    set_ax(1);
}

static void
create_dib_pdevice(uint32_t a)
{
    uint32_t info = rd32(a + 10), device = rd32(a + 6), bits = rd32(a + 2);
    uint16_t flags = rd16(a);
    uint32_t dev   = far_lin(device);
    uint32_t bi    = far_lin(info);
    uint32_t w = rd32(bi + 4), h = rd32(bi + 8), bpp = rd16(bi + 14);

    L.cdp_calls++;
    L.cdp_info   = info;
    L.cdp_device = device;
    L.cdp_bits   = bits;
    L.cdp_flags  = flags;
    /* Fill the DIBENGINE as the DIB engine does. */
    wr16(dev + 0, 0x5250);
    wr16(dev + 2, (uint16_t) w);
    wr16(dev + 4, (uint16_t) h);
    wr16(dev + 6, (uint16_t) (w * ((bpp + 7) / 8)));
    wr8(dev + 8, 1);
    wr8(dev + 9, (uint8_t) bpp);
    wr32(dev + 14, w * ((bpp + 7) / 8));
    wr32(dev + 22, bits & 0xffff);
    wr16(dev + 26, (uint16_t) (bits >> 16));
    wr16(dev + 28, flags);
    wr16(dev + 30, 0x400);
    wr32(dev + 32, info);
    set_reg(UC_X86_REG_EAX, 0x00010001);
}

/* DIB_SetPaletteExt: copies PALETTEENTRY values (red, green, blue, flags) into
   the PDEVICE's colour table (blue, green, red, flags), as the DIB engine does. */
static void
dib_set_palette(uint32_t a)
{
    uint32_t dev   = far_lin(rd32(a));
    uint32_t pal   = far_lin(rd32(a + 4));
    uint16_t count = rd16(a + 8), start = rd16(a + 10);
    uint32_t bi    = far_lin(rd32(dev + 32));
    uint32_t ct    = bi + 40;
    uint16_t i;

    L.setpal_calls++;
    for (i = 0; (i < count) && (start + i < 256); i++) {
        uint32_t e = pal + 4u * i;
        uint32_t c = ct + 4u * (start + i);

        wr8(c + 0, rd8(e + 2));
        wr8(c + 1, rd8(e + 1));
        wr8(c + 2, rd8(e + 0));
        wr8(c + 3, rd8(e + 3));
    }
    set_ax(0);
}

static void
handle_import(void)
{
    uint16_t ax = (uint16_t) reg(UC_X86_REG_AX);
    uint32_t a  = arg_base();

    if (ax >= N_IMPORTS) {
        fail(__LINE__, "the driver called an import the test does not provide");
        set_ax(0);
        return;
    }
    switch (imports[ax].id) {
        case IMP_GLOBALSMARTPAGELOCK:
            L.page_locks++;
            set_ax(1);
            break;
        case IMP_ALLOCCSTODSALIAS:
            set_ax(rd16(a)); /* real mode: the same paragraph */
            break;
        case IMP_FREESELECTOR:
            set_ax(0);
            break;
        case IMP_GETMODULEHANDLE: {
            char     name[8];
            uint32_t s = far_lin(rd32(a));
            int      i;

            for (i = 0; i < 7; i++)
                name[i] = (char) rd8(s + (uint32_t) i);
            name[7] = 0;
            set_ax(!strcmp(name, "USER") ? 0x5555 : 0);
            break;
        }
        case IMP_GETPROCADDRESS:
            if ((rd16(a + 4) == 0x5555) && (rd32(a) == 275)) {
                set_ax(REPAINT_STUB);
                set_reg(UC_X86_REG_DX, SEG_STUBS);
            } else {
                set_ax(0);
                set_reg(UC_X86_REG_DX, 0);
            }
            break;
        case IMP_DIB_ENABLE:
            dib_enable(a);
            break;
        case IMP_CREATEDIBPDEVICE:
            create_dib_pdevice(a);
            break;
        case IMP_DIB_BEGINACCESS:
            L.begin_access++;
            L.access_flags = rd16(a);
            break;
        case IMP_DIB_ENDACCESS:
            L.end_access++;
            L.access_flags = rd16(a);
            break;
        case IMP_DIB_SETPALETTEEXT:
            dib_set_palette(a);
            break;
        case IMP_DIB_SETPALETTETRANSLATEEXT:
            L.translate_calls++;
            break;
        case IMP_DIB_CHECKCURSOREXT:
            L.checkcursor_calls++;
            L.checkcursor_pdev = rd32(a);
            break;
        case IMP_FORWARDED:
            L.fwd_id = ax;
            uc_mem_read(uc, a, L.fwd_args, imports[ax].arg_bytes);
            if (imports[ax].ordinal == 102) { /* DIB_SetCursorExt(lpShape, lpPDevice) */
                L.setcursor_calls++;
                L.setcursor_shape = rd32(a + 4);
            } else if (imports[ax].ordinal == 103) { /* DIB_MoveCursorExt(x, y, lpPDevice) */
                L.movecursor_calls++;
                L.movecursor_y = rd16(a + 4);
                L.movecursor_x = rd16(a + 6);
            }
            set_ax(0x1234);
            break;
        default:
            break;
    }
}

static void
handle_vdd(void)
{
    uint32_t fn = reg(UC_X86_REG_EAX);

    CHECK(reg(UC_X86_REG_EBX) == 1, "VDD call %X with VM handle %X", fn, reg(UC_X86_REG_EBX));
    if (L.n_vdd < 64)
        L.vdd[L.n_vdd++] = (int) fn;
    switch (fn) {
        case 0x85: { /* VDD_GET_DISPLAY_CONFIG */
            uint32_t di = LIN(reg(UC_X86_REG_ES), reg(UC_X86_REG_EDI) & 0xffff);

            CHECK(reg(UC_X86_REG_ECX) >= 44, "VDD_GET_DISPLAY_CONFIG with ECX=%u", reg(UC_X86_REG_ECX));
            if (config.fail) {
                /* -1: the VDD could not read the registry; 85h (the function unchanged): no VDD. What the
                   structure holds then means nothing: a decoy 1024x768x16. */
                wr16(di + 24, 1024);
                wr16(di + 26, 768);
                wr8(di + 31, 16);
                set_reg(UC_X86_REG_EAX, (config.fail == 1) ? 0xffffffffu : 0x85u);
                break;
            }
            wr16(di + 0, (uint16_t) reg(UC_X86_REG_ECX));
            wr16(di + 24, config.x);
            wr16(di + 26, config.y);
            wr16(di + 28, config.dpi);
            wr8(di + 30, 1);
            wr8(di + 31, config.bpp);
            wr16(di + 32, config.refresh);
            wr16(di + 42, config.high_vert);
            set_reg(UC_X86_REG_EAX, 0);
            break;
        }
        case 0x80: /* VDD_DRIVER_REGISTER */
            L.register_bytes = reg(UC_X86_REG_ECX);
            L.restore_cb     = (reg(UC_X86_REG_ES) << 16) | (reg(UC_X86_REG_EDI) & 0xffff);
            /* -1: no VGA graphics in a window (they need planar memory beside the desktop) */
            CHECK(reg(UC_X86_REG_EDX) == 0xffffffffu, "VDD_DRIVER_REGISTER with EDX=%X", reg(UC_X86_REG_EDX));
            set_reg(UC_X86_REG_EAX, reg(UC_X86_REG_ECX));
            break;
        case 0x83: /* VDD_REGISTER_DISPLAY_DRIVER_INFO: the mini-VDD's REGISTER_DISPLAY_DRIVER */
            L.info_vram = reg(UC_X86_REG_ECX);
            CHECK(((reg(UC_X86_REG_EDX) & 0xffff) == 0) && ((reg(UC_X86_REG_EDI) & 0xffff) == 0),
                  "VDD_REGISTER_DISPLAY_DRIVER_INFO with DX=%X, DI=%X", reg(UC_X86_REG_EDX), reg(UC_X86_REG_EDI));
            break;
        default:
            break;
    }
}

static void
handle_dpmi(void)
{
    uint16_t ax = (uint16_t) reg(UC_X86_REG_AX);
    uint32_t eflags = reg(UC_X86_REG_EFLAGS) & ~1u;

    switch (ax) {
        case 0x0000:
            /* With a register window, DriverInit maps it first. */
            if (vga.regs_base && !L.regs_mapped) {
                L.regs_mapped = 1;
                set_ax(REGS_SEL);
                break;
            }
            L.dpmi_allocs++;
            set_ax(SCREEN_SEL);
            break;
        case 0x0001:
            break;
        case 0x0800:
            if (vga.regs_base && ((((reg(UC_X86_REG_BX) & 0xffff) << 16) | (reg(UC_X86_REG_CX) & 0xffff)) == vga.regs_base)) {
                set_reg(UC_X86_REG_BX, REGS_LINEAR >> 16);
                set_reg(UC_X86_REG_CX, REGS_LINEAR & 0xffff);
                break;
            }
            L.map_phys = (reg(UC_X86_REG_BX) << 16) | (reg(UC_X86_REG_CX) & 0xffff);
            L.map_size = (reg(UC_X86_REG_SI) << 16) | (reg(UC_X86_REG_DI) & 0xffff);
            set_reg(UC_X86_REG_BX, FB_LINEAR >> 16);
            set_reg(UC_X86_REG_CX, FB_LINEAR & 0xffff);
            break;
        case 0x0006: /* base of a segment (the shared block's linear address): the paragraph's */
            set_reg(UC_X86_REG_CX, ((reg(UC_X86_REG_BX) & 0xffff) << 4) >> 16);
            set_reg(UC_X86_REG_DX, ((reg(UC_X86_REG_BX) & 0xffff) << 4) & 0xffff);
            break;
        case 0x0007:
            if ((reg(UC_X86_REG_BX) & 0xffff) == REGS_SEL)
                break; /* the register window's: real mode puts it at REGS_LINEAR */
            CHECK(reg(UC_X86_REG_BX) == SCREEN_SEL, "DPMI set base of selector %X", reg(UC_X86_REG_BX));
            L.sel_base = (reg(UC_X86_REG_CX) << 16) | (reg(UC_X86_REG_DX) & 0xffff);
            break;
        case 0x0008:
            if ((reg(UC_X86_REG_BX) & 0xffff) == REGS_SEL)
                break;
            L.sel_limit = (reg(UC_X86_REG_CX) << 16) | (reg(UC_X86_REG_DX) & 0xffff);
            break;
        default:
            fail(__LINE__, "unexpected DPMI call %04X", ax);
            eflags |= 1;
            break;
    }
    set_reg(UC_X86_REG_EFLAGS, eflags);
}

static void
hook_intr(uc_engine *u, uint32_t intno, void *user)
{
    (void) u;
    (void) user;
    switch (intno) {
        case 0xfe:
            handle_import();
            break;
        case 0xfd:
            handle_vdd();
            break;
        case 0xfc:
            L.chained_2f++;
            break;
        case 0xfb:
            L.repaints++;
            break;
        case 0x2f: {
            uint16_t ax = (uint16_t) reg(UC_X86_REG_AX);

            if (L.n_int2f < 32)
                L.int2f[L.n_int2f++] = ax;
            if (ax == 0x4000) /* the driver must own the hardware before it programs it */
                L.trap_off_engine_state = !vga_disp_enabled(&vga) &&
                                          (vga_disp_value(&vga, VGPU98_SR_DISP_WIDTH, 2) == 0);
            if (ax == 0x4007) /* and turn the engine off before it gives the hardware back */
                L.trap_on_engine_on = vga_disp_enabled(&vga);
            if ((ax == 0x1684) && (reg(UC_X86_REG_BX) == 0x000a)) {
                set_reg(UC_X86_REG_ES, SEG_STUBS);
                set_reg(UC_X86_REG_DI, VDD_STUB);
            } else if (ax == 0x1683)
                set_reg(UC_X86_REG_BX, 1);
            break;
        }
        case 0x21: {
            uint16_t ax = (uint16_t) reg(UC_X86_REG_AX);

            if (ax == 0x352f) {
                set_reg(UC_X86_REG_ES, SEG_STUBS);
                set_reg(UC_X86_REG_BX, OLD2F_STUB);
            } else if (ax == 0x252f) {
                L.hooked_2f = (reg(UC_X86_REG_DS) << 16) | (reg(UC_X86_REG_DX) & 0xffff);
                L.set_2f_count++;
            } else
                fail(__LINE__, "unexpected INT 21h AX=%04X", ax);
            break;
        }
        case 0x10:
            L.int10_ax = (int) (reg(UC_X86_REG_AX) & 0xffff);
            break;
        case 0x31:
            handle_dpmi();
            break;
        default:
            fail(__LINE__, "unexpected INT %02Xh", intno);
            break;
    }
}

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

/* --- Running driver code ------------------------------------------------------------------------- */

/* A fresh machine with the driver loaded and the card's model reset. */
static void
machine(uint32_t vram_size)
{
    uc_hook h1, h2, h3;

    if (uc)
        uc_close(uc);
    if (uc_open(UC_ARCH_X86, UC_MODE_16, &uc) != UC_ERR_OK)
        die("cannot start Unicorn");
    if (uc_mem_map(uc, 0, MEM_SIZE, UC_PROT_ALL) != UC_ERR_OK)
        die("cannot map memory");
    uc_hook_add(uc, &h1, UC_HOOK_INTR, (void *) hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &h2, UC_HOOK_INSN, (void *) hook_in, NULL, 1, 0, UC_X86_INS_IN);
    uc_hook_add(uc, &h3, UC_HOOK_INSN, (void *) hook_out, NULL, 1, 0, UC_X86_INS_OUT);
    write_stubs();
    load_ne();
    vga_reset(&vga, vram_size);
    vga.lfb_base = 0xe0000000u;
    vga.debug_on = 1;
    vga.pel_mask = 0xff;
    memset(&L, 0, sizeof(L));
}

/* Far-calls CS:IP with the given argument words pushed (first pushed first,
   as Pascal does) and DS = the driver's data segment. Returns AX; checks that
   the callee popped its arguments. */
static uint32_t
call_far(uint32_t target, const uint16_t *words, int n_words, const char *what)
{
    uint8_t  stub[5] = { 0x9a, (uint8_t) target, (uint8_t) (target >> 8), (uint8_t) (target >> 16),
                         (uint8_t) (target >> 24) };
    uint16_t sp      = STACK_TOP;
    uc_err   err;
    int      i;

    for (i = 0; i < n_words; i++) {
        sp -= 2;
        wr16(LIN(SEG_STACK, sp), words[i]);
    }
    uc_mem_write(uc, LIN(SEG_STUBS, CALL_STUB), stub, sizeof(stub));
    set_reg(UC_X86_REG_SS, SEG_STACK);
    set_reg(UC_X86_REG_ESP, sp);
    set_reg(UC_X86_REG_CS, SEG_STUBS);
    set_reg(UC_X86_REG_DS, seg_para[2]);
    set_reg(UC_X86_REG_ES, 0);
    set_reg(UC_X86_REG_EBP, 0x1234);
    set_reg(UC_X86_REG_ESI, 0x56789abc);
    set_reg(UC_X86_REG_EDI, 0x13572468);
    err = uc_emu_start(uc, LIN(SEG_STUBS, CALL_STUB), LIN(SEG_STUBS, CALL_STUB + 5), 0, MAX_INSNS);
    if (err != UC_ERR_OK) {
        fail(__LINE__, "%s: emulation error %s at %04X:%04X", what, uc_strerror(err), reg(UC_X86_REG_CS),
             reg(UC_X86_REG_IP));
        return 0;
    }
    CHECK((reg(UC_X86_REG_SP) & 0xffff) == STACK_TOP, "%s: SP %04X after return (arguments not popped)", what,
          reg(UC_X86_REG_SP) & 0xffff);
    CHECK((reg(UC_X86_REG_EBP) == 0x1234) && (reg(UC_X86_REG_ESI) == 0x56789abc) &&
              (reg(UC_X86_REG_EDI) == 0x13572468) && (reg(UC_X86_REG_DS) == seg_para[2]),
          "%s: BP, ESI, EDI or DS not preserved", what);
    return reg(UC_X86_REG_EAX);
}

#define FARPTR(seg, off) ((uint16_t) (seg)), ((uint16_t) (off)) /* Pascal pushes the segment first */

static uint32_t
call_export(uint32_t ordinal, const uint16_t *words, int n_words, const char *what)
{
    return call_far(export_addr(ordinal), words, n_words, what);
}

static int
driver_init(void)
{
    uint32_t csip = f32(ne_hdr + 0x14);
    uint32_t ax;

    set_reg(UC_X86_REG_CX, 0);
    ax = call_far(((uint32_t) seg_para[csip >> 16] << 16) | (csip & 0xffff), NULL, 0, "DriverInit") & 0xffff;
    return (int) ax;
}

/* Enable(lpDevice, style, lpDeviceType, lpOutputFile, lpStuff) */
static uint32_t
enable(uint16_t buf, uint16_t style)
{
    uint16_t w[] = { FARPTR(SEG_BUFS, buf), style, 0, 0, 0, 0, 0, 0 };

    return call_export(5, w, 9, style ? "Enable (GDIINFO)" : "Enable (PDEVICE)") & 0xffff;
}

static uint32_t
disp_reg(uint8_t sr, int bytes)
{
    return vga_disp_value(&vga, sr, bytes);
}

/* --- Tests ---------------------------------------------------------------------------------------- */

typedef struct dmode_case_t {
    uint16_t w, h;
    uint8_t  bpp;
    uint16_t vbe;     /* expected SCRATCH, without the flag bits */
    uint8_t  refresh; /* expected rate for a default request */
} dmode_case_t;

static int
vdd_order_ok(const int *want, int n)
{
    int i, j = 0;

    for (i = 0; (i < L.n_vdd) && (j < n); i++) {
        if (L.vdd[i] == want[j])
            j++;
    }
    return j == n;
}

/* Starts the driver at a configured mode and checks everything Enable does. */
static void
test_enable(const dmode_case_t *m, uint32_t vram)
{
    char     what[64];
    uint32_t bytes = (uint32_t) m->w * (m->bpp / 8u) * m->h;
    uint32_t gi    = LIN(SEG_BUFS, BUF_GDIINFO);
    uint32_t pd    = LIN(SEG_BUFS, BUF_PDEVICE);
    uint32_t bi, i;
    uint16_t pdsize;
    int      bad = 0;
    static const int order[] = { 0x86, 0x83, 0x80, 0x87, 0x82 };

    snprintf(what, sizeof(what), "%ux%ux%u", m->w, m->h, m->bpp);
    machine(vram);
    config.x = m->w;
    config.y = m->h;
    config.bpp = m->bpp;
    config.dpi = 96;
    config.refresh = 0;
    config.high_vert = 0;
    config.fail = 0;

    CHECK(driver_init() == 1, "%s: DriverInit failed", what);
    CHECK(L.page_locks == 2, "%s: %d GlobalSmartPageLock calls, expected 2 (code, data)", what, L.page_locks);
    CHECK(!vga.disp_unlocked, "%s: DriverInit left the engine unlocked", what);

    /* GDIINFO pass. */
    for (i = 0; i < 0x200; i++)
        wr8(gi + i, 0xcc);
    CHECK(enable(BUF_GDIINFO, 1) == 110, "%s: Enable(GDIINFO) did not return the GDIINFO size", what);
    CHECK(L.dib_enable[1] == 1, "%s: DIB_Enable not called for the GDIINFO", what);
    CHECK((rd16(gi + 0) == 0x400) && (rd16(gi + 2) == 1) && (rd16(gi + 8) == m->w) && (rd16(gi + 10) == m->h) &&
              (rd16(gi + 12) == m->bpp) && (rd16(gi + 14) == 1),
          "%s: GDIINFO version, technology, resolution or depth wrong", what);
    CHECK((rd16(gi + 88) == 96) && (rd16(gi + 90) == 96), "%s: GDIINFO DPI", what);
    CHECK(rd16(gi + 4) == (uint16_t) ((m->w * 254u + 480) / 960), "%s: dpHorzSize %u", what, rd16(gi + 4));
    CHECK((rd16(gi + 48) == 254) && (rd16(gi + 52) == 96) && ((int16_t) rd16(gi + 54) == -96) &&
              (rd16(gi + 80) == 1440) && (rd16(gi + 84) == 96),
          "%s: GDIINFO mapping modes", what);
    CHECK(rd16(gi + 92) == 4, "%s: dpDCManage %04X", what, rd16(gi + 92));
    CHECK(rd16(gi + 94) == (0x0010 | 0x0080 | 0x0400 | 0x0800), "%s: dpCaps1 %04X", what, rd16(gi + 94));
    CHECK(rd16(gi + 28) == 0x1110 + 28, "%s: the DIB engine's capabilities were overwritten", what);
    pdsize = rd16(gi + 26);
    CHECK(pdsize == 48 + 40 + 1024, "%s: dpDEVICEsize %u", what, pdsize);
    if (m->bpp == 8)
        CHECK((rd16(gi + 24) == 20) && (rd16(gi + 104) == 256) && (rd16(gi + 106) == 20) && (rd16(gi + 108) == 18) &&
                  ((rd16(gi + 38) & 0x0341) == 0x0341),
              "%s: palette fields of the GDIINFO", what);
    else
        CHECK((rd16(gi + 24) == 0xffff) && (rd16(gi + 104) == 0) && ((rd16(gi + 38) & 0x0300) == 0x0200),
              "%s: direct-colour fields of the GDIINFO", what);

    /* PDEVICE pass. */
    for (i = 0; i < pdsize + 16u; i++)
        wr8(pd + i, 0);
    for (i = 0; i < bytes + 64; i += 4)
        wr32(FB_LINEAR + i, 0xaaaaaaaau);
    L.n_vdd = 0;
    CHECK(enable(BUF_PDEVICE, 0) == 1, "%s: Enable(PDEVICE) failed", what);
    CHECK((L.n_int2f >= 3) && (L.int2f[2] == 0x4000) && L.trap_off_engine_state,
          "%s: STOP_IO_TRAP not issued before the mode set", what);
    CHECK(vga_disp_enabled(&vga) && (disp_reg(VGPU98_SR_DISP_FORMAT, 1) == m->bpp) &&
              (disp_reg(VGPU98_SR_DISP_WIDTH, 2) == m->w) && (disp_reg(VGPU98_SR_DISP_HEIGHT, 2) == m->h) &&
              (disp_reg(VGPU98_SR_DISP_PITCH, 2) == m->w * (m->bpp / 8u)) &&
              (disp_reg(VGPU98_SR_DISP_START, 4) == 0) && (disp_reg(VGPU98_SR_DISP_BANK, 2) == 0),
          "%s: engine registers wrong", what);
    CHECK(disp_reg(VGPU98_SR_DISP_REFRESH, 1) == m->refresh, "%s: refresh %u, expected %u", what,
          disp_reg(VGPU98_SR_DISP_REFRESH, 1), m->refresh);
    CHECK(disp_reg(VGPU98_SR_DISP_SCRATCH, 2) == (uint32_t) (m->vbe | 0xc000), "%s: SCRATCH %04X", what,
          disp_reg(VGPU98_SR_DISP_SCRATCH, 2));
    CHECK(!vga.disp_unlocked && (vga.disp_locked_writes == 0), "%s: engine lock not kept", what);
    CHECK(vdd_order_ok(order, 5), "%s: VDD calls out of order (pre, driver info, register, post, save)", what);
    CHECK(L.info_vram == vram, "%s: VDD_REGISTER_DISPLAY_DRIVER_INFO with %X bytes of video memory", what,
          L.info_vram);
    CHECK(L.register_bytes == bytes, "%s: VDD_DRIVER_REGISTER with %u bytes, expected %u", what, L.register_bytes,
          bytes);
    CHECK((L.restore_cb >> 16) == seg_para[1], "%s: restore callback %08X not in the code segment", what,
          L.restore_cb);
    CHECK((L.map_phys == 0xe0000000u) && (L.map_size == vram) && (L.sel_base == FB_LINEAR) &&
              (L.sel_limit == vram - 1) && (L.dpmi_allocs == 1),
          "%s: framebuffer mapping (phys %X size %X base %X limit %X)", what, L.map_phys, L.map_size, L.sel_base,
          L.sel_limit);
    for (i = 0; i < bytes; i += 4)
        bad += rd32(FB_LINEAR + i) != 0;
    CHECK((bad == 0) && (rd32(FB_LINEAR + bytes) == 0xaaaaaaaau), "%s: screen not cleared (%d words) or cleared past it",
          what, bad);

    /* The PDEVICE and the bitmap header. */
    bi = pd + 48;
    CHECK((L.cdp_calls == 1) && (L.cdp_device == (((uint32_t) SEG_BUFS << 16) | BUF_PDEVICE)) &&
              (L.cdp_info == (((uint32_t) SEG_BUFS << 16) | (BUF_PDEVICE + 48))) &&
              (L.cdp_bits == ((uint32_t) SCREEN_SEL << 16)),
          "%s: CreateDIBPDevice arguments", what);
    CHECK((rd32(bi) == 40) && (rd32(bi + 4) == m->w) && (rd32(bi + 8) == m->h) && (rd16(bi + 12) == 1) &&
              (rd16(bi + 14) == m->bpp) && (rd32(bi + 16) == 0),
          "%s: BITMAPINFOHEADER", what);
    CHECK(L.cdp_flags == (0x8001 | (m->bpp == 16 ? 0x40 : 0) | (m->bpp == 8 ? 0x02 : 0)),
          "%s: PDEVICE flags %04X", what, L.cdp_flags);
    /* deBeginAccess is the driver's own (it waits for the GPU, then goes on to
       the DIB engine's); deEndAccess the DIB engine's. */
    CHECK(((rd32(pd + 36) >> 16) == SEG_FIRST) &&
              (rd32(pd + 40) == ((uint32_t) SEG_STUBS << 16 | (uint32_t) stub_for("DIBENG", 106))),
          "%s: deBeginAccess/deEndAccess are not the driver's and the DIB engine's", what);

    if (m->bpp == 8) {
        static const uint8_t low[3]  = { 0x80, 0x00, 0x00 }; /* entry 1: dark red */
        static const uint8_t white[3] = { 0xff, 0xff, 0xff };
        uint32_t             ct      = bi + 40;

        CHECK(L.translate_calls == 1, "%s: DIB_SetPaletteTranslateExt not called", what);
        CHECK((rd8(ct + 4 + 2) == 0x80) && (rd8(ct + 4 * 255) == 0xff) && (rd8(ct + 4 * 255 + 3) == 0x01) &&
                  (rd8(ct + 4 * 100 + 3) == 0x80) && (rd8(ct + 4 * 8 + 3) == 0x81),
              "%s: default colour table", what);
        CHECK((vga.dac[1][0] == low[0] >> 2) && (vga.dac[1][1] == low[1] >> 2) && (vga.dac[1][2] == low[2] >> 2) &&
                  (vga.dac[255][0] == white[0] >> 2) && (vga.dac[255][2] == white[2] >> 2) &&
                  (vga.dac[100][0] == 0),
              "%s: DAC not loaded from the colour table", what);
    } else
        CHECK(L.translate_calls == 0, "%s: palette translation in a direct-colour mode", what);
    CHECK((L.hooked_2f >> 16) == seg_para[1], "%s: INT 2Fh not hooked", what);
    CHECK(rd32(LIN(seg_para[1], 0)) == ((uint32_t) SEG_STUBS << 16 | OLD2F_STUB),
          "%s: previous INT 2Fh handler not saved", what);
}

/* After test_enable at 8 bpp: palette, cursor, thunks, switching, ReEnable, Disable. */
static void
test_running(void)
{
    uint32_t pd = LIN(SEG_BUFS, BUF_PDEVICE);
    uint32_t pal = LIN(SEG_BUFS, BUF_PALETTE);
    uint32_t hook = L.hooked_2f;
    uint32_t r;
    uint16_t pdev_words[2] = { FARPTR(SEG_BUFS, BUF_PDEVICE) };

    /* SetPalette(start 16, count 2, lpPalette): through the DIB engine to the DAC. */
    wr32(pal, 0x00102030);      /* red 30h, green 20h, blue 10h */
    wr32(pal + 4, 0x00fcfdfe);
    {
        uint16_t w[] = { 16, 2, FARPTR(SEG_BUFS, BUF_PALETTE) };

        call_export(22, w, 4, "SetPalette");
    }
    CHECK(L.setpal_calls == 1, "SetPalette did not reach the DIB engine");
    CHECK((vga.dac[16][0] == 0x30 >> 2) && (vga.dac[16][1] == 0x20 >> 2) && (vga.dac[16][2] == 0x10 >> 2) &&
              (vga.dac[17][0] == 0xfe >> 2),
          "SetPalette: DAC %02X %02X %02X", vga.dac[16][0], vga.dac[16][1], vga.dac[16][2]);

    /* CheckCursor. */
    call_export(104, NULL, 0, "CheckCursor");
    CHECK((L.checkcursor_calls == 1) && (L.checkcursor_pdev == (((uint32_t) SEG_BUFS << 16) | BUF_PDEVICE)),
          "CheckCursor did not reach the DIB engine with the PDEVICE");

    /* Thunks: arguments arrive untouched, the Ext ones with the extra argument last. */
    {
        uint16_t w[] = { 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888, 0x9999 };

        r = call_export(9, w, 8, "Pixel");
        CHECK((r & 0xffff) == 0x1234 && (L.fwd_args[0] == 0x88) && (L.fwd_args[14] == 0x11),
              "Pixel: not forwarded with its arguments");
        r = call_export(10, w, 9, "RealizeObject"); /* 18 bytes */
        CHECK((L.fwd_args[0] == (BUF_PDEVICE & 0xff)) && (L.fwd_args[2] == (SEG_BUFS & 0xff)) &&
                  (L.fwd_args[4] == 0x99) && (L.fwd_args[20] == 0x11),
              "RealizeObject: the PDEVICE is not the last argument");
        r = call_export(19, w, 5, "DibBlt"); /* 10 bytes + wPalettized */
        CHECK((L.fwd_args[0] == 1) && (L.fwd_args[1] == 0) && (L.fwd_args[2] == 0x55), "DibBlt: wPalettized");
        r = call_export(102, w, 2, "SetCursor");
        r = call_export(103, w, 2, "MoveCursor");
        CHECK((L.fwd_args[0] == (BUF_PDEVICE & 0xff)) && (L.fwd_args[4] == 0x22), "MoveCursor arguments");
    }

    /* GetDriverResourceID. */
    {
        uint16_t w[] = { 3, 0, 0 };

        r = call_export(450, w, 3, "GetDriverResourceID") & 0xffff;
        CHECK(r == 3, "GetDriverResourceID(3) at 96 dpi returned %u", r);
        w[0] = 1;
        r = call_export(450, w, 3, "GetDriverResourceID") & 0xffff;
        CHECK(r == 1, "GetDriverResourceID(1) returned %u", r);
    }

    /* INT 2Fh: other functions chain; switch out marks the PDEVICE busy. */
    {
        uint16_t w[1];

        set_reg(UC_X86_REG_AX, 0x1234);
        /* Simulate the interrupt: flags first, then the far call. */
        w[0] = 0x0202;
        set_reg(UC_X86_REG_EAX, 0x1600);
        call_far(hook, w, 1, "INT 2Fh 1600h");
    }
    CHECK(L.chained_2f == 1, "INT 2Fh 1600h did not reach the previous handler");
    {
        uint16_t w[1] = { 0x0203 };     /* CF set: the hook must clear it */

        set_reg(UC_X86_REG_EAX, 0x4001);
        call_far(hook, w, 1, "INT 2Fh 4001h");
        CHECK(rd16(pd + 28) & 0x10, "switch out: PDEVICE not busy");
        CHECK(!(rd16(LIN(SEG_STACK, STACK_TOP - 2)) & 1), "switch out: CF not cleared");
    }
    /* SetPalette while busy: the DAC is the DOS session's. */
    vga.dac[16][0] = 0x3f;
    {
        uint16_t w[] = { 16, 1, FARPTR(SEG_BUFS, BUF_PALETTE) };

        call_export(22, w, 4, "SetPalette (busy)");
    }
    CHECK(vga.dac[16][0] == 0x3f, "SetPalette changed the DAC while a DOS session owns it");

    /* The VDD calls back: the mode comes back over whatever the DOS session left. */
    {
        uint32_t cb = L.restore_cb;

        vga_out(&vga, 0x3c4, VGPU98_SR_DISP_KEY);
        vga_out(&vga, 0x3c5, VGPU98_DISP_KEY);
        vga_out(&vga, 0x3c4, VGPU98_SR_DISP_CONTROL);
        vga_out(&vga, 0x3c5, 0);
        vga_out(&vga, 0x3c4, VGPU98_SR_DISP_WIDTH);
        vga_out(&vga, 0x3c5, 0x40);
        vga_out(&vga, 0x3c4, VGPU98_SR_DISP_KEY);
        vga_out(&vga, 0x3c5, 0);
        vga.dac[1][0] = 0;
        L.n_vdd = 0;
        call_far(cb, NULL, 0, "RestoreDesktopMode");
        CHECK(vga_disp_enabled(&vga) && (disp_reg(VGPU98_SR_DISP_WIDTH, 2) == 640), "RestoreDesktopMode: mode not set");
        CHECK(vga.dac[1][0] == 0x80 >> 2, "RestoreDesktopMode: DAC not reloaded");
        CHECK(!(rd16(pd + 28) & 0x10), "RestoreDesktopMode: PDEVICE still busy");
        CHECK((L.n_vdd >= 1) && (L.vdd[L.n_vdd - 1] == 0x82), "RestoreDesktopMode: VDD_SAVE_DRIVER_STATE not called");
    }
    /* Switch in: repaint, held while USER disables repaints. */
    {
        uint16_t w[1] = { 0x0202 };
        uint16_t off[1] = { 0 };
        uint16_t on[1]  = { 1 };

        call_export(500, on, 1, "UserRepaintDisable(1)");
        set_reg(UC_X86_REG_EAX, 0x4002);
        call_far(hook, w, 1, "INT 2Fh 4002h");
        CHECK(L.repaints == 0, "repaint while USER holds repaints off");
        call_export(500, off, 1, "UserRepaintDisable(0)");
        CHECK(L.repaints == 1, "held repaint not done when USER allows it again");
    }

    /* ReEnable to 1024x768: the same PDEVICE, a new mode, the cursor excluded. */
    config.x = 1024;
    config.y = 768;
    L.n_vdd  = 0;
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), FARPTR(SEG_BUFS, BUF_GDI2) };

        r = call_export(31, w, 4, "ReEnable");
    }
    CHECK((r & 0xffff) == 1, "ReEnable to 1024x768 failed");
    CHECK((disp_reg(VGPU98_SR_DISP_WIDTH, 2) == 1024) && (disp_reg(VGPU98_SR_DISP_HEIGHT, 2) == 768) &&
              (disp_reg(VGPU98_SR_DISP_SCRATCH, 2) == (0x105 | 0xc000)),
          "ReEnable: engine not at 1024x768");
    CHECK((L.begin_access == 1) && (L.end_access == 1) && (L.access_flags == 0x0008),
          "ReEnable: cursor not excluded around the change");
    CHECK((rd16(LIN(SEG_BUFS, BUF_GDI2) + 8) == 1024) && (rd16(LIN(SEG_BUFS, BUF_GDI2) + 10) == 768),
          "ReEnable: GDIINFO not filled for the new mode");
    CHECK(L.register_bytes == 1024u * 768u, "ReEnable: VDD not told the new frame size");
    CHECK(L.set_2f_count == 1, "ReEnable hooked INT 2Fh again");
    CHECK(rd8(LIN(SEG_BUFS, BUF_PDEVICE) + 48 + 40 + 4 * 16 + 2) == 0x30, "ReEnable: the palette was reset");

    /* ReEnable to another depth: 1920x1440 at 32 bpp, in the same PDEVICE. */
    config.x   = 1920;
    config.y   = 1440;
    config.bpp = 32;
    L.cdp_calls = 0;
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), FARPTR(SEG_BUFS, BUF_GDI2) };

        r = call_export(31, w, 4, "ReEnable (32 bpp)");
    }
    CHECK(((r & 0xffff) == 1) && (disp_reg(VGPU98_SR_DISP_WIDTH, 2) == 1920) &&
              (disp_reg(VGPU98_SR_DISP_FORMAT, 1) == 32) && (L.cdp_calls == 1) && (L.cdp_flags == 0x8001),
          "ReEnable to 1920x1440x32: AX=%u, width %u, depth %u, flags %04X", r & 0xffff,
          disp_reg(VGPU98_SR_DISP_WIDTH, 2), disp_reg(VGPU98_SR_DISP_FORMAT, 1), L.cdp_flags);
    CHECK(rd16(LIN(SEG_BUFS, BUF_GDI2) + 12) == 32, "ReEnable: GDIINFO depth");

    /* Back to 256 colours: the colour table, unused at 32 bpp, starts from the static colours again. */
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), FARPTR(SEG_BUFS, BUF_GDI2) };
        uint32_t ct  = LIN(SEG_BUFS, BUF_PDEVICE) + 48 + 40;
        uint32_t i;

        for (i = 0; i < 1024; i++)
            wr8(ct + i, 0x55);
        config.x   = 800;
        config.y   = 600;
        config.bpp = 8;
        r = call_export(31, w, 4, "ReEnable (back to 8 bpp)");
        CHECK(((r & 0xffff) == 1) && (disp_reg(VGPU98_SR_DISP_FORMAT, 1) == 8) && (rd8(ct + 4 + 2) == 0x80) &&
                  (rd8(ct + 4 * 255) == 0xff) && (rd8(ct + 4 * 100 + 3) == 0x80) && (vga.dac[1][0] == 0x80 >> 2) &&
                  (vga.dac[255][1] == 0x3f),
              "ReEnable from 32 to 8 bpp: colour table or DAC not the static colours");
    }

    /* Disable. */
    L.n_int2f = 0;
    L.n_vdd   = 0;
    r = call_export(4, pdev_words, 2, "Disable");
    CHECK((r & 0xffff) == 1, "Disable returned %u", r & 0xffff);
    CHECK(rd16(pd + 28) & 0x10, "Disable: PDEVICE not busy");
    CHECK((L.n_int2f >= 1) && (L.int2f[0] == 0x4007), "Disable: START_IO_TRAP not issued");
    CHECK(!L.trap_on_engine_on, "Disable: engine still on when the hardware went back to the VDD");
    CHECK((L.n_vdd >= 1) && (L.vdd[0] == 0x81), "Disable: VDD_DRIVER_UNREGISTER not called");
    CHECK(!vga_disp_enabled(&vga) && (L.int10_ax == 0x0003), "Disable: engine on or no text mode set");
    CHECK((L.set_2f_count == 2) && (L.hooked_2f == ((uint32_t) SEG_STUBS << 16 | OLD2F_STUB)),
          "Disable: INT 2Fh not given back");
    call_export(104, NULL, 0, "CheckCursor (disabled)");
    CHECK(L.checkcursor_calls == 1, "CheckCursor reached the DIB engine after Disable");
}

static void
test_validate(void)
{
    static const struct {
        uint16_t bpp, w, h;
        uint32_t vram;
        uint16_t want;
    } cases[] = {
        { 8, 640, 480, 16u << 20, 0 },     { 16, 1024, 768, 16u << 20, 0 },   { 32, 1920, 1440, 16u << 20, 0 },
        { 32, 1920, 1440, 8u << 20, 2 },   { 32, 1920, 1080, 8u << 20, 2 },   { 24, 1024, 768, 16u << 20, 3 },
        { 15, 1024, 768, 16u << 20, 3 },   { 4, 640, 480, 16u << 20, 0 },     { 4, 800, 600, 16u << 20, 4 },
        { 16, 1280, 720, 16u << 20, 0 },   { 16, 1284, 720, 16u << 20, 4 },   { 16, 2048, 1536, 32u << 20, 4 },
        { 8, 320, 200, 16u << 20, 0 },     { 8, 336, 200, 16u << 20, 4 },
    };
    uint32_t ord = resident_ordinal("VALIDATEMODE");
    size_t   i;

    CHECK(ord == 700, "VALIDATEMODE is not a resident name at ordinal 700 (found %u)", ord);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t vm = LIN(SEG_BUFS, BUF_VALMODE);
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_VALMODE) };
        uint32_t r;

        machine(cases[i].vram); /* ValidateMode may run before DriverInit */
        wr16(vm + 0, 8);
        wr16(vm + 2, cases[i].bpp);
        wr16(vm + 4, cases[i].w);
        wr16(vm + 6, cases[i].h);
        r = call_export(700, w, 2, "ValidateMode") & 0xffff;
        CHECK(r == cases[i].want, "ValidateMode %ux%ux%u with %u MB: %u, expected %u", cases[i].w, cases[i].h,
              cases[i].bpp, cases[i].vram >> 20, r, cases[i].want);
    }
    /* No card: the wrong driver. */
    machine(16u << 20);
    vga.lfb_base = 0;
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_VALMODE) };

        wr16(LIN(SEG_BUFS, BUF_VALMODE) + 2, 8);
        CHECK((call_export(700, w, 2, "ValidateMode (no BAR1)") & 0xffff) == 1,
              "ValidateMode without a framebuffer did not answer NO_WRONGDRV");
        CHECK(driver_init() == 0, "DriverInit loaded without a framebuffer");
    }
}

static void
test_config_fallbacks(void)
{
    /* The VDD has no configuration (it failed, or answered as no VDD): 640x480 at 8 bpp. */
    for (config.fail = 1; config.fail <= 2; config.fail++) {
        machine(16u << 20);
        CHECK(driver_init() == 1, "DriverInit without a configuration");
        CHECK(enable(BUF_GDIINFO, 1) == 110, "Enable(GDIINFO) without a configuration");
        CHECK((rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 8) == 640) && (rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 12) == 8),
              "no configuration (VDD answer %d): not 640x480x8", config.fail);
    }
    config.fail = 0;

    /* 24 bpp in the registry: 8 bpp. A mode too big for the memory: 640x480. */
    machine(8u << 20);
    config.x = 1920; config.y = 1440; config.bpp = 24; config.dpi = 120; config.refresh = 0; config.high_vert = 0;
    CHECK(driver_init() == 1, "DriverInit with a 24 bpp configuration");
    enable(BUF_GDIINFO, 1);
    CHECK(rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 12) == 8, "24 bpp configuration: depth %u",
          rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 12));
    CHECK(rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 88) == 120, "120 dpi not reported");
    {
        uint16_t w[] = { 3, 0, 0 };

        CHECK((call_export(450, w, 3, "GetDriverResourceID") & 0xffff) == 2003, "large fonts: OEM fonts not 2003");
    }

    /* Refresh: a listed rate is used; "optimal" takes the fastest under the monitor's limit. */
    machine(16u << 20);
    config.x = 1024; config.y = 768; config.bpp = 16; config.dpi = 96; config.refresh = 85; config.high_vert = 0;
    driver_init();
    enable(BUF_GDIINFO, 1);
    enable(BUF_PDEVICE, 0);
    CHECK(disp_reg(VGPU98_SR_DISP_REFRESH, 1) == 85, "1024x768 at 85 Hz: %u", disp_reg(VGPU98_SR_DISP_REFRESH, 1));
    machine(16u << 20);
    config.refresh = 0xffff; config.high_vert = 100;
    driver_init();
    enable(BUF_GDIINFO, 1);
    enable(BUF_PDEVICE, 0);
    CHECK(disp_reg(VGPU98_SR_DISP_REFRESH, 1) == 100, "optimal under 100 Hz: %u", disp_reg(VGPU98_SR_DISP_REFRESH, 1));
    machine(16u << 20);
    config.refresh = 77; config.high_vert = 0;
    driver_init();
    enable(BUF_GDIINFO, 1);
    enable(BUF_PDEVICE, 0);
    CHECK(disp_reg(VGPU98_SR_DISP_REFRESH, 1) == 60, "unlisted 77 Hz: %u", disp_reg(VGPU98_SR_DISP_REFRESH, 1));
    /* A size outside the table: allowed, at 60 Hz, no VBE number. */
    machine(16u << 20);
    config.x = 1280; config.y = 720; config.refresh = 0;
    driver_init();
    enable(BUF_GDIINFO, 1);
    CHECK(enable(BUF_PDEVICE, 0) == 1, "1280x720 (not in the table) refused");
    CHECK((disp_reg(VGPU98_SR_DISP_REFRESH, 1) == 60) && (disp_reg(VGPU98_SR_DISP_SCRATCH, 2) == 0xc000),
          "1280x720: refresh %u, SCRATCH %04X", disp_reg(VGPU98_SR_DISP_REFRESH, 1), disp_reg(VGPU98_SR_DISP_SCRATCH, 2));
}

/* Control(lpDevice, wFunction, lpIn, lpOut), its pointers in SEG_BUFS (0:
   NULL); AX. */
static uint32_t
control(uint16_t function, uint16_t in, uint16_t out, const char *what)
{
    uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), function, FARPTR(in ? SEG_BUFS : 0, in),
                     FARPTR(out ? SEG_BUFS : 0, out) };

    return call_export(3, w, 7, what) & 0xffff;
}

/* Control's escapes: QUERYESCSUPPORT for DirectDraw's DCICOMMAND and for
   the Glide DLLs' escape, which answers where
   the shared block is for the question asked with room for the answer, and
   any other escape handed to the DIB engine. */
static void
test_escape(void)
{
    const uint16_t q_buf = BUF_ARGS, i_buf = BUF_ARGS + 0x10, n_buf = BUF_ARGS + 0x40; /* the answer is 32 bytes */
    const uint32_t query = LIN(SEG_BUFS, q_buf);
    const uint32_t info  = LIN(SEG_BUFS, i_buf);
    uint32_t       r, shared;
    int            i;

    machine(16u << 20);
    CHECK(driver_init() == 1, "DriverInit before the escapes");
    wr16(LIN(SEG_BUFS, n_buf), VGPU98_ESC_INFO);
    r = control(8 /* QUERYESCSUPPORT */, n_buf, 0, "Control (QUERYESCSUPPORT, VGPU98_ESC_INFO)");
    CHECK((r == 1) && ((reg(UC_X86_REG_DX) & 0xffff) == 0), "QUERYESCSUPPORT for VGPU98_ESC_INFO: %04X:%04X, expected 1",
          reg(UC_X86_REG_DX) & 0xffff, r);
    wr16(LIN(SEG_BUFS, n_buf), 3075 /* DCICOMMAND */);
    r = control(8, n_buf, 0, "Control (QUERYESCSUPPORT, DCICOMMAND)");
    CHECK(r == 0x00ff, "QUERYESCSUPPORT for DCICOMMAND: %04X, expected 00FFh (a DirectDraw HAL)", r);
    wr16(LIN(SEG_BUFS, n_buf), 0x1234);
    L.fwd_id = 0;
    r = control(8, n_buf, 0, "Control (QUERYESCSUPPORT, another escape)");
    CHECK((r == 0x1234) && !strcmp(imports[L.fwd_id].name, "DIB_Control"),
          "QUERYESCSUPPORT for another escape was not handed to DIB_Control");
    L.fwd_id = 0;
    r = control(0x1234, 0, 0, "Control (another escape)");
    /* Its arguments as Control had them: lpOut, lpIn, then the function. */
    CHECK((r == 0x1234) && !strcmp(imports[L.fwd_id].name, "DIB_Control") && (L.fwd_args[8] == 0x34) &&
              (L.fwd_args[9] == 0x12),
          "another escape was not handed to DIB_Control with its arguments");
    /* OpenGL's escape only while the GPU
       draws OpenGL: here, with no register window to read PROFILE from,
       the DIB engine answers both the question and the escape. */
    wr16(LIN(SEG_BUFS, n_buf), 0x1101 /* OPENGL_GETINFO */);
    L.fwd_id = 0;
    control(8, n_buf, 0, "Control (QUERYESCSUPPORT, OPENGL_GETINFO)");
    CHECK(L.fwd_id && !strcmp(imports[L.fwd_id].name, "DIB_Control"),
          "QUERYESCSUPPORT for OPENGL_GETINFO without OpenGL was not handed to DIB_Control");
    L.fwd_id = 0;
    for (i = 0; i < 16; i++)
        wr8(info + (uint32_t) i, 0xaa);
    control(0x1101, 0, i_buf, "Control (OPENGL_GETINFO)");
    CHECK(L.fwd_id && !strcmp(imports[L.fwd_id].name, "DIB_Control") && (rd32(info) == 0xaaaaaaaa),
          "OPENGL_GETINFO without OpenGL was answered");
    /* The ICD's cursor escape likewise */
    for (i = 0; i < 10; i++)
        wr8(info + (uint32_t) i, 0);
    wr16(info, 1);
    L.fwd_id = 0;
    control(VGPU98_ESC_ACCESS, i_buf, 0, "Control (VGPU98_ESC_ACCESS)");
    CHECK(L.fwd_id && !strcmp(imports[L.fwd_id].name, "DIB_Control"), "VGPU98_ESC_ACCESS without OpenGL was not handed to DIB_Control");

    wr32(query, ESC_INFO_MAGIC);
    wr32(query + 4, sizeof(vgpu98_escinfo_t));
    /* The answer and the 16 bytes after it (up to n_buf): those must stay. */
    for (i = 0; i < (int) sizeof(vgpu98_escinfo_t) + 16; i++)
        wr8(info + (uint32_t) i, 0xaa);
    r = control(VGPU98_ESC_INFO, q_buf, i_buf, "Control (VGPU98_ESC_INFO)");
    for (i = (int) sizeof(vgpu98_escinfo_t); i < (int) sizeof(vgpu98_escinfo_t) + 16; i++)
        CHECK(rd8(info + (uint32_t) i) == 0xaa, "VGPU98_ESC_INFO wrote past its 32-byte answer, at byte %d", i);
    CHECK(r == 1, "VGPU98_ESC_INFO returned %04X, expected 1", r);
    CHECK((rd32(info + offsetof(vgpu98_escinfo_t, magic)) == ESC_INFO_MAGIC) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, size)) == sizeof(vgpu98_escinfo_t)) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, sb_version)) == SB_VERSION),
          "VGPU98_ESC_INFO's answer: magic %08X, size %u, version %u", rd32(info), rd32(info + 4), rd32(info + 12));
    shared = rd32(info + offsetof(vgpu98_escinfo_t, shared_linear));
    CHECK((shared > LIN(seg_para[2], 0)) && (shared < LIN(seg_para[2], 0) + 0x10000) &&
              (rd32(shared + offsetof(vgpu98_shared_t, magic)) == SB_MAGIC) &&
              (rd32(shared + offsetof(vgpu98_shared_t, version)) == SB_VERSION) &&
              (rd32(shared + offsetof(vgpu98_shared_t, size)) == sizeof(vgpu98_shared_t)),
          "VGPU98_ESC_INFO: the address %08X does not hold the shared block", shared);
    /* The build stamp, NUL-padded. */
    {
        char stamp[sizeof(((vgpu98_escinfo_t *) 0)->stamp) + 1];

        for (i = 0; i < (int) sizeof(stamp) - 1; i++)
            stamp[i] = (char) rd8(info + offsetof(vgpu98_escinfo_t, stamp) + (uint32_t) i);
        stamp[sizeof(stamp) - 1] = 0;
        CHECK(!strcmp(stamp, VGPU98_BUILD_STAMP), "VGPU98_ESC_INFO's stamp '%s', expected '%s'", stamp, VGPU98_BUILD_STAMP);
        for (i = (int) strlen(VGPU98_BUILD_STAMP); i < (int) sizeof(stamp) - 1; i++)
            CHECK(rd8(info + offsetof(vgpu98_escinfo_t, stamp) + (uint32_t) i) == 0, "VGPU98_ESC_INFO's stamp is not padded with NULs");
    }
    /* A question with room for the head alone (the first Glide DLLs'):
       the head, its size 16, nothing past it. */
    for (i = 0; i < (int) sizeof(vgpu98_escinfo_t); i++)
        wr8(info + (uint32_t) i, 0xaa);
    wr32(query + 4, ESC_INFO_HEAD);
    r = control(VGPU98_ESC_INFO, q_buf, i_buf, "Control (VGPU98_ESC_INFO, the head)");
    CHECK((r == 1) && (rd32(info + offsetof(vgpu98_escinfo_t, magic)) == ESC_INFO_MAGIC) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, size)) == ESC_INFO_HEAD) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, shared_linear)) == shared) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, stamp)) == 0xaaaaaaaa),
          "VGPU98_ESC_INFO for its head alone: returned %04X, size %u, or it wrote past the head", r, rd32(info + 4));
    /* Room for more than the head but not the stamp: the head alone. */
    for (i = 0; i < (int) sizeof(vgpu98_escinfo_t); i++)
        wr8(info + (uint32_t) i, 0xaa);
    wr32(query + 4, ESC_INFO_HEAD + 4);
    r = control(VGPU98_ESC_INFO, q_buf, i_buf, "Control (VGPU98_ESC_INFO, 20 bytes)");
    CHECK((r == 1) && (rd32(info + offsetof(vgpu98_escinfo_t, magic)) == ESC_INFO_MAGIC) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, size)) == ESC_INFO_HEAD) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, shared_linear)) == shared) &&
              (rd32(info + offsetof(vgpu98_escinfo_t, stamp)) == 0xaaaaaaaa),
          "VGPU98_ESC_INFO with room for 20 bytes: returned %04X, size %u, or it wrote past the head", r,
          rd32(info + 4));
    /* Refused, nothing written: without an output buffer, without the
       question, with another question, with room too small. */
    for (i = 0; i < (int) sizeof(vgpu98_escinfo_t); i++)
        wr8(info + (uint32_t) i, 0xaa);
    r = control(VGPU98_ESC_INFO, q_buf, 0, "Control (VGPU98_ESC_INFO, no output)");
    CHECK(r == 0xffff, "VGPU98_ESC_INFO without an output buffer returned %04X, expected FFFFh", r);
    r = control(VGPU98_ESC_INFO, 0, i_buf, "Control (VGPU98_ESC_INFO, no input)");
    CHECK(r == 0xffff, "VGPU98_ESC_INFO without its question returned %04X, expected FFFFh", r);
    wr32(query, ESC_INFO_MAGIC + 1);
    r = control(VGPU98_ESC_INFO, q_buf, i_buf, "Control (VGPU98_ESC_INFO, another question)");
    CHECK(r == 0xffff, "VGPU98_ESC_INFO with another question returned %04X, expected FFFFh", r);
    wr32(query, ESC_INFO_MAGIC);
    wr32(query + 4, ESC_INFO_HEAD - 1);
    r = control(VGPU98_ESC_INFO, q_buf, i_buf, "Control (VGPU98_ESC_INFO, too small)");
    CHECK(r == 0xffff, "VGPU98_ESC_INFO with too small an output returned %04X, expected FFFFh", r);
    CHECK(rd32(info) == 0xaaaaaaaa, "a refused VGPU98_ESC_INFO wrote its answer");
}

/* --- The hardware cursor --------------------------------------------- */

#define REG(r)          (REGS_LINEAR + (r))
#define MOUSETRAILS_ESC 39 /* Control: pointer trails */
#define CURSOR_SLOT(vram, n) ((vram) - 0x40000u + 0x20000u + (n) * 0x8000u) /* the driver's two patterns */

/* A CURSORSHAPE at SEG_BUFS:buf, w x h with its hot spot, 1 bit a pixel or
   the screen's bpp, its masks made from seed; rows in whole words, as
   Windows' bitmaps. */
static void
make_shape(uint16_t buf, int w, int h, int hx, int hy, int bpp, uint8_t seed)
{
    const uint32_t s   = LIN(SEG_BUFS, buf);
    const int      wb  = ((w + 15) / 16) * 2;
    const int      img = (bpp == 1) ? wb : ((w * (bpp / 8) + 1) & ~1);
    int            i;

    wr16(s + 0, (uint16_t) hx);
    wr16(s + 2, (uint16_t) hy);
    wr16(s + 4, (uint16_t) w);
    wr16(s + 6, (uint16_t) h);
    wr16(s + 8, (uint16_t) wb);
    wr8(s + 10, 1);
    wr8(s + 11, (uint8_t) bpp);
    for (i = 0; i < wb * h; i++)
        wr8(s + 12 + (uint32_t) i, (uint8_t) (seed * 37 + i * 11));
    for (i = 0; i < img * h; i++)
        wr8(s + 12 + (uint32_t) (wb * h + i), (uint8_t) (seed * 53 + i * 7 + 1));
}

/* Pixels of the card's pattern at video memory offset `slot` that are not
   what the shape at SEG_BUFS:buf makes at `bytes` a pixel: the AND mask
   padded to 64x64 with ones, the image padded with zeros, a mono image's
   set bits all ones. */
static int
pattern_wrong(uint32_t slot, uint16_t buf, uint32_t bytes)
{
    const uint32_t s   = LIN(SEG_BUFS, buf);
    const uint32_t fb  = FB_LINEAR + slot;
    const int      w   = rd16(s + 4), h = rd16(s + 6), wb = rd16(s + 8), bpp = rd8(s + 11);
    const int      img = (bpp == 1) ? wb : ((w * (bpp / 8) + 1) & ~1);
    const uint32_t ia  = s + 12 + (uint32_t) (wb * h);
    int            bad = 0, x, y;

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            const int in      = (x < w) && (y < h);
            const int and_bit = in ? ((rd8(s + 12 + (uint32_t) (y * wb + x / 8)) >> (7 - x % 8)) & 1) : 1;
            uint32_t  want = 0, got = 0, k;

            if (((rd8(fb + (uint32_t) (y * 8 + x / 8)) >> (7 - x % 8)) & 1) != and_bit)
                bad++;
            if (in && (bpp == 1))
                want = ((rd8(ia + (uint32_t) (y * wb + x / 8)) >> (7 - x % 8)) & 1) ? 0xffffffffu : 0;
            else if (in)
                for (k = 0; k < bytes; k++)
                    want |= (uint32_t) rd8(ia + (uint32_t) (y * img) + (uint32_t) x * bytes + k) << (8 * k);
            for (k = 0; k < bytes; k++)
                got |= (uint32_t) rd8(fb + 0x200 + ((uint32_t) (y * 64 + x)) * bytes + k) << (8 * k);
            if (bytes < 4)
                want &= (1u << (8 * bytes)) - 1;
            bad += got != want;
        }
    }
    return bad;
}

static uint32_t
set_cursor(uint16_t buf, const char *what)
{
    uint16_t w[] = { FARPTR(buf ? SEG_BUFS : 0, buf) };

    return call_export(102, w, 2, what) & 0xffff;
}

static void
move_cursor(uint16_t x, uint16_t y)
{
    uint16_t w[] = { x, y };

    call_export(103, w, 2, "MoveCursor");
}

static void
re_enable(uint8_t bpp, const char *what)
{
    uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), FARPTR(SEG_BUFS, BUF_GDI2) };

    config.bpp = bpp;
    CHECK((call_export(31, w, 4, what) & 0xffff) == 1, "%s failed", what);
}

/* The card's cursor: with a register window whose PROFILE has CURSOR, Windows'
   shapes go to the card; pointer trails, shapes it cannot show and a card
   without PROFILE.CURSOR keep the DIB engine's. */
static void
test_cursor(void)
{
    const uint32_t vram  = 16u << 20;
    const uint32_t slot0 = CURSOR_SLOT(vram, 0), slot1 = CURSOR_SLOT(vram, 1);
    const uint32_t data  = (uint32_t) seg_para[2] << 16;
    uint32_t       base;

    machine(vram);
    vga.regs_base = REGS_PHYS;
    wr32(REG(VGPU98_REG_PROFILE), VGPU98_PROFILE); /* OpenGL too, for the ICD's escape */
    config.x = 640; config.y = 480; config.bpp = 8; config.dpi = 96; config.refresh = 0; config.high_vert = 0;
    config.fail = 0;
    CHECK(driver_init() == 1, "cursor: DriverInit");
    CHECK(L.regs_mapped, "cursor: the register window was not mapped");
    enable(BUF_GDIINFO, 1);
    CHECK(enable(BUF_PDEVICE, 0) == 1, "cursor: Enable");

    /* Before any shape, MoveCursor tells the DIB engine. */
    move_cursor(100, 50);
    CHECK((L.movecursor_calls == 1) && (L.movecursor_x == 100) && (L.movecursor_y == 50), "cursor: the first MoveCursor");

    /* A mono shape: to the card, in slot 1, placed by its hot spot; the DIB engine's cursor hidden once. */
    make_shape(BUF_SHAPE, 32, 32, 3, 5, 1, 1);
    L.setcursor_calls = 0;
    CHECK(set_cursor(BUF_SHAPE, "SetCursor (mono)") == 1, "cursor: SetCursor did not answer 1");
    CHECK((L.setcursor_calls == 1) && (L.setcursor_shape == 0), "cursor: the DIB engine's cursor not hidden (%d calls, shape %08X)",
          L.setcursor_calls, L.setcursor_shape);
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) && (rd32(REG(VGPU98_REG_CURSOR_BASE)) == slot1) &&
              (rd32(REG(VGPU98_REG_CURSOR_POS)) == (97u | (45u << 16))),
          "cursor: CONTROL %X, BASE %X (expected %X), POS %08X", rd32(REG(VGPU98_REG_CURSOR_CONTROL)),
          rd32(REG(VGPU98_REG_CURSOR_BASE)), slot1, rd32(REG(VGPU98_REG_CURSOR_POS)));
    CHECK(pattern_wrong(slot1, BUF_SHAPE, 1) == 0, "cursor: the mono pattern at 8 bpp (%d pixels wrong)", pattern_wrong(slot1, BUF_SHAPE, 1));

    /* MoveCursor: the register alone, the corner past the screen's edge signed. */
    L.movecursor_calls = 0;
    move_cursor(10, 2);
    CHECK((rd32(REG(VGPU98_REG_CURSOR_POS)) == 0xfffd0007u) && (L.movecursor_calls == 0),
          "cursor: MoveCursor wrote POS %08X, %d DIB engine calls", rd32(REG(VGPU98_REG_CURSOR_POS)), L.movecursor_calls);

    /* A colour shape at 8 bpp, into the other slot, without the DIB engine; then an odd size, in the first again. */
    make_shape(BUF_SHAPE2, 32, 32, 0, 0, 8, 2);
    L.setcursor_calls = 0;
    set_cursor(BUF_SHAPE2, "SetCursor (colour)");
    CHECK((L.setcursor_calls == 0) && (rd32(REG(VGPU98_REG_CURSOR_BASE)) == slot0) &&
              (rd32(REG(VGPU98_REG_CURSOR_POS)) == (10u | (2u << 16))),
          "cursor: the colour shape: %d DIB engine calls, BASE %X, POS %08X", L.setcursor_calls,
          rd32(REG(VGPU98_REG_CURSOR_BASE)), rd32(REG(VGPU98_REG_CURSOR_POS)));
    CHECK(pattern_wrong(slot0, BUF_SHAPE2, 1) == 0, "cursor: the colour pattern at 8 bpp");
    make_shape(BUF_SHAPE, 20, 7, 1, 1, 1, 3);
    set_cursor(BUF_SHAPE, "SetCursor (20x7)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_BASE)) == slot1) && (pattern_wrong(slot1, BUF_SHAPE, 1) == 0),
          "cursor: a 20x7 shape not padded transparent");

    /* The ICD's escape: nothing to keep out, the cursor is not in the window's pixels. */
    {
        const uint32_t ea = LIN(SEG_BUFS, BUF_ARGS);

        wr16(ea + 0, 1);
        wr16(ea + 2, 0);
        wr16(ea + 4, 0);
        wr16(ea + 6, 639);
        wr16(ea + 8, 479);
        L.begin_access = 0;
        CHECK((control(VGPU98_ESC_ACCESS, BUF_ARGS, 0, "Control (VGPU98_ESC_ACCESS, the card's cursor)") == 0) &&
                  (L.begin_access == 0),
              "cursor: VGPU98_ESC_ACCESS kept the card's cursor out");
    }

    /* Pointer trails: the DIB engine's, told the place and given the shape, and the escape goes on to it. */
    wr16(LIN(SEG_BUFS, BUF_ARGS), 3);
    L.setcursor_calls = L.movecursor_calls = 0;
    L.fwd_id = 0;
    control(MOUSETRAILS_ESC, BUF_ARGS, 0, "Control (MOUSETRAILS 3)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0) && (L.movecursor_calls == 1) && (L.movecursor_x == 10) &&
              (L.movecursor_y == 2) && (L.setcursor_calls == 1) && ((L.setcursor_shape & 0xffff0000u) == data) &&
              !strcmp(imports[L.fwd_id].name, "DIB_Control"),
          "cursor: trails on: CONTROL %X, %d moves, %d shapes (%08X), last call %s", rd32(REG(VGPU98_REG_CURSOR_CONTROL)),
          L.movecursor_calls, L.setcursor_calls, L.setcursor_shape, imports[L.fwd_id].name);
    move_cursor(20, 30);
    set_cursor(BUF_SHAPE, "SetCursor (trails)");
    CHECK((L.movecursor_calls == 2) && (L.setcursor_shape == (((uint32_t) SEG_BUFS << 16) | BUF_SHAPE)) &&
              (rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0),
          "cursor: with trails, a shape or a move reached the card");
    wr16(LIN(SEG_BUFS, BUF_ARGS), 0);
    control(MOUSETRAILS_ESC, BUF_ARGS, 0, "Control (MOUSETRAILS 0)");
    CHECK((L.setcursor_shape == 0) && (rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) &&
              (rd32(REG(VGPU98_REG_CURSOR_BASE)) == slot0) && (rd32(REG(VGPU98_REG_CURSOR_POS)) == (19u | (29u << 16))) &&
              (pattern_wrong(slot0, BUF_SHAPE, 1) == 0),
          "cursor: trails off: the card's cursor not back (CONTROL %X, BASE %X, POS %08X)",
          rd32(REG(VGPU98_REG_CURSOR_CONTROL)), rd32(REG(VGPU98_REG_CURSOR_BASE)), rd32(REG(VGPU98_REG_CURSOR_POS)));

    /* A shape too wide: the DIB engine's, told the place; then the card's again. */
    make_shape(BUF_SHAPE2, 65, 32, 0, 0, 1, 4);
    L.movecursor_calls = 0;
    set_cursor(BUF_SHAPE2, "SetCursor (65x32)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0) && (L.movecursor_calls == 1) &&
              (L.setcursor_shape == (((uint32_t) SEG_BUFS << 16) | BUF_SHAPE2)),
          "cursor: a 65x32 shape did not go to the DIB engine");
    set_cursor(BUF_SHAPE, "SetCursor (back to the card)");
    CHECK((L.setcursor_shape == 0) && (rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE),
          "cursor: a shape the card can show did not come back to it");

    /* NULL hides it, the DIB engine told too; the next shape needs no DIB engine call. */
    set_cursor(0, "SetCursor (NULL)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0) && (L.setcursor_shape == 0), "cursor: NULL did not hide the card's cursor");
    L.setcursor_calls = 0;
    set_cursor(BUF_SHAPE, "SetCursor (after NULL)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) && (L.setcursor_calls == 0),
          "cursor: the shape after NULL");

    /* A ReEnable to 16 bpp: the mono shape again at 2 bytes a pixel. */
    re_enable(16, "ReEnable (16 bpp, the cursor)");
    base = rd32(REG(VGPU98_REG_CURSOR_BASE));
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) && ((base == slot0) || (base == slot1)) &&
              (pattern_wrong(base, BUF_SHAPE, 2) == 0),
          "cursor: the mono shape not written again at 16 bpp");
    /* A colour shape at 16 bpp, then a ReEnable to 32: hidden until Windows gives another. */
    make_shape(BUF_SHAPE2, 32, 32, 0, 0, 16, 5);
    set_cursor(BUF_SHAPE2, "SetCursor (colour, 16 bpp)");
    CHECK(pattern_wrong(rd32(REG(VGPU98_REG_CURSOR_BASE)), BUF_SHAPE2, 2) == 0, "cursor: the colour pattern at 16 bpp");
    re_enable(32, "ReEnable (32 bpp, a 16-bit colour cursor)");
    CHECK(rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0, "cursor: a colour shape of another depth still shown");
    set_cursor(BUF_SHAPE, "SetCursor (mono, 32 bpp)");
    CHECK((rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) &&
              (pattern_wrong(rd32(REG(VGPU98_REG_CURSOR_BASE)), BUF_SHAPE, 4) == 0),
          "cursor: the mono pattern at 32 bpp");

    /* A full-screen DOS session: a shape given meanwhile is only kept; the desktop's return writes it. */
    {
        uint16_t w[1] = { 0x0202 };

        set_reg(UC_X86_REG_EAX, 0x4001);
        call_far(L.hooked_2f, w, 1, "INT 2Fh 4001h (the cursor)");
        make_shape(BUF_SHAPE2, 16, 16, 2, 2, 32, 6);
        base = rd32(REG(VGPU98_REG_CURSOR_BASE));
        L.setcursor_calls = 0;
        set_cursor(BUF_SHAPE2, "SetCursor (a DOS session)");
        CHECK((rd32(REG(VGPU98_REG_CURSOR_BASE)) == base) && (L.setcursor_calls == 0),
              "cursor: a shape was written while a DOS session owns the screen");
        call_far(L.restore_cb, NULL, 0, "RestoreDesktopMode (the cursor)");
        CHECK((rd32(REG(VGPU98_REG_CURSOR_BASE)) != base) && (rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == VGPU98_CURSOR_ENABLE) &&
                  (pattern_wrong(rd32(REG(VGPU98_REG_CURSOR_BASE)), BUF_SHAPE2, 4) == 0),
              "cursor: the shape not written at the desktop's return");
    }

    /* Disable: the card's cursor off. */
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE) };

        call_export(4, w, 2, "Disable (the cursor)");
        CHECK(rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0, "cursor: Disable left the card's cursor on");
    }

    /* A card without PROFILE.CURSOR: the DIB engine's, as before; the registers untouched. */
    machine(vram);
    vga.regs_base = REGS_PHYS;
    wr32(REG(VGPU98_REG_PROFILE), VGPU98_PROFILE & ~VGPU98_PROFILE_CURSOR);
    config.bpp = 8;
    driver_init();
    enable(BUF_GDIINFO, 1);
    enable(BUF_PDEVICE, 0);
    make_shape(BUF_SHAPE, 32, 32, 3, 5, 1, 1);
    set_cursor(BUF_SHAPE, "SetCursor (no hardware cursor)");
    move_cursor(5, 6);
    CHECK((L.setcursor_shape == (((uint32_t) SEG_BUFS << 16) | BUF_SHAPE)) && (L.movecursor_x == 5) &&
              (rd32(REG(VGPU98_REG_CURSOR_CONTROL)) == 0) && (rd32(REG(VGPU98_REG_CURSOR_BASE)) == 0),
          "cursor: without PROFILE.CURSOR the shape or the move did not go to the DIB engine alone");
}

/* --- The gamma table ------------------------------------------------------------ */

#define C1_GAMMA_RAMP 0x0020      /* dpCaps1: the driver takes gamma ramps */
#define REG_UNSET     0x5a5a5a5au /* in a gamma register: not written since the test put it there */
#define RAMP_UNSET    0xdeadu     /* in a ramp buffer: not written (no identity word has unequal bytes) */
#define GAMMA_SLOT(vram, n) ((vram) - 0x40000u + 0x30000u + (n) * 0x400u) /* the driver's two tables */

/* A ramp at SEG_BUFS:buf, red, green and blue, 256 words each, made from
   seed; seed 0 is the identity (each word's high byte its index). The low
   bytes are never the high bytes, so a table made from them shows. */
static void
make_ramp(uint16_t buf, int seed)
{
    const uint32_t r = LIN(SEG_BUFS, buf);
    int            c, i;

    for (c = 0; c < 3; c++) {
        for (i = 0; i < 256; i++) {
            const int hi = seed ? ((i * (c + 2) + seed * 37) & 0xff) : i;

            wr16(r + (uint32_t) (c * 512 + i * 2), (uint16_t) ((hi << 8) | ((hi + 0x5a + c) & 0xff)));
        }
    }
}

/* Words of the ramp at SEG_BUFS:got that are not those at SEG_BUFS:want, or
   with want 0 not the identity, i * 257. */
static int
ramp_wrong(uint16_t got, uint16_t want)
{
    int bad = 0, i;

    for (i = 0; i < 768; i++) {
        const uint16_t w = want ? rd16(LIN(SEG_BUFS, want) + (uint32_t) i * 2) : (uint16_t) ((i % 256) * 257);

        bad += rd16(LIN(SEG_BUFS, got) + (uint32_t) i * 2) != w;
    }
    return bad;
}

/* Entries of the card's table at video memory offset `slot` that are not the
   ramp at SEG_BUFS:buf as an 8-bit DAC takes it, each word's high byte:
   R << 16 | G << 8 | B. */
static int
table_wrong(uint32_t slot, uint16_t buf)
{
    const uint32_t r   = LIN(SEG_BUFS, buf);
    int            bad = 0, i;

    for (i = 0; i < 256; i++) {
        const uint32_t want = ((uint32_t) rd8(r + (uint32_t) (i * 2 + 1)) << 16) |
                              ((uint32_t) rd8(r + (uint32_t) (512 + i * 2 + 1)) << 8) |
                              rd8(r + (uint32_t) (1024 + i * 2 + 1));

        bad += rd32(FB_LINEAR + slot + (uint32_t) i * 4) != want;
    }
    return bad;
}

/* Both table slots filled with what no table holds, as a DOS session may
   leave them. */
static void
spoil_slots(uint32_t vram)
{
    uint32_t i;

    for (i = 0; i < 2 * 0x400u; i += 4)
        wr32(FB_LINEAR + GAMMA_SLOT(vram, 0) + i, 0xaaaaaaaau);
}

/* Dwords of slot n written since spoil_slots. */
static int
slot_written(uint32_t vram, int n)
{
    int      bad = 0;
    uint32_t i;

    for (i = 0; i < 0x400u; i += 4)
        bad += rd32(FB_LINEAR + GAMMA_SLOT(vram, n) + i) != 0xaaaaaaaau;
    return bad;
}

/* DDIGammaRamp(lpDevice, fGetSet, lpGammaRamp), the ramp at SEG_BUFS:buf; AX. */
static uint32_t
gamma_ramp(uint16_t set, uint16_t buf, const char *what)
{
    uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE), set, FARPTR(SEG_BUFS, buf) };

    return call_export(32, w, 5, what) & 0xffff;
}

/* A get into BUF_RAMP2, filled first with RAMP_UNSET; AX. */
static uint32_t
get_ramp(const char *what)
{
    uint32_t i;

    for (i = 0; i < 768; i++)
        wr16(LIN(SEG_BUFS, BUF_RAMP2) + i * 2, RAMP_UNSET);
    return gamma_ramp(0, BUF_RAMP2, what);
}

/* Whether a refused get left BUF_RAMP2 as get_ramp filled it. */
static int
get_untouched(void)
{
    return (rd16(LIN(SEG_BUFS, BUF_RAMP2)) == RAMP_UNSET) && (rd16(LIN(SEG_BUFS, BUF_RAMP2) + 767 * 2) == RAMP_UNSET);
}

#define GAMMA_CONTROL() rd32(REG(VGPU98_REG_GAMMA_CONTROL))
#define GAMMA_BASE()    rd32(REG(VGPU98_REG_GAMMA_BASE))

/* Puts REG_UNSET in both gamma registers. */
static void
unset_gamma_regs(void)
{
    wr32(REG(VGPU98_REG_GAMMA_BASE), REG_UNSET);
    wr32(REG(VGPU98_REG_GAMMA_CONTROL), REG_UNSET);
}

/* The card's gamma table: with a register window whose PROFILE has GAMMA, the
   GDIINFO announces DDIGammaRamp and its ramps go to the card, in the slot it
   is not showing; 8 bpp and a card without PROFILE.GAMMA refuse them. */
static void
test_gamma(void)
{
    const uint32_t vram  = 16u << 20;
    const uint32_t slot0 = GAMMA_SLOT(vram, 0), slot1 = GAMMA_SLOT(vram, 1);
    const uint16_t caps  = 0x0010 | 0x0080 | 0x0400 | 0x0800; /* test_enable's, without gamma */
    uint32_t       entry, r;

    machine(vram);
    vga.regs_base = REGS_PHYS;
    wr32(REG(VGPU98_REG_PROFILE), VGPU98_PROFILE);
    unset_gamma_regs();
    config.x = 640; config.y = 480; config.bpp = 32; config.dpi = 96; config.refresh = 0; config.high_vert = 0;
    config.fail = 0;
    entry = export_addr(32);
    CHECK((entry >> 16) == seg_para[1], "gamma: DDIGammaRamp (ordinal 32) at %08X, not in the code segment", entry);
    CHECK(driver_init() == 1, "gamma: DriverInit");
    CHECK(L.regs_mapped, "gamma: the register window was not mapped");
    enable(BUF_GDIINFO, 1);
    CHECK(rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 94) == (caps | C1_GAMMA_RAMP), "gamma: dpCaps1 %04X, expected %04X",
          rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 94), caps | C1_GAMMA_RAMP);
    CHECK(enable(BUF_PDEVICE, 0) == 1, "gamma: Enable");
    CHECK(GAMMA_CONTROL() == 0, "gamma: Enable left CONTROL %X with no ramp set", GAMMA_CONTROL());

    /* Before any set, a get answers the identity. */
    r = get_ramp("DDIGammaRamp (get, before any set)");
    CHECK((r == 1) && (ramp_wrong(BUF_RAMP2, 0) == 0), "gamma: the first get: AX=%u, %d words not the identity", r,
          ramp_wrong(BUF_RAMP2, 0));

    /* A ramp at 32 bpp: its high bytes in slot 1, the spare one; BASE names it, CONTROL turns it on. */
    spoil_slots(vram);
    make_ramp(BUF_RAMP, 1);
    r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set)");
    CHECK((r == 1) && (GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot1),
          "gamma: the set: AX=%u, CONTROL %X, BASE %X (expected %X)", r, GAMMA_CONTROL(), GAMMA_BASE(), slot1);
    CHECK((table_wrong(slot1, BUF_RAMP) == 0) && (slot_written(vram, 0) == 0),
          "gamma: the set's table (%d entries wrong), or slot 0 written", table_wrong(slot1, BUF_RAMP));

    /* A get: the words as set. */
    r = get_ramp("DDIGammaRamp (get)");
    CHECK((r == 1) && (ramp_wrong(BUF_RAMP2, BUF_RAMP) == 0), "gamma: the get: AX=%u, %d words not the ramp set", r,
          ramp_wrong(BUF_RAMP2, BUF_RAMP));

    /* The next set (any nonzero fGetSet sets): slot 0, BASE moved to it, slot 1 left as it was. */
    make_ramp(BUF_RAMP, 2);
    r = gamma_ramp(0x0100, BUF_RAMP, "DDIGammaRamp (set, the next)");
    CHECK((r == 1) && (GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot0) &&
              (table_wrong(slot0, BUF_RAMP) == 0),
          "gamma: the next set: AX=%u, CONTROL %X, BASE %X (expected %X), %d entries wrong", r, GAMMA_CONTROL(),
          GAMMA_BASE(), slot0, table_wrong(slot0, BUF_RAMP));
    make_ramp(BUF_RAMP, 1);
    CHECK(table_wrong(slot1, BUF_RAMP) == 0, "gamma: the next set wrote over the first's slot");

    /* An identity ramp (whatever its low bytes): CONTROL off, no slot written; a get answers it as given. */
    spoil_slots(vram);
    make_ramp(BUF_RAMP, 0);
    r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set, the identity)");
    CHECK((r == 1) && (GAMMA_CONTROL() == 0) && (slot_written(vram, 0) == 0) && (slot_written(vram, 1) == 0),
          "gamma: the identity: AX=%u, CONTROL %X, or a slot written", r, GAMMA_CONTROL());
    r = get_ramp("DDIGammaRamp (get, the identity)");
    CHECK((r == 1) && (ramp_wrong(BUF_RAMP2, BUF_RAMP) == 0), "gamma: the get after the identity: AX=%u, %d words wrong",
          r, ramp_wrong(BUF_RAMP2, BUF_RAMP));

    /* The first ramp again: slot 1, every load taking the other slot. */
    make_ramp(BUF_RAMP, 1);
    r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set, the first again)");
    CHECK((r == 1) && (GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot1) &&
              (table_wrong(slot1, BUF_RAMP) == 0),
          "gamma: the first ramp again: AX=%u, CONTROL %X, BASE %X (expected %X), %d entries wrong", r, GAMMA_CONTROL(),
          GAMMA_BASE(), slot1, table_wrong(slot1, BUF_RAMP));

    /* A ReEnable to 16 bpp and back to 32: the ramp stays, its table written again each time in the other slot. */
    unset_gamma_regs();
    spoil_slots(vram);
    re_enable(16, "ReEnable (16 bpp, gamma)");
    CHECK((GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot0) && (table_wrong(slot0, BUF_RAMP) == 0),
          "gamma: at 16 bpp: CONTROL %X, BASE %X (expected %X), %d entries wrong", GAMMA_CONTROL(), GAMMA_BASE(),
          slot0, table_wrong(slot0, BUF_RAMP));
    unset_gamma_regs();
    spoil_slots(vram);
    re_enable(32, "ReEnable (32 bpp, gamma)");
    CHECK((GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot1) && (table_wrong(slot1, BUF_RAMP) == 0),
          "gamma: back at 32 bpp: CONTROL %X, BASE %X (expected %X), %d entries wrong", GAMMA_CONTROL(), GAMMA_BASE(),
          slot1, table_wrong(slot1, BUF_RAMP));

    /* At 8 bpp the DAC is the palette: CONTROL off and nothing else written; sets and gets refused, writing nothing. */
    unset_gamma_regs();
    spoil_slots(vram);
    re_enable(8, "ReEnable (8 bpp, gamma)");
    CHECK((GAMMA_CONTROL() == 0) && (GAMMA_BASE() == REG_UNSET) && (slot_written(vram, 0) == 0) &&
              (slot_written(vram, 1) == 0),
          "gamma: at 8 bpp: CONTROL %X, BASE %X, or a slot written", GAMMA_CONTROL(), GAMMA_BASE());
    unset_gamma_regs();
    make_ramp(BUF_RAMP, 3);
    r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set, 8 bpp)");
    CHECK((r == 0) && (GAMMA_CONTROL() == REG_UNSET) && (GAMMA_BASE() == REG_UNSET) && (slot_written(vram, 0) == 0) &&
              (slot_written(vram, 1) == 0),
          "gamma: a set at 8 bpp: AX=%u, CONTROL %X, BASE %X, or a slot written", r, GAMMA_CONTROL(), GAMMA_BASE());
    r = get_ramp("DDIGammaRamp (get, 8 bpp)");
    CHECK((r == 0) && get_untouched(), "gamma: a get at 8 bpp: AX=%u, or it wrote the buffer", r);

    /* Back at 32 bpp: the ramp from before 8 bpp, not the one refused there. */
    make_ramp(BUF_RAMP, 1);
    re_enable(32, "ReEnable (back from 8 bpp, gamma)");
    CHECK((GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot0) && (table_wrong(slot0, BUF_RAMP) == 0),
          "gamma: back from 8 bpp: CONTROL %X, BASE %X (expected %X), %d entries wrong", GAMMA_CONTROL(), GAMMA_BASE(),
          slot0, table_wrong(slot0, BUF_RAMP));

    /* A full-screen DOS session: a set meanwhile is only kept (a get answers it); the desktop's return writes it,
       over whatever the session left in the slots. */
    {
        uint16_t w[1] = { 0x0202 };

        set_reg(UC_X86_REG_EAX, 0x4001);
        call_far(L.hooked_2f, w, 1, "INT 2Fh 4001h (gamma)");
        unset_gamma_regs();
        spoil_slots(vram);
        make_ramp(BUF_RAMP, 4);
        r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set, a DOS session)");
        CHECK((r == 1) && (GAMMA_CONTROL() == REG_UNSET) && (GAMMA_BASE() == REG_UNSET) &&
                  (slot_written(vram, 0) == 0) && (slot_written(vram, 1) == 0),
              "gamma: a set in a DOS session: AX=%u, CONTROL %X, BASE %X, or a slot written", r, GAMMA_CONTROL(),
              GAMMA_BASE());
        r = get_ramp("DDIGammaRamp (get, a DOS session)");
        CHECK((r == 1) && (ramp_wrong(BUF_RAMP2, BUF_RAMP) == 0), "gamma: a get in a DOS session: AX=%u, %d words wrong",
              r, ramp_wrong(BUF_RAMP2, BUF_RAMP));
        call_far(L.restore_cb, NULL, 0, "RestoreDesktopMode (gamma)");
        CHECK((GAMMA_CONTROL() == VGPU98_GAMMA_ENABLE) && (GAMMA_BASE() == slot1) && (table_wrong(slot1, BUF_RAMP) == 0),
              "gamma: the desktop's return: CONTROL %X, BASE %X (expected %X), %d entries wrong", GAMMA_CONTROL(),
              GAMMA_BASE(), slot1, table_wrong(slot1, BUF_RAMP));
    }

    /* Disable: the card's gamma off. */
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE) };

        call_export(4, w, 2, "Disable (gamma)");
        CHECK(GAMMA_CONTROL() == 0, "gamma: Disable left CONTROL %X", GAMMA_CONTROL());
    }

    /* A card without PROFILE.GAMMA (86Box's Gamma setting Off): no C1_GAMMA_RAMP, sets and gets refused, through a
       ReEnable and Disable nothing written. */
    machine(vram);
    vga.regs_base = REGS_PHYS;
    wr32(REG(VGPU98_REG_PROFILE), VGPU98_PROFILE & ~VGPU98_PROFILE_GAMMA);
    unset_gamma_regs();
    config.bpp = 32;
    driver_init();
    enable(BUF_GDIINFO, 1);
    CHECK(rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 94) == caps, "gamma: without PROFILE.GAMMA, dpCaps1 %04X, expected %04X",
          rd16(LIN(SEG_BUFS, BUF_GDIINFO) + 94), caps);
    enable(BUF_PDEVICE, 0);
    spoil_slots(vram);
    make_ramp(BUF_RAMP, 1);
    r = gamma_ramp(1, BUF_RAMP, "DDIGammaRamp (set, no gamma)");
    CHECK(r == 0, "gamma: without PROFILE.GAMMA a set returned %u", r);
    r = get_ramp("DDIGammaRamp (get, no gamma)");
    CHECK((r == 0) && get_untouched(), "gamma: without PROFILE.GAMMA a get returned %u, or it wrote the buffer", r);
    re_enable(16, "ReEnable (16 bpp, no gamma)");
    {
        uint16_t w[] = { FARPTR(SEG_BUFS, BUF_PDEVICE) };

        call_export(4, w, 2, "Disable (no gamma)");
    }
    CHECK((GAMMA_CONTROL() == REG_UNSET) && (GAMMA_BASE() == REG_UNSET) && (slot_written(vram, 0) == 0) &&
              (slot_written(vram, 1) == 0),
          "gamma: without PROFILE.GAMMA, CONTROL %X, BASE %X, or a slot written", GAMMA_CONTROL(), GAMMA_BASE());
}

/* The VBE number and default rate of a desktop mode, from the mode table
   (read with the shared loader, not from the generated include the driver uses). */
static dmode_case_t
table_mode(const dmodes_t *dm, uint16_t w, uint16_t h, uint8_t bpp)
{
    dmode_case_t m;
    size_t i;

    memset(&m, 0, sizeof(m));
    m.w   = w;
    m.h   = h;
    m.bpp = bpp;
    for (i = 0; i < dm->count; i++) {
        const dmode_t *d = &dm->mode[i];

        if (d->enabled && d->desktop && (d->width == w) && (d->height == h)) {
            m.vbe     = d->vbe[bpp == 8 ? 0 : bpp == 16 ? 2 : 3];
            m.refresh = d->refresh[0];
            return m;
        }
    }
    die("%ux%u is not a desktop mode", w, h);
}

int
main(int argc, char **argv)
{
    static const uint16_t sizes[][3] = {
        { 640, 480, 8 },   { 800, 600, 16 },  { 1024, 768, 32 }, { 1600, 1200, 8 },
        { 1920, 1440, 32 }, { 1280, 1024, 16 }, { 1920, 1080, 16 },
    };
    static const mode_limits_t limits = { VGPU98_DISP_MAX_WIDTH, VGPU98_DISP_MAX_HEIGHT, VGPU98_DISP_REFRESH_MIN,
                                          VGPU98_DISP_REFRESH_MAX };
    static dmodes_t dm;
    size_t          i;

    set_progname("drvtest");
    if (argc != 3)
        die("usage: drvtest DRIVER DISPLAY_MODES_CARD");
    ne_file = read_file(argv[1], &ne_size);
    modes_load(argv[2], &limits, &dm);

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        dmode_case_t m = table_mode(&dm, sizes[i][0], sizes[i][1], (uint8_t) sizes[i][2]);

        test_enable(&m, 32u << 20);
        if (failures)
            break;
    }
    if (!failures) {
        dmode_case_t m = table_mode(&dm, 640, 480, 8);

        test_enable(&m, (uint32_t) VGPU98_VRAM_MAX_MB << 20); /* the largest Memory size, all of it mapped */
        test_enable(&m, 16u << 20);
        test_running();
        test_validate();
        test_config_fallbacks();
        test_escape();
        test_cursor();
        test_gamma();
    }
    printf("drvtest: %d checks, %d failed\n", checks, failures);
    if (uc)
        uc_close(uc);
    return failures ? 1 : 0;
}
