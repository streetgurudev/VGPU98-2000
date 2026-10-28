/*
 * vglink: links pe-i386 COFF objects (from gcc -m32 or NASM -f win32) into a
 * 32-bit PE DLL for Windows 98: the DirectX driver DLL and the Glide DLLs.
 * Formats: spec/pe-format.md; the link definition: spec/vglink.md.
 *
 * The layout is fixed, as the DDK's own build of such DLLs does it: code and
 * read-only data in .text, data and BSS (and the import and export tables) in
 * .data, the version resource (if any) in .rsrc, base relocations in .reloc.
 * By default every section is shared, so the one copy Windows 98 loads into
 * the shared system arena serves every process; a per-process DLL (the Glide
 * DLLs) has none shared. The output depends only on the inputs: no
 * timestamps, no paths.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "vglink.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"

#define SECT_ALIGN 0x1000u
#define FILE_ALIGN 0x200u

/* COFF section flags */
#define SCN_CNT_CODE        0x00000020u
#define SCN_CNT_INIT_DATA   0x00000040u
#define SCN_CNT_UNINIT_DATA 0x00000080u
#define SCN_LNK_INFO        0x00000200u
#define SCN_LNK_REMOVE      0x00000800u
#define SCN_MEM_DISCARDABLE 0x02000000u
#define SCN_MEM_SHARED      0x10000000u
#define SCN_MEM_EXECUTE     0x20000000u
#define SCN_MEM_READ        0x40000000u
#define SCN_MEM_WRITE       0x80000000u

/* i386 relocation types */
#define REL_DIR32   0x0006
#define REL_DIR32NB 0x0007
#define REL_REL32   0x0014

enum { OUT_TEXT, OUT_DATA, OUT_NONE };

typedef struct csec_t {
    char     *name;
    uint32_t  flags;
    uint32_t  size;
    uint32_t  align;
    uint8_t  *data;    /* NULL for BSS */
    uint32_t  reloc_at;
    uint32_t  n_relocs;
    int       out;     /* OUT_* */
    uint32_t  rva;     /* where it lands in the image */
} csec_t;

typedef struct csym_t {
    char    *name;
    uint32_t value;
    int16_t  section; /* 1-based; 0 undefined or common; -1 absolute */
    uint8_t  class_;
    uint16_t type;    /* 0x20: a function (COFF's derived type) */
} csym_t;

typedef struct cobj_t {
    const char *path;
    uint8_t    *file;
    size_t      size;
    csec_t     *secs;
    uint32_t    n_secs;
    csym_t     *syms;    /* indexed by symbol table index; aux entries are blank */
    uint32_t    n_syms;
    uint32_t   *common_rva; /* per symbol: RVA allocated for a common symbol */
} cobj_t;

typedef struct gsym_t {
    char       *name;
    uint32_t    rva;
    int         absolute;
    uint32_t    abs_value;
    const char *where;
} gsym_t;

typedef struct pimport_t {
    char    *dll;
    char    *name;    /* the function's name in the DLL */
    char    *symbol;  /* the IAT slot's symbol (__imp__Name@n) */
    uint32_t ilt_rva, iat_rva, hint_rva;
} pimport_t;

typedef struct pexport_t {
    char       *name;
    char       *symbol;
    uint32_t    rva;
    int         line;      /* in the definition, or in the list file */
    const char *list;      /* the list file it came from, or NULL */
} pexport_t;

static cfg_t     *def;
static cobj_t    *objs;
static size_t     n_objs;
static gsym_t    *gsyms;
static size_t     n_gsyms;
static pimport_t *imps;
static size_t     n_imps;
static pexport_t *exps;
static size_t     n_exps;
static char      *dll_name;
static uint32_t   image_base;
static char      *entry_symbol;
static int        shared_sections; /* 1: every section shared (the default); 0: per-process data */
static buf_t      version_res;     /* the version resource ([version]), empty without one */
static uint32_t  *base_relocs; /* RVAs of DIR32 locations */
static size_t     n_base_relocs;

static uint32_t
rd16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t
rd32(const uint8_t *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

static void
wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static uint32_t
align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) / a * a;
}

/* --- COFF objects ---------------------------------------------------------- */

static char *
coff_name(const cobj_t *o, const uint8_t *field, uint32_t strtab)
{
    char name[9];

    if (rd32(field) == 0) { /* string table offset in the second dword */
        uint32_t off = rd32(field + 4);

        if ((size_t) strtab + off >= o->size)
            die("%s: symbol name outside the string table", o->path);
        return xstrdup((const char *) o->file + strtab + off);
    }
    memcpy(name, field, 8);
    name[8] = 0;
    return xstrdup(name);
}

static char *
section_name(const cobj_t *o, const uint8_t *field, uint32_t strtab)
{
    char name[9];

    memcpy(name, field, 8);
    name[8] = 0;
    if (name[0] == '/') { /* /n: offset n in the string table */
        uint32_t off = (uint32_t) strtoul(name + 1, NULL, 10);

        if ((size_t) strtab + off >= o->size)
            die("%s: section name outside the string table", o->path);
        return xstrdup((const char *) o->file + strtab + off);
    }
    return xstrdup(name);
}

static int
name_is(const char *name, const char *base)
{
    size_t n = strlen(base);

    return (strncmp(name, base, n) == 0) && ((name[n] == 0) || (name[n] == '$') || (name[n] == '.'));
}

