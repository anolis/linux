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

## 2026-07-29: First digital-XFB positive control exposed export bug

- Test implementation: `89cc5f69cae2`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `0eedeb3a595508cdfab9d0310d91c7f3865da6ae501cb74067174999a2344fce`
- Runtime kernel: `6.18.40-wii+ #12 PREEMPT Tue Jul 28 17:07:44 CDT 2026`
- Parameters: `renderer=direct texture_source=pattern hold_frame=1`

The known-clear one-pixel direct grid rendered clearly again. All FIFO and PE
controls passed, and the module logged a post-token, cache-invalidated XFB
snapshot of 614400 bytes at physical address `0x0172e000`. However,
`/sys/kernel/debug/gcn_gx/xfb_yuyv` returned zero bytes even though its width,
height, and physical-address metadata were correct.

This is a failed positive control for the digital capture mechanism; do not
use it as captured-frame evidence. `debugfs_create_blob()` fixed the readable
length at its creation-time value of zero. Commit `9f873066c` replaces it with
a custom `simple_read_from_buffer()` file and makes the cycle script wait on
published width metadata. Revalidate that implementation against the same
clear direct pattern before capturing textured output.

## 2026-07-29: Dynamic XFB reader still required inode-size publication

- Deployed repository commit: `1c61a66a7d93`
- Test implementation: `9f873066c`
- GX module SHA-256:
  `09fd4f157c098630e5d55f53c74cd8015b7803383cda70a30cb32e8caee35631`
- Parameters: `renderer=direct texture_source=pattern hold_frame=1`

The direct one-pixel grid was again visibly clear. The corrected module again
logged a 614400-byte post-token snapshot, and debugfs metadata reported width
640, height 480, and physical address `0x0172e000`. The custom debugfs reader
still returned zero bytes, however, because its regular-file inode retained
the creation-time length of zero and the VFS returned EOF before delivering
data through the read callback.

This is another failed capture positive control, not frame evidence. Commit
`be2905216` stores the debugfs dentry and publishes its inode size together
with the snapshot size after the cache-invalidated copy. Validate that exact
state once more against the clear direct pattern before proceeding.

## 2026-07-29: Full digital XFB capture passes direct positive control

- Deployed repository commit: `4cede2ab37a3`
- Test implementation: `be2905216`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `0899bf0dc71bf4530e6bbfcaf5b49d292dd6c22807eaf44b43e47d9b3766a7a1`
- XFB YUYV SHA-256:
  `2cdd48857ad4bbfc6ed403129df28ce0180cf7d0cf919281944acfbb3d1a1968`
- Parameters: `renderer=direct texture_source=pattern hold_frame=1`

The user again observed a clear one-pixel direct grid. After the validated PE
token, the module captured exactly 614400 bytes from physical XFB
`0x0172e000`; debugfs reported and returned the full length. The cycle script
retrieved the YUYV frame and converted it to a 640x480 PNG. Direct inspection
of that PNG showed the expected four solid quadrants and crisp one-pixel black
grid lines with no texture-like blur. All FIFO and PE controls passed, and
same-boot unload restored the CPU console.

This is the required positive control for digital XFB capture. Full-frame
snapshots obtained through this post-token, cache-invalidated path can now be
used as evidence. The next run should capture `renderer=generated` with the
same deterministic pattern and module bytes, then compare exact edge profiles
and pixels against this direct baseline instead of relying on camera output.

## 2026-07-29: Digital capture identifies texel-boundary aliasing, not blur

- Deployed repository commit: `2c1941d411ca`
- Test implementation: `be2905216`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `0899bf0dc71bf4530e6bbfcaf5b49d292dd6c22807eaf44b43e47d9b3766a7a1`
- XFB YUYV SHA-256:
  `6c5e2367185ddb148e617dc1c5a01a0adec72d0bdfec9686fa98b775b66feea1`
- Converted PNG SHA-256:
  `848624f6050403c7345bec564c13088b3e10fb3177afd8a58a946da4b61cad27`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

The user observed the familiar blurry grid. The first automatic SSH transfer
was truncated, but the immutable debugfs file continued to report and return
614400 bytes; a retry produced the complete checksum above. Both tiled source
buffers had the expected deterministic digest, every PE token completed, and
the generated FIFO drained to `RDoff == WToff == 0x03c0`.

The exact XFB is more specific than the visual report. Quadrant boundaries and
yellow diagonals are sharp, proving that gross texture dimensions, addressing,
and projection are correct. The intended one-pixel black grid is instead
broken into a regular pattern of dots and short segments across otherwise
solid regions. Horizontal-line samples vary with X and vertical-line samples
vary with Y, consistent with coordinates landing on texel boundaries and
raster interpolation precision selecting adjacent texels. The high-frequency
aliasing is what appears blurry after analog/HDMI conversion and capture.

BP `0x80000100` is already validated as nearest sampling with no mipmaps. The
next isolated test should add a positive half-texel translation to TEXMTX0:
`0.5 / 640` in S and `0.5 / 480` in T, while leaving scale, texture data,
filtering, raster state, and copy state unchanged. Success is a digital XFB
whose black grid lines are continuous and one pixel wide.

