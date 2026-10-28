# ROM layout

The VGA BIOS is a 32 KB PCI expansion ROM (`[bios] rom_size` in `card\vgpu98.card`), built from `bios\vgpu98.asm`.

## Build chain

| Step | Tool | Result |
|---|---|---|
| 1 | `cardc`, `fontc` | Generated include files in `build\gen\` |
| 2 | `nasm -f bin` | Raw image and listing (`build\rom\vgpu98.lst`) |
| 3 | `romfix --card` | Pads the image to the ROM size with zeros and sets the last byte so all bytes sum to 0 |
| 4 | `romlint --card` | Checks the finished image; the build fails on any error |
| 5 | `manifest` | `build\rom\manifest.txt`: SHA-256 of every input and output, plus tool versions |

`scripts/repro.sh` builds twice in differently named directories and requires identical outputs.

## Header

| Offset | Size | Contents |
|---|---|---|
| 0000h | 2 | 55h AAh |
| 0002h | 1 | Size in 512-byte units |
| 0003h | 3 | `jmp near` to the initialization entry. The system BIOS far-calls offset 3 after copying the ROM to C0000h. |
| 0006h | 18 | Zero |
| 0018h | 2 | Offset of the PCI data structure |
| 001Ah | 2 | Offset of a `$PnP` expansion header: 0, none |
| 001Ch | 2 | Zero |
| 001Eh | 18 | `IBM VGA Compatible`: a compatibility signature some software checks to detect a VGA BIOS |
| last byte | 1 | Checksum (set by `romfix`) |

There is no `$PnP` header. It only matters for boot devices; the PCI data structure carries the card's identity. `romlint` still validates one if a future version adds it.

## PCI data structure (PCI 2.2 format)

| Offset | Size | Value |
|---|---|---|
| 00h | 4 | `PCIR` |
| 04h | 2 | Vendor ID (from the card definition) |
| 06h | 2 | Device ID |
| 08h | 2 | 0: no vital product data |
| 0Ah | 2 | 18h: structure length |
| 0Ch | 1 | 0: structure revision |
| 0Dh | 3 | Class code: programming interface, sub-class, base class (00h 00h 03h) |
| 10h | 2 | Image length in 512-byte units |
| 12h | 2 | Revision level of code (`[bios] revision`) |
| 14h | 1 | 0: x86 code |
| 15h | 1 | 80h: last image |
| 16h | 2 | 0 |

The structure is 4-byte aligned. `romlint` checks every field against the card definition.

## Rules for the code

- **Shadow-safe:** the ROM runs from shadow RAM that the system BIOS may write-protect, so the BIOS keeps no writable data inside its own image. All state lives in the BIOS data area (segment 40h).
- **Label arithmetic:** NASM label values are not plain numbers. Write `label - $$` when a label is used in arithmetic.
