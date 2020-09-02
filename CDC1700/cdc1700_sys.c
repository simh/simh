/*

   Copyright (c) 2015-2016, John Forecast

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
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

/* cdc1700_sys.c: CDC1700 system description
 */

#include "cdc1700_defs.h"
#include <ctype.h>

extern void buildDCtables(void);
extern void buildIOtable(void);

extern int disassem(char *, uint16, t_bool, t_bool, t_bool);

extern uint16 M[];
extern REG cpu_reg[];
extern DEVICE cpu_dev, dca_dev, dcb_dev, dcc_dev,
  tti_dev, tto_dev, ptr_dev, ptp_dev, mt_dev, lp_dev, dp_dev,
  cd_dev, drm_dev, rtc_dev;
extern UNIT cpu_unit;

t_stat autoload(int32, CONST char *);
t_stat CDautoload(void);
t_stat DPautoload(void);
t_stat DRMautoload(void);

t_bool RelValid = FALSE;
uint16 RelBase;

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "CDC1700";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 2;

DEVICE *sim_devices[] = {
  &cpu_dev,
  &rtc_dev,
  &dca_dev,
  &dcb_dev,
  &dcc_dev,
  &tti_dev,
  &tto_dev,
  &ptr_dev,
  &ptp_dev,
  &mt_dev,
  &lp_dev,
  &dp_dev,
  &cd_dev,
  &drm_dev,
  NULL
};

const char *sim_stop_messages[SCPE_BASE] = {
  "OK",
  "Indirect addressing loop count exceeded",
  "Selective Stop",
  "Invalid bits set in EXI instruction",
  "Breakpoint",
  "Stop on reject",
  "Unimpl. instruction"
};

/*
 * New top-level command(s) for the CDC1700
 */
CTAB cdc1700_cmd[] = {
  { "AUTOLOAD", &autoload, 0,
    "a{utoload} <controller> Autoload from default device on controller\n"
    "                        Loads track 0 to location 0\n"
  },
  { NULL }
};

/*
 * Command post-processing routine.
 */
static void postUpdate(t_bool from_scp)
{
  /*
   * Rebuild the I/O device and buffered data channel tables in case the
   * command changed the configuration.
   */
  buildIOtable();
  buildDCtables();

  RelValid = FALSE;
}

/*
 * Special address print routine for "Relative" display.
 */
static void sprintAddress(char *buf, DEVICE *dptr, t_addr addr)
{
  if ((dptr == sim_devices[0]) && ((sim_switches & SWMASK('R')) != 0)) {
    if (!RelValid) {
      RelBase = (uint16)addr;
      RelValid = TRUE;
    }
    addr -= RelBase;
  } 
  sprint_val(buf, addr, dptr->aradix, dptr->awidth, PV_RZRO);
}

static void printAddress(FILE *st, DEVICE *dptr, t_addr addr)
{
  char buf[64];

  sprintAddress(buf, dptr, addr);
  fprintf (st, "%s", buf);
}

/*
 * VM initialization
 */
void VMinit(void)
{
  sim_vm_sprint_addr = &sprintAddress;
  sim_vm_fprint_addr = &printAddress;
  sim_vm_post = &postUpdate;
  sim_vm_cmd = cdc1700_cmd;
}

/*
 * Check for duplicate equipment addresses.
 */
static t_bool checkDuplicate(DEVICE *dptr, uint8 equipment)
{
  int i = 0;
  DEVICE *dptr2;

  while ((dptr2 = sim_devices[i++]) != NULL) {
    if ((dptr2 != dptr) && ((dptr2->flags & DEV_DIS) == 0)) {
      IO_DEVICE *iod = (IO_DEVICE *)dptr2->ctxt;

      if (iod->iod_equip == equipment)
        return TRUE;
    }
  }
  return FALSE;
}

/*
 * Common routine to change the equipment address of a peripheral. Some
 * devices (e.g. TT, PTR etc) cannot have their equipment address changed.
 */
