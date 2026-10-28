/*
 * Broken definition files the parser must reject. Each case writes one file and
 * loads it; the parser exits with an error, which CTest matches against the
 * expected message.
 *
 * Usage: test_cfg_bad CASE
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "util.h"

static const struct {
    const char *name;
    const char *text;
} cases[] = {
    { "bom", "\xef\xbb\xbf[alpha]\nkey = 1\n" },
    { "unterminated", "[alpha]\nstr = \"abc\\\"\n" },
    { "no_equals", "[alpha]\njust some words\n" },
};

int
main(int argc, char **argv)
{
    char           path[64];
    cfg_t         *cfg;
    cfg_section_t *sec;
    size_t         i;

    set_progname("test_cfg_bad");
    if (argc != 2)
        die("usage: test_cfg_bad CASE");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(argv[1], cases[i].name) != 0)
            continue;
        snprintf(path, sizeof(path), "test_cfg_bad_%s.card", cases[i].name);
        write_file(path, cases[i].text, strlen(cases[i].text));
        cfg = cfg_load(path);
        sec = cfg_section(cfg, "alpha");
        free(cfg_get_str(cfg, sec, "str", 64));
        cfg_free(cfg);
        printf("accepted\n");
        return 0;
    }
    die("unknown case %s", argv[1]);
}
