/* pdp11_rom.c: Read-Only Memory.

   Copyright (c) 2021, Lars Brinkhoff, Jos Fries

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <stdarg.h>

#include "sim_defs.h"
#include "pdp11_defs.h"
#include "pdp11_m9312.h"
#include "pdp11_vt40boot.h"

/* Forward references */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_rd (int32 *data, int32 PA, int32 access);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_boot (int32 u, DEVICE *dptr);
t_stat rom_set_addr (UNIT *, int32, CONST char *, void *);
t_stat rom_show_address (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_contents (UNIT *, int32, CONST char *, void *);
t_stat rom_show_contents (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_type (UNIT *, int32, CONST char *, void *);
t_stat rom_show_type (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_configmode (UNIT *, int32, CONST char *, void *);
t_stat rom_show_configmode (FILE *, UNIT *, int32, CONST void *);
t_stat rom_attach (UNIT *, CONST char *);
t_stat rom_detach (UNIT *);
t_stat rom_blank_help (FILE *, const char *);
t_stat rom_m9312_help (FILE *, const char *);
t_stat rom_vt40_help (FILE *, const char *);
t_stat rom_help (FILE *st, DEVICE *, UNIT *, int32, const char *);
t_stat rom_help_attach (FILE *st, DEVICE *, UNIT *, int32, const char *);
const char *rom_description (DEVICE *);
t_stat reset_dib (int unit_number, t_stat (reader (int32 *, int32, int32)),
    t_stat (writer (int32, int32, int32)));

static rom *find_rom (const char *cptr, rom (*rom_list)[]);
static t_stat attach_rom_to_unit (rom *romptr, rom_socket *socketptr, UNIT *uptr);
static t_stat m9312_auto_config ();
static t_stat m9312_auto_config_console_roms ();
static t_stat m9312_auto_config_bootroms ();
static t_stat attach_m9312_rom (const char *rom_name, int unit_number);

/* External references */
extern uint32 cpu_type;
extern uint32 cpu_opt;
extern uint32 cpu_model;
extern int32 HITMISS;


static const char rom_helptext[] =
/***************** 80 character line width template *************************/
"ROM, Read-Only Memory\n\n"
"A hardware PDP-11 comprises ROM code, containing console emulator, diagnostic\n"
"and bootstrap functionality. The ROM device can be used to add a ROM module\n"
"to the I/O page. Each module has one or more ROMs available that can be\n"
"attached to the module.\n\n"
"Available modules are:\n"
"   BLANK\n"
"   M9312\n"
"   VT40\n\n"
"The module to be used is selected by means of the TYPE modifier, the\n"
"'SET ROM TYPE=M9312'command e.g. selects the M9312 module. The ATTACH\n"
"command can then be used to attach a specific ROM to the units of the ROM\n"
"device.\n\n"
"The following commands are available:\n\n"
"   SHOW ROM\n"
"   SHOW ROM<unit>\n"
"   SET ROM TYPE={BLANK|M9312|VT40}\n"
"   SET ROM CONFIGMODE=AUTO | MANUAL\n"
"   SET ROM ADDRESS=<address>{;<address>}\n"
"   ATTACH ROM<unit> <file> | <built-in ROM>\n"
"   SHOW ROM<unit>\n"
"   HELP ROM\n"
"   HELP ROM SET\n"
"   HELP ROM SHOW\n"
"   HELP ROM ATTACH\n\n"
"The SET ROM ADDRESS command is only applicable to the BLANK module type. By\n"
"means of this command the base address of the ROM's sockets can be\n"
"specified. The command accepts a sequence of addresses, in order of socket\n"
"number, separated by a semi-colon. Empty addresses are allowed and leave the\n"
"socket base address unchanged. The command 'SET ADDRESS=17765000;;17773000'\n"
"e.g. sets the base address for socket 0 to 17765000, leaves the base address\n"
"for socket 1 unchanged and sets the base address for socket 2 to 17773000.\n\n"
"Help is available for the BLANK, M9312 and VT40 modules:\n\n"
"   HELP ROM BLANK\n"
"   HELP ROM M9312\n"
"   HELP ROM VT40\n\n";

static const char rom_blank_helptext[] =
/***************** 80 character line width template *************************/
"The contents of the BLANK ROM module have to be specified by setting the\n"
"ROM's base address and ROM image.First the ROM unit ADDRESS has to be set,\n"
"and then the ATTACH command can be used to fill the ROM with contents.\n\n";

static const char rom_vt40_helptext[] =
/***************** 80 character line width template *************************/
"The VT40 module is meant for the GT-40 graphic terminal, based on a\n"
"PDP-11/05. The VT40 included a bootstrap ROM.The module has just one socket\n"
"with one available ROM and a 'SET ROM TYPE=VT40' command suffices to\n"
"select this boot ROM.\n\n";

static const char rom_m9312_helptext[] =
/***************** 80 character line width template *************************/
"The M9312 module contains 512 words of read only memory (ROM) that can be\n"
"used for diagnostic routines, the console emulator routine, and bootstrap\n"
"programs. The module contains five sockets that allow the user to insert\n"
"ROMs, enabling the module to be used with any Unibus PDP11 system and boot\n"
"any peripheral device by simply adding or changing ROMs.\n\n"
"Socket 0 is solely used for a diagnostic ROM (PDP-11/60 and 11/70 systems)\n"
"or a ROM which contains the console emulator routine and diagnostics for all\n"
"other PDP-11 systems. The other four sockets accept ROMs which contain\n"
"bootstrap programs. In general one boot ROM contains the boot code for one\n"
"device type. In some cases a boot rom contains the boot code for two device\n"
"types and there are also cases where the boot code comprises two or three\n"
"ROMs. In these cases these ROMs have to be placed in subsequent sockets.\n\n"
"The M9312 module is in simh implemented as the M9312 device. In accordance\n"
"with the hardware module, the M9312 device contains five units. Every unit\n"
"can be supplied with a specific ROM. All ROMs are available in the device\n"
"itself and can be seated in a socket by means of the ATTACH command.\n"
"The 'ATTACH ROM0 B0' command for example puts the 11/70 diagnostics ROM in\n"
"socket 0.\n\n"
"The following ROMs are available:\n\n"
"ROM code       Function\n"
"A0             11/04, 11/34 Diagnostic/Console (M9312 E20)\n"
"B0             11/60, 11/70 Diagnostic (M9312 E20)\n"
"UBI            11/44 Diagnostic/Console (UBI; M7098 E58)\n"
"MEM            11/24 Diagnostic/Console (MEM; M7134 E74)\n"
"DL             RL01/RL02 cartridge disk\n"
"DM             RK06/RK07 cartridge disk\n"
"DX             RX01 floppy disk, single density\n"
"DP             RP02/RP03 cartridge disk\n"
"DB             RP04/RP05/RP06, RM02/RM03/RM05 cartridge disk\n"
"DK             RK03/RK05 DECdisk\n"
"DT             TU55/TU56 DECtape\n"
"MM             TU16/TU45/TU77, TE16 magtape\n"
"MT             TS03, TU10, TE10 magtape\n"
"DS             RS03/RS04 fixed disk\n"
"TT             ASR33 lowspeed reader\n"
"PR             PC05 hispeed reader\n"
"CT             TU60 DECcassette\n"
"MS             TS04/TS11, TU80, TSU05 tape\n"
"DD             TU58 DECtapeII\n"
"DU             MSCP UDA50 (RAxx) disk\n"
"DY             RX02 floppy disk, double density\n"
"MU             TMSCP TK50, TU81 magtape\n"
"XE0, XE1       Ethernet DEUNA / DELUA Net Boot (v2)\n"
"XM0, XM1, XM2  DECnet DDCMP DMC11 / DMR11\n"
"ZZ             Test ROM\n\n"
"Help is available for each ROM with the 'HELP ROM M9312 <ROM code>' command.\n\n"
/***************** 80 character line width template *************************/
"The M9312 module has 512 words of read only memory.The lower 256 words\n"
"(addresses 165000 through 165776) are used for the storage of ASCII console\n"
"and diagnostic routines. The diagnostics are rudimentary CPU and memory\n"
"diagnostics. The upper 256 words (addresses 173000 through 173776) are used\n"
"for bootstrap programs. These upper words are divided further into four\n"
"64-word segments. In principle each of the segments 0 to 4 contains the boot\n"
"programs for one or two device types. If necessary however, more than one\n"
"segment may be used for a boot program. The following table shows the ROM\n"
"segmentation.\n\n"
"ROM type                                           Base address\n"
"256 word console emulator and diagnostics ROM #0   165000\n"
"64 word boot ROM #1                                173000\n"
"64 word boot ROM #2                                173200\n"
"64 word boot ROM #3                                173400\n"
"64 word boot ROM #4                                173600\n\n"
"The system start adress can be determined in the following way:\n"
"- Take the socket base address the ROM is placed in from the table above,\n"
"- Add the ROM-specific offset of the entry point. The offsets are documented\n"
"  in the ROM-specific help text wich can be displayed via the\n"
"  'HELP ROM M9312 <ROM code>' command.\n\n"
/***************** 80 character line width template *************************/
"With a DL boot ROM in socket 1 e.g., a RL01 disk can be booted from unit 0,\n"
"without performing the diagnostics, by starting at address 173004. With the\n"
"same boot ROM placed in socket 2 that unit can be booted by starting at\n"
"address 1732004. Note that the start address specifies whether or not the\n"
"diagnostics code is executed.\n\n";

/*
 * ROM data structures
 *
 * The ROM device is described by means of the following data structures:
 * 1. A list of modules. The ROM device supports three modules:
 *      a) BLANK module. This module is freely configurable with the ROM base address
 *    and image.
 *      b) M9312 module. This module has built-in ROM images on fixed adresses is available
 *    on all Unibus models.
 *    c) VT40 module. This module is for use in the GT40 model.
 * 
 * 2. Every module comprises a number of sockets. Every socket has a base address and
 *    a size in the I/O address space. Every socket is represented as a unit in
 *    the ROM device.
 * 
 * 3. A socket points to a list of ROMs that are available for that socket. So, per
 *    module and unit one or more ROMs are available.
 * 
 * 4. Every ROM comprises an identification of the ROM in the form of a mnemonic
 *    and the image of the ROM.
 * 
 */

#define ROM_UNIT_FLAGS    UNIT_RO | UNIT_MUSTBUF | UNIT_BUFABLE | UNIT_ATTABLE
#define QBUS_MODEL        (1u << 0)
#define UNIBUS_MODEL      (1u << 1)

/* Define the default "blank" ROM module */
module blank =
{
    "BLANK",                                /* Module name */
    ROM_FILE,                               /* Module type */
    CPUT_ALL,                               /* Required CPU types */
    QBUS_MODEL | UNIBUS_MODEL,              /* Required CPU options */
    NUM_BLANK_SOCKETS,                      /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & blank_sockets,     /* Pointer to rom_socket structs */
    NULL,                                   /* Auto configuration function */
    &rom_blank_help                         /* Pointer to help function */
};

/* Define the M9312 module */
module m9312 =
{
    "M9312",                                /* Module name */
    ROM_BUILTIN,                            /* Module type */
    CPUT_ALL,                               /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    NUM_M9312_SOCKETS,                      /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & m9312_sockets,     /* Pointer to rom_socket structs */
    &m9312_auto_config,                     /* Auto configuration function */
    &rom_m9312_help                         /* Pointer to help function */
};

/* Define the VT40 module */
module vt40 =
{
    "VT40",                                 /* Module name */
    ROM_BUILTIN,                            /* Module type */
    CPUT_05,                                /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    NUM_VT40_SOCKETS,                       /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & vt40_sockets,      /* Pointer to rom_socket structs */
    NULL,                                   /* Auto configuration function */
    &rom_vt40_help                          /* Pointer to help function */
};

/*
 * Define the number of modules. The BLANK module must be the
 * first module in the list.
 */
#define NUM_MODULES            3
#define ROM_MODULE_BLANK       0

/* The list of available ROM modules */
module* module_list[NUM_MODULES] =
{
    &blank,
    &m9312,
    &vt40,
};


/*
 * The maximum number of sockets is the number of sockets any module can have.
 * For modules with a number of units less than this maximum the surplus
 * units are disabled.
 */ 
#define MAX_NUMBER_SOCKETS    5

/*
 * Define and initialize the UNIT structs. 
 */
UNIT rom_unit[MAX_NUMBER_SOCKETS] =
{
    { UDATA (NULL, ROM_UNIT_FLAGS, 0) },
    { UDATA (NULL, ROM_UNIT_FLAGS, 0) },
    { UDATA (NULL, ROM_UNIT_FLAGS, 0) },
    { UDATA (NULL, ROM_UNIT_FLAGS, 0) },
    { UDATA (NULL, ROM_UNIT_FLAGS | UNIT_DIS, 0) },
};

DIB rom_dib[MAX_NUMBER_SOCKETS];

/* Static definitions */
static t_bool rom_initialized = FALSE;              /* Initialize rom_unit on first reset call */
static uint32 cpu_type_on_selection;                /* cpu_type for which module type was selected */
static uint32 rom_device_flags = 0;                 /* rom device specific flags */
static FILE *rom_fileref[MAX_NUMBER_SOCKETS] = 
    {NULL, NULL, NULL, NULL, NULL};                 /* File references for opened ROM files */
// ToDo: Files can be closed after read in buffer?! So there is no need to keep file references?
static void *rom_buffer[MAX_NUMBER_SOCKETS];        /* Buffered ROM contents */
// ToDo: Use M9312 image pointer?
static uint32 rom_size[MAX_NUMBER_SOCKETS];         /* ROM contents size */

/*
 * Define ROM device registers. All state is maintained in these registers
 * to allow the state to be saved and restored by a SAVE/RESTORE cycle.
 */
static uint16 selected_type = ROM_MODULE_BLANK;     /* The module type as set by the user */
static int32 base_address[MAX_NUMBER_SOCKETS];      /* Socket base address */
static int32 end_address[MAX_NUMBER_SOCKETS];       /* Socket end address */
static char rom_name[MAX_NUMBER_SOCKETS][CBUFSIZE] 
    = {"", "", "", "", ""};                         /* File name for each socket */

REG rom_reg[] = {
    { ORDATAD (TYPE,     selected_type, 16,     "Type"), REG_HIDDEN | REG_RO },
    { BRDATAD (BASE,     base_address,  8,  22, MAX_NUMBER_SOCKETS, 
        "Socket Base Addresses"),  REG_RO},
    { BRDATAD (END,      end_address,   8,  22, MAX_NUMBER_SOCKETS,
        "Socket End Addresses"), REG_RO },
    { VBRDATA (ROM_NAMES, rom_name, 16, 8, CBUFSIZE * MAX_NUMBER_SOCKETS) },
    { NULL }
};

/*
 * Define the ROM device and units modifiers.
 * 
 * A number of modifiers are marked "named only" to prevent them showing
 * up on the SHOW ROM output. The ROM SHOW CONTENTS command will print all
 * relevant information for a socket.
 */
MTAB rom_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NMO, 0, "TYPE", "TYPE",
        &rom_set_type, &rom_show_type, NULL, "ROM type (BLANK, M9312 or VT40)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NMO, 0, "CONFIGMODE", "CONFIGMODE",
        &rom_set_configmode, &rom_show_configmode, NULL, "Auto configuration (AUTO or MANUAL)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NMO, 0,   "ADDRESS", "ADDRESS",
        &rom_set_addr, &rom_show_address, NULL, "Socket base address" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NC,  0,   "CONTENTS", "CONTENTS",
        &rom_set_contents, &rom_show_contents, NULL, "Socket ROM contents" },
    { 0 }
};

/* Device definition */
DEVICE rom_dev =
{
    "ROM",                               /* Device name */
    rom_unit,                            /* Pointer to device unit structures */
    rom_reg,                             /* Pointer to ROM registers */
    rom_mod,                             /* Pointer to modifier table */
    MAX_NUMBER_SOCKETS,                  /* Number of units */
    8,                                   /* Address radix */
    9,                                   /* Address width */
    2,                                   /* Address increment */
    8,                                   /* Data radix */
    16,                                  /* Data width */
    rom_ex,                              /* Examine routine */
    NULL,                                /* Deposit routine not available */
    rom_reset,                           /* Reset routine */
    &rom_boot,                           /* Boot routine */
    &rom_attach,                         /* Attach routine */
    &rom_detach,                         /* Detach routine */
    &rom_dib[0],                         /* Pointer to device information blocks */
    DEV_DISABLE | DEV_UBUS | DEV_QBUS,   /* Flags */
    0,                                   /* Debug control */
    NULL,                                /* Debug flags */
    NULL,                                /* Memory size routine */
    NULL,                                /* Logical name */
    &rom_help,                           /* Help routine */
    &rom_help_attach,                    /* Help attach routine */
    NULL,                                /* Context for help routines */
    &rom_description                     /* Description routine */
};

/*
 * The device_rom_map defines a mapping from simh device to a suitable
 * boot ROM for the device.
 */
rom_for_device device_rom_map [] =
{
    "RK",   "DK",
    "RL",   "DL",
    "HK",   "DM",
    "RY",   "DY",
    "RP",   "DB",
    "RQ",   "DU",
    "RX",   "DX",
    "RY",   "DY",
    "RS",   "DS",
    "TC",   "DT",
    "TS",   "MS",
    "TU",   "MU",
    "TA",   "CT",
    "TM",   "MT",
    "TQ",   "MU",
    NULL,   NULL,
};

/*
 * Define a mapping from CPU model to the console/diagnostic ROM for 
 * the model.
 */

rom_for_cpu_model cpu_rom_map[] =
{
    MOD_1104,           "A0",
    MOD_1105,           "A0",
    MOD_1124,           "MEM",
    MOD_1134,           "A0",
    MOD_1140,           "A0",
    MOD_1144,           "UBI",
    MOD_1145,           "A0",
    MOD_1160,           "B0",
    MOD_1170,           "B0",
    0,                   NULL,
};

/*
 * Check if the module type to be set is valid on the selected 
 * cpu_opt and cpu_type
 */
int module_type_is_valid (uint16 module_number)
{
    uint32 bus = UNIBUS ? UNIBUS_MODEL : QBUS_MODEL;

    return CPUT (module_list[module_number]->valid_cpu_types) &&
        bus & module_list[module_number]->valid_cpu_opts;
}

/* Set ROM module type */

t_stat rom_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 unit_number;
    uint16 module_number;
    rom *romptr;

    /* Is a module type specified? */
    if (cptr == NULL)
        return sim_messagef (SCPE_ARG, "No module specified\n");

    /* Search the module list for the specified module type */
    for (module_number = 0; module_number < NUM_MODULES; module_number++) {
        if (strcasecmp (cptr, module_list[module_number]->name) == 0) {
            /* Check if the module is allowed on this cpu and bus type */
            if (!module_type_is_valid (module_number))
                return sim_messagef (SCPE_ARG, "Module is not valid for current cpu and/or bus type\n");

            /* Save current cpu type for reference in rom_reset() */
            cpu_type_on_selection = cpu_type;

            /* 
             * Module type found. If the currently selected type differs
             * from the previously selected type, initialize the UNITs
             * with values for this type.
             */
            if (module_number != selected_type) {
                
                /* Set now selected module type */
                selected_type = module_number;

                for (unit_number = 0, uptr = &rom_unit[0]; 
                    unit_number < MAX_NUMBER_SOCKETS; unit_number++, uptr++) {

                    /* Check if an image is attached to this unit */
                    if (uptr->flags & UNIT_ATT) {
                        /* Detach the unit */
                        if (rom_detach (uptr) != SCPE_OK)
                            return SCPE_IERR;
                    }

                    /* Clear addressses and function and initialize flags */
                    base_address[unit_number] = 0;
                    end_address[unit_number] = 0;
                    uptr->flags = module_list[module_number]->flags;

                    /* Disable surplus ROMs for this module */
                    if (unit_number >= module_list[module_number]->num_sockets)
                        uptr->flags |= UNIT_DIS;
                    else
                        uptr->flags &= ~UNIT_DIS;
                }
            }

            /* (Re)set the configuration mode to manual */
            rom_device_flags &= ~ROM_CONFIG_AUTO;

            /* If this module has just one unit and that unit has just one possible
               image attach the image to the unit */
            if (module_list[module_number]->num_sockets == 1) {
                int num_roms = 0;
                rom_socket *socketptr = *module_list[module_number]->sockets;

                /* Count the number of ROMS for this socket */
                for (romptr = (rom *) socketptr->rom_list; romptr->image != NULL; romptr++)
                    num_roms++;

                if (num_roms == 1)
                    /* Attach the first image to the first unit */
                    rom_attach (&rom_unit[0], ((rom *) socketptr->rom_list)->device_mnemonic);
            }
            return SCPE_OK;
        }
    }

    /* Module type not found */
    return sim_messagef (SCPE_ARG, "Unknown module\n");
}


