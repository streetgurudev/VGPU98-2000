# Driver package

What a user installs from: one folder with the driver files and the INF that tells Windows 98 what to do with them. Windows' Have Disk takes the folder as it is; in 86Box it is mounted as a CD (Media → CD-ROM → Folder…). The same files also come as a CD image, `vgpu98-driver.iso` (below), for mounting like any CD. The package is defined by `card/driver.card`; `infgen` writes the INF, which depends only on its inputs.

**The end of a CD.** Data in a disc's last sectors is not safe to read: operating systems read ahead past the end of the data (and some drives and recording modes cannot read the final sectors), which is why CD mastering tools always pad the end of a disc (mkisofs leaves 150 sectors). Windows 98 SE once installed `vgpu98dx.dll` with its last 2,048-byte sector wrong when it was the last file of an unpadded disc and ended exactly on a sector boundary. So the CD image ends with 150 blank sectors, and our 86Box fork pads every folder it mounts as a CD the same way.

## The INF (`infgen`)

```
infgen --driver card/driver.card --card card/vgpu98.card --modes card/display-modes.card -o vgpu98.inf
```

Plain ASCII, CRLF line ends, in the Windows 95/98 INF dialect (`$CHICAGO$`). Sections, in order:

| Section | Contents |
|---|---|
| `[Version]` | `Signature="$CHICAGO$"`, `Class=DISPLAY`, the provider, `DriverVer` (the fixed date and version from driver.card) |
| `[DestinationDirs]` | `DefaultDestDir=11`: the WINDOWS\SYSTEM directory |
| `[SourceDisksNames]` | disk 1: its name, no tag file, so the files install from any folder, CD or floppy (as the DDK's and Voodoo3's INFs do) |
| `[SourceDisksFiles]` | every file the INF installs, on disk 1: the minidriver, the mini-VDD, the DirectX driver DLL, the Glide 2 and Glide 3 DLLs, the OpenGL ICD, the control program |
| `[Manufacturer]`, `[Models]` | the device on one line: `PCI\VEN_9898&DEV_2000&SUBSYS_20009898`, then `PCI\VEN_9898&DEV_2000` as a compatible ID (from vgpu98.card). Windows lists every matching line as a model, so two lines would offer the card twice |
| install section (`[VGPU98]`) | copies the files (`CopyFiles=VGPU98.Copy`), deletes a previous driver's display keys (`Ver`, `DevLoader`, `DEFAULT`, `MODES`, `CURRENT`: `VGPU98.DelReg`), adds the new ones, the OpenGL ICD's value and the control program's registration (`VGPU98.AddReg`) |
| `[VGPU98.Copy]` | the same seven files in the same order, all into DefaultDestDir: the Glide DLLs where a Voodoo3's driver puts its own, so Glide programs find them by name ([glide.md](glide.md)), the OpenGL ICD where Windows' `opengl32.dll` loads it from ([opengl.md](opengl.md)), and the control program where the Control Panel finds `.cpl` files ([below](#the-control-program)) |
| `[Strings]` | provider, device and disk names |

Registry values under the display device's key (HKR):

| Value | Contents |
|---|---|
| `Ver` | `4.0`: a Windows 95/98 display driver |
| `DevLoader` | `*vdd`: VDD.VXD loads the display device and its mini-VDD |
| `DEFAULT\drv` | the minidriver's file name |
| `DEFAULT\minivdd` | the mini-VDD's file name |
| `DEFAULT\vdd` | `*vdd` |
| `DEFAULT\Mode` | the mode after installing, as `bpp,width,height` (driver.card `default_mode`) |
| `DEFAULT\RefreshRate` | `-1`: "optimal", the mode's fastest rate up to the monitor's limit (0 would be the adapter's default), as the Voodoo3's INF has it |
| `DEFAULT\DDC` | `1`: read the monitor's EDID (VBE/DDC, [bios.md](bios.md)) |
| `MODES\4\640,480` | 16 colours through Windows' own VGA driver (`vga.drv`, `*vdd`) |
| `MODES\bpp\width,height` | every desktop mode of the mode table at 8, 16 and 32 bpp, with its refresh rates in ascending order |

Video memory is not considered here: the minidriver refuses modes that do not fit (ValidateMode), so the list can stay the same for every memory size. There are no `HKR\Glide` values (a Voodoo3's Glide tweaks) yet.

One value goes outside the device's key, in `[VGPU98.AddReg]` after `DEFAULT\DDC`:

```
HKLM,"Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers","VGPU98",,"vgpu98gl.dll"
```

Windows 98's `opengl32.dll` asks the display driver for an ICD's name (the escape OPENGL_GETINFO, which the display driver answers `VGPU98` while the GPU draws OpenGL), looks the name up under that key, and loads the file the value names ([opengl.md](opengl.md), How Windows finds the ICD). The value's name is driver.card's `opengl_name`, its data the `opengl_driver` file.

## The control program

`vgpu98cp.cpl` gives the card two tabs, **VGPU98 Color** (gamma, brightness and contrast) and **VGPU98 3D** (the V-sync overrides). Four more lines in `[VGPU98.AddReg]`, after the OpenGL value, register it, its class ID being driver.card's `control_clsid`:

```
HKCR,CLSID\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2},,,"VGPU98 2000"
HKCR,CLSID\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}\InProcServer32,,,"%11%\vgpu98cp.cpl"
HKCR,CLSID\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}\InProcServer32,ThreadingModel,,"Apartment"
HKLM,"Software\Microsoft\Windows\CurrentVersion\Controls Folder\Device\shellex\PropertySheetHandlers\VGPU98 2000",,,"{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}"
```

- **Three ways in, one DLL:**
  - the `Controls Folder\Device` key is the Advanced display dialog's (Display Properties > Settings > Advanced), whose shell asks the class for both pages (IShellExtInit, IShellPropSheetExt);
  - the Control Panel finds the `.cpl` in the system folder and shows its **VGPU98 2000** icon (`CPlApplet`, its icon drawn in code), which opens both pages in a sheet of their own;
  - `rundll32.exe vgpu98cp.cpl,vgpu98_apply` at start-up.
- **The pages** are built in memory (`cpl/tmpl.c`): standard Windows 98 property pages, MS Sans Serif 8, 252x218 dialog units, standard controls only, the US English wording of the era. Without the card's gamma or V-sync overrides (PROFILE: [gpu-registers.md](gpu-registers.md)), a page says why and disables its controls.
- **Saved settings,** in HKLM: `Software\VGPU98 Project\VGPU98 2000\Gamma` (nine DWORDs) and `\VSync` (DWORDs `Direct3D`, `OpenGL` and `Glide`: 0 the program's choice, 1 always on, 2 always off).
- **At start-up** `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` value `VGPU98 2000` runs `vgpu98_apply` while either page differs from its defaults; Apply on a page decides it from both pages' saved settings, and deletes `VGPU98 Gamma`, the value older builds used. The INF writes none of these: nothing removes them when the driver goes.
- **Its lines** go to the host's log through BAR0's DEBUG, as the other DLLs' do ([registers.md](registers.md)): which way each page was opened, each Apply, and the start-up's settings.

## The CD image (`isogen`)

```
isogen -o vgpu98-driver.iso -l VGPU98 -t SECONDS|@STAMPFILE FILE...
```

A plain ISO 9660 image (ECMA-119, level 1), which Windows 98 reads natively, built with the package (`build/package/vgpu98-driver.iso`; the release copies it to `dist/`):

| Sectors (2,048 bytes) | Contents |
|---|---|
| 0-15 | blank (the system area) |
| 16 | the primary volume descriptor: volume ID `VGPU98`, publisher `VGPU98 PROJECT`, application `ISOGEN`, the volume size, one root directory |
| 17 | the volume descriptor set terminator |
| 18, 19 | the path table, little-endian then big-endian: the root alone |
| 20 | the root directory: `.`, `..`, then the files sorted by name and extension, under their 8.3 names in upper case with version `;1` (Windows shows the names in upper case; Setup ignores case, so the INF works unchanged) |
| 21 on | each file's data, contiguous, from a sector boundary, in the directory's order |
| the last 150 | blank: the padding |

Every date is the build's commit time (UTC), from the build stamp (`cmake/build-stamp.cmake`), so the image depends only on its inputs. Before writing the image, `isogen` reads it back through its own volume descriptor, path tables and directory, compares every file with its source byte for byte, and checks that no file reaches into the padding.

## A floppy image (`mkdisk`, not built)

The package is a folder and a CD image. `mkdisk`, with its tests, can still write a floppy image:

```
mkdisk -o vgpu98-driver.img -l LABEL -d YYYY-MM-DD FILE...
```

A 1.44 MB FAT12 floppy image (2880 sectors of 512 bytes; 1 boot sector, two 9-sector FATs, a 224-entry root directory, one sector per cluster):

- The boot sector has the standard 1.44 MB parameter block (media F0h, 18 sectors a track, 2 heads), an extended boot signature with the volume label, a volume serial number derived from the label and date, and file system type `FAT12`. Started by mistake, it prints that it is the driver disk and, after a key, gives control back to the BIOS (INT 19h).
- The root directory starts with the volume label entry, then the files in the order given, under their names in upper case (they must be 8.3 names). Every timestamp is the given date at midnight.
- Each file's clusters are contiguous; unused data sectors hold F6h, the format filler byte.

## driver.card

| Key | Meaning |
|---|---|
| `provider` | the provider and manufacturer name in the INF |
| `device` | the device name Windows shows |
| `disk_name`, `disk_label` | the disk's name in the INF (Windows shows it if it asks for the files), and a floppy image's volume label (at most 11 characters; checked, but only `mkdisk` uses it) |
| `display_driver`, `minivdd`, `directx_driver`, `inf` | the file names (8.3, lower case; infgen refuses others) |
| `glide2_driver`, `glide3_driver` | the Glide DLLs' file names, `glide2x.dll` and `glide3x.dll`: the names Glide 2 and Glide 3 programs load (8.3, lower case) |
| `opengl_driver` | the OpenGL ICD's file name, `vgpu98gl.dll` (8.3, lower case) |
| `opengl_name` | the ICD's name, `VGPU98`: the HKLM value's name, which the display driver's OPENGL_GETINFO answers (`driver/ddraw.asm`); letters and digits alone |
| `control_panel` | the control program's file name, `vgpu98cp.cpl` (8.3, lower case) |
| `control_clsid` | its property sheet extension's class ID, in braces, upper-case hexadecimal (infgen checks the form): the CLSID the INF registers, which `cpl/main.c`'s DllGetClassObject answers; the test `cpl_clsid_matches_inf` holds the two together |
| `version` | four numbers: the INF's `DriverVer` alone. Each file's version information is in its own link definition: the minidriver's, the mini-VDD's, the DirectX DLL's, the OpenGL ICD's and the control program's have the same numbers (the file version the INF's, the product version the project's, 1.0), the Glide DLLs' Glide's own ([glide.md](glide.md), [opengl.md](opengl.md); the mini-VDD's in a VxD's layout: [le-format.md](le-format.md)) |
| `date` | MM/DD/YYYY: the INF's `DriverVer` date, fixed so the INF is reproducible |
| `default_mode` | bits per pixel (8, 16 or 32), width, height: a desktop mode of the mode table |

## Build and release

The `driver_package` target (`package/CMakeLists.txt`) writes the folder `build/package/vgpu98-driver/` with eight files: `vgpu98.inf`, `vgpu98.drv`, `vgpu98.vxd`, `vgpu98dx.dll`, `glide2x.dll`, `glide3x.dll`, `vgpu98gl.dll` and `vgpu98cp.cpl`, nothing else, and the CD image of the same eight. The test `driver_package_folder` (`package/package-check.cmake`) checks the folder: exactly these files, the INF's `[SourceDisksFiles]` naming exactly the other seven, each identical to the build's. The INF test (`tools/tests/infgen-check.cmake`) checks `[SourceDisksFiles]` and `[VGPU98.Copy]` line by line , the OpenGLdrivers value and the control program's four lines, and infgen's refusals include a Glide file name in capitals. `scripts/release.sh` copies the eight to `dist/vgpu98-driver/`, overwriting in place so that a folder 86Box has mounted stays valid (86Box reads the folder's listing when it mounts it, so mount it again after a rebuild), and anything else there is removed; it copies the CD image to `dist/vgpu98-driver.iso` and records the SHA-256 of the eight and of the image in `build/release.txt`. `scripts/repro.sh` checks that two builds give byte-identical files: among them the seven driver files as built (`glide/glide2x.dll`, `glide/glide3x.dll`, `opengl/vgpu98gl.dll` and `cpl/vgpu98cp.cpl` with the others), the package folder's eight and the CD image.
