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

## 2026-07-29: Production live-console milestone passed

- Implementation: `d884291369fa`
- Kernel image SHA-256:
  `87f3732a65c836824ba8bcff450a872d5fc036c990dc244a93262cc6d86e041a`
- GX module SHA-256:
  `d0f8b8ec5c2d57fb76f58adb77586b3df86e9b4a1b60ca72b31e2dc7fdcd3e4c`
- Parameters:
  `renderer=generated texture_source=console texcoord_source=position texcoord_mapping=affine direct_primitive=quad texcoord_space=normalized texel_bias_eighths=-2 hold_frame=0`

The normal position-derived, continuously publishing console path produced a
clear console and blinking cursor, visually confirmed by the user. The module
alternated physical XFBs `0x0172e000` and `0x01698000`, advanced past 56880
worker runs, and continued rendering without PE timeout, FIFO mismatch, GP
stall, machine check, or self-reboot. The first four live texture digests
matched their corresponding VFB sums, XOR values, and nonzero counts after
CPU tiling.

Keyboard responsiveness is not a graphics failure in this run. The kernel
command line specifies `init=/init-diag.sh`; that diagnostic PID 1 does not
start SysV init or any configured tty1 getty. `/sys/class/tty/tty0/active`
reports tty1, but no foreground login process owns it. Test local interaction
only after replacing the diagnostic init path or explicitly launching a getty.

This is the first production-path milestone: exact deterministic rendering
and stable live framebuffer presentation both pass. Remaining work is product
hardening rather than the original texture-corruption investigation: cold
boots, long-duration and load testing, normal getty/init integration,
unload/reload fallback, RGB888 disposition, synchronization/tearing checks,
and removal or gating of diagnostic state and logging.

## 2026-07-29: Hollywood OHCI keyboard-support test

- Test implementation: `c013cfb5c25f`
- Kernel image SHA-256:
  `ab6fc4f96878c9f6d85dc8bb02913ee1d85a95b6646dbc64ba7d00e67a295709`

The stable GX console had a running tty1 getty but no keyboard input. Runtime
inspection showed only the Hollywood GPIO buttons in `/proc/bus/input/devices`;
the kernel had `CONFIG_USB` disabled and no driver bound to either Wii OHCI
device-tree node.

This test ports the minimum legacy Hollywood OHCI support needed for a USB
keyboard onto the Linux 6.18 generic platform OHCI driver. It adds the Wii DT
match, big-endian register access with little-endian descriptors, Hollywood's
EHCI-vendor-register interrupt routing, and the legacy control-list and
interrupt/bulk scheduling workarounds. Streaming USB payloads retain a 32-bit
DMA mask, while coherent OHCI schedule structures are constrained below 16 MiB
in MEM1 to avoid the known uncached-MEM2 subword-store limitation without
restricting ordinary transfer buffers.

The Wii defconfig now builds USB core, OHCI, generic HID, and USB HID into the
kernel. The complete `zImage modules` build passes and the final objects contain
all three Hollywood quirk functions. Hardware positive control is a USB
keyboard appearing in `/proc/bus/input/devices` and producing tty1 input; boot
logs should also show both `ohci-platform` root hubs. This remains unvalidated
until that exact checksum is deployed and cold-booted.

Hardware result: the exact image checksum was deployed and cold-booted. USB
core and `usbhid` initialized, and both DT nodes matched `ohci-platform`, but
both probes returned `-EIO` before printing the OHCI product description or
requesting an IRQ. No USB input device appeared. The failure is before
controller-register setup: `dma_set_coherent_mask(DMA_BIT_MASK(24))` calls
`dma_direct_supported()`, which rejects any mask below the Wii's highest MEM2
PFN even though suitable MEM1 exists below 16 MiB.

The DT binding and driver selection are therefore validated, but the 24-bit
coherent-mask mechanism is ruled out. Replace it with explicit per-controller
coherent pools reserved in the free MEM1 gap between the GX texture buffers
and FIFO. Keep the streaming mask at 32 bits. This preserves the intended
memory safety property while satisfying the direct-DMA layer.

## 2026-07-29: Explicit MEM1 OHCI coherent-pool test

- Test implementation: `8cc37053f84c`
- Kernel image SHA-256:
  `3240ec66718963035d177779cb31d849811325a3e025414c207ffdee0e8cebd9`

This test changes only the DMA allocation mechanism rejected by the first
hardware run. The DT reserves `0x01400000-0x014fffff` in the unused MEM1 gap
and assigns `0x01400000+0x80000` and `0x01480000+0x80000` to the two OHCI
nodes. The platform driver retains a 32-bit DMA mask for streaming payloads,
declares each second resource as the controller's coherent pool, and releases
it on all probe-failure and remove paths.

The complete kernel/modules build passes. Decompiling the built DT confirms
the memreserve and both resources, while `ohci-platform.o` references both
`dma_declare_coherent_memory` and `dma_release_coherent_memory`. Hardware
positive controls remain two registered OHCI root hubs and a keyboard in
`/proc/bus/input/devices`; the stronger functional control is key input at the
tty1 getty. No conclusion is valid until the exact image checksum is deployed.

Hardware result: the exact image checksum was deployed and cold-booted. USB
core and `usbhid` initialized, but neither controller bound and no OHCI probe
message, IRQ, root hub, or input device appeared. Both platform devices and
their correct modaliases exist, and a manual bind fails before any driver
output. The pool was incorrectly encoded as a second `reg` entry beneath the
Hollywood bus even though that bus's `ranges` translates only Hollywood MMIO,
not MEM1 RAM.

The explicit-pool allocation strategy remains valid, but this DT
representation is ruled out. Describe each pool under the root-level
`reserved-memory` node, reference it from its controller with `memory-region`,
and attach it using `of_reserved_mem_device_init()`. Restore each OHCI `reg`
property to MMIO only.

## 2026-07-29: Standard reserved-memory OHCI pool test

- Test implementation: `0969cee575cf`
- Kernel image SHA-256:
  `c53d336a88ec81c41721782eaa038732257cb7dc090dc91607b105f5839bc86f`

The two MEM1 pools are now root-level `shared-dma-pool` reserved-memory nodes.
Each OHCI node has only its translatable Hollywood MMIO in `reg` and references
one pool through `memory-region`. The platform driver calls
`of_reserved_mem_device_init()` after configuring 32-bit DMA and releases the
association on every failure/remove path.

The complete build passes. Decompiling the built DT confirms both pool nodes,
their `no-map` properties, MMIO-only controller resources, and correct phandle
references. The platform object links the OF reserved-memory init and release
APIs. Hardware positive controls remain successful pool attachment, two OHCI
root hubs/IRQs, keyboard enumeration, and actual tty1 key input. This exact
checksum must be deployed before drawing a conclusion.

Hardware result: the exact image checksum was deployed and cold-booted. The
first reserved-memory pool attached successfully, controller 0 registered USB
bus 1 on IRQ 19, its root hub enumerated, and the hardware detected the
keyboard electrically as a new low-speed device. This is the first successful
Hollywood OHCI registration and device-connect positive control on Linux 6.18.
The keyboard did not complete descriptor enumeration, however, so no HID/input
device appeared and tty1 still received no keys. The controller's IRQ count
remained at 8 while enumeration was stalled.

Controller 1 failed probe with `-EINVAL`. Early boot reported that
`dma-pool@1480000` could not be reserved. The boot wrapper relocates to
`0x00f00000`, and this build's compressed image extends to approximately
`0x014f9000`, overlapping the second pool at `0x01480000`. The first coherent
allocation also raised a PowerPC alignment warning from `memset()` inside
`dma_alloc_from_dev_coherent()`, although execution continued and the root hub
worked. A later `memremap` warning for `0x01480000` is consistent with the
second controller attempting to attach the failed pool.

Keep the standard reserved-memory mechanism, but replace the adjacent pools
with one shared 1 MiB pool at `0x01500000`, above the relocated image and below
the GX FIFO at `0x01684000`. Both controllers can reference the same
`shared-dma-pool`; its allocator bitmap will arbitrate their allocations. Add
bounded logging around the Hollywood control-list workaround to establish
whether the first keyboard control transfer reaches it and where controller
state stops changing. Treat the alignment warning as an unresolved candidate
if enumeration still stalls.

## 2026-07-29: Shared MEM1 pool and control-workaround trace test

- Test implementation: `ae8821ae4`
- Kernel image SHA-256:
  `8adf892b13fa77d73aa99cfd3be9bade33fd593fb69e05bf67382193af462963`

Both Hollywood OHCI hosts now reference one 1 MiB `shared-dma-pool` at
`0x01500000`. Linux creates one coherent-memory allocator and serializes both
devices' allocations through its shared bitmap. The final wrapper is 6264204
bytes and relocates to `0x00f00000`, ending at `0x014f958c`; the new pool begins
about 27 KiB later and ends below the GX FIFO at `0x01684000`.

The first eight control-list workaround calls per controller now log their
saved control head, control-current value before and after the 10 us poll,
dummy ED DMA address, and poll result. This is bounded diagnostic output and
does not modify the stable GX path. The complete `zImage modules` build passes
with `make -j16`, and the compiled DT has both controller phandles referencing
the same pool.

Hardware positive controls are: the pool reserves without an early-boot
failure, both root hubs register, and at least one `hlwd control[...]` line
appears when the keyboard starts descriptor enumeration. Functional success
requires a USB HID/input device and actual tty1 key input. If the transfer
still stalls, the trace must be interpreted before changing descriptor memory
handling; the Test 3 alignment warning remains unresolved.

Hardware result: the exact image checksum was deployed and booted as kernel
build `#17`. The shared pool reserved successfully and attached to both
controllers. Both root hubs registered on IRQs 19 and 20, and hardware detected
one low-speed and one full-speed device. Both controller frame counters advance,
their root ports report connected and enabled, and each async schedule contains
the device-zero control ED. This validates the shared-pool placement and both
host-controller bring-up paths.

Neither device completed its initial `get_bMaxPacketSize0()` descriptor request.
Both USB hub workers eventually blocked in `usb_kill_urb()` after the request
timed out, both IRQ counters remained at 8, and no USB child or HID/input device
appeared. Debugfs showed control heads `0x01504000` and `0x01502000`, but empty
software TD lists by the time the workers were waiting for unlink completion.
The coherent-pool `memset()` alignment warning still occurred once.

The bounded positive control also failed: no `hlwd control[...]` line appeared.
The function exists in kallsyms, the built object contains the call from the
control submission branch, and `CONFIG_USB_OHCI_HCD_HLWD=y`. Therefore do not
interpret the missing line as proof that the workaround itself failed. Trace
the enqueue path before/after ED scheduling and TD submission, including the
live Wii flag, ED head/tail, control head/current, and HCCA done head. Only after
that trace should the old driver's 32-bit software-field workaround be ported.

## 2026-07-29: OHCI descriptor enqueue-stage trace

- Test implementation: `03efbcbd7`
- Kernel image SHA-256:
  `e56fd333762f29fc6a311d31a67af9652b6486ec766b1716215f1183f04f8d27`

This test retains the validated shared MEM1 pool and changes no GX or OHCI
scheduling behavior. Each Hollywood platform probe logs the live quirk flags
after `usb_add_hcd()`. For only the first eight Wii URBs per controller, the
enqueue path logs entry, the state after ED scheduling, and the state after TD
publication. The latter two records include ED DMA/head/tail, hardware control
head/current, and HCCA done head. The existing bounded control-workaround trace
remains enabled.

The complete `zImage modules` build passes with `make -j16`. The image is
6264404 bytes; at the wrapper's `0x00f00000` relocation it still ends below the
shared pool at `0x01500000`. Hardware interpretation requires the first device
request to show `flags` containing `OHCI_QUIRK_WII`, a matching enqueue triplet,
and a control-workaround line. The submitted ED/TD pointers and later debugfs
state will distinguish a publication failure from controller execution or
done-list/unlink failure.

Hardware result: the exact image checksum was deployed and booted as kernel
build `#18`. Both platform probes retained live flags `0x2010`, confirming
`OHCI_QUIRK_BE_MMIO | OHCI_QUIRK_WII` survived generic initialization. Both
initial device requests entered enqueue with `type=2` (`PIPE_CONTROL`) and a
64-byte transfer. ED scheduling succeeded with state `ED_OPER`, control heads
`0x01502000` and `0x01504000`, and zero control-current/done-head values.

The post-submit trace was unchanged for both devices: each ED's head and tail
still pointed to the same dummy TD (`0x01503000` or `0x01505000`). No
`hlwd control[...]` line appeared. Therefore `td_submit_urb()` published no
control TDs even though the same ED was type 2 immediately before scheduling.
The failure is before hardware transfer execution, done-list publication, and
unlink handling.

This result directly motivates the legacy Wii workaround that widens software
subword fields inside DMA-coherent ED/TD objects to 32 bits. In particular,
`ed_schedule()` writes byte-sized `ed->state` before `td_submit_urb()` switches
on adjacent byte-sized `ed->type`. The pool is mapped write-combining, the boot
already proves unsupported accesses through the coherent `memset()` alignment
exception, and the old driver explicitly widened `state`, `type`, `branch`,
periodic 16-bit fields, `tick`, and `td->index` for this hardware. Port exactly
that layout change next while retaining the trace as a positive control.

## 2026-07-29: Wii 32-bit OHCI descriptor software fields

- Test implementation: `d76d69d80`
- Kernel image SHA-256:
  `ca38449316333276a1907508e3d25880adf91afd94f972699419b798624eeddc`

This test ports the original Wii driver's descriptor-layout workaround to the
Linux 6.18 structures. Under `CONFIG_USB_OHCI_HCD_HLWD`, software-owned ED
fields `state`, `type`, `branch`, `interval`, `load`, `last_iso`, and `tick`,
plus `td->index`, are stored as 32-bit values. Hardware-defined ED and TD words
retain their exact OHCI layout, and non-Wii builds retain the generic compact
software fields.

The complete `zImage modules` build passes with `make -j16`; the final image is
6264216 bytes and remains below the shared pool after wrapper relocation. The
retained Test 5 trace provides the positive control. Success first requires a
`hlwd control[...]` line and a submitted ED tail different from its initial
dummy head. Complete success requires descriptor enumeration, USB HID/input
registration, increasing OHCI IRQ counts, and actual tty1 key input. The
coherent-pool `memset()` alignment warning may remain independently and must
not be conflated with whether subword field corruption is fixed.

Hardware result: the exact image checksum was deployed and booted as kernel
build `#19`. The positive control passed on both controllers. Every traced
device-zero request entered as `PIPE_CONTROL`, emitted a successful
`hlwd control[...]` record with `poll=0`, and advanced the ED tail from its
dummy head to a published control TD chain. Control-current also changed from
zero to the scheduled ED while the controller processed each request.

Both devices completed enumeration. Controller 0 registered the Dell USB
keyboard (`413c:2105`) through `hid-generic` as `input1`, with `kbd`, `event1`,
LED, and SysRq handlers. Controller 1 registered the Wii's internal Broadcom
BCM2045A Bluetooth USB device (`057e:0305`). OHCI IRQ counts advanced from the
previous stuck value of 8 to 65 and 46 at inspection time. This validates
control transfer submission, completion interrupts, descriptor enumeration,
and HID interrupt-endpoint setup on both Hollywood OHCI hosts.

The coherent-pool `memset()` alignment warning still appears once during host
setup, but it no longer prevents operation and is independent cleanup work.
The diagnostic PID 1 does not normally launch a getty, so a temporary tty1
getty was started for an explicit physical key-input check. The user pressed
keys on the attached Dell keyboard and confirmed that tty1 responded normally.
This completes the end-to-end keyboard positive control. Remove the bounded
enqueue/control traces, retaining the shared pool, Hollywood scheduling
workarounds, and 32-bit descriptor software fields.

## 2026-07-29: Production Hollywood OHCI cleanup

- Test implementation: `966a936a7`
- Kernel image SHA-256:
  `c4536d3faff99c5964c3628d18ae9d82536e8f5c4fc537e258e3a7ccf3ac2e00`

The temporary platform, enqueue-stage, descriptor-pointer, and successful
control-reset traces are removed. Special ED/TD allocation failures remain
errors, and a failed Hollywood control-list reset poll now produces a warning.
The validated shared pool, BE-MMIO mode, scheduling workarounds, and 32-bit
descriptor software fields are unchanged.

The complete `zImage modules` build passes with `make -j16`. This cleanup image
is 6263544 bytes and remains below the shared pool after wrapper relocation.
Remote hardware validation requires both USB devices to enumerate again, the
Dell keyboard to register through `hid-generic`, both OHCI IRQ counters to rise
above the root-hub-only baseline, and no new Hollywood timeout warning. The
physical key-input control already passed on the immediately preceding binary.

Hardware result: the exact cleanup image checksum was deployed and booted as
kernel build `#20`. The BCM2045A and Dell keyboard enumerated again, the
keyboard registered through `hid-generic` with its full input handlers, and
OHCI IRQ counts reached 64 and 46. No retired `hlwd enqueue`/`hlwd control`
trace and no Hollywood timeout warning appeared. Combined with the physical
key-input control on build `#19`, this closes the Hollywood OHCI keyboard port.

## 2026-07-29: Automated deployment reboot validation

- Deployment helper: `9b0bafd82`
- Re-deployed kernel image SHA-256:
  `c4536d3faff99c5964c3628d18ae9d82536e8f5c4fc537e258e3a7ccf3ac2e00`

The deployment helper now defaults every kernel make invocation to `-j16` and
uses the tested SysRq reboot path after syncing and unmounting the boot volume.
This replaces `reboot -f`, which the diagnostic PID 1 did not reliably service.

The helper checksum-verified and re-deployed the unchanged production OHCI
image over SSH, successfully forced a reboot, and the Wii returned to SSH at
approximately 50 seconds uptime on kernel build `#20`. The BCM2045A and Dell
USB keyboard both enumerated again, and `hid-generic` registered the keyboard.
This validates the automated deploy/reboot path without introducing a new
kernel binary or changing the already-validated hardware result.

## 2026-07-29: Normal SysV init test with writable root

- Test implementation: `15b1fea7f`
- Kernel image SHA-256:
  `4fec4709a6c47e21a8ca71fa4e97c4bfea92563e0903ca1eeebfd866ccc43eba`

This test removed only the temporary `init=/init-diag.sh` command-line override
and left the existing `rootwait rw` root-mount arguments unchanged. Before
deployment, the rootfs received a generated module dependency index, `b43` in
`/etc/modules`, and a checksum-verified copy of the stable `gcn-gx.ko`. GX was
intentionally not listed for automatic loading so this test isolated normal
userspace boot under the CPU framebuffer fallback.

The exact image deployed and the SysRq reboot path executed, but SSH did not
return during more than four minutes of polling. The diagnostic-script control
had returned around 50 seconds after the preceding reboot. No Wii console
capture was available, so the exact userspace stop point is not observed and
the failed test must not be interpreted as a Wi-Fi-specific result.

The test retained an invalid normal-init boot contract: Debian SysV
`checkroot.sh` expects the root filesystem to be mounted read-only while it
performs the root check and then remounts it writable, but the kernel mounted
root with `rw`. The diagnostic PID 1 required that old setting because it wrote
logs before explicitly remounting root. Retry normal SysV init with `rootwait
ro`; keep all other command-line arguments and the CPU-only graphics isolation
unchanged.

## 2026-07-29: Stage read-only-root SysV init retry

- Test implementation: `9af3d245d`
- Kernel image SHA-256:
  `adcf9687fe91a6ce481f795d47d77c300ca38c1e83401a6c492ed158b5b652df`
- Stable GX module SHA-256, installed but not auto-loaded:
  `d0f8b8ec5c2d57fb76f58adb77586b3df86e9b4a1b60ca72b31e2dc7fdcd3e4c`

The complete `zImage modules` build passes with `make -j16`. This retry changes
only the root mount argument from `rw` to `ro`, allowing the existing Debian
SysV root check to run before userspace remounts root writable. The diagnostic
init override remains removed and GX remains absent from `/etc/modules` so the
test continues to use the known CPU framebuffer fallback.

The preceding failed image left the Wii unreachable over SSH, so this image is
built and checksum-staged but not yet deployed. Hardware success requires PID
1 to be `/sbin/init`, runlevel 2 with a tty1 getty, Wi-Fi and SSH to return, both
Hollywood OHCI devices to enumerate, and no resident `gcn_gx` module. Do not
enable GX auto-loading until those controls pass.

Hardware result: the exact image booted successfully with `/sbin/init` as PID
1, runlevel 2, active gettys, and the root filesystem correctly processed from
the read-only kernel mount. Both the BCM2045A and Dell keyboard enumerated, the
keyboard bound through `hid-generic`, and `gcn_gx` remained unloaded under the
intended CPU framebuffer control.

Automatic networking exposed two independent rootfs issues. A stale persistent
rule assigned another Wii MAC to `wlan0` and renamed this Wii to `wlan1`; the
rule was backed up and corrected for MAC `00:1e:35:98:ea:c9`. On the following
boot, legacy ifupdown associated `wlan0` at approximately 18 seconds but its WPA
helper deliberately deauthenticated at approximately 27 seconds. The spawned
`dhclient` then retried indefinitely against the disconnected interface. The
known manual sequence immediately associated, acquired `10.3.10.12`, and
restored SSH. This validates normal userspace boot and isolates the remaining
failure to legacy network orchestration.

## 2026-07-29: Stage stable SysV wireless startup

- Rootfs script implementation: `f12a5b2c0`
- Rootfs script SHA-256:
  `456c6ca0ed9e3967e16cc830c3540e3b0d96fb8f488d31d2428fc295738b712e`
- Unchanged kernel image SHA-256:
  `adcf9687fe91a6ce481f795d47d77c300ca38c1e83401a6c492ed158b5b652df`

The credential-free `tools/rootfs/wii-network` script reproduces the proven
manual sequence while reading the existing private WPA configuration from the
rootfs. It requires eight consecutive seconds of WPA `COMPLETED` state before
starting a bounded one-shot DHCP request and emits status to the visible boot
console. POSIX shell syntax passes; ShellCheck was not installed on the build
host, so no ShellCheck result is claimed.

Install it as `/etc/init.d/wii-network`, replace only the failing
`/etc/rcS.d/S11networking` link with `S11wii-network`, and reboot normally.
Success requires automatic `wlan0` association, DHCP address acquisition, and
SSH availability without console commands. Retain normal PID 1/getty, OHCI,
and CPU-framebuffer controls during this rootfs-only test.

Hardware result: the exact script succeeded when invoked manually and acquired
the expected WLAN address, validating its WPA stabilization and DHCP sequence.
It was not invoked during boot. Debian's `/etc/init.d/rc` uses makefile-style
concurrent startup from `/etc/init.d/.depend.boot`; manually adding the rcS
symlink did not add `wii-network` to that dependency graph. This is a boot
registration failure, not a script or wireless failure.

## 2026-07-29: Stage insserv-registered wireless startup

- Rootfs script implementation: `926eaa7f2`
- Rootfs script SHA-256:
  `1f7b5ed1264c9338c536938d9eb94bcfa00eca448938c150c4e6ffda22acc416`

The LSB metadata now requires only local filesystems. Requiring remote
filesystems was directionally wrong because wireless networking must be ready
before remote mounts are attempted. Install the revised checksum-identified
script and run `insserv wii-network` to regenerate `.depend.boot`, `.depend.start`,
and `.depend.stop`. Verify that `wii-network` appears in the boot dependency
targets before rebooting.

The unattended positive control remains automatic WPA, DHCP, and SSH return at
`10.3.10.12` without console commands. The unchanged normal-init kernel and
CPU framebuffer control remain in place.

Hardware result: after removing and re-adding the service through `insserv`,
`.depend.boot` included `wii-network` after local mounts and generated matching
start and shutdown links. A manual cold reboot then returned SSH with only
45.94 seconds uptime and no network commands entered at the console.

PID 1 was normal SysV init at runlevel 2. `wlan0` held `10.3.10.12/24` with the
expected default route, and the live WPA and DHCP processes used the script's
dedicated PID files and arguments. SSH and gettys were active, both the
BCM2045A and Dell keyboard enumerated, `hid-generic` bound the keyboard, and
`gcn_gx` remained unloaded. This passes the unattended normal-init, wireless,
SSH, OHCI, and CPU-framebuffer control milestone.

## 2026-07-29: Stage automatic production GX loading

- Unchanged kernel image SHA-256:
  `adcf9687fe91a6ce481f795d47d77c300ca38c1e83401a6c492ed158b5b652df`
- Installed GX module SHA-256:
  `d0f8b8ec5c2d57fb76f58adb77586b3df86e9b4a1b60ca72b31e2dc7fdcd3e4c`

Add only `gcn_gx` after `b43` in the rootfs `/etc/modules` file and cold boot.
The module is byte-identical to the previously validated exact RGB565 renderer,
uses its production defaults (`renderer=generated`, `texture_source=console`,
and `texel_bias_eighths=-2`), and is already indexed by `depmod` for this exact
kernel vermagic.

Success requires the unattended normal-init/Wi-Fi/SSH/OHCI controls to remain
green, `gcn_gx` to be resident, and `gcnfb` to register the accelerator. The
display must transition from the initial CPU framebuffer to a stable readable
GX console without freezes, repeated columns, or persistent diagnostic fills.
Module unload must still restore the CPU console before this phase is closed.

Hardware result: after a synchronized cold reboot, SSH returned at 36.13
seconds uptime with normal init, automatic wireless, both OHCI devices, and
`gcn_gx` resident. The module registered with `gcnfb`, completed the seed,
isolated initialization, and first-live-frame PE token fences, and continuously
alternated the two XFB addresses with drained submissions. The user visually
confirmed a clear console with no blur, repeated columns, solid diagnostic
fill, or freeze.

At 78 seconds uptime, `rmmod gcn_gx` completed cleanly and `gcnfb` reported that
the accelerator was unregistered and software conversion had resumed. The user
again confirmed that the CPU-rendered console remained clear. This passes
production auto-load and immediate CPU fallback. A runtime reload remains as
the final module-lifecycle control before diagnostic cleanup.

## 2026-07-29: Stage production GX runtime reload

- GX module SHA-256:
  `d0f8b8ec5c2d57fb76f58adb77586b3df86e9b4a1b60ca72b31e2dc7fdcd3e4c`

Reload the same module with default parameters after the successful automatic
load and unload controls, without rebooting or rewriting the module. Success
requires a second accelerator registration, complete PE-fenced seed/init/live
startup, continued SSH and OHCI operation, and another visually clear live GX
console. Unload once more after the visual control so the CPU fallback remains
the recovery state if reload exposes a lifecycle bug.

Hardware result: `modprobe gcn_gx` at approximately 307 seconds uptime
registered the accelerator a second time without rebooting. The seed,
initialization, and first live frame each completed their PE token fence; live
rendering resumed with alternating XFB addresses while Wi-Fi and SSH remained
up. The user visually confirmed that the reloaded GX console was clear.

The reloaded module then ran beyond 2,000 worker iterations before the planned
final unload at approximately 379 seconds. `gcnfb` restored software conversion,
the module left `/proc/modules`, Wi-Fi remained configured, and the user again
confirmed a clear CPU console. This passes the complete production module
lifecycle and leaves the Wii in the known recovery state.

## 2026-07-29: Stage production diagnostic cleanup

- Test implementation: `26223813f`
- GX module SHA-256:
  `128f477e24920471c8d4d2e1c7a250a1c3979201a78eab53f692ca77d68794bd`
- Module size: 41,424 bytes (previously 44,832 bytes)

Normal module loading no longer allocates the two 614,400-byte debug snapshots,
calculates first-frame CRC/sum diagnostics, or emits per-submit and recurring
worker/texture progress logs. Debugfs capture remains available through the
explicit `debug_capture=1` parameter, which the live-cycle tool now supplies.
Validated startup phases, PE token fences, slow/stall/timeout warnings, exact
generated rendering, XFB alternation, and CPU fallback are unchanged.

Load this exact module with default parameters from the current CPU-console
recovery state. Success requires a visually clear GX console, only bounded
ready/active/registration information in the healthy log, no debugfs capture
directory, and no recurring `gcn-gx` output after steady state. Then unload and
require the same clear CPU fallback. A separate opt-in capture control must
subsequently prove that `debug_capture=1` still exposes working VFB/XFB files.

Hardware result: the exact module loaded with production defaults and reported
`debug_capture=0`. `/sys/kernel/debug/gcn_gx` was absent, `MemFree` remained
3,096 kB across the 20-second control, and `VmallocUsed` increased by only 28
kB rather than allocating the former 1.2 MB of snapshots. Healthy startup
emitted exactly three bounded lines: driver ready, accelerator registered, and
generated renderer active. No recurring GX output followed. The user confirmed
a clear GX console, then confirmed the same clear CPU console after unload.

The same module was loaded separately with `debug_capture=1` and
`hold_frame=8`. The live-cycle tool retrieved checksum-verified 614,400-byte
captures from both debugfs files:

- XFB YUYV SHA-256:
  `6d95c07564420ef68da1f53ec2a0b18e473bf1e29eab86ece34565dae5ecfa98`
- VFB RGB565BE SHA-256:
  `bc5ce5bdfd43eaf6b9b05694204a3a44bbc6d6db4c363b4972ab401586e3e47b`

The generated PNG from the hardware XFB capture is a clear, correctly framed
console. Final unload removed the debugfs directory, restored software
conversion, returned `VmallocUsed` to 3,936 kB, and left the CPU console as the
recovery state. This validates both low-overhead production defaults and the
explicit diagnostic capture path.

## 2026-07-30: Stage cleaned GX boot-default deployment

- GX module SHA-256:
  `128f477e24920471c8d4d2e1c7a250a1c3979201a78eab53f692ca77d68794bd`

Install the already hardware-validated production module at
`/lib/modules/6.18.40-wii+/gcn-gx.ko`, regenerate module dependencies, and cold
boot through the existing `gcn_gx` entry in `/etc/modules`. Preserve the prior
module as a checksum-addressed rootfs backup.

Success requires unattended normal init, Wi-Fi, SSH, OHCI, and automatic GX
registration at low uptime; `debug_capture` must remain disabled, the healthy
boot must emit only bounded GX startup lines, and the user must again confirm a
clear console. This is deployment verification of the tested binary, not a new
renderer experiment.

Hardware result: the checksum-verified module replaced the prior rootfs copy,
whose SHA-256-addressed backup was retained. After the synchronized cold reboot,
SSH returned at 39.22 seconds uptime with normal SysV init at runlevel 2,
automatic `wlan0`, both OHCI devices, and the exact cleaned GX module resident.

`debug_capture` remained disabled. The complete healthy boot log contained only
the initial CPU-fallback probe line followed by GX ready, accelerator registered,
and generated renderer active. No GX timeout, slow, stall, failure, warning,
oops, or recurring progress message appeared. The user confirmed a clear
console. This passes final production boot deployment and leaves the Wii
running the cleaned automatically loaded accelerator.

## 2026-07-30: Stage sustained changing-frame RGB565 workload

- Test implementation: `80dce19d9828`
- Static PowerPC workload SHA-256:
  `38c81219ee84cbffadd0e66aaa53e628d101ba4cb12b4b4f13db7eb4839859b2`
- Workload size: 784,652 bytes
- Planned duration and input rate: 120 seconds at 30 frames per second

Add a reusable target workload and host runner for the first sustained-motion
test of the production GX path. The target validates the live framebuffer as
RGB565, generates a deterministic full-screen colour-bar and alignment grid,
and moves independent vertical, horizontal, diagonal, and binary frame-count
markers. It regenerates one 614,400-byte staging frame and copies that complete
frame into `/dev/fb0` on every update. This bounds target memory use while
continuously changing the source image consumed by the GX worker.

The host runner cross-builds a static big-endian PowerPC executable, verifies
its checksum after SSH deployment, requires `gcn_gx` to be loaded, brackets the
run with unique kernel-log markers, and samples the `gcn-gx-pe-finish`
interrupt. It leaves the module and boot state unchanged and asks fbcon to
repaint a recovery status screen when the workload exits.

The pattern itself is the visual positive control: the red/white vertical bar,
cyan horizontal bar, white diagonal, and binary frame blocks must visibly move
while the static grid and colour boundaries remain spatially intact. Success
requires the target process to complete near 30 fps, the PE finish IRQ to
advance throughout the run, SSH and `gcn_gx` to remain live, no new GX
timeout/stall/warning/oops messages, and the clear console to return afterward.
Any tearing, stale regions, blur, duplicated rows or columns, solid diagnostic
fill, reboot, or lost SSH is a failure even if the process exits successfully.

Hardware result: the checksum-verified workload completed the full 120 seconds
without a signal, reboot, lost SSH, module unload, or network loss. It rendered
1,219 complete source frames in 120.021 seconds (10.16 fps). During the same
interval, the `gcn-gx-pe-finish` interrupt advanced from 47,729 to 55,020: a
delta of 7,291, or 60.76 completions per second. The kernel log contained only
the test's begin/end markers after startup; there was no GX timeout, stall,
warning, oops, or other fault.

The user reported that the pattern was mostly clear and smooth, with clipping
limited to the red/white vertical bar moving horizontally. The console returned
clear after the run. This clipping result is not yet attributable to GX because
the first pattern intentionally drew the independently moving cyan bar and
white diagonal over the red/white bar, creating expected occlusion that was not
visually distinguishable from a transfer defect.

The 30 fps source-rate criterion failed because the target pattern generator
recomputed every background pixel with integer division each frame; the GX
completion rate itself remained at the expected approximately 60 Hz. The host
runner also expanded an unescaped `$p` under `set -u` while printing its final
`sed` range, after the target process and measurements had completed. An
explicit follow-up SSH command restored fbcon and confirmed the complete log.
Optimize background generation, place moving elements in non-overlapping
regions, make console restoration unconditional, and repeat before closing the
sustained-motion milestone.

## 2026-07-30: Stage optimized sustained-motion rerun

- Test implementation: `a1cc9e37afc1`
- Static PowerPC workload SHA-256:
  `6d9dee1b5e59cdcf26af44cf1b51d391a13d82423c76f1a9af4fddc5b5edeb6c`
- Workload size: 784,652 bytes
- Planned duration and input rate: 120 seconds at 30 frames per second

Replace the expensive per-pixel background reconstruction with three
precomputed scanline templates while retaining one full staging frame and one
complete `/dev/fb0` copy per source update. Confine the vertical marker to the
upper half and the horizontal marker to the lower half, remove the crossing
diagonal, and use stable marker colours. No moving element now intentionally
clips another, so a discontinuity has a meaningful visual interpretation.

The host runner now captures the workload result, rejects an achieved source
rate below 90 percent of the request, records new dmesg lines without the prior
shell-expansion bug, and restores fbcon from an exit trap on success or failure.
Repeat the same 120-second visual and PE-interrupt controls. Success requires at
least 27 source frames per second, approximately 60 PE finish interrupts per
second, two smooth and continuous moving markers in their separate regions,
intact static grid/band geometry, no new kernel fault, continued SSH/module
residency, and a clear recovered console.

Hardware result: the optimized workload completed all 120 seconds and passed
every machine-checkable control. It produced 3,601 source frames in 120.011
seconds (30.01 fps). The `gcn-gx-pe-finish` counter advanced from 78,072 to
85,422, a delta of 7,350 or 61.25 interrupts per second. `gcn_gx`, wlan0, and
SSH remained live. The only intervening kernel messages were routine b43 group
key rotation; there was no GX warning, timeout, stall, oops, or reboot. The
console recovered clear and responsive.

The visual control exposed a remaining presentation defect. The lower
cyan/white horizontal marker remained crisp and smooth throughout, and the
static geometry was crisp. The upper red/white vertical marker moving
horizontally was clear most of the time, but after roughly 20 seconds showed
intermittent tearing for about five seconds. Because the markers no longer
overlap, this is a real failure rather than intentional pattern occlusion.

The orientation-sensitive result matches a source-buffer race: userspace
copies a new linear frame from top to bottom while the GX worker tiles the same
single VFB asynchronously. A capture boundary produces upper and lower
sections of the vertical marker at different X positions, while the full-width
lower marker makes the same boundary much less visible. Stable PE cadence and
intact output outside those boundaries argue against CP, raster, EFB, or XFB
corruption. Do not advance to RGB888 yet. Add an explicit synchronized source
handoff (preferably double-buffered pan/present ownership), then repeat this
same workload as its positive control.

## 2026-07-30: Stage synchronized double-VFB presentation

- Test implementation: `cac528678520`
- Kernel zImage SHA-256:
  `767b532ca0fe1e2460037db58d7c941337ec3e02457e23bf0126193f9f107117`
- GX module SHA-256:
  `b0060f2a18d7b3e75e92f254252eabb22b6f79c32c0edaba0b05684efc9c4bfa`
- Static PowerPC workload SHA-256:
  `3c16da7d587bd0407902a22ae1a39d3ded4d59b0eb2468cab6ce08ea092b3d63`

Implement vertical VFB panning as a synchronized RGB565 source-ownership
protocol. `gcn-gx` now returns the immutable VFB source pointer paired with each
completed XFB. `gcnfb` tracks the requested and presented source yoffsets;
`FBIOPAN_DISPLAY` publishes a completed userspace page and returns only after
that exact source has completed GX rendering and its XFB has been selected for
VI display. The previous VFB page is therefore safe for userspace to reuse.
Software fallback reports the same presentation event after conversion.

Also repair `FBIO_WAITFORVSYNC`, whose previous wait condition could never
become true without a signal, by waiting on an advancing retrace sequence. The
stress client now requests two 640x480 RGB565 pages, alternates them through
`FBIOPAN_DISPLAY`, and restores the original one-page mode on exit. Its
`--single-buffer` option preserves the previously validated tearing-prone
control.

Install the checksum-matched built-in kernel and module together and cold boot.
First require normal init, automatic Wi-Fi/SSH/GX loading, and a clear console.
Then repeat 120 seconds at 30 source fps in the default double-buffered mode.
Success requires at least 27 fps, approximately 60 PE finish interrupts per
second, completely continuous upper and lower moving markers with intact
static geometry, no VFB-present timeout or kernel fault, continued SSH/module
residency, restoration to a 640x480 one-page mode, and a clear responsive
console. The earlier intermittent upper-marker tear is the specific negative
control this test must eliminate.

Hardware result: kernel build `#22` booted normally with automatic Wi-Fi, SSH,
and the checksum-matched GX module. `/boot` returned to its intended read-only
mount after deployment. GX registered and entered the generated renderer with
no startup warning, pan timeout, oops, or reboot.

The double-buffered workload completed 1,200 source presentations in 120.093
seconds (9.99 fps). PE finish interrupts advanced from 3,208 to 10,554, a
delta of 7,346 or 61.22 per second. The module, wlan0, and SSH remained live,
the kernel log contained only the test markers, and the client restored the
original 640x480 one-page mode. The user confirmed that both moving markers
and the static pattern remained crisp with no tearing for the entire run, and
that the recovered console was clear.

This validates double-VFB source ownership as the fix for the prior tear, but
the test fails its source-rate criterion. Two independent costs are present:
the client sleeps one additional frame interval after a blocking pan misses its
absolute deadline, and the kernel blocks `FBIOPAN_DISPLAY` until the selected
XFB reaches VI presentation. Page reuse only needs to wait until the worker has
copied the VFB into private GX texture memory. Split source-consumed from
XFB-presented notification, wait on source consumption, remove the extra client
sleep, and repeat the same 30 fps control. Preserve completed-XFB tracking for
actual display selection and diagnostics.

## 2026-07-30: Stage source-consumed double-VFB optimization

- Test implementation: `db51fdd74`
- Kernel zImage SHA-256:
  `a815fd2c9fe9c59d7266a9a6634d039edbc11022c7c468b8b8911d4143ec0e1d`
- GX module SHA-256:
  `8463f409ae44322df34b1c1b3de2c2353ed75beaad252bf964d01abab96af109`
- Static PowerPC workload SHA-256:
  `40b864eb6a12cb49f2339cc017ad20e416596cd4e0d1f70ac33ec5f9ef16f87b`

Retain separate source-consumed and XFB-presented state. The GX worker now
notifies `gcnfb` immediately after `gx_process_rgb565()` has finished all CPU
reads and tiled the selected linear VFB into one of the two private MEM1
texture buffers. `FBIOPAN_DISPLAY` waits for this consumed marker before
allowing userspace to reuse the previous VFB page. Completed-XFB tracking still
controls VI page selection and records which source was actually displayed.
Mode setup, software conversion, and accelerator-unload restoration initialize
or advance both ownership states so blocked clients retain a fallback path.

The workload also no longer inserts a complete extra frame interval when a
blocking pan has already put its absolute schedule behind. The kernel image,
module, and static client built without compiler diagnostics using `-j16`;
diff-only checkpatch reported zero errors and zero warnings.

Deploy all three checksum-matched artifacts and cold boot. Repeat the same
120-second 30 fps double-buffered workload. Success requires at least 27 source
frames per second, approximately 60 PE finish interrupts per second, no visible
tear in either moving marker, intact static geometry, no consume timeout or
kernel fault, continued SSH/module residency, restoration to the 640x480
one-page mode, and a clear responsive console. A source-rate pass with any
return of tearing is a failure; the optimization must preserve the correctness
demonstrated by the 9.99 fps test.

Hardware result: kernel build `#23` booted normally and the installed GX
module matched the staged checksum. The 120-second double-buffered workload
completed 3,529 source presentations in 120.016 seconds, or 29.40 fps against
the requested 30 fps. This passes the 27 fps minimum and is 2.94 times the
previous synchronized result. PE finish interrupts advanced from 5,505 to
12,867, a delta of 7,362 or 61.35 per second.

The user observed both moving markers throughout the run and reported no
tearing. The client exited successfully, restored the original framebuffer
mode, and the console returned clear and responsive. `gcn_gx`, wlan0, and SSH
remained live. The only intervening kernel messages were routine b43 group-key
rotation; there was no consume timeout, GX warning, oops, stall, or reboot.

This validates the source-consumed handoff as both correct and fast enough for
RGB565 presentation. Keep the separate consumed and presented markers and the
double-VFB stress test as regression coverage. The synchronized RGB565 phase
is complete; proceed to RGB888 functionality and performance validation.

## 2026-07-30: Stage synchronized RGB888 functionality and load test

- Test implementation: `9ebee9c99`
- Kernel zImage SHA-256:
  `830cb8921a7a7ab5dc6d792c261588f5b4da90017d0123eb951a15f0ddb1ae96`
- GX module SHA-256:
  `81c0d4db8ee79f6a478603935e0676ea4570e5d6c2ee20bd433dec7fa3a013ae`
- Static PowerPC workload SHA-256:
  `ae375e8e255f6ef8b3b4a54c87a00a270300a9f254f9c8892da03e119a0844cb`

The previous RGB888 accelerator callback tiled a complete frame and submitted
GX commands directly from the VI hard IRQ, always read the first VFB page,
always targeted the first XFB, and exposed no ownership or completion event.
Replace that path with the validated process-context frame worker. Each work
item carries its RGB565 or XRGB8888 source format. RGB888 pages are converted
to tiled RGB565 in alternating private MEM1 texture buffers, after which the
same consumed notification, PE completion, alternate-XFB presentation, and
double-VFB pan protocol used by RGB565 applies unchanged. Software fallback
also converts the selected RGB888 page and advances both ownership markers.

The workload's `--rgb888` mode requests two 640x480 32-bit XRGB pages, checks
the returned 8:8:8 channel layout, renders native 8-bit primary/secondary color
bars plus the independent motion and static controls, and restores the
original RGB565 console mode from every normal/error exit. The kernel image,
module, and static client built cleanly using `-j16`; diff-only checkpatch
reported zero errors and zero warnings, and the new built-in consumed callback
is present in `Module.symvers`.

Deploy all three checksum-matched artifacts and cold boot. Run the 120-second
double-buffered workload at 30 requested fps with `--rgb888`. Success requires
at least 27 source frames per second, approximately 60 PE finish interrupts per
second, correctly ordered white/yellow/cyan/green/magenta/red/blue/gray bars,
two smooth tear-free moving markers, intact static geometry, no consume timeout
or kernel fault, continued module/Wi-Fi/SSH residency, restoration to the
640x480 RGB565 console mode, and a clear responsive console. Because this path
currently converts XRGB8888 to RGB565 before texturing, the test validates
32-bit framebuffer API compatibility and synchronization, not preservation of
all eight source bits per color channel.

Hardware result: kernel build `#25` booted normally. The previous module
correctly failed its renamed-symbol check, leaving the CPU fallback active;
after checksum-verifying and installing the matching module, `gcn-gx`
registered and entered the generated renderer without a warning or fault.

The RGB888 workload negotiated two 640x480 XRGB8888 pages with a 2,560-byte
stride and completed all 120 seconds. The user confirmed correct color bars,
geometry, and tear-free motion. Motion speed varied during warm-up and then
stabilized. The client produced 1,831 source presentations in 120.050 seconds,
or 15.25 fps, so this run fails the 27 fps throughput requirement. PE finish
interrupts nevertheless advanced from 2,903 to 10,221, a delta of 7,318 or
60.98 per second. This confirms that GX/PE presentation remained healthy while
new source frames arrived at approximately half the requested rate.

The workload restored RGB565 mode, and the user confirmed a clear responsive
console. `gcn_gx`, wlan0, and SSH remained live. The only intervening kernel
message was routine b43 key setup; there was no consume timeout, GX warning,
oops, stall, or reboot. Classify this as an RGB888 functionality,
synchronization, and recovery pass but a performance failure. Instrument
userspace generation/copy, pan wait, and kernel XRGB8888-to-RGB565 tiling
separately before changing the conversion path.

## 2026-07-30: Stage RGB888 source-pipeline profiling

- Test implementation: `2fd2b6759`
- GX module SHA-256:
  `fec433a79024f15df22f805e30a07cafa6f2e724df97de3c5f37ce3abc78a71b`
- Static PowerPC workload SHA-256:
  `6cab962d96314a264afc2f464451d5cd44efc21d6677173ee6a2710ca1cc780b`
- Kernel: unchanged checksum-verified build `#25`

Instrument the failed 15.25 fps RGB888 path without changing rendering or
ownership behavior. The workload measures pattern generation, the complete
1.2 MiB userspace-to-VFB copy, and blocking `FBIOPAN_DISPLAY` separately,
reporting cumulative average and maximum microseconds. The GX worker measures
XRGB8888-to-tiled-RGB565 conversion and the following texture-cache flush,
emitting one aggregate report every 256 RGB888 frames. Timing uses monotonic
nanoseconds and kernel-safe 64-bit division on 32-bit PowerPC.

Deploy only the checksum-matched module and client over SSH; no card exchange
or kernel replacement is required. Run RGB888 double-buffered for 30 seconds at
30 requested fps. Preserve the visual correctness and clean recovery controls,
but treat this as measurement rather than a rate acceptance test. Use the
client stage totals together with kernel tile/flush timing to account for the
observed approximately 65 ms source-frame interval before selecting an
optimization.

Hardware result: the 30-second run completed 596 RGB888 source presentations
at 19.83 fps. The shorter run was faster than the prior 120-second average but
remained below target. PE finish interrupts advanced by 1,946 at 64.87 per
second; output remained correct, and the user confirmed a clear responsive
console after RGB565 restoration.

Client timing averaged 8,081 us for pattern generation, 17,313 us for the
complete VFB copy, and 24,964 us blocked in pan, totaling approximately 50.36
ms per source frame. The kernel reported XRGB8888-to-tiled-RGB565 conversion at
12,220 us average by frame 768, with a 30,231 us maximum. Texture-cache flush
averaged only 303 us. The worker converted 768 frames while the client produced
596 because the VI callback resubmitted the unchanged selected VFB near 61
times per second.

This identifies redundant conversion as the primary driver-side bottleneck:
12.22 ms multiplied by approximately 61 worker frames consumes roughly 75
percent of the single Broadway CPU before userspace generation/copy work. Add
a source-generation value to the accelerator submission contract. Increment it
for each synchronized pan and skip a queued source generation already handled;
retain per-vblank refresh for one-page fbcon, whose contents can change without
a pan ioctl. Re-profile before optimizing the conversion loop itself.

## 2026-07-30: Stage source-generation deduplication

- Test implementation: `6b9dd096c`
- Kernel zImage SHA-256:
  `230da8d9f5d3d846d26224ea659847d36dd7f34b8ee624c5dd4f36ee4556550f`
- GX module SHA-256:
  `bffdaffe84a26ff0d8a542edc0a123297d3226205516a8b203d6f8812823b04f`
- Timed static workload SHA-256:
  `6cab962d96314a264afc2f464451d5cd44efc21d6677173ee6a2710ca1cc780b`

Add a source generation to the accelerator submission and consumed callback
contract. Synchronized multi-page modes advance it only when
`FBIOPAN_DISPLAY` publishes a source; one-page fbcon advances it every vblank
because applications and console rendering can modify that page without a pan
ioctl. Pan now waits for its exact consumed generation rather than accepting a
stale matching yoffset, including when the same page is submitted repeatedly.

The GX worker records the source pointer, format, and generation only after a
live frame has finished all CPU reads and been submitted. Subsequent VI calls
for that unchanged tuple do not queue another conversion. Diagnostic startup
phases remain exempt until the first live submission, avoiding a deadlock while
waiting for PE initialization. The kernel image and module built cleanly using
`-j16`; diff-only checkpatch reported zero errors and zero warnings.

Deploy the checksum-matched kernel by card and module over SSH. Repeat the timed
RGB888 workload for 30 seconds at 30 requested fps. Success for this profiling
rerun requires correct tear-free output, no generation timeout or kernel fault,
a worker timing-frame count close to client source-frame count rather than PE
interrupt count, materially lower client generation/copy/pan costs from reduced
CPU contention, and clear responsive RGB565 console restoration. If source
rate reaches at least 27 fps, follow immediately with the full 120-second
acceptance run from the identical artifacts.

## 2026-07-30: Source-generation deduplication failed on hardware

- Test commits: `6b9dd096c`, `1c462ce0c`
- Kernel zImage SHA-256:
  `230da8d9f5d3d846d26224ea659847d36dd7f34b8ee624c5dd4f36ee4556550f`
- GX module SHA-256:
  `bffdaffe84a26ff0d8a542edc0a123297d3226205516a8b203d6f8812823b04f`
- Timed static workload SHA-256:
  `6cab962d96314a264afc2f464451d5cd44efc21d6677173ee6a2710ca1cc780b`

Two checksum-identical 30-second RGB888 runs failed the acceptance gate. The
first completed 450 frames in 30.015 seconds, or 14.99 fps. The second visual
confirmation completed 475 frames in 30.013 seconds, or 15.83 fps. Its client
timings were `draw_avg_us=8248`, `copy_avg_us=15138`, and
`pan_avg_us=39781`; kernel RGB888 conversion remained approximately 11.1 ms
per submitted source. The worker timing count advanced with source frames
rather than the roughly 60 Hz VI rate, confirming that generation
deduplication suppressed redundant conversions, but it did not improve source
throughput to the required 27 fps.

The visual confirmation was definitively worse than the pre-deduplication
RGB888 run: the display blanked frames and jittered. Both runs also logged
three consecutive two-second `timed out consuming VFB yoffset 0` warnings
during RGB565 console restoration. The timeout has a concrete one-page race:
`vifb_pan_display()` waits for exact generation equality while the one-page VI
path advances the generation independently every vblank, so the requested
generation can be skipped permanently. More broadly, suppressing unchanged
worker submissions without an explicit retained-frame presentation contract
is not visually safe. Do not advance these artifacts to the 120-second run.

Next, fix one-page pan completion independently and replace the implicit
last-tuple skip with explicit source/presentation ownership. A retained source
must continue to produce stable XFB presentation without rereading or
reconverting its VFB page, and a newly published multi-page generation must be
latched atomically and consumed exactly once.

## 2026-07-30: Stage reloadable GX configuration sweep

- Test implementation: `717caa172`
- Kernel zImage SHA-256:
  `c5b6b8f5b5d731d2958ed06b1e2c26ceddafa77cc62667016138551df76d5d11`
- GX module SHA-256:
  `43592f3e08cd8935b15447d7e989394cab6484c7cdfe09f25967ed754ad1d724`
- Timed static workload SHA-256:
  `6cab962d96314a264afc2f464451d5cd44efc21d6677173ee6a2710ca1cc780b`
- Sweep harness SHA-256:
  `1d67aa614c6d3f2a596ee32374fca7989f8bcb6334c3bd55a89abcef83eb31dc`

Make the rejected source-generation suppression selectable at module load as
`source_dedup=0|1`, defaulting to the previously stable repeated-submission
behavior. The framebuffer core now latches source yoffset and generation under
one lock. One-page pan notifications return immediately instead of waiting for
an exact generation that per-vblank refresh can advance past; multi-page pan
still waits for exact source consumption and only rolls back if no newer
generation has replaced it.

Add `tools/wii-gx-sweep.sh`. It builds once with `-j16`, performs one
checksum-verified upload of the module and workload, reloads named module
parameter combinations, runs isolated timed workloads, records per-candidate
logs, rejects kernel timeouts/fault signatures, optionally records a V4L2 HDMI
feed, ranks technically clean candidates, and restores generated rendering
with deduplication disabled. The existing cycle and stress tools can now
checksum-verify and reuse their remote artifacts, eliminating repeated Wi-Fi
transfers. Bash syntax, ShellCheck, diff-only checkpatch, the kernel image, the
module, and the static workload all validate cleanly.

Deploy this kernel once by SD card and its matching module over SSH. Run the
default 15-second RGB888 matrix at 30 requested fps. It compares the stable
baseline (`source_dedup=0`) with the rejected optimization
(`source_dedup=1`) from identical binaries and restores the baseline
automatically. The apparatus passes only if both candidates execute and leave
the Wii reachable, the baseline is visually correct, no one-page restoration
timeout occurs, and the console returns clear and responsive. Do not promote a
candidate based only on numerical ranking; use full-frame HDMI or direct visual
confirmation for display correctness.

## 2026-07-30: Partial sweep result and harness stdin fix

- Deployed implementation: `717caa172`
- Original harness commit: `717caa172`
- Corrected harness commit: `387d7aba2`
- Corrected harness SHA-256:
  `de66059344f31f8f3e00722b49e560f93620f6cedf16d801f92b6bf6485dad6e`

The first automated run executed only the `source_dedup=0` baseline. It
completed 295 RGB888 frames in 15.000 seconds, or 19.67 fps, with 1,072 PE
finish interrupts (71.47 per second), no kernel fault or generation timeout,
and an average kernel conversion time of 11,930 us by worker frame 256. Client
timings averaged 7,328 us drawing, 16,666 us copying, and 26,725 us waiting in
pan. This remains below the 27 fps acceptance threshold. Direct visual status
was not recorded during this candidate, so the run is not a visual validation.

The dedup candidate did not execute. Child SSH processes inherited the matrix
loop's stdin and consumed its next row. The first ranking also reported an IRQ
delta of zero because it searched for `delta` while the stress report emits
`(delta`. Commit `387d7aba2` redirects inherited child stdin from `/dev/null`,
preserving explicit upload redirections inside the child tools, fixes the
parenthesized IRQ parser, and requires positive PE interrupt progress for a
technically valid result. Bash syntax and ShellCheck pass. No kernel, module,
or workload bytes changed, so no card exchange or target artifact replacement
is required. Repeat the complete default matrix from the current deployed
artifacts and record direct visual behavior for both named candidates.

## 2026-07-30: Complete numerical sweep, host wrapper interrupted cleanup

The corrected harness reached and completed both 15-second candidates from the
same deployed artifacts. Baseline (`source_dedup=0`) completed 222 RGB888
frames in 15.001 seconds, or 14.80 fps, with 1,058 PE finish interrupts
(70.53 per second). Client timing averaged 8,981 us drawing, 20,209 us copying,
and 38,367 us in pan; kernel conversion averaged 12,823 us by worker frame 256.

Deduplication (`source_dedup=1`) completed 225 frames in 15.032 seconds, or
14.97 fps, with 598 PE finish interrupts (39.87 per second). Client timing
averaged 9,658 us drawing, 16,753 us copying, and 40,389 us in pan. Neither run
logged a kernel fault or source-generation timeout. Deduplication materially
reduced PE submissions but did not improve source rate, so it remains rejected
as a throughput optimization even before visual grading.

The external command-execution wrapper terminated the host harness after the
dedup workload completed but before it appended that row, printed ranking, or
ran its EXIT restoration trap. This was not a target or harness control-flow
failure; both preserved candidate logs contain normal completion records. The
baseline module was reloaded manually and registered cleanly, with
`source_dedup=0` confirmed in dmesg. Run future long sweeps in a persistent PTY
and poll them so the wrapper cannot terminate cleanup. The user reported that
baseline and dedup looked approximately the same and that both were visibly
slower than required. There is no visual or numerical reason to enable dedup;
keep the default disabled and optimize the measured RGB888 memory and pan path.

## 2026-07-30: Stage direct-VFB RGB888 workload sweep

- Test implementation: `e50ece1ec`
- Static workload SHA-256:
  `ea878a366f69cb09e286dc81b03ee227d22a9ec65dae3de13fdf6515f646ba23`
- Stress runner SHA-256:
  `c83211c7ad819e374b40568515211b6a27dd161e8c89af07c3b7330be7ffa235`
- Sweep harness SHA-256:
  `f5b7737e259ce4f643fc6e5feac49a8467e7214f3baf800dbdd0e10f4ededed5`

The original stress client constructs every 1.2 MiB RGB888 frame in private
memory and then copies the complete frame into the inactive mapped VFB page.
In the latest baseline those two userspace phases averaged approximately 29 ms
before the driver's approximately 13 ms conversion, making 30 fps impossible
independently of GX scheduling. Add `--direct-render` to construct the
identical pattern directly in the inactive VFB page before synchronized pan.
This preserves page ownership and visual content while removing only the
redundant staging copy. Staged mode remains available as a memory-bandwidth
stress control.

Sweep matrix rows now accept `name|cycle arguments|stress arguments`. The
default matrix runs staged baseline, direct baseline, and direct deduplication.
Run it for 10 seconds per candidate in a persistent host PTY so external tool
timeouts cannot interrupt ranking or baseline restoration. No card exchange or
module replacement is required; only the checksum-verified static workload is
new. A direct baseline at or above 27 fps with correct tear-free output would
show that the driver can sustain the target when an application renders into
its framebuffer pages efficiently. If direct mode remains below 27 fps, use
its timing split to optimize the kernel conversion and pan path next.

## 2026-07-30: Direct VFB rendering reaches the RGB888 target

The checksum-matched 10-second persistent-PTY sweep completed all three rows,
printed ranking, exited zero, and restored `source_dedup=0` automatically:

- Staged baseline: 148 frames in 10.029 seconds, 14.76 fps, 786 PE finish
  interrupts, 9,129 us draw average, 20,244 us copy average, and 38,381 us pan
  average.
- Direct baseline: 284 frames in 10.001 seconds, 28.40 fps, 770 PE finish
  interrupts, 11,323 us draw average, 1 us copy average, and 21,297 us pan
  average. Kernel conversion averaged 11,518 us by worker frame 256.
- Direct dedup: 287 frames in 10.008 seconds, 28.68 fps, 774 PE finish
  interrupts, 11,095 us draw average, 1 us copy average, and 22,047 us pan
  average. Kernel conversion averaged 11,147 us by worker frame 256.

Every candidate had positive PE progress, zero screened fault signatures, and
normal remote workload completion. Removing the redundant private-buffer copy
nearly doubled source throughput and put both direct modes above the 27 fps
acceptance threshold. Deduplication again made no material difference, so keep
it disabled. The driver can sustain the target when userspace renders directly
into the inactive VFB page; the staged workload measures application memory
traffic rather than a driver throughput limit.

The three candidates transitioned too quickly for reliable visual grading of
direct deduplication. Repeat only `source_dedup=1 --direct-render` for 30
seconds from the same kernel, module, and workload hashes. Require correct
colors and geometry, no blanking or tearing, normal completion, and clear
responsive baseline-console restoration before treating the direct result as
visually accepted.

## 2026-07-30: Reject direct deduplication on visual quality

The isolated 30-second direct-dedup run completed normally and sustained 861
frames in 30.033 seconds, or 28.67 fps. It recorded 1,884 PE finish interrupts
(62.80 per second), 10,875 us average direct draw time, 1 us average copy time,
22,074 us average pan time, and approximately 11.1 ms average kernel conversion
across worker frames 256, 512, and 768. No kernel timeout or screened fault
occurred.

The user reported that the displayed image quality was terrible. This is a
definitive visual rejection despite the harness's technical pass; numerical
checks intentionally do not claim image correctness. The harness completed and
restored baseline automatically. `/sys/module/gcn_gx/parameters/source_dedup`
read `N`, and dmesg confirmed a clean unload followed by generated rendering
with `source_dedup=0`.

Keep deduplication disabled. Repeat only baseline
`source_dedup=0 --direct-render` for 30 seconds from the same artifacts. It must
retain at least 27 fps while showing correct colors and geometry without the
quality failure, then restore a clear responsive console. If baseline-direct
passes, promote that result rather than either dedup variant.

## 2026-07-30: Baseline-direct passes isolated RGB888 validation

The isolated 30-second baseline-direct run completed 856 frames in 30.016
seconds, or 28.52 fps, with `source_dedup=0`. It recorded 1,970 PE finish
interrupts (65.67 per second), 10,839 us average direct draw time, effectively
zero copy time, and 20,915 us average pan time. Kernel conversion remained
stable near 11.5 ms through worker frames 256, 512, and 768. No kernel timeout
or screened fault occurred.

The user confirmed that this candidate looked fine and that the restored
console was clear and responsive. This passes the isolated throughput, visual,
and recovery gate and demonstrates that the RGB888 path sustains the 27 fps
target when applications render directly into the inactive VFB page. The
deduplication parameter remains disabled.

Promote the identical deployed kernel/module and static workload to a
120-second baseline-direct acceptance run at 30 requested fps. Require at
least 27 fps, correct tear-free output throughout, positive PE progress, no
kernel timeout or fault, automatic `source_dedup=0` restoration, and a clear
responsive console after completion.

## 2026-07-30: Baseline-direct passes 120-second RGB888 acceptance

The promoted baseline-direct candidate completed 3,367 frames in 120.047
seconds, or 28.05 fps, with `source_dedup=0`. It recorded 7,296 PE finish
interrupts (60.80 per second), 11,173 us average direct draw time, effectively
zero copy time, and 22,048 us average pan time. Kernel conversion converged to
11,691 us average by worker frame 3,328; its maximum was 36,963 us. Texture
flush averaged 267 us with an 8,362 us maximum.

The workload exited normally and the harness reported zero screened fault
signatures. No source-generation timeout or kernel fault appeared in the test
log. The user reported that output seemed good and that the restored console
was clear. A final live-state check showed
`/sys/module/gcn_gx/parameters/source_dedup` as `N` and the `gcn_gx` module
loaded normally.

This passes the 120-second RGB888 throughput, PE-progress, visual, fault, and
recovery criteria. The accelerated framebuffer path can sustain the target
when userspace renders directly into the inactive VFB page and synchronizes
with `FBIOPAN_DISPLAY`. Keep source deduplication disabled; it provides no
throughput gain and was visually unacceptable. The staged-copy result is an
application memory-traffic limitation, not a GX driver throughput failure.

## 2026-07-30: Stage removal of rejected source deduplication

- Test implementation: `c7bd62a0a`
- Kernel zImage SHA-256:
  `c5b6b8f5b5d731d2958ed06b1e2c26ceddafa77cc62667016138551df76d5d11`
- GX module SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`
- Cycle script SHA-256:
  `9a69fa50b462c900ac7fb2a8ffbdfcab9f9f00028404f28c5a5315060039ad39`
- Sweep script SHA-256:
  `937511148f8b9a3e3841a708841d2ef3bcdb8018e06f997115a06861bdd075a4`

Remove the visually rejected `source_dedup` module parameter and its
last-source tracking from the driver. Remove the corresponding cycle and sweep
controls so an obsolete experimental path cannot be enabled accidentally.
Source-generation ownership and consumption callbacks remain unchanged; they
are required to prevent userspace from reusing a VFB page while GX still reads
it.

The zImage and module ABI are unchanged, so this test requires only a live
upload and reload of the checksum-matched module. First run a 30-second RGB565
direct-render regression, then record and commit its numerical and full-frame
visual result before testing RGB888. Follow with a separate 30-second RGB888
direct-render regression. Both formats must complete normally with positive PE
progress, no source-generation timeout or kernel fault, correct stable output,
and a clear responsive console after the baseline module is restored.

## 2026-07-30: Cleaned module passes RGB565 regression

The checksum-matched module loaded normally from implementation `c7bd62a0a`.
The removed `source_dedup` parameter was absent from both the live sysfs module
parameter directory and `modinfo`; `/tmp/gcn-gx.ko` on the Wii matched the
staged SHA-256
`88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`.

The isolated 30-second direct-render RGB565 workload completed 895 frames in
30.018 seconds, or 29.82 fps against a requested 30 fps. PE finish interrupts
advanced from 43,127 to 45,093, a delta of 1,966 or 65.53 per second. Direct
draw time averaged 5,419 us with a 17,098 us maximum; pan time averaged
18,410 us with a 42,352 us maximum. The workload exited zero and logged no GX
timeout, source-generation timeout, or kernel fault.

The user confirmed that the full-frame output looked correct and that the
restored console was nominal. RGB565 therefore passes numerical, PE-progress,
visual, fault, and recovery gates. Proceed with the separately staged
30-second RGB888 direct-render regression using the same kernel, cleaned
module, and static workload hashes.

## 2026-07-30: Cleaned module passes RGB888 regression

The separate 30-second direct-render RGB888 workload completed 839 frames in
30.014 seconds, or 27.95 fps against a requested 30 fps. This remains above
the established 27 fps acceptance floor. PE finish interrupts advanced from
61,237 to 63,203, a delta of 1,966 or 65.53 per second. Direct draw time
averaged 11,257 us with a 33,916 us maximum; pan time averaged 22,531 us with
a 62,305 us maximum. Kernel conversion remained stable at approximately
11.9 ms through worker frames 256, 512, and 768.

The workload exited zero and logged no GX timeout, source-generation timeout,
or kernel fault. The user confirmed that the full-frame pattern remained
visually correct and that the restored console was clear and responsive.
RGB888 therefore passes numerical, PE-progress, visual, fault, and recovery
gates.

Together with the preceding RGB565 result, this accepts the source-dedup
removal as the hardened accelerated-framebuffer baseline. Preserve this point
on `feature/wii-6.18-gx-port` before starting DRM/KMS work on a separate branch.

## 2026-07-30: Stage initial DRM/KMS handoff test

- Driver implementation: `a3be82c15`
- Reversible cycle harness: `8ee824b2b`
- DRM-enabled zImage SHA-256:
  `9260b832144d3846f9a3a4793b5357d626f61a32ef1b82497d50a9b61da45804`
- `drm_client_lib.ko` SHA-256:
  `3ab425eed71096a01e1090a9544debff2a3f7162e2fda35eb1d2e04afad8abc1`
- `drm_kms_helper.ko` SHA-256:
  `7a6ffc191f0cd3fb7b4dee94edeedccf9d3a1e51168c1e34b4625d39bb068865`
- `drm_shmem_helper.ko` SHA-256:
  `ee436abd8d0fc7f3ae26347917800ada03ffe4bca395878c4751d1f6c03f242a`
- `gcn-drm.ko` SHA-256:
  `0729449d311c30af612a42b24cc77aef75b2d738b6656a155e56af886007cf60`
- Cycle harness SHA-256:
  `c716251d2f5937ed27d31d1463bb5749033ad68e051ce7bd80d93dc67d3fc87b`

This is the first DRM/KMS hardware test. The kernel contains built-in DRM core
while retaining built-in `gcnfb`; the KMS, shmem, client, and Wii VI drivers
remain modules. Boot must first reach the accepted `gcnfb` console and SSH.
The cycle harness then checksum-verifies every uploaded module, unloads GX,
unbinds `gcnfb`, loads the DRM dependency set, and binds `gcn-drm` to
`c002000.video`. Any failed transition automatically unloads DRM in reverse
order and rebinds `gcnfb`.

Acceptance requires `/sys/class/drm/card0`, the `gcn-vi` platform binding, a
new DRM fbdev console, continuing SSH access, and no kernel fault. Capture the
HDMI output after binding and open the PNG in GIMP for visual grading; do not
infer image correctness from registration logs. Finally run the explicit
restore path and require the accepted clear, responsive `gcnfb`/GX console.

## 2026-07-31: Reject built-in DRM image at boot-wrapper boundary

The checksum-verified zImage
`9260b832144d3846f9a3a4793b5357d626f61a32ef1b82497d50a9b61da45804`
did not enter the kernel. Gumboot displayed `loading zImage.ngx` and the screen
then remained frozen. No kernel console output, SSH, or target log was
available, so this result says nothing about the `gcn-drm` driver itself.

The corresponding build emitted a new boot-wrapper layout warning: the
uncompressed kernel size was `0x012c8be0`, overlapping the wrapper at
`0x00600000`, and Kbuild moved the wrapper link address to `0x01300000`.
The accepted pre-DRM image did not exhibit this failed runtime behavior. Treat
crossing this boot-layout boundary as the leading explanation, but as an
inference rather than a proven root cause because execution produced no serial
log.

Restore the exact accepted
`c5b6b8f5b5d731d2958ed06b1e2c26ceddafa77cc62667016138551df76d5d11`
zImage before further testing. Reconfigure DRM core as a module along with the
KMS, shmem, client, and Wii VI modules. Build the smaller dependency-support
kernel first, then build and upload the complete module set. Do not retry the
built-in DRM image.

## 2026-07-31: Stage modular DRM/KMS boot and handoff test

- Modular-stack implementation: `a101ec91c`
- Modular DRM zImage size: `6297232` bytes
- Accepted GX zImage size: `6260764` bytes
- Modular DRM zImage SHA-256:
  `eedd96b4c140ff931848f7e275bef40332ac549a16567c6df527e67bf2cde6c5`
- `drm_panel_orientation_quirks.ko` SHA-256:
  `fa1e9862ec9379b26b572df3b4e93878f21e563264ad6fd8353a4f27dcadd1df`
- `drm.ko` SHA-256:
  `038eda5366251f715648d8fea770cdb2f19ec45ccc86104a14824f43ca5e65ce`
- `drm_client_lib.ko` SHA-256:
  `cdb90dc4d66ea83c7d28bd31a66b16ad3fdd62bb5fc027387b3ebd2bd933fa39`
- `drm_kms_helper.ko` SHA-256:
  `b3bb89745cd925a092d7a8c249a008c0d1f4d5d49cee80ee857c516d95dfea2f`
- `drm_shmem_helper.ko` SHA-256:
  `9acfcdf4cb2c9d34e0f42fbc15fad0e6bf08f5793d22fe7be99e478948b03d07`
- `gcn-drm.ko` SHA-256:
  `7ea073c9d2c009f494037883b970371003066b40a0fd77c3b567cf4191bef1a2`
- Cycle harness SHA-256:
  `e4b7e4933c308786c73894da0a95a41fefc55a4d1a59b087dd48fc3cbd994b91`

The accepted rollback image was restored and booted normally, independently
confirming that the card and Gumboot path remain sound. This new image keeps
DRM core and every helper modular; only their selected DMA-buf, fence, HDMI,
and related support remains built into the kernel. The resulting zImage is
only 36,468 bytes larger than the accepted image. Its uncompressed kernel is
`0x00ec7568`, and Kbuild places the wrapper at `0x00f00000`, both materially
below the rejected built-in layout (`0x012c8be0` and `0x01300000`).

The revised harness uploads and checksum-verifies all six modules. It loads
the generic orientation, DRM core, client, KMS, and shmem modules while
`gcnfb` still owns the screen. Only after that non-disruptive preflight passes
does it unload GX, unbind `gcnfb`, and load `gcn-drm`. A preflight failure must
leave the accepted display untouched. A transition failure must restore
`gcnfb` automatically.

First acceptance gate is boot only: require normal Gumboot completion, legacy
console output, and SSH. Do not begin the live handoff until that result is
recorded and committed. The subsequent handoff retains the previously defined
card0, binding, HDMI/GIMP visual, SSH, fault, and explicit-restore gates.

## 2026-07-31: Modular DRM kernel passes boot gate

The checksum-verified modular DRM zImage
`eedd96b4c140ff931848f7e275bef40332ac549a16567c6df527e67bf2cde6c5`
completed Gumboot and normal kernel startup. It reached the legacy console,
acquired `10.3.10.12`, and accepted SSH. The target reported
`6.18.40-wii+ #29`; `gcn-vifb` owned `c002000.video`, `gcn_gx` loaded, and the
generated GX renderer registered normally.

Early boot emitted the already documented PowerPC alignment warning from
`memset()` through `dma_alloc_from_dev_coherent()` during `ohci_setup()`. The
same warning predates this DRM work, and execution continued through USB,
Wi-Fi, graphics, and SSH. No new panic, machine check, or DRM-related fault
appeared.

This accepts the boot-only gate and rules out the modular image as having the
built-in image's boot-wrapper failure. Proceed to the separately committed
live handoff: upload and preflight all generic DRM modules while legacy output
remains active, then transition the VI to `gcn-drm`.

## 2026-07-31: Modular fbdev-emulation preflight stops safely

The checksum-pinned cycle uploaded all six modules and successfully loaded
`drm_panel_orientation_quirks.ko` and `drm.ko` while `gcnfb` and GX remained
active. Loading `drm_client_lib.ko` then failed with unresolved fb-helper
symbols including `drm_fb_helper_init`, `drm_fb_helper_lastclose`, and
`drm_helper_disable_unused_functions`. Those symbols are exported by
`drm_kms_helper.ko`, while that module in this configuration also imports
client symbols from `drm_client_lib.ko`. The fully modular fbdev-emulation
configuration therefore has a circular runtime load dependency.

This was a successful safety negative control: the harness had not marked the
display transition as started, did not unload GX, and did not unbind `gcnfb`.
The accepted console and SSH remained active. No DRM VI code ran, so this is
not a KMS rendering result.

Remove fbdev emulation and the default DRM client from the first handoff
milestone. Keep DRM core, KMS helper, shmem helper, and `gcn-drm` modular, and
validate scanout with a dedicated dumb-buffer KMS test client. Revisit fbdev
console support after basic atomic modesetting and vblank operation are proven;
do not force the circular module set into the boot image merely to obtain a
console.

## 2026-07-31: Stage no-fbdev modular DRM/KMS handoff

- No-fbdev implementation: `c0146e5c2`
- Running modular-support zImage SHA-256:
  `eedd96b4c140ff931848f7e275bef40332ac549a16567c6df527e67bf2cde6c5`
- Rebuilt no-fbdev zImage SHA-256 (not deployed for this module-only test):
  `923f94547fac0cca13b05c02303fc6c8c0d0cf3d11f99598c1d285229c5eac5d`
- `drm_panel_orientation_quirks.ko` SHA-256:
  `fa1e9862ec9379b26b572df3b4e93878f21e563264ad6fd8353a4f27dcadd1df`
- `drm.ko` SHA-256:
  `8dc6380087638e48c13aef507c983457c511ab7ea1f31fa69c87a9b1ffa3acd7`
- `drm_kms_helper.ko` SHA-256:
  `13563e78b9ecba7a907446fe1747f82b1a354c1c4a55a17c8a41198d38e3765b`
- `drm_shmem_helper.ko` SHA-256:
  `b7166b9ee61e88651119766f76979ed7891f748aa8b94330455071f3e86d86a8`
- `gcn-drm.ko` SHA-256:
  `0a3d86c3ee1d5adf30162560ee49f02a89b470ee3db836fda4b21e6eebe5fca4`
- Cycle harness SHA-256:
  `15d9af14725daea89615c04ecf86c616196dea00654d2fcbe9eba17542c85036`

This configuration removes `DRM_CLIENT_SELECTION`, DRM fbdev emulation, and
the default DRM client from the first KMS milestone. The resulting module
dependency chain is acyclic: `drm_kms_helper.ko` no longer imports fb-helper or
client-library symbols, and `gcn-drm.ko` depends only on DRM core, KMS helper,
and shmem helper. The rebuilt modules have matching `6.18.40-wii+` vermagic, so
the already booted checksum-accepted modular-support kernel can run this test
without another card exchange.

The cycle harness must first load all generic dependencies while `gcnfb` owns
the VI. It may unload GX and unbind `gcnfb` only after that preflight passes.
Acceptance requires `gcn-vi` to bind `c002000.video`, `/sys/class/drm/card0` to
exist, SSH to remain responsive, and no kernel fault. No DRM fbdev client is
present, so the display is expected to retain or freeze its last legacy frame;
that is not a scanout verdict. Visible DRM output will be evaluated separately
with a dedicated dumb-buffer KMS test client and a full-frame HDMI capture.

## 2026-07-31: Reject first no-fbdev handoff due to VI IRQ storm

The checksum-pinned `d4a1a0308` cycle passed generic-module preflight, unloaded
GX, and unbound legacy `gcnfb`. The last visible status line was
`drm-cycle: loading gcn-drm d4a1a0308a5c`. The machine then froze completely:
the displayed frame stopped, SSH disappeared, and the host could no longer
ping `10.3.10.12`. The harness could not execute its rollback because the
kernel was no longer scheduling network or userspace work. No `card0` success
marker appeared.

Static comparison with the established `gcnfb` VI handler identifies a direct
interrupt-acknowledge bug. VI DI status bit 31 is cleared by writing zero;
`gcnfb` acknowledges with `vi_dix_clear_irq(value)`. The new DRM handler and
vblank helpers instead write `value | VI_DI_IRQ`, preserving the asserted
status bit. Legacy teardown also frees the IRQ without disabling DI0/DI1, so
the new driver's `request_irq()` can immediately receive the still-enabled
source and loop forever without clearing it. This exactly fits the observed
hard freeze, but remains a root-cause hypothesis until a corrected build binds.

Next test: clear bit 31 in every DI acknowledge, and quiesce all four DI sources
before requesting the IRQ. Keep scanout and userspace modesetting out of this
test; acceptance is limited to a responsive machine, `gcn-vi` binding, and
`/sys/class/drm/card0` registration.

## 2026-07-31: Stage corrected VI IRQ handoff test

- Interrupt-fix implementation: `61c4a2498`
- `gcn-drm.ko` SHA-256:
  `a1b5538d4bdaacb31c7ff6f7ff4326d56d54f5ca7d4ba0a253f972e8ebead2a0`
- `drm_panel_orientation_quirks.ko` SHA-256:
  `fa1e9862ec9379b26b572df3b4e93878f21e563264ad6fd8353a4f27dcadd1df`
- `drm.ko` SHA-256:
  `8dc6380087638e48c13aef507c983457c511ab7ea1f31fa69c87a9b1ffa3acd7`
- `drm_kms_helper.ko` SHA-256:
  `13563e78b9ecba7a907446fe1747f82b1a354c1c4a55a17c8a41198d38e3765b`
- `drm_shmem_helper.ko` SHA-256:
  `b7166b9ee61e88651119766f76979ed7891f748aa8b94330455071f3e86d86a8`
- Cycle harness SHA-256:
  `15d9af14725daea89615c04ecf86c616196dea00654d2fcbe9eba17542c85036`

Only `gcn-drm.ko` changed from the rejected test. It now disables every VI DI
source before requesting the IRQ, preserves the programmed timing coordinates,
and clears asserted status by writing bit 31 as zero. Probe-stage messages were
added after allocation, mapping, mode-object setup, vblank setup, interrupt
quiesce, and IRQ installation.

After a cold reboot restores the accepted modular-support kernel and legacy
console, rerun the normal cycle with fresh uploads. Acceptance requires the
cycle to return normally, `gcn-vi` to own `c002000.video`, `card0` to exist,
SSH and ping to remain responsive for at least 30 seconds, and no kernel fault.
Do not grade the frozen legacy frame: no framebuffer client or KMS test buffer
is active in this milestone.

## 2026-07-31: Accept corrected VI IRQ handoff and card0 registration

The checksum-pinned `c390a0dca` cycle completed normally with
`gcn-drm.ko` SHA-256
`a1b5538d4bdaacb31c7ff6f7ff4326d56d54f5ca7d4ba0a253f972e8ebead2a0`.
Every probe-stage marker appeared: DRM allocation, VI/XFB mapping, mode-object
initialization, vblank initialization, VI interrupt quiesce, and IRQ install.
DRM core then registered `gcn-vi 1.0.0` on minor 0 and reported the fixed
640x480 handoff mode with XFB reservation `01698000+00168000`.

The display stopped changing after legacy `gcnfb` was unbound, which initially
looked like another crash. This was the expected no-fbdev result, not a machine
failure. After a 30-second soak, all three ICMP requests succeeded, SSH remained
responsive, `c002000.video` was still bound to `gcn-vi`, and `/sys/class/drm`
contained `card0` and `card0-Composite-1`. Uptime continued advancing and the
fault scan found no BUG, Oops, panic, machine check, unhandled access, or
watchdog report.

This validates the VI interrupt correction and accepts the first modular DRM
probe/registration milestone. Keep `gcn-drm` active. Next, run a dedicated
dumb-buffer KMS client that creates a 640x480 XRGB8888 framebuffer, draws a
deterministic full-frame pattern, and performs the first userspace modeset.
Only HDMI capture and direct visual inspection can accept scanout correctness.

## 2026-07-31: Stage first userspace dumb-buffer modeset

- Test-client implementation: `1393a3759`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-test` SHA-256:
  `cfff8537d0cc2ce455296ac2a30160d18f1d3c9f816bdf7a479d90fa1fcdca7a`
- Active `gcn-drm.ko` SHA-256:
  `a1b5538d4bdaacb31c7ff6f7ff4326d56d54f5ca7d4ba0a253f972e8ebead2a0`

Build command:

```sh
powerpc-linux-gnu-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -static -s \
  -I/usr/include/libdrm -o /tmp/wii-drm-test tools/wii-drm-test.c
```

The client uses raw DRM UAPI ioctls and links no libdrm runtime dependency. It
discovers the fixed connector and CRTC, creates a 640x480 32-bpp dumb buffer,
maps it, fills XRGB8888 pixels, adds a framebuffer, and issues `SETCRTC`. The
pattern contains red, green, blue, and white quadrants, black 80x60 grid lines,
an eight-pixel white border, and a cyan/magenta center checkerboard. It remains
alive after modeset so scanout can be inspected.

Upload to `/tmp/wii-drm-test`, verify the remote checksum, and launch it while
the accepted no-fbdev `gcn-drm` instance owns `card0`. Acceptance requires a
successful client status line, a live process, continuing SSH/ping, no kernel
fault, and a full-frame HDMI capture matching the described pattern. Any
kernel-only success remains provisional until the visible frame is graded.

## 2026-07-31: First KMS client exits before modeset on msync

The checksum-verified test binary launched while `gcn-vi` remained bound, but
exited with `msync dumb buffer: Invalid argument`. The process was no longer
alive after three seconds. The driver, `card0`, SSH, and the machine remained
healthy, and no kernel fault appeared. Because the client treats `msync()` as
fatal before `ADDFB` and `SETCRTC`, this run did not test scanout and the frozen
legacy frame was expected to remain unchanged.

A DRM dumb-buffer mapping does not require userspace `msync()` before the
modeset; the driver's GEM CPU-access hooks provide the relevant synchronization
when it reads the shmem framebuffer. Remove the unnecessary `msync` call and
repeat with a newly committed and checksum-pinned binary. Do not change the
pattern, DRM ioctl sequence, or kernel module for that retry.

## 2026-07-31: Stage msync-free KMS modeset retry

- Client fix: `a8cedcc54`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-test` SHA-256:
  `471e18e9f41997c74590f91a134e59483d1faef5eef43f8dc48c52b798ec1811`

Only the unsupported `msync()` call was removed. Reuse the currently active
and accepted `gcn-drm` instance; no reboot or module cycle is required. Replace
the remote client only after checksum verification, then launch it and require
the `active 640x480` status line plus a live process. Apply the same liveness,
fault, and full-frame visual gates defined for the first attempt.

## 2026-07-31: Accept first DRM/KMS userspace scanout

The checksum-verified msync-free client completed its ioctl sequence and
reported:

```text
wii-drm-test: active 640x480 640x480 crtc=36 connector=33
wii-drm-test: fb=38 handle=1 pitch=2560 size=1228800
```

The process remained alive, `gcn-vi` retained the platform binding, and the
30-second soak completed with three of three ICMP replies, continuing SSH and
uptime, and no conversion failure, BUG, Oops, panic, machine check, unhandled
access, or watchdog report.

Direct visual inspection passed. The displayed frame was clear and showed all
four color quadrants with the black grid and the teal/pink center checkerboard
overlay, matching the deterministic XRGB8888 source pattern. The HDMI capture
device was not connected, so no PNG artifact exists for this run; acceptance
is based on the user's direct full-frame observation rather than sampled XFB
values or kernel logs.

This accepts dumb-buffer allocation/mapping, XRGB8888 framebuffer creation,
legacy `SETCRTC` through the atomic helper path, CPU XRGB8888-to-YUYV
conversion, XFB programming, and stable VI scanout. Next milestones are a
reversible return to legacy `gcnfb`, then repeated page flips with vblank events
to validate frame updates and synchronization before adding DRM fbdev console
support.

## 2026-07-31: Stage post-modeset legacy restore

Terminate the active `wii-drm-test` process first so it removes framebuffer 38,
destroys dumb-buffer handle 1, closes `card0`, and releases DRM master. Then run
the committed cycle harness `--restore --no-build`. Acceptance requires
`gcn_drm` and generic DRM modules to unload, `gcn-vifb` to rebind
`c002000.video`, `gcn_gx` to reload with the accepted generated renderer, the
CPU/GX console to resume clearly and responsively, and SSH to remain available.
Record any module-use, teardown, rebind, or visual failure before page-flip
development.

## 2026-07-31: Accept post-modeset legacy restore

The KMS client terminated cleanly and released DRM master with `gcn_drm` at
module use count zero. The committed restore path unloaded the complete DRM
stack and rebound `gcn-vifb` to `c002000.video`; the legacy console returned
clear. The harness could not initially reload GX because `/tmp/gcn-gx.ko` was
absent, exposing an automation gap rather than a driver failure.

The exact accepted `gcn-gx.ko` artifact with SHA-256
`88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`
was uploaded, verified remotely, and loaded with `renderer=generated` and
`texel_bias_eighths=-2`. `gcnfb` reported accelerator registration, SSH stayed
responsive, no kernel fault appeared, and direct observation confirmed that
the accelerated console remained clear.

This accepts reversible DRM-to-fbdev ownership transfer after a real modeset.
Fix the cycle harness to upload and verify `gcn-gx.ko` alongside the DRM module
set so rollback never depends on a pre-existing temporary file.

## 2026-07-31: Stage self-contained restore-harness test

- Harness implementation: `5ba5cbf5d`
- Cycle harness SHA-256:
  `65914374d9a44b371c14c11a42ec994d7ebf346345452228cfcf5c773bcafbd6`
- Accepted `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Run `--restore --no-build` against the already restored Wii. The harness must
upload and remotely verify the local GX module before entering restore logic.
Because `gcn-vifb` and `gcn_gx` are already active, the operation must be
idempotent: preserve their binding/module state, clear console, SSH access, and
fault-free kernel. This validates artifact availability for future automatic
rollback without performing another DRM transition.

## 2026-07-31: Accept self-contained restore harness

The checksum-pinned restore-only run uploaded and remotely verified
`gcn-gx.ko` as
`88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`,
then completed normally. `gcn-vifb` remained bound to `c002000.video`,
`gcn_gx` remained the only matching graphics module loaded, SSH stayed
responsive, and no kernel fault signature appeared. Direct observation
confirmed the console remained clear throughout the idempotent restore.

This accepts the artifact-upload fix and makes both normal rollback and
restore-only operation independent of prior `/tmp` contents. Proceed to the
two-buffer vblank/page-flip milestone.

## 2026-07-31: Stage first vblank-synchronized page-flip test

- Page-flip client implementation: `ac1957769`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-test` SHA-256:
  `c4f81f793ca7bdb96909db32da51bafd300d06611289b790eecbbe6693bdefc0`
- Cycle harness SHA-256:
  `65914374d9a44b371c14c11a42ec994d7ebf346345452228cfcf5c773bcafbd6`
- `gcn-drm.ko` SHA-256:
  `a1b5538d4bdaacb31c7ff6f7ff4326d56d54f5ca7d4ba0a253f972e8ebead2a0`
- Accepted `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Use the self-contained cycle harness to transition from the accepted legacy
console to `gcn-drm`, then remotely verify and launch:

```sh
/tmp/wii-drm-test --flips 20 --delay-ms 500
```

The initial frame and both flip buffers retain the accepted quadrant, grid,
border, and center-checker pattern. Buffer 0 adds a yellow vertical marker near
the left; buffer 1 adds one near the right. Each page-flip ioctl requests an
event and the client refuses to submit the next flip until it receives a valid
`DRM_EVENT_FLIP_COMPLETE` with the exact expected user-data value. The timeout
is two seconds per event.

Acceptance requires `flips=20` with a nonzero final vblank sequence, a live
client holding the final frame, responsive SSH/ping, and no kernel fault or
conversion failure. Direct observation must show a clear base pattern with the
yellow marker alternating left/right at roughly two positions per second and
must note any tearing, corruption, missed transition, or instability.

## 2026-07-31: Accept vblank-synchronized page flips

The checksum-pinned client created two 640x480 XRGB8888 framebuffers with
2560-byte pitch and completed all 20 requested page flips. Every submission
received and validated its matching `DRM_EVENT_FLIP_COMPLETE`; the client
reported `flips=20 last-vblank=308` and remained alive holding the final frame.

The 30-second soak passed with three of three ICMP replies, responsive SSH,
continuing uptime, `gcn-vi` ownership, and no conversion failure, BUG, Oops,
panic, machine check, unhandled access, or watchdog report. Direct observation
confirmed the base image remained correct and the yellow marker alternated as
intended, with no visible tearing, corruption, missed movement, or instability.

This accepts the driver's pending-page handoff, DI1 vblank handling, DRM vblank
accounting, page-flip event arming/delivery, repeated CPU conversion, and
double-buffered XFB scanout. Keep the current module and client artifacts as
the baseline for a sustained zero-delay flip stress test.

## 2026-07-31: Stage sustained zero-delay page-flip stress

Reuse the accepted `gcn-drm.ko` and page-flip client artifacts without a module
reload. Terminate the current holding client, confirm `gcn_drm` returns to use
count zero, then launch:

```sh
/tmp/wii-drm-test --flips 300 --delay-ms 0
```

The test still serializes every submission behind its validated completion
event; zero delay means conversion and vblank are the only pacing mechanisms.
Acceptance requires exactly 300 flips, a nonzero advancing final vblank
sequence, a live final frame, continuing SSH/ping, no kernel or conversion
fault, and no visual blanking, corruption, loss of sync, or persistent tearing.
Rapid left/right marker alternation may appear as flicker and is not itself a
failure.

## 2026-07-31: Accept sustained zero-delay page-flip stress

The checksum-pinned client completed all 300 serialized zero-delay page flips
and reported `flips=300 last-vblank=776`. Completion was already present at the
20-second check; the process remained alive holding the final frame. The
subsequent 30-second soak passed with three of three ICMP replies, responsive
SSH, advancing uptime, retained `gcn-vi` ownership, and no conversion failure
or kernel fault signature.

Direct observation confirmed the full base pattern stayed visually correct
throughout the stress run, with no blanking, corruption, loss of sync,
persistent tearing, or instability. This accepts sustained event-serialized
page flipping and repeated full-frame CPU conversion on the current KMS path.

Next validate the second advertised primary-plane format, RGB565, with the same
deterministic static pattern and synchronized page-flip gates before declaring
the initial userspace format contract complete.

## 2026-07-31: Stage RGB565 scanout and page-flip validation

- RGB565 client implementation: `937321f9f`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-test` SHA-256:
  `d2aa7acc2fc097fb695d06b318543725c01ed39c5c6b53745a07849229430b50`

Reuse the active accepted DRM module without reloading it. Terminate the holding
XRGB8888 stress client, verify DRM master release, remotely checksum-replace the
test binary, and run:

```sh
/tmp/wii-drm-test --format rgb565 --flips 20 --delay-ms 500
```

The client must report `format=rgb565`, two 16-bpp buffers with 1280-byte pitch,
20 validated completion events, and a nonzero final vblank sequence. Require a
live final frame, responsive SSH/ping, and no conversion or kernel fault.
Direct observation must show the same clear quadrant/grid/checker pattern and
left/right yellow-marker alternation as the accepted XRGB8888 test, allowing
only normal RGB565 color quantization and no channel swaps, pitch errors,
tearing, corruption, or instability.

## 2026-07-31: First RGB565 run passes technically, visual result provisional

The checksum-pinned client reported `format=rgb565`, two 614400-byte buffers
with the required 1280-byte pitch, and `flips=20 last-vblank=1237`. The client
remained alive, all three post-test pings succeeded, SSH and `gcn-vi` ownership
remained stable, and no conversion failure or kernel fault appeared.

The user reported that everything looked good, but requested another run to be
sure. Accept the allocation, format selection, conversion, event, and stability
gates. Keep final visual acceptance provisional until an identical independent
confirmation explicitly verifies colors, geometry, checker/grid clarity,
marker motion, tearing, and corruption.

## 2026-07-31: Stage independent RGB565 confirmation run

Repeat the exact checksum-pinned RGB565 test from commit `51150fead` with no
source, binary, module, or parameter changes:

```sh
/tmp/wii-drm-test --format rgb565 --flips 20 --delay-ms 500
```

Require the same 1280-byte pitch, 20 completion events, live final frame, and
stable network/kernel state. The visual verdict must explicitly cover colors,
geometry, checker/grid clarity, marker motion, tearing, and corruption.

## 2026-07-31: Accept RGB565 scanout and synchronized flips

The independent confirmation reused the exact binary SHA-256
`d2aa7acc2fc097fb695d06b318543725c01ed39c5c6b53745a07849229430b50`
and unchanged parameters. It again reported `format=rgb565`, two 614400-byte
buffers with 1280-byte pitch, and all 20 completion events, this time through
vblank sequence 1701. The client remained alive and the final 30-second soak
passed with three of three ICMP replies, responsive SSH, retained `gcn-vi`
ownership, and no conversion or kernel fault.

Direct observation explicitly confirmed the repeated RGB565 run looked great:
colors and geometry were correct, the quadrant/grid/checker pattern remained
clear, marker motion was correct, and no channel swap, pitch distortion,
tearing, corruption, or instability was visible.

This accepts RGB565 dumb-buffer modeset, conversion, synchronized page flips,
and stable scanout. Both formats advertised by the primary plane, XRGB8888 and
RGB565, now have independent hardware validation.

## 2026-07-31: Stage opt-in standalone NTSC 480i programming

- VI programming implementation: `9dfa1d88a`
- Cycle-harness implementation: `051ff026a`
- `gcn-drm.ko` SHA-256:
  `a5c88ed9763dfab30899d4630551860e1928f48dbabca72b0d8c1966033077bd`
- Cycle harness SHA-256:
  `6941c264720fd8a3c2560913d37c61fe8921198f591af9f03476df572fe45a74`
- `wii-drm-test` SHA-256:
  `d2aa7acc2fc097fb695d06b318543725c01ed39c5c6b53745a07849229430b50`
- Accepted `gcn-gx.ko` rollback SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Run the reversible cycle harness with `--program-mode --no-build`. This keeps
the previously accepted inherited-mode path as the module default while asking
this test load to quiesce VI interrupts, disable VI, program the complete fixed
640x480 NTSC interlaced timing/filter/stride state derived from `gcnfb`, clear
both XFB pages to legal YUYV black, install the field addresses and retrace
coordinates, and re-enable VI. AVE encoder state remains inherited for this
first isolated test.

Require the kernel readback to report `DCR=0001`, `VTR=0f06`,
`HTR0=476901ad`, `HTR1=02e850c0`, and `PCR=2850`, followed by normal `card0`
registration. Then run the accepted XRGB8888 client:

```sh
/tmp/wii-drm-test --flips 20 --delay-ms 500
```

Acceptance requires all 20 validated page-flip events, responsive SSH/ping,
no conversion or kernel fault, and direct observation of the clear established
quadrant/grid/checker pattern with correct marker movement and no loss of sync,
tearing, corruption, or instability. Finally run the committed restore path
and require the clear, responsive legacy GX console to return. This test proves
that rewriting the VI register set is correct and non-destructive; a separate
test must explicitly invalidate the inherited VI state before load to prove
full initialization independence.

## 2026-07-31: Accept opt-in standalone NTSC 480i programming

The checksum-pinned cycle loaded `gcn-drm.ko` with `program_mode=1`. Hardware
readback exactly matched the staged register gate: `DCR=0001`, `VTR=0f06`,
`HTR0=476901ad`, `HTR1=02e850c0`, and `PCR=2850`. The driver then registered
`card0` normally and retained ownership of `c002000.video`.

The accepted XRGB8888 client created two 1228800-byte buffers with 2560-byte
pitch and completed all 20 validated page-flip events through vblank sequence
307. It remained alive holding the final frame. The post-test soak passed with
three of three ICMP replies, responsive SSH, stable DRM ownership, and no
conversion or kernel fault signature. Direct observation confirmed the image
was clear.

The checksum-verified restore terminated the client, unloaded the DRM stack,
rebound `gcn-vifb`, and reloaded the accepted generated GX renderer. The legacy
console resumed with no fault. A slight visible interlaced vibration was noted,
but the user confirmed the same behavior is present in Gumboot; it is therefore
an existing output characteristic rather than a regression from VI register
programming.

This accepts the fixed NTSC 480i register sequence and reversible ownership
transition. The driver no longer needs to preserve inherited VI register
values when `program_mode=1`, but the test began with VI enabled and AVE state
still inherited. Next explicitly disable VI before probe and require the same
readback, KMS image, flip completion, and recovery to validate the inactive-VI
entry path.

## 2026-07-31: Stage default standalone-mode load

- Default-mode implementation: `d8e6c8f46`
- `gcn-drm.ko` SHA-256:
  `5aac95f38e60cbe045719bea296065372f53ca1495c1ccea48f485862877ff3e`
- Cycle harness SHA-256:
  `6941c264720fd8a3c2560913d37c61fe8921198f591af9f03476df572fe45a74`
- `wii-drm-test` SHA-256:
  `d2aa7acc2fc097fb695d06b318543725c01ed39c5c6b53745a07849229430b50`

Promote the accepted standalone VI programming path to the module default and
retain `program_mode=0` only as an explicit inherited-mode compatibility path.
Run the reversible cycle harness with `--no-build` and no mode parameter. This
is specifically a parameterless-load test: do not pass `--program-mode`.

Require the same programmed-register log and exact readback established by the
preceding test, normal `card0` registration, then run the accepted XRGB8888
client for 20 flips at 500 ms. Require all completion events, a clear observed
pattern, responsive network, fault-free kernel, and successful restoration of
the legacy GX console. Passing this gate makes standalone programming the
accepted normal load behavior and unblocks an on-device boot service.

## 2026-07-31: Accept default standalone-mode load

The checksum-pinned module was loaded with no `program_mode` argument. It took
the standalone initialization path by default and again read back `DCR=0001`,
`VTR=0f06`, `HTR0=476901ad`, `HTR1=02e850c0`, and `PCR=2850` before normal
`card0` registration.

The unchanged XRGB8888 client completed all 20 validated page flips through
vblank sequence 308 and remained alive holding the final frame. No conversion
or kernel fault was logged. Direct observation confirmed the result looked
correct. The checksum-verified restore then returned ownership to `gcn-vifb`
and reloaded the accepted GX module.

This accepts fixed NTSC 480i programming as the normal parameterless gcn-drm
load behavior. Keep `program_mode=0` only for explicit compatibility tests.
Proceed with a target-side SysV service that installs and loads the modular DRM
stack, transfers VI ownership transactionally, exposes start/stop/status, and
restores the legacy console automatically if startup fails.

## 2026-07-31: Stage target-side DRM ownership service

- Init-service implementation: `8d9899220`
- `wii-drm-init` SHA-256:
  `fbc5a646a54a8afa7eb53d6bb3f98d41920f7cbded329c51a630ed508014d87e`
- `gcn-drm.ko` SHA-256:
  `5aac95f38e60cbe045719bea296065372f53ca1495c1ccea48f485862877ff3e`
- `drm.ko` SHA-256:
  `8dc6380087638e48c13aef507c983457c511ab7ea1f31fa69c87a9b1ffa3acd7`
- `drm_kms_helper.ko` SHA-256:
  `13563e78b9ecba7a907446fe1747f82b1a354c1c4a55a17c8a41198d38e3765b`
- `drm_shmem_helper.ko` SHA-256:
  `b7166b9ee61e88651119766f76979ed7891f748aa8b94330455071f3e86d86a8`
- `drm_panel_orientation_quirks.ko` SHA-256:
  `fa1e9862ec9379b26b572df3b4e93878f21e563264ad6fd8353a4f27dcadd1df`
- Accepted `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Install the exact modules into `/lib/modules/6.18.40-wii+`, run `depmod -a`,
and install the service as `/etc/init.d/wii-drm`. Do not create runlevel links
in this test; boot behavior must remain unchanged.

From the accepted legacy console, require `service wii-drm status` to identify
legacy ownership, then run `service wii-drm start`. Require dependency
preflight, GX unload, gcnfb unbind, parameterless gcn-drm load, `card0`, and
DRM ownership. Run the accepted 20-flip XRGB8888 client and require the known
correct image, events, network, and fault gates. Terminate the client, run
`service wii-drm stop`, and require gcn-drm unload, gcnfb rebind, GX reload,
clear legacy output, and an idempotent final status. Any start failure must
restore legacy ownership automatically; never enable the service at boot until
this full manual transaction passes.

## 2026-07-31: Reject first service stop due to module auto-reload race

The installed service and all six modules matched their staged SHA-256 values;
no runlevel link was present. Initial `status` correctly reported legacy gcnfb.
`start` then preflighted the modular dependencies, unloaded GX, unbound gcnfb,
loaded parameterless gcn-drm, and verified both `c002000.video` ownership and
`card0`.

The accepted XRGB8888 client completed all 20 validated flips through vblank
sequence 310. Direct observation confirmed the full pattern appeared
correctly, and no conversion or kernel fault was logged.

The first `stop` restored the visible legacy path: gcnfb rebound, the accepted
GX renderer loaded, and final status identified legacy ownership. However,
`gcn_drm` and its generic dependencies remained in `/proc/modules`. The stop
implementation removed gcn-drm before binding gcnfb, briefly leaving the VI
device ownerless; udev matched its platform alias and automatically loaded
gcn-drm again. The reloaded module stayed unbound because gcnfb won ownership,
so there was no concurrent hardware access, but the transaction failed the
module-cleanup gate.

The unbound DRM stack was manually removed after confirming gcnfb ownership;
the target is back at the accepted legacy state with only `gcn_gx` loaded.
Fix recovery ordering to unbind the DRM platform driver while retaining its
module, bind gcnfb while the device cannot trigger a competing probe, then
unload gcn-drm and generic dependencies. Re-run the complete staged service
transaction after a separately committed implementation and artifact hash.

## 2026-07-31: Stage corrected service stop ordering

- Stop-order implementation: `3f96c69ef`
- `wii-drm-init` SHA-256:
  `facbb0471ca8de26149fc5c9d5dd04cc55f69ec02f5b56b0d93e862be2f0a23e`

Replace only `/etc/init.d/wii-drm` and verify its checksum. Startup behavior and
all module artifacts are unchanged from the preceding test, where service
startup and the full KMS visual/event gates passed. Run `status`, `start`, and
`status` again to establish service-managed DRM ownership, but do not open a
KMS client for this targeted stop-order test.

Run `service wii-drm stop`. The corrected path must unbind the platform device
from gcn-vi while gcn_drm remains loaded, bind gcnfb, then remove gcn_drm and
the generic DRM modules after legacy ownership prevents udev reprobe. Require
final ownership by `gcn-vifb`, only `gcn_gx` among matching modules, final
status `legacy gcnfb active`, responsive SSH, no kernel fault, and clear legacy
output. A lingering unbound gcn_drm module is a failure even if the console is
visible.

## 2026-07-31: Accept corrected target-side DRM service transaction

The replacement `/etc/init.d/wii-drm` matched SHA-256
`facbb0471ca8de26149fc5c9d5dd04cc55f69ec02f5b56b0d93e862be2f0a23e`.
Initial status identified legacy gcnfb, and the unchanged start path again
preflighted dependencies, transferred VI ownership, registered `card0`, and
reported DRM active.

With no KMS client holding references, the corrected stop path unbound gcn-vi,
bound gcnfb, removed gcn-drm, reloaded GX, and unloaded every generic DRM
module. Final status identified legacy ownership; `gcn-vifb` owned
`c002000.video`, and `gcn_gx` was the only matching module in `/proc/modules`.
SSH remained responsive, no kernel fault appeared, and direct observation
confirmed the restored console was clear.

This accepts transactional on-device VI ownership management and closes the
udev auto-load race. The service and checksum-pinned modules are installed on
the target but have no runlevel links, so boot behavior remains legacy-first.
Next connect this accepted service to a practical userspace DRM client or
desktop session before enabling it automatically at boot; starting DRM without
a client would intentionally leave only the initialized black XFB visible.

## 2026-07-31: Stage first raw-DRM virtual-console mirror

- Console implementation: `ec8d3b94d`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-console` SHA-256:
  `317911dc539f0b8413557d80078fcfb5927be44117cb59c71b614df731d705b6`
- Accepted init-service SHA-256:
  `facbb0471ca8de26149fc5c9d5dd04cc55f69ec02f5b56b0d93e862be2f0a23e`

The console is built without libdrm or other target libraries:

```sh
powerpc-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror \
  -idirafter include tools/wii-drm-console.c -o /tmp/wii-drm-console
powerpc-linux-gnu-strip -s /tmp/wii-drm-console
```

Upload and remotely verify the binary, start DRM through the accepted target
service, and launch the mirror against `/dev/vcsa1` and `/dev/dri/card0`.
Require its startup log to report 640x480 DRM and an 80x30 virtual console.
Write a deterministic screen containing white text plus distinct red, green,
blue, and yellow attribute samples to tty1, then replace a status line while
the mirror is running.

Acceptance requires a clear 80x30 console with correct glyph geometry and
colors, visible cursor blinking, and the changed line appearing without a
restart. The process must remain alive, SSH and the kernel must remain stable,
and no page-flip, conversion, or fault error may appear. Send SIGTERM, require
clean process exit and DRM master release, then stop the ownership service and
require the accepted clear legacy GX console. This is a display mirror only:
keyboard input and getty remain owned by tty1 and must continue to work.

## 2026-07-31: First DRM console is functional but visually provisional

The uploaded binary matched SHA-256
`317911dc539f0b8413557d80078fcfb5927be44117cb59c71b614df731d705b6`.
The accepted service started DRM normally, and the mirror remained alive while
reporting 640x480 scanout. After gcnfb unbound, tty1 changed from its prior
80x30 fbcon geometry to the fallback console's 80x25 geometry; the mirror
handled this valid smaller layout and centered its 640x400 glyph area.

The deterministic screen updated from frame one to `FRAME TWO - UPDATE
PASSED` without restart, the cursor visibly blinked, and red, green, blue, and
yellow samples all rendered with the correct identities. The user reported
that text was clear, but somewhat blurrier than the normal legacy console; the
dark red sample was substantially blurrier, while the other colors looked
fine. Treat live VCSA mirroring, glyph/attribute decoding, update detection,
page flips, and cursor timing as passed, but do not accept final console image
quality yet.

SIGTERM produced a clean process exit with no logged error. The service then
restored gcnfb/GX, removed the complete DRM stack, retained SSH, and logged no
kernel fault. Next isolate source pixel format: change the mirror to RGB565,
matching the legacy console framebuffer and the driver's independently
accepted RGB565 conversion path, without changing font geometry, VI state,
palette values, or update timing. Compare overall text and red-sample clarity
against this XRGB8888 baseline before attempting font filtering or palette
adjustment.

## 2026-07-31: Stage RGB565 DRM-console quality comparison

- Dual-format implementation: `75b7f6464`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-console` SHA-256:
  `2989352d478beac6122a00422c44a5a0dde26f4a4e84a8d2fc169acb71206fb0`

The console now defaults to RGB565 while retaining `--format xrgb8888` as a
control. No font, palette, layout, VI, cursor, VCSA, or update behavior changes
in this iteration.

Upload and verify the new artifact, start DRM with the accepted service, launch
the console with no format option, and require its log to report `rgb565`.
Write the exact same deterministic color and frame-two update screen used for
the XRGB8888 baseline. Require the functional startup, live update, cursor,
stability, graceful-exit, and recovery gates to remain passed.

Visually compare general text sharpness and the dark red sample directly
against the preceding XRGB8888 run. Accept RGB565 as the console default only
if text is at least as clear and red is materially improved or no worse. If
quality is unchanged, retain the lower-memory RGB565 path provisionally and
test interlace-stable glyph filtering next; if quality regresses, restore
XRGB8888 before any font experiment.

## 2026-07-31: RGB565 quality comparison invalidated by VGA index bug

The checksum-pinned binary started normally and reported `640x480 rgb565` with
an 80x25 VCSA source. Live updates, cursor blinking, process stability, clean
SIGTERM, full DRM unload, and legacy restoration continued to pass.

The labeled color control exposed a pre-existing console-client bug: `RED
SAMPLE` appeared blue, `BLUE SAMPLE` appeared red, and `YELLOW SAMPLE`
appeared teal; green remained green. A raw `/dev/vcsa1` capture independently
showed red glyphs tagged with attribute `0x04`, blue with `0x01`, yellow with
`0x06`, and green with `0x02`. This exactly matches the VGA/BGR attribute-bit
order. The client instead indexed an RGB-ordered palette directly.

Kernel source confirms the required mapping in `drivers/tty/vt/vt.c`:
`color_table[]` begins `{ 0, 4, 2, 6, 1, 5, 3, 7 }`, translating console
attributes before indexing the RGB `default_red/default_grn/default_blu`
arrays. The client omitted this translation. Therefore the observed red/blue
swap is not evidence of an RGB565 endian bug, and both this RGB565 quality
comparison and the preceding XRGB8888 color-identity claim are invalid.

Keep RGB565 provisionally, correct only the 16-entry userspace palette into
direct VGA attribute order, and repeat the labeled screen. Require all four
labels to match before evaluating red blur or comparing source formats.

## 2026-07-31: Stage corrected VCSA attribute palette

- Palette implementation: `91df5e8b4`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`

Repeat the RGB565 deterministic console screen with no changes except the
16-entry direct VGA attribute-order palette. Require red, green, blue, and
yellow labels to display as their named colors; cyan or red/blue reversal is a
failure. Also recheck live frame-two update, cursor blink, process/kernel
stability, graceful termination, and service recovery.

Once color identity passes, obtain a fresh visual verdict on general text and
the now-actually-red sample. This is the first valid RGB565 quality observation
and must not be compared against the invalid prior color labels as if they were
the same hue.

## 2026-07-31: Accept RGB565 virtual-console mirror and VGA palette

The uploaded artifact matched SHA-256
`66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
and reported `640x480 rgb565` with the 80x25 post-unbind VCSA geometry. The
deterministic screen reached `FRAME TWO - UPDATE PASSED`, the cursor blinked,
the process remained alive, and no page-flip, conversion, or kernel fault was
logged.

Direct observation confirmed that red, green, blue, and yellow now matched
their labels. The user reported that the corrected actual red was clearer and
everything else looked great. This is the first valid labeled-color result and
accepts both direct VGA attribute-order palette mapping and RGB565 as the
console mirror default.

SIGTERM again produced a clean process exit. The accepted service restored
gcnfb/GX, removed the full DRM stack, retained SSH, and left only `gcn_gx`
loaded with no fault. Proceed to service integration: install the accepted
console artifact persistently, have `wii-drm start` launch and verify it after
`card0`, and have `stop` terminate and wait for it before unbinding DRM. Keep
boot runlevel enablement separate until the combined service/client transaction
passes and its failure rollback is validated.

## 2026-07-31: Stage service-managed DRM console transaction

- Client-service implementation: `f64b4f555`
- `wii-drm-init` SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`
- Accepted `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`

Install the accepted console as `/usr/local/sbin/wii-drm-console` and replace
`/etc/init.d/wii-drm` with the checksum-pinned client-aware service. Keep the
service disabled at runlevels for this manual transaction.

From legacy state, run `service wii-drm start` without launching any client
separately. Require the service to transfer ownership, register `card0`, start
the configured console, verify it after one second, and report both DRM and a
live client PID from `status`. Write a tty1 update and require it to appear on
the accepted clear RGB565 console with cursor blinking.

Run `service wii-drm stop` without manually signaling the client. Require the
service to identify the exact executable from the PID file, send SIGTERM, wait
for process and DRM-master release, then restore gcnfb/GX and unload all DRM
modules. Require no stale PID file, final legacy status, clear output, stable
network/kernel, and only `gcn_gx` remaining. A separate negative control must
remove or invalidate the configured client and prove automatic startup
rollback before boot enablement.

## 2026-07-31: Reject combined console service on RGB565 byte order

The persistent service and console matched their staged SHA-256 values and no
runlevel links existed. `service wii-drm start` successfully transferred VI,
started `/usr/local/sbin/wii-drm-console`, verified it after one second, and
reported the exact live client PID. The tty update and cursor continued to
work, and the service later stopped the client automatically, waited for exit,
restored gcnfb/GX, removed the DRM stack and PID file, and logged no fault.

The explicit labeled-color gate failed: red displayed blue, blue displayed
red, yellow displayed light blue/cyan, and green remained green. With the VCSA
attribute-order palette now independently corrected, this mapping is the
signature of 16-bit byte reversal: RGB565 red `0xa800` is interpreted as
`0x00a8`, and red+green yellow is interpreted as blue+green cyan.

Retract the preceding claim that the corrected RGB565 run established valid
color identity or final RGB565 console quality; the earlier response did not
explicitly enumerate each displayed hue, while this repeated service-managed
screen did. Keep the VGA attribute-order correction itself: raw VCSA attributes
and kernel `color_table[]` still independently prove that mapping.

Do not change byte packing yet. First run the same accepted console binary with
`--format xrgb8888` and the corrected palette as a channel-order control. If
the four labels are correct, constrain the fix to little-endian RGB565 storage;
if XRGB8888 is also swapped, correct the shared big-endian DRM format contract
in the driver and both raw clients. Boot enablement remains blocked.

## 2026-07-31: Stage corrected-palette XRGB8888 channel control

Reuse exact console SHA-256
`66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`.
Temporarily set `WII_DRM_CLIENT=` in `/etc/default/wii-drm` so the accepted
service transfers hardware ownership without launching the RGB565 default.
Then manually run:

```sh
/usr/local/sbin/wii-drm-console --format xrgb8888
```

Write the exact labeled red, green, blue, and yellow screen. Require the client
log to report XRGB8888 and obtain an explicit label-by-label visual result.
This is a channel-order control, not a quality acceptance run. Preserve the
usual liveness, cursor, fault, graceful-exit, service-recovery, and full module
cleanup gates. Remove the temporary `/etc/default/wii-drm` override afterward.

Correct four-color output constrains the implementation fix to RGB565's
little-endian storage. Any analogous red/blue swap requires a shared format
contract correction before further console integration.

## 2026-07-31: XRGB8888 control constrains bug to RGB565 storage

The exact console binary reported `640x480 xrgb8888` and remained stable. The
user explicitly confirmed red, green, blue, and yellow/gold each appeared as
their label. The non-bright ANSI 33 sample looked more gold than pure yellow,
which is correct for the canonical VGA brown/dark-yellow value `0xaa5500`;
bright ANSI 93 maps separately to `0xffff55`.

The manual client exited cleanly, the service restored gcnfb/GX and removed all
DRM modules, and the temporary `WII_DRM_CLIENT=` override was deleted. No fault
appeared.

This validates the corrected VCSA palette and XRGB8888 channel order. Constrain
the next implementation to RGB565 client storage: DRM_FORMAT_RGB565 is defined
as little-endian, but the big-endian PowerPC console writes converted values
through native `uint16_t` stores. Emit the low byte then high byte explicitly,
leaving driver conversion, format advertisement, palette, font, and XRGB8888
untouched. Repeat the labeled RGB565 service-managed screen before restoring
automatic boot-client work.

## 2026-07-31: Stage little-endian RGB565 console storage

- Storage implementation: `ecc7a253d`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-console` SHA-256:
  `f669351dd045377d2f1c65afecc94d21ff95a8bdddaea778918f8790f197283f`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

Install the new console at `/usr/local/sbin/wii-drm-console` and run the normal
service-managed transaction with no `/etc/default/wii-drm` override. Require
automatic client startup and an `rgb565` log, then write the same explicit red,
green, blue, and yellow/gold labels.

All four labels must match their named colors. Require red clarity at least as
good as the corrected-palette observation, plus live tty update, cursor blink,
stable client/network/kernel, automatic service SIGTERM and wait, complete DRM
module removal, and clear legacy recovery. Passing this test restores the
combined service/client milestone and permits a separate missing-client
rollback control before boot enablement.

## 2026-07-31: Reject explicit little-endian RGB565 storage

The deployed console matched SHA-256
`f669351dd045377d2f1c65afecc94d21ff95a8bdddaea778918f8790f197283f`
and reported `640x480 rgb565`. The labeled-color gate failed decisively: red
was very dark red, green was dark purple, blue was lime green, and yellow/gold
was purple. The `Expected: RED GREEN BLUE GOLD` line, live-update line, and
cursor label all appeared green rather than their intended VGA colors.

This mapping rejects explicit low-byte/high-byte RGB565 writes on the Wii's
big-endian PowerPC path. In particular, green RGB565 `0x0540` becomes `0x4005`
when its bytes are reversed, which explains the observed purple, while blue
`0x0015` becomes `0x1500`, which explains the observed lime green. The kernel
conversion path therefore consumes these dumb-buffer pixels as native 16-bit
PowerPC values despite the generic DRM format naming convention.

The service stopped its exact client PID, restored VI ownership to `gcn-vifb`,
left only `gcn_gx` loaded, and logged no new kernel fault. Restore native
`uint16_t` RGB565 stores while retaining the independently validated VGA/BGR
attribute palette. Re-run the same explicit label-by-label RGB565 test before
attempting the missing-client rollback control or boot runlevel enablement.

## 2026-07-31: Stage native-store RGB565 reproducibility control

- Native-store implementation: `822c92656`
- Static stripped PowerPC binary size: `726988` bytes
- `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

The rebuilt artifact is byte-for-byte identical to the earlier native-store
binary used for the corrected-palette XRGB8888 control and the disputed
RGB565 result. Deploy this exact artifact as the service-managed default and
repeat the explicit RGB565 labels without changing any driver or service
component.

Require the user to report each label separately: displayed red, green, blue,
and yellow/gold, followed by the expected-line, live-update-line, and cursor
colors. Also require cursor blink, tty update, stable client/network/kernel,
automatic service stop, full DRM module removal, and clear legacy recovery.
Do not infer channel correctness from general console clarity. A correct,
repeatable labeled result accepts native PowerPC RGB565 storage; another
red/blue swap requires capturing the exact installed binary hash and source
VCSA attributes before any further encoding change.

## 2026-07-31: Native RGB565 reproduces an exact red/blue swap

The installed console matched the staged SHA-256
`66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`,
reported `640x480 rgb565`, remained alive, updated tty1, and blinked its cursor.
Direct full-screen observation and a photograph established the complete color
mapping:

- source red displayed blue;
- source green displayed green;
- source blue displayed red;
- source gold/yellow displayed light blue/cyan;
- source magenta displayed magenta/purple;
- source cyan displayed gold/yellow; and
- source white displayed white.

The screen was otherwise clear. A live `/dev/vcsa1` dump independently showed
the correct source attributes, including `0x04` for the red line and `0x02` for
the green line, so terminal attribute decoding and the corrected VGA palette
are not the source of this transform. The exact installed binary hash also
rules out a stale deployment.

This is a deterministic red/blue exchange, unlike the rejected explicit-byte
test's mixed dark colors. Do not make another RGB565 storage change yet. Re-run
the same installed binary immediately with `--format xrgb8888` and the same
labeled screen. If XRGB8888 reproduces the swap, fix the shared RGB-to-YUV/XFB
contract; if XRGB8888 remains correct, inspect RGB565 framebuffer registration
and driver decoding with an instrumented positive control. Keep boot enablement
blocked.

## 2026-07-31: XRGB8888 repeat control passes every color

Without changing the installed console binary, transfer VI ownership through
the service with automatic client launch temporarily disabled and run the
console with `--format xrgb8888`. The client reported `640x480 xrgb8888` and
the user explicitly identified the displayed lines, in order, as red, green,
blue, gold, white, purple, and teal. Every source label therefore matched its
displayed color, including the red/blue and yellow/cyan pairs that exchange in
RGB565.

The manual client exited, the temporary `/etc/default/wii-drm` override was
removed, and service stop restored `gcn-vifb` with only `gcn_gx` loaded and no
kernel fault. This repeat control validates the shared RGB-to-YUV coefficients,
YUYV/XFB packing, scanout, VGA palette, and VCSA decode under the currently
loaded driver. Constrain the defect to the RGB565 path.

Before editing conversion code, checksum the installed `gcn-drm.ko` against
the local build and inspect the legacy `DRM_IOCTL_MODE_ADDFB` RGB565 format
selection on big-endian PowerPC. The checked-in `xrgb8888_to_rgb565()` and
`gcn_drm_rgb565_pair()` both appear to use standard R5:G6:B5 bit positions, so
an exact red/blue exchange is not explained by those source expressions alone.
Use an instrumented positive control if artifacts match; do not infer another
storage byte order from color output.

## 2026-07-31: Stage one-shot RGB565 conversion positive control

- Diagnostic implementation: `f5e3e7b64`
- `gcn-drm.ko` SHA-256:
  `92579cf75b311512bcd8ee8b17bad6dca0274b4d9f0aaf6dead101b4ab0738a6`
- Known-pattern `wii-drm-test` SHA-256:
  `88a1511225805948e56b7d003ab2429325faf90a2cac580f4dd9f0fa15f5166f`

Replace only `gcn-drm.ko`, transfer ownership with no automatic console, and
run the existing test tool once with `--format rgb565` and no flips. Its static
pattern has samples away from all borders, grid lines, checkerboards, and
markers at `(100,100)` red, `(500,100)` green, `(100,400)` blue, and
`(500,400)` white.

The driver must emit exactly one diagnostic set containing framebuffer format,
pitch, each native 16-bit word, its two memory bytes, and the corresponding
packed YUYV word. Expected native words from the userspace packer are
approximately `f904` red, `27e4` green, `221f` blue, and `ffff` white. Compare
the logged RGB565 YUYV values against the same colors through the already
validated XRGB8888 helper. This test's conclusion comes from logged source and
conversion values, not subjective screen colors.

After capture, terminate the test process and stop the service. Require legacy
`gcn-vifb` ownership, only `gcn_gx` loaded, and no kernel fault. Do not leave
the diagnostic module installed after its result is recorded and understood.

## 2026-07-31: RGB565 source and decoder positive control passes

The installed module and test utility matched their staged SHA-256 values. The
unchanged utility registered a 640x480 RGB565 framebuffer with pitch 1280 and
the driver logged format `0x36314752` (`RG16`). Its one-shot samples were:

```text
red   word=f904 bytes=f9,04 yuyv=725a72ef
green word=27e4 bytes=27,e4 yuyv=b336b322
blue  word=221f bytes=22,1f yuyv=5de45d60
white word=ffff bytes=ff,ff yuyv=eb80eb80
```

All four native words exactly match the userspace RGB565 packer at the known
pattern coordinates. Their memory bytes confirm native big-endian stores, and
the resulting YUYV words contain the expected red, green, blue, and neutral
white luma/chroma values. The test process remained live, handled SIGTERM, and
exited zero. Service stop restored `gcn-vifb`, left only `gcn_gx` loaded, and
logged no fault.

This validates the known-pattern producer and the driver's native-word RGB565
decode through YUYV packing, but does not yet explain the console's observed
red/blue exchange. Repeat this exact no-flip RGB565 pattern and explicitly
identify the displayed top-left, top-right, bottom-left, and bottom-right
quadrants. Correct quadrants constrain the defect to console framebuffer
production or update handling; swapped quadrants contradict the logged YUYV
values and require a matched XRGB pattern comparison at the XFB boundary.

## 2026-07-31: RGB565 visual pattern confirms chroma-pair exchange

The exact staged RGB565 pattern was displayed without flips. Direct visual
identification reported top-left blue instead of source red, top-right green,
bottom-right white, and bottom-left red instead of source blue. The center
checkerboard displayed magenta and gold instead of source magenta and cyan.

This independently reproduces the console result on a second userspace client:
red and blue exchange, cyan becomes yellow/gold, while green, white, and
magenta remain invariant. Combined with the immediately preceding driver log,
source red word `f904` was decoded into `yuyv=725a72ef` yet appeared blue. That
is the visual signature expected if the hardware interprets the two chroma
bytes in the opposite Cb/Cr order from the driver's current assumption.

The test process was terminated and service stop restored `gcn-vifb` with only
`gcn_gx` loaded. Do not change XFB packing until resolving the passing
XRGB8888 contradiction. Extend the one-shot diagnostic to sample the identical
XRGB8888 pattern and log source words plus packed YUYV. If XRGB source red
produces the same YUYV as RGB565 source red, repeat its visual quadrant test;
if it produces the blue YUYV word, locate the 32-bit source-channel reversal
that currently cancels the XFB chroma-order defect.

## 2026-07-31: Stage matched XRGB8888 conversion control

- Matched diagnostic implementation: `840ea5f24`
- `gcn-drm.ko` SHA-256:
  `98757aadc09643c73100b356c384c7a22a2d2fda89c328f979081e140276f5d4`
- `wii-drm-test` SHA-256:
  `88a1511225805948e56b7d003ab2429325faf90a2cac580f4dd9f0fa15f5166f`

Replace only the diagnostic module and run the unchanged no-flip pattern with
`--format xrgb8888`. The driver samples the same four coordinates used by the
RGB565 control. Expected native source words are `00ff2020` red, `0020ff20`
green, `002040ff` blue, and `00ffffff` white with corresponding big-endian
memory bytes.

Capture the one-shot XRGB8888 logs, then visually identify the same four
quadrants and center checkerboard. Compare each XRGB YUYV word directly with
its RGB565 counterpart from the preceding accepted diagnostic. Identical YUYV
plus different visual color would indicate that the compared runs did not
share the same hardware state and must be repeated; swapped XRGB source or
YUYV would identify the cancellation that made the prior XRGB console appear
correct. Recover legacy ownership and require no fault afterward.

## 2026-07-31: Paired rerun retracts prior XRGB color pass

The user warned that one earlier visual answer may have been incorrect, so the
previous XRGB8888 color acceptance is retracted. A controlled paired rerun used
one module load, one VI programming sequence, one DRM ownership interval, and
the exact same static pattern utility. Only the userspace framebuffer format
changed between tests.

RGB565 displayed top-left blue, top-right green, bottom-right white,
bottom-left red, and a magenta/gold checkerboard. XRGB8888 then displayed the
exact same blue, green, white, red quadrants and magenta/gold checkerboard.
Thus both formats exchange red with blue and cyan with yellow while preserving
green, white, and magenta.

The matched driver diagnostics independently logged standard source words for
both formats and effectively identical packed outputs for each color. Source
red produced `yuyv=725a72ef` in both paths; source blue produced
`yuyv=5de45d60` for RGB565 and `yuyv=5ce45c60` for XRGB8888. The small luma
difference is expected from 5:6:5 quantization and cannot explain a hue swap.

This resolves the prior contradiction and localizes the defect to the shared
XFB word layout. On this DRM scanout path, the VI interprets the two chroma
positions opposite the driver's current `Y0,Cb,Y1,Cr` packing. Change only
`gcn_drm_pack_yuyv()` to emit `Y0,Cr,Y1,Cb`, retain all coefficients and source
decoders, and repeat the same paired visual test. Keep the one-shot diagnostics
for that validation, then remove them after the corrected output is accepted.

## 2026-07-31: Stage corrected XFB chroma-order validation

- Chroma-order implementation: `9aa9dbd8c`
- `gcn-drm.ko` SHA-256:
  `7e9b6082bc811ad0c61cbd76e27db2a0349839f10d59f8d50158e25b44b3db7c`
- `wii-drm-test` SHA-256:
  `88a1511225805948e56b7d003ab2429325faf90a2cac580f4dd9f0fa15f5166f`

Replace only `gcn-drm.ko` and perform a same-session paired test under one
module load and VI programming sequence. Run the no-flip RGB565 pattern first,
record all quadrants and checkerboard colors, terminate it, then run XRGB8888
without stopping the service or reloading the driver and record the same items.

Both formats must display top-left red, top-right green, bottom-right white,
bottom-left blue, and a magenta/cyan checkerboard. Their one-shot diagnostics
must retain the previously validated source words while showing the two chroma
bytes exchanged in packed output, for example source red changing from
`725a72ef` to `72ef725a`.

After both visual passes, terminate the test, remove the temporary empty-client
override, and restore legacy ownership. Require only `gcn_gx` loaded and no
kernel fault. Passing permits removal of the temporary conversion diagnostics,
followed by a final service-managed console color and interaction regression.

## 2026-07-31: Accept corrected XFB chroma order in both formats

The installed module matched SHA-256
`7e9b6082bc811ad0c61cbd76e27db2a0349839f10d59f8d50158e25b44b3db7c`.
Under one module load and VI programming interval, the no-flip RGB565 pattern
displayed top-left red, top-right green, bottom-right white, bottom-left blue,
and a teal/magenta checkerboard. Without reloading the module or stopping the
service, XRGB8888 displayed the same correct pattern.

The diagnostics retained the exact validated source words while confirming
the intended packed-byte change. RGB565 source red `f904` changed from
`725a72ef` to `72ef725a`; XRGB8888 source red `00ff2020` produced the same
corrected output. Blue similarly changed to `5d605de4` for RGB565 and
`5c605ce4` for XRGB8888. White remained neutral `eb80eb80`.

This accepts `Y0,Cr,Y1,Cb` as the DRM driver's Wii XFB word layout and closes
the deterministic red/blue plus cyan/yellow exchange. The test processes
handled termination, service stop restored `gcn-vifb`, only `gcn_gx` remained
loaded, and no fault appeared.

Remove both temporary one-shot sample loggers and their state flags without
changing the accepted packer. Rebuild and run the service-managed default
RGB565 console with explicit labeled colors, live tty update, cursor blink,
client liveness, automatic stop, module cleanup, and clear legacy recovery.

## 2026-08-02: Stage cleaned-module console regression

- Diagnostic-removal implementation: `18193257d`
- Clean `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Native-store `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

The Wii booted with `gcn-vifb` owning VI and `gcn_gx` active, but udev also
autoloaded the matching `gcn_drm` module and its dependencies without binding
them. Unload that zero-use DRM stack before replacing the module so the next
service start necessarily loads the checksum-pinned clean artifact from disk.
Do not alter runlevel links or enable the service in this test.

Run the normal service-managed transaction with the default RGB565 console.
Write explicit red, green, blue, gold, white, magenta, and cyan/teal labels and
require every displayed hue to match. Also require a live tty update, blinking
cursor, exact client PID liveness, no temporary conversion log messages, stable
network/kernel, automatic client SIGTERM and wait, full DRM module removal,
and clear legacy `gcn-vifb`/`gcn_gx` recovery.

Passing this cleaned regression permits the separate missing-client rollback
control. Treat udev autoload suppression as a later memory-footprint cleanup:
it does not own hardware or affect this transaction while `gcn-vifb` remains
bound.

## 2026-08-02: Reject cleaned console color identity; client path remains

All deployed artifacts matched their staged SHA-256 values. The clean module
contained no temporary conversion logs, service start transferred ownership,
and the default client reported `640x480 rgb565` with a live verified PID. The
screen was clear and stable, tty update passed, and the cursor blinked.

The explicit color gate failed: source red displayed blue, green displayed
green, blue displayed red, gold displayed light blue/cyan, white displayed
white, magenta displayed magenta, and teal/cyan displayed gold. The purple live
update remained purple and the cyan cursor appeared gold. This is again an
exact red/blue and cyan/yellow exchange.

A live VCSA dump independently contained the correct Linux console attributes:
`0x04` for the red line, `0x02` for green, and `0x01` for blue. The exact same
clean driver passed the standalone RGB565 pattern in the preceding accepted
test, so do not revert its XFB chroma-order correction or change KMS state.
Constrain the remaining defect to the console client's VCSA-to-RGB565 path.

Service stop terminated and waited for the exact client PID, removed the DRM
stack and PID file, restored `gcn-vifb` plus `gcn_gx`, and logged no fault.
Instrument the console client to print each semantic palette entry's XRGB8888
value, converted RGB565 word, and actual mapped bytes as a positive control.
Use that evidence before changing palette values or storage again.

## 2026-08-02: Stage console RGB565 palette positive control

- Palette diagnostic implementation: `1b4b456ad`
- Diagnostic `wii-drm-console` SHA-256:
  `20a8713cd918578ad494b26e9f416a5ce27a99fbf8df73d5a51c68a6ec92dda5`
- Clean corrected `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

Replace only the console client and run the normal service transaction. The
client must print semantic palette values and PowerPC-native RGB565 bytes to
`/run/wii-drm-client.log` without changing the displayed framebuffer. Expected
red is XRGB `00aa0000`, RGB565 `a800`, bytes `a8,00`; expected blue is XRGB
`000000aa`, RGB565 `0015`, bytes `00,15`. Green, gold, white, magenta, and teal
must similarly match the standard VGA values in source.

Repeat the labeled screen only to correlate the unchanged visual failure with
the exact diagnostic run. Then stop the service and require normal client,
module, and ownership cleanup. If logged values and bytes are correct, the next
test must observe what the kernel conversion path reads from this console
framebuffer; do not compensate the palette or storage without that matched
downstream measurement.

## 2026-08-02: Palette positive control passes and screen is correct

The installed console, clean corrected module, and service matched all staged
SHA-256 values. The client logged the expected semantic encodings:

```text
red     xrgb=00aa0000 rgb565=a800 bytes=a8,00
green   xrgb=0000aa00 rgb565=0540 bytes=05,40
blue    xrgb=000000aa rgb565=0015 bytes=00,15
gold    xrgb=00aa5500 rgb565=aaa0 bytes=aa,a0
white   xrgb=00aaaaaa rgb565=ad55 bytes=ad,55
magenta xrgb=00aa00aa rgb565=a815 bytes=a8,15
teal    xrgb=0000aaaa rgb565=0555 bytes=05,55
```

The user then confirmed that every displayed color was correct and the cursor
blinked. Service stop terminated the exact client PID, restored `gcn-vifb` and
`gcn_gx`, removed the entire DRM stack and PID file, and logged no fault.

The logging implementation does not alter framebuffer production or storage,
so it cannot be treated as a color fix. This pass conflicts with the immediately
preceding checksum-verified clean run that displayed an exact channel swap.
Treat the discrepancy as either intermittent hardware/scanout state or a visual
reporting error, not as evidence for another encoding change.

Repeat one full service start/stop using these exact installed artifacts without
rebuilding or redeploying. Require all seven colors and cursor behavior again.
Two consecutive exact-binary passes permit removing the palette logger and
running one final clean-binary check; any recurrence requires matched XFB page
readback rather than further source-side instrumentation.

## 2026-08-02: Exact-binary console restart repeats correct colors

Without rebuilding, copying, or changing any target file, a second complete
service transaction re-verified all three installed SHA-256 values and launched
the same RGB565 client. The user again confirmed that all seven labeled colors
were correct and the cursor blinked. The client remained live and repeated the
same correct palette word and byte diagnostics.

Service stop again terminated the exact PID, restored `gcn-vifb` plus `gcn_gx`,
removed the DRM stack and PID file, and logged no fault. This provides two
consecutive correct service starts with identical driver and client artifacts.
Treat the earlier swapped report as unconfirmed rather than changing the now
twice-reproduced format contract.

Remove only `log_rgb565_palette()` and its call, rebuild, and require the output
to return byte-for-byte to the prior clean console SHA-256
`66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`.
Deploy that clean client with the accepted corrected module and perform one
final explicit color/cursor/service rollback check.

## 2026-08-02: Stage final clean console regression

- Palette-log removal: `3fa4c38b6`
- Clean `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Clean corrected `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

The rebuilt stripped console is byte-for-byte identical to the prior clean
artifact, proving that only the temporary logger was removed. Replace only the
target console binary and perform one final complete service transaction with
the explicit seven-color screen, live update, and blinking cursor.

Require every color to match, no palette or conversion diagnostic output,
stable client/network/kernel state, automatic exact-PID termination, complete
DRM module removal, and clear legacy recovery. Passing accepts the clean
service-managed console milestone and unblocks the missing-client rollback
control; it does not yet enable boot runlevel links.

## 2026-08-02: Reject final clean console; logger perturbs failure

The final clean console, corrected clean module, and service matched every
staged SHA-256 value, and no diagnostic text appeared. Service start and client
liveness passed, but the explicit color screen again displayed blue, green,
red, light blue, white, magenta, and gold instead of red, green, blue, gold,
white, magenta, and teal.

This creates a reproducible binary correlation: the palette-logging client
displayed correct colors on two consecutive complete service transactions,
while the clean byte-identical rendering implementation displayed the exact
red/blue and cyan/yellow exchange before and after those runs. The logger runs
after initial modeset and changes no framebuffer value, palette, format, or
storage operation. Treat its effect as timing, cache pressure, or code-layout
perturbation, not as functional behavior to retain.

Service stop again terminated the exact client PID, restored `gcn-vifb` and
`gcn_gx`, removed the DRM stack and PID file, and logged no fault. Investigate
cache visibility between the userspace dumb-buffer mmap and the driver's shmem
kernel mapping. In particular, audit `drm_gem_fb_begin_cpu_access()` direction,
PowerPC cache alias handling, and whether dirty userspace pages are written back
before `gcn_drm_convert()` reads them. Do not change color encoding again.

## 2026-08-02: Stage Broadway source-cache synchronization test

- Source-cache implementation: `95b1faa0e`
- `gcn-drm.ko` SHA-256:
  `a1929dba3ceda121b98e6a23e7b57fa1fa6be96633707c0eecdf066ac88d2e7a`
- Clean `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

DRM core inspection established that `drm_gem_fb_begin_cpu_access()` only
invokes synchronization for imported dma-bufs; it is a no-op for this driver's
native shmem dumb buffers. The client writes through a userspace mmap while the
driver reads a separate kernel vmap. The logging-only client's repeatable pass
is consistent with cache pressure evicting dirty userspace lines before kernel
conversion.

The implementation flushes and invalidates the complete active source range
through the kernel vmap immediately before conversion. Replace only the clean
module and run the clean, no-logger RGB565 console. Require all seven colors,
live update, and cursor to remain correct. If the first transaction passes,
repeat complete service stop/start transactions with the exact same artifacts
to test the previously intermittent boundary; do not redeploy between repeats.

Each stop must restore `gcn-vifb` and `gcn_gx`, remove the client PID and DRM
stack, and log no fault. Any remaining channel exchange requires actual source
and XFB page readback. Consecutive clean passes accept the cache synchronization
as the root fix and unblock rollback and boot-service work.

## 2026-08-02: Reject source-vmap cache flush as color fix

The installed module, clean console, and service matched all staged SHA-256
values. The client started normally with no diagnostic logger, remained live,
and displayed a clear blinking-cursor screen. All colors nevertheless repeated
the exact prior red/blue and cyan/yellow exchange.

Flushing and invalidating the full shmem kernel-vmap source range immediately
before every conversion therefore does not resolve the clean-client failure.
Reject the cache-alias hypothesis as the root cause; do not retain this added
per-frame cache cost without separate evidence that it is required for data
visibility. Service stop restored `gcn-vifb` and `gcn_gx`, removed the DRM stack
and PID file, and logged no fault.

The next measurement must correlate one deterministic frame at both sides of
conversion. Render seven full-width console background bars, then capture the
RGB565 source word and converted XFB word at each bar's center from the same
page. Also log the destination page and VI scanout address. This distinguishes
client/source encoding, RGB-to-XFB conversion, and wrong/stale VI page selection
without relying on timing or another palette inference.

## 2026-08-02: Stage matched source/XFB/VI bar capture

- Bar-capture implementation: `35e2f5933`
- `gcn-drm.ko` SHA-256:
  `a2c8879237efc62c6fe7cf6ff303c042b0eb201073420f4c3d00f8434d9e89c7`
- Clean `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

Replace only the module and start the normal clean console. Clear tty1 and
write seven 79-column space rows using red, green, blue, gold, white, magenta,
and teal background attributes in that order. With 80x25 VCSA centered in the
640x480 buffer, the diagnostic samples x=320 at y=48,64,80,96,112,128,144.

The source detector expects native RGB565 words
`a800,0540,0015,aaa0,ad55,a815,0555`. It logs at most sixteen attempts. On an
exact match, require seven converted XFB words from the same page and a later
scanout log for that page containing physical address plus VI TFBL/BFBL
readback. Save the complete lines before stopping the service.

Observe and report each displayed bar color as secondary confirmation. The
source and XFB logs, not the visual report, determine where the exchange enters.
Require normal exact-PID client termination, DRM cleanup, legacy recovery, and
no fault afterward.

## 2026-08-02: Matched bar capture passes and localizes intermittent exchange

The installed artifacts matched all staged SHA-256 values. The deterministic
bar frame was detected with the exact native RGB565 source sequence
`a800,0540,0015,aaa0,ad55,a815,0555` on destination page 1. The converted words
from that same page were:

```
red      43d64362
green    73387347
blue     237123d6
gold     75b27546
white    bb81bb80
magenta  57c857b9
teal     872a879e
```

VI then selected physical page `0x0172e000`; readback was
`TFBL=0x100b9700` and `BFBL=0x000b9728`, which encode that page and its bottom
field. The user independently reported the visible bars in the intended order:
red, green, blue, gold, white, magenta, teal.

This run validates source encoding, RGB565-to-XFB conversion, and VI page
selection together for the observed frame. It also reproduces the central
perturbation: the immediately preceding clean module displayed the stable
red/blue and gold/teal exchange, while adding post-conversion capture and printk
activity made the same clean userspace binary render correctly. Do not change
the channel formula or client palette based on the clean-build symptom.

The capture reads occur after `flush_dcache_range()` and before VI page publish.
Their apparent corrective effect makes destination-XFB completion ordering the
next narrow hypothesis. Test an explicit PowerPC memory barrier between the XFB
cache flush and `gcn_drm_set_scanout()` without retaining the capture reads.
Require the clean binary and repeated complete service transactions to preserve
the intended colors.

Service stop terminated the exact client PID, removed the DRM stack, restored
`gcn-vifb` plus `gcn_gx`, and logged no fault.

## 2026-08-02: Stage external XFB observer positive control

- Observer implementation: `c2718505d`
- `gcn-xfb-observer.ko` SHA-256:
  `3c588ba3ac75da2387cc87d74649159b23130249156717e07bfec67bb1b344bc`
- Known-correct capture `gcn-drm.ko` SHA-256:
  `a2c8879237efc62c6fe7cf6ff303c042b0eb201073420f4c3d00f8434d9e89c7`
- Clean `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

`CONFIG_STRICT_DEVMEM=y` correctly blocks direct userspace access to VI MMIO and
reserved XFB RAM. The observer is therefore a separate, read-only module. It
does not bind the VI platform device and must be loaded only after the rendered
frame is visibly established. It reads TFBL/BFBL, decodes the selected physical
page, and samples the seven established bar coordinates through an uncached
mapping.

Validate this measurement tool before drawing any conclusion from it. Install
the observer beside the already checksum-verified known-correct capture module,
start the clean console, render the seven bars in semantic order, and confirm
them visually. Only then load `gcn-xfb-observer.ko`. Require its TFBL/BFBL,
decoded page, and seven XFB words to equal the in-path capture:

```
43d64362 73387347 237123d6 75b27546 bb81bb80 57c857b9 872a879e
```

Unload the observer before stopping the service. Require exact client cleanup,
legacy recovery, and no mapping warning or kernel fault. A passing positive
control permits the unchanged observer to inspect a later clean wrong-color
frame without perturbing conversion or page publication before visibility.

## 2026-08-02: Reject uncached ioremap observer mapping

The observer positive-control frame was visually confirmed as red, green, blue,
gold, white, magenta, and teal before the observer was loaded. `insmod` then
failed with `ENOMEM` and produced no sample output. The kernel explained the
failure explicitly:

```
__ioremap_caller(): phys addr 0x1698000 is RAM lr ioremap
```

VI MMIO mapping therefore succeeded, but PowerPC correctly rejected an I/O
mapping of normal reserved XFB RAM. This is a failed positive control; none of
the observer's intended measurements were made or validated. Do not treat the
first observer hash as usable.

Revise only the XFB mapping to `memremap(..., MEMREMAP_WB)` and pair it with
`memunmap()`, matching both `gcn-drm` and the working legacy `gcnfb` mapping.
Keep VI MMIO on `ioremap()`. Repeat the known-correct positive control before
using the observer on a clean wrong-color frame.

The observer never remained loaded. Service stop terminated the exact client
PID, restored `gcn-vifb` plus `gcn_gx`, and logged no fault.

## 2026-08-02: Stage reserved-RAM observer positive control

- Observer mapping correction: `29330b9ec`
- Revised `gcn-xfb-observer.ko` SHA-256:
  `689d717e46300d8037f536176b0a5e4a168ea6f67cd444968cb392dcd79b7d0b`
- Known-correct capture `gcn-drm.ko` SHA-256:
  `a2c8879237efc62c6fe7cf6ff303c042b0eb201073420f4c3d00f8434d9e89c7`

Repeat the preceding positive-control procedure unchanged. The observer now
uses `memremap(MEMREMAP_WB)` only for reserved XFB RAM and retains `ioremap()`
for VI MMIO. Require successful module load and the exact seven known-correct
XFB words before accepting the observer. Unload it before service stop and
require clean legacy recovery with no mapping warning or fault.

## 2026-08-02: Validate reserved-RAM observer on page 1

The revised observer and all active artifacts matched their staged SHA-256
values. Before observer load, the user confirmed the seven visible bars as red,
green, blue, gold, white, magenta, and teal. The first snapshot landed on page
0, while the already validated in-path capture was page 1. Repeating unchanged
load snapshots across the cursor-flip phase produced a page-1 snapshot with
exact agreement:

```
TFBL=100b9700 BFBL=000b9728 top=0172e000
43d64362 73387347 237123d6 75b27546 bb81bb80 57c857b9 872a879e
```

This is a successful positive control for the observer on the selected page.
The user also confirmed a newly rendered foreground row labeled red, green,
blue, gold, white, magenta, and teal displayed every label in its intended
color.

Later repeated snapshots are not stable test data: every observer load emits
eight `pr_info` lines, and kernel console output becomes new VCSA content that
the running client mirrors into subsequent pages. Future use must lower console
loglevel before rendering the deterministic frame, preserve the old level, and
accept only the first snapshot for the intended page. Restore loglevel only
after the service is stopped.

Observer unload, exact client termination, DRM removal, and legacy `gcn-vifb`
plus `gcn_gx` recovery all passed without a mapping warning or fault.

Next restore the byte-identical clean `gcn-drm` implementation, render the same
bars, and visually classify the frame before loading the unchanged validated
observer. The first page-1 XFB snapshot will show whether a wrong-color clean
frame contains exchanged chroma bytes in RAM or whether the exchange occurs
after physical XFB storage.

## 2026-08-02: Stage clean driver with external XFB observation

- Clean-path restoration: `d2f7ff4d1`
- Clean `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Validated `gcn-xfb-observer.ko` SHA-256:
  `689d717e46300d8037f536176b0a5e4a168ea6f67cd444968cb392dcd79b7d0b`
- Clean `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`
- Client-aware service SHA-256:
  `74229640ab480c2eb5488acb296029af9827eaf5609e63e08d9058060214476b`

The restored driver source is byte-identical to clean baseline `3fa4c38b6`, and
its module hash exactly reproduces the previously wrong-color artifact. Replace
only `gcn-drm.ko`; leave the validated observer, client, and service unchanged.

Before service start, save the current console loglevel and set it to 1 so
driver and observer `pr_info` lines remain in the ring buffer without becoming
new VCSA content. Start DRM, render the deterministic bars, wait for both cursor
pages to update, and visually report the seven colors before observer load.

Load the unchanged observer only when TFBL selects page 1; accept only that
first page-1 snapshot. Compare its seven XFB words against the corrected
`Y0,Cr,Y1,Cb` reference:

```
43d64362 73387347 237123d6 75b27546 bb81bb80 57c857b9 872a879e
```

If a visibly exchanged frame contains these exact words, physical XFB storage
is correct and the fault is in VI/AVE interpretation or state. If its words
instead have Cr/Cb bytes exchanged, the fault occurs before physical XFB
visibility despite the clean source formula. Unload the observer, stop DRM,
restore the saved console loglevel, and require clean legacy recovery with no
fault.

## 2026-08-02: Clean wrong-color frame reads back corrected XFB words

All installed artifacts matched the staged SHA-256 values. With console printk
suppressed before service start, the byte-identical clean module reproduced the
stable wrong-color sequence visually:

```
blue, green, red, light blue, white, magenta, gold
```

This is the exact prior Cr/Cb-exchange signature. The unchanged validated
observer was then loaded after the frame was visibly wrong. TFBL selected page
0 and the first snapshot was:

```
TFBL=100b4c00 BFBL=000b4c28 top=01698000
43d64362 73387347 237123d6 75b27546 bb81bb80 57c857b9 872a879e
```

All seven words exactly match the corrected `Y0,Cr,Y1,Cb` reference despite
the visibly exchanged output. This rules out source palette generation and the
conversion formula for the observed frame.

Important qualification: the observer maps XFB with `MEMREMAP_WB`, so this is
a CPU cache-view readback, not an independent uncached proof of bytes visible to
VI. The result narrows the fault to either dirty/stale physical XFB visibility
or downstream VI/AVE interpretation. Do not yet claim physical RAM is correct.

Preserve this running wrong-color transaction with console loglevel suppressed.
Extend only the observer with an opt-in full selected-page
`flush_dcache_range()` after its pre-flush samples. Loading that observer once
must not reconvert or republish a frame. If the visible colors immediately
become correct, cache-to-VI visibility is proven. If they remain exchanged,
capture post-flush words and investigate VI/AVE state next.

## 2026-08-02: Stage post-visibility selected-page cache flush

- Flush-capable observer: `4e6c6282c`
- `gcn-xfb-observer.ko` SHA-256:
  `45fce0e24c5145547f6a9d7fef5564d97b9d4b9b823123e8a4ed1f2650c11cbf`
- Preserved clean `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`

The clean wrong-color transaction remains active with client PID 25802 and
console loglevel held at 1. Replace only the unloaded observer module. Reconfirm
the frame still shows the exchanged sequence, then send `SIGSTOP` to the exact
client PID so conversion and cursor page flips cannot race the test.

Load the observer once with `flush_selected=1`. It must log the selected page
and seven corrected pre-flush words before flushing and synchronizing the entire
614400-byte page. It does not reconvert source pixels or write VI registers.
Observe whether the frozen visible frame immediately changes from exchanged to
correct colors.

After observation, unload the observer, send `SIGCONT` to the exact client PID,
stop the service normally, and restore saved console loglevel 7. Require exact
legacy recovery and no fault. Do not leave a stopped client for service teardown.

## 2026-08-02: Reject XFB cache visibility as chroma-exchange cause

Immediately before the test, the user reconfirmed the frozen clean frame still
displayed blue, green, red, light blue, white, magenta, and gold. Client PID
25802 entered state `T`, proving it was stopped before the observer ran.

The flush-capable observer selected page 0 and again read the exact corrected
words:

```
TFBL=100b4c00 BFBL=000b4c28 top=01698000
43d64362 73387347 237123d6 75b27546 bb81bb80 57c857b9 872a879e
```

It then executed and synchronized `flush_dcache_range()` over the complete
`0x96000`-byte selected page. No source conversion, page flip, or VI register
write could occur while the client remained stopped. The user observed no
visual change: the same chroma-exchanged colors remained stable.

This rejects dirty or stale CPU-to-XFB cache visibility as the cause. Combined
with the exact selected-page words, the exchange is downstream of XFB storage:
VI register state, field/address interpretation, or AVE encoder state. Do not
add another XFB flush or change the accepted `Y0,Cr,Y1,Cb` conversion.

The observer was unloaded, PID 25802 was resumed before service stop, and the
service terminated that exact PID normally. DRM removal, legacy `gcn-vifb` plus
`gcn_gx` recovery, and restoration of console loglevel 7 all passed without a
fault.

Next capture complete VI register state for one known-correct diagnostic run and
one clean wrong-color run with console printk suppressed, then compare every
register byte-for-byte. If VI state is identical, instrument the AVE I2C state
and ownership handoff rather than revisiting pixel conversion.

## 2026-08-02: Stage paired complete VI register snapshots

- VI-snapshot observer: `0ef6e166f`
- `gcn-xfb-observer.ko` SHA-256:
  `ce611ed0dc11b49f8fd1353c8d8689b0d05fb433a00a78a87eb6d2d2f1b6fc35`
- Clean wrong-color `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Known-correct capture `gcn-drm.ko` SHA-256:
  `a2c8879237efc62c6fe7cf6ff303c042b0eb201073420f4c3d00f8434d9e89c7`

The observer now reads the complete 0x100-byte VI resource in sixteen-byte
groups after its selected-page samples. Run it with `flush_selected=0`; this is
a read-only state comparison.

For each driver artifact separately: suppress console printk before service
start, render the identical deterministic bars, wait for both pages, visually
classify the output, then load the checksum-verified observer once. Save the
complete `VI+00` through `VI+f0` snapshot and selected-page words before
teardown. Restore loglevel and legacy graphics between artifacts.

Compare all stable VI words byte-for-byte. Treat DI IRQ flags and field counters
as volatile unless a stable bit difference repeats. A stable VI difference is
the next targeted state test. If all mode, address, clock, filter, and control
state matches, move downstream to AVE I2C register capture and ownership timing.

## 2026-08-02: Rule out stable VI register state

Two complete service transactions used the exact same clean driver, observer,
client, and service artifacts with console printk suppressed.

The first transaction was visually classified correct and selected page 1. Its
stable VI snapshot was:

```
VI+00 0f060001 476901ad 02e850c0 00030018
VI+10 00020019 410c410c 40ed40ed 100b9700
VI+20 00000000 000b9728 00000000 00a101b3
VI+30 00010001 10f101ae 00000000 00000000
VI+40 00000000 00000000 28500100 1ae771f0
VI+50 0db4a574 00c1188e c4c0cbe2 fcecdecf
VI+60 13130f08 00080c0f 00ff0000 00000001
VI+70 02800000 000000ff 00ff00ff 00ff00ff
```

The second transaction was initially reported while the render command was
still completing, then classified incorrect after the bars were established.
It selected page 0 and showed the exact chroma exchange. Its snapshot was:

```
VI+00 0f060001 476901ad 02e850c0 00030018
VI+10 00020019 410c410c 40ed40ed 100b4c00
VI+20 00000000 000b4c28 00000000 01dd006b
VI+30 00010001 10f101ae 00000000 00000000
VI+40 00000000 00000000 28500100 1ae771f0
VI+50 0db4a574 00c1188e c4c0cbe2 fcecdecf
VI+60 13130f08 00080c0f 00ff0000 00000001
VI+70 02800000 000000ff 00ff00ff 00ff00ff
```

Both selected pages again contained all seven corrected XFB words. The only VI
differences are the expected page-0/page-1 TFBL and BFBL addresses and the live
beam/field counter at `VI+2c`. Every stable mode, filter, clock, control, and
interrupt-programming word is identical. Stable VI register state is therefore
ruled out as the chroma-exchange cause.

Both transactions unloaded the observer, terminated the exact client PID,
removed DRM, restored legacy `gcn-vifb` plus `gcn_gx`, restored console loglevel
7, and logged no fault. Continue downstream with AVE encoder I2C state and
ownership timing; do not modify VI timing or XFB conversion based on this result.

## 2026-08-02: Identify the AVE chroma-exchange control

The next clean-driver run again established the incorrect sequence after the
bars had settled. A source audit then found an exact match for the symptom in
the existing Wii framebuffer driver. `vi_ave_setup()` contains this component
output workaround:

```c
/* clear bit 1 otherwise red and blue get swapped */
if (ctl->has_component_cable)
	vi_ave_out8(client, 0x62, 0);
```

The paired VI snapshots report `VI_SEL=1`, confirming component output. The
modern DRM driver programs VI and XFB but never accesses the AVE encoder, so it
inherits whatever AVE register state firmware or an earlier owner left behind.
That explains why identical corrected XFB bytes and identical stable VI state
can produce either correct colors or the exact red/blue and Cb/Cr exchange.

The modern kernel cannot yet test this register: Wii DTS still comments out its
GPIO-backed AVE I2C bus using an obsolete pre-standard binding, and the running
system exposes no I2C adapter. Restore that node with the current `i2c-gpio`
binding first. The positive-control experiment is then to read AVE register
`0x62` on an established wrong frame and write zero without touching XFB or VI.
An immediate visible correction would isolate the fault to AVE state and define
the required DRM ownership fix.

## 2026-08-02: Stage direct AVE register 0x62 positive control

- AVE GPIO-I2C implementation: `aac7a314b`
- `zImage` SHA-256:
  `b55d13f13183faec81ce426632d75737353b46918856cb71068846b4b5780aa8`
- `dtbImage.wii` SHA-256:
  `b55d13f13183faec81ce426632d75737353b46918856cb71068846b4b5780aa8`
- `wii-ave-reg` SHA-256:
  `e7ca238709431579abc5a2355a04ef38fd801daf2b51576077c8381be68cfad2`

The Wii DTS now places a standard `i2c-gpio` adapter directly below the
Hollywood platform bus, where `wii_device_probe()` will populate it. GPIO 15 is
SDA and GPIO 14 is output-only SCL; both use open-drain semantics and a
two-microsecond delay matching the old 250 kHz target. The AVE remains declared
at address `0x70` without a bound kernel driver.

Boot this exact image and validate the test apparatus before drawing a hardware
conclusion. Require `/dev/i2c-0`, the `i2c-gpio` adapter in sysfs, and an AVE
register read that completes without changing the visible frame. Run the clean
DRM bars until the exact exchanged sequence is visually established, then run:

```
wii-ave-reg /dev/i2c-0
```

Record the pre-write value as the measurement positive control. Without
rerendering, page flipping, or writing VI/XFB, run:

```
wii-ave-reg /dev/i2c-0 --clear-swap
```

The utility reads register `0x62`, writes only `0x00`, and reads it back. If the
frozen or stable wrong-color frame immediately becomes the correct
red/green/blue/gold sequence, the AVE state is the proven cause. If it does not,
record both register values and the unchanged visual sequence; do not expand to
the full legacy AVE initialization sequence without another isolated test.

## 2026-08-02: AVE bus enumerates; reject dynamically linked test utility

The checksum-verified image booted normally. The restored bus produced
`/dev/i2c-0`, the declared client appeared as `0-0070`, and the kernel logged:

```
i2c-gpio hollywood:i2c-video: using lines 527 (SDA) and 526 (SCL, no clock stretching)
```

This validates DTS placement and platform-device population. The accompanying
warning that SCL cannot be read is expected for the deliberately output-only
clock line and matches the old `no-clock-stretching` declaration.

The first read-only utility invocation failed in the dynamic loader before
`main()` because the cross-toolchain binary requires `GLIBC_2.34`, which the Wii
rootfs does not provide. Return code was 1 and no I2C transaction occurred.
Therefore this attempt says nothing about AVE register `0x62` and must not be
counted as either a positive or negative hardware result. Rebuild the narrow
utility as a static executable, checksum-stage it separately, and repeat the
read-only positive control before any write.

## 2026-08-02: Stage static AVE register utility

- Static-link fix: `f45c1d97a`
- Static `wii-ave-reg` SHA-256:
  `5eeff399a163848d6957a788e81a9ccbcbac6f1cfb68864bc3ed2d636b816667`

The rebuilt PowerPC executable is statically linked and has no ELF interpreter,
removing the rootfs glibc dependency that invalidated the first deployment. The
Wii remains booted on the already validated `b55d13f1...` image with `/dev/i2c-0`
and client `0-0070` present. Replace only `/usr/local/sbin/wii-ave-reg`, verify
the installed checksum, and repeat the read-only invocation. Do not pass
`--clear-swap` until the read transaction succeeds and reports register `0x62`.

## 2026-08-02: AVE register read reaches I2C but bus clock is held low

The installed static utility matched its staged SHA-256 and executed without a
loader dependency. Its read-only combined transaction reached `/dev/i2c-0` but
failed with `ENXIO` (`No such device or address`), so the AVE did not acknowledge
address `0x70`. No register write was attempted and this is not evidence for or
against the value or effect of register `0x62`.

The GPIO debug positive control found the electrical failure both before and
after the read attempt:

```
gpio-14 (AVE_SCL | scl) out lo
gpio-15 (AVE_SDA | sda) out hi
```

An idle I2C clock cannot remain low. This explains the NACK and rejects the
current standard open-drain DTS description as a functional port of the old Wii
bus behavior.

The historical Wii `i2c-gpio` implementation used behavior not expressible by
the current standard binding: SCL remained an output and was actively written
high/low, while SDA used `sda-enforce-dir` to return to output mode for writes
and switch to input specifically for ACK/data reads. The next isolated change
should implement a small Wii AVE bit-bang adapter using `i2c-algo-bit` and those
exact line operations, rather than guessing AVE registers or broadening the DRM
change. Its positive control is an idle-high SCL in debugfs followed by a
successful read-only `0x62` transaction. Only then repeat `--clear-swap` on a
visibly wrong frame.

## 2026-08-02: Stage Wii-specific AVE bit-bang adapter

- Wii AVE adapter implementation: `2f1399b6c`
- `zImage` SHA-256:
  `b8b0c3f54c4f5616cbf8d32ba099390beb5afdd83bae93652abcbcaebf083af8`
- `dtbImage.wii` SHA-256:
  `b8b0c3f54c4f5616cbf8d32ba099390beb5afdd83bae93652abcbcaebf083af8`
- Static `wii-ave-reg` SHA-256:
  `5eeff399a163848d6957a788e81a9ccbcbac6f1cfb68864bc3ed2d636b816667`

The dedicated adapter ports the historical Wii line operations onto the modern
GPIO descriptor and `i2c-algo-bit` APIs. SCL remains an output and is actively
written high or low. SDA remains an output while sending, changes to input for
ACK/data reads, and returns to output on the next write. Both lines are forced
high after adapter registration. The generic open-drain flags and properties
that left SCL low are no longer present in Wii DTS.

The driver, binding, configuration, DTS, and complete Wii wrapper compile
cleanly; `git diff --check` and strict `checkpatch.pl` pass. The generated DTB
contains both `nintendo,wii-ave-i2c` and the AVE child at `0x70`.
`dt_binding_check` could not run because the host lacks the external `dtschema`
tool `dt-doc-validate`; record this as a host validation gap, not a schema pass.

Boot the exact image and require these gates in order before starting DRM:

1. `/dev/i2c-0` and client `0-0070` exist, the dedicated adapter logs successful
   registration, and debugfs reports both AVE_SCL and AVE_SDA `out hi` before
   any transfer.
2. The checksum-verified static utility reads register `0x62` without `ENXIO`.
   Record the value and require AVE_SCL to remain `out hi` after the transfer.

Only after both controls pass should the clean DRM bars be rendered and
visually classified. On an established exchanged frame, run one
`--clear-swap` transaction and record the before/after register values plus the
immediate visual result. Do not run the write when colors are already correct;
that would not validate the symptom transition.

## 2026-08-02: Validate Wii-specific AVE bus and read register 0x62

The deployed boot image matched the staged `b8b0c3f5...` SHA-256. Before any
userspace I2C transaction, the dedicated adapter registered `/dev/i2c-0` and
client `0-0070`, then debugfs reported the required idle state:

```
gpio-14 (AVE_SCL | scl) out hi
gpio-15 (AVE_SDA | sda) out hi
```

This is the direct positive control that failed with generic `i2c-gpio`; active
SCL and historical SDA direction handling are now validated on hardware.

The installed static utility matched SHA-256 `5eeff399...` and its read-only
combined transaction completed successfully:

```
AVE[0x62] before: 0x00
rc=0
```

Both GPIOs returned to `out hi` afterward. The AVE therefore acknowledges
address `0x70`, and the full register-pointer/write plus repeated-start/read
path works. Keep the dedicated adapter.

Register `0x62` was already zero before DRM started, so do not credit that value
with fixing or causing the earlier nondeterminism yet. Start the checksum-known
clean DRM path and render the deterministic bars without touching AVE. If the
frame is visibly exchanged while `0x62` still reads zero, the legacy comment is
not sufficient to explain this modern failure and another AVE register or
ownership sequence must be isolated. If colors are correct, repeat clean DRM
transactions while reading `0x62` before each run; do not write zero to an
already-correct frame.

## 2026-08-02: First clean DRM run is correct with AVE 0x62 zero

The first service start did not reach DRM rendering: udev had preloaded the
out-of-tree `gcn_drm` module while legacy gcnfb still owned VI, leaving the
module loaded but unbound. The service unbound legacy and called `modprobe`,
which could not reprobe an already-loaded module, then restored legacy and
removed the stale module. No bars were rendered and this operational failure is
not a color result.

The second start loaded the same checksum-known clean module fresh and bound
successfully. The deterministic bars settled in the expected semantic order:

```
red, green, blue, gold, white, magenta, teal
```

The user visually classified all colors correct. A read-only AVE transaction
during the stable frame again returned `AVE[0x62] before: 0x00`. No AVE write
was issued because writing zero to an already-correct frame would not validate
a transition.

Teardown stopped exact client PID 3933, removed DRM, restored legacy gcnfb plus
generated GX, restored console loglevel 7, and left both AVE GPIOs `out hi`.
There was no fault. This single run correlates AVE `0x62=0` with correct output
but does not yet explain the prior nondeterminism. Repeat complete clean DRM
transactions without AVE writes and classify each settled frame. Also fix the
service to remove an unbound preloaded `gcn_drm` before ownership transfer, but
keep that operational change separate from color-state conclusions.

## 2026-08-02: Rule out AVE register 0x62 as sole color-state cause

A second independent clean DRM transaction started from restored legacy state
with the same checksum-known kernel, DRM module, client, and service. Read-only
AVE measurements returned `0x00` both immediately before ownership transfer and
again after the deterministic bars had settled.

Unlike the preceding correct run, the user classified this frame as the exact
known exchange signature:

```
blue, green, red, light blue, white, magenta, gold
```

The expected semantic order remained red, green, blue, gold, white, magenta,
and teal. Client PID 4275 was stopped to preserve the exact wrong frame, and a
third read-only measurement still returned `AVE[0x62] before: 0x00`; both AVE
GPIOs remained `out hi`. No register write was issued because writing zero over
zero cannot validate a state transition.

The client was resumed before teardown. Service stop removed DRM, restored
legacy gcnfb plus generated GX, restored console loglevel 7, and left both AVE
lines high. Filesystems were synchronized and the Wii powered off cleanly.

Identical AVE register `0x62=0` now correlates with both one correct and one
wrong frame from consecutive clean DRM transactions. This definitively rules
out that register as the sole source of the nondeterminism despite the legacy
driver comment. Keep the validated AVE bus and next capture a controlled set of
other readable AVE configuration registers for paired correct and wrong runs;
do not revisit XFB bytes, stable VI state, or write `0x62` again.

## 2026-08-02: Qualify the AVE 0x62 readback conclusion

A follow-up source audit found no evidence that AVE register `0x62` is a
readable reflection of the encoder's effective swap latch. The legacy driver
only writes the register and never reads it. Therefore the identical zero
readbacks definitively reject `0x62` as a useful readable state discriminator,
but they do not yet prove that writing the register cannot control an internal
or write-only latch.

Narrow the next test instead of broad-dumping undocumented AVE registers. Extend
the utility with an explicit `--set-swap` action that writes only `0x02` to
register `0x62`; retain `--clear-swap` as the mandatory restoration write of
`0x00`. On one visually correct, frozen DRM frame, write `0x02` and observe
whether red/blue plus cyan/gold exchange immediately. Then write `0x00`
regardless of the visual outcome and require the original correct frame to
return or remain unchanged. This reversible positive control directly tests the
legacy comment without depending on register readback semantics.

## 2026-08-02: Stage reversible AVE 0x62 write control

- Reversible utility implementation: `c6bd58d73`
- Static `wii-ave-reg` SHA-256:
  `aab69bc762c226d1a6b82b341b1a43cbcc8e1d553b0c9bc1b77323af2fb74932`

The utility retains read-only mode and the existing `--clear-swap` write of
`0x00`, and adds one explicit `--set-swap` write of `0x02`. It is statically
linked, compiles with `-Wall -Wextra -Werror`, contains no ELF interpreter, and
rejects extra arguments. No kernel or DRM artifact changes in this test.

Deploy and verify only this binary. Start clean DRM transactions until one frame
is visually classified correct, then stop the exact client PID with `SIGSTOP`
so no conversion or page flip can race the encoder test. Run `--set-swap` once
and classify the unchanged frozen frame. Regardless of that result, run
`--clear-swap` once and require the original correct colors to return or remain
unchanged before resuming the client. If either command fails, attempt the clear
again before any teardown. Never leave register `0x62` intentionally set to
`0x02` across service stop or reboot.

## 2026-08-02: Validate AVE 0x62 as the live chroma-swap control

The deployed utility matched staged SHA-256 `aab69bc...`. A fresh clean DRM
transaction produced a visually correct deterministic frame while register
`0x62` read `0x00`. Exact client PID 10595 was stopped and confirmed in state
`T`, preserving one XFB, VI configuration, and DRM frame for the entire test.

Writing only `0x02` produced this immediate visible sequence on that unchanged
frame:

```
blue, green, red, light blue, white, magenta, orange
```

The utility read the register back as `0x02`. Writing only `0x00` then restored
the original correct red, green, blue, gold, white, magenta, and teal colors
immediately, and readback returned `0x00`. No rendering, page flip, XFB write,
or VI write could occur while the client remained stopped.

This is a successful positive and restoration control: AVE register `0x62` bit
1 directly causes the exact downstream red/blue plus teal/gold exchange. The
client was resumed before service stop; legacy gcnfb plus generated GX,
console loglevel 7, and final `AVE[0x62]=0x00` were all restored without fault.

The earlier naturally wrong frozen frame still read `0x00`, so software must
not treat a zero read as proof that the encoder's effective path is already
correct. The next production test should unconditionally write `0x00` whenever
DRM acquires VI/AVE ownership, before exposing the first frame, and verify
repeatable correct colors across multiple complete transactions. Keep the
dedicated AVE I2C adapter and remove the experimental `--set-swap` path after
the production write has passed its repeatability tests.

## 2026-08-02: Stage unconditional AVE chroma-swap clear on DRM ownership

- Implementation commit: `7ef1ccbec`
- `zImage` and `dtbImage.wii` SHA-256:
  `b39cdf270d8c0a960ce9b47f53845137106dadb373e4d2a6ccc61cfec8392505`
- `gcn-drm.ko` SHA-256:
  `7fa35c1812d5339d3b4f780b87768d3cffff61439ecb7f3dff612bfcc9aba83f`

The Wii VI node now references the AVE I2C client through an
`audio-video-encoder` phandle. During each Hollywood DRM probe, after mapping
VI/XFB resources but before programming the video mode or registering DRM, the
driver resolves that client and unconditionally writes `0x00` to AVE register
`0x62`. Probe defers if the adapter/client is not ready and fails on an I2C
error or short transfer. Flipper remains unaffected because its node has no AVE
phandle.

Deploy and checksum-verify both the kernel image and module. After reboot,
perform at least three independent complete DRM ownership transactions. Before
each start, remove any stale, unbound `gcn_drm` module that udev preloaded while
legacy gcnfb still owned VI. Require the driver log
`cleared AVE chroma-swap control` and visually correct red, green, blue, gold,
white, magenta, and teal output on every transaction. A readback of zero is
supporting evidence only; the full-frame visual classification is the deciding
result. Restore the legacy console and console loglevel after every run.

## 2026-08-03: Reject probe-time AVE clear placement

The deployed kernel and module matched their staged SHA-256 values. The AVE
adapter and client enumerated normally, `/boot` remained read-only, and a
read-only measurement returned register `0x62=0x00`. After removing the stale,
unbound udev-loaded `gcn_drm`, the service transferred VI ownership and the new
driver logged `cleared AVE chroma-swap control` before starting the live RGB565
console client.

The deterministic seven-bar fixture nevertheless displayed:

```
blue, green, red, light blue, white, magenta, gold
```

This is the exact known AVE chroma-swap signature rather than the expected red,
green, blue, gold, white, magenta, and teal sequence. A successful I2C write
during probe therefore does not guarantee the effective encoder state by the
time the first modeset reaches scanout.

The exact client PID was stopped to preserve the wrong frame for a post-modeset
clear control, but the utility was accidentally invoked as
`wii-ave-reg --clear-swap`. Its parser treated `--clear-swap` as an I2C device
path and made no transfer. Do not classify that as a hardware result. The
required invocation is `wii-ave-reg /dev/i2c-0 --clear-swap`.

Before pausing, the client was resumed, service stop restored legacy gcnfb plus
generated GX, console loglevel 7 was restored, filesystems were synchronized,
and the Wii powered off cleanly. Repeat the same frozen wrong-frame control
with the correct utility invocation before moving the driver write. If a
post-modeset zero write immediately restores the intended colors, relocate or
repeat the production clear at a lifecycle point after VI mode programming and
before the first visible framebuffer update.

## 2026-08-03: Validate AVE bit as a reversible compensator, not a fixed state

A fresh transaction reused the exact staged kernel, module, console, service,
and deterministic bars. Probe again logged its unconditional `0x62=0` write,
but the established frame reproduced the natural wrong sequence:

```
blue, green, red, light blue, white, magenta, gold
```

Client PID 2294 was stopped and confirmed in state `T`, preserving the exact
XFB, page, VI state, and displayed frame. Repeating a zero write with the
correct installed utility path and argument order completed successfully,
read back zero, and produced no visible change. This rejects both the original
probe placement and a simple post-modeset repetition of the same constant.

With the client still stopped, writing only `0x02` to AVE register `0x62`
immediately changed that same frozen wrong frame to the correct red, green,
blue, gold, white, magenta, and teal colors. Writing `0x00` again immediately
returned the unchanged frozen frame to the wrong sequence. Register readback
tracked each explicit value.

This is a second reversible positive control and, combined with the earlier
correct-frame control, changes the interpretation. AVE bit 1 deterministically
exchanges the two chroma channels, but the required setting depends on a
nondeterministic upstream chroma interpretation: an initially correct frame
requires zero, while this initially exchanged frame requires two. Therefore no
unconditional constant written at probe or after modeset can fix both states.
The AVE register is a diagnostic compensator, not yet the root cause.

The register was restored to zero, the client resumed, and service stop
restored legacy gcnfb plus generated GX and console loglevel 7 without fault.
Before modifying production code again, repeat one complete exact-artifact
transaction and frozen-frame toggle to confirm this inverse behavior. Then
identify a readable or controllable upstream state that predicts which AVE bit
setting is required; corrected XFB contents and stable VI MMIO state are
already ruled out by prior paired captures.

## 2026-08-03: Reproduce inverse AVE compensation on an independent transaction

An independent complete service transaction reused every installed artifact
without rebuilding or redeploying. Probe again cleared AVE register `0x62`, and
the deterministic bars again settled in the exact natural wrong order:

```
blue, green, red, light blue, white, magenta, gold
```

Client PID 3518 was stopped and confirmed in state `T`. With that frame frozen,
writing `0x02` immediately corrected all seven colors. Writing `0x00` again
immediately returned all seven colors to the same wrong sequence. The utility
read back the requested value after each transfer. This independently
reproduces the complete wrong-to-correct-to-wrong reversal without a page flip,
XFB write, VI update, or client execution.

The result confirms that AVE bit 1 is a reliable chroma-exchange control but a
fixed zero is not a production solution. The natural upstream state selected
the opposite relationship on two consecutive fresh transactions, both despite
the probe-time clear. Remove the unconditional probe clear before merging any
production code unless a later deterministic initialization sequence makes its
required value invariant.

The register ended at zero. The client resumed before service stop; DRM was
removed, legacy gcnfb plus generated GX returned, console loglevel 7 was
restored, and no fault occurred. Next inspect AVE initialization and readable
state beyond register `0x62`, comparing one firmware/legacy-owned baseline with
the state immediately after DRM ownership. Avoid broad register writes: gather
read-only data first and require a positive control for any candidate bit.

## 2026-08-03: Stage whitelisted read-only AVE state snapshots

- Diagnostic implementation: `2e9402343`
- Static `wii-ave-reg` SHA-256:
  `f89d236631c65587cc61a1217c13807ce083fc67790f1c955e5d6111ab5a0451`
- Current libogc reference commit:
  `99929510c8dd5b0dd69aaae26a93105f09d182de`

Current upstream libogc `VIDEO_Init()` still performs a complete AVE setup. Of
particular interest, it writes oversampling register `0x65=3`; the imported
legacy Linux sequence writes `0x65=1`, while the modern DRM ownership path
initializes neither. This is a candidate difference, not yet a cause.

The utility adds `--dump-state`, which performs only one-byte reads from an
explicit whitelist of registers touched by the known initialization sequence:
`00`, `01`, `02`, `03`, `04`, `05`, `06`, `08`, `09`, `0a`, `62`, `65`, `6a`,
`6e`, `71`, `72`, and `7a` through `7d`. It makes no register write. Existing
default, `--clear-swap`, and `--set-swap` behavior is unchanged.

Deploy and checksum-verify only the utility. Before starting DRM, capture two
consecutive legacy-state dumps. Require `AVE[0x62]` to match the established
single-register read, require both dumps to be byte-identical, and require no
visible display change. Those are the positive and non-perturbation controls.

Then capture one dump only after the deterministic DRM bars have settled and
been visually classified, without writing `0x62`. Compare the complete output
to the legacy baseline. Repeat fresh exact-artifact transactions until both a
naturally correct and naturally exchanged frame have been captured, or until a
reasonable run limit establishes that the current build no longer produces one
state. A candidate register matters only if its value reproducibly predicts the
visual state; do not write `0x65` or broaden the register list from a single
correlation.

## 2026-08-03: Readable AVE setup state matches on a swapped frame

The installed utility matched staged SHA-256 `f89d2366...`. In legacy state,
the standalone `0x62` read returned zero and two consecutive complete dumps
were byte-identical. The read-only positive control therefore passed. The
stable legacy snapshot was:

```
00=00 01=22 02=07 03=01 04=01 05=00 06=00 08=00 09=00 0a=00
62=00 65=01 6a=01 6e=00 71=8e 72=8e 7a=00 7b=00 7c=00 7d=00
```

This coherently reflects component/PAL selection and the historical Linux AVE
setup, including oversampling register `0x65=1`. The reads caused no reported
display change.

The next DRM transaction naturally displayed the exact swapped sequence.
Client PID 7097 was stopped and confirmed in state `T` before one dump. Every
whitelisted byte was identical to both legacy baselines; `diff` produced no
output. The readable setup state, including candidate register `0x65`, cannot
discriminate this wrong frame and does not justify a write test.

Two further complete transactions with the same artifacts also started
swapped, for three consecutive swapped starts. No duplicate dump was taken.
This run did not capture a naturally correct frame, so it does not prove that
all readable AVE state is identical across both visual outcomes.

An important artifact correlation now needs a direct control: the earlier
naturally correct frozen-frame test used clean module SHA-256 `f4aae461...`
without any AVE write, while all starts in this session used module SHA-256
`7fa35c18...` with the probe-time zero write and were swapped. Test the preserved
no-write module under the same current kernel and AVE adapter before adding
more diagnostics. If correct output returns, the probe-time transfer itself or
its ordering perturbs hidden AVE state despite writing the value already read
back. Treat this as a hypothesis until the exact module rollback control runs.

Each transaction stopped its exact client, removed DRM, restored legacy gcnfb
plus generated GX and console loglevel 7, and ended with `AVE[0x62]=0` and no
fault.

## 2026-08-03: Stage no-AVE-write module rollback control

- Current kernel image SHA-256:
  `b39cdf270d8c0a960ce9b47f53845137106dadb373e4d2a6ccc61cfec8392505`
- No-AVE-write `gcn-drm.ko` SHA-256:
  `f4aae461c3fe31fbc38bd6cc7ffedd715bf105d0d0e36a050d55652610c8f78d`
- Probe-write `gcn-drm.ko` SHA-256 being replaced:
  `7fa35c1812d5339d3b4f780b87768d3cffff61439ecb7f3dff612bfcc9aba83f`

Source comparison confirms that implementation `7ef1ccbec` adds only the AVE
phandle lookup, the two-byte `0x62=0` I2C transfer, and its probe call to the DRM
module. Rendering, XFB conversion, VI programming, KMS, and client behavior are
unchanged. The current kernel may retain its AVE phandle and dedicated adapter;
the older module simply ignores them.

Checksum-verify both preserved modules on the Wii, preserve the currently
installed probe-write module, and install the no-write module. Perform three
complete service start/bar-classification/stop transactions without any AVE
userspace write. Keep printk suppressed while each fixture is active and
restore legacy ownership between runs.

If the no-write module produces correct output after three consecutive wrong
starts from the probe-write module, freeze the first correct frame and capture
one read-only whitelisted AVE dump. Multiple correct no-write starts would
support the hypothesis that the early I2C transfer itself or its ordering
selects the wrong hidden encoder phase. Mixed or still-wrong no-write starts
reject that simple causal claim. Restore the probe-write module on disk after
the control regardless of outcome; do not reboot or modify the kernel image.

## 2026-08-03: Probe-time zero transfer causes the swapped state

Both remote modules and the current kernel matched their staged SHA-256 values.
Only `gcn-drm.ko` changed during this control; the kernel, DTB, AVE adapter,
console client, service, XFB contents, and deterministic fixture remained the
same.

The probe-write module had produced three consecutive naturally swapped starts
immediately before the rollback. Replacing it with the preserved no-AVE-write
module produced six consecutive complete transactions in which the user
classified all seven colors correct. Every transaction passed through a full
DRM stop, legacy gcnfb/generated-GX restoration, and fresh DRM acquisition.

On the first correct no-write transaction, client PID 8759 was stopped and one
read-only AVE snapshot was captured. It was byte-identical to the prior frozen
wrong probe-write snapshot and both legacy baselines. Thus the correct and wrong
frames have the same corrected XFB words, stable VI MMIO state, and all 20
whitelisted readable AVE bytes, including `0x62=0` and `0x65=1`.

This A/B result establishes that the early `0x62=0` I2C transfer or its exact
probe ordering selects the wrong hidden encoder phase under the current kernel,
even though it writes the value already returned by readback. The transfer must
be removed; moving or repeating the same constant is already ruled out by the
frozen post-modeset control. Do not infer ordinary register idempotence for this
undocumented AVE interface.

Important scope: older clean-module runs before this exact kernel/adapter state
reported both visual outcomes, so six current passes do not by themselves
close every historical nondeterminism report. They do provide a strict
production regression gate: revert implementation `7ef1ccbec`, rebuild the
kernel/module, and require repeated correct cold ownership transactions before
acceptance.

After transaction 6, service stop restored legacy graphics and console loglevel
7, AVE readback was zero, and no fault occurred. The probe-write module was
restored on disk as required by the staged control; the next deployment must
replace it with the source-reverted build.

## 2026-08-03: Stage production rollback of harmful AVE probe write

- Rollback implementation: `d8e005571`
- `zImage` and `dtbImage.wii` SHA-256:
  `b8b0c3f54c4f5616cbf8d32ba099390beb5afdd83bae93652abcbcaebf083af8`
- Rebuilt in-tree `gcn-drm.ko` SHA-256:
  `1a8456d3f475986060beb0f07d666ea5e3cad38901eefa23a915d50de1476bf1`
- Module `.text` SHA-256:
  `74a37a5ccc923fcae7f8c944a218a2114c8090ee11cb2b2b018cb0b1ba651f1a`

The rollback removes exactly implementation `7ef1ccbec`: AVE lookup/write code,
its probe call, the DRM I2C dependency, and the now-unused VI-to-AVE phandle.
The dedicated Wii AVE adapter, client node, and read-only utility remain.

The image exactly reproduces the previously validated dedicated-adapter image
hash. The rebuilt module's complete SHA-256 differs from the six-pass rollback
artifact `f4aae461...` only because the normal in-tree build adds `intree=Y` to
`.modinfo`; both modules have byte-identical executable `.text` with the hash
above. The full `-j16` image/module build passed, the scoped strict checkpatch
reported zero findings, and `git diff --check` passed.

Deploy and checksum-verify both artifacts, preserve the current probe-write
rollback copies, restore `/boot` read-only, and reboot. After boot, require the
AVE adapter/client and utility read positive controls, then run at least three
complete service-managed deterministic bar transactions with full legacy
restoration between each. All seven colors must remain correct and no
`cleared AVE chroma-swap control` line may appear. Any swapped start fails the
production regression despite the six-pass text-identical module control.

## 2026-08-03: Production rollback fails cold-boot color regression

The deployed image, rebuilt in-tree module, and diagnostic utility matched all
staged SHA-256 values after reboot. `/boot` remained read-only, the AVE client
responded with `0x62=0`, legacy gcnfb plus generated GX owned VI, and no
`cleared AVE chroma-swap control` log appeared.

After removing the expected stale unbound udev preload, three complete
service-managed DRM transactions each displayed the exact swapped sequence:

```
blue, green, red, light blue, white, magenta, gold
```

No AVE userspace write occurred. Full service stop and legacy restoration
between transactions did not change the result. This fails the production
acceptance gate.

The contrast is now specific: the same no-write executable module text was
correct for six consecutive transactions in the preceding warm session, but
the committed cold boot selected a wrong state that persisted for all three
ownership cycles. The state is therefore not simply random on every DRM probe;
it can be selected earlier and survive driver unbind/rebind plus legacy
restoration. The harmful probe-time zero transfer remains correctly reverted,
because its direct A/B was independently three swapped versus six correct, but
that rollback is not the complete fix.

Every failed transaction terminated its exact client, removed DRM, restored
legacy gcnfb/generated GX and console loglevel 7, and logged no fault. The
committed rollback artifacts remain installed.

The next isolated reset candidate is AVE oversampling register `0x65`. Current
libogc writes value 3 during `VIDEO_Init()`, while the inherited legacy state is
1 in both correct and wrong readable snapshots. Add explicit reversible utility
actions for `0x65=3` and restoration to `0x65=1`. On one frozen wrong frame,
write 3 and classify the unchanged output, then restore 1 regardless of result.
Do not expand to the complete AVE magic sequence unless this single-register
control is negative.

## 2026-08-03: Stage reversible AVE oversampling control

- Utility implementation: `6ec1d1e32`
- Static `wii-ave-reg` SHA-256:
  `2ca24d201ba5a8053bc1f6d85caf00dc2fe50a530ec3cc9b4040136d2a601dd9`

The utility adds two explicit actions for only AVE register `0x65`:
`--set-oversampling-3` writes current libogc's initialization value, and
`--set-oversampling-1` restores the inherited historical Linux value. Each
action reads before the write, performs one byte write, and reads afterward.
All existing read, dump, and `0x62` controls remain unchanged.

Deploy and checksum-verify only the utility. Start the committed no-write DRM
build, reproduce and visually classify the persistent swapped bars, then stop
the exact client PID in state `T`. Confirm `0x65=1` with a read-only dump before
any write. Write `0x65=3` once and classify the unchanged frozen frame. Then
write `0x65=1` once regardless of the first visual result and classify it again.

Success requires a reversible, reproducible color transition attributable to
these writes alone. Readback changes without a visible transition are a valid
negative result. Resume the client before service stop and leave register
`0x65=1`; never tear down or reboot with the experimental value 3 intentionally
active.

## 2026-08-03: Reject AVE oversampling as the hidden color-phase control

The deployed utility matched the staged SHA-256
`2ca24d201ba5a8053bc1f6d85caf00dc2fe50a530ec3cc9b4040136d2a601dd9`.
Its read-only baseline again reported `0x62=0` and `0x65=1`.

The first complete no-write DRM transaction displayed the correct seven-color
sequence with `0x65=1`; no AVE write was performed on that correct frame. After
a clean service stop and legacy restoration, the second transaction naturally
displayed the established swapped sequence with the same readable state:

```
blue, green, red, light blue, white, magenta, gold
```

The exact renderer PID 7535 was stopped and verified in state `T`, freezing the
source framebuffer and all client updates. A read-only dump immediately before
the experiment confirmed `0x62=0` and `0x65=1`. Writing only AVE register
`0x65=3` succeeded and read back as 3, but the frozen bars remained swapped.
Writing only `0x65=1` succeeded and read back as 1, and the same frozen bars
again remained swapped. This is a valid reversible register-write positive
control and a visual negative result.

AVE oversampling register `0x65` therefore does not select or compensate the
hidden chroma phase by itself. Keep the inherited value 1 and reject repeated
single-register oversampling tests. The renderer was resumed before teardown;
the service restored legacy `gcnfb`/generated GX, removed DRM, restored console
loglevel 7, and left `0x62=0`, `0x65=1`.

The next controlled reset candidate is the complete current-libogc AVE
initialization sequence, treated as one known reference operation rather than
independent speculative register writes. Audit and encode the sequence from the
checked-out libogc source, provide a reversible legacy restoration operation,
and validate both paths on a frozen naturally swapped frame before integrating
anything into kernel ownership transfer.

## 2026-08-03: Stage reversible current-libogc AVE reset

- Utility implementation: `2b46601d5`
- Audited libogc source: `99929510c8dd5b0dd69aaae26a93105f09d182de`
- Static `wii-ave-reg` SHA-256:
  `60441bb5cac230fed28c1ee07f7a73c1786fefa81bb6f339ec9aa1a17c614046`

The new `--apply-libogc-ntsc SNAPSHOT` action first reads every AVE byte
touched by libogc's `__VISetupEncoder()`, creates the snapshot with `O_EXCL`,
writes all 264 bytes, and fsyncs it before changing hardware. It then reproduces
libogc's ordered grouped transfers and delays for the Wii's current NTSC
component/DTV test configuration: encoder enable, oversampling 3, YUV selection
`0x20`, display controls, `0x8e8e` levels, zeroed 26-byte Macrovision block,
33-byte gamma table, and final EURGB60 filter disable. It reads and verifies
every touched byte. AVE chroma control `0x62` is not part of this sequence and
is not written.

`--restore-state SNAPSHOT` validates the snapshot header and exact size, replays
the same grouped register layout using the captured pre-test bytes, and verifies
every restored byte. A host emulated AVE register file validated exclusive
snapshot creation, exact apply readback, and byte-for-byte restoration. The
binary is a stripped static 32-bit big-endian PowerPC executable with no ELF
interpreter; strict checkpatch reports zero errors and warnings.

Deploy and checksum-verify only this utility. Confirm legacy graphics and
`0x62=0`, `0x65=1`, remove any obsolete `/run/wii-ave-libogc.snapshot` only
before the experiment, and start the committed no-write DRM path. Render the
identical seven bars until one transaction naturally displays the established
swapped order. Stop that exact renderer PID and verify state `T`, then run:

```
/usr/local/sbin/wii-ave-reg /dev/i2c-0 \
  --apply-libogc-ntsc /run/wii-ave-libogc.snapshot
```

Classify the unchanged frozen frame as correct, swapped, changed another way,
or signal lost. Whether the action succeeds, fails verification, or loses
signal, immediately run the restore action against the created snapshot and
classify the frozen frame again. Do not resume the client or tear down DRM until
restoration reports full readback success. If no snapshot was created, the
utility made no AVE writes and the test must stop without claiming a result.

A correct transition after apply, followed by return to swapped after restore,
is the required reversible positive result. Successful apply readback with no
visual transition is a valid negative. After verified restoration, require
`0x62=0`, `0x65=1`, resume the exact client, stop the service, restore legacy
graphics and console loglevel 7, and preserve the snapshot with the hardware
log until the result is committed.

## 2026-08-03: Reject byte-for-byte AVE snapshot verification

The deployed utility matched the staged SHA-256. The first service start hit
the known stale unbound `gcn_drm` preload and restored legacy without rendering;
the second start bound normally. Its deterministic bars displayed the natural
swapped sequence at `0x62=0`, `0x65=1`. Exact renderer PID 10623 was stopped and
verified in state `T` before the broad AVE operation.

Snapshot creation succeeded and produced a 264-byte file with SHA-256
`dd0c565326ed89e5b89b5412435162a9c418d01818761a26d2a0676d0f228a8d`.
The complete grouped libogc write sequence completed, but strict verification
failed first at register `0x40`: zero was written and `0xff` read back. The
automatic full snapshot replay then completed, but restoration verification
failed at register `0x04`: captured value 1 was written and value 0 read back.

A subsequent read-only scalar dump showed every previously tracked register at
its pre-test value except `0x04=0`; in particular `0x01=0x22`, `0x62=0`, and
`0x65=1`. The display had blanked before a reliable post-apply visual
classification, and an attempted identical fixture redraw occurred only after
the restore replay. This run therefore provides no valid color result for the
full libogc sequence.

Preserve the real finding: the AVE register file cannot be modeled as ordinary
byte-addressable storage. At least the Macrovision range beginning at `0x40`
does not return written data, and `0x04` behaves as a command/commit register or
otherwise self-clears after use. Reject full byte-for-byte apply and restore
verification, and do not repeat this utility action unchanged.

The failed snapshot and dmesg were preserved on the Wii as
`/root/20260803-libogc-reset-failed.snapshot` and
`/root/20260803-libogc-reset-failed.dmesg.txt`. The renderer was resumed before
service stop, legacy graphics and console loglevel 7 were restored, and the Wii
was rebooted rather than claiming same-session restoration. After reboot the
clean baseline returned with `0x04=1`, `0x01=0x22`, `0x62=0`, and `0x65=1`.

Any next full-sequence test must classify command-style ranges separately:
verify only stable scalar registers, regard successful I2C completion as the
positive control for the two block writes and command latch, and use a reboot
as the recovery boundary. A same-session snapshot replay is not proven capable
of restoring hidden encoder command state.

## 2026-08-03: Stage reboot-bounded current-libogc AVE reset

- Utility implementation: `dc2eeeca6`
- Audited libogc source: `99929510c8dd5b0dd69aaae26a93105f09d182de`
- Static `wii-ave-reg` SHA-256:
  `0d31ad651f0a6b25bb802704fd7751bb7f091e5ea21819f66d918328a4cad44b`

The corrected utility removes the callable snapshot replay action. The
exclusive fsynced 264-byte pre-write file is retained only as audit evidence.
`--apply-libogc-ntsc` still emits the complete ordered libogc NTSC/DTV encoder
sequence, but readback assertions are limited to stable scalar registers:
`0x00`-`0x03`, `0x05`-`0x06`, `0x08`-`0x0a`, `0x65`, `0x6a`, `0x6e`,
`0x71`-`0x72`, and `0x7a`-`0x7d`. The Macrovision block, gamma block, and
command/commit register `0x04` are excluded. Successful I2C completion is the
positive control for those command-style transfers.

A host emulator now models the observed hardware behavior by returning `0xff`
for Macrovision reads and self-clearing `0x04`. The complete apply action and
stable readback verification pass under that model, and a second action refuses
to overwrite the existing audit snapshot. The stripped static 32-bit
big-endian PowerPC binary builds with `-Wall -Wextra -Werror`; strict checkpatch
reports zero errors and warnings.

Deploy and checksum-verify only the utility. Begin from a freshly booted legacy
baseline with `0x04=1`, `0x01=0x22`, `0x62=0`, and `0x65=1`. Remove the known
stale unbound `gcn_drm` preload before ownership transfer, remove only an
obsolete runtime audit snapshot, and start the committed no-write DRM path.
Render identical bars until a transaction is naturally swapped, then stop the
exact renderer PID and verify state `T`.

Run the apply action once with `/run/wii-ave-libogc.snapshot`. If no snapshot is
created, no AVE write occurred and the attempt is not a result. Otherwise,
classify the frozen frame immediately as correct, swapped, changed another way,
or signal lost. Do not replay the snapshot or perform any additional AVE write.
Preserve the audit snapshot and dmesg under `/root`, resume the exact renderer,
sync, and reboot regardless of apply return code or visual result.

After reboot, require the original four-register baseline and legacy graphics
before recording the result. A transition from swapped to correct is a positive
result for the full libogc reset operation, even though this test intentionally
does not identify which write within the sequence selects the phase. A verified
sequence with unchanged swapped output is a valid negative. Signal loss or a
different frame is a real broad-sequence effect but does not establish a color
fix.

## 2026-08-03: Reject complete libogc AVE reset as color-phase control

The deployed utility matched staged SHA-256
`0d31ad651f0a6b25bb802704fd7751bb7f091e5ea21819f66d918328a4cad44b`.
The fresh baseline was `0x04=1`, `0x01=0x22`, `0x62=0`, and `0x65=1`.
The known stale unbound `gcn_drm` preload was removed while legacy owned VI,
allowing the first service start to bind normally.

Three complete no-write DRM transactions displayed, in order: correct,
correct, then naturally swapped. No AVE write occurred during either correct
transaction. Exact renderer PID 13709 was stopped and verified in state `T` on
the third transaction. The reboot-bounded action created the 264-byte audit
snapshot with SHA-256
`dd0c565326ed89e5b89b5412435162a9c418d01818761a26d2a0676d0f228a8d`,
completed every grouped libogc transfer, and verified every stable scalar.

The unchanged frozen bars remained swapped. This is a valid negative result:
the complete current-libogc AVE encoder initialization sequence does not reset
or compensate the hidden color phase selected by this path. The snapshot and
dmesg were preserved as `/root/20260803-libogc-reset-negative.snapshot` and
`/root/20260803-libogc-reset-negative.dmesg.txt`. The renderer was resumed,
storage synced, and the Wii rebooted without snapshot replay.

Post-reboot validation restored legacy graphics and the exact fresh baseline:
`0x04=1`, `0x01=0x22`, `0x62=0`, and `0x65=1`; the runtime snapshot was absent.
The expected stale unbound `gcn_drm` udev preload recurred and remains a service
packaging issue, not part of the color result.

The investigation now moves from AVE register values to subsystem reset and
mode-latch architecture. Both independent known implementations perform a VI
reset-domain pulse before normal operation: current libogc writes DCR `0x0002`,
delays, then writes zero; legacy Wii Linux sets and clears the DCR reset bit
before mode detection. The modern DRM programmer only writes DCR zero, programs
timing, and writes enable bit 0. It never asserts reset bit 1. Since a simple
enable edge already occurs on every correct and swapped DRM probe, the missing
reset-bit pulse is the next isolated mode-setting candidate.

## 2026-08-03: Stage VI DCR reset-pulse reliability test

- Implementation: `51ff89e12`
- `gcn-drm.ko` SHA-256:
  `20df08fec98d625dc4821821e427b8b03d4bda96ccc5ffe30a4f42e1995061f5`

The DRM fixed-mode programmer now writes DCR reset bit 1, holds it for 2 us,
clears DCR to zero, and only then performs the unchanged timing, XFB, interrupt,
and enable sequence. It logs `pulsed VI DCR reset` after the edge. This is the
smallest reset-domain operation common to both current libogc and legacy Wii
Linux but absent from the modern DRM path. AVE state, color conversion, XFB
contents, scanout addresses, and ownership logic are unchanged.

The configured PowerPC module set builds successfully with `make -j16 modules`.
Deploy and checksum-verify only this module from a freshly rebooted legacy
baseline. Remove the stale unbound udev preload before replacement and preserve
the prior module for rollback. Confirm the AVE baseline read-only; perform no
AVE write during this test.

Run eight complete service-managed DRM transactions. On every transaction,
require a fresh `pulsed VI DCR reset` kernel marker, render the identical seven
bars, classify the full frame, then stop the service and require legacy
gcnfb/generated-GX restoration before the next start. Success requires all
eight transactions to show the semantic red, green, blue, gold, white, magenta,
and teal order. One swapped frame fails immediately and requires restoring the
prior installed module after clean teardown.

This test checks repeatability against the earlier six-run warm correct streak;
one correct frame is not sufficient evidence. Record the complete ordered
outcomes and final AVE read-only state. Whether positive or negative, commit the
result and pause development before beginning subsystem diagramming and broader
architecture reversal.

## 2026-08-03: Reject isolated VI DCR reset pulse as sufficient phase fix

The local and uploaded test module both matched staged SHA-256
`20df08fec98d625dc4821821e427b8b03d4bda96ccc5ffe30a4f42e1995061f5`.
The Wii began from a fresh boot with legacy `gcn-vifb` plus `gcn_gx` active,
and read-only AVE access reported `0x62=0`. The known stale, unbound
`gcn_drm` preload was removed before the installed module was replaced. The
prior installed module was preserved under its exact SHA-256
`1a8456d3f475986060beb0f07d666ea5e3cad38901eefa23a915d50de1476bf1`.

The first service-managed ownership transaction bound the staged module and
started the unchanged RGB565 console client. The live kernel log contained the
required marker before timing setup:

```text
gcn-vi c002000.video: [drm] pulsed VI DCR reset
gcn-vi c002000.video: [drm] programmed NTSC 480i: DCR=0001 ...
```

The canonical seven-color fixture displayed the established swapped sequence:

```text
blue, green, red, light blue, white, magenta, gold
```

Expected output was red, green, blue, gold, white, magenta, and teal. This
failed the staged acceptance gate on transaction 1, so the remaining seven
transactions were intentionally not run. No AVE write occurred.

The isolated DCR reset pulse is therefore not sufficient to establish the
correct downstream color phase. This does not prove that reset bit 1 has no
hardware effect or that it cannot be one part of a larger initialization
transaction. It does reject adding this pulse alone as the production fix.

Service teardown stopped exact client PID 2443, removed DRM ownership, and
restored legacy `gcn-vifb` plus `gcn_gx`. The prior module was restored on
disk with its original checksum, console printk was restored, and final
read-only AVE access still returned `0x62=0`. The complete failed-run log is
preserved on the Wii as
`/root/20260803-vi-dcr-reset-negative.dmesg.txt`, SHA-256
`2430eccec7d26b5ffa5039231c3db383ce143d7293aeb14af67c46b11b50aef5`.

Move to ordered reset and clock-domain architecture rather than another scalar
register guess. Compare the complete VI/AVE acquisition order used by libogc,
legacy `gcn-vifb`, and DRM, classify register operations as stored state versus
edges or commands, and isolate which reset or clock boundary can persist across
driver ownership transitions and a full AVE initialization.

## 2026-08-05: Stage enabled-VI inherited-mode control

- Architecture audit: `54becc6e7` on `docs/wii-video-architecture`
- Cycle-harness fix: `a6879e145`
- `wii-drm-cycle.sh` SHA-256:
  `bf6ca68627513e566a16ad5533946c6d25cb31b1e41f1fcf9ec00cc1220fe7a6`
- Reused `gcn-drm.ko` SHA-256:
  `20df08fec98d625dc4821821e427b8b03d4bda96ccc5ffe30a4f42e1995061f5`
- Target `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`

The pinned libogc audit corrected an important premise: `VIDEO_Init()` pulses
VI reset only when DCR reports VI disabled. When VI is already enabled, libogc
imports the live mode and later commits shadowed mode/address changes from the
retrace handler without a disable/reset cycle. Legacy `gcn-vifb` likewise
leaves VI enabled when it is unbound. The standalone DRM path instead
quiesces, resets, programs with DCR zero, and enables last.

Run a no-build compatibility control with the existing module explicitly
loaded as `program_mode=0`. This skips `gcn_drm_program_ntsc_480i()` entirely:
there must be no fresh `pulsed VI DCR reset` or `programmed NTSC 480i` marker.
The driver must report a `handoff` mode and `/sys/module/gcn_drm/parameters/
program_mode` must read `N`. VI must remain enabled from the legacy owner.

The cycle harness now removes a stale unbound udev-preloaded `gcn_drm` before
legacy unbind and passes `program_mode=0` explicitly. This repairs a control
bug introduced when standalone programming became the module default: the
previous no-argument branch silently selected `program_mode=1` and could not
perform an inherited-mode test.

Perform six complete warm ownership transactions from the same boot. For each
transaction, start the checksum-pinned RGB565 console, classify the canonical
seven colors as correct (`red, green, blue, gold, white, magenta, teal`) or
swapped (`blue, green, red, light blue, white, magenta, gold`), then restore
legacy `gcn-vifb` plus `gcn_gx` before the next transaction. Record any other
output exactly rather than forcing it into either category.

Consistently correct handoff would localize the selector to standalone
disable/program/enable behavior. Consistently swapped handoff would show that
the selected phase predates standalone DRM mode programming or survives its
absence. Mixed handoff would reject that simple deterministic boundary. This
is a localization control, not a production fix; keep cold and warm histories
separate and perform no AVE write.

## 2026-08-06: Inherited enabled-VI handoff remains swapped 6/6

The corrected cycle harness and all reused artifacts matched their staged
checksums. The Wii started from a fresh boot with legacy `gcn-vifb` and
`gcn_gx` active. A stale unbound udev-preloaded `gcn_drm` instance was present;
the harness removed it before releasing legacy ownership so that the explicit
module parameter applied to the real platform probe.

All six DRM probes reported:

```text
gcn-vi c002000.video: [drm] bound fixed 640x480 handoff mode, ...
```

For every transaction, `/sys/module/gcn_drm/parameters/program_mode` read
`N`. The complete log contains exactly six handoff markers and zero `pulsed VI
DCR reset` or `programmed NTSC 480i` markers. Thus none of the six transactions
entered `gcn_drm_program_ntsc_480i()`.

The first client initially mirrored an unseeded tty, so no colors were visible;
this was not counted as a visual result. After writing the same labeled ANSI
fixture to tty1, the checksum-pinned RGB565 console remained alive and exposed
the canonical seven samples. The complete visual sequence was:

```text
transaction 1: swapped
transaction 2: swapped
transaction 3: swapped
transaction 4: swapped
transaction 5: swapped
transaction 6: swapped
```

Each transaction displayed blue, green, red, light blue, white, magenta, and
gold where red, green, blue, gold, white, magenta, and teal were expected. No
AVE write occurred. Between transactions the exact console PID exited, DRM
unloaded, legacy `gcn-vifb` rebound and ran its probe-time VI reset and mode
setup, and `gcn_gx` reloaded before the next inherited-mode handoff.

This is a decisive negative for `program_mode=0` as a color correction and
rejects standalone DRM reset/timing programming as a necessary cause of the
swap. The swapped outcome either survived or was deterministically reselected
across five complete legacy probe/reset/setup round trips. It does not prove
that the hidden phase was selected before the first DRM handoff, because the
handoff path still quiesces VI display interrupts and later programs XFB field
addresses when the DRM client enables its plane.

After transaction 6, teardown restored legacy ownership and generated GX.
Read-only AVE access reported `0x62=0`. Udev subsequently reloaded an unbound
`gcn_drm`, reproducing the known packaging issue without changing VI ownership.
The complete log is preserved on the Wii as
`/root/20260806-drm-inherited-mode-swapped-6of6.dmesg.txt`, SHA-256
`ef35afe48c45607a2a96380cf66f94a04a3e1d2b6958f76149f8b9a2b506471b`.

Next obtain the complementary control from a positively classified correct
state before changing source: establish a correct frame, restore legacy, then
run explicit `program_mode=0` handoffs and determine whether they preserve or
destroy that state. In parallel, enumerate the remaining writes common to
standalone and handoff paths, especially DI quiesce/acknowledgment and the
client-enable XFB address transaction. Do not return to scalar AVE guesses.

## 2026-08-06: Stage correct-phase inherited-handoff control

- Cycle harness: `a6879e145`
- `wii-drm-cycle.sh` SHA-256:
  `bf6ca68627513e566a16ad5533946c6d25cb31b1e41f1fcf9ec00cc1220fe7a6`
- Reused `gcn-drm.ko` SHA-256:
  `20df08fec98d625dc4821821e427b8b03d4bda96ccc5ffe30a4f42e1995061f5`
- Target `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`

The swapped 6/6 inherited-mode result establishes a stable negative state but
cannot determine whether inherited handoff preserves the incoming phase or
selects swapped itself. Run the complementary transition from a positively
classified correct state without changing any artifact or AVE register.

Begin after a real power cycle, not merely a warm ownership transition. Use
the explicit `--program-mode` harness path and the canonical labeled RGB565
fixture to classify standalone DRM. If it is swapped, restore legacy cleanly,
record that cold attempt, and power-cycle before trying again. Do not chain warm
standalone retries while searching for the positive state.

When a standalone transaction is visibly correct:

1. Stop the exact DRM console PID and restore legacy `gcn-vifb` plus `gcn_gx`.
2. Re-seed and classify the same tty fixture on the visible legacy console
   before another ownership change.
3. Load the same DRM module explicitly with `program_mode=0`, require parameter
   `N`, a handoff marker, and no new reset/program marker.
4. Start the same RGB565 client and classify the same fixture again.
5. Restore legacy and preserve the complete ordered log.

If standalone, restored legacy, and inherited DRM all remain correct, the
handoff path preserves a known-correct phase and the earlier 6/6 swapped run
demonstrates phase inheritance. If legacy is already swapped, localize the
transition to legacy restore/probe/setup. If legacy is correct but inherited
DRM is swapped, localize the selector to operations common to DRM handoff,
chiefly DI quiesce/acknowledgment or client-enable XFB field-address writes.
Any other transition must be recorded literally.

## 2026-08-06: Correct-phase acquisition attempt 1 is swapped

After a clean software power-off and physical power-on, the Wii reached legacy
ownership at approximately 59 seconds uptime. The first `--reuse-remote`
preflight rejected a missing or mismatched `/tmp` generic DRM module before any
VI ownership change. A normal checksum-verified upload then started explicit
standalone mode with `program_mode=Y`.

Kernel markers confirmed the expected reset and complete fixed-mode path:

```text
gcn-vi c002000.video: [drm] pulsed VI DCR reset
gcn-vi c002000.video: [drm] programmed NTSC 480i: DCR=0001 ...
gcn-vi c002000.video: [drm] bound fixed 640x480 programmed NTSC 480i mode, ...
```

The canonical labeled RGB565 fixture displayed the swapped sequence. This cold
attempt therefore cannot seed the complementary inherited-mode control. No
warm standalone retry was run. The exact client exited, legacy `gcn-vifb` and
`gcn_gx` were restored, read-only AVE access returned `0x62=0`, storage was
synced, and the Wii was powered off for another independent cold attempt.

The log is preserved as
`/root/20260806-correct-phase-acquire-1-swapped.dmesg.txt`, SHA-256
`a6fd6ffd30523ca744a3f5e86905df494e6cc8bb0f361bb644b0dee88bda3260`.

## 2026-08-06: Correct-phase acquisition attempt 2 is swapped

After another real power cycle, the Wii reached a fresh legacy baseline at
approximately 52 seconds uptime. Legacy `gcn-vifb` owned the video platform,
and read-only AVE access reported `0x62=0`. The checksum-pinned modules were
uploaded again because the power cycle had cleared the remote `/tmp` staging
area.

The explicit `--program-mode` path completed normally. Kernel markers again
confirmed a VI DCR reset, complete NTSC 480i programming, and a programmed-mode
DRM bind. The console client remained alive as PID 2595 and
`/sys/module/gcn_drm/parameters/program_mode` read `Y` when the canonical
labeled RGB565 fixture was classified.

The displayed sequence was again swapped: blue, green, red, light blue,
white, magenta, and gold instead of red, green, blue, gold, white, magenta,
and teal. This is the second independent cold swapped acquisition. It does not
establish that a cold boot can never produce the previously observed correct
phase, so the staged procedure remains unchanged: do not run a warm retry;
restore, power-cycle, and continue independent acquisitions until a correct
standalone state can seed the transition control.

The exact client PID stopped cleanly, legacy `gcn-vifb` and `gcn_gx` were
restored, and final ownership was verified through the bound legacy platform
device. Read-only AVE access still returned `0x62=0`. Storage was synced and
the Wii was powered off.

The complete log is preserved as
`/root/20260806-correct-phase-acquire-2-swapped.dmesg.txt`, SHA-256
`7840d433c3343b0a897f68e4989c192b4ab0d023037615041012c3ea05e5bcf1`.

## 2026-08-06: Correct-phase acquisition attempt 3 is swapped

A third real power-on reached a fresh legacy baseline with `gcn-vifb` owning
the video platform. At approximately 267 seconds uptime, read-only AVE access
again reported `0x62=0`. The persistent console binary retained its expected
checksum; the harness re-uploaded and checksum-verified the pinned module set
after the power cycle cleared `/tmp`.

Explicit standalone mode completed with the required VI DCR reset, NTSC 480i
programming, and programmed-mode bind markers. The canonical RGB565 console
client remained alive as PID 2179, and the live module parameter read
`program_mode=Y` at classification time.

The full-frame fixture again displayed blue, green, red, light blue, white,
magenta, and gold instead of the expected red, green, blue, gold, white,
magenta, and teal. This is cold acquisition attempt 3 of 3 with the established
swapped mapping. It increases confidence that swapped is the dominant cold
outcome for this unchanged transaction, but it does not reveal which hidden
state selects the intermittent correct mapping and does not replace the
planned positive-state transition control.

No warm retry was run. Exact client PID 2179 exited, the harness restored
legacy `gcn-vifb` and `gcn_gx`, and the bound legacy platform device confirmed
final ownership. Read-only AVE access remained `0x62=0`. Storage was synced and
the Wii was powered off.

The complete log is preserved as
`/root/20260806-correct-phase-acquire-3-swapped.dmesg.txt`, SHA-256
`be63e547594d0b04f9b3b6534ed399e9e3d71042fbaf6ba23dc9b56c69ffb419`.

## 2026-08-06: Attempt 4 is swapped; retire cold correct-phase search

A fourth real power cycle reached legacy ownership with read-only AVE register
`0x62=0`. The persistent console binary matched its pinned checksum, and the
harness uploaded and verified the unchanged module set before explicit
standalone acquisition. The kernel again logged the required VI DCR reset,
NTSC 480i programming, and programmed-mode bind. Client PID 2722 remained
alive and the module parameter read `program_mode=Y` during classification.

The canonical full-frame fixture displayed the same swapped sequence for the
fourth independent cold acquisition: blue, green, red, light blue, white,
magenta, and gold rather than red, green, blue, gold, white, magenta, and teal.
All four acquisitions in this controlled cold series are therefore swapped.
The earlier production-rollback cold boot also began swapped and remained so
across three warm ownership transactions, while the documented correct streaks
occurred during warm sessions. This evidence supports cold-swapped as the
reproducible baseline; it does not justify calling every future cold boot
mathematically guaranteed to be swapped.

Stop spending hardware cycles waiting for a naturally correct cold acquisition.
The planned correct-phase inherited-handoff control cannot currently obtain its
required natural positive state and is retired rather than reported as a
negative result. Redesign the next experiment around the reproducible
cold-swapped baseline and the already validated AVE `0x62` bit-1 compensator.
Keep the distinction explicit: writing `0x02` can correct a frozen swapped
frame, but it does not reset the unknown upstream phase and therefore cannot by
itself answer whether DRM handoff preserves that phase.

Exact client PID 2722 exited, legacy `gcn-vifb` and `gcn_gx` were restored,
final legacy ownership was verified, read-only AVE access remained `0x62=0`,
storage was synced, and the Wii was powered off. The complete log is preserved
as `/root/20260806-correct-phase-acquire-4-swapped.dmesg.txt`, SHA-256
`49f1fa52325b10baffd492e7cc60c2594ab2c63bbb56cf5a5622e2bad538d84d`.

## 2026-08-06: Stage live AVE compensation viability control

- Static `wii-ave-reg` SHA-256:
  `f1fc92bcd90eb4bcd1cbf3285e295ed5087fe446e45c47c5852681d5539b95e0`
- Reused `gcn-drm.ko` SHA-256:
  `20df08fec98d625dc4821821e427b8b03d4bda96ccc5ffe30a4f42e1995061f5`
- Target `wii-drm-console` SHA-256:
  `66fb1a55e46366bc2313ade57fcde11f401cd44b3e1487ab1de2fa13b3c6ea81`

The four-attempt cold acquisition series makes a swapped first standalone
frame the reproducible test baseline. Stop searching for a naturally correct
cold state. Instead determine whether the already validated AVE `0x62` bit-1
exchange can serve as a stable operational compensation while KMS continues
to render and flip pages.

Begin from another real power cycle and run one explicit programmed-mode DRM
transaction with the unchanged module and canonical RGB565 fixture. Require
the established swapped mapping and read-only `0x62=0`. Keep the renderer
running; do not freeze it. Deploy the checksum-pinned static utility and write
only `0x02` with `wii-ave-reg /dev/i2c-0 --set-swap`. Require an immediate
transition to the correct red, green, blue, gold, white, magenta, and teal
mapping.

After that transition, write visibly changing fixture text through tty1 for at
least ten renderer updates so conversion and both XFB pages continue changing.
The full frame must remain correctly mapped throughout and the client must stay
alive. This is the new positive control beyond the earlier frozen-frame tests:
the compensation must apply to subsequent XFB contents, not only the frame
that was visible during the I2C transfer.

Finally write only `0x00` with `--clear-swap` while the renderer remains live.
Require the same active fixture to return immediately to the established
swapped mapping. Read back zero, stop the exact client, and restore legacy
ownership. Never unload DRM, restore legacy, or power off while this experiment
intentionally leaves `0x62=2`.

A complete swapped-to-correct-live-updates-to-swapped reversal validates
`0x62=2` as a practical cold-session compensation and justifies staging a
late, explicitly controlled DRM/encoder integration. It still does not identify
or reset the upstream hidden phase, and it must not be described as such.

## 2026-08-06: Live AVE compensation remains correct across updates

The Wii began from another real power cycle with legacy `gcn-vifb` ownership,
read-only AVE `0x62=0`, and the checksum-pinned console client. The installed
AVE utility was replaced with the staged static build and verified at SHA-256
`f1fc92bcd90eb4bcd1cbf3285e295ed5087fe446e45c47c5852681d5539b95e0`.
The unchanged DRM module then entered explicit programmed mode with the
expected VI reset, NTSC 480i setup, and bind markers.

Client PID 3204 remained alive with `program_mode=Y`. At `0x62=0`, the
canonical full-frame RGB565 fixture displayed the reproducible cold-swapped
mapping. Without stopping the renderer or touching VI or XFB memory, the
utility wrote only `0x02` to AVE register `0x62` and read the same value back.
All seven colors immediately became correct.

The test then wrote ten visibly numbered updates through tty1 at one-second
intervals. The client remained alive, conversion and presentation continued,
and the user confirmed that all seven colors stayed correct throughout and
after the updates. Read-only access still returned `0x62=2`. This demonstrates
that the compensation applies to later framebuffer contents and page
presentations, not only to the frame visible during the I2C transfer.

With the renderer still live, writing only `0x00` changed readback from two to
zero and immediately returned the active fixture to the same swapped mapping.
This completes the required swapped-to-correct-live-updates-to-swapped
reversible positive control.

Exact client PID 3204 then exited, the harness restored legacy `gcn-vifb` and
`gcn_gx`, final ownership was verified through the bound legacy platform
device, and AVE readback remained `0x62=0`. The complete log is preserved as
`/root/20260806-live-ave-compensation-pass.dmesg.txt`, SHA-256
`ed56c78c3bc72147afccca085644f70921e1d75ade32e2fd974ce1dcd9824fe2`.

This passes AVE bit 1 as a stable operational compensation for the currently
reproducible cold-swapped session. The next implementation test may integrate
an explicit late `0x62=2` write after DRM mode acquisition and before normal
client presentation, with a mandatory `0x62=0` restoration on teardown. Keep
the mechanism opt-in until repeated cold starts and warm module reloads define
its safe lifecycle; it remains compensation for, not a reset of, the hidden
upstream phase.

## 2026-08-06: Stage transactional AVE compensation harness

The cycle harness adds an opt-in `--ave-swap` mode, currently restricted to
explicit `--program-mode` transactions. After DRM binds and its card appears,
the harness verifies the AVE utility and I2C device, creates a remote lifecycle
marker, and writes `0x62=2`. A failed write attempts an immediate clear and
removes the marker before returning failure.

Every normal restore and transition-error restore checks the marker before
unloading DRM. When present, restoration must successfully write `0x62=0`
before removing the marker and proceeding to legacy rebind. This also protects
a later standalone `--restore` invocation that does not repeat `--ave-swap`.
The marker is created before the set operation so a partially failed command
cannot silently bypass cleanup.

`shellcheck`, `git diff --check`, and argument validation pass. This is staged
but not hardware-validated. On resume, use the already installed checksum-
verified AVE utility and run one `--program-mode --ave-swap` cycle. Require
correct colors on the first client frame, marker presence and `0x62=2` while
active, then invoke ordinary `--restore` without `--ave-swap` and require
marker removal, `0x62=0`, and legacy ownership before accepting the lifecycle.

## 2026-08-06: Accept transactional AVE compensation lifecycle

The Wii started from a fresh legacy baseline at AVE `0x62=0` with no lifecycle
marker. The installed static AVE utility matched SHA-256
`f1fc92bcd90eb4bcd1cbf3285e295ed5087fe446e45c47c5852681d5539b95e0`.
Running the committed harness as `--no-build --program-mode --ave-swap`
uploaded the checksum-pinned modules, bound DRM through the expected VI reset
and NTSC setup path, and then wrote AVE register `0x62` from zero to two.

Before any client presentation, `/run/wii-drm-ave-swap-active` existed and
read-only AVE access returned two. The canonical RGB565 client remained alive
as PID 1844 with `program_mode=Y`. Its first visible fixture frame displayed
all seven semantic colors correctly, validating both the late ordering and the
absence of an uncorrected first client frame.

Exact PID 1844 exited before teardown. A separate plain
`--restore --reuse-remote` invocation intentionally omitted `--ave-swap`.
The persisted marker nevertheless selected the cleanup path, which read two,
wrote zero, verified zero, and only then proceeded with DRM unload and legacy
rebind. Final checks found legacy `gcn-vifb` ownership, no marker, and AVE
`0x62=0`. Kernel log markers independently record both the set and clear
operations.

This accepts the harness lifecycle as a safe opt-in test and development path:
compensation is active before the first client frame and a later independent
restore cannot silently strand it. It does not make `0x62=2` a universal
kernel default; warm states that naturally require zero still exist in the
historical record. Production integration must retain an explicit policy or
phase decision until that behavior is resolved.

The complete log is preserved as
`/root/20260806-transactional-ave-lifecycle-pass.dmesg.txt`, SHA-256
`dab0aad743d557513bf5f52237c5b99cf0153bf2de8f7c8b454e604a190a63bf`.

## 2026-08-06: Stage whitelisted display-MMIO snapshot comparison

- Observer module SHA-256:
  `d3eaca0740403efa8e057add3e8f6d078a680ff00be68707af92282037f8bf32`

Extend the existing read-only XFB observer into a constrained software
equivalent of port-state sniffing. It retains the established selected-XFB
samples and complete 0x100-byte VI readout, then adds only offsets already read
by active kernel drivers: CP status/control and FIFO registers, PE interrupt
status/token, PI FIFO base/end/write pointer, and both Hollywood GPIO register
banks including ownership. It does not scan arbitrary Hollywood addresses.

Each module load accepts a snapshot label and records two snapshots separated
by one millisecond by default. At most eight samples and a one-second delay are
accepted. The repeated reads distinguish stable state from the VI beam counter,
interrupt flags, or other naturally changing observations. The module performs
no MMIO write; the pre-existing optional selected-XFB cache flush remains off.

The configured PowerPC module build, modpost, strict checkpatch, and
`git diff --check` pass. Run this exact four-phase comparison on one ownership
session:

1. Capture two labeled snapshots under restored legacy ownership.
2. Start programmed DRM without AVE compensation, render the canonical fixture,
   require the reproducible swapped mapping at `0x62=0`, and capture twice.
3. Keep the client live, write AVE `0x62=2`, require correct colors, and capture
   twice without another DRM ownership transition.
4. Clear AVE back to zero, require swapped colors again, and capture twice.

Positive controls are mandatory. Every phase must emit exactly two complete
snapshots. Selected-XFB fixture words must remain coherent across the three DRM
phases, and AVE userspace readback must independently show zero, two, then zero.
Known same-phase changes identify dynamic fields and must be masked before any
cross-phase conclusion. Legacy-to-DRM differences validate ownership
visibility but are not evidence of the color selector by themselves.

The decisive comparison is DRM swapped versus AVE-corrected versus AVE-cleared.
A stable MMIO bit that follows wrong/correct/wrong is a candidate mirrored or
collateral state requiring an isolated control. No stable difference is also a
valid and expected localization result: it would show that the visible AVE
exchange has no reflection in this safe MMIO whitelist and strengthen the case
for transaction tracing or external AVE I2C capture rather than a broader
unsafe register sweep.

## 2026-08-06: Whitelisted MMIO has no color-correlated state

The deployed observer matched staged SHA-256
`d3eaca0740403efa8e057add3e8f6d078a680ff00be68707af92282037f8bf32`.
All four phases emitted exactly 46 labeled lines: one selected-XFB header,
seven XFB words, and two complete 19-line MMIO samples. Every load and unload
completed with the client or legacy framebuffer still operational, and the
kernel logged no machine check, oops, or other fault.

The legacy snapshot established the repetition filter. Its two samples changed
only the live VI beam/field count, CP read/write distance, and PE token. Under
the active DRM client, same-phase repeats additionally changed the selected
top/bottom XFB page addresses as the renderer presented pages. Those fields
were classified as dynamic before any cross-phase comparison.

The hardware sequence then completed under one DRM ownership interval:

```text
AVE 0x62=0: visually swapped
AVE 0x62=2: visually correct
AVE 0x62=0: visually swapped again
```

The same live client PID 15556 remained active throughout. Each phase was
explicitly redrawn and visually classified before its snapshot. AVE readback
independently passed the required zero, two, zero control. The seven selected
XFB sample sites contained the identical background word `0x00800080` in all
three DRM phases.

After masking only fields proven dynamic by the within-phase repeats, all
whitelisted VI, CP, PE, PI, and Hollywood GPIO values were byte-identical for
DRM swapped versus corrected, corrected versus cleared, and swapped versus
cleared. In particular, GPIO output, direction, input, interrupt, enable, and
ownership words showed no retained reflection of the AVE exchange setting once
its I2C transaction returned the bus to idle.

The apparatus passed a separate ownership positive control. Legacy versus DRM
showed VI offset `0x30` word 0 changing from `0x10010001` to `0x00010001` and PE
status changing from `0x0003` to `0x0000`, consistent with DRM interrupt
quiescence and removal of generated GX ownership. The null color-phase diff is
therefore not explained by an observer permanently returning constants.

This rules out a readable color-correlated bit in the safe MMIO whitelist. Do
not broaden the sweep to arbitrary Hollywood addresses: undocumented reads can
acknowledge state or machine-check, and this result gives no candidate range.
Move to ordered transaction tracing. Instrument Linux writes to VI and AVE,
and use the documented TP219-TP226 DEBUG GPIO outputs as external logic-analyzer
event markers when motherboard access is practical. Directly probing AVE SCL
and SDA can independently capture the real I2C wire transaction.

Teardown stopped exact PID 15556, restored legacy `gcn-vifb` and `gcn_gx`, and
left AVE `0x62=0`. The preserved files are:

```text
legacy snapshot:       6e42f89cc391e72d8a97a9976ece052e1595cfc8e6c88fe590efefc7ecdaa830
DRM swapped snapshot:  35ad5fa745649f674a8010a4a39864e1e788e68e5db3487b12c4bc50920b2eb1
DRM corrected snapshot:4457d4a913f77262dbcb45f926b0e4fec299fb00b81870467b08e532d7226c5a
DRM cleared snapshot:  dda46c9745b6b4cfb575f460bf8e0ecee0c8738713d933af4a198e2c8602e5c7
complete dmesg:        26942f28863d299a514fb8fc0e8ee04914070d06ac068384c57996efde1f177a
```

## 2026-08-06: Stage ordered VI/AVE transaction-trace positive control

- Tracing implementation: `97e12de9f`
- Wii-shell redirection fix: `b5ee207d8`
- `gcn-drm.ko` SHA-256:
  `bde57aa64a85a733e14d8ff990cc9bef0ae89115c43bac1b61f0aad750998aef`
- `wii-drm-cycle.sh` SHA-256:
  `3adad89c4e175b6fde79992a7af15a9f9c95afeefb5f9b9df8a289387e753f82`

The preceding read-only snapshot ruled out a retained color-correlated bit in
the safe VI, CP, PE, PI, and GPIO whitelist. Move from state sampling to
ordered software transaction tracing before considering external probes. No
oscilloscope or logic analyzer is required for this stage.

Every DRM VI write now passes through a width-aware wrapper with an opt-in
`gcn_vi_write` tracepoint. An enabled event records a monotonically increasing
write sequence, 16- or 32-bit width, register offset, value, and call site
immediately before the original MMIO write. When disabled, the wrapper performs
no atomic counter operation and issues the same direct big-endian MMIO write as
before.

The cycle harness adds `--trace-output FILE`. It preloads the module while the
legacy driver still owns the platform device, then creates an isolated,
256-KiB tracefs instance before legacy unbind. This allows the tracepoint to
observe the first DRM probe write rather than beginning after mode setup. The
same monotonic trace timeline includes the kernel's existing I2C transfer
events filtered to adapter 0 and AVE address `0x70`. Explicit markers bracket
legacy unbind, DRM bind, AVE compensation, and the active no-client state.
Tracing stops automatically and the downloaded artifact receives a printed
SHA-256 checksum. Error teardown disables tracing before restoring legacy
ownership.

The first invocation stopped before ownership changed because the Wii shell
does not accept a newline immediately after a redirection operator. Commit
`b5ee207d8` keeps each remote tracefs destination on the same command line.
An isolated live preflight then enabled all five filtered event files, captured
a trace marker, disabled the instance, and verified that legacy `gcn-vifb`
remained bound. No VI write, AVE write, or visual result occurred in the
rejected invocation.

Validate the measurement mechanism before drawing any color conclusion. Run a
programmed-mode cycle with `--ave-swap` and a local trace-output path. Require:

1. A complete, strictly increasing VI write sequence beginning with DI
   quiescence and containing known constants such as `VTR=0x0f06`,
   `HTR0=0x476901ad`, and final `DCR=0x0001`.
2. I2C write events for the AVE register-address read transaction and the exact
   compensation payload `[62 02]`, followed by a successful transfer result.
3. Correct ordering of the bind, AVE-start, AVE-complete, and capture-stop
   markers around those events.
4. No trace overrun, missing sequence number, machine check, oops, conversion
   failure, or loss of SSH/display ownership.

Only after those positive controls pass may subsequent traces compare natural
swapped and correct acquisitions. A trace with no AVE payload or no known VI
constant is a failed instrument, not evidence that the corresponding hardware
operation did not happen. After validation, launch the unchanged canonical
RGB565 fixture to confirm that trace collection did not perturb the expected
`0x62=2` corrected output, then use the ordinary transactional restore and
verify AVE `0x62=0` with legacy `gcn-vifb` rebound.

## 2026-08-06: Accept ordered VI/AVE software transaction tracing

The corrected harness and all six remote modules matched their staged
checksums. Tracefs was mounted on demand, the isolated `gcn-display` instance
started while legacy still owned the video platform, and the preloaded
`gcn_drm` tracepoint observed the first write made by the subsequent manual
platform bind.

The complete artifact contains 62 written and 62 retained events with no
overrun. All 43 VI events have a contiguous sequence from 1 through 43. The
trace begins with four DI quiescence writes, then records the DCR reset edge
`0x0002` followed by zero. Known programmed-mode constants appear at the
expected offsets, including `VTR=0x0f06`, `HTR0=0x476901ad`,
`HTR1=0x02e850c0`, and final `DCR=0x0001`. XFB addresses and the second
probe-time DI quiescence are also present.

The AVE positive control is complete on the same monotonic trace clock:

```text
read request:  address 0x70, payload [62]
read reply:    [00], result 2/2 messages
write request: address 0x70, payload [62-02]
write result:  1/1 message
verify request: address 0x70, payload [62]
verify reply:  [02], result 2/2 messages
```

Markers strictly bracket legacy ownership, transition start, legacy unbind,
DRM bind completion, AVE set start, AVE set completion, active no-client state,
and trace stop. This validates both sides of the measurement mechanism: the VI
tracepoint reports known kernel MMIO writes, and the existing I2C events report
known userspace AVE transactions and returned bytes.

After capture stopped, the unchanged checksum-pinned RGB565 console client
started as PID 25021. AVE readback remained `0x62=2`, and the user twice
confirmed the expected red, green, blue, gold, white, magenta, and teal mapping.
Thus the tracing build and completed capture did not perturb the compensated
visual path.

Exact-PID termination succeeded. Transactional restore read two, wrote zero,
and verified zero before rebinding legacy `gcn-vifb` and reloading `gcn_gx`.
Final checks found DRM unbound, no client or lifecycle marker, and tracing plus
all instance events disabled. Udev reloaded the known unbound packaged
`gcn_drm` instance without taking the platform device. A corrected
case-sensitive fault audit found no BUG, oops, panic, machine check, watchdog,
conversion failure, or trace overrun.

Preserved artifacts:

```text
ordered trace: 2f0809b1aa96194a064af30f505359c3455b5c196141214d098908e0de9b35d7
complete dmesg: 65ec6f9ccbea1445a4f6a76219a75f66d22d39104156e106a0c378cd9b09cac4
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

The ordered software tracer is accepted. Next collect repeated explicit
programmed-mode traces with no AVE write, launch the same canonical client only
after each capture, and visually classify each acquisition as naturally
swapped or naturally correct. Compare complete write order and inter-write
timing only after at least one trace exists in each visual class. Identical
traces across opposite visual classes would localize the hidden selector below
Linux's observable VI/AVE transaction boundary and justify external I2C wire
capture or DEBUG-pad timing markers; until then, no external instrument is
required.

## 2026-08-06: Natural traced acquisition 1 is swapped

The first acquisition after tracer acceptance reused the exact module and
harness artifacts with explicit programmed mode and no `--ave-swap` option.
AVE readback was `0x62=0`, and the transaction trace contains no I2C event.
It retained all 50 of 50 events without overrun, including a contiguous VI
sequence from 1 through 43.

After capture stopped, the unchanged RGB565 console started as PID 26181. The
user classified the canonical fixture as swapped. This establishes the first
natural visual-class baseline for the tracer; the earlier corrected trace does
not count as a naturally correct comparison because it deliberately includes
the AVE `[62 02]` compensation transaction.

Removing timestamps and task metadata produced no semantic VI diff against
the compensated positive-control run: all 43 sequence numbers, widths,
offsets, values, and reported call sites match. That expected equality proves
the baseline is internally coherent but cannot yet localize the hidden phase.
Do not compare timing classes until a naturally correct AVE-zero acquisition
exists.

Exact client termination succeeded, legacy `gcn-vifb` and `gcn_gx` returned,
DRM was unbound, AVE remained zero, and the corrected fault audit was empty.
Preserved artifacts:

```text
swapped trace: 85fa7ec9e51400af6916818b273c7dddbe510b68aaf18327ca405cb6987e6966
complete dmesg: 85a58bd707c99b4cbec24be74720c81abb0e86f5b44cfb8dcd695ab265bee62d
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

Continue independent no-AVE-write traced acquisitions with the same artifacts
until a naturally correct sample is captured. Preserve every classification
literally; do not use AVE compensation to convert a swapped acquisition into
the missing positive class.

## 2026-08-06: Natural traced acquisition 2 is also swapped

The second independent warm acquisition reused the same checksum-pinned
module, harness, programmed-mode path, AVE zero state, and RGB565 fixture. The
trace retained all 50 of 50 events without overrun and again contained no I2C
traffic. The user classified the live client PID 27202 output as swapped.

After removing timestamps and task metadata, acquisitions 1 and 2 have
byte-identical 43-event VI sequences. Their timing differences establish an
initial same-class jitter baseline rather than a color predictor. Relative to
VI sequence 1, selected event times in microseconds were:

```text
sequence             1     5     6     7    32     33     39     40
acquisition 1         0    12    17  6532  6592  11433  11452  33409
acquisition 2         0    12    17  6124  6185   9411   9430  43560
```

The reset assertion/deassertion timing is effectively identical, while printk,
XFB clearing/cache maintenance, DRM setup, and scheduling introduce
millisecond-scale variation later in probe. Do not treat timing differences of
that ordinary magnitude as color-correlated if a naturally correct sample is
eventually captured.

Exact client termination and transactional restore passed. Final state was
legacy `gcn-vifb` bound, DRM unbound, AVE `0x62=0`, and an empty corrected
fault audit. Preserved artifacts:

```text
swapped trace: 4f8669c0a6941fef1e4887e5342fee08f0360f36d98c13d6a208d629c2f820a6
complete dmesg: f4a172cc4500cd0e0bf087da8850e9f7140e8c4267da7f4dd429d337f194e259
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

Pause with the Wii in the verified legacy/AVE-zero state. On resume, run
natural acquisition 3 with the same no-write procedure. The comparison remains
blocked on obtaining at least one naturally correct AVE-zero trace; two
swapped traces alone cannot identify a cross-class transaction difference.

## 2026-08-07: Cold natural traced acquisition 3 is swapped

Acquisition 3 began after an independent power cycle. The Wii reached legacy
ownership at approximately 142 seconds uptime with AVE `0x62=0`; `/tmp` was
empty, so every pinned module was uploaded and remotely checksum-verified
before ownership changed. Explicit programmed mode was used with no AVE write.

The trace retained all 50 of 50 events without overrun and again contained a
contiguous 1-through-43 VI sequence with no I2C event. The unchanged RGB565
console started as PID 1865, and the user classified the canonical fixture as
swapped. After removing timestamps and task metadata, the full VI sequence is
identical to acquisitions 1 and 2.

Selected relative event times were:

```text
sequence             1     5     6     7    32    33    39    40
acquisition 3         0    12    17  3549  3608  7296  7315  29360
```

The 5-microsecond DCR assertion-to-deassertion interval matches both warm
swapped traces. Later timing variation remains inside the already established
same-class scheduling and setup jitter and is not evidence of a selector.

Exact client termination, transactional restore, AVE-zero verification,
legacy rebind, and the corrected fault audit all passed. Preserved artifacts:

```text
swapped trace: b6ffe6637a3af5ee1422e5901c6c9b4d0f080a7c007b98ec29316b5195d542c7
complete dmesg: 3a61f2d1c508d74addde5587a7d9df222e411f51314ad226bd89fa09e0e470d7
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

The natural traced set is now swapped 3/3, including one independent cold
boot. Continue only enough identical-artifact acquisitions to determine
whether a naturally correct AVE-zero state remains reproducible under the
transactional harness. Do not infer that it is impossible from three samples,
but reassess historical correct-at-zero reports if the controlled series
remains uniformly swapped.

## 2026-08-07: Natural traced acquisition 4 is swapped

Acquisition 4 reused the exact checksum-pinned module, harness,
explicit-programmed-mode path, AVE-zero state, and canonical RGB565 fixture.
The trace retained all 50 of 50 events without overrun, contains a contiguous
VI sequence from 1 through 43, and contains no I2C event. The console client
started as PID 2813, and the user classified the fixture as swapped.

After removing timestamps and task metadata, the complete 43-event VI sequence
is byte-identical to acquisition 3 and semantically identical to all preceding
natural acquisitions. Selected relative event times were:

```text
sequence             1     5     6     7    32     33     39     40
acquisition 4         0    13    17  8403  8463  26659  26679  50299
```

The DCR reset assertion-to-deassertion interval remains effectively identical
to the preceding acquisitions. The larger later gaps remain ordinary
same-visual-class scheduling and setup variation, so they cannot identify the
hidden color selector.

Exact client termination and transactional restore passed. Final state was
legacy `gcn-vifb` bound, DRM unbound, AVE `0x62=0`, and an empty corrected
fault audit. Preserved artifacts:

```text
swapped trace: dc6164b68686c1843b24f6df8d8fd1535cdafa838e1d9f7e7847be113d5afd81
complete dmesg: 2a6cbbb493fc0a5be6bfdadfb3b9a56982607ada937e08dc8c681323dff18f95
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

The controlled natural set is now swapped 4/4, with one independent cold boot.
This still does not prove that a naturally correct AVE-zero acquisition is
impossible. It does strengthen the need to reassess historical correct-at-zero
reports and avoid treating uncontrolled observations as a comparison class.
Collect a small bounded number of additional identical acquisitions before
deciding whether to stop waiting for the missing natural class and move the
selector investigation below the observable Linux VI/AVE transaction boundary.

## 2026-08-07: Natural traced acquisition 5 is swapped

Acquisition 5 used the unchanged pinned modules, harness, explicit programmed
mode, AVE-zero state, and RGB565 fixture. The trace retained all 50 of 50
events without overrun, contains all 43 contiguous VI writes, and contains no
I2C traffic. The user classified client PID 4273 as swapped.

The complete semantic VI sequence is identical to acquisition 4. Selected
relative event times were:

```text
sequence             1     5     6     7    32    33    39    40
acquisition 5         0    12    16  3523  3583  6742  6759  28398
```

The reset assertion-to-deassertion interval remains four microseconds. Later
timing is within the range already observed among swapped acquisitions and
does not provide a color-correlated difference.

Exact-PID teardown and transactional restore passed. Legacy `gcn-vifb` was
bound, DRM was unbound, AVE remained `0x62=0`, and the corrected fault audit
was empty. Preserved artifacts:

```text
swapped trace: fa243c7731bd071bdc058a53d8d42cdcc5c591f01d0a0680d4800dc81e66b51c
complete dmesg: 4d273cd49fe81a0883a31f66e20cafbfcf6bc4ac96e4cea9ccb489bbcf0b0401
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

The controlled natural series is swapped 5/5. Run one final identical
acquisition, then stop this sampling phase if it is also swapped. Six complete
same-class captures are enough to show that waiting indefinitely for a
naturally correct AVE-zero sample is not an efficient diagnostic path, while
still not asserting that such a state is impossible.

## 2026-08-07: Natural traced acquisition 6 is swapped; end sampling phase

The sixth and final bounded acquisition reused every pinned artifact and the
same explicit programmed-mode, no-AVE-write procedure. All 50 of 50 trace
events were retained without overrun. The VI sequence is contiguous from 1
through 43, contains no I2C event, and is semantically identical to acquisition
5. The user classified client PID 6355 as swapped.

Selected relative event times were:

```text
sequence             1     5     6     7    32     33     39     40
acquisition 6         0    13    17  3553  3611  10355  10373  28320
```

The four-microsecond DCR reset interval again matches the controlled series.
Later variation remains ordinary same-class setup and scheduling jitter.

Exact-PID teardown, trace preservation, transactional restore, and the final
state audit all passed. Legacy `gcn-vifb` is bound, DRM is unbound, AVE remains
`0x62=0`, and the corrected fault audit is empty. Preserved artifacts:

```text
swapped trace: ec1272f92c8cd99f7fbf9b7ec36d66c15a772bc2d0bdebf378f99d62c9ca64e1
complete dmesg: 32d426f9ea36b61b244a294b617b2b7af1990c92b91a3750e6cb4ec8fa5544a1
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

End the natural-acquisition sampling phase at swapped 6/6, including one
independent cold boot. This is not proof that a naturally correct AVE-zero
state cannot exist, but it is sufficient evidence that waiting for one is not
a reproducible engineering strategy. Historical correct-at-zero observations
were not acquired under this complete controlled procedure and must not be
used as the missing comparison class.

The accepted positive control already demonstrates a deterministic practical
path: the exact AVE write `[62 02]` changes the unchanged RGB565 fixture from
swapped to correct, readback verifies two, and clearing the register returns
the same live client to swapped. The next implementation phase should make
that exchange setting an explicit, verified part of DRM display enable or
mode setup, with restore to zero on disable/unbind. Use the ordered tracer to
validate write placement and lifecycle behavior, then repeat cold and warm
color fixtures to establish reliability. Keep external I2C wire capture and
DEBUG-pad markers as deeper reversal tools only if the explicit driver-owned
AVE sequence proves nondeterministic.

## 2026-08-07: Stage driver-owned AVE chroma-exchange lifecycle

- VI encoder-phandle binding: `9db029f3a`
- DRM/AVE implementation: `df08bbbf1`
- `gcn-drm.ko` SHA-256:
  `90546eaa24d77678f71720edc7d0a0121ceaf99c69b7ed429539b6d07ab6d7a6`
- `zImage` and `dtbImage.wii` SHA-256:
  `b39cdf270d8c0a960ce9b47f53845137106dadb373e4d2a6ccc61cfec8392505`

The bounded natural-acquisition phase ended swapped 6/6. Stop waiting for an
uncontrolled correct-at-zero state and turn the validated `AVE[0x62]=0x02`
compensation into explicit DRM ownership behavior.

Wii DTS again links the Hollywood VI node to the AVE I2C client. The DRM probe
resolves and retains that client before touching VI, defers until the adapter
is ready, and leaves phandle-free Flipper handling unchanged. After programming
and validating the VI mode, but before registering the DRM device, it writes
register `0x62=0x02` and requires a matching combined-transaction readback.
Transfer errors, short transfers, and readback mismatches fail probe rather
than exposing a silently misconfigured display.

The setting remains active for the complete DRM ownership interval. A
userspace pipe disable does not release VI ownership and therefore does not
clear the AVE setting. Driver remove and shutdown restore and verify
`0x62=0x00`. A managed cleanup action retains the AVE client reference and
provides probe-failure restoration plus one remove-path retry before releasing
the client.

The complete PowerPC module set and modpost pass with `-j16`. DTC resolves
`audio-video-encoder` to the AVE child phandle, and both the Wii DTB wrapper and
`zImage` build. Patch-only strict checkpatch has no implementation error; its
only warning requested the binding documentation be committed separately,
which commit `9db029f3a` does. The live Wii DT lacks this phandle, so hardware
validation requires deploying the pinned boot image rather than replacing only
the module.

Hardware acceptance procedure:

1. Boot the checksum-matched image into legacy ownership. Require the live VI
   node to contain `audio-video-encoder`, client `0-0070` to exist, and AVE
   readback to begin at zero.
2. Upload and verify the pinned module set. Start explicit programmed DRM with
   ordered tracing but without the harness `--ave-swap` option. Require the
   trace to contain the known 43-write VI sequence followed by the driver's
   exact AVE write `[62-02]`, successful transfer, and matching readback before
   DRM bind completion. No userspace AVE write is permitted.
3. Start the unchanged checksum-pinned RGB565 fixture only after trace capture.
   Require correct red, green, blue, gold, white, magenta, and teal mapping on
   the first acquisition. A swapped fixture rejects this implementation.
4. Stop the exact client PID and invoke ordinary transactional restore without
   `--ave-swap`. Require the driver log to report verified `62=00` restoration,
   AVE readback zero, legacy `gcn-vifb` bound, DRM unbound, and an empty fault
   audit.
5. If the first transaction passes, repeat at least one warm transaction and
   one independent cold boot before accepting the lifecycle as reliable.

Do not count a userspace `wii-ave-reg --set-swap` action as a pass. This phase
specifically validates that kernel ownership alone establishes and restores
the encoder state.

## 2026-08-07: First driver-owned AVE transaction is correct

The checksum-pinned boot image was deployed over the network through a
RAM-staged HTTP download, verified before and after a local `/boot` copy, and
left with `/boot` remounted read-only. The rebooted live device tree contains
the resolved `audio-video-encoder` phandle, client `0-0070` exists, AVE began at
`0x62=0`, legacy `gcn-vifb` owned VI, and the boot fault audit was empty.

All six modules were staged over HTTP and matched their local checksums. The
cycle used explicit programmed mode and ordered tracing without `--ave-swap`.
The complete trace retained all 56 of 56 events with no overrun. Its VI
sequence is contiguous from 1 through 43. After the final VI mode write and
before the `drm-bound` marker, the driver generated this exact I2C sequence:

```text
write request: address 0x70, payload [62-02]
write result:  1/1 message
read request:  address 0x70, payload [62]
read reply:    [02], result 2/2 messages
```

The kernel logged `enabled AVE chroma exchange: 62=02` before mode-object
initialization and DRM registration. No harness AVE lifecycle marker existed,
so the exchange write cannot be attributed to userspace compensation.

The unchanged checksum-pinned RGB565 console started as PID 2465. AVE readback
was two, and the user classified the first displayed fixture as correct. Exact
PID termination then preceded ordinary restore without `--ave-swap`.

Driver remove logged `restored AVE chroma exchange: 62=00`. Final readback was
zero, the harness marker remained absent, legacy `gcn-vifb` was bound, DRM was
unbound, and the corrected fault audit was empty. Preserved artifacts:

```text
correct trace: 6137ba951ed1d35cef87ffcca26b2e0bce5fe2ae91b64abd34be3cdb1863d55b
complete dmesg: 19fa94a1c5d0088e3e97bd65ddd8146d9feb10b694ef2e3143d61d8f5c98680b
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

This passes one complete kernel-owned acquisition/restoration transaction. It
does not yet establish repeatability. Run one identical warm transaction and
then one independent cold boot before accepting the lifecycle.

## 2026-08-07: Driver-owned AVE warm repeat is correct

The second transaction began from the restored warm baseline with AVE zero,
legacy ownership, and the same checksum-pinned remote modules. Explicit
programmed mode and tracing again ran without `--ave-swap`.

The trace retained all 56 of 56 events, contains the byte-identical contiguous
43-write VI sequence, and independently records `[62-02]`, a successful 1/1
write, `[62]`, `[02]`, and a successful 2/2 verification before DRM bind. The
unchanged RGB565 console started as PID 3337, AVE readback was two, and the user
classified the fixture as correct.

Exact-PID termination and ordinary restore again used no harness AVE marker.
Driver remove logged verified `62=00`; final readback was zero, legacy was
bound, DRM was unbound, and the corrected fault audit was empty. Preserved
artifacts:

```text
correct trace: 2f182aee3c47595152780ba5a40d93808106e13f20cd6445b45166b9d8a0dd1f
complete dmesg: 3045e510e02ab937fd9cd4981decdb05f94252c25932a1bd341f0268ea6ecbfb
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

The kernel-owned lifecycle is correct for two consecutive warm transactions.
Complete the staged acceptance procedure with one independent reboot, fresh
module staging, and the same trace, visual, and teardown gates.

## 2026-08-07: Accept driver-owned AVE chroma-exchange lifecycle

The Wii rebooted independently into the pinned phandle-enabled image. At 59
seconds uptime, the live VI phandle and AVE client were present, AVE readback
was zero, legacy owned VI, DRM was unbound, `/tmp` contained no modules, and
the boot fault audit was empty. The six modules were freshly transferred over
HTTP and independently matched the same staged checksums.

The cold transaction again used explicit programmed mode and tracing without
`--ave-swap`. Its trace retained all 56 of 56 events, contains the same
contiguous 43-write VI sequence, and records the exact driver `[62-02]`, 1/1
write result, `[62]` read request, `[02]` reply, and 2/2 verification before
DRM bind. No userspace AVE write or lifecycle marker existed.

The unchanged RGB565 client started as PID 1797. AVE readback was two, and the
user classified the independent cold fixture as correct. Exact-PID termination
and ordinary restore then caused driver remove to log verified `62=00`.
Final readback was zero, legacy was bound, DRM was unbound, the harness marker
was absent, `/boot` remained read-only, and the corrected fault audit was empty.
Preserved artifacts:

```text
correct trace: ea9ce6d3d588c94f5ebbbb7435fc36d1541453b79705afe23d3c88bfa08d71ba
complete dmesg: 9e72578e3889b6a3b2f18af118ac31affec5ff584dd451d8e32a1fc33245b677
client log:    17f4cd55eca14522a6729bc5bf82d4d2361078c0c997970f14d9447baf85f57c
```

Accept the driver-owned AVE lifecycle. Three consecutive transactions produced
correct first-frame colors: two warm ownership cycles and one independent
reboot with fresh module staging. Every acquisition traced and verified the
kernel's `0x62=0x02` write before DRM exposure, and every teardown restored
and verified `0x62=0x00` before legacy ownership returned. No transaction used
the harness compensation path.

The former natural AVE-zero sampling path is closed. DRM can now rely on a
deterministic encoder state rather than inherited or historical color phase.
Keep the reversible userspace utility and harness option as diagnostics for
now, but do not use them during ordinary DRM operation or as evidence for
future production tests.

## 2026-08-07: Stage native DRM fbcon as the boot display owner

- `zImage` SHA-256:
  `2f389ad2ac2c54072dcd5bcc23d7d86c38193c966f56df02fca9dfcf08febc0e`
- `vmlinux` SHA-256:
  `c17d5242e2df82e328ae6f9ce6c8bca30ba9f37f7fcb77e7ae55b9bd5cc7d111`
- rollback `zImage` SHA-256:
  `b39cdf270d8c0a960ce9b47f53845137106dadb373e4d2a6ccc61cfec8392505`

Promote the accepted GCN DRM and driver-owned AVE lifecycle from a reversible
module test to normal boot ownership. Build `CONFIG_DRM_GCN_VI` and DRM into
the kernel, enable DRM fbdev emulation, and disable legacy `gcn-vifb`. Remove
the obsolete `video=gcn-vifb:tv=auto,nostalgic` boot argument while retaining
`console=tty0` for fbcon.

The GCN driver now supplies the shmem fbdev operations and calls
`drm_client_setup()` after DRM registration with RGB565 as the preferred
format. Its framebuffer creation path uses `drm_gem_fb_create_with_dirty`, so
deferred fbcon writes enter the atomic update path and reach the XFB rather
than only changing the shmem shadow buffer. RGB565 minimizes the boot console
allocation and uses the already validated conversion path on this constrained
24 MiB system.

`wii_defconfig` resolves built-in DRM, GCN VI, client setup, fbdev emulation,
the shmem helper, and framebuffer console. Legacy GameCube framebuffer support
is unset. A complete `-j16` build links `gcn_drm_probe`,
`drm_client_setup`, and `drm_fbdev_shmem_driver_fbdev_probe`; no legacy
`gcn_vifb_probe` symbol is present. The embedded DT contains the AVE phandle
and the revised boot arguments. `git diff --check` and strict checkpatch pass.

Cold-boot acceptance procedure:

1. Deploy only the checksum-pinned image, leave `/boot` read-only, and cold
   boot. Require the kernel to bind `gcn-vi`, enable and verify AVE
   `0x62=0x02`, register DRM, and create a DRM-backed fbcon without any
   `gcn-vifb` bind or userspace mirror process.
2. Require a visible console that continues updating through boot, has a
   blinking cursor, and responds to keyboard input. Confirm correct colors and
   no frozen initial frame, recurring blur, repeated columns, or conversion
   errors.
3. Over SSH, verify `/sys/class/drm/card0`, identify `/sys/class/graphics/fb0`
   as DRM fbdev, confirm AVE readback two, and audit dmesg for faults. Recheck
   the console after generating new output to prove dirty updates reach XFB.
4. Reboot once without redeploying and repeat the visual, fbdev, AVE, and fault
   gates. Do not use `tools/wii-drm-cycle.sh`: built-in DRM intentionally has
   no legacy handoff or module-unload lifecycle.

If the display fails but SSH remains available, restore the preserved rollback
image from `/tmp/zImage.ngx.rollback-driver-owned-ave-b39cdf27`, verify its
checksum before and after the local `/boot` copy, sync, and remount `/boot`
read-only. A loss of both display and network requires physical card rollback.

## 2026-08-09: Reject first native DRM boot-owner attempt

Commit `01b86015c` and its checksum-pinned image
`2f389ad2ac2c54072dcd5bcc23d7d86c38193c966f56df02fca9dfcf08febc0e`
were deployed with matching pre-copy and installed checksums. `/boot` was
returned read-only before reboot.

The display froze during boot and the Wii did not return on SSH within the
bounded post-boot check. No pstore record or persistent failed-boot log was
available, so this result does not identify whether the failure occurred in
built-in GCN DRM probe, DRM fbdev client setup, or a later boot dependency.
Do not accept this image and do not infer a specific root cause from the
frozen last frame.

Physical-card rollback restored the accepted image with SHA-256
`b39cdf270d8c0a960ce9b47f53845137106dadb373e4d2a6ccc61cfec8392505`.
The source and installed checksums matched after sync, and the boot partition
was returned read-only. The Wii subsequently booted normally with legacy
`gcn-vifb` as `fb0`, no DRM device, working SSH, and `/boot` read-only.

Before retrying built-in ownership, validate the new fbdev client path under
the existing reversible module handoff. Keep legacy `gcn-vifb` as the boot
owner, build GCN DRM as a module, unbind legacy, and load the module without a
userspace framebuffer mirror. Require DRM to create its own fbdev console,
continue rendering new console output, preserve correct colors, and restore
legacy ownership cleanly on module removal. This separates native fbcon
behavior from built-in probe ordering and retains an SSH recovery path.

## 2026-08-09: Stage reversible native DRM fbcon handoff

- `zImage` SHA-256:
  `fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`
- `gcn-drm.ko` SHA-256:
  `72c5ac10a276f93f3ab2ff4b08aa428a68a91bac331e12d437af04df62b48490`
- `drm.ko` SHA-256:
  `038eda5366251f715648d8fea770cdb2f19ec45ccc86104a14824f43ca5e65ce`
- `drm_kms_helper.ko` SHA-256:
  `b3bb89745cd925a092d7a8c249a008c0d1f4d5d49cee80ee857c516d95dfea2f`
- `drm_shmem_helper.ko` SHA-256:
  `9acfcdf4cb2c9d34e0f42fbc15fad0e6bf08f5793d22fe7be99e478948b03d07`
- `drm_client_lib.ko` SHA-256:
  `cdb90dc4d66ea83c7d28bd31a66b16ad3fdd62bb5fc027387b3ebd2bd933fa39`
- `drm_panel_orientation_quirks.ko` SHA-256:
  `fa1e9862ec9379b26b572df3b4e93878f21e563264ad6fd8353a4f27dcadd1df`

Retain the fbdev client implementation from rejected boot-owner commit
`01b86015c`, but restore the accepted ownership topology: DRM and GCN VI are
modules, legacy `gcn-vifb` is built in, and the legacy video boot argument is
present. DRM fbdev emulation remains enabled. The resolved module stack places
the default fbdev client in `drm_client_lib.ko`, and `gcn-drm.ko` declares that
module as a dependency.

The complete image and module build passes with `-j16`; `git diff --check`
passes. This test requires the new image because the accepted rollback kernel
does not contain the matching DRM fbdev client module/configuration.

Hardware procedure:

1. Deploy the checksum-pinned image and matching module stack, leave `/boot`
   read-only, and reboot into legacy ownership. Require `gcn-vifb` as `fb0`,
   working SSH, no DRM device, AVE zero, and an empty relevant fault audit.
2. Stop any process using the legacy framebuffer, unbind `gcn-vifb`, and load
   the exact module stack in dependency order. Do not start
   `wii-drm-console` or any other userspace framebuffer mirror.
3. Require `gcn-drm` to enable and verify AVE `0x62=0x02`, register `card0`,
   create a DRM fbdev `fb0`, and attach fbcon. Generate new tty output after
   bind and require it to appear on screen with a blinking cursor, correct
   colors, and no freeze, blur, repeated columns, or conversion errors.
4. Remove the exact GCN DRM module and dependencies, rebind legacy, and require
   verified AVE restoration to zero, a responsive legacy console, and no
   kernel fault. If module removal cannot complete, reboot restores the known
   legacy boot owner.

A pass validates native DRM fbcon independently of built-in probe ordering. A
freeze at module load with SSH still alive localizes the rejected boot result
to DRM/fbdev ownership rather than unrelated late boot initialization.

## 2026-08-09: Accept reversible native DRM fbcon lifecycle

The checksum-pinned image booted normally into legacy ownership. At the
baseline gate, `gcn-vifb` was `fb0`, DRM was absent, SSH was responsive, and
`/boot` was read-only. All six staged modules independently matched the hashes
recorded above.

The first generic-module preflight attempted to load `drm_client_lib` before
`drm_kms_helper`. It failed with unresolved `drm_fb_helper_*` symbols while
legacy remained bound; no display transition occurred. Loading in the verified
order `drm`, `drm_kms_helper`, `drm_shmem_helper`, then `drm_client_lib`
succeeded. Preserve that order in future automation.

The exact GCN module was preloaded unbound, the GX helper was removed, and the
legacy platform device was unbound. Binding `c002000.video` to `gcn-vi`
completed normally. The driver programmed VI, enabled and verified AVE
`0x62=0x02`, registered `card0`, and created `fb0` as `gcn-vidrmfb`. Fbcon
switched from the dummy console to the DRM framebuffer. No
`wii-drm-console`, `wii-drm-test`, or other framebuffer mirror process ran.

The user classified the initial console as normal. Three separately timed
`/dev/tty0` writes labeled `NATIVE DRM DIRTY UPDATE 1/3` through `3/3` all
appeared clearly. This is a positive hardware control for the
`drm_gem_fb_create_with_dirty` plus shmem fbdev update path: native fbcon can
update the GCN XFB without a userspace copy loop.

Module removal completed, logged verified AVE restoration to `0x62=0x00`, and
removed `card0`. Rebinding legacy recreated `fb0` as `gcn-vifb`; `gcn_gx`
re-registered and the restoration marker appeared normally. The user
classified the restored console as normal. The remaining idle DRM modules
were unloaded after the lifecycle, leaving only `gcn_gx`, legacy ownership,
AVE zero, `/boot` read-only, and an empty relevant fault audit.

Preserved artifacts:

```text
complete dmesg: 69e8ea3a7f982e97816a913822d1525e67fe4458119cd44612b043e0d007eae8
handoff log:    12f38041cd3ffaf4a438ea3e81b7bbcf3802476df234e299a19dfe34320f6d18
restore log:    b82d5bf42cfa020f4003d00929490dc90ce9c9c0d161af4eb606c665fe8524fe
```

Accept the native DRM fbcon implementation and reversible ownership lifecycle.
The rejected built-in image is no longer evidence against fbdev dirty updates;
the same implementation works when loaded after boot. The remaining problem
is boot-time ownership/probe interaction. Update `wii-drm-cycle.sh` for
`drm_client_lib` and no-mirror native-fbcon validation before designing the
next built-in isolation test.

## 2026-08-09: Stage built-in DRM probe without fbcon

- `zImage` SHA-256:
  `4e18496d4139cc0e737dc78d0143a31f9fe008190651197de564f43cebc26b48`
- `vmlinux` SHA-256:
  `4a4dc41f57ce4c12fe202dafe7c4bf6231e99eb4d782e40d2b3df5eb87c8cdf4`
- accepted modular rollback image SHA-256:
  `fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`

The reversible module test accepts the native fbcon implementation and dirty
update path. Isolate the rejected built-in boot by retaining built-in DRM and
GCN VI with legacy disabled, but add the documented DRM client selector
`drm_client_lib.active=none` to the kernel command line. The GCN probe still
maps and programs VI, owns the IRQ, enables AVE chroma exchange, registers
`card0`, and returns; `drm_client_setup()` deliberately starts no fbdev client.
No driver implementation changes in this test.

The complete `-j16` image build passes. The resolved config contains built-in
DRM, GCN VI, client selection, and fbdev emulation with legacy GameCube
framebuffer disabled. The embedded DT contains the AVE phandle and exact
`active=none` command line. `git diff --check` passes.

Hardware procedure and outcome matrix:

1. Deploy the checksum-pinned image, verify the installed checksum, leave
   `/boot` read-only, and reboot. The display is expected to retain a static
   pre-DRM frame because neither legacy nor DRM fbdev will update it. Do not
   classify that expected display freeze as a machine failure.
2. Poll SSH independently. If SSH returns, require built-in `gcn-vi` bound,
   `card0` present, no `/proc/fb` entry, AVE `0x62=0x02`, the command line to
   contain `drm_client_lib.active=none`, and an empty relevant fault audit.
   This passes built-in probe and localizes the rejected image to early fbcon
   setup or boot-time console updates.
3. If SSH does not return within the same bounded boot interval, reject
   built-in GCN DRM probe independently of fbcon. Physical-card rollback is
   then required because a static screen alone provides no diagnostic stage.
4. After either result, restore the accepted modular image
   `fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`,
   verify the installed checksum, and leave `/boot` read-only.

Do not run the modular cycle harness on this image. Its purpose is only to
separate built-in platform probe from DRM fbcon startup.

## 2026-08-09: Reject built-in probe-only boot

Commit `5d485df76` and image
`4e18496d4139cc0e737dc78d0143a31f9fe008190651197de564f43cebc26b48`
were deployed with matching staged and installed checksums. `/boot` was
verified read-only before reboot.

The first SSH check began after a 20-second wait and timed out after eight
additional seconds. After a further 25-second bounded wait, the final check
failed with `No route to host`. The Wii therefore did not reach the known
network baseline in more than 50 seconds. No framebuffer client could have
started because the embedded command line explicitly selected
`drm_client_lib.active=none`.

Reject early native fbcon activity as the explanation for the first built-in
boot failure. This result does not yet distinguish GCN's built-in platform
probe from the larger built-in DRM core/image or another configuration/order
interaction. The intentionally static display is not diagnostic and no live
log can be recovered without network or persistent storage.

Physical-card rollback to the accepted modular image is required. After
rollback, isolate one level earlier: build DRM core in but keep GCN VI modular
and retain legacy `gcn-vifb` as the boot owner. A successful ordinary legacy
boot would validate the larger built-in DRM core and leave GCN's built-in
registration/probe ordering as the remaining suspect. A failure before any
GCN ownership change would instead implicate built-in DRM core size or config.

Physical-card rollback subsequently restored the accepted modular image
`fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`.
Source and installed checksums matched after sync, and the boot partition was
returned read-only. The Wii booted normally: SSH returned, legacy `gcn-vifb`
was `fb0`, DRM was absent, AVE read zero, and `/boot` remained read-only.

## 2026-08-09: Stage built-in DRM-core-only boot

- `zImage` SHA-256:
  `1387b9556c35827030fda945e457cf3b626c400b345724234e90772532952163`
- `vmlinux` SHA-256:
  `655ccd6556a5e8034bccde5f30baf96efe7ac47e670ad870f2bfc7f7dc8063e0`
- modular `gcn-drm.ko` SHA-256:
  `477aa26650a550e887542d010198461e66e7e46eedf484584dce6e750ac699c7`
- accepted rollback image SHA-256:
  `fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`

Isolate the failed built-in GCN boot one level earlier. Keep DRM core built in,
but build GCN VI and the DRM client/helper stack as modules. Restore built-in
legacy `gcn-vifb`, modular `gcn_gx`, and the accepted legacy video argument.
Add the kernel-enforced `module_blacklist=gcn_drm` parameter so userspace
cannot auto-load either an installed stale GCN module or the newly built one.

The complete `-j16` image and module build passes. The uncompressed kernel is
`0x12c8d00`, only `0x288` bytes smaller than the failed all-built-in image at
`0x12c8f88`, making this a strong image-size control. `System.map` contains
built-in `drm_dev_register` but no `gcn_drm_probe`. The embedded DT contains
the AVE phandle, legacy video argument, and exact kernel module blacklist.
`git diff --check` passes.

Hardware procedure and outcome matrix:

1. Deploy and verify the checksum-pinned image, leave `/boot` read-only, and
   reboot. Require the ordinary updating legacy console rather than a static
   last frame.
2. Require SSH to return, `gcn-vifb` to own `fb0`, AVE to remain zero, no
   `card0`, no `gcn_drm` module, and the live command line to contain
   `module_blacklist=gcn_drm`. Audit for faults and confirm `/boot` read-only.
3. A pass validates the built-in DRM core and near-identical image size,
   localizing the previous failures to built-in GCN registration/probe timing.
   A failure before any GCN load implicates the built-in DRM core/config or
   image layout instead.
4. This image retains the accepted legacy boot owner, so a successful boot is
   directly usable. Do not attempt the modular handoff until the matching
   helper/client modules are staged and the blacklist implications are
   understood.

## 2026-08-09: Reject DRM-core-only boot and identify wrapper overflow

Commit `750b3a2b1` and image
`1387b9556c35827030fda945e457cf3b626c400b345724234e90772532952163`
were deployed with matching staged and installed checksums. The display froze
during boot. An SSH probe after the initial bounded wait timed out, and a
second probe after another 25 seconds failed with `No route to host`. The Wii
did not reach the network baseline even though GCN DRM was modular and
kernel-blacklisted and built-in legacy `gcn-vifb` remained configured.

Reject both the GCN DRM platform probe and native fbcon as causes of this boot
failure. The near-identical uncompressed sizes of this image (`0x12c8d00`)
and the previous all-built-in failure (`0x12c8f88`) instead exposed a PowerPC
Wii boot-wrapper limit:

```text
failed image entry/load address: 0x01300000
failed wrapper file extent:      0x01300000-0x01920acc
failed wrapper memory extent:    0x01300000-0x019234a0
Wii MEM1 extent:                 0x00000000-0x01800000
accepted image entry/load:       0x00f00000
accepted wrapper memory extent:  0x00f00000-0x014f24a0
```

`arch/powerpc/boot/wrapper` defaults Wii to `0x00600000`, then relocates the
wrapper to the next MiB above the uncompressed kernel whenever the two would
overlap. For this image that generic rule selected `0x01300000`. The resulting
`PT_LOAD` segment extends more than one MiB beyond the end of physical MEM1.
It also crosses the DTS-reserved GX texture, FIFO, and XFB ranges, although the
hard physical-memory overflow is sufficient to reject the layout.

The platform wrapper independently confirms this limit. `wii.c` initializes
its allocator with:

```c
u32 heapsize = 24*1024*1024 - (u32)_end;
simple_alloc_init(_end, heapsize, 32, 64);
```

With `_end=0x019234a0`, the heap-size subtraction underflows. The accepted
modular image ends at `0x014f24a0`, remains below 24 MiB, and boots. This is a
positive and negative layout control using the same boot wrapper and hardware.

Physical-card rollback restored the accepted modular image
`fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`.
Source and installed checksums matched after sync, and `/boot` was remounted
read-only. Do not make another DRM ownership change for the next test. Move
the Wii boot wrapper and its allocator to a valid MEM2 window, first verifying
the decompressor's source/destination and Gumboot ELF-load assumptions, then
re-run this exact DRM-core-only configuration as the control.

## 2026-08-09: Stage Wii boot wrapper in MEM2

- `zImage` SHA-256:
  `2bb4d24654ba734151bcfb5af8e3e1ae11b50759cee6f8a5bf2be6efa1ad7be6`
- unchanged `vmlinux` SHA-256:
  `655ccd6556a5e8034bccde5f30baf96efe7ac47e670ad870f2bfc7f7dc8063e0`
- accepted rollback image SHA-256:
  `fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`

Correct the boot-image layout without changing the kernel or DRM test
configuration. Link the Wii wrapper at `0x10010000`, immediately after the
16 KiB MEM2 DSP reservation, and disable the generic MEM1-oriented
`make_space` relocation for Wii only. GameCube retains its existing
`0x00600000` behavior.

When a Wii wrapper resides above MEM1, initialize its simple allocator from
`_end` to mini's reported MEM2 boundary. Retain the existing 24 MiB MEM1 heap
for smaller/older wrappers. If mini's header cannot be read, use the existing
conservative fallback top of `0x13400000`; if `_end` reaches either selected
ceiling, fail explicitly instead of allowing unsigned subtraction to
underflow.

The boot flow was checked against the exact sources involved:

- `wii-head.S` installs an identity BAT for the complete
  `0x10000000-0x14000000` MEM2 range before entering C code.
- The generic wrapper decompresses the kernel to address zero. Its overlap
  checks now compare the MEM2 `_start`/`_end` against the unchanged kernel
  load and memory sizes, so the MEM1 output cannot overwrite the wrapper or
  relocated FDT.
- Gumboot revision `de22677b99f0` sends the complete ELF buffer to mini via
  `IPC_PPC_BOOT`; it does not impose a MEM1 load address.
- BootMii's published ELF loader revision `0b3705f9705e` copies each `PT_LOAD`
  segment to its declared `p_paddr` and enters at `e_entry`, with no MEM1-only
  address filter. MEM2 physical load addresses are therefore representable by
  the loader path.

The complete `-j16` build passes, as do wrapper shell syntax and
`git diff --check`. Final ELF layout:

```text
entry/PT_LOAD start: 0x10010000
PT_LOAD file end:    0x10630acc
PT_LOAD memory end:  0x106334a0
conservative top:    0x13400000
```

The `vmlinux` checksum exactly matches the rejected DRM-core-only image. This
is therefore a direct A/B test of wrapper placement, not another DRM change.
Deploy the pinned image and require the ordinary legacy console and SSH
baseline. A pass validates the MEM2 wrapper and proves the earlier failures
were boot-image layout failures. Then inspect the live command line,
`gcn-vifb` ownership, absence of `gcn_drm`, AVE zero, fault log, and read-only
`/boot` exactly as specified for the DRM-core-only test. A failure still needs
physical rollback, but should be classified by the earliest visible Gumboot,
wrapper LED, or console stage rather than attributed to DRM.

## 2026-08-09: Accept MEM2 wrapper boot, retain display failure

Commit `c37e03e16` and image
`2bb4d24654ba734151bcfb5af8e3e1ae11b50759cee6f8a5bf2be6efa1ad7be6`
were deployed with matching staged and installed checksums. The user saw
Gumboot, one frame of color static, and then a frozen display. The front disc
slot light came on and remained lit.

Do not classify either observation as a machine failure. `wii-head.S`
deliberately sets the front blue LED immediately after installing the Wii BATs
as its permanent "wrapper entered" marker; it contains no matching clear. The
returned persistent log proves that the kernel completed boot despite the
display:

- the MEM2 wrapper entered and handed a valid FDT to the kernel;
- legacy `gcn-vifb` probed as `fb0` with software conversion selected;
- the root filesystem mounted and `/init-diag.sh` started;
- Wi-Fi completed its four-way handshake;
- DHCP bound `10.3.10.12`, and gateway and host pings both passed;
- OpenSSH listened on port 22; and
- the script wrote its final completion marker at 47.6 seconds.

The two live SSH probes were false negatives caused by timing. The diagnostic
boot does not start OpenSSH until after an eight-second WPA stability gate, a
25-second `dhclient` timeout, and two three-packet ping checks. It reached the
listen socket at 46.4 seconds, later than the bounded probes, and the card was
removed before another probe. Increase this diagnostic image's SSH readiness
window to at least 60 seconds in future tests.

Preserved artifact hashes:

```text
dmesg.txt:       11f4b793435b132fba3dba4775ea6751186f4659273e9870ee30430eb7d887d9
early-dmesg.txt: 25172cb06fca5fb6dd7e70ebe50fc9f5b29d3cdc7e29d8ac372452b640ac0639
wpa-debug.txt:   e5dbe6790240365c0b661b56fee79f6d93e60f4944a2cd8080f89347b45aa0cb
sshd-debug.txt:  6e453408f190b8b75a3b9ba0128cbb2be464feb1edf0bfea292922d3de9b4e59
```

Accept the MEM2 wrapper and built-in DRM-core boot. The remaining visual
failure is real but is not evidence of a stopped CPU.

The enlarged kernel exposes a second independent layout conflict. Its
`PT_LOAD` memory extent is `0x00000000-0x01288708`, while the Wii DTS still
reserves `0x01200000-0x01380000` for two GX texture buffers. The final kernel
therefore occupies the first `0x88708` bytes of the old texture reservation.
`gcn_gx` was not loaded by this diagnostic init path, so this overlap did not
prevent the successful boot, but any later GX texture upload at the old fixed
address can overwrite live kernel data or BSS.

Before restoring the normal modular GX lifecycle, relocate only the GX texture
reservation to a verified MEM1 range above the kernel and below the existing
OHCI pool. The GX texture unit cannot address MEM2. Keep the accepted MEM2
wrapper, DRM-core-only payload, XFB addresses, FIFO address, and diagnostic
init path unchanged. Validate the new reserved range in the early memory log
before loading `gcn_gx`; then load it explicitly and require a clean fault
audit.

## 2026-08-09: Stage relocated MEM1 GX texture window

The accepted DRM-core kernel occupies `0x00000000-0x01288708`, overlapping
the former GX texture reservation at `0x01200000-0x01380000`. Move the two
fixed RGB565 texture slots to `0x01300000` and `0x013c0000`, with one combined
DTS reservation of `0x01300000-0x01480000`. Each slot remains 768 KiB, enough
for the maximum 640x576 RGB565 upload of 737,280 bytes.

This placement leaves `0x778f8` bytes (about 478 KiB) between the current
kernel end and the first texture slot, and `0x80000` bytes (512 KiB) between
the reservation end and the OHCI pool at `0x01500000`. The GX FIFO at
`0x01684000` and both XFBs beginning at `0x01698000` are unchanged.

Add a Wii-only wrapper build guard that reads the first kernel `PT_LOAD`
physical address and memory size from `objdump -p`, computes the in-memory
kernel end, and rejects any image extending beyond `0x01300000`. Failure to
parse the extent is also fatal. GameCube wrapper behavior is unchanged. A
positive/negative parser assertion measured the current end as `0x01288708`,
confirmed that it exceeds the obsolete `0x01200000` boundary, and confirmed
that it fits below the new boundary. `bash -n`, `git diff --check`, and the
complete `ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make zImage modules
-j16` build pass.

The embedded DT reserve map was extracted from the image and independently
checked. It contains the texture reservation at `0x01300000` with size
`0x00180000`, the FIFO at `0x01684000` with size `0x00010000`, and the XFB
reservation at `0x01698000` with size `0x00168000`. The wrapper remains in
MEM2 with entry `0x10010000` and memory end `0x106334a0`.

Pinned test artifacts:

```text
zImage.ngx: 685a7a7fe92bef21cf1cab34528d64b3fc5aad5c65afce9bc887bc02af52254d
gcn-gx.ko:  0b2f29bc94df5e0b4ef6c64239334529bd8606f5c0474b28c9d4458e2b038556
vmlinux:    655ccd6556a5e8034bccde5f30baf96efe7ac47e670ad870f2bfc7f7dc8063e0
```

Deploy the image and matching module together. Boot the diagnostic init and
wait at least 60 seconds even if the display remains frozen; the wrapper's
disc-slot light is a permanent entry marker, not a completion signal. Over
SSH, first verify the baseline and early reservation log, then explicitly
load the pinned `gcn-gx.ko`, because this diagnostic init does not load it.
Require `gcn-gx: ready fifo=01684000 tex=01300000/013c0000`, drained FIFO
diagnostics, continued network/CPU responsiveness, and no oops, exception,
machine check, or memory corruption. Record the full-frame display result
separately from machine liveness.

Hardware result: reject this image before module loading. Two cold boots of
the exact checksum-pinned image both left the normally transient color-static
frame on screen permanently. The wrapper-entry disc-slot light remained on,
but that light is intentionally latched and is not a crash indicator. After a
90-second window, ARP resolution for `10.3.10.12` was `FAILED`, ping returned
destination-host-unreachable, and SSH returned no-route-to-host.

The first return appeared to contain diagnostics, but all four files were
byte-identical to the previous accepted boot. Before the exact-image retry,
those files were renamed in place. The second return contained no new
`early-dmesg.txt`, `dmesg.txt`, `wpa-debug.txt`, or `sshd-debug.txt`, proving
that `/init-diag.sh` was never entered. The installed image and module still
matched the pinned hashes after the failed boot. Do not attribute failure to
the static frame itself; the user confirms that a brief instance is part of
normal Wii Linux startup. Only its persistence, absent networking, and absent
fresh logs establish the failed progress boundary.

Run a direct A/B boot-path control before changing another address. Restore
the already accepted MEM2-wrapper image from commit `c37e03e16`, SHA-256
`2bb4d24654ba734151bcfb5af8e3e1ae11b50759cee6f8a5bf2be6efa1ad7be6`,
which retains the old `0x01200000-0x01380000` texture reservation. Leave the
root filesystem and unloaded relocated-address `gcn-gx.ko` unchanged. The
diagnostic init does not load that module, so a successful boot and fresh logs
would isolate the regression to the image's DT reservation change rather than
the card, root filesystem, Wi-Fi setup, or module contents. The control is not
safe for loading `gcn-gx.ko`; its old reservation overlaps the enlarged live
kernel.

Control result: the accepted MEM2-wrapper image also failed under the current
boot conditions. Its normally transient static frame remained on screen, the
Wii never answered ARP, ping, or SSH after the full startup window, and the
root filesystem contained none of the four diagnostic files that had been
removed before boot. This disproves the relocated GX reservation as the cause
of the present failure. Do not change GX, framebuffer, DRM, or reservation
code while this shared pre-init boot failure is unresolved.

The host mounts the root partition read/write. `/init-diag.sh` remains an
executable POSIX shell script with SHA-256
`cf579d2b8681304eaf06b982fd729b7e48da9aa7f670a17b5f15efcf7efc7367`,
and its `/bin/sh` interpreter resolves to `dash`. Its first post-mount action
would create `dmesg.txt`; absence of that file places the failure before PID 1.

Use the preserved pre-DRM modular-fbcon image as a broader boot-path positive
control, without changing the root filesystem or any graphics source. Its
SHA-256 is
`fbf92f081b1cd5c8e35d65c9ec2175d3cb00c25ababc82d54a1f9d5f54220c81`.
Do not load the installed `gcn-gx.ko` during this control. A successful boot
would localize the failure to the newer built-in-DRM/MEM2-wrapper image family;
another pre-init failure would instead require wrapper and early-kernel stage
instrumentation or boot-media diagnosis before graphics work resumes.

Correction: retract the recent pre-PID1 classifications. Static inspection of
all three deployed images shows that none embeds `init=/init-diag.sh`; each
contains only its DTS default `root=/dev/mmcblk0p2 rootwait ro ...` command
line. The current `gumboot.lst` invokes `kernel /zImage.ngx` without appended
arguments. Therefore absence of the four diagnostic files is expected and
does not place the failure before PID 1.

The normal root filesystem explicitly lists `gcn_gx` in `/etc/modules` line
11. The modular-fbcon control reached a normal visible console before failing,
consistent with normal userspace starting and then loading the installed
relocated-address module against an incompatible old-layout kernel. Do not use
that run as a clean boot positive control and do not infer a graphics-code
regression from it. The two MEM2-wrapper runs likewise had neither the intended
diagnostic PID 1 nor an expected automatic Wi-Fi/SSHD path, so their missing
network and logs are not valid boot-failure controls.

Restore a deterministic boot harness in the SD-card `gumboot.lst` by appending
`root=/dev/mmcblk0p2 rootwait rw init=/init-diag.sh console=tty0
video=gcn-vifb:tv=auto,nostalgic udbg-immortal log_buf_len=1M
module_blacklist=gcn_drm,gcn_gx` to the kernel line. This is boot-media test
configuration, not a kernel or graphics-source change. Re-run the preserved
modular-fbcon image first. Success requires fresh diagnostics and SSH; do not
load either graphics module. Only after that control passes should the two
MEM2-wrapper images be compared under the exact same command line.

The Gumboot override attempt failed before Linux with `could not edit
bootargs: -1` and `MINI boot failed: -1`. Restore the original `gumboot.lst`;
this Mini version does not support Gumboot's runtime bootargs-edit operation.

## 2026-08-09: Stage embedded diagnostic PID 1 control

- Isolated boot-only branch: `test/wii-embedded-diag-bootargs`
- Test commit: `202db1f06`
- Base commit: accepted modular-fbcon source `e344ac116`
- `zImage` SHA-256:
  `da36498313772c9428957e2b42abdcfc412c2f23be35dc5c013362dfd59a834b`
- `vmlinux` SHA-256:
  `b6ca02f67be0f7ff47f835c021281bbcf1f5269633d1166bc85def69de33b3a1`
- Matching but intentionally undeployed `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Build from the exact accepted modular-fbcon source and configuration, changing
only the Wii DT `/chosen/bootargs` string. Embed `rootwait rw
init=/init-diag.sh` and `module_blacklist=gcn_drm,gcn_gx`; retain legacy
`gcn-vifb` ownership. The complete clean `zImage modules -j16` build and an
incremental verification build pass, `git diff --check` passes, and static
inspection of the final image finds the complete intended command line.

Deploy only the image. Leave the root filesystem and installed graphics module
unchanged because neither module may load in this control. Restore the original
Gumboot kernel line with no arguments. Success requires visible diagnostic
status, newly created diagnostic files, SSH, a live command line containing the
embedded PID 1 and both blacklists, legacy `gcn-vifb` as `fb0`, and no loaded
GX/DRM modules or kernel fault. This is a boot-harness validation, not a
graphics test.

Hardware result: accept the embedded diagnostic boot harness. The exact image
booted to `/init-diag.sh`, brought up Wi-Fi and OpenSSH, and remained live over
SSH. `/proc/cmdline` contains the complete embedded argument string including
both module blacklists. Legacy `gcn-vifb` owns `fb0`; neither `gcn_gx` nor any
DRM module is loaded. DHCP's bounded foreground command returned 124 after the
lease was already installed, as in earlier accepted runs; gateway and host
pings passed and SSH was ready at 48.4 seconds.

Preserved live log hashes:

```text
dmesg.txt:       e41046bd1ed4ef2b85b0bc9acd4b51c60710803bf2119caac0956f4dde858d26
early-dmesg.txt: 2b4fb0dbf08dfcb4c2c22a8ed8981e3f94397db3050f8eb05537bc13676c3067
wpa-debug.txt:   08098a641ad682dc5a90484e63e1602b8caea267947b014b68630a7cec528058
sshd-debug.txt:  6e453408f190b8b75a3b9ba0128cbb2be464feb1edf0bfea292922d3de9b4e59
```

The only warning is the already documented PowerPC alignment exception from
`memset()` in `dma_alloc_from_dev_coherent()` while OHCI initializes its MEM1
pool. Execution recovers, both boot and networking complete, and this is not a
new fault. Use this embedded-DT method for subsequent boot controls; do not
attempt runtime arguments through Gumboot on the current Mini version.

## 2026-08-09: Stage MEM2-wrapper embedded diagnostic control

- Isolated boot-only branch: `test/wii-mem2-wrapper-embedded-diag`
- Test commit: `6bc326bcb`
- Base commit: accepted MEM2-wrapper source `c37e03e16`
- `zImage` SHA-256:
  `a7814f82666fcc71249653db5638590786e1ad25d9ff26d961b7521ad7708c4f`
- `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Matching but intentionally undeployed `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Build from the previously accepted MEM2-wrapper source and configuration,
changing only the Wii DT chosen bootargs. The final wrapper remains linked at
`0x10010000`; its `PT_LOAD` file end is `0x10630acc` and memory end is
`0x106334a0`, matching the accepted layout. The complete clean and incremental
`zImage modules -j16` builds pass, `git diff --check` passes, and the image
contains the validated embedded diagnostic command line with both graphics
module blacklists.

Deploy only this image over the accepted diagnostic rootfs. Success requires
the same gates as the modular-fbcon harness: fresh logs, SSH, exact live
command line, legacy `gcn-vifb` as `fb0`, no GX/DRM module, and no new fault.
This isolates the larger built-in DRM-core kernel plus MEM2 wrapper without
loading any graphics accelerator or DRM driver.

Hardware result: reject the long-bootargs MEM2-wrapper rebuild before PID 1.
Two boots of the exact image remained on the normally transient static frame
with the wrapper light latched. Neither boot answered ARP, ping, or SSH. The
first returned root filesystem contained no newly created diagnostic file,
proving that the embedded init was not entered; the exact-binary retry
reproduced the same liveness failure.

Do not interpret this as a graphics result. No graphics module loaded and no
graphics source differs from the accepted MEM2-wrapper base. The wrapper
`PT_LOAD` boundaries also match the accepted layout. Isolate the remaining DT
input difference by shortening the embedded command line to only
`root=/dev/mmcblk0p2 rootwait rw init=/init-diag.sh console=tty0
module_blacklist=gcn_drm,gcn_gx`. Omit the optional video override,
`udbg-immortal`, and enlarged printk buffer for this control. If the short form
boots, investigate wrapper/FDT handling of the larger property; if it fails,
compare a fresh unmodified-base rebuild before changing boot code.

## 2026-08-09: Stage short-bootargs MEM2-wrapper isolation

- Test commit: `5dfff227b`
- `zImage` SHA-256:
  `17ac3e1d4aee7fe0e4a4b4a0ed76833f55596afc1748f6924ce83cc69cdfac48`
- Unchanged `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Unchanged, undeployed `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Change only the embedded DT string from the rejected long form to
`root=/dev/mmcblk0p2 rootwait rw init=/init-diag.sh console=tty0
module_blacklist=gcn_drm,gcn_gx`. The incremental `zImage modules -j16` build
regenerated only the DT and wrapper. `vmlinux`, the module, and the wrapper
`PT_LOAD` boundaries remain unchanged. Static inspection confirms the exact
short command line in the final image.

Deploy only the image and repeat the diagnostic/SSH gates. A pass isolates the
failure to optional bootargs or resulting FDT contents. A failure requires a
fresh unmodified-base rebuild comparison; do not change graphics code.

Initial hardware result retracted: the SD adapter returned with its physical
write-protect switch engaged. The host reported `RO=1` for the disk and both
partitions, and the ext3 root mounted read-only. Absence of diagnostic files in
that run therefore could not prove that PID 1 was not entered.

Checksum-identical rerun result: reject the short-bootargs image before PID 1.
Before boot, the host reported `RO=0` for the disk and both partitions, mounted
the root read-write, and successfully created, inspected, removed, and synced a
test file. The deployed `zImage.ngx` still hashed to
`17ac3e1d4aee7fe0e4a4b4a0ed76833f55596afc1748f6924ce83cc69cdfac48`.
The Wii remained on the normally transient static frame, never appeared on the
network, and the returned writable root contained none of `early-dmesg.txt`,
`dmesg.txt`, `wpa-debug.txt`, or `sshd-debug.txt`. This independently confirms
that `/init-diag.sh` was not entered. Bootargs length and the removed optional
parameters are therefore ruled out for this MEM2-wrapper rebuild.

## 2026-08-09: Stage fresh unmodified MEM2-wrapper rebuild

- Test commit: `6e4efe0f0`
- Source base: `c37e03e16`
- `zImage` SHA-256:
  `82320b32b7d6b8e4531aab8ef52f51fa048726e2c31ded7cad90216759dd37a5`
- `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Matching, undeployed `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Restore the exact original DTS command line. A source-tree comparison confirms
that `wii.dts` and all implementation files now match accepted base
`c37e03e16`; only generated build identity differs from the historical image.
The incremental `zImage modules -j16` build passes. Wrapper entry and memory
end remain `0x10010000` and `0x106334a0`.

This image starts normal userspace and contains no diagnostic PID 1. Prevent
the rootfs `/etc/modules` entry from loading GX by temporarily moving the
installed `gcn-gx.ko` out of its module path; preserve it for restoration. Do
not alter `/etc/modules` or graphics source. Success is a stable legacy console
through normal boot. If this fresh base fails while historical image
`2bb4d246...` succeeds under the same disabled-module rootfs, investigate
build reproducibility and generated payload differences.

Hardware result: reject the fresh unmodified-base rebuild. The on-card image
matched `82320b32b7d6b8e4531aab8ef52f51fa048726e2c31ded7cad90216759dd37a5`,
Gumboot remained unchanged, and the installed `gcn-gx.ko` was renamed out of
its module path before boot. The normally transient static frame remained for
the full observation window and no legacy console appeared. The returned card
still contained the exact image and the module remained disabled.

This is not a graphics result. Although the source and wrapper boundaries
match `c37e03e16`, this rebuild's `vmlinux` hash (`e2a30e3f...`) differs from
the historical accepted build (`655ccd65...`). The two `zImage` files have the
same 6,498,360-byte size but first differ inside the compressed payload. Run a
direct binary control next: deploy preserved historical image
`2bb4d24654ba734151bcfb5af8e3e1ae11b50759cee6f8a5bf2be6efa1ad7be6`
without restoring the disabled module or changing the root filesystem. A pass
would establish a build-reproducibility or payload-content problem; a failure
would invalidate the earlier acceptance under current boot-media conditions.

## 2026-08-10: Stage unchanged historical image with rootfs diagnostic init

- Historical `zImage` SHA-256:
  `2bb4d24654ba734151bcfb5af8e3e1ae11b50759cee6f8a5bf2be6efa1ad7be6`
- Preserved SysV `/sbin/init` SHA-256:
  `9bb25e184d04bbb5768cb37961835abacf3499b807374707ba8d84ee0d8fd014`
- Existing `/init-diag.sh` SHA-256:
  `cf579d2b8681304eaf06b982fd729b7e48da9aa7f670a17b5f15efcf7efc7367`

The historical image's default command line starts normal userspace, so a
persistent static display alone cannot distinguish a display freeze from a
kernel failure. Keep the historical image byte-exact and make the diagnostic
PID 1 deterministic through the root filesystem instead: rename the regular
SysV init binary to `/sbin/init.sysvinit.sha256-9bb25e18` and install relative
symlink `/sbin/init -> ../init-diag.sh`. Keep the installed `gcn-gx.ko` renamed
to `gcn-gx.ko.disabled-fresh-base`; the script does not load it.

The host mounted ext3 read-write and the redirect plus backup were synced and
read back with the hashes above. Afterward the USB card-reader device node
disappeared while the cached mount remained visible as read-only. Kernel logs
contain no ext3 error and the block-device hardware read-only flags stayed
zero, so treat this as a host reader/adapter disconnect. Unmount the stale
mount and physically reseat the card before boot. Success requires fresh
diagnostic files and SSH after at least 60 seconds; display state and the
wrapper LED are not liveness criteria.

Hardware result: the historical image does not currently reproduce its earlier
accepted boot. The returned card still contained exact image `2bb4d246...`,
the diagnostic `/sbin/init` symlink and checksum-verified SysV init backup were
intact, and `gcn-gx.ko` remained outside its module path. Nevertheless, no
diagnostic file was created and SSH never appeared during the full window.
The static frame is therefore accompanied by failure to reach the deterministic
rootfs diagnostic init, not merely an unobserved normal-userspace boot.

Do not erase the earlier accepted log evidence: this same historical artifact
did complete boot once. Its present failure means fresh-build reproducibility
is not the sole issue and points to a state-sensitive MEM2 wrapper/loader path
or a changed boot-media environment. Re-run the proven pre-MEM2 modular
embedded-diagnostic image `da36498313772c9428957e2b42abdcfc412c2f23be35dc5c013362dfd59a834b`
without changing the rootfs redirect or disabled module. A pass localizes the
current failure to the MEM2-wrapper image family. A failure means the previously
validated card/loader environment itself no longer reproduces and must be
repaired before wrapper analysis continues.

Hardware result: accept the pre-MEM2 modular environment control. Exact image
`da36498313772c9428957e2b42abdcfc412c2f23be35dc5c013362dfd59a834b`
booted visibly and reached SSH at 61 seconds. The live command line contains
the embedded diagnostic init and both graphics-module blacklists, `gcn-vifb`
owns `fb0`, and no GX or DRM module is loaded. The script completed at 47.6
seconds after WPA association, DHCP, successful gateway and host pings, and
OpenSSH startup.

Preserved control hashes:

```text
dmesg.txt:       9f934006bfbe49f68fcbbaeebfc6bb437dc7c6527dc7c1eb8239dd9c9a97d3ee
early-dmesg.txt: c69323435c00def53e20fb97c3142a697a1da9b0405368924c09f8bcee28f61a
wpa-debug.txt:   d5224b496d107139089ebb7ce285bbe5a680a4b1ddfe832fe3094f8bdb30cc22
sshd-debug.txt:  6e453408f190b8b75a3b9ba0128cbb2be464feb1edf0bfea292922d3de9b4e59
```

This clean A/B result localizes the present failure to the MEM2-wrapper image
family, not the SD card, Gumboot, Mini's general ELF path, root filesystem,
diagnostic script, Wi-Fi, or graphics modules. Audit wrapper entry, MEM2 cache
coherency, heap/FDT relocation, and decompression before returning to GX work.

Primary-source audit identifies the deterministic boundary. Gumboot revision
`de22677b99f0` reads the complete ELF, leaves it unchanged when no command-line
arguments are supplied, flushes that source buffer, and sends it through
`IPC_PPC_BOOT`. Its optional argument editor requires literal
`mark.start=1`/`mark.end=1` markers, explaining the earlier `-1` result on this
wrapper. Mini revision `fc1234b22df9` validates and copies each `PT_LOAD`,
flushes the Starlet cache and AHB writes, resets Broadway, and enters the ELF.
This weakens a stale-loader-cache explanation.

The MEM2 wrapper initializes `simple_alloc` above its own `_end`, and
`fdt_init()` unconditionally relocates the device tree with that allocator.
The finalized FDT is therefore passed to Linux at a physical MEM2 address near
`0x10600000`. In `head_book3s_32.S`, Linux preserves `r3`, clears all wrapper
BATs, and installs one initial 256 MiB BAT mapping physical
`0x00000000-0x0fffffff`. MEM2 begins at `0x10000000`, immediately beyond that
mapping. `machine_init()` later dereferences `__va(dt_ptr)` before the full MMU
is initialized, so an MEM2 FDT is inaccessible at exactly this stage.

Test this mechanism without changing graphics or the kernel payload: add a
Wii `platform_ops.kentry` hook that copies the packed FDT into a bounded,
non-overlapping MEM1 buffer, flushes it, and enters the unchanged kernel with
the MEM1 address. Retain the already-rejected short embedded diagnostic
command line. A boot validates the initial-BAT/FDT diagnosis; another failure
requires LED stage markers around finalize, copy, and kernel entry.

## 2026-08-10: Stage MEM1 FDT handoff control

- Isolated branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `343a6c5ff`
- `zImage` SHA-256:
  `2403d04e1a202abb7ce171402fac8cd848cdb91c7c0f6afaf9da7e2bcf07a594`
- Unchanged `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Unchanged, intentionally unloaded `gcn-gx.ko` SHA-256:
  `88474048238caad1e992ca961969f6d42b07fece9fa5438937deac94720dffe1`

Add only a Wii `platform_ops.kentry` hook to the previously rejected short
diagnostic MEM2 image. Validate the packed FDT size, copy it from the MEM2 heap
to `0x01480000`, flush the copied bytes, and enter the unchanged kernel with
that MEM1 address. The 512 KiB target ends immediately before the OHCI pool at
`0x01500000`; the kernel ends at `0x01288708`, and the old GX texture reserve
ends at `0x01380000`. Static disassembly confirms the hook passes `0x01480000`
in `r3` and calls the unchanged kernel entry.

Two successive `ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make zImage
modules -j16` builds pass with a stable image checksum,
`git diff --check` passes, the FDT is 5,505 bytes, and the final wrapper remains
at entry `0x10010000` with memory end `0x106334a0`. Deploy only the image over
the current rootfs diagnostic redirect and disabled GX module. Success requires
fresh diagnostic files and SSH after the full 60-second window.

The first returned-log result is invalid. The card image was exact
`2403d04e...`, but `early-dmesg.txt` identified Linux build
`Sun Aug 9 14:50:09 CDT 2026` with the older long modular-test command line.
The MEM1-FDT kernel's unchanged `vmlinux` identifies build
`Sun Aug 9 15:33:00 CDT 2026`, and its DTS contains the short command line
above. The returned log was the accepted modular control log extended through
uptime 2190 seconds and a second diagnostic-script invocation. It cannot prove
that the MEM1-FDT image entered Linux. Do not record this run as either a pass
or a failure.

## 2026-08-10: Stage self-identifying MEM1 FDT rerun

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `c9959c278`
- Parent implementation commit: `343a6c5ff`
- `zImage` SHA-256:
  `cbf1468bac728de636931e147ca39b97259e7dd75df14bfbcde9da8f9524fafe`
- Unchanged `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Required command-line marker: `wii_test=mem1_fdt_343a6c5ff`

Change only the embedded test identity. Preserve the MEM1 FDT handoff, short
diagnostic command line, legacy framebuffer, rootfs init redirect, and
GX/DRM module blacklist. The image remains a MEM2 wrapper at entry
`0x10010000`; the kernel payload and its 15:33 build identity are unchanged.

The returned-card verdict is mechanical: at least one diagnostic hash must
change from its pre-deployment value, and fresh `early-dmesg.txt` must contain
both the 15:33 kernel identity and
`wii_test=mem1_fdt_343a6c5ff`. Otherwise the test did not prove entry into
this kernel.

## Hardware-cycle harness

Use `tools/wii-hw-cycle.sh` for subsequent card tests:

```sh
tools/wii-hw-cycle.sh stage TEST_ID
# Boot the Wii, wait for the diagnostic cycle, then return the card.
tools/wii-hw-cycle.sh collect TEST_ID
```

`stage` validates a matching `wii_test=TEST_ID` DTS marker, rejects
uncommitted tracked source, builds `zImage` with `make -j16`, archives the
image and build identity, snapshots all four old diagnostic hashes, mounts the
SD partitions by label, copies with normal-user permissions, syncs, and
checksum-verifies the on-card image. `collect` copies only changed logs and
requires the exact test marker in fresh early dmesg. The harness therefore
needs neither root access nor deletion of root-owned logs. Artifacts and
manifests live under
`~/.local/state/wii-linux-ngx/hw-tests/`; `status` reports the current card
and latest run.

Hardware result for the self-identifying MEM1-FDT rerun: reject before
diagnostic PID 1. The harness verified the deployed card image as
`cbf1468bac728de636931e147ca39b97259e7dd75df14bfbcde9da8f9524fafe`
and recorded all four pre-boot hashes. On return, `dmesg.txt`,
`early-dmesg.txt`, `wpa-debug.txt`, and `sshd-debug.txt` were all
byte-identical to those pre-boot files. There is therefore no log from this
kernel and no evidence that it entered `/init-diag.sh`. This result is
distinct from the retracted prior run because the unchanged hashes were
captured automatically before deployment.

## 2026-08-10: Stage kernel drive-slot heartbeat

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `c103b492f`
- Parent marked control: `c9959c278`
- `zImage` SHA-256:
  `28952d83321831e625b8d046271f9380b41d72ca3034371cdcd50027ee3346d5`
- Unchanged `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Required command-line marker: `wii_test=mem1_fdt_led_hb_c995`

Set `linux,default-trigger = "heartbeat"` on the existing
`wii:blue:drive_slot` GPIO LED. `CONFIG_LEDS_GPIO=y` and
`CONFIG_LEDS_TRIGGER_HEARTBEAT=y` are built into this kernel, so the
scheduler-driven double pulse starts when `gpio-leds` probes and does not
depend on PID 1, SSH, graphics, direct MMIO, or SD-card writes. Preserve the
existing `panic-indicator` property.

Interpret the physical result narrowly:

- no heartbeat: failure occurs before the LED class device probes;
- heartbeat: Linux reached device probing and its scheduler remains alive;
- panic override/solid illumination: inspect panic output before inferring a
  normal heartbeat state.

This test changes no graphics code. If there is no heartbeat, instrument
earlier wrapper/kernel boundaries rather than adding userspace diagnostics.

Hardware result: no heartbeat and no PID 1 evidence. The screen remained on
the normal static transition frame and the blue drive-slot LED remained solid.
The harness found all four returned diagnostic hashes unchanged from their
pre-deployment values.

Do not interpret the solid LED as proof that `gpio-leds` probed. Failed images
predating the heartbeat property also left this LED solid, so Mini or another
loader stage can carry that state into Linux. Kernel source confirms that a
panic would transfer a `panic-indicator` LED to the panic trigger and toggle
it from the panic loop; a permanently solid light is not the expected panic
signature. The valid conclusion is only that the heartbeat never became
operational before the failure.

## 2026-08-10: Stage wrapper-clear versus kernel-heartbeat control

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `c672611f5`
- Parent heartbeat control: `c103b492f`
- `zImage` SHA-256:
  `ba4f17848ae1ee94da4ed8d22b9ef28d063e97a94d770a0065892de72af18f75`
- Unchanged `vmlinux` SHA-256:
  `e2a30e3f9ba58d656f910878735434d8e5cb7ac3f6951ecb08b0d579cf0fbe8f`
- Required command-line marker: `wii_test=mem1_fdt_led_zero_c103`

Remove the inherited loader state as a confound. In the wrapper kentry hook,
after the finalized FDT is copied and flushed to MEM1 and immediately before
entering the unchanged Linux payload, clear `GP_SLOTLED` (bit 5) in the PPC
GPIO output register at `0x0d8000c0`. BootMii Mini defines this exact register
and bit as `HW_GPIO1BOUT` and `GP_SLOTLED`; the slot LED is PPC-owned.
Disassembly confirms the wrapper performs the big-endian read/modify/write
between `flush_cache` and the kernel indirect branch.

Keep Linux's heartbeat default trigger. Interpret the physical sequence:

- LED turns off and stays off: wrapper kentry ran, but Linux never activated a
  working `gpio-leds` heartbeat;
- LED turns off, then double-pulses: Linux device probing and timer progress
  are alive;
- LED turns off, then returns solid: Linux activated the LED path, followed by
  timer/interrupt loss before the first or a subsequent heartbeat transition;
- LED never turns off: failure precedes this wrapper kentry hook or the GPIO
  write itself is ineffective.

Hardware result: the LED turned off and remained off, while all four returned
diagnostic hashes remained unchanged. This positively validates the wrapper
kentry hook, MEM1 FDT copy/flush path, direct GPIO write, and indirect branch
into the Linux payload. Linux did not activate its later `gpio-leds`
heartbeat before failure.

The existing Wii platform code also clears this LED in `wii_setup_arch()`.
Because pre-clear failed images retained the loader's solid LED, those images
did not prove that `setup_arch()` ran. The remaining failure window begins at
the kernel entry assembly and ends before Wii setup/device probing. Instrument
`machine_init()` next, before flat-device-tree parsing and early MMU setup.

## 2026-08-10: Stage early machine_init LED marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `eebf06721`
- Parent wrapper-clear control: `c672611f5`
- `zImage` SHA-256:
  `7b399df19b7f1ad189057e5f42a82466ea3f43631caeb03a3895b513f3bce745`
- Instrumented `vmlinux` SHA-256:
  `55d5d2fdd341572a17337e60d977f9736512cb4c5ab2464df83e5e81b1197fa9`
- Required command-line marker: `wii_test=machine_init_led_c672`

Retain the wrapper's immediate pre-entry LED clear. Under `CONFIG_WII`, add
the earliest practical C-level Linux marker in 32-bit `machine_init()`:
immediately after `early_ioremap_init()`, map physical GPIO output register
`0x0d8000c0`, assert slot-LED bit 5, and unmap it. This executes before udbg
initialization, `early_init_devtree()`, and `early_init_mmu()`.

Disassembly verifies the final payload calls `early_ioremap_init`, maps
`0x0d8000c0`, performs the big-endian bit-5 read/modify/write, unmaps it, and
only then calls `udbg_early_init` and `early_init_devtree`.

Interpret the result:

- off: wrapper entered Linux, but execution did not reach this marker or early
  MMIO mapping failed;
- solid on: `machine_init()` and early MMIO ran, but Linux did not reach a
  working device-level heartbeat;
- heartbeat: Linux reached normal `gpio-leds` probing and timer progress.

Hardware result: solid on, with all four diagnostic hashes unchanged. The
wrapper first cleared the loader state, then Linux's marker reasserted the LED.
This positively proves entry into 32-bit `machine_init()`, successful
`early_ioremap_init()`, and successful early MMIO access. The kernel still
did not reach PID 1 or a working device-level heartbeat.

The next unresolved call is `early_init_devtree(__va(dt_ptr))`, followed by
`early_init_mmu()`. Bracket the first call without changing the copied FDT or
memory layout.

## 2026-08-10: Stage early_init_devtree return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `5117e6864`
- Parent machine marker: `eebf06721`
- `zImage` SHA-256:
  `b686d87d48d01f8a529189a2e95440e098951c06e60df7f6483743c1c341b3dd`
- Instrumented `vmlinux` SHA-256:
  `6f310a9dba0f164a5cdf7bcc906be6f862be663467e86a3129279f98ea8485f6`
- Required command-line marker: `wii_test=fdt_return_led_eebf`

Keep the wrapper clear and machine-entry assertion. Generalize the early GPIO
helper to set or clear the slot LED, then clear it immediately after
`early_init_devtree()` returns and before `early_init_mmu()` begins.
Disassembly verifies the exact sequence: LED set, udbg setup and instruction
patching, `early_init_devtree`, LED clear, then `early_init_mmu`.

Interpret the result:

- solid: execution entered `machine_init()` but did not return from
  `early_init_devtree()`;
- off: flat-device-tree initialization returned; failure is in
  `early_init_mmu()` or later;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed from solid on to off and remained off. All
four diagnostic hashes remained unchanged. This proves that
`early_init_devtree()` returned successfully using the FDT copied from MEM2
to MEM1. The failure is in `early_init_mmu()` or later, before normal
`gpio-leds` heartbeat activation and PID 1.

## 2026-08-10: Stage early_init_mmu return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `bf13fa482`
- Parent FDT-return marker: `5117e6864`
- `zImage` SHA-256:
  `7de29795e57cb5f9afac792515256d897e27e70382e3c4159c1be7f662640f19`
- Instrumented `vmlinux` SHA-256:
  `9e618aaff96f4fb9fda61fac1e12125c97c8787ff3a9cb3f8f5bb4db9e9b2cdb`
- Required command-line marker: `wii_test=mmu_return_led_5117`

Keep the wrapper clear, machine-entry assertion, and post-FDT clear. Reassert
the LED immediately after `early_init_mmu()` returns and before
`setup_kdump_trampoline()`. Disassembly verifies that the early-MMU call is
followed by the GPIO map, bit-5 assertion, and unmap.

Interpret the result:

- off: `early_init_devtree()` returned, but execution did not pass the
  post-`early_init_mmu()` marker;
- solid: early MMU initialization returned; the failure is in the transition
  to or execution of `start_kernel()` and later setup;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed back to solid on and remained on. All four
diagnostic hashes remained unchanged, and the required marker did not appear
in a fresh log. This proves that `early_init_mmu()` returned successfully.
Because `CONFIG_CRASH_DUMP` is disabled, the following
`setup_kdump_trampoline()` call compiles to an empty inline function. The next
test therefore clears the LED at the end of `machine_init()` to distinguish a
successful return from failure in the subsequent assembly-to-`start_kernel()`
transition.

## 2026-08-10: Stage machine_init return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `7a4b64124`
- Parent MMU-return marker: `bf13fa482`
- `zImage` SHA-256:
  `939556cad7f8c1a5b83b04c4f756035d7e641512dd2df1a315231df4bad0c065`
- Instrumented `vmlinux` SHA-256:
  `482a7cc9052e0130b865d1c06cac9737c3b687061130d91a24dabdfb9537f4f7`
- Required command-line marker: `wii_test=machine_return_led_bf13`

Keep every preceding marker, then clear the slot LED at the end of
`machine_init()`. `CONFIG_CRASH_DUMP` is disabled, so
`setup_kdump_trampoline()` emits no call or instructions in this build.
Disassembly verifies that the final GPIO clear is immediately followed by the
`machine_init()` epilogue and `blr`.

Interpret the result:

- solid: execution returned from `early_init_mmu()` but did not reach the
  final `machine_init()` marker;
- off: `machine_init()` completed and reached its return path; the failure is
  later in the entry assembly or `start_kernel()` path;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed to off and remained off. All four diagnostic
hashes remained unchanged, and the required marker did not appear in a fresh
log. This proves that `machine_init()` reached its final marker and return
path. The unresolved interval now contains `__save_cpu_setup`, `MMU_init`,
`MMU_init_hw_patch`, the unmapped `load_up_mmu` transition, the final MMU-on
`rfi`, and entry into `start_kernel()`.

## 2026-08-10: Stage start_kernel entry marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `c4fc75a02`
- Parent machine-return marker: `7a4b64124`
- `zImage` SHA-256:
  `1be30d43d2c76454bdea32b485565b781c49262862cdd0927292e3e6ea17b74f`
- Instrumented `vmlinux` SHA-256:
  `14940677a82f37232ea131b9698bf9382e9dfbf6347ede064b8d921b87fc1c5c`
- Required command-line marker: `wii_test=start_kernel_led_7a4b`

Retain the wrapper and `machine_init()` sequence, whose final state is off.
Under `CONFIG_WII`, assert the slot LED as the first C-level operation in
`start_kernel()` using the already initialized PowerPC early-I/O mapping.
Disassembly verifies that the GPIO map, bit-5 assertion, and unmap precede
`set_task_stack_end_magic()`, CPU initialization, banner output, and
`setup_arch()`.

Interpret the result:

- off: `machine_init()` returned, but execution did not reach the first
  `start_kernel()` marker; the failure is in the intervening assembly/MMU
  transition;
- solid: the final MMU-on `rfi` reached virtual `start_kernel()` and early
  MMIO worked, but normal device-level heartbeat and PID 1 were not reached;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED remained off. All four diagnostic hashes remained
unchanged, and the required marker did not appear in a fresh log. The kernel
therefore did not reach the first C-level operation in `start_kernel()`.
The failure lies after the final marker in `machine_init()` and before virtual
`start_kernel()` entry, across the intervening Book3S assembly and MMU setup.

## 2026-08-10: Stage post-machine_init assembly marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `1674539e2`
- Parent start-kernel marker: `c4fc75a02`
- `zImage` SHA-256:
  `0c3ad561d6880d88a302282ac8a8c572b4c134baeb6b53f4550f70a205cabf43`
- Instrumented `vmlinux` SHA-256:
  `087d5c3ce8940662e22b47472ef70a1933265e3e5eeab3b77fb446982dfa36f5`
- Required command-line marker: `wii_test=machine_bl_return_led_c4fc`

Retain the final `machine_init()` clear. Expose its already validated early
GPIO helper and call it from `start_here` immediately after `bl machine_init`,
before `bl __save_cpu_setup`. Disassembly verifies this exact ordering and
then shows `MMU_init`, `MMU_init_hw_patch`, the MMU-off `rfi`, `load_up_mmu`,
and the final MMU-on `rfi` to `start_kernel()`.

Interpret the result:

- off: execution reached the final marker inside `machine_init()` but did not
  successfully return to and run the next Book3S assembly marker;
- solid: control returned to `start_here`; the failure is in
  `__save_cpu_setup` or the subsequent MMU transition;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed to solid on and remained on. All four
diagnostic hashes remained unchanged, and the required marker did not appear
in a fresh log. This proves that `machine_init()` returned to `start_here` and
the immediately following Book3S assembly marker executed. The failure is in
`__save_cpu_setup` or the subsequent MMU initialization and transition.

## 2026-08-10: Stage __save_cpu_setup return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `e3739443d`
- Parent assembly-return marker: `1674539e2`
- `zImage` SHA-256:
  `dea360f87324c2e31cfd92f185a7c47aa7862220c6f4f40362cfc2335aac088f`
- Instrumented `vmlinux` SHA-256:
  `91ca273d1f7a52081dcf574e5be3a1039654eff407862503684d9ecaad8e5a91`
- Required command-line marker: `wii_test=save_cpu_return_led_1674`

Keep the post-`machine_init()` assertion, then clear the slot LED immediately
after `bl __save_cpu_setup` and before `bl MMU_init`. Disassembly verifies the
exact on-call-off-call sequence.

Interpret the result:

- solid: execution returned from `machine_init()` but did not pass the marker
  after `__save_cpu_setup`;
- off: `__save_cpu_setup` returned successfully; the failure is in `MMU_init`
  or the subsequent MMU transition;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed to off and remained off. All four diagnostic
hashes remained unchanged, and the required marker did not appear in a fresh
log. This proves that `__save_cpu_setup` returned successfully. The unresolved
path now begins at `MMU_init()`.

## 2026-08-10: Stage MMU_init return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `69ae02d71`
- Parent CPU-setup marker: `e3739443d`
- `zImage` SHA-256:
  `74f7a0ca7406f4fe4f9d99452f959383759b623ca5c975d9388e8655cbfac0b5`
- Instrumented `vmlinux` SHA-256:
  `c5ec64efcc513c6c5b6cf14f5c582518e0edb59289d190c432d27ba4b87d84d5`
- Required command-line marker: `wii_test=mmu_init_return_led_e373`

Keep the post-`__save_cpu_setup` clear, then assert the slot LED immediately
after `bl MMU_init` and before `bl MMU_init_hw_patch`. Disassembly verifies
the exact off-call-on-call sequence.

Interpret the result:

- off: `__save_cpu_setup` returned but execution did not pass the marker after
  `MMU_init`;
- solid: `MMU_init` returned successfully; the failure is in
  `MMU_init_hw_patch` or the subsequent MMU transition;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED remained off. All four diagnostic hashes remained
unchanged, and the required marker did not appear in a fresh log. This proves
that `MMU_init()` did not return. The failure is now localized inside that
function, which performs memory accounting, hash/MMU hardware setup,
MEM1/MEM2 linear mapping, and final MMU feature setup.

## 2026-08-10: Stage MMU_init_hw return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `4727b64f3`
- Parent MMU-return marker: `69ae02d71`
- `zImage` SHA-256:
  `8ef44f4dc1581859e01d3c6c182f83d82fa676b76921fef69e5b017334c5632b`
- Instrumented `vmlinux` SHA-256:
  `892d657e56d6650beddc127f8e498fc6596952d119c1be3c1efd146cf8c77b24`
- Required command-line marker: `wii_test=mmu_hw_return_led_69ae`

Assert the slot LED inside `MMU_init()` immediately after `MMU_init_hw()` and
before `mapin_ram()`. Disassembly verifies the call-marker-call sequence.
This brackets early memory accounting and hash-table/MMU hardware setup from
the later MEM1/MEM2 linear mapping.

Interpret the result:

- off: failure occurred before the post-`MMU_init_hw` marker, in early memory
  accounting or `MMU_init_hw()` itself;
- solid: `MMU_init_hw()` returned; failure is in `mapin_ram()` or later;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED remained off. All four diagnostic hashes remained
unchanged, and the required marker did not appear in a fresh log. Execution
therefore did not pass the marker after `MMU_init_hw()`. The remaining window
is early `MMU_init()` memory accounting or `MMU_init_hw()` itself.

## 2026-08-10: Stage MMU_init_hw entry marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `14e4b06d2`
- Parent MMU-hardware-return marker: `4727b64f3`
- `zImage` SHA-256:
  `f52e772663e387370843dacc9c74337852ea8c8ad95fec79bf08d29160f7ac5b`
- Instrumented `vmlinux` SHA-256:
  `5aa083f52cbae5a4e2193754eddbd4e6b186a4506df9a89576d24dcb5c11f73f`
- Required command-line marker: `wii_test=mmu_hw_entry_led_4727`

Assert the slot LED after `MMU_init()` memory accounting and its optional
progress callback, immediately before `MMU_init_hw()`. Keep the existing
post-call assertion. Disassembly verifies the pre-call marker placement.

Interpret the result:

- off: execution failed during early `MMU_init()` memory accounting or its
  progress callback;
- solid: execution entered `MMU_init_hw()` but did not return from it;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED changed to solid on and remained on. All four
diagnostic hashes remained unchanged, and the required marker did not appear
in a fresh log. This proves early `MMU_init()` memory accounting completed and
execution entered `MMU_init_hw()`, but that function did not return.

## 2026-08-10: Stage hash-table allocation return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `b8ac835a0`
- Parent MMU-hardware-entry marker: `14e4b06d2`
- `zImage` SHA-256:
  `27b608123e835d9435adfb86bcb875de171bb6e24c31f88355ef650ca0080d85`
- Instrumented `vmlinux` SHA-256:
  `f9b10678e02a98d0ef99efa2fe442da076e7992640a70eb68f1bb9a826ca4704`
- Required command-line marker: `wii_test=hash_alloc_return_led_14e4`

Enter `MMU_init_hw()` with the LED solid, then clear it immediately after
`memblock_alloc_or_panic()` returns from allocating and zeroing the aligned
hash table. Keep the outer post-`MMU_init_hw` state clear as well, preventing
a later marker from masking the result. Disassembly verifies that the internal
clear precedes `_SDR1` calculation and `_printk`.

Interpret the result:

- solid: `MMU_init_hw()` entered but hash-table allocation or zeroing did not
  return;
- off: hash-table allocation returned; failure is in later `MMU_init_hw()`
  state setup or beyond;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED remained solid on. All four diagnostic hashes
remained unchanged, and the required marker did not appear in a fresh log.
The combined `memblock_alloc_or_panic()` operation therefore did not return.
That helper combines physical-range selection with zeroing through the
returned virtual address, so those operations must be separated next.

## 2026-08-11: Stage raw hash-allocation return marker

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `a80c0e286`
- Parent combined-allocation marker: `b8ac835a0`
- `zImage` SHA-256:
  `7471b68e41c7bf634c79f597b9f62b40852ac1fc99c0d589395f0230ae63cab2`
- Instrumented `vmlinux` SHA-256:
  `378cf3e32d5a4d88fa06f54cb3b851b4b568c504670163fe1cca483e0de1b216`
- Required command-line marker: `wii_test=hash_raw_return_led_b8ac`

Split `memblock_alloc_or_panic()` into the same raw allocation policy, an
equivalent explicit panic if it returns NULL, and explicit `memset` zeroing.
Clear the slot LED after the non-NULL check and immediately before `memset`.
Disassembly verifies the NULL check, clear, and zeroing call in exact order.

Interpret the result:

- solid: raw physical allocation did not return successfully, or returned
  NULL and entered panic before the clear;
- off: raw allocation returned a virtual address; failure is in zeroing that
  address or later `MMU_init_hw()` work;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: the LED remained solid on. All four diagnostic hashes
remained unchanged, and the required marker did not appear in a fresh log.
The raw allocator did not reach the non-NULL return marker. Static layout
analysis explains the failure: the linked kernel ends at physical
`0x0127d000`, and the remaining 24 MiB MEM1 space is fragmented by the GX,
XFB, FIFO, OHCI, and copied-FDT reservations. No free 1 MiB-aligned 1 MiB
extent remains below the early allocation limit.

The 1 MiB request is itself incorrect for the Wii. Generic `MMU_init()` sets
`total_memory` to `memblock_end_of_DRAM() - memstart_addr`, which counts the
physical hole between MEM1 and MEM2 and reports a 320 MiB span. The tested
mini configuration exposes 80 MiB to Linux: 24 MiB of MEM1 plus 56 MiB of
MEM2 after mini's reservation. The Book3S hash sizing formula rounds the
fictitious 320 MiB span to a 1 MiB hash, while 80 MiB requires a 256 KiB hash.

## 2026-08-11: Stage installed-RAM hash sizing fix

- Test branch: `test/wii-mem2-fdt-in-mem1`
- Test commit: `b38e89a03`
- Parent raw-allocation marker: `a80c0e286`
- `zImage` SHA-256:
  `dec02e32ffcfc157049bc108207ec80afa83599a6238721abab4512ff601f2ef`
- Instrumented `vmlinux` SHA-256:
  `1f661f0af353a66b8cf50487e4fa91b07b109e7ab14b05277d4841db4c8a6131`
- Required command-line marker: `wii_test=wii_hash_actual_ram_a80c`

On Wii, retain `total_lowmem` as the physical span consumed by the modern
per-range mapping code, but derive `total_memory` from
`memblock_phys_mem_size()`. This reports the actual memory ranges exposed by
mini rather than including their hole. Restore the standard
`memblock_alloc_or_panic()` call after the diagnostic split.

Remove the pre-`MMU_init_hw()` assertion and assert the slot LED only after
that function returns. Disassembly verifies the actual-memory query, standard
allocator, and post-call-only assertion.

Interpret the result:

- off: correcting hash size was insufficient; `MMU_init_hw()` still did not
  return;
- solid: the corrected 256 KiB hash allocation allowed `MMU_init_hw()` to
  return; failure, if any, is in `mapin_ram()` or later;
- heartbeat: Linux reached normal device probing and timer progress.

Hardware result: successful boot through PID 1 and the remote test loop. SSH
verified the running kernel as `6.18.40-wii+` with the exact command-line
marker `wii_test=wii_hash_actual_ram_a80c`. The root filesystem mounted,
`/init-diag.sh` ran, SD and SDIO initialized, b43 associated, gateway and host
pings passed, and OpenSSH became reachable at `10.3.10.12`.

The running kernel reports `Memory: 55348K/81920K available`, confirming that
the fictitious 320 MiB span is no longer used as installed-memory capacity.
The complete live `dmesg` capture contains 277 lines and has SHA-256
`57e87fc421ebbb8c9d0262791342590f78379ef6f19ad4d73be96525c015b17e`.
This is a validated positive control for both the MEM1 FDT relocation and the
installed-RAM Book3S hash-sizing fix.

A separate nonfatal warning remains during OHCI coherent-DMA initialization:
an alignment exception occurs in `memset()` from
`dma_alloc_from_dev_coherent()`, after which both OHCI controllers continue
initializing and USB functions. Track this independently; it did not block
boot, networking, or SSH and is not part of the MMU hash failure.

## 2026-08-11: Stage clean MEM2 boot integration candidate

- Integration branch: `fix/wii-6.18-mem2-boot`
- Integration commit: `e0d61d4c4`
- Validated diagnostic commit: `b38e89a03`
- Clean `zImage` SHA-256:
  `cac1dfecea0e9aeaf10d3692a704944c6476a272c27cf219f2cf8b3ce2efe767`
- Clean `vmlinux` SHA-256:
  `f870a8fb03e09847a11ab23b658b21a4392903f04acadc8ec8d676d78e404b08`
- Preserved image:
  `/tmp/zImage-clean-mem2-boot-cac1dfecea0e.ngx`

Reduce the validated diagnostic branch to its two production changes. Copy
and flush the finalized FDT into the 512 KiB MEM1 handoff buffer at
`0x01480000`, then pass that address in `r3` when entering the kernel. On Wii,
derive `total_memory` from `memblock_phys_mem_size()` while retaining the
existing physical span in `total_lowmem` for sparse-bank mapping.

The clean source contains no slot-LED instrumentation, `wii_test` boot marker,
temporary reservation change, or diagnostic initramfs. Its embedded bootargs
are the normal feature-branch bootargs. Source audit and `git diff --check`
pass, and a clean `-j16` cross-build produced the checksums above.

Hardware result: validated. Before reboot, the staged file and active boot
filename both matched the recorded clean SHA-256. The previous diagnostic
image was retained as `zImage.ngx.diag-b38e89a03` with its independently
verified checksum.

The Wii booted `6.18.40-wii+ #1` with the normal production command line and
no `wii_test` marker. SSH verification found `Memory: 50624K/81920K
available`, PID 1 running, the ext3 root mounted, both SD controllers
initialized, the root card enumerated, b43 firmware loaded, `wlan0`
associated, and OpenSSH reachable at `10.3.10.12`. Two-packet tests to both
the gateway and host completed without loss.

The complete clean live `dmesg` capture contains 304 lines and is preserved at
`/tmp/wii-dmesg-clean-mem2-e0d61d4c4.txt` with SHA-256
`b21b51c572f67485537b9202b97f552e050f66b0647dbcdcdc0fca4e25fddd7a`.
Its only severe-pattern match is the same nonfatal OHCI coherent-DMA alignment
warning already observed in the diagnostic boot. The clean integration test
therefore validates both production fixes without relying on any diagnostic
kernel instrumentation.

## 2026-08-11: Retry native DRM boot ownership after MEM2 fixes

- Test branch: `test/wii-native-drm-boot-after-mem2-fix`
- Base integration commit: `b85c5cd90`
- `zImage` and `dtbImage.wii` SHA-256:
  `1fe4f3d4f9532f9819d99f4848371169ca289dc2b392aa2881f7d950b0ebbd19`
- `vmlinux` SHA-256:
  `a226acdd9ae7c9e7ab080d6168d3538656ed6c872c7f824f4bb864b258ed2608`
- `vmlinux.unstripped` SHA-256:
  `cef9bf3cf7f06b74848fb85ac8d772662290fc0bea0d8c7e5ce7f2c4e3993e27`
- Wii DTB SHA-256:
  `b7c266c28f6b64da9923d6225506b8c65f55a188d029aea8fbabac25b10cc4b9`
- Validated rollback `zImage` SHA-256:
  `cac1dfecea0e9aeaf10d3692a704944c6476a272c27cf219f2cf8b3ce2efe767`

Retry the native DRM boot-owner milestone without changing graphics driver
source. Build DRM core, KMS helpers, shmem, client setup, fbdev emulation, and
GCN VI into the kernel. Disable legacy `gcn-vifb` and its GX accelerator, and
remove both the legacy video argument and `gcn_drm` module blacklist from the
embedded command line.

This is the first native-owner image containing both production boot fixes:
the finalized FDT is copied into MEM1 before kernel entry, and Book3S hash
sizing uses installed memory rather than the MEM1-to-MEM2 physical span. The
previous native-owner and built-in-DRM tests predated those fixes and therefore
did not reach a valid graphics verdict.

The complete `-j16 zImage modules` build passes. Static audit confirms
`gcn_drm_probe`, `drm_client_setup`, and the shmem fbdev probe are linked while
`gcn_vifb_probe` is absent. The kernel physical end is `0x01288990`, below the
relocated GX texture boundary at `0x01300000`. The wrapper starts at
`0x10010000` in MEM2. The final DT retains the texture, FIFO, XFB, OHCI, and
AVE-phandle state and embeds only the production DRM boot command line.

Cold-boot acceptance procedure:

1. Deploy and independently verify the exact image checksum. Retain the
   currently validated clean legacy image as rollback and leave the FAT boot
   partition unmounted before reboot.
2. Require the display to transition to an updating DRM fbcon with a blinking
   cursor. Reject a permanently static frame, recurring blur, repeated columns,
   incorrect colors, or loss of keyboard response.
3. Over SSH, require built-in `gcn-vi` to own `c002000.video`, `card0` to
   exist, `/proc/fb` to identify `gcn-vidrmfb`, AVE register `0x62` to read
   two, and no legacy `gcn-vifb`, `gcn_gx`, or userspace mirror process.
4. Generate new tty output after SSH arrives and require it to appear on the
   display, proving native fbdev dirty updates still reach the XFB. Audit the
   full kernel log for faults separately from the known nonfatal OHCI
   coherent-DMA alignment warning.
5. Reboot once without redeploying and repeat the ownership, update, color,
   input, networking, and fault gates before accepting native DRM as the boot
   display owner.

First cold-boot result: kernel-side ownership and visual output pass. The Wii
booted `6.18.40-wii+ #1` with the exact production command line and returned
on SSH. `/proc/fb` identifies `gcn-vidrmfb`; `card0` and the connected
640x480 composite connector exist; and `c002000.video` is bound to built-in
`gcn-vi`. No GCN/DRM module or userspace framebuffer mirror is present.

Probe programmed NTSC 480i, wrote and verified AVE `0x62=0x02`, installed the
VI IRQ, registered DRM, and switched fbcon at 1.44 seconds. The live VI IRQ
count continued advancing. Two separately timed `/dev/tty0` writes produced a
new native-owner heading, labeled color samples, and a second-frame dirty
update marker. The user classified the colors as correct.

The complete first-boot dmesg contains 307 lines and is preserved at
`/tmp/wii-dmesg-native-drm-boot1-15c880333.txt` with SHA-256
`567cf0d06d67767d713ab79a4363c5e83c17d846996faba6d59abd56cb299cb8`.
Its only severe-pattern match is the existing recoverable OHCI coherent-DMA
alignment warning. No panic, oops, machine check, graphics timeout, or DRM
fault appears. Keyboard input is not yet explicitly classified. Complete the
required no-redeployment reboot before accepting boot ownership.

Second-boot result: accepted without redeployment. The same kernel build and
production command line again returned on SSH with `gcn-vidrmfb`, `card0`, the
connected 640x480 composite connector, and built-in `gcn-vi` ownership. The
driver again programmed NTSC 480i, enabled and verified AVE `0x62=0x02`, and
registered fbcon. No GCN/DRM module or userspace mirror was present.

The VI IRQ count advanced from 2255 to 2316 during a two-second sample. A new
second-boot heading, color row, and separately timed dirty-update marker all
appeared; the user classified the complete display as looking great. The
physical USB keyboard was not connected to the Wii, so input is explicitly
unavailable rather than passed or failed in this run.

The complete second-boot dmesg also contains 307 lines and is preserved at
`/tmp/wii-dmesg-native-drm-boot2-15c880333.txt` with SHA-256
`d168339fba4c17baf97d1a6c7b6ac671e3785eb5c9f1ffb13fd43ad74025a2fd`.
Its fault audit again finds only the known recoverable OHCI alignment warning.

Accept native GCN DRM as the Wii boot display owner. Two consecutive boots of
the checksum-pinned image produced deterministic driver ownership, AVE state,
live framebuffer updates, correct visuals, advancing vblank interrupts,
working networking/SSH, and no graphics fault. Keep the prior clean legacy
image on the FAT partition as an independently checksum-verified rollback.
Recheck keyboard input when the keyboard is physically available; its absence
does not qualify the accepted graphics result.

## 2026-08-11: Stage modular GX acceleration behind native DRM

- Test branch: `test/wii-drm-gx-rgb565`
- Candidate commit: `654cb3718`
- `zImage` and `dtbImage.wii` SHA-256:
  `d2b351254cbb013fa6c12d7b90e8945974bdfcd6863194a21cd21237b2a823e3`
- `vmlinux` SHA-256:
  `bfba1fe0332fe7a539293207ca28fde58ab64d3ec80746397a6134f13cc22403`
- `vmlinux.unstripped` SHA-256:
  `be1e49b653ab830d329384312d6e55f7b933d1ae9968b84652de8d82fa15ba4e`
- `gcn-gx.ko` SHA-256:
  `56d14c28df67393dde8ad303460ddae29d847bbd7584f624e7b8ee9cf83ae421`

Connect the proven GX renderer to native DRM without transferring VI
ownership. DRM continues to own the Video Interface interrupt, vblank, and
double-buffered page flips. The optional `gcn-gx` module registers a
mutex-protected scanout provider, tiles the preferred RGB565 shadow buffer,
renders it through GX, waits for the PE finish interrupt, and writes the
inactive physical XFB. Only after successful completion does the existing DRM
path mark that page pending for the next vblank.

The accepted CPU RGB-to-YUYV converter remains the fallback when the module is
absent, when registration or rendering fails, and for XRGB8888. Unregistering
the provider waits for an in-flight call before module text can disappear, so
module removal must restore CPU conversion without unbinding DRM or changing
the live console owner. Source pitch is now explicit in GX texture tiling.
Read-only `frames` and `pe_finishes` module parameters provide independent
repeat-update observability.

The complete `ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make zImage
modules -j16` build, a `W=1` module build, strict diff checkpatch, `git diff
--check`, and module-symbol audit pass. `gcn-gx.ko` references the two intended
DRM provider symbols and no legacy `gcnfb` symbol.

Hardware acceptance procedure:

1. Deploy and independently verify the exact kernel and module checksums.
   Retain the accepted native-DRM image as rollback and prevent any stale GX
   module from loading automatically.
2. Boot without GX loaded. Require the accepted native DRM console, correct
   colors, live dirty updates, advancing VI IRQs, SSH, and a clean fault audit.
3. Load the pinned `gcn-gx.ko`, generate several distinct tty updates, and
   require `GX scanout accelerator active`, increasing `frames` and
   `pe_finishes`, continued VI IRQ progress, and no GX timeout or fallback
   error. Visually require correct colors and complete, stable updates.
4. Remove `gcn_gx`, generate another distinct tty update, and require the same
   DRM console to remain live through CPU conversion with no blank, ownership
   change, or kernel fault.
5. Reload the same module and repeat the update and counter checks. A second
   boot without redeployment must repeat the baseline, load, unload, and
   reload lifecycle before this integration is accepted.

First-boot result: kernel ownership, synchronization, and lifecycle pass, but
visual quality rejects candidate `654cb3718`. The new kernel booted as build
`#2` with native `gcn-vidrmfb`, built-in `gcn-vi`, no loaded GX module, and no
legacy framebuffer. Two CPU-rendered tty updates completed while the VI IRQ
advanced from 3239 to 3301.

The checksum-pinned module loaded and registered without changing DRM
ownership. Across three labeled updates, `frames` advanced from 2 to 37,
`pe_finishes` from 3 to 74, and the VI IRQ from 4039 to 4223. No PE timeout,
fallback error, FIFO stall, or kernel fault appeared. The user reported that
the text was mostly correct but had visible blur around character edges.

Removing `gcn_gx` cleanly unregistered the provider. Two same-boot CPU control
updates then rendered while the VI IRQ advanced from 7420 to 7544, and the
user classified the CPU text as clearer than GX. This controlled comparison
proves a GX-path quality regression rather than general VI/display blur.

Do not change the nearest sampler or the pixel-exact `-2/8` texel bias first.
The direct DRM entry omitted `gx_setup_display_copy_state()`, whereas the
validated legacy diagnostic programmed that EFB-to-XFB copy/filter state
before the first live frame. Test only that initialization gap next, using the
same kernel and same-boot CPU control.

### Display-copy initialization retry

- Candidate commit: `d77c4f7f1`
- Unchanged `zImage` SHA-256:
  `d2b351254cbb013fa6c12d7b90e8945974bdfcd6863194a21cd21237b2a823e3`
- Candidate `gcn-gx.ko` SHA-256:
  `f6cb3f01e089eab52dc58250ab8053b1378444e9c1ce5c9a5770156f622846c7`

On the first generated frame only, append
`gx_setup_display_copy_state()` immediately after the libogc initialization
preamble. This restores the validated copy-filter, Y-scale, EFB format, field,
and destination-alpha state before the DRM provider's first EFB-to-XFB copy.
No texture sampler, coordinate, primitive, synchronization, DRM, or VI state
changed.

Load this module from the existing CPU-control state and render the same-size
GX labels. Require the frame and PE-finish counters plus VI IRQ to advance
without timeout, then compare character edges directly with the visible CPU
control labels. A clear match supports the missing-state diagnosis; unchanged
blur rules it out and requires an XFB-level comparison before changing texel
coordinates.

First-boot result: passed. The checksum-pinned retry rendered three comparison
labels while reaching 35 frames and 70 PE finishes. The VI IRQ continued to
advance, no GX timeout/fallback or kernel fault appeared, and the user judged
the GX text identical to the CPU control text and therefore visually perfect.
This confirms the missing display-copy initialization caused the first
candidate's edge blur.

The corrected module then ran continuously to 446 frames and 892 PE finishes.
It unregistered cleanly, the same DRM console rendered a labeled CPU fallback
update, and the VI IRQ remained live. Reloading the same module rendered two
new GX labels, reached 24 frames and 48 PE finishes from fresh module state,
and again looked perfect to the user. DRM retained `gcn-vidrmfb` ownership
throughout.

The complete 321-line first-boot log is preserved at
`/tmp/wii-dmesg-drm-gx-copy-state-boot1-d77c4f7f1.txt` with SHA-256
`c09b3e3968f980e19dd410eb00eb1d2a11de487776db94717c1a7659911cb091`.
Its fault audit contains only the known recoverable OHCI alignment warning,
missing optional regulatory database, and unrelated boot-init fallback. No
graphics timeout, FIFO stall, oops, panic, or machine check occurred.

Complete one reboot without redeploying. Restage only the exact module because
`/tmp` is cleared at boot, then repeat CPU baseline, load, labeled GX updates,
counter/IRQ/fault checks, unload fallback, and reload visual validation.

Second-boot result: passed; accept the modular RGB565 GX scanout backend. The
unchanged kernel again booted as build `#2` with native `gcn-vidrmfb` and no GX
module loaded. Two CPU baseline updates completed while the VI IRQ advanced
from 2981 to 3043. The restaged module independently matched
`f6cb3f01...846c7` before loading.

Three initial GX updates reached 34 frames and 68 PE finishes while the VI IRQ
advanced to 3250. The user confirmed those labels were clear and identical to
the CPU baseline. Continuous rendering then reached 426 frames and 852 PE
finishes. Module removal restored a clear CPU fallback update without changing
DRM ownership; reloading reached 22 frames and at least 44 PE finishes while
the VI IRQ advanced from 5712 to 5837. The user confirmed the final reload
labels were also clear.

The complete 313-line second-boot log is preserved at
`/tmp/wii-dmesg-drm-gx-copy-state-boot2-d77c4f7f1.txt` with SHA-256
`41d0fe595018c9b1d0811fd5a3e71afd663b4f925e4e40208ca00d3f08c76c57`.
Its audit again contains only the known recoverable OHCI alignment warning,
missing optional regulatory database, and unrelated init fallback. No GX,
DRM, FIFO, PE, VI, oops, panic, or machine-check fault appears.

Accept commit `d77c4f7f1` on top of the provider implementation in
`654cb3718`. Native DRM remains the sole display owner; the module accelerates
RGB565 dirty updates into inactive XFBs and can be loaded or unloaded without
blanking or rebinding. XRGB8888 intentionally remains on CPU conversion and is
the next format-integration milestone, not a gap in this accepted RGB565
result.

## 2026-08-11: Stage modular GX XRGB8888 scanout

- Test branch: `test/wii-drm-gx-xrgb8888`
- Candidate commit: `322475ecd`
- `zImage` and `dtbImage.wii` SHA-256:
  `3718f607e3d6e2aaae726fb7eb110b130f4e48ae7fd5022a720f0c2ca0b1d5cc`
- `vmlinux` SHA-256:
  `a2f93e82b9f41d17513cd0fd090cc389038ab97db6a2f558382fc100ebff6bec`
- `vmlinux.unstripped` SHA-256:
  `2a6834b2039329b1bfbfff3bbd3fb926f1f6b49cc92b9a7831e1ec72522d9394`
- `gcn-gx.ko` SHA-256:
  `d05708de7d5c1a610564d3203bab249141818a20357484b025fd26de7290d21f`

Extend the optional DRM scanout-provider interface with an XRGB8888 callback.
The GX module routes it through the existing packed-XRGB8888-to-tiled-RGB565
conversion, generated texture renderer, synchronous PE-finish wait, and
inactive-XFB display copy. DRM retains sole ownership of VI programming,
vblank, and page flips. The accepted CPU XRGB8888-to-YUYV converter remains
the fallback when the module is absent or a GX callback returns an error.

The provider callbacks remain optional per format, while registration requires
at least one usable callback. DRM logs the first successful GX frame for each
format independently. The module exposes read-only `xrgb8888_frames` alongside
the existing total `frames` and `pe_finishes` counters, providing an immediate
positive control without relying on visual classification alone.

The complete `ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make zImage
modules -j16` build passes. Separate `W=1` builds of `gcn-gx.ko` and the
built-in DRM object pass. Strict diff checkpatch, `git diff --check`, module
parameter inspection, and undefined-symbol audit also pass. The module retains
only the intended DRM provider register/unregister dependencies and no legacy
`gcnfb` dependency.

Hardware acceptance procedure:

1. Deploy and independently verify the exact kernel and module checksums. Keep
   the accepted modular-RGB565 image as rollback and prevent automatic module
   loading.
2. Boot without GX loaded. Run the deterministic `wii-drm-test` XRGB8888
   fixture and require correct quadrants, grid, checkerboard, moving marker,
   dirty updates, VI IRQ progress, and no fault using CPU conversion.
3. Load the pinned module and rerun the exact XRGB8888 fixture. Require
   `GX XRGB8888 scanout accelerator active`, increasing `frames`,
   `xrgb8888_frames`, and `pe_finishes`, continued VI IRQ progress, and no
   timeout or fallback error. Visually compare the complete frame directly
   with the CPU baseline.
4. Unload the module and rerun the fixture. Require immediate, clean CPU
   fallback without blanking, stale output, ownership change, or kernel fault.
   Reload the same module and repeat the counter and visual checks.
5. Reboot once without redeploying and repeat the baseline/load/unload/reload
   lifecycle. Do not accept the XRGB8888 path from a single boot.

First-boot result: passed. The checksum-pinned kernel booted as build `#4`
with native `gcn-vidrmfb`, no loaded GX module, and advancing VI interrupts.
The unchanged deterministic test client independently matched its established
SHA-256 `d2aa7acc2fc097fb695d06b318543725c01ed39c5c6b53745a07849229430b50`.
Its CPU baseline completed 80 XRGB8888 page flips while the VI IRQ advanced
from 224618 to 225089. The user classified the complete pattern as correct.

The pinned GX module loaded from zero counters and registered without changing
DRM ownership. The exact XRGB8888 fixture then completed 120 flips.
`xrgb8888_frames` advanced from 0 to 121, total `frames` reached 125,
`pe_finishes` reached 250, and the VI IRQ advanced from 225089 to 225727. The
kernel logged both the expected RGB565 console activation and the first
`GX XRGB8888 scanout accelerator active` positive control. The user judged the
GX pattern correct against the same-boot CPU baseline.

Module removal cleanly unregistered the provider. The identical CPU fallback
fixture completed 60 flips while the VI IRQ advanced from 225727 to 226123.
Reloading the same module reset its counters and completed another 60-flip
XRGB8888 run, reaching 61 XRGB8888 frames, 65 total frames, and 130 PE
finishes while the VI IRQ advanced from 226123 to 226516. The user classified
the fallback and final reload output as correct.

The complete 377-line first-boot log is preserved at
`/tmp/wii-dmesg-drm-gx-xrgb8888-boot1-322475ecd.txt` with SHA-256
`ce4ec29c101f3a4c0aaa182626cf97a1299ef764f099306039ac2e04ad770005`.
Its fault audit contains only the known recoverable OHCI alignment warning,
missing optional regulatory database, and unrelated init fallback. No GX/DRM
timeout, FIFO stall, oops, panic, or machine check appears. Complete the
required second boot without redeploying before accepting XRGB8888 support.

Second-boot result: passed; accept modular GX XRGB8888 scanout. The Wii
rebooted without redeployment under a new boot ID and the same build `#4`.
Both `/tmp` artifacts were restaged and independently matched their pinned
checksums. With no GX module loaded, the XRGB8888 CPU baseline completed 60
flips while the VI IRQ advanced from 3235 to 3627.

The module loaded from fresh zero counters and the GX fixture completed 80
XRGB8888 flips. `xrgb8888_frames` reached 81, total `frames` reached 85,
`pe_finishes` reached 170, and the VI IRQ advanced from 3627 to 4101. The
expected XRGB8888 activation message appeared and the user confirmed that the
complete output matched the CPU baseline.

Unloading the module restored CPU conversion without rebinding DRM. The
fallback fixture completed 40 flips while the VI IRQ advanced from 4101 to
4422, with correct output confirmed by the user. Reloading the exact module
then completed a final 40-flip GX run, reaching 41 XRGB8888 frames, 45 total
frames, and 90 PE finishes while the VI IRQ advanced from 4423 to 4739. The
user again classified the output as correct.

The complete 321-line second-boot log is preserved at
`/tmp/wii-dmesg-drm-gx-xrgb8888-boot2-322475ecd.txt` with SHA-256
`6ff8684649fc403c80de86bb6c15813f410b260c382c612e082c482b45a77743`.
Its fault audit contains only the known recoverable OHCI alignment warning,
missing optional regulatory database, and unrelated init fallback. No GX,
DRM, FIFO, PE, VI, oops, panic, or machine-check fault appears.

Accept candidate `322475ecd`. The modular provider now accelerates both DRM
RGB565 and XRGB8888 shadow buffers through the established tiled-RGB565 GX
renderer while native DRM remains the sole display owner. Both formats retain
CPU conversion as a safe absent-provider and error fallback, and the module
can be unloaded and reloaded without interrupting the active DRM console.

### Post-acceptance mixed-format and live-lifecycle stress

Run two additional checksum-pinned mixed-format sequences on the accepted
second boot. Each sequence submitted 600 XRGB8888 page flips immediately
followed by 600 RGB565 page flips at a 16 ms client delay. The first sequence
advanced total GX frames from 4403 to 5614, XRGB8888 frames from 41 to 642,
PE finishes from 8806 to 11228, and VI IRQs from 31368 to 32674. The repeat
advanced total frames from 10776 to 11987, XRGB8888 frames from 642 to 1243,
PE finishes from 21552 to 23974, and VI IRQs from 64226 to 65533. Both clients
reported all 1200 requested flips. The user observed the repeat directly and
classified both formats and the transition as visually crisp and sharp.

Exercise provider synchronization under an active client rather than between
fixtures. Start one 1200-flip XRGB8888 stream, remove `gcn_gx` after four
seconds, leave CPU fallback active for four seconds, then reload the exact
module without stopping page flips. Unregistration and registration both
completed, the client reported all 1200 flips, and VI IRQs advanced from
134958 to 136383. The fresh module completed 1030 XRGB8888 frames and 2066 PE
finishes after reload. The user saw no blank, frozen frame, corruption, or
transition artifact and classified the complete test as perfect.

The authoritative 369-line post-stress kernel log was snapshotted on the Wii,
retrieved independently, and preserved at
`/tmp/wii-dmesg-drm-gx-postaccept-stress-322475ecd.txt` with SHA-256
`f2959d24ef2e40958adccb88afb4a9a9f712a28506a7ea628370d5d98a6fdf45`.
Its audit contains only the known recoverable OHCI alignment warning, missing
optional regulatory database, and unrelated init fallback. No GX, DRM, FIFO,
PE, VI, oops, panic, or machine-check fault appears.

This closes sustained mixed-format scanout and in-flight provider lifecycle as
hardware risks for the accepted backend. Further Wii-only work should probe
offscreen EFB-to-texture copy/readback needed by a future render interface,
not repeat the now-stable scanout path.

### Stage final XFB-copy PE fence

- Test branch: `test/wii-drm-gx-copy-fence`
- Candidate commit: `9a5848232`
- Unchanged running `zImage` SHA-256:
  `3718f607e3d6e2aaae726fb7eb110b130f4e48ae7fd5022a720f0c2ca0b1d5cc`
- Candidate `gcn-gx.ko` SHA-256:
  `482f7a2768d77bd8acd8fd04d1daf2bf32f18b7ec1e56c69e4c33965c4248bb4`

Hardware counters show that each generated DRM frame emits two PE-finish
markers: the explicit draw fence before the display copy and the fence queued
after EFB-to-XFB copy control. The accepted callback waited only for a counter
change, so it could return at the draw marker and let DRM publish the page
before the final copy marker. Stress happened to remain visually clean, but
that timing is not a valid completion contract.

Wait for a wrap-safe counter delta of two while retaining the existing 50 ms
timeout and CPU fallback. No GX command, raster state, copy state, DRM page
selection, or VI ownership changes. Normal and `W=1` module builds, strict
checkpatch, and `git diff --check` pass.

Live-load the pinned module against the unchanged accepted kernel. Run the
same deterministic XRGB8888 and RGB565 page-flip fixtures. For each isolated
run, require `pe_finishes` to equal exactly twice the number of submitted GX
frames, all requested flips to complete, VI IRQs to advance, output to remain
crisp, and no final-finish timeout or CPU fallback. Unload and reload once to
confirm fresh counter behavior before acceptance.

Hardware result: passed; accept the final XFB-copy fence. The XRGB8888 run
completed all 300 requested flips while total GX frames advanced by 302,
XRGB8888 frames advanced by 301, PE finishes advanced by exactly 604, and VI
IRQs advanced by 346. The following RGB565 run completed all 300 flips while
total GX frames advanced by 301, PE finishes advanced by exactly 602, and VI
IRQs advanced by 314. Both formats therefore met the exact two-PE-finishes per
generated-frame invariant, including their initial modeset submissions. The
user observed the complete mixed-format test and reported that it ran
perfectly.

Remove and reload the exact candidate module to reset all provider counters,
then run another 60-flip XRGB8888 fixture. The fresh instance reached 63 total
frames, 61 XRGB8888 frames, and exactly 126 PE finishes. All requested flips
completed and no timeout, fallback, FIFO, DRM, oops, panic, or machine-check
fault appeared.

The final 388-line kernel log, including the fresh-module result, is preserved
at `/tmp/wii-dmesg-drm-gx-copy-fence-9a5848232.txt` with SHA-256
`f455b6f3c69e95c6b45b695acec16bea82e6568a02bc55204c742c2dd6d5c157`.
Its audit contains only the known recoverable OHCI alignment warning, missing
optional regulatory database, and unrelated init fallback. Candidate
`9a5848232` now provides the required synchronous completion contract: DRM
does not publish an inactive XFB until the final EFB-to-XFB copy marker has
completed.

### Stage EFB-to-RGB565-texture round-trip probe

- Test branch: `test/wii-gx-efb-texture-copy`
- Candidate commit: `c6c288c78`
- Unchanged running `zImage` SHA-256:
  `3718f607e3d6e2aaae726fb7eb110b130f4e48ae7fd5022a720f0c2ca0b1d5cc`
- Candidate `gcn-gx.ko` SHA-256:
  `1c410f7371c456bdaa281f173be2c31fa707092e297e92d4310bb4d4494fc80f`

Probe the remaining Wii-only prerequisite for a future render interface:
offscreen EFB-to-texture copy followed by texture replay. This is isolated
behind the read-only `offscreen_probe=1` module parameter; default DRM GX
scanout is unchanged.

On the first provider callback, fill the alternate reserved MEM1 texture slot
with a sentinel, render the established direct-colour grid into EFB, copy EFB
to the slot as tiled RGB565, and clear EFB to purple. Wait for both the raster
and texture-copy PE finishes, invalidate the CPU cache, and require all 153600
32-bit destination words at 640x480 to differ from the sentinel. Then bind the
copied texture, replay it through the accepted texture renderer, copy it to the
inactive XFB, and clear EFB to teal. The explicit clears prevent the original
direct draw from creating a visual false positive.

Load the checksum-pinned module with `offscreen_probe=1` and run the exact
XRGB8888 page-flip fixture. Require `offscreen_copies=1`,
`offscreen_changed_words=153600`, increasing `offscreen_replays`, and the exact
counter invariant `pe_finishes = 2 * (offscreen_copies +
offscreen_replays)`. Visually require the reconstructed grid with red
top-left, green top-right, blue bottom-left, white bottom-right, and crisp
black 32-pixel grid lines. Purple or teal output is a replay failure. Finally,
unload the module and require immediate clean CPU-console fallback.

Normal and `W=1` PowerPC module builds, strict checkpatch, module-parameter
inspection, and `git diff --check` pass before deployment.

Hardware result: passed; accept EFB-to-RGB565-texture copy and replay. The
checksum-pinned module loaded with `offscreen_probe=1` and completed one
offscreen copy followed by 130 visible replays. All 153600 destination words
changed from the sentinel. Total PE finishes reached exactly 262, satisfying
`2 * (1 copy + 130 replays)`, while total visible frames reached 130 and VI
interrupts continued advancing. The 120-flip XRGB8888 client completed every
requested flip. No copy, replay, FIFO, completion, DRM, oops, panic, or
machine-check fault appeared. The user observed a crisp four-quadrant grid,
confirming that the texture replay reconstructed the original EFB draw after
the copy operation had cleared EFB to purple.

The first unload check correctly removed the module and unregistered the
provider, but fbcon did not become visible because the test client still held
DRM master. This is intentional client behavior: `wii-drm-test` pauses after
its requested flips so the final frame remains inspectable until it receives a
signal. After terminating that client, no DRM holder remained, VI interrupts
advanced from 177622 to 177758, output returned to active `tty1`, and the user
confirmed that the CPU console was visible. Treat explicit client termination
as part of future fallback tests; do not attribute a held final frame to GX
unregistration.

Reload the same candidate without `offscreen_probe` for a default-path
regression. All 80 requested XRGB8888 flips completed. Offscreen counters
remained zero, total frames reached 83, XRGB8888 frames reached 81, and PE
finishes reached exactly 166. VI interrupts reached 180996 with no fault. The
user classified the normal fixture as correct and crisp. Explicitly terminate
the holding client, unload the module, and confirm no DRM holder remains; VI
interrupts then reached 181133 and CPU output resumed.

The independently retrieved 407-line offscreen-test log is preserved at
`/tmp/wii-dmesg-gx-efb-texture-copy-c6c288c78.txt` with SHA-256
`379cb1e8871715626d3a998db95c2a94c1c6748d111780f2d2696c966cf0149a`.
Its fault audit is clean apart from the already documented boot-environment
warnings. The default regression was audited live before shutdown and also
contained no GX, DRM, FIFO, completion, oops, panic, or machine-check fault.

Candidate `c6c288c78` establishes the hardware capability needed for future
offscreen rendering: GX can render into EFB, copy the result into reserved
MEM1 in native tiled RGB565 layout, invalidate and rebind that GPU-produced
texture, and sample it into a later visible frame with deterministic
completion fences. A render UAPI can now be designed from a demonstrated
round trip rather than an assumed copy path.

### Stage GX platform-resource ownership

- Test branch: `test/wii-gx-resource-model`
- Candidate commit: `58ecee803`
- Candidate `zImage` SHA-256:
  `0b6558f289ad809d7a79d18da6c1e98d12685f1f91e1edb9c88e15d79f8a82e7`
- Candidate `gcn-gx.ko` SHA-256:
  `815162479537fe0b5f90ee814221f5b9728cb24dcadafbffd8622e58c918f1eb`
- Embedded `wii.dtb` SHA-256:
  `94acfda6083844dc3a14f643d2bcb9b1b3e1250c5e7337cc1774869c269d6a6f`

Replace the bring-up driver's anonymous GX reservations, fixed MEM1
addresses, broad `ioremap(0x0c000000, 0x9000)`, and raw hwirq mapping with a
named platform-resource contract. The unchanged physical ranges are now
described as reserved-memory nodes. GX receives separate CP and PE windows,
the PE finish interrupt, named FIFO and texture regions, and a phandle to the
shared big-endian PI syscon. VI receives an explicit XFB memory reference.

The module now binds through platform probe/remove. It rejects missing,
`no-map`, undersized, misaligned, overlapping, or non-MEM1 FIFO and texture
regions before hardware initialization. FIFO and texture addresses are
derived from DT; no operational hard-coded GX buffer address remains. PI
FIFO access uses the shared regmap instead of remapping the full PI block,
while CP and PE are the only MMIO windows claimed directly by GX. Rendering
commands, PE fences, provider callbacks, XFB selection, and CPU fallback are
unchanged.

Host validation passed a full PowerPC `zImage` and modules build with `-j16`,
Wii DT compilation and decompilation, changed-line checkpatch, `git diff
--check`, module OF-alias inspection, and embedded-DT string inspection. A
temporary `dtschema` environment then completed both `dt_binding_check` for
`nintendo,flipper-gx.yaml` and the filtered `dtbs_check` with no error. Only
the optional `yamllint` style pass was skipped because that package is not
installed.

This test must deploy both checksum-pinned artifacts because the old DT has
no GX platform node. After cold boot, first verify the named reserved-memory
regions and `nintendo,hollywood-gx` platform device without loading the
provider. CPU DRM scanout must remain clear and responsive. Then load the
matching module and require the discovered addresses to remain FIFO
`0x01684000` and textures `0x01300000`/`0x013c0000`, with the PE IRQ mapped
and no resource, regmap, overlap, timeout, or fault message.

Run short RGB565 and XRGB8888 fixtures and require the exact two-PE-finishes
per generated-frame invariant, completed flips, advancing VI interrupts, and
crisp output. Repeat the accepted `offscreen_probe=1` round trip and require
one copy, all 153600 words changed, visible replay, and the same completion
invariant. Explicitly terminate each holding client, unload the module, and
confirm immediate clean CPU-console fallback. Do not add the MEM1 allocator
or any render UAPI until this ownership-only conversion passes.

Hardware result: passed; accept the GX platform-resource ownership model.
Cold boot with the candidate DT reserved the exact named MEM1 ranges at
`0x01300000..0x0147ffff` (texture), `0x01684000..0x01693fff` (FIFO), and
`0x01698000..0x017fffff` (XFB). The `c000000.gpu` platform device existed
before provider load, CPU DRM scanout remained active, and its live OF cells
reported CP `0x0c000000+0x80`, PE `0x0c001000+0x10`, FIFO
`0x01684000+0x10000`, and texture `0x01300000+0x180000`.

Loading checksum-pinned module
`815162479537fe0b5f90ee814221f5b9728cb24dcadafbffd8622e58c918f1eb`
bound `c000000.gpu` to `gcn-gx` and discovered FIFO `0x01684000`, texture
slots `0x01300000`/`0x013c0000`, and Linux PE IRQ 25. A normal RGB565 run
reached 329 frames and exactly 658 PE finishes. Its independently retrieved
XFB snapshot has SHA-256
`952ad9ae5198e9f85e5661a8db4fbe728d3342fe5c2aee56c4db9e875be6f01d`;
the corresponding source RGB565 snapshot has SHA-256
`0f05a5822841e3c08e7e8c04ab672c7662645e8ea3f982ab5216565770c49ebf`.
Both PNG conversions show a readable, geometrically correct console with no
repeated columns, diffusion, tiling corruption, or channel swap.

Add opt-in DRM XFB capture in commits `e53246f6d` and `a86fdbefe` without
changing normal rendering. The resulting XRGB8888 fixture completed all 3000
requested flips while the provider reached 2986 captured callbacks and
exactly 5972 PE finishes. Its checksum-verified XFB has SHA-256
`d89e784ed7e48c8610e7f489671cb584ac869023255975549ecfb6b868773a49`;
PNG SHA-256 is
`8e2004f388ec7c6ff00b71391bedf09264956a355bb653c45ae1db362e1ec1a2`.
The PNG exactly reproduces the deterministic red/green/blue/white quadrants,
black grid, cyan/magenta checkerboard, and yellow marker.

Repeat the offscreen probe using final module SHA-256
`07c3d2b6305d7cea37a8758f82e20c6102085100e08986954d84a886571e0f4d`.
The DT-owned alternate texture changed all 153600 words; one copy and 63
replays produced exactly 126 PE finishes at snapshot time, and the fixture
subsequently completed all 120 requested flips. The replayed XFB has SHA-256
`2c488feb9b32a2510a6912f85c8187e021e0fe3d7b228f8431395502b57cd075`;
PNG SHA-256 is
`8bba3984a94f1556b0cf031a2ecdc96e606af63a3e9b2855b159cde3eea03e63`.
It shows the exact crisp red/green/blue/white grid, with no purple or teal
clear leakage and no replay-layout corruption.

Terminate the holding client and unload GX. With no provider present, a
40-flip XRGB8888 fixture completed through CPU fallback while VI interrupts
advanced from 62889 to 63129. No resource, regmap, overlap, FIFO, completion,
DRM, oops, panic, or machine-check fault appears in the independently
retrieved 464-line kernel log at
`/tmp/wii-dmesg-gx-resource-model-a86fdbefe.txt`, SHA-256
`c584a1df5771a8555debd6f112c0b3926651f46446e19200a39782d6af2fba6d`.

This closes Stage 1. GX now owns hardware through explicit DT resources while
preserving the accepted normal scanout, offscreen round trip, modular unload,
and CPU fallback behavior. The next ownership stage may introduce a bounded
MEM1 allocator; a render UAPI remains a later and separate interface change.