/* Show ROM module type */

t_stat rom_show_type (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (f, "module type %s\n", module_list[selected_type]->name);
    return SCPE_OK;
}


/* Test for disabled device */

static t_bool dev_disabled (DEVICE *dptr)
{
    return (dptr->flags & DEV_DIS ? TRUE : FALSE);
}

/* Set configuration mode MANUAL or AUTO */

static t_stat rom_set_configmode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
     /* Is a configuration type specified? */
    if (cptr == NULL)
        return sim_messagef (SCPE_ARG, "Specify AUTO or MANUAL configuration mode\n");

    if (strcasecmp (cptr, "MANUAL") == 0)
        rom_device_flags &= ~ROM_CONFIG_AUTO;
    else
        if (strcasecmp (cptr, "AUTO") == 0) {
            /* Check if auto config is available for the selected module */
            if (module_list[selected_type]->auto_config != NULL) {

                /* Set auto config mode */
                rom_device_flags |= ROM_CONFIG_AUTO;

                /* Perform auto-config */
                (*module_list[selected_type]->auto_config)();
            }
            else 
                return sim_messagef (SCPE_ARG, "Auto config is not available for the %s module\n",
                module_list[selected_type]->name);
        }
        else
            return sim_messagef (SCPE_ARG, "Unknown configuration mode, specify AUTO or MANUAL\n");
 
    return SCPE_OK;
}

