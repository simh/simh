/*  s100_icom.c: iCOM FD3712/FD3812 Flexible Disk System
  
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
*/

/*
    These functions support simulated iCOM FD3712 and FD3812
    floppy disk systems. The FD3712 supports IBM Diskette type 1
    single-density and the FD3812 also supports IBM Diskette
    type 2D double-density.

    The interface board provides 2 I/O ports:

    Command Register     Port C0    Output
    Data In Register     Port C0    Input
    Data Out Register    Port C1    Output

    +---------------------------------------------------------+
    |                                                         |
    |                      COMMAND SET                        |
    |                                                         |
    +---------------------------------------------------------+
    |       COMMAND       | 7 6 5 4 3 2 1 0 | BUSY | HEX CODE |
    +---------------------------------------------------------+
    | EXAMINE STATUS      | 0 0 0 0 0 0 0 0 | No   |    00    |
    | READ                | 0 0 0 0 0 0 1 1 | Yes  |    03    |
    | WRITE               | 0 0 0 0 0 1 0 1 | Yes  |    05    |
    | READ CRC            | 0 0 0 0 0 1 1 1 | Yes  |    07    |
    | SEEK                | 0 0 0 0 1 0 0 1 | Yes  |    09    |
    | CLEAR ERROR FLAGS   | 0 0 0 0 1 0 1 1 | No   |    0B    |
    | SEEK TRACK ZERO     | 0 0 0 0 1 1 0 1 | Yes  |    0D    |
    | WRITE DEL DATA MARK | 0 0 0 0 1 1 1 1 | Yes  |    0F    |
    | LOAD TRACK ADDRESS  | 0 0 0 1 0 0 0 1 | No   |    11    |
    | LOAD UNIT/SECTOR    | 0 0 1 0 0 0 0 1 | No   |    21    |
    | LOAD WRITE BUFFER   | 0 0 1 1 0 0 0 1 | No   |    31    |
    | EXAMINE READ BUFFER | 0 1 0 0 0 0 0 0 | No   |    40    |
    | SHIFT READ BUFFER   | 0 1 0 0 0 0 0 1 | No   |    41    |
    | CLEAR CONTROLLER    | 1 0 0 0 0 0 0 1 | No   |    81    |
    | LOAD CONFIGURATION* | 0 0 0 1 1 0 0 1 | No   |    15    |
    +---------------------------------------------------------+
    | * FD3812 Only                                           |
    +---------------------------------------------------------+

    +---------------------------------------------------------------+
    |                                                               |
    |                      DISK STATUS BITS                         |
    |                                                               |
    +---------------------------------------------------------------+
    | BIT | STATUS SIGNAL       | DESCRIPTION                       |
    +---------------------------------------------------------------+
    |  7  | DELETED DATA MARK   | The simulator does not implement  |
    |     |                     | this bit.                         |
    +---------------------------------------------------------------+
    |  6  | MEDIA STATUS        | This bit is always set.           |
    +---------------------------------------------------------------+
    |  5  | DRIVE FAIL          | This bit is set if any if a drive |
    |     |                     | is not attached using the         |
    |     |                     | "ATTACH" command or there is a    |
    |     |                     | problem reading from or writing   |
    |     |                     | to the attached file.             |
    +---------------------------------------------------------------+
    |  4  | WRITE PROTECT       | This bit is set if the selected   |
    |     |                     | drive contains a write protected  |
    |     |                     | diskette. This condition should   |
    |     |                     | not be tested if the selected     |
    |     |                     | drive has a "DRIVE FAIL" status.  |
    |     |                     | Use "SET ICOM WRTPROT" to write   |
    |     |                     | protect an attached diskette and  |
    |     |                     | "SET ICOM WRTENB" to enable       |
    |     |                     | writing.                          |
    +---------------------------------------------------------------+
    |  3  | CRC ERROR           | This bit is set when an error has |
    |     |                     | occurred during the previous      |
    |     |                     | command. This bit must be tested  |
    |     |                     | after all read, write, and seek   |
    |     |                     | operations. The simulator does    |
    |     |                     | not implement this bit.           |
    +---------------------------------------------------------------+
    |  2  | UNIT SELECT MSB     | Bits 2 and 1 contain the address  |
    +---------------------------| of the drive currently being      |
    |  1  | UNIT SELECT LSB     | selected by the controller.       |
    +---------------------------------------------------------------+
    |  0  | BUSY                | This bit is set when a read,      |
    |     |                     | write, seek command is sent to    |
    |     |                     | the controller.                   |
    +---------------------------------------------------------------+

    B = Memory Size - 16K

    32K:  B = 32K - 16K = 16K = 04000H
    48K:  B = 48K = 16K = 32K = 08000H
    62K:  B = 62K = 16K = 46K = 0B800H
    64K:  B = 64K = 16K = 48K = 0C000H 

    +----------------------------------------------------------------------+
    |                                                                      |
    |                 CP/M 1.41 Single Density Disk Layout                 |
    |                                                                      |
    +----------------------------------------------------------------------+
    | Track | Sector | Image Offset | Memory Address | Module              |
    +----------------------------------------------------------------------+
    | 00    | 01     | 0000-007FH   | 0080H          | SD Disk Boot Loader |
    +----------------------------------------------------------------------+
    | 00    | 02-17  | 0080-087FH   | 2900H+B        | CCP                 |
    +----------------------------------------------------------------------+
    | 00    | 18-26  | 0880-0CFFH   | 3100H+B        | BDOS                |
    | 01    | 01-17  | 0D00-157FH   | 3580H+B        | BDOS                |
    +----------------------------------------------------------------------+
    | 01    | 18-21  | 1580-177FH   | 3E00H+B        | BIOS                |
    +----------------------------------------------------------------------+
    | 01    | 22-26  |                               | Not Used            |
    +----------------------------------------------------------------------+

    +----------------------------------------------------------------------+
    |                                                                      |
    |                 CP/M 1.41 Double Density Disk Layout                 |
    |                                                                      |
    +----------------------------------------------------------------------+
    | Track | Sector | Image Offset | Memory Address | Module              |
    +----------------------------------------------------------------------+
    | 00    | 01     | 0000-007FH   | 0080H          | DD Disk Boot Loader |
    +----------------------------------------------------------------------+
    | 00    | 02-26  |              |                | Not Used            |
    +----------------------------------------------------------------------+
    | 01    | 01-09  | 0D00-14FFH   | 2900H+B        | CCP                 |
    +----------------------------------------------------------------------+
    | 01    | 10-21  | 1500-21FFH   | 3100H+B        | BDOS                |
    +----------------------------------------------------------------------+
    | 01    | 22-23  | 2200-23FFH   | 3E00H+B        | BIOS                |
    +----------------------------------------------------------------------+
    | 01    | 24-26  |              |                | Not Used            |
    +----------------------------------------------------------------------+

*/

/* #define DBG_MSG */

#include "altairz80_defs.h"
#include "sim_imd.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern DEVICE *find_dev (const char *cptr);
extern uint32 getClockFrequency(void);

#define ICOM_MAX_DRIVES        4
#define ICOM_SD_SECTOR_LEN     128
#define ICOM_DD_SECTOR_LEN     256
#define ICOM_SPT               26
#define ICOM_TRACKS            77
#define ICOM_SD_CAPACITY       (256256)      /* Default iCOM Single Density Disk Capacity */
#define ICOM_DD_CAPACITY       (509184)      /* Default iCOM Double Density Disk Capacity */

#define ICOM_IO_BASE      0xc0
#define ICOM_IO_SIZE      2

#define ICOM_PROM_BASE   0xf000
#define ICOM_PROM_SIZE   1024
#define ICOM_PROM_MASK   (ICOM_PROM_SIZE-1)
#define ICOM_MEM_BASE    0xf400
#define ICOM_MEM_SIZE    256                 /* Must be on a page boundary */
#define ICOM_MEM_MASK    (ICOM_MEM_SIZE-1)

static uint8 icom_mem[ICOM_MEM_SIZE];

/* iCOM PROMs are 1024 bytes */

