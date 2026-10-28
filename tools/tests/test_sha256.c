/*
 * SHA-256 against the FIPS 180 example vectors.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "sha256.h"

static int
matches(const char *msg, size_t len, const char *expect)
{
    char hex[65];

    sha256_hex(msg, len, hex);
    return strcmp(hex, expect) == 0;
}

int
main(void)
{
    static const char two_block[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    char             *million     = malloc(1000000);
    sha256_t          ctx;
    uint8_t           digest[32];
    size_t            i;

    CHECK(matches("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(matches("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(matches(two_block, strlen(two_block), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    memset(million, 'a', 1000000);
    CHECK(matches(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    /* The same message fed in uneven pieces must hash identically. */
    sha256_init(&ctx);
    for (i = 0; i < 1000000;) {
        size_t n = (i % 7) + 1;

        if (i + n > 1000000)
            n = 1000000 - i;
        sha256_update(&ctx, million + i, n);
        i += n;
    }
    sha256_final(&ctx, digest);
    CHECK((digest[0] == 0xcd) && (digest[1] == 0xc7) && (digest[31] == 0xd0));

    free(million);
    CHECK_DONE();
}