t_stat rom_show_configmode (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
   fprintf (f, "configuration mode %s\n", 
       (rom_device_flags & ROM_CONFIG_AUTO) ? "AUTO" : "MANUAL");
    return SCPE_OK;
}

/* Examine routine */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    int32 data;
    t_stat r;

    r = rom_rd (&data, addr, 0);
    if (r != SCPE_OK)
        return r;
    *vptr = (t_value) data;
    return SCPE_OK;
}


/* ROM read routine */
// ToDo: Detect RESTORed condition
 
t_stat rom_rd (int32 *data, int32 PA, int32 access)
{
    uint32 unit_number;
    UNIT *uptr;

    for (unit_number = 0, uptr = rom_dev.units; 
         unit_number < rom_dev.numunits; unit_number++, uptr++) {
        
        // *** if (PA >= base_address[unit_number] && PA <= end_address[unit_number] && (uptr->flags & UNIT_ATT)) {
        // *** uint16 *image = (uint16 *) uptr->filebuf;
        if (PA >= base_address[unit_number] && PA <= end_address[unit_number] &&
            (rom_buffer[unit_number] != NULL)) {
            uint16 *image = (uint16 *) rom_buffer[unit_number];
            *data = image[(PA - base_address[unit_number]) >> 1];
            return SCPE_OK;
        }
    }

    return SCPE_NXM;
}


