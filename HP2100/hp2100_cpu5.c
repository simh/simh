/* hp2100_cpu5.c: HP 1000 RTE-6/VM VMA and RTE-IV EMA instructions

   Copyright (c) 2007-2008, Holger Veit
   Copyright (c) 2006-2016, J. David Bryan

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU5         RTE-6/VM and RTE-IV firmware option instructions

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   17-Dec-12    JDB     Fixed cpu_vma_mapte to return FALSE if not a VMA program
   09-May-12    JDB     Separated assignments from conditional expressions
   23-Mar-12    JDB     Added sign extension for dim count in "cpu_ema_resolve"
   28-Dec-11    JDB     Eliminated unused variable in "cpu_ema_vset"
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   30-Jul-08    JDB     Redefined ABORT to pass address, moved def to hp2100_cpu.h
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   01-May-08    HV      Fixed mapping bug in "cpu_ema_emap"
   21-Apr-08    JDB     Added EMA support from Holger
   25-Nov-07    JDB     Added TF fix from Holger
   07-Nov-07    HV      VMACK diagnostic tests 1...32 passed
   19-Oct-07    JDB     Corrected $LOC operand profile to OP_CCCACC
   03-Oct-07    HV      Moved RTE-6/VM instrs from hp2100_cpu0.c
   26-Sep-06    JDB     Created

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.
*/

#include <setjmp.h>
#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"


/* RTE-6/VM Virtual Memory Area Instructions

   RTE-6/VM (product number 92084A) introduced Virtual Memory Area (VMA)
   instructions -- a superset of the RTE-IV EMA instructions.  Different
   microcode was supplied with the operating system that replaced the microcode
   used with RTE-IV.  Microcode was limited to the E/F-Series, and the M-Series
   used software equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92084A  92084A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     .PMAP    105240   Map VMA page into map register
     $LOC     105241   Load on call
     [test]   105242   [self test]
     .SWP     105243   [Swap A and B registers]
     .STAS    105244   [STA B; LDA SP]
     .LDAS    105245   [LDA SP]
     .MYAD    105246   [NOP in microcode]
     .UMPY    105247   [Unsigned multiply and add]

     .IMAP    105250   Integer element resolve address and map
     .IMAR    105251   Integer element resolve address
     .JMAP    105252   Double integer element resolve address and map
     .JMAR    105253   Double integer element resolve address
     .LPXR    105254   Map pointer in P+1 plus offset in P+2
     .LPX     105255   Map pointer in A/B plus offset in P+1
     .LBPR    105256   Map pointer in P+1
     .LBP     105257   Map pointer in A/B registers

   Implementation notes:

    1. The opcodes 105243-247 are undocumented and do not appear to be used in
       any HP software.

    2. The opcode list in the CE Handbook incorrectly shows 105246 as ".MYAD -
       multiply 2 signed integers."  The microcode listing shows that this
       instruction was deleted, and the opcode is now a NOP.

    3. RTE-IV EMA and RTE-6 VMA instructions shared the same address space, so a
       given machine could run one or the other, but not both.

   Additional references:
    - RTE-6/VM VMA/EMA Microcode Source (92084-18828, revision 3).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).
    - M/E/F-Series Computer Systems CE Handbook (5950-3767, Jul-1984).
*/

static const OP_PAT op_vma[16] = {
  OP_N,    OP_CCCACC, OP_N,    OP_N,                    /* .PMAP  $LOC   [test] .SWAP  */
  OP_N,    OP_N,      OP_N,    OP_K,                    /* .STAS  .LDAS  .MYAD  .UMPY  */
  OP_A,    OP_A,      OP_A,    OP_A,                    /* .IMAP  .IMAR  .JMAP  .JMAR  */
  OP_AA,   OP_A,      OP_A,    OP_N                     /* .LPXR  .LPX   .LBPR  .LBP   */
  };

/* some addresses in page0 of RTE-6/VM */
static const uint32 idx     = 0001645;
static const uint32 xmata   = 0001646;
static const uint32 xi      = 0001647;
static const uint32 xeqt    = 0001717;
static const uint32 vswp    = 0001776;
static const uint32 umaps   = 0003740;
static const uint32 page30  = 0074000;
static const uint32 page31  = 0076000;
static const uint32 ptemiss = 0176000;

/* frequent constants in paging */
#define SUITMASK 0176000
#define NILPAGE  0176000
#define PAGEIDX  0001777
#define MSEGMASK 0076000
#define RWPROT   0141777


/* microcode version of resolve(): allows a much higher # of indirection levels. Used for
   instance for LBP microcode diagnostics which will check > 100 levels.
 */
#define VMA_INDMAX 200

static t_stat vma_resolve (uint32 MA, uint32 *addr, t_bool debug)
{
uint32 i;
uint32 faultma = MA;

for (i = 0; (i < VMA_INDMAX) && (MA & I_IA); i++) { /* resolve multilevel */
    MA = ReadW (MA & VAMASK);                       /* follow address chain */
    }

if (MA & I_IA) {
    if (debug)
        fprintf(sim_deb,">>CPU VMA: vma_resolve indirect loop addr=%06o\n",faultma);
    return STOP_IND;                                /* indirect loop */
    }

*addr = MA;
return SCPE_OK;
}

/* $LOC
   ASSEMBLER CALLING SEQUENCE:

   $MTHK NOP             RETURN ADDRESS OF CALL (REDONE AFTER THIS ROUTINE)
         JSB $LOC
   .DTAB OCT LGPG#       LOGICAL PAGE # AT WHICH THE NODE TO
  *                      BE MAPPED IN BELONGS  (0-31)
         OCT RELPG       RELATIVE PAGE OFFSET FROM BEGINING
  *                      OF PARTITION OF WHERE THAT NODE RESIDES.
  *                      (0 - 1023)
         OCT RELBP       RELATIVE PAGE OFFSET FROM BEGINING OF
  *                      PARTITION OF WHERE BASE PAGE RESIDES
  *                      (0 - 1023)
   CNODE DEF .CNOD       THIS IS THE ADDRESS OF CURRENT PATH # WORD
   .ORD  OCT XXXXX       THIS NODE'S LEAF # (IE PATH #)
   .NOD# OCT XXXXX       THIS NODE'S ORDINAL #
*/

