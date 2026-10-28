/*
 * cardc: VGPU98 card compiler.
 *
 * Reads the card definition, the VGA mode tables and the display mode table,
 * validates them, and writes:
 *   card.inc           NASM constants: IDs, sizes, strings, display engine registers
 *   vgamodes.inc       NASM video parameter table and mode descriptor table
 *   palettes.inc       NASM default DAC palettes
 *   vbemodes.inc       NASM VBE mode table for the BIOS
 *   drvmodes.inc       NASM desktop mode table for the Windows driver
 *   vid_vgpu98_card.h  86Box device constants, including the monitor's EDID
 *
 * Usage: cardc --card FILE --modes FILE --display FILE --out-dir DIR
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "edid.h"
#include "modes.h"
#include "palettes.h"
#include "util.h"

#define PARAM_ENTRIES 29
#define PARAM_SIZE    64
#define MODE_COUNT    20 /* modes 00h-13h */
#define MAX_VRAM      8

enum model { MODEL_NONE, MODEL_TEXT, MODEL_CGA4, MODEL_CGA2, MODEL_PLANAR, MODEL_VGA256 };

static const char *model_names[]   = { "none", "text", "cga4", "cga2", "planar", "vga256" };
static const char *palette_names[] = { "cga64", "ega64", "mono64", "vga256" };

/* Upper-case copy of a short name, for NASM constant names. */
static const char *
upper(const char *name)
{
    static char out[4][32];
    static int  slot;
    char       *p = out[slot++ & 3];
    size_t      i;

    for (i = 0; name[i] && (i < sizeof(out[0]) - 1); i++)
        p[i] = (char) (((name[i] >= 'a') && (name[i] <= 'z')) ? name[i] - 'a' + 'A' : name[i]);
    p[i] = 0;
    return p;
}

typedef struct card_t {
    char    *name;
    char    *internal_name;
    char    *rom_path;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t revision;
    uint32_t class_code;
    uint32_t subsys_vendor_id;
    uint32_t subsys_id;
    uint32_t bar0_size;
    uint32_t bar1_size;
    uint32_t rom_bar_size;
    uint32_t vram_mb[MAX_VRAM];
    size_t   n_vram;
    uint32_t vram_default_mb;
    uint32_t rom_size;
    uint32_t bios_revision;
    char    *version;
    char    *vendor_name;
    char    *product_name;
    char    *signon;
    char    *copyright;
    uint32_t id_magic;
    uint32_t hw_version;
    uint32_t profile;
    uint32_t command_set;
    uint32_t gpu_reserve;
    uint32_t debug_seq_index;
    /* [display] */
    uint32_t disp_first_index;
    uint32_t disp_key;
    uint32_t disp_id;
    uint32_t disp_revision;
    mode_limits_t limits;
    /* [monitor] */
    edid_params_t monitor;
} card_t;

#define VBE_LIST_MAX 110 /* mode numbers that fit the BIOS's mode list */

/* Display engine registers: offsets from [display] first_index (spec/display-engine.md). */
static const struct {
    const char *name;
    uint32_t    offset;
} disp_regs[] = {
    { "KEY", 0x00 },      { "ID", 0x01 },     { "REVISION", 0x02 }, { "FEATURES", 0x04 },
    { "FORMATS", 0x05 },  { "MAX_WIDTH", 0x06 }, { "VRAM_SIZE", 0x08 }, { "LFB_BASE", 0x0c },
    { "CONTROL", 0x10 },  { "FORMAT", 0x11 }, { "WIDTH", 0x12 },    { "HEIGHT", 0x14 },
    { "PITCH", 0x16 },    { "START", 0x18 },  { "REFRESH", 0x1c },  { "BANK", 0x1e },
    { "EDID_INDEX", 0x20 }, { "EDID_DATA", 0x21 }, { "SCRATCH", 0x22 }, { "REGS_BASE", 0x24 },
    { "GPU_RESET", 0x28 },
};
#define DISP_REG_SPAN    0x40 /* SR first_index .. first_index + 3Fh */
#define DISP_FEATURES    0x1f /* engine, linear framebuffer, banked window, EDID port, REGS_BASE */
#define DISP_FORMATS     0x0f /* 8, 15, 16, 32 bpp */
#define DISP_CTRL_ENABLE 0x01
#define DISP_CTRL_DSCAN  0x02 /* double scan: every pixel and line shown twice */
#define DISP_GPU_RESET   0x01 /* GPU_RESET: the GPU to its power-on state */

/* BAR0 registers of interface 3.0 (spec/gpu-registers.md): the names written
   into both generated files, as GPU_REG_x (NASM) and VGPU98_REG_x (C). */
static const struct {
    const char *name;
    uint32_t    offset;
} gpu_regs[] = {
    { "ID", 0x00 },          { "HW_VERSION", 0x04 },  { "PROFILE", 0x08 },     { "VRAM_SIZE", 0x0c },
    { "COMMAND_SET", 0x10 }, { "RING_BASE", 0x20 },   { "RING_SIZE", 0x24 },   { "RING_HEAD", 0x28 },
    { "RING_TAIL", 0x2c },   { "CONTROL", 0x30 },     { "STATUS", 0x34 },      { "ERROR_OFFSET", 0x38 },
    { "ERROR_CODE", 0x3c },  { "FENCE", 0x40 },       { "VBLANK_COUNT", 0x48 }, { "SCANLINE", 0x4c },
    { "DEBUG", 0x50 },       { "SYSBUF_PHYS", 0x54 }, { "SYSBUF_LINEAR", 0x58 }, { "SYSBUF_SIZE", 0x5c },
    { "SYSBUF_STATUS", 0x60 }, { "STATUS_BASE", 0x64 },  { "GPUVM_BASE", 0x68 },    { "GPUVM_ENTRIES", 0x6c },
    { "CURSOR_CONTROL", 0x70 }, { "CURSOR_POS", 0x74 },  { "CURSOR_BASE", 0x78 }, /* interface 3.4 */
    { "GAMMA_CONTROL", 0x7c },  { "GAMMA_BASE", 0x80 },  /* interface 3.5 */
    { "VSYNC_CONTROL", 0x84 },  /* interface 3.6 */
};

