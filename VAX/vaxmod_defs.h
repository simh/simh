/* vaxmod_defs.h: VAX model-specific definitions file

   Copyright (c) 1998-2002, Robert M Supnik

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

   14-Jul-02	RMS	Added additional console halt codes
   28-Apr-02	RMS	Fixed DZV vector base and number of lines

   This file covers the KA65x ("Mayfair") series of CVAX-based Qbus systems.

   System memory map

	0000 0000 - 03FF FFFF		main memory
	0400 0000 - 0FFF FFFF		reserved
	1000 0000 - 13FF FFFF		secondary cache diagnostic space
	1400 0000 - 1FFF FFFF		reserved

	2000 0000 - 2000 1FFF		Qbus I/O page
	2000 2000 - 2003 FFFF		reserved
	2004 0000 - 2005 FFFF		ROM space, halt protected
	2006 0000 - 2007 FFFF		ROM space, halt unprotected
	2008 0000 - 201F FFFF		Local register space
	2020 0000 - 2FFF FFFF		reserved
	3000 0000 - 303F FFFF		Qbus memory space
	3400 0000 - 3FFF FFFF		reserved
*/

/* Microcode constructs */

#define CVAX_SID	(10 << 24)			/* system ID */
#define CVAX_UREV	6				/* ucode revision */
#define CON_HLTPIN	0x0200				/* external CPU halt */
#define CON_PWRUP	0x0300				/* powerup code */
#define CON_HLTINS	0x0600				/* HALT instruction */
#define CON_BADPSL	0x4000				/* invalid PSL flag */
#define CON_MAPON	0x8000				/* mapping on flag */
#define MCHK_TBM_P0	0x05				/* PPTE in P0 */
#define MCHK_TBM_P1	0x06				/* PPTE in P1 */
#define MCHK_M0_P0	0x07				/* PPTE in P0 */
#define MCHK_M0_P1	0x08				/* PPTE in P1 */
#define MCHK_INTIPL	0x09				/* invalid ireq */
#define MCHK_READ	0x80				/* read check */
#define MCHK_WRITE	0x82				/* write check */

/* Memory system error register */

#define MSER_HM		0x80				/* hit/miss */
#define MSER_CPE	0x40				/* CDAL par err */
#define MSER_CPM	0x20				/* CDAL mchk */

/* Cache disable register */

#define CADR_RW		0xF3
#define CADR_MBO	0x0C

/* Memory */

#define MAXMEMWIDTH	26				/* max mem addr width */
#define MAXMEMSIZE	(1 << MAXMEMWIDTH)		/* max mem size */
#define MAXMEMMASK	(MAXMEMSIZE - 1)		/* max mem addr mask */
#define INITMEMSIZE	(1 << 24)			/* initial memory size */
#define MEMSIZE		(cpu_unit.capac)
#define ADDR_IS_MEM(x)	(((t_addr) (x)) < MEMSIZE)

/* Cache diagnostic space */

#define CDAAWIDTH	16				/* cache dat addr width */
#define CDASIZE		(1u << CDAAWIDTH)		/* cache dat length */
#define CDAMASK		(CDASIZE - 1)			/* cache dat mask */
#define CTGAWIDTH	10				/* cache tag addr width */
#define CTGSIZE		(1u << CTGAWIDTH)		/* cache tag length */
#define CTGMASK		(CTGSIZE - 1)			/* cache tag mask */
#define CDGSIZE		(CDASIZE * CTGSIZE)		/* diag addr length */
#define CDGBASE		0x10000000			/* diag addr base */
#define CDG_GETROW(x)	(((x) & CDAMASK) >> 2)
#define CDG_GETTAG(x)	(((x) >> CDAAWIDTH) & CTGMASK)
#define CTG_V		(1u << (CTGAWIDTH + 0))		/* tag valid */
#define CTG_WP		(1u << (CTGAWIDTH + 1))		/* wrong parity */
#define ADDR_IS_CDG(x)	((((t_addr) (x)) >= CDGBASE) && \
			 (((t_addr) (x)) < (CDGBASE + CDGSIZE)))