static t_stat cpu_vma_loc(OPS op,uint32 intrq,t_bool debug)
{
uint32 eqt,mls,pnod,lstpg,fstpg,rotsz,lgpg,relpg,relbp,matloc,ptnpg,physpg,cnt,pgs,umapr;

eqt = ReadIO(xeqt,UMAP);                            /* get ID segment */
mls = ReadIO(eqt+33,SMAP);                          /* get word33 of alternate map */
if ((mls & 0x8000) == 0) {                          /* this is not an MLS prog! */
    PR = err_PC;
    if (debug)
        fprintf(sim_deb,">>CPU VMA: cpu_vma_loc at P=%06o: not an MLS program\n", PR);
    if (mp_control) MP_ABORT (eqt+33);              /* allow an MP abort */
    return STOP_HALT;                               /* FATAL error! */
    }

pnod = mls & 01777;                                 /* get #pages of mem res nodes */
if (pnod == 0) {                                    /* no pages? FATAL! */
    PR = err_PC;
    if (debug)
        fprintf(sim_deb,">>CPU VMA: cpu_vma_loc at P=%06o: no mem resident pages\n", PR);
    if (mp_control) MP_ABORT (eqt+33);              /* allow an MP abort */
    return STOP_HALT;
    }

lstpg = (ReadIO(eqt+29,SMAP) >> 10) - 1;            /* last page# of code */
fstpg =  ReadIO(eqt+23,SMAP) >> 10;                 /* index to 1st addr + mem nodes */
rotsz =  fstpg - (ReadIO(eqt+22,SMAP) >> 10);       /* #pages in root */
lgpg = op[0].word;

/* lets do some consistency checks, CPU halt if they fail */
if (lstpg < lgpg || lgpg < fstpg) {                 /* assert LSTPG >= LGPG# >= FSTPG */
    PR = err_PC;
    if (debug)
        fprintf(sim_deb,
                ">>CPU VMA: $LOC at P=%06o: failed check LSTPG >= LGPG# >= FSTPG\n",PR);
    if (mp_control) MP_ABORT (eqt+22);              /* allow an MP abort */
    return STOP_HALT;
    }

relpg = op[1].word;
if (pnod < relpg || relpg < (rotsz+1)) {            /* assert #PNOD >= RELPG >= ROTSZ+1 */
    PR = err_PC;
    if (debug)
        fprintf(sim_deb,
                ">>CPU VMA: $LOC at %06o: failed check #PNOD >= RELPG >= ROTSZ+1\n",PR);
    if (mp_control) MP_ABORT (eqt+22);              /* allow an MP abort */
    return STOP_HALT;
    }

relbp = op[2].word;
if (relbp != 0)                                     /* assert RELBP == 0 OR */
    if (pnod < relbp || relbp < (rotsz+1)) {        /* #PNOD >= RELBP >= ROTSZ+1 */
        PR = err_PC;
        if (debug)
            fprintf(sim_deb,
                    ">>CPU VMA: $LOC at P=%06o: failed check: #PNOD >= RELBP >= ROTSZ+1\n",PR);
        if (mp_control) MP_ABORT (eqt+22);          /* allow an MP abort */
        return STOP_HALT;
    }

cnt = lstpg - lgpg + 1;                             /* #pages to map */
pgs = pnod - relpg + 1;                             /* #pages from start node to end of code */
if (pgs < cnt) cnt = pgs;                           /* ensure minimum, so not to map into EMA */

matloc  = ReadIO(xmata,UMAP);                       /* get MAT $LOC address */
ptnpg  = ReadIO(matloc+3,SMAP) & 01777;             /* index to start phys pg */
physpg = ptnpg + relpg;                             /* phys pg # of node */
umapr = 32 + lgpg;                                  /* map register to start */

/* do an XMS with AR=umapr,BR=physpg,XR=cnt */
if (debug)
    fprintf(sim_deb,
            ">>CPU VMA: $LOC map %d pgs from phys=%06o to mapr=%d\n",
            cnt,physpg,umapr);
while (cnt != 0) {
    dms_wmap (umapr, physpg);                       /* map pages of new overlay segment */
    cnt = (cnt - 1) & DMASK;
    umapr = (umapr + 1) & DMASK;
    physpg = (physpg + 1) & DMASK;
    }

dms_wmap(32,relbp+ptnpg);                           /* map base page again */
WriteW(op[3].word,op[4].word);                      /* path# we are going to */

PR = (PR - 8) & DMASK;                              /* adjust P to return address */
                                                    /* word before the $LOC microinstr. */
PR = (ReadW(PR) - 1) & DMASK;                       /* but the call has to be rerun, */
                                                    /* so must skip back to the original call */
                                                    /* which will now lead to the real routine */
if (debug)
    fprintf(sim_deb,">>CPU VMA: $LOC done: path#=%06o, P=%06o\n",op[4].word,PR);
return SCPE_OK;
}

/* map pte into last page
   return FALSE if page fault, nil flag in PTE or suit mismatch
   return TRUE if suit match, physpg = physical page
                or page=0 -> last+1 page
*/
static t_bool cpu_vma_ptevl(uint32 pagid,uint32* physpg)
{
uint32 suit;
uint32 pteidx = pagid & 0001777;                    /* build index */
uint32 reqst  = pagid & SUITMASK;                   /* required suit */
uint32 pteval = ReadW(page31 | pteidx);             /* get PTE entry */
*physpg = pteval & 0001777;                         /* store physical page number */
suit = pteval & SUITMASK;                           /* suit number seen */
if (pteval == NILPAGE) return FALSE;                /* NIL value in PTE */
return suit == reqst || !*physpg;                   /* good page or last+1 */
}

/* handle page fault */
static t_stat cpu_vma_fault(uint32 x,uint32 y,int32 mapr,
            uint32 ptepg,uint32 ptr,uint32 faultpc, t_bool debug)
{
uint32 pre = ReadIO(xi,UMAP);                       /* get program preamble */
uint32 ema = ReadIO(pre+2,UMAP);                    /* get address of $EMA$/$VMA$ */
WriteIO(ema,faultpc,UMAP);                          /* write addr of fault instr */
XR = x;                                             /* X = faulting page */
YR = y;                                             /* Y = faulting address for page */

if (mapr>0)
    dms_wmap(mapr+UMAP,ptepg);                      /* map PTE into specified user dmsmap */

/* do a safety check: first instr of $EMA$/$VMA$ must be a DST instr */
if (ReadIO(ema+1,UMAP) != 0104400) {
    if (debug)
        fprintf(sim_deb, ">>CPU VMA: pg fault: no EMA/VMA user code present\n");
    if (mp_control) MP_ABORT (ema+1);               /* allow an MP abort */
    return STOP_HALT;                               /* FATAL: no EMA/VMA! */
    }

PR = (ema+1) & VAMASK;                              /* restart $EMA$ user code, */
                                                    /* will return to fault instruction */

AR = (ptr >> 16) & DMASK;                           /* restore A, B */
BR = ptr & DMASK;
E = 0;                                              /* enforce E = 0 */
if (debug)
    fprintf(sim_deb,
            ">>CPU VMA: Call pg fault OS exit, AR=%06o BR=%06o P=%06o\n",
            AR, BR, PR);
return SCPE_OK;
}

/* map in PTE into last page, return false, if page fault */
static t_bool cpu_vma_mapte(uint32* ptepg)
{
uint32 idext,idext2;
uint32 dispatch = ReadIO(vswp,UMAP) & 01777;        /* get fresh dispatch flag */
t_bool swapflag = TRUE;

if (dispatch == 0) {                                /* not yet set */
    idext = ReadIO(idx,UMAP);                       /* go into ID segment extent */
    if (idext == 0) {                               /* is ema/vma program? */
        swapflag = FALSE;                           /* no, so mark PTE as invalid */
        *ptepg = (uint32) -1;                       /*   and return an invalid page number */
        }

    else {                                          /* is an EMA/VMA program */
        dispatch = ReadWA(idext+1) & 01777;         /* get 1st ema page: new vswp */
        WriteIO(vswp,dispatch,UMAP);                /* move into $VSWP */
        idext2 = ReadWA(idext+2);                   /* get swap bit */
        swapflag = (idext2 & 020000) != 0;          /* bit 13 = swap bit */
        }
    }

if (dispatch) {                                     /* some page is defined */
    dms_wmap(31 + UMAP,dispatch);                   /* map $VSWP to register 31 */
    *ptepg = dispatch;                              /* return PTEPG# for later */
    }

return swapflag;                                    /* true for valid PTE */
}

