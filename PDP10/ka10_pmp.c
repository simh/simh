/* PMP disk controller interface for WAITS.

   Copyright (c) 2017, Richard Cornwell

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

   Structure of a disk. See Hercules CKD disks.

    Numbers are stored least to most significant.

     Devid = "CKD_P370"

       uint8    devid[8]        device header.
       uint32   heads           number of heads per cylinder
       uint32   tracksize       size of track
       uint8    devtype         Hex code of last two digits of device type.
       uint8    fileseq         always 0.
       uint16   highcyl         highest cylinder.

       uint8    resv[492]       pad to 512 byte block

   Each Track has:
       uint8    bin             Track header.
       uint16   cyl             Cylinder number
       uint16   head            Head number.

   Each Record has:
       uint16   cyl             Cylinder number  <- tpos
       uint16   head            Head number
       uint8    rec             Record id.
       uint8    klen            Length of key
       uint16   dlen            Length of data

       uint8    key[klen]       Key data.
       uint8    data[dlen]      Data len.

   cpos points to where data is actually read/written from

   Pad to being track to multiple of 512 bytes.

   Last record has cyl and head = 0xffffffff

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_PMP
#define NUM_DEVS_PMP 0
#endif

#if (NUM_DEVS_PMP > 0)
#define UNIT_V_TYPE        (UNIT_V_UF + 0)
#define UNIT_TYPE          (0xf << UNIT_V_TYPE)

#define GET_TYPE(x)        ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)         (UNIT_TYPE & ((x) << UNIT_V_TYPE))
#define UNIT_DASD          UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | \
                             UNIT_FIX | SET_TYPE(6)

#define UNIT_V_ADDR       (UNIT_V_TYPE + 4)
#define UNIT_ADDR_MASK    (0xff << UNIT_V_ADDR)
#define GET_UADDR(x)      ((UNIT_ADDR_MASK & x) >> UNIT_V_ADDR)
#define UNIT_ADDR(x)      ((x) << UNIT_V_ADDR)

#define NUM_UNITS_PMP     8

#define PMP_DEV 0500


#define CMD     u3

/* u3 */
#define DK_NOP             0x03       /* Nop operation */
#define DK_RELEASE         0x17       /* Release from channel */
#define DK_RESTORE         0x13       /* Restore */
#define DK_SEEK            0x07       /* Seek */
#define DK_SEEKCYL         0x0B       /* Seek Cylinder */
#define DK_SEEKHD          0x1B       /* Seek Head */
#define DK_SETMSK          0x1f       /* Set file mask */
#define DK_SPACE           0x0f       /* Space record */
#define DK_SRCH_HAEQ       0x39       /* Search HA equal */
#define DK_SRCH_IDEQ       0x31       /* Search ID equal */
#define DK_SRCH_IDGT       0x51       /* Search ID greater */
#define DK_SRCH_IDGE       0x71       /* Search ID greater or equal */
#define DK_SRCH_KYEQ       0x29       /* Search Key equal */
#define DK_SRCH_KYGT       0x49       /* Search Key greater */
#define DK_SRCH_KYGE       0x69       /* Search Key greater or equal */
#define DK_RD_IPL          0x02       /* Read IPL record */
#define DK_RD_HA           0x1A       /* Read home address */
#define DK_RD_CNT          0x12       /* Read count */
#define DK_RD_R0           0x16       /* Read R0 */
#define DK_RD_D            0x06       /* Read Data */
#define DK_RD_KD           0x0e       /* Read key and data */
#define DK_RD_CKD          0x1e       /* Read count, key and data */
#define DK_WR_HA           0x19       /* Write home address */
#define DK_WR_R0           0x15       /* Write R0 */
#define DK_WR_D            0x05       /* Write Data */
#define DK_WR_KD           0x0d       /* Write key and data */
#define DK_WR_CKD          0x1d       /* Write count, key and data */
#define DK_WR_SCKD         0x01       /* Write special count, key and data */
#define DK_ERASE           0x11       /* Erase to end of track */
#define DK_RD_SECT         0x22       /* Read sector counter */
#define DK_SETSECT         0x23       /* Set sector */
#define DK_MT              0x80       /* Multi track flag */

#define DK_INDEX           0x00100    /* Index seen in command */
#define DK_NOEQ            0x00200    /* Not equal compare */
#define DK_HIGH            0x00400    /* High compare */
#define DK_PARAM           0x00800    /* Parameter in u4 */
#define DK_MSET            0x01000    /* Mode set command already */
#define DK_SHORTSRC        0x02000    /* Last search was short */
#define DK_SRCOK           0x04000    /* Last search good */
#define DK_CYL_DIRTY       0x08000    /* Current cylinder dirty */
#define DK_DONE            0x10000    /* Write command done, zero fill */
#define DK_INDEX2          0x20000    /* Second index seen */
#define DK_ATTN            0x40000    /* Device has attention set */

#define DK_MSK_INHWR0      0x00       /* Inhbit writing of HA/R0 */
#define DK_MSK_INHWRT      0x40       /* Inhbit all writes */
#define DK_MSK_ALLWRU      0x80       /* Allow all updates */
#define DK_MSK_ALLWRT      0xc0       /* Allow all writes */
#define DK_MSK_WRT         0xc0       /* Write mask */

#define DK_MSK_SKALLSKR    0x00       /* Allow all seek/recal */
#define DK_MSK_SKALLCLY    0x08       /* Allow cyl/head only */
#define DK_MSK_SKALLHD     0x10       /* Allow head only */
#define DK_MSK_SKNONE      0x18       /* Allow no seeks */
#define DK_MSK_SK          0x18       /* Seek mask */

#define POS     u4 
/* u4 */
/* Holds the current track and head */
#define DK_V_TRACK         8
#define DK_M_TRACK         0x3ff00    /* Max 1024 cylinders */
#define DK_V_HEAD          0
#define DK_M_HEAD          0xff       /* Max 256 heads */

#define SENSE   u5
/* u5 */
/* Sense byte 0 */
#define SNS_CMDREJ         0x80       /* Command reject */
#define SNS_INTVENT        0x40       /* Unit intervention required */
#define SNS_BUSCHK         0x20       /* Parity error on bus */
#define SNS_EQUCHK         0x10       /* Equipment check */
#define SNS_DATCHK         0x08       /* Data Check */
#define SNS_OVRRUN         0x04       /* Data overrun */
#define SNS_TRKCND         0x02       /* Track Condition */
#define SNS_SEEKCK         0x01       /* Seek Check */

/* Sense byte 1 */
#define SNS_DCCNT          0x80       /* Data Check Count */
#define SNS_TRKOVR         0x40       /* Track Overrun */
#define SNS_ENDCYL         0x20       /* End of Cylinder */
#define SNS_INVSEQ         0x10       /* Invalid Sequence */
#define SNS_NOREC          0x08       /* No record found */
#define SNS_WRP            0x04       /* Write Protect */
#define SNS_ADDR           0x02       /* Missing Address Mark */
#define SNS_OVRINC         0x01       /* Overflow Incomplete */

/* Sense byte 2 */
#define SNS_BYTE2          0x00       /* Diags Use */
/* Sense byte 3 */
#define SNS_BYTE3          0x00       /* Diags Use */

/* saved in state field of data */
/* Record position, high 4 bits, low internal short count */
#define DK_POS_INDEX       0x0        /* At Index Mark */
#define DK_POS_HA          0x1        /* In home address (c) */
#define DK_POS_CNT         0x2        /* In count (c) */
#define DK_POS_KEY         0x3        /* In Key area */
#define DK_POS_DATA        0x4        /* In Data area */
#define DK_POS_AM          0x5        /* Address mark before record */
#define DK_POS_END         0x8        /* Past end of data */
#define DK_POS_SEEK        0xF        /* In seek */

#define LASTCMD       u6
/* u6 holds last command */
/* Held in ccyl entry */

#define DATAPTR       up7
/* Pointer held in up7 */
struct pmp_t
{
     uint8             *cbuf;    /* Cylinder buffer */
     uint32             cpos;    /* Position of head of cylinder in file */
     uint32             tstart;  /* Location of start of track */
     uint16             ccyl;    /* Current Cylinder number */
     uint16             cyl;     /* Cylinder head at */
     uint16             tpos;    /* Track position */
     uint16             rpos;    /* Start of current record */
     uint16             dlen;    /* remaining in data */
     uint32             tsize;   /* Size of one track include rounding */
     uint8              state;   /* Current state */
     uint8              klen;    /* remaining in key */
     uint8              filemsk; /* Current file mask */
     uint8              rec;     /* Current record number */
     uint16             count;   /* Remaining in current operation */
};


/* PDP10 CONO/CONI and DATA bits */

/* CONI 500 bits */
#define NXM_ERR    00200000000000LL /* Non-existent memory */
#define CHA_ERR    00100000000000LL /* Data chaining error */
#define SEL_ERR    00040000000000LL /* Selection error */
#define LST_ADDR   00037700000000LL /* Last address used */
#define PAR1_ERR   00000040000000LL /* Parity error control */
#define PAR2_ERR   00000020000000LL /* Parity error memory */
#define IDLE       00000010100000LL /* Channel idle */
#define INT_SEL    00000004000000LL /* Initial selection state */
#define REQ_SEL    00000002000000LL /* Device requestion select */
#define TRANS      00000001000000LL /* Transfer in progress */
#define PAR_ERR    00000000400000LL /* Parity error */
#define HOLD_EMPTY 00000000200000LL /* Command hold empty */
#define UNU_END    00000000040000LL /* Unusual end */
#define NEW_STS    00000000020000LL /* New status */
#define ATTN       00000000010000LL /* Attention */
#define ST_MOD     00000000004000LL /* Status modifier */
#define CTL_END    00000000002000LL /* Control unit end */
#define BSY        00000000001000LL /* Device is busy */
#define CHN_END    00000000000400LL /* Channel end */
#define DEV_END    00000000000200LL /* Device end */
#define UNIT_CHK   00000000000100LL /* Unit check */
#define UNIT_EXP   00000000000040LL /* Unit exception */
#define PI_ACT     00000000000020LL /* PI channel active */
#define PIA        00000000000007LL /* PI channel */
#define STS_MASK   00000000017740LL /* Status bits to clear */

/* CONO 500 bits */
#define IRQ_ERROR  00000000400000LL /* Disk or Core parity error */
#define IRQ_EMPTY  00000000200000LL /* Command hold empty */
#define IRQ_IDLE   00000000100000LL /* Channel is idle */
#define IRQ_UEND   00000000040000LL /* Unusual end */
#define IRQ_NSTS   00000000020000LL /* New Status */
#define IRQ_STS    00000000017740LL /* Status bits set */

/* DATAO 500 is -Word Count, Address */
/* DATAI 500 is Current Address */

