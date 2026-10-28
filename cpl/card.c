/*
 * vgpu98cp.cpl: the card, found through the display driver's escape as the
 * Glide DLLs find it (VGPU98_ESC_INFO: the shared block, then BAR0); its
 * VSYNC_CONTROL register; and lines to 86Box's
 * log through BAR0's DEBUG register (logline.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cpl.h"
#include "logline.h"
#include "shared.h"
#include "stamp.h"
#include "vid_vgpu98_card.h"

static cp_card_t        card;
static int              looked;
static vgpu98_shared_t *shared;    /* the display driver's, of this build's layout */
static volatile DWORD  *regs;      /* BAR0 */
static int              logging;   /* lines go to the log */
static volatile DWORD  *log_lock;  /* the shared block's */
static char             prefix[64];

/* "VGPU98 control <stamp> <program>: ", the program's file name cut to 20
   characters, as the other DLLs' lines start. */
static void
make_prefix(void)
{
    static const char tag[] = "VGPU98 control " VGPU98_BUILD_STAMP " ";
    char              path[260];
    const char       *name = path;
    const char       *s;
    size_t            n = 0, k = 0;

    if (!GetModuleFileNameA(NULL, path, sizeof(path)))
        path[0] = 0;
    path[sizeof(path) - 1] = 0;
    for (s = path; *s; s++) {
        if ((*s == '\\') || (*s == '/') || (*s == ':'))
            name = s + 1;
    }
    for (s = tag; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    for (s = name; *s && (k < 20) && (n + 1 < sizeof(prefix)); s++, k++)
        prefix[n++] = *s;
    if (!k && (n + 1 < sizeof(prefix)))
        prefix[n++] = '?';
    for (s = ": "; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    prefix[n] = 0;
}

static void
look(void)
{
    vgpu98_escquery_t query;
    vgpu98_escinfo_t  info;
    vgpu98_shared_t  *s   = NULL;
    const int         esc = VGPU98_ESC_INFO;
    HDC               dc  = CreateDCA("DISPLAY", NULL, NULL, NULL);
    volatile DWORD   *r;
    int               i;

    make_prefix();
    if (!dc)
        return;
    query.magic    = ESC_INFO_MAGIC;
    query.out_size = sizeof(info);
    memset(&info, 0, sizeof(info));
    if ((ExtEscape(dc, QUERYESCSUPPORT, sizeof(esc), (const char *) &esc, 0, NULL) > 0) &&
        (ExtEscape(dc, VGPU98_ESC_INFO, sizeof(query), (const char *) &query, sizeof(info), (char *) &info) > 0) &&
        (info.magic == ESC_INFO_MAGIC) && (info.size == sizeof(info)))
        s = (vgpu98_shared_t *) info.shared_linear;
    DeleteDC(dc);
    if (!s || (s->magic != SB_MAGIC))
        return; /* not the VGPU98 2000's display driver */
    card.found = 1;
    for (i = 0; (i < (int) sizeof(card.stamp) - 1) && info.stamp[i]; i++)
        card.stamp[i] = info.stamp[i];
    /* The block's first fields keep their offsets in every version; the
       rest only in this build's. */
    if ((s->version == SB_VERSION) && (s->size >= sizeof(*s)))
        shared = s;
    r = (volatile DWORD *) s->regs_linear;
    if (!r || (r[VGPU98_REG_ID / 4] != VGPU98_ID_MAGIC))
        return;
    regs       = r;
    card.bar0  = 1;
    card.gamma = (r[VGPU98_REG_PROFILE / 4] & VGPU98_PROFILE_GAMMA) != 0;
    card.vsync = (r[VGPU98_REG_PROFILE / 4] & VGPU98_PROFILE_VSYNC) != 0;
    /* Read once: the level changes only with the machine's reset. */
    if (r[VGPU98_REG_DEBUG / 4] & (VGPU98_DEBUG_LISTENING | VGPU98_DEBUG_EVENTS)) {
        logging  = 1;
        log_lock = &s->log_lock;
    }
}

const cp_card_t *
cp_card(void)
{
    if (!looked) {
        looked = 1;
        look();
    }
    return &card;
}

DWORD
cp_refresh(void)
{
    cp_card();
    return shared ? shared->mode_refresh : 0;
}

/* The fields only: the card ignores the rest. A card without PROFILE.VSYNC
   ignores the write and reads 0. */
DWORD
cp_vsync_write(DWORD control)
{
    cp_card();
    if (!regs || !card.vsync)
        return 0;
    regs[VGPU98_REG_VSYNC_CONTROL / 4] = control;
    return regs[VGPU98_REG_VSYNC_CONTROL / 4];
}

DWORD
cp_vsync_read(void)
{
    cp_card();
    return (regs && card.vsync) ? regs[VGPU98_REG_VSYNC_CONTROL / 4] : 0;
}

void
cp_event(const char *fmt, ...)
{
    va_list ap;

    cp_card();
    if (!logging)
        return;
    va_start(ap, fmt);
    log_line((volatile uint32_t *) regs, (volatile uint32_t *) log_lock, prefix, fmt, ap);
    va_end(ap);
}
