/*
 * vgpu98cp.cpl: the declarations of the Windows 98 interfaces it uses, our
 * own: USER32's dialogs and controls, GDI32's drawing and
 * gamma ramp, COMCTL32's property sheets and trackbars, ADVAPI32's
 * registry, the Control Panel's CPlApplet, and the shell's property sheet
 * extension. Values and layouts as Windows 98 has them.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CPL_WIN_H
#define VGPU98_CPL_WIN_H

#include <stddef.h>
#include <stdint.h>

#include "ddi.h" /* BYTE, WORD, DWORD, LONG, BOOL, HRESULT */

#define STDCALL   __attribute__((stdcall))
#define DLLIMPORT __declspec(dllimport)

typedef uint32_t UINT;
typedef uint32_t WPARAM;
typedef int32_t  LPARAM;
typedef int32_t  LRESULT;
typedef void    *HANDLE;
typedef HANDLE   HWND;
typedef HANDLE   HDC;
typedef HANDLE   HINSTANCE;
typedef HANDLE   HICON;
typedef HANDLE   HBITMAP;
typedef HANDLE   HBRUSH;
typedef HANDLE   HPEN;
typedef HANDLE   HGDIOBJ;
typedef HANDLE   HKEY;
typedef HANDLE   HPROPSHEETPAGE;

typedef struct RECT {
    LONG left, top, right, bottom;
} RECT;

typedef struct POINT {
    LONG x, y;
} POINT;

typedef struct NMHDR {
    HWND hwndFrom;
    UINT idFrom;
    UINT code;
} NMHDR;

typedef struct DRAWITEMSTRUCT {
    UINT  CtlType;
    UINT  CtlID;
    UINT  itemID;
    UINT  itemAction;
    UINT  itemState;
    HWND  hwndItem;
    HDC   hDC;
    RECT  rcItem;
    DWORD itemData;
} DRAWITEMSTRUCT;

typedef struct ICONINFO {
    BOOL    fIcon;
    DWORD   xHotspot;
    DWORD   yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
} ICONINFO;

