#ifdef USE_DISPLAY
/* pdp11_vt.c: PDP-11 VT11/VS60 Display Processor Simulation

   Copyright (c) 2003-2004, Philip L. Budne, Douglas A. Gwyn
   Copyright (c) 1993-2003, Robert M Supnik

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

   vt           VT11/VS60 Display Processor

   05-Feb-04    DAG     Improved VT11 emulation
                        Added VS60 support
   14-Sep-03    PLB     Start from pdp11_lp.c
*/

/*
 * this file is just a thin layer of glue to the simulator-
 * independent XY Display simulation
 */

#if defined (VM_VAX)                                    /* VAX version */
#include "vax_defs.h"
#elif defined(VM_PDP11)                                 /* PDP-11 version */
#include "pdp11_defs.h"
#else
#error "VT11/VS60 is supported only on the PDP-11 and VAX"
#endif

#include "display/display.h"
#include "display/vt11.h"

/*
 * Timing parameters.  Should allow some runtime adjustment,
 * since several different configurations were shipped, including:
 *
 * GT40: PDP-11/05 with VT11 display processor
 * GT44: PDP-11/40 with VT11 display processor
 * GT46: PDP-11/34 with VT11 display processor
 * GT62: PDP-11/34a with VS60 display system
 */

/*
 * run a VT11/VS60 cycle every this many PDP-11 "cycle" times;
 *
 * Under the X Window System (X11), this includes polling
 * for events (mouse movement)!
 */
#define VT11_DELAY 1

/*
 * memory cycle time
 */
#define MEMORY_CYCLE 1                  /* either .98 or 1.2 us? */

/*
 * delay in microseconds between VT11/VS60 cycles:
 * VT11/VS60 and PDP-11 CPU's share the same memory bus,
 * and each VT11/VS60 instruction requires a memory reference;
 * figure each PDP11 instruction requires two memory references
 */
#define CYCLE_US (MEMORY_CYCLE*(VT11_DELAY*2+1))

extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];

