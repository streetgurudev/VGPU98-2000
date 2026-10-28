/*
 * vglink: LE (linear executable) writer for Windows 98 virtual device drivers.
 * Layout: spec/le-format.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>

#include "vglink.h"

#define LE_PAGE        0x1000u
#define LE_HEADER_SIZE 0xc4u
#define LE_VXD         0x38000u /* module flags: library module, virtual device driver */

#define LE_RES_FLAGS   0x30u /* the version resource: moveable, pure, as Windows' own VxDs mark it */

#define FIX_TARGET32   0x10 /* target offset is 32 bits */
#define FIX_OBJECT16   0x40 /* object number is 16 bits */

static void
put_pname(buf_t *b, const char *s)
{
    put8(b, (uint32_t) strlen(s));
    buf_append(b, s, strlen(s));
}

static uint32_t
pages_of(const oseg_t *s)
{
    return (s->size + LE_PAGE - 1) / LE_PAGE;
}

static void
fixup_record(buf_t *b, const le_fixup_t *x, int32_t src_off)
{
    uint32_t flags = 0;

    if (x->target > 0xffff)
        flags |= FIX_TARGET32;
    if (x->object > 0xff)
        flags |= FIX_OBJECT16;
    put8(b, x->src);
    put8(b, flags);
    put16(b, (uint32_t) (uint16_t) (int16_t) src_off);
    if (flags & FIX_OBJECT16)
        put16(b, x->object);
    else
        put8(b, x->object);
    if (flags & FIX_TARGET32)
        put32(b, x->target);
    else
        put16(b, x->target);
}

