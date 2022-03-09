/* sel32_chan.c: SEL 32 Channel functions.

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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

*/

/* Handle Class E and F channel I/O operations */
#include "sel32_defs.h"

/* Class E I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08 09|10 11 12|13 14 15|16 17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     | Channel   |sub-addr|  Aug   |                 Command Code                  | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */

/* Bits 00-05 - Op code = 0xFC */
/* Bits 00-09 - I/O channel Address (0-15) */
/* Bits 10-12 - I/O sub address (0-7) */
/* Bits 13-15 - Aug code = 6 - CD */
/* Bits 16-31 - Command Code (Device Dependent) */

/* Bits 13-15 - Aug code = 5 - TD */
/* Bits 16-18 - TD Level 2000, 4000, 8000 */
/*      01 - TD 2000 Level Status Testing */
/*      02 - TD 4000 Level Status Testing */
/*      04 - TD 8000 Level Status Testing */
/*              CC1           CC2           CC3            CC4  */
/* TD8000   Undefined       I/O Activ      I/O Error     Dev Stat Present */
/* TD4000   Invd Mem Acc    Mem Parity     Prog Viol     Data Ovr/Undr  */
/* TD2000        -          Status Err       -           Controlr Absent  */

/* Class F I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     |  Reg   |  I/O type |  Aug   |0 |   Channel Address  |  Device Sub-address   | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */

/* Bits 00-06 - Op code 0xFC */
/* Bits 09-12 - I/O type */
/*      00 - Unassigned */
/*      01 - Unassigned */
/*      02 - Start I/O (SIO) */
/*      03 - Test I/O (TIO) */
/*      04 - Stop I/O (STPIO */
/*      05 - Reset channel (RSCHNL) */
/*      06 - Halt I/O (HIO) */
/*      07 - Grab controller (GRIO) Not supported */
/*      08 - Reset controller (RSCTL) */
/*      09 - Enable write channel WCS (ECWCS) Not supported */
/*      0A - Unassigned */
/*      0B - Write channel WCS (WCWCS) Not supported */
/*      0C - Enable channel interrupt (ECI) */
/*      0D - Disable channel interrupt (DCI) */
/*      0E - Activate channel interrupt (ACI) */
/*      0F - Deactivate channel interrupt (DACI) */
/* Bits 13-15 - Aug Code */
/* Bit 16 - unused - must be zero */
/* Bits 16-23 - Channel address (0-127) */
/* Bits 24-31 - Device Sub address (0-255) */

uint32  channels        = MAX_CHAN;             /* maximum number of channels */
int     subchannels     = SUB_CHANS;            /* maximum number of subchannel devices */
int     irq_pend        = 0;                    /* pending interrupt flag */

extern  uint32  CPUSTATUS;                      /* CPU status word */
extern  uint32  INTS[];                         /* Interrupt status flags */
extern  uint32  TPSD[];                         /* Temp save of PSD from memory 0&4 */
extern  uint8   waitqcnt;                       /* # of instructions to xeq b4 int */
extern  uint32  inbusy;
extern  uint32  outbusy;

DIB     *dib_unit[MAX_DEV];                     /* Pointer to Device info block */
DIB     *dib_chan[MAX_CHAN];                    /* pointer to channel mux dib */
uint16  loading;                                /* set when booting */

/* forward definitions */
CHANP   *find_chanp_ptr(uint16 chsa);           /* find chanp pointer */
UNIT    *find_unit_ptr(uint16 chsa);            /* find unit pointer */
int     chan_read_byte(uint16 chsa, uint8 *data);
int     chan_write_byte(uint16 chsa, uint8 *data);
void    set_devattn(uint16 chsa, uint16 flags);
void    set_devwake(uint16 chsa, uint16 flags); /* wakeup O/S for async line */
void    chan_end(uint16 chsa, uint16 flags);
int     test_write_byte_end(uint16 chsa);
t_stat  checkxio(uint16 chsa, uint32 *status);  /* check XIO */
t_stat  startxio(uint16 chsa, uint32 *status);  /* start XIO */
t_stat  testxio(uint16 chsa, uint32 *status);   /* test XIO */
t_stat  stoptxio(uint16 chsa, uint32 *status);  /* stop XIO */
t_stat  rschnlxio(uint16 chsa, uint32 *status); /* reset channel XIO */
t_stat  haltxio(uint16 chsa, uint32 *status);   /* halt XIO */
t_stat  grabxio(uint16 chsa, uint32 *status);   /* grab XIO n/u */
t_stat  rsctlxio(uint16 chsa, uint32 *status);  /* reset controller XIO */
uint32  find_int_icb(uint16 chsa);
uint32  find_int_lev(uint16 chsa);
uint32  scan_chan(uint32 *ilev);
uint32  cont_chan(uint16 chsa);
t_stat  set_inch(UNIT *uptr, uint32 inch_addr, uint32 num_inch);    /* set inch addr */
t_stat  chan_boot(uint16 chsa, DEVICE *dptr);
t_stat  chan_set_devs();
t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
DEVICE  *get_dev(UNIT *uptr);
void    store_csw(CHANP *chp);
void    push_csw(CHANP *chp);
int16   post_csw(CHANP *chp, uint32 rstat);

/* FIFO support */
/* These are FIFO queues which return an error when full.
 *
 * FIFO is empty when in == out.
 * If in != out, then
 * - items are placed into in before incrementing in
 * - items are removed from out before incrementing out
 * FIFO is full when in == (out-1 + FIFO_SIZE) % FIFO_SIZE;
 *
 * The queue will hold FIFO_SIZE items before the calls
 * to FIFO_Put fails.
 */

/* initialize FIFO to empty in boot channel code */

/* add an entry to the start of the FIFO */
int32 FIFO_Push(uint16 chsa, uint32 entry)
{
    int32 num;                                  /* number of entries */
    DIB *dibp = dib_chan[get_chan(chsa)];       /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Push ERR NULL dib ptr for chsa %04x\n", chsa);
        return -1;                              /* FIFO address error */
    }

    if (dibp->chan_fifo_in == ((dibp->chan_fifo_out-1+FIFO_SIZE) % FIFO_SIZE)) {
        num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Push ERR FIFO full for chsa %04x count %02x\n", chsa, num);
        return -1;                              /* FIFO Full */
    }
    dibp->chan_fifo_out += (FIFO_SIZE - 1);     /* get index to previous first entry */
    dibp->chan_fifo_out %= FIFO_SIZE;           /* modulo FIFO size */
    dibp->chan_fifo[dibp->chan_fifo_out] = entry;   /* add new entry to be new first */
    num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
    sim_debug(DEBUG_EXP, &cpu_dev,
        "FIFO_Push to FIFO for chsa %04x count %02x\n", chsa, num);
    return SCPE_OK;                             /* all OK */
}

/* add an entry to the FIFO */
int32 FIFO_Put(uint16 chsa, uint32 entry)
{
    int32 num;                                  /* number of entries */
    DIB *dibp = dib_chan[get_chan(chsa)];       /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Put ERR NULL dib ptr for chsa %04x\n", chsa);
        return -1;                              /* FIFO address error */
    }

    if (dibp->chan_fifo_in == ((dibp->chan_fifo_out-1+FIFO_SIZE) % FIFO_SIZE)) {
        num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Put ERR FIFO full for chsa %04x count %02x\n", chsa, num);
        return -1;                              /* FIFO Full */
    }
    dibp->chan_fifo[dibp->chan_fifo_in] = entry;    /* add new entry */
    dibp->chan_fifo_in += 1;                    /* next entry */
    dibp->chan_fifo_in %= FIFO_SIZE;            /* modulo FIFO size */
    num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
    return SCPE_OK;                             /* all OK */
}

/* get the next entry from the FIFO */
int32 FIFO_Get(uint16 chsa, uint32 *old)
{
    DIB *dibp = dib_chan[get_chan(chsa)];       /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Get ERR NULL dib ptr for chsa %04x\n", chsa);
        return -1;                              /* FIFO address error */
    }

    /* see if the FIFO is empty */
    if (dibp->chan_fifo_in == dibp->chan_fifo_out) {
        return -1;                              /* FIFO is empty, tell caller */
    }
    *old = dibp->chan_fifo[dibp->chan_fifo_out];    /* get the next entry */
    dibp->chan_fifo_out += 1;                   /* next entry */
    dibp->chan_fifo_out %= FIFO_SIZE;           /* modulo FIFO size */
    return SCPE_OK;                             /* all OK */
}

/* get number of entries in FIFO for channel */
int32 FIFO_Num(uint16 chsa)
{
    int32 num;                                  /* number of entries */
    DIB *dibp = dib_chan[get_chan(chsa)];       /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Num ERR NULL dib ptr for chsa %04x\n", chsa);
        return 0;                               /* FIFO address error */
    }
    /* calc entries */
    num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
    return (num>>1);        /*GT*/              /* two words/entry */
}

/* add an entry to the IOCLQ */
int32 IOCLQ_Put(IOCLQ *qptr, uint32 entry)
{
    int32 num;                                  /* number of entries */
    if (qptr == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev, "IOCLQ_Put ERROR NULL qptr\n");
        return -1;                              /* IOCLQ address error */
    }

    if (qptr->ioclq_in == ((qptr->ioclq_out-1+IOCLQ_SIZE) % IOCLQ_SIZE)) {
        num = (qptr->ioclq_in - qptr->ioclq_out + IOCLQ_SIZE) % IOCLQ_SIZE;
        sim_debug(DEBUG_EXP, &cpu_dev, "IOCLQ_Put ERROR IOCLQ full, entries %02x\n", num);
        return -1;                              /* IOCLQ Full */
    }
    qptr->ioclq_fifo[qptr->ioclq_in] = entry;   /* add new entry */
    qptr->ioclq_in += 1;                        /* next entry */
    qptr->ioclq_in %= IOCLQ_SIZE;               /* modulo IOCLQ size */
    num = (qptr->ioclq_in - qptr->ioclq_out + IOCLQ_SIZE) % IOCLQ_SIZE;
    return SCPE_OK;                             /* all OK */
}

/* get the next entry from the IOCLQ */
int32 IOCLQ_Get(IOCLQ *qptr, uint32 *old)
{
    if (qptr == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev, "IOCLQ_Get ERROR NULL qptr\n");
        return -1;                              /* IOCLQ address error */
    }

    /* see if the IOCLQ is empty */
    if (qptr->ioclq_in == qptr->ioclq_out) {
        return -1;                              /* IOCLQ is empty, tell caller */
    }
    *old = qptr->ioclq_fifo[qptr->ioclq_out];   /* get the next entry */
    qptr->ioclq_out += 1;                       /* next entry */
    qptr->ioclq_out %= IOCLQ_SIZE;              /* modulo IOCLQ size */
    return SCPE_OK;                             /* all OK */
}

/* get number of entries in IOCLQ for channel */
int32 IOCLQ_Num(IOCLQ *qptr)
{
    int32 num = 0;                              /* number of entries */
    if (qptr == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev, "IOCLQ_Num ERROR NULL qptr\n");
        return num;                             /* IOCLQ address error */
    }
    /* calc entries */
    num = (qptr->ioclq_in - qptr->ioclq_out + IOCLQ_SIZE) % IOCLQ_SIZE;
    return num;                                 /* one words/entry */
}

/*
 *  Number of inch buffers defined for each channel
 *  IOP         128 Dbl words
 *  MFP         128 Dbl words
 *  8-line      uses IOP/MFP (128)
 *  BTP tape    2 DBL wds
 *  UDP disk    33 Dbl wds
 *  SCFI disk   33 Dbl wds
 *  HSDP disk   33 Dbl wds
 *  SCSI disk   uses MFP (128)
 *  LP          uses IOP/MFP (128)
 *  Console     uses IOP/MFP (128)
 *  Ethernet    1 Dbl wd
 *  */
/* Set INCH buffer address for channel */
/* return SCPE_OK or SCPE_MEM if invalid address or SCPE_ARG if already defined */
t_stat set_inch(UNIT *uptr, uint32 inch_addr, uint32 num_inch) {
    uint16  chsa = GET_UADDR(uptr->u3);         /* get channel & sub address */
    uint32  chan = chsa & 0x7f00;               /* get just channel address */
    uint32  last = inch_addr + (num_inch-1)*8;  /* last inch address to use */
    CHANP   *chp;
    int     i, j;
    DIB     *dibp = dib_chan[chan>>8];          /* get channel dib ptr */
    CHANP   *pchp = 0;                          /* for channel prog ptr */

    /* must be valid DIB pointer */
    if (dibp == NULL)
        return SCPE_MEM;                        /* return memory error */
    pchp = dibp->chan_prg;                      /* get parent channel prog ptr */

    /* must be valid channel pointer */
    if (pchp == NULL)
        return SCPE_MEM;                        /* return memory error */

    /* see if start valid memory address */
    if (!MEM_ADDR_OK(inch_addr))                /* see if mem addr >= MEMSIZE */
        return SCPE_MEM;                        /* return memory error */

    /* see if end valid memory address */
    if (!MEM_ADDR_OK(last))                     /* see if mem addr >= MEMSIZE */
        return SCPE_MEM;                        /* return memory error */

    /* set INCH address for all units on master channel */
    chp = pchp;
    for (i=0; i<dibp->numunits; i++) {
        chp->chan_inch_addr = inch_addr;        /* set the current inch addr */
        chp->base_inch_addr = inch_addr;        /* set the base inch addr */
        chp->max_inch_addr = last;              /* set the last inch addr */
        chp++;                                  /* next unit channel */
    }

    sim_debug(DEBUG_XIO, &cpu_dev,
        "set_inch chan %04x inch addr %06x last %06x chp %p\n",
        chan, inch_addr, last, chp);

    /* now go through all the sub addresses for the channel and set inch addr */
    for (i=0; i<SUB_CHANS; i++) {
        chsa = chan | i;                        /* merge sa to real channel */
        if (dib_unit[chsa] == dibp)             /* if same dibp already done */
            continue;
        if (dib_unit[chsa] == 0)                /* make sure good address */
            continue;                           /* must have a DIB, so not used */
        dibp = dib_unit[chsa];                  /* finally get the new dib adddress */
        chp = dibp->chan_prg;                   /* get first unit channel prog ptr */
        /* set INCH address for all units on channel */
        for (j=0; j<dibp->numunits; j++) {
            chp->chan_inch_addr = inch_addr;    /* set the inch addr */
            chp->base_inch_addr = inch_addr;    /* set the base inch addr */
            chp->max_inch_addr = last;          /* set the last inch addr */
            chp++;                              /* next unit channel */
        }
    }
    return SCPE_OK;                             /* All OK */
}

/* Find interrupt level for the given physical device (ch/sa) */
/* return 0 if not found, otherwise level number */
uint32 find_int_lev(uint16 chsa)
{
    uint32  inta;
    /* get the device entry for channel in SPAD */
    uint32  spadent = SPAD[get_chan(chsa)];     /* get spad device entry for logical channel */

    if ((spadent == 0) || ((spadent&MASK24) == MASK24)) {   /* see if valid entry */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_lev ERR chsa %04x spadent %08x\n", chsa, spadent);
        return 0;                               /* not found */
    }
    inta = ((~spadent)>>16)&0x7f;               /* get interrupt level */
    return(inta);                               /* return the level*/
}

/* Find interrupt context block address for given device (ch/sa) */
/* return 0 if not found, otherwise ICB memory address */
uint32 find_int_icb(uint16 chsa)
{
    uint32  inta, icba;

    inta = find_int_lev(chsa);                  /* find the int level */
    if (inta == 0) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_icb ERR chsa %04x inta %02x\n", chsa, inta);
        return 0;                               /* not found */
    }
    /* add interrupt vector table base address plus int # byte address offset */
    icba = SPAD[0xf1] + (inta<<2);              /* interrupt vector address in memory */
    if (!MEM_ADDR_OK(icba)) {                   /* needs to be valid address in memory */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_icb ERR chsa %04x icba %02x\n", chsa, icba);
        return 0;                               /* not found */
    }
    icba = RMW(icba);                           /* get address of ICB from memory */
    if (!MEM_ADDR_OK(icba)) {                   /* needs to be valid address in memory */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_icb ERR chsa %04x icba %02x\n", chsa, icba);
        return 0;                               /* not found */
    }
    return(icba);                               /* return the address */
}

