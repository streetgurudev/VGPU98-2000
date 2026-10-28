# tools

Authoring tools and test harnesses, written in C and built by the root CMake project.

| Tool | Job |
|---|---|
| `cardc` | Card compiler: card definition to BIOS include files and the 86Box header |
| `fontc` | Font compiler: text-art fonts to BIOS include files |
| `romfix` | Pads the ROM image, fills in its length fields and sets the checksum |
| `romlint` | Checks the finished ROM's structure against the card definition |
| `manifest` | Records SHA-256 hashes of every input and output, plus tool versions |
| `vglink` | Linker: NASM's OMF objects to a 16-bit NE driver or an LE VxD, gcc's COFF objects to a PE DLL, shared by every process (the DirectX driver) or per process (the others), with decorated exports from a list for the Glide DLLs; a version resource for each ([spec/vglink.md](../spec/vglink.md)) |
| `drvlint` | Checks NE, LE and PE files and their version resources; `--display`, `--vxd`, `--pe` and `--glide` add what each kind of driver needs, `--exports` checks a PE export table against a list, `--strict` the checks vglink's own output must pass, `--dump` prints every table, relocation and fixup ([spec/pe-format.md](../spec/pe-format.md)) |
| `sbgen` | Writes the shared block's layout and the Glide DLLs' escape, its question and answer (driver32/shared.h), as the 16-bit driver's NASM include |
| `infgen` | Writes the driver's INF from the card definition ([spec/driver-package.md](../spec/driver-package.md)) |
| `mkdisk` | Writes a deterministic 1.44 MB FAT12 floppy image |
| `zipgen` | Writes the release zip, the same for the same files, and checks it by reading it back |
| `isogen` | Writes the driver package's CD image (ISO 9660, padded at the end) and checks it by reading it back ([spec/driver-package.md](../spec/driver-package.md)) |
| `biostest` | Runs the BIOS under the Unicorn CPU emulator against a model of the card (test only) |
| `drvtest` | Runs the display driver vgpu98.drv under Unicorn with stand-ins for the parts of Windows it calls, and checks what it does (test only) |
| `vxdtest` | Runs the mini-VDD vgpu98.vxd under Unicorn with stand-ins for the VMM and the VDD, and checks what it does (test only) |
