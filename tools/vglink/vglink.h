/*
 * vglink: links NASM's OMF objects into a 16-bit Windows NE module (a display
 * driver) or an LE virtual device driver (a VxD). Usage and the link
 * definition format: spec/vglink.md. File formats: spec/ne-format.md and
 * spec/le-format.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_VGLINK_H
#define VGPU98_VGLINK_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

enum { FORMAT_NE, FORMAT_LE };

/* NE relocation record (spec/ne-format.md). */
enum { NE_SRC_SEGMENT = 2, NE_SRC_FARADDR = 3, NE_SRC_OFFSET = 5 };
enum { NE_REL_INTERNAL = 0, NE_REL_ORDINAL = 1 };

typedef struct ne_reloc_t {
    uint16_t offset; /* location in the segment */
    uint8_t  src;    /* NE_SRC_* */
    uint8_t  flags;  /* NE_REL_* */
    uint16_t a;      /* internal: segment number; import: module index (1-based) */
    uint16_t b;      /* internal: offset; import: ordinal */
} ne_reloc_t;

/* LE fixup (spec/le-format.md). */
enum { LE_SRC_OFFSET32 = 7, LE_SRC_RELATIVE32 = 8 };

typedef struct le_fixup_t {
    uint32_t offset; /* location in the object */
    uint8_t  src;    /* LE_SRC_* */
    uint32_t object; /* target object, 1-based */
    uint32_t target; /* target offset in that object */
} le_fixup_t;

/* An output segment (NE) or object (LE). */
typedef struct oseg_t {
    char       *name;
    int         line;      /* definition file line */
    char      **members;   /* OMF segment names placed in it, in order */
    size_t      n_members;
    int         is_data;   /* NE */
    int         preload;   /* NE and LE */
    uint32_t    le_flags;  /* LE object flags */
    uint32_t    size;
    uint8_t    *data;
    ne_reloc_t *relocs;
    size_t      n_relocs;
    le_fixup_t *fixups;
    size_t      n_fixups;
} oseg_t;

typedef struct export_t {
    char    *symbol;
    char    *name;     /* as written to the names table (upper case) */
    uint16_t ordinal;
    int      resident;
    int      nodata;
    uint32_t seg;      /* 1-based output segment */
    uint32_t offset;
    int      line;
} export_t;

typedef struct import_t {
    char    *symbol;
    uint16_t module; /* 1-based */
    uint16_t ordinal;
    int      used;
} import_t;

typedef struct resource_t {
    char    *type_name; /* NULL: numeric type_id */
    uint16_t type_id;
    char    *id_name;   /* NULL: numeric id */
    uint16_t id;
    uint8_t *data;
    size_t   len;
    int      preload;
} resource_t;

typedef struct image_t {
    int         format;
    char       *name;        /* module name */
    char       *description;
    oseg_t     *segs;
    size_t      n_segs;
    export_t   *exports;
    size_t      n_exports;
    char      **modules;     /* NE imported modules */
    size_t      n_modules;
    import_t   *imports;
    size_t      n_imports;
    resource_t *res;
    size_t      n_res;
    /* NE */
    uint32_t    entry_seg;   /* 1-based, 0 = none */
    uint32_t    entry_off;
    uint32_t    autodata;    /* 1-based, 0 = none */
    uint32_t    heap;
    uint32_t    expver;
    /* LE */
    char       *ddb_name;
    uint32_t    ddb_obj;     /* 1-based */
    uint32_t    ddb_off;
    uint32_t    device_id;
    uint32_t    ddk_version;
} image_t;

/* The DOS stub both formats start with; returns its size (a multiple of 16). */
size_t write_stub(buf_t *out);

void ne_write(const image_t *img, buf_t *out);
void le_write(const image_t *img, buf_t *out);

/* PE: a separate path from COFF objects to a DLL (pe.c). */
void pe_link(const char *def_path, const char *out_path, const char *map_path, char **obj_paths, size_t n_paths);

/* Little-endian stores into a buffer being built. */
void put8(buf_t *b, uint32_t v);
void put16(buf_t *b, uint32_t v);
void put32(buf_t *b, uint32_t v);
void set16(buf_t *b, size_t at, uint32_t v);
void set32(buf_t *b, size_t at, uint32_t v);
void pad_to(buf_t *b, size_t align);

#endif /* VGPU98_VGLINK_H */
