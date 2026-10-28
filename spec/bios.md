# VGA BIOS

The BIOS is a 32 KB PCI expansion ROM (layout: [rom-layout.md](rom-layout.md)). It gives the card the INT 10h interface of an IBM VGA, so DOS, the Windows 98 SE setup and Windows' standard VGA driver work without anything card-specific. It also has the VESA BIOS Extensions 2.0 for the card's extended modes. Source: `bios/`. Host tests: `tools/biostest`.

Where this page says "as IBM's BIOS", the behaviour was checked call by call against IBM's VGA BIOS running in the same test harness (results only; none of its code or data is used). The deliberate differences are listed at the end.

## Power-on

The system BIOS copies the ROM to C0000h and far-calls offset 3. The BIOS then:

1. Moves the previous INT 10h vector to INT 42h. If INT 10h already points into this ROM (initialization run a second time), INT 42h is left alone.
2. Points INT 10h and INT 6Dh at its handler, INT 43h at the 8x8 font and INT 1Fh at the font's upper half (characters 80h-FFh).
3. Initializes the data area:

| Address | Value | Meaning |
|---|---|---|
| 40:87h | 60h | 256 KB of video memory, cursor emulation on, clear memory on mode set |
| 40:88h | 09h | switch settings 9 (colour display); no feature connector bits |
| 40:89h | 11h | 400 scan lines for text modes, VGA active |
| 40:8Ah | 0Bh | display combination code table index: VGA colour, no alternate display |
| 40:A8h | C000:save pointer table | |

4. Writes the display engine's GPU_RESET (unlocking the engine's registers and locking them again, [display-engine.md](display-engine.md)): whatever reset brought the machine here, a GPU the last boot left running writes no memory of this one.
5. Sets mode 03h and prints the sign-on and copyright lines (from `card/vgpu98.card`) by teletype, followed by a blank line.
6. Returns with every register and the flags preserved.

POST itself does not touch the equipment word (40:10h), but its mode 03h set does: every mode set writes the video bits 5-4 (10b colour 80x25, 11b monochrome).

## Video save pointer table

40:A8h points to a 7-entry save pointer table in the ROM: the video parameter table, then zero (no dynamic save area, no font overrides, no palette profile), then the secondary save pointer table. The secondary table holds the display combination code table. The parameter table layout is in [vga-parameter-table.md](vga-parameter-table.md).

Software may redirect 40:A8h to its own copy. Mode sets read the parameter table through the pointer, so a replaced table is honoured. A null parameter table pointer falls back to the ROM's table.

## INT 10h

On entry the handler enables interrupts and clears the direction flag. Every function returns with the stack balanced and preserves all registers, FS and GS included, except those it reports results in. Function numbers above 1Ch, other than 4Fh (VBE, below), return with nothing changed. The deepest call (a VBE extended mode set) uses 116 bytes of the caller's stack; `biostest` measures every call and fails above 256.

| AH | Function | Notes |
|---|---|---|
| 00h | Set mode | Modes 00h-07h, 0Dh-13h. Bit 7 of AL keeps video memory and sets 40:87h bit 7. Returns AL = 30h (modes 00h-05h, 07h, 0Fh), 3Fh (06h) or 20h (others), as IBM-compatible BIOSes do. 40:60h gets the mode's own cursor shape; 40:65h/40:66h are set only for modes 00h-07h. Other modes are ignored and change nothing. |
| 01h | Cursor shape | See *Cursor emulation* below. |
| 02h, 03h | Cursor position | Per page. AH=03h also returns the shape in CX. |
| 04h | Light pen | Returns AH = 0 (no light pen). |
| 05h | Active page | |
| 06h, 07h | Scroll window up/down | Text, CGA, planar and 256-colour layouts. The window is clipped to the screen. |
| 08h | Read character and attribute | In graphics modes, matches the cell against the current font. A blank cell reads as character 00h. |
| 09h, 0Ah | Write character (and attribute) | Graphics modes: bit 7 of BL draws with XOR. The 2-colour modes 06h and 11h draw the glyph whatever the colour, as IBM's BIOS does. CGA modes take characters 80h-FFh from INT 1Fh. In graphics modes the characters stop at the end of the row. |
| 0Bh | CGA palette | As IBM's BIOS. BH=0: border = BL bits 0-3; in graphics modes also the background (AR00) and palette registers 1-3; BL bit 4 picks the bright foreground set. BH=1: palette 0/1 in BL bit 0, graphics modes only. Both keep 40:66h up to date. |
| 0Ch, 0Dh | Write/read pixel | Bit 7 of AL draws with XOR (not in mode 13h). |
| 0Eh | Teletype | BEL, BS, CR, LF; wraps and scrolls. In graphics modes BL is the foreground colour. |
| 0Fh | Current mode | AL = mode (with bit 7 from 40:87h), AH = columns, BH = active page. |
| 10h | Palette and DAC | All IBM subfunctions 00h-03h, 07h-1Bh. |
| 11h | Character generator | 00h-04h, 10h-14h, 20h-24h, 30h. |
| 12h | Alternate select | 10h, 30h-34h, 36h. |
| 13h | Write string | AL = 0-3. With character/attribute pairs (AL = 2, 3), BEL, BS, LF and CR take no attribute byte, as IBM's BIOS. CX = 0 does nothing. |
| 1Ah | Display combination code | AL = 00h/01h. Returns AX = 001Ah. |
| 1Bh | Functionality and state | See below. |
| 1Ch | Save/restore state | See below. |
| 4Fh | VESA BIOS Extensions | See *VBE 2.0* below. |

