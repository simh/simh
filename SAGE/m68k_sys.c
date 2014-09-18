/* m68k_sys.c: assembler/disassembler/misc simfuncs for generic m68k_cpu
  
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
   15-Mar-10	HV		fix 2nd arg bug in disassembling btst
   24-Apr-10	HV		fix _fsymea for jsr.l
   27-Apr-10	HV		fix stop instr
   27-Jun-10	HV		improve error handling in Motorola S0 reader
   20-Jul-10	HV		fix disassemble error for LINK
*/

#include "m68k_cpu.h"
#include <ctype.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc)
{
	DEVICE* dptr;
	PNP_INFO* pnp;
	t_stat rc;
	uint16 newbase;

	if (!cptr) return SCPE_ARG;
	if (!uptr) return SCPE_IERR;
	if (!(dptr = find_dev_from_unit(uptr))) return SCPE_IERR;
	if (!(pnp = (PNP_INFO*)dptr->ctxt)) return SCPE_IERR;

    newbase = get_uint (cptr, 16, 0xFF, &rc);
    if (rc != SCPE_OK) return rc;

    if (dptr->flags & DEV_DIS) {
        printf("Device not enabled yet.\n");
        pnp->io_base = newbase;
    } else {
        dptr->flags |= DEV_DIS;
        dptr->reset(dptr);
        pnp->io_base = newbase;
        dptr->flags &= ~DEV_DIS;
        dptr->reset(dptr);
    }
    return SCPE_OK;
}

t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc)
{
	DEVICE *dptr;
	PNP_INFO *pnp;

	if (!uptr) return SCPE_IERR;
	if (!(dptr = find_dev_from_unit(uptr))) return SCPE_IERR;
	if (!(pnp = (PNP_INFO *) dptr->ctxt)) return SCPE_IERR;

    fprintf(st, "I/O=0x%02X-0x%02X", pnp->io_base, pnp->io_base + pnp->io_size - pnp->io_incr);
    return SCPE_OK;	
}

t_stat m68k_set_cpu(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	if (value < 0 || value > CPU_TYPE_68030)
		return SCPE_ARG;
	
	cputype = (value & UNIT_CPUTYPE_MASK) >> UNIT_CPU_V_TYPE;
	uptr->flags &= ~UNIT_CPUTYPE_MASK;
	uptr->flags |= value;
	return SCPE_OK;
}

t_stat m68k_show_cpu(FILE* st,UNIT *uptr, int32 value, void *desc)
{
	fprintf(st,"TYPE=%s",(char*)desc);
	return SCPE_OK;
}

t_stat m68k_alloc_mem() 
{
	if (M == NULL)
		M = (uint8*)calloc(MEMORYSIZE, 1);
	else
		M = (uint8*)realloc(M, MEMORYSIZE);
	return M == NULL ? SCPE_MEM : SCPE_OK;
}

t_stat m68k_set_size(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	t_stat rc;
	uptr->capac = value;
	if ((rc=m68k_alloc_mem()) != SCPE_OK) return rc;
	return SCPE_OK;	
}

t_stat m68k_set_fpu(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags |= value;
	return SCPE_OK;
}
t_stat m68k_set_nofpu(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags |= value;
	return SCPE_OK;
}

t_stat m68kcpu_set_flag(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags |= value;
	return SCPE_OK;
}

t_stat m68kcpu_set_noflag(UNIT *uptr, int32 value, char *cptr, void *desc)
{
	uptr->flags &= ~value;
	return SCPE_OK;
}


t_stat m68kcpu_ex(t_value* eval_array, t_addr addr, UNIT* uptr, int32 sw)
{
	uint32 val = 0;
	t_stat rc = (sw & SWMASK('V')) ? ReadVW(addr,&val) : ReadPW(addr,&val);
	if (rc==SCPE_OK) *eval_array = val;
	return rc;
}

t_stat m68kcpu_dep(t_value value, t_addr addr, UNIT* uptr, int32 sw)
{
	return (sw & SWMASK('V')) ? WriteVW(addr,value) : WritePW(addr,value);
}

