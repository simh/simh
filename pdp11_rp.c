/* pdp11_rp.c - RP04/05/06/07 RM02/03/05/80 "Massbus style" disk controller

   Copyright (c) 1993-2000, Robert M Supnik

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

   14-Apr-99	RMS	Changed t_addr to unsigned
   05-Oct-98	RMS	Fixed bug, failing to interrupt on go error
   04-Oct-98	RMS	Changed names to allow coexistence with RH/TU77
   12-Nov-97	RMS	Added bad block table command.
   10-Aug-97	RMS	Fixed bugs in interrupt handling.

   The "Massbus style" disks consisted of several different large
   capacity drives interfaced through a reasonably common (but not
   100% compatible) family of interfaces into a 22b direct addressing
   port.  On the PDP-11/70, this was the Massbus; but on 22b Qbus
   systems, this was through many different third party controllers
   which emulated the Massbus interface.

   WARNING: This controller is somewhat abstract.  It is intended to run
   the operating system drivers for the PDP-11 operating systems and
   nothing more.  Most error and all diagnostic functions have been
   omitted.  In addition, the controller conflates the RP04/05/06 series
   controllers with the RM02/03/05/80 series controllers and with the
   RP07 controller.  There are actually significant differences, which
   have been highlighted where noticed.
*/

#include "pdp11_defs.h"
#include <math.h>

#define RP_NUMDR	8				/* #drives */
#define RP_NUMWD	256				/* words/sector */
#define GET_SECTOR(x,d)	((int) fmod (sim_gtime() / ((double) (x)), \
			((double) drv_tab[d].sect)))

/* Flags in the unit flags word */

#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_V_DTYPE	(UNIT_V_UF + 1)			/* disk type */
#define UNIT_M_DTYPE	7
#define UNIT_V_AUTO	(UNIT_V_UF + 4)			/* autosize */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_DTYPE	(UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO	(1 << UNIT_V_AUTO)
#define UNIT_W_UF	6				/* user flags width */
#define UNIT_V_DUMMY	(UNIT_V_UF + UNIT_W_UF)	/* dummy flag */
#define UNIT_DUMMY	(1 << UNIT_V_DUMMY)
#define GET_DTYPE(x)	(((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */

#define CYL		u3				/* current cylinder */
#define FUNC		u4				/* function */

/* RPCS1 - 176700 - control/status 1 */

#define CS1_GO		CSR_GO				/* go */
#define CS1_V_FNC	1				/* function pos */
#define CS1_M_FNC	037				/* function mask */
#define CS1_FNC		(CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP	000				/* no operation */
#define  FNC_UNLOAD	001				/* unload */
#define  FNC_SEEK	002				/* seek */
#define  FNC_RECAL	003				/* recalibrate */
#define  FNC_DCLR	004				/* drive clear */
#define  FNC_RELEASE	005				/* port release */
#define  FNC_OFFSET	006				/* offset */
#define  FNC_RETURN	007				/* return to center */
#define  FNC_PRESET	010				/* read-in preset */
#define  FNC_PACK	011				/* pack acknowledge */
#define  FNC_SEARCH	014				/* search */
#define  FNC_WCHK	024				/* write check */
#define  FNC_WRITE	030				/* write */
#define  FNC_READ	034				/* read */
#define CS1_IE		CSR_IE				/* int enable */
#define CS1_DONE	CSR_DONE			/* ready */
#define CS1_V_UAE	8				/* Unibus addr ext */
#define CS1_M_UAE	03
#define CS1_UAE		(CS1_M_UAE << CS1_V_UAE)
#define CS1_DVA		0004000				/* drive avail NI */
#define CS1_MCPE	0020000				/* Mbus par err NI */
#define CS1_TRE		0040000				/* transfer err */
#define CS1_SC		0100000				/* special cond */
#define CS1_MBZ		0012000
#define CS1_RW		(CS1_FNC | CS1_IE | CS1_UAE)
#define GET_FNC(x)	(((x) >> CS1_V_FNC) & CS1_M_FNC)

/* RPWC - 176702 - word count */

/* RPBA - 176704 - base address */

#define BA_MBZ		0000001				/* must be zero */

/* RPDA - 176706 - sector/track */

#define DA_V_SC		0				/* sector pos */
#define DA_M_SC		077				/* sector mask */
#define DA_V_SF		8				/* track pos */
#define DA_M_SF		077				/* track mask */
#define DA_MBZ		0140300
#define GET_SC(x)	(((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)	(((x) >> DA_V_SF) & DA_M_SF)

/* RPCS2 - 176710 - control/status 2 */

#define CS2_V_UNIT	0				/* unit pos */
#define CS2_M_UNIT	07				/* unit mask */
#define CS2_UNIT	(CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI		0000010				/* addr inhibit NI */
#define CS2_PAT		0000020				/* parity test NI */
#define CS2_CLR		0000040				/* controller clear */
#define CS2_IR		0000100				/* input ready */
#define CS2_OR		0000200				/* output ready */
#define CS2_MDPE	0000400				/* Mbus par err NI */
#define CS2_MXF		0001000				/* missed xfer NI */
#define CS2_PGE		0002000				/* program err */
#define CS2_NEM		0004000				/* nx mem err */
#define CS2_NED		0010000				/* nx drive err */
#define CS2_PE		0020000				/* parity err NI */
#define CS2_WCE		0040000				/* write check err */
#define CS2_DLT		0100000				/* data late NI */
#define CS2_MBZ		(CS2_CLR)
#define CS2_RW		(CS2_UNIT | CS2_UAI | CS2_PAT)
#define CS2_ERR		(CS2_MDPE | CS2_MXF | CS2_PGE | CS2_NEM | \
			 CS2_NED | CS2_PE | CS2_WCE | CS2_DLT )
