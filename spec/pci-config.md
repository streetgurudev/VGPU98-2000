# PCI configuration space

Values in **bold** come from `card\vgpu98.card`.

| Offset | Size | Field | Value and behaviour |
|---|---|---|---|
| 00h | 2 | Vendor ID | **9898h**, read-only |
| 02h | 2 | Device ID | **2000h**, read-only |
| 04h | 2 | Command | 0003h at reset. Bit 0 (I/O space) and bit 1 (memory space) are writable. All other bits read 0, including bus master. |
| 06h | 2 | Status | 0200h: medium DEVSEL timing. Bit 4 is 0: no capability list. Read-only. |
| 08h | 1 | Revision ID | **01h** |
| 09h | 3 | Class code | **03h 00h 00h**: display controller, VGA-compatible |
| 0Ch | 1 | Cache line size | Reads 0 |
| 0Dh | 1 | Latency timer | Reads 0 (not a bus master) |
| 0Eh | 1 | Header type | 00h, single function |
| 0Fh | 1 | BIST | 00h |
| 10h | 4 | BAR0 | Register window: 32-bit memory, non-prefetchable, **64 KB**. Bits 31:16 writable. |
| 14h | 4 | BAR1 | Framebuffer: 32-bit memory, prefetchable (bit 3 = 1), **128 MB** whatever the VRAM size. Bits 31:27 writable. |
| 18h-27h | | BAR2-BAR5 | Not implemented, read 0 |
| 28h | 4 | CardBus CIS | 0 |
| 2Ch | 2 | Subsystem vendor ID | **9898h** |
| 2Eh | 2 | Subsystem ID | **2000h** |
| 30h | 4 | Expansion ROM BAR | **32 KB**: bits 31:15 writable, bit 0 enables the ROM. Bits 14:1 read 0. |
| 34h | 1 | Capabilities pointer | 0 |
| 3Ch | 1 | Interrupt line | Writable, for the BIOS |
| 3Dh | 1 | Interrupt pin | 01h (INTA). Declared but never raised. |
| 3Eh | 1 | Min_Gnt | 0 |
| 3Fh | 1 | Max_Lat | 0 |

Reads of offsets not listed return 0. Writes to read-only fields are ignored.

## What the command register controls

At reset the command register is 0003h: as the boot VGA device, the card decodes the legacy VGA ports and memory from power-on, so the system BIOS can use it before configuring the bus. This matches 86Box's other PCI video cards. The BARs start at 0 and nothing is mapped through them until the BIOS assigns addresses.

| Bit | When set, the card decodes |
|---|---|
| 0 (I/O) | VGA ports 3C0h-3CFh, plus 3D0h-3DFh when the miscellaneous output register selects colour, or 3B0h-3BFh when it selects monochrome. Within those ranges only the CRTC (3x4h/3x5h) and status/feature (3xAh) ports do anything; the others read FFh. The range not selected is not decoded at all. (In monochrome mode 86Box's VGA core also claims 3A0h-3AFh; those ports read FFh.) |
| 1 (memory) | The A0000h-BFFFFh window (as the graphics controller maps it); BAR0 and BAR1 once they hold an address; the ROM when the ROM BAR enable bit is also set |

With bit 1 clear, the A0000h window stays unmapped even when software changes the graphics controller's memory map (GR06).

A BAR that holds 0, or the all-ones value software writes to size it, is not mapped.

## Reset

A machine reset returns every VGA register to 0 (CR00 = 3Fh and CR06 = FFh, so the CRTC has a sane frame), the attribute flip-flop to index, and the configuration space to the values above. The A0000h window returns to 128 KB at A0000h.

The BIOS is visible only through the expansion ROM BAR. The system BIOS copies it to C0000h and runs it from there, as with any PCI VGA card.
