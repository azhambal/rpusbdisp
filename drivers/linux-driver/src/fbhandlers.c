/*
 *    RoboPeak USB LCD Display Linux Driver
 *    
 *    Copyright (C) 2009 - 2013 RoboPeak Team
 *    This file is licensed under the GPL. See LICENSE in the package.
 *
 *    http://www.robopeak.net
 *
 *    Author Shikai Chen
 *
 *   ---------------------------------------------------
 *    Frame Buffer Handlers
 */


#include "inc/common.h"
#include "inc/fbhandlers.h"
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/err.h>
#include "inc/devconf.h"
#include "inc/usbhandlers.h"


static struct fb_info *default_fb;

/* The framebuffer is created at module init, long before any USB device has
 * been probed, so there is no natural parent device to hang it on.  It still
 * needs one: on registration fbcon asks video_is_primary_device(fb->device)
 * and that helper dereferences the device without checking it for NULL, so a
 * parentless framebuffer oopses the registering task.  At boot that task is a
 * udev worker holding the console lock, and the machine hangs on the splash
 * screen whenever the panel is plugged in.  A virtual root device answers the
 * question safely: it sits on no bus, so the primary-device test says "no".
 */
static struct device *fb_parent_dev;

struct dirty_rect {
	int  left;
	int  top;
	int  right;
	int  bottom;
	atomic_t dirty_flag;
};  

enum {
	DISPLAY_UPDATE_HINT_NONE       = 0,
	DISPLAY_UPDATE_HINT_BITBLT     = 1,
	DISPLAY_UPDATE_HINT_FILLRECT   = 2,
	DISPLAY_UPDATE_HINT_COPYAREA   = 3,
};

struct rpusbdisp_fb_private {
	u32 pseudo_palette[16];
	struct dirty_rect dirty_rect;
	struct mutex      operation_lock;
	struct rpusbdisp_dev *binded_usbdev;

	/* lock free area */
	atomic_t               unsync_flag;
};

static const struct fb_fix_screeninfo vfb_fix = {
	.id =	    "rpusbdisp-fb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE,
	.line_length = RP_DISP_DEFAULT_WIDTH * RP_DISP_DEFAULT_PIXEL_BITS / 8,
};

static struct fb_var_screeninfo var_info = {
	.xres = RP_DISP_DEFAULT_WIDTH,
	.yres = RP_DISP_DEFAULT_HEIGHT,
	.xres_virtual = RP_DISP_DEFAULT_WIDTH,
	.yres_virtual = RP_DISP_DEFAULT_HEIGHT,
	.width = RP_DISP_DEFAULT_WIDTH,
	.height = RP_DISP_DEFAULT_HEIGHT,
	.bits_per_pixel =        RP_DISP_DEFAULT_PIXEL_BITS,
	.red = {0, 5, 0},
	.green = {5, 6, 0},
	.blue = {11, 5, 0},
	.activate = FB_ACTIVATE_NOW,
	.vmode = FB_VMODE_NONINTERLACED,
};

static DEFINE_MUTEX(mutex_devreg);

static inline struct rpusbdisp_fb_private *get_fb_private(struct fb_info *info)
{
	return (struct rpusbdisp_fb_private *)info->par;
}

static void clear_dirty_rect(struct dirty_rect *rect)
{
	rect->left = RP_DISP_DEFAULT_WIDTH;
	rect->right = -1;
	rect->top = RP_DISP_DEFAULT_HEIGHT;
	rect->bottom = -1;
	atomic_set(&rect->dirty_flag, 0);
}

static void reset_fb_private(struct rpusbdisp_fb_private *pa)
{
	mutex_init(&pa->operation_lock);
	clear_dirty_rect(&pa->dirty_rect);
	pa->binded_usbdev = NULL;
	atomic_set(&pa->unsync_flag, 1);
}

