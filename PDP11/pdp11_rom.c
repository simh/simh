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
t_stat rom_set_type (UNIT *, int32, CONST char *, void *);
t_stat rom_show_type (FILE *, UNIT *, int32, CONST void *);
t_stat rom_attach (UNIT *uptr, CONST char *cptr);
t_stat rom_detach (UNIT *uptr);
t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rom_description (DEVICE *dptr);

/* ROM data structures

   rom_dev       ROM device descriptor
   rom_unit      ROM unit list
*/

#define unit_base u3
#define unit_end u4

#define ROM_UNITS 4
#define ROM_UNIT_FLAGS UNIT_RO|UNIT_MUSTBUF|UNIT_BUFABLE|UNIT_ATTABLE

DIB rom_dib[ROM_UNITS];

MTAB rom_mod[] = {
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 010, "TYPE", "TYPE",
		&rom_set_type, &rom_show_type, NULL, "Module type" },
	{ MTAB_XTD | MTAB_VUN | MTAB_VALR, 010, "ADDRESS", "ADDRESS",
		&rom_set_addr, &rom_show_addr, NULL, "Bus address" },
	{ 0 }
};

UNIT rom_unit[ROM_UNITS];

// Device definition
DEVICE rom_dev =
{
	"ROM",								// Device name
	rom_unit,							// Pointer to device unit structures
	NULL,								// A ROM module has no registers
	rom_mod,							// Pointer to modifier table
	ROM_UNITS,							// Number of units
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

#define NUM_MODULES 1

module *module_list[NUM_MODULES] =
{
	&m9312,
};

module *selected_module = &m9312;

/* Set ROM module type */

t_stat rom_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	// Is a module type specified? 
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the module list for the specified module type
	for (int i = 0; i < NUM_MODULES; i++)
	{
		if (strcasecmp (cptr, module_list[i]->name) == 0)
		{
			// Module type found
			selected_module = module_list[i];
			return SCPE_OK;
		}
	}

	// Module type not found
	return SCPE_ARG;
}

/* Show ROM module type */

t_stat rom_show_type (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	fprintf (f, "ROM module type %s", selected_module->name);
	return SCPE_OK;
}

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

t_stat rom_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	uint16 *image = (uint16 *) uptr->filebuf;

	image[(addr - uptr->unit_base) >> 1] = val;
	return SCPE_OK;
}

t_stat rom_rd (int32 *data, int32 PA, int32 access)
{
	int i;
	for (i = 0; i < ROM_UNITS; i++) {
		if (PA >= rom_unit[i].unit_base && PA < rom_unit[i].unit_end) {
			uint16 *image = (uint16 *) rom_unit[i].filebuf;
			*data = image[(PA - rom_unit[i].unit_base) >> 1];
			return SCPE_OK;
		}
	}
	return SCPE_NXM;
}

t_stat rom_reset (DEVICE *dptr)
{
	int i;
	dptr->ctxt = &rom_dib[0];
	for (i = 0; i < ROM_UNITS; i++) {
		rom_unit[i].flags |= ROM_UNIT_FLAGS;
		rom_dib[i].next = &rom_dib[i + 1];
	}
	rom_dib[ROM_UNITS - 1].next = NULL;
	return SCPE_OK;
}

t_stat rom_boot (int32 u, DEVICE *dptr)
{
	cpu_set_boot (rom_unit[u].unit_base);
	return SCPE_OK;
}

t_stat rom_set_addr (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	int32 addr;
	t_stat r;

	if (uptr->flags & UNIT_ATT)
		return SCPE_ALATT;
	if (cptr == NULL)
		return SCPE_ARG;
	addr = (int32) get_uint (cptr, 8, IOPAGEBASE + IOPAGEMASK, &r);
	if (r != SCPE_OK)
		return r;
	if (addr < IOPAGEBASE)
		return sim_messagef (SCPE_ARG, "ROM must be in I/O page, at or above 0%o\n",
		IOPAGEBASE);
	uptr->unit_base = uptr->unit_end = addr;
	return SCPE_OK;
}

t_stat rom_show_addr (FILE *f, UNIT *uptr, int32 val, CONST void *desc)
{
	if (uptr == NULL)
		return SCPE_IERR;
	if (uptr->unit_base != uptr->unit_end)
		fprintf (f, "address=%o-%o", uptr->unit_base, uptr->unit_end - 1);
	else
		fprintf (f, "address=%o", uptr->unit_base);
	return SCPE_OK;
}

t_stat rom_make_dib (UNIT *uptr)
{
	DIB *dib = &rom_dib[uptr - rom_unit];

	dib->ba = uptr->unit_base;
	dib->lnt = uptr->capac;
	dib->rd = &rom_rd;
	return build_ubus_tab (&rom_dev, dib);
}

t_stat rom_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat r;

	if (uptr->flags & UNIT_ATT)
		return SCPE_ALATT;
	if (uptr->unit_base == 0)
		return sim_messagef (SCPE_ARG, "Set address first.\n");
	sim_switches |= SWMASK ('Q');
	uptr->capac = sim_fsize_name (cptr);
	if (uptr->capac == 0)
		return SCPE_OPENERR;
	r = attach_unit (uptr, cptr);
	if (r != SCPE_OK)
		return r;
	r = rom_make_dib (uptr);
	if (r != SCPE_OK)
		return rom_detach (uptr);
	uptr->unit_end = uptr->unit_base + uptr->capac;
	return SCPE_OK;
}

/* Detach */

t_stat rom_detach (UNIT *uptr)
{
	t_stat r;
	DIB *dib = &rom_dib[uptr - rom_unit];

	dib->rd = NULL;
	r = build_ubus_tab (&rom_dev, dib);
	if (r != SCPE_OK)
		return r;
	uptr->unit_end = uptr->unit_base;
	dib->lnt = uptr->capac = 0;
	return detach_unit (uptr);
}

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
	fprintf (st, "ROM, Read-Only Memory\n\n");
	fprintf (st, "The ROM device can be used to add ROM modules to the I/O page.\n");
	fprintf (st, "First the ROM unit ADDRESS has to be set, and then the ATTACH command\n");
	fprintf (st, "can be used to fill the ROM with contents.  The BOOT command is supported\n");
	fprintf (st, "for starting from the ROM.\n");
	return SCPE_OK;
}

t_stat rom_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
	fprintf (st, "The ATTACH command is used to specify the contents of a ROM unit.\n");
	fprintf (st, "Any file can be used.  The file contents must be a flat binary image.\n");
	fprintf (st, "The unit ADDRESS must be set first.\n");
	return SCPE_OK;
}

const char *rom_description (DEVICE *dptr)
{
	return "Read-Only Memory";
}
