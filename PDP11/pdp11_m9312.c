/*
   pdp11_m9312.c: M9312 Diagnostics/Console emulator and boot proms

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
*/

#include "pdp11_defs.h"
#include "pdp_m9312.h"

t_stat m9312_ex (t_value* vptr, t_addr addr, UNIT* uptr, int32 sw);
t_stat m9312_rd (int32* data, int32 PA, int32 access);
t_stat m9312_reset (DEVICE* dptr);
t_stat m9312_boot (int32 u, DEVICE* dptr);
t_stat m9312_set_rom0 (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat m9312_set_rom2_4 (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat m9312_show_rom (FILE* f, UNIT* uptr, int32 val, CONST void* desc);
t_stat m9312_help (FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr);
t_stat m9312_help_attach (FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr);
const char* m9312_description (DEVICE* dptr);


/* ROM data structures
 * 
 * A M9312 comprises five ROM positions. The first position is used for a
 * model-specific diagnostics/console emulator ROM. This ROM has as base
 * address 17765000. The other four positions are for device-specific boot
 * ROMS. As a PDP-11 can have more than four bootable device types, the M9312
 * has to be configured for the specific machine.
 * 
 * Every ROM is mapped to a M9312 unit as the five ROMS have different base
 * addresses and size.
 */
#define M9312_UNITS 5
#define M9312_UNIT_FLAGS	UNIT_RO | UNIT_FIX | UNIT_MUSTBUF | UNIT_BUFABLE


/* Define base adress and size of the ROMS */
 
struct m9312_rom
{
	t_addr	base_address;	// ROM base adress
	int16	size;			// ROM size
} m9312_memory_map[M9312_UNITS] =
{
	{017765000, 512},		// ROM 0
	{017773000, 128},		// ROM 1
	{017773200, 128},		// ROM 2
	{017773400, 128},		// ROM 3
	{017773600, 128},		// ROM 4
};

/* Define use of device specific fields in struct UNIT */
#define base_addr		u3
#define top_addr		u4
#define dev_mnemonic	u5


// Define Device information blocks 
DIB m9312_dib[M9312_UNITS];

// Define modifiers for the device
MTAB m9312_mod[] =
{
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, NULL, "ROM0",
		&m9312_set_rom0, &m9312_show_rom, NULL, "ROM 0 Function" },
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 1, NULL, "ROM1",
		&m9312_set_rom2_4, &m9312_show_rom, NULL, "ROM 1 Function" },
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 2, NULL, "ROM2",
		&m9312_set_rom2_4, &m9312_show_rom, NULL, "ROM 2 Function" },
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 3, NULL, "ROM3",
		&m9312_set_rom2_4, &m9312_show_rom, NULL, "ROM 3 Function" },
	{ MTAB_XTD | MTAB_VDV | MTAB_VALR, 4, NULL, "ROM4",
		&m9312_set_rom2_4, &m9312_show_rom, NULL, "ROM 4 Function" },
	{ 0 }
};

// Define unit structures
UNIT m9312_unit[M9312_UNITS];

// Define unit names (pointed to by uname)
char unit_name[M9312_UNITS][20] =
{
	"ROM0 (EMPTY) ",
	"ROM1 (EMPTY) ",
	"ROM2 (EMPTY) ",
	"ROM3 (EMPTY) ",
	"ROM4 (EMPTY) ",
};

// Device definition
DEVICE m9312_dev =
{
	"M9312",							// Device name
	m9312_unit,							// Pointer to device unit structures
	NULL,								// The M9312 board has no registers
	m9312_mod,							// Pointer to modifier table
	M9312_UNITS,						// Number of units
	8,									// Address radix
	9,									// Address width
	2,									// Address increment
	8,									// Data radix
	16,									// Data width
	m9312_ex,							// Examine routine
	NULL,								// No deposit routine available
	m9312_reset,						// Reset routine
	NULL,								// The device is not bootable
	NULL,								// No attach routine available
	NULL,								// No detach routine available
	&m9312_dib[0],						// Pointer to device information blocks
	DEV_DISABLE | DEV_UBUS | DEV_QBUS,	// Flags
	0,									// Debug control
	NULL,								// Debug flags
	NULL,								// Memory size routine
	NULL,								// Logical name
	&m9312_help,						// Help routine
	&m9312_help_attach,					// Help attach routine
	NULL,								// Context for help routines
	&m9312_description                  // Description routine
};


/* m9312_ex - Examine the data at the specified address */

t_stat m9312_ex (t_value* vptr, t_addr addr, UNIT* uptr, int32 sw)
{
	int32 data;
	t_stat r;

	r = m9312_rd (&data, addr, 0);
	if (r != SCPE_OK)
		return r;
	*vptr = (t_value) data;
	return SCPE_OK;
}


