/*
 * The Glide DLLs: finding the VGPU98 2000, the DLL's entry point, lines to
 * the host's log and the call record.
 *
 * The card is found once a process, at the program's first Glide call, not
 * in DllMain: GDI's calls are best kept out of the loader's lock. The
 * display driver's escape gives the shared block's address and the driver's
 * build stamp; the block holds BAR0's address and the log's line lock,
 * which the DirectX DLL's lines take too, so the host's one line buffer
 * gets whole lines.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"

#include "logline.h"

/* The card, for the ring too (gpuenv.h). */
vgpu98_shared_t *sb;
volatile DWORD  *regs;
uint8_t         *vram; /* video memory, through the display driver's mapping */
int              diag_on;     /* Diagnostics "Glide": every line, and the counters */
int              diag_events; /* any level but Off: the event lines */

glide_ring_diag_t diag;

static DWORD instance; /* the DLL's own, from DllMain: its file's path */
static DWORD own_lock; /* the log's lock while the block is another build's */
static int   found;    /* the card is there */
static char  prefix[96];   /* "<tag> <PROGRAM>: ", every line's start */
static char  program[260]; /* the program's full path */

/* The export the program is in, for the ring's long-wait lines. */
static const char *volatile current_export;

/* The last calls, with their first argument dwords:
   written after a fault, and at an unload without
   grGlideShutdown. */
#define RECENT 32 /* a power of two */

typedef struct recent_call {
    const char *name;
    DWORD       args[4];
    DWORD       tick;
    DWORD       thread;
    DWORD       calls; /* in a row on its thread (a poll's repeats fold into one entry: the last's arguments and time) */
    /* (Two threads at once may fold into, or write, the same entry: a
       diagnostic's race, as the ring's wrap-around already has.) */
} recent_call_t;

static recent_call_t  recent[RECENT];
static volatile DWORD recent_n;
static int            shutdown_seen;

static void recent_write(const char *why, int event);
static void fault_catcher(void);
static void fault_catcher_end(void);

/* 0 not looked for, 1 being looked for (by another thread), 2 done. */
static volatile DWORD look_state;

/* The exports called in the process (glide_called), and the calls counted
   at the last call record. */
static glide_export_t *volatile exports;
static DWORD                    recorded_calls;

BOOL STDCALL DllMain(DWORD dll_instance, DWORD reason, void *reserved);

/* Nothing else at load: the card is found at the program's first Glide
   call. At unload, a window still open closes (whole at a FreeLibrary, on
   the device alone as the process ends: `reserved` says which), and the
   calls since the last record (at Glide). */
BOOL STDCALL
DllMain(DWORD dll_instance, DWORD reason, void *reserved)
{
    if (reason == 1) /* DLL_PROCESS_ATTACH */
        instance = dll_instance;
    else if (reason == 0) { /* DLL_PROCESS_DETACH */
        glc_detach(reserved != NULL);
        glide_record();
        if (!shutdown_seen && recent_n)
            recent_write("unloaded without grGlideShutdown", 0);
        /* Unloaded by the program: its filter no longer the DLL's, whose
           code goes. */
        if (!reserved)
            fault_catcher_end();
    }
    return 1;
}

/* --- Lines ----------------------------------------------------------------- */

static void
write_line(const char *fmt, va_list ap)
{
    log_line(regs, sb ? &sb->log_lock : &own_lock, prefix, fmt, ap);
}

void
glide_event(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !diag_events)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

void
glide_dbg(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !diag_on)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

/* The ring's lines (gpuenv.h): its detail at Glide, its events at any
   level but Off. */
void
dbg(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !diag_on)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

static int
ring_event(const char *fmt, va_list ap)
{
    if (!regs || !diag_events)
        return 0;
    write_line(fmt, ap);
    return 1;
}

int
dbg_event(const char *fmt, ...)
{
    va_list ap;
    int     r;

    va_start(ap, fmt);
    r = ring_event(fmt, ap);
    va_end(ap);
    return r;
}

int
dbg_event_own(const char *fmt, ...)
{
    va_list ap;
    int     r;

    va_start(ap, fmt);
    r = ring_event(fmt, ap);
    va_end(ap);
    return r;
}

const char *
flight_current(void)
{
    const char *name = current_export;

    return name ? name : "a Glide call";
}

/* As a batch ends, at Glide: nothing here (the device writes the Glide
   report). */
