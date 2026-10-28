# The PE DLL format (as vglink writes it)

The DirectX driver, `vgpu98dx.dll` ([directx-hal.md](directx-hal.md)), and the Glide DLLs, `glide2x.dll` and `glide3x.dll` ([glide.md](glide.md)), are 32-bit Portable Executable DLLs. `vglink` writes them from the pe-i386 COFF objects gcc produces with `-m32` ([vglink.md](vglink.md)); `drvlint --pe` checks the DirectX driver, `drvlint --glide` the Glide DLLs and `drvlint --icd` the OpenGL ICD, `vgpu98gl.dll` ([opengl.md](opengl.md)). This page fixes the subset of the format vglink writes and what Windows 98's loader needs of it. It is restated from Microsoft's PE/COFF specification and checked against DLLs that Windows 98 loads; facts only.

## What Windows 98 needs

Two kinds of DLL, set by the link definition's `shared` key:

- **The DirectX driver: a DLL in the shared arena.** DirectDraw's helper process (DDHELP) loads the DLL, and the DirectDraw and Direct3D callbacks then run in whichever process calls DirectDraw. Windows 98 loads a DLL whose image base is in the shared system arena (80000000h-BFFFFFFFh) once, at that address, visible to every process. The image base is B00B0000h, the DDK's convention for such drivers. With the shared flag on every section, the DLL's data is one copy for all processes: the driver's state (contexts, texture handles, the ring position) is shared as it must be.
- **The Glide DLLs: per-process DLLs.** A program loads Glide into its own process, and Glide's state is that program's. The image base is 10000000h, in the process's own space (400000h-7FFFFFFFh), as a Voodoo3's Glide DLLs have it; no section is shared, so each process that loads the DLL has its own copy of its data. What processes share is the display driver's ([glide.md](glide.md)).

For both:

- **Base relocations** are present, so the loader can move the image if the address is taken.
- **Imports by name** from Windows' own DLLs: KERNEL32 only for the DirectX driver; KERNEL32, GDI32, USER32 and DDRAW for the Glide DLLs ([glide.md](glide.md), Per-process DLLs). Neither links a C library.
- **The entry point** is DllMain (`_DllMain@12`), called with the process attach and detach notifications.
- **Exports by name.** The DirectX driver's are undecorated names of stdcall functions: `DriverInit` for `_DriverInit@4`. The Glide DLLs' are decorated, the symbols as written (`_grDrawTriangle@12`): the names Glide programs import.
- **Version information** (the Glide DLLs): a 32-bit version resource, which Windows shows on the file's Version tab and compares with an installed copy's before Setup replaces it.

## Layout

| Part | Contents |
|---|---|
| MS-DOS header and stub | The same stub the NE and LE files have (it prints a line and exits); `e_lfanew` (90h) points to the PE header |
| PE signature | `PE\0\0` |
| COFF header | Machine 14Ch (i386), 3 sections (4 with a version resource), time stamp 0, optional header size E0h, characteristics 210Eh (executable, no line numbers, no local symbols, 32-bit, DLL) |
| Optional header | PE32 (10Bh), linker version 1.0, section alignment 1000h, file alignment 200h, OS and subsystem version 4.0, subsystem 2 (Windows GUI), stack and heap 1 MB reserved and 4 KB committed, 16 data directories |
| Section table | `.text`, `.data`, `.rsrc` (with a version resource), `.reloc` |
| `.text` | Code and read-only data; flags 60000020h (code, execute, read) |
| `.data` | Initialized data, the import tables and the export directory, then BSS and common symbols as zeros the file does not hold (the section's virtual size covers them); flags C0000040h (initialized data, read, write) |
| `.rsrc` | The resource directory and the version resource (below); flags 40000040h (initialized data, read) |
| `.reloc` | Base relocations; flags 42000040h (initialized data, discardable, read) |

In a shared DLL every section's flags also have 10000000h (shared): 70000020h, D0000040h, 50000040h, 52000040h.

The headers (the stub, the PE and COFF headers, the optional header and the section table) are rounded up to the file alignment: 200h bytes with three sections, 400h with four. Each section starts on a 200h boundary in the file and a 1000h boundary in memory, `.text` at RVA 1000h, the others after it in the table's order.

Data directories used: 0 (exports), 1 (imports), 2 (resources, with `.rsrc`), 5 (base relocations), 12 (the import address table). The others are 0.

### Imports

One import descriptor per DLL and a zero descriptor; for each DLL an import lookup table and an import address table (one slot per function and a zero), then the hint/name entries (hint 0, the name) and the DLL names. A reference to an import in the code is a reference to its IAT slot: gcc calls a `__declspec(dllimport)` function through `__imp__Name@n`, which vglink resolves to that slot.

### Exports

One export directory: ordinal base 1, the names sorted in byte order (the loader searches them by binary search), the ordinal table in the same order, the DLL's name.

### Resources

`.rsrc` holds one resource, the version information (type 16, RT_VERSION; ID 1; language 409h, U.S. English). Offsets from the section's start:

| Offset | Contents |
|---|---|
| 0 | The root directory: characteristics, time stamp and version 0, no named entries, one ID entry: type 16, its directory at 24 (80000018h, the high bit marking a directory) |
| 24 | The type's directory, likewise: ID 1, its directory at 48 (80000030h) |
| 48 | The ID's directory: language 409h, the data entry at 72 |
| 72 | The data entry: the data's RVA (the section's RVA + 88), its size, code page 0, a reserved 0 |
| 88 | The version resource |

