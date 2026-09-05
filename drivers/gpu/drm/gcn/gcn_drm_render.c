// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dma-resv.h>
#include <linux/dma-buf.h>
#include <linux/iosys-map.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>

#include <uapi/drm/gcn_drm.h>
#include <uapi/drm/drm_mode.h>

#include "gcn_drm_internal.h"
#include "gcn_drm_render.h"

struct gcn_drm_render_file {
	struct xarray contexts;
};

struct gcn_drm_bo {
	struct drm_gem_object gem;
	const struct gcn_drm_accel_ops *provider;
	void *allocation;
	u32 width;
	u32 height;
	u32 format;
	u32 layout;
};

struct gcn_drm_system_bo {
	struct drm_gem_shmem_object shmem;
	u32 width;
	u32 height;
	u32 format;
	u32 layout;
	bool render_object;
};

static inline struct gcn_drm_bo *to_gcn_drm_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct gcn_drm_bo, gem);
}

static inline struct gcn_drm_system_bo *
to_gcn_drm_system_bo(struct drm_gem_object *gem)
{
	return container_of(to_drm_gem_shmem_obj(gem),
			    struct gcn_drm_system_bo, shmem);
}

static void gcn_drm_bo_free(struct drm_gem_object *gem)
{
	struct gcn_drm_bo *bo = to_gcn_drm_bo(gem);

	drm_gem_object_release(gem);
	gcn_drm_provider_free(bo->provider, bo->allocation);
	kfree(bo);
}

static int gcn_drm_bo_mmap(struct drm_gem_object *gem,
			   struct vm_area_struct *vma)
{
	struct gcn_drm_bo *bo = to_gcn_drm_bo(gem);

	vma->vm_ops = gem->funcs->vm_ops;
	return gcn_drm_provider_mmap(bo->provider, bo->allocation, vma);
}

static struct dma_buf *gcn_drm_bo_export(struct drm_gem_object *gem,
					 int flags)
{
	(void)gem;
	(void)flags;
	return ERR_PTR(-EOPNOTSUPP);
}

static const struct vm_operations_struct gcn_drm_bo_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs gcn_drm_bo_funcs = {
	.free = gcn_drm_bo_free,
	.export = gcn_drm_bo_export,
	.mmap = gcn_drm_bo_mmap,
	.vm_ops = &gcn_drm_bo_vm_ops,
};

