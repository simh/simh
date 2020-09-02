/* pdq3_sys.c: PDQ3 simulator interface

   Work derived from Copyright (c) 2004-2012, Robert M. Supnik
   Copyright (c) 2013 Holger Veit

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

   Except as contained in this notice, the names of Robert M Supnik and Holger Veit 
   shall not be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik and Holger Veit.

   2013xxxx hv initial version (written up to the leval to test against bootloader)
   20130907 hv added VIEWSEG command
   20130925 hv added CALL and NAME command
   20130927 hv wrong disassembly of LDC instr
   20141003 hv compiler suggested warnings (vc++2013, gcc)
*/
#include "pdq3_defs.h"
#include <ctype.h>

t_stat parse_sym_m (char *cptr, t_value *val, int32 sw);
static t_stat pdq3_cmd_exstack(int32 arg, CONST char *buf);
static t_stat pdq3_cmd_exmscw(int32 arg, CONST char *buf);
static t_stat pdq3_cmd_extib(int32 arg, CONST char *buf);
static t_stat pdq3_cmd_exseg(int32 arg, CONST char *buf);
static t_stat pdq3_cmd_calltree(int32 arg, CONST char *buf);
static t_stat pdq3_cmd_namealias(int32 arg, CONST char *buf);

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE tty_dev;
extern DEVICE fdc_dev;
extern DEVICE tim_dev;
extern REG cpu_reg[];
extern uint16 M[];
extern uint16 reg_pc;

/* SCP data structures and interface routines
   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "PDQ3";
REG *sim_PC = &cpu_reg[0]; /* note this is the artifical register PCX */
int32 sim_emax = 6;
DEVICE *sim_devices[] = {
    &cpu_dev,
    &con_dev,
    &fdc_dev,
    &tim_dev,
    NULL
    };
const char *sim_stop_messages[SCPE_BASE] = {
  "---",
  "PC Breakpoint",
  "MEM Breakpoint",
  "Invalid Opcode",
  "Invalid MEM Access",
  "Invalid I/O Access",
  "Not yet implemented",
  "BPT instruction",
  "DEBUG PRE exec stop",
  "DEBUG POST exec stop",
  "HALT on Pascal Exception",
};

CTAB pdq3_cmds[] = {
  { "VSTACK", &pdq3_cmd_exstack, 0, "Display last N elements of stack. Top is where SP points to" },
  { "VMSCW", &pdq3_cmd_exmscw, 0, "Display current MSCW" },
  { "VTIB", &pdq3_cmd_extib, 0, "Display current TIB" },
  { "VSEG", &pdq3_cmd_exseg, 0, "Display a segment table entry" },  
  { "VCALL", &pdq3_cmd_calltree, 0, "Display the call tree" },
  { "NAME", &pdq3_cmd_namealias, 0, "Define a name" },
  { NULL, NULL, 0, NULL }
};

/* Loader proper */
t_stat sim_load (FILE *fi, CONST char *cptr, CONST char *fnam, int flag)
{
  int rombase;
  int c1, c2, i;
  if (flag == 1) /* don't dump */
    return SCPE_ARG;
  /* this assumes a HDT style ROM, where the first 2 bytes refer to the 
   * actual word start of the ROM, e.g. with PDQ-3 the HDT ROM has 0xf401
   * as the first word, so it will load at word address 0xf400, and 0xfc68
   * will be preset to 0xf401
   */
  if ((c1 = fgetc(fi))==EOF) return SCPE_EOF;
  if ((c2 = fgetc(fi))==EOF) return SCPE_EOF;
  rombase = c1 + c2 * 256;
  if (rombase > (MAXMEMSIZE-512)) return SCPE_ARG; 
  rom_write(rombase & 0xfffe, rombase);
  reg_fc68 = rombase;
  i = 0;
  while (!feof(fi) && i<0x1ff) {
    if ((c1 = fgetc(fi))==EOF) return SCPE_EOF;
    if ((c2 = fgetc(fi))==EOF) return SCPE_EOF;
    rom_write(rombase+i, (uint16)(c1 + c2*256));
    i++;
  }
  reg_romsize = i;
  /* preset the cpu_serial number from ROM, may be overwritten manually for special purposes */
  rom_read(rombase+i-1, &reg_cpuserial);
  return SCPE_OK;
}

