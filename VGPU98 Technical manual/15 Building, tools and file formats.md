# 15 Building, tools and file formats

How the project is built from source: the toolchain, the two repositories, the project's own tools, the file formats they write, how builds stay reproducible, the tests, and the clean-room rule. The README has the short version of the build commands. The reference is [`spec/vglink.md`](../spec/vglink.md), [`spec/ne-format.md`](../spec/ne-format.md), [`spec/le-format.md`](../spec/le-format.md), [`spec/pe-format.md`](../spec/pe-format.md), [`spec/card-definition.md`](../spec/card-definition.md), [`spec/driver-package.md`](../spec/driver-package.md) and [`tools/README.md`](../tools/README.md).

## The toolchain

MSYS2's UCRT64 environment on Windows. `toolchain.lock` pins the versions, and every build's manifest records them.

| Package | Version | For |
|---|---|---|
| gcc | 16.2.0-4 | The tools, the tests, the 32-bit guest DLLs (`-m32`), and 86Box |
| cmake | 4.4.4-3 | |
| ninja | 1.13.2-1 | |
| nasm | 3.02-1 | The BIOS, the display driver, the mini-VDD |
| glslang | 16.3.0-1 | 86Box's shaders, compiled to SPIR-V at build time |
| vulkan-headers | 1.4.363.0-1 | 86Box. There is no link-time Vulkan loader |
| zlib | 1.3.2-2 | `zipgen`'s deflate, linked statically |
| unicorn | 2.1.4-5 | Optional: the tests that run guest code on the host |
| pkgconf, qt5-translations, llvm-openmp, and the 29 libraries 86Box links | in `toolchain.lock` | 86Box's own build, as 86Box's MSYS2 workflow installs them; the libraries' licences are in `package/release/THIRD-PARTY-NOTICES.txt` |

Everything in `toolchain.lock` installs with `pacman -S --needed` and the package names. 86Box builds with the same GCC and a static Qt 5. The project builds with `-Wall -Wextra -Wpedantic -Werror`, and the guest DLLs with `-Wall -Wextra -Werror`. Build from a UCRT64 login shell; adding UCRT64 to another shell's PATH breaks CMake's checks.

## The two repositories

```
 VGPU98-2000 (the project)              86Box/: 86Box-VGPU98, a submodule
 -------------------------              --------------------------------
 bios/ driver/ minivdd/ driver32/        branch vgpu98: upstream 86Box
 glide/ opengl/ cpl/ card/ fonts/        plus the card's own files
 package/ tools/ cmake/ scripts/         src/video/vid_vgpu98*.c
 spec/ VGPU98 Technical manual/          src/video/vgpu98/ (shaders)
                                         src/include/86box/vid_vgpu98*
        |                                         ^
        | card/*.card --cardc--> vid_vgpu98_card.h |
        | card/vid_vgpu98_*.h (six) -- copied ---->+
        |   the build checks the copies match
        v
 build/  dist/  bin/86Box.exe  bin/roms/
```