## 2026-07-29: Positive half-texel bias changes phase but does not fix grid

- Test implementation: `5892e540b54c`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `30e888965f0d451b70a3264a32c1249715e35485164f97663a2b7ce19b30025e`
- XFB YUYV SHA-256:
  `17140135aa614f659aae7e1aa30bf11087da34d6b6fe9e00b3ed1eb4e9fa312e`
- Converted PNG SHA-256:
  `e4244fe5e7a0ac50b08206f55e0d32ee195a08503491d02a5d7301b6d108ffd7`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

This changed only TEXMTX0's translation terms from zero to positive
`0.5 / width` and `0.5 / height`. The first automatic SSH transfer was
truncated; retrying the still-live immutable snapshot returned all 614400
bytes and produced the checksums above. All FIFO, PE, and texture-digest
controls passed.

The positive bias did not make the one-pixel grid continuous. It changed the
periodic dot/segment phase and made the rising yellow diagonal visibly stair
and wander, while quadrant boundaries remained correctly positioned. The PNG
was opened beside the zero-bias baseline in GIMP for direct comparison.

This is a valid negative result: positive half-texel centering is the wrong
direction under the GX raster/texture convention used here. Because the bias
materially changes the artifact, coordinate centering remains implicated.
Test negative `0.5 / width` and `0.5 / height` next, changing only the signs
of the two translation terms.

## 2026-07-29: Negative half-texel bias restores continuous texture grid

- Test implementation: `0feb866b463d`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `aa608c9839142b96cbc1ede4ddfa1b9239ac5eceef4671f4939b99075bfd1e51`
- XFB YUYV SHA-256:
  `50de68819f0070c39d685007132efcf25f8158602eca29a8d7f60a6acbec261b`
- Converted PNG SHA-256:
  `b07734a176265746cb149446ba4d6e37547ef27b61ec9014fb274756673d3fcd`
- Parameters: `renderer=generated texture_source=pattern hold_frame=1`

This changed only the signs of TEXMTX0's half-texel translation terms, from
positive to negative `0.5 / width` and `0.5 / height`. The unstable Wii Wi-Fi
link truncated multiple whole-file SSH reads, while the remote debugfs file
remained exactly 614400 bytes. The complete immutable frame was retrieved in
verified chunks and converted to the checksums above. All FIFO, PE, and tiled
texture-digest controls passed.

The digital XFB shows continuous, one-pixel black horizontal and vertical grid
lines across all four quadrants. The periodic dots and short segments from the
zero-bias and positive-bias captures are gone. Large-scale geometry remains
correct and the yellow diagonals remain visible. The complete PNG was opened
in GIMP beside both earlier captures.

This is the first checksum-backed fix for the textured output-quality defect.
GX's raster convention requires position-derived normalized coordinates to be
translated by negative half a texel for one-to-one framebuffer sampling. Keep
this bias. Next run `renderer=generated texture_source=console hold_frame=1`
to verify real console legibility and capture its exact XFB. Do not conflate
any remaining YUYV chroma behavior at colored edges with the now-fixed broken
texture sampling.

## 2026-07-29: Real console improves but retains digital glyph breakup

- Deployed repository commit: `d9d6d228c527`
- Test implementation: `0feb866b463d`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `aa608c9839142b96cbc1ede4ddfa1b9239ac5eceef4671f4939b99075bfd1e51`
- XFB YUYV SHA-256:
  `763ed70cd2c4dd5d1243469ec460a7a73fcd1866e54272389a981c5de2876829`
- Converted PNG SHA-256:
  `8778f66e88a0f6b59ae41d1fb101c7aaf335224ed03e235f18c02509762ea545`
- Parameters: `renderer=generated texture_source=console hold_frame=1`

The user observed that the console remained blurry, but that its blur was more
uniform and represented an overall improvement. The complete digital XFB was
retrieved in verified chunks and opened in GIMP. It shows recognizable text,
but white glyph strokes break into a checkerboard-like pattern. The residual
defect is therefore present in the GPU-produced digital frame, not introduced
only by HDMI conversion or the camera.

Both generated console submissions completed every FIFO and PE control. For
each submitted frame, the linear VFB and tiled buffer had identical sums, XOR,
and nonzero counts; differing CRCs are expected because tiling changes byte
order. The deterministic texture pattern is now clean under the same negative
half-texel state, so this residual is specific to real console input or its
linear-to-tiled path rather than the general copy/output path.

`/dev/fb0` is mmap-only on this rootfs and returned zero bytes to a read test.
Add a module debugfs snapshot of the exact linear RGB565 VFB used for the held
frame, retrieve it alongside XFB, and convert it as `rgb565be`. Validate that
source snapshot before modifying tiling or coordinates again: sharp source
plus broken XFB implicates conversion/sampling, while a broken source means GX
is accurately displaying fbcon's input.

## 2026-07-29: Same-frame capture clears fbcon and isolates the GX texture path

