/* tarbell_fdc.c: Tarbell 1011/2022 Floppy Disk Controller

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
#include "s100_bus.h"
#include "altair8800_dsk.h"
#include "tarbell_fdc.h"
#include "wd_17xx.h"

#define DEV_NAME    "TARBELL"

static WD17XX_INFO *wd17xx = NULL;

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

static int32 poc = TRUE; /* Power On Clear */

static uint8 drv_sel = 0;
static int32 ddfdc_enabled = FALSE;
static int32 prom_enabled = TRUE;
static int32 prom_active = FALSE;

static RES tarbell_res = { TARBELL_IO_BASE, TARBELL_IO_SIZE, TARBELL_PROM_BASE, TARBELL_PROM_SIZE };
static MDEV mdev = { NULL, NULL };
static DSK_INFO dsk_info[TARBELL_NUM_DRIVES];

/* Tarbell PROM is 32 bytes */
static uint8 tarbell_prom[TARBELL_PROM_SIZE] = {
    0xdb, 0xfc, 0xaf, 0x6f, 0x67, 0x3c, 0xd3, 0xfa,
    0x3e, 0x8c, 0xd3, 0xf8, 0xdb, 0xfc, 0xb7, 0xf2,
    0x19, 0x00, 0xdb, 0xfb, 0x77, 0x23, 0xc3, 0x0c,
    0x00, 0xdb, 0xf8, 0xb7, 0xca, 0x7d, 0x00, 0x76
};

static t_stat tarbell_reset(DEVICE *tarbell_dev);
static t_stat tarbell_attach(UNIT *uptr, const char *cptr);
static t_stat tarbell_detach(UNIT *uptr);
static t_stat tarbell_boot(int32 unitno, DEVICE *dptr);
static t_stat tarbell_set_model(UNIT *uptr, int32 val, const char *cptr, void *desc);
static t_stat tarbell_show_model(FILE *st, UNIT *uptr, int32 val, const void *desc);
static t_stat tarbell_set_prom(UNIT *uptr, int32 val, const char *cptr, void *desc);
static t_stat tarbell_show_prom(FILE *st, UNIT *uptr, int32 val, const void *desc);
static void tarbell_enable_prom(void);
static void tarbell_disable_prom(void);
static int32 tarbell_io(const int32 port, const int32 io, const int32 data);
static int32 tarbell_memio(const int32 addr, const int32 rw, const int32 data);
static t_stat tarbell_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char* tarbell_description(DEVICE *dptr);

static UNIT tarbell_unit[TARBELL_NUM_DRIVES] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_SD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_SD_CAPACITY) }
};

static REG tarbell_reg[] = {
    { FLDATAD (POC,     poc,       0x01,      "Power on Clear flag"), },
    { DRDATAD (DRVSEL,  drv_sel,   8,         "Drive select"), },
    { NULL }
};

#define TARBELL_NAME    "Tarbell 2022 Double-Density FDC"

static const char* tarbell_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }
    return TARBELL_NAME;
}

static MTAB tarbell_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"        },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PROM", "PROM={ENABLE|DISABLE}",
        &tarbell_set_prom, &tarbell_show_prom, NULL, "ROM enabled/disabled status"},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MODEL", "MODEL={SD|DD}",
        &tarbell_set_model, &tarbell_show_model, NULL, "Set/Show the current controller model" },

    { 0 }
};

