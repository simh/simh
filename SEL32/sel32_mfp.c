/* sel32_mfp.c: SEL-32 Model 8002 MFP processor controller

   Copyright (c) 2018-2022, James C. Bevier

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This channel is the interrupt fielder for all of the MFP sub channels.  It's
   channel address is 7600.  This code handles the INCH command for the MFP
   devices and controls the status FIFO for the mfp devices on interrupts and
   TIO instructions..

   Possible devices:
   The f8iop communication controller (TY76A0), (TY76B0), (TY76C0) 
   The ctiop console communications controller (CT76FC & CT76FD)
   The lpiop line printer controller (LP76F8), (LP76F9)
   The scsi  SCSI disk controller (DM7600), (DM7640)

*/

#include "sel32_defs.h"

#if NUM_DEVS_MFP > 0

#define UNIT_MFP    UNIT_IDLE | UNIT_DISABLE

/* forward definitions */
t_stat  mfp_preio(UNIT *uptr, uint16 chan);
t_stat  mfp_startcmd(UNIT *uptr, uint16 chan, uint8 cmd);
void    mfp_ini(UNIT *uptr, t_bool f);
t_stat  mfp_rschnlio(UNIT *uptr);
t_stat  mfp_srv(UNIT *uptr);
t_stat  mfp_reset(DEVICE *dptr);
t_stat  mfp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char  *mfp_desc(DEVICE *dptr);

/* Held in u3 is the device command and status */
#define MFP_INCH    0x00    /* Initialize channel command */
#define MFP_INCH2   0xf0    /* Initialize channel command after start */
#define MFP_NOP     0x03    /* NOP command */
#define MFP_SID     0x80    /* MFP status command */
#define MFP_MSK     0xff    /* Command mask */

/* Status held in u3 */
/* controller/unit address in upper 16 bits */
#define CON_INPUT   0x100   /* Input ready for unit */
#define CON_CR      0x200   /* Output at beginning of line */
#define CON_REQ     0x400   /* Request key pressed */
#define CON_EKO     0x800   /* Echo input character */
#define CON_OUTPUT  0x1000  /* Output ready for unit */
#define CON_READ    0x2000  /* Read mode selected */

/* not used u4 */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000              /* Command reject */
#define SNS_INTVENT 0x40000000              /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80                    /* device ready */
#define SNS_ONLN    0x40                    /* device online */

/* std devices. data structures

    mfp_dev     Console device descriptor
    mfp_unit    Console unit descriptor
    mfp_reg     Console register list
    mfp_mod     Console modifiers list
*/

struct _mfp_data
{
    uint8       ibuff[145];                 /* Input line buffer */
    uint8       incnt;                      /* char count */
}
mfp_data[NUM_UNITS_MFP];

/* channel program information */
CHANP           mfp_chp[NUM_UNITS_MFP] = {0};

MTAB            mfp_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV",
        &set_dev_addr, &show_dev_addr, NULL, "Controller Channel address"},
    {0}
};

UNIT            mfp_unit[] = {
    {UDATA(&mfp_srv, UNIT_MFP, 0), 0, UNIT_ADDR(0x7600)}, /* Channel controller */
};

//DIB mfp_dib = {NULL, mfp_startcmd, NULL, NULL, NULL, mfp_ini, mfp_unit, mfp_chp, NUM_UNITS_MFP, 0xff, 0x7600,0,0,0};
DIB             mfp_dib = {
    mfp_preio,      /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    mfp_startcmd,   /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    NULL,           /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O HIO */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O HIO */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O TIO */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    mfp_rschnlio,   /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    mfp_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    mfp_unit,       /* UNIT* units */                           /* Pointer to units structure */
    mfp_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_MFP,  /* uint8 numunits */                        /* number of units defined */
    0xff,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7600,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          mfp_dev = {
    "MFP", mfp_unit, NULL, mfp_mod,
    NUM_UNITS_MFP, 8, 15, 1, 8, 8,
    NULL, NULL, &mfp_reset,                 /* examine, deposit, reset */
    NULL, NULL, NULL,                       /* boot, attach, detach */
    /* dib ptr, dev flags, debug flags, debug */
    &mfp_dib, DEV_CHAN|DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, dev_debug,
};

/* MFP controller routines */
/* initialize the console chan/unit */
void mfp_ini(UNIT *uptr, t_bool f)
{
    int     unit = (uptr - mfp_unit);       /* unit 0 */
    DEVICE *dptr = &mfp_dev;                /* one and only dummy device */

    mfp_data[unit].incnt = 0;               /* no input data */
    uptr->u5 = SNS_RDY|SNS_ONLN;            /* status is online & ready */
    sim_cancel(uptr);                       /* stop any timers */
    sim_debug(DEBUG_CMD, &mfp_dev,
        "MFP init device %s controller/device %04x SNS %08x\n",
        dptr->name, GET_UADDR(uptr->u3), uptr->u5);
}

/* handle rschnlio cmds for disk */
t_stat  mfp_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->u3);
    int     cmd = uptr->u3 & MFP_MSK;

    sim_debug(DEBUG_EXP, dptr,
        "mfp_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    mfp_ini(uptr, 0);                       /* reset the unit */
    return SCPE_OK;
}

