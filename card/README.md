# card

The card definition, and the headers shared with 86Box:
- `vgpu98.card`: PCI IDs, strings, video memory sizes, the ROM, the registers;
- `vga-modes.card` and `display-modes.card`: the standard VGA modes and the extended modes;
- `driver.card`: the Windows 98 driver package;
- `vid_vgpu98_*.h`: the GPU's packets and the rules both sides share, copied unchanged into 86Box.

It is the single source of truth. `cardc` turns the definitions into the BIOS's include files, the display driver's mode table and 86Box's `vid_vgpu98_card.h`, and `infgen` writes the INF from them, so nothing is typed by hand twice. The build checks that 86Box's copies match.
