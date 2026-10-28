/*
 * vgpu98dx.dll: the fault guard (spec/directx-hal.md).
 * Every callback, and every DrawPrimitives2 command, runs inside a Win32
 * exception frame of the DLL's own: a record on the stack linked into the
 * thread's chain at FS:[0], built by hand (the DLL is freestanding). A fault
 * inside resumes the guard's caller with the call abandoned and the fault's
 * figures recorded, as a C compiler's __except does: the frames above the
 * guard's unwound (RtlUnwind: the runtime's or KERNEL32's own handlers run),
 * the FPU emptied, the stack and registers guard_run's again. The caller
 * names it, lets go of the locks and drops what it half wrote. Without it,
 * DirectDraw's own handler catches the fault and a lock the callback held is
 * never released.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

/* What of the Win32 exception records the handler reads (the documented
   i386 layouts). */
typedef struct exc_record {
    DWORD              code;
    DWORD              flags;
    struct exc_record *next;
    DWORD              address;
    DWORD              n_params;
    DWORD              info[15];
} exc_record_t;

typedef struct exc_context {
    DWORD flags;
    DWORD dr[6];
    BYTE  fpu[112];
    DWORD gs, fs, es, ds;
    DWORD edi, esi, ebx, edx, ecx, eax;
    DWORD ebp, eip, cs, eflags, esp, ss;
} exc_context_t;

DDI_CHECK(exc_context_t, eax, 0xB0);
DDI_CHECK(exc_context_t, eip, 0xB8);
DDI_CHECK(exc_context_t, esp, 0xC4);
/* What guard_run and guard_jump's assembly knows. */
DDI_CHECK(guard_fault_t, step, 4);
DDI_CHECK(guard_frame_t, fault, 8);
DDI_CHECK(guard_frame_t, ebp, 12);

#define EXC_NONCONTINUABLE   0x1
#define EXC_UNWINDING        0x2
#define EXC_EXIT_UNWIND      0x4
#define EXC_CONTINUE_SEARCH  1
#define EXC_ACCESS_VIOLATION 0xC0000005u
#define EXC_BREAKPOINT       0x80000003u
#define EXC_SINGLE_STEP      0x80000004u

static DWORD image_lo; /* the DLL's image, for the addresses a fault names */
static DWORD image_hi;
static DWORD code_lo;  /* its code (the section holding the handler), for the callers */
static DWORD code_hi;

void guard_resume(void);
void STDCALL guard_unwind(guard_frame_t *frame, void *rtl_unwind);
void guard_jump(guard_frame_t *frame) __attribute__((noreturn));

/* DWORD STDCALL guard_run(guard_fn fn, void *arg, guard_fault_t *f): fn(arg)
   inside the frame. On a fault the handler jumps to guard_resume with ESP at
   the frame (guard_jump): the frame comes off the chain, the saved
   registers come back, and it returns 0 with f->faulted set (else
   f->faulted is 0: the handler fills the rest only on a fault). */
__asm__(".text\n"
        ".globl _guard_run@12\n"
        "_guard_run@12:\n"
        "    pushl %ebp\n"
        "    movl  %esp, %ebp\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    movl  16(%ebp), %eax\n"
        "    movl  $0, (%eax)\n"      /* f->faulted = 0 */
        "    movl  $0, 4(%eax)\n"     /* f->step = 0 */
        "    pushl %ebp\n"            /* frame.ebp */
        "    pushl 16(%ebp)\n"        /* frame.fault */
        "    pushl $_guard_handler\n" /* frame.handler */
        "    pushl %fs:0\n"           /* frame.prev */
        "    movl  %esp, %fs:0\n"
        "    pushl 12(%ebp)\n"        /* the argument */
        "    call  *8(%ebp)\n"        /* stdcall: it pops its argument */
        ".globl _guard_resume\n"
        "_guard_resume:\n"
        "    movl  (%esp), %ecx\n"
        "    movl  %ecx, %fs:0\n"
        "    addl  $16, %esp\n"
        "    popl  %edi\n"
        "    popl  %esi\n"
        "    popl  %ebx\n"
        "    popl  %ebp\n"
        "    ret   $12\n");

/* void STDCALL guard_unwind(frame, RtlUnwind): RtlUnwind(frame, the return,
   NULL, 0), saving the registers it may not keep, the way compilers call
   it. Every frame above the guard's gets its unwinding call and leaves the
   chain. */
