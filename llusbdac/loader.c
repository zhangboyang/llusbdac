#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/usb/composite.h>
#include <linux/stop_machine.h>
#include <asm/cacheflush.h>

#include "llusbdac.h"

/////////////////////////////////////////////////////////////////////
//	import symbols from /proc/kallsyms

static struct class **p_android_class;
static struct android_dev **p__android_dev;
static struct android_usb_function **p_supported_functions;
#define android_class (*p_android_class)
#define _android_dev (*p__android_dev)
#define supported_functions p_supported_functions

/////////////////////////////////////////////////////////////////////
/*
	heavily modified  from  drivers/usb/gadget/android.c  (GPLv2)

	* Copyright (C) 2008 Google, Inc.
	* Author: Mike Lockwood <lockwood@android.com>
	*         Benoit Goby <benoit@android.com>
	* Copyright (C) 2017 Mediatek Inc.
*/
struct android_dev {
	struct android_usb_function **functions;
	struct list_head enabled_functions;
	struct usb_composite_dev *cdev;
	struct device *dev;

	bool enabled;
	int disable_depth;
	struct mutex mutex;
	bool connected;
	bool configured;
	bool sw_connected;
	bool sw_configured;
#if 0
#ifdef CONFIG_ENABLE_USBCONN
#ifdef USBCONN_SUSPEND_ENABLLE
	bool suspended;
	bool resumed;
#endif
#endif
#endif
	struct work_struct work;
	char ffs_aliases[256];
	int rezero_cmd;
};

struct android_usb_function {
	char *name;
	void *config;

	struct device *dev;
	char *dev_name;
	struct device_attribute **attributes;

	/* for android_dev.enabled_functions */
	struct list_head enabled_list;

	/* Optional: initialization during gadget bind */
	int (*init)(struct android_usb_function *, struct usb_composite_dev *);
	/* Optional: cleanup during gadget unbind */
	void (*cleanup)(struct android_usb_function *);
	/* Optional: called when the function is added the list of
	 *		enabled functions */
	void (*enable)(struct android_usb_function *);
	/* Optional: called when it is removed */
	void (*disable)(struct android_usb_function *);

	int (*bind_config)(struct android_usb_function *,
			   struct usb_configuration *);

	/* Optional: called when the configuration is removed */
	void (*unbind_config)(struct android_usb_function *,
			      struct usb_configuration *);
	/* Optional: handle ctrl requests before the device is configured */
	int (*ctrlrequest)(struct android_usb_function *,
					struct usb_composite_dev *,
					const struct usb_ctrlrequest *);
};

static int android_init_one_function(struct android_usb_function **functions,
				  int index_need_init,
				  struct usb_composite_dev *cdev)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_function *f;
	struct device_attribute **attrs;
	struct device_attribute *attr;
	int err;
	int index = 0;

	for (; (f = *functions++); index++) {
		if (index != index_need_init)
			continue;
		
		f->dev_name = kasprintf(GFP_KERNEL, "f_%s", f->name);
		/* Added for USB Develpment debug, more log for more debuging help */
		pr_debug("%s: f->dev_name = %s, f->name = %s\n",
			 __func__, f->dev_name, f->name);
		/* Added for USB Develpment debug, more log for more debuging help */
		f->dev = device_create(android_class, dev->dev,
				MKDEV(0, index), f, f->dev_name);
		if (IS_ERR(f->dev)) {
			pr_err("%s: Failed to create dev %s", __func__,
							f->dev_name);
			err = PTR_ERR(f->dev);
			goto err_create;
		}

		if (f->init) {
			err = f->init(f, cdev);
			if (err) {
				pr_err("%s: Failed to init %s", __func__,
								f->name);
				goto err_out;
			} else {
				pr_debug("%s: init %s success!!\n",
					 __func__, f->name);
			}
		}

		attrs = f->attributes;
		if (attrs) {
			while ((attr = *attrs++) && !err)
				err = device_create_file(f->dev, attr);
		}
		if (err) {
			pr_err("%s: Failed to create function %s attributes",
					__func__, f->name);
			goto err_out;
		}
	}
	return 0;

err_out:
	device_destroy(android_class, f->dev->devt);
err_create:
	kfree(f->dev_name);
	return err;
}

static void android_cleanup_one_function(struct android_usb_function *f)
{
	if (f->dev) {
		device_destroy(android_class, f->dev->devt);
		kfree(f->dev_name);
	}

	if (f->cleanup)
		f->cleanup(f);
}


