/* m68k_mem.c: memory handling for m68k_cpu
  
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
   20-Dec-09    HV      Rewrite memory handler for MMU and noncontiguous memory
*/

#include "m68k_cpu.h"
#include <ctype.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/* io hash */
#define IOHASHSIZE 97 /* must be prime */
#define MAKEIOHASH(p) (p % IOHASHSIZE)
static IOHANDLER** iohash = NULL;

/*
 * memory
 */
uint8*	M = 0;
t_addr  addrmask = 0xffffffff;
int     m68k_fcode = 0;
int     m68k_dma = 0;

#if 0
/* TODO */
t_stat m68k_set_mmu(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags |= value;
	
	/* TODO initialize the MMU */
	TranslateAddr = &m68k_translateaddr;
	return SCPE_OK;
}

t_stat m68k_set_nommu(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags &= ~value;

	/* initialize NO MMU */
	TranslateAddr = &m68k_translateaddr;
	return SCPE_OK;
}
#endif

/* I/O dispatcher
 * 
 * I/O devices are implemented this way:
 * a unit will register its own I/O addresses together with its handler 
 * in a hash which allows simple translation of physical addresses 
 * into units in the ReadPx/WritePx routines.
 * These routines call the iohandler entry on memory mapped read/write.
 * The handler has the option to enqueue an event for its unit for
 * asynchronous callback, e.g. interrupt processing
 */
t_stat m68k_ioinit() 
{
	if (iohash == NULL) {
		iohash = (IOHANDLER**)calloc(IOHASHSIZE,sizeof(IOHANDLER*));
		if (iohash == NULL) return SCPE_MEM;
	}
	return SCPE_OK;
}

t_stat add_iohandler(UNIT* u,void* ctxt,
	t_stat (*io)(struct _iohandler* ioh,uint32* value,uint32 rw,uint32 mask))
{
	PNP_INFO* pnp = (PNP_INFO*)ctxt;
	IOHANDLER* ioh;
	uint32 i,k;
	
	if (!pnp) return SCPE_IERR;
	for (k=i=0; i<pnp->io_size; i+=pnp->io_incr,k++) {
		t_addr p = (pnp->io_base+i) & addrmask;
		t_addr idx = MAKEIOHASH(p);
		ioh = iohash[idx];
		while (ioh != NULL && ioh->port != p) ioh = ioh->next;
		if (ioh) continue; /* already registered */
		
//		printf("Register IO for address %x offset=%d\n",p,k);
		ioh = (IOHANDLER*)malloc(sizeof(IOHANDLER));
		if (ioh == NULL) return SCPE_MEM;
		ioh->ctxt = ctxt;
		ioh->port = p;
		ioh->offset = k;
		ioh->u = u;
		ioh->io = io;
		ioh->next = iohash[idx];
		iohash[idx] = ioh;
	}
	return SCPE_OK;
}
t_stat del_iohandler(void* ctxt)
{
	uint32 i;
	PNP_INFO* pnp = (PNP_INFO*)ctxt;
	
	if (!pnp) return SCPE_IERR;
	for (i=0; i<pnp->io_size; i += pnp->io_incr) {
		t_addr p = (pnp->io_base+i) & addrmask;
		t_addr idx = MAKEIOHASH(p);
		IOHANDLER **ioh = &iohash[idx];		
		while (*ioh != NULL && (*ioh)->port != p) ioh = &((*ioh)->next);
		if (*ioh) {
			IOHANDLER *e = *ioh;
			*ioh = (*ioh)->next;
			free((void*)e);
		}
	}
	return SCPE_OK;
}

/***********************************************************************************************
 * Memory handling
 * ReadP{B|W|L} and WriteP{B|W|L} simply access physical memory (addrmask applies)
 * ReadV{B|W|L} and WriteV{B|W|L} access virtual memory, i.e. after a "decoder" or mmu has processed
 *                                the address/rwmode/fcode
 * TranslateAddr is a user-supplied function, to be set into the function pointer,
 * which converts an address and other data (e.g. rw, fcode) provided by the CPU
 * into the real physical address. This is basically the MMU.
 * 
 * TranslateAddr returns SCPE_OK for valid translation
 *                       SIM_ISIO if I/O dispatch is required; ioh contains pointer to iohandler
 *                       STOP_ERRADDR if address is invalid
 * Mem accesses memory, selected by a (translated) address. Override in own code for non-contiguous memory
 * Mem returns SCPE_OK and a pointer to the selected byte, if okay, STOP_ERRADR for invalid accesses
 */

/* default handler */
t_stat m68k_translateaddr(t_addr in,t_addr* out, IOHANDLER** ioh,int rw,int fc,int dma)
{
	t_addr ma = in & addrmask;
	t_addr idx = MAKEIOHASH(ma);
	IOHANDLER* i = iohash[idx];

	*out = ma;
	*ioh = 0;
	while (i != NULL && i->port != ma) i = i->next;
	if (i) {
		*ioh = i;
		return SIM_ISIO;
	} else
		return SCPE_OK;
}

/* default memory pointer */
t_stat m68k_mem(t_addr addr,uint8** mem)
{
	if (addr > MEMORYSIZE) return STOP_ERRADR;
	*mem = M+addr;
	return SCPE_OK;
}

t_stat (*TranslateAddr)(t_addr in,t_addr* out,IOHANDLER** ioh,int rw,int fc,int dma) = &m68k_translateaddr;
t_stat (*Mem)(t_addr addr,uint8** mem) = &m68k_mem;

