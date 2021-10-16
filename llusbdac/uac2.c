/*
 * f_uac2.c -- USB Audio Class 2.0 Function
 *
 * Copyright (C) 2011
 *    Yadwinder Singh (yadi.brar01@gmail.com)
 *    Jaswinder Singh (jaswinder.singh@linaro.org)
 * Copyright (C) 2017 Mediatek Inc.
 * Copyright 2015,2016,2017 Sony Corporation
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *         Benoit Goby <benoit@android.com>
 * Copyright (C) 2021 Zhang Boyang (zhangboyang.id@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/usb/composite.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/kallsyms.h>
#include <linux/kthread.h>

#include "llusbdac.h"

// dirty things
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/musb/musb_core.h>
static void (**p_pf_get_uac2_buf_status)(u32 *status);
#define pf_get_uac2_buf_status (*p_pf_get_uac2_buf_status)

#ifndef __LITTLE_ENDIAN
#error
#endif

#define MAX_ALT 3 // if you modify this, must also modify all xrefs!
static const struct {
	u8 bSubslotSize;
	u8 bBitResolution;
} alt_params[1 + MAX_ALT] = {
	{ }, // dummy
	{2, 16},   // alt1: 16 bits in 2 bytes
	{3, 24},   // alt2: 24 bits in 3 bytes
	{4, 32},   // alt3: 32 bits in 4 bytes
	// xref: MAX_ALT
};

/* Keep everyone on toes */
#define USB_XFERS	4

/*
 * The driver implements a simple UAC_2 topology.
 * USB-OUT -> IT_1 -> OT_2 -> ALSA_Capture
 *    CLK_3 := c_srate
 */
enum {
	USB_OUT_IT_ID = 1,
	IO_OUT_OT_ID,
	USB_OUT_CLK_ID,
};

// Audio Channel Cluster Descriptor
#define FL_CHAN  1  // front left
#define FR_CHAN  2  // front right
#define CHAN_CONF  (FL_CHAN|FR_CHAN)

#define CONTROL_ABSENT	0
#define CONTROL_RDONLY	1
#define CONTROL_RDWR	3

#define CLK_FREQ_CTRL	0
#define CLK_VLD_CTRL	2

#define COPY_CTRL	0
#define CONN_CTRL	2
#define OVRLD_CTRL	4
#define CLSTR_CTRL	6
#define UNFLW_CTRL	8
#define OVFLW_CTRL	10

struct uac2_req {
	struct audio_dev *pp; /* parent param */
	struct usb_request *req;
};

struct audio_dev {
	u8 ac_intf, ac_alt;
	u8 as_out_intf, as_out_alt;
	unsigned rate, bits;

	bool ep_enabled;
	struct usb_ep *out_ep;
	struct usb_ep *fb_ep;
	struct usb_function func;

	void *rbuf;
	unsigned max_psize;
	struct uac2_req ureq[USB_XFERS];
};

static struct audio_dev *agdev_g;

static void reset_uac_params(struct audio_dev *agdev)
{
	agdev->rate = 44100;
}

static inline
struct audio_dev *func_to_agdev(struct usb_function *f)
{
	return container_of(f, struct audio_dev, func);
}

#define FEEDBACK_PERIOD 8   // xref: hs_epfb_desc.bInterval, do_dirty_musb_hook
static unsigned fb_low_limit, fb_high_limit;
void set_feedback_limits_base10000(unsigned low_limit, unsigned high_limit)
{
	BUG_ON(low_limit > 10000 || high_limit < 10000);
	fb_low_limit = low_limit;
	fb_high_limit = high_limit;
}
static void feedback_buffer_status(u32 *status)
{
	BUG_ON(((u64)MAX_RATE << 13) != ((u32)MAX_RATE << 13));
	unsigned reference = (agdev_g->rate << 13) / 1000;
	//*status = reference; return;
	unsigned ref_low = min_t(unsigned, div_u64((u64)reference * fb_low_limit, 10000), reference - (1<<3));
	unsigned ref_high = max_t(unsigned, div_u64((u64)reference * fb_high_limit, 10000), reference + (1<<3));
	
	unsigned inflight_frames, target_frames;
	int is_running = ringbuf_report(&inflight_frames, &target_frames);

	//printk("is_running=%d inflight_frames=%u target_frames=%u diff=%d reference=%08x [%08x,%08x]\n", is_running, inflight_frames, target_frames, target_frames - inflight_frames, reference, ref_low, ref_high);

	s64 feedback;
	if (is_running) {
		feedback = reference + ((s64)((s32)(target_frames - inflight_frames) / FEEDBACK_PERIOD) << 13);
		//printk("feedback=%08llx\n", feedback);
		feedback = max_t(s64, feedback, ref_low);
		feedback = min_t(s64, feedback, ref_high);
	    uac_stats.buf_frames = inflight_frames;
	} else {
		feedback = ref_low;
	}
	*status = feedback;
	//printk("feedback_buffer_status status=%08x\n",  *status);
}


