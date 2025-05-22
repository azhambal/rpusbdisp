/*
 *    RoboPeak USB LCD Display Linux Driver
 *    
 *    Copyright (C) 2009 - 2013 RoboPeak Team
 *    This file is licensed under the GPL. See LICENSE in the package.
 *
 *    http://www.robopeak.net
 *
 *    Author Shikai Chen
 *   -----------------------------------------------------------------
 *    USB Driver Implementations
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h> /* For cpu_to_le16 etc. */
#include <linux/wait.h> /* For wait_queue_head_t */
#include <linux/sched.h> /* For schedule_timeout, TASK_UNINTERRUPTIBLE */
#include <linux/delay.h> /* For msleep (if needed, though schedule_timeout is used) */
#include <linux/workqueue.h> /* For struct delayed_work */
#include <linux/align.h> /* For ALIGN macro */
#include "inc/usbhandlers.h"
#include "inc/fbhandlers.h"
#include "inc/touchhandlers.h"

/* #define DL_ALIGN_UP(x, a) ALIGN(x, a) // Replaced by standard ALIGN */
/* #define DL_ALIGN_DOWN(x, a) ALIGN(x, a) // Replaced by standard ALIGN */


static const struct usb_device_id id_table[] = {
	{ 
		.idVendor    = RP_DISP_USB_VENDOR_ID, 
		.idProduct   = RP_DISP_USB_PRODUCT_ID,
		.match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT, 
	},
	{ /* Terminating entry */ },
};

static atomic_t devlist_count = ATOMIC_INIT(0);
static LIST_HEAD(rpusbdisp_list);
static DEFINE_MUTEX(mutex_usbdevlist);
static DECLARE_WAIT_QUEUE_HEAD(usblist_waitqueue);

#if 0
static struct task_struct * usb_status_polling_task;  
static int kthread_usb_status_poller_proc(void *data);
#endif

static volatile int working_flag = 0;


#define RPUSBDISP_STATUS_BUFFER_SIZE   32


struct rpusbdisp_disp_ticket_bundle {
	int  ticket_count;
	struct list_head   ticket_list;
};

struct rpusbdisp_disp_ticket {
	struct urb      *transfer_urb;
	struct list_head   ticket_list_node;
	struct rpusbdisp_dev *binded_dev;
};


struct rpusbdisp_disp_ticket_pool {
	struct list_head   list;
	spinlock_t     oplock;
	size_t         disp_urb_count;
	size_t         packet_size_factor;
	int            availiable_count;
	wait_queue_head_t  wait_queue;
	struct delayed_work    completion_work;
};  

struct rpusbdisp_dev {
	/* timing and sync */
	struct list_head   dev_list_node;
	int            dev_id;
	struct mutex       op_locker;
	u8             is_alive;

	/* usb device info */
	struct usb_device   *udev;
	struct usb_interface *interface;
    
	/* status package related */
	u8         status_in_buffer[RPUSBDISP_STATUS_BUFFER_SIZE]; /* data buffer for the status IN endpoint */
	size_t         status_in_buffer_recvsize;
    
	u8         status_in_ep_addr;
	wait_queue_head_t  status_wait_queue;
	struct urb      *urb_status_query;
	int            urb_status_fail_count;

	/* display data related */
	u8         disp_out_ep_addr;
	size_t         disp_out_ep_max_size;

	struct rpusbdisp_disp_ticket_pool disp_tickets_pool;

	void          *fb_handle;
	void          *touch_handle;

	u16            device_fwver;
};

/*
 * The usb_class_driver lcd_class and related character device registration
 * code (usb_register_dev, disp_fops) have been removed as the device
 * will primarily interact via framebuffer and input subsystems.
 */

static int return_disp_tickets(struct rpusbdisp_dev *dev, struct rpusbdisp_disp_ticket *ticket);


struct device *rpusbdisp_usb_get_devicehandle(struct rpusbdisp_dev *dev)
{
	return &dev->udev->dev;
}

void rpusbdisp_usb_set_fbhandle(struct rpusbdisp_dev *dev, void *fbhandle)
{
	dev->fb_handle = fbhandle;
}
 
void *rpusbdisp_usb_get_fbhandle(struct rpusbdisp_dev *dev)
{
	return dev->fb_handle;
}

void rpusbdisp_usb_set_touchhandle(struct rpusbdisp_dev *dev, void *touch_handle)
{
	dev->touch_handle = touch_handle;
}

void *rpusbdisp_usb_get_touchhandle(struct rpusbdisp_dev *dev)
{
	return dev->touch_handle;
}


static void status_start_querying(struct rpusbdisp_dev *dev);


static void add_usbdev_to_list(struct rpusbdisp_dev *dev)
{
	mutex_lock(&mutex_usbdevlist);
	list_add(&dev->dev_list_node, &rpusbdisp_list);
	atomic_inc(&devlist_count);
	mutex_unlock(&mutex_usbdevlist);

	wake_up(&usblist_waitqueue);
}


static void del_usbdev_from_list(struct rpusbdisp_dev *dev)
{
	mutex_lock(&mutex_usbdevlist);
	list_del(&dev->dev_list_node);
	atomic_dec(&devlist_count);
	mutex_unlock(&mutex_usbdevlist);
}


