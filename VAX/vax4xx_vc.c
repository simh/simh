/* vax4xx_vc.c: Monochrome video simulator

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   vc          Monochrome video
*/

#include "vax_defs.h"
#include "sim_video.h"
#include "vax_lk.h"
#include "vax_vs.h"

#define VC_XSIZE        1024                            /* visible width */
#define VC_YSIZE        864                             /* visible height */

#define VC_BXSIZE       1024                            /* buffer width */
#define VC_BUFMASK      (VC_BUFSIZE - 1)

#define CUR_X_OF        216
#define CUR_Y_OF        33

#define CMD_TEST        0x8000
#define CMD_HSHI        0x4000
#define CMD_VBHI        0x2000
#define CMD_LODSA       0x1000
#define CMD_FORG2       0x0800
#define CMD_ENRG2       0x0400
#define CMD_FORG1       0x0200
#define CMD_ENRG1       0x0100
#define CMD_XHWID       0x0080
#define CMD_XHCL1       0x0040
#define CMD_XHCLP       0x0020
#define CMD_XHAIR       0x0010
#define CMD_FOPB        0x0008
#define CMD_ENPB        0x0004
#define CMD_FOPA        0x0002
#define CMD_ENPA        0x0001

#define CUR_X           ((vc_xpos < CUR_X_OF) ? 0 : (vc_xpos - CUR_X_OF))
#define CUR_Y           ((vc_ypos < CUR_Y_OF) ? 0 : (vc_ypos - CUR_Y_OF))
#define CUR_V           ((vc_cmd & CMD_LODSA) == 0)
#define CUR_F           (0)

#define CUR_PLNA        0
#define CUR_PLNB        16

/* Debugging Bitmaps */

#define DBG_REG         0x0001                          /* registers */
#define DBG_CURSOR      0x0002                          /* Cursor content, function and visibility activity */
#define DBG_TCURSOR     0x0800                          /* Cursor content, function and visibility activity */

extern int32 tmxr_poll;
extern int32 ka_cfgtst;

uint32 vc_cmd = 0;                                      /* cursor command reg */
uint32 vc_xpos = 0;                                     /* cursor x position */
uint32 vc_ypos = 0;                                     /* cursor y position */
uint32 vc_xmin1 = 0;                                    /* region 1 left edge */
uint32 vc_xmax1 = 0;                                    /* region 1 right edge */
uint32 vc_ymin1 = 0;                                    /* region 1 top edge */
uint32 vc_ymax1 = 0;                                    /* region 1 bottom edge */
uint32 vc_xmin2 = 0;                                    /* region 2 left edge */
uint32 vc_xmax2 = 0;                                    /* region 2 right edge */
uint32 vc_ymin2 = 0;                                    /* region 2 top edge */
uint32 vc_ymax2 = 0;                                    /* region 2 bottom edge */
uint16 vc_cur[32];                                      /* cursor image data */
uint32 vc_cur_p = 0;                                    /* cursor image pointer */
t_bool vc_updated[VC_YSIZE];
t_bool vc_cur_new_data = FALSE;                         /* New Cursor image data */
t_bool vc_input_captured = FALSE;                       /* Mouse and Keyboard input captured in video window */
uint32 vc_cur_x = 0;                                    /* Last cursor X-position */
uint32 vc_cur_y = 0;                                    /* Last cursor Y-position */
uint32 vc_cur_f = 0;                                    /* Last cursor function */
t_bool vc_cur_v = FALSE;                                /* Last cursor visible */
uint32 vc_org = 0;                                      /* display origin */
uint32 vc_last_org = 0;                                 /* display last origin */
uint32 vc_sel = 0;                                      /* interrupt select */
uint32 *vc_buf = NULL;                                  /* Video memory */
uint32 *vc_lines = NULL;                                /* Video Display Lines */
uint32 vc_palette[2];                                   /* Monochrome palette */
t_bool vc_active = FALSE;

t_stat vc_svc (UNIT *uptr);
t_stat vc_reset (DEVICE *dptr);
t_stat vc_detach (UNIT *dptr);
t_stat vc_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vc_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vc_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *vc_description (DEVICE *dptr);

/* VC data structures

   vc_dev      VC device descriptor
   vc_unit     VC unit descriptor
   vc_reg      VC register list
*/

UNIT vc_unit = {
    UDATA ( &vc_svc, UNIT_IDLE, 0 )
    };