/*  .LBP
    ASSEMBLER CALLING SEQUENCE:

    DLD PONTR       TRANSLATE 32 BIT POINTER TO 15
    JSB .LBP        BIT POINTER.
    <RETURN - B = LOGICAL ADDRESS, A = PAGID>

    32 bit pointer:
    ----------AR------------ -----BR-----
    15 14....10 9....4 3...0 15.10 9....0
    L<----------------------------------- L=1 local reference bit
       XXXXXXXX<------------------------- 5 bit unused
                PPPPPP PPPPP PPPPP<------ 16 bit PAGEID
                SSSSSS<------------------ SUIT# within PAGEID
                       PPPPP PPPPP<------ 10 bit PAGEID index into PTE
                                   OOOOOO 10 bit OFFSET
*/

static t_stat cpu_vma_lbp(uint32 ptr,uint32 aoffset,uint32 faultpc,uint32 intrq,t_bool debug)
{
uint32 pagid,offset,ptrl,pgidx,ptepg;
uint16 p30,p31,suit;
t_stat reason = SCPE_OK;
uint32 faultab = ptr;                               /* remember A,B for page fault */
ptr += aoffset;                                     /* add the offset e.g. for .LPX */

if (debug)
    fprintf(sim_deb,">>CPU VMA: cpu_vma_lbp: ptr=%o/%o\n",
            (ptr>>16) & DMASK,ptr & DMASK);

O = 0;                                              /* clear overflow */
if (ptr & 0x80000000) {                             /* is it a local reference? */
    ptrl = ptr & VAMASK;
    if (ptr&I_IA) {
        reason = vma_resolve (ReadW (ptrl), &ptrl, debug);
        if (reason)
            return reason;                          /* yes, resolve indirect ref */
        }
    BR = ptrl & VAMASK;                             /* address is local */
    AR = (ptr >> 16) & DMASK;
    if (debug)
        fprintf(sim_deb,">>CPU VMA: cpu_vma_lbp: local ref AR=%06o BR=%06o\n",AR,BR);
    return SCPE_OK;
    }

pagid  = (ptr >> 10) & DMASK;                       /* extract page id (16 bit idx, incl suit*/
offset = ptr & 01777;                               /* and offset */
suit   = pagid & SUITMASK;                          /* suit of page */
pgidx  = pagid & PAGEIDX;                           /* index into PTE */

if (!cpu_vma_mapte(&ptepg))                         /* map in PTE */
    return cpu_vma_fault(65535,ptemiss,-1,ptepg,faultab,faultpc, debug); /* oops, must init PTE */

/* ok, we have the PTE mapped to page31 */
/* the microcode tries to reads two consecutive data pages into page30 and page31 */

/* read the 1st page value from PTE */
p30 = ReadW(page31 | pgidx) ^ suit;
if (!p30)                                           /* matched suit for 1st page */
    return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc,debug);

/* suit switch situation: 1st page is in last idx of PTE, then following page
 * must be in idx 0 of PTE */
if (pgidx==01777) {                                 /* suit switch situation */
    pgidx = 0;                                      /* select correct idx 0 */
    suit = (uint16) (pagid + 1);                    /* suit needs increment */
    if (suit==0) {                                  /* is it page 65536? */
        offset += 02000;                            /* adjust to 2nd page */
        suit = NILPAGE;
        pgidx = 01777;
        }
} else
    pgidx++;                                        /* select next page */

p31 = ReadW(page31 | pgidx) ^ suit;
if (!p31) {                                         /* matched suit for 2nd page */
    dms_wmap(31+UMAP,p30);
    if (p30 & SUITMASK)
        return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc,debug);
    if (!(p31 ^ NILPAGE))                           /* suit is 63: fault */
        return cpu_vma_fault(pagid+1,page31,31,ptepg,faultab,faultpc,debug);

    offset += 02000;                                /* adjust offset to last user map because */
                                                    /* the address requested page 76xxx */
    }
else {
    dms_wmap(30+UMAP,p30);
    if (p30 & SUITMASK)
        return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc,debug);
    dms_wmap(31+UMAP,p31);
    if (p31 & SUITMASK)
        return cpu_vma_fault(pagid+1,page31,31,ptepg,faultab,faultpc,debug);
    }

AR = (uint16) pagid;                                    /* return pagid in A */
BR = (uint16) (page30 + offset);                        /* mapped address in B */
if (debug)
    fprintf(sim_deb,">>CPU VMA: cpu_vma_lbp: map done AR=%06o BR=%o6o\n",AR,BR);
return SCPE_OK;
}

/*  .PMAP
    ASSEMBLER CALLING SEQUENCE:

    LDA UMAPR          (MSEG - 31)
    LDB PAGID          (0-65535)
    JSB .PMAP          GO MAP IT IN
    <ERROR RETURN>     A-REG = REASON, NOTE 1
    <RETURN A=A+1, B=B+1,E=0 >> SEE NOTE 2>

    NOTE 1 : IF BIT 15 OF A-REG SET, THEN ALL NORMAL BRANCHES TO THE
          $EMA$/$VMA$ CODE WILL BE CHANGED TO P+1 EXIT.  THE A-REG
          WILL BE THE REASON THE MAPPING WAS NOT SUCCESSFUL IF BIT 15
          OF THE A-REG WAS NOT SET.
          THIS WAS DONE SO THAT A ROUTINE ($VMA$) CAN DO A MAPPING
          WITHOUT THE POSSIBILITY OF BEING RE-CURRED.  IT IS USED
          BY $VMA$ AND PSTVM IN THE PRIVLEDGED MODE.
    NOTE 2: E-REG WILL = 1 IF THE LAST+1 PAGE IS REQUESTED AND
            MAPPED READ/WRITE PROTECTED ON A GOOD P+2 RETURN.
*/
static t_stat cpu_vma_pmap(uint32 umapr,uint32 pagid, t_bool debug)
{
uint32 physpg, ptr, pgpte;
uint32 mapnm = umapr & 0x7fff;                      /* strip off bit 15 */

if (debug)
    fprintf(sim_deb, ">>CPU VMA: .PMAP AR=%06o(umapr) BR=%06o(pagid)\n",umapr,pagid);

if (mapnm > 31) {                                   /* check for invalid map register */
    AR = 80;                                        /* error: corrupt EMA/VMA system */
    if (debug)
        fprintf(sim_deb, ">>CPU VMA: .PMAP invalid mapr: AR=80, exit P+1\n");
    return SCPE_OK;                                 /* return exit P+1 */
    }

ptr = (umapr << 16) | (pagid & DMASK);              /* build the ptr argument for vma_fault */
if (!cpu_vma_mapte(&pgpte)) {                       /* map the PTE */
    if (umapr & 0x8000) {
        XR = 65535;
        YR = ptemiss;
        if (debug)
            fprintf(sim_deb,
                    ">>CPU VMA: .PMAP pg fault&bit15: XR=%06o YR=%06o, exit P+1\n",
                    XR, YR);
        return SCPE_OK;                             /* use P+1 error exit */
        }
    return cpu_vma_fault(65535,ptemiss,-1,pgpte,ptr,PR-1,debug);  /* oops: fix PTE */
    }

/* PTE is successfully mapped to page31 and dmsmap[63] */
if (!cpu_vma_ptevl(pagid,&physpg)) {
    if (umapr & 0x8000) {
        XR = pagid;
        YR = page31;
        if (debug)
            fprintf(sim_deb,
                    ">>CPU VMA: .PMAP pg map&bit15: XR=%06o YR=%06o, exit P+1\n",
                    XR, YR);
        return SCPE_OK;                             /* use P+1 error exit*/
        }
    return cpu_vma_fault(pagid,page31,31,pgpte,ptr,PR-1,debug);   /* page not present */
    }

E = 1;
if (physpg == 0)                                    /* last+1 page ? */
    physpg = RWPROT;                                /* yes, use page 1023 RW/Protected */
else E = 0;                                         /* normal page to map */

dms_wmap(mapnm+UMAP,physpg);                        /* map page to user page reg */
if (mapnm != 31)                                    /* unless already unmapped, */
    dms_wmap(31+UMAP,RWPROT);                       /* unmap PTE */

AR = (umapr + 1) & DMASK;                           /* increment mapr for next call */
BR = (pagid + 1) & DMASK;                           /* increment pagid for next call */
O = 0;                                              /* clear overflow */
PR = (PR + 1) & VAMASK;                             /* normal P+2 return */
if (debug)
    fprintf(sim_deb,">>CPU VMA: .PMAP map done: AR=%06o BR=%o6o exit P+2\n",AR,BR);
return SCPE_OK;
}