/* Register bits (spec/gpu-registers.md), written into both files likewise. */
static const struct {
    const char *name;
    uint32_t    value;
} gpu_bits[] = {
    { "PROFILE_COMMANDS", 0x01 }, { "PROFILE_BLIT", 0x02 },      { "PROFILE_RENDER3D", 0x04 },
    { "PROFILE_SYSRING", 0x08 },  { "PROFILE_STATUSWB", 0x10 }, { "PROFILE_GPUVM", 0x20 },
    { "PROFILE_VERTEX", 0x40 },   { "PROFILE_FIXED8", 0x80 },   { "PROFILE_PROGRAMS", 0x100 },
    { "PROFILE_DOWNLOAD", 0x200 }, { "PROFILE_GLIDE", 0x400 }, /* command set 2.4: the Glide device */
    { "PROFILE_OGL", 0x800 },     /* command set 2.5: the OpenGL device */
    { "PROFILE_CURSOR", 0x1000 }, /* interface 3.4: the hardware cursor */
    { "PROFILE_GAMMA", 0x2000 },  /* interface 3.5: the gamma table */
    { "PROFILE_VSYNC", 0x4000 },  /* interface 3.6: the V-sync overrides */
    { "CURSOR_ENABLE", 0x01 },    { "CURSOR_SIZE", 64 },         /* CURSOR_CONTROL's bit; the pattern's pixels each way */
    { "CURSOR_XOR", 0x200 },      { "CURSOR_ALIGN", 0x400 },     /* the image's offset in the pattern; CURSOR_BASE's */
    { "GAMMA_ENABLE", 0x01 },     { "GAMMA_ALIGN", 0x400 },      /* GAMMA_CONTROL's bit; GAMMA_BASE's alignment */
    { "GAMMA_ENTRIES", 256 },     /* the table's entries, each R << 16 | G << 8 | B */
    { "VSYNC_D3D_SHIFT", 0 },     { "VSYNC_OGL_SHIFT", 2 },      { "VSYNC_GLIDE_SHIFT", 4 }, /* VSYNC_CONTROL's fields */
    { "VSYNC_FIELD", 3 },         { "VSYNC_APP", 0 },            { "VSYNC_ON", 1 },          /* a field's mask; its values */
    { "VSYNC_OFF", 2 },
    { "CTRL_RUN", 0x01 },         { "CTRL_RESET", 0x02 },        { "CTRL_HOLD_DRIVER", 0x04 },
    { "CTRL_HOLD_VDD", 0x08 },    { "CTRL_HOLD_DRIVER_WE", 0x100 }, { "CTRL_HOLD_VDD_WE", 0x200 },
    { "STATUS_BUSY", 0x01 },      { "STATUS_ERROR", 0x02 },
    { "STATUS_RUNNING", 0x04 },   { "STATUS_FLIP_PENDING", 0x08 }, { "STATUS_HELD", 0x10 },
    { "SCANLINE_VBLANK", 0x80000000u },
    { "RING_SIZE_MIN", 0x1000 },  { "RING_SIZE_MAX", 0x100000 },
    { "RING_SYSMEM", 0x01 },      { "RING_SYS_SIZE_MAX", 0x400000 },
    { "GPUVM_VALID", 0x01 },      { "GPUVM_ENTRIES_MAX", 0x10000 },
    { "DEBUG_LISTENING", 0x01 },  { "DEBUG_AUDIT", 0x02 },        { "DEBUG_AUDIT8", 0x04 },
    { "DEBUG_SHADERS", 0x08 }, /* read: "DirectX 8 shaders: Capture" at a full level (LISTENING) */
    { "DEBUG_SHADERS14", 0x10 }, /* read: and its "with GPU programs" (once "pixel shaders 1.4") */
    { "DEBUG_EVENTS", 0x20 },    /* read: Diagnostics "Minimal": the DirectX driver's event lines alone, LISTENING clear */
    { "DEBUG_TEXTURES", 0x40 },  /* read: the texture audit, with LISTENING */
    { "DEBUG_GLIDE", 0x80 },     /* read: Diagnostics "Glide": the Glide DLLs' full lines, with EVENTS */
    { "DEBUG_OPENGL", 0x100 },   /* read: Diagnostics "OpenGL": the ICD's full lines, with EVENTS */
    { "DEBUG_CAPTURE", 0x5041430Du }, /* a write: the DirectX driver asks for a capture (low byte CR) */
    { "DEBUG_IDLE", 0x4C44490Du },    /* a write: the DirectX driver's last Direct3D context went (low byte CR) */
};

typedef struct param_t {
    int     reserved;
    int     line;
    uint8_t bytes[PARAM_SIZE];
} param_t;

typedef struct vga_mode_t {
    int      model;
    int      mono;
    uint32_t segment;
    uint8_t  params[3];
    uint8_t  palettes[3];
} vga_mode_t;

static card_t     card;
static dmodes_t   dmodes;
static uint8_t    edid[EDID_SIZE];
static param_t    params[PARAM_ENTRIES];
static vga_mode_t modes[MODE_COUNT];
static uint8_t    pal64[3][64][3]; /* cga64, ega64, mono64 */
static uint8_t    pal256[256][3];

static int
is_pow2(uint32_t v)
{
    return (v != 0) && ((v & (v - 1)) == 0);
}

/* ---------------------------------------------------------------------------
 * Card definition
 * ------------------------------------------------------------------------- */

