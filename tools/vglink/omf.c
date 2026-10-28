/*
 * vglink: OMF object reader. See omf.h.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "omf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

enum {
    THEADR   = 0x80,
    COMENT   = 0x88,
    MODEND   = 0x8a,
    MODEND32 = 0x8b,
    EXTDEF   = 0x8c,
    PUBDEF   = 0x90,
    PUBDEF32 = 0x91,
    LINNUM   = 0x94,
    LINNUM32 = 0x95,
    LNAMES   = 0x96,
    SEGDEF   = 0x98,
    SEGDEF32 = 0x99,
    GRPDEF   = 0x9a,
    FIXUPP   = 0x9c,
    FIXUPP32 = 0x9d,
    LEDATA   = 0xa0,
    LEDATA32 = 0xa1
};

typedef struct reader_t {
    omf_t         *omf;
    const uint8_t *p;   /* current record body */
    size_t         len; /* body length without the checksum */
    size_t         pos;
    int            type;
    int            record; /* 1-based record number */
} reader_t;

VGPU98_NORETURN static void bad(const reader_t *r, const char *fmt, ...) VGPU98_PRINTF(2, 3);

static void
bad(const reader_t *r, const char *fmt, ...)
{
    char    msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    die("%s: record %d (type %02Xh): %s", r->omf->path, r->record, r->type, msg);
}

static int
more(const reader_t *r)
{
    return r->pos < r->len;
}

static uint32_t
get8(reader_t *r)
{
    if (r->pos >= r->len)
        bad(r, "record too short");
    return r->p[r->pos++];
}

static uint32_t
get16(reader_t *r)
{
    uint32_t v = get8(r);

    return v | (get8(r) << 8);
}

static uint32_t
get32(reader_t *r)
{
    uint32_t v = get16(r);

    return v | (get16(r) << 16);
}

/* 16-bit or 32-bit field, by record type. */
static uint32_t
get_word(reader_t *r)
{
    return (r->type & 1) ? get32(r) : get16(r);
}

/* OMF index: one byte, or two with the high bit of the first set. */
static uint32_t
get_index(reader_t *r)
{
    uint32_t v = get8(r);

    if (v & 0x80)
        v = ((v & 0x7f) << 8) | get8(r);
    return v;
}

static char *
get_name(reader_t *r)
{
    uint32_t n = get8(r);
    char    *s;

    if (r->pos + n > r->len)
        bad(r, "name runs past the record");
    s = xmalloc(n + 1);
    memcpy(s, r->p + r->pos, n);
    s[n] = 0;
    r->pos += n;
    return s;
}

static const char *
lname(reader_t *r, uint32_t index)
{
    if ((index == 0) || (index > r->omf->n_names))
        bad(r, "name index %u out of range", index);
    return r->omf->names[index - 1];
}

static void
read_lnames(reader_t *r)
{
    omf_t *o = r->omf;

    while (more(r)) {
        o->names              = xrealloc(o->names, (o->n_names + 1) * sizeof(*o->names));
        o->names[o->n_names++] = get_name(r);
    }
}

static void
read_segdef(reader_t *r)
{
    static const uint32_t aligns[8] = { 0, 1, 2, 16, 256, 4, 4096, 0 };
    omf_t                *o         = r->omf;
    omf_seg_t            *s;
    uint32_t              acbp = get8(r);
    uint32_t              a    = acbp >> 5;
    uint32_t              length;

    if ((a == 0) || (aligns[a] == 0))
        bad(r, "absolute or unsupported segment alignment (A = %u)", a);
    length = get_word(r);
    if (acbp & 0x02) { /* B: the largest size */
        if (r->type & 1)
            bad(r, "4 GB segment");
        length = 0x10000;
    }
    o->segs = xrealloc(o->segs, (o->n_segs + 1) * sizeof(*o->segs));
    s       = &o->segs[o->n_segs++];
    memset(s, 0, sizeof(*s));
    s->name       = xstrdup(lname(r, get_index(r)));
    s->class_name = xstrdup(lname(r, get_index(r)));
    get_index(r); /* overlay name */
    s->length = length;
    s->align  = aligns[a];
    s->use32  = acbp & 0x01;
    s->data   = xcalloc(1, length ? length : 1);
}

static void
read_grpdef(reader_t *r)
{
    omf_t      *o    = r->omf;
    const char *name = lname(r, get_index(r));
    int         flat = strcmp(name, "FLAT") == 0;

    while (more(r)) {
        uint32_t seg;

        if (get8(r) != 0xff)
            bad(r, "unsupported group component");
        seg = get_index(r);
        if ((seg == 0) || (seg > o->n_segs))
            bad(r, "group segment %u out of range", seg);
        if (flat)
            o->segs[seg - 1].is_flat = 1;
    }
    o->groups              = xrealloc(o->groups, (o->n_groups + 1) * sizeof(*o->groups));
    o->groups[o->n_groups++] = xstrdup(name);
}