Each directory is a 16-byte table and its one 8-byte entry. Every time stamp is 0, so the output depends only on its inputs.

### Version information

The 32-bit VS_VERSIONINFO layout: nested blocks of { length word, value length word, type word (1 text, 0 binary), the key in UTF-16 with its terminator, padding to 4 bytes, value, padding, children }, every block and value on a 4-byte boundary from the resource's start. A block's length runs to its last byte, the padding after it not counted.

- The root, `VS_VERSION_INFO` (binary, value length 52), holds VS_FIXEDFILEINFO: signature FEEF04BDh, structure version 1.0, file and product versions, flags mask 3Fh, flags 0, OS 00010004h (VOS_DOS_WINDOWS32: 32-bit Windows on MS-DOS, as Windows 95 and 98), type, subtype, no date.
- `StringFileInfo` (text) with one table `040904E4` (U.S. English, code page 1252, Windows Latin-1): `CompanyName`, `FileDescription`, `FileVersion`, `InternalName`, `LegalCopyright`, `OriginalFilename`, `ProductName`, `ProductVersion`, each a text block whose value is UTF-16 with its terminator and whose value length is in characters, the terminator included.
- `VarFileInfo` (text) with `Translation` (binary, value length 4): 0409h, 04E4h.

The language and code page are those of a Voodoo3's Glide DLLs and Windows 98's own files (and of the NE drivers' version information): Windows' installer counts another language or code page as a conflict when it replaces a file.

The 16-bit layout NE drivers carry is in [ne-format.md](ne-format.md).

### Base relocations

Every 32-bit absolute address in the image (a COFF `DIR32` relocation) gets a HIGHLOW (type 3) entry. Blocks cover one 4 KB page each, in ascending order, padded with a type 0 entry to a multiple of 4 bytes.

## What vglink accepts

From each COFF object, `vglink` reads the sections, the symbol table and the relocations:

- `.text*`, `.rdata*`, `.rodata*` and code sections go to `.text`; everything else that is kept goes to `.data`. Sections marked for removal and `.debug*`, `.drectve`, `.comment`, `.note*` are dropped.
- Unwind tables (`.eh_frame`, `.pdata`, `.xdata`) are rejected: compile with `-fno-asynchronous-unwind-tables`. Constructor, destructor and TLS sections are rejected: the DLL has no C runtime.
- Relocation types `DIR32` (an absolute address: the image base is added and a base relocation recorded), `DIR32NB` (an RVA) and `REL32` (relative to the end of the field). Others are an error.
- Undefined symbols, symbols defined twice and exports or an entry point that are not defined are errors.

## drvlint --pe, --glide, --icd and --exports

For every PE file `drvlint` checks the headers above (machine, PE32, alignments, a 64 KB-aligned base, version 4.0 OS and subsystem), sections inside the file, aligned and not overlapping, the entry point inside the image, the export names sorted, the imports, and the base relocation blocks well formed and pointing into sections. An export directory outside the sections' file data is an error and is not read, and one claiming more names than the file could hold is an error. With `--strict` the version numbers must be exactly 4.0.

| Option | Also required |
|---|---|
| `--pe` (the DirectX driver) | A DLL with its base in the shared arena (80000000h-BFFFFFFFh), every section shared, `DriverInit` among the exports, every import from KERNEL32; base relocations under `--strict` |
| `--glide` (a Glide DLL) | A DLL with its base in a process's own space (400000h-7FFFFFFFh), no section shared, an export directory, every import from Windows' own KERNEL32, USER32, GDI32, ADVAPI32 or DDRAW (the DLLs a Voodoo3's Glide imports from), base relocations (10000000h is a base often taken), a version resource |
| `--icd` (the OpenGL ICD) | `--glide`'s checks, for a DLL that is also its process's own (the messages name it the OpenGL ICD) |

`--display`, `--vxd`, `--pe`, `--glide` and `--icd` exclude each other.

`--exports LIST` (a PE file only): the export table's names must be exactly the list's, a file in vglink's list format (one name per line; blank lines and lines starting with `;` or `#` ignored; a NUL byte or a UTF-8 byte order mark refused). Each name missing from the table and each one not in the list is an error, the first 20 of each named, then the totals. The build checks each Glide DLL with `--strict --glide --exports` and its list ([glide.md](glide.md), The export lists), and the OpenGL ICD with `--strict --icd --exports opengl/vgpu98gl.exports` (its 18 Drv* exports: [opengl.md](opengl.md)).

**The version resource** of any PE file that has one is checked and shown: the resource directory's type 16 entry is followed down its first entry at each level to the data entry, whose data must lie inside a section; the resource must be a 32-bit VS_VERSIONINFO with its fixed information (key `VS_VERSION_INFO`, value length 52, signature FEEF04BDh) and well-formed blocks, aligned from the resource's start. drvlint prints the file and product versions, the OS, type and subtype, each string table's key (`string table 040904E4`) and strings (`CompanyName: VGPU98 Project`), and each translation (`Translation: 0409 04E4`). A string whose value fills its block without a terminator is accepted, as Windows accepts it; `--strict` refuses it, and also requires each string's value length to be its characters with the terminator and its type to be 1 (text), as vglink writes them. Under `--glide` and `--icd`, a file without a version resource is an error.