static void on_parse_status_packet(struct rpusbdisp_dev *dev)
{
	rpusbdisp_status_packet_header_t *header = (rpusbdisp_status_packet_header_t *)dev->status_in_buffer;

	if (header->packet_type == RPUSBDISP_STATUS_TYPE_NORMAL) {
		/* only supports the normal status packet currently */
		rpusbdisp_status_normal_packet_t *normalpacket = (rpusbdisp_status_normal_packet_t *)header;
        
		if (normalpacket->display_status & RPUSBDISP_DISPLAY_STATUS_DIRTY_FLAG) {
			fbhandler_set_unsync_flag(dev);
			schedule_delayed_work(&dev->disp_tickets_pool.completion_work, 0);
		}
    
		touchhandler_send_ts_event(dev, le32_to_cpu(normalpacket->touch_x), le32_to_cpu(normalpacket->touch_y),  normalpacket->touch_status == RPUSBDISP_TOUCH_STATUS_PRESSED ? 1 : 0);
	}
}

static void on_display_transfer_finished_delaywork(struct work_struct *work)
{
	struct rpusbdisp_dev *dev = container_of(work, struct rpusbdisp_dev,
					      disp_tickets_pool.completion_work.work);
   
	if (!dev->is_alive)
		return;
	fbhandler_on_all_transfer_done(dev);
}

static void on_display_transfer_finished(struct urb *urb)
{
	struct rpusbdisp_disp_ticket *ticket = (struct rpusbdisp_disp_ticket *)urb->context;
	struct rpusbdisp_dev *dev = ticket->binded_dev;
	int all_finished = 0;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (dev->is_alive) {
			/* set unsync flag */
			fbhandler_set_unsync_flag(dev);
		}
		dev_err(&dev->interface->dev, "transmission failed for urb %p, error code %x\n", urb, urb->status);
	} 

	urb->transfer_buffer_length = dev->disp_tickets_pool.packet_size_factor * dev->disp_out_ep_max_size; /* reset buffer size */

	/* insert to the available queue */
	all_finished = return_disp_tickets(dev, ticket);

	if (all_finished && !urb->status)
		schedule_delayed_work(&dev->disp_tickets_pool.completion_work, 0);
}

static void on_status_query_finished(struct urb *urb)
{
	struct rpusbdisp_dev *dev = urb->context;

	if (!dev->is_alive)
		return;

	/* check the status... */
	switch (urb->status) {
	case 0:
		/* succeed */
		/* store the actual transfer size */
		dev->status_in_buffer_recvsize = urb->actual_length;
            
		on_parse_status_packet(dev);
		/* notify the waiters.. */
		wake_up(&dev->status_wait_queue);
		break;
	case -EPIPE:
		usb_clear_halt(dev->udev, usb_rcvintpipe(dev->udev, dev->status_in_ep_addr));
		/* Fall-through to default to increment fail count and potentially retry */
	default:
		/* Keeping original logic to set to max on error, but after clearing halt. */
		dev->urb_status_fail_count = RPUSBDISP_STATUS_QUERY_RETRY_COUNT;
	}
    
	if (dev->urb_status_fail_count < RPUSBDISP_STATUS_QUERY_RETRY_COUNT) {
		status_start_querying(dev);
	} else {
		dev_warn_once(&dev->interface->dev, "Status URB query failed after %d retries, last status %d\n", RPUSBDISP_STATUS_QUERY_RETRY_COUNT, urb->status);
	}
}


static void status_start_querying(struct rpusbdisp_dev *dev)
{
	unsigned int pipe;
	int status;
	struct usb_host_endpoint *ep;
	
	if (!dev->is_alive) {
		/* the device is dead, abort */
		return;
	}

	if (dev->urb_status_fail_count >= RPUSBDISP_STATUS_QUERY_RETRY_COUNT) {
		/* max retry count has reached, return */
		return;
	}

	pipe = usb_rcvintpipe(dev->udev, dev->status_in_ep_addr);
	ep = usb_pipe_endpoint(dev->udev, pipe);

	if (!ep) {
		dev_warn_once(&dev->interface->dev, "Failed to get endpoint for status URB\n");
		return;
	}

	usb_fill_int_urb(dev->urb_status_query, dev->udev, pipe, dev->status_in_buffer, RPUSBDISP_STATUS_BUFFER_SIZE,
			on_status_query_finished, dev,
			ep->desc.bInterval);
    
	/*submit it*/
	status = usb_submit_urb(dev->urb_status_query, GFP_ATOMIC);
	if (status) {
		if (status == -EPIPE)
			usb_clear_halt(dev->udev, pipe);
		if (status != -EPIPE)
			dev_warn_once(&dev->interface->dev, "Failed to submit status URB, status %d\n", status);
		++dev->urb_status_fail_count;
	}
}


static int sell_disp_tickets(struct rpusbdisp_dev *dev, struct rpusbdisp_disp_ticket_bundle *bundle, size_t required_count)
{
	unsigned long irq_flags;
	int ans = 0;
	struct list_head *node;

	/* do not sell tickets when the device has been closed */
	if (!dev->is_alive)
		return 0;
	if (required_count == 0) {
		pr_warn("required for zero ?!\n");
		return 0;
	}

	spin_lock_irqsave(&dev->disp_tickets_pool.oplock, irq_flags);
	do {
		if (required_count > dev->disp_tickets_pool.availiable_count) {
			/* no enough tickets availiable */
			break;
		}

		dev->disp_tickets_pool.availiable_count -= required_count;
        
		bundle->ticket_count = required_count;
		ans = bundle->ticket_count;
        
		INIT_LIST_HEAD(&bundle->ticket_list);
		while (required_count--) {
			node = dev->disp_tickets_pool.list.next;
			list_del_init(node);
			list_add_tail(node, &bundle->ticket_list);
		}
	} while (0);
	spin_unlock_irqrestore(&dev->disp_tickets_pool.oplock, irq_flags);
	return ans;
}


