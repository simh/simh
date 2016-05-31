/* nova_dkp.c: NOVA moving head disk simulator

   Copyright (c) 1993-2008, Robert M. Supnik

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

   dkp          moving head disk

   27-Apr-12    RMS     Changed ??? string digraphs to ?, per C rules
   04-Jul-04    BKR     device name changed to DG's DKP from DEC's DP,
                        DEV_SET/CLR/INTR macro use started,
                        fixed 'P' pulse code and secret quirks,
                        added 6097 diag and size support,
                        fixed losing unit drive type during unit change,
                        tightened sector size determination calculations,
                        controller DONE flag handling fixed,
                        fixed cylinder overflow test error,
                        seek error code fixed,
                        restructured dkp_go() and dkp_svc() routines
                        (for known future fixes needed),
                        fixed DIA status calculation,
                        fixed DKP read/write loop to properly emulate DG cylinder and sector overflows,
                        added trace facility,
                        changed 'stime' calculation to force delay time if no cylinders are crossed
                        (this fixes some DG code that assumes disk seek takes some time),
                        fixed boot code to match DG hardware standard
   04-Jan-04    RMS     Changed attach routine to use sim_fsize
   28-Nov-03    CEO     Boot from DP now puts device address in SR
   24-Nov-03    CEO     Added support for disk sizing on 6099/6103
   19-Nov-03    CEO     Corrected major DMA Mapping bug
   25-Apr-03    RMS     Revised autosizing
   08-Oct-02    RMS     Added DIB
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Changed FLG, CAPAC to arrays
   26-Apr-01    RMS     Added device enable/disable support
   12-Dec-00    RMS     Added Eclipse support from Charles Owen
   15-Oct-00    RMS     Editorial changes
   14-Apr-99    RMS     Changed t_addr to unsigned
   15-Sep-97    RMS     Fixed bug in DIB/DOB for new disks
   15-Sep-97    RMS     Fixed bug in cylinder extraction (found by Charles Owen)
   10-Sep-97    RMS     Fixed bug in error reporting (found by Charles Owen)
   25-Nov-96    RMS     Defaulted to autosize
   29-Jun-96    RMS     Added unit disable support
*/

#include "nova_defs.h"

#define DKP_NUMDR       4                               /* #drives */
#define DKP_NUMWD       256                             /* words/sector */
#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    017
#define UNIT_V_AUTO     (UNIT_V_UF + 5)                 /* autosize */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define FUNC            u3                              /* function */
#define CYL             u4                              /* on cylinder */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Unit, surface, sector, count register

   Original format: 2b, 6b, 4b, 4b
   Revised format:  2b, 5b, 5b, 4b
*/

#define USSC_V_COUNT    0                               /* count */
#define USSC_M_COUNT    017
#define USSC_V_OSECTOR  4                               /* old: sector */
#define USSC_M_OSECTOR  017
#define USSC_V_OSURFACE 8                               /* old: surface */
#define USSC_M_OSURFACE 077
#define USSC_V_NSECTOR  4                               /* new: sector */
#define USSC_M_NSECTOR  037
#define USSC_V_NSURFACE 9                               /* new: surface */
#define USSC_M_NSURFACE 037
#define USSC_V_UNIT     14                              /* unit */
#define USSC_M_UNIT     03
#define USSC_UNIT       (USSC_M_UNIT << USSC_V_UNIT)
#define GET_COUNT(x)    (((x) >> USSC_V_COUNT) & USSC_M_COUNT)
#define GET_SECT(x,dt)  ((drv_tab[dt].newf)? \
                        (((x) >> USSC_V_NSECTOR) & USSC_M_NSECTOR): \
                        (((x) >> USSC_V_OSECTOR) & USSC_M_OSECTOR) )
#define GET_SURF(x,dt)  ((drv_tab[dt].newf)? \
                        (((x) >> USSC_V_NSURFACE) & USSC_M_NSURFACE): \
                        (((x) >> USSC_V_OSURFACE) & USSC_M_OSURFACE) )
#define GET_UNIT(x)     (((x) >> USSC_V_UNIT) & USSC_M_UNIT)

/* Flags, command, cylinder register

   Original format: 5b, 2b, 1b + 8b (surrounding command)
   Revised format:  5b, 2b, 9b
*/

#define FCCY_V_OCYL     0                               /* old: cylinder */
#define FCCY_M_OCYL     0377
#define FCCY_V_OCMD     8                               /* old: command */
#define FCCY_M_OCMD     3
#define FCCY_V_OCEX     10                              /* old: cyl extend */
#define FCCY_OCEX       (1 << FCCY_V_OCEX)
#define FCCY_V_NCYL     0                               /* new: cylinder */
#define FCCY_M_NCYL     0777
#define FCCY_V_NCMD     9                               /* new: command */
#define FCCY_M_NCMD     3
#define  FCCY_READ      0
#define  FCCY_WRITE     1
#define  FCCY_SEEK      2
#define  FCCY_RECAL     3
#define FCCY_FLAGS      0174000                         /* flags */

#define GET_CMD(x,dt)   ((drv_tab[dt].newf)? \
                        (((x) >> FCCY_V_NCMD) & FCCY_M_NCMD): \
                        (((x) >> FCCY_V_OCMD) & FCCY_M_OCMD) )

#define SET_CMD(x,dt)   dkp_fccy = (dkp_fccy & ((drv_tab[dt].newf)? \
                            (FCCY_M_NCMD << FCCY_V_NCMD) : (FCCY_M_OCMD << FCCY_V_OCMD))) | \
                        ((drv_tab[dt].newf)? \
                        (((x) & FCCY_M_NCMD) << FCCY_V_NCMD): \
                        (((x) & FCCY_M_OCMD) << FCCY_V_OCMD) )

