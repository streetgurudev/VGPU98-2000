/*
 * vglink: links NASM's OMF objects into an NE display driver or an LE VxD,
 * and pe-i386 COFF objects into a PE DLL (pe.c).
 *
 *   vglink -d DEF -o OUT [-m MAP] [-r RESOURCE_DIR] OBJ...
 *
 * The link definition (DEF) names the module, places the objects' segments in
 * output segments (NE) or objects (LE), and lists exports, imports (by
 * ordinal), resources and version information. Format: spec/vglink.md.
 * The output depends only on the inputs: no timestamps, no paths.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "vglink.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "omf.h"

/* --- Buffer helpers ------------------------------------------------------ */

void
put8(buf_t *b, uint32_t v)
{
    uint8_t c = (uint8_t) v;

    buf_append(b, &c, 1);
}

void
put16(buf_t *b, uint32_t v)
{
    put8(b, v);
    put8(b, v >> 8);
}

void
put32(buf_t *b, uint32_t v)
{
    put16(b, v);
    put16(b, v >> 16);
}

void
set16(buf_t *b, size_t at, uint32_t v)
{
    b->data[at]     = (char) (v & 0xff);
    b->data[at + 1] = (char) ((v >> 8) & 0xff);
}

void
set32(buf_t *b, size_t at, uint32_t v)
{
    set16(b, at, v & 0xffff);
    set16(b, at + 2, v >> 16);
}

void
pad_to(buf_t *b, size_t align)
{
    while (b->len % align)
        put8(b, 0);
}

/* A DOS program that prints a line and exits: what runs if someone starts the
   file from DOS. */
size_t
write_stub(buf_t *out)
{
    static const char msg[]  = "This is a Windows 98 driver for the VGPU98 card.\r\n$";
    static const uint8_t code[] = {
        0x0e,             /* push cs */
        0x1f,             /* pop ds */
        0xba, 0x0e, 0x00, /* mov dx, msg */
        0xb4, 0x09,       /* mov ah, 9 */
        0xcd, 0x21,       /* int 21h */
        0xb8, 0x01, 0x4c, /* mov ax, 4C01h */
        0xcd, 0x21        /* int 21h */
    };
    size_t size = 0x40 + sizeof(code) + sizeof(msg) - 1;
    size_t total;

    size  = (size + 15) & ~(size_t) 15;
    total = size;
    put16(out, 0x5a4d);              /* MZ */
    put16(out, total % 512);         /* bytes in the last page */
    put16(out, (total + 511) / 512); /* pages */
    put16(out, 0);                   /* relocations */
    put16(out, 4);                   /* header paragraphs */
    put16(out, 0x10);                /* minimum extra paragraphs: the stack */
    put16(out, 0xffff);              /* maximum extra paragraphs */
    put16(out, 0);                   /* SS */
    put16(out, (uint32_t) (size - 0x40 + 0x100)); /* SP: above the code */
    put16(out, 0);                   /* checksum */
    put16(out, 0);                   /* IP */
    put16(out, 0);                   /* CS */
    put16(out, 0x40);                /* relocation table: 40h marks a new-format executable */
    put16(out, 0);                   /* overlay */
    while (out->len < 0x3c)
        put8(out, 0);
    put32(out, (uint32_t) total);    /* new header offset */
    buf_append(out, code, sizeof(code));
    buf_append(out, msg, sizeof(msg) - 1);
    pad_to(out, 16);
    return total;
}

/* --- Inputs ------------------------------------------------------------- */

static cfg_t   *def;
static image_t  img;
static omf_t  **objs;
static size_t   n_objs;
static uint32_t **seg_out;  /* [object][segment]: 0-based output segment */
static uint32_t **seg_base; /* [object][segment]: offset in the output segment */

typedef struct symbol_t {
    char    *name;
    int      is_import;
    uint32_t out;    /* 0-based output segment */
    uint32_t offset;
    uint32_t import; /* index into img.imports */
    const char *where;
} symbol_t;

static symbol_t *syms;
static size_t    n_syms;

static int
sym_cmp(const void *a, const void *b)
{
    return strcmp(((const symbol_t *) a)->name, ((const symbol_t *) b)->name);
}

static symbol_t *
find_sym(const char *name)
{
    symbol_t key;

    key.name = (char *) name;
    return bsearch(&key, syms, n_syms, sizeof(*syms), sym_cmp);
}

static void
add_sym(const symbol_t *s)
{
    syms           = xrealloc(syms, (n_syms + 1) * sizeof(*syms));
    syms[n_syms++] = *s;
}

static char *
upper(const char *s)
{
    char  *u = xstrdup(s);
    size_t i;

    for (i = 0; u[i]; i++)
        u[i] = (char) toupper((unsigned char) u[i]);
    return u;
}

/* Splits a value into words; the words live in one copy of the value, never freed. */
static size_t
words(const char *value, char ***out)
{
    char  *copy = xstrdup(value);
    char  *p    = copy;
    size_t n    = 0;

    *out = NULL;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (!*p)
            break;
        *out           = xrealloc(*out, (n + 1) * sizeof(**out));
        (*out)[n++]    = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    return n;
}

/* --- Definition file ----------------------------------------------------- */

/* An identifier (symbol, segment, module or file name): one word of letters,
   digits and _ . $ ? @ -, in any case. */
