/*
 * Intel Poulsbo USB Client Controller Driver
 * Copyright (C) 2006-07, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


/* Define DEBUG to enable per-transfer logging. */
//#define	VERBOSE	/* extra debug messages (success too) */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* Power management */
#include <linux/pm.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

#include "iusbc.h"

#define	DRIVER_DESC	"Intel Poulsbo USB Client Controller Driver"
#define	DRIVER_VERSION	"2.0.0.32L.0010-reader2"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

static const char driver_name [] = "iusbc";
static const char driver_desc [] = DRIVER_DESC;

static const char *const ep_name [] = {
	"ep0-in", "ep0-out",
	"ep1in-bulk", "ep1out-bulk",
	"ep2in-int", "ep2out-int",
	"ep3in-iso", "ep3out-iso",
};


/* module parameter */

/* force_fullspeed -- device will be forced to full speed operation
 * default value: 0 for high speed
 */
static int force_fullspeed = 0;

/* modprobe iusbc force_fullspeed=n" etc */
/* XXX: remove this feature due to HW full-speed bug
 * module_param (force_fullspeed, bool, S_IRUGO);
 */

//#define RNDIS_INIT_HW_BUG
//#define DMA_DISABLED

/*-------------------------------------------------------------------------*/
/* DEBUGGING */
#define xprintk(dev,level,fmt,args...) \
	printk(level "%s %s: " fmt , driver_name , \
			pci_name(dev->pdev) , ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */


#ifdef VERBOSE
#define VDEBUG DEBUG
#else
#define VDEBUG(dev,fmt,args...) \
	do { } while (0)
#endif	/* VERBOSE */


#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define IUSBC_WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)


#ifdef VERBOSE
static inline void print_all_registers(struct iusbc_regs *regs)
{
	unsigned	i, j;
	/* device */
	printk(KERN_DEBUG "----Intel USB-C Memory Space Registers----\n");
	printk(KERN_DEBUG "Register Length: 0x%x\n",
			sizeof(struct iusbc_regs));
	printk(KERN_DEBUG "gcap=0x%08x\n", readl(&regs->gcap));
	printk(KERN_DEBUG "dev_sts=0x%08x\n", readl(&regs->dev_sts));
	printk(KERN_DEBUG "frame=0x%04x\n", readw(&regs->frame));
	printk(KERN_DEBUG "int_sts=0x%08x\n", readl(&regs->int_sts));
	printk(KERN_DEBUG "int_ctrl=0x%08x\n", readl(&regs->int_ctrl));
	printk(KERN_DEBUG "dev_ctrl=0x%08x\n", readl(&regs->dev_ctrl));

	/* endpoints */
	for (i = 0; i < 5; i++) {
		printk(KERN_DEBUG "ep[%d]_base_low_32=0x%08x\n",
				i, readl(&regs->ep[i].ep_base_low_32));
		printk(KERN_DEBUG "ep[%d]_base_hi_32=0x%08x\n",
				i, readl(&regs->ep[i].ep_base_hi_32));
		printk(KERN_DEBUG "ep[%d]_len=0x%04x\n",
				i, readw(&regs->ep[i].ep_len));
		printk(KERN_DEBUG "ep[%d]_pib=0x%04x\n",
				i, readw(&regs->ep[i].ep_pib));
		printk(KERN_DEBUG "ep[%d]_dil=0x%04x\n",
				i, readw(&regs->ep[i].ep_dil));
		printk(KERN_DEBUG "ep[%d]_tiq=0x%04x\n",
				i, readw(&regs->ep[i].ep_tiq));
		printk(KERN_DEBUG "ep[%d]_max=0x%04x\n",
				i, readw(&regs->ep[i].ep_max));
		printk(KERN_DEBUG "ep[%d]_sts=0x%04x\n",
				i, readw(&regs->ep[i].ep_sts));
		printk(KERN_DEBUG "ep[%d]_cfg=0x%04x\n",
				i, readw(&regs->ep[i].ep_cfg));

		if (1 == i) {	/* ep0-out */
			printk(KERN_DEBUG "ep-out setup_pkt_sts=0x%02x\n",
					readb(&regs->ep[i].setup_pkt_sts));
			for (j = 0; j< 8; j++) {
				printk(KERN_DEBUG "ep0-out "
					"setup_pkt[%d]=0x%02x\n",
					j, readb(&regs->ep[i].setup_pkt[j]));
			}
		}
	}
}

#endif /* VERBOSE */

/*-------------------------------------------------------------------------*/

#define	DIR_STRING(bAddress) (((bAddress) & USB_DIR_IN) ? "in" : "out")


#if defined(CONFIG_USB_GADGET_DEBUG_FILES) || defined (DEBUG)
static __maybe_unused char *type_string(u8 bmAttributes)
{
	switch ((bmAttributes) & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		return "bulk";
	case USB_ENDPOINT_XFER_ISOC:
		return "iso";
	case USB_ENDPOINT_XFER_INT:
		return "int";
	};

	return "control";
}
#endif

/*-------------------------------------------------------------------------*/

/* configure endpoint, making it usable */	
static int
iusbc_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct iusbc		*dev;
	struct iusbc_ep		*ep;
	u16			val_16, max;
	u8			val_8;
	unsigned long		flags;
	unsigned		i;
	int			retval;

	ep = container_of(_ep, struct iusbc_ep, ep);

	DEBUG(ep->dev, "---> iusbc_ep_enable() \n");

	if (!_ep || !desc || !strcmp(_ep->name, "ep0-in")
			|| !strcmp(_ep->name, "ep0-out")
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* wMaxPacketSize up to 1024 bytes */
	max = le16_to_cpu(desc->wMaxPacketSize) & 0x3ff;

	spin_lock_irqsave(&dev->lock, flags);
	ep->ep.maxpacket = max;
	if (!ep->desc)
		ep->desc = desc;

	/* ep_reset() has already been called */
	ep->stopped = 0;
	ep->is_in = (USB_DIR_IN & desc->bEndpointAddress) != 0;

	/* sanity check type, direction, address */
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		if ((dev->gadget.speed == USB_SPEED_HIGH
					&& max != 512)
				|| (dev->gadget.speed == USB_SPEED_FULL
					&& max > 64)) {
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		if (strstr (ep->ep.name, "-iso")) /* bulk is ok */
			goto done;

		switch (dev->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
			fallthrough;
		case USB_SPEED_FULL:
			if (max <= 64)
				break;
			fallthrough;
		default:
			if (max <= 8)
				break;
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (strstr (ep->ep.name, "-bulk")
				|| strstr (ep->ep.name, "-int"))
			goto done;

		switch (dev->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
			fallthrough;
		case USB_SPEED_FULL:
			if (max <= 1023)
				break;
		default:
			goto done;
		}
		break;
	default:
		goto done;
	}

	/* ep_type */
	ep->ep_type = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);

	/* DMA modes, only support Linear Mode now */
	if (ep->dev->sg_mode_dma) {
		ep->dma_mode = SCATTER_GATHER_MODE;
	} else {
		ep->dma_mode = LINEAR_MODE;
	}

	/* reset ep registers to default value */
	i = ep->num;
	writew(0, &dev->regs->ep[i].ep_cfg);

	/* set endpoints valid */
	val_16 = readw(&dev->regs->ep[i].ep_cfg);
	val_16 |= INTR_BAD_PID_TYPE
		| INTR_CRC_ERROR
		| INTR_FIFO_ERROR
		| INTR_DMA_ERROR
		| INTR_TRANS_COMPLETE
		/* | INTR_PING_NAK_SENT */
		| INTR_DMA_IOC
		| ep->dma_mode << 6
		| ep->ep_type << 4 
		/* | EP_ENABLE */
		| EP_VALID;

	/* will set EP_ENABLE later to start dma */
	writew(val_16, &dev->regs->ep[i].ep_cfg);

	val_16 = readw(&dev->regs->ep[i].ep_cfg);
	VDEBUG(dev, "%s.ep_cfg = 0x%04x\n", _ep->name, val_16);

	val_8 = desc->bEndpointAddress;
	DEBUG(dev, "enabled %s (ep%d%s-%s), max %04x, dma_mode: %02x\n",
			_ep->name,
			val_8 & USB_ENDPOINT_NUMBER_MASK,
			DIR_STRING(val_8),
			type_string(desc->bmAttributes),
			max,
			ep->dma_mode);

	retval = 0;
done:
	spin_unlock_irqrestore(&dev->lock, flags);
	DEBUG(ep->dev, "<--- iusbc_ep_enable() \n");
	return retval;
}


static const struct usb_ep_ops iusbc_ep_ops;

static void ep_reset(struct iusbc_regs __iomem *regs, struct iusbc_ep *ep)
{
	unsigned	i = ep->num;

	/* reset ep values */
	ep->stopped = 1;
	usb_ep_set_maxpacket_limit(&ep->ep, i < 2 ? 64 : (i < 4 ? 512 : 1024));
        ep->ep.caps.type_control = i < 2;
        ep->ep.caps.type_bulk = i == 2 || i == 3;
        ep->ep.caps.type_int = i == 4;
        ep->ep.caps.dir_in = !(i & 1);
        ep->ep.caps.dir_out = !!(i & 1);
	ep->ep.ops = &iusbc_ep_ops;

	/* reset ep registers to default value
	 * clear all interrupt and status
	 * clear and reset all DMA FIFOs and state machine
	 * hardware shall minimize power usage
	 */
	writew(0, &regs->ep[i].ep_cfg);
}


static void nuke(struct iusbc_ep *);

/* endpoint is no longer usable */
static int
iusbc_ep_disable(struct usb_ep *_ep)
{
	struct iusbc_ep		*ep;
	unsigned long		flags;
	struct iusbc		*dev;
	unsigned		i;
	u16			val_16;

	ep = container_of(_ep, struct iusbc_ep, ep);

	VDEBUG(ep->dev, "---> iusbc_ep_disable() \n");

	if (!_ep || !ep->desc
		|| !strcmp(_ep->name, "ep0-in")
		|| !strcmp(_ep->name, "ep0-out"))
		return -EINVAL;

	dev = ep->dev;
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	spin_lock_irqsave(&ep->dev->lock, flags);
	nuke(ep);
	ep_reset(ep->dev->regs, ep);

	i = ep->num;

	/* display endpoint configuration register */
	val_16 = readw(&dev->regs->ep[i].ep_cfg);
	VDEBUG(dev, "%s.ep_cfg = 0x%04x\n", _ep->name, val_16);

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DEBUG(ep->dev, "disabled %s\n", _ep->name);

	VDEBUG(ep->dev, "<--- iusbc_ep_disable() \n");

	return 0;
}

/*-------------------------------------------------------------------------*/

/* allocate a request object to use with this endpoint */
static struct usb_request *
iusbc_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct iusbc_ep		*ep;
	struct iusbc_request	*req;
	struct iusbc_dma	*td;

	if (!_ep)
		return NULL;
	ep = container_of(_ep, struct iusbc_ep, ep);

	VDEBUG(ep->dev, "---> iusbc_alloc_request() \n");

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);

	/* this dma descriptor may be swapped with the previous dummy */
	td = dma_pool_alloc(ep->dev->requests,
			gfp_flags,
			&req->td_dma);

	if (!td) {
		kfree(req);
		return NULL;
	}

	td->dmacount = 0;	/* not VALID */
	td->dmaaddr = __constant_cpu_to_le32(DMA_ADDR_INVALID);

	req->td = td;

	VDEBUG(ep->dev, "alloc request for %s\n", _ep->name);

	VDEBUG(ep->dev, "<--- iusbc_alloc_request() \n");

	return &req->req;
}


