/* vfii_fdc.c: SD Systems VersaFloppy II

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
   13-Nov-2025 Initial version

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "altair8800_dsk.h"
#include "s100_bus.h"
#include "sds_vfii.h"
#include "wd_17xx.h"

#define DEV_NAME    "VFII"

static WD17XX_INFO *wd17xx = NULL;

#define VFII_WD17XX_OFFSET   1

static int32 poc = TRUE; /* Power On Clear */

static uint8 drv_sel = 0;
static uint8 vfii_creg = 0;

static RES vfii_res = { VFII_IO_BASE, VFII_IO_SIZE, 0x0000, 0x0000 };
static DSK_INFO dsk_info[VFII_NUM_DRIVES];

static t_stat vfii_reset(DEVICE *vfii_dev);
static t_stat vfii_attach(UNIT *uptr, CONST char *cptr);
static t_stat vfii_detach(UNIT *uptr);
static t_stat vfii_boot(int32 unitno, DEVICE *dptr);
static int32 vfii_io(const int32 port, const int32 io, const int32 data);
static int32 vfii_io_in(const int32 port);
static void vfii_io_out(const int32 port, const int32 data);
static t_stat vfii_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char* vfii_description(DEVICE *dptr);

static UNIT vfii_unit[VFII_NUM_DRIVES] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFII_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFII_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFII_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFII_SD_CAPACITY) }
};

static REG vfii_reg[] = {
    { FLDATAD (POC,     poc,       0x01,      "Power on Clear flag"), },
    { DRDATAD (DRVSEL,  drv_sel,   8,         "Drive select"), },
    { NULL }
};

#define VFII_NAME    "SD Systems VersaFloppy II"

static const char* vfii_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }
    return VFII_NAME;
}

static MTAB vfii_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"        },
    { 0 }
};

/* Debug flags */
#define VERBOSE_MSG (1 << 0)
#define ERROR_MSG   (1 << 1)
#define STATUS_MSG  (1 << 2)
#define DRIVE_MSG   (1 << 3)
#define IRQ_MSG     (1 << 4)
#define READ_MSG    (1 << 5)
#define WRITE_MSG   (1 << 6)
#define COMMAND_MSG (1 << 7)
#define FORMAT_MSG  (1 << 8)

/* Debug Flags */
static DEBTAB vfii_dt[] = {
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "DRIVE",      DRIVE_MSG,      "Drive messages"    },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { "READ",       READ_MSG,       "Read messages"     },
    { "WRITE",      WRITE_MSG,      "Write messages"    },
    { "COMMAND",    COMMAND_MSG,    "Command messages"  },
    { "FORMAT",     FORMAT_MSG,     "Format messages"   },
    { NULL,         0                                   }
};

DEVICE vfii_dev = {
    DEV_NAME, vfii_unit, vfii_reg, vfii_mod, VFII_NUM_DRIVES,
    ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &vfii_reset,
    &vfii_boot, &vfii_attach, &vfii_detach,
    &vfii_res, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    vfii_dt, NULL, NULL, &vfii_show_help, NULL, NULL, &vfii_description
};

/* Reset routine */
static t_stat vfii_reset(DEVICE *dptr)
{
    RES *res;
    int i;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        sim_printf("CTX is NULL!\n");
        return SCPE_IERR;
    }

    if(dptr->flags & DEV_DIS) { /* Unmap I/O Ports */
        wd17xx = wd17xx_release(wd17xx);

        s100_bus_remio(res->io_base, res->io_size, &vfii_io);

        poc = TRUE;
    } else {
        if (poc) {
            for (i = 0; i < VFII_NUM_DRIVES; i++) {
                vfii_unit[i].dptr = dptr;
                dsk_init(&dsk_info[i], &vfii_unit[i], 77, 1, 0);
                dsk_set_verbose_flag(&dsk_info[i], VERBOSE_MSG);
            }

            if (wd17xx == NULL) {
                if ((wd17xx = wd17xx_init(dptr)) == NULL) {
                    sim_printf("Could not init WD17XX\n");
                }
                else {
                    wd17xx_set_fdctype(wd17xx, WD17XX_FDCTYPE_1795);    /* Set to 1795 */
                    wd17xx_set_verbose_flag(wd17xx, VERBOSE_MSG);
                    wd17xx_set_error_flag(wd17xx, ERROR_MSG);
                    wd17xx_set_read_flag(wd17xx, READ_MSG);
                    wd17xx_set_write_flag(wd17xx, WRITE_MSG);
                    wd17xx_set_command_flag(wd17xx, COMMAND_MSG);
                    wd17xx_set_format_flag(wd17xx, FORMAT_MSG);
                }
            }

            s100_bus_addio(res->io_base, res->io_size, &vfii_io, DEV_NAME);

            poc = FALSE;
        }

        drv_sel = 0;

        if (wd17xx != NULL) {
            wd17xx_reset(wd17xx);
            wd17xx_set_dsk(wd17xx, &dsk_info[drv_sel]);
        }
    }

    return SCPE_OK;
}

