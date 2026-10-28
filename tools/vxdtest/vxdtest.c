/*
 * vxdtest: runs the VGPU98 mini-VDD (vgpu98.vxd) on the host.
 *
 * Loads the LE file with its own loader into flat 32-bit memory under
 * Unicorn, answers the VMM and VDD services it calls (INT 20h dynamic links)
 * with stubs, connects the card's ports to the display engine model from
 * biostest, and calls the control procedure and every mini-VDD function the
 * way the VDD does: initialization, sequencer virtualization for a windowed
 * DOS session and for the screen's owner, saving and restoring engine state
 * across full-screen switches, the switch veto and the VGA hand-over, the
 * command ring's buffer in main memory (interface 2.1), and the pool: the
 * GPU's page table and the chunks mapped into it (interface 3.0).
 *
 * Usage: vxdtest VXD
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "util.h"
#include "vga_model.h"
#include "vid_vgpu98_card.h"

#define OBJ_BASE    0x00100000u /* object n at OBJ_BASE * n */
#define SYS_VM      0x00400000u
#define DOS_VM      0x00410000u
#define MSG_VM      0x00420000u /* the VDD's pseudo-VM IDs */
#define PLANAR_VM   0x00430000u
#define TABLE       0x00500000u /* the VDD's mini-VDD dispatch table */
#define STACK_TOP   0x000f0000u
#define STOP        0x000ff000u
#define MEM_SIZE    0x01000000u
#define CB_OFFSET   0x800u
#define VS_BYTES    0x41 /* the mini-VDD's per-VM state */
#define REGS_PHYS   0xf0000000u /* BAR0, as REGS_BASE reports it in the ring buffer tests */
#define REGS_LIN    0x00600000u /* where the _MapPhysToLinear stub maps it: plain memory */
#define BUF_PHYS    0x00a00000u /* the _PageAllocate stub's buffer */
#define BUF_LIN     0xc1230000u
#define TABLE_PHYS  0x00c00000u /* its second contiguous allocation: the GPU's page table */
#define TABLE_LIN   0x00700000u /* plain memory: the mini-VDD writes the table */
#define CHUNK_LIN   0xc8000000u /* its pool chunks (never touched by the mini-VDD) */
#define CHUNK_PHYS  0x04000000u
#define DIOC_AT     0x00680000u /* a DIOCParams and its buffers */

static uc_engine  *uc;
static vga_model_t vga;
static uint8_t    *le_file;
static size_t      le_size;
static int         checks, failures;
static uint32_t    ddb;

/* Stub state */
static uint32_t crtc_owner = SYS_VM, memc_owner = SYS_VM;
static uint32_t table_entries = 62;
static int      cb_allocs;
static int      special_ids_calls;
static uint32_t cb_bytes;
static int      no_card;
static int      seq_write_log[512]; /* engine register writes (index, not the lock), in order */
static int      n_seq_writes;
static int      map_calls;
static uint32_t map_args[3];
static int      page_allocs;
static uint32_t page_args[2][8];
static int      page_fail; /* allocations still to fail */
static uint32_t sysbuf_log[16][2]; /* writes to the SYSBUF registers and STATUS_BASE: offset, value */
static int      n_sysbuf_writes;
static int      contig_allocs;      /* contiguous allocations: the ring buffer, then the page table */
static int      chunk_allocs;       /* the pool's chunks */
static uint32_t chunk_next;         /* where the next chunk's linear pages go */
static uint32_t demand_phys;        /* _GetDemandPageInfo's DIPhys_Count */
static int      demand_calls;
static int      copy_fail;          /* _CopyPageTable calls still to fail */
static int      copy_low;           /* _CopyPageTable calls still to name a page of conventional memory */
static int      copy_adapter;       /* _CopyPageTable calls still to name a page in the adapter area */
static int      copy_absent;        /* _CopyPageTable calls still to name a page not present */
static uint32_t freed[16];          /* _PageFree's handles */
static int      n_freed;
static uint32_t last_page_args[8]; /* the last _PageAllocate call's arguments */

#define CHECK(cond, ...)                 \
    do {                                 \
        checks++;                        \
        if (!(cond))                     \
            fail(__LINE__, __VA_ARGS__); \
    } while (0)