#define MAX_ISO_BYTES 512
static void agdev_iso_complete(struct usb_ep *ep, struct usb_request *req)
{
	// WORKAROUND BUGGY USB STACK
	if (req->dma && req->dma != DMA_ADDR_INVALID)
		dma_sync_single_for_cpu(NULL, req->dma, ALIGN(req->length, dma_get_cache_alignment()), DMA_FROM_DEVICE);

	//printk("agdev_iso_complete\n");
	struct uac2_req *ur = req->context;
	struct audio_dev *agdev = ur->pp;
	int status = req->status;
	
	/* i/f shutting down */
	if (!agdev->ep_enabled)
		return;

	/*
	 * We can't really do much about bad xfers.
	 * Afterall, the ISOCH xfers could fail legitimately.
	 */
	if (status) {
		pr_debug("%s: iso_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);
		uac_stats.err.usb++;
	}

	//printk("agdev_iso_complete buf=%p len=%u\n", req->buf, req->actual);
	void *buf = req->buf;
	unsigned len = req->actual;

	if (len % (agdev->bits / 8 * 2) == 0) {
		if (len > 0) {
			if (agdev->bits == 24) {
				static u8 cvtd[MAX_ISO_BYTES / 6 * 8];
				BUG_ON(len / 6 * 8 > sizeof(cvtd));
				u8 *src = buf;
				u8 *dst = cvtd;
				for (unsigned i = 0; i < len / 3; i++) {
					*dst++ = 0;
					*dst++ = *src++;
					*dst++ = *src++;
					*dst++ = *src++;
				}
				buf = cvtd;
				len = len / 6 * 8;
			}

			if (ringbuf_push(buf, len) != len) {
				const static u8 zeros[MAX_ISO_BYTES] = {0};
				BUG_ON(req->actual > sizeof(zeros));
				if (memcmp(req->buf, zeros, req->actual) != 0) {
					uac_stats.err.overrun++;
					printk("OVERRUN=%llu\n", uac_stats.err.overrun);
				}
			}
		}
	} else {
		printk("bad packet length buf=%p len=%u\n", req->buf, req->actual);
		uac_stats.err.usb++;
	}

	//memset(req->buf, 0xCC, agdev->max_psize);
	//dma_sync_single_for_cpu(NULL, req->dma, agdev->max_psize, DMA_BIDIRECTIONAL);

	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		pr_err( "%d Error!\n", __LINE__);

	return;
}



/* --------- USB Function Interface ------------- */

#if 0
enum {
	STR_ASSOC,
	STR_IF_CTRL,
	STR_CLKSRC_OUT,
	STR_USB_IT,
	STR_IO_OT,
	STR_AS_OUT_ALT0,
	STR_AS_OUT_ALTN,
};
static struct usb_string strings_fn[] = {
	[STR_ASSOC].s = "Source/Sink",
	[STR_IF_CTRL].s = "Topology Control",
	[STR_CLKSRC_OUT].s = "Clock Source",
	[STR_USB_IT].s = "USBH Out",
	[STR_IO_OT].s = "USBD In",
	[STR_AS_OUT_ALT0].s = "Playback Inactive",
	[STR_AS_OUT_ALTN].s = "Playback Active",
	{ },
};
#else
#define STR_ASSOC 0
#define STR_IF_CTRL 0
#define STR_CLKSRC_OUT 0
#define STR_USB_IT 0
#define STR_IO_OT 0
#define STR_AS_OUT_ALT0 0
#define STR_AS_OUT_ALTN 0
static struct usb_string strings_fn[] = {
	[0].s = LLUSBDAC_NAME,
	{ },
};
#endif

static struct usb_gadget_strings str_fn = {
	.language = 0x0409,	/* en-us */
	.strings = strings_fn,
};

static struct usb_gadget_strings *fn_strings[] = {
	&str_fn,
	NULL,
};

// Interface Association Descriptor
static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength = sizeof iad_desc,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,

	.bFirstInterface = 0,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_AUDIO,
	.bFunctionSubClass = UAC2_FUNCTION_SUBCLASS_UNDEFINED,
	.bFunctionProtocol = UAC_VERSION_2,
};


