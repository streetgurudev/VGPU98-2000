/*
 * Tests for vglink's NE and LE output: loads the linked test modules
 * (tools/tests/vglink) the way Windows does, with small loaders written here
 * independently of vglink and drvlint, and checks the values every relocation
 * and fixup produces against hand-worked expectations in the .asm files.
 *
 * Usage: test_vglink NE_FILE LE_FILE RESOURCE_DIR
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "util.h"

static uint8_t *f;
static size_t   fsize;

static uint32_t
u8(size_t at)
{
    return (at < fsize) ? f[at] : 0;
}

static uint32_t
u16(size_t at)
{
    return u8(at) | (u8(at + 1) << 8);
}

static uint32_t
u32(size_t at)
{
    return u16(at) | (u16(at + 2) << 16);
}

static uint32_t
get16(const uint8_t *p)
{
    return p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t
get32(const uint8_t *p)
{
    return get16(p) | (get16(p + 2) << 16);
}

static void
put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

/* --- NE ------------------------------------------------------------------- */

/* The test loader's selectors and import addresses. */
#define SEL(n)          ((uint32_t) (n) * 0x100 + 7)
#define IMP_SEL(module) ((uint32_t) (module) * 0x1000)

typedef struct ne_t {
    size_t   ne;
    uint32_t n_segs;
    uint8_t *seg[8];
    uint32_t seg_len[8];
    uint32_t seg_flags[8];
} ne_t;

/* Pieces of a names table, looked up by ordinal. */
static int
find_name(size_t at, const char *name, uint32_t ordinal)
{
    while (u8(at)) {
        uint32_t n = u8(at);

        if ((strlen(name) == n) && (memcmp(f + at + 1, name, n) == 0))
            return u16(at + 1 + n) == ordinal;
        at += 3 + n;
    }
    return 0;
}

/* Entry for an ordinal: segment, offset, flags; returns 0 if there is none. */
static int
ne_entry(const ne_t *m, uint32_t ordinal, uint32_t *seg, uint32_t *off, uint32_t *flags)
{
    size_t   at  = m->ne + u16(m->ne + 0x04);
    uint32_t ord = 1;

    while (u8(at)) {
        uint32_t count = u8(at);
        uint32_t type  = u8(at + 1);
        uint32_t k;

        at += 2;
        for (k = 0; k < count; k++, ord++) {
            if (type == 0)
                continue;
            if (ord == ordinal) {
                *flags = u8(at);
                *seg   = type;
                *off   = u16(at + 1);
                return 1;
            }
            at += 3;
        }
    }
    return 0;
}

static void
ne_load(ne_t *m)
{
    uint32_t shift = u16(m->ne + 0x32);
    uint32_t i;

    m->n_segs = u16(m->ne + 0x1c);
    CHECK(m->n_segs == 3);
    if (m->n_segs > 7)
        m->n_segs = 7;
    for (i = 1; i <= m->n_segs; i++) {
        size_t   st  = m->ne + u16(m->ne + 0x22) + 8 * (i - 1);
        size_t   off = (size_t) u16(st) << shift;
        uint32_t len = u16(st + 2);

        m->seg_len[i]   = len;
        m->seg_flags[i] = u16(st + 4);
        m->seg[i]       = xcalloc(1, u16(st + 6) + 1u);
        memcpy(m->seg[i], f + off, len);
    }
    /* Relocations, as the Windows loader applies them. */
    for (i = 1; i <= m->n_segs; i++) {
        size_t   st  = m->ne + u16(m->ne + 0x22) + 8 * (i - 1);
        size_t   r   = ((size_t) u16(st) << shift) + m->seg_len[i];
        uint32_t n, k;

        if (!(m->seg_flags[i] & 0x100))
            continue;
        n = u16(r);
        for (k = 0; k < n; k++) {
            size_t   p     = r + 2 + 8 * k;
            uint32_t src   = u8(p);
            uint32_t flags = u8(p + 1);
            uint32_t loc   = u16(p + 2);
            uint32_t sel, off;

            if ((flags & 3) == 0) {
                sel = SEL(u8(p + 4));
                off = u16(p + 6);
            } else {
                CHECK((flags & 3) == 1); /* imports by ordinal only */
                sel = IMP_SEL(u16(p + 4));
                off = u16(p + 6);
            }
            /* Follow the chain unless additive. */
            for (;;) {
                uint32_t next = get16(m->seg[i] + loc);

                CHECK(loc + 2 <= m->seg_len[i]);
                switch (src) {
                    case 2:
                        put16(m->seg[i] + loc, sel);
                        break;
                    case 3:
                        put16(m->seg[i] + loc, off);
                        put16(m->seg[i] + loc + 2, sel);
                        break;
                    case 5:
                        put16(m->seg[i] + loc, off);
                        break;
                    default:
                        CHECK(!"unexpected relocation source type");
                }
                if ((flags & 4) || (next == 0xffff))
                    break;
                loc = next;
            }
        }
    }
}

