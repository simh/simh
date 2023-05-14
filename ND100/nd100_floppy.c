/*
 * Copyright (c) 2023 Anders Magnusson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>

#include "sim_defs.h"

#include "nd100_defs.h"

/*
 * Floppy and Streamer Controller 3112.
 * ND documentation ND-11.021.1
 *
 * Also support Floppy Controller 3027 (Z80 version).
 * ND documentation ND-11.015.1
 *
 * Currently only floppies implemented (no streamer).
 *
 * The device uses eight IOX addresses, but the transfer commands 
 * are given in a command block of 12 words in memory.
 *
 * ND uses three floppy formats (but in theory can handle a lot of formats).
 *      - SS/SD 8" (type 0):    77*8*512= 315392 bytes.
 *      - DS/DD 8" (type 017):  77*8*1024*2= 1261568 bytes.
 *      - DS/DD 5 1/4 (type 017?): 80*8*1024*2= 1261568 bytes.
 */

t_stat floppy_svc(UNIT *uptr);
t_stat floppy_reset (DEVICE *dptr);
t_stat floppy_boot (int32 unitno, DEVICE *dptr);
t_stat floppy_attach (UNIT *uptr, CONST char *cptr);
static int floppy_excmd(void);
static int floppy_test(UNIT *up);
static int dtomem(FILE *fp, int daddr, int maddr, int wcnt, int how);
static int getmval(void);

#define FL_NTR  80      /* # tracks/side */
#define FL_NSC  8       /* # sectors/track */
#define FL_NSD  2       /* # sides */
#define FL_NBY  1024    /* # bytes/sector */

#define FL_SZ           (FL_NTR*FL_NTR*FL_NSD*FL_NBY)

/* hardware status reg flags */
#define FL_ST_IE        0000002 /* interrupt enable (RFT) */
#define FL_ST_ACT       0000004 /* device active */
#define FL_ST_RDY       0000010 /* device ready for transfer (RFT) */
#define FL_ST_ERR       0000020 /* OR of errors */
#define FL_ST_HE        0000100 /* Hard error (DMA) */
#define FL_ST_DENS      0140000 /* Dual density ctlr */

/* hardware control word */
#define FL_CW_IE        0000002 /* interrupt enable (RFT) */
#define FL_CW_AUTO      0000004 /* Activate autoload */
#define FL_CW_TEST      0000010 /* Test mode */
#define FL_CW_CLR       0000020 /* Device clear */
#define FL_CW_ENSTR     0000040 /* Enable streamer */
#define FL_CW_FCE       0000400 /* Fetch Command and Execute */

static int fl_rdata;    /* devno + 0, read data */
static int fl_rstatus;  /* devno + 2 (+4) read status */
static int fl_lcw;      /* devno + 3 load control word */
static int fl_lph;      /* devno + 5 load pointer high */
static int fl_lpl;      /* devno + 7 load pointer low */

/*
 * The command block (CB) is DMAed from ND100 memory.
 * Word 0-5 are the command part, 06-13 are the status part.
 *
 *      15                    8 7                     0
 *      +---------------------------------------------+
 *    0 | Command word                                |
 *      +---------------------------------------------+
 *    1 | Device address bit 15-0                     |
 *      +----------------------+----------------------+
 *    2 | Device addr bit 23-16| Memory addr bit 23-16|
 *      +----------------------+----------------------+
 *    3 | Memory addr bit 15-0                        |
 *      +----------------------+----------------------+
 *    4 | Options              | Word count bit 23-16 |
 *      +----------------------+----------------------+
 *    5 | Word count (or record count) bit 15-0       |
 *      +---------------------------------------------+
 *    6 | Status 1                                    |
 *      +---------------------------------------------+
 *    7 | Status 2                                    |
 *      +---------------------------------------------+
 *   10 | Empty                | Last addr 23-16      |
 *      +---------------------------------------------+
 *   11 | last memory address 15-0                    |
 *      +---------------------------------------------+
 *   12 | Empty                | Rem. words 23-16     |
 *      +---------------------------------------------+
 *   13 | Remaining words 15-0                        |
 *      +---------------------------------------------+
 *
 */