#define GET_CYL(x,dt)   ((drv_tab[dt].newf)? \
                        (((x) >> FCCY_V_NCYL) & FCCY_M_NCYL): \
                        ((((x) >> FCCY_V_OCYL) & FCCY_M_OCYL) | \
                        ((dt != TYPE_D44)? 0: \
                        (((x) & FCCY_OCEX) >> (FCCY_V_OCEX - FCCY_V_OCMD)))) )


        /*  (Warning: no sector or surface masking is done!)  */

#define DKP_UPDATE_USSC( type, count, surf, sect )                                      \
                dkp_ussc = (dkp_ussc & USSC_UNIT)                                       \
                        | ((dkp_ussc + count) & USSC_M_COUNT)                           \
                        | ((drv_tab[dtype].newf)?                                       \
                                ((surf << USSC_V_NSURFACE) | (sect << USSC_V_NSECTOR)): \
                                ((surf << USSC_V_OSURFACE) | (sect << USSC_V_OSECTOR))  \
                          );


/* Status */

#define STA_ERR         0000001                         /* error */
#define STA_DLT         0000002                         /* data late */
#define STA_CRC         0000004                         /* crc error */
#define STA_UNS         0000010                         /* unsafe */
#define STA_XCY         0000020                         /* cross cylinder */
#define STA_CYL         0000040                         /* nx cylinder */
#define STA_DRDY        0000100                         /* drive ready */
#define STA_SEEK3       0000200                         /* seeking unit 3 */
#define STA_SEEK2       0000400                         /* seeking unit 2 */
#define STA_SEEK1       0001000                         /* seeking unit 1 */
#define STA_SEEK0       0002000                         /* seeking unit 0 */
#define STA_SKDN3       0004000                         /* seek done unit 3 */
#define STA_SKDN2       0010000                         /* seek done unit 2 */
#define STA_SKDN1       0020000                         /* seek done unit 1 */
#define STA_SKDN0       0040000                         /* seek done unit 0 */
#define STA_DONE        0100000                         /* operation done */

#define STA_DYN         (STA_DRDY | STA_CYL)            /* set from unit */
#define STA_EFLGS       (STA_ERR | STA_DLT | STA_CRC | STA_UNS | \
                         STA_XCY | STA_CYL)             /* error flags */
#define STA_DFLGS       (STA_DONE | STA_SKDN0 | STA_SKDN1 | \
                         STA_SKDN2 | STA_SKDN3)         /* done flags */

#define GET_SA(cy,sf,sc,t) (((((cy)*drv_tab[t].surf)+(sf))* \
    drv_tab[t].sect)+(sc))

/* This controller supports many different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/     new format?
                 surface         cylinder        drive

   floppy       8               1               77              no
   DS/DD floppy 16              2               77              yes
   (6097 "quad floppy")
   Diablo 31    12              2               203             no
   6225         20              2               245             yes
   Century 111  6               10              203             no
   4048 (same as Century 111)
   Diablo 44    12              4               408             no
   6099         32              4               192             yes
   6227         20              6               245             yes
   6070         24              4               408             yes     
   Century 114  12              20              203             no
   4057 (same as Century 114)
   6103         32              8               192             yes
   4231         23              19              411             yes

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.
*/

#define TYPE_FLP        0
#define SECT_FLP        8
#define SURF_FLP        1
#define CYL_FLP         77
#define SIZE_FLP        (SECT_FLP * SURF_FLP * CYL_FLP * DKP_NUMWD)
#define NFMT_FLP        FALSE

#define TYPE_DSDD       1
#define TYPE_6097       TYPE_DSDD
#define SECT_DSDD       16
#define SURF_DSDD       2
#define CYL_DSDD        77
#define SIZE_DSDD       (SECT_DSDD * SURF_DSDD * CYL_DSDD * DKP_NUMWD)
#define NFMT_DSDD       TRUE

#define TYPE_D31        2
#define SECT_D31        12
#define SURF_D31        2
#define CYL_D31         203
#define SIZE_D31        (SECT_D31 * SURF_D31 * CYL_D31 * DKP_NUMWD)
#define NFMT_D31        FALSE

#define TYPE_6225       3
#define SECT_6225       20
#define SURF_6225       2
#define CYL_6225        245
#define SIZE_6225       (SECT_6225 * SURF_6225 * CYL_6225 * DKP_NUMWD)
#define NFMT_6225       TRUE

#define TYPE_C111       4
#define SECT_C111       6
#define SURF_C111       10
#define CYL_C111        203
#define SIZE_C111       (SECT_C111 * SURF_C111 * CYL_C111 * DKP_NUMWD)
#define NFMT_C111       FALSE

#define TYPE_D44        5
#define SECT_D44        12
#define SURF_D44        4
#define CYL_D44         408
#define SIZE_D44        (SECT_D44 * SURF_D44 * CYL_D44 * DKP_NUMWD)
#define NFMT_D44        FALSE

#define TYPE_6099       6
#define SECT_6099       32
#define SURF_6099       4
#define CYL_6099        192
#define SIZE_6099       (SECT_6099 * SURF_6099 * CYL_6099 * DKP_NUMWD)
#define NFMT_6099       TRUE    

#define TYPE_6227       7
#define SECT_6227       20
#define SURF_6227       6
#define CYL_6227        245
#define SIZE_6227       (SECT_6227 * SURF_6227 * CYL_6227 * DKP_NUMWD)
#define NFMT_6227       TRUE