- Test implementation: `77bc760f9608`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `bd9c9da0a91324e8d87b1c33ca6aa318365ff38fec9059eac34cf52788d7b523`
- XFB YUYV SHA-256:
  `7bdb703ed04958cbe0949aceed414349159238b44bf64cb5082d7f1ec5d138b6`
- XFB PNG SHA-256:
  `4db6411951424f13704296590f52d39e6bbcc4fc73936173b1af3906fffb17ef`
- VFB RGB565BE SHA-256:
  `1433dbadb98cdf3e6fea5f85baab8280b694b56723063fd5a342deb2919fbcdd`
- VFB PNG SHA-256:
  `86e717dda2cd5dcc8a5fabd1b41f663108f7c1e5a417cb323425cd9a7cdf6775`
- Parameters: `renderer=generated texture_source=console hold_frame=1`

The module copied the exact linear VFB immediately before tiling and captured
the corresponding XFB only after the held frame's PE token completed. The Wii
Wi-Fi link again truncated large direct reads, but compressing each immutable
debugfs file on the Wii reduced them to 33763 and 46328 bytes and allowed both
complete 614400-byte frames to be retrieved. Both converted images were opened
together in GIMP.

The VFB source is sharp. Its glyph edges and one-pixel strokes are intact. The
same-frame GX-produced XFB breaks those strokes into a regular checkerboard-like
pattern. The user's display observation agrees: the negative half-texel bias
makes the blur more uniform and is an overall improvement, but it is not a
complete fix.

This decisively clears fbcon and the CPU-side source framebuffer. Dolphin's
reference RGB565 decoder also confirms the driver's intended 4x4 block order:
blocks left-to-right and top-to-bottom, with four consecutive big-endian
RGB565 pixels in each of four rows. Continue with controlled tests of fine
texture addressing, coordinate scale/rounding, and texture-cache state. Do not
change the XFB copy path or blame the captured VFB without new contradictory
evidence.

## 2026-07-29: Coordinate probe proves mixed nearest-neighbor texel selection

- Deployed repository commit: `a36280b90505`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `96294cf70da3d859c7a524135f40650106988f49fbd32ce52e43b78479856451`
- XFB YUYV SHA-256:
  `54db4f67bca0342080f34af89df00b501fa92b78ca5db93688acb193ca236bd9`
- XFB PNG SHA-256:
  `d54eeb40f267dca55ca8e48b545d3bf9f3c13705765fc9da3aeaf0bd10d9c992`
- VFB RGB565BE SHA-256:
  `83c8ffc0b89bc80c0cf888cd8d1c4c7983455d3232b7463d0f0d0cfecc5726bf`
- VFB PNG SHA-256:
  `7ad0e6f02392c5849ca28962b42c1af16cccd653a412dee1b387656f9c30a895`
- Parameters: `renderer=generated texture_source=probe hold_frame=1`

The probe assigns every source coordinate a reproducible black or white value
from a 32-bit integer hash. Its published linear source snapshot matched the
same independently generated coordinate field exactly: 153941 white and
153259 black pixels. This validates the diagnostic before interpreting GX
output. The FIFO drained, every PE token completed, and the held XFB contained
only exact Y=16 black and Y=235 white samples. GX is therefore performing
nearest selection rather than blending or corrupting pixel values.

Integer-shift correlation found one strong mapping: output displaced two
pixels right and one pixel down agrees with the intended source at 74.8107%.
The next candidates were 59.8655% at (2,0), 58.9329% at (1,0), and 53.8882%
at (3,2); unrelated mappings remain at the expected 50% chance rate. A single
constant displacement cannot explain the output. Approximately half the
pixels follow the dominant mapping while the remainder select other nearby
texels. This directly explains the checkerboard breakup of one-pixel console
strokes.

The first compressed transfer attempt also exposed a tooling limit. Probe data
compresses to roughly 90 KiB rather than the console's 34-46 KiB, and a single
SSH read still truncated. Both immutable files were recovered and checksum
validated using independently retried 16 KiB compressed chunks. Harden the
cycle script with that fallback before the next hardware test.

Next refine the probe from one random bit to multiple grayscale levels per
coordinate. That reduces accidental matches from 50% to 12.5% and permits a
more precise phase-by-phase reconstruction of which source texel each raster
position selects. Do not change sampling state until that mapping is measured.

## 2026-07-29: Eight-level probe isolates deterministic texel lookup variation

- Deployed repository commit: `dce94a8a34d8`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `c6ed0aaebbe399167ea0e1a6dd3af739b167a370cc843965ca742bf5bd325361`
- XFB YUYV SHA-256:
  `57b2f2093480407b42ebfd0299b38e5db7d2286e95406bd8b2d368aea62c6779`
- XFB PNG SHA-256:
  `5e2cabb44e6da48f931dd006ec642bc8cf7b760c0611fffbd77a742853f5fef9`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters: `renderer=generated texture_source=probe hold_frame=1`

