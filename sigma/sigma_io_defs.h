/* sigma_io_defs.h: XDS Sigma I/O device simulator definitions

   Copyright (c) 2007-2008, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#ifndef SIGMA_IO_DEFS_H_
#define SIGMA_IO_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */
#include "sigma_defs.h"

/* Channel constants */

#define CHAN_N_CHAN         8                           /* max # channels */
#define CHAN_DFLT           4                           /* default # chan */
#define CHAN_N_DEV          32                          /* max dev per chan */
#define CHAN_V_IOPT         (DEV_V_UF + 0)              /* channel type */
#define CHAN_MIOP           (0 << CHAN_V_IOPT)
#define CHAN_SIOP           (1 << CHAN_V_IOPT)

/* I/O device definition block */

typedef struct {
    uint32      dva;                                    /* dev addr (chan+dev) */
    uint32      (*disp)(uint32 op, uint32 dva, uint32 *dvst);
    uint32      dio;                                    /* dev addr (direct IO) */
    uint32      (*dio_disp)(uint32 op, uint32 rn, uint32 dva);
    } dib_t;

/* Channel data structure */

typedef struct {
    uint32      clc[CHAN_N_DEV];                        /* location counter */
    uint32      ba[CHAN_N_DEV];                         /* mem addr */
    uint16      bc[CHAN_N_DEV];                         /* byte count */
    uint8       cmd[CHAN_N_DEV];                        /* command */
    uint8       cmf[CHAN_N_DEV];                        /* command flags */
    uint16      chf[CHAN_N_DEV];                        /* channel flags */
    uint8       chi[CHAN_N_DEV];                        /* interrupts */
    uint8       chsf[CHAN_N_DEV];                       /* simulator flags */
    uint32      (*disp[CHAN_N_DEV])(uint32 op, uint32 dva, uint32 *dvst);
    } chan_t;

/* Channel command words */

#define CCW1_V_CMD      24                              /* command */
#define CCW1_M_CMD      0xFF
#define CCW1_V_BA       0
#define CCW1_M_BA       ((cpu_tab[cpu_model].pamask << 2) | 0x3)
#define CHBA_MASK       (CCW1_M_BA << CCW1_V_BA)
#define CCW2_V_CMF      24                              /* cmd flags */
#define CCW2_M_CMF      0xFF
#define CCW2_V_BC       0
#define CCW2_M_BC       0xFFFF
#define CHBC_MASK       (CCW2_M_BC << CCW2_V_BC)
#define CCW1_GETCMD(x)  (((x) >> CCW1_V_CMD) & CCW1_M_CMD)
#define CCW1_GETBA(x)   (((x) >> CCW1_V_BA) & CCW1_M_BA)
#define CCW2_GETCMF(x)  (((x) >> CCW2_V_CMF) & CCW2_M_CMF)
#define CCW2_GETBC(x)   (((x) >> CCW2_V_BC) & CCW2_M_BC)

/* Channel commands */

#define CMD_TIC         0x8                             /* transfer */

/* Channel command flags */

#define CMF_DCH         0x80                            /* data chain */
#define CMF_IZC         0x40                            /* int on zero cnt */
#define CMF_CCH         0x20                            /* command chain */
#define CMF_ICE         0x10                            /* int on chan end */
#define CMF_HTE         0x08                            /* hlt on xmit err */
#define CMF_IUE         0x04                            /* int on uend */
#define CMF_SIL         0x02                            /* suppress lnt err */
#define CMF_SKP         0x01                            /* skip */

/* Channel flags */

#define CHF_INP         0x8000                          /* int pending */
#define CHF_UEN         0x0400                          /* unusual end */
#define CHF_LNTE        0x0080                          /* length error */
#define CHF_XMDE        0x0040                          /* xmit data error */
#define CHF_XMME        0x0020                          /* xmit mem error */
#define CHF_XMAE        0x0010                          /* xmit addr error */
#define CHF_IOME        0x0008                          /* IOP mem error */
#define CHF_IOCE        0x0004                          /* IOP ctrl error */
#define CHF_IOHE        0x0002                          /* IOP halted */
#define CHF_ALL         (CHF_INP|CHF_UEN|0xFF)

