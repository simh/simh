/* vax_rq.c: RQDX3 disk controller simulator

   Copyright (c) 2001, Robert M Supnik
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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   rq		RQDX3 disk controller

   19-Dec-01	RMS	Added bigger drives
   17-Dec-01	RMS	Added queue process
*/

#if defined (USE_INT64)
#include "vax_defs.h"
#define VM_VAX		1
#define RQ_RDX		16
#define RQ_AINC		4
#define RQ_WID		32

#else
#include "pdp11_defs.h"
#define VM_PDP11	1
#define RQ_RDX		8
#define RQ_AINC		2
#define RQ_WID		16
#endif

#include "dec_uqssp.h"
#include "dec_mscp.h"

#define RQ_SH_MAX	24				/* max display wds */
#define RQ_SH_PPL	8				/* wds per line */
#define RQ_SH_DPL	4				/* desc per line */
#define RQ_SH_RI	001				/* show rings */
#define RQ_SH_FR	002				/* show free q */
#define RQ_SH_RS	004				/* show resp q */
#define RQ_SH_UN	010				/* show unit q's */

#define RQ_CLASS	1				/* RQ class */
#define RQ_MODEL	19				/* RQ model */
#define RQ_HVER		1				/* hardware version */
#define RQ_SVER		3				/* software version */
#define RQ_DHTMO	60				/* def host timeout */
#define RQ_DCTMO	120				/* def ctrl timeout */
#define RQ_NUMDR	4				/* # drives */
#define RQ_NUMBY	512				/* bytes per block */
#define RQ_MAXFR	(1 << 16)			/* max xfer */

#define UNIT_V_ONL	(UNIT_V_UF + 0)			/* online */
#define UNIT_V_WLK	(UNIT_V_UF + 1)			/* hwre write lock */
#define UNIT_V_ATP	(UNIT_V_UF + 2)			/* attn pending */
#define UNIT_V_DTYPE	(UNIT_V_UF + 3)			/* drive type */
#define UNIT_M_DTYPE	0xF
#define UNIT_ONL	(1 << UNIT_V_ONL)
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_ATP	(1 << UNIT_V_ATP)
#define UNIT_DTYPE	(UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_W_UF	8				/* user flags width */
#define GET_DTYPE(x)	(((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define cpkt		u3				/* current packet */
#define pktq		u4				/* packet queue */
#define uf		buf				/* unit flags */
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write prot */

#define CST_S1		0				/* init stage 1 */
#define CST_S1_WR	1				/* stage 1 wrap */
#define CST_S2		2				/* init stage 2 */
#define CST_S3		3				/* init stage 3 */
#define CST_S3_PPA	4				/* stage 3 sa wait */
#define CST_S3_PPB	5				/* stage 3 ip wait */
#define CST_S4		6				/* stage 4 */
#define CST_UP		7				/* online */
#define CST_DEAD	8				/* fatal error */

#define rq_comm		rq_rq.ba

#define ERR		0				/* must be SCPE_OK! */
#define OK		1

#define ER_NXM		0x1000				/* nxm err */
#define ER_PTE		0x0400				/* pte err */

/* Internal packet management.  The real RQDX3 manages its packets as true
   linked lists.  However, use of actual addresses in structures won't work
   with save/restore.  Accordingly, the packets are an arrayed structure,
   and links are actually subscripts.  To minimize complexity, packet[0]
   is not used (0 = end of list), and the number of packets must be a power
   of two.
*/

#define RQ_NPKTS	32				/* # packets (pwr of 2) */
#define RQ_M_NPKTS	(RQ_NPKTS - 1)			/* mask */
#define RQ_PKT_SIZE_W	32				/* payload size (wds) */
#define RQ_PKT_SIZE	(RQ_PKT_SIZE_W * sizeof (int16))

struct rqpkt {
	int16	link;					/* link to next */
	uint16	d[RQ_PKT_SIZE_W];  };			/* data */

/* Packet payload extraction and insertion */

#define GETP(p,w,f)	((rq_pkt[p].d[w] >> w##_V_##f) & w##_M_##f)
#define GETP32(p,w)	(((uint32) rq_pkt[p].d[w]) | \
			(((uint32) rq_pkt[p].d[(w)+1]) << 16))
#define PUTP32(p,w,x)	rq_pkt[p].d[w] = (x) & 0xFFFF; \
			rq_pkt[p].d[(w)+1] = ((x) >> 16) & 0xFFFF

/* Disk formats.  An RQDX3 consists of the following regions:

   XBNs		Extended blocks - contain information about disk format,
		also holds track being reformatted during bad block repl.
		Size = sectors/track + 1, replicated 3 times.
   DBNs		Diagnostic blocks - used by diagnostics.  Sized to pad
		out the XBNs to a cylinder boundary.
   LBNs		Logical blocks - contain user information.
   RCT		Replacement control table - first block contains status,
		second contains data from block being replaced, remaining
		contain information about replaced bad blocks.
		Size = RBNs/128 + 3, replicated 4-8 times.
   RBNs		Replacement blocks - used to replace bad blocks.

   The simulator does not need to perform bad block replacement; the
   information below is for simulating RCT reads, if required.

   Note that an RA drive has a different order: LBNs, RCT, XBN, DBN;
   the RBNs are spare blocks at the end of every track.
*/

#define RCT_OVHD	2				/* #ovhd blks */
#define RCT_ENTB	128				/* entries/blk */
#define RCT_END		0x80000000			/* marks RCT end */

/* The RQDX3 supports multiple disk drive types:

   type	sec	surf	cyl	tpg	gpc	RCT	LBNs
	
   RX50	10	1	80	5	16	-	800
   RX33	15	2	80	2	1	-	2400
   RD51	18	4	306	4	1	36*4	21600
   RD31	17	4	615	4	1	3*8	41560
   RD52	17	8	512	8	1	4*8	60480
   RD53	17	7	1024	7	1	5*8	138672
   RD54	17	15	1225	15	1	7*8	311200

   The simulator also supports larger drives that only existed
   on SDI controllers.  XBN, DBN, RCTS and RCTC are not known
   for the SDI drives and are not used by the simulator:

   RA82	57	15	1435	15	1	?*8	1216665
   RA72	51	20	1921?	20	1	?*8	1953300
   RA90	69	13	2656	13	1	?*8	2376153
   RA92	73	13	3101	13	1	?*8	2940951

   Each drive can be a different type.  The drive field in the
   unit flags specified the drive type and thus, indirectly,
   the drive size.  DISKS MUST BE DECLARED IN ASCENDING SIZE.
*/

#define RQDF_RMV	01				/* removable */
#define RQDF_RO		02				/* read only */
#define RQDF_SDI	04				/* SDI drive */

#define RX50_DTYPE	0
#define RX50_SECT	10
#define RX50_SURF	1
#define RX50_CYL	80
#define RX50_TPG	5
#define RX50_GPC	16
#define RX50_XBN	0
#define RX50_DBN	0
#define RX50_LBN	800
#define RX50_RCTS	0
#define RX50_RCTC	0
#define RX50_RBN	0
#define RX50_MOD	7
#define RX50_MED	0x25658032
#define RX50_FLGS	RQDF_RMV

#define RX33_DTYPE	1
#define RX33_SECT	15
#define RX33_SURF	2
#define RX33_CYL	80
#define RX33_TPG	2
#define RX33_GPC	1
#define RX33_XBN	0
#define RX33_DBN	0
#define RX33_LBN	2400
#define RX33_RCTS	0
#define RX33_RCTC	0
#define RX33_RBN	0
#define RX33_MOD	10
#define RX33_MED	0x25658021
#define RX33_FLGS	RQDF_RMV

#define RD51_DTYPE	2
#define RD51_SECT	18
#define RD51_SURF	4
#define RD51_CYL	306
#define RD51_TPG	4
#define RD51_GPC	1
#define RD51_XBN	57
#define RD51_DBN	87
#define RD51_LBN	21600
#define RD51_RCTS	36
#define RD51_RCTC	4
#define RD51_RBN	144
#define RD51_MOD	6
#define RD51_MED	0x25644033
#define RD51_FLGS	0

#define RD31_DTYPE	3
#define RD31_SECT	17
#define RD31_SURF	4
#define RD31_CYL	615				/* last unused */
#define RD31_TPG	RD31_SURF
#define RD31_GPC	1
#define RD31_XBN	54
#define RD31_DBN	14
#define RD31_LBN	41560
#define RD31_RCTS	3
#define RD31_RCTC	8
#define RD31_RBN	100
#define RD31_MOD	12
#define RD31_MED	0x2564401F
#define RD31_FLGS	0

#define RD52_DTYPE	4				/* Quantum params */
#define RD52_SECT	17
#define RD52_SURF	8
#define RD52_CYL	512
#define RD52_TPG	RD52_SURF
#define RD52_GPC	1
#define RD52_XBN	54
#define RD52_DBN	82
#define RD52_LBN	60480
#define RD52_RCTS	4
#define RD52_RCTC	8
#define RD52_RBN	168
#define RD52_MOD	8
#define RD52_MED	0x25644034
#define RD52_FLGS	0

#define RD53_DTYPE	5
#define RD53_SECT	17
#define RD53_SURF	8
#define RD53_CYL	1024				/* last unused */
#define RD53_TPG	RD53_SURF
#define RD53_GPC	1
#define RD53_XBN	54
#define RD53_DBN	82
#define RD53_LBN	138672
#define RD53_RCTS	5
#define RD53_RCTC	8
#define RD53_RBN	280
#define RD53_MOD	9
#define RD53_MED	0x25644035
#define RD53_FLGS	0

#define RD54_DTYPE	6
#define RD54_SECT	17
#define RD54_SURF	15
#define RD54_CYL	1225				/* last unused */
#define RD54_TPG	RD54_SURF
#define RD54_GPC	1
#define RD54_XBN	54
#define RD54_DBN	201
#define RD54_LBN	311200
#define RD54_RCTS	7
#define RD54_RCTC	8
#define RD54_RBN	609
#define RD54_MOD	13
#define RD54_MED	0x25644036
#define RD54_FLGS	0

#define RA82_DTYPE	7				/* SDI drive */
#define RA82_SECT	57				/* +1 spare/track */
#define RA82_SURF	15
#define RA82_CYL	1435				/* 0-1422 user */
#define RA82_TPG	RA82_SURF
#define RA82_GPC	1
#define RA82_XBN	3420				/* cyl 1427-1430 */
#define RA82_DBN	3420				/* cyl 1431-1434 */
#define RA82_LBN	1216665				/* 57*15*1423 */
#define RA82_RCTS	400				/* cyl 1423-1426 */
#define RA82_RCTC	8
#define RA82_RBN	21345				/* 1 *15*1423 */
#define RA82_MOD	11
#define RA82_MED	0x25641052
#define RA82_FLGS	RQDF_SDI