static int getHex(FILE* fptr,int* chksum)
{
	char buf[3];
	int c;
	if ((c = fgetc(fptr))==EOF) return EOF;
	buf[0] = c;
	if ((c = fgetc(fptr))==EOF) return EOF;
	buf[1] = c;
	buf[2] = 0;
	return strtol(buf,0,16);
}

/* Motorola S-Record reader
 * Format:
 * 	type		2 Bytes		(S0, S1, S2, S3, S5, S7, S8, S9)
 *  reclength	2 Bytes
 *  address		4,6,8 Bytes
 *  data        0...2n
 *  checksum    2 Bytes     (lsb of 1'comp of fields reclength-data
 */
static t_stat m68k_sread(FILE* fptr)
{
	int typ;
	t_addr addr=0, a;
	int d, len, chksum, i;
	int end = FALSE;
	int line = 0;
	
	fseek(fptr,0l,SEEK_SET);
	for(;;) {
		while ((i = fgetc(fptr)) == '\r' || i == '\n');
		line++;
		if (end && i == EOF) return SCPE_OK;
		if (i != 'S') { printf("Line %d: expected S but did not find one (found %x)\n",line,i); return SCPE_FMT; }

		typ = fgetc(fptr);
		chksum = 0;
		len = getHex(fptr,&chksum);
		addr = getHex(fptr,&chksum);
		a = getHex(fptr,&chksum);
		if (len==EOF || addr==EOF || a==EOF) { typ = 'X'; goto error; }
		addr = (addr << 8) | a;
		i = 3;
		
		switch (typ) {
		case '0':
			for (i=2; i<len; i++) (void)getHex(fptr,&chksum);
			break;
		case '1':
			i = 2;
			goto dread;
		case '3':
			if ((a = getHex(fptr,&chksum))==EOF) goto error;
			addr = (addr << 8) | a;
			i = 4;
			/*fallthru*/
		case '2':
			if ((a = getHex(fptr,&chksum))==EOF) goto error;
			addr = (addr << 8) | a;
dread:
			for (; i<len; i++) { d = getHex(fptr,&chksum); WritePB(addr,d); addr++; }
			break;
		case '7':
			if ((a = getHex(fptr,&chksum))==EOF) goto error;
			addr = (addr << 8) | a;
			/*fallthru*/
		case '8':
			if ((a = getHex(fptr,&chksum))==EOF) goto error;
			addr = (addr << 8) | a;
			/*fallthru*/
		case '9':
			end = TRUE;
			/*fallthru*/
		case '5':
			if((d = getHex(fptr,&chksum))==EOF) goto error;
			break;
		}
		if ((chksum & 0xff) != 0) return SCPE_CSUM;
		saved_PC = addr;
	}

error:
	printf("S%c at line %d: Unexpected EOF/Invalid character\n",typ,line);
	return SCPE_FMT;
}

t_stat sim_load(FILE* fptr, char* cptr, char* fnam, t_bool flag)
{
	int i,len,rc;
	uint16 data;
	uint8 s;
	int32 addr = saved_PC;
	
	/* no dump */
	if (*cptr != 0 || flag != 0) return SCPE_ARG;

	/* check whether Motorola S-Record format was presented */
	fseek(fptr,0L,SEEK_SET);
	if (fread(&s,sizeof(uint8),1,fptr) == 1) {
		if (s == 'S') {
			/* asssume S record format */
			if (m68k_sread(fptr) == SCPE_OK) return SCPE_OK;
		}
	}

	/* assume plain octet word stream */
	fseek(fptr,0L,SEEK_END);
	len = ftell(fptr);
	if (len & 1) return SCPE_FMT;

	fseek(fptr,0L,SEEK_SET);
	for (i=0; i<len; i+=2) {
		if (fread(&data,sizeof(uint16),1,fptr) != 1) return SCPE_FMT;
		if ((rc=WritePW(addr,data)) != SCPE_OK) return rc;
		addr += 2;
	}
	return SCPE_OK;
}

const char *sim_stop_messages[] = {
	"---",
    "PC Breakpoint",
    "MEM Breakpoint",
    "Invalid Opcode",
    "Invalid I/O address",
    "Invalid Mem access",
    "Not yet implemented!",
    "(internal: IO dispatch)",
    "(internal: nonexisting memory)",
    "PC at I/O address",
    "Privilege Violation",
    "Trace trap",
    "STOP instruction",
    "Double Bus Fault",
    "Printer Offline",
};