/* Find unit pointer for given device (ch/sa) */
UNIT *find_unit_ptr(uint16 chsa)
{
    struct dib  *dibp;                          /* DIB pointer */
    UNIT        *uptr;                          /* UNIT pointer */
    int         i;

    dibp = dib_unit[chsa];                      /* get DIB pointer from device address */
    if (dibp == 0) {                            /* if zero, not defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_unit_ptr ERR chsa %04x dibp %p\n", chsa, dibp);
        return NULL;                            /* tell caller */
    }

    uptr = dibp->units;                         /* get the pointer to the units on this channel */
    if (uptr == 0) {                            /* if zero, no devices defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_unit_ptr ERR chsa %04x uptr %p\n", chsa, uptr);
        return NULL;                            /* tell caller */
    }

    for (i = 0; i < dibp->numunits; i++) {      /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {      /* does ch/sa match? */
            return uptr;                        /* return the pointer */
        }
        uptr++;                                 /* next unit */
    }
    return NULL;                                /* device not found on system */
}

/* Find channel program pointer for given device (ch/sa) */
CHANP *find_chanp_ptr(uint16 chsa)
{
    struct dib  *dibp;                          /* DIB pointer */
    UNIT        *uptr;                          /* UNIT pointer */
    CHANP       *chp;                           /* CHANP pointer */
    int         i;

    dibp = dib_unit[chsa];                      /* get DIB pointer from unit address */
    if (dibp == 0) {                            /* if zero, not defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev, "find_chanp_ptr ERR chsa %04x dibp %p\n", chsa, dibp);
        return NULL;                            /* tell caller */
    }
    if ((chp = (CHANP *)dibp->chan_prg) == NULL) {  /* must have channel information for each device */
        sim_debug(DEBUG_EXP, &cpu_dev, "find_chanp_ptr ERR chsa %04x chp %p\n", chsa, chp);
        return NULL;                            /* tell caller */
    }

    uptr = dibp->units;                         /* get the pointer to the units on this channel */
    if (uptr == 0) {                            /* if zero, no devices defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev, "find_chanp_ptr ERR chsa %04x uptr %p\n", chsa, uptr);
        return NULL;                            /* tell caller */
    }

    for (i = 0; i < dibp->numunits; i++) {      /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {      /* does ch/sa match? */
            return chp;                         /* return the pointer */
        }
        uptr++;                                 /* next UNIT */
        chp++;                                  /* next CHANP */
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "find_chanp_ptr ERR chsa %04x no match uptr %p\n", chsa, uptr);
    return NULL;                                /* device not found on system */
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readfull(CHANP *chp, uint32 maddr, uint32 *word)
{
    maddr &= MASK24;                            /* mask addr to 24 bits */
    if (!MEM_ADDR_OK(maddr)) {                  /* see if mem addr >= MEMSIZE */
        chp->chan_status |= STATUS_PCHK;        /* program check error */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "readfull read %08x from addr %08x ERROR\n", *word, maddr);
        return 1;                               /* show we have error */
    }
    *word = RMW(maddr);                         /* get 1 word */
    sim_debug(DEBUG_XIO, &cpu_dev, "READFULL chsa %04x read %08x from addr %08x\n",
        chp->chan_dev, *word, maddr);
    return 0;                                   /* return OK */
}

/* Read a byte into the channel buffer.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readbuff(CHANP *chp)
{
    uint32 addr = chp->ccw_addr;                /* channel buffer address */

    if (!MEM_ADDR_OK(addr & MASK24)) {          /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;        /* bad, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "readbuff PCHK addr %08x to big mem %08x status %04x\n",
            addr, MEMSIZE, chp->chan_status);
        chp->chan_byte = BUFF_CHNEND;           /* force channel end & busy */
        return 1;                               /* done, with error */
    }
    chp->chan_buf = RMB(addr&MASK24);           /* get 1 byte */
    return 0;
}

/* Write byte to channel buffer in memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int writebuff(CHANP *chp)
{
    uint32 addr = chp->ccw_addr;

    if (!MEM_ADDR_OK(addr & MASK24)) {          /* make sure write to good addr */
        chp->chan_status |= STATUS_PCHK;        /* non-present memory, abort */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "writebuff PCHK addr %08x to big mem %08x status %04x\n",
            addr, MEMSIZE, chp->chan_status);
        chp->chan_byte = BUFF_CHNEND;           /* force channel end & busy */
        return 1;
    }
    addr &= MASK24;                             /* good address, write the byte */
    sim_debug(DEBUG_DATA, &cpu_dev, "writebuff WRITE addr %06x DATA %08x status %04x\n",
        addr, chp->chan_buf, chp->chan_status);
    WMB(addr, chp->chan_buf);                   /* write byte to memory */
    return 0;
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
int32 load_ccw(CHANP *chp, int32 tic_ok)
{
    uint32      word1 = 0;
    uint32      word2 = 0;
    int32       docmd = 0;
    DIB         *dibp = dib_unit[chp->chan_dev];    /* get the DIB pointer */
    UNIT        *uptr = chp->unitptr;           /* get the unit ptr */
    uint16      chan = get_chan(chp->chan_dev); /* our channel */
    uint16      chsa = chp->chan_dev;           /* our chan/sa */
    uint16      devstat = 0;

    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x entry chan_status[%02x]=%04x\n",
        chp->chan_caw, chan, chp->chan_status);
#ifdef TEST_FOR_IOCL_CHANGE
    /* see if iocla or iocd has changed since start */
    if (!loading && (chp->chan_info & INFO_SIOCD)) {  /* see if 1st IOCD in channel prog */
        uint32  chan_icb;                       /* Interrupt level context block address */
        uint32  iocla;                          /* I/O channel IOCL address int ICB */

        chan_icb = find_int_icb(chsa);          /* Interrupt level context block address */
        iocla = RMW(chan_icb+16);               /* iocla is in wd 4 of ICB */
        word1 = RMW(iocla & MASK24);            /* get 1st IOCL word */
        word2 = RMW((iocla + 4) & MASK24);      /* get 2nd IOCL word */
        if (chp->chan_caw != iocla) {
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw iocla (%06x) != chan_caw (%06x) chsa %04x\n",
                iocla, chp->chan_caw, chsa);
        } else
        {
            /* iocla has not changed, see if IOCD has */
            if (chp->new_iocla != iocla) {      /* check current iocla */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw iocla (%06x) != new_iocla (%06x) chsa %04x\n",
                iocla, chp->new_iocla, chsa);
            }
            if (word1 != chp->new_iocd1) {      /* check word1 from memory */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw iocd1 (%06x) != new_iocd1 (%06x) chsa %04x\n",
                word1, chp->new_iocd1, chsa);
            }
            if (word2 != chp->new_iocd2) {      /* check word2 from memory */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw iocd2 (%06x) != new_iocd2 (%06x) chsa %04x\n",
                word2, chp->new_iocd2, chsa);
            }
        }
    }
#endif

    /* determine if channel DIB has a pre iocl processor */
    if (dibp->iocl_io != NULL) {                /* NULL if no function */
        /* call the device controller to process the iocl */
        int32 tempa = dibp->iocl_io(chp, tic_ok);   /* process IOCL */
        if (tempa != SCPE_OK) {                 /* see if OK */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw iocl_io call return ERROR chan %04x cstat %01x\n", chan, tempa);
        } else {
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw iocl_io call return OK chan %04x cstat %01x\n", chan, tempa);
        }
        return tempa;                           /* just return status */
    }
    /* check for valid iocd address if 1st iocd */
    if (chp->chan_info & INFO_SIOCD) {          /* see if 1st IOCD in channel prog */
        if (chp->chan_caw & 0x3) {              /* must be word bounded */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw iocd bad address chsa %02x caw %06x\n",
                chsa, chp->chan_caw);
            /* the disk returns the bad iocl in sw1 */
            chp->ccw_addr = chp->chan_caw & MASK24; /* set the bad IOCL address */
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid iocd addr */
            return 1;                           /* error return */
        }
    }

loop:
    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x @loop chan_status[%02x]=%04x\n",
        chp->chan_caw, chan, chp->chan_status);

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR) {      /* check channel error status */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR1 chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;
    }

    /* Read in first CCW */
    if (readfull(chp, chp->chan_caw, &word1) != 0) { /* read word1 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR2 chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Read in second CCW */
    if (readfull(chp, chp->chan_caw+4, &word2) != 0) { /* read word2 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR3 chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x read ccw chan %02x IOCD wd 1 %08x wd 2 %08x\n",
        chp->chan_caw, chan, word1, word2);

    chp->chan_caw = (chp->chan_caw & 0xfffffc) + 8; /* point to next IOCD */

    /* Check if we had data chaining in previous iocd */
    /* if we did, use previous cmd value */
    if (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
       (chp->ccw_flags & FLAG_DC)) {            /* last IOCD have DC set? */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x DO DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
    } else
        chp->ccw_cmd = (word1 >> 24) & 0xff;    /* set new command from IOCD wd 1 */

    if (!MEM_ADDR_OK(word1 & MASK24)) {         /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;        /* bad, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw bad IOCD1 chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    chp->ccw_count = word2 & 0xffff;            /* get 16 bit byte count from IOCD WD 2*/

    /* here is where we would validate the device commands */

    if (chp->chan_info & INFO_SIOCD) {          /* see if 1st IOCD in channel prog */
        /* 1st command can not be a TIC */
        if (chp->ccw_cmd == CMD_TIC) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid tic */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw TIC bad cmd chan_status[%02x]=%04x\n",
                chan, chp->chan_status);
            return 1;                           /* error return */
        }
    }

    /* TIC can't follow TIC or be first in command chain */
    /* diags send bad commands for testing.  Use all of op */
    if (chp->ccw_cmd == CMD_TIC) {
        if (tic_ok) {
            if (((word1 & MASK24) == 0) || (word1 & 0x3)) {
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "load_ccw tic cmd bad address chan %02x tic caw %06x IOCD wd 1 %08x\n",
                    chan, chp->chan_caw, word1);
                chp->chan_status |= STATUS_PCHK;    /* program check for invalid tic */
                chp->chan_caw = word1 & MASK24; /* get new IOCD address */
                return 1;                       /* error return */
            }
            tic_ok = 0;                         /* another tic not allowed */
            chp->chan_caw = word1 & MASK24;     /* get new IOCD address */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw tic cmd ccw chan %02x tic caw %06x IOCD wd 1 %08x\n",
                chan, chp->chan_caw, word1);
            goto loop;                          /* restart the IOCD processing */
        }
        chp->chan_caw = word1 & MASK24;         /* get new IOCD address */
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid tic */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw TIC ERROR chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Check if we had data chaining in previous iocd */
    if ((chp->chan_info & INFO_SIOCD) ||        /* see if 1st IOCD in channel prog */
        (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
        ((chp->ccw_flags & FLAG_DC) == 0))) {    /* last IOCD have DC set? */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x DO CMD No DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
        docmd = 1;                              /* show we have a command */
    }

    /* Set up for this command */
    chp->ccw_flags = (word2 >> 16) & 0xfc00;    /* get flags from bits 0-6 of WD 2 of IOCD */
    chp->chan_status = 0;                       /* clear status for next IOCD */
    /* make a 24 bit address */
    chp->ccw_addr = word1 & MASK24;             /* set the data/seek address */

    if (chp->ccw_flags & FLAG_PCI) {            /* do we have prog controlled int? */
        chp->chan_status |= STATUS_PCI;         /* set PCI flag in status */
        irq_pend = 1;                           /* interrupt pending */
    }

    /* validate parts of IOCD2 that is reserved, bits 5-15 */    
    if (word2 & 0x07ff0000) {
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid iocd */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw bad IOCD2 chan_status[%02x]=%04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* DC can only be used with a read/write cmd */
    /* TODO move ccw code to LPR processing */
    /* TEMP FIX FOR LPR */
    if ((chp->ccw_flags & FLAG_DC) && (chsa != 0x7ef8)) {
        if ((chp->ccw_cmd != 0x02) && (chp->ccw_cmd != 0x01)) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid DC */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw DC ERROR chan_status[%02x]=%04x\n", chan, chp->chan_status);
            return 1;                           /* error return */
        }
    }

    chp->chan_byte = BUFF_BUSY;                 /* busy & no bytes transferred yet */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x read docmd %01x addr %06x count %04x chsa %04x ccw_flags %04x\n",
        chp->chan_caw, docmd, chp->ccw_addr, chp->ccw_count, chsa, chp->ccw_flags);

    if (docmd) {                                /* see if we need to process a command */
        DIB *dibp = dib_unit[chp->chan_dev];    /* get the DIB pointer */
 
        uptr = chp->unitptr;                    /* get the unit ptr */
        if (dibp == 0 || uptr == 0) {
            chp->chan_status |= STATUS_PCHK;    /* program check if it is */
            return 1;                           /* if none, error */
        }

        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x before start_cmd chsa %04x status %04x count %04x SNS %08x\n",
            chp->chan_caw, chsa, chp->chan_status, chp->ccw_count, uptr->u5);

        /* call the device startcmd function to process the current command */
        /* just replace device status bits */
        chp->chan_info &= ~INFO_CEND;           /* show chan_end not called yet */
        devstat = dibp->start_cmd(uptr, chan, chp->ccw_cmd);
        chp->chan_status = (chp->chan_status & 0xff00) | devstat;
        chp->chan_info &= ~INFO_SIOCD;          /* not first IOCD in channel prog */

        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x after start_cmd chsa %04x status %08x count %04x\n",
            chp->chan_caw, chsa, chp->chan_status, chp->ccw_count);

        /* We will get a SNS_BSY status returned if device doing a command */
        /* We get STATUS_CEND & STATUS_DEND and an error */
        /* We get SCPE_OK (0) saying cmd is ready to process */
        /* see if bad status */
        if (chp->chan_status & (STATUS_ATTN|STATUS_ERROR)) {
            chp->chan_status |= STATUS_CEND;    /* channel end status */
            chp->ccw_flags = 0;                 /* no flags */
            chp->chan_byte = BUFF_NEXT;     /* have main pick us up */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw bad status chsa %04x status %04x cmd %02x\n",
                chsa, chp->chan_status, chp->ccw_cmd);
            /* done with command */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw ERROR return chsa %04x status %08x\n",
                chp->chan_dev, chp->chan_status);
            return 1;                       /* error return */
        }
        /* NOTE this code needed for MPX 1.X to run! */
        /* see if command completed */
        /* we have good status */
        /* TODO Test if chan_end called? */
        if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
            uint16  chsa = GET_UADDR(uptr->u3); /* get channel & sub address */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw @%06x FIFO #%1x cmd complete chan %04x status %04x count %04x\n",
                chp->chan_caw, FIFO_Num(chsa), chan, chp->chan_status, chp->ccw_count);
        }
    }
    /* the device processor returned OK (0), so wait for I/O to complete */
    /* nothing happening, so return */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x return, chsa %04x status %04x count %04x irq_pend %1x\n",
        chp->chan_caw, chsa, chp->chan_status, chp->ccw_count, irq_pend);
    return 0;                                   /* good return */
}