static void display_update(struct fb_info *p, int x, int y, int width, int height, int hint, const void *hint_data)
{
	struct rpusbdisp_fb_private *pa = get_fb_private(p);
	int clear_dirty = 0;

	mutex_lock(&pa->operation_lock);

	if (!pa->binded_usbdev)
		goto final;
	
	/* 1. update the dirty rect */
	if (atomic_dec_and_test(&pa->unsync_flag)) {
		/* force the dirty rect to cover the full display area if the display is not synced. */
		pa->dirty_rect.left = 0;
		pa->dirty_rect.right = p->var.xres - 1;
		pa->dirty_rect.top = 0;
		pa->dirty_rect.bottom = p->var.yres - 1;
		clear_dirty = 1;
	} else {
		if (pa->dirty_rect.top > y)
			pa->dirty_rect.top = y;
		if (pa->dirty_rect.bottom < height + y - 1)
			pa->dirty_rect.bottom = height + y - 1;
		if (pa->dirty_rect.left > x)
			pa->dirty_rect.left = x;
		if (pa->dirty_rect.right < width + x - 1)
			pa->dirty_rect.right = width + x - 1;
	}

	if (pa->dirty_rect.top > pa->dirty_rect.bottom || pa->dirty_rect.left > pa->dirty_rect.right)
		goto final;
	
	atomic_set(&pa->dirty_rect.dirty_flag, 1);

	/* 2. try to send it */
	if (pa->binded_usbdev) {
		switch (hint) {
		case DISPLAY_UPDATE_HINT_FILLRECT:
		{
			const struct fb_fillrect *fillrt = (const struct fb_fillrect *)hint_data;
			if (rpusbdisp_usb_try_draw_rect(pa->binded_usbdev, fillrt->dx, fillrt->dy, fillrt->dx + fillrt->width - 1, 
				fillrt->dy + fillrt->height - 1, fillrt->color, fillrt->rop == ROP_XOR ? RPUSBDISP_OPERATION_XOR : RPUSBDISP_OPERATION_COPY)) {
				/* data sent, rect the dirty rect */
				clear_dirty_rect(&pa->dirty_rect);
			}
		}
		break;
		case DISPLAY_UPDATE_HINT_COPYAREA:
		{
			const struct fb_copyarea *copyarea = (const struct fb_copyarea *)hint_data;
			if (rpusbdisp_usb_try_copy_area(pa->binded_usbdev, copyarea->sx, copyarea->sy, copyarea->dx,  copyarea->dy, 
				copyarea->width, copyarea->height)) {
				/* data sent, rect the dirty rect */
				clear_dirty_rect(&pa->dirty_rect);
			}
		}
		break;
		default:
			if (rpusbdisp_usb_try_send_image(pa->binded_usbdev, (const u16 *)p->screen_buffer,
				 pa->dirty_rect.left, pa->dirty_rect.top, pa->dirty_rect.right, pa->dirty_rect.bottom, p->fix.line_length / (RP_DISP_DEFAULT_PIXEL_BITS / 8),
				 clear_dirty)) {
				/* data sent, rect the dirty rect */
				clear_dirty_rect(&pa->dirty_rect);
			} 
		}
	}
final:
	mutex_unlock(&pa->operation_lock);
}

static void display_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	sys_fillrect(p, rect);
	display_update(p, rect->dx, rect->dy, rect->width, rect->height, DISPLAY_UPDATE_HINT_FILLRECT, rect);
}

static void display_imageblit(struct fb_info *p, const struct fb_image *image)
{
	sys_imageblit(p, image);
	display_update(p, image->dx, image->dy, image->width, image->height, DISPLAY_UPDATE_HINT_BITBLT, image);
}

static void display_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	/* Perform the copy operation */
	sys_copyarea(p, area);
	/* Update the display with the copied area */
	display_update(p, area->dx, area->dy, area->width, area->height, DISPLAY_UPDATE_HINT_COPYAREA, area);
}

static ssize_t display_write(struct fb_info *p, const char *buf __user, size_t count, loff_t *ppos)
{
	/* TODO: Review full screen update in _display_write for potential optimization. */
	int retval;

	/* Write to the framebuffer using the system's framebuffer write function */
	retval = fb_sys_write(p, buf, count, ppos);

	/* Update the entire display after writing */
	display_update(p, 0, 0, p->var.xres, p->var.yres, DISPLAY_UPDATE_HINT_NONE, NULL);
	return retval;
}

static int display_setcolreg(u_int regno, u_int red, u_int green, u_int blue, u_int transp, struct fb_info *info)
{
	#define CNVT_TOHW(val, width) ((((val) << (width)) + 0x7FFF - (val)) >> 16)
	int ret = 1;

	/* Convert to grayscale if required */
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green + 7471 * blue) >> 16;

	/* Handle different framebuffer visual types */
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			u32 value;

			/* Convert color components to hardware format */
			red = CNVT_TOHW(red, info->var.red.length);
			green = CNVT_TOHW(green, info->var.green.length);
			blue = CNVT_TOHW(blue, info->var.blue.length);
			transp = CNVT_TOHW(transp, info->var.transp.length);

			/* Combine the color components into a single value */
			value = (red << info->var.red.offset) |
				(green << info->var.green.offset) |
				(blue << info->var.blue.offset) |
				(transp << info->var.transp.offset);

			/* Update the pseudo-palette with the calculated value */
			pal[regno] = value;
			ret = 0;
		}
		break;
	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}
	return ret;
	#undef CNVT_TOHW
}