static void
load_card(const char *path)
{
    cfg_t         *cfg = cfg_load(path);
    cfg_section_t *sec;
    size_t         i;
    int            found_default = 0;

    sec                = cfg_section(cfg, "card");
    card.name          = cfg_get_str(cfg, sec, "name", 63);
    card.internal_name = cfg_get_str(cfg, sec, "internal_name", 31);
    card.rom_path      = cfg_get_str(cfg, sec, "rom_path", 127);

    sec                   = cfg_section(cfg, "pci");
    card.vendor_id        = cfg_get_u32(cfg, sec, "vendor_id", 0x0001, 0xfffe);
    card.device_id        = cfg_get_u32(cfg, sec, "device_id", 0x0000, 0xfffe);
    card.revision         = cfg_get_u32(cfg, sec, "revision", 0x00, 0xff);
    card.class_code       = cfg_get_u32(cfg, sec, "class_code", 0, 0xffffff);
    card.subsys_vendor_id = cfg_get_u32(cfg, sec, "subsystem_vendor_id", 0x0001, 0xfffe);
    card.subsys_id        = cfg_get_u32(cfg, sec, "subsystem_id", 0x0000, 0xffff);
    card.bar0_size        = cfg_get_u32(cfg, sec, "bar0_size", 0x1000, 0x10000000);
    card.bar1_size        = cfg_get_u32(cfg, sec, "bar1_size", 0x100000, 0x10000000);
    card.rom_bar_size     = cfg_get_u32(cfg, sec, "rom_bar_size", 0x800, 0x1000000);
    if (card.class_code != 0x030000)
        cfg_error(cfg, cfg_line(sec, "class_code"), "class_code must be 0x030000 (VGA-compatible display controller)");
    if (!is_pow2(card.bar0_size) || !is_pow2(card.bar1_size) || !is_pow2(card.rom_bar_size))
        cfg_error(cfg, sec->line, "BAR sizes must be powers of two");

    sec         = cfg_section(cfg, "memory");
    card.n_vram = cfg_get_u32_list(cfg, sec, "vram_mb", card.vram_mb, MAX_VRAM);
    card.vram_default_mb = cfg_get_u32(cfg, sec, "vram_default_mb", 1, 4096);
    if (card.n_vram == 0)
        cfg_error(cfg, cfg_line(sec, "vram_mb"), "vram_mb needs at least one size");
    for (i = 0; i < card.n_vram; i++) {
        if (!is_pow2(card.vram_mb[i]))
            cfg_error(cfg, cfg_line(sec, "vram_mb"), "VRAM sizes must be powers of two");
        if ((i > 0) && (card.vram_mb[i] <= card.vram_mb[i - 1]))
            cfg_error(cfg, cfg_line(sec, "vram_mb"), "VRAM sizes must be in increasing order");
        if ((uint64_t) card.vram_mb[i] << 20 > card.bar1_size)
            cfg_error(cfg, cfg_line(sec, "vram_mb"), "%u MB does not fit the BAR1 aperture", card.vram_mb[i]);
        if (card.vram_mb[i] == card.vram_default_mb)
            found_default = 1;
    }
    if (!found_default)
        cfg_error(cfg, cfg_line(sec, "vram_default_mb"), "vram_default_mb must be one of vram_mb");

    sec                = cfg_section(cfg, "bios");
    card.rom_size      = cfg_get_u32(cfg, sec, "rom_size", 512, 65536);
    card.bios_revision = cfg_get_u32(cfg, sec, "revision", 0, 0xffff);
    card.version       = cfg_get_str(cfg, sec, "version", 15);
    card.vendor_name   = cfg_get_str(cfg, sec, "vendor_name", 63);
    card.product_name  = cfg_get_str(cfg, sec, "product_name", 63);
    card.signon        = cfg_get_str(cfg, sec, "signon", 78);
    card.copyright     = cfg_get_str(cfg, sec, "copyright", 78);
    if (!is_pow2(card.rom_size))
        cfg_error(cfg, cfg_line(sec, "rom_size"), "rom_size must be a power of two (512 bytes to 64 KB)");
    if (card.rom_size > card.rom_bar_size)
        cfg_error(cfg, cfg_line(sec, "rom_size"), "rom_size does not fit rom_bar_size");

    sec                  = cfg_section(cfg, "registers");
    card.id_magic        = cfg_get_u32(cfg, sec, "id_magic", 0, 0xffffffff);
    card.hw_version      = cfg_get_u32(cfg, sec, "hw_version", 0, 0xffffffff);
    card.profile         = cfg_get_u32(cfg, sec, "profile", 0, 0x7fff);
    card.command_set     = cfg_get_u32(cfg, sec, "command_set", 0, 0xffffffff);
    card.gpu_reserve     = cfg_get_u32(cfg, sec, "gpu_reserve", 0x10000, 0x100000);
    card.debug_seq_index = cfg_get_u32(cfg, sec, "debug_seq_index", 0x05, 0xff);
    if (card.gpu_reserve % 0x10000)
        cfg_error(cfg, cfg_line(sec, "gpu_reserve"), "gpu_reserve must be a multiple of 64 KB");

    sec                     = cfg_section(cfg, "display");
    card.disp_first_index   = cfg_get_u32(cfg, sec, "first_index", 0x08, 0xff - DISP_REG_SPAN + 1);
    card.disp_key           = cfg_get_u32(cfg, sec, "key", 0x01, 0xff);
    card.disp_id            = cfg_get_u32(cfg, sec, "id", 0x00, 0xff);
    card.disp_revision      = cfg_get_u32(cfg, sec, "revision", 0x00, 0xff);
    /* 86Box draws into a 2048 x 2048 buffer and counts at most 2000 lines a frame. */
    card.limits.max_width   = cfg_get_u32(cfg, sec, "max_width", 640, 2048);
    card.limits.max_height  = cfg_get_u32(cfg, sec, "max_height", 480, 1536);
    card.limits.refresh_min = cfg_get_u32(cfg, sec, "refresh_min", 40, 255);
    card.limits.refresh_max = cfg_get_u32(cfg, sec, "refresh_max", card.limits.refresh_min, 255);
    if ((card.debug_seq_index >= card.disp_first_index) &&
        (card.debug_seq_index < card.disp_first_index + DISP_REG_SPAN))
        cfg_error(cfg, cfg_line(sec, "first_index"), "the display registers overlap the debug register");
    if (card.limits.max_width % 8)
        cfg_error(cfg, cfg_line(sec, "max_width"), "max_width must be a multiple of 8");

    sec = cfg_section(cfg, "monitor");
    {
        char *vendor = cfg_get_str(cfg, sec, "vendor", 3);
        char *pref[2];
        uint32_t size[2];

        if (strlen(vendor) != 3)
            cfg_error(cfg, cfg_line(sec, "vendor"), "vendor must be three upper-case letters");
        memcpy(card.monitor.vendor, vendor, 4);
        free(vendor);
        card.monitor.product = (uint16_t) cfg_get_u32(cfg, sec, "product", 0, 0xffff);
        card.monitor.serial  = cfg_get_u32(cfg, sec, "serial", 0, 0xffffffff);
        card.monitor.week    = (uint8_t) cfg_get_u32(cfg, sec, "week", 0, 53);
        card.monitor.year    = (uint16_t) cfg_get_u32(cfg, sec, "year", 1990, 2245);
        card.monitor.name    = cfg_get_str(cfg, sec, "name", 13);
        if (cfg_get_u32_list(cfg, sec, "size_mm", size, 2) != 2)
            cfg_error(cfg, cfg_line(sec, "size_mm"), "size_mm needs width and height");
        card.monitor.width_mm  = (uint16_t) size[0];
        card.monitor.height_mm = (uint16_t) size[1];
        if (cfg_get_word_list(cfg, sec, "preferred", pref, 2) != 2)
            cfg_error(cfg, cfg_line(sec, "preferred"), "preferred needs WIDTHxHEIGHT and a refresh rate");
        {
            uint32_t w, h, hz;
            char    *x = strchr(pref[0], 'x');

            if ((x == NULL) || (*x = 0, !parse_u32(pref[0], &w)) || !parse_u32(x + 1, &h) || !parse_u32(pref[1], &hz))
                cfg_error(cfg, cfg_line(sec, "preferred"), "preferred must be WIDTHxHEIGHT RATE");
            card.monitor.preferred_width   = (uint16_t) w;
            card.monitor.preferred_height  = (uint16_t) h;
            card.monitor.preferred_refresh = (uint8_t) hz;
        }
        free(pref[0]);
        free(pref[1]);
    }

    cfg_check_unused(cfg);
    cfg_free(cfg);
}

