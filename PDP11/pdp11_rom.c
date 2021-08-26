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
#include "pdp11_cpumod.h"
#include "pdp11_m9312.h"
#include "pdp11_vt40boot.h"

/* Forward references */
t_stat rom_rd (int32 *data, int32 PA, int32 access);
t_stat rom_wr (int32 data, int32 PA, int32 access);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_boot (int32 u, DEVICE *dptr);
t_stat rom_set_type (UNIT *, int32, CONST char *, void *);
t_stat rom_show_type (FILE *, UNIT *, int32, CONST void *);
static t_stat rom_auto_configure(UNIT*, int32, CONST char*, void*);
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
static t_stat vt40_auto_config ();
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
static void m9312_set_default_start_address ();
static t_stat blank_help_attach(FILE*, DEVICE*, UNIT*, int32, const char*);
static t_stat m9312_help_attach(FILE*, DEVICE*, UNIT*, int32, const char*);
static t_stat vt40_help_attach(FILE*, DEVICE*, UNIT*, int32, const char*);

/* External references */
extern uint32 cpu_type;
extern uint32 cpu_opt;
extern uint32 cpu_model;
extern CPUTAB cpu_tab[];
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
    create_filename_blank,                  /* Create unit file name */
    blank_rom_rd,                           /* ROM read function */
    blank_set_start_address,                /* ROM set start address */
    blank_show_start_address,               /* ROM show start address */
    blank_help_attach,                      /* Help attach */
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
    create_filename_embedded,               /* Create unit file name */
    m9312_rd,                               /* ROM read function */
    m9312_set_start_address,                /* ROM set start address */
    m9312_show_start_address,               /* ROM show start address */
    m9312_help_attach,                      /* Help attach */
};

/* Define the VT40 module */

MODULE_DEF vt40 =
{
    "VT40",                                 /* Module name */
    CPUT_05,                                /* Required CPU types */
    UNIBUS_MODEL,                           /* Required CPU options */
    VT40_NUM_SOCKETS,                       /* Number of sockets */
    (SOCKET_DEF (*)[]) & vt40_sockets,      /* Pointer to SOCKET_DEF structs */
    &vt40_auto_config,                      /* Auto configuration function */
    embedded_attach,                        /* Attach function */
    create_filename_embedded,               /* Create unit file name */
    blank_rom_rd,                           /* ROM read function */
    blank_set_start_address,                /* ROM set start address */
    blank_show_start_address,               /* ROM show start address */
    vt40_help_attach,                       /* Help attach */
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
    { ORDATAD (TYPE,          selected_type,     16,   "Type"), REG_HIDDEN  },
    { ORDATAD (START_ADDRESS, rom_start_address, 16,   "Start address"), REG_HIDDEN },
    { NULL }
};

/*
 * Define the ROM device modifiers.
 * 
 * The AUTO_CONFIGURE and WRITE_ENABLE modifiers are settable only
 * modifiers. The AUTO_CONFIGURE command is not a real modifier but
 * a one-shot command to configure the ROMs.
 * The WRITE_ENABLE modifier is settable only to prevent that the value
 * of this parameter is shown twice in the SHOW ROM output, its value
 * is shown by the units read-only flag in the SHOW ROM output.
 * 
 * The SOCKETS modifier is a view-only modifer and prints the relevant
 * information for all sockets in a pretty format.
 */