static const char *
get_ident(cfg_section_t *sec, const char *key)
{
    const char *v = cfg_value(sec, key);
    const char *p;

    if (v == NULL)
        cfg_error(def, sec->line, "[%s] needs '%s'", sec->name, key);
    for (p = v; *p; p++) {
        if (!isalnum((unsigned char) *p) && !strchr("_.$?@-", *p))
            cfg_error(def, cfg_line(sec, key), "'%s' must be one name (letters, digits, _ . $ ? @ -)", key);
    }
    return v;
}

static uint32_t
opt_u32(cfg_section_t *sec, const char *key, uint32_t dflt, uint32_t max)
{
    return cfg_has(sec, key) ? cfg_get_u32(def, sec, key, 0, max) : dflt;
}

static void
read_segments_key(cfg_section_t *sec, oseg_t *s)
{
    if (cfg_has(sec, "segments")) {
        char **w;
        size_t n = words(cfg_value(sec, "segments"), &w);

        s->members   = w;
        s->n_members = n;
    } else {
        s->members    = xmalloc(sizeof(char *));
        s->members[0] = xstrdup(s->name);
        s->n_members  = 1;
    }
}

static uint32_t
seg_by_name(const char *name, int line)
{
    size_t i;

    for (i = 0; i < img.n_segs; i++) {
        if (strcmp(img.segs[i].name, name) == 0)
            return (uint32_t) i;
    }
    cfg_error(def, line, "no output segment '%s'", name);
}

static void
read_version(cfg_section_t *sec);

static void
read_resource(cfg_section_t *sec, const char *res_dir)
{
    resource_t r;
    char     **w;
    char       path[2048];
    uint32_t   v;

    memset(&r, 0, sizeof(r));
    if ((sec->arg == NULL) || (words(sec->arg, &w) != 2))
        cfg_error(def, sec->line, "expected [resource TYPE ID]");
    if (parse_u32(w[0], &v)) {
        if ((v == 0) || (v > 0x7fff))
            cfg_error(def, sec->line, "resource type %u out of range", v);
        r.type_id = (uint16_t) v;
    } else
        r.type_name = upper(w[0]);
    if (parse_u32(w[1], &v)) {
        if ((v == 0) || (v > 0x7fff))
            cfg_error(def, sec->line, "resource id %u out of range", v);
        r.id = (uint16_t) v;
    } else
        r.id_name = upper(w[1]);
    snprintf(path, sizeof(path), "%s/%s", res_dir, get_ident(sec, "file"));
    r.data    = read_file(path, &r.len);
    r.preload = (int) opt_u32(sec, "preload", 1, 1);
    img.res   = xrealloc(img.res, (img.n_res + 1) * sizeof(*img.res));
    img.res[img.n_res++] = r;
}