/* Qbus I/O registers */

#define IOPAGEAWIDTH	13				/* IO addr width */
#define IOPAGESIZE	(1u << IOPAGEAWIDTH)		/* IO page length */
#define IOPAGEMASK	(IOPAGESIZE - 1)		/* IO addr mask */
#define IOPAGEBASE	0x20000000			/* IO page base */
#define ADDR_IS_IO(x)	((((t_addr) (x)) >= IOPAGEBASE) && \
			 (((t_addr) (x)) < (IOPAGEBASE + IOPAGESIZE)))

/* Read only memory - appears twice */

#define ROMAWIDTH	17				/* ROM addr width */
#define ROMSIZE		(1u << ROMAWIDTH)		/* ROM length */
#define ROMAMASK	(ROMSIZE - 1)			/* ROM addr mask */
#define ROMBASE		0x20040000			/* ROM base */
#define ADDR_IS_ROM(x)	((((t_addr) (x)) >= ROMBASE) && \
			 (((t_addr) (x)) < (ROMBASE + ROMSIZE + ROMSIZE)))

/* Local register space */

#define REGAWIDTH	19				/* REG addr width */
#define REGSIZE		(1u << REGAWIDTH)		/* REG length */
#define REGBASE		0x20080000			/* REG addr base */

/* KA655 board registers */

#define KAAWIDTH	3				/* KA reg width */
#define KASIZE		(1u << KAAWIDTH)		/* KA reg length */
#define KABASE		(REGBASE + 0x4000)		/* KA650 addr base */

/* CQBIC registers */

#define CQBICSIZE	(5 << 2)			/* 5 registers */
#define CQBICBASE	(REGBASE)			/* CQBIC addr base */
#define CQMAPASIZE	15				/* map addr width */
#define CQMAPSIZE	(1u << CQMAPASIZE)		/* map length */
#define CQMAPAMASK	(CQMAPSIZE - 1)			/* map addr mask */
#define CQMAPBASE	(REGBASE + 0x8000)		/* map addr base */
#define CQIPCSIZE	2				/* 2 bytes only */
#define CQIPCBASE	(REGBASE + 0x1F40)		/* ipc reg addr */

/* CMCTL registers */

#define CMCTLSIZE	(18 << 2)			/* 18 registers */
#define CMCTLBASE	(REGBASE + 0x100)		/* CMCTL addr base */

/* SSC registers */

#define SSCSIZE		0x150				/* SSC size */
#define SSCBASE		0x20140000			/* SSC base */

/* Non-volatile RAM - 1KB long */

#define NVRAWIDTH	10				/* NVR addr width */
#define NVRSIZE		(1u << NVRAWIDTH)		/* NVR length */
#define NVRAMASK	(NVRSIZE - 1)			/* NVR addr mask */
#define NVRBASE		0x20140400			/* NVR base */
#define ADDR_IS_NVR(x)	((((t_addr) (x)) >= NVRBASE) && \
			 (((t_addr) (x)) < (NVRBASE + NVRSIZE)))

/* CQBIC Qbus memory space (seen from CVAX) */

#define CQMAWIDTH	22				/* Qmem addr width */
#define CQMSIZE		(1u << CQMAWIDTH)		/* Qmem length */
#define CQMAMASK	(CQMSIZE - 1)			/* Qmem addr mask */
#define CQMBASE		0x30000000			/* Qmem base */

/* Qbus I/O modes */

#define READ		0				/* PDP-11 compatibility */
#define WRITE		(L_WORD)
#define WRITEB		(L_BYTE)

/* Common CSI flags */

