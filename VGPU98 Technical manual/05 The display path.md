# 05 The display path

How the card's picture reaches 86Box's window: the standard VGA, the card's display engine for the extended modes, the display on the host GPU that composes each frame, and the cursor, gamma and V-sync applied on the way. The reference is [`spec/display-engine.md`](../spec/display-engine.md), [`spec/modes.md`](../spec/modes.md), [`spec/registers.md`](../spec/registers.md) and, for the cursor, gamma and V-sync registers, [`spec/gpu-registers.md`](../spec/gpu-registers.md).

## Two ways to scan out

```
                           +---------------------------------------+
 VGA modes (00h-13h)  -->  | 86Box's VGA core (svga): the card is  |
 boot, DOS, setup,         | a register-exact VGA, drawn by 86Box's|--+
 Safe Mode                 | own line-by-line renderer             |  |
                           +---------------------------------------+  |
                                                                      v
                           +---------------------------------------+  86Box's
 extended modes       -->  | the display engine (SR80h-BFh):       |  window
 the Windows desktop,      | packed pixels at 8, 15, 16, 32 bpp,   |  ^
 VBE modes                 | composed on the host GPU (below), or  |--+
                           | by 86Box's line renderer as fallback  |
                           +---------------------------------------+
```

The VGA is always there: the ROM's text modes, DOS programs, Windows setup, Safe Mode and full-screen DOS sessions use it unchanged. The display engine takes over when a driver or a VBE call turns it on.

## The display engine

The engine's registers are sequencer indices 80h-BFh, behind a key: writing 98h to SR80h unlocks them. Every routine that touches them (the BIOS, the display driver, the mini-VDD) leaves the lock and the sequencer index as it found them.

| Index | Register | What it holds |
|---|---|---|
| 81h, 82h | ID, REVISION | 98h; revision 3 (double scan, the register base, GPU reset) |
| 84h, 85h, 86h | FEATURES, FORMATS, MAX_WIDTH | What the engine can do; the pixel formats; 1920 |
| 88h, 8Ch | VRAM_SIZE, LFB_BASE | Video memory size; the frame buffer's PCI address |
| 90h, 91h | CONTROL, FORMAT | ENABLE and double scan (DSCAN); 8, 15, 16 or 32 bpp |
| 92h-98h | WIDTH, HEIGHT, PITCH, START | The mode, and where in video memory the frame starts |
| 9Ch, 9Eh | REFRESH, BANK | 50-160 Hz; the VGA window's bank |
| A0h, A1h | EDID_INDEX, EDID_DATA | The virtual monitor's EDID |
| A2h, A4h, A8h | SCRATCH, REGS_BASE, GPU_RESET | Scratch; BAR0's address; reset the GPU |
| E0h | Debug | Text to 86Box's log ([14](14%20Diagnostics%20and%20logs.md)) |

**Timings** come from the mode and refresh rate with fixed blanking, so the vertical blank and scan line the drivers read are consistent at every rate. **Double scan** draws each line twice, for the low-resolution modes.

## The modes

`card/display-modes.card` is the single list for the BIOS, the Windows driver and the INF.

| Group | Resolutions | Where offered |
|---|---|---|
| The desktop | 640x480, 800x600, 960x720, 1024x768, 1152x864, 1280x960, 1280x1024, 1600x1024, 1600x1200, 1792x1344, 1856x1392, 1920x1080, 1920x1200, 1920x1440 | Windows at 8, 16 and 32 bpp, each with its refresh list from 50 to 160 Hz; VBE where a mode number is given |
| Low resolutions | 320x200, 320x240, 400x300, 512x384, 640x400 | VBE only, double-scanned |
| Widescreen | 1280x720, 1280x800, 1360x768, 1440x900, 1680x1050 | Defined but switched off |

Windows also gets 640x480 at 16 colours, which runs on its own `vga.drv` and the standard VGA.

**The virtual monitor** answers DDC with EDID 1.3: maker "VGP", product 2000h, "VGPU98 Screen", 20 inches at 4:3, preferred 1024x768 at 75 Hz. The INF turns DDC on, so Windows reads it.

## The host display

While the display engine is on, the card composes each frame on the host GPU (`vid_vgpu98_disp.c`), on the same Vulkan device the 3D renderer uses. 86Box's VGA core is told to draw no lines meanwhile (its `host_display` flag).

