# Display modes

Every extended mode the card offers comes from one table, `card/display-modes.card`. `cardc` checks it and generates the VBE BIOS's mode list (`build/gen/vbemodes.inc`), the Windows driver's desktop list (`build/gen/drvmodes.inc`) and the monitor's EDID (in the 86Box header). `infgen` writes the INF's mode list from the same file.

## The table

```
[mode 1024x768]
desktop = 1                 ; a Windows desktop mode at 8, 16 and 32 bpp
refresh = 60 70 75 85 100 120
vbe8    = 0x105             ; VBE mode numbers per depth: a number, auto or none
vbe15   = 0x116
vbe16   = 0x117
                            ; vbe32 omitted: auto
```

| Key | Meaning | Default |
|---|---|---|
| `desktop` | 1: offered to Windows at 8, 16 and 32 bpp (listed in the INF) | 0 (VBE only) |
| `enabled` | 0: defined but offered nowhere, and given no VBE numbers | 1 |
| `refresh` | Refresh rates in Hz; the first is the default (VBE mode sets use it) | required |
| `vbe8`, `vbe15`, `vbe16`, `vbe32` | VBE mode number for that depth: a number from 100h to 1FFh, `auto` or `none` | `auto` |

Rules checked by `cardc`: widths are multiples of 8 from 8 to 1920, heights 1 to 1440; no resolution twice; refresh rates within 50-160 Hz and not repeated; VBE numbers unique. `auto` numbers are assigned from 120h upward in file order, skipping numbers already given explicitly.

The standard VESA numbers (VBE 1.2) are used where they exist for 8, 15 and 16 bpp: 100h, 101h, 103h, 105h, 107h, 10Dh, 10Eh, 110h, 111h, 113h, 114h, 116h, 117h, 119h, 11Ah. The VESA 24-bit numbers (10Fh, 112h, 115h, 118h, 11Bh) are left unused, because the card has no 24 bpp packed format; 32 bpp modes get OEM numbers, so programs that hard-code a 24-bit number do not get a format they cannot draw.

## The modes

| Group | Resolutions | Offered as |
|---|---|---|
| Game modes | 320x200, 320x240, 400x300, 512x384, 640x400 | VBE, 8/15/16/32 bpp; DirectDraw at 8/16/32 bpp (the Windows driver sets those narrower than 640 with double scan) |
| Desktop (Voodoo3 1.07 driver's list) | 640x480, 800x600, 960x720, 1024x768, 1152x864, 1280x960, 1280x1024, 1600x1024, 1600x1200, 1792x1344, 1856x1392, 1920x1080, 1920x1200, 1920x1440 | Windows at 8/16/32 bpp; VBE at 8/15/16/32 bpp |
| Extra widescreen | 1280x720, 1280x800, 1360x768, 1440x900, 1680x1050 | Nothing until enabled |

The desktop refresh lists are the Voodoo3 driver's.

## Layout and memory

- Bytes per line (pitch): width × bytes per pixel (1, 2, 2 or 4). Widths are multiples of 8, so every line is a whole number of 32-bit words, as Windows' DIB engine needs.
- A mode needs pitch × height bytes. Modes that do not fit the configured video memory are not offered: the VBE BIOS leaves them out of its mode list, and the Windows driver reports them as needing more memory, so Display Properties does not show them.

The Windows driver keeps the top 256 KB of video memory for the GPU ([gpu-registers.md](gpu-registers.md)), so its modes must fit below it. With 8 MB, every desktop resolution works at 8 and 16 bpp; at 32 bpp, 1920x1080, 1920x1200, 1792x1344, 1856x1392 and 1920x1440 need 16 MB (1600x1200 still fits). With 16 MB or more, every mode fits. The VBE BIOS has no reserve: its mode list is unchanged.

## Timing

The display engine generates its own blanking: every line has 160 extra pixel clocks and every frame max(23, height / 20) extra lines. The pixel clock is (width + 160) × (height + blanking) × refresh. 86Box paces the emulated retrace to that clock, so the guest sees the refresh rate it asked for. The EDID's detailed timing and range limits use the same rule.

## The monitor (EDID)

The card reports one virtual monitor (`[monitor]` in `card/vgpu98.card`): an EDID 1.3 block with

- manufacturer ID "VGP", product 2000h, serial 1, made in week 41 of 2026, named "VGPU98 Screen";
- analog input, 400 x 300 mm, gamma 2.2, sRGB colour, DPMS standby/suspend/off, GTF supported;
- established timings for the 640x480, 800x600, 1024x768 and 1280x1024 rates in the table;
- standard timings for every other desktop resolution with a 4:3, 5:4, 16:10 or 16:9 shape, at its highest rate up to 123 Hz (the most a standard timing can say): eight in the block, the rest in descriptor 4. 1600x1024 (25:16) has no standard-timing shape, so the EDID does not name it; the INF still lists it. `cardc` fails if more than 14 resolutions need standard timings;
- descriptor 1: 1024x768 at 75 Hz (the preferred mode), descriptor 2: range limits covering every desktop mode at every listed rate (56-160 Hz, 30-126 kHz, 240 MHz), descriptor 3: the name, descriptor 4: standard timings 9-14. The serial number is in the header only.

The range limits cover the desktop modes. The low-resolution VBE modes (320x200 to 640x400) run below 30 kHz on the engine's timing rule; they are for DOS programs, which never check them against the monitor.

Windows reads it through the BIOS (VBE/DDC) and uses the range limits to decide which refresh rates the monitor accepts.