#define CSR_V_GO	0				/* go */
#define CSR_V_IE	6				/* interrupt enable */
#define CSR_V_DONE	7				/* done */
#define CSR_V_BUSY	11				/* busy */
#define CSR_V_ERR	15				/* error */
#define CSR_GO		(1u << CSR_V_GO)
#define CSR_IE		(1u << CSR_V_IE)
#define CSR_DONE	(1u << CSR_V_DONE)
#define CSR_BUSY	(1u << CSR_V_BUSY)
#define CSR_ERR		(1u << CSR_V_ERR)

/* IO parameters */

#define DZ_MUXES	4				/* max # of muxes */
#define DZ_LINES	4				/* (DZV) lines per mux */
#define MT_MAXFR	(1 << 16)			/* magtape max rec */

/* Timers */

#define TMR_CLK		0				/* 100Hz clock */

/* I/O system definitions */

/* Device information block */

struct pdp_dib {
	uint32		enb;				/* enabled */
	uint32		ba;				/* base addr */
	uint32		lnt;				/* length */
	t_stat		(*rd)(int32 *dat, int32 ad, int32 md);
	t_stat		(*wr)(int32 dat, int32 ad, int32 md);  };

typedef struct pdp_dib DIB;

/* I/O page layout */

#define IOBA_DZ		(IOPAGEBASE + 000100)		/* DZ11 */
#define IOLN_DZ		010
#define IOBA_RQ		(IOPAGEBASE + 012150)		/* RQDX3 */
#define IOLN_RQ		004
#define IOBA_TS		(IOPAGEBASE + 012520)		/* TS11 */
#define IOLN_TS		004
#define IOBA_RL		(IOPAGEBASE + 014400)		/* RL11 */
#define IOLN_RL		012
#define IOBA_RP		(IOPAGEBASE + 016700)		/* RP/RM */
#define IOLN_RP		054
#define IOBA_DBL	(IOPAGEBASE + 017500)		/* doorbell */
#define IOLN_DBL	002
#define IOBA_LPT	(IOPAGEBASE + 017514)		/* LP11 */
#define IOLN_LPT	004
#define IOBA_PT		(IOPAGEBASE + 017550)		/* PC11 */
#define IOLN_PT		010

/* The KA65x maintains 4 separate hardware IPL levels, IPL 17 to IPL 14
   Within each IPL, priority is right to left
*/

/* IPL 17 */

/* IPL 16 */

#define INT_V_CLK	0				/* clock */

/* IPL 15 */

#define INT_V_RQ	0				/* RQDX3 */
#define INT_V_RL	1				/* RLV12/RL02 */
#define INT_V_DZRX	2				/* DZ11 */
#define INT_V_DZTX	3
#define INT_V_RP	4				/* RP,RM drives */
#define INT_V_TS	5				/* TS11/TSV05 */

/* IPL 14 */

#define INT_V_TTI	0				/* console */
#define INT_V_TTO	1
#define INT_V_PTR	2				/* PC11 */
#define INT_V_PTP	3
#define INT_V_LPT	4				/* LP11 */
#define INT_V_CSI	5				/* SSC cons UART */
#define INT_V_CSO	6
#define INT_V_TMR0	7				/* SSC timers */
#define INT_V_TMR1	8

#define INT_CLK		(1u << INT_V_CLK)
#define INT_RQ		(1u << INT_V_RQ)
#define INT_RL		(1u << INT_V_RL)
#define INT_DZRX	(1u << INT_V_DZRX)
#define INT_DZTX	(1u << INT_V_DZTX)
#define INT_RP		(1u << INT_V_RP)
#define INT_TS		(1u << INT_V_TS)
#define INT_TTI		(1u << INT_V_TTI)
#define INT_TTO		(1u << INT_V_TTO)
#define INT_PTR		(1u << INT_V_PTR)
#define INT_PTP		(1u << INT_V_PTP)
#define INT_LPT		(1u << INT_V_LPT)
#define INT_CSI		(1u << INT_V_CSI)
#define INT_CSO		(1u << INT_V_CSO)
#define INT_TMR0	(1u << INT_V_TMR0)
#define INT_TMR1	(1u << INT_V_TMR1)