DEVICE vt_dev;
t_stat vt_rd(int32 *data, int32 PA, int32 access);
t_stat vt_wr(int32 data, int32 PA, int32 access);
t_stat vt_svc(UNIT *uptr);
t_stat vt_reset(DEVICE *dptr);
t_stat vt_boot(int32 unit, DEVICE *dptr);
t_stat vt_set_crt(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat vt_show_crt(FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat vt_set_scale(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat vt_show_scale(FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat vt_set_hspace(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat vt_show_hspace(FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat vt_set_vspace(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat vt_show_vspace(FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat vt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *vt_description (DEVICE *dptr);

/* VT11/VS60 data structures

   vt_dev       VT11 device descriptor
   vt_unit      VT11 unit descriptor
   vt_reg       VT11 register list
   vt_mod       VT11 modifier list
*/
#define IOLN_VT11   010             /* VT11 */
#define IOLN_VS60   040             /* VS60 */
DIB vt_dib = { IOBA_AUTO, IOLN_VT11, &vt_rd, &vt_wr,
               4, IVCL(VTST), VEC_AUTO, {NULL} };
        /* (VT11 uses only the first 3 interrupt vectors) */

UNIT vt_unit = {
    UDATA (&vt_svc, UNIT_SEQ, 0), VT11_DELAY};

REG vt_reg[] = {
    { GRDATA (DEVADDR, vt_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, vt_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }  };

MTAB vt_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "CRT", "CRT={VR14|VR17|VR48}",
                &vt_set_crt, &vt_show_crt, NULL, "CRT Type" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "SCALE", "SCALE={1|2|4|8}",
                &vt_set_scale, &vt_show_scale, NULL, "Pixel Scale Factor" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "HSPACE", "HSPACE={NARROW|NORMAL}",
                &vt_set_hspace, &vt_show_hspace, NULL, "Horizontal Spacing" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VSPACE", "VSPACE={TALL|NORMAL}",
                &vt_set_vspace, &vt_show_vspace, NULL, "Vertical Spacing" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec, &show_vec, NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
        &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
    { 0 }  };

DEVICE vt_dev = {
    "VT", &vt_unit, vt_reg, vt_mod,
    1, 8, 31, 1, DEV_RDX, 16,
    NULL, NULL, &vt_reset,
    &vt_boot, NULL, NULL,
    &vt_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_Q18,
    0, 0, NULL, NULL, NULL, NULL, NULL, 
    &vt_description
};

/* VT11/VS60 routines

   vt_rd        I/O page read
   vt_wr        I/O page write
   vt_svc       process event
   vt_reset     process reset
   vt_boot      bootstrap device
*/

t_stat
vt_rd(int32 *data, int32 PA, int32 access)
{
    switch (PA & 036) {
    case 000:                     *data = vt11_get_dpc(); return SCPE_OK; 
    case 002:                     *data = vt11_get_mpr(); return SCPE_OK;
    case 004:                     *data = vt11_get_xpr(); return SCPE_OK;
    case 006:                     *data = vt11_get_ypr(); return SCPE_OK;
    case 010:   if (!VS60) break; *data = vt11_get_rr();  return SCPE_OK; 
    case 012:   if (!VS60) break; *data = vt11_get_spr(); return SCPE_OK; 
    case 014:   if (!VS60) break; *data = vt11_get_xor(); return SCPE_OK; 
    case 016:   if (!VS60) break; *data = vt11_get_yor(); return SCPE_OK; 
    case 020:   if (!VS60) break; *data = vt11_get_anr(); return SCPE_OK; 
    case 022:   if (!VS60) break; *data = vt11_get_scr(); return SCPE_OK; 
    case 024:   if (!VS60) break; *data = vt11_get_nr();  return SCPE_OK; 
    case 026:   if (!VS60) break; *data = vt11_get_sdr(); return SCPE_OK; 
    case 030:   if (!VS60) break; *data = vt11_get_str(); return SCPE_OK; 
    case 032:   if (!VS60) break; *data = vt11_get_sar(); return SCPE_OK; 
    case 034:   if (!VS60) break; *data = vt11_get_zpr(); return SCPE_OK; 
    case 036:   if (!VS60) break; *data = vt11_get_zor(); return SCPE_OK; 
    }
    return SCPE_NXM;
}

t_stat
vt_wr(int32 data, int32 PA, int32 access)
{
    uint16 d = data & 0177777;          /* mask just in case */

    switch (PA & 037) {
    case 000:                           /* DPC */
        /* set the simulator PC */
        vt11_set_dpc(d);

        /* clear interrupt request (only one will be simulated at a time) */
        CLR_INT (VTST);
        CLR_INT (VTLP);
        CLR_INT (VTCH);
        CLR_INT (VTNM);

        /* start the display processor by running a cycle */
        return vt_svc(&vt_unit);

    case 002:                     vt11_set_mpr(d); return SCPE_OK;
    case 004:                     vt11_set_xpr(d); return SCPE_OK;
    case 006:                     vt11_set_ypr(d); return SCPE_OK;
    case 010:   if (!VS60) break; vt11_set_rr(d);  return SCPE_OK; 
    case 012:   if (!VS60) break; vt11_set_spr(d); return SCPE_OK; 
    case 014:   if (!VS60) break; vt11_set_xor(d); return SCPE_OK; 
    case 016:   if (!VS60) break; vt11_set_yor(d); return SCPE_OK; 
    case 020:   if (!VS60) break; vt11_set_anr(d); return SCPE_OK; 
    case 022:   if (!VS60) break; vt11_set_scr(d); return SCPE_OK; 
    case 024:   if (!VS60) break; vt11_set_nr(d);  return SCPE_OK;
    case 026:   if (!VS60) break; vt11_set_sdr(d); return SCPE_OK;
    case 030:   if (!VS60) break; vt11_set_str(d); return SCPE_OK; 
    case 032:   if (!VS60) break; vt11_set_sar(d); return SCPE_OK; 
    case 034:   if (!VS60) break; vt11_set_zpr(d); return SCPE_OK;
    case 036:   if (!VS60) break; vt11_set_zor(d); return SCPE_OK; 
    }
    return SCPE_NXM;
}

/*
 * here to run a display processor cycle, called as a SIMH
 * "device service routine".
 *
 * Under X11 this includes polling for events, so it can't be
 * call TOO infrequently...
 */
t_stat
vt_svc(UNIT *uptr)
{
    if (vt11_cycle(CYCLE_US, 1))
        sim_activate (&vt_unit, vt_unit.wait);  /* running; reschedule */
    return SCPE_OK;
}

t_stat
vt_reset(DEVICE *dptr)
{
    if (!(dptr->flags & DEV_DIS))
        vt11_reset();
    CLR_INT (VTST);
    CLR_INT (VTLP);
    CLR_INT (VTCH);
    CLR_INT (VTNM);
    sim_cancel (&vt_unit);              /* deactivate unit */
    return auto_config ("VT", (dptr->flags & DEV_DIS) ? 0 : 1);
}

/*
 * GT4x/GT62 bootstrap (acts as remote terminal)
 */
t_stat
vt_boot(int32 unit, DEVICE *dptr)
{
    /* XXX  should do something like vt11_set_dpc(&appropriate_ROM_image) */
    return SCPE_NOFNC;                  /* not yet implemented */
}

/* SET/SHOW VT options: */

t_stat
vt_set_crt(UNIT *uptr, int32 val, char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "VR14") == 0)
        vt11_display = DIS_VR14;
    else if (strcmp(gbuf, "VR17") == 0)
        vt11_display = DIS_VR17;
    else if (strcmp(gbuf, "VR48") == 0)
        vt11_display = DIS_VR48;
    else
        return SCPE_ARG;
    vt_dib.lnt = (VS60) ? IOLN_VS60 : IOLN_VT11;
    return SCPE_OK;
}

t_stat
vt_show_crt(FILE *st, UNIT *uptr, int32 val, void *desc)
{
    fprintf(st, "crt=VR%d", (int)vt11_display);
    return SCPE_OK;
}

t_stat
vt_set_scale(UNIT *uptr, int32 val, char *cptr, void *desc)
{
    t_stat r;
    t_value v;
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    v = get_uint(cptr, 10, 8, &r);
    if (r != SCPE_OK)
        return r;
    if (v != 1 && v != 2 && v != 4 && v != 8)
        return SCPE_ARG;
    vt11_scale = v;
    return SCPE_OK;
}

t_stat
vt_show_scale(FILE *st, UNIT *uptr, int32 val, void *desc)
{
    fprintf(st, "scale=%d", (int)vt11_scale);
    return SCPE_OK;
}

t_stat
vt_set_hspace(UNIT *uptr, int32 val, char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "NARROW") == 0)
        vt11_csp_w = 12;
    else if (strcmp(gbuf, "NORMAL") == 0)
        vt11_csp_w = 14;
   else
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat
vt_show_hspace(FILE *st, UNIT *uptr, int32 val, void *desc)
{
    fprintf(st, "hspace=%s", vt11_csp_w==12 ? "narrow" : "normal");
    return SCPE_OK;
}

t_stat
vt_set_vspace(UNIT *uptr, int32 val, char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "TALL") == 0)
        vt11_csp_h = 26;
    else if (strcmp(gbuf, "NORMAL") == 0)
        vt11_csp_h = 24;
   else
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat
vt_show_vspace(FILE *st, UNIT *uptr, int32 val, void *desc)
{
    fprintf(st, "vspace=%s", vt11_csp_h==26 ? "tall" : "normal");
    return SCPE_OK;
}

/* interface routines (called from display simulator) */

void
vt_stop_intr(void)
{
    SET_INT (VTST);
}

void
vt_lpen_intr(void)
{
    SET_INT (VTLP);
}

void
vt_char_intr(void)
{
    SET_INT (VTCH);
}

void
vt_name_intr(void)
{
    SET_INT (VTNM);
}

/* fetch memory */
int
vt_fetch(uint32 addr, vt11word *wp)
{
    /* On PDP-11 Unibus 22-bit systems, the VT11/VS60 behaves as
       an 18-bit Unibus peripheral and must go through the I/O map. */

    /* apply Unibus map, when appropriate */
    if (Map_ReadW(addr, 2, wp) == 0)
        return 0;                       /* no problem */
    /* else mapped address lies outside configured memory range */

    *wp = 0164000;                      /* DNOP; just updates DPC if used */
                                                /* which shouldn't happen */
    return 1;                           /* used to set "time_out" flag */
}

char *vt_description (DEVICE *dptr)
{
return (VS60) ? "VS60 Display processor"
              : "VT11 Display processor";
}

#ifdef VM_PDP11
/* PDP-11 simulation provides this */
extern int32 SR;                        /* switch register */
#else
int32 SR;                               /* switch register */
#endif

void
cpu_set_switches(unsigned long val)
{
    SR = val;
}

unsigned long
cpu_get_switches(void)
{
    return SR;
}
#else  /* USE_DISPLAY not defined */
char pdp11_vt_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
