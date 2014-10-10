/* 68k_cpu.c: 68k-CPU simulator for sage-II system

   Copyright (c) 2009, Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   04-Oct-09    HV      Initial version
*/

#ifndef M68K_CPU_H_
#define M68K_CPU_H_ 0

#include "sim_defs.h"

/* define this o 1 for adding debugging code */
#define DBG_MSG 1

#if !defined(HAVE_INT64)
#error Fix me, I need 64 bit data types!
#endif

/* these must be set in the system-specific CPU reset */
extern UNIT* m68kcpu_unit;
extern DEVICE* m68kcpu_dev;

/* implemented in m68k_cpu.c */
extern REG m68kcpu_reg[];

/* debug flags */
#define DBG_CPU_EXC			(1 << 0)
#define DBG_CPU_PC			(1 << 1)
#define DBG_CPU_INT			(1 << 2)
#define DBG_CPU_CTRACE		(1 << 3)
#define DBG_CPU_BTRACE		(1 << 4)
#define DBG_CPU_CUSTOM1		(1 << 5)	/* reserved for custom debugging */
#define DBG_CPU_CUSTOM2		(1 << 6)	/* reserved for custom debugging */
extern DEBTAB m68kcpu_dt[];
#if DBG_MSG==1
#define IFDEBUG(flag,func) if ((m68kcpu_dev->dctrl & flag) && sim_deb) { (void)(func); fflush(sim_deb); }
#else
#define IFDEBUG(flag,func)
#endif

#define SIM_EMAX 16	/* ? */
#define MAXMEMORY			(256*256*256)			/* 2^24 bytes */
#define MINMEMORY			(256*256)				/* reserve 64k by default */
#define MEMORYSIZE			(m68kcpu_unit->capac)	/* actual memory size */
#define KB					1024					/* kilobyte */

/* simulator stop codes */
#define STOP_IBKPT	1		/* pc breakpoint */
#define STOP_MEM	2		/* memory breakpoint */
#define STOP_ERROP	3		/* invalid opcode, normally exception 4 */
#define STOP_ERRIO	4		/* invalid I/O address, normally exception 2 */
#define STOP_ERRADR	5		/* invalid memory address, normally exception 3  */
#define STOP_IMPL   6		/* not yet implemented (should disappear) */
#define SIM_ISIO 	7		/* internal indicator that I/O dispatch is required */
#define SIM_NOMEM	8		/* allows to signal that there is no memory at that location */
#define STOP_PCIO 	9		/* code error, PC steps on I/O address */
#define STOP_PRVIO 	10		/* internal indicator: privileged instruction */
#define STOP_TRACE 	11		/* halt on trace */
#define STOP_HALT 	12		/* STOP instruction */
#define STOP_DBF 	13		/* double bus fault */
#define STOP_OFFLINE 14		/* printer offline */

#define UNIT_CPU_M_TYPE		017
#define UNIT_CPU_V_TYPE		(UNIT_V_UF+0)		/* CPUTYPE */
#define UNIT_CPU_TYPE		(1 << UNIT_CPU_V_CPU)
#define UNIT_CPU_V_EXC		(UNIT_V_UF+4)		/* halt on exception 2..4 */
#define UNIT_CPU_EXC		(1 << UNIT_CPU_V_EXC)
#define UNIT_CPU_V_STOP		(UNIT_V_UF+5)		/* halt on STOP instruction */
#define UNIT_CPU_STOP		(1 << UNIT_CPU_V_STOP)
#define UNIT_CPU_V_PRVIO	(UNIT_V_UF+6)		/* halt on privilege violation */
#define UNIT_CPU_PRVIO		(1 << UNIT_CPU_V_PRVIO)
#define UNIT_CPU_V_TRACE	(UNIT_V_UF+7)		/* halt on TRACE exception */
#define UNIT_CPU_TRACE		(1 << UNIT_CPU_V_TRACE)
#define UNIT_CPU_V_FPU		(UNIT_V_UF+8)		/* has FPU */
#define UNIT_CPU_FPU		(1 << UNIT_CPU_V_FPU)
#define UNIT_CPU_V_MMU		(UNIT_V_UF+9)		/* has MMU */
#define UNIT_CPU_MMU		(1 << UNIT_CPU_V_MMU)
#define UNIT_CPU_V_MSIZE	(UNIT_V_UF+10)		/* set memsize */
#define UNIT_CPU_MSIZE		(1 << UNIT_CPU_V_MSIZE)

