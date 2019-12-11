/* i7000_defs.h: IBM 70xx simulator definitions

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

   Generic channel interface for all processors in IBM 700 and 7000 line.
*/

#ifndef _I7000_H_
#define _I7000_H_

#include "sim_defs.h"                                   /* simulator defns */

/* Definitions for each supported CPU */

#ifdef I701
#define NUM_CHAN        1
#define NUM_DEVS_CDR    1
#define NUM_DEVS_CDP    1
#define NUM_DEVS_LPR    1
#define NUM_DEVS_DR     1
#define NUM_DEVS_MT     0
#define MT_CHANNEL_ZERO
#define NUM_UNITS_MT    5
#define NUM_UNITS_DR    16
#define MAXMEMSIZE      2048
#define CHARSPERWORD    6
extern t_uint64         M[];
#endif
#ifdef I7010            /* Includes 1410 and 7010 */
#define NUM_CHAN        5
#define NUM_DEVS_CDR    1
#define NUM_DEVS_CDP    1
#define STACK_DEV       1
#define NUM_DEVS_LPR    1
#define NUM_DEVS_CON    1
#define NUM_DEVS_DSK    5
#define NUM_DEVS_COM    1
#define NUM_DEVS_MT     3
#define CHAN_CHUREC     1
#define NUM_UNITS_MT    10      /* A, B */
#define MAXMEMSIZE      (100000)
#define CHARSPERWORD    1
extern uint8            M[];
#endif
#ifdef I7030
/* Not yet */
#endif
#ifdef I7070            /* Includes 7070, 7074 */
#define NUM_CHAN        9
#define NUM_DEVS_CDR    1
#define NUM_DEVS_CDP    1
#define NUM_DEVS_LPR    1
#define NUM_DEVS_CON    1
#define NUM_DEVS_MT     3
#define NUM_DEVS_DSK    10
#define NUM_DEVS_HT     0
#define NUM_DEVS_COM    1
#define NUM_UNITS_HT    10
#define NUM_UNITS_MT    10      /* A, B */
#define NUM_DEVS_CHRON  1
#define MAXMEMSIZE      (30000)
#define CHARSPERWORD    5
extern t_uint64         M[];
#endif
#ifdef I7080            /* Includes 702, 705-i/ii, 705-iii, 7080 */
#define NUM_CHAN        11
#define NUM_DEVS_CDR    1
#define NUM_DEVS_CDP    1
#define NUM_DEVS_LPR    1
#define NUM_DEVS_CON    1
#define NUM_DEVS_MT     4
#define NUM_DEVS_CHRON  1
#define NUM_DEVS_DR     1
#define NUM_DEVS_DSK    5
#define NUM_DEVS_HT     0
#define NUM_DEVS_COM    1
#define NUM_UNITS_MT    10
#define MT_CHANNEL_ZERO
#define NUM_UNITS_HT    10
#define MAXMEMSIZE      (160000)
#define CHARSPERWORD    1
extern uint8            M[];
#endif
#ifdef I704             /* Special build for 704 only */
#define NUM_CHAN        1
#define NUM_DEVS_CDR    1
#define NUM_DEVS_CDP    1
#define NUM_DEVS_LPR    1
#define NUM_DEVS_DR     1
#define NUM_DEVS_MT     0
#define NUM_UNITS_MT    10
#define MT_CHANNEL_ZERO
#define NUM_UNITS_DR    16
#define MAXMEMSIZE      (32*1024)
#define CHARSPERWORD    6
extern t_uint64         M[];
#endif
#ifdef I7040            /* Includes 7040, 7044 */
/* Not yet */
#define NUM_CHAN        8
#define NUM_DEVS_CDP    2
#define NUM_DEVS_CDR    2
#define NUM_DEVS_LPR    2
#define NUM_DEVS_MT     4
#define NUM_DEVS_CHRON  1
#define NUM_DEVS_DSK    10
#define NUM_DEVS_COM    1
#define NUM_DEVS_HD     1
#define NUM_DEVS_HT     0
#define MT_CHANNEL_ZERO
#define NUM_UNITS_HT    10
#define NUM_UNITS_MT    10      /* A, B */
#define NUM_UNITS_HD    8
#define MAXMEMSIZE      (32*1024)
#define CHARSPERWORD    6
extern t_uint64         M[];
#endif
#ifdef I7090            /* Includes 704, 709, 7090, 7094 */
#define NUM_CHAN        9
#define NUM_DEVS_CDP    4
#define NUM_DEVS_CDR    4
#define NUM_DEVS_LPR    4
#define NUM_DEVS_MT     3
#define NUM_DEVS_CHRON  1
#define NUM_DEVS_DR     1
#define NUM_DEVS_DSK    10
#define NUM_DEVS_COM    1
#define NUM_DEVS_HD     1
#define NUM_DEVS_HT     0
#define MT_CHANNEL_ZERO
#define NUM_UNITS_HT    10
#define NUM_UNITS_MT    10      /* A, B */
#define NUM_UNITS_DR    16
#define NUM_UNITS_HD    8
#define MAXMEMSIZE      (64*1024)
#define CHARSPERWORD    6
/*#define EXTRA_SL    */  /* Remove comments to allow 4 extra sense lights */
/*#define EXTRA_SW    */  /* Remove comments to allow 6 extra switchs */
extern t_uint64         M[];
#endif