The refined probe encodes the hash's top three bits as eight ordered RGB565
gray levels. The source snapshot again matched its independently generated
formula exactly. The XFB contained exactly eight corresponding luma values:
16, 46, 79, 109, 142, 172, 205, and 235. No intermediate values occurred, so
nearest-neighbor selection is independently reconfirmed.

With unrelated agreement now 12.5%, global correlations were 56.2313% for
offset (2,1), 29.6865% for (2,0), 28.2492% for (1,0), 19.3924% for (3,2),
and 16.6246% for (4,2). Subtracting chance agreement gives an approximate
mixture of 50%, 20%, 18%, 8%, and 5%, respectively. Among the 179831 pixels
that matched exactly one of those five candidates, the measured distribution
was 49.769%, 19.814%, 18.022%, 7.761%, and 4.634%. The candidate map forms a
dense deterministic diagonal pattern rather than spatially random corruption.

An independent audit of the earlier checksum-backed direct-pattern XFB found
every one-pixel grid line exactly at coordinates 0, 32, 64, and so on, with
quadrant boundaries exactly at x=320 and y=240. The viewport, scissor,
projection, primitive geometry, and EFB-to-XFB copy are therefore aligned.
The displacement and variation belong specifically to position-derived
texture lookup.

The post-transform selector is also correct: libogc encodes
`GX_DTTIDENTITY - GX_DTTMTX0 = 61`, matching XF 0x1050 value `0x3d`, and
the driver loads identity rows 61-63. Do not change that state.

One three-bit symbol still collides with five candidates often enough to leave
41.5% of pixels ambiguous. Add a second independent probe seed while keeping
all GX state identical, then classify both captures jointly. Six independent
bits reduce random five-candidate collisions enough to reconstruct nearly the
entire texel-selection map before testing any coordinate correction.

## 2026-07-29: Independent seed reconstructs the texel-selection map

- Test implementation: `80a0f740e5c2`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `57292964e076bfa5e347905b5844bc3ad53a67fa42045eb8ae792cfe5ae04955`
- Seed-1 XFB YUYV SHA-256:
  `6463956cefec3b70e81eeab62b7a2714e0f8d56d936ddd1a09e5771ea42dcbdc`
- Seed-1 XFB PNG SHA-256:
  `477356705fd7162304d71a238642a710de86ee824069280174246016e8cfb9a1`
- Seed-1 VFB RGB565BE SHA-256:
  `369b3baed5cc29a082b7a7318dc169f7f4b409fc504fbccd2890d3649c8bcc53`
- Seed-1 VFB PNG SHA-256:
  `f342dcb8727b2c105952c933105d9cacfd4bbca9f3919630fe271806626b7ffb`
- Joint candidate-map PNG SHA-256:
  `08f3e0c32b061c5d7227db183e1b1ef494d08db851a11a86ff09b19e2568c54f`
- Parameters: `renderer=generated texture_source=probe probe_seed=1 hold_frame=1`

The hardened 8 KiB chunk transport retrieved both incompressible frames with
matching remote raw SHA-256 values. The seed-1 source snapshot matched the
independently regenerated seeded formula at all 307200 pixels, and its XFB
again contained only the eight expected nearest-neighbor luma values. All PE
markers completed and the FIFO drained normally. The user described the
display as blurry but more uniform than before, which is expected from the
graded probe and is not itself used as the measurement.

Comparing the seed-0 and seed-1 symbol pairs reduces unrelated agreement to
1/64. Using the convention that output `(x,y)` selected source
`(x+dx,y+dy)`, 93.8534% of the interior pixels matched exactly one of the
five established candidates, 6.1466% had an accidental multi-match, and no
pixel was unmatched. The uniquely classified distribution was:

- `(-2,-1)`: 133852 pixels, 50.0064%
- `(-2, 0)`: 52701 pixels, 19.6888%
- `(-1, 0)`: 47740 pixels, 17.8354%
- `(-3,-2)`: 20827 pixels, 7.7808%
- `(-4,-2)`: 12550 pixels, 4.6886%

The map is highly structured. Labels agree after a four-row displacement at
99.572%, after a 16-column displacement at 98.024%, and after `(16,12)` at
99.335%. Converting each selected coordinate to the driver's confirmed 4x4
RGB565 tiled-memory index does not produce a constant word displacement; the
result splits across many offsets. This rejects a simple texture-base error
or a fixed shift in the tiled byte stream. The raster-grid periodicity instead
points to texture coordinates repeatedly landing on a fixed-point selection
boundary.

Do not change the tiler, texture base, cache invalidation, postmatrix, or XFB
copy based on this result. The next isolated test should move the current
negative half-texel translation away from the boundary by a quarter texel
while retaining the same scale and eight-level seeded probe. A clean result
would collapse the five-candidate pattern to one source displacement; a
persisting pattern would move the investigation to coordinate scale or direct
texcoord generation.

## 2026-07-29: Quarter-texel phase preserves the raster-periodic error

