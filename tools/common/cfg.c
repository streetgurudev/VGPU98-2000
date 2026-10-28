/*
 * VGPU98 authoring tools: parser for the project's INI-style definition files.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

static int
is_ident(char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-');
}

static int
hex_digit(char c)
{
    if ((c >= '0') && (c <= '9'))
        return c - '0';
    if ((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;
    if ((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;
    return -1;
}

int
parse_u32(const char *text, uint32_t *out)
{
    uint64_t value = 0;
    int      base  = 10;

    if ((text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X'))) {
        base = 16;
        text += 2;
    }
    if (*text == 0)
        return 0;

    for (; *text; text++) {
        int digit = hex_digit(*text);

        if ((digit < 0) || (digit >= base))
            return 0;
        value = value * (uint64_t) base + (uint64_t) digit;
        if (value > 0xffffffffu)
            return 0;
    }

    *out = (uint32_t) value;
    return 1;
}

void
cfg_error(const cfg_t *cfg, int line, const char *fmt, ...)
{
    char    msg[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (line > 0)
        die("%s:%d: %s", cfg->path, line, msg);
    die("%s: %s", cfg->path, msg);
}

/* Strips a comment that starts outside a quoted string, then surrounding whitespace. */
static char *
clean_line(char *line)
{
    int   quoted = 0;
    char *p;
    char *end;

    for (p = line; *p; p++) {
        if ((*p == '\\') && quoted && p[1]) {
            p++;
            continue;
        }
        if (*p == '"')
            quoted = !quoted;
        else if (!quoted && ((*p == ';') || (*p == '#'))) {
            *p = 0;
            break;
        }
    }

    while (is_space(*line))
        line++;
    end = line + strlen(line);
    while ((end > line) && (is_space(end[-1]) || (end[-1] == '\r')))
        *--end = 0;
    return line;
}

cfg_t *
cfg_load(const char *path)
{
    cfg_t         *cfg = xcalloc(1, sizeof(cfg_t));
    char          *text;
    char          *line;
    int            line_no = 0;
    cfg_section_t *cur     = NULL;

    cfg->path = xstrdup(path);
    text      = (char *) read_file(path, NULL);
    if ((unsigned char) text[0] == 0xef && (unsigned char) text[1] == 0xbb && (unsigned char) text[2] == 0xbf)
        cfg_error(cfg, 1, "the file starts with a UTF-8 byte order mark; save it as plain ASCII");

    for (line = text; line != NULL;) {
        char *next = strchr(line, '\n');
        char *p;

        if (next != NULL)
            *next++ = 0;
        line_no++;
        p = clean_line(line);
        line = next;

        if (*p == 0)
            continue;

        if (*p == '[') {
            char *close = strchr(p, ']');
            char *name  = p + 1;
            char *arg;

            if ((close == NULL) || (close[1] != 0))
                cfg_error(cfg, line_no, "malformed section header");
            *close = 0;

            for (arg = name; is_ident(*arg); arg++)
                ;
            if (arg == name)
                cfg_error(cfg, line_no, "section name must be lower-case letters, digits, '_' or '-'");
            if (*arg && !is_space(*arg))
                cfg_error(cfg, line_no, "invalid character in section name");
            if (*arg) {
                *arg++ = 0;
                while (is_space(*arg))
                    arg++;
            }

            cfg->sections = xrealloc(cfg->sections, (size_t) (cfg->n_sections + 1) * sizeof(cfg_section_t));
            cur           = &cfg->sections[cfg->n_sections++];
            memset(cur, 0, sizeof(*cur));
            cur->name = xstrdup(name);
            cur->arg  = *arg ? xstrdup(arg) : NULL;
            cur->line = line_no;
            continue;
        }

        {
            char *eq = strchr(p, '=');
            char *key_end;
            char *value;
            int   i;

            if (eq == NULL)
                cfg_error(cfg, line_no, "expected 'key = value', found '%.40s'", p);
            if (cur == NULL)
                cfg_error(cfg, line_no, "key outside any section");

            for (key_end = p; is_ident(*key_end); key_end++)
                ;
            if (key_end == p)
                cfg_error(cfg, line_no, "key must be lower-case letters, digits, '_' or '-'");
            value = eq + 1;
            while (is_space(*value))
                value++;
            while (is_space(*key_end))
                *key_end++ = 0;
            if (key_end != eq)
                cfg_error(cfg, line_no, "invalid character in key");
            *eq = 0;
            if (*value == 0)
                cfg_error(cfg, line_no, "empty value for '%s'", p);

            for (i = 0; i < cur->n_entries; i++) {
                if (strcmp(cur->entries[i].key, p) == 0)
                    cfg_error(cfg, line_no, "'%s' already set on line %d", p, cur->entries[i].line);
            }

            cur->entries = xrealloc(cur->entries, (size_t) (cur->n_entries + 1) * sizeof(cfg_entry_t));
            cur->entries[cur->n_entries].key   = xstrdup(p);
            cur->entries[cur->n_entries].value = xstrdup(value);
            cur->entries[cur->n_entries].line  = line_no;
            cur->entries[cur->n_entries].used  = 0;
            cur->n_entries++;
        }
    }

    free(text);
    return cfg;
}

