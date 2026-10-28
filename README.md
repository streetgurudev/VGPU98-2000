This is an Opus 5.5 Vibe Coded experiment to hook a host GPU into 86box, using an "OLD API" -> Vulkan translation layer, one for Glide(only Unreal GOLD is tested), DirectX up to DX8, and OpenGL 1.1 + Q2 extensions.

Hasn't been remotely fully tested, probably isn't too optimized. I don't know a single thing about coding, or maintaining a repo or project.

It does successfully run 3DMark 99/00/01/03 and I've tried out a few games for each API

But some of those specifically require DX 8.1, not DX9 installed, which some games may force install.

Glide may need it's own specific virtual GPU and work, because those games really want to see a 3DFX ID and dlls sometimes to even install. But Unreal GOLD works. Other games might be able to force glide.

I think it throws off 86box's performance % thingy, because games still run smooth at times, despite the P2 not being at 100% all the time. I run a 5800X3D + RX 9070 XT, haven't tested on other devices. The P2 may not be strong enough for some later DX8 titles itself, Morrowind boots for example, but even at 450mhz, assuming my 5800X3D can handle that, it a bit slow.

I used Slot 1, [i440BX] ASUS P2B-LS, 128MB RAM, Pentium II(Deschutes) 300-450mhz

Display [PCI]VGPU98 2000,
 with the config set to 128MB of VRAM
SoundBlaster 16 for sound
Windows 98 SE

Some games may require options tweaks, GTA3 for example needs "Trails" off

---------

# VGPU98 2000

A virtual PCI graphics card for the [86Box](https://86box.net) emulator, with its own VGA/VBE BIOS and its own Windows 98 SE drivers. Inside the virtual machine it is an ordinary card of 1999 or 2000: the desktop up to 1920x1440, DirectDraw, Direct3D through DirectX 8's driver interface (hardware T&L, shaders 1.x), Glide 2 and 3 answering as a Voodoo3, and OpenGL 1.1. The 3D work is done by the host's GPU through Vulkan.

**Version 1.0.** How everything works, path by path, is in the **[VGPU98 Technical manual](VGPU98%20Technical%20manual/00%20Index.md)**. The detailed reference (registers, packets, file formats) is in [`spec/`](spec/README.md).

## What you need

- **Ready-built:** the [Releases](https://github.com/streetgurudev/VGPU98-2000/releases) page has `86Box.exe` with the card, the card's ROM and the driver CD in one zip. 86Box's own ROM set comes from 86Box.
- **The host:** Windows, with a GPU and driver supporting Vulkan 1.1 with `shaderClipDistance` and `shaderInt64`. Without one, the card runs without its 3D.
- **The virtual machine:** an 86Box machine with a PCI slot and a Pentium Pro or Pentium II-class processor (an Intel 440BX board with a Pentium II, for example), 128 MB of memory or more, and Windows 98 SE. DirectX 8 or 9 programs need that DirectX runtime installed.
- **Installing:** choose **VGPU98 2000 (PCI)** as the video card, with its ROM at `roms/video/vgpu98/vgpu98.bin`; install Windows 98 SE; then update the display adapter's driver from `vgpu98-driver.iso` with Have Disk. The manual's [Windows 98 driver stack](VGPU98%20Technical%20manual/07%20The%20Windows%2098%20driver%20stack.md) has the details.

## Building from source

The project is two repositories: this one, and [86Box-VGPU98](https://github.com/streetgurudev/86Box-VGPU98), which is 86Box with the card's code on its `vgpu98` branch. The second is a submodule at `86Box/`, so one clone fetches both:

```sh
git clone --recursive https://github.com/streetgurudev/VGPU98-2000.git
```

**The toolchain** is MSYS2's UCRT64 environment, with the versions pinned in `toolchain.lock`: GCC, CMake, Ninja, NASM, zlib, glslang, the Vulkan headers, and the libraries 86Box needs; `pacman -S --needed` with the names there installs them all. Unicorn is optional; the tests use it to run the BIOS, the display driver and the mini-VDD on the host. Build from a UCRT64 login shell, in the clone:

```sh
scripts/setup-86box.sh            # once
cmake --preset default            # once
cmake --build --preset default    # the tools, the BIOS, the drivers, the CD
ctest --preset default            # the host-side tests
cd 86Box
cmake --preset release            # once
cmake --build --preset release    # bin/86Box.exe
```

- **`scripts/setup-86box.sh`** prepares `86Box/`: the branch `vgpu98`, a fetch-only `upstream` remote for the official 86Box repository, a pre-push hook that refuses pushes there, and the `release` preset, which writes `86Box.exe` into `bin/` beside `bin/roms`.
- **The submodule.** This repository records which commit of 86Box-VGPU98 goes with each of its own. After a commit in `86Box/`, record it here too (`git add 86Box`, then commit); `release.sh` refuses to run until the two agree.
- **`scripts/release.sh`** does a whole release from clean trees: every build, the tests, the card header check, the ROM copied into `bin/roms`, the driver folder, CD image and release zip in `dist/`, and `build/release.txt` with both commits and the SHA-256 of every file. With `--repro` it builds twice from two paths and requires identical bytes. It needs `git` on the shell's PATH and refuses to run while `bin/86Box.exe` is open.
- **The card header.** `card/*.card` defines the card's identity, registers and modes; `cardc` compiles it for the BIOS, the display driver and 86Box, and `infgen` writes the INF from it. When it changes, run `cmake --build build --target update-86box-header` and commit the header in the 86Box clone too.

The manual's [Building, tools and file formats](VGPU98%20Technical%20manual/15%20Building%2C%20tools%20and%20file%20formats.md) covers the tools, the tests and reproducibility in full.

## Notes

- **Optional tests.** `fonts_independent` reads a reference VGA BIOS image (`VGPU98_REFERENCE_VGA_ROM`, by default `bin/roms/video/vga/ibm_vga.bin`), and the five `drvlint_windows_*` tests read Windows' and the Voodoo3's own driver files from a local reference library (`VGPU98_REFERENCE_LIBRARY`, by default under `archive/`). Both are only read, never copied; without them the tests skip themselves.
- **Never push the card's work to the official 86Box repository.** In `86Box/`, `origin` is the fork and `upstream` the official repository, fetch-only. To take in upstream's changes, fetch `upstream` and merge `upstream/master` into `vgpu98`.
- **Diagnostics** go to 86Box's log, never inside the VM: set the card's Diagnostics setting and start 86Box with `--logfile <file>` ([Diagnostics and logs](VGPU98%20Technical%20manual/14%20Diagnostics%20and%20logs.md)). The `Illegal instruction` line 86Box logs during Windows 98's boot is 86Box's own and harmless.
- **Clean room.** No Microsoft DDK headers, samples or tools are in the tree, and no 3dfx code. Their documentation was read for facts only. The fonts are the project's own design.
- **No per-game fixes.** Every fix is a rule of the interface, applied to every program.

## Licence

GPL-2.0-or-later (`LICENSE`). Copyright 2026 VGPU98 Project contributors.

The project builds on 86Box, and on its developers' work on the VGA core and the machines the card runs in. The Glide DLLs answer with the names, version strings and exports of 3dfx's Voodoo3 drivers, and the desktop modes follow the Voodoo3 1.07 driver's list. These are facts of the interfaces programs expect; no 3dfx code is used. "Voodoo3" and "Glide" are 3dfx's names.
