/* sage_fd.c: Floppy device for sage-II system

   Copyright (c) 2009,2010 Holger Veit

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

static t_stat sagefd_reset(DEVICE* dptr);
static t_stat sagefd_boot(int32 unit_num,DEVICE* dptr);
static void   sagefd_seldrv(I8272* chip,int drvnum);
static void sagefd_interrupt(I8272* chip,int delay);
extern DEVICE sagefd_dev;
static t_stat fdcint_svc(UNIT*);

/* this is the FDC chip */
I8272 u21 = {
        { 0, 0, U21_ADDR, 4, 2 },
        &sagefd_dev,
        NULL, NULL, &i8272_reset, &sagefd_seldrv, &sagefd_interrupt
};

UNIT sagefd_unit[] = {
    { UDATA (&fdcint_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY), 58200 },
    { UDATA (&fdcint_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY), 58200 }
};

REG sagefd_reg[] = {
    { NULL }
};

static MTAB sagefd_mod[] = {
    { MTAB_XTD|MTAB_VDV,   0,                      "IO",       "IO",       &set_iobase, &show_iobase, NULL },
    { UNIT_I8272_WLK,      0,                      "WRTENB",   "WRTENB",   NULL },
    { UNIT_I8272_WLK,      UNIT_I8272_WLK,         "WRTLCK",   "WRTLCK",   NULL },
    { UNIT_I8272_VERBOSE,  0,                      "QUIET",    "QUIET",    NULL },
    { UNIT_I8272_VERBOSE,  UNIT_I8272_VERBOSE,     "VERBOSE",  "VERBOSE",  NULL },
    { 0 }
};

DEVICE sagefd_dev = {
    "FD", sagefd_unit, sagefd_reg, sagefd_mod,
    2, 16, 32, 2, 16, 16,
    NULL, NULL, &sagefd_reset,
    &sagefd_boot, &i8272_attach, &i8272_detach,
    &u21, (DEV_DISABLE|DEV_DEBUG), 0,
    i8272_dt, NULL, NULL
};

static void sagefd_seldrv(I8272* chip,int drvnum)
{
    /* this routine defeats the standard drive select in i8272.c
     * which interprets the US0/US1 bits of various commands.
     * Sage uses 8255 portc bits for that, and always leaves
     * US0/US1 = 0, despite which drive is selected.
     * The actual code to select drives is in sage_stddev.c in u22callc()
     */
    return;
}

static t_stat sagefd_reset(DEVICE* dptr) 
{
    t_stat rc;
    I8272* chip = (I8272*)dptr->ctxt;

    /* fixup device link */
    i8272_dev = dptr;

    rc = (dptr->flags & DEV_DIS) ? /* Disconnect I/O Ports */
        del_iohandler((void*)chip) :
        add_iohandler(&sagefd_unit[0],(void*)chip,i8272_io);
    if (rc != SCPE_OK) return rc;
    return (*chip->reset)(chip);
}

static t_stat fdcint_svc(UNIT* unit)
{
#if DBG_MSG==1
    I8272* chip;
    DEVICE* dptr;
    if (!unit) return -1;
    dptr = find_dev_from_unit(unit);
    if (!dptr) return -1;
    chip = (I8272*)dptr->ctxt;
#endif
    if (*u22_portc & U22C_FDIE) {
        TRACE_PRINT0(DBG_FD_IRQ,"FDCINT_SVC: deliver interrupt");
        m68k_raise_autoint(FDC_AUTOINT);

    } else {
        TRACE_PRINT0(DBG_FD_IRQ,"FDCINT_SVC: int not granted");
    }
    return SCPE_OK;
}


static t_stat sagefd_boot(int32 unit_num,DEVICE* dptr)
{
    printf("sagefd_boot\n");
    return SCPE_OK;
}

static void sagefd_interrupt(I8272* chip,int delay)
{
    TRACE_PRINT0(DBG_FD_IRQ,"SAGEFD_INT: request interrupt");
    sim_activate(&sagefd_unit[0],delay);
}

/* dummy routines for i8272 - sage does not use DMA */
void PutByteDMA(uint32 addr, uint8 data)
{
}

uint8 GetByteDMA(uint32 addr)
{
    return 0;
}