#define ONERR_QUIT() if (rc==SCPE_ARG) { printf("??\n\t"); return SCPE_ARG; }
#define REG0_FIELD(inst) (inst&7)
#define REG0_CHAR(inst) (inst&7) + '0'
#define REG9_CHAR(inst) ((inst>>9)&7) + '0'
#define OPLEN_FIELD(inst) (inst>>6)&3
#define EA_FIELD(inst) inst&077
#define EAMOD_FIELD(inst) inst & 070
#define BWL_CHAR(oplen) (oplen==0) ? 'b' : ((oplen==1) ? 'w' : 'l')
#define DATA_B(x) (x&0xff)
#define DATA_W(x) (x&0xffff)

static t_stat _fsymea(FILE* of,t_addr addr,int ea, int oplen,t_value* rest) 
{
	int eamod = EAMOD_FIELD(ea);
	char eareg = REG0_CHAR(ea);
	t_value offb = DATA_B(rest[0]);
	t_value offw =  DATA_W(rest[0]);
	t_value offw2 = DATA_W(rest[1]);
	char da = (rest[0] & 0x8000)? 'a' : 'd';
	char xreg = ((rest[0]>>12) & 7) + '0';
	char wl = (rest[0] & 0x800) ? 'l' : 'w';
	
	switch (eamod) {
	case 000: fprintf(of,"d%c",eareg); return 0; 
	case 010: fprintf(of,"a%c",eareg); return 0; 
	case 020: fprintf(of,"(a%c)",eareg); return 0; 
	case 030: fprintf(of,"(a%c)+",eareg); return 0; 
	case 040: fprintf(of,"-(a%c)",eareg); return 0; 
	case 050: fprintf(of,"($%x,a%c)",offw,eareg); return -2; 
	case 060: 
		if (offb)
			fprintf(of,"($%x,a%c,%c%c.%c)",offb,eareg,da,xreg,wl);
		else
			fprintf(of,"(a%c,%c%c.%c)",eareg,da,xreg,wl);
		return -2;
	case 070: 
		switch (eareg) {
		case '0': fprintf(of,"($%x).w",(uint32)((uint16)offw)); return -2;
		case '1': 
			if (offw)
				fprintf(of,"($%x%04x).l",offw,offw2);
			else
				fprintf(of,"($%x).l",offw2);
			return -4;
		case '2': //fprintf(of,"($%x,pc)",offw);
				  if (offw & 0x8000) offw |= 0xffff0000;
				  fprintf(of,"$%x",addr+offw+2);
				  return -2;
		case '3':
			if (offb)
				fprintf(of,"($%x,pc,%c%c.%c)",offb,da,xreg,wl);
			else
				fprintf(of,"(pc,%c%c.%c)",da,xreg,wl);				
			return -2;
		case '4':
			switch(oplen) {
			case 0: fprintf(of,"#$%x",offb); return -2;
			case 1: fprintf(of,"#$%x",offw); return -2;
			case 2: 
				if (offw)
					fprintf(of,"#$%x%04x",offw,offw2);
				else
					fprintf(of,"#$%x",offw2);
				return -4;
			case 3: fprintf(of,"ccr"); return 0;
			case 4: fprintf(of,"sr"); return 0;
			default: return SCPE_ARG;
			}
		default: return SCPE_ARG;
		}
	default: return SCPE_ARG;
	}
}

static t_stat _fsymead(FILE* of,int dir,char reg9,t_addr addr,int ea,int oplen,t_value* rest)
{
	int rc;
	if (dir) { 
		fprintf(of,"d%c,",reg9); rc = _fsymea(of,addr,ea,oplen,rest); 
	} else {
		rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); fprintf(of,",d%c",reg9); 
	}		
	return rc-1;	
}

static t_stat _fsymimm(FILE* of,int oplen,t_value* rest)
{
	t_value offb = rest[0] & 0xff;
	t_value offw  = rest[0] & 0xffff;
	t_value offw2 = rest[1] & 0xffff;
	switch(oplen) {
	case 0: fprintf(of,"#$%x",offb); return 1;
	case 1: fprintf(of,"#$%x",offw); return 1;
	case 2: fprintf(of,"#$%x%04x",offw,offw2); return 2;
	default: return SCPE_ARG;
	}
}

