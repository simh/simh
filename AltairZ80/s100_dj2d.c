/*  s100_dj2d.c: Morrow DISK JOCKEY 2D/B Floppy Disk Interface

    Created by Patrick Linstruth (patrick@deltecent.com)

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Patrick Linstruth shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Patrick Linstruth.

    ***************************************************************************
    ** This device simulates the DISK JOCKEY 2D Model B, not the original 2D **
    **                                                                       **
    ** If I can find PROMs and CP/M images for the original 2D, I will add   **
    ** support.                                                              **
    ***************************************************************************

    DJ2D units:

    DJ2D0 - Drive A
    DJ2D1 - Drive B
    DJ2D3 - Drive C
    DJ2D4 - Drive D
    DJ2D5 - Serial Port

*/

/* #define DBG_MSG */

#include "altairz80_defs.h"
#include "sim_imd.h"
#include "sim_tmxr.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern DEVICE *find_dev (const char *cptr);
extern uint32 getClockFrequency(void);
extern void setClockFrequency(const uint32 Value);

#define DJ2D_MAX_ADAPTERS      1
#define DJ2D_MAX_DRIVES        4
#define DJ2D_UNITS             DJ2D_MAX_DRIVES+1
#define DJ2D_SIO_UNIT          DJ2D_UNITS-1
#define DJ2D_TRACKS            77
#define DJ2D_TIMER             1             /* 1ms timer */
#define DJ2D_ROTATION_MS       166           /* 166 milliseconds per revolution */
#define DJ2D_HEAD_TIMEOUT      (DJ2D_ROTATION_MS / DJ2D_TIMER * 6)   /* 6 revolutions */
#define DJ2D_INDEX_TIMEOUT     (DJ2D_ROTATION_MS / DJ2D_TIMER)       /* 1 revolution */
#define DJ2D_BUSY_TIMEOUT      2                                     /* 2 timer ticks */

#define DJ2D_BAUD              19200         /* Default baud rate            */

enum { FMT_SD, FMT_256, FMT_512, FMT_1024, FMT_UNKNOWN };

static uint32 dj2d_image_size[] = {256256, 509184, 587008, 625920, 0};
static uint16 dj2d_sector_len[] = {128, 256, 512, 1024, 0};
static uint16 dj2d_spt[] = {26, 26, 15, 8, 0};
static uint16 dj2d_track_len[] = {5000, 9800, 10300, 9700, 0};

#define DJ2D_MEM_READ         FALSE
#define DJ2D_MEM_WRITE        TRUE

#define DJ2D_PROM_BASE   0xe000
#define DJ2D_PROM_SIZE   1024
#define DJ2D_PROM_MASK   (DJ2D_PROM_SIZE-1)
#define DJ2D_MEM_BASE    DJ2D_PROM_BASE + DJ2D_PROM_SIZE
#define DJ2D_MEM_SIZE    1024                /* Must be on a page boundary */
#define DJ2D_MEM_MASK    (DJ2D_MEM_SIZE-1)

static uint8 dj2d_mem[DJ2D_MEM_SIZE];

/* DJ2D PROM is 1018 bytes following by 8 memory-mapped I/O bytes */

