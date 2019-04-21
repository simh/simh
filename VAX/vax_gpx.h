/* vax_gpx.h: GPX video common components

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
*/

#ifndef VAX_GPX_H
#define VAX_GPX_H     0

#include "vax_defs.h"

/* FIXME - Some or all of these should be dynamic */

#ifndef VA_PLANES
#define VA_PLANES       4
#endif
#define VA_BPP          (1u << VA_PLANES)
#define VA_PLANE_MASK   (VA_BPP - 1)

#define VA_XSIZE        1024                            /* visible width */
#define VA_YSIZE        864                             /* visible height */
#define VA_BXSIZE       1024                            /* video buffer width */
#define VA_BYSIZE       2048                            /* video buffer height */
#define VA_BUFSIZE      (1u << 21)                      /* video buffer size */
#define VA_BUFMASK      (VA_BUFSIZE - 1)

/* Address processor (Adder) registers */

#define ADP_ADCT        0x0                             /* address counter */
#define ADP_REQ         0x1                             /* request enable */
#define ADP_INT         0x2                             /* interrupt enable */
#define ADP_STAT        0x3                             /* status */
#define ADP_IDD         0x7                             /* I/D data */
#define ADP_CMD1        0x8                             /* command */
#define ADP_MDE         0x9                             /* mode */
#define ADP_CMD2        0xA                             /* command (alt) */
#define ADP_IDS         0xC                             /* I/D scroll data */
#define ADP_ICS         0xD                             /* I/D scroll command */
#define ADP_PXMN        0xE                             /* scroll x min */
#define ADP_PXMX        0xF                             /* scroll x max */
#define ADP_PYMN        0x10                            /* scroll y min */
#define ADP_PYMX        0x11                            /* scroll y max */
#define ADP_PSE         0x12                            /* pause */
#define ADP_PYOF        0x13                            /* y offset */
#define ADP_PYSC        0x14                            /* y scroll constant */
#define ADP_PXI         0x15                            /* pending x index */
#define ADP_PYI         0x16                            /* pending y index */
#define ADP_NXI         0x17                            /* new x index */
#define ADP_NYI         0x18                            /* new y index */
#define ADP_OXI         0x19                            /* old x index */
#define ADP_OYI         0x1A                            /* old y index */
#define ADP_CXMN        0x1B                            /* clip x min */
#define ADP_CXMX        0x1C                            /* clip x max */
#define ADP_CYMN        0x1D                            /* clip y min */
#define ADP_CYMX        0x1E                            /* clip y max */
#define ADP_FSDX        0x20                            /* fast source 1 DX */
#define ADP_SSDY        0x21                            /* slow source 1 DY */
#define ADP_SXO         0x22                            /* source 1 X origin */
#define ADP_SYO         0x23                            /* source 1 Y origin */
#define ADP_DXO         0x24                            /* dest X origin */
#define ADP_DYO         0x25                            /* dest Y origin */
#define ADP_FDX         0x26                            /* fast dest DX */
#define ADP_FDY         0x27                            /* fast dest DY */
#define ADP_SDX         0x28                            /* slow dest DX */
#define ADP_SDY         0x29                            /* slow dest DY */
#define ADP_FS          0x2A                            /* fast scale */
#define ADP_SS          0x2B                            /* slow scale */
#define ADP_S2XO        0x2C                            /* source 2 X origin */
#define ADP_S2YO        0x2D                            /* source 2 Y origin */
#define ADP_S2HW        0x2E                            /* source 2 height/width */
#define ADP_ERR1        0x2F                            /* error 1 */
#define ADP_ERR2        0x30                            /* error 2 */
#define ADP_YCT0        0x31                            /* y scan count 0 */
#define ADP_YCT1        0x32                            /* y scan count 1 */
#define ADP_YCT2        0x33                            /* y scan count 2 */
#define ADP_YCT3        0x34                            /* y scan count 3 */
#define ADP_XCON        0x35                            /* x scan configuration */
#define ADP_XL          0x36                            /* x limit */
#define ADP_YL          0x37                            /* y limit */
#define ADP_XCT0        0x38                            /* x scan count 0 */
#define ADP_XCT1        0x39                            /* x scan count 1 */
#define ADP_XCT2        0x3A                            /* x scan count 2 */
#define ADP_XCT3        0x3B                            /* x scan count 3 */
#define ADP_XCT4        0x3C                            /* x scan count 4 */
#define ADP_XCT5        0x3D                            /* x scan count 5 */
#define ADP_XCT6        0x3E                            /* x scan count 6 */
#define ADP_SYNP        0x3F                            /* sync phase */
#define ADP_MAXREG      0x3F
#define ADP_NUMREG      (ADP_MAXREG + 1)

