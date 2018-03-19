/* i7070_chan.c: IBM 7070 Channel simulator

   Copyright (c) 2005-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   channel

   The system state for the IBM 7070 channel is:
   There are 4 types of channel:
        PIO:            Basic polled mode transfer. Channel only manages
                        status and disconnect of devices.
       7604:            Basic channel.
       7907:            Enhanced channel for disk, hypertape and com controlers.

   Common registers to all but PIO channels.
   ADDR<0:16>           Location to read or write next word from.
   CMD<0:6>             Channel command.
   LIMIT<0:16>          Transfer limit
   ASM<0:44>            Assembled data from devices.
   LOCATION<0:16>       Address of next command.

   Simulation registers to handle device handshake.
   STATUS<0:16>         Simulated register for basic channel status.
   SENSE<0:16>          Additional flags for 7907 channels.
*/

#include "i7070_defs.h"

extern UNIT         cpu_unit;

#define CHAN_DEF        UNIT_DISABLE|CHAN_SET

t_stat              chan_reset(DEVICE * dptr);
void                chan_fetch(int chan);
t_stat              chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *chan_description (DEVICE *dptr);


/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

uint32              location[NUM_CHAN];         /* Location of RDW instruction*/
uint32              caddr[NUM_CHAN];            /* Channel memory address */
uint8               bcnt[NUM_CHAN];             /* Channel character count */
uint8               cmd[NUM_CHAN];              /* Current command */
uint8               op[NUM_CHAN];               /* Operators for 7907 channel */
uint32              limit[NUM_CHAN];            /* Word count */
t_uint64            assembly[NUM_CHAN];         /* Assembly register */
uint32              chan_flags[NUM_CHAN];       /* Unit status */
uint32              chan_info[NUM_CHAN];        /* Private channel info */
uint8               chan_irq[NUM_CHAN];         /* Channel has a irq pending */
extern uint16       pri_latchs[10];

#define CHAN_OUTDEV     0x010000        /* Type out device */
#define CHAN_PRIO       0x008000        /* Channel has priority pending */
#define CHAN_TWE        0x004000        /* Channel format error */
#define CHAN_SEOR       0x002000        /* Channel saw a eor */
#define CHAN_NORDW      0x020000        /* No RDW for this command */
#define CHAN_SEOS       0x040000        /* Channel saw a end of segment */
#define CHAN_SCLR       0x080000        /* Short record */
#define CHAN_FIRST      0x100000        /* First tranfered word */
#define CHAN_START      0x200000        /* Channel has just started */
#define CHAN_OCTAL      0x400000        /* Octal conversion */

const char     *chan_type_name[] = {
    "Polled", "Unit Record", "7604", "7907", ""};


UNIT                chan_unit[] = {
    /* Puesdo channel for PIO devices */
    {UDATA(NULL, CHAN_SET|CHAN_S_TYPE(CHAN_UREC)|UNIT_S_CHAN(CHAN_CHUREC),0)},
    /* Normal channels */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_A)|CHAN_S_TYPE(CHAN_7604),0)},/* A */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_B)|CHAN_S_TYPE(CHAN_7604),0)},/* B */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_C)|CHAN_S_TYPE(CHAN_7604),0)},/* C */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_D)|CHAN_S_TYPE(CHAN_7604),0)},/* D */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_E)|CHAN_S_TYPE(CHAN_7907),0)},/* E */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_F)|CHAN_S_TYPE(CHAN_7907),0)},/* F */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_G)|CHAN_S_TYPE(CHAN_7907),0)},/* G */
    {UDATA(NULL, CHAN_DEF|UNIT_S_CHAN(CHAN_H)|CHAN_S_TYPE(CHAN_7907),0)} /* H */
};

REG                 chan_reg[] = {
    {BRDATA(ADDR, caddr, 10, 18, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(CMD, cmd, 8, 6, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(LIMIT, limit, 10, 18, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(ASM, assembly, 16, 44, NUM_CHAN), REG_VMIO|REG_RO|REG_FIT},
    {BRDATA(LOCATION, location, 10, 18, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(FLAGS, chan_flags, 2, 32, NUM_CHAN), REG_RO|REG_FIT},
    {NULL}
};

MTAB                chan_mod[] = {
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_UREC), "UREC", NULL, NULL,NULL,NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7604), "7604", NULL, NULL, NULL, NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7907), "7907", NULL, NULL, NULL, NULL},
    {MTAB_VUN, 0, "UNITS", NULL, NULL, &print_chan, NULL, "Show units on channel"},
    {0}
};

/* Simulator debug controls */
DEBTAB              chn_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"TRAP", DEBUG_TRAP},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"SENSE", DEBUG_SNS},
    {"CH0", 0x0100 << 0},
    {"CH1", 0x0100 << 1},
    {"CH2", 0x0100 << 2},
    {"CH3", 0x0100 << 3},
    {"CH4", 0x0100 << 4},
    {"CHA", 0x0100 << 5},
    {"CHB", 0x0100 << 6},
    {"CHC", 0x0100 << 7},
    {"CHD", 0x0100 << 8},
    {0, 0}
};

DEVICE              chan_dev = {
    "CH", chan_unit, chan_reg, chan_mod,
    NUM_CHAN, 10, 18, 1, 10, 44,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, chn_debug,
    NULL, NULL, &chan_help, NULL, NULL, &chan_description
};

#define DELTA_CHAR              057
#define SM_CHAR                 037
#define SM_MEM                  0x39
#define RM_CHAR                 0x80

/* Translation tables */
uint8   bcd_mem[64] = {
        /*  ?     1     2     3     4     5     6     7 */
/* 00 */ 0x00, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        /*  8     9     0   =/#   !/@     ?     ?    tm */
/* 10 */ 0x98, 0x99, 0x90, 0x45, 0x46, 0x47, 0x48, 0x49,
        /* sp     /     S     T     U     V     W     X */
/* 20 */ 0x60, 0x31, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        /*  Y     Z    rm     ,   %/(     ?     ?    sm */
/* 30 */ 0x88, 0x89, 0x80, 0x35, 0x36, 0x37, 0x38, 0x39,
        /*  -     J     K     L     M     N     O     P */
/* 40 */ 0x30, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
        /*  Q     R    -0     $     *     ?     ?   del */
/* 50 */ 0x78, 0x79, 0x70, 0x25, 0x26, 0x27, 0x28, 0xFF,
        /*+/&     A     B     C     D     E     F     G */
/* 60 */ 0x20, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
        /*  H     I    +0     .    sq     ?     ?    gm */
/* 70 */ 0x68, 0x69, 0x60, 0x15, 0x16, 0x17, 0x18, 0x19
};