/*
 * Reset the ROM device.
 * The function is independent of the selected module. It is called
 * (several times) at simh start and when the user issues a RESET or
 * a SET CPU command.
 * Reset also tries to maintain a consistent CPU/ROM combination. It
 * checks if the CPU type is changed and in that case selects the BLANK
 * module type as that is a module type valid on all CPU's and busses.
 */
t_stat rom_reset (DEVICE *dptr)
{
    uint32 unit_number;

    /* Check if the CPU opt and/or type has been changed since the module
       type was selected. If so, select the BLANK module. */
    if (cpu_type != cpu_type_on_selection)
        rom_set_type (&rom_unit[0], 0, "BLANK", NULL);

    /* Initialize the UNIT and DIB structs and create the linked list of DIBs */
    for (unit_number = 0; unit_number < MAX_NUMBER_SOCKETS; unit_number++) {
        rom_dib[unit_number].next = (unit_number < dptr->numunits - 1) ? &rom_dib[unit_number + 1] : NULL;
    }

    rom_initialized = TRUE;
    return SCPE_OK;
}


/* Boot routine */

t_stat rom_boot (int32 u, DEVICE *dptr)
{
    cpu_set_boot (base_address[u]);
    return SCPE_OK;
}


/* 
 * Set ROM base address
 * This operation is only allowed on module types to which an image
 * can be attached, i.e. the BLANK ROM module.
 */
