# vglink: the driver linker

`vglink` links object files into the three executable formats the VGPU98 drivers need:

- **NE**, the 16-bit Windows module format, for the display minidriver (`vgpu98.drv`), from the OMF objects NASM writes (`nasm -f obj`). Layout: [ne-format.md](ne-format.md).
- **LE**, the linear executable format, for the mini-VDD (`vgpu98.vxd`), from OMF objects likewise. Layout: [le-format.md](le-format.md).
- **PE**, a 32-bit DLL, for the DirectX driver (`vgpu98dx.dll`) and the Glide DLLs (`glide2x.dll`, `glide3x.dll`: [glide.md](glide.md)), from the pe-i386 COFF objects gcc writes with `-m32`. Layout: [pe-format.md](pe-format.md).

```
vglink -d DEFINITION -o OUTPUT [-m MAP] [-r RESOURCE_DIR] OBJECT...
```

| Option | Meaning |
|---|---|
| `-d` | The link definition (below) |
| `-o` | The output file |
| `-m` | Also write a map: output segments, every public symbol's address, imports, exports; for a PE DLL also every function of the objects by address, external and static (with its object), so an address inside the image names its function |
| `-r` | Directory for resource files (default: the definition file's directory) |

The output depends only on the inputs: no timestamps, no file paths, no host details. Objects are read in command-line order.

## The link definition

An INI-style file in the project's usual format ([card-definition.md](card-definition.md)): `;` comments, `[section]` headers, `key = value` lines, and every key must be used. Names (symbols, segments, modules, files) may hold letters, digits and `_ . $ ? @ -`.

### `[module]`

| Key | Format | Meaning |
|---|---|---|
| `format` | `ne`, `le` or `pe` | Output format |
| `name` | up to 8 characters (NE, LE); a file name (PE) | Module name, written in upper case for NE and LE. A display driver's is `DISPLAY`; a PE DLL's is its file name, as in its export directory |
| `description` | quoted string | First entry of the non-resident names table |
| `entry` | symbol (NE, PE) | The library's initialization routine (CS:IP), or the DLL's entry point (DllMain) |
| `base` | number (PE) | The image base, a multiple of 64 KB |
| `shared` | 0 or 1 (PE, default 1) | 1: every section shared, one copy for every process (a DLL in Windows 98's shared arena: the DirectX driver); 0: no section shared, the data per process (the Glide DLLs) |
| `data` | output segment (NE) | The automatic data segment: makes the module SINGLEDATA |
| `heap` | number (NE, default 0) | Local heap size |
| `windows` | number (NE, default 400h) | Expected Windows version |
| `ddb` | symbol (LE) | The device descriptor block, exported as ordinal 1 under this name |
| `device_id` | number (LE, default 0) | Device ID in the header |
| `ddk_version` | number (LE, default 0) | DDK version in the header (40Ah for Windows 98) |

### `[segment NAME]` (NE) and `[object TAG]` (LE)

One section per output segment or object, in file order.

| Key | Meaning |
|---|---|
| `segments` | The OMF segments placed in it, in order (default: the section's own name). Same-named segments from several objects are placed one after another in object order, each at its own alignment. Every segment of every object must be placed exactly once. |
| `type` (NE) | `code` or `data` |
| `preload` (NE) | 1 (default) to load with the module, 0 to load on first use |
| `flags` (LE) | Any of `preload`, `discardable`, `writable`, `shared`. Objects are always readable, executable and 32-bit. Preloaded objects come first. |

An LE object's `TAG` is its 4-character class, written into the object table as Windows' own VxDs do: `LCOD` (locked code and data), `PCOD` (pageable), `ICOD` (initialization, discardable), and so on.

NE segments are fixed (never moved by Windows), as a display driver's must be; NE output takes 16-bit segments, LE output 32-bit ones.

### `[exports]` (NE)

One line per export: `ordinal = symbol [resident] [nodata]`.

- The name in the module's tables is the symbol in upper case.
- `resident` puts the name in the resident names table (looked up often); the others go to the non-resident table.
- Entries are exported and, in a module with an automatic data segment, marked as using it (the loader sets DS on entry). `nodata` leaves that out.

### `[imports MODULE]` (NE)

One section per imported module, one line per import: `ordinal = symbol`. Imports are by ordinal only; the symbol is the name the objects use (`extern`). NASM's own `import`/`export` directives are rejected.

### `[imports DLL]` and `[exports]` (PE)

A PE link has no segment or object sections: the layout is fixed ([pe-format.md](pe-format.md)).

`[imports DLL]` has one key, `symbols`: stdcall symbols separated by spaces, imported under their undecorated names: in `[imports KERNEL32.dll]`, `__imp__Sleep@4` imports `Sleep` (gcc's name for the import's address slot).

`[exports]` takes `symbols` or `file`, never both, and `names`:

| Key | Meaning |
|---|---|
| `symbols` | Stdcall symbols separated by spaces (at most 256) |
| `file` | A list file in the definition's directory: one symbol a line (at most 1024), of the name characters above; blank lines and lines starting with `;` or `#` are ignored. A list with no symbols, a NUL byte or a UTF-8 byte order mark is an error |
| `names` | `undecorated` (the default): each symbol is exported under its undecorated name, `_DriverInit@4` as `DriverInit` (a symbol not of the form `_name@n` is an error). `decorated`: under its name as written, `_grDrawTriangle@12` as `_grDrawTriangle@12`, as a Glide DLL's table has them |

Every exported symbol must be defined by the objects. With `names = decorated` the exported name is the symbol itself, so a function whose stdcall decoration (its arguments' bytes) differs from the name's fails the link (`export` name`: symbol` name `is not defined`). A name exported twice is an error. An error about an export from a list file gives the list's path and the name's line in it (`path:line: ...`); one from `symbols`, the definition's line.

### `[resource TYPE ID]` (NE)

`TYPE` and `ID` are numbers (1-7FFFh) or names (written in upper case). Keys: `file` (read from the resource directory), `preload` (default 1).

### `[version]` (NE, LE, PE)

Builds a version information resource (type 16, ID 1): for NE in the 16-bit format Windows 98 reads for drivers ([ne-format.md](ne-format.md)); for LE the same 16-bit block in a resource record at the end of the file, which the header's B8h and BCh name, as Windows' own VxDs carry theirs ([le-format.md](le-format.md), The version resource), its `file_type` 5 (a VxD) and nothing else; for PE the 32-bit format (VS_VERSIONINFO, UTF-16 strings) in a `.rsrc` section of its own ([pe-format.md](pe-format.md)), which Windows shows on the file's Version tab. A PE DLL without `[version]` has no `.rsrc` section; in a PE definition the section takes no name (`[version]`, not `[version NAME]`).

| Key | Meaning |
|---|---|
| `file_version`, `product_version` | Four numbers up to 65535, as in `4.10.0.1` |
| `file_type`, `file_subtype` | VS_FIXEDFILEINFO type (1-7) and subtype (default 0): 3 and 4 for a display driver, 3 and 8 (an installable driver) for a Glide DLL, the OpenGL ICD and the DirectX driver DLL, 2 and 0 (a DLL) for the control program, 5 and the device ID for a VxD |
| `company`, `description`, `file_version_text`, `internal_name`, `copyright`, `original_name`, `product_name`, `product_version_text` | The string table entries, in both formats U.S. English with code page 1252, Windows Latin-1 (`040904E4`) |

## Fixups

What a PE link does with COFF relocations is in [pe-format.md](pe-format.md). What NASM's OMF fixups become in NE and LE output:

| In the object | NE output | LE output |
|---|---|---|
| 16-bit offset of a symbol | resolved by the linker, relative to the output segment | error |
| segment base (`seg x`, a segment name) | segment relocation (the loader writes the selector) | error |
| far pointer (offset and base fixups at consecutive words) | the offset resolved, a segment relocation for the base; to an import, one far-address relocation | error |
| 16-bit offset of an import | offset relocation | error |
| 32-bit offset | resolved by the linker | fixup to the target object and offset (the loader writes the address) |
| self-relative (near call or jump) | resolved within one output segment; across segments, an error | resolved within one object; across objects, a self-relative fixup |

Everything else is an error with the object, segment and offset: byte locations, group frames in NE output, imports plus offsets, 32-bit offsets to imports, near references between NE segments, absolute segments and symbols.

Non-additive NE relocations leave FFFFh in the location, the end of the loader's relocation chain, so each relocation patches exactly its own location.