/* CB offsets */
#define CB_CW           000
#define CB_DAL          001
#define CB_DAHMAH       002
#define CB_MAL          003
#define CB_OPTWCH       004
#define CB_WCL          005
#define CB_ST1          006
#define CB_ST2          007
#define CB_LAH          010
#define CB_LAL          011
#define CB_REMWH        012
#define CB_REMWL        013

/* Options word (004) */
#define CB_OPT_WC       0100000 /* set if word count in 4/5, else record */

/* Command word (000) */
#define CW_FL_RD        0000000 /* Read data */
#define CW_FL_WR        0000001 /* Write data */
#define CW_FL_EXTST     0000036 /* Read extended status */
#define CW_FL_RDFMT     0000042 /* Read format */
#define CW_FL_CMDMSK    077     /* mask for command */
#define CW_FL_SELSH     6       /* shift for unit */
#define CW_FL_1K        0001400 /* 1K sectors */
#define CW_FL_DS        0002000 /* Double sided */
#define CW_FL_DD        0004000 /* Double density */

/* Status 2 */
#define ST2_FL_BS1K     0000003 /* 1k sectors */
#define ST2_FL_DS       0000004 /* Double sided */
#define ST2_FL_DD       0000010 /* Double density */
#define ST2_FL_514      0000020 /* 5 1/4" floppy */

/* soft data structures (for simh) */
#define state   u3      /* current unit state */
#define U_RDY   00      /* unit idling */
#define U_READ  01      /* unit reading */
#define U_WRITE 02      /* unit writing */
#define U_RDFMT 03      /* Read format */
#define U_EXTST 04      /* Read extended status */

#define devaddr u4      /* unit offset (in words) */
#define wcnt    u5      /* word count */
#define memaddr u6      /* place in memory */

UNIT floppy_unit[] = {
        { UDATA (&floppy_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, FL_SZ) },
        { UDATA (&floppy_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, FL_SZ) },
        { UDATA (&floppy_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, FL_SZ) },
        { UDATA (&floppy_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, FL_SZ) },
        { 0 }
};

REG floppy_reg[] = {
        { ORDATA (RDATA, fl_rdata, 16) },
        { ORDATA (RSTATUS, fl_rstatus, 16) },
        { ORDATA (LCW, fl_lcw, 16) },
        { ORDATA (LPH, fl_lph, 16) },
        { ORDATA (LPL, fl_lpl, 16) },
        { NULL }
};

MTAB floppy_mod[] = {
        { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED",
            &set_writelock, &show_writelock, NULL,
            "Write enable floppy drive" },
        { 0 }
};

DEVICE floppy_dev = {
        "FLOPPY", floppy_unit, floppy_reg, floppy_mod,
        1, 8, 12, 1, 8, 16,
        NULL, NULL, &floppy_reset,
        &floppy_boot, NULL, NULL,
        NULL, DEV_DISABLE
};

struct intr floppy0_int = { 0, 021 };

/*
 * Floppy called via iox instruction.
 */
int
iox_floppy(int addr)
{
        int n;
        int rv = 0;

        switch (addr & 07) {
        case 0: /* read data */
                regA = 0;
                break;

        case 2:
                regA = fl_rstatus;
                break;

        case 3:
                n = regA;
                if (n & FL_CW_TEST) {
                        rv = floppy_test(&floppy_unit[0]);
                        break;
                }
                if (n & FL_CW_FCE) {
                        rv = floppy_excmd();
                        break;
                }
                if (n & FL_CW_IE) { /* Interrupt enable */
                        if ((fl_rstatus & (FL_CW_IE|FL_ST_RDY)) == FL_ST_RDY)
                                extint(11, &floppy0_int);
                        fl_rstatus |= FL_ST_IE;
                        break;
                }
                if (n & FL_CW_CLR) { /* reset */
                        break;
                }
                return STOP_UNHIOX;
                break;

        case 5:
                fl_lph = regA;
                break;

        case 7:
                fl_lpl = regA;
                break;

        default:
                if ((addr & 1) == 0)
                        regA = 0;
                break; /* Unused IOXes are ignored */
        }
        return rv;
}

