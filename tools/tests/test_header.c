/*
 * Compiles the generated 86Box header and checks that its constants agree.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <string.h>

#include "check.h"
#include "vid_vgpu98_card.h"

/* Same shape as the selection entries of 86Box's device_config_t. */
struct selection {
    const char *description;
    int         value;
};

static const struct selection vram[] = {
    VGPU98_VRAM_SELECTION
    { .description = "", .value = 0 }
};

int
main(void)
{
    size_t i;
    int    has_default = 0;

    for (i = 0; vram[i].description[0]; i++) {
        CHECK((vram[i].value << 20) <= VGPU98_BAR1_SIZE);
        if (vram[i].value == VGPU98_VRAM_DEFAULT_MB)
            has_default = 1;
        CHECK(vram[i].value <= VGPU98_VRAM_MAX_MB);
    }
    CHECK(i > 0);
    CHECK(has_default);
    CHECK(VGPU98_PCI_CLASS_CODE == 0x030000);
    CHECK(VGPU98_ROM_SIZE <= VGPU98_ROM_BAR_SIZE);
    CHECK(strlen(VGPU98_CARD_NAME) > 0);
    {
        static const unsigned char edid[] = { VGPU98_EDID_BYTES };
        unsigned char              sum    = 0;

        CHECK(sizeof(edid) == 128);
        for (i = 0; i < sizeof(edid); i++)
            sum = (unsigned char) (sum + edid[i]);
        CHECK(sum == 0);
    }
    CHECK((VGPU98_SR_DISP_KEY == VGPU98_SR_DISP_FIRST) && (VGPU98_SR_DISP_EDID_DATA <= VGPU98_SR_DISP_LAST));
    CHECK((VGPU98_DEBUG_SEQ_INDEX < VGPU98_SR_DISP_FIRST) || (VGPU98_DEBUG_SEQ_INDEX > VGPU98_SR_DISP_LAST));
    CHECK(strncmp(VGPU98_ROM_PATH, "roms/", 5) == 0);

    CHECK_DONE();
}
