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
t_stat rom_set_type (UNIT *, int32, CONST char *, void *);
t_stat rom_show_type (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_configmode (UNIT *, int32, CONST char *, void *);
t_stat rom_show_configmode (FILE *, UNIT *, int32, CONST void *);
t_stat rom_show_sockets (FILE *, UNIT *, int32, CONST void *);
void set_socket_addresses ();
t_stat rom_attach (UNIT *, CONST char *);
t_stat attach_blank_rom (CONST char *);
t_stat attach_embedded_rom (CONST char *);
t_stat parse_cmd (CONST char *, cmd_parameter *, ATTACH_PARAM_VALUES *);
t_stat set_socket_for_attach (char *, ATTACH_PARAM_VALUES *);
t_stat set_address_for_attach (char *, ATTACH_PARAM_VALUES *);
t_stat set_address_not_allowed (char *, ATTACH_PARAM_VALUES *);
t_stat set_image_for_attach (char *, ATTACH_PARAM_VALUES *);
static t_stat attach_rom_to_socket (char *, t_addr, void *, int16, void (*)(), int);
t_stat rom_detach (UNIT *);
t_stat detach_all_sockets ();
t_stat detach_socket (uint32);
t_stat rom_blank_help (FILE *, const char *);
t_stat rom_m9312_help (FILE *, const char *);
t_stat rom_vt40_help (FILE *, const char *);
t_stat rom_help (FILE *st, DEVICE *, UNIT *, int32, const char *);
t_stat rom_help_attach (FILE *st, DEVICE *, UNIT *, int32, const char *);
const char *rom_description (DEVICE *);
t_stat reset_dib (int unit_number, t_stat (reader (int32 *, int32, int32)),
t_stat (writer (int32, int32, int32)));
static rom *find_rom (const char *cptr, rom (*rom_list)[]);
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
"   SET ROM CONFIGURATION=AUTO | MANUAL\n"
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
module_type_definition blank =
{
    "BLANK",                                /* Module name */
    ROM_FILE,                               /* Module type */
    CPUT_ALL,                               /* Required CPU types */
    QBUS_MODEL | UNIBUS_MODEL,              /* Required CPU options */
    NUM_BLANK_SOCKETS,                      /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & blank_sockets,     /* Pointer to rom_socket structs */
    NULL,                                   /* Auto configuration function */
    &rom_blank_help,                        /* Pointer to help function */
    attach_blank_rom,                       /* Attach function */
};

/* Define the M9312 module */

module_type_definition m9312 =
{
    "M9312",                                /* Module name */
    ROM_BUILTIN,                            /* Module type */
    CPUT_ALL,                               /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    NUM_M9312_SOCKETS,                      /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & m9312_sockets,     /* Pointer to rom_socket structs */
    &m9312_auto_config,                     /* Auto configuration function */
    &rom_m9312_help,                        /* Pointer to help function */
    attach_embedded_rom,                    /* Attach function */
};

/* Define the VT40 module */

module_type_definition vt40 =
{
    "VT40",                                 /* Module name */
    ROM_BUILTIN,                            /* Module type */
    CPUT_05,                                /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    NUM_VT40_SOCKETS,                       /* Number of sockets (units) */
    ROM_UNIT_FLAGS,                         /* UNIT flags */
    (rom_socket (*)[]) & vt40_sockets,      /* Pointer to rom_socket structs */
    NULL,                                   /* Auto configuration function */
    &rom_vt40_help,                         /* Pointer to help function */
    attach_embedded_rom,                    /* Attach function */
};

/*
 * Define the number of modules. The BLANK module must be the
 * first module in the list.
 */
#define NUM_MODULES            3
#define ROM_MODULE_BLANK       0

/* The list of available ROM modules */
module_type_definition* module_list[NUM_MODULES] =
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

/* Define and initialize the UNIT struct */
UNIT rom_unit = {UDATA (NULL, ROM_UNIT_FLAGS, 0)};

/* Define device information blocks */
DIB rom_dib[MAX_NUMBER_SOCKETS];

/* Static definitions */
static t_bool rom_initialized = FALSE;              /* Initialize rom_unit on first reset call */
static uint32 cpu_type_on_selection;                /* cpu_type for which module type was selected */
static uint32 rom_device_flags = 0;                 /* rom device specific flags */

/*
 * Define ROM device registers. All state is maintained in these registers
 * to allow the state to be saved and restored by a SAVE/RESTORE cycle.
 */
static uint16 selected_type = ROM_MODULE_BLANK;                     /* The module type as set by the user */

/* Define socket administration */
SOCKET_INFO socket_info[MAX_NUMBER_SOCKETS];

REG rom_reg[] = {
    { ORDATAD (TYPE,     selected_type, 16,     "Type"), REG_RO },
    { VBRDATA (SOCKET_INFO, socket_info, 16, 8, sizeof(socket_info)), REG_RO },
    { NULL }
};

/*
 * Define the ROM device modifiers.
 * 
 * A number of modifiers are marked "named only" to prevent them showing
 * up on the SHOW ROM output. The ROM SHOW CONTENTS command will print all
 * relevant information for a socket.
 */
MTAB rom_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "TYPE", "TYPE",
        &rom_set_type, &rom_show_type, NULL, "ROM type (BLANK, M9312 or VT40)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "CONFIGURATION", "CONFIGURATION",
        &rom_set_configmode, &rom_show_configmode, NULL, "Auto configuration (AUTO or MANUAL)" },
    { MTAB_VDV | MTAB_NMO, 0, "SOCKETS", NULL,
        NULL, &rom_show_sockets, NULL, "Socket addresses and ROM images" },
    { 0 }
};

