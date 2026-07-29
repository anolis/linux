# Wii Linux 6.18 GX port test ledger

This ledger tracks the modern-kernel port of the Wii VI framebuffer and the
reloadable GX accelerator. Hardware conclusions require a committed source
state and a checksum-verified deployed image.

## 2026-07-28: CPU framebuffer positive control (invalid build)

- Source implementation: `89a40599d` (`video: fbdev: port the Wii VI
  framebuffer to Linux 6.18`)
- Deployment helper state: `9ad81d7be238`
- Deployed image SHA-256:
  `cfb0877630a343ad8b28b3b1483b6916905ce0e9f203b435bb48a0de05f21c2c`
- Runtime kernel: `6.18.40-wii+ #9 PREEMPT Tue Jul 28 15:15:21 CDT 2026`

Wi-Fi and SSH passed: `wlan0` associated, received `10.3.10.12`, and accepted
the restored root public key when the client enabled `ssh-rsa` compatibility
for the rootfs's OpenSSH 6.7 server.

The framebuffer test is invalid, not negative. `/proc/fb` was empty and no
`gcn-vifb` platform driver appeared. Runtime config inspection showed
`CONFIG_FB=y` and `CONFIG_FRAMEBUFFER_CONSOLE=y`, but no
`CONFIG_FB_GAMECUBE=y`. The build had reused a stale `.config`; adding the
symbol to `wii_defconfig` does not update an existing `.config` through an
incremental build.

The deployment helper now runs `make wii_defconfig` before every build and
refuses to continue unless `.config` contains `CONFIG_FB_GAMECUBE=y`. Retest
the CPU framebuffer before porting or loading any GX accelerator code.

## 2026-07-28: CPU framebuffer probe with corrected config

- Source implementation: `89a40599d`
- Deployed image SHA-256:
  `03601c0fbd69889d5a8033fff7be0f37ddc0821ca7e2098767681a6ba791f974`
- Runtime kernel: `6.18.40-wii+ #10 PREEMPT Tue Jul 28 15:47:50 CDT 2026`

The corrected image contained `CONFIG_FB_GAMECUBE=y`. The platform driver
registered and bound to `c002000.video`, proving the Kconfig, build, DT match,
and initcall paths. Probe then failed with `-EIO` after both
`request_mem_region()` and `ioremap()` rejected XFB physical range
`0x01698000+0x00168000`.

This is expected modern-kernel behavior: the DTS reserves the XFB with
`/memreserve/`, but it remains classified as System RAM. Modern PowerPC does
not permit an `ioremap()` alias of System RAM. Use `memremap(...,
MEMREMAP_WB)` to obtain the direct mapping and explicitly flush CPU-written
XFB cache lines before the noncoherent VI scans them.

The same boot also confirmed that the rootfs's static `/dev/console` is a
regular file and `CONFIG_DEVTMPFS` was disabled. Enable devtmpfs in
`wii_defconfig`; the external `init-diag.sh` has already been updated to mount
it, stop repeating physical-card pull banners, and launch a local shell when
`/dev/fb0` exists.

## 2026-07-28: CPU framebuffer positive control passed

- XFB mapping implementation: `204740990`
- Deployed image SHA-256:
  `ec7660bd41bea735994f45463ed5077e5ecfad8a119271b7e0b4eed58572da52`
- Runtime kernel: `6.18.40-wii+ #11 PREEMPT Tue Jul 28 16:55:32 CDT 2026`

All positive controls passed:

- `gcn-vifb` bound to `c002000.video` without warnings or faults.
- `/proc/fb` reported `0 gcn-vifb`.
- fbcon switched to an 80x30 color framebuffer console.
- sysfs reported 640x480, 16 bits per pixel.
- devtmpfs provided real `/dev/console`, `/dev/tty0`, and `/dev/fb0` nodes.
- Wi-Fi retained `10.3.10.12` and key-based SSH remained stable.
- A marker written remotely to `/dev/tty0` was visually confirmed on the Wii.

The CPU RGB565-to-YUYV path on Linux 6.18 is therefore the new known-good
fallback baseline. Proceed with the separately reloadable GX accelerator;
module unload must restore this exact live CPU console.

## 2026-07-28: First reloadable GX module cycle passed