### Functions that return AL

AH=12h (supported subfunctions other than BL=10h), AH=1Ah, AH=1Bh and AH=1Ch return AL = the function number (12h, 1Ah, 1Bh, 1Ch). Software uses this to detect a VGA BIOS. AH=1Ah and AH=1Ch also clear AH, as IBM's BIOS.

### Cursor emulation

With emulation on (40:87h bit 0 clear), AH=01h maps a cursor shape written for an 8-line cell onto the current character height *H*, as IBM's BIOS does. Let *M* = *H* - 1 (the bottom line) and *B* = *M* for *H* up to 13, *M* - 1 from 14 on. With *s* = start and *e* = end line, checked in this order:

| Shape | Programmed as |
|---|---|
| hidden (CH bit 5), or *e* = 0 | unchanged |
| *s* > *e* | *e* .. *M* |
| *e* = *M*, or *s* ≤ *e* ≤ 3, or *s* = *e* = *M* - 1 | unchanged |
| *e* - *s* = 0, 1 or 2 | *B* - (*e* - *s*) .. *B* |
| otherwise, *s* ≤ 2 | *s* .. *M* |
| otherwise | *H*/2 .. *M* |

So on an 8x16 font 0607h (underline) becomes 0D0Eh, 0007h a full block 000Fh, and 0307h the lower half 080Fh; on an 8x8 font 0D0Eh becomes 0607h. AH=12h BL=34h turns emulation off. 40:60h always holds the shape as the caller gave it. Across all start/end pairs 0-15 at heights 6-16 the results match IBM's BIOS except 25 shapes that end on the bottom line, where IBM's choice follows no rule worth copying.

### AH=11h row recalculation

Subfunctions 10h-14h load a font and then recalculate the text layout for the new character height *h* (2-32), with the results IBM's BIOS gives:

- the displayed scan lines come from the mode's parameter table entry, so repeated font loads never lose rows;
- CR09 bits 0-4 = *h* - 1;
- rows = displayed scan lines / *h*. The vertical display end (CR12, with bits 8 and 9 in CR07) is set to rows × *h* - 1;
- the cursor is set to lines *h* - 2 and *h* - 1 for *h* up to 13, *h* - 3 and *h* - 2 from 14 on, and 40:60h gets that shape;
- in monochrome modes the underline (CR14) moves to line *h* - 1;
- the data area gets rows - 1 (40:84h), *h* (40:85h) and the page size: rows × columns × 2 + 256 bytes (40:4Ch).

The CRTC protection bit is lifted for the CR07 write and restored. Fonts load with interrupts disabled, so no interrupt handler can write to video memory while plane 2 is mapped.

### AH=1Bh: functionality and state

BX = 0, ES:DI = a 64-byte buffer. Returns AL = 1Bh.

| Offset | Size | Contents |
|---|---|---|
| 00h | dword | far pointer to the static functionality table |
| 04h | 30 bytes | copy of 40:49h-40:66h |
| 22h | byte | rows (40:84h + 1) |
| 23h | word | character height |
| 25h | byte | active display code |
| 26h | byte | alternate display code |
| 27h | word | colours in the current mode (0 for monochrome) |
| 29h | byte | display pages |
| 2Ah | byte | scan lines: 0 = 200, 1 = 350, 2 = 400, 3 = 480 |
| 2Bh | byte | character block for attribute bit 3 = 0 |
| 2Ch | byte | character block for attribute bit 3 = 1 |
| 2Dh | byte | bit 0 all modes active, 1 gray summing, 2 monochrome display, 3 default palette loading off, 4 cursor emulation on, 5 blinking |
| 31h | byte | 3: 256 KB of video memory |
| 32h | byte | bit 1: a dynamic save area is set |
| other | | 0 |

The static functionality table (16 bytes): modes FFh E0h 0Fh (00h-07h, 0Dh-13h); scan lines 07h (200/350/400); 8 character blocks, 2 active at once; misc. functions FFh and 0Eh (save/restore, blink control, display combination code); save pointer flags 23h.

