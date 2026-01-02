/* s100_bram.c: MITS Altair 8800 Banked RAM

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

#include "s100_bus.h"
#include "s100_bram.h"

static t_stat bram_reset             (DEVICE *dptr);
static t_stat bram_dep               (t_value val, t_addr addr, UNIT *uptr, int32 sw);
static t_stat bram_ex                (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
static int32 bram_io                 (const int32 addr, const int32 rw, const int32 data);
static int32 bram_memio              (const int32 addr, const int32 rw, const int32 data);
static t_stat bram_set_banks         (int32 banks);
static t_stat bram_clear_command     (UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat bram_enable_command    (UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat bram_randomize_command (UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat bram_banks_command     (UNIT *uptr, int32 value, const char *cptr, void *desc);
static void bram_addio               (int32 type);
static void bram_remio               (int32 type);
static t_stat bram_set_type          (int32 type);
static t_stat bram_type_command      (UNIT *uptr, int32 value, const char *cptr, void *desc);
static void bram_clear               (void);
static void bram_randomize           (void);
static t_stat bram_show_help         (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char* bram_description  (DEVICE *dptr);

static void PutBYTE(register uint32 Addr, const register uint32 Value);
static uint32 GetBYTE(register uint32 Addr);

static int32 poc = TRUE; /* Power On Clear */

static int32 *M = NULL;   /* RAM */
static int32 bram_banks = 0;
static int32 bram_bank = 0;
static int32 bram_type = BRAM_TYPE_NONE;

static BRAM B[BRAM_TYPE_MAX + 1] = {
    { 0x00, 0, 0,       "NONE" },
    { 0xff, 1, 8,       "ERAM" },
    { 0x40, 1, 8,       "VRAM" },
    { 0x40, 1, 7,       "CRAM" },
    { 0xc0, 1, MAXBANK, "HRAM" },
    { 0x40, 1, MAXBANK, "B810" },
};

#define DEV_NAME "BRAM"

static const char* bram_description(DEVICE *dptr) {
    return "Banked Random Access Memory";
}

static UNIT bram_unit = {
    UDATA (NULL, UNIT_FIX | UNIT_BINK, MAXBANKSIZE)
};

static REG bram_reg[] = {
    { FLDATAD (POC,     poc,            0x01,         "Power on Clear flag"), },
    { HRDATAD (BANK,    bram_bank,      MAXBANKS2LOG, "Selected bank"), },
    { DRDATAD (BANKS,   bram_banks,     8,            "Number of banks"), },
    { DRDATAD (TYPE,    bram_type,      8,            "RAM type"), },
    { NULL }
};

static MTAB bram_mod[] = {
    { UNIT_BRAM_VERBOSE,     UNIT_BRAM_VERBOSE,   "VERBOSE",    "VERBOSE",      NULL, NULL,
        NULL, "Enable verbose messages"  },
    { UNIT_BRAM_VERBOSE,     0,                   "QUIET",      "QUIET",        NULL, NULL,
        NULL, "Disable verbose messages"                },

    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_B810      , NULL,           "B810",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to Digital Design B810"  },
    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_CRAM      , NULL,           "CRAM",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to Cromemco"  },
    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_ERAM      , NULL,           "ERAM",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to SD Systems ExpandoRAM"  },
    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_HRAM      , NULL,           "HRAM",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to NorthStar"  },
    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_VRAM      , NULL,           "VRAM",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to Vector"  },
    { MTAB_XTD | MTAB_VDV,  BRAM_TYPE_NONE      , NULL,           "NONE",         &bram_type_command,
        NULL, NULL, "Sets the RAM type to NONE"  },

    { MTAB_XTD | MTAB_VDV | MTAB_VALR,  0,      NULL, "BANKS={1-16}",                     &bram_banks_command,
        NULL, NULL, "Sets the RAM size" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR,  1,      NULL, "ADDPAGE={PAGE | START-END | ALL}", &bram_enable_command,
        NULL, NULL, "Enable RAM page(s)" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR,  0,      NULL, "REMPAGE={PAGE | START-END | ALL}", &bram_enable_command,
        NULL, NULL, "Disable RAM page(s)" },
    { MTAB_VDV,             0,                  NULL, "CLEAR",                            &bram_clear_command,
        NULL, NULL, "Sets RAM to 0x00"  },
    { MTAB_VDV,             0,                  NULL, "RANDOMIZE",                        &bram_randomize_command,
        NULL, NULL, "Sets RAM to random values"  },
    { 0 }
};