/* Note: this simh handles ABSOLUTE word addresses and segmented byte addresses.
 * A word address addresses a single cell in memory (up to 65536 cells).
 * A byte address only occurs in IPC context, it is relative to the content of
 * the reg_segb register.
 * Convention:
 * $xxxx = word address
 * xxxx:yyyy = byte address yyyy relative to segment xxxx
 * #yyyy = byte address relative to current reg_segb
 * The t_addr type must be 32 bit, the upper half contains the segment, the lower
 * half contains the offset. If the upper half is NIL, it is a word address
 */
void pdq3_sprint_addr (char *buf, DEVICE *dptr, t_addr addr)
{
  *buf = '\0';
  if (ADDR_ISWORD(addr))
    sprintf(buf,"$");
  else if (ADDR_SEG(addr) == reg_segb)
    sprintf(&buf[strlen(buf)],"#");
  else {
    sprint_val (&buf[strlen(buf)], ADDR_SEG(addr), dptr->dradix, dptr->dwidth, PV_LEFT);
    sprintf(&buf[strlen(buf)],":");
  }
  sprint_val (&buf[strlen(buf)], ADDR_OFF(addr), dptr->dradix, dptr->dwidth, PV_LEFT);
  return;
}

void pdq3_fprint_addr (FILE *st, DEVICE *dptr, t_addr addr)
{
  char buf[65];

  pdq3_sprint_addr (buf, dptr, addr);
  fprintf(st,"%s", buf);
}

t_addr pdq3_parse_addr (DEVICE *dptr, CONST char *cptr, CONST char **tptr)
{
  t_addr seg, off;
  if (cptr[0] == '#') {
    off = strtotv(cptr+1, tptr, dptr->aradix);
    return MAKE_BADDR(reg_segb,off);
  } else if (cptr[0] == '$') {
    off = strtotv(cptr+1, tptr, dptr->aradix);
    return MAKE_WADDR(off);
  } else {
    char gbuf[CBUFSIZE];
    get_glyph (cptr, gbuf, 0);
    if (!strncmp(gbuf,"SEGB",4)) {
      seg = reg_segb; *tptr = cptr+4;
    } else 
      seg = strtotv(cptr, tptr, dptr->aradix);
    if (*tptr[0] == ':') {
      cptr = *tptr + 1;
      off = strtotv(cptr, tptr, dptr->aradix);
      return MAKE_BADDR(seg,off);
    } else 
      return MAKE_WADDR(seg);
  }
}

void pdq3_vm_init (void)
{
  sim_vm_sprint_addr = &pdq3_sprint_addr;
  sim_vm_fprint_addr = &pdq3_fprint_addr;
  sim_vm_parse_addr = &pdq3_parse_addr;
  sim_vm_cmd = pdq3_cmds;
return;
}

static t_stat pdq3_cmd_exstack(int32 arg, CONST char *buf)
{
  t_stat rc;
  uint16 data;
  int i;
  int n = buf[0] ? atol(buf) : 0;
  if (n < 0) n = 0;
  sim_printf("SP: $%04x LOW: $%04x UPR: $%04x\n",
              reg_sp, reg_splow, reg_spupr);
  for (i=n; i>=0; i--) {
    if ((rc=Read(reg_sp+i, 0, &data, 0)) != SCPE_OK) continue;
    if (i==0) sim_printf("  TOS: "); else sim_printf("  %3d: ",i);
    sim_printf("%04x ($%04x)\n", data, reg_sp+i);
  }
  return SCPE_OK;
}

static t_stat pdq3_cmd_exmscw(int32 arg, CONST char *buf)
{
  CONST char* next;
  return dbg_dump_mscw(stdout, buf[0] ? pdq3_parse_addr(&cpu_dev, buf, &next) : reg_mp);
}

static t_stat pdq3_cmd_extib(int32 arg, CONST char *buf)
{
  CONST char* next;
  return dbg_dump_tib(stdout, buf[0] ? pdq3_parse_addr(&cpu_dev, buf, &next) : reg_ctp);
}

