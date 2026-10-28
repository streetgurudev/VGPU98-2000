/*
 * vgpu98cp.cpl: the VGPU98 3D page. A standard
 * Windows 98 property page in the VGPU98 Color page's style, its template
 * built in memory (tmpl.c): a Vertical sync group with a drop-down list for
 * each 3D interface (Application's choice, Always on, Always off), what
 * they do, the screen's refresh rate, a status line and the build. Apply
 * saves the settings and gives them to the card's VSYNC_CONTROL, which
 * takes them at each program's next flip or swap; Cancel changes nothing.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cpl.h"
#include "tmpl.h"

#define ID_D3D      201 /* then OpenGL's and Glide's: cp_vsync_t's order */
#define ID_OGL      202
#define ID_GLIDE    203
#define ID_DEFAULTS 210
#define ID_REFRESH  220
#define ID_STATUS   230
#define ID_BUILD    231
#define ID_LABEL    (-1) /* IDC_STATIC */

/* The lists' entries, in VGPU98_VSYNC_APP, _ON, _OFF's order. */
static const char *const mode_labels[3] = { "Application's choice", "Always on", "Always off" };

/* --- The template ------------------------------------------------------------ */

static WORD tmpl_words[TMPL_WORDS] __attribute__((aligned(4)));

static void
t_static(tmpl_t *t, int x, int y, int cx, int cy, int id, const char *text)
{
    tmpl_item(t, SS_LEFT, 0, x, y, cx, cy, id, CLASS_STATIC, NULL, text);
}

/* A label and its drop-down list; the list's height is the open list's. */
static void
t_row(tmpl_t *t, int y, const char *label, int id)
{
    t_static(t, 15, y + 2, 48, 8, ID_LABEL, label);
    tmpl_item(t, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 66, y, 104, 64, id, CLASS_COMBOBOX, NULL, "");
}

static const void *
build_template(void)
{
    tmpl_t t;

    tmpl_begin(&t, tmpl_words, TMPL_WORDS, WS_CHILD | WS_CAPTION | DS_SETFONT | DS_3DLOOK, 252, 218, "VGPU98 3D", 8,
               "MS Sans Serif");

    tmpl_item(&t, BS_GROUPBOX, 0, 7, 7, 238, 106, ID_LABEL, CLASS_BUTTON, NULL, "Vertical sync");
    t_static(&t, 15, 20, 222, 16, ID_LABEL,
             "Each applies to every program using that interface. Application's choice keeps the program's own.");
    t_row(&t, 40, "Direct&3D:", ID_D3D);
    t_row(&t, 57, "&OpenGL:", ID_OGL);
    t_row(&t, 74, "&Glide:", ID_GLIDE);
    tmpl_item(&t, BS_PUSHBUTTON | WS_TABSTOP, 0, 162, 94, 76, 14, ID_DEFAULTS, CLASS_BUTTON, NULL, "Restore &Defaults");

    tmpl_item(&t, BS_GROUPBOX, 0, 7, 118, 238, 74, ID_LABEL, CLASS_BUTTON, NULL, "About these settings");
    t_static(&t, 15, 130, 222, 16, ID_LABEL,
             "Always on: programs wait for each refresh, so they run no faster than the refresh rate.");
    t_static(&t, 15, 150, 222, 24, ID_LABEL,
             "Always off: programs never wait, and may draw more frames than the screen shows. The screen still "
             "shows whole frames, without tearing.");
    t_static(&t, 15, 178, 222, 8, ID_REFRESH, "");

    t_static(&t, 7, 197, 238, 8, ID_STATUS, "");
    t_static(&t, 7, 207, 238, 8, ID_BUILD, "");
    return tmpl_end(&t);
}

/* --- The page ------------------------------------------------------------------ */

static struct {
    HWND        dlg;
    const char *way;
    int         usable; /* the card, with its V-sync overrides */
    cp_vsync_t  v;      /* as the lists show them */
} sy;

static void
show_modes(void)
{
    int i;

    for (i = 0; i < CP_APIS; i++)
        SendMessageA(GetDlgItem(sy.dlg, ID_D3D + i), CB_SETCURSEL, sy.v.mode[i], 0);
}

/* The refresh rate now: the Adapter tab may have changed it since the page
   was shown. */
static void
show_refresh(void)
{
    const DWORD hz = cp_refresh();
    char        text[96];
    char       *p;

    if (!hz) {
        SetDlgItemTextA(sy.dlg, ID_REFRESH, "The refresh rate is set on the Adapter tab.");
        return;
    }
    p = cp_put_text(text, "Refresh rate now: ");
    p = cp_put_uint(p, (unsigned) hz);
    cp_put_text(p, " Hz (set on the Adapter tab).");
    SetDlgItemTextA(sy.dlg, ID_REFRESH, text);
}

static void
enable_controls(BOOL on)
{
    static const int ids[] = { ID_D3D, ID_OGL, ID_GLIDE, ID_DEFAULTS };
    unsigned         i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        EnableWindow(GetDlgItem(sy.dlg, ids[i]), on);
}

static void
mark_changed(void)
{
    SendMessageA(GetParent(sy.dlg), PSM_CHANGED, (WPARAM) sy.dlg, 0);
}