uint8   mem_bcd[256] = {
        /* sp                                      */
/* 00 */  020, 000, 000, 000, 000, 000, 000, 000,
        /*                                         */
/* 08 */  000, 000, 000, 000, 000, 000, 000, 000,
        /*                               sq    ?   */
/* 10 */  000, 000, 000, 000, 000, 073, 074, 075,
        /*  ?    ?                                 */
/* 18 */  076, 077, 000, 000, 000, 000, 000, 000,
        /*+/-                        $    *    ?   */
/* 20 */  060, 000, 000, 000, 000, 053, 054, 055,
        /*  ?  +/-                                 */
/* 28 */  056, 060, 000, 000, 000, 000, 000, 000,
        /*  -    /                   ,  %/(    ?   */
/* 30 */  040, 021, 000, 000, 000, 033, 034, 035,
        /*  ?   sm                                 */
/* 38 */  036, 037, 000, 000, 000, 000, 000, 000,
        /*                         =/#  !/@    ?   */
/* 40 */  000, 000, 000, 000, 000, 013, 014, 015,
        /*  ?   tm                                 */
/* 48 */  016, 017, 000, 000, 000, 000, 000, 000,
        /*                                         */
/* 50 */  000, 000, 000, 000, 000, 000, 000, 000,
        /*                                         */
/* 58 */  000, 000, 000, 000, 000, 000, 000, 000,
        /* +0    A    B    C    D    E    F    G   */
/* 60 */  072, 061, 062, 063, 064, 065, 066, 067,
        /*  H    I                                 */
/* 68 */  070, 071, 000, 000, 000, 000, 000, 000,
        /* -0    J    K    L    M    N    O    P   */
/* 70 */  052, 041, 042, 043, 044, 045, 046, 047,
        /*  Q    R                                 */
/* 78 */  050, 051, 000, 000, 000, 000, 000, 000,
        /* rm         S    T    U    V    W    X   */
/* 80 */  032, 000, 022, 023, 024, 025, 026, 027,
        /*  Y    Z                                 */
/* 88 */  030, 031, 000, 000, 000, 000, 000, 000,
        /*  0    1    2    3    4    5    6    7   */
/* 90 */  012, 001, 002, 003, 004, 005, 006, 007,
        /*  8    9                                 */
/* 98 */  010, 011, 000, 000, 000, 000, 000, 000
};


t_stat
chan_reset(DEVICE * dptr)
{
    int                 i;

    /* Clear channel assignment */
    for (i = 0; i < NUM_CHAN; i++) {
        chan_flags[i] = 0;
        chan_info[i] = 0;
        caddr[i] = 0;
        cmd[i] = 0;
        bcnt[i] = 10;
        chan_irq[i] = 0;
        limit[i] = 0;
        location[i] = 0;
    }
    return chan_set_devs(dptr);
}

/* Boot from given device */
t_stat
chan_boot(int32 unit_num, DEVICE * dptr)
{
    return SCPE_NOFNC;  /* Not implimented until I know how boot work */
}

t_stat
chan_issue_cmd(uint16 chan, uint16 dcmd, uint16 dev) {
    DEVICE            **dptr;
    DIB                *dibp;
    uint32              j;
    UNIT               *uptr;

    for (dptr = sim_devices; *dptr != NULL; dptr++) {
        int                 r;

        dibp = (DIB *) (*dptr)->ctxt;
        /* If no DIB, not channel device */
        if (dibp == 0)
            continue;
        uptr = (*dptr)->units;
        /* If this is a 79XX device, check it */
        if (dibp->ctype & CH_TYP_79XX) {
            for (j = 0; j < (*dptr)->numunits; j++, uptr++) {
                if ((uptr->flags & UNIT_DIS) == 0 &&
                    UNIT_G_CHAN(uptr->flags) == chan &&
                    (dev == ((UNIT_SELECT & uptr->flags) != 0))) {
                    r = dibp->cmd(uptr, dcmd, dev);
                    if (r != SCPE_NODEV)
                        return r;
                }
            }
        } else if ((dibp->addr & dibp->mask) == (dev & dibp->mask)) {
            if (dibp->upc == 1) {
                for (j = 0; j < (*dptr)->numunits; j++) {
                    if ((uptr->flags & UNIT_DIS) == 0 &&
                        UNIT_G_CHAN(uptr->flags) == chan) {
                        r = dibp->cmd(uptr, dcmd, dev);
                        if (r != SCPE_NODEV)
                            return r;
                    }
                    uptr++;
                }
            } else {
                if ((uptr->flags & UNIT_DIS) == 0 &&
                        UNIT_G_CHAN(uptr->flags) == chan) {
                    r = dibp->cmd(uptr, dcmd, dev);
                    if (r != SCPE_NODEV)
                        return r;
                }
            }
        }
    }
    return SCPE_NODEV;
}