static uint8 dj2d_prom_e000[DJ2D_PROM_SIZE] = {
    0xc3, 0x69, 0xe0, 0xc3, 0xe9, 0xe0, 0xc3, 0xda,
    0xe0, 0xc3, 0x5a, 0xe1, 0xc3, 0x8b, 0xe1, 0xc3,
    0x81, 0xe1, 0xc3, 0x43, 0xe1, 0xc3, 0xdd, 0xe1,
    0xc3, 0xbc, 0xe1, 0xc3, 0x3c, 0xe1, 0xc3, 0xf8,
    0xe0, 0xc3, 0x03, 0xe1, 0xc3, 0x34, 0xe1, 0xc3,
    0x09, 0xe1, 0xc3, 0xc5, 0xe0, 0xc3, 0xb3, 0xe3,
    0xc3, 0xe5, 0xe3, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x31, 0xfa, 0xe7, 0xcd, 0xd2, 0xe3, 0x21,
    0x01, 0x00, 0xe5, 0x2e, 0x09, 0xe5, 0x26, 0xff,
    0xe5, 0xe5, 0xe5, 0xe5, 0x21, 0x08, 0x00, 0xe5,
    0x2e, 0x7e, 0xe5, 0x2e, 0x08, 0xe5, 0x26, 0x18,
    0xe5, 0x3e, 0x7f, 0x32, 0xf9, 0xe3, 0x3e, 0xd0,
    0x32, 0xfc, 0xe3, 0xaf, 0xcd, 0x1b, 0xe3, 0xd2,
    0xa5, 0xe0, 0x3e, 0x01, 0x32, 0xf6, 0xe7, 0xcd,
    0xd2, 0xe3, 0xc3, 0x93, 0xe0, 0x3e, 0x09, 0x32,
    0xf6, 0xe7, 0xcd, 0x96, 0xe3, 0xc1, 0x01, 0x00,
    0xe7, 0xc5, 0xd5, 0x21, 0x00, 0x00, 0xe5, 0x00,
    0xc5, 0x06, 0x0c, 0xc5, 0xcd, 0xdd, 0xe1, 0xc1,
    0xd0, 0x05, 0xc2, 0xbb, 0xe0, 0x0e, 0x09, 0x11,
    0xc3, 0xa2, 0x1b, 0x7a, 0xb3, 0xc2, 0xca, 0xe0,
    0x3e, 0x08, 0xa9, 0x4f, 0x32, 0xfa, 0xe3, 0xc3,
    0xc7, 0xe0, 0x3a, 0xf9, 0xe3, 0xe6, 0x08, 0xc2,
    0xda, 0xe0, 0x79, 0x2f, 0x32, 0xf8, 0xe3, 0x2f,
    0xc9, 0x3a, 0xf9, 0xe3, 0xe6, 0x04, 0xc2, 0xe9,
    0xe0, 0x3a, 0xf8, 0xe3, 0x2f, 0xe6, 0x7f, 0xc9,
    0x3a, 0xf9, 0xe3, 0xe6, 0x04, 0xc0, 0xcd, 0xe9,
    0xe0, 0xb9, 0xc9, 0x3a, 0xf9, 0xe3, 0xe6, 0x04,
    0xc9, 0x21, 0xfd, 0xe3, 0x4e, 0x23, 0x46, 0x3a,
    0xf6, 0xe7, 0x2f, 0xe6, 0x01, 0x0f, 0x57, 0x3a,
    0xf7, 0xe7, 0x07, 0x07, 0x07, 0xb2, 0x57, 0x3a,
    0xe8, 0xe7, 0xee, 0x08, 0x17, 0x17, 0x82, 0x57,
    0x3a, 0xfd, 0xe7, 0x17, 0x17, 0xb2, 0x57, 0x3a,
    0xec, 0xe7, 0x82, 0xc9, 0xe5, 0x2a, 0xe6, 0xe7,
    0x44, 0x4d, 0xe1, 0xc9, 0x79, 0xe6, 0x03, 0x32,
    0xeb, 0xe7, 0xc9, 0x21, 0x00, 0x1c, 0x09, 0xda,
    0x54, 0xe1, 0x21, 0x08, 0x20, 0x09, 0xd2, 0x54,
    0xe1, 0x3e, 0x10, 0xc9, 0x60, 0x69, 0x22, 0xe6,
    0xe7, 0xc9, 0xcd, 0xe3, 0xe2, 0xd8, 0xcd, 0x70,
    0xe1, 0xf5, 0x9f, 0x32, 0xf9, 0xe7, 0x32, 0xfd,
    0xe3, 0xaf, 0x32, 0xed, 0xe7, 0xc3, 0x23, 0xe2,
    0xaf, 0x32, 0xe9, 0xe7, 0x21, 0x00, 0x00, 0x3e,
    0x09, 0xcd, 0x62, 0xe3, 0xe6, 0x04, 0xc0, 0x37,
    0xc9, 0xaf, 0xb1, 0x37, 0xc8, 0xe6, 0x3f, 0x32,
    0xf8, 0xe7, 0xc9, 0x79, 0xfe, 0x4d, 0x3f, 0xd8,
    0x32, 0xf9, 0xe7, 0xc9, 0x32, 0xe3, 0xe7, 0xcd,
    0x96, 0xe3, 0x0e, 0x01, 0x79, 0x32, 0xfe, 0xe3,
    0x3a, 0xf8, 0xe7, 0xb9, 0xc8, 0x3e, 0x80, 0xcd,
    0x5d, 0xe3, 0xda, 0x20, 0xe2, 0x0c, 0xc3, 0x9c,
    0xe1, 0x32, 0xfc, 0xe3, 0x48, 0x11, 0xff, 0xe3,
    0x2a, 0xe6, 0xe7, 0xc9, 0xcd, 0x33, 0xe2, 0xda,
    0x22, 0xe2, 0x3e, 0xa0, 0xcd, 0xb1, 0xe1, 0x7e,
    0x23, 0x12, 0x7e, 0x23, 0x12, 0x7e, 0x23, 0x12,
    0x0d, 0x7e, 0x23, 0x12, 0xc2, 0xc7, 0xe1, 0x21,
    0xc2, 0xe1, 0xc3, 0xfb, 0xe1, 0xcd, 0x33, 0xe2,
    0xda, 0x22, 0xe2, 0x3e, 0x80, 0xcd, 0xb1, 0xe1,
    0x1a, 0x77, 0x23, 0x1a, 0x77, 0x23, 0x1a, 0x77,
    0x23, 0x0d, 0x1a, 0x77, 0x23, 0xc2, 0xe8, 0xe1,
    0x21, 0xe3, 0xe1, 0xe5, 0x21, 0xfc, 0xe3, 0xcd,
    0x6c, 0xe3, 0xe6, 0x5f, 0xca, 0x21, 0xe2, 0xfe,
    0x10, 0xc2, 0x20, 0xe2, 0x3a, 0xe2, 0xe7, 0x3d,
    0xfa, 0x17, 0xe2, 0x32, 0xe2, 0xe7, 0xc9, 0x3a,
    0xe3, 0xe7, 0x3d, 0xf2, 0x94, 0xe1, 0x3e, 0x10,
    0x37, 0xe1, 0xf5, 0x3a, 0xf6, 0xe7, 0xee, 0x04,
    0x32, 0xfa, 0xe3, 0x3a, 0xea, 0xe7, 0x32, 0xf9,
    0xe3, 0xf1, 0xc9, 0xcd, 0xe3, 0xe2, 0xd8, 0x3a,
    0xfd, 0xe3, 0x3c, 0xcc, 0x70, 0xe1, 0xd8, 0x21,
    0xfd, 0xe3, 0x3a, 0xf9, 0xe7, 0xbe, 0x23, 0x23,
    0x77, 0x79, 0x32, 0xf9, 0xe3, 0xca, 0x6a, 0xe2,
    0xaf, 0x32, 0xe9, 0xe7, 0x3a, 0xfa, 0xe3, 0xe6,
    0x08, 0x32, 0xe8, 0xe7, 0x1f, 0x1f, 0x1f, 0xc6,
    0x18, 0x21, 0x00, 0x00, 0xcd, 0x62, 0xe3, 0xda,
    0x8e, 0xe2, 0x3a, 0xe9, 0xe7, 0xb7, 0xc2, 0xb9,
    0xe2, 0x06, 0x02, 0x3e, 0x1d, 0xcd, 0x5d, 0xe3,
    0xe6, 0x99, 0x57, 0xca, 0x95, 0xe2, 0x3a, 0xf6,
    0xe7, 0xee, 0x01, 0x32, 0xf6, 0xe7, 0x32, 0xfa,
    0xe3, 0x05, 0xc2, 0x73, 0xe2, 0x7a, 0x37, 0xf5,
    0xcd, 0x70, 0xe1, 0xf1, 0xc9, 0x06, 0x0a, 0x11,
    0xff, 0xe3, 0x21, 0xfa, 0xe7, 0x3e, 0xc4, 0x32,
    0xfc, 0xe3, 0x1a, 0x77, 0x2c, 0xc2, 0xa2, 0xe2,
    0x21, 0xfc, 0xe3, 0xcd, 0x6c, 0xe3, 0xb7, 0xca,
    0xb9, 0xe2, 0x05, 0xc2, 0x97, 0xe2, 0xc3, 0x8e,
    0xe2, 0x3a, 0xfd, 0xe7, 0x4f, 0x06, 0x00, 0x21,
    0xdf, 0xe2, 0x09, 0x3a, 0xf8, 0xe7, 0x47, 0x86,
    0x3e, 0x10, 0xd8, 0x78, 0x32, 0xfe, 0xe3, 0x3e,
    0x20, 0x21, 0x05, 0x05, 0x22, 0xe2, 0xe7, 0x0d,
    0x47, 0xf8, 0x17, 0xb7, 0xc3, 0xd7, 0xe2, 0xd5,
    0xd5, 0xf0, 0xf7, 0x21, 0xeb, 0xe7, 0x4e, 0x23,
    0x5e, 0x71, 0x23, 0x7b, 0xb9, 0x7e, 0x36, 0x01,
    0xca, 0x1b, 0xe3, 0x23, 0xe5, 0x16, 0x00, 0x42,
    0x19, 0x19, 0x3a, 0xf6, 0xe7, 0x77, 0x23, 0x11,
    0xfd, 0xe3, 0x1a, 0x77, 0xe1, 0x09, 0x09, 0x7e,
    0x32, 0xf6, 0xe7, 0x23, 0x7e, 0x12, 0x3e, 0x7f,
    0x07, 0x0d, 0xf2, 0x10, 0xe3, 0xe6, 0x7f, 0x32,
    0xea, 0xe7, 0xaf, 0x21, 0xfa, 0xe3, 0xa6, 0x32,
    0xe9, 0xe7, 0xf5, 0x3a, 0xea, 0xe7, 0x4f, 0x3a,
    0xf7, 0xe7, 0x2f, 0xa1, 0x32, 0xf9, 0xe3, 0xee,
    0x40, 0x4f, 0x3a, 0xf6, 0xe7, 0x47, 0x3a, 0xf9,
    0xe7, 0xd6, 0x01, 0x9f, 0x3d, 0x2f, 0xb0, 0x77,
    0xf1, 0xc2, 0x4f, 0xe3, 0xe5, 0x2a, 0xe4, 0xe7,
    0x2b, 0x7c, 0xb5, 0xc2, 0x48, 0xe3, 0xe1, 0x7e,
    0xe6, 0x80, 0xc0, 0x3a, 0xf6, 0xe7, 0xf6, 0x06,
    0x77, 0x3e, 0x80, 0x37, 0xc9, 0x2a, 0xe4, 0xe7,
    0x29, 0x29, 0xeb, 0x21, 0xfc, 0xe3, 0x77, 0x7e,
    0x1f, 0xd2, 0x67, 0xe3, 0x7e, 0x1f, 0x7e, 0xd0,
    0xc3, 0x76, 0xe3, 0xc3, 0xe3, 0xe2, 0x1b, 0x7a,
    0xb3, 0xc2, 0x6c, 0xe3, 0x5e, 0xe5, 0x23, 0x56,
    0x3a, 0xea, 0xe7, 0xee, 0x80, 0x32, 0xf9, 0xe3,
    0xee, 0xc0, 0xe3, 0x32, 0xf9, 0xe3, 0x36, 0xd0,
    0xe3, 0x72, 0xe1, 0x7b, 0x37, 0xc9, 0x11, 0x00,
    0x00, 0x21, 0xfa, 0xe3, 0x0e, 0x10, 0x7e, 0xa1,
    0xca, 0x9e, 0xe3, 0x7e, 0xa1, 0xc2, 0xa3, 0xe3,
    0x13, 0xe3, 0xe3, 0xe3, 0xe3, 0x7e, 0xa1, 0xca,
    0xa8, 0xe3, 0xc9, 0x79, 0xe6, 0x01, 0x2f, 0x47,
    0x21, 0xeb, 0xe7, 0x5e, 0x16, 0x00, 0x23, 0x7e,
    0xab, 0xf5, 0x23, 0x23, 0x19, 0x19, 0x7e, 0xf6,
    0x01, 0xa0, 0x77, 0xf1, 0xc0, 0x7e, 0x32, 0xf6,
    0xe7, 0xc9, 0x21, 0x00, 0x00, 0x2b, 0x7c, 0xb5,
    0xe3, 0xe3, 0xc2, 0xd5, 0xe3, 0xc9, 0xe5, 0x21,
    0xe2, 0xe3, 0xe9, 0xe1, 0xc9, 0x79, 0xe6, 0x01,
    0x17, 0x17, 0x17, 0x17, 0x32, 0xf7, 0xe7, 0xc9,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x00, 0xe0,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

static uint8 dj2d_prom_f800[DJ2D_PROM_SIZE] = {
    0xc3, 0x69, 0xf8, 0xc3, 0xe9, 0xf8, 0xc3, 0xda,
    0xf8, 0xc3, 0x5a, 0xf9, 0xc3, 0x8b, 0xf9, 0xc3,
    0x81, 0xf9, 0xc3, 0x43, 0xf9, 0xc3, 0xdd, 0xf9,
    0xc3, 0xbc, 0xf9, 0xc3, 0x3c, 0xf9, 0xc3, 0xf8,
    0xf8, 0xc3, 0x03, 0xf9, 0xc3, 0x34, 0xf9, 0xc3,
    0x09, 0xf9, 0xc3, 0xc5, 0xf8, 0xc3, 0xb3, 0xfb,
    0xc3, 0xe5, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x31, 0xfa, 0xff, 0xcd, 0xd2, 0xfb, 0x21,
    0x01, 0x00, 0xe5, 0x2e, 0x09, 0xe5, 0x26, 0xff,
    0xe5, 0xe5, 0xe5, 0xe5, 0x21, 0x08, 0x00, 0xe5,
    0x2e, 0x7e, 0xe5, 0x2e, 0x08, 0xe5, 0x26, 0x18,
    0xe5, 0x3e, 0x7f, 0x32, 0xf9, 0xfb, 0x3e, 0xd0,
    0x32, 0xfc, 0xfb, 0xaf, 0xcd, 0x1b, 0xfb, 0xd2,
    0xa5, 0xf8, 0x3e, 0x01, 0x32, 0xf6, 0xff, 0xcd,
    0xd2, 0xfb, 0xc3, 0x93, 0xf8, 0x3e, 0x09, 0x32,
    0xf6, 0xff, 0xcd, 0x96, 0xfb, 0xc1, 0x01, 0x00,
    0xff, 0xc5, 0xd5, 0x21, 0x00, 0x00, 0xe5, 0x00,
    0xc5, 0x06, 0x0c, 0xc5, 0xcd, 0xdd, 0xf9, 0xc1,
    0xd0, 0x05, 0xc2, 0xbb, 0xf8, 0x0e, 0x09, 0x11,
    0xc3, 0xa2, 0x1b, 0x7a, 0xb3, 0xc2, 0xca, 0xf8,
    0x3e, 0x08, 0xa9, 0x4f, 0x32, 0xfa, 0xfb, 0xc3,
    0xc7, 0xf8, 0x3a, 0xf9, 0xfb, 0xe6, 0x08, 0xc2,
    0xda, 0xf8, 0x79, 0x2f, 0x32, 0xf8, 0xfb, 0x2f,
    0xc9, 0x3a, 0xf9, 0xfb, 0xe6, 0x04, 0xc2, 0xe9,
    0xf8, 0x3a, 0xf8, 0xfb, 0x2f, 0xe6, 0x7f, 0xc9,
    0x3a, 0xf9, 0xfb, 0xe6, 0x04, 0xc0, 0xcd, 0xe9,
    0xf8, 0xb9, 0xc9, 0x3a, 0xf9, 0xfb, 0xe6, 0x04,
    0xc9, 0x21, 0xfd, 0xfb, 0x4e, 0x23, 0x46, 0x3a,
    0xf6, 0xff, 0x2f, 0xe6, 0x01, 0x0f, 0x57, 0x3a,
    0xf7, 0xff, 0x07, 0x07, 0x07, 0xb2, 0x57, 0x3a,
    0xe8, 0xff, 0xee, 0x08, 0x17, 0x17, 0x82, 0x57,
    0x3a, 0xfd, 0xff, 0x17, 0x17, 0xb2, 0x57, 0x3a,
    0xec, 0xff, 0x82, 0xc9, 0xe5, 0x2a, 0xe6, 0xff,
    0x44, 0x4d, 0xe1, 0xc9, 0x79, 0xe6, 0x03, 0x32,
    0xeb, 0xff, 0xc9, 0x21, 0x00, 0x04, 0x09, 0xda,
    0x54, 0xf9, 0x21, 0x08, 0x08, 0x09, 0xd2, 0x54,
    0xf9, 0x3e, 0x10, 0xc9, 0x60, 0x69, 0x22, 0xe6,
    0xff, 0xc9, 0xcd, 0xe3, 0xfa, 0xd8, 0xcd, 0x70,
    0xf9, 0xf5, 0x9f, 0x32, 0xf9, 0xff, 0x32, 0xfd,
    0xfb, 0xaf, 0x32, 0xed, 0xff, 0xc3, 0x23, 0xfa,
    0xaf, 0x32, 0xe9, 0xff, 0x21, 0x00, 0x00, 0x3e,
    0x09, 0xcd, 0x62, 0xfb, 0xe6, 0x04, 0xc0, 0x37,
    0xc9, 0xaf, 0xb1, 0x37, 0xc8, 0xe6, 0x3f, 0x32,
    0xf8, 0xff, 0xc9, 0x79, 0xfe, 0x4d, 0x3f, 0xd8,
    0x32, 0xf9, 0xff, 0xc9, 0x32, 0xe3, 0xff, 0xcd,
    0x96, 0xfb, 0x0e, 0x01, 0x79, 0x32, 0xfe, 0xfb,
    0x3a, 0xf8, 0xff, 0xb9, 0xc8, 0x3e, 0x80, 0xcd,
    0x5d, 0xfb, 0xda, 0x20, 0xfa, 0x0c, 0xc3, 0x9c,
    0xf9, 0x32, 0xfc, 0xfb, 0x48, 0x11, 0xff, 0xfb,
    0x2a, 0xe6, 0xff, 0xc9, 0xcd, 0x33, 0xfa, 0xda,
    0x22, 0xfa, 0x3e, 0xa0, 0xcd, 0xb1, 0xf9, 0x7e,
    0x23, 0x12, 0x7e, 0x23, 0x12, 0x7e, 0x23, 0x12,
    0x0d, 0x7e, 0x23, 0x12, 0xc2, 0xc7, 0xf9, 0x21,
    0xc2, 0xf9, 0xc3, 0xfb, 0xf9, 0xcd, 0x33, 0xfa,
    0xda, 0x22, 0xfa, 0x3e, 0x80, 0xcd, 0xb1, 0xf9,
    0x1a, 0x77, 0x23, 0x1a, 0x77, 0x23, 0x1a, 0x77,
    0x23, 0x0d, 0x1a, 0x77, 0x23, 0xc2, 0xe8, 0xf9,
    0x21, 0xe3, 0xf9, 0xe5, 0x21, 0xfc, 0xfb, 0xcd,
    0x6c, 0xfb, 0xe6, 0x5f, 0xca, 0x21, 0xfa, 0xfe,
    0x10, 0xc2, 0x20, 0xfa, 0x3a, 0xe2, 0xff, 0x3d,
    0xfa, 0x17, 0xfa, 0x32, 0xe2, 0xff, 0xc9, 0x3a,
    0xe3, 0xff, 0x3d, 0xf2, 0x94, 0xf9, 0x3e, 0x10,
    0x37, 0xe1, 0xf5, 0x3a, 0xf6, 0xff, 0xee, 0x04,
    0x32, 0xfa, 0xfb, 0x3a, 0xea, 0xff, 0x32, 0xf9,
    0xfb, 0xf1, 0xc9, 0xcd, 0xe3, 0xfa, 0xd8, 0x3a,
    0xfd, 0xfb, 0x3c, 0xcc, 0x70, 0xf9, 0xd8, 0x21,
    0xfd, 0xfb, 0x3a, 0xf9, 0xff, 0xbe, 0x23, 0x23,
    0x77, 0x79, 0x32, 0xf9, 0xfb, 0xca, 0x6a, 0xfa,
    0xaf, 0x32, 0xe9, 0xff, 0x3a, 0xfa, 0xfb, 0xe6,
    0x08, 0x32, 0xe8, 0xff, 0x1f, 0x1f, 0x1f, 0xc6,
    0x18, 0x21, 0x00, 0x00, 0xcd, 0x62, 0xfb, 0xda,
    0x8e, 0xfa, 0x3a, 0xe9, 0xff, 0xb7, 0xc2, 0xb9,
    0xfa, 0x06, 0x02, 0x3e, 0x1d, 0xcd, 0x5d, 0xfb,
    0xe6, 0x99, 0x57, 0xca, 0x95, 0xfa, 0x3a, 0xf6,
    0xff, 0xee, 0x01, 0x32, 0xf6, 0xff, 0x32, 0xfa,
    0xfb, 0x05, 0xc2, 0x73, 0xfa, 0x7a, 0x37, 0xf5,
    0xcd, 0x70, 0xf9, 0xf1, 0xc9, 0x06, 0x0a, 0x11,
    0xff, 0xfb, 0x21, 0xfa, 0xff, 0x3e, 0xc4, 0x32,
    0xfc, 0xfb, 0x1a, 0x77, 0x2c, 0xc2, 0xa2, 0xfa,
    0x21, 0xfc, 0xfb, 0xcd, 0x6c, 0xfb, 0xb7, 0xca,
    0xb9, 0xfa, 0x05, 0xc2, 0x97, 0xfa, 0xc3, 0x8e,
    0xfa, 0x3a, 0xfd, 0xff, 0x4f, 0x06, 0x00, 0x21,
    0xdf, 0xfa, 0x09, 0x3a, 0xf8, 0xff, 0x47, 0x86,
    0x3e, 0x10, 0xd8, 0x78, 0x32, 0xfe, 0xfb, 0x3e,
    0x20, 0x21, 0x05, 0x05, 0x22, 0xe2, 0xff, 0x0d,
    0x47, 0xf8, 0x17, 0xb7, 0xc3, 0xd7, 0xfa, 0xd5,
    0xd5, 0xf0, 0xf7, 0x21, 0xeb, 0xff, 0x4e, 0x23,
    0x5e, 0x71, 0x23, 0x7b, 0xb9, 0x7e, 0x36, 0x01,
    0xca, 0x1b, 0xfb, 0x23, 0xe5, 0x16, 0x00, 0x42,
    0x19, 0x19, 0x3a, 0xf6, 0xff, 0x77, 0x23, 0x11,
    0xfd, 0xfb, 0x1a, 0x77, 0xe1, 0x09, 0x09, 0x7e,
    0x32, 0xf6, 0xff, 0x23, 0x7e, 0x12, 0x3e, 0x7f,
    0x07, 0x0d, 0xf2, 0x10, 0xfb, 0xe6, 0x7f, 0x32,
    0xea, 0xff, 0xaf, 0x21, 0xfa, 0xfb, 0xa6, 0x32,
    0xe9, 0xff, 0xf5, 0x3a, 0xea, 0xff, 0x4f, 0x3a,
    0xf7, 0xff, 0x2f, 0xa1, 0x32, 0xf9, 0xfb, 0xee,
    0x40, 0x4f, 0x3a, 0xf6, 0xff, 0x47, 0x3a, 0xf9,
    0xff, 0xd6, 0x01, 0x9f, 0x3d, 0x2f, 0xb0, 0x77,
    0xf1, 0xc2, 0x4f, 0xfb, 0xe5, 0x2a, 0xe4, 0xff,
    0x2b, 0x7c, 0xb5, 0xc2, 0x48, 0xfb, 0xe1, 0x7e,
    0xe6, 0x80, 0xc0, 0x3a, 0xf6, 0xff, 0xf6, 0x06,
    0x77, 0x3e, 0x80, 0x37, 0xc9, 0x2a, 0xe4, 0xff,
    0x29, 0x29, 0xeb, 0x21, 0xfc, 0xfb, 0x77, 0x7e,
    0x1f, 0xd2, 0x67, 0xfb, 0x7e, 0x1f, 0x7e, 0xd0,
    0xc3, 0x76, 0xfb, 0xc3, 0xe3, 0xfa, 0x1b, 0x7a,
    0xb3, 0xc2, 0x6c, 0xfb, 0x5e, 0xe5, 0x23, 0x56,
    0x3a, 0xea, 0xff, 0xee, 0x80, 0x32, 0xf9, 0xfb,
    0xee, 0xc0, 0xe3, 0x32, 0xf9, 0xfb, 0x36, 0xd0,
    0xe3, 0x72, 0xe1, 0x7b, 0x37, 0xc9, 0x11, 0x00,
    0x00, 0x21, 0xfa, 0xfb, 0x0e, 0x10, 0x7e, 0xa1,
    0xca, 0x9e, 0xfb, 0x7e, 0xa1, 0xc2, 0xa3, 0xfb,
    0x13, 0xe3, 0xe3, 0xe3, 0xe3, 0x7e, 0xa1, 0xca,
    0xa8, 0xfb, 0xc9, 0x79, 0xe6, 0x01, 0x2f, 0x47,
    0x21, 0xeb, 0xff, 0x5e, 0x16, 0x00, 0x23, 0x7e,
    0xab, 0xf5, 0x23, 0x23, 0x19, 0x19, 0x7e, 0xf6,
    0x01, 0xa0, 0x77, 0xf1, 0xc0, 0x7e, 0x32, 0xf6,
    0xff, 0xc9, 0x21, 0x00, 0x00, 0x2b, 0x7c, 0xb5,
    0xe3, 0xe3, 0xc2, 0xd5, 0xfb, 0xc9, 0xe5, 0x21,
    0xe2, 0xfb, 0xe9, 0xe1, 0xc9, 0x79, 0xe6, 0x01,
    0x17, 0x17, 0x17, 0x17, 0x32, 0xf7, 0xff, 0xc9,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x00, 0xf8,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

/* PROM selection (default E000) */
uint8 *dj2d_prom = dj2d_prom_e000;

/*
** Western Digital WD1791 Registers and Interface Controls
*/
typedef struct {
    uint8   track;          /* Track Register */
    uint8   sector;         /* Sector Register */
    uint8   command;        /* Command Register */
    uint8   status;         /* Status Register */
    uint8   data;           /* Data Register */
    uint8   intrq;          /* Interrupt Request */
    uint8   drq;            /* Data Request */
    uint8   index;          /* Index */
    int8    stepDir;        /* Last Step Direction */
    uint32  dataCount;      /* Number of data bytes transferred from controller for current sector/address */
    uint32  trkCount;       /* Number of data bytes transferred from controller for current track */
    uint8   readActive;     /* Read Active */
    uint8   readTrkActive;  /* Read Track Active */
    uint8   writeActive;    /* Write Active */
    uint8   writeTrkActive; /* Write Track Active */
    uint8   idAddrMrk;      /* ID Addr Mark Flag */
    uint8   dataAddrMrk;    /* Data Addr Mark Flag */
    uint8   addrActive;     /* Address Active */
} WD1791_REG;

/*
** Disk Jockey 2D Registers
*/
typedef struct {
    uint8   uart_rxd;       /* UART rx data register */
    uint8   uart_txd;       /* UART tx data register */
    uint8   uart_txp;       /* UART tx data pending */
    uint8   uart_status;    /* UART status register */
    uint16  uart_baud;      /* UART baud rate */
    uint8   status;         /* Disk Jockey status register */
    uint8   control;        /* Disk Jockey control register */
    uint8   function;       /* Disk Jockey function register */
} DJ2D_REG;

#define WD1791_STAT_NOTREADY   0x80
#define WD1791_STAT_WRITEPROT  0x40
#define WD1791_STAT_RTYPEMSB   0x40
#define WD1791_STAT_HEADLOAD   0x20
#define WD1791_STAT_RTYPELSB   0x20
#define WD1791_STAT_WRITEFAULT 0x20
#define WD1791_STAT_SEEKERROR  0x10
#define WD1791_STAT_NOTFOUND   0x10
#define WD1791_STAT_CRCERROR   0x08
#define WD1791_STAT_TRACK0     0x04
#define WD1791_STAT_LOSTDATA   0x04
#define WD1791_STAT_INDEX      0x02
#define WD1791_STAT_DRQ        0x02
#define WD1791_STAT_BUSY       0x01

static TMLN dj2d_tmln[] = {         /* line descriptors */
    { 0 }
};

static TMXR dj2d_tmxr = {           /* multiplexer descriptor */
    1,                              /* number of terminal lines */
    0,                              /* listening port (reserved) */
    0,                              /* master socket  (reserved) */
    dj2d_tmln,                      /* line descriptor array */
    NULL,                           /* line connection order */
    NULL                            /* multiplexer device (derived internally) */
};

typedef struct {
    uint32     io_base;        /* NOT USED */
    uint32     io_size;        /* NOT USED */
    uint32     mem_base;       /* Memory Base Address */
    uint32     mem_size;       /* Memory Address space requirement */
    uint32     prom_base;      /* PROM Base Address */
    uint32     prom_size;      /* PROM Address space requirement */
    int32      conn;           /* Connected Status */
    TMLN      *tmln;           /* TMLN pointer     */
    TMXR      *tmxr;           /* TMXR pointer     */
    uint32     ticks;          /* Timer ticks */
    uint32     sioticks;       /* SIO Timer ticks */
    uint16     headTimeout;    /* Head unload timer tick value */
    uint16     indexTimeout;   /* Index timer tick value */
    uint16     busyTimeout;    /* Busy timer tick value */
    uint8      promEnabled;    /* PROM is enabled */
    uint8      writeProtect;   /* Write Protect is enabled */
    uint8      currentDrive;   /* currently selected drive */
    uint8      secsPerTrack;   /* sectors per track */
    uint16     bytesPerTrack;  /* bytes per track */
    uint8      headLoaded[DJ2D_MAX_DRIVES];     /* Head Loaded */
    uint8      format[DJ2D_MAX_DRIVES];         /* Attached disk format */
    uint16     sectorLen[DJ2D_MAX_DRIVES];      /* Attached disk sector length */
    uint8      side[DJ2D_MAX_DRIVES];           /* side 0 or side 1 */
    WD1791_REG WD1791;         /* WD1791 Registers and Data */
    DJ2D_REG   DJ2D;           /* DJ2D Registers and Data */
    UNIT      *uptr[DJ2D_UNITS];
} DJ2D_INFO;

static DJ2D_INFO dj2d_info_data = {
    0, 0,
    DJ2D_MEM_BASE, DJ2D_MEM_SIZE,
    DJ2D_PROM_BASE, DJ2D_PROM_SIZE,
    0, dj2d_tmln, &dj2d_tmxr,
    0, 0, 0
};

static DJ2D_INFO *dj2d_info = &dj2d_info_data;

static uint8 sdata[1024];       /* Sector data buffer */

/* DJ2D Registers */
#define DJ2D_REG_BASE           (DJ2D_PROM_BASE + 0x03f8)
#define DJ2D_REG_UART_DATA      0x00  /* Register 0 */
#define DJ2D_REG_UART_STATUS    0x01  /* Register 1 */
#define DJ2D_REG_2D_CONTROL     0x01  /* Register 1 */
#define DJ2D_REG_2D_FUNCTION    0x02  /* Register 2 */
#define DJ2D_REG_2D_STATUS      0x02  /* Register 2 */
#define DJ2D_REG_1791_STATUS    0x04  /* Register 4 */
#define DJ2D_REG_1791_COMMAND   0x04  /* Register 4 */
#define DJ2D_REG_1791_TRACK     0x05  /* Register 5 */
#define DJ2D_REG_1791_SECTOR    0x06  /* Register 6 */
#define DJ2D_REG_1791_DATA      0x07  /* Register 7 */

#define DJ2D_STAT_HEAD          0x01  /* HEAD        */
#define DJ2D_STAT_DATARQ        0x02  /* DATARQ      */
#define DJ2D_STAT_INTRQ         0x04  /* INTRQ       */
#define DJ2D_STAT_N2SIDED       0x08  /* NOT 2 SIDED */
#define DJ2D_STAT_INDEX         0x10  /* INDEX       */
#define DJ2D_STAT_READY         0x80  /* READY       */

#define DJ2D_STAT_PE            0x01  /* Parity Error  */
#define DJ2D_STAT_OE            0x02  /* Overrun Error */
#define DJ2D_STAT_DR            0x04  /* Data Ready    */
#define DJ2D_STAT_TBRE          0x08  /* TBRE          */
#define DJ2D_STAT_FE            0x10  /* Framing Error */

#define DJ2D_CTRL_DSEL          0x0f  /* Drive Select Mask */
#define DJ2D_CTRL_SIDE0         0x10  /* Side 0 Select     */
#define DJ2D_CTRL_INTDSBL       0x20  /* Interrupt Disable */
#define DJ2D_CTRL_AENBL         0x40  /* AENBL             */
#define DJ2D_CTRL_RESET         0x80  /* Reset 1791        */

#define DJ2D_FUNC_SINGLE        0x01  /* Single Density */
#define DJ2D_FUNC_HDMASK        0x06  /* Head Mask      */
#define DJ2D_FUNC_HDLOAD        0x00  /* Head Loaded    */
#define DJ2D_FUNC_HDUNLD        0x06  /* Head Unloaded  */
#define DJ2D_FUNC_LEDOFF        0x08  /* LED Off        */
#define DJ2D_FUNC_VCOFF         0x20  /* VCO Off        */

/* DJ2D Commands */
#define WD1791_CMD_RESTORE        0x00
#define WD1791_CMD_SEEK           0x10
#define WD1791_CMD_STEP           0x20
#define WD1791_CMD_STEPU          (WD1791_CMD_STEP | WD1791_FLAG_U)
#define WD1791_CMD_STEPIN         0x40
#define WD1791_CMD_STEPINU        (WD1791_CMD_STEPIN | WD1791_FLAG_U)
#define WD1791_CMD_STEPOUT        0x60
#define WD1791_CMD_STEPOUTU       (WD1791_CMD_STEPOUT | WD1791_FLAG_U)
#define WD1791_CMD_READ           0x80
#define WD1791_CMD_READM          (WD1791_CMD_READ | WD1791_FLAG_M)
#define WD1791_CMD_WRITE          0xA0
#define WD1791_CMD_WRITEM         (WD1791_CMD_WRITE | WD1791_FLAG_M)
#define WD1791_CMD_READ_ADDRESS   0xC0
#define WD1791_CMD_READ_TRACK     0xE0
#define WD1791_CMD_WRITE_TRACK    0xF0
#define WD1791_CMD_FORCE_INTR     0xD0

#define WD1791_FLAG_V             0x04
#define WD1791_FLAG_H             0x08
#define WD1791_FLAG_U             0x10
#define WD1791_FLAG_M             0x10
#define WD1791_FLAG_B             0x08
#define WD1791_FLAG_S             0x01
#define WD1791_FLAG_E             0x04

#define WD1791_FLAG_A1A0_FB       0x00
#define WD1791_FLAG_A1A0_FA       0x01
#define WD1791_FLAG_A1A0_F9       0x02
#define WD1791_FLAG_A1A0_F8       0x03

#define WD1791_FLAG_I0            0x01
#define WD1791_FLAG_I1            0x02
#define WD1791_FLAG_I2            0x04
#define WD1791_FLAG_I3            0x08

#define WD1791_FLAG_R1R0_6MS      0x00
#define WD1791_FLAG_R1R0_10ms     0x02
#define WD1791_FLAG_R1R0_20ms     0x03

#define WD1791_ADDR_TRACK         0x00
#define WD1791_ADDR_ZEROS         0x01
#define WD1791_ADDR_SECTOR        0x02
#define WD1791_ADDR_LENGTH        0x03
#define WD1791_ADDR_CRC1          0x04
#define WD1791_ADDR_CRC2          0x05

/* Local function prototypes */
static t_stat dj2d_reset(DEVICE *dj2d_dev);
static t_stat dj2d_svc(UNIT *uptr);
static t_stat dj2d_sio_svc(UNIT *uptr);
static t_stat dj2d_attach(UNIT *uptr, CONST char *cptr);
static t_stat dj2d_detach(UNIT *uptr);
static t_stat dj2d_boot(int32 unitno, DEVICE *dptr);
static t_stat dj2d_set_prombase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat dj2d_show_prombase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat dj2d_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat dj2d_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat dj2d_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat dj2d_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat dj2d_config_line(void);
static uint16 sector_len(uint8 drive, uint8 track);
static uint32 secs_per_track(uint8 track);
static uint32 bytes_per_track(uint8 track);
static uint32 calculate_dj2d_sec_offset(uint8 track, uint8 sector);
static void DJ2D_HeadLoad(UNIT *uptr, WD1791_REG *pWD1791, uint8 load);
static uint8 DJ2D_Read(uint32 Addr);
static uint8 DJ2D_Write(uint32 Addr, int32 data);
static const char * DJ2D_CommandString(uint8 command);
static uint8 DJ2D_Command(UNIT *uptr, WD1791_REG *pWD1791, int32 data);
static uint32 DJ2D_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static uint32 DJ2D_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static const char* dj2d_description(DEVICE *dptr);
static void showdata(int32 isRead);

static int32 dj2dprom(int32 Addr, int32 rw, int32 data);
static int32 dj2dmem(int32 Addr, int32 rw, int32 data);

static UNIT dj2d_unit[DJ2D_UNITS] = {
    { UDATA (dj2d_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, 0), 10000 },
    { UDATA (dj2d_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, 0), 10000 },
    { UDATA (dj2d_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, 0), 10000 },
    { UDATA (dj2d_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, 0), 10000 },
    { UDATA (dj2d_sio_svc, UNIT_ATTABLE + UNIT_DISABLE, 0), 10000 }
};

static REG dj2d_reg[] = {
    { DRDATAD (DRIVE, dj2d_info_data.currentDrive, 8, "Current drive register"), },
    { HRDATAD (STATUS, dj2d_info_data.WD1791.status, 8, "Status register"), },
    { HRDATAD (COMMAND, dj2d_info_data.WD1791.command, 8, "Command register"), },
    { HRDATAD (DATA, dj2d_info_data.WD1791.data, 8, "Data register"), },
    { DRDATAD (TRACK, dj2d_info_data.WD1791.track, 8, "Track register"), },
    { DRDATAD (SECTOR, dj2d_info_data.WD1791.sector, 8, "Sector register"), },
    { DRDATAD (SPT, dj2d_info_data.secsPerTrack, 8, "Sectors per track register"), },
    { DRDATAD (BPT, dj2d_info_data.bytesPerTrack, 16, "Bytes per track register"), },
    { DRDATAD (STEPDIR, dj2d_info_data.WD1791.stepDir, 8, "Last step direction register"), },
    { DRDATAD (SECCNT, dj2d_info_data.WD1791.dataCount, 16, "Sector byte count register"), },
    { DRDATAD (TRKCNT, dj2d_info_data.WD1791.trkCount, 16, "Track byte count register"), },
    { FLDATAD (RDACT, dj2d_info_data.WD1791.readActive, 0, "Read sector active status bit"), },
    { FLDATAD (WRACT, dj2d_info_data.WD1791.writeActive, 0, "Write sector active status bit"), },
    { FLDATAD (RDTACT, dj2d_info_data.WD1791.readTrkActive, 0, "Read track active status bit"), },
    { FLDATAD (WRTACT, dj2d_info_data.WD1791.writeTrkActive, 0, "Write track active status bit"), },
    { FLDATAD (INTRQ, dj2d_info_data.WD1791.intrq, 0, "INTRQ status bit"), },
    { FLDATAD (DRQ, dj2d_info_data.WD1791.drq, 0, "DRQ status bit"), },
    { FLDATAD (PROM, dj2d_info_data.promEnabled, 0, "PROM enabled bit"), },
    { FLDATAD (WRTPROT, dj2d_info_data.writeProtect, 0, "Write protect enabled bit"), },
    { DRDATAD (TICKS, dj2d_info_data.ticks, 32, "Timer ticks"), },
    { DRDATAD (SIOTICKS, dj2d_info_data.sioticks, 32, "SIO timer ticks"), },
    { DRDATAD (HEAD, dj2d_info_data.headTimeout, 16, "Head unload timeout"), },
    { DRDATAD (INDEX, dj2d_info_data.indexTimeout, 16, "Index timeout"), },
    { DRDATAD (BUSY, dj2d_info_data.busyTimeout, 16, "Busy timeout"), },
    { HRDATAD (DJSTAT, dj2d_info_data.DJ2D.status, 8, "DJ2D status register"), },
    { HRDATAD (DJCTRL, dj2d_info_data.DJ2D.control, 8, "DJ2D control register"), },
    { HRDATAD (DJFUNC, dj2d_info_data.DJ2D.function, 8, "DJ2D function register"), },
    { HRDATAD (URXD, dj2d_info_data.DJ2D.uart_rxd, 8, "UART RX data register"), },
    { HRDATAD (UTXD, dj2d_info_data.DJ2D.uart_txd, 8, "UART TX data register"), },
    { HRDATAD (UTXP, dj2d_info_data.DJ2D.uart_txp, 8, "UART TX data pending"), },
    { HRDATAD (USTAT, dj2d_info_data.DJ2D.uart_status, 8, "UART status register"), },
    { DRDATAD (BAUD, dj2d_info_data.DJ2D.uart_baud, 16, "UART baud rate"), },
    { NULL }
};

#define DJ2D_NAME  "DISK JOCKEY 2D/B Floppy Disk Controller"
#define DJ2D_SNAME "DJ2D"

static const char* dj2d_description(DEVICE *dptr) {
    return DJ2D_NAME;
}

#define UNIT_V_DJ2D_WPROTECT     (UNIT_V_UF + 0)                      /* WRTENB / WRTPROT */
#define UNIT_DJ2D_WPROTECT       (1 << UNIT_V_DJ2D_WPROTECT)

/* Terminal multiplexer library descriptors */

static MTAB dj2d_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PROM", "PROM={ENABLE|DISABLE}",
        &dj2d_set_prom, &dj2d_show_prom, NULL, "Set/Show PROM enabled/disabled status"},
    { MTAB_XTD|MTAB_VDV,    0,                  "PROMBASE",  "PROMBASE",
        &dj2d_set_prombase, &dj2d_show_prombase, NULL, "Sets PROM base address"   },
    { UNIT_DJ2D_WPROTECT,  0,                      "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " DJ2D_SNAME "n for writing"                 },
    { UNIT_DJ2D_WPROTECT,  UNIT_DJ2D_WPROTECT,  "WRTPROT",    "WRTPROT",  NULL, NULL, NULL,
        "Protects " DJ2D_SNAME "n from writing"                },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,  0,   "BAUD",  "BAUD",  &dj2d_set_baud, &dj2d_show_baud,
        NULL, "Set baud rate (default=19200)" },
    { 0 }
};