/* Device definition */
DEVICE rom_dev =
{
    "ROM",                               /* Device name */
    &rom_unit,                           /* Pointer to device unit structures */
    rom_reg,                             /* Pointer to ROM registers */
    rom_mod,                             /* Pointer to modifier table */
    1,                                   /* Number of units */
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
    "RK",   "DK",                       /* RK03/05 DECdisk disk bootstrap */
    "RL",   "DL",                       /* RL01/02 cartridge disk bootstrap */
    "HK",   "DM",                       /* RK06/07 cartridge disk bootstrap */
    "RY",   "DY",                       /* RX02 floppy disk, double density bootstrap */
    "RP",   "DB",                       /* RP04/05/06, RM02/03/05 cartridge disk bootstrap */
    "RQ",   "DU",                       /* MSCP UDA50 (RAxx) disk bootstrap */
    "RX",   "DX",                       /* RX01 floppy disk, single density bootstrap */
    "RY",   "DY",                       /* RX02 floppy disk, double density bootstrap */
    "RS",   "DS",                       /* RS03/04 fixed disk bootstrap */
    "TC",   "DT",                       /* TU55/56 DECtape */
    "TS",   "MS",                       /* TS04/11,TU80,TSU05 tape bootstrap */
    "TU",   "MU",                       /* TMSCP TK50,TU81 magtape bootstrap */
    "TA",   "CT",                       /* TU60 DECcassette bootstrap */
    "TM",   "MT",                       /* TS03,TU10,TE10 magtape bootstrap */
    "TQ",   "MU",                       /* TMSCP TK50,TU81 magtape bootstrap */
    NULL,   NULL,
};

/*
 * Define a mapping from CPU model to the console/diagnostic ROM for 
 * the model.
 */

rom_for_cpu_model cpu_rom_map[] =
{
    MOD_1104,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1105,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1124,           "MEM",          /* 11/24 Diagnostic/Console (MEM; M7134 E74) */
    MOD_1134,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1140,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1144,           "UBI",          /* 11/44 Diagnostic / Console (UBI; M7098 E58) */
    MOD_1145,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1160,           "B0",           /* 11/60,70 Diagnostic (M9312 E20) */
    MOD_1170,           "B0",           /* 11/60,70 Diagnostic (M9312 E20) */
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

                /* Detach the currently attached ROM's from their sockets */
                detach_all_sockets ();

                /* Set socket base and end address if available */
                set_socket_addresses ();

                /* (Re)set the configuration mode to manual */
                rom_device_flags &= ~ROM_CONFIG_AUTO;
            }