- Test implementation: `141ccb161c1d`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `197ce83f32a9638bb399c823da3abe05320fc1a30631a1b1720a231f4453185f`
- Seed-0 XFB YUYV SHA-256:
  `6b05b0537d505c52749a6395e9df15480123d92674dc3d37671dad122a0aac41`
- Seed-0 XFB PNG SHA-256:
  `275565c2b1492cf2eb7034b96006bb19a88310d9b28c681e41edd9cb64717ee9`
- Seed-1 XFB YUYV SHA-256:
  `61471f507ec981ebdaf5e33a4578dd48f6abff28645a013603ccaf2e4a6470a1`
- Seed-1 XFB PNG SHA-256:
  `05ed7c36784215474b06f618c644748b37c3fa003006b6d1188012cc55a1b993`
- Seed-0 VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- Seed-1 VFB RGB565BE SHA-256:
  `369b3baed5cc29a082b7a7318dc169f7f4b409fc504fbccd2890d3649c8bcc53`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0/1 texel_bias_eighths=-2 hold_frame=1`

Both source captures are byte-identical to their corresponding negative-half
baselines, proving that only the texture-matrix translation changed. Both
XFBs again contain exactly the eight expected luma symbols, every PE marker
completed, and every FIFO drained.

The two seeded captures jointly produce four real source offsets. Of the
interior pixels, 95.3303% match exactly one candidate, 4.6697% have an
accidental multi-match, and none are unmatched. The unique distribution is
`(-1,0)` at 50.0136%, `(-2,-1)` at 24.9998%, `(-2,0)` at 12.5010%, and
`(-3,-2)` at 12.4856%. Unrelated offsets remain at the expected 1/64 joint
agreement.

The new map agrees after a four-row displacement at 99.676%, after a
16-column displacement at 99.004%, and after `(16,12)` at exactly 100% for
the uniquely classified pixels. Moving from negative one-half to negative
one-quarter therefore changes phase and the candidate mixture but does not
collapse lookup to a single texel. Reject the simple phase-boundary
hypothesis.

Next preserve position-derived texgen and the negative-half phase, but emit
coordinates in texel space: TEXMTX0 scale 1 with BP SU scale 1 instead of
TEXMTX0 scale `1/dimension` with BP SU scale `dimension`. These forms are
mathematically equivalent, but the texel-space form removes the normalized
coordinate multiply and its fixed-point precision from the path. Do not
revisit direct TEX0 attributes unless this test also fails.

## 2026-07-29: Texel-space coordinates reproduce normalized lookup

- Test implementation: `df2386999950`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `15e9a1343247ba62a46963aed71d2af48716904465773af64d1d4815a64e7d99`
- XFB YUYV SHA-256:
  `eb503749c469bba98784a67f6aef09e351263c703589a60a9f8a020234d374c8`
- XFB PNG SHA-256:
  `59c7e586d1152fee8795014d48d329793bcdb5d47e38845f11abb63f88bb758d`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_space=texel texel_bias_eighths=-4 hold_frame=1`

The position-derived texel-space path completed normally. It did not reproduce
the old large-coordinate stall when BP SU scale was changed to 1 at the same
time. Every PE marker completed, the FIFO drained to the same `0x03c0`
endpoint, and both exact snapshots were retrieved.

The source is byte-identical to the normalized negative-half probe. Its XFB
has the same five correlation peaks, including 56.2313% at `(-2,-1)`, as the
normalized baseline. Direct comparison finds 99.8639% of luma samples
identical; the 418 differences are confined to columns 241-364 and do not
alter the failure class. The normalized-coordinate multiply is not the cause.

Static review then found that the historical direct-TEX0 path used and
documented XF source row 4. libogc's `GX_SetTexCoordGen2()` maps `GX_TG_TEX0`
to `vtxrow=5`, and Dolphin's `SourceRow` layout independently agrees. A
regular 2x4 TEX0 texgen must therefore emit XF 0x1040 value `0x280`, not the
old `0x200`; row 4 selects absent binormal data and explains that path's
downstream stalls. Retest direct TEX0 only with source row 5, correct direct
VCD/VAT payload, and the existing negative-half phase.

## 2026-07-29: Correct TEX0 source row drains but exposes INVTXSPEC gap

- Test implementation: `92eb9d4cc18c`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `4a33afd3b1236d6d273335b1eb646e48f8716da9130c2e67afc03e2205908450`
- XFB YUYV SHA-256:
  `37c5513c4756831e4dd21f4d03250673066a834942d82239603755133a8c2c02`
- XFB PNG SHA-256:
  `e96e42ba974081d3649d2d53c3a1939c206143d3c710e03cf4db5a062e6011f0`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

Correcting the texgen source to row 5 eliminates the historical direct-TEX0
stall. The expanded FIFO drains to `RDoff == WToff == 0x0440`, all PE markers
complete, and the held XFB is captured normally. The user observed purple;
the exact XFB confirms uniform copy-clear output with Y=62 at all 307200
pixels. The textured primitive still wrote no visible EFB pixels.