static uint8 icom_3712_prom[ICOM_PROM_SIZE] = {
    0xc3, 0x73, 0xf0, 0x20, 0x41, 0x4c, 0x54, 0x41, 
    0x49, 0x52, 0x43, 0x20, 0xc3, 0x85, 0xf0, 0x15, 
    0xc3, 0xa6, 0xf0, 0xc3, 0xc7, 0xf0, 0xc3, 0x06, 
    0xf4, 0xc3, 0x09, 0xf4, 0xc3, 0x0c, 0xf4, 0xc3, 
    0x0f, 0xf4, 0xc3, 0x12, 0xf4, 0xc3, 0x15, 0xf4, 
    0xc3, 0x6b, 0xf1, 0xc3, 0x73, 0xf1, 0xc3, 0x6e, 
    0xf1, 0xc3, 0x7d, 0xf1, 0xc3, 0x82, 0xf1, 0xc3, 
    0x88, 0xf1, 0xc3, 0xc5, 0xf1, 0xc9, 0x00, 0x00, 
    0xc3, 0x64, 0xf1, 0xc3, 0x5a, 0xf2, 0x20, 0x33, 
    0x37, 0x31, 0x32, 0x2d, 0x56, 0x32, 0x31, 0x20, 
    0x28, 0x43, 0x29, 0x20, 0x4c, 0x49, 0x46, 0x45, 
    0x42, 0x4f, 0x41, 0x54, 0x20, 0x41, 0x53, 0x53, 
    0x4f, 0x43, 0x49, 0x41, 0x54, 0x45, 0x53, 0x20, 
    0x31, 0x39, 0x37, 0x39, 0x20, 0x21, 0xe0, 0xf3, 
    0xc3, 0x7f, 0xf0, 0x21, 0xf0, 0xf3, 0xc3, 0x7f, 
    0xf0, 0x21, 0x68, 0xf3, 0xc3, 0x7f, 0xf0, 0x31, 
    0x80, 0x00, 0xcd, 0x8f, 0xf2, 0x31, 0x80, 0x00, 
    0xcd, 0x5a, 0xf2, 0x0e, 0x00, 0xcd, 0x6e, 0xf1, 
    0x01, 0x80, 0x00, 0xcd, 0x82, 0xf1, 0xcd, 0x88, 
    0xf1, 0xc2, 0x88, 0xf0, 0x21, 0x00, 0xf4, 0xeb, 
    0x21, 0x10, 0xf0, 0xc3, 0x80, 0x00, 0x22, 0x40, 
    0xf4, 0x11, 0xf0, 0xff, 0x19, 0x11, 0x20, 0xf4, 
    0x06, 0x10, 0xcd, 0x86, 0xf2, 0x11, 0x80, 0xff, 
    0x19, 0xaf, 0x32, 0x48, 0xf4, 0xcd, 0x4f, 0xf1, 
    0xaf, 0x32, 0x04, 0x00, 0xc3, 0x28, 0xf1, 0x31, 
    0x00, 0x01, 0xcd, 0x5a, 0xf2, 0x0e, 0x00, 0xcd, 
    0x6e, 0xf1, 0x2a, 0x40, 0xf4, 0x11, 0x00, 0xeb, 
    0x19, 0x24, 0x3e, 0x04, 0xcd, 0xf7, 0xf0, 0x0e, 
    0x01, 0xcd, 0x6e, 0xf1, 0x2a, 0x40, 0xf4, 0x11, 
    0x00, 0xeb, 0x19, 0x11, 0x80, 0x0c, 0x19, 0x3e, 
    0x01, 0xcd, 0xf7, 0xf0, 0xc3, 0x28, 0xf1, 0x32, 
    0x32, 0xf4, 0x22, 0x33, 0xf4, 0x3a, 0x41, 0xf4, 
    0x3d, 0xbc, 0xda, 0x0b, 0xf1, 0xcd, 0x88, 0xf1, 
    0xc2, 0xc7, 0xf0, 0x2a, 0x33, 0xf4, 0x11, 0x80, 
    0x01, 0x19, 0x3a, 0x32, 0xf4, 0xc6, 0x03, 0xfe, 
    0x1b, 0xda, 0xf7, 0xf0, 0xd6, 0x1a, 0x11, 0x00, 
    0xf3, 0x19, 0xfe, 0x01, 0xc2, 0xf7, 0xf0, 0xc9, 
    0x01, 0x80, 0x00, 0xcd, 0x82, 0xf1, 0x3e, 0xc3, 
    0x32, 0x00, 0x00, 0x32, 0x05, 0x00, 0x2a, 0x40, 
    0xf4, 0x23, 0x23, 0x23, 0x22, 0x01, 0x00, 0x11, 
    0x03, 0xf3, 0x19, 0x22, 0x06, 0x00, 0x3a, 0x04, 
    0x00, 0x4f, 0x11, 0xfa, 0xf7, 0x19, 0xe9, 0x7e, 
    0xb7, 0xc8, 0x4e, 0x23, 0xe5, 0xcd, 0x5c, 0xf1, 
    0xe1, 0xc3, 0x4f, 0xf1, 0x2a, 0x40, 0xf4, 0x11, 
    0x0c, 0x00, 0x19, 0xe9, 0x21, 0x00, 0xf4, 0x06, 
    0x00, 0x09, 0xc9, 0xc3, 0x67, 0xf2, 0x79, 0x32, 
    0x31, 0xf4, 0xc9, 0x79, 0x32, 0x30, 0xf4, 0x3e, 
    0xff, 0x32, 0x27, 0xf4, 0xc9, 0x79, 0x32, 0x32, 
    0xf4, 0xc9, 0x60, 0x69, 0x22, 0x33, 0xf4, 0xc9, 
    0xcd, 0x0a, 0xf2, 0xc2, 0x06, 0xf2, 0x0e, 0x0a, 
    0x3e, 0x03, 0xcd, 0x71, 0xf2, 0xe6, 0x28, 0xca, 
    0xa4, 0xf1, 0xcd, 0x7e, 0xf2, 0x0d, 0xc2, 0x90, 
    0xf1, 0xc3, 0x06, 0xf2, 0x2a, 0x33, 0xf4, 0x0e, 
    0x80, 0x3e, 0x40, 0xd3, 0xc0, 0xdb, 0xc0, 0x77, 
    0x23, 0xaf, 0xd3, 0xc0, 0x0d, 0x3e, 0x41, 0xd3, 
    0xc0, 0xdb, 0xc0, 0x77, 0x23, 0xaf, 0xd3, 0xc0, 
    0x0d, 0xc2, 0xb5, 0xf1, 0xc9, 0xcd, 0x0a, 0xf2, 
    0xc2, 0x06, 0xf2, 0x2a, 0x33, 0xf4, 0x0e, 0x80, 
    0x7e, 0xd3, 0xc1, 0x3e, 0x31, 0xd3, 0xc0, 0xaf, 
    0xd3, 0xc0, 0x23, 0x0d, 0xc2, 0xd0, 0xf1, 0x0e, 
    0x0a, 0x3e, 0x05, 0xcd, 0x71, 0xf2, 0xe6, 0x20, 
    0xca, 0xf1, 0xf1, 0xcd, 0x7e, 0xf2, 0xc3, 0x06, 
    0xf2, 0x3a, 0x2f, 0xf4, 0xe6, 0x40, 0xc8, 0x3e, 
    0x07, 0xcd, 0x71, 0xf2, 0xe6, 0x28, 0xc8, 0xcd, 
    0x7e, 0xf2, 0x0d, 0xc2, 0xe1, 0xf1, 0x3e, 0x01, 
    0xb7, 0xc9, 0xaf, 0xd3, 0xc1, 0x3e, 0x15, 0xcd, 
    0x80, 0xf2, 0xcd, 0x19, 0xf2, 0xcd, 0x2d, 0xf2, 
    0xc9, 0x3a, 0x30, 0xf4, 0xe6, 0x03, 0x0f, 0x0f, 
    0x4f, 0x3a, 0x32, 0xf4, 0xb1, 0xd3, 0xc1, 0x3e, 
    0x21, 0xcd, 0x80, 0xf2, 0xc9, 0x0e, 0x02, 0x3a, 
    0x31, 0xf4, 0x21, 0x27, 0xf4, 0xbe, 0xc8, 0x77, 
    0x3a, 0x31, 0xf4, 0xd3, 0xc1, 0x3e, 0x11, 0xcd, 
    0x80, 0xf2, 0x3e, 0x09, 0xcd, 0x71, 0xf2, 0xe6, 
    0x28, 0xc8, 0xcd, 0x7e, 0xf2, 0x36, 0xff, 0x0d, 
    0xc2, 0x2d, 0xf2, 0xcd, 0x62, 0xf2, 0x3e, 0x02, 
    0xb7, 0xc9, 0xaf, 0x32, 0x30, 0xf4, 0x3c, 0x32, 
    0x32, 0xf4, 0x3e, 0x81, 0xcd, 0x80, 0xf2, 0xcd, 
    0x19, 0xf2, 0x3e, 0xff, 0x32, 0x27, 0xf4, 0x3e, 
    0x0d, 0xcd, 0x80, 0xf2, 0xdb, 0xc0, 0xe6, 0x01, 
    0xc2, 0x74, 0xf2, 0xdb, 0xc0, 0xc9, 0x3e, 0x0b, 
    0xd3, 0xc0, 0xaf, 0xd3, 0xc0, 0xc9, 0x7e, 0x12, 
    0x23, 0x13, 0x05, 0xc2, 0x86, 0xf2, 0xc9, 0x11, 
    0x00, 0xf4, 0x06, 0x08, 0x3e, 0xc3, 0x12, 0x13, 
    0x7e, 0x12, 0x23, 0x13, 0x7e, 0x12, 0x23, 0x13, 
    0x05, 0xc2, 0x94, 0xf2, 0xc9, 0x3e, 0x03, 0xd3, 
    0x10, 0x3e, 0x11, 0xd3, 0x10, 0xc9, 0xdb, 0x10, 
    0xe6, 0x01, 0x3e, 0x00, 0xc8, 0x2f, 0xc9, 0xdb, 
    0x10, 0xe6, 0x01, 0xca, 0xb7, 0xf2, 0xdb, 0x11, 
    0xe6, 0x7f, 0xca, 0xb7, 0xf2, 0xc9, 0xdb, 0x10, 
    0xe6, 0x02, 0xca, 0xc6, 0xf2, 0x79, 0xd3, 0x11, 
    0xc9, 0xc9, 0xc9, 0xdb, 0x00, 0xe6, 0x01, 0x3e, 
    0x00, 0xc0, 0x2f, 0xc9, 0xdb, 0x00, 0xe6, 0x01, 
    0xc2, 0xdc, 0xf2, 0xdb, 0x01, 0xe6, 0x7f, 0xca, 
    0xdc, 0xf2, 0xc9, 0xdb, 0x00, 0xe6, 0x80, 0xc2, 
    0xeb, 0xf2, 0x79, 0xd3, 0x01, 0xc9, 0x3a, 0x48, 
    0xf4, 0xb7, 0xc2, 0x0c, 0xf3, 0x3e, 0x11, 0xd3, 
    0x03, 0xaf, 0xd3, 0x02, 0x32, 0x47, 0xf4, 0x3e, 
    0x84, 0x32, 0x48, 0xf4, 0x79, 0xfe, 0x0a, 0xc2, 
    0x1a, 0xf3, 0x32, 0x49, 0xf4, 0x3a, 0x47, 0xf4, 
    0xb7, 0xc8, 0x79, 0xfe, 0x08, 0xca, 0x4f, 0xf3, 
    0xfe, 0x09, 0xca, 0x5a, 0xf3, 0xfe, 0x0d, 0xca, 
    0x38, 0xf3, 0xd8, 0x3a, 0x47, 0xf4, 0x3c, 0xe5, 
    0x21, 0x48, 0xf4, 0xbe, 0xe1, 0xc2, 0x48, 0xf3, 
    0x3a, 0x47, 0xf4, 0xb7, 0xc2, 0x47, 0xf3, 0x3a, 
    0x49, 0xf4, 0xfe, 0x0d, 0xc8, 0x0e, 0x0a, 0xaf, 
    0x32, 0x47, 0xf4, 0x79, 0x32, 0x49, 0xf4, 0xdb, 
    0x02, 0xe6, 0x11, 0xca, 0x4f, 0xf3, 0x79, 0xd3, 
    0x03, 0xc9, 0x0e, 0x20, 0xcd, 0x0c, 0xf3, 0x3a, 
    0x47, 0xf4, 0xe6, 0x07, 0xc2, 0x5a, 0xf3, 0xc9, 
    0xa8, 0xf3, 0xd2, 0xf2, 0xa1, 0xf3, 0x78, 0xf3, 
    0x8e, 0xf3, 0xf6, 0xf2, 0x8e, 0xf3, 0x78, 0xf3, 
    0xcd, 0x84, 0xf3, 0xca, 0x78, 0xf3, 0x7e, 0xe6, 
    0x7f, 0x36, 0x00, 0xc9, 0x21, 0x4b, 0xf4, 0x7e, 
    0xb7, 0xcc, 0x1f, 0xc0, 0x77, 0xc9, 0x3a, 0x4a, 
    0xf4, 0xfe, 0x0d, 0xc2, 0x98, 0xf3, 0xb9, 0xc8, 
    0x79, 0x32, 0x4a, 0xf4, 0x41, 0xcd, 0x19, 0xc0, 
    0xc9, 0xcd, 0x84, 0xf3, 0xc8, 0x3e, 0xff, 0xc9, 
    0x21, 0x00, 0x00, 0x22, 0x4a, 0xf4, 0xc9, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xd1, 0xf2, 0xd2, 0xf2, 0xd3, 0xf2, 0xdc, 0xf2, 
    0xeb, 0xf2, 0xf6, 0xf2, 0xeb, 0xf2, 0xdc, 0xf2, 
    0xa5, 0xf2, 0xd2, 0xf2, 0xae, 0xf2, 0xb7, 0xf2, 
    0xc6, 0xf2, 0xf6, 0xf2, 0xc6, 0xf2, 0xb7, 0xf2, 
};

