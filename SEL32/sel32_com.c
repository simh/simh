/* sel32_com.c: SEL 32 8-Line IOP communications controller

   Copyright (c) 2018-2021, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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

#include "sel32_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#if NUM_DEVS_COM > 0

/* Constants */
#define COM_LINES       8                   /* lines defined */
//Change from 500 to 5000 12/02/2021
//Change from 5000 to 4000 12/02/2021
//#define COML_WAIT       500
#define COML_WAIT       4000
//Change from 1000 to 5000 12/02/2021
//#define COM_WAIT        5000
//#define COM_WAIT        1000
#define COM_WAIT        5000
#define COM_NUMLIN      com_desc.lines      /* curr # lines */

#define COMC            0                   /* channel thread */
#define COMI            1                   /* input thread */

/* Line status */
#define COML_XIA        0x01                /* xmt intr armed */
#define COML_XIR        0x02                /* xmt intr req */
#define COML_REP        0x04                /* rcv enable pend */
#define COML_RBP        0x10                /* rcv break pend */

struct _com_data
{
    uint8   incnt;                          /* char count */
    uint8   ibuff[120];                     /* Input line buffer */
}
com_data[COM_LINES];

uint8 com_rbuf[COM_LINES];                  /* rcv buf */
uint8 com_xbuf[COM_LINES];                  /* xmt buf */
uint8 com_stat[COM_LINES];                  /* status */

TMLN com_ldsc[COM_LINES] = { 0 };           /* line descrs */
TMXR com_desc = { COM_LINES, 0, 0, com_ldsc }; /* com descr */

#define CMD u3
/* Held in u3 is the device command and status */
#define COM_INCH    0x00                    /* Initialize channel command */
#define COM_WR      0x01                    /* Write terminal */
#define COM_RD      0x02                    /* Read terminal */
#define COM_NOP     0x03                    /* No op command */
#define COM_SNS     0x04                    /* Sense command */
#define COM_WRSCM   0x05                    /* Write w/Sub chan monitor */
#define COM_RDECHO  0x06                    /* Read with Echo */
#define COM_RDFC    0x0A                    /* Read w/flow control */
#define COM_DEFSC   0x0B                    /* Define special char */
#define COM_WRHFC   0x0D                    /* Write hardware flow control */
#define COM_RRDFLOW 0x0E                    /* Read w/hardware flow control only RTS */
#define COM_RDTR    0x13                    /* Reset DTR (ADVR) */
#define COM_SDTR    0x17                    /* Set DTR (ADVF) */
#define COM_RRTS    0x1B                    /* Reset RTS */
#define COM_SRTS    0x1F                    /* Set RTS */
#define COM_RBRK    0x33                    /* Reset BREAK */
#define COM_SBRK    0x37                    /* Set BREAK */
#define COM_SETFLOW 0x53                    /* Set transparent flow control mode */
#define COM_RDHFC   0x8E                    /* Read w/hardware flow control only DTR */
#define COM_SACE    0xFF                    /* Set ACE parameters */

#define COM_MSK     0xFF                    /* Command mask */

/* Status held in CMD (u3) */
/* controller/unit address in upper 16 bits */
#define COM_INPUT   0x0100                  /* Input ready for unit */
//#define COM_RDY     0x0200  /* SNS_DSRS */  /* Device is ready */
#define COM_SCD     0x0400                  /* Special char detect */
#define COM_EKO     0x0800                  /* Echo input character */
#define COM_OUTPUT  0x1000                  /* Output ready for unit */
#define COM_READ    0x2000                  /* Read mode selected */
#define COM_ACC     0x4000                  /* ASCII control char detect */
#define COM_CONN    0x8000  /* TMXR ATT */  /* Terminal connected */

/* ACE data kept in u4 */
/* ACE information in u4 */
#define ACE u4
#if 0
/* ACE byte 0  Modem Control/Operation status */
/* stored in u4 bytes 0-3 */
#define SNS_HALFD   0x80000000  /* Half-duplix operation set */
#define SNS_MRINGE  0x40000000  /* Modem ring enabled */
#define SNS_ACEFP   0x20000000  /* Forced parity 0=odd, 1=even */
#define SNS_ACEP    0x10000000  /* Parity 0=odd, 1=even */
#define SNS_ACEPE   0x08000000  /* Parity enable 0=dis, 1=enb */
#define SNS_ACESTOP 0x04000000  /* Stop bit 0=1, 1=1.5 or 2 */
#define SNS_ACECLEN 0x02000000  /* Character length 00=5, 01=6, 10=7, 11=8 */
#define SNS_ACECL2  0x01000000  /* 2nd bit for above */

/* ACE byte 1  Baud rate */
#define SNS_NODCDA  0x00800000  /* Enable Delta DCD Attention Interrupt */
#define SNS_WAITOLB 0x00400000  /* Wait on last byte enabled */
#define SNS_RINGCR  0x00200000  /* Ring or wakeup character recognition 0=enb, 1=dis */
#define SNS_DIAGL   0x00100000  /* Set diagnostic loopback */
#define SNS_BAUD    0x000F0000  /* Baud rate bits 4-7 */
#define BAUD50      0x00000000  /* 50 baud */
#define BAUD75      0x00010000  /* 75 baud */
#define BAUD110     0x00020000  /* 110 baud */
#define BAUD114     0x00030000  /* 134 baud */
#define BAUD150     0x00040000  /* 150 baud */
#define BAUD300     0x00050000  /* 300 baud */
#define BAUD600     0x00060000  /* 600 baud */
#define BAUD1200    0x00070000  /* 1200 baud */
#define BAUD1800    0x00080000  /* 1800 baud */
#define BAUD2000    0x00090000  /* 2000 baud */
#define BAUD2400    0x000A0000  /* 2400 baud */
#define BAUD3600    0x000B0000  /* 3600 baud */
#define BAUD4800    0x000C0000  /* 4800 baud */
#define BAUD7200    0x000D0000  /* 7200 baud */
#define BAUD9600    0x000E0000  /* 9600 baud */
#define BAUD19200   0x000F0000  /* 19200 baud */

/* ACE byte 2  Wake-up character */
#define ACE_WAKE    0x0000FF00              /* 8 bit wake-up character */
#endif
#define ACE_WAKE    0x0000FF00              /* 8 bit wake-up character in byte 2 of ACE */

