# GPU surface formats

**Versions:** command set 2.0 has cube maps, textures of any size and the vertex element types; command set 2.1 adds the formats and element types marked 2.1, volume textures and the A1R5G5B5 render target, with PROFILE.FIXED8; command set 2.2 adds the three marked 2.2, with PROFILE.PROGRAMS.

Every surface the GPU reads or writes has one of these formats. A format fixes the layout of the surface's bytes in video memory: that layout is what the guest sees and what DirectDraw describes with a DDPIXELFORMAT. How the device holds a surface on the host is its own business, with two rules: **the host representation round-trips** (converting a texel to the host format and back gives the same stored bits) and **it supports what the surface is used for** (filtering for textures, blending for render targets). The values the host computes with then differ from the stored ones by less than the host's floating-point precision, far below one step of the stored format.

All multi-byte values are little-endian. An n-bit unsigned normalized (UNORM) field with value v means v / (2^n − 1).

## Format codes

| Code | Name | Bits per pixel | Layout, high bit to low | DDPIXELFORMAT (flags; masks) |
|---|---|---|---|---|
| 01h | R5G6B5 | 16 | R 15:11, G 10:5, B 4:0 | RGB; F800h, 07E0h, 001Fh |
| 02h | X1R5G5B5 | 16 | unused 15, R 14:10, G 9:5, B 4:0 | RGB; 7C00h, 03E0h, 001Fh |
| 03h | A1R5G5B5 | 16 | A 15, R 14:10, G 9:5, B 4:0 | RGB, ALPHAPIXELS; as 02h, alpha 8000h |
| 04h | A4R4G4B4 | 16 | A 15:12, R 11:8, G 7:4, B 3:0 | RGB, ALPHAPIXELS; 0F00h, 00F0h, 000Fh, alpha F000h |
| 05h | X8R8G8B8 | 32 | unused 31:24, R 23:16, G 15:8, B 7:0 | RGB; 00FF0000h, 0000FF00h, 000000FFh |
| 06h | A8R8G8B8 | 32 | A 31:24, R 23:16, G 15:8, B 7:0 | RGB, ALPHAPIXELS; as 05h, alpha FF000000h |
| 07h | P8 | 8 | palette index | RGB, PALETTEINDEXED8 |
| 08h | L8 | 8 | luminance | LUMINANCE; FFh |
| 09h | A8L8 | 16 | A 15:8, L 7:0 | LUMINANCE, ALPHAPIXELS; 00FFh, alpha FF00h |
| 0Ah | V8U8 | 16 | V 15:8, U 7:0, both signed | BUMPDUDV; 00FFh, FF00h |
| 0Bh | L6V5U5 | 16 | L 15:10 (unsigned), V 9:5, U 4:0 (signed) | BUMPDUDV, BUMPLUMINANCE; 001Fh, 03E0h, FC00h |
| 0Ch | X8L8V8U8 | 32 | unused 31:24, L 23:16 (unsigned), V 15:8, U 7:0 (signed) | BUMPDUDV, BUMPLUMINANCE; 000000FFh, 0000FF00h, 00FF0000h |
| 0Dh | A8 (2.1) | 8 | A 7:0 | DirectX 8's D3DFMT_A8 (28) |
| 0Eh | X4R4G4B4 (2.1) | 16 | unused 15:12, R 11:8, G 7:4, B 3:0 | D3DFMT_X4R4G4B4 (30) |
| 0Fh | A4L4 (2.1) | 8 | A 7:4, L 3:0 | D3DFMT_A4L4 (52) |
| 10h-14h | DXT1-DXT5 | 4 (DXT1), 8 | 4x4 blocks: 8 bytes (DXT1) or 16 bytes per block, in rows of blocks | FOURCC `DXT1`...`DXT5` |
| 18h | Q8W8V8U8 (2.1) | 32 | Q 31:24, W 23:16, V 15:8, U 7:0, all signed | D3DFMT_Q8W8V8U8 (63) |
| 19h | V16U16 (2.2) | 32 | V 31:16, U 15:0, both signed | D3DFMT_V16U16 (64), or BUMPDUDV; 0000FFFFh, FFFF0000h |
| 1Ah | G16R16 (2.2) | 32 | G 31:16, R 15:0 | RGB; 0000FFFFh, FFFF0000h, 0 (or D3DFMT_G16R16, 34) |
| 1Bh | A2W10V10U10 (2.2) | 32 | A 31:30 (unsigned), W 29:20, V 19:10, U 9:0 (signed) | BUMPDUDV; 000003FFh, 000FFC00h (or D3DFMT_A2W10V10U10, 67) |
| 20h | D16 | 16 | Z 15:0 | ZBUFFER; Z FFFFh |
| 21h | D24X8 | 32 | Z 31:8, unused 7:0 | ZBUFFER; Z FFFFFF00h |
| 22h | D24S8 | 32 | Z 31:8, stencil 7:0 | ZBUFFER, STENCILBUFFER; Z FFFFFF00h, stencil 000000FFh |