/* ---------------------------------------------------------------------------
 * VGA mode tables
 * ------------------------------------------------------------------------- */

static int
lookup(const cfg_t *cfg, int line, const char *word, const char **names, int count, const char *what)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(word, names[i]) == 0)
            return i;
    }
    cfg_error(cfg, line, "unknown %s '%s'", what, word);
}

static void
load_param(cfg_t *cfg, cfg_section_t *sec, param_t *p)
{
    uint8_t *b = p->bytes;
    uint32_t value;

    p->line = sec->line;
    if (cfg_has(sec, "reserved")) {
        char *word = cfg_get_word(cfg, sec, "reserved");

        if (strcmp(word, "yes") != 0)
            cfg_error(cfg, cfg_line(sec, "reserved"), "reserved must be 'yes'");
        if (sec->n_entries != 1)
            cfg_error(cfg, sec->line, "a reserved entry has no other keys");
        free(word);
        p->reserved = 1;
        memset(b, 0, PARAM_SIZE);
        return;
    }

    b[0]  = (uint8_t) cfg_get_u32(cfg, sec, "columns", 1, 255);
    b[1]  = (uint8_t) (cfg_get_u32(cfg, sec, "rows", 1, 256) - 1);
    b[2]  = (uint8_t) cfg_get_u32(cfg, sec, "char_height", 1, 32);
    value = cfg_get_u32(cfg, sec, "page_size", 0, 0xffff);
    b[3]  = (uint8_t) (value & 0xff);
    b[4]  = (uint8_t) (value >> 8);
    cfg_get_bytes(cfg, sec, "seq", &b[5], 4);
    cfg_get_bytes(cfg, sec, "misc", &b[9], 1);
    cfg_get_bytes(cfg, sec, "crtc", &b[10], 25);
    cfg_get_bytes(cfg, sec, "attr", &b[35], 20);
    cfg_get_bytes(cfg, sec, "gc", &b[55], 9);
}

static void
load_mode(cfg_t *cfg, cfg_section_t *sec, vga_mode_t *m, uint32_t number)
{
    char    *word = cfg_get_word(cfg, sec, "model");
    uint32_t list[3];
    char    *names[3];
    size_t   n_params;
    size_t   n_pals;
    size_t   i;
    uint32_t want_seg;

    m->model = lookup(cfg, cfg_line(sec, "model"), word, model_names, 6, "model");
    free(word);
    if (m->model == MODEL_NONE) {
        if (sec->n_entries != 1)
            cfg_error(cfg, sec->line, "a mode with model = none has no other keys");
        return;
    }

    m->mono    = (int) cfg_get_u32(cfg, sec, "mono", 0, 1);
    m->segment = cfg_get_u32(cfg, sec, "segment", 0xa000, 0xb800);

    n_params = cfg_get_u32_list(cfg, sec, "params", list, 3);
    n_pals   = cfg_get_word_list(cfg, sec, "palettes", names, 3);
    if (m->model == MODEL_TEXT) {
        if ((n_params != 3) || (n_pals != 3))
            cfg_error(cfg, sec->line, "text modes list three params and three palettes (200, 350, 400 lines)");
    } else if ((n_params != 1) || (n_pals != 1))
        cfg_error(cfg, sec->line, "graphics modes list one param and one palette");

    for (i = 0; i < 3; i++) {
        size_t         src  = (n_params == 3) ? i : 0;
        const param_t *p;
        int            pal;

        if (list[src] >= PARAM_ENTRIES)
            cfg_error(cfg, cfg_line(sec, "params"), "param entry 0x%X does not exist", list[src]);
        p = &params[list[src]];
        if (p->reserved)
            cfg_error(cfg, cfg_line(sec, "params"), "param entry 0x%02X is reserved", list[src]);
        if (((p->bytes[9] & 1) == 0) != (m->mono != 0))
            cfg_error(cfg, cfg_line(sec, "params"),
                      "param entry 0x%02X: misc bit 0 does not match mono = %d", list[src], m->mono);
        if ((p->bytes[2] != 8) && (p->bytes[2] != 14) && (p->bytes[2] != 16))
            cfg_error(cfg, cfg_line(sec, "params"),
                      "param entry 0x%02X: char_height must be 8, 14 or 16 (a font the BIOS has)", list[src]);

        pal = lookup(cfg, cfg_line(sec, "palettes"), names[src], palette_names, PAL_COUNT, "palette");
        if ((pal == PAL_VGA256) != (m->model == MODEL_VGA256))
            cfg_error(cfg, cfg_line(sec, "palettes"), "the vga256 palette goes with model vga256 only");

        m->params[i]   = (uint8_t) list[src];
        m->palettes[i] = (uint8_t) pal;
    }
    for (i = 0; i < n_pals; i++)
        free(names[i]);

    if ((m->model == MODEL_TEXT) && m->mono)
        want_seg = 0xb000;
    else if ((m->model == MODEL_TEXT) || (m->model == MODEL_CGA4) || (m->model == MODEL_CGA2))
        want_seg = 0xb800;
    else
        want_seg = 0xa000;
    if (m->segment != want_seg)
        cfg_error(cfg, cfg_line(sec, "segment"), "mode %02Xh of this model uses segment 0x%04X", number, want_seg);
}