static int return_disp_tickets(struct rpusbdisp_dev *dev,  struct  rpusbdisp_disp_ticket *ticket)
{
	int all_finished = 0;
	unsigned long  irq_flags;

	/* insert to the available queue */
	spin_lock_irqsave(&dev->disp_tickets_pool.oplock, irq_flags);
	list_add_tail(&ticket->ticket_list_node, &dev->disp_tickets_pool.list);

	if (++dev->disp_tickets_pool.availiable_count == dev->disp_tickets_pool.disp_urb_count)
		all_finished = 1;   
	
	spin_unlock_irqrestore(&dev->disp_tickets_pool.oplock, irq_flags);

	wake_up(&dev->disp_tickets_pool.wait_queue);
	return all_finished;
}


int rpusbdisp_usb_try_copy_area(struct rpusbdisp_dev *dev, int sx, int sy, int dx, int dy, int width, int height)
{
	struct rpusbdisp_disp_ticket_bundle bundle;
	struct rpusbdisp_disp_ticket *ticket;
	rpusbdisp_disp_copyarea_packet_t *cmd_copyarea;

	/* only one ticket is enough */
	if (!sell_disp_tickets(dev, &bundle, 1)) {
		/* tickets is inadequate, try next time */
		return 0;
	}

	BUG_ON(sizeof(rpusbdisp_disp_copyarea_packet_t) > dev->disp_out_ep_max_size);

	ticket = list_entry(bundle.ticket_list.next, struct rpusbdisp_disp_ticket, ticket_list_node);
    
	cmd_copyarea = (rpusbdisp_disp_copyarea_packet_t *)ticket->transfer_urb->transfer_buffer;
	cmd_copyarea->header.cmd_flag = RPUSBDISP_DISPCMD_COPY_AREA | RPUSBDISP_CMD_FLAG_START;

	cmd_copyarea->sx = cpu_to_le16(sx);
	cmd_copyarea->sy = cpu_to_le16(sy);
	cmd_copyarea->dx = cpu_to_le16(dx);
	cmd_copyarea->dy = cpu_to_le16(dy);

	cmd_copyarea->width = cpu_to_le16(width);
	cmd_copyarea->height = cpu_to_le16(height);

	/* Add one more byte to bypass a known firmware bug in usbdisp 1.03. */
	/* The firmware might ignore the last byte if the size is exactly matching certain internal buffers. */
	ticket->transfer_urb->transfer_buffer_length = sizeof(rpusbdisp_disp_copyarea_packet_t) + 1; 

	if (usb_submit_urb(ticket->transfer_urb, GFP_KERNEL)) {
		/* submit failure, */
		on_display_transfer_finished(ticket->transfer_urb);
		return 0;
	}
    
	return 1;
}

int rpusbdisp_usb_try_draw_rect(struct rpusbdisp_dev *dev, int x, int y, int right, int bottom, u16 color, int operation)
{
	rpusbdisp_disp_fillrect_packet_t *cmd_fillrect;
	struct rpusbdisp_disp_ticket_bundle bundle;
	struct rpusbdisp_disp_ticket *ticket;

	/* only one ticket is enough */
	if (!sell_disp_tickets(dev, &bundle, 1)) {
		/* tickets is inadequate, try next time */
		return 0;
	}

	BUG_ON(sizeof(rpusbdisp_disp_fillrect_packet_t) > dev->disp_out_ep_max_size);

	ticket = list_entry(bundle.ticket_list.next, struct rpusbdisp_disp_ticket, ticket_list_node);
    
	cmd_fillrect = (rpusbdisp_disp_fillrect_packet_t *)ticket->transfer_urb->transfer_buffer;

	cmd_fillrect->header.cmd_flag = RPUSBDISP_DISPCMD_RECT | RPUSBDISP_CMD_FLAG_START;

	cmd_fillrect->left = cpu_to_le16(x);
	cmd_fillrect->top = cpu_to_le16(y);
	cmd_fillrect->right = cpu_to_le16(right);
	cmd_fillrect->bottom = cpu_to_le16(bottom);

	cmd_fillrect->color_565 = cpu_to_le16(color);
	cmd_fillrect->operation = operation;

	ticket->transfer_urb->transfer_buffer_length = sizeof(rpusbdisp_disp_fillrect_packet_t);

	if (usb_submit_urb(ticket->transfer_urb, GFP_KERNEL)) {
		/* submit failure, */
		on_display_transfer_finished(ticket->transfer_urb);
		return 0;
	}
    
	return 1;
}


/* context used by the bitblt display packet encoder */
struct bitblt_encoding_context_t {
	struct rpusbdisp_disp_ticket_bundle bundle;
	struct rpusbdisp_disp_ticket *ticket;
	struct list_head *current_node;

	size_t  encoded_pos;
	size_t  packet_pos;
	u8     *urbbuffer;
	int     rlemode;
};