/* Debug Flags */
static DEBTAB tarbell_dt[] = {
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

DEVICE tarbell_dev = {
    DEV_NAME, tarbell_unit, tarbell_reg, tarbell_mod, TARBELL_NUM_DRIVES,
    ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &tarbell_reset,
    &tarbell_boot, &tarbell_attach, &tarbell_detach,
    &tarbell_res, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    tarbell_dt, NULL, NULL, &tarbell_show_help, NULL, NULL, &tarbell_description
};

/* Reset routine */
static t_stat tarbell_reset(DEVICE *dptr)
{
    RES *res;
    int i;

    if ((res = (RES *) dptr->ctxt) == NULL) {
        sim_printf("CTX is NULL!\n");
        return SCPE_IERR;
    }

    if(dptr->flags & DEV_DIS) { /* Unmap I/O Ports */
        wd17xx = wd17xx_release(wd17xx);

        s100_bus_remio(res->io_base, res->io_size, &tarbell_io);

        poc = TRUE;
    } else {
        if (poc) {
            ddfdc_enabled = FALSE;

            for (i = 0; i < TARBELL_NUM_DRIVES; i++) {
                tarbell_unit[i].dptr = dptr;
                dsk_init(&dsk_info[i], &tarbell_unit[i], 77, 1, 0);
                dsk_set_verbose_flag(&dsk_info[i], VERBOSE_MSG);
            }

            if (wd17xx == NULL) {
                if ((wd17xx = wd17xx_init(dptr)) == NULL) {
                    sim_printf("Could not init WD17XX\n");
                }
                else {
                    wd17xx_set_fdctype(wd17xx, WD17XX_FDCTYPE_1771);    /* Set to 1771 */
                    wd17xx_set_verbose_flag(wd17xx, VERBOSE_MSG);
                    wd17xx_set_error_flag(wd17xx, ERROR_MSG);
                    wd17xx_set_read_flag(wd17xx, READ_MSG);
                    wd17xx_set_write_flag(wd17xx, WRITE_MSG);
                    wd17xx_set_command_flag(wd17xx, COMMAND_MSG);
                    wd17xx_set_format_flag(wd17xx, FORMAT_MSG);
                }
            }

            s100_bus_addio(res->io_base, res->io_size, &tarbell_io, DEV_NAME);

            if (prom_enabled) {
                tarbell_enable_prom();
            }

            poc = FALSE;
        }

        if (prom_enabled) {
            prom_active = TRUE;
        }

        drv_sel = 0;

        if (wd17xx != NULL) {
            wd17xx_reset(wd17xx);
            wd17xx_set_dsk(wd17xx, &dsk_info[drv_sel]);
        }
    }

    return SCPE_OK;
}

static t_stat tarbell_boot(int32 unitno, DEVICE *dptr)
{

    sim_debug(STATUS_MSG, &tarbell_dev, DEV_NAME ": Booting Controller at 0x%04x\n", tarbell_res.mem_base);

    s100_bus_set_addr(tarbell_res.mem_base);

    return SCPE_OK;
}

/* Attach routine */
static t_stat tarbell_attach(UNIT *uptr, const char *cptr)
{
    t_stat r;
    int d;

    /* Determine drive number */
    d = uptr - &tarbell_unit[0];

    if (d < 0 || d >= TARBELL_NUM_DRIVES) {
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
        case TARBELL_DD_CAPACITY:
            dsk_init_format(&dsk_info[d], 0, 0, 0, 0, DSK_DENSITY_SD, 26, 128, 1);
            dsk_init_format(&dsk_info[d], 1, 76, 0, 0, DSK_DENSITY_DD, 51, 128, 1);
            break;

        default:
            uptr->capac = TARBELL_SD_CAPACITY;
            dsk_init_format(&dsk_info[d], 0, 76, 0, 0, DSK_DENSITY_SD, 26, 128, 1);
            break;
    }

//    dsk_show(&dsk_info[d]);

    return r;
}

/* Detach routine */
static t_stat tarbell_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);  /* detach unit */

    return r;
}

static int32 tarbell_io(const int32 port, const int32 io, const int32 data)
{
    int32 result = 0xff;

    if (io == S100_IO_WRITE) { /* I/O Write */

        switch (port & TARBELL_IO_MASK) {
            case WD17XX_REG_COMMAND:
            case WD17XX_REG_TRACK:
            case WD17XX_REG_SECTOR:
            case WD17XX_REG_DATA:
                wd17xx_outp(wd17xx, port & TARBELL_IO_MASK, data & DATAMASK);
                sim_debug(STATUS_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT
                    " Write WD17XX, Port 0x%02x Data 0x%02x\n", s100_bus_get_addr(), port, data & DATAMASK);
                break;

            case TARBELL_REG_DRVSEL:
                if (ddfdc_enabled) {
                    drv_sel = (data & TARBELL_DSEL_MASK) >> 4;     /* 2022 not inverted */
                    wd17xx_sel_side(wd17xx, (data & TARBELL_SIDE_MASK) >> 6);
                    wd17xx_sel_dden(wd17xx, (data & TARBELL_DENS_MASK) ? TRUE : FALSE);

                    sim_debug(DRIVE_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT " WR DRVSEL (0x%02x)  = 0x%02x: Drive: %d, Side: %d, %s-Density.\n",
                        s100_bus_get_addr(), port, data & DATAMASK, drv_sel,
                        (data & TARBELL_SIDE_MASK) >> 6, (data & TARBELL_DENS_MASK) ? "Double" : "Single");
                }
                else {
                    drv_sel = (~data & TARBELL_DSEL_MASK) >> 4;    /* 1011 inverted */
                    wd17xx_sel_side(wd17xx, 0);
                    wd17xx_sel_dden(wd17xx, FALSE);

                    sim_debug(DRIVE_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT " WR DRVSEL (0x%02x)  = 0x%02x: Drive: %d\n",
                        s100_bus_get_addr(), port, data & DATAMASK, drv_sel);
                }

                /* Tell WD17XX which drive is selected */
                wd17xx_set_dsk(wd17xx, &dsk_info[drv_sel]);

                break;

            case TARBELL_REG_EXTADDR:
                sim_debug(STATUS_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT
                    " Write Extended Address, Port 0x%02x=0x%02x\n", s100_bus_get_addr(), port, data & DATAMASK);
                break;

            default:
                break;
        }
    } else { /* I/O Read */
        switch (port & TARBELL_IO_MASK) {
            case WD17XX_REG_STATUS:
            case WD17XX_REG_TRACK:
            case WD17XX_REG_SECTOR:
            case WD17XX_REG_DATA:
                result = wd17xx_inp(wd17xx, port & TARBELL_IO_MASK);
                sim_debug(STATUS_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT
                    " Read WD17XX, Port 0x%02x Result 0x%02x\n", s100_bus_get_addr(), port, result);
                break;

            case TARBELL_REG_WAIT:
                result = (wd17xx_intrq(wd17xx)) ? 0 : TARBELL_FLAG_DRQ;
                sim_debug(STATUS_MSG, &tarbell_dev, DEV_NAME ": " ADDRESS_FORMAT
                    " Read WAIT, Port 0x%02x Result 0x%02x\n", s100_bus_get_addr(), port, result);
                break;

            case TARBELL_REG_DMASTAT:
                result = 0x00;
                break;
        }
    }

    return result;
}