static t_stat pdq3_cmd_exseg(int32 arg, CONST char *buf)
{
  t_stat rc;
  uint16 nsegs;
  uint16 segnum, segptr;
  CONST char* next;
  FILE* fd = stdout; /* XXX */
  
  if (reg_ssv < 0x2030 || reg_ssv > 0xf000) {
    fprintf(fd, "Cannot list segments in bootloader: incomplete tables\n");
    return SCPE_NXM;
  }
  
  if ((rc=Read(reg_ssv, -1, &nsegs, 0)) != SCPE_OK) return rc;
  
  if (buf[0]) {
    segnum = pdq3_parse_addr(&cpu_dev, buf, &next);
    fprintf(fd, "Segment $%02x\n", segnum);
    if (segnum > nsegs) {
      fprintf(fd, "Too high: maxsegs=$%02x\n",nsegs);
      return SCPE_ARG;
    }
    if ((rc=Read(reg_ssv, segnum, &segptr, 0)) != SCPE_OK) return rc;
    rc = dbg_dump_seg(fd, segptr);
  } else
    rc = dbg_dump_segtbl(fd);
  return rc;
}

static t_stat pdq3_cmd_calltree(int32 arg, CONST char *buf) {
  return dbg_calltree(stdout);
}

static t_stat pdq3_cmd_namealias(int32 arg, CONST char *buf) {
  char* name, *alias, gbuf[2*CBUFSIZE];
  
  if (buf[0]==0)
    return dbg_listalias(stdout);

  gbuf[sizeof(gbuf)-1] = '\0';
  strncpy (gbuf, buf, sizeof(gbuf)-1);
  name = strtok(gbuf, " \t");
  alias = strtok(NULL, " \t\n");
  return name == NULL || alias == NULL ? SCPE_ARG : dbg_enteralias(name, alias);
}

/**************************************************************************************
 * PDQ utility functions
 *************************************************************************************/
