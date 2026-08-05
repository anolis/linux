# libogc hardware concordance for Wii Linux

This document maps the hardware contracts implemented by libogc onto the
legacy Wii Linux framebuffer and the Linux 6.18 DRM/GX work. Its purpose is to
replace register-by-register guessing with source-derived subsystem models.

The reference tree is devkitPro/libogc commit
[`99929510c8dd5b0dd69aaae26a93105f09d182de`](https://github.com/devkitPro/libogc/tree/99929510c8dd5b0dd69aaae26a93105f09d182de),
dated 2026-06-12. All libogc claims below refer to that exact revision.

This is a whole-tree architectural inventory and a deep audit of the
hardware-facing foundations relevant to display, rendering, DMA, interrupts,
and lifecycle management. It is not a line-by-line review of unrelated codec,
filesystem, network-protocol, or utility algorithms. Libogc is an important
working reference implementation, but it is not a silicon specification;
source-derived behavior remains distinct from a documented hardware guarantee.

## Evidence labels

- **Pinned source**: directly present in libogc commit `99929510c8dd`.
- **Linux source**: directly present in this kernel tree.
- **Proven on hardware**: established by a validated Wii test.
- **Inference**: a model consistent with the source and tests, not a proven
  silicon contract.
- **Unknown**: a question that must remain open.

## Hardware-facing source map

| Domain | Primary libogc sources | Contract exposed to Linux work |
| --- | --- | --- |
| Boot and exceptions | `tuxedo/common_crt0.S`, `tuxedo/exception.c` | Establish CPU runtime, exception vectors, and low-level ownership before libraries initialize devices. |
| Interrupt controller | `tuxedo/interrupt.c`, `gc/tuxedo/interrupt.h`, `gc/ogc/irq.h` | Decode PI causes, preserve unrelated mask bits, and let each subsystem own acknowledgment of its status source. |
| System lifecycle | `libogc/system.c`, `libogc/stm.c` | Initialize shared facilities, register reset callbacks, quiesce clients, and explicitly disable VI for final power/reboot transitions. |
| Cache and addressing | `libogc/cache.c`, `libogc/cache_asm.S`, `gc/ogc/cache.h`, `gc/ogc/system.h` | Treat physical addresses, cached aliases, cache-line alignment, publication barriers, and DMA visibility as explicit contracts. |
| Video Interface and AVE | `libogc/video.c`, `gc/ogc/video.h` | Import or initialize VI, build shadow mode state, commit at retrace, encode XFB addresses, and initialize the external encoder. |
| GX, CP, PE, PI | `libogc/gx.c`, `libogc/gx_regdef.h`, `gc/ogc/gx.h` | Own FIFO linkage, write gather, complete fixed-function state, texture/EFB/XFB flow, completion markers, and abort recovery. |
| Serial Interface | `libogc/si.c`, `libogc/pad.c` | Coordinate controller transfers, VI-derived sampling timing, IRQ acknowledgment, and reset callbacks. |
| EXI and removable media | `libogc/exi.c`, `libogc/card.c`, `libogc/gcsd.c`, `libogc/sdgecko_io.c`, `libogc/usbgecko.c` | Serialize shared-bus access, manage transfer-complete/external interrupts, and keep per-device callbacks outside raw IRQ mechanics. |
| DSP, ARAM, and audio | `libogc/dsp.c`, `libogc/aram.c`, `libogc/audio.c` | Program DMA in aligned physical units, flush or invalidate around ownership changes, and acknowledge shared DSP status carefully. |
| Disc Interface | `libogc/dvd.c` | Publish DMA buffers and commands in order, distinguish command/status registers, and complete asynchronous requests from DI IRQs. |
| IOS IPC and services | `libogc/ipc.c`, `libogc/ios.c`, `libogc/stm.c`, `libogc/es.c`, `libogc/isfs.c` | Marshal physical pointers through flushed request blocks, invalidate returned data, and use IOS for Wii-specific service ownership. |
| USB and networking | `libogc/usb.c`, `libogc/usbstorage.c`, `libogc/network_wii.c`, `libogc/ogc_sockets/` | Mostly higher-level consumers of IOS or device APIs; useful for asynchronous ownership patterns but not a source of GX/VI register truth. |

## Recurring design language

The same rules recur across independent libogc subsystems.

### One owner performs the whole transaction

Code that starts a hardware operation also owns its buffer publication,
interrupt source, completion state, and cleanup. GX does not rely on VIDEO to
make its FIFO coherent, and VIDEO does not rely on GX to commit VI registers.
Linux should preserve that split:

- KMS owns VI timing, XFB presentation, vblank, and the AVE connector side.
- A GX render driver owns CP/PI FIFO submission, texture visibility, EFB work,
  and PE completion.
- A buffer handoff or fence connects those owners; shared register writes do
  not.

### Initialization branches on inherited state

Libogc does not blindly reset every block. `VIDEO_Init()` checks DCR enable:

```c
if (!(_viReg[1] & 0x0001))
        __VIInit(VI_TVMODE_NTSC_INT);
```

An already active VI is imported. A disabled VI receives the cold reset and
default NTSC setup. This is a concrete warning against treating a cold-init
sequence as the normal ownership-handoff sequence.

### State changes are transactions, not final-value bags

VIDEO builds ordinary and shadow register sets. `VIDEO_Flush()` publishes the
changed mask; the retrace handler applies the shadow in register-index order.
GX similarly keeps a large software shadow, emits dirty state before drawing,
and initializes state families together.

This matters because a readable final value does not describe:

- the order in which clock, reset, mode, and address fields changed;
- whether a strobe or write-one-to-clear action occurred;
- the field or pipeline phase at the time of a write;
- hidden state downstream of a readable register.

### DMA visibility is directional

Libogc consistently distinguishes producer and consumer ownership:

- CPU to device: finish CPU writes, flush/store cache lines, publish the
  physical address or write pointer, then start the device.
- Device to CPU: invalidate the destination before or after completion as the
  API requires, then read it only after the completion event.
- Device to device: use the downstream block's completion marker, not merely
  the upstream command-consumption state.

`DCFlushRange()` includes a synchronization barrier. The `NoSync` variants
explicitly do not guarantee that data has reached memory yet. GX flushes eight
zero words to force one 32-byte write-gather burst, then executes `ppcsync()`.

### Register semantics are typed

Libogc code masks status bits before preserving writable state and writes
specific bits back to acknowledge events. This implies a practical review
rule: every MMIO field must be classified as stored state, read-only status,
write-one-to-clear status, command strobe, self-clearing control, or unknown.
Generic read-modify-write is not safe until that classification is known.

### Shutdown is part of the driver contract

GX registers a system reset callback that flushes and aborts the frame before
final reset. Wii STM power and reboot functions explicitly set VI DCR to zero.
Initialization without corresponding quiesce and recovery behavior is an
incomplete port.

## VI and AVE concordance

Primary pinned source:
[`libogc/video.c`](https://github.com/devkitPro/libogc/blob/99929510c8dd5b0dd69aaae26a93105f09d182de/libogc/video.c).

### Libogc cold path

When VI is disabled on entry, `__VIInit()` performs this sequence:

1. Write DCR reset (`0x0002`).
2. Busy-wait 1000 loop iterations.
3. Write DCR zero.
4. Program timing, burst blanking, display interrupt, and scaler state.
5. Write enabled mode to DCR.
6. Select the 27/54 MHz VI clock in register index 54.

This is the only path in current libogc that pulses VI reset during
`VIDEO_Init()`.

### Libogc enabled-VI path

When DCR enable is already set, `VIDEO_Init()`:

1. Leaves DCR active and skips `__VIInit()`.
2. Programs filter coefficients and initial width state.
3. Imports scan and television mode from live DCR.
4. Initializes software mode and shadow structures from that state.
5. Clears pending VI interrupt status and installs the retrace handler.
6. Initializes AVE after the VI IRQ is installed.

Applications then normally call `VIDEO_Configure()`, select a framebuffer, and
call `VIDEO_Flush()`. The mode and address changes are applied by
`__VIRetraceHandler()` through `__VISetRegs()`, not by disabling VI.

If the scan mode changes, `__VISetRegs()` waits for the required field before
committing. Changed registers are written in ascending register-index order,
which places DCR index 1 before most timing/address state and clock index 54
near the end. The source preserves the DCR enable bit while changing mode.

### Libogc AVE transaction

On Wii, `__VISetupEncoder()` reads cable status from VI register index 55 and
writes the AVE sequence:

```text
0x6a = 1
0x65 = 3
0x01 = cable/region selection
0x00 = 0
0x71 = 0x8e8e
0x02 = 7
0x05 = 0
0x08 = 0
0x7a = 0
0x40.. = zero Macrovision block
0x0a = 0
0x03 = 1
0x10.. = gamma table
0x04 = 1
0x6e = PAL60 filter selection
```

Libogc does not write AVE register `0x62`. It also treats `0x04` as a command
write, not ordinary retained state. The Linux project's `0x62=0` component
write is therefore a legacy-Linux extension, not part of this libogc sequence.

### Linux comparison

| Behavior | Libogc | Legacy `gcn-vifb` | Current `gcn_drm` standalone mode |
| --- | --- | --- | --- |
| Probe with active VI | Import active mode; no reset | Pulse reset during probe | Quiesce DI, pulse reset in current test commit |
| Owner unbind | Not an equivalent runtime operation | Does not reset or disable VI | Clears DI on remove; leaves DCR active |
| Timing programming | Shadowed and retrace-committed after initialization | DCR enabled first, then timing written directly | DCR zero while timing is written, then enabled last |
| DCR reset | Only disabled-VI cold branch | Probe and final shutdown | Current isolated-reset test path |
| VI clock | Updated as part of changed shadow, late by register index | Written after timing while DCR active | Written while DCR is zero |
| XFB update | Shadowed, committed at retrace | Page selected in VI retrace path | Pending page selected in DI1/vblank IRQ |
| Cable status | Read VI index 55 | Read `VI_SEL` | Not currently consumed by connector state |
| AVE setup | After VI IRQ installation | Full setup when AVE attaches and during mode setup | Not owned by DRM; service/tools preserve external state |
| AVE oversampling `0x65` | `3` | `1` | Inherited unless diagnostic changes it |
| AVE component `0x62` | Not written | Writes `0` for component | Inherited; controlled diagnostics only |

The isolated DRM reset result does not reproduce libogc's normal enabled-VI
path. It reproduces only one element of the disabled-VI cold branch, with a
different delay and different clock/timing ordering.

### Color-phase implications

The existing hardware evidence places the nondeterministic color exchange
downstream of correct XFB bytes. This audit adds three constraints:

1. A reset pulse is not the normal libogc ownership-handoff operation.
2. Both libogc's warm path and legacy Linux program an already enabled VI;
   standalone DRM currently programs while DCR is zero.
3. Libogc's mode update is field-aware and retrace-committed, whereas the DRM
   standalone probe performs a synchronous disable/program/enable transaction.

These are differences worth controlling. They do not prove that one is the
color-phase cause.

## GX, CP, PI, and PE concordance

Primary pinned sources:
[`libogc/gx.c`](https://github.com/devkitPro/libogc/blob/99929510c8dd5b0dd69aaae26a93105f09d182de/libogc/gx.c),
[`libogc/gx_regdef.h`](https://github.com/devkitPro/libogc/blob/99929510c8dd5b0dd69aaae26a93105f09d182de/libogc/gx_regdef.h),
and
[`gc/ogc/gx.h`](https://github.com/devkitPro/libogc/blob/99929510c8dd5b0dd69aaae26a93105f09d182de/gc/ogc/gx.h).

### Initialization contract

`GX_Init()` establishes infrastructure before default draw state:

1. Create the finish wait queue and register a reset callback.
2. Clear the complete GX software shadow.
3. Initialize CP IRQ ownership and FIFO objects.
4. Configure one FIFO as both CPU and GP FIFO.
5. Initialize PE token and finish IRQ ownership.
6. Enable the Broadway write-gather pipe at physical `0x0c008000`.
7. Seed BP/CP/XF shadow register identities and TMEM regions.
8. Run `__GX_InitGX()` to establish complete fixed-function defaults.

The FIFO sequence disables GP reads before programming CP state, writes base,
end, watermarks, distance, write pointer, and read pointer, executes
`ppcsync()`, links matching CPU/GP FIFOs, clears FIFO interrupt conditions, and
only then enables GP reads.

The Linux memory-FIFO path does not have to imitate write gather internally,
but it must preserve the equivalent publication contract: complete command
bytes, flush their cache lines, program coherent CP and PI pointers, order the
MMIO writes, then allow CP to read.

### Complete default state is intentional

`__GX_InitGX()` initializes state families that are easy to overlook when
constructing a minimal command stream:

- VCD/VAT and vertex-cache state;
- position, normal, texture, and dual-texture matrices;
- viewport, scissor, offset, culling, clipping, line, and point state;
- channel count, material, ambient, and channel controls;
- all TEV orders, stages, konst selections, swap modes, and swap tables;
- indirect-texture stages and coordinate scales;
- fog and fog-range adjustment;
- blend, color update, alpha update, depth, depth compare location, dither,
  destination alpha, pixel format, field mask, and field mode;
- display-copy source, destination, scale, clamp, filter, gamma, and
  frame-to-field state;
- direct PE poke state and performance-metric reset.

This does not mean every Linux submission must replay all defaults. It means
omitted state must be deliberately inherited, proven irrelevant, or
initialized once under exclusive GX ownership. A partial state stream should
be reviewed as a dependency graph, not just compared by command count.

### Completion hierarchy

Different observations prove different pipeline progress:

| Observation | What it proves | What it does not prove |
| --- | --- | --- |
| CP read pointer reaches write pointer | CP consumed FIFO bytes | Rasterization or EFB modification |
| PE token value reaches BP `0x47/0x48` marker | Work reached the downstream PE marker in order | Intended pixel values without a visual/memory check |
| PE finish interrupt after BP `0x45=2` | Draw-done command completed | Correct rendering state |
| EFB-to-XFB copy followed by token | Copy command completed before CPU presentation | Source primitive actually changed EFB |
| Full-frame output or validated XFB capture | End-to-end visible result | Internal stage responsible for an error by itself |

The project has proven PE token-value polling on hardware. Earlier PE status
bit and performance-counter diagnostics did not pass positive controls and
remain unsuitable evidence.

### Corrected register identities

- BP `0x45=2` is PE draw done, matching `GX_SetDrawDone()` and `GX_DrawDone()`.
- BP `0x47` and `0x48` carry draw-sync token commands.
- BP `0x65` is texture LUT load configuration, not draw done.
- `GX_CopyDisp()` emits copy source, dimensions, stride, physical destination
  shifted by 5, and execute control. It does not implicitly wait for draw done.

These identities are now reflected in the Linux GX code and test ledger.

### Texture, EFB, and XFB flow

Libogc texture image and display-copy addresses are physical addresses shifted
by 5. Texture data must use the hardware tile format and be visible to the
texture fetch path. `GX_InvalidateTexAll()` brackets BP `0x66` invalidations
with texture-state flushes.

EFB is an on-chip render target, not a CPU framebuffer. The display copy engine
reads EFB, converts/copies to an XFB in system memory, and VI scans that XFB.
Consequently:

- a consumed primitive stream is not a displayed frame;
- a completed EFB copy must target a non-visible or otherwise safely owned XFB;
- VI must not select that XFB before downstream completion;
- CPU inspection of a PE-written XFB needs cache invalidation.

### Abort and module-unload implications

On Wii, `GX_AbortFrame()` waits for a pixel-engine memory counter to stabilize,
pulses PI register 6, cleans FIFO pointers, restores revision state, and
flushes. This is stronger than merely setting CP control to zero.

A production reloadable Linux GX module needs an equivalent bounded quiesce
model before releasing FIFO/texture memory or returning to CPU fallback. The
exact safe Linux sequence still requires hardware validation; copying the
libogc delay loops mechanically would not establish correctness.

## Interrupt concordance

The Tuxedo interrupt layer maps PI aggregate causes to subsystem IRQs,
including VI, PE token, PE finish, and CP. The subsystem handler then clears
the device-local source:

- VI reads each display interrupt register and clears its pending bit.
- PE token reads the token and acknowledges token status.
- PE finish acknowledges finish status, records completion, and wakes waiters.
- CP first clears spurious FIFO interrupt latches, reads CP status, then handles
  underflow, overflow, or breakpoint state.

The design rule for Linux is that interrupt-controller acknowledgment and
device-local acknowledgment are separate responsibilities. Masking a PI IRQ
does not replace clearing the VI, CP, or PE source.

## Cache, physical-address, and DMA concordance

Libogc uses cached `0x8...` and uncached `0xc...` aliases and converts both to
physical addresses before programming devices. Linux has different virtual
address machinery, but the hardware contract remains:

- MMIO registers receive physical addresses in their documented units.
- CPU virtual pointers are never submitted by truncation or assumption.
- Shared buffers follow directional cache maintenance.
- FIFO and XFB alignment is at least one 32-byte Broadway cache line where the
  hardware/API requires it.
- Pointer publication follows completed cache maintenance and a suitable
  ordering barrier.

This source pattern explains several bugs already corrected in the project:
the missing FIFO data-cache flush, the initial write pointer exposing zero
commands, and inconsistent PI/CP pointer state before enabling FIFO linkage.

## System lifecycle concordance

`SYS_ResetSystem()` invokes registered reset callbacks before the final reset
or power transition. GX's non-final callback flushes and aborts current work.
Wii STM standby, idle, and reboot paths explicitly write VI DCR zero before
requesting the IOS transition.

For Linux this suggests explicit lifecycle phases:

1. Stop new userspace submissions or framebuffer updates.
2. Complete or cancel outstanding rendering with a bounded timeout.
3. Disable and acknowledge device-local interrupts.
4. Detach scanout/render ownership without freeing referenced memory early.
5. On final shutdown, place VI/GX in a known quiescent state.
6. On recoverable module unload, preserve enough display state for the next
   owner or execute a documented handoff transaction.

## Contract matrix for current Linux work

| Contract | Legacy fbdev/GX | DRM/KMS | Status |
| --- | --- | --- | --- |
| Exclusive VI owner | Service and platform bind enforce one owner | Same | Implemented |
| Retrace page selection | DI1 path | DI1/vblank pending-page path | Implemented |
| CPU XFB cache publication | Explicit flush | Explicit flush | Implemented |
| GX FIFO cache publication | Explicit flush before pointer exposure | Not yet a DRM render path | Implemented in legacy GX |
| CP/PI physical pointer units | Full physical pointers | N/A | Corrected and proven to drain |
| PE downstream completion | Token-value marker | N/A | Proven in legacy GX |
| Render/source buffer ownership | Source-generation and completed-XFB handshakes | GEM shadow and double XFB | Implemented in current paths |
| Complete GX state ownership | Large explicit setup, but still hand-maintained | Future work | Needs systematic state audit |
| Bounded GX abort/unload | Worker teardown and fallback exist | Future work | Needs libogc-guided hardware validation |
| VI warm handoff behavior | Unbind leaves VI active | `program_mode=0` compatibility path exists | Available control, not current default |
| Field-aware mode commit | Direct legacy setup; flips at retrace | Probe programs synchronously; flips at retrace | Mode-set transaction needs improvement |
| AVE connector ownership | Legacy I2C child setup | External service/diagnostic tooling | Architectural gap |
| Cable/mode detection | Legacy uses VI_SEL and AVE | Fixed always-connected NTSC mode | Deliberate prototype limitation |

## Architecture-guided next experiments

These are ordered to maximize information while minimizing new variables.

### 1. Revalidate enabled-VI handoff under the current color fault

Use the existing `program_mode=0` DRM compatibility path without rebuilding.
Start from a recorded cold state, transfer ownership from legacy fbdev, and run
multiple complete DRM transactions while recording whether color remains
correct or swapped.

- Consistently correct handoff would localize the selector to standalone
  disable/program/enable behavior.
- Swapped handoff would show that the selected phase predates standalone DRM
  mode programming or survives its absence.
- Mixed handoff would reject a simple deterministic distinction and require a
  finer timing/edge control.

This is a control, not a fix. Cold and warm histories must be logged
separately.

### 2. Compare complete ordered VI traces

Instrument or statically enumerate exact write order for:

- libogc disabled-VI cold initialization;
- libogc enabled-VI retrace commit;
- legacy probe and AVE attach setup;
- DRM standalone setup.

Record DCR, clock, XFB address, DI, VI_SEL readback, AVE command writes, and
delays. Test one ordering difference per committed artifact.

### 3. Build a field-aware standalone KMS commit

If handoff behavior supports it, move timing/address publication toward a
single vblank-bound transaction while VI remains enabled. Do not combine this
with an AVE rewrite or GX change in the same test.

### 4. Convert GX defaults into an auditable state table

Generate a table for every `__GX_InitGX()` call showing:

- emitted BP/CP/XF register and value;
- current Linux equivalent;
- initialization lifetime: once per ownership, once per context, or per draw;
- evidence that omission is safe;
- positive control that detects failure.

This is preferable to fuzzing unknown registers. Fuzzing can trigger command
strobes, DMA to arbitrary addresses, or destructive reset behavior without
providing a meaningful oracle.

### 5. Model GX as a DRM render owner

After KMS mode stability, define buffer placement, tiling, submit validation,
PE fences, reset recovery, and XFB presentation as explicit interfaces. Avoid
exposing arbitrary raw FIFO bytes as the first userspace API.

## Questions left open

- Which exact VI/AVE edge or hidden latch selects the observed chroma phase?
- Does VI reset affect only VI timing state, or can its timing perturb the
  downstream serializer/encoder boundary?
- Is a continuously enabled, retrace-committed mode transaction sufficient for
  deterministic cold standalone DRM startup?
- Which GX state can safely persist across Linux clients, and which must be
  restored for every context?
- What bounded CP/PE reset sequence is safe under Linux module unload and GPU
  hang recovery?
- Which MEM1 allocations should become DMA/GEM-managed rather than fixed
  device-tree reservations?

## Review checklist

Before adding a new low-level test, answer:

1. Which subsystem owns the operation and its cleanup?
2. Is this cold initialization, warm handoff, ordinary update, or shutdown?
3. What is the register semantic type for every write?
4. What cache operation and physical address unit apply?
5. What downstream completion event proves the tested stage ran?
6. What positive control validates the diagnostic?
7. What exact prior state is restored after failure?
8. Is the result visual, memory-observed, register-observed, or inferred?

The chronological artifact checksums and hardware results remain in
[`wii-6.18-gx-port.md`](wii-6.18-gx-port.md). The stable Wii pipeline and
ownership overview remains in
[`wii-video-architecture.md`](wii-video-architecture.md).
