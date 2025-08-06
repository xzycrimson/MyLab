/*
 * drivers/input/touchscreen/doubletap2wake.c
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com>
 * Copyright (c) 2015, Vineeth Raj <contact.twn@openmailbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>
#include <linux/input/doubletap2wake.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#ifdef CONFIG_POCKETMOD
#include <linux/pocket_mod.h>
#endif

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

#include <linux/pm_wakeup.h>
#include <linux/suspend.h>

/* if Sweep2Wake is compiled it will already have taken care of this */
#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

/* Version, author, desc, etc */
#define DRIVER_AUTHOR "Tanishk2k09.dev@gmail.com"
#define DRIVER_DESC "Doubletap2wake driver"
#define DRIVER_VERSION "1.0"

#define DT2W_NAME "doubletap2wake"

#define DT2W_ENABLE_ATTR "doubletap2wake_enable"
#define DT2W_X_COORD_ATTR "doubletap2wake_x_coord"
#define DT2W_Y_COORD_ATTR "doubletap2wake_y_coord"

static struct input_dev *dt2w_input_dev;
static struct hrtimer dt2w_timer;
static struct wakeup_source *dt2w_ws;

static int dt2w_enable = 0;
static int dt2w_x_coord = 0;
static int dt2w_y_coord = 0;

static ssize_t dt2w_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dt2w_enable);
}

static ssize_t dt2w_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int enable;

	if (sscanf(buf, "%d", &enable) != 1)
		return -EINVAL;

	if (enable != 0 && enable != 1)
		return -EINVAL;

	dt2w_enable = enable;

	return count;
}

static ssize_t dt2w_x_coord_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dt2w_x_coord);
}

static ssize_t dt2w_x_coord_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int x_coord;

	if (sscanf(buf, "%d", &x_coord) != 1)
		return -EINVAL;

	dt2w_x_coord = x_coord;

	return count;
}

static ssize_t dt2w_y_coord_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dt2w_y_coord);
}

static ssize_t dt2w_y_coord_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int y_coord;

	if (sscanf(buf, "%d", &y_coord) != 1)
		return -EINVAL;

	dt2w_y_coord = y_coord;

	return count;
}

static struct kobj_attribute dt2w_enable_attribute = __ATTR(doubletap2wake_enable, 0664, dt2w_enable_show, dt2w_enable_store);
static struct kobj_attribute dt2w_x_coord_attribute = __ATTR(doubletap2wake_x_coord, 0664, dt2w_x_coord_show, dt2w_x_coord_store);
static struct kobj_attribute dt2w_y_coord_attribute = __ATTR(doubletap2wake_y_coord, 0664, dt2w_y_coord_show, dt2w_y_coord_store);

static struct attribute *dt2w_attrs[] = {
	&dt2w_enable_attribute.attr,
	&dt2w_x_coord_attribute.attr,
	&dt2w_y_coord_attribute.attr,
	NULL,
};

static struct attribute_group dt2w_attr_group = {
	.attrs = dt2w_attrs,
};

static struct kobject *dt2w_kobj;

static enum hrtimer_restart dt2w_timer_func(struct hrtimer *timer)
{
	schedule_work(&dt2w_work);

	return HRTIMER_NORESTART;
}

static void dt2w_work_func(struct work_struct *work)
{
	input_report_key(dt2w_input_dev, KEY_POWER, 1);
	input_sync(dt2w_input_dev);
	input_report_key(dt2w_input_dev, KEY_POWER, 0);
	input_sync(dt2w_input_dev);
}

static int __init dt2w_init(void)
{
	int error;

	dt2w_input_dev = input_allocate_device();
	if (!dt2w_input_dev) {
		pr_err("Failed to allocate input device\n");
		return -ENOMEM;
	}

	dt2w_input_dev->name = DT2W_NAME;
	dt2w_input_dev->id.bustype = BUS_VIRTUAL;
	dt2w_input_dev->id.vendor = 0x0000;
	dt2w_input_dev->id.product = 0x0000;
	dt2w_input_dev->id.version = 0x0000;

	set_bit(EV_KEY, dt2w_input_dev->evbit);
	set_bit(KEY_POWER, dt2w_input_dev->keybit);

	error = input_register_device(dt2w_input_dev);
	if (error) {
		pr_err("Failed to register input device\n");
		input_free_device(dt2w_input_dev);
		return error;
	}

	dt2w_kobj = kobject_create_and_add(DT2W_NAME, kernel_kobj);
	if (!dt2w_kobj) {
		pr_err("Failed to create kobject\n");
		input_unregister_device(dt2w_input_dev);
		return -ENOMEM;
	}

	error = sysfs_create_group(dt2w_kobj, &dt2w_attr_group);
	if (error) {
		pr_err("Failed to create sysfs group\n");
		kobject_put(dt2w_kobj);
		input_unregister_device(dt2w_input_dev);
		return error;
	}

	hrtimer_init(&dt2w_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dt2w_timer.function = dt2w_timer_func;

	INIT_WORK(&dt2w_work, dt2w_work_func);

	dt2w_ws = wakeup_source_register(NULL, DT2W_NAME);

	pr_info("Doubletap2wake driver initialized\n");

	return 0;
}

static void __exit dt2w_exit(void)
{
	sysfs_remove_group(dt2w_kobj, &dt2w_attr_group);
	kobject_put(dt2w_kobj);
	input_unregister_device(dt2w_input_dev);
	hrtimer_cancel(&dt2w_timer);
	cancel_work_sync(&dt2w_work);
	wakeup_source_unregister(dt2w_ws);

	pr_info("Doubletap2wake driver exited\n");
}

module_init(dt2w_init);
module_exit(dt2w_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");