static void
read_extdef(reader_t *r)
{
    omf_t *o = r->omf;

    while (more(r)) {
        o->externs                = xrealloc(o->externs, (o->n_externs + 1) * sizeof(*o->externs));
        o->externs[o->n_externs++] = get_name(r);
        get_index(r); /* type */
    }
}

static void
read_pubdef(reader_t *r)
{
    omf_t   *o   = r->omf;
    uint32_t grp = get_index(r);
    uint32_t seg = get_index(r);

    (void) grp;
    if (seg == 0)
        bad(r, "absolute public symbols are not supported");
    if (seg > o->n_segs)
        bad(r, "public symbol segment %u out of range", seg);
    while (more(r)) {
        omf_pub_t *p;

        o->pubs = xrealloc(o->pubs, (o->n_pubs + 1) * sizeof(*o->pubs));
        p       = &o->pubs[o->n_pubs++];
        p->name   = get_name(r);
        p->offset = get_word(r);
        p->seg    = seg - 1;
        get_index(r); /* type */
        if (p->offset > o->segs[p->seg].length)
            bad(r, "public symbol %s lies past the end of its segment", p->name);
    }
}

typedef struct ledata_t {
    int      valid;
    uint32_t seg;    /* 0-based */
    uint32_t offset; /* where the record's data starts in the segment */
    uint32_t length;
} ledata_t;

static void
read_ledata(reader_t *r, ledata_t *last)
{
    omf_t     *o   = r->omf;
    uint32_t   seg = get_index(r);
    uint32_t   off;
    uint32_t   n;
    omf_seg_t *s;

    if ((seg == 0) || (seg > o->n_segs))
        bad(r, "data for segment %u, which does not exist", seg);
    s   = &o->segs[seg - 1];
    off = get_word(r);
    n   = (uint32_t) (r->len - r->pos);
    if ((off > s->length) || (n > s->length - off))
        bad(r, "data runs past the end of segment %s", s->name);
    memcpy(s->data + off, r->p + r->pos, n);
    r->pos       = r->len;
    last->valid  = 1;
    last->seg    = seg - 1;
    last->offset = off;
    last->length = n;
}

typedef struct thread_t {
    int      set;
    int      method;
    uint32_t index;
} thread_t;

/* Frame or target datum for a method; returns the reference. */
static omf_ref_t
datum(reader_t *r, int method, int is_frame)
{
    omf_t    *o = r->omf;
    omf_ref_t ref;

    ref.index = 0;
    switch (method) {
        case 0:
            ref.kind  = REF_SEGMENT;
            ref.index = get_index(r);
            if ((ref.index == 0) || (ref.index > o->n_segs))
                bad(r, "fixup segment %u out of range", ref.index);
            ref.index--;
            break;
        case 1:
            ref.kind  = REF_GROUP;
            ref.index = get_index(r);
            if ((ref.index == 0) || (ref.index > o->n_groups))
                bad(r, "fixup group %u out of range", ref.index);
            ref.index--;
            break;
        case 2:
            ref.kind  = REF_EXTERN;
            ref.index = get_index(r);
            if ((ref.index == 0) || (ref.index > o->n_externs))
                bad(r, "fixup external %u out of range", ref.index);
            ref.index--;
            break;
        case 3:
            bad(r, "fixups to absolute frames are not supported");
        case 4: /* frame of the location */
        case 5: /* frame of the target */
            if (!is_frame)
                bad(r, "target method %d", method);
            ref.kind  = (method == 4) ? -2 : -1;
            break;
        default:
            bad(r, "%s method %d", is_frame ? "frame" : "target", method);
    }
    return ref;
}