/* Execute the next channel instruction. */
void
chan_proc()
{
    int                 chan;
    int                 cmask;

    /* Scan channels looking for work */
    for (chan = 0; chan < NUM_CHAN; chan++) {
        /* Skip if channel is disabled */
        if (chan_unit[chan].flags & UNIT_DIS)
            continue;

        cmask = 0x0100 << chan;
        switch (CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_UREC:
        case CHAN_7604:
            /* If channel is disconnecting, do nothing */
            if (chan_flags[chan] & DEV_DISCO)
                continue;

            /* If device requested attention, abort current command */
            if (chan_flags[chan] & CHS_ATTN) {
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_EXP, &chan_dev, "chan %d Attn\n",
                              chan);
                chan_flags[chan] &=
                    ~(CHS_ATTN | STA_START | STA_ACTIVE | STA_WAIT);
                /* Disconnect if selected */
                if (chan_flags[chan] & DEV_SEL)
                    chan_flags[chan] |= (DEV_DISCO);
                continue;
            }

            /* If no select, stop channel */
            if ((chan_flags[chan] & DEV_SEL) == 0
                && (chan_flags[chan] & STA_TWAIT)) {
                t_uint64        temp;
                int             adr;
chan_trap:
                if (chan != 0) {
                    adr = 100 + (chan * 10) + (chan_info[chan] & 0xf);
                    temp = 2;
                    if (chan_info[chan] & CHAN_TWE)
                       temp = 0;
                    else if (chan_flags[chan] & CHS_ERR)
                       temp = 1;
                    else if (chan_flags[chan] & CHS_EOF)
                       temp = 5;
                    else if (chan_info[chan] & CHAN_SEOS)
                       temp = 6;
                    else if (chan_info[chan] & CHAN_SCLR)
                       temp = 7;
                    else if ((chan_info[chan] & CHAN_NORDW) == 0) {
                        if ((chan_info[chan] & CHAN_SEOR) == 0 &&
                                caddr[chan] > limit[chan])
                            temp = 4;
                         else if (caddr[chan] < limit[chan])
                            temp = 3;
                    }
                    chan_flags[chan] &= ~(CHS_ERR|CHS_EOF);
                    temp <<= 32;
                    if (chan_info[chan] & CHAN_NORDW) {
                        temp |= M[adr] & 0xFFFFFFFFLL;
                    } else {
                        upd_idx(&temp, caddr[chan]);
                        bin_dec(&temp, location[chan], 0, 4);
                    }
                    temp |= PSIGN;
                    /* Copy over flag */
                    temp |= M[adr] & 0xF000000000LL;
                    if (chan_dev.dctrl & cmask)
                           sim_debug(DEBUG_TRAP, &chan_dev,
                                 "chan %d Trap: %012llx prio=%d\n\r", chan,
                                         temp, (chan_info[chan]&CHAN_PRIO)?1:0);
                    M[adr] = temp;
                    if ((chan_info[chan] & CHAN_PRIO) ||
                                         ((temp >> 32) & 0xf) != 2)
                         pri_latchs[chan] |= 1 << (chan_info[chan] & 0xF);
                    chan_info[chan] &= ~CHAN_PRIO;
                } else if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_TRAP, &chan_dev, "chan %d Trap %04x\n",
                              chan, chan_info[chan]);
                chan_flags[chan] &=
                    ~(STA_START | STA_ACTIVE | STA_WAIT | STA_TWAIT);
                continue;
            }

            /* No activity, nothing happening here folks, move along */
            if ((chan_flags[chan] & (STA_ACTIVE | STA_WAIT)) == 0) {
                /* This could be a no data command, if pending priorty
                   request, ask device if it is ready */
                if ((cmd[chan] & CHN_SEGMENT) == 0 &&
                     chan_info[chan] & CHAN_PRIO &&
                     chan_issue_cmd(chan, IO_TRS, chan_info[chan]&0xf)
                                                        == SCPE_OK)
                   goto chan_trap;
                continue;
            }

            /* If first time through here, load up channel control */
            if (chan_flags[chan] & STA_ACTIVE && chan_info[chan] & CHAN_START)
                chan_fetch(chan);

            /* Process reading of a segment command */
            if ((cmd[chan] & (CHN_SEGMENT|CHN_RM_FND)) ==
                                (CHN_SEGMENT|CHN_RM_FND)) {
              /* Two backspaces and a read */
              switch (cmd[chan] & (CHN_RM_FND|CHN_NUM_MODE|CHN_COMPRESS)) {
                 case CHN_RM_FND:
                        if (chan_issue_cmd(chan, IO_BSR,
                                 chan_info[chan]&0xf) == SCPE_OK) {
                            cmd[chan] |= CHN_COMPRESS;
                         if (chan_dev.dctrl & cmask)
                              sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "segment %d bsr 2\n\r", chan);
                        }
                        break;
                 case CHN_RM_FND|CHN_COMPRESS:
                        if (chan_issue_cmd(chan, IO_BSR,
                                 chan_info[chan]&0xf) == SCPE_OK) {
                             cmd[chan] |= CHN_NUM_MODE;
                             if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DETAIL, &chan_dev,
                                             "segment %d bsr 2\n\r", chan);
                             if (chan_flags[chan] & CHS_BOT) {
                                  chan_flags[chan] &= ~(STA_ACTIVE);
                                  goto chan_trap;
                             }
                        }
                        break;
                 case CHN_RM_FND|CHN_NUM_MODE|CHN_COMPRESS:
                        chan_info[chan] &= ~(CHAN_SEOS|CHAN_FIRST);
                        if ( chan_issue_cmd(chan, IO_RDS,
                                 chan_info[chan]&0xf) == SCPE_OK) {
                           cmd[chan] &= ~(CHN_NUM_MODE|CHN_COMPRESS|CHN_RM_FND);
                         if (chan_dev.dctrl & cmask)
                              sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "segment %d read\n\r", chan);
                            chan_flags[chan] &= ~(STA_WAIT|DEV_REOR);
                        }
                        break;
                }
                /* Device ready, decide command to issue */
                if (cmd[chan] & CHN_RECORD) {
                    if (chan_flags[chan] & CHS_BOT) {
                        chan_flags[chan] |= STA_TWAIT;
                    }
                    /* Two backspaces and a read */
                } else if (chan_flags[chan] & CHS_EOT) {
                        chan_flags[chan] |= STA_TWAIT;
                }
                continue;
            }

            /* None disabled, active channel is if transfering */
            switch (chan_flags[chan] & (DEV_WRITE | DEV_FULL)) {
                /* Device has given us a dataword */
            case DEV_FULL:
                /* Process reading of a segment command */
                 if (cmd[chan] & CHN_SEGMENT) {
                     /* Check if hit end of record */
#if 0           /* Check segment operation correct before removing */
                    if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev,
                                         "chk segment %d %d data = %012llx",
                                         chan, bcnt[chan], assembly[chan]);

                    if ((chan_flags[chan] & (STA_WAIT|DEV_REOR))
                                == (STA_WAIT|DEV_REOR)) {
                        chan_flags[chan] &= ~(STA_WAIT|DEV_REOR|DEV_FULL);
                        chan_info[chan] &= ~(CHAN_SEOS|CHAN_FIRST);
                        continue;
                    }

                    if (bcnt[chan] >= 6 && chan_flags[chan] & DEV_REOR) {
                       if (chan_info[chan] & CHAN_SEOS) {
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev, " found\n");
                            /* Correct error */
                            chan_info[chan] &= ~(CHAN_TWE|CHAN_SEOS|CHAN_SCLR);
                            /* What we were looking for? */
                            if (caddr[chan] >= limit[chan]) {
                                chan_flags[chan] &= ~(STA_ACTIVE|CHS_EOF);
                                chan_flags[chan] |= STA_TWAIT;
                                chan_info[chan] |= CHAN_SEOR;
                            } else
                                caddr[chan]++;
                        }
                        chan_info[chan] &= ~(CHAN_SEOS|CHAN_FIRST|CHAN_TWE);
                        chan_flags[chan] &= ~(DEV_FULL|DEV_REOR|CHS_ERR);
                    } else
                        if (chan_dev.dctrl & cmask)
                             sim_debug(DEBUG_DETAIL, &chan_dev, " search\n");
                    /* How about regular record */
                    chan_flags[chan] &= ~DEV_FULL;
                    /* Wait for next record */
                    chan_flags[chan] |= STA_WAIT|DEV_DISCO;
                    if (cmd[chan] & CHN_RECORD)
                        cmd[chan] |= CHN_RM_FND;
                    else
                        cmd[chan] |= CHN_RM_FND|CHN_COMPRESS|CHN_NUM_MODE;
                    assembly[chan] = 0;
                    bcnt[chan] = 10;