/* Debug flags */
#define ERROR_MSG           (1 << 0)
#define SEEK_MSG            (1 << 1)
#define CMD_MSG             (1 << 2)
#define RD_DATA_MSG         (1 << 3)
#define WR_DATA_MSG         (1 << 4)
#define STATUS_MSG          (1 << 5)
#define RD_DATA_DETAIL_MSG  (1 << 6)
#define WR_DATA_DETAIL_MSG  (1 << 7)
#define VERBOSE_MSG         (1 << 8)
#define DEBUG_MSG           (1 << 9)

/* Debug Flags */
static DEBTAB dj2d_dt[] = {
    { "ERROR",      ERROR_MSG,          "Error messages"        },
    { "SEEK",       SEEK_MSG,           "Seek messages"         },
    { "CMD",        CMD_MSG,            "Command messages"      },
    { "READ",       RD_DATA_MSG,        "Read messages"         },
    { "WRITE",      WR_DATA_MSG,        "Write messages"        },
    { "STATUS",     STATUS_MSG,         "Status messages"       },
    { "RDDETAIL",   RD_DATA_DETAIL_MSG, "Read detail messages"  },
    { "WRDETAIL",   WR_DATA_DETAIL_MSG, "Write detail messags"  },
    { "VERBOSE",    VERBOSE_MSG,        "Verbose messages"      },
    { "DEBUG",      DEBUG_MSG,          "Debug messages"        },
    { NULL,         0                                           }
};