void
cfg_free(cfg_t *cfg)
{
    int i;
    int j;

    if (cfg == NULL)
        return;
    for (i = 0; i < cfg->n_sections; i++) {
        for (j = 0; j < cfg->sections[i].n_entries; j++) {
            free(cfg->sections[i].entries[j].key);
            free(cfg->sections[i].entries[j].value);
        }
        free(cfg->sections[i].entries);
        free(cfg->sections[i].name);
        free(cfg->sections[i].arg);
    }
    free(cfg->sections);
    free(cfg->path);
    free(cfg);
}

cfg_section_t *
cfg_section(cfg_t *cfg, const char *name)
{
    cfg_section_t *found = NULL;
    int            i;

    for (i = 0; i < cfg->n_sections; i++) {
        cfg_section_t *sec = &cfg->sections[i];

        if (strcmp(sec->name, name) != 0)
            continue;
        if (sec->arg != NULL)
            cfg_error(cfg, sec->line, "section [%s] takes no argument", name);
        if (found != NULL)
            cfg_error(cfg, sec->line, "section [%s] repeated (first on line %d)", name, found->line);
        found = sec;
    }
    if (found == NULL)
        cfg_error(cfg, 0, "missing section [%s]", name);

    found->used = 1;
    return found;
}

uint32_t
cfg_section_arg_u32(const cfg_t *cfg, const cfg_section_t *sec, uint32_t min, uint32_t max)
{
    uint32_t value;

    if ((sec->arg == NULL) || !parse_u32(sec->arg, &value))
        cfg_error(cfg, sec->line, "section [%s] needs a number argument", sec->name);
    if ((value < min) || (value > max))
        cfg_error(cfg, sec->line, "section [%s %s]: number out of range 0x%X-0x%X", sec->name, sec->arg, min, max);
    return value;
}

static cfg_entry_t *
find_entry(const cfg_section_t *sec, const char *key)
{
    int i;

    for (i = 0; i < sec->n_entries; i++) {
        if (strcmp(sec->entries[i].key, key) == 0)
            return &sec->entries[i];
    }
    return NULL;
}

const char *
cfg_value(cfg_section_t *sec, const char *key)
{
    cfg_entry_t *entry = find_entry(sec, key);

    sec->used = 1;
    if (entry == NULL)
        return NULL;
    entry->used = 1;
    return entry->value;
}

int
cfg_has(const cfg_section_t *sec, const char *key)
{
    return find_entry(sec, key) != NULL;
}

int
cfg_line(const cfg_section_t *sec, const char *key)
{
    const cfg_entry_t *entry = find_entry(sec, key);

    return entry ? entry->line : sec->line;
}

static const char *
required(const cfg_t *cfg, cfg_section_t *sec, const char *key)
{
    const char *value = cfg_value(sec, key);

    if (value == NULL) {
        if (sec->arg != NULL)
            cfg_error(cfg, sec->line, "[%s %s] is missing '%s'", sec->name, sec->arg, key);
        cfg_error(cfg, sec->line, "[%s] is missing '%s'", sec->name, key);
    }
    return value;
}

uint32_t
cfg_get_u32(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint32_t min, uint32_t max)
{
    const char *text = required(cfg, sec, key);
    uint32_t    value;

    if (!parse_u32(text, &value))
        cfg_error(cfg, cfg_line(sec, key), "'%s' must be a decimal or 0x-prefixed number", key);
    if ((value < min) || (value > max))
        cfg_error(cfg, cfg_line(sec, key), "'%s' = %s is out of range %u-%u", key, text, min, max);
    return value;
}

char *
cfg_get_str(const cfg_t *cfg, cfg_section_t *sec, const char *key, size_t max_len)
{
    const char *text = required(cfg, sec, key);
    int         line = cfg_line(sec, key);
    size_t      len  = strlen(text);
    char       *out;
    size_t      n = 0;
    size_t      i;

    if ((len < 2) || (text[0] != '"') || (text[len - 1] != '"'))
        cfg_error(cfg, line, "'%s' must be a quoted string", key);

    out = xmalloc(len);
    for (i = 1; i < len - 1; i++) {
        char c = text[i];

        if (c == '\\') {
            if (i + 1 >= len - 1)
                cfg_error(cfg, line, "'%s': unterminated string (the closing quote is escaped)", key);
            c = text[++i];
            if ((c != '\\') && (c != '"'))
                cfg_error(cfg, line, "'%s': only \\\\ and \\\" escapes are allowed", key);
        } else if (c == '"')
            cfg_error(cfg, line, "'%s': unescaped quote inside string", key);
        if ((c < 0x20) || (c > 0x7e))
            cfg_error(cfg, line, "'%s': strings must be printable ASCII", key);
        out[n++] = c;
    }
    out[n] = 0;

    if (n > max_len)
        cfg_error(cfg, line, "'%s' is %u characters long; the limit is %u", key, (unsigned) n, (unsigned) max_len);
    return out;
}