/* Debug Flags */
static DEBTAB bram_dt[] = {
    { NULL, 0 }
};

DEVICE bram_dev = {
    DEV_NAME,                  /* name */
    &bram_unit,                /* units */
    bram_reg,                  /* registers */
    bram_mod,                  /* modifiers */
    1,                         /* # units */
    ADDRRADIX,                 /* address radix */
    ADDRWIDTH,                 /* address width */
    1,                         /* addr increment */
    DATARADIX,                 /* data radix */
    DATAWIDTH,                 /* data width */
    &bram_ex,                  /* examine routine */
    &bram_dep,                 /* deposit routine */
    &bram_reset,               /* reset routine */
    NULL,                      /* boot routine */
    NULL,                      /* attach routine */
    NULL,                      /* detach routine */
    NULL,                      /* context */
    (DEV_DISABLE | DEV_DIS),   /* flags */
    0,                         /* debug control */
    bram_dt,                   /* debug flags */
    NULL,                      /* mem size routine */
    NULL,                      /* logical name */
    &bram_show_help,           /* help */
    NULL,                      /* attach help */
    NULL,                      /* context available to help routines */
    &bram_description          /* device description */
};

static t_stat bram_reset(DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        bram_set_type(BRAM_TYPE_NONE);

        poc = TRUE;
    }
    else {
        if (poc) {
            poc = FALSE;
        }
        else {
            bram_bank = 0;
        }
    }

    return SCPE_OK;
}

/* memory examine */
static t_stat bram_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    *vptr = GetBYTE(addr & ADDRMASK) & DATAMASK;

    return SCPE_OK;
}

/* memory deposit */
static t_stat bram_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    PutBYTE(addr & ADDRMASK, val & DATAMASK);
 
    return SCPE_OK;
}

static int32 bram_io(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_WRITE) {

        switch (bram_type) {
            case BRAM_TYPE_HRAM:
                if (data >= 0 && data < B[bram_type].banks) {
                    bram_bank = data;
                } else {
                    sim_printf("Invalid bank select 0x%02x for %s\n", data, B[bram_type].name);
                }
                break;

            case BRAM_TYPE_B810:
                if (data >= 0 && data < B[bram_type].banks) {
                    bram_bank = data;
                } else {
                    sim_printf("Invalid bank select 0x%02x for %s\n", data, B[bram_type].name);
                }
                break;

            case BRAM_TYPE_ERAM:
                if (data >= 0 && data < B[bram_type].banks) {
                    bram_bank = data;
                    if (bram_unit.flags & UNIT_BRAM_VERBOSE) {
                        sim_printf("%s selecting bank %d\n", B[bram_type].name, data);
                    }
                } else {
                    sim_printf("Invalid bank select 0x%02x for %s\n", data, B[bram_type].name);
                }
                break;

            case BRAM_TYPE_VRAM:
                switch(data & 0xFF) {
                    case 0x01:
                    case 0x41:      // OASIS uses this for some reason? */
                        bram_bank = 0;
                        break;
                    case 0x02:
                    case 0x42:      // OASIS uses this for some reason? */
                        bram_bank = 1;
                        break;
                    case 0x04:
                        bram_bank = 2;
                        break;
                    case 0x08:
                        bram_bank = 3;
                        break;
                    case 0x10:
                        bram_bank = 4;
                        break;
                    case 0x20:
                        bram_bank = 5;
                        break;
                    case 0x40:
                        bram_bank = 6;
                        break;
                    case 0x80:
                        bram_bank = 7;
                        break;
                    default:
                        sim_printf("Invalid bank select 0x%02x for %s\n", data, B[bram_type].name);
                        break;
                }
                break;

            case BRAM_TYPE_CRAM:
                switch(data & 0x7F) {
                    case 0x01:
                        bram_bank = 0;
                        break;
                    case 0x02:
                        bram_bank = 1;
                        break;
                    case 0x04:
                        bram_bank = 2;
                        break;
                    case 0x08:
                        bram_bank = 3;
                        break;
                    case 0x10:
                        bram_bank = 4;
                        break;
                    case 0x20:
                        bram_bank = 5;
                        break;
                    case 0x40:
                        bram_bank = 6;
                        break;
                    default:
                        sim_printf("Invalid bank select 0x%02x for %s\n", data, B[bram_type].name);
                        break;
                }
                break;

            default:
                break;
        }
    }

    return DATAMASK;
}

