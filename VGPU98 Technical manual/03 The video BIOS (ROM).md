# 03 The video BIOS (ROM)

The card's ROM is its own VGA BIOS: the code a PC runs at power-on to bring up the display, and the INT 10h services DOS, Windows setup and DOS programs use for as long as no Windows driver has taken over. This document covers what is in it, how it starts, what it offers, and how it is built and checked. The reference is [`spec/bios.md`](../spec/bios.md) (behaviour), [`spec/rom-layout.md`](../spec/rom-layout.md) (the image), [`spec/modes.md`](../spec/modes.md) (the mode tables) and [`spec/font-format.md`](../spec/font-format.md) (the fonts).

## What the ROM is

| Fact | Value |
|---|---|
| File | `roms/video/vgpu98/vgpu98.bin` under 86Box's `roms` folder, 32 KB |
| Kind | A PCI expansion ROM, code type x86, one image |
| Sign-on | "VGPU98 2000 PCI VGA BIOS Version 1.00" |
| Where it runs | Copied by the system BIOS to C0000h, the primary VGA BIOS's place |
| Source | `bios/`, about 5,400 lines of NASM, with `card/vgpu98.card` and the mode tables compiled in |
| Fonts | 8x8, 8x14 and 8x16, code page 437, the project's own design |

The ROM is built from source by the project's tools; it changes only when the BIOS or the card definition does. `release.sh` copies the built ROM into `bin\roms\video\vgpu98\` every time.

## The image

```
 offset 0000h  55h AAh, size in 512-byte blocks (64)
        0003h  entry: far-called by the system BIOS at power-on
        0018h  pointer to the PCI data structure
        001Eh  "IBM VGA Compatible" (some software checks for it)
               PCI data structure "PCIR": vendor 9898h, device 2000h,
               class 030000h, image length, BIOS revision, last image
               code: POST, INT 10h, mode set, text, graphics, palette,
               fonts and state services, VBE 2.0 and VBE/DDC, debug
               data: parameter tables, mode tables, the fonts, the
               protected-mode interface table
 last byte     checksum: the whole image sums to 0 (set by romfix)
```

| Source file | What it holds |
|---|---|
| `vgpu98.asm` | The ROM header and PCI data structure; includes everything else |
| `post.asm` | Power-on initialization |
| `int10.asm` | INT 10h's entry and dispatch, and the small services (cursor, pages, get mode, display combination) |
| `mode.asm` | Mode set, palette loading and font loading |
| `text.asm` | Character output, scrolling and the teletype in text modes |
| `gfx.asm` | Characters and pixels in the graphics modes; the CGA palette |
| `funcs.asm` | Palette and DAC (AH=10h), fonts (11h), alternate select (12h), state information and save/restore (1Bh, 1Ch) |
| `vbe.asm` | VBE 2.0 and VBE/DDC |
| `data.asm` | The save pointer table, the display combination table, per-mode tables |
| `debug.asm` | Debug text to the card's debug register |

## Power-on

```
 86Box powers on
      |
 the system BIOS finds the PCI ROM (55AAh, "PCIR", class 03h),
 copies it to C0000h and far-calls offset 3
      |
 POST (post.asm):
   INT 10h -> the ROM's handler; the board's own video handler
             moves to INT 42h (unless INT 10h is already the ROM's)
   INT 6Dh -> the same handler
   INT 1Fh, INT 43h -> the 8x8 font (upper and lower halves)
   40:A8h  -> the save pointer table
   the GPU -> its power-on state (the display engine's GPU_RESET),
             so a GPU left running by the last boot writes nothing
   mode 03h (80x25 text), then the sign-on line
      |
 the system BIOS continues: memory test, boot device, DOS or Windows
```

From here until Windows loads `vgpu98.drv`, everything on screen comes through the ROM's services and the standard VGA.

## The VGA services (INT 10h)