/* read byte from memory */
/* write to device */
int chan_read_byte(uint16 chsa, uint8 *data)
{
    CHANP   *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */
    int     byte;

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR)        /* check channel error status */
        return 1;                               /* return error */

    if (chp->chan_byte == BUFF_CHNEND)          /* check for end of data */
        return 1;                               /* yes, return error */

    if (chp->ccw_count == 0) {                  /* see if more data required */
         if ((chp->ccw_flags & FLAG_DC) == 0) { /* see if Data Chain */
            chp->chan_byte = BUFF_CHNEND;       /* buffer end too */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_read_byte no DC chan end, cnt %04x addr %06x chsa %04x\n",
                chp->ccw_count, chp->ccw_addr, chsa);
            return 1;                           /* return error */
         } else {
            /* we have data chaining, process iocl */
            if (load_ccw(chp, 1)) {             /* process data chaining */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_read_byte with DC error, cnt %04x addr %06x chsa %04x\n",
                    chp->ccw_count, chp->ccw_addr, chsa);
                return 1;                       /* return error */
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "chan_read_byte with DC IOCD loaded, cnt %04x addr %06x chsa %04x\n",
                chp->ccw_count, chp->ccw_addr, chsa);
         }
    }
    /* get the next byte from memory */
    if (readbuff(chp))                          /* read next char */
        return 1;                               /* return error */

    /* get the byte of data */
    byte = chp->chan_buf;                       /* read byte from memory */
    *data = byte;                               /* return the data */
    sim_debug(DEBUG_DATA, &cpu_dev, "chan_read_byte transferred %02x\n", byte);
    chp->ccw_addr += 1;                         /* next byte address */
    chp->ccw_count--;                           /* one char less to process */
    return 0;                                   /* good return */
}

/* test end of write byte I/O (device read) */
int test_write_byte_end(uint16 chsa)
{
    CHANP   *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND)          /* check for end of data */
        return 1;                               /* return done */
    if (chp->ccw_count == 0) {
        if ((chp->ccw_flags & FLAG_DC) == 0) {  /* see if we have data chaining */
            chp->chan_byte = BUFF_CHNEND;       /* thats all the data we want */
            return 1;                           /* return done */
        }
    }
    return 0;                                   /* not done yet */
}

/* write byte to memory */
/* read from device */
int chan_write_byte(uint16 chsa, uint8 *data)
{
    int     chan = get_chan(chsa);              /* get the channel number */
    CHANP   *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR)        /* check channel error status */
        return 1;                               /* return error */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND) {        /* check for end of data */
        /* if SLI not set, we have incorrect length */
        if ((chp->ccw_flags & FLAG_SLI) == 0) {
            sim_debug(DEBUG_EXP, &cpu_dev, "chan_write_byte 4 setting SLI ret\n");
            chp->chan_status |= STATUS_LENGTH;  /* set SLI */
        }
        return 1;                               /* return error */
    }
    if (chp->ccw_count == 0) {
        if ((chp->ccw_flags & FLAG_DC) == 0) {  /* see if we have data chaining */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_write_byte no DC ccw_flags %04x\n", chp->ccw_flags);
            chp->chan_status |= STATUS_CEND;    /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;       /* thats all the data we want */
            return 1;                           /* return done error */
        } else {
            /* we have data chaining, process iocl */
            if (load_ccw(chp, 1)) {             /* process data chaining */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_write_byte with DC error, cnt %04x addr %06x chan %04x\n",
                    chp->ccw_count, chp->ccw_addr, chan);
                return 1;                       /* return error */
            }
        }
    }
    /* we have data byte to write to chp->ccw_addr */
    /* see if we want to skip writing data to memory */
    if (chp->ccw_flags & FLAG_SKIP) {
        chp->ccw_count--;                       /* decrement skip count */
        chp->chan_byte = BUFF_BUSY;             /* busy, but no data */
        if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)
            chp->ccw_addr--;                    /* backward */
        else
            chp->ccw_addr++;                    /* forward */
        return 0;
    }
    chp->chan_buf = *data;                      /* get data byte */
    if (writebuff(chp))                         /* write the byte */
        return 1;

    chp->ccw_count--;                           /* reduce count */
    chp->chan_byte = BUFF_BUSY;                 /* busy, but no data */
    if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)     /* see if reading backwards */
        chp->ccw_addr -= 1;                     /* no, use previous address */
    else
        chp->ccw_addr += 1;                     /* yes, use next address */
    return 0;
}

/* post wakeup interrupt for specified async line */
void set_devwake(uint16 chsa, uint16 flags)
{
    uint32  stwd1, stwd2;                       /* words 1&2 of stored status */
    /* put sub address in byte 0 */
    stwd1 = (chsa & 0xff) << 24;                /* subaddress and IOCD address to SW 1 */
    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = (uint32)flags << 16;
    if ((FIFO_Put(chsa, stwd1) == -1) || (FIFO_Put(chsa, stwd2) == -1)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "set_devwake FIFO Overflow ERROR on chsa %04x\n", chsa);
    }
    irq_pend = 1;                               /* wakeup controller */
}

/* post interrupt for specified channel */
void set_devattn(uint16 chsa, uint16 flags)
{
    CHANP   *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */

    if (chp == NULL) {
        /* can not do anything, so just return */
        sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn chsa %04x, flags %04x\n", chsa, flags);
        fprintf(stdout, "set_devattn chsa %04x invalid configured device\n", chsa);
//      fflush(stdout);
        return;
    }

    if (chp->chan_dev == chsa && (chp->chan_status & STATUS_CEND) != 0 && (flags & SNS_DEVEND) != 0) {
        chp->chan_status |= ((uint16)flags);
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "set_devattn(%04x, %04x) %04x\n", chsa, flags, chp->chan_dev);
    irq_pend = 1;
}

/* channel operation completed */
void chan_end(uint16 chsa, uint16 flags) {
    uint16  tstat, tcnt;
    CHANP   *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */

    sim_debug(DEBUG_CMD, &cpu_dev,
        "chan_end entry chsa %04x flags %04x status %04x cmd %02x cpustatus %08x\n",
        chsa, flags, chp->chan_status, chp->ccw_cmd, CPUSTATUS);
//  fflush(sim_deb);

    /* see if already called */
    if (chp->chan_info & INFO_CEND) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "chan_end INFO_CEND set chsa %04x ccw_flags %04x status %04x byte %02x\n",
            chsa, chp->ccw_flags, chp->chan_status, chp->chan_byte);
    }
    chp->chan_info |= INFO_CEND;                /* we have been here */

    chp->chan_byte = BUFF_BUSY;                 /* we are empty & still busy now */
    chp->chan_status |= STATUS_CEND;            /* set channel end */
    chp->chan_status |= ((uint16)flags);        /* add in the callers flags */

    /* read/write must have none-zero byte count */
    /* all others can be zero, except NOP, which must be 0 */
    /* a NOP is Control command 0x03 with no modifier bits */
    /* see if this is a read/write cmd */
    if (((chp->ccw_cmd & 0x7) == 0x02) || ((chp->ccw_cmd & 0x7) == 0x01)) {
        /* test for incorrect transfer length */
        if (chp->ccw_count != 0 && ((chp->ccw_flags & FLAG_SLI) == 0)) {
            if ((chp->chan_status & STATUS_PCHK) == 0)  /* No SLI if channel prg check */
                chp->chan_status |= STATUS_LENGTH;  /* show incorrect length status */
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "chan_end setting SLI chsa %04x count %04x ccw_flags %04x status %04x\n",
                chsa, chp->ccw_count, chp->ccw_flags, chp->chan_status);
            chp->ccw_flags = 0;                 /* no iocl flags */
        }
    }

    /* Diags do not want SLI if we have no device end status */
    if ((chp->chan_status & STATUS_LENGTH) && ((chp->chan_status & STATUS_DEND) == 0))
        chp->chan_status &= ~STATUS_LENGTH;

    /* no flags for attention status */
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        chp->ccw_flags = 0;                     /* no flags */
    }

    sim_debug(DEBUG_EXP, &cpu_dev,
        "chan_end test end chsa %04x ccw_flags %04x status %04x byte %02x\n",
        chsa, chp->ccw_flags, chp->chan_status, chp->chan_byte);

    /* test for device or controller end */
    if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
        chp->chan_byte = BUFF_BUSY;             /* we are empty & still busy now */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "chan_end FIFO #%1x IOCL done chsa %04x ccw_flags %04x status %04x\n",
            FIFO_Num(chsa), chsa, chp->ccw_flags, chp->chan_status);

        /* handle a PPCI here.  DC is done and maybe have CC */
        if ((chp->chan_status & STATUS_PCI) && (chp->ccw_flags & FLAG_CC)) {
            chp->chan_status &= ~STATUS_PCI;    /* done with PCI */
            tstat = chp->chan_status;           /* save status */
            tcnt = chp->ccw_count;              /* save count */
            chp->chan_status = STATUS_PCI;      /* set PCI status */
            chp->ccw_count = 0;                 /* zero count */
            store_csw(chp);                     /* store the status */
            chp->chan_status = tstat;           /* restore status */
            chp->ccw_count = tcnt;              /* restore count */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_end done PCI chsa %04x ccw_flags %04x stat %04x cnt %04x\n",
                chsa, chp->ccw_flags, tstat, tcnt);
        }

        /* If channel end, check if we should continue */
        if (chp->ccw_flags & FLAG_CC) {         /* command chain flag */
            /* we have channel end and CC flag, continue channel prog */
            sim_debug(DEBUG_CMD, &cpu_dev,
                "chan_end chan end & CC chsa %04x status %04x\n",
                chsa, chp->chan_status);
            if (chp->chan_status & STATUS_DEND) {   /* device end? */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_end dev end & CC chsa %04x status %04x IOCLA %08x\n",
                    chsa, chp->chan_status, chp->chan_caw);
                /* Queue us to continue from cpu level */
                chp->chan_byte = BUFF_NEXT;     /* have main pick us up */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_end set RDYQ %04x Have CC BUFF_NEXT chp %p chan_byte %04x\n",
                    chsa, chp, chp->chan_byte);
                if (cont_chan(chsa)) {          /* continue processing channel */
                    sim_debug(DEBUG_EXP, &cpu_dev, "call cont_chan returns not OK\n");
                }
            }
            /* just return */
            goto goout;
        } else {
            /* we have channel end and no CC flag, continue channel prog */
            UNIT        *uptr = chp->unitptr;   /* get the unit ptr */
            DEVICE      *dptr = get_dev(uptr);
            uint16      chsa = GET_UADDR(uptr->u3);
            int         unit = (uptr-dptr->units);  /* get the UNIT number */
            DIB*        dibp = (DIB *)dptr->ctxt;   /* get the DIB pointer */
            IOCLQ       *qp = &dibp->ioclq_ptr[unit];   /* IOCLQ pointer */
            uint32      iocla;

            /* we have channel end and no CC flag, end this iocl command */
            sim_debug(DEBUG_CMD, &cpu_dev,
                "chan_end chan end & no CC chsa %04x status %04x cmd %02x\n",
                chsa, chp->chan_status, chp->ccw_cmd);

            /* we have completed channel program */
            /* handle case where we are loading the O/S on boot */
            /* if loading, store status to be discovered by scan_chan */
            if (!loading) {
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_end call store_csw dev/chan end chsa %04x cpustat %08x iocla %08x\n",
                    chsa, CPUSTATUS, chp->chan_caw);
            } else {
                /* we are loading, so keep moving */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_end we are loading O/S with DE & CE, keep status chsa %04x status %08x\n",
                    chsa, chp->chan_status);
            }
            /* store the status in channel FIFO to continue from cpu level */
            chp->chan_byte = BUFF_DONE;         /* we are done */
            store_csw(chp);                     /* store the status */
            /* change chan_byte to BUFF_POST */
            chp->chan_byte = BUFF_POST;         /* show done with data */
            chp->ccw_cmd = 0;                   /* no command anymore */

            if (chp->chan_status & STATUS_ERROR) {  /* check channel error status */
                qp = &dibp->ioclq_ptr[unit];    /* IOCLQ pointer */
                /* we have an error, so delete all other IOCLQ entries */
                while ((dibp->ioclq_ptr != NULL) && (qp != NULL) && IOCLQ_Get(qp, &iocla) == SCPE_OK) {
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "$$ CHEND removed IOCL from IOCLQ processing chsa %04x iocla %06x\n",
                        chsa, iocla);
                }
                chp->chan_status = 0;           /* no channel status yet */
            } else
            /* no error, see if we have a queued IOCL to start */
            /* This causes an error for hsdp where we just finished the I/O */
            /* but the status has not been posted yet nor the interrupt */
            /* starting another I/O confuses the scan_chan code and ends up */
            /* doing an extra interrupt for UTX 05/21/2021 */    
            if ((dibp->ioclq_ptr != NULL) && (qp != NULL) && IOCLQ_Get(qp, &iocla) == SCPE_OK) {
                /* channel not busy and ready to go, so start a new command */
                chp->chan_status = 0;           /* no channel status yet */
                chp->chan_caw = iocla;          /* get iocla address in memory */
                /* added 09/25/20 to fix hangs in iocl processing */
                chp->ccw_flags = 0;             /* clear flags */

                /* set status words in memory to first IOCD information */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "$$ CHEND start IOCL processing from IOCLQ num %02x chsa %04x iocla %06x\n",
                    IOCLQ_Num(qp), chsa, iocla);

                /* We are queueing the SIO */
                /* Queue us to continue IOCL from cpu level & make busy */
                chp->chan_byte = BUFF_NEXT;     /* have main pick us up */
                chp->chan_info = INFO_SIOCD;    /* show first IOCD in channel prog */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "chan_end BUFF_NEXT chsa %04x from IOCLQ cnt %02x chp %p chan_byte %04x\n",
                    chsa, IOCLQ_Num(qp), chp, chp->chan_byte);
                // FIXME - need to call iocl processing here */
                if (cont_chan(chsa)) {          /* continue processing channel */
                    sim_debug(DEBUG_EXP, &cpu_dev, "call cont_chan returns not OK\n");
                }
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "CHEND SIOQ queued chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x\n",
                    chsa, iocla, RMW(iocla), RMW(iocla+4));

            }
        }
    }
goout:
    sim_debug(DEBUG_CMD, &cpu_dev,
        "chan_end done chsa %04x status %08x chan_byte %02x\n",
        chsa, chp->chan_status, chp->chan_byte);
    /* following statement required for boot to work */
    irq_pend = 1;                               /* flag to test for int condition */
}

/* post the device status from the channel FIFO into memory */
/* the INCH command provides the status DW address in memory */
/* rstat are the bits to remove from status */
int16 post_csw(CHANP *chp, uint32 rstat)
{
    uint32  chsa = chp->chan_dev;               /* get ch/sa information */
    uint32  incha = chp->chan_inch_addr;        /* get inch status buffer address */
    uint32  sw1, sw2;                           /* status words */

    irq_pend = 1;                               /* flag to test for int condition */
    /* check channel FIFO for status to post */
    if ((FIFO_Num(chsa)) &&
        ((FIFO_Get(chsa, &sw1) == 0) && (FIFO_Get(chsa, &sw2) == 0))) {
        /* get chan_icb address */
        uint32 chan_icb = RMW(SPAD[0xf1] + (chp->chan_int<<2));

        if (chan_icb == 0) {
            sim_debug(DEBUG_EXP, &cpu_dev,
            "post_csw %04x READ FIFO #%1x inch %06x invalid chan_icb %06x\n",
            chsa, FIFO_Num(chsa), incha, chan_icb);
            return 0;                           /* no status to post */
        }
        if (chp->chan_byte != BUFF_POST) {
            sim_debug(DEBUG_EXP, &cpu_dev,
            "post_csw %04x CHP %p not BUFF_POST byte %04x ERROR FIFO #%1x inch %06x icb %06x\n",
            chsa, chp, chp->chan_byte, FIFO_Num(chsa), incha, chan_icb);
        }
        /* remove user specified bits */
        sw2 &= ~rstat;                          /* remove bits */
        /* we have status to post, do it now */
        /* save the status double word to memory */
        /* if bit 0 of sw2 is set (STATUS_ECHO), post inch addr 0 with bit 0 set */
        if (sw2 & BIT0) {                       /* see if only not busy post */
            WMW(chan_icb+20, 0x80000000);       /* post sw addr 0 in ICB+5w & reset CCs */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "post_csw %04x READ0 FIFO #%1x inch 0x80000000 chan_icb %06x sw1 %08x sw2 %08x\n",
                chsa, FIFO_Num(chsa), chan_icb, sw1, sw2);
        } else {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "post_csw %04x B4READ1 icb+16 %08x icb+20 %08x inch %06x chan_icb %06x\n",
                chsa, RMW(chan_icb+16), RMW(chan_icb+20), incha, chan_icb);
            WMW(incha, sw1);                    /* save sa & IOCD address in status WD 1 loc */
            WMW(incha+4, sw2);                  /* save status and residual cnt in status WD 2 loc */
            /* now store the status dw address into word 5 of the ICB for the channel */
            WMW(chan_icb+20, incha|BIT1);       /* post sw addr in ICB+5w & set CC2 in INCH addr */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "post_csw %04x READ1 FIFO #%1x inch %06x chan_icb %06x sw1 %08x sw2 %08x\n",
                chsa, FIFO_Num(chsa), incha, chan_icb, sw1, sw2);
            if ((incha + 8) > chp->max_inch_addr)   /* see if next inch addr OK */
                chp->chan_inch_addr = chp->base_inch_addr; /* reset to first inch addr */
        }
        return 1;                               /* show we posted status */
    }
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "post_csw %04x chp %p READ FIFO #%1x inch %06x No Status chan_byte %02x\n",
        chsa, chp, FIFO_Num(chsa), incha, chp->chan_byte);
    return 0;                                   /* no status to post */
}