MTAB rom_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "TYPE", "TYPE",
        &rom_set_type, &rom_show_type, NULL, "ROM type (BLANK, M9312 or VT40)" },
    { MTAB_XTD | MTAB_VDV,             0, NULL, "AUTO_CONFIGURE",
        &rom_auto_configure, NULL, NULL, "Auto configure ROMs" },
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

    /* Disallow SET commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "Non-existent device\n");

    /* Is a module type specified? */
    if (cptr == NULL)
        return sim_messagef (SCPE_ARG, "No module specified\n");

    /* Search the module list for the specified module type */
    for (module_number = 0; module_number < NUM_MODULES; module_number++) {
        if (strcasecmp (cptr, module_list[module_number]->name) == 0) {
            
            /* Check if the module is allowed on this cpu and bus type */
            if (!module_type_is_valid (module_number))
                return sim_messagef (SCPE_ARG, 
                    "Module %s is not compatible with current cpu (%s) and/or bus type (%s)\n",
                    cptr, cpu_tab[cpu_model].name, UNIBUS ? "Unibus" : "Q-bus");

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

                /* Auto-configure the module if available */
                if (module_list[selected_type]->auto_config != NULL)
                    (*module_list[selected_type]->auto_config)();
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
    /* Disallow SHOW commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "ROM\tdisabled\n");

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

    /* Disallow SHOW commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "ROM\tdisabled\n");

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


/* Perform an auto-configuration */

static t_stat rom_auto_configure (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    /* Disallow SET commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "Non-existent device\n");

    if (cptr != NULL)
        return sim_messagef (SCPE_ARG, "The SET ROM AUTO_CONFIGURE command takes no parameter\n");

    /* Check if auto config is available for the selected module */
    if (module_list[selected_type]->auto_config != NULL)
        (*module_list[selected_type]->auto_config)();
    else
        return sim_messagef (SCPE_ARG, "Auto configuration is not available for the %s module\n",
        module_list[selected_type]->name);

    return SCPE_OK;
}


/* Set ROM bootstrap start address */

t_stat rom_set_start_address(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    /* Disallow SET commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "Non-existent device\n");

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
    /* Disallow SHOW commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "ROM\tdisabled\n");

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
    /* Disallow SET commands on a disabled ROM device */
    if (rom_dev.flags & DEV_DIS)
        return sim_messagef (SCPE_ARG, "Non-existent device\n");

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
 * 
 * There is no need to check the device is enabled as we cannot
 * get into a state in which ROMs are attached on a disabled
 * ROM device. ROM's have to be detached before the device can
 * be disabled and ATTACH and auto-configure commands are
 * disallowed on a disabled device.
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

    /* 
     * Set the boot address
     * 
     * During a hardware boot the PC and PSW are read from locations 173024 and
     * 173026 respectively. The contents of address 173026 are provided in the
     * ROMs. The value of the PSW in this address for all M9312 boot ROMS is 340.
     * The cpu_set_boot function sets the PSW to this value.
     */
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

        /* Remove ROM image */
        free (socket_config[socket_number].rom_image);
        socket_config[socket_number].rom_image = NULL;
        return sim_messagef (SCPE_IERR, "reset_dib() failed\n");
    }

    /* Save the specified ROM image name in the configuration */
    strlcpy (socket_config[socket_number].rom_name, name, CBUFSIZE);
    
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

static t_stat vt40_auto_config ()
{
    SOCKET_DEF *socketptr;
    ROM_DEF *romptr;
    uint32 socket_number = 0;
    t_stat r;

    /* A VT40 has just one socket */
    socketptr = &vt40_sockets[socket_number];
    romptr = *socketptr->rom_list;

    /* Attach the ROM to the socket */
    if ((r = attach_rom_to_socket(romptr->device_mnemonic, socketptr->base_address,
        romptr->image, socketptr->size, romptr->rom_init, socket_number)) != SCPE_OK)
        return r;

    /* Compute the VT40 start adress and make it a 16-bit physical address */
    rom_start_address = socket_config[socket_number].base_address + romptr->boot_no_diags;
    rom_start_address &= VAMASK;

    return SCPE_OK;
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
    MODULE_DEF *modptr = *(module_list + selected_type);
    SOCKET_DEF *socketptr = *modptr->sockets + socket_number;

    /* Clear socket information */
    socket_config[socket_number].base_address = socketptr->base_address;
    socket_config[socket_number].rom_size = socketptr->size;
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
    UNIT* uptr;
    uint32 unit_number;
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

            /* Is any unit of this device attached? */
            for (uptr = dptr->units, unit_number = 0; unit_number < dptr->numunits; unit_number++) {
                if ((uptr + unit_number)->flags & UNIT_ATT) {

                    /* Search device_rom map for a suitable boot ROM for the device */
                    for (mptr = &device_rom_map[0]; mptr->device_name != NULL; mptr++) {
                        if (strcasecmp (dptr->name, mptr->device_name) == 0) {
                            if ((result = m9312_attach (mptr->rom_name, socket_number)) != SCPE_OK)
                                return result;
                            socket_number++;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    /* Is any ROM attached? */
    if (socket_number == 1)
        sim_printf ("warning - no boot ROMs attached for lack of attached devices\n");

    /* Set the default start address for the new configuration of ROMs */
    m9312_set_default_start_address ();

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

/*
 * The default start address is the NO-DIAG entry point of the ROM
 * attached to the boot ROM in socket 1 (if any).
 */
static void m9312_set_default_start_address ()
{
    ROM_DEF* romptr;
    int offset;

    /* Clear start address in case no valid start address can be determined */
    rom_start_address = 0;

    /* Check a ROM is available in socket 1 */
    if (socket_config[1].rom_image != NULL) {

        /* Get pointer to ROM in socket 1 */
        romptr = find_rom (socket_config[1].rom_name, (ROM_DEF (*)[]) &boot_roms);

        /* Get NO_DIAG offset for this ROM */
        offset = romptr->boot_no_diags;

        /* Calculate the start address from the ROM NO_DIAG offset*/
        if (offset != NO_START_ADDRESS)
            rom_start_address = (socket_config[1].base_address + offset) & VAMASK;
    }
}

/* Define help texts */

#define ROM_HLP_WRITE_ENABLE \
    " As their name suggest, ROM's in principle are read-only. The simulator\n" \
    " however provides a way to write-enable the ROMs. The SET ROM WRITE_ENABLE\n" \
    " command can be used to enable changing the contents of a ROM:\n\n" \
    "+SET ROM WRITE=ENABLED|DISABLED\n\n" \
    " The default value is DISABLED. After write-enabling the ROMs, the contents\n" \
    " can be changed by means of DEPOSIT commands.\n\n" \
    " Note that changing the contents of a ROM does not affect the original\n" \
    " source of the ROM. After re-attaching a ROM which has been changed, the\n" \
    " original contents are available again.\n"

#define ROM_HLP_SET_TYPE \
    " The module type can be selected by means of the SET ROM TYPE command:\n\n" \
    "+SET ROM TYPE=BLANK|M9312|VT40\n\n" \
    " The default type is BLANK. This module type is supported for all CPU and\n" \
    " bus types. The M9312 module is only available for the Unibus models; the\n" \
    " VT40 is only available for the GT40 graphics terminal which contains an\n" \
    " 11/05 CPU. If the CPU is set to a type not supported by the current module\n" \
    " type, the module type is reset to the BLANK module.\n\n" \
    " Setting the module type to the already selected module type has no effect.\n"

#define ROM_HLP_SET_START_ADDRESS \
    " The system can be booted by starting it at one of the suitable boot\n" \
    " addresses in one of the ROMs. The boot address can be set by means of the\n" \
    " SET START_ADDRESS command:\n\n" \
    "+SET ROM START_ADDRESS=<address>\n\n" \
    " The address must be a 16-bit physical address in an attached ROM. The\n" \
    " address to be used is determined by a combination of the socket base\n" \
    " address and offset in the ROM in the following way:\n\n" \
    "+1. Determine the socket base address the ROM is placed in,\n" \
    "+2. Determine the appropriate start address in the ROM and take that\n" \
    "+ address as an offset from the ROM base address,\n" \
    "+3. Add the ROM-specific offset to the socket base address.\n\n" \
    " After setting the start address the system can be started via a BOOT\n" \
    " command:\n\n" \
    "+BOOT {ROM|CPU}\n"

#define ROM_HLP_DETACH \
    " The DETACH command detaches all ROMs from the sockets and clears the start\n" \
    " address. The module type is not affected by this command.\n"

#define STD_OFFSETS \
    " Offsets\n" \
    "+004 - Boot without diagnostics, unit 0\n" \
    "+006 - Boot with diagnostics, unit 0\n" \
    "+012 - Boot with diagnostics, unit in R0\n" \
    "+016 - Boot with diagnostics, unit in R0, CSR in R1\n\n"

const char rom_helptext[] =
    /***************** 80 character line width template *************************/
    " A hardware PDP-11 comprises a module with Read Only Memory (ROM) code,\n"
    " containing console emulator, diagnostic and bootstrap functionality.\n"
    " Each module has one or more ROMs available, the contents of which can be\n"
    " accessed by a reserved address space in the I/O page.\n\n"
    " Currently the ROM device supports the following module types:\n\n"
    "+1. BLANK\tProvides the contents of a file as a ROM\n"
    "+2. M9312\tEmulator, diagnostics and boot ROMs for Unibus models\n"
    "+3. VT40\t\tThe VT40 ROM for a GT40 graphical terminal\n"
    /***************** 80 character line width template *************************/
    "1 Blank_Configuration\n"
    " The BLANK ROM module is a module with four sockets, in each of which one\n"
    " ROM can be placed. The contents of the ROMs has to be provided in files.\n\n"
    " First the BLANK module type must be selected:\n\n"
    "+SET ROM TYPE=BLANK\n\n"
    " Next, the BLANK module can be configured by the ATTACH and various SET\n"
    " commands.\n"
    "2 Attach_Command\n"
#define BLANK_HLP_ATTACH "Blank_Configuration Attach_Command"
    " ROMs can be placed in the module's sockets by means of the ATTACH command.\n"
    " For a succesful attach the following parameters have to be specified:\n\n"
    "+1. The socket number to attach a ROM to,\n"
    "+2. The I/O space address in which the ROM contents will be available,\n"
    "+3. The file with the ROM contents in RAW format.\n\n"
    " A single attach specification has the following format:\n\n"
    "+ATTACH ROM {<socket>:}<address>/<file>\n\n"
    " The socket is a number between 0 and 3. The socket number is optional and\n"
    " the default number is 0. The address is a physical address in I/O SPACE,\n"
    " i.e. between addresses 17760100 and 17777777. The address must not collide\n"
    " with register addresses used by other devices. The file is the name of a\n"
    " file with the contents of the ROM in RAW format.\n\n"
    " An attach command can comprise one to four attach specifications, separated\n"
    " by a comma:\n\n"
    "+ATTACH ROM {<socket>:}<address>/<file>{,{<socket>:}<address>/<file>}*\n\n"
    " The default socket number for the first attach specification in the ATTACH\n"
    " command is zero, for the following specifications it is the socket number\n"
    " of the previous specification plus one.\n\n"
    " The following commands attach two ROMs, at socket 0 and 1:\n\n"
    "+ATTACH ROM 0:17765000/23-616F1.IMG\n"
    "+ATTACH ROM 1:17773000/23-751A9.IMG\n\n"
    " These commands can also be combined in one ATTACH command:\n\n"
    "+ATTACH ROM 0:17765000/23-616F1.IMG, 17773000/23-751A9.IMG\n\n"
    " The effect of the last command is equal to the two separate ATTACH\n"
    " commands.\n\n"
    " Note that the syntax of the ATTACH command varies slightly between the\n"
    " different ROM module types. For the M9312 and VT40 module the address\n"
    " cannot be specified as these modules have fixed socket adresses.\n"
    "2 Detach_Command\n"
    " The DETACH command detaches all ROMs from the sockets, clears the socket\n"
    " addresses and clears the start address. The module type is not affected by\n"
    " this command.\n"
    "2 Set_Commands\n"
    " The BLANK module type supports the following SET commands:\n\n"
    "+SET ROM TYPE\t\t- Change the module type\n"
    "+SET ROM START_ADDRESS\t- Set the start address for the ROMs\n"
    "+SET ROM WRITE\t\t- Write enable the ROMs\n"
    "3 Type\n"
    ROM_HLP_SET_TYPE
    "3 Start_Address\n"
    ROM_HLP_SET_START_ADDRESS
    "3 Write\n"
    ROM_HLP_WRITE_ENABLE
    /***************** 80 character line width template *************************/
    "1 M9312_Configuration\n"
    " The M9312 module contains 512 words of read only memory (ROM) that can be\n"
    " used for diagnostic routines, the console emulator routine, and bootstrap\n"
    " programs. The module contains five sockets that allow the user to insert\n"
    " ROMs, enabling the module to be used with any Unibus PDP11 system and boot\n"
    " any peripheral device by simply adding or changing ROMs.\n\n"
    " The M9312 is configured by placing ROMs in the module's sockets with ATTACH\n"
    " commands and with various SET commands.\n"
    "2 Addressing\n"
    " The M9312 module has 512 words of read only memory. The lower 256 words\n"
    " (addresses 165000 through 165776) are used for the storage of ASCII console\n"
    " and diagnostic routines. The diagnostics are rudimentary CPU and memory\n"
    " diagnostics. The upper 256 words (addresses 173000 through 173776) are used\n"
    " for bootstrap programs. These upper words are divided further into four\n"
    " 64-word segments. In principle each of the segments 0 to 4 contains the\n"
    " boot programs for one or two device types. If necessary however, more than\n"
    " one segment may be used for a boot program. The following table shows the\n"
    " ROM segmentation.\n\n"
    "+ROM type                                           Base address\n"
    "+256 word console emulator and diagnostics ROM #0   165000\n"
    "+64 word boot ROM #1                                173000\n"
    "+64 word boot ROM #2                                173200\n"
    "+64 word boot ROM #3                                173400\n"
    "+64 word boot ROM #4                                173600\n\n"
    " For each segment a socket is available in which a ROM can be placed.\n"
    " Socket 0 is solely used for a diagnostic ROM (PDP-11/60 and 11/70 systems)\n"
    " or a ROM which contains the console emulator routine and diagnostics for\n"
    " all other PDP-11 systems. The other four sockets accept ROMs which contain\n"
    " bootstrap programs. In general one boot ROM contains the boot code for one\n"
    " device type. In some cases a boot rom contains the boot code for two device\n"
    " types and there are also cases where the boot code comprises two or three\n"
    " ROMs. In these cases these ROMs have to be placed in subsequent sockets.\n\n"
    " The system start adress can be determined in the following way:\n\n"
    "+1. Determine the socket base address the ROM is placed in,\n" \
    "+2. Determine the appropriate start address in the ROM and take that\n" \
    "+address as an offset from the ROM base address. The offsets are\n" \
    "+documented in the ROM-specific help text.\n" \
    "+3. Add the ROM-specific offset to the socket base address.\n\n" \
    /***************** 80 character line width template *************************/
    " With a DL boot ROM in socket 1 e.g., a RL01 disk can be booted from unit 0,\n"
    " without performing the diagnostics, by starting at address 173004. With the\n"
    " same boot ROM placed in socket 2 that unit can be booted by starting at\n"
    " address 173204. Note that the start address specifies whether or not the\n"
    " diagnostics code is executed.\n\n"
#define M9312_HLP_ATTACH "M9312_Configuration Attach_Command"
    "2 Attach_Command\n"
    " The M9312 module can be configured by means of the ATTACH command. For a\n"
    " succesful attach the following parameters have to be specified:\n\n"
    "+1. The socket number to attach the ROM to,\n"
    "+2. The name of the ROM image.\n\n"
    " A single attach specification has the following format:\n\n"
    "+ATTACH ROM {<socket>:}<ROM>\n\n"
    " The socket is a number between 0 and 4. The socket number is optional and\n"
    " the default number is 0. The ROM is the name of one of the available ROMs.\n"
    " An attach command can comprise one to four attach specifications,\n"
    " seperated by a comma:\n\n"
    "+ATTACH ROM {<socket>:}<ROM>{,{<socket>:}<ROM>}*\n\n"
    " The default socket number for the first attach specification in the ATTACH\n"
    " command is zero, for the following specifications it is the socket number\n"
    " of the previous specification plus one. The following commands attach two\n"
    " ROMs, at socket 0 and 1:\n\n"
    "+ATTACH ROM 0:B0\n"
    "+ATTACH ROM 1:DL\n\n"
    " These commands can also be combined in one ATTACH command:\n\n"
    "+ATTACH ROM B0,DL\n\n"
    " The effect of this command is equal to the two separate attach commands.\n\n"
    " Note that the syntax of the ATTACH command varies slightly between the\n"
    " different module types. For the BLANK module type a socket address has to\n"
    " be specified, as the sockets in this module have no fixed address.\n"
    "2 Detach_Command\n"
    ROM_HLP_DETACH
    "2 Set_Commands\n"
    " The M9312 module supports the following SET commands:\n\n"
    "+SET ROM TYPE\t\t- Change the module type\n"
    "+SET ROM AUTO_CONFIGURE\t- Auto-configure the ROM lineup\n"
    "+SET ROM START_ADDRESS\t- Set the start address for the ROMs\n"
    "+SET ROM WRITE\t\t- Write enable the ROMs\n"
    "3 Type\n"
    ROM_HLP_SET_TYPE "\n"
    " When the ROM type is set to M9312, an auto-configuration is performed. The\n"
    " sockets are filled with suitable ROMs for the current simulator\n"
    " configuration and the start address is set to the no-diagnostics start\n"
    " address of the ROM in the first boot ROM socket (i.e. socket 1).\n"
    "3 Configuration\n"
    " The M9312 implementation supports an auto-configuration option which can be\n"
    " performed with the AUTO_CONFIGURE parameter:\n\n"
    "+SET AUTO_CONFIGURE\n\n"
    " In the auto-configuration the sockets are filled with ROMs suited for the\n"
    " the current simulator configuration. For socket 0 the ROM selected depends\n"
    " on the CPU type, for sockets 1 to 4 the ROM lineup is determined by the\n"
    " devices with at least one attached unit. In case there are more devices with\n"
    " an attached unit than available sockets, boot ROMs are selected for the\n"
    " first four devices detected during the device scan. If no boot ROM could be\n"
    " selected a warning is given.\n\n"
    " After the suitable boot ROMs have been attached to the sockets, the start\n"
    " address is set to the no-diagnostics start address of the ROM in the first\n"
    " boot ROM socket (i.e. socket 1). If no ROM is attached to that socket, the\n"
    " start address is cleared.\n\n"
    " The auto-configured lineup of ROMs can be overriden by ATTACH commands for\n"
    " the sockets or by a renewed auto-configuration in case the simulator\n"
    " configuration has been changed.\n"
    "3 Start_Address\n"
    " The system can be booted by starting it at one of the suitable boot\n"
    " addresses in one of the ROMs. The boot address can be set by means of the\n"
    " SET START_ADDRESS command:\n\n"
    "+SET ROM START_ADDRESS=<address>\n\n"
    " The address must be a 16-bit physical address in an attached ROM. The\n"
    " address to be used is determined by a combination of the socket base\n"
    " address and offset in the ROM. See 'HELP ROM M9312 Addresses' for an\n"
    " explanation.\n\n"
    " Boot ROMs usually provide two start address for respectively starting\n"
    " without and with performing diagnostics before the device is booted. These\n"
    " addresses have been given the symbolic names '<ROM>-DIAG' and '<ROM>+DIAG'.\n"
    " To make setting the start address more user-friendly, these symbolic names\n"
    " can be used to specify the start address:\n\n"
    "+SET ROM START_ADDRESS=<ROM>+DIAG|<ROM>-DIAG\n\n"
    " '<ROM>' must be the name of a ROM currently attached to a socket.\n\n"
    " After setting the start address the system can be started via a BOOT\n"
    " command:\n\n"
    "+BOOT {ROM|CPU}\n"
    "3 Write\n"
    ROM_HLP_WRITE_ENABLE
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
    " Before execution of the console emulator routine, primary diagnostic tests\n"
    " are executed. After completion of the primary diagnostic tests the contents\n"
    " of R0, R4, R6 and R5 will be printed out on the terminal. An @ sign will be\n"
    " printed at the beginning of the next line of the terminal, indicating that\n"
    " the console emulator routine is waiting for input from the operator.\n\n"
    " The following symbols are used in the text below:\n"
    "+<SB> : Space bar\n"
    "+<CR> : Carriage return key\n"
    "+X    : Any octal number 0-7\n\n"
    " The console functions can be exercised by pressing keys, as follows:\n"
    "+Function        Keyboard Strokes\n"
    "+Load Address    L<SB> XXXXXX <CR>\n"
    "+Examine         E<SB>\n"
    "+Deposit         D<SB> XXXXXX <CR>\n"
    "+Start           S<CR>\n"
    "+Boot            <Boot command code><unit><CR>\n\n"
    " The following boot command codes can be specified:\n\n"
    "+Code  Device               Interface       Description\n"
    "+DL    RL01                 RL11            Disk Memory\n"
    "+DX    RXO1                 RX11            Floppy disk system\n"
    "+DK    RK03, 05/05J         RK11C, D        DECpack disk\n"
    "+DT    TU55/56              TC11            DualDECtape\n"
    "+DY    RX02                 RX211           Double density floppy disk\n"
    "+DM    RK06/07              RK611           Disk drive\n"
    "+MT    TS03, TU10           TM11/A11/B11    Magnetic tape\n"
    "+MM    TUI6, TM02           RH11/RH70       Magnetic tape\n"
    "+CT    TU60                 TA11            Dual magnetic tape\n"
    "+PR    PC11                                 High speed reader\n"
    "+TT    DL11-A                               Low speed reader\n"
    "+DP    RP02/03              RP11            Moving head disk\n"
    "+DB    RP04/05/06, RM02/03  RH11/RH70       Moving head disk\n"
    "+DS    RS03/04              RH11/RH70       Fixed head disk\n\n"
    "3 B0\n"
    " Function:              11/60, 11/70 Diagnostic (M9312 E20)\n"
    " DEC Part number:       23-616F1\n"
    " Place in socket:       0\n\n"
    " Offsets\n"
    "+000 - Diagnostics start address\n"
    "+744 - Boot device start address\n\n"
    " Booting from unit 0 is normally performed by starting one of the boot ROMs\n"
    " in socket 1-4. Booting from other units than unit 0 can be performed via\n"
    " the B0 diagnostics ROM. That ROM can be started at address 165744. Via that\n"
    " start address the boot code starting address and the unit to boot from can\n"
    " be specified in the console switch register. Bits 0-8 contain the starting\n"
    " address as an offset to 173000 and bits 9-11 contain the (octal) unit\n"
    " number. See the table below.\n\n"
    " 15  14  13  12 | 11 10 09    | 08  07  06  05  04  03  02  01  00\n"
    " NA  NA  NA  NA | Unit number | Start address boot code as offset to 173000\n\n"
    " The device is then booted as follows:\n\n"
    "+1. Load address 765744,\n"
    "+2. Set switch register according to the table above,\n"
    "+3. Start.\n\n"
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
    "+062 - Boot with diagnostics, unit in R0, CSR in R1\n\n"
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
    "+046 - Boot with diagnostics, unit in R0, CSR in R1\n\n"
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
    "1 VT40_Configuration\n"
    " The VT40 module is meant for the GT-40 graphic terminal, based on a\n"
    " PDP-11/05. The VT40 included a bootstrap ROM. The module has just one\n"
    " socket with one available ROM (VT40). The module can be configured by\n"
    " means of the ATTACH and various SET commands, but a 'SET ROM TYPE=VT40'\n"
    " command suffices to configure this module.\n"
#define VT40_HLP_ATTACH "VT40_Configuration Attach_Command"
    "2 Attach_Command\n"
    " The VT40 ROM can be attached to the one socket in this module by means of\n"
    " the following command:\n\n"
    "+ATTACH ROM VT40\n"
    "2 Detach_Command\n"
    ROM_HLP_DETACH
    "2 Set_Commands\n"
    " The VT40 module type supports the following SET commands:\n\n"
    "+SET ROM TYPE\t\t- Change the module type\n"
    "+SET ROM START_ADDRESS\t- Set the start address for the ROMs\n"
    "+SET ROM WRITE\t\t- Write enable the ROMs\n"
    "3 Type\n"
    ROM_HLP_SET_TYPE "\n"
    " When the ROM type is set to VT40, an auto-configuration is performed. The\n"
    " module's socket is filled with the VT40 ROM and the start address is set to\n"
    " the start address of that ROM.\n"
    "3 Start_Address\n"
    ROM_HLP_SET_START_ADDRESS
    "3 Write\n"
    ROM_HLP_WRITE_ENABLE
    "1 Monitoring\n"
    " The ROM configuration can be shown by means of the SHOW command:\n\n"
    "+SHOW ROM\t\t- Show ROM configuration and settings\n"
    "+SHOW ROM SOCKETS\t- Show ROM addresses and occupation\n"
    "+SHOW ROM TYPE\t\t- Shows the selected module type\n"
    "+SHOW ROM START_ADDRESS\t- Shows the start address\n"
    "2 ROM\n"
    " This command shows the current settings of the module and - between angled\n"
    " brackets - the ROMs attached to the sockets of the module:\n\n"
    "+ROM     module type M9312, start address not specified\n"
    "+-       attached to <0:B0, 1:DK, 2:DL, 3:DM, 4:DX>, read only\n\n"
    " Note that the string between angled brackets matches the parameters used in\n"
    " an ATTACH command. Using this string in an ATTACH command will result in\n"
    " the ROM configuration as shown in the SHOW ROM command.\n\n"
    "2 Sockets\n"
    " A table-like view of the ROM configuration can be shown by the adding the\n"
    " SOCKETS parameter to the SHOW ROM command:\n\n"
    "+SHOW ROM SOCKETS\n\n"
    " This command shows a table in following format:\n\n"
    "+socket 0: address=17765000-17765777, image=B0\n"
    "+socket 1: address=17773000-17773177, image=DK\n"
    "+socket 2: address=17773200-17773377, image=DL\n"
    "+socket 3: address=17773400-17773577, image=DM\n"
    "+socket 4: address=17773600-17773777, image=DX\n"
    "2 Type\n"
    " The SHOW ROM TYPE command shows the currently selected module type, either\n"
    " BLANK, M9312 or VT40.\n"
    "2 Start_Address\n"
    " The SHOW ROM START_ADDRESS command shows the set start address in the\n"
    " address space of the attached ROMs or 'not specified' if the start address\n"
    " is not set.\n"
    "1 Booting_the_System\n"
    " There are two ways to boot from an attached ROM:\n\n"
    "+1. By starting the simulator at an address in the ROM address space,\n"
    "+2. By setting the starting address and then issuing a BOOT command.\n\n"
    " The simulator can be started at an address in the ROM address space via a\n"
    " GO or RUN command:\n\n"
    "+GO <address>\n"
    "+RUN <address>\n\n"
    " For more information see the help for these commands.\n\n"
    " The simulator can also be started by first setting the start address in the\n"
    " ROM code and subsequently issuing a BOOT command. The start address is set\n"
    " by the following command:\n\n"
    "+SET ROM START_ADDRESS=<address>\n\n"
    " The address must be a 16-bit physical address in an attached ROM. The M9312\n"
    " supports a symbolic specification of the start address. Consult the\n"
    " 'HELP ROM M9312 SET START_ADDRESS' help text for more information.\n\n"
    " After setting the start address the system can be started via a BOOT\n"
    " command:\n\n"
    "+BOOT {ROM|CPU}\n"
    "1 Command_Summary\n"
    " The ROM device accepts the following SET commands:\n\n"
    "+SET ROM TYPE=BLANK|M9312|VT40\n"
    "+SET ROM AUTO_CONFIGURE (ROM types M9312 and VT40)\n"
    "+SET ROM START_ADDRESS=<address> (ROM types BLANK and VT40)\n"
    "+SET ROM START_ADDRESS=<address>|<ROM>+DIAG|<ROM>-DIAG (ROM type M9312)\n"
    "+SET ROM WRITE=ENABLED|DISABLED\n\n"
    " The ROM device accepts the following ATTACH and DETACH commands:\n\n"
    "+ATTACH ROM {socket:}<address>/file (ROM type BLANK)\n"
    "+ATTACH ROM {socket:}<ROM> (ROM types M9312 and VT40)\n"
    "+DETACH ROM\n\n"
    " The ROM device accepts the following SHOW commands:\n\n"
    "+SHOW ROM\n"
    "+SHOW ROM SOCKETS\n"
    "+SHOW ROM TYPE\n"
    "+SHOW ROM START_ADDRESS\n\n"
    " The ROM device accepts the following HELP commands:\n\n"
    "+HELP ROM\n"
    "+HELP ROM SET\n"
    "+HELP ROM SHOW\n"
    "+HELP ROM ATTACH - help text is module-specific\n\n"
    " The ROM device also accepts the following commands:\n\n"
    "+RESET ROM\n"
    "+BOOT ROM\n"
    "1 Examples\n"
    "2 Blank_Example\n"
    " The BLANK module type has four sockets in which ROMs can be placed, the\n"
    " contents of which have to be provided in files.\n\n"
    " The following commands attach two ROMs to the sockets and use these ROMs to\n"
    " boot the system:\n\n"
    "+SET CPU 11/70\n"
    "+ATTACH ROM 0:17765000/23-616F1.IMG, 17773000/23-751A9.IMG\n"
    "+SET ROM START_ADDRESS=173006\n"
    "+BOOT ROM\n"
    "2 M9312_Examples\n"
    "3 Attach_ROMs\n"
    " The following commands show the different uses of the ATTACH command:\n\n"
    "+ATTACH ROM 0:B0\n"
    "+ATTACH ROM 2:DL\n\n"
    " These commands attach the B0 rom to socket 0 and the DL rom to socket 2.\n\n"
    "+ATTACH ROM 0:B0,2:DL\n\n"
    " This command has the same effect as the two separate commands above.\n\n"
    "+ATTACH ROM B0,DL,DK,DB\n\n"
    " This command attaches the B0, DL, DK and DB ROMs to sockets 0 to 4.\n\n"
    "+ATTACH ROM 2:DL,DK\n\n"
    " This command attaches the DL ROM to socket 2 and the DK ROM to socket 3.\n"
    "3 Auto-configuration\n"
    " The M9312 supports an auto-configuration option for placing appropriate\n"
    " ROMs in its sockets. This auto-configuration is performed when the module\n"
    " type is changed to M9312 or when the SET ROM AUTO_CONFIGURE command is\n"
    " given.\n\n"
    "+SET CPU 11/70\n"
    "+SET ROM TYPE=M9312\n"
    "+warning - no boot ROMs attached for lack of attached devices\n"
    "+SHOW ROM\n"
    "+ROM     module type M9312, start address not specified\n"
    "+-       attached to <0:B0>, read only\n\n"
    " The auto configuration takes into account the current CPU type and the\n"
    " devices having at least one attached unit, as can be demonstrated by the\n"
    " following commands:\n\n"
    "+SET CPU 11/34\n"
    "+ATTACH RK0 RK0.DSK\n"
    "+ATTACH RL0 RL0.DSK\n"
    "+SET ROM AUTO_CONFIGURE\n"
    "+SHOW ROM\n"
    "+ROM     module type M9312, start address not specified\n"
    "+-       attached to <0:A0, 1:DK, 2:DL>, read only\n"
    "3 Booting_the_system\n"
    " By means of the auto-configuration and symbolic start addressing features\n"
    " of the M9312 implementation, the system can be started simply via the ROMs.\n"
    " The following commands suffice to boot the system, given the right boot\n"
    " media are available in DL0:\n\n"
    "+ATTACH RL0 rl0.dsk"
    "+SET ROM TYPE=M9312\n"
    "+BOOT\n"
    "2 VT40_Example\n"
    " Configuring the VT40 module type for use is quite simple as it has just one\n"
    " socket and one availabe ROM. Setting the type suffices to be able to use\n"
    " the module:\n\n"
    "+SET CPU 11/05\n"
    "+SET ROM TYPE=VT40\n"
    "+SHOW ROM\n"
    "+ROM     module type VT40, start address 166000\n"
    "+-       attached to <0:VT40>, read only\n";

/* Print help */

static t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    return scp_help (st, dptr, uptr, flag, rom_helptext, cptr);
}


/* Print attach command help */

static t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    /* Forward the command to the module-specific function */
    return module_list[selected_type]->help_attach (st, dptr, uptr, flag, rom_helptext);
}

static t_stat blank_help_attach (FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr)
{
    return scp_help(st, dptr, uptr, flag, rom_helptext, BLANK_HLP_ATTACH);
}

static t_stat m9312_help_attach(FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr)
{
    return scp_help(st, dptr, uptr, flag, rom_helptext, M9312_HLP_ATTACH);
}

static t_stat vt40_help_attach(FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr)
{
    return scp_help(st, dptr, uptr, flag, rom_helptext, VT40_HLP_ATTACH);
}


/* Return the ROM description */

const char *rom_description (DEVICE *dptr)
{
    return "Read-Only Memory";
}