static void
fail(int line, const char *fmt, ...)
{
    va_list ap;

    failures++;
    fprintf(stderr, "vxdtest.c:%d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static uint32_t
rd32(uint32_t a)
{
    uint32_t v = 0;

    uc_mem_read(uc, a, &v, 4);
    return v;
}

static uint8_t
rd8(uint32_t a)
{
    uint8_t v = 0;

    uc_mem_read(uc, a, &v, 1);
    return v;
}

static void
wr32(uint32_t a, uint32_t v)
{
    uc_mem_write(uc, a, &v, 4);
}

static void
wr8(uint32_t a, uint8_t v)
{
    uc_mem_write(uc, a, &v, 1);
}

static uint32_t
reg(int id)
{
    uint32_t v = 0;

    uc_reg_read(uc, id, &v);
    return v;
}

static void
set_reg(int id, uint32_t v)
{
    uc_reg_write(uc, id, &v);
}

static uint32_t
f32(size_t at)
{
    uint32_t v = 0;

    if (at + 4 <= le_size)
        memcpy(&v, le_file + at, 4);
    return v;
}

static uint32_t
f16(size_t at)
{
    return (at + 2 <= le_size) ? (uint32_t) (le_file[at] | (le_file[at + 1] << 8)) : 0;
}

/* --- The LE loader ------------------------------------------------------------------ */

static void
load_le(void)
{
    size_t   le       = f32(0x3c);
    uint32_t n_obj    = f32(le + 0x44);
    uint32_t pagesize = f32(le + 0x28);
    uint32_t datapage = f32(le + 0x80);
    size_t   fpt      = le + f32(le + 0x68);
    size_t   frt      = le + f32(le + 0x6c);
    size_t   ent      = le + f32(le + 0x5c);
    uint32_t i;

    if ((le + 0xc4 > le_size) || (le_file[le] != 'L') || (le_file[le + 1] != 'E'))
        die("not an LE file");
    for (i = 1; i <= n_obj; i++) {
        size_t   ot    = le + f32(le + 0x40) + 24 * (i - 1);
        uint32_t page  = f32(ot + 12);
        uint32_t pages = f32(ot + 16);
        uint32_t p;

        for (p = 0; p < pages; p++) {
            size_t   map = le + f32(le + 0x48) + 4 * (page - 1 + p);
            uint32_t pn  = (le_file[map] << 16) | (le_file[map + 1] << 8) | le_file[map + 2];
            size_t   src = datapage + (size_t) (pn - 1) * pagesize;
            size_t   n   = (src + pagesize <= le_size) ? pagesize : le_size - src;

            uc_mem_write(uc, OBJ_BASE * i + p * pagesize, le_file + src, n);
        }
    }
    for (i = 1; i <= n_obj; i++) {
        size_t   ot    = le + f32(le + 0x40) + 24 * (i - 1);
        uint32_t page  = f32(ot + 12);
        uint32_t pages = f32(ot + 16);
        uint32_t p;

        for (p = 0; p < pages; p++) {
            size_t at  = frt + f32(fpt + 4 * (page - 1 + p));
            size_t end = frt + f32(fpt + 4 * (page + p));

            while (at < end) {
                uint32_t src = le_file[at], flags = le_file[at + 1];
                int32_t  soff = (int16_t) f16(at + 2);
                uint32_t obj, off, value;

                at += 4;
                obj = (flags & 0x40) ? f16(at) : le_file[at];
                at += (flags & 0x40) ? 2 : 1;
                off = (flags & 0x10) ? f32(at) : f16(at);
                at += (flags & 0x10) ? 4 : 2;
                value = OBJ_BASE * obj + off;
                if (src == 8)
                    value -= OBJ_BASE * i + p * pagesize + (uint32_t) soff + 4;
                else if (src != 7)
                    die("fixup type %u", src);
                wr32(OBJ_BASE * i + p * pagesize + (uint32_t) soff, value);
            }
        }
    }
    if ((le_file[ent] != 1) || (le_file[ent + 1] != 3))
        die("unexpected entry table");
    ddb = OBJ_BASE * f16(ent + 2) + f32(ent + 5);
}

/* --- Stubs and ports ------------------------------------------------------------------- */

static void
hook_intr(uc_engine *u, uint32_t intno, void *user)
{
    uint32_t eip = reg(UC_X86_REG_EIP);
    uint32_t svc;

    (void) u;
    (void) user;
    if (intno != 0x20) {
        fail(__LINE__, "unexpected INT %02Xh at %08X", intno, eip);
        return;
    }
    svc = rd32(eip);
    set_reg(UC_X86_REG_EIP, eip + 4);
    switch (svc) {
        case 0x000100a7: /* _Allocate_Device_CB_Area(bytes, flags) */
            cb_allocs++;
            cb_bytes = rd32(reg(UC_X86_REG_ESP));
            set_reg(UC_X86_REG_EAX, CB_OFFSET);
            break;
        case 0x000a000e: /* VDD_Get_Mini_Dispatch_Table */
            set_reg(UC_X86_REG_EDI, TABLE);
            set_reg(UC_X86_REG_ECX, table_entries);
            break;
        case 0x000a0010: /* VDD_Get_VM_Info */
            set_reg(UC_X86_REG_ESI, memc_owner);
            set_reg(UC_X86_REG_EDI, crtc_owner);
            break;
        case 0x000a0011: /* VDD_Get_Special_VM_IDs */
            special_ids_calls++;
            set_reg(UC_X86_REG_EDI, MSG_VM);
            set_reg(UC_X86_REG_ESI, PLANAR_VM);
            break;
        case 0x0001006c: { /* _MapPhysToLinear(phys, bytes, flags) */
            int i;

            map_calls++;
            for (i = 0; i < 3; i++)
                map_args[i] = rd32(reg(UC_X86_REG_ESP) + 4u * (uint32_t) i);
            set_reg(UC_X86_REG_EAX, (map_args[0] == REGS_PHYS) ? REGS_LIN : 0xffffffffu);
            set_reg(UC_X86_REG_ECX, 0xdeadbeefu); /* the C convention leaves ECX and EDX to the callee */
            set_reg(UC_X86_REG_EDX, 0xdeadbeefu);
            break;
        }
        case 0x00010053: { /* _PageAllocate(pages, type, VM, align mask, min page, max page, &phys, flags) */
            uint32_t *a = page_args[(page_allocs < 2) ? page_allocs : 1];
            int       i;

            for (i = 0; i < 8; i++)
                a[i] = last_page_args[i] = rd32(reg(UC_X86_REG_ESP) + 4u * (uint32_t) i);
            page_allocs++;
            set_reg(UC_X86_REG_ECX, 0xdeadbeefu);
            if (page_fail > 0) {
                page_fail--;
                set_reg(UC_X86_REG_EAX, 0);
                set_reg(UC_X86_REG_EDX, 0);
                break;
            }
            if (!(a[7] & 0x04)) {
                /* A pool chunk: linear pages anywhere, no physical address. */
                chunk_allocs++;
                set_reg(UC_X86_REG_EAX, 0x12360000u + (uint32_t) chunk_allocs);
                set_reg(UC_X86_REG_EDX, CHUNK_LIN + chunk_next);
                chunk_next += a[0] << 12;
                break;
            }
            /* Contiguous: the ring buffer, then the GPU's page table. */
            if (contig_allocs++ == 0) {
                wr32(a[6], BUF_PHYS);
                set_reg(UC_X86_REG_EAX, 0x12340000u);
                set_reg(UC_X86_REG_EDX, BUF_LIN);
            } else {
                wr32(a[6], TABLE_PHYS);
                set_reg(UC_X86_REG_EAX, 0x12350000u);
                set_reg(UC_X86_REG_EDX, TABLE_LIN);
            }
            break;
        }
        case 0x00010055: /* _PageFree(handle, flags) */
            if (n_freed < 16)
                freed[n_freed++] = rd32(reg(UC_X86_REG_ESP));
            set_reg(UC_X86_REG_EAX, 1);
            set_reg(UC_X86_REG_ECX, 0xdeadbeefu);
            set_reg(UC_X86_REG_EDX, 0xdeadbeefu);
            break;
        case 0x00010061: { /* _CopyPageTable(first linear page, pages, &entries, flags) */
            const uint32_t sp = reg(UC_X86_REG_ESP);
            const uint32_t pg = rd32(sp), n = rd32(sp + 4), buf = rd32(sp + 8);
            uint32_t       i;

            set_reg(UC_X86_REG_ECX, 0xdeadbeefu);
            set_reg(UC_X86_REG_EDX, 0xdeadbeefu);
            if (copy_fail > 0) {
                copy_fail--;
                set_reg(UC_X86_REG_EAX, 0);
                break;
            }
            /* Each linear page's physical page, not in order, with the
               processor's bits (present, writable, user, accessed, dirty). */
            for (i = 0; i < n; i++)
                wr32(buf + 4 * i, (CHUNK_PHYS + (((pg + i - (CHUNK_LIN >> 12)) ^ 1u) << 12)) | 0x067u);
            if (copy_low > 0) {
                copy_low--;
                wr32(buf, 0x0009f000u | 0x067u);
            }
            if (copy_adapter > 0) {
                copy_adapter--;
                wr32(buf, 0x000b8000u | 0x067u);
            }
            if (copy_absent > 0) {
                copy_absent--;
                wr32(buf, rd32(buf) & ~0x001u);
            }
            set_reg(UC_X86_REG_EAX, 1);
            break;
        }
        case 0x0001006f: /* _GetDemandPageInfo(&DemandInfoStruc, flags) */
            demand_calls++;
            wr32(rd32(reg(UC_X86_REG_ESP)) + 4, demand_phys);
            set_reg(UC_X86_REG_ECX, 0xdeadbeefu);
            set_reg(UC_X86_REG_EDX, 0xdeadbeefu);
            break;
        default:
            fail(__LINE__, "unexpected VxD service %08X", svc);
            break;
    }
}

static uint32_t
hook_in(uc_engine *u, uint32_t port, int size, void *user)
{
    (void) u;
    (void) user;
    if (no_card && (port == 0x3c5))
        return 0xff;
    if (size == 1)
        return vga_in(&vga, (uint16_t) port);
    return 0xffffffffu;
}

static void
hook_out(uc_engine *u, uint32_t port, int size, uint32_t value, void *user)
{
    int i;

    (void) u;
    (void) user;
    for (i = 0; i < size; i++) {
        uint16_t p = (uint16_t) (port + i);

        if ((p == 0x3c5) && (vga.seq_index > 0x80) && (vga.seq_index <= 0xbf) && vga.disp_unlocked &&
            (n_seq_writes < 512))
            seq_write_log[n_seq_writes++] = vga.seq_index;
        vga_out(&vga, p, (uint8_t) (value >> (8 * i)));
    }
}

static void
hook_regs_write(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user)
{
    const uint32_t off = (uint32_t) (addr - REGS_LIN);

    (void) u;
    (void) type;
    (void) user;
    if ((off >= VGPU98_REG_SYSBUF_PHYS) && (off <= VGPU98_REG_STATUS_BASE) && (n_sysbuf_writes < 16)) {
        sysbuf_log[n_sysbuf_writes][0] = off;
        sysbuf_log[n_sysbuf_writes][1] = (size == 4) ? (uint32_t) value : 0xbadu;
        n_sysbuf_writes++;
    }
}

static void
machine(void)
{
    uc_hook h1, h2, h3, h4;

    if (uc)
        uc_close(uc);
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc) != UC_ERR_OK)
        die("cannot start Unicorn");
    if (uc_mem_map(uc, 0, MEM_SIZE, UC_PROT_ALL) != UC_ERR_OK)
        die("cannot map memory");
    uc_hook_add(uc, &h1, UC_HOOK_INTR, (void *) hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &h2, UC_HOOK_INSN, (void *) hook_in, NULL, 1, 0, UC_X86_INS_IN);
    uc_hook_add(uc, &h3, UC_HOOK_INSN, (void *) hook_out, NULL, 1, 0, UC_X86_INS_OUT);
    uc_hook_add(uc, &h4, UC_HOOK_MEM_WRITE, (void *) hook_regs_write, NULL, REGS_LIN, REGS_LIN + 0xffff);
    load_le();
    vga_reset(&vga, 16u << 20);
    vga.lfb_base  = 0xe0000000u;
    vga.debug_on  = 1;
    crtc_owner    = SYS_VM;
    memc_owner    = SYS_VM;
    table_entries = 62;
    cb_allocs         = 0;
    special_ids_calls = 0;
    no_card           = 0;
    map_calls         = 0;
    page_allocs       = 0;
    page_fail         = 0;
    n_sysbuf_writes   = 0;
    contig_allocs     = 0;
    chunk_allocs      = 0;
    chunk_next        = 0;
    demand_phys       = 0x8000; /* 128 MB */
    demand_calls      = 0;
    copy_fail         = 0;
    copy_low          = 0;
    copy_adapter      = 0;
    copy_absent       = 0;
    n_freed           = 0;
}