static int bitblt_encoder_init(struct bitblt_encoding_context_t *ctx, struct rpusbdisp_dev *dev, size_t image_size, int rlemode)
{
	size_t effective_payload_per_packet_size;
	size_t packet_count; /* Number of physical USB packets (max endpoint size) */
	size_t required_tickets_count; /* Number of URBs (tickets) needed */

	/* Calculate total payload size, including the main bitblt command header. */
	/* This is the raw image data plus the overhead of the bitblt command itself, */
	/* minus the generic packet header that will be part of each physical packet. */
	size_t payload_without_header_size = sizeof(rpusbdisp_disp_bitblt_packet_t) - sizeof(rpusbdisp_disp_packet_header_t)
						+ image_size;

	if (rlemode) {
		/* the worst case for RLE is 1 extra byte with each 128 pixels (since the 7bit size block can represent 128 uints) */
		payload_without_header_size += (((image_size >> 1) + 0x7f) >> 7);
	}   

	/* Calculate how many physical USB packets (of max endpoint size) are needed for the payload. */
	/* Each physical packet has a small header (rpusbdisp_disp_packet_header_t). */
	effective_payload_per_packet_size = dev->disp_out_ep_max_size - sizeof(rpusbdisp_disp_packet_header_t);
    
	packet_count = (payload_without_header_size + effective_payload_per_packet_size - 1) / effective_payload_per_packet_size;
    
	/* Calculate how many URBs (tickets) are needed. Each ticket can contain 'packet_size_factor' physical packets. */
	/* packet_size_factor is (RPUSBDISP_MAX_TRANSFER_SIZE / dev->disp_out_ep_max_size). */
	required_tickets_count = (packet_count + dev->disp_tickets_pool.packet_size_factor - 1) / dev->disp_tickets_pool.packet_size_factor;
    
	if (required_tickets_count > RPUSBDISP_MAX_TRANSFER_TICKETS_COUNT) {
		dev_err(&dev->interface->dev, "required_tickets_count (%zu)>RPUSBDISP_MAX_TRANSFER_TICKETS_COUNT(%d)\n", required_tickets_count, RPUSBDISP_MAX_TRANSFER_TICKETS_COUNT);
		return 0;
	}
    
	if (!sell_disp_tickets(dev, &ctx->bundle, required_tickets_count)) {
		/* tickets is inadequate, try next time */
		return 0;
	}

	/* init the context... */
	ctx->packet_pos = 0;
	ctx->current_node = ctx->bundle.ticket_list.next;
	ctx->ticket = list_entry(ctx->current_node, struct rpusbdisp_disp_ticket, ticket_list_node);
	ctx->urbbuffer = (u8 *)ctx->ticket->transfer_urb->transfer_buffer;
	ctx->encoded_pos = 0;
	ctx->rlemode = rlemode;
	return 1;
}


static void bitblt_encode_command_header(struct bitblt_encoding_context_t *ctx, struct rpusbdisp_dev *dev, int x, int y, int right, int bottom, int clear_dirty)
{
	rpusbdisp_disp_bitblt_packet_t *bitblt_header;

	/* encoding the command header... */
	bitblt_header = (rpusbdisp_disp_bitblt_packet_t *)ctx->urbbuffer;
	bitblt_header->header.cmd_flag = ctx->rlemode ? RPUSBDISP_DISPCMD_BITBLT_RLE : RPUSBDISP_DISPCMD_BITBLT;
	bitblt_header->header.cmd_flag |= RPUSBDISP_CMD_FLAG_START;
	if (clear_dirty)
		bitblt_header->header.cmd_flag |= RPUSBDISP_CMD_FLAG_CLEARDITY;

	bitblt_header->x = cpu_to_le16(x);
	bitblt_header->y = cpu_to_le16(y);
	bitblt_header->width = cpu_to_le16(right + 1 - x);
	bitblt_header->height = cpu_to_le16(bottom + 1 - y);
	bitblt_header->operation = RPUSBDISP_OPERATION_COPY;
        
	ctx->encoded_pos = sizeof(rpusbdisp_disp_bitblt_packet_t);
}


static void bitblt_encoder_cleanup(struct bitblt_encoding_context_t *ctx, struct rpusbdisp_dev *dev)
{
	struct rpusbdisp_disp_ticket *ticket;

	/* return unused tickets */
	while (ctx->current_node != &ctx->bundle.ticket_list) {
		ticket = list_entry(ctx->current_node, struct rpusbdisp_disp_ticket, ticket_list_node);
		ctx->current_node = ctx->current_node->next;
		return_disp_tickets(dev, ticket);
	}
}

static int bitblt_encoder_flush(struct bitblt_encoding_context_t *ctx, struct rpusbdisp_dev *dev)
{
	/* submit the final ticket */
	size_t transfer_size = ctx->packet_pos * dev->disp_out_ep_max_size + ctx->encoded_pos;
    
	if (transfer_size) {
		ctx->ticket->transfer_urb->transfer_buffer_length = transfer_size;
		ctx->current_node = ctx->current_node->next;
		if (usb_submit_urb(ctx->ticket->transfer_urb, GFP_KERNEL)) {
			/* submit failure, */
			on_display_transfer_finished(ctx->ticket->transfer_urb);
			return 0; /*abort*/
		}
	}

	bitblt_encoder_cleanup(ctx, dev);
	return 1;
}


