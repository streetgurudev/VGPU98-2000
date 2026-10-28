/*
 * VGPU98 authoring tools: shared helpers.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_UTIL_H
#define VGPU98_UTIL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#    define VGPU98_PRINTF(f, a) __attribute__((format(printf, f, a)))
#    define VGPU98_NORETURN     __attribute__((noreturn))
#else
#    define VGPU98_PRINTF(f, a)
#    define VGPU98_NORETURN
#endif

/* Name printed in front of every error message. */
void set_progname(const char *name);

/* Prints "progname: message" to stderr and exits with status 1. */
VGPU98_NORETURN void die(const char *fmt, ...) VGPU98_PRINTF(1, 2);

void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *str);

/* Reads a whole file in binary mode. The buffer has one extra NUL byte after the data. */
uint8_t *read_file(const char *path, size_t *size);

/* Writes a file in binary mode. Leaves the file untouched if it already holds exactly these bytes. */
void write_file(const char *path, const void *data, size_t size);

/* Growable byte buffer used to build output files. */
typedef struct buf_t {
    char  *data;
    size_t len;
    size_t cap;
} buf_t;

void buf_append(buf_t *buf, const void *data, size_t size);
void buf_printf(buf_t *buf, const char *fmt, ...) VGPU98_PRINTF(2, 3);
void buf_free(buf_t *buf);

#endif /* VGPU98_UTIL_H */