/* frees a request object */
static void
iusbc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct iusbc_ep		*ep;
	struct iusbc_request	*req;

	ep = container_of(_ep, struct iusbc_ep, ep);

	VDEBUG(ep->dev, "---> iusbc_free_request() \n");

	if (!_ep || !_req)
		return;

	req = container_of(_req, struct iusbc_request, req);

	WARN_ON(!list_empty(&req->queue));

	if (req->td)
		dma_pool_free(ep->dev->requests, req->td, req->td_dma);

	kfree(req);

	VDEBUG(ep->dev, "free request for %s\n", _ep->name);

	VDEBUG(ep->dev, "<--- iusbc_free_request() \n");
}

/*-------------------------------------------------------------------------*/

/* fill out dma descriptor to match a given request */
static void
fill_dma(struct iusbc_ep *ep, struct iusbc_request *req)
{
	struct iusbc_dma	*td = req->td;
	u16			dmacount;

	VDEBUG(ep->dev, "---> fill_dma() \n");

	dmacount = req->req.length;

	td->dmaaddr = cpu_to_le32(req->req.dma);
	td->dmacount = cpu_to_le16(dmacount);

	VDEBUG(ep->dev, "<--- fill_dma() \n");
}


static void start_dma(struct iusbc_ep *ep, struct iusbc_request *req)
{
	u16		val_16;		
	u32		val_32;
	unsigned	i;

	VDEBUG(ep->dev, "---> start_dma() \n");

	i = ep->num;

	/* init req->td, pointing to the current dummy */
	fill_dma(ep, req);

	/* ep_base_low_32 */
	writel(cpu_to_le32(req->req.dma),
			&ep->dev->regs->ep[i].ep_base_low_32);
	val_32 = readl(&ep->dev->regs->ep[i].ep_base_low_32);
	VDEBUG(ep->dev, "%s.ep_base_low_32=0x%08x\n",
			ep->ep.name, val_32);

	/* ep_base_hi_32 */
	writel(0, &ep->dev->regs->ep[i].ep_base_hi_32);
	val_32 = readl(&ep->dev->regs->ep[i].ep_base_hi_32);
	VDEBUG(ep->dev, "%s.ep_base_hi_32=0x%08x\n",
			ep->ep.name, val_32);

	writew(le16_to_cpu(req->td->dmacount), &ep->dev->regs->ep[i].ep_len);
	val_16 = readw(&ep->dev->regs->ep[i].ep_len);
	VDEBUG(ep->dev, "%s.ep_len=0x%04x\n",
			ep->ep.name, val_16);

	/* endpoint maximum transaction size, up to 1024 Bytes */
	writew((ep->ep.maxpacket & 0x3ff),
			&ep->dev->regs->ep[i].ep_max);
	val_16 = readw(&ep->dev->regs->ep[i].ep_max);
	VDEBUG(ep->dev, "%s.ep_max=0x%04x\n",
			ep->ep.name, val_16);

	/* validate endpoint, enable DMA */
	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	val_16 |= EP_VALID | EP_ENABLE;
	writew(val_16, &ep->dev->regs->ep[i].ep_cfg);

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	VDEBUG(ep->dev, "enable %s DMA transfer...\n",
			ep->ep.name);
	VDEBUG(ep->dev, "%s.ep_cfg = 0x%04x\n",
			ep->ep.name, val_16);

	VDEBUG(ep->dev, "<--- start_dma() \n");
}


/* queues I/O requests in endpoint queue  */
static inline void
queue_dma(struct iusbc_ep *ep, struct iusbc_request *req)
{
	struct iusbc_dma	*end;
	dma_addr_t		tmp_dma_addr;

	VDEBUG(ep->dev, "---> queue_dma() \n");

	/* swap new dummy for old, link; fill and maybe activate */
	end = ep->dummy;
	ep->dummy = req->td;
	req->td = end;

	tmp_dma_addr = ep->td_dma;
	ep->td_dma = req->td_dma;
	req->td_dma = tmp_dma_addr;

	fill_dma(ep, req);

	VDEBUG(ep->dev, "<--- queue_dma() \n");
}


static void
done(struct iusbc_ep *ep, struct iusbc_request *req, int status)
{
	struct iusbc		*dev;
	unsigned		stopped = ep->stopped;

	VDEBUG(ep->dev, "---> done() \n");

	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;

	if (req->mapped) {
		dma_unmap_single(&dev->pdev->dev, req->req.dma, req->req.length ? req->req.length : 1,
			ep->is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	}

	if (status != -ESHUTDOWN)
		DEBUG(dev, "complete %s, req %p, stat %d, len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;

	/* XXX WORKAROUND: first ep0-out OUT packet HW BUG */
#ifdef RNDIS_INIT_HW_BUG
	if (ep->num == 1) {
		char *buf;
		const u8 remote_ndis_initialize_msg[24] = {
			0x02, 0x00, 0x00, 0x00,
			0x18, 0x00, 0x00, 0x00,
			0x02, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x04, 0x00, 0x00
		};

		buf = req->req.buf;

		/* req->buf haven't been DMAed for hardware bug? */
		if ((buf[0] == 0x09) && (buf[1] == 0x02)) {
			memcpy(buf, remote_ndis_initialize_msg, 24);
			DEBUG(ep->dev, "WORKAROUND HW BUG: ep0-out OUT\n");
		}
	}
#endif
	/* XXX */

	spin_unlock(&dev->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&dev->lock);
	ep->stopped = stopped;

	VDEBUG(ep->dev, "<--- done() \n");
}

/*-------------------------------------------------------------------------*/

/* queues (submits) an I/O requests to an endpoint */
static int
iusbc_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct iusbc_request	*req;
	struct iusbc_ep		*ep;
	struct iusbc		*dev;
	unsigned long		flags;
	u16			val_16;
	unsigned		zlflag = 0;

	/* always require a cpu-view buffer */
	req = container_of(_req, struct iusbc_request, req);
	ep = container_of(_ep, struct iusbc_ep, ep);
	dev = ep->dev;
	VDEBUG(ep->dev, "---> iusbc_queue() \n");

	/* done() temporarily stops DMA while invoking the completion callback.
	 * Functions may queue their next transfer from that callback. Accept it
	 * here; the kickstart test below leaves DMA paused until done() returns
	 * and the interrupt handler restarts the queue. Disabled endpoints and
	 * disconnected devices are rejected by the normal checks below.
	 */

	if (!_req || !_req->complete || !_req->buf
			|| !list_empty(&req->queue)){
		VDEBUG(ep->dev, "_req=%p, complete=%p, buf=%p, list_empty=%d\n",
				_req, _req->complete,
				_req->buf,
				list_empty(&req->queue));
		return -EINVAL;
	}

	if (!_ep || (!ep->desc && ep->num > 1)){
		VDEBUG(ep->dev, "ep->desc=%p, ep->num=%d\n",
				ep->desc, ep->num);
		return -ESHUTDOWN;
	}

	if (dev->ep0state == EP0_DISCONNECT)
		return -ESHUTDOWN;
	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	/* can't touch registers when suspended */
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	/* set up dma mapping in case the caller didn't */
	if (_req->dma == DMA_ADDR_INVALID) {
		/* WORKAROUND: WARN_ON(size == 0) */
		if (_req->length == 0) {
			VDEBUG(dev, "req->length: 0->1\n");
			zlflag = 1;
			_req->length++;
		}

		_req->dma = dma_map_single(&dev->pdev->dev, _req->buf, _req->length,
			ep->is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		if (zlflag && (_req->length == 1)) {
			VDEBUG(dev, "req->length: 1->0\n");
			zlflag = 0;
			_req->length = 0;
		}

        if (dma_mapping_error(&dev->pdev->dev, _req->dma)) {
            _req->dma = DMA_ADDR_INVALID;
            return -EIO;
        }
		req->mapped = 1;
	}

	DEBUG(dev, "%s queue req %p, len %u, buf %p, dma 0x%08x\n",
			_ep->name, _req, _req->length,
			_req->buf, _req->dma);

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* set ZLP flag for ep0-in, when writting data,
	 * makes the last packet be "short" by adding a zero
	 * length packet as needed
	 */
	if (unlikely(ep->num == 0 && ep->is_in))
		_req->zero = 1;

	/* kickstart this I/O queue */
	if (list_empty(&ep->queue) && !ep->stopped) {
		start_dma(ep, req);

		val_16 = readw(&ep->dev->regs->ep[ep->num].ep_pib);
		VDEBUG(ep->dev, "after dma, %s.ep_pib = 0x%04x\n",
				_ep->name, val_16);

		val_16 = readw(&ep->dev->regs->ep[ep->num].ep_sts);
		VDEBUG(ep->dev, "after dma, %s.ep_sts = 0x%04x\n",
				_ep->name, val_16);
	} else {
		queue_dma(ep, req);
		VDEBUG(ep->dev, "%s queue_dma()\n", _ep->name);
	}

	if (likely(req != 0)) {
		list_add_tail(&req->queue, &ep->queue);
		VDEBUG(ep->dev, "list_add_tail() \n");
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	VDEBUG(ep->dev, "<--- iusbc_queue() \n");

	return 0;
}


static inline void
dma_done(struct iusbc_ep *ep, struct iusbc_request *req, int status)
{
	unsigned	i;
	VDEBUG(ep->dev, "---> dma_done() \n");

	i = ep->num;
	req->req.actual = readw(&ep->dev->regs->ep[i].ep_pib);
	VDEBUG(ep->dev, "req->req.actual = %d\n", req->req.actual);

	done(ep, req, status);

	VDEBUG(ep->dev, "<--- dma_done() \n");
}


/* restart dma in endpoint */
static void restart_dma(struct iusbc_ep *ep)
{
	struct iusbc_request	*req;

	VDEBUG(ep->dev, "---> restart_dma() \n");

	if (ep->stopped)
		return;

	req = list_entry(ep->queue.next, struct iusbc_request, queue);
	start_dma(ep, req);

	VDEBUG(ep->dev, "<--- restart_dma() \n");

	return;
}


/* dequeue ALL requests */
static void nuke(struct iusbc_ep *ep)
{
	struct iusbc_request	*req;

	VDEBUG(ep->dev, "---> nuke() \n");

	/* called with spinlock held */
	ep->stopped = 1;
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
				struct iusbc_request,
				queue);
		done(ep, req, -ESHUTDOWN);
	}

	VDEBUG(ep->dev, "<--- nuke() \n");
}


/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int
iusbc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct iusbc_ep		*ep;
	struct iusbc		*dev;
	struct iusbc_request	*req;
	unsigned long		flags;
	int			stopped;

	ep = container_of(_ep, struct iusbc_ep, ep);

	VDEBUG(ep->dev, "---> iusbc_dequeue() \n");

	if (!_ep || (!ep->desc && ep->num > 1) || !_req)
		return -EINVAL;

	dev = ep->dev;

	if (!dev->driver)
		return -ESHUTDOWN;

	/* can't touch registers when suspended */
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	spin_lock_irqsave(&ep->dev->lock, flags);
	stopped = ep->stopped;

	/* quiesce dma while we patch the queue */
	ep->stopped = 1;

	/* make sure it's still queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}

	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->dev->lock, flags);
		return -EINVAL;
	}

	/* queue head may be partially complete. */
	if (ep->queue.next == &req->queue) {
		DEBUG(ep->dev, "unlink (%s) dma\n", _ep->name);
		_req->status = -ECONNRESET;
		if (likely(ep->queue.next == &req->queue)) {
			req->td->dmacount = 0;	/* invalidate */
			dma_done(ep, req, -ECONNRESET);
		}
		req = NULL;
	}

	if (req)
		done(ep, req, -ECONNRESET);

	ep->stopped = stopped;

	if (!list_empty(&ep->queue) && (!ep->stopped)) {
		/* resume current request, or start new one */
		if (!req)
			start_dma(ep, list_entry(ep->queue.next,
				struct iusbc_request, queue));
	}

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	VDEBUG(ep->dev, "<--- iusbc_dequeue() \n");

	return 0;
}

