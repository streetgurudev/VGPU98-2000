# spec

The card's technical reference: the hardware, the BIOS, the guest drivers' interfaces, the GPU and the file formats. For how the pieces fit together, start with the [VGPU98 Technical manual](../VGPU98%20Technical%20manual/00%20Index.md).

The 86Box device and the BIOS both implement this spec, not each other.

| Page | Contents |
|---|---|
| [pci-config.md](pci-config.md) | PCI configuration space |
| [registers.md](registers.md) | Register map: BAR0, the debug register, the Diagnostics levels (Display among them) and 86Box's settings for the card |
| [display-engine.md](display-engine.md) | The display engine: extended registers, extended modes, mode changes (the frame a change strands, a new mode's first frame), EDID port |
| [modes.md](modes.md) | The display mode table, VBE numbers, timing, the monitor's EDID |
| [bios.md](bios.md) | The VGA BIOS: INT 10h, VBE 2.0, VBE/DDC |
| [rom-layout.md](rom-layout.md) | The option ROM image |
| [vga-parameter-table.md](vga-parameter-table.md) | The BIOS's video parameter table |
| [font-format.md](font-format.md) | The font source format |
| [card-definition.md](card-definition.md) | The card definition files |
| [vglink.md](vglink.md) | The driver linker and its link definitions |
| [ne-format.md](ne-format.md), [le-format.md](le-format.md), [pe-format.md](pe-format.md) | The executable formats of the minidriver, the mini-VDD (its version resource among them) and the 32-bit DLLs (the DirectX driver, shared by every process; the Glide DLLs, the OpenGL ICD and the control program, per process) |
| [win98-display-driver.md](win98-display-driver.md) | The Windows 98 display driver: exports, imports, Enable, modes, screen switching, the Glide DLLs' escape, the cursor, gamma (`DDIGammaRamp`) |
| [minivdd.md](minivdd.md) | The Windows 98 mini-VDD: initialization, dispatch functions, per-VM engine state, the ring buffer and the pool in main memory |
| [driver-package.md](driver-package.md) | The INF, the driver package folder (eight files), the OpenGL ICD's registry value, the control program (`vgpu98cp.cpl`: its registration, tabs and settings), and its CD image |
| [gpu-registers.md](gpu-registers.md) | The GPU's registers in BAR0 (interface 3.6): command ring (in video or main memory), status block, the GPU's page table, fences, display timing, the hardware cursor (3.4), the gamma table (3.5), the V-sync overrides (3.6) |
| [gpu-commands.md](gpu-commands.md) | Command sets 2.0 to 2.7, the packets the drivers send the GPU: 2D, render states, the vertex pipeline, fixed-function transform and lighting (DirectX 8's in 2.1), programs (2.2), DOWNLOAD (2.3), how a primitive is drawn; the Glide device's commands (2.4); the OpenGL device's commands (2.5), its extensions' additions (2.6) and clamp to edge (2.7); interface 3.6's V-sync overrides in FLIP and the SWAPs |
| [gpu-coherency.md](gpu-coherency.md) | How video memory, main memory and the host GPU's copies stay in step |
| [gpu-formats.md](gpu-formats.md) | Surface formats in video memory and on the host |
| [directx-hal.md](directx-hal.md) | The DirectDraw and Direct3D driver interfaces of DirectX 6.1, 7 and 8: handshake, callbacks, execute buffers, DrawPrimitives2, surface handles, streams and index buffers |
| [d3d-caps.md](d3d-caps.md) | What the driver advertises: the DirectX 7 and 8 profiles |
| [glide.md](glide.md) | The Glide DLLs, `glide2x.dll` and `glide3x.dll`, and 86Box's Glide device: the files, finding the card through the display driver's escape, the Voodoo3 they answer as, the session and the screen (DirectDraw's exclusive mode, the Glide screen), batches through the ring, the state registers, drawing (Glide 2's vertices, Glide 3's layout and coordinate spaces), the pixel pipeline, textures, the frame buffer, gamma, pacing, Glide 3's contexts and saved state, what every export does, what is deferred, the log and its Diagnostics level (the DLL check, the call record, the recorder and fault catcher, the device's report, findings and captures), the export lists, the host tests |
| [opengl.md](opengl.md) | The OpenGL ICD, `vgpu98gl.dll`, and 86Box's OpenGL device: the file, how Windows 98's `opengl32.dll` finds it (the display driver's OPENGL_GETINFO escape, the registry value), finding the card, the pixel formats, windows, contexts and threads, share groups, the device's objects and their IDs, batches through the ring and the registers sent as they change, GL's state in the ICD, drawing (the device's path and the ICD's own vertex path), textures, pixel rectangles and read-backs through the staging surface, the front buffer and swaps, mode changes, DOS sessions, GPU restarts and the process's end, the identity and limits; the device's objects, drawables, depth, transform, lighting and per-fragment operations, pipelines, the texel arena, PIXELS, READ, TEXREAD and SWAP; what is not offered, the log and its Diagnostics level, the export list, the host test |
| [pipeline-cache.md](pipeline-cache.md) | 86Box's cache of the host GPU's pipelines: its files and their format, the checks, which file is read and which written, the setting, the key list and warm-up, the cap, several 86Box at once |