static uint8 icom_3812_prom[ICOM_PROM_SIZE] = {
    0xc3, 0x46, 0xf0, 0x06, 0x80, 0x7e, 0x12, 0x23,
    0x13, 0x05, 0xc2, 0x05, 0xf0, 0xc9, 0xff, 0x3a,
    0xc3, 0x6d, 0xf0, 0xc3, 0x8a, 0xf0, 0x79, 0x32,
    0x31, 0xf4, 0xc9, 0x79, 0x32, 0x32, 0xf4, 0xc9,
    0x60, 0x69, 0x22, 0x33, 0xf4, 0xc9, 0xff, 0xff,
    0xc3, 0x08, 0xf1, 0xc3, 0x14, 0xf1, 0xc3, 0x16,
    0xf0, 0xc3, 0x1b, 0xf0, 0xc3, 0x20, 0xf0, 0xc3,
    0x30, 0xf1, 0xc3, 0x7b, 0xf1, 0xc3, 0x21, 0xf1,
    0xc3, 0x61, 0xf3, 0xc3, 0xa4, 0xf3, 0x31, 0x80,
    0x00, 0xcd, 0xa4, 0xf3, 0x21, 0x00, 0x00, 0x22,
    0x30, 0xf4, 0x0e, 0x01, 0xcd, 0x1b, 0xf0, 0x21,
    0x80, 0x00, 0x22, 0x33, 0xf4, 0xcd, 0x30, 0xf1,
    0xc2, 0x46, 0xf0, 0x21, 0x00, 0xf4, 0xeb, 0x21,
    0x10, 0xf0, 0xc3, 0x80, 0x00, 0x22, 0x40, 0xf4,
    0x11, 0xf0, 0xff, 0x19, 0x11, 0x20, 0xf4, 0x06,
    0x10, 0xcd, 0x05, 0xf0, 0x11, 0x80, 0xff, 0x19,
    0xcd, 0xdf, 0xf3, 0xaf, 0x32, 0x04, 0x00, 0xc3,
    0xe1, 0xf0, 0x31, 0x00, 0x01, 0xcd, 0xa4, 0xf3,
    0x21, 0x00, 0x01, 0x22, 0x30, 0xf4, 0x2a, 0x40,
    0xf4, 0x11, 0x00, 0xeb, 0x19, 0x3e, 0x01, 0x4f,
    0xc5, 0x32, 0x32, 0xf4, 0x22, 0x33, 0xf4, 0x7c,
    0x2a, 0x40, 0xf4, 0xbc, 0xd2, 0xb5, 0xf0, 0xcd,
    0x30, 0xf1, 0xc2, 0x8a, 0xf0, 0xc1, 0x79, 0x0f,
    0x79, 0x2a, 0x33, 0xf4, 0xda, 0xc3, 0xf0, 0xc6,
    0x04, 0x24, 0x24, 0x3c, 0x11, 0x80, 0x00, 0x19,
    0xfe, 0x35, 0xda, 0xdc, 0xf0, 0xd6, 0x34, 0xfe,
    0x03, 0x2a, 0x40, 0xf4, 0x11, 0x00, 0xec, 0x19,
    0xca, 0xdc, 0xf0, 0x24, 0xfe, 0x01, 0xc2, 0x9f,
    0xf0, 0x01, 0x80, 0x00, 0xcd, 0x20, 0xf0, 0x3e,
    0xc3, 0x32, 0x00, 0x00, 0x32, 0x05, 0x00, 0x2a,
    0x40, 0xf4, 0x23, 0x23, 0x23, 0x22, 0x01, 0x00,
    0x11, 0x03, 0xf3, 0x19, 0x22, 0x06, 0x00, 0x3a,
    0x04, 0x00, 0x4f, 0x11, 0xfa, 0xf7, 0x19, 0xe9,
    0xcd, 0x21, 0xf1, 0x3a, 0x30, 0xf4, 0x32, 0x3d,
    0xf4, 0xc3, 0xb6, 0xf3, 0x79, 0x32, 0x30, 0xf4,
    0xcd, 0x21, 0xf1, 0x3e, 0xff, 0x32, 0x27, 0xf4,
    0xc9, 0x3a, 0x39, 0xf4, 0x3c, 0xc8, 0xcd, 0x6f,
    0xf2, 0xc5, 0xcd, 0xf2, 0xf1, 0xc1, 0xc9, 0x11,
    0xcd, 0x6f, 0xf2, 0xcd, 0x57, 0xf3, 0xca, 0x5a,
    0xf1, 0x21, 0x30, 0xf4, 0x11, 0x39, 0xf4, 0xcd,
    0x2b, 0xf2, 0xc2, 0x4e, 0xf1, 0x1a, 0xbe, 0xc2,
    0x4e, 0xf1, 0xcd, 0xf2, 0xf1, 0xc0, 0x21, 0x30,
    0xf4, 0x11, 0x35, 0xf4, 0xcd, 0x2b, 0xf2, 0xca,
    0x64, 0xf1, 0x21, 0x30, 0xf4, 0xcd, 0x22, 0xf2,
    0xcd, 0x46, 0xf2, 0xc0, 0xcd, 0x57, 0xf3, 0xca,
    0x71, 0xf1, 0x3a, 0x32, 0xf4, 0x3c, 0x0f, 0xe6,
    0x80, 0x2a, 0x33, 0xf4, 0xeb, 0xcd, 0x9d, 0xf2,
    0xc8, 0xc3, 0x11, 0xcd, 0x6f, 0xf2, 0xcd, 0x57,
    0xf3, 0x2a, 0x33, 0xf4, 0xca, 0xb0, 0xf1, 0x21,
    0x30, 0xf4, 0x11, 0x39, 0xf4, 0xcd, 0x2b, 0xf2,
    0xc2, 0xbf, 0xf1, 0x1a, 0xbe, 0xca, 0xc3, 0xf1,
    0x3e, 0xff, 0x32, 0x39, 0xf4, 0x2a, 0x33, 0xf4,
    0xe5, 0x2a, 0x2c, 0xf4, 0x3a, 0x3b, 0xf4, 0x0f,
    0xda, 0xac, 0xf1, 0xe3, 0xcd, 0xf7, 0xf2, 0xe1,
    0xcd, 0xf7, 0xf2, 0x21, 0x30, 0xf4, 0xcd, 0x22,
    0xf2, 0xcd, 0x63, 0xf2, 0xc9, 0x2f, 0xfe, 0xcd,
    0xf2, 0xf1, 0xc0, 0x21, 0x30, 0xf4, 0x11, 0x39,
    0xf4, 0xcd, 0x25, 0xf2, 0x2a, 0x2c, 0xf4, 0xeb,
    0x2a, 0x33, 0xf4, 0xcd, 0x03, 0xf0, 0x2a, 0x40,
    0xf4, 0x11, 0x09, 0xf5, 0x19, 0x11, 0xf2, 0xf1,
    0xd5, 0x7e, 0xfe, 0x10, 0xc8, 0xfe, 0x13, 0xc8,
    0xfe, 0x16, 0xc8, 0xfe, 0x17, 0xc8, 0xd1, 0xaf,
    0xc9, 0x0e, 0x21, 0x39, 0xf4, 0x7e, 0x3c, 0xc8,
    0xcd, 0x22, 0xf2, 0x3e, 0xff, 0x32, 0x39, 0xf4,
    0xcd, 0x46, 0xf2, 0xc0, 0x3a, 0x3b, 0xf4, 0x0f,
    0xd2, 0x18, 0xf2, 0xcd, 0xf4, 0xf2, 0xcd, 0xb8,
    0xf2, 0xcd, 0x0a, 0xf3, 0xca, 0x1e, 0xf2, 0x11,
    0xcd, 0x0a, 0xf3, 0xcd, 0xf4, 0xf2, 0xcd, 0x63,
    0xf2, 0xc9, 0x11, 0x3d, 0xf4, 0x06, 0x03, 0xc3,
    0x05, 0xf0, 0x06, 0x1a, 0xb7, 0xf8, 0xbe, 0xc0,
    0x23, 0x13, 0x1a, 0xbe, 0xc0, 0x23, 0x13, 0x7e,
    0x3c, 0x0f, 0xe6, 0x7f, 0x4f, 0x1a, 0x3c, 0x0f,
    0xe6, 0x7f, 0xb9, 0xc9, 0xfe, 0x21, 0x3e, 0xff,
    0x32, 0x35, 0xf4, 0xaf, 0x32, 0x38, 0xf4, 0xcd,
    0x82, 0xf2, 0x3e, 0x01, 0xc0, 0x21, 0x3d, 0xf4,
    0x11, 0x35, 0xf4, 0xcd, 0x25, 0xf2, 0x78, 0xc8,
    0xc3, 0x7a, 0xf1, 0x3e, 0xff, 0x32, 0x35, 0xf4,
    0xcd, 0xcf, 0xf2, 0xc8, 0x3e, 0x01, 0xc9, 0xd1,
    0x21, 0x00, 0x00, 0x39, 0x31, 0x80, 0xf4, 0xe5,
    0x21, 0x7e, 0xf2, 0xe5, 0xeb, 0xe9, 0xe1, 0xf9,
    0xc9, 0x21, 0xcd, 0x28, 0xf3, 0xc2, 0x99, 0xf2,
    0x0e, 0x05, 0x3e, 0x03, 0xcd, 0xca, 0xf3, 0xe6,
    0x08, 0xc8, 0xcd, 0xd7, 0xf3, 0x0d, 0xc2, 0x8a,
    0xf2, 0x3e, 0x01, 0xb7, 0xc9, 0x21, 0x38, 0xf4,
    0xbe, 0xc4, 0xb8, 0xf2, 0x06, 0x80, 0x3e, 0x40,
    0xd3, 0xc0, 0xdb, 0xc0, 0x12, 0x13, 0x34, 0x05,
    0xc2, 0xaa, 0xf2, 0xaf, 0xd3, 0xc0, 0xc8, 0x11,
    0x06, 0x80, 0x21, 0x38, 0xf4, 0x3e, 0x40, 0xd3,
    0xc0, 0xdb, 0xc0, 0x34, 0x05, 0xc2, 0xc1, 0xf2,
    0x78, 0xd3, 0xc0, 0xc8, 0xcd, 0x17, 0xf2, 0xcd,
    0x28, 0xf3, 0xc2, 0x99, 0xf2, 0x0e, 0x05, 0x3e,
    0x05, 0xcd, 0xca, 0xf3, 0x3a, 0x2f, 0xf4, 0xe6,
    0x40, 0xc8, 0x3e, 0x07, 0xcd, 0xca, 0xf3, 0xe6,
    0x08, 0xc8, 0xcd, 0xd7, 0xf3, 0x0d, 0xc2, 0xd7,
    0xf2, 0xc3, 0x99, 0xf2, 0x2a, 0x2c, 0xf4, 0x06,
    0x80, 0x3e, 0x30, 0xd3, 0xc0, 0x7e, 0xd3, 0xc1,
    0x23, 0x05, 0xc2, 0xfd, 0xf2, 0x78, 0xd3, 0xc0,
    0xc8, 0x0e, 0x06, 0x80, 0x3e, 0x40, 0xd3, 0xc0,
    0xdb, 0xc0, 0x4f, 0xaf, 0xd3, 0xc0, 0x3e, 0x30,
    0xd3, 0xc0, 0x79, 0xd3, 0xc1, 0xaf, 0xd3, 0xc0,
    0x05, 0xc2, 0x0c, 0xf3, 0xc9, 0xcd, 0xb7, 0xf2,
    0x16, 0x05, 0xcd, 0x3f, 0xf3, 0xd3, 0xc1, 0x3e,
    0x21, 0xcd, 0xd9, 0xf3, 0xcd, 0x6b, 0xf3, 0xc8,
    0x15, 0xc2, 0x2a, 0xf3, 0xc3, 0x99, 0xf2, 0x2a,
    0x3d, 0xf4, 0x7d, 0x0f, 0x0f, 0x5f, 0xcd, 0x5a,
    0xf3, 0x3a, 0x3f, 0xf4, 0xca, 0x53, 0xf3, 0x3c,
    0x0f, 0xe6, 0x3f, 0xb3, 0xc9, 0x06, 0x0b, 0x2a,
    0x30, 0xf4, 0x7c, 0xb7, 0xc8, 0x3e, 0x28, 0x85,
    0x4f, 0x21, 0x00, 0xf4, 0x06, 0x00, 0x09, 0x7e,
    0xe6, 0x02, 0xc9, 0x3a, 0x3e, 0xf4, 0x21, 0x27,
    0xf4, 0xbe, 0xc8, 0x77, 0x5f, 0x2a, 0x3d, 0xf4,
    0xcd, 0x5a, 0xf3, 0xca, 0x80, 0xf3, 0x3e, 0x10,
    0xd3, 0xc1, 0x3e, 0x15, 0xcd, 0xd9, 0xf3, 0x7b,
    0xb7, 0x3e, 0x0d, 0xca, 0x98, 0xf3, 0x7b, 0xd3,
    0xc1, 0x3e, 0x11, 0xcd, 0xd9, 0xf3, 0x3e, 0x09,
    0xcd, 0xca, 0xf3, 0xe6, 0x28, 0xc8, 0xcd, 0xb1,
    0xf3, 0xc3, 0x99, 0xf2, 0x3e, 0xff, 0x32, 0x39,
    0xf4, 0xaf, 0x32, 0x3d, 0xf4, 0x3c, 0x32, 0x3f,
    0xf4, 0x3e, 0x81, 0xcd, 0xd9, 0xf3, 0xcd, 0x3f,
    0xf3, 0xd3, 0xc1, 0x3e, 0x21, 0xcd, 0xd9, 0xf3,
    0x3e, 0xff, 0x32, 0x27, 0xf4, 0x32, 0x35, 0xf4,
    0x3e, 0x0d, 0xcd, 0xd9, 0xf3, 0xdb, 0xc0, 0xe6,
    0x01, 0xc2, 0xcd, 0xf3, 0xdb, 0xc0, 0xc9, 0x3e,
    0x0b, 0xd3, 0xc0, 0xaf, 0xd3, 0xc0, 0xc9, 0x7e,
    0xb7, 0xc8, 0x4e, 0xe5, 0xcd, 0xec, 0xf3, 0xe1,
    0x23, 0xc3, 0xdf, 0xf3, 0x2a, 0x40, 0xf4, 0x2e,
    0x0c, 0xe9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff 
};