#endif
                    continue;
                }

                /* If we are not waiting EOR save it in memory */
                if ((chan_flags[chan] & STA_WAIT) == 0) {
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DATA, &chan_dev,
                                  "chan %d data < %011llx\n",
                                  chan, assembly[chan]);
                    if (caddr[chan] < MEMSIZE)
                        M[caddr[chan]] = assembly[chan];

                    if (bcnt[chan] != 0)
                        chan_info[chan] |= CHAN_SCLR;
                    else
                        chan_info[chan] &= ~CHAN_SCLR;
                    /* Check if last transfer before final */
                    if (caddr[chan] >= limit[chan] && cmd[chan] & CHN_LAST) {
                        chan_flags[chan] &= ~(STA_ACTIVE);
                        chan_flags[chan] |= STA_TWAIT|STA_WAIT;
                    } else
                        caddr[chan]++;

                    /* Update channel status word */
                    if (chan != 0 && (chan_info[chan] & CHAN_NORDW) == 0) {
                        int     adr = 100 + (chan * 10) + (chan_info[chan]&0xf);
                        upd_idx(&M[adr], caddr[chan]);
                        bin_dec(&M[adr], location[chan], 0, 4);
                    }

                    /* Check for record mark */
                    if ((cmd[chan] & CHN_RECORD) &&
                        (assembly[chan] & SMASK) == ASIGN &&
                        (assembly[chan] & 0xFF) == RM_CHAR) {
                        if (cmd[chan] & CHN_LAST) {
                            chan_flags[chan] &= ~(STA_ACTIVE);
                            chan_flags[chan] |= STA_TWAIT|STA_WAIT;
                        } else
                            chan_fetch(chan);
                    }
                    bcnt[chan] = 10;
                    assembly[chan] = 0;
                }
                chan_info[chan] |= CHAN_FIRST;  /* Saved one char */
                chan_flags[chan] &= ~DEV_FULL;

                /* Device does not need a word and has not given us one */
            case 0:
                if (chan_flags[chan] & DEV_REOR) {
                    /* Check EOR at end of segment */
                    if (cmd[chan] & CHN_SEGMENT) {
                        if ((chan_info[chan] & CHAN_FIRST) == 0 &&
                           bcnt[chan] == 8 &&
                          assembly[chan] == (ASIGN|(((t_uint64)SM_MEM) << 32))){
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "chk segment %d %d data = %012llx found\n\r",
                                         chan, bcnt[chan], assembly[chan]);

                            /* What we were looking for? */
                            if (caddr[chan] >= limit[chan]) {
                                chan_flags[chan] &= ~(STA_ACTIVE|CHS_EOF);
                                chan_flags[chan] |= STA_TWAIT;
                            } else
                                caddr[chan]++;
                        } else
                            /* Check if hit end of record */
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "chk segment %d %d data = %012llx search\n\r",
                                         chan, bcnt[chan], assembly[chan]);
                        /* Correct error */
                        chan_info[chan] &= ~(CHAN_TWE|CHAN_SEOS|CHAN_SCLR);

                        assembly[chan] = 0;
                        bcnt[chan] = 10;
                        chan_flags[chan] &= ~(DEV_REOR|DEV_FULL);
                        /* Wait for next record */
                        chan_flags[chan] |= STA_WAIT|DEV_DISCO;
                        if (cmd[chan] & CHN_RECORD)
                            cmd[chan] |= CHN_RM_FND;
                        else
                            cmd[chan] |= CHN_RM_FND|CHN_COMPRESS|CHN_NUM_MODE;
                        continue;
                    }
                /* Device idle, expecting data from it */
                /* Check if got EOR */
                    chan_flags[chan] &= ~(DEV_REOR|STA_ACTIVE|STA_WAIT);
                    chan_flags[chan] |= STA_TWAIT|DEV_DISCO;
                    chan_info[chan] |= CHAN_SEOR;
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_EXP, &chan_dev, "chan %d EOR< %o\n",
                                  chan, cmd[chan]);
                    continue;
                }
                if (caddr[chan] > limit[chan]
                    && (chan_flags[chan] & STA_WAIT) == 0) {
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_EXP, &chan_dev,
                                  "chan %d < WC0 %o\n", chan, cmd[chan]);
                    if (cmd[chan] & CHN_LAST) {
                        chan_flags[chan] &= ~(STA_ACTIVE);
                        chan_flags[chan] |= STA_TWAIT|STA_WAIT|DEV_DISCO;
                        chan_info[chan] &= ~CHAN_SEOR;
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_EXP, &chan_dev,
                                      "chan %d < DISCO\n", chan);
                    } else
                        chan_fetch(chan);
                }
                break;

                /* Device has word, but has not taken it yet */
            case DEV_WRITE | DEV_FULL:
                continue;       /* Do nothing if no data xfer pending */

                /* Device needs a word of data */
            case DEV_WRITE:     /* Device needs data word */
                /* If we are waiting on EOR, do nothing */
                if (chan_flags[chan] & STA_WAIT)
                     continue;

                /* Special for write segment mark command */
                if (cmd[chan] & CHN_SEGMENT) {
                     /* Send one char */
                     assembly[chan] = SM_MEM;
                     bcnt[chan] = 2;
                     caddr[chan] = limit[chan]+1;
                     chan_flags[chan] &= ~(STA_ACTIVE);
                     chan_flags[chan] |= STA_TWAIT|STA_WAIT|DEV_FULL|DEV_WEOR;
                     cmd[chan] = CHN_ALPHA|CHN_SEGMENT;
                     chan_info[chan] |= CHAN_NORDW;
                     continue;
                }
                /* Give device new word if we have one */
                if (caddr[chan] <= limit[chan]) {
                    /* Check if got EOR */
                    if (chan_flags[chan] & DEV_REOR) {
                            chan_flags[chan] &= ~(STA_WAIT | DEV_REOR
                                                        | STA_ACTIVE);
                            chan_flags[chan] |= STA_TWAIT;
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_EXP, &chan_dev,
                                    "chan %d EOR> %o\n", chan, cmd[chan] & 070);
                            continue;
                    }

                    if (caddr[chan] < MEMSIZE)
                        assembly[chan] = M[caddr[chan]];
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DATA, &chan_dev,
                                      "chan %d data > %011llx\n", chan,
                              assembly[chan]);
                    bcnt[chan] = 10;
                    chan_flags[chan] |= DEV_FULL;

                    /* Check if last transfer before final */
                    if (caddr[chan] >= limit[chan] && cmd[chan] & CHN_LAST) {
                        chan_flags[chan] &= ~(STA_ACTIVE);
                        chan_flags[chan] |= STA_TWAIT|STA_WAIT;
                    } else
                        caddr[chan]++;

                    /* Update channel status word */
                    if (chan != 0 && (chan_info[chan] & CHAN_NORDW) == 0) {
                        int     adr = 100 + (chan * 10) + (chan_info[chan]&0xf);
                        upd_idx(&M[adr], caddr[chan]);
                        bin_dec(&M[adr], location[chan], 0, 4);
                    }

                   /* Check for record mark */
                    if ((cmd[chan] & CHN_RECORD) &&
                        (assembly[chan] & SMASK) == ASIGN &&
                        (assembly[chan] & 0xFF) == RM_CHAR) {
                        if (cmd[chan] & CHN_LAST) {
                            chan_flags[chan] &= ~(STA_ACTIVE);
                            chan_flags[chan] |= STA_TWAIT|STA_WAIT;
                        } else
                            chan_fetch(chan);
                    }

                    continue;   /* Don't start next command until data taken */
                }

                /* Wait for device to recognize EOR */
                if (chan_flags[chan] & DEV_WEOR)
                    continue;

                /* Get here if passed limit */
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_EXP, &chan_dev, "chan %d > WC0 %o stat=%x\n",
                              chan, cmd[chan] & 070, chan_flags[chan]);

                if (cmd[chan] & CHN_LAST) {
                    chan_flags[chan] |= DEV_DISCO | DEV_WEOR | STA_TWAIT;
                    chan_flags[chan] &= ~(STA_START | STA_ACTIVE);
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_EXP, &chan_dev,
                                  "chan %d > DISCO\n", chan);
                 } else
                    chan_fetch(chan);
            }
            break;
        case CHAN_7907:
            /* If channel is disconnecting, just hold on */
            if (chan_flags[chan] & DEV_DISCO)
                continue;

            /* If no select, stop channel */
            if ((chan_flags[chan] & DEV_SEL) == 0
                && (chan_flags[chan] & STA_TWAIT)) {
                t_uint64        temp;

                temp = 2;
                if (chan_info[chan] & CHAN_TWE)
                   temp = 1;
                else if (chan_flags[chan] & SNS_UEND)
                   temp = 5;
                else if ((chan_info[chan] & CHAN_SEOR) == 0 && op[chan] == 1)
                   temp = 4;
                else if (caddr[chan] <= limit[chan] &&
                        (op[chan] == 1 || op[chan] == 3))
                   temp = 3;
                temp <<= 36;
                chan_irq[chan] |= chan_flags[chan] & (SNS_ATTN1|SNS_ATTN2);
                temp |= (chan_irq[chan])?MSIGN:PSIGN;
                chan_flags[chan] &= ~(SNS_UEND|CTL_CNTL|CTL_SNS|CTL_READ|
                                        CTL_WRITE|CTL_PREAD|CTL_PWRITE);
                upd_idx(&temp, caddr[chan]);
                bin_dec(&temp, location[chan], 0, 4);
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_TRAP, &chan_dev, "chan %d Trap: %012llx\n",
                              chan, temp);
                M[(chan - 4) + 300] = temp;
                if ((chan_info[chan] & CHAN_PRIO) || ((temp >> 36) & 0xf) != 2)
                      pri_latchs[8] |= 1<<(4-chan);
                chan_flags[chan] &=
                    ~(STA_START | STA_ACTIVE | STA_WAIT | STA_TWAIT);
                chan_info[chan] &= ~CHAN_PRIO;
                continue;
            }

            /* Check if device raised attention */
            if ((chan_flags[chan] & (STA_ACTIVE|DEV_SEL|STA_TWAIT)) == 0 &&
                (chan_flags[chan] & (SNS_ATTN1|SNS_ATTN2))) {
                t_uint64        temp;

                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_TRAP, &chan_dev, "chan %d Attn Trap\n",
                              chan);
                temp = 2;
                if (chan_flags[chan] & SNS_UEND)
                    temp = 5;
                temp <<= 36;
                temp |= MSIGN;
                chan_irq[chan] |= chan_flags[chan] & (SNS_ATTN1|SNS_ATTN2);
                chan_flags[chan] &= ~(SNS_ATTN1|SNS_ATTN2|SNS_UEND);
                upd_idx(&temp, caddr[chan]);
                bin_dec(&temp, location[chan], 0, 4);
                M[(chan - 4) + 300] = temp;
                pri_latchs[9] |= 1 << (4 - chan);
                continue;
            }

            /* Nothing more to do if not active. */
            if (chan_flags[chan] & STA_ACTIVE) {
                t_uint64        temp;

                /* Execute the next command */
                switch (op[chan]) {
                case 6: /* Tranfer in channel */
                        /* I am not sure if this is correct, but it passes
                           diagnostics */
                        location[chan] = limit[chan];
                        break;
                case 0: /* Write status */
                        temp = PSIGN|(2LL << 36);
                        upd_idx(&temp, caddr[chan]);
                        bin_dec(&temp, location[chan], 0, 4);
                        M[caddr[chan]] = temp;
                        break;
                case 1: /* Read */
                        /* Check if in other mode */
                        if (chan_flags[chan] & (CTL_CNTL)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d read busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_REOR) {
                                chan_flags[chan] &=
                                        ~(CTL_CNTL|DEV_REOR|DEV_WRITE);
                            } else
                                continue;
                        }
                        /* Check if last command not finished */
                        if (chan_flags[chan] & (CTL_SNS|CTL_WRITE)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d read busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_SEL) {
                                chan_flags[chan] |= DEV_DISCO;
                                chan_flags[chan] &= ~(CTL_SNS|CTL_WRITE);
                            }
                            continue;
                        }

                        /* If not already in read mode, set read mode */
                        if ((chan_flags[chan] & CTL_READ) == 0) {
                            chan_flags[chan] |= CTL_READ;
                            chan_flags[chan] &= ~(DEV_FULL|DEV_WRITE|DEV_REOR);
                            chan_info[chan] &= ~CHAN_SEOR;
                            bcnt[chan] = 10;
                            assembly[chan] = 0;
                            continue;
                        }

                        /* Has device given us a word */
                        if (chan_flags[chan] & DEV_FULL) {
                            /* Check if record mark */
                            if ((cmd[chan] & CHN_RECORD) &&
                                (assembly[chan] & SMASK) == ASIGN &&
                                (assembly[chan] & 0xFF) == RM_CHAR) {
                                break;
                            }


                            /* Check if ready to transfer something */
                            if (caddr[chan] <= limit[chan]) {
                                M[caddr[chan]] = assembly[chan];
                                if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d data > %012llx\n",
                                              chan, assembly[chan]);
                                caddr[chan]++;
                                bcnt[chan] = 10;
                                assembly[chan] = 0;
                                chan_flags[chan] &= ~DEV_FULL;
                                /* Check if last word transfered */
                                if (caddr[chan] > limit[chan])
                                    break;
                            }

                            /* Check if we still have a select signal */
                            if ((chan_flags[chan] & DEV_SEL) == 0) {
                                chan_info[chan] |= CHAN_TWE;
                                chan_flags[chan] &=
                                        ~(CTL_WRITE|CTL_END|STA_ACTIVE);
                                break;
                            }

                            /* Device gave us a EOR, get next word */
                            if (chan_flags[chan] & DEV_REOR) {
                                chan_info[chan] |= CHAN_SEOR;
                                chan_flags[chan] &= ~DEV_REOR;
                                break;
                            }
                            continue;
                        }

                        /* Abort if we get control end */
                        if (chan_flags[chan] & CTL_END) {
                            /* Disconnect channel if select still active */
                            if (chan_flags[chan] & DEV_SEL) {
                                chan_flags[chan] |= (DEV_DISCO);
                            }
                            if (chan_flags[chan] & DEV_REOR)
                                chan_info[chan] |= CHAN_SEOR;
                            chan_flags[chan] &=
                                 ~(DEV_REOR|CTL_SNS|CTL_READ|CTL_WRITE|CTL_END);
                            break;
                        }
                        continue;

                case 3: /* Write */
                        /* Check if in other mode */
                        if (chan_flags[chan] & (CTL_CNTL)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d write busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_REOR) {
                                chan_flags[chan] &=
                                         ~(CTL_CNTL|DEV_REOR|DEV_WRITE);
                            } else
                                continue;
                        }
                        /* Check if last command not finished */
                        if (chan_flags[chan] & (CTL_SNS|CTL_READ)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d write busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_SEL) {
                                chan_flags[chan] |= DEV_DISCO;
                                chan_flags[chan] &= ~(CTL_READ|CTL_SNS);
                            }
                            continue;
                        }

                        /* If not in write, flag as write */
                        if ((chan_flags[chan] & CTL_WRITE) == 0) {
                            chan_flags[chan] |= CTL_WRITE|DEV_WRITE;
                        }

                        /* Command finished?*/
                        if (chan_flags[chan] & CTL_END) {
                            /* Disconnect channel if select still active */
                            if (chan_flags[chan] & DEV_SEL) {
                                chan_flags[chan] |= (DEV_DISCO);
                            }
                            chan_flags[chan] &=
                                 ~(DEV_REOR|CTL_SNS|CTL_READ|CTL_WRITE|CTL_END);
                            break;
                        }

                        /* Check if we still have a select signal */
                        if ((chan_flags[chan] & DEV_SEL) == 0 &&
                                    caddr[chan] < limit[chan]) {
                            chan_info[chan] |= CHAN_TWE;
                            chan_flags[chan] &= ~(CTL_WRITE|CTL_END|STA_ACTIVE);
                            break;
                        }

                        /* Check if device needs data */
                        if ((chan_flags[chan] & DEV_FULL) == 0) {
                            /* Got EOR? */
                            if (chan_flags[chan] & DEV_REOR) {
                                if (caddr[chan] > limit[chan]) {
                                    chan_info[chan] |= CHAN_SEOR;
                                }
                                chan_flags[chan] |= DEV_DISCO;
                                chan_flags[chan] &= ~DEV_REOR;
                                break;
                            }


                            /* Check if ready to transfer something */
                            if (caddr[chan] <= limit[chan]) {
                                assembly[chan] = M[caddr[chan]];
                                if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d data > %012llx\n",
                                              chan, assembly[chan]);
                                caddr[chan]++;
                                bcnt[chan] = 10;
                                chan_flags[chan] |= DEV_FULL;
                                /* Check if record mark */
                                if ((cmd[chan] & CHN_RECORD) &&
                                    (assembly[chan] & SMASK) == ASIGN &&
                                    (assembly[chan] & 0xFF) == RM_CHAR) {
                                    chan_flags[chan] |= DEV_WEOR;
                                    break;
                                }
                                continue;
                            }
                            chan_info[chan] |= CHAN_SEOR;
                            break;
                        }
                        continue;

                case 5: /* Sense */
                        /* Check if in other mode */
                        if (chan_flags[chan] & (CTL_CNTL)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d sense busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_REOR) {
                                chan_flags[chan] &=
                                        ~(CTL_CNTL|DEV_REOR|DEV_WRITE);
                            } else
                                continue;
                        }
                        if (chan_flags[chan] & CTL_SNS) {
                            /* Check if we still have a select signal */
                            if ((chan_flags[chan] & DEV_SEL) == 0) {
                                chan_info[chan] |= CHAN_TWE;
                                chan_flags[chan] &= ~(CTL_SNS|STA_ACTIVE);
                                chan_flags[chan] |= STA_TWAIT;
                                break;
                            }

                            /* If device ended, quit transfer */
                            if ((chan_flags[chan] & DEV_FULL) == 0) {
                                if (chan_flags[chan] & CTL_END) {
                                /* Disconnect channel if select still active */
                                    if (chan_flags[chan] & DEV_SEL) {
                                        chan_flags[chan] |= (DEV_DISCO);
                                    }
                                    if (chan_flags[chan] & DEV_REOR) {
                                        chan_info[chan] |= CHAN_SEOR;
                                        chan_flags[chan] &= ~DEV_REOR;
                                    }
                                    chan_flags[chan] &= ~(CTL_SNS);
                                    break;
                                }

                                /* Check if last word transfered */
                                if (caddr[chan] > limit[chan]) {
                                    if (chan_flags[chan] & SNS_UEND) {
                                        chan_flags[chan] |=
                                            (DEV_DISCO | DEV_WEOR);
                                        chan_flags[chan] &= ~(DEV_SEL);
                                    } else {
                                        if (chan_flags[chan] & DEV_REOR) {
                                            chan_flags[chan] &= ~DEV_REOR;
                                            chan_info[chan] |= CHAN_SEOR;
                                        }
                                    }
                                    chan_flags[chan] &= ~(CTL_SNS);
                                    break;
                                }
                            } else {
                                /* Device has given us a dataword */
                                 if (chan_dev.dctrl & cmask)
                                     sim_debug(DEBUG_DATA, &chan_dev,
                                               "chan %d data < %012llx\n",
                                               chan, assembly[chan]);
                                 M[caddr[chan]] = assembly[chan];
                                 assembly[chan] = 0;
                                 bcnt[chan] = 10;
                                 chan_flags[chan] &= ~DEV_FULL;
                                 if (caddr[chan] >= limit[chan])
                                     break;
                                 caddr[chan]++;
                            }
                            /* Handle EOR on sense */
                            if (chan_flags[chan] & DEV_REOR) {
                                chan_flags[chan] &= ~(CTL_SNS|DEV_REOR);
                                chan_info[chan] |= CHAN_SEOR;
                                break;
                            }
                            continue;
                        }

                        /* Check if in other mode */
                        if (chan_flags[chan] & (CTL_CNTL|CTL_READ|CTL_WRITE)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d sense busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_SEL) {
                               chan_flags[chan] |= DEV_DISCO|DEV_WEOR|STA_WAIT;
                            }
                            chan_flags[chan] &= ~(CTL_CNTL|CTL_READ|CTL_WRITE);
                            continue;
                        }

                        /* Start sense command */
                        chan_flags[chan] |= CTL_SNS;
                        chan_flags[chan] &= ~(CTL_END|DEV_REOR|DEV_FULL);
                        switch(chan_issue_cmd(chan,0,chan_test(chan, CTL_SEL))){
                        case SCPE_IOERR:
                        case SCPE_NODEV:
                                chan_info[chan] |= CHAN_TWE;
                                chan_flags[chan] &= ~STA_ACTIVE;
                        case SCPE_BUSY: /* Device not ready yet, wait */
                                chan_flags[chan] &= ~(CTL_SNS);
                                continue;
                        case SCPE_OK:
                                        /* Device will be waiting for command */
                                break;
                        }
                        /* Get channel ready to transfer */
                        chan_flags[chan] &= ~DEV_WRITE;
                        chan_flags[chan] |= DEV_SEL;
                        continue;

                case 4: /* Transfer command */
                        if (chan_flags[chan] & CTL_CNTL)
                            goto xfer;
                        if (chan_flags[chan] & (CTL_READ|CTL_WRITE|CTL_SNS)) {
                            if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DATA, &chan_dev,
                                              "chan %d control busy %04x\n",
                                              chan, chan_flags[chan]);
                            if (chan_flags[chan] & DEV_SEL) {
                               chan_flags[chan] |= DEV_DISCO|DEV_WEOR|STA_WAIT;
                            }
                            chan_flags[chan] &= ~(CTL_SNS|CTL_READ|CTL_WRITE);
                            continue;
                        }
                        chan_flags[chan] |= CTL_CNTL;
                        /* Get channel ready to transfer */
                        chan_flags[chan] &= ~(CTL_END|DEV_REOR|DEV_FULL);

                        switch(chan_issue_cmd(chan,0,chan_stat(chan,CTL_SEL))){
                        case SCPE_IOERR:
                        case SCPE_NODEV:
                                chan_info[chan] |= CHAN_TWE;
                                chan_flags[chan] &=
                                                ~(CTL_SNS|CTL_CNTL|STA_ACTIVE);
                                continue;
                            case SCPE_BUSY:
                                /* Device not ready yet, wait */
                                continue;
                            case SCPE_OK:
                                 /* Device will be waiting for command */
                                break;
                        }

                        chan_flags[chan] |= DEV_WRITE;
                      xfer:
                        /* Check if comand tranfer done */
                        if (chan_flags[chan] & DEV_REOR) {
                            chan_flags[chan] &=
                                      ~(DEV_WRITE|DEV_REOR|DEV_FULL|CTL_CNTL);
                            chan_info[chan] |= CHAN_SEOR;
                            break;
                        }

                        /* Wait for device to grab the command */
                        if (chan_flags[chan] & DEV_FULL)
                            continue;

                        /* Check if device ready for next command word */
                        if ((chan_flags[chan] & (DEV_WRITE | DEV_FULL)) ==
                            DEV_WRITE) {
                            if (caddr[chan] <= limit[chan]) {
                                assembly[chan] = M[caddr[chan]];
                                chan_flags[chan] |= DEV_FULL;
                                bcnt[chan] = 10;
                                if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_CMD, &chan_dev,
                                          "chan %d cmd > %012llx\n",
                                          chan, assembly[chan]);
                                if (caddr[chan] < limit[chan])
                                    caddr[chan]++;
                                continue;
                            }
                        }
                        break;

                case 2: /* Read Backwards */
                        /* Unknown commands at moment */
                case 7:
                case 8:
                case 9:
                        chan_info[chan] |= CHAN_TWE;
                        if (chan_flags[chan] & DEV_SEL)
                             chan_flags[chan] |= DEV_DISCO;
                        chan_flags[chan] &=
                             ~(STA_ACTIVE|CTL_WRITE|CTL_READ|CTL_CNTL|CTL_SNS);
                        break;

                    }

                    /* If last all done */
                    if (cmd[chan] & CHN_LAST || chan_flags[chan] & (SNS_UEND)
                        || chan_info[chan] & CHAN_TWE) {
                        if (chan_flags[chan] & DEV_SEL)
                            chan_flags[chan] |= DEV_DISCO;
                        chan_flags[chan] &= ~(STA_ACTIVE);
                        chan_flags[chan] |= STA_TWAIT;
                    } else
                        chan_fetch(chan);
                }
                continue;
        }
    }
}

