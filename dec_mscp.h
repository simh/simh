/* dec_mscp.c: DEC MSCP definitions

   Copyright (c) 2001-2002, Robert M Supnik
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
*/

/* Misc constants */

#define UID_DISK	2				/* disk class */

/* Opcodes */

#define	OP_ABO		1				/* abort */
#define	OP_GCS		2				/* get command status */
#define	OP_GUS		3				/* get unit status */
#define	OP_SCC		4				/* set controller char */
#define	OP_AVL		8				/* available */
#define	OP_ONL		9				/* online */
#define	OP_SUC		10				/* set unit char */
#define	OP_DAP		11				/* det acc paths - nop */
#define	OP_ACC		16				/* access */
#define	OP_CCD		17				/* compare - nop */
#define	OP_ERS		18				/* erase */
#define	OP_FLU		19				/* flush - nop */
#define	OP_CMP		32				/* compare */
#define	OP_RD		33				/* read */
#define	OP_WR		34				/* write */
#define	OP_FMT		47				/* format */
#define OP_AVA		64				/* unit now avail */
#define OP_END		0x80				/* end flag */

/* Modifiers */

#define MD_EXP		0x8000				/* express NI */
#define MD_CMP		0x4000				/* compare NI */
#define MD_CSE		0x2000				/* clr ser err NI */
#define MD_ERR		0x1000				/* force error NI*/
#define MD_SEC		0x0200				/* supr err corr NI */
#define MD_SER		0x0100				/* supr err rec NI */
#define MD_SHD		0x0010				/* shadow NI */
#define MD_SWP		0x0004				/* enb set wrp */
#define MD_IMF		0x0002				/* onl: ign fmte NI */
#define MD_NXU		0x0001				/* gus: next unit */
#define MD_RIP		0x0001				/* onl: allow rip NI */

/* End flags */

#define EF_LOG		0x0020				/* error log */
#define EF_SEX		0x0010				/* serious exc NI */

/* Controller flags */

#define CF_RPL		0x8000				/* ctrl bad blk repl */
#define CF_ATN		0x0080				/* enb attention */
#define CF_MSC		0x0040				/* enb misc msgs */
#define CF_OTH		0x0020				/* enb othr host msgs */		
#define CF_THS		0x0010				/* enb this host msgs */
#define	CF_MSK		(CF_ATN|CF_MSC|CF_OTH|CF_THS)

/* Unit flags */

#define UF_RPL		0x8000				/* ctrl bad blk repl */
#define UF_WPH		0x2000				/* wr prot hwre */
#define UF_WPS		0x1000				/* wr prot swre */
#define	UF_WPD		0x0100				/* wr prot data NI */
#define UF_RMV		0x0080				/* removable */
#define UF_CMW		0x0002				/* cmp writes NI */
#define UF_CMR		0x0001				/* cmp reads NI */
#define	UF_MSK		(UF_CMR|UF_CMW)

/* Error log flags */

#define	LF_SUC		0x0080				/* successful */
#define	LF_CON		0x0040				/* continuing */
#define	LF_BBR		0x0020				/* bad blk repl NI */
#define	LF_RCT		0x0010				/* err in repl NI */
#define	LF_SNR		0x0001				/* seq # reset */

/* Error log formats */

#define FM_CNT		0				/* port lf err */
#define FM_BAD		1				/* bad host addr */
#define FM_DSK		2				/* disk xfer */
#define FM_SDI		3				/* SDI err */
#define FM_SDE		4				/* sm disk err */
#define FM_RPL		9				/* bad blk repl */

/* Status codes */

#define	ST_SUC		0				/* successful */
#define	ST_CMD		1				/* invalid cmd */
#define	ST_ABO		2				/* aborted cmd */
#define	ST_OFL		3				/* unit offline */
#define	ST_AVL		4				/* unit avail */
#define	ST_MFE		5				/* media fmt err */
#define	ST_WPR		6				/* write prot err */
#define	ST_CMP		7				/* compare err */
#define	ST_DAT		8				/* data err */
#define	ST_HST		9				/* host acc err */
#define	ST_CNT		10				/* ctrl err */
#define	ST_DRV		11				/* drive err */
#define	ST_BBR		20				/* bad block */
#define	ST_DIA		31				/* diagnostic */
#define	ST_V_SUB	5				/* subcode */
#define ST_V_INV	8				/* invalid op */

/* Status subcodes */