/*
 * Deferred I/O handler.
 *
 * Since kernel 6.2 the list passed here holds struct fb_deferred_io_pageref
 * entries linked through their ->list member, not struct page linked through
 * ->lru. Each pageref already carries the byte offset of the touched page
 * inside the framebuffer, so no pfn/address arithmetic is needed.
 */
static void display_defio_handler(struct fb_info *info, struct list_head *pagelist)
{
	struct fb_deferred_io_pageref *pageref;
	int top = RP_DISP_DEFAULT_HEIGHT, bottom = -1;
	int current_val;
	unsigned long offset;
	struct rpusbdisp_fb_private *pa = get_fb_private(info);

	if (!pa->binded_usbdev) /* No device bound, ignore */
		return;

	list_for_each_entry(pageref, pagelist, list) {
		offset = pageref->offset;

		if (offset >= info->fix.smem_len)
			continue;

		/* first scanline touched by this page */
		current_val = offset / info->fix.line_length;
		if (top > current_val)
			top = current_val;

		/* last scanline touched by this page */
		current_val = (offset + PAGE_SIZE - 1) / info->fix.line_length;
		if (bottom < current_val)
			bottom = current_val;
	}

	/* Adjust the bottom limit to prevent overflow */
	if (bottom >= RP_DISP_DEFAULT_HEIGHT)
		bottom = RP_DISP_DEFAULT_HEIGHT - 1;

	/* Update the display with the calculated dirty region */
	if (top <= bottom) /* Ensure valid rect */
		display_update(info, 0, top, info->var.xres, bottom - top + 1, DISPLAY_UPDATE_HINT_NONE, NULL);
}

/*
 * fb_mmap must stay fb_deferred_io_mmap: it installs the page-fault handler
 * that feeds display_defio_handler(). Mapping the vmalloc pages directly
 * would silently disable all deferred-io screen updates.
 */
static struct fb_ops display_fbops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = display_write,
	.fb_fillrect = display_fillrect,
	.fb_copyarea = display_copyarea,
	.fb_imageblit = display_imageblit,
	.fb_setcolreg = display_setcolreg,
	.fb_mmap = fb_deferred_io_mmap,
};


#if 0
static struct platform_driver rpusbdisp_fb_driver = {
	.probe = rpusbdisp_initial_probe,
	.remove = rpusbdisp_final_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "rpusbdisp-fb",
	},
};
#endif

static int on_create_new_fb(struct fb_info **out_fb, struct rpusbdisp_dev *dev)
{
	int ret = -ENOMEM;
	size_t fbmem_size = 0;
	void *fbmem = NULL;
	struct fb_info *fb;
	struct fb_deferred_io *fbdefio;
    
	*out_fb = NULL;
    
	fb = framebuffer_alloc(sizeof(struct rpusbdisp_fb_private), fb_parent_dev);
    
	if (!fb) {
		pr_err("Failed to initialize framebuffer device\n");
		goto failed;
	}

	fb->fix = vfb_fix;
	fb->var = var_info;

	fb->fbops = &display_fbops;
	/* FBINFO_DEFAULT was removed from the kernel in 6.10 and was a no-op (0) before that. */
	fb->flags = FBINFO_VIRTFB;

	fbmem_size = var_info.yres * vfb_fix.line_length;
	fbmem = vzalloc(fbmem_size);
	if (!fbmem) {
		pr_err("Failed to allocate framebuffer memory\n");
		goto failed_nofb;
	}

	fb->screen_buffer = fbmem;
	fb->fix.smem_start = (unsigned long)fbmem;
	fb->fix.smem_len = fbmem_size;
    
	fb->pseudo_palette = get_fb_private(fb)->pseudo_palette;
    
	if (fb_alloc_cmap(&fb->cmap, 256, 0)) {
		pr_err("Failed to allocate cmap\n");
		goto failed_nocmap;
	}

	fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);
	if (fbdefio) {
		/* frame rate is configurable through the fps module parameter */
		fbdefio->delay = HZ / (fps > 0 ? fps : 16);
		fbdefio->deferred_io = display_defio_handler;
	} else {
		pr_err("Failed to allocate fb_deferred_io structure\n");
		goto failed_nodefio;
	}

	fb->fbdefio = fbdefio;
	fb_deferred_io_init(fb);

	reset_fb_private(get_fb_private(fb));

	/* register the framebuffer device */
	ret = register_framebuffer(fb);
	if (ret < 0) {
		pr_err("Failed to register framebuffer device, error %d\n", ret);
		goto failed_on_reg;
	}

	*out_fb = fb;
	return ret;