DEVICE dj2d_dev = {
    DJ2D_SNAME,                        /* name */
    dj2d_unit,                         /* unit */
    dj2d_reg,                          /* registers */
    dj2d_mod,                          /* modifiers */
    DJ2D_UNITS,                   /* # units */
    10,                                   /* address radix */
    31,                                   /* address width */
    1,                                    /* addr increment */
    DJ2D_UNITS,                   /* data radix */
    DJ2D_UNITS,                   /* data width */
    NULL,                                 /* examine routine */
    NULL,                                 /* deposit routine */
    &dj2d_reset,                       /* reset routine */
    &dj2d_boot,                        /* boot routine */
    &dj2d_attach,                      /* attach routine */
    &dj2d_detach,                      /* detach routine */
    &dj2d_info_data,                   /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG),  /* flags */
    ERROR_MSG,                            /* debug control */
    dj2d_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &dj2d_description                  /* description */
};

/* Reset routine */
static t_stat dj2d_reset(DEVICE *dptr)
{
    uint8 i;
    DJ2D_INFO *pInfo = (DJ2D_INFO *)dptr->ctxt;

    if (dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pInfo->prom_base, pInfo->prom_size, RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", TRUE);
        sim_map_resource(pInfo->mem_base, pInfo->mem_size, RESOURCE_TYPE_MEMORY, &dj2dmem, "dj2dmem", TRUE);
    } else {
        if (sim_map_resource(pInfo->prom_base, pInfo->prom_size, RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", FALSE) != 0) {
            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": Error mapping PROM resource at 0x%04x\n", pInfo->prom_base);
            return SCPE_ARG;
        }
        if (sim_map_resource(pInfo->mem_base, pInfo->mem_size, RESOURCE_TYPE_MEMORY, &dj2dmem, "dj2dmem", FALSE) != 0) {
            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": Error mapping MEM resource at 0x%04x\n", pInfo->mem_base);
            return SCPE_ARG;
        }
    }

    for (i = 0; i < DJ2D_UNITS; i++) {
        if (dj2d_info->uptr[i] == NULL) {
            dj2d_info->uptr[i] = &dj2d_dev.units[i];
        }
    }

    /* Reset Registers */
    pInfo->currentDrive = 0;
    pInfo->promEnabled = TRUE;
    pInfo->writeProtect = FALSE;

    pInfo->DJ2D.uart_status = DJ2D_STAT_TBRE;
    pInfo->DJ2D.uart_txp = FALSE;
    pInfo->DJ2D.uart_baud = DJ2D_BAUD;

    pInfo->WD1791.track = 0;
    pInfo->WD1791.sector = 1;
    pInfo->WD1791.command = 0;
    pInfo->WD1791.status = 0;
    pInfo->WD1791.data = 0;
    pInfo->WD1791.drq = FALSE;
    pInfo->WD1791.index = FALSE;
    pInfo->WD1791.intrq = FALSE;
    pInfo->WD1791.stepDir = 1;
    pInfo->WD1791.dataCount = 0;
    pInfo->WD1791.trkCount = 0;
    pInfo->WD1791.addrActive = FALSE;
    pInfo->WD1791.readActive = FALSE;
    pInfo->WD1791.readTrkActive = FALSE;
    pInfo->WD1791.writeActive = FALSE;
    pInfo->WD1791.writeTrkActive = FALSE;
    pInfo->WD1791.addrActive = FALSE;

    for (i = 0; i < DJ2D_MAX_DRIVES; i++) {
        dj2d_info->headLoaded[i] = FALSE;
    }

    /* Start timer for unit 0 (we only need 1 timer for all drive units) */
    dj2d_info->indexTimeout = DJ2D_ROTATION_MS;
    sim_activate_after(dj2d_info->uptr[0], DJ2D_TIMER * 1000);

    /* Start timer for SIO unit */
    sim_activate_after(dj2d_info->uptr[DJ2D_SIO_UNIT], 500);  /* activate 500 us timer */

    /* Disable clockFrequency if it's set */
    if (getClockFrequency()) {
        setClockFrequency(0);
        sim_printf(DJ2D_SNAME ": CPU CLOCK register not supported. Use THROTTLE.\n");
    }

    /* Configure the serial interface */
    dj2d_config_line();

    sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": reset controller.\n");

    return SCPE_OK;
}

