// SPDX-License-Identifier: GPL-2.0-only
#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include <astra/display_capture.h>

#include "astra_display_capture_uapi.h"

#define ASTRA_DE25_CAPTURE_REGISTERS 0x20105000ULL
#define ASTRA_DE25_MEDIA_BASE 0x40000000ULL
#define ASTRA_DE25_MEDIA_BYTES 0x20000000ULL
#define ASTRA_DE25_CAPTURE_FRAME \
	(ASTRA_DE25_MEDIA_BASE + ASTRA_DE25_MEDIA_BYTES - \
	 ASTRA_DISPLAY_CAPTURE_FRAME_BYTES)
#define ASTRA_DE25_DMA_CHANNELS 4U
#define ASTRA_CAPTURE_FAULT_TIMEOUT_US 2000000U
#define ASTRA_CAPTURE_MAPPING_BYTES \
	PAGE_ALIGN(ASTRA_DISPLAY_CAPTURE_FRAME_BYTES)

struct astra_display_capture {
	struct miscdevice misc;
	struct mutex ownership_lock;
	struct mutex capture_lock;
	bool opened;
	void __iomem *registers;
	struct device *dma_device;
	struct dma_chan *channels[ASTRA_DE25_DMA_CHANNELS];
	unsigned int channel_count;
	void *frame;
	dma_addr_t frame_dma;
};

static struct astra_display_capture astra_capture;

static u32 astra_capture_read(struct astra_display_capture *capture, u32 offset)
{
	return readl(capture->registers + offset);
}

static void astra_capture_write(struct astra_display_capture *capture,
				u32 offset, u32 value)
{
	writel(value, capture->registers + offset);
}

static bool astra_dma_channel_filter(struct dma_chan *channel, void *context)
{
	struct device *required_device = context;

	if (strcmp(dev_driver_string(channel->device->dev),
		   "dw_axi_dmac_platform"))
		return false;
	return !required_device || channel->device->dev == required_device;
}

static void astra_dma_complete(void *argument)
{
	complete(argument);
}

static void astra_capture_resources_release(
	struct astra_display_capture *capture)
{
	unsigned int index;

	for (index = 0; index < capture->channel_count; ++index)
		dmaengine_terminate_sync(capture->channels[index]);
	if (capture->frame) {
		dma_free_coherent(capture->dma_device,
				  ASTRA_CAPTURE_MAPPING_BYTES,
				  capture->frame, capture->frame_dma);
		capture->frame = NULL;
	}
	for (index = 0; index < capture->channel_count; ++index) {
		dma_release_channel(capture->channels[index]);
		capture->channels[index] = NULL;
	}
	capture->channel_count = 0;
	capture->dma_device = NULL;
	if (capture->registers) {
		iounmap(capture->registers);
		capture->registers = NULL;
	}
}

static int astra_capture_resources_acquire(
	struct astra_display_capture *capture)
{
	dma_cap_mask_t mask;
	struct dma_chan *channel;

	capture->registers = ioremap(ASTRA_DE25_CAPTURE_REGISTERS,
				       ASTRA_CAPTURE_REGISTER_BYTES);
	if (!capture->registers)
		return -ENOMEM;
	if (astra_capture_read(capture, ASTRA_CAPTURE_REG_DEVICE_ID) !=
			ASTRA_DISPLAY_CAPTURE_DEVICE_ID ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_VERSION) !=
			ASTRA_DISPLAY_CAPTURE_VERSION ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_FRAME_BYTES) !=
			ASTRA_DISPLAY_CAPTURE_FRAME_BYTES)
		goto no_device;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	while (capture->channel_count < ASTRA_DE25_DMA_CHANNELS) {
		channel = dma_request_channel(mask, astra_dma_channel_filter,
					      capture->dma_device);
		if (!channel)
			break;
		if (!capture->dma_device)
			capture->dma_device = channel->device->dev;
		capture->channels[capture->channel_count++] = channel;
	}
	if (!capture->channel_count)
		goto no_device;

	capture->frame = dma_alloc_coherent(capture->dma_device,
					    ASTRA_CAPTURE_MAPPING_BYTES,
					    &capture->frame_dma, GFP_KERNEL);
	if (!capture->frame)
		goto no_memory;
	memset(capture->frame, 0, ASTRA_CAPTURE_MAPPING_BYTES);
	return 0;

no_memory:
	astra_capture_resources_release(capture);
	return -ENOMEM;
no_device:
	astra_capture_resources_release(capture);
	return -ENODEV;
}