/* start an mfp operation */
t_stat mfp_preio(UNIT *uptr, uint16 chan) {
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    uint16      chsa = GET_UADDR(uptr->u3);

    sim_debug(DEBUG_CMD, dptr, "mfp_preio CMD %08x unit %02x chsa %04x\n",
        uptr->u3, unit, chsa);

    if ((uptr->u3 & MFP_MSK) != 0) {        /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "mfp_preio unit %02x chsa %04x BUSY\n", unit, chsa);
        return SNS_BSY;                     /* yes, return busy */
    }

    sim_debug(DEBUG_CMD, dptr, "mfp_preio unit %02x chsa %04x OK\n", unit, chsa);
    return SCPE_OK;                         /* good to go */
}

/* start an I/O operation */
t_stat mfp_startcmd(UNIT *uptr, uint16 chan, uint8 cmd)
{
    sim_debug(DEBUG_CMD, &mfp_dev,
        "MFP startcmd %02x controller/device %04x\n",
        cmd, GET_UADDR(uptr->u3));
    if ((uptr->u3 & MFP_MSK) != 0)          /* is unit busy */
        return SNS_BSY;                     /* yes, return busy */

    /* process the commands */
    switch (cmd & 0xFF) {
    /* UTX uses the INCH cmd to detect the MFP or MFP */
    /* MFP has INCH cmd of 0, while MFP uses 0x80 */
    case MFP_INCH:                          /* INCH command */
        uptr->u5 = SNS_RDY|SNS_ONLN;        /* status is online & ready */
        uptr->u3 &= LMASK;                  /* leave only chsa */
        sim_debug(DEBUG_CMD, &mfp_dev,
            "mfp_startcmd %04x: Cmd INCH iptr %06x INCHa %06x\n",
            chan, mfp_chp[0].ccw_addr,      /* set inch buffer addr */
            mfp_chp[0].chan_inch_addr);     /* set inch buffer addr */

        mfp_chp[0].chan_inch_addr = mfp_chp[0].ccw_addr;   /* set inch buffer addr */
        mfp_chp[0].base_inch_addr = mfp_chp[0].ccw_addr;   /* set inch buffer addr */
        mfp_chp[0].max_inch_addr = mfp_chp[0].ccw_addr + (128 * 8); /* set last inch buffer addr */

        uptr->u3 |= MFP_INCH2;              /* save INCH command as 0xf0 */
        sim_activate(uptr, 40);             /* go on */
        return 0;                           /* no status change */
        break;

    case MFP_NOP:                           /* NOP command */
        sim_debug(DEBUG_CMD, &mfp_dev, "mfp_startcmd %04x: Cmd NOP\n", chan);
        uptr->u5 = SNS_RDY|SNS_ONLN;        /* status is online & ready */
        uptr->u3 &= LMASK;                  /* leave only chsa */
        uptr->u3 |= (cmd & MFP_MSK);        /* save NOP command */
        sim_activate(uptr, 40);             /* TRY 07-13-19 */
        return 0;                           /* no status change */
        break;

    case MFP_SID:                           /* status ID command */
        sim_debug(DEBUG_CMD, &mfp_dev, "mfp_startcmd %04x: Cmd SID\n", chan);
        uptr->u5 = SNS_RDY|SNS_ONLN;        /* status is online & ready */
        uptr->u3 &= LMASK;                  /* leave only chsa */
        uptr->u3 |= (cmd & MFP_MSK);        /* save SID command */
        sim_activate(uptr, 40);             /* TRY 07-13-19 */
        return 0;                           /* no status change */
        break;

    default:                                /* invalid command */
        uptr->u5 |= SNS_CMDREJ;             /* command rejected */
        sim_debug(DEBUG_CMD, &mfp_dev, "mfp_startcmd %04x: Cmd Invalid %02x status %02x\n",
            chan, cmd, uptr->u5);
        uptr->u3 &= LMASK;                  /* leave only chsa */
        uptr->u3 |= (cmd & MFP_MSK);        /* save command */
        sim_activate(uptr, 40);             /* force interrupt */
        return 0;                           /* no status change */
        break;
    }

    return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* not reachable for now */
}

