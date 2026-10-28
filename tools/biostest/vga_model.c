/*
 * biostest: a model of the IBM VGA registers and video memory.
 *
 * Memory addressing follows the VGA hardware and 86Box's svga core: chain-4
 * maps byte A to plane A & 3 at offset A & ~3; odd/even maps byte A to the even
 * or odd planes (A & 1) at offset A & ~1; otherwise every enabled plane at
 * offset A. Every read loads the latches, and every write goes through the
 * write mode, set/reset, logical function and bit mask, chained or not. The
 * DAC has one address register, as in 86Box.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "vga_model.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "vid_vgpu98_card.h"

static const uint8_t edid[128] = { VGPU98_EDID_BYTES };

/* The card's developer debug register (sequencer index); the tests act as if the setting is on. */
#define DEBUG_SEQ_INDEX 0xe0

void
vga_reset(vga_model_t *vga, uint32_t vram_size)
{
    free(vga->vram);
    memset(vga, 0, sizeof(*vga));
    vga->pel_mask  = 0xff;
    vga->debug_on  = 1;
    vga->vram_size = vram_size;
    vga->vram      = xcalloc(1, vram_size);
    memcpy(vga->edid, edid, sizeof(edid));
}

/* --- Display engine (spec/display-engine.md) --- */

#define DISP(vga, sr) ((vga)->disp[(sr) - VGPU98_SR_DISP_FIRST])

static int
is_disp(uint8_t sr)
{
    return (sr >= VGPU98_SR_DISP_FIRST) && (sr <= VGPU98_SR_DISP_LAST);
}

/* Registers software can write and read back, by sequencer index. */
static int
disp_writable(uint8_t sr)
{
    return ((sr >= VGPU98_SR_DISP_WIDTH) && (sr <= VGPU98_SR_DISP_REFRESH)) || (sr == VGPU98_SR_DISP_BANK) ||
           (sr == VGPU98_SR_DISP_BANK + 1) || (sr == VGPU98_SR_DISP_SCRATCH) || (sr == VGPU98_SR_DISP_SCRATCH + 1);
}

int
vga_disp_enabled(const vga_model_t *vga)
{
    return DISP(vga, VGPU98_SR_DISP_CONTROL) & VGPU98_DISP_CTRL_ENABLE;
}

uint32_t
vga_disp_value(const vga_model_t *vga, uint8_t sr, int bytes)
{
    uint32_t v = 0;

    while (bytes-- > 0)
        v = (v << 8) | DISP(vga, sr + bytes);
    return v;
}

static uint8_t
disp_in(vga_model_t *vga, uint8_t sr)
{
    uint8_t v;

    if (sr == VGPU98_SR_DISP_KEY)
        return vga->disp_unlocked ? 0x01 : 0x00;
    if (!vga->disp_unlocked)
        return 0x00;
    if ((sr >= VGPU98_SR_DISP_MAX_WIDTH) && (sr < VGPU98_SR_DISP_MAX_WIDTH + 2))
        return (uint8_t) (VGPU98_DISP_MAX_WIDTH >> (8 * (sr - VGPU98_SR_DISP_MAX_WIDTH)));
    if ((sr >= VGPU98_SR_DISP_VRAM_SIZE) && (sr < VGPU98_SR_DISP_VRAM_SIZE + 4))
        return (uint8_t) (vga->vram_size >> (8 * (sr - VGPU98_SR_DISP_VRAM_SIZE)));
    if ((sr >= VGPU98_SR_DISP_LFB_BASE) && (sr < VGPU98_SR_DISP_LFB_BASE + 4))
        return (uint8_t) (vga->lfb_base >> (8 * (sr - VGPU98_SR_DISP_LFB_BASE)));
    if ((sr >= VGPU98_SR_DISP_REGS_BASE) && (sr < VGPU98_SR_DISP_REGS_BASE + 4))
        return (uint8_t) (vga->regs_base >> (8 * (sr - VGPU98_SR_DISP_REGS_BASE)));
    switch (sr) {
        case VGPU98_SR_DISP_ID:
            return VGPU98_DISP_ID;
        case VGPU98_SR_DISP_REVISION:
            return VGPU98_DISP_REVISION;
        case VGPU98_SR_DISP_FEATURES:
            return VGPU98_DISP_FEATURES;
        case VGPU98_SR_DISP_FORMATS:
            return VGPU98_DISP_FORMATS;
        case VGPU98_SR_DISP_CONTROL:
        case VGPU98_SR_DISP_FORMAT:
            return DISP(vga, sr);
        case VGPU98_SR_DISP_EDID_INDEX:
            return vga->edid_index;
        case VGPU98_SR_DISP_EDID_DATA:
            v               = vga->edid[vga->edid_index];
            vga->edid_index = (vga->edid_index + 1) & 0x7f;
            return v;
        default:
            return disp_writable(sr) ? DISP(vga, sr) : 0x00;
    }
}

