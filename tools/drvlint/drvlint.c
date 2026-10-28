/*
 * drvlint: structural checks for NE modules (16-bit Windows drivers) and LE
 * virtual device drivers, written independently of vglink so it can check
 * vglink's output. It also runs on Windows' own files (DIBENG.DLL, system
 * VxDs) in the tests, so the checks agree with files Windows accepts.
 *
 *   drvlint [--dump] [--strict] [--vxd | --display | --pe | --glide | --icd | --cpl] [--exports LIST] FILE...
 *
 * --display: also require what a Windows 98 display minidriver needs (module
 * name DISPLAY, a library with its own data segment, Windows 4.0).
 * --vxd: also require what a VxD needs (LE, VxD module flags, a *_DDB export
 * at ordinal 1).
 * --pe: also require what the DirectX driver DLL needs (a PE DLL in Windows
 * 98's shared arena, every section shared, a DriverInit export, imports from
 * KERNEL32 only, a version resource).
 * --glide: also require what a Glide DLL needs (a PE DLL in its process's own
 * space, no section shared, exports, imports from Windows' own DLLs only, a
 * version resource).
 * --icd: the same for the OpenGL ICD, also a DLL of its process's own.
 * --cpl: the same for the control program, vgpu98cp.cpl, which may import
 * from COMCTL32 too.
 * --exports LIST: the PE export table's names must be exactly LIST's (a file:
 * one name per line, ';' comments).
 * A file's version resource (PE, NE or VxD), if it has one, is checked and
 * shown; with --strict, --vxd also requires one.
 * --strict: also apply the checks vglink's own output must pass but some of
 * Windows' files do not (page map in order, fixup targets inside their
 * objects, relocation locations inside their segments, ...).
 * --dump prints every table, relocation and fixup.
 *
 * Format references: spec/ne-format.md, spec/le-format.md, spec/pe-format.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const char *path;
static uint8_t    *file;
static size_t      size;
static int         errors;
static int         dump;
static int         strict;

static void
error(const char *fmt, ...) VGPU98_PRINTF(1, 2);

static void
error(const char *fmt, ...)
{
    va_list ap;

    errors++;
    fprintf(stderr, "%s: ", path);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* An error only in strict mode. */
static void
strict_error(const char *fmt, ...) VGPU98_PRINTF(1, 2);

static void
strict_error(const char *fmt, ...)
{
    va_list ap;

    if (!strict)
        return;
    errors++;
    fprintf(stderr, "%s: ", path);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void
out(const char *fmt, ...) VGPU98_PRINTF(1, 2);

static void
out(const char *fmt, ...)
{
    va_list ap;

    if (!dump)
        return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* Bounds-checked reads: out-of-range reads report once and return 0. */
static int
in_file(size_t at, size_t n)
{
    return (at <= size) && (n <= size - at);
}

static uint32_t
r8(size_t at)
{
    return in_file(at, 1) ? file[at] : 0;
}

static uint32_t
r16(size_t at)
{
    return r8(at) | (r8(at + 1) << 8);
}

static uint32_t
r32(size_t at)
{
    return r16(at) | (r16(at + 2) << 16);
}

/* A length-prefixed names table: checks it ends inside the file; returns its end. */
typedef struct name_t {
    char     text[256];
    uint32_t ordinal;
} name_t;

static size_t
read_names(size_t at, size_t limit, name_t **names, size_t *n, const char *what)
{
    *names = NULL;
    *n     = 0;
    for (;;) {
        uint32_t len;

        if (!in_file(at, 1) || (at >= limit)) {
            error("%s table runs past its end", what);
            return at;
        }
        len = r8(at);
        if (len == 0)
            return at + 1;
        if (!in_file(at + 1, len + 2) || (at + 3 + len > limit)) {
            error("%s table runs past its end", what);
            return at;
        }
        *names = xrealloc(*names, (*n + 1) * sizeof(**names));
        memcpy((*names)[*n].text, file + at + 1, len);
        (*names)[*n].text[len] = 0;
        (*names)[*n].ordinal   = r16(at + 1 + len);
        (*n)++;
        at += 3 + len;
    }
}

/* The version resource's checks (below, with the PE ones). */
static int check_version(size_t at, uint32_t n, int narrow);
static int check_version_record(size_t at, uint32_t n);

/* --- NE -------------------------------------------------------------------- */

typedef struct ne_seg_t {
    size_t   offset; /* file */
    uint32_t length; /* in the file */
    uint32_t alloc;
    uint32_t flags;
} ne_seg_t;

typedef struct ne_entry_t {
    uint32_t ordinal;
    uint32_t flags;
    uint32_t seg;
    uint32_t offset;
} ne_entry_t;

static int
has_entry(const ne_entry_t *e, size_t n, uint32_t ordinal)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (e[i].ordinal == ordinal)
            return 1;
    }
    return 0;
}