#define RRD40_DTYPE	8
#define RRD40_SECT	128
#define RRD40_SURF	1
#define RRD40_CYL	10400
#define RRD40_TPG	RRD40_SURF
#define RRD40_GPC	1
#define RRD40_XBN	0
#define RRD40_DBN	0
#define RRD40_LBN	1331200
#define RRD40_RCTS	0
#define RRD40_RCTC	0
#define RRD40_RBN	0
#define RRD40_MOD	26
#define RRD40_MED	0x25652228
#define RRD40_FLGS	(RQDF_RMV | RQDF_RO)

#define RA72_DTYPE	9				/* SDI drive */
#define RA72_SECT	51				/* +1 spare/trk */
#define RA72_SURF	20
#define RA72_CYL	1921				/* 0-1914 user */
#define RA72_TPG	RA72_SURF
#define RA72_GPC	1
#define RA72_XBN	2040				/* cyl 1917-1918? */
#define RA72_DBN	2040				/* cyl 1920-1921? */
#define RA72_LBN	1953300				/* 51*20*1915 */
#define RA72_RCTS	400				/* cyl 1915-1916? */
#define RA72_RCTC	5				/* ? */
#define RA72_RBN	38300				/* 1 *20*1915 */
#define RA72_MOD	37
#define RA72_MED	0x25641048
#define RA72_FLGS	RQDF_SDI

#define RA90_DTYPE	10				/* SDI drive */
#define RA90_SECT	69				/* +1 spare/trk */
#define RA90_SURF	13
#define RA90_CYL	2656				/* 0-2648 user */
#define RA90_TPG	RA90_SURF
#define RA90_GPC	1
#define RA90_XBN	1794				/* cyl 2651-2652? */
#define RA90_DBN	1794				/* cyl 2653-2654? */
#define RA90_LBN	2376153				/* 69*13*2649 */
#define RA90_RCTS	400				/* cyl 2649-2650? */
#define RA90_RCTC	6				/* ? */
#define RA90_RBN	34437				/* 1 *13*2649 */
#define RA90_MOD	19
#define RA90_MED	0x2564105A
#define RA90_FLGS	RQDF_SDI

#define RA92_DTYPE	11				/* SDI drive */
#define RA92_SECT	73				/* +1 spare/trk */
#define RA92_SURF	13
#define RA92_CYL	3101				/* 0-3098 user */
#define RA92_TPG	RA92_SURF
#define RA92_GPC	1
#define RA92_XBN	174				/* cyl 3100? */
#define RA92_DBN	775
#define RA92_LBN	2940951				/* 73*13*3099 */
#define RA92_RCTS	316				/* cyl 3099? */
#define RA92_RCTC	3				/* ? */
#define RA92_RBN	40287				/* 1 *13*3099 */
#define RA92_MOD	29
#define RA92_MED	0x2564105C
#define RA92_FLGS	RQDF_SDI

struct drvtyp {
	int32	sect;					/* sectors */
	int32	surf;					/* surfaces */
	int32	cyl;					/* cylinders */
	int32	tpg;					/* trk/grp */
	int32	gpc;					/* grp/cyl */
	int32	xbn;					/* XBN size */
	int32	dbn;					/* DBN size */
	int32	lbn;					/* LBN size */
	int32	rcts;					/* RCT size */
	int32	rctc;					/* RCT copies */
	int32	rbn;					/* RBNs */
	int32	mod;					/* MSCP model */
	int32	med;					/* MSCP media */
	int32	flgs;					/* flags */
};

#define RQ_DRV(d) \
	d##_SECT, d##_SURF, d##_CYL,  d##_TPG, \
	d##_GPC,  d##_XBN,  d##_DBN,  d##_LBN, \
	d##_RCTS, d##_RCTC, d##_RBN,  d##_MOD, \
	d##_MED, d##_FLGS
#define RQ_SIZE(d)	(d##_LBN * RQ_NUMBY)

static struct drvtyp drv_tab[] = {
	{ RQ_DRV (RX50) }, { RQ_DRV (RX33) },
	{ RQ_DRV (RD51) }, { RQ_DRV (RD31) },
	{ RQ_DRV (RD52) }, { RQ_DRV (RD53) },
	{ RQ_DRV (RD54) }, { RQ_DRV (RA82) },
	{ RQ_DRV (RRD40) }, { RQ_DRV (RA72) },
	{ RQ_DRV (RA90) }, { RQ_DRV (RA92) },
	{ 0 }  };

extern int32 int_req[IPL_HLVL];
extern int32 tmr_poll, clk_tps;
extern UNIT cpu_unit;
uint16 *rqxb = NULL;					/* xfer buffer */
uint32 rq_sa = 0;					/* status, addr */
uint32 rq_s1dat = 0;					/* S1 data */
uint32 rq_csta = 0;					/* ctrl state */
uint32 rq_perr = 0;					/* last error */
uint32 rq_cflgs = 0;					/* ctrl flags */
uint32 rq_prgi = 0;					/* purge int */
uint32 rq_pip = 0;					/* poll in progress */
struct uq_ring rq_cq = { SA_COMM_CI };			/* cmd ring */
struct uq_ring rq_rq = { SA_COMM_RI };			/* rsp ring */
struct rqpkt rq_pkt[RQ_NPKTS];				/* packet queue */
int32 rq_freq = 0;					/* free list */
int32 rq_rspq = 0;					/* resp list */
uint32 rq_pbsy = 0;					/* #busy pkts */
uint32 rq_credits = 0;					/* credits */
uint32 rq_hat = 0;					/* host timer */
uint32 rq_htmo = RQ_DHTMO;				/* host timeout */
int32 rq_qtime = 100;					/* queue time */
int32 rq_xtime = 500;					/* transfer time */
int32 rq_enb = 1;					/* device enable */

