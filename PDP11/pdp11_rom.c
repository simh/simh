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
t_stat rom_rd (int32 *data, int32 PA, int32 access);
t_stat rom_wr (int32 data, int32 PA, int32 access);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_boot (int32 u, DEVICE *dptr);
t_stat rom_set_type (UNIT *, int32, CONST char *, void *);
t_stat rom_show_type (FILE *, UNIT *, int32, CONST void *);
static t_stat rom_set_configmode(UNIT*, int32, CONST char*, void*);
t_stat rom_show_configmode (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_start_address (UNIT *, int32, CONST char *, void *);
t_stat rom_show_start_address (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_write_enable (UNIT*, int32, CONST char*, void*);
t_stat rom_show_sockets (FILE *, UNIT *, int32, CONST void *);
t_stat rom_attach (UNIT *, CONST char *);
t_stat rom_detach (UNIT *);
const char *rom_description (DEVICE *);

/* Forward references for helper functions */
t_stat blank_set_start_address (UNIT*, int32, CONST char*, void*);
t_stat m9312_set_start_address (UNIT*, int32, CONST char*, void*);
t_stat blank_show_start_address (FILE*);
t_stat m9312_show_start_address (FILE*);
t_stat get_numerical_start_address (const char*, int32*);
t_stat get_symbolic_start_address (const char*, int32*);
t_bool digits_only (const char*);
static t_bool address_available (int32);
static void set_socket_addresses ();
static int module_type_is_valid (uint16);
uint16 *get_rom_loc_ptr (int32);
static t_stat blank_attach (CONST char *);
static t_stat embedded_attach (CONST char *);
static t_stat parse_attach_cmd_seq (CONST char *, t_bool, ATTACH_CMD *, uint32 *);
static t_stat parse_attach_cmd (CONST char *, t_bool, ATTACH_CMD *, uint32 *);
static t_stat validate_attach_blank_rom (ATTACH_CMD *, uint32);
static t_stat validate_attach_embedded_rom (ATTACH_CMD *, uint32);
static t_stat exec_attach_blank_rom (ATTACH_CMD *);
static t_stat exec_attach_embedded_rom (ATTACH_CMD *);
static t_stat attach_rom_to_socket (char *, t_addr, void *, int16, void (*)(), int);
static t_stat vt40_auto_attach ();
static void create_filename_blank (char *);
static void create_filename_embedded (char*);
static void strclean(char *, CONST char *);
static t_stat detach_all_sockets ();
static t_stat detach_socket (uint32);
t_stat m9312_rd(int32* data, int32 PA, int32 access);
t_stat blank_rom_rd(int32* data, int32 PA, int32 access);
static t_stat rom_help (FILE *st, DEVICE *, UNIT *, int32, const char *);
static t_stat rom_help_attach (FILE *st, DEVICE *, UNIT *, int32, const char *);
static t_stat reset_dib (int, t_stat (reader (int32 *, int32, int32)),
    t_stat (writer (int32, int32, int32)));
static ROM_DEF *find_rom (const char *cptr, ROM_DEF (*rom_list)[]);
static t_stat m9312_auto_config ();
static t_stat m9312_auto_config_console_roms ();
static t_stat m9312_auto_config_bootroms ();
static t_stat m9312_attach (const char *rom_name, int unit_number);

/* External references */
extern uint32 cpu_type;
extern uint32 cpu_opt;
extern uint32 cpu_model;
extern int32 HITMISS;

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
MODULE_DEF blank =
{
    "BLANK",                                /* Module name */
    CPUT_ALL,                               /* Required CPU types */
    QBUS_MODEL | UNIBUS_MODEL,              /* Required CPU options */
    BLANK_NUM_SOCKETS,                      /* Number of sockets */
    (SOCKET_DEF(*)[]) &blank_sockets,       /* Pointer to SOCKET_DEF structs */
    NULL,                                   /* Auto configuration function */
    blank_attach,                           /* Attach function */
    NULL,                                   /* Auto-attach function */
    create_filename_blank,                  /* Create unit file name */
    blank_rom_rd,                           /* ROM read function */
    blank_set_start_address,                /* ROM set start address */
    blank_show_start_address                /* ROM show start address */
};

/* Define the M9312 module */

MODULE_DEF m9312 =
{
    "M9312",                                /* Module name */
    CPUT_ALL,                               /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    M9312_NUM_SOCKETS,                      /* Number of sockets */
    (SOCKET_DEF (*)[]) & m9312_sockets,     /* Pointer to SOCKET_DEF structs */
    &m9312_auto_config,                     /* Auto configuration function */
    embedded_attach,                        /* Attach function */
    NULL,                                   /* Auto-attach function */
    create_filename_embedded,               /* Create unit file name */
    m9312_rd,                               /* ROM read function */
    m9312_set_start_address,                /* ROM set start address */
    m9312_show_start_address                /* ROM show start address */
};

/* Define the VT40 module */

MODULE_DEF vt40 =
{
    "VT40",                                 /* Module name */
    CPUT_05,                                /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    VT40_NUM_SOCKETS,                       /* Number of sockets */
    (SOCKET_DEF (*)[]) & vt40_sockets,      /* Pointer to SOCKET_DEF structs */
    NULL,                                   /* Auto configuration function */
    embedded_attach,                        /* Attach function */
    &vt40_auto_attach,                      /* Auto-attach function */
    create_filename_embedded,               /* Create unit file name */
    blank_rom_rd,                           /* ROM read function */
    blank_set_start_address,                /* ROM set start address */
    blank_show_start_address                /* ROM show start address */
};

/*
 * Define the number of modules. The BLANK module must be the
 * first module in the list.
 */
#define NUM_MODULES            3
#define ROM_MODULE_BLANK       0

/* The list of available ROM modules */
MODULE_DEF* module_list[NUM_MODULES] =
{
    &blank,
    &m9312,
    &vt40,
};


/*
 * The maximum number of sockets is the number of sockets any module can have.
 */ 
#define ROM_MAX_SOCKETS    5

#define SOCKET1             1
#define SOCKET2             2
#define TRAP_LOCATION       024

/* Define and initialize the UNIT struct */
UNIT rom_unit = {UDATA (NULL, ROM_UNIT_FLAGS, 0)};

/* Define device information blocks */
DIB rom_dib[ROM_MAX_SOCKETS];

/* Static definitions */
static uint32 cpu_type_on_selection;                        /* cpu_type for which module type was selected */
static uint32 rom_device_flags = 0;                         /* ROM_DEF device specific flags */
static char unit_filename[M9312_NUM_SOCKETS * CBUFSIZE];    /* Composed file name for UNIT */

/*
 * Define ROM device registers. All state is maintained in these registers
 * to allow the state to be saved and restored by a SAVE/RESTORE cycle.
 */
static uint16 selected_type = ROM_MODULE_BLANK;             /* The module type as set by the user */
static int32 rom_start_address = 0;                         /* ROM bootstrap start address */

/* Define socket configuration */
SOCKET_CONFIG socket_config[ROM_MAX_SOCKETS];

/* Define variables to be saved and restored via registers */

REG rom_reg[] = {
    { ORDATAD (TYPE,          selected_type,     16,   "Type"), REG_RO  },
    { ORDATAD (START_ADDRESS, rom_start_address, 16,   "Start address"), REG_RO  },
    { NULL }
};

/*
 * Define the ROM device modifiers.
 * 
 * The WRITE_ENABLE modifier is settable only modifier. To prevent the value
 * of this parameter is shown twice in the SHOW ROM output, there is no
 * SHOW ROM WRITE_ENABLE command and it's value is shown by the units
 * read-only flag.
 * 
 * The SOCKETS modifier is a view-only modifer and prints the relevant
 * information for all sockets in a pretty format.
 */
MTAB rom_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "TYPE", "TYPE",
        &rom_set_type, &rom_show_type, NULL, "ROM type (BLANK, M9312 or VT40)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "CONFIGURATION", "CONFIGURATION",
        &rom_set_configmode, &rom_show_configmode, NULL, "Auto configuration (AUTO or MANUAL)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "START_ADDRESS", "START_ADDRESS",
        &rom_set_start_address, &rom_show_start_address, NULL, "ROM bootstrap start address" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, NULL, "WRITE_ENABLE",
        &rom_set_write_enable, NULL, NULL, "Write-enable ROM" },
    { MTAB_VDV | MTAB_NMO, 0, "SOCKETS", NULL,
        NULL, &rom_show_sockets, NULL, "Socket addresses and ROM images" },
    { 0 }
};

/* 
 * ROM device definition
 * 
 * The device provide no examine address functionality as the device
 * has just one unit while it has more than one socket with different
 * address spaces. The ROM contents are accessed via the CPU device.
 */