static void
check_ne(size_t ne, int display)
{
    uint32_t    flags    = r16(ne + 0x0c);
    uint32_t    autodata = r16(ne + 0x0e);
    uint32_t    cseg     = r16(ne + 0x1c);
    uint32_t    cmod     = r16(ne + 0x1e);
    uint32_t    shift    = r16(ne + 0x32);
    size_t      segtab   = ne + r16(ne + 0x22);
    size_t      rsrctab  = ne + r16(ne + 0x24);
    size_t      restab   = ne + r16(ne + 0x26);
    size_t      modtab   = ne + r16(ne + 0x28);
    size_t      imptab   = ne + r16(ne + 0x2a);
    size_t      enttab   = ne + r16(ne + 0x04);
    size_t      entlen   = r16(ne + 0x06);
    size_t      nrestab  = r32(ne + 0x2c);
    size_t      nreslen  = r16(ne + 0x20);
    uint32_t    csip     = r32(ne + 0x14);
    ne_seg_t   *segs;
    ne_entry_t *ents   = NULL;
    size_t      n_ents = 0;
    name_t     *res, *nres;
    size_t      n_res, n_nres;
    size_t      at, i;
    uint32_t    ordinal = 1;
    char      **modules;

    out("NE module: flags %04X, auto data %u, heap %u, stack %u, CS:IP %04X:%04X, Windows %u.%u, %u segments, "
        "%u modules, alignment %u\n",
        flags, autodata, r16(ne + 0x10), r16(ne + 0x12), csip >> 16, csip & 0xffff, r8(ne + 0x3f), r8(ne + 0x3e),
        cseg, cmod, 1u << (shift & 15));
    if ((shift == 0) || (shift > 15))
        error("alignment shift %u", shift);
    if (r8(ne + 0x36) != 2)
        error("target OS %u, expected 2 (Windows)", r8(ne + 0x36));
    if (!((segtab <= rsrctab) && (rsrctab <= restab) && (restab <= modtab) && (modtab <= imptab) &&
          (imptab <= enttab)))
        strict_error("the header's tables are out of the usual order");
    if (!in_file(segtab, 8u * cseg))
        error("segment table runs past the end of the file");

    /* Segments. */
    segs = xcalloc(cseg + 1, sizeof(*segs));
    for (i = 0; i < cseg; i++) {
        ne_seg_t *s = &segs[i];

        s->offset = (size_t) r16(segtab + 8 * i) << shift;
        s->length = r16(segtab + 8 * i + 2);
        s->flags  = r16(segtab + 8 * i + 4);
        s->alloc  = r16(segtab + 8 * i + 6);
        if (s->length == 0 && s->offset)
            s->length = 0x10000;
        if (s->alloc == 0)
            s->alloc = 0x10000;
        out("  segment %u: file %06lX, %04X bytes, alloc %04X, flags %04X (%s%s%s%s)\n", (unsigned) i + 1,
            (unsigned long) s->offset, s->length, s->alloc, s->flags, (s->flags & 1) ? "data" : "code",
            (s->flags & 0x10) ? ", moveable" : ", fixed", (s->flags & 0x40) ? ", preload" : "",
            (s->flags & 0x1000) ? ", discardable" : "");
        if (s->offset && !in_file(s->offset, s->length))
            error("segment %u data runs past the end of the file", (unsigned) i + 1);
        if (s->alloc < s->length)
            strict_error("segment %u: allocation smaller than its data", (unsigned) i + 1);
    }
    if (autodata && ((autodata > cseg) || !(segs[autodata - 1].flags & 1)))
        strict_error("automatic data segment %u is not a data segment", autodata);
    if ((flags & 3) == 1 && !autodata)
        error("SINGLEDATA without an automatic data segment");
    if (csip && (((csip >> 16) == 0) || ((csip >> 16) > cseg) || ((csip & 0xffff) >= segs[(csip >> 16) - 1].length)))
        error("entry point %08X is outside its segment", csip);

    /* Modules. */
    modules = xcalloc(cmod + 1, sizeof(*modules));
    for (i = 0; i < cmod; i++) {
        size_t o = imptab + r16(modtab + 2 * i);

        if ((o >= enttab) || !in_file(o, 1) || (o + 1 + r8(o) > enttab)) {
            error("module reference %u points outside the imported names", (unsigned) i + 1);
            modules[i] = xstrdup("?");
            continue;
        }
        modules[i] = xcalloc(1, r8(o) + 1);
        memcpy(modules[i], file + o + 1, r8(o));
        out("  imports from %s\n", modules[i]);
    }

    /* Entry table. */
    at = enttab;
    while (at < enttab + entlen) {
        uint32_t count = r8(at);
        uint32_t type  = r8(at + 1);
        uint32_t k;

        if (count == 0)
            break;
        at += 2;
        for (k = 0; k < count; k++, ordinal++) {
            ne_entry_t e;

            if (type == 0)
                continue;
            e.ordinal = ordinal;
            if (type == 0xff) {
                e.flags  = r8(at);
                e.seg    = r8(at + 3);
                e.offset = r16(at + 4);
                if (r16(at + 1) != 0x3fcd)
                    error("moveable entry %u without INT 3Fh", ordinal);
                at += 6;
            } else {
                e.flags  = r8(at);
                e.seg    = type;
                e.offset = r16(at + 1);
                at += 3;
            }
            if ((e.seg == 0) || (e.seg > cseg) || (e.offset >= segs[e.seg - 1].alloc))
                error("entry %u (%u:%04X) is outside its segment", ordinal, e.seg, e.offset);
            ents = xrealloc(ents, (n_ents + 1) * sizeof(*ents));
            ents[n_ents++] = e;
        }
    }
    if (at + 1 > enttab + entlen)
        error("entry table runs past its length");

    /* Names. */
    read_names(restab, modtab, &res, &n_res, "resident names");
    if (!in_file(nrestab, nreslen))
        error("non-resident names table runs past the end of the file");
    else if (read_names(nrestab, nrestab + nreslen, &nres, &n_nres, "non-resident names") != nrestab + nreslen)
        error("non-resident names table length does not match");
    if (n_res == 0)
        error("no module name");
    else
        out("  module name %s\n", res[0].text);
    if (n_nres)
        out("  description \"%s\"\n", nres[0].text);
    for (i = 1; i < n_res; i++) {
        out("  resident export %s @%u\n", res[i].text, res[i].ordinal);
        if (!has_entry(ents, n_ents, res[i].ordinal))
            error("resident name %s has no entry @%u", res[i].text, res[i].ordinal);
    }
    for (i = 1; i < n_nres; i++) {
        out("  export %s @%u\n", nres[i].text, nres[i].ordinal);
        if (!has_entry(ents, n_ents, nres[i].ordinal))
            error("name %s has no entry @%u", nres[i].text, nres[i].ordinal);
    }
    for (i = 0; i < n_ents; i++)
        out("  entry @%u: %u:%04X flags %02X\n", ents[i].ordinal, ents[i].seg, ents[i].offset, ents[i].flags);

    /* Relocations. */
    for (i = 0; i < cseg; i++) {
        const ne_seg_t *s = &segs[i];
        size_t          r;
        uint32_t        n, k;

        if (!(s->flags & 0x100))
            continue;
        r = s->offset + s->length;
        if (!s->offset || !in_file(r, 2)) {
            error("segment %u: relocation records missing", (unsigned) i + 1);
            continue;
        }
        n = r16(r);
        if (!in_file(r + 2, 8u * n)) {
            error("segment %u: relocation records run past the end of the file", (unsigned) i + 1);
            continue;
        }
        for (k = 0; k < n; k++) {
            size_t   p     = r + 2 + 8 * k;
            uint32_t src   = r8(p);
            uint32_t rflag = r8(p + 1);
            uint32_t off   = r16(p + 2);
            uint32_t a     = r16(p + 4);
            uint32_t b     = r16(p + 6);
            uint32_t width = (src == 0) ? 1 : (src == 3 || src == 13) ? 4 : (src == 11) ? 6 : 2;
            uint32_t chain = off;
            int      links = 0;

            if ((src != 0) && (src != 2) && (src != 3) && (src != 5) && (src != 11) && (src != 13))
                error("segment %u: relocation %u has source type %u", (unsigned) i + 1, k, src);
            switch (rflag & 3) {
                case 0:
                    if ((a & 0xff) == 0xff) {
                        if (!has_entry(ents, n_ents, b))
                            error("segment %u: relocation %u to missing entry @%u", (unsigned) i + 1, k, b);
                    } else if (((a & 0xff) == 0) || ((a & 0xff) > cseg))
                        error("segment %u: relocation %u to segment %u", (unsigned) i + 1, k, a & 0xff);
                    out("    seg %u+%04X: type %u internal %u:%04X%s\n", (unsigned) i + 1, off, src, a & 0xff, b,
                        (rflag & 4) ? " additive" : "");
                    break;
                case 1:
                case 2:
                    if ((a == 0) || (a > cmod))
                        error("segment %u: relocation %u to module %u", (unsigned) i + 1, k, a);
                    out("    seg %u+%04X: type %u %s.%s%u%s\n", (unsigned) i + 1, off, src,
                        (a && a <= cmod) ? modules[a - 1] : "?", (rflag & 3) == 1 ? "" : "name@", b,
                        (rflag & 4) ? " additive" : "");
                    break;
                default:
                    out("    seg %u+%04X: type %u OS fixup %u\n", (unsigned) i + 1, off, src, a);
                    break;
            }
            /* Follow the chain of a non-additive relocation through the segment. */
            while (!(rflag & 4)) {
                if (chain + width > s->length) {
                    strict_error("segment %u: relocation %u location %04X is outside the segment", (unsigned) i + 1, k,
                          chain);
                    break;
                }
                if (src == 0)
                    break; /* byte locations cannot chain */
                chain = r16(s->offset + chain);
                if (chain == 0xffff)
                    break;
                if (++links > 4096) {
                    strict_error("segment %u: relocation %u chain does not end", (unsigned) i + 1, k);
                    break;
                }
            }
            if ((rflag & 4) && (off + width > s->length))
                strict_error("segment %u: relocation %u location %04X is outside the segment", (unsigned) i + 1, k, off);
        }
    }

    /* Resources. */
    if (rsrctab != restab) {
        uint32_t rshift = r16(rsrctab);

        at = rsrctab + 2;
        for (;;) {
            uint32_t tid = r16(at);
            uint32_t cnt;
            uint32_t k;

            if (tid == 0)
                break;
            if (at + 8 > restab) {
                error("resource table runs past its end");
                break;
            }
            cnt = r16(at + 2);
            at += 8;
            for (k = 0; k < cnt; k++, at += 12) {
                size_t   o   = (size_t) r16(at) << rshift;
                size_t   len = (size_t) r16(at + 2) << rshift;
                uint32_t id  = r16(at + 6);
                char     tname[64], iname[64];

                if (tid & 0x8000)
                    snprintf(tname, sizeof(tname), "%u", tid & 0x7fff);
                else
                    snprintf(tname, sizeof(tname), "%.*s", (int) r8(rsrctab + tid), file + rsrctab + tid + 1);
                if (id & 0x8000)
                    snprintf(iname, sizeof(iname), "%u", id & 0x7fff);
                else
                    snprintf(iname, sizeof(iname), "%.*s", (int) r8(rsrctab + id), file + rsrctab + id + 1);
                out("  resource %s %s: file %06lX, %lu bytes, flags %04X\n", tname, iname, (unsigned long) o, (unsigned long) len, r16(at + 4));
                if (!in_file(o, len) && !in_file(o, len - (1u << rshift) + 1))
                    strict_error("resource %s %s runs past the end of the file", tname, iname);
                else if (tid == 0x8010) /* RT_VERSION: the 16-bit VS_VERSIONINFO */
                    check_version(o, (uint32_t) (in_file(o, len) ? len : size - o), 1);
            }
        }
    }

    if (display) {
        if ((n_res == 0) || strcmp(res[0].text, "DISPLAY"))
            error("a display driver's module name must be DISPLAY");
        if (!(flags & 0x8000))
            error("a display driver is a library module");
        if ((flags & 3) != 1)
            error("a display driver has a single automatic data segment");
        if (r16(ne + 0x3e) < 0x400)
            error("expected Windows version %04X; a Windows 98 minidriver needs 0400", r16(ne + 0x3e));
        for (i = 0; i < cseg; i++) {
            if (segs[i].flags & 0x10)
                error("segment %u is moveable; display driver segments stay fixed", (unsigned) i + 1);
        }
    }
    for (i = 0; i < cmod; i++)
        free(modules[i]);
    free(modules);
    free(segs);
    free(ents);
}