/*-------------------------------------------------------------------------*/

static void ep0_start(struct iusbc *dev);
static void ep_ack(struct iusbc_ep *ep);
static int iusbc_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc);

static void clear_halt(struct iusbc_ep *ep)
{
	u16		 	val_16;
	unsigned		i = ep->num;
	int			rc = 0;
	struct iusbc_request	*req;
	const struct usb_endpoint_descriptor	*desc;

	DEBUG(ep->dev, "---> clear_halt() \n");

	/* validate and enable endpoint */
	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	val_16 |= EP_VALID | EP_ENABLE;
	writew(val_16, &ep->dev->regs->ep[i].ep_cfg);

	/* re-enable endpoint */
	if (i < 2) {	/* ep0-in and ep0-out */
		ep0_start(ep->dev);
	} else {
		spin_unlock_irq(&ep->dev->lock);
		/* remember ep->desc */
		desc = ep->desc;
		rc = iusbc_ep_enable(&ep->ep, desc);
		if (rc) {
			DEBUG(ep->dev, "re-enable error: %d\n", rc);
		}
		spin_lock_irq(&ep->dev->lock);
	}

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	DEBUG(ep->dev, "%s.ep_cfg = 0x%04x\n", ep->ep.name, val_16);

	ep->stopped = 0;
	if (list_empty(&ep->queue))
		return;

	req = list_entry(ep->queue.next, struct iusbc_request,queue);
	start_dma(ep, req);

	DEBUG(ep->dev, "<--- clear_halt() \n");
}


static void set_halt(struct iusbc_ep *ep)
{
	u16		val_16;
	unsigned	i = ep->num;

	DEBUG(ep->dev, "---> set_halt() \n");

	/* reset data buffer zero length */
	writew(0, &ep->dev->regs->ep[i].ep_len);

	/* invalidate and disable endpoint */
	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	val_16 &= (~EP_VALID & ~EP_ENABLE);
	writew(val_16, &ep->dev->regs->ep[i].ep_cfg);

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	DEBUG(ep->dev, "%s.ep_cfg = 0x%04x\n", ep->ep.name, val_16);

	ep->stopped = 1;

	DEBUG(ep->dev, "<--- set_halt() \n");
}


static int iusbc_fifo_status(struct usb_ep *_ep);

/* sets the endpoint halt feature */
static int
iusbc_set_halt(struct usb_ep *_ep, int value)
{
	struct iusbc_ep		*ep;
	unsigned long		flags;
	int			retval = 0;

	ep = container_of(_ep, struct iusbc_ep, ep);

	DEBUG(ep->dev, "---> iusbc_set_halt() \n");

	if (!_ep || (!ep->desc && ep->num > 1))
		return -EINVAL;

	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	if (ep->desc && (ep->desc->bmAttributes & 0x03)
			== USB_ENDPOINT_XFER_ISOC)
		return -EINVAL;

	spin_lock_irqsave(&ep->dev->lock, flags);

	/* transfer requests are still queued */
	if (!list_empty(&ep->queue))
		retval = -EAGAIN;

	else if (ep->is_in && value && iusbc_fifo_status(_ep) != 0) {
		/* FIFO holds bytes, the host hasn't collected */
		DEBUG(ep->dev, "%s FIFO holds bytes\n", _ep->name);

		/* reset position in buffer register */
		writew(0, &ep->dev->regs->ep[ep->num].ep_pib);

		retval = -EAGAIN;
	} else {
		DEBUG(ep->dev, "%s %s halt\n", _ep->name,
				value ? "set" : "clear");
		/* set/clear, then synch memory views with the device */
		if (value) {
			if (ep->num < 2) { /* ep0-in/out */
				ep->dev->ep0state = EP0_STALL;
				VDEBUG(ep->dev, "ep0state: EP0_STALL\n");
			} else {
				set_halt(ep);
				ep_ack(&ep->dev->ep[0]);
			}
		} else {
			clear_halt(ep);
			ep_ack(&ep->dev->ep[0]);
		}

	}
	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DEBUG(ep->dev, "<--- iusbc_set_halt() \n");

	return retval;
}


/* return number of bytes in fifo, or error */
static int
iusbc_fifo_status(struct usb_ep *_ep)
{
	struct iusbc_ep		*ep;
	unsigned		i;
	u16			nbytes, fifo_size;

	ep = container_of(_ep, struct iusbc_ep, ep);

	DEBUG(ep->dev, "---> iusbc_fifo_status() \n");

	if (!_ep || (!ep->desc && ep->num > 1))
		return -ENODEV;

	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	i = ep->num;
	fifo_size = readw(&ep->dev->regs->ep[i].ep_len);
	nbytes = readw(&ep->dev->regs->ep[i].ep_pib);

	if (nbytes > fifo_size)
		return -EOVERFLOW;

	DEBUG(ep->dev, "%s, 0x%04x bytes (%s) in FIFO\n",
			_ep->name, nbytes, ep->is_in? "IN" : "OUT");

	DEBUG(ep->dev, "<--- iusbc_fifo_status() \n");

	return nbytes;
}


static void ep_nak(struct iusbc_ep *ep);

/* flushes contents of a fifo */
static void
iusbc_fifo_flush(struct usb_ep *_ep)
{
	struct iusbc_ep		*ep;
	unsigned		i;
	u16			val_16;

	ep = container_of(_ep, struct iusbc_ep, ep);

	DEBUG(ep->dev, "---> iusbc_fifo_flush() \n");

	if (!_ep || (!ep->desc && ep->num > 1)){
		return;
	}

	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return;

	i = ep->num;

	/* FIXME: remove it ? */
	ep_nak(ep);

	/* reset position in buffer register */
	val_16 = readw(&ep->dev->regs->ep[i].ep_pib);
	DEBUG(ep->dev, "%s.ep_pib = 0x%04x\n", _ep->name, val_16);
	writew(0, &ep->dev->regs->ep[i].ep_pib);

	DEBUG(ep->dev, "<--- iusbc_fifo_flush() \n");
}

static const struct usb_ep_ops iusbc_ep_ops = {

	/* configure endpoint, making it usable */	
	.enable		= iusbc_ep_enable,

	/* endpoint is no longer usable */
	.disable	= iusbc_ep_disable,

	/* allocate a request object to use with this endpoint */
	.alloc_request	= iusbc_alloc_request,

	/* frees a request object */
	.free_request	= iusbc_free_request,

	/* allocate an I/O buffer */
	/*.alloc_buffer	= iusbc_alloc_buffer,*/

	/* free an I/O buffer */
	/*.free_buffer	= iusbc_free_buffer,*/

	/* queues (submits) an I/O requests to an endpoint */
	.queue		= iusbc_queue,

	/* dequeues (cancels, unlinks) an I/O request from an endpoint */
	.dequeue	= iusbc_dequeue,

	/* sets the endpoint halt feature */
	.set_halt	= iusbc_set_halt,

	/* return number of bytes in fifo, or error */
	.fifo_status	= iusbc_fifo_status,

	/* flushes contents of a fifo */
	.fifo_flush	= iusbc_fifo_flush,
};

/*-------------------------------------------------------------------------*/

/* returns the current frame number */
static int iusbc_get_frame(struct usb_gadget *_gadget)
{
	struct iusbc		*dev;
	unsigned long		flags;
	u16			retval;

	if (!_gadget)
		return -ENODEV;

	dev = container_of(_gadget, struct iusbc, gadget);

	VDEBUG(dev, "---> iusbc_get_frame() \n");

	spin_lock_irqsave(&dev->lock, flags);
	retval = readw(&dev->regs->frame);
	spin_unlock_irqrestore(&dev->lock, flags);

	VDEBUG(dev, "<--- iusbc_get_frame() \n");

	return retval;
}


/* TODO: wakeup host function */
/* tries to wake up the host connected to this gadget */
static int iusbc_wakeup(struct usb_gadget *_gadget)
{
	struct iusbc		*dev;

	if (!_gadget)
		return 0;

	dev = container_of(_gadget, struct iusbc, gadget);

	VDEBUG(dev, "---> iusbc_wakeup() \n");

	/* TODO: spec 4.3 */

	VDEBUG(dev, "<--- iusbc_wakeup() \n");

	return 0;
}


/* software-controlled connect/disconnect to USB host */
static int iusbc_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct iusbc	*dev;
	u32             val_32;
	unsigned long   flags;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct iusbc, gadget);

	VDEBUG(dev, "---> iusbc_pullup() \n");

	spin_lock_irqsave(&dev->lock, flags);
	val_32 = readl(&dev->regs->dev_ctrl);
	dev->connected = (is_on != 0);
	if (is_on)
		val_32 |= CONNECTION_ENABLE;
	else
		val_32 &= ~CONNECTION_ENABLE;

	writel(val_32, &dev->regs->dev_ctrl);
	spin_unlock_irqrestore(&dev->lock, flags);

	VDEBUG(dev, "<--- iusbc_pullup() \n");

	return 0;
}


static int iusbc_udc_start(struct usb_gadget *, struct usb_gadget_driver *);
static int iusbc_udc_stop(struct usb_gadget *);

static const struct usb_gadget_ops iusbc_ops = {
    .udc_start = iusbc_udc_start,
    .udc_stop = iusbc_udc_stop,

	/* returns the current frame number */
	.get_frame	= iusbc_get_frame,

	/* TODO */
	/* tries to wake up the host connected to this gadget */
	.wakeup		= iusbc_wakeup,

	/* software-controlled connect/disconnect to USB host */
	.pullup		= iusbc_pullup,
};

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_GADGET_DEBUG_FILES

