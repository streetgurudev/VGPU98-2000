/*
 * romlint: checks a finished option ROM image against the card definition.
 *
 * Checks the expansion ROM header (signature, size, reserved bytes, and the
 * "IBM VGA Compatible" signature for a VGA class card), the checksum, the entry
 * point (a jump into code, not into a header or data structure), the PCI data
 * structure (IDs, class, length, code type, indicator, reserved word) and, if
 * present, the $PnP expansion header. Layout reference: spec/rom-layout.md.
 *
 * Usage: romlint ROM --card FILE
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "util.h"

static const char *rom_path;
static int         errors;

static void
fail(const char *fmt, ...) VGPU98_PRINTF(1, 2);

static void
fail(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "romlint: %s: ", rom_path);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    errors++;
}

#define VGA_SIGNATURE     "IBM VGA Compatible"
#define VGA_SIGNATURE_OFF 0x1e

static unsigned
rd16(const uint8_t *p)
{
    return (unsigned) p[0] | ((unsigned) p[1] << 8);
}

/* Structures the entry jump must not land in. */
typedef struct region_t {
    unsigned    start;
    unsigned    end; /* exclusive */
    const char *name;
} region_t;

static region_t regions[4];
static int      n_regions;

static void
add_region(unsigned start, unsigned end, const char *name)
{
    regions[n_regions].start = start;
    regions[n_regions].end   = end;
    regions[n_regions].name  = name;
    n_regions++;
}

static int
all_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

/* An ASCIIZ string of printable characters starting inside the image. */
static int
valid_string(const uint8_t *rom, size_t size, unsigned off)
{
    size_t i;

    if (off == 0 || off >= size)
        return 0;
    for (i = off; i < size; i++) {
        if (rom[i] == 0)
            return i > off;
        if ((rom[i] < 0x20) || (rom[i] > 0x7e))
            return 0;
    }
    return 0;
}

static void
check_pnp(const uint8_t *rom, size_t size, unsigned off)
{
    uint8_t  sum = 0;
    unsigned len;
    unsigned i;

    if ((off & 15) || (off + 32 > size)) {
        fail("$PnP header pointer 0x%04X must be 16-byte aligned and inside the image", off);
        return;
    }
    if (memcmp(rom + off, "$PnP", 4) != 0) {
        fail("no $PnP signature at 0x%04X", off);
        return;
    }
    len = rom[off + 5] * 16u;
    if ((rom[off + 4] != 0x01) || (len < 32) || (off + len > size))
        fail("$PnP header: revision must be 01h and its length must fit the image");
    if (rd16(rom + off + 6) != 0)
        fail("$PnP header: a single header must have next-header offset 0");
    for (i = 0; i < len && off + i < size; i++)
        sum = (uint8_t) (sum + rom[off + i]);
    if (sum != 0)
        fail("$PnP header checksum does not sum to zero");
    if ((rom[off + 0x12] != 0x03) || (rom[off + 0x13] != 0x00) || (rom[off + 0x14] != 0x00))
        fail("$PnP device type must be 03h 00h 00h (VGA display)");
    if (!(rom[off + 0x15] & 0x01))
        fail("$PnP device indicators must mark a display device (bit 0)");
    if (rd16(rom + off + 0x0e) && !valid_string(rom, size, rd16(rom + off + 0x0e)))
        fail("$PnP manufacturer string is not a printable ASCIIZ string inside the image");
    if (rd16(rom + off + 0x10) && !valid_string(rom, size, rd16(rom + off + 0x10)))
        fail("$PnP product string is not a printable ASCIIZ string inside the image");
}