/* m9312_rd - Read the data at the specified physical address  */

t_stat m9312_rd (int32* data, int32 PA, int32 access)
{
	int i;
	for (i = 0; i < M9312_UNITS; i++)
	{
		// Check if a ROM is present in the device and if it is, if the specified
		// adress is within the adress range of the ROM.
		if (m9312_unit[i].filebuf != NULL && 
			PA >= m9312_unit[i].base_addr && PA <= m9312_unit[i].top_addr)
		{
			uint16* image = (uint16*) m9312_unit[i].filebuf;
			*data = image[(PA - m9312_unit[i].base_addr) >> 1];
			return SCPE_OK;
		}
	}
	return SCPE_NXM;
}


/* m9312_reset - Initialize the units and device information blocks */

t_stat m9312_reset (DEVICE* dptr)
{
	int i;
	dptr->ctxt = &m9312_dib[0];

	for (i = 0; i < M9312_UNITS; i++)
	{
		m9312_unit[i].flags |= M9312_UNIT_FLAGS;
		m9312_unit[i].base_addr = m9312_memory_map[i].base_address;
		m9312_unit[i].top_addr = m9312_memory_map[i].base_address +
			m9312_memory_map[i].size - 2;
		m9312_unit[i].capac = m9312_memory_map[i].size;
		m9312_unit[i].uname = unit_name[i];
		// m9312_unit[i].filebuf = (void*) NULL;

		// From m9312_make_dib
		m9312_dib[i].ba = m9312_memory_map[i].base_address;
		m9312_dib[i].lnt = m9312_memory_map[i].size;
		m9312_dib[i].rd = &m9312_rd;
		m9312_dib[i].next = &m9312_dib[i + 1];
		build_ubus_tab (&m9312_dev, &m9312_dib[i]);
	}
	m9312_dib[M9312_UNITS - 1].next = NULL;
	return SCPE_OK;
}


/* m9312_set_rom0 - Set the function for ROM 0 */

t_stat m9312_set_rom0 (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
	int i;

	/* Is function specified? */
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the console rom tables for the specified device mnemonic
	for (i = 0; i < sizeof (console_roms) / sizeof (struct rom_tab); i++)
	{
		if (strcmp (cptr, console_roms[i].device_mnemonic) == 0)
		{
			m9312_unit[val].filebuf = console_roms[i].image;
			m9312_unit[val].dev_mnemonic = console_roms[i].device_mnemonic;
			snprintf (unit_name[val], sizeof (unit_name[val]), "ROM%d (%s) ", val, console_roms[i].device_mnemonic);
			return SCPE_OK;
		}
	}

	// Mnemonic not found
	return SCPE_ARG;
}

/* m9312_set_rom2_4 - Set the function for ROMs 2 to 4 */

t_stat m9312_set_rom2_4 (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
	int i;

	/* Is function specified? */
	if (cptr == NULL)
		return SCPE_ARG;

	// Search the boot rom tables for the specified mnemonic
	for (i = 0; i < sizeof (boot_roms) / sizeof (struct rom_tab); i++)
	{
		if (strcmp (cptr, boot_roms[i].device_mnemonic) == 0)
		{
			m9312_unit[val].filebuf = boot_roms[i].image;
			m9312_unit[val].dev_mnemonic = boot_roms[i].device_mnemonic;
			snprintf (unit_name[val], sizeof (unit_name[val]), "ROM%d (%s)", val, console_roms[i].device_mnemonic);
			return SCPE_OK;
		}
	}

	// Mnemonic not found
	return SCPE_ARG;
}

t_stat m9312_show_rom (FILE* f, UNIT* uptr, int32 val, CONST void* desc)
{
	if (uptr == NULL)
		return SCPE_IERR;

	// fprintf (f, "function: %s", uptr->dev_mnemonic);

	return SCPE_OK;
}

t_stat m9312_help (FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr)
{
	fprintf (st, "M9312, Diagnostics/Console emulator and bootstrap ROMS.\n\n");
	fprintf (st, "The M9312 has four boot ROMS available to boot from a specific device type.\n");
	fprintf (st, "The ROM contents can be selected by means of modifiers.\n");
	return SCPE_OK;
}

t_stat m9312_help_attach (FILE* st, DEVICE* dptr, UNIT* uptr, int32 flag, const char* cptr)
{
	fprintf (st, "The ATTACH command is not supported.\n");
	return SCPE_OK;
}

const char* m9312_description (DEVICE* dptr)
{
	return "M9312, Diagnostics/Console emulator and bootstrap ROMS";
}
