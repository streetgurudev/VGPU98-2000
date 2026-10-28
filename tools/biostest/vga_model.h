/*
 * biostest: a model of the IBM VGA registers and video memory, written for the
 * BIOS tests. It is independent of 86Box's VGA core on purpose. It also models
 * the card's display engine (spec/display-engine.md) from its specification.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_VGA_MODEL_H
#define VGPU98_VGA_MODEL_H

#include <stdint.h>

typedef struct vga_model_t {
    uint8_t  misc;
    uint8_t  seq_index;
    uint8_t  seq[8];
    uint8_t  crtc_index;
    uint8_t  crtc[32];
    uint8_t  gc_index;
    uint8_t  gc[16];
    uint8_t  attr_index;  /* includes bit 5, the palette address source */
    uint8_t  attr_ff;     /* 0 = next write is an index */
    uint8_t  attr[32];
    uint8_t  pel_mask;
    uint8_t  dac[256][3];
    uint8_t  dac_addr;    /* one address register, as in 86Box: 3C7h sets it to index + 1 */
    uint8_t  dac_status;  /* 3C7h reads: 00h after a write to 3C8h, 03h after 3C7h */
    uint8_t  dac_comp;    /* red, green, blue position */
    uint8_t  dac_rgb[3];  /* a write takes effect when blue arrives */
    uint8_t  planes[4][65536];
    uint8_t  latch[4];
    uint8_t  status_toggle;
    uint8_t  port61;
    uint8_t  refresh;
    int      debug_on;           /* the card's debug log setting: index E0h reads 01h when on */
    int      debug_writes;       /* writes to the debug index, whatever the setting */
    int      protect_violations; /* writes to CRTC 0-7 while protected */
    int      wrong_port_accesses;  /* 3Bx/3Dx accesses that the current mode does not decode */
    char     debug[8192];
    int      debug_len;

    /* Display engine. Its video memory is kept apart from the VGA planes: the
       tests only look at one or the other at a time. */
    int      disp_unlocked;
    uint8_t  disp[64];           /* SR 80h-BFh as stored (writable registers only) */
    uint8_t  edid_index;
    uint8_t  edid[128];
    uint32_t vram_size;
    uint32_t lfb_base;           /* LFB_BASE reads: 0 = BAR1 unassigned */
    uint32_t regs_base;          /* REGS_BASE reads: 0 = BAR0 unassigned */
    int      gpu_resets;         /* GPU_RESET writes of 01h, unlocked */
    uint8_t *vram;
    int      disp_locked_writes; /* writes to SR 81h-BFh while locked */
    int      status_reads_after_start; /* input status reads since START's high byte was written */
} vga_model_t;

/* Resets everything; video memory is vram_size bytes, zeroed. */
void     vga_reset(vga_model_t *vga, uint32_t vram_size);
int      vga_disp_enabled(const vga_model_t *vga);
uint32_t vga_disp_value(const vga_model_t *vga, uint8_t sr, int bytes); /* stored register value */
uint8_t  vga_in(vga_model_t *vga, uint16_t port);
void     vga_out(vga_model_t *vga, uint16_t port, uint8_t val);
uint8_t  vga_mem_read(vga_model_t *vga, uint32_t offset); /* offset from A0000h */
void     vga_mem_write(vga_model_t *vga, uint32_t offset, uint8_t val);

#endif /* VGPU98_VGA_MODEL_H */
