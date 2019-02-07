#ifdef USE_DISPLAY
/* pdp11_ng.c: NG, Knight vector display

   Copyright (c) 2018, Lars Brinkhoff

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

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.
*/

#include "pdp11_defs.h"
#include "display/display.h"
#include "display/ng.h"
#include "sim_video.h"
#include "pdp11_11logo_rom.h"

/* Run a NG cycle every this many PDP-11 "cycle" times. */
#define NG_DELAY 1

/* Memory cycle time. */
#define MEMORY_CYCLE 1

#define CYCLE_US (MEMORY_CYCLE*(NG_DELAY*2+1))

t_stat ng_rd(int32 *data, int32 PA, int32 access);
t_stat ng_wr(int32 data, int32 PA, int32 access);
t_stat ng_svc(UNIT *uptr);
t_stat ng_reset(DEVICE *dptr);
t_stat ng_boot(int32 unit, DEVICE *dptr);
t_stat ng_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ng_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ng_set_scale(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ng_show_scale(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ng_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *ng_description (DEVICE *dptr);

#define IOLN_NG   4
DIB ng_dib = {
  IOBA_AUTO, IOLN_NG, &ng_rd, &ng_wr,
  4, IVCL(NG), VEC_AUTO, {NULL}, IOLN_NG
};

UNIT ng_unit = {
  UDATA (&ng_svc, 0, 0), NG_DELAY
};

REG ng_reg[] = {
  { DRDATAD (CYCLE, ng_unit.wait, 24, "NG cycle"), REG_NZ + PV_LEFT },
  { GRDATAD(TYPE, ng_type, 16, 16, 0, "Hardware type"), REG_FIT},
  { GRDATAD(SCALE, ng_scale, 16, 16, 0, "Hardware type"), REG_FIT},
  { NULL }
};

MTAB ng_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "TYPE", "TYPE={DAZZLE|LOGO}",
    &ng_set_type,  &ng_show_type, NULL, "Hardware Type" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "SCALE", "SCALE={1|2|4|8}",
    &ng_set_scale,  &ng_show_scale, NULL, "Pixel Scale Factor" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Bus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VECTOR",  "VECTOR",
    &set_vec,  &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
  { 0 }  };


static t_bool ng_stop_flag = FALSE;
static t_bool ng_inited = FALSE;

static void ng_quit_callback (void)
{
  ng_stop_flag = TRUE;
}

/* Debug detail levels */

#define DEB_TRC       0001
#define DEB_INT       0002

DEBTAB ng_deb[] = {
  { "TRC", DEB_TRC, "trace" },
  { "INT", DEB_INT, "interrupts" },
  { NULL, 0 }
};

DEVICE ng_dev = {
    "NG", &ng_unit, ng_reg, ng_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &ng_reset,
    &ng_boot, NULL, NULL,
    &ng_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG,
    0, ng_deb, NULL, NULL, NULL, &ng_help, NULL, 
    &ng_description
};

const char *ng_regnam[] = {
    "CSR",
    "REL",
};

t_stat
ng_rd(int32 *data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;
    
  switch (PA & 002) {
  case 000:  *data = ng_get_csr(); break; 
  case 002:  *data = ng_get_reloc(); break; 
  default: stat = SCPE_NXM;
  }
  return stat;
}

t_stat
ng_wr(int32 data, int32 PA, int32 access)
{
  switch (PA & 002) {
  case 000:  ng_set_csr(data); return SCPE_OK;
  case 002:  ng_set_reloc(data); return SCPE_OK;
  }
  return SCPE_NXM;
}

t_stat
ng_svc(UNIT *uptr)
{
  if (ng_cycle(uptr->wait, 0))
    sim_activate_after (uptr, uptr->wait);
  if (ng_stop_flag) {
    ng_stop_flag = FALSE;
    return SCPE_STOP;
  }
  return SCPE_OK;
}

t_stat
ng_reset(DEVICE *dptr)
{
  DEVICE *dptr2;
  t_stat r;

  if (dptr->flags & DEV_DIS) {
    sim_cancel (dptr->units);
    return auto_config ("NG", (dptr->flags & DEV_DIS) ? 0 : 1);;
  }

  dptr2 = find_dev ("VT");
  if ((dptr2 != NULL) && !(dptr2->flags & DEV_DIS)) {
    dptr->flags |= DEV_DIS;
    return sim_messagef (SCPE_NOFNC, "NG and VT device can't both be enabled\n");
    }
  dptr2 = find_dev ("CH");
  if ((dptr2 != NULL) && !(dptr2->flags & DEV_DIS)) {
    dptr->flags |= DEV_DIS;
    return sim_messagef (SCPE_ALATT, "NG device in conflict with CH.\n");
    }

  r = auto_config ("NG", (dptr->flags & DEV_DIS) ? 0 : 1);;
  if (r != SCPE_OK) {
    dptr->flags |= DEV_DIS;
    return r;
    }

  if (!ng_inited && !ng_init(dptr, DEB_TRC))
      return sim_messagef (SCPE_ALATT, "Display already in use.\n");
  ng_inited = TRUE;

  CLR_INT (NG);
  ng_unit.wait = 100;
  sim_activate (dptr->units, 1);

  set_cmd (0, "DZ DISABLED"); /* Conflict with NG. */
  set_cmd (0, "HK DISABLED"); /* Conflict with RF. */

  vid_register_quit_callback (&ng_quit_callback);

  return SCPE_OK;
}

t_stat
ng_boot(int32 unit, DEVICE *dptr)
{
    t_stat r;

    set_cmd (0, "CPU 56K");
    set_cmd (0, "NG TYPE=LOGO");
    set_cmd (0, "PCLK ENABLED");
    set_cmd (0, "KE ENABLED");
    set_cmd (0, "RF ENABLED");
    attach_cmd (0, "RF dummy");
    sim_set_memory_load_file (BOOT_CODE_ARRAY, BOOT_CODE_SIZE);
    r = load_cmd (0, BOOT_CODE_FILENAME);
    sim_set_memory_load_file (NULL, 0);
    cpu_set_boot (0400);
    sim_printf ("List of 11LOGO commands:\n");
    sim_printf (
"AND, BACK, BUTFIRST, BUTLAST, COUNT, CTF, DIFFERENCE, DISPLAY, DO,\n"
"EDIT, ELSE, EMPTYP, END, EQUAL, ERASETRACE, FIRST, FORWARD, FPRINT,\n"
"FPUT, GO, GREATER, HEADING, HERE, HIDETURTLE, HOME, IF, KILLDISPLAY,\n"
"LAMPOFF, LAMPON, LAST, LEFT, LESS, LEVEL, LIST, LISTP, LPUT, MAKE,\n"
"MOD, NEWSNAP, NUMBERP, OF, OUTPUT, PENDOWN, PENUP, PRINT, PRODUCT,\n"
"QUOTIENT, REQUEST, RIGHT, RUG, SENTENCE, SETHEADING, SETTURTLE, SETX,\n"
"SETXY, SETY, SHOW, SHOWTURTLE, SNAP, STARTDISPLAY, STF, STOP, SUM,\n"
"THEN, TO, TOOT, TRACE, TYPE, VERSION, WIPE, WIPECLEAN, WORD, WORDP,\n"
"XCOR, YCOR.\n\n");
    sim_printf ("MIT AI memo 315 documents a later version of 11LOGO but may be helpful\n");
    sim_printf ("in exploring the software.  It can currently be found here:\n");
    sim_printf ("https://dspace.mit.edu/handle/1721.1/6228\n\n");
    sim_printf ("To get started with turtle graphics, type STARTDISPLAY.\n\n\n");
                
    return r;
}

t_stat
ng_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  //if ((uptr->flags & DEV_DIS) == 0)
  //return SCPE_ALATT;
  if (strcasecmp (cptr, "dazzle") == 0)
    ng_type = TYPE_DAZZLE;
  else if (strcasecmp (cptr, "logo") == 0)
    ng_type = TYPE_LOGO;
  else
    return SCPE_ARG;
  return SCPE_OK;
}