int
main(int argc, char **argv)
{
    const char    *card_path = NULL;
    cfg_t         *cfg;
    cfg_section_t *sec;
    uint32_t       vendor;
    uint32_t       device;
    uint32_t       class_code;
    uint32_t       rom_size;
    uint32_t       revision;
    uint8_t       *rom;
    size_t         size;
    uint8_t        sum = 0;
    unsigned       pcir;
    unsigned       pnp;
    unsigned       target;
    unsigned       header_end;
    int            is_vga;
    size_t         i;
    int            a;

    set_progname("romlint");
    for (a = 1; a < argc; a++) {
        if ((a + 1 < argc) && (strcmp(argv[a], "--card") == 0))
            card_path = argv[++a];
        else if ((argv[a][0] != '-') && (rom_path == NULL))
            rom_path = argv[a];
        else
            die("usage: romlint ROM --card FILE");
    }
    if (!rom_path || !card_path)
        die("usage: romlint ROM --card FILE");

    cfg        = cfg_load(card_path);
    sec        = cfg_section(cfg, "pci");
    vendor     = cfg_get_u32(cfg, sec, "vendor_id", 0, 0xffff);
    device     = cfg_get_u32(cfg, sec, "device_id", 0, 0xffff);
    class_code = cfg_get_u32(cfg, sec, "class_code", 0, 0xffffff);
    sec        = cfg_section(cfg, "bios");
    rom_size   = cfg_get_u32(cfg, sec, "rom_size", 512, 65536);
    revision   = cfg_get_u32(cfg, sec, "revision", 0, 0xffff);

    rom = read_file(rom_path, &size);

    /* Expansion ROM header */
    if (size != rom_size)
        fail("image is %u bytes; the card definition says %u", (unsigned) size, rom_size);
    if (size < 0x40) {
        fail("image is too small to hold a ROM header");
        return 1;
    }
    if ((rom[0] != 0x55) || (rom[1] != 0xaa))
        fail("missing 55h AAh signature");
    if (rom[2] * 512u != size)
        fail("size byte says %u bytes; the image is %u", rom[2] * 512u, (unsigned) size);
    for (i = 0; i < size; i++)
        sum = (uint8_t) (sum + rom[i]);
    if (sum != 0)
        fail("checksum: bytes sum to %02Xh, not 00h", sum);

    /* Reserved header bytes. A short jump leaves offset 5 free as well. */
    if (!all_zero(rom + 6, 0x18 - 6) || ((rom[3] == 0xeb) && (rom[5] != 0)))
        fail("reserved header bytes 0006h-0017h must be zero");
    if (rd16(rom + 0x1c) != 0)
        fail("reserved header word 001Ch must be zero");

    /* A VGA BIOS carries the compatibility signature that software looks for. */
    is_vga     = (class_code == 0x030000);
    header_end = 0x1e;
    if (is_vga) {
        header_end = VGA_SIGNATURE_OFF + (unsigned) strlen(VGA_SIGNATURE);
        if (memcmp(rom + VGA_SIGNATURE_OFF, VGA_SIGNATURE, strlen(VGA_SIGNATURE)) != 0)
            fail("a VGA BIOS must carry \"%s\" at %04Xh", VGA_SIGNATURE, VGA_SIGNATURE_OFF);
    }
    add_region(0, header_end, "the ROM header");
    add_region((unsigned) size - 1, (unsigned) size, "the checksum byte");

    /* PCI data structure */
    pcir = rd16(rom + 0x18);
    if ((pcir & 3) || (pcir + 0x18 > size))
        fail("PCI data structure pointer 0x%04X must be 4-byte aligned and inside the image", pcir);
    else if (pcir < header_end)
        fail("PCI data structure at 0x%04X overlaps the ROM header", pcir);
    else if (memcmp(rom + pcir, "PCIR", 4) != 0)
        fail("no PCIR signature at 0x%04X", pcir);
    else {
        add_region(pcir, pcir + 0x18, "the PCI data structure");
        const uint8_t *p = rom + pcir;

        if ((rd16(p + 4) != vendor) || (rd16(p + 6) != device))
            fail("PCIR IDs %04X:%04X do not match the card definition %04X:%04X", rd16(p + 4), rd16(p + 6),
                 vendor, device);
        if (rd16(p + 0x08) != 0)
            fail("PCIR VPD pointer must be 0");
        if ((rd16(p + 0x0a) != 0x18) || (p[0x0c] != 0))
            fail("PCIR length must be 18h with structure revision 0 (PCI 2.2)");
        if ((p[0x0d] != (class_code & 0xff)) || (p[0x0e] != ((class_code >> 8) & 0xff)) ||
            (p[0x0f] != (class_code >> 16)))
            fail("PCIR class code %02X%02X%02X does not match the card definition %06X", p[0x0f], p[0x0e],
                 p[0x0d], class_code);
        if (rd16(p + 0x10) * 512u != size)
            fail("PCIR image length says %u bytes; the image is %u", rd16(p + 0x10) * 512u, (unsigned) size);
        if (rd16(p + 0x12) != revision)
            fail("PCIR code revision %04X does not match the card definition %04X", rd16(p + 0x12), revision);
        if (p[0x14] != 0)
            fail("PCIR code type must be 0 (x86)");
        if (p[0x15] != 0x80)
            fail("PCIR indicator must be 80h: last image, reserved bits 0-6 clear");
        if (rd16(p + 0x16) != 0)
            fail("PCIR reserved word at offset 16h must be zero");
    }

    /* Optional $PnP expansion header */
    pnp = rd16(rom + 0x1a);
    if (pnp != 0) {
        check_pnp(rom, size, pnp);
        if (pnp + 32 <= size)
            add_region(pnp, pnp + ((rom[pnp + 5] * 16u < 32) ? 32 : rom[pnp + 5] * 16u), "the $PnP header");
    }

    /* Entry point at offset 3: a near or short jump into code. */
    if ((rom[3] == 0xe9) || (rom[3] == 0xeb)) {
        if (rom[3] == 0xe9)
            target = (6 + rd16(rom + 4)) & 0xffff;
        else
            target = (unsigned) (5 + (int8_t) rom[4]) & 0xffff;
        if (target >= size)
            fail("entry point jumps to 0x%04X, outside the image", target);
        for (a = 0; a < n_regions; a++) {
            if ((target >= regions[a].start) && (target < regions[a].end))
                fail("entry point jumps to 0x%04X, inside %s", target, regions[a].name);
        }
    } else
        fail("entry point at offset 3 is not a jump (E9h or EBh)");

    cfg_free(cfg);
    free(rom);
    return errors ? 1 : 0;
}