/* store the device status into the status FIFO for the channel */
void store_csw(CHANP *chp)
{
    uint32  stwd1, stwd2;                       /* words 1&2 of stored status */
    uint32  chsa = chp->chan_dev;               /* get ch/sa information */

    /* put sub address in byte 0 */
    stwd1 = ((chsa & 0xff) << 24) | chp->chan_caw;  /* subaddress and IOCD address to SW 1 */

    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = ((uint32)chp->chan_status << 16) | ((uint32)chp->ccw_count);

    if ((FIFO_Put(chsa, stwd1) == -1) || (FIFO_Put(chsa, stwd2) == -1)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "store_csw FIFO Overflow ERROR on chsa %04x\n", chsa);
    }
    sim_debug(DEBUG_XIO, &cpu_dev,
        "store_csw FIFO #%1x write chsa %04x sw1 %08x sw2 %08x incha %08x cmd %02x\n",
        FIFO_Num(chsa), chsa, stwd1, stwd2, chp->chan_inch_addr, chp->ccw_cmd);
    /* added 011321 */
    /* removed 112421 */
//  INTS[chp->chan_int] |= INTS_REQ;            /* request an interrupt for channel */
    irq_pend = 1;                               /* wakeup controller */
}

/* store the device status into the first entry of the status FIFO for the channel */
void push_csw(CHANP *chp)
{
    int32  stwd1, stwd2;                       /* words 1&2 of stored status */
    uint32  chsa = chp->chan_dev;              /* get ch/sa information */

    /* put sub address in byte 0 */
    stwd1 = ((chsa & 0xff) << 24) | chp->chan_caw;  /* subaddress and IOCD address to SW 1 */

    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = ((uint32)chp->chan_status << 16) | ((uint32)chp->ccw_count);

    /* Push in reverse order to allign status correctly */
    if ((FIFO_Push(chsa, stwd2) == -1) || (FIFO_Push(chsa, stwd1) == -1)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "push_csw FIFO Overflow ERROR on chsa %04x\n", chsa);
    }
    sim_debug(DEBUG_XIO, &cpu_dev,
        "push_csw FIFO #%1x write chsa %04x sw1 %08x sw2 %08x incha %08x cmd %02x\n",
        FIFO_Num(chsa), chsa, stwd1, stwd2, chp->chan_inch_addr, chp->ccw_cmd);
    /* added 011321 */
    /* removed 112421 */
//  INTS[chp->chan_int] |= INTS_REQ;            /* request an interrupt for channel */
    irq_pend = 1;                               /* wakeup controller */
}

/* check an XIO operation */
/* logical chan channel number 0-7f */
/* suba unit address within channel 0-ff */
/* Condition codes to return 0-f as specified above */
t_stat checkxio(uint16 lchsa, uint32 *status) {
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    CHANP   *chp;                               /* channel program pointer */
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    DEVICE  *dptr;                              /* DEVICE pointer */
    uint32  inta;
    uint32  spadent;
    uint16  rchan, rchsa;                       /* the real channel number */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchan = (spadent & 0x7f00) >> 8;            /* get real channel */
    rchsa = (rchan << 8) | (lchsa & 0xff);      /* get the read chan & suba */

    dibp = dib_chan[rchan];                     /* get DIB pointer for channel */
    if (dibp == 0) goto nothere;

    chp = dibp->chan_prg;                       /* find the chanp pointer */
    if (chp == 0) goto nothere;

    uptr = dibp->units;                         /* find pointer to 1st unit on channel */
    if (uptr == 0) {                            /* if no dib or unit ptr, CC3 on return */
nothere:
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio lchsa %04x rchan %04x is not found, CC3 return\n", lchsa, rchan);
        return SCPE_OK;                         /* not found, CC3 */
    }
    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */
    dptr = get_dev(uptr);                       /* pointer to DEVICE structure */

    /* is device or unit marked disabled? */
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        /* UTX wants CC1 on "mt offline" call.  If not, UTX loops forever */
        if ((dptr != NULL) &&
            (DEV_TYPE(dptr) == DEV_TAPE)) {     /* see if this is a tape */
            *status = CC1BIT;                   /* CCs = 1, not busy */
            sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio rchsa %04x device/unit not enabled, CC1 returned\n",
            rchsa);
        } else {
            *status = CC3BIT;                   /* not attached, so error CC3 */
            sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio rchsa %04x device/unit not enabled, CC3 returned\n",
            rchsa);
        }
        return SCPE_OK;                         /* Not found CC3/CC1 */
    }

    /* try this as MFP says it returns 0 on OK */
    if (dptr->flags & DEV_CHAN)
        *status = 0;                            /* CCs = 0, OK return */
    else
        /* return CC1 for non iop/mfp devices */
    *status = 0;                                /* CCs = 0, OK return */
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "checkxio lchsa %04x rchsa %04x done CC status %08x\n",
        lchsa, rchsa, *status);
    return SCPE_OK;                             /* No CC's all OK  */
}

/* SIO CC status returned to caller */
/* val condition */
/* 0   command accepted, will echo status - no CC's */
/* 1   channel busy  - CC4 */
/* 2   channel inop or undefined (operator intervention required) - CC3 */
/* 3   sub channel busy CC3 + CC4 */
/* 4   status stored - CC2 */
/* 5   unsupported transaction  CC2 + CC4 */
/* 6   unassigned CC2 + CC3 */
/* 7   unassigned CC2 + CC3 + CC4 */
/* 8   command accepted/queued, no echo status - CC1 */
/* 9   unassigned */
/* a   unassigned */
/* b   unassigned */
/* c   unassigned */
/* d   unassigned */
/* e   unassigned */
/* f   unassigned */

/* start an XIO operation */
/* when we get here the cpu has verified that there is a valid channel address */
/* and an interrupt entry in spad for the channel.  The IOCL address in the ICB */
/* has also been verified as present */
/* chan channel number 0-7f */
/* suba unit address within channel 0-ff */
/* Condition codes to return 0-f as specified above */
t_stat startxio(uint16 lchsa, uint32 *status) {
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    uint32  chan_icb;                           /* Interrupt level context block address */
    uint32  iocla;                              /* I/O channel IOCL address int ICB */
    int32   stat;                               /* return status 0/1 from loadccw */
    CHANP   *chp;                               /* channel program pointer */
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    uint16  chsa;
    uint32  tempa, inta, spadent, chan, incha;
    uint32  word1, word2, cmd;
    DEVICE  *dptr;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    inta = ((~spadent)>>16)&0x7f;               /* get interrupt level */
    chan = (spadent & 0x7f00) >> 8;             /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);        /* merge sa to real channel */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio entry inta %02x lchan %04x spadent %08x rchsa %04x\n",
        inta, lchan, spadent, chsa);

    dibp = dib_unit[chsa & 0x7f00];             /* get the device information pointer */
    uptr = find_unit_ptr(chsa & 0x7f00);        /* get unit 0 unit pointer */
    chan_icb = find_int_icb(lchsa);             /* Interrupt level context block address */
    incha = RMW(chan_icb+20);                   /* post inch addr in ICB+5w */

    /* check if we have a valid unit */
    chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
    if (chp == 0) goto missing;

    dibp = dib_unit[chsa];                      /* get the DIB pointer */
    if (dibp == 0) goto missing;

    uptr = find_unit_ptr(chsa);                 /* find pointer to unit on channel */

    if (uptr == 0) {                            /* if no dib or unit ptr, CC3 on return */
missing:
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio chsa %04x is not found, CC3 returned\n", chsa);
        return SCPE_OK;                         /* not found, CC3 */
    }

    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "startxio chsa %04x chp %p flags UNIT_ATTABLE %1x UNIT_ATT %1x UNIT_DIS %1x\n",
        chsa, chp, (uptr->flags & UNIT_ATTABLE)?1:0, (uptr->flags & UNIT_ATT)?1:0,
        (uptr->flags & UNIT_DIS)?1:0);

    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio chsa %04x device/unit disabled, CC3 returned flags %08x\n", chsa, uptr->flags);
        *status = CC3BIT;                       /* not attached, so error CC3 */
        return SCPE_OK;                         /* not found, CC3 */
    }

#ifndef FOR_DEBUG_01172021
    if ((INTS[inta]&INTS_ACT) || (SPAD[inta+0x80]&SINT_ACT)) { /* look for level active */
        /* just output a warning */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "SIOT Busy INTS ACT FIFO #%1x irq %02x SPAD %08x INTS %08x chan_byte %02x\n",
            FIFO_Num(SPAD[inta+0x80] & 0x7f00), inta, SPAD[inta+0x80], INTS[inta], chp->chan_byte);
    }
#endif

    incha = chp->chan_inch_addr;                /* get inch address */
    /* 05122021 cpu halts in diag if this code is enabled */
    /* disabling this code allows TE to be echoed at debugger prompt */
#ifndef TEST_FOR_HSDP_PUT_BACK_05122021
    /* channel not busy and ready to go, check for any status ready */
    /* see if any status ready to post */
    if (FIFO_Num(chsa&0x7f00)) {
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIOT chsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
            chsa, FIFO_Num(chsa), inta, incha, chp, chan_icb, chp->chan_byte);
        if (post_csw(chp, 0)) {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "SIOT chsa %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                chsa, FIFO_Num(chsa), inta, incha, RMW(chan_icb+20), chp->chan_byte);
            /* change status from BUFF_POST to BUFF_DONE */
            /* if not BUFF_POST we have a PPCI or channel busy interrupt */
            /* so leave the channel status alone */
            if (chp->chan_byte == BUFF_POST) {
                chp->chan_byte = BUFF_DONE;     /* show done & not busy */
            }
            sim_debug(DEBUG_XIO, &cpu_dev,
                "SIOT END status stored incha %06x chan_icba+20 %08x chsa %04x sw1 %08x sw2 %08x\n",
                incha, RMW(chan_icb+20), chsa, RMW(incha), RMW(incha+4));
            INTS[inta] &= ~INTS_REQ;            /* clear level request for no status */
//          INTS[inta] |= INTS_REQ;             /* set level request if no status */
            *status = CC2BIT;                   /* status stored from SIO, so CC2 */
            return SCPE_OK;                     /* No CC's all OK  */
        } else {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "SIOT chsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                 chsa, FIFO_Num(chsa), inta, incha, chan_icb, chp->chan_byte);
            /* now store the status dw address into word 5 of the ICB for the channel */
            WMW(chan_icb+20, 0);                /* post sw addr 0 in ICB+5w & reset CCs */
            *status = 0;                        /* no status stored from SIO, so no CC */
            return SCPE_OK;                     /* No CC's all OK  */
        }
    }
//TRY WMW(chan_icb+20, 0);                /* post sw addr 0 in ICB+5w & reset CCs */
    sim_debug(DEBUG_IRQ, &cpu_dev,
        "SIOT chsa %04x Nothing to post FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
         chsa, FIFO_Num(chsa), inta, incha, chan_icb, chp->chan_byte);
#endif

    /* check for a Command or data chain operation in progresss */
    if ((chp->chan_byte & BUFF_BUSY) && (chp->chan_byte != BUFF_POST)) {
        uint16  tstat = chp->chan_status;       /* save status */
        uint16  tcnt = chp->ccw_count;          /* save count */
        DEVICE  *dptr = get_dev(uptr);

        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio busy return CC3&CC4 chsa %04x chp %p cmd %02x flags %04x byte %02x\n",
            chsa, chp, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);
        /* ethernet controller wants an interrupt for busy status */
        if ((dptr != NULL) &&
            (DEV_TYPE(dptr) == DEV_ETHER)) {    /* see if this is ethernet */
            *status = CC1BIT;                   /* CCs = 1, SIO accepted & queued, no echo status */
            /* handle an Ethernet controller busy by sending interrupt/status */
            chp->chan_status = STATUS_BUSY|STATUS_CEND|STATUS_DEND; /* set busy status */
            chp->ccw_count = 0;                 /* zero count */
            push_csw(chp);                      /* store the status */
            chp->chan_status = tstat;           /* restore status */
            chp->ccw_count = tcnt;              /* restore count */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "startxio done BUSY/INT chp %p chsa %04x ccw_flags %04x stat %04x cnt %04x\n",
                chp, chsa, chp->ccw_flags, tstat, tcnt);
            return SCPE_OK;                     /* just busy CC3&CC4 */
        }
         /* see if controller has a IOCLQ defined to handle multiple SIO requests */
        /* keep processing SIO and handle busy later */
        if (dibp->ioclq_ptr == NULL) {          /* see if device has IOCL queue */
            /* everyone else just gets a busy return */
            *status = CC4BIT|CC3BIT;            /* busy, so CC3&CC4 */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "startxio done2 BUSY chp %p chsa %04x ccw_flags %04x stat %04x cnt %04x\n",
                chp, chsa, chp->ccw_flags, tstat, tcnt);
            return SCPE_OK;                     /* just busy CC3&CC4 */
        }
        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio busy ignored for IOCLQ chsa %04x chp %p cmd %02x flags %04x byte %02x\n",
            chsa, chp, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);
    }

#if 1
    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio int spad %08x icb %06x inta %02x chan %04x\n",
        SPAD[inta+0x80], chan_icb, inta, chan);
#endif

    /*  We have to validate all the addresses and parameters for the SIO */
    /* before calling load_ccw which does it again for each IOCL step */
    iocla = RMW(chan_icb+16);                   /* iocla is in wd 4 of ICB */
    word1 = RMW(iocla & MASK24);                /* get 1st IOCL word */
    word2 = RMW((iocla + 4) & MASK24);          /* get 2nd IOCL word */
    cmd = (word1 >> 24) & 0xff;                 /* get channel cmd from IOCL */
    chp = find_chanp_ptr(chsa&0x7f00);          /* find the parent chanp pointer */
    incha = chp->chan_inch_addr;                /* get inch address */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio do normal chsa %04x iocla %06x incha %06x IOCD1 %08x IOCD2 %08x\n",
        chsa, iocla, incha, RMW(iocla), RMW(iocla+4));

    chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
#ifdef TEST_FOR_IOCL_CHANGE
    chp->new_iocla = iocla;                     /* save iocla */
    chp->new_iocd1 = word1;                     /* save iocd word 1 */
    chp->new_iocd2 = word2;                     /* save iocd word 2 */ 
