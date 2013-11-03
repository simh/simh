/* pdp11_rq.c: MSCP disk controller simulator

   Copyright (c) 2002-2013, Robert M Supnik
   Derived from work by Stephen F. Shirron

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

   rq           MSCP disk controller

   23-Oct-13    RMS     Revised for new boot setup routine
   09-Dec-12    MB      Added support for changing controller type.
   24-Oct-12    MB      Added mapped transfers for VAX
   29-Jan-11    HUH     Added RC25, RCF25 and RA80 disks
                        Not all disk parameters set yet
                        "KLESI" MSCP controller (3) / port (1) types for RC25 
                        not yet implemented
                        
                        Remarks on the RC25 disk drives: 
                        In "real" life the RC25 drives exist in pairs only,
                        one RC25 (removable) and one RCF25 (fixed) in one housing.
                        The removable platter has always got an even drive number 
                        (e.g. "0"), the fixed platter has always got the next (odd) 
                        drive number (e.g. "1"). These two rules are not enforced 
                        by the disk drive simulation.
   07-Mar-11    MP      Added working behaviors for removable device types.
                        This allows physical CDROM's to come online and be 
                        ejected.
   02-Mar-11    MP      Fixed missing information from save/restore which
                        caused operations to not complete correctly after 
                        a restore until the OS reset the controller.
   02-Feb-11    MP      Added Autosize support to rq_attach
   28-Jan-11    MP      Adopted use of sim_disk disk I/O library
                         - added support for the multiple formats sim_disk
                           provides (SimH, RAW, and VHD)
                         - adjusted to potentially leverage asynch I/O when 
                           available
                         - Added differing detailed debug output via sim_debug
   14-Jan-09    JH      Added support for RD32 disc drive
   18-Jun-07    RMS     Added UNIT_IDLE flag to timer thread
   31-Oct-05    RMS     Fixed address width for large files
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   22-Jul-05    RMS     Fixed warning from Solaris C (Doug Gwyn)
   17-Jan-05    RMS     Added more RA and RD disks
   31-Oct-04    RMS     Added -L switch (LBNs) to RAUSER size specification
   01-Oct-04    RMS     Revised Unibus interface
                        Changed to identify as UDA50 in Unibus configurations
                        Changed width to be 16b in all configurations
                        Changed default timing for VAX
   24-Jul-04    RMS     VAX controllers luns start with 0 (Andreas Cejna)
   05-Feb-04    RMS     Revised for file I/O library
   25-Jan-04    RMS     Revised for device debug support
   12-Jan-04    RMS     Fixed bug in interrupt control (Tom Evans)
   07-Oct-03    RMS     Fixed problem with multiple RAUSER drives
   17-Sep-03    RMS     Fixed MB to LBN conversion to be more accurate
   11-Jul-03    RMS     Fixed bug in user disk size (Chaskiel M Grundman)
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   14-Mar-03    RMS     Fixed variable size interaction with save/restore
   27-Feb-03    RMS     Added user-defined drive support
   26-Feb-03    RMS     Fixed bug in vector calculation for VAXen
   22-Feb-03    RMS     Fixed ordering bug in queue process
   12-Oct-02    RMS     Added multicontroller support
   29-Sep-02    RMS     Changed addressing to 18b in Unibus mode
                        Added variable address support to bootstrap
                        Added vector display support
                        Fixed status code in HBE error log
                        Consolidated MSCP/TMSCP header file
                        New data structures
   16-Aug-02    RMS     Removed unused variables (David Hittner)
   04-May-02    RMS     Fixed bug in polling loop for queued operations
   26-Mar-02    RMS     Fixed bug, reset routine cleared UF_WPH
   09-Mar-02    RMS     Adjusted delays for M+ timing bugs
   04-Mar-02    RMS     Added delays to initialization for M+, RSTS/E
   16-Feb-02    RMS     Fixed bugs in host timeout logic, boot
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Dec-01    RMS     Revised show routines
   19-Dec-01    RMS     Added bigger drives
   17-Dec-01    RMS     Added queue process
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "RQDX3 not supported on PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define RQ_QTIME        100
#define RQ_XTIME        200
#define OLDPC           fault_PC
extern int32 fault_PC;

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define RQ_QTIME        200
#define RQ_XTIME        500
#define OLDPC           MMR2
extern int32 MMR2;
extern uint32 cpu_opt;
#endif

#if !defined (RQ_NUMCT)
#define RQ_NUMCT        4
#elif (RQ_NUMCT > 4)
#error "Assertion failure: RQ_NUMCT exceeds 4"
#endif

#include "pdp11_uqssp.h"
#include "pdp11_mscp.h"
#include "sim_disk.h"

#define UF_MSK          (UF_CMR|UF_CMW)                 /* settable flags */

#define RQ_SH_MAX       24                              /* max display wds */
#define RQ_SH_PPL       8                               /* wds per line */
#define RQ_SH_DPL       4                               /* desc per line */
#define RQ_SH_RI        001                             /* show rings */
#define RQ_SH_FR        002                             /* show free q */
#define RQ_SH_RS        004                             /* show resp q */
#define RQ_SH_UN        010                             /* show unit q's */
#define RQ_SH_ALL       017                             /* show all */

#define RQ_CLASS        1                               /* RQ class */
#define RQU_UQPM        6                               /* UB port model */
#define RQQ_UQPM        19                              /* QB port model */
#define RQ_UQPM         (UNIBUS? RQU_UQPM: RQQ_UQPM)
#define RQU_MODEL       6                               /* UB MSCP ctrl model (UDA50A) */
#define RQQ_MODEL       19                              /* QB MSCP ctrl model (RQDX3) */
#define RQ_MODEL        (UNIBUS? RQU_MODEL: RQQ_MODEL)
#define RQ_HVER         1                               /* hardware version */
#define RQ_SVER         3                               /* software version */
#define RQ_DHTMO        60                              /* def host timeout */
#define RQ_DCTMO        120                             /* def ctrl timeout */
#define RQ_NUMDR        4                               /* # drives */
#define RQ_NUMBY        512                             /* bytes per block */
#define RQ_MAXFR        (1 << 16)                       /* max xfer */
#define RQ_MAPXFER      (1u << 31)                      /* mapped xfer */
#define RQ_M_PFN        0x1FFFFF                        /* map entry PFN */

#define UNIT_V_ONL      (UNIT_V_UF + 0)                 /* online */
#define UNIT_V_WLK      (UNIT_V_UF + 1)                 /* hwre write lock */
#define UNIT_V_ATP      (UNIT_V_UF + 2)                 /* attn pending */
#define UNIT_V_DTYPE    (UNIT_V_UF + 3)                 /* drive type */
#define UNIT_M_DTYPE    0x1F
#define UNIT_V_NOAUTO   (UNIT_V_UF + 8)                 /* noautosize */
#define UNIT_ONL        (1 << UNIT_V_ONL)
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_ATP        (1 << UNIT_V_ATP)
#define UNIT_NOAUTO     (1 << UNIT_V_NOAUTO)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define cpkt            u3                              /* current packet */
#define pktq            u4                              /* packet queue */
#define uf              buf                             /* settable unit flags */
#define cnum            wait                            /* controller index */
#define io_status       u5                              /* io status from callback */
#define io_complete     u6                              /* io completion flag */
#define rqxb            filebuf                         /* xfer buffer */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write prot */
#define RQ_RMV(u)       ((drv_tab[GET_DTYPE (u->flags)].flgs & RQDF_RMV)? \
                        UF_RMV: 0)
#define RQ_WPH(u)       (((drv_tab[GET_DTYPE (u->flags)].flgs & RQDF_RO) || \
                        (u->flags & UNIT_WPRT) || sim_disk_wrp (u))? UF_WPH: 0)

#define CST_S1          0                               /* init stage 1 */
#define CST_S1_WR       1                               /* stage 1 wrap */
#define CST_S2          2                               /* init stage 2 */
#define CST_S3          3                               /* init stage 3 */
#define CST_S3_PPA      4                               /* stage 3 sa wait */
#define CST_S3_PPB      5                               /* stage 3 ip wait */
#define CST_S4          6                               /* stage 4 */
#define CST_UP          7                               /* online */
#define CST_DEAD        8                               /* fatal error */

#define ERR             0                               /* must be SCPE_OK! */
#define OK              1

#define RQ_TIMER        (RQ_NUMDR)
#define RQ_QUEUE        (RQ_TIMER + 1)

/* Internal packet management.  The real RQDX3 manages its packets as true
   linked lists.  However, use of actual addresses in structures won't work
   with save/restore.  Accordingly, the packets are an arrayed structure,
   and links are actually subscripts.  To minimize complexity, packet[0]
   is not used (0 = end of list), and the number of packets must be a power
   of two.
*/

#define RQ_NPKTS        32                              /* # packets (pwr of 2) */
#define RQ_M_NPKTS      (RQ_NPKTS - 1)                  /* mask */
#define RQ_PKT_SIZE_W   32                              /* payload size (wds) */
#define RQ_PKT_SIZE     (RQ_PKT_SIZE_W * sizeof (int16))

struct rqpkt {
    int16       link;                                   /* link to next */
    uint16      d[RQ_PKT_SIZE_W];                       /* data */
    };

/* Packet payload extraction and insertion; cp defines controller */

#define GETP(p,w,f)     ((cp->pak[p].d[w] >> w##_V_##f) & w##_M_##f)
#define GETP32(p,w)     (((uint32) cp->pak[p].d[w]) | \
                        (((uint32) cp->pak[p].d[(w)+1]) << 16))
#define PUTP32(p,w,x)   cp->pak[p].d[w] = (x) & 0xFFFF; \
                        cp->pak[p].d[(w)+1] = ((x) >> 16) & 0xFFFF

/* Disk formats.  An RQDX3 disk consists of the following regions:

   XBNs         Extended blocks - contain information about disk format,
                also holds track being reformatted during bad block repl.
                Size = sectors/track + 1, replicated 3 times.
   DBNs         Diagnostic blocks - used by diagnostics.  Sized to pad
                out the XBNs to a cylinder boundary.
   LBNs         Logical blocks - contain user information.
   RCT          Replacement control table - first block contains status,
                second contains data from block being replaced, remaining
                contain information about replaced bad blocks.
                Size = RBNs/128 + 3, replicated 4-8 times.
   RBNs         Replacement blocks - used to replace bad blocks.

   The simulator does not need to perform bad block replacement; the
   information below is for simulating RCT reads, if required.

   Note that an RA drive has a different order: LBNs, RCT, XBN, DBN;
   the RBNs are spare blocks at the end of every track.
*/

#define RCT_OVHD        2                               /* #ovhd blks */
#define RCT_ENTB        128                             /* entries/blk */
#define RCT_END         0x80000000                      /* marks RCT end */

/* The RQDX3 supports multiple disk drive types (x = not implemented):

   type sec     surf    cyl     tpg     gpc     RCT     LBNs
        
   RX50 10      1       80      5       16      -       800
   RX33 15      2       80      2       1       -       2400
   RD51 18      4       306     4       1       36*4    21600
   RD31 17      4       615     4       1       3*8     41560
   RD52 17      8       512     8       1       4*8     60480
   RD32 17      6       820     6       1       4*8     83204
x  RD33 17      7       1170    ?       ?       ?       138565
   RD53 17      7       1024    7       1       5*8     138672
   RD54 17      15      1225    15      1       7*8     311200

   The simulator also supports larger drives that only existed
   on SDI controllers.

   RA60 42(+1)  6       1600    6       1       1008    400176
x  RA70 33(+1)  11      1507+   11      1       ?       547041
   RA80 31      14      546      ?      ?       ?       237212
   RA81 51(+1)  14      1258    14      1       2856    891072
   RA82 57(+1)  15      1435    15      1       3420    1216665
   RA71 51(+1)  14      1921    14      1       1428    1367310         
   RA72 51(+1)  20      1921    20      1       2040    1953300
   RA90 69(+1)  13      2656    13      1       1794    2376153
   RA92 73(+1)  13      3101    13      1       949     2940951
x  RA73 70(+1)  21      2667+   21      1       ?       3920490

   LESI attached RC25 disks (one removable, one fixed)
   type  sec     surf    cyl     tpg     gpc     RCT     LBNs
   RC25  31      2        821    ?       ?       ?       50902
   RCF25 31      2        821    ?       ?       ?       50902

   Each drive can be a different type.  The drive field in the
   unit flags specified the drive type and thus, indirectly,
   the drive size.
*/

#define RQDF_RMV        01                              /* removable */
#define RQDF_RO         02                              /* read only */
#define RQDF_SDI        04                              /* SDI drive */

#define RX50_DTYPE      0
#define RX50_SECT       10
#define RX50_SURF       1
#define RX50_CYL        80
#define RX50_TPG        5
#define RX50_GPC        16
#define RX50_XBN        0
#define RX50_DBN        0
#define RX50_LBN        800
#define RX50_RCTS       0
#define RX50_RCTC       0
#define RX50_RBN        0
#define RX50_MOD        7
#define RX50_MED        0x25658032
#define RX50_FLGS       RQDF_RMV

#define RX33_DTYPE      1
#define RX33_SECT       15
#define RX33_SURF       2
#define RX33_CYL        80
#define RX33_TPG        2
#define RX33_GPC        1
#define RX33_XBN        0
#define RX33_DBN        0
#define RX33_LBN        2400
#define RX33_RCTS       0
#define RX33_RCTC       0
#define RX33_RBN        0
#define RX33_MOD        10
#define RX33_MED        0x25658021
#define RX33_FLGS       RQDF_RMV

#define RD51_DTYPE      2
#define RD51_SECT       18
#define RD51_SURF       4
#define RD51_CYL        306
#define RD51_TPG        4
#define RD51_GPC        1
#define RD51_XBN        57
#define RD51_DBN        87
#define RD51_LBN        21600
#define RD51_RCTS       36
#define RD51_RCTC       4
#define RD51_RBN        144
#define RD51_MOD        6
#define RD51_MED        0x25644033
#define RD51_FLGS       0

#define RD31_DTYPE      3
#define RD31_SECT       17
#define RD31_SURF       4
#define RD31_CYL        615                             /* last unused */
#define RD31_TPG        RD31_SURF
#define RD31_GPC        1
#define RD31_XBN        54
#define RD31_DBN        14
#define RD31_LBN        41560
#define RD31_RCTS       3
#define RD31_RCTC       8
#define RD31_RBN        100
#define RD31_MOD        12
#define RD31_MED        0x2564401F
#define RD31_FLGS       0

#define RD52_DTYPE      4                               /* Quantum params */
#define RD52_SECT       17
#define RD52_SURF       8
#define RD52_CYL        512
#define RD52_TPG        RD52_SURF
#define RD52_GPC        1
#define RD52_XBN        54
#define RD52_DBN        82
#define RD52_LBN        60480
#define RD52_RCTS       4
#define RD52_RCTC       8
#define RD52_RBN        168
#define RD52_MOD        8
#define RD52_MED        0x25644034
#define RD52_FLGS       0

#define RD53_DTYPE      5
#define RD53_SECT       17
#define RD53_SURF       8
#define RD53_CYL        1024                            /* last unused */
#define RD53_TPG        RD53_SURF
#define RD53_GPC        1
#define RD53_XBN        54
#define RD53_DBN        82
#define RD53_LBN        138672
#define RD53_RCTS       5
#define RD53_RCTC       8
#define RD53_RBN        280
#define RD53_MOD        9
#define RD53_MED        0x25644035
#define RD53_FLGS       0

#define RD54_DTYPE      6
#define RD54_SECT       17
#define RD54_SURF       15
#define RD54_CYL        1225                            /* last unused */
#define RD54_TPG        RD54_SURF
#define RD54_GPC        1
#define RD54_XBN        54
#define RD54_DBN        201
#define RD54_LBN        311200
#define RD54_RCTS       7
#define RD54_RCTC       8
#define RD54_RBN        609
#define RD54_MOD        13
#define RD54_MED        0x25644036
#define RD54_FLGS       0

#define RA82_DTYPE      7                               /* SDI drive */
#define RA82_SECT       57                              /* +1 spare/track */
#define RA82_SURF       15
#define RA82_CYL        1435                            /* 0-1422 user */
#define RA82_TPG        RA82_SURF
#define RA82_GPC        1
#define RA82_XBN        3480                            /* cyl 1427-1430 */
#define RA82_DBN        3480                            /* cyl 1431-1434 */
#define RA82_LBN        1216665                         /* 57*15*1423 */
#define RA82_RCTS       3420                            /* cyl 1423-1426 */
#define RA82_RCTC       1
#define RA82_RBN        21345                           /* 1 *15*1423 */
#define RA82_MOD        11
#define RA82_MED        0x25641052
#define RA82_FLGS       RQDF_SDI

#define RRD40_DTYPE     8
#define RRD40_SECT      128
#define RRD40_SURF      1
#define RRD40_CYL       10400
#define RRD40_TPG       RRD40_SURF
#define RRD40_GPC       1
#define RRD40_XBN       0
#define RRD40_DBN       0
#define RRD40_LBN       1331200
#define RRD40_RCTS      0
#define RRD40_RCTC      0
#define RRD40_RBN       0
#define RRD40_MOD       26
#define RRD40_MED       0x25652228
#define RRD40_FLGS      (RQDF_RMV | RQDF_RO)

#define RA72_DTYPE      9                               /* SDI drive */
#define RA72_SECT       51                              /* +1 spare/trk */
#define RA72_SURF       20
#define RA72_CYL        1921                            /* 0-1914 user */
#define RA72_TPG        RA72_SURF
#define RA72_GPC        1
#define RA72_XBN        2080                            /* cyl 1917-1918? */
#define RA72_DBN        2080                            /* cyl 1920-1921? */
#define RA72_LBN        1953300                         /* 51*20*1915 */
#define RA72_RCTS       2040                            /* cyl 1915-1916? */
#define RA72_RCTC       1
#define RA72_RBN        38300                           /* 1 *20*1915 */
#define RA72_MOD        37
#define RA72_MED        0x25641048
#define RA72_FLGS       RQDF_SDI