t_stat
ng_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  if (ng_type == TYPE_DAZZLE)
    fprintf(st, "type=DAZZLE");
  else if (ng_type == TYPE_LOGO)
    fprintf(st, "type=LOGO");
  else
    fprintf(st, "type=unknown");
  return SCPE_OK;
}

t_stat
ng_set_scale(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat r;
  t_value v;
  if ((uptr->flags & UNIT_DIS) == 0)
    return SCPE_ALATT;
  if (cptr == NULL)
    return SCPE_ARG;
  v = get_uint(cptr, 10, 8, &r);
  if (r != SCPE_OK)
    return r;
  if (v != 1 && v != 2 && v != 4 && v != 8)
    return SCPE_ARG;
  ng_scale = v;
  return SCPE_OK;
}

t_stat
ng_show_scale(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  fprintf(st, "scale=%d", (int)ng_scale);
  return SCPE_OK;
}

void
ng_nxm_intr(void)
{
  sim_debug (DEB_INT, &ng_dev, "NXM interrup\n");
  SET_INT (NG);
}

int
ng_store(uint32 addr, uint16 x)
{
  uint16 word = x;
  if (Map_WriteW(addr, 2, &word) == 0)
    return 0;
  return 1;
}

int
ng_fetch(uint32 addr, uint16 *wp)
{
  if (Map_ReadW(addr, 2, wp) == 0)
    return 0;
  *wp = 0;
  return 1;
}

const char *ng_description (DEVICE *dptr)
{
  return "Vector display controller for MIT Logo PDP-11/45";
}

t_stat ng_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
 /* The '*'s in the next line represent the standard text width of a help line */
              /****************************************************************************/
  fprintf(st, "%s\n\n", ng_description (dptr));
  fprintf(st, "The NG is a Unibus device which can control up to eight XY displays.\n");
  fprintf(st, "This simulation only supports one, which is also what the available\n");
  fprintf(st, "software uses.  Configurable options are TYPE and SCALE.\n\n");
  fprintf(st, "To select the hardware type compatible with Dazzle Dart, type\n\n");
  fprintf(st, "  sim> SET NG TYPE=DAZZLE\n\n");
  fprintf(st, "To select the hardware type compatible with Logo, type\n\n");
  fprintf(st, "  sim> SET NG TYPE=LOGO\n\n");
  fprintf(st, "Set SCALE to one of 1, 2, 3, or 4 to select full size, half size,\n");
  fprintf(st, "quarter size, or eighth size.\n\n");

  fprintf(st, "The primary software for the NG display was MIT's PDP-11 Logo, or 11LOGO.\n");
  fprintf(st, "To run 11LOGO:\n\n\n");
  fprintf(st, "   sim> set cpu 11/45\n");
  fprintf(st, "   sim> set ng enabled\n");
  fprintf(st, "   sim> boot ng\n\n");

  return SCPE_OK;
}
#else  /* USE_DISPLAY not defined */
char pdp11_ng_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
