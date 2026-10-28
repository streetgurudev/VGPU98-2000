/*
 * manifest: records what a build was made from and what it produced.
 *
 * Writes the SHA-256 and size of every input and output file, plus free-form tool
 * notes (versions). Paths are written relative to the source or build root and
 * sorted, and nothing time-dependent is recorded, so two builds of the same tree
 * produce identical manifests wherever they are run.
 *
 * Tools that live outside the tree (the assembler) are recorded by name and hash
 * with --tool, so a changed tool binary shows up even when its version string
 * does not change.
 *
 * Usage: manifest OUT --source-root DIR --build-root DIR [--note TEXT]...
 *                 [--tool NAME FILE]... [--inputs-from LIST] [--inputs FILE...]
 *                 [--outputs FILE...]
 *
 * --inputs-from reads input paths from a file, one per line, which keeps the
 * command line under the 8191 characters cmd.exe takes.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "util.h"

typedef struct list_t {
    char **items;
    int    count;
} list_t;

static void
push(list_t *list, const char *item)
{
    list->items                = xrealloc(list->items, (size_t) (list->count + 1) * sizeof(char *));
    list->items[list->count++] = xstrdup(item);
}

static int
compare(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

/* Copy of a path with backslashes turned into slashes. */
static char *
normalize(const char *path)
{
    char *copy = xstrdup(path);
    char *p;

    for (p = copy; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
    return copy;
}

static int
lower(int c)
{
    return ((c >= 'A') && (c <= 'Z')) ? c - 'A' + 'a' : c;
}

/* If `root` is a directory prefix of `path` (case-insensitive, for Windows), returns the rest. */
static const char *
strip_root(const char *path, const char *root)
{
    size_t n = strlen(root);
    size_t i;

    while ((n > 0) && (root[n - 1] == '/'))
        n--;
    for (i = 0; i < n; i++) {
        if (lower((unsigned char) path[i]) != lower((unsigned char) root[i]))
            return NULL;
    }
    return (path[n] == '/') ? path + n + 1 : NULL;
}

/* "sha256  size  relative/path" for each file, sorted by path. */
static void
hash_files(buf_t *out, const list_t *files, const char *src_root, const char *build_root)
{
    list_t lines = { 0 };
    int    i;

    for (i = 0; i < files->count; i++) {
        char       *path = normalize(files->items[i]);
        const char *rel  = strip_root(path, build_root);
        char        hex[65];
        char        line[1024];
        size_t      size;
        uint8_t    *data;

        /* The build root may sit inside the source root, so try it first. */
        if (rel == NULL)
            rel = strip_root(path, src_root);
        if (rel == NULL)
            die("%s is outside both the source and the build root", files->items[i]);

        data = read_file(files->items[i], &size);
        sha256_hex(data, size, hex);
        /* Path first so sorting orders by path; a tab separates it from the printed line. */
        snprintf(line, sizeof(line), "%s\t%s  %8u  %s\n", rel, hex, (unsigned) size, rel);
        push(&lines, line);
        free(data);
        free(path);
    }

    qsort(lines.items, (size_t) lines.count, sizeof(char *), compare);
    for (i = 0; i < lines.count; i++) {
        if ((i > 0) && (strcmp(lines.items[i], lines.items[i - 1]) == 0))
            continue; /* the same file listed twice */
        buf_printf(out, "%s", strchr(lines.items[i], '\t') + 1);
    }
}

int
main(int argc, char **argv)
{
    const char *out_path   = NULL;
    char       *src_root   = NULL;
    char       *build_root = NULL;
    list_t      notes      = { 0 };
    list_t      tools      = { 0 };
    list_t      inputs     = { 0 };
    list_t      outputs    = { 0 };
    list_t     *target     = NULL;
    buf_t       out        = { 0 };
    int         i;

    set_progname("manifest");
    for (i = 1; i < argc; i++) {
        if ((i + 1 < argc) && (strcmp(argv[i], "--source-root") == 0)) {
            src_root = normalize(argv[++i]);
            target   = NULL;
        } else if ((i + 1 < argc) && (strcmp(argv[i], "--build-root") == 0)) {
            build_root = normalize(argv[++i]);
            target     = NULL;
        } else if ((i + 1 < argc) && (strcmp(argv[i], "--note") == 0)) {
            push(&notes, argv[++i]);
            target = NULL;
        } else if ((i + 2 < argc) && (strcmp(argv[i], "--tool") == 0)) {
            char     hex[65];
            char     line[256];
            size_t   size;
            uint8_t *data = read_file(argv[i + 2], &size);

            sha256_hex(data, size, hex);
            snprintf(line, sizeof(line), "%s sha256: %s", argv[i + 1], hex);
            push(&tools, line);
            free(data);
            i += 2;
            target = NULL;
        } else if ((i + 1 < argc) && (strcmp(argv[i], "--inputs-from") == 0)) {
            char *text = (char *) read_file(argv[++i], NULL);
            char *line = text;

            while (line && *line) {
                char *end = strchr(line, '\n');

                if (end)
                    *end = 0;
                if (end && (end > line) && (end[-1] == '\r'))
                    end[-1] = 0;
                if (*line)
                    push(&inputs, line);
                line = end ? end + 1 : NULL;
            }
            free(text);
            target = NULL;
        } else if (strcmp(argv[i], "--inputs") == 0)
            target = &inputs;
        else if (strcmp(argv[i], "--outputs") == 0)
            target = &outputs;
        else if (target != NULL)
            push(target, argv[i]);
        else if ((argv[i][0] != '-') && (out_path == NULL))
            out_path = argv[i];
        else
            die("usage: manifest OUT --source-root DIR --build-root DIR [--note TEXT]... "
                "[--tool NAME FILE]... [--inputs FILE...] [--outputs FILE...]");
    }
    if (!out_path || !src_root || !build_root)
        die("usage: manifest OUT --source-root DIR --build-root DIR [--note TEXT]... "
            "[--tool NAME FILE]... [--inputs FILE...] [--outputs FILE...]");

    buf_printf(&out, "# VGPU98 build manifest. Generated by tools/manifest; do not edit.\n");
    buf_printf(&out, "# Lines: SHA-256, size in bytes, path relative to the source or build root.\n\n");
    buf_printf(&out, "[tools]\n");
    for (i = 0; i < notes.count; i++)
        buf_printf(&out, "%s\n", notes.items[i]);
    for (i = 0; i < tools.count; i++)
        buf_printf(&out, "%s\n", tools.items[i]);
    buf_printf(&out, "\n[inputs]\n");
    hash_files(&out, &inputs, src_root, build_root);
    buf_printf(&out, "\n[outputs]\n");
    hash_files(&out, &outputs, src_root, build_root);

    write_file(out_path, out.data, out.len);
    buf_free(&out);
    return 0;
}