OPTABLE optable[] = {
/*00*/  { "SLDC0",  OP_NULL },     { "SLDC1",  OP_NULL },
/*02*/  { "SLDC2",  OP_NULL },     { "SLDC3",  OP_NULL },
/*04*/  { "SLDC4",  OP_NULL },     { "SLDC5",  OP_NULL },
/*06*/  { "SLDC6",  OP_NULL },     { "SLDC7",  OP_NULL },
/*08*/  { "SLDC8",  OP_NULL },     { "SLDC9",  OP_NULL },
/*0a*/  { "SLDC10", OP_NULL },     { "SLDC11", OP_NULL },
/*0c*/  { "SLDC12", OP_NULL },     { "SLDC13", OP_NULL },
/*0e*/  { "SLDC14", OP_NULL },     { "SLDC15", OP_NULL },
/*10*/  { "SLDC16", OP_NULL },     { "SLDC17", OP_NULL },
/*12*/  { "SLDC18", OP_NULL },     { "SLDC19", OP_NULL },
/*14*/  { "SLDC20", OP_NULL },     { "SLDC21", OP_NULL },
/*16*/  { "SLDC22", OP_NULL },     { "SLDC23", OP_NULL },
/*18*/  { "SLDC24", OP_NULL },     { "SLDC25", OP_NULL },
/*1a*/  { "SLDC26", OP_NULL },     { "SLDC27", OP_NULL },
/*1c*/  { "SLDC28", OP_NULL },     { "SLDC29", OP_NULL },
/*1e*/  { "SLDC30", OP_NULL },     { "SLDC31", OP_NULL },
/*20*/  { "SLDL1",  OP_NULL },     { "SLDL2",  OP_NULL },
/*22*/  { "SLDL3",  OP_NULL },     { "SLDL4",  OP_NULL },
/*24*/  { "SLDL5",  OP_NULL },     { "SLDL6",  OP_NULL },
/*26*/  { "SLDL7",  OP_NULL },     { "SLDL8",  OP_NULL },
/*28*/  { "SLDL9",  OP_NULL },     { "SLDL10", OP_NULL },
/*2a*/  { "SLDL11", OP_NULL },     { "SLDL12", OP_NULL },
/*2c*/  { "SLDL13", OP_NULL },     { "SLDL14", OP_NULL },
/*2e*/  { "SLDL15", OP_NULL },     { "SLDL16", OP_NULL },
/*30*/  { "SLDO1",  OP_NULL },     { "SLDO2",  OP_NULL },
/*32*/  { "SLDO3",  OP_NULL },     { "SLDO4",  OP_NULL },
/*34*/  { "SLDO5",  OP_NULL },     { "SLDO6",  OP_NULL },
/*36*/  { "SLDO7",  OP_NULL },     { "SLDO8",  OP_NULL },
/*38*/  { "SLDO9",  OP_NULL },     { "SLDO10", OP_NULL },
/*3a*/  { "SLDO11", OP_NULL },     { "SLDO12", OP_NULL },
/*3c*/  { "SLDO13", OP_NULL },     { "SLDO14", OP_NULL },
/*3e*/  { "SLDO15", OP_NULL },     { "SLDO16", OP_NULL },
/*40*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*42*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*44*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*46*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*48*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*4a*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*4c*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*4e*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*50*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*52*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*54*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*56*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*58*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*5a*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*5c*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*5e*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*60*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*62*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*64*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*66*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*68*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*6a*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*6c*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*6e*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*70*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*72*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*74*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*76*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*78*/  { "SIND0",  OP_NULL },     { "SIND1",  OP_NULL },
/*7a*/  { "SIND2",  OP_NULL },     { "SIND3",  OP_NULL },
/*7c*/  { "SIND4",  OP_NULL },     { "SIND5",  OP_NULL },
/*7e*/  { "SIND6",  OP_NULL },     { "SIND7",  OP_NULL },
/*80*/  { "LDCB",   OP_UB },       { "LDCI",   OP_W },
/*82*/  { "LCA",    OP_AB },       { "LDC",    OP_BUB },
/*84*/  { "LLA",    OP_B },        { "LDO",    OP_B },
/*86*/  { "LAO",    OP_B },        { "LDL",    OP_B },
/*88*/  { "LDA",    OP_DBB },      { "LOD",    OP_DBB },
/*8a*/  { "UJP",    OP_SB },       { "UJPL",   OP_SW },
/*8c*/  { "MPI",    OP_NULL },     { "DVI",    OP_NULL },
/*8e*/  { "STM",    OP_UB },       { "MODI",   OP_NULL },
/*90*/  { "CPL",    OP_UB },       { "CPG",    OP_UB },
/*92*/  { "CPI",    OP_DBUB },     { "CXL",    OP_UBUB },
/*94*/  { "CXG",    OP_UBUB },     { "CXI",    OP_UBDBUB },
/*96*/  { "RPU",    OP_B },        { "CPF",    OP_NULL },
/*98*/  { "LDCN",   OP_NULL },     { "LSL",    OP_DB },
/*9a*/  { "LDE",    OP_UBB },      { "LAE",    OP_UBB },
/*9c*/  { "NOP",    OP_NULL },     { "LPR",    OP_NULL },
/*9e*/  { "BPT",    OP_NULL },     { "BNOT",   OP_NULL },
/*a0*/  { "LOR",    OP_NULL },     { "LAND",   OP_NULL },
/*a2*/  { "ADI",    OP_NULL },     { "SBI",    OP_NULL },
/*a4*/  { "STL",    OP_B },        { "SRO",    OP_B },
/*a6*/  { "STR",    OP_DBB },      { "LDB",    OP_NULL },
/*a8*/  { "LHO",    OP_NULL },     { "LVO",    OP_NULL },
/*aa*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*ac*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*ae*/  { "",       OP_ERROR },    { "",       OP_ERROR },
/*b0*/  { "EQUI",   OP_NULL },     { "NEQI",   OP_NULL },
/*b2*/  { "LEQI",   OP_NULL },     { "GEQI",   OP_NULL },
/*b4*/  { "LEUSW",  OP_NULL },     { "GEUSW",  OP_NULL },
/*b6*/  { "EQUPWR", OP_NULL },     { "LEQPWR", OP_NULL },
/*b8*/  { "GEQPWR", OP_NULL },     { "EQUBYT", OP_B },
/*ba*/  { "LEQBYT", OP_B },        { "GEQBYT", OP_B },
/*bc*/  { "SRS",    OP_NULL },     { "SWAP",   OP_NULL },
/*be*/  { "TNC",    OP_NULL },     { "RND",    OP_NULL },
/*c0*/  { "ADR",    OP_NULL },     { "SBR",    OP_NULL },
/*c2*/  { "MPR",    OP_NULL },     { "DVR",    OP_NULL },
/*c4*/  { "STO",    OP_NULL },     { "MOV",    OP_B },
/*c6*/  { "DUP2",   OP_NULL },     { "ADJ",    OP_UB },
/*c8*/  { "STB",    OP_NULL },     { "LDP",    OP_NULL },
/*ca*/  { "STP",    OP_NULL },     { "CHK",    OP_NULL },
/*cc*/  { "FLT",    OP_NULL },     { "EQUREAL",OP_NULL },
/*ce*/  { "LEQREAL",OP_NULL },     { "GEQREAL",OP_NULL },
/*d0*/  { "LDM",    OP_UB },       { "SPR",    OP_NULL },
/*d2*/  { "EFJ",    OP_SB },       { "NFJ",    OP_SB },
/*d4*/  { "FJP",    OP_SB },       { "FJPL",   OP_SW },
/*d6*/  { "XJP",    OP_B },        { "IXA",    OP_B },
/*d8*/  { "IXP",    OP_UBUB },     { "STE",    OP_UBB },
/*da*/  { "INN",    OP_NULL },     { "UNI",    OP_NULL },
/*dc*/  { "INT",    OP_NULL },     { "DIF",    OP_NULL },
/*de*/  { "SIGNAL", OP_NULL },     { "WAIT",   OP_NULL },
/*e0*/  { "ABI",    OP_NULL },     { "NGI",    OP_NULL },
/*e2*/  { "DUP1",   OP_NULL },     { "ABR",    OP_NULL },
/*e4*/  { "NGR",    OP_NULL },     { "LNOT",   OP_NULL },
/*e6*/  { "IND",    OP_B },        { "INC",    OP_B },
};