t_stat
floppy_reset(DEVICE *dptr)
{
        fl_rstatus = FL_ST_DENS | FL_ST_RDY;
        return 0;
}

/*
 * Estimate floppy format based on disk size and return status2 word.
 *      - SS/SD = 315392 bytes, c/h/s 77/1/8 (512b sectors)
 *      - DS/DD = 1261568 bytes, c/h/s 77/2/8 (1024b sectors)
 *      - DS/DD 5 1/4" = 1310720 bytes, c/h/s 80/2/8 (1024b sectors)
 */
static int
readfmt(UNIT *up)
{
        if (sim_fseek(up->fileref, 0, SEEK_END) < 0)
                return -1;
        switch (sim_ftell(up->fileref)) {
        case 315392:
                return 0;
        case 1261568:
                return ST2_FL_DD|ST2_FL_DS|ST2_FL_BS1K;
        case 1310720:
                return /* ST2_FL_514| */ST2_FL_DD|ST2_FL_DS|ST2_FL_BS1K;
        default:
                break;
        }
        return -1;
}
        

t_stat
floppy_svc(UNIT *uptr)
{
        int cbaddr = getmval();
        int lah = 0, lal = 0;
        int status2 = 0;
        int remwh = 0, remwl = 0;

        if ((fl_rstatus & FL_ST_ACT) == 0)
                return STOP_UNHIOX;

        switch (uptr->state) {
        case U_READ:
                dtomem(uptr->fileref, uptr->devaddr,
                    uptr->memaddr, uptr->wcnt, PM_DMA);
                lah = (uptr->memaddr + uptr->wcnt) >> 16;
                lal = (uptr->memaddr + uptr->wcnt) & 0177777;
                break;

        case U_RDFMT:
                if ((status2 = readfmt(uptr)) < 0)
                        goto err;
                break;

        case U_EXTST: /* XXX no docs found...? */
                lah = (uptr->memaddr + uptr->wcnt) >> 16;
                lal = (uptr->memaddr + uptr->wcnt) & 0177777;
                remwh = 0100000; /* XXX */
                remwl = 1; /* XXX */
                pwrmem(uptr->memaddr, 06201, PM_DMA); /* XXX */
                break;

        case U_WRITE:
        default:
                return STOP_UNHIOX;
        }

        pwrmem(cbaddr+CB_ST1, FL_ST_RDY, PM_DMA);
        pwrmem(cbaddr+CB_ST2, status2, PM_DMA); /* set after error or rdfmt */
        pwrmem(cbaddr+CB_LAH, lah, PM_DMA);
        pwrmem(cbaddr+CB_LAL, lal, PM_DMA);
        pwrmem(cbaddr+CB_REMWH, remwh, PM_DMA);
        pwrmem(cbaddr+CB_REMWL, remwl, PM_DMA);

        fl_rstatus &= ~FL_ST_ACT;
        fl_rstatus |= FL_ST_RDY;
        if (fl_rstatus & FL_ST_IE)
                extint(11, &floppy0_int);

        return SCPE_OK;

err:
        return STOP_UNHIOX;
}

/*
 * Get physical memory address for command block.
 */
static int
getmval(void)
{
        return fl_lpl + ((fl_lph & 0377) << 8);
}

/*
 * Read a block from file fp, position daddr (bytes) to ND100 memory maddr
 * wcnt words.
 * Return 0 or fail.
 */
static int
dtomem(FILE *fp, int daddr, int maddr, int wcnt, int how)
{
        unsigned char *wp;
        int bcnt = wcnt *2;
        int i;

        wp = malloc(wcnt * 2);  /* bounce buffer */
        if (sim_fseek(fp, daddr, SEEK_SET) < 0)
                return STOP_UNHIOX;
        if (sim_fread(wp, bcnt, 1, fp) < 0)
                return STOP_UNHIOX;
        for (i = 0; i < bcnt; i += 2)
                wrmem(maddr++, (wp[i] << 8) | wp[i+1], how);
        free(wp);
        return SCPE_OK;
}