#define SNS u5
/* in u5 packs sense byte 0, 1, 2 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000              /* Command reject */
#define SNS_INTVENT 0x40000000              /* Unit intervention required (N/U) */
#define SNS_BOCHK   0x20000000              /* Bus out check (IOP parity error */
#define SNS_EQUIPCK 0x10000000              /* Equipment check (device error) */
#define SNS_DATACK  0x08000000              /* Data check */
#define SNS_OVERRN  0x04000000              /* Overrun (N/U) */
#define SNS_NUB01   0x02000000              /* Zero (N/U) */
#define SNS_RDY     SNS_NUB01               /* SNS_RDY device ready */
#define SNS_NUB02   0x01000000              /* Zero (N/U) */
#define SNS_CONN    SNS_NUB02               /* SNS_CONN device connected */
/* Sense byte 1 */
#define SNS_ASCIICD 0x00800000              /* ASCII control char detected interrupt */
#define SNS_SPCLCD  0x00400000              /* Special char detected interrupt */
#define SNS_ETX     0x00200000              /* ETX interrupt */
#define SNS_BREAK   0x00100000              /* BREAK interrupt */
#define SNS_ACEFE   0x00080000              /* ACE framing error interrupt */
#define SNS_ACEPEI  0x00040000              /* ACE parity error interrupt */
#define SNS_ACEOVR  0x00020000              /* ACE overrun error interrupt */
#define SNS_RING    0x00010000              /* Ring character interrupt */
/* Sense byte 2  Modem status */
#define SNS_RLSDS   0x00008000              /* Received line signal detect */
#define SNS_RINGST  0x00004000              /* Ring indicator signal detect */
#define SNS_DSRS    0x00002000              /* DSR Data set ready line status */
#define SNS_CTSS    0x00001000              /* CTS Clear to send line status */
#define SNS_DELTA   0x00000800              /* Delta receive line signal detect failure interrupt */
#define SNS_MRING   0x00000400              /* RI Modem ring interrupt */
#define SNS_DELDSR  0x00000200              /* DSR failure interrupt */
#define SNS_DELCTS  0x00000100              /* CLS failure interrupt */
/* Sense byte 3  Modem Control/Operation status */
#define SNS_HALFD   0x00000080              /* Half-duplix operation set */
#define SNS_MRINGE  0x00000040              /* Modem ring enabled (1) */
#define SNS_ACEDEF  0x00000020              /* ACE parameters defined */
#define SNS_DIAGM   0x00000010              /* Diagnostic mode set */
#define SNS_AUXOL2  0x00000008              /* Auxiliary output level 2 */
#define SNS_AUXOL1  0x00000004              /* Auxiliary output level 1 */
#define SNS_RTS     0x00000002              /* RTS Request to send set */
#define SNS_DTR     0x00000001              /* DTR Data terminal ready set */
/* Sense byte 4  ACE Parameters status */
#define SNS_ACEDLE  0x80000000              /* Divisor latch enable 0=dis, 1=enb */
#define SNS_ACEBS   0x40000000              /* Break set 0=reset, 1=set */
#define SNS_ACEFP   0x20000000              /* Forced parity 0=odd, 1=even */
#define SNS_ACEP    0x10000000              /* Parity 0=odd, 1=even */
#define SNS_ACEPE   0x08000000              /* Parity enable 0=dis, 1=enb */
#define SNS_ACESTOP 0x04000000              /* Stop bit 0=1, 1=1.5 or 2 */
#define SNS_ACECLEN 0x02000000              /* Character length 00=5, 01=6, 11=7, 11=8 */
#define SNS_ACECL2  0x01000000              /* 2nd bit for above */
/* Sense byte 5  Baud rate */
#define SNS_NODCDA  0x00800000              /* Enable Delta DCD Attention Interrupt */
#define SNS_WAITOLB 0x00400000              /* Wait on last byte enabled */
#define SNS_RINGCR  0x00200000              /* Ring or wakeup character recognition 0=enb, 1=dis */
#define SNS_DIAGL   0x00100000              /* Set diagnostic loopback */
#define SNS_BAUD    0x000F0000              /* Baud rate bits 4-7 */
#define BAUD50      0x00000000              /* 50 baud */
#define BAUD75      0x00010000              /* 75 baud */
#define BAUD110     0x00020000              /* 110 baud */
#define BAUD114     0x00030000              /* 134 baud */
#define BAUD150     0x00040000              /* 150 baud */
#define BAUD300     0x00050000              /* 300 baud */
#define BAUD600     0x00060000              /* 600 baud */
#define BAUD1200    0x00070000              /* 1200 baud */
#define BAUD1800    0x00080000              /* 1800 baud */
#define BAUD2000    0x00090000              /* 2000 baud */
#define BAUD2400    0x000A0000              /* 2400 baud */
#define BAUD3600    0x000B0000              /* 3600 baud */
#define BAUD4800    0x000C0000              /* 4800 baud */
#define BAUD7200    0x000D0000              /* 7200 baud */
#define BAUD9600    0x000E0000              /* 9600 baud */
#define BAUD19200   0x000F0000              /* 19200 baud */
/* Sense byte 6  Firmware ID, Revision Level */
#define SNS_FID     0x00006200              /* ID part 1 */
/* Sense byte 7  Firmware ID, Revision Level */
#define SNS_REV     0x0000004f              /* ID part 2 plus 4 bit rev # */

/* u6 */
#define CNT u6

/* forward definitions */
t_stat      coml_preio(UNIT *uptr, uint16 chan);
t_stat      coml_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd);
t_stat      coml_haltio(UNIT *uptr);
void        com_ini(UNIT *, t_bool);
void        coml_ini(UNIT *, t_bool);
t_stat      coml_rschnlio(UNIT *uptr);
t_stat      com_rschnlio(UNIT *uptr);
t_stat      comi_srv(UNIT *uptr);
t_stat      como_srv(UNIT *uptr);
t_stat      comc_srv(UNIT *uptr);
t_stat      com_reset(DEVICE *dptr);
t_stat      com_attach(UNIT *uptr, CONST char *cptr);
t_stat      com_detach(UNIT *uptr);
void        com_reset_ln(int32 ln);
const char  *com_description(DEVICE *dptr);     /* device description */

/* COM data structures
    com_chp     COM channel program information
    com_dev     COM device descriptor
    com_unit    COM unit descriptor
    com_reg     COM register list
    com_mod     COM modifiers list
*/

//#define COM_UNITS 2
#define COM_UNITS 1

/* channel program information */
CHANP           com_chp[COM_UNITS] = {0};

/* dummy mux for 16 lines */
MTAB            com_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT", &tmxr_dscln, NULL, &com_desc},
    {UNIT_ATT, UNIT_ATT, "SUMMARY", NULL, NULL, &tmxr_show_summ, (void *) &com_desc},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL, NULL, &tmxr_show_cstat,(void *)&com_desc},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL, NULL, &tmxr_show_cstat, (void *)&com_desc},
    { 0 }
};

UNIT            com_unit[] = {
    {UDATA(&comc_srv, UNIT_ATTABLE|UNIT_IDLE, 0), COM_WAIT, UNIT_ADDR(0x0000)},       /* 0 */
};

DIB             com_dib = {
    NULL,           /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    NULL,           /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    NULL,           /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    com_rschnlio,   /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    com_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    com_unit,       /* UNIT* units */                           /* Pointer to units structure */
    com_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    COM_UNITS,      /* uint8 numunits */                        /* number of units defined */
    0x0f,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7E00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

REG             com_reg[] = {
    { BRDATAD (STA, com_stat, 16, 8, COM_LINES, "status buffers, lines 0 to 7") },
    { BRDATAD (RBUF, com_rbuf, 16, 8, COM_LINES, "input buffer, lines 0 to 7") },
    { BRDATAD (XBUF, com_xbuf, 16, 8, COM_LINES, "output buffer, lines 0 to 7") },
    { NULL }
    };

/* devices for channel 0x7ecx */
DEVICE          com_dev = {
    "COMC", com_unit, com_reg, com_mod,
    COM_UNITS, 8, 15, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &com_reset, NULL, &com_attach, &com_detach,
    /* ctxt is the DIB pointer */
    &com_dib, DEV_MUX|DEV_DISABLE|DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &com_description
};

/* COML data structures
    coml_dev    COM device descriptor
    coml_unit   COM unit descriptor
    coml_reg    COM register list
    coml_mod    COM modifiers list
*/

/*#define UNIT_COML UNIT_ATTABLE|UNIT_DISABLE|UNIT_IDLE */
//#define UNIT_COML UNIT_IDLE|UNIT_DISABLE|TT_MODE_UC
//#define UNIT_COML UNIT_IDLE|UNIT_DISABLE|TT_MODE_7P
//#define UNIT_COML UNIT_IDLE|UNIT_DISABLE|TT_MODE_8B
#define UNIT_COML UNIT_IDLE|UNIT_DISABLE|TT_MODE_7B

/* channel program information */
CHANP           coml_chp[COM_LINES*2] = {0};

UNIT            coml_unit[] = {
    /* 0-7 is input, 8-f is output */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA0)},  /* 0 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA1)},  /* 1 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA2)},  /* 2 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA3)},  /* 3 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA4)},  /* 4 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA5)},  /* 5 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA6)},  /* 6 */
    {UDATA(&comi_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA7)},  /* 7 */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA8)},  /* 8 */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EA9)},  /* 9 */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAA)},  /* A */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAB)},  /* B */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAC)},  /* C */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAD)},  /* D */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAE)},  /* E */
    {UDATA(&como_srv, UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EAF)},  /* F */
};