/* Registers on entry and what a call returns. */
typedef struct cpu_t {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp;
    int      cf;
} cpu_t;

static cpu_t
call(uint32_t fn, cpu_t in, const char *what)
{
    cpu_t    out;
    uint32_t esp = STACK_TOP - 4;
    uc_err   err;

    wr32(esp, STOP);
    set_reg(UC_X86_REG_ESP, esp);
    set_reg(UC_X86_REG_EAX, in.eax);
    set_reg(UC_X86_REG_EBX, in.ebx);
    set_reg(UC_X86_REG_ECX, in.ecx);
    set_reg(UC_X86_REG_EDX, in.edx);
    set_reg(UC_X86_REG_ESI, in.esi);
    set_reg(UC_X86_REG_EDI, in.edi);
    set_reg(UC_X86_REG_EBP, in.ebp);
    set_reg(UC_X86_REG_EFLAGS, 0x202 | (in.cf ? 1 : 0));
    err = uc_emu_start(uc, fn, STOP, 0, 1000000);
    if (err != UC_ERR_OK)
        fail(__LINE__, "%s: emulation error %s at %08X", what, uc_strerror(err), reg(UC_X86_REG_EIP));
    CHECK(reg(UC_X86_REG_ESP) == STACK_TOP, "%s: ESP %08X after return", what, reg(UC_X86_REG_ESP));
    out.eax = reg(UC_X86_REG_EAX);
    out.ebx = reg(UC_X86_REG_EBX);
    out.ecx = reg(UC_X86_REG_ECX);
    out.edx = reg(UC_X86_REG_EDX);
    out.esi = reg(UC_X86_REG_ESI);
    out.edi = reg(UC_X86_REG_EDI);
    out.ebp = reg(UC_X86_REG_EBP);
    out.cf  = (int) (reg(UC_X86_REG_EFLAGS) & 1);
    return out;
}

static cpu_t
regs(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    cpu_t r;

    r.eax = eax;
    r.ebx = ebx;
    r.ecx = ecx;
    r.edx = edx;
    r.esi = 0x51515151u;
    r.edi = 0xd1d1d1d1u;
    r.ebp = 0xb0b0b0b0u;
    r.cf  = 0;
    return r;
}

static uint32_t
fn(int index)
{
    return rd32(TABLE + 4u * (uint32_t) index);
}

/* The card as the display driver leaves it: the desktop at 1024x768x8, locked. */
static void
desktop_mode(void)
{
    static const uint8_t regs_set[][2] = {
        { 0x91, 8 },  { 0x92, 0x00 }, { 0x93, 0x04 }, { 0x94, 0x00 }, { 0x95, 0x03 }, { 0x96, 0x00 },
        { 0x97, 0x04 }, { 0x9c, 75 }, { 0xa2, 0x05 }, { 0xa3, 0xc1 }, { 0x90, 1 },
    };
    size_t i;

    vga_out(&vga, 0x3c4, 0x80);
    vga_out(&vga, 0x3c5, 0x98);
    for (i = 0; i < sizeof(regs_set) / sizeof(regs_set[0]); i++) {
        vga_out(&vga, 0x3c4, regs_set[i][0]);
        vga_out(&vga, 0x3c5, regs_set[i][1]);
    }
    vga_out(&vga, 0x3c4, 0x80);
    vga_out(&vga, 0x3c5, 0x00);
    vga_out(&vga, 0x3c4, 0x02);
}

static uint32_t
disp(uint8_t sr, int bytes)
{
    return vga_disp_value(&vga, sr, bytes);
}

/* A sequencer write or read by a VM, as the VDD hands it over. */
static cpu_t
seq_out(uint32_t vm, uint8_t index, uint8_t value)
{
    cpu_t in = regs(0xabcdef00u | value, vm, 0xccc00000u | index, 0x3c5);
    cpu_t r  = call(fn(19), in, "VIRTUALIZE_SEQUENCER_OUT");

    CHECK((r.eax == in.eax) && (r.ebx == in.ebx) && (r.ecx == in.ecx) && (r.edx == in.edx) && (r.esi == in.esi) &&
              (r.edi == in.edi) && (r.ebp == in.ebp),
          "VIRTUALIZE_SEQUENCER_OUT %02Xh: registers changed", index);
    return r;
}

static cpu_t
seq_in(uint32_t vm, uint8_t index)
{
    cpu_t in = regs(0xabcdef00u, vm, 0xccc00000u | index, 0x3c5);
    cpu_t r  = call(fn(18), in, "VIRTUALIZE_SEQUENCER_IN");

    CHECK(((r.eax & ~0xffu) == (in.eax & ~0xffu)) && (r.ebx == in.ebx) && (r.ecx == in.ecx) && (r.edx == in.edx) &&
              (r.esi == in.esi) && (r.edi == in.edi) && (r.ebp == in.ebp),
          "VIRTUALIZE_SEQUENCER_IN %02Xh: registers changed", index);
    return r;
}

/* --- Tests ------------------------------------------------------------------------------- */

static void
test_init(void)
{
    static const int ours[] = { 0, 4, 5, 8, 9, 18, 19, 26, 35, 36, 43, 45 };
    uint32_t         control;
    cpu_t            r;
    int              i, j, bad = 0;

    machine();
    CHECK((rd32(ddb + 4) & 0xffff) == 0x400, "DDB SDK version");
    CHECK(!memcmp(le_file, "MZ", 2), "MZ header");
    {
        char name[9];

        uc_mem_read(uc, ddb + 12, name, 8);
        name[8] = 0;
        CHECK(!strcmp(name, "VGPU98  "), "DDB name \"%s\"", name);
    }
    CHECK(rd32(ddb + 20) == 0x28000000u, "DDB init order %08X", rd32(ddb + 20));
    CHECK(rd32(ddb + 64) == 0x50, "DDB size");
    CHECK((rd32(ddb + 60) == 0x50726576u) && (rd32(ddb + 68) == 0x52737631u) && (rd32(ddb + 76) == 0x52737633u),
          "DDB reserved fields not as the VMM's headers define them");
    control = rd32(ddb + 24);
    CHECK((control >= OBJ_BASE) && (control < 2 * OBJ_BASE), "DDB control procedure %08X", control);

    r = call(control, regs(2, SYS_VM, 0, 0), "Init_Complete");
    CHECK(!r.cf && (cb_allocs == 0), "an unhandled message did something");
    r = call(control, regs(0x1b, SYS_VM, 0, 0), "Sys_Dynamic_Device_Init");
    CHECK(!r.cf, "Sys_Dynamic_Device_Init failed");
    CHECK((cb_allocs == 1) && (cb_bytes >= VS_BYTES), "control block area: %d allocations of %u bytes", cb_allocs,
          cb_bytes);
    for (i = 0; i < 62; i++) {
        int registered = 0;

        for (j = 0; j < (int) (sizeof(ours) / sizeof(ours[0])); j++)
            registered |= ours[j] == i;
        if (registered != ((fn(i) >= OBJ_BASE) && (fn(i) < 2 * OBJ_BASE)))
            bad++;
    }
    CHECK(bad == 0, "dispatch table: %d entries wrong", bad);
    r = call(control, regs(1, SYS_VM, 0, 0), "Device_Init after the dynamic one");
    CHECK(!r.cf && (cb_allocs == 1), "initialized twice");
    CHECK(!vga.disp_unlocked && (vga.disp_locked_writes == 0), "initialization left the engine unlocked");
    CHECK((strstr(vga.debug, "VGPU98 VxD ") != NULL) && (strstr(vga.debug, ": loaded") != NULL), "no debug line at load");

    /* No card, and a VDD with too short a table: refused. */
    machine();
    no_card = 1;
    r       = call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), "Sys_Dynamic_Device_Init (no card)");
    CHECK(r.cf && (fn(19) == 0), "loaded without a card");
    machine();
    table_entries = 45;
    r             = call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), "Sys_Dynamic_Device_Init (old VDD)");
    CHECK(r.cf && (fn(19) == 0), "loaded with a VDD table of 45 entries");
}