t_stat rq_svc (UNIT *uptr);
t_stat rq_tmrsvc (UNIT *uptr);
t_stat rq_quesvc (UNIT *uptr);
t_stat rq_reset (DEVICE *dptr);
t_stat rq_attach (UNIT *uptr, char *cptr);
t_stat rq_detach (UNIT *uptr);
t_stat rq_boot (int32 unitno);
t_stat rq_set_wlk (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rq_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rq_show_wlk (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_ctrl (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rq_show_unitq (FILE *st, UNIT *uptr, int32 val, void *desc);

t_bool rq_step4 (void);
t_bool rq_mscp (int32 pkt, t_bool q);
t_bool rq_abo (int32 pkt, t_bool q);
t_bool rq_avl (int32 pkt, t_bool q);
t_bool rq_fmt (int32 pkt, t_bool q);
t_bool rq_gcs (int32 pkt, t_bool q);
t_bool rq_gus (int32 pkt, t_bool q);
t_bool rq_onl (int32 pkt, t_bool q);
t_bool rq_rw (int32 pkt, t_bool q);
t_bool rq_scc (int32 pkt, t_bool q);
t_bool rq_suc (int32 pkt, t_bool q);
t_bool rq_plf (uint32 err);
t_bool rq_dte (UNIT *uptr, uint32 err);
t_bool rq_hbe (UNIT *uptr, uint32 err);
t_bool rq_una (UNIT *uptr);
t_bool rq_deqf (int32 *pkt);
int32 rq_deqh (int32 *lh);
void rq_enqh (int32 *lh, int32 pkt);
void rq_enqt (int32 *lh, int32 pkt);
t_bool rq_getpkt (int32 *pkt);
t_bool rq_putpkt (int32 pkt, t_bool qt);
t_bool rq_getdesc (struct uq_ring *ring, uint32 *desc);
t_bool rq_putdesc (struct uq_ring *ring, uint32 desc);
int32 rq_rw_valid (int32 pkt, UNIT *uptr, uint32 cmd);
t_bool rq_rw_end (UNIT *uptr, uint32 flg, uint32 sts);
void rq_putr (int32 pkt, uint32 cmd, uint32 flg, uint32 sts, uint32 lnt, uint32 typ);
void rq_putr_unit (int32 pkt, UNIT *uptr, uint32 lu, t_bool all);
void rq_setf_unit (int32 pkt, UNIT *uptr);
void rq_init_int (void);
void rq_ring_int (struct uq_ring *ring);
t_bool rq_fatal (uint32 err);
UNIT *rq_getucb (uint32 lu);

/* RQ data structures

   rq_dev	RQ device descriptor
   rq_unit	RQ unit list
   rq_reg	RQ register list
   rq_mod	RQ modifier list
*/

UNIT rq_unit[] = {
	{ UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
		(RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
	{ UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
		(RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
	{ UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
		(RD54_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RD54)) },
	{ UDATA (&rq_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
		(RX50_DTYPE << UNIT_V_DTYPE), RQ_SIZE (RX50)) },
	{ UDATA (&rq_tmrsvc, UNIT_DIS, 0) },
	{ UDATA (&rq_quesvc, UNIT_DIS, 0) }  };

#define RQ_TIMER	(RQ_NUMDR)
#define RQ_QUEUE	(RQ_TIMER + 1)

REG rq_reg[] = {
	{ GRDATA (SA, rq_sa, RQ_RDX, 16, 0) },
	{ GRDATA (S1DAT, rq_s1dat, RQ_RDX, 16, 0) },
	{ GRDATA (CQBA, rq_cq.ba, RQ_RDX, 22, 0) },
	{ GRDATA (CQLNT, rq_cq.lnt, RQ_RDX, 8, 2), REG_NZ },
	{ GRDATA (CQIDX, rq_cq.idx, RQ_RDX, 8, 2) },
	{ GRDATA (RQBA, rq_rq.ba, RQ_RDX, 22, 0) },
	{ GRDATA (RQLNT, rq_rq.lnt, RQ_RDX, 8, 2), REG_NZ },
	{ GRDATA (RQIDX, rq_rq.idx, RQ_RDX, 8, 2) },
	{ GRDATA (FREE, rq_freq, RQ_RDX, 5, 0) },
	{ GRDATA (RESP, rq_rspq, RQ_RDX, 5, 0) },
	{ GRDATA (PBSY, rq_pbsy, RQ_RDX, 5, 0) },
	{ GRDATA (CFLGS, rq_cflgs, RQ_RDX, 16, 0) },
	{ GRDATA (CSTA, rq_csta, RQ_RDX, 4, 0) },
	{ GRDATA (PERR, rq_perr, RQ_RDX, 9, 0) },
	{ GRDATA (CRED, rq_credits, RQ_RDX, 5, 0) },
	{ GRDATA (HAT, rq_hat, RQ_RDX, 16, 0) },
	{ GRDATA (HTMO, rq_htmo, RQ_RDX, 17, 0) },
	{ URDATA (CPKT, rq_unit[0].cpkt, RQ_RDX, 5, 0, RQ_NUMDR, 0) },
	{ URDATA (PKTQ, rq_unit[0].pktq, RQ_RDX, 5, 0, RQ_NUMDR, 0) },
	{ URDATA (UFLG, rq_unit[0].uf, RQ_RDX, 16, 0, RQ_NUMDR, 0) },
	{ URDATA (SFLG, rq_unit[0].flags, RQ_RDX, UNIT_W_UF, UNIT_V_UF-1,
		  RQ_NUMDR, REG_HRO) },
	{ FLDATA (PRGI, rq_prgi, 0), REG_HIDDEN },
	{ FLDATA (PIP, rq_pip, 0), REG_HIDDEN },
	{ FLDATA (INT, IREQ (RQ), INT_V_RQ) },
	{ DRDATA (QTIME, rq_qtime, 24), PV_LEFT + REG_NZ },
	{ DRDATA (XTIME, rq_xtime, 24), PV_LEFT + REG_NZ },
	{ BRDATA (PKTS, rq_pkt, RQ_RDX, 16, RQ_NPKTS * (RQ_PKT_SIZE_W + 1)) },
	{ FLDATA (*DEVENB, rq_enb, 0), REG_HRO },
	{ NULL }  };

MTAB rq_mod[] = {
	{ UNIT_WLK, 0, NULL, "ENABLED", &rq_set_wlk },
	{ UNIT_WLK, UNIT_WLK, NULL, "LOCKED", &rq_set_wlk },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, RQ_SH_RI, "RINGS", NULL,
		NULL, &rq_show_ctrl, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, RQ_SH_FR, "FREEQ", NULL,
		NULL, &rq_show_ctrl, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, RQ_SH_RS, "RESPQ", NULL,
		NULL, &rq_show_ctrl, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, RQ_SH_UN, "UNITQ", NULL,
		NULL, &rq_show_ctrl, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, -1, "ALL", NULL,
		NULL, &rq_show_ctrl, NULL },
	{ MTAB_XTD | MTAB_VUN | MTAB_NMO, 0, "UNITQ", NULL,
		NULL, &rq_show_unitq, NULL },
	{ MTAB_XTD | MTAB_VUN, 0, "WRITE", NULL,
		NULL, &rq_show_wlk, NULL },
 	{ UNIT_DTYPE, (RX50_DTYPE << UNIT_V_DTYPE), "RX50", "RX50", &rq_set_size },
	{ UNIT_DTYPE, (RX33_DTYPE << UNIT_V_DTYPE), "RX33", "RX33", &rq_set_size }, 
 	{ UNIT_DTYPE, (RD31_DTYPE << UNIT_V_DTYPE), "RD31", "RD31", &rq_set_size },
 	{ UNIT_DTYPE, (RD51_DTYPE << UNIT_V_DTYPE), "RD51", "RD51", &rq_set_size },
 	{ UNIT_DTYPE, (RD52_DTYPE << UNIT_V_DTYPE), "RD52", "RD52", &rq_set_size },
 	{ UNIT_DTYPE, (RD53_DTYPE << UNIT_V_DTYPE), "RD53", "RD53", &rq_set_size },
 	{ UNIT_DTYPE, (RD54_DTYPE << UNIT_V_DTYPE), "RD54", "RD54", &rq_set_size },
	{ UNIT_DTYPE, (RA82_DTYPE << UNIT_V_DTYPE), "RA82", "RA82", &rq_set_size },
	{ UNIT_DTYPE, (RA72_DTYPE << UNIT_V_DTYPE), "RA72", "RA72", &rq_set_size },
	{ UNIT_DTYPE, (RA90_DTYPE << UNIT_V_DTYPE), "RA90", "RA90", &rq_set_size },
	{ UNIT_DTYPE, (RA92_DTYPE << UNIT_V_DTYPE), "RA92", "RA92", &rq_set_size },
	{ UNIT_DTYPE, (RRD40_DTYPE << UNIT_V_DTYPE), "RRD40", "RRD40", &rq_set_size },
	{ UNIT_DTYPE, (RRD40_DTYPE << UNIT_V_DTYPE), NULL, "CDROM", &rq_set_size },
	{ 0 }  };

DEVICE rq_dev = {
	"RQ", rq_unit, rq_reg, rq_mod,
	RQ_NUMDR + 1, RQ_RDX, 31, RQ_AINC, RQ_RDX, RQ_WID,
	NULL, NULL, &rq_reset,
	&rq_boot, &rq_attach, &rq_detach };

/* I/O dispatch routine, I/O addresses 17772150 - 17772152

   17772150	IP	read/write
   17772152	SA	read/write
*/

t_stat rq_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {				/* decode PA<1> */
case 0:							/* IP */
	*data = 0;					/* reads zero */
	if (rq_csta == CST_S3_PPB) rq_step4 ();		/* waiting for poll? */
	else if (rq_csta == CST_UP) {			/* if up */
		rq_pip = 1;				/* poll host */
		sim_activate (&rq_unit[RQ_QUEUE], rq_qtime);  }
	break;
case 1:							/* SA */
	*data = rq_sa;
	break;  }
return SCPE_OK;
}

t_stat rq_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {				/* decode PA<1> */
case 0:							/* IP */
	rq_reset (&rq_dev);				/* init device */
	break;
case 1:							/* SA */
	switch (rq_csta) {				/* controller state? */
	case CST_S1:					/* need S1 reply */
		if (data & SA_S1H_VL) {			/* valid? */
			if (data & SA_S1H_WR) {		/* wrap? */
				rq_sa = data;		/* echo data */
				rq_csta = CST_S1_WR;  }	/* endless loop */
			else {	rq_s1dat = data;	/* save data */
				rq_sa = SA_S2 | SA_S2C_PT | SA_S2C_EC (rq_s1dat);
				rq_csta = CST_S2;	/* now in step 2 */
				rq_init_int ();  }	/* intr if req */
			}
			break;
	case CST_S1_WR:					/* wrap mode */
		rq_sa = data;				/* echo data */
		break;
	case CST_S2:					/* need S2 reply */
		rq_comm = data & SA_S2H_CLO;		/* get low addr */
		rq_prgi = data & SA_S2H_PI;		/* get purge int */
		rq_sa = SA_S3 | SA_S3C_EC (rq_s1dat);
		rq_csta = CST_S3;			/* now in step 3 */
		rq_init_int ();				/* intr if req */
		break;
	case CST_S3:					/* need S3 reply */
		rq_comm = ((data & SA_S3H_CHI) << 16) | rq_comm;
		if (data & SA_S3H_PP) {			/* purge/poll test? */
			rq_sa = 0;			/* put 0 */
			rq_csta = CST_S3_PPA;  }	/* wait for 0 write */
		else rq_step4 ();			/* send step 4 */
		break;
	case CST_S3_PPA:				/* need purge test */
		if (data) rq_fatal (PE_PPF);		/* data not zero? */
		else rq_csta = CST_S3_PPB;		/* wait for poll */
		break;
	case CST_S4:					/* need S4 reply */
		if (data & SA_S4H_GO) {			/* go set? */
			rq_csta = CST_UP;		/* we're up */
			rq_sa = 0;			/* clear SA */
			sim_activate (&rq_unit[RQ_TIMER], tmr_poll * clk_tps);
			if ((data & SA_S4H_LF) && rq_perr) rq_plf (rq_perr);
			rq_perr = 0;  }
		break;  }				
	break;  }
return SCPE_OK;
}

/* Transition to step 4 - init communications region */

t_bool rq_step4 (void)
{
int32 i, lnt;
t_addr base;
uint16 zero[SA_COMM_MAX >> 1];

rq_rq.ba = rq_comm;					/* set rsp q base */
rq_rq.lnt = SA_S1H_RQ (rq_s1dat) << 2;			/* get resp q len */
rq_cq.ba = rq_comm + rq_rq.lnt;				/* set cmd q base */
rq_cq.lnt = SA_S1H_CQ (rq_s1dat) << 2;			/* get cmd q len */
rq_cq.idx = rq_rq.idx = 0;				/* clear q idx's */
if (rq_prgi) base = rq_comm + SA_COMM_QQ;
else base = rq_comm + SA_COMM_CI;
lnt = rq_comm + rq_cq.lnt + rq_rq.lnt - base;		/* comm lnt */
if (lnt > SA_COMM_MAX) lnt = SA_COMM_MAX;		/* paranoia */
for (i = 0; i < (lnt >> 1); i++) zero[i] = 0;		/* clr buffer */
if (Map_WriteW (base, lnt, zero, QB))			/* zero comm area */
	return rq_fatal (PE_QWE);			/* error? */
rq_sa = SA_S4 | (RQ_MODEL << SA_S4C_V_MOD) |		/* send step 4 */
	(RQ_SVER << SA_S4C_V_VER);
rq_csta = CST_S4;					/* set step 4 */
rq_init_int ();						/* poke host */
return OK;
}

/* Queue service - invoked when any of the queues (host queue, unit
   queues, response queue) require servicing

   Process at most one item off the host queue
   If the host queue is empty, process at most one item off
	each unit queue
   Process at most one item off the response queue

   If all queues are idle, terminate thread
*/


t_stat rq_quesvc (UNIT *uptr)
{
int32 i, cnid;
int32 pkt = 0;

if (rq_pip) {						/* polling? */
    if (!rq_getpkt (&pkt)) return SCPE_OK;		/* get host pkt */
    if (pkt) {						/* got one? */
	if (GETP (pkt, UQ_HCTC, TYP) != UQ_TYP_SEQ)	/* seq packet? */
	    return rq_fatal (PE_PIE);			/* no, term thread */
	cnid = GETP (pkt, UQ_HCTC, CID);		/* get conn ID */
	if (cnid == UQ_CID_MSCP) {			/* MSCP packet? */
	    if (!rq_mscp (pkt, TRUE)) return SCPE_OK;  } /* proc, q non-seq */
	else if (cnid == UQ_CID_DUP) {			/* DUP packet? */
	    rq_putr (pkt, OP_END, 0, ST_CMD | I_OPCD, RSP_LNT, UQ_TYP_SEQ);
	    if (!rq_putpkt (pkt, TRUE)) return SCPE_OK;  } /* ill cmd */
 	else return rq_fatal (PE_ICI);			/* no, term thread */
	}						/* end if pkt */
    else rq_pip = 0;					/* discontinue poll */
    }							/* end if pip */
if (!rq_pip) {						/* not polling? */
    for (i = 0; i < RQ_NUMDR; i++) {			/* chk unit q's */
	if (uptr -> cpkt || (uptr -> pktq == 0)) continue;
	pkt = rq_deqh (&uptr -> pktq);			/* get top of q */
	if (!rq_mscp (pkt, FALSE)) return SCPE_OK;  }	/* process */
    }							/* end if !pip */
if (rq_rspq) {						/* resp q? */
    pkt = rq_deqh (&rq_rspq);				/* get top of q */
    if (!rq_putpkt (pkt, FALSE)) return SCPE_OK;	/* send to hst */
    }							/* end if resp q */
if (pkt) sim_activate (&rq_unit[RQ_QUEUE], rq_qtime);	/* more to do? */ 
return SCPE_OK;						/* done */
}

/* Clock service (roughly once per second) */

t_stat rq_tmrsvc (UNIT *uptr)
{
int32 i;
UNIT *nuptr;

sim_activate (uptr, tmr_poll * clk_tps);		/* reactivate */
for (i = 0; i < RQ_NUMDR; i++) {			/* poll */
	nuptr = rq_dev.units + i;
	if ((nuptr -> flags & UNIT_ATP) &&		/* ATN pending? */
	    (nuptr -> flags & UNIT_ATT) &&		/* still online? */
	    (rq_cflgs & CF_ATN)) {			/* wanted? */
		if (!rq_una (nuptr)) return SCPE_OK;  }
	nuptr -> flags = nuptr -> flags & ~UNIT_ATP;  }
if ((rq_hat > 0) && (--rq_hat == 0))			/* host timeout? */
	rq_fatal (PE_HAT);				/* fatal err */	
return SCPE_OK;
}

/* MSCP packet handling */

t_bool rq_mscp (int32 pkt, t_bool q)
{
uint32 sts, cmd = GETP (pkt, CMD_OPC, OPC);

switch (cmd) {
case OP_ABO:						/* abort */
	return rq_abo (pkt, q);
case OP_AVL:						/* avail */
	return rq_avl (pkt, q);
case OP_FMT:						/* format */
	return rq_fmt (pkt, q);
case OP_GCS:						/* get cmd status */
	return rq_gcs (pkt, q);
case OP_GUS:						/* get unit status */
	return rq_gus (pkt, q);
case OP_ONL:						/* online */
	return rq_onl (pkt, q);
case OP_SCC:						/* set ctrl char */
	return rq_scc (pkt, q);
case OP_SUC:						/* set unit char */
	return rq_suc (pkt, q);
case OP_ACC:						/* access */
case OP_CMP:						/* compare */
case OP_ERS:						/* erase */
case OP_RD:						/* read */
case OP_WR:						/* write */
	return rq_rw (pkt, q);
case OP_CCD:						/* nops */
case OP_DAP:
case OP_FLU:
	cmd = cmd | OP_END;				/* set end flag */
	sts = ST_SUC;					/* success */
	break;
default:
	cmd = OP_END;					/* set end op */
	sts = ST_CMD | I_OPCD;				/* ill op */
	break;  }
rq_putr (pkt, cmd, 0, sts, RSP_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Abort a command - 1st parameter is ref # of cmd to abort */

t_bool rq_abo (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 ref = GETP32 (pkt, ABO_REFL);			/* cmd ref # */
int32 tpkt, prv;
UNIT *uptr;

tpkt = 0;						/* set no mtch */
if (uptr = rq_getucb (lu)) {				/* get unit */
	if (uptr -> cpkt &&				/* curr pkt? */
	    (GETP32 (uptr -> cpkt, CMD_REFL) == ref)) {	/* match ref? */
		tpkt = uptr -> cpkt;			/* save match */
		uptr -> cpkt = 0;			/* gonzo */
		sim_cancel (uptr);  }			/* cancel unit */
	else if (uptr -> pktq &&			/* head of q? */
	    (GETP32 (uptr -> pktq, CMD_REFL) == ref)) {	/* match ref? */
		tpkt = uptr -> pktq;			/* save match */
		uptr -> pktq = rq_pkt[tpkt].link;  }	/* unlink */
	else if (prv = uptr -> pktq) {			/* srch pkt q */
		while (tpkt = rq_pkt[prv].link) {	/* walk list */
		    if (GETP32 (tpkt, RSP_REFL) == ref) {
			rq_pkt[prv].link = rq_pkt[tpkt].link;	/* unlink */
			break;  }  }  }
	if (tpkt) {					/* found target? */
		uint32 tcmd = GETP (tpkt, CMD_OPC, OPC); /* get opcode */
		rq_putr (tpkt, tcmd | OP_END, 0, ST_ABO, RSP_LNT, UQ_TYP_SEQ);
		if (!rq_putpkt (tpkt, TRUE)) return ERR;  }
	}						/* end if unit */
rq_putr (pkt, cmd | OP_END, 0, ST_SUC, ABO_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Unit available - set unit status to available - defer if q'd cmds */

t_bool rq_avl (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 sts;
UNIT *uptr;

if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if (q && uptr -> cpkt) {			/* need to queue? */
		rq_enqt (&uptr -> pktq, pkt);		/* do later */
		return OK;  }
	uptr -> flags = uptr -> flags & ~UNIT_ONL;	/* not online */
	uptr -> uf = uptr -> uf & (UF_WPH | UF_RPL | UF_RMV);
	sts = ST_SUC;  }				/* success */
else sts = ST_OFL;					/* offline */
rq_putr (pkt, cmd | OP_END, 0, sts, AVL_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Get command status - only interested in active xfr cmd */

t_bool rq_gcs (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 ref = GETP32 (pkt, GCS_REFL);			/* ref # */
int32 tpkt;
UNIT *uptr;

if ((uptr = rq_getucb (lu)) && 				/* valid lu? */
    (tpkt = uptr -> cpkt) &&				/* queued pkt? */
    (GETP32 (tpkt, CMD_REFL) == ref) &&			/* match ref? */
    (GETP (tpkt, CMD_OPC, OPC) >= OP_ACC)) {		/* rd/wr cmd? */
	rq_pkt[pkt].d[GCS_STSL] = rq_pkt[tpkt].d[RW_WBCL];
	rq_pkt[pkt].d[GCS_STSH] = rq_pkt[tpkt].d[RW_WBCH];  }
else rq_pkt[pkt].d[GCS_STSL] = rq_pkt[pkt].d[GCS_STSH] = 0;
rq_putr (pkt, cmd | OP_END, 0, ST_SUC, GCS_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Get unit status */

t_bool rq_gus (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 dtyp, sts, rbpar;
UNIT *uptr;

if (rq_pkt[pkt].d[CMD_MOD] & MD_NXU) {			/* next unit? */
	if (lu >= RQ_NUMDR) {				/* end of range? */
		lu = 0;					/* reset to 0 */
		rq_pkt[pkt].d[RSP_UN] = lu;  }  }
if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if ((uptr -> flags & UNIT_ATT) == 0)		/* not attached? */
		sts = ST_OFL | SB_OFL_NV;		/* offl no vol */
	else if (uptr -> flags & UNIT_ONL) sts = ST_SUC; /* online */
	else sts = ST_AVL;				/* avail */
	rq_putr_unit (pkt, uptr, lu, FALSE);		/* fill unit fields */
	dtyp = GET_DTYPE (uptr -> flags);		/* get drive type */
	if (drv_tab[dtyp].rcts) rbpar = 1;		/* ctrl bad blk? */
	else rbpar = 0;					/* fill geom, bblk */
	rq_pkt[pkt].d[GUS_TRK] = drv_tab[dtyp].sect;
	rq_pkt[pkt].d[GUS_GRP] = drv_tab[dtyp].tpg;
	rq_pkt[pkt].d[GUS_CYL] = drv_tab[dtyp].gpc;
	rq_pkt[pkt].d[GUS_UVER] = 0;
	rq_pkt[pkt].d[GUS_RCTS] = drv_tab[dtyp].rcts;
	rq_pkt[pkt].d[GUS_RBSC] =
		(rbpar << GUS_RB_V_RBNS) | (rbpar << GUS_RB_V_RCTC);  }
else sts = ST_OFL;					/* offline */
rq_pkt[pkt].d[GUS_SHUN] = lu;				/* shadowing */
rq_pkt[pkt].d[GUS_SHST] = 0;
rq_putr (pkt, cmd | OP_END, 0, sts, GUS_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Unit online - defer if q'd commands */

t_bool rq_onl (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 sts;
UNIT *uptr;

if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if (q && uptr -> cpkt) {			/* need to queue? */
		rq_enqt (&uptr -> pktq, pkt);		/* do later */
		return OK;  }
	if ((uptr -> flags & UNIT_ATT) == 0)		/* not attached? */
		sts = ST_OFL | SB_OFL_NV;		/* offl no vol */
	else if (uptr -> flags & UNIT_ONL)		/* already online? */
		sts = ST_SUC | SB_SUC_ON;
	else {	sts = ST_SUC;				/* mark online */
		uptr -> flags = uptr -> flags | UNIT_ONL;
		rq_setf_unit (pkt, uptr);  }		/* hack flags */
	rq_putr_unit (pkt, uptr, lu, TRUE);  }		/* set fields */
else sts = ST_OFL;					/* offline */
rq_pkt[pkt].d[ONL_SHUN] = lu;				/* shadowing */
rq_pkt[pkt].d[ONL_SHST] = 0;
rq_putr (pkt, cmd | OP_END, 0, sts, ONL_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Set controller characteristics */

t_bool rq_scc (int32 pkt, t_bool q)
{
int32 sts, cmd, tmo;

if (rq_pkt[pkt].d[SCC_MSV]) {				/* MSCP ver = 0? */
	sts = ST_CMD | I_VRSN;				/* no, lose */
	cmd = 0;  }
else {	sts = ST_SUC;					/* success */
	cmd = GETP (pkt, CMD_OPC, OPC);			/* get opcode */
	rq_cflgs = (rq_cflgs & CF_RPL) |		/* hack ctrl flgs */
		rq_pkt[pkt].d[SCC_CFL];
	if (tmo = rq_pkt[pkt].d[SCC_TMO])		/* valid timeout? */
		rq_htmo = tmo + 2;			/* set new val */
	rq_pkt[pkt].d[SCC_CFL] = rq_cflgs;		/* return flags */
	rq_pkt[pkt].d[SCC_TMO] = RQ_DCTMO;		/* ctrl timeout */
	rq_pkt[pkt].d[SCC_VER] = (RQ_HVER << SCC_VER_V_HVER) |
		(RQ_SVER << SCC_VER_V_SVER);
	rq_pkt[pkt].d[SCC_CIDA] = 0;			/* ctrl ID */
	rq_pkt[pkt].d[SCC_CIDB] = 0;
	rq_pkt[pkt].d[SCC_CIDC] = 0;
	rq_pkt[pkt].d[SCC_CIDD] = (RQ_CLASS << SCC_CIDD_V_CLS) |
		(RQ_MODEL << SCC_CIDD_V_MOD);
	rq_pkt[pkt].d[SCC_MBCL] = 0;			/* max bc */
	rq_pkt[pkt].d[SCC_MBCH] = 0;  }
rq_putr (pkt, cmd | OP_END, 0, sts, SCC_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}
	
/* Set unit characteristics - defer if q'd commands */

t_bool rq_suc (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 sts;
UNIT *uptr;

if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if (q && uptr -> cpkt) {			/* need to queue? */
		rq_enqt (&uptr -> pktq, pkt);		/* do later */
		return OK;  }
	if ((uptr -> flags & UNIT_ATT) == 0)		/* not attached? */
		sts = ST_OFL | SB_OFL_NV;		/* offl no vol */
	else {	sts = ST_SUC;				/* avail or onl */
		rq_setf_unit (pkt, uptr);  }		/* hack flags */
	rq_putr_unit (pkt, uptr, lu, TRUE);  }		/* set fields */
else sts = ST_OFL;					/* offline */
rq_pkt[pkt].d[ONL_SHUN] = lu;				/* shadowing */
rq_pkt[pkt].d[ONL_SHST] = 0;
rq_putr (pkt, cmd | OP_END, 0, sts, SUC_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Format command - floppies only */

t_bool rq_fmt (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 sts;
UNIT *uptr;

if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if (q && uptr -> cpkt) {			/* need to queue? */
		rq_enqt (&uptr -> pktq, pkt);		/* do later */
		return OK;  }
	if (GET_DTYPE (uptr -> flags) != RX33_DTYPE)	/* RX33? */
		sts = ST_CMD | I_OPCD;			/* no, err */
	else if ((rq_pkt[pkt].d[FMT_IH] & 0100000) == 0) /* magic bit set? */
		sts = ST_CMD | I_FMTI;			/* no, err */
	else if ((uptr -> flags & UNIT_ATT) == 0)	/* offline? */
		sts = ST_OFL | SB_OFL_NV;		/* no vol */
	else if (uptr -> flags & UNIT_ONL) {		/* online? */
		uptr -> flags = uptr -> flags & ~UNIT_ONL;
		uptr -> uf = uptr -> uf & (UF_WPH | UF_RPL | UF_RMV);
		sts = ST_AVL | SB_AVL_INU;  }		/* avail, in use */
	else if (uptr -> uf & UF_WPH)			/* write prot? */
		sts = ST_WPR | SB_WPR_HW;		/* can't fmt */
	else sts = ST_SUC;				/*** for now ***/
	}
else sts = ST_OFL;					/* offline */
rq_putr (pkt, cmd | OP_END, 0, sts, FMT_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Data transfer commands */

t_bool rq_rw (int32 pkt, t_bool q)
{
uint32 lu = rq_pkt[pkt].d[CMD_UN];			/* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* opcode */
uint32 sts;
UNIT *uptr;

if (uptr = rq_getucb (lu)) {				/* unit exist? */
	if (q && uptr -> cpkt) {			/* need to queue? */
		rq_enqt (&uptr -> pktq, pkt);		/* do later */
		return OK;  }
	sts = rq_rw_valid (pkt, uptr, cmd);		/* validity checks */
	if (sts == 0) {					/* ok? */
		uptr -> cpkt = pkt;			/* op in progress */
		rq_pkt[pkt].d[RW_WBAL] = rq_pkt[pkt].d[RW_BAL];
		rq_pkt[pkt].d[RW_WBAH] = rq_pkt[pkt].d[RW_BAH];
		rq_pkt[pkt].d[RW_WBCL] = rq_pkt[pkt].d[RW_BCL];
		rq_pkt[pkt].d[RW_WBCH] = rq_pkt[pkt].d[RW_BCH];
		rq_pkt[pkt].d[RW_WBLL] = rq_pkt[pkt].d[RW_LBNL];
		rq_pkt[pkt].d[RW_WBLH] = rq_pkt[pkt].d[RW_LBNH];
		sim_activate (uptr, rq_xtime);		/* activate */
		return OK;  }  }			/* done */
else sts = ST_OFL;					/* offline */
rq_pkt[pkt].d[RW_BCL] = rq_pkt[pkt].d[RW_BCH] = 0;	/* bad packet */
rq_putr (pkt, cmd | OP_END, 0, sts, RW_LNT, UQ_TYP_SEQ);
return rq_putpkt (pkt, TRUE);
}

/* Validity checks */

int32 rq_rw_valid (int32 pkt, UNIT *uptr, uint32 cmd)
{
uint32 dtyp = GET_DTYPE (uptr -> flags);		/* get drive type */
uint32 lbn = GETP32 (pkt, RW_LBNL);			/* get lbn */
uint32 bc = GETP32 (pkt, RW_BCL);			/* get byte cnt */
uint32 maxlbn = drv_tab[dtyp].lbn;			/* get max lbn */

if ((uptr -> flags & UNIT_ATT) == 0)			/* not attached? */
	return (ST_OFL | SB_OFL_NV);			/* offl no vol */
if ((uptr -> flags & UNIT_ONL) == 0)			/* not online? */
	return ST_AVL;					/* only avail */
if ((cmd != OP_ACC) && (cmd != OP_ERS) &&		/* 'real' xfer */
    (rq_pkt[pkt].d[RW_BAL] & 1))			/* odd address? */
	return (ST_HST | SB_HST_OA);			/* host buf odd */
if (bc & 1) return (ST_HST | SB_HST_OC);		/* odd byte cnt? */
if (bc & 0xF0000000) return (ST_CMD | I_BCNT);		/* 'reasonable' bc? */
if (lbn & 0xF0000000) return (ST_CMD | I_LBN);		/* 'reasonable' lbn? */
if (lbn >= maxlbn) {					/* accessing RCT? */
	if (lbn >= (maxlbn + drv_tab[dtyp].rcts))	/* beyond copy 1? */
		return (ST_CMD | I_LBN);		/* lbn err */
	if (bc != RQ_NUMBY) return (ST_CMD | I_BCNT);  }/* bc must be 512 */
else if ((lbn + ((bc + (RQ_NUMBY - 1)) / RQ_NUMBY)) > maxlbn)
	return (ST_CMD | I_BCNT);			/* spiral to RCT */
if ((cmd == OP_WR) || (cmd == OP_ERS)) {		/* write op? */
	if (lbn >= maxlbn)				/* accessing RCT? */
		return (ST_CMD | I_LBN);		/* lbn err */
	if (uptr -> uf & UF_WPS)			/* swre wlk? */
		return (ST_WPR | SB_WPR_SW);
	if (uptr -> uf & UF_WPH)			/* hwre wlk? */
		return (ST_WPR | SB_WPR_HW);  }
return 0;						/* success! */
}

/* Unit service for data transfer commands */

t_stat rq_svc (UNIT *uptr)
{
uint32 i, t, err, tbc, abc, wwc;
int32 pkt = uptr -> cpkt;				/* get packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* get cmd */
uint32 ba = GETP32 (pkt, RW_WBAL);			/* buf addr */
uint32 bc = GETP32 (pkt, RW_WBCL);			/* byte count */
uint32 bl = GETP32 (pkt, RW_WBLL);			/* block addr */
uint32 da = bl * RQ_NUMBY;				/* disk addr */

if (pkt == 0) return STOP_RQ;				/* what??? */
tbc = (bc > RQ_MAXFR)? RQ_MAXFR: bc;			/* trim cnt to max */

if ((uptr -> flags & UNIT_ATT) == 0) {			/* not attached? */
	rq_rw_end (uptr, 0, ST_OFL | SB_OFL_NV);	/* offl no vol */
	return SCPE_OK;  }
if (bc == 0) {						/* no xfer? */
	rq_rw_end (uptr, 0, ST_SUC);			/* ok by me... */
	return SCPE_OK;  }

if (cmd == OP_ERS) {					/* erase? */
	if (uptr -> uf & (UF_WPH | UF_WPS)) rq_rw_end (uptr, 0,
	    ST_WPR | ((uptr -> uf & UF_WPH)? SB_WPR_HW: SB_WPR_SW));
	wwc = ((tbc + (RQ_NUMBY - 1)) & ~(RQ_NUMBY - 1)) >> 1;
	for (i = 0; i < wwc; i++) rqxb[i] = 0;		/* clr buf */
	err = fseek (uptr -> fileref, da, SEEK_SET);	/* set pos */
	if (!err) fxwrite (rqxb, sizeof (int16), wwc, uptr -> fileref);
	err = ferror (uptr -> fileref);  }		/* end if erase */
else if (cmd == OP_WR) {				/* write? */
	if (uptr -> uf & (UF_WPH | UF_WPS)) rq_rw_end (uptr, 0,
	    ST_WPR | ((uptr -> uf & UF_WPH)? SB_WPR_HW: SB_WPR_SW));
	t = Map_ReadW (ba, tbc, rqxb, QB);		/* fetch buffer */
	if (abc = tbc - t) {				/* any xfer? */
		wwc = ((abc + (RQ_NUMBY - 1)) & ~(RQ_NUMBY - 1)) >> 1;
		for (i = (abc >> 1); i < wwc; i++) rqxb[i] = 0;
		err = fseek (uptr -> fileref, da, SEEK_SET);
		if (!err) fxwrite (rqxb, sizeof (int16), wwc, uptr -> fileref);
		err = ferror (uptr -> fileref);  }
	if (t) {					/* nxm? */
		PUTP32 (pkt, RW_WBCL, bc - abc);	/* adj bc */
		PUTP32 (pkt, RW_WBAL, ba + abc);	/* adj ba */
		if (rq_hbe (uptr, ER_NXM))		/* post err log */
		    rq_rw_end (uptr, EF_LOG, ST_HST | SB_HST_NXM);	
		return SCPE_OK;  }  }			/* end else wr */
else {	err = fseek (uptr -> fileref, da, SEEK_SET);	/* set pos */
	if (!err) {
		i = fxread (rqxb, sizeof (int16), tbc >> 1, uptr -> fileref);
		for ( ; i < (tbc >> 1); i++) rqxb[i] = 0; /* fill */
		err = ferror (uptr -> fileref);  }
	if ((cmd == OP_RD) && !err) {			/* read? */
		if (t = Map_WriteW (ba, tbc, rqxb, QB)) { /* store, nxm? */
		    PUTP32 (pkt, RW_WBCL, bc - (tbc - t)); /* adj bc */
		    PUTP32 (pkt, RW_WBAL, ba + (tbc - t)); /* adj ba */
		    if (rq_hbe (uptr, ER_NXM))		/* post err log */
			rq_rw_end (uptr, EF_LOG, ST_HST | SB_HST_NXM);	
		    return SCPE_OK;  }
		}
	else if ((cmd == OP_CMP) && !err) {		/* compare? */
		uint8 dby, mby;
		for (i = 0; i < tbc; i++) {		/* loop */
		    if (Map_ReadB (ba + i, 1, &mby, QB)) {	/* fetch, nxm? */
			PUTP32 (pkt, RW_WBCL, bc - i);	/* adj bc */
			PUTP32 (pkt, RW_WBAL, bc - i);	/* adj ba */
			if (rq_hbe (uptr, ER_NXM))	/* post err log */
			    rq_rw_end (uptr, EF_LOG, ST_HST | SB_HST_NXM);
			return SCPE_OK;  }
		    dby = (rqxb[i >> 1] >> ((i & 1)? 8: 0)) & 0xFF;
		    if (mby != dby) {			/* cmp err? */
			PUTP32 (pkt, RW_WBCL, bc - i);	/* adj bc */
			rq_rw_end (uptr, 0, ST_CMP);	/* done */
			return SCPE_OK;  }		/* exit */
		    }					/* end for */
		}					/* end else if */
	}						/* end else read */
if (err != 0) {						/* error? */
	if (rq_dte (uptr, ST_DRV))			/* post err log */
		rq_rw_end (uptr, EF_LOG, ST_DRV);	/* if ok, report err */
	perror ("RQ I/O error");
	clearerr (uptr -> fileref);
	return SCPE_IOERR;  }
ba = ba + tbc;						/* incr bus addr */
bc = bc - tbc;						/* decr byte cnt */
bl = bl + ((tbc + (RQ_NUMBY - 1)) / RQ_NUMBY);		/* incr blk # */
PUTP32 (pkt, RW_WBAL, ba);				/* update pkt */
PUTP32 (pkt, RW_WBCL, bc);
PUTP32 (pkt, RW_WBLL, bl);
if (bc) sim_activate (uptr, rq_xtime);			/* more? resched */
else rq_rw_end (uptr, 0, ST_SUC);			/* done! */
return SCPE_OK;
}

/* Transfer command complete */

t_bool rq_rw_end (UNIT *uptr, uint32 flg, uint32 sts)
{
int32 pkt = uptr -> cpkt;				/* packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);			/* get cmd */
uint32 bc = GETP32 (pkt, RW_BCL);			/* init bc */
uint32 wbc = GETP32 (pkt, RW_WBCL);			/* work bc */

uptr -> cpkt = 0;					/* done */
PUTP32 (pkt, RW_BCL, bc - wbc);				/* bytes processed */
rq_pkt[pkt].d[RW_WBAL] = rq_pkt[pkt].d[RW_WBAH] = 0;	/* clear temps */
rq_pkt[pkt].d[RW_WBCL] = rq_pkt[pkt].d[RW_WBCH] = 0;
rq_pkt[pkt].d[RW_WBLL] = rq_pkt[pkt].d[RW_WBLH] = 0;
rq_putr (pkt, cmd | OP_END, flg, sts, RW_LNT, UQ_TYP_SEQ); /* fill pkt */
if (!rq_putpkt (pkt, TRUE)) return ERR;			/* send pkt */
if (uptr -> pktq)					/* more to do? */
	sim_activate (&rq_unit[RQ_QUEUE], rq_qtime);	/* activate thread */
return OK;
}

/* Data transfer error log packet */

t_bool rq_dte (UNIT *uptr, uint32 err)
{
int32 pkt, tpkt;
uint32 lu, dtyp, lbn, ccyl, csurf, csect, t;

if ((rq_cflgs & CF_THS) == 0) return OK;		/* logging? */
if (!rq_deqf (&pkt)) return ERR;			/* get log pkt */
tpkt = uptr -> cpkt;					/* rw pkt */
lu = rq_pkt[tpkt].d[CMD_UN];				/* unit # */
lbn = GETP32 (tpkt, RW_WBLL);				/* recent LBN */
dtyp = GET_DTYPE (uptr -> flags);			/* drv type */
if (drv_tab[dtyp].flgs & RQDF_SDI) t = 0;		/* SDI? ovhd @ end */
else t = (drv_tab[dtyp].xbn + drv_tab[dtyp].dbn) /	/* ovhd cylinders */
	(drv_tab[dtyp].sect * drv_tab[dtyp].surf);
ccyl = t + (lbn / drv_tab[dtyp].cyl);			/* curr real cyl */
t = lbn % drv_tab[dtyp].cyl;				/* trk relative blk */
csurf = t / drv_tab[dtyp].surf;				/* curr surf */
csect = t % drv_tab[dtyp].surf;				/* curr sect */

rq_pkt[pkt].d[ELP_REFL] = rq_pkt[tpkt].d[CMD_REFL];	/* copy cmd ref */
rq_pkt[pkt].d[ELP_REFH] = rq_pkt[tpkt].d[CMD_REFH];	/* copy cmd ref */
rq_pkt[pkt].d[ELP_UN] = lu;				/* copy unit */
rq_pkt[pkt].d[ELP_SEQ] = 0;				/* clr seq # */
rq_pkt[pkt].d[DTE_CIDA] = 0;				/* ctrl ID */
rq_pkt[pkt].d[DTE_CIDB] = 0;
rq_pkt[pkt].d[DTE_CIDC] = 0;
rq_pkt[pkt].d[DTE_CIDD] = (RQ_CLASS << DTE_CIDD_V_CLS) |
	(RQ_MODEL << DTE_CIDD_V_MOD);
rq_pkt[pkt].d[DTE_VER] = (RQ_HVER << DTE_VER_V_HVER) |
	(RQ_SVER << DTE_VER_V_SVER);
rq_pkt[pkt].d[DTE_MLUN] = lu;				/* MLUN */
rq_pkt[pkt].d[DTE_UIDA] = lu;				/* unit ID */
rq_pkt[pkt].d[DTE_UIDB] = 0;
rq_pkt[pkt].d[DTE_UIDC] = 0;
rq_pkt[pkt].d[DTE_UIDD] = (UID_DISK << DTE_UIDD_V_CLS) |
	(drv_tab[dtyp].mod << DTE_UIDD_V_MOD);
rq_pkt[pkt].d[DTE_UVER] = 0;				/* unit versn */
rq_pkt[pkt].d[DTE_SCYL] = ccyl;				/* cylinder */
rq_pkt[pkt].d[DTE_VSNL] = 01234 + lu;			/* vol ser # */
rq_pkt[pkt].d[DTE_VSNH] = 0;
rq_pkt[pkt].d[DTE_D1] = 0;
rq_pkt[pkt].d[DTE_D2] = csect << DTE_D2_V_SECT;		/* geometry */
rq_pkt[pkt].d[DTE_D3] = (ccyl << DTE_D3_V_CYL) |
	(csurf << DTE_D3_V_SURF);
rq_putr (pkt, FM_SDE, LF_SNR, err, DTE_LNT, UQ_TYP_DAT);
return rq_putpkt (pkt, TRUE);
}

/* Host bus error log packet */

t_bool rq_hbe (UNIT *uptr, uint32 err)
{
int32 pkt, tpkt;

if ((rq_cflgs & CF_THS) == 0) return OK;		/* logging? */
if (!rq_deqf (&pkt)) return ERR;			/* get log pkt */
tpkt = uptr -> cpkt;					/* rw pkt */
rq_pkt[pkt].d[ELP_REFL] = rq_pkt[tpkt].d[CMD_REFL];	/* copy cmd ref */
rq_pkt[pkt].d[ELP_REFH] = rq_pkt[tpkt].d[CMD_REFH];	/* copy cmd ref */
rq_pkt[pkt].d[ELP_UN] = rq_pkt[tpkt].d[CMD_UN];		/* copy unit */
rq_pkt[pkt].d[ELP_SEQ] = 0;				/* clr seq # */
rq_pkt[pkt].d[HBE_CIDA] = 0;				/* ctrl ID */
rq_pkt[pkt].d[HBE_CIDB] = 0;
rq_pkt[pkt].d[HBE_CIDC] = 0;
rq_pkt[pkt].d[HBE_CIDD] = (RQ_CLASS << DTE_CIDD_V_CLS) |
	(RQ_MODEL << DTE_CIDD_V_MOD);
rq_pkt[pkt].d[HBE_VER] = (RQ_HVER << HBE_VER_V_HVER) |	/* versions */
	(RQ_SVER << HBE_VER_V_SVER);
rq_pkt[pkt].d[HBE_RSV] = 0;
rq_pkt[pkt].d[HBE_BADL] = rq_pkt[tpkt].d[RW_WBAL];	/* bad addr */
rq_pkt[pkt].d[HBE_BADH] = rq_pkt[tpkt].d[RW_WBAH];
rq_putr (pkt, FM_BAD, LF_SNR, err, HBE_LNT, UQ_TYP_DAT);
return rq_putpkt (pkt, TRUE);
}

/* Port last failure error log packet */

t_bool rq_plf (uint32 err)
{
int32 pkt;

if (!rq_deqf (&pkt)) return ERR;			/* get log pkt */
rq_pkt[pkt].d[ELP_REFL] = rq_pkt[pkt].d[ELP_REFH] = 0;	/* ref = 0 */
rq_pkt[pkt].d[ELP_UN] = rq_pkt[pkt].d[ELP_SEQ] = 0;	/* no unit, seq */
rq_pkt[pkt].d[PLF_CIDA] = 0;				/* cntl ID */
rq_pkt[pkt].d[PLF_CIDB] = 0;
rq_pkt[pkt].d[PLF_CIDC] = 0;
rq_pkt[pkt].d[PLF_CIDD] = (RQ_CLASS << PLF_CIDD_V_CLS) |
	(RQ_MODEL << PLF_CIDD_V_MOD);
rq_pkt[pkt].d[PLF_VER] = (RQ_SVER << PLF_VER_V_SVER) |
	(RQ_HVER << PLF_VER_V_HVER);
rq_pkt[pkt].d[PLF_ERR] = err;
rq_putr (pkt, FM_CNT, LF_SNR, ST_CNT, PLF_LNT, UQ_TYP_DAT);
rq_pkt[pkt].d[UQ_HCTC] |= (UQ_CID_DIAG << UQ_HCTC_V_CID);
return rq_putpkt (pkt, TRUE);
}

/* Unit now available attention packet */

int32 rq_una (UNIT *uptr)
{
int32 pkt;
uint32 lu;

if (!rq_deqf (&pkt)) return ERR;			/* get log pkt */
lu = uptr - rq_dev.units;				/* get unit */
rq_pkt[pkt].d[RSP_REFL] = rq_pkt[pkt].d[RSP_REFH] = 0;	/* ref = 0 */
rq_pkt[pkt].d[RSP_UN] = lu;
rq_pkt[pkt].d[RSP_RSV] = 0;
rq_putr_unit (pkt, uptr, lu, FALSE);			/* fill unit fields */
rq_putr (pkt, OP_AVA, 0, 0, UNA_LNT, UQ_TYP_SEQ);	/* fill std fields */
return rq_putpkt (pkt, TRUE);
}

/* List handling

   rq_deqf	-	dequeue head of free list (fatal err if none)
   rq_deqh	-	dequeue head of list
   rq_enqh	-	enqueue at head of list
   rq_enqt	-	enqueue at tail of list
*/

t_bool rq_deqf (int32 *pkt)
{
if (rq_freq == 0) return rq_fatal (PE_NSR);		/* no free pkts?? */
rq_pbsy = rq_pbsy + 1;					/* cnt busy pkts */
*pkt = rq_freq;						/* head of list */
rq_freq = rq_pkt[rq_freq].link;				/* next */
return OK;
}

int32 rq_deqh (int32 *lh)
{
int32 ptr = *lh;					/* head of list */

if (ptr) *lh = rq_pkt[ptr].link;			/* next */
return ptr;
}

void rq_enqh (int32 *lh, int32 pkt)
{
if (pkt == 0) return;					/* any pkt? */
rq_pkt[pkt].link = *lh;					/* link is old lh */
*lh = pkt;						/* pkt is new lh */
return;
}

void rq_enqt (int32 *lh, int32 pkt)
{
if (pkt == 0) return;					/* any pkt? */
rq_pkt[pkt].link = 0;					/* it will be tail */
if (*lh == 0) *lh = pkt;				/* if empty, enqh */
else {	uint32 ptr = *lh;				/* chase to end */
	while (rq_pkt[ptr].link) ptr = rq_pkt[ptr].link;
	rq_pkt[ptr].link = pkt;  }			/* enq at tail */
return;
}

/* Packet and descriptor handling */

/* Get packet from command ring */

t_bool rq_getpkt (int32 *pkt)
{
uint32 desc;
t_addr addr;

if (!rq_getdesc (&rq_cq, &desc)) return ERR;		/* get cmd desc */
if ((desc & UQ_DESC_OWN) == 0) {			/* none */
	*pkt = 0;					/* pkt = 0 */
	return OK;  }					/* no error */
if (!rq_deqf (pkt)) return ERR;				/* get cmd pkt */
rq_hat = 0;						/* dsbl hst timer */
addr = desc & UQ_ADDR;					/* get Q22 addr */
if (Map_ReadW (addr + UQ_HDR_OFF, RQ_PKT_SIZE, rq_pkt[*pkt].d, QB))
	return rq_fatal (PE_PRE);			/* read pkt */
return rq_putdesc (&rq_cq, desc);			/* release desc */
}

/* Put packet to response ring - note the clever hack about credits.
   The controller sends all its credits to the host.  Thereafter, it
   supplies one credit for every response packet sent over.  Simple!
*/

t_bool rq_putpkt (int32 pkt, t_bool qt)
{
uint32 desc, lnt, cr;
t_addr addr;

if (pkt == 0) return OK;				/* any packet? */
if (!rq_getdesc (&rq_rq, &desc)) return ERR;		/* get rsp desc */
if ((desc & UQ_DESC_OWN) == 0) {			/* not valid? */
	if (qt) rq_enqt (&rq_rspq, pkt);		/* normal? q tail */
	else rq_enqh (&rq_rspq, pkt);			/* resp q call */
	sim_activate (&rq_unit[RQ_QUEUE], rq_qtime);	/* activate q thrd */
	return OK;  }
addr = desc & UQ_ADDR;					/* get Q22 addr */
lnt = rq_pkt[pkt].d[UQ_HLNT] - UQ_HDR_OFF;		/* size, with hdr */
if ((GETP (pkt, UQ_HCTC, TYP) == UQ_TYP_SEQ) &&		/* seq packet? */
    (GETP (pkt, CMD_OPC, OPC) & OP_END)) {		/* end packet? */
	cr = (rq_credits >= 14)? 14: rq_credits;	/* max 14 credits */
	rq_credits = rq_credits - cr;			/* decr credits */
	rq_pkt[pkt].d[UQ_HCTC] |= ((cr + 1) << UQ_HCTC_V_CR);  }
if (Map_WriteW (addr + UQ_HDR_OFF, lnt, rq_pkt[pkt].d, QB))
	return rq_fatal (PE_PWE);			/* write pkt */
rq_enqh (&rq_freq, pkt);				/* pkt is free */
rq_pbsy = rq_pbsy - 1;					/* decr busy cnt */
if (rq_pbsy == 0) rq_hat = rq_htmo;			/* idle? strt hst tmr */
return rq_putdesc (&rq_rq, desc);			/* release desc */
}

/* Get a descriptor from the host */

t_bool rq_getdesc (struct uq_ring *ring, uint32 *desc)
{
t_addr addr = ring -> ba + ring -> idx;
uint16 d[2];

if (Map_ReadW (addr, 4, d, QB))				/* fetch desc */
	return rq_fatal (PE_QRE);			/* err? dead */
*desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
return OK;						/* own? ok */
}

/* Return a descriptor to the host, clearing owner bit
   If rings transitions from "empty" to "not empty" or "full" to
   "not full", and interrupt bit was set, interrupt the host.
   Actually, test whether previous ring entry was owned by host.
*/

t_bool rq_putdesc (struct uq_ring *ring, uint32 desc)
{
uint32 prvd, newd = (desc & ~UQ_DESC_OWN) | UQ_DESC_F;
t_addr prva, addr = ring -> ba + ring -> idx;
uint16 d[2];

d[0] = newd & 0xFFFF;					/* 32b to 16b */
d[1] = (newd >> 16) & 0xFFFF;
if (Map_WriteW (addr, 4, d, QB))			/* store desc */
	return rq_fatal (PE_QWE);			/* err? dead */
if (desc & UQ_DESC_F) {					/* was F set? */
	if (ring -> lnt <= 4) rq_ring_int (ring);	/* lnt = 1? intr */
	else {	prva = ring -> ba +			/* prv desc */
			((ring -> idx - 4) & (ring -> lnt - 1));
		if (Map_ReadW (prva, 4, d, QB))		/* read prv */
			return rq_fatal (PE_QRE);
		prvd = ((uint32) d[0]) | (((uint32) d[1]) << 16);
		if (prvd & UQ_DESC_OWN) rq_ring_int (ring);  }  }
ring -> idx = (ring -> idx + 4) & (ring -> lnt - 1);
return OK;
}

/* Get unit descriptor for logical unit - trivial now,
   but eventually, hide multiboard complexities here */

UNIT *rq_getucb (uint32 lu)
{
UNIT *uptr;

if (lu >= RQ_NUMDR) return NULL;
uptr = rq_dev.units + lu;
if (uptr -> flags & UNIT_DIS) return NULL;
return uptr;
}

/* Hack unit flags */

void rq_setf_unit (int32 pkt, UNIT *uptr)
{
uptr -> uf = (uptr -> uf & (UF_WPH | UF_RPL | UF_RMV)) |
	(rq_pkt[pkt].d[ONL_UFL] & UF_MSK);		/* settable flags */
if ((rq_pkt[pkt].d[CMD_MOD] & MD_SWP) &&		/* swre wrp enb? */
	(rq_pkt[pkt].d[ONL_UFL] & UF_WPS))		/* swre wrp on? */
	uptr -> uf = uptr -> uf | UF_WPS;		/* simon says... */
return;
}

/* Unit response fields */

void rq_putr_unit (int32 pkt, UNIT *uptr, uint32 lu, t_bool all)
{
uint32 dtyp = GET_DTYPE (uptr -> flags);		/* get drive type */

rq_pkt[pkt].d[ONL_MLUN] = lu;				/* unit */
rq_pkt[pkt].d[ONL_UFL] = uptr -> uf;			/* flags */
rq_pkt[pkt].d[ONL_RSVL] = rq_pkt[pkt].d[ONL_RSVH] = 0;	/* reserved */
rq_pkt[pkt].d[ONL_UIDA] = lu;				/* UID low */
rq_pkt[pkt].d[ONL_UIDB] = 0;
rq_pkt[pkt].d[ONL_UIDC] = 0;
rq_pkt[pkt].d[ONL_UIDD] = (UID_DISK << ONL_UIDD_V_CLS) |
	(drv_tab[dtyp].mod << ONL_UIDD_V_MOD);		/* UID hi */
PUTP32 (pkt, ONL_MEDL, drv_tab[dtyp].med);		/* media type */
if (all) {						/* if long form */
	PUTP32 (pkt, ONL_SIZL, drv_tab[dtyp].lbn);	/* user LBNs */
	rq_pkt[pkt].d[ONL_VSNL] = 01234 + lu;		/* vol serial # */
	rq_pkt[pkt].d[ONL_VSNH] = 0;  }
return;
}

/* UQ_HDR and RSP_OP fields */

void rq_putr (int32 pkt, uint32 cmd, uint32 flg, uint32 sts, uint32 lnt, uint32 typ)
{
rq_pkt[pkt].d[RSP_OPF] = (cmd << RSP_OPF_V_OPC) |	/* set cmd, flg */
	(flg << RSP_OPF_V_FLG);
rq_pkt[pkt].d[RSP_STS] = sts;
rq_pkt[pkt].d[UQ_HLNT] = lnt;				/* length */
rq_pkt[pkt].d[UQ_HCTC] = typ << UQ_HCTC_V_TYP;		/* type, clr cid, cr */
return;
}

/* Post interrupt during init */

void rq_init_int (void)
{
if ((rq_s1dat & SA_S1H_IE) && (rq_s1dat & SA_S1H_VEC)) {
	SET_INT (RQ);  }
return;
}

/* Post interrupt during putpkt - note that NXMs are ignored! */

void rq_ring_int (struct uq_ring *ring)
{
t_addr iadr = rq_comm + ring -> ioff;			/* addr intr wd */
uint16 flag = 1;

Map_WriteW (iadr, 2, &flag, QB);			/* write flag */
if (rq_s1dat & SA_S1H_VEC) SET_INT (RQ);		/* if enb, intr */
return;
}

/* Return interrupt vector */

int32 rq_inta (void)
{
return (VEC_Q + ((rq_s1dat & SA_S1H_VEC) << 2));	/* prog vector */
}

/* Fatal error */

t_bool rq_fatal (uint32 err)
{
rq_reset (&rq_dev);					/* reset device */
rq_sa = SA_ER | err;					/* SA = dead code */
rq_csta = CST_DEAD;					/* state = dead */
rq_perr = err;						/* save error */
return ERR;
}

/* Set/clear hardware write lock */

t_stat rq_set_wlk (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 dtyp = GET_DTYPE (uptr -> flags);		/* get drive type */

if (drv_tab[dtyp].flgs & RQDF_RO) return SCPE_NOFNC;	/* not on read only */
if (val || (uptr -> flags & UNIT_RO))
	uptr -> uf = uptr -> uf | UF_WPH;		/* copy to uf */
else uptr -> uf = uptr -> uf & ~UF_WPH;
return SCPE_OK;
}

/* Show write lock status */

t_stat rq_show_wlk (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 dtyp = GET_DTYPE (uptr -> flags);		/* get drive type */

if (drv_tab[dtyp].flgs & RQDF_RO) fprintf (st, "read only");
else if (uptr -> uf & UF_WPH) fprintf (st, "write locked");
else fprintf (st, "write enabled");
return SCPE_OK;
}

/* Change unit size */

t_stat rq_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ALATT;
uptr -> capac = drv_tab[GET_DTYPE (val)].lbn * RQ_NUMBY;
if (drv_tab[GET_DTYPE (val)].flgs & RQDF_RMV)
	uptr -> uf = uptr -> uf | UF_RMV;
else uptr -> uf =  uptr -> uf & ~UF_RMV;
if (val == RRD40_DTYPE) uptr -> uf = uptr -> uf | UF_WPH;
return SCPE_OK;
}

/* Device attach */

t_stat rq_attach (UNIT *uptr, char *cptr)
{
int32 dtyp = GET_DTYPE (uptr -> flags);
t_stat r;

uptr -> capac = drv_tab[dtyp].lbn * RQ_NUMBY;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
if (rq_csta == CST_UP) uptr -> flags = uptr -> flags | UNIT_ATP;
if ((drv_tab[dtyp].flgs & RQDF_RO) || (uptr -> flags & UNIT_RO))
    uptr -> uf = uptr -> uf | UF_WPH;
return SCPE_OK;
}

/* Device detach */

t_stat rq_detach (UNIT *uptr)
{
t_stat r;

r = detach_unit (uptr);					/* detach unit */
if (r != SCPE_OK) return r;
uptr -> flags = uptr -> flags & ~(UNIT_ONL | UNIT_ATP);	/* clr onl, atn pend */
uptr -> uf = uptr -> uf & (UF_WPH | UF_RPL | UF_RMV);	/* clr unit flgs */
return SCPE_OK;
} 

/* Device reset */

t_stat rq_reset (DEVICE *dptr)
{
int32 i, j;

rq_csta = CST_S1;					/* init stage 1 */
rq_s1dat = 0;						/* no S1 data */
rq_sa = SA_S1 | SA_S1C_Q22 | SA_S1C_DI | SA_S1C_MP;	/* init SA val */
rq_cflgs = CF_RPL;					/* ctrl flgs off */
rq_htmo = RQ_DHTMO + 1;					/* default timeout */
rq_hat = rq_htmo;					/* default timer */
rq_cq.ba = rq_cq.lnt = rq_cq.idx = 0;			/* clr cmd ring */
rq_rq.ba = rq_rq.lnt = rq_rq.idx = 0;			/* clr rsp ring */
rq_credits = (RQ_NPKTS / 2) - 1;			/* init credits */
rq_freq = 1;						/* init free list */
for (i = 0; i < RQ_NPKTS; i++) {			/* all pkts free */
	if (i) rq_pkt[i].link = (i + 1) & RQ_M_NPKTS;
	else rq_pkt[i].link = 0;
	for (j = 0; j < RQ_PKT_SIZE_W; j++) rq_pkt[i].d[j] = 0;  }
rq_rspq = 0;						/* no q'd rsp pkts */
rq_pbsy = 0;						/* all pkts free */
rq_pip = 0;						/* not polling */
CLR_INT (RQ);						/* clr intr req */
for (i = 0; i < RQ_NUMDR; i++) {			/* init units */
	UNIT *uptr = rq_dev.units + i;
	sim_cancel (uptr);				/* clr activity */
	uptr -> flags = uptr -> flags & ~UNIT_ONL;	/* not online */
	if (drv_tab[GET_DTYPE (uptr -> flags)].flgs & RQDF_RMV)
		uptr -> uf = UF_RPL | UF_RMV;		/* init flags */
	else uptr -> uf = UF_RPL;
	uptr -> cpkt = uptr -> pktq = 0;  }		/* clr pkt q's */
sim_cancel (&rq_unit[RQ_TIMER]);			/* clr timer thrd */
sim_cancel (&rq_unit[RQ_QUEUE]);			/* clr queue thrd */
if (rqxb == NULL) rqxb = calloc (RQ_MAXFR >> 1, sizeof (unsigned int16));
if (rqxb == NULL) return SCPE_MEM;
return SCPE_OK;
}

/* Device bootstrap */

#if defined (VM_PDP11)

#define BOOT_START 016000				/* start */
#define BOOT_UNIT 016006				/* unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {

	/* Four step init process */

	0012706, 0016000,		/* st: mov #st,sp */
	0012700, 0000000,		/*   mov  #unit,r0 */
	0012701, 0172150,		/*   mov  #172150, r1	; ip addr */
	0012704, 0016160,		/*   mov  #it, r4 */
	0012705, 0004000,		/*   mov  #4000,r5	; s1 mask */
	0010102,			/*   mov  r1,r2 */
	0005022,			/*   clr  (r2)+		; init */
	0005712,			/* 10$: tst (r2)	; err? */
	0100001,			/*   bpl  20$ */
	0000000,			/*   halt */
	0030512,			/* 20$: bit r5,(r2)	; step set? */
	0001773,			/*   beq  10$		; wait */
	0012412,			/*   mov  (r4)+,(r2)	; send next */
	0006305,			/*   asl  r5		; next mask */
	0100370,			/*   bpl 10$		; s4 done? */

	/* Send ONL, READ commands */

	0105714,			/* 30$:	tstb	(r4)	; end tbl? */
	0001434,			/*   beq  done		; 0 = yes */
	0012702, 0007000,		/*   mov  #rpkt-4,r2	; clr pkts */
	0005022,			/* 40$: clr (r2)+ */
	0020227, 0007204,		/*   cmp  r2,#comm */
	0103774,			/*   blo  40$ */
	0112437, 0007100,		/*   movb (r4)+,cpkt-4	; set lnt */
	0110037, 0007110,		/*   movb r0,cpkt+4	; set unit */
	0112437, 0007114,		/*   movb (r4)+,cpkt+10	; set op */
	0112437, 0007121,		/*   movb (r4)+,cpkt+15	; set param */
	0012722, 0007004,		/*   mov  #rpkt,(r2)+	; rq desc */
	0010522,			/*   mov  r5,(r2)+	; rq own */
	0012722, 0007104,		/*   mov  #ckpt,(r2)+	; cq desc */
	0010512,			/*   mov  r5,(r2)	; cq own */
	0024242,			/*   cmp  -(r2),-(r2)	; back up */
	0005711,			/*   tst  (r1)		; wake ctrl */
	0005712,			/* 50$: tst (r2)	; rq own clr? */
	0100776,			/*   bmi  50$		; wait */
	0005737, 0007016,		/*   tst  rpkt+12	; stat ok? */
	0001743,			/*   beq  30$		; next cmd */
	0000000,			/*   halt */

	/* Boot block read in, jump to 0 */

	0005002,			/* done: clr r2 */
	0005003,			/*   clr  r3 */
	0012705, 0052504,		/*   mov  #"DU,r5 */
	0010704,			/*   mov  pc,r4 */
	0005007,			/*   clr  pc */

	/* Data */

	0100000,			/* it: no ints, ring sz = 1 */
	0007204,			/*    .word comm */
	0000000,			/*    .word 0 */
	0000001,			/*    .word 1 */
	0004420,			/*   .byte 20,11 */
	0020000,			/*   .byte 0,40 */
	0001041,			/*   .byte 41,2 */
	0000000
};

t_stat rq_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;
extern uint16 *M;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & 3;
saved_PC = BOOT_START;
return SCPE_OK;
}

#else

t_stat rq_boot (int32 unitno)
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
fprintf (st, "ring, base = %o, index = %d, length = %d",
	 rp -> ba, rp -> idx >> 2, rp -> lnt >> 2);
#else
fprintf (st, "ring, base = %x, index = %d, length = %d",
	 rp -> ba, rp -> idx >> 2, rp -> lnt >> 2);
#endif
for (i = 0; i < rp -> lnt >> 2; i = i++) {
	if ((i % RQ_SH_DPL) == 0) fprintf (st, "\n");
	if (Map_ReadW (rp -> ba + (i << 2), 4, d, QB))	{
		fprintf (st, " %3d: non-existent memory", i);
		break;  }
	desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
#if defined (VM_PDP11)
	fprintf (st, " %3d: %011o", i, desc);
#else
	fprintf (st, " %3d: %08x", i, desc);
#endif
	}
return;
}

void rq_show_pkt (FILE *st, int32 pkt)
{
int32 i, j;
uint32 cr = GETP (pkt, UQ_HCTC, CR);
uint32 typ = GETP (pkt, UQ_HCTC, TYP);
uint32 cid = GETP (pkt, UQ_HCTC, CID);

fprintf (st, "packet %d, credits = %d, type = %d, cid = %d",
	pkt, cr, typ, cid);
for (i = 0; i < RQ_SH_MAX; i = i + RQ_SH_PPL) {
	fprintf (st, "\n %2d:", i);
	for (j = i; (j < (i + RQ_SH_PPL)); j++)
#if defined (VM_PDP11)
		fprintf (st, " %06o", rq_pkt[pkt].d[j]);
#else
		fprintf (st, " %04x", rq_pkt[pkt].d[j]);
#endif
	}
return;
}

t_stat rq_show_unitq (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 pkt, u = uptr - rq_dev.units;

if (rq_csta != CST_UP) {
	fprintf (st, "Controller is not initialized");
	return SCPE_OK;  }
if ((uptr -> flags & UNIT_ONL) == 0) {
	if (uptr -> flags & UNIT_ATT)
		fprintf (st, "Unit %d is available", u);
	else fprintf (st, "Unit %d is offline", u);
	return SCPE_OK;  }
if (uptr -> cpkt) {
	fprintf (st, "Unit %d current ", u);
	rq_show_pkt (st, uptr -> cpkt);
	if (pkt = uptr -> pktq) {
		do {	fprintf (st, "\nUnit %d queued ", u);
			rq_show_pkt (st, pkt);  }
		while (pkt = rq_pkt[pkt].link);  }  }
else fprintf (st, "Unit %d queues are empty", u);
return SCPE_OK;
}

t_stat rq_show_ctrl (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, pkt;

if (rq_csta != CST_UP) {
    fprintf (st, "Controller is not initialized");
    return SCPE_OK;  }
if (val & RQ_SH_RI) {
    if (rq_pip) fprintf (st, "Polling in progress\n");
    fprintf (st, "Command ");
    rq_show_ring (st, &rq_cq);
    fprintf (st, "\nResponse ");
    rq_show_ring (st, &rq_rq);
    }
if (val & RQ_SH_FR) {
    if (val & RQ_SH_RI) fprintf (st, "\n");
    if (pkt = rq_freq) {
	for (i = 0; pkt != 0; i++, pkt = rq_pkt[pkt].link) {
	    if (i == 0) fprintf (st, "Free queue = %d", pkt);
	    else if ((i % 16) == 0) fprintf (st, ",\n %d", pkt);
	    else fprintf (st, ", %d", pkt);  }  }
    else fprintf (st, "Free queue is empty");
    }
if (val & RQ_SH_RS) {
    if (val & (RQ_SH_RI | RQ_SH_FR)) fprintf (st, "\n");
    if (pkt = rq_rspq) {
	do {	fprintf (st, "Response ");
		rq_show_pkt (st, pkt);  }
	while (pkt = rq_pkt[pkt].link);  }
    else fprintf (st, "Response queue is empty");
    }
if (val & RQ_SH_UN) {
    for (i = 0; i < RQ_NUMDR; i++) {
        if ((val & (RQ_SH_RI | RQ_SH_FR | RQ_SH_RS)) || i)
	    fprintf (st, "\n");
	rq_show_unitq (st, &rq_unit[i], 0, NULL); }
    }
return SCPE_OK;
}