__asm__(".text\n"
        ".globl _guard_unwind@8\n"
        "_guard_unwind@8:\n"
        "    pushl %ebp\n"
        "    movl  %esp, %ebp\n"
        "    pushl %ebx\n"
        "    pushl %esi\n"
        "    pushl %edi\n"
        "    pushl %ebp\n"
        "    pushl $0\n"              /* ReturnValue */
        "    pushl $0\n"              /* ExceptionRecord */
        "    pushl $1f\n"             /* TargetIp: just below */
        "    pushl 8(%ebp)\n"         /* TargetFrame */
        "    call  *12(%ebp)\n"
        "1:\n"
        "    popl  %ebp\n"
        "    popl  %edi\n"
        "    popl  %esi\n"
        "    popl  %ebx\n"
        "    movl  %ebp, %esp\n"
        "    popl  %ebp\n"
        "    ret   $8\n");

/* void guard_jump(frame): to guard_resume with ESP at the frame and EBP
   guard_run's, the FPU's registers emptied and its exceptions cleared (the
   control word kept: the runtime's precision). */
__asm__(".text\n"
        ".globl _guard_jump\n"
        "_guard_jump:\n"
        "    movl  4(%esp), %eax\n"
        "    subl  $4, %esp\n"
        "    fnstcw (%esp)\n"
        "    fninit\n"
        "    fldcw (%esp)\n"
        "    movl  12(%eax), %ebp\n"
        "    movl  %eax, %esp\n"
        "    xorl  %eax, %eax\n"
        "    jmp   _guard_resume\n");

/* `a` follows a call instruction of the DLL's: a return address, not data
   (constants share the code's section). E8 rel32 into the code, or FF /2 of
   the length its ModRM (and SIB) give. */
static int
after_call(DWORD a)
{
    const BYTE *b = (const BYTE *) a;
    int         k;

    if ((a < code_lo + 7) || (a > code_hi))
        return 0;
    if (b[-5] == 0xE8) {
        const DWORD to = a + *(const DWORD *) (b - 4);

        if ((to >= code_lo) && (to < code_hi))
            return 1;
    }
    for (k = 2; k <= 7; k++) {
        const BYTE  m   = b[1 - k];
        const DWORD mod = m >> 6;
        const DWORD rm  = m & 7;
        int         len;

        if ((b[-k] != 0xFF) || (((m >> 3) & 7) != 2))
            continue;
        if (mod == 3)
            len = 2;
        else if (mod == 0)
            len = (rm == 5) ? 6 : (rm == 4) ? (((b[2 - k] & 7) == 5) ? 7 : 3) : 2;
        else
            len = ((mod == 1) ? 3 : 6) + (rm == 4);
        if (len == k)
            return 1;
    }
    return 0;
}

/* Called by Windows' dispatcher (cdecl). Declines unwinding, breakpoints
   and single steps (a debugger's), exceptions that are no error (by their
   severity) or are raised as noncontinuable (a C++ throw, for its own
   handler further down), and a second fault before the guard's caller has
   seen the first (one inside the recovery). A fault it takes, it does not
   return from: guard_jump. */
int
guard_handler(struct exc_record *rec, guard_frame_t *frame, struct exc_context *ctx, void *dispatch)
{
    guard_fault_t *f = frame->fault;
    const DWORD   *p;
    DWORD          n = 0;

    (void) dispatch;
    if ((rec->flags & (EXC_NONCONTINUABLE | EXC_UNWINDING | EXC_EXIT_UNWIND)) || ((rec->code >> 30) != 3) ||
        (rec->code == EXC_BREAKPOINT) || (rec->code == EXC_SINGLE_STEP) || f->faulted)
        return EXC_CONTINUE_SEARCH;
    f->faulted = 1;
    f->code    = rec->code;
    f->eip     = rec->address;
    f->write   = 0;
    f->addr    = 0;
    if ((rec->code == EXC_ACCESS_VIOLATION) && (rec->n_params >= 2)) {
        f->write = rec->info[0];
        f->addr  = rec->info[1];
    }
    /* The DLL's return addresses between the fault and the frame (a stack
       that looks sane only: under 64 KB, below the frame), guard_run's own
       left out. */
    if ((ctx->esp < (DWORD) frame) && ((DWORD) frame - ctx->esp < 0x10000)) {
        for (p = (const DWORD *) ctx->esp; (p < (const DWORD *) frame) && (n < GUARD_STACK); p++) {
            if ((*p != (DWORD) guard_resume) && after_call(*p))
                f->stack[n++] = *p;
        }
    }
    while (n < GUARD_STACK)
        f->stack[n++] = 0;
    guard_unwind(frame, (void *) RtlUnwind);
    guard_jump(frame);
}