/* Starts the mini-VDD with the GPU's registers mapped: PROFILE as given, the
   SYSBUF registers holding an earlier boot's buffer, and the first `fails`
   allocations failing. */
static void
ring_buffer_boot(uint32_t profile, int fails, const char *what)
{
    cpu_t r;

    machine();
    vga.regs_base = REGS_PHYS;
    wr32(REGS_LIN + VGPU98_REG_PROFILE, profile);
    wr32(REGS_LIN + VGPU98_REG_SYSBUF_PHYS, 0x11111000u);
    wr32(REGS_LIN + VGPU98_REG_SYSBUF_LINEAR, 0xc2222000u);
    wr32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE, 0x00200000u);
    wr32(REGS_LIN + VGPU98_REG_SYSBUF_STATUS, 0x11311000u);
    wr32(REGS_LIN + VGPU98_REG_STATUS_BASE, 0x11311000u);
    n_sysbuf_writes = 0;
    page_fail       = fails;
    r               = call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), what);
    CHECK(!r.cf && (cb_allocs == 1), "%s: initialization failed", what);
    CHECK((map_calls == 1) && (map_args[0] == REGS_PHYS) && (map_args[1] == 0x10000), "%s: registers not mapped", what);
    /* The earlier boot's buffer is forgotten first, the size before the addresses, and the GPU
       stops writing its status there. */
    CHECK((n_sysbuf_writes >= 5) && (sysbuf_log[0][0] == VGPU98_REG_SYSBUF_SIZE) && !sysbuf_log[0][1] && !sysbuf_log[1][1] &&
              !sysbuf_log[2][1] && !sysbuf_log[3][1] && (sysbuf_log[4][0] == VGPU98_REG_STATUS_BASE) && !sysbuf_log[4][1],
          "%s: the SYSBUF registers and STATUS_BASE were not cleared first", what);
}

static int
page_args_are(int i, uint32_t pages)
{
    const uint32_t *a = page_args[i];

    /* 1 MB and up, below 4 GB; PAGECONTIG | PAGEFIXED | PAGELOCKED | PAGEUSEALIGN | PAGEZEROINIT. */
    return (a[0] == pages) && (a[1] == 1) && !a[2] && !a[3] && (a[4] == 0x100) && (a[5] == 0xfffff) &&
           (a[6] >= OBJ_BASE) && (a[6] < 2 * OBJ_BASE) && (a[7] == 0x8f);
}

static void
test_ring_buffer(void)
{
    cpu_t r;

    ring_buffer_boot(0x0f, 0, "Sys_Dynamic_Device_Init (ring buffer)");
    CHECK((page_allocs == 1) && page_args_are(0, 513), "_PageAllocate: %d calls, or not 513 contiguous fixed pages",
          page_allocs);
    CHECK((rd32(REGS_LIN + VGPU98_REG_SYSBUF_PHYS) == BUF_PHYS) && (rd32(REGS_LIN + VGPU98_REG_SYSBUF_LINEAR) == BUF_LIN) &&
              (rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE) == 0x200000u),
          "SYSBUF registers %08X %08X %08X", rd32(REGS_LIN + VGPU98_REG_SYSBUF_PHYS),
          rd32(REGS_LIN + VGPU98_REG_SYSBUF_LINEAR), rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE));
    CHECK(rd32(REGS_LIN + VGPU98_REG_SYSBUF_STATUS) == BUF_PHYS + 0x200000u, "SYSBUF_STATUS %08X, not the page after the ring",
          rd32(REGS_LIN + VGPU98_REG_SYSBUF_STATUS));
    CHECK((n_sysbuf_writes == 9) && (sysbuf_log[8][0] == VGPU98_REG_SYSBUF_SIZE), "the size was not written last");
    CHECK(strstr(vga.debug, "ring buffer ready: 2048 KB in main memory") != NULL, "no debug line for the buffer");

    r = call(rd32(ddb + 24), regs(5, SYS_VM, 0, 0), "System_Exit");
    CHECK(!r.cf && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_PHYS) && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_LINEAR) &&
              !rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE) && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_STATUS) &&
              !rd32(REGS_LIN + VGPU98_REG_STATUS_BASE),
          "System_Exit left the buffer named or the status block on");

    ring_buffer_boot(0x0f, 1, "Sys_Dynamic_Device_Init (2 MB not available)");
    CHECK((page_allocs == 2) && page_args_are(1, 257) && (rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE) == 0x100000u) &&
              (rd32(REGS_LIN + VGPU98_REG_SYSBUF_STATUS) == BUF_PHYS + 0x100000u),
          "no 1 MB buffer when 2 MB could not be had");
    CHECK(strstr(vga.debug, "ring buffer ready: 1024 KB in main memory") != NULL, "no debug line for the 1 MB buffer");

    ring_buffer_boot(0x0f, 2, "Sys_Dynamic_Device_Init (no memory)");
    CHECK((page_allocs == 2) && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_PHYS) && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_LINEAR) &&
              !rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE),
          "a buffer named without memory");
    CHECK(strstr(vga.debug, "no ring buffer") != NULL, "no debug line without a buffer");

    ring_buffer_boot(0x07, 0, "Sys_Dynamic_Device_Init (GPU without SYSRING)");
    CHECK((page_allocs == 0) && !rd32(REGS_LIN + VGPU98_REG_SYSBUF_SIZE), "a buffer for a GPU that cannot read it");
}

/* The card after Windows has started: the VDD has its message mode state, the display driver runs the desktop
   and has registered (VDD_REGISTER_DISPLAY_DRIVER_INFO). */
static void
init_card(void)
{
    machine();
    call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), "Sys_Dynamic_Device_Init");
    call(fn(45), regs(0, SYS_VM, 0, 0), "SAVE_MESSAGE_MODE_STATE");
    desktop_mode();
    call(fn(0), regs(0x83, SYS_VM, 16u << 20, 0), "REGISTER_DISPLAY_DRIVER");
}