failed_on_reg:
	kfree(fbdefio);
failed_nodefio:
	fb_dealloc_cmap(&fb->cmap);
failed_nocmap:
	vfree(fbmem);
failed_nofb:
	framebuffer_release(fb);
failed:
	return ret;
}

static void on_release_fb(struct fb_info *fb)
{
	if (!fb)
		return;

	/* the fb must be gone from userspace before the defio state is torn down */
	unregister_framebuffer(fb);
	fb_deferred_io_cleanup(fb);

	kfree(fb->fbdefio);
	fb_dealloc_cmap(&fb->cmap);
	vfree(fb->screen_buffer);
	framebuffer_release(fb);
}

int __init_or_module register_fb_handlers(void)
{
	int ret;

	fb_parent_dev = root_device_register("rpusbdisp");
	if (IS_ERR(fb_parent_dev)) {
		ret = PTR_ERR(fb_parent_dev);
		fb_parent_dev = NULL;
		pr_err("Failed to register the framebuffer parent device, error %d\n", ret);
		return ret;
	}

	ret = on_create_new_fb(&default_fb, NULL);
	if (ret < 0) {
		root_device_unregister(fb_parent_dev);
		fb_parent_dev = NULL;
	}

	return ret;
}

void unregister_fb_handlers(void)
{
	on_release_fb(default_fb);

	if (fb_parent_dev) {
		root_device_unregister(fb_parent_dev);
		fb_parent_dev = NULL;
	}
}

void fbhandler_on_all_transfer_done(struct rpusbdisp_dev *dev)
{
	/* we have a chance to flush pending modifications to the display */
	struct fb_info *fb = (struct fb_info *)rpusbdisp_usb_get_fbhandle(dev);
	struct rpusbdisp_fb_private *fb_pri;
    
	if (!fb)
		return;

	fb_pri = get_fb_private(fb);
    
	if (atomic_read(&fb_pri->dirty_rect.dirty_flag) || atomic_read(&fb_pri->unsync_flag) == 1)
		display_update(fb, 0, 0, RP_DISP_DEFAULT_WIDTH, RP_DISP_DEFAULT_HEIGHT, DISPLAY_UPDATE_HINT_NONE, NULL);
}

int fbhandler_on_new_device(struct rpusbdisp_dev *dev)
{
	int ans = -1;
	struct rpusbdisp_fb_private *fb_pri;
    
	mutex_lock(&mutex_devreg);

	/* check whether the default fb has been binded */
	fb_pri = get_fb_private(default_fb);
	if (!fb_pri->binded_usbdev) {
		mutex_lock(&fb_pri->operation_lock);

		/* bind to the default framebuffer ( the only one) */
		fb_pri->binded_usbdev = dev;
		rpusbdisp_usb_set_fbhandle(dev, default_fb);

		ans = 0; 
		mutex_unlock(&fb_pri->operation_lock);
	}

	mutex_unlock(&mutex_devreg);
	return ans;
}

void fbhandler_on_remove_device(struct rpusbdisp_dev *dev) 
{
	struct fb_info *fb = (struct fb_info *)rpusbdisp_usb_get_fbhandle(dev);

	/* Acquire the mutex for safe device removal */
	mutex_lock(&mutex_devreg);

	if (fb) {
		struct rpusbdisp_fb_private *fb_pri = get_fb_private(default_fb);

		/* Acquire the operation lock */
		mutex_lock(&fb_pri->operation_lock);

		/* Unbind the device */
		fb_pri->binded_usbdev = NULL;
		rpusbdisp_usb_set_fbhandle(dev, NULL);

		mutex_unlock(&fb_pri->operation_lock);

		/* Unregister the framebuffer if it is not the default one */
		if (fb != default_fb)
			on_release_fb(fb);
	}
	mutex_unlock(&mutex_devreg);
}

void fbhandler_set_unsync_flag(struct rpusbdisp_dev *dev) 
{
	struct fb_info *fb = (struct fb_info *)rpusbdisp_usb_get_fbhandle(dev);

	if (fb) {
		struct rpusbdisp_fb_private *fb_pri = get_fb_private(default_fb);

		atomic_set(&fb_pri->unsync_flag, 1);
	}
}