void
diag_report(void)
{
}

static size_t
text_length(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;
    return n;
}

/* Do two strings match, letters in either case? */
static int
same_text(const char *a, const char *b, int any_case)
{
    for (;; a++, b++) {
        char x = *a, y = *b;

        if (any_case) {
            x = ((x >= 'a') && (x <= 'z')) ? (char) (x - 'a' + 'A') : x;
            y = ((y >= 'a') && (y <= 'z')) ? (char) (y - 'a' + 'A') : y;
        }
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* Every line's start: the DLL's tag and the program's file name, without
   its folder (at most 20 characters of it, so every line fits after it;
   the full path has a line of its own at Glide). The full path is kept for
   the DLL check. */
static void
make_prefix(void)
{
    const char *name = program;
    const char *s;
    size_t      n = 0, k = 0;

    if (!GetModuleFileNameA(0, program, sizeof(program)))
        program[0] = 0;
    program[sizeof(program) - 1] = 0;
    for (s = program; *s; s++) {
        if ((*s == '\\') || (*s == '/') || (*s == ':'))
            name = s + 1;
    }
    for (s = glide_tag; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    if (n + 1 < sizeof(prefix))
        prefix[n++] = ' ';
    for (s = name; *s && (k < 20) && (n + 1 < sizeof(prefix)); s++, k++)
        prefix[n++] = *s;
    while (k && (prefix[n - 1] == ' ')) { /* a name cut at a space */
        n--;
        k--;
    }
    if (!k && (n + 1 < sizeof(prefix)))
        prefix[n++] = '?';
    for (s = ": "; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    prefix[n] = 0;
}

/* What a line has room for after its prefix and a short heading. */
static size_t
line_room(void)
{
    const size_t used = text_length(prefix) + 24;

    return (used < 159 - 40) ? 159 - used : 40;
}

/* --- The DLL check ---------- */

/* The DLL's own file and whether its folder is the system folder; the
   program's file. */
static void
check_files(void)
{
    char  path[260], system[260];
    DWORD folder = 0, k;
    int   has_folder = 0;

    path[0] = system[0] = 0;
    if (!GetModuleFileNameA(instance, path, sizeof(path)))
        path[0] = 0;
    path[sizeof(path) - 1] = 0;
    if (!GetSystemDirectoryA(system, sizeof(system)) || (text_length(system) >= sizeof(system) - 1))
        system[0] = 0;
    for (k = 0; path[k]; k++) {
        if (path[k] == '\\') {
            folder     = k;
            has_folder = 1;
        }
    }
    if (!path[0])
        glide_dbg("file not known");
    else {
        int in_system = 0;

        if (has_folder) {
            path[folder] = 0;
            in_system    = system[0] && same_text(path, system, 1);
            path[folder] = '\\';
        }
        /* The verdict first: a long path may run past the line's end. */
        if (in_system)
            glide_dbg("file (the system folder): %s", path);
        else
            glide_dbg("file (not the system folder, %s): %s", system[0] ? system : "not known", path);
    }
    glide_dbg("program %s", program[0] ? program : "not known");
}

/* The DLL's build against the display driver's: another means an old copy
   of one of them, an event at every level. */
static void
check_build(const vgpu98_escinfo_t *info)
{
    char stamp[sizeof(info->stamp) + 1];

    if (info->size < sizeof(*info)) {
        glide_event("possible stale Glide DLL: built %s; the display driver gives no stamp (an older build)",
                    VGPU98_BUILD_STAMP);
        return;
    }
    memcpy(stamp, info->stamp, sizeof(info->stamp));
    stamp[sizeof(info->stamp)] = 0;
    if (!same_text(stamp, VGPU98_BUILD_STAMP, 0))
        glide_event("possible stale Glide DLL: built %s, the display driver %s", VGPU98_BUILD_STAMP, stamp);
    else
        glide_dbg("build %s, the display driver's %s", VGPU98_BUILD_STAMP, stamp);
}

/* Is the variable NAME=value one of Glide's? */
static int
glide_variable(const char *var)
{
    static const char *const prefixes[] = { "FX_GLIDE_", "SST_", "SSTV2_", "SSTH3_" };
    size_t                   i, k;

    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        for (k = 0; prefixes[i][k]; k++) {
            char c = var[k];

            c = ((c >= 'a') && (c <= 'z')) ? (char) (c - 'a' + 'A') : c;
            if (c != prefixes[i][k])
                break;
        }
        if (!prefixes[i][k])
            return 1;
    }
    return 0;
}

/* The Glide variables set, NAME=value (each at most 100 characters), in as
   many lines as they need, at most six. */
static void
check_environment(void)
{
    char       *block = GetEnvironmentStringsA();
    const char *var;
    char        text[160];
    size_t      n = 0, room = line_room();
    int         lines = 0, any = 0;

    if (!block) {
        glide_dbg("environment: not readable");
        return;
    }
    for (var = block; *var; var += text_length(var) + 1) {
        size_t len;

        if (!glide_variable(var))
            continue;
        any = 1;
        len = text_length(var);
        if (len > 100)
            len = 100;
        if (len > room)
            len = room; /* one variable alone still fits its line */
        if (n && (n + 1 + len > room)) {
            text[n] = 0;
            if (++lines <= 6)
                glide_dbg("%s %s", (lines == 1) ? "environment:" : "environment, more:", text);
            n = 0;
        }
        if (n)
            text[n++] = ' ';
        memcpy(text + n, var, len);
        n += len;
    }
    if (n) {
        text[n] = 0;
        if (++lines <= 6)
            glide_dbg("%s %s", (lines == 1) ? "environment:" : "environment, more:", text);
    }
    if (lines > 6)
        glide_dbg("environment: %u more lines not shown", (DWORD) (lines - 6));
    if (!any)
        glide_dbg("environment: no Glide variables");
    FreeEnvironmentStringsA(block);
}

/* --- Finding the card ------------------------------------------------------ */

/* The display driver's escape, the shared block, then BAR0. */
static void
look_for_card(void)
{
    vgpu98_escquery_t query;
    vgpu98_escinfo_t  info;
    vgpu98_shared_t  *s   = NULL;
    const int         esc = VGPU98_ESC_INFO;
    DWORD             dc  = CreateDCA("DISPLAY", NULL, NULL, NULL);

    make_prefix();
    if (!dc)
        return; /* nobody to tell */
    query.magic    = ESC_INFO_MAGIC;
    query.out_size = sizeof(info);
    memset(&info, 0, sizeof(info));
    /* The whole answer, or its head from a display driver of before the
       stamp. */
    if ((ExtEscape(dc, QUERYESCSUPPORT, sizeof(esc), (const char *) &esc, 0, NULL) > 0) &&
        (ExtEscape(dc, VGPU98_ESC_INFO, sizeof(query), (const char *) &query, sizeof(info), (char *) &info) > 0) &&
        (info.magic == ESC_INFO_MAGIC) && ((info.size == sizeof(info)) || (info.size == ESC_INFO_HEAD)))
        s = (vgpu98_shared_t *) info.shared_linear;
    DeleteDC(dc);
    if (!s || (s->magic != SB_MAGIC))
        return; /* not the VGPU98 2000's display driver: nobody to tell */

    /* The block's first fields keep their offsets in every version, so a
       block from another build still leads to the log. */
    regs = (volatile DWORD *) s->regs_linear;
    if (regs && (regs[VGPU98_REG_ID / 4] != VGPU98_ID_MAGIC))
        regs = NULL;
    if (!regs)
        return;
    /* Read once: the level changes only with the machine's reset. */
    diag_on  = (regs[VGPU98_REG_DEBUG / 4] & VGPU98_DEBUG_GLIDE) != 0;
    diag_events = (regs[VGPU98_REG_DEBUG / 4] & (VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_LISTENING | VGPU98_DEBUG_GLIDE)) != 0;
    if ((s->version != SB_VERSION) || (s->size != sizeof(vgpu98_shared_t)) || (info.sb_version != SB_VERSION)) {
        glide_event("no Glide board: another build's display driver (block %u/%u bytes, answer %u; this DLL's "
                    "%u/%u bytes)",
                    s->version, s->size, info.sb_version, SB_VERSION, (DWORD) sizeof(vgpu98_shared_t));
        /* Nothing more from this process: its lines would go out under a
           lock of their own (the DirectX DLL does the same). */
        regs = NULL;
        return;
    }
    sb    = s;
    vram  = (uint8_t *) s->fb_linear;
    found = 1;
    glide_event("loaded; a Voodoo3 (%u MB, two TMUs) for %s", VOODOO3_MEMORY_MB, glide_api);
    check_files();
    check_build(&info);
    if (diag_on)
        check_environment();
}

int
glide_card(void)
{
    DWORD expect = 0;

    if (__atomic_load_n(&look_state, __ATOMIC_ACQUIRE) == 2)
        return found;
    if (__atomic_compare_exchange_n(&look_state, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        look_for_card();
        __atomic_store_n(&look_state, 2, __ATOMIC_RELEASE);
    } else {
        /* Another of the program's threads is looking: its answer. */
        while (__atomic_load_n(&look_state, __ATOMIC_ACQUIRE) != 2)
            Sleep(1);
    }
    return found;
}

/* --- Calls and the call record ---- */

void
glide_called(glide_export_t *e, const DWORD *args)
{
    const DWORD    thread = GetCurrentThreadId();
    const DWORD    n      = __atomic_load_n(&recent_n, __ATOMIC_ACQUIRE);
    recent_call_t *r      = n ? &recent[(n - 1) & (RECENT - 1)] : NULL;

    /* The same export again on the same thread (a poll): one entry. */
    if (r && (r->name == e->name) && (r->thread == thread) && (r->calls < 0xffffffffu))
        r->calls++;
    else {
        r         = &recent[(__atomic_fetch_add(&recent_n, 1, __ATOMIC_ACQ_REL)) & (RECENT - 1)];
        r->name   = e->name;
        r->thread = thread;
        r->calls  = 1;
    }
    r->args[0] = args[0];
    r->args[1] = args[1];
    r->args[2] = args[2];
    r->args[3] = args[3];
    r->tick    = GetTickCount();
    e->calls++;
    current_export = e->name;
    if (e->listed || __atomic_exchange_n(&e->listed, 1, __ATOMIC_ACQ_REL))
        return;
    /* Its first call in the process: on the list, and noted at Glide. The
       first call of any export finds the card first. */
    glide_card();
    {
        glide_export_t *head = __atomic_load_n(&exports, __ATOMIC_ACQUIRE);

        do
            e->next = head;
        while (!__atomic_compare_exchange_n(&exports, &head, e, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    }
    glide_dbg("first call: %s%s", e->name, e->does_nothing ? " (does nothing yet)" : "");
    fault_catcher();
}

void
glide_shutdown_seen(void)
{
    shutdown_seen = 1;
}

/* The last calls, oldest first: an event's lines after a fault, Glide's
   at an unload. */
static void
recent_write(const char *why, int event)
{
    const DWORD n     = recent_n;
    const DWORD first = (n > RECENT) ? n - RECENT : 0;
    DWORD       i;

    if (event)
        glide_event("the last %u Glide calls (%s), oldest first:", n - first, why);
    else
        glide_dbg("the last %u Glide calls (%s), oldest first:", n - first, why);
    for (i = first; i < n; i++) {
        const recent_call_t *r = &recent[i & (RECENT - 1)];
        char                 times[16];

        /* " x" and the count, for a call repeated in a row. */
        times[0] = 0;
        if (r->calls > 1) {
            char  digits[10];
            DWORD c = r->calls;
            int   k = 0, j = 2;

            do {
                digits[k++] = (char) ('0' + c % 10);
                c /= 10;
            } while (c && (k < 10));
            times[0] = ' ';
            times[1] = 'x';
            while (k)
                times[j++] = digits[--k];
            times[j] = 0;
        }
        if (event)
            glide_event("  %s(%08x %08x %08x %08x)%s at %u ms, thread %x", r->name ? r->name : "?", r->args[0], r->args[1],
                        r->args[2], r->args[3], times, r->tick, r->thread);
        else
            glide_dbg("  %s(%08x %08x %08x %08x)%s at %u ms, thread %x", r->name ? r->name : "?", r->args[0], r->args[1],
                      r->args[2], r->args[3], times, r->tick, r->thread);
    }
}

/* --- The fault catcher -------------------- */

/* EXCEPTION_RECORD and EXCEPTION_POINTERS, as far as the catcher reads
   them, and CONTEXT's offsets of Eip and Esp (i386). */
typedef struct exc_record {
    DWORD code;
    DWORD flags;
    void *next;
    DWORD address;
} exc_record_t;

typedef struct exc_pointers {
    exc_record_t  *rec;
    const uint8_t *ctx;
} exc_pointers_t;

#define CTX_EIP 0xb8
#define CTX_ESP 0xc4

static glide_filter_t previous_filter;
static volatile DWORD in_filter;
static volatile DWORD placed;

/* The DLL's image's bytes, from its PE header. */
static DWORD
image_size(void)
{
    const uint8_t *b = (const uint8_t *) (uintptr_t) instance;
    DWORD          pe, size;

    if (!b)
        return 0;
    memcpy(&pe, b + 0x3c, 4);
    if (pe > 0x1000)
        return 0;
    memcpy(&size, b + pe + 80, 4);
    return size;
}

/* A fault the program does not handle: in the DLL, an event with the
   export, the fault, its place in the DLL, the DLL's return addresses on
   the stack, and the last calls; in the program during Glide calls, those
   at Glide. Then the program's own filter, or Windows'. */
static LONG STDCALL
fault_filter(void *pointers)
{
    const exc_pointers_t *ep = (const exc_pointers_t *) pointers;

    if (ep && ep->rec && ep->ctx && !__atomic_exchange_n(&in_filter, 1, __ATOMIC_ACQ_REL)) {
        const DWORD base = instance, size = image_size();
        DWORD       eip, esp, rets[6], n_rets = 0, i;

        memcpy(&eip, ep->ctx + CTX_EIP, 4);
        memcpy(&esp, ep->ctx + CTX_ESP, 4);
        /* The DLL's addresses on the stack, the way back from the fault. */
        for (i = 0; (i < 256) && (n_rets < 6); i++) {
            const DWORD *at = (const DWORD *) (uintptr_t) (esp + i * 4);
            DWORD        v;

            if (IsBadReadPtr(at, 4))
                break;
            v = *at;
            if (size && (v - base < size))
                rets[n_rets++] = v - base;
        }
        while (n_rets < 6)
            rets[n_rets++] = 0;
        if (size && (eip - base < size)) {
            glide_event("a fault in the Glide DLL in %s: code %08x at +%x", flight_current(), ep->rec->code, eip - base);
            glide_event("  its addresses on the stack: +%x +%x +%x +%x +%x +%x", rets[0], rets[1], rets[2], rets[3], rets[4],
                        rets[5]);
            recent_write("before the fault", 1);
        } else {
            glide_dbg("the program faulted (code %08x at %08x), not in the Glide DLL", ep->rec->code, eip);
            recent_write("before the program's fault", 0);
        }
        __atomic_store_n(&in_filter, 0, __ATOMIC_RELEASE);
    }
    /* The program's filter from before: never the DLL's own (loaded twice),
       never one in memory that went. */
    {
        const glide_filter_t prev = previous_filter;
        const DWORD          at   = (DWORD) (uintptr_t) prev;

        if (!prev || (prev == fault_filter) || ((at - instance) < image_size()) || IsBadReadPtr((const void *) prev, 4))
            return 0; /* EXCEPTION_CONTINUE_SEARCH */
        return prev(pointers);
    }
}

/* Put in place at the first call, with the card and the log there; the
   program's filter from before is called after it. */
static void
fault_catcher(void)
{
    if (!regs || !diag_events || __atomic_exchange_n(&placed, 1, __ATOMIC_ACQ_REL))
        return;
    previous_filter = SetUnhandledExceptionFilter(fault_filter);
}

/* The program's filter from before back, if the DLL's is still the one in
   place (one the program set since stays). */
static void
fault_catcher_end(void)
{
    glide_filter_t now;

    if (!__atomic_exchange_n(&placed, 0, __ATOMIC_ACQ_REL))
        return;
    now = SetUnhandledExceptionFilter(previous_filter);
    if (now != fault_filter)
        SetUnhandledExceptionFilter(now);
}

/* Busier first; of as many calls, by name. */
static int
before(const glide_export_t *a, const glide_export_t *b)
{
    const char *x = a->name, *y = b->name;

    if (a->calls != b->calls)
        return a->calls > b->calls;
    while (*x && (*x == *y)) {
        x++;
        y++;
    }
    return (unsigned char) *x < (unsigned char) *y;
}

/* One group of the record: "done:" or "nothing yet:", then each export's
   name and count, in as many lines as they need. */
static void
record_group(glide_export_t **sorted, DWORD n, int does_nothing, const char *heading)
{
    char   text[160];
    size_t len = 0, room = line_room();
    int    lines = 0;
    DWORD  i;

    for (i = 0; i < n; i++) {
        char        item[80];
        char       *p = item, *end = item + sizeof(item) - 1;
        const char *s;

        if (sorted[i]->does_nothing != does_nothing)
            continue;
        for (s = sorted[i]->name; *s && (p < end - 12); s++)
            *p++ = *s;
        *p++ = ' ';
        p = log_num(p, end, sorted[i]->calls, 10, 0, 0);
        *p = 0;
        if (len && (len + 2 + (size_t) (p - item) > room)) {
            text[len] = 0;
            glide_dbg("%s%s %s", heading, lines++ ? ", more:" : ":", text);
            len = 0;
        }
        if (len) {
            text[len++] = ',';
            text[len++] = ' ';
        }
        memcpy(text + len, item, (size_t) (p - item));
        len += (size_t) (p - item);
    }
    if (len) {
        text[len] = 0;
        glide_dbg("%s%s %s", heading, lines ? ", more:" : ":", text);
    }
}

void
glide_record(void)
{
    static glide_export_t *sorted[256];
    static volatile DWORD  busy; /* one record at a time: the array is static */
    glide_export_t        *e;
    DWORD                  n = 0, calls = 0, nothing = 0, i;

    if (!regs || !diag_on || __atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
        return;
    for (e = __atomic_load_n(&exports, __ATOMIC_ACQUIRE); e && (n < sizeof(sorted) / sizeof(sorted[0])); e = e->next) {
        DWORD k = n++;

        /* In order as they come: an insertion sort of at most the DLL's
           exports. */
        while (k && before(e, sorted[k - 1])) {
            sorted[k] = sorted[k - 1];
            k--;
        }
        sorted[k] = e;
        calls += e->calls;
    }
    if (n && (calls != recorded_calls)) {
        recorded_calls = calls;
        for (i = 0; i < n; i++)
            nothing += (DWORD) sorted[i]->does_nothing;
        glide_dbg("call record: %u exports, %u calls; %u of the exports do nothing yet", n, calls, nothing);
        record_group(sorted, n, 0, "done");
        record_group(sorted, n, 1, "nothing yet");
    }
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
}

/* --- Windows --------------------------------------------------------------- */

void
glide_refuse_window(FxU32 hwnd, FxU32 res, FxU32 refresh, FxU32 color_format, FxU32 origin, FxU32 col_buffers,
                    FxU32 aux_buffers)
{
    static volatile DWORD refused;
    const DWORD           n     = __atomic_add_fetch(&refused, 1, __ATOMIC_ACQ_REL);
    const DWORD           limit = diag_on ? 64 : 3; /* a program may retry every frame */

    /* The verdict first, the buffers as colour + aux: the line fits after
       any prefix. */
    if (n <= limit)
        glide_event("grSstWinOpen refused: %s, %s, window %x, colour format %u, origin %u, buffers %u+%u",
                    glide_resolution(res), glide_refresh(refresh), hwnd, color_format, origin, col_buffers,
                    aux_buffers);
    else if (n == limit + 1)
        glide_event("grSstWinOpen refused again: no more of its lines from this process");
}

const char *
glide_resolution(FxU32 res)
{
    /* GR_RESOLUTION_320x200 (0) to GR_RESOLUTION_2048x2048 (17h). */
    static const char *const names[] = { "320x200",   "320x240",   "400x256",   "512x384",   "640x200",
                                         "640x350",   "640x400",   "640x480",   "800x600",   "960x720",
                                         "856x480",   "512x256",   "1024x768",  "1280x1024", "1600x1200",
                                         "400x300",   "1152x864",  "1280x960",  "1600x1024", "1792x1344",
                                         "1856x1392", "1920x1440", "2048x1536", "2048x2048" };

    if (res == 0xff)
        return "window size";
    return (res < sizeof(names) / sizeof(names[0])) ? names[res] : "?";
}

const char *
glide_refresh(FxU32 rate)
{
    /* GR_REFRESH_60Hz (0) to GR_REFRESH_120Hz (8): 85 Hz comes after 100. */
    static const char *const names[] = { "60 Hz", "70 Hz", "72 Hz", "75 Hz", "80 Hz",
                                         "90 Hz", "100 Hz", "85 Hz", "120 Hz" };

    if (rate == 0xff)
        return "no refresh";
    return (rate < sizeof(names) / sizeof(names[0])) ? names[rate] : "?";
}