/* A DOS session in a window: its own lock and mode registers; the desktop stays. */
static void
test_windowed(void)
{
    cpu_t r;

    init_card();
    crtc_owner = SYS_VM;
    memc_owner = DOS_VM;
    vga.seq_index = 0x02;

    r = seq_in(DOS_VM, 0x80);
    CHECK(r.cf && ((r.eax & 0xff) == 0), "windowed: lock not locked at first");
    r = seq_in(DOS_VM, 0x81);
    CHECK(r.cf && ((r.eax & 0xff) == 0), "windowed: identity readable while locked");
    seq_out(DOS_VM, 0x80, 0x98);
    CHECK(!vga.disp_unlocked && (rd8(DOS_VM + CB_OFFSET) == 1), "windowed: unlock reached the hardware");
    r = seq_in(DOS_VM, 0x80);
    CHECK((r.eax & 0xff) == 1, "windowed: KEY reads %02X", r.eax & 0xff);
    r = seq_in(DOS_VM, 0x81);
    CHECK(r.cf && ((r.eax & 0xff) == 0x98), "windowed: ID reads %02X", r.eax & 0xff);
    r = seq_in(DOS_VM, 0x8a);
    CHECK((r.eax & 0xff) == 0x00, "windowed: VRAM size byte 2 %02X", r.eax & 0xff); /* 16 MB = 01000000h */
    r = seq_in(DOS_VM, 0x8b);
    CHECK((r.eax & 0xff) == 0x01, "windowed: VRAM size byte 3 %02X", r.eax & 0xff);
    r = seq_in(DOS_VM, 0x8f);
    CHECK((r.eax & 0xff) == 0xe0, "windowed: framebuffer address byte 3 %02X", r.eax & 0xff);
    CHECK(!vga.disp_unlocked && (vga.seq_index == 0x02), "windowed reads: hardware lock or index disturbed");

    /* The BIOS's mode set in a window turns the VM's engine off, not the desktop's. */
    seq_out(DOS_VM, 0x90, 0x00);
    CHECK(vga_disp_enabled(&vga) && (disp(0x92, 2) == 1024), "windowed: the desktop's engine was turned off");
    seq_out(DOS_VM, 0x91, 16);
    r = seq_in(DOS_VM, 0x91);
    CHECK(((r.eax & 0xff) == 16) && (disp(0x91, 1) == 8), "windowed: FORMAT not the VM's own");
    seq_out(DOS_VM, 0x91, 24);
    r = seq_in(DOS_VM, 0x91);
    CHECK((r.eax & 0xff) == 16, "windowed: FORMAT 24 accepted");
    seq_out(DOS_VM, 0xa2, 0x17);
    seq_out(DOS_VM, 0xa3, 0x41);
    r = seq_in(DOS_VM, 0xa3);
    CHECK((r.eax & 0xff) == 0x41, "windowed: SCRATCH");
    seq_out(DOS_VM, 0x90, 0x03);
    r = seq_in(DOS_VM, 0x90);
    CHECK((r.eax & 0xff) == 0x01, "windowed: CONTROL keeps reserved bits");

    /* EDID through the hardware port. */
    seq_out(DOS_VM, 0xa0, 8);
    r = seq_in(DOS_VM, 0xa1);
    CHECK((r.eax & 0xff) == vga.edid[8], "windowed: EDID byte 8");
    r = seq_in(DOS_VM, 0xa0);
    CHECK((r.eax & 0xff) == 9, "windowed: EDID index %02X", r.eax & 0xff);
    seq_out(DOS_VM, 0xa0, 0);
    r = seq_in(DOS_VM, 0xa1);
    CHECK((r.eax & 0xff) == 0x00, "windowed: EDID byte 0");
    r = seq_in(DOS_VM, 0xa1);
    CHECK((r.eax & 0xff) == 0xff, "windowed: EDID byte 1");
    CHECK(!vga.disp_unlocked && (vga.seq_index == 0x02), "windowed EDID: hardware lock or index disturbed");

    /* Locked again: reads 00h, writes ignored. */
    seq_out(DOS_VM, 0x80, 0x00);
    r = seq_in(DOS_VM, 0x91);
    CHECK((r.eax & 0xff) == 0, "windowed: FORMAT readable while locked");
    seq_out(DOS_VM, 0x91, 32);
    seq_out(DOS_VM, 0x80, 0x98);
    r = seq_in(DOS_VM, 0x91);
    CHECK((r.eax & 0xff) == 16, "windowed: a write while locked took effect");

    /* Other registers are the VDD's; the debug register is shared. */
    r = seq_in(DOS_VM, 0x02);
    CHECK(!r.cf, "SR02 handled by the mini-VDD");
    r = seq_out(DOS_VM, 0x04, 0x06);
    CHECK(!r.cf, "SR04 handled by the mini-VDD");
    vga.debug_len = 0;
    memset(vga.debug, 0, sizeof(vga.debug));
    r = seq_out(DOS_VM, 0xe0, 'x');
    CHECK(r.cf && (vga.debug[0] == 'x'), "debug register write from a window lost");
    r = seq_in(DOS_VM, 0xe0);
    CHECK(r.cf && ((r.eax & 0xff) == 1), "debug register read from a window");

    /* CHECK_HIRES_MODE: the window's own engine. */
    seq_out(DOS_VM, 0x90, 0x01);
    r = call(fn(35), regs(0, DOS_VM, 0, 0), "CHECK_HIRES_MODE");
    CHECK(r.cf, "CHECK_HIRES_MODE: a window with the engine on is not hi-res");
    seq_out(DOS_VM, 0x90, 0x00);
    r = call(fn(35), regs(0, DOS_VM, 0, 0), "CHECK_HIRES_MODE");
    CHECK(!r.cf, "CHECK_HIRES_MODE: a window with the engine off is hi-res");
    r = call(fn(35), regs(0, SYS_VM, 0, 0), "CHECK_HIRES_MODE");
    CHECK(!r.cf, "CHECK_HIRES_MODE: the system VM is reported");
    CHECK(vga_disp_enabled(&vga) && (disp(0x91, 1) == 8) && !vga.disp_unlocked,
          "windowed session changed the desktop's engine");

    /* The VDD saving a windowed VM's state: its copy is its state, not the hardware's. */
    call(fn(8), regs(0, DOS_VM, 0, 0), "SAVE_REGISTERS (windowed)");
    CHECK(rd8(DOS_VM + CB_OFFSET + 1 + 0x11) == 16, "SAVE_REGISTERS copied the desktop into a windowed VM");
}

/* CONTROL was the last engine register written since n_seq_writes was cleared. */
static int
control_last(void)
{
    int i;

    for (i = 0; i < n_seq_writes - 1; i++) {
        if (seq_write_log[i] == 0x90)
            return 0;
    }
    return (n_seq_writes > 0) && (seq_write_log[n_seq_writes - 1] == 0x90);
}

/* A full-screen DOS session: direct access, its state saved and restored. */
static void
test_full_screen(void)
{
    cpu_t r;

    init_card();

    /* The VDD saves the desktop, hands the screen over. */
    r = call(fn(8), regs(0, SYS_VM, 0, 0x3c5), "SAVE_REGISTERS (desktop)");
    CHECK((r.ebx == SYS_VM) && (r.edx == 0x3c5) && (r.esi == 0x51515151u) && (r.edi == 0xd1d1d1d1u),
          "SAVE_REGISTERS: registers not preserved");
    CHECK((rd8(SYS_VM + CB_OFFSET + 1 + 0x10) == 1) && (rd8(SYS_VM + CB_OFFSET + 1 + 0x12) == 0x00) &&
              (rd8(SYS_VM + CB_OFFSET + 1 + 0x13) == 0x04),
          "SAVE_REGISTERS: the desktop's state not saved");
    call(fn(4), regs(0, DOS_VM, 0, 0), "PRE_HIRES_TO_VGA");
    CHECK(!vga_disp_enabled(&vga) && !vga.disp_unlocked && (disp(0x92, 2) == 1024),
          "PRE_HIRES_TO_VGA: engine still on, lock or other registers changed");
    crtc_owner = DOS_VM;
    memc_owner = DOS_VM;
    r = call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (new session)");
    CHECK(!vga_disp_enabled(&vga) && !vga.disp_unlocked, "a new session's engine is not off and locked");

    /* The session owns the screen: straight to the hardware. */
    seq_out(DOS_VM, 0x80, 0x98);
    CHECK(vga.disp_unlocked, "full screen: unlock did not reach the hardware");
    seq_out(DOS_VM, 0x91, 16);
    seq_out(DOS_VM, 0x92, 0x80);
    seq_out(DOS_VM, 0x93, 0x02);
    seq_out(DOS_VM, 0x90, 0x01);
    CHECK(vga_disp_enabled(&vga) && (disp(0x91, 1) == 16) && (disp(0x92, 2) == 640),
          "full screen: the session's mode did not reach the hardware");
    r = seq_in(DOS_VM, 0x91);
    CHECK((r.eax & 0xff) == 16, "full screen: FORMAT read");

    /* With the session's engine on, switching away is refused; with it off, allowed. The VDD passes the system
       VM in EBX: the session is the screen's owner. */
    r = call(fn(43), regs(0xffffffffu, SYS_VM, 0x13, 0), "CHECK_SCREEN_SWITCH_OK");
    CHECK(r.cf && (r.ebx == SYS_VM) && (r.edi == 0xd1d1d1d1u), "switch away from a VBE session allowed");
    seq_out(DOS_VM, 0x90, 0x00);
    r = call(fn(43), regs(0xffffffffu, SYS_VM, 0x03, 0), "CHECK_SCREEN_SWITCH_OK");
    CHECK(!r.cf && (r.eax == 0), "switch away from a VGA session refused");

    /* Away and back: each side gets its own state again, CONTROL last. */
    seq_out(DOS_VM, 0x98, 0x00);
    seq_out(DOS_VM, 0x99, 0x10);
    seq_out(DOS_VM, 0x9a, 0x00);
    seq_out(DOS_VM, 0x9b, 0x00);
    seq_out(DOS_VM, 0x90, 0x01);
    call(fn(8), regs(0, DOS_VM, 0, 0), "SAVE_REGISTERS (session)");
    crtc_owner   = SYS_VM;
    memc_owner   = SYS_VM;
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop)");
    CHECK(vga_disp_enabled(&vga) && (disp(0x91, 1) == 8) && (disp(0x92, 2) == 1024) && (disp(0x98, 4) == 0) &&
              !vga.disp_unlocked && control_last(),
          "RESTORE_REGISTERS: the desktop's saved state not back, or CONTROL not last");
    r = call(fn(43), regs(0, SYS_VM, 0x03, 0), "CHECK_SCREEN_SWITCH_OK");
    CHECK(!r.cf, "switch away from the desktop refused");

    /* A windowed session taking video memory: the hardware already holds the desktop's state. */
    memc_owner   = DOS_VM;
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop, memory to a window)");
    CHECK(n_seq_writes == 0, "RESTORE_REGISTERS rewrote the state the hardware holds");
    memc_owner = SYS_VM;

    crtc_owner   = DOS_VM;
    n_seq_writes = 0;
    r = call(fn(9), regs(0, 0, DOS_VM, 0x3c5), "RESTORE_REGISTERS (session)");
    CHECK((r.ebx == 0) && (r.edx == 0x3c5), "RESTORE_REGISTERS: EBX or EDX changed");
    CHECK(vga_disp_enabled(&vga) && (disp(0x91, 1) == 16) && (disp(0x92, 2) == 640) && (disp(0x98, 4) == 0x1000) &&
              vga.disp_unlocked && control_last(),
          "RESTORE_REGISTERS: the session's state not back (format %u, width %u, start %X, unlocked %d)",
          disp(0x91, 1), disp(0x92, 2), disp(0x98, 4), vga.disp_unlocked);

    /* The desktop changed mode since it was saved (the driver registered again): the driver sets it itself. */
    call(fn(0), regs(0x83, SYS_VM, 16u << 20, 0), "REGISTER_DISPLAY_DRIVER (a new mode)");
    crtc_owner   = SYS_VM;
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop, state out of date)");
    CHECK(n_seq_writes == 0, "RESTORE_REGISTERS restored a desktop state from before a mode change");

    /* Once the display driver is gone, the desktop gets the engine off. */
    crtc_owner = DOS_VM;
    call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (session)");
    seq_out(DOS_VM, 0x90, 0x01);
    call(fn(8), regs(0, SYS_VM, 0, 0), "SAVE_REGISTERS (not the hardware's VM)");
    CHECK(rd8(SYS_VM + CB_OFFSET + 1 + 0x11) == 8, "SAVE_REGISTERS saved the hardware into another VM's state");
    call(fn(26), regs(0, SYS_VM, 0, 0), "DISPLAY_DRIVER_DISABLING");
    crtc_owner = SYS_VM;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (no display driver)");
    CHECK(!vga_disp_enabled(&vga), "RESTORE_REGISTERS without a display driver left the engine on");

    r = call(fn(36), regs(0x1234, 0, 0, 0), "GET_TOTAL_VRAM_SIZE");
    CHECK(r.cf && (r.ecx == (16u << 20)) && (r.eax == 0x1234), "GET_TOTAL_VRAM_SIZE: %X", r.ecx);
}