static uint8 *icom_prom = icom_3712_prom;

/*
** ICOM Registers and Interface Controls
*/
typedef struct {
    uint8   status;         /* Status Register */
    uint8   track;          /* Track Register */
    uint8   sector;         /* Sector Register */
    uint8   command;        /* Command Register */
    uint8   rData;          /* Read Data Register */
    uint32  rDataBuf;       /* Read buffer index */
    uint8   wData;          /* Write Data Register */
    uint32  wDataBuf;       /* Write buffer index */
    uint8   formatMode;     /* format mode */
    uint16  bytesPerSec;    /* bytes per sector */
} ICOM_REG;

/* iCOM Registers */
#define ICOM_REG_COMMAND        0x00
#define ICOM_REG_DATAI          0x00
#define ICOM_REG_DATAO          0x01

/* iCOM Commands */
#define ICOM_CMD_STATUS         0x00
#define ICOM_CMD_CMDMSK         0x01
#define ICOM_CMD_READ           0x03
#define ICOM_CMD_WRITE          0x05
#define ICOM_CMD_READCRC        0x07
#define ICOM_CMD_SEEK           0x09
#define ICOM_CMD_CLRERRFLGS     0x0b
#define ICOM_CMD_TRACK0         0x0d
#define ICOM_CMD_WRITEDDM       0x0f
#define ICOM_CMD_LDTRACK        0x11
#define ICOM_CMD_LDUNITSEC      0x21
#define ICOM_CMD_LDWRITEBUFNOP  0x30
#define ICOM_CMD_LDWRITEBUF     0x31
#define ICOM_CMD_EXREADBUF      0x40
#define ICOM_CMD_SHREADBUF      0x41
#define ICOM_CMD_CLEAR          0x81
#define ICOM_CMD_LDCONF         0x15

