/*
 * vgpu98cp.cpl: the VGPU98 Color page. A
 * standard Windows 98 property page, its template built in memory
 * (tmpl.c): "MS Sans Serif" 8, the large page size (252 x 218 dialog
 * units), group boxes, radio buttons, trackbars, static text and a push
 * button, system colours throughout. Moving a trackbar shows the ramp at
 * once through SetDeviceGammaRamp; Apply saves it, Cancel puts back the
 * ramp the page found.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cpl.h"
#include "tmpl.h"

#define ID_ALL          101
#define ID_RED          102
#define ID_GREEN        103
#define ID_BLUE         104
#define ID_GAMMA        110
#define ID_GAMMA_VAL    111
#define ID_BRIGHT       112
#define ID_BRIGHT_VAL   113
#define ID_CONTRAST     114
#define ID_CONTRAST_VAL 115
#define ID_DEFAULTS     116
#define ID_CURVE        120
#define ID_STEPS        121
#define ID_STATUS       130
#define ID_BUILD        131
#define ID_LABEL        (-1) /* static text that is never addressed: IDC_STATIC */

/* --- The template ------------------------------------------------------------ */

static WORD tmpl_words[TMPL_WORDS] __attribute__((aligned(4)));

static void
t_static(tmpl_t *t, int x, int y, int cx, int cy, int id, DWORD style, const char *text)
{
    tmpl_item(t, SS_LEFT | style, 0, x, y, cx, cy, id, CLASS_STATIC, NULL, text);
}

static void
t_trackbar(tmpl_t *t, int x, int y, int cx, int id)
{
    tmpl_item(t, TBS_AUTOTICKS | WS_TABSTOP, 0, x, y, cx, 16, id, 0, TRACKBAR_CLASSA, "");
}

static const void *
build_template(void)
{
    tmpl_t t;

    /* A child page with the dialog font. */
    tmpl_begin(&t, tmpl_words, TMPL_WORDS, WS_CHILD | WS_CAPTION | DS_SETFONT | DS_3DLOOK, 252, 218, "VGPU98 Color", 8,
               "MS Sans Serif");

    tmpl_item(&t, BS_GROUPBOX, 0, 7, 7, 238, 106, ID_LABEL, CLASS_BUTTON, NULL, "Color adjustment");
    tmpl_item(&t, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 0, 15, 20, 56, 10, ID_ALL, CLASS_BUTTON, NULL,
              "&All colors");
    tmpl_item(&t, BS_AUTORADIOBUTTON, 0, 76, 20, 36, 10, ID_RED, CLASS_BUTTON, NULL, "&Red");
    tmpl_item(&t, BS_AUTORADIOBUTTON, 0, 116, 20, 40, 10, ID_GREEN, CLASS_BUTTON, NULL, "Gr&een");
    tmpl_item(&t, BS_AUTORADIOBUTTON, 0, 160, 20, 36, 10, ID_BLUE, CLASS_BUTTON, NULL, "Bl&ue");

    t_static(&t, 15, 39, 42, 8, ID_LABEL, WS_GROUP, "&Gamma:");
    t_trackbar(&t, 58, 35, 146, ID_GAMMA);
    t_static(&t, 206, 39, 32, 8, ID_GAMMA_VAL, SS_RIGHT, "");
    t_static(&t, 15, 59, 42, 8, ID_LABEL, 0, "&Brightness:");
    t_trackbar(&t, 58, 55, 146, ID_BRIGHT);
    t_static(&t, 206, 59, 32, 8, ID_BRIGHT_VAL, SS_RIGHT, "");
    t_static(&t, 15, 79, 42, 8, ID_LABEL, 0, "&Contrast:");
    t_trackbar(&t, 58, 75, 146, ID_CONTRAST);
    t_static(&t, 206, 79, 32, 8, ID_CONTRAST_VAL, SS_RIGHT, "");
    tmpl_item(&t, BS_PUSHBUTTON | WS_TABSTOP, 0, 162, 94, 76, 14, ID_DEFAULTS, CLASS_BUTTON, NULL, "Restore &Defaults");

    tmpl_item(&t, BS_GROUPBOX, 0, 7, 118, 238, 74, ID_LABEL, CLASS_BUTTON, NULL, "Preview");
    tmpl_item(&t, SS_OWNERDRAW, WS_EX_CLIENTEDGE, 15, 130, 72, 56, ID_CURVE, CLASS_STATIC, NULL, "");
    tmpl_item(&t, SS_OWNERDRAW, WS_EX_CLIENTEDGE, 94, 130, 144, 22, ID_STEPS, CLASS_STATIC, NULL, "");
    t_static(&t, 94, 157, 144, 28, ID_LABEL, 0,
             "Each step from black to white should differ from the next. The curve shows red, green and blue.");

    t_static(&t, 7, 197, 238, 8, ID_STATUS, 0, "");
    t_static(&t, 7, 207, 238, 8, ID_BUILD, 0, "");
    return tmpl_end(&t);
}

