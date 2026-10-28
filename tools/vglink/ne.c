/*
 * vglink: NE (16-bit Windows) module writer. Layout: spec/ne-format.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <string.h>

#include "vglink.h"

#define NE_ALIGN_SHIFT 4 /* segments and resources start on 16-byte boundaries */

/* Segment table flags. */
#define NESEG_DATA     0x0001
#define NESEG_PURE     0x0020 /* code: shareable; data: shared */
#define NESEG_PRELOAD  0x0040
#define NESEG_RELOC    0x0100
#define NESEG_DPL3     0x0c00

/* Module flags. */
#define NE_SINGLEDATA  0x0001
#define NE_WINAPI      0x0300
#define NE_LIBRARY     0x8000

/* Entry flags. */
#define ENTRY_EXPORTED 0x01
#define ENTRY_SHARED   0x02 /* the entry loads DS with the automatic data segment */

/* Resource flags. */
#define RES_MOVEABLE   0x0010
#define RES_PURE       0x0020
#define RES_PRELOAD    0x0040
#define RES_PRIORITY   0x0c00 /* discard priority bits, as Windows' resource compiler sets */

static void
put_pname(buf_t *b, const char *s)
{
    put8(b, (uint32_t) strlen(s));
    buf_append(b, s, strlen(s));
}

static void
names_table(buf_t *b, const char *first, const image_t *img, int resident)
{
    size_t i;

    put_pname(b, first);
    put16(b, 0);
    for (i = 0; i < img->n_exports; i++) {
        if (img->exports[i].resident != resident)
            continue;
        put_pname(b, img->exports[i].name);
        put16(b, img->exports[i].ordinal);
    }
    put8(b, 0);
}

/* Entry table: bundles of consecutive ordinals in one segment, with empty
   bundles over the gaps. Exports are sorted by ordinal. */
static void
entry_table(buf_t *b, const image_t *img)
{
    uint32_t next = 1;
    size_t   i    = 0;

    while (i < img->n_exports) {
        uint32_t ord = img->exports[i].ordinal;
        size_t   j;
        size_t   n;

        while (next < ord) { /* unused ordinals */
            uint32_t gap = ord - next;

            if (gap > 255)
                gap = 255;
            put8(b, gap);
            put8(b, 0);
            next += gap;
        }
        for (n = 1; (i + n < img->n_exports) && (n < 255) &&
                    (img->exports[i + n].ordinal == ord + n) && (img->exports[i + n].seg == img->exports[i].seg);
             n++)
            ;
        put8(b, (uint32_t) n);
        put8(b, img->exports[i].seg); /* fixed segment number */
        for (j = 0; j < n; j++) {
            const export_t *e = &img->exports[i + j];

            put8(b, ENTRY_EXPORTED | ((e->nodata || !img->autodata) ? 0 : ENTRY_SHARED));
            put16(b, e->offset);
        }
        i += n;
        next = ord + (uint32_t) n;
    }
    put8(b, 0);
}

typedef struct restype_t {
    const resource_t **items;
    size_t             n;
} restype_t;

static int
same_type(const resource_t *a, const resource_t *b)
{
    if ((a->type_name == NULL) != (b->type_name == NULL))
        return 0;
    return a->type_name ? strcmp(a->type_name, b->type_name) == 0 : a->type_id == b->type_id;
}

/* Resource table, with placeholders for the data offsets; returns the
   positions of those placeholders (in resource order). */
