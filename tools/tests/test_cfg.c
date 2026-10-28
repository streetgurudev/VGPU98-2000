/*
 * Tests for the definition-file parser.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "check.h"
#include "util.h"

static const char sample[] =
    "; leading comment\n"
    "\n"
    "[alpha]\n"
    "num   = 0x1F      ; trailing comment\n"
    "dec   = 4096\n"
    "str   = \"a;b \\\"q\\\" \\\\ end\"   # comment after a string\n"
    "bytes = 01 0a FF\n"
    "list  = 8, 16 0x20\n"
    "words = cga64 ega64\n"
    "word  = text\n"
    "\r\n"
    "[item 0x05]\n"
    "key = 7\n"
    "\n"
    "[item 0x06]\n"
    "key = 8\r\n";

int
main(void)
{
    uint32_t       value;
    uint32_t       list[4];
    uint8_t        bytes[3];
    char          *words[2];
    cfg_t         *cfg;
    cfg_section_t *sec;
    char          *str;
    int            i;
    int            items = 0;

    set_progname("test_cfg");

    CHECK(parse_u32("0", &value) && (value == 0));
    CHECK(parse_u32("255", &value) && (value == 255));
    CHECK(parse_u32("0x1F", &value) && (value == 0x1f));
    CHECK(parse_u32("0XfF", &value) && (value == 0xff));
    CHECK(parse_u32("0xFFFFFFFF", &value) && (value == 0xffffffffu));
    CHECK(!parse_u32("0x100000000", &value));
    CHECK(!parse_u32("0x", &value));
    CHECK(!parse_u32("", &value));
    CHECK(!parse_u32("12a", &value));
    CHECK(!parse_u32("-1", &value));

    write_file("test_cfg_sample.card", sample, sizeof(sample) - 1);
    cfg = cfg_load("test_cfg_sample.card");
    CHECK(cfg->n_sections == 3);

    sec = cfg_section(cfg, "alpha");
    CHECK(cfg_get_u32(cfg, sec, "num", 0, 0xff) == 0x1f);
    CHECK(cfg_get_u32(cfg, sec, "dec", 0, 65535) == 4096);
    str = cfg_get_str(cfg, sec, "str", 64);
    CHECK(strcmp(str, "a;b \"q\" \\ end") == 0);
    free(str);
    cfg_get_bytes(cfg, sec, "bytes", bytes, 3);
    CHECK((bytes[0] == 0x01) && (bytes[1] == 0x0a) && (bytes[2] == 0xff));
    CHECK(cfg_get_u32_list(cfg, sec, "list", list, 4) == 3);
    CHECK((list[0] == 8) && (list[1] == 16) && (list[2] == 32));
    CHECK(cfg_get_word_list(cfg, sec, "words", words, 2) == 2);
    CHECK((strcmp(words[0], "cga64") == 0) && (strcmp(words[1], "ega64") == 0));
    free(words[0]);
    free(words[1]);
    str = cfg_get_word(cfg, sec, "word");
    CHECK(strcmp(str, "text") == 0);
    free(str);
    CHECK(cfg_value(sec, "missing") == NULL);

    for (i = 0; i < cfg->n_sections; i++) {
        cfg_section_t *item = &cfg->sections[i];

        if (strcmp(item->name, "item") != 0)
            continue;
        item->used = 1;
        value      = cfg_section_arg_u32(cfg, item, 0, 0xff);
        CHECK(cfg_get_u32(cfg, item, "key", 0, 99) == value + 2);
        items++;
    }
    CHECK(items == 2);

    /* Every key was read, so this must return. */
    cfg_check_unused(cfg);
    cfg_free(cfg);

    CHECK_DONE();
}