/* --- Numbers as text (both pages') --------------------------------------------- */

char *
cp_put_uint(char *p, unsigned v)
{
    char tmp[12];
    int  n = 0;

    do {
        tmp[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

char *
cp_put_text(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

/* --- The page's state ----------------------------------------------------------- */

static struct {
    HWND            dlg;
    const char     *way;
    int             channel; /* 0 all colors, 1-3 red, green, blue */
    int             usable;  /* the card's gamma, in a direct colour mode */
    int             changed; /* shown since the last Apply */
    ramp_settings_t s;
    uint16_t        found[RAMP_WORDS]; /* the ramp the page found, for Cancel */
    int             have_found;
    uint16_t        words[RAMP_WORDS];
} pg;

/* The channel the trackbars show: red's for all colors. */
static int
shown_channel(void)
{
    return pg.channel ? pg.channel - 1 : 0;
}

static void
show_values(void)
{
    const int c = shown_channel();
    char      text[16];
    char     *p;

    p = cp_put_uint(text, (unsigned) pg.s.gamma[c] / 100);
    *p++ = '.';
    if (pg.s.gamma[c] % 100 < 10)
        *p++ = '0';
    cp_put_uint(p, (unsigned) pg.s.gamma[c] % 100);
    SetDlgItemTextA(pg.dlg, ID_GAMMA_VAL, text);
    p = text;
    if (pg.s.brightness[c] > 0)
        *p++ = '+';
    if (pg.s.brightness[c] < 0)
        *p++ = '-';
    cp_put_uint(p, (unsigned) ((pg.s.brightness[c] < 0) ? -pg.s.brightness[c] : pg.s.brightness[c]));
    SetDlgItemTextA(pg.dlg, ID_BRIGHT_VAL, text);
    p = cp_put_uint(text, (unsigned) pg.s.contrast[c]);
    cp_put_text(p, "%");
    SetDlgItemTextA(pg.dlg, ID_CONTRAST_VAL, text);
}

static void
show_trackbars(void)
{
    const int c = shown_channel();

    SendMessageA(GetDlgItem(pg.dlg, ID_GAMMA), TBM_SETPOS, 1, pg.s.gamma[c]);
    SendMessageA(GetDlgItem(pg.dlg, ID_BRIGHT), TBM_SETPOS, 1, pg.s.brightness[c] - RAMP_BRIGHTNESS_MIN);
    SendMessageA(GetDlgItem(pg.dlg, ID_CONTRAST), TBM_SETPOS, 1, pg.s.contrast[c]);
    show_values();
}

/* The ramp to the card, at once, and the preview drawn again. */
static void
preview(void)
{
    HDC dc;

    ramp_build(&pg.s, pg.words);
    dc = GetDC(NULL);
    if (dc) {
        SetDeviceGammaRamp(dc, pg.words);
        ReleaseDC(NULL, dc);
    }
    InvalidateRect(GetDlgItem(pg.dlg, ID_CURVE), NULL, 0);
    InvalidateRect(GetDlgItem(pg.dlg, ID_STEPS), NULL, 0);
}

static void
mark_changed(void)
{
    pg.changed = 1;
    SendMessageA(GetParent(pg.dlg), PSM_CHANGED, (WPARAM) pg.dlg, 0);
}

/* --- Drawing ----------------------------------------------------------------------- */

/* The curves: the identity dotted, then red, green and blue; one black
   curve when all three are the same. */
static void
draw_curve(const DRAWITEMSTRUCT *d)
{
    const RECT r     = d->rcItem;
    const int  w     = r.right - r.left - 1;
    const int  h     = r.bottom - r.top - 1;
    const int  same  = !memcmp(pg.words, pg.words + 256, 512) && !memcmp(pg.words, pg.words + 512, 512);
    HPEN       pen;
    HGDIOBJ    old;
    int        c;
    int        i;

    FillRect(d->hDC, &r, GetSysColorBrush(COLOR_WINDOW));
    pen = CreatePen(PS_DOT, 1, GetSysColor(COLOR_GRAYTEXT));
    old = SelectObject(d->hDC, pen);
    MoveToEx(d->hDC, r.left, r.bottom - 1, NULL);
    LineTo(d->hDC, r.right - 1, r.top);
    SelectObject(d->hDC, old);
    DeleteObject(pen);
    for (c = 0; c < (same ? 1 : 3); c++) {
        static const DWORD colours[3] = { RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255) };

        pen = CreatePen(PS_SOLID, 1, same ? GetSysColor(COLOR_WINDOWTEXT) : colours[c]);
        old = SelectObject(d->hDC, pen);
        for (i = 0; i < 256; i++) {
            const int x = r.left + i * w / 255;
            const int y = r.bottom - 1 - (int) ((pg.words[c * 256 + i] >> 8) * h / 255);

            if (!i)
                MoveToEx(d->hDC, x, y, NULL);
            else
                LineTo(d->hDC, x, y);
        }
        SelectObject(d->hDC, old);
        DeleteObject(pen);
    }
}

/* 16 grays from black to white, as the screen shows them (through the card's
   gamma). */
static void
draw_steps(const DRAWITEMSTRUCT *d)
{
    const RECT r = d->rcItem;
    int        i;

    for (i = 0; i < 16; i++) {
        RECT   s;
        HBRUSH b = CreateSolidBrush(RGB(i * 17, i * 17, i * 17));

        s.left   = r.left + (r.right - r.left) * i / 16;
        s.right  = r.left + (r.right - r.left) * (i + 1) / 16;
        s.top    = r.top;
        s.bottom = r.bottom;
        FillRect(d->hDC, &s, b);
        DeleteObject(b);
    }
}

/* --- The dialog procedure ------------------------------------------------------------ */

static void
enable_controls(BOOL on)
{
    static const int ids[] = { ID_ALL, ID_RED, ID_GREEN, ID_BLUE, ID_GAMMA, ID_BRIGHT, ID_CONTRAST, ID_DEFAULTS };
    unsigned         i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        EnableWindow(GetDlgItem(pg.dlg, ids[i]), on);
}

static void
init(HWND dlg)
{
    const cp_card_t *card = cp_card();
    char             text[96];
    char            *p;
    HDC              dc;
    int              bits = 0;
    uint16_t         saved[RAMP_WORDS];

    pg.dlg     = dlg;
    pg.channel = 0;
    pg.changed = 0;
    cp_load(&pg.s);
    ramp_build(&pg.s, saved);
    memcpy(pg.words, saved, sizeof(pg.words));
    dc = GetDC(NULL);
    if (dc) {
        bits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
        pg.have_found = GetDeviceGammaRamp(dc, pg.found);
        ReleaseDC(NULL, dc);
    }

    SendMessageA(GetDlgItem(dlg, ID_GAMMA), TBM_SETRANGE, 1, RAMP_GAMMA_MIN | (RAMP_GAMMA_MAX << 16));
    SendMessageA(GetDlgItem(dlg, ID_GAMMA), TBM_SETTICFREQ, 25, 0);
    SendMessageA(GetDlgItem(dlg, ID_GAMMA), TBM_SETPAGESIZE, 0, 10);
    SendMessageA(GetDlgItem(dlg, ID_BRIGHT), TBM_SETRANGE, 1, (RAMP_BRIGHTNESS_MAX - RAMP_BRIGHTNESS_MIN) << 16);
    SendMessageA(GetDlgItem(dlg, ID_BRIGHT), TBM_SETTICFREQ, 10, 0);
    SendMessageA(GetDlgItem(dlg, ID_BRIGHT), TBM_SETPAGESIZE, 0, 5);
    SendMessageA(GetDlgItem(dlg, ID_CONTRAST), TBM_SETRANGE, 1, RAMP_CONTRAST_MIN | (RAMP_CONTRAST_MAX << 16));
    SendMessageA(GetDlgItem(dlg, ID_CONTRAST), TBM_SETTICFREQ, 10, 0);
    SendMessageA(GetDlgItem(dlg, ID_CONTRAST), TBM_SETPAGESIZE, 0, 5);
    SendMessageA(GetDlgItem(dlg, ID_ALL), BM_SETCHECK, BST_CHECKED, 0);
    show_trackbars();

    pg.usable = card->found && card->gamma && (bits > 8) && pg.have_found;
    if (!card->found)
        SetDlgItemTextA(dlg, ID_STATUS, "The VGPU98 2000's display driver is not running.");
    else if (!card->bar0)
        SetDlgItemTextA(dlg, ID_STATUS, "The card's registers could not be reached.");
    else if (!card->gamma)
        SetDlgItemTextA(dlg, ID_STATUS, "Gamma is turned off in 86Box's settings for this card.");
    else if (bits <= 8)
        SetDlgItemTextA(dlg, ID_STATUS, "Gamma needs High Color (16 bit) or True Color (32 bit).");
    else if (!pg.have_found)
        SetDlgItemTextA(dlg, ID_STATUS, "The display driver did not give its gamma.");
    else if (memcmp(pg.found, saved, sizeof(saved)))
        SetDlgItemTextA(dlg, ID_STATUS, "A program has set its own gamma. Apply sets yours again.");
    else
        SetDlgItemTextA(dlg, ID_STATUS, "The screen shows each change at once. Apply keeps it.");
    enable_controls(pg.usable);
    p = cp_put_text(text, CPL_NAME);
    if (card->found) {
        p = cp_put_text(p, ", driver build ");
        cp_put_text(p, card->stamp);
    }
    SetDlgItemTextA(dlg, ID_BUILD, text);
    cp_event("VGPU98 Color opened as %s (%u bits a pixel; gamma %s)", pg.way, (DWORD) bits,
             !card->gamma ? "off in 86Box's settings" : (bits <= 8) ? "needs High Color or True Color" : "on");
}

/* A trackbar moved: the shown channel, or all three. */
static void
trackbar(HWND bar)
{
    const int pos = (int) SendMessageA(bar, TBM_GETPOS, 0, 0);
    int       first = pg.channel ? pg.channel - 1 : 0;
    int       last  = pg.channel ? pg.channel - 1 : 2;
    int       c;

    for (c = first; c <= last; c++) {
        if (bar == GetDlgItem(pg.dlg, ID_GAMMA))
            pg.s.gamma[c] = pos;
        else if (bar == GetDlgItem(pg.dlg, ID_BRIGHT))
            pg.s.brightness[c] = pos + RAMP_BRIGHTNESS_MIN;
        else if (bar == GetDlgItem(pg.dlg, ID_CONTRAST))
            pg.s.contrast[c] = pos;
        else
            return;
    }
    ramp_clamp(&pg.s);
    show_values();
    preview();
    mark_changed();
}

static void
apply(void)
{
    cp_save(&pg.s);
    if (pg.changed || !pg.have_found || memcmp(pg.found, pg.words, sizeof(pg.words)))
        preview();
    memcpy(pg.found, pg.words, sizeof(pg.found));
    pg.have_found = 1;
    pg.changed    = 0;
    cp_event("VGPU98 Color applied: gamma %u/%u/%u, brightness %d/%d/%d, contrast %u/%u/%u%s", (DWORD) pg.s.gamma[0],
             (DWORD) pg.s.gamma[1], (DWORD) pg.s.gamma[2], (DWORD) pg.s.brightness[0], (DWORD) pg.s.brightness[1],
             (DWORD) pg.s.brightness[2], (DWORD) pg.s.contrast[0], (DWORD) pg.s.contrast[1], (DWORD) pg.s.contrast[2],
             ramp_is_default(&pg.s) ? " (the defaults)" : "");
    SetDlgItemTextA(pg.dlg, ID_STATUS, "Saved. Windows gives the card this gamma when it starts.");
}

/* Cancel: the ramp the page found, back. */
static void
reset(void)
{
    HDC dc;

    if (!pg.changed || !pg.have_found)
        return;
    dc = GetDC(NULL);
    if (dc) {
        SetDeviceGammaRamp(dc, pg.found);
        ReleaseDC(NULL, dc);
    }
    pg.changed = 0;
    cp_event("VGPU98 Color cancelled: the gamma the page found put back");
}

static BOOL STDCALL
page_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_INITDIALOG:
            pg.way = (const char *) ((const PROPSHEETPAGEA *) lp)->lParam;
            init(dlg);
            return 1;
        case WM_HSCROLL:
            if (pg.usable)
                trackbar((HWND) lp);
            return 1;
        case WM_COMMAND:
            if ((wp >> 16) != BN_CLICKED)
                return 0;
            switch (wp & 0xFFFF) {
                case ID_ALL:
                case ID_RED:
                case ID_GREEN:
                case ID_BLUE:
                    pg.channel = (int) (wp & 0xFFFF) - ID_ALL;
                    show_trackbars();
                    return 1;
                case ID_DEFAULTS:
                    ramp_defaults(&pg.s);
                    show_trackbars();
                    preview();
                    mark_changed();
                    return 1;
                default:
                    return 0;
            }
        case WM_DRAWITEM:
            if (((const DRAWITEMSTRUCT *) lp)->CtlID == ID_CURVE)
                draw_curve((const DRAWITEMSTRUCT *) lp);
            else if (((const DRAWITEMSTRUCT *) lp)->CtlID == ID_STEPS)
                draw_steps((const DRAWITEMSTRUCT *) lp);
            else
                return 0;
            return 1;
        case WM_NOTIFY:
            switch (((const NMHDR *) lp)->code) {
                case PSN_APPLY:
                    if (pg.usable)
                        apply();
                    SetWindowLongA(dlg, DWL_MSGRESULT, PSNRET_NOERROR);
                    return 1;
                case PSN_RESET:
                    reset();
                    return 1;
                default:
                    return 0;
            }
        case WM_DESTROY:
            /* Closed without Apply or Cancel (the sheet's close box is
               Cancel's): nothing more to do. */
            pg.dlg = NULL;
            return 0;
        default:
            return 0;
    }
}

int
cp_color_page(PROPSHEETPAGEA *page, const char *way)
{
    memset(page, 0, sizeof(*page));
    page->dwSize      = sizeof(*page);
    page->dwFlags     = PSP_DLGINDIRECT | PSP_USETITLE | PSP_USEREFPARENT;
    page->hInstance   = cp_instance;
    page->pResource   = build_template();
    page->pszTitle    = "VGPU98 Color";
    page->pfnDlgProc  = page_proc;
    page->lParam      = (LPARAM) way;
    page->pcRefParent = &cp_page_refs;
    return page->pResource != NULL;
}
