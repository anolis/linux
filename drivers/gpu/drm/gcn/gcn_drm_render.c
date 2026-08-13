// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dma-resv.h>
#include <linux/dma-buf.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>

#include <uapi/drm/gcn_drm.h>

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

static inline struct gcn_drm_bo *to_gcn_drm_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct gcn_drm_bo, gem);
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

static bool gcn_drm_is_mem1_bo(const struct drm_gem_object *gem)
{
	return gem->funcs == &gcn_drm_bo_funcs;
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
	struct gcn_drm_bo *bo;
	u64 size;
	int ret;

	if (args->handle || args->size)
		return -EINVAL;
	ret = gcn_drm_render_bo_size(args, &size);
	if (ret)
		return ret;

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
	if (!gcn_drm_is_mem1_bo(gem)) {
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
	if (!gcn_drm_is_mem1_bo(gem)) {
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
	struct gcn_drm_bo *dst;
	struct gcn_drm_render_rect rect;
	struct gcn_drm_render_blit_rect blit_rect;
	struct drm_exec exec;
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
	if ((src_gem && !gcn_drm_is_mem1_bo(src_gem)) ||
	    !gcn_drm_is_mem1_bo(dst_gem)) {
		ret = -EINVAL;
		goto out_put;
	}

	dst = to_gcn_drm_bo(dst_gem);
	if (dst->format != DRM_GCN_GEM_FORMAT_RGB565 ||
	    dst->layout != DRM_GCN_GEM_LAYOUT_TILED_4X4) {
		ret = -EINVAL;
		goto out_put;
	}
	if (args->op == DRM_GCN_RENDER_OP_FILL_RECT_RGB565) {
		ret = gcn_drm_render_validate_rect(args->data, dst->width,
						   dst->height);
		if (ret)
			goto out_put;
		gcn_drm_render_decode_rect(args->data, &rect);
	}
	if (src_gem) {
		src = to_gcn_drm_bo(src_gem);
		if (src->provider != dst->provider ||
		    src->format != dst->format || src->layout != dst->layout) {
			ret = -EINVAL;
			goto out_put;
		}
		if (args->op == DRM_GCN_RENDER_OP_COPY_RGB565 &&
		    (src->width != dst->width || src->height != dst->height)) {
			ret = -EINVAL;
			goto out_put;
		}
	}
	if (args->op == DRM_GCN_RENDER_OP_BLIT_RECT_RGB565) {
		ret = gcn_drm_render_validate_blit_rect(args->data, src->width,
							src->height, dst->width,
							dst->height);
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

	if (args->op == DRM_GCN_RENDER_OP_COPY_RGB565)
		ret = gcn_drm_provider_copy(src->provider, src->allocation,
					    dst->allocation, src->width,
					    src->height);
	else if (args->op == DRM_GCN_RENDER_OP_FILL_RGB565)
		ret = gcn_drm_provider_fill(dst->provider, dst->allocation,
					    dst->width, dst->height,
					    (u16)args->data);
	else if (args->op == DRM_GCN_RENDER_OP_FILL_RECT_RGB565)
		ret = gcn_drm_provider_fill_rect(dst->provider, dst->allocation,
						 dst->width, dst->height,
						 rect.x, rect.y, rect.width,
						 rect.height, rect.color);
	else
		ret = gcn_drm_provider_blit_rect(src->provider, src->allocation,
						 dst->allocation, src->width,
						 src->height, dst->width,
						 dst->height, blit_rect.src_x,
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