#endif
    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio test chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x\n",
        chsa, iocla, RMW(iocla), RMW(iocla+4));

    sim_debug(DEBUG_CMD, &cpu_dev, "SIO chsa %04x cmd %02x cnt %04x ccw_flags %04x\n",
        chsa, cmd, word2&MASK16, word2>>16);

    /* determine if channel DIB has a pre startio command processor */
    if (dibp->pre_io != NULL) {                 /* NULL if no startio function */
        DEVICE  *dptr = get_dev(uptr);          /* get device ptr */
        int     unit = uptr-dptr->units;        /* get unit number */

        /* call the device controller to get prestart_io status */
        tempa = dibp->pre_io(uptr, chan);       /* get status from device */
        /* SCPE_OK if unit not busy and IOCLQ is not full */
        /* SNS_BSY if unit IOCLQ is full */
        /* SNS_SMS if unit IOCLQ is not full, but device is busy */
        /* SNS_CTLEND if waiting for INCH command */
        if (tempa == SNS_CTLEND) {              /* see if sub channel status is ready */
            /* manual says to just return OK nad do nother if inch is required */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "SIO pre_io call return NO INCH %04x chsa %04x cstat %02x cmd %02x cnt %02x\n",
                incha, chsa, tempa, cmd, word2);
            if ((cmd != 0) || ((MASK16 & word2) == 0)) {
                *status = 0;                    /* request accepted, no status, so CC1 */
                return SCPE_OK;                 /* just do nothing until inch */
            }
        }
        if (tempa == SNS_BSY) {                 /* see if sub channel status is ready */
            /* The device must be busy or something, but it is not ready.  Return busy */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "startxio pre_io call return busy1 chan %04x cstat %08x\n", chan, tempa);
            *status = CC3BIT|CC4BIT;            /* sub channel busy, so CC3|CC4 */
            return SCPE_OK;                     /* just busy or something, CC3|CC4 */
        }
        if (tempa == SNS_SMS) {                 /* see if sub channel status is ready */
            if (dibp->ioclq_ptr == NULL) {      /* see if device has IOCL queue */
                /* The device must be busy or something, but it is not ready.  Return busy */
                /* This should not happen for SNS_SMS status */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "startxio pre_io call return busy2 chan %04x cstat %08x\n", chan, tempa);
                *status = CC3BIT|CC4BIT;        /* sub channel busy, so CC3|CC4 */
                return SCPE_OK;                 /* just busy or something, CC3|CC4 */
            }
            /* device has IOCLQ, queue up the iocla */
            if (IOCLQ_Put(&dibp->ioclq_ptr[unit], iocla) == -1) {
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "startxio IOCLQ_Put error return chsa %04x unit %02x\n", chsa, unit);
                *status = CC3BIT|CC4BIT;        /* sub channel busy, so CC3|CC4 */
                return SCPE_OK;                 /* just busy or something, CC3|CC4 */
            }
            sim_debug(DEBUG_XIO, &cpu_dev,
                "startxio IOCLQ_Put call sucessful count %02x chan %04x unit %02x\n",
                IOCLQ_Num(&dibp->ioclq_ptr[unit]), chan, unit);
            *status = CC1BIT;                   /* CCs = 1, SIO accepted & queued, no echo status */
            return SCPE_OK;                     /* CC1 all OK  */
        }
        /* device is not busy */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "startxio pre_io call return not busy chan %04x cstat %08x\n",
            chan, tempa);
    }       /* remove else 05132021 */

    /* check for a Command or data chain operation in progresss */
    if ((chp->chan_byte & BUFF_BUSY) && (chp->chan_byte != BUFF_POST)) {
        uint16  tstat = chp->chan_status;       /* save status */
        uint16  tcnt = chp->ccw_count;          /* save count */

        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio busy return CC3&CC4 chsa %04x chp %p cmd %02x flags %04x byte %02x\n",
            chsa, chp, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);
        /* everyone else just gets a busy return */
        *status = CC4BIT|CC3BIT;                /* busy, so CC3&CC4 */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "startxio done BUSY chp %p chsa %04x ccw_flags %04x stat %04x cnt %04x\n",
            chp, chsa, chp->ccw_flags, tstat, tcnt);
        return SCPE_OK;                         /* just busy CC3&CC4 */
    }

    /* channel not busy and ready to go, so start a new command */
    chp->chan_int = inta;                       /* save interrupt level in channel */
    chp->chan_status = 0;                       /* no channel status yet */
    chp->chan_caw = iocla;                      /* get iocla address in memory */
    /* added 09/25/20 to fix hangs in iocl processing */
    chp->ccw_flags = 0;                         /* clear flags */

    /* set status words in memory to first IOCD information */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "$$ SIO start IOCL processing chsa %04x iocla %08x incha %08x\n",
        chsa, iocla, incha);

    /* We are queueing the SIO */
    /* Queue us to continue IOCL from cpu level & make busy */
    chp->chan_byte = BUFF_NEXT;                 /* have main pick us up */
    chp->chan_info |= INFO_SIOCD;               /* show first IOCD in channel prog */
    chp->chan_info &= ~INFO_CEND;               /* show chan_end not called yet */

    /* start processing the IOCL */
    stat = load_ccw(chp, 0);                    /* start the I/O operation */
    if (stat) {
        /* we have an error or user requested interrupt, return status */
        sim_debug(DEBUG_EXP, &cpu_dev, "startxio store csw CC2 chan %04x status %08x\n",
            chan, chp->chan_status);
/*NOTE*//* if we have an error, we would loop forever if the CC bit was set */
        /* the only way to stop was to do a kill */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* reset chaining bits */
        /* DIAG's want CC1 with memory access error */
        if (chp->chan_status & STATUS_PCHK) {
            chp->chan_status &= ~STATUS_LENGTH; /* clear incorrect length */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "startxio Error1 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
                FIFO_Num(chsa), chan, chp->chan_status);
        } else {
            /* other error, stop the show */
            chp->chan_status &= ~STATUS_PCI;    /* remove PCI status bit */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "startxio Error2 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
                FIFO_Num(chsa), chan, chp->chan_status);
        }
        /* we get here when the start cmd has been processed without error */
        /* go wait for the cmd to finish */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "startxio start wait chsa %04x status %08x iocla %06x byte %02x\n",
            chsa, chp->chan_status, chp->chan_caw, chp->chan_byte);
    }
    sim_debug(DEBUG_XIO, &cpu_dev,
        "SIO started chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x incha %06x icb+20 %08x\n",
        chsa, iocla, RMW(iocla), RMW(iocla+4), incha, RMW(chan_icb+20));

    *status = CC1BIT;                           /* CCs = 1, SIO accepted & queued, no echo status */
    sim_debug(DEBUG_XIO, &cpu_dev, "SIO return chsa %04x status %08x iocla %08x CC's %08x byte %02x\n",
        chsa, chp->chan_status, iocla, *status, chp->chan_byte);
    return SCPE_OK;                             /* No CC's all OK  */
}

/* TIO - I/O status */
t_stat testxio(uint16 lchsa, uint32 *status) {  /* test XIO */
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    uint32  chan_icb;                           /* Interrupt level context block address */
    CHANP   *chp;                               /* Channel prog pointers */
    DEVICE  *dptr;                              /* Device ptr */
    uint32  inta, incha, itva;
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    uint32  spadent;
    uint16  rchan, rchsa;                       /* the real channel number, chsa */

    lchsa &= 0x7f00;                            /* use just chan and sa of 0 */
    /* get the real channel entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchsa = (spadent & 0x7f00);                 /* get real channel suba of zero */
    rchan = rchsa >> 8;                         /* get real channel */

    /* get the device entry for the channel in SPAD */
    dibp = dib_chan[rchan];                     /* get the DIB pointer */
    chp = find_chanp_ptr(rchan << 8);           /* find the device chanp pointer */

    if (dibp == 0 || chp == 0) {                /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "TIO lchsa %04x rchsa %04x device not present, CC3 returned\n", lchsa, rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    uptr = chp->unitptr;                        /* get the unit 0 ptr */
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "TIO rchsa %04x device/unit not enabled, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    /* the XIO opcode processing software has already checked for F class */
    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */
    itva = SPAD[0xf1] + (inta<<2);              /* int vector address */
    chan_icb = RMW(itva);                       /* Interrupt context block addr */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "TIO int spad %08x icb %06x inta %04x rchsa %04x\n",
        SPAD[inta+0x80], chan_icb, inta, rchsa);

    incha = chp->chan_inch_addr;                /* get inch address */

    /* see if any status ready to post */
    if (FIFO_Num(rchsa)) {
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "TIO rchsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
            rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);
        if (chp->chan_byte == BUFF_DONE) {
            chp->chan_byte = BUFF_POST;         /* if done, show post for post_csw() */
        }
        if (post_csw(chp, 0)) {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "TIO rchsa %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                rchsa, FIFO_Num(rchsa), inta, incha, RMW(chan_icb+20), chp->chan_byte);
            /* change status from BUFF_POST to BUFF_DONE */
            /* if not BUFF_POST we have a PPCI or channel busy interrupt */
            /* so leave the channel status alone */
            if (chp->chan_byte == BUFF_POST) {
                chp->chan_byte = BUFF_DONE;     /* show done & not busy */
            }
            sim_debug(DEBUG_XIO, &cpu_dev,
                "TIO END incha %06x chan_icba+20 %08x rchsa %04x sw1 %08x sw2 %08x\n",
                incha, RMW(chan_icb+20), rchsa, RMW(incha), RMW(incha+4));
            INTS[inta] &= ~INTS_REQ;            /* clear any level request if no status */
            *status = CC2BIT;                   /* status stored from SIO, so CC2 */
            return SCPE_OK;                     /* No CC's all OK  */
        } else {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "TIO rchsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                 rchsa, FIFO_Num(rchsa), inta, chp->chan_inch_addr, chan_icb, chp->chan_byte);
            /* now store the status dw address into word 5 of the ICB for the channel */
            WMW(chan_icb+20, 0);                /* post sw addr 0 in ICB+5w & reset CCs */
            *status = 0;                        /* no status stored from TIO, so no CC */
            return SCPE_OK;                     /* No CC's all OK  */
        }
    }

    /* nothing going on, so say all OK */
    /* now store the status dw address into word 5 of the ICB for the channel */
#ifdef FIXES_DMDIAG_TEST_11C_TIO_BUT_BREAKS_UTX
    WMW(chan_icb+20, 0x80000000);               /* post CC1 & sw addr 0 in ICB+5w & reset CCs */
    *status = CC4BIT; /* FIX FOR DIAG */        /* request accepted, not busy, so CC4 */
#else
    /* MPX 1X requires CC1 to be returned instead of CC2 or CC4 */
    /* MPX 1X will hang on boot if set to CC2 */
    WMW(chan_icb+20, 0x80000000);               /* post sw addr 0 in ICB+5w & reset CCs */
    *status = CC1BIT;                           /* request accepted, no status, so CC1 */
#endif
//  INTS[inta] &= ~INTS_REQ;                    /* clear any level request if no status */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "TIO END rchsa %04x rchan %04x ccw_flags %04x chan_stat %04x CCs %08x\n",
        rchsa, rchan, chp->ccw_flags, chp->chan_status, *status);
    return SCPE_OK;                             /* No CC's all OK  */
}

/* Stop XIO */
t_stat stopxio(uint16 lchsa, uint32 *status) {  /* stop XIO */
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    uint32  chan_icb;                           /* Interrupt level context block address */
    uint32  iocla, inta, itva;                  /* I/O channel IOCL address int ICB */
    CHANP   *chp;                               /* Channel prog pointers */
    DEVICE  *dptr;                              /* Device ptr */
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    uint32  spadent;
    uint16  rchan, rchsa;                       /* the real channel number, chsa */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchan = (spadent & 0x7f00) >> 8;            /* get real channel */
    rchsa = (rchan << 8) | (lchsa & 0xff);      /* get the read chan & suba */

    /* get the device entry for the logical channel in SPAD */
    dibp = dib_unit[rchsa];                     /* get the DIB pointer */
    chp = find_chanp_ptr(rchsa);                /* find the device chanp pointer */

    if (dibp == 0 || chp == 0) {                /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "STPIO test 1 rchsa %04x device not present, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* not found CC3 */
    }

    uptr = chp->unitptr;                        /* get the unit ptr */
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "STPIO rchsa %04x device/unit not enabled, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    /* the XIO opcode processing software has already checked for F class */
    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */
    itva = SPAD[0xf1] + (inta<<2);              /* int vector address */
    chan_icb = RMW(itva);                       /* Interrupt context block addr */
    iocla = RMW(chan_icb+16);                   /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_CMD, &cpu_dev,
        "STPIO busy test rchsa %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x\n",
        rchsa, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);
    /* reset the CC bit to force completion after current IOCD */
    chp->ccw_flags &= ~FLAG_CC;                 /* reset chaining bits */

    /* see if we have a stopio device entry */
    if (dibp->stop_io != NULL) {                /* NULL if no stop_io function */
        /* call the device controller to get stop_io status */
        int32 tempa = dibp->stop_io(uptr);      /* get status from device */

        /* test for SCPE_IOERR */
        /* CC's are returned in byte 0, status in bytes 2-3 */
        /* SCPR_OK is 0 */
        /* SCPR_IOERR is 2 */
        if ((tempa & RMASK) != SCPE_OK) {       /* sub channel has status ready */
            /* The device I/O has been terminated and status stored. */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "STPIO stop_io call return ERROR FIFO #%1x rchan %04x retstat %08x cstat %08x\n",
                FIFO_Num(rchsa), rchan, tempa, chp->chan_status);

            /* chan_end is called in stop device service routine */
            /* the device is no longer busy, post status */
            /* remove PPCI status.  Unit check should not be set */
            if ((tempa & LMASK) == CC2BIT) {
                chp->ccw_count = 0;             /* zero the count */
                /* post status for UTX */
                if (post_csw(chp, ((STATUS_PCI) << 16))) {
                    INTS[inta] &= ~INTS_REQ;    /* clear any level request */
                    *status = CC2BIT;           /* status stored */
                    sim_debug(DEBUG_CMD, &cpu_dev,
                        "STPIO END2 rchsa %04x rchan %04x cmd %02x ccw_flags %04x status %04x\n",
                        rchsa, rchan, chp->ccw_cmd, chp->ccw_flags, *status);
                    /* change status from BUFF_POST to BUFF_DONE */
                    if (chp->chan_byte == BUFF_POST) {
                        chp->chan_byte = BUFF_DONE; /* show done & not busy */
                    }
                    return SCPE_OK;             /* CC2 & all OK  */
                }
            } else {
                chp->ccw_count = 0;             /* zero the count */
                /* The diags want the interrupt for the disk */
                *status = CC1BIT;               /* request accepted, no status, so CC1 */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "STPIO END2 ECHO rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
                    rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
                return SCPE_OK;                 /* CC1 & all OK  */
            }
        }
        /* the channel is not busy, so return OK */
        *status = CC1BIT;                       /* request accepted, no status, so CC1 */
        sim_debug(DEBUG_CMD, &cpu_dev,
            "STPIO END3 rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
            rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
        return SCPE_OK;                         /* No CC's all OK  */
    }
    if ((chp->chan_byte & BUFF_BUSY) == 0) {
        /* the channel is not busy, so return OK */
        sim_debug(DEBUG_CMD, &cpu_dev,
            "STPIO not busy return rchsa %04x cmd %02x ccw_flags %04x status %04x byte %02x\n",
            rchsa, chp->ccw_cmd, chp->ccw_flags, *status, chp->chan_byte);
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "STPIO rchsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
            rchsa, FIFO_Num(rchsa), inta, chp->chan_inch_addr, chan_icb, chp->chan_byte);
        /* now store the status dw address into word 5 of the ICB for the channel */
        WMW(chan_icb+20, 0x80000000);           /* post sw addr 0 in ICB+5w & set CC 1*/
        *status = CC1BIT;                       /* show not busy, post no status with CC1 */
        return SCPE_OK;                         /* No CC's all OK  */
    }

    /* device does not have stop_io entry, so stop the I/O */
    /* check for a Command or data chain operation in progresss */
    /* set the return to CC3BIT & CC4BIT causes infinite loop in MPX1X */
    /* restore code to old CC1BIT return 12/21/2020 */
    // try using CC4 on MPX3X when still busy
    if (chp->chan_byte == BUFF_POST) {
        uint32  incha = chp->chan_inch_addr;    /* get inch address */
        *status = CC1BIT;                       /* request accepted, no status, so CC1 */
        /* see if any status ready to post */
        if (FIFO_Num(rchsa)) {
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "STPIO chsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);
            if (post_csw(chp, 0)) {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "STPIO chsa %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, RMW(chan_icb+20), chp->chan_byte);
                /* change status from BUFF_POST to BUFF_DONE */
                /* if not BUFF_POST we have a PPCI or channel busy interrupt */
                /* so leave the channel status alone */
                chp->chan_byte = BUFF_DONE;     /* show done & not busy */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "STPIO END incha %06x chan_icba+20 %08x chsa %04x sw1 %08x sw2 %08x\n",
                    incha, RMW(chan_icb+20), rchsa, RMW(incha), RMW(incha+4));
                INTS[inta] &= ~INTS_REQ;        /* clear any level request if no status */
                *status = CC2BIT;               /* status stored from SIO, so CC2 */
                return SCPE_OK;                 /* No CC's all OK  */
            } else {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "STPIOX chsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, chan_icb, chp->chan_byte);
                /* now store the status dw address into word 5 of the ICB for the channel */
                WMW(chan_icb+20, 0x80000000);   /* post CC1 & sw addr 0 in ICB+5w & reset CCs */
//              WMW(chan_icb+20, 0);            /* post sw addr 0 in ICB+5w & reset CCs */
                *status = CC1BIT;               /* show not busy, post no status with CC1 */
                return SCPE_OK;                 /* No CC's all OK  */
            }
        }
    } else {
        /* setting this to CC4 allows MPX mstrall to boot */ 
        /* having it set to CC1 allows diags to work, but not MPX 3X boot! */
        // This check allows DBUG2 and DIAGS to both work
        if (chp->chan_byte == BUFF_NEXT)
//BAD??     *status = CC1BIT;                   /* request accepted, no status, so CC1 */
            *status = CC4BIT; /* BAD FOR DIAG *//* request accepted, busy, so CC4 */
        else
            *status = CC4BIT; /* BAD FOR DIAG *//* request accepted, busy, so CC4 */
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "STPIO2 chsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
            rchsa, FIFO_Num(rchsa), inta, chp->chan_inch_addr, chan_icb, chp->chan_byte);
    }
    /* reset the CC bit to force completion after current IOCD */
    chp->ccw_flags &= ~FLAG_CC;                 /* reset chaining bits */
    sim_debug(DEBUG_CMD, &cpu_dev,
        "STPIO busy return CC1/4 rchsa %04x status %08x cmd %02x flags %04x byte %02x\n",
        rchsa, *status, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);
    return SCPE_OK;                             /* go wait CC1 */
}