- GX module implementation: `9500ee207`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `fcd616b8c19c98e2ff061020cbfcfbafe62de5d0d1895beca0d42e35e5700c2c`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=reference hold_frame=1`

The module loaded successfully, registered with gcnfb, mapped PE finish hwirq
10 to Linux IRQ 23, and completed the seed, copy-clear, libogc init, and two
reference-renderer submissions. Every FIFO drained to `RDoff == WToff`; all
four PE finish IRQs arrived; token waits completed in 310-860 microseconds.

The displayed GX frame was visually confirmed blurry, reproducing the known
3.15 accelerator defect on Linux 6.18. This is a useful reproduction, not a
port regression: `rmmod gcn_gx` immediately restored the clear live CPU
console without reboot, also visually confirmed. Wi-Fi and SSH remained live.

The modern reloadable investigation loop is therefore validated. Subsequent
GX-only changes require only `tools/wii-gx-cycle.sh`; no kernel rebuild, card
movement, or rootfs write is needed unless reserved-memory requirements
change.

## 2026-07-28: Generated renderer bounded-frame control

- Source state: `f9cc3d1927a3`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `fcd616b8c19c98e2ff061020cbfcfbafe62de5d0d1895beca0d42e35e5700c2c`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=generated hold_frame=1`

The generated renderer completed the same seed, copy-clear, isolated libogc
init, and two live-frame submissions as the reference test. All four PE
finish IRQs arrived, token waits completed in 410-860 microseconds, and every
FIFO submission drained to `RDoff == WToff`. The VFB and tiled-texture digest
sums matched for both captured live frames.

A full-frame capture from the live webcam feed showed real spatial
corruption: console content was compressed and repeated in several vertical
regions, with additional vertical duplication. This is materially different
from a solid copy-clear result and proves that the generated path publishes
changing framebuffer content, but with incorrect texture sampling or display
layout. After `rmmod gcn_gx`, a second full-frame capture showed the clear,
full-width CPU console. The camera and VI mode are therefore valid controls;
the repeated layout is produced by the GX path.

Next isolate texture geometry before changing raster state. The source and
tiled-buffer digests matching rules out corruption in the CPU tiling loop,
but does not validate the GX texture dimensions, format, cache/TMEM state,
texture coordinates, or EFB-to-XFB copy stride.

## 2026-07-28: Texture geometry passed, sharpness failed

- Test implementation: `8e376ece808f`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `842c368f90d943cfbaad3dfba8c4190dc384b020ce574014f15e0fd714a8eff3`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

The deterministic tiled RGB565 texture rendered with correct large-scale
geometry on real hardware. A full-frame webcam capture showed the expected
red upper-left, dark-green upper-right, blue lower-left, and white lower-right
quadrants. Black 32-pixel grid lines covered the full frame and the yellow
diagonal markers crossed the expected geometry. The user directly observed
that the GX output was blurry, however, while the CPU-console control after
module unload was sharp. This is a spatial-layout positive control, not an
image-quality positive control.

All four PE finish IRQs arrived, token waits completed in 410-860
microseconds, and every FIFO drained to `RDoff == WToff`. Both texture buffers
produced the same deterministic digest (`crc=a05bcbcf`, `sum=30d9faae`,
`xor=0410`, `nz=288345`). Module unload then restored the clear CPU console,
also confirmed with a full-frame capture.

Because the pattern uses the same MEM1 buffers, cache flush, RGB565 texture
descriptor, generated command state, draw geometry, EFB copy, XFB stride, and
presentation path as the corrupt console test, it rules out gross dimension,
axis, tiling-block, viewport, and stride mistakes. It does not rule out
half-texel sampling, texture filtering, EFB copy filtering, or another
downstream quality issue. The repeated/compressed console layout may still
involve the live VFB contract or linear-to-tiled copy, but the shared blur
requires auditing the GX sampling and copy-filter state first.

## 2026-07-28: Narrow display-copy filter did not fix blur

- Test implementation: `d6c394313cbf`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `c6ad95d20beced0534d1764ed76d7c43badcc6cc924cdf1575ce2b187c49d424`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

This test changed only display-copy filter BP registers 0x53 and 0x54. The
previous values encoded the broad video-mode coefficients
`[8,8,10,12,10,8,8]`, despite a source comment claiming `vf=false`. The test
used libogc's exact `GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL)` values,
which encode `[0,0,21,22,21,0,0]`.

The user still observed blurry GX output, and a full-frame webcam capture
showed no meaningful sharpness improvement over the broad-filter capture.
FIFO drains, PE finish IRQs, tokens, pattern digest, and spatial geometry all
remained correct. Module unload restored the CPU console.

The broad seven-tap copy filter is therefore ruled out as the primary blur
cause. Keep the narrow values because they accurately implement the stated
`vf=false` policy, but do not credit them as a visual fix. Next render a
hard-edged direct-color EFB pattern without texture sampling, then copy it
through the same XFB path. A sharp direct-color result implicates texture
sampling; a blurry result implicates the EFB-copy/presentation path.