DIB             coml_dib = {
    coml_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    coml_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    coml_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    coml_rschnlio,  /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    coml_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    coml_unit,      /* UNIT* units */                           /* Pointer to units structure */
    coml_chp,       /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    COM_LINES*2,    /* uint8 numunits */                        /* number of units defined */
    0x0f,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7E00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

REG             coml_reg[] = {
    { URDATA (TIME, coml_unit[0].wait, 10, 24, 0, COM_LINES, REG_NZ + PV_LEFT) },
    { NULL }
};

MTAB            coml_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &com_desc },
    { 0 }
    };

DEVICE          coml_dev = {
    "COML", coml_unit, coml_reg, coml_mod,
    COM_LINES*2, 10, 31, 1, 8, 8,
    NULL, NULL, &com_reset,
    NULL, NULL, NULL,
    /* ctxt is the DIB pointer */
    &coml_dib, DEV_DISABLE|DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &com_description
};

/* 8-line serial routines */
void coml_ini(UNIT *uptr, t_bool f)
{
    /* set SNS_RLSDS SNS_DSRS SNS_CTSS SNS_RTS SNS_CTS */
    uptr->SNS = 0x0000b003;                     /* status is online & ready */
    uptr->CMD &= LMASK;                         /* leave only chsa */
    sim_cancel(uptr);                           /* stop any timer */
}

/* handle rschnlio cmds for coml */
t_stat  coml_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & COM_MSK;

    sim_debug(DEBUG_EXP, dptr,
        "coml_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    coml_ini(uptr, 0);                          /* reset the unit */
    return SCPE_OK;
}

/* 8-line serial routines */
void com_ini(UNIT *uptr, t_bool f)
{
    DEVICE *dptr = get_dev(uptr);

    sim_debug(DEBUG_CMD, dptr,
        "COM init device %s controller 0x7e00\n", dptr->name);
    sim_cancel(uptr);                           /* stop input poll */
    sim_activate(uptr, 1000);                   /* start input poll */
}

/* handle rschnlio cmds for com */
t_stat  com_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & COM_MSK;

    sim_debug(DEBUG_EXP, dptr,
        "com_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    com_ini(uptr, 0);                           /* reset the unit */
    return SCPE_OK;
}

/* start a com operation */
t_stat coml_preio(UNIT *uptr, uint16 chan) {
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    uint16      chsa = GET_UADDR(uptr->CMD);    /* get channel/sub-addr */
    UNIT        *ruptr = &dptr->units[unit&7];  /* read uptr */
    UNIT        *wuptr = &dptr->units[(unit&7)+8];  /* write uptr */

    sim_debug(DEBUG_CMD, dptr,
        "coml_preio CMD %08x unit %02x chsa %04x\n",
        uptr->CMD, unit, chsa);
    sim_debug(DEBUG_CMD, dptr,
        "coml_preio chsa %04x ln %1x conn %x rcve %x xmte %x SNS %08x SNS %08x\n",
        chsa, unit, com_ldsc[unit&7].conn, com_ldsc[unit&7].rcve,
        com_ldsc[unit&7].xmte, ruptr->SNS, wuptr->SNS);

    if ((uptr->CMD & COM_MSK) != 0) {           /* just return if busy */
        sim_debug(DEBUG_CMD, dptr,
            "coml_preio unit %02x chsa %04x BUSY\n", unit, chsa);
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr,
        "coml_preio unit %02x chsa %04x OK\n", unit, chsa);
    return SCPE_OK;                             /* good to go */
}