// Standard AC Interface Descriptor
/* Audio Control Interface */
static struct usb_interface_descriptor std_ac_if_desc = {
	.bLength = sizeof std_ac_if_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = UAC_VERSION_2,
};



/* Clock source for OUT traffic */
static struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof out_clk_src_desc, // Clock Source Descriptor
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,

	.bClockID = USB_OUT_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_PROG,
	.bmControls = (CONTROL_RDWR << CLK_FREQ_CTRL) | (CONTROL_RDONLY << CLK_VLD_CTRL),
	.bAssocTerminal = IO_OUT_OT_ID,
};

/* Input Terminal for USB_OUT */
struct uac2_input_terminal_descriptor usb_out_it_desc = {
	.bLength = sizeof usb_out_it_desc, // Input Terminal Descriptor
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	.bTerminalID = USB_OUT_IT_ID,
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = IO_OUT_OT_ID,
	.bCSourceID = USB_OUT_CLK_ID,
	.bNrChannels = hweight32(CHAN_CONF),
	.bmChannelConfig = CHAN_CONF,
	.iChannelNames = 0,
	.bmControls = 0,
};

/* Ouput Terminal for I/O-Out */
struct uac2_output_terminal_descriptor io_out_ot_desc = {
	.bLength = sizeof io_out_ot_desc, // Output Terminal Descriptor
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	.bTerminalID = IO_OUT_OT_ID,
	.wTerminalType = cpu_to_le16(UAC_OUTPUT_TERMINAL_HEADPHONES),
	.bAssocTerminal = USB_OUT_IT_ID,
	.bSourceID = USB_OUT_IT_ID,
	.bCSourceID = USB_OUT_CLK_ID,
	.bmControls = 0,
};

struct uac2_ac_header_descriptor ac_hdr_desc = {
	.bLength = sizeof ac_hdr_desc, // Class-Specific AC Interface Descriptor
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_MS_HEADER,
	.bcdADC = cpu_to_le16(0x200),
	.bCategory = UAC2_FUNCTION_IO_BOX,
	.wTotalLength = sizeof out_clk_src_desc +
	                sizeof usb_out_it_desc +
	                sizeof io_out_ot_desc,
	.bmControls = 0,
};

// Standard AS Interface Descriptor
/* Audio Streaming OUT Interface - Alt0 */
static struct usb_interface_descriptor std_as_out_if0_desc = {
	.bLength = sizeof std_as_out_if0_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};
/* Audio Streaming OUT Interface - Alt[N] */
#define MAKE_STD_AS_OUT_IF_DESC(altid) \
static struct usb_interface_descriptor std_as_out_if##altid##_desc = { \
	.bLength = sizeof std_as_out_if##altid##_desc, \
	.bDescriptorType = USB_DT_INTERFACE, \
 \
	.bAlternateSetting = altid, \
	.bNumEndpoints = 2, \
	.bInterfaceClass = USB_CLASS_AUDIO, \
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING, \
	.bInterfaceProtocol = UAC_VERSION_2, \
}
MAKE_STD_AS_OUT_IF_DESC(1);
MAKE_STD_AS_OUT_IF_DESC(2);
MAKE_STD_AS_OUT_IF_DESC(3); // xref: MAX_ALT


