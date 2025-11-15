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

#define SIO_TYPE_MITS_SIO   0
#define SIO_TYPE_MITS_2SIO  1
#define SIO_TYPE_CCS_UART   2
#define SIO_TYPE_CCS_USART  3
#define SIO_TYPE_NONE       0xff

static SIO sio_board[] = {
/*       NAME            BASE  STAT  DATA  RXM   RXB   TXM   TXB  */
    { "MITS SIO",        0x00, 0x00, 0x01, 0x01, 0x01, 0x08, 0x08 },
    { "MITS 2SIO",       0x10, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02 },
    { "CCS 2S+2P UART",  0x80, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02 },
    { "CCS 2S+2P USART", 0x80, 0x02, 0x03, 0x01, 0x01, 0x02, 0x02 }
};

static SIO sio;   /* Active SIO configuration */

static int32 sio_type = SIO_TYPE_NONE;

static t_stat sio_reset    (DEVICE *dptr);
static int32 sio_io        (const int32 addr, const int32 rw, const int32 data);
static int32 sio_io_in     (const int32 addr);
static void sio_io_out     (const int32 addr, int32 data);
static t_stat sio_set_board(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat sio_show_board(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

static const char* sio_description(DEVICE *dptr) {
    return "Generic Serial IO";
}

/*
 * u3 = status
 * u4 = input data
 */
static UNIT sio_unit = {
    UDATA (NULL, 0, 0)
};

static REG sio_reg[] = {
    { HRDATAD (TYPE, sio_type, 8, "SIO Board Type") },
    { NULL }
};

static MTAB sio_mod[] = {
    { UNIT_SIO_VERBOSE,     UNIT_SIO_VERBOSE, "VERBOSE", "VERBOSE", NULL, &sio_show_board,
        NULL, "Enable verbose messages"  },
    { UNIT_SIO_VERBOSE,     0,               "QUIET",   "QUIET",   NULL, &sio_show_board,
        NULL, "Disable verbose messages" },

    { UNIT_SIO_CONSOLE,     UNIT_SIO_CONSOLE, NULL, "CONSOLE", NULL, NULL,
        NULL, "Enable keyboard input"  },
    { UNIT_SIO_CONSOLE,     0,               NULL,   "NOCONSOLE",   NULL, NULL,
        NULL, "Disable keyboard input" },

    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_MITS_SIO,  NULL, "MSIO={base}",     &sio_set_board, NULL, NULL, "Configure for MITS SIO" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_MITS_2SIO, NULL, "M2SIO={base}",    &sio_set_board, NULL, NULL, "Configure for MITS SIO" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_CCS_UART,   NULL, "CCSUART={base}", &sio_set_board, NULL, NULL, "Configure for CCS 2S+2P UART" },
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, SIO_TYPE_CCS_USART,  NULL, "CCSUSART={base}", &sio_set_board, NULL, NULL, "Configure for CCS 2S+2P USART" },

    { 0 }
};

/* Debug Flags */
static DEBTAB sio_dt[] = {
    { NULL, 0 }
};

#define SIO_SNAME "SIO"

DEVICE sio_dev = {
    SIO_SNAME, &sio_unit, sio_reg, sio_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sio_reset,
    NULL, NULL, NULL,
    NULL, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    sio_dt, NULL, NULL, NULL, NULL, NULL, &sio_description
};

static t_stat sio_reset(DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        if (sio_type != SIO_TYPE_NONE) {
            s100_bus_remio(sio.status, 1, &sio_io);
            s100_bus_remio(sio.data, 1, &sio_io);
        }

        poc = TRUE;
    }
    else {
        if (poc) {
            /* Set board type */
            sio_set_board(NULL, sio_type, NULL, NULL);

            poc = FALSE;
        }

        sio_unit.u3 = sio.tbe_bit;
    }

    return SCPE_OK;
}

static int32 sio_io(const int32 addr, const int32 rw, const int32 data)
{
    int32 c;

    if ((sio_unit.u3 & sio.rdf_mask) != sio.rdf_bit) { /* If the receive buffer is empty */
        c = sim_poll_kbd();                            /* check for keyboard input       */

        if (c & SCPE_KFLAG) {
            sio_unit.u3 |= sio.rdf_bit;
            sio_unit.u4 = c & DATAMASK;
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
    if (addr == sio.base + sio.status) {
        return sio_unit.u3;
    }
    else if (addr == sio.base + sio.data) {
        sio_unit.u3 &= ~sio.rdf_mask;   /* Clear RDF status bit */

        return sio_unit.u4;             /* return byte */
    }

    return 0xff;
}

static void sio_io_out(const int32 addr, int32 data)
{
    if (addr == sio.base + sio.data) {
        sim_putchar(data & DATAMASK);

        sio_unit.u3 |= sio.tbe_bit;    /* Transmit buffer is always empty */
    }
}

static t_stat sio_set_board(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    int32 result, base;

    if (value == sio_type) {
        return SCPE_OK;
    }

    if (sio_type != SIO_TYPE_NONE) {
        s100_bus_remio(sio.base + sio.status, 1, &sio_io);
        s100_bus_remio(sio.base + sio.data, 1, &sio_io);
    }

    sio_type = value;

    if (sio_type != SIO_TYPE_NONE) {
        sio.name = sio_board[sio_type].name;
        sio.base = sio_board[sio_type].base;
        sio.status = sio_board[sio_type].status;
        sio.data = sio_board[sio_type].data;
        sio.rdf_mask = sio_board[sio_type].rdf_mask;
        sio.rdf_bit = sio_board[sio_type].rdf_bit;
        sio.tbe_mask = sio_board[sio_type].tbe_mask;
        sio.tbe_bit = sio_board[sio_type].tbe_bit;

        if (cptr != NULL) {
            result = sscanf(cptr, "%x", &base);

            if (result == 1) {
                sio.base = base & DATAMASK;
            }
        }

        s100_bus_addio(sio.base + sio.status, 1, &sio_io, SIO_SNAME"S");
        s100_bus_addio(sio.base + sio.data, 1, &sio_io, SIO_SNAME"D");
    }

    return SCPE_OK;
}

static t_stat sio_show_board(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    sim_printf("%s, ", sio_unit.flags & UNIT_SIO_VERBOSE ? "VERBOSE" : "QUIET");
    sim_printf("%s", sio_unit.flags & UNIT_SIO_CONSOLE ? "CONSOLE" : "NOCONSOLE");

    if (sio_type != SIO_TYPE_NONE) {
        sim_printf(", ");
        sim_printf("TYPE=%s,\n", sio.name);
        sim_printf("\tIOBASE=%02X, ", sio.base);
        sim_printf("STAT=%02X, DATA=%02X\n", sio.base + sio.status, sio.base + sio.data);
        sim_printf("\tRDFMASK=%02X, RDFBIT=%02X\n", sio.rdf_mask, sio.rdf_bit);
        sim_printf("\tTBEMASK=%02X, TBEBIT=%02X\n", sio.tbe_mask, sio.tbe_bit);
    }
    else {
        sim_printf("\n\tNo board selected.\n");
    }

    return SCPE_OK;
}