/* --- LE ------------------------------------------------------------------- */

typedef struct le_obj_t {
    uint32_t vsize;
    uint32_t flags;
    uint32_t page;  /* first, 1-based */
    uint32_t pages;
} le_obj_t;

static void
check_le(size_t le, int vxd)
{
    uint32_t  mflags   = r32(le + 0x10);
    uint32_t  n_pages  = r32(le + 0x14);
    uint32_t  pagesize = r32(le + 0x28);
    uint32_t  lastpage = r32(le + 0x2c);
    size_t    objtab   = le + r32(le + 0x40);
    uint32_t  n_obj    = r32(le + 0x44);
    size_t    objmap   = le + r32(le + 0x48);
    size_t    restab   = le + r32(le + 0x58);
    size_t    enttab   = le + r32(le + 0x5c);
    size_t    fpagetab = le + r32(le + 0x68);
    size_t    frectab  = le + r32(le + 0x6c);
    size_t    impmod   = le + r32(le + 0x70);
    size_t    datapage = r32(le + 0x80);
    size_t    nrestab  = r32(le + 0x88);
    size_t    nreslen  = r32(le + 0x8c);
    le_obj_t *objs;
    name_t   *res, *nres = NULL;
    size_t    n_res, n_nres = 0;
    size_t    i;
    uint32_t  ddb_obj = 0;

    out("LE module: cpu %u, os %u, flags %05X, %u pages of %u bytes (last %u), %u objects, device ID %04X, DDK %04X\n",
        r16(le + 0x08), r16(le + 0x0a), mflags, n_pages, pagesize, lastpage, n_obj, r16(le + 0xc0),
        r16(le + 0xc2));
    if (r8(le + 2) || r8(le + 3))
        error("byte or word order is not little-endian");
    if ((pagesize == 0) || (pagesize & (pagesize - 1)) || (pagesize > 0x10000))
        error("page size %u", pagesize);
    if ((n_pages && ((lastpage == 0) || (lastpage > pagesize))))
        error("last page size %u", lastpage);
    if (n_pages && pagesize && !in_file(datapage, (size_t) (n_pages - 1) * pagesize + lastpage))
        error("page data runs past the end of the file");
    if (r32(le + 0x38) != fpagetab - objtab)
        error("loader section size %X, tables span %lX", r32(le + 0x38), (unsigned long) (fpagetab - objtab));
    if (r32(le + 0x30) != r32(le + 0x78) - r32(le + 0x68))
        error("fixup section size %X does not match its tables", r32(le + 0x30));
    if (r32(le + 0x74) || (r32(le + 0x70) != r32(le + 0x78)))
        error("the module imports from other modules");
    if (r32(le + 0x4c))
        error("iterated pages are not supported");
    if (!in_file(objtab, 24u * n_obj))
        error("object table runs past the end of the file");

    /* Objects and the page map. */
    objs = xcalloc(n_obj + 1, sizeof(*objs));
    for (i = 0; i < n_obj; i++) {
        le_obj_t *o = &objs[i];
        char      tag[5];

        o->vsize = r32(objtab + 24 * i);
        o->flags = r32(objtab + 24 * i + 8);
        o->page  = r32(objtab + 24 * i + 12);
        o->pages = r32(objtab + 24 * i + 16);
        memcpy(tag, file + objtab + 24 * i + 20, 4);
        tag[4] = 0;
        out("  object %u %s: %X bytes, flags %04X, pages %u-%u\n", (unsigned) i + 1, tag, o->vsize, o->flags,
            o->page, o->page + o->pages - 1);
        if (o->pages && ((o->page == 0) || (o->page + o->pages - 1 > n_pages)))
            error("object %u pages lie outside the page map", (unsigned) i + 1);
        /* Pages may cover less than the virtual size (the rest is zero-filled), and
           Windows' own files leave gaps between objects' pages. */
    }
    for (i = 0; i < n_pages; i++) {
        size_t   p  = objmap + 4 * i;
        uint32_t pn = (r8(p) << 16) | (r8(p + 1) << 8) | r8(p + 2);

        if (r8(p + 3) != 0)
            error("page %u has flags %02X; only stored pages are supported", (unsigned) i + 1, r8(p + 3));
        if (pn != i + 1)
            strict_error("page map entry %u holds page %u", (unsigned) i + 1, pn);
    }

    /* Names and the entry table. */
    read_names(restab, enttab, &res, &n_res, "resident names");
    if (nrestab) {
        if (!in_file(nrestab, nreslen))
            error("non-resident names table runs past the end of the file");
        else
            read_names(nrestab, nrestab + nreslen, &nres, &n_nres, "non-resident names");
    }
    if (n_res)
        out("  module name %s\n", res[0].text);
    if (n_nres)
        out("  description \"%s\"\n", nres[0].text);
    {
        size_t   at      = enttab;
        uint32_t ordinal = 1;

        for (;;) {
            uint32_t count = r8(at);
            uint32_t type  = r8(at + 1);
            uint32_t k;

            if (count == 0)
                break;
            if (type == 0) {
                ordinal += count;
                at += 2;
                continue;
            }
            if (type != 3) {
                error("entry bundle type %u (only 32-bit entries are supported)", type);
                break;
            }
            {
                uint32_t obj = r16(at + 2);

                at += 4;
                for (k = 0; k < count; k++, ordinal++, at += 5) {
                    uint32_t off = r32(at + 1);

                    out("  entry @%u: object %u + %X, flags %02X\n", ordinal, obj, off, r8(at));
                    if ((obj == 0) || (obj > n_obj) || (off >= objs[obj - 1].vsize))
                        error("entry @%u lies outside its object", ordinal);
                    if (ordinal == 1)
                        ddb_obj = obj;
                }
            }
        }
    }

    /* Fixups. */
    if (!in_file(fpagetab, 4u * (n_pages + 1)))
        error("fixup page table runs past the end of the file");
    else {
        for (i = 0; i < n_obj; i++) {
            uint32_t p;

            for (p = 0; p < objs[i].pages; p++) {
                uint32_t page = objs[i].page - 1 + p;
                size_t   at, end;

                if (page >= n_pages)
                    break;
                at = frectab + r32(fpagetab + 4 * page);
                end = frectab + r32(fpagetab + 4 * page + 4);

                if ((end < at) || (end > impmod)) {
                    error("page %u: fixup records out of range", page + 1);
                    continue;
                }
                while (at < end) {
                    uint32_t src   = r8(at);
                    uint32_t flags = r8(at + 1);
                    uint32_t count = 1; /* source offsets */
                    int32_t  soffs[256];
                    uint32_t obj, target, k;

                    at += 2;
                    if ((src & 0xf) != 7 && (src & 0xf) != 8) {
                        error("page %u: fixup source type %02X is not supported here", page + 1, src);
                        break;
                    }
                    if ((flags & 3) != 0) {
                        error("page %u: fixup target type %u (only internal references)", page + 1, flags & 3);
                        break;
                    }
                    if (src & 0x20) /* source list: a count here, the offsets after the target */
                        count = r8(at++);
                    else {
                        soffs[0] = (int16_t) r16(at);
                        at += 2;
                    }
                    obj = (flags & 0x40) ? r16(at) : r8(at);
                    at += (flags & 0x40) ? 2 : 1;
                    target = (flags & 0x10) ? r32(at) : r16(at);
                    at += (flags & 0x10) ? 4 : 2;
                    if (src & 0x20) {
                        for (k = 0; k < count; k++, at += 2)
                            soffs[k] = (int16_t) r16(at);
                    }
                    if ((obj == 0) || (obj > n_obj) || (target > objs[obj - 1].vsize))
                        strict_error("page %u: fixup target %u:%X is outside its object", page + 1, obj, target);
                    for (k = 0; k < count; k++) {
                        int32_t soff = soffs[k];

                        out("    object %u + %X: %s to object %u + %X\n", (unsigned) i + 1,
                            (unsigned) ((int32_t) (p * pagesize) + soff),
                            (src & 0xf) == 7 ? "offset32" : "relative32", obj, target);
                        if ((soff < -3) || (soff >= (int32_t) pagesize) ||
                            ((int64_t) p * pagesize + soff + 4 > (int64_t) objs[i].vsize))
                            strict_error("page %u: fixup location %d is outside the page or object", page + 1, soff);
                    }
                }
                if (at != end)
                    error("page %u: fixup records overrun", page + 1);
            }
        }
    }

    if (vxd) {
        if ((mflags & 0x38000) != 0x38000 && (mflags & 0x38000) != 0x28000)
            error("module flags %05X are not a VxD's", mflags);
        if ((r16(le + 0x08) != 2) || (r16(le + 0x0a) != 4))
            error("a VxD is for the 386 (cpu 2) and Windows 386 (os 4)");
        if (!ddb_obj)
            error("no entry @1 (the device descriptor block)");
        for (i = 1; i < n_nres; i++) {
            if (nres[i].ordinal == 1)
                break;
        }
        if ((i >= n_nres) || (strlen(nres[i].text) < 5) ||
            strcmp(nres[i].text + strlen(nres[i].text) - 4, "_DDB"))
            error("the export @1 must be named *_DDB");
    }

    /* The version resource Windows shows for a VxD: the header's +B8h and
       +BCh place a 16-bit resource record, at the end of the file in
       Windows' own VxDs. Some of Windows' files
       have none; ours must. */
    if (r32(le + 0xb8) || r32(le + 0xbc)) {
        const int type = check_version_record(r32(le + 0xb8), r32(le + 0xbc));

        if (vxd && (type >= 0) && (type != 5))
            strict_error("a VxD's version resource has file type %d, not 5 (VFT_VXD)", type);
    } else if (vxd)
        strict_error("no version resource");
    free(objs);
}