/* array calc helper for .imar, .jmar, .imap, .jmap
   ij=in_s: 16 bit descriptors
   ij=in_d: 32 bit descriptors

   This helper expects mainly the following arguments:
   dtbl: pointer to an array descriptor table
   atbl: pointer to the table of actual subscripts

   where subscript table is the following:
   atbl-> DEF last_subscript,I      (point to single or double integer)
          ...
          DEF first subscript,I     (point to single or double integer)

   where Descriptor_table is the following table:
   dtbl-> DEC #dimensions
          DEC/DIN next-to-last dimension    (single or double integer)
          ...
          DEC/DIN first dimension           (single or double integer)
          DEC elementsize in words
          DEC high,low offset from start of EMA to element(0,0...0)

    Note that subscripts are counting from 0
*/
static t_stat cpu_vma_ijmar(OPSIZE ij,uint32 dtbl,uint32 atbl,uint32* dimret,
                            uint32 intrq,t_bool debug)
{
t_stat reason = SCPE_OK;
uint32 ndim,MA,i,ws;
int32 accu,ax,dx;
OP din;
int opsz = ij==in_d ? 2 : 1;

ndim = ReadW(dtbl++);                               /* get #dimensions itself */
if (debug) {
    fprintf(sim_deb, ">>CPU VMA array calc #dim=%d, size=%d\n",ndim,opsz);
    fprintf(sim_deb, ">>CPU VMA: array actual subscripts (");
    for (i=0; i<ndim; i++) {
        MA = ReadW(atbl+i);
        if (resolve (MA, &MA, intrq)) break;
        din = ReadOp(MA,ij);
        if (i>0) fputc(',',sim_deb);
        fprintf(sim_deb,"%d",ij==in_d?INT32(din.dword) : INT16(din.word));
        }

    fprintf(sim_deb,")\n>>CPU VMA: array descriptor table (");
    if (ndim) {
        for (i=0; i<ndim-1; i++) {
            din = ReadOp(dtbl+i*opsz,ij);
            if (i>0) fputc(',',sim_deb);
            fprintf(sim_deb,"%d",ij==in_d?INT32(din.dword) : INT16(din.word));
            }
        i = dtbl+1+(ndim-1)*opsz;
        ws = ReadW(i-1);
        }
    else {
        i = dtbl;
        ws = 1;
        }
    fprintf(sim_deb,")\n>>CPU VMA: array elemsz=%d base=%o/%o\n",
            ws,ReadW(i),ReadW(i+1));
    }

if (dimret) *dimret = ndim;                         /* return dimensions */
if (ndim == 0) {                                    /* no dimensions:  */
    AR = ReadW(dtbl++);                             /* return the array base itself */
    BR = ReadW(dtbl);
    if (debug)
        fprintf(sim_deb,">>CPU VMA: #dim=0, AR=%06o, BR=%06o\n",AR,BR);
    return SCPE_OK;
    }

/* calculate
 *  (...(An*Dn-1)+An-1)*Dn-2)+An-2....)+A2)*D1)+A1)*#words + Array base
 * Depending on ij, Ax and Dx can be 16 or 32 bit
 */
accu = 0;
while (ndim-- > 0) {
    MA = ReadW(atbl++);                             /* get addr of subscript */
    reason = resolve (MA, &MA, intrq);              /* and resolve it */
    if (reason)
        return reason;
    din = ReadOp(MA,ij);                            /* get actual subscript value */
    ax = ij==in_d ? INT32(din.dword) : INT16(din.word);
    accu += ax;                                     /* add to accu */

    if (ndim==0) ij = in_s;                         /* #words is single */
    din = ReadOp(dtbl,ij);                          /* get dimension from descriptor table */
    if (ij==in_d) {
        dx = INT32(din.dword);                      /* either get double or single dimension */
        dtbl += 2;
    } else {
        dx = INT16(din.word);
        dtbl++;
        }
    accu *= dx;                                     /* multiply */
    }

din = ReadOp(dtbl,in_d);                            /* add base address */
accu += din.dword;

AR = (accu >> 16) & DMASK;                          /* transfer to AB */
BR = accu & DMASK;
if (debug)
    fprintf(sim_deb,">>CPU VMA: resulting virt addr=%o (AR=%06o, BR=%06o)\n",accu,AR,BR);
return reason;
}

/*
 * This is the main handler for the RTE6/VMA microcodes */
t_stat cpu_rte_vma (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
uint16 t16;
uint32 entry,t32,ndim;
uint32 dtbl,atbl;                                   /* descriptor table ptr, actual args ptr */
OP dop0,dop1;
uint32 pcsave = (PR+1) & VAMASK;                    /* save P to check for redo in imap/jmap */
t_bool debug = DEBUG_PRI (cpu_dev, DEB_VMA);

entry = IR & 017;                                   /* mask to entry point */
pattern = op_vma[entry];                            /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op, intrq);              /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

if (debug) {                                            /* debugging? */
    fprintf (sim_deb, ">>CPU VMA: IR = %06o (", IR);    /* print preamble and IR */
    fprint_sym (sim_deb, err_PC, (t_value *) &IR,       /* print instruction mnemonic */
                NULL, SWMASK('M'));
    fprintf (sim_deb, "), P = %06o, XEQT = %06o",       /* print location and program ID */
             err_PC, ReadW (xeqt));

    fprint_ops (pattern, op);                           /* print operands */
    fputc ('\n', sim_deb);                              /* terminate line */
    }