static void
test_ne(const char *path, const char *res_dir)
{
    ne_t     m;
    uint32_t seg, off, flags;
    uint8_t *t, *d;
    size_t   rsrc;
    char     p1[1024], p2[1024];
    size_t   l1, l2;
    uint8_t *r1, *r2;
    int      found_version = 0, found_1 = 0, found_fonts = 0;

    memset(&m, 0, sizeof(m));
    f    = read_file(path, &fsize);
    m.ne = u32(0x3c);
    CHECK((u16(0) == 0x5a4d) && (u16(0x18) == 0x40));
    CHECK((u8(m.ne) == 'N') && (u8(m.ne + 1) == 'E'));
    CHECK(u16(m.ne + 0x0c) == 0x8301);   /* library, Windows API, single data */
    CHECK(u16(m.ne + 0x0e) == 2);        /* _DATA */
    CHECK(u16(m.ne + 0x10) == 0x40);     /* heap */
    CHECK(u32(m.ne + 0x14) == 0x10000);  /* entry 1:0000 */
    CHECK(u16(m.ne + 0x1e) == 2);        /* DIBENG, KERNEL */
    CHECK(u8(m.ne + 0x36) == 2);         /* Windows */
    CHECK(u16(m.ne + 0x3e) == 0x400);
    ne_load(&m);
    CHECK(m.seg_flags[1] == 0x0d60); /* code: preload, relocations */
    CHECK(m.seg_flags[2] == 0x0d61); /* data: preload, relocations */
    CHECK(m.seg_flags[3] == 0x0c20); /* code: load on call, no relocations */
    CHECK((m.seg_len[1] == 0x66) && (m.seg_len[2] == 0x20) && (m.seg_len[3] == 5));

    /* Code: see ne_a.asm and ne_b.asm. */
    t = m.seg[1];
    CHECK(get16(t + 0x01) == SEL(2));
    CHECK((get16(t + 0x06) == 1) && (get16(t + 0x08) == IMP_SEL(1)));
    CHECK(get16(t + 0x0b) == IMP_SEL(2));
    CHECK(get16(t + 0x0e) == 18);
    CHECK((get16(t + 0x11) == 0x60) && (get16(t + 0x13) == SEL(1)));
    CHECK((t[0x60] == 0x9a) && (get16(t + 0x61) == 0x40) && (get16(t + 0x63) == SEL(1)));
    CHECK((t[0x5a] == 0) && (t[0x5f] == 0)); /* alignment padding before object B */

    /* Data. */
    d = m.seg[2];
    CHECK((get16(d + 0x00) == 0x40) && (get16(d + 0x02) == SEL(1)));
    CHECK((get16(d + 0x04) == 0x60) && (get16(d + 0x06) == SEL(1)));
    CHECK((get16(d + 0x08) == 1) && (get16(d + 0x0a) == IMP_SEL(1)));
    CHECK(get32(d + 0x0c) == 0x12345678);
    CHECK((get16(d + 0x10) == 4) && (get16(d + 0x12) == SEL(3)));
    CHECK((get32(d + 0x14) == 0) && (get32(d + 0x1c) == 0));

    /* Entries and names. */
    CHECK(ne_entry(&m, 1, &seg, &off, &flags) && (seg == 1) && (off == 0x00) && (flags == 3));
    CHECK(ne_entry(&m, 2, &seg, &off, &flags) && (seg == 1) && (off == 0x40) && (flags == 1));
    CHECK(ne_entry(&m, 5, &seg, &off, &flags) && (seg == 1) && (off == 0x44) && (flags == 3));
    CHECK(ne_entry(&m, 400, &seg, &off, &flags) && (seg == 1) && (off == 0x48) && (flags == 3));
    CHECK(ne_entry(&m, 700, &seg, &off, &flags) && (seg == 1) && (off == 0x4c) && (flags == 3));
    CHECK(!ne_entry(&m, 3, &seg, &off, &flags) && !ne_entry(&m, 399, &seg, &off, &flags) &&
          !ne_entry(&m, 701, &seg, &off, &flags));
    CHECK(find_name(m.ne + u16(m.ne + 0x26), "DISPLAY", 0));
    CHECK(find_name(m.ne + u16(m.ne + 0x26), "EXP700", 700));
    CHECK(find_name(u32(m.ne + 0x2c), "DISPLAY : 100, 96, 96 : vglink test", 0));
    CHECK(find_name(u32(m.ne + 0x2c), "ENTRY", 1) && find_name(u32(m.ne + 0x2c), "EXP2", 2) &&
          find_name(u32(m.ne + 0x2c), "EXP5", 5) && find_name(u32(m.ne + 0x2c), "EXP400", 400));
    CHECK(!find_name(u32(m.ne + 0x2c), "EXP700", 700));

    /* Resources: version information, OEMBIN 1 and OEMBIN FONTS with the files' bytes. */
    snprintf(p1, sizeof(p1), "%s/res1.bin", res_dir);
    snprintf(p2, sizeof(p2), "%s/res2.bin", res_dir);
    r1   = read_file(p1, &l1);
    r2   = read_file(p2, &l2);
    rsrc = m.ne + u16(m.ne + 0x24);
    {
        uint32_t rshift = u16(rsrc);
        size_t   at     = rsrc + 2;

        while (u16(at)) {
            uint32_t type  = u16(at);
            uint32_t count = u16(at + 2);
            uint32_t k;

            at += 8;
            for (k = 0; k < count; k++, at += 12) {
                size_t   off_data = (size_t) u16(at) << rshift;
                uint32_t id       = u16(at + 6);

                if (type == 0x8010) {
                    found_version = (id == 0x8001) && (memcmp(f + off_data + 4, "VS_VERSION_INFO", 16) == 0) &&
                                    (u32(off_data + 20) == 0xfeef04bd) && (u32(off_data + 28) == 0x0004000a) &&
                                    (u32(off_data + 32) == 0x00000001) && (u32(off_data + 56) == 3) &&
                                    (u32(off_data + 60) == 4);
                } else if (!(type & 0x8000) && (u8(rsrc + type) == 6) && !memcmp(f + rsrc + type + 1, "OEMBIN", 6)) {
                    CHECK((u16(at + 4) & 0x0070) == 0x0070); /* moveable, pure, preload */
                    if (id == 0x8001)
                        found_1 = (u16(at + 2) << rshift >= l1) && !memcmp(f + off_data, r1, l1);
                    else if (!(id & 0x8000) && (u8(rsrc + id) == 5) && !memcmp(f + rsrc + id + 1, "FONTS", 5))
                        found_fonts = (u16(at + 2) << rshift >= l2) && !memcmp(f + off_data, r2, l2);
                }
            }
        }
    }
    CHECK(found_version);
    CHECK(found_1);
    CHECK(found_fonts);
    free(r1);
    free(r2);
    free(f);
}