/* --- PE -------------------------------------------------------------------- */

typedef struct pe_sec_t {
    char     name[9];
    uint32_t vsize, rva, raw, raw_at, flags;
} pe_sec_t;

static pe_sec_t *pe_secs;
static uint32_t  pe_nsecs;

/* File offset of an RVA inside a section's raw data, or 0 (with an error) if none holds it. */
static size_t
pe_offset(uint32_t rva, uint32_t n, const char *what)
{
    uint32_t i;

    for (i = 0; i < pe_nsecs; i++) {
        const pe_sec_t *s = &pe_secs[i];

        if ((rva >= s->rva) && (rva - s->rva + (uint64_t) n <= s->raw) && in_file(s->raw_at + (rva - s->rva), n))
            return s->raw_at + (rva - s->rva);
    }
    error("%s at RVA %X is not in any section's file data", what, rva);
    return 0;
}

static int
pe_inside(uint32_t rva, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < pe_nsecs; i++) {
        if ((rva >= pe_secs[i].rva) && (rva - pe_secs[i].rva + (uint64_t) n <= pe_secs[i].vsize))
            return 1;
    }
    return 0;
}

static void
pe_string(uint32_t rva, char *out_text, size_t max, const char *what)
{
    size_t at = pe_offset(rva, 1, what);
    size_t i;

    out_text[0] = 0;
    if (!at)
        return;
    for (i = 0; i + 1 < max; i++) {
        if (!in_file(at + i, 1)) {
            error("%s runs past the end of the file", what);
            break;
        }
        out_text[i] = (char) file[at + i];
        if (!out_text[i])
            return;
    }
    out_text[max - 1] = 0;
}