static void
disp_out(vga_model_t *vga, uint8_t sr, uint8_t val)
{
    if (sr == VGPU98_SR_DISP_KEY) {
        vga->disp_unlocked = (val == VGPU98_DISP_KEY);
        return;
    }
    if (!vga->disp_unlocked) {
        vga->disp_locked_writes++;
        return;
    }
    switch (sr) {
        case VGPU98_SR_DISP_CONTROL:
            DISP(vga, sr) = val & VGPU98_DISP_CTRL_ENABLE;
            break;
        case VGPU98_SR_DISP_FORMAT:
            if ((val == 8) || (val == 15) || (val == 16) || (val == 32))
                DISP(vga, sr) = val;
            break;
        case VGPU98_SR_DISP_EDID_INDEX:
            vga->edid_index = val & 0x7f;
            break;
        case VGPU98_SR_DISP_GPU_RESET:
            if (val & VGPU98_DISP_GPU_RESET)
                vga->gpu_resets++;
            break;
        default:
            if (disp_writable(sr))
                DISP(vga, sr) = val;
            if (sr == VGPU98_SR_DISP_START + 3) /* the engine takes START with this byte */
                vga->status_reads_after_start = 0;
            break;
    }
}

/* The A0000h window in extended mode: 64 KB of video memory at BANK x 64 KB.
   Returns NULL outside it (B0000h-BFFFFh is not decoded). */
static uint8_t *
window_byte(vga_model_t *vga, uint32_t offset)
{
    uint32_t bank = vga_disp_value(vga, VGPU98_SR_DISP_BANK, 2);

    if (offset >= 0x10000)
        return NULL;
    return &vga->vram[((bank << 16) + offset) & (vga->vram_size - 1)];
}

static int
color_ports(const vga_model_t *vga)
{
    return vga->misc & 1;
}

uint8_t
vga_in(vga_model_t *vga, uint16_t port)
{
    /* Reading the other input status register is a common, harmless way to reset
       the attribute flip-flop whichever port set is active, so it is not counted. */
    if ((port >= 0x3b0) && (port <= 0x3bf) && color_ports(vga)) {
        if (port != 0x3ba)
            vga->wrong_port_accesses++;
        return 0xff;
    }
    if ((port >= 0x3d0) && (port <= 0x3df) && !color_ports(vga)) {
        if (port != 0x3da)
            vga->wrong_port_accesses++;
        return 0xff;
    }

    switch (port) {
        case 0x3c0:
            return vga->attr_index;
        case 0x3c1:
            return vga->attr[vga->attr_index & 0x1f];
        case 0x3c2:
            return 0x10; /* switch sense: monitor present */
        case 0x3c4:
            return vga->seq_index;
        case 0x3c5:
            if (vga->seq_index == DEBUG_SEQ_INDEX)
                return vga->debug_on ? 0x01 : 0x00;
            if (is_disp(vga->seq_index))
                return disp_in(vga, vga->seq_index);
            return vga->seq[vga->seq_index & 7];
        case 0x3c6:
            return vga->pel_mask;
        case 0x3c7:
            return vga->dac_status;
        case 0x3c8:
            return vga->dac_addr;
        case 0x3c9: {
            uint8_t v = vga->dac[(uint8_t) (vga->dac_addr - 1)][vga->dac_comp];

            if (++vga->dac_comp == 3) {
                vga->dac_comp = 0;
                vga->dac_addr++;
            }
            return v;
        }
        case 0x3cc:
            return vga->misc;
        case 0x3ce:
            return vga->gc_index;
        case 0x3cf:
            return vga->gc[vga->gc_index & 15];
        case 0x3b4:
        case 0x3d4:
            return vga->crtc_index;
        case 0x3b5:
        case 0x3d5:
            return vga->crtc[vga->crtc_index & 31];
        case 0x3ba:
        case 0x3da:
            vga->status_reads_after_start++;
            vga->attr_ff = 0;
            vga->status_toggle ^= 0x09;
            return vga->status_toggle;
        case 0x61:
            vga->refresh ^= 0x10;
            return (uint8_t) ((vga->port61 & ~0x10) | vga->refresh);
        default:
            return 0xff;
    }
}

