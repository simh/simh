/*
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
   
   20130920 hv initial version, moved some code from pdq3_cpu.c
*/
#include "pdq3_defs.h"

/* the memory */
uint16 M[MAXMEMSIZE];

/******************************************************************************
 * IO dispatcher
 *****************************************************************************/

static t_bool initio = FALSE;
#define IOSIZE 4096
#define IOPAGEMASK 0x0fff
IOREAD ioreaders[IOSIZE];
IOWRITE iowriters[IOSIZE];

/* I/O devices are implemented this way:
 * a unit will register its own I/O addresses together with its handler 
 * in a hash which allows simple lookup of memory mapped I/O addresses
 */
t_stat pdq3_ioinit() {
  int i;
  if (!initio) {
    for (i=0; i < IOSIZE; i++) {
      ioreaders[i] = NULL;
      iowriters[i] = NULL;
    }
    for (i=8; i < 32; i++)
      cpu_setIntVec(NIL, i);
    initio = TRUE;
  }
  return SCPE_OK;
}

t_stat add_ioh(IOINFO* ioi) {
  while (ioi) {
    int i;
    for (i=0; i<ioi->iosize; i++) {
      int idx = (ioi->iobase + i) & IOPAGEMASK;
      ioreaders[idx] = ioi->read;
      iowriters[idx] = ioi->write;
    }
    ioi = ioi->next;
  }
  return SCPE_OK;
}

t_stat del_ioh(IOINFO* ioi) {
  while (ioi) {
    int i;
    for (i=0; i<ioi->iosize; i++) {
      int idx = (ioi->iobase + i) & IOPAGEMASK;
      ioreaders[idx] = NULL;
      iowriters[idx] = NULL;
    }
    ioi = ioi->next;
  }
  return SCPE_OK;
}

/******************************************************************************
 * configuration
 *****************************************************************************/
t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  t_bool first = TRUE;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  while (ioi) {
    if (ioi->iobase) {
      if (ioi->iobase > 0xfc00) {
        fprintf(st, first ? "IOBASE=$%04x":",$%04x", ioi->iobase);
        first = FALSE;
      }
    }
    ioi = ioi->next;
  }
  return SCPE_OK;
}

t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
  t_stat rc;
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  if (!cptr) return SCPE_ARG;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  if (ioi->next)
    return SCPE_ARG; /* note: fixed devices on mainboard cannot be changed */
  ioi->iobase = get_uint(cptr, 16, 0xffff, &rc);
  return rc;
}

t_stat set_iovec(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
  t_stat rc;
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  if (!cptr) return SCPE_ARG;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  if (ioi->next)
    return SCPE_ARG; /* note: fixed devices on mainboard cannot be changed */
  ioi->qvector = get_uint(cptr, 16, 0xff, &rc);
  return rc;
}

t_stat show_iovec(FILE *st, UNIT *uptr, int value, CONST void *desc) {
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  t_bool first = TRUE;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  while (ioi) {
    if (ioi->qprio < 32) {
      fprintf(st, first ? "VECTOR=$%04x":",$%04x", ioi->qvector);
      first = FALSE;
    }
    ioi = ioi->next;
  }
  return SCPE_OK;
}

t_stat set_ioprio(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
  t_stat rc;
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  if (!cptr) return SCPE_ARG;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  if (ioi->next)
    return SCPE_ARG; /* note: fixed devices on mainboard cannot be changed */
  ioi->qprio = get_uint(cptr, 16, 31, &rc);
  return rc;
}

t_stat show_ioprio(FILE *st, UNIT *uptr, int value, CONST void *desc) {
  DEVICE* dptr;
  DEVCTXT* ctxt;
  IOINFO* ioi;
  t_bool first = TRUE;
  if (!uptr) return SCPE_IERR;
  if ((dptr = find_dev_from_unit(uptr)) == 0) return SCPE_IERR;
  ctxt = (DEVCTXT*)dptr->ctxt;
  ioi = ctxt->ioi;
  while (ioi) {
    if (ioi->qprio < 32) {
      fprintf(st, first ? "PRIO=%d":",%d", ioi->qprio);
      first = FALSE;
    }
    ioi = ioi->next;
  }
  return SCPE_OK;
}

/******************************************************************************
 * central memory handling 
 *****************************************************************************/
t_stat Read(t_addr base, t_addr woffset, uint16 *data, uint32 dctrl) {
  t_stat rc;
  uint16 ea = base + woffset;

  /* Note: the PRIAM driver attempts to read the ready bit from FF25 (bit 9) which should be 1.
   * As long as we don't have a HDP device, the invalid value should be 0x0000 */
  *data = 0x0000; /* preload invalid data value */

  if (ea < 0xf000 || (ea == 0xfffe && cpu_unit.capac > 65535)) {
    *data = M[ea]; /* normal memory */
    rc = SCPE_OK;
  } else {
    IOREAD reader = ioreaders[ea & IOPAGEMASK];
    rc = reader ? (*reader)(ea, data) : SCPE_NXM;
  }
  if (rc != SCPE_OK) {
    cpu_buserror();
    sim_debug(DBG_CPU_READ, &cpu_dev, DBG_PCFORMAT1 "Invalid Mem read from $%04x\n", DBG_PC, ea);
    printf("read buserror: ea=$%04x at $%x:#%x\n",ea,reg_segb,reg_ipc);
    return rc;
  }
  if (dctrl & DBG_CPU_PICK) {
    sim_debug(DBG_CPU_PICK, &cpu_dev, DBG_PCFORMAT1 "Pick %04x at SP=$%04x\n", DBG_PC, *data, ea);
  } else if (dctrl & DBG_CPU_POP) {
    sim_debug(DBG_CPU_POP, &cpu_dev, DBG_PCFORMAT2 "Pop %04x from SP=$%04x\n", DBG_PC, *data, ea);
  } else {
    sim_debug(dctrl, &cpu_dev, DBG_PCFORMAT2 "Word read %04x from $%04x\n", DBG_PC, *data, ea);
  }
  return rc;
}

