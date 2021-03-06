/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __VIVANTE_DRV_H__
#define __VIVANTE_DRV_H__

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/types.h>
#include <asm/sizes.h>


/* stubs we need for compile-test: */
static inline struct device *vivante_iommu_get_ctx(const char *ctx_name)
{
	return NULL;
}

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/vivante_drm.h>

struct vivante_gpu;
struct vivante_mmu;

#define NUM_DOMAINS 2    /* one for KMS, then one per gpu core (?) */

struct vivante_file_private {
	/* currently we don't do anything useful with this.. but when
	 * per-context address spaces are supported we'd keep track of
	 * the context's page-tables here.
	 */
	int dummy;
};

struct vivante_drm_private {
	struct vivante_gpu *gpu[VIVANTE_MAX_PIPES];
	struct vivante_file_private *lastctx;

	uint32_t next_fence, completed_fence;
	wait_queue_head_t fence_event;

	/* list of GEM objects: */
	struct list_head inactive_list;

	struct workqueue_struct *wq;

	/* callbacks deferred until bo is inactive: */
	struct list_head fence_cbs;

	/* registered MMUs: */
	unsigned int num_mmus;
	struct vivante_iommu *mmus[NUM_DOMAINS];
};

struct vivante_format {
	uint32_t pixel_format;
};

/* callback from wq once fence has passed: */
struct vivante_fence_cb {
	struct work_struct work;
	uint32_t fence;
	void (*func)(struct vivante_fence_cb *cb);
};

void __vivante_fence_worker(struct work_struct *work);

#define INIT_FENCE_CB(_cb, _func)  do {                     \
		INIT_WORK(&(_cb)->work, __vivante_fence_worker); \
		(_cb)->func = _func;                         \
	} while (0)

int vivante_register_mmu(struct drm_device *dev, struct vivante_iommu *mmu);

int vivante_wait_fence_interruptable(struct drm_device *dev, uint32_t fence,
		struct timespec *timeout);
void vivante_update_fence(struct drm_device *dev, uint32_t fence);

int vivante_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

int vivante_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int vivante_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
uint64_t vivante_gem_mmap_offset(struct drm_gem_object *obj);
int vivante_gem_get_iova_locked(struct drm_gem_object *obj, int id,
		uint32_t *iova);
int vivante_gem_get_iova(struct drm_gem_object *obj, int id, uint32_t *iova);
struct page **vivante_gem_get_pages(struct drm_gem_object *obj);
void vivante_gem_put_pages(struct drm_gem_object *obj);
void vivante_gem_put_iova(struct drm_gem_object *obj, int id);
int vivante_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int vivante_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
struct sg_table *vivante_gem_prime_get_sg_table(struct drm_gem_object *obj);
void *vivante_gem_prime_vmap(struct drm_gem_object *obj);
void vivante_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
struct drm_gem_object *vivante_gem_prime_import_sg_table(struct drm_device *dev,
		size_t size, struct sg_table *sg);
int vivante_gem_prime_pin(struct drm_gem_object *obj);
void vivante_gem_prime_unpin(struct drm_gem_object *obj);
void *vivante_gem_vaddr_locked(struct drm_gem_object *obj);
void *vivante_gem_vaddr(struct drm_gem_object *obj);
int vivante_gem_queue_inactive_cb(struct drm_gem_object *obj,
		struct vivante_fence_cb *cb);
void vivante_gem_move_to_active(struct drm_gem_object *obj,
		struct vivante_gpu *gpu, bool write, uint32_t fence);
void vivante_gem_move_to_inactive(struct drm_gem_object *obj);
int vivante_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op,
		struct timespec *timeout);
int vivante_gem_cpu_fini(struct drm_gem_object *obj);
void vivante_gem_free_object(struct drm_gem_object *obj);
int vivante_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle);
struct drm_gem_object *vivante_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags);
struct drm_gem_object *vivante_gem_import(struct drm_device *dev,
		uint32_t size, struct sg_table *sgt);

#ifdef CONFIG_DEBUG_FS
void vivante_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void vivante_gem_describe_objects(struct list_head *list, struct seq_file *m);
void vivante_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
#endif

void __iomem *vivante_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname);
void vivante_writel(u32 data, void __iomem *addr);
u32 vivante_readl(const void __iomem *addr);

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

static inline bool fence_completed(struct drm_device *dev, uint32_t fence)
{
	struct vivante_drm_private *priv = dev->dev_private;
	return priv->completed_fence >= fence;
}

static inline int align_pitch(int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* adreno needs pitch aligned to 32 pixels: */
	return bytespp * ALIGN(width, 32);
}

/* for the generated headers: */
#define INVALID_IDX(idx) ({BUG(); 0;})
#define fui(x)                ({BUG(); 0;})
#define util_float_to_half(x) ({BUG(); 0;})


#define FIELD(val, name) (((val) & name ## __MASK) >> name ## __SHIFT)

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)


#endif /* __VIVANTE_DRV_H__ */