static int32 bram_memio(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_READ) {
        return GetBYTE(addr);
    }

    PutBYTE(addr, data);

    return DATAMASK;
}

static uint32 GetBYTE(register uint32 Addr)
{
    t_addr bankAddr;

    if (M != NULL) {
        Addr &= ADDRMASK;

        bankAddr = Addr + (bram_bank * MAXBANKSIZE);

        return M[bankAddr] & DATAMASK;
    }

    return DATAMASK;
}

static void PutBYTE(register uint32 Addr, const register uint32 Value)
{
    t_addr bankAddr;

    if (M != NULL) {
        Addr &= ADDRMASK;

        bankAddr = Addr + (bram_bank * MAXBANKSIZE);

        M[bankAddr] = Value & DATAMASK;
    }
}

static void bram_addio(int32 type)
{
    if (type > BRAM_TYPE_NONE && type <= BRAM_TYPE_MAX) {
        if (B[type].size) {
            s100_bus_addio_out(B[type].baseport, B[type].size, &bram_io, B[type].name);
        }
    }
}

static void bram_remio(int32 type)
{
    if (type > BRAM_TYPE_NONE && type <= BRAM_TYPE_MAX) {
        s100_bus_remio_out(B[type].baseport, B[type].size, &bram_io);
    }
}

static t_stat bram_set_type(int32 type)
{
    if (bram_type == type) {  /* No change */
        return SCPE_OK;
    }

    bram_remio(bram_type);    /* Changing type - remove previous IO */

    bram_type = type;
    bram_bank = 0;

    bram_set_banks(B[bram_type].banks);
    bram_addio(bram_type);

    return SCPE_OK;
}

static t_stat bram_type_command(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    return bram_set_type(value);
}

static t_stat bram_set_banks(int32 banks) {
    if (banks > 0 && banks <= MAXBANK) {
        M = realloc(M, banks * MAXBANKSIZE);
    }
    else if (M != NULL) {
        free(M);

        M = NULL;

        s100_bus_remmem(0x0000, MAXBANKSIZE, &bram_memio);         /* Remove enabled pages */
    }

    bram_banks = banks;

    return SCPE_OK;
}

static t_stat bram_banks_command(UNIT *uptr, int32 value, const char *cptr, void *desc) {
    int32 result, banks;

    if (cptr == NULL) {
        sim_printf("Banks must be provided as SET %s BANKS=1-%d\n", DEV_NAME, MAXBANK);
        return SCPE_ARG | SCPE_NOMESSAGE;
    }

    result = sscanf(cptr, "%i", &banks);

    if (result == 1 && banks && banks <= MAXBANK) {
        return bram_set_banks(banks);
    }

    return SCPE_ARG | SCPE_NOMESSAGE;
}

static t_stat bram_enable_command(UNIT *uptr, int32 value, const char *cptr, void *desc) {
    int32 size;
    t_addr start, end;

    if (cptr == NULL) {
        sim_printf("Memory page(s) must be provided as SET %s [ADD|REM]PAGE=E0-EF\n", DEV_NAME);
        return SCPE_ARG | SCPE_NOMESSAGE;
    }

    if (get_range(NULL, cptr, &start, &end, 16, PAGEMASK, 0) == NULL) {
        return SCPE_ARG;
    }

    if (start < MAXPAGE) {
        start = start << LOG2PAGESIZE;
    }
    if (end < MAXPAGE) {
        end = end << LOG2PAGESIZE;
    }

    start &= 0xff00;
    end &= 0xff00;

    size = end - start + PAGESIZE;

    if (value) {
        s100_bus_addmem(start, size, &bram_memio, DEV_NAME); /* Add pages */
    }
    else {
        s100_bus_remmem(start, size, &bram_memio);         /* Remove pages */
    }

    return SCPE_OK;
}

static t_stat bram_clear_command(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    bram_clear();

    return SCPE_OK;
}

static t_stat bram_randomize_command(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    bram_randomize();

    return SCPE_OK;
}

static void bram_clear()
{
    int32 i;

    for (i = 0; i < MAXBANKSIZE; i++) {
        M[i] = 0;
    }
}

static void bram_randomize()
{
    int32 i;

    for (i = 0; i < bram_banks * MAXBANKSIZE; i++) {
        if (M != NULL) {
            M[i] = sim_rand() & DATAMASK;
        }
    }
}

static t_stat bram_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 Banked RAM (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

