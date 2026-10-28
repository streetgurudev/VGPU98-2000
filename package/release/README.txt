VGPU98 2000 @VERSION@
===============

A virtual PCI graphics card for the 86Box emulator, with its own VGA/VBE BIOS
and its own Windows 98 SE drivers: the desktop up to 1920x1440, DirectDraw,
Direct3D (the DirectX 6, 7 and 8 driver interfaces, hardware T&L, shaders
1.x; DirectX 9 runtimes drive it as a DirectX 8 card), Glide 2 and 3
answering as a Voodoo3, and OpenGL 1.1. The 3D work is done by your
computer's own GPU, through Vulkan.

This is an unofficial build of 86Box, not made or supported by the 86Box
project. Please report problems at
https://github.com/streetgurudev/VGPU98-2000/issues, not to 86Box.


WHAT IS IN THIS FOLDER

  86Box.exe                      86Box with the VGPU98 2000 card (a build of
                                 86Box's development branch of 2026-10-09)
  roms\video\vgpu98\vgpu98.bin   the card's BIOS
  vgpu98-driver.iso              the Windows 98 driver CD
  README.txt                     this file
  THIRD-PARTY-NOTICES.txt        the libraries inside 86Box.exe, and their
                                 licences
  LICENSE.txt, COPYING.txt       the licences of the VGPU98 2000 and of 86Box
                                 (GNU GPL)
  SOURCE.txt                     where the source code is, with its versions
  vgpu98.patch                   what the card changes in 86Box, as one diff


WHAT YOU NEED

  - Windows 10 or 11, 64-bit.
  - A GPU and driver with Vulkan 1.1, including the features shaderClipDistance
    and shaderInt64 (most AMD, NVIDIA and Intel GPUs of recent years have
    them). Without them the card still works for the desktop and DirectDraw,
    but offers no Direct3D, Glide or OpenGL.
  - 86Box's ROM set. 86Box needs the ROMs of the machines it emulates, which
    the 86Box project distributes separately. This build goes with the set at
    commit 9596fe8:
      https://github.com/86Box/roms/archive/9596fe8d80acb30b36a8a678c7bbce9a890f8050.zip
  - A Windows 98 SE installation CD, as an image file.
  - For DirectX 8 or 9 games, that DirectX runtime (Windows 98 SE ships
    DirectX 6.1).


SETTING UP

  1. Unpack this zip into a folder of its own.
  2. Unpack the ROM set and copy everything inside its top folder into the
     "roms" folder next to 86Box.exe, so that roms\machines, roms\video and
     the rest sit beside the roms\video\vgpu98 folder that is already there.
  3. Start 86Box.exe. Windows may warn that it does not know the program
     ("Windows protected your PC"): choose More info, then Run anyway.
  4. Create a machine and open its settings:
       Machine:  machine type Slot 1, machine [i440BX] ASUS P2B-LS (or another
                 board with PCI slots), CPU a Pentium II
       Memory:   128 MB or more
       Display:  VGPU98 2000 (PCI)
       Storage:  a hard disk and a CD-ROM drive
     The drivers need a Pentium Pro, Pentium II or later: Socket 7 Pentiums
     and K6 processors are not supported.
  5. Install Windows 98 SE from its CD image as usual. It uses the card as a
     standard VGA adapter at first.
  6. In 86Box, Media > CD-ROM > Image, choose vgpu98-driver.iso.
  7. In Windows: Display Properties (right-click the desktop) > Settings >
     Advanced > Adapter > Change. Choose "Display a list of all the drivers
     in a specific location", then Have Disk, and give it the CD's drive.
     Restart when it asks.
  8. For DirectX 8 or 9 games, install that DirectX runtime.

  Updating to a newer driver works the same way: Have Disk with the new CD.


THE CARD IN WINDOWS

  Display Properties > Settings > Advanced has two tabs of the card's own, also
  under Control Panel > VGPU98 2000:
    VGPU98 Color   gamma, brightness and contrast, for all channels or one
    VGPU98 3D      vertical sync for Direct3D, OpenGL and Glide: the program's
                   choice, always on, or always off


THE CARD'S SETTINGS IN 86BOX

  Settings > Display > Configure, beside the card:
    Memory size         8 to 128 MB of video memory (128 by default)
    Diagnostics         what the card writes to 86Box's log (below)
    and switches that each turn one feature off, so you can tell whether it
    causes a problem: Cursor (Software), Gamma, V-sync overrides, Locks
    through main memory, DirectX 8 interface, DirectX 8 shaders, Pipeline
    cache (Start empty).


WHEN SOMETHING GOES WRONG

  - Try turning off the feature involved with its switch (above).
  - To report a problem, set Diagnostics for the area: DirectX (or DirectX +
    capture), Glide, OpenGL, or Display for mode changes. Start 86Box with a
    log file, for example from a shortcut whose target ends in
      86Box.exe --logfile vgpu98.log
    then show the problem and attach the log to your report. Pictures the card
    captures go into the machine's vgpu98-textures folder.
  - The "Illegal instruction" line 86Box logs while Windows 98 boots is 86Box's
    own, and harmless.


KNOWN ISSUES

  - 3DMark99: some texture stretching.
  - Giants: distant terrain textures stretch with the camera angle.
  - GTA3: a garbled first frame.
  - Morrowind: one door texture is wrong.
  - Unreal Gold (Glide): a crash after the window loses focus.
  - 3DMark03 skips game test 4, which needs pixel shaders 2.0, as on any
    DirectX 8 card.
  - Need for Speed III's 3D setup looks for a 3dfx card on the PCI bus and
    does not find one.
  - Not offered: 24 bits per pixel, widescreen resolutions, video overlays,
    DirectX 9's own driver interface, OpenGL beyond 1.1, Windows 95.


MORE

  How the card works: the VGPU98 Technical manual,
    https://github.com/streetgurudev/VGPU98-2000/blob/main/VGPU98%20Technical%20manual/00%20Index.md
  The source code: SOURCE.txt.
  Licences: LICENSE.txt (the VGPU98 2000), COPYING.txt (86Box) and
  THIRD-PARTY-NOTICES.txt (the libraries inside 86Box.exe).