/* Reset Channel XIO */
t_stat rschnlxio(uint16 lchsa, uint32 *status) {    /* reset channel XIO */
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    CHANP   *chp;                               /* Channel prog pointers */
    DEVICE  *dptr;                              /* Device ptr */
    int     i;
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    uint32  inta;
    uint32  spadent;
    uint16  rchan, rchsa;                       /* the real channel number */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchan = (spadent & 0x7f00) >> 8;            /* get real channel */
    rchsa = rchan << 8;                         /* get the real chan & zero suba */

    /* get the device entry for the logical channel in SPAD */
    dibp = dib_unit[rchsa];                     /* get the channel device information pointer */
    chp = find_chanp_ptr(rchsa);                /* find the channel chanp pointer */

    if (dibp == 0 || chp == 0) {                /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "rschnlxio test 1 dibp %p chp %p lchsa %04x rchsa %04x device not present, CC3 returned\n",
            dibp, chp, lchsa, rchsa);
        return SCPE_OK;                         /* not found CC3 */
    }

    uptr = chp->unitptr;                        /* get the unit ptr */
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "RSCHNL rchsa %04x device/unit not enabled, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */

    /* reset this channel */
    dibp->chan_fifo_in = 0;                     /* reset the FIFO pointers */
    dibp->chan_fifo_out = 0;                    /* reset the FIFO pointers */
    chp->chan_inch_addr = 0;                    /* remove inch status buffer address */
    chp->base_inch_addr = 0;                    /* clear the base inch addr */
    chp->max_inch_addr = 0;                     /* clear the last inch addr */
    INTS[inta] &= ~INTS_ACT;                    /* clear level active */
    SPAD[inta+0x80] &= ~SINT_ACT;               /* clear in spad too */

    /* now go through all the sa for the channel and stop any IOCLs */
    for (i=0; i<SUB_CHANS; i++) {
        int     j;
        rchsa = (rchan<<8) | i;                 /* merge sa to real channel */
        dibp = dib_unit[rchsa];                 /* get the DIB pointer */
        if (dibp == 0)
            continue;                           /* not used */
        chp = find_chanp_ptr(rchsa);            /* find the chanp pointer */
        if (chp == 0)
            continue;                           /* not used */
        uptr = chp->unitptr;                    /* get the unit ptr */

        /* see if we have a rschnl device entry */
        if (dibp->rschnl_io != NULL) {          /* NULL if no rschnl_io function */
            /* call the device controller to process rschnl */
            j = dibp->rschnl_io(uptr);          /* get status from device */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "rschnl_io returned %02x chsa %04x\n", j, rchsa);
        }
        chp->chan_status = 0;                   /* clear the channel status */
        chp->chan_byte = BUFF_EMPTY;            /* no data yet */
        chp->ccw_addr = 0;                      /* clear buffer address */
        chp->chan_caw = 0x0;                    /* clear IOCD address */
        chp->ccw_count = 0;                     /* channel byte count 0 bytes*/
        chp->ccw_flags = 0;                     /* clear flags */
        chp->ccw_cmd = 0;                       /* read command */
        chp->chan_inch_addr = 0;                /* clear inch addr */
        chp->base_inch_addr = 0;                /* clear the base inch addr */
        chp->max_inch_addr = 0;                 /* clear the last inch addr */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "rschnlxio return CC1 lchan %02x lchan %02x inta %04x\n",
        lchan, rchan, inta);
    *status = CC1BIT;                           /* request accepted, no status, so CC1 TRY THIS */
    return SCPE_OK;                             /* All OK */
}

/* HIO - Halt I/O */
t_stat haltxio(uint16 lchsa, uint32 *status) {  /* halt XIO */
    DIB     *dibp;
    UNIT    *uptr;
    uint32  chan_icb;                           /* Interrupt level context block address */
    uint32  iocla;                              /* I/O channel IOCL address int ICB */
    uint32  inta, spadent, tempa, itva;
    uint16  lchan = get_chan(lchsa);
    uint16  rchan, rchsa;
    CHANP   *chp;                               /* Channel prog pointers */
    DEVICE  *dptr;                              /* DEVICE pointer */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchan = (spadent & 0x7f00) >> 8;            /* get real channel */
    rchsa = (rchan << 8) | (lchsa & 0xff);      /* merge sa to real channel */
    dibp = dib_unit[rchsa];                     /* get the device DIB pointer */
    chp = find_chanp_ptr(rchsa);                /* find the chanp pointer */

    if (dibp == 0 || chp == 0) {                /* if no dib or chan ptr, CC3 on return */
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO lchsa %04x rchsa %04x device not present, CC3 returned\n", lchsa, rchsa);
        return SCPE_OK;                         /* not found, CC3 */
    }
    uptr = chp->unitptr;                        /* get the unit ptr */
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO rchsa %04x device/unit not enabled, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_EXP, &cpu_dev, "HIO dev spad %08x lchsa %04x rchsa %04x\n", spadent, lchsa, rchsa);

    /* the haltio opcode processing software has already checked for F class */
    inta = ((~spadent)>>16)&0x7f;               /* get channel interrupt level */
    chp->chan_int = inta;                       /* make sure it is set in channel */
    sim_debug(DEBUG_EXP, &cpu_dev, "HIO int spad %08x inta %02x rchan %02x\n", spadent, inta, rchan);

    /* get the address of the interrupt IVL in main memory */
    itva = SPAD[0xf1] + (inta<<2);              /* int vector address */
    chan_icb = RMW(itva);                       /* Interrupt context block addr */
    iocla = RMW(chan_icb+16);                   /* iocla is in wd 4 of ICB */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "HIO busy test byte %02x rchsa %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x\n",
        chp->chan_byte, rchsa, chp->ccw_cmd, chp->ccw_flags, RMW(iocla), RMW(iocla+4));

    /* the channel is busy, so process */
    /* see if we have a haltio device entry */
    if (dibp->halt_io != NULL) {                /* NULL if no haltio function */
        /* call the device controller to get halt_io status */
        tempa = dibp->halt_io(uptr);            /* get status from device */

        /* CC's are returned in bits 1-4.  Bits 16-31 has SCPE code */
        /* SCPE_IOERR is 2 */
        /* SCPE_OK is 0 */
        if ((tempa & RMASK) != SCPE_OK) {       /* sub channel has status ready */
            uint32  incha = chp->chan_inch_addr;    /* get inch address */
            /* The device I/O has been terminated and status stored. */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "HIO halt_io call return ERROR FIFO #%1x rchsa %04x retstat %08x cstat %08x\n",
                FIFO_Num(rchsa), rchsa, tempa, chp->chan_status);

            /* chan_end is called in hio device service routine */
            /* the device is no longer busy, post status */
            /* The diags want the interrupt for the disk */
            *status = CC1BIT;                   /* request accepted, no status, so CC1 */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "HIO END2X ECHO rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
                rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
//          irq_pend = 1;                       /* flag to test for int condition */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "HIO rchsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);

            /* see if user wants to have status posted and setting CC2 in return value */
            if ((tempa & LMASK) == CC2BIT) {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "HIO rchsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);
                /* post any status */
                if (post_csw(chp, 0)) {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "HIO rchsa %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                        rchsa, FIFO_Num(rchsa), inta, incha, RMW(chan_icb+20), chp->chan_byte);
                    /* change status from BUFF_POST to BUFF_DONE */
                    /* if not BUFF_POST we have a PPCI or channel busy interrupt */
                    /* so leave the channel status alone */
                    if (chp->chan_byte == BUFF_POST) {
                        chp->chan_byte = BUFF_DONE;     /* show done & not busy */
                    }
                    sim_debug(DEBUG_XIO, &cpu_dev,
                        "HIO END incha %06x chan_icba+20 %08x rchsa %04x sw1 %08x sw2 %08x\n",
                        incha, RMW(chan_icb+20), rchsa, RMW(incha), RMW(incha+4));
                    /*ADDED 111921 to disable int request after data posted */
                    INTS[inta] &= ~INTS_REQ;    /* clear any level request */
                    *status = CC2BIT;           /* status stored from SIO, so CC2 */
                    return SCPE_OK;             /* No CC's all OK  */
                }
            }
            /* see if user wants to have status posted by setting CC4 in return value */
            if ((tempa & LMASK) == CC4BIT) {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "HIO rchsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);
            }
            sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO END2Y rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
                rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
            return SCPE_OK;                     /* CC1 & all OK  */
        }
        /* the device is not busy, so cmd is completed */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO BUFF_DONE1 chp %p chan_byte %04x\n", chp, chp->chan_byte);
        /* the channel is not busy, so return OK */
        *status = CC1BIT;                       /* request accepted, post good status, so CC1 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO END3 rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
            rchsa, chp->ccw_cmd, chp->ccw_flags, *status);

#ifndef GIVE_INT_ON_NOT_BUSY_121420_03082021
        chp->chan_byte = BUFF_DONE;             /* we are done */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "haltxio BUFF_DONE2 chp %p chan_byte %04x\n", chp, chp->chan_byte);
        if ((dptr != NULL) &&
            (DEV_TYPE(dptr) == DEV_ETHER)) {    /* see if this is ethernet */
            /* Ethernet does not want SNS_UNITEXP */
            chp->chan_status = (STATUS_DEND|STATUS_CEND);
        } else {
            chp->chan_status = (STATUS_DEND|STATUS_CEND|STATUS_EXPT);
        }
        push_csw(chp);                          /* store the status 1st in FIFO */
        /* change chan_byte to BUFF_POST */
        chp->chan_byte = BUFF_POST;             /* show done with data */
        chp->chan_status = 0;                   /* no status anymore */
        chp->ccw_cmd = 0;                       /* no command anymore */
        irq_pend = 1;                           /* flag to test for int condition */
#endif
        return SCPE_OK;                         /* No CC's all OK  */
    }

    /* device does not have a HIO entry, so terminate the I/O */
    if ((chp->chan_byte & BUFF_BUSY) == 0) {
        /* the channel is not busy, so return OK */
        *status = CC1BIT;                       /* request accepted, no status, so CC1 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO END1 not busy return rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
            rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
        chp->chan_byte = BUFF_DONE;             /* we are done */
        chp->chan_status = (STATUS_DEND|STATUS_CEND|STATUS_EXPT);
        store_csw(chp);                         /* store the status */
        /* change chan_byte to BUFF_POST */
        chp->chan_byte = BUFF_POST;             /* show done with data */
        chp->chan_status = 0;                   /* no status anymore */
        chp->ccw_cmd = 0;                       /* no command anymore */
        irq_pend = 1;                           /* flag to test for int condition */
        return SCPE_OK;                         /* CC1 & all OK  */
    }

    /* device does not have a HIO entry, so terminate the I/O */
    /* a haltxio entry should be provided for a device so busy can be cleared */
    /* check for a Command or data chain operation in progresss */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "HIO device busy lchsa %04x rchsa %04x\n", lchsa, rchsa);

    /* reset the DC or CC bits to force completion */
    chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* reset chaining bits */
    chp->chan_byte = BUFF_BUSY;             /* wait for post_csw to be done */
    sim_cancel(uptr);                       /* cancel timer service */
    chp->chan_status &= ~STATUS_BUSY;       /* remove BUSY status bit */
    chan_end(rchsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP); /* show I/O complete */

    /* post the channel status */
    chp->ccw_count = 0;                     /* zero the count */
    /* remove SLI, PPCI and Unit check status bits */
    if (post_csw(chp, ((STATUS_PCI) << 16))) {
        INTS[inta] &= ~INTS_REQ;            /* clear any level request */
        *status = CC2BIT;                   /* status stored from SIO, so CC2 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "HIO END4 rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
            rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
        /* change status from BUFF_POST to BUFF_DONE */
        if (chp->chan_byte == BUFF_POST) {
            chp->chan_byte = BUFF_DONE;     /* show done & not busy */
        }
        return SCPE_OK;                     /* CC2 & all OK  */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "HIO END5 rchsa %04x cmd %02x ccw_flags %04x status %04x\n",
        rchsa, chp->ccw_cmd, chp->ccw_flags, *status);
    return SCPE_OK;                             /* No CC's all OK  */
}

/* grab controller n/u */
/* TODO return unimplemented function error, not busy */
t_stat grabxio(uint16 lchsa, uint32 *status) {  /* grab controller XIO n/u */
    DIB     *dibp;                              /* device information pointer */
    UNIT    *uptr;                              /* pointer to unit in channel */
    CHANP   *chp;
    DEVICE  *dptr;                              /* Device ptr */
    uint16  lchan = get_chan(lchsa);            /* get the logical channel number */
    uint32  spadent;
    uint16  rchan, rchsa;                       /* the real channel number, chsa */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                      /* get spad device entry for logical channel */
    rchan = (spadent & 0x7f00) >> 8;            /* get real channel */
    rchsa = (rchan << 8) | (lchsa & 0xff);      /* merge sa to real channel */

    /* get the device entry for the logical channel in SPAD */
    dibp = dib_unit[rchsa];                     /* get the DIB pointer */
    chp = find_chanp_ptr(rchsa);                /* find the device chanp pointer */

    if (dibp == 0 || chp == 0) {                /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                       /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "GRIO test 1 rchsa %04x device not present, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* not found CC3 */
    }

    sim_debug(DEBUG_CMD, &cpu_dev,
        "GRIO entry rchsa %04x status %08x cmd %02x flags %02x byte %02x\n",
        rchsa, *status, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);

    uptr = chp->unitptr;                        /* get the unit ptr */
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        /* is device/unit disabled? */
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "GRIO rchsa %04x device/unit not enabled, CC3 returned\n", rchsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        *status = CC4BIT;                       /* busy, so CC4 */
        sim_debug(DEBUG_CMD, &cpu_dev,
            "GRIO busy return CC4 lchsa %04x rchsa %04x status %08x\n",
            lchsa, rchsa, *status);
        return SCPE_OK;                         /* CC4 all OK  */
    }