/* CONI 504 */
#define OP1       000000010000LL  /* Channel in operation */
#define DAT_CHAIN 000000004000LL  /* Data Chaining enabled */
#define WCMA_LD   000000002000LL  /* WCMA hold register full */
#define CMD_LD    000000001000LL  /* Command hold loaded */
#define IDLE_CH   000000000400LL  /* Channel is idle */
#define REQ_CH    000000000200LL  /* Request for channel */
#define IS_CH     000000000100LL  /* Initial select state */
#define TRANS_CH  000000000040LL  /* Tranfer in progress */
#define CMD_EMP   000000000020LL  /* Command hold empty */
#define CMD_FUL   000000000010LL  /* Command hold full */
#define OPL       000000000004LL  /* Operation out */

/* CONO 504 */
#define CLR_UEND   00000004000LL  /* Clear Unusual end */
#define CLR_MUX    00000002000LL  /* Clear memory multiplexer */
#define CLR_DATCH  00000001000LL  /* Clear data chaining flag */
#define CLR_IRQ    00000000400LL  /* Clear IRQs */
#define NSTS_CLR   00000000200LL  /* Clear New status flag */
#define PWR_CLR    00000000100LL  /* Power on clear */
#define STS_CLR    00000000040LL  /* Clear device status */
#define CMD_CLR    00000000020LL  /* Clear command hold */
#define CMD_HOLD   00000000010LL  /* Set command hold loaded. */
#define DEV_RESET  00000000004LL  /* Reset current device */
#define OPL_RESET  00000000002LL  /* Reset all devices */
#define CHN_RESET  00000000001LL  /* Reset the channel */

/* DATAO 504 */
#define CMD_MASK    0000000000377LL   /* Command */
#define SKP_MOD_OFF 0000000000400LL   /* Skip mod off */
#define SKP_MOD_ON  0000000001000LL   /* Skip mod on */
#define CMDCH_ON    0000000002000LL   /* Command chaining */
#define CNT_BYT     0000000004000LL   /* Count in bytes */
#define BYTE_MODE   0000000010000LL   /* Transfer bytes not words */
#define SET_HOLD    0000000020000LL   /* Set command hold */
#define DEV_ADDR    0000017740000LL   /* Device address */
#define DATCH_ON    0000020000000LL   /* Data chaining on */
#define HOLD_MASK   0000037777777LL   /* Bits in command */

/* Channel sense bytes */
#define     SNS_ATTN      0x80                /* Unit attention */
#define     SNS_SMS       0x40                /* Status modifier */
#define     SNS_CTLEND    0x20                /* Control unit end */
#define     SNS_BSY       0x10                /* Unit Busy */
#define     SNS_CHNEND    0x08                /* Channel end */
#define     SNS_DEVEND    0x04                /* Device end */
#define     SNS_UNITCHK   0x02                /* Unit check */
#define     SNS_UNITEXP   0x01                /* Unit exception */

/* Channel pmp_cnt values. */
#define BUFF_EMPTY       0x10               /* Buffer is empty */
#define BUFF_DIRTY       0x20               /* Buffer is dirty flag */
#define BUFF_CHNEND      0x40               /* Channel end */

struct disk_t
{
    const char         *name;         /* Type Name */
    int                 cyl;          /* Number of cylinders */
    int                 heads;        /* Number of heads/cylinder */
    int                 bpt;          /* Max bytes per track */
    uint8               sen_cnt;      /* Number of sense bytes */
    uint8               dev_type;     /* Device type code */
}
disk_type[] =
{
       {"2301",   1, 200, 20483,  6,  0x01},   /*   4.1  M */
       {"2302", 250,  46,  4984,  6,  0x02},   /*  57.32 M 50ms, 120ms/10, 180ms> 10 */
       {"2303",  80,  10,  4984,  6,  0x03},   /*   4.00 M */
       {"2305",  48,   8, 14568,  6,  0x05},   /*   5.43 M */
       {"2305-2",96,   8, 14858,  6,  0x05},   /*  11.26 M */
       {"2311",  202, 10,  3717,  6,  0x11},   /*   7.32 M  156k/s 30 ms 145 full */
       {"2314",  203, 20,  7294,  6,  0x14},   /*  29.17 M */
       {"3330",  411, 19, 13165, 24,  0x30},   /* 100.00 M */
       {"3330-2",815, 19, 13165, 24,  0x30},
       {0},
};


/* Header block */
struct pmp_header
{
       uint8    devid[8];      /* device header. */
       int      heads;         /* number of heads per cylinder */
       uint32   tracksize;     /* size of track */
       uint8    devtype;       /* Hex code of last two digits of device type. */
       uint8    fileseq;       /* always 0. */
       uint16   highcyl;       /* highest cylinder. */
       uint8    resv[492];     /* pad to 512 byte block */
};

int                 pmp_pia;         /* PIA for PMP device */
uint64              pmp_status;      /* CONI status for device 500 */
int                 pmp_statusb;
uint32              pmp_cmd_hold;    /* Command hold register */
uint32              pmp_wc_hold;     /* Word count hold */
uint32              pmp_addr_hold;   /* Address register hold */
uint32              pmp_wc;          /* Current word count register */
uint32              pmp_addr;        /* Current address register */
uint64              pmp_data;        /* Data assembly register */
int                 pmp_cnt;         /* Character count in asm register */
int                 pmp_cmd;         /* Current command */
uint32              pmp_irq;         /* Irq enable flags */
UNIT               *pmp_cur_unit;    /* Currently addressed unit */


t_stat              pmp_devio(uint32 dev, uint64 *data);
int                 pmp_checkirq();
int                 pmp_posterror(uint64);
int                 chan_read_byte(uint8 *data);
int                 chan_write_byte(uint8 *data);
void                chan_end(uint8 flags);
void                pmp_startcmd();
void                pmp_adjpos(UNIT * uptr);
t_stat              pmp_srv(UNIT *);
t_stat              pmp_reset(DEVICE *);
t_stat              pmp_attach(UNIT *, CONST char *);
t_stat              pmp_detach(UNIT *);
t_stat              pmp_set_type(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              pmp_get_type(FILE * st, UNIT * uptr, int32 v,
                                 CONST void *desc);
t_stat              pmp_set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              pmp_get_dev_addr(FILE * st, UNIT * uptr, int32 v,
                                 CONST void *desc);
t_stat              pmp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *pmp_description (DEVICE *dptr);

DIB pmp_dib[] = { 
    {PMP_DEV, 2, &pmp_devio, NULL}};


MTAB                pmp_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
     &pmp_set_type, &pmp_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &pmp_set_dev_addr,
        &pmp_get_dev_addr, NULL},
    {0}
};

UNIT                pmp_unit[] = {
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x60), 0)},       /* 0 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x61), 0)},       /* 1 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x62), 0)},       /* 2 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x63), 0)},       /* 3 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x64), 0)},       /* 4 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x65), 0)},       /* 5 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x66), 0)},       /* 6 */
    {UDATA(&pmp_srv, UNIT_DASD|UNIT_ADDR(0x67), 0)},       /* 7 */
};

DEVICE              pmp_dev = {
    "PMP", pmp_unit, NULL, pmp_mod,
    NUM_UNITS_PMP, 8, 15, 1, 8, 8,
    NULL, NULL, &pmp_reset, NULL, &pmp_attach, &pmp_detach,
    &pmp_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_DISK, 0, dev_debug,
    NULL, NULL, &pmp_help, NULL, NULL, &pmp_description
};