#define ICOM_STAT_BUSY       0x01
#define ICOM_STAT_UNITMSK    0x06
#define ICOM_STAT_CRC        0x08
#define ICOM_STAT_WRITEPROT  0x10
#define ICOM_STAT_DRVFAIL    0x20
#define ICOM_STAT_MEDIASTAT  0x40
#define ICOM_STAT_DDM        0x80

#define ICOM_CONF_DD         0x10    /* Double Density */
#define ICOM_CONF_FM         0x20    /* Format Mode */

#define ICOM_TYPE_3712       0x00
#define ICOM_TYPE_3812       0x01

typedef struct {
    uint32    mem_base;       /* Memory Base Address                 */
    uint32    mem_size;       /* Memory Address space requirement    */
    uint32    io_base;        /* I/O Base Address                    */
    uint32    io_size;        /* I/O Address Space requirement       */
    uint32    prom_base;      /* Boot PROM Base Address              */
    uint32    prom_size;      /* Boot PROM Address space requirement */
    uint8     promEnabled;    /* PROM is enabled                     */
    uint8     boardType;      /* Interface Board Type                */
    uint8     rwsMs;          /* Read/Write Sector ms                */
    uint8     seekMs;         /* Seek ms                             */
    uint8     currentDrive;   /* Currently selected drive            */
    uint8     currentTrack[ICOM_MAX_DRIVES];
    uint32    msTime;         /* MS time for BUSY                    */
    ICOM_REG  ICOM;           /* ICOM Registers and Data             */
    UNIT *uptr[ICOM_MAX_DRIVES];
} ICOM_INFO;

static ICOM_INFO icom_info_data = {
    ICOM_MEM_BASE, ICOM_MEM_SIZE, ICOM_IO_BASE, ICOM_IO_SIZE, ICOM_PROM_BASE, ICOM_PROM_SIZE,
    TRUE, ICOM_TYPE_3812, 6, 10
};

static ICOM_INFO *icom_info = &icom_info_data;

/*
** Read and Write Data Ring Buffers
*/
#define DATA_MASK ICOM_DD_SECTOR_LEN-1

static uint8 rdata[ICOM_DD_SECTOR_LEN];
static uint8 wdata[ICOM_DD_SECTOR_LEN];

/* Local function prototypes */
static t_stat icom_reset(DEVICE *icom_dev);
static t_stat icom_svc(UNIT *uptr);
static t_stat icom_attach(UNIT *uptr, CONST char *cptr);
static t_stat icom_detach(UNIT *uptr);
static t_stat icom_boot(int32 unitno, DEVICE *dptr);
static t_stat icom_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat icom_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat icom_set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat icom_show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat icom_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat icom_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static uint32 calculate_icom_sec_offset(ICOM_REG *pICOM, uint8 track, uint8 sector);
static void icom_set_busy(uint32 msec);
static uint8 ICOM_Read(uint32 Addr);
static uint8 ICOM_Write(uint32 Addr, int32 data);
static const char * ICOM_CommandString(uint8 command);
static uint8 ICOM_Command(UNIT *uptr, ICOM_REG *pICOM, int32 data);
static uint32 ICOM_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static uint32 ICOM_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static uint32 ICOM_FormatTrack(UNIT *uptr, uint8 track, uint8 *buffer);
static uint8 ICOM_DriveNotReady(UNIT *uptr, ICOM_REG *pICOM);
static const char* icom_description(DEVICE *dptr);
static void showReadSec(void);
static void showWriteSec(void);
static int32 icomdev(int32 Addr, int32 rw, int32 data);
static int32 icomprom(int32 Addr, int32 rw, int32 data);
static int32 icommem(int32 Addr, int32 rw, int32 data);

static UNIT icom_unit[ICOM_MAX_DRIVES] = {
    { UDATA (icom_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, ICOM_DD_CAPACITY), 10000 },
    { UDATA (icom_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, ICOM_DD_CAPACITY), 10000 },
    { UDATA (icom_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, ICOM_DD_CAPACITY), 10000 },
    { UDATA (icom_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, ICOM_DD_CAPACITY), 10000 }
};

static REG icom_reg[] = {
    { DRDATAD (DRIVE, icom_info_data.currentDrive, 8, "Current drive register"), },
    { HRDATAD (STATUS, icom_info_data.ICOM.status, 8, "Status register"), },
    { HRDATAD (COMMAND, icom_info_data.ICOM.command, 8, "Command register"), },
    { HRDATAD (RDATA, icom_info_data.ICOM.rData, 8, "Read Data register"), },
    { HRDATAD (WDATA, icom_info_data.ICOM.wData, 8, "Write Data register"), },
    { DRDATAD (TRACK, icom_info_data.ICOM.track, 8, "Track register"), },
    { DRDATAD (SECTOR, icom_info_data.ICOM.sector, 8, "Sector register"), },
    { DRDATAD (RBUF, icom_info_data.ICOM.rDataBuf, 16, "Read data buffer index register"), },
    { DRDATAD (WBUF, icom_info_data.ICOM.wDataBuf, 16, "Write data buffer index register"), },
    { DRDATAD (FORMAT, icom_info_data.ICOM.formatMode, 8, "Current format mode register"), },
    { DRDATAD (DENSITY, icom_info_data.ICOM.bytesPerSec, 16, "Current density register"), },
    { FLDATAD (PROM, icom_info_data.promEnabled, 0, "PROM enabled bit"), },
    { DRDATAD (RWSMS, icom_info_data.rwsMs, 8, "Read/Write sector time (ms)"), },
    { DRDATAD (SEEKMS, icom_info_data.seekMs, 8, "Seek track to track time (ms)"), },
    { NULL }
};

#define ICOM_NAME  "iCOM 3712/3812 Floppy Disk Interface"
#define ICOM_SNAME "ICOM"

static const char* icom_description(DEVICE *dptr) {
    return ICOM_NAME;
}

#define UNIT_V_ICOM_WPROTECT     (UNIT_V_UF + 1)                      /* WRTENB / WRTPROT */
#define UNIT_ICOM_WPROTECT       (1 << UNIT_V_ICOM_WPROTECT)

static MTAB icom_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,        "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets interface board I/O base address"   },
    { MTAB_XTD|MTAB_VDV,    0,        "MEMBASE",  "MEMBASE",
        &icom_set_membase, &icom_show_membase, NULL, "Shows interface board memory base address"   },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PROM", "PROM={ENABLE|DISABLE}",
        &icom_set_prom, &icom_show_prom, NULL, "Set/Show PROM enabled/disabled status"},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "TYPE", "TYPE={3712|3812}",
        &icom_set_type, &icom_show_type, NULL, "Set/Show the current controller type" },
    { UNIT_ICOM_WPROTECT,  0,         "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " ICOM_SNAME "n for writing"                 },
    { UNIT_ICOM_WPROTECT,  UNIT_ICOM_WPROTECT,  "WRTPROT",    "WRTPROT",  NULL, NULL, NULL,
        "Protects " ICOM_SNAME "n from writing"                },
    { 0 }
};