static int bitblt_encode_n_transfer_data(struct bitblt_encoding_context_t *ctx, struct rpusbdisp_dev *dev, const void *data, size_t count)
{
	const u8 *payload_in_bytes = (const u8 *)data;

	while (count) {
		/* `ctx->encoded_pos` is the current offset within the current USB packet buffer segment. */
		/* `dev->disp_out_ep_max_size` is the max size of one USB packet segment. */
		/* `ctx->urbbuffer` points to the start of the current USB packet segment. */
		size_t buffer_avail_length = dev->disp_out_ep_max_size - ctx->encoded_pos;
		size_t size_to_copy = count > buffer_avail_length ? buffer_avail_length : count;

		memcpy(ctx->urbbuffer + ctx->encoded_pos, payload_in_bytes, size_to_copy);
		payload_in_bytes += size_to_copy;
		ctx->encoded_pos += size_to_copy;
		count -= size_to_copy;

		/* If the current USB packet segment is full: */
		if (buffer_avail_length == size_to_copy) {
			/* Move to the next packet segment within the current URB (ticket). */
			ctx->urbbuffer += dev->disp_out_ep_max_size;
			ctx->packet_pos++; /* Increment count of packets filled in this URB. */
            
			/* If the current URB (ticket) is full (all its packet segments are used): */
			if (ctx->packet_pos >= dev->disp_tickets_pool.packet_size_factor) {
				/* Submit the current URB. */
				ctx->current_node = ctx->current_node->next; /* Move to the next ticket in the bundle */
				if (usb_submit_urb(ctx->ticket->transfer_urb, GFP_KERNEL)) {
					on_display_transfer_finished(ctx->ticket->transfer_urb); /* Handle error and release ticket */
					return 0; /* Abort operation */
				}
                
				/* Reset for the new URB (ticket). */
				ctx->packet_pos = 0;
				BUG_ON(ctx->current_node == &ctx->bundle.ticket_list); /* Should not run out of tickets */
				ctx->ticket = list_entry(ctx->current_node, struct rpusbdisp_disp_ticket, ticket_list_node);
				ctx->urbbuffer = (u8 *)ctx->ticket->transfer_urb->transfer_buffer; /* Point to start of new URB's buffer */
			}

			/* For the new packet segment (either in the same URB or a new one), */
			/* set up its header and reset encoded_pos. */
			/* The first byte is reserved for potential future use (e.g., sequence numbers). */
			*ctx->urbbuffer = 0; 
			((rpusbdisp_disp_packet_header_t *)ctx->urbbuffer)->cmd_flag = ctx->rlemode ? RPUSBDISP_DISPCMD_BITBLT_RLE : RPUSBDISP_DISPCMD_BITBLT;
			ctx->encoded_pos = sizeof(rpusbdisp_disp_packet_header_t);
		}
	}
	return 1;
}

struct rle_encoder_context {
	struct bitblt_encoding_context_t *encoder_ctx;
	int    is_common_section;
	size_t section_size;
	u16   section_data[128];
};


static void rle_compress_init(struct rle_encoder_context *rle_ctx, struct bitblt_encoding_context_t *encoder_ctx, struct rpusbdisp_dev *dev)
{
	rle_ctx->encoder_ctx = encoder_ctx;
	rle_ctx->is_common_section = 0;
	rle_ctx->section_size = 0;
}


static int rle_flush_section(struct rle_encoder_context *rle_ctx, struct rpusbdisp_dev *dev)
{
	/* flush the current section ... */
	u8 section_header;
	size_t section_data_len;

	BUG_ON(rle_ctx->section_size > RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT + 1);

	if (rle_ctx->section_size == 0)
		return 1; /* Do not flush an empty section. */

	/* The RLE header byte: */
	/* Lower 7 bits: (section_size - 1). Max size is 128 (0x7F + 1). */
	/* Highest bit (0x80): Set if this is a "common" section (repeated data). */
	section_header = ((rle_ctx->section_size - 1) & RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT);
	if (rle_ctx->is_common_section)
		section_header |= RPUSBDISP_RLE_BLOCKFLAG_COMMON_BIT;

	/* Write the RLE section header. */
	if (!bitblt_encode_n_transfer_data(rle_ctx->encoder_ctx, dev, &section_header, sizeof(u8)))
		return 0; /* Abort if data transfer fails. */
                
	/* For common sections, write only one pixel value. */
	/* For raw sections, write all pixel values in the section. */
	section_data_len = rle_ctx->is_common_section ? 1 : rle_ctx->section_size;
	if (!bitblt_encode_n_transfer_data(rle_ctx->encoder_ctx, dev, rle_ctx->section_data, sizeof(u16) * section_data_len))
		return 0; /* Abort if data transfer fails. */
                
	/* Reset section state for the next block. */
	rle_ctx->is_common_section = 0;
	rle_ctx->section_size = 0;
	return 1;
}

static int rle_compress_n_encode(struct rle_encoder_context *rle_ctx, struct rpusbdisp_dev *dev, u16 pixel)
{
#if RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT + 1 > 128
#error "RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT + 1 > 128"
#endif
	/* Current section is a "common" (repeated pixel) section. */
	if (rle_ctx->is_common_section) {
		BUG_ON(rle_ctx->section_size == 0);

		/* If the new pixel is different from the repeated pixel, or section is full. */
		if (pixel != rle_ctx->section_data[0] || rle_ctx->section_size == (RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT + 1)) {
			if (!rle_flush_section(rle_ctx, dev)) /* Flush the current common section. */
				return 0;
			/* Start a new raw section with the current pixel. */
			rle_ctx->is_common_section = 0; /* Switch to raw section mode */
			rle_ctx->section_size = 1;
			rle_ctx->section_data[0] = pixel;
		} else {
			/* Pixel is the same, just increment the count for the common section. */
			++rle_ctx->section_size;
		}
	} else { /* Current section is a "raw" (different pixels) section. */
		/* If the current pixel is the same as the last one in the raw section, */
		/* it's an opportunity to start a common section. */
		if (rle_ctx->section_size > 0 && pixel == rle_ctx->section_data[rle_ctx->section_size - 1]) {
			/* Flush the current raw section (excluding the last pixel, which will start the common block). */
			--rle_ctx->section_size;
			if (rle_ctx->section_size > 0) { /* Only flush if there's something to flush */
				if (!rle_flush_section(rle_ctx, dev))
					return 0;
			}
			/* Start a new common section with the current pixel (which is same as previous). */
			rle_ctx->is_common_section = 1;
			rle_ctx->section_size = 2; /* Count includes the previous pixel and current one. */
			rle_ctx->section_data[0] = pixel;
		} else { /* Pixel is different from the last, or section is empty. */
			/* If the raw section is full, flush it. */
			if (rle_ctx->section_size == (RPUSBDISP_RLE_BLOCKFLAG_SIZE_BIT + 1)) {
				if (!rle_flush_section(rle_ctx, dev))
					return 0;
			}
			/* Add the new pixel to the current raw section. */
			rle_ctx->section_data[rle_ctx->section_size++] = pixel;
		}
	}
	return 1;
}


