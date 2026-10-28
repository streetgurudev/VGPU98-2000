/*
 * Lines to the host's log through BAR0's DEBUG register (logline.c), for
 * vgpu98dx.dll and the Glide DLLs.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_LOGLINE_H
#define VGPU98_LOGLINE_H

#include <stdarg.h>
#include <stdint.h>

/* V in BASE (10 or 16), at least WIDTH digits, into P (up to END); the end of
   what it wrote. */
char *log_num(char *p, char *end, uint32_t v, int base, int width, int is_signed);

/* A line: PREFIX as it is (NULL for none), then FMT with %s %c %d %u %x (a
   width for any number), a newline added, at most 159 characters, written
   whole under LOCK (the shared block's log_lock, 0 free and 1 held) into
   BAR0 at REGS. */
void log_line(volatile uint32_t *regs, volatile uint32_t *lock, const char *prefix, const char *fmt, va_list ap);

#endif /* VGPU98_LOGLINE_H */
