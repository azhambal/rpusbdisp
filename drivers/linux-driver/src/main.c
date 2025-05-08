/*
 * RoboPeak USB LCD Display Linux Driver
 * 
 * Copyright (C) 2009 - 2013 RoboPeak Team
 * This file is licensed under the GPL. See LICENSE in the package.
 *
 * http://www.robopeak.net
 *
 * Author: Shikai Chen
 */

#include "inc/common.h"
#include "inc/usbhandlers.h"
#include "inc/fbhandlers.h"
#include "inc/touchhandlers.h"

#if 0
// File operations for the USB LCD device
static const struct file_operations lcd_fops = {
    .owner =        THIS_MODULE,
    .read =         lcd_read,
    .write =        lcd_write,
    .open =         lcd_open,
    .unlocked_ioctl = lcd_ioctl,
    .release =      lcd_release,
    .llseek =       noop_llseek,
};

// USB class driver information to register the device
static struct usb_class_driver lcd_class = {
    .name =         "usbdisp%d",
    .fops =         &lcd_fops,
    .minor_base =   USBLCD_MINOR,
};
#endif

// Frame rate parameter for display refresh
int fps = 0;
module_param(fps, int, 0);
MODULE_PARM_DESC(fps, "Specify the frame rate used to refresh the display (override kernel config)");


// Module initialization function
static int __init usb_disp_init(void)
{
    int result;

    // Check if the frame rate is specified via modprobe
    if (fps == 0) {
        // If not, use the value defined in the kernel configuration
#ifdef CONFIG_RPUSBDISP_FPS
        fps = CONFIG_RPUSBDISP_FPS;
#else
        // Default frame rate in case the Kconfig is not integrated
        fps = 16;
#endif
    }

    // Register the touch, framebuffer, and USB handlers
    do {
        // Register touch handler
        result = register_touch_handler();
        if (result) {
            err("Touch handler registration failed. Error number %d", result);
            break;
        } 

        // Register framebuffer handlers
        result = register_fb_handlers();
        if (result) {
            err("Framebuffer handler registration failed. Error number %d", result);
            break;
        }

        // Register USB handlers
        result = register_usb_handlers();
        if (result) {
            err("USB handler registration failed. Error number %d", result);
            break;
        }

    } while (0);

    return result;
}


// Module cleanup function
static void __exit usb_disp_exit(void)
{
    // Unregister USB, framebuffer, and touch handlers
    unregister_usb_handlers();
    unregister_fb_handlers();
    unregister_touch_handler();
}


// Register the module's initialization and cleanup functions
module_init(usb_disp_init);
module_exit(usb_disp_exit);

// Module metadata
MODULE_AUTHOR("Shikai Chen <csk@live.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE(DRIVER_LICENSE_INFO);