static t_stat dj2d_sio_svc(UNIT *uptr)
{
    int32 c;
    t_stat r;

    dj2d_info->sioticks++;

    /* Check for new incoming connection */
    if (uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(dj2d_info->tmxr) >= 0) {      /* poll connection */

            dj2d_info->conn = 1;          /* set connected   */

            sim_debug(STATUS_MSG, uptr->dptr, "new connection.\n");
        }
    }

    /* TX byte pending? */
    if (dj2d_info->DJ2D.uart_txp) {
        if (uptr->flags & UNIT_ATT) {
            r = tmxr_putc_ln(dj2d_info->tmln, dj2d_info->DJ2D.uart_txd);
        } else {
            r = sim_putchar(dj2d_info->DJ2D.uart_txd);
        }

        dj2d_info->DJ2D.uart_txp = FALSE;

        if (r == SCPE_LOST) {
            dj2d_info->conn = 0;          /* Connection was lost */
            sim_debug(STATUS_MSG, uptr->dptr, "lost connection.\n");
        }
    }

    /* Update TBRE */
    if (!(dj2d_info->DJ2D.uart_status & DJ2D_STAT_TBRE)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_tx(dj2d_info->tmxr);
            dj2d_info->DJ2D.uart_status |= (tmxr_txdone_ln(dj2d_info->tmln) && dj2d_info->conn) ? DJ2D_STAT_TBRE : 0;
        } else {
            dj2d_info->DJ2D.uart_status |= DJ2D_STAT_TBRE;
        }
    }

    /* Check for Data if RX buffer empty */
    if (!(dj2d_info->DJ2D.uart_status & DJ2D_STAT_DR)) {
        if (uptr->flags & UNIT_ATT) {
            tmxr_poll_rx(dj2d_info->tmxr);

            c = tmxr_getc_ln(dj2d_info->tmln);
        } else {
            c = sim_poll_kbd();
        }

        if (c & (TMXR_VALID | SCPE_KFLAG)) {
            dj2d_info->DJ2D.uart_rxd = c & 0xff;
            dj2d_info->DJ2D.uart_status |= DJ2D_STAT_DR;
            dj2d_info->DJ2D.uart_status &= ~(DJ2D_STAT_FE | DJ2D_STAT_OE | DJ2D_STAT_PE);
        }
    }

    /* Restart timer */
    sim_activate_after(uptr, 500);  /* reactivate 500 us timer */

    return SCPE_OK;
}

static t_stat dj2d_svc(UNIT *uptr)
{
    WD1791_REG *pWD1791;

    pWD1791 = &dj2d_info->WD1791;

    dj2d_info->ticks++;

    if (dj2d_info->headTimeout) {
        if (!(--dj2d_info->headTimeout)) {
            DJ2D_HeadLoad(uptr, pWD1791, FALSE);
        }
    }

    if (dj2d_info->indexTimeout) {
        if (!(--dj2d_info->indexTimeout)) {
            pWD1791->index = FALSE;
            dj2d_info->indexTimeout = DJ2D_INDEX_TIMEOUT;
        } else {
            pWD1791->index = TRUE;
        }
    }

    if (dj2d_info->busyTimeout) {
        if (!(--dj2d_info->busyTimeout)) {
            pWD1791->status &= ~WD1791_STAT_BUSY;
            pWD1791->drq = FALSE;
            pWD1791->intrq = TRUE;
        }
    }

    /* Restart timer */
    sim_activate_after(uptr, DJ2D_TIMER * 1000);  /* activate timer */

    return SCPE_OK;
}

/*
** Verify that prombase is within valid range
** before calling set_membase
*/
static t_stat dj2d_set_prombase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 newba;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;

    newba = get_uint (cptr, 16, 0xFFFF, &r);
    if (r != SCPE_OK)
        return r;

    if ((newba != 0xe000) && (newba != 0xf800)) {
        sim_printf(DJ2D_SNAME ": Valid options are E000,F800\n");
        return SCPE_ARG;
    }

    /*
    ** Release previous memory maps
    */
    sim_map_resource(dj2d_info->prom_base, dj2d_info->prom_size, RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", TRUE);
    sim_map_resource(dj2d_info->mem_base, dj2d_info->mem_size, RESOURCE_TYPE_MEMORY, &dj2dmem, "dj2dmem", TRUE);

    dj2d_info->prom_base = newba;
    dj2d_info->mem_base = newba+DJ2D_PROM_SIZE;

    if (sim_map_resource(dj2d_info->prom_base, dj2d_info->prom_size, RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", FALSE) != 0) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": Error mapping PROM resource at 0x%04x\n", dj2d_info->prom_base);
        return SCPE_ARG;
    }
    if (sim_map_resource(dj2d_info->mem_base, dj2d_info->mem_size, RESOURCE_TYPE_MEMORY, &dj2dmem, "dj2dmem", FALSE) != 0) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": Error mapping MEM resource at 0x%04x\n", dj2d_info->mem_base);
        return SCPE_ARG;
    }

    if (newba == 0xe000) {
        dj2d_prom = dj2d_prom_e000;
    } else {
        dj2d_prom = dj2d_prom_f800;
    }

    return dj2d_reset(&dj2d_dev);
}

/* Show Base Address routine */
t_stat dj2d_show_prombase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr;
    DJ2D_INFO *pInfo;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    pInfo = (DJ2D_INFO *) dptr->ctxt;

    if(pInfo->promEnabled) {
        fprintf(st, "PROM=0x%04X-0x%04X", pInfo->prom_base, pInfo->prom_base+pInfo->prom_size-9);
        fprintf(st, ", REG=0x%04X-0x%04X", pInfo->prom_base+pInfo->prom_size-8, pInfo->prom_base+pInfo->prom_size-1);
        fprintf(st, ", RAM=0x%04X-0x%04X", pInfo->mem_base, pInfo->mem_base+pInfo->mem_size-1);
    }

    return SCPE_OK;
}