- **`86Box/`** is the fork, [86Box-VGPU98](https://github.com/streetgurudev/86Box-VGPU98), as a git submodule of [VGPU98-2000](https://github.com/streetgurudev/VGPU98-2000): `git clone --recursive https://github.com/streetgurudev/VGPU98-2000.git` fetches both. The project records which of the fork's commits goes with each of its own, so after a commit in `86Box/` the project records it too (`git add 86Box`, then commit); `release.sh` refuses to run until the two agree.
- **`scripts/setup-86box.sh`** prepares the clone: the branch `vgpu98`, a fetch-only `upstream` remote for the official 86Box repository, a pre-push hook that refuses pushes there whatever the remote is called, and the `release` build preset. `origin` is the fork.
- **The release preset** (`86Box/CMakeUserPresets.json`, from `scripts/86box/`) inherits upstream's `regular` preset and writes `86Box.exe` into the project's `bin/`, beside `bin/roms`, so cleaning a build never touches the ROMs.
- **The card header.** When `card/vgpu98.card` or a shared header changes, the root build stops ("does not match") until `cmake --build build --target update-86box-header` copies the new files into the clone; then they are committed there too.

## Building

```sh
git clone --recursive https://github.com/streetgurudev/VGPU98-2000.git
cd VGPU98-2000
scripts/setup-86box.sh            # once
cmake --preset default            # once
cmake --build --preset default    # tools, BIOS, drivers, the package
ctest --preset default            # the host-side tests
cd 86Box
cmake --preset release            # once
cmake --build --preset release    # bin/86Box.exe
```

When filtering build output for problems, look for `error`, `warning`, `FAILED` and `stopped`: a stopped build otherwise looks clean.

## The project's own tools (`tools/`)

```
 card/vgpu98.card ---------+
 card/display-modes.card --+--cardc--> BIOS includes, the driver's mode
 card/vga-modes.card ------+           table, vid_vgpu98_card.h
 fonts/*.fnt ----------------fontc---> font includes (and a preview PNG)
 driver32/shared.h ----------sbgen---> shared.inc for the 16-bit driver

 bios/*.asm --NASM--> romfix (pad, checksum) --> romlint --> vgpu98.bin
                                                  biostest (Unicorn)
 driver/*.asm   --NASM (OMF)--> vglink --> vgpu98.drv (NE) --> drvlint
 minivdd/*.asm  --NASM (OMF)--> vglink --> vgpu98.vxd (LE) --> drvlint
                                           drvtest, vxdtest (Unicorn)
 driver32/ glide/ opengl/ cpl/ --GCC -m32 (COFF)--> vglink --> PE DLLs
                                                               drvlint
 card/driver.card, vgpu98.card, display-modes.card --infgen--> vgpu98.inf
 the eight files --isogen--> vgpu98-driver.iso (read back and checked)
 everything --manifest--> SHA-256 and size of every input and output
```

| Tool | What it does |
|---|---|
| `cardc` | The card compiler: reads the card definition and the mode tables; writes the BIOS's includes, the driver's mode table and 86Box's card header |
| `fontc` | Compiles the text-art fonts into NASM includes |
| `sbgen` | Writes the shared block's layout as a NASM include for the 16-bit driver |
| `vglink` | The linker: NASM's OMF objects into NE (the display driver) or LE (the VxD); GCC's pe-i386 COFF objects into PE DLLs; version resources for all three |
| `drvlint` | Structural checks of NE, LE and PE files, written apart from vglink so it checks vglink's output; it also passes Windows' own files in the tests |
| `romfix`, `romlint` | Pad the ROM and set its checksum; check the finished ROM against the card definition |
| `infgen` | Writes the INF from `driver.card`, `vgpu98.card` and `display-modes.card` |
| `isogen` | Writes the CD image (ISO 9660 level 1, padded after the last file), then checks it by reading it back |
| `mkdisk` | Writes a 1.44 MB floppy image; kept, not used by the package |
| `zipgen` | Writes the release zip: deflated with zlib, entries in the order given, every date the build time, then read back and every entry compared with its source |
| `manifest` | Records every input and output's SHA-256 and size, and the tool versions, with nothing time-dependent |
| `biostest` | Runs the BIOS on the host under Unicorn, with a VGA model of its own |
| `drvtest` | Runs `vgpu98.drv` under Unicorn, with stand-ins for KERNEL, DIBENG, USER and the VDD |
| `vxdtest` | Runs `vgpu98.vxd` under Unicorn, with stand-in VMM and VDD services |
| `common/` | Shared code: the configuration reader, EDID, modes, SHA-256 |

## The file formats

| Format | Files | Notes |
|---|---|---|
| **NE**, 16-bit Windows modules | `vgpu98.drv` | Fixed segments, exports by ordinal, imports from KERNEL, DIBENG, USER and the rest, resources (fonts, the colour table, version information in the 16-bit layout) |
| **LE**, linear executables | `vgpu98.vxd` | Objects by class (LCOD, PCOD, ICOD...), the device descriptor block exported as ordinal 1, fixups; the version resource as Windows' own VxDs carry it, a record at the end of the file named by the header |
| **PE**, 32-bit DLLs | `vgpu98dx.dll`, `glide2x.dll`, `glide3x.dll`, `vgpu98gl.dll`, `vgpu98cp.cpl` | A fixed section layout. The DirectX DLL's sections are all shared (one copy in Windows 98's shared arena); the Glide DLLs, the ICD and the control program have per-process data. Version information in its own `.rsrc` section |
| **Link definitions** (`*.lnk`) | One per driver file | An INI-style file: the module, segments or objects, exports, imports, `[version]` |
| **The card definition** (`card/*.card`) | The card, its modes, the package | INI-style; every key must be used |
| **The INF** | `vgpu98.inf` | Windows 98's format, written by infgen |
| **The CD image** | `vgpu98-driver.iso` | ISO 9660 level 1, padded with 150 sectors after the last file |
| **The pipeline cache** | `vgpu98-pipelines.bin` | A self-checking file of Vulkan's data and the key list ([`spec/pipeline-cache.md`](../spec/pipeline-cache.md)) |

