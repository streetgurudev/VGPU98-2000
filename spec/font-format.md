# Font format

The BIOS fonts live in `fonts\` as text art and are compiled by `fontc` into `build\gen\font8x8.inc`, `font8x14.inc` and `font8x16.inc`. Each glyph becomes one byte per scan line, bit 7 = leftmost pixel. `fontc` also writes a preview sheet per font to `build\font-previews\`.

| Font | Used by |
|---|---|
| `vga8x16.fnt` | 400-line text modes; modes 11h and 12h |
| `vga8x14.fnt` | 350-line text modes; modes 0Fh and 10h. Derived from the 8x16 design. |
| `vga8x8.fnt` | 200-line modes, mode 13h, 43- and 50-line text |

## Syntax

```
; comment
font 8x16                        ; once, first: cell size (width is always 8)
derive vga8x16.fnt drop-rows 0 15 ; optional: start from another font, minus some rows
lines 7 6 8 3 2 4                ; box-drawing geometry (see below)

glyph 41 @2 A                    ; hex code, optional first row, free label
..###...
.##.##..
```

- **`glyph XX [@ROW] [label]`** starts a glyph. Pixel rows follow: exactly 8 characters each, `.` for off and `#` for on. They fill the cell from `@ROW` (default 0). Rows not given are blank, and a glyph with no rows is blank.
- Any line that is not a pixel row ends the glyph.
- **`derive FILE drop-rows N ...`** copies every glyph from another font file (path relative to this one) without the listed rows. `glyph` lines after it redraw individual glyphs. Derived fonts may chain at most 4 deep.
- **`lines HS HD1 HD2 VS VD1 VD2`** gives the box-drawing geometry: the row of single horizontal lines, the two rows of double horizontal lines, the column of single vertical lines, and the two columns of double vertical lines.

## Checks

`fontc` refuses a font unless:
- all 256 code points are defined, none twice, and 00h, 20h and FFh are blank
- printable ASCII (21h-7Eh, except `_`) leaves column 7 clear, so characters stay apart in 8-dot cells (graphics modes and 8-dot text). In 9-dot text the hardware adds a blank ninth column anyway.
- every box-drawing character B3h-DAh has ink on each cell edge exactly where it connects: single or double line in the declared positions, nothing elsewhere. Columns C0h-DFh are stretched to 9 pixels by the hardware in line-graphics mode, so this keeps joins seamless.
- the block characters DBh-DFh fill exactly their full cell, halves or sides
- a derived font loses no pixels: if a dropped row carries pixels that the next row inward does not repeat, the glyph must be drawn again below the `derive` line. Shades, box drawing and blocks (B0h-DFh) are exempt, since they are cell patterns.

Files must be plain ASCII; a UTF-8 byte order mark is refused with a message.

## Design

An original design in the classic VGA style: code page 437, vertical stems two pixels wide and horizontals one pixel, letters in columns 0-6 so column 7 stays clear.

| | 8x16 | 8x14 | 8x8 |
|---|---|---|---|
| Ascenders from | row 2 | row 1 | row 0 |
| Capitals and digits | rows 3-11 | rows 2-10 | rows 0-5 |
| Lower-case x-height | rows 6-11 | rows 5-10 | rows 2-5 |
| Descenders | rows 12-14 | rows 11-13 | rows 6-7 |
| Accents | lower case rows 3-4; accented capitals shrink to rows 4-11 | as 8x16, one row up | lower case rows 0-1; accented capitals shrink to rows 1-5 |
| Box drawing (`lines`) | 7 6 8 3 2 4 | 6 5 7 3 2 4 | 3 2 4 3 2 4 |

Ascenders rise one row above the capitals in the larger sizes. The 8x14 font is the 8x16 design without rows 0 and 15, which the 8x16 design keeps blank outside cell-spanning glyphs. The baseline then sits on row 10, just above the 350-line text cursor (lines 11-12). Accented letters reuse their base letters, with the marks placed by the rules above.

## Provenance

Every glyph is drawn for this project to the metrics above. Nothing is copied from an existing ROM or font file.

The first drafts were drawn from memory of the classic look and matched IBM's VGA fonts glyph for glyph in many places. They were replaced by this design.

The `fonts_independent` test guards against drifting back. It compares the three fonts with a reference VGA BIOS image and fails if more glyphs match than the ceilings set per font allow: one ceiling for exact matches, and one that also counts glyphs matching the reference moved by up to two rows and one column. The image path is the CMake cache variable `VGPU98_REFERENCE_VGA_ROM`, by default 86Box's `roms/video/vga/ibm_vga.bin` under `bin/`. The image is only read, never copied, and the test is skipped when it is absent. Blank glyphs, shades, box drawing and blocks are not counted. Simple shapes such as H, o or + can come out the same in any font drawn in this style, so the test prints every identical glyph for review.