static t_stat _fsym0(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	int rc;
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	int bitnum= DATA_B(rest[0]);
	int ea    = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	char* s = 0;

	switch (inst & 000700) {
	case 000400:
		if (eamod==010) {
			fprintf(of,"movep.w $%x(a%c),d%c",rest[0],reg0,reg9); return -3;
		} else s = "btst";
		break;
	case 000500:
		if (eamod==010) {
			fprintf(of,"movep.l $%x(a%c),d%c",rest[0],reg0,reg9); return -3;
		} else s = "bchg";
		break;
	case 000600:
		if (eamod==010) {
			fprintf(of,"movep.w d%c,$%x(a%c)",reg9,rest[0],reg0); return -3;
		} else s = "bclr";
		break;
	case 000700:
		if (eamod==010) {
			fprintf(of,"movep.l d%c,$%x(a%c)",reg9,rest[0],reg0); return -3;
		} else s = "bset";
		break;
	}
	if (s) {
		fprintf(of,"%s d%c,",s,reg9); rc = _fsymea(of,addr,ea,3,rest); ONERR_QUIT(); return rc-1;		
	}
	
	switch (inst & 007000) {
	case 000000:
		s = "ori"; break;
	case 001000:
		s = "andi"; break;
	case 002000:
		s = "subi"; break;
	case 003000:
		s = "addi"; break;
	case 004000:
		switch (inst & 000700) {
		case 000000:
			s = "btst"; break;
		case 000100:
			s = "bchg"; break;
		case 000200:
			s = "bclr"; break;
		case 000300:
			s = "bset"; break;
		default:
			return SCPE_ARG;
		}
		fprintf(of,"%s #%x,",s,bitnum); rc = _fsymea(of,addr,ea,0,rest+1); ONERR_QUIT(); return rc-3;		
	case 005000:
		s = "eori"; break;
	case 006000:
		s = "cmpi"; break;
	default:
		return SCPE_ARG;
	}
	
	fprintf(of,"%s.%c ",s,bwl); rc = _fsymimm(of,oplen,rest); ONERR_QUIT();
	fputc(',',of); rc = _fsymea(of,addr,ea,oplen+3,rest+rc);
	return rc - 3 - ((oplen==2) ? 2 : 0);
}

static t_stat _fsym123(FILE* of,t_value inst,t_addr addr,t_value* rest,char w,int oplen)
{
	int rc, rc2;
	int eas = inst & 077;
	int eat = ((inst>>9)&7)|((inst&0700)>>3);
	char *s = ((eat&070)==010) ? "movea" : "move";
	fprintf(of,"%s.%c ",s,w);
	rc = _fsymea(of,addr,eas,oplen,rest); ONERR_QUIT(); rc2 = rc;
	fputc(',',of);
	rc = _fsymea(of,addr,eat,oplen,rest-rc2/2); ONERR_QUIT();
	return rc2 + rc -1;
}

static char* moveregs[] = {
	"d0","d1","d2","d3","d4","d5","d6","d7","a0","a1","a2","a3","a4","a5","a6","a7"
};
static char* moveregsp[] = {
	"a7","a6","a5","a4","a3","a2","a1","a0","d7","d6","d5","d4","d3","d2","d1","d0"
};

#define BITEMIT() if (sl) fputc('/',of); sl = 1; \
	if (hi==lo) fprintf(of,"%s",regs[lo]); \
	else if (ispredec) fprintf(of,"%s-%s",regs[hi],regs[lo]); \
	else fprintf(of,"%s-%s",regs[lo],regs[hi]); \
	lo = hi = -1;
#define BITSEQ() bit = regset & 1; regset >>= 1; \
	if (bit && lo == -1) lo = i; \
	if (bit == 0 && lo != -1) {	hi = i-1; BITEMIT(); }

static void _fsymregs(FILE* of, int regset,int ispredec)
{
	int lo, hi, bit, sl, i;
	char** regs = ispredec ? moveregsp : moveregs;

//printf("regset=%x\n",regset);
	sl = 0;
	bit = lo = hi = -1;
	for (i=0; i<8; i++)  { BITSEQ(); }
	if (lo != -1) { hi = 7; BITEMIT(); }
	bit = -1;
	for (i=8; i<16; i++) { BITSEQ(); }
	if (lo != -1) { hi = 15; BITEMIT(); }
}