void
chan_fetch(int chan)
{
    uint32              loc = location[chan];
    t_uint64            temp;

    sim_interval--;
    chan_info[chan] &= ~CHAN_START;
    if (loc < MEMSIZE)
        temp = M[loc];
    else {
        cmd[chan] |= CHN_LAST;
        return;
    }
    location[chan] = (loc + 1);
    if ((temp & SMASK) == MSIGN)
        cmd[chan] |= CHN_LAST;
    get_rdw(temp, &caddr[chan], &limit[chan]);
    op[chan] = (temp >> 36) & 0xf;
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_CHAN, &chan_dev,
          "chan %d fetch adr=%05d op=%d cmd=%03o caddr=%05d limit=%05d\n",
          chan, loc, op[chan], cmd[chan], caddr[chan], limit[chan]);
}

void chan_set_attn_a(int chan) {
    pri_latchs[0] |= 0x002;
}

void chan_set_attn_b(int chan) {
    pri_latchs[0] |= 0x004;
}

void chan_set_attn_inq(int chan) {
    if (chan == CHAN_UREC)
        pri_latchs[0] |= 0x080;
    else
        pri_latchs[0] |= 0x100;
}

void chan_clear_attn_inq(int chan) {
    if (chan == CHAN_UREC)
        pri_latchs[0] &= ~0x080;
    else
        pri_latchs[0] &= ~0x100;
}