static void
load_modes(const char *path)
{
    cfg_t *cfg = cfg_load(path);
    int    seen_param[PARAM_ENTRIES];
    int    seen_mode[MODE_COUNT];
    int    i;

    memset(seen_param, 0, sizeof(seen_param));
    memset(seen_mode, 0, sizeof(seen_mode));

    /* Parameter entries first: mode descriptors refer to them. */
    for (i = 0; i < cfg->n_sections; i++) {
        cfg_section_t *sec = &cfg->sections[i];
        uint32_t       n;

        if (strcmp(sec->name, "param") != 0)
            continue;
        sec->used = 1;
        n         = cfg_section_arg_u32(cfg, sec, 0, PARAM_ENTRIES - 1);
        if (seen_param[n])
            cfg_error(cfg, sec->line, "[param 0x%02X] repeated", n);
        seen_param[n] = 1;
        load_param(cfg, sec, &params[n]);
    }
    for (i = 0; i < PARAM_ENTRIES; i++) {
        if (!seen_param[i])
            cfg_error(cfg, 0, "missing [param 0x%02X]", i);
    }

    for (i = 0; i < cfg->n_sections; i++) {
        cfg_section_t *sec = &cfg->sections[i];
        uint32_t       n;

        if (strcmp(sec->name, "mode") != 0)
            continue;
        sec->used = 1;
        n         = cfg_section_arg_u32(cfg, sec, 0, MODE_COUNT - 1);
        if (seen_mode[n])
            cfg_error(cfg, sec->line, "[mode 0x%02X] repeated", n);
        seen_mode[n] = 1;
        load_mode(cfg, sec, &modes[n], n);
    }
    for (i = 0; i < MODE_COUNT; i++) {
        if (!seen_mode[i])
            cfg_error(cfg, 0, "missing [mode 0x%02X]", i);
    }

    cfg_check_unused(cfg);
    cfg_free(cfg);
}

/* ---------------------------------------------------------------------------
 * Output
 * ------------------------------------------------------------------------- */

static void
nasm_string(buf_t *out, const char *name, const char *value)
{
    const char *p;

    buf_printf(out, "%%define %-24s `", name);
    for (p = value; *p; p++) {
        if ((*p == '\\') || (*p == '`'))
            buf_append(out, "\\", 1);
        buf_append(out, p, 1);
    }
    buf_printf(out, "`\n");
}

static void
c_string(buf_t *out, const char *name, const char *value)
{
    const char *p;

    buf_printf(out, "#define %-31s \"", name);
    for (p = value; *p; p++) {
        if ((*p == '\\') || (*p == '"'))
            buf_append(out, "\\", 1);
        buf_append(out, p, 1);
    }
    buf_printf(out, "\"\n");
}

static void
bytes_rows(buf_t *out, const uint8_t *data, size_t count, size_t per_row)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if ((i % per_row) == 0)
            buf_printf(out, "    db ");
        buf_printf(out, "0x%02X", data[i]);
        buf_printf(out, (((i % per_row) == per_row - 1) || (i == count - 1)) ? "\n" : ", ");
    }
}

static void
write_out(const char *dir, const char *name, buf_t *buf)
{
    size_t len  = strlen(dir) + strlen(name) + 2;
    char  *path = xmalloc(len);

    snprintf(path, len, "%s/%s", dir, name);
    write_file(path, buf->data, buf->len);
    free(path);
    buf_free(buf);
}