static uint16 UB(t_value arg) 
{
  return arg & 0xff;
}
static uint16 DB(t_value arg) 
{
  return UB(arg);
}
static int16 W(t_value arg1, t_value arg2)
{
  uint16 wl = arg1 & 0xff;
  uint16 wh = arg2 & 0xff;
  return wl | ((wh << 8) & 0xff00);
}

static int16 SW(t_value arg1, t_value arg2)
{
  return W(arg1,arg2);
}
static int16 SB(t_value arg)
{
  int16 w = arg & 0xff;
  if (w & 0x80) w |= 0xff00;
  return w;
}
static uint16 B(t_value arg1, t_value arg2, int* sz) {
  uint16 wh = arg1 & 0xff;
  uint16 wl;
  if (wh & 0x80) {
    wl = arg2 & 0xff;
    wl |= ((wh & 0x7f) << 8);
    *sz = 2;
    return wl;
  } else {
    *sz = 1;
    return wh;
  }
}

t_stat print_hd(FILE *of, t_value val, t_bool hexdec, t_bool isbyte)
{
  uint16 data = isbyte ? (val & 0xff) : (val & 0xffff);
     
  if (hexdec)
    fprintf(of,"%0xh",data);
  else
    fprintf(of,"%d",data);
  return SCPE_OK;
}

t_stat fprint_sym_m (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
  uint16 op, arg1, arg2, arg3;
  int16 sarg;
  t_stat size = 0;
  int optype, sz;
  t_bool hexdec = (sw & SWMASK('H')) ? TRUE : FALSE;
  addr = ADDR_OFF(addr);

  op = val[0];
  if (op > 0xe7) return SCPE_ARG;

  optype = optable[op].flags;
  if (optype > OP_ERROR) {
    fprintf(of,"%-8s", optable[op].name);
    switch (optype) {
    case OP_NULL:
      break;
    case OP_UB:
      size = 1; arg1 = UB(val[1]);
      print_hd(of, arg1, hexdec, FALSE);
      break;
    case OP_W:
      size = 2; sarg = W(val[1],val[2]);
      print_hd(of, sarg, hexdec, FALSE);
      break;
    case OP_AB:
      arg1 = B(val[1],val[2], &sz); size = sz;
      fprintf(of,"#%x", arg1*2);
      break;
    case OP_B:
      arg1 = B(val[1],val[2], &sz); size = sz;
      print_hd(of, arg1, hexdec, FALSE);
      break;
    case OP_DBB:
      arg1 = DB(val[1]);
      arg2 = B(val[2],val[3], &sz); size = sz+1;
      print_hd(of, arg1, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg2, hexdec, FALSE);
      break;
    case OP_UBB:
      arg1 = UB(val[1]);
      arg2 = B(val[2],val[3], &sz); size = sz+1;
      print_hd(of, arg1, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg2, hexdec, FALSE);
      break;
    case OP_BUB:
      arg1 = B(val[1],val[2], &sz); size = sz+1;
      arg2 = UB(val[sz+1]);
      print_hd(of, arg1, hexdec, FALSE); fputc(',',of); 
      print_hd(of, arg2, hexdec, TRUE);
      break;
    case OP_SB:
      size = 1; sarg = SB(val[1]);
      fprintf(of,"#%x", addr+sarg+2);
      break;
    case OP_SW:
      size = 2; sarg = SW(val[1],val[2]);
      fprintf(of,"#%x", addr+sarg+3);
      break;
    case OP_DBUB:
      size = 2; arg1 = DB(val[1]);
      arg2 = UB(val[2]);
      print_hd(of, arg1, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg2, hexdec, TRUE);
      break;
    case OP_UBUB:
      size = 2; arg1 = UB(val[1]);
      arg2 = UB(val[2]);
      print_hd(of, arg1, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg2, hexdec, TRUE);
      break;
    case OP_UBDBUB:
      size  = 3; arg1 = UB(val[1]);
      arg2 = DB(val[2]);
      arg3 = UB(val[3]);
      print_hd(of, arg1, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg2, hexdec, TRUE); fputc(',',of); 
      print_hd(of, arg3, hexdec, TRUE);
      break;
    case OP_DB:
      size = 1; arg1 = DB(val[1]);
      print_hd(of, arg1, hexdec, TRUE);
      break;
    }
    return -size;
  } else {
    fprintf(of,"%-8s","DB"); print_hd(of, op, hexdec, TRUE);
    return SCPE_OK;
  }
}