/* The VDD's pseudo-VMs: message mode (blue screens) and forced planar (font loading). */
static void
test_message_mode(void)
{
    cpu_t   r;
    uint8_t cb[VS_BYTES];
    int     i, nonzero = 0;

    /* Before SAVE_MESSAGE_MODE_STATE, the VDD's virtual mode sets in the system VM stay virtual. */
    machine();
    call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), "Sys_Dynamic_Device_Init");
    CHECK(special_ids_calls == 1, "VDD_Get_Special_VM_IDs not called at initialization");
    desktop_mode();
    call(fn(0), regs(0x83, SYS_VM, 16u << 20, 0), "REGISTER_DISPLAY_DRIVER");
    crtc_owner = SYS_VM;
    seq_out(SYS_VM, 0x80, 0x98);
    seq_out(SYS_VM, 0x90, 0x00);
    CHECK(vga_disp_enabled(&vga) && !vga.disp_unlocked,
          "before SAVE_MESSAGE_MODE_STATE: the system VM's BIOS reached the engine");
    r = seq_in(SYS_VM, 0x80);
    CHECK((r.eax & 0xff) == 1, "before SAVE_MESSAGE_MODE_STATE: the system VM's own lock");
    seq_out(SYS_VM, 0x80, 0x00);
    r = call(fn(45), regs(0x1111, 0x2222, 0x3333, 0x4444), "SAVE_MESSAGE_MODE_STATE");
    CHECK((r.eax == 0x1111) && (r.ebx == 0x2222) && (r.ecx == 0x3333) && (r.edx == 0x4444) &&
              (r.esi == 0x51515151u) && (r.edi == 0xd1d1d1d1u),
          "SAVE_MESSAGE_MODE_STATE: registers changed");
    seq_out(SYS_VM, 0x80, 0x98);
    CHECK(vga.disp_unlocked, "after SAVE_MESSAGE_MODE_STATE: the system VM's lock does not reach the hardware");
    seq_out(SYS_VM, 0x80, 0x00);

    /* A blue screen: the desktop is saved, the engine goes off; the pseudo-VMs have no control blocks. */
    call(fn(8), regs(0, SYS_VM, 0, 0), "SAVE_REGISTERS (desktop)");
    for (i = 0; i < VS_BYTES; i++) {
        wr8(MSG_VM + CB_OFFSET + i, 0xee);
        wr8(PLANAR_VM + CB_OFFSET + i, 0xee);
    }
    n_seq_writes = 0;
    crtc_owner   = MSG_VM;
    memc_owner   = MSG_VM;
    r = call(fn(9), regs(0, 0, MSG_VM, 0), "RESTORE_REGISTERS (message mode)");
    CHECK(!vga_disp_enabled(&vga) && !vga.disp_unlocked && (disp(0x92, 2) == 1024) && (n_seq_writes == 1),
          "message mode: engine not off, or more than CONTROL written");
    call(fn(8), regs(0, MSG_VM, 0, 0), "SAVE_REGISTERS (message mode)");
    call(fn(8), regs(0, PLANAR_VM, 0, 0), "SAVE_REGISTERS (forced planar)");
    call(fn(9), regs(0, 0, MSG_VM, 0), "RESTORE_REGISTERS (message mode again)");
    uc_mem_read(uc, MSG_VM + CB_OFFSET, cb, sizeof(cb));
    for (i = 0; i < VS_BYTES; i++)
        nonzero += cb[i] != 0xee;
    uc_mem_read(uc, PLANAR_VM + CB_OFFSET, cb, sizeof(cb));
    for (i = 0; i < VS_BYTES; i++)
        nonzero += cb[i] != 0xee;
    CHECK(nonzero == 0, "a pseudo-VM ID was used as a VM handle (%d bytes written)", nonzero);

    /* Font loading: forced planar as the memory owner, the screen's owner unchanged. */
    {
        cpu_t in = regs(0, 0, MSG_VM, 0);

        in.esi       = PLANAR_VM;
        memc_owner   = PLANAR_VM;
        n_seq_writes = 0;
        call(fn(9), in, "RESTORE_REGISTERS (forced planar memory)");
        CHECK(n_seq_writes == 0, "RESTORE_REGISTERS for the forced planar state wrote the engine");
    }

    /* Back to the desktop: its saved state, engine on. */
    crtc_owner   = SYS_VM;
    memc_owner   = SYS_VM;
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop after message mode)");
    CHECK(vga_disp_enabled(&vga) && (disp(0x92, 2) == 1024) && (disp(0x91, 1) == 8) && control_last(),
          "the desktop's state not back after message mode");
}