DEVICE rom_dev =
{
    "ROM",                               /* Device name */
    &rom_unit,                           /* Pointer to device unit structures */
    rom_reg,                             /* Pointer to ROM registers */
    rom_mod,                             /* Pointer to modifier table */
    1,                                   /* Number of units */
    0,                                   /* Address radix */
    0,                                   /* Address width */
    0,                                   /* Address increment */
    8,                                   /* Data radix */
    16,                                  /* Data width */
    NULL,                                /* Examine routine not available */
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
    MOD_1124,           "D0",           /* 11/24 Diagnostic/Console (MEM; M7134 E74) */
    MOD_1134,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1140,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1144,           "C0",           /* 11/44 Diagnostic / Console (UBI; M7098 E58) */
    MOD_1145,           "A0",           /* 11/04,34 Diagnostic/Console (M9312 E20) */
    MOD_1160,           "B0",           /* 11/60,70 Diagnostic (M9312 E20) */
    MOD_1170,           "B0",           /* 11/60,70 Diagnostic (M9312 E20) */
    0,                   NULL,
};

/* Set ROM module type */

t_stat rom_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint16 module_number;

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
             * from the previously selected type, initialize the sockets
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

                /* Auto-attach ROM(s) if available */
                if (module_list[selected_type]->auto_attach != NULL)
                    (*module_list[selected_type]->auto_attach)();
            }
            return SCPE_OK;
        }
    }

    /* Module type not found */
    return sim_messagef (SCPE_ARG, "Unknown module\n");
}


/*
 * Check if the module type to be set is valid on the selected
 * cpu_opt and cpu_type
 */
static int module_type_is_valid (uint16 module_number)
{
    uint32 bus = UNIBUS ? UNIBUS_MODEL : QBUS_MODEL;

    return CPUT (module_list[module_number]->valid_cpu_types) &&
        bus & module_list[module_number]->valid_cpu_opts;
}


/* Show ROM module type */

t_stat rom_show_type (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (f, "module type %s", module_list[selected_type]->name);
    return SCPE_OK;
}


/* Set (if available) base address for the sockets */