REG vc_reg[] = {
    { HRDATAD  (CMD,   vc_cmd,   16, "Cursor command register") },
    { DRDATAD  (XPOS,  vc_xpos,  16, "Cursor X position") },
    { DRDATAD  (YPOS,  vc_ypos,  16, "Cursor Y position") },
    { DRDATAD  (XMIN1, vc_xmin1, 16, "Region 1 left edge") },
    { DRDATAD  (XMAX1, vc_xmax1, 16, "Region 1 right edge") },
    { DRDATAD  (YMIN1, vc_ymin1, 16, "Region 1 top edge") },
    { DRDATAD  (YMAX1, vc_ymax1, 16, "Region 1 bottom edge") },
    { DRDATAD  (XMIN2, vc_xmin2, 16, "Region 2 left edge") },
    { DRDATAD  (XMAX2, vc_xmax2, 16, "Region 2 right edge") },
    { DRDATAD  (YMIN2, vc_ymin2, 16, "Region 2 top edge") },
    { DRDATAD  (YMAX2, vc_ymax2, 16, "Region 2 bottom edge") },
    { DRDATAD  (ORG,   vc_org,    8, "Display origin") },
    { DRDATAD  (ISEL,  vc_sel,    1, "Interrupt select") },
    { HRDATA   (CURP,  vc_cur_p,  5), REG_HRO },
    { NULL }
    };

DEBTAB vc_debug[] = {
    { "REG",     DBG_REG,     "Register activity" },
    { "CURSOR",  DBG_CURSOR,  "Cursor content, function and visibility activity" },
    { "TCURSOR", DBG_TCURSOR, "Cursor content, function and visibility activity" },
    { 0 }
    };

MTAB vc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLE",
        &vc_set_enable, NULL, NULL, "Enable Monochrome Video" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLE",
        &vc_set_enable, NULL, NULL, "Disable Monochrome Video" },
    { MTAB_XTD|MTAB_VDV, TRUE, NULL, "CAPTURE",
        &vc_set_capture, &vc_show_capture, NULL, "Enable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, FALSE, NULL, "NOCAPTURE",
        &vc_set_capture, NULL, NULL, "Disable Captured Input Mode" },
    { MTAB_XTD|MTAB_VDV, TRUE, "OSCURSOR", NULL,
        NULL, &vc_show_capture, NULL, "Display Input Capture mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VIDEO", NULL,
        NULL, &vid_show_video, NULL, "Display the host system video capabilities" },
    { 0 }
    };

DEVICE vc_dev = {
    "VC", &vc_unit, vc_reg, vc_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &vc_reset,
    NULL, NULL, &vc_detach,
    NULL, DEV_DEBUG | DEV_DIS, 0,
    vc_debug, NULL, NULL, &vc_help, NULL, NULL, 
    &vc_description
    };

/* VC routines

   vc_wr       I/O page write
   vc_svc      process event
   vc_reset    process reset
*/

void vc_wr (int32 pa, int32 data, int32 access)
{
int32 rg = (pa >> 2) & 0x1F;

if (vc_dev.flags & DEV_DIS)                             /* disabled? */
    return;

switch (rg) {

    case 0:                                             /* CUR_CMD */
        if ((data & CMD_TEST) == 0) {
            if (data & (CMD_ENRG2|CMD_FORG2|CMD_ENRG1|CMD_FORG1|CMD_FOPB|CMD_FOPA))
                ka_cfgtst = ka_cfgtst & ~(1 << 4);
            else ka_cfgtst = ka_cfgtst | (1 << 4);
            }
        else ka_cfgtst = ka_cfgtst | (1 << 4);
        if ((vc_cmd ^ data) & CMD_LODSA)                /* toggle sprite display? */
            vc_cur_p = 0;                               /* reset array ptr */
        vc_cmd = data;
        break;

    case 1:                                             /* CUR_XPOS */
        vc_xpos = data;
        vid_set_cursor_position (CUR_X, CUR_Y);
        break;

    case 2:                                             /* CUR_YPOS */
        vc_ypos = data;
        vid_set_cursor_position (CUR_X, CUR_Y);
        break;

    case 3:                                             /* CUR_XMIN_1 */
        vc_xmin1 = data;
        break;

    case 4:                                             /* CUR_XMAX_1 */
        vc_xmax1 = data;
        break;

    case 5:                                             /* CUR_YMIN_1 */
        vc_ymin1 = data;
        break;

    case 6:                                             /* CUR_YMAX_1 */
        vc_ymax1 = data;
        break;

    case 11:                                            /* CUR_XMIN_2 */
        vc_xmin2 = data;
        break;

    case 12:                                            /* CUR_XMAX_2 */
        vc_xmax2 = data;
        break;

    case 13:                                            /* CUR_YMIN_2 */
        vc_ymin2 = data;
        break;

    case 14:                                            /* CUR_YMAX_2 */
        vc_ymax2 = data;
        break;

    case 15:                                            /* CUR_LOAD */
        vc_cur[vc_cur_p++] = data;
        if (vc_cur_p == 32)
            vc_cur_p--;
        vc_cur_new_data = TRUE;
        break;
    }

sim_debug (DBG_REG, &vc_dev, "reg %d write, value = %X\n", rg, data);
return;
}

