/*
 * Optional accelerator interface for the Nintendo GameCube/Wii VI driver.
 *
 * gcnfb owns fbcon, VI interrupts, and the software RGB-to-YUYV fallback.
 * Accelerator modules register these callbacks without taking ownership of
 * the framebuffer device, so they can be loaded and unloaded independently.
 */

#ifndef _GCNFB_ACCEL_H
#define _GCNFB_ACCEL_H

#include <linux/types.h>

struct gcnfb_accel_ops {
	const char *name;
	/* Return the XFB and immutable VFB source associated with one completion. */
	bool (*take_completed)(u32 *xfb_phys, const void **vfb);
	void (*blit_rgb565)(const void *vfb, u32 xfb_phys,
			    u16 width, u16 height);
	void (*blit_rgb888)(const void *vfb, u32 xfb_phys,
			    u16 width, u16 height);
};

int gcnfb_register_accel(const struct gcnfb_accel_ops *ops);
void gcnfb_unregister_accel(const struct gcnfb_accel_ops *ops);
void gcnfb_accel_source_consumed(const struct gcnfb_accel_ops *ops,
				 const void *vfb);

#endif /* _GCNFB_ACCEL_H */