#define SB_SUC_ON	(8 << ST_V_SUB)			/* already online */
#define SB_OFL_NV	(1 << ST_V_SUB)			/* no volume */
#define SB_AVL_INU	(32 << ST_V_SUB)		/* in use */
#define SB_WPR_SW	(128 << ST_V_SUB)		/* swre wlk */
#define SB_WPR_HW	(256 << ST_V_SUB)		/* hwre wlk */
#define SB_HST_OA	(1 << ST_V_SUB)			/* odd addr */
#define SB_HST_OC	(2 << ST_V_SUB)			/* odd count */
#define SB_HST_NXM	(3 << ST_V_SUB)			/* nx memory */
#define SB_HST_PTE	(5 << ST_V_SUB)			/* mapping err */

/* Status invalid command subcodes */

#define	I_OPCD		(8 << ST_V_INV)			/* inv opcode */
#define	I_BCNT		(12 << ST_V_INV)		/* inv byte cnt */
#define	I_LBN		(28 << ST_V_INV)		/* inv LBN */
#define	I_VRSN		(12 << ST_V_INV)		/* inv version */
#define	I_FMTI		(28 << ST_V_INV)		/* inv format */

/* Packet formats - note that all packet lengths must be multiples of 4 bytes */

/* Command packet header */

#define CMD_REFL	2				/* ref # */
#define CMD_REFH	3
#define CMD_UN		4				/* unit # */
/*			5				/* reserved */
#define CMD_OPC		6				/* opcode */
#define CMD_MOD		7				/* modifier */

#define CMD_OPC_V_OPC	0				/* opcode */
#define CMD_OPC_M_OPC	0xFF
#define CMD_OPC_V_CAA	8				/* cache NI */
#define CMD_OPC_M_CAA	0xFF

/* Response packet header */

#define RSP_LNT		12
#define RSP_REFL	2				/* ref # */
#define RSP_REFH	3
#define RSP_UN		4				/* unit # */
#define RSP_RSV		5				/* reserved */
#define RSP_OPF		6				/* opcd,flg */
#define RSP_STS		7				/* modifiers */

#define RSP_OPF_V_OPC	0				/* opcode */
#define RSP_OPF_V_FLG	8				/* flags */

/* Abort packet - 2 W parameter, 2 W status  */

#define ABO_LNT		16
#define ABO_REFL	8				/* ref # */
#define ABO_REFH	9

/* Avail packet - min size */

#define AVL_LNT		12

/* Get command status packet - 2 W parameter, 4 W of status */

#define GCS_LNT		20
#define GCS_REFL	8				/* ref # */
#define GCS_REFH	9
#define GCS_STSL	10				/* status */
#define GCS_STSH	11

/* Format packet - 8 W parameters, none returned */

#define FMT_LNT		12
#define FMT_IH		17				/* magic bit */

/* Get unit status packet - 18 W status */

#define GUS_LNT		48
#define GUS_MLUN	8				/* mlun */
#define GUS_UFL		9				/* flags */
#define GUS_RSVL	10				/* reserved */
#define GUS_RSVH	11
#define GUS_UIDA	12				/* unit ID */
#define GUS_UIDB	13
#define GUS_UIDC	14
#define GUS_UIDD	15
#define GUS_MEDL	16				/* media ID */
#define GUS_MEDH	17
#define GUS_SHUN	18				/* shadowing */
#define GUS_SHST	19
#define GUS_TRK		20				/* track */
#define GUS_GRP		21				/* group */
#define GUS_CYL		22				/* cylinder */
#define GUS_UVER	23				/* unit version */
#define GUS_RCTS	24				/* RCT size */
#define GUS_RBSC	25				/* RBNs, copies */

#define GUS_UIDD_V_MOD	0				/* unit model */
#define GUS_UIDD_V_CLS	8				/* unit class */
#define GUS_RB_V_RBNS	0				/* RBNs/track */
#define GUS_RB_V_RCTC	8				/* RCT copies */

/* Unit online - 2 W parameter, 16 W status */

#define ONL_LNT		44
#define ONL_MLUN	8				/* mlun */
#define ONL_UFL		9				/* flags */
#define ONL_RSVL	10				/* reserved */
#define ONL_RSVH	11
#define ONL_UIDA	12				/* unit ID */
#define ONL_UIDB	13
#define ONL_UIDC	14
#define ONL_UIDD	15
#define ONL_MEDL	16				/* media ID */
#define ONL_MEDH	17
#define ONL_SHUN	18				/* shadowing */
#define ONL_SHST	19
#define ONL_SIZL	20				/* size */
#define ONL_SIZH	21
#define ONL_VSNL	22				/* vol ser # */
#define ONL_VSNH	23

#define ONL_UIDD_V_MOD	0				/* unit model */
#define ONL_UIDD_V_CLS	8				/* unit class */

/* Set controller characteristics packet - 8 W parameters, 10 W status */