#define GET_UNIT(x)	(((x) >> CS2_V_UNIT) & CS2_M_UNIT)

/* RPDS - 176712 - drive status */

#define DS_OF		0000001				/* offset mode */
#define DS_VV		0000100				/* volume valid */
#define DS_RDY		0000200				/* drive ready */
#define DS_DPR		0000400				/* drive present */
#define DS_PGM		0001000				/* programable NI */
#define DS_LST		0002000				/* last sector */
#define DS_WRL		0004000				/* write locked */
#define DS_MOL		0010000				/* medium online */
#define DS_PIP		0020000				/* pos in progress */
#define DS_ERR		0040000				/* error */
#define DS_ATA		0100000				/* attention active */
#define DS_MBZ		0000076

/* RPER1 - 176714 - error status 1 */

#define ER1_ILF		0000001				/* illegal func */
#define ER1_ILR		0000002				/* illegal register */
#define ER1_RMR		0000004				/* reg mod refused */
#define ER1_PAR		0000010				/* parity err */
#define ER1_FER		0000020				/* format err NI */
#define ER1_WCF		0000040				/* write clk fail NI */
#define ER1_ECH		0000100				/* ECC hard err NI */
#define ER1_HCE		0000200				/* hdr comp err NI */
#define ER1_HCR		0000400				/* hdr CRC err NI */
#define ER1_AOE		0001000				/* addr ovflo err */
#define ER1_IAE		0002000				/* invalid addr err */
#define ER1_WLE		0004000				/* write lock err */
#define ER1_DTE		0010000				/* drive time err NI */
#define ER1_OPI		0020000				/* op incomplete */
#define ER1_UNS		0040000				/* drive unsafe */
#define ER1_DCK		0100000				/* data check NI */

/* RPAS - 176716 - attention summary */

#define AS_U0		0000001				/* unit 0 flag */

/* RPLA - 176720 - look ahead register */

#define LA_V_SC		6				/* sector pos */

/* RPDB - 176722 - data buffer */
/* RPMR - 176724 - maintenace register */
/* RPDT - 176726 - drive type */
/* RPSN - 176730 - serial number */

/* RPOF - 176732 - offset register */

#define OF_HCI		0002000				/* hdr cmp inh NI */
#define OF_ECI		0004000				/* ECC inhibit NI */
#define OF_F22		0010000				/* format NI */
#define OF_MBZ		0161400

/* RPDC - 176734 - desired cylinder */

#define DC_V_CY		0				/* cylinder pos */
#define DC_M_CY		01777				/* cylinder mask */
#define DC_MBZ		0176000
#define GET_CY(x)	(((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,fs,d)	((((GET_CY (c) * drv_tab[d].surf) + \
			    GET_SF (fs)) * drv_tab[d].sect) + GET_SC (fs))

/* RPCC - 176736 - current cylinder - unimplemented */
/* RPER2 - 176740 - error status 2 - drive unsafe conditions - unimplemented */
/* RPER3 - 176742 - error status 3 - more unsafe conditions - unimplemented */
/* RPEC1 - 176744 - ECC status 1 - unimplemented */
/* RPEC2 - 176746 - ECC status 2 - unimplemented */

/* RPBAE - 176750 - bus address extension */

#define AE_M_MAE	0				/* addr ext pos */
#define AE_V_MAE	077				/* addr ext mask */
#define AE_MBZ		0177700

/* RPCS3 - 176752 - control/status 3 - unused except for duplicate IE */

#define CS3_MBZ		0177660

/* This controller supports many different disk drive types:

   type		#sectors/	#surfaces/	#cylinders/
		 surface	 cylinder	 drive

   RM02/3   	32		5		823		=67MB
   RP04/5   	22		19		411		=88MB
   RM80   	31		14		559		=124MB
   RP06   	22		19		815		=176MB
   RM05   	32		19		823		=256MB
   RP07		50		32		630		=516MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.
*/

#define RM03_DTYPE	0
#define RM03_SECT	32
#define RM03_SURF	5
#define RM03_CYL	823
#define RM03_DEV	020024
#define RM03_SIZE	(RM03_SECT * RM03_SURF * RM03_CYL * RP_NUMWD)

#define RP04_DTYPE	1
#define RP04_SECT	22
#define RP04_SURF	19
#define RP04_CYL	411
#define RP04_DEV	020020
#define RP04_SIZE	(RP04_SECT * RP04_SURF * RP04_CYL * RP_NUMWD)

#define RM80_DTYPE	2
#define RM80_SECT	31
#define RM80_SURF	14
#define RM80_CYL	559
#define RM80_DEV	020026
#define RM80_SIZE	(RM80_SECT * RM80_SURF * RM80_CYL * RP_NUMWD)

#define RP06_DTYPE	3
#define RP06_SECT	22
#define RP06_SURF	19
#define RP06_CYL	815
#define RP06_DEV	020022
#define RP06_SIZE	(RP06_SECT * RP06_SURF * RP06_CYL * RP_NUMWD)

#define RM05_DTYPE	4
#define RM05_SECT	32
#define RM05_SURF	19
#define RM05_CYL	823
#define RM05_DEV	020027
#define RM05_SIZE	(RM05_SECT * RM05_SURF * RM05_CYL * RP_NUMWD)

#define RP07_DTYPE	5
#define RP07_SECT	50
#define RP07_SURF	32
#define RP07_CYL	630
#define RP07_DEV	020042
#define RP07_SIZE	(RP07_SECT * RP07_SURF * RP07_CYL * RP_NUMWD)