#define TYPE_6070       8
#define SECT_6070       24
#define SURF_6070       4
#define CYL_6070        408
#define SIZE_6070       (SECT_6070 * SURF_6070 * CYL_6070 * DKP_NUMWD)
#define NFMT_6070       TRUE

#define TYPE_C114       9
#define SECT_C114       12
#define SURF_C114       20
#define CYL_C114        203
#define SIZE_C114       (SECT_C114 * SURF_C114 * CYL_C114 * DKP_NUMWD)
#define NFMT_C114       FALSE

#define TYPE_6103       10
#define SECT_6103       32
#define SURF_6103       8
#define CYL_6103        192
#define SIZE_6103       (SECT_6103 * SURF_6103 * CYL_6103 * DKP_NUMWD)
#define NFMT_6103       TRUE

#define TYPE_4231       11
#define SECT_4231       23
#define SURF_4231       19
#define CYL_4231        411
#define SIZE_4231       (SECT_4231 * SURF_4231 * CYL_4231 * DKP_NUMWD)
#define NFMT_4231       TRUE

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       newf;                                   /* new format flag */
    };

struct drvtyp drv_tab[] = {
    { SECT_FLP,  SURF_FLP,  CYL_FLP,  SIZE_FLP,  NFMT_FLP },
    { SECT_DSDD, SURF_DSDD, CYL_DSDD, SIZE_DSDD, NFMT_DSDD },
    { SECT_D31,  SURF_D31,  CYL_D31,  SIZE_D31,  NFMT_D31 },
    { SECT_6225, SURF_6225, CYL_6225, SIZE_6225, NFMT_6225 },
    { SECT_C111, SURF_C111, CYL_C111, SIZE_C111, NFMT_C111 },
    { SECT_D44,  SURF_D44,  CYL_D44,  SIZE_D44,  NFMT_D44 },
    { SECT_6099, SURF_6099, CYL_6099, SIZE_6099, NFMT_6099 },
    { SECT_6227, SURF_6227, CYL_6227, SIZE_6227, NFMT_6227 },
    { SECT_6070, SURF_6070, CYL_6070, SIZE_6070, NFMT_6070 },
    { SECT_C114, SURF_C114, CYL_C114, SIZE_C114, NFMT_C114 },
    { SECT_6103, SURF_6103, CYL_6103, SIZE_6103, NFMT_6103 },
    { SECT_4231, SURF_4231, CYL_4231, SIZE_4231, NFMT_4231 },
    { 0 }
    };

#define DKP_TRACE(x)    (dkp_trace & (1<<(x)))
#define DKP_TRACE_FP    stderr
/*  current trace bit use (bit 0 = LSB)
    0   I/O instructions
    1   pre-seek/read/write event setup
    2   seek events
    3   read/write events
    4   post read/write events
 */

extern uint16 M[];
extern UNIT cpu_unit;
extern int32 int_req, dev_busy, dev_done, dev_disable;
extern int32 saved_PC, SR, AMASK;

int32 dkp_ma = 0;                                       /* memory address */
int32 dkp_map = 0;                                      /* DCH map 0=A 3=B */
int32 dkp_ussc = 0;                                     /* unit/sf/sc/cnt */
int32 dkp_fccy = 0;                                     /* flags/cylinder */
int32 dkp_sta = 0;                                      /* status register */
int32 dkp_swait = 100;                                  /* seek latency */
int32 dkp_rwait = 100;                                  /* rotate latency */
int32 dkp_diagmode = 0;                                 /* diagnostic mode */

int32 dkp_trace = 0 ;

int32 dkp (int32 pulse, int32 code, int32 AC);
t_stat dkp_svc (UNIT *uptr);
t_stat dkp_reset (DEVICE *dptr);
t_stat dkp_boot (int32 unitno, DEVICE *dptr);
t_stat dkp_attach (UNIT *uptr, CONST char *cptr);
t_stat dkp_go ( int32 pulse );
t_stat dkp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* DKP data structures

   dkp_dev      DKP device descriptor
   dkp_unit     DKP unit list
   dkp_reg      DKP register list
   dkp_mod      DKP modifier list
*/

DIB dkp_dib = { DEV_DKP, INT_DKP, PI_DKP, &dkp };

UNIT dkp_unit[] = {
    { UDATA (&dkp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_D31 << UNIT_V_DTYPE), SIZE_D31) },
    { UDATA (&dkp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_D31 << UNIT_V_DTYPE), SIZE_D31) },
    { UDATA (&dkp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_D31 << UNIT_V_DTYPE), SIZE_D31) },
    { UDATA (&dkp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_D31 << UNIT_V_DTYPE), SIZE_D31) }
    };

REG dkp_reg[] = {
    { ORDATA (FCCY, dkp_fccy, 16) },
    { ORDATA (USSC, dkp_ussc, 16) },
    { ORDATA (STA, dkp_sta, 16) },
    { ORDATA (MA, dkp_ma, 16) },
    { FLDATA (INT, int_req, INT_V_DKP) },
    { FLDATA (BUSY, dev_busy, INT_V_DKP) },
    { FLDATA (DONE, dev_done, INT_V_DKP) },
    { FLDATA (DISABLE, dev_disable, INT_V_DKP) },
    { FLDATA (DIAG,  dkp_diagmode, 0) },
    { DRDATA (TRACE, dkp_trace,   32) },
    { ORDATA (MAP, dkp_map, 2) },
    { DRDATA (STIME, dkp_swait, 24), PV_LEFT },
    { DRDATA (RTIME, dkp_rwait, 24), PV_LEFT },
    { URDATA (CAPAC, dkp_unit[0].capac, 10, T_ADDR_W, 0,
              DKP_NUMDR, PV_LEFT | REG_HRO) },
    { NULL }
    };