/* called from sel32_chan to start an I/O operation */
t_stat  coml_startcmd(UNIT *uptr, uint16 chan, uint8 cmd)
{
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
//  int         unit = (uptr - dptr->units) & 0x7;  /* make 0-7 */
    UNIT        *ruptr = &dptr->units[unit&7];  /* read uptr */
    UNIT        *wuptr = &dptr->units[(unit&7)+8];  /* write uptr */
    uint16      chsa = (((uptr->CMD & LMASK) >> 16) | (chan << 8));
    uint8       ch, fcb[3];

    if ((uptr->CMD & COM_MSK) != 0) {           /* is unit busy */
        return SNS_BSY;                         /* yes, return busy */
    }

    sim_debug(DEBUG_CMD, dptr,
        "coml_startcmd chsa %04x line %1x cmd %02x conn %x rcve %x xmte %x SNS %08x SNS %08x\n",
        chsa, unit, cmd, com_ldsc[unit&7].conn, com_ldsc[unit&7].rcve,
        com_ldsc[unit&7].xmte, ruptr->SNS, wuptr->SNS);

    uptr->CMD &= LMASK;                         /* clear any flags that are set */
    /* process the commands */
    switch (cmd & 0xFF) {
    case COM_INCH:      /* 00 */                /* INCH command */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: CMD INCH\n", chsa);
        uptr->CMD &= LMASK;                     /* leave only chsa */
        uptr->CMD |= (0x7f & COM_MSK);          /* save 0x7f as INCH cmd command */
        uptr->SNS |= SNS_RDY;                   /* status is online & ready */
        sim_activate(uptr, 500);                /* start us up */
        break;

    /* write commands must use address 8-f */
    case COM_WR:        /* 0x01 */              /* Write command */
    case COM_WRSCM:     /* 0x05 */              /* Write w/ input sub channel monitor */
    case COM_WRHFC:     /* 0x0D */              /* Write w/hardware flow control only */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd WRITE %02x\n", chsa, cmd);

        /* see if DSR is set, if not give unit check error */
        if (((ruptr->SNS & SNS_DSRS) == 0) || ((ruptr->SNS & SNS_CONN) == 0)) {
//YY    if ((com_ldsc[unit&7].conn == 0) ||
            ruptr->SNS &= ~SNS_RDY;             /* status is not ready */
            wuptr->SNS &= ~SNS_RDY;             /* status is not ready */
            ruptr->SNS |= SNS_CMDREJ;           /* command reject */
            wuptr->SNS |= SNS_CMDREJ;           /* command reject */
            sim_debug(DEBUG_CMD, dptr,
                "coml_startcmd chsa %04x: Cmd WRITE %02x unit check\n", chsa, cmd);
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        uptr->CMD &= LMASK;                     /* leave only chsa */
        uptr->CMD |= (cmd & COM_MSK);           /* save command */
        uptr->SNS |= SNS_RDY;                   /* status is online & ready */
        sim_activate(uptr, 250);                /* TRY 08-13-18 */
        return 0;                               /* no status change */
        break;

    /* read commands must use address 0-7 */
    /* DSR must be set when a read command is issued, else it is unit check */
    /* bit 1-3 (ASP) of command has more definition */
    /* bit 1 A=1 ASCII control character  detect (7-char mode only) */
    /* bit 2 S=1 Special character detect (7-char mode only) */
    /* bit 3 P=1 Purge input buffer */
    case COM_RD:        /* 0x02 */              /* Read command */
    case 0x22:          /* 0x22 */              /* Read command w/ASCII CC */
    case 0x32:          /* 0x32 */              /* Read command w/ASCII CC & Purge input */
    case COM_RDECHO:    /* 0x06 */              /* Read command w/ECHO */
    case 0x46:          /* 0x46 */              /* Read command w/ECHO & ASCII CC*/
    case 0x56:          /* 0x56 */              /* Read command w/ECHO & ASCII CC & Purge input */
    /* if bit 0 set for COM_RDFC, use DTR for flow, else use RTS for flow control */
    case COM_RDFC:      /* 0x0A */              /* Read command w/flow control */
    case COM_RDHFC:     /* 0x8E */              /* Read command w/hardware flow control only */

        /* see if DSR is set, if not give unit check error */
        if (((ruptr->SNS & SNS_DSRS) == 0) || ((ruptr->SNS & SNS_CONN) == 0)) {
//XX    if (com_ldsc[unit&7].conn == 0) {
            ruptr->SNS &= ~SNS_RDY;             /* status is not ready */
            wuptr->SNS &= ~SNS_RDY;             /* status is not ready */
            ruptr->SNS |= SNS_CMDREJ;           /* command reject */
            wuptr->SNS |= SNS_CMDREJ;           /* command reject */
            /* SNS_DSRS will be 0 */
/*UTX*/     ruptr->SNS |= SNS_DELDSR;           /* set attention status */
/*UTX*/     wuptr->SNS |= SNS_DELDSR;           /* set attention status */
            sim_debug(DEBUG_CMD, dptr,
                "coml_startcmd chsa %04x: Cmd READ %02x unit check\n", chsa, cmd);
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        unit &= 0x7;                            /* make unit 0-7 */
        uptr->CMD &= ~COM_EKO;                  /* clear echo status */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd read\n", chsa);
        uptr->CMD &= LMASK;                     /* leave only chsa */
        uptr->CMD |= (cmd & COM_MSK);           /* save command */
        if ((cmd & 0x0f) == COM_RDECHO)         /* echo command? */
            uptr->CMD |= COM_EKO;               /* save echo status */
        if (cmd & 0x10) {                       /* purge input request? */
            uptr->CNT = 0;                      /* no input count */
            com_data[unit].incnt = 0;           /* no input data */
            com_rbuf[unit&7] = 0;               /* clear read buffer */
        }
        uptr->CMD |= COM_READ;                  /* show read mode */
        uptr->SNS |= SNS_RDY;                   /* status is online & ready */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: input cnt = %04x\n",
            chsa, coml_chp[unit].ccw_count);
        sim_activate(uptr, 250);                /* TRY 08-13-18 */
        return 0;
        break;

    case COM_NOP:       /* 0x03 */              /* NOP has do nothing */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x NOP\n", chsa, cmd);
        uptr->SNS |= SNS_RDY;                   /* status is online & ready */
        uptr->CMD &= LMASK;                     /* leave only chsa */
        uptr->CMD |= (cmd & COM_MSK);           /* save command */
        sim_activate(uptr, 250);                /* start us up */
        break;

    case COM_SNS:       /* 0x04 */              /* Sense (8 bytes) */
        unit &= 0x7;                            /* make unit 0-7 */
        /* status is in SNS (u5) */
        /* ACE is in ACE (u4) */
///*MPX*/ uptr->SNS = 0x03813401;

        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd SENSE chsa %04x: unit %02x Cmd Sense SNS %08x ACE %08x\n",
            chsa, unit, uptr->SNS, uptr->ACE);

        /* byte 0 device status */
        ch = (uptr->SNS >> 24) & 0xff;          /* no bits in byte 0 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 1 line status and error conditions */
        ch = (uptr->SNS >> 16) & 0xff;          /* no bits in byte 1 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 2 modem status */
        // SNS_DELDSR will be set if just connected, clear at end
        ch = (uptr->SNS >> 8) & 0xff;           /* CTS & DSR bits in byte 2 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 3 modem control/operation mode */
        ch = uptr->SNS & 0xff;                  /* maybe DTR bit in byte 3 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 4 ACE byte 0 parameters (parity, stop bits, char len */
        ch = (uptr->ACE >> 24) & 0xff;          /* ACE byte 0 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 5 ACE byte 1 parameters (baud rate) */
        ch = (uptr->ACE >> 16) & 0xff;          /* ACE byte 1 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 6 ACE parameters (Firmware ID 0x62) */
        ch = 0x62;                              /* ACE IOP firmware byte 0 */
//      ch = 0x19;                              /* ACE MFP firmware byte 0 */
        chan_write_byte(chsa, &ch);             /* write status */

        /* byte 7 ACE parameters (Revision Level 0x4?) */
//      Firmware 0x44 supports RTS flow control */
//      Firmware 0x45 supports DCD modem control */
//      ch = 0x44;                              /* ACE firmware byte 1 */
//      ch = 0x45;                              /* ACE firmware byte 1 */
        ch = 0x43;                              /* ACE firmware byte 1 */
//      ch = 0x40;                              /* ACE firmware byte 1 */
        chan_write_byte(chsa, &ch);             /* write status */

        ruptr->SNS &= ~SNS_RING;                /* reset ring attention status */
        ruptr->SNS &= ~SNS_MRING;               /* reset ring attention status */
        ruptr->SNS &= ~SNS_ASCIICD;             /* reset ASCII attention status */
        ruptr->SNS &= ~SNS_DELDSR;              /* reset attention status */
//MPX   ruptr->SNS &= ~SNS_RLSDS;               /* reset rec'd line signal detect */
        ruptr->SNS &= ~SNS_CMDREJ;              /* command reject */
/*MPX*/ ruptr->SNS &= ~SNS_DELTA;               /* reset attention status */

        wuptr->SNS &= ~SNS_RING;                /* reset ring attention status */
        wuptr->SNS &= ~SNS_MRING;               /* reset ring attention status */
        wuptr->SNS &= ~SNS_ASCIICD;             /* reset ASCII attention status */
        wuptr->SNS &= ~SNS_DELDSR;              /* reset attention status */
//1X    wuptr->SNS &= ~SNS_RLSDS;               /* reset rec'd line signal detect */
        wuptr->SNS &= ~SNS_CMDREJ;              /* command reject */
/*MPX*/ wuptr->SNS &= ~SNS_DELTA;               /* reset attention status */

        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd CMD SENSE return chsa %04x: unit %02x Cmd Sense SNS %08x ACE %08x\n",
            chsa, unit, uptr->SNS, uptr->ACE);
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_DEFSC:     /* 0x0B */              /* Define special char */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x DEFSC\n", chsa, cmd);
        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char from memory */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        uptr->ACE &= ~ACE_WAKE;                 /* clear out old wake char */
        uptr->ACE |= ((uint32)ch << 8);         /* insert special char */
        ruptr->ACE = uptr->ACE;                 /* set special char in read unit */
        wuptr->ACE = uptr->ACE;                 /* set special char in write unit */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x DEFSC char %02x SNS %08x ACE %08x\n",
            chsa, cmd, ch, uptr->SNS, uptr->ACE);
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_RRTS:      /* 0x1B */              /* Reset RTS */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x RRTS\n", chsa, cmd);
        uptr->SNS &= ~SNS_RTS;                  /* Request to send not ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_SRTS:      /* 0x1F */              /* Set RTS */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x SRTS\n", chsa, cmd);
        uptr->SNS |= SNS_RTS;                   /* Request to send ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_RBRK:      /* 0x33 */              /* Reset BREAK */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x RBRK\n", chsa, cmd);
        uptr->SNS &= ~SNS_BREAK;                /* Request to send not ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_SBRK:      /* 0x37 */              /* Set BREAK */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x SBRK\n", chsa, cmd);
        uptr->SNS |= SNS_BREAK;                 /* Requestd to send ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_RDTR:      /* 0x13 */              /* Reset DTR (ADVR) */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x RDTR\n", chsa, cmd);
        uptr->SNS &= ~SNS_DTR;                  /* Data terminal not ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_SDTR:      /* 0x17 */              /* Set DTR (ADVF) */
        sim_debug(DEBUG_CMD, dptr, "coml_startcmd chsa %04x: Cmd %02x SDTR\n", chsa, cmd);
        uptr->SNS |= SNS_DTR;                   /* Data terminal ready */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    case COM_SACE:      /* 0xff */              /* Set ACE parameters (3 chars) */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x SACE\n", chsa, cmd);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char 0 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        uptr->ACE = ((uint32)ch)<<24;           /* byte 0 of ACE data */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x ACE byte 0 %02x\n",
            chsa, cmd, ch);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char 1 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        uptr->ACE |= ((uint32)ch)<<16;          /* byte 1 of ACE data */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x ACE byte 1 %02x\n",
            chsa, cmd, ch);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char 2 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        uptr->ACE |= ((uint32)ch)<<8;           /* byte 2 of ACE data */
        uptr->SNS |= SNS_ACEDEF;                /* show ACE defined */
        if (uptr->SNS & SNS_CONN) {
            if (!(uptr->ACE & SNS_MRINGE)) {    /* see if RING enabled */
                uptr->SNS |= (SNS_DTR | SNS_RTS); /* set DTR & DSR if yes */
            }
        }
        ruptr->SNS |= SNS_RDY;                  /* status is online & ready */
        if (uptr == wuptr) {
            ruptr->ACE = uptr->ACE;             /* set ACE in read uptr */
            ruptr->SNS = uptr->SNS;             /* set status to read uptr */
        } else {
            wuptr->ACE = uptr->ACE;             /* set ACE in write uptr */
            wuptr->SNS = uptr->SNS;             /* set status to write uptr */
        }
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x ACE byte 2 %02x\n",
            chsa, cmd, ch);
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd ACE DONE chsa %04x: Cmd %02x ACE bytes %08x\n",
            chsa, cmd, uptr->ACE);

        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;

    /* Set transparent flow control mode */
    case COM_SETFLOW:   /* 0x53 */              /* Set flow control (3 chars) */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x SETFLOW\n", chsa, cmd);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) { /* read char 0 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        fcb[0] = ch;                            /* byte 0 of Flow Cont data */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x SETFLOW byte 0 %02x\n",
            chsa, cmd, ch);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char 1 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        fcb[1] = ch;                            /* byte 1 of Flow Cont data */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x SETFLOW byte 1 %02x\n",
            chsa, cmd, ch);

        if (chan_read_byte(GET_UADDR(uptr->CMD), &ch)) {    /* read char 2 */
            /* nothing to read, error */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* good return */
        }
        fcb[2] = ch;                            /* byte 2 of Flow Cont data */
        ruptr->SNS |= SNS_RDY;                  /* status is online & ready */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd %02x SETFLOW byte 2 %02x\n",
            chsa, cmd, ch);
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd SETFLOW DONE chsa %04x: Cmd %02x FCB bytes %02x%02x%02x\n",
            chsa, cmd, fcb[0], fcb[1], fcb[2]);

        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        return SNS_CHNEND|SNS_DEVEND;           /* good return */
        break;
    default:                                    /* invalid command */
        uptr->SNS |= SNS_CMDREJ;                /* command rejected */
        sim_debug(DEBUG_CMD, dptr,
            "coml_startcmd chsa %04x: Cmd Invalid %02x status %02x\n",
            chsa, cmd, uptr->u5);
        return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* program check */
        break;
    }

    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Unit service - polled input
   Poll for new connections
   Poll all connected lines for input