t_stat rom_set_addr (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 unit_number;
    int32 specified_address[MAX_NUMBER_SOCKETS] = {0, 0, 0, 0, 0};
    char glyph[CBUFSIZE];
    int32 addr;
    t_stat r;

    /* Check if the command is allowed for the selected module */
    if (module_list[selected_type]->type != ROM_FILE)
        return sim_messagef (SCPE_ARG, "Command not allowed for the selected module\n");

    /* Check if an address is specified */
    if (cptr == NULL)
        return sim_messagef (SCPE_ARG, "No address specified\n");

    /* Go through the specified adress string */
    for (unit_number = 0; 
        (unit_number < module_list[selected_type]->num_sockets) && (*cptr != 0); 
         unit_number++) {

        /* Get current base addresses as default address */
        specified_address[unit_number] = base_address[unit_number];

        /* Get next glyph from string */
        cptr = get_glyph (cptr, glyph, ';');

        /* Check if an address was given for this unit number */
        if (*glyph == 0)
            continue;

        /* Convert the adress glyph and check if produced a valid value */
        addr = (int32) get_uint (glyph, 8, IOPAGEBASE + IOPAGEMASK, &r);
        if (r != SCPE_OK)
            return r;

        /* Check if a valid adress is specified */
        if (addr < IOPAGEBASE)
            return sim_messagef (SCPE_ARG, "Address must be in I/O page, at or above 0%o\n",
            IOPAGEBASE);

        /* Check if the unit is not already attached */
         if (rom_unit[unit_number].flags & UNIT_ATT)
            return SCPE_ALATT;

        /* Save the specified adress for the unit number */
        specified_address[unit_number] = addr;
    }

    /* There shouldn't be any addresses left */
    if (*cptr != 0)
        return sim_messagef (SCPE_ARG, "Specify a maximum of %d addresses\n",
        module_list[selected_type]->num_sockets);

    // A valid address string has been specified, copy them to the registers */
    for (unit_number = 0;
        (unit_number < module_list[selected_type]->num_sockets); unit_number++) {
        base_address[unit_number] = end_address[unit_number] = specified_address[unit_number];
    }

    return SCPE_OK;
}