int rpusbdisp_usb_try_send_image(struct rpusbdisp_dev *dev, const u16 *framebuffer, int x, int y, int right, int bottom, int line_width, int clear_dirty)
{
	struct bitblt_encoding_context_t encoder_ctx;
	struct rle_encoder_context rle_ctx;
	int last_copied_x, last_copied_y; 
	int rlemode;

	/* estimate how many tickets are needed */
	const size_t image_size = (right - x + 1) * (bottom - y + 1) * (RP_DISP_DEFAULT_PIXEL_BITS / 8);

	/* do not transmit zero size image */
	if (!image_size)
		return 1;

	if (dev->device_fwver >= RP_DISP_FEATURE_RLE_FWVERSION)
		rlemode = 1;
	else
		rlemode = 0;
    
	if (!bitblt_encoder_init(&encoder_ctx, dev, image_size, rlemode))
		return 0;

	if (rlemode)
		rle_compress_init(&rle_ctx, &encoder_ctx, dev);

	bitblt_encode_command_header(&encoder_ctx, dev, x, y, right, bottom, clear_dirty);

	/* locate to the begining... */
	framebuffer += (y * line_width + x);
    
	for (last_copied_y = y; last_copied_y <= bottom; ++last_copied_y) {
		for (last_copied_x = x; last_copied_x <= right; ++last_copied_x) {
			u16 current_pixel_le = cpu_to_le16(*framebuffer);
            
#if (RP_DISP_DEFAULT_PIXEL_BITS/8) != 2
    #error "only 16bit u16 type is supported"
#endif  
			if (rlemode) {
				if (!rle_compress_n_encode(&rle_ctx, dev, current_pixel_le)) {
					bitblt_encoder_cleanup(&encoder_ctx, dev);
					return 0;
				}
			} else {
				if (!bitblt_encode_n_transfer_data(&encoder_ctx, dev, &current_pixel_le, sizeof(u16))) {
					/* abort the operation... */
					bitblt_encoder_cleanup(&encoder_ctx, dev);
					return 0;
				}
			}
			++framebuffer;
		}
		framebuffer += line_width - right - 1 + x;
	}
    
	if (rlemode) {
		if (!rle_flush_section(&rle_ctx, dev)) {
			bitblt_encoder_cleanup(&encoder_ctx, dev);
			return 0;
		}            
	}

	return bitblt_encoder_flush(&encoder_ctx, dev);
}

static void on_release_disp_tickets_pool(struct rpusbdisp_dev *dev)
{
	unsigned long irq_flags;
	struct rpusbdisp_disp_ticket *ticket;
	struct list_head *node;
	int tickets_count = dev->disp_tickets_pool.disp_urb_count;
	DEFINE_WAIT(wait);
    
	dev_info(&dev->interface->dev, "waiting for all tickets to be finished...\n");

	while (tickets_count) {
		spin_lock_irqsave(&dev->disp_tickets_pool.oplock, irq_flags);
		if (dev->disp_tickets_pool.availiable_count) {
			--dev->disp_tickets_pool.availiable_count;
		} else {
			spin_unlock_irqrestore(&dev->disp_tickets_pool.oplock, irq_flags);
			prepare_to_wait(&dev->disp_tickets_pool.wait_queue, &wait, TASK_UNINTERRUPTIBLE);
			schedule_timeout(2 * HZ);
			finish_wait(&dev->disp_tickets_pool.wait_queue, &wait);
			continue;
		}
		node = dev->disp_tickets_pool.list.next;
		list_del_init(node);
		spin_unlock_irqrestore(&dev->disp_tickets_pool.oplock, irq_flags);

		ticket = list_entry(node, struct rpusbdisp_disp_ticket, ticket_list_node);
      
		usb_free_coherent(ticket->transfer_urb->dev, RPUSBDISP_MAX_TRANSFER_SIZE,
				  ticket->transfer_urb->transfer_buffer, ticket->transfer_urb->transfer_dma);

		usb_free_urb(ticket->transfer_urb);
		kfree(ticket);
		--tickets_count;
	}
}