/* "function" sysfs attribute */
static ssize_t
show_function(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct iusbc	*dev = dev_get_drvdata(_dev);

	if (!dev->driver
			|| !dev->driver->function
			|| strlen(dev->driver->function) > PAGE_SIZE)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR(function, S_IRUGO, show_function, NULL);


static ssize_t
show_registers(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct iusbc		*dev;
	char			*next;
	unsigned		size;
	unsigned		t;
	unsigned		i;
	unsigned long		flags;
	const char		*name;
	const char		*speed;
	volatile u32 		dev_sts;

	dev = dev_get_drvdata(_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave(&dev->lock, flags);

	if (dev->driver)
		name = dev->driver->driver.name;
	else
		name = "(none)";

	dev_sts = readl(&dev->regs->dev_sts);
	if ((dev->gadget.speed == USB_SPEED_HIGH) && (dev_sts & CONNECTED))
		speed = "high speed";
	else if ((dev->gadget.speed == USB_SPEED_FULL) && (dev_sts & CONNECTED))
		speed = "full speed";
	else 
		speed = "unknown speed";

	/* device information */
	t = scnprintf(next, size,
			"%s %s - %s\n"
			"Version: %s\n"
			"Gadget driver: %s\n"
			"Speed mode: %s\n",
			driver_name, pci_name(dev->pdev), driver_desc,
			DRIVER_VERSION, 
			name,
			speed);
	size -= t;
	next += t;

	/* device memory space registers */
	t = scnprintf(next, size,
			"\nDevice registers:\n"
			"\tgcap=0x%08x\n"
			"\tdev_sts=0x%08x\n"
			"\tframe=0x%04x\n"
			"\tint_sts=0x%08x\n"
			"\tint_ctrl=0x%08x\n"
			"\tdev_ctrl=0x%08x\n",
			readl(&dev->regs->gcap),
			readl(&dev->regs->dev_sts),
			readw(&dev->regs->frame),
			readl(&dev->regs->int_sts),
			readl(&dev->regs->int_ctrl),
			readl(&dev->regs->dev_ctrl)
			);
	size -= t;
	next += t;

	/* endpoints memory space registers */
	t = scnprintf(next, size, "\nEndpoints registers:\n");
	size -= t;
	next += t;

	for (i = 0; i < 5; i++) {
		struct iusbc_ep		*ep;
		ep = &dev->ep[i];

		if (i > 1 && !ep->desc)
			continue;

		name = ep->ep.name;
		t = scnprintf(next, size,
				"\t%s.ep_base_low_32=0x%08x\n"
				"\t%s.ep_base_hi_32=0x%08x\n"
				"\t%s.ep_len=0x%04x\n"
				"\t%s.ep_pib=0x%04x\n"
				"\t%s.ep_dil=0x%04x\n"
				"\t%s.ep_tiq=0x%04x\n"
				"\t%s.ep_max=0x%04x\n"
				"\t%s.ep_sts=0x%04x\n"
				"\t%s.ep_cfg=0x%04x\n",
				name, readl(&dev->regs->ep[i].ep_base_low_32),
				name, readl(&dev->regs->ep[i].ep_base_hi_32),
				name, readw(&dev->regs->ep[i].ep_len),
				name, readw(&dev->regs->ep[i].ep_pib),
				name, readw(&dev->regs->ep[i].ep_dil),
				name, readw(&dev->regs->ep[i].ep_tiq),
				name, readw(&dev->regs->ep[i].ep_max),
				name, readw(&dev->regs->ep[i].ep_sts),
				name, readw(&dev->regs->ep[i].ep_cfg)
			     );
		size -= t;
		next += t;
	}

	/* ep0-out setup packet registers */
	t = scnprintf(next, size,
			"\tsetup_pkt_sts=0x%02x\n",
			readb(&dev->regs->ep[1].setup_pkt_sts)
		     );
	size -= t;
	next += t;

	for (i = 0; i < 8; i++) {
		t = scnprintf(next, size,
				"\tsetup_pkt[%d]=0x%02x\n",
				i,
				readb(&dev->regs->ep[1].setup_pkt[i])
				);
		size -= t;
		next += t;
	}

	/* Irq statistics */
	t = scnprintf(next, size, "\nIrq statistics:\n");
	size -= t;
	next += t;

	for (i = 0; i < 5; i++) {
		struct iusbc_ep	*ep;
		ep = &dev->ep[i];

		if (i && !ep->irqs)
			continue;

		t = scnprintf(next, size,
				"\t%s/%lu\n",
				ep->ep.name, ep->irqs);
		size -= t;
		next += t;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR(registers, S_IRUGO, show_registers, NULL);


static ssize_t
show_queues(struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct iusbc		*dev;
	char			*next;
	unsigned		size, i;
	unsigned long		flags;

	dev = dev_get_drvdata(_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave(&dev->lock, flags);

	for (i = 0; i < 5; i++) {
		struct iusbc_ep		*ep = &dev->ep[i];
		struct iusbc_request	*req;
		struct iusbc_dma	*td;
		int			t;
		int			addr;

		if (i > 1 && !ep->desc)
			continue;

		/* ep0-in, ep0-out */
		if (i == 0 || i == 1) {
			t = scnprintf(next, size,
				"%s (ep%d%s-%s), "
				"max %04x, dma_mode: %02x\n",
				ep->ep.name,
				0,
				ep->is_in ? "in" : "out",
				"control",
				ep->ep.maxpacket,
				ep->dma_mode);
		} else {
			addr = ep->desc->bEndpointAddress;
			t = scnprintf(next, size,
				"\n%s (ep%d%s-%s), "
				"max %04x, dma_mode: %02x\n",
				ep->ep.name,
				addr & USB_ENDPOINT_NUMBER_MASK,
				DIR_STRING(addr),
				type_string(ep->desc->bmAttributes),
				ep->ep.maxpacket,
				ep->dma_mode);
		}

		if (t <= 0 || t > size)
			goto done;

		size -= t;
		next += t;

		if (list_empty(&ep->queue)) {
			t = scnprintf(next, size, "\t(nothing queued)\n");
			if (t <= 0 || t > size)
				goto done;

			size -= t;
			next += t;
			continue;
		}

		list_for_each_entry(req, &ep->queue, queue) {
			t = scnprintf(next, size,
				"\treq %p, len %u/%u, "
				"buf %p, dma 0x%08x)\n",
				&req->req, req->req.actual,
				req->req.length, req->req.buf,
				req->req.dma);

			if (t <= 0 || t > size)
				goto done;

			size -= t;
			next += t;

			td = req->td;
			t = scnprintf(next, size, "\ttd 0x%08x, "
				" count 0x%08x, buf 0x%08x\n",
				(u32) req->td_dma,
				le32_to_cpu(td->dmacount),
				le32_to_cpu(td->dmaaddr));

			if (t <= 0 || t > size)
				goto done;

			size -= t;
			next += t;
		}
	}

done:
	spin_unlock_irqrestore(&dev->lock, flags);
	return PAGE_SIZE - size;
}
static DEVICE_ATTR(queues, S_IRUGO, show_queues, NULL);

#else

#define device_create_file(a,b)	(0)
#define device_remove_file(a,b)	do { } while (0)

#endif	/*CONFIG_USB_GADGET_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

/* global variable */
static struct iusbc	*the_controller;

static void iusbc_reset(struct iusbc *dev)
{
	DEBUG(dev, "---> iusbc_reset() \n");

	/* disable irqs */
	writel(0, &dev->regs->int_ctrl);

	/* set device power stauts */
	dev->powered = 1;

	/* set device remote wakeup flag */
	dev->enable_wakeup = 0;

	/* 16 bits status data for GET_STATUS */
	dev->status_d = 0;

	DEBUG(dev, "<--- iusbc_reset() \n");
}


static void iusbc_reinit(struct iusbc *dev)
{
	unsigned	i;

	DEBUG(dev, "---> iusbc_reinit() \n");

	INIT_LIST_HEAD(&dev->gadget.ep_list);

	/* ep0-in */
	dev->gadget.ep0 = &dev->ep[0].ep;

	/* init ep0-in and ep0-out driver_data */
	dev->ep[0].ep.driver_data = get_gadget_data(&dev->gadget);
	dev->ep[1].ep.driver_data = get_gadget_data(&dev->gadget);

	dev->ep0state = EP0_DISCONNECT;
	VDEBUG(dev, "ep0state: EP0_DISCONNECT\n");

	/* basic endpoint init */
	/* 2 ep0, 3 data ep */
	for (i = 0; i < 5; i++) {
		struct iusbc_ep	*ep = &dev->ep[i];
		ep->dev = dev;

		INIT_LIST_HEAD(&ep->queue);

		ep->desc = NULL;
		ep->num = i;
		ep->stopped = 1;
		ep->ep.name = ep_name[i];
		usb_ep_set_maxpacket_limit(&ep->ep, i < 2 ? 64 : (i < 4 ? 512 : 1024));
        ep->ep.caps.type_control = i < 2;
        ep->ep.caps.type_bulk = i == 2 || i == 3;
        ep->ep.caps.type_int = i == 4;
        ep->ep.caps.dir_in = !(i & 1);
        ep->ep.caps.dir_out = !!(i & 1);
		ep->ep.ops = &iusbc_ep_ops;

		list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);
		ep_reset(dev->regs, ep);
	}

	/* set ep0 maxpacket */
	dev->ep[0].ep.maxpacket = 64; /* ep0_in */
	dev->ep[1].ep.maxpacket = 64; /* ep0_out */

	dev->ep[0].stopped = 0;
	dev->ep[1].stopped = 0;

	list_del_init(&dev->ep[0].ep.ep_list);
	list_del_init(&dev->ep[1].ep.ep_list);

	DEBUG(dev, "<--- iusbc_reinit() \n");
}


static void ep0_start(struct iusbc *dev)
{
	u16		val_16;
	u32		val_32;
	unsigned	i;

	DEBUG(dev, "---> ep0_start() \n");

	iusbc_reset(dev);
	iusbc_reinit(dev);

	for (i = 0; i < 2; i++) {
		struct iusbc_ep	*ep = &dev->ep[i];

		/* ep[0]: ep0-in, ep[1]: ep0-out */
		ep->is_in = (i == 0 ? 1 : 0);

		/* ep0 ep_type */
		ep->ep_type = USB_ENDPOINT_XFER_CONTROL;

		/* linear mode only, control mode is useless */
		ep->dma_mode = LINEAR_MODE;

		/* reset ep0-in/out registers to default value */
		writew(0, &dev->regs->ep[i].ep_cfg);

		/* set ep0-in/out endpoints valid */
		val_16 = readw(&dev->regs->ep[i].ep_cfg);
		val_16 |= INTR_BAD_PID_TYPE
			| INTR_CRC_ERROR
			| INTR_FIFO_ERROR
			| INTR_DMA_ERROR
			| INTR_TRANS_COMPLETE
			/* | INTR_PING_NAK_SENT */
			| INTR_DMA_IOC
			| ep->dma_mode << 6
			| ep->ep_type << 4 
			/* | EP_ENABLE */
			| EP_VALID;

		writew(val_16, &dev->regs->ep[i].ep_cfg);

		val_16 = readw(&dev->regs->ep[i].ep_cfg);
		DEBUG(dev, "%s.ep_cfg = 0x%04x\n", ep->ep.name, val_16);

		DEBUG(dev, "enabled %s (ep0-%s), max %d, dma_mode: %02x\n",
			ep->ep.name,
			ep->is_in ? "in" : "out",
			ep->ep.maxpacket, ep->dma_mode);
	}

	/* enable irqs */
	val_32 = readl(&dev->regs->int_ctrl);
	val_32 |= RESET_INTR_ENABLE
		| CONNECT_INTR_ENABLE
		| SUSPEND_INTR_ENABLE
		/* | EP3_OUT_INTR_ENABLE */
		/* | EP3_IN_INTR_ENABLE */
		/* | EP2_OUT_INTR_ENABLE */
		| EP2_IN_INTR_ENABLE
		| EP1_OUT_INTR_ENABLE
		| EP1_IN_INTR_ENABLE
		| EP0_OUT_INTR_ENABLE
		| EP0_IN_INTR_ENABLE;

	writel(val_32, &dev->regs->int_ctrl);
	val_32 = readl(&dev->regs->int_ctrl);
	DEBUG(dev, "ep0_start: enable irqs, int_ctrl = 0x%08x\n",
			val_32);

	dev->ep0state = EP0_IDLE;
	VDEBUG(dev, "ep0state: EP0_IDLE\n");

	DEBUG(dev, "<--- ep0_start() \n");
}


static void iusbc_do_tasklet(unsigned long arg);

static void device_start(struct iusbc *dev)
{
	u32	val_32;

	DEBUG(dev, "---> device_start() \n");

	dev->running = 1;
	/* reset all registers */ 
	writel(0, &dev->regs->dev_ctrl);

	/* PCI enable: write 1 to DeviceEnable */
	writel(DEVICE_ENABLE, &dev->regs->dev_ctrl);	
	/* FIXME: 5 ms is not enough? */
	mdelay(5);
	val_32 = readl(&dev->regs->dev_ctrl);
	if (!(val_32 & DEVICE_ENABLE))
		ERROR(dev, "hardware reset error\n");

	/* hardware transfer to running state now */
	val_32 |= DEVICE_ENABLE
		/* | CONNECTION_ENABLE */
		| SIGNAL_RESUME
		| CHARGE_ENABLE;

	/* module parameter: force_fullspeed */
	if (force_fullspeed) {
		val_32 |= FORCE_FULLSPEED;
		dev->force_fullspeed = 1;
	} else {
		/* disable DMAs in high speed mode */
#ifdef DMA_DISABLED
		val_32 |= DMA1_DISABLED
			| DMA2_DISABLED
			| DMA3_DISABLED;
#endif
		dev->force_fullspeed = 0;
	}

	writel(val_32, &dev->regs->dev_ctrl);
	val_32 = readl(&dev->regs->dev_ctrl);
	DEBUG(dev, "dev_ctrl = 0x%08x\n", val_32);

	/* check device status */
	val_32 = readl(&dev->regs->dev_sts);
	DEBUG(dev, "device_start: dev_sts = 0x%08x\n", val_32);

	if (val_32 & CONNECTED) {
		dev->connected = 1;
		VDEBUG(dev, "device_start: USB attached\n");
	} else {
		dev->connected = 0;
		VDEBUG(dev, "device_start: USB detached\n");
	}

	if (val_32 & SUSPEND)
		dev->suspended = 1;
	else 
		dev->suspended = 0;

 	/* set device reset flag */
	dev->is_reset = 0;

	iusbc_pullup(&dev->gadget, 1);

	/* init irq tasklet */

	/* enable ep0 and host detection */
	ep0_start(dev);

	DEBUG(dev, "<--- device_start() \n");
}


/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
static int iusbc_udc_start(struct usb_gadget *gadget,
                           struct usb_gadget_driver *driver)
{
    struct iusbc *dev = container_of(gadget, struct iusbc, gadget);
    dev->driver = driver;
    device_start(dev);
    return 0;
}

static void
stop_activity(struct iusbc *dev, struct usb_gadget_driver *driver)
{
	unsigned	i;

	DEBUG(dev, "---> stop_activity() \n");

	/* don't disconnect if it's not connected */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;

	/* stop hardware; prevent new request submissions;
	 * and kill any outstanding requests.
	 */
	iusbc_reset(dev);

	/* we need to mark ep0state to disconnect to avoid upper layer to start
 	 * next transfer immediately after the following nuke operation, the
 	 * iusbc_queue function need to check ep0state before issue a transfer
 	 */
	dev->ep0state = EP0_DISCONNECT;

	/* 2 ep0, 3 data ep */
	for (i = 0; i < 5; i++)
		nuke(&dev->ep[i]);

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&dev->lock);
		driver->disconnect(&dev->gadget);
		spin_lock(&dev->lock);
	}

	iusbc_reinit(dev);

	DEBUG(dev, "<--- stop_activity() \n");
}


static int iusbc_udc_stop(struct usb_gadget *gadget)
{
    struct iusbc *dev = container_of(gadget, struct iusbc, gadget);
    unsigned long flags;
    spin_lock_irqsave(&dev->lock, flags);
    dev->running = 0;
    writel(0, &dev->regs->int_ctrl);
    spin_unlock_irqrestore(&dev->lock, flags);
    synchronize_irq(dev->pdev->irq);
    tasklet_kill(&dev->iusbc_tasklet);
    spin_lock_irqsave(&dev->lock, flags);
    stop_activity(dev, NULL);
    dev->driver = NULL;
    spin_unlock_irqrestore(&dev->lock, flags);
    iusbc_pullup(gadget, 0);
    return 0;
}

/*-------------------------------------------------------------------------*/

static struct iusbc_ep *
get_ep_by_addr(struct iusbc *dev, u16 wIndex)
{
	struct iusbc_ep		*ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &dev->ep[0];

	list_for_each_entry(ep, &dev->gadget.ep_list, ep.ep_list) {
		u8	bEndpointAddress;
		if (!ep->desc)
			continue;

		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;

		if ((wIndex & USB_ENDPOINT_NUMBER_MASK)
			== (bEndpointAddress & USB_ENDPOINT_NUMBER_MASK))
			return ep;
	}
	return NULL;
}


/* NAK an endpoint */
static void ep_nak(struct iusbc_ep *ep)
{
	u16		val_16;
	unsigned	i = ep->num;

	DEBUG(ep->dev, "---> ep_nak() \n");

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	val_16 &= ~EP_ENABLE;
	val_16 |= EP_VALID;
	writew(val_16, &ep->dev->regs->ep[i].ep_cfg);

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	VDEBUG(ep->dev, "%s.ep_cfg = 0x%04x\n", ep->ep.name, val_16);

	DEBUG(ep->dev, "<--- ep_nak() \n");
}


/* ACK an out transfer with a zero length packet (ZLP) */
static void ep_ack(struct iusbc_ep *ep)
{
	u16		val_16;
	unsigned	i = ep->num;

	DEBUG(ep->dev, "---> ep_ack() \n");

	/* reset data buffer zero length */
	writew(0, &ep->dev->regs->ep[i].ep_len);
	val_16 = readw(&ep->dev->regs->ep[i].ep_len);
	VDEBUG(ep->dev, "%s.ep_len = 0x%04x\n", ep->ep.name, val_16);

	/* validate endpoint, enable DMA */
	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	val_16 |= (EP_VALID | EP_ENABLE);
	writew(val_16, &ep->dev->regs->ep[i].ep_cfg);

	val_16 = readw(&ep->dev->regs->ep[i].ep_cfg);
	VDEBUG(ep->dev, "enable %s DMA transfer...\n", ep->ep.name);
	VDEBUG(ep->dev, "%s.ep_cfg = 0x%04x\n", ep->ep.name, val_16);

	DEBUG(ep->dev, "<--- ep_ack() \n");
}


static void status_complete(struct usb_ep *ep, struct usb_request *req)
{
}

static void ep0_setup(struct iusbc *dev)
{
	struct usb_ctrlrequest		ctrl;
	struct iusbc_ep			*epn;
	unsigned			i, tmp = 0;
	u8				addr_new, setup_pkt[8];

	VDEBUG(dev, "---> ep0_setup() \n");

	for (i = 0; i < 8; i++) {
		setup_pkt[i] = readb(&dev->regs->ep[1].setup_pkt[i]);
	}

	/* read SETUP packet and enter DATA stage */
	ctrl.bRequestType = setup_pkt[0];
	ctrl.bRequest = setup_pkt[1];
	ctrl.wValue  = cpu_to_le16((setup_pkt[3] << 8)
			| setup_pkt[2]);
	ctrl.wIndex  = cpu_to_le16((setup_pkt[5] << 8)
			| setup_pkt[4]);
	ctrl.wLength = cpu_to_le16((setup_pkt[7] << 8)
			| setup_pkt[6]);

	dev->ep[1].stopped = 0;

	/* data stage direction */
	if (ctrl.bRequestType & USB_DIR_IN) {
		dev->ep0state = EP0_IN;
		VDEBUG(dev, "ep0state: EP0_IN\n");
	} else {
		dev->ep0state = EP0_OUT;
		VDEBUG(dev, "ep0state: EP0_OUT\n");
	}

    /* Class/vendor request numbers can overlap standard USB requests. */
	if ((ctrl.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD)
		goto delegate;

	/* WORKAROUND: for RNDIS */
	/* CDC: SEND_ENCAPSULATED_COMMAND */
	if ((ctrl.bRequestType == 0x21) && (ctrl.bRequest == 0x00)) {
		goto delegate;
	}
	/* CDC: GET_ENCAPSULATED_COMMAND */
	if ((ctrl.bRequestType == 0xa1) && (ctrl.bRequest == 0x01)) {
		goto delegate;
	}

	switch (ctrl.bRequest) {
	/* GET_STATUS is handled by software */
	case USB_REQ_GET_STATUS:
		DEBUG(dev, "SETUP: USB_REQ_GET_STATUS\n");
		switch (ctrl.bRequestType &
				__constant_cpu_to_le16(USB_RECIP_MASK)) {
		case USB_RECIP_DEVICE:
			/* bit 0: USB_DEVICE_SELF_POWERED
			 * bit 1: USB_DEVICE_REMOTE_WAKEUP
			 */
			if (dev->enable_wakeup) {
				dev->status_d =
					__constant_cpu_to_le16(1 << 1 | 1);
			} else {
				dev->status_d = __constant_cpu_to_le16(1);
			}
			DEBUG(dev, "device status 0x%04x\n", dev->status_d);
			break;
		case USB_RECIP_INTERFACE:
			/* all bits zero */
			dev->status_d = __constant_cpu_to_le16(0);
			DEBUG(dev, "interface status 0x%04x\n", dev->status_d);
			break;
		case USB_RECIP_ENDPOINT:
			if ((epn = get_ep_by_addr(dev,
					le16_to_cpu(ctrl.wIndex))) == 0)
				goto stall;
			if (epn->stopped) {	/* halted */
				dev->status_d = __constant_cpu_to_le16(1);
			} else {
				dev->status_d = __constant_cpu_to_le16(0);
			}
			DEBUG(dev, "%s endpoint status 0x%04x\n",
					epn->ep.name, dev->status_d);
			break;
		}
        /* Modern function drivers expect standard GET_STATUS in the UDC. */
        if (ctrl.bRequestType & USB_DIR_IN) {
            struct usb_request *req = dev->status_req;
            if (le16_to_cpu(ctrl.wLength) != 2 ||
                !list_empty(&container_of(req, struct iusbc_request, req)->queue))
                goto stall;
            memcpy(req->buf, &dev->status_d, 2);
            req->length = 2;
            req->complete = status_complete;
            req->zero = 0;
            spin_unlock(&dev->lock);
            tmp = iusbc_queue(&dev->ep[0].ep, req, GFP_ATOMIC);
            spin_lock(&dev->lock);
            if ((int)tmp < 0)
                goto stall;
            goto end;
        }
        goto stall;

	case USB_REQ_CLEAR_FEATURE:
		DEBUG(dev, "SETUP: USB_REQ_CLEAR_FEATURE\n");
		switch (ctrl.bRequestType) {
		case USB_RECIP_DEVICE:
			if (ctrl.wValue == __constant_cpu_to_le16(
						USB_DEVICE_REMOTE_WAKEUP)) {
				dev->enable_wakeup = 0;
				DEBUG(dev, "CLEAR_FEATURE: "
					"remote wakeup disabled\n");
				goto end;
			} else {
				DEBUG(dev, "unsupported CLEAR_FEATURE\n");
				goto stall;
			}
			break;
		case USB_RECIP_INTERFACE:
			DEBUG(dev, "unsupported CLEAR_FEATURE\n");
			goto stall;
		case USB_RECIP_ENDPOINT:
			if (ctrl.wValue != __constant_cpu_to_le16(
						USB_ENDPOINT_HALT)
					|| le16_to_cpu(ctrl.wLength) != 0)
				goto stall;
			if ((epn = get_ep_by_addr(dev,
					le16_to_cpu(ctrl.wIndex))) == 0)
				goto stall;
			clear_halt(epn);
			ep_ack(&dev->ep[0]);

			dev->ep0state = EP0_STATUS;
			VDEBUG(dev, "ep0state: EP0_STATUS\n");

			DEBUG(dev, "%s clear halt\n", epn->ep.name);
			goto end;
		}
		break;

	case USB_REQ_SET_FEATURE:
		DEBUG (dev, "SETUP: USB_REQ_SET_FEATURE\n");
		switch (ctrl.bRequestType) {
		case USB_RECIP_DEVICE:
			if (ctrl.wValue == __constant_cpu_to_le16(
						USB_DEVICE_REMOTE_WAKEUP)) {
				dev->enable_wakeup = 1;
				DEBUG(dev, "SET_FEATURE: "
					"remote wakeup enabled\n");
				goto end;
			} else {
				DEBUG(dev, "unsupported SET_FEATURE\n");
				goto stall;
			}
			break;
		case USB_RECIP_INTERFACE:
			DEBUG(dev, "unsupported SET_FEATURE\n");
			goto stall;
		case USB_RECIP_ENDPOINT:
			if (ctrl.wValue != __constant_cpu_to_le16(
						USB_ENDPOINT_HALT)
					|| le16_to_cpu(ctrl.wLength) != 0)
				goto stall;
			if ((epn = get_ep_by_addr(dev,
					le16_to_cpu(ctrl.wIndex))) == 0)
				goto stall;
			set_halt(epn);
			ep_ack(&dev->ep[0]);

			DEBUG(dev, "%s set halt\n", epn->ep.name);
			goto end;
		}
		break;

	case USB_REQ_SET_ADDRESS:
		/* hw handles set_address, address range: 1-127 */
		DEBUG(dev, "SETUP: USB_REQ_SET_ADDRESS\n");
		if (setup_pkt[1] == USB_REQ_SET_ADDRESS) {
			addr_new = le16_to_cpu(ctrl.wValue) & 0x7f;
			DEBUG(dev, "addr_new = 0x%02x\n", addr_new);

			/* hardware didn't ACK SET_ADDRESS */
			ep_ack(&dev->ep[0]);
		}
		goto end;

	case USB_REQ_GET_DESCRIPTOR:
		DEBUG(dev, "SETUP: USB_REQ_GET_DESCRIPTOR\n");
		goto delegate;

	case USB_REQ_SET_DESCRIPTOR:
		DEBUG(dev, "SETUP: USB_REQ_SET_DESCRIPTOR unsupported\n");
		goto stall;
		break;

	case USB_REQ_GET_CONFIGURATION:
		DEBUG(dev, "SETUP: USB_REQ_GET_CONFIGURATION\n");
		goto delegate;

	case USB_REQ_SET_CONFIGURATION:
		DEBUG(dev, "SETUP: USB_REQ_SET_CONFIGURATION\n");
		goto delegate;

	case USB_REQ_GET_INTERFACE:
		DEBUG(dev, "SETUP: USB_REQ_GET_INTERFACE\n");
		goto delegate;

	case USB_REQ_SET_INTERFACE:
		DEBUG(dev, "SETUP: USB_REQ_SET_INTERFACE\n");
		goto delegate;

	case USB_REQ_SYNCH_FRAME:
		DEBUG(dev, "SETUP: USB_REQ_SYNCH_FRAME unsupported\n");
		goto stall;
		break;
	default:
		/* delegate usb standard requests to the gadget driver.
		 * it may respond after this irq handler returns.
		 */
		goto delegate;
delegate:
		DEBUG(dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
			ctrl.bRequestType, ctrl.bRequest,
			le16_to_cpu(ctrl.wValue), le16_to_cpu(ctrl.wIndex),
			le16_to_cpu(ctrl.wLength));

		/* WORKAROUND: for RNDIS */
		/* CDC: SEND_ENCAPSULATED_COMMAND */
		if ((ctrl.bRequestType == 0x21)
				&& (ctrl.bRequest == 0x00)) {
			/* CDC: SEND_ENCAPSULATED_COMMAND */
			DEBUG(dev, "CDC: SEND_ENCAPSULATED_COMMAND\n");
			dev->gadget.ep0 = &dev->ep[1].ep;
			spin_unlock(&dev->lock);
			tmp = dev->driver->setup(&dev->gadget, &ctrl);
			spin_lock(&dev->lock);

			/* switch back to ep0-in */
			dev->gadget.ep0 = &dev->ep[0].ep;

			/* hardware didn't ACK */
			ep_ack(&dev->ep[0]);
		} else { 
			dev->gadget.ep0 = &dev->ep[0].ep;
			spin_unlock(&dev->lock);
			tmp = dev->driver->setup(&dev->gadget, &ctrl);
			spin_lock(&dev->lock);

			/* CDC: GET_ENCAPSULATED_COMMAND */
			if ((ctrl.bRequestType == 0xa1)
				&& (ctrl.bRequest == 0x01)) {
				DEBUG(dev, "CDC: GET_ENCAPSULATED_COMMAND\n");

				/* hardware didn't ACK */
				ep_ack(&dev->ep[1]);
			}
		}
		break;
	}

	/* stall ep0-out on error */
	if (unlikely(tmp < 0)) {
stall:
		DEBUG(dev, "req %02x.%02x protocol STALL; err %d\n",
				ctrl.bRequestType, ctrl.bRequest, tmp);
		dev->ep[1].stopped = 1;
		dev->ep0state = EP0_STALL;
		VDEBUG(dev, "ep0state: EP0_STALL\n");
	}
end:
	VDEBUG(dev, "<--- ep0_setup() \n");
}


static void handle_device_irqs(struct iusbc *dev)
{
	u32			stat;
	volatile u32		dev_sts;
	unsigned		is_connected = 0;

	DEBUG(dev, "---> handle_device_irqs() \n");

	stat = dev->int_sts;

	/* get device status, set USB speed */
	if ((stat & RESET_INTR) || (stat & CONNECT_INTR)
			|| (stat & SUSPEND_INTR)) {
		/* check device status */
		dev_sts = readl(&dev->regs->dev_sts);
		DEBUG(dev, "handle_device_irqs: dev_sts = 0x%08x\n",
				dev_sts);

		if (dev_sts & CONNECTED) {
			is_connected = 1;
			DEBUG(dev, "device connected\n");

			/* check suspend/resume status now */ 
			if (dev_sts & SUSPEND)
				dev->suspended = 1;
			else 
				dev->suspended = 0;

			/* RATE: high/full speed flag
			 * 1 clock cycle after CONNECT,
			 * read one more time until RATE bit set
			 */
			dev_sts = readl(&dev->regs->dev_sts);
			DEBUG(dev, "read for RATE: dev_sts = 0x%08x\n",
					dev_sts);
			if (dev_sts & RATE) {
				if (dev->force_fullspeed) {
					dev->gadget.speed = USB_SPEED_FULL;
					DEBUG(dev, "speed: USB_SPEED_FULL\n");
				} else { 
					dev->gadget.speed = USB_SPEED_HIGH;
					DEBUG(dev, "speed: USB_SPEED_HIGH\n");
				}
			} else {
				dev->gadget.speed = USB_SPEED_FULL;
				DEBUG(dev, "speed: USB_SPEED_FULL\n");
			}
		} else {	/* disconnected */
			is_connected = 0;
			DEBUG(dev, "device disconnected\n");
		}
	}

	/* USB reset interrupt indication */
	if ((stat & RESET_INTR) && (!dev->is_reset)) {
		DEBUG(dev, "USB Reset interrupt: stat = 0x%08x\n", stat);
		/* ACK RESET_INTR */
		stat &= ~RESET_INTR;
		dev->irqs++;
		VDEBUG(dev, "dev->irqs: %lu\n", dev->irqs);

		/* set device reset flag */
		dev->is_reset = 1;

		stop_activity(dev, dev->driver);
		ep0_start(dev);
		VDEBUG(dev, "reset: ep0_start()\n");

		goto end;
	}


	/* connect interrupt indication */
	if (stat & CONNECT_INTR) {
		DEBUG(dev, "CONNECT interrupt: stat = 0x%08x\n", stat);
		/* ACK CONNECT_INTR */
		stat &= ~CONNECT_INTR;
		dev->irqs++;
		VDEBUG(dev, "dev->irqs: %lu\n", dev->irqs);

		/* connected status has changed */
		if (dev->connected != is_connected) {
			DEBUG(dev, "connected status has changed\n");
			dev->connected = is_connected;
			if (is_connected) {
				ep0_start(dev);
				DEBUG(dev, "connect %s\n", dev->driver->driver.name);
			} else { /* disconnected */
				/* set device reset flag */
				dev->is_reset = 0;
				stop_activity(dev, dev->driver);
				DEBUG(dev, "disconnect %s\n", dev->driver->driver.name);
				dev->ep0state = EP0_DISCONNECT;
				VDEBUG(dev, "ep0state: EP0_DISCONNECT\n");
			}
		}
	}
	
	/* host suspend interrupt indication */
	if (stat & SUSPEND_INTR) {
		DEBUG(dev, "SUSPEND interrupt: stat = 0x%08x\n", stat);
		/* ACK SUSPEND_INTR */
		stat &= ~SUSPEND_INTR;
		dev->irqs++;
		VDEBUG(dev, "dev->irqs: %lu\n", dev->irqs);

		/* call gadget driver suspend/resume routines */
		if (dev->suspended) {
			if (dev->driver->suspend) {
				spin_unlock(&dev->lock);
				dev->driver->suspend(&dev->gadget);
				spin_lock(&dev->lock);
			}
			DEBUG(dev, "suspend %s\n", dev->driver->driver.name);
		} else {
			if (dev->driver->resume) {
				spin_unlock(&dev->lock);
				dev->driver->resume(&dev->gadget);
				spin_lock(&dev->lock);
			}
			DEBUG(dev, "resume %s\n", dev->driver->driver.name);
		}
	}

	/* if haven't USB Reset yet, wait for the next USB reset interrupt */
	if (!dev->is_reset) {
		DEBUG(dev, "Skip other interrupts before RESET\n");
	}

end:
	VDEBUG(dev, "handle device_irq finish: int_sts = 0x%08x, "
			"stat = 0x%08x\n", dev->int_sts, stat);

	VDEBUG(dev, "<--- handle_device_irqs() \n");
}


static void handle_ep0_irqs(struct iusbc *dev)
{
	volatile u16		ep_sts, val_16;
	u32			stat;
	u8			setup_pkt_sts;
	struct iusbc_request	*req;

	VDEBUG(dev, "---> handle_ep0_irqs() \n");

	stat = dev->int_sts;
	VDEBUG(dev, "stat = 0x%08x\n", stat);

	/* ep0-out interrupt */
	if (stat & EP0_OUT_INTR) {
		dev->ep[1].irqs++;
		VDEBUG(dev, "%s.irqs = %lu\n",
				dev->ep[1].ep.name ,dev->ep[1].irqs);

		ep_sts = readw(&dev->regs->ep[1].ep_sts);
		VDEBUG(dev, "%s.ep_sts = 0x%04x\n",
				dev->ep[1].ep.name, ep_sts);

		/* W1C ep0-out status register */
		writew(ep_sts, &dev->regs->ep[1].ep_sts);

		if ((ep_sts & BAD_PID_TYPE)
				|| (ep_sts & CRC_ERROR)
				|| (ep_sts & FIFO_ERROR)
				|| (ep_sts & DMA_ERROR)
				|| (ep_sts & DMA_IOC)) {
			DEBUG(dev, "%s error: 0x%04x \n",
					dev->ep[1].ep.name, ep_sts);
		}

		if (ep_sts & TRANS_COMPLETE) {
			VDEBUG(dev, "handle ep0-out interrupt\n");

			setup_pkt_sts = readb(&dev->regs->ep[1].setup_pkt_sts);
			VDEBUG(dev, "setup_pkt_sts = 0x%02x\n", setup_pkt_sts);
			/* ep0-out SETUP packet */
			if (setup_pkt_sts) {
				VDEBUG(dev, "ep0-out SETUP packet\n");

				/* W1C ep0-out Setup Packet Status Register */ 
				writeb(1, &dev->regs->ep[1].setup_pkt_sts);

				/* read ep0-out Setup Packet Register
				 * then handle ep0-out SETUP packet 
				 */
				ep0_setup(dev);
			} else {
				/* ep0-out standard OUT packet */
				DEBUG(dev, "ep0-out OUT packet\n");
				if (!list_empty(&dev->ep[1].queue)) {
					req = list_entry(dev->ep[1].queue.next,
							struct iusbc_request,
							queue);
					VDEBUG(dev, "dmacount = %d\n",
							req->td->dmacount);
					dma_done(&dev->ep[1], req, 0);
					VDEBUG(dev, "%s dma_done()\n",
						dev->ep[1].ep.name);
				}

				/* handle next standard OUT packet */
				if (!list_empty(&dev->ep[1].queue)) {
					restart_dma(&dev->ep[1]);
					VDEBUG(dev, "%s restart_dma()\n",
						dev->ep[1].ep.name);
				}
			}
		} else {
			/* WORKAROUND: FIFO_ERROR TC=0  */ 
			if (ep_sts & FIFO_ERROR)
				DEBUG(dev, "ep0-out FIFO_ERROR, TC=0\n");
		}

		/* enable DMA again */
		val_16 = readw(&dev->regs->ep[1].ep_cfg);
		val_16 |= EP_ENABLE;
		writew(val_16, &dev->regs->ep[1].ep_cfg);

		val_16 = readw(&dev->regs->ep[1].ep_cfg);
		VDEBUG(dev, "enable %s DMA transfer...\n",
				dev->ep[1].ep.name);
		VDEBUG(dev, "%s EP_ENABLE again, ep_cfg=0x%04x\n",
				dev->ep[1].ep.name, val_16);
	}

	/* ep0-in interrupt */
	if (stat & EP0_IN_INTR) {
		dev->ep[0].irqs++;
		VDEBUG(dev, "%s.irqs = %lu\n",
				dev->ep[0].ep.name, dev->ep[0].irqs);

		ep_sts = readw(&dev->regs->ep[0].ep_sts);
		VDEBUG(dev, "%s.ep_sts = 0x%04x\n",
				dev->ep[0].ep.name, ep_sts);

		/* W1C ep0-in status register */
		writew(ep_sts, &dev->regs->ep[0].ep_sts);

		if ((ep_sts & BAD_PID_TYPE)
				|| (ep_sts & CRC_ERROR)
				|| (ep_sts & FIFO_ERROR)
				|| (ep_sts & DMA_ERROR)
				|| (ep_sts & DMA_IOC)) {
			DEBUG(dev, "%s error: 0x%04x \n",
					dev->ep[0].ep.name, ep_sts);
		}

		if (ep_sts & TRANS_COMPLETE) {
			VDEBUG(dev, "handle ep0-in interrupt\n");
			if (!list_empty(&dev->ep[0].queue)) {
				req = list_entry(dev->ep[0].queue.next,
						struct iusbc_request,
						queue);
				VDEBUG(dev, "dmacount = %d\n", req->td->dmacount);
				dma_done(&dev->ep[0], req, 0);
				VDEBUG(dev, "%s dma_done()\n",
						dev->ep[0].ep.name);
			}

			/* handle next standard IN packet */
			if (!list_empty(&dev->ep[0].queue)) {
				restart_dma(&dev->ep[0]);
				VDEBUG(dev, "%s restart_dma()\n",
						dev->ep[0].ep.name);
			}
		} else {
			if (ep_sts & FIFO_ERROR)
				DEBUG(dev, "ep0-in FIFO_ERROR, TC=0\n");
		}

	}

	VDEBUG(dev, "<--- handle_ep0_irqs() \n");
}


static void handle_ep_irqs(struct iusbc *dev)
{
	volatile u16		ep_sts;
	u32			stat;
	unsigned		i;
	struct iusbc_request	*req;

	VDEBUG(dev, "---> handle_ep_irqs() \n");

	stat = dev->int_sts;
	VDEBUG(dev, "stat = 0x%08x\n", stat);

	/* ep1in-bulk, ep1out-bulk and ep2in-int */
	for (i = 2; i < 5; i++) { 
		if ((1 << i) & stat) {
			dev->ep[i].irqs++;
			ep_sts = readw(&dev->regs->ep[i].ep_sts);
			if (ep_sts)
				VDEBUG(dev, "%s.ep_sts = 0x%04x\n",
					dev->ep[i].ep.name, ep_sts);

			/* W1C ep status register */
			writew(ep_sts, &dev->regs->ep[i].ep_sts);

			if ((ep_sts & BAD_PID_TYPE)
					|| (ep_sts & CRC_ERROR)
					|| (ep_sts & FIFO_ERROR)
					|| (ep_sts & DMA_ERROR)
					|| (ep_sts & DMA_IOC)) {
				DEBUG(dev, "%s error: 0x%04x \n",
					dev->ep[i].ep.name, ep_sts);
			}

			if (ep_sts & TRANS_COMPLETE) {
				VDEBUG(dev, "handle %s interrupt\n",
						dev->ep[i].ep.name);
				VDEBUG(dev, "data ep dma TRANS_COMPLETE\n");
				if (!list_empty(&dev->ep[i].queue)) {
					req = list_entry(dev->ep[i].queue.next,
							struct iusbc_request,
							queue);
					VDEBUG(dev, "dmacount = %d\n",
							req->td->dmacount);
					dma_done(&dev->ep[i], req, 0);
					VDEBUG(dev, "%s dma_done()\n",
						dev->ep[i].ep.name);
				}

				/* handle next standard OUT and IN packet */
				if (!list_empty(&dev->ep[i].queue)) {
					restart_dma(&dev->ep[i]);
					VDEBUG(dev, "%s restart_dma()\n",
						dev->ep[i].ep.name);
				}
			} else {
				if (ep_sts & FIFO_ERROR)
					DEBUG(dev, "%s FIFO_ERROR, TC=0\n",
							dev->ep[i].ep.name);
			}
		}
	}

	VDEBUG(dev, "<--- handle_ep_irqs() \n");
}


static void iusbc_do_tasklet(unsigned long arg)
{
	u32		val_32;
	volatile u32 dev_sts;
	struct iusbc	*dev;
	dev = (struct iusbc *) arg;
	DEBUG(dev, "---> iusbc_do_tasklet() \n");

	spin_lock(&dev->lock);
    if (!dev->running || !dev->driver) {
        spin_unlock(&dev->lock);
        return;
    }

	/* disable irqs, will re-enable later */
	writel(0, &dev->regs->int_ctrl);

	/* device-wide reset, connect, suspend */
	if (dev->int_sts & (RESET_INTR | CONNECT_INTR | SUSPEND_INTR)) {
		DEBUG(dev, "iusbc_do_tasklet -> handle_device_irqs\n");
		handle_device_irqs(dev);
	}

	/* QQQ: disable Full speed mode for D1 hw issues. */ 
	dev_sts = readl(&dev->regs->dev_sts);
	if ((dev_sts & RATE) && (dev_sts & CONNECTED)) {
		/* ep0-in/out control requests and interrupt */
		if (dev->int_sts & (EP0_IN_INTR | EP0_OUT_INTR)) {
			DEBUG(dev, "iusbc_do_tasklet -> handle_ep0_irqs\n");
			handle_ep0_irqs(dev);
		}
		/* data endpoints requests interrupt */
		if (dev->int_sts & (EP1_IN_INTR | EP1_OUT_INTR | EP2_IN_INTR)) {
			DEBUG(dev, "iusbc_do_tasklet -> handle_ep_irqs\n");
			handle_ep_irqs(dev);
		}
	} else if (dev_sts & CONNECTED) {
		stop_activity(dev, dev->driver);
		dev->ep0state = EP0_DISCONNECT;
		/*iusbc_pullup(&dev->gadget, 0);*/
	}	

	/* enable irqs again */
	val_32 = 0;
	val_32 |= RESET_INTR_ENABLE
		| CONNECT_INTR_ENABLE
		| SUSPEND_INTR_ENABLE
		/* | EP3_OUT_INTR_ENABLE */
		/* | EP3_IN_INTR_ENABLE */
		/* | EP2_OUT_INTR_ENABLE */
		| EP2_IN_INTR_ENABLE
		| EP1_OUT_INTR_ENABLE
		| EP1_IN_INTR_ENABLE
		| EP0_OUT_INTR_ENABLE
		| EP0_IN_INTR_ENABLE;

	writel(val_32, &dev->regs->int_ctrl);
	val_32 = readl(&dev->regs->int_ctrl);
	DEBUG(dev, "enable irqs again in iusbc_do_tasklet(), "
			"int_ctrl = 0x%08x\n", val_32);

	spin_unlock(&dev->lock);

	DEBUG(dev, "<--- iusbc_do_tasklet() \n");
}


static irqreturn_t iusbc_irq(int irq, void *_dev)
{
	struct iusbc	*dev = _dev;
	u32		int_sts,
			int_ctrl,
			val_32;

	VDEBUG(dev, "---> iusbc_irq() \n");

	/* interrupt control */
	int_ctrl = readl(&dev->regs->int_ctrl);
	VDEBUG(dev, "int_ctrl = 0x%08x\n", int_ctrl);

	/* interrupt status */
	int_sts = readl(&dev->regs->int_sts);
	VDEBUG(dev, "int_sts = 0x%08x\n", int_sts);

	if (!int_sts || !int_ctrl) {
		VDEBUG(dev, "handle IRQ_NONE\n");
		VDEBUG(dev, "<--- iusbc_irq() \n");
		return IRQ_NONE;
	}

	/* disable irqs, will re-enable later */
	writel(0, &dev->regs->int_ctrl);

	/* W1C interrupt status register */
	writel(int_sts, &dev->regs->int_sts);
	val_32 = readl(&dev->regs->int_sts);
	VDEBUG(dev, "W1C: regs->int_sts = 0x%08x\n", val_32);

	/* for iusbc_tasklet */
	dev->int_sts = int_sts;

	/* check device status */
	val_32 = readl(&dev->regs->dev_sts);
	DEBUG(dev, "iusbc_irq: dev_sts = 0x%08x\n", val_32);

	/* schedule irq tasklet */
	tasklet_schedule(&dev->iusbc_tasklet);

	VDEBUG(dev, "<--- iusbc_irq() \n");
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void gadget_release(struct device *_dev)
{
	struct iusbc	*dev;
#ifdef VERBOSE
	printk(KERN_DEBUG "---> gadget_release() \n");
#endif

	dev = dev_get_drvdata(_dev);
	kfree(dev);

#ifdef VERBOSE
	printk(KERN_DEBUG "<--- gadget_release() \n");
#endif
}


/* tear down the binding between this driver and the pci device */
static void iusbc_remove(struct pci_dev *pdev)
{
	struct iusbc		*dev;

	dev = pci_get_drvdata(pdev);

	if (dev->udc_registered)
		usb_del_gadget(&dev->gadget);

	VDEBUG(dev, "---> iusbc_remove() \n");

	if (dev->regs)
        writel(0, &dev->regs->int_ctrl);
    if (dev->got_irq)
        synchronize_irq(pdev->irq);
    tasklet_kill(&dev->iusbc_tasklet);

    /* then clean up the resources we allocated during probe() */
    if (dev->status_req) {
        kfree(dev->status_req->buf);
        iusbc_free_request(&dev->ep[0].ep, dev->status_req);
    }
	if (dev->requests) {
		unsigned	i;
		/* 2 ep0, 3 data ep */
		for (i = 0; i < 5; i++) {
			if (!dev->ep[i].dummy)
				continue;

			dma_pool_free(dev->requests, dev->ep[i].dummy,
					dev->ep[i].td_dma);
		}
		dma_pool_destroy(dev->requests);
	}

	if (dev->got_irq)
		free_irq(pdev->irq, dev);
	tasklet_kill(&dev->iusbc_tasklet);

	if (dev->regs)
		iounmap(dev->regs);

	if (dev->region)
		release_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));

	if (dev->enabled)
		pci_disable_device(pdev);

	
	device_remove_file(&pdev->dev, &dev_attr_registers);
	pci_set_drvdata(pdev, NULL);

	INFO(dev, "unbind\n");

	the_controller = NULL;

	if (dev->gadget_initialized)
		usb_put_gadget(&dev->gadget);
	else
		kfree(dev);
}


/* wrap this driver around the specified device, but
 * don't respond over USB until a gadget driver binds to us.
 */
static int iusbc_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct iusbc		*dev;
	unsigned long		resource, len;
	void			__iomem *base = NULL;
	int			retval;
	unsigned		i;
	u32			val_32;

	/* if you want to support more than one controller in a system,
	 * usb_gadget_{register,unregister}_driver() must change.
	 */
	if (the_controller) {
		dev_warn(&pdev->dev, "ignoring\n");
		return -EBUSY;
	}

	/* alloc, and start init */
	dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (dev == NULL){
		retval = -ENOMEM;
		goto done;
	}

	pci_set_drvdata(pdev, dev);
	spin_lock_init(&dev->lock);
	tasklet_init(&dev->iusbc_tasklet, iusbc_do_tasklet, (unsigned long) dev);
	dev->pdev = pdev;

	DEBUG(dev, "---> iusbc_probe() \n");

	dev->gadget.ops = &iusbc_ops;
	dev->gadget.max_speed = USB_SPEED_HIGH; /* support high/full speed */

	/* the "gadget" abstracts/virtualizes the controller */
	/* The UDC core names and registers gadget.dev. */
	dev->gadget.dev.parent = &pdev->dev;
	dev->gadget.dev.dma_mask = pdev->dev.dma_mask;
	
	dev->gadget.name = driver_name;

	/* now all the pci goodies ... */
	if (pci_enable_device(pdev) < 0) {
	        retval = -ENODEV;
		goto done;
	}

	/* WORKAROUND: USB port routing bug */
	pci_write_config_byte(pdev, 0x40, 0x82);

	dev->enabled = 1;
	retval = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (retval)
		goto done;

	/* control register: BAR 0 */
	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	if (!request_mem_region(resource, len, driver_name)) {
		DEBUG(dev, "controller already in use\n");
		retval = -EBUSY;
		goto done;
	}
	dev->region = 1;

	base = ioremap(resource, len);
	if (base == NULL) {
		DEBUG(dev, "can't map memory\n");
		retval = -EFAULT;
		goto done;
	}
	dev->regs = (struct iusbc_regs __iomem *) base;

	/* global capabilities, ep number and dma modes */
	val_32 = readl(&dev->regs->gcap);
	if (val_32 & TRANSFER_MODE_CAP)
		dev->transfer_mode_dma = 1;

	if (val_32 & SCATTER_GATHER_MODE_CAP)
		dev->sg_mode_dma = 1;

	if (val_32 & LINEAR_MODE_CAP)
		dev->linear_mode_dma = 1;

	if (val_32 & CONTROL_MODE_CAP)
		dev->control_mode_dma = 1;

	dev->ep_cap = (val_32 >> 28);

	/* reset all registers */ 
	writel(0, &dev->regs->dev_ctrl);

	/* init usb client controller device */
	iusbc_reset(dev);
	iusbc_reinit(dev);

	/* irq setup after old hardware is cleaned up */
	if (!pdev->irq) {
		ERROR(dev, "No IRQ. Check PCI setup!\n");
		retval = -ENODEV;
		goto done;
	}

	if (request_irq(pdev->irq, iusbc_irq, IRQF_SHARED, driver_name, dev)
			!= 0) {
		ERROR(dev, "request interrupt %d failed\n", pdev->irq);
		retval = -EBUSY;
		goto done;
	}
	dev->got_irq = 1;

	/* ------------------------------------------------------------------ */
	/* DMA setup
	 * NOTE: we know only the 32 LSBs of dma addresses may be nonzero
	 */
	dev->requests = dma_pool_create("requests", &pdev->dev,
		sizeof(struct iusbc_dma),
		0 /* no alignment requirements */,
		0 /* or page-crossing issues */);

	if (!dev->requests) {
		DEBUG(dev, "can't get request pool\n");
		retval = -ENOMEM;
		goto done;
	}

	/* assgined DMA */
	/* 2 ep0, 3 data ep */
	for (i = 0; i < 5 ; i++) {
		struct iusbc_dma	*td;
		td = dma_pool_alloc(dev->requests,
				GFP_KERNEL,
				&dev->ep[i].td_dma);

		if (!td) {
			DEBUG(dev, "can't get dummy %d\n", i);
			retval = -ENOMEM;
			goto done;
		}

		td->dmacount = 0;	/* not VALID */
		td->dmaaddr = __constant_cpu_to_le32(DMA_ADDR_INVALID);

		dev->ep[i].dummy = td;
	}

    dev->status_req = iusbc_alloc_request(&dev->ep[0].ep, GFP_KERNEL);
    if (!dev->status_req) {
        retval = -ENOMEM;
        goto done;
    }
    dev->status_req->buf = kmalloc(2, GFP_KERNEL);
    if (!dev->status_req->buf) {
        retval = -ENOMEM;
        goto done;
    }

	/* enables bus-mastering for device dev */
	pci_set_master(pdev);

	/* ------------------------------------------------------------------ */
	/* done */
	INFO(dev, "%s\n", driver_desc);
	INFO(dev, "irq %d, pci mem %p\n", pdev->irq, base);
	INFO(dev, "version: " DRIVER_VERSION "\n");
	INFO(dev, "support (max) %d endpoints\n", dev->ep_cap * 2);

#ifdef VERBOSE
	/* only for debugging, print mapped memory registers */
	VDEBUG(dev, "After iusbc_probe(), print register values:\n");
	print_all_registers(dev->regs);
#endif

	the_controller = dev;
	usb_initialize_gadget(&pdev->dev, &dev->gadget, gadget_release);
	dev->gadget_initialized = 1;
	retval = usb_add_gadget(&dev->gadget);
	if (!retval) dev->udc_registered = 1;

	if (retval)
		goto done;

	retval = device_create_file(&pdev->dev, &dev_attr_registers);

	if (retval)
		goto done;

	DEBUG(dev, "<--- iusbc_probe() \n");

	return 0;

done:
	if (dev)
		iusbc_remove(pdev);

	DEBUG(dev, "<--- iusbc_probe() \n");

	return retval;
}


#ifdef CONFIG_PM
/* client suspend */
static int iusbc_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iusbc	*dev;
	unsigned long	flags;

	dev = pci_get_drvdata(pdev);

	DEBUG(dev, "---> iusbc_suspend() \n");

	spin_lock_irqsave(&dev->lock, flags);
	dev->running = 0;
	writel(0, &dev->regs->int_ctrl);
	spin_unlock_irqrestore(&dev->lock, flags);
	synchronize_irq(pdev->irq);
	tasklet_kill(&dev->iusbc_tasklet);

	spin_lock_irqsave(&dev->lock, flags);
	stop_activity(dev, dev->driver);
	spin_unlock_irqrestore(&dev->lock, flags);

	iusbc_pullup(&dev->gadget, 0);

	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	DEBUG(dev, "<--- iusbc_suspend() \n");

	return 0;
}