typedef BOOL(STDCALL *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

/* COMCTL32's property sheets, their Windows 95 layouts (the _V1 sizes). */
struct PROPSHEETPAGEA;
typedef UINT(STDCALL *LPFNPSPCALLBACKA)(HWND, UINT, struct PROPSHEETPAGEA *);

typedef struct PROPSHEETPAGEA {
    DWORD            dwSize;
    DWORD            dwFlags;
    HINSTANCE        hInstance;
    const void      *pResource; /* PSP_DLGINDIRECT: a DLGTEMPLATE in memory */
    HICON            hIcon;
    const char      *pszTitle;
    DLGPROC          pfnDlgProc;
    LPARAM           lParam;
    LPFNPSPCALLBACKA pfnCallback;
    UINT            *pcRefParent;
} PROPSHEETPAGEA;

typedef struct PROPSHEETHEADERA {
    DWORD                 dwSize;
    DWORD                 dwFlags;
    HWND                  hwndParent;
    HINSTANCE             hInstance;
    HICON                 hIcon;
    const char           *pszCaption;
    UINT                  nPages;
    UINT                  nStartPage;
    const PROPSHEETPAGEA *ppsp; /* PSH_PROPSHEETPAGE */
    void                 *pfnCallback;
} PROPSHEETHEADERA;

_Static_assert(sizeof(PROPSHEETPAGEA) == 40, "PROPSHEETPAGEA is not Windows 95's 40 bytes");
_Static_assert(sizeof(PROPSHEETHEADERA) == 40, "PROPSHEETHEADERA is not Windows 95's 40 bytes");

typedef BOOL(STDCALL *LPFNADDPROPSHEETPAGE)(HPROPSHEETPAGE, LPARAM);

/* The Control Panel's (cpl.h). */
typedef struct CPLINFO {
    int  idIcon;
    int  idName;
    int  idInfo;
    LONG lData;
} CPLINFO;

typedef struct NEWCPLINFOA {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHelpContext;
    LONG  lData;
    HICON hIcon; /* CONTROL.EXE's to delete */
    char  szName[32];
    char  szInfo[64];
    char  szHelpFile[128];
} NEWCPLINFOA;

#define CPL_INIT        1
#define CPL_GETCOUNT    2
#define CPL_INQUIRE     3
#define CPL_DBLCLK      5
#define CPL_STOP        6
#define CPL_EXIT        7
#define CPL_NEWINQUIRE  8
#define CPL_DYNAMIC_RES 0

/* COM and the shell's property sheet extension. */
typedef struct GUID {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID;

#define S_OK                      0
#define S_FALSE                   1
#define E_NOINTERFACE             ((HRESULT) 0x80004002)
#define E_FAIL                    ((HRESULT) 0x80004005)
#define E_OUTOFMEMORY             ((HRESULT) 0x8007000E)
#define CLASS_E_NOAGGREGATION     ((HRESULT) 0x80040110)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT) 0x80040111)

/* Window messages and styles. */
#define WM_DESTROY        0x0002
#define WM_DRAWITEM       0x002B
#define WM_NOTIFY         0x004E
#define WM_INITDIALOG     0x0110
#define WM_COMMAND        0x0111
#define WM_HSCROLL        0x0114
#define WM_USER           0x0400
#define BM_GETCHECK       0x00F0
#define BM_SETCHECK       0x00F1
#define BN_CLICKED        0
#define BST_CHECKED       1
#define CB_ADDSTRING      0x0143
#define CB_GETCURSEL      0x0147
#define CB_SETCURSEL      0x014E
#define CBN_SELCHANGE     1

#define WS_CHILD          0x40000000u
#define WS_VISIBLE        0x10000000u
#define WS_DISABLED       0x08000000u
#define WS_CAPTION        0x00C00000u
#define WS_VSCROLL        0x00200000u
#define WS_GROUP          0x00020000u
#define WS_TABSTOP        0x00010000u
#define WS_EX_CLIENTEDGE  0x00000200u
#define DS_3DLOOK         0x0004u
#define DS_SETFONT        0x0040u
#define BS_PUSHBUTTON     0x0000u
#define BS_GROUPBOX       0x0007u
#define BS_AUTORADIOBUTTON 0x0009u
#define SS_LEFT           0x0000u
#define SS_RIGHT          0x0002u
#define SS_OWNERDRAW      0x000Du
#define CBS_DROPDOWNLIST  0x0003u
#define ODT_STATIC        5
#define DWL_MSGRESULT     0

/* The dialog template's class atoms. */
#define CLASS_BUTTON      0x0080
#define CLASS_STATIC      0x0082
#define CLASS_COMBOBOX    0x0085

/* COMCTL32's trackbar and property sheet messages. */
#define TRACKBAR_CLASSA   "msctls_trackbar32"
#define TBS_AUTOTICKS     0x0001u
#define TBM_GETPOS        (WM_USER)
#define TBM_SETPOS        (WM_USER + 5)
#define TBM_SETRANGE      (WM_USER + 6)
#define TBM_SETTICFREQ    (WM_USER + 20)
#define TBM_SETPAGESIZE   (WM_USER + 21)
#define TBM_SETLINESIZE   (WM_USER + 23)
#define PSM_CHANGED       (WM_USER + 104)
#define PSN_SETACTIVE     ((UINT) -200)
#define PSN_APPLY         ((UINT) -202)
#define PSN_RESET         ((UINT) -203)
#define PSNRET_NOERROR    0
#define PSP_DLGINDIRECT   0x0001u
#define PSP_USETITLE      0x0008u
#define PSP_USEREFPARENT  0x0040u
#define PSP_USECALLBACK   0x0080u
#define PSPCB_RELEASE     1
#define PSH_PROPSHEETPAGE 0x0008u

/* Colours, pens and the stock icon. */
#define COLOR_WINDOW      5
#define COLOR_WINDOWTEXT  8
#define COLOR_BTNFACE     15
#define COLOR_BTNSHADOW   16
#define COLOR_GRAYTEXT    17
#define COLOR_BTNTEXT     18
#define PS_SOLID          0
#define PS_DOT            2
#define IDI_APPLICATION   32512
#define RGB(r, g, b)      ((DWORD) ((BYTE) (r) | ((DWORD) (BYTE) (g) << 8) | ((DWORD) (BYTE) (b) << 16)))

/* GDI's device capabilities and the escape's question. */
#define BITSPIXEL         12
#define PLANES            14
#define QUERYESCSUPPORT   8

/* The registry. */
#define HKEY_LOCAL_MACHINE      ((HKEY) 0x80000002u)
#define KEY_READ                0x00020019u
#define KEY_WRITE               0x00020006u
#define REG_OPTION_NON_VOLATILE 0
#define REG_SZ                  1
#define REG_DWORD               4
#define ERROR_SUCCESS           0

/* KERNEL32 */
DLLIMPORT DWORD STDCALL GetModuleFileNameA(HINSTANCE module, char *name, DWORD size);

/* USER32 */
DLLIMPORT HWND STDCALL    GetDlgItem(HWND dlg, int id);
DLLIMPORT LRESULT STDCALL SendMessageA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
DLLIMPORT BOOL STDCALL    SetDlgItemTextA(HWND dlg, int id, const char *text);
DLLIMPORT BOOL STDCALL    EnableWindow(HWND wnd, BOOL enable);
DLLIMPORT BOOL STDCALL    InvalidateRect(HWND wnd, const RECT *rect, BOOL erase);
DLLIMPORT HWND STDCALL    GetParent(HWND wnd);
DLLIMPORT LONG STDCALL    SetWindowLongA(HWND wnd, int index, LONG value);
DLLIMPORT HDC STDCALL     GetDC(HWND wnd);
DLLIMPORT int STDCALL     ReleaseDC(HWND wnd, HDC dc);
DLLIMPORT int STDCALL     FillRect(HDC dc, const RECT *rect, HBRUSH brush);
DLLIMPORT DWORD STDCALL   GetSysColor(int index);
DLLIMPORT HBRUSH STDCALL  GetSysColorBrush(int index);
DLLIMPORT HICON STDCALL   LoadIconA(HINSTANCE module, const char *name);
DLLIMPORT HICON STDCALL   CreateIconIndirect(ICONINFO *info);

/* GDI32 */
DLLIMPORT HDC STDCALL     CreateDCA(const char *driver, const char *device, const char *output, const void *mode);
DLLIMPORT BOOL STDCALL    DeleteDC(HDC dc);
DLLIMPORT int STDCALL     ExtEscape(HDC dc, int escape, int in_bytes, const char *in, int out_bytes, char *out);
DLLIMPORT int STDCALL     GetDeviceCaps(HDC dc, int index);
DLLIMPORT BOOL STDCALL    GetDeviceGammaRamp(HDC dc, void *ramp);
DLLIMPORT BOOL STDCALL    SetDeviceGammaRamp(HDC dc, void *ramp);
DLLIMPORT HPEN STDCALL    CreatePen(int style, int width, DWORD colour);
DLLIMPORT HBRUSH STDCALL  CreateSolidBrush(DWORD colour);
DLLIMPORT HGDIOBJ STDCALL SelectObject(HDC dc, HGDIOBJ object);
DLLIMPORT BOOL STDCALL    DeleteObject(HGDIOBJ object);
DLLIMPORT BOOL STDCALL    MoveToEx(HDC dc, int x, int y, POINT *old);
DLLIMPORT BOOL STDCALL    LineTo(HDC dc, int x, int y);
DLLIMPORT HDC STDCALL     CreateCompatibleDC(HDC dc);
DLLIMPORT HBITMAP STDCALL CreateCompatibleBitmap(HDC dc, int width, int height);
DLLIMPORT HBITMAP STDCALL CreateBitmap(int width, int height, UINT planes, UINT bits, const void *data);

/* COMCTL32 */
DLLIMPORT void STDCALL           InitCommonControls(void);
DLLIMPORT HPROPSHEETPAGE STDCALL CreatePropertySheetPageA(const PROPSHEETPAGEA *page);
DLLIMPORT BOOL STDCALL           DestroyPropertySheetPage(HPROPSHEETPAGE page);
DLLIMPORT int STDCALL            PropertySheetA(const PROPSHEETHEADERA *header);

/* ADVAPI32 */
DLLIMPORT LONG STDCALL RegCreateKeyExA(HKEY key, const char *sub, DWORD reserved, char *cls, DWORD options,
                                       DWORD access, void *security, HKEY *result, DWORD *disposition);
DLLIMPORT LONG STDCALL RegOpenKeyExA(HKEY key, const char *sub, DWORD options, DWORD access, HKEY *result);
DLLIMPORT LONG STDCALL RegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *data,
                                        DWORD *bytes);
DLLIMPORT LONG STDCALL RegSetValueExA(HKEY key, const char *name, DWORD reserved, DWORD type, const BYTE *data,
                                      DWORD bytes);
DLLIMPORT LONG STDCALL RegDeleteValueA(HKEY key, const char *name);
DLLIMPORT LONG STDCALL RegCloseKey(HKEY key);

#endif /* VGPU98_CPL_WIN_H */