static void
read_definition(const char *path, const char *res_dir)
{
    cfg_section_t *mod;
    const char    *fmt;
    int            i;

    def = cfg_load(path);
    mod = cfg_section(def, "module");
    fmt = cfg_get_word(def, mod, "format");
    if (strcmp(fmt, "ne") == 0)
        img.format = FORMAT_NE;
    else if (strcmp(fmt, "le") == 0)
        img.format = FORMAT_LE;
    else
        cfg_error(def, cfg_line(mod, "format"), "format must be ne, le or pe");
    img.name        = upper(get_ident(mod, "name"));
    img.description = cfg_get_str(def, mod, "description", 255);
    if (strlen(img.name) > 8)
        cfg_error(def, cfg_line(mod, "name"), "module names have at most 8 characters");

    /* Output segments (NE) or objects (LE), in file order. */
    for (i = 0; i < def->n_sections; i++) {
        cfg_section_t *sec = &def->sections[i];
        int            want_ne = strcmp(sec->name, "segment") == 0;
        int            want_le = strcmp(sec->name, "object") == 0;
        oseg_t         s;

        if (!want_ne && !want_le)
            continue;
        sec->used = 1;
        if (want_ne != (img.format == FORMAT_NE))
            cfg_error(def, sec->line, "[%s] does not belong in a %s link", sec->name, fmt);
        if ((sec->arg == NULL) || strchr(sec->arg, ' '))
            cfg_error(def, sec->line, "expected [%s NAME]", sec->name);
        memset(&s, 0, sizeof(s));
        s.name = xstrdup(sec->arg);
        s.line = sec->line;
        read_segments_key(sec, &s);
        s.preload = (int) opt_u32(sec, "preload", 1, 1);
        if (want_ne) {
            const char *type = cfg_get_word(def, sec, "type");

            if (strcmp(type, "code") == 0)
                s.is_data = 0;
            else if (strcmp(type, "data") == 0)
                s.is_data = 1;
            else
                cfg_error(def, cfg_line(sec, "type"), "type must be code or data");
        } else {
            char **w;
            size_t n = cfg_has(sec, "flags") ? words(cfg_value(sec, "flags"), &w) : 0;
            size_t k;

            if (strlen(s.name) != 4)
                cfg_error(def, sec->line, "LE object names are 4 characters (LCOD, ICOD, ...)");
            s.le_flags = 0x2005; /* readable, executable, 32-bit */
            s.preload  = 0;
            for (k = 0; k < n; k++) {
                if (strcmp(w[k], "preload") == 0) {
                    s.le_flags |= 0x0040;
                    s.preload = 1;
                } else if (strcmp(w[k], "discardable") == 0)
                    s.le_flags |= 0x0010;
                else if (strcmp(w[k], "writable") == 0)
                    s.le_flags |= 0x0002;
                else if (strcmp(w[k], "shared") == 0)
                    s.le_flags |= 0x0020;
                else
                    cfg_error(def, cfg_line(sec, "flags"), "unknown object flag '%s'", w[k]);
            }
            if (cfg_has(sec, "preload"))
                cfg_error(def, cfg_line(sec, "preload"), "LE objects take 'flags = preload'");
        }
        img.segs = xrealloc(img.segs, (img.n_segs + 1) * sizeof(*img.segs));
        img.segs[img.n_segs++] = s;
    }
    if (img.n_segs == 0)
        cfg_error(def, 0, "no output %s", img.format == FORMAT_NE ? "segments" : "objects");

    if (img.format == FORMAT_NE) {
        img.heap   = opt_u32(mod, "heap", 0, 0xffff);
        img.expver = opt_u32(mod, "windows", 0x400, 0xffff);
        if (cfg_has(mod, "data")) {
            img.autodata = seg_by_name(get_ident(mod, "data"), cfg_line(mod, "data")) + 1;
            if (!img.segs[img.autodata - 1].is_data)
                cfg_error(def, cfg_line(mod, "data"), "the automatic data segment must be a data segment");
        }

        /* Imports: [imports MODULE], ordinal = symbol. */
        for (i = 0; i < def->n_sections; i++) {
            cfg_section_t *sec = &def->sections[i];
            int            k;

            if (strcmp(sec->name, "imports") != 0)
                continue;
            sec->used = 1;
            if ((sec->arg == NULL) || strchr(sec->arg, ' ') || (strlen(sec->arg) > 8))
                cfg_error(def, sec->line, "expected [imports MODULE] with a module name of at most 8 characters");
            img.modules = xrealloc(img.modules, (img.n_modules + 1) * sizeof(*img.modules));
            img.modules[img.n_modules++] = upper(sec->arg);
            for (k = 0; k < sec->n_entries; k++) {
                cfg_entry_t *e = &sec->entries[k];
                import_t     im;
                uint32_t     ord;

                e->used = 1;
                if (!parse_u32(e->key, &ord) || (ord == 0) || (ord > 0xffff))
                    cfg_error(def, e->line, "imports are 'ordinal = symbol'");
                if (strchr(e->value, ' '))
                    cfg_error(def, e->line, "one symbol per import");
                memset(&im, 0, sizeof(im));
                im.symbol  = xstrdup(e->value);
                im.module  = (uint16_t) img.n_modules;
                im.ordinal = (uint16_t) ord;
                img.imports = xrealloc(img.imports, (img.n_imports + 1) * sizeof(*img.imports));
                img.imports[img.n_imports++] = im;
            }
        }

        /* Exports: [exports], ordinal = symbol [resident] [nodata]. */
        for (i = 0; i < def->n_sections; i++) {
            cfg_section_t *sec = &def->sections[i];
            int            k;

            if (strcmp(sec->name, "exports") != 0)
                continue;
            sec->used = 1;
            for (k = 0; k < sec->n_entries; k++) {
                cfg_entry_t *e = &sec->entries[k];
                export_t     ex;
                char       **w;
                size_t       n = words(e->value, &w);
                size_t       j;
                uint32_t     ord;

                e->used = 1;
                if (!parse_u32(e->key, &ord) || (ord == 0) || (ord > 0x7fff))
                    cfg_error(def, e->line, "exports are 'ordinal = symbol [resident] [nodata]'");
                memset(&ex, 0, sizeof(ex));
                ex.symbol  = xstrdup(w[0]);
                ex.name    = upper(w[0]);
                ex.ordinal = (uint16_t) ord;
                ex.line    = e->line;
                for (j = 1; j < n; j++) {
                    if (strcmp(w[j], "resident") == 0)
                        ex.resident = 1;
                    else if (strcmp(w[j], "nodata") == 0)
                        ex.nodata = 1;
                    else
                        cfg_error(def, e->line, "unknown export option '%s'", w[j]);
                }
                img.exports = xrealloc(img.exports, (img.n_exports + 1) * sizeof(*img.exports));
                img.exports[img.n_exports++] = ex;
            }
        }

        for (i = 0; i < def->n_sections; i++) {
            cfg_section_t *sec = &def->sections[i];

            if (strcmp(sec->name, "resource") == 0) {
                sec->used = 1;
                read_resource(sec, res_dir);
            } else if (strcmp(sec->name, "version") == 0) {
                sec->used = 1;
                read_version(sec);
            }
        }
    } else {
        img.ddb_name    = xstrdup(get_ident(mod, "ddb"));
        img.device_id   = opt_u32(mod, "device_id", 0, 0xffff);
        img.ddk_version = opt_u32(mod, "ddk_version", 0, 0xffff);

        /* A VxD's version information, the one resource it carries. */
        for (i = 0; i < def->n_sections; i++) {
            cfg_section_t *sec = &def->sections[i];

            if (strcmp(sec->name, "version") == 0) {
                sec->used = 1;
                read_version(sec);
            }
        }
    }
}

/* --- Version information (16-bit VS_VERSIONINFO, an RT_VERSION resource) --- */

/* Starts a block: length and value length are filled in by end_block. */
static size_t
begin_block(buf_t *b, const char *key)
{
    size_t at = b->len;

    put16(b, 0);
    put16(b, 0);
    buf_append(b, key, strlen(key) + 1);
    pad_to(b, 4);
    return at;
}