/*
 * The Tarbell Floppy Disk Constroller has a 32-byte PROM
 * located at 0x0000. The PROM loads the first sector of
 * track 0 from drive 0 into 0x0000. Since the PROM is
 * active at 0x0000, the Tarbell asserts /PHANTOM. While
 * /PHANTOM is asserted, memory reads from 0x0000-0x001f
 * will be provided by the Tarbell PROM, while memory
 * writes to those locations will be handled by the RAM
 * board. /PHANTOM is simulated below by passing requests
 * to the RAM board configured on the BUS for the first
 * page of RAM. The PROM is disabled and /PHANTOM is
 * deasserted when A5 is active.
 */
static int32 tarbell_memio(const int32 addr, const int32 rw, const int32 data)
{
    if (rw == S100_IO_READ) {
        if (prom_active && ((addr & TARBELL_PROM_MASK) == addr)) {
            return tarbell_prom[addr];
        }
        else if (mdev.routine != NULL) {
            if (addr & 0x0020) {
                prom_active = FALSE;
            }

            return (mdev.routine)(addr, rw, data);
        }
    }
    else {
        /* If writing to RAM, call memory device routine */
        if (mdev.routine != NULL) {
            return (mdev.routine)(addr, rw, data);
        }
    }

    return 0xff;
}

static t_stat tarbell_set_model(UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;

    /* Remove IO mapping */
    s100_bus_remio(tarbell_res.io_base, tarbell_res.io_size, &tarbell_io);

    /* this assumes that the parameter has already been upcased */
    if (!strcmp(cptr, "DD")) {
        ddfdc_enabled = TRUE;
        tarbell_res.io_size = TARBELL_IO_SIZE;
        wd17xx_set_fdctype(wd17xx, WD17XX_FDCTYPE_1791);    /* Set to 1791 */
    } else {
        ddfdc_enabled = FALSE;
        tarbell_res.io_size = TARBELL_IO_SIZE - 1;
        wd17xx_set_fdctype(wd17xx, WD17XX_FDCTYPE_1771);    /* Set to 1771 */
    }
    
    /* Map new IO */
    s100_bus_addio(tarbell_res.io_base, tarbell_res.io_size, &tarbell_io, DEV_NAME);

    return SCPE_OK;
}

static t_stat tarbell_show_model(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    fprintf(st, "MODEL=%s", (ddfdc_enabled) ? "2022DD" : "1011SD");

    return SCPE_OK;
}

static t_stat tarbell_set_prom(UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "ENABLE", strlen(cptr)) && prom_enabled == FALSE) {
        tarbell_enable_prom();
    } else if (!strncmp(cptr, "DISABLE", strlen(cptr)) && prom_enabled == TRUE) {
        tarbell_disable_prom();
    } else {
        return SCPE_ARG;
    }

    return SCPE_OK;
}

static void tarbell_enable_prom(void)
{
    /* Save existing memory device */
    s100_bus_get_mdev(tarbell_res.mem_base, &mdev);

    /* Add PROM to bus */
    s100_bus_addmem(tarbell_res.mem_base, tarbell_res.mem_size, &tarbell_memio, DEV_NAME);

    prom_enabled = TRUE;
}

static void tarbell_disable_prom(void)
{
    /* Restore memory device */
    if (mdev.routine != NULL) {
        s100_bus_addmem(tarbell_res.mem_base, tarbell_res.mem_size, mdev.routine, mdev.name);
        mdev.routine = NULL;
        mdev.name = NULL;
    }
    else {
        s100_bus_remmem(tarbell_res.mem_base, tarbell_res.mem_size, &tarbell_memio);
    }

    prom_enabled = FALSE;
}

static t_stat tarbell_show_prom(FILE *st, UNIT *uptr, int32 val, const void *desc)
{
    fprintf(st, "%s (%sactive)", (prom_enabled) ? "PROM" : "NOPROM", (prom_active) ? "" : "in");

    return SCPE_OK;
}

static t_stat tarbell_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nTarbell Model 1011/2022 Disk Controller (%s)\n", dptr->name);

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