int32 vc_mem_rd (int32 pa)
{
int32 rg = ((pa - 0x30000000) >> 2);

if (!vc_buf)                                            /* MONO disabled? */
    return 0;                                           /* Invalid memory reference */

return vc_buf[rg];
}

void vc_mem_wr (int32 pa, int32 val, int32 lnt)
{
int32 nval;
int32 rg = ((pa - 0x30000000) >> 2);
uint32 scrln;

if (!vc_buf)                                            /* MONO disabled? */
    return;                                             /* Invalid memory reference */

if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    int32 t = vc_buf[rg];
    nval = ((val & mask) << sc) | (t & ~(mask << sc));
    }
else nval = val;
vc_buf[rg] = nval;                                      /* update buffer */
scrln = ((rg >> 5) + VC_BYSIZE - (vc_org << VC_ORSC)) & (VC_BYSIZE - 1);
if (scrln < VC_YSIZE)
    vc_updated[scrln] = TRUE;                           /* flag as updated */
return;
}

static void vc_set_vid_cursor (t_bool visible)
{
uint8 data[2*16];
uint8 mask[2*16];
uint32 ln, col;
uint16 *plna, *plnb;
uint16 bita, bitb;
int i, d, m;

sim_debug (DBG_CURSOR, &vc_dev, "vc_set_vid_cursor(%s)\n", visible ? "Visible" : "Invisible");
memset (data, 0, sizeof(data));
memset (mask, 0, sizeof(mask));
for (ln = 0; ln < 16; ln++) {
    plna = &vc_cur[(CUR_PLNA + ln)];                    /* get plane A base */
    plnb = &vc_cur[(CUR_PLNB + ln)];                    /* get plane B base */
    for (col = 0; col < 16; col++) {
        if (vc_cmd & CMD_FOPA)                          /* force plane A to 1? */
            bita = 1;
        else if (vc_cmd & CMD_ENPA)                     /* plane A enabled? */
            bita = (*plna >> col) & 1;
        else bita = 0;
        if (vc_cmd & CMD_FOPB)                          /* force plane B to 1? */
            bitb = 1;
        else if (vc_cmd & CMD_ENPB)                     /* plane B enabled? */
            bitb = (*plnb >> col) & 1;
        else bitb = 0;

        if (bita) {
            if (bitb) {
                d = 0; m = 1;                           /* white */
                }
            else {
                d = 1; m = 0;                           /* inverted */
                }
            }
        else {
            if (bitb) {
                d = 1; m = 1;                           /* black */
                }
            else {
                d = 0; m = 0;                           /* transparent */
                }
            }
        i = (ln * 16) + col;
        data[i>>3] |= d<<(7-(i&7));
        mask[i>>3] |= m<<(7-(i&7));
        }
    }
if ((vc_dev.dctrl & DBG_CURSOR) && (vc_dev.dctrl & DBG_TCURSOR)) {
    /* box the cursor image */
    for (i=0; i<16*16; i++) {
        if ((0 == i>>4) || (0xF == i>>4) || (0 == (i&0xF)) || (0xF == (i&0xF))) {
            data[i>>3] |= 1<<(7-(i&7));
            mask[i>>3] |= 1<<(7-(i&7));
            }
        if ((1 == i>>4) || (0xE == i>>4) || (1 == (i&0xF)) || (0xE == (i&0xF))) {
            data[i>>3] &= ~(1<<(7-(i&7)));
            mask[i>>3] |= 1<<(7-(i&7));
            }
        }
    }
vid_set_cursor (visible, 16, 16, data, mask, 0, 0);
}