static t_stat _fsym4(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	t_stat rc;
	char reg9 = REG9_CHAR(inst);
	int ea = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	char reg0 = REG0_CHAR(inst);
	int oplen = OPLEN_FIELD(inst);
	char* s;
	
	switch (inst & 000700) {
	case 000600:
		fprintf(of,"chk "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT();
		fprintf(of,",d%c",reg9); return rc-1;
	case 000700:
		fprintf(of,"lea "); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT(); 
		fprintf(of,",a%c",reg9); return rc-1;
	case 000000:
		switch (inst & 007000) {
		case 000000:
			s = "negx.b "; break;
		case 001000:
			s = "clr.b "; break;
		case 002000:
			s = "neg.b "; break;
		case 003000:
			s = "not.b "; break;
		case 004000:
			s = "nbcd "; break;
		case 005000:
			s = "tst.b "; break;
		default:
			return SCPE_ARG;
		}
		fputs(s,of); rc =  _fsymea(of,addr,ea,0,rest); ONERR_QUIT(); return rc-1;		
	case 000100:
		switch (inst & 007000) {
		case 007000:
			switch (inst & 000070) {
			case 000000:
			case 000010:
				fprintf(of,"trap #$%x",inst & 0xf); return -1;
			case 000020:
				fprintf(of,"link a%c,#$%x",reg0,rest[0]); return -3;
			case 000030:
				fprintf(of,"unlk a%c",reg0); return -1;
			case 000040:
				fprintf(of,"move a%c,usp",reg0); return -1;
			case 000050:
				fprintf(of,"move usp,a%c",reg0); return -1;
			case 000060:
				switch (inst & 000007) {
				case 000000:
					s = "reset"; break;
				case 000001:
					s = "nop"; break;
				case 000002:
					fprintf(of,"stop #%x",DATA_W(rest[0])); return -3;
				case 000003:
					s = "rte"; break;
				case 000005:
					s = "rts"; break;
				case 000006:
					s = "trapv"; break;
				case 000007:
					s = "rtr"; break;
				default:
					return SCPE_ARG;
				}
				fputs(s,of); return -1;
			default:
				return SCPE_ARG;
			}
		case 000000:
			s = "negx.w "; break;
		case 001000:
			s = "clr.w "; break;
		case 002000:
			s = "neg.w "; break;
		case 003000:
			s = "not.w "; break;
		case 005000:
			s = "tst.w "; break;
		case 004000:
			if (eamod==0) {
				fprintf(of,"swap d%c",reg0); return -1;
			} else {
				fputs("pea ",of); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT(); return rc-1;
			}
		default:
			return SCPE_ARG;
		}
		fputs(s,of); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); return rc-1;
		
	case 000200:
		switch (inst & 007000) {
		case 000000:
			s = "negx.l "; break;
		case 001000:
			s = "clr.l "; break;
		case 002000:
			s = "neg.l "; break;
		case 003000:
			s = "not.l "; break;
		case 004000:
			if (eamod==0) {
				fprintf(of,"ext.w d%c",reg0); return -1;
			} else {
				fprintf(of,"movem.w ");	_fsymregs(of,rest[0],eamod==040);
				fputc(',',of); rc = _fsymea(of,addr,ea,oplen==2?1:2,rest+1); return rc-3;
			}
		case 005000:
			s = "tst.l "; break;
		case 006000:
			fprintf(of,"movem.w "); rc = _fsymea(of,addr,ea,oplen==2?1:2,rest+1);
			fputc(',',of); _fsymregs(of,rest[0],0); return rc-3;					
		case 007000:
			s = "jsr "; break;
		default:
			return SCPE_ARG;
		}
		fputs(s,of); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT(); return rc-1;

	case 000300:
		switch (inst & 007000) {
		case 000000:
			fprintf(of,"move sr,"); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); return rc-1;
		case 003000:
			fprintf(of,"move "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); fputs(",sr",of); return rc-1;
		case 002000:
			fprintf(of,"move "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); fputs(",ccr",of); return rc-1;
		case 004000:
			if (eamod==0) {
				fprintf(of,"ext.l d%c",reg0); return -1;
			} else {
				fprintf(of,"movem.l ");	_fsymregs(of,rest[0],eamod==040);
				fputc(',',of); rc = _fsymea(of,addr,ea,oplen==2?1:2,rest+1);	return rc-3;
			}
		case 005000:
			switch (inst & 000077) {
			case 000074:
				fputs("illegal",of); return -1;
			default:
				fprintf(of,"tas "); rc = _fsymea(of,addr,ea,0,rest); return rc-1;
			}
		case 006000:
			fprintf(of,"movem.l "); rc = _fsymea(of,addr,ea,oplen==2?1:2,rest+1);
			fputc(',',of); _fsymregs(of,rest[0],0); return rc-3;
		case 007000:
			fputs("jmp ",of); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT(); return rc-1;
		default:
			return SCPE_ARG;
		}
	default:
		return SCPE_ARG;
	}
}