#define IPL_CLK		(0x16 - IPL_HMIN)			/* relative IPL */
#define IPL_RQ		(0x15 - IPL_HMIN)
#define IPL_RL		(0x15 - IPL_HMIN)
#define IPL_DZRX	(0x15 - IPL_HMIN)
#define IPL_DZTX	(0x15 - IPL_HMIN)
#define IPL_RP		(0x15 - IPL_HMIN)
#define IPL_TS		(0x15 - IPL_HMIN)
#define IPL_TTI		(0x14 - IPL_HMIN)
#define IPL_TTO		(0x14 - IPL_HMIN)
#define IPL_PTR		(0x14 - IPL_HMIN)
#define IPL_PTP		(0x14 - IPL_HMIN)
#define IPL_LPT		(0x14 - IPL_HMIN)
#define IPL_CSI		(0x14 - IPL_HMIN)
#define IPL_CSO		(0x14 - IPL_HMIN)
#define IPL_TMR0	(0x14 - IPL_HMIN)
#define IPL_TMR1	(0x14 - IPL_HMIN)

#define IPL_HMAX	0x17				/* highest hwre level */
#define IPL_HMIN	0x14				/* lowest hwre level */
#define IPL_HLVL	(IPL_HMAX - IPL_HMIN + 1)	/* # hardware levels */
#define IPL_SMAX	0xF				/* highest swre level */

#define VEC_Q		0x200				/* Qbus vector offset */
#define VEC_PTR		(VEC_Q + 0070)			/* Qbus vectors */
#define VEC_PTP		(VEC_Q + 0074)
#define VEC_RQ		(VEC_Q + 0154)
#define VEC_RL		(VEC_Q + 0160)
#define VEC_LPT		(VEC_Q + 0200)
#define VEC_TS		(VEC_Q + 0224)
#define VEC_RP		(VEC_Q + 0254)
#define VEC_DZRX	(VEC_Q + 0300)
#define VEC_DZTX	(VEC_Q + 0304)

#define IREQ(dv)	int_req[IPL_##dv]
#define SET_INT(dv)	int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)	int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* cond error return */

/* Logging */

#define LOG_CPU_I	0x001				/* intexc */
#define LOG_CPU_R	0x002				/* REI */
#define LOG_CPU_P	0x004				/* context */
#define LOG_CPU_A	0x008
#define LOG_RP		0x010
#define LOG_TS		0x020
#define LOG_RQ		0x040

#define DBG_LOG(x)	(sim_log && (cpu_log & (x)))

/* Function prototypes for I/O */

t_bool map_addr (t_addr qa, t_addr *ma);
int32 map_readB (t_addr ba, int32 bc, uint8 *buf);
int32 map_readW (t_addr ba, int32 bc, uint16 *buf);
int32 map_readL (t_addr ba, int32 bc, uint32 *buf);
int32 map_writeB (t_addr ba, int32 bc, uint8 *buf);
int32 map_writeW (t_addr ba, int32 bc, uint16 *buf);
int32 map_writeL (t_addr ba, int32 bc, uint32 *buf);

/* Macros for PDP-11 compatibility */

#define QB		0				/* Q22 native */
#define UB		1				/* Unibus */

#define Map_Addr(a,b)		map_addr (a, b)
#define Map_ReadB(a,b,c,d)	map_readB (a, b, c)
#define Map_ReadW(a,b,c,d)	map_readW (a, b, c)
#define Map_WriteB(a,b,c,d)	map_writeB (a, b, c)
#define Map_WriteW(a,b,c,d)	map_writeW (a, b, c)
t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat set_enbdis (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool dev_conflict (uint32 nba, DIB *curr);