void
vga_out(vga_model_t *vga, uint16_t port, uint8_t val)
{
    if ((port >= 0x3b0) && (port <= 0x3bf) && color_ports(vga)) {
        vga->wrong_port_accesses++;
        return;
    }
    if ((port >= 0x3d0) && (port <= 0x3df) && !color_ports(vga)) {
        vga->wrong_port_accesses++;
        return;
    }

    switch (port) {
        case 0x3c0:
            if (!vga->attr_ff)
                vga->attr_index = val & 0x3f;
            else
                vga->attr[vga->attr_index & 0x1f] = val;
            vga->attr_ff ^= 1;
            break;
        case 0x3c2:
            vga->misc = val;
            break;
        case 0x3c4:
            vga->seq_index = val;
            break;
        case 0x3c5:
            if (vga->seq_index == DEBUG_SEQ_INDEX) {
                vga->debug_writes++;
                if (vga->debug_on && (vga->debug_len < (int) sizeof(vga->debug) - 1))
                    vga->debug[vga->debug_len++] = (char) val;
            } else if (is_disp(vga->seq_index))
                disp_out(vga, vga->seq_index, val);
            else if (vga->seq_index < 8)
                vga->seq[vga->seq_index] = val;
            break;
        case 0x3c6:
            vga->pel_mask = val;
            break;
        case 0x3c7:
        case 0x3c8:
            vga->dac_addr   = (uint8_t) (val + (port & 1));
            vga->dac_status = (uint8_t) (port & 3);
            vga->dac_comp   = 0;
            break;
        case 0x3c9:
            vga->dac_rgb[vga->dac_comp] = val & 0x3f;
            if (++vga->dac_comp == 3) {
                memcpy(vga->dac[vga->dac_addr], vga->dac_rgb, 3);
                vga->dac_comp = 0;
                vga->dac_addr++;
            }
            break;
        case 0x3ce:
            vga->gc_index = val;
            break;
        case 0x3cf:
            vga->gc[vga->gc_index & 15] = val;
            break;
        case 0x3b4:
        case 0x3d4:
            vga->crtc_index = val;
            break;
        case 0x3b5:
        case 0x3d5: {
            uint8_t i = vga->crtc_index & 31;

            if ((i < 8) && (vga->crtc[0x11] & 0x80)) {
                if (i == 7) /* only the line compare bit stays writable */
                    vga->crtc[7] = (uint8_t) ((vga->crtc[7] & ~0x10) | (val & 0x10));
                else
                    vga->protect_violations++;
                break;
            }
            vga->crtc[i] = val;
            break;
        }
        case 0x61:
            vga->port61 = val;
            break;
        default:
            break;
    }
}

