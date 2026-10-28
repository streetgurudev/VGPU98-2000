# LE format (as vglink writes it)

The linear executable format that Windows 98 loads virtual device drivers (VxDs) from. This page covers the parts `vglink` writes and `drvlint` checks, restated from Windows 98's own VxDs and published format descriptions. All values are little-endian unless stated.

## File layout

```
DOS stub (as for NE), new header offset at 3Ch
LE header (C4h bytes)
object table              }
object page map           } loader section
resident names table      }
entry table               }
fixup page table          } fixup section
fixup record table        }
(empty import module and procedure tables)
page data                 (512-byte aligned start; every page full except the last)
non-resident names table
version resource          (with [version]: a 16-bit resource record, unaligned, last in the file)
```

## LE header

Offsets from the header's start; table offsets are relative to it too, except the page data and non-resident names (file offsets).

| Offset | Field | vglink writes |
|---|---|---|
| 00h | signature | `LE` |
| 02h, 03h | byte order, word order | 0 (little-endian) |
| 04h | format level | 0 |
| 08h | CPU | 2 (80386) |
| 0Ah | OS | 4 (Windows 386: VxD) |
| 0Ch | module version | 0 |
| 10h | module flags | 38000h: a library module, type virtual device driver |
| 14h | page count | |
| 18h-27h | start object, EIP, stack object, ESP | 0 |
| 28h | page size | 1000h, as Windows 98's own VxDs |
| 2Ch | bytes in the last page | |
| 30h | fixup section size | fixup page table through the end of the fixup records |
| 38h | loader section size | object table through the end of the entry table |
| 40h, 44h | object table offset, object count | |
| 48h | object page map offset | |
| 4Ch | iterated pages | 0 |
| 50h, 54h | resource table, resource count | 0 |
| 58h | resident names table | |
| 5Ch | entry table | |
| 60h, 64h | module directives | 0 |
| 68h, 6Ch | fixup page table, fixup record table | |
| 70h, 74h | import module table, import module count | end of the fixup records, 0 |
| 78h | import procedure table | end of the fixup records |
| 80h | page data (file offset) | |
| 84h | preloaded pages | pages of the preloaded objects |
| 88h, 8Ch | non-resident names (file offset), length | |
| B8h, BCh | VxD version resource offset (file offset), length | the record's offset and length with a `[version]` section ([below](#the-version-resource)), else 0 |
| C0h | device ID | `device_id` |
| C2h | DDK version | `ddk_version` |

## Object table

24 bytes an object, numbered from 1:

| Offset | Field |
|---|---|
| 0 | virtual size |
| 4 | relocation base address: 0 (a VxD is always relocated) |
| 8 | flags: 0001h readable, 0002h writable, 0004h executable, 0010h discardable, 0020h shared, 0040h preload, 2000h 32-bit |
| 0Ch | first page map entry (1-based) |
| 10h | page count |
| 14h | the object's 4-character class (`LCOD`, `PCOD`, `ICOD`, ...), in file order |

## Object page map

4 bytes a page: a page number in three bytes, most significant first, then a flags byte (0: the page is stored in the file). Page *n* of the file starts at the page data offset + (*n* - 1) × page size. `vglink` stores every page of every object, in object order, with the map numbering them 1, 2, 3, ...; Windows' own files may skip and reorder pages, which `drvlint` accepts.

## Names and entries

Names tables as in NE: length-prefixed strings with word ordinals, ending with a zero byte. The resident table holds the module name; the non-resident table the description (ordinal 0) and the device descriptor block's name (ordinal 1, by convention `NAME_DDB`).

The entry table holds one bundle: count 1, type 3 (32-bit entry), the object number (word), then the entry: flags 03h, offset (dword); a zero byte ends the table.

## Fixups

The fixup page table has one dword per page, plus one: the offset into the fixup record table of the page's first record; the next entry marks its end.

Records written (internal references only; VxDs call each other through `INT 20h`, never through imports):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | source type: 07h 32-bit offset, 08h 32-bit self-relative |
| 1 | 1 | target flags: 00h internal reference; 10h the target offset is 32 bits; 40h the object number is 16 bits |
| 2 | 2 | source offset in the page (signed) |
| 4 | 1 or 2 | target object |
| | 2 or 4 | target offset in the object |

The loader writes the target's address (07h), or the target's address minus the address after the location (08h), into the location. A location that runs across a page boundary is recorded in both pages: in the second with a negative source offset; each page then writes its own bytes.

Windows' own VxDs also use source lists (flag 20h in the source type: a count byte instead of the offset, and the offsets after the target), which `drvlint` reads but `vglink` does not write.

## The version resource

What a VxD's Properties show on their Version tab. The layout is Windows' own VxDs' (VDD.VXD, VXDLDR.VXD and a Voodoo3's mini-VDD, read for facts). The LE resource table (50h, 54h) stays empty: the header's B8h and BCh name the resource instead, at the very end of the file, after the non-resident names, at whatever offset that is (not aligned).

| Offset | Size | Contents |
|---|---|---|
| 0 | 3 | `FF 10 00`: type 16 (RT_VERSION) as an ordinal |
| 3 | 3 | `FF 01 00`: name 1 as an ordinal |
| 6 | 2 | flags 0030h (moveable, pure), as Windows' VxDs have them |
| 8 | 4 | the data's size, in bytes |
| 12 | | the data: a 16-bit VS_VERSIONINFO, the block an NE module carries ([ne-format.md](ne-format.md)): ANSI keys and values, no type field, every block and value on a 32-bit boundary counted from the data's first byte |

BCh's length is 12 plus the data's size. In the block's fixed information the OS is 10001h (VOS_DOS_WINDOWS16) and the file type 5 (VFT_VXD); Windows' VDD.VXD gives its device ID as the subtype. `vglink` writes it from a `[version]` section ([vglink.md](vglink.md)), whose file type must be 5. `drvlint` checks the record and the block wherever B8h or BCh is set, on Windows' files too, and with `--strict --vxd`, which our build uses, requires one; some of Windows' VxDs have none (the 98 DDK's S3 sample), so loaders take a VxD either way.