static char* conds[] = { "ra","sr","hi","ls","cc","cs","ne","eq","vc","vs","pl","mi","ge","lt","gt","le" };
static char* conds2[] = { "t", "f","hi","ls","cc","cs","ne","eq","vc","vs","pl","mi","ge","lt","gt","le" };

static t_stat _fsym5(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	t_stat rc;
	int ea = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	int reg0 = REG0_CHAR(inst);
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);
	t_addr a;

	if (oplen==3) {
		char* cond = conds2[(inst>>8)&0xf];
		if (eamod==010) {
			a = rest[0] & 0xffff;
			if (a & 0x8000) a |= 0xffff0000;
			//printf("addr=%x a=%x sum=%x\n",addr,a,addr+a+2);
			fprintf(of,"db%s d%c,$%x",cond,reg0,addr+a+2);
			return -3;
		} else {
			fprintf(of,"s%s ",cond); rc = _fsymea(of,addr,ea,0,rest); return rc-3;
		}
	} else {
		int data = (inst>>9) & 07;
		char *s = (inst & 0x0100) ? "subq" : "addq";

        if (data==0) data = 8;
		fprintf(of,"%s.%c #%d,",s,bwl,data); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); return rc-1;
	}
	
}
static t_stat _fsym6(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	char* cond = conds[(inst>>8)&0xf];
	t_addr a = inst & 0xff;
	if (a) {
		if (a & 0x80) a |= 0xffffff00;
		fprintf(of,"b%s.s $%x",cond,addr+a+2); return -1;
	} else {
		a = rest[0] & 0xffff;
		if (a & 0x8000) a |= 0xffff0000;
		fprintf(of,"b%s.w $%x",cond,addr+a+2); return -3;
	}
}

static t_stat _fsym7(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	int reg9 = REG9_CHAR(inst);
	switch (inst & 000400) {
	case 000000:
		fprintf(of,"moveq #$%x,d%c",(int32)((int8)(inst&0xff)),reg9); return -1;
	default:
		return SCPE_ARG;
	}
}

static t_stat _fsym8(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	t_stat rc;
	int oplen = OPLEN_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	int ea    = EA_FIELD(inst);
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	char bwl  = BWL_CHAR(oplen);

	switch (inst & 000700) {
	case 000000:
	case 000100:
	case 000200:
		fprintf(of,"or.%c ",bwl); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); 
		fprintf(of,",d%c",reg9); return rc-1;
	case 000300:
		fprintf(of,"divu.w "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); 
		fprintf(of,",d%c",reg9); return rc-1;
	case 000400:
		switch (eamod) {
		case 000:
			fprintf(of,"sbcd d%c,d%c",reg0,reg9); return -1;
		case 010:
			fprintf(of,"sbcd -(a%c),-(a%c)",reg0,reg9); return -1;
		default:
			fprintf(of,"or.%c d%c,",bwl,reg9); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT();
			return rc-1;
		}
	case 000500:
	case 000600:
		fprintf(of,"or.%c d%c,",bwl,reg9); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT();
		return rc-1;
	case 000700:
		fprintf(of,"divs.w "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); 
		fprintf(of,",d%c",reg9); return rc-1;
	}
	return SCPE_ARG; /* Not reached, but silence agressive compiler warnings */
}

