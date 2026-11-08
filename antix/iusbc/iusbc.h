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

#include <linux/interrupt.h>

/*
 * MEMORY SPACE REGISTERS
 */

struct iusbc_ep_regs {	/* 0x20 bytes */
	u32		ep_base_low_32;
	u32		ep_base_hi_32;
	u16		ep_len;
	u16		ep_pib;
	u16		ep_dil;
	u16		ep_tiq;
	u16		ep_max;
	u16		ep_sts;
#define		BAD_PID_TYPE		(1 << 15)
#define		CRC_ERROR		(1 << 14)
#define		FIFO_ERROR		(1 << 13)
#define		DMA_ERROR		(1 << 12)
#define		TRANS_COMPLETE		(1 << 11)
#define		PING_NAK_SENT		(1 << 10)
#define		DMA_IOC			(1 << 9)
	u16		ep_cfg;
#define		INTR_BAD_PID_TYPE	(1 << 15)
#define		INTR_CRC_ERROR		(1 << 14)
#define		INTR_FIFO_ERROR		(1 << 13)
#define		INTR_DMA_ERROR		(1 << 12)
#define		INTR_TRANS_COMPLETE	(1 << 11)
#define		INTR_PING_NAK_SENT	(1 << 10)
#define		INTR_DMA_IOC		(1 << 9)
#define		LINEAR_MODE		0	
#define		SCATTER_GATHER_MODE	1
#define		TRANSFER_MODE		2
#define		CONTROL_MODE		3
#define		EP_ENABLE		(1 << 1)
#define		EP_VALID		(1 << 0)
	u8		_unused;
	u8		setup_pkt_sts;
#define		SETUPPACKET_VALID	(1 << 0)
	u8		setup_pkt[8];
} __attribute__ ((packed));


struct iusbc_regs {
	/* offset 0x0000 */
	u32		gcap;
#define		DMA_IOC_CAP		(1 << 4)
#define		TRANSFER_MODE_CAP	(1 << 3)
#define		SCATTER_GATHER_MODE_CAP	(1 << 2)
#define		LINEAR_MODE_CAP		(1 << 1)
#define		CONTROL_MODE_CAP	(1 << 0)
	u8		_unused0[0x100-0x004];	

	/* offset 0x0100 */
	u32		dev_sts;
#define		RATE			(1 << 3)
#define		CONNECTED		(1 << 1)
#define		SUSPEND			(1 << 0)
	u16		frame;
	u8		_unused1[0x10c-0x106];
	
	/* offset 0x010c */
	u32		int_sts;
#define		RESET_INTR		(1 << 18)
#define		CONNECT_INTR		(1 << 17)
#define		SUSPEND_INTR		(1 << 16)
#define		EP3_OUT_INTR		(1 << 7)
#define		EP3_IN_INTR		(1 << 6)
#define		EP2_OUT_INTR		(1 << 5)
#define		EP2_IN_INTR		(1 << 4)
#define		EP1_OUT_INTR		(1 << 3)
#define		EP1_IN_INTR		(1 << 2)
#define		EP0_OUT_INTR		(1 << 1)
#define		EP0_IN_INTR		(1 << 0)
	u32		int_ctrl;
#define		RESET_INTR_ENABLE	(1 << 18)
#define		CONNECT_INTR_ENABLE	(1 << 17)
#define		SUSPEND_INTR_ENABLE	(1 << 16)
#define		EP3_OUT_INTR_ENABLE	(1 << 7)
#define		EP3_IN_INTR_ENABLE	(1 << 6)
#define		EP2_OUT_INTR_ENABLE	(1 << 5)
#define		EP2_IN_INTR_ENABLE	(1 << 4)
#define		EP1_OUT_INTR_ENABLE	(1 << 3)
#define		EP1_IN_INTR_ENABLE	(1 << 2)
#define		EP0_OUT_INTR_ENABLE	(1 << 1)
#define		EP0_IN_INTR_ENABLE	(1 << 0)
	u32		dev_ctrl;
#define		DEVICE_ENABLE		(1 << 31)
#define		CONNECTION_ENABLE	(1 << 30)
#define		DMA3_DISABLED		(1 << 15)
#define		DMA2_DISABLED		(1 << 14)
#define		DMA1_DISABLED		(1 << 13)
#define		DMA0_DISABLED		(1 << 12)
#define		CPU_SET_ADDRESS		(1 << 11)
#define		DISABLE_NYET		(1 << 9)
#define		TEST_MODE		(1 << 8)
#define		SIGNAL_RESUME		(1 << 4)
#define		CHARGE_ENABLE		(5 << 1)
#define		FORCE_FULLSPEED		(1 << 0)
	u8		_unused2[0x200-0x118];

	/* offset: 0x200, 0x220, ..., 0x2e0 */
	struct iusbc_ep_regs	ep[8];
} __attribute__ ((packed));


/*-------------------------------------------------------------------------*/

/* DRIVER DATA STRUCTURES and UTILITIES */

/* FIXME: for scatter/gather mode DMA */
struct iusbc_dma {
	__le16		dmacount;
	__le32		dmaaddr;		/* the buffer */
	__le32		dmadesc;		/* next dma descriptor */
	__le32		_reserved;
} __attribute__ ((aligned (16)));

struct iusbc_ep {
	struct usb_ep				ep;
	struct iusbc_dma			*dummy;
	dma_addr_t				td_dma;	/* of dummy */
	struct iusbc				*dev;
	unsigned long				irqs;

	/* analogous to a host-side qh */
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;
	unsigned				num : 8,
						stopped : 1,
						is_in : 1,
						dma_mode : 2,
						ep_type : 2;
};

struct iusbc_request {
	struct usb_request			req;
	struct iusbc_dma			*td;
	dma_addr_t				td_dma;
	struct list_head			queue;
	unsigned				mapped : 1,
						valid : 1;
};

enum ep0state {
	EP0_DISCONNECT,		/* no host */
	EP0_IDLE,		/* between STATUS ack and SETUP report */
	EP0_IN, EP0_OUT, 	/* data stage */
	EP0_STATUS,		/* status stage */
	EP0_STALL,		/* data or status stages */
	EP0_SUSPEND,		/* usb suspend */
};

struct iusbc {
	/* each pci device provides one gadget, several endpoints */
	struct usb_gadget		gadget;
	spinlock_t			lock;
	struct iusbc_ep			ep[8];
	struct usb_gadget_driver 	*driver;
	struct usb_request		*status_req;
	enum ep0state			ep0state;
	unsigned			ep_cap;
	unsigned			enabled : 1,
					powered : 1,
					enable_wakeup : 1,
					force_fullspeed : 1,
					rate : 1,
					connected : 1,
					suspended : 1,
					got_irq : 1,
					udc_registered : 1,
					gadget_initialized : 1,
					running : 1,
					region : 1,
					transfer_mode_dma : 1,
					sg_mode_dma : 1,
					linear_mode_dma : 1,
					control_mode_dma : 1,
					is_reset : 1;

	/* pci state used to access those endpoints */
	struct pci_dev			*pdev;
	struct iusbc_regs		__iomem *regs;
	struct dma_pool			*requests;

	/* statistics... */
	unsigned long			irqs;

	/* 16 bits status data for GET_STATUS */
	__le16				status_d;

	/* device irq tasklet */
	struct tasklet_struct		iusbc_tasklet;

	/* interrupt status register value */
	volatile u32			int_sts;
};