static int astra_capture_hardware(struct astra_display_capture *capture,
				  struct astra_display_capture_info *info)
{
	ktime_t started = ktime_get();
	u32 status;
	int result;

	astra_capture_write(capture, ASTRA_CAPTURE_REG_CONTROL,
			    ASTRA_CAPTURE_ENABLE | ASTRA_CAPTURE_CLEAR_COUNTERS);
	astra_capture_write(capture, ASTRA_CAPTURE_REG_BUFFER_BASE,
			    ASTRA_DE25_CAPTURE_FRAME);
	astra_capture_write(capture, ASTRA_CAPTURE_REG_CONTROL,
			    ASTRA_CAPTURE_ENABLE | ASTRA_CAPTURE_ARM);
	result = readl_poll_timeout(
		capture->registers + ASTRA_CAPTURE_REG_STATUS, status,
		(status & (ASTRA_CAPTURE_STATUS_COMPLETE |
			   ASTRA_CAPTURE_STATUS_ERROR_MASK)),
		100, ASTRA_CAPTURE_FAULT_TIMEOUT_US);
	if (result || !(status & ASTRA_CAPTURE_STATUS_COMPLETE) ||
	    (status & ASTRA_CAPTURE_STATUS_ERROR_MASK) ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_COMPLETED_BASE) !=
			ASTRA_DE25_CAPTURE_FRAME ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_COMPLETED_COUNT) != 1 ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_DROPPED_COUNT) != 0 ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_OVERFLOW_COUNT) != 0 ||
	    astra_capture_read(capture, ASTRA_CAPTURE_REG_AXI_ERROR_COUNT) != 0 ||
	    astra_capture_read(capture,
			       ASTRA_CAPTURE_REG_COMMAND_ERROR_COUNT) != 0) {
		astra_capture_write(capture, ASTRA_CAPTURE_REG_CONTROL,
				    ASTRA_CAPTURE_ACKNOWLEDGE);
		return result ?: -EIO;
	}
	info->generation = astra_capture_read(
		capture, ASTRA_CAPTURE_REG_COMPLETED_GENERATION);
	info->hardware_cycles = astra_capture_read(
		capture, ASTRA_CAPTURE_REG_LAST_CYCLES);
	info->capture_nanoseconds = ktime_to_ns(ktime_sub(ktime_get(), started));
	astra_capture_write(capture, ASTRA_CAPTURE_REG_CONTROL,
			    ASTRA_CAPTURE_ACKNOWLEDGE);
	return 0;
}

static int astra_capture_dma(struct astra_display_capture *capture,
			     struct astra_display_capture_info *info)
{
	struct dma_async_tx_descriptor *descriptors[ASTRA_DE25_DMA_CHANNELS];
	struct completion completions[ASTRA_DE25_DMA_CHANNELS];
	dma_cookie_t cookies[ASTRA_DE25_DMA_CHANNELS];
	dma_addr_t source_dma;
	size_t offset = 0;
	size_t stripe;
	ktime_t started;
	unsigned int index;
	int result;