/* Attach routine */
static t_stat dj2d_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;
    int i,f;

    /* Attaching to serial interface? */
    if (uptr == &dj2d_dev.units[DJ2D_SIO_UNIT]) {
        if ((r = tmxr_attach(dj2d_info->tmxr, uptr, cptr)) == SCPE_OK) {
            dj2d_info->tmln->rcve = 1;

            sim_debug(VERBOSE_MSG, uptr->dptr, "attached '%s' to serial interface.\n", cptr);
        }

        return r;
    }

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if (r != SCPE_OK) {              /* error?       */
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": ATTACH error=%d\n", r);
        return r;
    }

    for (i = 0; i < DJ2D_UNITS; i++) {
        if (dj2d_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= DJ2D_UNITS) {
        return SCPE_ARG;
    }

    uptr->capac = sim_fsize(uptr->fileref);

    /* Default is 1024 byte sectors */
    dj2d_info->format[i] = FMT_1024;
    dj2d_info->sectorLen[i] = dj2d_sector_len[FMT_1024];

    for (f = 0; f < FMT_UNKNOWN; f++) {
        if (uptr->capac == dj2d_image_size[f]) {
            dj2d_info->format[i] = f;
            dj2d_info->sectorLen[i] = dj2d_sector_len[f];
        }
    }

    sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": ATTACH drive=%d uptr->capac=%d format=%d sectorLen=%d\n", i, uptr->capac, dj2d_info->format[i], dj2d_info->sectorLen[i]);

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if (uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if ((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            sim_printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            dj2d_detach(uptr);
            return SCPE_OPENERR;
        }
    }


    sim_debug(VERBOSE_MSG, uptr->dptr, DJ2D_SNAME "%d: attached to '%s', type=%s, len=%d\n", i, cptr,
        uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
        uptr->capac);

    return SCPE_OK;
}


/* Detach routine */
static t_stat dj2d_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < DJ2D_UNITS; i++) {
        if (dj2d_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= DJ2D_UNITS) {
        return SCPE_ARG;
    }

    r = detach_unit(uptr);  /* detach unit */

    if (r != SCPE_OK) {
        return r;
    }

    dj2d_dev.units[i].fileref = NULL;

    dj2d_info->WD1791.index = TRUE;
    dj2d_info->indexTimeout = 0;

    sim_debug(VERBOSE_MSG, uptr->dptr, DJ2D_SNAME "%d: detached\n", i);

    return SCPE_OK;
}

static t_stat dj2d_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    int32 baud;
    t_stat r = SCPE_ARG;

    /* Force serial interface unit */
    uptr = &dj2d_dev.units[DJ2D_SIO_UNIT];

    if (!(uptr->flags & UNIT_ATT)) {
        return SCPE_UNATT;
    }

    if (cptr != NULL) {
        if (sscanf(cptr, "%d", &baud)) {
            switch (baud) {
                case 110:
                case 1200:
                case 9600:
                case 19200:
                    dj2d_info->DJ2D.uart_baud = baud;
                    r = dj2d_config_line();
                    break;

                default:
                    break;
            }
        }
    }

    return r;
}

static t_stat dj2d_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc)
{
    if (uptr->flags & UNIT_ATT) {
        fprintf(st, "Baud rate: %d", dj2d_info->DJ2D.uart_baud);
    }

    return SCPE_OK;
}

static t_stat dj2d_config_line()
{
    char config[20];
    const char *fmt;
    t_stat r = SCPE_IERR;

    fmt = "8N1";

    sprintf(config, "%d-%s", dj2d_info->DJ2D.uart_baud, fmt);

    r = tmxr_set_config_line(dj2d_info->tmln, config);

    sim_debug(STATUS_MSG, &dj2d_dev, "port configuration set to '%s'.\n", config);

    return r;
}

static t_stat dj2d_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "ENABLE", strlen(cptr))) {
        sim_map_resource(dj2d_info->prom_base, dj2d_info->prom_size,
            RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", FALSE);
        dj2d_info->promEnabled = TRUE;
    } else if (!strncmp(cptr, "DISABLE", strlen(cptr))) {
        dj2d_info->promEnabled = FALSE;
        sim_map_resource(dj2d_info->prom_base, dj2d_info->prom_size,
            RESOURCE_TYPE_MEMORY, &dj2dprom, "dj2dprom", TRUE);
    } else {
        return SCPE_ARG;
    }

    return SCPE_OK;
}

static t_stat dj2d_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (dj2d_info->promEnabled) {
        fprintf(st, "PROM");
    } else {
        fprintf(st, "NOPROM");
    }

    return SCPE_OK;
}

static t_stat dj2d_boot(int32 unitno, DEVICE *dptr)
{

    DJ2D_INFO *pInfo = (DJ2D_INFO *)dptr->ctxt;

    sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": Booting Controller at 0x%04x\n", pInfo->prom_base);

    *((int32 *) sim_PC->loc) = pInfo->prom_base;

    return SCPE_OK;
}

static void showdata(int32 isRead) {
    int32 i;
    sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &dj2d_dev, DJ2D_SNAME ": %s track/sector %02d/%03d:\n\t", isRead ? "Read" : "Write", dj2d_info->WD1791.track, dj2d_info->WD1791.sector);
    for (i = 0; i < sector_len(dj2d_info->currentDrive, dj2d_info->WD1791.track); i++) {
        sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &dj2d_dev, "%02X ", sdata[i]);
        if (((i+1) & 0xf) == 0) {
            sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &dj2d_dev, "\n\t");
        }
    }
    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &dj2d_dev, "\n");
}

static uint16 sector_len(uint8 drive, uint8 track)
{
    if (track == 0) {  /* Track 0 is always SD */
        return(dj2d_sector_len[FMT_SD]);
    }

    return(dj2d_info->sectorLen[drive]);
}

static uint32 secs_per_track(uint8 track)
{
    if (track == 0) {
        dj2d_info->secsPerTrack = (uint8)dj2d_spt[FMT_SD];
    } else {
        dj2d_info->secsPerTrack = (uint8)dj2d_spt[dj2d_info->format[dj2d_info->currentDrive]];
    }

    return dj2d_info->secsPerTrack;
}

static uint32 bytes_per_track(uint8 track)
{
    int8 format;

    format = dj2d_info->format[dj2d_info->currentDrive];

    if (track == 0) {
        dj2d_info->bytesPerTrack = dj2d_track_len[FMT_SD];
    } else {
        dj2d_info->bytesPerTrack = dj2d_track_len[format];
    }

    return dj2d_info->bytesPerTrack;
}

static uint32 calculate_dj2d_sec_offset(uint8 track, uint8 sector)
{
    uint32 offset;
    uint8 ds;
    uint8 format;

    ds = dj2d_info->side[dj2d_info->currentDrive];
    format = dj2d_info->format[dj2d_info->currentDrive];

    /*
    ** Side 0: tracks 0-76
    ** Side 1: tracks 77-153
    */
    if (ds) {
        track += 77;
    }

    /*
    ** Calculate track offset
    */
    if (track == 0) {    /* Track 0 is always SD */
        offset = 0;
        format = FMT_SD;
    } else {
        offset = dj2d_spt[FMT_SD] * dj2d_sector_len[FMT_SD]; /* Track 0 / Side 0 always SD */
        offset += (track-1) * dj2d_spt[format] * dj2d_sector_len[format]; /* Track 1-153 */
    }

    /*
    ** Add sector offset to track offset
    */
    offset += (sector-1) * dj2d_sector_len[format];

    sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": OFFSET=%d drive=%d side=%d format=%d track=%03d sector=%03d\r\n", offset, dj2d_info->currentDrive, ds, dj2d_info->format[dj2d_info->currentDrive], track, sector);

    return (offset);
}

static void DJ2D_HeadLoad(UNIT *uptr, WD1791_REG *pWD1791, uint8 load)
{
    /*
    ** If no disk has been attached, uptr will be NULL - return
    */
    if (uptr == NULL) {
        return;
    }

    if (load) {
        dj2d_info->headTimeout = DJ2D_HEAD_TIMEOUT;

        if (dj2d_info->headLoaded[dj2d_info->currentDrive] == FALSE) {
            sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": Drive %d head Loaded.\n", dj2d_info->currentDrive);
        }
    } else {
        dj2d_info->headTimeout = 0;

        if (dj2d_info->headLoaded[dj2d_info->currentDrive] == TRUE) {
            sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": Drive %d head Unloaded.\n", dj2d_info->currentDrive);
        }
    }

    dj2d_info->headLoaded[dj2d_info->currentDrive] = load;
}