static t_stat _fsym9(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	t_stat rc;
	int oplen = OPLEN_FIELD(inst);
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	char bwl  = BWL_CHAR(oplen);
	int ea    = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	
	switch (inst & 000700) {
	case 000000:
	case 000100:
	case 000200:
		fprintf(of,"sub.%c ",bwl); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); 
		fprintf(of,",d%c",reg9);return rc-1;
	case 000300:
		fprintf(of,"suba.w "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); 
		fprintf(of,",a%c",reg9);return rc-1;		
	case 000400:
		switch (eamod) {
		case 000:
			fprintf(of,"subx.%c d%c,d%c",bwl,reg9,reg0); return -1;
		case 001:
			fprintf(of,"subx.%c d%c,d%c",bwl,reg9,reg0); return -1;
		default:
			fprintf(of,"sub.%c d%c,",bwl,reg9); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); return rc-1;			
		}
	case 000500:
	case 000600:
		fprintf(of,"sub.%c d%c,",bwl,reg9); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT(); return rc-1;
	case 000700:
		fprintf(of,"suba.l "); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT(); 
		fprintf(of,",a%c",reg9);return rc-1;
	default:
		return SCPE_ARG;
	}
}

static t_stat _fsyma(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	fprintf(of,"trapa #$%x",inst&0xfff); return -1;	
}

static t_stat _fsymb(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	int rc;
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	int ea    = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);

	switch (inst & 000700) {
	case 000000:
	case 000100:
	case 000200:
		fprintf(of,"cmp.%c ",bwl); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT();
		fprintf(of,",d%c",reg9); return rc-1;		
	case 000300:
		fprintf(of,"cmpa.w "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT();
		fprintf(of,",a%c",reg9); return rc-1;
	case 000400:
	case 000500:
	case 000600:
		if (eamod==010) {
			fprintf(of,"cmpm.%c (a%c)+,(a%c)+",bwl,reg0,reg9); return -1;			
		} else {
			fprintf(of,"eor.%c d%c,",bwl,reg9); rc = _fsymea(of,addr,ea,oplen,rest); ONERR_QUIT();
			return rc-1;
		}
	case 000700:
		fprintf(of,"cmpa.l "); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT();
		fprintf(of,",a%c",reg9); return rc-1;
	default:
		return SCPE_ARG;
	}
}

static t_stat _fsymc(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	char reg9 = REG9_CHAR(inst);
	int ea    = EA_FIELD(inst);
	int reg0  = REG0_CHAR(inst); 
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);

	switch (inst & 0000770) {
	case 0000500:
		fprintf(of,"exg d%c,d%c",reg9,reg0); return -1;
	case 0000510:
		fprintf(of,"exg a%c,a%c",reg9,reg0); return -1;
	case 0000610:
		fprintf(of,"exg d%c,a%c",reg9,reg0); return -1;
	case 0000400:
		fprintf(of,"abcd d%c,d%c",reg9,reg0); return -1;
	case 0000410:
		fprintf(of,"abcd -(a%c),-(a%c)",reg9,reg0); return -1;
	default:
		break;
	}

	switch (inst & 0000700) {
	case 0000400:
		fprintf(of,"and.%c ",bwl); return _fsymead(of,1,reg9,addr,ea,oplen,rest);
	case 0000000:
	case 0000100:
	case 0000200:
		fprintf(of,"and.%c ",bwl); return _fsymead(of,0,reg9,addr,ea,oplen,rest);
	case 0000300:
		fprintf(of,"mulu.w ");  return _fsymead(of,0,reg9,addr,ea,1,rest);
	case 0000700:
		fprintf(of,"muls.w ");  return _fsymead(of,0,reg9,addr,ea,1,rest);
	default:
		return SCPE_ARG;
	}
}

