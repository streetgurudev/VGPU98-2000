/*
 * VGPU98 authoring tools: parser for the project's INI-style definition files.
 *
 * Format (see spec/card-definition.md):
 *   ; comment              # comment
 *   [section]              [section argument]
 *   key = value
 *
 * Every getter marks its key as used. cfg_check_unused() then reports any key
 * nobody asked for, which catches typos in definition files.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CFG_H
#define VGPU98_CFG_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

typedef struct cfg_entry_t {
    char *key;
    char *value;
    int   line;
    int   used;
} cfg_entry_t;

typedef struct cfg_section_t {
    char        *name;
    char        *arg; /* text after the name inside the brackets, or NULL */
    int          line;
    int          used;
    cfg_entry_t *entries;
    int          n_entries;
} cfg_section_t;

typedef struct cfg_t {
    char          *path;
    cfg_section_t *sections;
    int            n_sections;
} cfg_t;

cfg_t *cfg_load(const char *path);
void   cfg_free(cfg_t *cfg);

/* Reports an error at a line of the file and exits. */
VGPU98_NORETURN void cfg_error(const cfg_t *cfg, int line, const char *fmt, ...) VGPU98_PRINTF(3, 4);

/* The one section with this name and no argument. Exits if it is missing or repeated. */
cfg_section_t *cfg_section(cfg_t *cfg, const char *name);

/* Parses the section argument as an unsigned number within [min, max]. */
uint32_t cfg_section_arg_u32(const cfg_t *cfg, const cfg_section_t *sec, uint32_t min, uint32_t max);

/* Raw value, or NULL if the key is absent. Marks the key as used. */
const char *cfg_value(cfg_section_t *sec, const char *key);
int         cfg_has(const cfg_section_t *sec, const char *key);
int         cfg_line(const cfg_section_t *sec, const char *key);

/* Required values. Each exits with a file:line message when the value is missing or invalid. */
uint32_t cfg_get_u32(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint32_t min, uint32_t max);
char    *cfg_get_str(const cfg_t *cfg, cfg_section_t *sec, const char *key, size_t max_len);
char    *cfg_get_word(const cfg_t *cfg, cfg_section_t *sec, const char *key);
void     cfg_get_bytes(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint8_t *out, size_t count);
size_t   cfg_get_u32_list(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint32_t *out, size_t max);
size_t   cfg_get_word_list(const cfg_t *cfg, cfg_section_t *sec, const char *key, char **out, size_t max);

/* Exits if any key or section was never read. */
void cfg_check_unused(const cfg_t *cfg);

/* Parses a decimal or 0x-prefixed hexadecimal number. Returns 0 on failure. */
int parse_u32(const char *text, uint32_t *out);

#endif /* VGPU98_CFG_H */