t_stat rom_show_address (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 socket_number;

    /* For all sockets */
    for (socket_number = 0;
        (socket_number < module_list[selected_type]->num_sockets); socket_number++) {
        fprintf (f, "Socket %1d: ", socket_number);
            
        /* If the unit has an address range print the range, otherwise print
           just the base address */
        if (end_address[socket_number] != base_address[socket_number])
            fprintf (f, "address=%o-%o\n", base_address[socket_number],
            end_address[socket_number] - 1);
        else
            fprintf (f, "address=%o\n", base_address[socket_number]);
    }
    return SCPE_OK;
}


t_stat rom_set_contents (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 socket_number;
    char specified_name[MAX_NUMBER_SOCKETS][CBUFSIZE] = {"", "", "", "", ""};
    FILE *fileref;

    /* Go through the specified ROM name string */
    for (socket_number = 0;
        (socket_number < module_list[selected_type]->num_sockets) && (*cptr != 0);
        socket_number++) {

        /* Get next glyph without upper case conversion from string */
        cptr = get_glyph_nc (cptr, specified_name[socket_number], ';');

        /* Check if a ROM name was given for this socket number */
        if (*specified_name[socket_number] == 0)
            continue;

        /* Check the ROM base address is set */
        if (base_address[socket_number] == 0)
            return sim_messagef (SCPE_ARG, "Set address for socket %d first\n", socket_number);

        /* Check if an existing file is specified */
        fileref = sim_fopen (specified_name[socket_number], "rb");
        if (fileref == NULL)
            return sim_messagef (SCPE_ARG, "File %s cannot be opened\n",
                specified_name[socket_number]);
        fclose (fileref);
    }

    /* There shouldn't be any ROM names left */
    if (*cptr != 0)
        return sim_messagef (SCPE_ARG, "Specify a maximum of %d ROMs\n",
        module_list[selected_type]->num_sockets);

    /* For all specified ROMs buffer the image */
    for (socket_number = 0;
        (socket_number < module_list[selected_type]->num_sockets); socket_number++) {

        /* Check if a ROM has to be seated */
        if (*specified_name[socket_number] != 0) {

            /* Deallocate previously allocated buffer */
            if (rom_buffer[socket_number] != NULL)
                free (rom_buffer[socket_number]);

            /* Copy the specified ROM name to the register */
            strncpy (rom_name[socket_number], specified_name[socket_number], CBUFSIZE);

            /* Open the file read-only */
            rom_fileref[socket_number] = sim_fopen (rom_name[socket_number], "rb");
            if (rom_fileref[socket_number] == NULL)
                return sim_messagef (SCPE_IERR, "File %s cannot be opened\n",
                rom_name[socket_number]);

            /* Get the file size and allocate a buffer with the appropriate size */
            // ToDo: Rename fileref to file pointer
            rom_size[socket_number] = (uint32) sim_fsize_ex (rom_fileref[socket_number]);
            rom_buffer[socket_number] = calloc (rom_size[socket_number], sizeof(char));

            /* Read ROM contents into image */
            // ToDo: Check return value against size?
            rom_size[socket_number] = (uint32) sim_fread (rom_buffer[socket_number],
                sizeof(char), rom_size[socket_number], rom_fileref[socket_number]);

            /* The file can now be closed */
            // ToDo: Just one fileref is needed?
            fclose (rom_fileref[socket_number]);

            /* Fill the DIB for the unit */
            if (reset_dib (socket_number, &rom_rd, NULL) != SCPE_OK) {

                // Remove ROM image
                // ToDo: Create remove_rom() function
                free (rom_buffer[socket_number]);
                rom_buffer[socket_number] = NULL;
                rom_fileref[socket_number] = NULL;
                *rom_name[socket_number] = 0;
                return SCPE_IERR;
            }

            /* Set end adress */
            end_address[socket_number] = base_address[socket_number] + 
                rom_size[socket_number];
        }
    }

    return SCPE_OK;
}


/* Show ROM contents and addresses */

t_stat rom_show_contents (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 socket_number;

    fprintf (f, "module type %s, ", module_list[selected_type]->name);
    fprintf (f, "configuration mode %s,\n",
        (rom_device_flags & ROM_CONFIG_AUTO) ? "AUTO" : "MANUAL");

    /* For all sockets */
    for (socket_number = 0;
        (socket_number < module_list[selected_type]->num_sockets); socket_number++) {
        fprintf (f, "Socket %1d: %s,\t", socket_number, 
            (*rom_name[socket_number] != 0) ? rom_name[socket_number] : "empty");

        /* If the unit has an address range print the range, otherwise print
           just the base address */
        if (end_address[socket_number] != base_address[socket_number])
            fprintf (f, "address=%o-%o\n", base_address[socket_number],
            end_address[socket_number] - 1);
        else
            fprintf (f, "address=%o\n", base_address[socket_number]);
    }
    return SCPE_OK;
}