/* Simulation stop codes. */
#define STOP_IONRDY     1               /* I/O dev not ready */
#define STOP_HALT       2               /* HALT */
#define STOP_IBKPT      3               /* breakpoint */
#define STOP_UUO        4               /* invalid opcode */
#define STOP_INDLIM     5               /* indirect limit */
#define STOP_XECLIM     6               /* XEC limit */
#define STOP_IOCHECK    7               /* IOCHECK */
#define STOP_MMTRP      8               /* mm in trap */
#define STOP_INVLIN     9               /* 7750 invalid line number */
#define STOP_INVMSG    10               /* 7750 invalid message */
#define STOP_NOOFREE   11               /* 7750 No free output buffers */
#define STOP_NOIFREE   12               /* 7750 No free input buffers */
#define STOP_FIELD     13               /* Field overflow */
#define STOP_ACOFL     13               /* AC Overflow - 7080 */
#define STOP_SIGN      14               /* Sign change */
#define STOP_DIV       15               /* Divide error */
#define STOP_INDEX     16               /* 7070 Alpha index */
#define STOP_NOWM      17               /* Stop if no word mark found */
#define STOP_INVADDR   18               /* Stop on invalid address */
#define STOP_INVLEN    19               /* Invalid length instruction */
#define STOP_RECCHK    19               /* Record check - 7080 */
#define STOP_PROG      20               /* Program fault */
#define STOP_PROT      21               /* Protection fault */

/* Memory */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define MEMMASK         (MEMSIZE - 1)                   /* Memory bits */

/* Globally visible flags */
#define CHAN_PIO        (0)     /* Polled mode I/O */
#define CHAN_UREC       (1)     /* Unit record devices */
#define CHAN_7010       (1)     /* Channel type for 7010 */
#define CHAN_7604       (2)     /* 7070 tape controller */
#define CHAN_7607       (2)     /* Generic 7090 channel */
#define CHAN_7621       (2)     /* 7080 tape controller */
#define CHAN_7904       (3)     /* 7040 generic channel */
#define CHAN_7907       (3)     /* Disk/Hyper/7750 channel for 7070 */
#define CHAN_7908       (3)     /* Disk/Hyper/7750 channel for 7080 */
#define CHAN_7909       (3)     /* Disk/Hyper/7750 channel for 7090 */
#define CHAN_7289       (4)     /* Special CTSS device for 7090 */
#define CHAN_754        (4)     /* 705 tape controller */

#define CH_TYP_PIO      01      /* Can be on PIO channel */
#define CH_TYP_UREC     02      /* Can be on Unit record channel */
#define CH_TYP_76XX     04      /* Can be on 76xx Channel */
#define CH_TYP_79XX     010     /* Can be on a 79xx channel */
#define CH_TYP_SPEC     020     /* Special channel */
#define CH_TYP_754      020     /* 705 tape controller */