static t_stat _fsymd(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	int rc;
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	int ea    = EA_FIELD(inst);
	int eamod = EAMOD_FIELD(inst);
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);
	
	switch (inst & 000700) {
	case 000000:
	case 000100:
	case 000200:
		fprintf(of,"add.%c ",bwl); return _fsymead(of,0,reg9,addr,ea,oplen,rest);
	case 000300:
		fprintf(of,"adda.w "); rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT();
		fprintf(of,",a%c",reg9); return rc-1;		
	case 000400:
		switch (eamod) {
		case 000:
			fprintf(of,"addx.%c d%c,d%c",bwl,reg9,reg0); return -1;
		case 001:
			fprintf(of,"addx.%c d%c,d%c",bwl,reg9,reg0); return -1;
		default:
			fprintf(of,"add.%c ",bwl); return _fsymead(of,1,reg9,addr,ea,oplen,rest);
		}
	case 000500:
	case 000600:
		fprintf(of,"add.%c ",bwl); return _fsymead(of,1,reg9,addr,ea,oplen,rest);
	case 000700:
		fprintf(of,"adda.l "); rc = _fsymea(of,addr,ea,2,rest); ONERR_QUIT();
		fprintf(of,",a%c",reg9); return rc-1;		
	}
	return SCPE_ARG; /* Not reached, but silence agressive compiler warnings */
}

static t_stat _fsyme(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	int rc;
	int oplen = OPLEN_FIELD(inst);
	char bwl  = BWL_CHAR(oplen);
	int op = (oplen==3 ? (inst>>9) : (inst>>3)) & 3;
	char dir = (inst&0x100) ? 'l' : 'r';
	int ir = inst & 0x20;
	int ea = EA_FIELD(inst);
	char reg9 = REG9_CHAR(inst);
	char reg0 = REG0_CHAR(inst);
	char *s;

	switch (op) {
	case 0: s = "as"; break;
	case 1: s = "ls"; break;
	case 2: s = "rox"; break;
	case 3: s = "ro"; break;
	default: s = "??"; break;
	}
	fprintf(of,"%s%c",s,dir); 
	if (oplen<3) {
		fprintf(of,".%c ",bwl);
		if (ir)
			fprintf(of,"d%c,d%c",reg9,reg0);
		else {
			if (reg9=='0') reg9 = '8';
			fprintf(of,"#%d,d%c",reg9-'0',reg0);
		}
		return -1;
	} else {
		fputc(' ',of);
		rc = _fsymea(of,addr,ea,1,rest); ONERR_QUIT(); return rc-1;
	}
}

static t_stat _fsymf(FILE* of,t_value inst,t_addr addr,t_value* rest)
{
	fprintf(of,"trapf #$%x",inst&0xfff); return -1;
}

t_stat fprint_sym(FILE* of, t_addr addr, t_value* val, UNIT* uptr, int32 sw)
{
	int32 c1, c2, inst;

	c1 = (val[0] >> 8) & 0177;
	c2 = val[0] & 0177;
	if (sw & SWMASK ('A')) {
	    fprintf (of, (c2 < 040)? "<%02x>": "%c", c2);
	    return SCPE_OK;
	}
	if (sw & SWMASK ('C')) {
	    fprintf (of, (c1 < 040)? "<%02x>": "%c", c1);
	    fprintf (of, (c2 < 040)? "<%02x>": "%c", c2);
	    return -1;
	}
	if (!(sw & SWMASK ('M'))) return SCPE_ARG;

	inst = val[0];
	switch ((inst>>12) & 0xf) {
	case 0x0:	return _fsym0(of,inst,addr,val+1);
	case 0x1:	return _fsym123(of,inst,addr,val+1,'b',0);
	case 0x2:	return _fsym123(of,inst,addr,val+1,'l',2);
	case 0x3:	return _fsym123(of,inst,addr,val+1,'w',1);
	case 0x4:	return _fsym4(of,inst,addr,val+1);
	case 0x5:	return _fsym5(of,inst,addr,val+1);
	case 0x6:	return _fsym6(of,inst,addr,val+1);
	case 0x7:	return _fsym7(of,inst,addr,val+1);
	case 0x8:	return _fsym8(of,inst,addr,val+1);
	case 0x9:	return _fsym9(of,inst,addr,val+1);
	case 0xa:	return _fsyma(of,inst,addr,val+1);
	case 0xb:	return _fsymb(of,inst,addr,val+1);
	case 0xc:	return _fsymc(of,inst,addr,val+1);
	case 0xd:	return _fsymd(of,inst,addr,val+1);
	case 0xe:	return _fsyme(of,inst,addr,val+1);
	case 0xf:	return _fsymf(of,inst,addr,val+1);
	}
	return SCPE_OK;
}
