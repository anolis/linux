.. SPDX-License-Identifier: GPL-2.0-only

==================
Wii Linux NXT
=============

Wii Linux NXT is an experimental modern Linux kernel port for Nintendo
GameCube and Wii hardware.  This tree tracks Linux 6.18 and contains the
validated Video Interface (VI) DRM/KMS driver and GX graphics accelerator
developed on physical Wii consoles.

This is development software.  Keep a known-good SD-card image and recovery
path available.  The graphics stack is usable for framebuffer consoles and
lightweight native userspace, but it is not yet an upstream-quality driver or
a complete OpenGL implementation.

Graphics architecture
=====================

The hardware has two distinct responsibilities:

* VI scans packed YUYV external framebuffers (XFBs), generates display timing,
  and reports vertical blank interrupts.
* GX renders through its embedded framebuffer (EFB) and copies completed
  frames into an XFB.

The ``gcn-drm`` driver owns modesetting, scanout, vblank events, dumb buffers,
and atomic page flips.  The optional ``gcn-gx`` module owns GX command
submission and accelerates conversion and rendering while preserving the DRM
driver's scanout ownership.  If GX is absent or unloaded, the DRM driver uses
its CPU conversion path and keeps the display live.

Validated functionality
=======================

The branch has been exercised on physical Wii hardware with:

* native DRM/KMS ownership and framebuffer console output;
* standard RGB565 and XRGB8888 dumb buffers;
* atomic page flips and vblank events;
* reversible ``gcn-gx`` module load and unload with CPU fallback;
* bounded RGB565 render objects, fills, rectangle blits, and scaled blits;
* system-memory RGB565 and XRGB8888 presentation;
* KUnit coverage for MEM1 allocation and render-UAPI validation;
* progressive output through ``gcn_drm.progressive=1``; and
* AVE I2C integration and device-tree resource ownership.

A triple-buffered full-frame RGB565 software workload sustained approximately
29.77 frames per second, matching the roughly 29.97 Hz complete-frame display
cadence.  The corresponding full-frame XRGB8888 workload reached approximately
24.71 frames per second.  Detailed checksum-pinned hardware experiments are
recorded in ``docs/wii-6.18-gx-port.md``.

Current limitations
===================

* There is no Mesa driver or hardware OpenGL implementation yet.
* The GX interface is experimental and its UAPI must not be treated as stable.
* Modesetting and AVE behavior need broader validation across cables, regions,
  display standards, and GameCube hardware.
* XRGB8888 conversion still has avoidable memory and cache traffic.
* The legacy ``gcnfb`` path remains available during migration; production
  configurations should select one display owner.

Build
=====

Install a 32-bit big-endian PowerPC cross compiler, then run::

    make ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- wii_defconfig
    make -j16 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- zImage modules

The Wii boot image is produced under ``arch/powerpc/boot/``.  Do not deploy an
artifact without recording its commit and checksum; the hardware-validation
history relies on exact image identification.

Important configuration symbols include:

* ``CONFIG_DRM_GCN_VI`` for VI DRM/KMS;
* ``CONFIG_DRM_GCN_GX`` for the optional GX accelerator;
* ``CONFIG_I2C_WII_AVE`` for the Wii AV encoder interface; and
* ``CONFIG_FB_GAMECUBE`` for the legacy framebuffer migration path.

Development boundaries
======================

This repository owns the kernel, hardware drivers, device-tree bindings, and
kernel UAPI.  WiiDesk, login/session management, VNC, desktop applications,
themes, and distribution packaging belong in a separate userspace repository.
Userspace should prefer standard DRM/KMS interfaces; Wii-specific render
operations should be used only where standard DRM cannot express the hardware
operation.

Further reading
===============

* ``Documentation/gpu/gcn-drm.rst`` describes the DRM/GX ownership model.
* ``Documentation/devicetree/bindings/gpu/nintendo,flipper-gx.yaml`` describes
  the GX hardware node.
* ``Documentation/devicetree/bindings/i2c/nintendo,wii-ave-i2c.yaml`` describes
  the AVE control interface.
* ``docs/wii-6.18-gx-port.md`` is the detailed engineering and hardware-test
  journal.
* The pre-publication development history is preserved in the
  ``feature/wii-6.18-drm-kms`` branch of
  https://github.com/anolis/linux.