/* (Re)set the DIB and build the Unibus table for the specified unit */

t_stat reset_dib (int unit_number, t_stat (reader (int32 *, int32, int32)),
    t_stat (writer (int32, int32, int32)))
{
    // *** DIB *dib = &rom_dib[uptr - rom_unit];
    // *** int unit_number = uptr - rom_unit;
    DIB *dib = &rom_dib[unit_number];

    dib->ba = base_address[unit_number];
    dib->lnt = rom_size[unit_number];
    dib->rd = reader;
    dib->wr = writer;
    return build_ubus_tab (&rom_dev, dib);
}

/*
 * Set the HITMISS register to 1 so the cache tests 16 and 17 of the
 * B0 11/60,70 Diagnostic ROM will succeed and the system will boot.
 */
void setHITMISS ()
{
    HITMISS = 1;
}

/* 
 * Attach either file or a built-in ROM image to a socket
 * 
 * As the DEV_DONTAUTO flag is not set, an already attached image
 * is detached before rom_attach() is called.
 */
t_stat rom_attach (UNIT *uptr, CONST char *cptr)
{
    int unit_number = uptr - rom_unit;
    module *modptr;
    rom_socket *socketptr;
    rom *romptr;
    t_stat r;

    switch (module_list[selected_type]->type) {
        case ROM_FILE:
            /* Check the ROM base address is set */
            if (base_address[unit_number] == 0)
                return sim_messagef (SCPE_ARG, "Set address first\n");

            /* Set quiet mode to avoid a "buffering file in memory" message */
            sim_switches |= SWMASK ('Q');

            /* Check and set unit capacity */
            uptr->capac = sim_fsize_name (cptr);
            if (uptr->capac == 0)
                return SCPE_OPENERR;

            /* Attach unit and check the result */
            r = attach_unit (uptr, cptr);
            if (r != SCPE_OK)
                return r;

            /* Fill the DIB for the unit */
            r = reset_dib (unit_number, &rom_rd, NULL);
            if (r != SCPE_OK)
                return rom_detach (uptr);

            /* Set end adress */
            end_address[unit_number] = base_address[unit_number] + uptr->capac;
            return SCPE_OK;

        case ROM_BUILTIN:
            /* Is function specified? */
            if (cptr == NULL)
                return sim_messagef (SCPE_ARG, "No ROM type specified\n");

            /* Get a pointer to the selected module and from that a pointer to
               socket for the unit */
            modptr = *(module_list + selected_type);
            socketptr = *modptr->sockets + unit_number;

            /* Try to find the ROM in the list of ROMs for this socket */
            romptr = find_rom (cptr, socketptr->rom_list);
            if (romptr != NULL) {
                /* Reset config mode  to manual and attach the unit */
                rom_device_flags &= ~ROM_CONFIG_AUTO;
                return attach_rom_to_unit (romptr, socketptr, uptr);
            }
            else
                /* ROM not found */
                return sim_messagef (SCPE_ARG, "Unknown ROM type\n");

        default:
            return SCPE_IERR;
    }
}


/*
 * Find the ROM with the specified name in a list with ROMs
 *
 * Inputs:
 *      cptr   = ROM name
 *      romptr = Pointer to list of ROM's 
 *
 * Return value:
 *      Pointer to ROM if found or NULL pointer if ROM is not
 *      found in the list.
 */
static rom *find_rom (const char *cptr, rom (*rom_listptr)[])
{
    rom *romptr;

    for (romptr = *rom_listptr; romptr->image != NULL; romptr++) {
        if (strcasecmp (cptr, romptr->device_mnemonic) == 0) {
            /* ROM found */
            return romptr;
        }
    }

    /* ROM not found*/
    return NULL;
}


/*
 * Attach the specified ROM with the information in the specified
 * socket to the specified unit
 * 
 * Inputs:
 *      romptr = Pointer to the ROM to attach
 *      uptr   = unit to attach the specified ROM to
 * 
 * Return value:
 *      Status code
 */

static t_stat attach_rom_to_unit (rom* romptr, rom_socket *socketptr, UNIT *uptr)
{
    int unit_number = uptr - rom_unit;

    /* Set image, adresses and capacity for the specified unit.
       The filename string is stored in an allocated buffer as detach_unit()
       wants to free the filename. */
    uptr->filename = strdup (romptr->device_mnemonic);
    uptr->filebuf = romptr->image;
    base_address[unit_number] = socketptr->base_address;
    end_address[unit_number] = socketptr->base_address + socketptr->size;
    uptr->capac = socketptr->size;
    uptr->flags |= UNIT_ATT;

    /* Execute rom specific function if available */
    if (romptr->rom_attached != NULL)
        (*romptr->rom_attached)();

    /* Fill the DIB for this unit */
    return reset_dib (unit_number, &rom_rd, NULL);
}


/*
 * Detach file or built in image from unit.
 */
t_stat rom_detach (UNIT *uptr)
{
    t_stat r;
    DIB *dib = &rom_dib[uptr - rom_unit];
    int unit_number = uptr - rom_unit;
 
    /* Leave address intact for modules with separate address
       and image specification (socket_number.e. the BLANK module type) */
    if (module_list[selected_type]->type == ROM_FILE)
        end_address[unit_number] = base_address[unit_number];
    else
        end_address[unit_number] = base_address[unit_number] = 0;

    r = reset_dib (unit_number, NULL, NULL);
    if (r != SCPE_OK)
        return r;

    /* Reset config mode to manual and detach the unit */
    rom_device_flags &= ~ROM_CONFIG_AUTO;
    return detach_unit (uptr);
}