/* read routine that does not generate bus errors, for SIMH Examine
 * will read 0x0000 for unknown memory */
t_stat ReadEx(t_addr base, t_addr woffset, uint16 *data) {
  t_stat rc;
  uint16 ea = base + woffset;
  *data = 0x0000; /* preload invalid data value */
  if (ea < 0xf000) {
    *data = M[ea]; /* normal memory */
    rc = SCPE_OK;
  } else {
    IOREAD reader = ioreaders[ea & IOPAGEMASK];
    rc = reader ? (*reader)(ea, data) : SCPE_NXM;
  }
  return rc;
}

t_stat Write(t_addr base, t_addr woffset, uint16 data, uint32 dctrl) {
  t_stat rc;
  uint16 ea = base + woffset;
  if (ea < 0xf000) {
    M[ea] = data;
    rc = SCPE_OK;
  } else {
    IOWRITE write = iowriters[ea & IOPAGEMASK];
    rc = write ? (*write)(ea, data) : SCPE_NXM;
  }
  if (rc != SCPE_OK) {
    cpu_buserror();
    sim_debug(DBG_CPU_WRITE, &cpu_dev, DBG_PCFORMAT0 "Invalid Mem write to $%04x\n", DBG_PC, ea);
printf("write buserror %x at %x:%x\n",ea,reg_segb,reg_ipc);
//exit(1);
    return rc;
  }
  if (dctrl & DBG_CPU_STACK)
    sim_debug(DBG_CPU_PUSH, &cpu_dev, DBG_PCFORMAT1 "Push %04x to SP=$%04x\n", DBG_PC, data, ea);
  else 
    sim_debug(dctrl, &cpu_dev, DBG_PCFORMAT2 "Word write %04x to $%04x\n", DBG_PC, data, ea);
  return rc;
}

t_stat ReadB(t_addr base, t_addr boffset, uint16 *data, uint32 dctrl)
{
  t_stat rc;
  t_addr ea = base + boffset/2;
  if ((rc=Read(ea, 0, data, DBG_NONE)) != SCPE_OK) return rc;
  if (boffset & 1)
    *data >>= 8;
  *data &= 0xff;
  if (dctrl & DBG_CPU_FETCH)
    sim_debug(DBG_CPU_FETCH, &cpu_dev, DBG_PCFORMAT0 "Fetch %02x from SEGB:%04x\n", 
      DBG_PC, *data, reg_ipc);
  else
    sim_debug(dctrl, &cpu_dev, DBG_PCFORMAT2 "Byte[%d] read %02x from $%04x\n",
      DBG_PC, boffset & 1, *data, ea);
  return SCPE_OK;
}

t_stat ReadBEx(t_addr base, t_addr boffset, uint16 *data)
{
  t_stat rc;
  t_addr ea = base + boffset/2;
  if ((rc=ReadEx(ea, 0, data)) != SCPE_OK) return rc;
  if (boffset & 1)
    *data >>= 8;
  *data &= 0xff;
  return SCPE_OK;
}

t_stat WriteB(t_addr base, t_addr boffset, uint16 data, uint32 dctrl)
{
  uint16 wdata;
  t_addr ea = base + boffset/2;
  if (ea < 0xfc00) {
    sim_debug(dctrl, &cpu_dev, DBG_PCFORMAT2 "Byte[%d] write %02x to $%04x\n",
              DBG_PC, boffset & 1, data, ea);
    wdata = M[ea];
  } else {
    printf(DBG_PCFORMAT0 "Invalid byte[%d] write %02x to I/O addr $%04x\n", DBG_PC, boffset & 1, data, ea);
    return STOP_ERRIO;
  }
  if (boffset & 1) {
    wdata = (wdata & 0xff) | (data<<8);
  } else {
    wdata = (wdata & 0xff00) | (data & 0xff);
  }
  return Write(ea, 0, wdata, 0);
}

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  int32 mc;
  t_addr i;

  if (val < 0 || val > 1)
    return SCPE_ARG;
    
  val = val ? 65536 : 32768;

  for (mc = 0, i = val; i < memorysize; i++)
    mc = mc | M[i];

  if (mc && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
    
  memorysize = val;
  for (i = memorysize; i < MAXMEMSIZE; i++)
    M[i] = 0;
  return SCPE_OK;
}

t_stat rom_read(t_addr ea, uint16 *data)
{
  *data = M[ea];
  return SCPE_OK;
}

t_stat rom_write(t_addr ea, uint16 data) {
  M[ea] = data;
  return SCPE_OK;
}


