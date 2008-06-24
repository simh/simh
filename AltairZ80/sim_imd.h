/*************************************************************************
 *                                                                       *
 * $Id: sim_imd.h 1904 2008-05-21 06:57:57Z hharte $                     *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     ImageDisk Disk Image File access module for SIMH, definitions.    *
 *     see :                                                             *
 *     for details on the ImageDisk format and other utilities.          *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

typedef struct {
    unsigned char mode;
    unsigned char cyl;
    unsigned char head;
    unsigned char nsects;
    unsigned char sectsize;
} IMD_HEADER;


#define IMD_FLAG_SECT_HEAD_MAP  (1 << 6)
#define IMD_FLAG_SECT_CYL_MAP   (1 << 7)

#define SECT_RECORD_UNAVAILABLE         0   /* Data could not be read from the original media */
#define SECT_RECORD_NORM                1   /* Normal Data */
#define SECT_RECORD_NORM_COMP           2   /* Compressed Normal Data */
#define SECT_RECORD_NORM_DAM            3   /* Normal Data with deleted address mark */
#define SECT_RECORD_NORM_DAM_COMP       4   /* Compressed Normal Data with deleted address mark */
#define SECT_RECORD_NORM_ERR            5   /* Normal Data */
#define SECT_RECORD_NORM_COMP_ERR       6   /* Compressed Normal Data */
#define SECT_RECORD_NORM_DAM_ERR        7   /* Normal Data with deleted address mark */
#define SECT_RECORD_NORM_DAM_COMP_ERR   8   /* Compressed Normal Data with deleted address mark */

#define MAX_CYL     80
#define MAX_HEAD    2
#define MAX_SPT     26

#define FD_FLAG_WRITELOCK   1

#define IMD_DISK_IO_ERROR_GENERAL       (1 << 0)    /* General data error. */
#define IMD_DISK_IO_ERROR_CRC           (1 << 1)    /* Data read/written, but got a CRC error. */
#define IMD_DISK_IO_DELETED_ADDR_MARK   (1 << 2)    /* Sector had a deleted address mark */
#define IMD_DISK_IO_COMPRESSED          (1 << 3)    /* Sector is compressed in the IMD file (Read Only) */

#define IMD_MODE_500K_FM        0
#define IMD_MODE_300K_FM        1
#define IMD_MODE_250K_FM        2
#define IMD_MODE_500K_MFM       3
#define IMD_MODE_300K_MFM       4
#define IMD_MODE_250K_MFM       5

#define IMD_MODE_FM(x)      (x <= IMD_MODE_250K_FM)
#define IMD_MODE_MFM(x)     (x >= IMD_MODE_500K_MFM)

typedef struct {
    unsigned char mode;
    unsigned char nsects;
    unsigned int sectsize;
    unsigned int sectorOffsetMap[MAX_SPT];
    unsigned char start_sector;
} TRACK_INFO;

typedef struct {
    FILE *file;
    unsigned int ntracks;
    unsigned char nsides;
    unsigned char flags;
    TRACK_INFO track[MAX_CYL][MAX_HEAD];
} DISK_INFO;

extern DISK_INFO *diskOpen(FILE *fileref, int isVerbose); /*char *filename); */
extern unsigned int diskClose(DISK_INFO *myDisk);
extern unsigned int imdGetSides(DISK_INFO *myDisk);
extern unsigned int imdIsWriteLocked(DISK_INFO *myDisk);

extern int sectSeek(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head);
extern int sectRead(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head, unsigned int Sector, unsigned char *buf, unsigned int buflen, unsigned int *flags, unsigned int *readlen);
extern int sectWrite(DISK_INFO *myDisk, unsigned int Cyl, unsigned int Head, unsigned int Sector, unsigned char *buf, unsigned int buflen, unsigned int *flags, unsigned int *writelen);