/* Is DLL the name WANT (upper case), in any case? */
static int
dll_is(const char *dll, const char *want)
{
    size_t i;

    for (i = 0; want[i]; i++) {
        char c = dll[i];

        if ((c >= 'a') && (c <= 'z'))
            c = (char) (c - 'a' + 'A');
        if (c != want[i])
            return 0;
    }
    return dll[i] == 0;
}

/* A Glide DLL imports from Windows' own DLLs alone: those a Voodoo3's Glide
   imports from, never a C runtime. The OpenGL
   ICD likewise, and the control program, which also
   takes its property sheets and trackbars from COMCTL32. */
static const char *own_dll   = "a Glide DLL"; /* what --glide, --icd or --cpl checks, for the messages */
static int         comctl_ok = 0;             /* --cpl: COMCTL32 too */

static int
is_glide_import(const char *dll)
{
    static const char *const allowed[] = { "KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "ADVAPI32.DLL", "DDRAW.DLL" };
    size_t                   i;

    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (dll_is(dll, allowed[i]))
            return 1;
    }
    return comctl_ok && dll_is(dll, "COMCTL32.DLL");
}

/* --exports FILE: the names the export table must hold, exactly (one per
   line, ';' comments and blank lines ignored: vglink's list format), sorted. */
static char      **want_exports;
static size_t      n_want_exports;
static const char *exports_path;

static int
str_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static void
load_exports(const char *list)
{
    size_t   n;
    uint8_t *text = read_file(list, &n);
    char    *p    = (char *) text;

    exports_path = list;
    if (strlen((const char *) text) != n)
        die("%s: a NUL byte in a list of names", list);
    if ((n >= 3) && (text[0] == 0xef) && (text[1] == 0xbb) && (text[2] == 0xbf))
        die("%s: starts with a UTF-8 byte order mark; save it as plain ASCII", list);
    while (*p) {
        char *start, *end;

        while ((*p == ' ') || (*p == '\t'))
            p++;
        start = p;
        while (*p && (*p != '\n'))
            p++;
        end = p;
        if (*p)
            p++;
        while ((end > start) && ((end[-1] == '\r') || (end[-1] == ' ') || (end[-1] == '\t')))
            end--;
        if ((end == start) || (*start == ';') || (*start == '#'))
            continue;
        want_exports = xrealloc(want_exports, (n_want_exports + 1) * sizeof(*want_exports));
        want_exports[n_want_exports] = xmalloc((size_t) (end - start) + 1);
        memcpy(want_exports[n_want_exports], start, (size_t) (end - start));
        want_exports[n_want_exports][end - start] = 0;
        n_want_exports++;
    }
    free(text);
    if (n_want_exports)
        qsort(want_exports, n_want_exports, sizeof(*want_exports), str_cmp);
}

/* The export table's names (sorted, as the table must be) against the list:
   each name missing and each one too many, the first 20 of each. */
static void
compare_exports(char **have, size_t n_have)
{
    size_t i = 0, j = 0;
    int    missing = 0, extra = 0;

    while ((i < n_want_exports) || (j < n_have)) {
        const int c = (i == n_want_exports) ? 1 : (j == n_have) ? -1 : strcmp(want_exports[i], have[j]);

        if (c == 0) {
            i++;
            j++;
        } else if (c < 0) {
            if (missing++ < 20)
                error("export %s (in %s) is missing", want_exports[i], exports_path);
            i++;
        } else {
            if (extra++ < 20)
                error("export %s is not in %s", have[j], exports_path);
            j++;
        }
    }
    if ((missing > 20) || (extra > 20))
        error("%d exports missing and %d not in %s in all", missing, extra, exports_path);
}

/* --- PE: the version resource ------------------------------------------------ */

/* A UTF-16 key or value at AT (up to END) as ASCII in TEXT (at most MAX - 1
   characters; '?' for anything else). Returns the offset past it (past its
   terminator if it has one before END), or 0 if there is no room for one
   character. *CHARS is its length in characters, its terminator counted. */
static size_t
wstr(size_t at, size_t end, char *text, size_t max, int *terminated, uint32_t *chars)
{
    size_t n = 0;

    *terminated = 0;
    *chars      = 0;
    for (;;) {
        uint32_t c;

        if ((at + 2 > end) || !in_file(at, 2)) {
            text[n] = 0;
            return *chars ? at : 0;
        }
        c = r16(at);
        at += 2;
        (*chars)++;
        if (c == 0) {
            *terminated = 1;
            break;
        }
        if (n + 1 < max)
            text[n++] = ((c >= 0x20) && (c < 0x7f)) ? (char) c : '?';
    }
    text[n] = 0;
    return at;
}

/* The same for the 16-bit form's ANSI text: one byte a character. */
static size_t
nstr(size_t at, size_t end, char *text, size_t max, int *terminated, uint32_t *chars)
{
    size_t n = 0;

    *terminated = 0;
    *chars      = 0;
    for (;;) {
        uint32_t c;

        if ((at + 1 > end) || !in_file(at, 1)) {
            text[n] = 0;
            return *chars ? at : 0;
        }
        c = r8(at);
        at++;
        (*chars)++;
        if (c == 0) {
            *terminated = 1;
            break;
        }
        if (n + 1 < max)
            text[n++] = ((c >= 0x20) && (c < 0x7f)) ? (char) c : '?';
    }
    text[n] = 0;
    return at;
}