#define SCC_LNT		32
#define SCC_MSV		8				/* MSCP version */
#define SCC_CFL		9				/* flags */
#define SCC_TMO		10				/* timeout */
#define SCC_VER		11				/* ctrl version */
#define SCC_CIDA	12				/* ctrl ID */
#define SCC_CIDB	13
#define SCC_CIDC	14
#define SCC_CIDD	15
#define SCC_MBCL	16				/* max byte count */
#define SCC_MBCH	17

#define SCC_VER_V_SVER	0				/* swre vrsn */
#define SCC_VER_V_HVER	8				/* hwre vrsn */
#define SCC_CIDD_V_MOD	0				/* ctrl model */
#define SCC_CIDD_V_CLS	8				/* ctrl class */

/* Set unit characteristics - 2 W parameter, 16 W status - same as ONL */

#define SUC_LNT		44

/* Data transfer packet - 10 W parameters, 10 W status */

#define RW_LNT		32
#define RW_BCL		8				/* byte count */
#define RW_BCH		9
#define RW_BAL		10				/* buff desc */
#define RW_BAH		11
#define RW_MAPL		12				/* map table */
#define RW_MAPH		13
/*			14				/* reserved */
/*			15				/* reserved */
#define RW_LBNL		16				/* LBN */
#define RW_LBNH		17
#define RW_WBCL		18				/* working bc */
#define RW_WBCH		19
#define RW_WBAL		20				/* working ba */
#define RW_WBAH		21
#define RW_WBLL		22				/* working lbn */
#define RW_WBLH		23

/* Error log packet header */

#define ELP_REFL	2				/* ref # */
#define ELP_REFH	3
#define ELP_UN		4				/* unit */
#define ELP_SEQ		5
#define ELP_FF		6				/* fmt,flg */
#define ELP_EVT		7				/* event */

#define ELP_EV_V_FMT	0				/* format */
#define ELP_EV_V_FLG	8				/* flag */

/* Port last failure error log packet - 6 W status */

#define PLF_LNT		24				/* length */
#define PLF_CIDA	8				/* ctrl ID */
#define PLF_CIDB	9
#define PLF_CIDC	10
#define PLF_CIDD	11
#define PLF_VER		12				/* ctrl version */
#define PLF_ERR		13				/* err */

#define PLF_CIDD_V_MOD	0				/* ctrl model */
#define PLF_CIDD_V_CLS	8				/* ctrl class */
#define PLF_VER_V_SVER	0				/* swre ver */
#define PLF_VER_V_HVER	8				/* hwre ver */

/* Disk transfer error log packet - 18 W status */

#define DTE_LNT		48
#define DTE_CIDA	8				/* ctrl ID */
#define DTE_CIDB	9
#define DTE_CIDC	10
#define DTE_CIDD	11
#define DTE_VER		12				/* version */
#define DTE_MLUN	13				/* mlun */
#define DTE_UIDA	14				/* unit ID */
#define DTE_UIDB	15
#define DTE_UIDC	16
#define DTE_UIDD	17
#define DTE_UVER	18
#define DTE_SCYL	19				/* cylinder */
#define DTE_VSNL	20				/* vol ser # */
#define DTE_VSNH	21
#define DTE_D1		22				/* dev params */
#define DTE_D2		23
#define DTE_D3		24
#define DTE_D4		25

#define DTE_CIDD_V_MOD	0				/* ctrl model */
#define DTE_CIDD_V_CLS	8				/* ctrl class */
#define DTE_VER_V_SVER	0				/* ctrl swre ver */
#define DTE_VER_V_HVER	8				/* ctrl hwre ver */
#define DTE_UIDD_V_MOD	0				/* unit model */
#define DTE_UIDD_V_CLS	8				/* unit class */
#define DTE_D2_V_SECT	8
#define DTE_D3_V_SURF	0
#define DTE_D3_V_CYL	8

/* Host bus error log packet - 8 W status */

#define HBE_LNT		28
#define HBE_CIDA	8				/* ctrl ID */
#define HBE_CIDB	9
#define HBE_CIDC	10
#define HBE_CIDD	11
#define HBE_VER		12				/* ctrl version */
#define HBE_RSV		13				/* reserved */
#define HBE_BADL	14				/* bad address */
#define HBE_BADH	15

#define HBE_CIDD_V_MOD	0				/* ctrl model */
#define HBE_CIDD_V_CLS	8				/* ctrl class */
#define HBE_VER_V_SVER	0				/* ctrl swre ver */
#define HBE_VER_V_HVER	8				/* ctrl hwre ver */

/* Unit now available attention message - 10 W status, same as
   first 10 W of status from get unit status
*/

#define UNA_LNT		32