	source_dma = dma_map_resource(
		capture->dma_device, ASTRA_DE25_CAPTURE_FRAME,
		ASTRA_DISPLAY_CAPTURE_FRAME_BYTES, DMA_TO_DEVICE, 0);
	if (dma_mapping_error(capture->dma_device, source_dma))
		return -EIO;
	stripe = ASTRA_DISPLAY_CAPTURE_FRAME_BYTES / capture->channel_count;
	for (index = 0; index < capture->channel_count; ++index) {
		size_t bytes = index + 1 == capture->channel_count ?
			ASTRA_DISPLAY_CAPTURE_FRAME_BYTES - offset : stripe;

		init_completion(&completions[index]);
		descriptors[index] = dmaengine_prep_dma_memcpy(
			capture->channels[index], capture->frame_dma + offset,
			source_dma + offset, bytes,
			DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
		if (!descriptors[index]) {
			result = -EIO;
			goto terminate;
		}
		descriptors[index]->callback = astra_dma_complete;
		descriptors[index]->callback_param = &completions[index];
		cookies[index] = dmaengine_submit(descriptors[index]);
		result = dma_submit_error(cookies[index]);
		if (result)
			goto terminate;
		offset += bytes;
	}

	started = ktime_get();
	for (index = 0; index < capture->channel_count; ++index)
		dma_async_issue_pending(capture->channels[index]);
	for (index = 0; index < capture->channel_count; ++index) {
		if (!wait_for_completion_timeout(
				&completions[index],
				usecs_to_jiffies(ASTRA_CAPTURE_FAULT_TIMEOUT_US))) {
			result = -ETIMEDOUT;
			goto terminate;
		}
		if (dma_async_is_tx_complete(capture->channels[index],
					     cookies[index], NULL, NULL) !=
					     DMA_COMPLETE) {
			result = -EIO;
			goto terminate;
		}
	}
	info->dma_channels = capture->channel_count;
	info->dma_nanoseconds = ktime_to_ns(ktime_sub(ktime_get(), started));
	dma_unmap_resource(capture->dma_device, source_dma,
			   ASTRA_DISPLAY_CAPTURE_FRAME_BYTES, DMA_TO_DEVICE, 0);
	return 0;

terminate:
	for (index = 0; index < capture->channel_count; ++index)
		dmaengine_terminate_sync(capture->channels[index]);
	dma_unmap_resource(capture->dma_device, source_dma,
			   ASTRA_DISPLAY_CAPTURE_FRAME_BYTES, DMA_TO_DEVICE, 0);
	return result;
}

static int astra_capture_open(struct inode *inode, struct file *file)
{
	struct astra_display_capture *capture = &astra_capture;
	int result;

	(void)inode;
	mutex_lock(&capture->ownership_lock);
	if (capture->opened) {
		mutex_unlock(&capture->ownership_lock);
		return -EBUSY;
	}
	capture->opened = true;
	result = astra_capture_resources_acquire(capture);
	if (result)
		capture->opened = false;
	mutex_unlock(&capture->ownership_lock);
	if (result)
		return result;
	file->private_data = capture;
	return 0;
}

static int astra_capture_release(struct inode *inode, struct file *file)
{
	struct astra_display_capture *capture = file->private_data;

	(void)inode;
	mutex_lock(&capture->ownership_lock);
	astra_capture_resources_release(capture);
	capture->opened = false;
	mutex_unlock(&capture->ownership_lock);
	return 0;
}

static long astra_capture_ioctl(struct file *file, unsigned int command,
				unsigned long argument)
{
	struct astra_display_capture *capture = file->private_data;
	struct astra_display_capture_info info = {
		.width = ASTRA_DISPLAY_CAPTURE_WIDTH,
		.height = ASTRA_DISPLAY_CAPTURE_HEIGHT,
		.stride = ASTRA_DISPLAY_CAPTURE_WIDTH *
			  ASTRA_DISPLAY_CAPTURE_PIXEL_BYTES,
		.frame_bytes = ASTRA_DISPLAY_CAPTURE_FRAME_BYTES,
	};
	int result;

	if (command != ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE)
		return -ENOTTY;
	mutex_lock(&capture->capture_lock);
	result = astra_capture_hardware(capture, &info);
	if (!result)
		result = astra_capture_dma(capture, &info);
	if (!result && copy_to_user((void __user *)argument, &info, sizeof(info)))
		result = -EFAULT;
	mutex_unlock(&capture->capture_lock);
	return result;
}

static int astra_capture_mmap(struct file *file, struct vm_area_struct *area)
{
	struct astra_display_capture *capture = file->private_data;
	size_t bytes = area->vm_end - area->vm_start;

	if (area->vm_pgoff || bytes != ASTRA_CAPTURE_MAPPING_BYTES ||
	    (area->vm_flags & VM_WRITE))
		return -EINVAL;
	return dma_mmap_coherent(capture->dma_device, area, capture->frame,
				 capture->frame_dma,
				 ASTRA_CAPTURE_MAPPING_BYTES);
}

static const struct file_operations astra_capture_operations = {
	.owner = THIS_MODULE,
	.open = astra_capture_open,
	.release = astra_capture_release,
	.unlocked_ioctl = astra_capture_ioctl,
	.compat_ioctl = astra_capture_ioctl,
	.mmap = astra_capture_mmap,
};

static int __init astra_capture_init(void)
{
	BUILD_BUG_ON(ASTRA_DISPLAY_CAPTURE_FRAME_BYTES %
		     ASTRA_DE25_DMA_CHANNELS);
	mutex_init(&astra_capture.ownership_lock);
	mutex_init(&astra_capture.capture_lock);
	astra_capture.misc.minor = MISC_DYNAMIC_MINOR;
	astra_capture.misc.name = "astra-display-capture";
	astra_capture.misc.fops = &astra_capture_operations;
	astra_capture.misc.mode = 0600;
	return misc_register(&astra_capture.misc);
}

static void __exit astra_capture_exit(void)
{
	misc_deregister(&astra_capture.misc);
}

module_init(astra_capture_init);
module_exit(astra_capture_exit);
MODULE_DESCRIPTION("Astra final composited display capture");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: dw_axi_dmac_platform");