static void
gen_card_inc(const char *dir)
{
    buf_t  out = { 0 };
    size_t i;

    buf_printf(&out, "; Generated by cardc from card/vgpu98.card. Do not edit.\n\n");
    buf_printf(&out, "%%define CARD_PCI_VENDOR_ID         0x%04X\n", card.vendor_id);
    buf_printf(&out, "%%define CARD_PCI_DEVICE_ID         0x%04X\n", card.device_id);
    buf_printf(&out, "%%define CARD_PCI_REVISION          0x%02X\n", card.revision);
    buf_printf(&out, "%%define CARD_PCI_CLASS_BASE        0x%02X\n", (card.class_code >> 16) & 0xff);
    buf_printf(&out, "%%define CARD_PCI_CLASS_SUB         0x%02X\n", (card.class_code >> 8) & 0xff);
    buf_printf(&out, "%%define CARD_PCI_CLASS_PROGIF      0x%02X\n", card.class_code & 0xff);
    buf_printf(&out, "%%define CARD_ROM_SIZE              %u\n", card.rom_size);
    buf_printf(&out, "%%define CARD_BIOS_REVISION         0x%04X\n", card.bios_revision);
    buf_printf(&out, "%%define CARD_DEBUG_SEQ_INDEX       0x%02X\n", card.debug_seq_index);
    buf_printf(&out, "\n; Display engine registers (sequencer indices, spec/display-engine.md)\n");
    for (i = 0; i < sizeof(disp_regs) / sizeof(disp_regs[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "SR_DISP_%s", disp_regs[i].name);
        buf_printf(&out, "%%define %-26s 0x%02X\n", name, card.disp_first_index + disp_regs[i].offset);
    }
    buf_printf(&out, "%%define DISP_KEY                   0x%02X\n", card.disp_key);
    buf_printf(&out, "%%define DISP_ID                    0x%02X\n", card.disp_id);
    buf_printf(&out, "%%define DISP_REVISION              0x%02X\n", card.disp_revision);
    buf_printf(&out, "%%define DISP_CTRL_ENABLE           0x%02X\n", DISP_CTRL_ENABLE);
    buf_printf(&out, "%%define DISP_CTRL_DSCAN            0x%02X\n", DISP_CTRL_DSCAN);
    buf_printf(&out, "%%define DISP_GPU_RESET             0x%02X\n", DISP_GPU_RESET);
    buf_printf(&out, "%%define DISP_MAX_WIDTH             %u\n", card.limits.max_width);
    buf_printf(&out, "%%define DISP_MAX_HEIGHT            %u\n", card.limits.max_height);
    buf_printf(&out, "%%define DISP_REFRESH_MIN           %u\n", card.limits.refresh_min);
    buf_printf(&out, "%%define DISP_REFRESH_MAX           %u\n", card.limits.refresh_max);
    buf_printf(&out, "\n; GPU registers in BAR0 (spec/gpu-registers.md)\n");
    for (i = 0; i < sizeof(gpu_regs) / sizeof(gpu_regs[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "GPU_REG_%s", gpu_regs[i].name);
        buf_printf(&out, "%%define %-26s 0x%02X\n", name, gpu_regs[i].offset);
    }
    for (i = 0; i < sizeof(gpu_bits) / sizeof(gpu_bits[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "GPU_%s", gpu_bits[i].name);
        buf_printf(&out, "%%define %-26s 0x%08X\n", name, gpu_bits[i].value);
    }
    buf_printf(&out, "%%define GPU_ID_MAGIC               0x%08X\n", card.id_magic);
    buf_printf(&out, "%%define GPU_HW_VERSION             0x%08X\n", card.hw_version);
    buf_printf(&out, "%%define GPU_RESERVE                0x%08X\n", card.gpu_reserve);
    buf_printf(&out, "\n");
    nasm_string(&out, "CARD_STR_VERSION", card.version);
    nasm_string(&out, "CARD_STR_VENDOR", card.vendor_name);
    nasm_string(&out, "CARD_STR_PRODUCT", card.product_name);
    nasm_string(&out, "CARD_STR_SIGNON", card.signon);
    nasm_string(&out, "CARD_STR_COPYRIGHT", card.copyright);
    write_out(dir, "card.inc", &out);
}

static void
gen_vgamodes_inc(const char *dir)
{
    buf_t out = { 0 };
    int   i;

    buf_printf(&out, "; Generated by cardc from card/vga-modes.card. Do not edit.\n\n");
    buf_printf(&out, "VGA_PARAM_ENTRIES   equ %d\n", PARAM_ENTRIES);
    buf_printf(&out, "VGA_PARAM_SIZE      equ %d\n", PARAM_SIZE);
    buf_printf(&out, "VGA_MODE_COUNT      equ %d\n\n", MODE_COUNT);
    for (i = 0; i < 6; i++)
        buf_printf(&out, "MODEL_%-12s equ %d\n", upper(model_names[i]), i);
    buf_printf(&out, "\n");
    for (i = 0; i < PAL_COUNT; i++)
        buf_printf(&out, "PAL_%-14s equ %d\n", upper(palette_names[i]), i);
    buf_printf(&out, "\n; Mode descriptor layout\n");
    buf_printf(&out, "MODE_MODEL          equ 0   ; MODEL_*\n");
    buf_printf(&out, "MODE_FLAGS          equ 1   ; MODE_FLAG_*\n");
    buf_printf(&out, "MODE_SEGMENT        equ 2   ; word\n");
    buf_printf(&out, "MODE_PARAMS         equ 4   ; parameter entry for 200, 350, 400 lines\n");
    buf_printf(&out, "MODE_PALETTES       equ 7   ; PAL_* for 200, 350, 400 lines\n");
    buf_printf(&out, "MODE_SIZE           equ 10\n");
    buf_printf(&out, "MODE_FLAG_MONO      equ 0x01\n\n");

    buf_printf(&out, "vga_param_table:\n");
    for (i = 0; i < PARAM_ENTRIES; i++) {
        buf_printf(&out, "    ; entry %02Xh%s\n", i, params[i].reserved ? " (reserved)" : "");
        bytes_rows(&out, params[i].bytes, PARAM_SIZE, 16);
    }

    buf_printf(&out, "\nvga_mode_table:\n");
    for (i = 0; i < MODE_COUNT; i++) {
        const vga_mode_t *m = &modes[i];

        buf_printf(&out, "    ; mode %02Xh\n", i);
        buf_printf(&out, "    db MODEL_%s, 0x%02X\n", upper(model_names[m->model]), m->mono ? 1 : 0);
        buf_printf(&out, "    dw 0x%04X\n", m->segment);
        buf_printf(&out, "    db 0x%02X, 0x%02X, 0x%02X\n", m->params[0], m->params[1], m->params[2]);
        buf_printf(&out, "    db PAL_%s, PAL_%s, PAL_%s\n", upper(palette_names[m->palettes[0]]),
                   upper(palette_names[m->palettes[1]]), upper(palette_names[m->palettes[2]]));
    }
    write_out(dir, "vgamodes.inc", &out);
}

static void
gen_palettes_inc(const char *dir)
{
    buf_t out = { 0 };
    int   p;

    buf_printf(&out, "; Generated by cardc. Do not edit.\n");
    buf_printf(&out, "; Default DAC palettes as 6-bit red, green, blue triples.\n\n");
    for (p = 0; p < 3; p++) {
        buf_printf(&out, "pal_%s:\n", palette_names[p]);
        bytes_rows(&out, &pal64[p][0][0], 64 * 3, 12);
    }
    buf_printf(&out, "pal_vga256:\n");
    bytes_rows(&out, &pal256[0][0], 256 * 3, 12);
    write_out(dir, "palettes.inc", &out);
}

/* VBE mode table for the BIOS: one entry per enabled mode and depth with a VBE number,
   sorted by number. Entry: number, width, height, pitch (bytes), bpp, default refresh. */
static void
gen_vbemodes_inc(const char *dir)
{
    buf_t    out = { 0 };
    uint32_t n;
    size_t   i;
    size_t   d;
    unsigned count = 0;

    buf_printf(&out, "; Generated by cardc from card/display-modes.card. Do not edit.\n\n");
    buf_printf(&out, "VBE_ENTRY_NUMBER    equ 0   ; word\n");
    buf_printf(&out, "VBE_ENTRY_WIDTH     equ 2   ; word\n");
    buf_printf(&out, "VBE_ENTRY_HEIGHT    equ 4   ; word\n");
    buf_printf(&out, "VBE_ENTRY_PITCH     equ 6   ; word, bytes per line\n");
    buf_printf(&out, "VBE_ENTRY_BPP       equ 8   ; byte: 8, 15, 16 or 32\n");
    buf_printf(&out, "VBE_ENTRY_REFRESH   equ 9   ; byte, Hz\n");
    buf_printf(&out, "VBE_ENTRY_SIZE      equ 10\n\n");
    buf_printf(&out, "vbe_mode_table:\n");
    for (n = 0x100; n <= VBE_LAST; n++) {
        for (i = 0; i < dmodes.count; i++) {
            const dmode_t *m = &dmodes.mode[i];

            for (d = 0; d < MODE_DEPTHS; d++) {
                if (!m->enabled || (m->vbe[d] != n))
                    continue;
                buf_printf(&out, "    dw 0x%03X, %4u, %4u, %4u\n    db %2u, %3u\n", n, m->width, m->height,
                           m->width * mode_bytes_per_pixel(mode_bpp[d]), mode_bpp[d], m->refresh[0]);
                count++;
            }
        }
    }
    /* The BIOS builds the mode list in the 222-byte reserved area of the controller
       information block: 110 numbers and the end mark (spec/bios.md). */
    if (count > VBE_LIST_MAX)
        die("%u VBE modes; the BIOS mode list holds at most %u", count, VBE_LIST_MAX);
    buf_printf(&out, "    dw 0xFFFF                          ; end\n");
    buf_printf(&out, "VBE_MODE_COUNT      equ %u\n", count);
    write_out(dir, "vbemodes.inc", &out);
}

/* Desktop mode table for the Windows driver: width, height, refresh count, rates. */
static void
gen_drvmodes_inc(const char *dir)
{
    buf_t    out = { 0 };
    size_t   i;
    size_t   j;
    unsigned count = 0;

    buf_printf(&out, "; Generated by cardc from card/display-modes.card. Do not edit.\n\n");
    buf_printf(&out, "DRV_MODE_WIDTH      equ 0   ; word\n");
    buf_printf(&out, "DRV_MODE_HEIGHT     equ 2   ; word\n");
    buf_printf(&out, "DRV_MODE_RATES      equ 4   ; byte: number of refresh rates\n");
    buf_printf(&out, "DRV_MODE_RATE       equ 5   ; bytes: rates in Hz, the default first\n");
    buf_printf(&out, "DRV_MODE_VBE8       equ %d  ; word: VBE mode number at 8 bpp (0 = none)\n", 5 + MODES_REFRESH);
    buf_printf(&out, "DRV_MODE_VBE16      equ %d  ; word: at 16 bpp\n", 7 + MODES_REFRESH);
    buf_printf(&out, "DRV_MODE_VBE32      equ %d  ; word: at 32 bpp\n", 9 + MODES_REFRESH);
    buf_printf(&out, "DRV_MODE_SIZE       equ %d\n\n", 11 + MODES_REFRESH);
    buf_printf(&out, "drv_mode_table:\n");
    for (i = 0; i < dmodes.count; i++) {
        const dmode_t *m = &dmodes.mode[i];

        if (!m->enabled || !m->desktop)
            continue;
        buf_printf(&out, "    dw %4u, %4u\n    db %u", m->width, m->height, (unsigned) m->n_refresh);
        for (j = 0; j < MODES_REFRESH; j++)
            buf_printf(&out, ", %u", (j < m->n_refresh) ? m->refresh[j] : 0);
        /* mode_bpp: 8, 15, 16, 32 */
        buf_printf(&out, "\n    dw 0x%03X, 0x%03X, 0x%03X\n", m->vbe[0], m->vbe[2], m->vbe[3]);
        count++;
    }
    buf_printf(&out, "DRV_MODE_COUNT      equ %u\n\n", count);

    /* Every enabled resolution, the low-resolution game modes included: the
       modes DirectDraw may set (at 8, 16 and 32 bpp). */
    buf_printf(&out, "; Every enabled resolution: width, height (DirectDraw's mode list)\n");
    buf_printf(&out, "drv_ddmode_table:\n");
    count = 0;
    for (i = 0; i < dmodes.count; i++) {
        const dmode_t *m = &dmodes.mode[i];

        if (!m->enabled)
            continue;
        buf_printf(&out, "    dw %4u, %4u\n", m->width, m->height);
        count++;
    }
    buf_printf(&out, "DRV_DDMODE_COUNT    equ %u\n", count);
    write_out(dir, "drvmodes.inc", &out);
}

static void
gen_header(const char *dir)
{
    buf_t  out = { 0 };
    size_t i;

    buf_printf(&out,
               "/*\n"
               " * 86Box    A hypervisor and IBM PC system emulator that specializes in\n"
               " *          running old operating systems and software designed for IBM\n"
               " *          PC systems and compatibles from 1981 through fairly recent\n"
               " *          system designs based on the PCI bus.\n"
               " *\n"
               " *          This file is part of the 86Box distribution.\n"
               " *\n"
               " *          VGPU98 card constants.\n"
               " *\n"
               " *          Generated by cardc from card/vgpu98.card in the VGPU98\n"
               " *          project. Do not edit; change the card definition instead.\n"
               " *\n"
               " * Authors: VGPU98 Project contributors\n"
               " *\n"
               " *          Copyright 2026 VGPU98 Project contributors.\n"
               " */\n"
               "#ifndef VIDEO_VGPU98_CARD_H\n"
               "#define VIDEO_VGPU98_CARD_H\n\n");

    c_string(&out, "VGPU98_CARD_NAME", card.name);
    c_string(&out, "VGPU98_CARD_INTERNAL_NAME", card.internal_name);
    c_string(&out, "VGPU98_ROM_PATH", card.rom_path);
    buf_printf(&out, "#define %-31s 0x%05X\n\n", "VGPU98_ROM_SIZE", card.rom_size);

    buf_printf(&out, "#define %-31s 0x%04X\n", "VGPU98_PCI_VENDOR_ID", card.vendor_id);
    buf_printf(&out, "#define %-31s 0x%04X\n", "VGPU98_PCI_DEVICE_ID", card.device_id);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_PCI_REVISION", card.revision);
    buf_printf(&out, "#define %-31s 0x%06X\n", "VGPU98_PCI_CLASS_CODE", card.class_code);
    buf_printf(&out, "#define %-31s 0x%04X\n", "VGPU98_PCI_SUBSYS_VENDOR_ID", card.subsys_vendor_id);
    buf_printf(&out, "#define %-31s 0x%04X\n", "VGPU98_PCI_SUBSYS_ID", card.subsys_id);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_BAR0_SIZE", card.bar0_size);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_BAR1_SIZE", card.bar1_size);
    buf_printf(&out, "#define %-31s 0x%08X\n\n", "VGPU98_ROM_BAR_SIZE", card.rom_bar_size);

    buf_printf(&out, "#define %-31s %u\n", "VGPU98_VRAM_DEFAULT_MB", card.vram_default_mb);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_VRAM_MAX_MB", card.vram_mb[card.n_vram - 1]);
    buf_printf(&out, "#define VGPU98_VRAM_SELECTION");
    for (i = 0; i < card.n_vram; i++) {
        char desc[32];

        snprintf(desc, sizeof(desc), "\"%u MB\",", card.vram_mb[i]);
        buf_printf(&out, " \\\n    { .description = %-9s .value = %3u },", desc, card.vram_mb[i]);
    }
    buf_printf(&out, "\n\n");

    buf_printf(&out, "/* BAR0 registers (spec/registers.md, spec/gpu-registers.md). */\n");
    for (i = 0; i < sizeof(gpu_regs) / sizeof(gpu_regs[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "VGPU98_REG_%s", gpu_regs[i].name);
        buf_printf(&out, "#define %-31s 0x%02X\n", name, gpu_regs[i].offset);
    }
    for (i = 0; i < sizeof(gpu_bits) / sizeof(gpu_bits[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "VGPU98_%s", gpu_bits[i].name);
        buf_printf(&out, "#define %-31s 0x%08X\n", name, gpu_bits[i].value);
    }
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_ID_MAGIC", card.id_magic);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_HW_VERSION", card.hw_version);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_PROFILE", card.profile);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_COMMAND_SET", card.command_set);
    buf_printf(&out, "#define %-31s 0x%08X\n", "VGPU98_GPU_RESERVE", card.gpu_reserve);
    buf_printf(&out, "#define %-31s 0x%02X\n\n", "VGPU98_DEBUG_SEQ_INDEX", card.debug_seq_index);

    buf_printf(&out, "/* Display engine registers: sequencer indices (spec/display-engine.md). */\n");
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_SR_DISP_FIRST", card.disp_first_index);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_SR_DISP_LAST", card.disp_first_index + DISP_REG_SPAN - 1);
    for (i = 0; i < sizeof(disp_regs) / sizeof(disp_regs[0]); i++) {
        char name[40];

        snprintf(name, sizeof(name), "VGPU98_SR_DISP_%s", disp_regs[i].name);
        buf_printf(&out, "#define %-31s 0x%02X\n", name, card.disp_first_index + disp_regs[i].offset);
    }
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_KEY", card.disp_key);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_ID", card.disp_id);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_REVISION", card.disp_revision);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_FEATURES", DISP_FEATURES);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_FORMATS", DISP_FORMATS);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_CTRL_ENABLE", DISP_CTRL_ENABLE);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_CTRL_DSCAN", DISP_CTRL_DSCAN);
    buf_printf(&out, "#define %-31s 0x%02X\n", "VGPU98_DISP_GPU_RESET", DISP_GPU_RESET);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_MAX_WIDTH", card.limits.max_width);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_MAX_HEIGHT", card.limits.max_height);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_REFRESH_MIN", card.limits.refresh_min);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_REFRESH_MAX", card.limits.refresh_max);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_H_BLANK", MODE_H_BLANK);
    buf_printf(&out, "#define %-31s %u\n", "VGPU98_DISP_V_BLANK", MODE_V_BLANK);
    buf_printf(&out, "#define %-31s %u\n\n", "VGPU98_DISP_V_DIV", MODE_V_DIV);

    buf_printf(&out, "/* EDID 1.3 block of the card's virtual monitor (card/vgpu98.card [monitor]). */\n");
    buf_printf(&out, "#define VGPU98_EDID_BYTES");
    for (i = 0; i < EDID_SIZE; i++)
        buf_printf(&out, "%s0x%02X%s", (i % 12) ? " " : " \\\n    ", edid[i], (i == EDID_SIZE - 1) ? "" : ",");
    buf_printf(&out, "\n\n");

    buf_printf(&out, "#endif /* VIDEO_VGPU98_CARD_H */\n");
    write_out(dir, "vid_vgpu98_card.h", &out);
}