t_stat set_equipment(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  DEVICE *dptr;
  IO_DEVICE *iod;
  t_value v;
  t_stat r;

  if (cptr == NULL)
    return SCPE_ARG;

  v = get_uint(cptr, DEV_RDX, 15, &r);
  if (r != SCPE_OK)
    return r;
  if (v == 0)
    return SCPE_ARG;

  /*
   * Check to see if any other, non-disabled device is already using this
   * address.
   */
  if ((dptr = find_dev_from_unit(uptr)) != NULL) {
    if (checkDuplicate(dptr, v))
      return sim_messagef(SCPE_ARG, "Equipment address already in use\n");

    iod = (IO_DEVICE *)dptr->ctxt;
    iod->iod_equip = v;
    iod->iod_interrupt = 1 << v;
    return SCPE_OK;
  }
  return SCPE_NXDEV;
}
/*
 * Check for a duplicate address when a device is reset. If a duplicate is
 * found, the device being reset is disabled.
 */
t_stat checkReset(DEVICE *dptr, uint8 equipment)
{
  if (checkDuplicate(dptr, equipment)) {
    dptr->flags |= DEV_DIS;
    return sim_messagef(SCPE_ARG, "Equipment address already in use\n");
  }
  return SCPE_OK;
}

t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fname, int flag)
{
  t_addr lo, hi;

  if (flag == 0)
    return SCPE_ARG;

  /*
   * We want to write the memory in some device-dependent format. sim_switches
   * contains the command switches which will be used to determine the
   * format:
   *
   *    -p              Paper tape format
   *
   * Command syntax is:
   *
   * dump <file> -p <loaddr>-<hiaddr>
   */
  if ((sim_switches & SWMASK('P')) != 0) {
    const char *tptr;
    t_addr addr;
    int temp, count = 0;

    tptr = get_range(NULL, cptr, &lo, &hi, cpu_dev.aradix, cpu_unit.capac - 1, 0);
    if (tptr != NULL) {
      /*
       * Output a couple of NULL frames to start the dump
       */
      fputc(0, fileref);
      fputc(0, fileref);

      for (addr = lo; addr <= hi; addr++) {
        temp = M[addr];

        /*
         * If the data is 0, map it to -0 (0xFFFF) since 0 terminates the
         * sequence. We also count the number of times this happens and
         * report it at the end.
         */
        if (temp == 0) {
          temp =0xFFFF;
          count++;
        }
        fputc((temp >> 8) & 0xFF, fileref);
        fputc(temp & 0xFF, fileref);
      }
      /*
       * Terminate the dump with 2 more NULL frames
       */
      fputc(0, fileref);
      fputc(0, fileref);

      if (count != 0)
        printf("%d zero word translated to 0xFFFF\n", count);

      return SCPE_OK;
    }
  }
  return SCPE_ARG;
}

/*
 * Symbolic decode
 */
#define FMTASC(x)       ((x) < 040) ? "<%03o>" : "%c", (x)

t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
  int32 inst = val[0];
  t_bool target = (sw & SWMASK('T')) != 0;
  char buf[128];
  int consume;

  if ((sw & SWMASK('A')) != 0) {
    /* ASCII character */
    if (inst > 0377)
      return SCPE_ARG;
    fprintf(of, FMTASC(inst & 0177));
    return SCPE_OK;
  }

  if ((sw & SWMASK('C')) != 0) {
    unsigned char c1 = (inst >> 8) & 0xFF, c2 = inst & 0xFF;

    fprintf(of, FMTASC(c1 & 0177));
    fprintf(of, FMTASC(c2 & 0177));
    return SCPE_OK;
  }

  if ((sw & SWMASK('M')) == 0)
    return SCPE_ARG;

  consume = disassem(buf, (uint16)addr, FALSE, target, FALSE);
  fprintf(of, "%s", buf);
  return -(consume - 1);
}

/*
 * Autoload top-level command routine
 */
t_stat autoload(int32 flag, CONST char *ptr)
{
  char gbuf[CBUFSIZE];
  DEVICE *dptr;

  if (!ptr || !*ptr)
    return SCPE_2FARG;

  get_glyph(ptr, gbuf, 0);
  dptr = find_dev(gbuf);
  if (dptr == NULL)
    return SCPE_ARG;

  if (dptr == &cd_dev)
    return CDautoload();
  if (dptr == &dp_dev)
    return DPautoload();
  if (dptr == &drm_dev)
    return DRMautoload();

  return SCPE_NOFNC;
}
