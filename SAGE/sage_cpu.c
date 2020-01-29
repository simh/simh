/* sage_cpu.c: CPU simulator for sage-II/IV system

   Copyright (c) 20092010 Holger Veit

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

#include "sage_defs.h"

static t_stat sagecpu_reset(DEVICE* dptr);
static t_stat sagecpu_boot(int unit,DEVICE* dptr);
static t_stat sage_translateaddr(t_addr in,t_addr* out, IOHANDLER** ioh,int rw,int fc,int dma);
static t_stat sage_mem(t_addr addr,uint8** mem);
static t_stat sagecpu_set_bios(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat sagecpu_show_bios(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static uint8* ROM = 0;
static int rom_enable = TRUE; /* LS74 U51 in CPU schematic */

extern int32 DR[];
extern t_addr AR[];

#define UNIT_CPU_V_BIOS     UNIT_CPU_V_FREE     /* has custom BIOS */
#define UNIT_CPU_BIOS       (1 << UNIT_CPU_V_BIOS)


#define MAX_ROMSIZE 16384
char* biosfile = NULL;

static MTAB sagecpu_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,      "BIOS", "BIOS",  &sagecpu_set_bios, &sagecpu_show_bios  },
    M68KCPU_STDMOD,
    { 0 }
};
UNIT sagecpu_unit = {
    UDATA (NULL, UNIT_FIX|UNIT_BINK|CPU_TYPE_68000|UNIT_CPU_EXC|UNIT_CPU_STOP|UNIT_CPU_PRVIO, SAGEMEM)
};

#define DBG_CPU_OSCPM   DBG_CPU_CUSTOM1
DEBTAB sagecpu_dt[] = {
    { "EXC",    DBG_CPU_EXC    },
    { "PC",     DBG_CPU_PC     },
    { "INT",    DBG_CPU_INT    },
    { "CTRACE", DBG_CPU_CTRACE },
    { "BTRACE", DBG_CPU_BTRACE },
    { "OSCPM",  DBG_CPU_OSCPM  },
    { NULL,         0      }
};

DEVICE sagecpu_dev = {
    "CPU", &sagecpu_unit, m68kcpu_reg, sagecpu_mod,
    1, 16, 32, 2, 16, 16,
    &m68kcpu_ex, &m68kcpu_dep, &sagecpu_reset,
    &sagecpu_boot, NULL, NULL,
    NULL, DEV_DEBUG, 0,
    sagecpu_dt, NULL, NULL
};

static t_stat sagecpu_set_bios(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    FILE* fp;
    if (cptr==NULL) return SCPE_ARG;
    if ((fp=fopen(cptr,"r"))==0) return SCPE_OPENERR;
    fclose(fp);
    
    biosfile = (char *)realloc(biosfile, strlen(cptr)+1);
    strcpy(biosfile,cptr);

    /* enforce reload of BIOS code on next boot */
    if (ROM != 0) free(ROM);
    ROM = 0;
    return SCPE_OK; 
}

static t_stat sagecpu_show_bios(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "BIOS=%s", biosfile);
    return SCPE_OK; 
}

t_stat sagecpu_boot(int32 unitno,DEVICE* dptr)
{
    t_stat rc;

    if (!ROM) return SCPE_IERR;
    
    if (*ROM==0) {
        printf("Loading boot code from %s\n",biosfile);
        if ((rc = load_cmd(0,biosfile)) != SCPE_OK) return rc;
    }
    return m68kcpu_boot(unitno,dptr);
}

/* special logic: capture essential TRAP 8-14 for debugging */
static void sage_trapcallback(DEVICE* dptr,int trapnum)
{
    if ((dptr->dctrl & DBG_CPU_OSCPM) && sim_deb) {
        if (trapnum>=0x08 && trapnum<=0x0e) {
            fprintf(sim_deb,"SAGE: TRAP #%x: D0=%x A0=%x\n",trapnum,DR[0],AR[0]);
        }
        if (trapnum==2) {
            fprintf(sim_deb,"SAGE: CPM BDOS #%d D1=0x%x D2=0x%x\n",DR[0]&0xff,DR[1],DR[2]);
        }
        if (trapnum==3) {
            fprintf(sim_deb,"SAGE: CPM BIOS #%d D1=0x%x D2=0x%x\n",DR[0]&0xff,DR[1],DR[2]);
        }
    }
}

static t_stat sagecpu_reset(DEVICE* dptr) 
{
    t_stat rc;
    extern void m68k_sim_init(void);


    m68k_sim_init();

    /* set CPU pointers */
    m68kcpu_dev = &sagecpu_dev;
    m68kcpu_unit = &sagecpu_unit;

    /* redefine memory handlers */
    TranslateAddr = &sage_translateaddr;
    Mem = &sage_mem;

    if (!biosfile)
#ifdef SAGE_IV
        sagecpu_set_bios(NULL, 0, "sage-iv.hex", NULL);
#else
        sagecpu_set_bios(NULL, 0, "sage-ii.hex", NULL);
#endif

    if (!ROM) ROM = (uint8*)calloc(MAX_ROMSIZE,1);
    rom_enable = TRUE;

    if ((rc=m68kcpu_reset(dptr)) != SCPE_OK) return rc;
    
    /* redirect callbacks */
    m68kcpu_trapcallback = &sage_trapcallback;

    return SCPE_OK;
}

uint8 ioemul[4] = { 0,0,0,0 };

/* sage memory */
static t_stat sage_mem(t_addr addr,uint8** mem)
{
    t_addr a;
//  printf("Try to access %x\n",addr); fflush(stdout);
    if (rom_enable && (addr < MAX_ROMSIZE)) { /* boot rom mapped to zero page */
        *mem = ROM+addr;
        return SCPE_OK;
    }
    a = addr - 0xfe0000;            /* boot rom at normal ROM page */
    if (a < MAX_ROMSIZE) {
        rom_enable = FALSE;
        *mem = ROM+a;
        return SCPE_OK;
    }
    a = addr - 0xffc0fe;
    if (a < 2) {                    /* boot rom diagnostic address: black hole */
        ioemul[0] = ioemul[1] = 0;
        *mem = ioemul+a;
        return SCPE_OK;
    }
    a = addr - 0xff0000;
    if (a < 0x10000) {
        *mem = ioemul;
        return SCPE_OK;
    }
    if (addr > MEMORYSIZE) return SIM_NOMEM;
    return m68k_mem(addr,mem);
}

t_stat sage_translateaddr(t_addr in,t_addr* out, IOHANDLER** ioh,int rw,int fc,int dma)
{
    static uint32 bptype[] = { R_BKPT_SPC|SWMASK('R'), W_BKPT_SPC|SWMASK('W') };
    t_addr ma = in & addrmask;
    if (sim_brk_summ && sim_brk_test(ma, bptype[rw])) return STOP_IBKPT;
    return m68k_translateaddr(in,out,ioh,rw,fc,dma);
}
