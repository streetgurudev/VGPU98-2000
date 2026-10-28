# NE format (as vglink writes it)

The 16-bit Windows executable format, used for the display minidriver. This page covers the parts `vglink` writes and `drvlint` checks, restated from Windows' own files (DIBENG.DLL, display drivers) and published format descriptions. All values are little-endian.

## File layout

```
DOS stub (MZ header + a program that prints a line), offset 40h of the
  header holding the NE header's file offset
NE header (40h bytes)
segment table
resource table          (absent when there are no resources)
resident names table
module reference table
imported names table
entry table
non-resident names table
segment data, each segment followed by its relocation records   } 16-byte
resource data                                                    } aligned
```

Offsets of the tables in the header are relative to the NE header, except the non-resident names table's, which is a file offset. Segment and resource data start on 16-byte boundaries (alignment shift 4); their table entries give positions in 16-byte units.

## DOS stub

A 64-byte MZ header (relocation table offset 40h, which marks a new-format executable; new header offset at 3Ch) and a few bytes of code that print one line and exit with code 1.

## NE header

| Offset | Size | Field | vglink writes |
|---|---|---|---|
| 00h | 2 | signature | `NE` |
| 02h | 1+1 | linker version, revision | 5, 0 |
| 04h | 2 | entry table offset | |
| 06h | 2 | entry table length | |
| 08h | 4 | file CRC | 0 (Windows does not check it) |
| 0Ch | 2 | module flags | 8000h library + 0300h Windows API + 0001h single data (with an automatic data segment): 8301h |
| 0Eh | 2 | automatic data segment number | from `data`, or 0 |
| 10h | 2 | local heap size | `heap` |
| 12h | 2 | stack size | 0 (a library has none) |
| 14h | 4 | CS:IP | `entry`: segment number in the high word, offset in the low |
| 18h | 4 | SS:SP | 0 |
| 1Ch | 2 | segment count | |
| 1Eh | 2 | module reference count | |
| 20h | 2 | non-resident names table length | |
| 22h-2Ah | 2 each | segment, resource, resident names, module reference, imported names table offsets | |
| 2Ch | 4 | non-resident names table (file offset) | |
| 30h | 2 | movable entry count | 0 |
| 32h | 2 | alignment shift | 4 |
| 34h | 2 | resource segment count | 0 (OS/2 only) |
| 36h | 1 | target OS | 2: Windows |
| 37h | 1 | other flags | 0: no fast-load area |
| 38h-3Dh | 2 each | fast-load area offset and length, minimum code swap area | 0 |
| 3Eh | 2 | expected Windows version | `windows`: 0400h |

## Segment table

8 bytes a segment, numbered from 1:

| Offset | Field |
|---|---|
| 0 | file position, in alignment units |
| 2 | length in the file (0 means 64 KB) |
| 4 | flags |
| 6 | allocation size (0 means 64 KB) |

Flags written: 0001h data segment, 0020h pure (shareable code, shared data), 0040h preload, 0100h has relocation records, 0C00h descriptor privilege bits (as Windows' tools set). Segments are fixed (0010h, moveable, is never set). Empty segments are refused, since a length of 0 means 64 KB.

## Relocation records

After a segment's data: a word count, then 8-byte records.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | source type: 2 segment (selector), 3 far address (offset and selector), 5 offset |
| 1 | 1 | flags: 0 internal reference, 1 import by ordinal; bit 2 additive (not used) |
| 2 | 2 | location in the segment |
| 4 | 4 | internal: segment number (byte), 0, offset (word); import: module index (1-based), ordinal |

A non-additive relocation's location holds the offset of the next location to patch the same way, ending with FFFFh. `vglink` writes one record per location and FFFFh in each.

## Names tables

Length-prefixed strings, each followed by a word ordinal; a zero length byte ends the table.

- Resident names: the module name (ordinal 0), then exports marked `resident`.
- Non-resident names: the description (ordinal 0), then the other exports.

Export names are in upper case, as Windows' resource and module tools write them.

## Module references and imported names

The module reference table holds one word per imported module: the offset of its name in the imported names table. The imported names table starts with an empty name (a zero byte), then the length-prefixed module names.

## Entry table

Bundles of consecutive ordinals, from ordinal 1:

- A count byte (0 ends the table) and a type byte. Type 0 skips `count` ordinals (no bytes follow); type 1-FEh is a fixed segment number, followed by `count` 3-byte entries: flags, then the offset.
- Entry flags: 01h exported, 02h the entry loads the automatic data segment (the module's shared data).

Moveable entries (type FFh, with INT 3Fh thunks) are not written.

## Resource table

| Field | Size |
|---|---|
| alignment shift | 2 |
| per type: type ID (8000h + number, or the offset of a name), count, reserved | 2 + 2 + 4 |
| per resource: file position, length (both in alignment units), flags, ID (8000h + number, or a name offset), 2 reserved words | 12 |
| end of types | 2 (0) |
| names (length-prefixed, offsets from the table's start), then a zero byte | |

Resource flags written: 0010h moveable, 0020h pure, 0040h preload (unless `preload = 0`), 0C00h as Windows' resource compiler sets.

## Version information

Type 16 (RT_VERSION), ID 1, in the 16-bit layout: nested blocks of { length word, value length word, NUL-terminated key, padding to 4 bytes, value, padding, children }. The root `VS_VERSION_INFO` holds the fixed information (signature FEEF04BDh, structure version 1.0, file and product versions, flags mask 3Fh, OS 00010001h = 16-bit Windows on DOS, type, subtype, no date), then `StringFileInfo` with one table `040904E4` (U.S. English, Windows Latin-1) and `VarFileInfo` with `Translation` = 0409h, 04E4h.