static const struct drm_gem_object_funcs gcn_drm_system_bo_funcs = {
	.free = drm_gem_shmem_object_free,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

struct drm_gem_object *
gcn_drm_render_create_object(struct drm_device *drm, size_t size)
{
	struct gcn_drm_system_bo *bo;

	(void)drm;
	(void)size;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->shmem.base.funcs = &gcn_drm_system_bo_funcs;
	return &bo->shmem.base;
}

static bool gcn_drm_is_mem1_bo(const struct drm_gem_object *gem)
{
	return gem->funcs == &gcn_drm_bo_funcs;
}

static bool gcn_drm_is_system_bo(const struct drm_gem_object *gem)
{
	return gem->funcs == &gcn_drm_system_bo_funcs &&
	       to_gcn_drm_system_bo((struct drm_gem_object *)gem)->render_object;
}

int gcn_drm_render_validate_framebuffer(struct drm_file *file,
					const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct gcn_drm_system_bo *bo;
	struct drm_gem_object *gem;
	int ret = 0;

	if (!file || !mode_cmd || !mode_cmd->handles[0])
		return -EINVAL;

	gem = drm_gem_object_lookup(file, mode_cmd->handles[0]);
	if (!gem)
		return -ENOENT;
	if (gcn_drm_is_mem1_bo(gem)) {
		ret = -EINVAL;
		goto out_put;
	}
	if (gem->funcs != &gcn_drm_system_bo_funcs)
		goto out_put;

	bo = to_gcn_drm_system_bo(gem);
	if (!bo->render_object)
		goto out_put;
	ret = gcn_drm_render_validate_fb(bo->width, bo->height, bo->format,
					 bo->layout, mode_cmd);

out_put:
	drm_gem_object_put(gem);
	return ret;
}

static int gcn_drm_ioctl_get_param(struct drm_device *drm, void *data,
				   struct drm_file *file)
{
	struct drm_gcn_get_param *args = data;
	struct gcn_drm_mem1_info info;
	int ret;

	(void)drm;
	(void)file;

	if (args->pad)
		return -EINVAL;

	switch (args->param) {
	case DRM_GCN_PARAM_ABI_VERSION:
		args->value = DRM_GCN_RENDER_ABI_VERSION;
		return 0;
	case DRM_GCN_PARAM_FEATURES:
		args->value = DRM_GCN_FEATURE_CONTEXTS |
			      DRM_GCN_FEATURE_WAIT |
			      DRM_GCN_FEATURE_SYNCOBJ;
		ret = gcn_drm_provider_info(&info);
		if (!ret)
			args->value |= DRM_GCN_FEATURE_MEM1_GEM | info.features;
		return 0;
	case DRM_GCN_PARAM_PROVIDER_AVAILABLE:
		args->value = !gcn_drm_provider_info(&info);
		return 0;
	case DRM_GCN_PARAM_MEM1_TOTAL_BYTES:
	case DRM_GCN_PARAM_MEM1_FREE_BYTES:
	case DRM_GCN_PARAM_MEM1_ALIGNMENT:
	case DRM_GCN_PARAM_MAX_EFB_WIDTH:
	case DRM_GCN_PARAM_MAX_EFB_HEIGHT:
	case DRM_GCN_PARAM_FORMATS:
	case DRM_GCN_PARAM_LAYOUTS:
		break;
	default:
		return -EINVAL;
	}

	ret = gcn_drm_provider_info(&info);
	if (ret)
		return ret;

	switch (args->param) {
	case DRM_GCN_PARAM_MEM1_TOTAL_BYTES:
		args->value = info.total_bytes;
		break;
	case DRM_GCN_PARAM_MEM1_FREE_BYTES:
		args->value = info.free_bytes;
		break;
	case DRM_GCN_PARAM_MEM1_ALIGNMENT:
		args->value = info.alignment;
		break;
	case DRM_GCN_PARAM_MAX_EFB_WIDTH:
		args->value = info.max_width;
		break;
	case DRM_GCN_PARAM_MAX_EFB_HEIGHT:
		args->value = info.max_height;
		break;
	case DRM_GCN_PARAM_FORMATS:
		args->value = info.formats;
		break;
	case DRM_GCN_PARAM_LAYOUTS:
		args->value = info.layouts;
		break;
	}

	return 0;
}

static int gcn_drm_ioctl_gem_create(struct drm_device *drm, void *data,
				    struct drm_file *file)
{
	struct drm_gcn_gem_create *args = data;
	struct drm_gem_shmem_object *shmem;
	struct gcn_drm_system_bo *system_bo;
	struct gcn_drm_bo *bo;
	struct gcn_drm_mem1_info info;
	u64 size;
	int ret;

	if (args->handle || args->size)
		return -EINVAL;
	ret = gcn_drm_render_bo_size(args, &size);
	if (ret)
		return ret;

	if (args->flags & DRM_GCN_GEM_CREATE_SYSTEM) {
		ret = gcn_drm_provider_info(&info);
		if (ret)
			return ret;
		if (!(info.features & DRM_GCN_FEATURE_SYSTEM_GEM))
			return -EOPNOTSUPP;
		if (args->format == DRM_GCN_GEM_FORMAT_XRGB8888 &&
		    (!(info.formats & DRM_GCN_FORMAT_XRGB8888) ||
		     !(info.features &
		       DRM_GCN_FEATURE_BLIT_SCALED_SYSTEM_XRGB8888_TO_RGB565)))
			return -EOPNOTSUPP;
		if (args->layout == DRM_GCN_GEM_LAYOUT_LINEAR &&
		    !(info.features & DRM_GCN_FEATURE_SYSTEM_GEM_LINEAR))
			return -EOPNOTSUPP;

		shmem = drm_gem_shmem_create(drm, size);
		if (IS_ERR(shmem))
			return PTR_ERR(shmem);
		system_bo = container_of(shmem, struct gcn_drm_system_bo, shmem);
		system_bo->width = args->width;
		system_bo->height = args->height;
		system_bo->format = args->format;
		system_bo->layout = args->layout;
		system_bo->render_object = true;
		ret = drm_gem_handle_create(file, &shmem->base, &args->handle);
		if (!ret)
			args->size = size;
		drm_gem_object_put(&shmem->base);
		return ret;
	}

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return -ENOMEM;

	ret = gcn_drm_provider_alloc(size, &bo->provider, &bo->allocation);
	if (ret)
		goto err_free;

	bo->gem.funcs = &gcn_drm_bo_funcs;
	drm_gem_private_object_init(drm, &bo->gem, size);
	ret = drm_gem_create_mmap_offset(&bo->gem);
	if (ret)
		goto err_release;

	bo->width = args->width;
	bo->height = args->height;
	bo->format = args->format;
	bo->layout = args->layout;
	ret = drm_gem_handle_create(file, &bo->gem, &args->handle);
	if (ret)
		goto err_release;

	args->size = size;
	drm_gem_object_put(&bo->gem);
	return 0;

err_release:
	drm_gem_object_release(&bo->gem);
	gcn_drm_provider_free(bo->provider, bo->allocation);
err_free:
	kfree(bo);
	return ret;
}

static int gcn_drm_ioctl_gem_mmap(struct drm_device *drm, void *data,
				  struct drm_file *file)
{
	struct drm_gcn_gem_mmap *args = data;
	struct drm_gem_object *gem;
	int ret = 0;

	(void)drm;

	if (args->pad || args->offset)
		return -EINVAL;

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;
	if (!gcn_drm_is_mem1_bo(gem) && !gcn_drm_is_system_bo(gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	args->offset = drm_vma_node_offset_addr(&gem->vma_node);
out_put:
	drm_gem_object_put(gem);
	return ret;
}

static int gcn_drm_ioctl_ctx_create(struct drm_device *drm, void *data,
				    struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_ctx_create *args = data;
	struct gcn_drm_mem1_info info;
	int ret;

	(void)drm;

	if (args->flags || args->id)
		return -EINVAL;
	ret = gcn_drm_provider_info(&info);
	if (ret)
		return ret;

	return xa_alloc(&render->contexts, &args->id, xa_mk_value(1),
			xa_limit_32b, GFP_KERNEL);
}

static int gcn_drm_ioctl_ctx_free(struct drm_device *drm, void *data,
				  struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_ctx_free *args = data;

	(void)drm;

	if (!args->id || args->pad)
		return -EINVAL;
	if (!xa_erase(&render->contexts, args->id))
		return -ENOENT;

	return 0;
}

static int gcn_drm_ioctl_wait(struct drm_device *drm, void *data,
			      struct drm_file *file)
{
	struct drm_gcn_wait *args = data;
	struct drm_gem_object *gem;
	unsigned long timeout;
	long ret;

	(void)drm;

	if (args->flags & ~DRM_GCN_WAIT_WRITE)
		return -EINVAL;

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;
	if (!gcn_drm_is_mem1_bo(gem) && !gcn_drm_is_system_bo(gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	timeout = gcn_drm_render_timeout_jiffies(args->timeout_ns,
						 ktime_get_ns());
	ret = dma_resv_wait_timeout(gem->resv,
				    dma_resv_usage_rw(args->flags &
						      DRM_GCN_WAIT_WRITE),
				    true, timeout);
	if (!ret)
		ret = -ETIME;
	else if (ret > 0)
		ret = 0;
out_put:
	drm_gem_object_put(gem);
	return ret;
}

static int gcn_drm_ioctl_submit(struct drm_device *drm, void *data,
				struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_submit *args = data;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_bo *src = NULL;
	struct gcn_drm_bo *dst = NULL;
	struct gcn_drm_system_bo *system_dst = NULL;
	struct gcn_drm_render_rect rect;
	struct gcn_drm_render_blit_rect blit_rect;
	struct drm_exec exec;
	u16 dst_width;
	u16 dst_height;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_submit(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (args->src_handle)
		src_gem = drm_gem_object_lookup(file, args->src_handle);
	if ((args->src_handle && !src_gem) || !dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (src_gem && !gcn_drm_is_mem1_bo(src_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	if (gcn_drm_is_mem1_bo(dst_gem)) {
		dst = to_gcn_drm_bo(dst_gem);
		if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
			ret = -EINVAL;
			goto out_put;
		}
		dst_width = dst->width;
		dst_height = dst->height;
	} else if (gcn_drm_is_system_bo(dst_gem) &&
		   (args->op == DRM_GCN_RENDER_OP_FILL_RGB565 ||
		    args->op == DRM_GCN_RENDER_OP_FILL_RECT_RGB565)) {
		system_dst = to_gcn_drm_system_bo(dst_gem);
		if (system_dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    system_dst->layout != DRM_GCN_GEM_LAYOUT_LINEAR) {
			ret = -EINVAL;
			goto out_put;
		}
		dst_width = system_dst->width;
		dst_height = system_dst->height;
	} else {
		ret = -EINVAL;
		goto out_put;
	}
	if (args->op == DRM_GCN_RENDER_OP_FILL_RECT_RGB565) {
		ret = gcn_drm_render_validate_rect(args->data, dst_width,
						   dst_height);
		if (ret)
			goto out_put;
		gcn_drm_render_decode_rect(args->data, &rect);
	}
	if (src_gem) {
		src = to_gcn_drm_bo(src_gem);
		if (!dst || src->provider != dst->provider ||
		    src->format != dst->format || src->layout != dst->layout) {
			ret = -EINVAL;
			goto out_put;
		}
		if (args->op == DRM_GCN_RENDER_OP_COPY_RGB565 &&
		    (src->width != dst_width || src->height != dst_height)) {
			ret = -EINVAL;
			goto out_put;
		}
	}
	if (args->op == DRM_GCN_RENDER_OP_BLIT_RECT_RGB565) {
		ret = gcn_drm_render_validate_blit_rect(args->data, src->width,
							src->height, dst_width,
							dst_height);
		if (ret)
			goto out_put;
		gcn_drm_render_decode_blit_rect(args->data, &blit_rect);
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT,
		      src_gem && src_gem != dst_gem ? 2 : 1);
	drm_exec_until_all_locked(&exec) {
		if (src_gem) {
			ret = drm_exec_prepare_obj(&exec, src_gem, 1);
			drm_exec_retry_on_contention(&exec);
			if (ret)
				break;
		}
		if (src_gem != dst_gem) {
			ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
			drm_exec_retry_on_contention(&exec);
			if (ret)
				break;
		}
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	if (system_dst) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);

		ret = drm_gem_shmem_vmap_locked(&system_dst->shmem, &map);
		if (ret)
			goto out_exec;
		if (map.is_iomem) {
			ret = -EINVAL;
		} else if (args->op == DRM_GCN_RENDER_OP_FILL_RGB565) {
			ret = gcn_drm_provider_fill_system(map.vaddr, dst_width,
							   dst_height,
							   system_dst->layout, 0, 0,
							   dst_width, dst_height,
							   (u16)args->data);
		} else {
			ret = gcn_drm_provider_fill_system(map.vaddr, dst_width,
							   dst_height,
							   system_dst->layout,
							   rect.x, rect.y, rect.width,
							   rect.height, rect.color);
		}
		drm_gem_shmem_vunmap_locked(&system_dst->shmem, &map);
	} else if (args->op == DRM_GCN_RENDER_OP_COPY_RGB565)
		ret = gcn_drm_provider_copy(src->provider, src->allocation,
					    dst->allocation, src->width,
					    src->height);
	else if (args->op == DRM_GCN_RENDER_OP_FILL_RGB565)
		ret = gcn_drm_provider_fill(dst->provider, dst->allocation,
					    dst_width, dst_height,
					    (u16)args->data);
	else if (args->op == DRM_GCN_RENDER_OP_FILL_RECT_RGB565)
		ret = gcn_drm_provider_fill_rect(dst->provider, dst->allocation,
						 dst_width, dst_height,
						 rect.x, rect.y, rect.width,
						 rect.height, rect.color);
	else
		ret = gcn_drm_provider_blit_rect(src->provider, src->allocation,
						 dst->allocation, src->width,
						 src->height, dst_width,
						 dst_height, blit_rect.src_x,
						 blit_rect.src_y, blit_rect.dst_x,
						 blit_rect.dst_y, blit_rect.width,
						 blit_rect.height);
	if (ret)
		goto out_exec;

	/* An aliased blit writes the one object; do not add a duplicate read fence. */
	if (src_gem && src_gem != dst_gem)
		dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	return ret;
}

static int
gcn_drm_blit_scaled_system_locked(struct drm_gem_object *src_gem,
				  struct drm_gem_object *dst_gem,
				  u16 src_width, u16 src_height,
				  u16 dst_width, u16 dst_height,
				  const struct drm_gcn_blit_scaled *args)
{
	struct gcn_drm_system_bo *src = to_gcn_drm_system_bo(src_gem);
	struct gcn_drm_system_bo *dst = to_gcn_drm_system_bo(dst_gem);
	struct iosys_map src_map = IOSYS_MAP_INIT_VADDR(NULL);
	struct iosys_map dst_map = IOSYS_MAP_INIT_VADDR(NULL);
	void *dst_addr;
	int ret;

	ret = drm_gem_shmem_vmap_locked(&src->shmem, &src_map);
	if (ret)
		return ret;

	if (src_gem == dst_gem) {
		dst_addr = src_map.vaddr;
	} else {
		ret = drm_gem_shmem_vmap_locked(&dst->shmem, &dst_map);
		if (ret)
			goto out_unmap_src;
		dst_addr = dst_map.vaddr;
	}

	if (src_map.is_iomem || (src_gem != dst_gem && dst_map.is_iomem)) {
		ret = -EINVAL;
		goto out_unmap_dst;
	}
	ret = gcn_drm_provider_blit_scaled_system(src_map.vaddr, dst_addr,
						  src_width, src_height,
						  dst_width, dst_height,
						  src->format, dst->format,
						  src->layout, dst->layout, args);

out_unmap_dst:
	if (src_gem != dst_gem)
		drm_gem_shmem_vunmap_locked(&dst->shmem, &dst_map);
out_unmap_src:
	drm_gem_shmem_vunmap_locked(&src->shmem, &src_map);
	return ret;
}

static int gcn_drm_ioctl_blit_scaled(struct drm_device *drm, void *data,
				     struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_blit_scaled *args = data;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_system_bo *system_src = NULL;
	struct gcn_drm_system_bo *system_dst = NULL;
	struct gcn_drm_bo *mem1_src = NULL;
	struct gcn_drm_bo *mem1_dst = NULL;
	struct drm_exec exec;
	u16 src_width;
	u16 src_height;
	u16 dst_width;
	u16 dst_height;
	bool system_objects = false;
	int ret;

	(void)drm;

	if (!args->ctx_id || !args->src_handle || !args->dst_handle ||
	    args->flags || args->pad || !args->src_width || !args->src_height ||
	    !args->dst_width || !args->dst_height)
		return -EINVAL;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	src_gem = drm_gem_object_lookup(file, args->src_handle);
	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!src_gem || !dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (gcn_drm_is_mem1_bo(src_gem) && gcn_drm_is_mem1_bo(dst_gem)) {
		mem1_src = to_gcn_drm_bo(src_gem);
		mem1_dst = to_gcn_drm_bo(dst_gem);
		if (mem1_src->provider != mem1_dst->provider) {
			ret = -EINVAL;
			goto out_put;
		}
		src_width = mem1_src->width;
		src_height = mem1_src->height;
		dst_width = mem1_dst->width;
		dst_height = mem1_dst->height;
		if (mem1_src->format != mem1_dst->format ||
		    mem1_src->layout != mem1_dst->layout ||
		    mem1_src->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    mem1_src->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
			ret = -EINVAL;
			goto out_put;
		}
	} else if (gcn_drm_is_system_bo(src_gem) &&
		   gcn_drm_is_system_bo(dst_gem)) {
		system_src = to_gcn_drm_system_bo(src_gem);
		system_dst = to_gcn_drm_system_bo(dst_gem);
		src_width = system_src->width;
		src_height = system_src->height;
		dst_width = system_dst->width;
		dst_height = system_dst->height;
		ret = gcn_drm_valid_system_formats(system_src->format,
						   system_src->layout,
						   system_dst->format,
						   system_dst->layout,
						   src_gem == dst_gem);
		if (ret)
			goto out_put;
		system_objects = true;
	} else {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_scaled(args, src_width, src_height,
					     dst_width, dst_height);
	if (ret)
		goto out_put;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT,
		      src_gem != dst_gem ? 2 : 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, src_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
		if (src_gem != dst_gem) {
			ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
			drm_exec_retry_on_contention(&exec);
			if (ret)
				break;
		}
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	if (system_objects)
		ret = gcn_drm_blit_scaled_system_locked(src_gem, dst_gem,
							src_width, src_height,
							dst_width, dst_height, args);
	else
		ret = gcn_drm_provider_blit_scaled(mem1_src->provider,
						   mem1_src->allocation,
						   mem1_dst->allocation,
						   src_width, src_height,
						   dst_width, dst_height, args);
	if (ret)
		goto out_exec;

	if (src_gem != dst_gem)
		dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	return ret;
}

static int gcn_drm_ioctl_draw_triangle(struct drm_device *drm, void *data,
				       struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_triangle *args = data;
	struct gcn_drm_color_vertex vertices[3];
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	unsigned int i;
	int ret;

	(void)drm;

	if (!args->ctx_id || !args->dst_handle || args->flags ||
	    args->pad[0] || args->pad[1])
		return -EINVAL;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (!gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_triangle(args, dst->width, dst->height);
	if (ret)
		goto out_put;

	for (i = 0; i < 3; i++) {
		vertices[i].x = args->vertices[i].x;
		vertices[i].y = args->vertices[i].y;
		vertices[i].r = args->vertices[i].rgba >> 24;
		vertices[i].g = args->vertices[i].rgba >> 16;
		vertices[i].b = args->vertices[i].rgba >> 8;
		vertices[i].a = args->vertices[i].rgba;
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_triangle(dst->provider, dst->allocation,
					     dst->width, dst->height, vertices);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	return ret;
}

static int gcn_drm_ioctl_draw_triangles(struct drm_device *drm, void *data,
					struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_triangles *args = data;
	struct drm_gcn_color_triangle *triangles = NULL;
	struct gcn_drm_color_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	unsigned int i;
	unsigned int vertex_count;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_triangle_batch(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	triangles = memdup_array_user(u64_to_user_ptr(args->triangles_ptr),
				      args->triangle_count, sizeof(*triangles));
	if (IS_ERR(triangles)) {
		ret = PTR_ERR(triangles);
		triangles = NULL;
		goto out_put;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (!gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}

	for (i = 0; i < args->triangle_count; i++) {
		ret = gcn_drm_render_validate_color_triangle(triangles[i].vertices,
							     dst->width,
							     dst->height);
		if (ret)
			goto out_put;
	}

	vertex_count = args->triangle_count * 3;
	vertices = kcalloc(vertex_count, sizeof(*vertices), GFP_KERNEL);
	if (!vertices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < vertex_count; i++) {
		const struct drm_gcn_color_vertex *src =
			&triangles[i / 3].vertices[i % 3];

		vertices[i].x = src->x;
		vertices[i].y = src->y;
		vertices[i].r = src->rgba >> 24;
		vertices[i].g = src->rgba >> 16;
		vertices[i].b = src->rgba >> 8;
		vertices[i].a = src->rgba;
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_triangles(dst->provider, dst->allocation,
					      dst->width, dst->height,
					      vertices,
					      args->triangle_count);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(vertices);
	kfree(triangles);
	return ret;
}

static int gcn_drm_ioctl_draw_triangles_state(struct drm_device *drm, void *data,
					      struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_triangles_state *args = data;
	struct drm_gcn_color_triangle *triangles = NULL;
	struct gcn_drm_color_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	unsigned int vertex_count;
	unsigned int i;
	bool require_opaque;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_triangle_state_batch(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	triangles = memdup_array_user(u64_to_user_ptr(args->triangles_ptr),
				      args->triangle_count, sizeof(*triangles));
	if (IS_ERR(triangles)) {
		ret = PTR_ERR(triangles);
		triangles = NULL;
		goto out_put;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (!gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;

	require_opaque = args->state.blend_mode == DRM_GCN_BLEND_NONE;
	for (i = 0; i < args->triangle_count; i++) {
		ret = gcn_drm_render_validate_color_triangle_alpha(triangles[i].vertices,
								   dst->width, dst->height,
								   require_opaque);
		if (ret)
			goto out_put;
	}

	vertex_count = args->triangle_count * 3;
	vertices = kcalloc(vertex_count, sizeof(*vertices), GFP_KERNEL);
	if (!vertices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < vertex_count; i++) {
		const struct drm_gcn_color_vertex *src =
			&triangles[i / 3].vertices[i % 3];

		vertices[i].x = src->x;
		vertices[i].y = src->y;
		vertices[i].r = src->rgba >> 24;
		vertices[i].g = src->rgba >> 16;
		vertices[i].b = src->rgba >> 8;
		vertices[i].a = src->rgba;
	}
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_triangles_state(dst->provider, dst->allocation,
						    dst->width, dst->height, vertices,
						    args->triangle_count, &state);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(vertices);
	kfree(triangles);
	return ret;
}

static int gcn_drm_ioctl_draw_triangles_depth(struct drm_device *drm, void *data,
					      struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_triangles_depth *args = data;
	struct drm_gcn_color_depth_triangle *triangles = NULL;
	struct gcn_drm_color_depth_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_depth_state depth;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	unsigned int vertex_count;
	unsigned int i;
	bool require_opaque;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_triangle_depth_batch(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	triangles = memdup_array_user(u64_to_user_ptr(args->triangles_ptr),
				      args->triangle_count, sizeof(*triangles));
	if (IS_ERR(triangles)) {
		ret = PTR_ERR(triangles);
		triangles = NULL;
		goto out_put;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (!gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;

	require_opaque = args->state.blend_mode == DRM_GCN_BLEND_NONE;
	for (i = 0; i < args->triangle_count; i++) {
		ret = gcn_drm_validate_z(triangles[i].vertices, dst->width,
					 dst->height, require_opaque);
		if (ret)
			goto out_put;
	}

	vertex_count = args->triangle_count * 3;
	vertices = kcalloc(vertex_count, sizeof(*vertices), GFP_KERNEL);
	if (!vertices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < vertex_count; i++) {
		const struct drm_gcn_color_depth_vertex *src =
			&triangles[i / 3].vertices[i % 3];

		vertices[i].x = src->x;
		vertices[i].y = src->y;
		vertices[i].z = src->z;
		vertices[i].r = src->rgba >> 24;
		vertices[i].g = src->rgba >> 16;
		vertices[i].b = src->rgba >> 8;
		vertices[i].a = src->rgba;
	}
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;
	depth.test_enable = args->depth.test_enable;
	depth.compare = args->depth.compare;
	depth.write_enable = args->depth.write_enable;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_depth(dst->provider, dst->allocation,
					  dst->width, dst->height, vertices,
					  args->triangle_count, &state, &depth);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(vertices);
	kfree(triangles);
	return ret;
}

static int
gcn_drm_ioctl_draw_textured_triangles(struct drm_device *drm,
				      void *data,
				      struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_textured_triangles *args = data;
	struct drm_gcn_texture_triangle *triangles = NULL;
	struct gcn_drm_texture_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_bo *src;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	unsigned int vertex_count;
	unsigned int i;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_textured_triangle_batch(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	triangles = memdup_array_user(u64_to_user_ptr(args->triangles_ptr),
				      args->triangle_count, sizeof(*triangles));
	if (IS_ERR(triangles)) {
		ret = PTR_ERR(triangles);
		triangles = NULL;
		goto out_put;
	}

	src_gem = drm_gem_object_lookup(file, args->src_handle);
	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!src_gem || !dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (src_gem == dst_gem || !gcn_drm_is_mem1_bo(src_gem) ||
	    !gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	src = to_gcn_drm_bo(src_gem);
	dst = to_gcn_drm_bo(dst_gem);
	if (src->provider != dst->provider ||
	    src->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    src->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;
	for (i = 0; i < args->triangle_count; i++) {
		ret = gcn_drm_validate_texture_triangle(triangles[i].vertices,
							src->width, src->height,
							dst->width, dst->height);
		if (ret)
			goto out_put;
	}

	vertex_count = args->triangle_count * 3;
	vertices = kcalloc(vertex_count, sizeof(*vertices), GFP_KERNEL);
	if (!vertices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < vertex_count; i++) {
		const struct drm_gcn_texture_vertex *vertex =
			&triangles[i / 3].vertices[i % 3];

		vertices[i].x = vertex->x;
		vertices[i].y = vertex->y;
		vertices[i].s = vertex->s;
		vertices[i].t = vertex->t;
	}
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 2);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, src_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_textured(src->provider, src->allocation,
					     dst->allocation,
					     src->width, src->height,
					     dst->width, dst->height, vertices,
					     args->triangle_count, &state);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(vertices);
	kfree(triangles);
	return ret;
}

static int
gcn_drm_ioctl_draw_indexed_triangles(struct drm_device *drm,
				     void *data,
				     struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_indexed_triangles *args = data;
	struct drm_gcn_color_vertex *user_vertices = NULL;
	struct gcn_drm_color_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	u16 *user_indices = NULL;
	u8 *indices = NULL;
	unsigned int index_count;
	unsigned int i;
	bool require_opaque;
	int ret;

	(void)drm;

	ret = gcn_drm_render_validate_indexed_triangle_batch(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	user_vertices = memdup_array_user(u64_to_user_ptr(args->vertices_ptr),
					  args->vertex_count,
					  sizeof(*user_vertices));
	if (IS_ERR(user_vertices)) {
		ret = PTR_ERR(user_vertices);
		user_vertices = NULL;
		goto out_put;
	}
	index_count = args->triangle_count * 3;
	user_indices = memdup_array_user(u64_to_user_ptr(args->indices_ptr),
					 index_count, sizeof(*user_indices));
	if (IS_ERR(user_indices)) {
		ret = PTR_ERR(user_indices);
		user_indices = NULL;
		goto out_put;
	}

	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (!gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}
	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;
	require_opaque = args->state.blend_mode == DRM_GCN_BLEND_NONE;
	ret = gcn_drm_validate_indexed_triangles(user_vertices,
						 args->vertex_count, user_indices,
						 args->triangle_count, dst->width,
						 dst->height, require_opaque);
	if (ret)
		goto out_put;

	vertices = kcalloc(args->vertex_count, sizeof(*vertices), GFP_KERNEL);
	indices = kmalloc_array(index_count, sizeof(*indices), GFP_KERNEL);
	if (!vertices || !indices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < args->vertex_count; i++) {
		vertices[i].x = user_vertices[i].x;
		vertices[i].y = user_vertices[i].y;
		vertices[i].r = user_vertices[i].rgba >> 24;
		vertices[i].g = user_vertices[i].rgba >> 16;
		vertices[i].b = user_vertices[i].rgba >> 8;
		vertices[i].a = user_vertices[i].rgba;
	}
	for (i = 0; i < index_count; i++)
		indices[i] = user_indices[i];
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 1);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_indexed(dst->provider, dst->allocation,
					    dst->width, dst->height, vertices,
					    args->vertex_count, indices,
					    args->triangle_count, &state);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(indices);
	kfree(vertices);
	kfree(user_indices);
	kfree(user_vertices);
	return ret;
}

static int gcn_drm_ioctl_draw_indexed_textured(struct drm_device *drm,
					       void *data, struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_indexed_textured *args = data;
	struct drm_gcn_texture_vertex *user_vertices = NULL;
	struct gcn_drm_texture_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_bo *src;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	u16 *user_indices = NULL;
	u8 *indices = NULL;
	unsigned int index_count;
	unsigned int i;
	int ret;

	(void)drm;

	ret = gcn_drm_itex_args(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	user_vertices = memdup_array_user(u64_to_user_ptr(args->vertices_ptr),
					  args->vertex_count,
					  sizeof(*user_vertices));
	if (IS_ERR(user_vertices)) {
		ret = PTR_ERR(user_vertices);
		user_vertices = NULL;
		goto out_put;
	}
	index_count = args->triangle_count * 3;
	user_indices = memdup_array_user(u64_to_user_ptr(args->indices_ptr),
					 index_count, sizeof(*user_indices));
	if (IS_ERR(user_indices)) {
		ret = PTR_ERR(user_indices);
		user_indices = NULL;
		goto out_put;
	}

	src_gem = drm_gem_object_lookup(file, args->src_handle);
	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!src_gem || !dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (src_gem == dst_gem || !gcn_drm_is_mem1_bo(src_gem) ||
	    !gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	src = to_gcn_drm_bo(src_gem);
	dst = to_gcn_drm_bo(dst_gem);
	if (src->provider != dst->provider ||
	    src->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    src->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;
	ret = gcn_drm_itex_vertices(user_vertices,
				    args->vertex_count, user_indices,
			args->triangle_count, src->width, src->height,
			dst->width, dst->height);
	if (ret)
		goto out_put;

	vertices = kcalloc(args->vertex_count, sizeof(*vertices), GFP_KERNEL);
	indices = kmalloc_array(index_count, sizeof(*indices), GFP_KERNEL);
	if (!vertices || !indices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < args->vertex_count; i++) {
		vertices[i].x = user_vertices[i].x;
		vertices[i].y = user_vertices[i].y;
		vertices[i].s = user_vertices[i].s;
		vertices[i].t = user_vertices[i].t;
	}
	for (i = 0; i < index_count; i++)
		indices[i] = user_indices[i];
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 2);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, src_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_itex(src->provider, src->allocation,
					 dst->allocation,
			src->width, src->height, dst->width, dst->height,
			vertices, args->vertex_count, indices,
			args->triangle_count, &state);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(indices);
	kfree(vertices);
	kfree(user_indices);
	kfree(user_vertices);
	return ret;
}

static int gcn_drm_ioctl_draw_itex_depth(struct drm_device *drm, void *data,
					 struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_indexed_textured_depth *args = data;
	struct drm_gcn_texture_depth_vertex *user_vertices = NULL;
	struct gcn_drm_itex_depth_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_depth_state depth;
	struct gcn_drm_bo *src;
	struct gcn_drm_bo *dst;
	struct drm_exec exec;
	u16 *user_indices = NULL;
	u8 *indices = NULL;
	unsigned int index_count;
	unsigned int i;
	int ret;

	(void)drm;

	ret = gcn_drm_itex_z_args(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	user_vertices = memdup_array_user(u64_to_user_ptr(args->vertices_ptr),
					  args->vertex_count,
					  sizeof(*user_vertices));
	if (IS_ERR(user_vertices)) {
		ret = PTR_ERR(user_vertices);
		user_vertices = NULL;
		goto out_put;
	}
	index_count = args->triangle_count * 3;
	user_indices = memdup_array_user(u64_to_user_ptr(args->indices_ptr),
					 index_count, sizeof(*user_indices));
	if (IS_ERR(user_indices)) {
		ret = PTR_ERR(user_indices);
		user_indices = NULL;
		goto out_put;
	}

	src_gem = drm_gem_object_lookup(file, args->src_handle);
	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (!src_gem || !dst_gem) {
		ret = -ENOENT;
		goto out_put;
	}
	if (src_gem == dst_gem || !gcn_drm_is_mem1_bo(src_gem) ||
	    !gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	src = to_gcn_drm_bo(src_gem);
	dst = to_gcn_drm_bo(dst_gem);
	if (src->provider != dst->provider ||
	    src->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    src->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst->width,
						 dst->height);
	if (ret)
		goto out_put;
	ret = gcn_drm_itex_z_vertices(user_vertices, args->vertex_count,
				      user_indices, args->triangle_count,
				      src->width, src->height, dst->width,
				      dst->height);
	if (ret)
		goto out_put;

	vertices = kcalloc(args->vertex_count, sizeof(*vertices), GFP_KERNEL);
	indices = kmalloc_array(index_count, sizeof(*indices), GFP_KERNEL);
	if (!vertices || !indices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < args->vertex_count; i++) {
		vertices[i].x = user_vertices[i].x;
		vertices[i].y = user_vertices[i].y;
		vertices[i].z = user_vertices[i].z;
		vertices[i].s = user_vertices[i].s;
		vertices[i].t = user_vertices[i].t;
	}
	for (i = 0; i < index_count; i++)
		indices[i] = user_indices[i];
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;
	depth.test_enable = args->depth.test_enable;
	depth.compare = args->depth.compare;
	depth.write_enable = args->depth.write_enable;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 2);
	drm_exec_until_all_locked(&exec) {
		ret = drm_exec_prepare_obj(&exec, src_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	ret = gcn_drm_provider_draw_itex_depth(src->provider, src->allocation,
					       dst->allocation, src->width,
					       src->height, dst->width, dst->height,
					       vertices, args->vertex_count, indices,
					       args->triangle_count, &state, &depth);
	if (ret)
		goto out_exec;

	dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(indices);
	kfree(vertices);
	kfree(user_indices);
	kfree(user_vertices);
	return ret;
}

static int gcn_drm_ioctl_draw_fixed(struct drm_device *drm, void *data,
				    struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;
	struct drm_gcn_draw_indexed_fixed *args = data;
	struct drm_gcn_fixed_vertex *user_vertices = NULL;
	struct gcn_drm_fixed_vertex *vertices = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct drm_gem_object *src_gem = NULL;
	struct drm_gem_object *dst_gem = NULL;
	struct dma_fence *fence = NULL;
	struct gcn_drm_draw_state state;
	struct gcn_drm_depth_state depth;
	struct gcn_drm_bo *src = NULL;
	struct gcn_drm_bo *dst = NULL;
	struct gcn_drm_system_bo *system_dst = NULL;
	struct drm_exec exec;
	u16 *user_indices = NULL;
	u8 *indices = NULL;
	unsigned int index_count;
	unsigned int i;
	u16 dst_width;
	u16 dst_height;
	bool textured;
	int ret;

	(void)drm;

	ret = gcn_drm_fixed_args(args);
	if (ret)
		return ret;
	if (!xa_load(&render->contexts, args->ctx_id))
		return -ENOENT;

	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}

	user_vertices = memdup_array_user(u64_to_user_ptr(args->vertices_ptr),
					  args->vertex_count,
					  sizeof(*user_vertices));
	if (IS_ERR(user_vertices)) {
		ret = PTR_ERR(user_vertices);
		user_vertices = NULL;
		goto out_put;
	}
	index_count = args->triangle_count * 3;
	user_indices = memdup_array_user(u64_to_user_ptr(args->indices_ptr),
					 index_count, sizeof(*user_indices));
	if (IS_ERR(user_indices)) {
		ret = PTR_ERR(user_indices);
		user_indices = NULL;
		goto out_put;
	}

	textured = args->tev_mode != DRM_GCN_TEV_PASS_COLOR;
	dst_gem = drm_gem_object_lookup(file, args->dst_handle);
	if (textured)
		src_gem = drm_gem_object_lookup(file, args->src_handle);
	if (!dst_gem || (textured && !src_gem)) {
		ret = -ENOENT;
		goto out_put;
	}
	if (textured && !gcn_drm_is_mem1_bo(src_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	if (textured)
		src = to_gcn_drm_bo(src_gem);
	if (src && (src->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    src->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4)) {
		ret = -EINVAL;
		goto out_put;
	}
	if (gcn_drm_is_mem1_bo(dst_gem)) {
		dst = to_gcn_drm_bo(dst_gem);
		if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4 ||
		    (src && src->provider != dst->provider)) {
			ret = -EINVAL;
			goto out_put;
		}
		dst_width = dst->width;
		dst_height = dst->height;
	} else if (gcn_drm_is_system_bo(dst_gem)) {
		system_dst = to_gcn_drm_system_bo(dst_gem);
		if (system_dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
		    system_dst->layout != DRM_GCN_GEM_LAYOUT_LINEAR) {
			ret = -EINVAL;
			goto out_put;
		}
		dst_width = system_dst->width;
		dst_height = system_dst->height;
	} else {
		ret = -EINVAL;
		goto out_put;
	}
	ret = gcn_drm_render_validate_draw_state(&args->state, dst_width,
						 dst_height);
	if (ret)
		goto out_put;
	ret = gcn_drm_fixed_vertices(user_vertices, args->vertex_count,
				     user_indices, args->triangle_count,
				     args->tev_mode,
				     src ? src->width : 0,
				     src ? src->height : 0,
				     dst_width, dst_height,
				     args->state.blend_mode == DRM_GCN_BLEND_NONE);
	if (ret)
		goto out_put;

	vertices = kcalloc(args->vertex_count, sizeof(*vertices), GFP_KERNEL);
	indices = kmalloc_array(index_count, sizeof(*indices), GFP_KERNEL);
	if (!vertices || !indices) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < args->vertex_count; i++) {
		vertices[i].x = user_vertices[i].x;
		vertices[i].y = user_vertices[i].y;
		vertices[i].z = user_vertices[i].z;
		vertices[i].r = user_vertices[i].rgba >> 24;
		vertices[i].g = user_vertices[i].rgba >> 16;
		vertices[i].b = user_vertices[i].rgba >> 8;
		vertices[i].a = user_vertices[i].rgba;
		vertices[i].s = user_vertices[i].s;
		vertices[i].t = user_vertices[i].t;
	}
	for (i = 0; i < index_count; i++)
		indices[i] = user_indices[i];
	state.viewport_x = args->state.viewport_x;
	state.viewport_y = args->state.viewport_y;
	state.viewport_width = args->state.viewport_width;
	state.viewport_height = args->state.viewport_height;
	state.scissor_x = args->state.scissor_x;
	state.scissor_y = args->state.scissor_y;
	state.scissor_width = args->state.scissor_width;
	state.scissor_height = args->state.scissor_height;
	state.blend_mode = args->state.blend_mode;
	state.cull_mode = args->state.cull_mode;
	depth.test_enable = args->depth.test_enable;
	depth.compare = args->depth.compare;
	depth.write_enable = args->depth.write_enable;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, src_gem ? 2 : 1);
	drm_exec_until_all_locked(&exec) {
		if (src_gem) {
			ret = drm_exec_prepare_obj(&exec, src_gem, 1);
			drm_exec_retry_on_contention(&exec);
			if (ret)
				break;
		}
		ret = drm_exec_prepare_obj(&exec, dst_gem, 1);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}
	if (ret)
		goto out_exec;

	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence) {
		ret = -ENOMEM;
		goto out_exec;
	}

	if (system_dst) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
		const struct gcn_drm_accel_ops *provider =
			src ? src->provider : NULL;

		ret = drm_gem_shmem_vmap_locked(&system_dst->shmem, &map);
		if (ret)
			goto out_exec;
		if (map.is_iomem) {
			ret = -EINVAL;
		} else {
			ret = gcn_drm_provider_draw_fixed_system(provider,
								 src ? src->allocation : NULL,
								 map.vaddr,
								 src ? src->width : 0,
								 src ? src->height : 0,
								 dst_width, dst_height,
								 system_dst->layout, vertices,
								 args->vertex_count, indices,
								 args->triangle_count,
								 args->tev_mode, &state,
								 &depth);
		}
		drm_gem_shmem_vunmap_locked(&system_dst->shmem, &map);
	} else {
		ret = gcn_drm_provider_draw_fixed(dst->provider,
						  src ? src->allocation : NULL,
						  dst->allocation,
						  src ? src->width : 0,
						  src ? src->height : 0,
						  dst_width, dst_height, vertices,
						  args->vertex_count, indices,
						  args->triangle_count,
						  args->tev_mode, &state, &depth);
	}
	if (ret)
		goto out_exec;

	if (src_gem)
		dma_resv_add_fence(src_gem->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(dst_gem->resv, fence, DMA_RESV_USAGE_WRITE);
	if (out_syncobj)
		drm_syncobj_replace_fence(out_syncobj, fence);

out_exec:
	drm_exec_fini(&exec);
out_put:
	dma_fence_put(fence);
	if (dst_gem)
		drm_gem_object_put(dst_gem);
	if (src_gem)
		drm_gem_object_put(src_gem);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	kfree(indices);
	kfree(vertices);
	kfree(user_indices);
	kfree(user_vertices);
	return ret;
}

const struct drm_ioctl_desc gcn_drm_render_ioctls[DRM_GCN_NUM_IOCTLS] = {
	DRM_IOCTL_DEF_DRV(GCN_GET_PARAM, gcn_drm_ioctl_get_param,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_GEM_CREATE, gcn_drm_ioctl_gem_create,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_GEM_MMAP, gcn_drm_ioctl_gem_mmap,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_CTX_CREATE, gcn_drm_ioctl_ctx_create,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_CTX_FREE, gcn_drm_ioctl_ctx_free,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_WAIT, gcn_drm_ioctl_wait, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_SUBMIT, gcn_drm_ioctl_submit, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_BLIT_SCALED, gcn_drm_ioctl_blit_scaled,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_TRIANGLE, gcn_drm_ioctl_draw_triangle,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_TRIANGLES, gcn_drm_ioctl_draw_triangles,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_TRIANGLES_STATE,
			  gcn_drm_ioctl_draw_triangles_state,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_TRIANGLES_DEPTH,
			  gcn_drm_ioctl_draw_triangles_depth,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_TEXTURED_TRIANGLES,
			  gcn_drm_ioctl_draw_textured_triangles,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_INDEXED_TRIANGLES,
			  gcn_drm_ioctl_draw_indexed_triangles,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_INDEXED_TEXTURED_TRIANGLES,
			  gcn_drm_ioctl_draw_indexed_textured,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_INDEXED_TEXTURED_DEPTH,
			  gcn_drm_ioctl_draw_itex_depth,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GCN_DRAW_INDEXED_FIXED,
			  gcn_drm_ioctl_draw_fixed,
			  DRM_RENDER_ALLOW),
};

int gcn_drm_render_open(struct drm_device *drm, struct drm_file *file)
{
	struct gcn_drm_render_file *render;

	(void)drm;

	render = kzalloc(sizeof(*render), GFP_KERNEL);
	if (!render)
		return -ENOMEM;

	xa_init_flags(&render->contexts, XA_FLAGS_ALLOC1);
	file->driver_priv = render;
	return 0;
}

void gcn_drm_render_postclose(struct drm_device *drm, struct drm_file *file)
{
	struct gcn_drm_render_file *render = file->driver_priv;

	(void)drm;

	if (!render)
		return;

	xa_destroy(&render->contexts);
	kfree(render);
	file->driver_priv = NULL;
}