/* A version block at AT (inside its parent, which ends at END): its length,
   value length, type and key, and where its value starts. 0 if malformed.
   Blocks and values start on 32-bit boundaries from the resource's start
   (version_base). The 16-bit form (NE files and VxDs, version_narrow) has
   no type, and its text is ANSI. */
typedef struct vblock_t {
    size_t   at, end, value;
    uint32_t value_len, type;
    char     key[64];
} vblock_t;

static size_t version_base;
static int    version_narrow;

static size_t
vstr(size_t at, size_t end, char *text, size_t max, int *terminated, uint32_t *chars)
{
    return version_narrow ? nstr(at, end, text, max, terminated, chars) : wstr(at, end, text, max, terminated, chars);
}

static size_t
valign(size_t at)
{
    return version_base + ((at - version_base + 3) & ~(size_t) 3);
}

static int
vblock(size_t at, size_t end, vblock_t *b)
{
    const size_t head = version_narrow ? 4 : 6;
    uint32_t     len, chars;
    size_t       k;
    int          terminated;

    if ((at + head > end) || !in_file(at, head))
        return 0;
    len = r16(at);
    if ((len < head) || (at + len > end))
        return 0;
    b->at        = at;
    b->end       = at + len;
    b->value_len = r16(at + 2);
    b->type      = version_narrow ? 1 : r16(at + 4);
    k            = vstr(at + head, b->end, b->key, sizeof(b->key), &terminated, &chars);
    if (!k || !terminated)
        return 0;
    b->value = valign(k);
    return 1;
}

/* Checks the version resource at file offset AT, N bytes: a 32-bit
   VS_VERSIONINFO (PE), or with NARROW the 16-bit one (NE, VxD). Shows its
   numbers, strings and translations; returns its file type, or -1 if it is
   malformed. */
static int
check_version(size_t at, uint32_t n, int narrow)
{
    vblock_t root, child;
    size_t   c;
    uint32_t fms, fls, pms, pls;

    version_base   = at;
    version_narrow = narrow;
    if (!vblock(at, at + n, &root) || strcmp(root.key, "VS_VERSION_INFO") || (root.value_len != 52) ||
        (root.value + 52 > root.end) || (r32(root.value) != 0xfeef04bd)) {
        error("the version resource is not a %s VS_VERSIONINFO with its fixed file information",
              narrow ? "16-bit" : "32-bit");
        return -1;
    }
    fms = r32(root.value + 8);
    fls = r32(root.value + 12);
    pms = r32(root.value + 16);
    pls = r32(root.value + 20);
    out("  version resource: file %u.%u.%u.%u, product %u.%u.%u.%u, OS %X, type %u, subtype %u\n", fms >> 16,
        fms & 0xffff, fls >> 16, fls & 0xffff, pms >> 16, pms & 0xffff, pls >> 16, pls & 0xffff, r32(root.value + 32),
        r32(root.value + 36), r32(root.value + 40));
    /* The children: StringFileInfo (its tables' strings shown) and
       VarFileInfo (its translations shown). */
    for (c = valign(root.value + 52); c < root.end; c = valign(child.end)) {
        size_t t;

        if (!vblock(c, root.end, &child)) {
            error("the version resource has a malformed block at %lX", (unsigned long) c);
            return -1;
        }
        if (strcmp(child.key, "VarFileInfo") == 0) {
            for (t = child.value; t < child.end;) {
                vblock_t var;
                size_t   v;

                if (!vblock(t, child.end, &var)) {
                    error("the version resource has a malformed VarFileInfo at %lX", (unsigned long) t);
                    return -1;
                }
                if (strcmp(var.key, "Translation") == 0) {
                    for (v = var.value; (v + 4 <= var.end) && (v + 4 <= var.value + var.value_len); v += 4)
                        out("    Translation: %04X %04X\n", r16(v), r16(v + 2));
                }
                t = valign(var.end);
            }
            continue;
        }
        if (strcmp(child.key, "StringFileInfo") != 0)
            continue;
        for (t = child.value; t < child.end;) {
            vblock_t table;
            size_t   s;

            if (!vblock(t, child.end, &table)) {
                error("the version resource has a malformed string table at %lX", (unsigned long) t);
                return -1;
            }
            out("    string table %s\n", table.key);
            for (s = table.value; s < table.end;) {
                vblock_t str;
                char     value[256];
                int      terminated = 1;
                uint32_t chars      = 0;

                value[0] = 0;
                if (!vblock(s, table.end, &str) ||
                    ((str.value_len > 0) && !vstr(str.value, str.end, value, sizeof(value), &terminated, &chars))) {
                    error("the version resource has a malformed string at %lX", (unsigned long) s);
                    return -1;
                }
                /* Windows accepts a value that fills its block without a
                   terminator; vglink writes every one with its own, its
                   length in characters with it, and the text type. */
                if (str.value_len > 0) {
                    if (!terminated)
                        strict_error("the version resource's %s has no terminator", str.key);
                    else if (str.value_len != chars)
                        strict_error("the version resource's %s: value length %u, %u characters with the terminator",
                                     str.key, str.value_len, chars);
                }
                if (str.type != 1)
                    strict_error("the version resource's %s is not of the text type", str.key);
                out("    %s: %s\n", str.key, value);
                s = valign(str.end);
            }
            t = valign(table.end);
        }
    }
    return (int) r32(root.value + 36);
}

/* A 16-bit resource record at AT, N bytes (a VxD's, which the LE header
   places): type 16, name 1, flags, the data's size, then the version
   resource. Returns its file type, or -1. */
static int
check_version_record(size_t at, uint32_t n)
{
    if ((n < 12) || !in_file(at, n)) {
        error("the version resource at %lX (%u bytes) is not inside the file", (unsigned long) at, n);
        return -1;
    }
    if ((r8(at) != 0xff) || (r16(at + 1) != 16) || (r8(at + 3) != 0xff) || (r16(at + 4) != 1)) {
        error("the version resource at %lX is not of type 16 and name 1", (unsigned long) at);
        return -1;
    }
    if (r32(at + 8) != n - 12) {
        error("the version resource's size %u does not match its length %u less its 12-byte header", r32(at + 8), n);
        return -1;
    }
    out("  version resource record at %06lX, %u bytes, flags %04X\n", (unsigned long) at, n, r16(at + 6));
    return check_version(at + 12, n - 12, 1);
}

/* The resource directory at RVA: the version resource (type 16), checked if
   there is one. 1 if there is. */