// Class-Specific AS Interface Descriptor
/* Audio Stream OUT Intface Desc */
struct uac2_as_header_descriptor as_out_hdr_desc = {
	.bLength = sizeof as_out_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	.bTerminalLink = USB_OUT_IT_ID,
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.bNrChannels = hweight32(CHAN_CONF),
	.bmChannelConfig = CHAN_CONF,
	.iChannelNames = 0,
};
/* Audio USB_OUT Format */
#define MAKE_AS_OUT_FMT_DESC(altid) \
struct uac2_format_type_i_descriptor as_out_fmt##altid##_desc = { \
	.bLength = sizeof as_out_fmt##altid##_desc, \
	.bDescriptorType = USB_DT_CS_INTERFACE, \
	.bDescriptorSubtype = UAC_FORMAT_TYPE, \
	.bFormatType = UAC_FORMAT_TYPE_I, \
}
MAKE_AS_OUT_FMT_DESC(1);
MAKE_AS_OUT_FMT_DESC(2);
MAKE_AS_OUT_FMT_DESC(3); // xref: MAX_ALT

// AS Isochronous Audio Data Endpoint Descriptors
/* STD AS ISO OUT Endpoint */
struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = 512,
	.bInterval = 1,
};
/* CS AS ISO OUT Endpoint */
static struct uac2_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength = sizeof as_iso_out_desc,
	.bDescriptorType = USB_DT_CS_ENDPOINT,

	.bDescriptorSubtype = UAC_EP_GENERAL,
	.bmAttributes = 0,
	.bmControls = 0,
	.bLockDelayUnits = 0,
	.wLockDelay = 0,
};

// AS Isochronous Feedback Endpoint Descriptor
/* STD AS ISO Feedback Endpoint */
struct usb_endpoint_descriptor hs_epfb_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_USAGE_FEEDBACK,
	.wMaxPacketSize = 512,
	//.bInterval = 1,
	.bInterval = 4, // xref: FEEDBACK_PERIOD, do_dirty_musb_hook
};


static struct usb_descriptor_header *hs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,

#define MAKE_ALT_DESCRIPTOR(altid) \
	(struct usb_descriptor_header *)&std_as_out_if##altid##_desc, \
	(struct usb_descriptor_header *)&as_out_hdr_desc, \
	(struct usb_descriptor_header *)&as_out_fmt##altid##_desc, \
	(struct usb_descriptor_header *)&hs_epout_desc, \
	(struct usb_descriptor_header *)&as_iso_out_desc, \
	(struct usb_descriptor_header *)&hs_epfb_desc

	MAKE_ALT_DESCRIPTOR(1),
	MAKE_ALT_DESCRIPTOR(2),
	MAKE_ALT_DESCRIPTOR(3), // xref: MAX_ALT

	NULL
};

struct cntrl_cur_lay3 {
	__u32	dCUR;
};

#define N_SUPPORTED_RATES (14-5)
const static struct {
	__u16	wNumSubRanges;
	struct { __u32	dMIN, dMAX, dRES; } __packed SubRanges[N_SUPPORTED_RATES];
} __packed supported_rates = {
	N_SUPPORTED_RATES, {
		/*{ 8000, 8000, 0 },
		{ 11025, 11025, 0 },
		{ 16000, 16000, 0 },
		{ 22050, 22050, 0 },
		{ 32000, 32000, 0 },*/ // workaround buggy windows 10 usbaudio2.sys div-by-zero bsod problem
		{ 44100, 44100, 0 },
		{ 48000, 48000, 0 },
		{ 64000, 64000, 0 },
		{ 88200, 88200, 0 },
		{ 96000, 96000, 0 },
		{ 176400, 176400, 0 },
		{ 192000, 192000, 0 },
		{ 352800, 352800, 0 },
		{ 384000, 384000, 0 }, // xref: N_SUPPORTED_RATES, MAX_RATE
	},
};