struct drvtyp {
	int	sect;					/* sectors */
	int	surf;					/* surfaces */
	int	cyl;					/* cylinders */
	int	size;					/* #blocks */
	int	devtype;				/* device type */
};

struct drvtyp drv_tab[] = {
	{ RM03_SECT, RM03_SURF, RM03_CYL, RM03_SIZE, RM03_DEV },
	{ RP04_SECT, RP04_SURF, RP04_CYL, RP04_SIZE, RP04_DEV },
	{ RM80_SECT, RM80_SURF, RM80_CYL, RM80_SIZE, RM80_DEV },
	{ RP06_SECT, RP06_SURF, RP06_CYL, RP06_SIZE, RP06_DEV },
	{ RM05_SECT, RM05_SURF, RM05_CYL, RM05_SIZE, RM05_DEV },
	{ RP07_SECT, RP07_SURF, RP07_CYL, RP07_SIZE, RP07_DEV },
	{ 0 }  };

extern int32 int_req;
extern unsigned int16 *M;				/* memory */
extern UNIT cpu_unit;
int32 rpcs1 = 0;					/* control/status 1 */
int32 rpwc = 0;						/* word count */
int32 rpba = 0;						/* bus address */
int32 rpda = 0;						/* track/sector */
int32 rpcs2 = 0;					/* control/status 2 */
int32 rpds[RP_NUMDR] = { 0 };				/* drive status */
int32 rper1[RP_NUMDR] = { 0 };				/* error status 1 */
int32 rpdb = 0;						/* data buffer */
int32 rpmr = 0;						/* maint register */
int32 rpof = 0;						/* offset */
int32 rpdc = 0;						/* cylinder */
int32 rper2 = 0;					/* error status 2 */
int32 rper3 = 0;					/* error status 3 */
int32 rpec1 = 0;					/* ECC correction 1 */
int32 rpec2 = 0;					/* ECC correction 2 */
int32 rpbae = 0;					/* bus address ext */
int32 rpcs3 = 0;					/* control/status 3 */
int32 rp_stopioe = 1;					/* stop on error */
int32 rp_swait = 10;					/* seek time */
int32 rp_rwait = 10;					/* rotate time */
int reg_in_drive[32] = {
 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

void update_rpcs (int32 flags, int32 drv);
void rp_go (int32 drv);
t_stat rp_set_size (UNIT *uptr, int32 value);
t_stat rp_set_bad (UNIT *uptr, int32 value);
t_stat rp_svc (UNIT *uptr);
t_stat rp_reset (DEVICE *dptr);
t_stat rp_boot (int32 unitno);
t_stat rp_attach (UNIT *uptr, char *cptr);
t_stat rp_detach (UNIT *uptr);
extern t_stat sim_activate (UNIT *uptr, int32 delay);
extern t_stat sim_cancel (UNIT *uptr);
extern int32 sim_is_active (UNIT *uptr);
extern size_t fxread (void *bptr, size_t size, size_t count, FILE *fptr);
extern size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr);
extern t_stat pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds);

/* RP data structures

   rp_dev	RP device descriptor
   rp_unit	RP unit list
   rp_reg	RP register list
   rp_mod	RP modifier list
*/

UNIT rp_unit[] = {
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) },
	{ UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
		(RM03_DTYPE << UNIT_V_DTYPE), RM03_SIZE) }  };