#define UNIT_CPU_V_FREE		(UNIT_V_UF+11)		/* next free bit */

/* the various CPUs */
#define UNIT_CPUTYPE_MASK	(UNIT_CPU_M_TYPE << UNIT_CPU_V_TYPE)
#define CPU_TYPE_68000		(0 << UNIT_CPU_V_TYPE)
#define CPU_TYPE_68008		(1 << UNIT_CPU_V_TYPE)
#define CPU_TYPE_68010		(2 << UNIT_CPU_V_TYPE)	/* not yet! */
#define CPU_TYPE_68020		(3 << UNIT_CPU_V_TYPE)	/* not yet! */
#define CPU_TYPE_68030		(4 << UNIT_CPU_V_TYPE)	/* not yet! */

extern uint8	*M;
extern int16	cputype;
extern t_addr   saved_PC;
#define PCX		saved_PC

/* breakpoint space for data accesses (R=read, W=write) */
#define E_BKPT_SPC (0)
#define R_BKPT_SPC (1<<SIM_BKPT_V_SPC)
#define W_BKPT_SPC (2<<SIM_BKPT_V_SPC)

/* IR 7-6 bits */
#define SZ_BYTE	0
#define SZ_WORD 1
#define SZ_LONG 2 
#define SZ_SPEC 3

/* functions to access memory
 * xxxxPx access physical memory
 * xxxxVx access virtual memory using MMU; if no MMU xxxxVX == xxxxPX
 * xxxxxB = byte, xxxxxW = 16 bit word, xxxxxL = 32 bit word
 */
#define BMASK	0x000000ff
#define BLMASK	BMASK
#define BHMASK	0x0000ff00

#define WMASK	0x0000ffff
#define WLMASK	WMASK
#define WHMASK	0xffff0000

#define LMASK	0xffffffff
extern t_addr addrmask;

#define MEM_READ 0
#define MEM_WRITE 1

/* I/O handler block */
#define IO_READ	0
#define IO_WRITE 1
struct _iohandler {
	void* ctxt;
	t_addr port;
	t_addr offset;
	UNIT* u;
	t_stat (*io)(struct _iohandler* ioh,uint32* value,uint32 rw,uint32 mask);
	struct _iohandler* next;
};
typedef struct _iohandler IOHANDLER;

typedef struct {
    uint32 mem_base;    /* Memory Base Address */
    uint32 mem_size;    /* Memory Address space requirement */
    uint32 io_base;     /* I/O Base Address */
    uint32 io_size;     /* I/O Address Space requirement */
    uint32 io_incr;		/* I/O Address increment */
} PNP_INFO;

extern t_stat add_iohandler(UNIT* u,void* ctxt,
	t_stat (*io)(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask));
extern t_stat del_iohandler(void* ctxt);
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);

/* public memory access routines */
extern t_stat ReadPB(t_addr a, uint32* val);
extern t_stat ReadPW(t_addr a, uint32* val);
extern t_stat ReadPL(t_addr a, uint32* val);
extern t_stat WritePB(t_addr a, uint32 val);
extern t_stat WritePW(t_addr a, uint32 val);
extern t_stat WritePL(t_addr a, uint32 val);

extern t_stat ReadVB(t_addr a, uint32* val);
extern t_stat ReadVW(t_addr a, uint32* val);
extern t_stat ReadVL(t_addr a, uint32* val);
extern t_stat WriteVB(t_addr a, uint32 val);
extern t_stat WriteVW(t_addr a, uint32 val);
extern t_stat WriteVL(t_addr a, uint32 val);
extern t_stat (*TranslateAddr)(t_addr in,t_addr* out,IOHANDLER** ioh,int rw,int fc,int dma);
extern t_stat m68k_translateaddr(t_addr in,t_addr* out,IOHANDLER** ioh,int rw,int fc,int dma);
extern t_stat (*Mem)(t_addr a,uint8** mem);
extern t_stat m68k_mem(t_addr a,uint8** mem);

