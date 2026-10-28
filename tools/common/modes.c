/*
 * Display mode table loader. See modes.h.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "modes.h"

#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "util.h"

const uint8_t mode_bpp[MODE_DEPTHS] = { 8, 15, 16, 32 };

static const char *vbe_keys[MODE_DEPTHS] = { "vbe8", "vbe15", "vbe16", "vbe32" };

uint32_t
mode_bytes_per_pixel(uint32_t bpp)
{
    return (bpp + 7) / 8;
}

uint32_t
mode_vtotal(uint32_t height)
{
    uint32_t blank = height / MODE_V_DIV;

    return height + ((blank > MODE_V_BLANK) ? blank : MODE_V_BLANK);
}

uint32_t
mode_htotal(uint32_t width)
{
    return width + MODE_H_BLANK;
}

/* "WIDTHxHEIGHT" -> width, height; 0 on a malformed name. */
static int
parse_size(const char *text, uint32_t *w, uint32_t *h)
{
    char *x;
    char *copy;
    int   ok;

    if ((text == NULL) || ((x = strchr(text, 'x')) == NULL))
        return 0;
    copy               = xstrdup(text);
    copy[x - text]     = 0;
    ok                 = parse_u32(copy, w) && parse_u32(x + 1, h);
    free(copy);
    return ok;
}

void
modes_load(const char *path, const mode_limits_t *limits, dmodes_t *out)
{
    cfg_t   *cfg = cfg_load(path);
    uint16_t used[VBE_LAST + 1 - 0x100];
    uint32_t next_oem = VBE_OEM_FIRST;
    int      s;
    size_t   i;
    size_t   d;

    memset(out, 0, sizeof(*out));
    memset(used, 0, sizeof(used));

    for (s = 0; s < cfg->n_sections; s++) {
        cfg_section_t *sec = &cfg->sections[s];
        dmode_t       *m;
        uint32_t       w;
        uint32_t       h;
        uint32_t       list[MODES_REFRESH + 1];

        if (strcmp(sec->name, "mode") != 0)
            cfg_error(cfg, sec->line, "unknown section [%s]; only [mode WIDTHxHEIGHT] sections are allowed", sec->name);
        sec->used = 1;
        if (!parse_size(sec->arg, &w, &h))
            cfg_error(cfg, sec->line, "expected [mode WIDTHxHEIGHT]");
        if ((w < 8) || (w > limits->max_width) || (w % 8))
            cfg_error(cfg, sec->line, "width %u must be a multiple of 8 from 8 to %u", w, limits->max_width);
        if ((h < 1) || (h > limits->max_height))
            cfg_error(cfg, sec->line, "height %u must be 1 to %u", h, limits->max_height);
        if (out->count == MODES_MAX)
            cfg_error(cfg, sec->line, "more than %d modes", MODES_MAX);
        for (i = 0; i < out->count; i++) {
            if ((out->mode[i].width == w) && (out->mode[i].height == h))
                cfg_error(cfg, sec->line, "%ux%u repeated (first on line %d)", w, h, out->mode[i].line);
        }

        m          = &out->mode[out->count++];
        m->width   = (uint16_t) w;
        m->height  = (uint16_t) h;
        m->line    = sec->line;
        m->desktop = cfg_has(sec, "desktop") ? (int) cfg_get_u32(cfg, sec, "desktop", 0, 1) : 0;
        m->enabled = cfg_has(sec, "enabled") ? (int) cfg_get_u32(cfg, sec, "enabled", 0, 1) : 1;

        m->n_refresh = cfg_get_u32_list(cfg, sec, "refresh", list, MODES_REFRESH);
        if (m->n_refresh == 0)
            cfg_error(cfg, cfg_line(sec, "refresh"), "'refresh' needs at least one rate");
        for (i = 0; i < m->n_refresh; i++) {
            size_t j;

            if ((list[i] < limits->refresh_min) || (list[i] > limits->refresh_max))
                cfg_error(cfg, cfg_line(sec, "refresh"), "refresh rate %u Hz is outside %u-%u", list[i],
                          limits->refresh_min, limits->refresh_max);
            for (j = 0; j < i; j++) {
                if (list[j] == list[i])
                    cfg_error(cfg, cfg_line(sec, "refresh"), "refresh rate %u Hz repeated", list[i]);
            }
            m->refresh[i] = (uint8_t) list[i];
        }

        for (d = 0; d < MODE_DEPTHS; d++) {
            const char *text = cfg_value(sec, vbe_keys[d]);
            uint32_t    n;

            if (!m->enabled) {
                if (text != NULL)
                    cfg_error(cfg, cfg_line(sec, vbe_keys[d]), "a disabled mode has no VBE numbers");
                continue;
            }
            if ((text == NULL) || (strcmp(text, "auto") == 0))
                continue; /* assigned below, after every explicit number is known */
            if (strcmp(text, "none") == 0) {
                m->vbe[d] = 0xffff; /* marker, cleared below */
                continue;
            }
            if (!parse_u32(text, &n) || (n < 0x100) || (n > VBE_LAST))
                cfg_error(cfg, cfg_line(sec, vbe_keys[d]), "'%s' must be auto, none or a number from 100h to %Xh",
                          vbe_keys[d], VBE_LAST);
            if (used[n - 0x100])
                cfg_error(cfg, cfg_line(sec, vbe_keys[d]), "VBE mode %Xh used twice", n);
            used[n - 0x100] = 1;
            m->vbe[d]       = (uint16_t) n;
        }
    }

    /* OEM numbers for "auto", in file order, skipping explicit ones. */
    for (i = 0; i < out->count; i++) {
        dmode_t *m = &out->mode[i];

        for (d = 0; d < MODE_DEPTHS; d++) {
            if (!m->enabled)
                continue;
            if (m->vbe[d] == 0xffff) {
                m->vbe[d] = 0;
                continue;
            }
            if (m->vbe[d] != 0)
                continue;
            while ((next_oem <= VBE_LAST) && used[next_oem - 0x100])
                next_oem++;
            if (next_oem > VBE_LAST)
                cfg_error(cfg, m->line, "out of OEM VBE mode numbers");
            used[next_oem - 0x100] = 1;
            m->vbe[d]              = (uint16_t) next_oem++;
        }
    }

    if (out->count == 0)
        cfg_error(cfg, 0, "no modes defined");
    cfg_check_unused(cfg);
    cfg_free(cfg);
}