REG rp_reg[] = {
	{ ORDATA (RPCS1, rpcs1, 16) },
	{ ORDATA (RPWC, rpwc, 16) },
	{ ORDATA (RPBA, rpba, 16) },
	{ ORDATA (RPDA, rpda, 16) },
	{ ORDATA (RPCS2, rpcs2, 16) },
	{ ORDATA (RPOF, rpof, 16) },
	{ ORDATA (RPDC, rpdc, 16) },
	{ ORDATA (RPER2, rper2, 16) },
	{ ORDATA (RPER3, rper3, 16) },
	{ ORDATA (RPEC1, rpec1, 16) },
	{ ORDATA (RPEC2, rpec2, 16) },
	{ ORDATA (RPMR, rpmr, 16) },
	{ ORDATA (RPDB, rpdb, 16) },
	{ ORDATA (RPBAE, rpbae, 6) },
	{ ORDATA (RPCS3, rpcs3, 16) },
	{ FLDATA (INT, int_req, INT_V_RP) },
	{ FLDATA (SC, rpcs1, CSR_V_ERR) },
	{ FLDATA (DONE, rpcs1, CSR_V_DONE) },
	{ FLDATA (IE, rpcs1, CSR_V_IE) },
	{ DRDATA (STIME, rp_swait, 24), REG_NZ + PV_LEFT },
	{ DRDATA (RTIME, rp_rwait, 24), REG_NZ + PV_LEFT },
	{ ORDATA (RPDS0, rpds[0], 16) },
	{ ORDATA (RPDS1, rpds[1], 16) },
	{ ORDATA (RPDS2, rpds[2], 16) },
	{ ORDATA (RPDS3, rpds[3], 16) },
	{ ORDATA (RPDS4, rpds[4], 16) },
	{ ORDATA (RPDS5, rpds[5], 16) },
	{ ORDATA (RPDS6, rpds[6], 16) },
	{ ORDATA (RPDS7, rpds[7], 16) },
	{ ORDATA (RPDE0, rper1[0], 16) },
	{ ORDATA (RPDE1, rper1[1], 16) },
	{ ORDATA (RPDE2, rper1[2], 16) },
	{ ORDATA (RPDE3, rper1[3], 16) },
	{ ORDATA (RPDE4, rper1[4], 16) },
	{ ORDATA (RPDE5, rper1[5], 16) },
	{ ORDATA (RPDE6, rper1[6], 16) },
	{ ORDATA (RPDE7, rper1[7], 16) },
	{ GRDATA (FLG0, rp_unit[0].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG1, rp_unit[1].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG2, rp_unit[2].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG3, rp_unit[3].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG4, rp_unit[4].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG5, rp_unit[5].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG6, rp_unit[6].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ GRDATA (FLG7, rp_unit[7].flags, 8, UNIT_W_UF, UNIT_V_UF - 1),
		  REG_HRO },
	{ FLDATA (STOP_IOE, rp_stopioe, 0) },
	{ NULL }  };

MTAB rp_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
	{ UNIT_DUMMY, 0, NULL, "BADBLOCK", &rp_set_bad },
	{ (UNIT_DTYPE+UNIT_ATT), (RM03_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RM03", NULL, NULL },
	{ (UNIT_DTYPE+UNIT_ATT), (RP04_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RP04", NULL, NULL },
	{ (UNIT_DTYPE+UNIT_ATT), (RM80_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RM80", NULL, NULL },
	{ (UNIT_DTYPE+UNIT_ATT), (RP06_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RP06", NULL, NULL },
	{ (UNIT_DTYPE+UNIT_ATT), (RM05_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RM05", NULL, NULL },
	{ (UNIT_DTYPE+UNIT_ATT), (RP07_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
		"RP07", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM03_DTYPE << UNIT_V_DTYPE),
		"RM03", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP04_DTYPE << UNIT_V_DTYPE),
		"RP04", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM80_DTYPE << UNIT_V_DTYPE),
		"RM80", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP06_DTYPE << UNIT_V_DTYPE),
		"RP06", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RM05_DTYPE << UNIT_V_DTYPE),
		"RM05", NULL, NULL },
	{ (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (RP07_DTYPE << UNIT_V_DTYPE),
		"RP07", NULL, NULL },
	{ (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
	{ UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
 	{ (UNIT_AUTO+UNIT_DTYPE), (RM03_DTYPE << UNIT_V_DTYPE),
		NULL, "RM03", &rp_set_size },
	{ (UNIT_AUTO+UNIT_DTYPE), (RP04_DTYPE << UNIT_V_DTYPE),
		NULL, "RP04", &rp_set_size }, 
 	{ (UNIT_AUTO+UNIT_DTYPE), (RM80_DTYPE << UNIT_V_DTYPE),
		NULL, "RM80", &rp_set_size },
 	{ (UNIT_AUTO+UNIT_DTYPE), (RP06_DTYPE << UNIT_V_DTYPE),
		NULL, "RP06", &rp_set_size },
 	{ (UNIT_AUTO+UNIT_DTYPE), (RM05_DTYPE << UNIT_V_DTYPE),
		NULL, "RM05", &rp_set_size },
 	{ (UNIT_AUTO+UNIT_DTYPE), (RP07_DTYPE << UNIT_V_DTYPE),
		NULL, "RP07", &rp_set_size },
	{ 0 }  };

DEVICE rp_dev = {
	"RP", rp_unit, rp_reg, rp_mod,
	RP_NUMDR, 8, 30, 1, 8, 16,
	NULL, NULL, &rp_reset,
	&rp_boot, &rp_attach, &rp_detach };

/* I/O dispatch routines, I/O addresses 17776700 - 17776776 */

t_stat rp_rd (int32 *data, int32 PA, int32 access)
{
int32 drv, dtype, i, j;

drv = GET_UNIT (rpcs2);					/* get current unit */
dtype = GET_DTYPE (rp_unit[drv].flags);			/* get drive type */
j = (PA >> 1) & 037;					/* get reg offset */
if (reg_in_drive[j] && (rp_unit[drv].flags & UNIT_DIS)) {	/* nx disk */
	rpcs2 = rpcs2 | CS2_NED;			/* set error flag */
	update_rpcs (CS1_SC, drv);			/* request intr */
	*data = 0;
	return SCPE_OK;  }

update_rpcs (0, drv);					/* update status */
switch (j) {						/* decode PA<5:1> */
case 000:						/* RPCS1 */
	*data = rpcs1;
	break;
case 001:						/* RPWC */
	*data = rpwc;
	break;
case 002:						/* RPBA */
	*data = rpba = rpba & ~BA_MBZ;
	break;
case 003:						/* RPDA */
	*data = rpda = rpda & ~DA_MBZ;
	break;
case 004:						/* RPCS2 */
	*data = rpcs2 = (rpcs2 & ~CS2_MBZ) | CS2_IR | CS2_OR;
	break;
case 005:						/* RPDS */
	*data = rpds[drv];
	break;
case 006:						/* RPER1 */
	*data = rper1[drv];
	break;
case 007:						/* RPAS */
	*data = 0;
	for (i = 0; i < RP_NUMDR; i++)
		if (rpds[i] & DS_ATA) *data = *data | (AS_U0 << i);
	break;
case 010:						/* RPLA */
	*data = GET_SECTOR (rp_rwait, dtype) << LA_V_SC;
	break;
case 011:						/* RPDB */
	*data = rpdb;
	break;
case 012:						/* RPMR */
	*data = rpmr;
	break;
case 013:						/* RPDT */
	*data = drv_tab[dtype].devtype;
	break;
case 014:						/* RPSN */
	*data = 020 | (drv + 1);
	break;
case 015:						/* RPOF */
	*data = rpof = rpof & ~OF_MBZ;
	break;
case 016:						/* RPDC */
	*data = rpdc = rpdc & ~DC_MBZ;
	break;
case 017:						/* RPCC, RMHR */
	*data = rp_unit[drv].CYL;
	break;
case 020:						/* RPER2, RMMR2 */
	*data = rper2;
	break;
case 021:						/* RPER3, RMER2 */
	*data = rper3;
	break;
case 022:						/* RPEC1 */
	*data = rpec1;
	break;
case 023:						/* RPEC2 */
	*data = rpec2;
	break;
case 024:						/* RPBAE */
	*data = rpbae = rpbae & ~AE_MBZ;
	break;
case 025:						/* RPCS3 */
	*data = rpcs3 = (rpcs3 & ~(CS1_IE | CS3_MBZ)) | (rpcs1 & CS1_IE);
	break;
default:						/* all others */
	rper1[drv] = rper1[drv] | ER1_ILR;
	update_rpcs (0, drv);
	break;  }
return SCPE_OK;
}

t_stat rp_wr (int32 data, int32 PA, int32 access)
{
int32 drv, i, j;

drv = GET_UNIT (rpcs2);					/* get current unit */
j = (PA >> 1) & 037;					/* get reg offset */
if (reg_in_drive[j] && (rp_unit[drv].flags & UNIT_DIS)) {	/* nx disk */
	rpcs2 = rpcs2 | CS2_NED;			/* set error flag */
	update_rpcs (CS1_SC, drv);			/* request intr */
	return SCPE_OK;  }
if (reg_in_drive[j] && sim_is_active (&rp_unit[drv])) {	/* unit busy? */
	rper1[drv] = rper1[drv] | ER1_RMR;		/* won't write */
	update_rpcs (0, drv);
	return SCPE_OK;  }

switch (j) {						/* decode PA<5:1> */
case 000:						/* RPCS1 */
	if (access == WRITEB) data = (PA & 1)?
		(rpcs1 & 0377) | (data << 8): (rpcs1 & ~0377) | data;
	if ((data & CS1_IE) == 0) int_req = int_req & ~INT_RP;
	else if ((((rpcs1 & CS1_IE) == 0) && (rpcs1 & CS1_DONE)) ||
		(data & CS1_DONE)) int_req = int_req | INT_RP;
	rpcs1 = (rpcs1 & ~CS1_RW) | (data & CS1_RW);
	rpbae = (rpbae & ~CS1_M_UAE) | ((rpcs1 >> CS1_V_UAE) & CS1_M_UAE);
	rpcs3 = (rpcs3 & ~CS1_IE) | (rpcs1 & CS1_IE);
	if (data & CS1_GO) {				/* new command? */
		if (rpcs1 & CS1_DONE) rp_go (drv);	/* start if not busy */
		else rpcs2 = rpcs2 | CS2_PGE;  }	/* else prog error */
	break;	
case 001:						/* RPWC */
	if (access == WRITEB) data = (PA & 1)?
		(rpwc & 0377) | (data << 8): (rpwc & ~0377) | data;
	rpwc = data;
	break;
case 002:						/* RPBA */
	if (access == WRITEB) data = (PA & 1)?
		(rpba & 0377) | (data << 8): (rpba & ~0377) | data;
	rpba = data & ~BA_MBZ;
	break;
case 003:						/* RPDA */
	if (access == WRITEB) data = (PA & 1)?
		(rpda & 0377) | (data << 8): (rpda & ~0377) | data;
	rpda = data & ~DA_MBZ;
	break;
case 004:						/* RPCS2 */
	if (access == WRITEB) data = (PA & 1)?
		(rpcs2 & 0377) | (data << 8): (rpcs2 & ~0377) | data;
	if (data & CS2_CLR) rp_reset (&rp_dev);
	else rpcs2 = (rpcs2 & ~CS2_RW) | (data & CS2_RW) | CS2_IR | CS2_OR;
	drv = GET_UNIT (rpcs2);
	break;
case 006:						/* RPER1 */
	if (access == WRITEB) break;
	rper1[drv] = rper1[drv] & data;
	break;
case 007:						/* RPAS */
	if (PA & 1) break;
	for (i = 0; i < RP_NUMDR; i++)
		if (data & (AS_U0 << i)) rpds[i] = rpds[i] & ~DS_ATA;
	break;
case 011:						/* RPDB */
	if (access == WRITEB) data = (PA & 1)?
		(rpdb & 0377) | (data << 8): (rpdb & ~0377) | data;
	rpdb = data;
	break;
case 012:						/* RPMR */
	if (access == WRITEB) data = (PA & 1)?
		(rpmr & 0377) | (data << 8): (rpmr & ~0377) | data;
	rpmr = data;
	break;
case 015:						/* RPOF */
	if (access == WRITEB) data = (PA & 1)?
		(rpof & 0377) | (data << 8): (rpof & ~0377) | data;
	rpof = data & ~OF_MBZ;
	break;
case 016:						/* RPDC */
	if (access == WRITEB) data = (PA & 1)?
		(rpdc & 0377) | (data << 8): (rpdc & ~0377) | data;
	rpdc = data & ~DC_MBZ;
	break;
case 024:						/* RPBAE */
	if (PA & 1) break;
	rpbae = data & ~AE_MBZ;
	rpcs1 = (rpcs1 & ~CS1_UAE) | ((rpbae << CS1_V_UAE) & CS1_UAE);
	break;
case 025:						/* RPCS3 */
	if (PA & 1) break;
	rpcs3 = data & ~CS3_MBZ;
	if ((data & CS1_IE) == 0) int_req = int_req & ~INT_RP;
	else if (((rpcs1 & CS1_IE) == 0) && (rpcs1 & CS1_DONE))
			int_req = int_req | INT_RP;
	rpcs1 = (rpcs1 & ~CS1_IE) | (rpcs3 & CS1_IE);
	break;
case 005:						/* RPDS */
case 010:						/* RPLA */
case 013:						/* RPDT */
case 014:						/* RPSN */
case 017:						/* RPDC, RMHR */
case 020:						/* RPER2, RMMN2 */
case 021:						/* RPER3, RMER2 */
case 022:						/* RPEC1 */
case 023:						/* RPEC2 */
	break;						/* read only */
default:						/* all others */
	rper1[drv] = rper1[drv] | ER1_ILR;
	break;  }					/* end switch */
update_rpcs (0, drv);					/* update status */
return SCPE_OK;
}

/* Initiate operation */

void rp_go (int32 drv)
{

int32 dc, dtype, fnc;
UNIT *uptr;

fnc = GET_FNC (rpcs1);					/* get function */
uptr = rp_dev.units + drv;				/* get unit */
if (uptr -> flags & UNIT_DIS) {				/* nx unit? */
	rpcs2 = rpcs2 | CS2_NED;			/* set error flag */
	update_rpcs (CS1_SC, drv);			/* request intr */
	return;  }
if (((fnc != FNC_DCLR) && (rpds[drv] & DS_ERR)) ||	/* not clear & err? */
	((rpds[drv] & DS_RDY) == 0)) {			/* not ready? */
	rpcs2 = rpcs2 | CS2_PGE;			/* set error flag */
	update_rpcs (CS1_SC, drv);			/* request intr */
	return;  }
dtype = GET_DTYPE (uptr -> flags);			/* get drive type */
rpds[drv] = rpds[drv] & ~DS_ATA;			/* clear attention */
dc = rpdc;						/* assume seek, sch */

switch (fnc) {						/* case on function */
case FNC_DCLR:						/* drive clear */
	rpda = 0;					/* clear disk addr */
	rper1[drv] = rper2 = rper3 = 0;			/* clear errors */
case FNC_NOP:						/* no operation */
case FNC_RELEASE:					/* port release */
	return;

case FNC_PRESET:					/* read-in preset */
	rpdc = 0;					/* clear disk addr */
	rpda = 0;
	rpof = 0;					/* clear offset */
case FNC_PACK:						/* pack acknowledge */
	rpds[drv] = rpds[drv] | DS_VV;			/* set volume valid */
	return;

case FNC_OFFSET:					/* offset mode */
case FNC_RETURN:
	uptr -> FUNC = fnc;				/* save function */
	rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;	/* set positioning */
	sim_activate (uptr, rp_swait);			/* time operation */
	return;

case FNC_UNLOAD:					/* unload */
case FNC_RECAL:						/* recalibrate */
	dc = 0;						/* seek to 0 */
case FNC_SEEK:						/* seek */
case FNC_SEARCH:					/* search */
	if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||	/* bad cylinder */
	    (GET_SF (rpda) >= drv_tab[dtype].surf) ||	/* bad surface */
            (GET_SC (rpda) >= drv_tab[dtype].sect)) {	/* or bad sector? */
		rper1[drv] = rper1[drv] | ER1_IAE;
		break;  }
	rpds[drv] = (rpds[drv] & ~DS_RDY) | DS_PIP;	/* set positioning */
	sim_activate (uptr, rp_swait * abs (dc - uptr -> CYL));
	uptr -> FUNC = fnc;				/* save function */
	uptr -> CYL = dc;				/* save cylinder */
	return;

case FNC_WRITE:						/* write */
case FNC_WCHK:						/* write check */
case FNC_READ:						/* read */
	rpcs2 = rpcs2 & ~CS2_ERR;			/* clear errors */
	rpcs1 = rpcs1 & ~(CS1_TRE | CS1_MCPE | CS1_DONE);
	if ((GET_CY (dc) >= drv_tab[dtype].cyl) ||	/* bad cylinder */
	    (GET_SF (rpda) >= drv_tab[dtype].surf) ||	/* bad surface */
            (GET_SC (rpda) >= drv_tab[dtype].sect)) {	/* or bad sector? */
		rper1[drv] = rper1[drv] | ER1_IAE;
		update_rpcs (CS1_DONE | CS1_TRE, drv);	/* set done, err */
		return;  }
	rpds[drv] = rpds[drv] & ~DS_RDY;		/* clear drive rdy */
	sim_activate (uptr, rp_rwait + (rp_swait * abs (dc - uptr -> CYL)));
	uptr -> FUNC = fnc;				/* save function */
	uptr -> CYL = dc;				/* save cylinder */
	return;

default:						/* all others */
	rper1[drv] = rper1[drv] | ER1_ILF;		/* not supported */
	break;  }
update_rpcs (CS1_SC, drv);				/* error, req intr */
return;
}

/* Service unit timeout

   Complete movement or data transfer command
   Unit must exist - can't remove an active unit
   Unit must be attached - detach cancels in progress operations
*/

static unsigned int16 fill[RP_NUMWD] = { 0 };
t_stat rp_svc (UNIT *uptr)
{
int32 dtype, drv, err;
int32 pa, wc, awc, twc, da, fillc;

dtype = GET_DTYPE (uptr -> flags);			/* get drive type */
drv = uptr - rp_dev.units;				/* get drv number */
rpds[drv] = (rpds[drv] & ~DS_PIP) | DS_RDY;		/* change drive status */

switch (uptr -> FUNC) {					/* case on function */
case FNC_OFFSET:					/* offset */
	rpds[drv] = rpds[drv] | DS_OF | DS_ATA;		/* set offset, attention */
	update_rpcs (CS1_SC, drv);
	return SCPE_OK;
case FNC_RETURN:					/* return to centerline */
	rpds[drv] = (rpds[drv] & ~DS_OF) | DS_ATA;	/* clear offset, set attn */
	update_rpcs (CS1_SC, drv);
	return SCPE_OK;	
case FNC_UNLOAD:					/* unload */
	rp_detach (uptr);				/* detach unit */
	return SCPE_OK;
case FNC_RECAL:						/* recalibrate */
case FNC_SEARCH:					/* search */
case FNC_SEEK:						/* seek */
	rpds[drv] = rpds[drv] | DS_ATA;			/* set attention */
	update_rpcs (CS1_SC, drv);
	return SCPE_OK;

case FNC_WRITE:						/* write */
	if (uptr -> flags & UNIT_WLK) {			/* write locked? */
		rper1[drv] = rper1[drv] | ER1_WLE;	/* set drive error */
		update_rpcs (CS1_DONE | CS1_TRE, drv);	/* set done, err */
		return SCPE_OK;  }
case FNC_WCHK:						/* write check */
case FNC_READ:						/* read */
	pa = ((rpbae << 16) | rpba) >> 1;		/* get mem addr */
	da = GET_DA (rpdc, rpda, dtype) * RP_NUMWD;	/* get disk addr */
	twc = 0200000 - rpwc;				/* get true wc */
	if (((t_addr) (pa + twc)) > (MEMSIZE / 2)) {	/* mem overrun? */
		rpcs2 = rpcs2 | CS2_NEM;
		wc = ((MEMSIZE / 2) - pa);	
		if (wc < 0) {				/* abort transfer? */
			update_rpcs (CS1_DONE, drv);	/* set done */
			return SCPE_OK;  }  }
	else wc = twc;
	if ((da + twc) > drv_tab[dtype].size) {		/* disk overrun? */
		rper1[drv] = rper1[drv] | ER1_AOE;
		if (wc > (drv_tab[dtype].size - da))
			wc = drv_tab[dtype].size - da;  }

	err = fseek (uptr -> fileref, da * sizeof (int16), SEEK_SET);

	if ((uptr -> FUNC == FNC_READ) && (err == 0)) {	/* read? */
		awc = fxread (&M[pa], sizeof (int16), wc, uptr -> fileref);
		for ( ; awc < wc; awc++) M[pa + awc] = 0;
		err = ferror (uptr -> fileref);  }

	if ((uptr -> FUNC == FNC_WRITE) && (err == 0)) {	/* write? */
		fxwrite (&M[pa], sizeof (int16), wc, uptr -> fileref);
		err = ferror (uptr -> fileref);
		if ((err == 0) && (fillc = (wc & (RP_NUMWD - 1)))) {
			fxwrite (fill, sizeof (int16), fillc, uptr -> fileref);
			err = ferror (uptr -> fileref);  }  }

	if ((uptr -> FUNC == FNC_WCHK) && (err == 0)) {	/* wcheck? */
		twc = wc;				/* xfer length */
		for (wc = 0; (err == 0) && (wc < twc); wc++)  {
			awc = fxread (&rpdb, sizeof (int16), 1, uptr -> fileref);
			if (awc == 0) rpdb = 0;
			if (rpdb != M[pa + wc])  {
				rpcs2 = rpcs2 | CS2_WCE;
				break;  }  }
		err = ferror (uptr -> fileref);  }

	rpwc = (rpwc + wc) & 0177777;			/* final word count */
	pa = (pa + wc) << 1;				/* final byte addr */
	rpba = (pa & 0177777) & ~BA_MBZ;		/* lower 16b */
	rpbae = (pa >> 16) & ~AE_MBZ;			/* upper 6b */
	rpcs1 = (rpcs1 & ~ CS1_UAE) | ((rpbae << CS1_V_UAE) & CS1_UAE);
	da = da + wc + (RP_NUMWD - 1);
	if (da >= drv_tab[dtype].size) rpds[drv] = rpds[drv] | DS_LST;
	da = da / RP_NUMWD;
	rpda = da % drv_tab[dtype].sect;
	da = da / drv_tab[dtype].sect;
	rpda = rpda | ((da % drv_tab[dtype].surf) << DA_V_SF);
	rpdc = da / drv_tab[dtype].surf;

	if (err != 0) {					/* error? */
		rper1[drv] = rper1[drv] | ER1_PAR;	/* set drive error */
		update_rpcs (CS1_DONE | CS1_TRE, drv);	/* set done, err */
		perror ("RP I/O error");
		clearerr (uptr -> fileref);
		return SCPE_IOERR;  }
	update_rpcs (CS1_DONE, drv);			/* set done */
	return SCPE_OK;  }				/* end case function */
return SCPE_OK;
}

/* Controller status update  
   First update drive status, then update RPCS1
   If optional argument, request interrupt
*/

void update_rpcs (int32 flag, int32 drv)
{
int32 i;

if (rp_unit[drv].flags & UNIT_DIS) rpds[drv] = rper1[drv] = 0;
else rpds[drv] = (rpds[drv] | DS_DPR) & ~DS_PGM;
if (rp_unit[drv].flags & UNIT_ATT) rpds[drv] = rpds[drv] | DS_MOL;
else rpds[drv] = rpds[drv] & ~(DS_MOL | DS_VV | DS_RDY);
if (rper1[drv] | rper2 | rper3) rpds[drv] = rpds[drv] | DS_ERR;
else rpds[drv] = rpds[drv] & ~DS_ERR;

rpcs1 = (rpcs1 & ~(CS1_SC | CS1_MCPE | CS1_MBZ)) | CS1_DVA | flag;
if (rpcs2 & CS2_ERR) rpcs1 = rpcs1 | CS1_TRE | CS1_SC;
for (i = 0; i < RP_NUMDR; i++)
	if (rpds[i] & DS_ATA) rpcs1 = rpcs1 | CS1_SC;
if (((rpcs1 & CS1_IE) == 0) || ((rpcs1 & CS1_DONE) == 0))
	int_req = int_req & ~INT_RP;
else if (flag) int_req = int_req | INT_RP;
return;
}

/* Interrupt acknowledge */

int32 rp_inta (void)
{
/* rpcs1 = rpcs1 & ~CS1_IE;				/* clear int enable */
/* rpcs3 = rpcs3 & ~CS1_IE;				/* in both registers */
return VEC_RP;						/* acknowledge */
}

/* Device reset */

t_stat rp_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rpcs1 = CS1_DVA | CS1_DONE;
rpcs2 = CS2_IR | CS2_OR;
rpba = rpda = 0;
rpof = rpdc = 0;
rper2 = rper3 = 0;
rpec1 = rpec2 = 0;
rpbae = rpcs3 = 0;
int_req = int_req & ~INT_RP;
for (i = 0; i < RP_NUMDR; i++) {
	uptr = rp_dev.units + i;
	sim_cancel (uptr);
	uptr -> CYL = uptr -> FUNC = 0;
	if (uptr -> flags & UNIT_ATT) rpds[i] = (rpds[i] & DS_VV) |
		DS_DPR | DS_RDY | DS_MOL | ((uptr -> flags & UNIT_WLK)? DS_WRL: 0);
	else if (uptr -> flags & UNIT_DIS) rpds[i] = 0;
	else rpds[i] = DS_DPR;
	rper1[i] = 0;  }
return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, char *cptr)
{
int drv, i, p;
t_stat r;

uptr -> capac = drv_tab[GET_DTYPE (uptr -> flags)].size;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK) return r;
drv = uptr - rp_dev.units;				/* get drv number */
rpds[drv] = DS_ATA | DS_MOL | DS_RDY | DS_DPR |
	((uptr -> flags & UNIT_WLK)? DS_WRL: 0);
rper1[drv] = 0;
update_rpcs (CS1_SC, drv);

if ((uptr -> flags & UNIT_AUTO) == 0) return SCPE_OK;	/* autosize? */
if (fseek (uptr -> fileref, 0, SEEK_END)) return SCPE_OK;
if ((p = ftell (uptr -> fileref)) == 0) return SCPE_OK;
for (i = 0; drv_tab[i].sect != 0; i++) {
    if (p <= (drv_tab[i].size * (int) sizeof (int16))) {
	uptr -> flags = (uptr -> flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
	uptr -> capac = drv_tab[i].size;
	return SCPE_OK;  }  }
return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
int32 drv;

drv = uptr - rp_dev.units;				/* get drv number */
rpds[drv] = (rpds[drv] & ~(DS_MOL | DS_RDY | DS_WRL | DS_VV | DS_OF)) |
	DS_ATA;
if (sim_is_active (uptr)) {				/* unit active? */
	sim_cancel (uptr);				/* cancel operation */
	rper1[drv] = rper1[drv] | ER1_OPI;		/* set drive error */
	if (uptr -> FUNC >= FNC_WCHK)			/* data transfer? */
		rpcs1 = rpcs1 | CS1_DONE | CS1_TRE;  }	/* set done, err */
update_rpcs (CS1_SC, drv);				/* request intr */
return detach_unit (uptr);
}

/* Set size command validation routine */

t_stat rp_set_size (UNIT *uptr, int32 value)
{
if (uptr -> flags & UNIT_ATT) return SCPE_ALATT;
uptr -> capac = drv_tab[GET_DTYPE (value)].size;
return SCPE_OK;
}

/* Set bad block routine */

t_stat rp_set_bad (UNIT *uptr, int32 value)
{
return pdp11_bad_block (uptr, drv_tab[GET_DTYPE (uptr -> flags)].sect, RP_NUMWD);
}

/* Device bootstrap */

#define BOOT_START 02000		/* start */
#define BOOT_UNIT 02006			/* where to store unit number */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
	0012706, 0002000,		/* mov #2000, sp */
	0012700, 0000000,		/* mov #unit, r0 */
	0012701, 0176700,		/* mov #RPCS1, r1 */
	0012737, 0000040, 0176710,	/* mov #CS2_CLR, RPCS2 */
	0010037, 0176710,		/* mov r0, RPCS2 */
	0012711, 0000021,		/* mov #RIP+GO, (R1) */
	0012737, 0010000, 0176732,	/* mov #FMT16B, RPOF */
	0005037, 0176750,		/* clr RPBAE */
	0005037, 0176704,		/* clr RPBA */
	0005037, 0176734,		/* clr RPDC */
	0005037, 0176706,		/* clr RPDA */
	0012737, 0177000, 0176702,	/* mov #-512., RPWC */
	0012711, 0000071,		/* mov #READ+GO, (R1) */
	0005002,			/* clr R2 */
	0005003,			/* clr R3 */
	0005004,			/* clr R4 */
	0012705, 0042120,		/* mov #"DP, r5 */
	0105711,			/* tstb (R1) */
	0100376,			/* bpl .-2 */
	0105011,			/* clrb (R1) */
	0005007				/* clr PC */
};

t_stat rp_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & CS2_M_UNIT;
saved_PC = BOOT_START;
return SCPE_OK;
}