#if 0
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
#endif
            return SCPE_OK;
        }
    }

    /* Module type not found */
    return sim_messagef (SCPE_ARG, "Unknown module\n");
}


/* Show ROM module type */

t_stat rom_show_type (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (f, "module type %s", module_list[selected_type]->name);
    return SCPE_OK;
}


/* Set (if available) base and end addresses for the sockets */

void set_socket_addresses ()
{
    module_type_definition *modptr;
    rom_socket *socketptr;
    uint32 socket_number;

    /* Get a pointer to the selected module and from that a pointer to
   socket for the unit */
    modptr = *(module_list + selected_type);
    socketptr = *modptr->sockets;

    /* For all sockets on this module */
    for (socket_number = 0; socket_number < modptr->num_sockets;
        socket_number++, socketptr++) {
        
        /* Fill socket_info only if a valid address is given */
        if (socketptr->base_address > 0) {
            socket_info[socket_number].base_address = socketptr->base_address;
            socket_info[socket_number].rom_size = socketptr->size;
            socket_info[socket_number].end_address 
                = socketptr->base_address + socketptr->size - 1;
        }
    }
}

/* Show socket addresses and contents */

t_stat rom_show_sockets (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 socket_number;

    for (socket_number = 0;
        socket_number < module_list[selected_type]->num_sockets; socket_number++) {
        fprintf (f, "socket %d: ", socket_number);
        if (socket_info[socket_number].rom_size > 0)
            fprintf (f, "address=%o-%o, ", socket_info[socket_number].base_address,
            socket_info[socket_number].base_address +
            socket_info[socket_number].rom_size - 1);
        else
            fprintf (f, "address=%o, ", socket_info[socket_number].base_address);
        fprintf (f, "image=%s\n",
            (*socket_info[socket_number].rom_name != 0) ?
            socket_info[socket_number].rom_name : "none");
    }
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
        return sim_messagef (SCPE_ARG, "Specify AUTO or MANUAL configuration\n");

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
                return sim_messagef (SCPE_ARG, "Auto configuration is not available for the %s module\n",
                module_list[selected_type]->name);
        }
        else
            return sim_messagef (SCPE_ARG, "Unknown configuration mode, specify AUTO or MANUAL\n");
 
    return SCPE_OK;
}