static int on_alloc_disp_tickets_pool(struct rpusbdisp_dev *dev)
{
	struct rpusbdisp_disp_ticket *newborn;
	int actual_allocated = 0;
	u8 *transfer_buffer;
	size_t packet_size_factor;
	size_t ticket_logic_size;

	packet_size_factor = (RPUSBDISP_MAX_TRANSFER_SIZE / dev->disp_out_ep_max_size);
	ticket_logic_size = packet_size_factor * dev->disp_out_ep_max_size;
    
	spin_lock_init(&dev->disp_tickets_pool.oplock);
	INIT_LIST_HEAD(&dev->disp_tickets_pool.list);

	while (actual_allocated < RPUSBDISP_MAX_TRANSFER_TICKETS_COUNT) {
		newborn = kzalloc(sizeof(struct rpusbdisp_disp_ticket), GFP_KERNEL);
		if (!newborn) {
			dev_err(&dev->interface->dev, "Failed to allocate display ticket structure\n");
			break;
		}
        
		newborn->transfer_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!newborn->transfer_urb) {
			dev_err(&dev->interface->dev, "Failed to allocate display URB\n");
			kfree(newborn);
			break;
		}
        
		transfer_buffer = usb_alloc_coherent(dev->udev, RPUSBDISP_MAX_TRANSFER_SIZE, GFP_KERNEL, &newborn->transfer_urb->transfer_dma);
		if (!transfer_buffer) {
			dev_err(&dev->interface->dev, "Failed to allocate display URB buffer\n");
			usb_free_urb(newborn->transfer_urb);
			kfree(newborn);
			break;
		}
        
		/* setup urb */
		usb_fill_bulk_urb(newborn->transfer_urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->disp_out_ep_addr), 
				transfer_buffer, ticket_logic_size, on_display_transfer_finished, newborn);
		newborn->transfer_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
        
		newborn->binded_dev = dev;
		dev_info(&dev->interface->dev, "allocated ticket %p with urb %p\n", newborn, newborn->transfer_urb);
		list_add_tail(&newborn->ticket_list_node, &dev->disp_tickets_pool.list);
		++actual_allocated;
	}

	INIT_DELAYED_WORK(&dev->disp_tickets_pool.completion_work, on_display_transfer_finished_delaywork);

	init_waitqueue_head(&dev->disp_tickets_pool.wait_queue);
	dev->disp_tickets_pool.disp_urb_count = actual_allocated;
	dev->disp_tickets_pool.availiable_count = actual_allocated;
	dev->disp_tickets_pool.packet_size_factor = packet_size_factor;
	dev_info(&dev->interface->dev, "allocated %d urb tickets for transfering display data. %lu size each\n", actual_allocated, ticket_logic_size);

	return actual_allocated ? 0 : -ENOMEM;
}

static int on_new_usb_device(struct rpusbdisp_dev *dev)
{
	/* the rp-usb-display device has been verified */
	mutex_init(&dev->op_locker);
	init_waitqueue_head(&dev->status_wait_queue);
	
	dev->urb_status_query = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb_status_query) {
		dev_err(&dev->interface->dev, "Cannot allocate status query URB\n");
		goto status_urb_alloc_fail;
	}

	if (on_alloc_disp_tickets_pool(dev) < 0) {
		/* Error already logged in on_alloc_disp_tickets_pool if partial allocation occurred */
		dev_err(&dev->interface->dev, "Cannot allocate display tickets pool\n");
		goto disp_tickets_alloc_fail;
	}
   
	add_usbdev_to_list(dev);
	dev->dev_id = rpusbdisp_usb_get_device_count();
	dev->is_alive = 1;
	dev->device_fwver = le16_to_cpu(dev->udev->descriptor.bcdDevice);

	dev_info(&dev->interface->dev, "RP USB Display found (#%d), Firmware Version: %d.%02d, S/N: %s\n", 
				dev->dev_id, 
				(dev->device_fwver >> 8),
				(dev->device_fwver & 0xFF),
				dev->udev->serial ? dev->udev->serial : "(unknown)");

	/* start status querying... */
	status_start_querying(dev);

	fbhandler_on_new_device(dev);
	touchhandler_on_new_device(dev);

	/* force all the image to be flush */
	fbhandler_set_unsync_flag(dev);
	schedule_delayed_work(&dev->disp_tickets_pool.completion_work, 0);
	return 0;

disp_tickets_alloc_fail:
	usb_free_urb(dev->urb_status_query);
status_urb_alloc_fail:
	return -ENOMEM;
}


static void on_del_usb_device(struct rpusbdisp_dev *dev)
{
	mutex_lock(&dev->op_locker);
	dev->is_alive = 0;
	mutex_unlock(&dev->op_locker);
    
	touchhandler_on_remove_device(dev);
	fbhandler_on_remove_device(dev);

	/* kill all pending urbs */
	usb_kill_urb(dev->urb_status_query);
	cancel_delayed_work_sync(&dev->disp_tickets_pool.completion_work);
    
	del_usbdev_from_list(dev);
    
	on_release_disp_tickets_pool(dev);
   
	usb_free_urb(dev->urb_status_query);
	dev->urb_status_query = NULL;
     
	dev_info(&dev->interface->dev, "RP USB Display (#%d) now disconnected\n", dev->dev_id);
}


int rpusbdisp_usb_get_device_count(void)
{
	return atomic_read(&devlist_count);
}


