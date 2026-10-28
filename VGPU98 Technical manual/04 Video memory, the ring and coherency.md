# 04 Video memory, the ring and coherency

How the guest and the GPU share memory and work: what lives where in video memory and guest RAM, how drivers hand the GPU packets through the ring, how they know work is done, how the GPU is stopped safely, and how pixels the host GPU drew stay consistent with the video memory the guest can read. The reference is [`spec/gpu-registers.md`](../spec/gpu-registers.md) (the ring, CONTROL, the reserve, holds), [`spec/gpu-commands.md`](../spec/gpu-commands.md) (packets) and [`spec/gpu-coherency.md`](../spec/gpu-coherency.md) (coherency).

## The memory map

```
 video memory (8-128 MB, BAR1)        guest RAM                host GPU
 +------------------------------+     +--------------------+   +-----------+
 | 0: the visible desktop frame |     | the ring's buffer  |   | an image  |
 |    (display start)           |     |  (SYSBUF, from the |   | for each  |
 |------------------------------|     |   mini-VDD)        |   | surface   |
 | DirectDraw's heap:           |     |--------------------|   | and       |
 |   back buffers, off-screen   |     | status written     |   | texture   |
 |   surfaces, textures, depth, |     |  back (STATUS_BASE)|   | in video  |
 |   Glide's and OpenGL's       |     |--------------------|   | memory    |
 |   staging surfaces           |     | the pool: chunks   |   |-----------|
 |------------------------------|     |  of locked pages   |   | the mirror|
 | the reserve, top 256 KB:     |     |  in the GPU's page |   | of video  |
 |   +0      ring (128 KB) when |     |  table (GPUVM):    |   | memory    |
 |           not in guest RAM   |     |  vertex and index  |   |-----------|
 |   +128 KB cursor patterns,   |     |  buffers, command  |   | Glide and |
 |           2 x 32 KB          |     |  buffers, Lock     |   | OpenGL    |
 |   +192 KB gamma table slots, |     |  copies            |   | buffers   |
 |           2 x 1 KB           |     +--------------------+   +-----------+
 +------------------------------+
```

- **Video memory** is 86Box's emulated memory; the guest reaches it through BAR1, and the VGA through A0000h. The display driver's modes and DirectDraw's heap end below the reserve. The device does not enforce the reserve; the drivers keep to it.
- **Guest RAM** holds what the GPU reads but the guest writes often: the ring, when the mini-VDD could allocate it (writing it then costs no bus cycles), and the pool, which the GPU reaches through its page table (up to 65536 entries, a quarter of guest RAM at most).
- **The host GPU** holds images for everything the GPU draws with or into, and the display's own mirror of video memory ([05](05%20The%20display%20path.md), [06](06%20The%20host%20renderer%20%28Vulkan%29.md)).

## The command ring

```
                  RING_BASE                               RING_BASE+SIZE
 the ring:        |  done  |  packets waiting  |  free   |
                           ^                   ^
                       RING_HEAD           RING_TAIL
                    (the GPU reads to)   (the guest wrote to)

 one packet:  [ header: opcode bits 7:0, payload dwords bits 23:8 ]
              [ payload ... ]
```

**A batch** is how every driver writes the ring:
1. Take the ring lock in the shared block.
2. Make sure the GPU runs: if RUN is clear (after a reset or a hold), program the ring again.
3. Write the packets.
4. End with a FENCE carrying a new value.
5. Write RING_TAIL.
6. Let go of the lock.

The DirectX DLL in every process, the Glide DLLs, the OpenGL ICD and the display driver all write the ring this way, so their batches never interleave. The Glide DLLs and the ICD build their commands in a batch buffer of their own first and copy it into the ring in one go.

**The ring's place.** At boot the mini-VDD allocates the ring's buffer in guest RAM and names it in SYSBUF_PHYS, SYSBUF_LINEAR and SYSBUF_SIZE. The DirectX DLL uses it, setting bit 0 of RING_BASE; without it, or if the GPU refuses it, the ring is 128 KB at the start of the reserve.

**The command processor** runs on its own host thread. It reads from RING_HEAD to RING_TAIL and runs each packet:
- 2D and coherency packets (FILL, BLT, UPLOAD, DOWNLOAD, SYNC, DISCARD, FLIP) run on the host CPU, directly in video memory;
- 3D packets go to the Direct3D device, the Glide device or the OpenGL device, on the host GPU ([06](06%20The%20host%20renderer%20%28Vulkan%29.md));
- a packet whose PROFILE bit is clear, or that is malformed, stops the GPU: STATUS gets ERROR, ERROR_OFFSET and ERROR_CODE say where and why, and the log names the packet.

The Glide and OpenGL devices are more forgiving with their own packets: a command they cannot take is dropped and counted, and the ring goes on.