/* Channel interrupts */

#define CHI_F_SHF       1                               /* flag shift */
#define CHI_CTL         (0x40 << CHI_F_SHF)             /* ctl int (fake) */
#define CHI_ZBC         (0x20 << CHI_F_SHF)             /* zero by cnt int */
#define CHI_END         (0x10 << CHI_F_SHF)             /* channel end int */
#define CHI_UEN         (0x08 << CHI_F_SHF)             /* unusual end int */
#define CHI_FLAGS       (CHI_ZBC|CHI_END|CHI_UEN)
#define CHI_V_UN        0
#define CHI_M_UN        0xF
#define CHI_GETUN(x)    (((x) >> CHI_V_UN) & CHI_M_UN)
#define CHI_GETINT(x)   (((x) & CHI_FLAGS) >> CHI_F_SHF)

/* Internal simulator flags */

#define CHSF_ACT        0x0001                          /* channel active */
#define CHSF_MU         0x0002                          /* multi-unit dev */

/* Dispatch routine status return value */

#define DVT_V_UN        24                              /* unit addr (AIO only) */
#define DVT_M_UN        0xF
#define DVT_V_CC        16                              /* cond codes */
#define DVT_M_CC        0xF
#define DVT_V_DVS       0                               /* device status */
#define DVT_M_DVS       0xFF
#define DVS_V_DST       5                               /* device status */
#define DVS_M_DST       0x3
#define DVS_DST         (DVS_M_DST << DVS_V_DST)
#define DVS_DOFFL       (0x1 << DVS_V_DST)
#define DVS_DBUSY       (0x3 << DVS_V_DST)
#define DVS_AUTO        0x10                            /* manual/automatic */
#define DVS_V_CST       1                               /* ctrl status */
#define DVS_M_CST       0x3
#define DVS_CBUSY       (0x3 << DVS_V_CST)
#define DVS_CST         (DVS_M_CST << DVS_V_CST)
#define DVT_GETUN(x)    (((x) >> DVT_V_UN) & DVT_M_UN)
#define DVT_GETCC(x)    (((x) >> DVT_V_CC) & DVT_M_CC)
#define DVT_GETDVS(x)   (((x) >> DVT_V_DVS) & DVT_M_DVS)
#define DVT_NOST        (CC1 << DVT_V_CC)               /* no status */
#define DVT_NODEV       ((CC1|CC2) < DVT_V_CC)          /* no device */

/* Read and write direct address format */

#define DIO_V_MOD       12                              /* mode */
#define DIO_M_MOD       0xF
#define DIO_V_0FNC      0                               /* mode 0 func */
#define DIO_M_0FNC      0xFFF
#define DIO_V_1FNC      8                               /* mode 1 int func */
#define DIO_M_1FNC      0x7
#define DIO_V_1GRP      0                               /* int group */
#define DIO_M_1GRP      0xF
#define DIO_GETMOD(x)   (((x) >> DIO_V_MOD) & DIO_M_MOD)
#define DIO_GET0FNC(x)  (((x) >> DIO_V_0FNC) & DIO_M_0FNC)
#define DIO_GET1FNC(x)  (((x) >> DIO_V_1FNC) & DIO_M_1FNC)
#define DIO_GET1GRP(x)  (((x) >> DIO_V_1GRP) & DIO_M_1GRP)
#define DIO_N_MOD       (DIO_M_MOD + 1)                 /* # DIO "modes" */

/* I/O instruction address format */

#define DVA_V_CHAN      8                               /* channel */
#define DVA_M_CHAN      (CHAN_N_CHAN - 1)
#define DVA_CHAN        (DVA_M_CHAN << DVA_V_CHAN)
#define DVA_V_DEVSU     0                               /* dev, 1 unit */
#define DVA_M_DEVSU     0x7F
#define DVA_DEVSU       (DVA_M_DEVSU << DVA_V_DEVSU)
#define DVA_MU          0x80                            /* multi-unit flg */
#define DVA_V_DEVMU     4                               /* dev, multi */
#define DVA_M_DEVMU     0x7
#define DVA_DEVMU       (DVA_M_DEVMU << DVA_V_DEVMU)
#define DVA_V_UNIT      0                               /* unit number */
#define DVA_M_UNIT      0xF
#define DVA_GETCHAN(x)  (((x) >> DVA_V_CHAN) & DVA_M_CHAN)
#define DVA_GETDEV(x)   (((x) & DVA_MU)? \
                         (((x) >> DVA_V_DEVMU) & DVA_M_DEVMU): \
                         (((x) >> DVA_V_DEVSU) & DVA_M_DEVSU))
