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