/* Debug flags */
#define VERBOSE_MSG         (1 << 0)
#define ERROR_MSG           (1 << 1)
#define RBUF_MSG            (1 << 2)
#define WBUF_MSG            (1 << 3)
#define CMD_MSG             (1 << 4)
#define RD_DATA_MSG         (1 << 5)
#define WR_DATA_MSG         (1 << 6)
#define STATUS_MSG          (1 << 7)
#define RD_DATA_DETAIL_MSG  (1 << 8)
#define WR_DATA_DETAIL_MSG  (1 << 9)

/* Debug Flags */
static DEBTAB icom_dt[] = {
    { "VERBOSE",    VERBOSE_MSG,        "Verbose messages"      },
    { "ERROR",      ERROR_MSG,          "Error messages"        },
    { "CMD",        CMD_MSG,            "Command messages"      },
    { "RBUF",       RBUF_MSG,           "Read Buffer messages"  },
    { "WBUF",       WBUF_MSG,           "Write Buffer messages" },
    { "READ",       RD_DATA_MSG,        "Read messages"         },
    { "WRITE",      WR_DATA_MSG,        "Write messages"        },
    { "STATUS",     STATUS_MSG,         "Status messages"       },
    { "RDDETAIL",   RD_DATA_DETAIL_MSG, "Read detail messages"  },
    { "WRDETAIL",   WR_DATA_DETAIL_MSG, "Write detail messags"  },
    { NULL,         0                                           }
};

DEVICE icom_dev = {
    ICOM_SNAME,                        /* name */
    icom_unit,                         /* unit */
    icom_reg,                          /* registers */
    icom_mod,                          /* modifiers */
    ICOM_MAX_DRIVES,                   /* # units */
    10,                                   /* address radix */
    31,                                   /* address width */
    1,                                    /* addr increment */
    ICOM_MAX_DRIVES,                   /* data radix */
    ICOM_MAX_DRIVES,                   /* data width */
    NULL,                                 /* examine routine */
    NULL,                                 /* deposit routine */
    &icom_reset,                       /* reset routine */
    &icom_boot,                        /* boot routine */
    &icom_attach,                      /* attach routine */
    &icom_detach,                      /* detach routine */
    &icom_info_data,                   /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG),  /* flags */
    ERROR_MSG,                            /* debug control */
    icom_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &icom_description                  /* description */
};

/* Reset routine */
static t_stat icom_reset(DEVICE *dptr)
{
    uint8 i;

    if (dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(icom_info->prom_base, icom_info->prom_size, RESOURCE_TYPE_MEMORY, &icomprom, "icomprom", TRUE);
        sim_map_resource(icom_info->mem_base, icom_info->mem_size, RESOURCE_TYPE_MEMORY, &icommem, "icommem", TRUE);
        sim_map_resource(icom_info->io_base, icom_info->io_size, RESOURCE_TYPE_IO, &icomdev, "icomdev", TRUE);
    } else {
        if (sim_map_resource(icom_info->prom_base, icom_info->prom_size, RESOURCE_TYPE_MEMORY, &icomprom, "icomprom", FALSE) != 0) {
            sim_debug(ERROR_MSG, &icom_dev, "Error mapping PROM resource at 0x%04x\n", icom_info->prom_base);
            return SCPE_ARG;
        }
        if (sim_map_resource(icom_info->mem_base, icom_info->mem_size, RESOURCE_TYPE_MEMORY, &icommem, "icommem", FALSE) != 0) {
            sim_debug(ERROR_MSG, &icom_dev, "Error mapping MEM resource at 0x%04x\n", icom_info->mem_base);
            return SCPE_ARG;
        }
        /* Connect I/O Ports at base address */
        if (sim_map_resource(icom_info->io_base, icom_info->io_size, RESOURCE_TYPE_IO, &icomdev, "icomdev", FALSE) != 0) {
            sim_debug(ERROR_MSG, &icom_dev, "Error mapping I/O resource at 0x%02x\n", icom_info->io_base);
            return SCPE_ARG;
        }
    }

    icom_info->currentDrive = 0;

    icom_info->ICOM.track = 0;
    icom_info->ICOM.sector = 1;
    icom_info->ICOM.command = 0;
    icom_info->ICOM.status = 0;
    icom_info->ICOM.rData = 0;
    icom_info->ICOM.wData = 0;
    icom_info->ICOM.rDataBuf = 0;
    icom_info->ICOM.wDataBuf = 0;
    icom_info->ICOM.bytesPerSec = ICOM_SD_SECTOR_LEN;
    icom_info->ICOM.formatMode = 0;

    /* Reset Registers and Interface Controls */
    for (i=0; i < ICOM_MAX_DRIVES; i++) {
        if (icom_info->uptr[i] == NULL) {
            icom_info->uptr[i] = &icom_dev.units[i];
        }

        icom_info->currentTrack[i] = 0;
    }

    sim_debug(STATUS_MSG, &icom_dev, "reset controller.\n");

    return SCPE_OK;
}

/* Service routine */
static t_stat icom_svc(UNIT *uptr)
{
//    if (icom_info->msTime != sim_os_msec()) {
        icom_info->ICOM.status &= ~ICOM_STAT_BUSY;
//    }
//    else {
//        sim_activate_after_abs(icom_info->uptr[icom_info->currentDrive], 1000);  /* Try another 1ms */
//    }

    return SCPE_OK;
}

/* Attach routine */
static t_stat icom_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if (r != SCPE_OK) {              /* error?       */
        sim_debug(ERROR_MSG, &icom_dev, "ATTACH error=%d\n", r);
        return r;
    }

    /* Determine length of this disk */
    if (sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = ICOM_SD_CAPACITY;
    }

    for (i = 0; i < ICOM_MAX_DRIVES; i++) {
        if (icom_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= ICOM_MAX_DRIVES) {
        icom_detach(uptr);

        return SCPE_ARG;
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    sim_debug(VERBOSE_MSG, uptr->dptr, "unit %d, attached to '%s' size=%d interface=%s\n", 
        i, cptr, uptr->capac, (icom_info->boardType == ICOM_TYPE_3712) ? "FD3712" : "FD3812");

    return SCPE_OK;
}


/* Detach routine */
static t_stat icom_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < ICOM_MAX_DRIVES; i++) {
        if (icom_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= ICOM_MAX_DRIVES) {
        return SCPE_ARG;
    }

    DBG_PRINT(("Detach ICOM%d\n", i));

    r = detach_unit(uptr);  /* detach unit */

    if (r != SCPE_OK) {
        return r;
    }

    icom_dev.units[i].fileref = NULL;

    sim_debug(VERBOSE_MSG, uptr->dptr, "unit %d detached.\n", i);

    return SCPE_OK;
}

/*
** If membase is 0, remove from system
*/
static t_stat icom_set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 newba;
    t_stat r;

    if (cptr == NULL) {
        sim_debug(ERROR_MSG, &icom_dev, "cptr=NULL\n");
        return SCPE_ARG;
    }

    newba = get_uint(cptr, 16, 0xFFFF, &r);

    if (r != SCPE_OK) {
        sim_debug(ERROR_MSG, &icom_dev, "get_uint=%d\n", r);
        return r;
    }

    if (newba) {
    r = set_membase(uptr, val, cptr, desc);
        if (r) {
            sim_debug(ERROR_MSG, &icom_dev, "Error setting MEM resource at 0x%04x\n", icom_info->mem_base);
            icom_info->mem_base = 0;
        }
        else {
            sim_debug(VERBOSE_MSG, &icom_dev, "memory now at 0x%04x\n", icom_info->mem_base);
            icom_info->mem_base = newba;
        }
    }
    else if (icom_info->mem_base) {
        sim_map_resource(icom_info->mem_base, icom_info->mem_size, RESOURCE_TYPE_MEMORY, &icommem, "icommem", TRUE);
        icom_info->mem_base = 0;
        sim_debug(VERBOSE_MSG, &icom_dev, "disabled memory at 0x%04x\n", icom_info->mem_base);
    }

    return r;
}

/* Show Base Address routine */
t_stat icom_show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (icom_info->mem_base) {
        fprintf(st, "MEM=0x%04X-0x%04X", icom_info->mem_base, icom_info->mem_base+icom_info->mem_size-1);
    }

    if (icom_info->promEnabled) {
        if (icom_info->mem_base) {
            fprintf(st, ", ");
        }
        fprintf(st, "PROM=0x%04X-0x%04X", icom_info->prom_base, icom_info->prom_base+icom_info->prom_size-1);
    }

    return SCPE_OK;
}

