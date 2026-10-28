/*
 * vgpu98cp.cpl: its three ways in. The VGPU98
 * Color and VGPU98 3D tabs under Display Properties > Settings > Advanced
 * come from a property sheet extension: the INF registers its CLSID under
 * the Advanced dialog's handlers (Controls Folder\Device\shellex\
 * PropertySheetHandlers), and the shell asks DllGetClassObject for an
 * object with IShellExtInit and IShellPropSheetExt, whose AddPages gives
 * the pages. The Control Panel's VGPU98 2000 icon is CPlApplet, which shows
 * the same pages in a property sheet of its own. vgpu98_apply is
 * rundll32's when Windows starts. (The Windows 98 SE DDK's control panel
 * extension and monitor samples, read for facts.)
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cpl.h"

#define DLL_PROCESS_ATTACH 1

HINSTANCE cp_instance;
UINT      cp_page_refs;

static LONG objects; /* the shell's objects alive */
static LONG locks;   /* IClassFactory::LockServer */

/* {79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}: the tab's, the INF's. */
static const GUID clsid_tab = { 0x79D3DDE2, 0x6AC4, 0x43A5, { 0x86, 0x0C, 0x5C, 0x7E, 0x56, 0xC6, 0x89, 0xF2 } };
static const GUID iid_unknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID iid_class_factory = { 0x00000001, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID iid_shell_ext_init = { 0x000214E8, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID iid_shell_prop_sheet_ext = { 0x000214E9, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static int
same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

/* --- The tab: one object with IShellPropSheetExt and IShellExtInit --------------- */

/* The object has no state of its own, so every CreateInstance gives the
   same one, counted. */
typedef struct tab_t {
    const void *const *psx; /* IShellPropSheetExt (and IUnknown) */
    const void *const *sxi; /* IShellExtInit */
} tab_t;

static tab_t tab;

static HRESULT STDCALL
tab_query(void *self, const GUID *riid, void **out)
{
    (void) self;
    if (same_guid(riid, &iid_unknown) || same_guid(riid, &iid_shell_prop_sheet_ext))
        *out = &tab.psx;
    else if (same_guid(riid, &iid_shell_ext_init))
        *out = &tab.sxi;
    else {
        *out = NULL;
        return E_NOINTERFACE;
    }
    __atomic_add_fetch(&objects, 1, __ATOMIC_SEQ_CST);
    return S_OK;
}

static DWORD STDCALL
tab_add_ref(void *self)
{
    (void) self;
    return (DWORD) __atomic_add_fetch(&objects, 1, __ATOMIC_SEQ_CST);
}

static DWORD STDCALL
tab_release(void *self)
{
    (void) self;
    return (DWORD) __atomic_sub_fetch(&objects, 1, __ATOMIC_SEQ_CST);
}

/* The pages, in their tabs' order. */
static int (*const pages[2])(PROPSHEETPAGEA *page, const char *way) = { cp_color_page, cp_3d_page };

static HRESULT STDCALL
tab_add_pages(void *self, LPFNADDPROPSHEETPAGE add, LPARAM lp)
{
    PROPSHEETPAGEA page;
    HPROPSHEETPAGE h;
    int            added = 0;
    unsigned       i;

    (void) self;
    InitCommonControls(); /* the trackbar's class, in the shell's process */
    /* CreatePropertySheetPage keeps its own copy of the page; the template
       stays the page's (tmpl.h). */
    for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        if (!pages[i](&page, "the Advanced tab"))
            continue;
        h = CreatePropertySheetPageA(&page);
        if (!h)
            continue;
        if (!add(h, lp))
            DestroyPropertySheetPage(h);
        else
            added++;
    }
    return added ? S_OK : E_FAIL;
}

static HRESULT STDCALL
tab_replace_pages(void *self, UINT id, LPFNADDPROPSHEETPAGE replace, LPARAM lp)
{
    (void) self;
    (void) id;
    (void) replace;
    (void) lp;
    return E_FAIL; /* the tab replaces none of the dialog's */
}

static HRESULT STDCALL
sxi_initialize(void *self, const void *pidl, void *data, HKEY key)
{
    (void) self;
    (void) pidl;
    (void) data;
    (void) key;
    return S_OK;
}

static const void *const psx_vtable[] = { (const void *) tab_query, (const void *) tab_add_ref,
                                          (const void *) tab_release, (const void *) tab_add_pages,
                                          (const void *) tab_replace_pages };
static const void *const sxi_vtable[] = { (const void *) tab_query, (const void *) tab_add_ref,
                                          (const void *) tab_release, (const void *) sxi_initialize };

/* --- Its class factory ---------------------------------------------------------- */

static const void *const *factory;

static HRESULT STDCALL
factory_query(void *self, const GUID *riid, void **out)
{
    if (same_guid(riid, &iid_unknown) || same_guid(riid, &iid_class_factory)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static DWORD STDCALL
factory_add_ref(void *self)
{
    (void) self;
    return 2; /* static */
}

static DWORD STDCALL
factory_release(void *self)
{
    (void) self;
    return 1;
}

static HRESULT STDCALL
factory_create(void *self, void *outer, const GUID *riid, void **out)
{
    (void) self;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    tab.psx = psx_vtable;
    tab.sxi = sxi_vtable;
    return tab_query(&tab, riid, out);
}

static HRESULT STDCALL
factory_lock(void *self, BOOL lock)
{
    (void) self;
    if (lock)
        __atomic_add_fetch(&locks, 1, __ATOMIC_SEQ_CST);
    else
        __atomic_sub_fetch(&locks, 1, __ATOMIC_SEQ_CST);
    return S_OK;
}

static const void *const factory_vtable[] = { (const void *) factory_query, (const void *) factory_add_ref,
                                              (const void *) factory_release, (const void *) factory_create,
                                              (const void *) factory_lock };

HRESULT STDCALL
DllGetClassObject(const GUID *clsid, const GUID *riid, void **out)
{
    *out = NULL;
    if (!same_guid(clsid, &clsid_tab))
        return CLASS_E_CLASSNOTAVAILABLE;
    factory = factory_vtable;
    return factory_query(&factory, riid, out);
}

HRESULT STDCALL
DllCanUnloadNow(void)
{
    return (__atomic_load_n(&objects, __ATOMIC_SEQ_CST) || __atomic_load_n(&locks, __ATOMIC_SEQ_CST) ||
            __atomic_load_n(&cp_page_refs, __ATOMIC_SEQ_CST))
               ? S_FALSE
               : S_OK;
}

/* --- The Control Panel ----------------------------------------------------------- */

/* The icon, drawn here (the linker writes no resources): a grey monitor of
   the time with a teal screen, as Windows 98's own display icon has it. */
static HICON
make_icon(void)
{
    static BYTE mask[32 * 4];
    HDC         screen = GetDC(NULL);
    HDC         mem;
    HBITMAP     colour;
    HBITMAP     bits;
    HGDIOBJ     old;
    ICONINFO    info;
    HICON       icon = NULL;
    int         x;
    int         y;
    static const struct {
        int   left, top, right, bottom; /* exclusive */
        DWORD colour;
    } parts[] = {
        { 2, 3, 30, 24, RGB(192, 192, 192) }, /* the case */
        { 2, 3, 30, 4, RGB(255, 255, 255) },  /* its light top and left */
        { 2, 3, 3, 24, RGB(255, 255, 255) },
        { 2, 23, 30, 24, RGB(128, 128, 128) }, /* its shaded bottom and right */
        { 29, 3, 30, 24, RGB(128, 128, 128) },
        { 5, 6, 27, 21, RGB(0, 0, 0) },        /* the screen's frame */
        { 6, 7, 26, 20, RGB(0, 128, 128) },    /* the screen */
        { 13, 24, 19, 27, RGB(128, 128, 128) }, /* the stand */
        { 8, 27, 24, 29, RGB(192, 192, 192) },  /* the base */
    };
    unsigned i;

    if (!screen)
        return LoadIconA(NULL, (const char *) IDI_APPLICATION);
    /* The mask: 0 where the monitor is, 1 (transparent) elsewhere. */
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            const int in = ((x >= 2) && (x < 30) && (y >= 3) && (y < 24)) ||   /* the case */
                           ((x >= 13) && (x < 19) && (y >= 24) && (y < 27)) || /* the stand */
                           ((x >= 8) && (x < 24) && (y >= 27) && (y < 29));    /* the base */

            if (in)
                mask[y * 4 + x / 8] &= (BYTE) ~(0x80 >> (x % 8));
            else
                mask[y * 4 + x / 8] |= (BYTE) (0x80 >> (x % 8));
        }
    }
    mem    = CreateCompatibleDC(screen);
    colour = CreateCompatibleBitmap(screen, 32, 32);
    bits   = CreateBitmap(32, 32, 1, 1, mask);
    if (mem && colour && bits) {
        RECT all = { 0, 0, 32, 32 };

        old = SelectObject(mem, colour);
        {
            HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));

            FillRect(mem, &all, black); /* black under the transparent part */
            DeleteObject(black);
        }
        for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
            RECT   r = { parts[i].left, parts[i].top, parts[i].right, parts[i].bottom };
            HBRUSH b = CreateSolidBrush(parts[i].colour);

            FillRect(mem, &r, b);
            DeleteObject(b);
        }
        SelectObject(mem, old);
        info.fIcon    = 1;
        info.xHotspot = 0;
        info.yHotspot = 0;
        info.hbmMask  = bits;
        info.hbmColor = colour;
        icon          = CreateIconIndirect(&info);
    }
    if (bits)
        DeleteObject(bits);
    if (colour)
        DeleteObject(colour);
    if (mem)
        DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return icon ? icon : LoadIconA(NULL, (const char *) IDI_APPLICATION);
}

