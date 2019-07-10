/* ka10_lights.c: KA10 console lights.

   Copyright (c) 2018, Lars Brinkhoff

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <libusb-1.0/libusb.h>

#include "kx10_defs.h"

static libusb_device_handle *lights_handle = NULL;
static uint64 lights_main = 0;
static int lights_aux = 0;

static void ka10_lights_latch (void)
{
    unsigned char buffer[8];

    if (lights_handle == NULL)
        return;

    buffer[0] = (lights_main >> 32) & 0377;
    buffer[1] = (lights_main >> 24) & 0377;
    buffer[2] = (lights_main >> 16) & 0377;
    buffer[3] = (lights_main >> 8) & 0377;
    buffer[4] = lights_main & 0377;

    buffer[5] = (lights_aux << 4) & 0340;
    buffer[6] = 0;
    buffer[7] = 0;

    libusb_control_transfer(lights_handle,
                            LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
                            LIBUSB_REQUEST_SET_CONFIGURATION,
                            0x0000,
                            0,
                            buffer,
                            sizeof buffer,
                            5000);
}

void ka10_lights_main (uint64 data)
{
    lights_main = data;
    ka10_lights_latch ();
}

void ka10_lights_set_aux (int n)
{
    lights_aux |= 1 << n;
    ka10_lights_latch ();
}

void ka10_lights_clear_aux (int n)
{
    lights_aux &= ~(1 << n);
    ka10_lights_latch ();
}

#define USB_CFG_VENDOR_ID       0xc0, 0x16
#define USB_CFG_DEVICE_ID       0xdf, 0x05
#define USB_CFG_DEVICE_NAME     'P','a','n','d','a',' ','D','i','s','p','l','a','y',
#define USB_CFG_DEVICE_NAME_LEN 13

static libusb_device_handle *get_panda_handle(libusb_device **devs)
{
    libusb_device *dev;
    libusb_device_handle *handle = NULL;
    int i = 0;
    int r;

    int found = 0;
    int openable = 0;

    unsigned char prod[256];
    char devname[USB_CFG_DEVICE_NAME_LEN] = {USB_CFG_DEVICE_NAME};

    unsigned char   rawVid[2] = {USB_CFG_VENDOR_ID};
    unsigned char    rawPid[2] = {USB_CFG_DEVICE_ID};

    int vid = rawVid[0] + 256 * rawVid[1];
    int pid = rawPid[0] + 256 * rawPid[1];


    while ((dev = devs[i++]) != NULL) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(dev, &desc); /* this always succeeds */
        // Do the VID and PID match?
        if (desc.idVendor == vid && desc.idProduct == pid) {
            found = 1;
            r = libusb_open(dev, &handle);
            // If we can't open it, keep trying.
            // There may be a device with the same pid and vid but not a Panda Display
            if (r < 0) {
                continue;
            }
            openable = 1;
            r = libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, sizeof prod);
            if (r < 0) {
                libusb_close(handle);
                return NULL;
            }
            // Here we have something that matches the free
            // VID and PID offered by Objective Development.
            // Now we need to Check device name to see if it
            // really is a Panda Display.
            if ((0 == strncmp((char *)prod, devname, USB_CFG_DEVICE_NAME_LEN)) &&
                (desc.idVendor == vid) &&
                (desc.idProduct == pid)) {
                return handle;
            }
            libusb_close(handle);
        }
    }

    if (found) {
        if (openable)
                  sim_messagef (SCPE_NOFNC, "Found USB device matching 16c0:05df, but it isn't a Panda Display\n");
        else
                  sim_messagef (SCPE_NOFNC, "Found something that might be a Panda Display, but couldn't open it.\n");
    }

    return NULL;
}

void ka10_lights_init (void)
{
    libusb_device **devs;
    libusb_context *ctx = NULL;
    ssize_t cnt;
    int r, i, pos;

    if (lights_handle != NULL)
        return;

    r = libusb_init(&ctx);
    if (r < 0)
        return;

    cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0)
        return;

    lights_handle = get_panda_handle(devs);
    if (lights_handle == NULL)
        return;

    if (libusb_kernel_driver_active(lights_handle, 0) == 1)
        libusb_detach_kernel_driver(lights_handle, 0);

    r = libusb_claim_interface(lights_handle, 0);
    if(r < 0)
        return;
}
