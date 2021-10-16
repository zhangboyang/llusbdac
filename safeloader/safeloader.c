#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>

static char *script = "echo hello > /dev/kmsg";
module_param(script, charp, 0000);

static int countdown = 3;
module_param(countdown, int, 0000);

static void **p_agdev_g;

static void invoke_sh(char *script)
{
	static char *argv[] = {
		/*0*/"/xbin/busybox",
		/*1*/"sh",
		/*2*/"-c",
		/*3*/NULL,
		NULL
	};
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"LD_LIBRARY_PATH=/system/lib:/system/lib:/system/usr/local/lib:/usr/lib:/usr/local/lib",
		"PATH=/bin:/usr/bin:/sbin:/xbin:/system/bin:/system/usr/bin:/system/usr/local/bin:/system/sbin:/system/vendor/sony/bin",
		NULL
	};
	argv[3] = script;
	call_usermodehelper(argv[0], argv, envp, UMH_NO_WAIT);
}

static void safeloader_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
    //printk("safeloader_event(%p, %u, %u, %d)\n", handle, type, code, value);
    if (type == 1 && value == 1) {
        switch (code) {
        case 105: case 106: case 28:
			if (p_agdev_g && *p_agdev_g) {
				if (countdown > 0 && --countdown == 0) {
					//printk(KERN_INFO "script=[%s]\n", script);
					invoke_sh(script);
				}
			}
            break;
        }
    }
}

static int safeloader_connect(struct input_handler *handler,
                         struct input_dev *dev,
                         const struct input_device_id *id)
{
	struct input_handle *handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
    int error;
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "llusbdac_input";
    if ((error = input_register_handle(handle)))
        goto fail_free;
    if ((error = input_open_device(handle)))
        goto fail_unregister;
    return 0;
fail_unregister:
	input_unregister_handle(handle);
fail_free:
	kfree(handle);
    return error;
}
static void safeloader_disconnect(struct input_handle *handle)
{
    input_close_device(handle);
    input_unregister_handle(handle);
	kfree(handle);
}
static bool safeloader_match(struct input_handler *handler, struct input_dev *dev)
{
    return dev && dev->name && strcmp(dev->name, "icx_key") == 0;
}
static const struct input_device_id safeloader_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
        .evbit = { BIT_MASK(EV_KEY) },
    },
    { },
};

static struct input_handler safeloader_handler = {
    .event          = safeloader_event,
    .connect        = safeloader_connect,
    .disconnect     = safeloader_disconnect,
    .match          = safeloader_match,
    .name           = "safeloader",
    .id_table       = safeloader_ids,
};


static int handler_registered = 0;
static int __init init(void)
{
	printk(KERN_INFO "safeloader init\n");
	
	p_agdev_g = (void **) kallsyms_lookup_name("agdev_g");
	//printk(KERN_INFO "&agdev_g=%p\n", p_agdev_g);

	handler_registered = (input_register_handler(&safeloader_handler) == 0);
	if (!handler_registered) {
		printk(KERN_INFO "input_register_handler() failed\n");
	}
	return 0;
}
static void __exit cleanup(void)
{
	printk(KERN_INFO "safeloader cleanup\n");

	if (handler_registered) {
		input_unregister_handler(&safeloader_handler);
		handler_registered = 0;
	}
}

module_init(init);
module_exit(cleanup);
MODULE_DESCRIPTION("safeloader");
MODULE_LICENSE("GPL");