/* Device information block */
struct dib {
        uint8   ctype;                                  /* Type of channel */
        uint8   upc;                                    /* Units per channel */
        uint16  addr;                                   /* Unit address */
        uint16  mask;                                   /* Channel mask type */
        uint32  (*cmd)(UNIT *up, uint16 cmd, uint16 dev);/* Issue command. */
        void    (*ini)(UNIT *up, t_bool f);
};

typedef struct dib DIB;


/* Debuging controls */
#define DEBUG_CHAN      0x0000001       /* Show channel fetchs */
#define DEBUG_TRAP      0x0000002       /* Show CPU Traps */
#define DEBUG_CMD       0x0000004       /* Show device commands */
#define DEBUG_DATA      0x0000008       /* Show data transfers */
#define DEBUG_DETAIL    0x0000020       /* Show details */
#define DEBUG_EXP       0x0000040       /* Show error conditions */
#define DEBUG_SNS       0x0000080       /* Shows sense data for 7909 devs */
#define DEBUG_CTSS      0x0000100       /* Shows CTSS specail instructions */
#define DEBUG_PRIO      0x0000100       /* Debug Priority mode on 7010 */
#define DEBUG_PROT      0x0000200       /* Protection traps */

extern DEBTAB dev_debug[];
extern DEBTAB crd_debug[];

/* Channels */
#define CHAN_CHPIO      0               /* Puesdo access for 704 */
#ifndef CHAN_CHUREC
#define CHAN_CHUREC     0               /* Unit record devices */
#endif
#define CHAN_A          1
#define CHAN_B          2
#define CHAN_C          3
#define CHAN_D          4
#define CHAN_E          5
#define CHAN_F          6
#define CHAN_G          7
#define CHAN_H          8

#define UNIT_V_SELECT   (UNIT_V_UF + 9)              /* 9 */
#define UNIT_SELECT     (1 << UNIT_V_SELECT)
#define UNIT_V_CHAN     (UNIT_V_SELECT + 1)          /* 10 */
#define UNIT_CHAN       (017 << UNIT_V_CHAN)         /* 10-14 */
#define UNIT_S_CHAN(x)  (UNIT_CHAN & ((x) << UNIT_V_CHAN))
#define UNIT_G_CHAN(x)  ((UNIT_CHAN & (x)) >> UNIT_V_CHAN)
#define UNIT_V_LOCAL    (UNIT_V_UF + 0)              /* 0 */
#define DEV_BUF_NUM(x)  (((x) & 07) << DEV_V_UF)
#define GET_DEV_BUF(x)  (((x) >> DEV_V_UF) & 07)
#define UNIT_V_MODE     (UNIT_V_LOCAL + 1)           /* 1 */

/* Specific to channel devices */
#define UNIT_V_MODEL    (UNIT_V_UF + 0)
#define CHAN_MODEL      (07 << UNIT_V_MODEL)
#define CHAN_S_TYPE(x)  (CHAN_MODEL & ((x) << UNIT_V_MODEL))
#define CHAN_G_TYPE(x)  ((CHAN_MODEL & (x)) >> UNIT_V_MODEL)
#define UNIT_V_AUTO     (UNIT_V_UF + 4)
#define CHAN_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_V_SET      (UNIT_V_UF + 5)
#define CHAN_SET        (1 << UNIT_V_SET)

/* I/O routine functions */
/* Channel half of controls */
/* Channel status */
extern uint32   chan_flags[NUM_CHAN];           /* Channel flags */
extern const char *chname[11];                  /* Channel names */
extern int      num_devs[NUM_CHAN];             /* Number devices per channel*/
extern uint8    lpr_chan9[NUM_CHAN];
#ifdef I7010
extern uint8    lpr_chan12[NUM_CHAN];
#endif