static void
coff_read(cobj_t *o, const char *path)
{
    uint32_t nsec, symtab, nsym, strtab, i;

    memset(o, 0, sizeof(*o));
    o->path = path;
    o->file = read_file(path, &o->size);
    if ((o->size < 20) || (rd16(o->file) != 0x014c))
        die("%s: not a pe-i386 COFF object (compile with gcc -m32 or nasm -f win32)", path);
    nsec   = rd16(o->file + 2);
    symtab = rd32(o->file + 8);
    nsym   = rd32(o->file + 12);
    strtab = symtab + nsym * 18;
    if ((size_t) 20 + rd16(o->file + 16) + nsec * 40 > o->size || (size_t) strtab + 4 > o->size)
        die("%s: truncated COFF object", path);

    o->n_secs = nsec;
    o->secs   = xcalloc(nsec + 1, sizeof(*o->secs));
    for (i = 0; i < nsec; i++) {
        const uint8_t *h   = o->file + 20 + rd16(o->file + 16) + i * 40;
        csec_t        *s   = &o->secs[i];
        uint32_t       raw = rd32(h + 20);
        uint32_t       al  = (rd32(h + 36) >> 20) & 0xf;

        s->name     = section_name(o, h, strtab);
        s->flags    = rd32(h + 36);
        s->size     = rd32(h + 16);
        s->align    = al ? (1u << (al - 1)) : 16;
        s->reloc_at = rd32(h + 24);
        s->n_relocs = rd16(h + 32);
        if (!(s->flags & SCN_CNT_UNINIT_DATA) && s->size) {
            if ((size_t) raw + s->size > o->size)
                die("%s: section %s runs past the end of the file", path, s->name);
            s->data = o->file + raw;
        }
        if ((size_t) s->reloc_at + (size_t) s->n_relocs * 10 > o->size)
            die("%s: section %s's relocations run past the end of the file", path, s->name);

        if ((s->flags & (SCN_LNK_INFO | SCN_LNK_REMOVE)) || name_is(s->name, ".debug") ||
            name_is(s->name, ".drectve") || name_is(s->name, ".comment") || name_is(s->name, ".note"))
            s->out = OUT_NONE;
        else if (name_is(s->name, ".eh_frame") || name_is(s->name, ".pdata") || name_is(s->name, ".xdata"))
            die("%s: section %s: compile with -fno-asynchronous-unwind-tables", path, s->name);
        else if (name_is(s->name, ".ctors") || name_is(s->name, ".dtors") || name_is(s->name, ".tls") ||
                 name_is(s->name, ".CRT"))
            die("%s: section %s needs a C runtime this DLL does not have", path, s->name);
        else if ((s->flags & SCN_CNT_CODE) || name_is(s->name, ".text") || name_is(s->name, ".rdata") ||
                 name_is(s->name, ".rodata"))
            s->out = OUT_TEXT;
        else
            s->out = OUT_DATA;
    }

    o->n_syms     = nsym;
    o->syms       = xcalloc(nsym + 1, sizeof(*o->syms));
    o->common_rva = xcalloc(nsym + 1, sizeof(*o->common_rva));
    for (i = 0; i < nsym; i++) {
        const uint8_t *e   = o->file + symtab + i * 18;
        csym_t        *s   = &o->syms[i];
        uint32_t       aux = e[17];

        s->name    = coff_name(o, e, strtab);
        s->value   = rd32(e + 8);
        s->section = (int16_t) rd16(e + 12);
        s->class_  = e[16];
        s->type    = (uint16_t) rd16(e + 14);
        i += aux;
    }
}

/* --- Global symbols -------------------------------------------------------- */

/* The map's functions, every object's (external and static), by address. */
typedef struct mfunc_t {
    uint32_t    rva;
    const char *name;
    const char *obj; /* a static one's object, else NULL */
} mfunc_t;

static int
mfunc_cmp(const void *a, const void *b)
{
    const mfunc_t *x = a;
    const mfunc_t *y = b;

    if (x->rva != y->rva)
        return (x->rva > y->rva) - (x->rva < y->rva);
    return strcmp(x->name, y->name);
}

static int
gsym_cmp(const void *a, const void *b)
{
    return strcmp(((const gsym_t *) a)->name, ((const gsym_t *) b)->name);
}

static gsym_t *
gsym_find(const char *name)
{
    gsym_t key;

    key.name = (char *) name;
    return bsearch(&key, gsyms, n_gsyms, sizeof(*gsyms), gsym_cmp);
}

static void
gsym_add(const char *name, uint32_t rva, const char *where)
{
    gsyms = xrealloc(gsyms, (n_gsyms + 1) * sizeof(*gsyms));
    memset(&gsyms[n_gsyms], 0, sizeof(*gsyms));
    gsyms[n_gsyms].name  = xstrdup(name);
    gsyms[n_gsyms].rva   = rva;
    gsyms[n_gsyms].where = where;
    n_gsyms++;
}

/* --- Definition -------------------------------------------------------------- */

static const char *
ident(cfg_section_t *sec, const char *key)
{
    const char *v = cfg_value(sec, key);
    const char *p;

    if (v == NULL)
        cfg_error(def, sec->line, "[%s] needs '%s'", sec->name, key);
    for (p = v; *p; p++) {
        if (!isalnum((unsigned char) *p) && !strchr("_.$?@-", *p))
            cfg_error(def, cfg_line(sec, key), "'%s' must be one name", key);
    }
    return v;
}

/* The symbols of a section's 'symbols' key: names separated by spaces, each of
   letters, digits and _ . $ ? @ - (the characters of decorated names). */
static size_t
symbol_list(cfg_section_t *sec, char **out, size_t max)
{
    const char *v = cfg_value(sec, "symbols");
    size_t      n = 0;

    if (v == NULL)
        cfg_error(def, sec->line, "[%s] needs 'symbols'", sec->name);
    while (*v) {
        const char *start;

        while ((*v == ' ') || (*v == '	'))
            v++;
        if (!*v)
            break;
        start = v;
        while (*v && (*v != ' ') && (*v != '	')) {
            if (!isalnum((unsigned char) *v) && !strchr("_.$?@-", *v))
                cfg_error(def, cfg_line(sec, "symbols"), "'symbols' must be names separated by spaces");
            v++;
        }
        if (n == max)
            cfg_error(def, cfg_line(sec, "symbols"), "more than %u symbols", (unsigned) max);
        out[n] = xmalloc((size_t) (v - start) + 1);
        memcpy(out[n], start, (size_t) (v - start));
        out[n][v - start] = 0;
        n++;
    }
    return n;
}

/* The symbols of an export list file, the [exports] section's 'file' (in the
   definition's directory): one name per line, of the characters above;
   blank lines and lines starting with ';' or '#' are ignored. Each symbol's
   line goes to LINES and the file's path to *LIST, for messages. */
static size_t
symbol_file(cfg_section_t *sec, const char *dir, char **out, int *lines, size_t max, char **list)
{
    const char *name = ident(sec, "file");
    char       *path = xmalloc(strlen(dir) + strlen(name) + 2);
    size_t      size, n = 0;
    uint8_t    *text;
    char       *p;
    int         line = 0;

    sprintf(path, "%s/%s", dir, name);
    text = read_file(path, &size);
    if (strlen((const char *) text) != size)
        die("%s: a NUL byte in a list of names", path);
    if ((size >= 3) && (text[0] == 0xef) && (text[1] == 0xbb) && (text[2] == 0xbf))
        die("%s: starts with a UTF-8 byte order mark; save it as plain ASCII", path);
    for (p = (char *) text; *p;) {
        char *start, *end;

        line++;
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
        {
            const char *c;

            for (c = start; c < end; c++) {
                if (!isalnum((unsigned char) *c) && !strchr("_.$?@-", *c))
                    die("%s:%d: '%.*s' is not one symbol name", path, line, (int) (end - start), start);
            }
        }
        if (n == max)
            die("%s: more than %u symbols", path, (unsigned) max);
        out[n] = xmalloc((size_t) (end - start) + 1);
        memcpy(out[n], start, (size_t) (end - start));
        out[n][end - start] = 0;
        lines[n] = line;
        n++;
    }
    if (n == 0)
        die("%s: no symbols", path);
    free(text);
    *list = path;
    return n;
}