static void
resource_table(buf_t *b, const image_t *img, size_t *offset_at)
{
    restype_t *types   = xcalloc(img->n_res + 1, sizeof(*types));
    size_t     n_types = 0;
    size_t    *type_name_at;
    size_t    *id_name_at;
    size_t     start = b->len;
    size_t     i, k;

    /* Group by type, in order of first appearance. */
    for (i = 0; i < img->n_res; i++) {
        for (k = 0; k < n_types; k++) {
            if (same_type(types[k].items[0], &img->res[i]))
                break;
        }
        if (k == n_types)
            n_types++;
        types[k].items = xrealloc(types[k].items, (types[k].n + 1) * sizeof(*types[k].items));
        types[k].items[types[k].n++] = &img->res[i];
    }

    type_name_at = xcalloc(n_types + 1, sizeof(size_t));
    id_name_at   = xcalloc(img->n_res + 1, sizeof(size_t));
    put16(b, NE_ALIGN_SHIFT);
    for (k = 0; k < n_types; k++) {
        const resource_t *t = types[k].items[0];

        type_name_at[k] = b->len;
        put16(b, t->type_name ? 0 : (0x8000u | t->type_id)); /* name offset patched below */
        put16(b, (uint32_t) types[k].n);
        put32(b, 0);
        for (i = 0; i < types[k].n; i++) {
            const resource_t *r = types[k].items[i];

            offset_at[r - img->res] = b->len;
            put16(b, 0);                              /* offset, in alignment units */
            put16(b, (uint32_t) ((r->len + 15) >> NE_ALIGN_SHIFT));
            put16(b, RES_PRIORITY | RES_PURE | RES_MOVEABLE | (r->preload ? RES_PRELOAD : 0));
            id_name_at[r - img->res] = b->len;
            put16(b, r->id_name ? 0 : (0x8000u | r->id));
            put16(b, 0);
            put16(b, 0);
        }
    }
    put16(b, 0); /* end of types */

    /* Names, as offsets from the start of the table. */
    for (k = 0; k < n_types; k++) {
        if (types[k].items[0]->type_name) {
            set16(b, type_name_at[k], (uint32_t) (b->len - start));
            put_pname(b, types[k].items[0]->type_name);
        }
    }
    for (i = 0; i < img->n_res; i++) {
        if (img->res[i].id_name) {
            set16(b, id_name_at[i], (uint32_t) (b->len - start));
            put_pname(b, img->res[i].id_name);
        }
    }
    put8(b, 0); /* end of names */

    for (k = 0; k < n_types; k++)
        free(types[k].items);
    free(types);
    free(type_name_at);
    free(id_name_at);
}