t_stat
floppy_boot(int32 unitno, DEVICE *dptr)
{
        printf("floppy_boot \n");
        return 1;
}

/*
 * It seems that some of the boot programs uses the Z80 test programs
 * on the controller card.  We try to mimic the behaviour.
 * 
 * This data structure (in ND100) is used in test T13 and T14.
 *
 *      15                    8 7                     0
 *      +---------------------------------------------+
 *    0 | ND100 Load address                          |
 *      +---------------------------------------------+
 *    1 | Z80 address                                 |
 *      +---------------------------------------------+
 *    2 | Byte count                                  |
 *      +---------------------------------------------+
 */
static int
floppy_test(UNIT *up)
{
        int cbaddr = getmval();
        int n100addr, z80addr, bcnt;
        int rv = 0;

        if ((regA & FL_CW_FCE) == 0)
                return STOP_UNHIOX; /* What to do? */
        switch (regA >> 9) {
        case 016: /* T14, load from Z80 to ND100 */
                n100addr = prdmem(cbaddr, PM_CPU);
                z80addr = prdmem(cbaddr+1, PM_CPU);
                bcnt = prdmem(cbaddr+2, PM_CPU);
                if (bcnt > 3584)
                        return STOP_UNHIOX;
                printf("\r\n addr %06o nd100 %06o z80 %04x count %o\r\n", 
                    cbaddr, prdmem(cbaddr, PM_CPU),
                    prdmem(cbaddr+1, PM_CPU), prdmem(cbaddr+2, PM_CPU));
                dtomem(up->fileref, up->devaddr + z80addr - 0x2200,
                    n100addr, bcnt/2, PM_CPU);
                break;

        default:
                return STOP_UNHIOX;
        }
        return rv;
}

/*
 * Execute. Fills in the unit local vars memaddr/wcnt/devaddr from the 
 * command block and setup for interrupt later.
 */
static int
floppy_excmd(void)
{
        UNIT *unit;
        int cw, u, cmd, i;
        int cbaddr = getmval();
        int status2, sectsz;

        cw = prdmem(cbaddr+CB_CW, PM_CPU);
        u = (cw >> CW_FL_SELSH) & 03;
        cmd = cw & CW_FL_CMDMSK;

        unit = &floppy_unit[u];
        if ((unit->flags & UNIT_ATT) == 0)
                goto err; /* floppy not inserted */

        status2 = readfmt(unit);
        sectsz = (status2 & ST2_FL_BS1K) == 0 ? 512 : 1024;

        /* XXX check disk size, word count etc... */
        unit->memaddr = ((prdmem(cbaddr+CB_DAHMAH, PM_CPU) & 0377) << 16) |
            prdmem(cbaddr+CB_MAL, PM_CPU);
        i = prdmem(cbaddr+CB_OPTWCH, PM_CPU);
        unit->wcnt = ((i & 0377) << 16) | prdmem(cbaddr+CB_WCL, PM_CPU);
        if ((i & CB_OPT_WC) == 0)
                unit->wcnt *= (sectsz/2);
        unit->devaddr = (((prdmem(cbaddr+CB_DAHMAH, PM_CPU) & 0177400) << 8) |
            prdmem(cbaddr+CB_DAL, PM_CPU)) * sectsz;

        switch (cmd) {
        case CW_FL_RD:
                unit->state = U_READ;
                break;
        case CW_FL_WR:
                goto err; /* floppy write protected */
        case CW_FL_EXTST:
                unit->state = U_EXTST;
                break;
        case CW_FL_RDFMT:
                unit->state = U_RDFMT;
                break;
        default:
                goto err;
        }

        sim_activate(&floppy_unit[u], 10);
        fl_rstatus &= ~FL_ST_RDY;
        fl_rstatus |= FL_ST_ACT;
        return SCPE_OK;

err:
        return STOP_UNHIOX;
}
