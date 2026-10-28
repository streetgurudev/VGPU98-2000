/*
 * The DLL's runtime: what gcc expects of a C library and of libgcc when it
 * compiles freestanding 32-bit code. The DLL links neither (it imports from
 * KERNEL32 only), so these are its own.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdint.h>

/* gcc turns loops into calls to these; keep them from turning these loops
   into calls to themselves. */
#define NO_BUILTIN __attribute__((optimize("no-tree-loop-distribute-patterns")))

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* Dwords, then the bytes left: a quarter of the iterations on the emulated
   CPU (a forward copy, safe for memmove's case too). */
NO_BUILTIN void *
memcpy(void *dst, const void *src, size_t n)
{
    void        *ret   = dst;
    size_t       words = n >> 2;
    const size_t tail  = n & 3;

    __asm__ volatile("cld\n\t"
                     "rep movsl\n\t"
                     "movl %3, %%ecx\n\t"
                     "rep movsb"
                     : "+D"(dst), "+S"(src), "+c"(words)
                     : "rm"(tail)
                     : "memory");
    return ret;
}

NO_BUILTIN void *
memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = dst;
    const uint8_t *s = src;

    if ((d <= s) || (d >= s + n))
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n];
    return dst;
}

NO_BUILTIN void *
memset(void *dst, int c, size_t n)
{
    void *ret = dst;

    __asm__ volatile("cld\n\t"
                     "rep stosb"
                     : "+D"(dst), "+c"(n)
                     : "a"(c)
                     : "memory");
    return ret;
}

NO_BUILTIN int
memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *p = a;
    const uint8_t *q = b;
    size_t         i;

    for (i = 0; i < n; i++) {
        if (p[i] != q[i])
            return (p[i] < q[i]) ? -1 : 1;
    }
    return 0;
}

/* 64-bit division (libgcc's names). Shift and subtract: the DLL divides
   64-bit numbers rarely. */
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem);
uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);
int64_t  __divdi3(int64_t n, int64_t d);
int64_t  __moddi3(int64_t n, int64_t d);

uint64_t
__udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0;
    uint64_t r = 0;
    int      i;

    if (d == 0) {
        volatile int zero = 0;

        return (uint64_t) (1 / zero); /* the same fault as a 32-bit division by zero */
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

uint64_t
__udivdi3(uint64_t n, uint64_t d)
{
    return __udivmoddi4(n, d, NULL);
}

uint64_t
__umoddi3(uint64_t n, uint64_t d)
{
    uint64_t r = 0;

    __udivmoddi4(n, d, &r);
    return r;
}

int64_t
__divdi3(int64_t n, int64_t d)
{
    int      neg = (n < 0) != (d < 0);
    uint64_t q   = __udivmoddi4((n < 0) ? -(uint64_t) n : (uint64_t) n, (d < 0) ? -(uint64_t) d : (uint64_t) d, NULL);

    return neg ? -(int64_t) q : (int64_t) q;
}

int64_t
__moddi3(int64_t n, int64_t d)
{
    uint64_t r = 0;

    __udivmoddi4((n < 0) ? -(uint64_t) n : (uint64_t) n, (d < 0) ? -(uint64_t) d : (uint64_t) d, &r);
    return (n < 0) ? -(int64_t) r : (int64_t) r;
}