*/
t_stat comc_srv(UNIT *uptr)
{
    uint8   ch;
    DEVICE *dptr = get_dev(uptr);
    int32   newln, ln, c;
//  int     cmd = uptr->CMD & 0xff;
    uint16  chsa = GET_UADDR(coml_unit[0].CMD); /* get channel/sub-addr */

    /* see if comc attached */
    if ((com_unit[COMC].flags & UNIT_ATT) == 0){    /* attached? */
        return SCPE_OK;
    }
    /* poll for any input from com lines, units 0-7 */
    newln = tmxr_poll_conn(&com_desc);          /* look for connect */
    if (newln >= 0) {                           /* rcv enb pending? */
        uint16  chsa = GET_UADDR(coml_unit[newln].CMD); /* get read channel/sub-addr */
        uint16  wchsa = GET_UADDR(coml_unit[newln+8].CMD); /* get write channel/sub-addr */
        UNIT    *nuptr = coml_unit+newln;       /* get uptr for coml line */
        UNIT    *wuptr = coml_unit+newln+8;     /* get output uptr for coml line */
        com_ldsc[newln].rcve = 1;               /* enable rcv */
        com_ldsc[newln].xmte = 1;               /* enable xmt for output line */
        com_stat[newln] |= COML_RBP;            /* connected */
        com_stat[newln] &= ~COML_REP;           /* not pending */

        sim_debug(DEBUG_CMD, &com_dev,
            "comc_srv conn b4 wakeup on read chsa %04x line %02x SNS %08x ACE %08x\n",
            chsa, newln, nuptr->SNS, nuptr->ACE);
        sim_debug(DEBUG_CMD, &com_dev,
            "comc_srv conn b4 wakeup on write chsa %04x line %02x SNS %08x ACE %08x\n",
            wchsa, newln+8, wuptr->SNS, wuptr->ACE);

        /* send attention to OS here for this channel */
        /* set DSR, CTS and delta DSR status */
        nuptr->SNS |= SNS_CONN;                 /* status is now connected */
        /* UTX says this is an error if set, so do not set SNS_DELDSR */
/*MPX*/ nuptr->SNS |= (SNS_DSRS | SNS_CTSS | SNS_RING);  /* set the read bits */
        nuptr->SNS |= (SNS_RTS | SNS_DTR);      /* set RTS & DTR */
/*MPX*/ nuptr->SNS |= SNS_MRING;                /* set RING interrupt */
        if (nuptr->SNS & SNS_ACEDEF) {          /* ACE defined */
            /* this must be set to login for UTX after system is up */
/*UTX*/     nuptr->SNS |= SNS_DELDSR;           /* set delta dsr status */
            nuptr->SNS |= SNS_RLSDS;            /* set rec'd line signal detect */
        } else {
            nuptr->SNS |= SNS_DELDSR;           /* set delta dsr status */
            nuptr->SNS |= SNS_RLSDS;            /* set rec'd line signal detect */
        }
        nuptr->SNS &= ~SNS_CMDREJ;              /* no command reject */
        wuptr->SNS = nuptr->SNS;                /* set write line too */
        wuptr->ACE = nuptr->ACE;                /* set write line too */
        sim_debug(DEBUG_CMD, &com_dev,
            "comc_srv conn wakeup on chsa %04x line %02x SNS %08x ACE %08x\n",
            chsa, newln, nuptr->SNS, nuptr->ACE);
        set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
    }
    /* poll all devices for input */
    tmxr_poll_rx(&com_desc);                    /* poll for input */
    for (ln = 0; ln < COM_NUMLIN; ln++) {       /* loop thru lines */
        UNIT    *nuptr = coml_unit+ln;          /* get uptr for coml line */
        int     cmd = nuptr->CMD & 0xff;        /* get the active cmd */
        uint16  chsa = GET_UADDR(nuptr->CMD);   /* get channel/sub-addr */

        if (com_ldsc[ln].conn)                  /* connected? */
            sim_debug(DEBUG_CMD, &com_dev,
                "comc_srv conn poll input chsa %04x line %02x SNS %08x ACE %08x\n",
                chsa, ln, nuptr->SNS, nuptr->ACE);

        if ((com_ldsc[ln].conn) &&              /* connected? */
            (c = tmxr_getc_ln(&com_ldsc[ln]))) {    /* get char */
            ch = c & 0x7f;
            if (ch == '\n')                     /* convert newline */
                ch = '\r';                      /* to C/R */
            sim_debug(DEBUG_CMD, &com_dev,
                "comc_srv read %02x (%02x) chsa %04x line %02x SNS %08x ACE %08x CMD %08x\n",
                c, ch, chsa, ln, nuptr->SNS, nuptr->ACE, nuptr->CMD);
            /* tmxr says break is 0x80??, but SCPE_BREAK is 0x800000?? */
            if (c & SCPE_BREAK) {               /* break? */
                nuptr->SNS |= SNS_BREAK;        /* set received break bit */
                com_stat[ln] |= COML_RBP;       /* set rcv brk */
                set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);
                continue;
            }
            /* normal char */
            nuptr->SNS &= ~SNS_BREAK;           /* reset received break bit */
            com_stat[ln] &= ~COML_RBP;          /* clr rcv brk */

            /* convert to user requested input */
            ch = sim_tt_inpcvt(ch, TT_GET_MODE(coml_unit[ln].flags));
            com_rbuf[ln] = ch;                  /* save char */

            /* Special char detect? */
            if ((ch & 0x7f) == ((nuptr->ACE >> 8) & 0xff)) { /* is it spec char */
                nuptr->CMD |= COM_SCD;          /* set special char detected */
                nuptr->SNS |= SNS_SPCLCD;       /* set special char detected */
//              nuptr->SNS |= SNS_RLSDS;        /* set rec'd line signal detect */
                nuptr->SNS |= SNS_RING;         /* set ring attention status */
                sim_debug(DEBUG_CMD, &com_dev,
                    "comc_srv user ACE wakeup on chsa %04x line %02x cmd %02x SNS %08x ACE %08x\n",
                    chsa, ln, cmd, nuptr->SNS, nuptr->ACE);
                set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);
                continue;
            }

            /* put char in buffer */
            com_data[ln].ibuff[com_data[ln].incnt++] = ch;

            /* see if at max, if so reset to start */
            if (com_data[ln].incnt >= sizeof(com_data[ln].ibuff))
                com_data[ln].incnt = 0;         /* reset buffer cnt */

            nuptr->CMD |= COM_INPUT;            /* we have a char available */
            sim_debug(DEBUG_CMD, dptr,
                "comc_srv readch ln %02x: CMD %08x read %02x CNT %02x incnt %02x c %04x\n",
                ln, nuptr->CMD, ch, nuptr->CNT, com_data[ln].incnt, c);
        }
        else                                    /* end if conn */
        /* if we were connected and not now, reset serial line */
        if ((nuptr->SNS & SNS_CONN) && (com_ldsc[ln].conn == 0)) {
            UNIT    *wuptr = coml_unit+ln+8;    /* get output uptr for coml line */
            sim_debug(DEBUG_CMD, &com_dev,
                "comc_srv disconnect on chsa %04x line %02x cmd %02x SNS %08x ACE %08x\n",
                chsa, ln, cmd, nuptr->SNS, nuptr->ACE);
            com_ldsc[ln].rcve = 0;              /* disable rcv */
            com_ldsc[ln].xmte = 0;              /* disable xmt for output line */
            com_stat[ln] &= ~COML_RBP;          /* disconnected */
            com_stat[ln] |= COML_REP;           /* set pending */
            nuptr->SNS &= ~(SNS_RTS | SNS_DTR); /* reset RTS & DTR */
            nuptr->SNS &= ~(SNS_DSRS);          /* status is not connected */
            nuptr->SNS |= (SNS_DELDSR);         /* status is not connected */
            nuptr->SNS |= (SNS_DELTA);          /* status is not connected */
            nuptr->SNS &= ~(SNS_RDY|SNS_CONN);  /* status is not connected */
            wuptr->SNS = nuptr->SNS;            /* set write channel too */
            set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);
        }
    }                                           /* end for */

    sim_debug(DEBUG_DETAIL, &com_dev,
        "comc_srv POLL DONE on chsa %04x\n", chsa);
    /* this says to use 200, but simh really uses 50000 for cnt */
    /* changed 12/02/2021 from 200 to 5000 */