static uint8 DJ2D_Read(uint32 Addr)
{
    uint8 cData;
    uint8 driveNum;
    WD1791_REG *pWD1791;
    DJ2D_REG *pDJ2D;
    UNIT *uptr;

    driveNum = dj2d_info->currentDrive;
    uptr = dj2d_info->uptr[driveNum];
    pWD1791 = &dj2d_info->WD1791;
    pDJ2D = &dj2d_info->DJ2D;

    switch(Addr & 0x07) {
        case DJ2D_REG_UART_DATA:  /* Read character from UART */
            if (pDJ2D->uart_status & DJ2D_STAT_DR) {
                cData = ~pDJ2D->uart_rxd;  /* Inverted */
                pDJ2D->uart_status &= ~DJ2D_STAT_DR;
            } else {
                cData = 0xff;
            }
            break;

        case DJ2D_REG_UART_STATUS:
            cData = ~pDJ2D->uart_status;  /* Inverted */
            break;

        case DJ2D_REG_2D_STATUS:
            cData = (pWD1791->intrq) ? DJ2D_STAT_INTRQ : 0;
            cData |= (pWD1791->drq) ? DJ2D_STAT_DATARQ : 0;
            cData |= (pWD1791->index) ? DJ2D_STAT_INDEX : 0;
            cData |= (dj2d_info->headLoaded[dj2d_info->currentDrive]) ? DJ2D_STAT_HEAD : 0;
            cData |= (pWD1791->status & WD1791_STAT_NOTREADY) ? 0 : DJ2D_STAT_READY;
            cData |= DJ2D_STAT_N2SIDED;
            pDJ2D->status = cData;
            break;

        case DJ2D_REG_1791_STATUS:
            cData = pWD1791->status;
            break;

        case DJ2D_REG_1791_TRACK:
            cData = pWD1791->track;
            break;

        case DJ2D_REG_1791_DATA:
            /*
            ** If a READ operation is currently active, get the next byte
            */
            if (pWD1791->readActive) {
                /* Store byte in DATA register */
                pWD1791->data = sdata[pWD1791->dataCount++];

                /* If we reached the end of the sector, terminate command and set INTRQ */
                if (pWD1791->dataCount == sector_len(driveNum, pWD1791->track)) {
                    pWD1791->readActive = FALSE;
                    pWD1791->dataCount = 0;
                    pWD1791->status = 0x00;
                    pWD1791->drq = FALSE;
                    pWD1791->intrq = TRUE;
                }

                DJ2D_HeadLoad(uptr, pWD1791, TRUE);
            } else if (pWD1791->readTrkActive) {
                /* If we reached the end of the track data, terminate command and set INTRQ */
                if (pWD1791->trkCount == bytes_per_track(pWD1791->track)) {
                    pWD1791->readTrkActive = FALSE;
                    pWD1791->status = 0x00;
                    pWD1791->drq = FALSE;
                    pWD1791->intrq = TRUE;
                } else {
                    pWD1791->trkCount++;
                }

                DJ2D_HeadLoad(uptr, pWD1791, TRUE);
            } else if (pWD1791->addrActive) {
                /* Store byte in DATA register */
                pWD1791->data = sdata[pWD1791->dataCount++];

                /* If we reached the end of the address data, terminate command and set INTRQ */
                if (pWD1791->dataCount > WD1791_ADDR_CRC2) {
                    pWD1791->addrActive = FALSE;
                    pWD1791->status = 0x00;
                    pWD1791->drq = FALSE;
                    pWD1791->intrq = TRUE;
                }

                DJ2D_HeadLoad(uptr, pWD1791, TRUE);
            }

            cData = pWD1791->data;
            break;

        case DJ2D_REG_1791_SECTOR:
            cData = pWD1791->sector;
            break;

        default:
            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": READ REG Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": READ REG currentDrive=%d format=%d track=%02d sector=%02d data=%02x status=%02x\n", dj2d_info->currentDrive, dj2d_info->format[dj2d_info->currentDrive], pWD1791->track, pWD1791->sector, pWD1791->data, pWD1791->status);

    return (cData);
}

static uint8 DJ2D_Write(uint32 Addr, int32 Data)
{
    uint8 cData;
    uint8 driveNum;
    int32 rtn;
    UNIT *uptr;
    WD1791_REG *pWD1791;
    DJ2D_REG *pDJ2D;

    Data &= 0xff;

    sim_debug(CMD_MSG, &dj2d_dev, DJ2D_SNAME ": OUT %04X Data %02X\n", Addr, Data);

    cData = 0;
    driveNum = dj2d_info->currentDrive;
    uptr = dj2d_info->uptr[driveNum];
    pWD1791 = &dj2d_info->WD1791;
    pDJ2D = &dj2d_info->DJ2D;

    switch(Addr & 0x07) {
        case DJ2D_REG_UART_DATA:
            pDJ2D->uart_txd = ~Data;  /* Character is inverted */
            pDJ2D->uart_txp = TRUE;
            pDJ2D->uart_status &= ~DJ2D_STAT_TBRE;
            break;

        case DJ2D_REG_1791_COMMAND:
            cData = DJ2D_Command(uptr, pWD1791, Data);
            break;

        case DJ2D_REG_2D_FUNCTION:
            pDJ2D->function = Data;

            switch (Data & DJ2D_FUNC_HDMASK) {
                case DJ2D_FUNC_HDLOAD:
                    DJ2D_HeadLoad(uptr, pWD1791, TRUE);
                    break;

                case DJ2D_FUNC_HDUNLD:
                    DJ2D_HeadLoad(uptr, pWD1791, FALSE);
                    break;

                default:
                    break;
            }

            break;

        case DJ2D_REG_1791_DATA:
            pWD1791->data = Data;   /* Store byte in DATA register */

            if (pWD1791->writeActive) {

                /* Store DATA register in Sector Buffer */
                sdata[pWD1791->dataCount++] = pWD1791->data;

                /* If we reached the end of the sector, write sector, terminate command and set INTRQ */
                if (pWD1791->dataCount == sector_len(driveNum, pWD1791->track)) {
                    pWD1791->status = 0x00;  /* Clear Status Bits */

                    rtn = DJ2D_WriteSector(uptr, pWD1791->track, pWD1791->sector, sdata);

                    showdata(FALSE);

                    if (rtn != sector_len(driveNum, pWD1791->track)) {
                        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": sim_fwrite errno=%d\n", errno);

                        pWD1791->status |= WD1791_STAT_WRITEFAULT;
                    }
                    pWD1791->writeActive = FALSE;
                    pWD1791->dataCount = 0;
                    pWD1791->drq = FALSE;
                    pWD1791->intrq = TRUE;
                }

                DJ2D_HeadLoad(uptr, pWD1791, TRUE);
            } else if (pWD1791->writeTrkActive) {

                if (pWD1791->idAddrMrk) {
                    if (++pWD1791->dataCount == 4) {   /* Sector Len */
                        dj2d_info->sectorLen[dj2d_info->currentDrive] = dj2d_sector_len[pWD1791->data];
                        dj2d_info->format[dj2d_info->currentDrive] = pWD1791->data;
                        pWD1791->idAddrMrk = 0;
                        pWD1791->dataCount = 0;
                    }
                } else if (pWD1791->dataAddrMrk) {
                    /* Store DATA register in Sector Buffer */
                    sdata[pWD1791->dataCount++] = pWD1791->data;

                    /* If we reached the end of the sector, write sector */
                    if (pWD1791->dataCount == sector_len(driveNum, pWD1791->track)) {
                        pWD1791->status &= ~WD1791_STAT_WRITEFAULT;  /* Clear Status Bit */

                        rtn = DJ2D_WriteSector(uptr, pWD1791->track, pWD1791->sector, sdata);

                        if (rtn != sector_len(driveNum, pWD1791->track)) {
                            pWD1791->status |= WD1791_STAT_WRITEFAULT;
                            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": WRITE ERROR could not write track %03d sector %03d\n", pWD1791->track, pWD1791->sector);
                        }

                        sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": WRITE TRACK drive=%d track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X\n", driveNum, pWD1791->track, pWD1791->sector, pWD1791->trkCount, pWD1791->dataCount, pWD1791->data, pWD1791->status);

                        pWD1791->dataCount = 0;
                        pWD1791->idAddrMrk = 0;
                        pWD1791->dataAddrMrk = 0;

                        if (pWD1791->sector < secs_per_track(pWD1791->track)) {
                            pWD1791->sector++;
                        }
                    }
                } else if (pWD1791->data == 0xFE) {
                        pWD1791->idAddrMrk = 1;
                } else if (pWD1791->data == 0xFB) {
                        pWD1791->dataAddrMrk = 1;
                }

                /*
                ** Increment number for bytes written to track
                */
                pWD1791->trkCount++;

                if (pWD1791->trkCount == bytes_per_track(pWD1791->track)) {
                    pWD1791->status = 0x00;  /* Clear Status Bits */
                    pWD1791->drq = FALSE;
                    pWD1791->intrq = TRUE;
                    pWD1791->writeTrkActive = FALSE;

                    /*
                    ** Last track, truncate file size in case it shrank
                    */
                    if (pWD1791->track == 76) {
                        sim_set_fsize(uptr->fileref, (t_addr)sim_ftell(uptr->fileref));
                    }
                    sim_debug(WR_DATA_MSG, &dj2d_dev, DJ2D_SNAME ": WRITE TRACK COMPLETE track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X\n", pWD1791->track, pWD1791->sector, pWD1791->trkCount, pWD1791->dataCount, pWD1791->data, pWD1791->status);
                }

                DJ2D_HeadLoad(uptr, pWD1791, TRUE);
            }

            break;

        case DJ2D_REG_1791_TRACK:
            pWD1791->track = Data;
            break;

        case DJ2D_REG_1791_SECTOR:
            pWD1791->sector = Data;
            break;

        case DJ2D_REG_2D_CONTROL:
            pDJ2D->control = Data & 0xff;

            /* Drive Select */
            switch (~Data & DJ2D_CTRL_DSEL) {
                case 0x01:
                    cData = 0;
                    break;
                case 0x02:
                    cData = 1;
                    break;
                case 0x04:
                    cData = 2;
                    break;
                case 0x08:
                    cData = 3;
                    break;
            }

            /* Side */
            dj2d_info->side[cData] = (Data & DJ2D_CTRL_SIDE0) == 0x00;

            if (dj2d_info->currentDrive != cData) {
                sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": Current drive now %d side %d\n", cData, dj2d_info->side[cData]);
            }

            dj2d_info->currentDrive = cData;

            break;

        default:
            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": WRITE Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": WRITE REG currentDrive=%d format=%d track=%02d sector=%02d data=%02x status=%02x\n", dj2d_info->currentDrive, dj2d_info->format[dj2d_info->currentDrive], pWD1791->track, pWD1791->sector, pWD1791->data, pWD1791->status);

    return(cData);
}

static uint32 DJ2D_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;
    uint32 len;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": READSEC uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_dj2d_sec_offset(track, sector);

    len = sector_len(dj2d_info->currentDrive, track);

    sim_debug(RD_DATA_MSG, &dj2d_dev, DJ2D_SNAME ": READSEC track %03d sector %03d at offset %04X len %d\n", track, sector, sec_offset, len);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": READSEC sim_fseek error.\n");
        return 0;
    }

    rtn = sim_fread(buffer, 1, len, uptr->fileref);

    return rtn;
}

static uint32 DJ2D_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 len;
    uint32 rtn = 0;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": READSEC uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_dj2d_sec_offset(track, sector);

    len = sector_len(dj2d_info->currentDrive, track);

    sim_debug(WR_DATA_MSG, &dj2d_dev, DJ2D_SNAME ": WRITESEC track %03d sector %03d at offset %04X len %d\n", track, sector, sec_offset, len);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": WRITESEC sim_fseek error.\n");
        return 0;
    }

    rtn = sim_fwrite(buffer, 1, len, uptr->fileref);

    return rtn;
}

static const char * DJ2D_CommandString(uint8 command)
{
    switch (command & 0xf0) {
        case WD1791_CMD_RESTORE:
            return "RESTORE";

        case WD1791_CMD_SEEK:
            return "SEEK";

        case WD1791_CMD_STEP:
            return "STEP";

        case WD1791_CMD_STEPU:
            return "STEP U";

        case WD1791_CMD_STEPIN:
            return "STEP IN";

        case WD1791_CMD_STEPINU:
            return "STEP IN U";

        case WD1791_CMD_STEPOUT:
            return "STEP OUT";

        case WD1791_CMD_STEPOUTU:
            return "STEP OUT U";

        case WD1791_CMD_READ:
            return "READ";

        case WD1791_CMD_WRITE:
            return "WRITE";

        case WD1791_CMD_WRITEM:
            return "WRITE M";

        case WD1791_CMD_READ_ADDRESS:
            return "READ ADDRESS";

        case WD1791_CMD_READ_TRACK:
            return "READ TRACK";

        case WD1791_CMD_WRITE_TRACK:
            return "WRITE TRACK";

        case WD1791_CMD_FORCE_INTR:
            return "FORCE INTR";

        default:
            break;
    }

    return "UNRECOGNIZED COMMAND";
}