## 2026-07-28: Direct-color EFB pattern was clear

- Test implementation: `acc19b0770dc`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `c6037cd65e8d83f20026f7f1869820a21c3a015710819a159b6abdac507b04b8`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=console hold_frame=1`

The direct renderer bypassed VFB reads, linear-to-tiled conversion, MEM1
texture buffers, texture descriptors, texture coordinates, TMEM, and texture
sampling. It drew four vertex-color quadrants and a two-pixel 32x32 black grid
directly into the EFB, then used the same PE fence, EFB-to-XFB copy, XFB
stride, VI scanout, and presentation path as the blurry textured tests.

The user observed a clear image. A full-frame webcam capture independently
confirmed materially sharp grid edges and quadrant boundaries. All four PE
finish IRQs arrived, token waits completed in 410-420 microseconds, and FIFO
submissions drained to `RDoff == WToff`; the direct frame used 2688 command
bytes. Module unload restored the CPU console.

This is a valid positive control for the downstream display path. EFB copy,
the narrow BP 0x53/0x54 filter, XFB stride, VI presentation, and the capture
path do not cause the texture blur. The remaining blur is in the texture path:
texture coordinates, LOD/filter state, TMEM/cache behavior, or sampling. The
correct large-scale textured-pattern geometry makes a gross coordinate or
dimension error unlikely. Audit BP 0x80 texMode0 encoding first, especially
min/mag filter and LOD fields, against libogc and Dolphin.

## 2026-07-29: Near-identity copy filter still blurry

- Test implementation: `be2d3c13943a`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `1b09ced4f16359211329e72285f234fc45cf36349de322ca1bd4feb57cae1451`
- Runtime kernel before the post-test restart:
  `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

The copy filter used `[0,0,0,63,1,0,0]`, the closest unity-sum hardware
filter to an identity operation because each coefficient is only six bits.
The user still observed blurry output. A full-frame webcam capture obtained
while the frame was held confirmed that the one-pixel grid remained visibly
degraded. FIFO drains, PE IRQs, token waits, texture digest, and pattern
geometry all passed as before.

The Wii was manually restarted after the visual result and capture, before a
same-boot unload recovery control could be trusted. The next boot reached
Wi-Fi and SSH normally with no GX module loaded. Do not classify the restart
as either a GX crash or a clean unload result.

This rules out programmable EFB vertical filtering as the primary blur cause,
including the broad seven-tap, libogc three-tap, and near-identity variants.
Retest the direct-color pattern with a one-pixel grid to match feature width;
without that control, the clear two-pixel direct pattern does not yet prove
that texture sampling alone causes the degradation.

## 2026-07-29: One-pixel direct grid reproducibly copied purple clear

- Test implementation: `e2833fa2608e`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `cf3616357c7a68ef75f47ba618c927132998e536f5b8b2f113f38ec0f28cdf60`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=console hold_frame=1`

This changed only the direct diagnostic's vertical and horizontal grid
rectangles from two pixels wide to one. The test was run twice with identical
module bytes. Both runs displayed a uniform purple diagnostic clear rather
than any quadrants or grid. Each run still received all PE finish IRQs and
tokens and drained the 2688-byte direct FIFO to `RDoff == WToff`. Same-boot
module unload restored the CPU console after the second run.

The webcam was unavailable, so this result is based on two matching direct
user observations and kernel logs, not a saved full-frame capture. Purple is
a categorical primitive-missing result rather than a sharpness judgment; it
does not satisfy the intended one-pixel quality comparison. Do not retract
the earlier clear two-pixel direct result.

The one-pixel change was made after switching the copy filter from libogc's
three-tap values to the near-identity values, so two differences exist versus
the clear direct test. Restore the two-pixel grid while retaining the
near-identity filter. A clear result isolates one-pixel geometry; purple
instead implicates the filter-state change or test chronology.

## 2026-07-29: Two-pixel direct grid also purple under identity filter

- Test implementation: `28d2c3406695`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `1b09ced4f16359211329e72285f234fc45cf36349de322ca1bd4feb57cae1451`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=console hold_frame=1`

Restoring the direct grid to two pixels while retaining the custom
`[0,0,0,63,1,0,0]` copy filter still produced a uniform purple diagnostic
clear. All FIFO, PE, token, and same-boot unload controls passed. The webcam
remained unavailable, so this is a direct user-observed color result.

