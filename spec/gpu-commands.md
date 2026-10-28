# GPU command set 2.1

**Versions:** command set 2.0 keeps command set 1.0's control, 2D and render-state packets, replaces its vertex packets with a vertex pipeline (declarations, streams, indices), and adds fixed-function transform and lighting. Command set 2.1 adds DirectX 8's fixed function to 2.0, below in [Command set 2.1](#command-set-21) and marked "2.1" where 2.0's tables gain a value; it needs PROFILE.FIXED8, and a GPU without that bit refuses every value 2.1 adds (ERROR_CODE 5), so it is exactly command set 2.0. Command set 2.2 adds programmable vertex and pixel processing, below in [Command set 2.2](#command-set-22); it needs PROFILE.PROGRAMS, and a GPU without that bit is exactly command set 2.1. Command set 2.3 adds DOWNLOAD, below in [2D](#2d) and marked "2.3"; it needs PROFILE.DOWNLOAD, and a GPU without that bit is exactly command set 2.2. Command set 2.4 adds the Glide device's commands, opcodes 40h-4Fh, below in [Command set 2.4](#command-set-24): a device of its own beside the 3D pipeline, for the Glide DLLs ([glide.md](glide.md)); it needs PROFILE.GLIDE, and a GPU without that bit is exactly command set 2.3. Command set 2.5 adds the OpenGL device's commands, opcodes 50h-5Fh, below in [Command set 2.5](#command-set-25): a second device of its own beside the 3D pipeline, for the OpenGL ICD ([opengl.md](opengl.md)); it needs PROFILE.OGL, and a GPU without that bit is exactly command set 2.4. Command set 2.6 adds to the OpenGL device what the ICD's extensions need, below in [Command set 2.6](#command-set-26) and marked "2.6" where 2.5's tables gain a value; it needs no bit of its own (PROFILE.OGL, as 2.5), and adds SWAP's swap interval, texture unit 1 (its registers, DRAW_TEX1, PIXELS_TEX1), partial texture updates, the point parameters' registers, and index textures with their palettes (TEXIMAGE's INDEX base format, opcode 5Eh PALETTE). Command set 2.7, the current one, adds TEXPARAM's wrap value 2, clamp to edge, below in [Command set 2.7](#command-set-27) and marked "2.7" in TEXPARAM's table; it needs no bit of its own either, and nothing else changes. Interface 3.6, with the command set still 2.7, puts the V-sync overrides into FLIP's and the Glide SWAP's waits, and gives the OpenGL SWAP the flag OVERRIDE (8) that lets them into a SHOW's ([gpu-registers.md](gpu-registers.md), The V-sync overrides).

The packets the driver writes into the command ring ([gpu-registers.md](gpu-registers.md)). Command set 2.0 is a GPU of the DirectX 7 class with hardware transform and lighting: 2D fills, copies and uploads, page flips, and triangle, line and point rendering with up to eight texture stages, from vertices either already in screen space or transformed, lit and clipped by the GPU. It describes what the GPU does, independently of any graphics API; the Direct3D driver translates DrawPrimitives2 and DirectDraw calls into it ([directx-hal.md](directx-hal.md)). Its vertex input is shaped like DirectX 8's (declarations, streams, constant registers), so later command sets add programmable vertex and pixel processing on the same pipeline.

Where this page and Direct3D 7's documentation leave a detail open, the GPU does what Microsoft's reference rasterizer and its reference transform and lighting do: their source in the DirectX 7 DDK (in `archive/`) is the reference for facts, as the DDK is elsewhere. The passes that implement each part settle those details and record them here.

## Packets

Every packet is a header dword followed by its payload.

| Header bits | Contents |
|---|---|
| 7:0 | Opcode |
| 23:8 | Payload length in dwords (0-65535) |
| 31:24 | 0 |

A packet whose payload is shorter than its opcode requires is rejected (ERROR_CODE 2). Packets execute in ring order, each as if the previous one had finished. A rejected packet stops the GPU ([gpu-registers.md](gpu-registers.md)).

### Common payload items

**Surface descriptor (SURF, 3 dwords)**

| Dword | Bits | Contents |
|---|---|---|
| 0 | 31:0 | Video memory offset of the first byte |
| 1 | 15:0, 31:16 | Width, height in pixels |
| 2 | 15:0 | Pitch in bytes (for DXT formats: between rows of 4×4 blocks) |
| 2 | 23:16 | Format code ([gpu-formats.md](gpu-formats.md)) |
| 2 | 31:24 | 0 |

The surface must lie inside video memory (ERROR_CODE 3).

**Rectangle (RECT, 2 dwords)**: `left | top << 16`, `right | bottom << 16`, in pixels, right and bottom exclusive. A rectangle must lie inside its surface; an empty one does nothing.

**GPU addresses** are byte addresses in the GPU's own address space, which its page table maps onto guest RAM (interface 3.0, [gpu-registers.md](gpu-registers.md)). A packet that reads through an address the table does not map is rejected (ERROR_CODE 7).

**Colours** are A8R8G8B8 dwords (Direct3D's D3DCOLOR). **Floats** are IEEE single-precision bit patterns. **Matrices** are 16 floats in Direct3D's D3DMATRIX order (`_11, _12, _13, _14, _21, ... _44`): a row vector is multiplied on the left, `v' = v · M`.

## Opcode summary

| Opcode | Name | Payload dwords | Needs PROFILE bits |
|---|---|---|---|
| 00h | NOP | any | COMMANDS |
| 01h | FENCE | 1 | COMMANDS |
| 02h | SYNC | 2 | COMMANDS |
| 03h | FLIP | 2 | COMMANDS |
| 04h | DISCARD | 2 | COMMANDS |
| 08h | FILL | 6 | BLIT |
| 09h | BLT | 13 | BLIT |
| 0Ah | UPLOAD | 8 + data | BLIT (GPUVM for a GPU address source) |
| 0Bh | DOWNLOAD (2.3) | 7 | BLIT, GPUVM, DOWNLOAD |
| 10h | TARGET | 6 | RENDER3D |
| 11h | VIEWPORT | 4 | RENDER3D |
| 12h | WRANGE | 2 | RENDER3D |
| 13h | STATE | 2 per state | RENDER3D |
| 14h | TEXTURE | 4, + 2 per level per face | RENDER3D |
| 15h | PALETTE | 2 + entries | RENDER3D |
| 18h | CLEAR | 5 + 2 per rectangle | RENDER3D |
| 19h | ZRANGE | 2 | RENDER3D, VERTEX |
| 1Ah | DECLARE | 1 + 2 per element | RENDER3D, VERTEX |
| 1Bh | STREAM | 6 | RENDER3D, VERTEX (GPUVM for a GPU address source) |
| 1Ch | INDICES | 4 | RENDER3D, VERTEX (GPUVM likewise) |
| 1Dh | INLINE | 1 + data | RENDER3D, VERTEX |
| 1Eh | DRAW | 4 + edge flags | RENDER3D, VERTEX |
| 1Fh | TRANSFORM | 17 | RENDER3D, VERTEX |
| 20h | LIGHT | 27 | RENDER3D, VERTEX |
| 21h | LIGHTENABLE | 1 | RENDER3D, VERTEX |
| 22h | MATERIAL | 17 | RENDER3D, VERTEX |
| 23h | CLIPPLANE | 5 | RENDER3D, VERTEX |
| 30h | PROGRAM (2.2) | 3 + 4 per instruction | RENDER3D, VERTEX, PROGRAMS |
| 31h | BIND (2.2) | 2 | RENDER3D, VERTEX, PROGRAMS |
| 32h | CONSTANTS (2.2) | 2 + 4 per register | RENDER3D, VERTEX, PROGRAMS |
| 40h | GL_OPEN (2.4) | 5 | RENDER3D, GLIDE |
| 41h | GL_CLOSE (2.4) | 1 | RENDER3D, GLIDE |
| 42h | GL_STATE (2.4) | 2 + 1 per register | RENDER3D, GLIDE |
| 43h | GL_DRAW (2.4) | 3 + 16 per vertex | RENDER3D, GLIDE |
| 44h | GL_CLEAR (2.4) | 4 | RENDER3D, GLIDE |
| 45h | GL_SWAP (2.4) | 2 | RENDER3D, GLIDE |
| 46h | GL_TEXDATA (2.4) | 4 + data | RENDER3D, GLIDE |
| 47h | GL_PALETTE (2.4) | 3 + entries | RENDER3D, GLIDE |
| 48h | GL_LFB_READ (2.4) | 7 | RENDER3D, GLIDE |
| 49h | GL_LFB_WRITE (2.4) | 7 | RENDER3D, GLIDE |
| 4Ah | GL_GAMMA (2.4) | 257 | RENDER3D, GLIDE |
| 50h | OGL_GROUP (2.5) | 2 | RENDER3D, OGL |
| 51h | OGL_CONTEXT (2.5) | 3 | RENDER3D, OGL |
| 52h | OGL_DRAWABLE (2.5) | 4 | RENDER3D, OGL |
| 53h | OGL_BIND (2.5) | 2 | RENDER3D, OGL |
| 54h | OGL_STATE (2.5) | 2 + 1 per register | RENDER3D, OGL |
| 55h | OGL_DRAW (2.5) | 4 + 16 per vertex (20 with DRAW_TEX1, 2.6) + 1 per two indices | RENDER3D, OGL |
| 56h | OGL_CLEAR (2.5) | 8 | RENDER3D, OGL |
| 57h | OGL_SWAP (2.5) | 7 + 2 per rectangle | RENDER3D, OGL |
| 58h | OGL_TEXIMAGE (2.5) | 6 + 1 per texel | RENDER3D, OGL |
| 59h | OGL_TEXPARAM (2.5) | 8 | RENDER3D, OGL |
| 5Ah | OGL_TEXDELETE (2.5) | 2 | RENDER3D, OGL |
| 5Bh | OGL_PIXELS (2.5) | 17 (21 with PIXELS_TEX1, 2.6) + data | RENDER3D, OGL |
| 5Ch | OGL_READ (2.5) | 6 | RENDER3D, OGL |
| 5Dh | OGL_TEXREAD (2.5) | 6 | RENDER3D, OGL |
| 5Eh | OGL_PALETTE (2.6) | 4 + 1 per entry | RENDER3D, OGL |

Opcodes 16h and 17h (command set 1.0's VERTICES and DRAW) are no longer defined; 30h-32h are command set 2.2's programs ([below](#command-set-22)) and 33h-3Fh stay reserved. 40h-4Fh are command set 2.4's Glide commands ([below](#command-set-24)): the ring checks a Glide packet's header as any other's (its first dword at least, else ERROR_CODE 2; inside the ring, else 4) and the PROFILE bits (else 5), and nothing more; the Glide device takes or drops the rest, 4Bh-4Fh (reserved) among them, and never stops the GPU. 50h-5Fh are command set 2.5's OpenGL commands ([below](#command-set-25)), checked by the ring likewise (their first dword at least, inside the ring, PROFILE's RENDER3D and OGL) and nothing more; the OpenGL device takes or drops the rest, 5Fh (reserved) among them, and 5Eh (2.6's PALETTE) on a 2.5 device, and never stops the GPU. All other opcodes are rejected (ERROR_CODE 1).

## Control

**00h NOP.** Does nothing; its payload is skipped. Pads the ring up to its end.

With the texture audit ([registers.md](registers.md), Diagnostics), a NOP whose payload is an **audit note** is the DirectX driver's note to the GPU; a GPU without the audit skips it, as it skips every NOP's payload. A note's first dword is its tag, its second a sequence number, its last a check word: FNV-1a over the dwords before it from VGPU98_NOTE_HASH_SEED (811C9DC5h; each dword XORed in, then multiplied by VGPU98_NOTE_HASH_PRIME, 01000193h). The GPU takes a note only at its tag's length, with a check word that matches and a sequence above the last it took (never 0; from 1 again after a reset that starts a new session), since a NOP padding the ring may hold an old one. Other payloads are skipped.

| Tag | Length | Payload |
|---|---|---|
| VGPU98_NOTE_VSUM (4D555356h, 'VSUM') | VGPU98_LEN_NOTE_VSUM (16) | tag, sequence, stream, offset, length, stride, lock, 8 sums, check |
| VGPU98_NOTE_PROC (434F5250h, 'PROC') | VGPU98_LEN_NOTE_PROC (7) | tag, sequence, 16 characters of name in 4 dwords, check |

A **VSUM** note describes the DRAW that follows: stream `stream`'s vertices from byte `offset` of the stream's address, `length` bytes, as the DirectX driver read them when it sent the draw. The `stream` word holds the stream in bits 7:0 (VGPU98_NOTE_VSUM_STREAM) and the buffer's audit id in bits 31:8 (from VGPU98_NOTE_VSUM_BUF_SHIFT, 8): the driver's id for the buffer, 0 for none, which a rename keeps, so the GPU follows one buffer through its new memory. The stream FFh (VGPU98_NOTE_VSUM_INDEX) is the index buffer: `stride` is then the index size, and `offset` counts from INDICES' address. `length` is a whole number of `stride`, the stream's stride (or the index size), and at most VGPU98_NOTE_VSUM_MAX (10000h). The window is cut in VGPU98_NOTE_VSUM_PARTS (8) ranges of ceil(n/8) vertices (or indices) each, n the window's vertices; each sum is FNV-1a from the seed over its range's bytes, as little-endian dwords, then each byte past the last whole dword as a dword of its own (a range past the window's end is empty, its sum the seed). The lock word is the buffer's Locks the driver gave since the buffer's last note, so those given after a draw's note come in the buffer's next one, where a write that changed the draw's window shows:

| Bits | Contents |
|---|---|
| 15:0 | VGPU98_NOTE_LOCK_FLAGS: their DDLOCK flags together (WAIT 1, READONLY 10h, NOOVERWRITE 1000h, DISCARDCONTENTS 2000h) |
| 16 | VGPU98_NOTE_LOCK_SEEN: one at least |
| 17 | VGPU98_NOTE_LOCK_UNWAITED: one given while a draw that read the buffer was pending, without waiting or new memory (never a READONLY one) |
| 18 | VGPU98_NOTE_LOCK_WAITED: one waited for such a draw |
| 19 | VGPU98_NOTE_LOCK_RENAMED: one gave the buffer new memory (DISCARDCONTENTS, or a copy) |
| 20 | VGPU98_NOTE_LOCK_OPEN: a Lock was still open (no Unlock) when the draw was sent |
| 21 | VGPU98_NOTE_LOCK_IDLE: one given with no draw that read the buffer pending |
| 22 | VGPU98_NOTE_LOCK_NOOW_BUSY: an UNWAITED one was a NOOVERWRITE |
| 23 | 0 |
| 31:24 | VGPU98_NOTE_LOCK_COUNT: how many (255 at most) |

The GPU hashes the same window where the draw reads it: the arena's copy when the arena of resident buffers holds that buffer and version ([gpu-coherency.md](gpu-coherency.md)), else guest RAM through the page table; it hashes guest RAM in either case. The command processor hands each note its place in the ring (the ring's bytes run so far) and how far the driver had written when the packet began, so a later note of the buffer is known to be written before or after the GPU's read. The stream, or for FFh the INDICES binding, is checked as the GPU has it bound: a note naming one not at a GPU address, another stride (index size) or bytes past its size is refused and counted, as are notes damaged or out of sequence. A refused note's lock word still reaches the races held for its buffer, by its audit id. What it reports: [registers.md](registers.md), Vertex race detector.

A **PROC** note names the process that draws next: its executable's file name, 16 characters at most, four to a dword from the low byte, the rest 0. The DirectX driver sends one at each DrawPrimitives2 batch's start and before a draw, when the process is not the one the last PROC note named, and again after each new context and after a GPU reset. The GPU keeps the name, its characters past the first 0 dropped and others outside 20h-7Eh taken as `?`, until the next PROC note or a reset that starts a new session; its texture audit's findings name it ([registers.md](registers.md), Diagnostics).

**01h FENCE** `value`. When every earlier packet has completed, including the copies back to video memory they need ([gpu-coherency.md](gpu-coherency.md)), the FENCE register takes `value`.

**02h SYNC** `offset`, `length`. Brings video memory from `offset` to `offset + length − 1` up to date with every GPU result covering it.

**03h FLIP** `start`, `flags`. Shows the frame at video memory offset `start` (a multiple of 4).
- First, the frame (`start` to `start + PITCH × HEIGHT`, from the display engine's current registers) is made ready to show: its GPU-newer pixels go to the display ([gpu-coherency.md](gpu-coherency.md)).
- Then, at the start of the next vertical blank, the display engine's START register (SR98h-9Bh) takes `start`, as if software had written it; the engine latches it at that retrace and shows it from the next frame ([display-engine.md](display-engine.md)). Flags bits 3:1 (INTERVAL), when 2, 3 or 4, make it wait for that many vertical blanks since the previous flip took effect; 0 and 1 mean the next one.
- With flags bit 0 (IMMEDIATE) set, START takes `start` at once and the FLIP takes effect at once. The engine still shows the new frame only from its next retrace, so for the rest of the current frame later packets may draw into the frame on screen: that is what DirectDraw's NOVSYNC asks for.
- **The V-sync overrides** (interface 3.6, PROFILE.VSYNC): the wait asked for, 0 for IMMEDIATE and else the INTERVAL (0 and 1 as 1), first goes through VSYNC_CONTROL's Direct3D field; a result of 0 is done as IMMEDIATE and any other as that INTERVAL ([gpu-registers.md](gpu-registers.md), The V-sync overrides).
- The FLIP has **taken effect** when START has been loaded at that moment, whether or not the value differs from before (a flip to the frame already shown completes at the next vertical blank like any other).
- STATUS.FLIP_PENDING is set from when the packet is read until it has taken effect. Later packets wait until then, so the GPU never draws into a frame that is still on screen. A FENCE after a FLIP completes once the flip has taken effect: the driver tracks flips by that fence, since a FLIP still queued behind other work is not yet pending.

**04h DISCARD** `offset`, `length`. The bytes from `offset` to `offset + length − 1` are about to belong to something else: the GPU drops every host copy overlapping them. A copy that extends beyond the range first copies back its GPU-newer data outside it; nothing inside the range is copied back ([gpu-coherency.md](gpu-coherency.md)).

## 2D

**08h FILL** `SURF dst`, `RECT`, `value`. Writes `value` into every pixel of the rectangle, as raw bits of the surface's format (its low 8, 16 or 32 bits). Depth and stencil surfaces are filled the same way. DXT surfaces are rejected.

**09h BLT** `SURF src`, `RECT srcrect`, `SURF dst`, `RECT dstrect`, `flags`, `srckey`, `dstkey`. Copies pixels between surfaces of the same format (else ERROR_CODE 3).

| flags bit | Meaning |
|---|---|
| 0 | SRCKEY: source pixels equal to `srckey` are not copied |
| 1 | DSTKEY: only destination pixels equal to `dstkey` are replaced |
| 2 | MIRROR_X: the source is read right to left |
| 3 | MIRROR_Y: the source is read bottom to top |

- Colour keys compare the format's colour bits only (alpha and unused bits are ignored), as DirectDraw does. A8 (2.1) has none, so every pixel of it matches a key.
- When the rectangles differ in size, the copy stretches or shrinks by point sampling. Destination pixel (x, y) takes the source pixel at column `srcrect.left + floor((x − dstrect.left + 0.5) × srcwidth / dstwidth)`, and likewise for rows. Mirroring then reverses the index.
- When source and destination share bytes, the result is as if the whole source had been read before anything was written.
- DXT surfaces are copied as whole blocks, unchanged: both rectangles the same size, their left and top multiples of 4, their right and bottom multiples of 4 or the surface's edge, and `flags` 0 (else ERROR_CODE 3). This is Direct3D's texture load between textures in video memory (TEXBLT); DirectDraw itself never asks a driver without DDCAPS2_COPYFOURCC to copy them.

**0Ah UPLOAD** `SURF dst`, `RECT`, `source`, `pitch`, `address`, then, for an inline source, the data. Writes bytes from outside video memory into a rectangle of a surface, exactly as they are: a texture or surface load from system memory.

| Dword | Contents |
|---|---|
| source, bit 0 | GPUVM: the bytes are at GPU address `address`; clear, they follow in the packet |
| source, bits 31:1 | 0 |
| pitch | Bytes from one source row to the next (for DXT surfaces, one row of blocks), at least a row's bytes |
| address | The GPU address of the first source row (ignored for an inline source) |

- The rectangle's rows (rows of blocks for DXT) are read `pitch` bytes apart; each row's bytes are the rectangle's width times the pixel size (or its width in blocks times the block size). An inline source holds the rows `pitch` apart: the packet carries at least `ceil(((rows − 1) × pitch + row bytes) / 4)` data dwords after its 8 (more are ignored). A load larger than a packet's 65535 dwords is sent as several UPLOADs of fewer rows.
- For a DXT surface the rectangle's left and top are multiples of 4, and its right and bottom are multiples of 4 or the surface's edge (else ERROR_CODE 3).
- Any format is accepted: depth, palette indices and DXT blocks are copied as raw bytes.
- The bytes land in video memory as the GPU's own write, after GPU-newer pixels of other images over the same bytes are copied back as for any 2D packet ([gpu-coherency.md](gpu-coherency.md)).
- From a GPU address, only the rows from the first to the last that differ from video memory are written (after that copy back): a copy a Lock went through and wrote nothing into moves no version and reloads no image, so a depth buffer keeps its precision.

**0Bh DOWNLOAD** (2.3) `SURF src`, `RECT`, `pitch`, `address`. Writes a rectangle of a surface into main memory at a GPU address, exactly as video memory holds it, in the surface's own format: UPLOAD's counterpart, with which the DirectX driver Locks a surface the GPU drew into through a copy in main memory ([directx-hal.md](directx-hal.md)).

| Dword | Contents |
|---|---|
| pitch | Bytes from one destination row to the next (for DXT surfaces, one row of blocks), at least a row's bytes (else ERROR_CODE 3) |
| address | The GPU address of the first destination row |

- First the GPU-newer pixels over the rectangle's bytes are copied back to video memory, as SYNC does ([gpu-coherency.md](gpu-coherency.md)); then the rows (rows of blocks for DXT) go to main memory `pitch` bytes apart, each the rectangle's width times the pixel size (or its width in blocks times the block size).
- The page table is checked for every row before any byte is written: a row the table does not map, or that reaches past 4 GB, stops the GPU (ERROR_CODE 7) with nothing written; an entry outside guest RAM, ERROR_CODE 8 ([gpu-registers.md](gpu-registers.md)). Each entry is checked again as its page is written, so a guest changing the table meanwhile cannot have the GPU write outside its RAM (the same errors, the rows before already written).
- For a DXT surface the rectangle is aligned as for UPLOAD (else ERROR_CODE 3). A format of command set 2.1 or 2.2 needs that command set's PROFILE bit, as elsewhere (ERROR_CODE 5).
- A GPU without PROFILE.DOWNLOAD (or BLIT and GPUVM) refuses the packet (ERROR_CODE 5). The destination is the DirectX driver's pool, data the guest's CPU never runs.

## 3D

### Setting up

**10h TARGET** `SURF colour`, `SURF depth`. Sets the render target and the depth/stencil buffer. A depth descriptor with format 0 means no depth buffer. The colour surface must be a render target format and the depth surface a depth format at least as large ([gpu-formats.md](gpu-formats.md)). A target may be a mip level or a cube face of a texture: surfaces are named by where they are, and the GPU finds every image of the same bytes ([gpu-coherency.md](gpu-coherency.md)).

**11h VIEWPORT** `x`, `y`, `width`, `height`. The viewport rectangle: drawing is confined to it, intersected with the render target (the scissor), and transformed vertices are mapped into it (Vertex processing, below). After RESET it is empty.

**19h ZRANGE** `minz`, `maxz` (floats). The viewport's depth range: a transformed vertex's depth z/w (0 to 1 after clipping) becomes `minz + z/w × (maxz − minz)`. Each value is clamped to 0-1 (NaN reads as 0); `minz` may be larger than `maxz`. Pre-transformed vertices are not affected. After RESET it is (0, 1).

**12h WRANGE** `wnear`, `wfar` (floats). The W range: the W-buffer's scale where the guest reads its values, `(w − wnear) / (wfar − wnear)`, the step ZBIAS takes in W, and for table fog whether the projection is affine. After RESET it is (1, 1), which means an affine projection: table fog then uses Z (below). `wfar` may be +infinity (an infinite far plane); a NaN, an infinite `wnear` or a `wfar` of −infinity is rejected (ERROR_CODE 3), the range unchanged.

**13h STATE** `id`, `value`, `id`, `value`, ... Sets render states (below). An unknown `id`, or an enumerated value outside its list, is rejected (ERROR_CODE 3). Numeric states outside their range are reduced as their table says (8-bit values masked to their low 8 bits, others clamped), so no value a program can pass through stops the GPU.

**14h TEXTURE** `control`, `size`, `format`, `colorkey`, then `offset`, `pitch` for each level of each face. Binds a texture to a stage.

| Dword | Bits | Contents |
|---|---|---|
| control | 2:0 | Stage 0-7 |
| control | 8 | COLORKEY: `colorkey` is the texture's source colour key (raw texel value) |
| control | 9 | PALETTE_ALPHA: for P8, the palette's alpha is used; otherwise alpha is 255 |
| control | 13:12 | TYPE: 0 a 2D texture, 1 a cube map, 2 (2.1) a volume; 3 is rejected (ERROR_CODE 3) |
| size | 15:0, 31:16 | Width and height of level 0, 1-2048 each, not necessarily powers of two; a cube map's are equal |
| format | 7:0 | Format code; 0 unbinds the stage (no level dwords follow) |
| format | 15:8 | Number of mip levels, 1-12, each half the size of the one before (at least 1×1) |
| format | 31:16 | 0; for a volume (2.1), its depth, 1-256 (each level half the one before, at least 1) |

A 2D texture has one face; a cube map six, in Direct3D's order +X, −X, +Y, −Y, +Z, −Z. The level dwords go face by face, levels from 0 within each face: 4 + 2 × levels × faces dwords. A volume (2.1) is 1-256 texels in each of width, height and depth, with 1-9 levels, and has three dwords per level: `offset`, `pitch` (between rows) and `slice pitch` (between the depth slices: at least the pitch times the level's height): 4 + 3 × levels dwords. Its formats are those of 2D textures but P8 and DXT1-DXT5, and it is never a render target.

**15h PALETTE** `stage`, `first | count << 16`, then `count` colours. Sets entries `first` to `first + count − 1` of the stage's 256-entry palette, used by a P8 texture on that stage. Changing a palette changes what the texture shows from the next DRAW.

**18h CLEAR** `flags`, `colour`, `depth`, `stencil`, `count`, then `count` rectangles. Clears the rectangles, clipped to the render target.

| flags bit | Clears |
|---|---|
| 0 | the render target to `colour`, converted to its format (COLORWRITEENABLE does not apply) |
| 1 | the depth buffer to `depth` (float, 0-1) |
| 2 | the stencil bits to `stencil` |

### Vertex input

The GPU draws from up to 16 **streams** of vertices, described by one **declaration** that says which element of a vertex is where. Vertex *n* of stream *s* starts at byte `n × stride` of the stream's buffer. A buffer is either the **inline buffer**, 16 MB the driver fills with INLINE packets, or memory at a GPU address (interface 3.0's main memory).

**1Ah DECLARE** `count`, then `count` elements of two dwords. Sets the vertex declaration (1-32 elements; else ERROR_CODE 3).

| Element dword | Bits | Contents |
|---|---|---|
| 0 | 3:0 | Stream 0-15 |
| 0 | 15:8 | Type (below) |
| 0 | 23:16 | Use (below) |
| 0 | 31:24 | Use index |
| 1 | 15:0 | Byte offset within the stream's vertex, a multiple of 4 |
| 1 | 31:16 | 0 |

| Type | Name | Bytes | Read as |
|---|---|---|---|
| 1-4 | FLOAT1-FLOAT4 | 4-16 | 1-4 floats |
| 5 | D3DCOLOR | 4 | an A8R8G8B8 dword: (r, g, b, a), each byte / 255 |
| 6 | UBYTE4 (2.1) | 4 | four unsigned bytes, (b0, b1, b2, b3), each 0-255 as a float, the lowest-addressed byte first |
| 7 | SHORT2 (2.1) | 4 | two signed 16-bit integers, (x, y), as floats |
| 8 | SHORT4 (2.1) | 8 | four signed 16-bit integers, (x, y, z, w), as floats |
| 9-15 | reserved (DirectX 9's types) | | rejected |

| Use | Name | Types | Index | Meaning |
|---|---|---|---|---|
| 0 | POSITION | FLOAT3 | 0 | An untransformed position (x, y, z): the GPU transforms and lights the vertex |
| 1 | POSITIONT | FLOAT4 | 0 | A pre-transformed position (x, y, z, rhw) in screen space |
| 2 | BLENDWEIGHT | FLOAT1-FLOAT4 | 0 | Vertex blending weights, the first 1-4 of which are used |
| 3 | NORMAL | FLOAT3 | 0 | The vertex normal |
| 4 | COLOR | D3DCOLOR | 0, 1 | The diffuse (0) and specular (1) colours |
| 5 | TEXCOORD | FLOAT1-FLOAT4 | 0-7 | A texture coordinate set |
| 0 | POSITION (2.1) | FLOAT3 | 1 | Tweening's second position |
| 3 | NORMAL (2.1) | FLOAT3 | 1 | Tweening's second normal |
| 6 | PSIZE (2.1) | FLOAT1 | 0 | The vertex's point size |
| 7 | BLENDINDICES (2.1) | UBYTE4 | 0 | The matrix indices of indexed vertex blending, the lowest byte first |
| 8 | INPUT (2.2) | any | 0-15 | A vertex program's input register ([Command set 2.2](#command-set-22)) |
| 5 | TEXCOORD (2.1) | SHORT2, SHORT4 | 0-7 | A coordinate set of two or four components |

A declaration without INPUT elements (2.2) has one POSITION (index 0) or one POSITIONT element, and no use and index twice; POSITION or NORMAL index 1 only with an index-0 POSITION (else ERROR_CODE 3). Elements it lacks read as: diffuse FFFFFFFFh, specular 00000000h, a coordinate set (0, 0, 0, 0), no normal (lighting then has none, below), no weights.

**1Bh STREAM** `stream`, `source`, `address`, `size`, `stride`, `version`. Sets where a stream's vertices are.

| Dword | Contents |
|---|---|
| stream | 0-15 |
| source | 0: the inline buffer, `address` a byte offset in it; 1: memory at GPU address `address` (needs PROFILE.GPUVM); `address` a multiple of 4 either way |
| size | The buffer's bytes from `address`; the inline buffer's must end within its 16 MB |
| stride | Bytes from one vertex to the next, 0-1024, a multiple of 4 |
| version | Names the buffer's contents: the GPU may keep a copy of a buffer at a GPU address and read it again only when its address, size or version changes (the inline buffer is always current). A version is never used twice for the same address, even when the memory is freed and given to another buffer: the driver draws versions from one counter for the whole system |

**1Ch INDICES** `format`, `address`, `size`, `version`. Sets the index buffer for indexed draws: `format` bits 7:0 the index size in bytes (2 or 4), bit 8 a GPU address source (else the inline buffer, `address` a byte offset in it, a multiple of the index size); `size` and `version` as for STREAM.

**1Dh INLINE** `offset`, then data dwords. Writes the data into the inline buffer at byte `offset` (a multiple of 4), wholly inside it (else ERROR_CODE 3). Vertices and indices go there exactly as the guest holds them.

**1Eh DRAW** `control`, `start`, `count`, `base`, then edge flags.

| control bits | Meaning |
|---|---|
| 7:0 | 1 point list, 2 line list, 3 line strip, 4 triangle list, 5 triangle strip, 6 triangle fan |
| 8 | INDEXED: the vertices come through the index buffer |
| 9 | EDGES: edge flags follow (triangle topologies only; with another, ERROR_CODE 3) |

- Without INDEXED, the primitives use vertices `start` to `start + count − 1`.
- With INDEXED, they use indices `start` to `start + count − 1` of the index buffer, and index value *i* names vertex `base + i` (`base` a signed 32-bit number).
- `count` is at most 100000h (1M; else ERROR_CODE 3). Vertex numbers run from 0 to FFFFFEh: a vertex numbered below 0 or from FFFFFFh up reads every element as 0 (the driver's largest vertex index, as DirectX 8 reports it, is below this).
- Edge flags are 4 bits per triangle, eight triangles per dword, starting in the low bits; in each triangle's 4 bits, bit 0 is edge first–second, bit 1 second–third, bit 2 third–first, of the triangle as assembled (below). They matter only in wireframe fill mode, where only flagged edges are drawn; without EDGES every edge is drawn.
- A vertex element, or an index, that lies past the end of its buffer reads as 0: a program's out-of-range index draws something undefined but never stops the GPU.
- A draw reads at most 64 MB of its streams at GPU addresses (a host limit far above any real draw: 1M vertices of 64 bytes); vertices past it read as 0. The bytes it reads there (those of the vertices from the first it uses to the last, within the stream's size) must be mapped, as must the indices an indexed draw reads (else ERROR_CODE 7 or 8).
- A declaration, the streams it uses and, for INDEXED, the index buffer must have been set (else ERROR_CODE 3).

### Fixed-function state

These packets carry Direct3D 7's transform and lighting state as the program set it; they matter for vertices with a POSITION element. After RESET every matrix is the identity, every light slot is a directional light with diffuse (1, 1, 1, 0) and direction (0, 0, 1), no light is enabled, the material is all zero and the clip planes are (0, 0, 0, 0).

**1Fh TRANSFORM** `which`, then a matrix. Sets one of the GPU's matrices: `which` 0-3 the world matrices 0-3, 4 the view matrix, 5 the projection matrix, 8-15 the texture matrices of stages 0-7; 256-511 the world matrix palette of command set 2.1 ([below](#indexed-vertex-blending-and-tweening); ERROR_CODE 5 without PROFILE.FIXED8); any other value is rejected (ERROR_CODE 3). The shared header's `VGPU98_XFORM_VALID` names the slots: the GPU's check and the driver's loop over the slots it sends both use it. These are the GPU's numbers, not Direct3D's transform state types: the driver maps them.

**20h LIGHT** `slot`, then 26 dwords: Direct3D 7's D3DLIGHT7. Sets light slot 0-7 (another slot is rejected, ERROR_CODE 3).

| Dwords | Contents |
|---|---|
| 1 | Type: 1 point, 2 spot, 3 directional (else ERROR_CODE 3) |
| 4, 4, 4 | Diffuse, specular and ambient colours, (r, g, b, a) floats |
| 3, 3 | Position and direction, (x, y, z) floats in world space |
| 7 | Range, falloff, attenuation 0, 1 and 2, theta, phi (floats) |

**21h LIGHTENABLE** `mask`. Bits 0-7 enable light slots 0-7 (other bits ignored).

**22h MATERIAL** diffuse, ambient, specular and emissive (each (r, g, b, a) floats), then the specular power (float). Direct3D 7's D3DMATERIAL7.

**23h CLIPPLANE** `index`, `a`, `b`, `c`, `d` (floats). Sets user clip plane 0-5 (another index is rejected, ERROR_CODE 3), in world space: a point (x, y, z) is inside when `a·x + b·y + c·z + d ≥ 0`.

## Render states

STATE ids reuse Direct3D 7's numbers, and the values reuse its enumerations, so the driver passes them through. This page, not Direct3D's headers, defines what the GPU does with them. Every other id is rejected; the driver translates or drops the Direct3D states not listed here ([directx-hal.md](directx-hal.md)).

### Global states

| Id | Name | Values | Default |
|---|---|---|---|
| 4 | TEXTUREPERSPECTIVE | 0, 1: perspective-correct texture coordinates | 1 |
| 7 | ZENABLE | 0 off, 1 Z-buffer, 2 W-buffer | 0 |
| 8 | FILLMODE | 1 point, 2 wireframe, 3 solid | 3 |
| 9 | SHADEMODE | 1 flat, 2 Gouraud, 3 (Direct3D's phong) as Gouraud, as the reference rasterizer does | 2 |
| 14 | ZWRITEENABLE | 0, 1 | 1 |
| 15 | ALPHATESTENABLE | 0, 1 | 0 |
| 16 | LASTPIXEL | 0, 1: draw the last pixel of a line | 1 |
| 19 | SRCBLEND | 1-13 (below) | 2 (one) |
| 20 | DESTBLEND | 1-13 | 1 (zero) |
| 22 | CULLMODE | 1 none, 2 clockwise, 3 counter-clockwise | 3 |
| 23 | ZFUNC | 1-8 (below) | 4 (less or equal) |
| 24 | ALPHAREF | low 8 bits used | 0 |
| 25 | ALPHAFUNC | 1-8 | 8 (always) |
| 26 | DITHERENABLE | 0, 1 | 0 |
| 27 | ALPHABLENDENABLE | 0, 1 | 0 |
| 28 | FOGENABLE | 0, 1 | 0 |
| 29 | SPECULARENABLE | 0, 1: add the specular colour; with lighting, compute it | 0 |
| 34 | FOGCOLOR | colour (alpha ignored) | 0 |
| 35 | FOGTABLEMODE | 0 none (vertex fog), 1 exp, 2 exp², 3 linear | 0 |
| 36, 37, 38 | FOGSTART, FOGEND, FOGDENSITY | floats: table and vertex fog's | 0, 1, 1 |
| 41 | COLORKEYENABLE | 0, 1 | 0 |
| 47 | ZBIAS | 0-16 (larger values clamped) | 0 |
| 48 | RANGEFOGENABLE | 0, 1: vertex fog by distance from the eye rather than depth | 0 |
| 52 | STENCILENABLE | 0, 1 | 0 |
| 53, 54, 55 | STENCILFAIL, STENCILZFAIL, STENCILPASS | 1-8 (below) | 1 (keep) |
| 56 | STENCILFUNC | 1-8 | 8 (always) |
| 57 | STENCILREF | low 8 bits used | 0 |
| 58, 59 | STENCILMASK, STENCILWRITEMASK | 0-FFFFFFFFh (low 8 bits used) | FFFFFFFFh |
| 60 | TEXTUREFACTOR | colour | FFFFFFFFh |
| 128-135 | WRAP0-WRAP7 | bits 0-3: cylindrical wrapping of the first to fourth coordinate of texture coordinate set 0-7 (DirectX 7's WRAPCOORD_0-3; bit 0 is U, bit 1 V; other bits ignored) | 0 |
| 136 | CLIPPING | 0, 1: user clip planes apply (the view volume always clips) | 1 |
| 137 | LIGHTING | 0, 1 | 1 |
| 138 | EXTENTS | 0, 1: accepted, no effect | 0 |
| 139 | AMBIENT | colour: the global ambient light | 0 |
| 140 | FOGVERTEXMODE | 0 none, 1 exp, 2 exp², 3 linear | 0 |
| 141 | COLORVERTEX | 0, 1: the material sources below may take the vertex colours | 1 |
| 142 | LOCALVIEWER | 0, 1 | 1 |
| 143 | NORMALIZENORMALS | 0, 1 | 0 |
| 144 | COLORKEYBLENDENABLE | 0, 1: colour-keyed texels become transparent black, and the pixel is drawn; it takes precedence over COLORKEYENABLE | 0 |
| 145 | DIFFUSEMATERIALSOURCE | 0 the material, 1 the vertex's diffuse colour, 2 its specular colour | 1 |
| 146 | SPECULARMATERIALSOURCE | 0-2 | 2 |
| 147 | AMBIENTMATERIALSOURCE | 0-2 | 2 (DirectX 7's documented default) |
| 148 | EMISSIVEMATERIALSOURCE | 0-2 | 0 |
| 151 | VERTEXBLEND | 0 off, 1-3 weights (2-4 world matrices); 2.1: 255 tweening, 256 one matrix by index with weight 1 | 0 |
| 152 | CLIPPLANEENABLE | bits 0-5 enable user clip planes 0-5 (higher bits ignored) | 0 |
| 154 | POINTSIZE (2.1) | float: the size of points whose vertices carry none, in pixels, or with POINTSCALEENABLE in camera-space units (negative and NaN as 0) | 1.0 |
| 155 | POINTSIZE_MIN (2.1) | float: the smallest size a point is drawn at, at most 256 (larger values clamped; negative and NaN as 0) | 0.0 |
| 156 | POINTSPRITEENABLE (2.1) | 0, 1: texture coordinates spanning each point | 0 |
| 157 | POINTSCALEENABLE (2.1) | 0, 1: the size of transformed vertices' points scaled by distance | 0 |
| 158, 159, 160 | POINTSCALE_A, B, C (2.1) | floats: the scaling's factors (negative and NaN as 0) | 1.0, 0.0, 0.0 |
| 161 | MULTISAMPLEANTIALIAS (2.1) | 0, 1: accepted, no effect (single-sample targets) | 1 |
| 162 | MULTISAMPLEMASK (2.1) | any: accepted, no effect (single-sample targets) | FFFFFFFFh |
| 163, 164 | PATCHEDGESTYLE, PATCHSEGMENTS (2.1) | any: accepted, no effect (no patches) | 0, 1.0 |
| 166 | POINTSIZE_MAX (2.1) | float: the largest size a point is drawn at, at most 256 (larger values clamped; negative and NaN as 0) | 64.0 |
| 167 | INDEXEDVERTEXBLENDENABLE (2.1) | 0, 1: vertex blending through the matrices the vertex's BLENDINDICES name | 0 |
| 168 | COLORWRITEENABLE (2.1) | bits 0-3: write red, green, blue, alpha (higher bits ignored) | Fh |
| 170 | TWEENFACTOR (2.1) | float: tweening's factor | 0.0 |
| 171 | BLENDOP (2.1) | 1 add, 2 subtract (source − destination), 3 reverse subtract (destination − source), 4 min, 5 max | 1 |
| 172, 173 | POSITIONORDER, NORMALORDER (2.1) | any: accepted, no effect (no patches) | 3 (cubic), 1 (linear) |

Comparison functions: 1 never, 2 less, 3 equal, 4 less or equal, 5 greater, 6 not equal, 7 greater or equal, 8 always.
Blend factors: 1 zero, 2 one, 3 source colour, 4 1 − source colour, 5 source alpha, 6 1 − source alpha, 7 destination alpha, 8 1 − destination alpha, 9 destination colour, 10 1 − destination colour, 11 source alpha saturated, 12 both source alpha (source factor source alpha, destination factor 1 − source alpha), 13 both inverse source alpha (the reverse).
Stencil operations: 1 keep, 2 zero, 3 replace, 4 increment with saturation, 5 decrement with saturation, 6 invert, 7 increment with wrap, 8 decrement with wrap.

### Texture stage states

Stage s (0-7) has ids `100h + 20h × s + n`, where n is the Direct3D 7 texture stage state number:

| n | Name | Values | Default |
|---|---|---|---|
| 1 | COLOROP | 1-24 (below) | stage 0: 4 (modulate); others: 1 (disable) |
| 2, 3 | COLORARG1, COLORARG2 | argument (below) | 2 (texture), 1 (current) |
| 4 | ALPHAOP | 1-24 | stage 0: 2 (select arg 1); others: 1 (disable) |
| 5, 6 | ALPHAARG1, ALPHAARG2 | argument | 2 (texture), 1 (current) |
| 7-10 | BUMPENVMAT00, 01, 10, 11 | floats | 0 |
| 11 | TEXCOORDINDEX | bits 2:0 the coordinate set; bits 17:16 generation: 0 none, 1 camera-space normal, 2 camera-space position, 3 camera-space reflection vector (other bits ignored) | the stage's number |
| 13, 14 | ADDRESSU, ADDRESSV | 1 wrap, 2 mirror, 3 clamp, 4 border | 1 |
| 15 | BORDERCOLOR | colour | 0 |
| 16 | MAGFILTER | 1 point, 2 linear, 5 anisotropic | 1 |
| 17 | MINFILTER | 1 point, 2 linear, 3 anisotropic | 1 |
| 18 | MIPFILTER | 1 none, 2 point, 3 linear | 1 |
| 19 | MIPMAPLODBIAS | float | 0 |
| 20 | MAXMIPLEVEL | the largest level used; values past the last level clamped | 0 |
| 21 | MAXANISOTROPY | 1-16 (0 is 1; larger values clamped) | 1 |
| 22, 23 | BUMPENVLSCALE, BUMPENVLOFFSET | floats | 0 |
| 24 | TEXTURETRANSFORMFLAGS | bits 7:0 COUNT: 0 off, 1-4 the coordinates the stage uses, above 4 counts as 4; bit 8 PROJECTED (with a COUNT of 2-4; ignored otherwise); other bits ignored | 0 |
| 25 | ADDRESSW (2.1) | as ADDRESSU, for a volume's third coordinate | 1 |
| 26, 27 | COLORARG0, ALPHAARG0 (2.1) | argument: A0 of MULTIPLYADD and LERP (the operations' table), besides arguments 1 and 2 | 1 (current) |
| 28 | RESULTARG (2.1) | 1 current, 5 temp: where the stage's result goes | 1 |

Arguments: bits 3:0 select 0 diffuse, 1 current (the previous stage's result; diffuse at stage 0), 2 texture, 3 texture factor, 4 specular (the specular colour as it reaches the pixel, whether or not SPECULARENABLE adds it at the end, with alpha 0: the reference rasterizer keeps no specular alpha per pixel), 5 (2.1) temp (the temporary register: (0, 0, 0, 0) until a stage writes it); bit 4 complements (1 − x); bit 5 replicates alpha into the colour channels. Other selector values are rejected.

Texture operations (each on colour or alpha, with A1 and A2 the arguments, results clamped to 0-1 except where noted). The colour-only operations (18-21, 24) are rejected as an ALPHAOP; the driver never sends them there ([directx-hal.md](directx-hal.md)):

| Op | Name | Result |
|---|---|---|
| 1 | DISABLE | this stage and the ones after it are skipped |
| 2, 3 | SELECTARG1, SELECTARG2 | A1; A2 |
| 4, 5, 6 | MODULATE, 2X, 4X | A1·A2; ×2; ×4 |
| 7 | ADD | A1 + A2 |
| 8, 9 | ADDSIGNED, 2X | A1 + A2 − 0.5; ×2 |
| 10 | SUBTRACT | A1 − A2 |
| 11 | ADDSMOOTH | A1 + A2 − A1·A2 |
| 12-14 | BLENDDIFFUSEALPHA, BLENDTEXTUREALPHA, BLENDFACTORALPHA | A1·α + A2·(1 − α), α from the diffuse colour, the texture, the texture factor |
| 15 | BLENDTEXTUREALPHAPM | A1 + A2·(1 − texture α) |
| 16 | BLENDCURRENTALPHA | A1·α + A2·(1 − α), α from the current colour |
| 17 | PREMODULATE | this stage's result is multiplied by the next stage's texture |
| 18, 19 | MODULATEALPHA_ADDCOLOR, MODULATECOLOR_ADDALPHA | A1.rgb + A1.a·A2.rgb; A1.rgb·A2.rgb + A1.a (colour only) |
| 20, 21 | MODULATEINVALPHA_ADDCOLOR, MODULATEINVCOLOR_ADDALPHA | (1 − A1.a)·A2.rgb + A1.rgb; (1 − A1.rgb)·A2.rgb + A1.a (colour only) |
| 22, 23 | BUMPENVMAP, BUMPENVMAPLUMINANCE | the stage does no blending and passes its input (current) through. Its texture's (dU, dV) offset the next stage's texture coordinates by (M00·dU + M10·dV, M01·dU + M11·dV), M being this stage's BUMPENVMAT. With luminance, the next stage's texture colour is also multiplied by clamp(L·LSCALE + LOFFSET, 0, 1) before that stage blends. A next stage that samples a cube map (or, 2.1, a volume) takes no offset (its coordinates are a direction, or three), but takes the luminance scale, as in the reference rasterizer |
| 24 | DOTPRODUCT3 | 4·Σ(A1 − 0.5)·(A2 − 0.5), summed over r, g and b, replicated into all four channels (colour only) |
| 25 | MULTIPLYADD (2.1) | A0 + A1·A2 |
| 26 | LERP (2.1) | A0·A1 + (1 − A0)·A2 |

## Command set 2.1

Command set 2.1 is DirectX 8's fixed function on 2.0's pipeline. Everything below needs PROFILE.FIXED8 ([gpu-registers.md](gpu-registers.md)): a GPU without it refuses each of these values (ERROR_CODE 5), so a driver that sends only 2.0's values sees a 2.0 GPU. The values are DirectX 8's numbers, as 2.0's are DirectX 7's. After RESET every state below has its default (the tables above).

### Vertex input

- **Types** UBYTE4, SHORT2 and SHORT4 (the declaration's table): a coordinate set of SHORT2 or SHORT4 pads as one of two or four floats does; a BLENDINDICES element is UBYTE4.
- **Uses:** PSIZE, a FLOAT1, is the vertex's point size (below). BLENDINDICES, a UBYTE4, holds the vertex's four matrix indices, the lowest-addressed byte the first. POSITION and NORMAL index 1 are tweening's second position and normal, with an untransformed (index 0) POSITION.

### Indexed vertex blending and tweening

- **The world matrix palette:** TRANSFORM slots 256-511 are world matrices 0-255. Slots 256-259 are the same matrices as world matrices 0-3 (slots 0-3): either number sets them. After RESET each is the identity.
- **Indexed blending** (INDEXEDVERTEXBLENDENABLE 1): with VERTEXBLEND k (1-3) and a BLENDINDICES element, the vertex blends k + 1 matrices as 2.0 blends world matrices 0 to k, but matrix j is world matrix `index j` of the vertex's indices; with VERTEXBLEND 256 it uses one matrix, its index 0, with weight 1. Without a BLENDINDICES element its indices are 0, 1, 2 and 3, as Direct3D 8 blends without indices. The camera-space normal is blended through each matrix's inverse transpose, as in 2.0. With INDEXEDVERTEXBLENDENABLE 0, VERTEXBLEND 256 is world matrix 0 with weight 1, and the indices are 0, 1, 2 and 3 as in 2.0.
- **Tweening** (VERTEXBLEND 255): the position is P0·(1 − t) + P1·t where the declaration has P1 (POSITION index 1), and the normal N0·(1 − t) + N1·t where it has N1 (NORMAL index 1), for t the TWEENFACTOR, before anything else; a declaration with one of them leaves the other as it is (DirectX 8 asks for a second position or a second normal). Then world matrix 0 alone, as without blending. Tweening and weights do not mix: VERTEXBLEND 255 takes no weights.
- With FIXED8, VERTEXBLEND 1-3 and 256 also apply to the user clip planes' world-space position, as in 2.0.

### Points

- **The size** of a point is its vertex's PSIZE when it has one, else POINTSIZE. With POINTSCALEENABLE and an untransformed vertex it is in camera-space units and becomes pixels as `S = H · Si · sqrt(1 / (A + B·d + C·d²))`, for the viewport's height H in pixels, the input size Si, POINTSCALE A, B and C, and the camera-space distance d of the vertex from the eye (a non-positive or NaN denominator gives the largest size, but an input size of 0 stays 0). Otherwise it is in pixels already, as are pre-transformed vertices' sizes always. The size is then clamped as DirectX 8 states it: to the largest size first (POINTSIZE_MAX, at most 256), then to the smallest (POINTSIZE_MIN, at most 256), the smallest winning where they cross.
- **A point** whose size is exactly 1, without POINTSPRITEENABLE, is a pixel as in 2.0. **Any other point** is the square of side S centred on its screen position, at the vertex's depth and W, drawn with the vertex's colours and fog at every pixel and the pixels whose centres lie inside it by the top-left rule. A square smaller than a pixel draws only the pixel centres it covers, so with POINTSIZE_MIN below 1 a small point may draw nothing (the DirectX 8.1 SDK's "drop out"); none is widened to a pixel.
- **Its texture coordinates** are the vertex's at every pixel, or with POINTSPRITEENABLE, the first two of every stage's coordinates go from (0, 0) at the square's top-left corner to (1, 1) at its bottom-right, the stage's other coordinates the vertex's (texture transforms apply before, to the vertex's; a stage's PROJECTED does not divide a sprite's coordinates, a pairing DirectX 8 leaves undefined).
- **Clipping:** a transformed point whose vertex lies outside the view volume's near or far plane, or outside an enabled user clip plane, is not drawn; one inside them is drawn whole, clipped only by the scissor (the viewport within the target), so a point partly outside the viewport draws its part inside. This is DirectX 8's rule for a card without D3DPMISCCAPS_CLIPPLANESCALEDPOINTS.
- Points of a point list and point fill (FILLMODE 1) are drawn so; wireframe and solid fill are unaffected.

### The pixel pipeline

- **BLENDOP** chooses the blend operation (the render states' table). With ALPHABLENDENABLE 0 no blending happens, whatever it says. Min and max compare the source and destination colours themselves, without SRCBLEND and DESTBLEND (the host's operations; DirectX 8.1 and 9 document them only as min and max of source and destination).
- **COLORWRITEENABLE** masks the channels written to the target; a target without alpha has none to write. CLEAR ignores it.
- **MULTIPLYADD and LERP** take a third argument besides arguments 1 and 2: A0 in the operations' table, COLORARG0 (ALPHAARG0 for alpha). Unlike DOTPRODUCT3 they may be alpha operations as well as colour ones, and they clamp their result to 0-1 like the others.
- **RESULTARG** sends a stage's result to the temporary register instead of current: the next stage's current is then the one before; the temp register keeps its value for later stages to read (argument 5). Every pixel's temp register starts at (0, 0, 0, 0). The colour that reaches fog and blending is current.

### Volume textures

- TEXTURE type 2 (above) binds a volume: a stage samples it with its first three coordinates, the third addressed by ADDRESSW, filtered within and between slices as 2D textures are within a level (MAGFILTER and MINFILTER as for them, anisotropic ones included, though DirectX 8's caps offer point and linear for volumes; mip levels by MIPFILTER), with BORDER addressing on all three coordinates. A cube map's and a volume's coordinates are both three: the texture's type decides. A volume has no colour key and no palette: TEXTURE's COLORKEY and PALETTE_ALPHA bits are ignored for it. Its levels go no further than 1 x 1 x 1 (more are refused, ERROR_CODE 3).
- A volume may not be drawn into: no TARGET names its bytes. A target over a volume's bytes is a surface of its own, and the volume sees what it wrote once those pixels are copied back to video memory ([gpu-coherency.md](gpu-coherency.md)).

### Render targets and formats

- **TARGET** also takes A1R5G5B5 as a colour target ([gpu-formats.md](gpu-formats.md)). A4R4G4B4 and X4R4G4B4 are textures only: no host attachment format has their bits in their order without an extension, and holding every such texture in a half-float image so it could also be drawn into would cost DirectX 7 programs memory; GeForce3-class cards did not draw into them either.
- **TEXTURE** also takes A8, X4R4G4B4, A4L4 and Q8W8V8U8.

## Command set 2.2

Command set 2.2 is DirectX 8's programmable vertex and pixel processing on 2.1's pipeline: **programs**, short straight-line sequences of the GPU's own instructions that take the place of the fixed-function transform and lighting (a vertex program) or of the texture stages' operations (a pixel program). Everything below needs PROFILE.PROGRAMS ([gpu-registers.md](gpu-registers.md)), which comes with FIXED8, as do 2.2's three surface formats, V16U16, G16R16 and A2W10V10U10 ([gpu-formats.md](gpu-formats.md)): a GPU without it refuses PROGRAM, BIND, CONSTANTS and DECLARE's INPUT use, and those formats in TEXTURE, BLT, FILL and UPLOAD (ERROR_CODE 5), so it is exactly command set 2.1. The instruction set is the GPU's, independent of any graphics API; the Direct3D driver translates DirectX 8's shader tokens into it ([directx-hal.md](directx-hal.md)).

### Packets

**30h PROGRAM** `id`, `kind`, `range`, then instructions of four dwords each (payload length 3 + 4n, n 0-256; else ERROR_CODE 2 or 3).

- `id` 1-4095 names the program; a PROGRAM with an id in use replaces it (a bound program replaced by one of the other kind is unbound: BIND's id for its old kind becomes 0). PROGRAM `id`, 0, 0 with no instructions frees the id (an id not in use is no error); a bound program freed is unbound likewise. Draws already sent keep the programs they were sent with.
- `kind`: 1 a vertex program, 2 a pixel program (anything else is refused, ERROR_CODE 3).
- `range`: for a pixel program, a float from 1 to 8, the bound its registers are clamped to (below); for a vertex program, 0.
- The GPU validates the whole program against the rules below before taking it; a program it refuses leaves the id as it was (ERROR_CODE 3, the failing dword named).
- Programs are the GPU's, not a context's: they stay until replaced, freed or RESET.

**31h BIND** `vertex`, `pixel`. Selects the programs the draws that follow run: a vertex program's id or 0, a pixel program's id or 0 (0 is the fixed function). An id that names no program of that kind is refused (ERROR_CODE 3). Vertex and pixel programs are independent: either may be bound with the other's fixed function.

**32h CONSTANTS** `kind`, `first`, then four floats per register (payload length 2 + 4n, n at least 1). Sets constant registers `first` to `first + n − 1` of the vertex (1) or pixel (2) programs' constant file: vertex 0-255, pixel 0-7 (else ERROR_CODE 3). A pixel constant is stored clamped to −1 to 1 (NaN as 0), the range DirectX 8 gives pixel shader constants; vertex constants are stored as sent. After RESET every constant is (0, 0, 0, 0).

**1Ah DECLARE** takes a use for programs: **8 INPUT**, index 0-15, any type (FLOAT1-FLOAT4, D3DCOLOR, UBYTE4, SHORT2, SHORT4): the element loads a vertex program's input register vn, n the index. A declaration with an INPUT element has only INPUT elements, no index twice (else ERROR_CODE 3); it needs no position. A DRAW with a vertex program bound needs a declaration of INPUT elements, and one without a vertex program a declaration without them (else ERROR_CODE 3).

### The instruction

| Dword | Bits | Contents |
|---|---|---|
| 0 | 7:0 | Opcode (below) |
| 0 | 11:8 | Destination file |
| 0 | 19:12 | Destination register |
| 0 | 23:20 | Write mask: bit 20 x, 21 y, 22 z, 23 w |
| 0 | 24 | Saturate: the result clamped to 0-1 |
| 0 | 27:25 | Shift, a signed 3-bit count −3 to 3 (100b refused): the result times 2^shift (pixel programs; 0 in vertex programs) |
| 0 | 28 | PROJECT, for SAMPLE and BEM only (below); 0 otherwise |
| 0 | 29 | NOCLAMP (pixel programs, an operation with a destination): the result is not clamped to the range, for a value that is a texture coordinate rather than a colour (DirectX 8's texture instructions compute their coordinates beyond the registers' range) |
| 0 | 31:30 | 0 |
| 1-3 | 3:0 | Source 0-2's file; a source the operation does not take is a dword of 0 |
| 1-3 | 11:4 | Its register |
| 1-3 | 19:12 | Its swizzle: bits 13:12 the component that goes to x, 15:14 to y, 17:16 to z, 19:18 to w (0-3 for x-w); E4h passes them as they are |
| 1-3 | 23:20 | Its modifier (below) |
| 1-3 | 24 | RELATIVE: the register plus the address register a0.x (vertex constants only) |
| 1-3 | 31:25 | 0 |

**Files** (V vertex programs, P pixel programs):

| File | Name | Registers | Read | Written |
|---|---|---|---|---|
| 0 | NONE | | an absent source | the destination of KILL and DEPTH, mask 0 |
| 1 | TEMP | 0-15 | V, P | V, P |
| 2 | INPUT | V: 0-15, the declaration's INPUT elements; P: 0 the diffuse colour, 1 the specular colour | V, P | |
| 3 | CONST | V 0-255, P 0-7 | V, P | |
| 4 | ADDR | 0 (a0) | as RELATIVE only | V, by MOVA only, mask x |
| 5 | COORD | P: 0-7, the coordinate sets as interpolated | P | |
| 6 | OUTPUT | V: 0 oPos, 1 oD0, 2 oD1, 3 oFog, 4 oPts, 5-12 oT0-oT7 | | V |
| 7 | STAGE | P: 0-7, a texture stage | the stage operand of SAMPLE, BEM and LUM, swizzle E4h and modifier 0 | |
| 8 | FIXED | 0 (0, 0, 0, 0), 1 (1, 1, 1, 1), 2 (0, 0, 0, 1), 3 (0.5, 0.5, 0.5, 0.5) | V, P | |

**Source modifiers**, applied after the swizzle: 0 none; 1 −x; 2 x − 0.5; 3 −(x − 0.5); 4 2x − 1; 5 −(2x − 1); 6 1 − x; 7 2x; 8 −2x; 9 DZ: x and y divided by z (each 1.0 where z is 0), z and w as they are; 10 DW: the same with w. 9 and 10 only on SAMPLE's coordinates and on a MOV in a pixel program; 11-15 are refused.

**Results:** an operation computes four components from its sources (read before anything is written); the shift multiplies them, then saturate clamps them to 0-1, then in a pixel program, without NOCLAMP, each is clamped to ±range; the components the mask names are written. The mask is not 0 (but for KILL and DEPTH, whose destination is NONE).

### Operations

`a`, `b`, `c` are sources 0-2 after swizzle and modifier; a scalar operation reads `a.x` and writes its result to every component.

| Opcode | Name | Sources | Result |
|---|---|---|---|
| 01h | MOV | a | a |
| 02h | ADD | a, b | a + b |
| 03h | SUB | a, b | a − b |
| 04h | MUL | a, b | a · b |
| 05h | MAD | a, b, c | a · b + c |
| 06h | DP3 | a, b | a.x·b.x + a.y·b.y + a.z·b.z in every component |
| 07h | DP4 | a, b | the four-component dot product in every component |
| 08h | LRP | a, b, c | a · b + (1 − a) · c |
| 09h | CND | a, b, c | per component, a > 0.5 ? b : c |
| 0Ah | CMP | a, b, c | per component, a ≥ 0 ? b : c |
| 0Bh | MIN | a, b | per component, a < b ? a : b |
| 0Ch | MAX | a, b | per component, a ≥ b ? a : b |
| 0Dh | SLT | a, b | per component, a < b ? 1 : 0 |
| 0Eh | SGE | a, b | per component, a ≥ b ? 1 : 0 |
| 0Fh | RCP | a | scalar: 1 for exactly 1, +∞ for 0, else 1/a.x |
| 10h | RSQ | a | scalar, of v = abs(a.x): 1 for exactly 1, +∞ for 0, else 1/√v |
| 11h | EXP | a | scalar: 2^a.x |
| 12h | LOG | a | scalar, of v = abs(a.x): the largest negative float for 0, else log2 v |
| 13h | EXPP | a | for s = a.x: (2^floor(s), s − floor(s), 2^s with the low 8 bits of its significand cleared, 1) |
| 14h | LOGP | a | for v = abs(a.x) not 0: (the exponent of v, v's significand as a number in 1-2, log2 v with the low 8 bits of its significand cleared, 1); for 0: (the largest negative float, 1, the largest negative float, 1) |
| 15h | FRC | a | per component a − floor(a) |
| 16h | LIT | a | (1, max(a.x, 0), a.x > 0 and a.y > 0 ? a.y^p : 0, 1) with p = a.w clamped to ±127.9961 |
| 17h | DST | a, b | (1, a.y · b.y, a.z, b.w) |
| 18h | MOVA | a | (vertex programs) a0.x = floor(a.x) as an integer; the destination ADDR 0, mask x |
| 20h | SAMPLE | a, b | (pixel programs) the texture of stage b (STAGE) sampled at coordinates a: a 2D texture at (a.x, a.y), a cube map or volume at (a.x, a.y, a.z), with the stage's addressing, filters, mip settings, border colour and anisotropy, its colour key as the fixed function applies it; a stage without a texture reads (0, 0, 0, 0). With PROJECT and the stage's TEXTURETRANSFORMFLAGS PROJECTED with a COUNT of 2-4, a is first divided by its component COUNT − 1, as the fixed function divides |
| 21h | BEM | a, b, c | (pixel programs) a bump transform by stage c's (STAGE) matrix: x = a.x + M00·b.x + M10·b.y, y = a.y + M01·b.x + M11·b.y, z = a.z, w = a.w; with PROJECT, a is first divided as SAMPLE divides it, by stage c's TEXTURETRANSFORMFLAGS (the fixed function's bump stage projects its coordinates before it adds the offset) |
| 22h | LUM | a, b, c | (pixel programs) all four components of a times the luminance b.z · LSCALE + LOFFSET of stage c (STAGE), the luminance clamped to 0-1 as the fixed function clamps it (DirectX 8 states no clamp; the choice follows the fixed function) |
| 23h | RFL | a, b | (pixel programs) the reflection of the eye vector b.xyz about the normal a.xyz: 2·(a·b)/(a·a)·a − b in x, y, z (−b where a·a is 0), w 0 |
| 24h | KILL | a | (pixel programs) the pixel is not drawn if any of a.x, a.y, a.z is below 0; the program runs to its end (the kill is applied with the colour key's) |
| 25h | DEPTH | a | (pixel programs) the pixel's depth becomes a.x / a.y (1 where a.y is 0), clamped to 0-1, in place of the interpolated one: the value tested and written as the depth buffer holds it (ZRANGE and ZBIAS do not apply to it; a fixed-point buffer rounds it to its grid). With ZENABLE 2 (W-buffering) it is ignored and the pixel keeps its W |

Other opcodes are refused (ERROR_CODE 3), as are operations of the other kind's, a source file the kind cannot read, a register past its file, RELATIVE but on a vertex CONST source, a destination the kind cannot write, and SAMPLE, BEM or LUM without a STAGE operand where the table puts it or with one elsewhere. A program holds at most one DEPTH.

### Vertex programs

- **Inputs.** Input register n holds the declaration's INPUT element of index n, as its type reads (FLOAT1 (x, 0, 0, 1), FLOAT2 (x, y, 0, 1), FLOAT3 (x, y, z, 1), FLOAT4; D3DCOLOR (r, g, b, a) 0-1; UBYTE4 four values 0-255; SHORT2 (x, y, 0, 1); SHORT4); a register without an element reads (0, 0, 0, 1).
- **Before the program** every TEMP is (0, 0, 0, 0), a0 is 0, and the outputs are oPos (0, 0, 0, 0), oD0 (1, 1, 1, 1), oD1 (0, 0, 0, 0), oFog 1, oPts the render state POINTSIZE, oTn (0, 0, 0, 1).
- **RELATIVE** constants: register + a0.x; one outside 0-255 reads (0, 0, 0, 0).
- **After the program:** oPos is the clip-space position (clipped, divided and mapped into the viewport as a transformed vertex's, [Vertex processing](#vertex-processing)); oD0 and oD1, clamped to 0-1, are the diffuse and specular colours; oT0-oT7 are stage 0-7's coordinates as they are (TEXCOORDINDEX, generation and the texture transform are not applied; PROJECTED still divides in the pixel stage); oFog, clamped to 0-1, is the fog factor (vertex fog, below), interpolated apart from the colours (oD1's alpha stays the specular colour's); oPts is the point size in pixels (not scaled by POINTSCALEENABLE), clamped as other sizes are ([Points](#points)).
- **What the fixed function does then:** lighting, vertex blending, tweening, generated coordinates, texture transforms and vertex fog's formulas are not applied. The user clip planes CLIPPLANEENABLE selects (with CLIPPING 1) are in clip space: a point is inside when plane · oPos ≥ 0. With FOGENABLE, oFog is the fog factor, whatever FOGTABLEMODE says (table fog needs the fixed function's W). WRAPn applies to stage n's coordinates (set n is stage n's).
- **Points:** a point list (or point fill) of a vertex program is drawn as squares ([Points](#points)) when the program writes oPts, POINTSIZE is not 1, POINTSPRITEENABLE is on, or the size limits (POINTSIZE_MIN above 1, the largest size below 1) move a size of 1; otherwise as pixels.

### Pixel programs

- **Before the program** every TEMP is (0, 0, 0, 0); INPUT 0 and 1 are the diffuse and specular colours as interpolated (flat with flat shading), clamped to 0-1, alpha included (the fog factor is interpolated apart: a vertex program's oFog, the fixed function's vertex fog, a pre-transformed vertex's specular alpha, which is also its specular alpha); COORD n is stage n's coordinates as interpolated, before any projection. A coordinate set the vertex supplies with fewer than four components, untransformed, reads its missing components as 0 and its fourth as 1 (DirectX 8's texcoord), not as the fixed function pads it for its texture transform; a set the texture transform makes (TEXTURETRANSFORMFLAGS COUNT 1-3) reads its components past COUNT likewise.
- **The colour** is TEMP 0 at the end, clamped to 0-1. Then fog (vertex fog's factor, or table fog), the alpha test, dithering, the stencil and depth tests and blending as for the fixed function. The texture stages' colour and alpha operations, their arguments and RESULTARG are not used, and SPECULARENABLE adds nothing.
- **The stages a program samples** (the STAGE operands it holds) are the stages whose textures the GPU binds for it, whatever their COLOROP; their sampling states (addressing, filters, LOD bias, largest level, anisotropy, border colour) and bump states apply as the operations say.
- **Gradients** of computed coordinates are taken where they are computed (the program runs as one sequence for the whole draw): a SAMPLE after BEM takes the bumped coordinates' slopes, where the fixed function's bump stage keeps the unbumped ones (within [Tolerance](#tolerance)).

### Limits and reset

A program has at most 256 instructions; the GPU holds programs for ids 1-4095, 256 vertex and 8 pixel constants. After RESET no program exists, BIND is (0, 0) and every constant is 0. W-buffering (ZENABLE 2) takes a vertex program's W from oPos's w, as for any transformed vertex.

## How a primitive is drawn

1. **Vertex fetch.** Each vertex the primitives use is read from its streams through the declaration.
2. **Vertex processing.**
   - A vertex with a POSITIONT element is in screen space already: (x, y) in pixels with pixel centres at whole numbers (pixel (0, 0) centred on (0.0, 0.0), as in Direct3D), z the depth 0 to 1, rhw = 1/w. Its colours and coordinates are used as they are, and it is not clipped (its x and y lie within the guard band, ±32768).
   - A vertex with a POSITION element is transformed, lit, fogged and given texture coordinates by the fixed-function pipeline (Vertex processing, below), then clipped and mapped into the viewport.
3. **Primitives.**
   - Strips draw triangles (i, i+1, i+2) for even i and (i, i+2, i+1) for odd i; fans draw (i+1, i+2, 0), the centre vertex last. Line strips draw (i, i+1).
   - Points are single pixels (command set 2.1 also draws squares: [Points](#points)).
   - Lines follow the reference rasterizer's rules, with LASTPIXEL deciding the final pixel.
   - Triangles cover the pixels whose centres are inside them, by the top-left rule.
   - Culling uses the screen-space winding, y pointing down.
   - FILLMODE draws vertices only, flagged edges only, or the whole triangle.
4. **Interpolation.**
   - Gouraud shading interpolates the diffuse and specular colours.
   - Flat shading uses the colour of the first vertex of each assembled primitive.
   - Colours, the fog factor and texture coordinates are interpolated with perspective correction when TEXTUREPERSPECTIVE is 1, and linearly in screen space when it is 0, for every vertex (the reference rasterizer gives transformed vertices an RHW of 1 for interpolation then).
   - WRAPn makes coordinate set n take the shorter way around between vertices.
5. **Texturing.**
   - Stages run from 0 until a COLOROP of DISABLE.
   - Each stage takes its coordinates (below), samples its texture (filter, mip level, address mode, border colour, LOD bias, largest level, anisotropy), then applies its colour and alpha operations.
   - With COLORKEYBLENDENABLE, a texel equal to its texture's colour key reads as transparent black (0, 0, 0, 0) and the pixel is drawn; otherwise, with COLORKEYENABLE, such a texel makes the pixel not drawn.
6. **Specular.** With SPECULARENABLE, the specular colour's RGB is added.
7. **Fog.**
   - Vertex fog (FOGTABLEMODE 0): the interpolated specular alpha f mixes colour·f + fog colour·(1 − f).
   - Table fog: f comes from the pixel's W by the linear, exp or exp² formula with the start, end and density states; when WRANGE is (1, 1) (an affine projection), from its depth clamped to 0-1 instead. This is the reference rasterizer's choice, made for every frame, not only when W-buffering.
8. **Alpha test**, then the **stencil test**, then the **depth test**, with the stencil operation chosen by which of them failed.
   - The depth tested and stored is z clamped to 0-1, or with ZENABLE 2 the pixel's W itself (below, "W buffering").
   - ZBIAS n moves the depth toward the viewer by n steps of the depth buffer's resolution: with ZENABLE 2, n × |wfar − wnear| / (2^bits − 1) in W, or n / 65535 of W itself when the range is infinite or empty (a 16-bit step at least, which interpolation does not swallow) (the reference rasterizer does not implement ZBIAS; this follows Direct3D's description: higher values appear in front of coplanar polygons with lower ones).
9. **Blending** (with 2.1's BLENDOP: the sum, either difference, or the per-channel minimum or maximum of source and destination, the factors ignored for minimum and maximum), then the write to the target in its format, rounded to the nearest value (within the host's conversion: see Tolerance), of the channels COLORWRITEENABLE (2.1) lets through.
   - On a target without alpha (X8R8G8B8, X1R5G5B5, R5G6B5), destination alpha reads as 1 in blending and the unused bits are written as 0, as the reference rasterizer does.
   - With DITHERENABLE on a 16-bit target, an ordered 4×4 dither is applied to the colour before blending. Direct3D leaves the dither pattern to the hardware.

### A stage's texture coordinates

1. **The source.** With a generation mode in TEXCOORDINDEX bits 17:16 (transformed vertices only), the vertex's camera-space normal, camera-space position or reflection vector (Vertex processing), three components. Otherwise the coordinate set TEXCOORDINDEX names, with as many components as its declaration element has.
2. **Padding.** The components are extended to four: 1 at the first missing one, 0 after it ((u, v) becomes (u, v, 1, 0)). A set the declaration lacks is (0, 0, 0, 0).
3. **The transform.** With a COUNT in TEXTURETRANSFORMFLAGS and a transformed vertex, the four components are multiplied by the stage's texture matrix (as a row vector). Pre-transformed vertices are never multiplied.
4. **Projection.** With PROJECTED (and a COUNT of 2-4), every component is divided, per pixel, by component COUNT − 1 of the result.
5. **Use.** A 2D texture takes the first two components; a cube map the first three, a direction from the cube's centre (the face the largest-magnitude component points to, as Direct3D defines cube maps). WRAPn applies to the components of set n its bits name, after the transform.

## Vertex processing

For a vertex with a POSITION element, as the reference transform and lighting does it. All arithmetic is in 32-bit floats; colours are worked in units of 0-255.

**Blending.** With VERTEXBLEND k (1-3) and a BLENDWEIGHT element of at least k weights, the vertex uses world matrices 0 to k, with weights b0 ... b(k−1) from the element and 1 − Σb for the last; with fewer weights in the element, only as many as it has; otherwise world matrix 0 with weight 1. Each world matrix's result is weighted and summed:

- the clip-space position Σ bj · (P · Worldj · View · Projection);
- the camera-space position Σ bj · (P · Worldj · View);
- the camera-space normal Σ bj · (N · ((Worldj · View)⁻¹)ᵀ), using the upper 3×3 of the inverse's transpose; a Worldj · View without an inverse (determinant 0) transforms normals by the identity instead (the reference T&L keeps the last inverse it had, which depends on history);
- with NORMALIZENORMALS, the camera-space normal is then normalized;
- a vertex without a NORMAL element has the normal (0, 0, 0) in every formula below.

**Clipping.** The clip-space position (x, y, z, w) is inside the view volume when −w ≤ x ≤ w, −w ≤ y ≤ w and 0 ≤ z ≤ w. Primitives are clipped to it, and, with CLIPPING 1, to each user plane CLIPPLANEENABLE selects (the world-space position, blended as above, inside the plane). Clipping interpolates every attribute linearly in clip space.

**The viewport.** After clipping, the screen position is `x_s = X + (1 + x/w) · W/2`, `y_s = Y + (1 − y/w) · H/2`, `z_s = minz + (z/w) · (maxz − minz)`, rhw = 1/w, for the viewport (X, Y, W, H) and ZRANGE (minz, maxz).

**Lighting.** With LIGHTING 1:

- The material sources: with COLORVERTEX 1, each of the ambient, diffuse, specular and emissive colours is the material's or, as its source state says, the vertex's diffuse or specular colour, when the vertex has that colour; otherwise the material's. Vertex colours count as their bytes (0-255), material colours as their values × 255, light colours and the global AMBIENT (its bytes / 255) as they are.
- The diffuse result starts as AMBIENT · Ma + Me. Each enabled light adds, for a vertex at camera-space position V with normal N:
  - **Directional:** L is the reverse of the light's direction in camera space, normalized. The ambient term La · Ma always; then, with a normal and N·L > 0, the diffuse term Ld · Md · (N·L) and, with SPECULARENABLE, the specular term.
  - **Point and spot:** D = the light's camera-space position − V. A vertex at or beyond the light's range (|D|² ≥ range²), or exactly at the light, gets nothing from it. The attenuation is 1 / (a0 + a1·|D| + a2·|D|²) (a zero denominator counts as the largest float). For a spot, with ρ = D·S/|D| (S the reverse of its direction in camera space, normalized): nothing at ρ ≤ cos(φ/2); the full light at ρ ≥ cos(θ/2); between them the attenuation is multiplied by ((ρ − cos(φ/2)) / (cos(θ/2) − cos(φ/2)))^falloff (no power when falloff is 1). The ambient term att · La · Ma; then, with a normal and N·D > 0, the diffuse term Ld · Md · (N·D)/|D| · att and, with SPECULARENABLE, the specular term times att.
  - **The specular term:** H = normalize(L + E), with E = normalize(−V) with LOCALVIEWER and (0, 0, −1) without; with N·H at least the threshold 0.001^(1/power) (0 when the power is 0.001 or less), Ls · Ms · (N·H)^power.
- The diffuse colour is the result's channels rounded to whole numbers (to nearest, halves to even) and clamped to 0-255, with the alpha of the diffuse source (the material's diffuse alpha × 255, truncated and clamped to 0-255, or the vertex's diffuse or specular alpha). The specular colour likewise: with SPECULARENABLE, the specular result with the specular source's alpha; without it, the vertex's own specular colour, alpha included, when it has one; else RGB 0 with the specular source's alpha.
- With LIGHTING 0, the vertex's colours pass through (diffuse FFFFFFFFh and specular 0 when it has none).

**Vertex fog.** With FOGENABLE, a FOGVERTEXMODE other than 0 and FOGTABLEMODE 0, the specular colour's alpha becomes the fog factor, for d the camera-space z or, with RANGEFOGENABLE, the camera-space distance |V|: linear 255 for d < start, 0 for d ≥ end, else (end − d) · 255 / (end − start); exp e^(−density·d) · 255; exp² e^(−(density·d)²) · 255; rounded to a whole number as the colours are, of which the low 8 bits are kept (a factor above 255, from a negative distance or density, wraps, as in the reference T&L).

**Generated coordinates.** The camera-space normal (blended, normalized with NORMALIZENORMALS); the camera-space position; the reflection vector: with LOCALVIEWER, for E = normalize(V), E − 2(E·N)N; without, (−2·Nx·Nz, −2·Ny·Nz, 1 − 2·Nz²).

## Settled against the reference rasterizer

These details Direct3D's documentation leaves open; the GPU does what the reference rasterizer (DirectX 7 DDK) does.

- **Assembly order** and **flat shading** follow step 3 and 4 above. Edge flags and the flat-shading vertex refer to the assembled order. The vertex fog factor (specular alpha) is always interpolated.
- **Culling and degenerate triangles.** The winding is the determinant of the vertices snapped to a 1/16-pixel grid; a triangle with no area on that grid is never drawn, in any fill mode. CULLMODE 2 removes triangles with a positive determinant (clockwise on screen, y down), 3 those with a negative one.
- **Lines** (line primitives and wireframe edges) are rasterized by the reference rasterizer's rules: vertices snapped to 1/16 pixel, the major axis walked pixel by pixel, the minor coordinate rounded to nearest with halves down, the first pixel kept when the start vertex lies in its pixel's diamond or ahead of it, the last pixel kept when the line passes its pixel's diamond or, with LASTPIXEL set, ends inside it. In a line strip only the last line may draw its last pixel. Attributes are interpolated along the major axis, perspective-correctly, clamped to the segment.
- **Points** cover the pixel a one-pixel square centred on the vertex covers, by the top-left rule.
- **Interpolation.** Colours, the fog factor and texture coordinates are perspective-correct; with TEXTUREPERSPECTIVE 0 every attribute of every vertex is linear in screen space. Depth and W are linear in screen space (W is 1 / the interpolated RHW), with TEXTUREPERSPECTIVE 0 too: the reference rasterizer takes RHW as 1 then, so its W-buffer and W-based table fog are constant; the GPU keeps the real W (a deliberate difference).
- **CLIPPING 0** turns off the user clip planes only; the view volume still clips. The reference T&L does no clipping at all then, leaving vertices outside the view volume (behind the eye among them) to the rasterizer: a deliberate difference, since such vertices have no screen position to draw at.
- **Texture stages.** A stage with no texture bound reads transparent black (0, 0, 0, 0). Arguments are complemented first, then alpha is replicated; an alpha argument's complement affects alpha only. ALPHAOP DISABLE with a colour operation enabled passes the current alpha on. After a PREMODULATE stage, the next stage multiplies the current colour (and, for an alpha PREMODULATE, the current alpha) by its own texture before computing its arguments. Every stage's result is clamped to 0-1.
- **Bump mapping.** The bump stage samples its texture with its own filter as (du, dv, l); the next stage's coordinates are offset as defined above, and with luminance its texture colour (not alpha) is scaled before it blends.
- **Colour keys.** A texel matches when its raw bits (all of them, unused and alpha bits included) equal the key. With COLORKEYBLENDENABLE, matching texels read as (0, 0, 0, 0) before filtering; otherwise, with COLORKEYENABLE, a pixel is not drawn when any texel with a non-zero weight in any stage's filter matched. DXT textures have no colour key.
- **Fog formulas.** Exp: e^(−density·d); exp²: e^(−(density·d)²); linear: 1 at d ≤ start, 0 at d ≥ end, (end − d)/(end − start) between; the fog factor f then gives colour·f + fog colour·(1 − f). Vertex fog computes them per vertex in 0-255 and rounds (Vertex processing).
- **W buffering** compares and stores the pixel's W itself, with a 32-bit float's precision at every distance; an RHW of 0 is W at infinity (the far end), a negative RHW the near end. This is a deliberate difference: the reference rasterizer stores (W − wnear) / (wfar − wnear) on the buffer's 16- or 24-bit grid, whose precision falls with the range (with a far plane at 100,000, one step of a 16-bit buffer is 1.5 units) and collapses with an infinite one, where every W ties. The cards of the time either had no W-buffer or kept W in a floating-point form whose precision does not depend on the far plane, and the DDK leaves the scaling to the driver. Where the guest reads a W-buffer (a Lock, a 2D packet, a copy back) it finds the reference's form on its grid, with the W range at that time (an infinite range gives 0, as the reference's scale does; the far end, a clear to 1, reads as the largest value); depths outside the range keep their order instead of tying at its ends. (86Box on a host without 32-bit float depth formats stores the reference's form itself, RHW 0 at the far end there too: [gpu-formats.md](gpu-formats.md).)
- **Alpha test** compares alpha rounded to 8 bits with ALPHAREF.
- **Clear colours** are converted to the target's format by rounding each channel to nearest.
- **Lighting** works in 0-255 units and rounds each vertex's colours to whole numbers (Vertex processing), as the reference T&L does when it runs: its conversion (FTOI) uses the FPU's rounding mode, round to nearest (its source says it needs truncation, but nothing sets it). Two of its conversions are not copied, deliberately: a channel that overflows (with all three attenuation factors 0, the light's attenuation is the largest float) saturates at 255, where the reference's conversion gives 0; and a negative material alpha is 0, where the reference's unsigned conversion gives 255.

### Tolerance

Some results Direct3D never defined bit-exactly, and real cards differed in them: the choice of mip level and the anisotropic footprint, subpixel precision, the dither pattern and where dithering happens (here before blending; the reference rasterizer dithers the blended result), the rounding of a computed colour to the target's bits (Vulkan recommends round to nearest but does not require it), the last bits of transform and lighting arithmetic (the order of the sums, the precision of powers, exponentials and square roots on the host GPU), the position of vertices made by clipping (not snapped to the 1/16 grid, as clipped vertices are in the reference rasterizer) and, with TEXTUREPERSPECTIVE 0, their attributes (a triangle's keep the screen-linear value along the original edge, so clipping changes nothing in the image, where the reference interpolates them linearly in clip space; lines and wireframe edges, clipped by the GPU itself, take the reference's values, so at a clipping point an edge and its triangle may differ), WRAPn's shorter way taken before clipping rather than after it (the same result except for a coordinate that wraps between a vertex and the clipping point), generated texture coordinates at vertices made by clipping (interpolated from the vertices' coordinates; the reference rasterizer generates them from the interpolated camera-space normal and position), the filtering of a cube map across the edges of its faces (the host GPU blends in the neighbouring face's texels; the reference rasterizer clamps within the face), which pixels a transformed line draws where it crosses the viewport's edge (lines are clipped there by the scissor, not geometrically), and point fill of a transformed triangle that clipping cut (its own vertices inside the view volume are drawn, not the vertices clipping made, which the reference rasterizer draws as well). These follow the host GPU, within its precision. Everything else (formats, operations, tests, blending factors, fog formulas, lighting formulas, the order of operations) follows this page exactly.

## Command set 2.4

Command set 2.4 is the **Glide device**, for the Glide DLLs ([glide.md](glide.md)): **sessions**, each a Glide window's 16-bit buffers on the host GPU, its state registers, its texture memory and its gamma table, drawn into by Glide's own commands. It is a device of its own: no Glide packet reads or changes the state of the 3D packets above (2.0-2.2), their surfaces in video memory or their coherency ([gpu-coherency.md](gpu-coherency.md)), and none of theirs touches Glide's; the two share the ring, the command processor and, in 86Box, the host GPU's device and command buffers. Everything here needs PROFILE.GLIDE (with RENDER3D: [gpu-registers.md](gpu-registers.md)): a GPU without it refuses opcodes 40h-4Fh (ERROR_CODE 5), so it is exactly command set 2.3. The interface stays 3.3.

The values are in `card/vid_vgpu98_glcmd.h`, kept in the project and copied unchanged into 86Box, which the DLLs include too. Glide's enumerations keep Glide's own numbers (its manuals' and SDK headers', read for facts): GR_CMP_*, GR_BLEND_*, GR_COMBINE_*, GR_TEXFMT_*, GR_LFBWRITEMODE_*, GR_BUFFER_* and the rest. The device's arithmetic (a texture's layout in its memory, texel decoding, the W float and the fog table) is `card/vid_vgpu98_glcpu.h`, shared the same way ([glide.md](glide.md), Textures and The pixel pipeline).

### Packets

- **Every Glide packet's first payload dword is its session**, an ID the DLL chooses (non-zero; the Glide DLLs take their process's ID and a count of their opens: [glide.md](glide.md)). A packet for a session the device does not have is dropped.
- **A Glide command is never a ring error.** The ring checks its header as every packet's ([Opcode summary](#opcode-summary)); everything inside the payload is the Glide device's. A command it cannot take (too short for its opcode, a session it does not have, an argument outside the checks below, or any command while 3D is stopped on the host after a failure) is **dropped** and counted, the first 16 named in the log (`VGPU98 Glide: command 43h dropped:` why: [glide.md](glide.md), The device's diagnostics), and the GPU goes on with the next packet. The minimum lengths below are the device's, not the ring's.
- **Size.** The DLLs write no packet over F000h bytes (VGPU98_GL_PACKET_BYTES), within the ring's largest, 64 KB.
- **Order.** Glide packets execute in ring order with every other packet, each as if the one before had finished: a FENCE after them completes once their work is done (a READ's pixels in video memory, a SWAP's frame shown).

| Opcode | Name | Payload |
|---|---|---|
| 40h | OPEN | session, size, flags, staging offset, staging bytes |
| 41h | CLOSE | session |
| 42h | STATE | session, first register, values |
| 43h | DRAW | session, kind, count, vertices |
| 44h | CLEAR | session, colour, alpha, depth |
| 45h | SWAP | session, interval |
| 46h | TEXDATA | session, memory, address, bytes, data |
| 47h | PALETTE | session, TMU, first and count, entries |
| 48h | LFB_READ | session, buffer, x and y, width and height, format, offset, stride |
| 49h | LFB_WRITE | the same |
| 4Ah | GAMMA | session, 256 entries |
| 4Bh-4Fh | reserved | (dropped) |

### Sessions

**40h OPEN** `session`, `size`, `flags`, `staging offset`, `staging bytes`. Opens a session.

| Dword | Bits | Contents |
|---|---|---|
| size | 15:0, 31:16 | Width and height, 1-2048 each (VGPU98_GL_MAX_SIZE) |
| flags | 1:0 | Colour buffers, 1-3 (VGPU98_GL_OPEN_BUFFERS_MASK) |
| flags | 2 | AUX: an aux (depth) buffer |
| flags | 3 | LOWER_LEFT: the origin GR_ORIGIN_LOWER_LEFT at the start |
| staging offset, staging bytes | | The DLL's staging surface in video memory: every frame buffer transfer of the session stays inside it. Offset + bytes within video memory (0 and 0: no transfers) |

- Dropped: under 5 dwords; session 0; a width or height of 0 or over 2048; no colour buffers; a staging range past video memory.
- The session's **buffers** on the host GPU: each colour buffer a 16-bit 5:6:5 image, the aux buffer a 16-bit depth image holding the Voodoo's 16-bit depth values exactly; the colour buffers cleared to black, the aux buffer to FFFFh. Colour buffer 0 is the front. Its **texture memory**: each TMU's 8 MB (VGPU98_GL_TMU_BYTES), zero. Its registers at their defaults (below); its gamma table the identity.
- A session already open with that ID ends first (a program's second window). The device holds 4 sessions (VGPU98_GL_SESSIONS): with all taken, the one not on the screen with the fewest swaps ends for the new one (a process that ended without closing), said in a line. A session whose images the host GPU cannot make ends at once, said in a line.

**41h CLOSE** `session`. The session ends: its images and texture memory go; if the screen shows it, the desktop comes back ([glide.md](glide.md), Showing Glide). Dropped for a session the device does not have.

**A session ends** also at a RESET that holds the GPU for a mode change (HOLD_DRIVER), at a machine reset and at GPU_RESET ([gpu-registers.md](gpu-registers.md)), each said in a line. At a RESET holding it for a VGA mode (HOLD_VDD: a full-screen DOS session) the sessions are kept and the screen is given back, the session shown again at its next SWAP (Windows comes back in the same mode). RESET's other causes, a driver starting the GPU or restarting it after an error or after it made no progress, leave every session as it was, its registers, buffers and texture memory included; what the ring lost then, the DLL sends again ([glide.md](glide.md), Batches and the ring).

### The state registers

**42h STATE** `session`, `first`, then n values (n the length less 2, at least 1). Sets registers `first` to `first + n − 1` of the session's 72 (VGPU98_GS_COUNT): dropped unless `first` is below 72 and the run within them. Draws, clears and transfers read the registers as they stand.

| Register | Name | Bits | Contents | At OPEN |
|---|---|---|---|---|
| 0 | CLIP_MIN | 15:0, 31:16 | The clip window's first column and row, in the origin's coordinates | 0, 0 |
| 1 | CLIP_MAX | 15:0, 31:16 | Its end, exclusive | The session's width and height |
| 2 | RENDER_BUFFER | | GR_BUFFER_FRONTBUFFER 0 or BACKBUFFER 1: what draws and clears go into | 1 with two or three colour buffers, else 0 |
| 3 | MASKS | 0, 1, 2 | Writes of RGB, ALPHA (no alpha is kept), DEPTH | RGB |
| 4 | ORIGIN | | GR_ORIGIN_UPPER_LEFT 0, LOWER_LEFT 1: row 0 the top or the bottom | OPEN's LOWER_LEFT |
| 5 | COLOR_COMBINE | | The colour combine unit (a combine, below) | SCALE_OTHER by ONE of ITERATED, ITERATED |
| 6 | ALPHA_COMBINE | | The alpha combine unit | SCALE_OTHER by ONE of CONSTANT, CONSTANT |
| 7 | CONSTANT | | The constant colour, 0xAARRGGBB | FFFFFFFFh |
| 8 | BLEND | 7:0, 15:8, 23:16, 31:24 | Colour source factor, colour destination factor, alpha source factor, alpha destination factor (GR_BLEND_*) | ONE, ZERO, ONE, ZERO |
| 9 | ALPHA_TEST | 7:0, 15:8 | The function (GR_CMP_*), the reference | ALWAYS, 0 |
| 10 | ITRGB_LIGHTING | | 1: grAlphaControlsITRGBLighting | 0 |
| 11 | DEPTH | 7:0, 15:8, 31:16 | The mode (0 DISABLE, 1 ZBUFFER, 2 WBUFFER, 3 ZBUFFER_COMPARE_TO_BIAS, 4 WBUFFER_COMPARE_TO_BIAS), the function (GR_CMP_*), the bias (signed 16 bits) | DISABLE, LESS, 0 |
| 12 | FOG | 7:0, 8, 9 | The mode (0 off, 1 iterated alpha, 2 the table at 1/w, 3 iterated Z, 4 the table at the fog coordinate), MULT2, ADD2 | 0 |
| 13 | FOG_COLOR | | 0x00RRGGBB | 0 |
| 14 | CHROMA | | 1: chroma key on | 0 |
| 15 | CHROMA_VALUE | | The chroma key's colour, 0x00RRGGBB | 0 |
| 16 | CULL | | GR_CULL_DISABLE 0, NEGATIVE 1, POSITIVE 2: kept for the DLL, which culls; the device does not read it | 0 |
| 17 | DITHER | | GR_DITHER_DISABLE 0, 2x2 1, 4x4 2 | 2 |
| 18 | LFB_CONSTANT | 7:0, 31:16 | grLfbConstantAlpha, grLfbConstantDepth: kept, not read (frame buffer writes through the pixel pipeline, which would read them, are taken as plain writes) | FFh, 0 |
| 19 | | | Reserved | 0 |
| 20-35 | FOG_TABLE | | The fog table's 64 entries, four a register, the first in the low byte | 0 |
| 36-39 | | | Reserved | 0 |
| 40-50 | TMU0 | | TMU0's registers (below): VGPU98_GS_TMU(0) + n | |
| 51-55 | | | Reserved | 0 |
| 56-66 | TMU1 | | TMU1's: VGPU98_GS_TMU(1) + n | |
| 67-71 | | | Reserved | 0 |

Each TMU's registers:

| n | Name | Bits | Contents | At OPEN |
|---|---|---|---|---|
| 0 | TEX_ADDRESS | | The texture's start in the TMU's memory | 0 |
| 1 | TEX_INFO | | The texture's description (below) | 0 |
| 2 | TEX_COMBINE | 7:0, 15:8, 23:16, 31:24 | The TMU's combine: colour function, colour factor, alpha function, alpha factor (a combine's numbers, below) | ZERO |
| 3 | TEX_INVERT | 0, 1 | Colour inverted, alpha inverted | 0 |
| 4 | TEX_FILTER | 7:0, 15:8 | Minification, magnification: 0 point, 1 bilinear (GR_TEXTUREFILTER_*) | Point |
| 5 | TEX_CLAMP | 7:0, 15:8 | s, t: 0 wrap, 1 clamp, 2 mirror (GR_TEXTURECLAMP_*) | Clamp, clamp |
| 6 | TEX_MIPMAP | 7:0, 15:8 | The mode (0 none: the largest level alone; 1 nearest; 2 nearest, dithered), trilinear (grTexMipMapMode's lodBlend) | 0 |
| 7 | TEX_LOD_BIAS | | The LOD bias, a float | 0.0 |
| 8 | TEX_DETAIL | 7:0, 15:8, 23:16 | grTexDetailControl's bias (signed 8 bits), scale (0-7) and maximum (0-255 for 0 to 1.0) | 0, 1, 255 |
| 9 | | | Reserved | 0 |
| 10 | TEX_SOURCE | | 1 while a texture is set on the TMU | 0 |

**A combine** (VGPU98_GS_COMBINE): bits 7:0 the function, 15:8 the factor, 19:16 local, 23:20 other, 24 invert. A TMU's combine has a function and a factor for colour and for alpha, local its own texel and other the TMU upstream's output.

| Field | Values (Glide's GR_COMBINE_*) |
|---|---|
| Function | 0 ZERO, 1 LOCAL, 2 LOCAL_ALPHA, 3 SCALE_OTHER, 4 SCALE_OTHER_ADD_LOCAL, 5 SCALE_OTHER_ADD_LOCAL_ALPHA, 6 SCALE_OTHER_MINUS_LOCAL, 7 SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL (BLEND), 8 SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA, 9 SCALE_MINUS_LOCAL_ADD_LOCAL (BLEND_LOCAL), 10h SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA; any other as ZERO |
| Factor | 0 ZERO, 1 LOCAL, 2 OTHER_ALPHA, 3 LOCAL_ALPHA, 4 TEXTURE_ALPHA (in a TMU's combine: the detail factor), 5 TEXTURE_RGB (in a TMU's: the LOD fraction), 8 ONE; 9-0Dh one minus 1-5 (bit 3) |
| Local | 0 ITERATED, 1 CONSTANT, 2 DEPTH (the alpha combine's: Z's top 8 bits) |
| Other | 0 ITERATED, 1 TEXTURE, 2 CONSTANT |

**A texture's description** (VGPU98_GS_TEXINFO):

| Bits | Contents |
|---|---|
| 7:0 | The format (GR_TEXFMT_*): 0 RGB_332, 1 YIQ_422, 2 ALPHA_8, 3 INTENSITY_8, 4 ALPHA_INTENSITY_44, 5 P_8, 6 P_8 through the 6666 palette (the device's own number: the palette's alpha used), 8 ARGB_8332, 9 AYIQ_8422, 0Ah RGB_565, 0Bh ARGB_1555, 0Ch ARGB_4444, 0Dh ALPHA_INTENSITY_88, 0Eh AP_88 |
| 11:8, 15:12 | The smallest and largest levels, as log2 of their long side (0 for 1 texel to 8 for 256: Glide 3's numbers) |
| 19:16 | The aspect, log2 of width over height, −3 to 3, two's complement in 4 bits |
| 23:20 | The levels it holds: 1 even, 2 odd, 3 both (GR_MIPMAPLEVELMASK_*) |
| 27:24 | The memory it is in: 0 TMU0, 1 TMU1 |

A TMU whose TEX_SOURCE is 0, or whose description is not one of these (another format, the smallest level above the largest, the largest above 8, another level mask or memory), samples white.

### Drawing

**43h DRAW** `session`, `kind`, `count`, then `count` vertices of 16 floats (VGPU98_GL_VERTEX_DWORDS). `kind` 2 (TRIANGLES) draws threes; 0 (POINTS) and 1 (LINES) are reserved, and dropped: the DLLs make every point and line triangles, and strips, fans and polygons lists, in Glide's order. Dropped: under 3 dwords, another kind, a `count` of 0 or not a multiple of 3, more vertices than the packet holds, a RENDER_BUFFER that names no colour buffer, or no room on the host for its vertices.

| Float | Name | Contents |
|---|---|---|
| 0, 1 | X, Y | The screen position in pixels, in the origin's coordinates |
| 2 | OOZ | 65535/z: the Z buffer's value |
| 3 | OOW | 1/w: the W buffer's, fog's, and the TMUs' where the DLL gives them none of their own |
| 4-7 | R, G, B, A | The iterated colour, 0-255 each |
| 8, 9, 10 | SOW0, TOW0, OOW0 | TMU0's s/w and t/w (Glide's texel space: 256 for the long side) and its 1/w |
| 11, 12, 13 | SOW1, TOW1, OOW1 | TMU1's |
| 14 | FOG | Glide 3's fog coordinate (GR_PARAM_FOG_EXT), else 0 |
| 15 | | Padding |

The triangles go into the RENDER_BUFFER's colour buffer (and the aux buffer), within the clip window (an empty one draws nothing): x and y snapped to 1/16 of a pixel, every parameter interpolated linearly in screen space and s/w, t/w divided by their 1/w per pixel, as the Voodoo iterates them, then the pixel pipeline [glide.md](glide.md) (The pixel pipeline) defines, from the registers as they stand. The device does not cull (the DLLs do). A TMU with a texture samples it from its memory ([glide.md](glide.md), Textures).

**44h CLEAR** `session`, `colour` (0x00RRGGBB), `alpha`, `depth`. Clears the clip window of the RENDER_BUFFER's colour buffer to the colour, with MASKS' RGB bit, and of the aux buffer to `depth`'s low 16 bits as they are, with MASKS' DEPTH bit and depth buffering on (DEPTH's mode not 0); `alpha` is not kept (the buffers have no alpha). Dropped: under 4 dwords, or a RENDER_BUFFER that names no colour buffer.

**45h SWAP** `session`, `interval` (without it, 1). The colour buffer after the front becomes the front (with one colour buffer the front stays), then the GPU waits for `interval` vertical blanks since the last Glide swap and at least one from now (at most 4; 0 waits for none), as FLIP waits, a RESET giving the wait up; then the session's front buffer is shown ([glide.md](glide.md), Showing Glide). A FENCE after a SWAP completes once the frame is shown. With interface 3.6's PROFILE.VSYNC the interval first goes through VSYNC_CONTROL's Glide field ([gpu-registers.md](gpu-registers.md), The V-sync overrides).

**The buffers** a register or a transfer names: GR_BUFFER_FRONTBUFFER (0) the front; BACKBUFFER (1) the one after it (the front itself with one colour buffer); TRIPLEBUFFER (5) the one after that (only with three); AUXBUFFER (2) the aux buffer.

### Texture memory

**46h TEXDATA** `session`, `memory`, `address`, `bytes`, then the bytes, packed in dwords (4 + (bytes + 3) / 4 dwords). Writes them into a TMU's memory at `address`: memory 0 is TMU0's, 1 TMU1's (2, VGPU98_GL_MEM_UNIFIED, Glide 3's unified memory, is dropped). Every texture decoded from bytes it writes over is dropped, to be decoded again when next sampled. Dropped: under 4 dwords, another memory, `address` or `address + bytes` past 8 MB, or `bytes` past the packet.

**47h PALETTE** `session`, `TMU`, `first | count << 16` (bits 15:0 the first entry, 31:16 the count), then `count` entries, 0x00RRGGBB (for the 6666 format, 0xAARRGGBB). Sets entries `first` to `first + count − 1` of TMU0's palette (TMU 0), TMU1's (1) or both (2). Textures of a palette format are found by their palette's contents, so a palette change drops none. Dropped: under 3 dwords, another TMU, a `count` of 0, entries past 256 or past the packet.

### The frame buffer

**48h LFB_READ** and **49h LFB_WRITE** `session`, `buffer`, `x | y << 16`, `width | height << 16`, `format`, `offset`, `stride`. Move a rectangle of a buffer between the host GPU and video memory, in the staging surface the session's OPEN gave: the program's row r of the rectangle is at `offset + (y + r) × stride + x × pixel bytes`. Dropped: under 7 dwords; a format the device does not know; a buffer it does not have (TRIPLEBUFFER without three colour buffers, AUXBUFFER without an aux buffer or in another format than ZA16, a colour format on the aux buffer); an empty rectangle or one past the session; a stride over 8192 or below `(x + width) × pixel bytes`; rows outside the staging range, or the range outside video memory; a depth format without an aux buffer.

| Format bits | Contents |
|---|---|
| 7:0 | The pixels' format, a GR_LFBWRITEMODE_* (below) |
| 8 | LOWER_LEFT: row 0 is the buffer's bottom |
| 9 | CHANGED (WRITE): only the pixels that differ from what the last READ into the same rows wrote |
| 10 | PIPELINE (WRITE): through the pixel pipeline; taken as a plain write, said once |
| 12, 13 | SWAP_BYTES, SWAP_WORDS: grLfbWriteColorSwizzle's swaps of 32-bit pixels |
| 17:16 | grLfbWriteColorFormat of 32-bit pixels: 0 ARGB, 1 ABGR, 2 RGBA, 3 BGRA |

| Format | Bytes | A pixel |
|---|---|---|
| 0 (565) | 2 | The colour, 5:6:5 |
| 1 (555) | 2 | The colour, 5:5:5, bit 15 0 |
| 2 (1555) | 2 | The colour, 5:5:5, bit 15 alpha |
| 4 (888) | 4 | The colour, 8:8:8, in the colour format |
| 5 (8888) | 4 | The colour and alpha, 8:8:8:8, in the colour format |
| 0Ch, 0Dh, 0Eh (565_DEPTH, 555_DEPTH, 1555_DEPTH) | 4 | The colour as 565, 555 or 1555 in bits 15:0, the depth in 31:16 |
| 0Fh (ZA16) | 2 | The aux buffer's 16-bit depth |

- **LFB_READ** writes the buffer's pixels, after every packet before it, into video memory in the format: 565 and the depth as the buffers hold them; 555 and 1555 without green's lowest bit (1555's alpha 1); 888 and 8888 each channel widened by repeating its top bits, alpha FFh, through the colour format and the swaps. The device keeps what it wrote, for a WRITE with CHANGED.
- **LFB_WRITE** writes the pixels into the buffer: the colour formats into the colour buffer, ZA16 into the aux buffer, the depth formats into both. 555 and 1555 become 565 with green's lowest bit its highest; 888 and 8888 are swapped (bytes, then words), read by the colour format and cut to 5:6:5; alpha is not kept. With CHANGED, after a READ of this buffer in this format into the same rows (the same offset and stride), a row or a pixel inside the READ's rectangle that the program left as the READ wrote it keeps the buffer's exact value, so a format narrower than the buffer's changes nothing the program did not touch; the READ's record then goes, so a second such WRITE takes every pixel. Without CHANGED every pixel is written.

### Gamma

**4Ah GAMMA** `session`, then 256 entries, each red in bits 7:0, green in 15:8 and blue in 23:16: the session's gamma table, through which the screen shows it ([glide.md](glide.md), Showing Glide). The session on the screen is shown again at once through the new table. Dropped: under 257 dwords.

## Command set 2.5

Command set 2.5 is the **OpenGL device**, for the OpenGL ICD ([opengl.md](opengl.md)): **share groups** (a group's textures), **contexts** (each its state registers, 433 of them, 495 from 2.6, its group and the drawable it draws into) and **drawables** (a window's images on the host GPU: front, back and depth/stencil), drawn into by GL 1.1's operations and written into the window's visible rectangles in video memory. It is a device of its own: no 2.5 packet reads or changes the state of the 3D packets above (2.0-2.2) or of the Glide device (2.4), and none of theirs touches OpenGL's; they share the ring, the command processor, the host GPU's device and command buffers, the pipeline cache's file, and video memory, whose coherency the device's writes follow ([gpu-coherency.md](gpu-coherency.md)). Everything here needs PROFILE.OGL (with RENDER3D: [gpu-registers.md](gpu-registers.md)): a GPU without it refuses opcodes 50h-5Fh (ERROR_CODE 5), so it is exactly command set 2.4. The interface stays 3.3.

The values are in `card/vid_vgpu98_oglcmd.h`, kept in the project and copied unchanged into 86Box, which the ICD includes too. They are the device's own small numbers, not GL's enums: the ICD turns GL's state into them ([opengl.md](opengl.md), Batches and the ring). Matrices are 16 floats in GL's column-major order (a column vector multiplied on the right), unlike the Direct3D order of the packets above. Floats are IEEE single-precision bit patterns.

### Packets

- **Every 2.5 packet's first payload dword names its object**: a share group (GROUP, TEXIMAGE, TEXPARAM, TEXDELETE, TEXREAD), a context (CONTEXT, BIND, STATE, DRAW, CLEAR, PIXELS, READ, and 2.6's PALETTE, whose texture is the context's group's) or a drawable (DRAWABLE, SWAP). IDs are the ICD's, non-zero and unique among the live objects of their kind (the ICD folds its process's ID into the high 16 bits and counts in the low 16: [opengl.md](opengl.md), The device's objects); a texture's ID is its group's own. A packet for an object the device does not have is dropped (a TEXDELETE of such a texture does nothing).
- **An OpenGL command is never a ring error.** The ring checks its header as every packet's ([Opcode summary](#opcode-summary)); everything inside the payload is the OpenGL device's. A command it cannot take (too short for its opcode, an object it does not have, an argument outside the checks below, or any command while 3D is stopped on the host after a failure) is **dropped**, counted, the first 16 named in the log (`VGPU98 OpenGL: command 58h (TEXIMAGE) dropped:` why, at every Diagnostics level but Off; the rest counted by opcode in the report at **OpenGL**: [opengl.md](opengl.md), Diagnostics), and the GPU goes on with the next packet. The minimum lengths below are the device's, not the ring's. A well-formed command with nothing to act on (no drawable bound, a drawable without images, an empty scissor box or viewport) does nothing, and is not counted; READ alone is dropped without a drawable bound.
- **Size.** The ICD writes no packet over F000h bytes (VGPU98_OGL_PACKET_BYTES), within the ring's largest, 64 KB; DRAW's vertices, its indices and PIXELS' data are each refused past F000h bytes.
- **Order.** OpenGL packets execute in ring order with every other packet, each as if the one before had finished: a FENCE after them completes once their work is done (a READ's or TEXREAD's rows in video memory, a SWAP's SHOW written).
- **Use.** Each packet stamps the objects it names with the device's count of packets: GROUP and the texture packets the group, DRAWABLE and SWAP the drawable, the others the context, its group and its drawable. When a kind is full, the least recently named object goes for a new one, so the objects of a process that ended without closing them are taken back.

| Opcode | Name | Payload |
|---|---|---|
| 50h | GROUP | group, op |
| 51h | CONTEXT | context, op, group |
| 52h | DRAWABLE | drawable, op, width and height, format |
| 53h | BIND | context, drawable |
| 54h | STATE | context, first register, values |
| 55h | DRAW | context, control, vertices, indices, then the vertices and the indices |
| 56h | CLEAR | context, mask, red, green, blue, alpha, depth, stencil |
| 57h | SWAP | drawable, flags, offset, pitch, bpp, place, rectangles, then each rectangle |
| 58h | TEXIMAGE | group, texture, control, size, place, base format, texels |
| 59h | TEXPARAM | group, texture, wraps, filters, border colour |
| 5Ah | TEXDELETE | group, texture |
| 5Bh | PIXELS | context, kind, size, x, y, z, zoom x, zoom y, colour, texture coordinates, fog distance, data |
| 5Ch | READ | context, what, x and y, width and height, offset, stride |
| 5Dh | TEXREAD | group, texture, level, offset, stride, first row and rows |
| 5Eh | PALETTE (2.6) | context, texture, base format, width, entries |
| 5Fh | reserved | (dropped; 5Eh too on a 2.5 device) |

### Objects

**50h GROUP** `group`, `op`. `op` 1 (VGPU98_OGL_OPEN) opens a share group; 0 (VGPU98_OGL_CLOSE) closes one, its contexts and textures ending with it. Dropped: under 2 dwords; a CLOSE of a group the device does not have; another op, or group 0. An OPEN of a group the device has changes nothing (the ICD sending its objects again after a GPU restart). The device holds 32 groups (VGPU98_OGL_GROUPS); with all taken, the least recently named ends, its contexts and textures with it.

**51h CONTEXT** `context`, `op`, `group`. OPEN opens a context in the group, its registers at the device's defaults (the table below) until the ICD's first STATE, which carries them whole; CLOSE ends it (`group` is not read, its dword still required). Dropped: under 3 dwords; a CLOSE of a context the device does not have; another op, context 0, or a group the device does not have; no memory for it. An OPEN of a context the device has puts it in the group named and keeps its registers. The device holds 64 contexts (VGPU98_OGL_CONTEXTS); with all taken, the least recently named ends.

**52h DRAWABLE** `drawable`, `op`, `width | height << 16`, `format`. OPEN makes a drawable; 2 (VGPU98_OGL_RESIZE) gives one a new size and format; CLOSE ends it, and the contexts bound to it have none (its size and format dwords are not read, but required). Dropped: under 4 dwords; a CLOSE or RESIZE of a drawable the device does not have; another op; drawable 0; a width or height of 0 or over 2048 (VGPU98_OGL_MAX_SIZE); a format not of the table below. An OPEN of a drawable the device has, with its size, format and images, changes nothing; otherwise it is a RESIZE. The device holds 32 drawables (VGPU98_OGL_DRAWABLES); with all taken, the least recently named ends.

| Format bits | Contents |
|---|---|
| 3:0 | The colour: 1 RGB 5:6:5 (a 16-bit desktop's pixels), 2 XRGB 8:8:8:8 (a 32-bit desktop's; alpha reads 1), 3 ARGB 8:8:8:8 (8 alpha bits kept) |
| 7:4 | The depth and stencil: 0 none, 1 16-bit depth, 2 24-bit depth and 8-bit stencil |
| 8 | DOUBLE: a back buffer |

The drawable's **images** on the host GPU, each of its size: the front image, the back image with DOUBLE, and the depth image with a depth kind. At OPEN and RESIZE they are made anew and cleared, colour to black with alpha 1, depth to 1, stencil to 0 (GL leaves a resized drawable's contents undefined). 16-bit depth is kept exactly; 24-bit depth in D24S8, or on a host without it in D32F with S8, its values on the 24-bit grid ([opengl.md](opengl.md), Depth). A drawable whose images the host GPU cannot make stays without them, its DRAWABLE counted as dropped, and what is drawn into it does nothing.

**53h BIND** `context`, `drawable` (0: none). The drawable the context draws into, clears and reads. Dropped: under 2 dwords; a context the device does not have; a non-zero drawable it does not have.

### The state registers

**54h STATE** `context`, `first`, then n values (n the length less 2, at least 1). Sets registers `first` to `first + n − 1` of the context's 433 (VGPU98_OGL_R_COUNT_25; 2.6: 495, VGPU98_OGL_R_COUNT, texture unit 1's block and the point parameters after the 433: [Command set 2.6](#command-set-26)). Dropped: under 3 dwords; a context the device does not have; `first` not below the count, or the run past the last register. Draws, clears, pixel rectangles and read-backs read the registers as they stand. Each is a dword, a float as its bits where the table says so. What GL computes when a command is called (the modelview's inverse transpose, light positions and spot directions, the eye and clip planes in eye coordinates), the ICD has computed.

| Register | Name | Contents | At OPEN |
|---|---|---|---|
| 0-15 | MODELVIEW | The modelview matrix, 16 floats | Identity |
| 16-31 | PROJECTION | The projection matrix | Identity |
| 32-47 | TEXTURE | The texture matrix | Identity |
| 48-59 | NORMAL | The modelview's inverse transpose (of its upper 3x3): three columns of x, y, z (floats) and 0 | Identity |
| 60 | ENABLES | VGPU98_OGL_EN_* (below) | DITHER |
| 61 | LIGHTS_ON | A bit a light, 0-7 | 0 |
| 62 | CLIPS_ON | A bit a user clip plane, 0-5 | 0 |
| 63 | SHADE | 0 flat, 1 smooth | 1 |
| 64 | FACE | Bit 0 the front face (0 counter-clockwise, 1 clockwise); bits 5:4 the faces culled (1 front, 2 back, 3 both) | 0 |
| 65-68 | MODEL_AMBIENT | The light model's ambient colour, 4 floats | 0 |
| 69 | MODEL_FLAGS | Bit 0 local viewer, bit 1 two-sided lighting | 0 |
| 70 | COLOR_MATERIAL | Bits 1:0 the faces (1 front, 2 back, 3 both); bits 7:4 which part (VGPU98_OGL_CM_*, below) | 0 |
| 71 | | Reserved | 0 |
| 72-91 | MATERIAL (front) | VGPU98_OGL_R_MATERIAL(0) + 0 ambient, + 4 diffuse, + 8 specular, + 12 emission (4 floats each), + 16 shininess (a float); + 17 to + 19 reserved | 0 |
| 92-111 | MATERIAL (back) | VGPU98_OGL_R_MATERIAL(1), likewise | 0 |
| 112-303 | LIGHT 0-7 | VGPU98_OGL_R_LIGHT(i) = 112 + 24i: + 0 ambient, + 4 diffuse, + 8 specular (4 floats each), + 12 position (4 floats, eye coordinates), + 16 spot direction (3 floats, eye coordinates), + 19 spot exponent, + 20 spot cutoff (degrees; 180: not a spotlight), + 21 attenuation (3 floats: constant, linear, quadratic) | 0 |
| 304 | TEXGEN_MODE | s in bits 3:0, t 7:4, r 11:8, q 15:12: VGPU98_OGL_TG_* (below) | 0 |
| 305-320 | TEXGEN_OBJECT | The object planes of s, t, r and q, 4 floats each | 0 |
| 321-336 | TEXGEN_EYE | The eye planes, in eye coordinates | 0 |
| 337-360 | CLIP_PLANE | Six planes, 4 floats each, in eye coordinates | 0 |
| 361 | FOG_MODE | 0 linear, 1 exp, 2 exp2 | 0 |
| 362 | FOG_DENSITY | A float | 1.0 |
| 363 | FOG_START | A float | 0.0 |
| 364 | FOG_END | A float | 1.0 |
| 365-368 | FOG_COLOR | 4 floats | 0 |
| 369 | TEXTURE_ID | The texture drawn with: its ID in the context's group; 0 for none (texturing off, or the texture incomplete) | 0 |
| 370 | TEX_ENV_MODE | VGPU98_OGL_ENV_* (below) | 0 |
| 371-374 | TEX_ENV_COLOR | 4 floats | 0 |
| 375 | ALPHA_FUNC | A compare function (below) | 7 (ALWAYS) |
| 376 | ALPHA_REF | A float, 0-1 | 0.0 |
| 377 | DEPTH | Bits 2:0 the function; bit 8 depth writes | LESS, writes |
| 378-379 | DEPTH_RANGE | Near, far (floats) | 0.0, 1.0 |
| 380 | STENCIL | Bits 7:0 the function, 15:8 the reference, 23:16 the value mask, 31:24 the write mask | ALWAYS, 0, FFh, FFh |
| 381 | STENCIL_OPS | Bits 3:0 on a stencil fail, 7:4 on a depth fail, 11:8 on a pass: VGPU98_OGL_SOP_* (below) | KEEP, KEEP, KEEP |
| 382 | BLEND | Bits 7:0 the source factor, 15:8 the destination factor: VGPU98_OGL_BF_* (below) | ONE, ZERO |
| 383 | LOGIC_OP | 0-15, GL's order (GL_CLEAR 0 to GL_SET 15), which is Vulkan's | 3 (COPY) |
| 384 | COLOR_MASK | Red 1, green 2, blue 4, alpha 8 | 15 |
| 385-388 | SCISSOR | x, y, width, height: window coordinates, x and y signed | 0 |
| 389-392 | VIEWPORT | x, y, width, height, signed | 0 |
| 393 | POINT_SIZE | A float | 1.0 |
| 394 | LINE_WIDTH | A float | 1.0 |
| 395 | LINE_STIPPLE | Bits 15:0 the pattern, 31:16 the factor | FFFFh, 1 |
| 396-397 | POLYGON_OFFSET | Factor, units (floats) | 0 |
| 398 | DRAW_BUFFERS | Front 1, back 2 | 2 |
| 399 | READ_BUFFER | 0 front, 1 back | 1 |
| 400 | FOG_PER_PIXEL | 1: fog computed per fragment (kept, not read: always so) | 1 |
| 401-432 | POLYGON_STIPPLE | 32 rows of 32 bits, the bottom row first, bit 31 the leftmost pixel | All ones |
| 433-487 | UNIT1 (2.6) | Texture unit 1's registers ([Command set 2.6](#command-set-26)) | Its texture matrix the identity, the rest 0 |
| 488-494 | POINT_PARAMS, POINT_MIN, POINT_MAX, POINT_FADE, POINT_ATTEN (2.6) | The point parameters ([Command set 2.6](#command-set-26)) | 0, 0.0, 64.0, 1.0, 1.0, 0.0, 0.0 |

The device's defaults are not all GL's (GL's fog mode is exp, its light 0 is white, its materials are not 0): the ICD's first STATE after an OPEN carries every register as GL has it ([opengl.md](opengl.md), Batches and the ring).

| ENABLES bit | Name | Read by |
|---|---|---|
| 0 | LIGHTING | Lighting (ogl.vert) |
| 1 | COLOR_MATERIAL | Colour material (ogl.vert) |
| 2 | NORMALIZE | Normals normalized (ogl.vert) |
| 3 | FOG | Fog (ogl.frag) |
| 4 | ALPHA_TEST | The alpha test (ogl.frag) |
| 5 | DEPTH_TEST | The depth test (the pipeline) |
| 6 | STENCIL_TEST | The stencil test (the pipeline) |
| 7 | BLEND | Blending (the pipeline) |
| 8 | LOGIC_OP | The logic op, in place of blending, where the host has Vulkan's `logicOp` (the pipeline) |
| 9 | SCISSOR | The scissor box |
| 10 | CULL_FACE | Face culling (the pipeline) |
| 11, 12, 13 | OFFSET_FILL, OFFSET_LINE, OFFSET_POINT | Polygon offset for triangles, a polygon's edges, a polygon's vertices |
| 14 | POLY_STIPPLE | Polygon stipple (ogl.frag) |
| 15 | LINE_STIPPLE | Line stipple (ogl.frag) |
| 16 | POINT_SMOOTH | Smooth points (ogl.vert, ogl.frag) |
| 17 | LINE_SMOOTH | Smooth lines (ogl.vert, ogl.frag) |
| 18 | POLY_SMOOTH | Kept, not read (no polygon coverage) |
| 19 | DITHER | Kept, not read (no dithering) |
| 20 | TEXTURE_1D | The texture drawn with is one-dimensional: kept, not read (the device takes a texture's dimensions from its level 0) |
| 21 | TEXTURE1_1D (2.6) | Unit 1's texture drawn with is one-dimensional: kept, not read, as TEXTURE_1D |
| 22 | SHARED_PALETTE (2.6) | Index textures look their texels up in the context's shared palette, not their own (ogl.frag) |

| Values | Numbers |
|---|---|
| Compare functions (VGPU98_OGL_CMP_*: GL's GL_NEVER, 200h, to GL_ALWAYS, 207h, less 200h) | 0 NEVER, 1 LESS, 2 EQUAL, 3 LEQUAL, 4 GREATER, 5 NOTEQUAL, 6 GEQUAL, 7 ALWAYS |
| Stencil operations (VGPU98_OGL_SOP_*) | 0 KEEP, 1 ZERO, 2 REPLACE, 3 INCR (clamped), 4 DECR (clamped), 5 INVERT; another as KEEP |
| Blend factors (VGPU98_OGL_BF_*) | 0 ZERO, 1 ONE, 2 SRC_COLOR and 3 ONE_MINUS_SRC_COLOR (destination factors), 4 SRC_ALPHA, 5 ONE_MINUS_SRC_ALPHA, 6 DST_ALPHA, 7 ONE_MINUS_DST_ALPHA, 8 DST_COLOR and 9 ONE_MINUS_DST_COLOR (source factors), 10 SRC_ALPHA_SATURATE (a source factor); another as ONE. Alpha is blended by the same factors as colour |
| Colour material (VGPU98_OGL_CM_*) | 0 EMISSION, 1 AMBIENT, 2 DIFFUSE, 3 SPECULAR, 4 AMBIENT_AND_DIFFUSE |
| Texture generation (VGPU98_OGL_TG_*) | 0 off, 1 object linear, 2 eye linear, 3 sphere map (s and t alone) |
| Texture environment (VGPU98_OGL_ENV_*) | 0 MODULATE, 1 DECAL, 2 BLEND, 3 REPLACE |

### Drawing

**55h DRAW** `context`, `control`, `vertices` (nv), `indices` (ni), then nv vertices of 16 floats each (VGPU98_OGL_VERTEX_DWORDS; 2.6: 20 with DRAW_TEX1, VGPU98_OGL_VERTEX1_DWORDS), then the ni 16-bit indices, two a dword, the first in the low half: 4 + 16 × nv (20 × nv with DRAW_TEX1) + (ni + 1) / 2 dwords exactly.

| Control bits | Name | Contents |
|---|---|---|
| 3:0 | The kind | 0 POINTS (an index a point), 1 LINES (two a line, its provoking vertex the second, GL's), 2 TRIANGLES (three a triangle, its provoking vertex the first) |
| 8 | DRAW_CLIP | The vertices are clip coordinates, clipped, lit and textured already (the ICD's own path) |
| 9 | DRAW_STIPPLE_ON | LINES: each line's stipple counter starts at its first vertex's STIPPLE (else 0) |
| 10 | DRAW_POLYGON | LINES, POINTS: a polygon's edges or vertices, drawn by its polygon mode: polygon offset applies (OFFSET_LINE, OFFSET_POINT) |
| 11 | DRAW_TEX1 (2.6) | Each vertex is 20 floats, texture unit 1's coordinates last |

| Float | Name | Contents | With DRAW_CLIP |
|---|---|---|---|
| 0-3 | X, Y, Z, W | Object coordinates | Clip coordinates |
| 4-7 | R, G, B, A | The current colour as given | The final colour, 0-1 |
| 8-10 | NX, NY, NZ | The current normal | NX the fog distance; NY and NZ 0 |
| 11 | STIPPLE | 0 | For lines, the stipple counter at the vertex; (2.6) for points, the point's size when above 0 (else POINT_SIZE) |
| 12-15 | S, T, R, Q | The current texture coordinates (unit 0's) | The final ones |
| 16-19 | S1, T1, R1, Q1 (2.6, with DRAW_TEX1) | Unit 1's current texture coordinates | Unit 1's final ones |

- Dropped: under 4 dwords; another kind, or control bits other than these; no vertices or more than 65536; no indices, or a count not a multiple of the kind's (1, 2 or 3); a length other than the vertices' and indices' exactly; vertices or indices past F000h bytes each; an index not below nv.
- Drawn into the bound drawable's draw buffers (DRAW_BUFFERS, those the drawable has, each in a pass of its own; with none, one buffer through a colour mask of 0, so depth and stencil are still updated), within the scissor box: triangles through GL's viewport (VIEWPORT, DEPTH_RANGE; an empty viewport draws nothing), points and lines as quads in window coordinates in the whole drawable. Without DRAW_CLIP each vertex goes through GL 1.1's transform, lighting, texture coordinate generation and texture matrix (2.6: each unit's, unit 1's with DRAW_TEX1), the user's clip planes and the fog distance (its eye z), from the registers; then each fragment through the per-fragment operations. Device-path triangles are culled by FACE with CULL_FACE; DRAW_CLIP triangles are not (the ICD culled them). How each step is done: [opengl.md](opengl.md), Drawing.

**56h CLEAR** `context`, `mask`, `red`, `green`, `blue`, `alpha`, `depth` (floats), `stencil`. Clears the bound drawable within the scissor box (with SCISSOR; else the whole drawable):
- mask bit 0 (VGPU98_OGL_CLEAR_COLOR): the draw buffers' colour (DRAW_BUFFERS, those the drawable has), to the colour clamped to 0-1, through COLOR_MASK; alpha only in an ARGB drawable (1 in the others);
- bit 1 (VGPU98_OGL_CLEAR_DEPTH): the depth, to `depth` clamped to 0-1 (on D32F, put on the 24-bit grid), while DEPTH's writes bit is set;
- bit 2 (VGPU98_OGL_CLEAR_STENCIL): the stencil of a D24S8 drawable, to `stencil`'s low 8 bits, through the stencil write mask (STENCIL bits 31:24).

A colour mask that keeps some but not all of the drawable's components, or a stencil write mask that keeps some but not all of its 8 bits, is honoured by drawing a quad over the box through a pipeline with those masks. Depth and stencil are cleared once, however many colour buffers are. Dropped: under 8 dwords (VGPU98_OGL_LEN_CLEAR); mask bits other than these; a context the device does not have.

### Swaps

**57h SWAP** `drawable`, `flags`, `offset`, `pitch`, `bpp`, `x | y << 16`, `rectangles` (n), then n rectangles, each `x0 | y0 << 16`, `x1 | y1 << 16`: 7 + 2n dwords at least. `x | y << 16` is the window's client area's top-left corner on the screen, each a signed 16-bit value; each rectangle is on the screen, its right and bottom exclusive, inside the client area. `offset`, `pitch` and `bpp` name the screen as the ICD found it: the primary surface's offset in video memory, its pitch in bytes and its bits a pixel (16 or 32). The flags are done in this order, those given:

| Flag | Name | Does |
|---|---|---|
| 1 | LOAD | The front image takes the rectangles' pixels from video memory |
| 2 | COPY | A double-buffered drawable's back image is copied whole into its front image (SwapBuffers) |
| 4 | SHOW | The front image's pixels under the rectangles are written into video memory |
| 8 | OVERRIDE (interface 3.6) | With SHOW: the V-sync overrides may change its wait (VSYNC_CONTROL's OpenGL field, [gpu-registers.md](gpu-registers.md)); a SHOW without it waits as asked ([SWAP's swap interval](#swaps-swap-interval)) |
| 70h (bits 6:4) | INTERVAL (2.6) | With SHOW: the vertical blanks it waits for before writing, 0-4 (VGPU98_OGL_SWAP_INTERVAL_SHIFT 4, _MASK 70h, _MAX 4; [Command set 2.6](#command-set-26)) |

- Dropped: under 7 dwords; a drawable the device does not have; flags other than these (a 2.5 device's are 1, 2 and 4 alone, so it drops a SWAP with an INTERVAL); an INTERVAL above 4 (2.6); more than 1024 rectangles, or more than the packet holds; a `bpp` other than 16 or 32; a rectangle with a negative corner, empty, outside the client area (the drawable at the place given), whose right edge's bytes pass the pitch, or past video memory.
- LOAD and SHOW write nothing (COPY is still done) unless the screen named is the one shown: the visible mode on, its start `offset`, its pitch `pitch`, its bits a pixel `bpp`, and every rectangle's bottom within its height; and unless `bpp`'s bytes are the drawable's (2 for 565, 4 for 8888). So a swap recorded before a mode change goes to no screen.
- Video memory stays coherent ([gpu-coherency.md](gpu-coherency.md)): newer DirectX pixels in a rectangle are copied back before LOAD reads it or SHOW writes it, and SHOW marks the pages it wrote. SHOW reads the front image back, waits for the host GPU and copies the pixels out to memory of its own (2.6: with an INTERVAL, then waits for the vertical blanks), then writes the rectangles row by row under the display's layer lock when there is a display (its own setup can fail while the host GPU works), so the host's display composes all of the old frame or all of the new; a RESET since the packet was taken (a mode change: video memory laid out anew) writes nothing. A FENCE after a SWAP completes once SHOW has written. A drawable without images does nothing.

### Textures

**58h TEXIMAGE** `group`, `texture`, `control`, `width | height << 16`, `x | y << 16`, `base format`, then the texels: RGBA8 dwords, red in the low byte, a row after another from the first (t = 0); (2.6) for base format INDEX, bytes, four a dword, the first in the low byte, each row padded to a whole dword ((width + 3) / 4 dwords a row).

| Control bits | Name | Contents |
|---|---|---|
| 3:0 | Dimensions | 1 or 2 |
| 7:4 | Level | 0-11 (VGPU98_OGL_LEVELS, 12: 2048 down to 1) |
| 8 | BORDER | A border of one texel (level 0's makes the texture's) |
| 9 | DEFINE | The level's size, its border included, is given |
| 10 | TEXELS | With DEFINE: the texels for all of it follow |

| Base format | Components | How the ICD fills a texel |
|---|---|---|
| 1 | ALPHA | 0, 0, 0, A |
| 2 | LUMINANCE | L, L, L, FFh |
| 3 | LUMINANCE_ALPHA | L, L, L, A |
| 4 | INTENSITY | I, I, I, I |
| 5 | RGB | R, G, B, FFh |
| 6 | RGBA | R, G, B, A |
| 7 | INDEX (2.6) | An 8-bit index a texel, looked up in a palette ([Command set 2.6](#command-set-26)) |

- **With DEFINE** the level is defined at the size given, its texels the width × height that follow with TEXELS, else zero (the place is not read); level 0's dimensions, base format and border are the texture's. A texture the group does not have by that ID is made. Dropped: a length other than 6 + its texels' dwords; the device's 16384 textures in use (VGPU98_OGL_TEXTURES); all textures' texels past 512 MB of the host's memory; a one-dimensional level of a height other than 1.
- **Without DEFINE** the width × height texels replace a rectangle of a level defined before, at `x | y << 16`, the border included (GL's −1 is 0). Dropped: no such level, a level of the other kind (an index level for colours, or the reverse: 2.6), a rectangle past it, or a length other than 6 + its texels' dwords.
- **Dropped either way**: under 6 dwords; a group the device does not have; texture 0; control bits other than these; dimensions other than 1 or 2; a level past 11; a base format other than 1-6 (1-7 in 2.6); a width or height of 0 or over 2050 (VGPU98_OGL_MAX_SIZE and a border each side).
- The texels are kept in the host's memory as the truth; the texture's copy in the device's texel arena is made again at its next use (2.6: texels replaced without DEFINE in a copy that is up to date are patched into it instead: [Command set 2.6](#command-set-26)). The ICD decides completeness: a draw samples the levels defined from 0 on, one after another, at most level 0's chain down to 1 ([opengl.md](opengl.md), Texturing and the texel arena).

**59h TEXPARAM** `group`, `texture`, `wraps`, `filters`, then the border colour, 4 floats (8 dwords). A texture the group does not have by that ID is made.

| Dword | Bits | Contents |
|---|---|---|
| wraps | 3:0, 7:4 | s, t: 0 REPEAT, 1 CLAMP, 2 clamp to edge (2.7, VGPU98_OGL_WRAP_CLAMP_EDGE: [Command set 2.7](#command-set-27)) |
| filters | 3:0 | Minification: 0 NEAREST, 1 LINEAR, 2 NEAREST_MIPMAP_NEAREST, 3 LINEAR_MIPMAP_NEAREST, 4 NEAREST_MIPMAP_LINEAR, 5 LINEAR_MIPMAP_LINEAR |
| filters | 7:4 | Magnification: 0 NEAREST, 1 LINEAR |

Dropped: under 8 dwords; a group the device does not have; texture 0; the device's 16384 textures in use; a value outside these (a wrap of 2 on a 2.6 device). The texture keeps the wraps as given; how CLAMP is sampled is the device's, by a setting ([Command set 2.7](#command-set-27)).

**5Ah TEXDELETE** `group`, `texture`. The texture goes, its levels and its arena copy with it. A texture the device does not have, or a packet under 2 dwords, does nothing.

### Pixel rectangles

**5Bh PIXELS** `context`, `kind`, `width | height << 16`, then floats: `x`, `y`, `z`, `zoom x`, `zoom y`, the colour (4), the texture coordinates (4), the fog distance; (2.6, with PIXELS_TEX1 in `kind`) texture unit 1's coordinates (4); then the data, a row after another from the bottom: width × height dwords for kinds 0 and 1, (width + 3) / 4 dwords a row for kinds 2 and 3. 17 (VGPU98_OGL_LEN_PIXELS; 21 with PIXELS_TEX1) + the data's dwords exactly. `kind`'s bits 7:0 are the kind (VGPU98_OGL_PIXELS_KIND_MASK); bit 8 is PIXELS_TEX1 (100h, 2.6).

| Kind | Name | Data |
|---|---|---|
| 0 | COLOR | RGBA8, red in the low byte: the image's colours |
| 1 | DEPTH | Floats, 0-1: the image's depths |
| 2 | STENCIL | Bytes, four a dword: stencil indices, written through the stencil write mask, with no colour and no tests |
| 3 | BITMAP | Bytes, four a dword: not 0, a fragment |

- Image pixel (i, j) covers the pixel centres from `(x + zoom x × i, y + zoom y × j)` to `(x + zoom x × (i + 1), y + zoom y × (j + 1))` in window coordinates (GL 1.1, section 3.6.4); a bitmap's pixel (i, j) is at `(floor(x) + i, floor(y) + j)`, unzoomed (section 3.7). z (clamped to 0-1), the colour (clamped to 0-1), the texture coordinates (2.6: each unit's, unit 1's (0, 0, 0, 1) without PIXELS_TEX1) and the fog distance are the raster position's: a colour image's own colours stand in place of the colour, a depth image's own depths in place of z.
- COLOR, DEPTH and BITMAP fragments go through the per-fragment operations as a point's do (texturing, fog, the alpha test, scissor, stencil, depth, blending, the logic op, the masks), into the draw buffers; a depth image's depths are put on the 24-bit grid. STENCIL images write their indices one bit at a time (GL 1.1, section 4.3.1): for each bit the stencil write mask lets through, the fragments whose index has it set write 1 there, then those with it clear write 0. Without a D24S8 drawable, or with a stencil write mask of 0, a STENCIL image draws nothing.
- Dropped: under 17 dwords; a kind past 3, or another bit set in `kind` (but PIXELS_TEX1, 2.6); a width or height of 0; a length other than 17 (21 with PIXELS_TEX1) + its data exactly; data past F000h bytes; a context the device does not have.

### Read-backs

**5Ch READ** `context`, `what`, `x | y << 16` (each a signed 16-bit value), `width | height << 16`, `offset`, `stride`. Writes a rectangle of the bound drawable into video memory, its rows from the bottom (GL's order), row r at `offset + r × stride`:

| What | Name | A pixel |
|---|---|---|
| 0 | FRONT | Colour as the drawable keeps it: 565 in 2 bytes, or BGRA 8888 in 4 |
| 1 | BACK | Likewise, from the back image (the front image without one) |
| 2 | DEPTH | A 32-bit integer: a D16 drawable's 16-bit value, a D24S8 drawable's 24-bit value |
| 3 | STENCIL | A byte |

- The part of the rectangle inside the drawable is written, after every packet before it (the host GPU waited for); the rest of the rows is left as video memory has it (GL leaves those pixels undefined). Video memory stays coherent: newer DirectX pixels in each row's range are copied back first, and the pages written are marked after. A RESET since the packet was taken (a mode change) writes nothing.
- Dropped: under 6 dwords; `what` past 3; a width or height of 0; no drawable bound, or a context the device does not have; DEPTH without a depth buffer, or STENCIL without D24S8; rows outside video memory, or a stride below a row's bytes; rows that would land on the frame shown (a staging surface found before a mode change may lie inside the new desktop). A drawable without images writes nothing.

**5Dh TEXREAD** `group`, `texture`, `level`, `offset`, `stride`, `first | rows << 16`. Writes rows `first` to `first + rows − 1` of a level's texels, from the host's copy, RGBA8 with the level's border, the level's width × 4 bytes each (2.6: an index level's indices, a byte each, the level's width in bytes), row r at `offset + r × stride`, under the same coherency rule. Dropped: under 6 dwords; a texture or level the device does not have; no rows, or rows past the level; rows outside video memory, a stride below a row's bytes, or rows on the frame shown. A RESET since the packet was taken writes nothing.

### Lifetimes and resets

The device's objects outlast every RESET but a new machine's (GL 1.1 has no lost context): a RESET holding the GPU for a mode change (HOLD_DRIVER) or a VGA mode (HOLD_VDD: a full-screen DOS session), and the drivers' starts and recoveries, leave every group, context, drawable and texture as it was, registers, images and texels included; only what the device made from them for drawing (each context's uniform block, each texture's copy in the arena) is made again when next needed. A machine reset and GPU_RESET at POST ([gpu-registers.md](gpu-registers.md)) end them all: the command processor tells the device a new machine from a mode change by a reset kind of OpenGL's own, Glide's kinds as they were. What the ring lost at a restart, the ICD sends again ([opengl.md](opengl.md), Batches and the ring).

## Command set 2.6

Command set 2.6 is the OpenGL device's additions for the ICD's extensions (whose decisions this section cites; [opengl.md](opengl.md), The extensions). It is additive: every 2.5 packet means what it did, and 2.6 gives a meaning to values a 2.5 device drops. It needs PROFILE.OGL, as 2.5 does, and no bit of its own; COMMAND_SET reads 00020006h on a 2.6 device (00020007h on a 2.7 one: [Command set 2.7](#command-set-27); [gpu-registers.md](gpu-registers.md)). The ICD sends a 2.6 value only to a device whose COMMAND_SET reads 2.6 or later, so an older 86Box, which would drop them, gets command set 2.5 alone. The interface stays 3.3.

The extensions' passes added to it one at a time: SWAP's swap interval, texture unit 1 and partial texture updates, the point parameters, index textures and their palettes.

### SWAP's swap interval

For WGL_EXT_swap_control, **57h SWAP**'s flags bits 6:4 are **INTERVAL** (VGPU98_OGL_SWAP_INTERVAL_SHIFT 4, VGPU98_OGL_SWAP_INTERVAL_MASK 70h): with SHOW, the vertical blanks to wait for before the front image is written, 0 to 4 (VGPU98_OGL_SWAP_INTERVAL_MAX). A SWAP with an INTERVAL above 4 is dropped. Without SHOW the field is not read.

- **The order.** SHOW reads the front image's pixels under the rectangles back, waits for the host GPU and copies them out to memory of its own, as every SHOW does (the staging buffer they came through may be reused by other command buffer slots during the wait and the coherency rule's copies back); then it waits for the vertical blanks; then it writes the rectangles into video memory (under the display's layer lock when there is a display), so the writes land at the start of the blank.
- **The count.** The device keeps, for each drawable, the vertical blank count at its last SHOW: every SHOW sets it, with an INTERVAL or without; a new drawable's is 0, and RESIZE keeps it. With INTERVAL n the GPU waits until the card's vertical blank count is n or more past that mark and has moved at least once since the wait began, as FLIP and the Glide device's SWAP wait (Glide's SWAP counts from its own last swap, as before). INTERVAL 0 waits for none. Later packets wait behind it.
- **A RESET ends the wait**: a mode change, a DOS session, a driver starting or restarting the GPU, a machine reset, GPU_RESET at POST. The SWAP then writes nothing, as for any RESET since the packet was taken.
- **Nothing visible still waits.** A SHOW with no rectangle to write (none given, or the checks of 2.5's SWAP leave none: another screen shown, another depth) still waits for its vertical blanks, so the swap keeps its pace. A drawable without images does nothing, and does not wait.
- A FENCE after the SWAP completes once SHOW has written, after its wait.
- The ICD sends INTERVAL only in a double-buffered window's SwapBuffers (COPY and SHOW), the window's interval kept to 4, and only for a window whose client area is the whole screen and wholly visible (its corner at 0, 0, its size the mode's, its one visible rectangle the screen) when the swap does not keep the cursor out of the window: the wait holds the command processor, every later packet waits behind it, and GDI waits for the last fence issued before it draws, so a windowed program's waits would hold the desktop's drawing to one a frame. Its LOADs and its front-buffer SHOWs never carry it ([opengl.md](opengl.md), WGL_EXT_swap_control).
- **OVERRIDE** (8, interface 3.6). On a card with PROFILE.VSYNC the ICD sets it on exactly the SwapBuffers that can carry an INTERVAL (those above), whatever interval they carry, and on no other SWAP. The device puts the interval of a SHOW with OVERRIDE alone through VSYNC_CONTROL's OpenGL field, so the user's Always on never makes a windowed, covered or front-buffer SHOW wait, nor one the cursor is kept out of, which a wait would hide for a refresh. A 2.6 device without interface 3.6 drops a SWAP with flag 8, so the ICD sends it only to a card with PROFILE.VSYNC.

### Texture unit 1

For GL_ARB_multitexture and GL_SGIS_multitexture, a context gains a second texture unit; unit 0 is 2.5's, its registers where they were.

**Registers.** STATE addresses 2.6's 495 registers (VGPU98_OGL_R_COUNT; 2.5's 433 are VGPU98_OGL_R_COUNT_25): unit 1's 55 (VGPU98_OGL_UNIT_REGS) from VGPU98_OGL_R_UNIT1 (433), at the offsets VGPU98_OGL_U_*, each as unit 0's register of the same name; then the point parameters' 7 ([Point parameters](#point-parameters)).

| Register | Offset | Name | Contents | At OPEN |
|---|---|---|---|---|
| 433-448 | 0 | U_TEXTURE | Unit 1's texture matrix, 16 floats | Identity |
| 449 | 16 | U_TEXGEN_MODE | As TEXGEN_MODE | 0 |
| 450-465 | 17 | U_TEXGEN_OBJECT | The object planes of s, t, r and q, 4 floats each | 0 |
| 466-481 | 33 | U_TEXGEN_EYE | The eye planes, in eye coordinates | 0 |
| 482 | 49 | U_TEXTURE_ID | The texture unit 1 draws with: its ID in the context's group; 0 for none | 0 |
| 483 | 50 | U_TEX_ENV_MODE | VGPU98_OGL_ENV_* | 0 (MODULATE) |
| 484-487 | 51 | U_TEX_ENV_COLOR | 4 floats | 0 |

- **ENABLES** bit 21, TEXTURE1_1D (VGPU98_OGL_EN_TEXTURE1_1D, 200000h): unit 1's texture drawn with is one-dimensional; kept, not read, as TEXTURE_1D.
- **A 2.5 device** drops a STATE that reaches past its 433 registers: the ICD changes and sends registers past them only on a device whose COMMAND_SET reads 2.6 or later. It sends changed registers as STATE runs, a changed register 16 or more past the last starting a STATE of its own, so unit 1's block does not drag the registers between in ([opengl.md](opengl.md), Batches and the ring).
- **DRAW_TEX1** (800h, DRAW's control bit 11): each vertex is 20 floats (VGPU98_OGL_VERTEX1_DWORDS), unit 1's S1, T1, R1 and Q1 (VGPU98_OGLV_S1 to VGPU98_OGLV_Q1, floats 16-19) after 2.5's 16: its current coordinates, or with DRAW_CLIP its final ones. The DRAW's length is then 4 + 20 × nv + (ni + 1) / 2 dwords, its vertices at most F000h bytes. Without DRAW_TEX1 unit 1's coordinates are (0, 0, 0, 1), neither generated nor transformed. The ICD sets DRAW_TEX1 while unit 1 draws with a texture (its U_TEXTURE_ID not 0), and its DRAWs then hold 640 vertices at most.
- **PIXELS_TEX1** (100h in PIXELS' `kind`): unit 1's raster texture coordinates, 4 floats, follow the fog distance, before the data; the length is 21 + the data's dwords. Without it unit 1's coordinates are (0, 0, 0, 1). The ICD sets it while unit 1 draws with a texture.
- **Drawing.** Without DRAW_CLIP each unit's coordinates are generated by its own TEXGEN_MODE and planes and transformed by its own TEXTURE matrix (unit 1's with DRAW_TEX1). A fragment, a PIXELS fragment included, samples unit 0's texture and applies unit 0's environment to its colour, then samples unit 1's texture at unit 1's coordinates and applies unit 1's environment (U_TEX_ENV_MODE, U_TEX_ENV_COLOR) to that result: ARB_multitexture's cascade. A unit whose TEXTURE_ID is 0, or whose texture has no copy in the texel arena, is skipped. How sampling and the environments are done: [opengl.md](opengl.md), Texturing and the texel arena.
- **The uniform buffer** holds, after the 495 registers (124 vectors), a texture block of 16 vectors for each unit, unit 0's first: 2496 bytes (an index texture's block: [Index textures and palettes](#index-textures-and-palettes)) ([opengl.md](opengl.md), Pipelines, the uniform block and the data chunks).
- **Residency.** A draw's two textures stay in the texel arena together: making room for one never drops the other texture of the same draw. A texture that finds no room beside the draw's other one is drawn without (its unit skipped).

### Partial texture updates

A TEXIMAGE without DEFINE (texels replacing a rectangle of a level defined before) into a texture whose copy in the texel arena is up to date no longer makes that copy again whole: the device widens the level's changed rectangle (the bounds of every such rectangle since the copy), and at the texture's next use copies each changed row's span of it into the arena, one copy region a row, through the command buffer slot's staging buffer. A TEXIMAGE with DEFINE, any change to a texture without an up-to-date copy, and a RESET (an upload may have been dropped with the recording) still make the copy again whole at the next use. When the staging buffer cannot be had (3D stopped), the copy is dropped.

### Point parameters

For GL_EXT_point_parameters, registers 488-494, after unit 1's block:

| Register | Name | Contents | At OPEN |
|---|---|---|---|
| 488 | POINT_PARAMS | Bit 0: points sized by their distance (below); 0, POINT_SIZE as in 2.5 | 0 |
| 489 | POINT_MIN | A float: the smallest size | 0.0 |
| 490 | POINT_MAX | A float: the largest size | 64.0 |
| 491 | POINT_FADE | A float: the fade threshold | 1.0 |
| 492-494 | POINT_ATTEN | 3 floats: the attenuation's a, b and c | 1.0, 0.0, 0.0 |

- **With POINT_PARAMS**, a point of the device's path (without DRAW_CLIP) has the size POINT_SIZE × √(1 / (a + b·d + c·d²)), d its distance from the eye (the length of its eye coordinates over their w), where that sum is above 0, else POINT_SIZE; raised to POINT_MIN, then lowered to POINT_MAX; below POINT_FADE, POINT_FADE's size, both faces' alpha multiplied by (size / POINT_FADE)²; then kept to 1-64, as every point.
- **A DRAW_CLIP point** (the ICD's own) takes its vertex's STIPPLE float as its size when that is above 0, else POINT_SIZE, then 1-64; the ICD has computed its size and put its fade in its alpha.
- The ICD sets POINT_PARAMS only while one of the four values is not its default, so GL 1.1's points are drawn as 2.5 draws them ([opengl.md](opengl.md), GL_EXT_point_parameters).

### Index textures and palettes

For GL_EXT_paletted_texture and GL_EXT_shared_texture_palette:

- **TEXIMAGE's base format INDEX** (7, VGPU98_OGL_BASE_INDEX): a level of 8-bit indices, a byte a texel. In the packet they go four a dword, the first in the low byte, each row padded to a whole dword: a DEFINE with TEXELS is 6 + ((width + 3) / 4) × height dwords, a rectangle without DEFINE likewise for its own width. A rectangle goes only into a level of its own kind (indices into an index level, colours into a colour one), else it is dropped. The host keeps an index level's bytes, rows not padded, within the same 512 MB as other texels. Level 0's base format makes the texture an index texture, and a draw samples only the levels from 0 on of level 0's kind.
- **TEXREAD** of an index level writes its indices, a byte each, the level's width in bytes a row.
- **5Eh PALETTE** `context`, `texture`, `base format`, `width`, then `width` entries. Sets the palette of the context's group's texture with that ID (made if the group does not have it), or, for texture 0, the context's shared palette. The base format is 1-6 (ALPHA to RGBA, as TEXIMAGE's), the entries texels of it as TEXIMAGE's colours are (RGBA8, filled in as the table above), the width a power of two from 1 to 256 (VGPU98_OGL_PALETTE_MAX). A texture and a context each start with a palette of one white RGBA entry (FFFFFFFFh). Dropped: under 4 dwords (VGPU98_OGL_LEN_PALETTE); a context the device does not have; a base format other than 1-6; a width of 0, over 256 or not a power of two; a length other than 4 + the width; a texture the device cannot make (its 16384 in use).
- **ENABLES** bit 22, SHARED_PALETTE (VGPU98_OGL_EN_SHARED_PALETTE, 400000h): index textures, on either unit, look up the context's shared palette instead of their own.
- **Sampling.** Each tap of an index texture reads its index, a byte, takes it within the palette's width (masked by the width less one) and reads that palette entry, RGBA8, as the texel, before filtering (LINEAR's four taps each looked up, then blended). The texture's base format, for the texture environment, is the palette's; the border colour stays the border colour.
- **The texel arena.** An index texture's copy holds its levels' bytes, each level from a whole dword, then its palette, with room for 256 entries. A PALETTE for a texture whose copy is up to date has the palette copied in alone at the texture's next use; a palette change never sends texels. Each context's shared palette has a range of its own, 1 KB, made when an index texture first draws with it (other textures' copies going for room, never the draw's), kept until the context ends, and brought up to date at the next such draw after a PALETTE or a RESET. Partial updates of an index level patch its bytes.
- **The uniform block's texture block** for an index texture: dword 0 has TEX_INDEXED (bit 24) set and the palette's base format in place of the texture's; dword 2 is the border | (the palette's width − 1) << 8; dword 3 is the palette's first entry in the arena, as a dword index (the texture's own palette after its levels, or the context's shared one). A colour texture's block is as before: bit 24 clear, dword 2 the border, dword 3 0.

## Command set 2.7

Command set 2.7 is clamp to edge for the OpenGL device's textures (whose decisions this section cites; [opengl.md](opengl.md), Clamp-to-edge). It is additive, as 2.6 is: every 2.6 packet means what it did, and 2.7 gives a meaning to one value a 2.6 device drops. It needs PROFILE.OGL and no bit of its own; COMMAND_SET reads 00020007h ([gpu-registers.md](gpu-registers.md)). The ICD sends the value only to a device whose COMMAND_SET reads 2.7 or later, where alone it offers the extensions that ask for it, so an older 86Box, which would drop the TEXPARAM, gets 2.6's alone. Nothing else changes: no opcode, packet length or flag is new, STATE still addresses 495 registers, the uniform block keeps its layout, and the interface stays 3.3.

### TEXPARAM's clamp to edge

**59h TEXPARAM**'s wraps take **2, clamp to edge** (VGPU98_OGL_WRAP_CLAMP_EDGE), for s (bits 3:0) and t (bits 7:4) each; a wrap above 2 is dropped, as a wrap of 2 is by a 2.6 device. The ICD sends it for GL_CLAMP_TO_EDGE; GL_CLAMP still goes as 1 and GL_REPEAT as 0 ([opengl.md](opengl.md), Clamp-to-edge).

- **Sampling** (`ogl.frag`). Clamp to edge keeps every tap inside the level, its border left out, so neither the border texels nor the border colour is ever read. s (or t) is clamped to 0-1, as CLAMP's is. NEAREST takes texel `floor(u)` lowered to the level's width − 1, as CLAMP's NEAREST does, which is already inside the level. LINEAR's two taps in that direction, `floor(u − 1/2)` and the next, are each clamped to 0 through the width − 1 (t's to the height − 1), so at the edge both read the edge texel. Each mipmap level is clamped by its own size. A 1D texture's t is not read.
- **REPEAT and CLAMP** are sampled as before: REPEAT wraps the indices; CLAMP by OpenGL 1.1's rule, its LINEAR taps reaching the border (the border texels, or past them the border colour).
- **CLAMP as period drivers sampled it**. With the 86Box setting "OpenGL GL_CLAMP" at **As period drivers (clamp to edge)**, the default, the device samples a texture's CLAMP as clamp to edge; at **Exact (OpenGL 1.1)** (the host option VGPU98_OPT_OGL_CLAMP_EXACT, 40h), by the rule ([registers.md](registers.md), 86Box settings). The texture keeps the wraps TEXPARAM gave: the setting changes only the texture block the device writes into the uniform block for a draw, whose dword 0 holds the wraps as sampled, s in bits 11:8 and t in bits 15:12, 0, 1 or 2, a CLAMP written as 2 unless the setting is Exact. A wrap of 2 is sampled so whatever the setting.