static inline void
free_ep(struct audio_dev *agdev, struct usb_ep *ep)
{
	printk("free_ep\n");
	int i;

	agdev->ep_enabled = false;

	for (i = 0; i < USB_XFERS; i++) {
		if (agdev->ureq[i].req) {
			usb_ep_dequeue(ep, agdev->ureq[i].req);
			usb_ep_free_request(ep, agdev->ureq[i].req);
			agdev->ureq[i].req = NULL;
		}
	}

	if (usb_ep_disable(ep))
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
	
	if (usb_ep_disable(agdev->fb_ep))
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
}

static int
afunc_bind(struct usb_configuration *cfg, struct usb_function *fn)
{
	printk("afunc_bind\n");
	struct audio_dev *agdev = func_to_agdev(fn);
	struct usb_composite_dev *cdev = cfg->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	int ret;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_ac_if_desc.bInterfaceNumber = ret;
	agdev->ac_intf = ret;
	agdev->ac_alt = 0;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_out_if0_desc.bInterfaceNumber = ret;
#define SET_ALT_NUMBER(altid) do { \
	std_as_out_if##altid##_desc.bInterfaceNumber = ret; \
} while (0)
	SET_ALT_NUMBER(1);
	SET_ALT_NUMBER(2);
	SET_ALT_NUMBER(3); // xref: MAX_ALT

	agdev->as_out_intf = ret;
	agdev->as_out_alt = 0;

	agdev->out_ep = usb_ep_autoconfig(gadget, &hs_epout_desc);
	if (!agdev->out_ep) {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	agdev->out_ep->driver_data = agdev;

	agdev->fb_ep = usb_ep_autoconfig(gadget, &hs_epfb_desc);
	
	if (!agdev->fb_ep) {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	agdev->fb_ep->driver_data = agdev;

	//hs_epout_desc.bEndpointAddress = hs_epout_desc.bEndpointAddress;
	//hs_epout_desc.wMaxPacketSize = hs_epout_desc.wMaxPacketSize;

	ret = usb_assign_descriptors(fn, NULL, hs_audio_desc, NULL);
	if (ret)
		goto err;

	agdev->max_psize = hs_epout_desc.wMaxPacketSize;
	BUG_ON(agdev->max_psize > MAX_ISO_BYTES);
	agdev->rbuf = kzalloc(agdev->max_psize * USB_XFERS, GFP_KERNEL);
	printk("agdev->rbuf=%p\n", agdev->rbuf);
	if (!agdev->rbuf) {
		agdev->max_psize = 0;
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}

	return 0;
err:
	usb_free_all_descriptors(fn);
	if (agdev->out_ep)
		agdev->out_ep->driver_data = NULL;
	if (agdev->fb_ep)
		agdev->fb_ep->driver_data = NULL;
	return -EINVAL;
}

static void
afunc_unbind(struct usb_configuration *cfg, struct usb_function *fn)
{
	printk("afunc_unbind\n");
	struct audio_dev *agdev = func_to_agdev(fn);

	usb_free_all_descriptors(fn);

	if (agdev->out_ep)
		agdev->out_ep->driver_data = NULL;
	if (agdev->fb_ep)
		agdev->fb_ep->driver_data = NULL;
}

static int
afunc_set_alt(struct usb_function *fn, unsigned intf, unsigned alt)
{
	printk("afunc_set_alt intf=%u alt=%u\n", intf, alt);
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct usb_gadget *gadget = cdev->gadget;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;
	

	if (intf == agdev->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		return 0;
	}

	if (intf == agdev->as_out_intf) {
		if (alt > MAX_ALT) {
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		ep = agdev->out_ep;
		config_ep_by_speed(gadget, fn, ep);
		agdev->as_out_alt = alt;
		agdev->bits = alt_params[alt].bBitResolution;
		config_ep_by_speed(gadget, fn, agdev->fb_ep);
	} else {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (alt == 0) {
		ringbuf_clear(0, 0);
		uac_stats.running = 0;
		free_ep(agdev, ep);
		return 0;
	}

	
	uac_stats.running = 1;
	uac_stats.sample_bits = agdev->bits;
	uac_stats.n_frames = 0;
	memset((void *)&uac_stats.err, 0, sizeof(uac_stats.err));
	ringbuf_clear(agdev->rate, agdev->bits);
	
	if (!agdev->ep_enabled) {
		agdev->ep_enabled = true;
		if (usb_ep_enable(ep))
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
		if (usb_ep_enable(agdev->fb_ep))
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
	}

	for (i = 0; i < USB_XFERS; i++) {
		if (agdev->ureq[i].req) {
			continue;
		}

		req = usb_ep_alloc_request(ep, GFP_ATOMIC);
		if (req == NULL) {
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}

		agdev->ureq[i].req = req;
		agdev->ureq[i].pp = agdev;

		req->zero = 0;
		req->context = &agdev->ureq[i];
		req->length = agdev->max_psize;
		req->complete =	agdev_iso_complete;
		req->buf = agdev->rbuf + i * req->length;

		if (usb_ep_queue(ep, req, GFP_ATOMIC))
			pr_err("%d Error!\n", __LINE__);
	}
	return 0;
}

static int
afunc_get_alt(struct usb_function *fn, unsigned intf)
{
	printk("afunc_get_alt intf=%u\n", intf);
	struct audio_dev *agdev = func_to_agdev(fn);

	if (intf == agdev->ac_intf)
		return agdev->ac_alt;
	else if (intf == agdev->as_out_intf)
		return agdev->as_out_alt;
	else
		pr_err(
			"%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}

static void
afunc_disable(struct usb_function *fn)
{
	printk("afunc_disable\n");
	struct audio_dev *agdev = func_to_agdev(fn);

	free_ep(agdev, agdev->out_ep);
	agdev->as_out_alt = 0;
	ringbuf_clear(0, -1);

	uac_stats.running = 0;
	reset_uac_params(agdev);
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("in_rq_cur\n");
	struct audio_dev *agdev = func_to_agdev(fn);
	struct usb_request *req = fn->config->cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		struct cntrl_cur_lay3 c;
		memset(&c, 0, sizeof(c));

		if (entity_id == USB_OUT_CLK_ID)
			c.dCUR = agdev->rate;
		
		printk("UAC2_CS_CONTROL_SAM_FREQ=%u\n", c.dCUR);

		value = min_t(unsigned, w_length, sizeof c);
		memcpy(req->buf, &c, value);
	} else if (control_selector == UAC2_CS_CONTROL_CLOCK_VALID) {
		*(u8 *)req->buf = 1;
		printk("UAC2_CS_CONTROL_CLOCK_VALID=%d\n", *(u8 *)req->buf);
		value = min_t(unsigned, w_length, 1);
	} else {
		pr_err(
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
in_rq_range(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("in_rq_range\n");
	struct usb_request *req = fn->config->cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		if (entity_id != USB_OUT_CLK_ID)
			return -EOPNOTSUPP;

		value = min_t(unsigned, w_length, sizeof supported_rates);
		memcpy(req->buf, &supported_rates, value);
	} else {
		pr_err(
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
ac_rq_in(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("ac_rq_in\n");
	if (cr->bRequest == UAC2_CS_CUR)
		return in_rq_cur(fn, cr);
	else if (cr->bRequest == UAC2_CS_RANGE)
		return in_rq_range(fn, cr);
	else
		return -EOPNOTSUPP;
}

static void set_srate_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct audio_dev *agdev = req->context;

	struct cntrl_cur_lay3 c;
	memset(&c, 0, sizeof(c));
	memcpy(&c, req->buf, req->actual);
	for (int i = 0; i < N_SUPPORTED_RATES; i++) {
		if (supported_rates.SubRanges[i].dMIN == c.dCUR) {
			printk("req->actual=%d dCUR=%u\n", req->actual, c.dCUR);
			agdev->rate = c.dCUR;
			uac_stats.sample_rate = c.dCUR;
			ringbuf_clear(agdev->rate, agdev->bits);
			return;
		}
	}
	printk(KERN_ERR "unsupported sample rate %d\n", c.dCUR);
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("out_rq_cur\n");
	struct audio_dev *agdev = func_to_agdev(fn);
	struct usb_request *req = fn->config->cdev->req;
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;

	if (entity_id != USB_OUT_CLK_ID) {
		return -EOPNOTSUPP;
	}
	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		req->complete = set_srate_complete;
		req->context = agdev;
		return w_length;
	}

	return -EOPNOTSUPP;
}

static int
setup_rq_inf(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("setup_rq_inf\n");
	struct audio_dev *agdev = func_to_agdev(fn);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 intf = w_index & 0xff;

	if (intf != agdev->ac_intf) {
		pr_err(
			"%s:%d Error!\n", __func__, __LINE__);
		return -EOPNOTSUPP;
	}

	if (cr->bRequestType & USB_DIR_IN)
		return ac_rq_in(fn, cr);
	else if (cr->bRequest == UAC2_CS_CUR)
		return out_rq_cur(fn, cr);

	return -EOPNOTSUPP;
}

static int
afunc_setup(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	printk("afunc_setup\n");
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	int value = -EOPNOTSUPP;

	/* Only Class specific requests are supposed to reach here */
	if ((cr->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return -EOPNOTSUPP;

	if ((cr->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE)
		value = setup_rq_inf(fn, cr);
	else
		pr_err( "%s:%d Error!\n", __func__, __LINE__);

	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			pr_err(
				"%s:%d Error!\n", __func__, __LINE__);
			req->status = 0;
		}
	}

	return value;
}

//static void do_dirty_musb_hook(int en, struct musb *musb);

int audio_bind_config(struct usb_configuration *cfg)
{
	printk("audio_bind_config\n");

	lock_cpufreq(1);

	start_player();

	pf_get_uac2_buf_status = feedback_buffer_status;

	int res;

	agdev_g = kzalloc(sizeof *agdev_g, GFP_KERNEL);
	if (agdev_g == NULL) {
		printk(KERN_ERR "Unable to allocate audio gadget\n");
		return -ENOMEM;
	}

	reset_uac_params(agdev_g);

	res = usb_string_ids_tab(cfg->cdev, strings_fn);
	if (res)
		return res;
	iad_desc.iFunction = strings_fn[STR_ASSOC].id;
	std_ac_if_desc.iInterface = strings_fn[STR_IF_CTRL].id;
	out_clk_src_desc.iClockSource = strings_fn[STR_CLKSRC_OUT].id;
	usb_out_it_desc.iTerminal = strings_fn[STR_USB_IT].id;
	io_out_ot_desc.iTerminal = strings_fn[STR_IO_OT].id;
	std_as_out_if0_desc.iInterface = strings_fn[STR_AS_OUT_ALT0].id;

#define INIT_ALT_DESCRIPTOR(altid) do { \
	std_as_out_if##altid##_desc.iInterface = strings_fn[STR_AS_OUT_ALTN].id; \
	as_out_fmt##altid##_desc.bSubslotSize = alt_params[altid].bSubslotSize; \
	as_out_fmt##altid##_desc.bBitResolution = alt_params[altid].bBitResolution; \
} while (0)
	INIT_ALT_DESCRIPTOR(1);
	INIT_ALT_DESCRIPTOR(2);
	INIT_ALT_DESCRIPTOR(3); // xref: MAX_ALT

	agdev_g->func.name = "llusbdac";
	agdev_g->func.strings = fn_strings;
	agdev_g->func.bind = afunc_bind;
	agdev_g->func.unbind = afunc_unbind;
	agdev_g->func.set_alt = afunc_set_alt;
	agdev_g->func.get_alt = afunc_get_alt;
	agdev_g->func.disable = afunc_disable;
	agdev_g->func.setup = afunc_setup;

	res = usb_add_function(cfg, &agdev_g->func);
	if (res < 0)
		kfree(agdev_g);
	
	enable_gui(1);

	//do_dirty_musb_hook(1, gadget_to_musb(cfg->cdev->gadget));
	
	return res;
}

void uac2_unbind_config(struct usb_configuration *cfg)
{
	printk("uac2_unbind_config\n");
	//do_dirty_musb_hook(0, gadget_to_musb(cfg->cdev->gadget));
	enable_gui(-1);

	kfree(agdev_g);
	agdev_g = NULL;

	stop_player();

	lock_cpufreq(0);
}

int gadget_enabled(void)
{
	return !!agdev_g;
}




void uac2_init(void)
{
	IMPORT_KALLSYMS(pf_get_uac2_buf_status);
}