void
ne_write(const image_t *img, buf_t *out)
{
    size_t  ne;
    size_t *seg_entry   = xcalloc(img->n_segs + 1, sizeof(size_t));
    size_t *res_offset  = xcalloc(img->n_res + 1, sizeof(size_t));
    size_t *mod_offsets = xcalloc(img->n_modules + 1, sizeof(size_t));
    size_t  t_seg, t_res, t_resident, t_mod, t_imp, t_ent, t_nonres, t_nonres_end;
    size_t  i;
    buf_t   imp;

    write_stub(out);
    ne = out->len;
    for (i = 0; i < 0x40; i++)
        put8(out, 0);

    /* Segment table (offsets and flags patched once the data is placed). */
    t_seg = out->len;
    for (i = 0; i < img->n_segs; i++) {
        seg_entry[i] = out->len;
        put16(out, 0);
        put16(out, img->segs[i].size & 0xffff); /* 0 means 64 KB */
        put16(out, 0);
        put16(out, img->segs[i].size & 0xffff);
    }

    /* Resource table: present only when there are resources. */
    t_res = out->len;
    if (img->n_res)
        resource_table(out, img, res_offset);

    t_resident = out->len;
    names_table(out, img->name, img, 1);

    /* Imported names: an empty name first, then the module names. */
    memset(&imp, 0, sizeof(imp));
    put8(&imp, 0);
    for (i = 0; i < img->n_modules; i++) {
        mod_offsets[i] = imp.len;
        put_pname(&imp, img->modules[i]);
    }

    t_mod = out->len;
    for (i = 0; i < img->n_modules; i++)
        put16(out, (uint32_t) mod_offsets[i]);
    t_imp = out->len;
    buf_append(out, imp.data, imp.len);
    buf_free(&imp);

    t_ent = out->len;
    entry_table(out, img);

    t_nonres = out->len;
    names_table(out, img->description, img, 0);
    t_nonres_end = out->len;

    /* Segment data, each followed by its relocation records. */
    for (i = 0; i < img->n_segs; i++) {
        const oseg_t *s     = &img->segs[i];
        uint32_t      flags = NESEG_DPL3 | NESEG_PURE | (s->preload ? NESEG_PRELOAD : 0) |
                         (s->is_data ? NESEG_DATA : 0);
        size_t        k;

        if (s->size == 0)
            die("segment %s is empty", s->name); /* a length of 0 would mean 64 KB */
        pad_to(out, 1u << NE_ALIGN_SHIFT);
        set16(out, seg_entry[i], (uint32_t) (out->len >> NE_ALIGN_SHIFT));
        buf_append(out, s->data, s->size);
        if (s->n_relocs) {
            flags |= NESEG_RELOC;
            put16(out, (uint32_t) s->n_relocs);
            for (k = 0; k < s->n_relocs; k++) {
                const ne_reloc_t *r = &s->relocs[k];

                put8(out, r->src);
                put8(out, r->flags);
                put16(out, r->offset);
                if (r->flags == NE_REL_INTERNAL) {
                    put8(out, r->a); /* fixed segment number */
                    put8(out, 0);
                    put16(out, r->b);
                } else {
                    put16(out, r->a);
                    put16(out, r->b);
                }
            }
        }
        set16(out, seg_entry[i] + 4, flags);
    }

    /* Resource data. */
    for (i = 0; i < img->n_res; i++) {
        pad_to(out, 1u << NE_ALIGN_SHIFT);
        set16(out, res_offset[i], (uint32_t) (out->len >> NE_ALIGN_SHIFT));
        buf_append(out, img->res[i].data, img->res[i].len);
    }
    pad_to(out, 1u << NE_ALIGN_SHIFT);
    if ((out->len >> NE_ALIGN_SHIFT) > 0xffff)
        die("the module is larger than 1 MB");

    /* Header. */
    out->data[ne]     = 'N';
    out->data[ne + 1] = 'E';
    out->data[ne + 2] = 5;  /* linker version */
    out->data[ne + 3] = 0;  /* linker revision */
    set16(out, ne + 0x04, (uint32_t) (t_ent - ne));
    set16(out, ne + 0x06, (uint32_t) (t_nonres - t_ent));
    set32(out, ne + 0x08, 0); /* CRC: not used by Windows */
    set16(out, ne + 0x0c, NE_LIBRARY | NE_WINAPI | (img->autodata ? NE_SINGLEDATA : 0));
    set16(out, ne + 0x0e, img->autodata);
    set16(out, ne + 0x10, img->heap);
    set16(out, ne + 0x12, 0); /* stack: a library has none */
    set32(out, ne + 0x14, (img->entry_seg << 16) | img->entry_off);
    set32(out, ne + 0x18, 0);
    set16(out, ne + 0x1c, (uint32_t) img->n_segs);
    set16(out, ne + 0x1e, (uint32_t) img->n_modules);
    set16(out, ne + 0x20, (uint32_t) (t_nonres_end - t_nonres));
    set16(out, ne + 0x22, (uint32_t) (t_seg - ne));
    set16(out, ne + 0x24, (uint32_t) (t_res - ne));
    set16(out, ne + 0x26, (uint32_t) (t_resident - ne));
    set16(out, ne + 0x28, (uint32_t) (t_mod - ne));
    set16(out, ne + 0x2a, (uint32_t) (t_imp - ne));
    set32(out, ne + 0x2c, (uint32_t) t_nonres);
    set16(out, ne + 0x30, 0);              /* movable entries */
    set16(out, ne + 0x32, NE_ALIGN_SHIFT);
    set16(out, ne + 0x34, 0);              /* resource segments (OS/2) */
    out->data[ne + 0x36] = 2;              /* Windows */
    out->data[ne + 0x37] = 0;              /* no fast-load area */
    set16(out, ne + 0x38, 0);
    set16(out, ne + 0x3a, 0);
    set16(out, ne + 0x3c, 0);
    set16(out, ne + 0x3e, img->expver);

    free(seg_entry);
    free(res_offset);
    free(mod_offsets);
}