/* Sense information for 7909 channels */
#define SNS_IOCHECK     0x00000400      /* IO Check */
#define SNS_SEQCHECK    0x00000200      /* Sequence check */
#define SNS_UEND        0x00000100      /* Unusual end */
#define SNS_ATTN1       0x00000080      /* Attention 1 */
#define SNS_ATTN2       0x00000040      /* Attention 2 */
#define SNS_ADCHECK     0x00000020      /* Adaptor check */
#define CTL_PREAD       0x00000010      /* Prepare to read */
#define CTL_PWRITE      0x00000008      /* Prepare to write */
#define CTL_READ        0x00000004      /* Read Status */
#define CTL_WRITE       0x00000002      /* Write Status */
#define SNS_IRQ         0x00000001      /* IRQ */
#define SNS_MASK        0x000007fe      /* Mask of sense codes */
#define SNS_IRQS        0x000007e0
#define SNS_IMSK        0x00000620      /* Non maskable irqs */
#define CTL_END         0x00000800      /* Transfer is done */
#define CTL_INHB        0x00001000      /* Interupts inhibited */
#define CTL_SEL         0x00002000      /* Device select */
#define CTL_SNS         0x00004000      /* Sense transfer */
#define CTL_CNTL        0x00008000      /* Control transfer */
/* Channel status infomation */
#define STA_PEND        0x00010000      /* Pending LCH instruct */
#define STA_ACTIVE      0x00020000      /* Channel active */
#define STA_WAIT        0x00040000      /* Channel waiting for EOR */
#define STA_START       0x00080000      /* Channel was started, but not reset */
#define STA_TWAIT       0x00100000      /* Channel waiting on IORT */
/* Device error controls */
#define CHS_EOT         0x00200000      /* Channel at EOT */
#define CHS_BOT         0x00400000      /* Channel at BOT */
#define CHS_EOF         0x00800000      /* Channel at EOF */
#define CHS_ERR         0x01000000      /* Channel has Error */
#define CHS_ATTN        0x02000000      /* Channel attention*/
/* Device half of controls */
#define DEV_SEL         0x04000000      /* Channel selected */
#define DEV_WRITE       0x08000000      /* Device is writing to memory */
#define DEV_FULL        0x10000000      /* Buffer full */
#define DEV_REOR        0x20000000      /* Device at End of Record */
#define DEV_DISCO       0x40000000      /* Channel is done with device */
#define DEV_WEOR        0x80000000      /* Channel wants EOR written */

/* Device status information stored in u5 */
#define URCSTA_EOF      0001    /* Hit end of file */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */
#define URCSTA_WDISCO   0020    /* Device is wait for disconnect */
#define URCSTA_READ     0040    /* Device is reading channel */
#define URCSTA_WRITE    0100    /* Device is reading channel */
#define URCSTA_INPUT    0200    /* Console fill buffer from keyboard */
#define URCSTA_ON       0200    /* 7090 Unit is on */
#define URCSTA_IDLE     0400    /* 7090 Unit is idle */
#define URCSTA_WMKS     0400    /* Printer print WM as 1 */
#define URCSTA_SKIPAFT  01000   /* Skip to line after printing next line */
#define URCSTA_NOXFER   01000   /* Don't set up to transfer after feed */
#define URCSTA_LOAD     01000   /* Load flag for 7070 card reader */
#define URCSTA_CMD      01000   /* 7090 Command recieved */

/* Boot from given device */
t_stat chan_boot(int32 unit_num, DEVICE *dptr);

