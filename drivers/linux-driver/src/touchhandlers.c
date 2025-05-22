/*
 *    RoboPeak USB LCD Display Linux Driver
 *    
 *    Copyright (C) 2009 - 2013 RoboPeak Team
 *    This file is licensed under the GPL. See LICENSE in the package.
 *
 *    http://www.robopeak.net
 *
 *    Author Shikai Chen
 *
 *   ---------------------------------------------------
 *   Touch Event Handlers
 */


#include "inc/touchhandlers.h"
#include <linux/input.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "inc/devconf.h"
#include "inc/usbhandlers.h"

static struct input_dev *default_input_dev;
static volatile int live_flag;

/**
 * on_create_input_dev - Allocate and register a new input device.
 * @inputdev: Pointer to store the allocated input device.
 *
 * Return: 0 on success, or an error code on failure.
 */
static int on_create_input_dev(struct input_dev **inputdev)
{
    *inputdev = input_allocate_device();

    if (!*inputdev) { // Dereference inputdev to check the allocated device
        pr_err("Failed to allocate input device\n");
        return -ENOMEM;
    }

    (*inputdev)->evbit[0] = BIT(EV_SYN) | BIT(EV_KEY) | BIT(EV_ABS);  
    
    (*inputdev)->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);  


    input_set_abs_params((*inputdev), ABS_X, 0, RP_DISP_DEFAULT_WIDTH, 0, 0);
    input_set_abs_params((*inputdev), ABS_Y, 0, RP_DISP_DEFAULT_HEIGHT, 0, 0);
    input_set_abs_params((*inputdev), ABS_PRESSURE, 0, 1, 0, 0);  

    (*inputdev)->name = "RoboPeakUSBDisplayTS";
    /* TODO: Populate input_dev->id.vendor, product, version if a specific usb_device can be associated here. */
    (*inputdev)->id.bustype    = BUS_USB;

    {
    int ret = input_register_device((*inputdev));
    if (ret) {
        pr_err("Failed to register input device, error %d\n", ret);
        input_free_device(*inputdev); // Free on registration failure
        *inputdev = NULL; // Prevent use after free or double free
    }
    return ret;
}
}

/**
 * on_release_input_dev - Unregister and free an input device.
 * @inputdev: The input device to release.
 */
static void on_release_input_dev(struct input_dev *inputdev)
{
    
    input_unregister_device(inputdev);
    // input_free_device typically called by input_unregister_device if refcount is 0
    // For explicit cleanup, caller should ensure input_free_device if input_dev is not NULL after this.
    // However, standard pattern is that input_allocate_device is paired with input_free_device.
    // input_unregister_device makes it available for freeing if refcount is zero.
}


/**
 * register_touch_handler - Register the global touchscreen input device.
 *
 * Return: 0 on success, or an error code on failure.
 */
int __init register_touch_handler(void)
{
    int ret = on_create_input_dev(&default_input_dev);
    if (!ret) {
     live_flag = 1;
    } else {
    // on_create_input_dev now handles freeing default_input_dev if registration fails and sets it to NULL
       default_input_dev = NULL; 
    }
    return ret;
}

/**
 * unregister_touch_handler - Unregister the global touchscreen input device.
 */
void unregister_touch_handler(void)
{
    live_flag = 0;
    if (default_input_dev) { /* Check if it was allocated and registered */
       on_release_input_dev(default_input_dev);
       input_free_device(default_input_dev); /* Explicitly free after unregistering */
       default_input_dev = NULL; /* Clear the pointer after release */
    }
}


/**
 * touchhandler_on_new_device - Placeholder for new device notification.
 * @dev: The rpusbdisp device.
 *
 * Return: 0 (currently a no-op).
 */
int touchhandler_on_new_device(struct rpusbdisp_dev * dev)
{
    /* TODO: Consider per-device input registration for multi-device support. */
    // singleton design
    return 0;
}

/**
 * touchhandler_on_remove_device - Placeholder for device removal notification.
 * @dev: The rpusbdisp device.
 */
void touchhandler_on_remove_device(struct rpusbdisp_dev * dev)
{
    /* TODO: Consider per-device input registration for multi-device support. */
    // singleton design
}


/**
 * touchhandler_send_ts_event - Report a touch event to the input subsystem.
 * @dev: The rpusbdisp device (currently unused due to singleton input_dev).
 * @x: The x-coordinate of the touch.
 * @y: The y-coordinate of the touch.
 * @touch: Non-zero if touched, zero if released.
 */
void touchhandler_send_ts_event(struct rpusbdisp_dev *dev, int x, int y, int touch)
{
    if (!default_input_dev || !live_flag)
        return;

    if (touch) {
        input_report_abs(default_input_dev, ABS_X, x);
        input_report_abs(default_input_dev, ABS_Y, y);
        input_report_abs(default_input_dev, ABS_PRESSURE, 1);
        input_report_key(default_input_dev, BTN_TOUCH, 1);
        input_sync(default_input_dev);
    } else {
        input_report_abs(default_input_dev, ABS_PRESSURE, 0);
        input_report_key(default_input_dev, BTN_TOUCH, 0);
        input_sync(default_input_dev);        
    }
}