//  return sim_clock_coschedule(uptr, 200);     /* continue poll */
    return sim_clock_coschedule(uptr, 5000);    /* continue poll */
//  return sim_activate(uptr, 10000);           /* continue poll */
//  return sim_activate(uptr, 5000);            /* continue poll */
}

/* Unit service - input transfers */
t_stat comi_srv(UNIT *uptr)
{
    DEVICE *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);        /* get channel/sub-addr */
    uint32  ln = (uptr - coml_unit) & 0x7;      /* use line # 0-7 for 8-15 */
    CHANP   *chp = find_chanp_ptr(chsa);        /* find the chanp pointer */
    int     cmd = uptr->CMD & 0xff;             /* get active cmd */
    uint8   ch, cc;

    /* handle NOP and INCH cmds */
    sim_debug(DEBUG_CMD, dptr,
        "comi_srv entry chsa %04x line %04x cmd %02x conn %x rcve %x xmte %x SNS %08x\n",
        chsa, ln, cmd, com_ldsc[ln].conn, com_ldsc[ln].rcve, com_ldsc[ln].xmte, uptr->SNS);

    if (com_ldsc[ln].conn) {                    /* connected? */
        if ((uptr->CNT != com_data[ln].incnt) ||    /* input empty */
            (uptr->CMD & COM_INPUT)) {          /* input waiting? */
            ch = com_data[ln].ibuff[uptr->CNT]; /* get char from read buffer */
            sim_debug(DEBUG_CMD, dptr,
                "com_srvi readbuf unit %02x: CMD %08x read %02x incnt %02x CNT %02x len %02x\n",
                ln, uptr->CMD, ch, com_data[ln].incnt, uptr->CNT, chp->ccw_count);

            if (uptr->CNT != com_data[ln].incnt) { /* input available */
                /* process any characters */
                /* this fixes mpx1x time entry on startup */
                if (uptr->CMD & COM_EKO) {      /* ECHO requested */
                    /* echo the char out */
                    /* convert to user requested output */
                    sim_debug(DEBUG_CMD, &com_dev,
                        "comi_srv echo char %02x on chsa %04x line %02x cmd %02x ACE %08x\n",
                        ch, chsa, ln, cmd, uptr->ACE);
//                  cc = sim_tt_outcvt(c, TT_GET_MODE(coml_unit[ln].flags));
                    tmxr_putc_ln(&com_ldsc[ln], ch); /* output char */
                    tmxr_poll_tx(&com_desc);    /* poll xmt to send */
                }
                if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                    /* write error */
                    cmd = 0;                    /* no cmd now */
                    sim_debug(DEBUG_CMD, dptr,
                        "comi_srv write error ln %02x: CMD %08x read %02x CNT %02x ccw_count %02x\n",
                        ln, uptr->CMD, ch, uptr->CNT, chp->ccw_count);
                    uptr->CMD &= ~COM_MSK;      /* remove old CMD */
                    uptr->CMD &= ~COM_INPUT;    /* input waiting? */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                    return SCPE_OK;
                }
                /* character accepted, bump buffer pointer */
                uptr->CNT++;                    /* next char position */

                sim_debug(DEBUG_CMD, dptr,
                    "comi_srv write to mem line %02x: CMD %08x read %02x CNT %02x incnt %02x\n",
                     ln, uptr->CMD, ch, uptr->CNT, com_data[ln].incnt);

                /* see if at end of buffer */
                if (uptr->CNT >= (int32)sizeof(com_data[ln].ibuff))
                    uptr->CNT = 0;              /* reset pointer */

                cc = ch & 0x7f;                 /* clear parity bit */
                /* Special char detected? (7 bit read only) */
                if (cc == ((uptr->ACE >> 8) & 0xff)) { /* is it spec char */
//                  uptr->CMD |= COM_SCD;       /* set special char detected */
                    uptr->SNS |= SNS_SPCLCD;    /* set special char detected */
                    sim_debug(DEBUG_CMD, &com_dev,
                        "comi_srv user ACE %02x wakeup on chsa %04x line %02x cmd %02x ACE %08x\n",
                        cc, chsa, ln, cmd, uptr->ACE);
                    uptr->CMD &= LMASK;         /* nothing left, command complete */
                    sim_debug(DEBUG_CMD, dptr,
                        "comi_srv read done chsa %04x ln %04x: chnend|devend\n", chsa, ln);
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
                    return SCPE_OK;
                }

                /* ASCII control char (7 bit read only) */
                /* see if control char detected */
                if (uptr->CMD & 0x40) {         /* is ASCII ctrl char test bit set */
                    if (((cc & 0x60) == 0) || (cc == 0x7f)) {
                        uptr->SNS |= SNS_ASCIICD;   /* ASCII ctrl char detected */
                        sim_debug(DEBUG_CMD, &com_dev,
                    "comi_srv user ASCII %02x wakeup on chsa %04x line %02x cmd %02x ACE %08x\n",
                            cc, chsa, ln, cmd, uptr->ACE);
                        uptr->CMD &= LMASK;     /* nothing left, command complete */
                        sim_debug(DEBUG_CMD, dptr,
                            "comi_srv read CC done chsa %04x ln %04x: chnend|devend\n", chsa, ln);
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
                        return SCPE_OK;
                    }
                }

                /* user want more data? */
                if ((test_write_byte_end(chsa)) == 0) {
                    sim_debug(DEBUG_CMD, dptr,
                        "comi_srv need more line %02x CMD %08x CNT %02x ccw_count %02x incnt %02x\n",
                        ln, uptr->CMD, uptr->CNT, chp->ccw_count, com_data[ln].incnt);
                    /* user wants more, look next time */
                    if (uptr->CNT == com_data[ln].incnt) {  /* input empty */
                        uptr->CMD &= ~COM_INPUT;    /* no input available */
                    }
/* change 02DEC21*/ sim_activate(uptr, uptr->wait); /* wait */
// change 02DEC21*/ sim_clock_coschedule(uptr, 1000);   /* continue poll */
                    return SCPE_OK;
                }
                /* command is completed */
                sim_debug(DEBUG_CMD, dptr,
                    "comi_srv read done line %02x CMD %08x read %02x CNT %02x ccw_count %02x incnt %02x\n",
                    ln, uptr->CMD, ch, uptr->CNT, chp->ccw_count, com_data[ln].incnt);
                uptr->CMD &= LMASK;             /* nothing left, command complete */
                if (uptr->CNT != com_data[ln].incnt) {  /* input empty */
                    uptr->CMD |= COM_INPUT;     /* input still available */
                }
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
            }
        }