The direct path updated CP VCD and VAT but missed the paired XF vertex-spec
register. libogc's `__GX_SetVCD()` always calls `__GX_XfVtxSpecs()`, which
counts direct/indexed attributes and writes XF 0x1008. The established color
path programs `0x01` for one color and zero texture attributes. Direct TEX0
requires `0x11`: one color in bits 1:0 and one texture attribute in bits 7:4.
Leaving `0x01` makes CP parsing and XF input expectations disagree.

Add XF 0x1008=`0x11` only in direct mode, retaining source row 5, the
validated VCD/VAT values, normalized negative-half endpoints, and every
downstream state value. Purple remains the negative control; eight probe luma
levels indicate the direct path has become active.

## 2026-07-29: Direct TEX0 works and exactly reproduces position texgen

- Test implementation: `366ed88f95b4`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `9be75bbbfdf890104a0c5563bc1de83f39677125f26b4411f4a4ffb5e915f573`
- XFB YUYV SHA-256:
  `57b2f2093480407b42ebfd0299b38e5db7d2286e95406bd8b2d368aea62c6779`
- XFB PNG SHA-256:
  `5e2cabb44e6da48f931dd006ec642bc8cf7b760c0611fffbd77a742853f5fef9`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

With XF INVTXSPEC corrected to `0x11`, direct TEX0 becomes a fully active
rendering path. The draw's PE token returns to the normal approximately 410 us
completion time instead of the previous 10 us no-work signature. The FIFO
drains to `RDoff == WToff == 0x0440`, the held output contains all eight probe
luma levels, and the complete source and XFB snapshots pass remote checksums.

The direct-TEX0 XFB SHA-256 is exactly the same as the original
position-derived negative-half probe XFB from `dce94a8a34d8`. This is stronger
than similar correlations: all 614400 output bytes are identical. Direct
vertex ST values, position-derived TEXMTX0 values, and their respective XF
source rows therefore converge to the same downstream behavior.

Keep both genuine direct-path fixes: TEX0 source row 5 (`0x280`) and XF
INVTXSPEC one-color/one-texture value `0x11`. However, clear texgen source,
matrix multiplication, and direct attribute parsing as causes of the mixed
lookup. The remaining common path starts at raster interpolation and TMU
sampling.

Next use corrected direct TEX0 but replace the four-vertex quad with one
oversized triangle whose affine ST endpoints produce the same mapping across
the 640x480 viewport. This removes quad decomposition and its diagonal edge
or slope setup while retaining the texture object, nearest filter, TEV,
viewport, copy path, coordinate phase, and probe data unchanged.

## 2026-07-29: Oversized triangle retains the mixed-texel failure

- Test implementation: `8d1b73a68492`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `11d1cbe6f955a793e6d2f5815d995b62ebecfc74e531f78b625cf0ba58da9116`
- XFB YUYV SHA-256:
  `56e0d3f3456d5491486d0fbbed528b1ac67370bfed8cfeaf0e69ca8b45c2325d`
- XFB PNG SHA-256:
  `1f7b4ed8bc354ac7ab68652e0138c9976e18802886a7db1a267eac49dc4aba7c`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct direct_primitive=triangle texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

The oversized direct-TEX0 triangle completed normally. The FIFO drained to
`RDoff == WToff == 0x0420`, the draw's PE marker completed in approximately
410 us, and the output contains exactly the same eight probe luma levels as
the quad. The triangle intentionally covers the complete viewport, so the
absence of a visible triangular boundary is expected.

Changing only the primitive changes the exact output: 52.6068% of luma
samples match the quad and 145592 differ. The triangle's strongest source
correlations remain nearby mixed offsets: 56.2749% at `(-2,-1)`, 32.8976%
at `(-2,0)`, 24.9414% at `(-1,0)`, 18.7181% at `(-4,-2)`, and 17.1806% at
`(-3,-2)`, versus approximately 12.6% for unrelated offsets.

The primitive topology therefore affects the deterministic pattern but does
not remove its failure class. Clear the quad's split and internal diagonal as
the root cause. The remaining shared path begins at raster interpolation or
TMU sampling and includes EFB-to-XFB sample/copy state. A high-frequency
direct-colour stripe test should distinguish the TMU from the latter path.

## 2026-07-29: One-pixel direct stripes clear raster and display copy

- Test implementation: `041b4ddfb3e5`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `1d093da726d0d86718b5d234fd2529cfa6020427842b9c98fc6543c2a5db601a`
- XFB YUYV SHA-256:
  `4850d373baa1554810d7d08ca0f5d1bf195e1b0013b865a5b6f7dbf472b5560b`
- XFB PNG SHA-256:
  `2ada091ee68ae7f928dc7d2dddf7b8ca3161b19b5d763fea3f908c4f1828094a`
- Parameters:
  `renderer=direct direct_pattern=vstripes texture_source=console hold_frame=1`

The texture-free direct renderer drew a white full-screen background followed
by 320 one-pixel black rectangles at even X coordinates. The 17056-byte FIFO
drained completely to `RDoff == WToff == 0x42a0`; the draw PE marker completed
in 980 us; and the exact 614400-byte XFB snapshot passed its remote checksum.

