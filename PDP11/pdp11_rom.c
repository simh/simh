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

#include "pdp11_defs.h"
#include "pdp11_m9312.h"
#include "pdp11_vt40boot.h"

/* Forward references */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_wr (int32 data, int32 PA, int32 access);
t_stat rom_rd (int32 *data, int32 PA, int32 access);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_boot (int32 u, DEVICE *dptr);
t_stat rom_set_addr (UNIT *, int32, CONST char *, void *);
t_stat rom_show_addr (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_module (UNIT *, int32, CONST char *, void *);
t_stat rom_show_module (FILE *, UNIT *, int32, CONST void *);
t_stat rom_attach (UNIT *uptr, CONST char *cptr);
t_stat rom_detach (UNIT *uptr);
t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rom_description (DEVICE *dptr);

/* External references */
extern uint32 cpu_type;
extern uint32 cpu_opt;
extern int32 HITMISS;

/* Static definitions */
static uint32 cpu_type_on_selection;		/* cpu_type for which module type was selected */

/*
 * ROM data structures
 *
 * The ROM device is described by means of the following data structures:
 * 1. A list of modules. The ROM device supports three modules:
 *	  a) BLANK module. This module is freely configurable with the ROM base address
 *    and image.
 *	  b) M9312 module. This module has built-in ROM images on fixed adresses is available
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

#define ROM_UNIT_FLAGS	UNIT_RO | UNIT_MUSTBUF | UNIT_BUFABLE | UNIT_ATTABLE
#define QBUS_MODEL		(1u << 0)
#define UNIBUS_MODEL	(1u << 1)

 // Define the default "blank" ROM module
module blank =
{
	"BLANK",							// Module name
	ROM_FILE,							// Module type
	CPUT_ALL,							// Required CPU types
	QBUS_MODEL | UNIBUS_MODEL,			// Required CPU options
	NUM_BLANK_SOCKETS,					// Number of sockets (units)
	ROM_UNIT_FLAGS,						// UNIT flags
	(rom_socket (*)[]) & blank_sockets	// Pointer to rom_socket structs
};

// Define the M9312 module
module m9312 =
{
	"M9312",							// Module name
	ROM_BUILTIN,						// Module type
	CPUT_ALL,							// Required CPU types
	UNIBUS_MODEL,						// Required CPU options
	NUM_M9312_SOCKETS,					// Number of sockets (units)
	ROM_UNIT_FLAGS,						// UNIT flags
	(rom_socket (*)[]) & m9312_sockets	// Pointer to rom_socket structs
};

// Define the VT40 module
module vt40 =
{
	"VT40",								// Module name
	ROM_BUILTIN,						// Module type
	CPUT_05,							// Required CPU types
	UNIBUS_MODEL,						// Required CPU options
	NUM_VT40_SOCKETS,					// Number of sockets (units)
	ROM_UNIT_FLAGS,						// UNIT flags
	(rom_socket (*)[]) & vt40_sockets	// Pointer to rom_socket structs
};

/*
 * Define the number of modules. The BLANK module must be the
 * module in the list.
 */
#define NUM_MODULES			3
#define ROM_MODULE_BLANK	0

// The list of available ROM modules
module* module_list[NUM_MODULES] =
{
	&blank,
	&m9312,
	&vt40,
};


/*
 * Use some device specific fields in the UNIT structure.
 * 
 * The u5 (selected_module) field is just used to indicate the selected
 * module. It would be more appropriate to use a field in the DEVICE structure 
 * for that purpose, but there is no (device-specific) field in that
 * structure that is saved and restored.
 */
#define unit_base			u3			/* Base adress of the ROM unit */
#define unit_end			u4			/* End adress of the ROM unit */
#define selected_module		u5			/* Index of module in module_list */

/*
 * The maximum number of sockets is the number of sockets any module can have.
 * For modules with a number of units less than this maximum the surplus
 * units are disabled.
 */ 
#define MAX_NUMBER_SOCKETS	5

/*
 * Define and initialize the UNIT structs. 
 */

#define ROM_UNIT_INIT(flags,module) NULL,NULL,NULL,NULL,NULL,0,0,(flags),0,0,0,NULL,0,0,0,0,0,module

UNIT rom_unit[MAX_NUMBER_SOCKETS] =
{
	{ ROM_UNIT_INIT (ROM_UNIT_FLAGS, ROM_MODULE_BLANK) },
	{ ROM_UNIT_INIT (ROM_UNIT_FLAGS, ROM_MODULE_BLANK) },
	{ ROM_UNIT_INIT (ROM_UNIT_FLAGS, ROM_MODULE_BLANK) },
	{ ROM_UNIT_INIT (ROM_UNIT_FLAGS, ROM_MODULE_BLANK) },
	{ ROM_UNIT_INIT (ROM_UNIT_FLAGS | UNIT_DIS, ROM_MODULE_BLANK) },
};

DIB rom_dib[MAX_NUMBER_SOCKETS];

/*
 * Define the ROM device and units modifiers.
 */
MTAB rom_mod[] = {
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 010, "MODULE", "MODULE",
		&rom_set_module, &rom_show_module, NULL, "Module type" },
	{ MTAB_XTD | MTAB_VUN | MTAB_VALR, 010, "ADDRESS", "ADDRESS",
		&rom_set_addr, &rom_show_addr, NULL, "Bus address" },
	{ 0 }
};

