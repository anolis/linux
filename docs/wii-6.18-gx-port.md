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
