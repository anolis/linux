.. SPDX-License-Identifier: GPL-2.0-only

=========================================
Nintendo GameCube/Wii DRM/KMS development
=========================================

Hardware split
==============

The Video Interface (VI) scans packed YUYV external framebuffer (XFB) memory
and drives the analog video output. The GX graphics processor renders into its
embedded framebuffer (EFB) and can copy the result into an XFB. These are
separate hardware blocks and should remain separate software responsibilities:

* KMS owns VI mode state, XFB scanout addresses, vertical blank interrupts,
  atomic presentation, and connector state.
* The rendering driver will own GX command submission, GPU buffers, fences,
  and the render node used by Mesa.
* A software conversion path remains available so KMS and fbcon work before
  GX acceleration is attached.

Migration boundary
==================

The legacy ``gcnfb`` driver and ``gcn-drm`` match the same device-tree VI
node. They may be compiled together to support reversible development, but
only one driver can bind that node at a time. The first KMS milestone supports
live handoff after ``gcnfb`` has established the known-good display mode:

1. Unbind ``gcnfb`` from the VI platform device.
2. Load and bind ``gcn-drm`` without resetting the established VI timings.
3. Exercise atomic scanout and vblank event handling.
4. Unload ``gcn-drm`` and rebind ``gcnfb`` to restore the accepted console.

This handoff dependency is temporary. Native VI mode programming must be
validated before ``gcn-drm`` replaces ``gcnfb`` in the boot configuration.

Milestones
==========

1. Register one fixed VI connector, CRTC, and primary plane using atomic KMS.
2. Back dumb buffers with shmem GEM and expose XRGB8888 and RGB565 scanout.
3. Convert into two reserved YUYV XFB pages and switch pages at VI vblank.
4. Validate fbcon, atomic page flips, teardown, and legacy-driver recovery.
5. Move native VI mode programming behind KMS mode validation and enable.
6. Integrate GX rendering through DRM GEM objects, explicit fences, and a
   render node without weakening KMS ownership.
7. Implement the matching Mesa driver and expose hardware OpenGL.