MTAB dkp_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_FLP << UNIT_V_DTYPE) + UNIT_ATT,
      "6030 (floppy)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_DSDD << UNIT_V_DTYPE) + UNIT_ATT,
      "6097 (DS/DD floppy)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_D31 << UNIT_V_DTYPE) + UNIT_ATT,
      "4047 (Diablo 31)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_D44 << UNIT_V_DTYPE) + UNIT_ATT,
      "4234/6045 (Diablo 44)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_C111 << UNIT_V_DTYPE) + UNIT_ATT,
      "4048 (Century 111)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_C114 << UNIT_V_DTYPE) + UNIT_ATT,
      "2314/4057 (Century 114)", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6225 << UNIT_V_DTYPE) + UNIT_ATT,
      "6225", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6227 << UNIT_V_DTYPE) + UNIT_ATT,
      "6227", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6099 << UNIT_V_DTYPE) + UNIT_ATT,
      "6099", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6103 << UNIT_V_DTYPE) + UNIT_ATT,
      "6103", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6070 << UNIT_V_DTYPE) + UNIT_ATT,
      "6070", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_4231 << UNIT_V_DTYPE) + UNIT_ATT,
      "4231/3330", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_FLP << UNIT_V_DTYPE),
      "6030 (floppy)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_DSDD << UNIT_V_DTYPE),
      "6097 (DS/DD floppy)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_D31 << UNIT_V_DTYPE),
      "4047 (Diablo 31)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_D44 << UNIT_V_DTYPE),
      "4234/6045 (Diablo 44)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_C111 << UNIT_V_DTYPE),
      "4048 (Century 111)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_C114 << UNIT_V_DTYPE),
      "2314/4057 (Century 114)", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6225 << UNIT_V_DTYPE),
      "6225", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6227 << UNIT_V_DTYPE),
      "6227", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6099 << UNIT_V_DTYPE),
      "6099", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6103 << UNIT_V_DTYPE),
      "6103", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6070 << UNIT_V_DTYPE),
      "6070", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_4231 << UNIT_V_DTYPE),
      "4231/3330", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_FLP << UNIT_V_DTYPE),
      NULL, "FLOPPY", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_FLP << UNIT_V_DTYPE),
      NULL, "6030", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_DSDD << UNIT_V_DTYPE),
      NULL, "DSDDFLOPPY", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_DSDD << UNIT_V_DTYPE),
      NULL, "6097", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_D31 << UNIT_V_DTYPE),
      NULL, "D31", &dkp_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_D31 << UNIT_V_DTYPE),
      NULL, "4047", &dkp_set_size }, 
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_D44 << UNIT_V_DTYPE),
      NULL, "D44", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_D44 << UNIT_V_DTYPE),
      NULL, "4234", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_D44 << UNIT_V_DTYPE),
      NULL, "6045", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_C111 << UNIT_V_DTYPE),
      NULL, "C111", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_C111 << UNIT_V_DTYPE),
      NULL, "4048", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_C114 << UNIT_V_DTYPE),
      NULL, "C114", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_C114 << UNIT_V_DTYPE),
      NULL, "2314", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_C114 << UNIT_V_DTYPE),
      NULL, "4057", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6225 << UNIT_V_DTYPE),
      NULL, "6225", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6227 << UNIT_V_DTYPE),
      NULL, "6227", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6099 << UNIT_V_DTYPE),
      NULL, "6099", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6103 << UNIT_V_DTYPE),
      NULL, "6103", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6070 << UNIT_V_DTYPE),
      NULL, "6070", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_4231 << UNIT_V_DTYPE),
      NULL, "4231", &dkp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_4231 << UNIT_V_DTYPE),
      NULL, "3330", &dkp_set_size },
    { 0 }
    };

DEVICE dkp_dev = {
    "DKP", dkp_unit, dkp_reg, dkp_mod,
    DKP_NUMDR, 8, 30, 1, 8, 16,
    NULL, NULL, &dkp_reset,
    &dkp_boot, &dkp_attach, NULL,
    &dkp_dib, DEV_DISABLE
    };


/* IOT routine */