This rules out one-pixel geometry as the cause of the preceding purple runs.
The custom near-identity filter is the remaining controlled difference from
the earlier clear two-pixel direct test and is not a valid diagnostic baseline
on this hardware, regardless of its nominal unity coefficient sum. Revert to
libogc's `vf=false` `[0,0,21,22,21,0,0]` state and revalidate the direct
positive control before investigating texture sharpness further.

## 2026-07-29: Fresh-boot sequence proves missing GX initialization state

- Test implementation: `d48140133a95`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `c6037cd65e8d83f20026f7f1869820a21c3a015710819a159b6abdac507b04b8`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`

After the user's manual restart, the restored libogc-filter, two-pixel direct
renderer displayed purple rather than the grid. This module checksum is
byte-for-byte identical to the earlier `acc19b0770dc` module that produced a
clear grid and full-frame capture. The same current module was then exercised
without another reboot in this sequence:

1. `renderer=direct texture_source=console hold_frame=1`: purple clear.
2. `renderer=generated texture_source=pattern hold_frame=1`: purple clear.
3. `renderer=reference texture_source=pattern hold_frame=1`: lime-green
   clear, matching the captured reference stream's own EFB clear color.

Every run drained its FIFO, received all PE finish IRQs and tokens, and
unloaded back to the CPU console. The webcam was unavailable; colors were
reported directly by the user. Both generated and byte-replayed libogc
primitives therefore failed to alter the EFB while copy-clear remained
functional.

This supersedes the attempted filter-based explanation. Identical module
bytes can produce visible primitives or only clear colors depending on prior
GX/boot state. The driver's partial `gx_load_libogc_init_preamble()` does not
establish a self-contained raster pipeline and relies on residual state from
Mini, an earlier application, or an earlier module sequence. Stop varying
filters and geometry. Capture or reconstruct complete libogc `GX_Init()`
state, validate it from a cold/fresh boot, and only then resume texture-quality
work.

## 2026-07-29: Vertex-cache invalidation did not restore primitives

- Test implementation: `4351d2557af6`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `160c0e5d773df8ed5197e4dbf4c089a03a3b93777303e86800577711887b1037`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=console hold_frame=1`

The initialization preamble emitted libogc's standalone `GX_InvVtxCache()`
FIFO opcode `0x48`, which was a real command missing from the driver. The
direct renderer still displayed purple. FIFO, PE, token, and same-boot unload
controls passed. The webcam remained unavailable.

Keep the invalidation because it is part of canonical `GX_Init()`, but rule it
out as an isolated fix. The old 3.15 ledger records a more relevant ordering
control: prepending the conservative preamble and the first exact reference
texture frame in one contiguous submission eliminated green draw failures on
seven of seven cold boots. The modern reference path currently sends its
preamble in an earlier, separate init submission. Restore the validated
contiguous ordering before expanding the preamble further.

## 2026-07-29: Contiguous preamble still green in warm-state test

- Test implementation: `6b554cea98d9`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `aa66ff9a3565680cac3a3c2ebe130dc9c74bfe0fef965c9303ec5739ba28ec72`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=reference texture_source=pattern hold_frame=1`

The first exact reference frame contained the conservative preamble in the
same submission and drained the expected padded `WT=RD=0x03e0`. The user still
observed lime green, matching the captured frame's clear color. All PE, token,
and same-boot unload controls passed. The webcam remained unavailable.

This run occurred after many module state permutations on one boot and its
preamble also contained the newly added `GX_InvVtxCache()` opcode, unlike the
old seven-of-seven cold-boot control. It therefore proves that contiguous
ordering is not sufficient to recover the current warm GX state, but it is not
an exact rejection of the historical startup result. Remove the unhelpful
opcode and retest the exact historical stream after a real full power-off boot.

## 2026-07-29: Historical contiguous preamble restores cold-boot primitives

- Test implementation: `ebaaf76b15ea`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `39724ebe901a0f3f2839c9ae925a7603425b789d7a28cc3e225ecf0935ad6ee3`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=reference texture_source=pattern hold_frame=1`

The Wii was fully powered off before this run. After boot, `uptime` reported
less than one minute and no `gcn_gx` module was loaded, validating the intended
cold-start condition. This build removed the newly added `GX_InvVtxCache()`
opcode while retaining the historical conservative initialization preamble
contiguous with the first exact reference frame.

The deterministic four-quadrant grid pattern became visible. The user judged
it blurry, but this is categorically different from the lime-green copy-clear
seen in the preceding warm-state run: GX primitives altered the EFB on this
cold boot. The first combined frame had the expected padded size of 992 bytes
(`WT=RD=0x03e0`), every PE token completed, every FIFO drained, and same-boot
module unload restored the CPU console. The webcam remained unavailable, so
the visual result is based on direct user observation rather than a saved
full-frame capture.