#define RA90_DTYPE      10                              /* SDI drive */
#define RA90_SECT       69                              /* +1 spare/trk */
#define RA90_SURF       13
#define RA90_CYL        2656                            /* 0-2648 user */
#define RA90_TPG        RA90_SURF
#define RA90_GPC        1
#define RA90_XBN        1820                            /* cyl 2651-2652? */
#define RA90_DBN        1820                            /* cyl 2653-2654? */
#define RA90_LBN        2376153                         /* 69*13*2649 */
#define RA90_RCTS       1794                            /* cyl 2649-2650? */
#define RA90_RCTC       1
#define RA90_RBN        34437                           /* 1 *13*2649 */
#define RA90_MOD        19
#define RA90_MED        0x2564105A
#define RA90_FLGS       RQDF_SDI

#define RA92_DTYPE      11                              /* SDI drive */
#define RA92_SECT       73                              /* +1 spare/trk */
#define RA92_SURF       13
#define RA92_CYL        3101                            /* 0-3098 user */
#define RA92_TPG        RA92_SURF
#define RA92_GPC        1
#define RA92_XBN        174                             /* cyl 3100? */
#define RA92_DBN        788
#define RA92_LBN        2940951                         /* 73*13*3099 */
#define RA92_RCTS       949                             /* cyl 3099? */
#define RA92_RCTC       1
#define RA92_RBN        40287                           /* 1 *13*3099 */
#define RA92_MOD        29
#define RA92_MED        0x2564105C
#define RA92_FLGS       RQDF_SDI

#define RA8U_DTYPE      12                              /* user defined */
#define RA8U_SECT       57                              /* from RA82 */
#define RA8U_SURF       15
#define RA8U_CYL        1435                            /* from RA82 */
#define RA8U_TPG        RA8U_SURF
#define RA8U_GPC        1
#define RA8U_XBN        0
#define RA8U_DBN        0
#define RA8U_LBN        1216665                         /* from RA82 */
#define RA8U_RCTS       400
#define RA8U_RCTC       8
#define RA8U_RBN        21345
#define RA8U_MOD        11                              /* RA82 */
#define RA8U_MED        0x25641052                      /* RA82 */
#define RA8U_FLGS       RQDF_SDI
#define RA8U_MINC       10000                           /* min cap LBNs */
#define RA8U_MAXC       4194303                         /* max cap LBNs */
#define RA8U_EMAXC      2147483647                      /* ext max cap */

#define RA60_DTYPE      13                              /* SDI drive */
#define RA60_SECT       42                              /* +1 spare/track */
#define RA60_SURF       6
#define RA60_CYL        1600                            /* 0-1587 user */
#define RA60_TPG        RA60_SURF
#define RA60_GPC        1
#define RA60_XBN        1032                            /* cyl 1592-1595 */
#define RA60_DBN        1032                            /* cyl 1596-1599 */
#define RA60_LBN        400176                          /* 42*6*1588 */
#define RA60_RCTS       1008                            /* cyl 1588-1591 */
#define RA60_RCTC       1
#define RA60_RBN        9528                            /* 1 *6*1588 */
#define RA60_MOD        4
#define RA60_MED        0x22A4103C
#define RA60_FLGS       (RQDF_RMV | RQDF_SDI)

#define RA81_DTYPE      14                              /* SDI drive */
#define RA81_SECT       51                              /* +1 spare/track */
#define RA81_SURF       14
#define RA81_CYL        1258                            /* 0-1247 user */
#define RA81_TPG        RA81_SURF
#define RA81_GPC        1
#define RA81_XBN        2436                            /* cyl 1252-1254? */
#define RA81_DBN        2436                            /* cyl 1255-1256? */
#define RA81_LBN        891072                          /* 51*14*1248 */
#define RA81_RCTS       2856                            /* cyl 1248-1251? */
#define RA81_RCTC       1
#define RA81_RBN        17472                           /* 1 *14*1248 */
#define RA81_MOD        5
#define RA81_MED        0x25641051
#define RA81_FLGS       RQDF_SDI

#define RA71_DTYPE      15                              /* SDI drive */
#define RA71_SECT       51                              /* +1 spare/track */
#define RA71_SURF       14
#define RA71_CYL        1921                            /* 0-1914 user */
#define RA71_TPG        RA71_SURF
#define RA71_GPC        1
#define RA71_XBN        1456                            /* cyl 1917-1918? */
#define RA71_DBN        1456                            /* cyl 1919-1920? */
#define RA71_LBN        1367310                         /* 51*14*1915 */
#define RA71_RCTS       1428                            /* cyl 1915-1916? */
#define RA71_RCTC       1
#define RA71_RBN        26810                           /* 1 *14*1915 */
#define RA71_MOD        40
#define RA71_MED        0x25641047
#define RA71_FLGS       RQDF_SDI

#define RD32_DTYPE      16
#define RD32_SECT       17
#define RD32_SURF       6
#define RD32_CYL        820
#define RD32_TPG        RD32_SURF
#define RD32_GPC        1
#define RD32_XBN        54
#define RD32_DBN        48
#define RD32_LBN        83236
#define RD32_RCTS       4
#define RD32_RCTC       8
#define RD32_RBN        200
#define RD32_MOD        15
#define RD32_MED        0x25644020
#define RD32_FLGS       0

#define RC25_DTYPE      17                              /*  */
#define RC25_SECT       50                              /*  */
#define RC25_SURF       8
#define RC25_CYL        1260                            /*  */
#define RC25_TPG        RC25_SURF
#define RC25_GPC        1
#define RC25_XBN        0                               /*  */
#define RC25_DBN        0                               /*  */
#define RC25_LBN        50902                           /* ? 50*8*1260 ? */
#define RC25_RCTS       0                               /*  */
#define RC25_RCTC       1
#define RC25_RBN        0                               /*  */
#define RC25_MOD        3
#define RC25_MED        0x20643019
#define RC25_FLGS       RQDF_RMV

#define RCF25_DTYPE     18                              /*  */
#define RCF25_SECT      50                              /*  */
#define RCF25_SURF      8
#define RCF25_CYL       1260                            /*  */
#define RCF25_TPG       RCF25_SURF
#define RCF25_GPC       1
#define RCF25_XBN       0                               /*  */
#define RCF25_DBN       0                               /*  */
#define RCF25_LBN       50902                           /* ? 50*8*1260 ? */
#define RCF25_RCTS      0                               /*  */
#define RCF25_RCTC      1
#define RCF25_RBN       0                               /*  */
#define RCF25_MOD       3
#define RCF25_MED       0x20643319
#define RCF25_FLGS      0

#define RA80_DTYPE      19                              /* SDI drive */
#define RA80_SECT       31                              /* +1 spare/track */
#define RA80_SURF       14
#define RA80_CYL        546                             /*  */
#define RA80_TPG        RA80_SURF
#define RA80_GPC        1
#define RA80_XBN        0                               /*  */
#define RA80_DBN        0                               /*  */
#define RA80_LBN        237212                          /* 31*14*546 */
#define RA80_RCTS       0                               /*  */
#define RA80_RCTC       1
#define RA80_RBN        0                               /*  */
#define RA80_MOD        1
#define RA80_MED        0x25641050
#define RA80_FLGS       RQDF_SDI

/* Controller parameters */

#define DEFAULT_CTYPE   0

#define KLESI_CTYPE     1
#define KLESI_UQPM      1
#define KLESI_MODEL     1

#define RUX50_CTYPE     2
#define RUX50_UQPM      2
#define RUX50_MODEL     2

#define UDA50_CTYPE     3
#define UDA50_UQPM      6
#define UDA50_MODEL     6

#define RQDX3_CTYPE     4
#define RQDX3_UQPM      19
#define RQDX3_MODEL     19

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       tpg;                                    /* trk/grp */
    int32       gpc;                                    /* grp/cyl */
    int32       xbn;                                    /* XBN size */
    int32       dbn;                                    /* DBN size */
    uint32      lbn;                                    /* LBN size */
    int32       rcts;                                   /* RCT size */
    int32       rctc;                                   /* RCT copies */
    int32       rbn;                                    /* RBNs */
    int32       mod;                                    /* MSCP model */
    int32       med;                                    /* MSCP media */
    int32       flgs;                                   /* flags */
    char        *name;                                  /* name */
    };

#define RQ_DRV(d) \
    d##_SECT, d##_SURF, d##_CYL,  d##_TPG, \
    d##_GPC,  d##_XBN,  d##_DBN,  d##_LBN, \
    d##_RCTS, d##_RCTC, d##_RBN,  d##_MOD, \
    d##_MED, d##_FLGS
#define RQ_SIZE(d)      d##_LBN

static struct drvtyp drv_tab[] = {
    { RQ_DRV (RX50), "RX50" }, 
    { RQ_DRV (RX33), "RX33" },
    { RQ_DRV (RD51), "RD51" }, 
    { RQ_DRV (RD31), "RD31" },
    { RQ_DRV (RD52), "RD52" }, 
    { RQ_DRV (RD53), "RD53" },
    { RQ_DRV (RD54), "RD54" }, 
    { RQ_DRV (RA82), "RA82" },
    { RQ_DRV (RRD40), "RRD40" }, 
    { RQ_DRV (RA72), "RA72" },
    { RQ_DRV (RA90), "RA90" }, 
    { RQ_DRV (RA92), "RA92" },
    { RQ_DRV (RA8U), "RAUSER" }, 
    { RQ_DRV (RA60), "RA60" },
    { RQ_DRV (RA81), "RA81" }, 
    { RQ_DRV (RA71), "RA71" },
    { RQ_DRV (RD32), "RD32" }, 
    { RQ_DRV (RC25), "RC25" },
    { RQ_DRV (RCF25), "RCF25" },
    { RQ_DRV (RA80), "RA80" },
    { 0 }
    };

struct ctlrtyp {
    uint32      uqpm;                                   /* port model */
    uint32      model;                                  /* controller model */
    char        *name;                                  /* name */
    };

#define RQ_CTLR(d) \
    d##_UQPM, d##_MODEL

static struct ctlrtyp ctlr_tab[] = {
    { 0,            0, "DEFAULT" },
    { RQ_CTLR (KLESI), "KLESI" },
    { RQ_CTLR (RUX50), "RUX50" },
    { RQ_CTLR (UDA50), "UDA50" },
    { RQ_CTLR (RQDX3), "RQDX3" },
    { 0 }
    };

extern int32 int_req[IPL_HLVL];

int32 rq_itime = 200;                                   /* init time, except */
int32 rq_itime4 = 10;                                   /* stage 4 */
int32 rq_qtime = RQ_QTIME;                              /* queue time */
int32 rq_xtime = RQ_XTIME;                              /* transfer time */

typedef struct {
    uint32              cnum;                           /* ctrl number */
    uint32              ubase;                          /* unit base */
    uint32              sa;                             /* status, addr */
    uint32              saw;                            /* written data */
    uint32              s1dat;                          /* S1 data */
    uint32              comm;                           /* comm region */
    uint32              csta;                           /* ctrl state */
    uint32              perr;                           /* last error */
    uint32              cflgs;                          /* ctrl flags */
    uint32              irq;                            /* intr request */
    uint32              prgi;                           /* purge int */
    uint32              pip;                            /* poll in progress */
    int32               freq;                           /* free list */
    int32               rspq;                           /* resp list */
    uint32              pbsy;                           /* #busy pkts */
    uint32              credits;                        /* credits */
    uint32              hat;                            /* host timer */
    uint32              htmo;                           /* host timeout */
    uint32              ctype;                          /* controller type */
    struct uq_ring      cq;                             /* cmd ring */
    struct uq_ring      rq;                             /* rsp ring */
    struct rqpkt        pak[RQ_NPKTS];                  /* packet queue */
    } MSC;

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_INI  0x0002                                 /* display setup/init sequence info */
#define DBG_REG  0x0004                                 /* trace read/write registers */
#define DBG_REQ  0x0008                                 /* display transfer requests */
#define DBG_DSK  0x0010                                 /* display sim_disk activities */
#define DBG_DAT  0x0020                                 /* display transfer data */

DEBTAB rq_debug[] = {
  {"TRACE",  DBG_TRC},
  {"INIT",   DBG_INI},
  {"REG",    DBG_REG},
  {"REQ",    DBG_REQ},
  {"DISK",   DBG_DSK},
  {"DATA",   DBG_DAT},
  {0}
};

static char *rq_cmdname[] = {
    "",                                                 /*  0 */
    "ABO",                                              /*  1 b: abort */
    "GCS",                                              /*  2 b: get command status */
    "GUS",                                              /*  3 b: get unit status */
    "SCC",                                              /*  4 b: set controller char */
    "","","",                                           /*  5-7 */
    "AVL",                                              /*  8 b: available */
    "ONL",                                              /*  9 b: online */
    "SUC",                                              /* 10 b: set unit char */
    "DAP",                                              /* 11 b: det acc paths - nop */
    "","","","",                                        /* 12-15 */
    "ACC",                                              /* 16 b: access */
    "CCD",                                              /* 17 d: compare - nop */
    "ERS",                                              /* 18 b: erase */
    "FLU",                                              /* 19 d: flush - nop */
    "","",                                              /* 20-21 */
    "ERG",                                              /* 22 t: erase gap */
    "","","","","","","","","",                         /* 23-31 */
    "CMP",                                              /* 32 b: compare */
    "RD",                                               /* 33 b: read */
    "WR",                                               /* 34 b: write */
    "",                                                 /* 35 */
    "WTM",                                              /* 36 t: write tape mark */
    "POS",                                              /* 37 t: reposition */
    "","","","","","","","","",                         /* 38-46 */
    "FMT",                                              /* 47 d: format */
    "","","","","","","","","","","","","","","","",    /* 48-63 */
    "AVA",                                              /* 64 b: unit now avail */
    };

DEVICE rq_dev, rqb_dev, rqc_dev, rqd_dev;

