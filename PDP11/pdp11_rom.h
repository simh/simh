/* pdp11_m9312.h: M9312 bootrom data

   Copyright (c) 2021, Jos Fries

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  Modification history:

  12-Jan-21  GAF  Initial version

*/

#ifndef PDP11_ROM_H
#define PDP11_ROM_H

#include "pdp11_defs.h"

/* Define command parameter */

typedef struct
{
    const char *name;                           /* Parameter name */
    t_stat (*set_parameter)(char *, void *);    /* Set parameter routine */
}
cmd_parameter;


/* Define attach command parameter values */

typedef struct
{
    int     socket_number;
    int32   address;
    char    image_name[CBUFSIZE];
}
ATTACH_PARAM_VALUES;

/*
 * The rom structure contains the image for the rom plus an
 * identifying device mnemonic. The structure also contains a
 *  pointer to a function that is called when that specific ROM
 *  is attached. 
 */
typedef struct
{
    char device_mnemonic[5];            /* ROM identifier */
    void (*rom_attached)();             /* ROM specific function */
    uint16 (*image)[];                  /* ROM image */
    const char *help_text;              /* ROM help text */
}
rom;

/*
 * A socket on a ROM module provides an address space in the IOPAGE with
 * a base address and size. In a socket a number of pre-defined ROMs can
 * be placed.
 */
typedef struct
{
    t_addr base_address;                /* ROM code base address */
    int16 size;                         /* Address space size */
    rom (*rom_list)[];                  /* ROMs available for this socket */
}
rom_socket;

/*
 * A module is either variable, i.e. a module for which socket addresses
 * and images can freely be selected, or fixed, i.e. a module with
 * built in addresses and images.
 */
typedef enum
{
    ROM_FILE,
    ROM_BUILTIN
}
module_type;

/*
 * Define all relevant information for a module type
 */
typedef struct
{
    const char *name;                           /* Module name */
    module_type type;                           /* Module type */
    const uint32 valid_cpu_types;               /* Valid CPU types */
    const uint32 valid_cpu_opts;                /* Required CPU options */
    const uint32 num_sockets;                   /* Number of sockets for the module type */
    uint32 flags;                               /* Flags for initialization of the UNIT flag field */
    rom_socket (*sockets)[];                    /* Sockets for this module */
    t_stat (*auto_config)();                    /* Auto-configuration function for the module type */
    t_stat (*help_func)(FILE *, const char *);  /* Help function for the module type */
    t_stat (*attach)(const char *);             /* Attach function for the module type */
    t_stat (*auto_attach)();                    /* Auto attach on module type selection */
}
module_type_definition;

/* Define a device to ROM mapping */

typedef struct
{
    const char *device_name;                    /* Logical device name */
    const char *rom_name;                       /* Fitting ROM for the device */
}
rom_for_device;

/* Define a cpu_type to ROM mapping */

typedef struct
{
    const uint32 cpu_model;                     /* CPU model */
    const char *rom_name;                       /* Fitting ROM for the model */
}
rom_for_cpu_model;

/*
 * ROM device specific flags
 */
#define ROM_CONFIG_AUTO     (1u << 1)           /* Automatic configuration enabled */

/*
 * Definitions for the BLANK ROM module, i.e. a ROM module for which
 * the sockets have no fixed base address and the image for the ROM
 * has to be attached.
 */

#define NUM_BLANK_SOCKETS    4

rom_socket blank_sockets[NUM_BLANK_SOCKETS] =
{
    {0, 0, (rom (*)[]) NULL},        /* ROM 0 */
    {0, 0, (rom (*)[]) NULL},        /* ROM 1 */
    {0, 0, (rom (*)[]) NULL},        /* ROM 2 */
    {0, 0, (rom (*)[]) NULL},        /* ROM 3 */
};

/* Define socket information */

typedef struct
{
    int32 base_address;             /* Base address for the socket */
    int32 end_address;              /* End address for the socket */
    // ToDo: Either end_address or size should suffice
    int32 rom_size;                /* ROM size */
    char rom_name[CBUFSIZE];        /* Name of the ROM image */
    // ToDo: Rename rom_buffer to rom_image
    void *rom_buffer;               /* ROM contents */
}
SOCKET_INFO;

#endif /* PDP11_ROM_H */