static uint8 DJ2D_Command(UNIT *uptr, WD1791_REG *pWD1791, int32 Data)
{
    uint8 cData;
    uint8 newTrack;
    uint8 statusUpdate;
    int32 rtn;

    cData = 0;
    statusUpdate = TRUE;

    if (uptr == NULL) {
        return cData;
    }

    pWD1791->command = Data;

    /*
    ** Type II-IV Command
    */
    if (pWD1791->command & 0x80) {
        pWD1791->readActive = FALSE;
        pWD1791->writeActive = FALSE;
        pWD1791->readTrkActive = FALSE;
        pWD1791->writeTrkActive = FALSE;
        pWD1791->addrActive = FALSE;
        pWD1791->dataCount = 0;

        pWD1791->status &= ~WD1791_STAT_DRQ;    /* Reset DRQ */
        pWD1791->drq = FALSE;
    }

    /*
    ** Set BUSY for all but Force Interrupt
    */
    if ((pWD1791->command & WD1791_CMD_FORCE_INTR) != WD1791_CMD_FORCE_INTR) {
        pWD1791->status |= WD1791_STAT_BUSY;
        dj2d_info->busyTimeout = DJ2D_BUSY_TIMEOUT;
    }

    pWD1791->intrq = FALSE;

    switch(pWD1791->command & 0xf0) {
        case WD1791_CMD_RESTORE:
            pWD1791->track = 0;

            sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": RESTORE track=%03d\n", pWD1791->track);

            DJ2D_HeadLoad(uptr, pWD1791, (Data & WD1791_FLAG_H) ? TRUE : FALSE);

            pWD1791->status &= ~WD1791_STAT_SEEKERROR;
            pWD1791->status &= ~WD1791_STAT_DRQ;
            pWD1791->drq = FALSE;
            break;

        case WD1791_CMD_SEEK:
            newTrack = pWD1791->data;

            pWD1791->status &= ~WD1791_STAT_SEEKERROR;

            if (newTrack < DJ2D_TRACKS) {
                pWD1791->track = newTrack;

                DJ2D_HeadLoad(uptr, pWD1791, (Data & WD1791_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": SEEK       track=%03d\n", pWD1791->track);
            } else {
                pWD1791->status |= WD1791_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": SEEK ERR   track=%03d\n", newTrack);
            }

            pWD1791->status &= ~WD1791_STAT_DRQ;
            pWD1791->drq = FALSE;
            break;

        case WD1791_CMD_STEP:
        case WD1791_CMD_STEPU:
            pWD1791->status &= ~WD1791_STAT_SEEKERROR;

            newTrack = pWD1791->track + pWD1791->stepDir;

            if (newTrack < DJ2D_TRACKS) {
                if (Data & WD1791_FLAG_U) {
                    pWD1791->track = newTrack;
                }
                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEP        track=%03d\n", pWD1791->track);
            } else {
                pWD1791->status |= WD1791_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEP ERR    track=%03d\n", newTrack);
            }

            DJ2D_HeadLoad(uptr, pWD1791, (Data & WD1791_FLAG_H) ? TRUE : FALSE);

            pWD1791->status &= ~WD1791_STAT_DRQ;
            pWD1791->drq = FALSE;
            break;

        case WD1791_CMD_STEPIN:
        case WD1791_CMD_STEPINU:
            pWD1791->status &= ~WD1791_STAT_SEEKERROR;

            if (pWD1791->track < DJ2D_TRACKS-1) {
                if (Data & WD1791_FLAG_U) {
                    pWD1791->track++;
                }

                DJ2D_HeadLoad(uptr, pWD1791, (Data & WD1791_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEPIN      track=%03d\n", pWD1791->track);
            } else {
                pWD1791->status |= WD1791_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEPIN ERR  track=%03d\n", pWD1791->track+1);
            }

            pWD1791->stepDir = 1;
            pWD1791->status &= ~WD1791_STAT_DRQ;
            pWD1791->drq = FALSE;
            break;

        case WD1791_CMD_STEPOUT:
        case WD1791_CMD_STEPOUTU:
            pWD1791->status &= ~WD1791_STAT_SEEKERROR;

            if (pWD1791->track > 0) {
                if (Data & WD1791_FLAG_U) {
                    pWD1791->track--;
                }

                DJ2D_HeadLoad(uptr, pWD1791, (Data & WD1791_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEPOUT     track=%03d\n", pWD1791->track);
            } else {
                pWD1791->status |= WD1791_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &dj2d_dev, DJ2D_SNAME ": STEPOUT ERR track=%03d\n", pWD1791->track-1);
            }

            pWD1791->stepDir = -1;
            pWD1791->status &= ~WD1791_STAT_DRQ;
            pWD1791->drq = FALSE;
            break;

        case WD1791_CMD_READ:

            if ((uptr == NULL) || (uptr->fileref == NULL)) {
                sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": " ADDRESS_FORMAT
                          " Drive: %d not attached - read ignored.\n",
                          PCX, dj2d_info->currentDrive);

                return cData;
            }

            rtn = DJ2D_ReadSector(uptr, pWD1791->track, pWD1791->sector, sdata);

            if (rtn == sector_len(dj2d_info->currentDrive, pWD1791->track)) {
                pWD1791->readActive = TRUE;
                pWD1791->drq = TRUE;

                dj2d_info->busyTimeout = 0;  /* BUSY not cleared until all bytes read */

                showdata(TRUE);
            } else {
                sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": sim_fread errno=%d rtn=%d len=%d\n", errno, rtn, sector_len(dj2d_info->currentDrive, pWD1791->track));

                pWD1791->status |= WD1791_STAT_NOTFOUND;
                pWD1791->intrq = TRUE;
            }

            break;

        case WD1791_CMD_WRITE:
            /*
            ** If no disk in drive, return
            */
            if ((uptr == NULL) || (uptr->fileref == NULL)) {
                sim_debug(STATUS_MSG, &dj2d_dev, DJ2D_SNAME ": " ADDRESS_FORMAT
                          " Drive: %d not attached - write ignored.\n",
                          PCX, dj2d_info->currentDrive);
            }

            if ((uptr->flags & UNIT_DJ2D_WPROTECT) || dj2d_info->writeProtect) {
                sim_debug(VERBOSE_MSG, &dj2d_dev, DJ2D_SNAME  ": Disk write protected. uptr->flags=%04x writeProtect=%04x\n", uptr->flags & UNIT_DJ2D_WPROTECT, dj2d_info->writeProtect);
                pWD1791->intrq = TRUE;
            } else {
                dj2d_info->busyTimeout = 0;  /* BUSY not cleared until all bytes written */

                pWD1791->writeActive = TRUE;
                pWD1791->dataCount = 0;
                pWD1791->drq = TRUE;
            }

            break;

        case WD1791_CMD_READ_ADDRESS:
            sdata[WD1791_ADDR_TRACK] = pWD1791->track;
            sdata[WD1791_ADDR_ZEROS] = 0;
            sdata[WD1791_ADDR_SECTOR] = pWD1791->sector;
            sdata[WD1791_ADDR_LENGTH] = (pWD1791->track) ? dj2d_info->format[dj2d_info->currentDrive] : 0;
            sdata[WD1791_ADDR_CRC1] = 0;
            sdata[WD1791_ADDR_CRC2] = 0;

            pWD1791->addrActive = TRUE;
            pWD1791->drq = TRUE;

            break;

        case WD1791_CMD_READ_TRACK:
            dj2d_info->busyTimeout = 0;  /* BUSY not cleared until all bytes read */
            pWD1791->readTrkActive = TRUE;
            pWD1791->trkCount = 0;
            pWD1791->dataCount = 0;
            pWD1791->sector = 1;
            pWD1791->drq = TRUE;
            break;

        case WD1791_CMD_WRITE_TRACK:
            if ((uptr->flags & UNIT_DJ2D_WPROTECT) || dj2d_info->writeProtect) {
                sim_debug(DEBUG_MSG, &dj2d_dev, DJ2D_SNAME ": Disk write protected. uptr->flags=%04x writeProtect=%04x\n", uptr->flags & UNIT_DJ2D_WPROTECT, dj2d_info->writeProtect);
                pWD1791->intrq = TRUE;
            } else {
                dj2d_info->busyTimeout = 0;  /* BUSY not cleared until all bytes written */
                pWD1791->writeTrkActive = TRUE;
                pWD1791->trkCount = 0;
                pWD1791->dataCount = 0;
                pWD1791->sector = 1;
                pWD1791->idAddrMrk = 0;
                pWD1791->dataAddrMrk = 0;
                pWD1791->drq = TRUE;
            }
            break;

        case WD1791_CMD_FORCE_INTR:
            if (pWD1791->status & WD1791_STAT_BUSY) {
                pWD1791->status &= ~WD1791_STAT_BUSY;
                dj2d_info->busyTimeout = 0;
                statusUpdate = FALSE;
            }

            /* Reset Status */
            pWD1791->dataCount = 0;
            pWD1791->trkCount = 0;
            pWD1791->readActive = FALSE;
            pWD1791->readTrkActive = FALSE;
            pWD1791->writeActive = FALSE;
            pWD1791->writeTrkActive = FALSE;
            pWD1791->addrActive = FALSE;
            break;

        default:
            cData = 0xFF;
            sim_debug(ERROR_MSG, &dj2d_dev, DJ2D_SNAME ": UNRECOGNIZED CMD %02X\n", pWD1791->command);
            pWD1791->intrq = TRUE;
            break;
    }

    /**************************/
    /* Update Status Register */
    /**************************/

    /* drive not ready bit */
    pWD1791->status &= ~WD1791_STAT_NOTREADY;
    pWD1791->status |= (uptr->fileref == NULL) ? WD1791_STAT_NOTREADY : 0x00;

    /* DRQ bit */
    pWD1791->status &= ~WD1791_STAT_DRQ;
    pWD1791->status |= (pWD1791->drq) ? WD1791_STAT_DRQ : 0x00;

    switch(pWD1791->command & 0xf0) {
        case WD1791_CMD_RESTORE:
        case WD1791_CMD_SEEK:
        case WD1791_CMD_STEP:
        case WD1791_CMD_STEPU:
        case WD1791_CMD_STEPIN:
        case WD1791_CMD_STEPINU:
        case WD1791_CMD_STEPOUT:
        case WD1791_CMD_STEPOUTU:
        case WD1791_CMD_FORCE_INTR:
            if (statusUpdate) {
                pWD1791->status &= ~WD1791_STAT_HEADLOAD;
                pWD1791->status &= ~WD1791_STAT_WRITEPROT;
                pWD1791->status &= ~WD1791_STAT_CRCERROR;
                pWD1791->status &= ~WD1791_STAT_TRACK0;
                pWD1791->status &= ~WD1791_STAT_INDEX;
                pWD1791->status |= ((uptr->flags & UNIT_DJ2D_WPROTECT) || dj2d_info->writeProtect) ? WD1791_STAT_WRITEPROT : 0x00;
                pWD1791->status |= (pWD1791->track) ? 0x00 : WD1791_STAT_TRACK0;
                pWD1791->status |= (dj2d_info->headLoaded[dj2d_info->currentDrive]) ? WD1791_STAT_HEADLOAD : 0x00;
                pWD1791->status |= (pWD1791->index) ? WD1791_STAT_INDEX : 0x00;
            }
            break;

        case WD1791_CMD_READ:
            pWD1791->status &= ~WD1791_STAT_LOSTDATA;
            pWD1791->status &= ~WD1791_STAT_NOTFOUND;
            pWD1791->status &= ~WD1791_STAT_CRCERROR;
            pWD1791->status &= ~WD1791_STAT_RTYPELSB;
            break;

        case WD1791_CMD_WRITE:
            pWD1791->status &= ~WD1791_STAT_WRITEPROT;
            pWD1791->status &= ~WD1791_STAT_LOSTDATA;
            pWD1791->status &= ~WD1791_STAT_NOTFOUND;
            pWD1791->status &= ~WD1791_STAT_CRCERROR;
            pWD1791->status &= ~WD1791_STAT_RTYPELSB;
            pWD1791->status |= ((uptr->flags & UNIT_DJ2D_WPROTECT) || dj2d_info->writeProtect) ? WD1791_STAT_WRITEPROT : 0x00;
            break;

        case WD1791_CMD_READ_ADDRESS:
            pWD1791->status &= ~0x20;
            pWD1791->status &= ~0x40;
            pWD1791->status &= ~WD1791_STAT_LOSTDATA;
            pWD1791->status &= ~WD1791_STAT_NOTFOUND;
            pWD1791->status &= ~WD1791_STAT_CRCERROR;
            break;

        case WD1791_CMD_READ_TRACK:
            pWD1791->status &= ~0x08;
            pWD1791->status &= ~0x10;
            pWD1791->status &= ~0x20;
            pWD1791->status &= ~0x40;
            pWD1791->status &= ~WD1791_STAT_LOSTDATA;
            break;

        case WD1791_CMD_WRITE_TRACK:
            pWD1791->status &= ~0x08;
            pWD1791->status &= ~0x10;
            pWD1791->status &= ~WD1791_STAT_WRITEPROT;
            pWD1791->status &= ~WD1791_STAT_LOSTDATA;
            pWD1791->status |= ((uptr->flags & UNIT_DJ2D_WPROTECT) || dj2d_info->writeProtect) ? WD1791_STAT_WRITEPROT : 0x00;
            break;
    }

    sim_debug(CMD_MSG, &dj2d_dev,
            DJ2D_SNAME ": CMD cmd=%02X (%s) drive=%d side=%d track=%03d sector=%03d status=%02X\n",
            pWD1791->command, DJ2D_CommandString(pWD1791->command), dj2d_info->currentDrive,
            dj2d_info->side[dj2d_info->currentDrive],
            pWD1791->track, pWD1791->sector, pWD1791->status);

    return(cData);
}

/*
** The DJ2D has 1016 bytes of PROM followed by 8 memory-mapped
** I/O registers.
*/
static int32 dj2dprom(int32 Addr, int32 rw, int32 Data)
{
    /*
    ** Check for memory-mapped I/O
    */
    if ((Addr & DJ2D_REG_BASE) == DJ2D_REG_BASE) {
        if (rw == DJ2D_MEM_READ) { /* Read */
            return(DJ2D_Read(Addr));
        } else {                   /* Write */
            return(DJ2D_Write(Addr, Data));
        }
    }

    /*
    ** Read from PROM
    */
    if (rw == DJ2D_MEM_READ) {
        return(dj2d_prom[Addr & DJ2D_PROM_MASK]);
    }

    /*
    ** Writes are ignored and return 0xff
    */
    return 0xff;
}

/*
** The DJ2D has 1K of RAM following the PROM
*/
static int32 dj2dmem(int32 Addr, int32 rw, int32 Data)
{
    if (rw == DJ2D_MEM_WRITE) {
        dj2d_mem[Addr & DJ2D_MEM_MASK] = Data;
    }
    else {
        Data = dj2d_mem[Addr & DJ2D_MEM_MASK];
    }

    return Data;
}

