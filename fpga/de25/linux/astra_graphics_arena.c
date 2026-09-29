// SPDX-License-Identifier: GPL-2.0-only
/*
 * The DE25 graphics arenas.
 *
 * /dev/astra-graphics-arena is a write-combining view of the media RAM behind
 * the HPS-to-FPGA bridge. /dev/mem can only map memory outside System RAM as
 * Device-nGnRnE, so every 8-byte store to the arena waits for its bridge
 * response: a render batch copies at about 18 MB/s. Normal non-cacheable lets
 * stores merge into bursts and loads pipeline. Nothing is cached, so no cache
 * maintenance is needed; callers order arena stores before a doorbell, and
 * doorbell reads before arena loads, with a DSB, as they already do.
 *
 * /dev/astra-host-arena is the display mailbox payload: one physically
 * contiguous block of HPS memory below 4 GiB, allocated once at load and
 * shared by every opener (QEMU writes batches into it, the display helper
 * validates them). The render engine reads it in place over F2SDRAM, which
 * is not coherent with the CPU caches, so it is mapped Normal non-cacheable
 * too; a writer completes its stores with a DSB before the engine is rung.
 */
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "astra_de25_media.h"
#include "astra_host_arena_uapi.h"

static int astra_arena_mmap(struct file *file, struct vm_area_struct *vma)
{
	u64 offset = (u64)vma->vm_pgoff << PAGE_SHIFT;
	unsigned long bytes = vma->vm_end - vma->vm_start;

	if (offset >= ASTRA_DE25_MEDIA_BYTES ||
	    bytes > ASTRA_DE25_MEDIA_BYTES - offset)
		return -EINVAL;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	return remap_pfn_range(vma, vma->vm_start,
			       (ASTRA_DE25_MEDIA_BASE + offset) >> PAGE_SHIFT,
			       bytes, vma->vm_page_prot);
}

static const struct file_operations astra_arena_fops = {
	.owner = THIS_MODULE,
	.mmap = astra_arena_mmap,
};

static struct miscdevice astra_arena_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "astra-graphics-arena",
	.fops = &astra_arena_fops,
	.mode = 0600,
};

static void *astra_host_arena;
static dma_addr_t astra_host_arena_physical;

static int astra_host_arena_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct miscdevice *device = file->private_data;

	/* dma_mmap_coherent checks the range and maps with dma_pgprot, which
	   for this non-coherent device is Normal non-cacheable. */
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	return dma_mmap_coherent(device->this_device, vma, astra_host_arena,
				 astra_host_arena_physical,
				 ASTRA_HOST_ARENA_BYTES);
}

static long astra_host_arena_ioctl(struct file *file, unsigned int command,
				   unsigned long argument)
{
	struct astra_host_arena_info info = {
		.physical = astra_host_arena_physical,
		.bytes = ASTRA_HOST_ARENA_BYTES,
	};

	if (command != ASTRA_HOST_ARENA_IOC_INFO)
		return -ENOTTY;
	return copy_to_user((void __user *)argument, &info, sizeof(info)) ?
		       -EFAULT : 0;
}

static const struct file_operations astra_host_arena_fops = {
	.owner = THIS_MODULE,
	.mmap = astra_host_arena_mmap,
	.unlocked_ioctl = astra_host_arena_ioctl,
};

static struct miscdevice astra_host_arena_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "astra-host-arena",
	.fops = &astra_host_arena_fops,
	.mode = 0600,
};

static int __init astra_arenas_init(void)
{
	struct device *device;
	int result;

	result = misc_register(&astra_arena_device);
	if (result)
		return result;
	result = misc_register(&astra_host_arena_device);
	if (result)
		goto arena;
	/* No DT node: dma-direct, no IOMMU, so the DMA address is the physical
	   address F2SDRAM uses (the HPS has f2sdram_SMMU=false). The 32-bit
	   mask keeps it inside the 4 GiB F2SDRAM window and the aperture
	   register. Contiguous 8 MiB comes from CMA. */
	device = astra_host_arena_device.this_device;
	result = dma_coerce_mask_and_coherent(device, DMA_BIT_MASK(32));
	if (result)
		goto host;
	astra_host_arena = dma_alloc_coherent(device, ASTRA_HOST_ARENA_BYTES,
					      &astra_host_arena_physical,
					      GFP_KERNEL);
	if (!astra_host_arena) {
		result = -ENOMEM;
		goto host;
	}
	dev_info(device, "host arena %pad, %u bytes\n",
		 &astra_host_arena_physical, ASTRA_HOST_ARENA_BYTES);
	return 0;

host:
	misc_deregister(&astra_host_arena_device);
arena:
	misc_deregister(&astra_arena_device);
	return result;
}

static void __exit astra_arenas_exit(void)
{
	/* Every mapping holds its file, and so this module: nothing maps the
	   buffer once exit runs. */
	dma_free_coherent(astra_host_arena_device.this_device,
			  ASTRA_HOST_ARENA_BYTES, astra_host_arena,
			  astra_host_arena_physical);
	misc_deregister(&astra_host_arena_device);
	misc_deregister(&astra_arena_device);
}

module_init(astra_arenas_init);
module_exit(astra_arenas_exit);

MODULE_DESCRIPTION("Astra DE25 media RAM and host arenas");
MODULE_LICENSE("GPL");
