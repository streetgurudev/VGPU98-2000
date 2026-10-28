# The pipeline cache

**Covers:** the self-checking file, the setting, the key list and warm-up, several 86Box at once, and the OpenGL device's warm-up ([The OpenGL device's warm-up](#the-opengl-devices-warm-up)).

In 86Box the GPU draws on the host GPU through Vulkan pipelines, one for each combination of the states a pipeline fixes (its key, below). A pipeline made cold takes 0.2 to 2 seconds (the shaders are large); one found in Vulkan's pipeline cache, milliseconds. 86Box keeps that cache on disk between sessions. It is the host's alone: the guest never sees it, and it changes how soon a pipeline is ready, never what is drawn.

## The files

Two files beside 86Box's global configuration file (`86box_global.cfg`: on Windows in `%LOCALAPPDATA%\86Box\`, in 86Box's own folder in portable mode, or beside the file `--global` names), shared by every machine:

| File | Name |
|---|---|
| 0 | `vgpu98-pipelines.bin` |
| 1 | `vgpu98-pipelines.2.bin` |

Saves go to them in turn, so a save cut short leaves the other file whole ([Saving](#saving)). The files are every machine's; the setting is each machine's ([The setting](#the-setting)).

## The format

A file is a header, Vulkan's data, then the key list. Every number is a 32-bit little-endian dword.

| Offset | Size | Field | Contents |
|---|---|---|---|
| 00h | 8 | magic | `VGPU98C4` (no terminating zero) |
| 08h | 4 | header_bytes | 34h: the header's size |
| 0Ch | 4 | number | The save's number: one past the newest file on disk when it was made. It wraps |
| 10h | 4 | session | The save's session: the saving session's number, or the file it merged's when that is later ([Saving](#saving)). The key list's clock |
| 14h | 4 | stamp | The GPU's shaders and pipeline code ([The stamp](#the-stamp)) |
| 18h | 4 | vk_bytes | The size of Vulkan's data; 0, none ([The cap](#the-cap)) |
| 1Ch | 4 | vk_hash | The hash of Vulkan's data |
| 20h | 4 | key_version | The key records' version: 1 |
| 24h | 4 | key_bytes | The size of a key record: 20h |
| 28h | 4 | key_count | How many key records follow |
| 2Ch | 4 | key_hash | The hash of the key list |
| 30h | 4 | header_hash | The hash of the header's 48 bytes before it (00h-2Fh) |
| 34h | vk_bytes | | Vulkan's data, as `vkGetPipelineCacheData` gave it ([Vulkan's header](#vulkans-header)) |
| 34h + vk_bytes | key_bytes × key_count | | The key list ([The key list](#the-key-list)) |

**The hash** is FNV-1a taken a dword at a time. From 811C9DC5h, for each whole dword w of the bytes in order, h = (h XOR w) × 01000193h (modulo 2^32); then for each of the last 0-3 bytes b, h = (h XOR b) × 01000193h. vk_hash covers Vulkan's data, key_hash the key list and header_hash the header's first 48 bytes, each from 811C9DC5h (no data, or no keys: 811C9DC5h). The header's own hash catches a flipped bit in its number or session, which would misorder the files or age every key out.

**A header checks out** when it passes these checks, in this order. Each failure's reason is the one the log gives:

| Check | Reason when it fails |
|---|---|
| The file holds the 52 bytes of a header | `cut short` |
| magic is `VGPU98C4` | `another 86Box's format` (`VGPU98C` and another last character, as an older 86Box's `VGPU98C3`), else `not a pipeline cache` |
| header_bytes is 34h | `a header of another size` |
| header_hash is the hash of the 48 bytes before it | `damaged (its header's hash does not match)` |
| vk_bytes is at most 64 MB, key_count at most 4,096 and key_bytes at most 256 | `its sizes past their limits` |
| The file is exactly 34h + vk_bytes + key_bytes × key_count bytes long (a save cut short leaves a whole header and less; two writing it at once, any length) | `not the length its header says` |

**A file read whole** checks out when its header passes the checks above again (all but the length), 86Box has the memory for its body (`too large to read`), the body is all there (`cut short`) and both hashes match (`damaged (its hash does not match)`). A file that went between its header's reading and its own gives `gone`. A key list of another key version or record size does not fail the file: its Vulkan data is used, its keys are not.

**A key record** is 32 bytes. Its first 28 are the renderer's key for one pipeline (`pipe_key_t`), the render pass's formats among them; most of its values are Vulkan's own. A key from a file is checked before anything is made from it, against the last column; one that fails is dropped (a list outlives a change of host GPU, and names the old one's formats).

| Offset | Size | Contents | Taken from a file when |
|---|---|---|---|
| 00h | 4 | The colour attachment's format (a VkFormat) | One of the renderer's own render formats on this host GPU (not VK_FORMAT_UNDEFINED), which the host GPU takes as a colour attachment and blends |
| 04h | 4 | The depth attachment's format | VK_FORMAT_UNDEFINED (0: none), or one of the renderer's own render formats on this host GPU, which the host GPU takes as a depth attachment |
| 08h | 1 | What each vertex shader invocation draws: 0 a vertex, 1 a triangle's corner with WRAPn, 2 a triangle's vertex in point fill, 3 a corner of a wireframe edge's band, 4 a corner of a line's band, 5 a corner of a point's square, 6 a corner of the square of a triangle's vertex in point fill | At most 6 |
| 09h | 1 | The vertex stage: 0 pre-transformed, 1 fixed function, 2 a vertex program | At most 2 |
| 0Ah | 1 | The topology (VkPrimitiveTopology) | At most VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN |
| 0Bh | 1 | The cull mode (VkCullModeFlags) | At most VK_CULL_MODE_FRONT_AND_BACK |
| 0Ch | 1 | 1: the fragment shader computes the depth (its computed-depth variant) | 0 or 1 |
| 0Dh | 1 | 1: blending | 0 or 1 |
| 0Eh | 1 | The source blend factor (VkBlendFactor) | At most VK_BLEND_FACTOR_SRC_ALPHA_SATURATE |
| 0Fh | 1 | The destination blend factor (VkBlendFactor) | At most VK_BLEND_FACTOR_SRC_ALPHA_SATURATE |
| 10h | 1 | 1: the depth test | 0 or 1 |
| 11h | 1 | 1: depth writes | 0 or 1 |
| 12h | 1 | The depth test's function (VkCompareOp) | At most VK_COMPARE_OP_ALWAYS |
| 13h | 1 | 1: the stencil test | 0 or 1 |
| 14h | 1 | The stencil operation on a stencil failure (VkStencilOp) | At most VK_STENCIL_OP_DECREMENT_AND_WRAP |
| 15h | 1 | The stencil operation on a depth failure (VkStencilOp) | At most VK_STENCIL_OP_DECREMENT_AND_WRAP |
| 16h | 1 | The stencil operation on a pass (VkStencilOp) | At most VK_STENCIL_OP_DECREMENT_AND_WRAP |
| 17h | 1 | The stencil test's function (VkCompareOp) | At most VK_COMPARE_OP_ALWAYS |
| 18h | 1 | The blend operation (VkBlendOp) | At most VK_BLEND_OP_MAX |
| 19h | 1 | The colour write mask (VkColorComponentFlags) | At most 15 |
| 1Ah | 1 | 1: a pixel program runs instead of the texture stages | 0 or 1 |
| 1Bh | 1 | 0 | 0 |
| 1Ch | 4 | The session the pipeline was last used in | |

Key version 1 (`PCACHE_KEY_VERSION`) is this layout with these meanings; it is raised whenever either changes.

## The stamp

The header's stamp names the code that made Vulkan's data. It is the hash (above), from 811C9DC5h XOR `PCACHE_PIPE_VERSION`, over the SPIR-V of the three shaders as built into 86Box, in turn: `vgpu98.vert`, `vgpu98.frag`, and `vgpu98.frag` built with `COMPUTED_DEPTH` (its computed-depth variant). `PCACHE_PIPE_VERSION` (now 1) is raised whenever anything else a pipeline is made from changes: the fixed states, dynamic states and specialization constants `pipe_make` gives, the render pass `pass_make` makes, and the pipelines' layout (its sets, bindings and counts). 86Box works its stamp out once, at start.

A file with another stamp holds data no pipeline of this 86Box will ask for: its Vulkan data is neither used nor merged into a save, so it is not saved again. Its keys still are ([Warm-up](#warm-up)).

## Vulkan's header

Vulkan's data starts with Vulkan's own pipeline cache header (VkPipelineCacheHeaderVersionOne, in the Vulkan specification). 86Box checks it before the data goes to the driver, and before a save merges a file's data:

| Offset | Size | Contents | Must be |
|---|---|---|---|
| 00h | 4 | The header's size | At least 32, and within Vulkan's data |
| 04h | 4 | The header's version | 1 (VK_PIPELINE_CACHE_HEADER_VERSION_ONE); another version is left to the driver, and the rest is not checked |
| 08h | 4 | The vendor ID | The host GPU's (VkPhysicalDeviceProperties) |
| 0Ch | 4 | The device ID | The host GPU's |
| 10h | 16 | The pipeline cache UUID | The host GPU's driver's |

A driver ignores data that is not its own anyway; the check makes the log say so. Data shorter than 32 bytes, or a version 1 header that does not match: the data is not used (`the host GPU or its driver changed`).

## Reading

At the renderer's start, with the setting On:

1. Both headers are read and checked. The newer of those that check out is chosen by number, which wraps: file 1 is newer when its number minus file 0's, as a signed 32-bit value, is above 0; on a tie, file 0.
2. Neither checks out: the cache starts empty, with file 0's reason when it exists, else file 1's, else `none saved yet`.
3. The newer one's number is the number loaded, and this session's number is one past its session, whatever is read. The other file, when it is not the length its header says, is named in the log. The newer is read whole. It fails and the older's header checked out: the log names the newer and why, and the older is read whole instead. It fails too, or there was no older to read: the cache starts empty, with the reason of the last file read.
4. A file read whole: its Vulkan data is used unless its stamp is not this 86Box's (`the GPU's shaders changed`), vk_bytes is 0 (`emptied at its limit`) or Vulkan's header does not match (`the host GPU or its driver changed`).
5. Its key list, of key version 1 with 32-byte records, is kept whatever became of the data: each key checked (above), the most recently used first. It goes into every save, and into a warm-up.
6. The data goes to `vkCreatePipelineCache`. The driver refuses it: the cache starts empty (`the host's driver refused it`). Without even an empty cache, pipelines are made without one, nothing is warmed and nothing is saved.
7. With keys kept, the listed pipelines are readied in the background, whether Vulkan's data was used or not ([Warm-up](#warm-up)).

With no header checking out, this session is session 1 and the number loaded is 0.

## Saving

- **When:** at the end of a batch on the GPU thread, when pipelines were made since the last save (by the GPU thread, for the DirectX renderer, the Glide device or the OpenGL device, or by the renderer's warm-up: [Warm-up](#warm-up); the OpenGL device's warm-up asks for none of its own) and two minutes have gone by since the last save or its last try; and at close. A save runs on a thread of its own ("VGPU98 pipeline cache"), one at a time: while one still writes, the next waits for the next chance. When no thread can be made, the save runs on the GPU thread. While the GPU thread starts a save, the watchdog names its step `saving the pipeline cache` ([registers.md](registers.md), Diagnostics).
- **At close:** as the GPU's close begins (beside the display's), a warm-up still running is told to stop after the pipelines in hand. The renderer's close waits for it, up to 10 seconds; then for a save still writing, up to 30 seconds; then, with pipelines made since the last save, the last save runs on the closing thread. A save or warm-up thread that has not ended in time is left as it is (86Box is closing: what it uses is not freed under it), and the log says so. Such a thread keeps only the renderer, with what it uses (the host GPU's Vulkan device and the cache; for a warm-up, the shaders, the pipelines' layout and its sets and the key list too), and the command processor's record, through which a save logs and opens its file. Neither reads video memory or the 86Box device, which go.
- **Which file:** the save reads both headers on disk at that moment and writes the file that is not the newest whose header checks out (file 0 when neither does), numbered one past that newest (one past the number loaded or last saved when neither checks out). The newest stays whole, whatever becomes of the save. The newest is read whole, to be merged; when it fails and the older reads, the older is merged and the save replaces the damaged newest instead.
- **Vulkan's data:** this cache's, merged with the file read when its stamp is this 86Box's, its vk_bytes not 0 and its Vulkan header the host's (`vkMergePipelineCaches`, into a temporary cache: the file's entries go back to disk, never into the cache in memory). Every save merges, so what another 86Box, an earlier session or a Start-empty session's others saved stays. The merge past 64 MB: this cache's data alone. This cache's alone past 64 MB: [The cap](#the-cap). The data is asked of the driver again, up to four times, when pipelines made meanwhile grow it.
- **The save's session:** this session's number, or the file read's session when that is later (a session that read nothing at start, another 86Box saving since), so the merged keys age as the file's do. The header and this session's keys carry it.
- **The keys:** this session's, the list loaded and the file read's, merged ([The key list](#the-key-list)).
- **The header** as above, with this 86Box's stamp and the save's session.
- **A failure** leaves the newest file whole, and is named once a session for each kind: no data from the driver; no memory for the data; the file could not be opened; it could not be written whole. A save that failed is tried again two minutes later, and at close.

## The setting

**Pipeline cache (shared by every machine)**, config key `pipeline_cache` ([registers.md](registers.md), 86Box settings). The files are every machine's; the setting is each machine's. The values are never renumbered.

| Value | Setting | What it does |
|---|---|---|
| 0 | **On** (the default) | The cache is read at start and saved, as above |
| 1 | **Start empty** | This session reads nothing and warms nothing, so every pipeline is made anew. Only the newest header is read, so its number and session go on. It still saves, and every save merges what is on disk: other machines keep their pipelines, and the file's keys age |
| 2 | **Off** | The cache is neither read nor saved, for a suspected cache problem. The session's pipelines go through an empty cache of its own |

The start line names the setting when it is not On ([The log](#the-log)), and so does the card's first line at power-on (`, pipeline cache "Start empty"` or `, pipeline cache Off`).

## The key list

Each pipeline the GPU thread makes for the DirectX renderer is recorded by its key (a key record's first 28 bytes). The Glide and OpenGL devices' pipelines go into Vulkan's data and count toward the next save, but are not listed (OpenGL's listing is deferred). A save merges three lists: this session's pipelines, with the save's session ([Saving](#saving)); the list loaded at start; and the file the save read, of key version 1. Each key is kept once, with the latest session it was used in; a session ahead of the save's (another 86Box's count) is taken as the save's. A key last used 16 or more sessions before the save's (`PCACHE_KEY_AGE`) goes. The rest are sorted the most recently used first (keys of one session by their bytes), and the first 4,096 (`PCACHE_KEYS`) are saved; past the cap, half of those, rounded up. A pipeline the warm-up makes again keeps its key's session until the GPU thread uses it.

## Warm-up

Whenever keys were kept at start (the setting On), whatever became of Vulkan's data, the listed pipelines are made in the background as 86Box starts. Those already in the data take milliseconds; the rest (all of them after a change of shaders, host GPU or driver, or the cap; those of a warm-up cut short last time; data the driver silently ignored) are made before their first use. Start empty and Off warm nothing.

- **Two threads** ("VGPU98 pipeline warm-up") start once the renderer's shaders and pipelines' layout are made, before the `VGPU98 3D: ready` line; one, when the second cannot be made. Each takes the next key, the most recently used first. When none can start (or the event that waits for them cannot be made), the log says so and nothing is warmed.
- **Each pipeline** is made into the session's Vulkan cache, by the same code as the GPU thread's (`pipe_make`), and then destroyed: the GPU thread's own request for it later finds the work done. A warm-up thread makes its own render passes from the key's formats (`pass_make`), and keeps 16 of them for the keys that follow.
- **A key that cannot be made** is counted, and the thread goes on with the next; a lost device (the host GPU failed) stops the thread.
- **At close** the threads are told to stop as the GPU's close begins, and stop after the pipelines in hand; the renderer's close waits for them by an event, up to 10 seconds ([Saving](#saving), At close).
- **Saves:** a warm-up started cold (Vulkan's data not used) makes only new pipelines, and every one counts toward the next save ([Saving](#saving)). One started from the data finds most of them there: it asks for a save only at its end, and only when the cache's data grew.
- **Its end** is named once, after its last thread ends: on the GPU thread, or at close on the closing thread. A warm-up with a thread left at close is not named.

### The OpenGL device's warm-up

The OpenGL device's pipelines are not in the key list, so the warm-up above never makes them; a release that changes `ogl.frag` would leave the first OpenGL draw making its shaders' first pipeline cold, which can take over a second. So the device readies a fixed set of its own ([opengl.md](opengl.md), Pipelines, the uniform block and the data chunks):

- **When:** as the OpenGL device starts, after its render passes, shaders and layout are made, when the renderer has a cache and the setting is neither Start empty nor Off. One thread of its own, "VGPU98 OpenGL warm-up", beside the renderer's warm-up threads; when it cannot be made, nothing is warmed and the log says nothing.
- **What:** 18 pipelines, each colour format (565, XRGB, ARGB) with each depth kind (none, D16, the 24-bit format with stencil), plainly and alpha-blended, made into the session's Vulkan cache by the same code as the OpenGL device's GPU thread (`pipe_build`) and destroyed at once. Its pipelines' keys are fixed, not from a list, and mostly not a game's states (no depth test, culling or stencil): what they leave done is chiefly the shaders' first making, which the game's own first pipelines then find in the cache.
- **Saves:** it asks for none of its own. What it made goes to disk with the next save something else brings: a pipeline the GPU thread makes (an OpenGL program's first draws make theirs), or the renderer's warm-up's end, when the cache's data grew by then.
- **At close** the OpenGL device's close tells it to stop after the pipeline in hand and waits for it, up to 10 seconds, before the device frees its shaders and render passes. One still running then is left as it is: the device keeps its own objects and itself, not freed under the thread, and the GPU's close keeps the renderer (this cache and its device) and the GPU's record, as for the renderer's warm-up ([Saving](#saving), At close), and says so ([The log](#the-log)).
- **Its end** is an event line of the OpenGL device's, `VGPU98 OpenGL:` n `pipelines readied in the background in` m `ms`, with ` (some could not be made)` when any failed and `, stopped by the close` when the close stopped it short of the 18 ([The log](#the-log)).

## The cap

Vulkan's data is saved only up to 64 MB (`PCACHE_CAP`). Past it (this cache's data alone; when only the merge is past it, this cache's alone is saved):

- The save writes the key list alone (vk_bytes 0), halved to its most recently used. It is named once.
- From then on, every save of this session writes the key list alone, merged and halved likewise: the list stays at about half of what filled the cache, so the next start's warm-up fits.
- The cache in memory stays as it is, and keeps the session's pipelines.
- The next start finds `emptied at its limit` and makes the listed pipelines again in the background.

With several machines this is kept as designed. A machine past the cap writes its key list alone over the file that is not the newest, as every save does, so after two such saves neither file holds the other machines' Vulkan data unless they saved since; their next start finds no data and makes the listed pipelines (theirs among them, merged) again in the background. Two machines whose merge is past the cap, each within it alone, take turns saving their own data alone, with both lists' keys; each start's warm-up makes what the data it finds lacks.

## Several 86Box at once

Several 86Box share the files. Every save reads the newest readable file on disk and merges it ([Saving](#saving)): Vulkan's data when its stamp and Vulkan's header match, its keys when their version does. So another 86Box's pipelines stay, whichever saves last. When the newest file's number is not the number this 86Box loaded or last saved, another 86Box has saved meanwhile; the first time a save that worked took in that file's data or keys, the log says so (once a session). Two 86Box saving the same file at the same moment tear it: its length or its hash catches that, the newest stays whole, and the next save (two minutes later, or at close) merges again. There are no locks and no renames: plain C, on every platform. Past the cap, one machine's saves can drop the others' Vulkan data ([The cap](#the-cap)).

## The log

With Diagnostics at any level but Off (the events: [registers.md](registers.md), Diagnostics), each line after the host's date and time. One line at start; one when a warm-up ends, or cannot start; one at close for a thread left running; one for each kind of failure, and for the cap, once a session; one the first time another 86Box's save is merged. A save that works says nothing. `<...>` stands for a value.

| When | Line |
|---|---|
| Start, Vulkan's data used, keys listed | `VGPU98 3D: pipeline cache: <N> KB from <file>; its <K> listed pipelines readied in the background` |
| Start, Vulkan's data used, no keys | `VGPU98 3D: pipeline cache: <N> KB from <file>` |
| Start, the cache empty, keys listed | `VGPU98 3D: pipeline cache: started empty (<reason>); its <K> listed pipelines made again in the background` |
| Start, the cache empty, no keys | `VGPU98 3D: pipeline cache: started empty (<reason>)` |
| Start, the setting Start empty | `VGPU98 3D: pipeline cache: started empty (the setting is Start empty)` |
| Start, the setting Off | `VGPU98 3D: pipeline cache: Off (neither read nor saved)` |
| Start, before the start line: the other file not the length its header says; the newer failing its check, the older's header checking out | `VGPU98 3D: pipeline cache: <file> failed its check (not the length its header says)`; `VGPU98 3D: pipeline cache: <file> failed its check (<why>)` |
| A warm-up that cannot start | `VGPU98 3D: pipeline cache: the background warm-up could not start` |
| A warm-up's end | `VGPU98 3D: pipeline cache: <made> of <K> listed pipelines ready in <S> s`, then `; <F> could not be made` when any failed, then `; stopped at close` (86Box closed first) or `; stopped: the host GPU failed` (a lost device) when not every key was tried |
| The OpenGL device's warm-up's end | `VGPU98 OpenGL: <made> pipelines readied in the background in <M> ms`, then ` (some could not be made)` when any failed, then `, stopped by the close` when the close stopped it short of the 18 ([The OpenGL device's warm-up](#the-opengl-devices-warm-up)) |
| Close, the OpenGL device's warm-up not ended in time | `VGPU98 OpenGL: the warm-up did not end within 10 s of the close; its objects left as they are` |
| Close, a save or warm-up thread not ended in time | `VGPU98 3D: at close, the pipeline cache's save had not ended: left as it is`, or `warm-up` for `save` (the save named when both) |
| A save that worked taking in another 86Box's (the first) | `VGPU98 3D: pipeline cache: another 86Box's save merged into this one's` |
| A save past the cap (the first) | `VGPU98 3D: pipeline cache: past 64 MB: from now on its pipelines' keys alone are saved; the next start makes the <K> most recently used again in the background` |
| A save without data (each kind once) | `VGPU98 3D: pipeline cache: not saved (the host's driver gave no data)`, or `(no memory for its data)` |
| A save whose file failed (each kind once) | `VGPU98 3D: pipeline cache: saving to <file> failed (it could not be opened)`, or `(it could not be written whole)` |

`<S>` has one decimal. The reasons a cache starts empty: `none saved yet`; a file and why it failed, `<file>: <why>`; `the GPU's shaders changed`; `emptied at its limit`; `the host GPU or its driver changed`; `the host's driver refused it`. Why a file fails: `cut short`, `not a pipeline cache`, `another 86Box's format`, `a header of another size`, `damaged (its header's hash does not match)`, `its sizes past their limits`, `not the length its header says`, `too large to read`, `damaged (its hash does not match)`, `gone` ([The format](#the-format)).