void
le_write(const image_t *img, buf_t *out)
{
    size_t   le;
    uint32_t n_pages = 0;
    uint32_t preload = 0;
    uint32_t page;
    size_t   t_obj, t_map, t_res, t_ent, t_fpage, t_frec, t_imp, t_nonres;
    size_t   nonres_len;
    size_t   t_ver   = 0; /* the version resource's record, 0: none */
    size_t   ver_len = 0;
    size_t   fpage_entry;
    size_t   i;
    uint32_t last_size = 0;

    for (i = 0; i < img->n_segs; i++) {
        if (img->segs[i].size == 0)
            die("object %s is empty", img->segs[i].name);
        if (img->segs[i].preload && (i > 0) && !img->segs[i - 1].preload)
            die("object %s: preloaded objects must come first", img->segs[i].name);
        n_pages += pages_of(&img->segs[i]);
        if (img->segs[i].preload)
            preload += pages_of(&img->segs[i]);
    }

    write_stub(out);
    le = out->len;
    for (i = 0; i < LE_HEADER_SIZE; i++)
        put8(out, 0);

    /* Object table. */
    t_obj = out->len;
    page  = 1;
    for (i = 0; i < img->n_segs; i++) {
        const oseg_t *s = &img->segs[i];

        put32(out, s->size);
        put32(out, 0); /* relocation base: VxDs are always relocated */
        put32(out, s->le_flags);
        put32(out, page);
        put32(out, pages_of(s));
        buf_append(out, s->name, 4); /* the object's 4-character class */
        page += pages_of(s);
    }

    /* Object page map: page numbers, big-endian in three bytes, then flags 0
       (a page stored in the file). */
    t_map = out->len;
    for (page = 1; page <= n_pages; page++) {
        put8(out, page >> 16);
        put8(out, page >> 8);
        put8(out, page);
        put8(out, 0);
    }

    /* Resident names: the module name. */
    t_res = out->len;
    put_pname(out, img->name);
    put16(out, 0);
    put8(out, 0);

    /* Entry table: ordinal 1 is the device descriptor block. */
    t_ent = out->len;
    put8(out, 1);  /* one entry */
    put8(out, 3);  /* 32-bit entries */
    put16(out, img->ddb_obj);
    put8(out, 3);  /* exported, shared data */
    put32(out, img->ddb_off);
    put8(out, 0);

    /* Fixup page table and records. */
    t_fpage     = out->len;
    fpage_entry = out->len;
    for (i = 0; i <= n_pages; i++)
        put32(out, 0);
    t_frec = out->len;
    page   = 0;
    for (i = 0; i < img->n_segs; i++) {
        const oseg_t *s = &img->segs[i];
        uint32_t      p;

        for (p = 0; p < pages_of(s); p++, page++) {
            uint32_t lo = p * LE_PAGE;
            size_t   k;

            set32(out, fpage_entry + 4 * page, (uint32_t) (out->len - t_frec));
            for (k = 0; k < s->n_fixups; k++) {
                const le_fixup_t *x = &s->fixups[k];

                /* A location may start in this page or run into it from the previous one. */
                if ((x->offset >= lo) && (x->offset < lo + LE_PAGE))
                    fixup_record(out, x, (int32_t) (x->offset - lo));
                else if ((x->offset < lo) && (x->offset + 4 > lo))
                    fixup_record(out, x, (int32_t) x->offset - (int32_t) lo);
            }
        }
    }
    set32(out, fpage_entry + 4 * n_pages, (uint32_t) (out->len - t_frec));
    t_imp = out->len; /* empty import module and procedure tables */

    /* Page data. */
    pad_to(out, 0x200);
    {
        size_t datapage = out->len;

        for (i = 0; i < img->n_segs; i++) {
            const oseg_t *s = &img->segs[i];

            buf_append(out, s->data, s->size);
            if (i + 1 < img->n_segs) {
                while ((out->len - datapage) % LE_PAGE)
                    put8(out, 0); /* every page but the last is whole */
            } else
                last_size = s->size % LE_PAGE ? s->size % LE_PAGE : LE_PAGE;
        }
        set32(out, le + 0x80, (uint32_t) datapage);
    }

    /* Non-resident names: the description, then the DDB export. */
    t_nonres = out->len;
    put_pname(out, img->description);
    put16(out, 0);
    put_pname(out, img->ddb_name);
    put16(out, 1);
    put8(out, 0);
    nonres_len = out->len - t_nonres;

    /* The version resource ([version]) last, as in Windows' own VxDs: a
       16-bit resource record (type 16, name 1, flags, the data's size) and
       its VS_VERSIONINFO, unaligned; the header's +B8h and +BCh place it
       (spec/le-format.md). */
    for (i = 0; i < img->n_res; i++) {
        const resource_t *r = &img->res[i];

        if (r->type_name || (r->type_id != 16))
            continue;
        t_ver = out->len;
        put8(out, 0xff);
        put16(out, 16); /* RT_VERSION */
        put8(out, 0xff);
        put16(out, 1);
        put16(out, LE_RES_FLAGS);
        put32(out, (uint32_t) r->len);
        buf_append(out, r->data, r->len);
        ver_len = out->len - t_ver;
    }

    /* Header. */
    out->data[le]     = 'L';
    out->data[le + 1] = 'E';
    set16(out, le + 0x08, 2); /* 80386 */
    set16(out, le + 0x0a, 4); /* Windows 386 (VxD) */
    set32(out, le + 0x10, LE_VXD);
    set32(out, le + 0x14, n_pages);
    set32(out, le + 0x28, LE_PAGE);
    set32(out, le + 0x2c, last_size);
    set32(out, le + 0x30, (uint32_t) (t_imp - t_fpage));
    set32(out, le + 0x38, (uint32_t) (t_fpage - t_obj));
    set32(out, le + 0x40, (uint32_t) (t_obj - le));
    set32(out, le + 0x44, (uint32_t) img->n_segs);
    set32(out, le + 0x48, (uint32_t) (t_map - le));
    set32(out, le + 0x58, (uint32_t) (t_res - le));
    set32(out, le + 0x5c, (uint32_t) (t_ent - le));
    set32(out, le + 0x68, (uint32_t) (t_fpage - le));
    set32(out, le + 0x6c, (uint32_t) (t_frec - le));
    set32(out, le + 0x70, (uint32_t) (t_imp - le));
    set32(out, le + 0x78, (uint32_t) (t_imp - le));
    set32(out, le + 0x84, preload);
    set32(out, le + 0x88, (uint32_t) t_nonres);
    set32(out, le + 0x8c, (uint32_t) nonres_len);
    set32(out, le + 0xb8, (uint32_t) t_ver);
    set32(out, le + 0xbc, (uint32_t) ver_len);
    set16(out, le + 0xc0, img->device_id);
    set16(out, le + 0xc2, img->ddk_version);
}
