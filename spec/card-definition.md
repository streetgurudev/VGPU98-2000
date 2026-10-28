# Card definition format

The card is described by the files in `card\`:

| File | Contents | Read by |
|---|---|---|
| `vgpu98.card` | Identity: PCI IDs, names, sizes, BIOS strings, register constants, display engine, monitor | `cardc`, `infgen` |
| `vga-modes.card` | The standard VGA video parameter table and the mode descriptors | `cardc` |
| `display-modes.card` | The extended modes ([modes.md](modes.md)) | `cardc`, `infgen` |
| `driver.card` | The Windows 98 driver package ([driver-package.md](driver-package.md)) | `infgen`, the release script |

`cardc` writes its outputs to `build\gen\`:

| Output | Used by |
|---|---|
| `card.inc` | BIOS: IDs, ROM size, debug register index, strings |
| `vgamodes.inc` | BIOS: parameter table, mode descriptors, model and palette constants |
| `palettes.inc` | BIOS: default DAC palettes |
| `vid_vgpu98_card.h` | 86Box device. Copied into `86Box\src\include\86box\` by the `update-86box-header` target; the build fails if the two copies differ. |

## Syntax

```
; comment             # also a comment
[section]             [section argument]
key = value           ; comments may follow a value
```

- Section names and keys are lower-case letters, digits, `_` and `-`. A key may appear once per section.
- **Numbers:** decimal, or hexadecimal with a `0x` prefix.
- **Strings:** double-quoted printable ASCII. Only `\"` and `\\` escapes exist.
- **Byte lists:** two-digit hex values separated by spaces or commas, without a prefix: `seq = 01 0F 00 06`.
- **Word lists and number lists:** separated by spaces or commas.
- Unknown sections and keys are errors, so a typo never passes silently.

## `vgpu98.card`

| Section | Key | Meaning |
|---|---|---|
| `[card]` | `name` | Name in 86Box's video card list |
| | `internal_name` | 86Box configuration name. Never change it after a release: saved VMs refer to it. |
| | `rom_path` | ROM location relative to 86Box's directory |
| `[pci]` | `vendor_id`, `device_id`, `revision` | PCI identity |
| | `class_code` | Must be `0x030000` (VGA-compatible display controller) |
| | `subsystem_vendor_id`, `subsystem_id` | PCI subsystem identity |
| | `bar0_size`, `bar1_size`, `rom_bar_size` | Power-of-two BAR sizes in bytes |
| `[memory]` | `vram_mb` | VRAM choices in 86Box's settings, increasing powers of two, each fitting BAR1 |
| | `vram_default_mb` | Default choice; must be in the list |
| `[bios]` | `rom_size` | ROM image size, a multiple of 512, no larger than the ROM BAR |
| | `revision` | PCI data structure "revision level of code" |
| | `version`, `vendor_name`, `product_name` | BIOS strings (`$PnP` header, sign-on) |
| | `signon`, `copyright` | Sign-on lines shown at POST, at most 78 characters |
| `[registers]` | `id_magic`, `hw_version`, `profile`, `command_set` | BAR0 identification register values ([registers.md](registers.md), [gpu-registers.md](gpu-registers.md)); `profile` is the full profile, at most 7FFFh (bits 0-14: commands, 2D, 3D, the ring and the status block in main memory, the GPU page table, the vertex pipeline, command set 2.1's DirectX 8 additions, 2.2's programs, 2.3's DOWNLOAD, the Glide and OpenGL devices, the hardware cursor, the gamma table, the V-sync overrides: [gpu-registers.md](gpu-registers.md), PROFILE), which the device reports when its host GPU starts and no setting takes a bit away. `driver.card`'s keys, the control program's `control_panel` and `control_clsid` among them, are in [driver-package.md](driver-package.md) |
| | `gpu_reserve` | Bytes at the top of video memory the drivers keep for the GPU (a multiple of 64 KB) |
| | `debug_seq_index` | Sequencer index of the developer debug register (05h-FFh) |
| `[display]` | `first_index` | First of the 64 sequencer indices holding the display engine registers; must not overlap the debug register ([display-engine.md](display-engine.md)) |
| | `key`, `id`, `revision` | Unlock value, identity and engine revision read back from those registers |
| | `max_width`, `max_height` | Largest mode the engine accepts; `max_width` a multiple of 8, at most 2048 (86Box's frame buffer), `max_height` at most 1536 (86Box counts at most 2000 lines a frame, blanking included) |
| | `refresh_min`, `refresh_max` | Allowed refresh range in Hz |
| `[monitor]` | `vendor`, `product`, `serial`, `week`, `year`, `name`, `size_mm` | The virtual monitor's EDID identity and size ([modes.md](modes.md)); `name` at most 13 characters |
| | `preferred` | `WIDTHxHEIGHT RATE`: the preferred timing; must be an enabled desktop mode and one of its rates |

The display mode table is a separate file, `display-modes.card`, described in [modes.md](modes.md).

## `vga-modes.card`

### `[param N]`, N = 00h-1Ch

One entry of the 29-entry video parameter table (layout in `vga-parameter-table.md`).

| Key | Meaning |
|---|---|
| `columns` | Text columns |
| `rows` | Text rows (stored as rows - 1) |
| `char_height` | Character height in scan lines |
| `page_size` | Bytes per display page |
| `seq` | Sequencer registers 01h-04h (4 bytes) |
| `misc` | Miscellaneous output register (1 byte) |
| `crtc` | CRT controller registers 00h-18h (25 bytes) |
| `attr` | Attribute controller registers 00h-13h (20 bytes) |
| `gc` | Graphics controller registers 00h-08h (9 bytes) |
| `reserved = yes` | Marks an unused entry, stored as 64 zero bytes. No other keys allowed. |

All 29 entries must be present.

### `[mode N]`, N = 00h-13h

| Key | Meaning |
|---|---|
| `model` | `text`, `cga4`, `cga2`, `planar`, `vga256`, or `none` for modes the card does not implement (08h-0Ch). A `none` mode has no other keys. |
| `mono` | `1` if the mode uses the monochrome ports (3B4h, 3BAh) |
| `segment` | Screen memory segment: B000h for monochrome text, B800h for colour text and CGA graphics, A000h otherwise |
| `params` | Parameter entry. Text modes list three: 200, 350 and 400 lines. |
| `palettes` | DAC palette loaded at mode set, one per `params` entry: `cga64`, `ega64`, `mono64`, `vga256` |

`cardc` cross-checks each mode against its parameter entries:
- the `mono` flag matches bit 0 of the miscellaneous output register
- the character height is 8, 14 or 16, a font the BIOS has
- the segment matches the model
- `vga256` palettes go with the `vga256` model only

## Default palettes

`cardc` generates the palettes from rules, not tables. The rules reproduce the standard VGA values; `tools\tests\test_palettes.c` spot-checks them.

| Palette | Rule (6-bit DAC values) |
|---|---|
| `ega64` | Bits 0-2 add 2Ah to blue, green, red; bits 3-5 add 15h to blue, green, red |
| `cga64` | Bits 0-2 give 2Ah to blue, green, red; bit 4 adds 15h to all three; colour 6 has green 15h (brown); bits 3 and 5 are ignored |
| `mono64` | Bits 3 and 4 both set: 3Fh; either one: 2Ah; neither: 0 |
| `vga256` | 0-15: the 16 colours; 16-31: gray ramp; 32-247: three intensities x three saturations x 24 hues; 248-255: black |