```
 video memory --changed pages only--> the mirror (on the host GPU)
                                           |
 the 3D layer: GPU-newer pixels      ------+
 published from the images, with           |
 the rectangles that hold them             v
                                  display.comp (compute shader):
 Glide's front buffer, while a    takes each pixel from the layer
 session shows (replaces the      where it has one, else from the
 frame)                     ----> mirror; converts to 86Box's 32-bit
                                  pixels as 86Box's own renderers do
                                           |
                                  the hardware cursor laid over
                                  the gamma table applied (direct
                                  colour modes only)
                                           |
 at each vertical blank, on the emulation thread:
   the rows composed since --> the monitor's frame buffer
   86Box's VGA core blits it, sized, bordered and timed as for any
   card, and the next frame is posted
                                           v
                                    86Box's window
```

- **The mirror** only uploads the pages of the visible frame whose write counters changed since their last upload. A still desktop costs almost nothing.
- **The layer** is how Direct3D frames reach the screen without a copy back: the renderer publishes the newest pixels of the surface on display straight from its image ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)).
- **A Glide session** hands each swapped frame to the display, which shows it instead of video memory until the session ends ([11](11%20Glide%20translation.md)).
- **OpenGL** writes its frames into video memory, so they arrive through the mirror like any other pixels ([12](12%20OpenGL%20translation.md)).
- **The fallback.** When the host display cannot compose (no usable host GPU, a frame larger than 86Box's frame buffer, the monitor asleep), 86Box's own line renderer draws the engine's modes, with the card's cursor and gamma applied to each line it draws.
- **A pass-through card** that takes the screen, such as a Voodoo in the same machine, has it to itself; the display steps aside.

## The hardware cursor

| Fact | Value |
|---|---|
| Registers | CURSOR_CONTROL, CURSOR_POS, CURSOR_BASE in BAR0, taken at vertical blank |
| Shapes | Up to 64x64, monochrome and colour, Windows' own cursor shapes |
| Patterns | Two 32 KB slots in the reserve, so a new shape never tears |
| Drawn | At scan-out, by Windows' AND/XOR rule, over the composed frame. The cursor is never in video memory |
| Driver | `driver/cursor.asm` in `vgpu98.drv` |

Shapes the card cannot hold, every shape while pointer trails are on, and every shape on a card without PROFILE.CURSOR go to the DIB engine's software cursor instead; changing over hides the other cursor first, so there are never two. The cursor is hidden over a Glide session's screen, as on a Voodoo3.

## Gamma

| Fact | Value |
|---|---|
| Registers | GAMMA_CONTROL, GAMMA_BASE in BAR0, taken at vertical blank |
| Table | 256 entries for red, green and blue, in two 1 KB slots in the reserve |
| Where it comes from | Windows' SetDeviceGammaRamp and DirectDraw's (and so Direct3D's) gamma control reach the display driver's DDIGammaRamp, ordinal 32; the control program's VGPU98 Color tab sets the desktop's ramp ([07](07%20The%20Windows%2098%20driver%20stack.md)) |
| Applied | At scan-out, to the direct colour modes. At 8 bpp the palette is the DAC, so a ramp is refused |
| Off | The identity ramp turns the table off. The setting "Gamma" Off clears PROFILE.GAMMA |

## V-sync

- **The card's timing** is the display engine's: VBLANK_COUNT and SCANLINE in BAR0 follow the mode's refresh rate.
- **A flip** (DirectDraw's, and Direct3D's through it) waits for the vertical blank unless the program asked not to; FLIP_PENDING shows a flip not yet taken.
- **Glide's swaps** and **OpenGL's swap interval** wait for the given number of vertical blanks.
- **The V-sync overrides** (VSYNC_CONTROL) can force each interface's waits on or off, whatever the program asks: two bits each for Direct3D, OpenGL and Glide. The control program's VGPU98 3D tab sets them; a program picks up a change at its next flip or swap. Windowed OpenGL never waits, so GDI is never held behind it.

## Mode changes

A mode change happens in the middle of whatever frame was being shown. The card keeps the change clean:
- **The display driver holds the GPU** around the change (HOLD_DRIVER), so no 3D work lands in memory being laid out again ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)).
- **The card ends the frame its own change strands,** so 86Box never shows one frame stretched over two modes' timings.
- **The first frame of each new mode is black,** instead of whatever video memory held from before.
- **VGA to VGA changes** are 86Box's own, untouched.

Diagnostics "Display" logs each transition and captures the frames around it ([14](14%20Diagnostics%20and%20logs.md)).