/////////////////////////////////////////////////////////////////////
//	audio function

static int audio_function_init(struct android_usb_function *f,
			struct usb_composite_dev *cdev)
{
	pr_err("audio_function_init: init\n");
	return 0;
}
static void audio_function_cleanup(struct android_usb_function *f)
{
	pr_err("audio_function_cleanup: cleanup\n");
}
static int audio_function_bind_config(struct android_usb_function *f,
						struct usb_configuration *c)
{
	pr_err("audio_function_bind_config: bind interface=%d\n",c->next_interface_id);
	return audio_bind_config(c);
}
static void audio_function_unbind_config(struct android_usb_function *f,
						struct usb_configuration *c)
{
	pr_err("audio_function_unbind_config: unbind\n");
	uac2_unbind_config(c);
}

static u16 idVendor_sv, idProduct_sv;
static void audio_function_enable(struct android_usb_function *f)
{
	struct usb_device_descriptor *desc = &_android_dev->cdev->desc;
	idVendor_sv = desc->idVendor;
	idProduct_sv = desc->idProduct;
	desc->idVendor = LLUSBDAC_IDVENDER;
	desc->idProduct = LLUSBDAC_IDPRODUCT;
	printk("hack usb id: new %04x %04x old %04x %04x\n", desc->idVendor, desc->idProduct, idVendor_sv, idProduct_sv);
}
static void audio_function_disable(struct android_usb_function *f)
{
	struct usb_device_descriptor *desc = &_android_dev->cdev->desc;
	desc->idVendor = idVendor_sv;
	desc->idProduct = idProduct_sv;
	printk("restore usb id: %04x %04x\n", desc->idVendor, desc->idProduct);
}

static struct device_attribute *audio_function_attributes[] = {
	NULL
};

static struct android_usb_function audio_function = {
	.name		= "audio_func",
	.init		= audio_function_init,
	.cleanup	= audio_function_cleanup,
	.bind_config	= audio_function_bind_config,
	.unbind_config	= audio_function_unbind_config,
	.attributes	= audio_function_attributes,
	.enable = audio_function_enable,
	.disable = audio_function_disable,
};

/////////
int usb_connected(void)
{
	return !!_android_dev->connected;
}

/////////////////////////////////////////////////////////////////////
//	init and cleanup

static struct android_usb_function *victim_function;

static struct android_usb_function *replace_function(int index, struct android_usb_function *f)
{
	struct android_usb_function *old = supported_functions[index];
	android_cleanup_one_function(old);
	supported_functions[index] = f;
	android_init_one_function(supported_functions, index, NULL); // FIXME: cdev
	return old;
}


typedef struct {
	void *pc;
	const void *code_new;
	void *code_save;
	const void *code_verify;
	size_t len;
} patch_kernel_ctx;
int __patch_kernel(void *data)
{
	patch_kernel_ctx *ctx = data;
	if (ctx->code_verify && memcmp(ctx->pc, ctx->code_verify, ctx->len) != 0)
		return 0;
	if (ctx->code_save)
		memcpy(ctx->code_save, ctx->pc, ctx->len);
	memcpy(ctx->pc, ctx->code_new, ctx->len);
	flush_icache_range((unsigned)ctx->pc, (unsigned)ctx->pc + ctx->len);
	return 1;
}
void patch_kernel(void *pc, const void *code_new, void *code_save, const void *code_verify, size_t len)
{
	static void (*p_set_kernel_text_rw)(void);
#define set_kernel_text_rw p_set_kernel_text_rw
	static void (*p_set_kernel_text_ro)(void);
#define set_kernel_text_ro p_set_kernel_text_ro
	IMPORT_KALLSYMS(set_kernel_text_rw);
	IMPORT_KALLSYMS(set_kernel_text_ro);

	static patch_kernel_ctx ctx;
	ctx = (patch_kernel_ctx) {
		.pc = pc,
		.code_new = code_new,
		.code_save = code_save,
		.code_verify = code_verify,
		.len = len,
	};
	set_kernel_text_rw();
	set_all_modules_text_rw();
	int ok = stop_machine(__patch_kernel, &ctx, NULL);
	set_all_modules_text_ro();
	set_kernel_text_ro();
	BUG_ON(!ok);
}