The ROM implements the IBM VGA BIOS interface: mode set (AH=00h), the cursor (01h-03h), pages (05h), scrolling (06h, 07h), characters and pixels (08h-0Eh), get mode (0Fh), palettes and the DAC (10h), fonts (11h), alternate select (12h), write string (13h), display combination (1Ah), state information (1Bh) and state save/restore (1Ch). The standard VGA modes 00h-13h come from `card/vga-modes.card`.

All of these run on 86Box's register-exact VGA. The ROM programs the VGA's registers as an IBM VGA BIOS does, and 86Box's own VGA renderer draws the result ([05](05%20The%20display%20path.md)).

Inside INT 10h the caller's registers are kept in a stack frame: DS is the BIOS data area, every register the caller didn't get a result in is restored, FS and GS included.

## VBE 2.0: the extended modes

AX=4Fxxh is the VESA BIOS Extensions 2.0 interface, for DOS programs and for Windows' setup before the driver is installed.

| Function | What it does |
|---|---|
| 4F00h | Controller information: VBE 2.0, the OEM strings, the mode list, the memory size |
| 4F01h | Mode information, the linear frame buffer address (BAR1) among it |
| 4F02h | Set a mode: banked or linear, optionally keeping memory |
| 4F03h | The current mode |
| 4F04h | Save and restore the state |
| 4F05h | Bank window control (also as a far call) |
| 4F06h | Scan line length |
| 4F07h | Display start, for page flipping |
| 4F08h, 4F09h | DAC width; palette data |
| 4F0Ah | The protected-mode interface |
| 4F15h | VBE/DDC: the monitor's EDID |

**The modes** come from `card/display-modes.card`, the same table the Windows driver and the INF use:
- the standard VESA numbers 100h-11Ah where VESA defined one;
- OEM numbers from 120h for the rest, in the table's order;
- the desktop's 14 resolutions, 640x480 to 1920x1440, at 8, 15, 16 and 32 bits per pixel;
- low resolutions for DOS games, 320x200 to 640x400, double-scanned on the display engine.

The extended modes run on the card's display engine, not the VGA. Every VBE call unlocks the engine's registers and leaves the lock as it found it ([05](05%20The%20display%20path.md)). A VBE mode set uses the mode's first refresh rate; VBE 3.0's refresh control is not offered.

**DDC** returns the virtual monitor's EDID 1.3: maker "VGP", "VGPU98 Screen", a 20-inch 4:3 screen with 1024x768 at 75 Hz as its preferred timing.

## The fonts

The 8x8, 8x14 and 8x16 fonts are drawn as text art in `fonts/` and compiled by the project's `fontc` into NASM includes. They are the project's own design. A test, `fonts_independent`, compares them glyph by glyph with a reference VGA BIOS image and fails if too many glyphs are identical; the image is read only, never copied, and the test skips itself without it.

## Debug output

The ROM can write lines to 86Box's log through the card's debug register, sequencer index E0h. It writes a character only while that index reads 01h, which it does only while 86Box's log is listening; on other hardware or with logging off, nothing is written. The sequencer index is restored afterwards, so a debug line never disturbs a caller. The display driver and the mini-VDD use the same register.

## Building and checking it

```
 card/vgpu98.card ------cardc-----> card.inc (identity, sizes, registers)
 card/vga-modes.card                vga mode tables
 card/display-modes.card            VBE mode tables
 fonts/*.fnt -----------fontc-----> font includes
                                         |
 bios/*.asm ------------NASM (flat binary)
                                         |
                        romfix: pad to 32 KB, set the checksum
                                         |
                        romlint: check the image against the card
                        definition (header, PCIR, size, checksum)
                                         |
                        biostest: run it on the host under Unicorn,
                        against a VGA model of its own (not 86Box's)
                                         |
                              build/.../vgpu98.bin
                                         |
              release.sh: copy to bin/roms/video/vgpu98/vgpu98.bin
```

The image is reproducible: two builds from different folders give the same bytes, and `release.sh --repro` checks it ([15](15%20Building%2C%20tools%20and%20file%20formats.md)).
