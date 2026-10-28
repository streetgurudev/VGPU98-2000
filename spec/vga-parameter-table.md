# Video parameter table and mode descriptors

## Parameter table

29 entries of 64 bytes. The BIOS programs every standard mode from it, and the save pointer table at 40:A8h points to it so software can read it.

### Entry layout

| Offset | Size | Contents |
|---|---|---|
| 00h | 1 | Text columns |
| 01h | 1 | Text rows - 1 |
| 02h | 1 | Character height in scan lines |
| 03h | 2 | Page size in bytes |
| 05h | 4 | Sequencer registers 01h-04h |
| 09h | 1 | Miscellaneous output register |
| 0Ah | 25 | CRT controller registers 00h-18h |
| 23h | 20 | Attribute controller registers 00h-13h |
| 37h | 9 | Graphics controller registers 00h-08h |

### Entries

| Entry | Mode |
|---|---|
| 00h-03h | Modes 00h-03h at 200 lines (CGA emulation) |
| 04h-07h | Modes 04h-07h (07h at 350 lines) |
| 08h-0Ch | Reserved, zero |
| 0Dh-0Eh | Modes 0Dh-0Eh |
| 0Fh-10h | Modes 0Fh-10h on a 64 KB EGA. Unused by the BIOS; copies of entries 11h-12h. |
| 11h-12h | Modes 0Fh-10h |
| 13h-16h | Modes 00h-03h at 350 lines |
| 17h | Modes 00h-01h at 400 lines |
| 18h | Modes 02h-03h at 400 lines |
| 19h | Mode 07h at 400 lines |
| 1Ah-1Ch | Modes 11h, 12h, 13h |

Register values follow the VGA standard. Every implemented entry was checked byte for byte against a reference VGA BIOS.

## Mode descriptors

20 entries of 10 bytes, one per mode 00h-13h.

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | Memory model: 0 none, 1 text, 2 CGA 4-colour, 3 CGA 2-colour, 4 planar, 5 256-colour |
| 1 | 1 | Flags: bit 0 = monochrome CRTC and status ports (3B4h/3BAh). The segment is given separately: mode 07h uses B000h, mode 0Fh A000h. |
| 2 | 2 | Screen memory segment |
| 4 | 3 | Parameter entry for 200, 350 and 400 scan lines. Graphics modes repeat one entry. |
| 7 | 3 | Palette for 200, 350 and 400 scan lines: 0 `cga64`, 1 `ega64`, 2 `mono64`, 3 `vga256` |

The scan-line setting comes from the BIOS data area (40:89h, set by INT 10h AH=12h BL=30h). The default is 400 lines.