char *
cfg_get_word(const cfg_t *cfg, cfg_section_t *sec, const char *key)
{
    const char *text = required(cfg, sec, key);
    const char *p;

    for (p = text; *p; p++) {
        if (!is_ident(*p))
            cfg_error(cfg, cfg_line(sec, key), "'%s' must be a single word", key);
    }
    return xstrdup(text);
}

/* Splits a value into whitespace- or comma-separated tokens. Stores at most `max`
   of them and returns how many there are in all, so callers can report the real
   count. */
static size_t
tokens(char *text, char **out, size_t max)
{
    size_t n = 0;
    char  *p = text;

    for (;;) {
        while (is_space(*p) || (*p == ','))
            p++;
        if (*p == 0)
            break;
        if (n < max)
            out[n] = p;
        n++;
        while (*p && !is_space(*p) && (*p != ','))
            p++;
        if (*p)
            *p++ = 0;
    }
    return n;
}

void
cfg_get_bytes(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint8_t *out, size_t count)
{
    char  *copy = xstrdup(required(cfg, sec, key));
    int    line = cfg_line(sec, key);
    char **tok  = xmalloc((count + 1) * sizeof(char *));
    size_t n    = tokens(copy, tok, count);
    size_t i;

    if (n != count)
        cfg_error(cfg, line, "'%s' needs exactly %u bytes, found %u", key, (unsigned) count, (unsigned) n);
    for (i = 0; i < n; i++) {
        int hi = hex_digit(tok[i][0]);
        int lo = (hi >= 0) ? hex_digit(tok[i][1]) : -1;

        if ((hi < 0) || (lo < 0) || (tok[i][2] != 0))
            cfg_error(cfg, line, "'%s': '%s' is not a two-digit hex byte", key, tok[i]);
        out[i] = (uint8_t) ((hi << 4) | lo);
    }

    free(tok);
    free(copy);
}

size_t
cfg_get_u32_list(const cfg_t *cfg, cfg_section_t *sec, const char *key, uint32_t *out, size_t max)
{
    char  *copy = xstrdup(required(cfg, sec, key));
    int    line = cfg_line(sec, key);
    char **tok  = xmalloc((max + 1) * sizeof(char *));
    size_t n    = tokens(copy, tok, max);
    size_t i;

    if (n > max)
        cfg_error(cfg, line, "'%s' has %u items; the limit is %u", key, (unsigned) n, (unsigned) max);
    for (i = 0; i < n; i++) {
        if (!parse_u32(tok[i], &out[i]))
            cfg_error(cfg, line, "'%s': '%s' is not a number", key, tok[i]);
    }

    free(tok);
    free(copy);
    return n;
}

size_t
cfg_get_word_list(const cfg_t *cfg, cfg_section_t *sec, const char *key, char **out, size_t max)
{
    char  *copy = xstrdup(required(cfg, sec, key));
    int    line = cfg_line(sec, key);
    char **tok  = xmalloc((max + 1) * sizeof(char *));
    size_t n    = tokens(copy, tok, max);
    size_t i;

    if (n > max)
        cfg_error(cfg, line, "'%s' has %u items; the limit is %u", key, (unsigned) n, (unsigned) max);
    for (i = 0; i < n; i++) {
        const char *p;

        for (p = tok[i]; *p; p++) {
            if (!is_ident(*p))
                cfg_error(cfg, line, "'%s': '%s' is not a single word", key, tok[i]);
        }
        out[i] = xstrdup(tok[i]);
    }

    free(tok);
    free(copy);
    return n;
}

void
cfg_check_unused(const cfg_t *cfg)
{
    int i;
    int j;

    for (i = 0; i < cfg->n_sections; i++) {
        const cfg_section_t *sec = &cfg->sections[i];

        if (!sec->used)
            cfg_error(cfg, sec->line, "unknown section [%s]", sec->name);
        for (j = 0; j < sec->n_entries; j++) {
            if (!sec->entries[j].used)
                cfg_error(cfg, sec->entries[j].line, "unknown key '%s' in [%s]", sec->entries[j].key, sec->name);
        }
    }
}