/* Issue a command to a channel */
int
chan_cmd(uint16 dev, uint16 dcmd, uint16 addr)
{
    uint32              chan;
    int                 prio;
    t_stat              r;

    /* Find device on given channel and give it the command */
    chan = (dev >> 8) & 0xf;
    /* If no channel device, quick exit */
    if (chan_unit[chan].flags & UNIT_DIS)
        return SCPE_IOERR;
    /* Unit is busy doing something, wait */
    if (chan_flags[chan] & (DEV_SEL|DEV_DISCO|STA_TWAIT|STA_WAIT|STA_ACTIVE))
        return SCPE_BUSY;
    /* Ok, try and find the unit */
    prio = (dev & 0x1000)? 1: 0;
    dev &= 0xff;
    location[chan] = addr;
    cmd[chan] = dcmd & 0xff;
    dcmd >>= 8;
    chan_info[chan] = (dev & 0xf) | (chan << 4);
    chan_info[chan] |= CHAN_START;
    /* Special check for console */
    if (chan == 0 && dev == 0)
        chan_info[chan] |= CHAN_OUTDEV;

    /* Check for octal translation */
    if (chan == 1 && dev & 020)
        chan_info[chan] |= CHAN_OCTAL;

    /* Enable priority */
    if (prio)
        chan_info[chan] |= CHAN_PRIO;
    assembly[chan] = 0;
    bcnt[chan] = 10;

    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7907) {
        chan_flags[chan] |= STA_ACTIVE;
        if (dev & 1)
           chan_flags[chan] |= CTL_SEL;
        else
           chan_flags[chan] &= ~CTL_SEL;
        chan_fetch(chan);
        return SCPE_OK;
    }
    r = chan_issue_cmd(chan, dcmd, dev);
    if (r != SCPE_OK) {
        /* No device, kill active */
        chan_flags[chan] &= ~(STA_ACTIVE);
    } else {
        extern uint32   IC;
        /* If transfering data, activate channel */
        if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= STA_ACTIVE;

        if (chan_dev.dctrl & (0x0100 << chan))
             sim_debug(DEBUG_CMD, &chan_dev,
                "chan %d cmd=%o IC=%05d addr=%05d\n\r", chan, dcmd, IC, addr);
    }
    return r;
}