t_stat rq_rd (int32 *data, int32 PA, int32 access);
t_stat rq_wr (int32 data, int32 PA, int32 access);
t_stat rq_svc (UNIT *uptr);
t_stat rq_tmrsvc (UNIT *uptr);
t_stat rq_quesvc (UNIT *uptr);
t_stat rq_reset (DEVICE *dptr);
t_stat rq_attach (UNIT *uptr, char *cptr);
t_stat rq_detach (UNIT *uptr);
t_stat rq_boot (int32 unitno, DEVICE *dptr);
t_stat rq_set_wlk (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rq_set_type (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rq_set_ctype (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rq_show_type (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_ctype (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_wlk (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_ctrl (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_unitq (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *rq_description (DEVICE *dptr);

t_bool rq_step4 (MSC *cp);
t_bool rq_mscp (MSC *cp, int32 pkt, t_bool q);
t_bool rq_abo (MSC *cp, int32 pkt, t_bool q);
t_bool rq_avl (MSC *cp, int32 pkt, t_bool q);
t_bool rq_fmt (MSC *cp, int32 pkt, t_bool q);
t_bool rq_gcs (MSC *cp, int32 pkt, t_bool q);
t_bool rq_gus (MSC *cp, int32 pkt, t_bool q);
t_bool rq_onl (MSC *cp, int32 pkt, t_bool q);
t_bool rq_rw (MSC *cp, int32 pkt, t_bool q);
t_bool rq_scc (MSC *cp, int32 pkt, t_bool q);
t_bool rq_suc (MSC *cp, int32 pkt, t_bool q);
t_bool rq_plf (MSC *cp, uint32 err);
t_bool rq_dte (MSC *cp, UNIT *uptr, uint32 err);
t_bool rq_hbe (MSC *cp, UNIT *uptr);
t_bool rq_una (MSC *cp, int32 un);
t_bool rq_deqf (MSC *cp, int32 *pkt);
int32 rq_deqh (MSC *cp, int32 *lh);
void rq_enqh (MSC *cp, int32 *lh, int32 pkt);
void rq_enqt (MSC *cp, int32 *lh, int32 pkt);
t_bool rq_getpkt (MSC *cp, int32 *pkt);
t_bool rq_putpkt (MSC *cp, int32 pkt, t_bool qt);
t_bool rq_getdesc (MSC *cp, struct uq_ring *ring, uint32 *desc);
t_bool rq_putdesc (MSC *cp, struct uq_ring *ring, uint32 desc);
int32 rq_rw_valid (MSC *cp, int32 pkt, UNIT *uptr, uint32 cmd);
t_bool rq_rw_end (MSC *cp, UNIT *uptr, uint32 flg, uint32 sts);
uint32 rq_map_ba (uint32 ba, uint32 ma);
int32 rq_readb (uint32 ba, int32 bc, uint32 ma, uint8 *buf);
int32 rq_readw (uint32 ba, int32 bc, uint32 ma, uint16 *buf);
int32 rq_writew (uint32 ba, int32 bc, uint32 ma, uint16 *buf);
void rq_putr (MSC *cp, int32 pkt, uint32 cmd, uint32 flg,
    uint32 sts, uint32 lnt, uint32 typ);
void rq_putr_unit (MSC *cp, int32 pkt, UNIT *uptr, uint32 lu, t_bool all);
void rq_setf_unit (MSC *cp, int32 pkt, UNIT *uptr);
void rq_init_int (MSC *cp);
void rq_ring_int (MSC *cp, struct uq_ring *ring);
t_bool rq_fatal (MSC *cp, uint32 err);
UNIT *rq_getucb (MSC *cp, uint32 lu);
int32 rq_map_pa (uint32 pa);
void rq_setint (MSC *cp);
void rq_clrint (MSC *cp);
int32 rq_inta (void);

/* RQ data structures

   rq_dev       RQ device descriptor
   rq_unit      RQ unit list
   rq_reg       RQ register list
   rq_mod       RQ modifier list
*/

MSC rq_ctx = { 0 };

#define IOLN_RQ         004

DIB rq_dib = {
    IOBA_AUTO, IOLN_RQ, &rq_rd, &rq_wr,
    1, IVCL (RQ), 0, { &rq_inta }, IOLN_RQ
    };

UNIT rq_unit[] = {
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RX50_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RX50)) },
    { UDATA (&rq_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA (&rq_quesvc, UNIT_DIS, 0) }
    };

REG rq_reg[] = {
    { GRDATAD (UBASE,   rq_ctx.ubase,   DEV_RDX,  8, 0, "unit base"), REG_RO },
    { GRDATAD (SA,      rq_ctx.sa,      DEV_RDX, 16, 0, "status/address register") },
    { GRDATAD (SAW,     rq_ctx.saw,     DEV_RDX, 16, 0, "written data") },
    { GRDATAD (S1DAT,   rq_ctx.s1dat,   DEV_RDX, 16, 0, "step 1 init host data") },
    { GRDATAD (COMM,    rq_ctx.comm,    DEV_RDX, 22, 0, "comm region") },
    { GRDATAD (CQIOFF,  rq_ctx.cq.ioff, DEV_RDX, 32, 0, "command queue intr offset") },
    { GRDATAD (CQBA,    rq_ctx.cq.ba,   DEV_RDX, 22, 0, "command queue base address") },
    { GRDATAD (CQLNT,   rq_ctx.cq.lnt,  DEV_RDX, 32, 2, "command queue length"), REG_NZ },
    { GRDATAD (CQIDX,   rq_ctx.cq.idx,  DEV_RDX,  8, 2, "command queue index") },
    { GRDATAD (RQIOFF,  rq_ctx.rq.ioff, DEV_RDX, 32, 0, "request queue intr offset") },
    { GRDATAD (RQBA,    rq_ctx.rq.ba,   DEV_RDX, 22, 0, "request queue base address") },
    { GRDATAD (RQLNT,   rq_ctx.rq.lnt,  DEV_RDX, 32, 2, "request queue length"), REG_NZ },
    { GRDATAD (RQIDX,   rq_ctx.rq.idx,  DEV_RDX,  8, 2, "request queue index") },
    { DRDATAD (FREE,    rq_ctx.freq,                 5, "head of free packet list") },
    { DRDATAD (RESP,    rq_ctx.rspq,                 5, "head of response packet list") },
    { DRDATAD (PBSY,    rq_ctx.pbsy,                 5, "number of busy packets") },
    { GRDATAD (CFLGS,   rq_ctx.cflgs,   DEV_RDX, 16, 0, "controller flags") },
    { GRDATAD (CSTA,    rq_ctx.csta,    DEV_RDX,  4, 0, "controller state") },
    { GRDATAD (PERR,    rq_ctx.perr,    DEV_RDX,  9, 0, "port error number") },
    { DRDATAD (CRED,    rq_ctx.credits,              5, "host credits") },
    { DRDATAD (HAT,     rq_ctx.hat,                 17, "host available timer") },
    { DRDATAD (HTMO,    rq_ctx.htmo,                17, "host timeout value") },
    { FLDATA  (PRGI,    rq_ctx.prgi,                 0), REG_HIDDEN },
    { FLDATA  (PIP,     rq_ctx.pip,                  0), REG_HIDDEN },
    { FLDATA  (CTYPE,   rq_ctx.ctype,               32), REG_HIDDEN  },
    { DRDATAD (ITIME,   rq_itime,                   24, "init time delay, except stage 4"), PV_LEFT + REG_NZ },
    { DRDATAD (I4TIME,  rq_itime4,                  24, "init stage 4 delay"), PV_LEFT + REG_NZ },
    { DRDATAD (QTIME,   rq_qtime,                   24, "response time for 'immediate' packets"), PV_LEFT + REG_NZ },
    { DRDATAD (XTIME,   rq_xtime,                   24, "response time for data transfers"), PV_LEFT + REG_NZ },
    { BRDATAD (PKTS,    rq_ctx.pak,     DEV_RDX,    16, sizeof(rq_ctx.pak)/2, "packet buffers, 33W each, 32 entries") },
    { URDATAD (CPKT,    rq_unit[0].cpkt, 10, 5, 0, RQ_NUMDR, 0, "current packet, units 0 to 3") },
    { URDATAD (UCNUM,   rq_unit[0].cnum, 10, 5, 0, RQ_NUMDR, 0, "ctrl number, units 0 to 3") },
    { URDATAD (PKTQ,    rq_unit[0].pktq, 10, 5, 0, RQ_NUMDR, 0, "packet queue, units 0 to 3") },
    { URDATAD (UFLG,    rq_unit[0].uf,  DEV_RDX, 16, 0, RQ_NUMDR, 0, "unit flags, units 0 to 3") },
    { URDATA  (CAPAC,   rq_unit[0].capac, 10, T_ADDR_W, 0, RQ_NUMDR, PV_LEFT | REG_HRO) },
    { GRDATA  (DEVADDR, rq_dib.ba,      DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,  rq_dib.vec,     DEV_RDX, 16, 0), REG_HRO },
    { DRDATA  (DEVLBN,  drv_tab[RA8U_DTYPE].lbn, 22), REG_HRO },
    { NULL }
    };

MTAB rq_mod[] = {
    { UNIT_WLK,                 0,  NULL, "WRITEENABLED", 
        &rq_set_wlk, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK,          UNIT_WLK,  NULL, "LOCKED", 
        &rq_set_wlk, NULL, NULL, "Write lock disk drive"  },
    { MTAB_XTD|MTAB_VUN, 0, "WRITE", NULL,
      NULL, &rq_show_wlk, NULL,  "Display drive writelock status" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RQ_SH_RI, "RINGS", NULL,
      NULL, &rq_show_ctrl, NULL, "Display command and response rings" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RQ_SH_FR, "FREEQ", NULL,
      NULL, &rq_show_ctrl, NULL, "Display free queue" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RQ_SH_RS, "RESPQ", NULL,
      NULL, &rq_show_ctrl, NULL, "Display response queue" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RQ_SH_UN, "UNITQ", NULL,
      NULL, &rq_show_ctrl, NULL, "Display all unit queues" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RQ_SH_ALL, "ALL", NULL,
      NULL, &rq_show_ctrl, NULL, "Display complete controller state" },
    { MTAB_XTD|MTAB_VDV, RQDX3_CTYPE, NULL, "RQDX3",
      &rq_set_ctype, NULL, NULL, "Set RQDX3 Controller Type" },
    { MTAB_XTD|MTAB_VDV, UDA50_CTYPE, NULL, "UDA50",
      &rq_set_ctype, NULL, NULL, "Set UDA50 Controller Type" },
    { MTAB_XTD|MTAB_VDV, KLESI_CTYPE, NULL, "KLESI",
      &rq_set_ctype, NULL, NULL, "Set KLESI Controller Type"  },
    { MTAB_XTD|MTAB_VDV, RUX50_CTYPE, NULL, "RUX50",
      &rq_set_ctype, NULL, NULL, "Set RUX50 Controller Type" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "UNITQ", NULL,
      NULL, &rq_show_unitq, NULL, "Display unit queue" },
    { MTAB_XTD|MTAB_VUN, RX50_DTYPE, NULL, "RX50",
      &rq_set_type, NULL, NULL, "Set RX50 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RX33_DTYPE, NULL, "RX33",
      &rq_set_type, NULL, NULL, "Set RX33 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD31_DTYPE, NULL, "RD31",
      &rq_set_type, NULL, NULL, "Set RD31 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD32_DTYPE, NULL, "RD32",
      &rq_set_type, NULL, NULL, "Set RD32 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD51_DTYPE, NULL, "RD51",
      &rq_set_type, NULL, NULL, "Set RD51 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD52_DTYPE, NULL, "RD52",
      &rq_set_type, NULL, NULL, "Set RD52 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD53_DTYPE, NULL, "RD53",
      &rq_set_type, NULL, NULL, "Set RD53 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD54_DTYPE, NULL, "RD54",
      &rq_set_type, NULL, NULL, "Set RD54 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA60_DTYPE, NULL, "RA60",
      &rq_set_type, NULL, NULL, "Set RA60 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA81_DTYPE, NULL, "RA81",
      &rq_set_type, NULL, NULL, "Set RA81 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA82_DTYPE, NULL, "RA82",
      &rq_set_type, NULL, NULL, "Set RA82 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRD40_DTYPE, NULL, "RRD40",
      &rq_set_type, NULL, NULL, "Set RRD40 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRD40_DTYPE, NULL, "CDROM",
      &rq_set_type, NULL, NULL, "Set CDROM Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA71_DTYPE, NULL, "RA71",
      &rq_set_type, NULL, NULL, "Set RA71 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA72_DTYPE, NULL, "RA72",
      &rq_set_type, NULL, NULL, "Set RA72 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA90_DTYPE, NULL, "RA90",
      &rq_set_type, NULL, NULL, "Set RA90 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA92_DTYPE, NULL, "RA92",
      &rq_set_type, NULL, NULL, "Set RA92 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RC25_DTYPE, NULL, "RC25",
      &rq_set_type, NULL, NULL, "Set RC25 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RCF25_DTYPE, NULL, "RCF25",
      &rq_set_type, NULL, NULL, "Set RCF25 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA80_DTYPE, NULL, "RA80",
      &rq_set_type, NULL, NULL, "Set RA80 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RA8U_DTYPE, NULL, "RAUSER",
      &rq_set_type, NULL, NULL, "Set RAUSER=size Disk Type" },
    { MTAB_XTD|MTAB_VUN, 0, "TYPE", NULL,
      NULL, &rq_show_type, NULL, "Display device type" },
    { UNIT_NOAUTO, UNIT_NOAUTO, "noautosize", "NOAUTOSIZE", NULL, NULL, NULL, "Disables disk autosize on attach" },
    { UNIT_NOAUTO,           0, "autosize",   "AUTOSIZE",   NULL, NULL, NULL, "Enables disk autosize on attach" },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_disk_set_fmt, &sim_disk_show_fmt, NULL, "Set/Display disk format (SIMH, VHD, RAW)" },
#if defined (VM_PDP11)
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
      &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
#else
    { MTAB_XTD|MTAB_VDV, 004, "ADDRESS", NULL,
      NULL, &show_addr, NULL, "Bus address" },
#endif
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL, "Interrupt vector" },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &rq_show_ctype, NULL, "Display controller type" },
    { 0 }
    };

DEVICE rq_dev = {
    "RQ", rq_unit, rq_reg, rq_mod,
    RQ_NUMDR + 2, DEV_RDX, T_ADDR_W, 2, DEV_RDX, 16,
    NULL, NULL, &rq_reset,
    &rq_boot, &rq_attach, &rq_detach,
    &rq_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_DISK | DEV_SECTORS,
    0, rq_debug, NULL, NULL, &rq_help, NULL, NULL,
    &rq_description
    };

/* RQB data structures

   rqb_dev      RQB device descriptor
   rqb_unit     RQB unit list
   rqb_reg      RQB register list
   rqb_mod      RQB modifier list
*/

MSC rqb_ctx = { 1 };

DIB rqb_dib = {
    IOBA_AUTO, IOLN_RQ, &rq_rd, &rq_wr,
    1, IVCL (RQ), 0, { &rq_inta }, IOLN_RQ
    };

UNIT rqb_unit[] = {
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA (&rq_quesvc, UNIT_DIS, 0) }
    };

REG rqb_reg[] = {
    { GRDATAD (UBASE,   rqb_ctx.ubase,   DEV_RDX,  8, 0, "unit base"), REG_RO },
    { GRDATAD (SA,      rqb_ctx.sa,      DEV_RDX, 16, 0, "status/address register") },
    { GRDATAD (SAW,     rqb_ctx.saw,     DEV_RDX, 16, 0, "written data") },
    { GRDATAD (S1DAT,   rqb_ctx.s1dat,   DEV_RDX, 16, 0, "step 1 init host data") },
    { GRDATAD (COMM,    rqb_ctx.comm,    DEV_RDX, 22, 0, "comm region") },
    { GRDATAD (CQIOFF,  rqb_ctx.cq.ioff, DEV_RDX, 32, 0, "command queue intr offset") },
    { GRDATAD (CQBA,    rqb_ctx.cq.ba,   DEV_RDX, 22, 0, "command queue base address") },
    { GRDATAD (CQLNT,   rqb_ctx.cq.lnt,  DEV_RDX, 32, 2, "command queue length"), REG_NZ },
    { GRDATAD (CQIDX,   rqb_ctx.cq.idx,  DEV_RDX,  8, 2, "command queue index") },
    { GRDATAD (RQIOFF,  rqb_ctx.rq.ioff, DEV_RDX, 32, 0, "request queue intr offset") },
    { GRDATAD (RQBA,    rqb_ctx.rq.ba,   DEV_RDX, 22, 0, "request queue base address") },
    { GRDATAD (RQLNT,   rqb_ctx.rq.lnt,  DEV_RDX, 32, 2, "request queue length"), REG_NZ },
    { GRDATAD (RQIDX,   rqb_ctx.rq.idx,  DEV_RDX,  8, 2, "request queue index") },
    { DRDATAD (FREE,    rqb_ctx.freq,                 5, "head of free packet list") },
    { DRDATAD (RESP,    rqb_ctx.rspq,                 5, "head of response packet list") },
    { DRDATAD (PBSY,    rqb_ctx.pbsy,                 5, "number of busy packets") },
    { GRDATAD (CFLGS,   rqb_ctx.cflgs,   DEV_RDX, 16, 0, "controller flags") },
    { GRDATAD (CSTA,    rqb_ctx.csta,    DEV_RDX,  4, 0, "controller state") },
    { GRDATAD (PERR,    rqb_ctx.perr,    DEV_RDX,  9, 0, "port error number") },
    { DRDATAD (CRED,    rqb_ctx.credits,              5, "host credits") },
    { DRDATAD (HAT,     rqb_ctx.hat,                 17, "host available timer") },
    { DRDATAD (HTMO,    rqb_ctx.htmo,                17, "host timeout value") },
    { FLDATA  (PRGI,    rqb_ctx.prgi,                 0), REG_HIDDEN },
    { FLDATA  (PIP,     rqb_ctx.pip,                  0), REG_HIDDEN },
    { FLDATA  (CTYPE,   rqb_ctx.ctype,               32), REG_HIDDEN  },
    { BRDATAD (PKTS,    rqb_ctx.pak,     DEV_RDX,    16, sizeof(rq_ctx.pak)/2, "packet buffers, 33W each, 32 entries") },
    { URDATAD (CPKT,    rqb_unit[0].cpkt, 10, 5, 0, RQ_NUMDR, 0, "current packet, units 0 to 3") },
    { URDATAD (UCNUM,   rqb_unit[0].cnum, 10, 5, 0, RQ_NUMDR, 0, "ctrl number, units 0 to 3") },
    { URDATAD (PKTQ,    rqb_unit[0].pktq, 10, 5, 0, RQ_NUMDR, 0, "packet queue, units 0 to 3") },
    { URDATAD (UFLG,    rqb_unit[0].uf,  DEV_RDX, 16, 0, RQ_NUMDR, 0, "unit flags, units 0 to 3") },
    { URDATA  (CAPAC,   rqb_unit[0].capac, 10, T_ADDR_W, 0, RQ_NUMDR, PV_LEFT | REG_HRO) },
    { GRDATA  (DEVADDR, rqb_dib.ba,      DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,  rqb_dib.vec,     DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

DEVICE rqb_dev = {
    "RQB", rqb_unit, rqb_reg, rq_mod,
    RQ_NUMDR + 2, DEV_RDX, T_ADDR_W, 2, DEV_RDX, 16,
    NULL, NULL, &rq_reset,
    &rq_boot, &rq_attach, &rq_detach,
    &rqb_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_DISK | DEV_SECTORS,
    0, rq_debug, NULL, NULL, &rq_help, NULL, NULL,
    &rq_description
    };

/* RQC data structures

   rqc_dev      RQC device descriptor
   rqc_unit     RQC unit list
   rqc_reg      RQC register list
   rqc_mod      RQC modifier list
*/

MSC rqc_ctx = { 2 };

DIB rqc_dib = {
    IOBA_AUTO, IOLN_RQ, &rq_rd, &rq_wr,
    1, IVCL (RQ), 0, { &rq_inta }, IOLN_RQ
    };

UNIT rqc_unit[] = {
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA (&rq_quesvc, UNIT_DIS, 0) }
    };

REG rqc_reg[] = {
    { GRDATAD (UBASE,   rqc_ctx.ubase,   DEV_RDX,  8, 0, "unit base"), REG_RO },
    { GRDATAD (SA,      rqc_ctx.sa,      DEV_RDX, 16, 0, "status/address register") },
    { GRDATAD (SAW,     rqc_ctx.saw,     DEV_RDX, 16, 0, "written data") },
    { GRDATAD (S1DAT,   rqc_ctx.s1dat,   DEV_RDX, 16, 0, "step 1 init host data") },
    { GRDATAD (COMM,    rqc_ctx.comm,    DEV_RDX, 22, 0, "comm region") },
    { GRDATAD (CQIOFF,  rqc_ctx.cq.ioff, DEV_RDX, 32, 0, "command queue intr offset") },
    { GRDATAD (CQBA,    rqc_ctx.cq.ba,   DEV_RDX, 22, 0, "command queue base address") },
    { GRDATAD (CQLNT,   rqc_ctx.cq.lnt,  DEV_RDX, 32, 2, "command queue length"), REG_NZ },
    { GRDATAD (CQIDX,   rqc_ctx.cq.idx,  DEV_RDX,  8, 2, "command queue index") },
    { GRDATAD (RQIOFF,  rqc_ctx.rq.ioff, DEV_RDX, 32, 0, "request queue intr offset") },
    { GRDATAD (RQBA,    rqc_ctx.rq.ba,   DEV_RDX, 22, 0, "request queue base address") },
    { GRDATAD (RQLNT,   rqc_ctx.rq.lnt,  DEV_RDX, 32, 2, "request queue length"), REG_NZ },
    { GRDATAD (RQIDX,   rqc_ctx.rq.idx,  DEV_RDX,  8, 2, "request queue index") },
    { DRDATAD (FREE,    rqc_ctx.freq,                 5, "head of free packet list") },
    { DRDATAD (RESP,    rqc_ctx.rspq,                 5, "head of response packet list") },
    { DRDATAD (PBSY,    rqc_ctx.pbsy,                 5, "number of busy packets") },
    { GRDATAD (CFLGS,   rqc_ctx.cflgs,   DEV_RDX, 16, 0, "controller flags") },
    { GRDATAD (CSTA,    rqc_ctx.csta,    DEV_RDX,  4, 0, "controller state") },
    { GRDATAD (PERR,    rqc_ctx.perr,    DEV_RDX,  9, 0, "port error number") },
    { DRDATAD (CRED,    rqc_ctx.credits,              5, "host credits") },
    { DRDATAD (HAT,     rqc_ctx.hat,                 17, "host available timer") },
    { DRDATAD (HTMO,    rqc_ctx.htmo,                17, "host timeout value") },
    { FLDATA  (PRGI,    rqc_ctx.prgi,                 0), REG_HIDDEN },
    { FLDATA  (PIP,     rqc_ctx.pip,                  0), REG_HIDDEN },
    { FLDATA  (CTYPE,   rqc_ctx.ctype,               32), REG_HIDDEN  },
    { BRDATAD (PKTS,    rqc_ctx.pak,     DEV_RDX,    16, sizeof(rq_ctx.pak)/2, "packet buffers, 33W each, 32 entries") },
    { URDATAD (CPKT,    rqc_unit[0].cpkt, 10, 5, 0, RQ_NUMDR, 0, "current packet, units 0 to 3") },
    { URDATAD (UCNUM,   rqc_unit[0].cnum, 10, 5, 0, RQ_NUMDR, 0, "ctrl number, units 0 to 3") },
    { URDATAD (PKTQ,    rqc_unit[0].pktq, 10, 5, 0, RQ_NUMDR, 0, "packet queue, units 0 to 3") },
    { URDATAD (UFLG,    rqc_unit[0].uf,  DEV_RDX, 16, 0, RQ_NUMDR, 0, "unit flags, units 0 to 3") },
    { URDATA  (CAPAC,   rqc_unit[0].capac, 10, T_ADDR_W, 0, RQ_NUMDR, PV_LEFT | REG_HRO) },
    { GRDATA  (DEVADDR, rqc_dib.ba,      DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,  rqc_dib.vec,     DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

DEVICE rqc_dev = {
    "RQC", rqc_unit, rqc_reg, rq_mod,
    RQ_NUMDR + 2, DEV_RDX, T_ADDR_W, 2, DEV_RDX, 16,
    NULL, NULL, &rq_reset,
    &rq_boot, &rq_attach, &rq_detach,
    &rqc_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_DISK | DEV_SECTORS,
    0, rq_debug, NULL, NULL, &rq_help, NULL, NULL,
    &rq_description
    };

/* RQD data structures

   rqd_dev      RQ device descriptor
   rqd_unit     RQ unit list
   rqd_reg      RQ register list
   rqd_mod      RQ modifier list
*/

MSC rqd_ctx = { 3 };

DIB rqd_dib = {
    IOBA_AUTO, IOLN_RQ, &rq_rd, &rq_wr,
    1, IVCL (RQ), 0, { &rq_inta }, IOLN_RQ
    };

UNIT rqd_unit[] = {
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
    { UDATA (&rq_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA (&rq_quesvc, UNIT_DIS, 0) }
    };

REG rqd_reg[] = {
    { GRDATAD (UBASE,   rqd_ctx.ubase,   DEV_RDX,  8, 0, "unit base"), REG_RO },
    { GRDATAD (SA,      rqd_ctx.sa,      DEV_RDX, 16, 0, "status/address register") },
    { GRDATAD (SAW,     rqd_ctx.saw,     DEV_RDX, 16, 0, "written data") },
    { GRDATAD (S1DAT,   rqd_ctx.s1dat,   DEV_RDX, 16, 0, "step 1 init host data") },
    { GRDATAD (COMM,    rqd_ctx.comm,    DEV_RDX, 22, 0, "comm region") },
    { GRDATAD (CQIOFF,  rqd_ctx.cq.ioff, DEV_RDX, 32, 0, "command queue intr offset") },
    { GRDATAD (CQBA,    rqd_ctx.cq.ba,   DEV_RDX, 22, 0, "command queue base address") },
    { GRDATAD (CQLNT,   rqd_ctx.cq.lnt,  DEV_RDX, 32, 2, "command queue length"), REG_NZ },
    { GRDATAD (CQIDX,   rqd_ctx.cq.idx,  DEV_RDX,  8, 2, "command queue index") },
    { GRDATAD (RQIOFF,  rqd_ctx.rq.ioff, DEV_RDX, 32, 0, "request queue intr offset") },
    { GRDATAD (RQBA,    rqd_ctx.rq.ba,   DEV_RDX, 22, 0, "request queue base address") },
    { GRDATAD (RQLNT,   rqd_ctx.rq.lnt,  DEV_RDX, 32, 2, "request queue length"), REG_NZ },
    { GRDATAD (RQIDX,   rqd_ctx.rq.idx,  DEV_RDX,  8, 2, "request queue index") },
    { DRDATAD (FREE,    rqd_ctx.freq,                 5, "head of free packet list") },
    { DRDATAD (RESP,    rqd_ctx.rspq,                 5, "head of response packet list") },
    { DRDATAD (PBSY,    rqd_ctx.pbsy,                 5, "number of busy packets") },
    { GRDATAD (CFLGS,   rqd_ctx.cflgs,   DEV_RDX, 16, 0, "controller flags") },
    { GRDATAD (CSTA,    rqd_ctx.csta,    DEV_RDX,  4, 0, "controller state") },
    { GRDATAD (PERR,    rqd_ctx.perr,    DEV_RDX,  9, 0, "port error number") },
    { DRDATAD (CRED,    rqd_ctx.credits,              5, "host credits") },
    { DRDATAD (HAT,     rqd_ctx.hat,                 17, "host available timer") },
    { DRDATAD (HTMO,    rqd_ctx.htmo,                17, "host timeout value") },
    { FLDATA  (PRGI,    rqd_ctx.prgi,                 0), REG_HIDDEN },
    { FLDATA  (PIP,     rqd_ctx.pip,                  0), REG_HIDDEN },
    { FLDATA  (CTYPE,   rqd_ctx.ctype,               32), REG_HIDDEN  },
    { BRDATAD (PKTS,    rqd_ctx.pak,     DEV_RDX,    16, sizeof(rq_ctx.pak)/2, "packet buffers, 33W each, 32 entries") },
    { URDATAD (CPKT,    rqd_unit[0].cpkt, 10, 5, 0, RQ_NUMDR, 0, "current packet, units 0 to 3") },
    { URDATAD (UCNUM,   rqd_unit[0].cnum, 10, 5, 0, RQ_NUMDR, 0, "ctrl number, units 0 to 3") },
    { URDATAD (PKTQ,    rqd_unit[0].pktq, 10, 5, 0, RQ_NUMDR, 0, "packet queue, units 0 to 3") },
    { URDATAD (UFLG,    rqd_unit[0].uf,  DEV_RDX, 16, 0, RQ_NUMDR, 0, "unit flags, units 0 to 3") },
    { URDATA  (CAPAC,   rqd_unit[0].capac, 10, T_ADDR_W, 0, RQ_NUMDR, PV_LEFT | REG_HRO) },
    { GRDATA  (DEVADDR, rqd_dib.ba,      DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,  rqd_dib.vec,     DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

DEVICE rqd_dev = {
    "RQD", rqd_unit, rqd_reg, rq_mod,
    RQ_NUMDR + 2, DEV_RDX, T_ADDR_W, 2, DEV_RDX, 16,
    NULL, NULL, &rq_reset,
    &rq_boot, &rq_attach, &rq_detach,
    &rqd_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_DISK | DEV_SECTORS,
    0, rq_debug, NULL, NULL, &rq_help, NULL, NULL,
    &rq_description
    };

static DEVICE *rq_devmap[RQ_NUMCT] = {
    &rq_dev, &rqb_dev, &rqc_dev, &rqd_dev
    };

static MSC *rq_ctxmap[RQ_NUMCT] = {
    &rq_ctx, &rqb_ctx, &rqc_ctx, &rqd_ctx
    };

/* I/O dispatch routines, I/O addresses 17772150 - 17772152

   base + 0     IP      read/write
   base + 2     SA      read/write
*/

t_stat rq_rd (int32 *data, int32 PA, int32 access)
{
int32 cidx = rq_map_pa ((uint32) PA);
MSC *cp = rq_ctxmap[cidx];
DEVICE *dptr = rq_devmap[cidx];

sim_debug(DBG_REG, dptr, "rq_rd(PA=0x%08X [%s], access=%d)\n", PA, ((PA >> 1) & 01) ? "IP" : "SA", access);

if (cidx < 0)
    return SCPE_IERR;
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* IP */
        *data = 0;                                      /* reads zero */
        if (cp->csta == CST_S3_PPB)                     /* waiting for poll? */
            rq_step4 (cp);
        else if (cp->csta == CST_UP) {                  /* if up */
            sim_debug (DBG_REQ, dptr, "poll started, PC=%X\n", OLDPC);
            cp->pip = 1;                                /* poll host */
            sim_activate (dptr->units + RQ_QUEUE, rq_qtime);
            }
        break;

    case 1:                                             /* SA */
        *data = cp->sa;
        break;
        }
return SCPE_OK;
}

t_stat rq_wr (int32 data, int32 PA, int32 access)
{
int32 cidx = rq_map_pa ((uint32) PA);
MSC *cp = rq_ctxmap[cidx];
DEVICE *dptr = rq_devmap[cidx];

if (cidx < 0)
    return SCPE_IERR;

sim_debug(DBG_REG, dptr, "rq_wr(PA=0x%08X [%s], access=%d)\n", PA, ((PA >> 1) & 01) ? "IP" : "SA", access);

switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* IP */
        rq_reset (rq_devmap[cidx]);                     /* init device */
        sim_debug (DBG_REQ, dptr, "initialization started\n");
        break;

    case 1:                                             /* SA */
        cp->saw = data;
        if (cp->csta < CST_S4)                          /* stages 1-3 */
            sim_activate (dptr->units + RQ_QUEUE, rq_itime);
        else if (cp->csta == CST_S4)                    /* stage 4 (fast) */
            sim_activate (dptr->units + RQ_QUEUE, rq_itime4);
        break;
        }

return SCPE_OK;
}

/* Map physical address to device context */

int32 rq_map_pa (uint32 pa)
{
int32 i;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < RQ_NUMCT; i++) {                        /* loop thru ctrls */
    dptr = rq_devmap[i];                                /* get device */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((pa >= dibp->ba) &&                             /* in range? */
        (pa < (dibp->ba + dibp->lnt)))
        return i;                                       /* return ctrl idx */
    }
return -1;
}

/* Transition to step 4 - init communications region */

t_bool rq_step4 (MSC *cp)
{
int32 i, lnt;
uint32 base;
uint16 zero[SA_COMM_MAX >> 1];

cp->rq.ioff = SA_COMM_RI;                               /* set intr offset */
cp->rq.ba = cp->comm;                                   /* set rsp q base */
cp->rq.lnt = SA_S1H_RQ (cp->s1dat) << 2;                /* get resp q len */
cp->cq.ioff = SA_COMM_CI;                               /* set intr offset */
cp->cq.ba = cp->comm + cp->rq.lnt;                      /* set cmd q base */
cp->cq.lnt = SA_S1H_CQ (cp->s1dat) << 2;                /* get cmd q len */
cp->cq.idx = cp->rq.idx = 0;                            /* clear q idx's */
if (cp->prgi)
    base = cp->comm + SA_COMM_QQ;
else base = cp->comm + SA_COMM_CI;
lnt = cp->comm + cp->cq.lnt + cp->rq.lnt - base;        /* comm lnt */
if (lnt > SA_COMM_MAX)                                  /* paranoia */
    lnt = SA_COMM_MAX;
for (i = 0; i < (lnt >> 1); i++)                        /* clr buffer */
    zero[i] = 0;
if (Map_WriteW (base, lnt, zero))                       /* zero comm area */
    return rq_fatal (cp, PE_QWE);                       /* error? */
cp->sa = SA_S4 |                                        /* send step 4 */
    (ctlr_tab[cp->ctype].uqpm << SA_S4C_V_MOD) |
    (RQ_SVER << SA_S4C_V_VER);
cp->csta = CST_S4;                                      /* set step 4 */
rq_init_int (cp);                                       /* poke host */
return OK;
}

/* Queue service - invoked when any of the queues (host queue, unit
   queues, response queue) require servicing.  Also invoked during
   initialization to provide some delay to the next step.

   Process at most one item off each unit queue
   If the unit queues were empty, process at most one item off the host queue
   Process at most one item off the response queue

   If all queues are idle, terminate thread
*/

t_stat rq_quesvc (UNIT *uptr)
{
int32 i, cnid;
int32 pkt = 0;
UNIT *nuptr;
MSC *cp = rq_ctxmap[uptr->cnum];
DEVICE *dptr = rq_devmap[uptr->cnum];
DIB *dibp = (DIB *) dptr->ctxt;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_quesvc\n");

if (cp->csta < CST_UP) {                                /* still init? */

    sim_debug(DBG_INI, dptr, "CSTA=%d, SAW=0x%X\n", cp->csta, cp->saw);

    switch (cp->csta) {                                 /* controller state? */

    case CST_S1:                                        /* need S1 reply */
        if (cp->saw & SA_S1H_VL) {                      /* valid? */
            if (cp->saw & SA_S1H_WR) {                  /* wrap? */
                cp->sa = cp->saw;                       /* echo data */
                cp->csta = CST_S1_WR;                   /* endless loop */
                }
            else {
                cp->s1dat = cp->saw;                    /* save data */
                dibp->vec = (cp->s1dat & SA_S1H_VEC) << 2; /* get vector */
                if (dibp->vec)                          /* if nz, bias */
                    dibp->vec = dibp->vec + VEC_Q;
                cp->sa = SA_S2 | SA_S2C_PT | SA_S2C_EC (cp->s1dat);
                cp->csta = CST_S2;                      /* now in step 2 */
                rq_init_int (cp);                       /* intr if req */
                }
            }                                           /* end if valid */
        break;

    case CST_S1_WR:                                     /* wrap mode */
        cp->sa = cp->saw;                               /* echo data */
        break;

    case CST_S2:                                        /* need S2 reply */
        cp->comm = cp->saw & SA_S2H_CLO;                /* get low addr */
        cp->prgi = cp->saw & SA_S2H_PI;                 /* get purge int */
        cp->sa = SA_S3 | SA_S3C_EC (cp->s1dat);
        cp->csta = CST_S3;                              /* now in step 3 */
        rq_init_int (cp);                               /* intr if req */
        break;

    case CST_S3:                                        /* need S3 reply */
        cp->comm = ((cp->saw & SA_S3H_CHI) << 16) | cp->comm;
        if (cp->saw & SA_S3H_PP) {                      /* purge/poll test? */
            cp->sa = 0;                                 /* put 0 */
            cp->csta = CST_S3_PPA;                      /* wait for 0 write */
            }
        else rq_step4 (cp);                             /* send step 4 */
        break;

    case CST_S3_PPA:                                    /* need purge test */
        if (cp->saw)                                    /* data not zero? */
            rq_fatal (cp, PE_PPF);
        else cp->csta = CST_S3_PPB;                     /* wait for poll */
        break;

    case CST_S4:                                        /* need S4 reply */
        if (cp->saw & SA_S4H_GO) {                      /* go set? */
            sim_debug (DBG_REQ, dptr, "initialization complete\n");
            cp->csta = CST_UP;                          /* we're up */
            cp->sa = 0;                                 /* clear SA */
            sim_activate_after (dptr->units + RQ_TIMER, 1000000);
            if ((cp->saw & SA_S4H_LF)
                && cp->perr) rq_plf (cp, cp->perr);
            cp->perr = 0;
            }
        break;
        }                                               /* end switch */  
                      
    return SCPE_OK;
    }                                                   /* end if */

for (i = 0; i < RQ_NUMDR; i++) {                        /* chk unit q's */
    nuptr = dptr->units + i;                            /* ptr to unit */
    if (nuptr->cpkt || (nuptr->pktq == 0))
        continue;
    pkt = rq_deqh (cp, &nuptr->pktq);                   /* get top of q */
    if (!rq_mscp (cp, pkt, FALSE))                      /* process */
        return SCPE_OK;
    }
if ((pkt == 0) && cp->pip) {                            /* polling? */
    if (!rq_getpkt (cp, &pkt))                          /* get host pkt */
        return SCPE_OK;
    if (pkt) {                                          /* got one? */
        sim_debug (DBG_REQ, dptr, "cmd=%04X(%3s), mod=%04X, unit=%d, bc=%04X%04X, ma=%04X%04X, lbn=%04X%04X\n", 
                cp->pak[pkt].d[CMD_OPC], rq_cmdname[cp->pak[pkt].d[CMD_OPC]&0x3f],
                cp->pak[pkt].d[CMD_MOD], cp->pak[pkt].d[CMD_UN],
                cp->pak[pkt].d[RW_BCH], cp->pak[pkt].d[RW_BCL],
                cp->pak[pkt].d[RW_BAH], cp->pak[pkt].d[RW_BAL],
                cp->pak[pkt].d[RW_LBNH], cp->pak[pkt].d[RW_LBNL]);
        if (GETP (pkt, UQ_HCTC, TYP) != UQ_TYP_SEQ)     /* seq packet? */
            return rq_fatal (cp, PE_PIE);               /* no, term thread */
        cnid = GETP (pkt, UQ_HCTC, CID);                /* get conn ID */
        if (cnid == UQ_CID_MSCP) {                      /* MSCP packet? */
            if (!rq_mscp (cp, pkt, TRUE))               /* proc, q non-seq */
                return SCPE_OK;
            }
        else if (cnid == UQ_CID_DUP) {                  /* DUP packet? */
            rq_putr (cp, pkt, OP_END, 0, ST_CMD | I_OPCD, RSP_LNT, UQ_TYP_SEQ);
            if (!rq_putpkt (cp, pkt, TRUE))             /* ill cmd */
                return SCPE_OK;
            }
        else return rq_fatal (cp, PE_ICI);              /* no, term thread */
        }                                               /* end if pkt */
    else cp->pip = 0;                                   /* discontinue poll */
    }                                                   /* end if pip */
if (cp->rspq) {                                         /* resp q? */
    pkt = rq_deqh (cp, &cp->rspq);                      /* get top of q */
    if (!rq_putpkt (cp, pkt, FALSE))                    /* send to host */
        return SCPE_OK;
    sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_quesvc - rq_putpkt failed - 1\n");
    }                                                   /* end if resp q */
if (pkt)                                                /* more to do? */
    sim_activate (uptr, rq_qtime);
return SCPE_OK;                                         /* done */
}

/* Clock service (roughly once per second) */

t_stat rq_tmrsvc (UNIT *uptr)
{
int32 i;
UNIT *nuptr;
MSC *cp = rq_ctxmap[uptr->cnum];
DEVICE *dptr = rq_devmap[uptr->cnum];

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_tmrsvc\n");
sim_activate_after (uptr, 1000000);                     /* reactivate */
for (i = 0; i < RQ_NUMDR; i++) {                        /* poll */
    nuptr = dptr->units + i;
    if ((nuptr->flags & UNIT_ATP) &&                    /* ATN pending? */
        (nuptr->flags & UNIT_ATT) &&                    /* still online? */
        (cp->cflgs & CF_ATN)) {                         /* wanted? */
        if (!rq_una (cp, i))
            return SCPE_OK;
        }
    nuptr->flags = nuptr->flags & ~UNIT_ATP;
    }
if ((cp->hat > 0) && (--cp->hat == 0))                  /* host timeout? */
    rq_fatal (cp, PE_HAT);                              /* fatal err */ 
return SCPE_OK;
}

/* MSCP packet handling */

t_bool rq_mscp (MSC *cp, int32 pkt, t_bool q)
{
uint32 sts, cmd = GETP (pkt, CMD_OPC, OPC);

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_mscp - %s\n", q? "Queue" : "No Queue");

switch (cmd) {

    case OP_ABO:                                        /* abort */
        return rq_abo (cp, pkt, q);

    case OP_AVL:                                        /* avail */
        return rq_avl (cp, pkt, q);

    case OP_FMT:                                        /* format */
        return rq_fmt (cp, pkt, q);

    case OP_GCS:                                        /* get cmd status */
        return rq_gcs (cp, pkt, q);

    case OP_GUS:                                        /* get unit status */
        return rq_gus (cp, pkt, q);

    case OP_ONL:                                        /* online */
        return rq_onl (cp, pkt, q);

    case OP_SCC:                                        /* set ctrl char */
        return rq_scc (cp, pkt, q);

    case OP_SUC:                                        /* set unit char */
        return rq_suc (cp, pkt, q);

    case OP_ACC:                                        /* access */
    case OP_CMP:                                        /* compare */
    case OP_ERS:                                        /* erase */
    case OP_RD:                                         /* read */
    case OP_WR:                                         /* write */
        return rq_rw (cp, pkt, q);

    case OP_CCD:                                        /* nops */
    case OP_DAP:
    case OP_FLU:
        cmd = cmd | OP_END;                             /* set end flag */
        sts = ST_SUC;                                   /* success */
        break;

    default:
        cmd = OP_END;                                   /* set end op */
        sts = ST_CMD | I_OPCD;                          /* ill op */
        break;
        }

rq_putr (cp, pkt, cmd, 0, sts, RSP_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Abort a command - 1st parameter is ref # of cmd to abort */

t_bool rq_abo (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 ref = GETP32 (pkt, ABO_REFL);                    /* cmd ref # */
int32 tpkt, prv;
UNIT *uptr;
DEVICE *dptr = rq_devmap[cp->cnum];

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_abo\n");

tpkt = 0;                                               /* set no mtch */
if ((uptr = rq_getucb (cp, lu))) {                      /* get unit */
    if (uptr->cpkt &&                                   /* curr pkt? */
        (GETP32 (uptr->cpkt, CMD_REFL) == ref)) {       /* match ref? */
        tpkt = uptr->cpkt;                              /* save match */
        uptr->cpkt = 0;                                 /* gonzo */
        sim_cancel (uptr);                              /* cancel unit */
        sim_activate (dptr->units + RQ_QUEUE, rq_qtime);
        }
    else if (uptr->pktq &&                              /* head of q? */
        (GETP32 (uptr->pktq, CMD_REFL) == ref)) {       /* match ref? */
        tpkt = uptr->pktq;                              /* save match */
        uptr->pktq = cp->pak[tpkt].link;                /* unlink */
        }
    else if ((prv = uptr->pktq)) {                      /* srch pkt q */
        while ((tpkt = cp->pak[prv].link)) {            /* walk list */
            if (GETP32 (tpkt, RSP_REFL) == ref) {       /* match? unlink */
                cp->pak[prv].link = cp->pak[tpkt].link;
                break;
                }
            prv = tpkt;                                 /* no match, next */
            }
        }
    if (tpkt) {                                         /* found target? */
        uint32 tcmd = GETP (tpkt, CMD_OPC, OPC);        /* get opcode */
        rq_putr (cp, tpkt, tcmd | OP_END, 0, ST_ABO, RSP_LNT, UQ_TYP_SEQ);
        if (!rq_putpkt (cp, tpkt, TRUE))
            return ERR;
        }
    }                                                   /* end if unit */
rq_putr (cp, pkt, cmd | OP_END, 0, ST_SUC, ABO_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Unit available - set unit status to available - defer if q'd cmds */

t_bool rq_avl (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 mdf = cp->pak[pkt].d[CMD_MOD];                   /* modifier */
uint32 sts;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_avl\n");

if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if (q && uptr->cpkt) {                              /* need to queue? */
        rq_enqt (cp, &uptr->pktq, pkt);                 /* do later */
        return OK;
        }
    uptr->flags = uptr->flags & ~UNIT_ONL;              /* not online */
    if ((mdf & MD_SPD) && RQ_RMV (uptr))                /* unload of removable device */
        sim_disk_unload (uptr);
    uptr->uf = 0;                                       /* clr flags */
    sts = ST_SUC;                                       /* success */
    }
else sts = ST_OFL;                                      /* offline */
rq_putr (cp, pkt, cmd | OP_END, 0, sts, AVL_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Get command status - only interested in active xfr cmd */

t_bool rq_gcs (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 ref = GETP32 (pkt, GCS_REFL);                    /* ref # */
int32 tpkt;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_gcs\n");

if ((uptr = rq_getucb (cp, lu)) &&                      /* valid lu? */
    (tpkt = uptr->cpkt) &&                              /* queued pkt? */
    (GETP32 (tpkt, CMD_REFL) == ref) &&                 /* match ref? */
    (GETP (tpkt, CMD_OPC, OPC) >= OP_ACC)) {            /* rd/wr cmd? */
    cp->pak[pkt].d[GCS_STSL] = cp->pak[tpkt].d[RW_WBCL];
    cp->pak[pkt].d[GCS_STSH] = cp->pak[tpkt].d[RW_WBCH];
    }
else {
    cp->pak[pkt].d[GCS_STSL] = 0;                       /* return 0 */
    cp->pak[pkt].d[GCS_STSH] = 0;
    }
rq_putr (cp, pkt, cmd | OP_END, 0, ST_SUC, GCS_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Get unit status */

t_bool rq_gus (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 dtyp, sts, rbpar;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_gus\n");

if (cp->pak[pkt].d[CMD_MOD] & MD_NXU) {                 /* next unit? */
    if (lu >= (cp->ubase + RQ_NUMDR)) {                 /* end of range? */
        lu = 0;                                         /* reset to 0 */
        cp->pak[pkt].d[RSP_UN] = lu;
        }
    }
if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    else if (uptr->flags & UNIT_ONL)                    /* online */
        sts = ST_SUC;
    else sts = ST_AVL;                                  /* avail */
    rq_putr_unit (cp, pkt, uptr, lu, FALSE);            /* fill unit fields */
    dtyp = GET_DTYPE (uptr->flags);                     /* get drive type */
    if (drv_tab[dtyp].rcts)                             /* ctrl bad blk? */
        rbpar = 1;
    else rbpar = 0;                                     /* fill geom, bblk */
    cp->pak[pkt].d[GUS_TRK] = drv_tab[dtyp].sect;
    cp->pak[pkt].d[GUS_GRP] = drv_tab[dtyp].tpg;
    cp->pak[pkt].d[GUS_CYL] = drv_tab[dtyp].gpc;
    cp->pak[pkt].d[GUS_UVER] = 0;
    cp->pak[pkt].d[GUS_RCTS] = drv_tab[dtyp].rcts;
    cp->pak[pkt].d[GUS_RBSC] =
        (rbpar << GUS_RB_V_RBNS) | (rbpar << GUS_RB_V_RCTC);
    }
else sts = ST_OFL;                                      /* offline */
cp->pak[pkt].d[GUS_SHUN] = lu;                          /* shadowing */
cp->pak[pkt].d[GUS_SHST] = 0;
rq_putr (cp, pkt, cmd | OP_END, 0, sts, GUS_LNT_D, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Unit online - defer if q'd commands */

t_bool rq_onl (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 sts;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_onl\n");

if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if (q && uptr->cpkt) {                              /* need to queue? */
        rq_enqt (cp, &uptr->pktq, pkt);                 /* do later */
        return OK;
        }
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    else if (uptr->flags & UNIT_ONL)                    /* already online? */
        sts = ST_SUC | SB_SUC_ON;
    else if (sim_disk_isavailable (uptr))
        {                                              /* mark online */
        sts = ST_SUC;
        uptr->flags = uptr->flags | UNIT_ONL;
        rq_setf_unit (cp, pkt, uptr);                   /* hack flags */
        }
    else
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    rq_putr_unit (cp, pkt, uptr, lu, TRUE);             /* set fields */
    }
else sts = ST_OFL;                                      /* offline */
cp->pak[pkt].d[ONL_SHUN] = lu;                          /* shadowing */
cp->pak[pkt].d[ONL_SHST] = 0;
rq_putr (cp, pkt, cmd | OP_END, 0, sts, ONL_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Set controller characteristics */

t_bool rq_scc (MSC *cp, int32 pkt, t_bool q)
{
int32 sts, cmd;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_scc\n");

if (cp->pak[pkt].d[SCC_MSV]) {                          /* MSCP ver = 0? */
    sts = ST_CMD | I_VRSN;                              /* no, lose */
    cmd = 0;
    }
else {
    sts = ST_SUC;                                       /* success */
    cmd = GETP (pkt, CMD_OPC, OPC);                     /* get opcode */
    cp->cflgs = (cp->cflgs & CF_RPL) |                  /* hack ctrl flgs */
        cp->pak[pkt].d[SCC_CFL];
    if ((cp->htmo = cp->pak[pkt].d[SCC_TMO]))           /* set timeout */
        cp->htmo = cp->htmo + 2;                        /* if nz, round up */
    cp->pak[pkt].d[SCC_CFL] = cp->cflgs;                /* return flags */
    cp->pak[pkt].d[SCC_TMO] = RQ_DCTMO;                 /* ctrl timeout */
    cp->pak[pkt].d[SCC_VER] = (RQ_HVER << SCC_VER_V_HVER) |
        (RQ_SVER << SCC_VER_V_SVER);
    cp->pak[pkt].d[SCC_CIDA] = 0;                       /* ctrl ID */
    cp->pak[pkt].d[SCC_CIDB] = 0;
    cp->pak[pkt].d[SCC_CIDC] = 0;
    cp->pak[pkt].d[SCC_CIDD] = (RQ_CLASS << SCC_CIDD_V_CLS) |
        (ctlr_tab[cp->ctype].model << SCC_CIDD_V_MOD);
    cp->pak[pkt].d[SCC_MBCL] = 0;                       /* max bc */
    cp->pak[pkt].d[SCC_MBCH] = 0;
    }
rq_putr (cp, pkt, cmd | OP_END, 0, sts, SCC_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}
    
/* Set unit characteristics - defer if q'd commands */

t_bool rq_suc (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 sts;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_suc\n");

if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if (q && uptr->cpkt) {                              /* need to queue? */
        rq_enqt (cp, &uptr->pktq, pkt);                 /* do later */
        return OK;
        }
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    else {                                              /* avail or onl */
        sts = ST_SUC;
        rq_setf_unit (cp, pkt, uptr);                   /* hack flags */
        }
    rq_putr_unit (cp, pkt, uptr, lu, TRUE);             /* set fields */
    }
else sts = ST_OFL;                                      /* offline */
cp->pak[pkt].d[ONL_SHUN] = lu;                          /* shadowing */
cp->pak[pkt].d[ONL_SHST] = 0;
rq_putr (cp, pkt, cmd | OP_END, 0, sts, SUC_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Format command - floppies only */

t_bool rq_fmt (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 sts;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_fmt\n");

if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if (q && uptr->cpkt) {                              /* need to queue? */
        rq_enqt (cp, &uptr->pktq, pkt);                 /* do later */
        return OK;
        }
    if (GET_DTYPE (uptr->flags) != RX33_DTYPE)          /* RX33? */
        sts = ST_CMD | I_OPCD;                          /* no, err */
    else if ((cp->pak[pkt].d[FMT_IH] & 0100000) == 0)   /* magic bit set? */
        sts = ST_CMD | I_FMTI;                          /* no, err */
    else if ((uptr->flags & UNIT_ATT) == 0)             /* offline? */
        sts = ST_OFL | SB_OFL_NV;                       /* no vol */
    else if (uptr->flags & UNIT_ONL) {                  /* online? */
        uptr->flags = uptr->flags & ~UNIT_ONL;
        uptr->uf = 0;                                   /* clear flags */
        sts = ST_AVL | SB_AVL_INU;                      /* avail, in use */
        }
    else if (RQ_WPH (uptr))                             /* write prot? */
        sts = ST_WPR | SB_WPR_HW;                       /* can't fmt */
    else sts = ST_SUC;                                  /*** for now ***/
    }
else sts = ST_OFL;                                      /* offline */
rq_putr (cp, pkt, cmd | OP_END, 0, sts, FMT_LNT, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Data transfer commands */

t_bool rq_rw (MSC *cp, int32 pkt, t_bool q)
{
uint32 lu = cp->pak[pkt].d[CMD_UN];                     /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 sts;
UNIT *uptr;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_rw(lu=%d, pkt=%d, queue=%s)\n", lu, pkt, q?"yes" : "no");

if ((uptr = rq_getucb (cp, lu))) {                      /* unit exist? */
    if (q && uptr->cpkt) {                              /* need to queue? */
        sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_rw - queued\n");
        rq_enqt (cp, &uptr->pktq, pkt);                 /* do later */
        return OK;
        }
    sts = rq_rw_valid (cp, pkt, uptr, cmd);             /* validity checks */
    if (sts == 0) {                                     /* ok? */
        uptr->cpkt = pkt;                               /* op in progress */
        cp->pak[pkt].d[RW_WBAL] = cp->pak[pkt].d[RW_BAL];
        cp->pak[pkt].d[RW_WBAH] = cp->pak[pkt].d[RW_BAH];
        cp->pak[pkt].d[RW_WBCL] = cp->pak[pkt].d[RW_BCL];
        cp->pak[pkt].d[RW_WBCH] = cp->pak[pkt].d[RW_BCH];
        cp->pak[pkt].d[RW_WBLL] = cp->pak[pkt].d[RW_LBNL];
        cp->pak[pkt].d[RW_WBLH] = cp->pak[pkt].d[RW_LBNH];
        cp->pak[pkt].d[RW_WMPL] = cp->pak[pkt].d[RW_MAPL];
        cp->pak[pkt].d[RW_WMPH] = cp->pak[pkt].d[RW_MAPH];
        uptr->iostarttime = sim_grtime();
        sim_activate (uptr, 0);                         /* activate */
        sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_rw - started\n");
        return OK;                                      /* done */
        }
    }
else sts = ST_OFL;                                      /* offline */
cp->pak[pkt].d[RW_BCL] = cp->pak[pkt].d[RW_BCH] = 0;    /* bad packet */
rq_putr (cp, pkt, cmd | OP_END, 0, sts, RW_LNT_D, UQ_TYP_SEQ);
return rq_putpkt (cp, pkt, TRUE);
}

/* Validity checks */

int32 rq_rw_valid (MSC *cp, int32 pkt, UNIT *uptr, uint32 cmd)
{
uint32 dtyp = GET_DTYPE (uptr->flags);                  /* get drive type */
uint32 lbn = GETP32 (pkt, RW_LBNL);                     /* get lbn */
uint32 bc = GETP32 (pkt, RW_BCL);                       /* get byte cnt */
uint32 maxlbn = (uint32)uptr->capac;                    /* get max lbn */

if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return (ST_OFL | SB_OFL_NV);                        /* offl no vol */
if ((uptr->flags & UNIT_ONL) == 0)                      /* not online? */
    return ST_AVL;                                      /* only avail */
if ((cmd != OP_ACC) && (cmd != OP_ERS) &&               /* 'real' xfer */
    (cp->pak[pkt].d[RW_BAL] & 1))                       /* odd address? */
    return (ST_HST | SB_HST_OA);                        /* host buf odd */
if (bc & 1)                                             /* odd byte cnt? */
    return (ST_HST | SB_HST_OC);
if (bc & 0xF0000000)                                    /* 'reasonable' bc? */
    return (ST_CMD | I_BCNT);
/* if (lbn & 0xF0000000) return (ST_CMD | I_LBN);     *//* 'reasonable' lbn? */
if (lbn >= maxlbn) {                                    /* accessing RCT? */
    if (lbn >= (maxlbn + drv_tab[dtyp].rcts))           /* beyond copy 1? */
        return (ST_CMD | I_LBN);                        /* lbn err */
    if (bc != RQ_NUMBY)                                 /* bc must be 512 */
        return (ST_CMD | I_BCNT);
    }
else if ((lbn + ((bc + (RQ_NUMBY - 1)) / RQ_NUMBY)) > maxlbn)
    return (ST_CMD | I_BCNT);                           /* spiral to RCT */
if ((cmd == OP_WR) || (cmd == OP_ERS)) {                /* write op? */
    if (lbn >= maxlbn)                                  /* accessing RCT? */
        return (ST_CMD | I_LBN);                        /* lbn err */
    if (uptr->uf & UF_WPS)                              /* swre wlk? */
        return (ST_WPR | SB_WPR_SW);
    if (RQ_WPH (uptr))                                  /* hwre wlk? */
        return (ST_WPR | SB_WPR_HW);
    }
return 0;                                               /* success! */
}

/* I/O completion callback */

void rq_io_complete (UNIT *uptr, t_stat status)
{
MSC *cp = rq_ctxmap[uptr->cnum];

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_io_complete(status=%d)\n", status);

uptr->io_status = status;
uptr->io_complete = 1;
/* Reschedule for the appropriate delay */
sim_activate_notbefore (uptr, uptr->iostarttime+rq_xtime);
}

/* Map buffer address */

uint32 rq_map_ba (uint32 ba, uint32 ma)
{
#if defined (VM_VAX)                                    /* VAX version */
int32 idx;
uint32 rg;

idx = (VA_GETVPN(ba) << 2);                            /* map register index */
rg = ReadL (ma + idx);                                 /* map register */
if (rg & PTE_V)                                        /* valid? */
    return ((rg & RQ_M_PFN) << VA_N_OFF) | (ba & VA_M_OFF);
#endif
return 0;
}

/* Read byte buffer from memory */

int32 rq_readb (uint32 ba, int32 bc, uint32 ma, uint8 *buf)
{
#if defined (VM_VAX)                                    /* VAX version */
int32 lbc, t, tbc = 0;
uint32 pba;

if (ba & RQ_MAPXFER) {                                  /* mapped xfer? */
    while (tbc < bc) {
        if (!(pba = rq_map_ba (ba, ma)))                /* get physical ba */
            return (bc - tbc);
        lbc = 0x200 - (ba & VA_M_OFF);                  /* bc for this tx */
        if (lbc > (bc - tbc)) lbc = (bc - tbc);
        t = Map_ReadB (pba, lbc, buf);
        tbc += (lbc - t);                               /* bytes xfer'd so far */
        if (t) return (bc - tbc);                       /* incomplete xfer? */
        ba += lbc;
        buf += lbc;
        }
    return 0;
    }
#endif
return Map_ReadB (ba, bc, buf);                         /* unmapped xfer */
}

/* Read word buffer from memory */

int32 rq_readw (uint32 ba, int32 bc, uint32 ma, uint16 *buf)
{
#if defined (VM_VAX)                                    /* VAX version */
int32 lbc, t, tbc = 0;
uint32 pba;

if (ba & RQ_MAPXFER) {                                  /* mapped xfer? */
    while (tbc < bc) {
        if (!(pba = rq_map_ba (ba, ma)))                /* get physical ba */
            return (bc - tbc);
        lbc = 0x200 - (ba & VA_M_OFF);                  /* bc for this tx */
        if (lbc > (bc - tbc)) lbc = (bc - tbc);
        t = Map_ReadW (pba, lbc, buf);
        tbc += (lbc - t);                               /* bytes xfer'd so far */
        if (t) return (bc - tbc);                       /* incomplete xfer? */
        ba += lbc;
        buf += (lbc >> 1);
        }
    return 0;
    }
#endif
return Map_ReadW (ba, bc, buf);                         /* unmapped xfer */
}

/* Write word buffer to memory */

int32 rq_writew (uint32 ba, int32 bc, uint32 ma, uint16 *buf)
{
#if defined (VM_VAX)                                    /* VAX version */
int32 lbc, t, tbc = 0;
uint32 pba;

if (ba & RQ_MAPXFER) {                                  /* mapped xfer? */
    while (tbc < bc) {
        if (!(pba = rq_map_ba (ba, ma)))                /* get physical ba */
            return (bc - tbc);
        lbc = 0x200 - (ba & VA_M_OFF);                  /* bc for this tx */
        if (lbc > (bc - tbc)) lbc = (bc - tbc);
        t = Map_WriteW (pba, lbc, buf);
        tbc += (lbc - t);                               /* bytes xfer'd so far */
        if (t) return (bc - tbc);                       /* incomplete xfer? */
        ba += lbc;
        buf += (lbc >> 1);
        }
    return 0;
    }
#endif
return Map_WriteW (ba, bc, buf);                        /* unmapped xfer */
}

/* Unit service for data transfer commands */

t_stat rq_svc (UNIT *uptr)
{
MSC *cp = rq_ctxmap[uptr->cnum];
uint32 i, t, tbc, abc, wwc;
uint32 err = 0;
int32 pkt = uptr->cpkt;                                 /* get packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* get cmd */
uint32 ba = GETP32 (pkt, RW_WBAL);                      /* buf addr */
uint32 bc = GETP32 (pkt, RW_WBCL);                      /* byte count */
uint32 bl = GETP32 (pkt, RW_WBLL);                      /* block addr */
uint32 ma = GETP32 (pkt, RW_WMPL);                      /* block addr */

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_svc(unit=%d, pkt=%d, cmd=%s, lbn=%0X, bc=%0x, phase=%s)\n",
           uptr-rq_devmap[cp->cnum]->units, pkt, rq_cmdname[cp->pak[pkt].d[CMD_OPC]&0x3f], bl, bc,
           uptr->io_complete ? "bottom" : "top");

if ((cp == NULL) || (pkt == 0))                         /* what??? */
    return STOP_RQ;
tbc = (bc > RQ_MAXFR)? RQ_MAXFR: bc;                    /* trim cnt to max */

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    rq_rw_end (cp, uptr, 0, ST_OFL | SB_OFL_NV);        /* offl no vol */
    return SCPE_OK;
    }
if (bc == 0) {                                          /* no xfer? */
    rq_rw_end (cp, uptr, 0, ST_SUC);                    /* ok by me... */
    return SCPE_OK;
    }

if ((cmd == OP_ERS) || (cmd == OP_WR)) {                /* write op? */
    if (RQ_WPH (uptr)) {
        rq_rw_end (cp, uptr, 0, ST_WPR | SB_WPR_HW);
        return SCPE_OK;
        }
    if (uptr->uf & UF_WPS) {
        rq_rw_end (cp, uptr, 0, ST_WPR | SB_WPR_SW);
        return SCPE_OK;
        }
    }

if (!uptr->io_complete) { /* Top End (I/O Initiation) Processing */
    if (cmd == OP_ERS) {                                /* erase? */
        wwc = ((tbc + (RQ_NUMBY - 1)) & ~(RQ_NUMBY - 1)) >> 1;
        memset (uptr->rqxb, 0, wwc * sizeof(uint16));   /* clr buf */
        sim_disk_data_trace(uptr, uptr->rqxb, bl, wwc << 1, "sim_disk_wrsect-ERS", DBG_DAT & rq_devmap[cp->cnum]->dctrl, DBG_REQ);
        err = sim_disk_wrsect_a (uptr, bl, uptr->rqxb, NULL, (wwc << 1) / RQ_NUMBY, rq_io_complete);
        }

    else if (cmd == OP_WR) {                            /* write? */
        t = rq_readw (ba, tbc, ma, uptr->rqxb);         /* fetch buffer */
        if ((abc = tbc - t)) {                          /* any xfer? */
            wwc = ((abc + (RQ_NUMBY - 1)) & ~(RQ_NUMBY - 1)) >> 1;
            for (i = (abc >> 1); i < wwc; i++)
                ((uint16 *)(uptr->rqxb))[i] = 0;
            sim_disk_data_trace(uptr, uptr->rqxb, bl, wwc << 1, "sim_disk_wrsect-WR", DBG_DAT & rq_devmap[cp->cnum]->dctrl, DBG_REQ);
            err = sim_disk_wrsect_a (uptr, bl, uptr->rqxb, NULL, (wwc << 1) / RQ_NUMBY, rq_io_complete);
            }
        }

    else {  /* OP_RD & OP_CMP */
        err = sim_disk_rdsect_a (uptr, bl, uptr->rqxb, NULL, (tbc + RQ_NUMBY - 1) / RQ_NUMBY, rq_io_complete);
        }                                               /* end else read */
    return SCPE_OK;                                     /* done for now until callback */    
    }
else { /* Bottom End (After I/O processing) */
    uptr->io_complete = 0;
    err = uptr->io_status;
    if (cmd == OP_ERS) {                                /* erase? */
        }

    else if (cmd == OP_WR) {                            /* write? */
        t = rq_readw (ba, tbc, ma, uptr->rqxb);         /* fetch buffer */
        abc = tbc - t;                                  /* any xfer? */
        if (t) {                                        /* nxm? */
            PUTP32 (pkt, RW_WBCL, bc - abc);            /* adj bc */
            PUTP32 (pkt, RW_WBAL, ba + abc);            /* adj ba */
            if (rq_hbe (cp, uptr))                      /* post err log */
                rq_rw_end (cp, uptr, EF_LOG, ST_HST | SB_HST_NXM);  
            return SCPE_OK;                             /* end else wr */
            }
        }

    else {
        sim_disk_data_trace(uptr, uptr->rqxb, bl, tbc, "sim_disk_rdsect", DBG_DAT & rq_devmap[cp->cnum]->dctrl, DBG_REQ);
        if ((cmd == OP_RD) && !err) {                   /* read? */
            if ((t = rq_writew (ba, tbc, ma, uptr->rqxb))) {/* store, nxm? */
                PUTP32 (pkt, RW_WBCL, bc - (tbc - t));  /* adj bc */
                PUTP32 (pkt, RW_WBAL, ba + (tbc - t));  /* adj ba */
                if (rq_hbe (cp, uptr))                  /* post err log */
                    rq_rw_end (cp, uptr, EF_LOG, ST_HST | SB_HST_NXM);      
                return SCPE_OK;
                }
            }
        else if ((cmd == OP_CMP) && !err) {             /* compare? */
            uint8 dby, mby;
            for (i = 0; i < tbc; i++) {                 /* loop */
                if (rq_readb (ba + i, 1, ma, &mby)) {   /* fetch, nxm? */
                    PUTP32 (pkt, RW_WBCL, bc - i);      /* adj bc */
                    PUTP32 (pkt, RW_WBAL, bc - i);      /* adj ba */
                    if (rq_hbe (cp, uptr))              /* post err log */
                        rq_rw_end (cp, uptr, EF_LOG, ST_HST | SB_HST_NXM);
                    return SCPE_OK;
                    }
                dby = (((uint16 *)(uptr->rqxb))[i >> 1] >> ((i & 1)? 8: 0)) & 0xFF;
                if (mby != dby) {                       /* cmp err? */
                    PUTP32 (pkt, RW_WBCL, bc - i);      /* adj bc */
                    rq_rw_end (cp, uptr, 0, ST_CMP);    /* done */
                    return SCPE_OK;                     /* exit */
                    }                                   /* end if */
                }                                       /* end for */
            }                                           /* end else if */
        }                                               /* end else read */
    }                                                   /* end else bottom end */
if (err != 0) {                                         /* error? */
    if (rq_dte (cp, uptr, ST_DRV))                      /* post err log */
        rq_rw_end (cp, uptr, EF_LOG, ST_DRV);           /* if ok, report err */
    sim_disk_perror (uptr, "RQ I/O error");
    sim_disk_clearerr (uptr);
    return SCPE_IOERR;
    }
ba = ba + tbc;                                          /* incr bus addr */
bc = bc - tbc;                                          /* decr byte cnt */
bl = bl + ((tbc + (RQ_NUMBY - 1)) / RQ_NUMBY);          /* incr blk # */
PUTP32 (pkt, RW_WBAL, ba);                              /* update pkt */
PUTP32 (pkt, RW_WBCL, bc);
PUTP32 (pkt, RW_WBLL, bl);
if (bc)                                                 /* more? resched */
    sim_activate (uptr, 0);
else rq_rw_end (cp, uptr, 0, ST_SUC);                   /* done! */
return SCPE_OK;
}

/* Transfer command complete */

t_bool rq_rw_end (MSC *cp, UNIT *uptr, uint32 flg, uint32 sts)
{
int32 pkt = uptr->cpkt;                                 /* packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* get cmd */
uint32 bc = GETP32 (pkt, RW_BCL);                       /* init bc */
uint32 wbc = GETP32 (pkt, RW_WBCL);                     /* work bc */
DEVICE *dptr = rq_devmap[uptr->cnum];

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_rw_end\n");

uptr->cpkt = 0;                                         /* done */
PUTP32 (pkt, RW_BCL, bc - wbc);                         /* bytes processed */
cp->pak[pkt].d[RW_WBAL] = 0;                            /* clear temps */
cp->pak[pkt].d[RW_WBAH] = 0;
cp->pak[pkt].d[RW_WBCL] = 0;
cp->pak[pkt].d[RW_WBCH] = 0;
cp->pak[pkt].d[RW_WBLL] = 0;
cp->pak[pkt].d[RW_WBLH] = 0;
cp->pak[pkt].d[RW_WMPL] = 0;
cp->pak[pkt].d[RW_WMPH] = 0;
rq_putr (cp, pkt, cmd | OP_END, flg, sts, RW_LNT_D, UQ_TYP_SEQ); /* fill pkt */
if (!rq_putpkt (cp, pkt, TRUE))                         /* send pkt */
    return ERR;
if (uptr->pktq)                                         /* more to do? */
    sim_activate (dptr->units + RQ_QUEUE, rq_qtime);    /* activate thread */
return OK;
}

/* Data transfer error log packet */

t_bool rq_dte (MSC *cp, UNIT *uptr, uint32 err)
{
int32 pkt, tpkt;
uint32 lu, dtyp, lbn, ccyl, csurf, csect, t;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_dte\n");

if ((cp->cflgs & CF_THS) == 0)                          /* logging? */
    return OK;
if (!rq_deqf (cp, &pkt))                                /* get log pkt */
    return ERR;
tpkt = uptr->cpkt;                                      /* rw pkt */
lu = cp->pak[tpkt].d[CMD_UN];                           /* unit # */
lbn = GETP32 (tpkt, RW_WBLL);                           /* recent LBN */
dtyp = GET_DTYPE (uptr->flags);                         /* drv type */
if (drv_tab[dtyp].flgs & RQDF_SDI)                      /* SDI? ovhd @ end */
    t = 0;
else t = (drv_tab[dtyp].xbn + drv_tab[dtyp].dbn) /      /* ovhd cylinders */
    (drv_tab[dtyp].sect * drv_tab[dtyp].surf);
ccyl = t + (lbn / drv_tab[dtyp].cyl);                   /* curr real cyl */
t = lbn % drv_tab[dtyp].cyl;                            /* trk relative blk */
csurf = t / drv_tab[dtyp].surf;                         /* curr surf */
csect = t % drv_tab[dtyp].surf;                         /* curr sect */

cp->pak[pkt].d[ELP_REFL] = cp->pak[tpkt].d[CMD_REFL];   /* copy cmd ref */
cp->pak[pkt].d[ELP_REFH] = cp->pak[tpkt].d[CMD_REFH];
cp->pak[pkt].d[ELP_UN] = lu;                            /* copy unit */
cp->pak[pkt].d[ELP_SEQ] = 0;                            /* clr seq # */
cp->pak[pkt].d[DTE_CIDA] = 0;                           /* ctrl ID */
cp->pak[pkt].d[DTE_CIDB] = 0;
cp->pak[pkt].d[DTE_CIDC] = 0;
cp->pak[pkt].d[DTE_CIDD] = (RQ_CLASS << DTE_CIDD_V_CLS) |
    (ctlr_tab[cp->ctype].model << DTE_CIDD_V_MOD);
cp->pak[pkt].d[DTE_VER] = (RQ_HVER << DTE_VER_V_HVER) |
    (RQ_SVER << DTE_VER_V_SVER);
cp->pak[pkt].d[DTE_MLUN] = lu;                          /* MLUN */
cp->pak[pkt].d[DTE_UIDA] = lu;                          /* unit ID */
cp->pak[pkt].d[DTE_UIDB] = 0;
cp->pak[pkt].d[DTE_UIDC] = 0;
cp->pak[pkt].d[DTE_UIDD] = (UID_DISK << DTE_UIDD_V_CLS) |
    (drv_tab[dtyp].mod << DTE_UIDD_V_MOD);
cp->pak[pkt].d[DTE_UVER] = 0;                           /* unit versn */
cp->pak[pkt].d[DTE_SCYL] = ccyl;                        /* cylinder */
cp->pak[pkt].d[DTE_VSNL] = 01234 + lu;                  /* vol ser # */
cp->pak[pkt].d[DTE_VSNH] = 0;
cp->pak[pkt].d[DTE_D1] = 0;
cp->pak[pkt].d[DTE_D2] = csect << DTE_D2_V_SECT;        /* geometry */
cp->pak[pkt].d[DTE_D3] = (ccyl << DTE_D3_V_CYL) |
    (csurf << DTE_D3_V_SURF);
rq_putr (cp, pkt, FM_SDE, LF_SNR, err, DTE_LNT, UQ_TYP_DAT);
return rq_putpkt (cp, pkt, TRUE);
}

/* Host bus error log packet */

t_bool rq_hbe (MSC *cp, UNIT *uptr)
{
int32 pkt, tpkt;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_hbe\n");

if ((cp->cflgs & CF_THS) == 0)                          /* logging? */
    return OK;
if (!rq_deqf (cp, &pkt))                                /* get log pkt */
    return ERR;
tpkt = uptr->cpkt;                                      /* rw pkt */
cp->pak[pkt].d[ELP_REFL] = cp->pak[tpkt].d[CMD_REFL];   /* copy cmd ref */
cp->pak[pkt].d[ELP_REFH] = cp->pak[tpkt].d[CMD_REFH];
cp->pak[pkt].d[ELP_UN] = cp->pak[tpkt].d[CMD_UN];       /* copy unit */
cp->pak[pkt].d[ELP_SEQ] = 0;                            /* clr seq # */
cp->pak[pkt].d[HBE_CIDA] = 0;                           /* ctrl ID */
cp->pak[pkt].d[HBE_CIDB] = 0;
cp->pak[pkt].d[HBE_CIDC] = 0;
cp->pak[pkt].d[HBE_CIDD] = (RQ_CLASS << DTE_CIDD_V_CLS) |
    (ctlr_tab[cp->ctype].model << DTE_CIDD_V_MOD);
cp->pak[pkt].d[HBE_VER] = (RQ_HVER << HBE_VER_V_HVER) | /* versions */
    (RQ_SVER << HBE_VER_V_SVER);
cp->pak[pkt].d[HBE_RSV] = 0;
cp->pak[pkt].d[HBE_BADL] = cp->pak[tpkt].d[RW_WBAL];    /* bad addr */
cp->pak[pkt].d[HBE_BADH] = cp->pak[tpkt].d[RW_WBAH];
rq_putr (cp, pkt, FM_BAD, LF_SNR, ST_HST | SB_HST_NXM, HBE_LNT, UQ_TYP_DAT);
return rq_putpkt (cp, pkt, TRUE);
}

/* Port last failure error log packet */

t_bool rq_plf (MSC *cp, uint32 err)
{
int32 pkt;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_plf\n");

if (!rq_deqf (cp, &pkt))                                /* get log pkt */
    return ERR;
cp->pak[pkt].d[ELP_REFL] = 0;                           /* ref = 0 */
cp->pak[pkt].d[ELP_REFH] = 0;
cp->pak[pkt].d[ELP_UN] = 0;                             /* no unit */
cp->pak[pkt].d[ELP_SEQ] = 0;                            /* no seq */
cp->pak[pkt].d[PLF_CIDA] = 0;                           /* cntl ID */
cp->pak[pkt].d[PLF_CIDB] = 0;
cp->pak[pkt].d[PLF_CIDC] = 0;
cp->pak[pkt].d[PLF_CIDD] = (RQ_CLASS << PLF_CIDD_V_CLS) |
    (ctlr_tab[cp->ctype].model << PLF_CIDD_V_MOD);
cp->pak[pkt].d[PLF_VER] = (RQ_SVER << PLF_VER_V_SVER) |
    (RQ_HVER << PLF_VER_V_HVER);
cp->pak[pkt].d[PLF_ERR] = err;
rq_putr (cp, pkt, FM_CNT, LF_SNR, ST_CNT, PLF_LNT, UQ_TYP_DAT);
cp->pak[pkt].d[UQ_HCTC] |= (UQ_CID_DIAG << UQ_HCTC_V_CID);
return rq_putpkt (cp, pkt, TRUE);
}

/* Unit now available attention packet */

t_bool rq_una (MSC *cp, int32 un)
{
int32 pkt;
uint32 lu = cp->ubase + un;
UNIT *uptr = rq_getucb (cp, lu);

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_una (Unit=%d)\n", lu);
if (uptr == NULL)                                       /* huh? */
    return OK;
if (!rq_deqf (cp, &pkt))                                /* get log pkt */
    return ERR;
cp->pak[pkt].d[RSP_REFL] = 0;                           /* ref = 0 */
cp->pak[pkt].d[RSP_REFH] = 0;
cp->pak[pkt].d[RSP_UN] = lu;
cp->pak[pkt].d[RSP_RSV] = 0;
rq_putr_unit (cp, pkt, uptr, lu, FALSE);                /* fill unit fields */
rq_putr (cp, pkt, OP_AVA, 0, 0, UNA_LNT, UQ_TYP_SEQ);   /* fill std fields */
return rq_putpkt (cp, pkt, TRUE);
}

/* List handling

   rq_deqf      -       dequeue head of free list (fatal err if none)
   rq_deqh      -       dequeue head of list
   rq_enqh      -       enqueue at head of list
   rq_enqt      -       enqueue at tail of list
*/

t_bool rq_deqf (MSC *cp, int32 *pkt)
{
*pkt = 0;
if (cp->freq == 0)                                      /* no free pkts?? */
    return rq_fatal (cp, PE_NSR);
cp->pbsy = cp->pbsy + 1;                                /* cnt busy pkts */
*pkt = cp->freq;                                        /* head of list */
cp->freq = cp->pak[cp->freq].link;                      /* next */
return OK;
}

int32 rq_deqh (MSC *cp, int32 *lh)
{
int32 ptr = *lh;                                        /* head of list */

if (ptr)                                                /* next */
    *lh = cp->pak[ptr].link;
return ptr;
}

void rq_enqh (MSC *cp, int32 *lh, int32 pkt)
{
if (pkt == 0)                                           /* any pkt? */
    return;
cp->pak[pkt].link = *lh;                                /* link is old lh */
*lh = pkt;                                              /* pkt is new lh */
return;
}

void rq_enqt (MSC *cp, int32 *lh, int32 pkt)
{
if (pkt == 0)                                           /* any pkt? */
    return;
cp->pak[pkt].link = 0;                                  /* it will be tail */
if (*lh == 0)                                           /* if empty, enqh */
    *lh = pkt;
else {
    uint32 ptr = *lh;                                   /* chase to end */
    while (cp->pak[ptr].link)
        ptr = cp->pak[ptr].link;
    cp->pak[ptr].link = pkt;                            /* enq at tail */
    }
return;
}

/* Packet and descriptor handling */

/* Get packet from command ring */

t_bool rq_getpkt (MSC *cp, int32 *pkt)
{
uint32 addr, desc;

*pkt = 0;
if (!rq_getdesc (cp, &cp->cq, &desc))                   /* get cmd desc */
    return ERR;
if ((desc & UQ_DESC_OWN) == 0) {                        /* none */
    *pkt = 0;                                           /* pkt = 0 */
    return OK;                                          /* no error */
    }
if (!rq_deqf (cp, pkt))                                 /* get cmd pkt */
    return ERR;
cp->hat = 0;                                            /* dsbl hst timer */
addr = desc & UQ_ADDR;                                  /* get Q22 addr */
if (Map_ReadW (addr + UQ_HDR_OFF, RQ_PKT_SIZE, cp->pak[*pkt].d))
    return rq_fatal (cp, PE_PRE);                       /* read pkt */
return rq_putdesc (cp, &cp->cq, desc);                  /* release desc */
}

/* Put packet to response ring - note the clever hack about credits.
   The controller sends all its credits to the host.  Thereafter, it
   supplies one credit for every response packet sent over.  Simple!
*/

t_bool rq_putpkt (MSC *cp, int32 pkt, t_bool qt)
{
uint32 addr, desc, lnt, cr;
DEVICE *dptr = rq_devmap[cp->cnum];

if (pkt == 0)                                           /* any packet? */
    return OK;
sim_debug (DBG_REQ, dptr, "rsp=%04X, sts=%04X\n", 
                           cp->pak[pkt].d[RSP_OPF], cp->pak[pkt].d[RSP_STS]);
if (!rq_getdesc (cp, &cp->rq, &desc))                   /* get rsp desc */
    return ERR;
if ((desc & UQ_DESC_OWN) == 0) {                        /* not valid? */
    if (qt)                                             /* normal? q tail */
        rq_enqt (cp, &cp->rspq, pkt);
    else rq_enqh (cp, &cp->rspq, pkt);                  /* resp q call */
    sim_activate (dptr->units + RQ_QUEUE, rq_qtime);    /* activate q thrd */
    return OK;
    }
addr = desc & UQ_ADDR;                                  /* get Q22 addr */
lnt = cp->pak[pkt].d[UQ_HLNT] - UQ_HDR_OFF;             /* size, with hdr */
if ((GETP (pkt, UQ_HCTC, TYP) == UQ_TYP_SEQ) &&         /* seq packet? */
    (GETP (pkt, CMD_OPC, OPC) & OP_END)) {              /* end packet? */
    cr = (cp->credits >= 14)? 14: cp->credits;          /* max 14 credits */
    cp->credits = cp->credits - cr;                     /* decr credits */
    cp->pak[pkt].d[UQ_HCTC] |= ((cr + 1) << UQ_HCTC_V_CR);
    }
if (Map_WriteW (addr + UQ_HDR_OFF, lnt, cp->pak[pkt].d))
    return rq_fatal (cp, PE_PWE);                       /* write pkt */
rq_enqh (cp, &cp->freq, pkt);                           /* pkt is free */
cp->pbsy = cp->pbsy - 1;                                /* decr busy cnt */
if (cp->pbsy == 0)                                      /* idle? strt hst tmr */
    cp->hat = cp->htmo;
return rq_putdesc (cp, &cp->rq, desc);                  /* release desc */
}

/* Get a descriptor from the host */

t_bool rq_getdesc (MSC *cp, struct uq_ring *ring, uint32 *desc)
{
uint32 addr = ring->ba + ring->idx;
uint16 d[2];

*desc = 0;
if (Map_ReadW (addr, 4, d))                             /* fetch desc */
    return rq_fatal (cp, PE_QRE);                       /* err? dead */
*desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
return OK;
}

/* Return a descriptor to the host, clearing owner bit
   If rings transitions from "empty" to "not empty" or "full" to
   "not full", and interrupt bit was set, interrupt the host.
   Actually, test whether previous ring entry was owned by host.
*/

t_bool rq_putdesc (MSC *cp, struct uq_ring *ring, uint32 desc)
{
uint32 prvd, newd = (desc & ~UQ_DESC_OWN) | UQ_DESC_F;
uint32 prva, addr = ring->ba + ring->idx;
uint16 d[2];

d[0] = newd & 0xFFFF;                                   /* 32b to 16b */
d[1] = (newd >> 16) & 0xFFFF;
if (Map_WriteW (addr, 4, d))                            /* store desc */
    return rq_fatal (cp, PE_QWE);                       /* err? dead */
if (desc & UQ_DESC_F) {                                 /* was F set? */
    if (ring->lnt <= 4)                                 /* lnt = 1? intr */
        rq_ring_int (cp, ring);
    else {                                              /* prv desc */
        prva = ring->ba + ((ring->idx - 4) & (ring->lnt - 1));
        if (Map_ReadW (prva, 4, d))                     /* read prv */
            return rq_fatal (cp, PE_QRE);
        prvd = ((uint32) d[0]) | (((uint32) d[1]) << 16);
        if (prvd & UQ_DESC_OWN)
            rq_ring_int (cp, ring);
        }
    }
ring->idx = (ring->idx + 4) & (ring->lnt - 1);
return OK;
}

/* Get unit descriptor for logical unit */

UNIT *rq_getucb (MSC *cp, uint32 lu)
{
DEVICE *dptr = rq_devmap[cp->cnum];
UNIT *uptr;

if ((lu < cp->ubase) || (lu >= (cp->ubase + RQ_NUMDR)))
    return NULL;
uptr = dptr->units + (lu % RQ_NUMDR);
if (uptr->flags & UNIT_DIS)
    return NULL;
return uptr;
}

/* Hack unit flags */

void rq_setf_unit (MSC *cp, int32 pkt, UNIT *uptr)
{
uptr->uf = cp->pak[pkt].d[ONL_UFL] & UF_MSK;            /* settable flags */
if ((cp->pak[pkt].d[CMD_MOD] & MD_SWP) &&               /* swre wrp enb? */
    (cp->pak[pkt].d[ONL_UFL] & UF_WPS))                 /* swre wrp on? */
    uptr->uf = uptr->uf | UF_WPS;                       /* simon says... */
return;
}

/* Unit response fields */

void rq_putr_unit (MSC *cp, int32 pkt, UNIT *uptr, uint32 lu, t_bool all)
{
uint32 dtyp = GET_DTYPE (uptr->flags);                  /* get drive type */
uint32 maxlbn = (uint32)uptr->capac;                    /* get max lbn */

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_putr_unit\n");

cp->pak[pkt].d[ONL_MLUN] = lu;                          /* unit */
cp->pak[pkt].d[ONL_UFL] = uptr->uf | UF_RPL | RQ_WPH (uptr) | RQ_RMV (uptr);
cp->pak[pkt].d[ONL_RSVL] = 0;                           /* reserved */
cp->pak[pkt].d[ONL_RSVH] = 0;
cp->pak[pkt].d[ONL_UIDA] = lu;                          /* UID low */
cp->pak[pkt].d[ONL_UIDB] = 0;
cp->pak[pkt].d[ONL_UIDC] = 0;
cp->pak[pkt].d[ONL_UIDD] = (UID_DISK << ONL_UIDD_V_CLS) |
    (drv_tab[dtyp].mod << ONL_UIDD_V_MOD);              /* UID hi */
PUTP32 (pkt, ONL_MEDL, drv_tab[dtyp].med);              /* media type */
if (all) {                                              /* if long form */
    PUTP32 (pkt, ONL_SIZL, maxlbn);                     /* user LBNs */
    cp->pak[pkt].d[ONL_VSNL] = 01234 + lu;              /* vol serial # */
    cp->pak[pkt].d[ONL_VSNH] = 0;
    }
return;
}

/* UQ_HDR and RSP_OP fields */

void rq_putr (MSC *cp, int32 pkt, uint32 cmd, uint32 flg,
          uint32 sts, uint32 lnt, uint32 typ)
{
cp->pak[pkt].d[RSP_OPF] = (cmd << RSP_OPF_V_OPC) |      /* set cmd, flg */
    (flg << RSP_OPF_V_FLG);
cp->pak[pkt].d[RSP_STS] = sts;
cp->pak[pkt].d[UQ_HLNT] = lnt;                          /* length */
cp->pak[pkt].d[UQ_HCTC] = (typ << UQ_HCTC_V_TYP) |      /* type, cid */
    (UQ_CID_MSCP << UQ_HCTC_V_CID);                     /* clr credits */
return;
}

/* Post interrupt during init */

void rq_init_int (MSC *cp)
{
if ((cp->s1dat & SA_S1H_IE) &&                          /* int enab & */
    (cp->s1dat & SA_S1H_VEC))                           /* ved set? int */
    rq_setint (cp);
return;
}

/* Post interrupt during putpkt - note that NXMs are ignored! */

void rq_ring_int (MSC *cp, struct uq_ring *ring)
{
uint32 iadr = cp->comm + ring->ioff;                    /* addr intr wd */
uint16 flag = 1;

Map_WriteW (iadr, 2, &flag);                            /* write flag */
if (cp->s1dat & SA_S1H_VEC)                             /* if enb, intr */
    rq_setint (cp);
return;
}

/* Set RQ interrupt */

void rq_setint (MSC *cp)
{
sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_setint\n");

cp->irq = 1;                                            /* set ctrl int */
SET_INT (RQ);                                           /* set master int */
return;
}

/* Clear RQ interrupt */

void rq_clrint (MSC *cp)
{
int32 i;
MSC *ncp;

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_clrint\n");

cp->irq = 0;                                            /* clr ctrl int */
for (i = 0; i < RQ_NUMCT; i++) {                        /* loop thru ctrls */
    ncp = rq_ctxmap[i];                                 /* get context */
    if (ncp->irq) {                                     /* other interrupt? */
        SET_INT (RQ);                                   /* yes, set master */
        return;
        }
    }
CLR_INT (RQ);                                           /* no, clr master */
return;
}

/* Return interrupt vector */

int32 rq_inta (void)
{
int32 i;
MSC *ncp;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < RQ_NUMCT; i++) {                        /* loop thru ctrl */
    ncp = rq_ctxmap[i];                                 /* get context */
    if (ncp->irq) {                                     /* ctrl int set? */
        dptr = rq_devmap[i];                            /* get device */
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        rq_clrint (ncp);                                /* clear int req */
        return dibp->vec;                               /* return vector */
        }
    }
return 0;                                               /* no intr req */
}

/* Fatal error */

t_bool rq_fatal (MSC *cp, uint32 err)
{
DEVICE *dptr = rq_devmap[cp->cnum];

sim_debug (DBG_TRC, rq_devmap[cp->cnum], "rq_fatal\n");

sim_debug (DBG_REQ, dptr, "fatal err=%X\n", err);
rq_reset (rq_devmap[cp->cnum]);                         /* reset device */
cp->sa = SA_ER | err;                                   /* SA = dead code */
cp->csta = CST_DEAD;                                    /* state = dead */
cp->perr = err;                                         /* save error */
return ERR;
}

/* Set/clear hardware write lock */

t_stat rq_set_wlk (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 dtyp = GET_DTYPE (uptr->flags);                  /* get drive type */

if (drv_tab[dtyp].flgs & RQDF_RO)                       /* not on read only */
    return SCPE_NOFNC;
return SCPE_OK;
}

/* Show write lock status */

t_stat rq_show_wlk (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 dtyp = GET_DTYPE (uptr->flags);                  /* get drive type */

if (drv_tab[dtyp].flgs & RQDF_RO)
    fprintf (st, "read only");
else if (uptr->flags & UNIT_WPRT)
    fprintf (st, "write locked");
else fprintf (st, "write enabled");
return SCPE_OK;
}

/* Set unit type (and capacity if user defined) */

t_stat rq_set_type (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 cap;
uint32 max = sim_toffset_64? RA8U_EMAXC: RA8U_MAXC;
t_stat r;

if ((val < 0) || ((val != RA8U_DTYPE) && cptr))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (cptr) {
    cap = (uint32) get_uint (cptr, 10, 0xFFFFFFFF, &r);
    if ((sim_switches & SWMASK ('L')) == 0)
        cap = cap * 1954;
    if ((r != SCPE_OK) || (cap < RA8U_MINC) || (cap > max))
        return SCPE_ARG;
    drv_tab[val].lbn = cap;
    }
uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (val << UNIT_V_DTYPE);
uptr->capac = (t_addr)drv_tab[val].lbn;
return SCPE_OK;
}

/* Show unit type */

t_stat rq_show_type (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "%s", drv_tab[GET_DTYPE (uptr->flags)].name);
return SCPE_OK;
}

/* Set controller type */

t_stat rq_set_ctype (UNIT *uptr, int32 val, char *cptr, void *desc)
{
MSC *cp = rq_ctxmap[uptr->cnum];

if (val < 0)
    return SCPE_ARG;
cp->ctype = val;
return SCPE_OK;
}

/* Show controller type */

t_stat rq_show_ctype (FILE *st, UNIT *uptr, int32 val, void *desc)
{
MSC *cp = rq_ctxmap[uptr->cnum];
fprintf (st, "%s", ctlr_tab[cp->ctype].name);
return SCPE_OK;
}

/* Device attach */

t_stat rq_attach (UNIT *uptr, char *cptr)
{
MSC *cp = rq_ctxmap[uptr->cnum];
t_stat r;

r = sim_disk_attach (uptr, cptr, RQ_NUMBY, sizeof (uint16), (uptr->flags & UNIT_NOAUTO), DBG_DSK, drv_tab[GET_DTYPE (uptr->flags)].name, 0, 0);
if (r != SCPE_OK)
    return r;

if ((cp->csta == CST_UP) && sim_disk_isavailable (uptr))
    uptr->flags = uptr->flags | UNIT_ATP;
return SCPE_OK;
}

/* Device detach */

t_stat rq_detach (UNIT *uptr)
{
t_stat r;

r = sim_disk_detach (uptr);                             /* detach unit */
if (r != SCPE_OK)
    return r;
uptr->flags = uptr->flags & ~(UNIT_ONL | UNIT_ATP);     /* clr onl, atn pend */
uptr->uf = 0;                                           /* clr unit flgs */
return SCPE_OK;
} 

/* Device reset */

t_stat rq_reset (DEVICE *dptr)
{
int32 i, j, cidx;
UNIT *uptr;
MSC *cp;
DIB *dibp = (DIB *) dptr->ctxt;

sim_debug (DBG_TRC, dptr, "rq_reset\n");

for (i = 0, cidx = -1; i < RQ_NUMCT; i++) {             /* find ctrl num */
    if (rq_devmap[i] == dptr)
        cidx = i;
    }
if (cidx < 0)                                           /* not found??? */
    return SCPE_IERR;
cp = rq_ctxmap[cidx];                                   /* get context */
cp->cnum = cidx;                                        /* init index */
if (cp->ctype == DEFAULT_CTYPE)
    cp->ctype = (UNIBUS? UDA50_CTYPE : RQDX3_CTYPE);

#if defined (VM_VAX)                                    /* VAX */
cp->ubase = 0;                                          /* unit base = 0 */
#else                                                   /* PDP-11 */
cp->ubase = cidx * RQ_NUMDR;                            /* init unit base */
#endif

cp->csta = CST_S1;                                      /* init stage 1 */
cp->s1dat = 0;                                          /* no S1 data */
dibp->vec = 0;                                          /* no vector */
cp->comm = 0;                                           /* no comm region */
if (UNIBUS)                                             /* Unibus? */
    cp->sa = SA_S1 | SA_S1C_DI | SA_S1C_MP;
else cp->sa = SA_S1 | SA_S1C_Q22 | SA_S1C_DI | SA_S1C_MP; /* init SA val */
cp->cflgs = CF_RPL;                                     /* ctrl flgs off */
cp->htmo = RQ_DHTMO;                                    /* default timeout */
cp->hat = cp->htmo;                                     /* default timer */
cp->cq.ba = cp->cq.lnt = cp->cq.idx = 0;                /* clr cmd ring */
cp->rq.ba = cp->rq.lnt = cp->rq.idx = 0;                /* clr rsp ring */
cp->credits = (RQ_NPKTS / 2) - 1;                       /* init credits */
cp->freq = 1;                                           /* init free list */
for (i = 0; i < RQ_NPKTS; i++) {                        /* all pkts free */
    if (i)
        cp->pak[i].link = (i + 1) & RQ_M_NPKTS;
    else cp->pak[i].link = 0;
    for (j = 0; j < RQ_PKT_SIZE_W; j++)
        cp->pak[i].d[j] = 0;
    }
cp->rspq = 0;                                           /* no q'd rsp pkts */
cp->pbsy = 0;                                           /* all pkts free */
cp->pip = 0;                                            /* not polling */
rq_clrint (cp);                                         /* clr intr req */
for (i = 0; i < (RQ_NUMDR + 2); i++) {                  /* init units */
    uptr = dptr->units + i;
    sim_cancel (uptr);                                  /* clr activity */
    sim_disk_reset (uptr);
    uptr->cnum = cidx;                                  /* set ctrl index */
    uptr->flags = uptr->flags & ~(UNIT_ONL | UNIT_ATP);
    uptr->uf = 0;                                       /* clr unit flags */
    uptr->cpkt = uptr->pktq = 0;                        /* clr pkt q's */
    uptr->rqxb = (uint16 *) realloc (uptr->rqxb, (RQ_MAXFR >> 1) * sizeof (uint16));
    if (uptr->rqxb == NULL)
        return SCPE_MEM;
    }
return auto_config (0, 0);                              /* run autoconfig */
}

/* Device bootstrap */

#if defined (VM_PDP11)

#define BOOT_START      016000                          /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {

    0042125,                        /* st: "UD" */

                                    /* Four step init process */

    0012706, 0016000,               /*   mov  #st,sp */
    0012700, 0000000,               /*   mov  #unit,r0 */
    0012701, 0172150,               /*   mov  #172150, r1   ; ip addr */
    0012704, 0016162,               /*   mov  #it, r4 */
    0012705, 0004000,               /*   mov  #4000,r5      ; s1 mask */
    0010102,                        /*   mov  r1,r2 */
    0005022,                        /*   clr  (r2)+         ; init */
    0005712,                        /* 10$: tst (r2)        ; err? */
    0100001,                        /*   bpl  20$ */
    0000000,                        /*   halt */
    0030512,                        /* 20$: bit r5,(r2)     ; step set? */
    0001773,                        /*   beq  10$           ; wait */
    0012412,                        /*   mov  (r4)+,(r2)    ; send next */
    0006305,                        /*   asl  r5            ; next mask */
    0100370,                        /*   bpl 10$            ; s4 done? */

                                    /* Send ONL, READ commands */

    0105714,                        /* 30$: tstb    (r4)    ; end tbl? */
    0001434,                        /*   beq  done          ; 0 = yes */
    0012702, 0007000,               /*   mov  #rpkt-4,r2    ; clr pkts */
    0005022,                        /* 40$: clr (r2)+ */
    0020227, 0007204,               /*   cmp  r2,#comm */
    0103774,                        /*   blo  40$ */
    0112437, 0007100,               /*   movb (r4)+,cpkt-4  ; set lnt */
    0110037, 0007110,               /*   movb r0,cpkt+4     ; set unit */
    0112437, 0007114,               /*   movb (r4)+,cpkt+10 ; set op */
    0112437, 0007121,               /*   movb (r4)+,cpkt+15 ; set param */
    0012722, 0007004,               /*   mov  #rpkt,(r2)+   ; rq desc */
    0010522,                        /*   mov  r5,(r2)+      ; rq own */
    0012722, 0007104,               /*   mov  #ckpt,(r2)+   ; cq desc */
    0010512,                        /*   mov  r5,(r2)       ; cq own */
    0024242,                        /*   cmp  -(r2),-(r2)   ; back up */
    0005711,                        /*   tst  (r1)          ; wake ctrl */
    0005712,                        /* 50$: tst (r2)        ; rq own clr? */
    0100776,                        /*   bmi  50$           ; wait */
    0005737, 0007016,               /*   tst  rpkt+12       ; stat ok? */
    0001743,                        /*   beq  30$           ; next cmd */
    0000000,                        /*   halt */

                                    /* Boot block read in, jump to 0 */

    0005011,                        /* done: clr (r1)       ; for M+ */
    0005003,                        /*   clr  r3 */
    0012704, BOOT_START+020,        /*   mov  #st+020,r4 */
    0005005,                        /*   clr  r5 */
    0005007,                        /*   clr  pc */

                                    /* Data */

    0100000,                        /* it: no ints, ring sz = 1 */
    0007204,                        /*    .word comm */
    0000000,                        /*    .word 0 */
    0000001,                        /*    .word 1 */
    0004420,                        /*   .byte 20,11 */
    0020000,                        /*   .byte 0,40 */
    0001041,                        /*   .byte 41,2 */
    0000000
    };

t_stat rq_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 *M;
DIB *dibp = (DIB *) dptr->ctxt;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & 3;
M[BOOT_CSR >> 1] = dibp->ba & DMASK;
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

t_stat rq_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}
#endif

/* Special show commands */

void rq_show_ring (FILE *st, struct uq_ring *rp)
{
uint32 i, desc;
uint16 d[2];

#if defined (VM_PDP11)
fprintf (st, "ring, base = %o, index = %d, length = %d\n",
     rp->ba, rp->idx >> 2, rp->lnt >> 2);
#else
fprintf (st, "ring, base = %x, index = %d, length = %d\n",
     rp->ba, rp->idx >> 2, rp->lnt >> 2);
#endif
for (i = 0; i < (rp->lnt >> 2); i++) {
    if (Map_ReadW (rp->ba + (i << 2), 4, d)) {
        fprintf (st, " %3d: non-existent memory\n", i);
        break;
        }
    desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
#if defined (VM_PDP11)
    fprintf (st, " %3d: %011o\n", i, desc);
#else
    fprintf (st, " %3d: %08x\n", i, desc);
#endif
    }
return;
}

void rq_show_pkt (FILE *st, MSC *cp, int32 pkt)
{
int32 i, j;
uint32 cr = GETP (pkt, UQ_HCTC, CR);
uint32 typ = GETP (pkt, UQ_HCTC, TYP);
uint32 cid = GETP (pkt, UQ_HCTC, CID);

fprintf (st, "packet %d, credits = %d, type = %d, cid = %d\n",
    pkt, cr, typ, cid);
for (i = 0; i < RQ_SH_MAX; i = i + RQ_SH_PPL) {
    fprintf (st, " %2d:", i);
    for (j = i; j < (i + RQ_SH_PPL); j++)
#if defined (VM_PDP11)
    fprintf (st, " %06o", cp->pak[pkt].d[j]);
#else
    fprintf (st, " %04x", cp->pak[pkt].d[j]);
#endif
    fprintf (st, "\n");
    }
return;
}

t_stat rq_show_unitq (FILE *st, UNIT *uptr, int32 val, void *desc)
{
MSC *cp = rq_ctxmap[uptr->cnum];
DEVICE *dptr = rq_devmap[uptr->cnum];
int32 pkt, u;

u = (int32) (uptr - dptr->units);
if (cp->csta != CST_UP) {
    fprintf (st, "Controller is not initialized\n");
    return SCPE_OK;
    }
if ((uptr->flags & UNIT_ONL) == 0) {
    if (uptr->flags & UNIT_ATT)
        fprintf (st, "Unit %d is available\n", u);
    else fprintf (st, "Unit %d is offline\n", u);
    return SCPE_OK;
    }
if (uptr->cpkt) {
    fprintf (st, "Unit %d current ", u);
    rq_show_pkt (st, cp, uptr->cpkt);
    if ((pkt = uptr->pktq)) {
        do {
            fprintf (st, "Unit %d queued ", u);
            rq_show_pkt (st, cp, pkt);
            } while ((pkt = cp->pak[pkt].link));
        }
    }
else fprintf (st, "Unit %d queues are empty\n", u);
return SCPE_OK;
}

t_stat rq_show_ctrl (FILE *st, UNIT *uptr, int32 val, void *desc)
{
MSC *cp = rq_ctxmap[uptr->cnum];
DEVICE *dptr = rq_devmap[uptr->cnum];
int32 i, pkt;

if (cp->csta != CST_UP) {
    fprintf (st, "Controller is not initialized\n");
    return SCPE_OK;
    }
if (val & RQ_SH_RI) {
    if (cp->pip)
        fprintf (st, "Polling in progress, host timer = %d\n", cp->hat);
    else fprintf (st, "Host timer = %d\n", cp->hat);
    fprintf (st, "Command ");
    rq_show_ring (st, &cp->cq);
    fprintf (st, "Response ");
    rq_show_ring (st, &cp->rq);
    }
if (val & RQ_SH_FR) {
    if ((pkt = cp->freq)) {
        for (i = 0; pkt != 0; i++, pkt = cp->pak[pkt].link) {
            if (i == 0)
                fprintf (st, "Free queue = %d", pkt);
            else if ((i % 16) == 0)
                fprintf (st, ",\n %d", pkt);
            else fprintf (st, ", %d", pkt);
            }
        fprintf (st, "\n");
        }
    else fprintf (st, "Free queue is empty\n");
    }
if (val & RQ_SH_RS) {
    if ((pkt = cp->rspq)) {
        do {
            fprintf (st, "Response ");
            rq_show_pkt (st, cp, pkt);
            } while ((pkt = cp->pak[pkt].link));
        }
    else fprintf (st, "Response queue is empty\n");
    }
if (val & RQ_SH_UN) {
    for (i = 0; i < RQ_NUMDR; i++)
        rq_show_unitq (st, dptr->units + i, 0, desc);
    }
return SCPE_OK;
}

t_stat rq_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "UDA50 MSCP Disk Controller (%s)\n\n", dptr->name);
fprintf (st, "The simulator implements four MSCP disk controllers, RQ, RQB, RQC, RQD.\n");
fprintf (st, "Initially, RQB, RQC, and RQD are disabled.  Each RQ controller simulates\n");
fprintf (st, "an MSCP disk controller with four drives.  The MSCP controller type can be\n");
fprintf (st, "specified as one of RQDX3, UDA50, KLESI or RUX50.  RQ options include the\n");
fprintf (st, "ability to set units write enabled or write locked, and to set the drive\n");
fprintf (st, "type to one of many disk types:\n");
fprint_set_help (st, dptr);
fprintf (st, "set RQn RAUSER{=n}        Set disk type to RA82 with n MB's\n");
fprintf (st, "set -L RQn RAUSER{=n}     Set disk type to RA82 with n LBN's\n\n");
fprintf (st, "The type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "RAUSER is a \"user specified\" disk; the user can specify the size of the\n");
fprintf (st, "disk in either MB (1000000 bytes) or logical block numbers (LBN's, 512 bytes\n");
fprintf (st, "each).  The minimum size is 5MB; the maximum size is 2GB without extended\n");
fprintf (st, "file support, 1TB with extended file support.\n\n");
fprintf (st, "The %s controllers support the BOOT command.\n\n", dptr->name);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\nWhile VMS is not timing sensitive, most of the BSD-derived operating systems\n");
fprintf (st, "(NetBSD, OpenBSD, etc) are.  The QTIME and XTIME parameters are set to values\n");
fprintf (st, "that allow these operating systems to run correctly.\n\n");
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         processed as\n");
fprintf (st, "    not attached  disk not ready\n");
fprintf (st, "    end of file   assume rest of disk is zero\n");
fprintf (st, "    OS I/O error  report error and stop\n");
fprintf (st, "\nDisk drives on the %s device can be attacbed to simulated storage in the\n", dptr->name);
fprintf (st, "following ways:\n\n");
sim_disk_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

char *rq_description (DEVICE *dptr)
{
static char buf[80];

sprintf (buf, "%s MSCP disk controller", ctlr_tab[rq_ctxmap[dptr->units->cnum]->ctype].name);
return buf;
}
