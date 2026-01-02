/* s100_rom.c: MITS Altair 8800 ROM

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   07-Nov-2025 Initial version

*/

#include "sim_defs.h"
#include "s100_bus.h"
#include "s100_ram.h"
#include "s100_rom.h"
#include "s100_roms.h"

static t_stat rom_reset             (DEVICE *dptr);
static int32 rom_memio              (const int32 addr, const int32 rw, const int32 data);

static int32 poc = TRUE; /* Power On Clear */

static const char* rom_description  (DEVICE *dptr);

static uint32 GetBYTE(register uint32 Addr);

static t_stat rom_enadis(int32 value, int32 ena);
static t_stat rom_ena(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat rom_dis_dbl(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat rom_dis_hdsk(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat rom_dis_altmon(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat rom_dis_turmon(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat rom_show_list(FILE *st, UNIT *uptr, int32 val, const void *desc);
static t_stat rom_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static int32 M[MAXBANKSIZE];

static ROM rom_table[] = {
    { UNIT_ROM_ALTMON, rom_altmon,   ROM_ALTMON_BASEADDR,    ROM_ALTMON_SIZE,    ROM_ALTMON_NAME,    ROM_ALTMON_DESC },
    { UNIT_ROM_DBL,    rom_mits_dbl, ROM_MITS_DBL_BASEADDR,  ROM_MITS_DBL_SIZE,  ROM_MITS_DBL_NAME,  ROM_MITS_DBL_DESC },
    { UNIT_ROM_HDSK,   rom_mits_hdsk, ROM_MITS_HDSK_BASEADDR,  ROM_MITS_HDSK_SIZE,  ROM_MITS_HDSK_NAME,  ROM_MITS_HDSK_DESC },
    { UNIT_ROM_TURMON, rom_mits_turmon, ROM_MITS_TURMON_BASEADDR, ROM_MITS_TURMON_SIZE, ROM_MITS_TURMON_NAME, ROM_MITS_TURMON_DESC },
    { UNIT_ROM_AZ80DBL,    rom_az80_dbl, ROM_AZ80_DBL_BASEADDR,  ROM_AZ80_DBL_SIZE,  ROM_AZ80_DBL_NAME,  ROM_AZ80_DBL_DESC },

    { 0, NULL, 0x0000, 0, "", "" }
};

static const char* rom_description(DEVICE *dptr) {
    return "Read Only Memory";
}

static UNIT rom_unit = {
    UDATA (NULL, UNIT_FIX | UNIT_BINK | UNIT_ROM_DBL, MAXBANKSIZE)
};

static REG rom_reg[] = {
    { FLDATAD (POC,     poc,       0x01,         "Power on Clear flag"), },
    { NULL }
};

static MTAB rom_mod[] = {
    { UNIT_ROM_VERBOSE,     UNIT_ROM_VERBOSE,   "VERBOSE",                 "VERBOSE",           NULL, NULL,
        NULL, "Enable verbose messages"    },
    { UNIT_ROM_VERBOSE,     0,                  "QUIET",                   "QUIET",             NULL, NULL,
        NULL, "Disable verbose messages"   },

    { UNIT_ROM_DBL,         UNIT_ROM_DBL,       ROM_MITS_DBL_NAME,      ROM_MITS_DBL_NAME,      &rom_ena,  NULL,
        NULL, "Enable "  ROM_MITS_DBL_DESC },
    { UNIT_ROM_DBL,         0,                  "NO" ROM_MITS_DBL_NAME, "NO" ROM_MITS_DBL_NAME, &rom_dis_dbl,  NULL,
        NULL, "Disable " ROM_MITS_DBL_DESC },

    { UNIT_ROM_AZ80DBL,         UNIT_ROM_AZ80DBL,       ROM_AZ80_DBL_NAME,      ROM_AZ80_DBL_NAME,      &rom_ena,  NULL,
        NULL, "Enable "  ROM_AZ80_DBL_DESC },
    { UNIT_ROM_AZ80DBL,         0,                  "NO" ROM_AZ80_DBL_NAME, "NO" ROM_AZ80_DBL_NAME, &rom_dis_dbl,  NULL,
        NULL, "Disable " ROM_AZ80_DBL_DESC },

    { UNIT_ROM_HDSK,        UNIT_ROM_HDSK,      ROM_MITS_HDSK_NAME,      ROM_MITS_HDSK_NAME,      &rom_ena,  NULL,
        NULL, "Enable "  ROM_MITS_HDSK_DESC },
    { UNIT_ROM_HDSK,         0,                  "NO" ROM_MITS_HDSK_NAME, "NO" ROM_MITS_HDSK_NAME, &rom_dis_hdsk,  NULL,
        NULL, "Disable " ROM_MITS_HDSK_DESC },

    { UNIT_ROM_ALTMON,      UNIT_ROM_ALTMON,    ROM_ALTMON_NAME,        ROM_ALTMON_NAME,        &rom_ena,  NULL,
        NULL, "Enable "  ROM_ALTMON_DESC   },
    { UNIT_ROM_ALTMON,      0,                  "NO" ROM_ALTMON_NAME,   "NO" ROM_ALTMON_NAME,   &rom_dis_altmon,  NULL,
        NULL, "Disable " ROM_ALTMON_DESC   },

    { UNIT_ROM_TURMON,      UNIT_ROM_TURMON,    ROM_MITS_TURMON_NAME,        ROM_MITS_TURMON_NAME,        &rom_ena,  NULL,
        NULL, "Enable "  ROM_MITS_TURMON_DESC   },
    { UNIT_ROM_TURMON,      0,                  "NO" ROM_MITS_TURMON_NAME,   "NO" ROM_MITS_TURMON_NAME,   &rom_dis_turmon,  NULL,
        NULL, "Disable " ROM_MITS_TURMON_DESC   },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "LIST",    NULL, NULL, &rom_show_list,   NULL, "Show available ROMs" },

    { 0 }
};

/* Debug Flags */
static DEBTAB rom_dt[] = {
    { NULL, 0 }
};

DEVICE rom_dev = {
    "ROM", &rom_unit, rom_reg, rom_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &rom_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE), 0,
    rom_dt, NULL, NULL,
    &rom_show_help, NULL, NULL,
    &rom_description
};

static t_stat rom_reset(DEVICE *dptr) {
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        rom_enadis(rom_unit.flags, FALSE);

        poc = TRUE;
    }
    else {
        if (poc) {
            rom_enadis(rom_unit.flags, TRUE);

            poc = FALSE;
        }
    }

    return SCPE_OK;
}

static int32 rom_memio(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_READ) {
        return GetBYTE(addr);
    }

    return 0x0ff;
}

uint32 GetBYTE(register uint32 Addr)
{
    return M[Addr & ADDRMASK]; /* ROM */
}

static t_stat rom_enadis(int32 value, int32 ena)
{
    ROM *r = rom_table;
    int i;

    while (r->flag != 0) {
        if (value & r->flag) {
            if (ena) {
                for (i = 0; i < r->size; i++) {
                    M[r->baseaddr + i] = r->rom[i];
                }

                s100_bus_addmem(r->baseaddr, r->size, &rom_memio, r->name);

                if (rom_unit.flags & UNIT_ROM_VERBOSE) {
                    sim_printf("Installed ROM %s @ %04X\n", r->name, r->baseaddr);
                }
            }
            else {
                s100_bus_remmem(r->baseaddr, r->size, &rom_memio);

                if (rom_unit.flags & UNIT_ROM_VERBOSE) {
                    sim_printf("Removed ROM %s @ %04X\n", r->name, r->baseaddr);
                }
            }
        }

        r++;
    }

    return SCPE_OK;
}

static t_stat rom_ena(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return rom_enadis(value, TRUE);
}

static t_stat rom_dis_dbl(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return rom_enadis(UNIT_ROM_DBL, FALSE);
}

static t_stat rom_dis_hdsk(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return rom_enadis(UNIT_ROM_HDSK, FALSE);
}

static t_stat rom_dis_turmon(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return rom_enadis(UNIT_ROM_TURMON, FALSE);
}

static t_stat rom_dis_altmon(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return rom_enadis(UNIT_ROM_ALTMON, FALSE);
}

static t_stat rom_show_list(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    ROM *r = rom_table;

    fprintf(st, "\n");

    while (r->rom != NULL) {
        fprintf(st, "%c %-8.8s: %-25.25s @ %04X-%04X\n", 
            rom_unit.flags & r->flag ? '*' : ' ', r->name, r->desc, r->baseaddr, r->baseaddr + r->size - 1);
        r++;
    }

    fprintf(st, "\n* = enabled\n");

    return SCPE_OK;
}

static t_stat rom_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 ROM (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    fprintf(st, "\nVarious ROMs are available through the ROM device. A list of ROMs is available using\n");
    fprintf(st, "the SHOW ROM LIST command. To enable a ROM, enter SET ROM <name>. To disable a ROM,\n");
    fprintf(st, "enter SET ROM NO<name>. Enabled ROMs can be seen with the SHOW BUS CONFIG command.\n\n");

    return SCPE_OK;
}