void
guard_init(DWORD base)
{
    const BYTE *b   = (const BYTE *) base;
    const BYTE *nt  = b + *(const DWORD *) (b + 0x3C);
    const BYTE *sec = nt + 0x18 + *(const WORD *) (nt + 0x14); /* past the optional header */
    const DWORD n   = *(const WORD *) (nt + 6);
    const DWORD at  = (DWORD) guard_handler - base;
    DWORD       i;

    image_lo = base;
    image_hi = base + *(const DWORD *) (nt + 0x50); /* SizeOfImage */
    for (i = 0; i < n; i++, sec += 40) {
        const DWORD va   = *(const DWORD *) (sec + 12);
        const DWORD size = *(const DWORD *) (sec + 8);

        if ((at >= va) && (at - va < size)) {
            code_lo = base + va;
            code_hi = base + va + size;
        }
    }
}

/* --- Where the code inside a guard was --------------------------------------- */

static char *
put_str(char *p, char *end, const char *s)
{
    while (*s && (p < end))
        *p++ = *s++;
    return p;
}

static char *
put_num(char *p, char *end, DWORD v, int hex)
{
    char  t[12];
    DWORD n = 0;

    do {
        const DWORD d = hex ? (v & 15) : (v % 10);

        t[n++] = (char) ((d < 10) ? '0' + d : 'A' + d - 10);
        v      = hex ? (v >> 4) : (v / 10);
    } while (v && (n < sizeof(t)));
    while (n && (p < end))
        *p++ = t[--n];
    return p;
}

/* The step a fault interrupted, as a few words. */
static void
step_text(const guard_fault_t *f, char *buf, DWORD size)
{
    char *p   = buf;
    char *end = buf + size - 1;

    switch (f->step) {
        case GUARD_STEP_STATE:
            p = put_str(p, end, ", the state flush");
            break;
        case GUARD_STEP_TEXTURE:
            p = put_str(p, end, ", stage ");
            p = put_num(p, end, f->step_a, 0);
            p = put_str(p, end, " texture ");
            p = put_num(p, end, f->step_b, 1);
            break;
        case GUARD_STEP_STREAM:
            p = put_str(p, end, ", stream ");
            p = put_num(p, end, f->step_a, 0);
            p = put_str(p, end, " buffer ");
            p = put_num(p, end, f->step_b, 1);
            break;
        case GUARD_STEP_INDICES:
            p = put_str(p, end, ", index buffer ");
            p = put_num(p, end, f->step_b, 1);
            break;
        case GUARD_STEP_AUDIT:
            p = put_str(p, end, ", the stream audit");
            break;
        case GUARD_STEP_PACKETS:
            p = put_str(p, end, ", the packets");
            break;
        case GUARD_STEP_SETTLE:
            p = put_str(p, end, ", settling buffer ");
            p = put_num(p, end, f->step_b, 1);
            break;
        case GUARD_STEP_BLT:
            p = put_str(p, end, ", blit ");
            p = put_num(p, end, f->step_a, 1);
            p = put_str(p, end, " to ");
            p = put_num(p, end, f->step_b, 1);
            break;
        case GUARD_STEP_TARGET:
            p = put_str(p, end, ", target ");
            p = put_num(p, end, f->step_a, 1);
            break;
        default:
            break;
    }
    *p = 0;
}

/* --- Naming a fault ------------------------------------------------------------ */

/* A fault is named the first three times at each place: the faulting
   instruction, the callback, the DrawPrimitives2 command and the step (so a
   buffer settled and the draws that use it, which fault at the same
   instruction, are each named); again after 10 seconds without one there.
   GUARD_SITES of them are told apart. */
#define GUARD_SITES     32
#define GUARD_LOGGED    3
#define GUARD_QUIET_MS  10000