/* The directory a definition file is in: the files it names are there. */
static char *
def_directory(const char *path)
{
    char *dir       = xstrdup(path);
    char *slash     = strrchr(dir, '/');
    char *backslash = strrchr(dir, '\\');

    if (backslash && (!slash || (backslash > slash)))
        slash = backslash;
    if (slash)
        *slash = 0;
    else
        strcpy(dir, ".");
    return dir;
}

/* --- Version information (32-bit VS_VERSIONINFO, an RT_VERSION resource) --- */

/* A string as UTF-16, with its terminator. The definition's strings are
   printable ASCII (cfg_get_str), so each character is its own code. */
static void
put_wstr(buf_t *b, const char *s)
{
    do
        put16(b, (uint8_t) *s);
    while (*s++);
}

/* Starts a block at the next 32-bit boundary: its length and value length are
   filled in by end_block32 and the caller. TYPE is 1 for text, 0 for binary. */
static size_t
begin_block32(buf_t *b, const char *key, uint32_t type)
{
    size_t at;

    pad_to(b, 4);
    at = b->len;
    put16(b, 0);
    put16(b, 0);
    put16(b, type);
    put_wstr(b, key);
    pad_to(b, 4);
    return at;
}

/* A block's length: up to its last byte, the padding after it not counted
   (the next block starts on a 32-bit boundary). */
static void
end_block32(buf_t *b, size_t at)
{
    set16(b, at, (uint32_t) (b->len - at));
}