/* client resume */
static int iusbc_resume(struct pci_dev *pdev)
{
	struct iusbc	*dev;

	dev = pci_get_drvdata(pdev);

	DEBUG(dev, "---> iusbc_resume() \n");

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (dev->driver)
        device_start(dev);

	DEBUG(dev, "<--- iusbc_resume() \n");

	return 0;
}
#endif


static void iusbc_shutdown(struct pci_dev *pdev)
{
	struct iusbc	*dev;
	u32		val_32;

	dev = pci_get_drvdata(pdev);

	DEBUG(dev, "---> iusbc_shutdown() \n");

	/* disable irqs */
	writel(0, &dev->regs->int_ctrl);

	/* reset all registers */ 
	writel(0, &dev->regs->dev_ctrl);

	val_32 = readl(&dev->regs->dev_ctrl);
	DEBUG(dev, "dev_ctrl = 0x%08x\n", val_32);

	DEBUG(dev, "<--- iusbc_shutdown() \n");
}

/*-------------------------------------------------------------------------*/

static const struct pci_device_id pci_ids [] = { {
	.class =	((PCI_CLASS_SERIAL_USB << 8) | 0x80),
	.class_mask =	~0,
	.vendor =	0x8086, /* Intel */
	.device =	0x8118, /* Poulsbo USB Client Controller */
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver iusbc_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	iusbc_probe,
	.remove =	iusbc_remove,

#ifdef CONFIG_PM
	.suspend =	iusbc_suspend,	
	.resume =	iusbc_resume,
#endif

	.shutdown =	iusbc_shutdown,
};


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Xiaochen Shen: xiaochen.shen@intel.com");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");


static int __init init(void)
{
	return pci_register_driver(&iusbc_pci_driver);
}
module_init(init);


static void __exit cleanup(void)
{
	pci_unregister_driver(&iusbc_pci_driver);
}
module_exit(cleanup);
