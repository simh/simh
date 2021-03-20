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

#include "pdp11_defs.h"
#include "pdp11_m9312.h"

/* Forward references */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_rd (int32 *data, int32 PA, int32 access);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_boot (int32 u, DEVICE *dptr);
t_stat rom_set_addr (UNIT *, int32, CONST char *, void *);
t_stat rom_show_addr (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_module (UNIT *, int32, CONST char *, void *);
t_stat rom_show_module (FILE *, UNIT *, int32, CONST void *);
t_stat rom_set_function (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rom_show_function (FILE *f, UNIT *uptr, int32 val, CONST void *desc);
t_stat rom_attach (UNIT *uptr, CONST char *cptr);
t_stat rom_detach (UNIT *uptr);
t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rom_description (DEVICE *dptr);

/*
 * ROM data structures
 *
 * The ROM device supports two types of modules:
 * 1. Blank module. This module is freely configurable with the ROM base address
 *    and image.
 * 2. M9312 module. This module has built-in ROM images on fixed adresses.
 * 
 * Every module type gets its own UNIT and DIB structures to allow switching
 * of module types. The DEVICE structure by default points to the UNITs and DIBs
 * for the blank module. When the user issues a SET ROM TYPE command, the
 * pointers are set to UNITs and DIBs of the specified module type.
 */

#define BLANK_UNIT_FLAGS	UNIT_RO | UNIT_MUSTBUF | UNIT_BUFABLE | UNIT_ATTABLE
#define M9312_UNIT_FLAGS	UNIT_RO | UNIT_FIX | UNIT_MUSTBUF | UNIT_BUFABLE
#define CONFIG_UNIT_FLAGS   (BLANK_UNIT_FLAGS | M9312_UNIT_FLAGS)

UNIT blank_rom_unit[NUM_BLANK_SOCKETS];
UNIT m9312_rom_unit[NUM_M9312_SOCKETS];

/* Use some device specific fields in the UNIT structure */
#define unit_base u3		/* Base adress of the ROM unit */
#define unit_end  u4		/* End adress of the ROM unit */
#define dib_ptr   up7		/* Pointer to the DIB for this unit */
#define usage	  up8		/* Function usage of the unit */

DIB blank_rom_dib[NUM_BLANK_SOCKETS];
DIB m9312_rom_dib[NUM_M9312_SOCKETS];


// Define the default "blank" ROM module
module blank =
{
	"BLANK",							// Module name
	NUM_BLANK_SOCKETS,					// Number of sockets (units)
	(UNIT (*)[]) &blank_rom_unit,		// Pointer to UNIT structs
	(DIB (*)[]) &blank_rom_dib,			// Pointer to DIB structs
	BLANK_UNIT_FLAGS,					// UNIT flags
	(rom_socket (*)[]) &blank_sockets	// Pointer to rom_socket structs
};

// Define the M9312 module
module m9312 =
{
	"M9312",							// Module name
	NUM_M9312_SOCKETS,					// Number of sockets (units)
	(UNIT (*)[])  &m9312_rom_unit,		// Pointer to UNIT structs
	(DIB (*)[]) &m9312_rom_dib,			// Pointer to DIB structs
	M9312_UNIT_FLAGS,					// UNIT flags
	(rom_socket (*)[]) &m9312_sockets	// Pointer to rom_socket structs
};

#define NUM_MODULES 2

// The list of available ROM modules
module *module_list[NUM_MODULES] =
{
	&blank,
	&m9312,
};

/*
 * Define the ROM device and units modifiers.
 * The modifier indicated by MODULE_MODIFIER must be the MODULE
 *  modifier as the description field of that modifier is dynamically 
 * set to the selected module.
 */
#define MODULE_MODIFIER		0

MTAB rom_mod[] = {
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 010, "MODULE", "MODULE",
		&rom_set_module, &rom_show_module, (void *) &blank, "Module type" },
	{ MTAB_XTD | MTAB_VUN | MTAB_VALR, 010, "ADDRESS", "ADDRESS",
		&rom_set_addr, &rom_show_addr, NULL, "Bus address" },
	{ MTAB_XTD | MTAB_VUN | MTAB_VALR, 010, "FUNCTION", "FUNCTION",
		&rom_set_function, &rom_show_function, NULL, "ROM Function" },
	{ 0 }
};

// Device definition
DEVICE rom_dev =
{
	"ROM",								// Device name
	blank_rom_unit,						// Pointer to device unit structures
	NULL,								// A ROM module has no registers
	rom_mod,							// Pointer to modifier table
	NUM_BLANK_SOCKETS,					// Number of units
	8,									// Address radix
	9,									// Address width
	2,									// Address increment
	8,									// Data radix
	16,									// Data width
	rom_ex,								// Examine routine
	rom_dep,							// Deposit routine
	rom_reset,							// Reset routine
	&rom_boot,							// Boot routine
	&rom_attach,						// Attach routine
	&rom_detach,						// Detach routine
	&blank_rom_dib[0],					// Pointer to device information blocks
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


/* Set ROM module type */

t_stat rom_set_module (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	// Is a module type specified? 
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the module list for the specified module type
	for (int i = 0; i < NUM_MODULES; i++)
	{
		if (strcasecmp (cptr, module_list[i]->name) == 0)
		{
			// Module type found. Set the modifier description 
			// field to the selected modifier
			rom_mod[MODULE_MODIFIER].desc = module_list[i];

			// Fill the device structure with the module-specific data
			rom_dev.numunits = module_list[i]->num_sockets;
			rom_dev.units = (UNIT*) module_list[i]->units;
			rom_dev.ctxt = module_list[i]->dibs;
			// rom_dev.reset = module_list[i]->reset;

			// Reset the device
			// (*rom_dev.reset)(&rom_dev);
			rom_reset (&rom_dev);
			return SCPE_OK;
		}
	}

	// Module type not found
	return SCPE_ARG;
}

/* Show ROM module type */

t_stat rom_show_module (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	fprintf (f, "module type %s", ((module*) desc)->name);
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


/* Deposit routine */

t_stat rom_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	uint16 *image = (uint16 *) uptr->filebuf;

	image[(addr - uptr->unit_base) >> 1] = val;
	return SCPE_OK;
}


/* ROM read routine */

t_stat rom_rd (int32 *data, int32 PA, int32 access)
{
	uint32 i;
	UNIT *uptr;

	for (i = 0, uptr = rom_dev.units; i < rom_dev.numunits; i++, uptr++)
	{
		// if (PA >= blank_rom_unit[i].unit_base && PA < blank_rom_unit[i].unit_end)
		if (PA >= uptr->unit_base && PA <= uptr->unit_end)
		{
			uint16 *image = (uint16 *) uptr->filebuf;
			*data = image[(PA - uptr->unit_base) >> 1];
			return SCPE_OK;
		}
	}

	return SCPE_NXM;
}

/*
 * Set name of the specified ROM unit. For units of fixed size, show_unit()
 * displays the unit's capacity after the unit's name.
 */
rom_set_unit_name (UNIT *uptr, int unit_number)
{
	int needed_size = snprintf (NULL, 0, (uptr->flags & UNIT_FIX) ? "ROM%d: size=" : "ROM%d: ", unit_number);
	char *buffer = malloc (needed_size);
	if (buffer != NULL) {
		sprintf (buffer, (uptr->flags & UNIT_FIX) ? "ROM%d: size=" : "ROM%d: ", unit_number);
		uptr->uname = buffer;
	}
}

/*
 * Reset function for the ROM modules.
 * The function is independ of the selected module. It is called
 * (several times) at simh start and when the user issues a RESET command.
 */
t_stat rom_reset (DEVICE *dptr)
{
	uint32 i;
	UNIT* uptr = dptr->units;
	DIB* dibptr = dptr->ctxt;
	module* modptr = (module*) rom_mod[MODULE_MODIFIER].desc;

	// Initialize the UNIT and DIB structs 
	for (i = 0; i < dptr->numunits; i++, uptr++, dptr++)
	{
		// Set the flags as specified in the module struct for the selected module
		uptr->flags = modptr->flags;

		// Set pointer to DIB for this unit
		uptr->dib_ptr = dibptr;

		// Create the linked list of DIBs
		dibptr->next = (i < dptr->numunits) ? dibptr + 1 : NULL;

		// Set the name for this unit
		rom_set_unit_name (uptr, i);
	}

	return SCPE_OK;
}



/* Boot routine */

t_stat rom_boot (int32 u, DEVICE *dptr)
{
	cpu_set_boot (blank_rom_unit[u].unit_base);
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

	// Check if the command is allowed
	if ( !(uptr->flags & UNIT_ATTABLE))
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


/* Fill the DIB for the specified unit */

t_stat rom_make_dib (UNIT *uptr)
{
	//DIB *dib = &blank_rom_dib[uptr - blank_rom_unit];
	DIB *dib = uptr->dib_ptr;

	dib->ba = uptr->unit_base;
	dib->lnt = uptr->capac;
	dib->rd = &rom_rd;
	return build_ubus_tab (&rom_dev, dib);
}


/* Set M9312 ROM function */

t_stat rom_set_function (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	int unit_number = uptr - m9312_rom_unit;

	// Is the FUNCTION modifier supported on this module type? 
	// ToDo: Find better way to discriminate module type
	if (uptr->flags & UNIT_ATTABLE)
		return SCPE_NOFNC;

	// Is function specified? 
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the rom table for the specified function
	for (rom *romptr = (rom *) m9312_sockets[unit_number].rom_list;
			romptr->image != NULL; romptr++)
	{
		if (strcasecmp (cptr, romptr->device_mnemonic) == 0)
		{
			// Set usage, image, adresses and capacity for the specified unit
			uptr->usage = romptr->device_mnemonic;
			uptr->filebuf = romptr->image;
			uptr->unit_base = m9312_sockets[unit_number].base_address;
			uptr->unit_end = m9312_sockets[unit_number].base_address +
				m9312_sockets[unit_number].size - 2;
			uptr->capac = m9312_sockets[unit_number].size;

			// Fill the DIB for this unit
			return rom_make_dib (uptr);
		}
	}

	// Mnemonic not found
	return SCPE_ARG;
}

/* Show M9312 ROM function */

t_stat rom_show_function (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	if (uptr == NULL)
		return SCPE_IERR;

	if (uptr->flags & UNIT_ATTABLE)
		fprintf (f, "function not supported");
	else 
		fprintf (f, "function=%s", (uptr->usage)? uptr->usage : "none");
	return SCPE_OK;
}


/* Attach an image to a socket in the BLANK module */

t_stat rom_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat r;

	// Check the unit is attachable
	if (!(uptr->flags & UNIT_ATTABLE))
	return SCPE_NOATT;

	// Check the unit is attached
	if (uptr->flags & UNIT_ATT)
		return SCPE_ALATT;

	// Check the ROM base address is set
	if (uptr->unit_base == 0)
		return sim_messagef (SCPE_ARG, "Set address first\n");

	// Set quiet mode
	// ToDo: Find out use of this switch
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
	r = rom_make_dib (uptr);
	if (r != SCPE_OK)
		return rom_detach (uptr);

	// Set end adress 
	uptr->unit_end = uptr->unit_base + uptr->capac;
	return SCPE_OK;
}

/* Detach */

t_stat rom_detach (UNIT *uptr)
{
	t_stat r;
	DIB *dib = &blank_rom_dib[uptr - blank_rom_unit];

	dib->rd = NULL;
	r = build_ubus_tab (&rom_dev, dib);
	if (r != SCPE_OK)
		return r;
	uptr->unit_end = uptr->unit_base;
	dib->lnt = uptr->capac = 0;
	return detach_unit (uptr);
}


/* Print help */

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
	fprintf (st, "ROM, Read-Only Memory\n\n");
	fprintf (st, "The ROM device can be used to add ROM modules to the I/O page. Two module\n");
	fprintf (st, "types are available, the BLANK and the M9312 module. The contents\n");
	fprintf (st, "of the BLANK ROM module have to specified by setting the ROM's base address\n");
	fprintf (st, "and ROM image. The contents of the M9312 ROM's are built in and can be set\n");
	fprintf (st, "by specifying its function.\n\n");
	fprintf (st, "For the BLANK module first the ROM unit ADDRESS has to be set, and then\n");
	fprintf (st, "the ATTACH command can be used to fill the ROM with contents.\n\n");
	fprintf (st, "The M9312 has five ROM sockets available, ROM0 is used for a Diagnostics/Console Emulator ROM,\n");
	fprintf (st, "ROMs 1-4 are used for boot ROMs for specific devices. The function of the ROMs\n");
	fprintf (st, "is specified by means of the FUNCTION modifier. The command 'SET ROM0 FUNCTION=B0'\n");
	fprintf (st, "for example, puts the ROM B0 in socket 0.\n\n");
	fprintf (st, "Available ROMs for socket 0 are A0, B0, UBI and MEM, available ROMs for\n");
	fprintf (st, "sockets 1-4 are identified by their device mnemonic.\n");
	fprintf (st, "The BOOT command is supported for starting from the ROM.\n");
	return SCPE_OK;
}


/* Print attach command help */

t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
	fprintf (st, "The ATTACH command is only available for the BLANK ROM module and is used to specify\n");
	fprintf (st, "the contents of a ROM unit. The file contents must be a flat binary image.\n");
	fprintf (st, "The unit ADDRESS must be set first.\n");
	return SCPE_OK;
}


/* Return the ROM description */

const char *rom_description (DEVICE *dptr)
{
	return "Read-Only Memory";
}