// NOW ON 05142021 */
    /* device does not have stop_io entry, so stop the I/O */
    /* check for a Command or data chain operation in progresss */
    /* set the return to CC3BIT & CC4BIT causes infinite loop in MPX1X */
    /* restore code to old CC1BIT return 12/21/2020 */
    // try using CC4 on MPX3X when still busy
    if (chp->chan_byte == BUFF_POST) {
        uint32  chan_icb;                       /* Interrupt level context block address */
        uint32  inta = ((~spadent)>>16)&0x7f;   /* get channel interrupt level */
        /* get the address of the interrupt IVL in main memory */
        uint32  itva = SPAD[0xf1] + (inta<<2);  /* int vector address */
        chan_icb = RMW(itva);                   /* Interrupt context block addr */
        *status = CC1BIT;                       /* request accepted, no status, so CC1 */
        /* see if any status ready to post */
        if (FIFO_Num(rchsa)) {
            uint32  incha = chp->chan_inch_addr;    /* get inch address */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "GRIO chsa %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                rchsa, FIFO_Num(rchsa), inta, incha, chp, chan_icb, chp->chan_byte);
            if (post_csw(chp, 0)) {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "GRIO chsa %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, RMW(chan_icb+20), chp->chan_byte);
                /* change status from BUFF_POST to BUFF_DONE */
                /* if not BUFF_POST we have a PPCI or channel busy interrupt */
                /* so leave the channel status alone */
                chp->chan_byte = BUFF_DONE;     /* show done & not busy */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "GRIO END incha %06x chan_icba+20 %08x chsa %04x sw1 %08x sw2 %08x\n",
                    incha, RMW(chan_icb+20), rchsa, RMW(incha), RMW(incha+4));
                INTS[inta] &= ~INTS_REQ;        /* clear any level request if no status */
                *status = CC2BIT;               /* status stored from SIO, so CC2 */
                return SCPE_OK;                 /* No CC's all OK  */
            } else {
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "GRIO chsa %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                    rchsa, FIFO_Num(rchsa), inta, incha, chan_icb, chp->chan_byte);
                /* now store the status dw address into word 5 of the ICB for the channel */
                WMW(chan_icb+20, 0);            /* post sw addr 0 in ICB+5w & reset CCs */
                *status = 0;                    /* no status stored from STPIO, so no CC */
                return SCPE_OK;                 /* No CC's all OK  */
            }
        }
    }

    /* If this is console, debugger wants CC3 & CC4 = 0 */
    if (rchan == 0x7e) {
        /* returning No CC's causes MPX1X to loop forever */
        /* so restore returning CC1 */
        *status = 0;                            /* return no CC's */
    } else {
        /* diags want unsupported transaction for disk */
        *status = CC2BIT|CC4BIT;                /* unsupported transaction */
    }
    sim_debug(DEBUG_CMD, &cpu_dev,
        "GRIO lchsa %04x rchsa %04x status %08x\n", lchsa, rchsa, *status);
    return SCPE_OK;                             /* dono */
}

/* reset controller XIO */
t_stat rsctlxio(uint16 lchsa, uint32 *status) { /* reset controller XIO */
    DIB     *dibp;
    UNIT    *uptr;
    uint32  spadent;
    uint16  chsa;
    CHANP   *chp;
    int     lev, i;
    uint32  chan = get_chan(lchsa);
    DEVICE  *dptr;                              /* get device ptr */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                       /* get spad device entry for logical channel */
    chan = spadent & 0x7f00;                    /* get real channel */
    chsa = chan;                                /* use just channel */
    dibp = dib_unit[chsa];                      /* get the DIB pointer */
    chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
    uptr = chp->unitptr;                        /* get the unit ptr */

    sim_debug(DEBUG_EXP, &cpu_dev, "rsctlxio 1 chan %04x SPAD %08x\n", chsa, spadent);
    if (dibp == 0 || uptr == 0) {               /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                       /* not found, so CC3 */
        return SCPE_OK;                         /* not found, CC3 */
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "rsctlxio 2 chan %04x spad %08x\r\n", chsa, spadent);
    /* is device or unit marked disabled? */
    dptr = get_dev(uptr);                       /* get device ptr */

    /* is device/unit disabled? */
    if ((dptr->flags & DEV_DIS) || ((uptr->flags & UNIT_DIS) && 
        ((uptr->flags & UNIT_SUBCHAN) == 0))) {
        *status = CC3BIT;                       /* not enabled, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "RSCTL rchsa %04x device/unit not enabled, CC3 returned\n", chsa);
        return SCPE_OK;                         /* Not found, CC3 */
    }
    lev = find_int_lev(chan);                   /* get our int level */
    INTS[lev] &= ~INTS_ACT;                     /* clear level active */
    SPAD[lev+0x80] &= ~SINT_ACT;                /* clear spad too */
    INTS[lev] &= ~INTS_REQ;                     /* clear level request */

    /* now go through all the sa for the channel and stop any IOCLs */
    for (i=0; i<SUB_CHANS; i++) {
        int     j;
        IOCLQ   *qp;                            /* IOCLQ pointer */
        int     unit;                           /* get the UNIT number */

        chsa = chan | i;                        /* merge sa to real channel */
        dibp = dib_unit[chsa];                  /* get the DIB pointer */
        if (dibp == 0) {
            continue;                           /* not used */
        }
        chp = find_chanp_ptr(chsa);             /* find the chanp pointer */
        if (chp == 0) {
            continue;                           /* not used */
        }
        /* reset the FIFO pointers */
        dibp->chan_fifo_in = 0;                 /* set no FIFO entries */
        dibp->chan_fifo_out = 0;                /* set no FIFO entries */

        uptr = chp->unitptr;                    /* get the unit ptr */
        unit = uptr - dptr->units;              /* get the UNIT number */
        if (dibp->ioclq_ptr != NULL) {
            qp = &dibp->ioclq_ptr[unit];        /* IOCLQ pointer */
            if (qp != NULL) {
                qp->ioclq_in = 0;               /* clear any entries */
                qp->ioclq_out = 0;              /* clear any entries */
            }
        }

        /* see if we have a rsctl device entry */
        if (dibp->rsctl_io != NULL) {           /* NULL if no rsctl_io function */
            /* call the device controller to process rsctl */
            j = dibp->rsctl_io(uptr);           /* get status from device */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "rsctl_io returned %02x chsa %04x\n", j, chsa);
        }
        chp->chan_status = 0;                   /* clear the channel status */
        chp->chan_byte = BUFF_EMPTY;            /* no data yet */
        chp->ccw_addr = 0;                      /* clear buffer address */
        chp->chan_caw = 0x0;                    /* clear IOCD address */
        chp->ccw_count = 0;                     /* channel byte count 0 bytes*/
        chp->ccw_flags = 0;                     /* clear flags */
        chp->ccw_cmd = 0;                       /* read command */
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "rsctlxio return CC1 chan %04x lev %04x\n", chan, lev);
    /* returning 0 for status breaks ethernet controller */
    if ((dptr != NULL) &&
        (DEV_TYPE(dptr) == DEV_ETHER)) {        /* see if this is ethernet */
        *status = CC1BIT;                       /* request accepted, no status, so CC1 */
    } else
        *status = 0;                            /* request accepted, no status, return 0 */
    return SCPE_OK;                             /* All OK */
}

/* boot from the device (ch/sa) the caller specified */
/* on CPU reset, the cpu has set the IOCD data at location 0-4 */
t_stat chan_boot(uint16 chsa, DEVICE *dptr) {
    int     chan = get_chan(chsa);
    DIB     *dibp = (DIB *)dptr->ctxt;          /* get pointer to DIB for this device */
    UNIT    *uptr = find_unit_ptr(chsa);        /* find pointer to unit on channel */
    CHANP   *chp = 0;

    sim_debug(DEBUG_EXP, &cpu_dev, "Channel Boot chan/device addr %04x SNS %08x\n", chsa, uptr->u5);
    if (dibp == 0)                              /* if no channel or device, error */
        return SCPE_IOERR;                      /* error */
    if (dibp->chan_prg == NULL)                 /* must have channel information for each device */
        return SCPE_IOERR;                      /* error */
    chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
    if (chp == 0)                               /* if no channel, error */
        return SCPE_IOERR;                      /* error */

    /* make sure there is an IOP/MFP configured at 7e00 on system */
    if (dib_chan[0x7e] == NULL) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nIOP/MFP device 0x7e00 not configured on system, aborting\n");
        printf("ERROR===ERROR\nIOP/MFP device 0x7e00 not configured on system, aborting\n");
        return SCPE_UNATT;                      /* error */
    }

    /* make sure there is an IOP/MFP console configured at 7efc/7efd on system */
    if ((dib_unit[0x7efc] == NULL) || (dib_unit[0x7efd] == NULL)) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nCON device 0x7efc/0x7ecd not configured on system, aborting\n");
        printf("ERROR===ERROR\nCON device 0x7efc/0x7efd not configured on system, aborting\n");
        return SCPE_UNATT;                      /* error */
    }

    chp->chan_status = 0;                       /* clear the channel status */
    chp->chan_dev = chsa;                       /* save our address (ch/sa) */
    chp->chan_byte = BUFF_EMPTY;                /* no data yet */
    chp->ccw_addr = 0;                          /* start loading at loc 0 */
    chp->chan_caw = 0x0;                        /* set IOCD address to memory location 0 */
    chp->ccw_count = 0;                         /* channel byte count 0 bytes*/
    chp->ccw_flags = 0;                         /* Command chain and supress incorrect length */
    chp->chan_info = INFO_SIOCD;                /* show first IOCD in channel prog */
    chp->ccw_cmd = 0;                           /* read command */
    /* moved here to not destry loc 0-0x14 on reset/go cmds */
    M[0] = 0x02000000;                  /* 0x00 IOCD 1 read into address 0 */
    M[1] = 0x60000078;                  /* 0x04 IOCD 1 CMD Chain, Suppress incor length, 120 bytes */
    M[2] = 0x53000000;                  /* 0x08 IOCD 2 BKSR or RZR to re-read boot code */
    M[3] = 0x60000001;                  /* 0x0C IOCD 2 CMD chain,Supress incor length, 1 byte */
    M[4] = 0x02000000;                  /* 0x10 IOCD 3 Read into address 0 */
    M[5] = 0x000006EC;                  /* 0x14 IOCD 3 Read 0x6EC bytes */
    loading = chsa;                             /* show we are loading from the boot device */

    sim_debug(DEBUG_CMD, &cpu_dev, "Channel Boot calling load_ccw chan %04x status %08x\n",
        chan, chp->chan_status);

    /* start processing the boot IOCL at loc 0 */
    if (load_ccw(chp, 0)) {                     /* load IOCL starting from location 0 */
        sim_debug(DEBUG_EXP, &cpu_dev, "Channel Boot Error return from load_ccw chan %04x status %08x\n",
            chan, chp->chan_status);
        chp->ccw_flags = 0;                     /* clear the command flags */
        chp->chan_byte = BUFF_DONE;             /* done with errors */
        loading = 0;                            /* show we are done loading from the boot device */
        return SCPE_IOERR;                      /* return error */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "Channel Boot OK return from load_ccw chsa %04x status %04x\n",
        chsa, chp->chan_status);
    return SCPE_OK;                             /* all OK */
}

/* Continue a channel program for a device */
uint32 cont_chan(uint16 chsa)
{
    int32   stat;                               /* return status 0/1 from loadccw */
    CHANP   *chp = find_chanp_ptr(chsa);        /* channel program */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "cont_chan entry chp %p chan_byte %02x chsa %04x addr %06x\n",
        chp, chp->chan_byte, chsa, chp->ccw_addr);
    /* we have entries, continue channel program */
    if (chp->chan_byte != BUFF_NEXT) {
        /* channel program terminated already, ignore entry */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan chan_byte %02x is NOT BUFF_NEXT chsa %04x addr %06x\n",
            chp->chan_byte, chsa, chp->ccw_addr);
        return 1;
    }
    if (chp->chan_byte == BUFF_NEXT) {
        uint32  chan = get_chan(chsa);
        sim_debug(DEBUG_XIO, &cpu_dev,
            "cont_chan resume chan prog chsa %04x iocla %06x\n",
            chsa, chp->chan_caw);

        /* start a channel program */
        stat = load_ccw(chp, 1);                /* resume the channel program */
        /* we get status returned if there is an error on the startio cmd call */
        if (stat) {
            /* we have an error or user requested interrupt, return status */
            sim_debug(DEBUG_EXP, &cpu_dev, "cont_chan error, store csw chsa %04x status %08x\n",
                chsa, chp->chan_status);
/*NOTE*/    /* if we have an error, we would loop forever if the CC bit was set */
            /* the only way to stop was to do a kill */
            chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* reset chaining bits */
            /* DIAG's want CC1 with memory access error */
            if (chp->chan_status & STATUS_PCHK) {
                chp->chan_status &= ~STATUS_LENGTH; /* clear incorrect length */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "cont_chan Error1 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
                    FIFO_Num(chsa), chan, chp->chan_status);
                return SCPE_OK;                 /* done */
            }
            /* other error, stop the show */
            chp->chan_status &= ~STATUS_PCI;    /* remove PCI status bit */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "cont_chan Error2 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
                FIFO_Num(chsa), chan, chp->chan_status);
            return SCPE_OK;                     /* done */
        }
        /* we get here when the start cmd has been processed without error */
        /* go wait for the cmd to finish */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "cont_chan continue wait chsa %04x status %08x iocla %06x byte %02x\n",
            chsa, chp->chan_status, chp->chan_caw, chp->chan_byte);
        return SCPE_OK;                         /* done, status stored */
    }
    /* must be more IOCBs, wait for them */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "cont_chan continue not next chsa %04x status %08x iocla %06x\n",
        chsa, chp->chan_status, chp->chan_caw);
    return SCPE_OK;
}