#define DVA_GETUNIT(x)  (((x) & DVA_MU)? \
                         (((x) >> DVA_V_UNIT) & DVA_M_UNIT): 0)

/* Default I/O device addresses */

#define DVA_TT          0x001
#define DVA_LP          0x002
#define DVA_CR          0x003
#define DVA_CP          0x004
#define DVA_PT          0x005
#define DVA_MUX         0x006
#define DIO_MUX         0x3
#define DVA_MT          0x080
#define DVA_RAD         0x180
#define DVA_DK          0x190
#define DVA_DPA         0x280
#define DVA_DPB         0x380

/* Channel routine status codes */

#define CHS_ERR         0x4000                          /* any error */
#define CHS_INF         0x8000                          /* information */
#define CHS_IFERR(x)    (((x) != 0) && ((x) < CHS_INF))

#define CHS_INACTV      (CHS_ERR + 0)
#define CHS_NXM         (CHS_ERR + 1)
#define CHS_SEQ         (CHS_ERR + 2)

#define CHS_ZBC         (CHS_INF + 1)                   /* zero byte count */
#define CHS_CCH         (CHS_INF + 2)                   /* command chain */

/* Boot ROM */

#define BOOT_SA         0x20
#define BOOT_LNT        12
#define BOOT_DEV        0x25
#define BOOT_PC         0x26

/* Internal real-time scheduler */

#define RTC_C1          0
#define RTC_C2          1
#define RTC_C3          2
#define RTC_C4          3
#define RTC_NUM_CNTRS   4
#define RTC_TTI         (RTC_NUM_CNTRS + 0)
#define RTC_COC         (RTC_NUM_CNTRS + 1)
#define RTC_ALARM       (RTC_NUM_CNTRS + 2)
#define RTC_NUM_EVNTS   (RTC_NUM_CNTRS + 3)

#define RTC_HZ_OFF      0
#define RTC_HZ_500      1
#define RTC_HZ_50       2
#define RTC_HZ_60       3
#define RTC_HZ_100      4
#define RTC_HZ_2        5
#define RTC_NUM_HZ      6

/* Function prototypes */

uint32 chan_get_cmd (uint32 dva, uint32 *cmd);
uint32 chan_set_chf (uint32 dva, uint32 fl);
t_bool chan_tst_cmf (uint32 dva, uint32 fl);
void chan_set_chi (uint32 dva, uint32 fl);
void chan_set_dvi (uint32 dva);
int32 chan_clr_chi (uint32 dva);
int32 chan_chk_chi (uint32 dva);
uint32 chan_end (uint32 dva);
uint32 chan_uen (uint32 dva);
uint32 chan_RdMemB (uint32 dva, uint32 *dat);
uint32 chan_WrMemB (uint32 dva, uint32 dat);
uint32 chan_WrMemBR (uint32 dva, uint32 dat);
uint32 chan_RdMemW (uint32 dva, uint32 *dat);
uint32 chan_WrMemW (uint32 dva, uint32 dat);
t_stat chan_reset_dev (uint32 dva);
void io_sclr_req (uint32 inum, uint32 val);
void io_sclr_arm (uint32 inum, uint32 val);
t_stat io_set_dvc (UNIT* uptr, int32 val, CONST char *cptr, void *desc);
t_stat io_show_dvc (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat io_set_dva (UNIT* uptr, int32 val, CONST char *cptr, void *desc);
t_stat io_show_dva (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat io_show_cst (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat io_boot (int32 u, DEVICE *dptr);

/* Internal real-time event scheduler */

t_stat rtc_set_tps (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rtc_show_tps (FILE *of, UNIT *uptr, int32 val, CONST void *desc);
t_stat rtc_register (uint32 tm, uint32 idx, UNIT *uptr);

#endif