static int
check_resources(uint32_t rva, uint32_t n)
{
    size_t   root = pe_offset(rva, 16, "the resource directory");
    uint32_t count, k;

    if (!root)
        return 0;
    count = r16(root + 12) + r16(root + 14);
    for (k = 0; k < count; k++) {
        const size_t e = root + 16 + 8 * k;
        uint32_t     off, level;
        size_t       dir;

        if (r32(e) != 16)
            continue;
        /* Down the first entry of each level to the data entry. */
        off = r32(e + 4);
        for (level = 0; level < 2; level++) {
            if (!(off & 0x80000000u) || ((off & 0x7fffffffu) + 24 > n)) {
                error("the version resource's directory is malformed");
                return 1;
            }
            dir = pe_offset(rva + (off & 0x7fffffffu), 24, "a resource directory");
            if (!dir || (r16(dir + 12) + r16(dir + 14) == 0)) {
                error("the version resource's directory is malformed");
                return 1;
            }
            off = r32(dir + 16 + 4);
        }
        if ((off & 0x80000000u) || (off + 16 > n)) {
            error("the version resource's data entry is malformed");
            return 1;
        }
        {
            const size_t   d    = pe_offset(rva + off, 16, "the version resource's data entry");
            const uint32_t drva = r32(d), dlen = r32(d + 4);
            const size_t   data = dlen ? pe_offset(drva, dlen, "the version resource") : 0;

            if (data)
                check_version(data, dlen, 0);
            else
                error("the version resource is empty");
        }
        return 1;
    }
    return 0;
}

static void
check_pe(size_t pe, int require_driver, int require_glide)
{
    uint32_t nsecs, opt_size, chars, base, salign, falign, image_size, headers, entry, i;
    size_t   opt, sec_at;
    uint32_t exp_rva, imp_rva, rel_rva, rel_size, res_rva, res_size;
    char   **have_exports   = NULL;
    size_t   n_have_exports = 0;

    pe_secs  = NULL;
    pe_nsecs = 0;
    if (r16(pe + 4) != 0x014c)
        error("machine %04X is not i386", r16(pe + 4));
    nsecs    = r16(pe + 6);
    opt_size = r16(pe + 20);
    chars    = r16(pe + 22);
    opt      = pe + 24;
    if ((opt_size < 0xe0) || (r16(opt) != 0x010b)) {
        error("not a PE32 optional header");
        return;
    }
    base       = r32(opt + 28);
    salign     = r32(opt + 32);
    falign     = r32(opt + 36);
    image_size = r32(opt + 56);
    headers    = r32(opt + 60);
    entry      = r32(opt + 16);
    exp_rva    = r32(opt + 96);
    imp_rva    = r32(opt + 104);
    res_rva    = r32(opt + 112);
    res_size   = r32(opt + 116);
    rel_rva    = r32(opt + 136);
    rel_size   = r32(opt + 140);
    out("PE: image base %08X, %u sections, entry %08X, characteristics %04X, subsystem %u.%u\n", base, nsecs,
        entry, chars, r16(opt + 48), r16(opt + 50));
    if ((salign != 0x1000) || (falign < 0x200) || (falign > salign) || (falign & (falign - 1)))
        error("section alignment %X, file alignment %X", salign, falign);
    if (base & 0xffff)
        error("image base %08X is not a multiple of 64 KB", base);
    if ((r16(opt + 40) != 4) || (r16(opt + 48) != 4))
        strict_error("OS and subsystem version should be 4.0 for Windows 98");

    sec_at  = opt + opt_size;
    pe_secs = xcalloc(nsecs + 1, sizeof(*pe_secs));
    for (i = 0; i < nsecs; i++) {
        size_t    h = sec_at + 40 * i;
        pe_sec_t *s = &pe_secs[i];

        if (!in_file(h, 40)) {
            error("section table runs past the end of the file");
            break;
        }
        memcpy(s->name, file + h, 8);
        s->name[8] = 0;
        s->vsize   = r32(h + 8);
        s->rva     = r32(h + 12);
        s->raw     = r32(h + 16);
        s->raw_at  = r32(h + 20);
        s->flags   = r32(h + 36);
        pe_nsecs++;
        out("  section %-8s RVA %08X size %6X, file %6X at %6X, flags %08X\n", s->name, s->rva, s->vsize, s->raw,
            s->raw_at, s->flags);
        if (s->raw && (!in_file(s->raw_at, s->raw) || (s->raw_at % falign) || (s->raw_at < headers)))
            error("section %s's file data (%X bytes at %X) is misplaced", s->name, s->raw, s->raw_at);
        if (s->rva % salign)
            error("section %s's address %X is not aligned", s->name, s->rva);
        if (i && (s->rva < pe_secs[i - 1].rva + pe_secs[i - 1].vsize))
            error("section %s overlaps the one before", s->name);
        if (s->rva + s->vsize > image_size)
            error("section %s ends past the image size %X", s->name, image_size);
        if (require_driver && !(s->flags & 0x10000000))
            error("section %s is not shared: Windows 98 must load one copy for every process", s->name);
        if (require_glide && (s->flags & 0x10000000))
            error("section %s is shared: %s's data belongs to its process", s->name, own_dll);
    }
    if (entry && !pe_inside(entry, 1))
        error("entry point %X is outside the sections", entry);

    if (exp_rva && pe_offset(exp_rva, 40, "the export directory")) {
        size_t   d     = pe_offset(exp_rva, 40, "the export directory");
        uint32_t nfunc = r32(d + 20), nname = r32(d + 24), funcs = r32(d + 28), names = r32(d + 32);
        uint32_t ords  = r32(d + 36), k;
        char     prev[256] = "", name[256];
        int      have_init = 0;

        pe_string(r32(d + 12), name, sizeof(name), "the export DLL name");
        out("  exports of %s: %u functions, %u names\n", name, nfunc, nname);
        /* Each name takes a pointer and an ordinal in the file: no more of
           them than it could hold. */
        if (nname > size / 6) {
            error("the export directory claims %u names, more than the file could hold", nname);
            nname = 0;
        }
        for (k = 0; k < nname; k++) {
            size_t   na  = pe_offset(names + 4 * k, 4, "an export name pointer");
            size_t   oa  = pe_offset(ords + 2 * k, 2, "an export ordinal");
            uint32_t idx = r16(oa);
            size_t   fa;

            pe_string(r32(na), name, sizeof(name), "an export name");
            if (idx >= nfunc) {
                error("export %s names function %u of %u", name, idx, nfunc);
                continue;
            }
            fa = pe_offset(funcs + 4 * idx, 4, "an export address");
            out("    %s -> %08X\n", name, r32(fa));
            if (!pe_inside(r32(fa), 1))
                error("export %s points outside the sections", name);
            if (k && (strcmp(prev, name) >= 0))
                error("export names are not sorted (%s after %s)", name, prev);
            strcpy(prev, name);
            if (strcmp(name, "DriverInit") == 0)
                have_init = 1;
            have_exports = xrealloc(have_exports, (n_have_exports + 1) * sizeof(*have_exports));
            have_exports[n_have_exports++] = xstrdup(name);
        }
        if (require_driver && !have_init)
            error("no DriverInit export");
    } else if (require_driver || require_glide)
        error("no export directory");
    if (exports_path)
        compare_exports(have_exports, n_have_exports);
    for (i = 0; i < n_have_exports; i++)
        free(have_exports[i]);
    free(have_exports);

    if (imp_rva) {
        uint32_t d;

        for (d = 0;; d++) {
            size_t   at = pe_offset(imp_rva + 20 * d, 20, "an import descriptor");
            uint32_t ilt, nm, iat, k;
            char     dll[256];

            if (!at || (r32(at + 12) == 0))
                break;
            ilt = r32(at);
            nm  = r32(at + 12);
            iat = r32(at + 16);
            pe_string(nm, dll, sizeof(dll), "an imported DLL's name");
            out("  imports from %s\n", dll);
            if (require_driver && !dll_is(dll, "KERNEL32.DLL"))
                error("imports from %s: the driver DLL may import only from KERNEL32", dll);
            if (require_glide && !is_glide_import(dll))
                error("imports from %s: %s imports only from Windows' own KERNEL32, USER32, GDI32, ADVAPI32 and DDRAW", dll,
                      own_dll);
            for (k = 0;; k++) {
                size_t   ia = pe_offset((ilt ? ilt : iat) + 4 * k, 4, "an import lookup entry");
                uint32_t v;
                char     fn[256];

                if (!ia)
                    break;
                v = r32(ia);
                if (v == 0)
                    break;
                if (v & 0x80000000u)
                    out("    ordinal %u\n", v & 0xffff);
                else {
                    pe_string(v + 2, fn, sizeof(fn), "an imported name");
                    out("    %s\n", fn);
                }
                if (!pe_inside(iat + 4 * k, 4))
                    error("import address slot %X is outside the sections", iat + 4 * k);
            }
        }
    }

    if (rel_rva) {
        uint32_t done = 0, count = 0;

        while (done + 8 <= rel_size) {
            size_t   at   = pe_offset(rel_rva + done, 8, "a relocation block");
            uint32_t page = r32(at), len = r32(at + 4), k;

            if ((len < 8) || (len & 3) || (done + len > rel_size)) {
                error("relocation block at %X has length %X", rel_rva + done, len);
                break;
            }
            for (k = 8; k < len; k += 2) {
                uint32_t e = r16(at + k);

                if ((e >> 12) == 0)
                    continue;
                if ((e >> 12) != 3)
                    error("relocation type %u at %X: only HIGHLOW is expected", e >> 12, page + (e & 0xfff));
                else if (!pe_inside(page + (e & 0xfff), 4))
                    error("relocation at %X is outside the sections", page + (e & 0xfff));
                count++;
            }
            done += len;
        }
        out("  %u base relocations\n", count);
    } else if (require_glide)
        error("no base relocations: the DLL cannot be moved if its base is taken (10000000h often is)");
    else if (require_driver)
        strict_error("no base relocations: the DLL cannot be moved if its base is taken");

    /* Every driver DLL of ours carries its version. */
    if (res_rva) {
        if (!check_resources(res_rva, res_size) && (require_glide || require_driver))
            error("no version resource");
    } else if (require_glide || require_driver)
        error("no version resource");

    if (require_driver) {
        if (!(chars & 0x2000))
            error("not marked as a DLL");
        if ((base < 0x80000000u) || (base >= 0xc0000000u))
            error("image base %08X is not in Windows 98's shared arena (80000000h-BFFFFFFFh)", base);
    }
    if (require_glide) {
        if (!(chars & 0x2000))
            error("not marked as a DLL");
        if ((base < 0x400000u) || (base >= 0x80000000u))
            error("image base %08X is not in a process's own space (400000h-7FFFFFFFh)", base);
    }
    free(pe_secs);
}