The DDPIXELFORMAT flags and masks are what the driver reports for the format ([directx-hal.md](directx-hal.md)); command set 2.1's and 2.2's formats are reported only through DirectX 8's format list, by their D3DFMT codes (a 2.2 format reaches the driver as the DDK says: V16U16 as a FourCC, the others by their masks; the DLL takes either) ([d3d-caps.md](d3d-caps.md)), so DirectX 6 and 7 programs never see them. For the Z formats, `dwZBufferBitDepth` is the pixel's total size including stencil: 16 for D16, 32 for D24X8 and D24S8 (with `dwStencilBitDepth` 8 for D24S8). A DirectX 5 program that asks for a 32-bit Z buffer without a pixel format gets D24X8. Signed bump fields are two's complement; a signed n-bit field with value s means s / 2^(n−1), so the most negative value maps to −1 exactly.

For a DXT surface the pitch is the distance in bytes between rows of blocks, and the width and height are in pixels (rounded up to whole blocks for the size in memory).

## Where each format can be used

| Use | Formats |
|---|---|
| Display (scan-out) | P8, R5G6B5, X8R8G8B8 (the display engine's 8, 16 and 32 bpp; X1R5G5B5 is its 15 bpp VBE format) |
| 2D packets (FILL, BLT) | Any format. FILL writes a raw value. BLT copies between surfaces of the same format |
| Render targets | R5G6B5, X1R5G5B5, X8R8G8B8, A8R8G8B8; 2.1: A1R5G5B5 |
| Depth/stencil buffers | D16, D24X8, D24S8 |
| Textures | Every colour, luminance, bump and DXT format above: 2D textures of any size from 1 to 2048 in each dimension (powers of two or not), and cube maps (six square faces of the same size and levels); 2.1: volume textures in every format but P8 and DXT1-DXT5, 1 to 256 in each dimension |

## Host representation (86Box)

The device picks, at start-up, the Vulkan format for each code by asking the host's Vulkan driver what it supports. In order of preference:

1. **A native format with the same bits** that supports the use, read through a component swizzle where the channel order differs: R5G6B5 is `R5G6B5_UNORM_PACK16`, A1R5G5B5 and X1R5G5B5 are `A1R5G5B5_UNORM_PACK16`, A4R4G4B4 is `B4G4R4A4_UNORM_PACK16` swizzled (the four-bit format Vulkan requires every implementation to sample), A8R8G8B8 and X8R8G8B8 are `B8G8R8A8_UNORM`, L8 is `R8_UNORM`, A8L8 is `R8G8_UNORM`. Uploading and copying back are then plain copies.
2. **`R16G16B16A16_SFLOAT`**, converting each texel on upload and back. Vulkan requires every implementation to filter and blend it, and it round-trips every format above: an n-bit UNORM value v/(2^n − 1) and a signed value s/2^(n−1) for n ≤ 8 come back to the same bits. Expanding a 5-bit field to 8 bits is **not** a round trip that keeps the value (v/31 is not a multiple of 1/255) and is never used.
3. **Formats the device always converts on upload**, because their values come from elsewhere:
   - P8 textures: each index is looked up in the stage's palette ([gpu-commands.md](gpu-commands.md), PALETTE) and stored as A8R8G8B8. A palette change re-uploads the texture. The lookup happens before filtering, as in Direct3D's reference rasterizer.
   - DXT1-DXT5: decoded on the host CPU to A8R8G8B8. The DXTn document gives the interpolation formulas (thirds, halves, sevenths and fifths) but not their rounding or how endpoints expand, so those follow the reference rasterizer: endpoints expanded from 5:6:5 by replicating their top bits, the four-colour steps (2a + b + 1) / 3, the three-colour midpoint and the DXT4/5 alpha steps truncated. One rule comes from the document and overrides the reference rasterizer: three-colour mode (colour 0 ≤ colour 1, index 3 transparent black) applies to DXT1 only, and DXT2-DXT5 colour blocks are always four-colour. Decoding on the CPU gives the same texels on every host GPU, which hardware BC decoding does not guarantee.
   - The bump formats: converted to `R16G16B16A16_SFLOAT` holding the signed values defined above (Q8W8V8U8's four: (U, V, W, Q)).
   - Command set 2.2's formats: converted to `R32G32B32A32_SFLOAT`, which holds their 16- and 10-bit values exactly, or to `R16G16B16A16_SFLOAT` (within 1/2048) on a host that cannot filter 32-bit floats.

**What a texel reads as.** A format without some of the four channels reads them as Direct3D defines (the DirectX 9 documentation's table; DirectX 8.1's documentation is silent): alpha 1 where the format has none; L8 and A8L8 as (L, L, L, A); A4L4 the same; A8 as (0, 0, 0, A); V8U8 as (U, V, 1, 1); L6V5U5 and X8L8V8U8 as (U, V, L, 1); 2.2's V16U16 as (U, V, 1, 1) and G16R16 as (R, G, 1, 1) (the reference rasterizer's blue and alpha of 1, the Server 2003 DDK's "Recommended DirectX 9.0 Driver Support"), A2W10V10U10 as (U, V, W, A). The 2.1 formats' host images (rule 1 where the host has the bits, else rule 2): A8 is `R8_UNORM` read with the swizzle (0, 0, 0, R); X4R4G4B4 is A4R4G4B4's image with alpha read as 1; A4L4 converts (rule 2); Q8W8V8U8 converts as the bump formats do.

Render targets use rule 1 where the host supports the format as a colour attachment with blending, and rule 2 otherwise. With rule 2 the target keeps more precision between draws than the card would (each blend reads an unrounded value) and is rounded to its format when copied back; this differs from a real 16-bit target only in the low bit of blended pixels, and only on hosts without the native format. An A1R5G5B5 target (2.1) under rule 2 also keeps fractional alpha between draws (destination alpha factors read it), and its blends are not clamped to 0-1 until it is copied back (a SUBTRACT can leave a value below 0 that a later blend reads): a deviation on hosts without an A1R5G5B5 attachment format, which desktop hosts have.

Depth formats:

| Code | Vulkan format | Conversion |
|---|---|---|
| D16 | `D16_UNORM`; `D32_SFLOAT` once W-buffered (below), and always with the setting "16-bit depth precision" at Full ([registers.md](registers.md)) | none; on `D32_SFLOAT` as below |
| D24X8, D24S8 | `D24_UNORM_S8_UINT` where supported, else `D32_SFLOAT_S8_UINT` | Z moves between bits 31:8 and the depth aspect; stencil between bits 7:0 and the stencil aspect. On `D32_SFLOAT_S8_UINT`, as below |

A depth image in a 32-bit float format holds one of two things, and notes which: Z, or W (W-buffering, [gpu-commands.md](gpu-commands.md)). What it holds follows the last draw writing its depth, and a clear made while depth was on (ZENABLE 1 or 2); a clear with ZENABLE 0 changes nothing.

- **Z** is held on the guest buffer's grid, exactly: step k of a 16- or 24-bit buffer as k / 2^bits. The fragment's depth is rounded to the grid before testing and storing (the product in float, to the nearest step, halves to even; ZBIAS taking whole steps), and a clear's value is rounded the same way, so the tests and stored values are those of the guest's buffer, and a value converts to float and back to the same bits. With "16-bit depth precision" at Full, D16's Z is held as the host computes it, without the grid (ZBIAS still n / 65535 per step): its tests have the host's precision, and a copy back rounds it to 16 bits.
- **W** is held as W × 2⁻⁶⁴ (exact, ordered, below 1 for every W under 2⁶⁴, the far end 1), whatever the W range. Uploads and copy-backs convert between it and the reference's (W − wnear) / (wfar − wnear) on the guest's grid with the W range at that time; the largest value is the far end both ways. Video memory's form holds less than the image: a value converted to W and back is off by up to about wfar / (wfar − wnear) steps of a 24-bit grid, 256 times fewer of a 16-bit one (so none for a 16-bit buffer and one at most for a 24-bit one with a range starting near 0; more for a range far from 0 against its span, and for a negative wnear), and with an infinite range video memory holds only the near and far ends. The image keeps the W it holds, though: it takes again only the pixels the guest writes ([gpu-coherency.md](gpu-coherency.md)).

**Surfaces W-buffered into.** The first packet that W-buffers into a depth surface records it (eight at most): from then on its images are made in the float format of its code (`D32_SFLOAT` for D16, `D32_SFLOAT_S8_UINT`, stencil kept, for D24X8 and D24S8), holding what its last image held. An image already made in a fixed-point format (`D16_UNORM`, `D24_UNORM_S8_UINT`) moves: its GPU-newer pixels go back to video memory (unless the packet overwrites the whole surface), and the float image takes its place from there, taking video memory's values as W. A surface leaves the records when a DISCARD overlaps it (the program freed it), when its image, holding nothing GPU-newer, goes unused for 10 seconds (a program may end without freeing it), when it has no image (evicted for room) and nothing W-buffered into it for 10 seconds, and at RESET; with all eight in use, the least recently W-buffered into goes. A depth buffer that is only ever Z-buffered keeps the fixed-point format, as before. A host without the float format keeps the fixed-point image and stores the reference's form of W on its grid.

## Surfaces in the command stream

Packets describe surfaces by where they are, not by handles ([gpu-commands.md](gpu-commands.md)): a video memory offset, width, height, pitch and format. Two descriptions of the same bytes are the same memory: the coherency rules keep every copy of it in step ([gpu-coherency.md](gpu-coherency.md)).

## Cube maps and textures of any size (command set 2.0)

- **A cube map** is six faces, each a square surface with the same format, size and number of levels, anywhere in video memory: TEXTURE names every level of every face by offset and pitch, faces in Direct3D's order +X, −X, +Y, −Y, +Z, −Z ([gpu-commands.md](gpu-commands.md)). Its host image is one Vulkan image of six layers, cube-compatible, in the format a 2D texture of the same code would have; every conversion above applies face by face. Sampling takes a direction (the first three texture coordinates) and picks the face and the texel as Vulkan and Direct3D both define it.
- **A texture's size** may be anything from 1 to 2048 in each dimension. Each level is half the one before, rounded down, at least 1. Whether a program may use sizes that are not powers of two, and with which addressing and filtering, is the capability profile's to say ([d3d-caps.md](d3d-caps.md)); the GPU samples any size correctly in every mode.
- **A level or face as a render target** is the same bytes as the texture's: the host draws into the texture's image ([gpu-coherency.md](gpu-coherency.md)).

## Vertex elements

The vertex declaration's element types ([gpu-commands.md](gpu-commands.md), DECLARE), as the GPU reads them from a stream's bytes (little-endian):

| Type | Name | Bytes | Value |
|---|---|---|---|
| 1-4 | FLOAT1-FLOAT4 | 4 per component | IEEE single-precision floats |
| 5 | D3DCOLOR | 4 | An A8R8G8B8 dword: bits 23:16 red, 15:8 green, 7:0 blue, 31:24 alpha, each UNORM (byte / 255) |
| 6 | UBYTE4 (2.1) | 4 | Four unsigned bytes, the lowest-addressed first: (b0, b1, b2, b3), each 0-255 as a float |
| 7 | SHORT2 (2.1) | 4 | Two signed 16-bit integers (two's complement), the low word first: (x, y) as floats |
| 8 | SHORT4 (2.1) | 8 | Four, likewise |

Types 9 and up are reserved for DirectX 9's (the packed and half-float types); the GPU rejects them, and command set 2.0 rejects 6-8 too.

## Volume textures (command set 2.1)

- **A volume** is width × height × depth texels (1-256 each) with 1-9 levels, each half the one before in every dimension (at least 1). Each level is `depth` slices of `height` rows: row r of slice z at `offset + z × slice pitch + r × pitch`. TEXTURE names every level's offset, pitch and slice pitch ([gpu-commands.md](gpu-commands.md)).
- **Its host image** is one Vulkan 3D image in the format a 2D texture of the same code would have (rule 1 or 2; P8 and DXT have no volumes), uploaded slice by slice with the same conversions. A volume is never drawn into, so its image only ever takes video memory's bytes.
