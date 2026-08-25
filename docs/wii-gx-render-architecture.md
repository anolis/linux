# Wii GX render architecture

## Status

This document records the render-driver architecture and its remaining path to
Mesa. It is a design record; feature discovery, not assumptions about a
particular kernel build, governs use of the private render ABI.

The accepted driver already demonstrates these operations on Wii hardware:

- DRM/KMS owns VI timing, vblank, XFB selection, and page flips.
- The optional GX provider accelerates RGB565 and XRGB8888 scanout while CPU
  conversion remains a functional fallback.
- GX submissions complete synchronously at the second PE-finish marker, after
  the final EFB-to-XFB copy.
- GX can render a deterministic primitive grid into EFB, copy it to MEM1 in
  native tiled RGB565 layout, invalidate and rebind the texture, and sample it
  in a later visible frame.

Stages 1 through 3 below are now implemented and hardware-accepted. The driver
has platform-resource ownership, a bounded MEM1 allocator, primary and render
nodes, per-file contexts, typed GEM objects, reservation fences, syncobjs, and
validated copy, fill, rectangle, scaling, and format-conversion operations.
Standard RGB565 KMS scanout sustains the full display cadence. Direct scanout
of private tiled objects still awaits a standard DRM modifier, and no Mesa
driver exists yet.

The first Stage 5 operations are hardware-accepted: an untextured,
Gouraud-shaded triangle with three screen-space RGBA8 vertices, plus batches of
up to 64 such triangles against one destination, EFB restore, primitive
stream, copyback, and fence. Both operations establish semantic primitive
submission without exposing raw GX packets, registers, or addresses.

## Non-negotiable constraints

1. EFB is one global on-chip render target, not normal system RAM.
2. The current GX texture path can address MEM1, not ordinary MEM2-backed GEM
   shmem. GPU-visible buffers therefore need a dedicated MEM1 allocator.
3. CPU and GX accesses are non-coherent. Every ownership transition needs an
   explicit cache operation and a completion fence.
4. The CP, XF, rasterizer, TEV, PE, texture cache, and copy engine form one
   ordered pipeline. FIFO consumption alone is not job completion.
5. VI and XFB ownership stays in `gcn-drm`. A render client must never program
   VI registers or choose a physical XFB page.
6. Userspace must never receive physical addresses or direct MMIO access.
7. A raw, unvalidated GX FIFO is not an acceptable UAPI. Texture and copy
   addresses, PE markers, FIFO control, and reserved registers must remain
   kernel-owned.
8. Module removal and hardware failure must leave linear DRM scanout and CPU
   conversion usable.

## Current MEM1 map

| Range | Size | Current owner |
| --- | ---: | --- |
| `0x01300000-0x01480000` | 1536 KiB | Two maximum-size GX RGB565 texture slots |
| `0x01500000-0x01600000` | 1024 KiB | Shared OHCI DMA pool |
| `0x01600000-0x01680000` | 512 KiB | Bounded userspace GX render-object pool |
| `0x01684000-0x01694000` | 64 KiB | GX command FIFO |
| `0x01698000-0x01800000` | 1440 KiB | Two maximum-size YUYV XFB pages |

The texture window holds only two 640x576 RGB565 staging surfaces. The separate
512 KiB render-object pool is a deliberately bounded `drm_mm` heap and cannot
hold even one full 640x480 RGB565 object. Full-frame composition therefore uses
system GEM objects and the accepted staged conversion path; MEM1 objects are
for smaller reusable textures and render targets. Changing either partition
requires a measured regression and must not silently consume the OHCI, FIFO,
XFB, or wrapper safety gaps.

## Target ownership model