static SIM_INLINE void vc_invalidate (uint32 y1, uint32 y2)
{
uint32 ln;

for (ln = y1; ln < y2; ln++)
    vc_updated[ln] = TRUE;                              /* flag as updated */
}

t_stat vc_svc (UNIT *uptr)
{
SIM_MOUSE_EVENT mev;
SIM_KEY_EVENT kev;
t_bool updated = FALSE;                                 /* flag for refresh */
uint32 lines;
uint32 ln, col, off;
uint16 *plna, *plnb;
uint16 bita, bitb;
uint32 c;

if (vc_cur_v != CUR_V) {                                /* visibility changed? */
    if (CUR_V)                                          /* visible? */
        vc_invalidate (CUR_Y, (CUR_Y + 16));            /* invalidate new pos */
    else
        vc_invalidate (vc_cur_y, (vc_cur_y + 16));      /* invalidate old pos */
    }
else if (vc_cur_y != CUR_Y) {                           /* moved (Y)? */
    vc_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    vc_invalidate (vc_cur_y, (vc_cur_y + 16));          /* invalidate old pos */
    }
else if ((vc_cur_x != CUR_X) ||                         /* moved (X)? */
         (vc_cur_new_data)) {                           /* cursor image changed? */
    vc_invalidate (CUR_Y, (CUR_Y + 16));                /* invalidate new pos */
    }

if ((!vc_input_captured) &&                             /* OS cursor? AND */
    ((vc_cur_new_data) ||                               /*  cursor image changed? OR */
     (vc_cur_v != CUR_V))) {                            /*  visibility changed? */
    vc_set_vid_cursor (CUR_V);
    }

vc_cur_x = CUR_X;                                       /* store cursor data */
vc_cur_y = CUR_Y;
vid_set_cursor_position (vc_cur_x, vc_cur_y);
vc_cur_v = CUR_V;
vc_cur_f = CUR_F;
vc_cur_new_data = FALSE;

if (vid_poll_kb (&kev) == SCPE_OK)                      /* poll keyboard */
    lk_event (&kev);                                    /* push event */
if (vid_poll_mouse (&mev) == SCPE_OK)                   /* poll mouse */
    vs_event (&mev);                                    /* push event */

if (vc_org != vc_last_org)                              /* origin moved? */
    vc_invalidate (0, (VC_YSIZE - 1));                  /* redraw whole screen */

vc_last_org = vc_org;                                   /* store video origin */

lines = 0;
for (ln = 0; ln < VC_YSIZE; ln++) {
    if (vc_updated[ln]) {                               /* line invalid? */
        off = ((ln + (vc_org << VC_ORSC)) << 5) & VC_BUFMASK; /* get video buf offet */
        for (col = 0; col < VC_XSIZE; col++)  
            vc_lines[ln*VC_XSIZE + col] = vc_palette[(vc_buf[off + (col >> 5)] >> (col & 0x1F)) & 1];
                                                        /* 1bpp to 32bpp */
        if (CUR_V &&                                    /* cursor visible && need to draw cursor? */
            (vc_input_captured || (vc_dev.dctrl & DBG_CURSOR))) {
            if ((ln >= CUR_Y) && (ln < (CUR_Y + 16))) { /* cursor on this line? */
                plna = &vc_cur[(CUR_PLNA + ln - CUR_Y)];/* get plane A base */
                plnb = &vc_cur[(CUR_PLNB + ln - CUR_Y)];/* get plane B base */
                for (col = 0; col < 16; col++) {
                    if ((CUR_X + col) >= VC_XSIZE)      /* Part of cursor off screen? */
                        continue;                       /* Skip */
                    if (vc_cmd & CMD_FOPA)              /* force plane A to 1? */
                        bita = 1;
                    else if (vc_cmd & CMD_ENPA)         /* plane A enabled? */
                        bita = (*plna >> col) & 1;
                    else bita = 0;
                    if (vc_cmd & CMD_FOPB)              /* force plane B to 1? */
                        bitb = 1;
                    else if (vc_cmd & CMD_ENPB)         /* plane B enabled? */
                        bitb = (*plnb >> col) & 1;
                    else bitb = 0;
                    vc_lines[ln*VC_XSIZE + CUR_X + col] = vc_palette[((vc_lines[ln*VC_XSIZE + CUR_X + col] == vc_palette[1]) & ~bitb) ^ bita];
                    }
                }
            }
        vc_updated[ln] = FALSE;                         /* set valid */
        if ((ln == (VC_YSIZE-1)) ||                     /* if end of window OR */
            (vc_updated[ln+1] == FALSE)) {              /* next is already valid? */
            vid_draw (0, ln-lines, VC_XSIZE, lines+1, vc_lines+(ln-lines)*VC_XSIZE); /* update region */
            lines = 0;
            }
        else
            lines++;
        updated = TRUE;
        }
    }

if (updated)                                            /* video updated? */
    vid_refresh ();                                     /* put to screen */

SET_INT (VC1);                                          /* VSYNC int */
sim_activate (uptr, tmxr_poll);
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
return SCPE_OK;
}