## Knowing work is done

| Mechanism | What it gives |
|---|---|
| FENCE | The value of the last FENCE packet reached. Everything before it is complete. |
| STATUS | BUSY, ERROR, RUNNING, FLIP_PENDING, HELD |
| Status written back | The same status in guest RAM at STATUS_BASE, so drivers can poll without touching BAR0 |
| VBLANK_COUNT, SCANLINE | Vertical blanks and the scan line, for flips, waits and timing |

**No wait trusts the GPU blindly.** A driver's wait ends when the fence arrives, or when the GPU is held, stopped or in error, or when neither FENCE nor RING_HEAD has moved for about a second (counted in vertical blanks). The OpenGL ICD allows 5 seconds, as GL programs can submit long batches. A refused packet is named in the log with the context that wrote it.

## Holding the GPU

The GPU's work must never land on a screen or in video memory that belongs to something else. Whoever stops it does three things: waits for it to finish (giving up after a second without progress), writes its hold bit together with RESET, and clears the hold bit once the screen is back. RESET makes the GPU copy back anything it held newer than video memory and forget every host copy.

| When | Who | Hold bit |
|---|---|---|
| A mode change: the display driver's Enable, ReEnable, Disable | `vgpu98.drv`, under the ring lock, from before the change until after it | HOLD_DRIVER |
| A VGA mode takes the screen: a full-screen DOS session, or Windows' blue-screen message mode | `vgpu98.vxd`, until the desktop's state comes back | HOLD_VDD |

While either bit is set:
- DirectDraw calls fail with DDERR_SURFACELOST, as on any card while a DOS session has the screen, and DirectDraw restores its surfaces afterwards;
- the Glide DLLs and the OpenGL ICD keep their batches waiting rather than losing them;
- whoever writes the ring next finds RUN clear and programs the ring again.

## Coherency: where the newest pixels are

The GPU draws on the host GPU, into host images. The guest can read video memory at any moment. Coherency is the rule that keeps the two consistent without copying every frame back.

```
 video memory page (4 KB)            its host images
 ------------------------            ---------------
 the guest writes it        ----->   the page's write counter moves;
                                     the image is uploaded again
                                     before its next use

 the GPU draws into an image ----->  the pages under it are marked
                                     GPU-newer in the page map

 something reads a GPU-newer
 page:                               the newer pixels are copied back
   the guest's CPU (BAR1)    ----->  first, then the read goes on
   a Lock (SYNC packet)
   a 2D packet (FILL, BLT)
   another image

 the display shows a        ----->   no copy back: the host display
 GPU-newer surface                   takes the pixels straight from
                                     the image (the layer, doc 05)
```

**The page map.** For every 4 KB page of video memory, the renderer counts the host images holding pixels newer than video memory there. A guest access to a page with a non-zero count copies those pixels back before it completes.

**The packets that steer it:**
- **SYNC** (offset, length): bring a range of video memory up to date, as a Lock does before the program reads.
- **DISCARD** (offset, length): the program freed the surfaces there; their host copies are forgotten, and nothing is copied back.
- **UPLOAD** and **DOWNLOAD**: copy between the pool in guest RAM and video memory or a host image. DOWNLOAD is how a Lock of a surface the GPU drew is served from a copy in guest RAM instead of through the frame buffer window ([08](08%20DirectDraw%20translation.md)).

**Each device's place in it:**

| Device | Its memory | Its pixels reach video memory |
|---|---|---|
| Direct3D | Host images of surfaces in video memory | Only when something reads them; the display takes them directly |
| Glide | The session's own buffers and TMU memory, apart from video memory | Only when a program locks the frame buffer, through a staging surface |
| OpenGL | The drawables' images and the texel arena | At each swap, into the window's visible rectangles: newer DirectX pixels there are copied back first, and the pages marked after |

## The pool

The pool is guest RAM the GPU reads through its page table: applications' vertex and index buffers, the Direct3D runtime's own vertex and command buffers, and the copies Locks are served from.

- **The mini-VDD** allocates the page table at boot and maps chunks of fixed, locked, zeroed pages into it as the DirectX DLL asks (DeviceIoControl on `\\.\VGPU98`). Chunks belong to the DLL as a whole, never to one process.
- **The DLL** carves chunks into 32-byte-aligned blocks, one per buffer, from shared tables under the pool lock. A DISCARDCONTENTS Lock of a buffer queued packets still read is renamed onto fresh memory, or onto one of the buffer's own spare blocks, so the program does not wait for the GPU and the GPU never reads half-written vertices. A Lock that nothing queued reads keeps the buffer's memory.
- **Memory goes back:** idle chunks are returned, so a program's peak does not keep guest RAM forever.

The pool's rules are in [10](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md) and [`spec/directx-hal.md`](../spec/directx-hal.md).
