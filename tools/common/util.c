/*
 * VGPU98 authoring tools: shared helpers.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "vgpu98";

void
set_progname(const char *name)
{
    progname = name;
}

void
die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void *
xmalloc(size_t size)
{
    void *ptr = malloc(size ? size : 1);

    if (ptr == NULL)
        die("out of memory");
    return ptr;
}

void *
xcalloc(size_t count, size_t size)
{
    void *ptr = calloc(count ? count : 1, size ? size : 1);

    if (ptr == NULL)
        die("out of memory");
    return ptr;
}

void *
xrealloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size ? size : 1);
    if (ptr == NULL)
        die("out of memory");
    return ptr;
}

char *
xstrdup(const char *str)
{
    size_t len  = strlen(str) + 1;
    char  *copy = xmalloc(len);

    memcpy(copy, str, len);
    return copy;
}

uint8_t *
read_file(const char *path, size_t *size)
{
    FILE    *fp = fopen(path, "rb");
    uint8_t *data;
    size_t   len = 0;
    size_t   cap = 65536;

    if (fp == NULL)
        die("cannot open %s", path);

    data = xmalloc(cap + 1);
    for (;;) {
        size_t got = fread(data + len, 1, cap - len, fp);

        len += got;
        if (len < cap) {
            if (ferror(fp))
                die("cannot read %s", path);
            break;
        }
        cap *= 2;
        data = xrealloc(data, cap + 1);
    }
    fclose(fp);

    data[len] = 0;
    if (size != NULL)
        *size = len;
    return data;
}

void
write_file(const char *path, const void *data, size_t size)
{
    FILE *fp = fopen(path, "rb");

    if (fp != NULL) {
        size_t   old_size;
        uint8_t *old;

        fclose(fp);
        old = read_file(path, &old_size);
        if ((old_size == size) && (memcmp(old, data, size) == 0)) {
            free(old);
            return;
        }
        free(old);
    }

    fp = fopen(path, "wb");
    if (fp == NULL)
        die("cannot create %s", path);
    if ((size != 0) && (fwrite(data, 1, size, fp) != size))
        die("cannot write %s", path);
    if (fclose(fp) != 0)
        die("cannot write %s", path);
}

void
buf_append(buf_t *buf, const void *data, size_t size)
{
    if (buf->len + size + 1 > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096;

        while (buf->len + size + 1 > cap)
            cap *= 2;
        buf->data = xrealloc(buf->data, cap);
        buf->cap  = cap;
    }
    memcpy(buf->data + buf->len, data, size);
    buf->len += size;
    buf->data[buf->len] = 0;
}

void
buf_printf(buf_t *buf, const char *fmt, ...)
{
    va_list ap;
    int     len;
    char   *tmp;

    va_start(ap, fmt);
    len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (len < 0)
        die("formatting error");

    tmp = xmalloc((size_t) len + 1);
    va_start(ap, fmt);
    vsnprintf(tmp, (size_t) len + 1, fmt, ap);
    va_end(ap);

    buf_append(buf, tmp, (size_t) len);
    free(tmp);
}

void
buf_free(buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len  = 0;
    buf->cap  = 0;
}