// change 02DEC21   sim_activate(uptr, uptr->wait); /* wait */
/* change 02DEC21*/ sim_clock_coschedule(uptr, 1000);   /* continue poll */
        return SCPE_OK;
    }
    /* not connected, so dump chars on ground */
    uptr->CNT = 0;                              /* no input count */
    com_data[ln].incnt = 0;                     /* no input data */
    uptr->CMD &= LMASK;                         /* nothing left, command complete */
    uptr->SNS |= 0x00003003;                    /* status is online & ready */
    uptr->SNS &= SNS_DSRS;                      /* reset DSR */
    uptr->SNS |= SNS_DELDSR;                    /* give change status */
    uptr->SNS |= SNS_MRING;                     /* give RING status */
    sim_debug(DEBUG_CMD, dptr,
        "comi_srv read dump DONE line %04x status %04x cmd %02x SNS %08x\n",
        ln, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK, cmd, uptr->SNS);
    /* if line active, abort cmd */
    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);   /* error return */
    return SCPE_OK;
}

/* Unit service - output transfers */
t_stat como_srv(UNIT *uptr)
{
    DEVICE *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);        /* get channel/sub-addr */
    uint32  ln = (uptr - coml_unit) & 0x7;      /* use line # 0-7 for 8-15 */
    UNIT    *ruptr = &dptr->units[ln&7];        /* read uptr */
    uint32  done;
    int     cmd = uptr->CMD & 0xff;             /* get active cmd */
    uint8   ch;

    sim_debug(DEBUG_CMD, dptr,
        "como_srv entry chsa %04x line %04x cmd %02x conn %x rcve %x xmte %x\n",
        chsa, ln, cmd, com_ldsc[ln].conn, com_ldsc[ln].rcve, com_ldsc[ln].xmte);

    if (com_dev.flags & DEV_DIS) {              /* disabled */
        sim_debug(DEBUG_CMD, dptr,
            "como_srv chsa %04x line %02x SNS %08x DEV_DIS set\n", chsa, ln, uptr->SNS);
            sim_debug(DEBUG_CMD, dptr,
                "como_srv Write forced DONE %04x status %04x\n",
                ln, SNS_CHNEND|SNS_DEVEND);
            uptr->CMD &= LMASK;                 /* nothing left, command complete */
            ruptr->SNS &= SNS_DSRS;             /* reset DSR */
            ruptr->SNS |= SNS_DELDSR;           /* give change status */
//          chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);   /* error return */
            return SCPE_OK;                     /* return */
    }

    /* handle NOP and INCH cmds */
    if (cmd == COM_NOP || cmd == 0x7f) {        /* check for NOP or INCH */
        uptr->CMD &= LMASK;                     /* leave only chsa */
        sim_debug(DEBUG_CMD, &com_dev,
            "como_srv NOP or INCH done chsa %04x line %04x cmd %02x\n",
            chsa, ln, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;                         /* return */
    }

    /* handle SACE, 3 char already read, so we are done */
    if (cmd == COM_SACE) {                      /* check for SACE 0xff */
        uptr->CMD &= LMASK;                     /* leave only chsa */
        sim_debug(DEBUG_CMD, &com_dev,
            "como_srv SACE done chsa %04x line %02x cmd %02x ACE %08x\n",
            chsa, ln, cmd, uptr->ACE);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;                         /* return */
    }

    if (cmd == 0)
        /* all done, so stop polling */
        return SCPE_OK;

    if (com_ldsc[ln].conn == 0) {               /* connected? */
        /* not connected, so dump char on ground */
        sim_debug(DEBUG_CMD, dptr,
            "como_srv write dump DONE line %04x status %04x cmd %02x\n",
            ln, SNS_CHNEND|SNS_DEVEND, cmd);
        uptr->CMD &= LMASK;                     /* nothing left, command complete */

        uptr->SNS |= 0x00003003;                /* status is online & ready */
        ruptr->SNS &= SNS_DSRS;                 /* reset DSR */
        ruptr->SNS |= SNS_DELDSR;               /* give change status */
        uptr->SNS |= SNS_MRING;                 /* give RING status */
        /* if line not active, abort cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);   /* error return */
        return SCPE_OK;
    }

    sim_debug(DEBUG_CMD, dptr,
        "como_srv entry 1 chsa %04x line %04x cmd %02x\n", chsa, ln, cmd);
    /* get a user byte from memory */
doagain:
    done = chan_read_byte(chsa, &ch);           /* get byte from memory */
    if (done) {
        uptr->CMD &= LMASK;                     /* leave only chsa */
        sim_debug(DEBUG_CMD, dptr,
            "como_srv Write DONE %01x chsa %04x line %04x status %04x\n",
            done, chsa, ln, SNS_CHNEND|SNS_DEVEND);
        tmxr_poll_tx(&com_desc);                /* send out data */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;                         /* return */
    }

    /* not done */
    sim_debug(DEBUG_DETAIL, dptr,
        "como_srv poll chsa %04x line %02x SNS %08x ACE %08x\n",
        chsa, ln, uptr->SNS, uptr->ACE);

    /* convert to user requested output */
    ch = sim_tt_outcvt(ch, TT_GET_MODE(coml_unit[ln].flags));
    /* send the next char out */
    tmxr_putc_ln(&com_ldsc[ln], ch);            /* output char */
    sim_debug(DEBUG_CMD, dptr,
        "como_srv writing char 0x%02x to ln %04x\n", ch, ln);
    goto doagain;                               /* keep going */
}