```text
 userspace
 +----------------------+       +-------------------------+
 | Mesa / 2D compositor |       | KMS client / fbcon      |
 +----------+-----------+       +------------+------------+
            | render ioctls                  | atomic KMS
            v                                v
 +---------------------------------------------------------+
 | gcn-drm                                                |
 | - primary and render nodes                             |
 | - GEM handles and dma-resv fences                      |
 | - MEM1 allocation policy                               |
 | - VI, vblank, inactive-XFB selection, page publication |
 | - provider lifetime and CPU scanout fallback           |
 +--------------------------+------------------------------+
                            | validated kernel jobs
                            v
 +---------------------------------------------------------+
 | gcn-gx provider                                        |
 | - CP/PE/PI FIFO ownership and command generation       |
 | - complete GX state restore per context/job            |
 | - texture cache and CPU dcache transitions             |
 | - EFB render/copy sequencing and PE completion fences  |
 | - timeout diagnosis and hardware reset                 |
 +--------------------------+------------------------------+
                            |
                            v
                  CP -> XF -> raster -> TEV -> PE
                            |                 |
                           EFB        MEM1 texture / XFB
```

`gcn-drm` remains the stable device exposed to userspace. The optional
`gcn-gx` module remains the sole GX hardware owner and registers an expanded
kernel provider. Render ioctls may exist while the provider is absent, but
must return `-ENODEV`; KMS scanout continues through CPU conversion.

The lock order is DRM provider/lifetime lock, GX submission lock, then local
buffer bookkeeping. Provider unregister waits for every in-flight render and
scanout callback before unmapping hardware or MEM1.

## Stage 1: resource model, no new UAPI

The current hard-coded addresses and broad `ioremap(0x0c000000, 0x9000)` are
acceptable bring-up scaffolding but not a render-driver foundation.

1. Replace raw `/memreserve/` entries for GX texture, FIFO, and XFB storage
   with named reserved-memory nodes and explicit device references.
2. Add a GX device-tree node describing CP and PE register windows, finish
   interrupt, FIFO memory, and texture memory. Model the shared PI FIFO
   registers without claiming or remapping VI-owned registers.
3. Convert `gcn-gx` to a platform driver. Map only declared resources and
   reject missing, overlapping, undersized, misaligned, or non-MEM1 regions.
4. Add a `drm_mm` allocator over the GX texture pool. Reserve the scanout
   staging allocation explicitly instead of deriving two pointers from fixed
   constants.
5. Represent internal GX buffers with size, physical address, CPU mapping,
   layout, access state, and allocation node. No physical address crosses a
   userspace boundary.
6. Add allocator and range-validation tests that run without Wii hardware.
7. Re-run the complete RGB565/XRGB8888 scanout, unload/reload, final-copy
   fence, and EFB-texture round-trip tests on hardware.

No render node or ioctl should be added in this stage. Resource ownership must
be correct before it becomes permanent ABI.

## Stage 2: MEM1 GEM objects and discovery

After Stage 1 passes hardware regression, add `DRIVER_RENDER` and
`DRIVER_SYNCOBJ` to the existing DRM device and define a small versioned UAPI:

- `GET_PARAM`: ABI version, MEM1 capacity, allocation alignment, maximum EFB
  dimensions, supported texture layouts, and feature bits.
- `GEM_CREATE`: allocate a zeroed MEM1 object with a declared format/layout.
- `GEM_MMAP`: return the standard GEM mmap offset. CPU mappings never reveal
  the physical address.
- `CTX_CREATE` and `CTX_FREE`: allocate per-file software state even though
  the first implementation serializes all work onto one hardware EFB.
- `WAIT`: wait on a BO reservation or job fence using an absolute timeout.

All structures use fixed-width types, `__u64` for userspace pointers, explicit
padding, and zero-required flags. Initial BO support is limited to RGB565 in
the proven GX 4x4 tiled layout and to sizes that fit the MEM1 pool. PRIME
export/import remains disabled until cache coherency and addressability are
defined for imported objects.

The tiled layout eventually needs a DRM format modifier before such BOs can
be used directly as framebuffers. The kernel currently has no Nintendo DRM
modifier vendor ID. Do not publish an invented modifier value; obtain an
assignment or keep the layout private until the upstream naming is settled.