static void
read_fixupp(reader_t *r, const ledata_t *last)
{
    omf_t    *o = r->omf;
    thread_t  frames[4];
    thread_t  targets[4];

    memset(frames, 0, sizeof(frames));
    memset(targets, 0, sizeof(targets));
    while (more(r)) {
        uint32_t b = get8(r);

        if (!(b & 0x80)) { /* THREAD */
            int       is_frame = (b >> 6) & 1;
            int       method   = (b >> 2) & 7;
            thread_t *t        = is_frame ? &frames[b & 3] : &targets[b & 3];
            omf_ref_t ref;

            if (!is_frame && (method > 3))
                bad(r, "target thread method %d", method);
            ref       = datum(r, method, is_frame);
            t->set    = 1;
            t->method = method;
            t->index  = ref.index;
        } else { /* FIXUP */
            uint32_t     locat = (b << 8) | get8(r);
            uint32_t     fix   = get8(r);
            omf_fixup_t  f;
            omf_seg_t   *s;
            int          method;

            memset(&f, 0, sizeof(f));
            f.self_relative = !((locat >> 14) & 1);
            f.loc           = (int) ((locat >> 10) & 0x0f);
            f.line          = r->record;
            if (!last->valid)
                bad(r, "fixup without a preceding data record");
            if ((locat & 0x3ff) >= last->length)
                bad(r, "fixup outside its data record");
            f.offset = last->offset + (locat & 0x3ff);

            if (fix & 0x80) { /* frame from a thread */
                thread_t *t = &frames[(fix >> 4) & 3];

                if (!t->set)
                    bad(r, "undefined frame thread");
                method = t->method;
                if (method <= 2) {
                    f.frame.kind  = (method == 0) ? REF_SEGMENT : (method == 1) ? REF_GROUP : REF_EXTERN;
                    f.frame.index = t->index;
                } else
                    f.frame.kind = (method == 4) ? -2 : -1;
            } else
                f.frame = datum(r, (fix >> 4) & 7, 1);

            if (fix & 0x08) { /* target from a thread */
                thread_t *t = &targets[fix & 3];

                if (!t->set)
                    bad(r, "undefined target thread");
                method         = t->method & 3;
                f.target.kind  = (method == 0) ? REF_SEGMENT : (method == 1) ? REF_GROUP : REF_EXTERN;
                f.target.index = t->index;
            } else
                f.target = datum(r, fix & 3, 0);
            if (!(fix & 0x04)) /* P = 0: displacement present */
                f.disp = (int32_t) get_word(r);

            s = &o->segs[last->seg];
            s->fixups = xrealloc(s->fixups, (s->n_fixups + 1) * sizeof(*s->fixups));
            s->fixups[s->n_fixups++] = f;
        }
    }
}

omf_t *
omf_read(const char *path)
{
    size_t   size;
    uint8_t *file = read_file(path, &size);
    omf_t   *o    = xcalloc(1, sizeof(*o));
    reader_t r;
    ledata_t last;
    size_t   at    = 0;
    int      ended = 0;

    o->path = xstrdup(path);
    memset(&r, 0, sizeof(r));
    memset(&last, 0, sizeof(last));
    r.omf = o;
    while (!ended) {
        uint32_t reclen;
        uint8_t  sum = 0;
        size_t   i;

        r.record++;
        if (at + 3 > size)
            die("%s: ends without a MODEND record", path);
        r.type = file[at];
        reclen = file[at + 1] | ((uint32_t) file[at + 2] << 8);
        if ((reclen == 0) || (at + 3 + reclen > size))
            bad(&r, "record length %u runs past the end of the file", reclen);
        if (file[at + 2 + reclen] != 0) { /* a checksum of 0 means "not computed" */
            for (i = 0; i < 3 + (size_t) reclen; i++)
                sum = (uint8_t) (sum + file[at + i]);
            if (sum != 0)
                bad(&r, "bad checksum");
        }
        r.p   = file + at + 3;
        r.len = reclen - 1;
        r.pos = 0;

        switch (r.type) {
            case THEADR:
            case LINNUM:
            case LINNUM32:
                r.pos = r.len;
                break;
            case COMENT:
                if ((r.len >= 3) && (r.p[1] == 0xa0) && ((r.p[2] == 1) || (r.p[2] == 2)))
                    bad(&r, "NASM import/export directives are not supported; list imports and exports in the "
                            "link definition file");
                r.pos = r.len;
                break;
            case LNAMES:
                read_lnames(&r);
                break;
            case SEGDEF:
            case SEGDEF32:
                read_segdef(&r);
                break;
            case GRPDEF:
                read_grpdef(&r);
                break;
            case EXTDEF:
                read_extdef(&r);
                break;
            case PUBDEF:
            case PUBDEF32:
                read_pubdef(&r);
                break;
            case LEDATA:
            case LEDATA32:
                read_ledata(&r, &last);
                break;
            case FIXUPP:
            case FIXUPP32:
                read_fixupp(&r, &last);
                break;
            case MODEND:
            case MODEND32:
                r.pos = r.len;
                ended = 1;
                break;
            default:
                bad(&r, "unsupported record type");
        }
        if (r.pos != r.len)
            bad(&r, "%u unexpected bytes at the end of the record", (unsigned) (r.len - r.pos));
        if ((r.type != FIXUPP) && (r.type != FIXUPP32) && (r.type != LEDATA) && (r.type != LEDATA32) &&
            (r.type != LINNUM) && (r.type != LINNUM32))
            last.valid = 0;
        at += 3 + reclen;
    }
    free(file);
    return o;
}