/* haltxio routine */
t_stat coml_haltio(UNIT *uptr) {
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & COM_MSK;
    int     unit = (uptr - coml_unit);          /* unit # 0 is read, 1 is write */
    CHANP   *chp = find_chanp_ptr(chsa);        /* find the chanp pointer */

    sim_debug(DEBUG_EXP, &com_dev, "coml_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & COM_MSK) != 0) {           /* is unit busy */
        sim_debug(DEBUG_CMD, &coml_dev,
            "coml_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        /* stop any I/O and post status and return error status */
        chp->ccw_count = 0;                     /* zero the count */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* reset chaining bits */
        uptr->CMD &= LMASK;                     /* make non-busy */
        uptr->CNT = 0;                          /* no I/O yet */
        com_data[unit].incnt = 0;               /* no input data */
        sim_cancel(uptr);                       /* stop timer */
//      uptr->SNS |= (SNS_RDY|SNS_CONN);        /* status is online & ready */
        sim_debug(DEBUG_CMD, &coml_dev,
            "coml_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* force end */
        return 1;                               /* tell chan code to post status */
    }
    uptr->CNT = 0;                              /* no I/O yet */
    com_data[unit].incnt = 0;                   /* no input data */
    uptr->CMD &= LMASK;                         /* make non-busy */
//  uptr->SNS |= (SNS_RDY|SNS_CONN);            /* status is online & ready */
    return SCPE_OK;                             /* not busy */
}

/* Reset routine */
t_stat com_reset (DEVICE *dptr)
{
    int32 i;

    if (com_dev.flags & DEV_DIS)                /* master disabled? */
        com_dev.flags |= DEV_DIS;               /* disable lines */
    else
        com_dev.flags &= ~DEV_DIS;
    if (com_unit[COMC].flags & UNIT_ATT)        /* master att? */
        sim_clock_coschedule(&com_unit[0], 200);    /* activate */
    for (i = 0; i < COM_LINES; i++)             /* reset lines */
        com_reset_ln(i);
    return SCPE_OK;
}


/* attach master unit */
t_stat com_attach(UNIT *uptr, CONST char *cptr)
{
    DEVICE  *dptr = get_dev(uptr);
    t_stat  r;

    r = tmxr_attach(&com_desc, uptr, cptr);     /* attach */
    if (r != SCPE_OK)                           /* error? */
        return r;                               /* return error */
    sim_debug(DEBUG_CMD, dptr, "com_srv comc is now attached\n");
    sim_activate(uptr, 100);                    /* start poll at once */
    return SCPE_OK;
}

/* detach master unit */
t_stat com_detach(UNIT *uptr)
{
    int32 i;
    t_stat r;

    r = tmxr_detach(&com_desc, uptr);           /* detach */
    for (i = 0; i < COM_LINES; i++)             /* disable rcv */
        com_reset_ln(i);                        /* reset the line */
    sim_cancel(uptr);                           /* stop poll, cancel timer */
    return r;
}

/* Reset an individual line */
void com_reset_ln (int32 ln)
{
    sim_cancel(&coml_unit[ln]);
    com_stat[ln] = 0;
    com_stat[ln] |= COML_REP;                   /* set pending */
    com_rbuf[ln] = 0;                           /* clear read buffer */
    com_xbuf[ln] = 0;                           /* clear write buffer */
    com_ldsc[ln].rcve = 0;
    com_ldsc[ln].xmte = 0;
    coml_unit[ln].CNT = 0;                      /* no input count */
    com_data[ln].incnt = 0;                     /* no input data */
    return;
}

t_stat com_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "SEL32 8512 8-Line Async Controller Terminal Interfaces\n\n");
fprintf (st, "Terminals perform input and output through Telnet sessions connected to a \n");
fprintf (st, "user-specified port.\n\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprintf (st, "   sim> SET COMLn DATASET        simulate attachment to a dataset (modem)\n");
fprintf (st, "   sim> SET COMLn NODATASET      simulate direct attachment to a terminal\n\n");
fprintf (st, "Finally, each line supports output logging.  The SET COMLn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET COMLn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET COMLn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DCI is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DCI DISCONNECT command, or a DETACH DCI command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW COMC CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW COMC STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET COMLn DISCONNECT     disconnects the specified line.\n");
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DCI is detached.\n");
    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    return SCPE_OK;
}

/* description of controller */
const char *com_description (DEVICE *dptr)
{
    return "SEL-32 8512 8-Line async communications controller";
}

#endif
