# 14 Diagnostics and logs

The card and every one of its drivers report to 86Box's log on the host, never inside the virtual machine. This document covers where the lines come from, the Diagnostics levels, the captures, and how to read a run. The reference is [`spec/registers.md`](../spec/registers.md) (Diagnostics, the DEBUG register) and the diagnostics sections of [`spec/directx-hal.md`](../spec/directx-hal.md), [`spec/glide.md`](../spec/glide.md) and [`spec/opengl.md`](../spec/opengl.md).

## Where the lines come from

```
 GUEST                                     HOST: 86Box
 -----                                     -----------
 the BIOS, vgpu98.drv, vgpu98.vxd
   one character at a time to the   --->  the card's line buffer
   debug register (sequencer E0h),        "VGPU98 debug: ..."
   only while it reads 01h                      |
                                                |
 vgpu98dx.dll, glide2x/3x, vgpu98gl,            |
 vgpu98cp.cpl                                   |
   one character at a time to BAR0's  --->  the same buffer, whole
   DEBUG register, under the shared         lines kept apart by the
   block's line lock; DEBUG read            line lock
   first: which lines to write                  |
                                                |
 audit notes (VSUM, PROC, Lock records)         |
   as NOP packets in the ring         --->  the Direct3D device's
                                            checks and summaries
                                                |
 the card itself: the command processor,        |
 the three devices, the display          --->  86Box's log (pclog)
                                                |
 captures: pictures and text files  ------->  <machine folder>/
                                               vgpu98-textures/
```

- **Every line** carries the host's local date and time. The guest drivers' lines also carry their build stamp (the commit they were built from) and the program's name.
- **The drivers write only what is asked.** Reading BAR0's DEBUG register tells them which lines the current level wants; with the log off, or on other hardware, nothing is written.
- **To keep a log,** start 86Box with `--logfile <file>`.

## The levels

The setting "Diagnostics (86Box's log, not inside the VM)". The saved values are fixed; the list shows them in this order.

| Value | Level | What it adds |
|---|---|---|
| 0 | Off | Nothing |
| 6 | Minimal (default) | The start line naming the machine and the settings; events, the watchdog, resets and their causes; the drivers' event lines. No summaries |
| 1 | DirectX | Summaries every few seconds, and the DirectX DLL's full lines |
| 2 | DirectX + capture | And the recorder, written out as a snapshot around a failure |
| 7 | DirectX: texture audit | And the texture audit: stale textures, churn and formats, coordinate and stretch checks, the display draw check, the vertex race detector, texture dumps at each capture, the DLL's texture trace |
| 4 | DirectX 7: vertex audit | And the DLL's checks of every call's vertex ranges, draws that lack vertices, dropped-draw counts, captures at screenshots |
| 5 | DirectX 8: stream + texture audit (slow) | Level 4, plus DirectX 8's draws, the handshake trace and the whole texture audit |
| 8 | Glide | Minimal, plus the Glide DLLs' full lines and the Glide device's report, findings and captures |
| 9 | OpenGL | Minimal, plus the ICD's full lines and the OpenGL device's report every 5 seconds, findings and captures |
| 10 | Display | Minimal, plus a line at each screen transition, 86Box's screen sizes, and display captures |
| 3 | Developer: every packet (slow) | Every packet and coherency event as it happens |

At every level but Off, the OpenGL device names its first 16 dropped commands and the ICD writes its event lines.

**Bits.** The card turns the level into log bits (DEBUG_REGISTER, EVENTS, SUMMARY, CAPTURE, COMMANDS, AUDIT, AUDIT8, MINIMAL, TEXTURES, GLIDE, OPENGL, DISPLAY in `vid_vgpu98_dev.h`) and into the bits the guest reads from DEBUG (LISTENING, AUDIT, AUDIT8, SHADERS, SHADERS14, EVENTS, TEXTURES, GLIDE, OPENGL).

## Lines worth knowing

| Line | From | What it tells |
|---|---|---|
| `VGPU98: diagnostics "...", N MB video memory; ...` | The card, at power-on | The level, memory, settings not at their defaults, and the machine: board, processor, speed, memory |
| `VGPU98 debug: ...` | BIOS, display driver, mini-VDD | Their own lines |
| `VGPU98 3D: ...` | The Direct3D device | The pipeline cache, pipelines, summaries |
| `VGPU98 summary ...` | The Direct3D device | The periodic summaries |
| `VGPU98 capture N textures` | The Direct3D device | A capture's texture comparisons |
| `VGPU98 Glide: ...` | The Glide device | Its report, findings, captures |
| `VGPU98 OpenGL: ...` | The OpenGL device | Its report, findings, drops |
| `VGPU98 opengl <stamp> <PROGRAM>: ...` | The ICD | Its lines |
| `VGPU98 Display: ...` | The display | Transitions, "the card ended the frame there", black frames, a stretched frame |
| `VGPU98: gamma table on: ...` / `off` | The card | Gamma changes |
| `VGPU98: V-sync overrides: Direct3D ..., OpenGL ..., Glide ...` | The card | A change of VSYNC_CONTROL |
| `[PROGRAM.EXE]` | Direct3D findings | Which process drew |
| `possible ...` | Every device | A finding: something past a real-problem threshold |

## Captures

At the levels that capture, a finding starts a capture, and so does an 86Box screenshot (Action > Take screenshot). Each goes into a folder of its own under the machine's `vgpu98-textures/`:

| Folder | What it holds |
|---|---|
| `<date time> capture N/` | Direct3D, Glide and OpenGL captures: the frame, its state and its textures, with the texture audit's PNG dumps (`tNN-host.png`, the host's copy; `tNN-guest.png`, video memory's) |
| `<date time> display capture N/` | The display's frames around a transition: `frame NN shown.png` (what was shown), `frame NN memory.png` (what video memory held), `transition.txt` |

## Reading a run

1. **Start with the start line and the events:** a reset, a refused packet (the line names it and the context that sent it), the watchdog, a fault caught in a DLL.
2. **Then the findings,** the `possible ...` lines, and the capture each one started. A capture's files are the main evidence; screenshots come second.
3. **Then the summaries** for the area being tested.

**For a display problem,** compare a display capture's `shown.png` with its `memory.png`:
- wrong pixels in both: the trouble is in video memory;
- wrong in `shown` only: presentation;
- right in both: 86Box's own window renderer.

## How the diagnostics are designed

- **Warnings only past real-problem thresholds,** and findings worded "possible". The host's watchdog waits 5 minutes without progress before it speaks, because long scene loads are normal.
- **No speed figures from the emulator.** The card reports its own measurements: time at flips, busy answers, copies back, the display line. An "emulated speed" percentage is not a reliable measure and is never printed.
- **Off is quiet.** With the level at Off, no driver writes a line.
- **86Box's own `Illegal instruction` line** during Windows 98's boot is a known 86Box quirk, not the card's.