static int rpusbdisp_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct rpusbdisp_dev *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory");
		goto error;
	}
	
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	if (le16_to_cpu(dev->udev->descriptor.bcdDevice) > 0x0200)
		dev_warn(&interface->dev, "The device you used may requires a newer driver version to work.\n");
	
	/* check for endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->status_in_ep_addr &&
		    usb_endpoint_is_int_in(endpoint)) {
			/* the status input endpoint has been found */
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			dev->status_in_ep_addr = endpoint->bEndpointAddress;
		}

		if (!dev->disp_out_ep_addr &&
		    usb_endpoint_is_bulk_out(endpoint) && endpoint->wMaxPacketSize) {
			/* endpoint for video output has been found */
			dev->disp_out_ep_addr = endpoint->bEndpointAddress;
			dev->disp_out_ep_max_size = le16_to_cpu(endpoint->wMaxPacketSize);
		}
	}

	if (!(dev->status_in_ep_addr && dev->disp_out_ep_addr)) {
		dev_err(&interface->dev, "Could not find the required endpoints\n");
		retval = -ENODEV; 
		goto error_put_dev;
	}
    
	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* add the device to the list */
	if (on_new_usb_device(dev)) {
		dev_err(&interface->dev, "Failed to set up new USB device structure during probe\n");
		/* on_new_usb_device has its own internal cleanup for most things it allocates.
		 * If it returns an error, it means something went wrong there.
		 * We have already set intfdata, so we need to clear it.
		 * We have already got udev, so we need to put it.
		 */
		retval = -ENODEV; /* Consider using the actual error from on_new_usb_device if available and appropriate */
		goto error_clear_intfdata; 
	}
 
	/* Character device registration (usb_register_dev) was removed. */
	/* Device nodes will be created by framebuffer and input subsystems later. */

	return 0;

error_clear_intfdata:
	usb_set_intfdata(interface, NULL);
error_put_dev:
	if (dev && dev->udev) { 
		usb_put_dev(dev->udev);
	}
error: /* This label is primarily for kfree(dev) if dev itself was allocated */
	if (dev) {
		kfree(dev);
	}
	return retval;
}


#if 0
static void lcd_draw_down(struct usb_lcd *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
}
#endif

static int rpusbdisp_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct rpusbdisp_dev *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;

	dev_info(&intf->dev, "Suspending device (state: %u)...\n", message.event);

	/* Stop new URB submissions and processing */
	mutex_lock(&dev->op_locker);
	dev->is_alive = 0; /* This flag is checked in URB completion handlers and submission paths */
	mutex_unlock(&dev->op_locker);

	/* Kill the status URB to stop status queries */
	if (dev->urb_status_query) /* Check if URB was allocated */
		usb_kill_urb(dev->urb_status_query);

	/* Cancel any pending work that might submit new display URBs */
	cancel_delayed_work_sync(&dev->disp_tickets_pool.completion_work);
    
    /* Note: Active display URBs will complete or be unlinked.
     * Their completion handlers will see dev->is_alive = 0 and should not resubmit.
     * This avoids complex iteration and killing of display URBs here,
     * relying on them to drain naturally or error out.
     */

	return 0;
}


static int rpusbdisp_resume(struct usb_interface *intf)
{
	struct rpusbdisp_dev *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;

	dev_info(&intf->dev, "Resuming device...\n");

	mutex_lock(&dev->op_locker);
	dev->is_alive = 1;
	dev->urb_status_fail_count = 0; /* Reset error count for status URB */
	mutex_unlock(&dev->op_locker);

	/* Restart status querying */
	status_start_querying(dev);

	/* Mark framebuffer as needing full update and trigger it */
	if (dev->fb_handle) { /* Ensure fb_handle is valid */
		fbhandler_set_unsync_flag(dev);
		/* Schedule the work that eventually calls fbhandler_on_all_transfer_done */
		schedule_delayed_work(&dev->disp_tickets_pool.completion_work, HZ / 10); /* e.g., 100ms delay */
	}

	return 0;
}

static void rpusbdisp_disconnect(struct usb_interface *interface)
{
	struct rpusbdisp_dev *dev;
  
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL); /* Clear before kfree */

	if (dev) { 
		on_del_usb_device(dev); /* This handles internal cleanup of dev's resources */
		if (dev->udev) {
			usb_put_dev(dev->udev); /* Dereference the USB device */
		}
		kfree(dev);    /* Free the driver's private structure */
	}
}


static struct usb_driver usbdisp_driver = {
	.name       = RP_DISP_DRIVER_NAME,
	.probe      = rpusbdisp_probe,
	.disconnect = rpusbdisp_disconnect,
	.suspend    = rpusbdisp_suspend,
	.resume     = rpusbdisp_resume,
	.id_table   = id_table,
	.supports_autosuspend = 0, /* TODO: Consider enabling autosuspend if device supports it */
};

int __init register_usb_handlers(void)
{
	/* create the status polling task */
	working_flag = 1;
#if 0
	usb_status_polling_task = kthread_run(kthread_usb_status_poller_proc, NULL, "rpusbdisp_worker%d", 0);  
	if (IS_ERR(usb_status_polling_task)) {  
		pr_err("Cannot create the kernel worker thread!\n");
		return PTR_ERR(usb_status_polling_task);
	}  
#endif
	return usb_register(&usbdisp_driver);
}


void unregister_usb_handlers(void)
{
	/* cancel the worker thread */
	working_flag = 0;
	wake_up(&usblist_waitqueue);
#if 0
	if (usb_status_polling_task && !IS_ERR(usb_status_polling_task))  
		kthread_stop(usb_status_polling_task);  
#endif

	usb_deregister(&usbdisp_driver);
}


#if 0

static int kthread_usb_status_poller_proc(void *data) 
{
	struct list_head *pos, *q;

	while (working_flag) {
		if (rpusbdisp_usb_get_device_count() < 1) {
			/* nothing to do , sleep... */
			wait_event_interruptible(usblist_waitqueue, working_flag == 0 || rpusbdisp_usb_get_device_count() >= 1);
			if (working_flag == 0)
				break;
			continue;
		}

		/* send usb msg to query the status */
		list_for_each_safe(pos, q, &rpusbdisp_list) {
			struct rpusbdisp_dev *current_dev;

			current_dev = list_entry(pos, struct rpusbdisp_dev, dev_list_node);
            
			/* send urbs */
		}
	}
	return 0;
}
#endif