switch (entry) {                                    /* decode IR<3:0> */

    case 000:                                       /* .PMAP 105240 (OP_N) */
        reason = cpu_vma_pmap(AR,BR,debug);         /* map pages */
        break;

    case 001:                                       /* $LOC  105241 (OP_CCCACC) */
        reason = cpu_vma_loc(op,intrq,debug);       /* handle the coroutine switch */
        break;

    case 002:                                       /* [test] 105242 (OP_N) */
        XR = 3;                                     /* refer to src code 92084-18828 rev 3 */
        SR = 0102077;                               /* HLT 77 instruction */
        YR = 1;                                     /* ROMs correctly installed */
        PR = (PR+1) & VAMASK;                       /* skip instr if VMA/EMA ROM installed */
        break;

    case 003:                                       /* [swap] 105243 (OP_N) */
        t16 = AR;                                   /* swap A and B registers */
        AR = BR;
        BR = t16;
        break;

    case 004:                                       /* [---] 105244 (OP_N) */
        reason = stop_inst;                         /* fragment of dead code */
        break;                                      /* in microrom */

    case 005:                                       /* [---] 105245 (OP_N) */
        reason = stop_inst;                         /* fragment of dead code */
        break;                                      /* in microrom */

    case 006:                                       /* [nop] 105246 (OP_N) */
        break;                                      /* do nothing */

    case 007:                                       /* [umpy] 105247 (OP_K) */
        t32 = AR * op[0].word;                      /* get multiplier */
        t32 += BR;                                  /* add B */
        AR = (t32 >> 16) & DMASK;                   /* move result back to AB */
        BR = t32 & DMASK;
        O = 0;                                      /* instr clears OV */
        break;

    case 010:                                       /* .IMAP 105250 (OP_A) */
        dtbl = op[0].word;
        atbl = PR;
        reason = cpu_vma_ijmar(in_s,dtbl,atbl,&ndim,intrq,debug);   /* calc the virt address to AB */
        if (reason)
            return reason;
        t32 = (AR << 16) | (BR & DMASK);
        reason = cpu_vma_lbp(t32,0,PR-2,intrq,debug);
        if (reason)
            return reason;
        if (PR==pcsave)
            PR = (PR+ndim) & VAMASK;                /* adjust P: skip ndim subscript words */
        break;

    case 011:                                       /* .IMAR 105251 (OP_A) */
        dtbl = ReadW(op[0].word);
        atbl = (op[0].word+1) & VAMASK;
        reason = cpu_vma_ijmar(in_s,dtbl,atbl,0,intrq,debug); /* calc the virt address to AB */
    break;

    case 012:                                       /* .JMAP 105252 (OP_A) */
        dtbl = op[0].word;
        atbl = PR;
        reason = cpu_vma_ijmar(in_d,dtbl,atbl,&ndim,intrq,debug);   /* calc the virtual address to AB */
        if (reason)
            return reason;
        t32 = (AR << 16) | (BR & DMASK);
        reason = cpu_vma_lbp(t32,0,PR-2,intrq,debug);
        if (reason)
            return reason;
        if (PR==pcsave)
            PR = (PR + ndim) & VAMASK;              /* adjust P: skip ndim subscript dword ptr */
        break;

    case 013:                                       /* .JMAR 105253 (OP_A) */
        dtbl = ReadW(op[0].word);
        atbl = (op[0].word+1) & VAMASK;
        reason = cpu_vma_ijmar(in_d,dtbl,atbl,0,intrq,debug); /* calc the virt address to AB */
        break;

    case 014:                                       /* .LPXR 105254 (OP_AA) */
        dop0 = ReadOp(op[0].word,in_d);             /* get pointer from arg */
        dop1 = ReadOp(op[1].word,in_d);
        t32 = dop0.dword + dop1.dword;              /* add offset to it */
        reason = cpu_vma_lbp(t32,0,PR-3,intrq,debug);
        break;

    case 015:                                       /* .LPX  105255 (OP_A) */
        t32 = (AR << 16) | (BR & DMASK);            /* pointer in AB */
        dop0 = ReadOp(op[0].word,in_d);
        reason = cpu_vma_lbp(t32,dop0.dword,PR-2,intrq,debug);
        break;

    case 016:                                       /* .LBPR 105256 (OP_A) */
        dop0 = ReadOp(op[0].word,in_d);             /* get the pointer */
        reason = cpu_vma_lbp(dop0.dword,0,PR-2,intrq,debug);
        break;

    case 017:                                       /* .LBP  105257 (OP_N) */
        t32 = (AR << 16) | (BR & DMASK);
        reason = cpu_vma_lbp(t32,0,PR-1,intrq,debug);
        break;
    }

return reason;
}


/* RTE-IV Extended Memory Area Instructions

   The RTE-IV operating system (HP product number 92067A) introduced the
   Extended Memory Area (EMA) instructions.  EMA provided a mappable data area
   up to one megaword in size.  These three instructions accelerated data
   accesses to variables stored in EMA partitions.  Support was limited to
   E/F-Series machines; M-Series machines used software equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92067A  92067A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     .EMIO    105240   EMA I/O
     MMAP     105241   Map physical to logical memory
     [test]   105242   [self test]
     .EMAP    105257   Resolve array element address

   Notes:

     1. RTE-IV EMA and RTE-6 VMA instructions share the same address space, so a
        given machine can run one or the other, but not both.

     2. The EMA diagnostic (92067-16013) reports bogus MMAP failures if it is
        not loaded at the start of its partition (e.g., because of a LOADR "LO"
        command).  The "ICMPS" map comparison check in the diagnostic assumes
        that the starting page of the program's partition contains the first
        instruction of the program and prints "MMAP ERROR" if it does not.

   Additional references:
    - RTE-IVB Programmer's Reference Manual (92068-90004, Dec-1983).
    - RTE-IVB Technical Specifications (92068-90013, Jan-1980).
*/

static const OP_PAT op_ema[16] = {
  OP_AKA,  OP_AKK,    OP_N,    OP_N,                    /* .EMIO  MMAP   [test]  ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_AAA                   /*  ---    ---    ---   .EMAP  */
  };

/* calculate the 32 bit EMA subscript for an array */
static t_bool cpu_ema_resolve(uint32 dtbl,uint32 atbl,uint32* sum)
{
int32 sub, act, low, sz, ndim;
uint32 MA, base;

ndim = ReadW(dtbl++);                                   /* # dimensions */
ndim = SEXT(ndim);                                      /* sign extend */
if (ndim < 0) return FALSE;                             /* invalid? */

*sum = 0;                                               /* accu for index calc */
while (ndim > 0) {
    MA = ReadW(atbl++);                                 /* get address of A(N) */
    resolve (MA, &MA, 0);
    act = ReadW(MA);                                    /* A(N) */
    low = ReadW(dtbl++);                                /* -L(N) */
    sub = SEXT(act) + SEXT(low);                        /* subscript */
    if (sub & 0xffff8000) return FALSE;                 /* overflow? */
    *sum += sub;                                        /* accumulate */
    sz = ReadW(dtbl++);
    sz = SEXT(sz);
    if (sz < 0) return FALSE;
    *sum *= sz;
    if (*sum > (512*1024)) return FALSE;                /* overflow? */
    ndim--;
}
base = (ReadW(dtbl+1)<<16) | (ReadW(dtbl) & 0xffff);    /* base of array in EMA */
if (base & 0x8000000) return FALSE;
*sum += base;                                           /* calculate address into EMA */
if (*sum & 0xf8000000) return FALSE;                    /* overflow? */
return TRUE;
}