static t_stat icom_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;

    if (!strcmp(cptr, "3812")) {
        icom_info->boardType = ICOM_TYPE_3812;
        icom_info->ICOM.status |= ICOM_STAT_MEDIASTAT;
        icom_prom = icom_3812_prom;
    } else if (!strcmp(cptr, "3712")) {
        icom_info->boardType = ICOM_TYPE_3712;
        icom_info->ICOM.status &= ~ICOM_STAT_MEDIASTAT;
        icom_info->ICOM.bytesPerSec = ICOM_SD_SECTOR_LEN;
        icom_prom = icom_3712_prom;
    } else {
        return SCPE_ARG;
    }

    return SCPE_OK;
}

static t_stat icom_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "TYPE=%s", (icom_info->boardType == ICOM_TYPE_3812) ? "3812" : "3712");

    return SCPE_OK;
}

static t_stat icom_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "ENABLE", strlen(cptr))) {
        if (sim_map_resource(icom_info->prom_base, icom_info->prom_size, RESOURCE_TYPE_MEMORY, &icomprom, "icomprom", FALSE) != 0) {
            sim_debug(ERROR_MSG, &icom_dev, "Error mapping MEM resource at 0x%04x\n", icom_info->prom_base);
            return SCPE_ARG;
        }
        icom_info->promEnabled = TRUE;
    } else if (!strncmp(cptr, "DISABLE", strlen(cptr))) {
        sim_map_resource(icom_info->prom_base, icom_info->prom_size, RESOURCE_TYPE_MEMORY, &icomprom, "icomprom", TRUE);
        icom_info->promEnabled = FALSE;
    } else {
        return SCPE_ARG;
    }

    return SCPE_OK;
}

static t_stat icom_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s", (icom_info->promEnabled) ? "PROM" : "NOPROM");

    return SCPE_OK;
}

static t_stat icom_boot(int32 unitno, DEVICE *dptr)
{
    sim_debug(VERBOSE_MSG, dptr, "Booting using PROM at 0x%04x\n", icom_info->prom_base);

    *((int32 *) sim_PC->loc) = icom_info->prom_base;

    return SCPE_OK;
}

static void icom_set_busy(uint32 msec)
{
    icom_info->ICOM.status |= ICOM_STAT_BUSY;

    if (!msec) {
        msec = 1;
    }

    icom_info->msTime = sim_os_msec();

    sim_activate_after_abs(icom_info->uptr[icom_info->currentDrive], msec * 1000);  /* activate timer */
}

static int32 icomdev(int32 Addr, int32 rw, int32 data)
{
    if (rw == 0) {
        return(ICOM_Read(Addr));
    } else {
        return(ICOM_Write(Addr, data));
    }
}

static void showReadSec()
{
    int i;
    ICOM_REG *pICOM;

    pICOM = &icom_info->ICOM;

    sim_debug(RD_DATA_DETAIL_MSG, &icom_dev, "rdata unit %d track/sector %02d/%02d:\n", icom_info->currentDrive, pICOM->track, pICOM->sector);

    for (i=0; i < pICOM->bytesPerSec; i++) {
        if (((i) & 0xf) == 0) {
            sim_debug(RD_DATA_DETAIL_MSG, &icom_dev, "\t");
        }
        sim_debug(RD_DATA_DETAIL_MSG, &icom_dev, "%02X ", rdata[i]);
        if (((i+1) & 0xf) == 0) {
            sim_debug(RD_DATA_DETAIL_MSG, &icom_dev, "\n");
        }
    }
}

static void showWriteSec()
{
    int i;
    ICOM_REG *pICOM;

    pICOM = &icom_info->ICOM;

    sim_debug(WR_DATA_DETAIL_MSG, &icom_dev, "wdata unit %d track/sector %02d/%02d:\n", icom_info->currentDrive, pICOM->track, pICOM->sector);

    for (i=0; i < pICOM->bytesPerSec; i++) {
        if (((i) & 0xf) == 0) {
            sim_debug(WR_DATA_DETAIL_MSG, &icom_dev, "\t");
        }
        sim_debug(WR_DATA_DETAIL_MSG, &icom_dev, "%02X ", wdata[i]);
        if (((i+1) & 0xf) == 0) {
            sim_debug(WR_DATA_DETAIL_MSG, &icom_dev, "\n");
        }
    }
}

static uint32 calculate_icom_sec_offset(ICOM_REG *pICOM, uint8 track, uint8 sector)
{
    uint32 offset;
    uint16 bps;

    bps = pICOM->bytesPerSec;

    /*
    ** Calculate track offset
    */
    if (track==0) {
        offset=0;
    }
    else {
        offset=ICOM_SPT * ICOM_SD_SECTOR_LEN; /* Track 0 always SD */
        offset+=(track-1) * ICOM_SPT * bps; /* Track 1-77 SD or DD */
    }

    /*
    ** Add sector offset to track offset
    */
    offset += (sector-1) * bps;

    DBG_PRINT(("ICOM: offset calc drive=%d bps=%d track=%d sector=%d offset=%04x\n", icom_info->currentDrive, bps, track, sector, offset));

    return (offset);
}

static uint8 ICOM_Read(uint32 Addr)
{
    uint8 cData;
    uint8 driveNum;
    ICOM_REG *pICOM;
    UNIT *uptr;

    cData = 0;
    driveNum = icom_info->currentDrive;
    uptr = icom_info->uptr[driveNum];
    pICOM = &icom_info->ICOM;

    switch(Addr & 0x01) {
        case ICOM_REG_DATAI:
            if (pICOM->command & ICOM_CMD_EXREADBUF) {
                pICOM->rData = rdata[pICOM->rDataBuf];
                sim_debug(RBUF_MSG, &icom_dev, "read buffer[%d]=%02x\n", pICOM->rDataBuf, pICOM->rData);
                if (icom_info->boardType == ICOM_TYPE_3812) {
                    ICOM_Command(uptr, pICOM, ICOM_CMD_SHREADBUF);
                }
                cData = pICOM->rData;
            }
            else {
                cData = pICOM->status;
            }

            break;

        default:
            sim_debug(ERROR_MSG, &icom_dev, "READ Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x01);
            cData = 0xff;
            break;
    }

    return (cData);
}

static uint8 ICOM_Write(uint32 Addr, int32 Data)
{
    uint8 cData;
    uint8 driveNum;
    UNIT *uptr;
    ICOM_REG *pICOM;

    cData = 0;
    driveNum = icom_info->currentDrive;
    uptr = icom_info->uptr[driveNum];
    pICOM = &icom_info->ICOM;

    switch(Addr & 0x01) {
        case ICOM_REG_COMMAND:
            cData = ICOM_Command(uptr, pICOM, Data);
            break;

        case ICOM_REG_DATAO:
            pICOM->wData = Data;

            if (pICOM->command == ICOM_CMD_LDWRITEBUFNOP && icom_info->boardType == ICOM_TYPE_3812) {
                ICOM_Command(uptr, pICOM, ICOM_CMD_LDWRITEBUF);
            }
            break;

        default:
            sim_debug(ERROR_MSG, &icom_dev, "WRITE Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x01);
            cData = 0xff;
            break;
    }

    return(cData);
}

static uint32 ICOM_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;
    ICOM_REG *pICOM;

    pICOM = &icom_info->ICOM;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &icom_dev, "uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_icom_sec_offset(pICOM, track, sector);

    sim_debug(RD_DATA_MSG, &icom_dev, "track %d sector %d at offset %04X\n", track, sector, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &icom_dev, "sim_fseek error.\n");
        return 0;
    }

    rtn = sim_fread(buffer, 1, pICOM->bytesPerSec, uptr->fileref);

    sim_debug(RD_DATA_MSG, &icom_dev, "read %d bytes at offset %04X\n", rtn, sec_offset);

    return rtn;
}


static uint32 ICOM_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;
    ICOM_REG *pICOM;

    pICOM = &icom_info->ICOM;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &icom_dev, "uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_icom_sec_offset(pICOM, track, sector);

    sim_debug(WR_DATA_MSG, &icom_dev, "track %d sector %d bytes %d at offset %04X\n", track, sector, pICOM->bytesPerSec, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &icom_dev, "sim_fseek error.\n");
        return 0;
    }

    rtn = sim_fwrite(buffer, 1, pICOM->bytesPerSec, uptr->fileref);

    sim_debug(WR_DATA_MSG, &icom_dev, "wrote %d bytes at offset %04X\n", rtn, sec_offset);

    return rtn;
}

static uint32 ICOM_FormatTrack(UNIT *uptr, uint8 track, uint8 *buffer)
{
    uint8 sector;
    uint32 rtn;

    for (sector = 1; sector <= ICOM_SPT; sector++) {
        rtn = ICOM_WriteSector(uptr, track, sector, buffer);
        sim_debug(WR_DATA_MSG, &icom_dev, "FORMAT track %d sector %d\n", track, sector);
    }

    return rtn;
}

static uint8 ICOM_DriveNotReady(UNIT *uptr, ICOM_REG *pICOM)
{
    pICOM->status &= ~ICOM_STAT_DRVFAIL;

    if ((uptr == NULL) || (uptr->fileref == NULL)) {
        pICOM->status |= ICOM_STAT_DRVFAIL;
        sim_debug(STATUS_MSG, &icom_dev, "Drive: %d not attached.\n", icom_info->currentDrive);
    }

    return (pICOM->status & ICOM_STAT_DRVFAIL);
}

