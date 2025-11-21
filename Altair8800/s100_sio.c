/* s100_sio.c: MITS Altair 8800 Generic SIO

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
#include "s100_sio.h"

static int32 poc = TRUE;       /* Power On Clear */

#define SIO_TYPE_CUST       0
#define SIO_TYPE_2502       1
#define SIO_TYPE_2651       2
#define SIO_TYPE_6850       3
#define SIO_TYPE_8250       4
#define SIO_TYPE_8251       5
#define SIO_TYPE_NONE       0xff

static SIO sio_types[] = {
/*       TYPE         NAME     DESC        BASE  STAT  DATA  RDRE  RDRF  TDRE  TDRF */
    { SIO_TYPE_CUST, "CUST",  "CUSTOM",    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { SIO_TYPE_2502, "2502",  "2502 UART", 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x08 },
    { SIO_TYPE_2651, "2651",  "2651 UART", 0x00, 0x01, 0x00, 0xc0, 0xc2, 0xc1, 0xc0 },
    { SIO_TYPE_6850, "6850",  "6850 ACIA", 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02 },
    { SIO_TYPE_8250, "8250",  "8250 UART", 0x00, 0x05, 0x00, 0x00, 0x01, 0x60, 0x00 },
    { SIO_TYPE_8251, "8251",  "8251 UART", 0x00, 0x01, 0x00, 0x80, 0x82, 0x85, 0x80 },
    { SIO_TYPE_NONE, "NONE",  "NONE"     , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static SIO_BOARD sio_boards[] = {
    /* MITS 88-SIO */
    { SIO_TYPE_2502, "SIO",  "MITS 88-SIO",               0x00 },

    /* CompuPro System Support 1 */
    { SIO_TYPE_2651, "SS1",  "CompuPro System Support 1", 0x5c },

    /* No type selected */
    { SIO_TYPE_NONE, "NONE", "NONE",                      0x00 }
};

static SIO sio;   /* Active SIO configuration */

static int32 sio_type = SIO_TYPE_NONE;

static int32 sio_rdr;    /* Receive Data Register            */
static int32 sio_rdre;   /* Receive Data Register Empty Flag */
static int32 sio_tdre;   /* Transmit Buffer Full Empty       */

static t_stat sio_reset(DEVICE *dptr);
static int32 sio_io(const int32 addr, const int32 rw, const int32 data);
static int32 sio_io_in(const int32 addr);
static void sio_io_out(const int32 addr, int32 data);
static t_stat sio_set_board(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat sio_set_type (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat sio_set_val(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat sio_set_console(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat sio_show_config(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat sio_show_list(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat sio_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static const char* sio_description(DEVICE *dptr) {
    return "Generic Serial IO";
}

static UNIT sio_unit = {
    UDATA (NULL, 0, 0)
};

static REG sio_reg[] = {
    { HRDATAD (TYPE, sio_type, 8, "SIO Board Type") },
    { HRDATAD (RDR,  sio_rdr,  8, "Receive Data Register") },
    { HRDATAD (RDRE, sio_rdre, 1, "Receive Data Register Empty") },
    { HRDATAD (TDRE, sio_tdre, 1, "Transmit Data Register Empty") },
    { NULL }
};

static MTAB sio_mod[] = {
    { UNIT_SIO_VERBOSE,     UNIT_SIO_VERBOSE, "VERBOSE", "VERBOSE", NULL, NULL,
        NULL, "Enable verbose messages"  },
    { UNIT_SIO_VERBOSE,     0,               "QUIET",   "QUIET",   NULL, NULL,
        NULL, "Disable verbose messages" },

    { MTAB_XTD | MTAB_VUN,  UNIT_SIO_CONSOLE, NULL, "CONSOLE",   &sio_set_console, NULL, NULL, "Set as CONSOLE" },
    { MTAB_XTD | MTAB_VUN,  0,                NULL, "NOCONSOLE", &sio_set_console, NULL, NULL, "Remove as CONSOLE" },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "CONFIG",  NULL, NULL, &sio_show_config, NULL, "Show SIO configuration" },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO,  0, "LIST",    NULL, NULL, &sio_show_list,   NULL, "Show available types and boards" },

    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_2502, NULL, "2502={base}",  &sio_set_type,  NULL, NULL, "Configure SIO for 2502 at base" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_2651, NULL, "2651={base}",  &sio_set_type,  NULL, NULL, "Configure SIO for 2651 at base" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_6850, NULL, "6850={base}",  &sio_set_type,  NULL, NULL, "Configure SIO for 6850 at base" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_8250, NULL, "8250={base}",  &sio_set_type,  NULL, NULL, "Configure SIO for 8250 at base" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_8251, NULL, "8251={base}",  &sio_set_type,  NULL, NULL, "Configure SIO for 8251 at base" },
    { MTAB_XTD | MTAB_VDV            , SIO_TYPE_NONE, NULL, "NONE",         &sio_set_type,  NULL, NULL, "No type selected" },

    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0,             NULL, "BOARD={name}", &sio_set_board, NULL, NULL, "Configure SIO for name" },

    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 1,             NULL, "IOBASE={base}", &sio_set_val,  NULL, NULL,  "Set BASE I/O Address" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 2,             NULL, "STAT={offset}", &sio_set_val,  NULL, NULL,  "Set STAT I/O Offset" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 3,             NULL, "DATA={offset}", &sio_set_val,  NULL, NULL,  "Set DATA I/O Offset" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 4,             NULL, "RDRE={mask}",   &sio_set_val,  NULL, NULL,  "Set RDRE Mask" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 5,             NULL, "RDRF={mask}",   &sio_set_val,  NULL, NULL,  "Set RDRF Mask" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 6,             NULL, "TDRE={mask}",   &sio_set_val,  NULL, NULL,  "Set TDRE Mask" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 7,             NULL, "TDRF={mask}",   &sio_set_val,  NULL, NULL,  "Set TDRF Mask" },

    { 0 }
};

/* Debug Flags */
#define STATUS_MSG        (1 << 0)
#define IN_MSG            (1 << 1)
#define OUT_MSG           (1 << 2)

/* Debug Flags */
static DEBTAB sio_dt[] = {
    { "STATUS",   STATUS_MSG,    "Status messages"   },
    { "IN",       IN_MSG,        "IN operations"     },
    { "OUT",      OUT_MSG,       "OUT operations"    },
    { NULL, 0                                        }
};

#define SIO_SNAME "SIO"

DEVICE sio_dev = {
    SIO_SNAME, &sio_unit, sio_reg, sio_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &sio_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    sio_dt, NULL, NULL, &sio_show_help, NULL, NULL, &sio_description
};

static t_stat sio_reset(DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        if (sio_type != SIO_TYPE_NONE) {
            s100_bus_remio(sio.stat, 1, &sio_io);
            s100_bus_remio(sio.data, 1, &sio_io);

            s100_bus_noconsole(&dptr->units[0]);
        }

        poc = TRUE;

        return SCPE_OK;
    }

    /* Device is enabled */
    if (poc) {
        /* Set board type */
        sio_set_type(NULL, sio_type, NULL, NULL);

        poc = FALSE;
    }

    /* Set as CONSOLE unit  */
    if (dptr->units[0].flags & UNIT_SIO_CONSOLE) {
        s100_bus_console(&dptr->units[0]);
    }

    sio_rdre = TRUE;
    sio_tdre = TRUE;

    sim_debug(STATUS_MSG, dptr, "reset adapter.\n");

    return SCPE_OK;
}

static int32 sio_io(const int32 addr, const int32 rw, const int32 data)
{
    int32 c;

    if (sio_rdre) {                           /* If the receive data register is empty and this */
        c = s100_bus_poll_kbd(&sio_unit);     /* is the CONSOLE, check for keyboard input       */

        if (c & SCPE_KFLAG) {
            sio_rdre = FALSE;
            sio_rdr = c & DATAMASK;
        }
    }

    if (rw == S100_IO_READ) {
        return sio_io_in(addr);
    }

    sio_io_out(addr, data);

    return 0x0ff;
}

static int32 sio_io_in(const int32 addr)
{
    sim_debug(IN_MSG, &sio_dev, ADDRESS_FORMAT " Port %02X.\n", s100_bus_get_addr(), addr & DATAMASK);

    if (addr == sio.base + sio.stat) {
        return ((sio_rdre) ? sio.rdre : sio.rdrf) | ((sio_tdre) ? sio.tdre : sio.tdrf);
    }
    else if (addr == sio.base + sio.data) {
        sio_rdre = TRUE;    /* Clear RDF status bit */

        return sio_rdr;     /* return byte */
    }

    return 0xff;
}

static void sio_io_out(const int32 addr, int32 data)
{
    sim_debug(OUT_MSG, &sio_dev, ADDRESS_FORMAT " Port %02X.\n", s100_bus_get_addr(), addr & DATAMASK);

    if (addr == sio.base + sio.data) {
        sim_putchar(data & DATAMASK);

        sio_tdre = TRUE;    /* Transmit buffer is always empty */
    }
}

static t_stat sio_set_type(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    int32 result, base;

    if (value == sio_type) {
        return SCPE_OK;
    }

    if (sio_type != SIO_TYPE_NONE) {
        s100_bus_remio(sio.base + sio.stat, 1, &sio_io);
        s100_bus_remio(sio.base + sio.data, 1, &sio_io);
    }

    sio_type = value;

    if (sio_type != SIO_TYPE_NONE) {
        sio.type = sio_type;
        sio.name = sio_types[sio_type].name;
        sio.desc = sio_types[sio_type].desc;
        sio.base = sio_types[sio_type].base;
        sio.stat = sio_types[sio_type].stat;
        sio.data = sio_types[sio_type].data;
        sio.rdre = sio_types[sio_type].rdre;
        sio.rdrf = sio_types[sio_type].rdrf;
        sio.tdre = sio_types[sio_type].tdre;
        sio.tdrf = sio_types[sio_type].tdrf;

        if (cptr != NULL) {
            result = sscanf(cptr, "%x", &base);

            if (result == 1) {
                sio.base = base & DATAMASK;
            }
        }

        s100_bus_addio(sio.base + sio.stat, 1, &sio_io, SIO_SNAME"S");
        s100_bus_addio(sio.base + sio.data, 1, &sio_io, SIO_SNAME"D");
    }

    return SCPE_OK;
}

static t_stat sio_set_board(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    char cbuf[10];
    int i = 0;

    if (cptr == NULL) {
        return SCPE_ARG;
    }

    do {
        if (sim_strcasecmp(cptr, sio_boards[i].name) == 0) {
            sprintf(cbuf, "%04X", sio_boards[i].base);
            return sio_set_type(uptr, sio_boards[i].type, cbuf, NULL);
        }
    } while (sio_boards[i++].type != SIO_TYPE_NONE);

    return SCPE_ARG;
}

static t_stat sio_set_val(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    uint32 val;

    if (cptr == NULL || sscanf(cptr, "%02x", &val) == 0) {
        return SCPE_ARG;
    }

    val &= DATAMASK;

    switch (value) {
        case 1:
            sio.base = val;
            break;

        case 2:
            sio.stat = val;
            break;

        case 3:
            sio.data = val;
            break;

        case 4:
            sio.rdre = val;
            break;

        case 5:
            sio.rdrf = val;
            break;

        case 6:
            sio.tdre = val;
            break;

        case 7:
            sio.tdrf = val;
            break;

        default:
            return SCPE_ARG;
    }

    sio.name = sio_types[SIO_TYPE_CUST].name;
    sio.desc = sio_types[SIO_TYPE_CUST].desc;
    sio.type = SIO_TYPE_CUST;
    sio_type = SIO_TYPE_CUST;

    return SCPE_OK;
}

static t_stat sio_set_console(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    if (value == UNIT_SIO_CONSOLE) {
        s100_bus_console(uptr);
    }
    else {
        s100_bus_noconsole(uptr);
    }

    return SCPE_OK;
}

static t_stat sio_show_config(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (sio_type != SIO_TYPE_NONE) {
        sim_printf("SIO Base Address:    %02X\n\n", sio.base);
        sim_printf("SIO Status Register: %02X\n", sio.base + sio.stat);
        sim_printf("SIO Data Register:   %02X\n", sio.base + sio.data);

        sim_printf("SIO RDRE Mask:       %02X\n", sio.rdre);
        sim_printf("SIO RDRF Mask:       %02X\n\n", sio.rdrf);

        sim_printf("SIO TDRE Mask:       %02X\n", sio.tdre);
        sim_printf("SIO TDRF Mask:       %02X\n\n", sio.tdrf);

        sim_printf("%sCONSOLE\n", (uptr->flags & UNIT_SIO_CONSOLE) ? "" : "NO");
    }
    else {
        sim_printf("\n\tNot configured.\n");
    }

    return SCPE_OK;
}

static t_stat sio_show_list(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int i;

    sim_printf("\nAvailable types:\n");

    i = 0;

    do {
        if (sio_types[i].type != SIO_TYPE_CUST) {
            sim_printf("%-8.8s %s\n", sio_types[i].name, sio_types[i].desc);
        }
    } while (sio_types[i++].type != SIO_TYPE_NONE);

    sim_printf("\nAvailable boards:\n");

    i = 0;

    do {
        sim_printf("%-8.8s %s\n", sio_boards[i].name, sio_boards[i].desc);
    } while (sio_boards[i++].type != SIO_TYPE_NONE);

    return SCPE_OK;
}

static t_stat sio_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 Generic SIO Device (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    fprintf(st, "\n");
    fprintf(st, "----- NOTES -----\n\n");
    fprintf(st, "Only one device may poll the host keyboard for CONSOLE input.\n");
    fprintf(st, "Use SET %s CONSOLE to select this UNIT as the CONSOLE device.\n", sim_dname(dptr));
    fprintf(st, "\nUse SHOW BUS CONSOLE to display the current CONSOLE device.\n\n");

    return SCPE_OK;
}