/* Applies the memory map select; returns 0 if the offset is outside the window. */
static int
map(const vga_model_t *vga, uint32_t *offset)
{
    uint32_t a = *offset;

    switch ((vga->gc[6] >> 2) & 3) {
        case 0:
            break;
        case 1:
            if (a >= 0x10000)
                return 0;
            break;
        case 2:
            if ((a < 0x10000) || (a >= 0x18000))
                return 0;
            a -= 0x10000;
            break;
        default:
            if (a < 0x18000)
                return 0;
            a -= 0x18000;
            break;
    }
    *offset = a;
    return 1;
}

static uint8_t
rotate(uint8_t v, int n)
{
    n &= 7;
    return (uint8_t) ((v >> n) | (v << (8 - n)));
}

uint8_t
vga_mem_read(vga_model_t *vga, uint32_t offset)
{
    uint32_t a = offset;
    uint32_t o;
    int      plane;
    int      p;

    if (vga_disp_enabled(vga)) {
        uint8_t *b = window_byte(vga, offset);

        return b ? *b : 0xff;
    }
    if (!map(vga, &a))
        return 0xff;

    if (vga->seq[4] & 0x08) { /* chain-4 */
        o     = a & 0xfffc;
        plane = (int) (a & 3);
    } else if (vga->gc[5] & 0x10) { /* odd/even read */
        o     = a & 0xfffe;
        plane = (vga->gc[4] & 2) | (int) (a & 1);
    } else {
        o     = a & 0xffff;
        plane = vga->gc[4] & 3;
    }

    for (p = 0; p < 4; p++)
        vga->latch[p] = vga->planes[p][o];

    if (vga->gc[5] & 0x08) {
        /* read mode 1: colour compare */
        uint8_t result = 0xff;

        for (p = 0; p < 4; p++) {
            if (!(vga->gc[7] & (1 << p)))
                continue;
            if (vga->gc[2] & (1 << p))
                result &= vga->latch[p];
            else
                result &= (uint8_t) ~vga->latch[p];
        }
        return result;
    }
    return vga->latch[plane];
}

void
vga_mem_write(vga_model_t *vga, uint32_t offset, uint8_t val)
{
    uint32_t a      = offset;
    uint8_t  mask   = vga->gc[8];
    int      mode   = vga->gc[5] & 3;
    int      func   = (vga->gc[3] >> 3) & 3;
    uint8_t  planes = vga->seq[2] & 0x0f;
    uint32_t o;
    int      p;

    if (vga_disp_enabled(vga)) {
        uint8_t *b = window_byte(vga, offset);

        if (b)
            *b = val;
        return;
    }
    if (!map(vga, &a))
        return;

    if (vga->seq[4] & 0x08) { /* chain-4 */
        o = a & 0xfffc;
        planes &= (uint8_t) (1 << (a & 3));
    } else if (!(vga->seq[4] & 0x04)) { /* odd/even: even planes for even bytes */
        o = a & 0xfffe;
        planes &= (uint8_t) (0x05 << (a & 1));
    } else
        o = a & 0xffff;

    for (p = 0; p < 4; p++) {
        uint8_t v;
        uint8_t m = mask;
        uint8_t l = vga->latch[p];

        if (!(planes & (1 << p)))
            continue;
        switch (mode) {
            case 0:
                v = rotate(val, vga->gc[3]);
                if (vga->gc[1] & (1 << p))
                    v = (vga->gc[0] & (1 << p)) ? 0xff : 0x00;
                break;
            case 1:
                vga->planes[p][o] = l;
                continue;
            case 2:
                v = (val & (1 << p)) ? 0xff : 0x00;
                break;
            default:
                v = (vga->gc[0] & (1 << p)) ? 0xff : 0x00;
                m &= rotate(val, vga->gc[3]);
                break;
        }
        switch (func) {
            case 1:
                v &= l;
                break;
            case 2:
                v |= l;
                break;
            case 3:
                v ^= l;
                break;
            default:
                break;
        }
        vga->planes[p][o] = (uint8_t) ((v & m) | (l & ~m));
    }
}