static t_stat vfii_boot(int32 unitno, DEVICE *dptr)
{

    sim_debug(STATUS_MSG, &vfii_dev, DEV_NAME ": Booting Controller at 0x%04x\n", 0xE000);

    s100_bus_set_addr(0xE000);

    return SCPE_OK;
}

/* Attach routine */
static t_stat vfii_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    int d;

    /* Determine drive number */
    d = uptr - &vfii_unit[0];

    if (d < 0 || d >= VFII_NUM_DRIVES) {
        return SCPE_IERR;
    }

    sim_switches |= SWMASK ('E');     /* File must exist */

    if ((r = attach_unit(uptr, cptr)) != SCPE_OK) {    /* attach unit  */
        sim_printf(DEV_NAME ": ATTACH error=%d\n", r);
        return r;
    }

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    /* init format based on file size */
    switch (uptr->capac) {
        case VFII_DD_CAPACITY:
            dsk_init_format(&dsk_info[d], 0, 76, 0, 0, DSK_DENSITY_DD, 26, 256, 1);
            break;

        default:
            uptr->capac = VFII_SD_CAPACITY;
            dsk_init_format(&dsk_info[d], 0, 76, 0, 0, DSK_DENSITY_DD, 26, 256, 1);
            break;
    }

//    dsk_show(&dsk_info[d]);

    return r;
}

/* Detach routine */
static t_stat vfii_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);  /* detach unit */

    return r;
}

static int32 vfii_io(const int32 port, const int32 io, const int32 data)
{
    int32 result = 0xff;

    if (io == S100_IO_READ) { /* I/O Write */
        result = vfii_io_in(port);
    } else { /* I/O Write */
        vfii_io_out(port, data);
    }

    return result;
}

static int32 vfii_io_in(const int32 port)
{
    int32 result = 0xff;
    int32 offset = port - vfii_res.io_base;

    switch (offset) {
        case VFII_REG_STATUS:
            result = vfii_creg;

            sim_debug(STATUS_MSG, &vfii_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Read WAIT, Port 0x%02x Result 0x%02x\n", s100_bus_get_addr(), port, result);
            break;

        case WD17XX_REG_STATUS + VFII_WD17XX_OFFSET:
        case WD17XX_REG_TRACK + VFII_WD17XX_OFFSET:
        case WD17XX_REG_SECTOR + VFII_WD17XX_OFFSET:
        case WD17XX_REG_DATA + VFII_WD17XX_OFFSET:
            result = wd17xx_inp(wd17xx, offset - VFII_WD17XX_OFFSET);

            sim_debug(STATUS_MSG, &vfii_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Read WD17XX, Port 0x%02x (0x%02x) Result 0x%02x\n", s100_bus_get_addr(), port, offset - VFII_WD17XX_OFFSET, result);
            break;

        default:
            break;
    }

    return result;
}

/* VersaFloppy II Control/Status
 *
 * BIT 0-3 Drive Select
 * BIT 4   Side Select (1 = Side 0)
 * BIT 5   5"/8" Drive (1 = 8")
 * BIT 6   Double/Single Density (1 = SD)
 * BIT 7   Wait Enable (Not used in simulator)
 *
 * All bits are inverted on the VFII
 *
 */

static void vfii_io_out(const int32 port, const int32 data)
{
    int32 offset = port - vfii_res.io_base;

    switch (offset) {
        case VFII_REG_CONTROL:
            vfii_creg = data & 0xff;

            drv_sel = sys_floorlog2((~data) & VFII_DSEL_MASK);
            wd17xx_sel_side(wd17xx, (data & VFII_SIDE_MASK) ? 0 : 1);
            wd17xx_sel_dden(wd17xx, (data & VFII_DDEN_MASK) ? FALSE : TRUE);
            wd17xx_sel_drive_type(wd17xx, (data & VFII_SIZE_MASK) ? 8 : 5);

            sim_debug(DRIVE_MSG, &vfii_dev, DEV_NAME ": " ADDRESS_FORMAT " WR DRVSEL (0x%02x)  = 0x%02x: Drive: %d\n",
                s100_bus_get_addr(), port, data & DATAMASK, drv_sel);

            /* Tell WD17XX which drive is selected */
            wd17xx_set_dsk(wd17xx, &dsk_info[drv_sel]);
            break;

        case WD17XX_REG_COMMAND + VFII_WD17XX_OFFSET:
        case WD17XX_REG_TRACK + VFII_WD17XX_OFFSET:
        case WD17XX_REG_SECTOR + VFII_WD17XX_OFFSET:
        case WD17XX_REG_DATA + VFII_WD17XX_OFFSET:
            wd17xx_outp(wd17xx, offset - VFII_WD17XX_OFFSET, data & DATAMASK);

            sim_debug(STATUS_MSG, &vfii_dev, DEV_NAME ": " ADDRESS_FORMAT
                " Write WD17XX, Port 0x%02x (0x%02x) Data 0x%02x\n", s100_bus_get_addr(), port, offset - VFII_WD17XX_OFFSET, data & DATAMASK);
            break;

        default:
            break;
    }
}

static t_stat vfii_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nSD Systems VersaFlopyy II (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