/* memory access routines 
 * The Motorola CPU is big endian, whereas others like the i386 is
 * little endian. The memory uses the natural order of the emulating CPU.
 *
 * addressing uses all bits but LSB to access the memory cell
 * 
 * Memorybits 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
 *            ------68K Byte0-(MSB)-- ---68K Byte1-----------
 *            ------68K Byte2-------- ---68K Byte3-(LSB)-----
 */
t_stat ReadPB(t_addr a, uint32* val)
{
	uint8* mem;

	t_stat rc = Mem(a & addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SIM_NOMEM:
		*val = 0xff;
		return SCPE_OK;
	case SCPE_OK:
		*val = *mem & BMASK;
		return SCPE_OK;
	}
}

t_stat ReadPW(t_addr a, uint32* val)
{
	uint8* mem;
	uint32 dat1,dat2;

	t_stat rc = Mem((a+1)&addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SIM_NOMEM:
		*val = 0xffff;
		return SCPE_OK;
	case SCPE_OK:
		/* 68000/08/10 do not like unaligned access */
		if (cputype < 3 && (a & 1)) return STOP_ERRADR;
		dat1 = (*(mem-1) & BMASK) << 8;
		dat2 = *mem & BMASK;
		*val = (dat1 | dat2) & WMASK;
		return SCPE_OK;
	}
}

t_stat ReadPL(t_addr a, uint32* val)
{
	uint8* mem;
	uint32 dat1,dat2,dat3,dat4;

	t_stat rc = Mem((a+3)&addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SIM_NOMEM:
		*val = 0xffffffff;
		return SCPE_OK;
	case SCPE_OK:
		/* 68000/08/10 do not like unaligned access */
		if (cputype < 3 && (a & 1)) return STOP_ERRADR;
		dat1 = *(mem-3) & BMASK;
		dat2 = *(mem-2) & BMASK;
		dat3 = *(mem-1) & BMASK;
		dat4 = *mem & BMASK;
		*val = (((((dat1 << 8) | dat2) << 8) | dat3) << 8) | dat4;
		return SCPE_OK;
	}
}

t_stat WritePB(t_addr a, uint32 val)
{
	uint8* mem;
	
	t_stat rc = Mem(a&addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SCPE_OK:
		*mem = val & BMASK;
		/*fallthru*/
	case SIM_NOMEM:
		return SCPE_OK;
	}
}

t_stat WritePW(t_addr a, uint32 val)
{
	uint8* mem;
	t_stat rc = Mem((a+1)&addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SCPE_OK:
		/* 68000/08/10 do not like unaligned access */
		if (cputype < 3 && (a & 1)) return STOP_ERRADR;
		*(mem-1) = (val >> 8) & BMASK;
		*mem     =  val       & BMASK;
		/*fallthru*/
	case SIM_NOMEM:
		return SCPE_OK;
	}
}

t_stat WritePL(t_addr a, uint32 val)
{
	uint8* mem;

	t_stat rc = Mem((a+3)&addrmask,&mem);
	switch (rc) {
	default:
		return rc;
	case SCPE_OK:
		/* 68000/08/10 do not like unaligned access */
		if (cputype < 3 && (a & 1)) return STOP_ERRADR;
		*(mem-3) = (val >> 24) & BMASK;	
		*(mem-2) = (val >> 16) & BMASK;
		*(mem-1) = (val >>  8) & BMASK;
		*mem     =  val        & BMASK;
		/*fallthru*/
	case SIM_NOMEM:
		return SCPE_OK;
	}
}

t_stat ReadVB(t_addr a, uint32* val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_READ,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		/* note this is a hack to persuade memory testing code that there is no memory:
		 * writing to such an address is a bit bucket,
		 * and reading from it will return some arbitrary value.
		 * 
		 * SIM_NOMEM has to be defined for systems without a strict memory handling that will
		 * result in reading out anything without trapping a memory fault
		 */
		*val = 0xff;
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,val,IO_READ,BMASK);
	case SCPE_OK:
		return ReadPB(addr,val);
	default:
		return rc;
	}
}

t_stat ReadVW(t_addr a, uint32* val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_READ,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		*val = 0xffff;
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,val,IO_READ,WMASK);
	case SCPE_OK:
		return ReadPW(addr,val);
	default:
		return rc;
	}
}

t_stat ReadVL(t_addr a, uint32* val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_READ,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		*val = 0xffffffff;
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,val,IO_READ,LMASK);
	case SCPE_OK:
		return ReadPL(addr,val);
	default:
		return rc;
	}
}

t_stat WriteVB(t_addr a, uint32 val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_WRITE,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		/* part 2 of hack for less strict memory handling: ignore anything written
		 * to a nonexisting address
		 */
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,&val,IO_WRITE,BMASK);
	case SCPE_OK:
		return WritePB(addr,val);
	default:
		return rc;
	}
}

t_stat WriteVW(t_addr a, uint32 val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_WRITE,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,&val,IO_WRITE,WMASK);
	case SCPE_OK:
		return WritePW(addr,val);
	default:
		return rc;
	}
}

t_stat WriteVL(t_addr a, uint32 val)
{
	t_addr addr;
	IOHANDLER* ioh;
	t_stat rc = TranslateAddr(a,&addr,&ioh,MEM_WRITE,m68k_fcode,m68k_dma);
	switch (rc) {
	case SIM_NOMEM:
		return SCPE_OK;
	case SIM_ISIO:
		return ioh->io(ioh,&val,IO_WRITE,LMASK);
	case SCPE_OK:
		return WritePL(addr,val);
	default:
		return rc;
	}	
}
