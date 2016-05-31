/* pdq3_debug.c: PDQ3 debug helper

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

   20130421 hv initial version
   20130928 hv fix problem with callstack when S_Start_P patches MSCW
   20131012 hv view calltree returned incorrect segment of caller
   20141003 hv compiler suggested warnings (vc++2013, gcc)
   */
#include "pdq3_defs.h"

static uint8 *opdebug = NULL;

static void dbg_opdbgcreate() {
  int i;
  FILE *fd = fopen(DEBUG_OPDBGFILE, "w");
  if (fd==NULL) {
    fprintf(stderr,"Cannot create %s\n", DEBUG_OPDBGFILE);
    exit(1);
  }
  for (i=DEBUG_MINOPCODE; i<DEBUG_MAXOPCODE; i++) {
    if (DEBUG_VALIDOP(i)) {
      fprintf(fd,"%x %d ;%s\n",
        i, DEBUG_PRE|DEBUG_POST, optable[i].name);
    } else {
      fprintf(fd,"%x %d ;invalid\n",
        i, DEBUG_PRE|DEBUG_POST);
    }
  }
  fclose(fd);
  fprintf(stderr,"%s created. Adapt file manually and restart simh\n", DEBUG_OPDBGFILE);
  exit(2);
}

static void dbg_opdbginit() {
  char line[100];
  int i, f;
  FILE* fd = fopen(DEBUG_OPDBGFILE,"r");
  if (fd == NULL)
    dbg_opdbgcreate(); /* will not return */

  if (opdebug == NULL)
    opdebug = (uint8*)calloc(DEBUG_MAXOPCODE-DEBUG_MINOPCODE,sizeof(uint8));

  for (i=DEBUG_MINOPCODE; i<DEBUG_MAXOPCODE; i++)
      opdebug[i-DEBUG_MINOPCODE] = DEBUG_PRE|DEBUG_POST;
  while (!feof(fd)) {
    fgets(line,100,fd);
    sscanf(line,"%x %d", &i, &f);
    opdebug[i-DEBUG_MINOPCODE] = f;
  }
  fclose(fd);
}

t_stat dbg_check(t_value op, uint8 flag) {
  if (opdebug[op-DEBUG_MINOPCODE] & flag) {
    if (flag & DEBUG_PRE) {
      opdebug[op-DEBUG_MINOPCODE] &= ~DEBUG_PRE;
      return STOP_DBGPRE;
    } else
      return STOP_DBGPOST;
  }
  return SCPE_OK;
}

t_stat dbg_dump_tib(FILE *fd, uint16 base) {
  t_stat rc;
  uint16 data;
  fprintf(fd, "TIB at $%04x (CTP=$%04x, RQ=$%04x)\n",base, reg_ctp, reg_rq);
  if ((rc=ReadEx(base, OFF_WAITQ, &data)) != SCPE_OK) return rc;
  fprintf(fd, " WAITQ: $%04x\n",data);
  if ((rc=ReadBEx(base+OFFB_PRIOR, 0, &data)) != SCPE_OK) return rc;
  fprintf(fd, " PRIOR: %02x\n",data);
  if ((rc=ReadEx(base, OFF_SPLOW, &data)) != SCPE_OK) return rc;
  fprintf(fd, " SPLOW: $%04x\n",data);
  if ((rc=ReadEx(base, OFF_SPUPR, &data)) != SCPE_OK) return rc;
  fprintf(fd, " SPUPR: $%04x\n",data);
  if ((rc=ReadEx(base, OFF_SP, &data)) != SCPE_OK) return rc;
  fprintf(fd, " SP:    $%04x\n",data);
  if ((rc=ReadEx(base, OFF_MP, &data)) != SCPE_OK) return rc;
  fprintf(fd, " MP:    $%04x\n",data);
  if ((rc=ReadEx(base, OFF_BP, &data)) != SCPE_OK) return rc;
  fprintf(fd, " BP:    $%04x\n",data);
  if ((rc=ReadEx(base, OFF_IPC, &data)) != SCPE_OK) return rc;
  fprintf(fd, " IPC:   #%04x\n",data);
  if ((rc=ReadEx(base, OFF_SEGB, &data)) != SCPE_OK) return rc;
  fprintf(fd, " SEGB:  $%04x\n",data);
  if ((rc=ReadEx(base, OFF_HANGP, &data)) != SCPE_OK) return rc;
  fprintf(fd, " HANGP: $%04x\n",data);
  if ((rc=ReadEx(base, OFF_IORSLT, &data)) != SCPE_OK) return rc;
  fprintf(fd, " IORSLT: %04x\n",data);
  if ((rc=ReadEx(base, OFF_SIBS, &data)) != SCPE_OK) return rc;
  fprintf(fd, " SIBS:  $%04x\n",data);
  return SCPE_OK;
}