static void
usage(void)
{
    die("usage: cardc --card FILE --modes FILE --display FILE --out-dir DIR");
}

int
main(int argc, char **argv)
{
    const char *card_path  = NULL;
    const char *modes_path = NULL;
    const char *disp_path  = NULL;
    const char *out_dir    = NULL;
    int         i;

    set_progname("cardc");
    for (i = 1; i < argc; i++) {
        if ((i + 1 < argc) && (strcmp(argv[i], "--card") == 0))
            card_path = argv[++i];
        else if ((i + 1 < argc) && (strcmp(argv[i], "--modes") == 0))
            modes_path = argv[++i];
        else if ((i + 1 < argc) && (strcmp(argv[i], "--display") == 0))
            disp_path = argv[++i];
        else if ((i + 1 < argc) && (strcmp(argv[i], "--out-dir") == 0))
            out_dir = argv[++i];
        else
            usage();
    }
    if (!card_path || !modes_path || !disp_path || !out_dir)
        usage();

    load_card(card_path);
    load_modes(modes_path);
    modes_load(disp_path, &card.limits, &dmodes);
    edid_build(edid, &card.monitor, &dmodes);
    build_palettes(pal64, pal256);

    gen_card_inc(out_dir);
    gen_vgamodes_inc(out_dir);
    gen_palettes_inc(out_dir);
    gen_vbemodes_inc(out_dir);
    gen_drvmodes_inc(out_dir);
    gen_header(out_dir);
    return 0;
}
