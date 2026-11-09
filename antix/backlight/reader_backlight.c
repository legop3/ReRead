// SPDX-License-Identifier: GPL-2.0-only
/* Intel Reader panel backlight. Stock HMSetLCDBrightness uses EC byte 0x86
 * through the Colbert index/data window. See analysis/brightness.md.
 */
#include <linux/backlight.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>

static DEFINE_MUTEX(controller_lock);
static struct backlight_device *panel;
static int read_level(void)
{
    int value;
    mutex_lock(&controller_lock);
    outb(0xf4, 0x1201);
    outb(0x81, 0x1202);
    value = (inb(0x1203) >> 1) & 0x7f;
    mutex_unlock(&controller_lock);
    return clamp(value, 0, 100);
}
static int panel_get(struct backlight_device *bd) { return read_level(); }
static int panel_update(struct backlight_device *bd)
{
    int level = backlight_get_brightness(bd);
    mutex_lock(&controller_lock);
    outb(0xf4, 0x1201);
    outb(0x86, 0x1202);
    outb(level, 0x1203);
    mutex_unlock(&controller_lock);
    return 0;
}
static const struct backlight_ops panel_ops = {
    .options = BL_CORE_SUSPENDRESUME,
    .get_brightness = panel_get,
    .update_status = panel_update,
};
static int __init panel_init(void)
{
    struct backlight_properties props = {0};
    if (!dmi_match(DMI_SYS_VENDOR, "Intel Corporation") ||
        !dmi_match(DMI_PRODUCT_NAME, "Colbert")) return -ENODEV;
    props.type = BACKLIGHT_PLATFORM;
    props.max_brightness = 100;
    props.brightness = read_level();
    panel = backlight_device_register("reader-panel", NULL, NULL, &panel_ops, &props);
    return PTR_ERR_OR_ZERO(panel);
}
static void __exit panel_exit(void) { backlight_device_unregister(panel); }
module_init(panel_init);
module_exit(panel_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Reader stock EC panel backlight");