/* The VDD's way into VGA and out of it, in any order: the desktop always comes back. */
static void
test_transitions(void)
{
    cpu_t r;

    /* A switch that goes to VGA and straight back: the desktop's state, kept by PRE_HIRES_TO_VGA, returns. A
       save after the engine went off does not replace it. */
    init_card();
    call(fn(4), regs(0, SYS_VM, 0, 0), "PRE_HIRES_TO_VGA");
    CHECK(!vga_disp_enabled(&vga), "PRE_HIRES_TO_VGA: engine still on");
    call(fn(8), regs(0, SYS_VM, 0, 0), "SAVE_REGISTERS (desktop, engine off)");
    CHECK(rd8(SYS_VM + CB_OFFSET + 1 + 0x10) == 1, "the desktop's state was saved with the engine off");
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop)");
    CHECK(vga_disp_enabled(&vga) && (disp(0x92, 2) == 1024) && control_last(),
          "the desktop did not come back after PRE_HIRES_TO_VGA");

    /* POST_HIRES_TO_VGA: the new owner's state is the hardware's after the BIOS mode set. */
    call(fn(4), regs(0, SYS_VM, 0, 0), "PRE_HIRES_TO_VGA");
    crtc_owner = DOS_VM;
    memc_owner = DOS_VM;
    wr8(DOS_VM + CB_OFFSET + 1 + 0x10, 1); /* a stale copy with the engine on */
    r = call(fn(5), regs(0x1111, DOS_VM, 0x3333, 0x4444), "POST_HIRES_TO_VGA");
    CHECK((r.eax == 0x1111) && (r.ebx == DOS_VM) && (r.ecx == 0x3333) && (r.edx == 0x4444) &&
              (r.esi == 0x51515151u) && (r.edi == 0xd1d1d1d1u),
          "POST_HIRES_TO_VGA: registers changed");
    CHECK(rd8(DOS_VM + CB_OFFSET + 1 + 0x10) == 0, "POST_HIRES_TO_VGA did not take the new owner's state");
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (session)");
    CHECK(!vga_disp_enabled(&vga) && (n_seq_writes == 0), "RESTORE_REGISTERS after POST_HIRES_TO_VGA");
    seq_out(DOS_VM, 0x80, 0x98);
    seq_out(DOS_VM, 0xa2, 0x13);
    call(fn(8), regs(0, DOS_VM, 0, 0), "SAVE_REGISTERS (session)");
    CHECK(rd8(DOS_VM + CB_OFFSET + 1 + 0x22) == 0x13, "SAVE_REGISTERS skipped the session after POST_HIRES_TO_VGA");
    seq_out(DOS_VM, 0x80, 0x00);

    /* Back: the desktop's state from before. */
    crtc_owner   = SYS_VM;
    memc_owner   = SYS_VM;
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop)");
    CHECK(vga_disp_enabled(&vga) && (disp(0x92, 2) == 1024) && (disp(0xa2, 2) == 0xc105) && control_last(),
          "the desktop did not come back after a session");

    /* POST_HIRES_TO_VGA for the desktop's VM or message mode keeps the desktop's state, and message mode has no
       control block. */
    call(fn(4), regs(0, SYS_VM, 0, 0), "PRE_HIRES_TO_VGA");
    wr8(MSG_VM + CB_OFFSET + 1 + 0x10, 0xee);
    call(fn(5), regs(0, MSG_VM, 0, 0), "POST_HIRES_TO_VGA (message mode)");
    CHECK(rd8(MSG_VM + CB_OFFSET + 1 + 0x10) == 0xee, "POST_HIRES_TO_VGA wrote through the message mode ID");
    call(fn(5), regs(0, SYS_VM, 0, 0), "POST_HIRES_TO_VGA (desktop's VM)");
    CHECK(rd8(SYS_VM + CB_OFFSET + 1 + 0x10) == 1, "POST_HIRES_TO_VGA replaced the desktop's state");
    call(fn(9), regs(0, 0, SYS_VM, 0), "RESTORE_REGISTERS (desktop)");
    CHECK(vga_disp_enabled(&vga), "the desktop did not come back after POST_HIRES_TO_VGA for its VM");

    /* A destroyed VM's handle, used again, is a new VM. */
    crtc_owner = DOS_VM;
    call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (session)");
    r = call(rd32(ddb + 24), regs(0x0c, 0x00440000u, 0, 0), "Destroy_VM (another VM)");
    CHECK(!r.cf, "Destroy_VM failed");
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (session)");
    CHECK(n_seq_writes == 0, "Destroy_VM of another VM changed which state the hardware holds");
    r = call(rd32(ddb + 24), regs(0x0c, DOS_VM, 0, 0), "Destroy_VM");
    CHECK(!r.cf, "Destroy_VM failed");
    seq_out(DOS_VM, 0x80, 0x98);
    seq_out(DOS_VM, 0x91, 32);
    seq_out(DOS_VM, 0x80, 0x00);
    memset(seq_write_log, 0, sizeof(seq_write_log));
    n_seq_writes = 0;
    call(fn(9), regs(0, 0, DOS_VM, 0), "RESTORE_REGISTERS (a new VM with the old handle)");
    CHECK(n_seq_writes > 0, "a new VM with a destroyed VM's handle was taken for it");
}

/* A DeviceIoControl as VWIN32 hands it to the mini-VDD: the DIOCParams at DIOC_AT, the input at +100h, the
   output at +200h (filled with OUT_UNTOUCHED, so a check sees what was written), the bytes returned at +300h.
   Only EAX may change, and carry stays clear. */
#define OUT_UNTOUCHED 0xa5a5a5a5u

static cpu_t
dioc(uint32_t code, const uint32_t *in, uint32_t in_bytes, uint32_t out_bytes)
{
    cpu_t    c = regs(0x23, SYS_VM, code, 0x5555u);
    cpu_t    r;
    uint32_t i;

    for (i = 0; i < 0x400; i += 4)
        wr32(DIOC_AT + i, ((i >= 0x200) && (i < 0x300)) ? OUT_UNTOUCHED : 0);
    wr32(DIOC_AT + 12, code);
    wr32(DIOC_AT + 16, in ? DIOC_AT + 0x100 : 0);
    wr32(DIOC_AT + 20, in_bytes);
    wr32(DIOC_AT + 24, out_bytes ? DIOC_AT + 0x200 : 0);
    wr32(DIOC_AT + 28, out_bytes);
    wr32(DIOC_AT + 32, DIOC_AT + 0x300);
    for (i = 0; in && (i < (in_bytes + 3) / 4); i++)
        wr32(DIOC_AT + 0x100 + 4 * i, in[i]);
    c.esi = DIOC_AT;
    r     = call(rd32(ddb + 24), c, "W32_DEVICEIOCONTROL");
    CHECK(!r.cf && (r.ebx == c.ebx) && (r.ecx == c.ecx) && (r.edx == c.edx) && (r.esi == c.esi) && (r.edi == c.edi) &&
              (r.ebp == c.ebp),
          "W32_DEVICEIOCONTROL %d: carry set, or registers changed", (int) code);
    return r;
}

static uint32_t
dioc_out(int i)
{
    return rd32(DIOC_AT + 0x200 + 4u * (uint32_t) i);
}

/* Starts the mini-VDD with the GPU's registers mapped, PROFILE as given and an earlier boot's page table named. */
static void
pool_boot(uint32_t profile, const char *what)
{
    cpu_t r;

    machine();
    vga.regs_base = REGS_PHYS;
    wr32(REGS_LIN + VGPU98_REG_PROFILE, profile);
    wr32(REGS_LIN + VGPU98_REG_GPUVM_BASE, 0x13579000u);
    wr32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES, 77);
    r = call(rd32(ddb + 24), regs(0x1b, SYS_VM, 0, 0), what);
    CHECK(!r.cf, "%s: initialization failed", what);
}

/* Entries [from, to) of the GPU's page table: how many are not as a chunk mapped from linear page `first` would
   make them (with `first` ~0, how many are not 0). */
static int
table_bad(uint32_t from, uint32_t to, uint32_t first)
{
    int      bad = 0;
    uint32_t i;

    for (i = from; i < to; i++) {
        uint32_t want = (first == ~0u) ? 0 : ((CHUNK_PHYS + (((first + i - from) ^ 1u) << 12)) | VGPU98_GPUVM_VALID);

        bad += rd32(TABLE_LIN + 4 * i) != want;
    }
    return bad;
}