int32 dkp (int32 pulse, int32 code, int32 AC)
{
UNIT *uptr;
int32 u, rval, dtype;

rval = 0;
uptr = dkp_dev.units + GET_UNIT (dkp_ussc);             /* select unit */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */

if ( DKP_TRACE(0) )
    {
    static const char * f[8] =
        { "NIO", "DIA", "DOA", "DIB", "DOB", "DIC", "DOC", "SKP" } ;
    static const char * s[4] =
        { " ", "S", "C", "P" } ;

        printf( "  [DKP  %s%s %06o ", f[code & 0x07], s[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

switch (code) {                                         /* decode IR<5:7> */

    case ioDIA:                                         /* DIA */
        dkp_sta = dkp_sta & (~STA_DRDY) ;               /* keep error flags  */
        if (uptr->flags & UNIT_ATT)                     /* update ready */
            dkp_sta = dkp_sta | STA_DRDY;
        if (uptr->CYL >= drv_tab[dtype].cyl)
            dkp_sta = dkp_sta | STA_CYL;                /* bad cylinder? */
        if (dkp_sta & STA_EFLGS)
            dkp_sta = dkp_sta | STA_ERR;
        rval = dkp_sta;
        break;

    case ioDOA:                                         /* DOA */
        if (AC & 0100000)                               /* clear rw done? */
            dkp_sta = dkp_sta & ~(STA_CYL|STA_XCY|STA_UNS|STA_CRC);
        if ((dev_busy & INT_DKP) == 0) {
            dkp_fccy = AC;                              /* save cmd, cyl */
            dkp_sta = dkp_sta & ~(AC & FCCY_FLAGS);
            }
        DEV_CLR_DONE( INT_DKP );                        /* assume done flags 0 */
        if ( dkp_sta & STA_DFLGS )                      /* done flags = 0? */
            DEV_SET_DONE( INT_DKP )    ;                /* nope - set done  */
        DEV_UPDATE_INTR    ;                            /* update intr  */
        break;

    case ioDIB:                                         /* DIB */
        rval = dkp_ma & 077777 ;                        /* return buf addr */
        /* with B0 clear (no DCH B map support) */
        break;

    case ioDOB:                                         /* DOB */
        if ((dev_busy & INT_DKP) == 0) {
            dkp_ma = AC & (drv_tab[dtype].newf? DMASK: AMASK);
            if (AC & 0100000)
                dkp_map = 3;                            /* high bit is map */
            else
                dkp_map = 0;
        }
        break;

    case ioDIC:                                         /* DIC */
        rval = dkp_ussc;                                /* return unit, sect */
        break;

    case ioDOC:                                         /* DOC */
        if ((dev_busy & INT_DKP) == 0)                  /* if device is not busy */
        dkp_ussc = AC ;                                 /* save unit, sect */
        if (((dtype == TYPE_6099) ||                    /* (BKR: don't forget 6097) */
             (dtype == TYPE_6097) ||                    /* for 6099 and 6103 */
             (dtype == TYPE_6103)) &&                   /* if data<0> set, */
            (AC & 010000) )
        dkp_diagmode = 1;                                /* set diagnostic mode */
        break;
        }                                               /* end switch code */

u = GET_UNIT(dkp_ussc);                                 /* update current unit */
uptr = dkp_dev.units + u ;                                /* select unit */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */

if ( DKP_TRACE(0) )
    {
    if ( code & 1 )
        printf( "  [%06o]  ", (rval & 0xFFFF) ) ;
    printf( "]  \n" ) ;
    }

switch (pulse) {                                        /* decode IR<8:9> */

    case iopS:                                          /* start */
        DEV_SET_BUSY( INT_DKP ) ;                       /*  set busy    */
        DEV_CLR_DONE( INT_DKP ) ;                       /*  clear done  */
        DEV_UPDATE_INTR ;                               /*  update ints */
        if (dkp_diagmode) {                             /* in diagnostic mode? */
            dkp_diagmode = 0;                           /* reset it     */
            if (dtype == TYPE_6097)                     /* (BKR - quad floppy) */
                dkp_ussc = 010001;
            if (dtype == TYPE_6099)                     /* return size bits */
                dkp_ussc = 010002;
            if (dtype == TYPE_6103)                     /* for certain types */
                dkp_ussc = 010003;
            } 
        else {                                          /* normal mode ... */
            if (dkp_go (pulse))                         /* do command    */
                break ;                                 /* break if no error  */
            }
        DEV_CLR_BUSY( INT_DKP ) ;                       /*  clear busy  */
        DEV_SET_DONE( INT_DKP ) ;                       /*  set done    */
        DEV_UPDATE_INTR ;                               /*  update ints */
        dkp_sta = dkp_sta | STA_DONE;                   /*  set controller done  */
        break;

    case iopC:                                          /* clear */
        DEV_CLR_BUSY( INT_DKP ) ;                       /*  clear busy  */
        DEV_CLR_DONE( INT_DKP ) ;                       /*  set done    */
        DEV_UPDATE_INTR ;                               /*  update ints */
        dkp_sta = dkp_sta & ~(STA_DFLGS + STA_EFLGS);   /*  clear controller flags  */
        if (dkp_unit[u].FUNC != FCCY_SEEK)
            sim_cancel (&dkp_unit[u]);                  /*  cancel any r/w op  */
        break;

    case iopP:                                          /* pulse */
        if ( dkp_diagmode )
            {
            dkp_diagmode = 0 ;                          /*  clear DG diagnostic mode  */
            }
        else
            {
            DEV_CLR_DONE( INT_DKP ) ;                   /*  clear done  */
            DEV_UPDATE_INTR ;

            /*  DG "undocumented feature": 'P' pulse can not start a read/write operation!
             *  Diagnostic routines will use this crock to do 'crazy things' to size a disk
             *  and many assume that a recal is done, other assume that they can stop the
             *  read operation before any damage is done.  Must also [re]calculate unit, function
             *  and type because DOx instruction may have updated the controller info after
             *  start of this procedure and before our 'P' handler.   BKR
             */
            if (dkp_go(pulse))
                break;                                  /* no error - do not set done and status  */
            }

        DEV_SET_DONE( INT_DKP ) ;                       /* set done */
        DEV_UPDATE_INTR ;                               /* update ints */
        dkp_sta = dkp_sta | (STA_SKDN0 >> u);           /* set controller seek done */
        break;
    }                                                   /* end case pulse */

return rval;
}


/* New command, start vs pulse handled externally
   Returns true if command ok, false if error
*/

t_stat dkp_go ( int32 pulse )
{
UNIT *uptr;
int32 oldCyl, u, dtype;

dkp_sta = dkp_sta & ~STA_EFLGS;                         /* clear errors */
u = GET_UNIT (dkp_ussc);                                /* get unit number */
uptr = dkp_dev.units + u;                               /* get unit */
if (((uptr->flags & UNIT_ATT) == 0) || sim_is_active (uptr)) {
    dkp_sta = dkp_sta | STA_ERR;                        /* attached or busy? */
    return FALSE;
    }

if (dkp_diagmode) {                                     /* diagnostic mode? */
    dkp_sta = (dkp_sta | STA_DONE);                     /* Set error bit only */
    DEV_CLR_BUSY( INT_DKP ) ;                           /* clear busy  */
    DEV_SET_DONE( INT_DKP ) ;                           /* set   done  */
    DEV_UPDATE_INTR ;                                   /* update interrupts  */
    return ( TRUE ) ;                                   /* do not do function */
    }

oldCyl = uptr->CYL ;                                    /* get old cylinder  */
dtype  = GET_DTYPE (uptr->flags);                       /* get drive type */
uptr->FUNC = GET_CMD (dkp_fccy, dtype) ;                /* save command */
uptr->CYL  = GET_CYL (dkp_fccy, dtype) ;

if ( DKP_TRACE(1) )
    {
    int32        xSect ;
    int32        xSurf ;
    int32        xCyl ;
    int32        xCnt ;

    xSect = GET_SECT(dkp_ussc, dtype) ;
    xSurf = GET_SURF(dkp_ussc, dtype) ;
    xCyl  = GET_CYL (dkp_fccy, dtype) ;
    xCnt  = 16 - (GET_COUNT(dkp_ussc)) ;

    fprintf( DKP_TRACE_FP,
        "  [%s:%c  %-5s:  %3d / %2d / %2d   %2d   %06o ] \r\n",
        "DKP",
        (char) (u + '0'),
        ((uptr->FUNC == FCCY_READ) ?
              "read"
            : ((uptr->FUNC == FCCY_WRITE) ?
                  "write"
                : ((uptr->FUNC == FCCY_SEEK) ?
                      "seek"
                    : "<?>"
                  )
              )
        ),
        (unsigned) xCyl,
        (unsigned) xSurf,
        (unsigned) xSect,
        (unsigned) (16 - xCnt),
        (unsigned) (dkp_ma & 0xFFFF) /* show all 16-bits in case DCH B */
        ) ;
    }


switch (uptr->FUNC) {                                   /* decode command */

    case FCCY_READ:
    case FCCY_WRITE:
    if (((uptr->flags & UNIT_ATT) == 0) ||              /* not attached? */
        ((uptr->flags & UNIT_WPRT) && (uptr->FUNC == FCCY_WRITE)))
            {
            dkp_sta = dkp_sta | STA_DONE | STA_ERR;        /* error */
            }
    else if ( uptr->CYL  >= drv_tab[dtype].cyl )        /* bad cylinder */
        {
        dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_CYL ;
        }
    else if ( GET_SURF(dkp_ussc, dtype) >= drv_tab[dtype].surf ) /* bad surface */
        {
        dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_UNS;   /* older drives may not even do this... */
        /*    dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_XCY ;  /-  newer disks give this error  */
        }
    else if ( GET_SECT(dkp_ussc, dtype) >= drv_tab[dtype].sect ) /* or bad sector? */
        {
    /*  dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older drives may not even do this... */
        dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_XCY ;  /*  newer disks give this error  */
        }
    if ( (pulse != iopS) || (dkp_sta & STA_ERR) )
        {
        return ( FALSE ) ;
        }
        sim_activate (uptr, dkp_rwait);                 /* schedule read or write request */
        break;

    case FCCY_RECAL:                                    /* recalibrate */
        uptr->FUNC = FCCY_SEEK ;                        /* save command */
        uptr->CYL  = 0 ;

    case FCCY_SEEK:                                     /* seek */
        if ( ! (uptr->flags & UNIT_ATT) )                /* not attached? */
            {
            dkp_sta = dkp_sta | STA_DONE | STA_ERR;     /* error */
            }
        else if ( uptr->CYL >= drv_tab[dtype].cyl )     /* bad cylinder? */
            {
            dkp_sta = dkp_sta | STA_ERR | STA_CYL;
            }
        if ( (pulse != iopP) || (dkp_sta & STA_ERR) )
            {
            return ( FALSE ) ;                          /* only 'P' pulse start seeks!  */
            }

        /*  do the seek  */
        /* must check for "do we support seeking bits" flag before setting SEEK0'ish bits!  */
        dkp_sta = dkp_sta | (STA_SEEK0 >> u);           /* set seeking */
        oldCyl = abs(oldCyl - uptr->CYL) ;
        if ( (dkp_swait) && (! (oldCyl)) )              /* enforce minimum wait if req  */
            oldCyl = 1 ;
        sim_activate ( uptr, (dkp_swait * oldCyl) ) ;
        break;
        }                                               /* end case command */

return ( TRUE ) ;                                       /* no error */
}


/* Unit service

   If seek done, put on cylinder;
   else, do read or write
   If controller was busy, clear busy, set done, interrupt

   Memory access: sectors are read into/written from an intermediate
   buffer to allow word-by-word mapping of memory addresses on the
   Eclipse.  This allows each word written to memory to be tested
   for out of range.
*/

t_stat dkp_svc (UNIT *uptr)
{
int32 sa, bda;
int32 dx, pa, u;
int32 dtype, err, newsect, newsurf;
uint32 awc;
t_stat rval;
static uint16 tbuf[DKP_NUMWD];                          /* transfer buffer */


rval  = SCPE_OK;
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
u     = uptr - dkp_dev.units;                           /* get unit number */

if (uptr->FUNC == FCCY_SEEK) {                          /* seek? */
    if ( ! (uptr->flags & UNIT_ATT) )                   /* not attached? */
        {
        dkp_sta = dkp_sta | STA_DONE | STA_ERR;            /* error (changed during queue time?) */
        }
    else if ( uptr->CYL >= drv_tab[dtype].cyl )         /* bad cylinder? */
        {
        dkp_sta = dkp_sta | STA_ERR | STA_CYL;
        }
    DEV_SET_DONE( INT_DKP ) ;
    DEV_UPDATE_INTR ;
    dkp_sta = (dkp_sta | (STA_SKDN0 >> u))              /* set seek done */
                & ~(STA_SEEK0 >> u);                    /* clear seeking */
    if ( DKP_TRACE(2) )
        {
        fprintf( DKP_TRACE_FP,
            "  [%s:%c  seek : %4d ] \r\n",
            "DKP",
            (char) (u + '0'),
            (unsigned) (uptr->CYL)
            ) ;
        }
    return SCPE_OK;
    }

/*  read or write  */

if (((uptr->flags & UNIT_ATT) == 0) ||                  /* not attached? */
    ((uptr->flags & UNIT_WPRT) && (uptr->FUNC == FCCY_WRITE)))
    {
    dkp_sta = dkp_sta | STA_DONE | STA_ERR;             /* error */
    }
else if ( uptr->CYL >= drv_tab[dtype].cyl )             /* bad cylinder */
    {
    dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_CYL ;
    dkp_sta  = dkp_sta | STA_ERR | STA_CYL;
    DEV_SET_DONE( INT_DKP ) ;
    DEV_UPDATE_INTR ;
    return SCPE_OK ;
    }
else if ( GET_SURF(dkp_ussc, dtype) >= drv_tab[dtype].surf ) /* bad surface */
    {
    dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_UNS;   /* older drives may not even do this... */
/*  dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_XCY ;  /- newer disks give this error  */
/* set sector to some bad value and wait then exit?  */
    }
else if ( GET_SECT(dkp_ussc, dtype) >= drv_tab[dtype].sect )   /* or bad sector? */
    {
/*    dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older DG drives do not even give error(!), but we do */
    dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_XCY ;  /* newer disks give this error  */
    }
else {
err = 0 ;
do  {
    if ( DKP_TRACE(3) )
        {
        fprintf( DKP_TRACE_FP,
            "  [%s:%c  %-5s:  %3d / %2d / %2d   %06o ] \r\n",
            "DKP",
            (char) (u + '0'),
            ((uptr->FUNC == FCCY_READ) ?
                  "read"
                : ((uptr->FUNC == FCCY_WRITE) ?
                      "write"
                    : "<?>")
            ),
            (unsigned) (uptr->CYL),
            (unsigned) (GET_SURF(dkp_ussc, dtype)),
            (unsigned) (GET_SECT(dkp_ussc, dtype)),
            (unsigned) (dkp_ma & 0xFFFF) /* show all 16-bits in case DCH B */
            ) ;
        }


    if ( GET_SECT(dkp_ussc, dtype) >= drv_tab[dtype].sect )   /* or bad sector? */
        {
        /* sector overflows to 0 ;
         * surface gets incremented
         */
        newsurf = GET_SURF(dkp_ussc, dtype) + 1 ;
        newsurf = newsurf & ((drv_tab[dtype].newf) ? USSC_M_NSURFACE : USSC_M_OSURFACE) ;
        DKP_UPDATE_USSC( type, 0, newsurf, 0 )

        if ( (GET_SURF(dkp_ussc, dtype)) >= drv_tab[dtype].surf )
            {
        /*  dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older drives may not even do this... */
            dkp_sta = dkp_sta | STA_DONE | STA_ERR | STA_XCY ;  /*  newer disks give this error  */
            /* DG retains overflowed surface number,
             * other vendors have different/expanded options
             */
            break ;
            }
        }
    sa = GET_SA (uptr->CYL, GET_SURF (dkp_ussc, dtype),
         GET_SECT (dkp_ussc, dtype), dtype);            /* get disk block */
    bda = sa * DKP_NUMWD * sizeof(uint16) ;             /* to words, bytes */
    err = fseek (uptr->fileref, bda, SEEK_SET);         /* position drive */

    if (uptr->FUNC == FCCY_READ) {                      /* read? */
            awc = fxread (tbuf, sizeof(uint16), DKP_NUMWD, uptr->fileref);
            for ( ; awc < DKP_NUMWD; awc++) tbuf[awc] = 0;
            if ((err = ferror (uptr->fileref)))
                break;
            for (dx = 0; dx < DKP_NUMWD; dx++) {            /* loop thru buffer */
                pa = MapAddr (dkp_map, (dkp_ma & AMASK));
                if (MEM_ADDR_OK (pa))
                    M[pa] = tbuf[dx];
                dkp_ma = (dkp_ma + 1) & AMASK;
                }
        }
    else if (uptr->FUNC == FCCY_WRITE) {                /* write? */
            for (dx = 0; dx < DKP_NUMWD; dx++) {        /* loop into buffer */
                pa = MapAddr (dkp_map, (dkp_ma & AMASK));
                tbuf[dx] = M[pa];
                dkp_ma = (dkp_ma + 1) & AMASK;
                }
            fxwrite (tbuf, sizeof(int16), DKP_NUMWD, uptr->fileref);
            if ((err = ferror (uptr->fileref)))
                break;
            }

    if (err != 0) {
        sim_perror ("DKP I/O error");
        clearerr (uptr->fileref);
        rval = SCPE_IOERR;
        break ;
        }

newsect = GET_SECT (dkp_ussc, dtype) + 1 ;              /*  update next sector  */
newsurf = GET_SURF (dkp_ussc, dtype) ;                  /*  and next head  */
                                                        /*  (count set below)    */
DKP_UPDATE_USSC( type, 1, newsurf, newsect )
}  /*  end read/write loop  */

    while ( (GET_COUNT(dkp_ussc)) ) ;
    dkp_sta = dkp_sta | STA_DONE;                       /* set status */

    if ( DKP_TRACE(4) )
           {
           fprintf( DKP_TRACE_FP,
                   "  [%s:%c  %-5s:  %3d / %2d / %2d   %06o ] \r\n",
                   "DKP",
                    (char) (u + '0'),
                    "post",
                    (unsigned) (uptr->CYL),
                    (unsigned) (GET_SURF(dkp_ussc, dtype)),
                    (unsigned) (GET_SECT(dkp_ussc, dtype)),
                    (unsigned) (dkp_ma & 0xFFFF) /* show all 16-bits in case DCH B */
                    ) ;
            }
    }

DEV_CLR_BUSY( INT_DKP ) ;
DEV_SET_DONE( INT_DKP ) ;
DEV_UPDATE_INTR ;
return rval;
}

/* Reset routine */

t_stat dkp_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

DEV_CLR_BUSY( INT_DKP ) ;                               /*  clear busy    */
DEV_CLR_DONE( INT_DKP ) ;                               /*  clear done    */
DEV_UPDATE_INTR ;                                       /*  update ints    */
dkp_fccy = dkp_ussc = dkp_ma = dkp_sta = 0;             /* clear registers */
dkp_diagmode = 0;                                       /* clear diagnostic mode */
dkp_map = 0;
for (u = 0; u < DKP_NUMDR; u++) {                       /* loop thru units */
    uptr = dkp_dev.units + u;
    sim_cancel (uptr);                                  /* cancel activity */
    uptr->CYL = uptr->FUNC = 0;
    }
return SCPE_OK;
}

/* Attach routine (with optional autosizing) */

t_stat dkp_attach (UNIT *uptr, CONST char *cptr)
{
int32 i, p;
t_stat   r;

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;    /* restore capac */
r = attach_unit (uptr, cptr);                           /* attach */
if ((r != SCPE_OK) || !(uptr->flags & UNIT_AUTO))
    return r;
if ((p = sim_fsize (uptr->fileref)) == 0)               /* get file size */
    return SCPE_OK;
for (i = 0; drv_tab[i].sect != 0; i++) {
    if (p <= (drv_tab[i].size * (int32) sizeof (uint16))) {
        uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
        uptr->capac = drv_tab[i].size;
        return SCPE_OK;
        }
    }
return SCPE_OK;
}

/* Set size command validation routine */

t_stat dkp_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = drv_tab[GET_DTYPE (val)].size;
return SCPE_OK;
}

/* Bootstrap routine */

#if defined(_OLD_CODE_)

#define BOOT_START  02000
#define BOOT_UNIT   02021
#define BOOT_SEEK   02022
#define BOOT_LEN    (sizeof(boot_rom) / sizeof(int32))

static const int32 boot_rom[] = {
    0060233,                    /* NIOC 0,DKP           ; clear disk */
    0020420,                    /* LDA 0,USSC           ; unit, sfc, sec, cnt */
    0063033,                    /* DOC 0,DKP            ; select disk */
    0020417,                    /* LDA 0,SEKCMD         ; command, cylinder */
    0061333,                    /* DOAP 0,DKP           ; start seek */
    0024415,                    /* LDA 1,SEKDN */
    0060433,                    /* DIA 0,DKP            ; get status */
    0123415,                    /* AND# 1,0,SZR         ; skip if done */
    0000776,                    /* JMP .-2 */
    0102400,                    /* SUB 0,0              ; mem addr = 0 */
    0062033,                    /* DOB 0,DKP */
    0020411,                    /* LDA 0,REDCMD         ; command, cylinder */
    0061133,                    /* DOAS 0,DKP           ; start read */
    0060433,                    /* DIA 0, DKP           ; get status */
    0101113,                    /* MOVL# 0,0,SNC        ; skip if done */
    0000776,                    /* JMP .-2 */
    0000377,                    /* JMP 377 */
    0000016,                    /* USSC:   0.B1+0.B7+0.B11+16 */
    0175000,                    /* SEKCMD: 175000 */
    0074000,                    /* SEKDN:  074000 */
    0174000                     /* REDCMD: 174000 */
    };


t_stat dkp_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dtype;
extern int32 saved_PC, SR;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
unitno = unitno & USSC_M_UNIT;
dtype = GET_DTYPE (dkp_unit[unitno].flags);
M[BOOT_UNIT] = M[BOOT_UNIT] | (unitno << USSC_V_UNIT);
if (drv_tab[dtype].newf) M[BOOT_SEEK] = 0176000;
saved_PC = BOOT_START;
SR = 0100000 + DEV_DKP;
return SCPE_OK;
}

#endif      /*  _OLD_CODE_  */



#define BOOT_START  0375
#define BOOT_LEN    (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
      0062677                     /* IORST                ; reset the I/O system  */
    , 0060133                     /* NIOS DKP             ; start the disk        */
    , 0000377                     /* JMP 377              ; wait for the world    */
    } ;


t_stat dkp_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = (uint16) boot_rom[i];
saved_PC = BOOT_START;
SR = 0100000 + DEV_DKP;
return SCPE_OK;
}