t_stat rom_show_configmode (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
   fprintf (f, "configuration mode %s", 
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
 
t_stat rom_rd (int32 *data, int32 PA, int32 access)
{
    uint32 socket_number;

    for (socket_number = 0; 
        socket_number < module_list[selected_type]->num_sockets; socket_number++) {
        
        if (PA >= socket_info[socket_number].base_address && 
            PA <= socket_info[socket_number].end_address &&
            (socket_info[socket_number].rom_buffer != NULL)) {
            uint16 *image = (uint16 *) socket_info[socket_number].rom_buffer;
            *data = image[(PA - socket_info[socket_number].base_address) >> 1];
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
    uint32 socket_number;

    /* Check if the CPU opt and/or type has been changed since the module
       type was selected. If so, select the BLANK module. */
    if (cpu_type != cpu_type_on_selection)
        rom_set_type (&rom_unit, 0, "BLANK", NULL);

    /* Initialize the UNIT and DIB structs and create the linked list of DIBs */
    for (socket_number = 0; socket_number < MAX_NUMBER_SOCKETS; socket_number++) {
        rom_dib[socket_number].next = 
            (socket_number < dptr->numunits - 1) ? &rom_dib[socket_number + 1] : NULL;
    }

    rom_initialized = TRUE;
    return SCPE_OK;
}


/* Boot routine */

t_stat rom_boot (int32 u, DEVICE *dptr)
{
    cpu_set_boot (socket_info[u].base_address);
    return SCPE_OK;
}


/* (Re)set the DIB and build the Unibus table for the specified socket */

t_stat reset_dib (int socket_number, t_stat (reader (int32 *, int32, int32)),
    t_stat (writer (int32, int32, int32)))
{
    DIB *dib = &rom_dib[socket_number];

    // ToDo: socket_info shouldn't be global?!
    dib->ba = socket_info[socket_number].base_address;
    dib->lnt = socket_info[socket_number].rom_size;
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
 * ToDo: As the DEV_DONTAUTO flag is not set, an already attached image
 * is detached before rom_attach() is called.
 */
t_stat rom_attach (UNIT *uptr, CONST char *cptr)
{
    /* Execute attach cmd for the selected module type */
    return (*module_list[selected_type]->attach)(cptr);
}

/*
 * Processing the ATTACH command is performed in three steps:
 * 1. Parsing the command parameters,
 * 2. Validating the parameters,
 * 3. Executing the command.
 *
 * This sequence is necessary to allow a free ordering of the parameters
 * and to check that every parameter is specified just once.
 */
t_stat attach_blank_rom (CONST char *cptr)
{
    int socket_number;
    t_stat r = SCPE_OK;
    FILE *fileptr;
    void *rom_image = NULL;
    uint32 rom_size;
    ATTACH_PARAM_VALUES param_values = {-1, -1, ""};

    cmd_parameter attach_parameters[] =
    {
        {"SOCKET",  &set_socket_for_attach},        /* SOCKET parameter should occur exactly once */
        {"ADDRESS", &set_address_for_attach},       /* ADDRESS parameter is optional */
        {"IMAGE",   &set_image_for_attach},         /* IMAGE parameter is optional */
        {NULL},
    };

    /* Parse the command */
    if ((r = parse_cmd (cptr, attach_parameters, &param_values)) != SCPE_OK)
        return r;

    socket_number = param_values.socket_number;

    /* Validate the parameters */
    if (socket_number == -1)
        return sim_messagef (SCPE_ARG, "SOCKET specification is required\n");
    
    if ((param_values.address == -1) &&
        (*param_values.image_name == 0))
        return sim_messagef (SCPE_ARG, "Either ADDRESS or IMAGE or both must be specified");

    if (param_values.address != -1) {

        /* Set base and preliminary end address */
        socket_info[socket_number].base_address =
            socket_info[socket_number].end_address = param_values.address;
    }

    /* If a ROM image is specified an attach can be performed */
    if (*param_values.image_name != 0) {

        /* Check the ROM base address is set */
        // ToDo: Initialze attach_cmd_params.address to 0
        if (socket_info[socket_number].base_address == 0)
            return sim_messagef (SCPE_ARG, "Set address first\n", socket_number);

        /* Check if an existing file is specified */
        fileptr = sim_fopen (param_values.image_name, "rb");
        if (fileptr == NULL)
            return sim_messagef (SCPE_ARG, "File %s cannot be opened\n",
                param_values.image_name);

        /* Get the file size and allocate a buffer with the appropriate size */
        rom_size = (uint32) sim_fsize_ex (fileptr);
        rom_image = calloc (rom_size, sizeof (char));

        /* Read ROM contents into image */
        rom_size = (uint32) sim_fread (rom_image, sizeof (char), rom_size, fileptr);

        /* The file can now be closed */
        fclose (fileptr);

        /* Attach the created ROM to the socket */
        r = attach_rom_to_socket (param_values.image_name,
            socket_info[socket_number].base_address, rom_image, rom_size,
            NULL, socket_number);
    }
    
    /* Free allocated resources */
    if (rom_image != NULL)
        free (rom_image);
    
    return r;
}

/*
 * Processing the ATTACH command is performed in three steps:
 * 1. Parsing the command parameters,
 * 2. Validating the parameters,
 * 3. Executing the command.
 *
 * This sequence is necessary to allow a free ordering of the parameters
 * and to check that every parameter is specified just once.
 */
t_stat attach_embedded_rom (CONST char *cptr)
{
    int socket_number;
    module_type_definition *modptr;
    rom_socket *socketptr;
    rom *romptr;
    t_stat r;
    ATTACH_PARAM_VALUES param_values = {-1, -1, ""};

    cmd_parameter attach_parameters[] =
    {
        {"SOCKET",  &set_socket_for_attach},        /* SOCKET parameter should occur exactly once */
        {"ADDRESS", &set_address_not_allowed},      /* ADDRESS parameter is optional */
        {"IMAGE",   &set_image_for_attach},         /* IMAGE parameter is optional */
        {NULL},
    };

    /* Parse the command */
    if ((r = parse_cmd (cptr, attach_parameters, &param_values)) != SCPE_OK)
        return r;

    socket_number = param_values.socket_number;

    /* Validate the command */
    if (socket_number == -1)
        return sim_messagef (SCPE_ARG, "SOCKET specification is required\n");

    if (*param_values.image_name == 0)
        return sim_messagef (SCPE_ARG, "A ROM IMAGE must be specified\n");

    /* Execute the command */

    /* Get a pointer to the selected module and from that a pointer to
       socket for the unit */
    modptr = *(module_list + selected_type);
    socketptr = *modptr->sockets + socket_number;

    /* Try to find the ROM in the list of ROMs for this socket */
    if ((romptr = find_rom (param_values.image_name, socketptr->rom_list)) == NULL)
        return sim_messagef (SCPE_ARG, "Unknown ROM type\n");

    /* Attach the ROM to the socket */
    if ((r = attach_rom_to_socket (romptr->device_mnemonic, socketptr->base_address,
        romptr->image, socketptr->size, romptr->rom_attached, socket_number)) != SCPE_OK) {
        return r;
    }

    /* (Re)set the configuration mode to manual */
    rom_device_flags &= ~ROM_CONFIG_AUTO;

    return SCPE_OK;
}

/*
 * Parse an ATTACH command string
 * 
 * Syntax: ATTACH ROM SOCKET=<socket>,ADDRESS=<address>,IMAGE=<image name>
 * 
 * The parameters can be abbreviated to one character, The SOCKET parameters is
 * required; the ADDRESS and IMAGE parameters are optional, but at least one of
 * them has to be specified. No specific ordering of the parameters is required.
 * Every parameter must be specified just once.
 * 
 * In:  cptr            - The command to be parsed
 * In:  valid_params    - The valid ATTACH command parameters
 * Out: valid_params    - The value and occurence of the specified
 *                        parameters
 * 
 * Result:
 *      SCPE_OK         - A command was succesfully parsed
 *      SCPE_ARG        - No meaningful command could be parsed
 */
t_stat parse_cmd (CONST char *cptr, cmd_parameter *valid_params, 
    ATTACH_PARAM_VALUES *attach_cmd_params)
{
    char glyph[CBUFSIZE];
    char *param_value_ptr;
    cmd_parameter *param;
    t_stat result;

    while (*cptr != 0) {

        /* Get next "glyph" (i.e. parameter plus value) without conversion
           to upper case as a file name can be part of the command */
        cptr = get_glyph_nc (cptr, glyph, ',');

        /* Check if parameter has equal sign and parameter value. Then
           terminate parameter name and point to value */
        if ((param_value_ptr = strchr (glyph, '=')))
            *param_value_ptr++ = 0;

        /* Check if a parameter value has been specified */
        if ((param_value_ptr == NULL) || (*param_value_ptr == 0))
            return sim_messagef (SCPE_ARG, "Parameter %s requires value\n", glyph);

        /* Check against valid parameters */
        for (param = valid_params; param->name != NULL; param++) {

            /* Check if this parameter matches a valid parameter */
            if (MATCH_CMD (glyph, param->name) == 0) {

                /* Save parameter */
                if (result =
                    (*param->set_parameter) (param_value_ptr, attach_cmd_params) != SCPE_OK)
                    return result;
                break;
            }
        }

        /* Check if parameter found */
        if (param->name == NULL)
            return sim_messagef (SCPE_ARG, "Unknown parameter %s\n", glyph);
    }
    return SCPE_OK;
}


/* Set the socket number for the current ATTACH command */

t_stat set_socket_for_attach (char *value, ATTACH_PARAM_VALUES *context)
{
    char *ptr;
    uint32 socket_number = strtol (value, &ptr, 0);

    /* Check if socket number is not already set */
    if (context->socket_number != -1)
        return sim_messagef (SCPE_ARG, "SOCKET must be specified just once\n");

    /* Convert value to int, check validity and save socket number */
    if ((socket_number >= 0) &&
        (socket_number < module_list[selected_type]->num_sockets)) {
        context->socket_number = socket_number;
        return SCPE_OK;
    }
    return sim_messagef (SCPE_ARG, "SOCKET must be in range 0 to %d\n",
        module_list[selected_type]->num_sockets - 1);
}


/* 
 * Set the address to attach the image on. This only applies to the
 * BLANK module type.
 */
t_stat set_address_for_attach (char *value, ATTACH_PARAM_VALUES *context)
{
    t_value r;
    int32   address;

    /* ToDo: Check that unit isn't already attached */

    /* Check if address is not already set */
    if (context->address != -1)
        return sim_messagef (SCPE_ARG, "ADDRESS must be specified just once\n");

    /* Convert the adress glyph and check if produced a valid value */
    address = (int32) get_uint (value, 8, IOPAGEBASE + IOPAGEMASK, &r);
    if (r != SCPE_OK)
        return r;

    /* Check if a valid adress is specified */
    if (address < IOPAGEBASE)
        return sim_messagef (SCPE_ARG, "ADDRESS must be in I/O page, at or above 0%o\n",
        IOPAGEBASE);

    context->address = address;
    return SCPE_OK;
}


/* Return an error on the ADDRESS parameter */

t_stat set_address_not_allowed (char *value, ATTACH_PARAM_VALUES * context)
{
    return sim_messagef (SCPE_ARG,
        "ADDRESS parameter not allowed for the selected module type\n");
}



/* Set the ROM image name to attach on the socket */

t_stat set_image_for_attach (char *value,
    ATTACH_PARAM_VALUES *attach_param_values)
{
    /* Check if image is not already set */
    if (*attach_param_values->image_name != 0)
        return sim_messagef (SCPE_ARG, "IMAGE must be specified just once\n");
        
    strcpy (attach_param_values->image_name, value);
    return SCPE_OK;
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
 * Attach the specified ROM with the information to the specified
 * socket.
 */
static t_stat attach_rom_to_socket (char* rom_name, t_addr base_address,
    void *image, int16 size, void (*rom_attached)(), int socket_number)
{
    
    /* Set the ROM size and pointer to the ROM image */
    socket_info[socket_number].base_address = base_address;
    socket_info[socket_number].rom_size = size;
    socket_info[socket_number].end_address = base_address + size - 1;

    /* Deallocate previously allocated buffer */
    if (socket_info[socket_number].rom_buffer != NULL)
        free (socket_info[socket_number].rom_buffer);

    /*
     * Allocate a buffer for the ROM image and copy the image into the
     * buffer, allowing to overwrite the ROM image.
     */
    if ((socket_info[socket_number].rom_buffer =
        malloc (socket_info[socket_number].rom_size)) == 0)
        return sim_messagef (SCPE_IERR, "malloc() failed\n");

    memcpy (socket_info[socket_number].rom_buffer,
        image, socket_info[socket_number].rom_size);

    /* Fill the DIB for the socket */
    if (reset_dib (socket_number, &rom_rd, NULL) != SCPE_OK) {

        // Remove ROM image
        // ToDo: Create remove_rom() function
        free (socket_info[socket_number].rom_buffer);
        socket_info[socket_number].rom_buffer = NULL;
        return sim_messagef (SCPE_IERR, "reset_dib() failed\n");
    }

    /* Save the specified ROM image  name in the register */
    strncpy (socket_info[socket_number].rom_name,
        rom_name, CBUFSIZE);

    /* Execute rom specific function if available */
    if (rom_attached != NULL)
        (*rom_attached)();

    /* Create attachment information so attach is called during a restore */
    // ToDo: Set meaningful uptr->filename
    // ToDo: Reduce rom_unit
    rom_unit.flags |= UNIT_ATT;
    rom_unit.dynflags |= UNIT_ATTMULT;
    rom_unit.filename = "M9312";

    return SCPE_OK;
}


/* DETACH ROM routine */

t_stat rom_detach (UNIT *uptr)
{
    t_stat r;

    if ((r = detach_all_sockets ()) != SCPE_OK)
        return r;

    /* Set socket base and end address if available */
    set_socket_addresses ();

    /* (Re)set the configuration mode to manual */
    rom_device_flags &= ~ROM_CONFIG_AUTO;

    return SCPE_OK;
}


/* Detach images from all sockets */

t_stat detach_all_sockets ()
{
    uint32 socket_number;
    t_stat r;

    /* For all sockets */
    for (socket_number = 0;
        socket_number < module_list[selected_type]->num_sockets;
        socket_number++) {

        /* Clear socket information */
        if ((r = detach_socket (socket_number) != SCPE_OK))
            return r;

        // ToDo: The UNIT_ATT flag should be reset in detach_socket()
        // if no sockets are attched anymore?
        rom_unit.flags &= ~UNIT_ATT;
    }
    return SCPE_OK;
}

/* Detach the image from the specified socket */

t_stat detach_socket (uint32 socket_number)
{
    /* Clear socket information */
    socket_info[socket_number].base_address = 0;
    socket_info[socket_number].end_address = 0;
    socket_info[socket_number].rom_size = 0;
    if (socket_info[socket_number].rom_buffer != NULL) {
        free (socket_info[socket_number].rom_buffer);
        socket_info[socket_number].rom_buffer = NULL;
    }
    *socket_info[socket_number].rom_name = 0;

    /* Clear Unibus map */
    return reset_dib (socket_number, NULL, NULL);
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
    int socket_number;
    t_stat result;

    /* Detach all units with boot ROMs (socket_number.e. 1-4) to avoid the possibility that on a 
       subsequent m9312 auto configuration with an altered device configuration
       a same ROM is present twice. */
    for (socket_number = 1; socket_number < NUM_M9312_SOCKETS; socket_number++) {
        detach_socket (socket_number);
    }

    /* For all devices */
    for (dev_index = 0, socket_number = 1; 
        ((dptr = sim_devices[dev_index]) != NULL) && (socket_number < NUM_M9312_SOCKETS); dev_index++) {

        /* Is the device enabled? */
        if (!dev_disabled (dptr)) {

            /* Search device_rom map for a suitable boot ROM for the device */
            for (mptr = &device_rom_map[0]; mptr->device_name != NULL; mptr++) {
                if (strcasecmp (dptr->name, mptr->device_name) == 0) {
                    if ((result = attach_m9312_rom (mptr->rom_name, socket_number)) != SCPE_OK)
                        return result;
                    socket_number++;
                    break;
                }
            }
        }
    }
    return SCPE_OK;
}



static t_stat attach_m9312_rom (const char *rom_name, int socket_number)
{
    rom_socket *socketptr;
    rom *romptr;

    socketptr = &m9312_sockets[socket_number];

    /* Try to find the ROM in the list of ROMs for this socket */
    romptr = find_rom (rom_name, socketptr->rom_list);
    if (romptr != NULL) {
        return attach_rom_to_socket (romptr->device_mnemonic, socketptr->base_address,
            romptr->image, socketptr->size, romptr->rom_attached, socket_number);
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