/* Adder status register */

#define ADPSTAT_PC      0x0001                          /* pause complete */
#define ADPSTAT_SC      0x0002                          /* scroll service */
#define ADPSTAT_IC      0x0004                          /* rasterop init complete */
#define ADPSTAT_RC      0x0008                          /* rasterop complete */
#define ADPSTAT_AC      0x0010                          /* address output complete */
#define ADPSTAT_IRR     0x0020                          /* I/D data rcv ready */
#define ADPSTAT_ITR     0x0040                          /* I/D data xmt ready */
#define ADPSTAT_ISR     0x0080                          /* I/D scroll data ready */
#define ADPSTAT_CT      0x0100                          /* rasterop clipped top */
#define ADPSTAT_CB      0x0200                          /* rasterop clipped bottom */
#define ADPSTAT_CL      0x0400                          /* rasterop clipped left */
#define ADPSTAT_CR      0x0800                          /* rasterop clipped right */
#define ADPSTAT_CP      (ADPSTAT_CT|ADPSTAT_CB|ADPSTAT_CL|ADPSTAT_CR)
#define ADPSTAT_CN      0x1000                          /* rasterop clipped none */
#define ADPSTAT_VB      0x2000                          /* vertical blanking */
#define ADPSTAT_W0C     0x3F83

#define INT_ADP         0                               /* Adder interrupt */

/* Video processor (Viper) registers */

#define VDP_RES         0x0                             /* resolution mode */
#define VDP_BW          0x1                             /* bus width */
#define VDP_SC          0x2                             /* scroll constant */
#define VDP_PA          0x3                             /* plane address */
#define VDP_FNC0        0x4                             /* logic function 0 */
#define VDP_FNC1        0x5                             /* logic function 1 */
#define VDP_FNC2        0x6                             /* logic function 2 */
#define VDP_FNC3        0x7                             /* logic function 3 */
#define VDP_MSK1        0x8                             /* mask 1 */
#define VDP_MSK2        0x9                             /* mask 2 */
#define VDP_SRC         0xA                             /* source */
#define VDP_FILL        0xB                             /* fill */
#define VDP_LSB         0xC                             /* left scroll boundary */
#define VDP_RSB         0xD                             /* right scroll boundary */
#define VDP_BG          0xE                             /* background colour */
#define VDP_FG          0xF                             /* foreground colour */
#define VDP_CSR0        0x10                            /* CSR 0 */
#define VDP_CSR1        0x11                            /* CSR 1 */
#define VDP_CSR2        0x12                            /* CSR 2 */
#define VDP_CSR4        0x14                            /* CSR 4 */
#define VDP_CSR5        0x15                            /* CSR 5 */
#define VDP_CSR6        0x16                            /* CSR 6 */
#define VDP_MAXREG      0x17

#define CMD             u3

#define CMD_NOP         0                               /* no operation */
#define CMD_BTPX        1                               /* bitmap to processor (x-mode) */
#define CMD_BTPZ        2                               /* bitmap to processor (z-mode) */
#define CMD_PTBX        3                               /* processor to bitmap (x-mode) */
#define CMD_PTBZ        4                               /* processor to bitmap (z-mode) */
#define CMD_ROP         5                               /* rasterop */
#define CMD_ERASE       6                               /* erase region */

/* Debugging Bitmaps */

#define DBG_REG         0x0100                          /* register activity */
#define DBG_FIFO        0x0200                          /* fifo activity */
#define DBG_ADP         0x0400                          /* adder activity */
#define DBG_VDP         0x0800                          /* viper activity */
#define DBG_ROP         0x1000                          /* raster operations */
#define DBG_ROM         0x2000                          /* rom reads */

/* Internal functions/data - implemented by vax_gpx.c */

int32 va_adp_rd (int32 rg);
void va_adp_wr (int32 rg, int32 val);
t_stat va_adp_reset (DEVICE *dptr);
t_stat va_adp_svc (UNIT *uptr);
t_stat va_vdp_reset (DEVICE *dptr);

t_stat va_btp (UNIT *uptr, t_bool zmode);
t_stat va_ptb (UNIT *uptr, t_bool zmode);
void va_fifo_wr (uint32 val);
uint32 va_fifo_rd (void);
void va_adpstat (uint32 set, uint32 clr);

extern int32 va_adp[ADP_NUMREG];                        /* Address processor registers */

/* External functions/data - implemented by machine specific device */

extern void va_setint (int32 src);
extern void va_clrint (int32 src);

extern uint32 *va_buf;                                  /* Video memory */
extern t_bool va_updated[VA_BYSIZE];
extern UNIT va_unit[];

#endif