## Releasing and reproducibility

`scripts/release.sh` builds everything needed to test, and never starts 86Box. It needs `git` on the shell's PATH.

1. Checks that both repositories have no uncommitted changes, and that `bin/86Box.exe` is not running.
2. Deletes both build trees and builds from clean: the tools, the generated files, the ROM, the drivers, the package and the manifest.
3. Runs the tests.
4. Checks that the 86Box clone carries the current card header.
5. Copies the ROM to `bin/roms/video/vgpu98/vgpu98.bin`.
6. Builds 86Box's release preset and checks `bin/86Box.exe` is new.
7. Copies the package to `dist/vgpu98-driver/` and `dist/vgpu98-driver.iso`.
8. Writes the release zip, `dist/VGPU98-2000-<version>-win64.zip`, for people who do not build: `86Box.exe`, the ROM, the CD image, a quick start (`package/release/README.txt`), the licences and the third-party notices (`package/release/THIRD-PARTY-NOTICES.txt`, written by `scripts/third-party-notices.sh` from the 86Box build's link line and MSYS2's licence files), `SOURCE.txt` naming both repositories' tags and commits, and `vgpu98.patch`, the card's changes against 86Box's base (`scripts/86box/base`).
9. Writes `build/release.txt`: both commits, the build stamp, and the SHA-256 of the ROM, every driver file, the CD image and the zip.

**With `--repro`** it also builds the ROM and the package a second time, from a copy of the tree at a different, longer path, and requires identical bytes. The zip is the same for the same files, but `86Box.exe` itself carries its link time, so a rebuilt exe makes a new zip. Builds are reproducible because:
- the generators use integer arithmetic and write no timestamps or paths;
- the build stamp and time come from the commit (`cmake/build-stamp.cmake`), with `-dirty` when tracked files have changed;
- `.gitattributes` keeps LF line ends, so the sources hash the same on every machine.

## The tests

223 tests, run by `ctest`. Five of them check `drvlint` against Windows' and the Voodoo3's own driver files, read in place from a local reference library (`VGPU98_REFERENCE_LIBRARY`, by default under `archive/`); without it they skip themselves and 218 run.

| Group | Count | Covers |
|---|---|---|
| `ogl_*` | 53 | The OpenGL ICD on the host: state, drawing, textures, extensions, swap intervals, V-sync |
| `cardc_*` | 39 | The card compiler, its outputs, determinism and bad inputs |
| `glide2x_*`, `glide3x_*`, `glide_*` | 31 | The Glide DLLs' rules: coordinates, TMUs, palettes, the frame buffer |
| `fontc_*`, `fonts_independent` | 20 | The font compiler; the fonts' own design |
| `romlint_*`, `bios_on_host` | 19 | The ROM checks; the BIOS under biostest |
| `vglink_*` | 16 | The linker: NE, LE and PE output, determinism, bad definitions |
| `drvlint_*` | 12 | The checker, on the project's files and on Windows' |
| `infgen_*`, `cfg_*` | 14 | The INF; the configuration reader |
| `zipgen_*` | 2 | The release zip: the same archive twice, and no name that leaves its folder |
| Others | 17 | `driver_on_host`, `minivdd_on_host`, `display_diagnostics`, `gamma_ramp`, `heap_allocator`, `palettes_standard_values`, `vertex_declarations`, `shader_translator`, `edid_and_modes`, `sha256_vectors`, the control program's (`cpl_*`), `mkdisk_*`, `generated_*`, `driver_package_folder` |

**Not covered on the host:** 86Box's devices themselves. There is no device-side test bench for the Direct3D, Glide or OpenGL devices; a VM run is the first execution of new device code.

## The clean room

- No Microsoft DDK headers, samples or tools are in the tree, and no 3dfx code. `driver32/include/` and each driver's own headers declare what the drivers use, written from the project's specs.
- Microsoft's DDKs and SDKs, and 3dfx's documentation and drivers, were read for facts only: interface layouts, export names, version fields, token formats.
- The reference files the tests read (a VGA BIOS image, Windows' and the Voodoo3's driver files) are read in place and never copied into the tree.
- The fonts are the project's own design, and a test guards it.

## Notes for working on the code

- **86Box's threads on Windows:** a thread's handle closes as the thread ends, so wait for a thread with an event, never on its handle.
- **MSYS2's Python** joins paths with `/`, and a component starting with a backslash restarts at the drive's root; pass `C:/` paths to scripts.
- **Diagnostics changes** follow [14](14%20Diagnostics%20and%20logs.md)'s design: warnings only past real-problem thresholds.
