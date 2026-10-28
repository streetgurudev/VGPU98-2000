/*
 * vglink: reader for the OMF object files NASM writes with -f obj.
 *
 * Supported records: THEADR, COMENT (ignored, except NASM's import/export
 * directives, which are rejected), LNAMES, SEGDEF, GRPDEF, EXTDEF, PUBDEF,
 * LEDATA, FIXUPP (with threads), LINNUM (ignored), MODEND, in their 16- and
 * 32-bit forms. Anything else is an error. Format notes: spec/vglink.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_OMF_H
#define VGPU98_OMF_H

#include <stddef.h>
#include <stdint.h>

/* Fixup location types (OMF LOCAT field). */
enum {
    LOC_LOBYTE   = 0,
    LOC_OFFSET16 = 1,
    LOC_BASE     = 2,
    LOC_PTR1616  = 3,
    LOC_HIBYTE   = 4,
    LOC_LOFF16   = 5,  /* loader-resolved offset, treated as LOC_OFFSET16 */
    LOC_OFFSET32 = 9,
    LOC_PTR1632  = 11,
    LOC_LOFF32   = 13  /* loader-resolved offset, treated as LOC_OFFSET32 */
};

/* What a fixup refers to. */
enum { REF_SEGMENT, REF_GROUP, REF_EXTERN, REF_ABSOLUTE };

typedef struct omf_ref_t {
    int      kind;  /* REF_* */
    uint32_t index; /* 0-based segment, group or extern number in its object; frame number for REF_ABSOLUTE */
} omf_ref_t;

typedef struct omf_fixup_t {
    uint32_t  offset;        /* location, from the start of the segment piece */
    int       loc;           /* LOC_* */
    int       self_relative; /* M = 0 */
    omf_ref_t frame;         /* REF_SEGMENT, REF_GROUP or REF_EXTERN; kind -1 = the target's own frame */
    omf_ref_t target;
    int32_t   disp;          /* target displacement (0 when the record has none) */
    int       line;          /* record number in the file, for messages */
} omf_fixup_t;

typedef struct omf_seg_t {
    char        *name;
    char        *class_name;
    uint32_t     length;
    uint32_t     align;  /* bytes: 1, 2, 4, 16, 256 or 4096 */
    int          use32;
    int          is_flat; /* member of the FLAT group */
    uint8_t     *data;    /* length bytes, zero where nothing was written */
    omf_fixup_t *fixups;
    size_t       n_fixups;
} omf_seg_t;

typedef struct omf_pub_t {
    char    *name;
    uint32_t seg; /* 0-based */
    uint32_t offset;
} omf_pub_t;

typedef struct omf_t {
    char      *path;
    char     **names;    /* LNAMES, 0-based (OMF index - 1) */
    size_t     n_names;
    omf_seg_t *segs;
    size_t     n_segs;
    char     **groups;   /* group names */
    size_t     n_groups;
    char     **externs;
    size_t     n_externs;
    omf_pub_t *pubs;
    size_t     n_pubs;
} omf_t;

/* Reads and checks an object file; exits with "path: message" on any error. */
omf_t *omf_read(const char *path);

#endif /* VGPU98_OMF_H */
