/*
 * vgpu98cp.cpl: the saved settings.
 * Gamma's under HKLM\Software\VGPU98
 * Project\VGPU98 2000\Gamma, the V-sync overrides' beside them under VSync,
 * all DWORDs. One start-up entry, rundll32's vgpu98_apply, stays in the Run
 * key while either page's settings are not the defaults, and gives the card
 * both.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cpl.h"
#include "vid_vgpu98_card.h"

#define GAMMA_KEY     "Software\\VGPU98 Project\\VGPU98 2000\\Gamma"
#define VSYNC_KEY     "Software\\VGPU98 Project\\VGPU98 2000\\VSync"
#define RUN_KEY       "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE     "VGPU98 2000"
#define OLD_RUN_VALUE "VGPU98 Gamma" /* the gamma round's, replaced at the next save */
#define RUN_COMMAND   "rundll32.exe vgpu98cp.cpl,vgpu98_apply"

const char *const cp_api_names[CP_APIS] = { "Direct3D", "OpenGL", "Glide" };
const char *const cp_mode_names[3]      = { "application's choice", "always on", "always off" };

static const int vsync_shifts[CP_APIS] = { VGPU98_VSYNC_D3D_SHIFT, VGPU98_VSYNC_OGL_SHIFT, VGPU98_VSYNC_GLIDE_SHIFT };

/* --- The registry's DWORDs ------------------------------------------------------- */

/* Each of `names` that is a DWORD, into `v`; the rest left as they are. */
static void
load_values(const char *sub, const char *const *names, int count, DWORD *v)
{
    HKEY key;
    int  i;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    for (i = 0; i < count; i++) {
        DWORD type;
        DWORD d;
        DWORD bytes = sizeof(d);

        if ((RegQueryValueExA(key, names[i], NULL, &type, (BYTE *) &d, &bytes) == ERROR_SUCCESS) &&
            (type == REG_DWORD) && (bytes == sizeof(d)))
            v[i] = d;
    }
    RegCloseKey(key);
}

static void
save_values(const char *sub, const char *const *names, int count, const DWORD *v)
{
    HKEY key;
    int  i;

    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, sub, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
        return;
    for (i = 0; i < count; i++)
        RegSetValueExA(key, names[i], 0, REG_DWORD, (const BYTE *) &v[i], sizeof(v[i]));
    RegCloseKey(key);
}

/* --- Gamma --------------------------------------------------------------------- */

static const char *const gamma_names[9] = { "Red Gamma",      "Green Gamma",      "Blue Gamma",
                                            "Red Brightness", "Green Brightness", "Blue Brightness",
                                            "Red Contrast",   "Green Contrast",   "Blue Contrast" };

static int *
setting(ramp_settings_t *s, int i)
{
    return (i < 3) ? &s->gamma[i] : (i < 6) ? &s->brightness[i - 3] : &s->contrast[i - 6];
}

void
cp_load(ramp_settings_t *s)
{
    DWORD v[9];
    int   i;

    ramp_defaults(s);
    for (i = 0; i < 9; i++)
        v[i] = (DWORD) *setting(s, i);
    load_values(GAMMA_KEY, gamma_names, 9, v);
    for (i = 0; i < 9; i++)
        *setting(s, i) = (int) v[i];
    ramp_clamp(s);
}

/* --- The V-sync overrides -------------------------------------------------------- */

void
cp_vsync_defaults(cp_vsync_t *v)
{
    int i;

    for (i = 0; i < CP_APIS; i++)
        v->mode[i] = VGPU98_VSYNC_APP;
}

int
cp_vsync_is_default(const cp_vsync_t *v)
{
    int i;

    for (i = 0; i < CP_APIS; i++) {
        if (v->mode[i] != VGPU98_VSYNC_APP)
            return 0;
    }
    return 1;
}

void
cp_vsync_load(cp_vsync_t *v)
{
    int i;

    cp_vsync_defaults(v);
    load_values(VSYNC_KEY, cp_api_names, CP_APIS, v->mode);
    for (i = 0; i < CP_APIS; i++) {
        if (v->mode[i] > VGPU98_VSYNC_OFF)
            v->mode[i] = VGPU98_VSYNC_APP;
    }
}