static void invoke_sh(char *script)
{
	char *argv[] = {
		"/xbin/busybox",
		"sh",
		"-c",
		script,
		NULL
	};
	char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"LD_LIBRARY_PATH=/system/lib:/system/lib:/system/usr/local/lib:/usr/lib:/usr/local/lib",
		"PATH=/bin:/usr/bin:/sbin:/xbin:/system/bin:/system/usr/bin:/system/usr/local/bin:/system/sbin:/system/vendor/sony/bin",
		NULL
	};
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

void lock_cpufreq(int en)
{
	if (en > 0) {
		// lock to highest freq
		invoke_sh(
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | tail -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq;"
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | tail -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq;"
			"exit 0;"
		);
	} else if (en < 0) {
		// lock to lowest freq
		invoke_sh(
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | head -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq;"
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | head -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq;"
			"exit 0;"
		);
	} else {
		// normal
		invoke_sh(
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | tail -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq;"
			"cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies | tr ' ' '\\n' | sed '/^$/d' | sort -n | head -n1 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq;"
			"exit 0;"
		);
	}
}

static void enable_audio_func(int en)
{
	if (en) {
		// enable gadget
		invoke_sh(
			"trap '' HUP TERM INT PIPE; exec > /dev/null; exec 2>&1; set -x;"
			"echo 0 > /sys/class/android_usb/android0/enable;"
			"F=$(sed -e 's/audio_func,//' -e 's/,audio_func//' -e 's/audio_func//' /sys/class/android_usb/android0/functions);"
			"if [ -z \"$F\" ]; then F=audio_func; else F=audio_func,$F; fi;"
			"echo $F > /sys/class/android_usb/android0/functions;"
			"echo 1 > /sys/class/android_usb/android0/enable;"
			"exit 0;"
		);
	} else {
		// disable gadget
		invoke_sh(
			"trap '' HUP TERM INT PIPE; exec > /dev/null; exec 2>&1; set -x;"
			"if grep -q audio_func /sys/class/android_usb/android0/functions; then"
			"  echo 0 > /sys/class/android_usb/android0/enable;"
			"  F=$(sed -e 's/audio_func,//' -e 's/,audio_func//' -e 's/audio_func/dummy/' /sys/class/android_usb/android0/functions);"
			"  echo $F > /sys/class/android_usb/android0/functions;"
			"  echo 1 > /sys/class/android_usb/android0/enable;"
			"fi;"
			"exit 0;"
		);
	}
}

static int gap_ms = 100;
module_param(gap_ms, int, 0);
static int fb_low = 10000 - 5;
module_param(fb_low, int, 0);
static int fb_high = 10000 + 5;
module_param(fb_high, int, 0);
static int xrun_us = 2000;
module_param(xrun_us, int, 0);
static int target_us = 4000;
module_param(target_us, int, 0);
static int buffer_us = 16000;
module_param(buffer_us, int, 0);

static int __init init(void)
{
	printk(KERN_INFO "llusbdac init\n");

	IMPORT_KALLSYMS(android_class);
	IMPORT_KALLSYMS(_android_dev);
	IMPORT_KALLSYMS(supported_functions);
	
    /**(int *)kallsyms_lookup_name("disp_ovl_engine_log_level") = 10;
	*(int *)kallsyms_lookup_name("disp_log_on") = 1;
	*(int *)kallsyms_lookup_name("dbg_log") = 1;
	*(int *)kallsyms_lookup_name("irq_log") = 1;
    *(void **)kallsyms_lookup_name("xLogMem") = NULL;*/

	//*(unsigned *)kallsyms_lookup_name("musb_debug") = 999;
	//*(unsigned *)kallsyms_lookup_name("musb_uart_debug") = 1;

	ztcrc32_init();

	crc32mgr_init(gap_ms);
    init_gui();

	set_feedback_limits_base10000(fb_low, fb_high);
	uac2_init();

	set_player_param("/dev/snd/pcmC0D0p", xrun_us, target_us, buffer_us);


    // SUPER DIRTY HACK: replace one existing android usb gadget
	enable_audio_func(0);
#define victim 8
	victim_function = replace_function(victim, &audio_function);
	enable_audio_func(1);

	return 0;
}
static void __exit cleanup(void)
{
	printk(KERN_INFO "llusbdac cleanup\n");

	enable_audio_func(0);

	cleanup_gui();

	replace_function(victim, victim_function);
}

module_init(init);
module_exit(cleanup);
MODULE_DESCRIPTION("llusbdac");
MODULE_LICENSE("GPL");