Every captured pixel is exact. All 480 rows are identical, each row's luma is
`16,235,16,235,...` for all 640 columns, and every one of the 307200 shared
YUYV chroma bytes is neutral 128. There are exactly 153600 black and 153600
white luma samples, every column is vertically uniform, and every run is one
pixel wide.

This is a stronger positive control than the earlier 32-pixel grid. The
direct-colour rasterizer and the common EFB-to-XFB sampling, conversion, and
copy path preserve the highest representable horizontal spatial frequency
without diffusion or neighbour substitution. Localize the mixed-nearby-texel
failure to the texture path rather than display copying.

Next hold direct TEX0 constant at all primitive vertices. A uniform expected
texel would implicate coordinate interpolation or gradients; continued mixed
texels would implicate texture addressing or sampling after interpolation.

## 2026-07-29: Constant TEX0 removes gradients but retains a binary pattern

- Test implementation: `38d30d69b465`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `0e8e79e0415d12d9606f7687e430f52435794272e1982c14112836423f9709f3`
- XFB YUYV SHA-256:
  `b3a9c9f6172fc37a2d5cd1670e2978f40cfc67ee8fe5f36d6329a43261c9a2e7`
- XFB PNG SHA-256:
  `7c7fb4e0b3eda1c21f00d7419bdb6f6c0f339a292b96ca26d6484f4012d81cd7`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_mapping=constant direct_primitive=quad texcoord_space=normalized hold_frame=1`

All four vertices carried bit-exact normalized TEX0 `(0.5,0.5)`, eliminating
both coordinate gradients. The FIFO drained to `RDoff == WToff == 0x0440`,
the draw PE marker completed in 330 us, and both snapshots passed their remote
checksums.

The result is not uniform. It contains 153760 pixels at luma 16 and 153440 at
luma 142, with no other luma values. Its binary selection agrees after four
rows at 99.5864%, inverts after two rows at 99.7928%, agrees after 16 columns
at 98.75%, and repeats exactly after displacement `(16,12)`. Thus ordinary
affine gradients and quad interpolation are not required to produce the
screen-position-dependent selection.

This does not yet prove broken texture addressing. Normalized 0.5 can lie on
the boundary between the central texels, where a position-dependent nearest
tie-break may be legitimate. This implementation also overrides rather than
applies `texel_bias_eighths`. Correct constant mode to apply the configured
fractional-texel phase around the texture centre, then test negative one-half
before drawing a stronger conclusion.

## 2026-07-29: Phased constant TEX0 exposes stale indirect-texture state

- Test implementation: `3233c744c3b4`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `69994a61f33381fddd1eab3f8f505d49bc7aee009717fb2dd7a485e84d01a60a`
- Seed 0 XFB YUYV / PNG SHA-256:
  `962849fe6c022e90a486ec47fa7cd20e4403d2aa7631c9541f52d4a20a981c56` /
  `31675d9417894e2c90eae174b5a210ff4a39c5ce2ba7d05098944515ead71674`
- Seed 1 XFB YUYV / PNG SHA-256:
  `0027d5c4355412674a71e6f31375ea2ed152ea421d15fc24371e68c037e78517` /
  `582ca079854866f758c31b8d240285c5502446f750a27b310977d340358c31b8`
- Seed 2 XFB YUYV / PNG SHA-256:
  `a5b63e0111c21bda64fef926289cf7a28bf84ea1f6ad086b5992f01c157b90d5` /
  `44890201da32d556721d249bc5cad0dbdbe82ab4d9405359bc7066be7b6301f8`
- Seed 3 XFB YUYV / PNG SHA-256:
  `9c1f84a2a9ea60032987d3d9d35bd7188cfe0bbc69df76358aab08f0d7cc48b8` /
  `033cdd5c081f0f42bc836585019c8426adeafb2e62d465ae9b76997e95e03a85`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0/1/2/3 texcoord_source=direct texcoord_mapping=constant direct_primitive=quad texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

Constant mode now applies the configured phase, so all vertices carry the
normalized equivalent of texel coordinate `(319.5,239.5)`. The half-texel
shift does not make the result uniform. Across four independent source seeds,
the destination map consistently selects exactly four source signatures:
`(318,239)` for 153680 pixels, `(317,238)` for 76880, `(318,240)` for 38320,
and `(316,237)` for 38320. The complete signature map still repeats exactly
after destination displacement `(16,12)`.

This rules out a single boundary tie and proves that identical S/T values are
being modified or interpreted differently as a function of screen position.
Static audit then found that the driver never writes BP 0x10-0x1f, the TEV
indirect-texture command registers. libogc's `GX_Init()` calls
`GX_SetTevDirect()` for every TEV stage; for stage 0 this emits BP `0x10=0`.
Dolphin independently documents that a nonzero TEV-indirect command combined
with a disabled indirect stage is undefined and produces a glitchy pattern on
hardware. Our genMode disables indirect stages but inherited BP 0x10 remains
unknown, which matches both the periodic coordinate perturbation and the
otherwise correct texture data.

Next emit the exact `GX_SetTevDirect(GX_TEVSTAGE0)` value, BP `0x10=0`, in the
generated texture state and repeat the constant probe. This is a focused
single-register test; do not change coordinates or any other TEV state.

## 2026-07-29: BP 0x10 fixes constant-coordinate texture corruption

- Test implementation: `5b8a19800861`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `59a9dac94a3d462cb676ae4470c5c957400ee5bff896d36c733c4455df8ca13e`
- XFB YUYV SHA-256:
  `35c0262cccbb4b1014dab2550f0f25456d172a1c9ff7dbcda554b3daf9d271d5`
- XFB PNG SHA-256:
  `e0479513409949acda72406cc07cd5b1490b5c70d37d37e640f473b716842de7`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_mapping=constant direct_primitive=quad texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

Adding only BP `0x10=0` completely removes the screen-periodic selection.
All 307200 XFB pixels have luma 109, every one of the 480 rows and 640 columns
is identical, and the complete frame has no second luma value. The FIFO still
drains to `RDoff == WToff == 0x0440`; the draw PE marker changes from the
broken path's 330 us to 720 us, confirming materially different downstream
work rather than a coincidental copy result.

This is a validated root-cause fix for the mixed-nearby-texel corruption.
Unknown Mini-inherited BP 0x10 enabled an indirect TEV operation while
genMode exposed zero indirect stages, producing undefined hardware coordinate
offsets. `GX_SetTevDirect(GX_TEVSTAGE0)` restores deterministic regular
texture lookup.

Next retain this exact binary and switch only `texcoord_mapping` from constant
to affine. Compare the complete output against the seeded source to verify
one-to-one full-screen texture mapping and determine the correct sampling
phase.

## 2026-07-29: Fixed affine lookup is exact outside the clamp boundary

- Test implementation: `5b8a19800861` (capture run at docs-only HEAD
  `f779185df81d`)
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `59a9dac94a3d462cb676ae4470c5c957400ee5bff896d36c733c4455df8ca13e`
- XFB YUYV SHA-256:
  `9bd9070b86267cac3f7f3095f93b1a041ad26892a8b66cfe852384296f2c430e`
- XFB PNG SHA-256:
  `985e5dc383d90bd5a285dc9d990279858feb60f734eaf8d5e1757c47d37e8654`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_mapping=affine direct_primitive=quad texcoord_space=normalized texel_bias_eighths=-4 hold_frame=1`