static struct {
    DWORD       eip;
    const char *where;
    DWORD       key; /* the command and the step */
    DWORD       logged;
    DWORD       ms;
} sites[GUARD_SITES];
static DWORD n_sites;

static int
site_tell(DWORD eip, const char *where, DWORD key)
{
    const DWORD now = GetTickCount();
    DWORD       i;

    for (i = 0; (i < n_sites) && ((sites[i].eip != eip) || (sites[i].where != where) || (sites[i].key != key)); i++)
        ;
    if (i == n_sites) {
        if (n_sites < GUARD_SITES)
            n_sites++;
        else
            i = now % GUARD_SITES; /* a full table: one replaced */
        sites[i].eip    = eip;
        sites[i].where  = where;
        sites[i].key    = key;
        sites[i].logged = 0;
    }
    if (sites[i].logged && (now - sites[i].ms >= GUARD_QUIET_MS))
        sites[i].logged = 0;
    sites[i].ms = now;
    if (sites[i].logged >= GUARD_LOGGED)
        return 0;
    sites[i].logged++;
    return 1;
}

/* An address as an offset in the DLL (+offset), else as it is. */
static char *
put_where(char *p, char *end, DWORD a)
{
    if ((a >= image_lo) && (a < image_hi)) {
        p = put_str(p, end, "+");
        return put_num(p, end, a - image_lo, 1);
    }
    return put_num(p, end, a, 1);
}

void
guard_report(const char *where, const char *cmd, DWORD op, const guard_fault_t *f)
{
    char  step[48];
    char  what[48];
    char  stack[48];
    char *p;
    char *end;
    DWORD i;

    DIAG(diag.faults++);
    if (!site_tell(f->eip, where, (op << 8) | f->step))
        return;
    step_text(f, step, sizeof(step));
    p   = what;
    end = what + sizeof(what) - 1;
    p   = put_num(p, end, f->code, 1);
    p   = put_str(p, end, " at ");
    p   = put_where(p, end, f->eip);
    if (f->code == EXC_ACCESS_VIOLATION) {
        p = put_str(p, end, f->write ? ", write " : ", read ");
        p = put_num(p, end, f->addr, 1);
    }
    *p  = 0;
    p   = stack;
    end = stack + sizeof(stack) - 1;
    for (i = 0; (i < GUARD_STACK) && f->stack[i]; i++) {
        p = put_str(p, end, " ");
        p = put_where(p, end, f->stack[i]);
    }
    *p = 0;
    /* Two lines: one holds 160 characters. At Minimal too, site_tell's
       first three a place being the rate. */
    if (dbg_event_own(DLL_TAG ": fault in %s%s%s: %s", where, cmd, step, what))
        dbg_event_own(DLL_TAG ": the fault's callers:%s", stack[0] ? stack : " none found");
}

/* --- Recovering --------------------------------------------------------------- */

void
guard_capture(void)
{
    if (diag_on)
        reg_write(VGPU98_REG_DEBUG, VGPU98_DEBUG_CAPTURE);
}

void
guard_recover(const char *where, guard_fault_t *f)
{
    guard_report(where, "", 0, f);
    /* This thread's locks first (nothing below takes one, and the wait
       would keep them from everyone else); then the batch this thread
       held: what it wrote past the last doorbell goes, the rest gets a
       fence and is waited for, the lock goes. */
    dlock_release_thread();
    gpu_fault_end();
    flight_fault(NULL);
    guard_capture();
}

/* --- The self-test ----------------------------------------------------------- */

static volatile DWORD guard_test_at; /* 0: the self-test's fault writes there */

static DWORD STDCALL
guard_test(void *unused)
{
    (void) unused;
    *(volatile DWORD *) guard_test_at = 1;
    return 1;
}

void
guard_selftest(void)
{
    static DWORD  tested; /* the process last tested */
    guard_fault_t f;
    char          what[24];
    char         *p = what;

    if (tested == GetCurrentProcessId())
        return;
    tested = GetCurrentProcessId();
    guard_run(guard_test, NULL, &f);
    if (!f.faulted) {
        dbg(DLL_TAG ": the fault guard did not catch its test fault");
        return;
    }
    p  = put_where(p, what + sizeof(what) - 1, f.eip);
    *p = 0;
    dbg(DLL_TAG ": the fault guard works (its test fault %x at %s caught)", f.code, what);
}