/* --- LE ------------------------------------------------------------------- */

#define OBJ_BASE(n) ((uint32_t) (n) * 0x100000u) /* where the test loader puts object n */

static void
test_le(const char *path)
{
    size_t   le;
    uint32_t n_obj, pagesize, datapage;
    uint8_t *obj[4] = { 0 };
    uint32_t vsize[4] = { 0 };
    uint32_t i;
    size_t   fpt, frt;

    f  = read_file(path, &fsize);
    le = u32(0x3c);
    CHECK((u8(le) == 'L') && (u8(le + 1) == 'E'));
    CHECK((u16(le + 0x08) == 2) && (u16(le + 0x0a) == 4) && (u32(le + 0x10) == 0x38000));
    CHECK((u16(le + 0xc0) == 0x1234) && (u16(le + 0xc2) == 0x40a));
    n_obj    = u32(le + 0x44);
    pagesize = u32(le + 0x28);
    datapage = u32(le + 0x80);
    CHECK((n_obj == 3) && (pagesize == 4096));
    if ((n_obj != 3) || (pagesize != 4096)) {
        free(f);
        return;
    }
    fpt = le + u32(le + 0x68);
    frt = le + u32(le + 0x6c);

    /* Load the objects: each page from the page map. */
    for (i = 1; i <= n_obj; i++) {
        size_t   ot    = le + u32(le + 0x40) + 24 * (i - 1);
        uint32_t page  = u32(ot + 12);
        uint32_t pages = u32(ot + 16);
        uint32_t p;

        vsize[i] = u32(ot);
        obj[i]   = xcalloc(1, (size_t) pages * pagesize + 8);
        for (p = 0; p < pages; p++) {
            size_t   map = le + u32(le + 0x48) + 4 * (page - 1 + p);
            uint32_t pn  = (u8(map) << 16) | (u8(map + 1) << 8) | u8(map + 2);
            size_t   src = datapage + (size_t) (pn - 1) * pagesize;
            size_t   n   = (src + pagesize <= fsize) ? pagesize : fsize - src;

            memcpy(obj[i] + (size_t) p * pagesize, f + src, n);
        }
    }
    CHECK(vsize[1] == 0x35);         /* LCOD: _LTEXT 20h + _LDATA 15h */
    CHECK(vsize[2] == 0x12173);      /* PCOD */
    CHECK(vsize[3] == 0x0b);         /* ICOD */
    CHECK((u32(le + u32(le + 0x40) + 8) == 0x2045) && (u32(le + u32(le + 0x40) + 24 + 8) == 0x2005) &&
          (u32(le + u32(le + 0x40) + 48 + 8) == 0x2015));

    /* Apply the fixups page by page, as the VxD loader does. */
    for (i = 1; i <= n_obj; i++) {
        size_t   ot    = le + u32(le + 0x40) + 24 * (i - 1);
        uint32_t page  = u32(ot + 12);
        uint32_t pages = u32(ot + 16);
        uint32_t p;

        for (p = 0; p < pages; p++) {
            size_t at  = frt + u32(fpt + 4 * (page - 1 + p));
            size_t end = frt + u32(fpt + 4 * (page + p));

            while (at < end) {
                uint32_t src   = u8(at);
                uint32_t flags = u8(at + 1);
                int32_t  soff  = (int16_t) u16(at + 2);
                uint32_t tobj, toff, value;
                int32_t  loc;
                uint8_t  bytes[4];
                int      b;

                at += 4;
                tobj = (flags & 0x40) ? u16(at) : u8(at);
                at += (flags & 0x40) ? 2 : 1;
                toff = (flags & 0x10) ? u32(at) : u16(at);
                at += (flags & 0x10) ? 4 : 2;
                CHECK(((src == 7) || (src == 8)) && ((flags & 3) == 0) && (tobj >= 1) && (tobj <= n_obj));
                loc   = (int32_t) (p * pagesize) + soff;
                value = OBJ_BASE(tobj) + toff;
                if (src == 8)
                    value -= OBJ_BASE(i) + (uint32_t) loc + 4;
                /* A location across a page boundary is written from both pages: each
                   page writes only its own bytes. */
                put32(bytes, value);
                for (b = 0; b < 4; b++) {
                    int32_t byte_at = loc + b;

                    if ((byte_at >= (int32_t) (p * pagesize)) && (byte_at < (int32_t) ((p + 1) * pagesize)))
                        obj[i][byte_at] = bytes[b];
                }
            }
        }
    }

    /* LCOD: see le.asm. */
    CHECK(get32(obj[1] + 0x01) == OBJ_BASE(1) + 0x30);
    CHECK(get32(obj[1] + 0x06) == OBJ_BASE(2) + 0x12172 - (OBJ_BASE(1) + 0x0a));
    CHECK(get32(obj[1] + 0x0b) == OBJ_BASE(3) - (OBJ_BASE(1) + 0x0f));
    CHECK(get32(obj[1] + 0x10) == 0x20);
    CHECK(get32(obj[1] + 0x20) == 0);
    CHECK(get32(obj[1] + 0x24) == OBJ_BASE(1));
    CHECK(get32(obj[1] + 0x28) == OBJ_BASE(1) + 0x28);
    CHECK(get32(obj[1] + 0x2c) == OBJ_BASE(2) + 0x12172);
    /* PCOD: the location across the page boundary. */
    CHECK(get32(obj[2] + 0x0ffe) == OBJ_BASE(1));
    CHECK(obj[2][0x12172] == 0xc3);
    /* ICOD. */
    CHECK(get32(obj[3] + 0x01) == OBJ_BASE(1) - (OBJ_BASE(3) + 0x05));
    CHECK(get32(obj[3] + 0x06) == OBJ_BASE(2) + 0x12172);

    /* The DDB: entry 1 at LCOD+20h, named TEST_DDB. */
    {
        size_t ent = le + u32(le + 0x5c);

        CHECK((u8(ent) == 1) && (u8(ent + 1) == 3) && (u16(ent + 2) == 1) && (u32(ent + 5) == 0x20));
        CHECK(find_name(u32(le + 0x88), "TEST_DDB", 1));
        CHECK(find_name(le + u32(le + 0x58), "VGTEST", 0));
    }
    /* The version resource: the file's last bytes, placed by the header's
       +B8h and +BCh, a 16-bit resource record (type 16, name 1, flags 30h,
       the data's size) and its VS_VERSIONINFO, a VxD's (type 5) with the
       device ID as its subtype. */
    {
        const uint32_t at = u32(le + 0xb8), len = u32(le + 0xbc);
        const int      placed = (at > 0) && (len > 12 + 20 + 52) && ((size_t) at + len == fsize);

        CHECK(placed);
        if (placed) {
            const size_t fixed = at + 12 + 20; /* past the root's lengths and "VS_VERSION_INFO" */

            CHECK((u8(at) == 0xff) && (u16(at + 1) == 16) && (u8(at + 3) == 0xff) && (u16(at + 4) == 1) &&
                  (u16(at + 6) == 0x30) && (u32(at + 8) == len - 12));
            CHECK((u16(at + 12) == len - 12) && (u16(at + 14) == 52) &&
                  (memcmp(f + at + 16, "VS_VERSION_INFO", 16) == 0));
            CHECK((u32(fixed) == 0xfeef04bd) && (u32(fixed + 8) == 0x0004000a) && (u32(fixed + 12) == 0x00000001));
            CHECK((u32(fixed + 32) == 0x00010001) && (u32(fixed + 36) == 5) && (u32(fixed + 40) == 0x1234));
        }
    }
    for (i = 1; i <= n_obj; i++)
        free(obj[i]);
    free(f);
}

int
main(int argc, char **argv)
{
    set_progname("test_vglink");
    if (argc != 4)
        die("usage: test_vglink NE_FILE LE_FILE RESOURCE_DIR");
    test_ne(argv[1], argv[3]);
    test_le(argv[2]);
    CHECK_DONE();
}
