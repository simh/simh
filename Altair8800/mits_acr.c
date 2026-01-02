/* mits_acr.c: MITS Altair 8800 88-ACR

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
   27-Dec-2025 Initial version

*/

#include "sim_defs.h"
#include "s100_bus.h"
#include "mits_acr.h"

#define DEVICE_NAME "ACR"

static int32 poc = TRUE;            /* Power On Clear */
static int32 rdr = 0x00;            /* Receive Data Register */
static int32 rdre = ACR_RDRE;       /* Receive Data Register Empty Bit */

static t_stat acr_reset      (DEVICE *dptr);
static t_stat acr_attach     (UNIT *uptr, const char *cptr);
static t_stat acr_detach     (UNIT *uptr);
static void acr_rdr          ();
static int32 acr_io          (const int32 addr, const int32 rw, const int32 data);
static int32 acr_data        (const int32 addr, const int32 rw, const int32 data);
static t_stat acr_rewind     (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat acr_show_help  (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat acr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

static const char* acr_description(DEVICE *dptr) {
    return "MITS 88-ACR";
}

static RES acr_res = { ACR_IOBASE, ACR_IOSIZE, 0, 0, NULL };

static UNIT acr_unit = {
    UDATA (NULL, UNIT_ATTABLE | UNIT_ROABLE | UNIT_ACR_VERBOSE, 0)
};

static REG acr_reg[] = {
    { NULL }
};

static MTAB acr_mod[] = {
    { UNIT_ACR_VERBOSE,     UNIT_ACR_VERBOSE, "VERBOSE", "VERBOSE", NULL, NULL, NULL, "Enable verbose messages"  },
    { UNIT_ACR_VERBOSE,     0,               "QUIET",   "QUIET",   NULL, NULL, NULL, "Disable verbose messages" },

    { MTAB_XTD | MTAB_VDV, 0, "IOBASE", "IOBASE", &set_iobase, &show_iobase, NULL, "Sets MITS ACR base I/O address" },

    { MTAB_XTD | MTAB_VUN, 0, NULL, "REWIND", &acr_rewind, NULL, NULL, "Rewind cassette" },

    { 0 }
};

/* Debug Flags */
static DEBTAB acr_dt[] = {
    { NULL, 0 }
};

DEVICE acr_dev = {
    DEVICE_NAME, &acr_unit, acr_reg, acr_mod,
    1, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &acr_reset, NULL,
    &acr_attach, &acr_detach, &acr_res,
    (DEV_DISABLE | DEV_DIS), 0,
    acr_dt, NULL, NULL, &acr_show_help, &acr_attach_help, NULL, &acr_description
};

static t_stat acr_reset(DEVICE *dptr) {
    if (dptr->flags & DEV_DIS) {    /* Disable Device */
        s100_bus_remio(acr_res.io_base, acr_res.io_size, &acr_io);

        poc = TRUE;

        return SCPE_OK;
    }

    if (poc) {
        s100_bus_addio(acr_res.io_base, acr_res.io_size, &acr_io, DEVICE_NAME);

        poc = FALSE;
    }

    rdre = ACR_RDRE; /* Receive Data Register Empty */

    return SCPE_OK;
}

static t_stat acr_attach(UNIT *uptr, const char *cptr)
{
    t_stat r;

    rdre = ACR_RDRE;

    if ((r = attach_unit(uptr, cptr)) == SCPE_OK) {
        acr_rdr();
    }

    return r;
}

static t_stat acr_detach(UNIT *uptr)
{
    return detach_unit(uptr);
}

static void acr_rdr()
{
    t_stat ch;

    if (acr_unit.fileref != NULL) { /* attached to a file? */
        if (rdre) {                 /* receive data register empty? */
            ch = getc(acr_unit.fileref);

            if (ch == EOF) {
                rdre = ACR_RDRE;
            } else {
                rdre = 0x00;                 /* indicate character available */

                rdr = ch;                    /* store character in register  */
            }
        }
    }
}

static int32 acr_io(const int32 addr, const int32 rw, const int32 data)
{
    if (addr & 0x01) {
        return acr_data(addr, rw, data);
    }

    if (rw == S100_IO_READ) { /* Return status */
        return rdre;
    }

    return 0xff;
}

static int32 acr_data(const int32 addr, const int32 rw, const int32 data)
{
    t_stat ch;

    if (rw == S100_IO_READ) {
        ch = rdr;

        rdre = ACR_RDRE;  /* Receive data register empty */

        acr_rdr();        /* Check for another character */

        return ch;
    }

    if (acr_unit.fileref != NULL) {  /* attached to a file? */
        fputc(data, acr_unit.fileref);
    }
    else {
        sim_putchar(data);
    }

    return 0xff;
}

static t_stat acr_rewind(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (acr_unit.fileref != NULL) {
        rewind(acr_unit.fileref);

        if (acr_unit.flags & UNIT_ACR_VERBOSE) {
            sim_printf("TAPE is rewound\n");
        }
    }
    else {
        sim_printf("No file attached to %s device.\n", DEVICE_NAME);
    }

    return SCPE_OK;
}

static t_stat acr_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "\n88-ACR (%s)\n", DEVICE_NAME);

    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);

    return SCPE_OK;
}

static t_stat acr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "\n88-ACR (%s)\n\n", DEVICE_NAME);

    fprintf(st, "The %s device simulates the MITS ACR Audio Cassette Interface\n", DEVICE_NAME);
    fprintf(st, "and cassette tape recorder.\n");

    fprintf(st, "\nATTACH %s <filename>\n\n", DEVICE_NAME);
    fprintf(st, "    Inserts a tape into the cassette recorder. Files attached to\n");
    fprintf(st, "    the %s device are binary files that contain the contants of\n", DEVICE_NAME);
    fprintf(st, "    the data stored on the tape.\n");

    fprintf(st, "\nDETACH %s\n\n", DEVICE_NAME);
    fprintf(st, "    Removes a tape from the cassette recorder.\n\n");

    fprintf(st, "\nSHOW %s TAPE\n\n", DEVICE_NAME);
    fprintf(st, "    Shows the current status of the %s device.\n", DEVICE_NAME);

    fprintf(st, "\nExample:\n\n");
    fprintf(st, "SET %s ENA\n", DEVICE_NAME);
    fprintf(st, "ATTACH %s BASIC Ver 1-0.tap\n", DEVICE_NAME);
    fprintf(st, "HEXLOAD LOAD10.HEX\n");
    fprintf(st, "SET SIO ENA\n");
    fprintf(st, "SET SIO BOARD=SIO\n");
    fprintf(st, "SET SIO CONSOLE\n");
    fprintf(st, "BREAK -M 117F\n");
    fprintf(st, "G 1800\n");
    fprintf(st, "G 0\n\n");
    fprintf(st, "This example loads ALTAIR BASIC 1.0 from tape using the %s device.\n", DEVICE_NAME);
    fprintf(st, "The files are available from:\n\n");
    fprintf(st, "https://deramp.com/downloads/altair/software/papertape_cassette/\n");

    return SCPE_OK;
}