/*
 * Write a word to the assembly register.
 */
int
chan_write(int chan, t_uint64 * data, int flags)
{
    return TIME_ERROR;
}

/*
 * Read next word from assembly register.
 */
int
chan_read(int chan, t_uint64 * data, int flags)
{
    return TIME_ERROR;
}

/*
 * Write a char to the assembly register.
 */
int
chan_write_char(int chan, uint8 * data, int flags)
{
    uint8       ch = *data;
    /* Check if last data still not taken */
    if (chan_flags[chan] & DEV_FULL) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] &= ~DEV_WEOR;
            chan_flags[chan] |= DEV_REOR;
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;       /* We had error */
        }
        if (chan == 0) {
            chan_flags[chan] |= DEV_DISCO;
        }
        return TIME_ERROR;
    }

#if 0  /* Check segment working before removing */
    /* Check for segment mark */
    if (flags & DEV_REOR && (chan_info[chan] & CHAN_FIRST) == 0) {
        if (ch == SM_CHAR)
            chan_info[chan] |= CHAN_SEOS;
        assembly[chan] |= ASIGN|(((t_uint64)bcd_mem[ch]) << 32);
        if (cmd[chan] & CHN_ALPHA)
            chan_flags[chan] |= DEV_FULL|DEV_REOR;
        else
            chan_flags[chan] |= DEV_REOR;
        chan_flags[chan] &= ~DEV_WRITE;
        chan_proc();
        return END_RECORD;
    }

    /* Clear first char in record flag */
    chan_info[chan] |= CHAN_FIRST;