This reproduces the important direction of the old 3.15 seven-of-seven result
on Linux 6.18: the conservative preamble must be contiguous with the first
reference draw, and startup state matters. Treat primitive visibility and
image sharpness as separate problems. Preserve this checksum-backed stream as
the cold-start positive control. Before changing filters or texture state,
repeat it across cold boots to establish reliability, then compare a direct
renderer whose first draw is preceded by the same contiguous preamble. A
successful direct comparison will isolate the remaining blur to texture input
or sampling rather than EFB copy output.

## 2026-07-29: Cold-boot direct renderer is sharp with identical copy path

- Deployed repository commit: `743e3e778365`
- Test implementation: `ebaaf76b15ea`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `39724ebe901a0f3f2839c9ae925a7603425b789d7a28cc3e225ecf0935ad6ee3`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=pattern hold_frame=1`

After another full power-off boot, `uptime` reported one minute and no
`gcn_gx` module was resident. The module bytes were identical to the preceding
reference-renderer test. The direct-color renderer also placed the historical
conservative preamble contiguously before its first frame.

The deterministic four-quadrant, two-pixel grid appeared clear. Its 2688-byte
first-frame FIFO drained to `RDoff == WToff == 0x0a80`, every PE token
completed, and same-boot unload restored the CPU console. The webcam remained
unavailable, so sharpness was judged directly by the user.

Together with the preceding blurry reference-texture result, this is a useful
same-module, cold-boot primitive comparison, but not a filter-controlled A/B:
the embedded reference frame programs libogc's broad BP 0x53/0x54 copy filter,
while the direct path programs the driver's narrow filter. The older generated
texture tests used the narrow filter and were still blurry, but revalidate that
comparison under the corrected initialization sequence before treating it as
decisive. Keep the direct renderer as the sharp positive control.

## 2026-07-29: One-pixel direct grid remains sharp on a cold boot

- Test implementation: `bdee7ff92fb0`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `a56d3294693b7807efbdc8dcb63a45a6a3c16f8b7eefccd299b2bd352f7e3b0f`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=pattern hold_frame=1`

This changed only the direct-color diagnostic's horizontal and vertical grid
lines from two pixels wide to one, matching the deterministic textured
pattern's grid width. The Wii was fully powered off before the test; `uptime`
reported one minute and no GX module was resident before loading it.

The one-pixel direct grid appeared clear. The first-frame FIFO again drained
to `RDoff == WToff == 0x0a80`, all PE tokens completed, and same-boot unload
restored the CPU console. The webcam remained unavailable, so the visual
result is based on direct user observation.

This removes feature width as an explanation for the blurry textured grid.
The downstream EFB copy and VI path preserve one-pixel direct geometry, while
the texture path does not. The next tests must alter only texture-path state;
start by decoding and validating BP 0x80 texture filtering and LOD fields
against libogc and Dolphin rather than changing copy or raster state.

## 2026-07-29: Narrow-filter generated texture remains blurry

- Deployed repository commit: `4603022c24b6`
- Test implementation: `bdee7ff92fb0`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `a56d3294693b7807efbdc8dcb63a45a6a3c16f8b7eefccd299b2bd352f7e3b0f`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

The Wii reported 36 minutes of uptime, so this run is explicitly a warm-state
test rather than the requested cold boot. No GX module was resident before the
load. The immediately preceding GX run used the identical module bytes and
showed a clear one-pixel direct grid before unloading.

The generated renderer showed the deterministic grid, but it was blurry. Both
source buffers had the expected texture digest (`crc=a05bcbcf`,
`sum=30d9faae`, `xor=0410`, `nz=288345`). The first generated frame drained
its 960-byte FIFO to `RDoff == WToff == 0x03c0`, every PE token completed, and
same-boot unload restored the CPU console. The webcam remained unavailable.

Unlike the embedded reference blob, generated and direct renderers both use
the driver's narrow BP 0x53/0x54 copy filter. This same-module comparison
therefore confirms that the remaining blur is texture-path-specific, not a
display-copy or VI-output effect. Dolphin's `TexMode0` definition and libogc's
`GX_InitTexObjLOD()` also confirm that BP `0x80000100` already means clamp,
nearest magnification, no mipmap filter, nearest minification, diagonal LOD,
zero bias, and no anisotropy. Do not spend another test on that same encoding;
move next to texture-coordinate scale/centering and tiled-data interpretation.