/* IOT routines */
t_stat
pmp_devio(uint32 dev, uint64 *data) {
     int      i;

     switch(dev & 07) {
     case CONI:
          *data = pmp_status | pmp_pia;
          if (pmp_checkirq())
              *data |= PI_ACT;
          if (pmp_statusb & IS_CH)
              *data |= INT_SEL;
          if (pmp_statusb & REQ_CH)
              *data |= REQ_SEL;
          if (pmp_statusb & IDLE_CH)
              *data |= IDLE;
          if ((pmp_statusb & (WCMA_LD|CMD_LD)) != (WCMA_LD|CMD_LD))
              *data |= HOLD_EMPTY;
          if (pmp_cur_unit != NULL)
              *data |= ((uint64)GET_UADDR(pmp_cur_unit->flags)) << 24; 
          if ((pmp_status & (NXM_ERR|CHA_ERR|SEL_ERR)) != 0)
              *data |= UNU_END;
          sim_debug(DEBUG_CONI, &pmp_dev, "PMP %03o CONI %012llo PC=%o\n",
               dev, *data, PC);
          break;
    
     case CONO:
          sim_debug(DEBUG_CONO, &pmp_dev, "PMP %03o CONO %012llo PC=%06o\n",
                    dev, *data, PC);
          if (*data & 010)
             pmp_pia = *data & 7;
          pmp_irq = (uint32)(*data);
          (void)pmp_checkirq();
          break;

     case DATAI:
          sim_debug(DEBUG_DATAIO, &pmp_dev, "PMP %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
          *data = (uint64)(pmp_addr);
          break;

     case DATAO:
          pmp_addr_hold = (*data) & RMASK;
          pmp_wc_hold = (*data >> 18) & RMASK;
          pmp_statusb |= WCMA_LD;
          sim_debug(DEBUG_DATAIO, &pmp_dev, "PMP %03o DATO %012llo %d PC=%06o\n",
                    dev, *data, (int)(((RMASK ^ pmp_wc_hold) + 1) & RMASK), PC);
          (void)pmp_checkirq();
          break;

     case CONI|04:
          *data = pmp_statusb;
          if ((pmp_statusb & WCMA_LD) != 0 && (pmp_statusb & CMD_LD) != 0)
             *data |= CMD_FUL;
          if ((*data & CMD_FUL) == 0)
             *data |= CMD_EMP;
          if ((pmp_statusb & (OP1|REQ_CH|IDLE_CH)) == IDLE_CH)
             *data |= OPL;
          sim_debug(DEBUG_CONI, &pmp_dev, "IBM %03o CONI %012llo PC=%o\n",
               dev, *data, PC);
          break;

     case CONO|04:
          sim_debug(DEBUG_CONO, &pmp_dev, "IBM %03o CONO %012llo PC=%06o\n",
                    dev, *data, PC);
          if (*data & PWR_CLR) {    /* Power on clear */
              pmp_statusb = IDLE_CH;
              pmp_status = 0;
              pmp_pia = 0;
              /* Clear command in each unit */
              break;
          }
          if (*data & CHN_RESET) {
              pmp_statusb = IDLE_CH;
              pmp_status = 0;
              break;
          }
          if (*data & STS_CLR)      /* Clear status bits */
              pmp_status &= ~STS_MASK;
          if (*data & CLR_DATCH)    /* Data chaining */
              pmp_cmd &= ~DATCH_ON;
          if (*data & CMD_CLR)      /* Clear pending command */
              pmp_statusb &= ~CMD_LD;
          if (*data & CMD_HOLD) {    /* Set command buffer full */
              pmp_statusb |= CMD_LD;
          }
          if (*data & (CLR_UEND|CLR_IRQ))     /* Clear unusual end condtions */
              pmp_status &= ~(UNU_END|NEW_STS|STS_MASK);
          if (*data & NSTS_CLR) {    /* Clear new status */
              pmp_status &= ~NEW_STS;
              if ((pmp_statusb & OP1) == 0) { /* Check if any device requesting attn */
                  for (i = 0; i < NUM_UNITS_PMP; i++) {
                      if ((pmp_dev.units[i].CMD & DK_ATTN) != 0) {
                          pmp_cur_unit = &pmp_dev.units[i];
                          pmp_status |= NEW_STS|DEV_END;
                          pmp_dev.units[i].CMD &= ~DK_ATTN;
                          break;
                      }
                  }
                  if ((pmp_statusb & NEW_STS) == 0) {
                      pmp_statusb &= ~REQ_CH;
                      if (pmp_statusb & CMD_LD)
                          pmp_startcmd();
                  }
              }
          }
          (void)pmp_checkirq();
          break;

     case DATAI|4:
          sim_debug(DEBUG_DATAIO, &pmp_dev, "IBM %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
          break;

     case DATAO|4:
          sim_debug(DEBUG_DATAIO, &pmp_dev, "IBM %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
          pmp_cmd_hold = (*data) & HOLD_MASK;
          pmp_statusb |= CMD_LD;
          pmp_startcmd();
          (void)pmp_checkirq();
          break;
     }
     return SCPE_OK;
}

/* Check if interrupt pending for device */
int
pmp_checkirq() {
    int   f = 0;

    clr_interrupt(PMP_DEV);
    if ((pmp_irq & IRQ_ERROR) != 0 && (pmp_status & (PAR1_ERR|PAR2_ERR|PAR_ERR)) != 0) {
          sim_debug(DEBUG_DETAIL, &pmp_dev, "parity irq\n");
        f = 1;
    }
    if ((pmp_irq & IRQ_EMPTY) != 0 && (pmp_statusb & (WCMA_LD|CMD_LD)) != (WCMA_LD|CMD_LD))  {
          sim_debug(DEBUG_DETAIL, &pmp_dev, "load irq\n");
        f = 1;
    }
    if ((pmp_irq & IRQ_IDLE) != 0 && (pmp_statusb & (OP1|IDLE_CH)) == IDLE_CH) {
          sim_debug(DEBUG_DETAIL, &pmp_dev, "idle irq\n");
        f = 1;
}
    if ((pmp_irq & IRQ_UEND) != 0 && (pmp_status & (NXM_ERR|CHA_ERR|SEL_ERR|UNU_END)) != 0) {
          sim_debug(DEBUG_DETAIL, &pmp_dev, "uend irq\n");
        f = 1;
}
    if ((pmp_status & pmp_irq & (IRQ_NSTS|IRQ_STS)) != 0) {
          sim_debug(DEBUG_DETAIL, &pmp_dev, "mem sts %o\n", (int)(pmp_status & pmp_irq & (IRQ_NSTS|IRQ_STS)));
        f = 1;
}
    if (f)
        set_interrupt(PMP_DEV, pmp_pia);
    return f;
}

/* Post and error message and clear channel */
int
pmp_posterror(uint64 err) {
    pmp_status |= err;
    pmp_statusb &= ~(OP1|IS_CH|TRANS_CH);
    pmp_statusb |= IDLE_CH;
    (void)pmp_checkirq();
    return 1;
}

/* read byte from memory */
int
chan_read_byte(uint8 *data) {
    int         byte;
    int         xfer = 0;

    if ((pmp_cmd & 0x1)  == 0) {
        return 1;
    }
    /* Check if finished transfer */
    if (pmp_cnt & BUFF_CHNEND)
        return 1;

    pmp_statusb |= TRANS_CH;                 /* Tranfer in progress */
    /* Read in next work if buffer is in empty status */
    if (pmp_cnt & BUFF_EMPTY) {
        if (pmp_addr >= (int)MEMSIZE) 
            return pmp_posterror(NXM_ERR);
        pmp_data = M[pmp_addr];
         sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_read %06o %012llo\n", pmp_addr, pmp_data);
        pmp_addr++;
        pmp_cnt = 0;
        xfer = 1;  /* Read in a word */
    }
    /* Handle word vs byte mode */
    if (pmp_cmd & BYTE_MODE) {
        byte = (pmp_data >> (4 + (8 * (3 - (pmp_cnt & 0x3))))) & 0xff;
        pmp_cnt++;
        *data = byte;
        if ((pmp_cnt & 03) == 0)
            pmp_cnt = BUFF_EMPTY;
    } else {
        if ((pmp_cnt & 0xf) > 0x3) {
           if ((pmp_cnt & 0xf) == 0x4) {   /* Split byte */
              byte = (pmp_data << 4) & 0xf0;
              if (pmp_addr >= (int)MEMSIZE)
                  return pmp_posterror(NXM_ERR);
              pmp_data = M[pmp_addr];
         sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_read %06o %012llo\n", pmp_addr, pmp_data);
              pmp_addr++;
              xfer = 1;  /* Read in a word */
              byte |= pmp_data & 0xf;
           } else {
              byte = (pmp_data >> (4 + (8 * (8 - (pmp_cnt & 0xf))))) & 0xff;
           }
        } else {
           byte = (pmp_data >> (4 + (8 * (3 - (pmp_cnt & 0xf))))) & 0xff;
        }
        pmp_cnt++;
        if ((pmp_cnt & 0xf) == 9)
            pmp_cnt = BUFF_EMPTY;
     }
     *data = byte;
     if (pmp_cmd & CNT_BYT) {
         pmp_wc ++;
     } else if (xfer) {
         pmp_wc ++;
     }
     if (pmp_wc & 07000000) 
         pmp_cnt |= BUFF_CHNEND;
     return 0;
#if 0
next:
     /* If not data channing, let device know there will be no
      * more data to come
      */
     if ((pmp_cmd & DATCH_ON) == 0) {
         pmp_cnt = BUFF_CHNEND;
         sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_read_end\n");
         return 1;
     } else {
         if (pmp_statusb & WCMA_LD) {
             pmp_statusb &= ~(WCMA_LD);
             pmp_addr = pmp_addr_hold;
             pmp_wc = pmp_wc_hold;
             pmp_data = 0;
         } else {
             return pmp_posterror(CHA_ERR);
         }
     }
     goto load;
#endif
}

/* write byte to memory */
int
chan_write_byte(uint8 *data) {
    int          xfer = 0;

    if ((pmp_cmd & 0x1)  != 0) {
        return 1;
    }
    /* Check if at end of transfer */
    if (pmp_cnt == BUFF_CHNEND) {
        return 1;
    }

    pmp_statusb |= TRANS_CH;                 /* Tranfer in progress */
    if (pmp_cnt == BUFF_EMPTY) {
        pmp_data = 0;
        pmp_cnt = 0;
    }
    /* Handle word vs byte mode */
    if (pmp_cmd & BYTE_MODE) {
        if (pmp_cnt & BUFF_CHNEND)
            return 1;
        pmp_data &= ~(0xff <<(4 + (8 * (3 - (pmp_cnt & 0x3)))));
        pmp_data |= (uint64)(*data & 0xff) << (4 + (8 * (3 - (pmp_cnt & 0x3))));
        pmp_cnt++;
        pmp_cnt |= BUFF_DIRTY;
        if ((pmp_cnt & 03) == 0) {
            pmp_cnt &= ~(BUFF_DIRTY|7);
            if (pmp_addr >= (int)MEMSIZE)
                return pmp_posterror(NXM_ERR);
            M[pmp_addr] = pmp_data;
              sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write %06o %012llo\n", pmp_addr, pmp_data);
              pmp_addr++;
            xfer = 1;
        }
    } else {
        if ((pmp_cnt & 0xf) > 0x3) {
           if ((pmp_cnt & 0xf) == 0x4) {   /* Split byte */
              pmp_data &= ~0xf;
              pmp_data |= (uint64)((*data >> 4) & 0xf);
              if (pmp_addr >= (int)MEMSIZE)
                  return pmp_posterror(NXM_ERR);
              M[pmp_addr] = pmp_data;
              sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write %06o %012llo %2x\n", pmp_addr, pmp_data, pmp_cnt);
              pmp_addr++;
              xfer = 1;  /* Read in a word */
              pmp_data = *data & 0xf;
              pmp_cnt |= BUFF_DIRTY;
           } else {
              pmp_data &= ~(0xff <<(4 + (8 * (8 - (pmp_cnt & 0xf)))));
              pmp_data |= (uint64)(*data & 0xff) << (4 + (8 * (8 - (pmp_cnt & 0xf))));
              pmp_cnt |= BUFF_DIRTY;
           }
        } else {
           pmp_data &= ~(0xff <<(4 + (8 * (3 - (pmp_cnt & 0xf)))));
           pmp_data |= (uint64)(*data & 0xff) << (4 + (8 * (3 - (pmp_cnt & 0xf))));
           pmp_cnt |= BUFF_DIRTY;
        }
        pmp_cnt++;
        if ((pmp_cnt & 0xf) == 9) {
            pmp_cnt = BUFF_EMPTY;
            if (pmp_addr >= (int)MEMSIZE)
                return pmp_posterror(NXM_ERR);
            M[pmp_addr] = pmp_data;
            sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write %06o %012llo %2x\n", pmp_addr, pmp_data, pmp_cnt);
            pmp_addr++;
            xfer = 1;  /* Read in a word */
        }
     }
     if (pmp_cmd & CNT_BYT) {
         pmp_wc ++;
     } else if (xfer) {
         pmp_wc ++;
     }
     if (pmp_wc & 07000000) {
         /* If not data channing, let device know there will be no
          * more data to come
          */
             sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write_wc\n");
         if ((pmp_cmd & DATCH_ON) == 0) {
             pmp_cnt = BUFF_CHNEND;
             sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write_end\n");
             return 1;
         } else {
             sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_write reload\n");
             if (pmp_statusb & WCMA_LD) {
                 pmp_statusb &= ~(WCMA_LD);
                 pmp_addr = pmp_addr_hold;
                 pmp_wc = pmp_wc_hold;
                 pmp_data = 0;
             } else {
                 return pmp_posterror(CHA_ERR);
             }
         }
     }
     return 0;
}

/*
 * Signal end of transfer by device.
 */
void
chan_end(uint8 flags) {

    sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_end(%x) %x\n", flags, pmp_wc);
    /* If PCI flag set, trigger interrupt */
    /* Flush buffer if there was any change */
    if (pmp_cnt & BUFF_DIRTY) {
        pmp_cnt = BUFF_EMPTY;
        if (pmp_addr >= (int)MEMSIZE) {
            (void) pmp_posterror(NXM_ERR);
            return;
        }
        M[pmp_addr] = pmp_data;
        sim_debug(DEBUG_DATA, &pmp_dev, "chan_write %012llo\n", pmp_data);
        pmp_addr++;
    }
    pmp_statusb &= ~TRANS_CH;                 /* Clear transfer in progress */
    pmp_statusb |= IDLE_CH; 
    pmp_status |= NEW_STS | CHN_END | ((uint64)flags) << 5;

    if (pmp_status & (BSY|UNIT_CHK))
        pmp_status |= UNU_END;

    /* If channel is also finished, then skip any more data commands. */
    if (pmp_status & (CHN_END|DEV_END)) {
        pmp_cnt = BUFF_CHNEND;
        sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_endc %012llo %06o\n", pmp_status, pmp_cmd);

        /* While command has chain data set, continue to skip */
        if (pmp_cmd & DATCH_ON) {
            (void) pmp_posterror(CHA_ERR);
            return;
        } 

        if (pmp_cmd & CMDCH_ON) {
           pmp_startcmd();
           (void)pmp_checkirq();
           return;
        }
        /* Indicate that device is done */
        pmp_statusb &= ~OP1;
    }
    sim_debug(DEBUG_DETAIL, &pmp_dev, "chan_endf %012llo %06o\n", pmp_status, pmp_statusb);
    (void)pmp_checkirq();
}

/* Issue command to device */
void
pmp_startcmd() {
    uint16         addr;
    int            i;
    int            unit;
    int            cmd;
    int            old_cmd = pmp_cmd;
    uint8          ch;

    sim_debug(DEBUG_CMD, &pmp_dev, "start command %o\n", pmp_statusb);
    if ((pmp_statusb & CMD_LD) == 0 || (pmp_statusb & IDLE_CH) == 0) {
       sim_debug(DEBUG_CMD, &pmp_dev, "not ready %o\n", pmp_statusb);
       return;
    }
    /* Idle, no device selected. */
    if ((pmp_statusb & OP1) == 0) {
        /* Set to initial selection. */
        pmp_statusb |= IS_CH;
        pmp_cur_unit = NULL;

        /* Copy over command */
        pmp_cmd = pmp_cmd_hold;
        cmd = pmp_cmd & CMD_MASK;
        pmp_statusb &= ~(CMD_LD);
        if (pmp_statusb & WCMA_LD) {
            pmp_statusb &= ~(WCMA_LD);
            pmp_addr = pmp_addr_hold;
            pmp_wc = pmp_wc_hold;
            pmp_cnt = BUFF_EMPTY;
        }
        addr = (uint16)((pmp_cmd & DEV_ADDR) >> 14);
        sim_debug(DEBUG_CMD, &pmp_dev, "initiate on %02x\n", addr);
        /* scan units looking for matching device. */
        for (i = 0; i < NUM_UNITS_PMP; i++) {
            if (addr == GET_UADDR(pmp_dev.units[i].flags)) {
               pmp_cur_unit = &pmp_dev.units[i];
               break;
            }
        }
    }


    /* If no matching device found, report selection error */
    if (pmp_cur_unit == NULL) {
        sim_debug(DEBUG_CMD, &pmp_dev, "No device\n");
        (void)pmp_posterror(SEL_ERR);
        return;
    }

    /* Check if unit is busy */
    unit = GET_UADDR(pmp_cur_unit->flags) & 0x7;

    /* Check if device busy */
    if ((pmp_cur_unit->CMD & 0xff) != 0) {
       sim_debug(DEBUG_CMD, &pmp_dev, "busy %o\n", pmp_statusb);
       if (pmp_statusb & IS_CH)
          (void)pmp_posterror(SEL_ERR);
       pmp_status |= UNU_END|BSY;
       (void)pmp_checkirq();
       return;
    }

    /* Copy over command */
    if ((pmp_statusb & CMD_LD) != 0) {
        pmp_cmd = pmp_cmd_hold;
        sim_debug(DEBUG_CMD, &pmp_dev, "load %o\n", pmp_cmd);
        pmp_statusb &= ~(CMD_LD);
        if (pmp_statusb & WCMA_LD) {
            pmp_statusb &= ~(WCMA_LD);
            pmp_addr = pmp_addr_hold;
            pmp_wc = pmp_wc_hold;
            pmp_cnt = BUFF_EMPTY;
        }
    }

    /* Otherwise if there is command chaining, try new command */
    if (old_cmd & CMDCH_ON) {
        /* Channel in operation, must be command chaining */
        if (((old_cmd & SKP_MOD_OFF) != 0) && ((pmp_status & ST_MOD) == 0)) {
            pmp_statusb &= ~(CMD_LD);
            (void)pmp_checkirq();
            return;
        }
        if (((old_cmd & SKP_MOD_ON) != 0) && ((pmp_status & ST_MOD) != 0)) {
            pmp_statusb &= ~(CMD_LD);
            (void)pmp_checkirq();
            return;
        }
    }
    sim_debug(DEBUG_CMD, &pmp_dev, "CMD unit=%d %02x %06o\n", unit, pmp_cmd, pmp_addr);

    (void)pmp_checkirq();

    cmd = pmp_cmd & CMD_MASK;
    /* If device not attached, return error */
    if ((pmp_cur_unit->flags & UNIT_ATT) == 0) {
       if (cmd == 0x4) {  /* Sense */
           sim_debug(DEBUG_CMD, &pmp_dev, "CMD sense\n");
           ch = pmp_cur_unit->SENSE & 0xff;
           sim_debug(DEBUG_DETAIL, &pmp_dev, "sense unit=%d 1 %x\n", unit, ch);
           chan_write_byte(&ch) ;
           ch = (pmp_cur_unit->SENSE >> 8) & 0xff;
           sim_debug(DEBUG_DETAIL, &pmp_dev, "sense unit=%d 2 %x\n", unit, ch);
           chan_write_byte(&ch) ;
           ch = 0;
           sim_debug(DEBUG_DETAIL, &pmp_dev, "sense unit=%d 3 %x\n", unit, ch);
           chan_write_byte(&ch) ;
           ch = unit;
           sim_debug(DEBUG_DETAIL, &pmp_dev, "sense unit=%d 4 %x\n", unit, ch);
           chan_write_byte(&ch) ;
           ch = 0;
           chan_write_byte(&ch) ;
           chan_write_byte(&ch) ;
           pmp_cur_unit->SENSE = 0;
           pmp_status |= NEW_STS|CHN_END|DEV_END;
           (void)pmp_posterror(0);
           return;
       }
       if (cmd == 0x0)
           return;
       pmp_cur_unit->SENSE = SNS_INTVENT|SNS_CMDREJ;
       pmp_status |= UNU_END|NEW_STS|CHN_END|DEV_END|UNIT_CHK;
       (void)pmp_posterror(0);
       return;
    }

    /* Issue the actual command */
    switch (cmd & 0x3) {
    case 0x3:              /* Control */
         if (cmd == 0x3 || cmd == DK_RELEASE) {
            pmp_status &= ~(STS_MASK);
            pmp_status |= NEW_STS|CHN_END|DEV_END;
            if ((pmp_cmd & CMDCH_ON) == 0)
                /* Indicate that device is done */
                pmp_statusb &= ~OP1;
            (void)pmp_checkirq();
            return;
         }

         /* Fall Through */

    case 0x1:              /* Write command */
    case 0x2:              /* Read command */
         pmp_statusb &= ~IDLE_CH;
         pmp_cur_unit->CMD &= ~(DK_PARAM);
         pmp_cur_unit->CMD |= cmd;
         sim_debug(DEBUG_CMD, &pmp_dev, "CMD unit=%d CMD=%02x\n", unit, pmp_cur_unit->CMD);
         return;

    case 0x0:               /* Status */
         if (cmd == 0x4) {  /* Sense */
            pmp_statusb &= ~IDLE_CH;
            pmp_cur_unit->CMD |= cmd;
            return;
         }
         break;
    }
    pmp_status &= ~(STS_MASK);
    if (pmp_cur_unit->SENSE & 0xff)
        pmp_status |= UNU_END|UNIT_CHK;
    pmp_status |= NEW_STS|CHN_END|DEV_END;
    pmp_statusb |= IDLE_CH;
    pmp_statusb &= ~OP1;
    sim_debug(DEBUG_CMD, &pmp_dev, "CMD unit=%d finish\n", unit);
    (void)pmp_checkirq();
}

/* Compute position on new track. */
void
pmp_adjpos(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->flags);
    struct pmp_t      *data = (struct pmp_t *)(uptr->DATAPTR);
    uint8               *rec;
    int                 pos;

    /* Save current position */
    pos = data->tpos;

    /* Set ourselves to start of track */
    data->state = DK_POS_HA;
    data->rec = data->klen = 0;
    data->rpos = data->count = data->dlen = 0;
    data->tstart = (uptr->POS & 0xff) * data->tsize;
    rec = &data->cbuf[data->rpos + data->tstart];
    /* Skip forward until we reach pos */
    for (data->tpos = 0; data->tpos < pos; data->tpos++) {
        switch(data->state) {
         case DK_POS_HA:                /* In home address (c) */
              if (data->count == 4) {
                  data->tpos = data->rpos = 5;
                  data->state = DK_POS_CNT;
                  rec = &data->cbuf[data->rpos + data->tstart];
                  /* Check for end of track */
                  if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                     data->state = DK_POS_END;
              }
              break;
         case DK_POS_CNT:               /* In count (c) */
              if (data->count == 0) {
                  /* Check for end of track */
                  if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff) {
                     data->state = DK_POS_END;
                  }
                  data->klen = rec[5];
                  data->dlen = (rec[6] << 8) | rec[7];
              }
              if (data->count == 7) {
                     data->state = DK_POS_KEY;
                     if (data->klen == 0)
                        data->state = DK_POS_DATA;
              }
              break;
         case DK_POS_KEY:               /* In Key area */
              if (data->count == data->klen) {
                  data->state = DK_POS_DATA;
                  data->count = 0;
              }
              break;
         case DK_POS_DATA:              /* In Data area */
              if (data->count == data->dlen) {
                  data->state = DK_POS_AM;
              }
              break;
         case DK_POS_AM:                /* Beginning of record */
              data->rpos += data->dlen + data->klen + 8;
              data->tpos = data->rpos;
              data->rec++;
              data->state = DK_POS_CNT;
              data->count = 0;
              rec = &data->cbuf[data->rpos + data->tstart];
              /* Check for end of track */
              if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                 data->state = DK_POS_END;
              break;
         case DK_POS_END:               /* Past end of data */
              data->tpos+=10;
              data->count = 0;
              data->klen = 0;
              data->dlen = 0;
              return;
         }
     }
}

/* Handle processing of disk requests. */
t_stat pmp_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    struct pmp_t       *data = (struct pmp_t *)(uptr->DATAPTR);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->CMD & 0x7f;
    int                 type = GET_TYPE(uptr->flags);
    int                 state = data->state;
    int                 count = data->count;
    int                 trk;
    int                 i;
    int                 rd = ((cmd & 0x3) == 0x1) | ((cmd & 0x3) == 0x2);
    uint8               *rec;
    uint8               *da;
    uint8               ch;
    uint8               buf[8];

    /* Check if read or write command, if so grab correct cylinder */
    if (rd && data->cyl != data->ccyl) {
        uint32 tsize = data->tsize * disk_type[type].heads;
        if (uptr->CMD & DK_CYL_DIRTY) {
              (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
              (void)sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
              uptr->CMD &= ~DK_CYL_DIRTY;
        }
        data->ccyl = data->cyl;
        sim_debug(DEBUG_DETAIL, dptr, "Load unit=%d cyl=%d\n", unit, data->cyl);
        data->cpos = sizeof(struct pmp_header) + (data->ccyl * tsize);
        (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    }
    sim_debug(DEBUG_EXP, dptr, "state unit=%d %02x %d\n", unit, state, data->tpos);

    rec = &data->cbuf[data->rpos + data->tstart];
    da = &data->cbuf[data->tpos + data->tstart];
    if (state != DK_POS_SEEK && data->tpos >= data->tsize) {
        sim_debug(DEBUG_EXP, dptr, "state end unit=%d %d\n", unit, data->tpos);
        state = DK_POS_INDEX;
    }
    switch(state) {
    case DK_POS_INDEX:             /* At Index Mark */
         /* Read and multi-track advance to next head */
         if ((uptr->CMD & 0x83) == 0x82 || (uptr->CMD & 0x83) == 0x81) {
             sim_debug(DEBUG_DETAIL, dptr, "adv head unit=%d %02x %d %d %02x\n",
                   unit, state, data->tpos, uptr->POS & 0xff, data->filemsk);
             if ((data->filemsk & DK_MSK_SK) == DK_MSK_SKNONE) {
                 sim_debug(DEBUG_DETAIL, dptr, "end cyl skmsk unit=%d %02x %d %02x\n",
                           unit, state, data->tpos, data->filemsk);
                 uptr->SENSE = (SNS_WRP << 8);
                 uptr->CMD &= ~0xff;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 goto index;
             }
             uptr->POS ++;
             if ((uptr->POS & 0xff) >= disk_type[type].heads) {
                 sim_debug(DEBUG_DETAIL, dptr, "end cyl unit=%d %02x %d\n",
                           unit, state, data->tpos);
                 uptr->SENSE = (SNS_ENDCYL << 8);
                 data->tstart = 0;
                 uptr->POS &= ~0xff;
                 uptr->CMD &= ~0xff;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 goto index;
             }
             if ((uptr->CMD & 0x7) == 1 && (uptr->CMD & 0x60) != 0)
                 uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
         }
         /* If INDEX set signal no record if read */
         if ((cmd & 0x03) == 0x01 && uptr->CMD & DK_INDEX2) {
             sim_debug(DEBUG_DETAIL, dptr, "index unit=%d %02x %d %04x\n",
                   unit, state, data->tpos, uptr->SENSE);
             /* Unless command is Read Header, return No record found */
             if (cmd != DK_RD_HA)
                 uptr->SENSE |= (SNS_NOREC << 8);
             uptr->CMD &= ~0xff;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
index:
         uptr->CMD |= (uptr->CMD & DK_INDEX) ? DK_INDEX2 : DK_INDEX;
         uptr->CMD &= ~DK_SRCOK;
         data->tstart = data->tsize * (uptr->POS & 0xff);
         data->tpos = data->rpos = 0;
         data->state = DK_POS_HA;
         data->rec = 0;
         sim_activate(uptr, 100);
         break;

    case DK_POS_HA:                /* In home address (c) */
         data->tpos++;
         if (data->count == 4) {
             data->tpos = data->rpos = 5;
             data->state = DK_POS_CNT;
             sim_debug(DEBUG_EXP, dptr, "state HA unit=%d %d %d\n", unit, data->count,
                      data->tpos);
             rec = &data->cbuf[data->rpos + data->tstart];
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                data->state = DK_POS_END;
             sim_activate(uptr, 40);
         } else
             sim_activate(uptr, 10);
         break;
    case DK_POS_CNT:               /* In count (c) */
         data->tpos++;
         if (data->count == 0) {
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff) {
                state = DK_POS_END;
                data->state = DK_POS_END;
             }
             data->klen = rec[5];
             data->dlen = (rec[6] << 8) | rec[7];
             sim_debug(DEBUG_EXP, dptr, "state count unit=%d r=%d k=%d d=%d %d\n",
                 unit, data->rec, data->klen, data->dlen, data->tpos);
         }
         if (data->count == 7) {
             data->state = DK_POS_KEY;
             if (data->klen == 0)
                 data->state = DK_POS_DATA;
             sim_activate(uptr, 50);
         } else {
             sim_activate(uptr, 10);
         }
         break;
    case DK_POS_KEY:               /* In Key area */
         data->tpos++;
         if (data->count == data->klen) {
             sim_debug(DEBUG_EXP, dptr, "state key unit=%d %d %d\n", unit, data->rec,
                      data->count);
             data->state = DK_POS_DATA;
             data->count = 0;
             count = 0;
             state = DK_POS_DATA;
             sim_activate(uptr, 50);
         } else {
             sim_activate(uptr, 10);
         }
         break;
    case DK_POS_DATA:              /* In Data area */
         data->tpos++;
         if (data->count == data->dlen) {
             sim_debug(DEBUG_EXP, dptr, "state data unit=%d %d %d\n", unit, data->rec,
                      data->count);
             data->state = DK_POS_AM;
             sim_activate(uptr, 50);
         } else {
             sim_activate(uptr, 10);
         }
         break;
    case DK_POS_AM:                /* Beginning of record */
         data->rpos += data->dlen + data->klen + 8;
         data->tpos = data->rpos;
         data->rec++;
         sim_debug(DEBUG_EXP, dptr, "state am unit=%d %d %d\n", unit, data->rec,
                data->count);
         data->state = DK_POS_CNT;
         data->count = 0;
         rec = &data->cbuf[data->rpos + data->tstart];
         /* Check for end of track */
         if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
            data->state = DK_POS_END;
         sim_activate(uptr, 60);
         break;
    case DK_POS_END:               /* Past end of data */
         data->tpos+=10;
         data->count = 0;
         data->klen = 0;
         data->dlen = 0;
         sim_activate(uptr, 50);
         break;
    case DK_POS_SEEK:                  /* In seek */
         /* Compute delay based of difference. */
         /* Set next state = index */
         i = (uptr->POS >> 8) - data->cyl;
         sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d %d %d s=%x\n", unit, uptr->POS >> 8, i,
                 data->state);
         if (i == 0) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             data->state = DK_POS_INDEX;
             sim_activate(uptr, 20);
         } else if (i > 0 ) {
             if (i > 20) {
                data->cyl += 20;
                sim_activate(uptr, 1000);
             } else {
                data->cyl ++;
                sim_activate(uptr, 200);
             }
         } else {
             if (i < -20) {
                data->cyl -= 20;
                sim_activate(uptr, 1000);
             } else {
                data->cyl --;
                sim_activate(uptr, 200);
             }
         }
         sim_debug(DEBUG_DETAIL, dptr, "seek next unit=%d %d %d %x\n", unit, uptr->POS >> 8,
                data->cyl, data->state);
         break;
    }

    if ((pmp_statusb & IS_CH) != 0 && cmd != 0) {
        pmp_statusb &= ~IS_CH;
        pmp_statusb |= OP1;
        uptr->CMD &= ~(DK_INDEX|DK_NOEQ|DK_HIGH|DK_PARAM|DK_MSET|DK_DONE|DK_INDEX2);
        data->filemsk = 0;
        sim_debug(DEBUG_CMD, dptr, "initial select  unit=%d\n", unit);
    }

    switch (cmd) {
    case 0:                               /* No command, stop tape */
         break;

    case 0x3:
         sim_debug(DEBUG_CMD, dptr, "nop unit=%d\n", unit);
         uptr->CMD &= ~0xff;
         chan_end(SNS_CHNEND|SNS_DEVEND);
         break;

    case 0x4:                 /* Sense */
         ch = uptr->SENSE & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         if (chan_write_byte(&ch))
             goto sense_end;
         ch = (uptr->SENSE >> 8) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 2 %x\n", unit, ch);
         if (chan_write_byte(&ch))
             goto sense_end;
         ch = 0;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
         if (chan_write_byte(&ch))
             goto sense_end;
         if (disk_type[type].sen_cnt > 6) {
             ch = (unit & 07) | ((~unit & 07) << 3);
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             ch = unit;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             ch = (uptr->POS >> 8) & 0xff;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 6 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             ch = (uptr->POS & 0x1f) | ((uptr->POS & 0x10000) ? 0x40 : 0);
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 7 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             ch = 0;              /* Compute message code */
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 8 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             i = 8;
         } else {
             if (disk_type[type].dev_type == 0x11)
                 ch = 0xc8;
             else
                 ch = 0x40;
             if ((uptr->POS >> 8) & SNS_ENDCYL)
                ch |= 4;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             ch = unit;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
             i = 5;
         }
         ch = 0;
         for (; i < disk_type[type].sen_cnt; i++) {
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d %d %x\n", unit, i, ch);
             if (chan_write_byte(&ch))
                 goto sense_end;
         }
sense_end:
         uptr->CMD &= ~(0xff|DK_INDEX|DK_INDEX2);
         chan_end(SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SETSECT:
         /* Not valid for drives before 3330 */
         sim_debug(DEBUG_DETAIL, dptr, "setsector unit=%d\n", unit);
         if (disk_type[type].sen_cnt > 6) {
             if (chan_read_byte(&ch)) {
                 sim_debug(DEBUG_DETAIL, dptr, "setsector rdr\n");
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 uptr->SENSE |= SNS_CMDREJ;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             /* Treat as NOP */
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(SNS_DEVEND|SNS_CHNEND);
             sim_debug(DEBUG_DETAIL, dptr, "setsector %02x\n", ch);
             break;
          }
          /* Otherwise report as invalid command */
          uptr->LASTCMD = 0;
          uptr->CMD &= ~(0xff);
          uptr->SENSE |= SNS_CMDREJ;
          chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
          break;

    case DK_SEEK:            /* Seek */
    case DK_SEEKCYL:         /* Seek Cylinder */
    case DK_SEEKHD:          /* Seek Head */

         /* If we are waiting on seek to finish, check if there yet. */
         if (uptr->CMD & DK_PARAM) {
             if ((uptr->POS >> 8) == data->cyl) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff|DK_PARAM);
             //    uptr->CMD |= DK_ATTN;
              //   pmp_statusb |= REQ_CH;
                 sim_debug(DEBUG_DETAIL, dptr, "seek end unit=%d %d %d %x\n", unit,
                      uptr->POS >> 8, data->cyl, data->state);
             chan_end(SNS_DEVEND|SNS_CHNEND);
              }
              break;
         }

         /* Check if seek valid */
         i = data->filemsk & DK_MSK_SK;
         if (i == DK_MSK_SKNONE) { /* No seeks allowed, error out */
             sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d not allow\n", unit);
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SENSE |= SNS_WRP << 8;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (i != DK_MSK_SKALLSKR) { /* Some restrictions */
             if ((cmd == DK_SEEKHD && i != DK_MSK_SKALLHD) || (cmd == DK_SEEK)) {
                 sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d not allow\n", unit);
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 uptr->SENSE |= SNS_WRP << 8;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }

         /* Read in 6 character seek code */
         for (i = 0; i < 6; i++) {
             if (chan_read_byte(&buf[i])) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 uptr->SENSE |= SNS_CMDREJ|SNS_SEEKCK;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }
         sim_debug(DEBUG_DETAIL, dptr,
             "seek unit=%d %02x %02x %02x %02x %02x %02x\n", unit,
              buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
         trk = (buf[2] << 8) | buf[3];
         sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d %d %d\n", unit, trk, buf[5]);

         /* Check if seek valid */
         if ((buf[0] | buf[1] | buf[4]) != 0 || trk > disk_type[type].cyl
                  || buf[5] >= disk_type[type].heads)  {
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SENSE |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (cmd == DK_SEEKHD && ((uptr->POS >> 8) & 0x7fff) != trk) {
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SENSE |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         uptr->POS = (trk << 8) | buf[5];

         /* Check if on correct cylinder */
         if (trk != data->cyl) {
             /* Do seek */
             uptr->CMD |= DK_PARAM;
             data->state = DK_POS_SEEK;
             sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d doing\n", unit);
//             chan_end(SNS_CHNEND);
         } else {
             pmp_adjpos(uptr);
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(SNS_DEVEND|SNS_CHNEND);
         }
         return SCPE_OK;

    case DK_RESTORE:         /* Restore */

         /* If we are waiting on seek to finish, check if there yet. */
         if (uptr->CMD & DK_PARAM) {
             if ((uptr->POS >> 8) == data->cyl) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 uptr->CMD |= DK_ATTN;
                 pmp_statusb |= REQ_CH;
                 sim_debug(DEBUG_DETAIL, dptr, "seek end unit=%d %d %d %x\n", unit,
                          uptr->POS >> 8, data->cyl, data->state);
              }
              break;
         }

         sim_debug(DEBUG_DETAIL, dptr, "restore unit=%d\n", unit);
         /* Check if restore is valid */
         if ((data->filemsk & DK_MSK_SK) != DK_MSK_SKALLSKR) {
             uptr->SENSE |= SNS_CMDREJ;
             uptr->LASTCMD = 0;
             uptr->CMD &= ~(0xff|DK_PARAM);
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         uptr->POS = 0;
         data->tstart = 0;
         /* Check if on correct cylinder */
         if (0 != data->cyl) {
             /* Do seek */
             uptr->CMD |= DK_PARAM;
             data->state = DK_POS_SEEK;
             chan_end(SNS_CHNEND);
         } else {
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(SNS_DEVEND|SNS_CHNEND);
         }
         return SCPE_OK;

    case DK_SETMSK:          /* Set file mask */
         /* If mask already set, error */
         sim_debug(DEBUG_DETAIL, dptr, "setmsk unit=%d\n", unit);
         uptr->LASTCMD = cmd;
         uptr->CMD &= ~(0xff|DK_PARAM);
         if (uptr->CMD & DK_MSET) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk dup\n");
             uptr->LASTCMD = 0;
             uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* Grab mask */
         if (chan_read_byte(&ch)) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk rdr\n");
             uptr->LASTCMD = 0;
             uptr->SENSE |= SNS_CMDREJ;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         /* Save */
         if (disk_type[type].dev_type >= 0x30) {
             /* Clear bits which have no meaning in simulator */
             ch &= 0xFC;
         }
         if ((ch & ~(DK_MSK_SK|DK_MSK_WRT)) != 0) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk inv\n");
             uptr->LASTCMD = 0;
             uptr->SENSE |= SNS_CMDREJ;
             chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         sim_debug(DEBUG_DETAIL, dptr, "setmsk unit=%d %x\n", unit, ch);
         data->filemsk = ch;
         uptr->CMD |= DK_MSET;
         chan_end(SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SPACE:           /* Space record */
         /* Not implemented yet */
         break;

    case DK_SRCH_HAEQ:       /* Search HA equal */

         /* Wait until home address is found */
         if (state == DK_POS_HA && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "search HA unit=%d %x %d %x\n",
                  unit, state, count, uptr->POS);
             uptr->CMD &= ~DK_SRCOK;
             uptr->CMD |= DK_PARAM;
             break;
         }

         /* In home address, do compare */
         if (uptr->CMD & DK_PARAM) {
             if (chan_read_byte(&ch)) {
                  if (count < 4)
                      uptr->CMD |= DK_SHORTSRC;
             } else if (ch != *da) {
                  uptr->CMD |= DK_NOEQ;
             }
             sim_debug(DEBUG_DETAIL, dptr,
                 "search HA unit=%d %d %x %02x=%02x %d\n", unit,
                 count, state, ch, *da, data->tpos);
             /* At end of count */
             if (count == 4 || uptr->CMD & DK_SHORTSRC) {
                uptr->LASTCMD = cmd;
                uptr->CMD &= ~(0xff|DK_PARAM);
                if (uptr->CMD & DK_NOEQ)
                    chan_end(SNS_CHNEND|SNS_DEVEND);
                else {
                    uptr->CMD |= DK_SRCOK;
                    chan_end(SNS_CHNEND|SNS_DEVEND|SNS_SMS);
                }
             }
         }
         break;

    case DK_RD_CNT:          /* Read count */
         /* Wait for next address mark */
         if (state == DK_POS_AM)
             uptr->CMD |= DK_PARAM;

         /* When we are at count segment and passed address mark */
         if (uptr->CMD & DK_PARAM && state == DK_POS_CNT && data->rec != 0) {
             ch = *da;
             sim_debug(DEBUG_DETAIL, dptr, "readcnt ID unit=%d %d %x %02x %x %d %x\n",
                 unit, count, state, ch, uptr->POS, data->tpos, uptr->POS);
             if (chan_write_byte(&ch) || count == 7) {
                uptr->LASTCMD = cmd;
                uptr->CMD &= ~(0xff);
                chan_end(SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_SRCH_IDEQ:       /* Search ID equal */
    case DK_SRCH_IDGT:       /* Search ID greater */
    case DK_SRCH_IDGE:       /* Search ID greater or equal */
         /* Wait for beginning of count segment */
         if (state == DK_POS_CNT && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "search ID unit=%d %x %d %x %d\n",
                           unit, state, count, uptr->POS, data->rec);
             sim_debug(DEBUG_DETAIL, dptr, "ID unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                 unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
             uptr->CMD &= ~(DK_SRCOK|DK_SHORTSRC|DK_NOEQ|DK_HIGH);
             uptr->CMD |= DK_PARAM;
         }

         /* In count segment */
         if (uptr->CMD & DK_PARAM) {
             /* Wait for start of record */
             if (chan_read_byte(&ch)) {
                  uptr->CMD |= DK_SHORTSRC;
             } else if (ch != *da) {
                  if ((uptr->CMD & DK_NOEQ) == 0) {
                      uptr->CMD |= DK_NOEQ;
                      if (ch < *da)
                         uptr->CMD |= DK_HIGH;
                  }
             }
             sim_debug(DEBUG_DETAIL, dptr,
                  "search ID unit=%d %d %x %02x=%02x %d %c %c\n", unit, count,
                      state, ch, *da, data->tpos,
                         ((uptr->CMD & DK_NOEQ) ? '!' : '='),
                         ((uptr->CMD & DK_HIGH) ? 'h' : 'l'));
             if (count == 4 || uptr->CMD & DK_SHORTSRC) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x20) && (uptr->CMD & DK_NOEQ) == 0)
                      i = SNS_SMS;
                 if ((cmd & 0x40) && (uptr->CMD & DK_HIGH))
                      i = SNS_SMS;
                 if (i) {
                     uptr->CMD |= DK_SRCOK;
                 }
                 chan_end(SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_SRCH_KYEQ:       /* Search Key equal */
    case DK_SRCH_KYGT:       /* Search Key greater */
    case DK_SRCH_KYGE:       /* Search Key greater or equal */
         /* Check if at beginning of key */
         if (state == DK_POS_KEY && count == 0) {
             /* Check proper sequence */
                sim_debug(DEBUG_DETAIL, dptr, "search Key cn unit=%d %x %d %x %d %x\n",
                          unit, state, count, uptr->POS, data->rec, uptr->LASTCMD);
             if (uptr->LASTCMD == DK_RD_CNT || uptr->LASTCMD == 0x100
                 || ((uptr->LASTCMD & 0x1F) == 0x11 && data->rec != 0)
                 || ((uptr->LASTCMD & 0x1F) == 0x11 && /* Search ID */
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK))  {
                uptr->CMD &= ~(DK_SRCOK|DK_SHORTSRC|DK_NOEQ|DK_HIGH);
                uptr->CMD |= DK_PARAM;
             }
         }
         /* Check if previous record had zero length key */
         if (state == DK_POS_DATA && count == 0 && data->klen == 0) {
             if (uptr->LASTCMD == DK_RD_CNT || ((uptr->LASTCMD & 0x1F) == 0x11 &&
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK ))  {
                sim_debug(DEBUG_DETAIL, dptr, "search Key da unit=%d %x %d %x %d\n",
                          unit, state, count, uptr->POS, data->rec);
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND);
                 break;
             }
         }
         /* If we hit address mark, see if over */
         if (state == DK_POS_AM) {
             if (uptr->LASTCMD == DK_RD_CNT || ((uptr->LASTCMD & 0x1F) == 0x11 &&
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK ))  {
                sim_debug(DEBUG_DETAIL, dptr, "search Key am unit=%d %x %d %x %d\n",
                          unit, state, count, uptr->POS, data->rec);
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND);
                 break;
             } else {
                 uptr->LASTCMD = 0x100;
             }
         }
         if (uptr->CMD & DK_PARAM) {
         /* Wait for key */
             if (chan_read_byte(&ch)) {
                  uptr->CMD |= DK_SHORTSRC;
             } else if (ch != *da) {
                  if ((uptr->CMD & DK_NOEQ) == 0) {
                      uptr->CMD |= DK_NOEQ;
                      if (ch < *da)
                         uptr->CMD |= DK_HIGH;
                  }
             }
             sim_debug(DEBUG_DETAIL, dptr,
                  "search Key unit=%d %d %x %02x=%02x %d %c %c\n", unit, count,
                      state, ch, *da, data->tpos,
                         ((uptr->CMD & DK_NOEQ) ? '!' : '='),
                         ((uptr->CMD & DK_HIGH) ? 'h' : 'l'));
             if (count == data->klen-1 || uptr->CMD & DK_SHORTSRC) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x20) && (uptr->CMD & DK_NOEQ) == 0)
                     i = SNS_SMS;
                 if ((cmd & 0x40) && (uptr->CMD & DK_HIGH))
                     i = SNS_SMS;
                 if (i) {
                     uptr->CMD |= DK_SRCOK;
                 }
                 chan_end(SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_RD_HA:           /* Read home address */
         /* Wait until next index pulse */
         if (state == DK_POS_INDEX) {
             uptr->CMD |= DK_PARAM;
         }

         /* Read while we are in the home address */
         if (uptr->CMD & DK_PARAM && state == DK_POS_HA) {
             ch = *da;
             if (chan_write_byte(&ch) || count == 4) {
                uptr->LASTCMD = cmd;
                uptr->CMD &= ~(0xff);
                chan_end(SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_RD_IPL:          /* Read IPL record */

         /* If we are not on cylinder zero, issue a seek */
         if (uptr->POS != 0) {
             /* Do a seek */
             uptr->POS = 0;
             data->tstart = 0;
             data->state = DK_POS_SEEK;
             sim_debug(DEBUG_DETAIL, dptr, "RD IPL unit=%d seek\n", unit);
             break;
         }

         /* Wait for seek to finish */
         if (data->cyl != 0)
             break;

         /* Read in the first record on track zero */
         if (count == 0 && state == DK_POS_DATA && data->rec == 1) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD IPL unit=%d %d k=%d d=%d %02x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_R0:           /* Read R0 */
         /* Wait for record zero count */
         if (count == 0 && state == DK_POS_CNT && data->rec == 0) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD R0 unit=%d %d k=%d d=%d %02x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_CKD:          /* Read count, key and data */
         /* Wait for any count */
         if (count == 0 && state == DK_POS_CNT && data->rec != 0) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD CKD unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_KD:           /* Read key and data */
         /* Wait for next key */
         if (count == 0 && ((data->klen != 0 && state == DK_POS_KEY) ||
                            (data->klen == 0 && state == DK_POS_DATA))) {
             if ((uptr->CMD & DK_INDEX) && data->rec == 0 &&
                 (uptr->CMD & DK_SRCOK) == 0)
                 break;
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD KD unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_D:            /* Read Data */
         /* Wait for next data */
         if (count == 0 && state == DK_POS_DATA) {
             if ((uptr->CMD & DK_INDEX) && data->rec == 0 &&
                 (uptr->CMD & DK_SRCOK) == 0)
                 break;
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr,
                 "RD D unit=%d %d k=%d d=%d %02x %04x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen, count);
         }

rd:
         if (uptr->CMD & DK_PARAM) {
             /* Check for end of file */
             if (state == DK_POS_DATA && data->dlen == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "RD EOF unit=%d %x %d %d d=%d\n",
                          unit, state, count, data->rec, data->dlen);
                uptr->CMD &= ~(0xff|DK_PARAM);
                chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                break;
             }
             if (state == DK_POS_INDEX) {
                 uptr->SENSE = SNS_TRKOVR << 8;
                 uptr->CMD &= ~(0xff|DK_PARAM);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (state == DK_POS_DATA && count == data->dlen) {
                 sim_debug(DEBUG_DETAIL, dptr,
                     "RD next unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                     unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
                 uptr->CMD &= ~(0xff|DK_PARAM);
                 chan_end(SNS_CHNEND|SNS_DEVEND);
                 break;
             }
             ch = *da;
             sim_debug(DEBUG_DATA, dptr, "RD Char %02x %02x %d %d\n",
                    ch, state, count, data->tpos);
             if (chan_write_byte(&ch)) {
                 sim_debug(DEBUG_DETAIL, dptr,
                     "RD next unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                     unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
                 uptr->CMD &= ~(0xff|DK_PARAM);
                 chan_end(SNS_CHNEND|SNS_DEVEND);
                 break;
             }
         }
         break;

    case DK_RD_SECT:         /* Read sector */
         /* Not valid for drives before 3330 */
         sim_debug(DEBUG_DETAIL, dptr, "readsector unit=%d\n", unit);
         if (disk_type[type].sen_cnt > 6) {
             ch = data->tpos / 110;
             if (chan_write_byte(&ch)) {
                 sim_debug(DEBUG_DETAIL, dptr, "readsector rdr\n");
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 uptr->SENSE |= SNS_CMDREJ;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             /* Nothing more to do */
             uptr->LASTCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(SNS_DEVEND|SNS_CHNEND);
             sim_debug(DEBUG_DETAIL, dptr, "readsector %02x\n", ch);
             break;
          }
          /* Otherwise report as invalid command */
          uptr->LASTCMD = 0;
          uptr->CMD &= ~(0xff);
          uptr->SENSE |= SNS_CMDREJ;
          chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
          break;

    case DK_WR_HA:           /* Write home address */
         /* Wait for index */
         if (state == DK_POS_INDEX) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->SENSE |= SNS_CMDREJ;
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }

             uptr->CMD |= DK_PARAM;
             break;
         }

         if (uptr->CMD & DK_PARAM) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "WR HA unit=%d %x %d %d\n", unit,
                         state, count, data->rec);
             if (chan_read_byte(&ch)) {
                 ch = 0;
             }
             *da = ch;
             uptr->CMD |= DK_CYL_DIRTY;
             if (count == 4) {
                  uptr->LASTCMD = cmd;
                  uptr->CMD &= ~(0xff|DK_PARAM);
                  chan_end(SNS_CHNEND|SNS_DEVEND);
                  for(i = 1; i < 9; i++)
                     da[i] = 0xff;
             }
         }
         break;

    case DK_WR_R0:           /* Write R0 */

         /* Wait for first record or end of disk */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec == 0 && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "WR R0 unit=%d %x %d\n", unit,
                   state, count);
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LASTCMD == DK_WR_HA ||
                (uptr->LASTCMD == DK_SRCH_HAEQ &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 data->tpos = data->rpos;
                 da = &data->cbuf[data->tpos + data->tstart];
                 data->tpos++;
                 state = data->state = DK_POS_CNT;
                 uptr->CMD |= DK_PARAM;
             } else {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_CKD:          /* Write count, key and data */
         /* Wait for next non-zero record, or end of disk */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec != 0 && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d %x %d\n", unit,
                      state, count);
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i == DK_MSK_INHWRT || i == DK_MSK_ALLWRU) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d mask\n", unit);
                 uptr->SENSE |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LASTCMD == DK_WR_R0 || uptr->LASTCMD == DK_WR_CKD ||
                ((uptr->LASTCMD & 0x7) == 1 && (uptr->LASTCMD & 0x60) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d ok\n", unit);
                 data->tpos = data->rpos;
                 da = &data->cbuf[data->tpos + data->tstart];
                 data->tpos++;
                 state = data->state = DK_POS_CNT;
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d seq\n", unit);
                 uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_KD:           /* Write key and data */
         /* Wait for beginning of next key */
         if (count == 0 && ((data->klen != 0 && state == DK_POS_KEY) ||
                            (data->klen == 0 && state == DK_POS_DATA))) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->LASTCMD & 0x13) == 0x11 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             sim_debug(DEBUG_DETAIL, dptr, "WR KD unit=%d %d k=%d d=%d %02x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen, count);
             } else {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_D:            /* Write Data */
         /* Wait for beginning of next data */
         if ((state == DK_POS_DATA) && count == 0) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->LASTCMD & 0x3) == 1 && (uptr->LASTCMD & 0xE0) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             sim_debug(DEBUG_DETAIL, dptr, "WR D unit=%d %d k=%d d=%d %02x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen, count);
             } else {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }

wrckd:
         if (uptr->CMD & DK_PARAM) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             if (state == DK_POS_INDEX) {
                 uptr->SENSE = SNS_TRKOVR << 8;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             } else if ((cmd == DK_WR_KD || cmd == DK_WR_D) && state == DK_POS_DATA
                   && data->dlen == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR EOF unit=%d %x %d %d d=%d\n",
                            unit, state, count, data->rec, data->dlen);
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                 uptr->LASTCMD = cmd;
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 break;
             } else if (state == DK_POS_DATA && data->count == data->dlen) {
                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                 if ((cmd & 0x10) != 0) {
                      for(i = 0; i < 8; i++)
                         da[i] = 0xff;
                 }
                 sim_debug(DEBUG_DETAIL, dptr, "WCKD end unit=%d %d %d %04x\n",
                          unit, data->tpos+8, count, data->tpos - data->rpos);
                 chan_end(SNS_CHNEND|SNS_DEVEND);
                 break;
             }
             if (uptr->CMD & DK_DONE || chan_read_byte(&ch)) {
                 ch = 0;
                 uptr->CMD |= DK_DONE;
             }
             sim_debug(DEBUG_DATA, dptr, "Char %02x, %02x %d %d\n", ch, state,
                   count, data->tpos);
             *da = ch;
             uptr->CMD |= DK_CYL_DIRTY;
             if (state == DK_POS_CNT && count == 7) {
                 data->klen = rec[5];
                 data->dlen = (rec[6] << 8) | rec[7];
                 sim_debug(DEBUG_DETAIL, dptr,
                     "WCKD count unit=%d %d k=%d d=%d %02x %04x\n",
                     unit, data->rec, data->klen, data->dlen, data->state,
                     8 + data->klen + data->dlen);
                 if (data->klen == 0)
                     data->state = DK_POS_DATA;
                 else
                     data->state = DK_POS_KEY;
                 data->count = 0;
             }
         }
         break;

    case DK_ERASE:           /* Erase to end of track */
         if ((state == DK_POS_AM || state == DK_POS_END) && data->count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "Erase unit=%d %d %d\n",
                     unit, data->rec, data->rpos);
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i == DK_MSK_INHWRT || i == DK_MSK_ALLWRU) {
                 uptr->SENSE |= SNS_CMDREJ;
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LASTCMD == DK_WR_R0 || uptr->LASTCMD == DK_WR_CKD ||
                ((uptr->LASTCMD & 0x3) == 1 && (uptr->LASTCMD & 0x70) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 state = data->state = DK_POS_END;
                 /* Write end mark */
                 for(i = 0; i < 8; i++)
                    rec[i] = 0xff;

                 uptr->LASTCMD = cmd;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_INDEX|DK_INDEX2);
                 uptr->CMD |= DK_CYL_DIRTY;
                 chan_end(SNS_CHNEND|SNS_DEVEND);
             } else {
                 uptr->SENSE |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LASTCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }

         break;

    case DK_WR_SCKD:         /* Write special count, key and data */
    default:
         sim_debug(DEBUG_DETAIL, dptr, "invalid command=%d %x\n", unit, cmd);
         uptr->SENSE |= SNS_CMDREJ;
         uptr->LASTCMD = 0;
         uptr->CMD &= ~(0xff);
         chan_end(SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         break;
    }
    if (state == data->state)
        data->count++;
    else
        data->count = 0;
    return SCPE_OK;
}

t_stat
pmp_reset(DEVICE * dptr)
{
    UNIT    *uptr = dptr->units;
    int     i;

    for (i = 0; i < NUM_UNITS_PMP; i++) {
           int      t = GET_TYPE(uptr->flags);
           uptr->capac = disk_type[t].bpt * disk_type[t].heads * disk_type[t].cyl;
           uptr++;
    }
    pmp_statusb = IDLE_CH;
    return SCPE_OK;
}

/*
 * Format the pack for WAITS. 22 128 word sectors per track. Or 576 bytes per sector.
 */
int
pmp_format(UNIT * uptr, int flag) {
    struct pmp_header  hdr;
    struct pmp_t       *data;
    uint16              addr = GET_UADDR(uptr->flags);
    int                 type = GET_TYPE(uptr->flags);
    int                 tsize;
    int                 cyl;
    int                 sector;
    int                 rec;
    int                 hd;
    uint32              pos;

    if (flag || get_yn("Initialize dasd? [Y] ", TRUE)) {
        memset(&hdr, 0, sizeof(struct pmp_header));
        memcpy(&hdr.devid[0], "CKD_P370", 8);
        hdr.heads = disk_type[type].heads;
        hdr.tracksize = (disk_type[type].bpt | 0x1ff) + 1;
        hdr.devtype = disk_type[type].dev_type;
        hdr.highcyl = disk_type[type].cyl;
        (void)sim_fseek(uptr->fileref, 0, SEEK_SET);
        sim_fwrite(&hdr, 1, sizeof(struct pmp_header), uptr->fileref);
        if ((data = (struct pmp_t *)calloc(1, sizeof(struct pmp_t))) == 0)
            return 1;
        uptr->DATAPTR = (void *)data;
        tsize = hdr.tracksize * hdr.heads;
        data->tsize = hdr.tracksize;
        if ((data->cbuf = (uint8 *)calloc(tsize, sizeof(uint8))) == 0)
            return 1;
        for (cyl = 0; cyl <= disk_type[type].cyl; cyl++) {
            pos = 0;
            for (hd = 0; hd < disk_type[type].heads; hd++) {
                uint32 cpos = pos;
                rec = 0;
                data->cbuf[pos++] = 0;            /* HA */
                data->cbuf[pos++] = (cyl >> 8);
                data->cbuf[pos++] = (cyl & 0xff);
                data->cbuf[pos++] = (hd >> 8);
                data->cbuf[pos++] = (hd & 0xff);
                data->cbuf[pos++] = (cyl >> 8);   /* R0 Rib block */
                data->cbuf[pos++] = (cyl & 0xff);
                data->cbuf[pos++] = (hd >> 8);
                data->cbuf[pos++] = (hd & 0xff);
                data->cbuf[pos++] = rec++;          /* Rec */
                data->cbuf[pos++] = 0;              /* keylen */
                data->cbuf[pos++] = 0;              /* dlen */
                data->cbuf[pos++] = 144;            /*  */
                pos += 144;
                for (sector = 0; sector < 17; sector++) {
                    data->cbuf[pos++] = (cyl >> 8);   /* R1 */
                    data->cbuf[pos++] = (cyl & 0xff);
                    data->cbuf[pos++] = (hd >> 8);
                    data->cbuf[pos++] = (hd & 0xff);
                    data->cbuf[pos++] = rec++;        /* Rec */
                    data->cbuf[pos++] = 0;            /* keylen */
                    data->cbuf[pos++] = 2;            /* dlen = 576 */
                    data->cbuf[pos++] = 0100;         /*  */
                    pos += 576;
                }
                data->cbuf[pos++] = 0xff;           /* End record */
                data->cbuf[pos++] = 0xff;
                data->cbuf[pos++] = 0xff;
                data->cbuf[pos++] = 0xff;
                if ((pos - cpos) > data->tsize) {
                    fprintf(stderr, "Overfull %d %d\n", pos-cpos, data->tsize);
                }
                pos = cpos + data->tsize;
            }
            sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
            if ((cyl % 10) == 0)
               fputc('.', stderr);
        }
        (void)sim_fseek(uptr->fileref, sizeof(struct pmp_header), SEEK_SET);
        (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
        data->cpos = sizeof(struct pmp_header);
        data->ccyl = 0;
        data->ccyl = 0;
        uptr->CMD |= DK_ATTN;
        pmp_statusb |= REQ_CH;
        sim_activate(uptr, 100);
        fputc('\n', stderr);
        fputc('\r', stderr);
        return 0;
    } else
        return 1;
}

t_stat
pmp_attach(UNIT * uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->flags);
    int                 flag = (sim_switches & SWMASK ('I')) != 0;
    t_stat              r;
    int                 i;
    struct pmp_header  hdr;
    struct pmp_t       *data;
    int                 tsize;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;

    if (sim_fread(&hdr, 1, sizeof(struct pmp_header), uptr->fileref) !=
          sizeof(struct pmp_header) || strncmp((CONST char *)&hdr.devid[0], "CKD_P370", 8) != 0 || flag) {
        if (pmp_format(uptr, flag)) {
            detach_unit(uptr);
            return SCPE_FMT;
        }
        return SCPE_OK;
    }

    sim_messagef(SCPE_OK, "Drive %03x=%d %d %02x %d\n\r",  addr,
             hdr.heads, hdr.tracksize, hdr.devtype, hdr.highcyl);
    for (i = 0; disk_type[i].name != 0; i++) {
         tsize = (disk_type[i].bpt | 0x1ff) + 1;
         if (hdr.devtype == disk_type[i].dev_type && hdr.tracksize == tsize &&
             hdr.heads == disk_type[i].heads && hdr.highcyl == disk_type[i].cyl) {
             if (GET_TYPE(uptr->flags) != i) {
                  /* Ask if we should change */
                  fprintf(stderr, "Wrong type %s\n\r", disk_type[i].name);
                  if (!get_yn("Update dasd type? [N] ", FALSE)) {
                      detach_unit(uptr);
                      return SCPE_FMT;
                  }
                  uptr->flags &= ~UNIT_TYPE;
                  uptr->flags |= SET_TYPE(i);
                  uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
             }
             break;
         }
    }
    if (disk_type[i].name == 0) {
         detach_unit(uptr);
         return SCPE_FMT;
    }
    if ((data = (struct pmp_t *)calloc(1, sizeof(struct pmp_t))) == 0)
        return 0;
    uptr->DATAPTR = (void *)data;
    tsize = hdr.tracksize * hdr.heads;
    data->tsize = hdr.tracksize;
    if ((data->cbuf = (uint8 *)calloc(tsize, sizeof(uint8))) == 0) {
        detach_unit(uptr);
        return SCPE_ARG;
    }
    (void)sim_fseek(uptr->fileref, sizeof(struct pmp_header), SEEK_SET);
    (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    data->cpos = sizeof(struct pmp_header);
    data->ccyl = 0;
    uptr->CMD |= DK_ATTN;
    pmp_statusb |= REQ_CH;
    sim_activate(uptr, 100);
    return SCPE_OK;
}

t_stat
pmp_detach(UNIT * uptr)
{
    struct pmp_t       *data = (struct pmp_t *)uptr->DATAPTR;
    int                 type = GET_TYPE(uptr->flags);
    uint16              addr = GET_UADDR(uptr->flags);
    int                 cmd = uptr->CMD & 0x7f;

    if (uptr->CMD & DK_CYL_DIRTY) {
        (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        (void)sim_fwrite(data->cbuf, 1,
               data->tsize * disk_type[type].heads, uptr->fileref);
        uptr->CMD &= ~DK_CYL_DIRTY;
    }
    if (cmd != 0)
         chan_end(SNS_CHNEND|SNS_DEVEND);
    sim_cancel(uptr);
    free(data->cbuf);
    free(data);
    uptr->DATAPTR = 0;
    uptr->CMD &= ~0xffff;
    return detach_unit(uptr);
}


/* Disk option setting commands */

t_stat
pmp_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 i;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    for (i = 0; disk_type[i].name != 0; i++) {
        if (strcmp(disk_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;
            uptr->flags |= SET_TYPE(i);
            uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat
pmp_get_type(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(disk_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* Sets the device onto a given channel */
t_stat
pmp_set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value             newdev;
    t_stat              r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;

    newdev = get_uint (cptr, 16, 0xff, &r);

    if (r != SCPE_OK)
        return r;


    /* Update device entry */
    uptr->flags &= ~UNIT_ADDR(0xff);
    uptr->flags |= UNIT_ADDR(newdev);
    fprintf(stderr, "Set dev %x\n\r", GET_UADDR(uptr->flags));
    return r;
}

t_stat
pmp_get_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    int                 addr;

    if (uptr == NULL)
        return SCPE_IERR;
    addr = GET_UADDR(uptr->flags);
    fprintf(st, "%02x", addr);
    return SCPE_OK;
}

t_stat
pmp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
    int i;
    fprintf (st, "PMP Disk File Controller\n\n");
    fprintf (st, "Use:\n\n");
    fprintf (st, "    sim> SET %sn TYPE=type\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; disk_type[i].name != 0; i++) {
        fprintf(st, "%s", disk_type[i].name);
        if (disk_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\n\n");
    for (i = 0; disk_type[i].name != 0; i++) {
        int32 size = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
        char  sm = 'K';
        size /= 1024;
        size = (10 * size) / 1024;
        fprintf(st, "      %-8s %4d.%1dMB\n", disk_type[i].name, size/10, size%10);
    }
    fprintf (st, "Attach command switches\n");
    fprintf (st, "    -I          Initialize the drive. No prompting.\n");
    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    return SCPE_OK;
}

const char *pmp_description (DEVICE *dptr)
{
    return "PMP disk file controller";
}

#endif