DWORD
cp_vsync_control(const cp_vsync_t *v)
{
    DWORD control = 0;
    int   i;

    for (i = 0; i < CP_APIS; i++)
        control |= (v->mode[i] & VGPU98_VSYNC_FIELD) << vsync_shifts[i];
    return control;
}

/* 3, reserved, reads as the application's choice. */
void
cp_vsync_from_control(cp_vsync_t *v, DWORD control)
{
    int i;

    for (i = 0; i < CP_APIS; i++) {
        v->mode[i] = (control >> vsync_shifts[i]) & VGPU98_VSYNC_FIELD;
        if (v->mode[i] > VGPU98_VSYNC_OFF)
            v->mode[i] = VGPU98_VSYNC_APP;
    }
}

/* --- The start-up entry --------------------------------------------------------- */

/* The entry while either page's saved settings are not the defaults. */
static void
update_startup(void)
{
    ramp_settings_t s;
    cp_vsync_t      v;
    HKEY            key;

    cp_load(&s);
    cp_vsync_load(&v);
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, RUN_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
        return;
    RegDeleteValueA(key, OLD_RUN_VALUE);
    if (ramp_is_default(&s) && cp_vsync_is_default(&v))
        RegDeleteValueA(key, RUN_VALUE);
    else
        RegSetValueExA(key, RUN_VALUE, 0, REG_SZ, (const BYTE *) RUN_COMMAND, sizeof(RUN_COMMAND));
    RegCloseKey(key);
}

void
cp_save(const ramp_settings_t *s)
{
    DWORD v[9];
    int   i;

    for (i = 0; i < 9; i++)
        v[i] = (DWORD) *setting((ramp_settings_t *) s, i);
    save_values(GAMMA_KEY, gamma_names, 9, v);
    update_startup();
}

void
cp_vsync_save(const cp_vsync_t *v)
{
    save_values(VSYNC_KEY, cp_api_names, CP_APIS, v->mode);
    update_startup();
}

/* --- Start-up ---------------------------------------------------------------------- */

/* Each page's settings that are not the defaults. The card starts with
   neither: the gamma table and VSYNC_CONTROL are cleared at the machine's
   reset. */
void
cp_apply_saved(void)
{
    const cp_card_t *card = cp_card();
    ramp_settings_t  s;
    cp_vsync_t       v;

    if (!card->found || !card->bar0)
        return; /* and no log to tell */
    cp_load(&s);
    if (!card->gamma)
        cp_event("start-up: gamma is off in 86Box's settings");
    else if (ramp_is_default(&s))
        cp_event("start-up: the saved gamma is the default (nothing to set)");
    else {
        uint16_t words[RAMP_WORDS];
        HDC      dc;
        BOOL     ok = 0;

        ramp_build(&s, words);
        dc = GetDC(NULL);
        if (dc) {
            ok = SetDeviceGammaRamp(dc, words);
            ReleaseDC(NULL, dc);
        }
        cp_event("start-up: the saved gamma %s (red, green, blue at 128: %u/%u/%u)",
                 ok ? "applied" : "refused by the driver", words[128] >> 8, words[256 + 128] >> 8,
                 words[512 + 128] >> 8);
    }
    cp_vsync_load(&v);
    if (!card->vsync)
        cp_event("start-up: the V-sync overrides are off in 86Box's settings");
    else if (cp_vsync_is_default(&v))
        cp_event("start-up: the saved V-sync overrides are the defaults (nothing to set)");
    else {
        const DWORD control = cp_vsync_control(&v);
        const DWORD back    = cp_vsync_write(control);

        cp_event("start-up: the saved V-sync overrides set: Direct3D %s, OpenGL %s, Glide %s (VSYNC_CONTROL %x%s)",
                 cp_mode_names[v.mode[0]], cp_mode_names[v.mode[1]], cp_mode_names[v.mode[2]], control,
                 (back == control) ? "" : ", but the card read back something else");
    }
}
