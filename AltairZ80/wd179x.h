/*************************************************************************
 *                                                                       *
 * Copyright (c) 2007-2022 Howard M. Harte.                              *
 * https://github.com/hharte                                             *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-            *
 * INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE   *
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN       *
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN     *
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE      *
 * SOFTWARE.                                                             *
 *                                                                       *
 * Except as contained in this notice, the names of The Authors shall    *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * from the Authors.                                                     *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     Generic Intel 8272 Disk Controller module for SIMH.               *
 *                                                                       *
 *************************************************************************/

extern t_stat wd179x_attach(UNIT *uptr, CONST char *cptr);
extern t_stat wd179x_detach(UNIT *uptr);
extern uint8 WD179X_Set_DMA(const uint32 dma_addr);
extern uint8 WD179X_Read(const uint32 Addr);
extern uint8 WD179X_Write(const uint32 Addr, uint8 cData);

extern void wd179x_external_restore(void);
extern uint8 wd179x_get_nheads(void);

#define WD179X_FDC_MSR       0   /* R=FDC Main Status Register, W=Drive Select Register */
#define WD179X_FDC_DATA      1   /* R/W FDC Data Register */

#define WD179X_STATUS 0
#define WD179X_TRACK  1
#define WD179X_SECTOR 2
#define WD179X_DATA   3

/* Note: this struct must be kept in sync with WD179X_INFO */
typedef struct {
    PNP_INFO pnp;       /* Plug-n-Play Information */
    uint16 fdctype;     /* Default is 1793 */
    uint8 intenable;    /* Interrupt Enable */
    uint8 intvector;    /* Interrupt Vector */
    uint8 intrq;        /* WD179X Interrupt Request Output (EOJ) */
    uint8 hld;          /* WD179X Head Load Output */
    uint8 drq;          /* WD179X DMA Request Output */
    uint8 ddens;        /* WD179X Double-Density Input */
    uint8 fdc_head;     /* H Head Number */
    uint8 sel_drive;    /* Currently selected drive */
    uint8 drivetype;    /* 8 or 5 depending on disk type. */
} WD179X_INFO_PUB;