/* Sets the device onto a given channel */
t_stat chan_set_devs(DEVICE *dptr);
t_stat set_chan(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat set_cchan(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat print_chan(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
t_stat get_chan(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
t_stat chan9_set_select(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat chan9_get_select(FILE *st, UNIT *uptr, int32 v, CONST void *desc);

/* Check channel for error */
int chan_error(int chan);

/* Check channel for flags, clear flags if set */
int chan_stat(int chan, uint32 flag);

/* Check channel for flags set */
int chan_test(int chan, uint32 flag);

/* Check channel is active */
int chan_active(int chan);

/* Check channel is selected */
int chan_select(int chan);

/* Channel data handling char at a time */
int chan_write_char(int chan, uint8 *data, int flags);
int chan_read_char(int chan, uint8 *data, int flags);

/* Flag end of file on channel */
void chan_set_eof(int chan);

/* Flag error on channel */
void chan_set_error(int chan);

/* Flag attention on channel */
void chan_set_attn(int chan);

/* Start a selection command on channel */
void chan_set_sel(int chan, int need);
void chan_clear_status(int chan);

/* Set or clear a flag */
void chan_set(int chan, uint32 flag);
void chan_clear(int chan, uint32 flag);

/* Channel 7909 special error posting */
void chan9_clear_error(int chan, int sel);
void chan9_set_attn(int chan, int sel);
void chan9_set_error(int chan, uint32 mask);

void chan_proc();

#ifdef I7010
/* Sets the device that will interrupt on the channel. */
t_stat set_urec(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat get_urec(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
/* Execute the next channel instruction. */
void chan_set_attn_urec(int chan, uint16 addr);
void chan_set_attn_inq(int chan);
void chan_clear_attn_inq(int chan);
#endif

#ifdef I7070
void chan_set_attn_a(int chan);
void chan_set_attn_b(int chan);
void chan_set_attn_inq(int chan);
void chan_clear_attn_inq(int chan);
void chan_set_load_mode(int chan);
#endif

#ifdef I7080
void chan_set_attn_inq(int chan);
void chan_clear_attn_inq(int chan);
#endif

/* Convert micro seconds to click ticks */
#define us_to_ticks(us) (((us) * 10) / cycle_time)

/* Returns from chan_read/chan_write */
#define DATA_OK         0       /* Data transfered ok */
#define TIME_ERROR      1       /* Channel did not transfer last operation */
#define END_RECORD      2       /* End of record */

/* Returns from device commands */
#define SCPE_BUSY       (1)     /* Device is active */
#define SCPE_NODEV      (2)     /* No device exists */

/* I/O Command codes */
#define IO_RDS  1       /* Read record */
#define IO_BSR  2       /* Backspace one record */
#define IO_BSF  3       /* Backspace one file */
#define IO_WRS  4       /* Write one record */
#define IO_WEF  5       /* Write eof */
#define IO_REW  6       /* Rewind */
#define IO_DRS  7       /* Set unit offline */
#define IO_SDL  8       /* Set density low */
#define IO_SDH  9       /* Set density high */
#define IO_RUN  10      /* Rewind and unload unit */
#define IO_TRS  11      /* Check it unit ready */
#define IO_CTL  12      /* Io control device specific */
#define IO_RDB  13      /* Read backwards */
#define IO_SKR  14      /* Skip record forward */
#define IO_ERG  15      /* Erase next records from tape */

/* Global device definitions */
#ifdef CPANEL
extern DEVICE       cp_dev;
#endif

#ifdef NUM_DEVS_TP
extern DIB          tp_dib;
extern uint32       tp_cmd(UNIT *, uint16, uint16);
extern DEVICE       tpa_dev;
#endif

#ifdef NUM_DEVS_CDR
extern DIB          cdr_dib;
extern DEVICE       cdr_dev;
extern uint32       cdr_cmd(UNIT *, uint16, uint16);
#endif

#ifdef NUM_DEVS_CDP
extern DIB          cdp_dib;
extern DEVICE       cdp_dev;
extern uint32       cdp_cmd(UNIT *, uint16, uint16);
extern void         cdp_ini(UNIT *, t_bool);
#endif

#ifdef STACK_DEV
extern DEVICE       stack_dev;
#endif

#ifdef NUM_DEVS_LPR
extern DIB          lpr_dib;
extern DEVICE       lpr_dev;
extern uint32       lpr_cmd(UNIT *, uint16, uint16);
extern void         lpr_ini(UNIT *, t_bool);
#endif

#ifdef NUM_DEVS_CON
extern DIB          con_dib;
extern DEVICE       con_dev;
extern uint32       con_cmd(UNIT *, uint16, uint16);
extern void         con_ini(UNIT *, t_bool);
#endif

#ifdef NUM_DEVS_CHRON
extern DIB         chron_dib;
extern DEVICE      chron_dev;
extern uint32      chron_cmd(UNIT *, uint16, uint16);
#endif

#ifdef NUM_DEVS_COM
extern uint32      com_cmd(UNIT *, uint16, uint16);
extern DIB         com_dib;
extern DEVICE      com_dev;
extern DEVICE      coml_dev;
#endif

#ifdef NUM_DEVS_DR
extern uint32      drm_cmd(UNIT *, uint16, uint16);
extern void        drm_ini(UNIT *, t_bool);
extern DIB         drm_dib;
extern DEVICE      drm_dev;
#endif

#ifdef NUM_DEVS_DSK
extern uint32      dsk_cmd(UNIT *, uint16, uint16);
extern void        dsk_ini(UNIT *, t_bool);
extern DIB         dsk_dib;
extern DEVICE      dsk_dev;
#endif

#ifdef NUM_DEVS_HD
extern uint32      hsdrm_cmd(UNIT *, uint16, uint16);
extern void        hsdrm_ini(UNIT *, t_bool);
extern DIB         hsdrm_dib;
extern DEVICE      hsdrm_dev;
#endif

#ifdef NUM_DEVS_HT
extern DIB         ht_dib;
extern uint32      ht_cmd(UNIT *, uint16, uint16);
extern DEVICE      hta_dev;
#if NUM_DEVS_HT > 1
extern DEVICE      htb_dev;
#endif
#endif

#if (NUM_DEVS_MT > 0) || defined(MT_CHANNEL_ZERO)
extern DIB         mt_dib;
extern uint32      mt_cmd(UNIT *, uint16, uint16);
extern void        mt_ini(UNIT *, t_bool);
#ifdef MT_CHANNEL_ZERO
extern DEVICE      mtz_dev;
#endif
#if NUM_DEVS_MT > 0
extern DEVICE      mta_dev;
#if NUM_DEVS_MT > 1
extern DEVICE      mtb_dev;
#if NUM_DEVS_MT > 2
extern DEVICE      mtc_dev;
#if NUM_DEVS_MT > 3
extern DEVICE      mtd_dev;
#if NUM_DEVS_MT > 4
extern DEVICE      mte_dev;
#if NUM_DEVS_MT > 5
extern DEVICE      mtf_dev;
#endif  /* 5 */
#endif  /* 4 */
#endif  /* 3 */
#endif  /* 2 */
#endif  /* 1 */
#endif  /* 0 */
#endif  /* NUM_DEVS_MT */

/* Character codes */
#define CHR_ABLANK      000
#define CHR_MARK        CHR_ABLANK
#define CHR_1           001
#define CHR_2           002
#define CHR_3           003
#define CHR_4           004
#define CHR_5           005
#define CHR_6           006
#define CHR_7           007
#define CHR_8           010
#define CHR_9           011
#define CHR_0           012
#define CHR_EQ          013
#define CHR_QUOT        014     /* Also @ */
#define CHR_COL         015
#define CHR_GT          016
#define CHR_TRM         017
#define CHR_BLANK       020
#define CHR_SLSH        021
#define CHR_S           022
#define CHR_T           023
#define CHR_U           024
#define CHR_V           025
#define CHR_W           026
#define CHR_X           027
#define CHR_Y           030
#define CHR_Z           031
#define CHR_RM          032
#define CHR_COM         033
#define CHR_RPARN       034     /* Also % */
#define CHR_WM          035
#define CHR_BSLSH       036
#define CHR_UND         037
#define CHR_MINUS       040
#define CHR_J           041
#define CHR_K           042
#define CHR_L           043
#define CHR_M           044
#define CHR_N           045
#define CHR_O           046
#define CHR_P           047
#define CHR_Q           050
#define CHR_R           051
#define CHR_EXPL        052
#define CHR_DOL         053
#define CHR_STAR        054
#define CHR_LBRK        055
#define CHR_SEMI        056
#define CHR_CART        057
#define CHR_PLUS        060
#define CHR_A           061
#define CHR_B           062
#define CHR_C           063
#define CHR_D           064
#define CHR_E           065
#define CHR_F           066
#define CHR_G           067
#define CHR_H           070
#define CHR_I           071
#define CHR_QUEST       072
#define CHR_DOT         073
#define CHR_LPARN       074     /* Also Square */
#define CHR_RBRAK       075
#define CHR_LESS        076
#define CHR_GM          077

/* Generic devices common to all */
extern DEVICE      cpu_dev;
extern UNIT        cpu_unit;
extern DEVICE      chan_dev;
extern UNIT        chan_unit[NUM_CHAN];
extern REG         cpu_reg[];
extern int         cycle_time;

extern const char mem_to_ascii[64];

extern const char *cpu_description(DEVICE *dptr);
extern const char *chan_type_name[];
extern void help_set_chan_type(FILE *st, DEVICE *dptr, const char *name);

#endif /* _I7000_H_ */