/* implementation of VIS RTE-IVB EMA support
 * .ERES microcode routine, resolves only EMA addresses
 *  Call:
 *    .OCT 101474B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF DUMMY        dummy argument for compatibility with .EMAP
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="20", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */
t_stat cpu_ema_eres(uint32 *rtn,uint32 dtbl,uint32 atbl,t_bool debug)
{
uint32 sum;
if (cpu_ema_resolve(dtbl,atbl,&sum)) {                  /* calculate subscript */
    AR = sum & 0xffff;
    BR = sum >> 16;
    if (!(BR & SIGN)) {                                 /* no overflow? */
        (*rtn)++;                                       /* return via good exit */
        return SCPE_OK;
    }
}
AR = 0x3230;                                            /* error condition: */
BR = 0x454d;                                            /* AR = '20', BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}

/* implementation of VIS RTE-IVB EMA support
 * .ESEG microcode routine
 *  Call:
 *    LDA FIRST        first map to set
 *    LDB N            # of maps to set
 *    .OCT 101475B/105475B
 *    DEF RTN          ptr to return
 *    DEF TABLE        map table
 *    RTN EQU *        error return A="21", B="EM"
 *    RTN+1 EQU *+1    good return B=logical address
 *
 * load maps FIRST to FIRST+N from TABLE, with FIRST = FIRST + LOG_START MSEG
 * update map table in base page. Set LOG_START MSEG=0 if opcode==105475
 */
t_stat cpu_ema_eseg(uint32* rtn, uint32 IR, uint32 tbl, t_bool debug)
{
uint32 xidex,eqt,idext0,idext1;
uint32 msegsz,phys,msegn,last,emasz,pg0,pg1,pg,i,lp;

if ((BR & SIGN) || BR==0) goto em21;                    /* #maps not positive? */
xidex = ReadIO(idx,UMAP);                               /* read ID extension */
if (xidex==0) goto em21;
idext0 = ReadWA(xidex+0);                               /* get 1st word idext */
msegsz = idext0 & 037;                                  /* S7 MSEG size */
WriteIO(xidex+0, idext0 | 0100000, SMAP);               /* enforce nonstd MSEG */
idext1 = ReadWA(xidex+1);                               /* get 2nd word idext */
phys = idext1 & 01777;                                  /* S5 phys start of EMA */
msegn = (idext1 >> 11) & 037;                           /* S9 get logical start MSEG# */
if (IR & 04000) {                                       /* opcode == 105475? (.VPRG) */
    msegn = 0;                                          /* log start = 0 */
    msegsz = 32;                                        /* size = full range */
}
last = AR-1 + BR;                                       /* last page */
if (last > msegsz) goto em21;                           /* too many? error */
eqt = ReadIO(xeqt,UMAP);
emasz = (ReadWA(eqt+28) & 01777) - 1;                   /* S6 EMA size in pages */

/* locations 1740...1777 of user base page contain the map entries we need.
 * They are normally hidden by BP fence, therefore they have to be accessed by
 * another fence-less map register. uCode uses #1 temporarily */
pg0 = dms_rmap(UMAP+0);                                 /* read map #0 */
pg1 = dms_rmap(UMAP+1);                                 /* save map #1 */
dms_wmap(UMAP+1,pg0);                                   /* copy #0 into reg #1 */
lp = AR + msegn;                                        /* first */
for (i=0; i<BR; i++) {                                  /* loop over N entries */
    pg = ReadW(tbl++);                                  /* get value from table */
    if ((pg & SIGN) || pg > emasz) pg |= 0140000;       /* write protect if outside */
    pg += phys;                                         /* adjust into EMA page range */
    WriteIO(umaps+lp+i, pg, UMAP);                      /* copy pg to user map */
/* printf("MAP val %oB to reg %d (addr=%oB)\n",pg,lp+i,umaps+lp+i); */
    dms_wmap(UMAP+lp+i, pg);                            /* set DMS reg */
}
dms_wmap(UMAP+1,pg1);                                   /* restore map #1 */
O = 0;                                                  /* clear overflow */
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em21:
AR = 0x3231;                                            /* error condition: */
BR = 0x454d;                                            /* AR = '21', BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}

/* implementation of VIS RTE-IVB EMA support
 * .VSET microcode routine
 *  Call:
 *    .OCT 101476B
 *    DEF RTN          return address
 *    DEF VIN          input vector
 *    DEF VOUT         output vector
 *    DEF MAPS
 *    OCT #SCALARS
 *    OCT #VECTORS
 *    OCT K            1024/(#words/element)
 *    RTN EQU *        error return  (B,A) = "VI22"
 *    RTN+1 EQU *+1    hard return, A = K/IMAX
 *    RTN+2 EQU *+2    easy return, A = 0, B = 2* #VCTRS
 */
t_stat cpu_ema_vset(uint32* rtn, OPS op, t_bool debug)
{
uint32 vin     = op[0].word;                            /* S1 */
uint32 vout    = op[1].word;                            /* S2 */
uint32 maps    = op[2].word;                            /* S3 */
uint32 scalars = op[3].word;                            /* S4 */
uint32 vectors = op[4].word;                            /* S5 */
uint32 k       = op[5].word;                            /* S6 */
uint32 imax    = 0;                                     /* imax S11*/
uint32 xidex, idext1, mseg, addr, i, MA;
t_bool negflag = FALSE;

for (i=0; i<scalars; i++) {                             /* copy scalars */
    XR = ReadW(vin++);
    WriteW(vout++, XR);
}
xidex = ReadIO(idx,UMAP);                               /* get ID extension */
if (xidex==0) goto vi22;                                /* NO EMA? error */
idext1 = ReadWA(xidex+1);
mseg = (idext1 >> 1) & MSEGMASK;                        /* S9 get logical start MSEG */

for (i=0; i<vectors; i++) {                             /* copy vector addresses */
    MA = ReadW(vin++);
    resolve (MA, &MA, 0);
    addr = ReadW(MA) & 0177777;                         /* LSB */
    addr |= (ReadW(MA+1)<<16);                          /* MSB, build address */
    WriteW(vout++, mseg + (addr & 01777));              /* build and write log addr of vector */
    addr = (addr >> 10) & 0xffff;                       /* get page */
    WriteW(maps++, addr);                               /* save page# */
    WriteW(maps++, addr+1);                             /* save next page# as well */
    MA = ReadW(vin++);                                  /* get index into Y */
    resolve(MA, &MA, 0);
    YR = ReadW(MA);                                     /* get index value */
    WriteW(vout++, MA);                                 /* copy address of index */
    if (YR & SIGN) {                                    /* index is negative */
         negflag = TRUE;                                /* mark a negative index (HARD) */
         YR = (~YR + 1) & DMASK;                        /* make index positive */
    }
    if (imax < YR) imax = YR;                           /* set maximum index */
    mseg += 04000;                                      /* incr mseg address by 2 more pages */
}
MA = ReadW(vin);                                        /* get N index into Y */
resolve(MA, &MA, 0);
YR = ReadW(MA);
WriteW(vout++, MA); vin++;                              /* copy address of N */

if (imax==0) goto easy;                                 /* easy case */
AR = (uint16) (k / imax); AR++;                         /* calculate K/IMAX */
if (negflag) goto hard;                                 /* had a negative index? */
if (YR > AR) goto hard;

easy:
(*rtn)++;                                               /* return via exit 2 */
AR = 0;

hard:
(*rtn)++;                                               /* return via exit 1 */
BR = 2 * op[4].word;                                    /* B = 2* vectors */
return SCPE_OK;

vi22:                                                   /* error condition */
  AR=0x3232;                                            /* AR = '22' */
  BR=0x5649;                                            /* BR = 'VI' */
  return SCPE_OK;                                       /* return via unmodified e->rtn */
}

typedef struct ema4 {
    uint32 mseg;                                         /* logical start of MSEG */
    uint32 msegsz;                                       /* size of std mseg in pgs */
    uint32 pgoff;                                        /* pg # in EMA containing element */
    uint32 offs;                                         /* offset into page of element */
    uint32 msoff;                                        /* total offset to element in MSEG */
    uint32 emasz;                                        /* size of ema in pgs */
    uint32 msegno;                                       /* # of std mseg */
    uint32 ipgs;                                         /* # of pgs to start of MSEG */
    uint32 npgs;                                         /* # of pgs needed */
    uint32 spmseg;                                       /* first phys pg of MSEG */
} EMA4;

static t_bool cpu_ema_emas(uint32 dtbl,uint32 atbl,EMA4* e)
{
uint32 xidex, eqt;
uint32 sum, msegsz,pgoff,offs,emasz,msegno,msoff,ipgs;

if (!cpu_ema_resolve(dtbl,atbl,&sum)) return FALSE;     /* calculate 32 bit index */

xidex = ReadIO(idx,UMAP);                               /* read ID extension */
msegsz = ReadWA(xidex+0) & 037;                         /* S5 # pgs for std MSEG */
pgoff = sum >> 10;                                      /* S2 page containing element */
offs = sum & 01777;                                     /* S6 offset in page to element */
if (pgoff > 1023) return FALSE;                         /* overflow? */
eqt = ReadIO(xeqt,UMAP);
emasz = ReadWA(eqt+28) & 01777;                         /* S EMA size in pages */
if (pgoff > emasz) return FALSE;                        /* outside EMA? */
msegno = pgoff / msegsz;                                /* S4 # of MSEG */
msoff = pgoff % msegsz;                                 /* offset within MSEG in pgs */
ipgs = pgoff - msoff;                                   /* S7 # pgs to start of MSEG */
msoff = msoff << 10;                                    /* offset within MSEG in words */
msoff += offs;                                          /* S1 offset to element in words */

e->msegsz = msegsz;                                     /* return calculated data */
e->pgoff = pgoff;
e->offs = offs;
e->emasz = emasz;
e->msegno = msegno;
e->ipgs = ipgs;
e->msoff = msoff;
return TRUE;
}

static t_bool cpu_ema_mmap01(EMA4* e)
{
uint32 xidex,idext0, pg, pg0, pg1, i;

uint32 base = e->mseg >> 10;                            /* get the # of first MSEG DMS reg */
xidex = ReadIO(idx,UMAP);                               /* get ID extension */
idext0 = ReadWA(xidex+1);

if (e->npgs==0) return FALSE;                           /* no pages to map? */
if ((e->npgs+1+e->ipgs) <= e->emasz) e->npgs++;         /* actually map npgs+1 pgs */

/* locations 1740...1777 of user base page contain the map entries we need.
 * They are normally hidden by BP fence, therefore they have to be accessed by
 * another fence-less map register. uCode uses #1, macro code uses $DVCT (==2)
 */
pg0 = dms_rmap(UMAP+0);                                 /* read base page map# */
pg1 = dms_rmap(UMAP+1);                                 /* save map# 1 */
dms_wmap(UMAP+1,pg0);                                   /* map #0 into reg #1 */
for (i=0; (base+i)<32; i++) {
    pg = i<e->npgs ? e->spmseg : 0140000;               /* write protect if outside */
    WriteIO(umaps+base+i, pg, UMAP);                    /* copy pg to user map */
/* printf("MAP val %d to reg %d (addr=%o)\n",pg,base+i,umaps+base+i); */
    dms_wmap(UMAP+base+i, pg);                          /* set DMS reg */
    e->spmseg++;
}
dms_wmap(UMAP+1,pg1);                                   /* restore map #1 */

xidex = ReadIO(idx,UMAP);                               /* get ID extension */
idext0 = ReadWA(xidex+0);
if (e->msegno == 0xffff)                                /* non std mseg */
    idext0 |= 0x8000;                                   /* set nonstd marker */
else
    idext0 = (idext0 & 037) | (e->msegno<<5);           /* set new current mseg# */
WriteIO(xidex, idext0, SMAP);                           /* save back value */
AR = 0;                                                 /* was successful */
return TRUE;
}

static t_bool cpu_ema_mmap02(EMA4* e)
{
uint32 xidex, eqt, idext1;
uint32 mseg,phys,spmseg,emasz,msegsz,msegno;

xidex = ReadIO(idx,UMAP);                               /* get ID extension */
msegsz = ReadWA(xidex+0) & 037;                         /* P size of std MSEG */
idext1 = ReadWA(xidex+1);
mseg = (idext1 >> 1) & MSEGMASK;                        /* S9 get logical start MSEG */
phys = idext1 & 01777;                                  /* S phys start of EMA */
spmseg = phys + e->ipgs;                                /* S7 phys pg# of MSEG */
msegno = e->ipgs / msegsz;
if ((e->ipgs % msegsz) != 0)                            /* non std MSEG? */
    msegno = 0xffff;                                    /* S4 yes, set marker */
if (e->npgs > msegsz) return FALSE;                     /* map more pages than MSEG sz? */
eqt = ReadIO(xeqt,UMAP);
emasz = ReadWA(eqt+28) & 01777;                         /* B EMA size in pages */
if ((e->ipgs+e->npgs) > emasz) return FALSE;            /* outside EMA? */
if ((e->ipgs+msegsz) > emasz)                           /* if MSEG overlaps end of EMA */
    e->npgs = emasz - e->ipgs;                          /* only map until end of EMA */

e->emasz = emasz;                                       /* copy arguments */
e->msegsz = msegsz;
e->msegno = msegno;
e->spmseg = spmseg;
e->mseg = mseg;
return cpu_ema_mmap01(e);
}

static t_stat cpu_ema_mmap(uint32 ipage,uint32 npgs, t_bool debug)
{
uint32 xidex;
EMA4 ema4, *e = &ema4;

e->ipgs = ipage;                                        /* S6 set the arguments */
e->npgs = npgs;                                         /* S5 */

AR = 0;
xidex = ReadIO(idx,UMAP);
if ((ipage & SIGN) ||                                   /* negative page displacement? */
    (npgs & SIGN) ||                                    /* negative # of pages? */
    xidex == 0 ||                                       /* no EMA? */
    !cpu_ema_mmap02(e))                                 /* mapping failed? */
    AR = 0177777;                                       /* return with error */
return SCPE_OK;                                         /* leave */
}

static t_bool cpu_ema_emat(EMA4* e)
{
uint32 xidex,idext0;
uint32 curmseg,phys,msnum,lastpgs;

xidex = ReadIO(idx,UMAP);                               /* read ID extension */
idext0 = ReadWA(xidex+0);                               /* get current segment */
curmseg = idext0 >> 5;
if ((idext0 & 0100000) ||                               /* was nonstd MSEG? */
    curmseg != e->msegno) {                             /* or different MSEG last time? */
    phys = ReadWA(xidex+1) & 01777;                     /* physical start pg of EMA */
    e->spmseg = phys + e->ipgs;                         /* physical start pg of MSEG */
    msnum = e->emasz / e->msegsz;                       /* find last MSEG# */
    lastpgs = e->emasz % e->msegsz;                     /* #pgs in last MSEG */
    if (lastpgs==0) msnum--;                            /* adjust # of last MSEG */
    e->npgs = msnum==e->msegno ? lastpgs : e->msegsz;   /* for last MSEG, only map available pgs */
    if (!cpu_ema_mmap01(e)) return FALSE;               /* map npgs pages at ipgs */
}
BR = (uint16) (e->mseg + e->msoff);                     /* return address of element */
return TRUE;                                            /* and everything done */
}

/* .EMIO microcode routine, resolves element addr for EMA array
 * and maps the appropriate map segment
 *
 *  Call:
 *    OCT 105250B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF BUFLEN       length of buffer in words (bufl)
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="15", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */
static t_stat cpu_ema_emio(uint32* rtn,uint32 bufl,uint32 dtbl,uint32 atbl,t_bool debug)
{
uint32 xidex, idext1;
uint32 mseg, bufpgs, npgs;
EMA4 ema4, *e = &ema4;

xidex = ReadIO(idx,UMAP);                               /* read ID extension */
if (bufl & SIGN ||                                      /* buffer length negative? */
    xidex==0) goto em16;                                /* no EMA declared? */

idext1 = ReadWA(xidex+1);                               /* |logstrt mseg|d|physstrt ema| */
mseg = (idext1 >> 1) & MSEGMASK;                        /* get logical start MSEG */
if (!cpu_ema_emas(dtbl,atbl,e)) goto em16;              /* resolve address */
bufpgs = (bufl + e->offs) >> 10;                        /* # of pgs reqd for buffer */
if ((bufl + e->offs) & 01777) bufpgs++;                 /* S11 add 1 if not at pg boundary */
if ((bufpgs + e->pgoff) > e->emasz) goto em16;          /* exceeds EMA limit? */
npgs = (e->msoff + bufl) >> 10;                         /* # of pgs reqd for MSEG */
if ((e->msoff + bufl) & 01777) npgs++;                  /* add 1 if not at pg boundary */
if (npgs < e->msegsz) {
    e->mseg = mseg;                                     /* logical stat of MSEG */
    if (!cpu_ema_emat(e)) goto em16;                    /* do a std mapping */
} else {
    BR = (uint16) (mseg + e->offs);                     /* logical start of buffer */
    e->npgs = bufpgs;                                   /* S5 # pgs required */
    e->ipgs = e->pgoff;                                 /* S6 page offset to reqd pg */
    if (!cpu_ema_mmap02(e)) goto em16;                  /* do nonstd mapping */
}
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em16:                                                   /* error condition */
AR=0x3136;                                              /* AR = '16' */
BR=0x454d;                                              /* BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}

/* .EMAP microcode routine, resolves both EMA/non-EMA calls
 *  Call:
 *    OCT 105257B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF ARRAY[,I]    array base (abase)
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="15", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */
static t_stat cpu_ema_emap(uint32* rtn,uint32 abase,uint32 dtbl,uint32 atbl,t_bool debug)
{
uint32 xidex, eqt, idext0, idext1;
int32 sub, act, low, ndim, sz;
uint32 offs, pgoff, emasz, phys, msgn, mseg, sum, MA, pg0, pg1;

xidex = ReadIO(idx,UMAP);                               /* read ID Extension */
if (xidex) {                                            /* is EMA declared? */
    idext1 = ReadWA(xidex+1);                           /* get word 1 of idext */
    mseg = (idext1 >> 1) & MSEGMASK;                    /* get logical start MSEG */
    if (abase >= mseg) {                                /* EMA reference? */
        if (!cpu_ema_resolve(dtbl,atbl,&sum))           /* calculate subscript */
            goto em15;
        offs = sum & 01777;                             /* address offset within page */
        pgoff = sum >> 10;                              /* ema offset in pages */
        if (pgoff > 1023) goto em15;                    /* overflow? */
        eqt = ReadIO(xeqt,UMAP);
        emasz = ReadWA(eqt+28) & 01777;                 /* EMA size in pages */
        phys = idext1 & 01777;                          /* physical start pg of EMA */
        if (pgoff > emasz) goto em15;                   /* outside EMA range? */

        msgn = mseg >> 10;                              /* get # of 1st MSEG reg */
        phys += pgoff;

        pg0 = dms_rmap(UMAP+0);                         /* read base page map# */
        pg1 = dms_rmap(UMAP+1);                         /* save map# 1 */
        dms_wmap(UMAP+1,pg0);                           /* map #0 into reg #1 */

        WriteIO(umaps+msgn, phys, UMAP);                /* store 1st mapped pg in user map */
        dms_wmap(UMAP+msgn, phys);                      /* and set the map register */
        phys = (pgoff+1)==emasz ? 0140000 : phys+1;     /* protect 2nd map if end of EMA */
        WriteIO(umaps+msgn+1, phys, UMAP);              /* store 2nd mapped pg in user map */
        dms_wmap(UMAP+msgn+1, phys);                    /* and set the map register */

        dms_wmap(UMAP+1,pg1);                           /* restore map #1 */

        idext0 = ReadWA(xidex+0) | 0100000;             /* set NS flag in id extension */
        WriteIO(xidex+0, idext0, SMAP);                 /* save back value */
        AR = 0;                                         /* was successful */
        BR = (uint16) (mseg + offs);                    /* calculate log address */
        (*rtn)++;                                       /* return via good exit */
        return SCPE_OK;
    }
}                                                       /* not EMA reference */
ndim = ReadW(dtbl++);
if (ndim<0) goto em15;                                  /* negative ´dimensions */
sum = 0;                                                /* accu for index calc */
while (ndim > 0) {
    MA = ReadW(atbl++);                                 /* get address of A(N) */
    resolve (MA, &MA, 0);
    act = ReadW(MA);                                    /* A(N) */
    low = ReadW(dtbl++);                                /* -L(N) */
    sub = SEXT(act) + SEXT(low);                        /* subscript */
    if (sub & 0xffff8000) goto em15;                    /* overflow? */
    sum += sub;                                         /* accumulate */
    sz = ReadW(dtbl++);
    sz = SEXT(sz);
    if (sz < 0) goto em15;
    sum *= sz;                                          /* and multiply with sz of dimension */
    if (sum & 0xffff8000) goto em15;                    /* overflow? */
    ndim--;
}
BR = (uint16) (abase + sum);                            /* add displacement */
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em15:                                                   /* error condition */
  AR=0x3135;                                            /* AR = '15' */
  BR=0x454d;                                            /* BR = 'EM' */
  return SCPE_OK;                                       /* return via unmodified e->rtn */
}

t_stat cpu_rte_ema (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
uint32 entry, rtn;
t_bool debug = DEBUG_PRI (cpu_dev, DEB_EMA);

entry = IR & 017;                                       /* mask to entry point */
pattern = op_ema[entry];                                /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op, intrq);              /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

if (debug) {                                            /* debugging? */
    fprintf (sim_deb, ">>CPU EMA: P = %06o, IR = %06o (", err_PC,IR);    /* print preamble and IR */
    fprint_sym (sim_deb, err_PC, (t_value *) &IR,       /* print instruction mnemonic */
                NULL, SWMASK('M'));
    fputc (')', sim_deb);

    fprint_ops (pattern, op);                           /* print operands */
    fputc ('\n', sim_deb);                              /* terminate line */
    }

switch (entry) {                                        /* decode IR<3:0> */
    case 000:                                           /* .EMIO 105240 (OP_A) */
        rtn = op[0].word;
        reason = cpu_ema_emio(&rtn, op[1].word,
                              op[2].word, PR, debug);   /* handle the EMIO instruction */
        PR = rtn;
        if (debug)
            fprintf (sim_deb, ">>CPU EMA: return .EMIO: AR = %06o, BR = %06o, rtn=%s\n",
                     AR, BR, PR==op[0].word?"error":"good");
        break;

    case 001:                                           /* .MMAP  105241 (OP_AKK) */
        reason = cpu_ema_mmap(op[1].word,
                              op[2].word, debug);       /* handle the MMAP instruction */
        if (debug)
            fprintf (sim_deb, ">>CPU EMA: return .MMAP: AR = %06o\n",AR);
        break;

    case 002:                                           /* [test] 105242 (OP_N) */
        /* effectively, this code just returns without error:
         * real microcode will set S register to 102077B when in single step mode */
        if (sim_step==1) {
            if (debug)
                fprintf(sim_deb, ">>CPU EMA: EMA option 92067 correctly installed: S=102077\n");
            SR = 0102077;
        }
        break;

    case 017:                                           /* .EMAP  105247 (OP_A) */
        rtn = op[0].word;                               /* error return */
        reason = cpu_ema_emap(&rtn, op[1].word,
                              op[2].word, PR, debug);   /* handle the EMAP instruction */
        PR = rtn;
        if (debug) {
            fprintf (sim_deb, ">>CPU EMA: return .EMAP: AR = %06o, BR = %06o, rtn=%s\n",
                     AR, BR, PR==op[0].word?"error":"good");
        }
        break;

    default:                                            /* others undefined */
        reason = stop_inst;
    }

return reason;
}
