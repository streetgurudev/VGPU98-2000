/*
 * A line to the host's log through BAR0's DEBUG register, as every 32-bit
 * guest driver writes it: vgpu98dx.dll and the Glide DLLs,
 * so their lines look alike and take the same
 * lock. Freestanding: no other part of either DLL is needed.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "logline.h"

#include "vid_vgpu98_card.h"

/* A character into the host's line. The Glide DLLs' host test
   (tools/tests/test_glide.c) defines LOG_PUT_HOOK to take the characters
   itself, so it reads the lines as this writer makes them. */
#ifdef LOG_PUT_HOOK
void LOG_PUT_HOOK(volatile uint32_t *regs, char c);
#    define LOG_PUT(regs, c) LOG_PUT_HOOK(regs, c)
#else
#    define LOG_PUT(regs, c) ((regs)[VGPU98_REG_DEBUG / 4] = (uint8_t) (c))
#endif

char *
log_num(char *p, char *end, uint32_t v, int base, int width, int is_signed)
{
    char tmp[12];
    int  n   = 0;
    int  neg = is_signed && ((int32_t) v < 0);

    if (neg)
        v = (uint32_t) -(int32_t) v;
    do {
        uint32_t d = v % (uint32_t) base;

        tmp[n++] = (char) ((d < 10) ? ('0' + d) : ('A' + d - 10));
        v /= (uint32_t) base;
    } while (v && (n < (int) sizeof(tmp)));
    if (neg && (p < end))
        *p++ = '-';
    while ((width-- > n) && (p < end))
        *p++ = '0';
    while (n && (p < end))
        *p++ = tmp[--n];
    return p;
}

void
log_line(volatile uint32_t *regs, volatile uint32_t *lock, const char *prefix, const char *fmt, va_list ap)
{
    char        line[160];
    char       *p   = line;
    char       *end = line + sizeof(line) - 1;
    const char *s;

    for (s = prefix; s && *s && (p < end); s++)
        *p++ = *s;
    while (*fmt && (p < end)) {
        int width = 0;

        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        fmt++;
        while ((*fmt >= '0') && (*fmt <= '9'))
            width = width * 10 + (*fmt++ - '0');
        if (!*fmt)
            break; /* a format ending in '%': nothing more to read */
        switch (*fmt++) {
            case 's':
                for (s = va_arg(ap, const char *); *s && (p < end); s++)
                    *p++ = *s;
                break;
            case 'c':
                *p++ = (char) va_arg(ap, int);
                break;
            case 'd':
                p = log_num(p, end, va_arg(ap, uint32_t), 10, width, 1);
                break;
            case 'u':
                p = log_num(p, end, va_arg(ap, uint32_t), 10, width, 0);
                break;
            case 'x':
                p = log_num(p, end, va_arg(ap, uint32_t), 16, width, 0);
                break;
            default:
                *p++ = '?';
                break;
        }
    }
    *p++ = '\n';
    /* One line at a time into the host's line (threads and processes share
       it): a short spin, then the lock is taken over, so a holder that died
       never blocks the log for good (its line is lost; one still writing
       may interleave with this one). */
    {
        uint32_t k;

        for (k = 0; (k < 100000) && __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE); k++)
            ;
        for (s = line; s < p; s++)
            LOG_PUT(regs, *s);
        __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    }
}