t_stat vc_reset (DEVICE *dptr)
{
t_stat r;
uint32 i;

CLR_INT (VC1);
sim_cancel (&vc_unit);                                  /* deactivate unit */

vc_cmd = 0;
vc_xpos = 0;
vc_ypos = 0;
vc_xmin1 = 0;
vc_xmax1 = 0;
vc_ymin1 = 0;
vc_ymax1 = 0;
vc_xmin2 = 0;
vc_xmax2 = 0;
vc_ymin2 = 0;
vc_ymax2 = 0;
vc_cur_p = 0;

for (i = 0; i < VC_YSIZE; i++)
    vc_updated[i] = FALSE;

if (dptr->flags & DEV_DIS) {
    if (vc_active) {
        free (vc_buf);
        vc_buf = NULL;
        free (vc_lines);
        vc_lines = NULL;
        vc_active = FALSE;
        return vid_close ();
        }
    else
        return SCPE_OK;
    }

if (!vid_active && !vc_active)  {
    r = vid_open (dptr, NULL, VC_XSIZE, VC_YSIZE, vc_input_captured ? SIM_VID_INPUTCAPTURED : 0); /* display size */
    if (r != SCPE_OK)
        return r;
    vc_buf = (uint32 *) calloc (VC_BUFSIZE, sizeof (uint32));
    if (vc_buf == NULL) {
        vid_close ();
        return SCPE_MEM;
        }
    vc_lines = (uint32 *) calloc (VC_XSIZE * VC_YSIZE, sizeof (uint32));
    if (vc_lines == NULL) {
        free (vc_buf);
        vid_close ();
        return SCPE_MEM;
        }
    vc_palette[0] = vid_map_rgb (0x00, 0x00, 0x00);     /* black */
    vc_palette[1] = vid_map_rgb (0xFF, 0xFF, 0xFF);     /* white */
    sim_printf ("Monochrome Video Display Created.  ");
    vc_show_capture (stdout, NULL, 0, NULL);
    if (sim_log)
        vc_show_capture (sim_log, NULL, 0, NULL);
    sim_printf ("\n");
    vc_active = TRUE;
    }
sim_activate_abs (&vc_unit, tmxr_poll);
return SCPE_OK;
}

t_stat vc_detach (UNIT *uptr)
{
if ((vc_dev.flags & DEV_DIS) == 0) {
    vc_dev.flags |= DEV_DIS;
    vc_reset(&vc_dev);
    }
return SCPE_OK;
}

t_stat vc_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return cpu_set_model (NULL, 0, (val ? "VAXSTATION" : "MICROVAX"), NULL);
}

t_stat vc_set_capture (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (vid_active)
    return sim_messagef (SCPE_ALATT, "Capture Mode Can't be changed with device enabled\n");
vc_input_captured = val;
return SCPE_OK;
}

t_stat vc_show_capture (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (vc_input_captured) {
    fprintf (st, "Captured Input Mode, ");
    vid_show_release_key (st, uptr, val, desc);
    }
else
    fprintf (st, "Uncaptured Input Mode");
return SCPE_OK;
}

t_stat vc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Monochrome Video Subsystem (%s)\n\n", dptr->name);
fprintf (st, "Use the Control-Right-Shift key combination to regain focus from the simulated\n");
fprintf (st, "video display\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *vc_description (DEVICE *dptr)
{
return "Monochrome Graphics Adapter";
}