/* Scan all channels and see if one is ready to start or has
   interrupt pending. Return icb address and interrupt level
*/
uint32 scan_chan(uint32 *ilev) {
    int         i;
    uint32      chsa;                           /* No device */
    uint32      chan;                           /* channel num 0-7f */
    uint32      tempa;                          /* icb address */
    uint32      incha;                          /* inch address */
    uint32      chan_ivl;                       /* int level table address */
    uint32      chan_icba;                      /* Interrupt level context block address */
    CHANP       *chp;                           /* channel prog pointer */
    DIB         *dibp;                          /* DIB pointer */
    uint32      sw1, sw2;                       /* status words */

    /* see if we are loading */
    if (loading) {
        /* we are loading see if chan prog complete */
        /* get the device entry for the logical channel in SPAD */
        chan = loading & 0x7f00;                /* get real channel and zero sa */
        dibp = dib_unit[chan];                  /* get the IOP/MFP DIB pointer */
        if (dibp == 0)
             return 0;                          /* skip unconfigured channel */
        /* see if status is stored in FIFO */
        /* see if the FIFO is empty */
        if ((FIFO_Num(chan)) && ((FIFO_Get(chan, &sw1) == 0) &&
            (FIFO_Get(chan, &sw2) == 0))) {
            /* the SPAD entries are not set up, so no access to icb or ints */
            /* get the status from the FIFO and throw it away */
            /* we really should post it to the current inch address */
            /* there is really no need to post, but it might be helpfull */
            chp = find_chanp_ptr(chan);         /* find the chanp pointer for channel */
            /* this address most likely will be zero here */
            tempa = chp->chan_inch_addr;        /* get inch status buffer address */
            /* before overwriting memory loc 0+4, save PSD for caller in TPSD[] locations */
            TPSD[0] = M[0];                     /* save PSD from loc 0&4 */
            TPSD[1] = M[1];
            /* save the status double word to memory */
            /* set BIT 1 to show status stored */
            WMW(tempa, sw1|BIT1);               /* save sa & IOCD address in status WD 1 loc */
            WMW(tempa+4, sw2);                  /* save status and residual cnt in status WD 2 loc */
            chp->chan_byte = BUFF_DONE;         /* we are done */
            sim_debug(DEBUG_IRQ, &cpu_dev,
            "LOADING %06x %04x FIFO #%1x read inch %06x sw1 %08x sw2 %08x\n",
            chp->chan_caw, chan, FIFO_Num(chan), tempa, sw1|BIT1, sw2);
            return loading;
        }
        return 0;                               /* not ready, return */
    }

    /* ints not blocked, so look for highest requesting interrupt */
    for (i=0; i<112; i++) {
        if (SPAD[i+0x80] == 0)                  /* not initialize? */
            continue;                           /* skip this one */
        if ((SPAD[i+0x80]&MASK24) == MASK24)    /* not initialize? */
            continue;                           /* skip this one */
        if (INTS[i] & INTS_REQ)                 /* if already requesting, skip */
            continue;                           /* skip this one */

        /* see if there is pending status for this channel */
        /* if there is and the level is not requesting, do it */
        /* get the device entry for the logical channel in SPAD */
        chan = (SPAD[i+0x80] & 0x7f00);         /* get real channel and zero sa */
        dibp = dib_chan[get_chan(chan)];        /* get the channel device information pointer */
        if (dibp == 0)                          /* we have a channel to check */
            continue;                           /* not defined, skip this one */

        /* we have a channel to check */
        /* check for pending status */
        if (FIFO_Num(chan)) {
            INTS[i] |= INTS_REQ;                /* turn on channel interrupt request */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "scan_chan FIFO REQ FIFO #%1x irq %02x SPAD %08x INTS %08x\n",
                FIFO_Num(SPAD[i+0x80] & 0x7f00), i, SPAD[i+0x80], INTS[i]);
#ifdef TRY_DEBUG_01172021
            irq_pend = 1;                       /* we have pending interrupt */
#endif
            continue;
        }
    }

    /* see if we are able to look for ints */
    if (CPUSTATUS & BIT24)                      /* interrupts blocked? */
        return 0;                               /* yes, done */

    /* now go process the highest requesting interrupt */
    for (i=0; i<112; i++) {
        if (SPAD[i+0x80] == 0)                  /* not initialize? */
            continue;                           /* skip this one */
        /* this is a bug fix for MPX 1.x restart command */
        if ((SPAD[i+0x80]&MASK24) == MASK24)    /* not initialize? */
            continue;                           /* skip this one */
        /* stop looking if an active interrupt is found */
        if ((INTS[i]&INTS_ACT) || (SPAD[i+0x80]&SINT_ACT)) { /* look for level active */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "scan_chan INTS ACT irq %02x SPAD %08x INTS %08x\n",
                i, SPAD[i+0x80], INTS[i]);
            return 0;                           /* this level active, so stop looking */
        }

        if ((INTS[i] & INTS_ENAB) == 0) {       /* ints must be enabled */
            continue;                           /* skip this one */
        }

        /* look for the highest requesting interrupt */
        /* that is enabled */
        if (((INTS[i] & INTS_ENAB) && (INTS[i] & INTS_REQ)) ||
            ((SPAD[i+0x80] & SINT_ENAB) && (INTS[i] & INTS_REQ))) {

            sim_debug(DEBUG_IRQ, &cpu_dev,
                "scan_chan highest int req irq %02x SPAD %08x INTS %08x\n",
                i, SPAD[i+0x80], INTS[i]);

            /* requesting, make active and turn off request flag */
            INTS[i] &= ~INTS_REQ;               /* turn off request */
            INTS[i] |= INTS_ACT;                /* turn on active */
            SPAD[i+0x80] |= SINT_ACT;           /* show active in SPAD too */

            /* get the address of the interrupt IVL table in main memory */
            chan_ivl = SPAD[0xf1] + (i<<2);     /* contents of spad f1 points to chan ivl in mem */
            chan_icba = RMW(chan_ivl);          /* get the interrupt context block addr in memory */

            /* see if there is pending status for this channel */
            /* get the device entry for the logical channel in SPAD */
            chan = (SPAD[i+0x80] & 0x7f00);     /* get real channel and zero sa */
            dibp = dib_chan[get_chan(chan)];    /* get the channel device information pointer */
            if (dibp == 0) {                    /* see if we have a channel to check */
                /* not a channel, must be clk or ext int */
                *ilev = i;                      /* return interrupt level */
                irq_pend = 0;                   /* not pending anymore */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "scan_chan %04x POST NON FIFO irq %02x chan_icba %06x SPAD[%02x] %08x\n",
                    chan, i, chan_icba, i+0x80, SPAD[i+0x80]);
                return(chan_icba);              /* return ICB address */
            }
            /* must be a device, get status ready to post */
            if (FIFO_Num(chan)) {
                /* new 051020 find actual device with the channel program */
                /* not the channel, that is not correct most of the time */
                tempa = dibp->chan_fifo[dibp->chan_fifo_out];   /* get SW1 of FIFO entry */
                chsa = chan | (tempa >> 24);    /* find device address for requesting chan prog */
                chp = find_chanp_ptr(chsa);     /* find the chanp pointer for channel */
                incha = chp->chan_inch_addr;    /* get inch status buffer address */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "scan_chan %04x LOOK FIFO #%1x irq %02x inch %06x chp %p icba %06x chan_byte %02x\n",
                    chsa, FIFO_Num(chan), i, incha, chp, chan_icba, chp->chan_byte);
                if (post_csw(chp, 0)) {
                    /* change status from BUFF_POST to BUFF_DONE */
                    /* if not BUFF_POST we have a PPCI or channel busy interrupt */
                    /* so leave the channel status alone */
                    if (chp->chan_byte == BUFF_POST) {
                        chp->chan_byte = BUFF_DONE; /* show done & not busy */
                    }
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "scan_chanx %04x POST FIFO #%1x irq %02x inch %06x chan_icba+20 %08x chan_byte %02x\n",
                        chan, FIFO_Num(chan), i, incha, RMW(chan_icba+20), chp->chan_byte);
                } else {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "scan_chanx %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                        chan, FIFO_Num(chan), i, incha, chan_icba, chp->chan_byte);
                }
                *ilev = i;                      /* return interrupt level */
                irq_pend = 0;                   /* not pending anymore */
                return(chan_icba);              /* return ICB address */
            } else {
                /* we had an interrupt request, but no status is available */
                /* clear the interrupt and go on */
                /* this is a fix for MPX1X restart 092220 */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "scan_chan highest int has no stat irq %02x SPAD %08x INTS %08x\n",
                    i, SPAD[i+0x80], INTS[i]);

                /* requesting, make active and turn off request flag */
                INTS[i] &= ~INTS_ACT;           /* turn off active int */
                SPAD[i+0x80] &= ~SINT_ACT;      /* clear active in SPAD too */
            }
        }
    }
    /* if the interrupt is not zero'd here, we get SPAD error */
    irq_pend = 0;                               /* not pending anymore */
    return 0;                                   /* done */
}

/* part of find_dev_from_unit(UNIT *uptr) in scp.c */
/* Find_dev pointer for a unit
   Input:  uptr = pointer to unit
   Output: dptr = pointer to device
*/
DEVICE *get_dev(UNIT *uptr)
{
    DEVICE *dptr = NULL;
    uint32 i, j;

    if (uptr == NULL)                           /* must be valid unit */
        return NULL;
    if (uptr->dptr)                             /* get device pointer from unit */
        return uptr->dptr;                      /* return valid pointer */

    /* the device pointer in the unit is not set up, do it now */
    /* This should never happen as the pointer is setup in first reset call */
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) { /* do all devices */
        for (j = 0; j < dptr->numunits; j++) {  /* do all units for device */
            if (uptr == (dptr->units + j)) {    /* match found? */
                uptr->dptr = dptr;              /* set the pointer in unit */
                return dptr;                    /* return valid pointer */
            }
        }
    }
    return NULL;
}

/* set up the devices configured into the simulator */
/* only devices with a DIB will be processed */
t_stat chan_set_devs() {
    uint32 i, j;

    for (i = 0; i < MAX_DEV; i++) {
        dib_unit[i] = NULL;                     /* clear DIB pointer array */
    }
    for (i = 0; i < MAX_CHAN; i++) {
        dib_chan[i] = NULL;                     /* clear DIB pointer array */
    }
    /* Build channel & device arrays */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE  *dptr = sim_devices[i];         /* get pointer to next configured device */
        UNIT    *uptr = dptr->units;            /* get pointer to units defined for this device */
        DIB     *dibp = (DIB *)dptr->ctxt;      /* get pointer to DIB for this device */
        CHANP   *chp;                           /* channel program pointer */
        int     chsa;                           /* addr of device chan & subaddress */

        /* set the device back pointer in the unit structure */
        for (j = 0; j < dptr->numunits; j++) {  /* loop through unit entries */
            uptr->dptr = dptr;                  /* set the device pointer in unit structure */
            uptr++;                             /* next UNIT pointer */
        }
        uptr = dptr->units;                     /* get pointer to units again */

        if (dibp == NULL)                       /* If no DIB, not channel device */
            continue;
        if ((dptr->flags & DEV_DIS) ||          /* Skip disabled devices */
            ((dibp->chan_prg) == NULL)) {       /* must have channel info for each device */
            chsa = GET_UADDR(uptr->u3);         /* ch/sa value */
            continue;
        }

        chp = (CHANP *)dibp->chan_prg;          /* must have channel information for each device */
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {  /* loop through unit entries */
            chsa = GET_UADDR(uptr->u3);         /* ch/sa value */
            /* zero some channel data loc's for device */
            chp->unitptr = uptr;                /* set the unit back pointer */
            chp->chan_status = 0;               /* clear the channel status */
            chp->chan_dev = chsa;               /* save our address (ch/sa) */
            chp->chan_byte = BUFF_EMPTY;        /* no data yet */
            chp->ccw_addr = 0;                  /* start loading at loc 0 */
            chp->chan_caw = 0;                  /* set IOCD address to memory location 0 */
            chp->ccw_count = 0;                 /* channel byte count 0 bytes*/
            chp->ccw_flags = 0;                 /* Command chain and supress incorrect length */
            chp->ccw_cmd = 0;                   /* read command */
            chp->chan_inch_addr = 0;            /* clear curr address of stat dw in memory */
            chp->base_inch_addr = 0;            /* clear base address of stat dw in memory */
            chp->max_inch_addr = 0;             /* clear last address of stat dw in memory */

            /* is unit marked disabled? */
            if ((uptr->flags & UNIT_DIS) == 0 || (uptr->flags & UNIT_SUBCHAN) != 0) {
                /* see if this is unit zero */
                if ((chsa & 0xff) == 0) {
                    /* we have channel mux or dev 0 of units */
                    if (dptr->flags & DEV_CHAN) {
                        /* see if channel address already defined */
                        if (dib_chan[get_chan(chsa)] != 0) {
                            return SCPE_IERR;               /* no, arg error */
                        }
                        /* channel mux, save dib for channel */
                        dib_chan[get_chan(chsa)] = dibp;
                        if (dibp->dev_ini != NULL)  /* if there is an init routine, call it now */
                            dibp->dev_ini(uptr, 1); /* init the channel */
                    } else {
                        /* we have unit 0 of non-IOP/MFP device */
                        if (dib_unit[chsa] != 0) {
                            return SCPE_IERR;   /* no, arg error */
                        } else {
                            /* channel mux, save dib for channel */
                            /* for now, save any zero dev as chan */
                            if (chsa) {
                                dib_unit[chsa] = dibp;  /* no, save the dib address */
                                if (dibp->dev_ini != NULL)  /* if there is an init routine, call it now */
                                    dibp->dev_ini(uptr, 1); /* init the channel */
                            }
                        }
                    }
                } else {
                    /* see if address already defined */
                    if (dib_unit[chsa] != 0) {
                        return SCPE_IERR;       /* no, arg error */
                    }
                    dib_unit[chsa] = dibp;      /* no, save the dib address */
                }
            }
            if (dibp->dev_ini != NULL)          /* call channel init if defined */
                dibp->dev_ini(uptr, 1);         /* init the channel */
            uptr++;                             /* next UNIT pointer */
            chp++;                              /* next CHANP pointer */
        }
    }
    /* now make another pass through the channels and see which integrated */
    /* channel/controllers are defined and add them to the dib_chan definitions */
    /* this will handle non-MFP/IOP channel controllers */
    for (i = 0; i < MAX_CHAN; i++) {
        if (dib_chan[i] == 0) {
            /* channel not defined, see if defined in dib_unit array */
            /* check device zero for suspected channel */
            if (dib_unit[i<<8]) {
                /* write dibp to channel array */
                dib_chan[i] = dib_unit[i<<8];   /* save the channel dib */
            }
        } else {
            /* channel is defined, see if defined in dib_unit array */
            if ((dib_unit[i<<8]) == 0) {
                /* write dibp to units array */
                dib_unit[i<<8] = dib_chan[i];   /* save the channel dib */
            }
        }
    }
    return SCPE_OK;                             /* all is OK */
}

/* Validate and set the device onto a given channel */
t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
    DEVICE  *dptr;                              /* device pointer */
    DIB     *dibp;                              /* dib pointer */
    UNIT    *tuptr;                             /* temp unit pointer */
    t_value chan;                               /* new channel addr */
    t_stat  r;                                  /* return status */
    int     i;                                  /* temp */
    int     chsa, ochsa;                        /* dev addr */

    if (cptr == NULL)                           /* is there a UNIT name specified */
        return SCPE_ARG;                        /* no, arg error */
    if (uptr == NULL)                           /* is there a UNIT pointer */
        return SCPE_IERR;                       /* no, arg error */
    dptr = get_dev(uptr);                       /* find the device from unit pointer */
    if (dptr == NULL) {                         /* device not found, so error */
        fprintf(stderr, "Set dev no DEVICE cptr %s uptr %p\r\n", cptr, uptr);
        return SCPE_IERR;                       /* error */
    }

    dibp = (DIB *)dptr->ctxt;                   /* get dib pointer from device struct */
    if (dibp == NULL) {                         /* we need a DIB */
        fprintf(stderr, "Set dev no DIB ptr %s uptr %p\r\n", cptr, uptr);
        return SCPE_IERR;                       /* no DIB, so error */
    }

    chan = get_uint(cptr, 16, 0xffff, &r);      /* get new device address */
    if (r != SCPE_OK)                           /* need good number */
        return r;                               /* number error, return error */

    dibp->chan_addr = chan;                     /* set new parent channel addr */

    /* change all the unit addresses with the new channel, but keep sub address */
    /* Clear out existing entries for all units on this device */
    tuptr = dptr->units;                        /* get pointer to units defined for this device */

    /* loop through all units for this device */
    for (i = 0; i < dibp->numunits; i++) {
        int mask=dibp->mask;                    /* sa bits that are used */
        ochsa = GET_UADDR(tuptr->u3);           /* get old chsa for this unit */
        dib_unit[ochsa] = NULL;                 /* clear sa dib pointer */
        dib_unit[ochsa&0x7f00] = NULL;          /* clear the channel dib address */
        chan &= ~mask;                          /* remove the unit number */
        chsa = chan | (ochsa & mask);           /* put in new sa */
        if (chsa != ochsa) {
            fprintf(stderr, "Set unit %x new chsa %04x old chsa %04x\r\n", i, chsa, ochsa);
        }
        tuptr->u3 &= ~UNIT_ADDR_MASK;           /* clear old chsa for this unit */
        tuptr->u3 |= UNIT_ADDR(chsa);           /* set new chsa for this unit */
        dib_unit[chan&0x7f00] = dibp;           /* set the channel dib address */
        dib_unit[chsa] = dibp;                  /* save the dib address for new chsa */
        tuptr++;                                /* next unit pointer */
    }
    return SCPE_OK;
}

/* display channel/sub-address for device */
t_stat show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc) {
    DEVICE      *dptr;
    int         chsa;

    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error return */
    dptr = get_dev(uptr);                       /* get the device pointer from unit */
    if (dptr == NULL)                           /* valid pointer? */
        return SCPE_IERR;                       /* return error */
    chsa = GET_UADDR(uptr->u3);                 /* get the unit address */
    fprintf(st, "CHAN/SA %04x", chsa);          /* display channel/subaddress */
    return SCPE_OK;                             /* we done */
}

