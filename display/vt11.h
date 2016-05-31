/*
 * $Id: vt11.h,v 1.8 2005/01/14 18:58:02 phil Exp $
 * interface to VT11 simulator
 * Phil Budne <phil@ultimate.com>
 * September 16, 2003
 * Substantially revised by Douglas A. Gwyn, 14 Jan. 2004
 *
 * prerequisite: display.h
 */

/*
 * Copyright (c) 2003-2004, Philip L. Budne and Douglas A. Gwyn
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

#if defined(__cplusplus)
extern "C" {
#endif
#ifndef SIM_DEFS_H_
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
#endif /* SIM_DEFS_H_ */

/*
 * VT11 jumpers control character spacing; VS60 always uses VT11 normal.
 * The VT11_CSP_{W,H} #defines establish the initial default character
 * spacing; to change the VT11 simulation from these default values,
 * set vt11_csp_{w,h} before calling any function named vt11_*.
 */
extern unsigned char vt11_csp_w;        /* horizontal character spacing */
#ifdef  VT11_NARROW_OPT                 /* W3 or W6 installed */
#define VT11_CSP_W      12
#else   /* VT11 normal;                    W4 or W5 installed */
#define VT11_CSP_W      14
#endif
extern unsigned char vt11_csp_h;        /* vertical character spacing */
#ifdef  VT11_TALL_OPT                   /* W3 or W4 installed */
#define VT11_CSP_H      26
#else   /* VT11 normal;                    W5 or W6 installed */
#define VT11_CSP_H      24
#endif

/*
 * The DISPLAY_TYPE #define establishes the initial default display
 * type; to change from the default display type, set vt11_display
 * before calling any function named vt11_* (other than vt11_reset()).
 */
#ifndef DISPLAY_TYPE
#define DISPLAY_TYPE DIS_VR17           /* default display type */
#endif
extern enum display_type vt11_display;  /* DIS_VR{14,17,48} */
/*
 * The PIX_SCALE #define establishes the initial default display scale
 * factor; to change from the default scale factor, set vt11_scale
 * before calling any function named vt11_* (other than vt11_reset()).
 */
#ifndef PIX_SCALE
#define PIX_SCALE RES_HALF              /* default display scale factor */
#endif
extern int vt11_scale;                  /* RES_{FULL,HALF,QUARTER,EIGHTH} */
/*
 * When vt11_init (READONLY) is nonzero, it indicates that it is too late
 * to change display parameters (type, scale, character spacing, etc.).
 */
extern unsigned char vt11_init;         /* set after display_init() called */

/* vt11.c simulates either a VT11 or a VT48(VS60), according to display type: */
#define VS60    (vt11_display == DIS_VR48)
#define VT11    (!VS60)

/* The display file is an array of 16-bit words. */
typedef uint16 vt11word;

extern int32 vt11_get_dpc(void);        /* read Display PC */
extern int32 vt11_get_mpr(void);        /* read mode parameter register */
extern int32 vt11_get_xpr(void);        /* read graphplot incr/X pos register */
extern int32 vt11_get_ypr(void);        /* read char code/Y pos register */
extern int32 vt11_get_rr(void);         /* read relocate register */
extern int32 vt11_get_spr(void);        /* read status parameter register */
extern int32 vt11_get_xor(void);        /* read X offset register */
extern int32 vt11_get_yor(void);        /* read Y offset register */
extern int32 vt11_get_anr(void);        /* read associative name register */
extern int32 vt11_get_scr(void);        /* read slave console/color register */
extern int32 vt11_get_nr(void);         /* read name register */
extern int32 vt11_get_sdr(void);        /* read stack data register */
extern int32 vt11_get_str(void);        /* read char string term register */
extern int32 vt11_get_sar(void);        /* read stack address/maint register */
extern int32 vt11_get_zpr(void);        /* read Z position register */
extern int32 vt11_get_zor(void);        /* read Z offset register */

extern void vt11_set_dpc(uint16);       /* write Display PC */
extern void vt11_set_mpr(uint16);       /* write mode parameter register */
extern void vt11_set_xpr(uint16);       /* write graphplot inc/X pos register */
extern void vt11_set_ypr(uint16);       /* write char code/Y pos register */
extern void vt11_set_rr(uint16);        /* write relocate register */
extern void vt11_set_spr(uint16);       /* write status parameter register */
extern void vt11_set_xor(uint16);       /* write X offset register */
extern void vt11_set_yor(uint16);       /* write Y offset register */
extern void vt11_set_anr(uint16);       /* write associative name register */
extern void vt11_set_scr(uint16);       /* write slave console/color register */
extern void vt11_set_nr(uint16);        /* write name register */
extern void vt11_set_sdr(uint16);       /* write stack data register */
extern void vt11_set_str(uint16);       /* write char string term register */
extern void vt11_set_sar(uint16);       /* write stack address/maint register */
extern void vt11_set_zpr(uint16);       /* write Z position register */
extern void vt11_set_zor(uint16);       /* write Z offset register */

extern void vt11_reset(void *, int);    /* reset the display processor */
extern int  vt11_cycle(int, int);       /* perform a display processor cycle */

/*
 * callbacks from VT11/VS60 simulator (to SIMH PDP-11 VT driver, for example)
 */
extern int  vt_fetch(uint32, vt11word *);       /* get a display-file word */
extern void vt_stop_intr(void);         /* post a display-stop interrupt */
extern void vt_lpen_intr(void);         /* post a surface-related interrupt */
extern void vt_char_intr(void);         /* post a bad-char./timeout interrupt */
extern void vt_name_intr(void);         /* post a name-match interrupt */

#if defined(__cplusplus)
}
#endif