// Device definition
DEVICE rom_dev =
{
	"ROM",								// Device name
	rom_unit,							// Pointer to device unit structures
	NULL,								// A ROM module has no registers
	rom_mod,							// Pointer to modifier table
	MAX_NUMBER_SOCKETS,					// Number of units
	8,									// Address radix
	9,									// Address width
	2,									// Address increment
	8,									// Data radix
	16,									// Data width
	rom_ex,								// Examine routine
	NULL,								// Deposit routine not available
	rom_reset,							// Reset routine
	&rom_boot,							// Boot routine
	&rom_attach,						// Attach routine
	&rom_detach,						// Detach routine
	&rom_dib[0],						// Pointer to device information blocks
	DEV_DISABLE | DEV_UBUS | DEV_QBUS,	// Flags
	0,									// Debug control
	NULL,								// Debug flags
	NULL,								// Memory size routine
	NULL,								// Logical name
	&rom_help,							// Help routine
	&rom_help_attach,					// Help attach routine
	NULL,								// Context for help routines
	&rom_description	                // Description routine
};

/*
 * Check if the module type to be set is valid on the selected 
 * cpu_opt and cpu_type
 */
int module_type_is_valid (int module_number)
{
	uint32 bus = UNIBUS ? UNIBUS_MODEL : QBUS_MODEL;

	return CPUT (module_list[module_number]->valid_cpu_types) &&
		bus & module_list[module_number]->valid_cpu_opts;
}

/* Set ROM module type */

t_stat rom_set_module (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
	uint32 unit_number;

	// Is a module type specified? 
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the module list for the specified module type
	for (int module_number = 0; module_number < NUM_MODULES; module_number++)
	{
		if (strcasecmp (cptr, module_list[module_number]->name) == 0)
		{
			// Check if the module is allowed on this cpu and bus type
			if (!module_type_is_valid (module_number))
					return SCPE_INVSW;

			// Save current cpu type for reference in rom_reset()
			cpu_type_on_selection = cpu_type;

			// Module type found
			// Initialize the UNITs with values for this module
			for (unit_number = 0, uptr = &rom_unit[0]; unit_number < MAX_NUMBER_SOCKETS; unit_number++, uptr++)
			{
				// Check if the selected module differs from the
				// currently selected module
				if (uptr->selected_module != module_number)
				{
					// Set the currently selected module
					uptr->selected_module = module_number;

					// Check if an image is attached 
					if (uptr->flags & UNIT_ATT)
					{
						// Detach the unit
						if (rom_detach (uptr) != SCPE_OK)
							return SCPE_IERR;
					}

					// Clear addressses and function and initialize flags
					uptr->unit_base = 0;
					uptr->unit_end = 0;
					uptr->flags = module_list[module_number]->flags;

					// Disable surplus ROMs for this module
					if (unit_number >= module_list[module_number]->num_sockets)
						uptr->flags |= UNIT_DIS;
				}
			}

			// If this module has just one unit and that unit has just one possible
			// image attach the image to the unit
			if (module_list[module_number]->num_sockets == 1)
			{
				int num_roms = 0;
				rom_socket* socketptr = *module_list[module_number]->sockets;
				
				// Count the number of ROMS for this socket
				for (rom* romptr = (rom*) socketptr->rom_list; romptr->image != NULL; romptr++)
					num_roms++;

				if (num_roms == 1)
					// Attach the first image to the first unit
					rom_attach (&rom_unit[0], ((rom*) socketptr->rom_list)->device_mnemonic);
			}
			return SCPE_OK;
		}
	}

	// Module type not found
	return SCPE_ARG;
}


/* Show ROM module type */