t_stat rom_blank_help (FILE *st, const char *cptr)
{
    fprintf (st, rom_blank_helptext);
    return SCPE_OK;
}

/* Auto configure console emulator/diagnostics ROMs */

static t_stat m9312_auto_config ()
{
    t_stat result;

    if ((result = m9312_auto_config_console_roms()) != SCPE_OK)
        return result;
    return m9312_auto_config_bootroms();
}

/* Auto configure the console/diagnostic ROM for the M9312 module */
static t_stat m9312_auto_config_console_roms ()
{
    rom_for_cpu_model *mptr;

    /* Search device_rom map for a suitable boot ROM for the device */
    for (mptr = &cpu_rom_map[0]; mptr->rom_name != NULL; mptr++) {
        if (mptr->cpu_model == cpu_model)
            return attach_m9312_rom (mptr->rom_name, 0);
    }

    /* No ROM found for the CPU model */
    return SCPE_OK;
}

/* Auto configure boot ROMs for the M9312 module */

static t_stat m9312_auto_config_bootroms ()
{
    int32 dev_index;
    DEVICE *dptr;
    rom_for_device *mptr;
    int unit_number;
    t_stat result;

    /* Detach all units with boot ROMs (socket_number.e. 1-4) to avoid the possibility that on a 
       subsequent m9312 auto configuration with an altered device configuration
       a same ROM is present twice. */
    for (unit_number = 1; unit_number < NUM_M9312_SOCKETS; unit_number++) {
        detach_unit (&rom_unit[unit_number]);
    }

    /* For all devices */
    for (dev_index = 0, unit_number = 1; 
        ((dptr = sim_devices[dev_index]) != NULL) && (unit_number < NUM_M9312_SOCKETS); dev_index++) {

        /* Is the device enabled? */
        if (!dev_disabled (dptr)) {

            /* Search device_rom map for a suitable boot ROM for the device */
            for (mptr = &device_rom_map[0]; mptr->device_name != NULL; mptr++) {
                if (strcasecmp (dptr->name, mptr->device_name) == 0) {
                    if ((result = attach_m9312_rom (mptr->rom_name, unit_number)) != SCPE_OK)
                        return result;
                    unit_number++;
                    break;
                }
            }
        }
    }
    return SCPE_OK;
}

static t_stat attach_m9312_rom (const char *rom_name, int unit_number)
{
    UNIT *uptr;
    rom_socket *socketptr;
    rom *romptr;

    uptr = &rom_unit[unit_number];
    socketptr = &m9312_sockets[unit_number];

    /* Try to find the ROM in the list of ROMs for this socket */
    romptr = find_rom (rom_name, socketptr->rom_list);
    if (romptr != NULL) {
        return attach_rom_to_unit (romptr, socketptr, uptr);
    } else
        /* ROM not found */
        return SCPE_IERR;
}

t_stat rom_m9312_help (FILE *st, const char *cptr)
{
    rom *romptr;

    /* If a 'HELP ROM M9312' is given print the help text for the module */
    if (*cptr == '\0')
        fprintf (st, rom_m9312_helptext);
    else {
        /* Search for the name in diag rom list */
        for (romptr = diag_roms; romptr->image != NULL; romptr++) {
            if (strcasecmp (cptr, romptr->device_mnemonic) == 0) {
                fprintf (st, romptr->help_text);
                return SCPE_OK;
            }
        }

        /* Search for the name in boot rom list */
        for (romptr = boot_roms; romptr->image != NULL; romptr++) {
            if (strcasecmp (cptr, romptr->device_mnemonic) == 0) {
                fprintf (st, romptr->help_text);
                return SCPE_OK;
            }
        }

        /* The name wasn't found in both lists */
        fprintf (st, "Unknown ROM type\n");
    }

    return SCPE_OK;
}

t_stat rom_vt40_help (FILE *st, const char *cptr)
{
    fprintf (st, rom_vt40_helptext);
    return SCPE_OK;
}

/* Print help */

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    uint16 module_number;
    char gbuf[CBUFSIZE];

    /* If no argument to 'HELP ROM' is given print the general help text */
    if (*cptr == '\0')
        fprintf (st, rom_helptext);
    else {
        /* The (first) HELP ROM argument must be a module name. Look it up
           in the module list en call its help function. */
        cptr = get_glyph (cptr, gbuf, 0);

        for (module_number = 0; module_number < NUM_MODULES; module_number++) {
            if (strcasecmp (gbuf, module_list[module_number]->name) == 0)
                return (*module_list[module_number]->help_func) (st, cptr);
        }

        /* The module wasn't found in the module list */
        fprintf (st, "Unknown ROM module\n");
    }

    return SCPE_OK;
}


/* Print attach command help */

t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The ATTACH command is used to specify the contents of a ROM unit. For the BLANK\n");
    fprintf (st, "module a file must be specified. The file contents must be a flat binary image and\n");
    fprintf (st, "the unit ADDRESS must be set first.\n\n");
    fprintf (st, "For the M9312 module the function of the ROM must be specified. The units have\n");
    fprintf (st, "fixed adresses in the I/O space.\n");
    return SCPE_OK;
}


/* Return the ROM description */

const char *rom_description (DEVICE *dptr)
{
    return "Read-Only Memory";
}