static void set_socket_addresses ()
{
    MODULE_DEF *modptr;
    SOCKET_DEF *socketptr;
    uint32 socket_number;

   /* 
    * Get a pointer to the selected module and from that a pointer to
    * socket for the module.
    */
    modptr = *(module_list + selected_type);
    socketptr = *modptr->sockets;

    /* For all sockets on this module */
    for (socket_number = 0; socket_number < modptr->num_sockets;
        socket_number++, socketptr++) {
        
        /* Fill socket_info only if a valid address is given */
        if (socketptr->base_address > 0) {
            socket_config[socket_number].base_address = socketptr->base_address;
            socket_config[socket_number].rom_size = socketptr->size;
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
        
        if (socket_config[socket_number].rom_size > 0)
            fprintf(f, "address=%o-%o, ", socket_config[socket_number].base_address,
                socket_config[socket_number].base_address + 
                socket_config[socket_number].rom_size - 1);
        else
            fprintf(f, "address=%o, ", socket_config[socket_number].base_address);
        fprintf(f, "image=%s\n",
            (*socket_config[socket_number].rom_name != 0) ? 
                socket_config[socket_number].rom_name : "none");
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
    if (MATCH_CMD (cptr, "MANUAL") == 0)
        rom_device_flags &= ~ROM_CONFIG_AUTO;
    else
        if (MATCH_CMD (cptr, "AUTO") == 0) {

            /* Check if auto config is available for the selected module */
            if (module_list[selected_type]->auto_config != NULL) {

                /* Set auto config mode */
                rom_device_flags |= ROM_CONFIG_AUTO;

                /* Perform auto-config */
                (*module_list[selected_type]->auto_config)();
            } else
                return sim_messagef (SCPE_ARG, "Auto configuration is not available for the %s module\n",
                module_list[selected_type]->name);
        } else
            return sim_messagef (SCPE_ARG, "Specify AUTO or MANUAL configuration mode\n\n");

        return SCPE_OK;
}

t_stat rom_show_configmode (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (f, "configuration mode %s",
        (rom_device_flags & ROM_CONFIG_AUTO) ? "AUTO" : "MANUAL");
    return SCPE_OK;
}


/* Set ROM bootstrap start address */

t_stat rom_set_start_address(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    /* Forward the command to the module-specific function */
    return module_list[selected_type]->set_start_address (uptr, value, cptr, desc);
}

t_stat blank_set_start_address (UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    t_stat r;
    int32 address = 0;

    /* Check a value is given */
    if (cptr == NULL)
        return sim_messagef(SCPE_ARG, "START_ADDRESS requires a value\n");

    /* Get a numerical start address */
    if ((r = get_numerical_start_address(cptr, &address)) != SCPE_OK)
        return r;

    /* Save the start address */
    rom_start_address = address;
    return SCPE_OK;
}


/*
 * Set M9312 bootstrap start address 
 * 
 * The M9312 Technical Manual states:
 * With an M93I2 Bootstrap/Terminator Module in the PDP-11 computer system,
 * on power-up the user can optionally force the processor to read its new PC
 * from a ROM memory location and the Offset Switch Bank on the M9312 (Unibus
 * location 773024(8)). A switch (S1-2) on the M9312 or an external switch on 
 * Faston tabs TP3 and TP4 can enable or disable this feature. The new PSW will
 * be read from a location (Unibus location 773026(8)) in the M93I2 memory.
 * This new PC and PSW will then direct the processor to a program (typically a
 * bootstrap) in the M9312 ROM memory (Unibus memory locations 773000(8) through
 * 773776(8), and 765000(8) through 765776(8)).
 * 
 * This translates to the following functionality of the ROM device:
 * - The address specified in the Offset Switch Bank can be set via the 
 *   "SET ROM START_ADDRESS=<address>" command. For ease of use the address must be
 *   a 16-bit physical address in the ROM address space instead of an offset.
 * 
 * - The start address is made available in the location 077732024 (or 07773224
 *   for an 11/60).
 */
t_stat m9312_set_start_address (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 address = 0;

    /* Check a value is given */
    if (cptr == NULL)
        return sim_messagef (SCPE_ARG, "START_ADDRESS requires a value\n");

    /* Check whether a numerical or symbolic address is given */
    if (digits_only (cptr)) {

        /* Get a numerical start address */
        if ((r = get_numerical_start_address (cptr, &address)) != SCPE_OK)
            return r;
    }
    else {
        /* Get a symbolic start address */
        if ((r = get_symbolic_start_address (cptr, &address)) != SCPE_OK)
            return r;
    }

    /* Save the start address */
    rom_start_address = address;
    return SCPE_OK;
}


/* Get a numerical address */

t_stat get_numerical_start_address (const char *cptr, int32 *start_address)
{
    t_stat r;
    int32 address;

    /*
     * Convert the adress glyph and check if it produced a valid value.
     * Note that the actual maximum value is VASIZE - 2, but accepting
     * addresses between VASIZE -2 and MAXMEMSIZE -2 at this point
     * produces more meaningful error messages.
     */
    address = (int32) get_uint (cptr, 8, MAXMEMSIZE - 2, &r);
    if (r != SCPE_OK)
        return r;

    /* Check if the address is in the address space of an attached ROM */
    if (!address_available (address))
        return sim_messagef (SCPE_ARG,
            "Specify a 16-bit physical address in the address space of an attached ROM\n");

    /* Check the address is even */
    if (address & 0x1)
        return sim_messagef (SCPE_ARG, "Specify an even address\n");

    /* Return address */
    *start_address = address;
    return SCPE_OK;
}

/* Get a symbolic start address in the form <ROM>+DIAG|<ROM>-DIAG */

t_stat get_symbolic_start_address (const char* cptr, int32* start_address)
{
    char rom_name[4];
    char plus_minus;
    int num_chars = 0;
    ROM_DEF* romptr;
    int socket_number;
    int32 offset;

   /*
    * sscanf splits the symbolic address in three parts:
    * 1. The ROM name, ended by a plus or minus,
    * 2. The plus or minus,
    * 3. The string "DIAG".
    */
    if ((sscanf (cptr, "%3[^+-]%cDIAG%n", rom_name, &plus_minus, &num_chars) != 2) ||
        (num_chars == 0) || (cptr[num_chars] != '\0') ||
        ((plus_minus != '+') && (plus_minus != '-')))
            return sim_messagef (SCPE_ARG, "Specify start address as <ROM>+DIAG|<ROM>-DIAG\n");

    /* Try to find the ROM in the list of boot and console/diag ROMs */
    if (((romptr = find_rom (rom_name, (ROM_DEF(*)[]) &boot_roms)) == NULL) &&
        ((romptr = find_rom (rom_name, (ROM_DEF(*)[]) &diag_roms)) == NULL))
            return sim_messagef (SCPE_ARG, "Unknown ROM type %s\n", rom_name);

    /* Find (if any) the socket the ROM is placed in */
    for (socket_number = 0; socket_number < M9312_NUM_SOCKETS; socket_number++) {
        if (strcmp (socket_config[socket_number].rom_name, rom_name) == 0) {

            /* ROM found, get address offset */
            offset = (plus_minus == '+') ? romptr->boot_with_diags : romptr->boot_no_diags;

            /* Check the specified start address is available for the ROM */
            if (offset == NO_START_ADDRESS)
                return sim_messagef(SCPE_ARG, "Start address not available for %s\n", rom_name);

            /* Set start address to the ROM in this socket */
            *start_address = socket_config[socket_number].base_address + offset;

            /* Transform to a 16-bit physical address */
            *start_address &= VAMASK;
            return SCPE_OK;
        }
    }

    /* No socket with a ROM for the specified start address found */
    return sim_messagef (SCPE_ARG, "ROM %s is not attached to a socket\n", rom_name);
}


/* Show ROM bootstrap start address */

t_stat rom_show_start_address (FILE* f, UNIT* uptr, int32 val, CONST void* desc)
{
    /* Forward the command to the module-specific function */
    return module_list[selected_type]->show_start_address (f);
}


/* Show start address for BLANK and VT40 modules */

t_stat blank_show_start_address (FILE* f)
{
    fprintf(f, "start address ");
    (rom_start_address == 0) ? fprintf(f, "not specified") :
        fprintf(f, "%o", rom_start_address);

    /* Search socket */
    return SCPE_OK;
}


/* Show start address for M9312 module */

t_stat m9312_show_start_address (FILE *f)
{
    uint32 base_address;
    uint32 offset;
    uint32 socket_number;
    char *rom_name;
    ROM_DEF* romptr;

    fprintf (f, "start address ");
    (rom_start_address == 0) ? fprintf (f, "not specified") : 
        fprintf (f, "%o", rom_start_address);

    base_address = rom_start_address & 0177000;
    offset = rom_start_address & 0777;

    /* For all M9312 sockets */
    for (socket_number = 0; socket_number < M9312_NUM_SOCKETS; socket_number++) {

        /* Is this the socket with the base address? */
        if ((m9312_sockets[socket_number].base_address & VAMASK) == base_address) {

            rom_name = socket_config[socket_number].rom_name;

            /* Find the ROM in this socket */
            if (((romptr = find_rom(rom_name, (ROM_DEF(*)[]) & boot_roms)) == NULL) &&
                ((romptr = find_rom(rom_name, (ROM_DEF(*)[]) & diag_roms)) == NULL))
                return sim_messagef(SCPE_IERR, "ROM %s in socket %d not found\n", 
                    rom_name, socket_number);

            /* 
             * Check if the offset corresponds with one of the start addresses
             * of for the ROM.
             */
            if (offset == romptr->boot_no_diags)
                fprintf (f, " (%s-DIAG)", rom_name);
            else if (offset == romptr->boot_with_diags)
                fprintf(f, " (%s+DIAG)", rom_name);
            break;
        }
    }

    /* Search socket */
    return SCPE_OK;
}


/* Set ROM write enabled */

t_stat rom_set_write_enable (UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    if (MATCH_CMD (cptr, "ENABLED") == 0)
        uptr->flags &= ~UNIT_RO;
    else if (MATCH_CMD (cptr, "DISABLED") == 0)
        uptr->flags |= UNIT_RO;
    else
        return sim_messagef(SCPE_ARG, "Specify WRITE=ENABLED or WRITE=DISABLED\n");

    return SCPE_OK;
}


/* Check that a string contains digits only */

t_bool digits_only (const char* cptr)
{
    while (*cptr) {
        if (isdigit (*cptr++) == 0) 
            return FALSE;
    }

    return TRUE;
}

/*
 * Verify that the given address is within the address space of an attached ROM.
 * The address must be a 16-bit address that, with 16-bit mapping enabled, maps
 * to an address in the I/O page.
 */
static t_bool address_available (int32 address)
{
    int socket_number;
    int32 num_sockets = module_list[selected_type]->num_sockets;

    address |= IOPAGEBASE;

    /* For all sockets */
    for (socket_number = 0; socket_number < num_sockets; socket_number++) {
        
        if ((address >= socket_config[socket_number].base_address) &&
            (address < socket_config[socket_number].base_address + 
                socket_config[socket_number].rom_size))

            /* Availabe address found */
            return TRUE;
    }

    /* No available address found */
    return FALSE;
}


/*
 * ROM read routine. This routine forwards the read request
 * to the ROM-specific read function.
 */
t_stat rom_rd (int32* data, int32 PA, int32 access)
{
    return module_list[selected_type]->read (data, PA, access);
}

/* ROM read routine */
 
t_stat blank_rom_rd (int32 *data, int32 PA, int32 access)
{
    uint16* rom_location;

    /* Get pointer to ROM image and check an image is available */
    rom_location = get_rom_loc_ptr (PA);
    if (rom_location == NULL)
        return SCPE_NXM;

    /* Read data from the ROM */
    *data = *rom_location;
    return SCPE_OK;
}


/* M9312 specific ROM read routine */

t_stat m9312_rd (int32* data, int32 PA, int32 access)
{
    int32 pwrup_boot_socket = (cpu_model == MOD_1160) ? SOCKET2 : SOCKET1;
    int32 trap_location = 
        m9312_sockets[pwrup_boot_socket].base_address + TRAP_LOCATION;

    if ((PA == trap_location) && 
        (socket_config[pwrup_boot_socket].rom_image != NULL) &&
        (rom_start_address != 0)) {
            *data = rom_start_address;
            return SCPE_OK;
    }
    else
        return blank_rom_rd (data, PA, access);
}


/*
 * ROM write routine
 */
t_stat rom_wr (int32 data, int32 PA, int32 access)
{
    uint16* rom_location;

    /* Check the ROM's are writable */
    if (rom_unit.flags & UNIT_RO)
        return SCPE_NXM;

    /* Get pointer to ROM image and check an image is available */
    rom_location = get_rom_loc_ptr (PA);
    if (rom_location == NULL)
        return SCPE_NXM;

    /* Write data to the ROM */
    *rom_location = data;
    return SCPE_OK;
}


/* Get a pointer to the location in the ROM image for the physical address */

uint16 *get_rom_loc_ptr (int32 phys_addr)
{
    uint32 socket_number;
    uint32 num_sockets = module_list[selected_type]->num_sockets;
    uint16* image;

    /* For all sockets in socket_info */
    for (socket_number = 0; socket_number < num_sockets; socket_number++) {

        /* Check if the address is in the range of this socket */
        if (phys_addr >= socket_config[socket_number].base_address &&
            phys_addr <= socket_config[socket_number].base_address +
            socket_config[socket_number].rom_size - 1 &&
            (socket_config[socket_number].rom_image != NULL)) {

            /* Get pointer to ROM image and return address in that image*/
            image = (uint16*)socket_config[socket_number].rom_image;
            // image[(PA - socket_config[socket_number].base_address)] = data;
            return (uint16*) socket_config[socket_number].rom_image + 
                ((phys_addr - socket_config[socket_number].base_address) >> 1);
        }
    }

    /* Image not found */
    return NULL;
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

    /* Create the linked list of DIBs */
    for (socket_number = 0; socket_number < ROM_MAX_SOCKETS; socket_number++) {
        rom_dib[socket_number].next = 
            (socket_number < ROM_MAX_SOCKETS - 1) ? &rom_dib[socket_number + 1] : NULL;
    }
    return SCPE_OK;
}


/* Boot routine */

t_stat rom_boot (int32 u, DEVICE *dptr)
{
    uint32 socket_number;
    void (*rom_init)();

    /* Check that a valid start address is set */
    if (rom_start_address == 0)
        return SCPE_NOFNC;

    /* Initialize ROMs */
    for (socket_number = 0; socket_number < ROM_MAX_SOCKETS; socket_number++) {
        if ((rom_init = socket_config[socket_number].rom_init) != NULL) 
            (*rom_init)();
    }

    /* Set the boot address */
    // ToDo: Use PSW from boot rom?!
    cpu_set_boot (rom_start_address);
    return SCPE_OK;
}


/* (Re)set the DIB and build the Unibus table for the specified socket */

static t_stat reset_dib (int socket_number, t_stat (reader (int32 *, int32, int32)),
    t_stat (writer (int32, int32, int32)))
{
    DIB *dib = &rom_dib[socket_number];

    dib->ba = socket_config[socket_number].base_address;
    dib->lnt = socket_config[socket_number].rom_size;
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

 
/* Attach either file or a built-in ROM image to a socket */
 
t_stat rom_attach (UNIT *uptr, CONST char *cptr)
{
    char cmdstr[CBUFSIZE];

    /* Remove any angled brackets from command sequence  */
    strclean(cmdstr, cptr);

    /* Execute attach cmd for the selected module type */
    return (*module_list[selected_type]->attach)(cmdstr);
}


/* Copy the source to destination string, removing any angled brackets */

static void strclean (char *dest, CONST char *src)
{
    while (*src != 0) {

        /* Check if this a character to be removed */
        if ((*src != '<') && (*src != '>'))
            *dest++ = *src;
        src++;
    }
    *dest = 0;
}

/*
 * Processing the ATTACH command is performed in three steps:
 * 1. Parsing the command parameters,
 * 2. Validating the parameters,
 * 3. Executing the command.
 *
 * A separate validation step makes sure the commands in the
 * sequence can be executed without leaving side effects while
 * the sequence contains a mix of valid and invalid commands.
 */
static t_stat blank_attach (CONST char *cptr)
{
    uint32 num_commands;
    uint32 i;
    t_stat r = SCPE_OK;
    CONST t_bool address_required = TRUE;
    ATTACH_CMD attach_cmd[ROM_MAX_SOCKETS] =
        {{0, 0, ""}, {0, 0, ""}, {0, 0, ""}, {0, 0, ""}, {0, 0, ""}};

    /* Parse the command sequence */
    if ((r = parse_attach_cmd_seq (cptr, address_required, &attach_cmd[0], &num_commands)) != SCPE_OK)
        return r;

    /* Validate the command sequence */
    if ((r = validate_attach_blank_rom (&attach_cmd[0], num_commands)) != SCPE_OK)
        return r;

#if 0
    /* Print the attachments to be executed */
    // ToDo: Print debug output?
    for (i = 0; i < num_commands; i++) {
        sim_messagef (SCPE_OK, "ATTACH ROM socket %d: address %o, image %s\n",
            attach_cmd[i].socket_number, attach_cmd[i].address, attach_cmd[i].image_name);
    }
#endif

    /* Execute the commands in the command sequence */
    for (i = 0; i < num_commands; i++) {
        if ((r = exec_attach_blank_rom (&attach_cmd[i])) != SCPE_OK)
            return r;
    }

    return SCPE_OK;
}

static t_stat embedded_attach (CONST char *cptr)
{
    uint32 num_commands;
    uint32 i;
    t_stat r = SCPE_OK;
    CONST t_bool address_required = FALSE;
    ATTACH_CMD attach_cmd[ROM_MAX_SOCKETS] =
        {{0, 0, ""}, {0, 0, ""}, {0, 0, ""}, {0, 0, ""}, {0, 0, ""}};
    
    /* Parse the command sequence */
    if ((r = parse_attach_cmd_seq (cptr, address_required, 
            &attach_cmd[0], &num_commands)) != SCPE_OK)
        return r;

    /* Validate the command sequence */
    if ((r = validate_attach_embedded_rom (&attach_cmd[0], num_commands)) != SCPE_OK)
        return r;

    /* Execute the commands in the command sequence */
    for (i = 0; i < num_commands; i++) {
        if ((r = exec_attach_embedded_rom (&attach_cmd[i])) != SCPE_OK)
            return r;
    }

    return SCPE_OK;
}


/*
 * Parse an ATTACH command sequence
 *
 * Syntax:
 *  A command sequence is a list of one or more commands, separated by a comma.
 *  The maximum number of commands is the number of sockets present on the module. 
 *
 * Syntax of an ATTACH command: {socket:}{address/}image
 *  Specification of an address is only allowed for the BLANK module type.
 * 
 * The default socket number for the first command in the sequence is 0, for the
 * following commands in the sequence the default is the socket number of the
 * previous command in the sequence. So, the command sequence "xx/yy" attaches
 * ROM xx on socket 0 and ROM yy on socket 1. The command sequence "2:xx,yy,zz"
 * attaches ROM xx on socket 2, ROM YY on socket 3 and ROM zz on socket 4. 
 *
 * In:  cptr             - The command to be parsed
 * In:  address_required - Whether or not the commands must specify an address
 * Out: cmdptr           - The parsed commands
 * Out: num_commands     - The number of commands in the sequence
 *
 * Result:
 *      SCPE_OK          - The command sequence was succesfully parsed
 *      SCPE_ARG         - No meaningful command sequence could be parsed
 */
static t_stat parse_attach_cmd_seq (CONST char *cptr, t_bool address_required,
    ATTACH_CMD *cmdptr, uint32 *num_commands)
{
    char command[CBUFSIZE];
    uint32 default_socket_number = 0;
    t_stat r = SCPE_OK;

    /* While there is some string to parse */
    *num_commands = 0;
    while (*cptr != 0) {

        /* A maximum of num_sockets ROM's can be specified */
        if (*num_commands >= module_list[selected_type]->num_sockets)
            return sim_messagef (SCPE_ARG, "Specify a maximum of %d ROM's",
                module_list[selected_type]->num_sockets);

        /* Get next command in the command sequence */
        cptr = get_glyph_nc (cptr, command, ',');

        /* And parse that command */
        if ((r = parse_attach_cmd (command, address_required,
                &cmdptr[*num_commands], &default_socket_number)) != SCPE_OK)
            return r;

        (*num_commands)++;
    }
    return SCPE_OK;
}


/* Parse one command from a command sequnce */

static t_stat parse_attach_cmd (CONST char *cptr, t_bool address_required,
    ATTACH_CMD *cmdptr, uint32 *default_socket_number)
{
    char value[CBUFSIZE];
    char *ptr;
    t_stat r;
    uint32 socket_number;
    int32 address;

    /* See if a socket number is specified */
    if (strchr (cptr, ':')) {

        /* Get socket number */
        cptr = get_glyph_nc (cptr, value, ':');
        socket_number = strtol (value, &ptr, 0);

        /* Check a valid number was specified */
        if (*ptr != 0)
            return sim_messagef (SCPE_ARG, "Specify a valid socket number\n");

        /* Check valid socket number is specified */
        if (socket_number >= module_list[selected_type]->num_sockets) 
            return sim_messagef (SCPE_ARG, "Socket must be in range 0 to %d\n",
            module_list[selected_type]->num_sockets - 1);

        /* Save socket number and set default socket number for next command */
        cmdptr->socket_number = socket_number;
        (*default_socket_number) = socket_number + 1;
    } 
    else {
        /* No socket specified use default */
        /* Check default socket number is valid */
        if (*default_socket_number >= module_list[selected_type]->num_sockets)
            return sim_messagef (SCPE_ARG, "Socket must be in range 0 to %d\n",
                module_list[selected_type]->num_sockets - 1);

        cmdptr->socket_number = *default_socket_number;
        (*default_socket_number)++;
    }

    if (address_required) {

        /* See if an address and/or image is specified */
        if (*cptr == 0)
            return sim_messagef (SCPE_ARG, "Specify address and image\n");

        /* Get adress */
        cptr = get_glyph_nc (cptr, value, '/');

        /* Convert the adress glyph and check if produced a valid value */
        address = (int32) get_uint (value, 8, IOPAGEBASE + IOPAGEMASK, &r);
        if (r != SCPE_OK)
            return sim_messagef (r, "Specify a valid address\n");

        /* Check if a valid adress is specified */
        if (address < IOPAGEBASE)
            return sim_messagef (SCPE_ARG, "Address must be in I/O page, at or above 0%o\n",
            IOPAGEBASE);

        cmdptr->address = address;
    }
    else {
        /* Check that no address is specified */
        if (strchr (cptr, '/'))
            return sim_messagef (SCPE_ARG, 
                "Specification of address not allowed for this module type\n");
    }

    /* The remaining part of the command string should specify the image name */
    if (*cptr == 0)
        return sim_messagef (SCPE_ARG, "Specify a ROM image\n");

    strcpy (cmdptr->image_name, cptr);
    return SCPE_OK;
}


/* Validate all attach commands */

static t_stat validate_attach_blank_rom (ATTACH_CMD *cmdptr, uint32 num_commands)
{
    uint32 i, j;
    FILE *fileptr;

    /* For all commands in the command sequence */
    for (i = 0; i < num_commands; i++) {
        for (j = 0; j < num_commands; j++) {

            /* Check that socket numbers are specified just once */
            if ((cmdptr[j].socket_number == cmdptr[i].socket_number) && (j != i))
                return sim_messagef (SCPE_ARG, "Socket %d used more than once\n",
                    cmdptr[i].socket_number);

            /* Check that addresses are specified just once */
            if ((cmdptr[j].address == cmdptr[i].address) && (j != i))
                return sim_messagef (SCPE_ARG, "Address %o used more than once\n",
                    cmdptr[i].address);

            /* Check that a valid file name has been specified */
            fileptr = sim_fopen (cmdptr[i].image_name, "rb");
            if (fileptr == NULL)
                return sim_messagef (SCPE_ARG, "File %s cannot be opened\n",
                    cmdptr->image_name);
            fclose (fileptr);
        }
    }

    return SCPE_OK;
}

static t_stat validate_attach_embedded_rom (ATTACH_CMD *cmdptr, uint32 num_commands)
{
    uint32 i, j;
    MODULE_DEF *modptr;
    SOCKET_DEF *socketptr;
    ROM_DEF *romptr;

    /* For all commands in the command sequence */
    for (i = 0; i < num_commands; i++) {
        for (j = 0; j < num_commands; j++) {

            /* Check that socket numbers are specified just once */
            if ((cmdptr[j].socket_number == cmdptr[i].socket_number) && (j != i))
                return sim_messagef (SCPE_ARG, "Socket %d used more than once\n",
                cmdptr[i].socket_number);

            /* Check that a valid image name has been specified */
            modptr = *(module_list + selected_type);
            socketptr = *modptr->sockets + cmdptr[i].socket_number;

            /* Try to find the ROM in the list of ROMs for this socket */
            if ((romptr = find_rom (cmdptr[i].image_name, socketptr->rom_list)) == NULL)
                return sim_messagef (SCPE_ARG, "Unknown ROM type %s\n", cmdptr[i].image_name);
        }
    }
    return SCPE_OK;
}

/* Execute an attach command for the BLANK module */

static t_stat exec_attach_blank_rom (ATTACH_CMD *cmdptr)
{
    t_stat r = SCPE_OK;
    FILE *fileptr;
    void *rom_image = NULL;
    uint32 rom_size;
    uint32 socket_number = cmdptr->socket_number;

    /* Set base address */
    socket_config[socket_number].base_address = cmdptr->address;

    /* (Try to) open the image file */
    fileptr = sim_fopen (cmdptr->image_name, "rb");
    if (fileptr == NULL)
        return sim_messagef (SCPE_IERR, "File %s cannot be opened\n",
            cmdptr->image_name);

    /* Get the file size and allocate a buffer with the appropriate size */
    rom_size = (uint32) sim_fsize_ex (fileptr);
    rom_image = calloc (rom_size, sizeof (char));

    /* Read ROM contents into image */
    rom_size = (uint32) sim_fread (rom_image, sizeof (char), rom_size, fileptr);

    /* The file can now be closed */
    fclose (fileptr);

    /* Attach the created ROM to the socket */
    r = attach_rom_to_socket (cmdptr->image_name,
        socket_config[socket_number].base_address, rom_image, rom_size,
        NULL, socket_number);

    /* Free allocated resources */
    if (rom_image != NULL)
        free (rom_image);

    return r;
}


/* Execute the attach command for an embedded module */

static t_stat exec_attach_embedded_rom (ATTACH_CMD *param_values)
{
    t_stat r;
    MODULE_DEF *modptr;
    SOCKET_DEF *socketptr;
    ROM_DEF *romptr;
    uint32 socket_number = param_values->socket_number;

   /*
    * Get a pointer to the selected module and from that a pointer to
    * socket for the module.
    */
    modptr = *(module_list + selected_type);
    socketptr = *modptr->sockets + socket_number;

    /* Try to find the ROM in the list of ROMs for this socket */
    if ((romptr = find_rom (param_values->image_name, socketptr->rom_list)) == NULL)
        return sim_messagef (SCPE_ARG, "Unknown ROM type\n");

    /* Attach the ROM to the socket */
    if ((r = attach_rom_to_socket (romptr->device_mnemonic, socketptr->base_address,
        romptr->image, socketptr->size, romptr->rom_init, socket_number)) != SCPE_OK) {
        return r;
    }

    /* (Re)set the configuration mode to manual */
    rom_device_flags &= ~ROM_CONFIG_AUTO;

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
static ROM_DEF *find_rom (const char *cptr, ROM_DEF (*rom_listptr)[])
{
    ROM_DEF *romptr;

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
static t_stat attach_rom_to_socket (char* name, t_addr address,
    void *image, int16 size, void (*rom_init)(), int socket_number)
{
    /* Set the ROM size and pointer to the ROM image */
    socket_config[socket_number].base_address = address;
    socket_config[socket_number].rom_size = size;

    /* Deallocate previously allocated buffer */
    if (socket_config[socket_number].rom_image != NULL)
        free (socket_config[socket_number].rom_image);

    /*
     * Allocate a buffer for the ROM image and copy the image into the
     * buffer, allowing to overwrite the ROM image.
     */
    if ((socket_config[socket_number].rom_image =
        malloc (socket_config[socket_number].rom_size)) == 0)
        return sim_messagef(SCPE_IERR, "malloc() failed\n");

    memcpy (socket_config[socket_number].rom_image, image,
        socket_config[socket_number].rom_size);

    /* Fill the DIB for the socket */
    if (reset_dib (socket_number, &rom_rd, &rom_wr) != SCPE_OK) {

        // Remove ROM image
        free (socket_config[socket_number].rom_image);
        socket_config[socket_number].rom_image = NULL;
        return sim_messagef (SCPE_IERR, "reset_dib() failed\n");
    }

    /* Save the specified ROM image name in the register */
    strncpy (socket_config[socket_number].rom_name, name, CBUFSIZE);
    
    /* Execute ROM init function if available and save pointer for re-use */
    if (rom_init != NULL) {
        (*rom_init)();
        socket_config[socket_number].rom_init = rom_init;
    }

    /* Create attachment information so attach is called during a restore */
    rom_unit.flags |= UNIT_ATT;
    rom_unit.dynflags |= UNIT_ATTMULT;

    /*
     * Fill the unit file name with the ATTACH command sequence that
     * will reproduce the current state of ROM attachments.
     */
    (*module_list[selected_type]->create_filename)(unit_filename);
    rom_unit.filename = unit_filename;

    return SCPE_OK;
}

/* Auto-attach ROM for VT40 */

static t_stat vt40_auto_attach ()
{
    SOCKET_DEF *socketptr;
    ROM_DEF *romptr;
    uint32 socket_number = 0;

    /* A VT40 has just one socket */
    socketptr = &vt40_sockets[socket_number];
    romptr = *socketptr->rom_list;

    /* Attach the ROM to the socket */
    return attach_rom_to_socket (romptr->device_mnemonic, socketptr->base_address,
        romptr->image, socketptr->size, romptr->rom_init, socket_number);
}

/* 
 * Create a meaningful filename for the UNIT structure.
 *
 * This filename is displayed in a SHOW ROM command and is used in
 * rom_attach() as a command sequence to reattached the ROMS during a
 * RESTORE operation.
 * As the commands to attach the ROMS differ per module type, the file name
 * to be created also differs per module type. For the BLANK module type
 * the socket address has to be included in the commands, while for
 * embedded module types specification of the socket address isn't
 * allowed.
 */
static void create_filename_blank (char *filename)
{
    uint32 socket_number;
    t_bool first_socket = TRUE;

    filename += sprintf (filename, "<");

    for (socket_number = 0; socket_number < module_list[selected_type]->num_sockets;
        socket_number++) {

        /* Only use filled sockets */
        if (*socket_config[socket_number].rom_name != 0) {
            filename += sprintf(filename, "%s%d:%o/%s",
                first_socket ? "" : ", ", socket_number,
                socket_config[socket_number].base_address, socket_config[socket_number].rom_name);
            first_socket = FALSE;
        }
    }

    sprintf (filename, ">");
    return;
}

static void create_filename_embedded (char* filename)
{
    uint32 socket_number;
    t_bool first_socket = TRUE;

    filename += sprintf(filename, "<");

    for (socket_number = 0; socket_number < module_list[selected_type]->num_sockets;
        socket_number++) {

        /* Only use filled sockets */
        if (*socket_config[socket_number].rom_name != 0) {
            filename += sprintf(filename, "%s%d:%s",
                first_socket ? "" : ", ", socket_number,
                socket_config[socket_number].rom_name);
            first_socket = FALSE;
        }
    }

    sprintf(filename, ">");
    return;
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

static t_stat detach_all_sockets ()
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

        /* Mark no units attached */
        rom_unit.flags &= ~UNIT_ATT;

        /* Reset start address */
        rom_start_address = 0;
    }
    return SCPE_OK;
}

/* Detach the image from the specified socket */

static t_stat detach_socket (uint32 socket_number)
{
    /* Clear socket information */
    socket_config[socket_number].base_address = 0;
    socket_config[socket_number].rom_size = 0;
    socket_config[socket_number].rom_init = NULL;
    *socket_config[socket_number].rom_name = 0;

    if (socket_config[socket_number].rom_image != NULL) {
        free (socket_config[socket_number].rom_image);
        socket_config[socket_number].rom_image = NULL;
    }
    
    /* Clear Unibus map */
    return reset_dib (socket_number, NULL, NULL);
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
            return m9312_attach (mptr->rom_name, 0);
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

   /*
    * Detach all sockets with boot ROMs (socket_number.e. 1-4) to avoid the
    * possibility that on a subsequent m9312 auto configuration with an altered
    * device configuration a same ROM is present twice.
    */
    for (socket_number = 1; socket_number < M9312_NUM_SOCKETS; socket_number++) {
        detach_socket (socket_number);
    }

    /* For all devices */
    for (dev_index = 0, socket_number = 1; 
        ((dptr = sim_devices[dev_index]) != NULL) && (socket_number < M9312_NUM_SOCKETS); dev_index++) {

        /* Is the device enabled? */
        if (!dev_disabled (dptr)) {

            /* Search device_rom map for a suitable boot ROM for the device */
            for (mptr = &device_rom_map[0]; mptr->device_name != NULL; mptr++) {
                if (strcasecmp (dptr->name, mptr->device_name) == 0) {
                    if ((result = m9312_attach (mptr->rom_name, socket_number)) != SCPE_OK)
                        return result;
                    socket_number++;
                    break;
                }
            }
        }
    }
    return SCPE_OK;
}

static t_stat m9312_attach (const char *rom_name, int socket_number)
{
    SOCKET_DEF *socketptr;
    ROM_DEF *romptr;

    socketptr = &m9312_sockets[socket_number];

    /* Try to find the ROM in the list of ROMs for this socket */
    romptr = find_rom (rom_name, socketptr->rom_list);
    if (romptr != NULL) {
        return attach_rom_to_socket (romptr->device_mnemonic, socketptr->base_address,
            romptr->image, socketptr->size, romptr->rom_init, socket_number);
    } else
        /* ROM not found */
        return SCPE_IERR;
}


/* Print help */

static t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    const char rom_helptext[] =
        /***************** 80 character line width template *************************/
        " A hardware PDP-11 comprises a module with Read Only Memory (ROM) code,\n"
        " containing console emulator, diagnostic and bootstrap functionality.\n"
        " Each module has one or more ROMs available, the contents of which can be\n"
        " accessed by a reserved address space in the I/O page.\n\n"
        " Currently the following module types are supported:\n\n"
        "+1. BLANK\tProvides the contents of a file as a ROM\n"
        "+2. M9312\tEmulator, diagnostics and boot functionality for Unibus models\n"
        "+3. VT40\tThis is the VT40 ROM for a GT40 graphical terminal\n\n"
        " The module type can be selected by means of the SET ROM TYPE command:\n\n"
        "+SET ROM TYPE=BLANK|M9312|VT40\n\n"
 //       " All ROM modules comprise one or more sockets. In each socket one ROM can be\n"
 //      " placed and each socket is associated with a base address at which the\n"
 //       " the contents of the ROMs can be read. The contents of the M9312 and VT40\n"
 //       " modules are embedded in simh, the BLANK module ROM contents have to be\n"
 //       " provided in a file per ROM.\n\n"
        " All module types support the ATTACH and SHOW command and a number of SET\n"
        " commands. The commands common to all module types are described in the topic\n"
        " 'Common_commands'. The module-specific commands are described in the\n"
        " topic 'Commands' per module type.\n"
        /***************** 80 character line width template *************************/
        "1 Blank\n"
        " The BLANK ROM module is a module with four sockets in each of which one ROM\n"
        " can be placed, the contents of which are coming from a file.\n"
        "2 Configuration\n"
        " The BLANK module can be configured by means of the ATTACH command. For a\n"
        " succesful ATTACH the following parameters have to be specified:\n\n"
        "+1. The socket number to attach a ROM to,\n"
        "+2. The address in the I/O space in which the ROM contents will be available,\n"
        "+3. The file with the ROM contents in RAW format.\n\n"
        " A single attach specification has the following format:\n\n"
        "+ATTACH ROM {<socket>:}<address>/<file>\n\n"
        " The socket is a number between 0 and 3. The socket number is optional and\n"
        " the default number is 0. The address is a physical address in the I/O SPACE,\n"
        " i.e.between addresses 17760100 and 17777777. The address must not collide\n"
        " with register addresses used by other devices. The file is the name of a\n"
        " file with the contents of the ROM in RAW format.\n\n"
        " An attach command can comprise more one to four attach specifications,\n"
        " seperated by a comma:\n\n"
        "+ATTACH ROM {<socket>:}<address>/<file>{,{<socket>:}<address>/<file>}+\n\n"
        " The default socket number for the first attach specification in the attach\n"
        " command is zero, for the following specifications it is the socket number of\n"
        " the previous specification plus one.\n\n"
        " The following commands attach two ROMs, at socket 0 and 1:\n\n"
        "+ATTACH ROM 0:17765000/23-616F1.IMG\n"
        "+ATTACH ROM 1:17773000/23-751A9.IMG\n\n"
        " These commands can also be combined in one ATTACH command:\n\n"
        "+ATTACH ROM 0:17765000/23-616F1.IMG, 17773000/23-751A9.IMG\n\n"
        " The effect of the last command is equal to the two separate attach commands.\n"
        "2 Boot\n"
        " There are two ways to boot from an attached ROM:\n\n"
        "+1. By starting the simulator at an address in the ROM address space,\n"
        "+2. By setting the starting address and subsequently issue a BOOT command.\n\n"
        " The simulator can be started at an address in the ROM address space via a GO\n"
        " or RUN command:\n\n"
        "+GO <address>\n"
        "+RUN <address>\n\n"
        " For more information see the help for these commands.\n\n"
        " The simulator can also be started by first setting the start address in the\n"
        " ROM code and subsequently issue a BOOT command. The start address is set by\n"
        " the following command:\n\n"
        "+SET ROM START_ADDRESS=<address>\n\n"
        " The address must be a 16-bit physical address in an attached ROM. The system\n"
        " can then be started via a BOOT command:\n\n"
        "+BOOT {ROM|CPU}\n"
        "2 Examples\n"
        " The following commands attach two ROMs to the sockets and use these ROMs to\n"
        " boot the system:\n\n"
        "+SET CPU 11/70\n"
        "+ATTACH ROM 0:17765000/23-616F1.IMG, 17773000/23-751A9.IMG\n"
        "+SET ROM START_ADDRESS=173006\n"
        "+BOOT ROM\n"
        /***************** 80 character line width template *************************/
        "1 M9312\n"
        " The M9312 module contains 512 words of read only memory (ROM) that can be\n"
        " used for diagnostic routines, the console emulator routine, and bootstrap\n"
        " programs. The module contains five sockets that allow the user to insert\n"
        " ROMs, enabling the module to be used with any Unibus PDP11 system and boot\n"
        " any peripheral device by simply adding or changing ROMs.\n\n"
        " The M9312 is configured by placing ROMs in the module's sockets with ATTACH\n"
        " commands and with various SET commands.\n"
        "2 Configuration\n"
        " The M9312 module has 512 words of read only memory. The lower 256 words\n"
        " (addresses 165000 through 165776) are used for the storage of ASCII console\n"
        " and diagnostic routines. The diagnostics are rudimentary CPU and memory\n"
        " diagnostics. The upper 256 words (addresses 173000 through 173776) are used\n"
        " for bootstrap programs. These upper words are divided further into four\n"
        " 64-word segments. In principle each of the segments 0 to 4 contains the boot\n"
        " programs for one or two device types. If necessary however, more than one\n"
        " segment may be used for a boot program. The following table shows the ROM\n"
        " segmentation.\n\n"
        "+ROM type                                           Base address\n"
        "+256 word console emulator and diagnostics ROM #0   165000\n"
        "+64 word boot ROM #1                                173000\n"
        "+64 word boot ROM #2                                173200\n"
        "+64 word boot ROM #3                                173400\n"
        "+64 word boot ROM #4                                173600\n\n"
        " For each segment a socket is available in which a ROM can be placed.\n"
        " Socket 0 is solely used for a diagnostic ROM (PDP-11/60 and 11/70 systems)\n"
        " or a ROM which contains the console emulator routine and diagnostics for all\n"
        " other PDP-11 systems. The other four sockets accept ROMs which contain\n"
        " bootstrap programs. In general one boot ROM contains the boot code for one\n"
        " device type. In some cases a boot rom contains the boot code for two device\n"
        " types and there are also cases where the boot code comprises two or three\n"
        " ROMs. In these cases these ROMs have to be placed in subsequent sockets.\n\n"
        " The system start adress can be determined in the following way:\n\n"
        "+1. Take the socket base address the ROM is placed in from the table above,\n"
        "+2. Add the ROM-specific offset of the start address. The offsets are\n"
        "+documented in the ROM-specific help text.\n\n"
        /***************** 80 character line width template *************************/
        " With a DL boot ROM in socket 1 e.g., a RL01 disk can be booted from unit 0,\n"
        " without performing the diagnostics, by starting at address 173004. With the\n"
        " same boot ROM placed in socket 2 that unit can be booted by starting at\n"
        " address 1732004. Note that the start address specifies whether or not the\n"
        " diagnostics code is executed.\n\n"
        "2 ATTACH_command\n"
        " The M9312 module can be configured by means of the ATTACH command. For a\n"
        " succesful ATTACH the following parameters have to be specified:\n\n"
        "+1. The socket number to attach a ROM to,\n"
        "+2. The name of the ROM image.\n\n"
        " A single attach specification has the following format:\n\n"
        "+ATTACH ROM {<socket>:}<ROM>\n\n"
        " The socket is a number between 0 and 4. The socket number is optional and\n"
        " the default number is 0. The ROM is the name of one of the available ROMs.\n"
        " An attach command can comprise more one to four attach specifications,\n"
        " seperated by a comma:\n\n"
        "+ATTACH ROM {<socket>:}<ROM>{,{<socket>:}<ROM>}+\n\n"
        " The default socket number for the first attach specification in the attach\n"
        " command is zero, for the following specifications it is the socket number of\n"
        " the previous specification plus one. The following commands attach two ROMs,\n"
        " at socket 0 and 1:\n\n"
        "+ATTACH ROM 0:B0\n"
        "+ATTACH ROM 1:DL\n\n"
        " These commands can also be combined in one ATTACH command:\n\n"
        "+ATTACH ROM B0,DL\n\n"
        " The effect of this command is equal to the two separate attach commands.\n"
        "2 SET_commands\n"
        " This module supports, apart from the common commands, the following type-\n"
        " specific commands:\n\n"
        "+SET ROM CONFIGURATION\n"
        "+SET ROM START_ADDRESS\n"
        "3 CONFIGURATION\n"
        " The M9312 implementation support an auto-configuration option which can be\n"
        " enabled with the CONFIGURATION parameter:\n\n"
        "+SET CONFIGURATION=AUTO|MANUAL\n\n"
        " In the auto-configuration mode the sockets are filled with ROMs suited for\n"
        " the current configuration. For socket 0 the ROM selected depends on the CPU\n"
        " type, for sockets 1 to 4 the ROM lineup is determined by the available and\n"
        " enabled devices. Usually the configuration will contain more usable devices\n"
        " than the four sockets that are available for boot ROMs for these devices. In\n"
        " that case boot ROMs are selected for the first four devices detected during\n"
        " the device scan.\n\n"
        " The auto-configured lineup of ROMs can be overriden by ATTACH commands for\n"
        " the sockets. In that case the configuration mode is reset to MANUAL.\n"
        "3 START_ADDRESS\n"
        " The system can be booted by starting it at one of the suitable boot\n"
        " addresses in one of the ROMs. The boot address can be set by means of the\n"
        " SET START_ADDRESS command:\n\n"
        "+SET ROM START_ADDRESS=<address>\n\n"
        " The address must be a 16-bit physical address in an attached ROM. The\n"
        " address to be used is determined by a combination of the socket base address\n"
        " and offset in the ROM. See 'HELP ROM M9312 Configuration' for an\n"
        " explanation.\n\n"
        " To make setting the start address more user-friendly, the address can also\n"
        " be specified symbolically by naming the ROM and the start address:\n\n"
        "+SET ROM START_ADDRESS=<ROM>+DIAG|<ROM>-DIAG\n\n"
        " '<ROM>' must be the name of a ROM currently attached to a socket. Boot ROMs\n"
        " useably provide two start address for respectively starting without and with\n"
        " performing diagnostics before the device is booted. '+DIAG' and '-DIAG'\n"
        " refer to these start addresses.\n\n"
        " After setting the start address the system can be started via a BOOT command:\n\n"
        "+BOOT {ROM|CPU}\n"
        "2 Available_ROMs\n"
        " The following ROMs are available.\n\n"
        "+ROM code       Function\n"
        "+A0             11/04, 11/34 Diagnostic/Console (M9312 E20)\n"
        "+B0             11/60, 11/70 Diagnostic (M9312 E20)\n"
        "+C0             11/44 Diagnostic/Console (UBI; M7098 E58)\n"
        "+D0             11/24 Diagnostic/Console (MEM; M7134 E74)\n"
        "+DL             RL01/RL02 cartridge disk\n"
        "+DM             RK06/RK07 cartridge disk\n"
        "+DX             RX01 floppy disk, single density\n"
        "+DP             RP02/RP03 cartridge disk\n"
        "+DB             RP04/RP05/RP06, RM02/RM03/RM05 cartridge disk\n"
        "+DK             RK03/RK05 DECdisk\n"
        "+DT             TU55/TU56 DECtape\n"
        "+MM             TU16/TU45/TU77, TE16 magtape\n"
        "+MT             TS03, TU10, TE10 magtape\n"
        "+DS             RS03/RS04 fixed disk\n"
        "+TT             ASR33 lowspeed reader\n"
        "+PR             PC05 hispeed reader\n"
        "+CT             TU60 DECcassette\n"
        "+MS             TS04/TS11, TU80, TSU05 tape\n"
        "+DD             TU58 DECtapeII\n"
        "+DU             MSCP UDA50 (RAxx) disk\n"
        "+DY             RX02 floppy disk, double density\n"
        "+MU             TMSCP TK50, TU81 magtape\n"
        "+XE0, XE1       Ethernet DEUNA / DELUA Net Boot (v2)\n"
        "+XM0, XM1, XM2  DECnet DDCMP DMC11 / DMR11\n"
        "+ZZ             Test ROM\n\n"
        "3 A0\n"
        " Function:              11/04, 11/34 Diagnostic/Console (M9312 E20)\n"
        " DEC Part number:       23-248F1\n"
        " Place in socket:       0\n\n"
        " Offsets\n"
        "+020 - Primary diagnostic start address\n"
        "+144 - No diagnostic start address\n"
        "+564 - Secondary diagnostic start address\n\n"
        " Before execution of the console emulator routine, primary diagnostic tests are\n" 
        " executed. After completion of the primary diagnostic tests the contents of R0,\n" 
        " R4, R6, and R5 will be printed out on the terminal. An @ sign will be printed at\n"
        " the beginning of the next line of the terminal, indicating that the console\n"
        " emulator routine is waiting for input from the operator.\n\n"
        " The following symbols are used in the text below:\n"
        " <SB> : Space bar\n" 
        " <CR> : Carriage return key\n" 
        " X    : Any octal number 0-7\n\n" 
        " The console functions can be exercised by pressing keys, as follows:\n" 
        " Function        Keyboard Strokes\n" 
        " Load Address    L<SB> XXXXXX <CR>\n" 
        " Examine         E<SB>\n" 
        " Deposit         D<SB> XXXXXX <CR>\n"
        " Start           S<CR>\n"
        " Boot            <Boot command code><unit><CR>\n\n"
        " The following boot command codes can be specified:\n"
        " Code  Device               Interface       Description\n"
        " DL    RL01                 RL11            Disk Memory\n"
        " DX    RXO1                 RX11            Floppy disk system\n"
        " DK    RK03, 05/05J         RK11C, D        DECpack disk\n"
        " DT    TU55/56              TC11            DualDECtape\n"
        " DY    RX02                 RX211           Double density floppy disk system\n"
        " DM    RK06/07              RK611           Disk drive\n"
        " MT    TS03, TU10           TM11/A11/B11    Magnetic tape (9 track, 800 bits/in, NRZ)\n"
        " MM    TUI6, TM02           RH11/RH70       Magnetic tape\n"
        " CT    TU60                 TA11            Dual magnetic tape\n"
        " PR    PC11                                 High speed reader\n"
        " TT    DL11-A                               Low speed reader\n"
        " DP    RP02/03              RP11            Moving head disk\n"
        " DB    RP04/05/06, RM02/03  RH11/RH70       Moving head disk\n"
        " DS    RS03/04              RH11/RH70       Fixed head disk\n\n"
        "3 B0\n"
        " Function:              11/60, 11/70 Diagnostic (M9312 E20)\n"
        " DEC Part number:       23-616F1\n"
        " Place in socket:       0\n\n"
        " Offsets\n" 
        "+000 - Diagnostics start address\n" 
        "+744 - Boot device start address\n\n"
        " Booting from unit 0 is normally performed by starting one of the boot ROMs in\n"
        " socket 1-4. Booting from other units than unit 0 can be performed via the B0\n"
        " diagnostics ROM. That ROM can be started at address 165744. Via that start address\n"
        " the boot code starting address and the unit to boot from can be specified in the\n"
        " console switch register. Bits 0-8 contain the starting address as an offset to\n"
        " 173000 and bits 9-11 contain the (octal) unit number. See the table below.\n\n"
        " 15  14  13  12 | 11 10  09         | 08  07  06  05  04  03  02  01  00\n"
        " NA  NA  NA  NA | Octal unit number | Start address boot code as offset to 17300\n\n"
        " The device is then booted as follows:\n"
        " 1. Load address 765744,\n"
        " 2. Set switch register according to the table above,\n"
        " 3. Start.\n\n"
        " To boot for example from unit 2, for the device for which the boot ROM is\n"
        " available in socket 1, the value 2012 has to be put in the console switch\n"
        " register.\n\n"
        "3 C0\n"
        " Function:              11/44 Diagnostic/Console (UBI; M7098 E58)\n"
        " DEC Part number:       23-446F1\n"
        " Place in socket:       0\n\n"
        " Offsets\n"
        "+000 - Start start address\n"
        "+020 - Diagnostics start address\n"
        "+144 - No diagnostics start address\n\n"
        "3 D0\n"
        " Function:              11/24 Diagnostic/Console (MEM; M7134 E74)\n"
        " DEC Part number:       23-774F1\n"
        " Place in socket:       0\n\n"
        " Offsets\n"
        "+020 - Diagnostics start address\n\n"
        "3 DL\n"
        " Function:              RL01/02 cartridge disk bootstrap\n" 
        " DEC Part number:       23-751A9\n" 
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DM\n"
        " Function:              RK06/07 cartridge disk bootstrap\n"
        " DEC Part number:       23-752A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DX\n"
        " Function:              RX01 floppy disk, single density bootstrap\n"
        " DEC Part number : 23 - 753A9\n"
        " Place in socket : 1 - 4\n\n"
        STD_OFFSETS
        "3 DP\n"
        " Function:              RP02/03 cartridge disk bootstrap\n"
        " DEC Part number:       23-755A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DB\n"
        " Function:              RP04/05/06, RM02/03/05 cartridge disk bootstrap\n"
        " DEC Part number:       23-755A9\n"
        " Place in socket:       1-4\n\n"
        " Offsets\n" \
        "+050 - Boot without diagnostics, unit 0\n"
        "+052 - Boot with diagnostics, unit 0\n"
        "+056 - Boot with diagnostics, unit in R0\n"
        "+062 - Boot with diag, unit in R0, CSR in R1\n\n"
        "3 DK\n"
        " Function:              RK03/05 DECdisk disk bootstrap\n"
        " DEC Part number:       23-756A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DT\n"
        " Function:              TU55/56 DECtape\n"
        " DEC Part number:       23-756A9\n"
        " Place in socket:       1-4\n\n"
        " Offsets\n"
        "+034 - Boot without diagnostics, unit 0\n"
        "+036 - Boot with diagnostics, unit 0\n"
        "+042 - Boot with diagnostics, unit in R0\n"
        "+046 - Boot with diag, unit in R0, CSR in R1\n\n"
        "3 MM\n"
        " Function:              TU16/45/77,TE16 magtape bootstrap\n"
        " DEC Part number:       23-757A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 MT\n"
        " Function:              TS03,TU10,TE10 magtape bootstrap\n"
        " DEC Part number:       23-758A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DS\n"
        " Function:              RS03/04 fixed disk bootstrap\n"
        " DEC Part number:       23-759A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 TT\n"
        " Function:              ASR33 lowspeed reader bootstrap\n"
        " DEC Part number:       23-760A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 PR\n"
        " Function:              PC05 hispeed reader bootstrap\n"
        " DEC Part number:       23-760A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 CT\n"
        " Function:              TU60 DECcassette bootstrap\n"
        " DEC Part number:       23-761A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 MS\n"
        " Function:              TS04/11,TU80,TSU05 tape bootstrap\n"
        " DEC Part number:       23-764A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DD\n"
        " Function:              TU58 DECtapeII bootstrap\n"
        " DEC Part number:       23-765B9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        " This ROM replaces 23-765A9 and fixes non-standard CSR access.\n\n"
        "3 DU\n"
        " Function:              MSCP UDA50 (RAxx) disk bootstrap\n"
        " DEC Part number:       23-767A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 DY\n"
        " Function:              RX02 floppy disk, double density bootstrap\n"
        " DEC Part number:       23-811A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 MU\n"
        " Function:              TMSCP TK50,TU81 magtape bootstrap\n"
        " DEC Part number:       23-E39A9\n"
        " Place in socket:       1-4\n\n"
        STD_OFFSETS
        "3 XE0\n"
        " Function:              Ethernet DEUNA/DELUA Net Boot (v2)\n"
        " DEC Part number:       23-E32A9\n"
        " Place in socket:       1-3\n\n"
        " The Ethernet DEUNA/DELUA Net Boot comprises two ROM’s, XE0 and XE1. These\n"
        " ROMs have to be placed in two subsequent sockets.\n\n"
        STD_OFFSETS
        "3 XE1\n"
        " Function:              Ethernet DEUNA/DELUA Net Boot (v2)\n"
        " DEC Part number:       23-E33A9\n"
        " Place in socket:       2-4\n\n"
        "3 XM0\n"
        " Function:              DECnet DDCMP DMC11/DMR11 bootstrap\n"
        " DEC Part number:       23-862A9\n"
        " Place in socket:       1-2\n\n"
        " The DECnet DDCMP DMC11/DMR11 bootstrap comprises three ROM’s, XM0, XM1 and\n"
        " XM2. These ROMs have to be placed in three subsequent sockets.\n\n"
        STD_OFFSETS
        "3 XM1\n"
        " Function:              DECnet DDCMP DMC11/DMR11 bootstrap\n"
        " DEC Part number:       23-863A9\n"
        " Place in socket:       2-3\n\n"
        "3 XM2\n"
        " Function:              DECnet DDCMP DMC11/DMR11 bootstrap\n"
        " DEC Part number:       23-864A9\n"
        " Place in socket:       3-4\n\n"
        "3 ZZ\n"
        " Function:              ROM diagnostics tests\n"
        " DEC Part number:       23-ZZZA9\n"
        " Place in socket:       1-4\n\n"
        " Offsets\n"
        "+004 - Boot without diagnostics, unit 0, standard CSR\n"
        "+006 - Boot with diagnostics, unit 0, standard CSR (Not used)\n"
        "+012 - Boot with unit in R0, standard CSR (Not used)\n"
        "+016 - Boot with unit in R0, CSR in R11\n\n"
        /***************** 80 character line width template *************************/
        "1 VT40\n"
        " The VT40 module is meant for the GT-40 graphic terminal, based on a\n"
        " PDP-11/05. The VT40 included a bootstrap ROM. The module has just one socket\n"
        " with one available ROM and a 'SET ROM TYPE=VT40' command suffices to\n"
        " select this boot ROM.\n"
        "1 Common_commands\n"
        " A number of commands is common to all modules types. This holds for the\n"
        " SHOW and some SET commands.\n"
        "2 SHOW_commands\n"
        " The ROM configuration can be shown by means of the SHOW command:\n\n"
        "+SHOW ROM\n\n"
        " This command shows the current settings of the module and - between angled\n"
        " brackets - the ROMs attached to the sockets of the module:\n\n"
        "+ROM     module type M9312, configuration mode AUTO, start address not specified\n"
        "+-       attached to <0:B0, 1 : DK, 2 : DL, 3 : DM, 4 : DX>, read only\n\n"
        " Note that the string between angled brackets matches the parameters used in an\n"
        " ATTACH command. Using this string in an ATTACH command will result in the\n"
        " ROM configuration as shown in the SHOW ROM command.\n\n"
        " A table-like view of the ROM configuration can be shown by adding the\n"
        " SOCKETs parameter:\n\n"
        "+SHOW ROM SOCKETS\n\n"
        " This commands shows a table in following format:\n\n"
        "+socket 0: address=17765000-17765777, image=B0\n"
        "+socket 1: address=17773000-17773177, image=DK\n"
        "+socket 2: address=17773200-17773377, image=DL\n"
        "+socket 3: address=17773400-17773577, image=DM\n"
        "+socket 4: address=17773600-17773777, image=DX\n"
        "2 SET_commands\n"
        "+SET ROM TYPE\t\tSelect the module tye\n"
        "+SET ROM WRITE\tWrite enable the ROMs\n\n"
        "3 TYPE\n"
        " The module type can be selected by means of the SET ROM TYPE command:\n\n"
        "+SET ROM TYPE=BLANK|M9312|VT40\n\n"
        " The default type is BLANK. This module type is supported for all CPU and bus\n"
        " types. The M9312 module is only available for the UNIBUS models; the VT40 is\n"
        " only available for the GT40 graphics terminal which contains an 11/05 CPU.\n"
        " If the CPU is set to a type not supported by the set ROM module type, the\n"
        " module type is reset to the BLANK module.\n\n"
        "3 WRITE\n"
        " As their name suggest, ROM's in principle are read-only. The simulator\n"
        " however provides a way to write-enable the ROMs. The SET ROM WRITE_ENABLE\n"
        " command can be used to enable changing the contents of a ROM:\n\n"
        "+SET ROM WRITE=ENABLED|DISABLED\n\n"
        " The default value is DISABLED. After write-enabling the ROMs, the contents\n"
        " can be changed by means of DEPOSIT commands.\n";

    return scp_help (st, dptr, uptr, flag, rom_helptext, cptr);
}


/* Print attach command help */

static t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
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