static const char * ICOM_CommandString(uint8 command)
{
    switch (command) {
        case ICOM_CMD_STATUS:
            return "STATUS";

        case ICOM_CMD_READ:
            return "READ";

        case ICOM_CMD_WRITE:
            return "WRITE";

        case ICOM_CMD_READCRC:
            return "READ CRC";

        case ICOM_CMD_SEEK:
            return "SEEK";

        case ICOM_CMD_CLRERRFLGS:
            return "CLR ERR FLAGS";

        case ICOM_CMD_TRACK0:
            return "TRACK 0";

        case ICOM_CMD_WRITEDDM:
            return "WRITE DDM";

        case ICOM_CMD_LDTRACK:
            return "LD TRACK";

        case ICOM_CMD_LDUNITSEC:
            return "LD UNIT/SEC";

        case ICOM_CMD_LDWRITEBUFNOP:
            return "LD WR BUF NOP";

        case ICOM_CMD_LDWRITEBUF:
            return "LD WR BUF";

        case ICOM_CMD_EXREADBUF:
            return "EX RD BUF";

        case ICOM_CMD_SHREADBUF:
            return "SHFT RD BUF";

        case ICOM_CMD_CLEAR:
            return "CLEAR";

        case ICOM_CMD_LDCONF:
            return "LD CONFIG";

        default:
            break;
    }

    return "UNRECOGNIZED COMMAND";
}

static uint8 ICOM_Command(UNIT *uptr, ICOM_REG *pICOM, int32 Data)
{
    uint8 cData;
    uint8 newTrack;
    uint8 drive;
    int32 rtn;

    cData = 0;

    if (uptr == NULL) {
        return cData;
    }

    pICOM->command = Data;

    drive = icom_info->currentDrive;

    switch(pICOM->command) {
        case ICOM_CMD_STATUS:
            pICOM->rData = pICOM->status;
            break;

        case ICOM_CMD_READ:
            if (ICOM_DriveNotReady(uptr, pICOM)) {
                break;
            }

            rtn = ICOM_ReadSector(uptr, pICOM->track, pICOM->sector, rdata);

            if (rtn == pICOM->bytesPerSec) {
                showReadSec();
                icom_set_busy(icom_info->rwsMs);
            }
            else {
                sim_debug(ERROR_MSG, &icom_dev, "sim_fread errno=%d\n", errno);

                pICOM->status |= ICOM_STAT_DRVFAIL;
            }

            pICOM->rDataBuf = 0;    // Reset read buffer address

            break;

        case ICOM_CMD_WRITEDDM:
            sim_debug(VERBOSE_MSG, &icom_dev, "DDM writes not supported. Performing standard write.\n");

            /* fall into ICOM_CMD_WRITE */

        case ICOM_CMD_WRITE:
            if (ICOM_DriveNotReady(uptr, pICOM)) {
                break;
            }

            if (uptr->flags & UNIT_ICOM_WPROTECT) {
                sim_debug(ERROR_MSG, &icom_dev, "Disk '%s' write protected.\n", uptr->filename);
                break;
            }

            /*
            ** If format mode, format entire track with wdata
            */
            if (pICOM->formatMode) {
                rtn = ICOM_FormatTrack(uptr, pICOM->track, wdata);
            }
            else {
                rtn = ICOM_WriteSector(uptr, pICOM->track, pICOM->sector, wdata);
            }

            if (rtn == pICOM->bytesPerSec) {
                showWriteSec();
                icom_set_busy(icom_info->rwsMs);
            }
            else {
                sim_debug(ERROR_MSG, &icom_dev, "sim_fwrite errno=%d\n", errno);

                pICOM->status |= ICOM_STAT_DRVFAIL;
            }

            pICOM->wDataBuf = 0;    // Reset write buffer address
            break;

        case ICOM_CMD_READCRC:
            if (ICOM_DriveNotReady(uptr, pICOM)) {
                break;
            }
            icom_set_busy(icom_info->rwsMs);
            break;

        case ICOM_CMD_SEEK:
            if (ICOM_DriveNotReady(uptr, pICOM)) {
                break;
            }

            icom_set_busy(icom_info->seekMs * abs((int8) pICOM->track - (int8) icom_info->currentTrack[icom_info->currentDrive]));
            icom_info->currentTrack[icom_info->currentDrive] = pICOM->track;

            break;

        case ICOM_CMD_CLRERRFLGS:
            pICOM->status &= ~ICOM_STAT_BUSY;
            pICOM->status &= ~ICOM_STAT_DDM;
            break;

        case ICOM_CMD_TRACK0:
            if (ICOM_DriveNotReady(uptr, pICOM)) {
                break;
            }

            pICOM->track = 0;
            icom_set_busy(icom_info->seekMs * abs((int8) pICOM->track - (int8) icom_info->currentTrack[icom_info->currentDrive]));
            icom_info->currentTrack[icom_info->currentDrive] = 0;

            break;

        case ICOM_CMD_LDTRACK:
            newTrack = pICOM->wData;

            if (newTrack < ICOM_TRACKS) {
                pICOM->track = newTrack;
            }

            break;

        case ICOM_CMD_LDUNITSEC:
            pICOM->sector = pICOM->wData & 0x1f;
            icom_info->currentDrive = pICOM->wData >> 6;
            pICOM->status &= ~ICOM_STAT_UNITMSK;
            pICOM->status |= icom_info->currentDrive << 1;
            break;

        case ICOM_CMD_LDWRITEBUFNOP:
            sim_debug(WBUF_MSG, &icom_dev, "LOAD WRITE BUF NOP index=%04x\n", pICOM->wDataBuf);
            break;

        case ICOM_CMD_LDWRITEBUF:
            sim_debug(WBUF_MSG, &icom_dev, "LOAD WRITE BUF %d=%02x\n", pICOM->wDataBuf, pICOM->wData);
            wdata[pICOM->wDataBuf] = pICOM->wData;
            pICOM->wDataBuf++;
            pICOM->wDataBuf &= DATA_MASK;
            break;

        case ICOM_CMD_EXREADBUF:
            sim_debug(RBUF_MSG, &icom_dev, "EXAMINE READ BUF index=%04x\n", pICOM->rDataBuf);
            break;

        case ICOM_CMD_SHREADBUF:
            pICOM->rDataBuf++;
            pICOM->rDataBuf &= DATA_MASK;
            sim_debug(RBUF_MSG, &icom_dev, "SHIFT READ BUF index=%04x\n", pICOM->rDataBuf);
            break;

        case ICOM_CMD_CLEAR:
            pICOM->status &= ~ICOM_STAT_BUSY;
            pICOM->status &= ~ICOM_STAT_DRVFAIL;
            pICOM->status &= ~ICOM_STAT_CRC;
            pICOM->status &= ~ICOM_STAT_DDM;
            pICOM->rDataBuf = 0;
            pICOM->wDataBuf = 0;
            break;

        case ICOM_CMD_LDCONF:
            pICOM->formatMode = (pICOM->wData & ICOM_CONF_FM);
            pICOM->bytesPerSec = (pICOM->wData & ICOM_CONF_DD) ? ICOM_DD_SECTOR_LEN : ICOM_SD_SECTOR_LEN;
            break;

        default:
            cData=0xFF;
            break;
    }

    /* Set WRITE PROTECT bit */
    pICOM->status &= ~ICOM_STAT_WRITEPROT;
    pICOM->status |= (uptr->flags & UNIT_ICOM_WPROTECT) ? ICOM_STAT_WRITEPROT : 0;

    /* Set data register to status if command bit 6 is 0 */
    if (!(pICOM->command & 0x40)) {
        pICOM->rData = pICOM->status;
    }

    /* Clear command bit 0 */
    pICOM->command &= ~ICOM_CMD_CMDMSK;

    sim_debug(CMD_MSG, &icom_dev,
            "%-13.13s (%02Xh) unit=%d trk=%02d sec=%02d stat=%02Xh density=%d formatMode=%s\n",
            ICOM_CommandString(Data),
            Data, icom_info->currentDrive,
            pICOM->track, pICOM->sector, pICOM->status,
            pICOM->bytesPerSec, (pICOM->formatMode) ? "TRUE" : "FALSE");

    return(cData);
}

static int32 icomprom(int32 Addr, int32 rw, int32 Data)
{
    /*
    ** The iCOM controller PROM occupies 1024 bytes (1K) of RAM at
    ** location F000H.
    */
    if (icom_info->promEnabled == TRUE) {
        return(icom_prom[Addr & ICOM_PROM_MASK]);
    }

    return 0xff;
}

static int32 icommem(int32 Addr, int32 rw, int32 Data)
{
    if (rw) {
       icom_mem[Addr & ICOM_MEM_MASK] = Data & 0xff;
    }

    return(icom_mem[Addr & ICOM_MEM_MASK]);
}