/* The pool (interface 3.0): the GPU's page table at boot, chunks mapped and released through DeviceIoControl. */
static void
test_pool(void)
{
    uint32_t in[1];
    uint32_t at;
    cpu_t    r;

    pool_boot(0x7f, "Sys_Dynamic_Device_Init (pool)");
    CHECK(demand_calls == 1, "_GetDemandPageInfo called %d times", demand_calls);
    /* A quarter of 128 MB is 8192 pages: a 32 KB table, contiguous, fixed and zeroed above the first megabyte. */
    CHECK((contig_allocs == 2) && (last_page_args[0] == 8) && (last_page_args[1] == 1) && !last_page_args[2] &&
              !last_page_args[3] && (last_page_args[4] == 0x100) && (last_page_args[5] == 0xfffff) &&
              (last_page_args[7] == 0x8f),
          "the page table: not 8 contiguous fixed pages");
    CHECK((rd32(REGS_LIN + VGPU98_REG_GPUVM_BASE) == TABLE_PHYS) && !rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES),
          "GPUVM_BASE %08X, GPUVM_ENTRIES %u", rd32(REGS_LIN + VGPU98_REG_GPUVM_BASE),
          rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES));
    CHECK(strstr(vga.debug, "pool ready") != NULL, "no debug line for the pool");

    r = dioc(0, NULL, 0, 0);
    CHECK(r.eax == 0, "DIOC_OPEN: %u", r.eax);
    r = dioc(0xffffffffu, NULL, 0, 0);
    CHECK(r.eax == 0, "DIOC_CLOSEHANDLE: %u", r.eax);
    r = dioc(9, NULL, 0, 0);
    CHECK(r.eax == 50, "an unknown code: %u, not ERROR_NOT_SUPPORTED", r.eax);
    r = dioc(3, NULL, 0, 12);
    CHECK((r.eax == 0) && (dioc_out(0) == 0x2000) && !dioc_out(1) && !dioc_out(2) && (rd32(DIOC_AT + 0x300) == 12),
          "POOL_QUERY at boot: %u %u %u %u", r.eax, dioc_out(0), dioc_out(1), dioc_out(2));

    /* A chunk: fixed, locked, zeroed pages; their physical pages in the table, VALID and nothing else. */
    in[0] = 256;
    r     = dioc(1, in, 4, 12);
    CHECK((r.eax == 0) && (dioc_out(0) == CHUNK_LIN) && (dioc_out(1) == 0) && (dioc_out(2) == 256) &&
              (rd32(DIOC_AT + 0x300) == 12),
          "POOL_GROW 256: %u, %08X %08X %u", r.eax, dioc_out(0), dioc_out(1), dioc_out(2));
    CHECK((last_page_args[0] == 256) && (last_page_args[1] == 1) && !last_page_args[2] && !last_page_args[6] &&
              (last_page_args[7] == 0x89),
          "a chunk's _PageAllocate: not fixed, locked and zeroed pages in the system arena");
    CHECK(!table_bad(0, 256, 0) && !table_bad(256, 384, ~0u), "the chunk's page table entries");
    CHECK(rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES) == 256, "GPUVM_ENTRIES %u", rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES));
    in[0] = 128;
    r     = dioc(1, in, 4, 12);
    CHECK((r.eax == 0) && (dioc_out(0) == CHUNK_LIN + 0x100000u) && (dioc_out(1) == 0x100000u) &&
              !table_bad(256, 384, 256) && (rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES) == 384),
          "the second chunk: %u, %08X %08X", r.eax, dioc_out(0), dioc_out(1));

    /* Released: its entries cleared, its pages freed; the next chunk that fits takes its place. */
    in[0] = 0;
    r     = dioc(2, in, 4, 0);
    CHECK((r.eax == 0) && (n_freed == 1) && (freed[0] == 0x12360001u) && !table_bad(0, 256, ~0u) &&
              !table_bad(256, 384, 256),
          "POOL_RELEASE: %u, %d freed", r.eax, n_freed);
    r = dioc(3, NULL, 0, 12);
    CHECK((dioc_out(1) == 128) && (dioc_out(2) == 384), "POOL_QUERY after a release: %u %u", dioc_out(1), dioc_out(2));
    in[0] = 64;
    r     = dioc(1, in, 4, 12);
    CHECK((r.eax == 0) && (dioc_out(1) == 0) && !table_bad(0, 64, 384) && (rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES) == 384),
          "a chunk after a release: %u at %08X", r.eax, dioc_out(1));

    /* Refusals, each naming its step in the output. */
    in[0] = 0x2000;
    r     = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && !dioc_out(0) && (dioc_out(1) == 1), "past the cap: %u, step %u", r.eax, dioc_out(1));
    in[0] = 16;
    r     = dioc(1, in, 2, 12);
    CHECK((r.eax == 87) && (dioc_out(0) == OUT_UNTOUCHED) && (dioc_out(1) == OUT_UNTOUCHED),
          "a short input: %u, the output %08X %08X", r.eax, dioc_out(0), dioc_out(1));
    r = dioc(1, in, 4, 8);
    CHECK((r.eax == 87) && (dioc_out(0) == OUT_UNTOUCHED) && (dioc_out(1) == OUT_UNTOUCHED),
          "a short output: %u, the output %08X %08X", r.eax, dioc_out(0), dioc_out(1));
    in[0] = 0x5000;
    r     = dioc(2, in, 4, 0);
    CHECK(r.eax == 87, "releasing an unknown chunk: %u", r.eax);
    in[0] = 0x100800;
    r     = dioc(2, in, 4, 0);
    CHECK(r.eax == 87, "releasing an unaligned address: %u", r.eax);
    copy_fail = 1;
    n_freed   = 0;
    in[0]     = 32;
    r         = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && (n_freed == 1) && !table_bad(64, 256, ~0u) && !dioc_out(0) && (dioc_out(1) == 5),
          "a chunk whose pages cannot be found: %u, %d freed, step %u", r.eax, n_freed, dioc_out(1));
    page_fail = 1;
    n_freed   = 0;
    r         = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && !n_freed && !table_bad(64, 256, ~0u) && !dioc_out(0) && (dioc_out(1) == 4),
          "a chunk Windows has no pages for: %u, %d freed, step %u", r.eax, n_freed, dioc_out(1));
    /* A page in the adapter area, which the GPU cannot read: the chunk goes. */
    copy_adapter = 1;
    n_freed      = 0;
    r            = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && (n_freed == 1) && !table_bad(64, 256, ~0u) && !dioc_out(0) && (dioc_out(1) == 7),
          "a chunk with a page in the adapter area: %u, %d freed, step %u", r.eax, n_freed, dioc_out(1));
    /* A page not present (locked pages always are): the chunk goes. */
    copy_absent = 1;
    n_freed     = 0;
    r           = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && (n_freed == 1) && !table_bad(64, 256, ~0u) && !dioc_out(0) && (dioc_out(1) == 6),
          "a chunk with a page not present: %u, %d freed, step %u", r.eax, n_freed, dioc_out(1));
    r = dioc(3, NULL, 0, 12);
    CHECK(dioc_out(1) == 192, "pages mapped: %u", dioc_out(1));
    /* A page of conventional memory is RAM like any other: the chunk stays. */
    copy_low = 1;
    at       = chunk_next >> 12;
    r        = dioc(1, in, 4, 12);
    CHECK((r.eax == 0) && (dioc_out(1) == 0x40000u) &&
              (rd32(TABLE_LIN + 4u * 64) == (0x0009f000u | VGPU98_GPUVM_VALID)) && !table_bad(65, 96, at + 1),
          "a chunk with a page of conventional memory: %u at %08X, entry %08X", r.eax, dioc_out(1), rd32(TABLE_LIN + 4u * 64));
    in[0] = 0x40000;
    r     = dioc(2, in, 4, 0);
    CHECK((r.eax == 0) && !table_bad(64, 256, ~0u), "releasing it: %u", r.eax);
    r = dioc(3, NULL, 0, 12);
    CHECK(dioc_out(1) == 192, "pages mapped: %u", dioc_out(1));

    /* Windows leaves: no page table. */
    r = call(rd32(ddb + 24), regs(5, SYS_VM, 0, 0), "System_Exit");
    CHECK(!r.cf && !rd32(REGS_LIN + VGPU98_REG_GPUVM_BASE) && !rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES),
          "System_Exit left the page table named");

    /* A GPU without GPUVM: the last boot's table forgotten, no pool. */
    pool_boot(0x1f, "Sys_Dynamic_Device_Init (GPU without GPUVM)");
    CHECK(!demand_calls && (contig_allocs == 1) && !rd32(REGS_LIN + VGPU98_REG_GPUVM_BASE) &&
              !rd32(REGS_LIN + VGPU98_REG_GPUVM_ENTRIES),
          "a page table for a GPU that cannot read it");
    in[0] = 16;
    r     = dioc(1, in, 4, 12);
    CHECK((r.eax == 8) && !dioc_out(0) && (dioc_out(1) == 1), "a chunk without a pool: %u, step %u", r.eax, dioc_out(1));
    r = dioc(3, NULL, 0, 12);
    CHECK((r.eax == 0) && !dioc_out(0), "POOL_QUERY without a pool: cap %u", dioc_out(0));
}

int
main(int argc, char **argv)
{
    set_progname("vxdtest");
    if (argc != 2)
        die("usage: vxdtest VXD");
    le_file = read_file(argv[1], &le_size);
    test_init();
    if (!failures) {
        test_ring_buffer();
        test_pool();
        test_windowed();
        test_full_screen();
        test_message_mode();
        test_transitions();
    }
    printf("vxdtest: %d checks, %d failed\n", checks, failures);
    if (uc)
        uc_close(uc);
    return failures ? 1 : 0;
}