t_stat rom_show_module (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	// rom_repair_flags ();
	int selected_module = rom_unit[0].selected_module;
	fprintf (f, "module type %s", (module*) module_list[selected_module]->name);
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


/* 
 * ROM write routine 
 * The sole purpose of this function to return a meaningful error message
 * for a write operation to a ROM device. The standard error message "Address
 * space exceeded" is inappropriate as the ROM addresses are within the
 * address space. An SCPE_RO error would be a better fit but that error code
 * results in an unclear message ("Read only argument"). We therefore chose to
 * an I/O error. An attempt to write to a ROM device would yield that result.
 */
t_stat rom_wr (int32 data, int32 PA, int32 access)
{
	return SCPE_IOERR;
}


/*
 * ROM read routine 
 * 
 * As the iodispR[] entries for the address range are not nullified when the
 * when build_ubus_tab() is called with a nullified pointer to the read function,
 * this function can be called while the image is detached. Therefore we have to
 * check if the read access is valid.
 */
t_stat rom_rd (int32 *data, int32 PA, int32 access)
{
	uint32 i;
	UNIT *uptr;

	for (i = 0, uptr = rom_dev.units; i < rom_dev.numunits; i++, uptr++)
	{
		if (PA >= uptr->unit_base && PA <= uptr->unit_end && (uptr->flags & UNIT_ATT))
		{
			uint16 *image = (uint16 *) uptr->filebuf;
			*data = image[(PA - uptr->unit_base) >> 1];
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

	// Check if the CPU opt and/or type has been changed since the module
	// type was selected. If so, select the BLANK module.
	if (cpu_type != cpu_type_on_selection)
		rom_set_module (&rom_unit[0], 0, "BLANK", NULL);

	// Initialize the UNIT and DIB structs and create the linked list of DIBs
	for (unit_number = 0; unit_number < MAX_NUMBER_SOCKETS; unit_number++)
		rom_dib[unit_number].next = (unit_number < dptr->numunits - 1) ? &rom_dib[unit_number + 1] : NULL;

	return SCPE_OK;
}


/* Boot routine */

t_stat rom_boot (int32 u, DEVICE *dptr)
{
	cpu_set_boot (rom_unit[u].unit_base);
	return SCPE_OK;
}


/* 
 * Set ROM base address
 * This operation is only allowed on module types to which an image
 * can be attached, i.e. the BLANK ROM module.
 */
t_stat rom_set_addr (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	int32 addr;
	t_stat r;
	int32 selected_module = uptr->selected_module;

	// Check if the command is allowed for the selected module
	if (module_list[selected_module]->type != ROM_FILE)
		return SCPE_NOFNC;

	// Check if the unit is not already attached
	if (uptr->flags & UNIT_ATT)
		return SCPE_ALATT;

	// Check if an address is specified
	if (cptr == NULL)
		return SCPE_ARG;

	// Convert the adress string and check if produced a valid value
	addr = (int32) get_uint (cptr, 8, IOPAGEBASE + IOPAGEMASK, &r);
	if (r != SCPE_OK)
		return r;

	// Check if a valid adress is specified
	if (addr < IOPAGEBASE)
		return sim_messagef (SCPE_ARG, "ROM must be in I/O page, at or above 0%o\n",
		IOPAGEBASE);

	// Set the base adress
	uptr->unit_base = uptr->unit_end = addr;
	return SCPE_OK;
}


/* Show ROM base address */

t_stat rom_show_addr (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	// Check that we got a valid unit pointer
	if (uptr == NULL)
		return SCPE_IERR;

	// If the unit has an address range print the range, otherwise print
	// just the base address
	if (uptr->unit_base != uptr->unit_end)
		fprintf (f, "address=%o-%o", uptr->unit_base, uptr->unit_end - 1);
	else
		fprintf (f, "address=%o", uptr->unit_base);
	return SCPE_OK;
}


/* (Re)set the DIB and build the Unibus table for the specified unit */

t_stat reset_dib (UNIT *uptr, t_stat (reader (int32*, int32, int32)),
	t_stat (writer (int32, int32, int32)))
{
	DIB *dib = &rom_dib[uptr - rom_unit];

	dib->ba = uptr->unit_base;
	dib->lnt = uptr->capac;
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
	// int unit_number = uptr - rom_unit;
	int module_number = uptr->selected_module;
	int unit_number = uptr - rom_unit;
	t_stat r;

	switch (module_list[module_number]->type)
	{
		case ROM_FILE:
			// Check the ROM base address is set
			if (uptr->unit_base == 0)
				return sim_messagef (SCPE_ARG, "Set address first\n");

			// Set quiet mode to avoid a "buffering file in memory" message
			sim_switches |= SWMASK ('Q');

			// Check and set unit capacity
			uptr->capac = sim_fsize_name (cptr);
			if (uptr->capac == 0)
				return SCPE_OPENERR;

			// Attach unit and check the result
			r = attach_unit (uptr, cptr);
			if (r != SCPE_OK)
				return r;

			// Fill the DIB for the unit
			r = reset_dib (uptr, &rom_rd, &rom_wr);
			if (r != SCPE_OK)
				return rom_detach (uptr);

			// Set end adress 
			uptr->unit_end = uptr->unit_base + uptr->capac;
			return SCPE_OK;

		case ROM_BUILTIN:
			// Is function specified? 
			if (cptr == NULL)
				return SCPE_ARG;

			// Get a pointer to the selected module and from that a pointer to
			// socket for the unit
			module* modptr = *(module_list + uptr->selected_module);
			rom_socket* socketptr = *modptr->sockets + unit_number;
		
			// Search the list of ROMs for this socket for the specified image
			for (rom* romptr = (rom*) socketptr->rom_list; romptr->image != NULL; romptr++)
			{
				if (strcasecmp (cptr, romptr->device_mnemonic) == 0)
				{
					// Set image, adresses and capacity for the specified unit
					// The filename string is stored in an allocated buffer as detach_unit()
					// wants to free the filename.
					uptr->filename = strdup (romptr->device_mnemonic);
					uptr->filebuf = romptr->image;
					uptr->unit_base = socketptr->base_address;
					uptr->unit_end = socketptr->base_address + socketptr->size;
					uptr->capac = socketptr->size;
					uptr->flags |= UNIT_ATT;

					// Execute rom specific function if available
					if (romptr->rom_attached != NULL)
						(*romptr->rom_attached)();

					// Fill the DIB for this unit
					return reset_dib (uptr, &rom_rd, &rom_wr);
				}
			}

			// Mnemonic not found
			return SCPE_ARG;

		default:
			return SCPE_IERR;
	}	
}

/*
 * Detach file or built in image from unit.
 * Note that although the pointer to the read function in dib->rd is 
 * nullified, build_ubus_tab() does not clear the iodispR[] entries.
 */
t_stat rom_detach (UNIT *uptr)
{
	t_stat r;
	DIB *dib = &rom_dib[uptr - rom_unit];
	int selected_module = uptr->selected_module;

	// Leave address intact for modules with separate address
	// and image specification (i.e. the BLANK module type).
	if (module_list[selected_module]->type == ROM_FILE)
		uptr->unit_end = uptr->unit_base;
	else
		uptr->unit_end = uptr->unit_base = 0;

	r = reset_dib (uptr, NULL, NULL);
	if (r != SCPE_OK)
		return r;

	return detach_unit (uptr);
}


/* Print help */

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
	fprintf (st, "ROM, Read-Only Memory\n\n");
	fprintf (st, "The ROM device can be used to add ROM modules to the I/O page. Three module\n");
	fprintf (st, "types are available, the BLANK, the M9312 and the VT40 module. The module\n");
	fprintf (st, "is selected by means of the MODULE modifier.\n");
	fprintf (st, "The contents of the BLANK ROM module have to be specified by setting the ROM's\n");
	fprintf (st, "base address and ROM image. First the ROM unit ADDRESS has to be set, and then\n");
	fprintf (st, "the ATTACH command can be used to fill the ROM with contents.\n\n");
	fprintf (st, "The contents of the M9312 ROM's are built in and can be set by specifying its\n");
	fprintf (st, "function. The M9312 has five ROM sockets available, ROM0 is used for a\n");
	fprintf (st, "Diagnostics/Console Emulator ROM and ROMs 1-4 are used for boot ROMs for specific\n");
	fprintf (st, "devices. The ATTACH command is used to specify the function of the ROM. The command\n");
	fprintf (st, "'ATTACH ROM0 B0' for example puts the ROM B0 in socket 0.\n");
	fprintf (st, "Available ROMs for socket 0 are A0, B0, UBI and MEM, available ROMs for\n");
	fprintf (st, "sockets 1-4 are identified by their device mnemonic.\n\n");
	fprintf (st, "The VT40 module is meant for the GT-40 graphic terminal, based on a PDP-11/05. The VT40\n");
	fprintf (st, "included a bootstrap ROM. The module has just one socket with one available ROM and a\n");
	fprintf (st, "'SET ROM MODULE=VT40' command suffices to select this boot ROM.\n");
	fprintf (st, "The BOOT command is supported for starting from the ROM.\n");
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