static void
version_numbers(cfg_section_t *sec, const char *key, uint32_t *ms, uint32_t *ls)
{
    const char *v = ident(sec, key);
    unsigned    a, b, c, d;
    char        tail;

    if ((sscanf(v, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) || (a > 0xffff) || (b > 0xffff) ||
        (c > 0xffff) || (d > 0xffff))
        cfg_error(def, cfg_line(sec, key), "'%s' must be four numbers, as in 4.10.0.1", key);
    *ms = (a << 16) | b;
    *ls = (c << 16) | d;
}

/* [version]: the keys of an NE module's (spec/vglink.md), as the 32-bit
   resource Windows reads from a PE file (the Version tab of its Properties). */
static void
read_pe_version(cfg_section_t *sec)
{
    static const struct {
        const char *key;
        const char *name;
    } strings[] = { { "company", "CompanyName" },           { "description", "FileDescription" },
                    { "file_version_text", "FileVersion" }, { "internal_name", "InternalName" },
                    { "copyright", "LegalCopyright" },      { "original_name", "OriginalFilename" },
                    { "product_name", "ProductName" },      { "product_version_text", "ProductVersion" } };
    buf_t   *b = &version_res;
    uint32_t fms, fls, pms, pls, type, subtype;
    size_t   root, sfi, table, vfi, var, i;

    if (b->len)
        cfg_error(def, sec->line, "[version] repeated");
    version_numbers(sec, "file_version", &fms, &fls);
    version_numbers(sec, "product_version", &pms, &pls);
    type    = cfg_get_u32(def, sec, "file_type", 1, 7);
    subtype = cfg_has(sec, "file_subtype") ? cfg_get_u32(def, sec, "file_subtype", 0, 0xffff) : 0;

    root = begin_block32(b, "VS_VERSION_INFO", 0);
    set16(b, root + 2, 52);
    put32(b, 0xfeef04bd); /* signature */
    put32(b, 0x00010000); /* structure version */
    put32(b, fms);
    put32(b, fls);
    put32(b, pms);
    put32(b, pls);
    put32(b, 0x3f);       /* flags mask */
    put32(b, 0);          /* flags */
    put32(b, 0x00010004); /* VOS_DOS_WINDOWS32: 32-bit Windows on MS-DOS, Windows 95 and 98 */
    put32(b, type);
    put32(b, subtype);
    put32(b, 0);          /* date */
    put32(b, 0);

    sfi   = begin_block32(b, "StringFileInfo", 1);
    /* U.S. English, Windows Latin-1: as a Voodoo3's Glide DLLs, Windows 98's own and
       the NE version resources have it. Windows' installer counts another
       language or code page as a conflict when it replaces a file. */
    table = begin_block32(b, "040904E4", 1);
    for (i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        const char  *value = cfg_get_str(def, sec, strings[i].key, 200);
        const size_t at    = begin_block32(b, strings[i].name, 1);

        put_wstr(b, value);
        set16(b, at + 2, (uint32_t) strlen(value) + 1); /* in characters, with the terminator */
        end_block32(b, at);
    }
    end_block32(b, table);
    end_block32(b, sfi);

    vfi = begin_block32(b, "VarFileInfo", 1);
    var = begin_block32(b, "Translation", 0);
    set16(b, var + 2, 4);
    put16(b, 0x0409);
    put16(b, 0x04e4);
    end_block32(b, var);
    end_block32(b, vfi);
    end_block32(b, root);
}

/* The .rsrc section for the version resource at RVA: the resource directory
   (type 16, ID 1, language 409h: one entry each), the data entry, the data. */
static void
build_rsrc(buf_t *r, uint32_t rva)
{
    static const uint32_t ids[3] = { 16, 1, 0x0409 }; /* RT_VERSION, ID 1, U.S. English */
    int                   level;

    for (level = 0; level < 3; level++) {
        put32(r, 0); /* characteristics */
        put32(r, 0); /* time stamp: none, for reproducible output */
        put32(r, 0); /* version */
        put16(r, 0); /* named entries */
        put16(r, 1); /* ID entries */
        put32(r, ids[level]);
        /* A directory below, or (from the last level) the data entry: offsets in the section. */
        put32(r, (level < 2) ? (0x80000000u | (uint32_t) (24 * (level + 1))) : 72u);
    }
    put32(r, rva + 88); /* the data entry: the data's RVA, */
    put32(r, (uint32_t) version_res.len);
    put32(r, 0);        /* code page */
    put32(r, 0);
    buf_append(r, version_res.data, version_res.len);
}

/* An error about an export, at its line: in its list file, or in the
   definition. */
static VGPU98_NORETURN void export_error(const pexport_t *ex, const char *fmt, ...) VGPU98_PRINTF(2, 3);

static void
export_error(const pexport_t *ex, const char *fmt, ...)
{
    char    text[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (ex->list)
        die("%s:%d: %s", ex->list, ex->line, text);
    cfg_error(def, ex->line, "%s", text);
}

/* The name a stdcall symbol stands for: SYMBOL is PREFIX name @ bytes.
   NULL if it is not of that shape. */
static char *
undecorate(const char *symbol, const char *prefix)
{
    size_t      plen = strlen(prefix);
    const char *at   = strrchr(symbol, '@');
    const char *p;
    char       *name;

    if ((strncmp(symbol, prefix, plen) != 0) || !at || (at == symbol + plen) || !at[1])
        return NULL;
    for (p = at + 1; *p; p++) {
        if (!isdigit((unsigned char) *p))
            return NULL;
    }
    name = xmalloc((size_t) (at - symbol) - plen + 1);
    memcpy(name, symbol + plen, (size_t) (at - symbol) - plen);
    name[(at - symbol) - plen] = 0;
    return name;
}

static void
read_pe_definition(const char *def_dir)
{
    cfg_section_t *mod = cfg_section(def, "module");
    int            i;

    (void) cfg_get_word(def, mod, "format");
    dll_name        = xstrdup(ident(mod, "name"));
    image_base      = cfg_get_u32(def, mod, "base", 0x10000, 0xffff0000u);
    entry_symbol    = cfg_has(mod, "entry") ? xstrdup(ident(mod, "entry")) : NULL;
    shared_sections = cfg_has(mod, "shared") ? (int) cfg_get_u32(def, mod, "shared", 0, 1) : 1;
    if (image_base & 0xffff)
        cfg_error(def, cfg_line(mod, "base"), "the image base must be a multiple of 64 KB");
    if (cfg_has(mod, "description"))
        (void) cfg_get_str(def, mod, "description", 255);

    for (i = 0; i < def->n_sections; i++) {
        cfg_section_t *sec = &def->sections[i];
        char          *words[1024];
        int            lines[1024];
        char          *list = NULL;
        size_t         n;
        size_t         k;

        if (strcmp(sec->name, "imports") == 0) {
            if ((sec->arg == NULL) || strchr(sec->arg, ' '))
                cfg_error(def, sec->line, "expected [imports DLLNAME]");
            n = symbol_list(sec, words, 256);
            for (k = 0; k < n; k++) {
                pimport_t im;

                memset(&im, 0, sizeof(im));
                im.dll    = xstrdup(sec->arg);
                im.symbol = words[k];
                im.name   = undecorate(words[k], "__imp__");
                if (!im.name)
                    cfg_error(def, cfg_line(sec, "symbols"), "'%s' is not an import symbol (__imp__name@n)", words[k]);
                imps           = xrealloc(imps, (n_imps + 1) * sizeof(*imps));
                imps[n_imps++] = im;
            }
        } else if (strcmp(sec->name, "exports") == 0) {
            /* Undecorated names (DriverInit for _DriverInit@4) by default;
               'decorated' exports each symbol's name as it is written, as a
               Glide DLL's table has them (_grDrawTriangle@12). */
            const char *names     = cfg_has(sec, "names") ? cfg_get_word(def, sec, "names") : "undecorated";
            const int   decorated = strcmp(names, "decorated") == 0;
            const int   from_file = cfg_has(sec, "file");

            if (!decorated && (strcmp(names, "undecorated") != 0))
                cfg_error(def, cfg_line(sec, "names"), "'names' is undecorated or decorated");
            if (from_file == cfg_has(sec, "symbols"))
                cfg_error(def, sec->line, "[exports] takes either 'symbols' or 'file'");
            n = from_file ? symbol_file(sec, def_dir, words, lines, 1024, &list) : symbol_list(sec, words, 256);
            for (k = 0; k < n; k++) {
                pexport_t ex;

                memset(&ex, 0, sizeof(ex));
                ex.symbol = words[k];
                ex.name   = decorated ? xstrdup(words[k]) : undecorate(words[k], "_");
                ex.line   = from_file ? lines[k] : cfg_line(sec, "symbols");
                ex.list   = from_file ? list : NULL;
                if (!ex.name)
                    export_error(&ex, "'%s' is not a stdcall symbol (_name@n)", words[k]);
                exps           = xrealloc(exps, (n_exps + 1) * sizeof(*exps));
                exps[n_exps++] = ex;
            }
        } else if (strcmp(sec->name, "version") == 0) {
            if (sec->arg)
                cfg_error(def, sec->line, "expected [version], without a name");
            read_pe_version(sec);
        }
    }
}

static int
export_name_cmp(const void *a, const void *b)
{
    return strcmp(((const pexport_t *) a)->name, ((const pexport_t *) b)->name);
}

/* --- Link ------------------------------------------------------------------ */

typedef struct outsec_t {
    buf_t    b;
    uint32_t rva;
    uint32_t vsize;
} outsec_t;

static void
place(outsec_t *out, csec_t *s)
{
    uint32_t at = align_up((uint32_t) out->b.len, s->align);

    while (out->b.len < at)
        put8(&out->b, 0);
    s->rva = at; /* relative to the section for now */
    if (s->data)
        buf_append(&out->b, s->data, s->size);
    else {
        uint32_t k;

        for (k = 0; k < s->size; k++)
            put8(&out->b, 0);
    }
}

static uint32_t
symbol_rva(const cobj_t *o, uint32_t index, int *absolute, const char *what)
{
    const csym_t *s = &o->syms[index];

    *absolute = 0;
    if (s->section > 0) {
        const csec_t *sec = &o->secs[s->section - 1];

        if (sec->out == OUT_NONE)
            die("%s: %s refers to %s in discarded section %s", o->path, what, s->name, sec->name);
        return sec->rva + s->value;
    }
    if (s->section == -1) {
        *absolute = 1;
        return s->value;
    }
    if ((s->section == 0) && (s->value != 0))
        return o->common_rva[index];
    {
        const gsym_t *g = gsym_find(s->name);

        if (g == NULL)
            die("%s: undefined symbol %s", o->path, s->name);
        *absolute = g->absolute;
        return g->absolute ? g->abs_value : g->rva;
    }
}

static void
add_base_reloc(uint32_t rva)
{
    base_relocs = xrealloc(base_relocs, (n_base_relocs + 1) * sizeof(*base_relocs));
    base_relocs[n_base_relocs++] = rva;
}

static int
u32_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;

    return (x > y) - (x < y);
}

static uint8_t *
image_byte(outsec_t *text, outsec_t *data, uint32_t rva, uint32_t len)
{
    if ((rva >= text->rva) && (rva + len <= text->rva + text->b.len))
        return (uint8_t *) text->b.data + (rva - text->rva);
    if ((rva >= data->rva) && (rva + len <= data->rva + data->b.len))
        return (uint8_t *) data->b.data + (rva - data->rva);
    die("internal error: location %X outside the image", rva);
}

static void
apply_relocations(outsec_t *text, outsec_t *data)
{
    size_t o;

    for (o = 0; o < n_objs; o++) {
        cobj_t  *ob = &objs[o];
        uint32_t s;

        for (s = 0; s < ob->n_secs; s++) {
            csec_t  *sec = &ob->secs[s];
            uint32_t r;

            if ((sec->out == OUT_NONE) || (sec->n_relocs == 0))
                continue;
            if (sec->data == NULL)
                die("%s: relocations in BSS section %s", ob->path, sec->name);
            for (r = 0; r < sec->n_relocs; r++) {
                const uint8_t *rel  = ob->file + sec->reloc_at + r * 10;
                uint32_t       at   = sec->rva + rd32(rel);
                uint32_t       idx  = rd32(rel + 4);
                uint32_t       type = rd16(rel + 8);
                int            absolute;
                uint32_t       target;
                uint8_t       *p;

                if (idx >= ob->n_syms)
                    die("%s: relocation in %s names symbol %u of %u", ob->path, sec->name, idx, ob->n_syms);
                if (rd32(rel) + 4 > sec->size)
                    die("%s: relocation at %s+%X runs past the section", ob->path, sec->name, rd32(rel));
                target = symbol_rva(ob, idx, &absolute, "a relocation");
                p      = image_byte(text, data, at, 4);
                switch (type) {
                    case REL_DIR32:
                        if (absolute)
                            wr32(p, rd32(p) + target);
                        else {
                            wr32(p, rd32(p) + image_base + target);
                            add_base_reloc(at);
                        }
                        break;
                    case REL_DIR32NB:
                        if (absolute)
                            die("%s: image-relative reference to absolute symbol %s", ob->path, ob->syms[idx].name);
                        wr32(p, rd32(p) + target);
                        break;
                    case REL_REL32:
                        if (absolute)
                            die("%s: relative reference to absolute symbol %s", ob->path, ob->syms[idx].name);
                        wr32(p, rd32(p) + target - (at + 4));
                        break;
                    default:
                        die("%s: relocation type %04Xh in %s is not supported", ob->path, type, sec->name);
                }
            }
        }
    }
}

static void
build_relocs(buf_t *b)
{
    size_t i = 0;

    qsort(base_relocs, n_base_relocs, sizeof(*base_relocs), u32_cmp);
    while (i < n_base_relocs) {
        uint32_t page  = base_relocs[i] & ~0xfffu;
        size_t   start = b->len;
        size_t   n     = 0;

        put32(b, page);
        put32(b, 0);
        while ((i < n_base_relocs) && ((base_relocs[i] & ~0xfffu) == page)) {
            put16(b, 0x3000 | (base_relocs[i] & 0xfff)); /* HIGHLOW */
            i++;
            n++;
        }
        if (n & 1)
            put16(b, 0); /* ABSOLUTE: padding to a dword */
        set32(b, start + 4, (uint32_t) (b->len - start));
    }
}

static void
put_str(buf_t *b, const char *s)
{
    buf_append(b, s, strlen(s) + 1);
}

void
pe_link(const char *def_path, const char *out_path, const char *map_path, char **obj_paths, size_t n_paths)
{
    outsec_t text, data;
    buf_t    relocs, rsrc, out;
    size_t   o, i;
    uint32_t s;
    uint32_t imp_dir_rva = 0, imp_dir_size = 0, iat_rva = 0, iat_size = 0;
    uint32_t exp_dir_rva = 0, exp_dir_size = 0;
    uint32_t rsrc_rva = 0, reloc_rva, size_of_image, headers_size, entry_rva = 0;
    uint32_t text_raw, data_raw, rsrc_raw, reloc_raw, file_off, n_out_secs;
    uint32_t data_init = 0;
    size_t   pe_at;
    char    *def_dir = def_directory(def_path);

    def = cfg_load(def_path);
    read_pe_definition(def_dir);
    free(def_dir);

    n_objs = n_paths;
    objs   = xcalloc(n_objs, sizeof(*objs));
    for (o = 0; o < n_objs; o++)
        coff_read(&objs[o], obj_paths[o]);

    memset(&text, 0, sizeof(text));
    memset(&data, 0, sizeof(data));

    /* Code and read-only data, then initialized data, the import and export
       tables, then BSS, in object order. */
    for (o = 0; o < n_objs; o++) {
        for (s = 0; s < objs[o].n_secs; s++) {
            if (objs[o].secs[s].out == OUT_TEXT)
                place(&text, &objs[o].secs[s]);
        }
    }
    for (o = 0; o < n_objs; o++) {
        for (s = 0; s < objs[o].n_secs; s++) {
            if ((objs[o].secs[s].out == OUT_DATA) && objs[o].secs[s].data)
                place(&data, &objs[o].secs[s]);
        }
    }
    /* Import tables, after the initialized data: descriptors, ILTs, IATs, names. */
    if (n_imps) {
        char  **dlls   = NULL;
        size_t  n_dlls = 0, d;
        uint32_t desc_at, at;

        for (i = 0; i < n_imps; i++) {
            for (d = 0; d < n_dlls; d++) {
                if (strcmp(dlls[d], imps[i].dll) == 0)
                    break;
            }
            if (d == n_dlls) {
                dlls = xrealloc(dlls, (n_dlls + 1) * sizeof(*dlls));
                dlls[n_dlls++] = imps[i].dll;
            }
        }
        pad_to(&data.b, 4);
        desc_at = (uint32_t) data.b.len;
        for (d = 0; d <= n_dlls; d++) {
            int k;

            for (k = 0; k < 5; k++)
                put32(&data.b, 0);
        }
        imp_dir_size = (uint32_t) (n_dlls + 1) * 20;
        /* ILT and IAT per DLL: one slot per import and a zero. */
        for (d = 0; d < n_dlls; d++) {
            uint32_t ilt = (uint32_t) data.b.len, iat;

            for (i = 0; i < n_imps; i++) {
                if (strcmp(imps[i].dll, dlls[d]) == 0) {
                    imps[i].ilt_rva = (uint32_t) data.b.len;
                    put32(&data.b, 0);
                }
            }
            put32(&data.b, 0);
            iat = (uint32_t) data.b.len;
            if (d == 0)
                iat_rva = iat;
            for (i = 0; i < n_imps; i++) {
                if (strcmp(imps[i].dll, dlls[d]) == 0) {
                    imps[i].iat_rva = (uint32_t) data.b.len;
                    put32(&data.b, 0);
                }
            }
            put32(&data.b, 0);
            iat_size = (uint32_t) data.b.len - iat_rva;
            set32(&data.b, desc_at + d * 20 + 0, ilt);  /* fixed up to RVAs below */
            set32(&data.b, desc_at + d * 20 + 16, iat);
        }
        for (i = 0; i < n_imps; i++) {
            pad_to(&data.b, 2);
            imps[i].hint_rva = (uint32_t) data.b.len;
            put16(&data.b, 0);
            put_str(&data.b, imps[i].name);
        }
        for (d = 0; d < n_dlls; d++) {
            at = (uint32_t) data.b.len;
            put_str(&data.b, dlls[d]);
            set32(&data.b, desc_at + d * 20 + 12, at);
        }
        imp_dir_rva = desc_at;
        free(dlls);
    }

    /* Export directory, after the imports. */
    if (n_exps) {
        uint32_t dir, funcs, names, ords, name_at;

        qsort(exps, n_exps, sizeof(*exps), export_name_cmp);
        for (i = 1; i < n_exps; i++) {
            if (strcmp(exps[i - 1].name, exps[i].name) == 0)
                export_error(&exps[i], "%s is exported twice", exps[i].name);
        }
        pad_to(&data.b, 4);
        dir = (uint32_t) data.b.len;
        for (i = 0; i < 10; i++)
            put32(&data.b, 0);
        funcs = (uint32_t) data.b.len;
        for (i = 0; i < n_exps; i++)
            put32(&data.b, 0);
        names = (uint32_t) data.b.len;
        for (i = 0; i < n_exps; i++)
            put32(&data.b, 0);
        ords = (uint32_t) data.b.len;
        for (i = 0; i < n_exps; i++)
            put16(&data.b, (uint32_t) i);
        name_at = (uint32_t) data.b.len;
        put_str(&data.b, dll_name);
        for (i = 0; i < n_exps; i++) {
            set32(&data.b, names + 4 * i, (uint32_t) data.b.len); /* fixed up below */
            put_str(&data.b, exps[i].name);
        }
        set32(&data.b, dir + 12, name_at);
        set32(&data.b, dir + 16, 1);                    /* ordinal base */
        set32(&data.b, dir + 20, (uint32_t) n_exps);    /* functions */
        set32(&data.b, dir + 24, (uint32_t) n_exps);    /* names */
        set32(&data.b, dir + 28, funcs);
        set32(&data.b, dir + 32, names);
        set32(&data.b, dir + 36, ords);
        exp_dir_rva  = dir;
        exp_dir_size = (uint32_t) data.b.len - dir;
    }

    /* BSS and common symbols last: zeros the file does not hold (the
       section's virtual size covers them). */
    data_init = (uint32_t) data.b.len;
    for (o = 0; o < n_objs; o++) {
        for (s = 0; s < objs[o].n_secs; s++) {
            if ((objs[o].secs[s].out == OUT_DATA) && !objs[o].secs[s].data)
                place(&data, &objs[o].secs[s]);
        }
    }
    /* Common symbols go into the data section as zeros. */
    for (o = 0; o < n_objs; o++) {
        uint32_t k;

        for (k = 0; k < objs[o].n_syms; k++) {
            const csym_t *sy = &objs[o].syms[k];

            if ((sy->section == 0) && (sy->value != 0) && (sy->class_ == 2)) {
                uint32_t at = align_up((uint32_t) data.b.len, sy->value >= 8 ? 8 : 4);
                uint32_t z;

                while (data.b.len < at)
                    put8(&data.b, 0);
                objs[o].common_rva[k] = at; /* relative to .data for now */
                for (z = 0; z < sy->value; z++)
                    put8(&data.b, 0);
            }
        }
    }


    /* Final addresses. */
    text.rva     = SECT_ALIGN;
    data.rva     = align_up(text.rva + (uint32_t) (text.b.len ? text.b.len : 1), SECT_ALIGN);
    for (o = 0; o < n_objs; o++) {
        uint32_t k;

        for (s = 0; s < objs[o].n_secs; s++) {
            csec_t *sec = &objs[o].secs[s];

            if (sec->out == OUT_TEXT)
                sec->rva += text.rva;
            else if (sec->out == OUT_DATA)
                sec->rva += data.rva;
        }
        for (k = 0; k < objs[o].n_syms; k++) {
            if (objs[o].common_rva[k])
                objs[o].common_rva[k] += data.rva;
        }
    }
    for (i = 0; i < n_imps; i++) {
        imps[i].ilt_rva += data.rva;
        imps[i].iat_rva += data.rva;
        imps[i].hint_rva += data.rva;
    }
    if (n_imps) {
        size_t d;

        imp_dir_rva += data.rva;
        iat_rva += data.rva;
        for (d = 0; d * 20 + 20 < imp_dir_size; d++) {
            size_t at = imp_dir_rva - data.rva + d * 20;

            set32(&data.b, at + 0, rd32((uint8_t *) data.b.data + at) + data.rva);
            set32(&data.b, at + 12, rd32((uint8_t *) data.b.data + at + 12) + data.rva);
            set32(&data.b, at + 16, rd32((uint8_t *) data.b.data + at + 16) + data.rva);
        }
        for (i = 0; i < n_imps; i++) {
            set32(&data.b, imps[i].ilt_rva - data.rva, imps[i].hint_rva);
            set32(&data.b, imps[i].iat_rva - data.rva, imps[i].hint_rva);
        }
    }
    if (n_exps) {
        size_t at = exp_dir_rva; /* still relative to .data */

        exp_dir_rva += data.rva;
        set32(&data.b, at + 12, rd32((uint8_t *) data.b.data + at + 12) + data.rva);
        set32(&data.b, at + 28, rd32((uint8_t *) data.b.data + at + 28) + data.rva);
        set32(&data.b, at + 32, rd32((uint8_t *) data.b.data + at + 32) + data.rva);
        set32(&data.b, at + 36, rd32((uint8_t *) data.b.data + at + 36) + data.rva);
        for (i = 0; i < n_exps; i++) {
            size_t nat = rd32((uint8_t *) data.b.data + at + 32) - data.rva + 4 * i;

            set32(&data.b, nat, rd32((uint8_t *) data.b.data + nat) + data.rva);
        }
    }

    /* Global symbols: definitions in the objects, and the import slots. */
    for (o = 0; o < n_objs; o++) {
        uint32_t k;

        for (k = 0; k < objs[o].n_syms; k++) {
            const csym_t *sy = &objs[o].syms[k];

            if ((sy->class_ != 2) || (sy->name == NULL))
                continue;
            if (sy->section > 0) {
                if (objs[o].secs[sy->section - 1].out != OUT_NONE)
                    gsym_add(sy->name, objs[o].secs[sy->section - 1].rva + sy->value, objs[o].path);
            } else if ((sy->section == 0) && (sy->value != 0))
                gsym_add(sy->name, objs[o].common_rva[k], objs[o].path);
            else if (sy->section == -1) {
                gsym_add(sy->name, 0, objs[o].path);
                gsyms[n_gsyms - 1].absolute  = 1;
                gsyms[n_gsyms - 1].abs_value = sy->value;
            }
        }
    }
    for (i = 0; i < n_imps; i++)
        gsym_add(imps[i].symbol, imps[i].iat_rva, def_path);
    qsort(gsyms, n_gsyms, sizeof(*gsyms), gsym_cmp);
    for (i = 1; i < n_gsyms; i++) {
        /* A common symbol defined in several objects is one symbol. */
        if ((strcmp(gsyms[i - 1].name, gsyms[i].name) == 0) && (gsyms[i - 1].rva != gsyms[i].rva))
            die("symbol %s is defined twice (%s and %s)", gsyms[i].name, gsyms[i - 1].where, gsyms[i].where);
    }

    apply_relocations(&text, &data);

    for (i = 0; i < n_exps; i++) {
        const gsym_t *g = gsym_find(exps[i].symbol);
        size_t        at;

        if ((g == NULL) || g->absolute)
            export_error(&exps[i], "export %s: symbol %s is not defined", exps[i].name, exps[i].symbol);
        exps[i].rva = g->rva;
        at          = exp_dir_rva - data.rva;
        set32(&data.b, rd32((uint8_t *) data.b.data + at + 28) - data.rva + 4 * i, g->rva);
    }
    if (entry_symbol) {
        const gsym_t *g = gsym_find(entry_symbol);

        if ((g == NULL) || g->absolute)
            die("entry point %s is not defined", entry_symbol);
        entry_rva = g->rva;
    }
    cfg_check_unused(def);

    memset(&relocs, 0, sizeof(relocs));
    memset(&rsrc, 0, sizeof(rsrc));
    build_relocs(&relocs);
    reloc_rva = align_up(data.rva + (uint32_t) (data.b.len ? data.b.len : 1), SECT_ALIGN);
    if (version_res.len) {
        /* The resources between the data and the relocations. */
        rsrc_rva = reloc_rva;
        build_rsrc(&rsrc, rsrc_rva);
        reloc_rva = align_up(rsrc_rva + (uint32_t) rsrc.len, SECT_ALIGN);
    }
    size_of_image = align_up(reloc_rva + (uint32_t) (relocs.len ? relocs.len : 1), SECT_ALIGN);
    n_out_secs    = rsrc.len ? 4 : 3;

    /* The file. */
    memset(&out, 0, sizeof(out));
    pe_at = write_stub(&out);
    /* The stub, the PE headers and the section table, to a file alignment
       boundary (200h with three sections). */
    headers_size = align_up((uint32_t) pe_at + 4 + 20 + 0xe0 + 40 * n_out_secs, FILE_ALIGN);
    text_raw  = align_up((uint32_t) text.b.len, FILE_ALIGN);
    data_raw  = align_up(data_init, FILE_ALIGN);
    rsrc_raw  = align_up((uint32_t) rsrc.len, FILE_ALIGN);
    reloc_raw = align_up((uint32_t) relocs.len, FILE_ALIGN);

    put32(&out, 0x00004550);                /* PE\0\0 */
    put16(&out, 0x014c);                    /* i386 */
    put16(&out, n_out_secs);                /* sections */
    put32(&out, 0);                         /* time stamp: none, for reproducible output */
    put32(&out, 0);
    put32(&out, 0);
    put16(&out, 0xe0);                      /* optional header size */
    put16(&out, 0x210e);                    /* executable, no line numbers or local symbols, 32-bit, DLL */

    put16(&out, 0x010b);                    /* PE32 */
    put8(&out, 1);                          /* linker version 1.0 */
    put8(&out, 0);
    put32(&out, text_raw);                  /* size of code */
    put32(&out, data_raw + rsrc_raw + reloc_raw); /* size of initialized data */
    put32(&out, 0);
    put32(&out, entry_rva);
    put32(&out, text.rva);                  /* base of code */
    put32(&out, data.rva);                  /* base of data */
    put32(&out, image_base);
    put32(&out, SECT_ALIGN);
    put32(&out, FILE_ALIGN);
    put16(&out, 4); put16(&out, 0);         /* OS 4.0 */
    put16(&out, 0); put16(&out, 0);         /* image version */
    put16(&out, 4); put16(&out, 0);         /* subsystem 4.0 */
    put32(&out, 0);
    put32(&out, size_of_image);
    put32(&out, headers_size);
    put32(&out, 0);                         /* checksum */
    put16(&out, 2);                         /* Windows GUI */
    put16(&out, 0);                         /* DLL characteristics */
    put32(&out, 0x100000); put32(&out, 0x1000);
    put32(&out, 0x100000); put32(&out, 0x1000);
    put32(&out, 0);
    put32(&out, 16);                        /* data directories */
    put32(&out, n_exps ? exp_dir_rva : 0); put32(&out, exp_dir_size);
    put32(&out, n_imps ? imp_dir_rva : 0); put32(&out, imp_dir_size);
    put32(&out, rsrc_rva); put32(&out, (uint32_t) rsrc.len); /* resources */
    put32(&out, 0); put32(&out, 0);         /* exceptions */
    put32(&out, 0); put32(&out, 0);         /* security */
    put32(&out, relocs.len ? reloc_rva : 0); put32(&out, (uint32_t) relocs.len);
    for (i = 6; i < 12; i++) {
        put32(&out, 0);
        put32(&out, 0);
    }
    put32(&out, n_imps ? iat_rva : 0); put32(&out, iat_size);
    for (i = 13; i < 16; i++) {
        put32(&out, 0);
        put32(&out, 0);
    }

    file_off = headers_size;
    {
        const uint32_t shared = shared_sections ? SCN_MEM_SHARED : 0;
        const char    *names[4];
        uint32_t       vsize[4], rva[4], raw[4], flags[4];
        uint32_t       k = 0;

        names[k] = ".text";  vsize[k] = (uint32_t) text.b.len; rva[k] = text.rva;  raw[k] = text_raw;
        flags[k++] = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | shared;
        names[k] = ".data";  vsize[k] = (uint32_t) data.b.len; rva[k] = data.rva;  raw[k] = data_raw;
        flags[k++] = SCN_CNT_INIT_DATA | SCN_MEM_READ | SCN_MEM_WRITE | shared;
        if (rsrc.len) {
            names[k] = ".rsrc"; vsize[k] = (uint32_t) rsrc.len; rva[k] = rsrc_rva; raw[k] = rsrc_raw;
            flags[k++] = SCN_CNT_INIT_DATA | SCN_MEM_READ | shared;
        }
        names[k] = ".reloc"; vsize[k] = (uint32_t) relocs.len; rva[k] = reloc_rva; raw[k] = reloc_raw;
        flags[k++] = SCN_CNT_INIT_DATA | SCN_MEM_DISCARDABLE | SCN_MEM_READ | shared;
        for (k = 0; k < n_out_secs; k++) {
            char name[8];

            memset(name, 0, sizeof(name));
            memcpy(name, names[k], strlen(names[k]));
            buf_append(&out, name, 8);
            put32(&out, vsize[k]);
            put32(&out, rva[k]);
            put32(&out, raw[k]);
            put32(&out, raw[k] ? file_off : 0);
            put32(&out, 0);
            put32(&out, 0);
            put16(&out, 0);
            put16(&out, 0);
            put32(&out, flags[k]);
            file_off += raw[k];
        }
    }
    if (out.len > headers_size)
        die("internal error: PE headers larger than %X bytes", headers_size);
    while (out.len < headers_size)
        put8(&out, 0);
    buf_append(&out, text.b.data, text.b.len);
    pad_to(&out, FILE_ALIGN);
    buf_append(&out, data.b.data, data_init);
    pad_to(&out, FILE_ALIGN);
    buf_append(&out, rsrc.data, rsrc.len);
    pad_to(&out, FILE_ALIGN);
    buf_append(&out, relocs.data, relocs.len);
    pad_to(&out, FILE_ALIGN);
    (void) pe_at;
    write_file(out_path, out.data, out.len);

    if (map_path) {
        buf_t m;

        memset(&m, 0, sizeof(m));
        buf_printf(&m, "PE DLL %s, image base %08X\n\n", dll_name, image_base);
        buf_printf(&m, "Sections:\n  .text   %08X  %6X bytes\n  .data   %08X  %6X bytes\n", text.rva,
                   (unsigned) text.b.len, data.rva, (unsigned) data.b.len);
        if (rsrc.len)
            buf_printf(&m, "  .rsrc   %08X  %6X bytes, the version resource\n", rsrc_rva, (unsigned) rsrc.len);
        buf_printf(&m, "  .reloc  %08X  %6X bytes, %u relocations\n", reloc_rva, (unsigned) relocs.len,
                   (unsigned) n_base_relocs);
        buf_printf(&m, "\nSymbols:\n");
        for (i = 0; i < n_gsyms; i++) {
            if (gsyms[i].absolute)
                buf_printf(&m, "  absolute  %08X  %s\n", gsyms[i].abs_value, gsyms[i].name);
            else
                buf_printf(&m, "  %08X  %s\n", image_base + gsyms[i].rva, gsyms[i].name);
        }
        /* Every function by address, the static ones with their object, so an
           address inside the image names its function. */
        {
            mfunc_t *f   = NULL;
            size_t   n   = 0;
            size_t   cap = 0;
            size_t   k;
            uint32_t j;

            for (k = 0; k < n_objs; k++) {
                const cobj_t *o = &objs[k];

                for (j = 0; j < o->n_syms; j++) {
                    const csym_t *sy = &o->syms[j];

                    if (!sy->name || (sy->section <= 0) || ((uint32_t) sy->section > o->n_secs) || ((sy->type & 0x30) != 0x20) ||
                        ((sy->class_ != 2) && (sy->class_ != 3)) || (o->secs[sy->section - 1].out != OUT_TEXT))
                        continue;
                    if (n == cap) {
                        cap = cap ? 2 * cap : 256;
                        f   = xrealloc(f, cap * sizeof(*f));
                    }
                    f[n].rva  = o->secs[sy->section - 1].rva + sy->value;
                    f[n].name = sy->name;
                    f[n].obj  = (sy->class_ == 3) ? o->path : NULL;
                    n++;
                }
            }
            if (n)
                qsort(f, n, sizeof(*f), mfunc_cmp);
            buf_printf(&m, "\nFunctions (by address; static ones with their object):\n");
            for (k = 0; k < n; k++) {
                const char *obj = f[k].obj;

                if (obj) {
                    const char *p;

                    /* The basename: past the last slash of either kind. */
                    for (p = obj; *p; p++) {
                        if ((*p == '/') || (*p == '\\'))
                            obj = p + 1;
                    }
                    buf_printf(&m, "  %08X  %s  (static, %s)\n", image_base + f[k].rva, f[k].name, obj);
                } else
                    buf_printf(&m, "  %08X  %s\n", image_base + f[k].rva, f[k].name);
            }
            free(f);
        }
        if (n_exps) {
            buf_printf(&m, "\nExports:\n");
            for (i = 0; i < n_exps; i++)
                buf_printf(&m, "  @%u  %08X  %s\n", (unsigned) i + 1, image_base + exps[i].rva, exps[i].name);
        }
        if (n_imps) {
            buf_printf(&m, "\nImports:\n");
            for (i = 0; i < n_imps; i++)
                buf_printf(&m, "  %08X  %s!%s\n", image_base + imps[i].iat_rva, imps[i].dll, imps[i].name);
        }
        write_file(map_path, m.data, m.len);
        buf_free(&m);
    }
    buf_free(&out);
    buf_free(&rsrc);
    buf_free(&relocs);
}