### AH=1Ch: save/restore state

CX selects the parts: bit 0 hardware, bit 1 BIOS data, bit 2 DAC (bit 3, the display engine, only through VBE function 04h). AL=00h returns BX = buffer size in 64-byte blocks; AL=01h saves to ES:BX; AL=02h restores from ES:BX. Returns AX = 001Ch.

The buffer starts with a 32-byte header. Words 0, 1 and 2 hold the offsets (from the buffer start) of the hardware, data area and DAC parts, or 0 for a part not saved; word 3 is the display engine part's (VBE only). The rest of the header is 0. The parts follow in that order.

**Hardware, 66 bytes:**

| Offset | Contents |
|---|---|
| 0 | sequencer index |
| 1-4 | SR1-SR4 |
| 5 | CRTC index |
| 6-30 | CR00-CR18 |
| 31 | attribute index (with the palette address source bit) |
| 32-52 | AR00-AR14 |
| 53 | graphics controller index |
| 54-62 | GR0-GR8 |
| 63 | miscellaneous output |
| 64-65 | CRTC base port (3B4h or 3D4h) |

Restoring holds the sequencer in synchronous reset while the miscellaneous output (and so the clock) changes. The CRTC protection bit is lifted before CR00-CR07 are written.

**BIOS data, 49 bytes:** 40:49h-40:66h, 40:84h-40:8Ah, 40:A8h-40:ABh, then the INT 1Fh and INT 43h vectors. The data area is restored before the hardware, because the hardware restore needs the CRTC port.

**DAC, 771 bytes:** the write index, the PEL mask, the DAC state (3C7h), then 256 red/green/blue entries.

With all three parts the buffer is 918 bytes (15 blocks).

## VBE 2.0 (AH=4Fh)

The BIOS implements the VESA BIOS Extensions 2.0 for the extended modes of the mode table ([modes.md](modes.md)), running on the display engine ([display-engine.md](display-engine.md)). Source: `bios/vbe.asm`.

Every function returns AX = 004Fh on success, 014Fh on failure, 024Fh when the hardware cannot do it and 034Fh when it is invalid in the current mode. Function numbers the BIOS does not have (AL other than 00h-0Ah and 15h) return with AX unchanged, so AL is not 4Fh. Each call unlocks the engine registers and leaves the lock as it found it. The functions use 32-bit registers internally and preserve their upper halves.