## Stage 3: validated 2D jobs

The first submission ABI should accelerate desktop composition without
accepting arbitrary FIFO bytes. A bounded job contains kernel-defined
operations such as:

- clear EFB;
- draw solid rectangles;
- draw textured rectangles from MEM1 RGB565 BOs;
- copy EFB into a MEM1 RGB565 BO; and
- finish and signal an output syncobj.

Every referenced BO is supplied as a GEM handle with read/write access flags.
The kernel checks operation count, total byte size, dimensions, arithmetic
overflow, format/layout compatibility, rectangle bounds, aliasing, and BO
ownership before generating commands. The kernel inserts all physical
addresses, texture invalidations, PE markers, and final fences.

Jobs are serialized initially because there is one EFB and one CP FIFO. Each
job carries optional input and output syncobjs. BO `dma_resv` fences prevent
CPU access, another render job, or KMS scanout from racing GPU writes. A small
kernel workqueue and `dma_fence` are sufficient initially; adopt
`drm_gpu_scheduler` only when preemption, priorities, or multiple queues have
a demonstrated need.

## Stage 4: direct KMS scanout of GX-tiled BOs

Once a format modifier is assigned, advertise RGB565 plus the GX tiled
modifier on the primary plane. The KMS path waits on the BO reservation,
samples the tiled BO directly, renders it to EFB, copies to DRM's inactive
XFB, waits for the final PE marker, and only then arms the page flip.

Linear RGB565 and XRGB8888 remain supported through the existing staging and
CPU-fallback paths. Userspace never submits an XFB address or changes VI state.

This stage removes the CPU retiling pass for frames produced by GX itself and
is the bridge from a 2D renderer to an accelerated desktop compositor.

## Stage 5: Mesa and broader GX state

The initial Mesa target should match demonstrated fixed-function hardware,
not claim modern programmable OpenGL. A Gallium driver can first expose the
subset implementable with GX vertex formats, transforms, TEV stages, texture
formats, depth, blend, scissor, and copy operations.

Two UAPI directions remain possible:

1. Extend validated semantic operations with bounded pipeline-state and draw
   descriptions. This is safer and easier to review but expands kernel ABI.
2. Add a strict GX packet parser with GEM relocation records. The parser must
   track VCD/VAT state to determine vertex payload sizes and must reject or
   rewrite every address-bearing, FIFO-control, token, finish, and copy-target
   register. This is more flexible but substantially harder to secure.

Do not choose between them until the Stage 3 compositor path and a Mesa
prototype show what command frequency and state coverage are actually needed.
Never expose the existing diagnostic FIFO buffer as the shortcut.

## Recovery and observability

Every job records a monotonic sequence, submit timestamp, completion
timestamp, PE-finish delta, final token, FIFO read/write offsets, and errno.
Expose aggregate counters and the last failed job through debugfs, not UAPI.

On timeout:

1. stop accepting render jobs;
2. preserve diagnostic registers and the bounded generated command stream;
3. disable and reset CP/PE using the already validated ordering;
4. fail outstanding fences with an error;
5. restore the kernel-owned baseline GX state; and
6. keep KMS operational through CPU conversion if recovery fails.

No render client may wedge fbcon or prevent provider removal indefinitely.

## Immediate implementation slice

Add explicit viewport/scissor and blend/depth state to the accepted primitive
batch in the next slice. Keep state semantic and bounded: fixed-width values,
known enums, destination-relative rectangles, finite depth ranges, and no raw
BP/XF register payloads. Establish disabled-state equivalence first, then add
one positive visual and readback control for each enabled state while retaining
the complete batch and 2D regression suite.

After state passes, add indexed vertex buffers, RGB565 textures and samplers,
then a bounded TEV combiner description in the order required by a Gallium
prototype. Each extension remains feature-bit-gated and must have a positive
hardware control before Mesa depends on it.