t_stat dbg_dump_queue(FILE* fd, const char* qname, uint16 q) {
  t_stat rc;
  fprintf(fd, "dump queue %s: address=$%04x\n  ",qname, q);
  while (q != NIL) {
    fprintf(fd, "$%04x->",q);
    if ((rc=ReadEx(q, OFF_WAITQ, &q)) != SCPE_OK) return rc;
  }
  fprintf(fd, "NIL\n");
  return SCPE_OK;
}

t_stat dbg_dump_mscw(FILE* fd, uint16 base) {
  t_stat rc;
  uint16 data;
  fprintf(fd, "MSCW at $%04x\n",base);
  if ((rc=ReadEx(base, OFF_MSSTAT, &data)) != SCPE_OK) return rc;
  fprintf(fd, " MSSTAT: $%04x\n", data);
  if ((rc=ReadEx(base, OFF_MSDYNL, &data)) != SCPE_OK) return rc;
  fprintf(fd, " MSDYNL: $%04x\n", data);
  if ((rc=ReadEx(base, OFF_MSIPC, &data)) != SCPE_OK) return rc;
  fprintf(fd, " MSIPC:  $%04x\n", data);
  if ((rc=ReadBEx(base+OFFB_MSSEG, 0, &data)) != SCPE_OK) return rc;
  fprintf(fd, " MSSEG:  %02x\n", data);
  return SCPE_OK;
}

void dbg_enable() {
  cpu_dev.dctrl |= (DBG_CPU_READ|DBG_CPU_WRITE|DBG_CPU_STACK);
}
  
/******************************************************************************
 * Segment Tracking support
 *****************************************************************************/

static char* pdq3_segname(uint16 nameptr) {
  static char name[10];
  uint16 data;
  int i;
  for (i=0; i<8; i++) {
    ReadBEx(nameptr,i,&data);
    name[i] = data != ' ' ? data : 0;
  }
  name[8] = 0;
  return name;
}