static void
copy_text(char *dst, size_t size, const char *src)
{
    size_t n = 0;

    while (src[n] && (n + 1 < size)) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

LONG STDCALL
CPlApplet(HWND owner, UINT msg, LPARAM lp1, LPARAM lp2)
{
    (void) lp1;
    switch (msg) {
        case CPL_INIT:
            InitCommonControls();
            return 1;
        case CPL_GETCOUNT:
            return 1;
        case CPL_INQUIRE:
            {
                CPLINFO *info = (CPLINFO *) lp2;

                /* Nothing from resources: CPL_NEWINQUIRE's icon and text. */
                info->idIcon = CPL_DYNAMIC_RES;
                info->idName = CPL_DYNAMIC_RES;
                info->idInfo = CPL_DYNAMIC_RES;
                info->lData  = 0;
                return 0;
            }
        case CPL_NEWINQUIRE:
            {
                NEWCPLINFOA *info = (NEWCPLINFOA *) lp2;

                memset(info, 0, sizeof(*info));
                info->dwSize = sizeof(*info);
                info->hIcon  = make_icon();
                copy_text(info->szName, sizeof(info->szName), CPL_NAME);
                copy_text(info->szInfo, sizeof(info->szInfo),
                          "Adjusts the VGPU98 2000's color and its 3D settings.");
                return 0;
            }
        case CPL_DBLCLK:
            {
                PROPSHEETPAGEA   page[2];
                PROPSHEETHEADERA sheet;
                UINT             n = 0;
                unsigned         i;

                for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
                    if (pages[i](&page[n], "the Control Panel"))
                        n++;
                }
                if (!n)
                    return 0;
                memset(&sheet, 0, sizeof(sheet));
                sheet.dwSize     = sizeof(sheet);
                sheet.dwFlags    = PSH_PROPSHEETPAGE;
                sheet.hwndParent = owner;
                sheet.hInstance  = cp_instance;
                sheet.pszCaption = CPL_NAME " Properties";
                sheet.nPages     = n;
                sheet.ppsp       = page;
                PropertySheetA(&sheet);
                return 0;
            }
        default:
            return 0;
    }
}

/* --- Start-up ------------------------------------------------------------------- */

/* rundll32.exe vgpu98cp.cpl,vgpu98_apply: the saved gamma and V-sync
   overrides to the card. */
void STDCALL
vgpu98_apply(HWND wnd, HINSTANCE inst, char *line, int show)
{
    (void) wnd;
    (void) inst;
    (void) line;
    (void) show;
    cp_apply_saved();
}

BOOL STDCALL
DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void) reserved;
    if (reason == DLL_PROCESS_ATTACH)
        cp_instance = inst;
    return 1;
}