static void
init(HWND dlg)
{
    const cp_card_t *card    = cp_card();
    DWORD            control = 0;
    char             text[96];
    char            *p;
    int              i;
    int              j;

    sy.dlg = dlg;
    cp_vsync_load(&sy.v);
    for (i = 0; i < CP_APIS; i++) {
        for (j = 0; j < 3; j++)
            SendMessageA(GetDlgItem(dlg, ID_D3D + i), CB_ADDSTRING, 0, (LPARAM) mode_labels[j]);
    }
    show_modes();
    show_refresh();

    sy.usable = card->found && card->vsync;
    if (sy.usable)
        control = cp_vsync_read();
    if (!card->found)
        SetDlgItemTextA(dlg, ID_STATUS, "The VGPU98 2000's display driver is not running.");
    else if (!card->bar0)
        SetDlgItemTextA(dlg, ID_STATUS, "The card's registers could not be reached.");
    else if (!card->vsync)
        SetDlgItemTextA(dlg, ID_STATUS, "V-sync overrides are turned off in 86Box's settings.");
    else if (control != cp_vsync_control(&sy.v)) {
        /* Apply enabled to set them. */
        SetDlgItemTextA(dlg, ID_STATUS, "The card is not using these settings. Apply sets them.");
        mark_changed();
    } else
        SetDlgItemTextA(dlg, ID_STATUS, "Changes take effect at each program's next frame.");
    enable_controls(sy.usable);
    p = cp_put_text(text, CPL_NAME);
    if (card->found) {
        p = cp_put_text(p, ", driver build ");
        cp_put_text(p, card->stamp);
    }
    SetDlgItemTextA(dlg, ID_BUILD, text);
    cp_event("VGPU98 3D opened as %s (saved: Direct3D %s, OpenGL %s, Glide %s; the card's VSYNC_CONTROL %x%s)",
             sy.way, cp_mode_names[sy.v.mode[0]], cp_mode_names[sy.v.mode[1]], cp_mode_names[sy.v.mode[2]], control,
             card->vsync ? "" : ", the overrides off in 86Box's settings");
}

static void
apply(void)
{
    const DWORD control = cp_vsync_control(&sy.v);
    DWORD       back;

    cp_vsync_save(&sy.v);
    back = cp_vsync_write(control);
    if (back == control)
        cp_event("VGPU98 3D applied: Direct3D %s, OpenGL %s, Glide %s (VSYNC_CONTROL %x)", cp_mode_names[sy.v.mode[0]],
                 cp_mode_names[sy.v.mode[1]], cp_mode_names[sy.v.mode[2]], control);
    else
        cp_event("VGPU98 3D applied: Direct3D %s, OpenGL %s, Glide %s (VSYNC_CONTROL %x, but the card read back %x)",
                 cp_mode_names[sy.v.mode[0]], cp_mode_names[sy.v.mode[1]], cp_mode_names[sy.v.mode[2]], control,
                 back);
    SetDlgItemTextA(sy.dlg, ID_STATUS,
                    (back == control) ? "Saved. The card gets these settings when Windows starts."
                                      : "Saved, but the card did not take these settings.");
}

static BOOL STDCALL
page_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_INITDIALOG:
            sy.way = (const char *) ((const PROPSHEETPAGEA *) lp)->lParam;
            init(dlg);
            return 1;
        case WM_COMMAND:
            {
                const int id = (int) (wp & 0xFFFF);

                if (!sy.usable)
                    return 0;
                if (((wp >> 16) == CBN_SELCHANGE) && (id >= ID_D3D) && (id <= ID_GLIDE)) {
                    const LRESULT sel = SendMessageA((HWND) lp, CB_GETCURSEL, 0, 0);

                    if ((sel >= 0) && (sel < 3) && ((DWORD) sel != sy.v.mode[id - ID_D3D])) {
                        sy.v.mode[id - ID_D3D] = (DWORD) sel;
                        mark_changed();
                    }
                    return 1;
                }
                if (((wp >> 16) == BN_CLICKED) && (id == ID_DEFAULTS)) {
                    cp_vsync_defaults(&sy.v);
                    show_modes();
                    mark_changed();
                    return 1;
                }
                return 0;
            }
        case WM_NOTIFY:
            switch (((const NMHDR *) lp)->code) {
                case PSN_SETACTIVE:
                    show_refresh();
                    SetWindowLongA(dlg, DWL_MSGRESULT, 0);
                    return 1;
                case PSN_APPLY:
                    if (sy.usable)
                        apply();
                    SetWindowLongA(dlg, DWL_MSGRESULT, PSNRET_NOERROR);
                    return 1;
                default:
                    return 0;
            }
        case WM_DESTROY:
            sy.dlg = NULL;
            return 0;
        default:
            return 0;
    }
}

int
cp_3d_page(PROPSHEETPAGEA *page, const char *way)
{
    memset(page, 0, sizeof(*page));
    page->dwSize      = sizeof(*page);
    page->dwFlags     = PSP_DLGINDIRECT | PSP_USETITLE | PSP_USEREFPARENT;
    page->hInstance   = cp_instance;
    page->pResource   = build_template();
    page->pszTitle    = "VGPU98 3D";
    page->pfnDlgProc  = page_proc;
    page->lParam      = (LPARAM) way;
    page->pcRefParent = &cp_page_refs;
    return page->pResource != NULL;
}