t_stat dbg_dump_seg(FILE* fd, uint16 segptr) {
  t_stat rc;
  uint16 data;
  if ((rc=ReadEx(segptr, OFF_SEGBASE, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  BASE:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGLENG, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  LENGTH:  $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGREFS, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  REFS:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGADDR, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  ADDR:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGUNIT, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  UNIT:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_PREVSP, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  PREVSP:  $%04x\n",data);
  fprintf(fd, "  NAME:    %s\n", pdq3_segname(segptr+OFF_SEGNAME));
  if ((rc=ReadEx(segptr, OFF_SEGLINK, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  LINK:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGGLOBAL, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  GLOBAL:  $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGINIT, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  INIT:    $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEG13, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  entry13: $%04x\n",data);
  if ((rc=ReadEx(segptr, OFF_SEGBACK, &data)) != SCPE_OK) return rc;
  fprintf(fd, "  SELF:    $%04x\n",data);
  return SCPE_OK;
}

t_stat dbg_dump_segtbl(FILE* fd) {
  int i;
  uint16 segptr, nsegs;
  t_stat rc;
  
  if (reg_ssv < 0x2030 || reg_ssv > 0xf000) {
    sim_printf("Cannot list segments in bootloader: incomplete tables\n");
    return SCPE_NXM;
  }

  if ((rc=Read(reg_ssv, -1, &nsegs, 0)) != SCPE_OK) return rc;

  fprintf(fd, "Segment table: ssv=$%04x size=%d\n",reg_ssv, nsegs);
  for (i=0; i<=nsegs; i++) {
    if ((rc=ReadEx(reg_ssv, i, &segptr)) != SCPE_OK) return rc;
    fprintf(fd, " %02x %04x %s\n",i,segptr, pdq3_segname(segptr + OFF_SEGNAME));
  }
  return SCPE_OK;
}

/* segment tracking */
typedef struct _seginfo {
  uint16 base; /* base load address */
  struct _seginfo* next;
  uint16 idx; /* index into SSV table */
  char name[10]; /* segment name */
  uint16 size;
  uint16 nproc;
  uint16 segno;
} SEGINFO;

#define SEGHASHSIZE 97
SEGINFO* seghash[SEGHASHSIZE];
#define SEGHASHFUNC(i) (i % SEGHASHSIZE)

t_stat dbg_segtrackinit() {
  int i;
  for (i=0; i<SEGHASHSIZE; i++)
    seghash[i] = NULL;
  return SCPE_OK;
}

static SEGINFO* new_seginfo(SEGINFO* next, uint16 base) {
  SEGINFO* s = (SEGINFO*)malloc(sizeof(SEGINFO));
  s->next = next;
  s->base = base;
  return s;
}

static SEGINFO* find_seginfo(uint16 base, int* idx) {
  SEGINFO* s;
  *idx = SEGHASHFUNC(base);
  s = seghash[*idx];
  while (s && s->base != base) s = s->next;
  return s;
}

t_stat dbg_segtrack(uint16 segbase) {
  t_stat rc;
  int idx;
  SEGINFO* s = find_seginfo(segbase, &idx);
  if (!s) {
    s = seghash[idx] = new_seginfo(seghash[idx], segbase);
    if ((rc=ReadEx(segbase, 0, &s->size)) != SCPE_OK) return rc;
    strcpy(s->name, segbase==0xf418 ? "HDT" : pdq3_segname(segbase+2));
    if ((rc=ReadBEx(segbase+s->size, 0, &s->segno)) != SCPE_OK) return rc;
    if ((rc=ReadBEx(segbase+s->size, 1, &s->nproc)) != SCPE_OK) return rc;
//    printf("Entered at %04x: %s sz=%x seg=%x np=%x\n",segbase, s->name, s->size, s->segno, s->nproc);
  }
  return SCPE_OK;
}

/******************************************************************************
 * Name Alias Handling
 *****************************************************************************/

typedef struct _aliases {
  char* key;
  char* alias;
  struct _aliases* next;
} ALIASES;

#define ALIASHASHSIZE 97
static ALIASES* aliases[ALIASHASHSIZE];

static t_stat dbg_aliasesinit() {
  int i;
  for (i=0; i<ALIASHASHSIZE; i++)
    aliases[i] = NULL;
  return SCPE_OK;
}

static int aliashash(const char* key) {
  int i, h=0;
  int len = strlen(key);
  for (i=0; i<len; i++)
    h += key[i];
  return h % ALIASHASHSIZE;
}

static ALIASES* find_alias(const char* key, int* idx) {
  ALIASES* a;
  char gbuf[CBUFSIZE], gbuf2[CBUFSIZE];

  get_glyph(key, gbuf, 0);
  *idx = aliashash(gbuf);
  a = aliases[*idx];
  if (a) get_glyph(a->key, gbuf2, 0);
  while (a && strcmp(gbuf2,gbuf)) {
    a = a->next;
    if (a) get_glyph(a->key, gbuf2, 0);
  }
  return a;
}

t_stat dbg_enteralias(const char* key, const char* value) {
  int idx;
  ALIASES* a = find_alias(key, &idx);
  if (!a) {
    a = (ALIASES*)malloc(sizeof(ALIASES));
    a->key = strdup(key);
    a->alias = strdup(value);
    a->next = aliases[idx];
    aliases[idx] = a;
  }
  return SCPE_OK;
}

t_stat dbg_listalias(FILE* fd) {
  int i;
  ALIASES* a;
  fprintf(fd, "Name table:\n");
  for (i=0; i<ALIASHASHSIZE; i++) {
    a = aliases[i];
    while (a) {
      fprintf(fd, "  Name %s = %s\n", a->key, a->alias);
      a = a->next;
    }
  }
  return SCPE_OK;
}

/******************************************************************************
 * Procedure tracking support
 *****************************************************************************/
 
typedef struct _procinfo {
  struct _procinfo *next;
  uint16 procno;
  SEGINFO* seg;
  uint16 localsz;
  uint16 freesz;
  uint16 mscw;
  uint16 segb;
  uint16 instipc;
  uint16 ipc;
} PROCINFO;

const char* find_procname(PROCINFO* p) {
  ALIASES* a;
  int dummy;
  static char buf[100];
  sprintf(buf,"%s:proc%d", p->seg->name, p->procno);
  a = find_alias(buf, &dummy);
  if (a) return a->alias;
  return buf;
}

static PROCINFO* procroot = NULL;

static PROCINFO* new_procinfo(uint16 segbase, uint16 procno, uint16 mscw, uint16 osegb) {
  int dummy;
  uint16 procbase, procaddr;
  uint16 exitic, sz1, sz2;
  PROCINFO* p = (PROCINFO*)malloc(sizeof(PROCINFO));
  p->procno = procno;
  p->mscw = mscw;
  p->seg = find_seginfo(segbase, &dummy);
  p->segb = osegb;
  p->instipc = ADDR_OFF(PCX);
  ReadEx(mscw,OFF_MSIPC, &p->ipc);
  ReadEx(segbase, 0, &procbase);
  ReadEx(segbase+procbase-procno, 0, &procaddr);
  ReadEx(segbase+procaddr, 0, &p->localsz);
  ReadEx(segbase+procaddr-1, 0, &exitic);
  ReadBEx(segbase, exitic, &sz1);
  if (sz1==0x96) {
    ReadBEx(segbase, exitic+1, &sz1);
    if (sz1 & 0x80) {
      ReadBEx(segbase, exitic+2, &sz2);
      sz1 = ((sz1 & 0x7f)<<8) | sz2;
    }
    p->freesz = sz1;
  }
  return p;
}

t_stat dbg_procenter(uint16 segbase, uint16 procno, uint16 mscw, uint16 osegb) {
  PROCINFO* p = new_procinfo(segbase, procno, mscw, osegb);
  p->next = procroot;
  procroot = p;
  return SCPE_OK;
}

t_stat dbg_procleave() {
  t_stat rc;
  PROCINFO* p = procroot;
  uint16 ipc,pipc;
  while (p) {
    pipc = p->ipc;
    if ((rc=ReadEx(p->mscw,OFF_MSIPC, &ipc)) != SCPE_OK) return rc;
    procroot = p->next;
    free(p);
    if (pipc == ipc) break;
    p = procroot;
  }
  return SCPE_OK;
}

t_stat dbg_calltree(FILE* fd) {
  PROCINFO* p = procroot, *lastp;
  
  if (!p) {
    fprintf(fd,"Callstack is empty\n");
    return SCPE_OK;
  }
  
  fprintf(fd,"Calltree:\nCurrently in %s at %04x:%04x\n",
    find_procname(p), reg_segb, reg_ipc);
  lastp = p;
  p = p->next;
  while (p) {
    fprintf(fd," at %04x:%04x called by %s (%04x:%04x)\n",
        lastp->segb, lastp->instipc, find_procname(p), p->segb, p->instipc);
    lastp = p;
    p = p->next;
  }
  return SCPE_OK;
}

/******************************************************************************
 * Initialization
 *****************************************************************************/

t_stat dbg_init() {
  dbg_opdbginit();
  dbg_segtrackinit();
  dbg_aliasesinit();
  
  return SCPE_OK;
}


