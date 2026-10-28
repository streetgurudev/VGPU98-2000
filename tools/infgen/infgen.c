/*
 * infgen: writes the Windows 98 display driver INF from the card definition.
 *
 *   infgen --driver driver.card --card vgpu98.card --modes display-modes.card -o OUT.inf
 *
 * The PCI identity comes from vgpu98.card, the package (file names, version,
 * default mode) from driver.card, and the desktop modes with their refresh
 * rates from display-modes.card. The output is plain ASCII with CRLF line ends
 * and depends only on those files. What each line means: spec/driver-package.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "modes.h"
#include "util.h"

static buf_t out;

/* One INF line, CRLF-terminated. */
static void
line(const char *fmt, ...) VGPU98_PRINTF(1, 2);

static void
line(const char *fmt, ...)
{
    char    text[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    buf_append(&out, text, strlen(text));
    buf_append(&out, "\r\n", 2);
}

static void
blank(void)
{
    buf_append(&out, "\r\n", 2);
}

/* A raw value made of the characters a file name, version or date may hold. */
static const char *
get_token(cfg_t *cfg, cfg_section_t *sec, const char *key, const char *allowed)
{
    const char *v = cfg_value(sec, key);
    const char *p;

    if (v == NULL)
        cfg_error(cfg, sec->line, "[%s] needs '%s'", sec->name, key);
    for (p = v; *p; p++) {
        if (!isalnum((unsigned char) *p) && !strchr(allowed, *p))
            cfg_error(cfg, cfg_line(sec, key), "'%s' has an unexpected character '%c'", key, *p);
    }
    return v;
}

/* An 8.3 file name in lower case. */
static const char *
get_file(cfg_t *cfg, cfg_section_t *sec, const char *key)
{
    const char *v   = get_token(cfg, sec, key, "._-");
    const char *dot = strchr(v, '.');

    if (!dot || (dot - v > 8) || (dot == v) || (strlen(dot + 1) > 3) || (strlen(dot + 1) == 0) ||
        strchr(dot + 1, '.'))
        cfg_error(cfg, cfg_line(sec, key), "'%s' must be an 8.3 file name", key);
    for (; *v; v++) {
        if (isupper((unsigned char) *v))
            cfg_error(cfg, cfg_line(sec, key), "'%s': write file names in lower case", key);
    }
    return cfg_value(sec, key);
}

/* A class ID as the registry holds it: braces around 8-4-4-4-12 hexadecimal
   digits, in upper case. */
static const char *
get_clsid(cfg_t *cfg, cfg_section_t *sec, const char *key)
{
    static const char shape[] = "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}";
    const char       *v       = get_token(cfg, sec, key, "{}-");
    size_t            i;

    if (strlen(v) != sizeof(shape) - 1)
        cfg_error(cfg, cfg_line(sec, key), "'%s' must be a class ID in upper case, as {0123ABCD-...}", key);
    for (i = 0; shape[i]; i++) {
        const int hex = isdigit((unsigned char) v[i]) || ((v[i] >= 'A') && (v[i] <= 'F'));

        if ((shape[i] == 'x') ? !hex : (v[i] != shape[i]))
            cfg_error(cfg, cfg_line(sec, key), "'%s' must be a class ID in upper case, as {0123ABCD-...}", key);
    }
    return v;
}

/* An INF string value: no quotes or percent signs inside. */
static char *
get_text(cfg_t *cfg, cfg_section_t *sec, const char *key)
{
    char *s = cfg_get_str(cfg, sec, key, 100);

    if (strpbrk(s, "\"%"))
        cfg_error(cfg, cfg_line(sec, key), "'%s' may not contain quotes or %%", key);
    return s;
}

int
main(int argc, char **argv)
{
    const char      *driver_path = NULL, *card_path = NULL, *modes_path = NULL, *out_path = NULL;
    cfg_t           *drv, *card;
    cfg_section_t   *sec;
    static dmodes_t  modes;
    mode_limits_t    limits;
    uint32_t         vendor, device, sub_vendor, sub_device;
    const char      *version, *date, *label, *drv_file, *vxd_file, *dll_file, *glide2_file, *glide3_file, *gl_file;
    const char      *gl_name, *cpl_file, *cpl_clsid;
    char            *provider, *device_name, *disk_name;
    uint32_t         dm[3];
    unsigned         month, day, year, v[4];
    char             tail;
    size_t           i;
    int              depth;
    int              have_default = 0;

    set_progname("infgen");
    for (i = 1; i < (size_t) argc; i++) {
        if (!strcmp(argv[i], "--driver") && (i + 1 < (size_t) argc))
            driver_path = argv[++i];
        else if (!strcmp(argv[i], "--card") && (i + 1 < (size_t) argc))
            card_path = argv[++i];
        else if (!strcmp(argv[i], "--modes") && (i + 1 < (size_t) argc))
            modes_path = argv[++i];
        else if (!strcmp(argv[i], "-o") && (i + 1 < (size_t) argc))
            out_path = argv[++i];
        else
            die("usage: infgen --driver DRIVER.card --card CARD.card --modes DISPLAY_MODES.card -o OUT.inf");
    }
    if (!driver_path || !card_path || !modes_path || !out_path)
        die("usage: infgen --driver DRIVER.card --card CARD.card --modes DISPLAY_MODES.card -o OUT.inf");

    /* The card: PCI identity and the engine's limits (only these keys are read). */
    card       = cfg_load(card_path);
    sec        = cfg_section(card, "pci");
    vendor     = cfg_get_u32(card, sec, "vendor_id", 1, 0xfffe);
    device     = cfg_get_u32(card, sec, "device_id", 0, 0xffff);
    sub_vendor = cfg_get_u32(card, sec, "subsystem_vendor_id", 0, 0xffff);
    sub_device = cfg_get_u32(card, sec, "subsystem_id", 0, 0xffff);
    sec                = cfg_section(card, "display");
    limits.max_width   = cfg_get_u32(card, sec, "max_width", 640, 2048);
    limits.max_height  = cfg_get_u32(card, sec, "max_height", 480, 1536);
    limits.refresh_min = cfg_get_u32(card, sec, "refresh_min", 40, 255);
    limits.refresh_max = cfg_get_u32(card, sec, "refresh_max", limits.refresh_min, 255);
    modes_load(modes_path, &limits, &modes);

    /* The package. */
    drv         = cfg_load(driver_path);
    sec         = cfg_section(drv, "driver");
    provider    = get_text(drv, sec, "provider");
    device_name = get_text(drv, sec, "device");
    if (strchr(device_name, '\\'))
        cfg_error(drv, cfg_line(sec, "device"), "'device' names a registry key: no backslashes");
    disk_name   = get_text(drv, sec, "disk_name");
    label       = get_token(drv, sec, "disk_label", "_-");
    drv_file    = get_file(drv, sec, "display_driver");
    vxd_file    = get_file(drv, sec, "minivdd");
    dll_file    = get_file(drv, sec, "directx_driver");
    glide2_file = get_file(drv, sec, "glide2_driver");
    glide3_file = get_file(drv, sec, "glide3_driver");
    gl_file     = get_file(drv, sec, "opengl_driver");
    gl_name     = get_token(drv, sec, "opengl_name", "");
    cpl_file    = get_file(drv, sec, "control_panel");
    cpl_clsid   = get_clsid(drv, sec, "control_clsid");
    get_file(drv, sec, "inf");
    version = get_token(drv, sec, "version", ".");
    date    = get_token(drv, sec, "date", "/");
    if (strlen(label) > 11)
        cfg_error(drv, cfg_line(sec, "disk_label"), "a volume label has at most 11 characters");
    if ((sscanf(version, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) || (v[0] > 0xffff) ||
        (v[1] > 0xffff) || (v[2] > 0xffff) || (v[3] > 0xffff))
        cfg_error(drv, cfg_line(sec, "version"), "version must be four numbers, as in 4.10.0.1");
    if ((sscanf(date, "%u/%u/%u%c", &month, &day, &year, &tail) != 3) || (month < 1) || (month > 12) ||
        (day < 1) || (day > 31) || (year < 1995) || (year > 2099))
        cfg_error(drv, cfg_line(sec, "date"), "date must be MM/DD/YYYY");
    if (cfg_get_u32_list(drv, sec, "default_mode", dm, 3) != 3)
        cfg_error(drv, cfg_line(sec, "default_mode"), "default_mode is bits per pixel, width and height");
    if ((dm[0] != 8) && (dm[0] != 16) && (dm[0] != 32))
        cfg_error(drv, cfg_line(sec, "default_mode"), "the default depth must be 8, 16 or 32");
    for (i = 0; i < modes.count; i++) {
        if (modes.mode[i].enabled && modes.mode[i].desktop && (modes.mode[i].width == dm[1]) &&
            (modes.mode[i].height == dm[2]))
            have_default = 1;
    }
    if (!have_default)
        cfg_error(drv, cfg_line(sec, "default_mode"), "%ux%u is not a desktop mode", dm[1], dm[2]);
    cfg_check_unused(drv);

    line("; %s display driver for Windows 98 SE.", device_name);
    line("; Generated by infgen from card/driver.card, card/vgpu98.card and card/display-modes.card.");
    line("; Do not edit. Copyright 2026 VGPU98 Project contributors, GPL-2.0-or-later.");
    blank();
    line("[Version]");
    line("Signature=\"$CHICAGO$\"");
    line("Class=DISPLAY");
    line("Provider=%%Provider%%");
    line("DriverVer=%02u/%02u/%04u,%u.%u.%u.%u", month, day, year, v[0], v[1], v[2], v[3]);
    blank();
    line("[DestinationDirs]");
    line("DefaultDestDir=11"); /* LDID_SYS: WINDOWS\\SYSTEM */
    blank();
    /* No tag file: the files install from the floppy or from any folder. */
    line("[SourceDisksNames]");
    line("1=%%DiskName%%,,0");
    blank();
    line("[SourceDisksFiles]");
    line("%s=1", drv_file);
    line("%s=1", vxd_file);
    line("%s=1", dll_file);
    line("%s=1", glide2_file);
    line("%s=1", glide3_file);
    line("%s=1", gl_file);
    line("%s=1", cpl_file);
    blank();
    line("[Manufacturer]");
    line("%%Provider%%=Models");
    blank();
    /* One line: the card's own ID, then the chip's as a compatible ID. Windows lists every matching line as a
       model, so two lines would show the card twice. */
    line("[Models]");
    line("%%Device%%=VGPU98,PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X,PCI\\VEN_%04X&DEV_%04X", vendor, device,
         sub_device, sub_vendor, vendor, device);
    blank();
    line("[VGPU98]");
    line("CopyFiles=VGPU98.Copy");
    line("DelReg=VGPU98.DelReg");
    line("AddReg=VGPU98.AddReg");
    blank();
    /* Every file into WINDOWS\SYSTEM (DefaultDestDir): the Glide DLLs where
       a Voodoo3's driver puts its own, so Glide programs find them by name,
       the OpenGL ICD where opengl32.dll
       loads it from, and the control program where
       the Control Panel finds its .cpl files and rundll32 its DLLs. */
    line("[VGPU98.Copy]");
    line("%s", drv_file);
    line("%s", vxd_file);
    line("%s", dll_file);
    line("%s", glide2_file);
    line("%s", glide3_file);
    line("%s", gl_file);
    line("%s", cpl_file);
    blank();
    line("[VGPU98.DelReg]");
    line("HKR,,Ver");
    line("HKR,,DevLoader");
    line("HKR,DEFAULT");
    line("HKR,MODES");
    line("HKR,CURRENT");
    blank();
    line("[VGPU98.AddReg]");
    line("HKR,,Ver,,4.0");
    line("HKR,,DevLoader,,*vdd");
    line("HKR,DEFAULT,Mode,,\"%u,%u,%u\"", dm[0], dm[1], dm[2]);
    line("HKR,DEFAULT,drv,,%s", drv_file);
    line("HKR,DEFAULT,vdd,,\"*vdd\"");
    line("HKR,DEFAULT,minivdd,,%s", vxd_file);
    line("HKR,DEFAULT,RefreshRate,,-1");
    line("HKR,DEFAULT,DDC,,1");
    /* The OpenGL ICD's name, as Windows 98's opengl32.dll looks it up: a
       value under OpenGLdrivers naming the DLL. */
    line("HKLM,\"Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLdrivers\",\"%s\",,\"%s\"", gl_name, gl_file);
    /* The control program's tab: an in-process server for its class, and
       the class among the Advanced display dialog's property sheet
       handlers, named after the device (the
       Windows 98 SE DDK's monitor sample, read for facts). */
    line("HKCR,CLSID\\%s,,,\"%s\"", cpl_clsid, device_name);
    line("HKCR,CLSID\\%s\\InProcServer32,,,\"%%11%%\\%s\"", cpl_clsid, cpl_file);
    line("HKCR,CLSID\\%s\\InProcServer32,ThreadingModel,,\"Apartment\"", cpl_clsid);
    line("HKLM,\"Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Device\\shellex\\PropertySheetHandlers\\%s\",,,"
         "\"%s\"",
         device_name, cpl_clsid);
    line("HKR,\"MODES\\4\\640,480\",drv,,vga.drv");
    line("HKR,\"MODES\\4\\640,480\",vdd,,*vdd");
    for (depth = 0; depth < 3; depth++) {
        static const unsigned bpp[3] = { 8, 16, 32 };

        for (i = 0; i < modes.count; i++) {
            const dmode_t *m = &modes.mode[i];
            uint8_t        sorted[MODES_REFRESH];
            char           rates[80];
            size_t         n = 0;
            size_t         r, k;

            if (!m->enabled || !m->desktop)
                continue;
            /* The rates in ascending order (the table lists the default first). */
            memcpy(sorted, m->refresh, m->n_refresh);
            for (r = 1; r < m->n_refresh; r++) {
                for (k = r; (k > 0) && (sorted[k - 1] > sorted[k]); k--) {
                    uint8_t x     = sorted[k];
                    sorted[k]     = sorted[k - 1];
                    sorted[k - 1] = x;
                }
            }
            for (r = 0; r < m->n_refresh; r++)
                n += (size_t) snprintf(rates + n, sizeof(rates) - n, "%s%u", r ? "," : "", sorted[r]);
            line("HKR,\"MODES\\%u\\%u,%u\",,,\"%s\"", bpp[depth], m->width, m->height, rates);
        }
    }
    blank();
    line("[Strings]");
    line("Provider=\"%s\"", provider);
    line("Device=\"%s\"", device_name);
    line("DiskName=\"%s\"", disk_name);

    write_file(out_path, out.data, out.len);
    buf_free(&out);
    return 0;
}