/* cpu_mod for alternative implementations */
extern t_stat m68k_set_cpu(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68k_show_cpu(FILE* st,UNIT *uptr, int32 value, void *desc);
extern t_stat m68k_set_size(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68k_set_fpu(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68k_set_nofpu(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68kcpu_set_flag(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68kcpu_set_noflag(UNIT *uptr, int32 value, char *cptr, void *desc);
extern t_stat m68kcpu_reset(DEVICE* dptr);
extern t_stat m68kcpu_ex(t_value* eval_array, t_addr addr, UNIT *uptr, int32 switches);
extern t_stat m68kcpu_dep(t_value value, t_addr addr, UNIT* uptr, int32 switches);
extern t_stat m68kcpu_boot(int32 unitno,DEVICE* dptr);
extern t_stat m68k_ioinit();
extern t_stat m68kcpu_peripheral_reset();
extern t_stat m68k_alloc_mem();
extern t_stat m68k_raise_vectorint(int level,int vector);
extern t_stat m68k_raise_autoint(int level);

#define XFMT "0x%08x"
#define SFMT "$%x"
extern char*  m68k_getsym(t_addr val,const char* fmt, char* outbuf);

/* overloadable callbacks */
extern void (*m68kcpu_trapcallback)(DEVICE* cpudev,int trapnum);

/* standard MTAB declarations for most 68K CPUs */
#define M68KCPU_STDMOD \
{ UNIT_CPUTYPE_MASK,	CPU_TYPE_68000,		"",			"68000",	&m68k_set_cpu, 		&m68k_show_cpu, "68000"	},\
{ UNIT_CPUTYPE_MASK,	CPU_TYPE_68008,		"",			"68008",	&m68k_set_cpu, 		&m68k_show_cpu,	"68008" },\
{ UNIT_CPUTYPE_MASK,	CPU_TYPE_68010,		"",			"68010",	&m68k_set_cpu, 		&m68k_show_cpu,	"68010" },\
{ UNIT_CPU_MSIZE,       (1u << 16),         NULL,   	"64K",      &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 17),         NULL,   	"128K",     &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 18),         NULL,   	"256K",     &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 19),         NULL,   	"512K",     &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 20),         NULL,   	"1M",       &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 21),         NULL,   	"2M",       &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 22),         NULL,   	"4M",       &m68k_set_size      },\
{ UNIT_CPU_MSIZE,       (1u << 23),         NULL,   	"8M",       &m68k_set_size      },\
{ UNIT_CPU_EXC,			UNIT_CPU_EXC,		"halt on EXC",	"EXC",	&m68kcpu_set_flag 	},\
{ UNIT_CPU_EXC,			0,					"no EXC",	NULL,		NULL				},\
{ MTAB_XTD|MTAB_VDV,	UNIT_CPU_EXC,		NULL,		"NOEXC",	&m68kcpu_set_noflag	},\
{ UNIT_CPU_STOP,		UNIT_CPU_STOP,		"halt on STOP",	"STOP",	&m68kcpu_set_flag 	},\
{ UNIT_CPU_STOP,		0,					"no STOP",	NULL,		NULL				},\
{ MTAB_XTD|MTAB_VDV,	UNIT_CPU_STOP,		NULL,		"NOSTOP",	&m68kcpu_set_noflag	},\
{ UNIT_CPU_PRVIO,		UNIT_CPU_PRVIO,		"halt on PRVIO",	"PRVIO",	&m68kcpu_set_flag 	},\
{ UNIT_CPU_PRVIO,		0,					"no PRVIO",	NULL,		NULL				},\
{ MTAB_XTD|MTAB_VDV,	UNIT_CPU_PRVIO,		NULL,		"NOPRVIO",	&m68kcpu_set_noflag	},\
{ UNIT_CPU_TRACE,		UNIT_CPU_TRACE,		"halt on TRACE",	"TRACE",	&m68kcpu_set_flag 	},\
{ UNIT_CPU_TRACE,		0,					"no TRACE",	NULL,		NULL				},\
{ MTAB_XTD|MTAB_VDV,	UNIT_CPU_TRACE,		NULL,		"NOTRACE",	&m68kcpu_set_noflag	}

#if 0
,{ UNIT_CPU_FPU,			UNIT_CPU_FPU,		"FPU",		"FPU",		&m68k_set_fpu		},
{ UNIT_CPU_FPU,			0,					"no FPU",	NULL,		NULL				},
{ MTAB_XTD|MTAB_VDV,	UNIT_CPU_FPU,		NULL,		"NOFPU",	&m68k_set_nofpu		},
#endif

#endif