#endif

    if (ch == DELTA_CHAR && (cmd[chan] & CHN_ALPHA) == 0) {
        if (bcnt[chan] == 10)
            cmd[chan] ^= CHN_NUM_MODE;
        else
            chan_info[chan] |= CHAN_TWE;
    } else {
        if (chan_flags[chan] & CTL_SNS) {
            /* Set sign based on attention signal */
            if (bcnt[chan] == 10) {
                if (chan_irq[chan] & (SNS_ATTN1 >> (chan_info[chan] & 1)))
                    assembly[chan] = PSIGN;
                else
                    assembly[chan] = MSIGN;
                chan_irq[chan] &= ~(SNS_ATTN1 >> (chan_info[chan] & 1));
            }
            /* Store character */
            ch &= 0x17;
            if (ch & 0x04)
                ch ^= 0x24;     /* Bit move */
            ch |= 0x44;
            bcnt[chan]-=2;
            assembly[chan] |= ((t_uint64)ch) << (4 * bcnt[chan]);
        } else if (cmd[chan] & CHN_NUM_MODE) {
            ch &= 0xf;
            if (ch == 0 || ch > 10)
              chan_info[chan] |= CHAN_TWE;
            else if (ch == 10)
                ch = 0;
            bcnt[chan]--;
            assembly[chan] |= ((t_uint64)ch) << (4 * bcnt[chan])|PSIGN;
            /* Check for sign digit */
            switch(*data & 060) {
            case 0:     /* Normal digit */
            case 020:   /* error */
                      if (bcnt[chan] == 0)
                          chan_info[chan] |= CHAN_TWE;
                      break;
            case 040:
                      if (bcnt[chan] > 5)
                          chan_info[chan] |= CHAN_TWE;
                      assembly[chan] &= DMASK;
                      /* Put number in right location */
                      while(bcnt[chan] != 0) {
                         bcnt[chan]--;
                         assembly[chan] >>= 4;
                      }
                      assembly[chan] |= MSIGN;
                      break;
            case 060:
                      if (bcnt[chan] > 5)
                          chan_info[chan] |= CHAN_TWE;
                      assembly[chan] &= DMASK;
                      /* Put number in right location */
                      while(bcnt[chan] != 0) {
                         bcnt[chan]--;
                         assembly[chan] >>= 4;
                      }
                      assembly[chan] |= PSIGN;
                      break;
            }
        } else {
            if (chan_info[chan] & CHAN_OCTAL)
                ch = ((ch & 070) << 1) | (ch & 07);
            else
                ch = bcd_mem[ch];
            if (ch == 0xFF) {
                chan_info[chan] |= CHAN_TWE;
                ch = 0;
            }
            bcnt[chan] -= 2;
            assembly[chan] |= ((t_uint64)ch) << (8 * (bcnt[chan] / 2));
            assembly[chan] |= (chan_info[chan] & CHAN_OCTAL)?PSIGN:ASIGN;
        }
    }

    if (flags & DEV_REOR) {
        chan_flags[chan] |= DEV_FULL|DEV_REOR;
        chan_flags[chan] &= ~DEV_WRITE;
        if (bcnt[chan] != 0 && ((cmd[chan] & (CHN_NUM_MODE)) == 0 ||
                (cmd[chan] & (CHN_ALPHA)) != 0))
           chan_info[chan] |= CHAN_SCLR;
        chan_info[chan] |= CHAN_SEOR;
        chan_proc();
        return END_RECORD;
    } else if (bcnt[chan] == 0) {
        chan_flags[chan] |= DEV_FULL;
        chan_flags[chan] &= ~DEV_WRITE;
        chan_proc();
    }

    /* If Writing end of record, abort */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_FULL | DEV_WEOR);
        return END_RECORD;
    }

    return DATA_OK;
}

/*
 * Read next char from assembly register.
 */
int
chan_read_char(int chan, uint8 * data, int flags)
{
    uint8       ch;
    /* Return END_RECORD if requested */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_WEOR /*| STA_WAIT*/);
        return END_RECORD;
    }

    chan_proc();

    /* Check if he write out last data */
    if ((chan_flags[chan] & DEV_FULL) == 0) {
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] &= ~(DEV_WEOR | STA_WAIT | DEV_WRITE);
            chan_flags[chan] |= DEV_REOR|DEV_DISCO;
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
        }
        if (chan == 0) {
            chan_flags[chan] |= DEV_DISCO;
        }
        return TIME_ERROR;
    }

    /* Send control words differently */
    if (chan_flags[chan] & CTL_CNTL) {
        if ((assembly[chan] & SMASK) == ASIGN) {
            bcnt[chan] -= 2;
            ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xff;
            *data = mem_bcd[ch];
        } else {
            bcnt[chan]--;
            ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xf;
            if (ch == 0)
               ch = 10;
            *data = ch;
        }
        goto done;
    }

    /* Handle console type out */
    if (chan_info[chan] & CHAN_OUTDEV) {
        if (bcnt[chan] == 10 && (cmd[chan] & CHN_NUM_MODE) == 0) {
            switch(assembly[chan] & SMASK) {
            case ASIGN: break;
            case PSIGN: *data = 060;
                        cmd[chan] |= CHN_NUM_MODE;
                        return SCPE_OK;
            case MSIGN: *data = 040;
                        cmd[chan] |= CHN_NUM_MODE;
                        return SCPE_OK;
            }
        }
        if (cmd[chan] & CHN_NUM_MODE) {
            bcnt[chan]--;
            ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xf;
            if (ch == 0)
               ch = 10;
            if (bcnt[chan] == 0)
                cmd[chan] &= ~CHN_NUM_MODE;
            *data = ch;
        } else {
            bcnt[chan] -= 2;
            ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xff;
            *data = mem_bcd[ch];
        }
        goto done;
    }

    /* Check for mode change */
    if (bcnt[chan] == 10  && (cmd[chan] & (CHN_ALPHA)) == 0) {
         if (((assembly[chan] & SMASK) == ASIGN &&
                          (cmd[chan] & CHN_NUM_MODE) == CHN_NUM_MODE)
             ||((assembly[chan] & SMASK) != ASIGN &&
                          (cmd[chan] & CHN_NUM_MODE) == CHN_ALPHA_MODE)) {
             *data = DELTA_CHAR;
             cmd[chan] ^= CHN_NUM_MODE;
             return DATA_OK;
        }
        /* Handle zero compression here */
        if ((cmd[chan] & (CHN_NUM_MODE|CHN_COMPRESS)) ==
                        (CHN_NUM_MODE|CHN_COMPRESS)) {
            while((assembly[chan] >> (4 * bcnt[chan]) & 0xf) == 0 &&
                bcnt[chan] < 5)
                bcnt[chan]--;
        }
    }

    /* If in number mode, dump as number */
    if (cmd[chan] & CHN_NUM_MODE) {
        bcnt[chan]--;
        ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xf;
        if (ch == 0)
           ch = 10;
        if (bcnt[chan] == 0)
           ch |= ((assembly[chan] & SMASK) == MSIGN)? 040: 060;
        *data = ch;
    } else {
        bcnt[chan] -= 2;
        ch = (assembly[chan] >> (4 * bcnt[chan])) & 0xff;
        *data = mem_bcd[ch];
    }

done:
    if (bcnt[chan] == 0) {
        chan_flags[chan] &= ~DEV_FULL;
        bcnt[chan] = 10;
    }
    /* If end of record, don't transfer any data */
    if (flags & DEV_REOR) {
        chan_flags[chan] &= ~(DEV_WRITE|DEV_FULL);
        chan_flags[chan] |= DEV_REOR;
        chan_proc();
    } else
        chan_flags[chan] |= DEV_WRITE;
    return DATA_OK;
}

void
chan_set_load_mode(int chan)
{
    cmd[chan] &= ~CHN_ALPHA;
    cmd[chan] |= CHN_NUM_MODE;
}

void
chan9_set_error(int chan, uint32 mask)
{
    if (chan_flags[chan] & mask)
        return;
    chan_flags[chan] |= mask;
}

t_stat
chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", chan_description(dptr));
   fprintf (st, "The 7070 supports up to 8 channels. Channel models include\n\n");
   fprintf (st, "        7604            standard multiplexor channel\n");
   fprintf (st, "        7907            advanced capabilities channel\n\n");
   fprintf (st, "Channels are fixed on the 7070.\n\n");
   fprintf (st, "Channel * is for unit record devices.\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
chan_description(DEVICE *dptr)
{
    return "IBM 7070 channel controller";
}