static void
end_block(buf_t *b, size_t at)
{
    set16(b, at, (uint32_t) (b->len - at));
}

static void
string_block(buf_t *b, const char *key, const char *value)
{
    size_t at = begin_block(b, key);

    buf_append(b, value, strlen(value) + 1);
    set16(b, at + 2, (uint32_t) strlen(value) + 1);
    pad_to(b, 4);
    end_block(b, at);
}

static void
parse_version(cfg_section_t *sec, const char *key, uint32_t *ms, uint32_t *ls)
{
    const char *v = get_ident(sec, key);
    unsigned    a, b, c, d;
    char        tail;

    if ((sscanf(v, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) || (a > 0xffff) || (b > 0xffff) ||
        (c > 0xffff) || (d > 0xffff))
        cfg_error(def, cfg_line(sec, key), "'%s' must be four numbers, as in 4.10.0.1", key);
    *ms = (a << 16) | b;
    *ls = (c << 16) | d;
}

static void
read_version(cfg_section_t *sec)
{
    static const struct {
        const char *key;
        const char *name;
    } strings[] = { { "company", "CompanyName" },           { "description", "FileDescription" },
                    { "file_version_text", "FileVersion" }, { "internal_name", "InternalName" },
                    { "copyright", "LegalCopyright" },      { "original_name", "OriginalFilename" },
                    { "product_name", "ProductName" },      { "product_version_text", "ProductVersion" } };
    buf_t      b;
    resource_t r;
    uint32_t   fms, fls, pms, pls;
    size_t     root, sfi, table, vfi, var;
    size_t     i;
    uint32_t   type, subtype;

    for (i = 0; i < img.n_res; i++) {
        if (!img.res[i].type_name && (img.res[i].type_id == 16))
            cfg_error(def, sec->line, "[version] repeated");
    }
    memset(&b, 0, sizeof(b));
    parse_version(sec, "file_version", &fms, &fls);
    parse_version(sec, "product_version", &pms, &pls);
    type    = cfg_get_u32(def, sec, "file_type", 1, 7);
    subtype = opt_u32(sec, "file_subtype", 0, 0xffff);
    /* Windows' own VxDs are VFT_VXD, their subtype the device ID. */
    if ((img.format == FORMAT_LE) && (type != 5))
        cfg_error(def, cfg_line(sec, "file_type"), "a VxD's file_type is 5 (VFT_VXD)");

    root = begin_block(&b, "VS_VERSION_INFO");
    set16(&b, root + 2, 52);
    put32(&b, 0xfeef04bd); /* signature */
    put32(&b, 0x00010000); /* structure version */
    put32(&b, fms);
    put32(&b, fls);
    put32(&b, pms);
    put32(&b, pls);
    put32(&b, 0x3f);       /* flags mask */
    put32(&b, 0);          /* flags */
    put32(&b, 0x00010001); /* VOS_DOS_WINDOWS16 */
    put32(&b, type);
    put32(&b, subtype);
    put32(&b, 0);          /* date */
    put32(&b, 0);

    sfi   = begin_block(&b, "StringFileInfo");
    table = begin_block(&b, "040904E4"); /* U.S. English, Windows Latin-1 */
    for (i = 0; i < sizeof(strings) / sizeof(strings[0]); i++)
        string_block(&b, strings[i].name, cfg_get_str(def, sec, strings[i].key, 200));
    end_block(&b, table);
    end_block(&b, sfi);

    vfi = begin_block(&b, "VarFileInfo");
    var = begin_block(&b, "Translation");
    set16(&b, var + 2, 4);
    put16(&b, 0x0409);
    put16(&b, 0x04e4);
    end_block(&b, var);
    end_block(&b, vfi);
    end_block(&b, root);

    memset(&r, 0, sizeof(r));
    r.type_id = 16; /* RT_VERSION */
    r.id      = 1;
    r.data    = (uint8_t *) b.data;
    r.len     = b.len;
    img.res   = xrealloc(img.res, (img.n_res + 1) * sizeof(*img.res));
    img.res[img.n_res++] = r;
}

/* --- Layout and symbols -------------------------------------------------- */

static void
place_segments(void)
{
    size_t o, i, k, m;

    seg_out  = xcalloc(n_objs, sizeof(*seg_out));
    seg_base = xcalloc(n_objs, sizeof(*seg_base));
    for (o = 0; o < n_objs; o++) {
        seg_out[o]  = xmalloc((objs[o]->n_segs + 1) * sizeof(uint32_t));
        seg_base[o] = xcalloc(objs[o]->n_segs + 1, sizeof(uint32_t));
        for (k = 0; k < objs[o]->n_segs; k++)
            seg_out[o][k] = UINT32_MAX;
    }

    for (i = 0; i < img.n_segs; i++) {
        oseg_t  *s    = &img.segs[i];
        uint64_t size = 0;

        for (m = 0; m < s->n_members; m++) {
            int found = 0;

            for (o = 0; o < n_objs; o++) {
                for (k = 0; k < objs[o]->n_segs; k++) {
                    omf_seg_t *os = &objs[o]->segs[k];

                    if (strcmp(os->name, s->members[m]) != 0)
                        continue;
                    if (seg_out[o][k] != UINT32_MAX)
                        cfg_error(def, s->line, "segment %s is placed twice", os->name);
                    if ((img.format == FORMAT_LE) != (os->use32 != 0))
                        die("%s: segment %s is %d-bit; %s output takes %d-bit segments", objs[o]->path, os->name,
                            os->use32 ? 32 : 16, img.format == FORMAT_LE ? "LE" : "NE",
                            img.format == FORMAT_LE ? 32 : 16);
                    size           = (size + os->align - 1) / os->align * os->align;
                    seg_out[o][k]  = (uint32_t) i;
                    seg_base[o][k] = (uint32_t) size;
                    size += os->length;
                    found = 1;
                }
            }
            if (!found)
                cfg_error(def, s->line, "no object has a segment %s", s->members[m]);
        }
        if ((img.format == FORMAT_NE) && (size > 0x10000))
            cfg_error(def, s->line, "segment %s is %llu bytes; NE segments hold at most 64 KB", s->name,
                      (unsigned long long) size);
        if (size > 0x1000000)
            cfg_error(def, s->line, "object %s is larger than 16 MB", s->name);
        s->size = (uint32_t) size;
        s->data = xcalloc(1, size ? size : 1);
    }

    for (o = 0; o < n_objs; o++) {
        for (k = 0; k < objs[o]->n_segs; k++) {
            omf_seg_t *os = &objs[o]->segs[k];

            if (seg_out[o][k] == UINT32_MAX)
                die("%s: segment %s is not placed by the link definition", objs[o]->path, os->name);
            memcpy(img.segs[seg_out[o][k]].data + seg_base[o][k], os->data, os->length);
        }
    }
}

static void
collect_symbols(void)
{
    size_t o, k;

    for (o = 0; o < n_objs; o++) {
        for (k = 0; k < objs[o]->n_pubs; k++) {
            const omf_pub_t *p = &objs[o]->pubs[k];
            symbol_t         s;

            memset(&s, 0, sizeof(s));
            s.name   = p->name;
            s.out    = seg_out[o][p->seg];
            s.offset = seg_base[o][p->seg] + p->offset;
            s.where  = objs[o]->path;
            add_sym(&s);
        }
    }
    for (k = 0; k < img.n_imports; k++) {
        symbol_t s;

        memset(&s, 0, sizeof(s));
        s.name      = img.imports[k].symbol;
        s.is_import = 1;
        s.import    = (uint32_t) k;
        s.where     = def->path;
        add_sym(&s);
    }
    qsort(syms, n_syms, sizeof(*syms), sym_cmp);
    for (k = 1; k < n_syms; k++) {
        if (strcmp(syms[k - 1].name, syms[k].name) == 0)
            die("symbol %s is defined twice (%s and %s)", syms[k].name, syms[k - 1].where, syms[k].where);
    }
}

/* --- Fixups -------------------------------------------------------------- */

typedef struct target_t {
    int      is_import;
    uint32_t out;    /* 0-based */
    uint32_t offset;
    uint32_t import; /* index into img.imports */
} target_t;

VGPU98_NORETURN static void fix_error(size_t o, const omf_seg_t *os, const omf_fixup_t *f, const char *fmt, ...)
    VGPU98_PRINTF(4, 5);

static void
fix_error(size_t o, const omf_seg_t *os, const omf_fixup_t *f, const char *fmt, ...)
{
    char    msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    die("%s: fixup at %s:%04X (record %d): %s", objs[o]->path, os->name, f->offset, f->line, msg);
}

static target_t
resolve(size_t o, const omf_seg_t *os, const omf_fixup_t *f, omf_ref_t ref)
{
    target_t t;

    memset(&t, 0, sizeof(t));
    switch (ref.kind) {
        case REF_SEGMENT:
            t.out    = seg_out[o][ref.index];
            t.offset = seg_base[o][ref.index];
            break;
        case REF_EXTERN: {
            const char     *name = objs[o]->externs[ref.index];
            const symbol_t *s    = find_sym(name);

            if (s == NULL)
                fix_error(o, os, f, "undefined symbol %s", name);
            if (s->is_import) {
                t.is_import = 1;
                t.import    = s->import;
                img.imports[s->import].used = 1;
            } else {
                t.out    = s->out;
                t.offset = s->offset;
            }
            break;
        }
        default:
            fix_error(o, os, f, "fixups to groups are not supported");
    }
    return t;
}

static uint32_t
get_le(const uint8_t *p, int bytes)
{
    uint32_t v = 0;
    int      i;

    for (i = bytes - 1; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static void
put_le(uint8_t *p, uint32_t v, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++)
        p[i] = (uint8_t) (v >> (8 * i));
}

static void
add_reloc(oseg_t *s, uint32_t at, uint8_t src, uint8_t flags, uint16_t a, uint16_t b)
{
    ne_reloc_t r;

    r.offset = (uint16_t) at;
    r.src    = src;
    r.flags  = flags;
    r.a      = a;
    r.b      = b;
    s->relocs = xrealloc(s->relocs, (s->n_relocs + 1) * sizeof(*s->relocs));
    s->relocs[s->n_relocs++] = r;
}

static void
add_fixup(oseg_t *s, uint32_t at, uint8_t src, uint32_t object, uint32_t target)
{
    le_fixup_t x;

    x.offset = at;
    x.src    = src;
    x.object = object;
    x.target = target;
    s->fixups = xrealloc(s->fixups, (s->n_fixups + 1) * sizeof(*s->fixups));
    s->fixups[s->n_fixups++] = x;
}

/* NE: offsets are relative to the output segment, so the frame must be the
   segment the target ends up in. LE: flat, every frame is the same. */
static void
check_frame(size_t o, const omf_seg_t *os, const omf_fixup_t *f, const target_t *t, uint32_t loc_out)
{
    target_t fr;

    if ((img.format == FORMAT_LE) || t->is_import || (f->frame.kind == -1))
        return;
    if (f->frame.kind == -2) {
        if (loc_out != t->out)
            fix_error(o, os, f, "frame of the location, but the target is in another segment");
        return;
    }
    if (f->frame.kind == REF_GROUP)
        fix_error(o, os, f, "group frames are not supported in NE output");
    fr = resolve(o, os, f, f->frame);
    if (fr.is_import || (fr.out != t->out))
        fix_error(o, os, f, "frame is not the target's segment");
}

static void
apply_ne(size_t o, const omf_seg_t *os, const omf_fixup_t *f)
{
    uint32_t  out  = seg_out[o][os - objs[o]->segs];
    oseg_t   *s    = &img.segs[out];
    uint32_t  at   = seg_base[o][os - objs[o]->segs] + f->offset;
    uint8_t  *p    = s->data + at;
    int       loc  = (f->loc == LOC_LOFF16) ? LOC_OFFSET16 : (f->loc == LOC_LOFF32) ? LOC_OFFSET32 : f->loc;
    int       size = (loc == LOC_OFFSET32 || loc == LOC_PTR1616) ? 4 : 2;
    target_t  t    = resolve(o, os, f, f->target);
    uint32_t  inl;

    if ((loc != LOC_OFFSET16) && (loc != LOC_BASE) && (loc != LOC_PTR1616) && (loc != LOC_OFFSET32))
        fix_error(o, os, f, "location type %d is not supported in NE output", f->loc);
    if (at + (uint32_t) size > s->size)
        fix_error(o, os, f, "location runs past the end of the segment");
    inl = get_le(p, loc == LOC_PTR1616 ? 2 : size) + (uint32_t) f->disp;
    check_frame(o, os, f, &t, out);

    if (t.is_import) {
        const import_t *im = &img.imports[t.import];

        if (f->self_relative)
            fix_error(o, os, f, "self-relative reference to an import");
        if (inl != 0)
            fix_error(o, os, f, "an import plus an offset is not supported");
        switch (loc) {
            case LOC_OFFSET16:
                add_reloc(s, at, NE_SRC_OFFSET, NE_REL_ORDINAL, im->module, im->ordinal);
                put_le(p, 0xffff, 2); /* end of the relocation chain */
                break;
            case LOC_BASE:
                add_reloc(s, at, NE_SRC_SEGMENT, NE_REL_ORDINAL, im->module, im->ordinal);
                put_le(p, 0xffff, 2);
                break;
            case LOC_PTR1616:
                add_reloc(s, at, NE_SRC_FARADDR, NE_REL_ORDINAL, im->module, im->ordinal);
                put_le(p, 0xffff, 2);
                put_le(p + 2, 0, 2);
                break;
            default:
                fix_error(o, os, f, "a 32-bit offset to an import; use 'dw sym, seg sym' for a far pointer");
        }
        return;
    }

    if (f->self_relative) {
        int32_t rel;

        if ((loc != LOC_OFFSET16) && (loc != LOC_OFFSET32))
            fix_error(o, os, f, "self-relative fixup of location type %d", f->loc);
        if (t.out != out)
            fix_error(o, os, f, "near reference to another segment (%s)", img.segs[t.out].name);
        rel = (int32_t) (t.offset + inl - (at + (uint32_t) size));
        put_le(p, (uint32_t) rel, size);
        return;
    }

    switch (loc) {
        case LOC_OFFSET16:
            if (t.offset + inl > 0xffff)
                fix_error(o, os, f, "offset %X does not fit in 16 bits", t.offset + inl);
            put_le(p, t.offset + inl, 2);
            break;
        case LOC_OFFSET32:
            put_le(p, t.offset + inl, 4);
            break;
        case LOC_BASE:
            add_reloc(s, at, NE_SRC_SEGMENT, NE_REL_INTERNAL, (uint16_t) (t.out + 1), 0);
            put_le(p, 0xffff, 2);
            break;
        case LOC_PTR1616:
            put_le(p, t.offset + inl, 2);
            add_reloc(s, at + 2, NE_SRC_SEGMENT, NE_REL_INTERNAL, (uint16_t) (t.out + 1), 0);
            put_le(p + 2, 0xffff, 2);
            break;
        default:
            break;
    }
}

static void
apply_le(size_t o, const omf_seg_t *os, const omf_fixup_t *f)
{
    uint32_t out = seg_out[o][os - objs[o]->segs];
    oseg_t  *s   = &img.segs[out];
    uint32_t at  = seg_base[o][os - objs[o]->segs] + f->offset;
    uint8_t *p   = s->data + at;
    int      loc = (f->loc == LOC_LOFF32) ? LOC_OFFSET32 : f->loc;
    target_t t;
    uint32_t inl;

    if (loc != LOC_OFFSET32)
        fix_error(o, os, f, "location type %d: LE output takes 32-bit offsets only", f->loc);
    if (at + 4 > s->size)
        fix_error(o, os, f, "location runs past the end of the object");
    t = resolve(o, os, f, f->target);
    if (t.is_import)
        fix_error(o, os, f, "imports are not supported in LE output");
    inl = get_le(p, 4) + (uint32_t) f->disp;
    check_frame(o, os, f, &t, out);

    if (f->self_relative) {
        if (t.out == out) {
            put_le(p, t.offset + inl - (at + 4), 4);
            return;
        }
        add_fixup(s, at, LE_SRC_RELATIVE32, t.out + 1, t.offset + inl);
    } else
        add_fixup(s, at, LE_SRC_OFFSET32, t.out + 1, t.offset + inl);
    put_le(p, 0, 4); /* the loader writes the whole value */
}

static int
reloc_cmp(const void *a, const void *b)
{
    const ne_reloc_t *x = a;
    const ne_reloc_t *y = b;

    return (x->offset > y->offset) - (x->offset < y->offset);
}

static int
fixup_cmp(const void *a, const void *b)
{
    const le_fixup_t *x = a;
    const le_fixup_t *y = b;

    return (x->offset > y->offset) - (x->offset < y->offset);
}

/* NASM writes a far reference as an offset fixup and a base fixup two bytes
   later; an import referenced that way becomes one far-address relocation. */
static void
merge_far_imports(oseg_t *s)
{
    size_t i, n = 0;

    for (i = 0; i < s->n_relocs; i++) {
        ne_reloc_t *r = &s->relocs[i];

        if ((i + 1 < s->n_relocs) && (r->src == NE_SRC_OFFSET) && (r->flags == NE_REL_ORDINAL) &&
            (s->relocs[i + 1].src == NE_SRC_SEGMENT) && (s->relocs[i + 1].flags == NE_REL_ORDINAL) &&
            (s->relocs[i + 1].offset == r->offset + 2) && (s->relocs[i + 1].a == r->a) &&
            (s->relocs[i + 1].b == r->b)) {
            r->src = NE_SRC_FARADDR;
            s->data[r->offset + 2] = 0;
            s->data[r->offset + 3] = 0;
            s->relocs[n++]         = *r;
            i++;
            continue;
        }
        s->relocs[n++] = *r;
    }
    s->n_relocs = n;
}

static void
apply_fixups(void)
{
    size_t o, k, i;

    for (o = 0; o < n_objs; o++) {
        for (k = 0; k < objs[o]->n_segs; k++) {
            const omf_seg_t *os = &objs[o]->segs[k];

            for (i = 0; i < os->n_fixups; i++) {
                if (img.format == FORMAT_NE)
                    apply_ne(o, os, &os->fixups[i]);
                else
                    apply_le(o, os, &os->fixups[i]);
            }
        }
    }
    for (i = 0; i < img.n_segs; i++) {
        oseg_t *s = &img.segs[i];
        size_t  j;

        qsort(s->relocs, s->n_relocs, sizeof(*s->relocs), reloc_cmp);
        qsort(s->fixups, s->n_fixups, sizeof(*s->fixups), fixup_cmp);
        for (j = 1; j < s->n_relocs; j++) {
            if (s->relocs[j].offset < s->relocs[j - 1].offset + 2)
                die("segment %s: relocations at %04X and %04X overlap", s->name, s->relocs[j - 1].offset,
                    s->relocs[j].offset);
        }
        for (j = 1; j < s->n_fixups; j++) {
            if (s->fixups[j].offset < s->fixups[j - 1].offset + 4)
                die("object %s: fixups at %X and %X overlap", s->name, s->fixups[j - 1].offset,
                    s->fixups[j].offset);
        }
        merge_far_imports(s);
        if (s->n_relocs > 0xffff)
            die("segment %s has more than 65535 relocations", s->name);
    }
}

/* --- Entry points and exports ---------------------------------------------- */

static void
resolve_internal(const char *symbol, int line, uint32_t *seg, uint32_t *off)
{
    const symbol_t *s = find_sym(symbol);

    if (s == NULL)
        cfg_error(def, line, "symbol %s is not defined (is it declared global?)", symbol);
    if (s->is_import)
        cfg_error(def, line, "symbol %s is an import", symbol);
    *seg = s->out + 1;
    *off = s->offset;
}

static int
export_cmp(const void *a, const void *b)
{
    const export_t *x = a;
    const export_t *y = b;

    return (x->ordinal > y->ordinal) - (x->ordinal < y->ordinal);
}

static void
resolve_exports(void)
{
    cfg_section_t *mod = cfg_section(def, "module");
    size_t         i;

    if (img.format == FORMAT_NE) {
        for (i = 0; i < img.n_exports; i++) {
            export_t *e = &img.exports[i];

            resolve_internal(e->symbol, e->line, &e->seg, &e->offset);
            if (e->offset > 0xffff)
                cfg_error(def, e->line, "export %s lies past 64 KB", e->symbol);
        }
        qsort(img.exports, img.n_exports, sizeof(*img.exports), export_cmp);
        if (cfg_has(mod, "entry"))
            resolve_internal(get_ident(mod, "entry"), cfg_line(mod, "entry"), &img.entry_seg,
                             &img.entry_off);
        for (i = 0; i < img.n_exports; i++) {
            size_t j;

            for (j = i + 1; j < img.n_exports; j++) {
                if (strcmp(img.exports[i].name, img.exports[j].name) == 0)
                    cfg_error(def, img.exports[j].line, "%s is exported twice", img.exports[j].name);
            }
        }
    } else
        resolve_internal(img.ddb_name, cfg_line(mod, "ddb"), &img.ddb_obj, &img.ddb_off);
}

/* --- Map file ------------------------------------------------------------ */

static int
sym_addr_cmp(const void *a, const void *b)
{
    const symbol_t *x = a;
    const symbol_t *y = b;

    if (x->is_import != y->is_import)
        return x->is_import - y->is_import;
    if (x->out != y->out)
        return (x->out > y->out) - (x->out < y->out);
    if (x->offset != y->offset)
        return (x->offset > y->offset) - (x->offset < y->offset);
    return strcmp(x->name, y->name);
}

static void
write_map(const char *path)
{
    buf_t     b;
    size_t    i;
    symbol_t *sorted = xmalloc((n_syms + 1) * sizeof(*sorted));

    memset(&b, 0, sizeof(b));
    buf_printf(&b, "%s %s\n\n", img.format == FORMAT_NE ? "NE module" : "LE module", img.name);
    buf_printf(&b, "%s:\n", img.format == FORMAT_NE ? "Segments" : "Objects");
    for (i = 0; i < img.n_segs; i++)
        buf_printf(&b, "  %2u %-12s %6X bytes, %u %s\n", (unsigned) i + 1, img.segs[i].name, img.segs[i].size,
                   (unsigned) (img.format == FORMAT_NE ? img.segs[i].n_relocs : img.segs[i].n_fixups),
                   img.format == FORMAT_NE ? "relocations" : "fixups");
    memcpy(sorted, syms, n_syms * sizeof(*syms));
    qsort(sorted, n_syms, sizeof(*sorted), sym_addr_cmp);
    buf_printf(&b, "\nSymbols:\n");
    for (i = 0; i < n_syms; i++) {
        if (sorted[i].is_import) {
            const import_t *im = &img.imports[sorted[i].import];

            buf_printf(&b, "  import  %s.%u  %s%s\n", img.modules[im->module - 1], im->ordinal, sorted[i].name,
                       im->used ? "" : "  (unused)");
        } else
            buf_printf(&b, "  %04X:%08X  %s\n", sorted[i].out + 1, sorted[i].offset, sorted[i].name);
    }
    if (img.n_exports) {
        buf_printf(&b, "\nExports:\n");
        for (i = 0; i < img.n_exports; i++)
            buf_printf(&b, "  @%-4u %04X:%04X  %s%s%s\n", img.exports[i].ordinal, img.exports[i].seg,
                       img.exports[i].offset, img.exports[i].name, img.exports[i].resident ? "  resident" : "",
                       img.exports[i].nodata ? "  nodata" : "");
    }
    write_file(path, b.data, b.len);
    buf_free(&b);
    free(sorted);
}

/* --- Main ---------------------------------------------------------------- */

static void
usage(void)
{
    die("usage: vglink -d DEFINITION -o OUTPUT [-m MAP] [-r RESOURCE_DIR] OBJECT...");
}

int
main(int argc, char **argv)
{
    const char *def_path = NULL;
    const char *out_path = NULL;
    const char *map_path = NULL;
    const char *res_dir  = NULL;
    char      **paths    = NULL;
    size_t      n_paths  = 0;
    char       *def_dir;
    char       *slash;
    buf_t       out;
    int         i;
    size_t      k;

    set_progname("vglink");
    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-d") == 0) && (i + 1 < argc))
            def_path = argv[++i];
        else if ((strcmp(argv[i], "-o") == 0) && (i + 1 < argc))
            out_path = argv[++i];
        else if ((strcmp(argv[i], "-m") == 0) && (i + 1 < argc))
            map_path = argv[++i];
        else if ((strcmp(argv[i], "-r") == 0) && (i + 1 < argc))
            res_dir = argv[++i];
        else if (argv[i][0] == '-')
            usage();
        else {
            paths            = xrealloc(paths, (n_paths + 1) * sizeof(*paths));
            paths[n_paths++] = argv[i];
        }
    }
    if (!def_path || !out_path || (n_paths == 0))
        usage();

    /* A PE link takes COFF objects and its own path. */
    {
        cfg_t         *peek = cfg_load(def_path);
        cfg_section_t *mod  = cfg_section(peek, "module");
        const char    *fmt  = cfg_value(mod, "format");
        int            pe   = (fmt != NULL) && (strcmp(fmt, "pe") == 0);

        cfg_free(peek);
        if (pe) {
            pe_link(def_path, out_path, map_path, paths, n_paths);
            return 0;
        }
    }
    for (k = 0; k < n_paths; k++) {
        objs           = xrealloc(objs, (n_objs + 1) * sizeof(*objs));
        objs[n_objs++] = omf_read(paths[k]);
    }

    def_dir = xstrdup(def_path);
    slash   = strrchr(def_dir, '/');
    if (strrchr(def_dir, '\\') && (!slash || (strrchr(def_dir, '\\') > slash)))
        slash = strrchr(def_dir, '\\');
    if (slash)
        *slash = 0;
    else
        strcpy(def_dir, ".");
    read_definition(def_path, res_dir ? res_dir : def_dir);

    place_segments();
    collect_symbols();
    apply_fixups();
    resolve_exports();
    cfg_check_unused(def);

    memset(&out, 0, sizeof(out));
    if (img.format == FORMAT_NE)
        ne_write(&img, &out);
    else
        le_write(&img, &out);
    write_file(out_path, out.data, out.len);
    if (map_path)
        write_map(map_path);
    buf_free(&out);
    return 0;
}