/* Symbolic decode
   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to data
        *uptr   =       pointer to unit 
        sw      =       switches
   Outputs:
        return  =       status code
*/
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
  t_addr off;
  T_FLCVT t;
  int ch;
  
  if (sw & SWMASK('M') && !ADDR_ISWORD(addr)) {
    return fprint_sym_m(of, addr, val, uptr, sw);
  }
  if (sw & SWMASK('B')) { /* as BYTE */
    if (ADDR_ISWORD(addr)) {
      fprint_val(of, (val[0]>>8) & 0xff, cpu_dev.dradix, 8, PV_RZRO);
      fprintf(of, ",");
      fprint_val(of, val[0] & 0xff, cpu_dev.dradix, 8, PV_RZRO);
    } else
      fprint_val(of, val[0], cpu_dev.dradix, 8, PV_RZRO);
    return SCPE_OK;
  }
  if (sw & SWMASK('C')) { /* as CHAR */
    if (ADDR_ISWORD(addr)) {
      ch = val[0] & 0xff;
      fprintf(of, isprint(ch) ? "%c," : "%02x,", ch);
      ch = val[0]>>8;
      fprintf(of, isprint(ch) ? "%c" : "%02x", ch);
    } else {
      ch = val[0] & 0xff;
      fprintf(of, isprint(ch) ? "%c" : "%02x", ch);
    }
    return SCPE_OK;
  }
  if (sw & SWMASK('W')) { /* as WORD */
    if (ADDR_ISWORD(addr)) {
      fprint_val(of, val[0], cpu_dev.dradix, 16, PV_RZRO);
      off = ADDR_OFF(addr);
      if (off > (t_addr)(reg_bp+MSCW_SZ-1)) 
        fprintf(of," (GLOBAL+%d)", off - reg_bp - MSCW_SZ + 1);
      else if (off >= reg_mp && off <= (t_addr)(reg_mp+OFFB_MSSEG)) 
        fprintf(of," (MP+%d)", off - reg_mp);
      else if (off > (t_addr)(reg_mp+MSCW_SZ-1)) 
        fprintf(of," (LOCAL+%d)", off - reg_mp - MSCW_SZ + 1);
      else if (off >= reg_sp && off < reg_spupr) 
        fprintf(of," (SP+%d)", off - reg_sp);
    } else {
      fprint_val(of, val[0], cpu_dev.dradix, 8, PV_RZRO);
      fprint_val(of, val[1], cpu_dev.dradix, 8, PV_RZRO);
    }    
    return SCPE_OK;
  }
  if (sw & SWMASK('F')) { /* as FLOAT */
    t.i[0] = val[1];
    t.i[1] = val[0];
    fprintf(of, "%12.6e", t.f);
    return -1;
  }
  if (sw & SWMASK('S')) { /* as semaphore */
    fprintf(of, "SEM(count=%d, waitq=$%04x)", val[0], val[1]);
    return -1;
  }
  if (sw & SWMASK('M')) { /* as MSCW */
    dbg_dump_mscw(of, val[0]);
    return SCPE_OK;
  }
  if (sw & SWMASK('T')) { /* as TIB */
    dbg_dump_tib(of, addr);
    return SCPE_OK;
  }
  return SCPE_ARG;
}

/* Symbolic input
   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/
t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
  return SCPE_ARG;
}
