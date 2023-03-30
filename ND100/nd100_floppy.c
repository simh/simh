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
 * Floppy and Streamer Controller (3112).
 * ND documentation ND-11.021.1
 *
 * Currently only 5 1/4" DS/DD floppies implemented (no streamer).
 *
 * The device uses eight IOX addresses, but the transfer commands 
 * are given in a command block of 12 words in memory.
 */

t_stat floppy_svc(UNIT *uptr);
t_stat floppy_reset (DEVICE *dptr);
t_stat floppy_boot (int32 unitno, DEVICE *dptr);
t_stat floppy_attach (UNIT *uptr, CONST char *cptr);
static int floppy_excmd(void);

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
#define FL_ST_DENS      0100000 /* Dual density ctlr */

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

        case 1:
                break;

        case 2:
                regA = fl_rstatus;
                break;

        case 3:
                n = regA;
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
                rv = STOP_UNHIOX;
                break;
        }

        return rv;
}

t_stat
floppy_reset(DEVICE *dptr)
{
        fl_rstatus = FL_ST_DENS | FL_ST_RDY;
        return 0;
}

t_stat
floppy_svc(UNIT *uptr)
{
        unsigned char *wp;
        int i, j;
        int cbaddr = fl_lpl + ((fl_lph & 0377) << 8);
        int lah = 0, lal = 0;

        if ((fl_rstatus & FL_ST_ACT) == 0)
                return STOP_UNHIOX;

        switch (uptr->state) {
        case U_READ:
                wp = malloc(uptr->wcnt * 2);
                if (fseek(uptr->fileref, uptr->devaddr * 2, SEEK_SET) < 0)
                        goto err;
                if (sim_fread(wp, uptr->wcnt, 2, uptr->fileref) < 0)
                        goto err;
                for (i = 0, j = 0; i < uptr->wcnt; i++, j += 2)
                        wrmem(uptr->memaddr+i, (wp[j] << 8) | wp[j+1]);
                lah = (uptr->memaddr + uptr->wcnt) >> 16;
                lal = (uptr->memaddr + uptr->wcnt) & 0177777;
                free(wp);
                break;

        case U_RDFMT:
                break;

        case U_WRITE:
        default:
                return STOP_UNHIOX;
        }

        wrmem(cbaddr+CB_ST1, FL_ST_RDY);
        wrmem(cbaddr+CB_ST2,
            ST2_FL_BS1K|ST2_FL_DS|ST2_FL_DD|ST2_FL_514);
        wrmem(cbaddr+CB_LAH, lah);
        wrmem(cbaddr+CB_LAL, lal);
        wrmem(cbaddr+CB_REMWH, 0);
        wrmem(cbaddr+CB_REMWL, 0);

        fl_rstatus &= ~FL_ST_ACT;
        fl_rstatus |= FL_ST_RDY;
        if (fl_rstatus & FL_ST_IE)
                extint(11, &floppy0_int);

        return SCPE_OK;

err:
        return STOP_UNHIOX;
}

t_stat
floppy_boot(int32 unitno, DEVICE *dptr)
{
        printf("floppy_boot \n");
        return 1;
}

static int
floppy_excmd(void)
{
        UNIT *unit;
        int cw, u, cmd;
        int cbaddr = fl_lpl + ((fl_lph & 0377) << 8);

        cw = rdmem(cbaddr+CB_CW);
        u = (cw >> CW_FL_SELSH) & 03;
        cmd = cw & CW_FL_CMDMSK;

        unit = &floppy_unit[u];
        if ((unit->flags & UNIT_ATT) == 0)
                goto err; /* floppy not inserted */

        /* XXX check disk size, word count etc... */
        unit->memaddr = ((rdmem(cbaddr+CB_DAHMAH) & 0377) << 16) | rdmem(cbaddr+CB_MAL);
        unit->wcnt = ((rdmem(cbaddr+CB_OPTWCH) & 0377) << 16) | rdmem(cbaddr+CB_WCL);
        unit->devaddr = ((rdmem(cbaddr+CB_DAHMAH) & 0177400) << 8) |
                    rdmem(cbaddr+CB_DAL);

        if (cmd == CW_FL_RDFMT) {
                unit->state = U_RDFMT;
        } else if (cmd == CW_FL_RD || cmd == CW_FL_WR) {
                if (cmd == CW_FL_WR)
                        goto err; /* floppy write protected */

                if ((cw & CW_FL_1K) != CW_FL_1K)
                        goto err; /* Require 1K sectors */
                if ((cw & (CW_FL_DS|CW_FL_DD)) != (CW_FL_DS|CW_FL_DD))
                        goto err; /* Must be double sided/double density */

                unit->state = U_READ;
        } else
                goto err;

        sim_activate(&floppy_unit[u], 10);
        fl_rstatus &= ~FL_ST_RDY;
        fl_rstatus |= FL_ST_ACT;
        return SCPE_OK;

err:
        return STOP_UNHIOX;
}