#define USAGE "usage: drvlint [--dump] [--strict] [--display | --vxd | --pe | --glide | --icd | --cpl] [--exports LIST] FILE..."

int
main(int argc, char **argv)
{
    int display = 0;
    int vxd     = 0;
    int pe      = 0;
    int glide   = 0;
    int i;
    int files = 0;

    set_progname("drvlint");
    for (i = 1; i < argc; i++) {
        size_t hdr;

        if (strcmp(argv[i], "--dump") == 0) {
            dump = 1;
            continue;
        }
        if (strcmp(argv[i], "--display") == 0) {
            display = 1;
            continue;
        }
        if (strcmp(argv[i], "--vxd") == 0) {
            vxd = 1;
            continue;
        }
        if (strcmp(argv[i], "--pe") == 0) {
            pe = 1;
            continue;
        }
        if (strcmp(argv[i], "--glide") == 0) {
            glide++;
            continue;
        }
        if (strcmp(argv[i], "--icd") == 0) {
            glide++;
            own_dll = "the OpenGL ICD";
            continue;
        }
        if (strcmp(argv[i], "--cpl") == 0) {
            glide++;
            own_dll   = "the control program";
            comctl_ok = 1;
            continue;
        }
        if ((strcmp(argv[i], "--exports") == 0) && (i + 1 < argc)) {
            load_exports(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--strict") == 0) {
            strict = 1;
            continue;
        }
        if ((argv[i][0] == '-') || (display + vxd + pe + glide > 1))
            die(USAGE);
        files++;
        path = argv[i];
        file = read_file(path, &size);
        out("%s: %lu bytes\n", path, (unsigned long) size);
        if ((size < 0x40) || (r16(0) != 0x5a4d)) {
            error("not an MZ executable");
            free(file);
            continue;
        }
        if (r16(0x18) < 0x40)
            error("relocation table offset %04X: not marked as a new-format executable", r16(0x18));
        hdr = r32(0x3c);
        if (!in_file(hdr, 0x40)) {
            error("new header offset %lX is outside the file", (unsigned long) hdr);
        } else if ((r8(hdr) == 'N') && (r8(hdr + 1) == 'E')) {
            if (vxd)
                error("expected an LE file");
            else if (pe || glide)
                error("expected a PE file");
            check_ne(hdr, display);
        } else if ((r8(hdr) == 'L') && (r8(hdr + 1) == 'E')) {
            if (display)
                error("expected an NE file");
            else if (pe || glide)
                error("expected a PE file");
            check_le(hdr, vxd);
        } else if (r32(hdr) == 0x00004550) {
            if (display || vxd)
                error("expected an %s file", display ? "NE" : "LE");
            check_pe(hdr, pe, glide);
        } else
            error("unknown new header '%c%c'", r8(hdr), r8(hdr + 1));
        if (exports_path && ((r8(hdr) != 'P') || (r8(hdr + 1) != 'E')))
            error("--exports checks a PE file's export table");
        free(file);
    }
    if (!files)
        die(USAGE);
    if (errors)
        fprintf(stderr, "drvlint: %d error%s\n", errors, errors == 1 ? "" : "s");
    return errors ? 1 : 0;
}