With BP 0x10 cleared, the full affine output matches the source at offset
`(0,0)` for 305409 of 307200 pixels (99.4170%). All eight expected probe
levels are present and there are no unknown output symbols. Every mismatch is
confined to the union of rows 0-3 and columns 0-3; the remaining 636x476
interior is exact for all 302736 pixels. Of the border mismatches, 1312 match
the previous X texel and 701 match the previous Y texel, with random probe
agreement accounting for overlap.

The old full-frame mixed-offset failure is gone. The residual is specifically
the negative-half coordinate phase crossing the top and left clamp boundary,
not stale indirect state. Retain the fixed binary and test
`texel_bias_eighths=-2` to move samples one-quarter texel inward without
changing the source mapping by a whole texel.

## 2026-07-29: Negative-quarter phase gives an exact affine blit

- Test implementation: `5b8a19800861` (capture run at docs-only HEAD
  `dc5f0d63d66c`)
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `59a9dac94a3d462cb676ae4470c5c957400ee5bff896d36c733c4455df8ca13e`
- XFB YUYV SHA-256:
  `764f1a9f4d97f1e3385e09a16fd80fb1be596215fd63eca442876a1be1230cac`
- XFB PNG SHA-256:
  `325373e0d050429d78fd042ed0cea1866bb7df5301feeb0e712dee6706f7c6cc`
- VFB RGB565BE SHA-256:
  `097cf3243cdac9ad91917aa50953ab54e12da1adb9daa1db5c0e628a150218ee`
- VFB PNG SHA-256:
  `bb9316286754514190184fe6245bc65ddf2d270bef10cd0970bb1fa23ae2e501`
- Parameters:
  `renderer=generated texture_source=probe probe_seed=0 texcoord_source=direct texcoord_mapping=affine direct_primitive=quad texcoord_space=normalized texel_bias_eighths=-2 hold_frame=1`

The negative-quarter phase produces an exact one-to-one full-frame texture
blit. All 307200 XFB luma symbols match source coordinate `(0,0)`; mismatch
count is zero, including the complete top and left boundaries. All eight
probe levels remain present and no unexpected output symbol appears. The PE
marker completes normally and the FIFO drains completely.

Make `texel_bias_eighths=-2` the default. Then validate the normal production
configuration: generated renderer, live console texture, position-derived
coordinates, affine mapping, and continuous output. The deterministic probe
has now validated texture upload, tiling, direct coordinates, TMU lookup,
rasterization, TEV, EFB copy, and XFB publication exactly.