| AL | Function | Notes |
|---|---|---|
| 00h | Controller information | ES:DI = 256-byte block, or 512 bytes if it starts with "VBE2". Version 2.0, capabilities 0 (6-bit DAC, VGA-compatible controller), total memory in 64 KB units. The mode list is built in the block's reserved area (offset 34): every mode of the table that fits in video memory, ascending, ended by FFFFh. "VBE2" callers get the four strings copied into the OEM area (offset 256); others get pointers into the ROM. |
| 01h | Mode information | CX = mode. Attributes BBh (supported, colour graphics, not VGA-compatible, windowed and linear; linear only while BAR1 is assigned). Window A: 64 KB at A000h, readable and writable, granularity 64 KB; window B absent. Memory model 4 (packed) for 8 bpp, 6 (direct colour) otherwise, with the colour fields of spec/display-engine.md (15 bpp reports bit 15 as a 1-bit reserved field). Image pages: as many more frames as fit, at most 255. PhysBasePtr = LFB_BASE. The off-screen memory fields are 0. Fails for VGA modes, unknown modes and modes that do not fit. |
| 02h | Set mode | BX bits 0-8 = mode, bit 14 = linear framebuffer, bit 15 = keep video memory. A VGA mode number (00h-7Fh) is an ordinary AH=00h mode set (bit 15 keeps memory). An extended mode first sets mode 13h with memory kept (VGA core and DAC in their 256-colour state, 40:49h = 13h), then programs the engine (format, size, pitch, start 0, the mode's first refresh rate, bank 0, SCRATCH = the mode number with bits 14 and 15) and enables it. Unless bit 15 is set it then clears every image page 01h reports (VBE 2.0: memory beyond them is left alone) through the window. 40:87h bit 7 follows bit 15. Returns 014Fh for unknown modes and modes that do not fit, 024Fh for bit 14 with a VGA mode or while BAR1 is unassigned (no linear framebuffer). |
| 03h | Current mode | BX = the last VBE mode number with its flags while the engine is on; otherwise the VGA mode from 40:49h, with bit 15 from 40:87h bit 7. |
| 04h | Save/restore state | DL = 00h size (BX = 64-byte blocks), 01h save, 02h restore; CX bits 0-2 as AH=1Ch, bit 3 the display engine; ES:BX = buffer. See below. |
| 05h | Window control | BH = 00h set (DX = bank, in 64 KB units), 01h get (DX = bank). Window A only. Returns 034Fh outside an extended mode and in a linear framebuffer mode. Also reachable by a far call to WinFuncPtr (AX, DX destroyed). |
| 06h | Logical line length | BL = 00h set in pixels, 01h get, 02h set in bytes, 03h get maximum. A new length is rounded up to whole pixels and a multiple of 4 bytes, and never below the visible width; longer than the maximum returns 024Fh. Returns BX = bytes per line, CX = pixels per line, DX = lines that fit in video memory. The maximum is the lesser of 65532 bytes and video memory / height. |
| 07h | Display start | BL = 00h set, 80h set and wait, 01h get (BH = 0, CX = pixel, DX = line). The engine shows a new start from the next frame. With BL = 80h the function writes the start and then waits for the next vertical retrace to begin, so the new start is on screen when it returns (page flipping). The start is rounded down to a multiple of 4 bytes; a line past the end of video memory, or a start that would put the frame past it, fails. |
| 08h | DAC format | The DAC is fixed at 6 bits: BL = 00h/01h return BH = 6. Returns 034Fh in a direct colour extended mode. |
| 09h | Palette data | BL = 00h set, 80h wait for the next vertical retrace, then set, 01h get; CX entries from DX, ES:DI = blue, green, red, alignment (6-bit values). BL = 02h/03h (secondary palette) return 024Fh. |
| 0Ah | Protected mode interface | BL = 00h: ES:DI = the table in the ROM, CX = its length. Entries for functions 05h, 07h and 09h; no port or memory list (the code uses only the VGA ports). The 32-bit code is position independent, returns with a near RET and preserves every register except EAX. Function 07h takes DX:CX = start address / 4 and, with BL = 80h, waits like 4F07h. |
| 15h | VBE/DDC | BL = 00h: BX = 0102h (DDC2, about a second per block). BL = 01h, DX = 0: the 128-byte EDID to ES:DI. Other blocks fail. |

Extended-mode programs should not call the text and pixel functions (AH=00h-13h other than the mode set); they act on the VGA state underneath (mode 13h).

### AH=4Fh AL=04h: engine state

CX bit 3 adds an 18-byte part after the DAC part, at the offset in header word 3: SR90h-9Fh (CONTROL first), then the SCRATCH word. Restore writes it last, with CONTROL after every other register, so the engine comes back over an already restored VGA state; restoring a state with CONTROL = 0 turns the engine off. AH=1Ch ignores CX bit 3. With all four parts the buffer is 936 bytes (15 blocks).

### The BIOS scratch register

SR A2h-A3h (SCRATCH) holds the VBE mode number of the last extended mode set, with its bits 14 (linear framebuffer) and 15 (memory kept). The engine does not use it; it travels with the engine state, so function 03h answers correctly after a restore.

## Gray summing

With gray summing on (AH=12h BL=33h, 40:89h bit 1), mode sets and AH=10h AL=1Bh replace each DAC colour with its luminance (76 × red + 151 × green + 28 × blue + 128) / 256. The weights sum to 255, so white stays white; AH=10h AL=1Bh gives the same values as IBM's BIOS.

## Developer debug log

When the card's debug log setting is on, the BIOS writes text to sequencer index E0h ([registers.md](registers.md)). Before each character it reads the index back and writes only if it reads 01h, so with the log off, or under a virtualizer that does not know the register, nothing is written. The lines:

| Line | When |
|---|---|
| `POST: start` | initialization entry |
| `mode NNh` | every mode set, with the mode number as given in AL |
| `POST: done` | initialization finished |

VBE mode sets log `mode 13h` for their VGA base mode set; they add no lines of their own.

## Deliberate differences from IBM's BIOS

- **Mono modes on a colour setup:** asked for mode 07h or 0Fh while the equipment word says colour, IBM's BIOS sets mode 00h. This BIOS sets the mode that was asked for.
- **Gray summing at mode set:** IBM's BIOS uses its own hand-tuned gray ramp for the 16 base colours. This BIOS uses the luminance formula for every entry.
- **AH=09h/0Ah in graphics modes:** IBM's BIOS keeps drawing past the end of the row, and treats CX = 0 as 65536 characters. This BIOS stops at the end of the row and draws nothing for CX = 0.
- **Cursor emulation:** 25 rare shapes that end on the bottom line (see above).
- **Index registers** (sequencer, CRTC, graphics, DAC address) are left at whatever the last access used; IBM's BIOS leaves different values. Software does not depend on them.
- **The saved ROM segment:** the save pointer tables name segment C000h, where a primary VGA BIOS always runs.