/* Handle transfers for other sub-channels on MFP */
t_stat mfp_srv(UNIT *uptr)
{
    uint16  chsa = GET_UADDR(uptr->u3);
    int     cmd = uptr->u3 & MFP_MSK;
    CHANP   *chp = &mfp_chp[0];             /* find the chanp pointer */
    uint32  mema = chp->ccw_addr;           /* get inch or buffer addr */
    uint32  tstart;

    /* test for NOP or INCH cmds */
    if ((cmd != MFP_NOP) && (cmd != MFP_INCH2) && (cmd != MFP_SID)) {   /* NOP, SID or INCH */
        uptr->u3 &= LMASK;                  /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &mfp_dev,
            "mfp_srv Unknown cmd %02x chan %02x: chnend|devend|unitexp\n", cmd, chsa);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* done */
        return SCPE_OK;
    } else

    if (cmd == MFP_NOP) {                   /* NOP do nothing */
        uptr->u3 &= LMASK;                  /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &mfp_dev, "mfp_srv NOP chan %02x: chnend|devend\n", chsa);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;
    } else

    /* 3 status wds are to be returned */
    /* Wd 1 MMXXXXXX board model # assume 00 00 08 02*/
    /* Wd 2 MMXXXXXX board firmware model # assume 00 00 08 02*/
    /* Wd 3 MMXXXXXX board firmware revision # assume 00 00 00 14*/
    if (cmd == MFP_SID) {                   /* send 12 byte Status ID data */
        uint8   ch;

        /* Word 0 */        /* board mod 4324724 = 0x0041fd74 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 0 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 1 */
        ch = 0x81;
        chan_write_byte(chsa, &ch);         /* write byte 2 */
        ch = 0x02;
        chan_write_byte(chsa, &ch);         /* write byte 3 */

        /* Word 1 */        /* firmware 4407519 = 0x004340df */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 4 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 5 */
        ch = 0x80;
        chan_write_byte(chsa, &ch);         /* write byte 6 */
        ch = 0x02;
        chan_write_byte(chsa, &ch);         /* write byte 7 */

        /* Word 2 */        /* firmware rev 4259588 = 0x0040ff04 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 8 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 9 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);         /* write byte 10 */
        ch = 0x14;
        chan_write_byte(chsa, &ch);         /* write byte 11 */

        uptr->u3 &= LMASK;                  /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &mfp_dev, "mfp_srv SID chan %02x: chnend|devend\n", chsa);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;
    } else

    /* test for INCH cmd */
    if (cmd == MFP_INCH2) {                 /* INCH */
        sim_debug(DEBUG_CMD, &mfp_dev,
            "mfp_srv starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            mema, chsa, chp->ccw_addr, chp->ccw_count);

        /* now call set_inch() function to write and test inch buffer addresses */
        /* the chp->ccw_addr location contains the inch address */
        /* 1-256 wd buffer is provided for 128 status dbl words */
        tstart = set_inch(uptr, mema, 128); /* new address of 128 entries */
        if ((tstart == SCPE_MEM) || (tstart == SCPE_ARG)) { /* any error */
            /* we have error, bail out */
            uptr->u5 |= SNS_CMDREJ;
            uptr->u3 &= LMASK;              /* nothing left, command complete */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
        uptr->u3 &= LMASK;                  /* clear the cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
    }
    return SCPE_OK;
}

t_stat mfp_reset(DEVICE *dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* sho help mfp */
t_stat mfp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
    fprintf(st, "SEL-32 MFP Model 8002 Channel Controller at 0x7600\r\n");
    fprintf(st, "The MFP fields all interrupts and status posting\r\n");
    fprintf(st, "for each of the controllers on the system.\r\n");
    fprintf(st, "Nothing can be configured for this Channel.\r\n");
    return SCPE_OK;
}

const char *mfp_desc(DEVICE *dptr)
{
    return("SEL-32 MFP Model 8002 Channel Controller @ 0x7600");
}

#endif

