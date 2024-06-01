/*** s100_jair.c: Josh's Altair / IMASI Replacement CPU & SBC

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

    Devices:

    JAIR   - ROM and SD Card
    JAIRS0 - COM1
    JAIRS1 - COM2
    JAIRP - Printer Port

    The serial and printer ports support TMXR which allow these
    ports to be attached to real serial ports and sockets. If no TMXR
    interfaces are attached, JAIRS0 will use the SIMH console for
    both input and output, JAIRS1 and JAIRP will use the SIMH console
    for output.
*/

#include "altairz80_defs.h"
#include "sim_imd.h"
#include "sim_tmxr.h"

/********/
/* SIMH */
/********/

extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern t_stat set_dev_enbdis(DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
extern t_stat set_cmd(int32 flag, CONST char *cptr);
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint32 nmiInterrupt;

/* Debug flags */
#define VERBOSE_MSG         (1 << 0)
#define ERROR_MSG           (1 << 1)
#define STATUS_MSG          (1 << 2)

/* Debug Flags */
static DEBTAB jair_dt[] = {
    { "VERBOSE",    VERBOSE_MSG,        "Verbose messages" },
    { "ERROR",      ERROR_MSG,          "Error messages"   },
    { "STATUS",     STATUS_MSG,         "Status messages"  },
    { NULL,         0                                      }
};

/*****************************/
/* Local function prototypes */
/*****************************/

static t_stat jair_reset(DEVICE *dptr);
static t_stat jair_port_reset(DEVICE *dptr);
static t_stat jair_svc(UNIT *uptr);
static t_stat jair_rx_svc(UNIT *uptr);
static t_stat jair_tx_svc(UNIT *uptr);
static t_stat jair_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat jair_boot(int32 unitno, DEVICE *dptr);
static t_stat jair_attach(UNIT *uptr, CONST char *cptr);
static t_stat jair_detach(UNIT *uptr);
static t_stat jair_attach_mux(UNIT *uptr, CONST char *cptr);
static t_stat jair_detach_mux(UNIT *uptr);
static const char* jair_description(DEVICE *dptr);
static t_stat jair_config_line(DEVICE *dev, TMLN *tmln, int baud);
static const char* jairs0_description(DEVICE *dptr);
static const char* jairs1_description(DEVICE *dptr);
static const char* jairp_description(DEVICE *dptr);
static int jair_get_modem_status(UNIT *uptr);
static void jair_get_rxdata(UNIT *uptr);
static int jair_set_mc(TMLN *tmln, uint8 data);
static int jair_new_baud(UNIT *uptr);
static t_stat jair_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat jair_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc);
static t_stat jair_show_ports(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static int32 jairio(int32 addr, int32 rw, int32 data);
static uint8 jair_io_in(uint32 addr);
static uint8 jair_io_out(uint32 addr, int32 data);
static t_stat jair_set_rom(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat jair_set_norom(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static int32 jair_shadow_rom(int32 Addr, int32 rw, int32 data);

/***********/
/* RAM/ROM */
/***********/

#define JAIR_ROM_BASE   0x0000
#define JAIR_ROM_SIZE   8192
#define JAIR_ROM_MASK   (JAIR_ROM_SIZE-1)

#define JAIR_ROM_READ   FALSE
#define JAIR_ROM_WRITE  TRUE

static uint8 jair_rom_v25[JAIR_ROM_SIZE] = {
    0x3e, 0x02, 0x21, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x09, 0xd2, 0x08, 0x00, 0x3d, 0xc2, 0x02, 0x00,
    0x3e, 0x80, 0xd3, 0x23, 0xd3, 0x2b, 0x3e, 0x0c,
    0xd3, 0x20, 0xd3, 0x28, 0x3e, 0x00, 0xd3, 0x21,
    0xd3, 0x29, 0x3e, 0x03, 0xd3, 0x23, 0xd3, 0x2b,
    0xd3, 0x24, 0xd3, 0x2c, 0xdb, 0x20, 0x21, 0x34,
    0x00, 0xc3, 0xa4, 0x04, 0x0d, 0x0a, 0x41, 0x4c,
    0x54, 0x41, 0x49, 0x52, 0x2f, 0x49, 0x4d, 0x53,
    0x41, 0x49, 0x20, 0x38, 0x30, 0x38, 0x30, 0x20,
    0x43, 0x50, 0x55, 0x20, 0x42, 0x4f, 0x41, 0x52,
    0x44, 0x20, 0x42, 0x4f, 0x4f, 0x54, 0x20, 0x4c,
    0x4f, 0x41, 0x44, 0x45, 0x52, 0x20, 0x2d, 0x20,
    0x4a, 0x6f, 0x73, 0x68, 0x20, 0x42, 0x65, 0x6e,
    0x73, 0x61, 0x64, 0x6f, 0x6e, 0x20, 0x76, 0x32,
    0x2e, 0x35, 0x20, 0x53, 0x65, 0x70, 0x20, 0x33,
    0x2c, 0x20, 0x32, 0x30, 0x31, 0x38, 0x0d, 0x0a,
    0x3c, 0x44, 0x3e, 0x20, 0x2d, 0x53, 0x44, 0x20,
    0x43, 0x61, 0x72, 0x64, 0x20, 0x44, 0x69, 0x72,
    0x65, 0x63, 0x74, 0x6f, 0x72, 0x79, 0x0d, 0x0a,
    0x3c, 0x52, 0x3e, 0x20, 0x2d, 0x52, 0x41, 0x4d,
    0x20, 0x54, 0x65, 0x73, 0x74, 0x0d, 0x0a, 0x3c,
    0x56, 0x3e, 0x20, 0x2d, 0x56, 0x69, 0x65, 0x77,
    0x20, 0x4c, 0x6f, 0x61, 0x64, 0x0d, 0x0a, 0x3e,
    0x20, 0x00, 0xaf, 0x32, 0x24, 0xfd, 0x01, 0x03,
    0x00, 0x1e, 0x05, 0x21, 0x00, 0x00, 0xdb, 0x25,
    0xe6, 0x01, 0xca, 0xd2, 0x00, 0xdb, 0x20, 0xc3,
    0xe4, 0x00, 0xdb, 0x00, 0xe6, 0x02, 0xca, 0x03,
    0x01, 0xdb, 0x01, 0xb7, 0xca, 0x03, 0x01, 0xfe,
    0xff, 0xca, 0x03, 0x01, 0xd3, 0x20, 0xd3, 0x01,
    0xfe, 0x1b, 0xca, 0xb6, 0x04, 0xfe, 0x20, 0xca,
    0x07, 0x01, 0xe6, 0x5f, 0xfe, 0x44, 0xca, 0x1c,
    0x01, 0xfe, 0x52, 0xca, 0xcc, 0x01, 0xfe, 0x56,
    0xca, 0x14, 0x01, 0x09, 0xd2, 0xc6, 0x00, 0x3e,
    0x2e, 0xd3, 0x20, 0xd3, 0x01, 0x1d, 0xc2, 0xc3,
    0x00, 0xc3, 0xb6, 0x04, 0x3e, 0x01, 0x32, 0x24,
    0xfd, 0xc3, 0xc1, 0x00, 0x31, 0x00, 0xfd, 0xcd,
    0x90, 0x07, 0x0d, 0x0a, 0x49, 0x4e, 0x49, 0x54,
    0x5f, 0x46, 0x41, 0x54, 0x20, 0x00, 0xcd, 0xd8,
    0x09, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x44, 0x49,
    0x52, 0x45, 0x43, 0x54, 0x4f, 0x52, 0x59, 0x3a,
    0x0d, 0x0a, 0x00, 0xcd, 0xe9, 0x07, 0xca, 0x87,
    0x01, 0x7e, 0xfe, 0x21, 0xfa, 0x81, 0x01, 0xfe,
    0x7f, 0xf2, 0x81, 0x01, 0xe5, 0x01, 0x1a, 0x00,
    0x09, 0x7e, 0x23, 0xb6, 0xe1, 0xca, 0x81, 0x01,
    0xcd, 0xaf, 0x0d, 0x3a, 0x35, 0xfd, 0xfe, 0x40,
    0xfa, 0x71, 0x01, 0xcd, 0x90, 0x07, 0x0d, 0x0a,
    0x00, 0x3a, 0x35, 0xfd, 0xe6, 0x0f, 0xca, 0x81,
    0x01, 0x3e, 0x20, 0xcd, 0xc3, 0x07, 0xc3, 0x71,
    0x01, 0xcd, 0xff, 0x07, 0xc3, 0x46, 0x01, 0xcd,
    0x90, 0x07, 0x0d, 0x0a, 0x45, 0x4e, 0x54, 0x45,
    0x52, 0x20, 0x38, 0x2e, 0x33, 0x20, 0x46, 0x49,
    0x4c, 0x45, 0x20, 0x4e, 0x41, 0x4d, 0x45, 0x3e,
    0x20, 0x00, 0x21, 0x6d, 0xfd, 0x06, 0x0b, 0x3e,
    0x20, 0xcd, 0x51, 0x0e, 0x0e, 0x2e, 0x06, 0x08,
    0xcd, 0x89, 0x0d, 0xda, 0x87, 0x01, 0xfe, 0x0d,
    0xca, 0xc3, 0x01, 0x21, 0x75, 0xfd, 0x06, 0x03,
    0xcd, 0x89, 0x0d, 0xcd, 0x90, 0x07, 0x0d, 0x0a,
    0x00, 0xc3, 0x0d, 0x05, 0x21, 0x00, 0x00, 0x01,
    0x01, 0x00, 0x09, 0xd2, 0xd2, 0x01, 0x21, 0xdc,
    0x01, 0xc3, 0xa4, 0x04, 0x0d, 0x0a, 0x54, 0x65,
    0x73, 0x74, 0x69, 0x6e, 0x67, 0x20, 0x53, 0x59,
    0x53, 0x54, 0x45, 0x4d, 0x20, 0x52, 0x41, 0x4d,
    0x20, 0x40, 0x46, 0x30, 0x30, 0x30, 0x2d, 0x46,
    0x46, 0x46, 0x46, 0x0d, 0x0a, 0x52, 0x41, 0x4d,
    0x20, 0x50, 0x41, 0x47, 0x45, 0x20, 0x4d, 0x41,
    0x52, 0x43, 0x48, 0x00, 0x1e, 0xff, 0x21, 0x00,
    0xf0, 0x7b, 0x2f, 0x77, 0x2c, 0xc2, 0x13, 0x02,
    0x7c, 0x24, 0xfe, 0xff, 0xc2, 0x11, 0x02, 0x16,
    0xf0, 0x62, 0x3e, 0x2e, 0xd3, 0x20, 0xd3, 0x01,
    0x7a, 0x2f, 0xd3, 0xff, 0x73, 0x2c, 0xc2, 0x2c,
    0x02, 0x21, 0x00, 0xf0, 0x7c, 0xba, 0x7b, 0xca,
    0x3b, 0x02, 0x2f, 0xbe, 0xc2, 0x26, 0x04, 0x2c,
    0xc2, 0x3b, 0x02, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0x34, 0x02, 0x62, 0x7b, 0x2f, 0x77, 0x2c, 0xc2,
    0x4d, 0x02, 0x7a, 0x14, 0xfe, 0xff, 0xc2, 0x21,
    0x02, 0x1c, 0xca, 0x0e, 0x02, 0x21, 0x63, 0x02,
    0xc3, 0xa4, 0x04, 0x50, 0x41, 0x53, 0x53, 0x45,
    0x44, 0x0d, 0x0a, 0x52, 0x41, 0x4d, 0x20, 0x42,
    0x59, 0x54, 0x45, 0x20, 0x4d, 0x41, 0x52, 0x43,
    0x48, 0x20, 0x41, 0x00, 0x05, 0xc2, 0x7c, 0x02,
    0x1e, 0xff, 0x26, 0xf0, 0x2e, 0x00, 0x7c, 0x2f,
    0xd3, 0xff, 0x3e, 0x2e, 0xd3, 0x20, 0xd3, 0x01,
    0x7b, 0x2f, 0x77, 0x2c, 0xc2, 0x92, 0x02, 0x16,
    0x00, 0x6a, 0x7b, 0x77, 0x2f, 0x2e, 0x00, 0xbe,
    0xca, 0xaf, 0x02, 0x2f, 0xbe, 0xc2, 0x26, 0x04,
    0x7d, 0xba, 0xc2, 0x26, 0x04, 0x7b, 0x2f, 0x2c,
    0xc2, 0x9f, 0x02, 0x6a, 0x7b, 0x2f, 0x77, 0x14,
    0xc2, 0x99, 0x02, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0x84, 0x02, 0x1c, 0xca, 0x82, 0x02, 0x21, 0xcc,
    0x02, 0xc3, 0xa4, 0x04, 0x50, 0x41, 0x53, 0x53,
    0x45, 0x44, 0x0d, 0x0a, 0x52, 0x41, 0x4d, 0x20,
    0x42, 0x59, 0x54, 0x45, 0x20, 0x4d, 0x41, 0x52,
    0x43, 0x48, 0x20, 0x42, 0x00, 0x1e, 0xff, 0x16,
    0x00, 0x21, 0x00, 0xf0, 0x7b, 0x2f, 0x77, 0x2c,
    0xc2, 0xee, 0x02, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0xec, 0x02, 0x7a, 0x2f, 0xd3, 0xff, 0xe6, 0x0f,
    0xc2, 0x09, 0x03, 0x3e, 0x2e, 0xd3, 0x20, 0xd3,
    0x01, 0x26, 0xf0, 0x6a, 0x73, 0x7c, 0x24, 0xfe,
    0xff, 0xc2, 0x0c, 0x03, 0x2e, 0x00, 0x26, 0xf0,
    0x7d, 0xba, 0xca, 0x2d, 0x03, 0x7b, 0x2f, 0xbe,
    0xc2, 0x26, 0x04, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0x1d, 0x03, 0xc3, 0x39, 0x03, 0x7b, 0xbe, 0xc2,
    0x26, 0x04, 0x7c, 0x24, 0xfe, 0xff, 0xc2, 0x2d,
    0x03, 0x2c, 0xc2, 0x16, 0x03, 0x26, 0xf0, 0x6a,
    0x7b, 0x2f, 0x77, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0x40, 0x03, 0x14, 0xc2, 0xfa, 0x02, 0x1c, 0xca,
    0xe7, 0x02, 0x21, 0x58, 0x03, 0xc3, 0xa4, 0x04,
    0x50, 0x41, 0x53, 0x53, 0x45, 0x44, 0x0d, 0x0a,
    0x52, 0x41, 0x4d, 0x20, 0x42, 0x49, 0x54, 0x20,
    0x4d, 0x41, 0x52, 0x43, 0x48, 0x20, 0x00, 0x1e,
    0x01, 0x21, 0x00, 0xf0, 0x7b, 0x2f, 0xd3, 0xff,
    0x3e, 0x2e, 0xd3, 0x20, 0xd3, 0x01, 0x7b, 0x77,
    0x2c, 0xc2, 0x7f, 0x03, 0x7c, 0x24, 0xfe, 0xff,
    0xc2, 0x7e, 0x03, 0x21, 0x00, 0xf0, 0x7b, 0xbe,
    0xc2, 0x26, 0x04, 0x2c, 0xc2, 0x8f, 0x03, 0x7c,
    0x24, 0xfe, 0xff, 0xc2, 0x8e, 0x03, 0x7b, 0x17,
    0x7b, 0x07, 0x5f, 0xfe, 0x01, 0xc2, 0xad, 0x03,
    0x2f, 0x5f, 0xc3, 0x71, 0x03, 0xfe, 0xfe, 0xc2,
    0x71, 0x03, 0x21, 0xb8, 0x03, 0xc3, 0xa4, 0x04,
    0x50, 0x41, 0x53, 0x53, 0x45, 0x44, 0x0d, 0x0a,
    0x52, 0x41, 0x4d, 0x20, 0x53, 0x45, 0x51, 0x55,
    0x45, 0x4e, 0x43, 0x45, 0x20, 0x54, 0x45, 0x53,
    0x54, 0x00, 0x1e, 0x01, 0x7b, 0xe6, 0x07, 0xc2,
    0xe0, 0x03, 0x3e, 0x2e, 0xd3, 0x20, 0xd3, 0x01,
    0x7b, 0x2f, 0xd3, 0xff, 0x21, 0x00, 0xf0, 0x53,
    0x14, 0xc2, 0xed, 0x03, 0x14, 0x72, 0x2c, 0xc2,
    0xe8, 0x03, 0x7c, 0x24, 0xfe, 0xff, 0xc2, 0xe8,
    0x03, 0x21, 0x00, 0xf0, 0x53, 0x14, 0xc2, 0x02,
    0x04, 0x14, 0x7a, 0xbe, 0xc2, 0x26, 0x04, 0x2c,
    0xc2, 0xfd, 0x03, 0x7c, 0x24, 0xfe, 0xff, 0xc2,
    0xfd, 0x03, 0x1c, 0xc2, 0xd4, 0x03, 0x21, 0x1c,
    0x04, 0xc3, 0xa4, 0x04, 0x50, 0x41, 0x53, 0x53,
    0x45, 0x44, 0x00, 0xc3, 0xb6, 0x04, 0x54, 0x5d,
    0x21, 0x2e, 0x04, 0xc3, 0xa4, 0x04, 0x0d, 0x0a,
    0x46, 0x41, 0x49, 0x4c, 0x45, 0x44, 0x20, 0x41,
    0x54, 0x3a, 0x00, 0x7a, 0x0f, 0x0f, 0x0f, 0x0f,
    0xe6, 0x0f, 0xc6, 0x90, 0x27, 0xce, 0x40, 0x27,
    0x05, 0xc2, 0x48, 0x04, 0xd3, 0x20, 0xd3, 0x01,
    0x7a, 0xe6, 0x0f, 0xc6, 0x90, 0x27, 0xce, 0x40,
    0x27, 0x05, 0xc2, 0x59, 0x04, 0xd3, 0x20, 0xd3,
    0x01, 0x7b, 0x0f, 0x0f, 0x0f, 0x0f, 0xe6, 0x0f,
    0xc6, 0x90, 0x27, 0xce, 0x40, 0x27, 0x05, 0xc2,
    0x6e, 0x04, 0xd3, 0x20, 0xd3, 0x01, 0x7b, 0xe6,
    0x0f, 0xc6, 0x90, 0x27, 0xce, 0x40, 0x27, 0x05,
    0xc2, 0x7f, 0x04, 0xd3, 0x20, 0xd3, 0x01, 0x21,
    0x8d, 0x04, 0xc3, 0xa4, 0x04, 0x20, 0x2d, 0x20,
    0x53, 0x59, 0x53, 0x54, 0x45, 0x4d, 0x20, 0x48,
    0x41, 0x4c, 0x54, 0x45, 0x44, 0x0d, 0x0a, 0x00,
    0x76, 0xc3, 0xa0, 0x04, 0x7e, 0x23, 0xb7, 0xc2,
    0xab, 0x04, 0xe9, 0x05, 0xc2, 0xab, 0x04, 0xd3,
    0x20, 0xd3, 0x01, 0xc3, 0xa4, 0x04, 0x31, 0x00,
    0xfd, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x42, 0x4f,
    0x4f, 0x54, 0x20, 0x42, 0x49, 0x4f, 0x53, 0x2e,
    0x48, 0x45, 0x58, 0x2c, 0x20, 0x49, 0x4e, 0x49,
    0x54, 0x5f, 0x46, 0x41, 0x54, 0x20, 0x00, 0xcd,
    0xd8, 0x09, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x4c,
    0x4f, 0x41, 0x44, 0x49, 0x4e, 0x47, 0x20, 0x46,
    0x49, 0x4c, 0x45, 0x20, 0x00, 0x11, 0x01, 0xfd,
    0xcd, 0x3d, 0x07, 0x42, 0x49, 0x4f, 0x53, 0x20,
    0x20, 0x20, 0x20, 0x48, 0x45, 0x58, 0x00, 0x21,
    0x00, 0xfd, 0x36, 0x00, 0x23, 0x11, 0x6d, 0xfd,
    0x06, 0x0b, 0xcd, 0x43, 0x0e, 0xcd, 0x21, 0x08,
    0xca, 0x1c, 0x01, 0xe5, 0x01, 0x1c, 0x00, 0x09,
    0xcd, 0xe5, 0x0d, 0x21, 0x20, 0xfd, 0xcd, 0xed,
    0x0d, 0xe1, 0x01, 0x1a, 0x00, 0x09, 0xcd, 0x4c,
    0x0e, 0xeb, 0x21, 0x00, 0xfd, 0x36, 0x01, 0x21,
    0x0c, 0xfd, 0x73, 0x23, 0x72, 0x23, 0x06, 0x0e,
    0x3e, 0xff, 0xcd, 0x51, 0x0e, 0xcd, 0x90, 0x07,
    0x0d, 0x0a, 0x46, 0x49, 0x4c, 0x45, 0x20, 0x53,
    0x49, 0x5a, 0x45, 0x3d, 0x30, 0x78, 0x00, 0x2a,
    0x22, 0xfd, 0xcd, 0x61, 0x07, 0x2a, 0x20, 0xfd,
    0xcd, 0x61, 0x07, 0xcd, 0x90, 0x07, 0x0d, 0x0a,
    0x00, 0x21, 0x00, 0x00, 0x22, 0x3e, 0xfd, 0x21,
    0xff, 0xff, 0x22, 0x25, 0xfd, 0x3e, 0x00, 0x32,
    0x34, 0xfd, 0x21, 0xc8, 0x05, 0x22, 0x27, 0xfd,
    0x21, 0x20, 0xfd, 0xcd, 0xe5, 0x0d, 0xcd, 0x13,
    0x0e, 0xca, 0xba, 0x05, 0xcd, 0x0b, 0x0e, 0x21,
    0x20, 0xfd, 0xcd, 0xed, 0x0d, 0x2a, 0x25, 0xfd,
    0x23, 0x7c, 0xb5, 0xc2, 0xa3, 0x05, 0xcd, 0x58,
    0x08, 0x2a, 0x3e, 0xfd, 0x23, 0x22, 0x3e, 0xfd,
    0x21, 0x00, 0xfe, 0x22, 0x25, 0xfd, 0x3a, 0x24,
    0xfd, 0xb7, 0xca, 0xb1, 0x05, 0x7e, 0xcd, 0xc3,
    0x07, 0x7e, 0x21, 0x78, 0x05, 0xe5, 0x2a, 0x27,
    0xfd, 0xe9, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x2d,
    0x45, 0x4f, 0x46, 0x2d, 0x00, 0xc3, 0xbe, 0x06,
    0xfe, 0x3a, 0xc0, 0xaf, 0x32, 0x2d, 0xfd, 0x21,
    0x2a, 0x06, 0x22, 0x2a, 0xfd, 0x21, 0xdc, 0x05,
    0x22, 0x27, 0xfd, 0xc9, 0xcd, 0x18, 0x07, 0xda,
    0x08, 0x06, 0x07, 0x07, 0x07, 0x07, 0x32, 0x2c,
    0xfd, 0x21, 0xf0, 0x05, 0x22, 0x27, 0xfd, 0xc9,
    0xcd, 0x18, 0x07, 0xda, 0x08, 0x06, 0x67, 0x3a,
    0x2c, 0xfd, 0xb4, 0x67, 0x3a, 0x2d, 0xfd, 0x84,
    0x32, 0x2d, 0xfd, 0x7c, 0x2a, 0x2a, 0xfd, 0xe9,
    0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x21, 0x21, 0x21,
    0x20, 0x45, 0x52, 0x52, 0x4f, 0x52, 0x2c, 0x20,
    0x4e, 0x4f, 0x54, 0x20, 0x41, 0x20, 0x48, 0x45,
    0x58, 0x20, 0x43, 0x48, 0x41, 0x52, 0x00, 0xc3,
    0x87, 0x04, 0x32, 0x2e, 0xfd, 0x21, 0x33, 0x06,
    0xc3, 0xd2, 0x05, 0x32, 0x30, 0xfd, 0x21, 0x3c,
    0x06, 0xc3, 0xd2, 0x05, 0x32, 0x2f, 0xfd, 0x3a,
    0x34, 0xfd, 0xb7, 0xc2, 0x50, 0x06, 0x3c, 0x32,
    0x34, 0xfd, 0x2a, 0x2f, 0xfd, 0x22, 0x32, 0xfd,
    0x21, 0x56, 0x06, 0xc3, 0xd2, 0x05, 0x32, 0x31,
    0xfd, 0xfe, 0x02, 0xf2, 0x93, 0x06, 0x3a, 0x2e,
    0xfd, 0xb7, 0x21, 0x7d, 0x06, 0xca, 0xd2, 0x05,
    0x3d, 0x32, 0x2e, 0xfd, 0x21, 0x72, 0x06, 0xc3,
    0xd2, 0x05, 0x2a, 0x2f, 0xfd, 0x77, 0x23, 0x22,
    0x2f, 0xfd, 0xc3, 0x5e, 0x06, 0x3a, 0x2d, 0xfd,
    0xb7, 0xc2, 0xa3, 0x06, 0x3a, 0x31, 0xfd, 0xfe,
    0x01, 0xca, 0xbe, 0x06, 0x21, 0xc8, 0x05, 0x22,
    0x27, 0xfd, 0xc9, 0xcd, 0x90, 0x07, 0x2d, 0x49,
    0x67, 0x6e, 0x6f, 0x72, 0x65, 0x64, 0x20, 0x00,
    0xc3, 0x8c, 0x06, 0xcd, 0x90, 0x07, 0x0d, 0x0a,
    0x21, 0x21, 0x21, 0x20, 0x43, 0x48, 0x45, 0x43,
    0x4b, 0x53, 0x55, 0x4d, 0x20, 0x45, 0x52, 0x52,
    0x4f, 0x52, 0x00, 0xc3, 0x87, 0x04, 0x3a, 0x34,
    0xfd, 0xb7, 0xc2, 0xe7, 0x06, 0xcd, 0x90, 0x07,
    0x0d, 0x0a, 0x21, 0x21, 0x21, 0x20, 0x53, 0x54,
    0x41, 0x52, 0x54, 0x20, 0x41, 0x44, 0x44, 0x52,
    0x45, 0x53, 0x53, 0x20, 0x4e, 0x4f, 0x54, 0x20,
    0x53, 0x45, 0x54, 0x00, 0xc3, 0x87, 0x04, 0xcd,
    0x90, 0x07, 0x0d, 0x0a, 0x45, 0x78, 0x65, 0x63,
    0x75, 0x74, 0x65, 0x20, 0x61, 0x74, 0x3a, 0x00,
    0x2a, 0x15, 0x07, 0x22, 0xfd, 0xff, 0x3a, 0x17,
    0x07, 0x32, 0xff, 0xff, 0x2a, 0x32, 0xfd, 0xcd,
    0x61, 0x07, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x00,
    0x3e, 0x01, 0xc3, 0xfd, 0xff, 0xd3, 0x38, 0xe9,
    0xfe, 0x30, 0xfa, 0x36, 0x07, 0xfe, 0x3a, 0xfa,
    0x3a, 0x07, 0xfe, 0x41, 0xfa, 0x36, 0x07, 0xfe,
    0x47, 0xfa, 0x38, 0x07, 0xfe, 0x61, 0xfa, 0x36,
    0x07, 0xfe, 0x67, 0xfa, 0x38, 0x07, 0x37, 0xc9,
    0xd6, 0x07, 0xe6, 0x0f, 0xc9, 0xe3, 0x7e, 0x23,
    0xb7, 0xca, 0x49, 0x07, 0x12, 0x13, 0xc3, 0x3e,
    0x07, 0xe3, 0xc9, 0xf5, 0x78, 0xcd, 0x6c, 0x07,
    0x79, 0xcd, 0x6c, 0x07, 0xf1, 0xc9, 0xf5, 0x7a,
    0xcd, 0x6c, 0x07, 0x7b, 0xcd, 0x6c, 0x07, 0xf1,
    0xc9, 0xf5, 0x7c, 0xcd, 0x6c, 0x07, 0x7d, 0xcd,
    0x6c, 0x07, 0xf1, 0xc9, 0xf5, 0x0f, 0x0f, 0x0f,
    0x0f, 0xcd, 0x7b, 0x07, 0xf1, 0xf5, 0xcd, 0x7b,
    0x07, 0xf1, 0xc9, 0xe6, 0x0f, 0xc6, 0x90, 0x27,
    0xce, 0x40, 0x27, 0xc3, 0xc3, 0x07, 0x7e, 0x23,
    0xb7, 0xc8, 0xcd, 0xc3, 0x07, 0xc3, 0x86, 0x07,
    0xe3, 0xf5, 0xcd, 0x86, 0x07, 0xf1, 0xe3, 0xc9,
    0x7e, 0xcd, 0xc3, 0x07, 0x23, 0x05, 0xc2, 0x98,
    0x07, 0xc9, 0xdb, 0x25, 0xe6, 0x01, 0xca, 0xae,
    0x07, 0xdb, 0x20, 0xc3, 0xc0, 0x07, 0xdb, 0x00,
    0xe6, 0x02, 0xca, 0xa2, 0x07, 0xdb, 0x01, 0xb7,
    0xca, 0xa2, 0x07, 0xfe, 0xff, 0xca, 0xa2, 0x07,
    0xfe, 0x20, 0xf8, 0xf5, 0xfe, 0x0d, 0xca, 0xe2,
    0x07, 0xfe, 0x20, 0xfa, 0xd5, 0x07, 0x3a, 0x35,
    0xfd, 0x3c, 0x32, 0x35, 0xfd, 0xdb, 0x25, 0xe6,
    0x20, 0xca, 0xd5, 0x07, 0xf1, 0xd3, 0x20, 0xd3,
    0x01, 0xc9, 0xaf, 0x32, 0x35, 0xfd, 0xc3, 0xd5,
    0x07, 0x21, 0x65, 0xfd, 0xcd, 0xe5, 0x0d, 0x2a,
    0x55, 0xfd, 0x22, 0x7d, 0xfd, 0xb7, 0xcd, 0x98,
    0x0b, 0x21, 0x00, 0xfe, 0xaf, 0xbe, 0xc9, 0x01,
    0x20, 0x00, 0x09, 0xd2, 0xfc, 0x07, 0xaf, 0x2a,
    0x7d, 0xfd, 0x01, 0xf0, 0xff, 0x09, 0x22, 0x7d,
    0xfd, 0xd0, 0x7c, 0xb5, 0xc8, 0x21, 0x78, 0xfd,
    0xcd, 0xe5, 0x0d, 0xcd, 0x05, 0x0e, 0xc3, 0xf6,
    0x07, 0x21, 0x6d, 0xfd, 0xcd, 0xaf, 0x0d, 0xcd,
    0x4a, 0x08, 0xc2, 0x3d, 0x08, 0xcd, 0x90, 0x07,
    0x20, 0x2d, 0x4e, 0x4f, 0x54, 0x20, 0x46, 0x4f,
    0x55, 0x4e, 0x44, 0x00, 0xc9, 0xcd, 0x90, 0x07,
    0x20, 0x2d, 0x45, 0x58, 0x49, 0x53, 0x54, 0x53,
    0x00, 0xc9, 0xcd, 0xe9, 0x07, 0xc8, 0xcd, 0xab,
    0x09, 0xc0, 0xcd, 0xff, 0x07, 0xc3, 0x4d, 0x08,
    0x21, 0x00, 0xfd, 0x7e, 0xb7, 0xc2, 0x80, 0x08,
    0xcd, 0xff, 0x04, 0x3a, 0x00, 0xfd, 0xb7, 0xc2,
    0x80, 0x08, 0xcd, 0x90, 0x07, 0x20, 0x2d, 0x44,
    0x69, 0x73, 0x6b, 0x20, 0x4e, 0x6f, 0x74, 0x20,
    0x4c, 0x6f, 0x61, 0x64, 0x65, 0x64, 0x00, 0xc9,
    0x21, 0x12, 0xfd, 0x5e, 0x23, 0x56, 0x2a, 0x3e,
    0xfd, 0xcd, 0x36, 0x0e, 0xc2, 0x98, 0x08, 0x21,
    0x18, 0xfd, 0xcd, 0xe5, 0x0d, 0xc3, 0xa4, 0x09,
    0xeb, 0x21, 0x12, 0xfd, 0x73, 0x23, 0x72, 0x2a,
    0x36, 0xfd, 0xcd, 0x5e, 0x0e, 0x21, 0x0e, 0xfd,
    0xe5, 0xcd, 0x4c, 0x0e, 0x44, 0x4d, 0xe1, 0x7a,
    0xb8, 0xc2, 0xb9, 0x08, 0x7b, 0xb9, 0xca, 0x88,
    0x09, 0xd2, 0xc4, 0x08, 0x01, 0x00, 0x00, 0x2b,
    0x2b, 0xc3, 0xcc, 0x08, 0x7b, 0x91, 0x5f, 0x7a,
    0x98, 0x57, 0x23, 0x23, 0xcd, 0x4c, 0x0e, 0x7a,
    0xb3, 0xca, 0x37, 0x09, 0x23, 0x7c, 0xb5, 0xc2,
    0x06, 0x09, 0xcd, 0x90, 0x07, 0x20, 0x2d, 0x45,
    0x52, 0x52, 0x4f, 0x52, 0x2c, 0x20, 0x4e, 0x4f,
    0x20, 0x4d, 0x4f, 0x52, 0x45, 0x20, 0x41, 0x4c,
    0x4c, 0x4f, 0x43, 0x41, 0x54, 0x45, 0x44, 0x20,
    0x43, 0x4c, 0x55, 0x53, 0x54, 0x45, 0x52, 0x53,
    0x21, 0x00, 0x76, 0xc3, 0x02, 0x09, 0x2b, 0xc5,
    0xd5, 0xe5, 0x5c, 0x2a, 0x61, 0xfd, 0x7d, 0x83,
    0x5f, 0x7c, 0xce, 0x00, 0x57, 0x2a, 0x63, 0xfd,
    0xd2, 0x1c, 0x09, 0x23, 0x44, 0x4d, 0xcd, 0x98,
    0x0b, 0xd1, 0x21, 0x00, 0xfe, 0xb7, 0x7b, 0x17,
    0x6f, 0x7c, 0xce, 0x00, 0x67, 0xcd, 0x4c, 0x0e,
    0xd1, 0xc1, 0x03, 0x1b, 0xc3, 0xcf, 0x08, 0xeb,
    0x21, 0x0e, 0xfd, 0x71, 0x23, 0x70, 0x23, 0x73,
    0x23, 0x72, 0xeb, 0x2b, 0x2b, 0x01, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x3e, 0x08, 0x32, 0x3a, 0xfd,
    0x3a, 0x51, 0xfd, 0x1f, 0x32, 0x39, 0xfd, 0xd2,
    0x60, 0x09, 0xeb, 0x19, 0xeb, 0x79, 0x88, 0x4f,
    0x29, 0x78, 0x17, 0x47, 0x3a, 0x3a, 0xfd, 0x3d,
    0x32, 0x3a, 0xfd, 0x3a, 0x39, 0xfd, 0xc2, 0x53,
    0x09, 0x06, 0x00, 0x2a, 0x69, 0xfd, 0x19, 0xeb,
    0x2a, 0x6b, 0xfd, 0xd2, 0x7f, 0x09, 0x03, 0x09,
    0xe5, 0xc1, 0x21, 0x14, 0xfd, 0xcd, 0xed, 0x0d,
    0x21, 0x12, 0xfd, 0x5e, 0x23, 0x56, 0x2a, 0x3b,
    0xfd, 0xcd, 0x5e, 0x0e, 0x21, 0x14, 0xfd, 0x01,
    0x00, 0x00, 0x16, 0x00, 0x5f, 0xcd, 0xf5, 0x0d,
    0x23, 0xcd, 0xed, 0x0d, 0xcd, 0x98, 0x0b, 0x21,
    0x00, 0xfe, 0xc9, 0xe5, 0x06, 0x08, 0x11, 0x6d,
    0xfd, 0x1a, 0xbe, 0xc2, 0xd5, 0x09, 0x23, 0x13,
    0x05, 0xc2, 0xb1, 0x09, 0xe1, 0xe5, 0x11, 0x08,
    0x00, 0x19, 0x06, 0x03, 0x11, 0x75, 0xfd, 0x1a,
    0xbe, 0xc2, 0xd5, 0x09, 0x23, 0x13, 0x05, 0xc2,
    0xc7, 0x09, 0x04, 0xe1, 0xc9, 0xaf, 0xe1, 0xc9,
    0xcd, 0x06, 0x0c, 0xc0, 0xcd, 0x90, 0x07, 0x4d,
    0x42, 0x52, 0x00, 0x01, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x37, 0xcd, 0x98, 0x0b, 0xcd, 0x83, 0x0b,
    0xc0, 0xcd, 0x90, 0x07, 0x20, 0x54, 0x79, 0x70,
    0x65, 0x00, 0x3a, 0xc2, 0xff, 0xcd, 0x6c, 0x07,
    0x32, 0x46, 0xfd, 0xfe, 0x04, 0xca, 0x12, 0x0a,
    0xfe, 0x06, 0xca, 0x12, 0x0a, 0xfe, 0x86, 0xc2,
    0x6d, 0x0b, 0x21, 0xc6, 0xff, 0x11, 0x47, 0xfd,
    0x06, 0x08, 0xcd, 0x43, 0x0e, 0xcd, 0x90, 0x07,
    0x20, 0x50, 0x42, 0x52, 0x00, 0x21, 0x47, 0xfd,
    0xcd, 0xe5, 0x0d, 0xcd, 0x98, 0x0b, 0xcd, 0x83,
    0x0b, 0xc0, 0x21, 0x0b, 0xfe, 0x11, 0x4f, 0xfd,
    0x06, 0x0a, 0xcd, 0x43, 0x0e, 0xeb, 0x2b, 0x2b,
    0x7e, 0x23, 0xb6, 0xc2, 0x51, 0x0a, 0x2b, 0xeb,
    0x21, 0x20, 0xfe, 0xcd, 0x41, 0x0e, 0xc3, 0x58,
    0x0a, 0xaf, 0x23, 0x77, 0x23, 0x77, 0x23, 0xeb,
    0x21, 0x1c, 0xfe, 0xcd, 0x41, 0x0e, 0x21, 0x16,
    0xfe, 0xcd, 0x3c, 0x0e, 0x2a, 0x52, 0xfd, 0xeb,
    0x2a, 0x47, 0xfd, 0x19, 0x22, 0x61, 0xfd, 0x2a,
    0x49, 0xfd, 0xd2, 0x76, 0x0a, 0x23, 0x22, 0x63,
    0xfd, 0x3a, 0x54, 0xfd, 0x47, 0x2a, 0x5f, 0xfd,
    0xeb, 0x21, 0x00, 0x00, 0x19, 0x05, 0xc2, 0x84,
    0x0a, 0xeb, 0x2a, 0x61, 0xfd, 0x19, 0x22, 0x65,
    0xfd, 0x2a, 0x63, 0xfd, 0xd2, 0x98, 0x0a, 0x23,
    0x22, 0x67, 0xfd, 0x06, 0x10, 0x2a, 0x4f, 0xfd,
    0xeb, 0x2a, 0x55, 0xfd, 0x7b, 0x1f, 0xda, 0xd3,
    0x0a, 0x7d, 0x1f, 0xda, 0xd3, 0x0a, 0x7a, 0x1f,
    0x57, 0x7b, 0x1f, 0x5f, 0x7c, 0x1f, 0x67, 0x7d,
    0x1f, 0x6f, 0x05, 0xc2, 0xa4, 0x0a, 0xcd, 0x90,
    0x07, 0x20, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x20,
    0x44, 0x41, 0x54, 0x41, 0x53, 0x54, 0x41, 0x52,
    0x54, 0x00, 0xc9, 0x06, 0x05, 0x29, 0xda, 0xbe,
    0x0a, 0x05, 0xc2, 0xd5, 0x0a, 0x7b, 0x2f, 0x4f,
    0x7a, 0x2f, 0x47, 0x03, 0x11, 0xff, 0xff, 0x09,
    0x13, 0xda, 0xe7, 0x0a, 0x2a, 0x65, 0xfd, 0x19,
    0x22, 0x69, 0xfd, 0x2a, 0x67, 0xfd, 0xd2, 0xfa,
    0x0a, 0x23, 0x22, 0x6b, 0xfd, 0x3a, 0x51, 0xfd,
    0x3d, 0x32, 0x3d, 0xfd, 0x3c, 0xca, 0x57, 0x0b,
    0x01, 0x00, 0x08, 0x1f, 0xd2, 0x11, 0x0b, 0x50,
    0x0c, 0x05, 0xc2, 0x0b, 0x0b, 0x3e, 0x01, 0xb9,
    0xc2, 0x2b, 0x0b, 0x7a, 0x2f, 0xc6, 0x0a, 0x32,
    0x38, 0xfd, 0x21, 0x7b, 0x0e, 0x11, 0x8b, 0x0e,
    0xc3, 0x30, 0x0b, 0x21, 0x5f, 0x0e, 0xe5, 0xd1,
    0x22, 0x36, 0xfd, 0xeb, 0x22, 0x3b, 0xfd, 0xcd,
    0x90, 0x07, 0x20, 0x56, 0x4f, 0x4c, 0x3d, 0x00,
    0x21, 0x2b, 0xfe, 0x06, 0x0b, 0xcd, 0x98, 0x07,
    0xcd, 0x90, 0x07, 0x20, 0x53, 0x59, 0x53, 0x3d,
    0x00, 0x06, 0x08, 0xcd, 0x98, 0x07, 0xc9, 0xcd,
    0x90, 0x07, 0x0d, 0x0a, 0x45, 0x72, 0x72, 0x6f,
    0x72, 0x3d, 0x30, 0x20, 0x53, 0x65, 0x63, 0x2f,
    0x43, 0x6c, 0x75, 0x73, 0x00, 0xcd, 0x90, 0x07,
    0x0d, 0x0a, 0x46, 0x41, 0x54, 0x20, 0x49, 0x6e,
    0x69, 0x74, 0x20, 0x46, 0x41, 0x49, 0x4c, 0x45,
    0x44, 0x00, 0xc9, 0xcd, 0x90, 0x07, 0x20, 0x53,
    0x00, 0x2b, 0x3e, 0xaa, 0xbe, 0xc2, 0x6d, 0x0b,
    0x2b, 0x3e, 0x55, 0xbe, 0xc2, 0x6d, 0x0b, 0xc9,
    0x21, 0x78, 0xfd, 0xda, 0xa2, 0x0b, 0xcd, 0x1a,
    0x0e, 0xc8, 0x21, 0x78, 0xfd, 0xcd, 0xed, 0x0d,
    0xcd, 0xeb, 0x0b, 0x06, 0x05, 0x3e, 0x11, 0xcd,
    0x0b, 0x0d, 0xca, 0xbe, 0x0b, 0x05, 0xc2, 0xad,
    0x0b, 0x05, 0xcd, 0xf3, 0x0c, 0xc9, 0x06, 0x00,
    0xcd, 0x51, 0x0d, 0xfe, 0xfe, 0xca, 0xd0, 0x0b,
    0x05, 0xc2, 0xc0, 0x0b, 0xcd, 0xf3, 0x0c, 0xc9,
    0x01, 0x00, 0x02, 0xcd, 0x51, 0x0d, 0x77, 0x23,
    0x0d, 0xc2, 0xd3, 0x0b, 0x05, 0xc2, 0xd3, 0x0b,
    0xcd, 0x51, 0x0d, 0xcd, 0x51, 0x0d, 0xcd, 0xf3,
    0x0c, 0xaf, 0xc9, 0x3a, 0x40, 0xfd, 0xfe, 0x03,
    0xca, 0xfc, 0x0b, 0x79, 0xeb, 0x29, 0x17, 0x47,
    0x4c, 0x55, 0x1e, 0x00, 0x21, 0x42, 0xfd, 0xcd,
    0xed, 0x0d, 0x21, 0x00, 0xfe, 0xc9, 0xcd, 0xf3,
    0x0c, 0x0e, 0x80, 0x3e, 0xff, 0x32, 0x40, 0xfd,
    0xd3, 0x30, 0x0d, 0xc2, 0x10, 0x0c, 0xcd, 0xfa,
    0x0c, 0xcd, 0x90, 0x07, 0x0d, 0x0a, 0x49, 0x6e,
    0x69, 0x74, 0x20, 0x53, 0x44, 0x00, 0xcd, 0x7b,
    0x0d, 0x06, 0x00, 0x3e, 0x00, 0xcd, 0x0b, 0x0d,
    0xfe, 0x01, 0xca, 0x4a, 0x0c, 0x05, 0xc2, 0x2b,
    0x0c, 0xcd, 0x90, 0x07, 0x2d, 0x46, 0x41, 0x49,
    0x4c, 0x45, 0x44, 0x00, 0xcd, 0xf3, 0x0c, 0xaf,
    0x3d, 0xc9, 0xcd, 0x90, 0x07, 0x20, 0x54, 0x79,
    0x70, 0x65, 0x23, 0x00, 0x21, 0xaa, 0x01, 0x22,
    0x42, 0xfd, 0x3e, 0x08, 0xcd, 0x0b, 0x0d, 0xe6,
    0x04, 0xca, 0x6c, 0x0c, 0x3e, 0x01, 0x32, 0x40,
    0xfd, 0xc3, 0x87, 0x0c, 0xcd, 0x51, 0x0d, 0xcd,
    0x51, 0x0d, 0xcd, 0x51, 0x0d, 0xcd, 0x51, 0x0d,
    0x32, 0x41, 0xfd, 0xfe, 0xaa, 0x3e, 0xaa, 0xc2,
    0x39, 0x0c, 0x3e, 0x02, 0x32, 0x40, 0xfd, 0xcd,
    0x7b, 0x07, 0xcd, 0x90, 0x07, 0x20, 0x41, 0x43,
    0x4d, 0x44, 0x34, 0x31, 0x00, 0xcd, 0x7b, 0x0d,
    0x06, 0x00, 0x3e, 0x37, 0xcd, 0x0b, 0x0d, 0x3e,
    0x29, 0xcd, 0x0b, 0x0d, 0xfe, 0x00, 0xca, 0xb4,
    0x0c, 0xaf, 0xcd, 0x06, 0x0d, 0x05, 0xc2, 0x9a,
    0x0c, 0xc3, 0x39, 0x0c, 0xcd, 0x90, 0x07, 0x2b,
    0x00, 0x3a, 0x40, 0xfd, 0xfe, 0x02, 0xc2, 0xee,
    0x0c, 0x3e, 0x3a, 0xcd, 0x0b, 0x0d, 0xfe, 0x00,
    0xc2, 0x39, 0x0c, 0xcd, 0x51, 0x0d, 0xe6, 0xc0,
    0xfe, 0xc0, 0xc2, 0xe5, 0x0c, 0x3e, 0x03, 0x32,
    0x40, 0xfd, 0xcd, 0x90, 0x07, 0x20, 0x54, 0x79,
    0x70, 0x65, 0x23, 0x33, 0x00, 0xcd, 0x51, 0x0d,
    0xcd, 0x51, 0x0d, 0xcd, 0x51, 0x0d, 0xcd, 0xf3,
    0x0c, 0xaf, 0xc9, 0xf5, 0x3e, 0x01, 0xd3, 0x31,
    0xf1, 0xc9, 0xf5, 0x3e, 0x00, 0xd3, 0x31, 0xcd,
    0x04, 0x0d, 0xf1, 0xc9, 0x3e, 0x0d, 0x3d, 0xc2,
    0x06, 0x0d, 0xc9, 0xc5, 0xcd, 0xfa, 0x0c, 0xcd,
    0x59, 0x0d, 0x06, 0xff, 0xfe, 0x00, 0xc2, 0x1b,
    0x0d, 0x06, 0x95, 0xfe, 0x08, 0xc2, 0x22, 0x0d,
    0x06, 0x87, 0xf6, 0x40, 0xd3, 0x30, 0x3a, 0x45,
    0xfd, 0xd3, 0x30, 0x3a, 0x44, 0xfd, 0xd3, 0x30,
    0x3a, 0x43, 0xfd, 0xd3, 0x30, 0x3a, 0x42, 0xfd,
    0xd3, 0x30, 0x00, 0x78, 0xd3, 0x30, 0x06, 0x00,
    0xcd, 0x51, 0x0d, 0x32, 0x41, 0xfd, 0xb7, 0xf2,
    0x4f, 0x0d, 0x05, 0xc2, 0x40, 0x0d, 0xb7, 0xc1,
    0xc9, 0x3e, 0xff, 0xd3, 0x30, 0x00, 0xdb, 0x30,
    0xc9, 0xf5, 0xc5, 0x06, 0x00, 0x0e, 0x01, 0xcd,
    0x51, 0x0d, 0x3c, 0xc2, 0x6f, 0x0d, 0x0d, 0xc2,
    0x5f, 0x0d, 0xc1, 0xf1, 0x37, 0x3f, 0xc9, 0xaf,
    0xcd, 0x06, 0x0d, 0x05, 0xc2, 0x5d, 0x0d, 0xc1,
    0xf1, 0x37, 0xc9, 0xaf, 0x32, 0x42, 0xfd, 0x32,
    0x43, 0xfd, 0x32, 0x44, 0xfd, 0x32, 0x45, 0xfd,
    0xc9, 0xcd, 0xa2, 0x07, 0xfe, 0x1b, 0x37, 0xc8,
    0xfe, 0x0d, 0xc8, 0xb9, 0xc8, 0xcd, 0xa6, 0x0d,
    0xfe, 0x21, 0xda, 0x89, 0x0d, 0x05, 0x04, 0xc8,
    0x77, 0x23, 0x05, 0xc3, 0x89, 0x0d, 0xfe, 0x61,
    0xd8, 0xfe, 0x7b, 0xd0, 0xe6, 0x5f, 0xc9, 0xe5,
    0x06, 0x08, 0x7e, 0xb7, 0xca, 0xc4, 0x0d, 0xfe,
    0x20, 0xca, 0xc4, 0x0d, 0xcd, 0xc3, 0x07, 0x23,
    0x05, 0xc2, 0xb2, 0x0d, 0x3e, 0x2e, 0xcd, 0xc3,
    0x07, 0xe1, 0xe5, 0x01, 0x08, 0x00, 0x09, 0x06,
    0x03, 0x7e, 0xb7, 0xca, 0xe3, 0x0d, 0xfe, 0x20,
    0xca, 0xe3, 0x0d, 0xcd, 0xc3, 0x07, 0x23, 0x05,
    0xc2, 0xd1, 0x0d, 0xe1, 0xc9, 0x5e, 0x23, 0x56,
    0x23, 0x4e, 0x23, 0x46, 0xc9, 0x73, 0x23, 0x72,
    0x23, 0x71, 0x23, 0x70, 0xc9, 0x7b, 0x86, 0x5f,
    0x23, 0x7a, 0x8e, 0x57, 0x23, 0x79, 0x8e, 0x4f,
    0x23, 0x78, 0x8e, 0x47, 0xc9, 0x13, 0x7a, 0xb3,
    0xc0, 0x03, 0xc9, 0x7a, 0xb3, 0xc2, 0x11, 0x0e,
    0x0b, 0x1b, 0xc9, 0x7a, 0xb3, 0xc0, 0xb1, 0xc0,
    0xb0, 0xc9, 0x23, 0x23, 0x23, 0x78, 0xbe, 0xc2,
    0x32, 0x0e, 0x2b, 0x79, 0xbe, 0xc2, 0x33, 0x0e,
    0x2b, 0x7a, 0xbe, 0xc2, 0x34, 0x0e, 0x2b, 0x7b,
    0xbe, 0xc9, 0x2b, 0x2b, 0x2b, 0xc9, 0x7a, 0xbc,
    0xc0, 0x7b, 0xbd, 0xc9, 0x06, 0x02, 0xc3, 0x43,
    0x0e, 0x06, 0x04, 0x7e, 0x12, 0x23, 0x13, 0x05,
    0xc2, 0x43, 0x0e, 0xc9, 0x7e, 0x23, 0x66, 0x6f,
    0xc9, 0xf5, 0xc5, 0xe5, 0x77, 0x23, 0x05, 0xc2,
    0x54, 0x0e, 0xe1, 0xc1, 0xf1, 0xc9, 0xe9, 0x3a,
    0x51, 0xfd, 0xeb, 0x1e, 0x00, 0x47, 0x0e, 0x08,
    0x29, 0x7b, 0x07, 0x5f, 0x7c, 0x90, 0xda, 0x73,
    0x0e, 0x67, 0x1c, 0x0d, 0xc2, 0x68, 0x0e, 0x7c,
    0x16, 0x00, 0xc9, 0x3a, 0x38, 0xfd, 0x47, 0x05,
    0xc8, 0xb7, 0x7a, 0x1f, 0x57, 0x7b, 0x1f, 0x5f,
    0xc3, 0x7f, 0x0e, 0x3a, 0x3d, 0xfd, 0xa3, 0xc9
    };

static uint8 jair_ram[JAIR_ROM_SIZE] = {0};

/*********************/
/* JAIR Definitions */
/*********************/

/*
**  PORT ASSIGNMENTS
*/
#define JAIR_CPU_IO  0x20                 /* BASE ADDRESS FOR ONBOARD CPU I/O */
#define JAIR_UART0   JAIR_CPU_IO          /* UART0 OFFSET                     */
#define JAIR_UART1   JAIR_CPU_IO + 0x08   /* UART1 OFFSET                     */
#define JAIR_SPI     JAIR_CPU_IO + 0x10   /* SPI OFFSET                       */
#define JAIR_SPI_SS  JAIR_CPU_IO + 0x11   /* SPI_SS OFFSET                    */
#define JAIR_PPORT   JAIR_CPU_IO + 0x18   /* PPORT OFFSET                     */

#define JAIR_SDATA   0x00    /* 8250 SERIAL DATA                 */
#define JAIR_IER     0x01    /* 8250 INTERRUPT ENABLE REGISTER   */
#define JAIR_IIR     0x02    /* 8250 INTERRUPT IDENT REGISTER    */
#define JAIR_LCR     0x03    /* 8250 LINE CONTROL REGISTER       */
#define JAIR_MCR     0x04    /* 8250 MODEM CONTROL REGISTER      */
#define JAIR_LSR     0x05    /* 8250 LINE STATUS REGISTER        */
#define JAIR_MSR     0x06    /* 8250 MODEM STATUS REGISTER       */
#define JAIR_SR      0x07    /* 8250 SCRATCH REGISTER            */

/*
**  BIT ASSIGNMENT MASKS
*/
#define JAIR_DR      0x01    /* SERIAL DATA READY                */
#define JAIR_OE      0x02    /* SERIAL OVERRUN ERROR             */
#define JAIR_THRE    0x20    /* SERIAL TRANSMITTER BUFFER EMPTY  */
#define JAIR_TEMT    0x40    /* SERIAL TRANSMITTER BUFFER EMPTY  */
#define JAIR_DLAB    0x80    /* DIVISOR LATCH ACCESS BIT         */

#define JAIR_DCTS    0x01    /* Clear to Send (Delta)            */
#define JAIR_DDSR    0x02    /* Data Set Ready (Delta)           */
#define JAIR_DRNG    0x04    /* Ring Indicate (Delta)            */
#define JAIR_DDCD    0x08    /* Data Carrier Detect (Delta)      */
#define JAIR_CTS     0x10    /* Clear to Send                    */
#define JAIR_DSR     0x20    /* Data Set Ready                   */
#define JAIR_RNG     0x40    /* Ring Indicate                    */
#define JAIR_DCD     0x80    /* Data Carrier Detect              */
#define JAIR_DTR     0x01    /* Data Terminal Ready              */
#define JAIR_RTS     0x02    /* Request to Send                  */

#define JAIR_STATE_IDLE 0    /* idle */
#define JAIR_STATE_CMD  1    /* receiving command */
#define JAIR_STATE_RESP 2    /* sending response */
#define JAIR_STATE_SBLK 3    /* start block */
#define JAIR_STATE_WBLK 4    /* write block */

#define JAIR_STAT_WAIT  10000 /* Status wait interval */
#define JAIR_IO_WAIT    250   /* IO wait interval */

/*
** SD Commands
*/
#define JAIR_CMD0        0
#define JAIR_CMD8        8          /* send if condition */
#define JAIR_CMD13       13         /* send status */
#define JAIR_CMD17       17         /* read single block */
#define JAIR_CMD24       24         /* write single block */
#define JAIR_CMD55       55         /* application command */
#define JAIR_ACMD41      41 + 0x80  /* send op condition */

/****************/
/* JAIR Device */
/****************/

#define JAIR_NAME  "Josh's Altair / IMASI Replacement CPU & SBC"
#define JAIR_SNAME "JAIR"

#define JAIR_UNITS 1

#define UNIT_V_JAIR_VERBOSE      (UNIT_V_UF + 0)                      /* VERBOSE / QUIET  */
#define UNIT_JAIR_VERBOSE        (1 << UNIT_V_JAIR_VERBOSE)
#define UNIT_V_JAIR_WPROTECT     (UNIT_V_UF + 1)                      /* WRTENB / WRTPROT */
#define UNIT_JAIR_WPROTECT       (1 << UNIT_V_JAIR_WPROTECT)
#define UNIT_V_JAIR_ROM          (UNIT_V_UF + 2)                      /* WRTENB / WRTPROT */
#define UNIT_JAIR_ROM            (1 << UNIT_V_JAIR_ROM)
#define UNIT_V_JAIR_CONSOLE      (UNIT_V_UF + 3)                      /* Port checks console for input */
#define UNIT_JAIR_CONSOLE        (1 << UNIT_V_JAIR_CONSOLE)

/*
** JAIR Registers and Interface Controls
*/

typedef struct {
    uint32    rom_base;       /* Memory Base Address */
    uint32    rom_size;       /* Memory Address space requirement */
    uint32    io_base;        /* I/O Base Address */
    uint32    io_size;        /* I/O Address Space requirement */
    t_bool    sr_ena;         /* Shadow ROM enable */
    t_bool    spi_cs;         /* SPI *CS (Active Low) */
    uint8     sd_istate;      /* SD Card Input State */
    uint8     sd_ostate;      /* SD Card Output State */
    uint8     sd_cmd[512+6];  /* SD Card Command */
    uint16    sd_cmd_len;
    uint16    sd_cmd_idx;
    uint8     sd_resp[512+6]; /* SD Card Response */
    uint16    sd_resp_len;
    uint16    sd_resp_idx;
    t_bool    sd_appcmd;      /* SD app command flag */
} JAIR_CTX;

static JAIR_CTX jair_ctx = {
    JAIR_ROM_BASE, JAIR_ROM_SIZE, JAIR_SPI, 2
};

static UNIT jair_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE + UNIT_JAIR_ROM, 0) }
};

static REG jair_reg[] = {
    { NULL }
};

static MTAB jair_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "PORT", "PORT",
        NULL, jair_show_ports, NULL, "I/O port address" },
    { UNIT_JAIR_ROM,   UNIT_JAIR_ROM, "ROM",    "ROM",    &jair_set_rom,
        NULL, NULL, "Enable JAIR ROM"  },
    { UNIT_JAIR_ROM,   0,              "NOROM", "NOROM",  &jair_set_norom,
        NULL, NULL, "Disable JAIR ROM"},
    { 0 }
};

DEVICE jair_dev = {
    JAIR_SNAME,                          /* name */
    jair_unit,                           /* unit */
    jair_reg,                            /* registers */
    jair_mod,                            /* modifiers */
    JAIR_UNITS,                          /* # units */
    10,                                  /* address radix */
    31,                                  /* address width */
    1,                                   /* addr increment */
    8,                                   /* data radix */
    8,                                   /* data width */
    NULL,                                /* examine routine */
    NULL,                                /* deposit routine */
    &jair_reset,                         /* reset routine */
    &jair_boot,                          /* boot routine */
    &jair_attach,                        /* attach routine */
    &jair_detach,                        /* detach routine */
    &jair_ctx,                           /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG), /* flags */
    ERROR_MSG,                           /* debug control */
    jair_dt,                             /* debug flags */
    NULL,                                /* mem size routine */
    NULL,                                /* logical name */
    &jair_help,                          /* help */
    NULL,                                /* attach help */
    NULL,                                /* context for help */
    &jair_description                    /* description */
};

/****************/
/* GENERIC PORT */
/****************/

#define JAIR_IOBUF_SIZE     (128)
#define JAIR_IOBUF_MASK     (JAIR_IOBUF_SIZE-1)

#define JAIR_PORT_UNITS     3

#define JAIR_UNIT_STAT      0
#define JAIR_UNIT_RX        1
#define JAIR_UNIT_TX        2

typedef struct {
    PNP_INFO  pnp;          /* Must be first */
    int32     conn;         /* Connected Status */
    uint16    baud;         /* Baud rate */
    uint8     status;       /* Status Byte */
    uint8     rdr;          /* Receive Data Ready */
    uint8     rxd;          /* Receive Data Buffer */
    uint8     txd;          /* Transmit Data Buffer */
    t_bool    txp;          /* Transmit Data Pending */
    uint8     ier;          /* Interrupt Enable Register */
    uint8     iir;          /* Interrupt Ident Register */
    uint8     lcr;          /* Line Control Register */
    uint8     mcr;          /* Modem Control Register */
    uint8     lsr;          /* Line Status Register */
    uint8     msr;          /* Modem Status Register */
    uint8     sr;           /* Scratch Register */
    uint8     dlls;         /* Divisor Latch LS */
    uint8     dlms;         /* Divisor Latch MS */
    TMLN     *tmln;         /* TMLN pointer */
    TMXR     *tmxr;         /* TMXR pointer */
    int32    iobuf[JAIR_IOBUF_SIZE];
    uint32   iobufin;
    uint32   iobufout;
} JAIR_PORT_CTX;

/**************************/
/* JAIRS0 Keyboard Device */
/**************************/

#define JAIRS0_NAME  "JAIR Serial Port 0"
#define JAIRS0_SNAME "JAIRS0"

static TMLN jairs0_tmln[1] = { /* line descriptors */
    { 0 }
};

static TMXR jairs0_tmxr = { /* multiplexer descriptor */
    1,                      /* number of terminal lines */
    0,                      /* listening port (reserved) */
    0,                      /* master socket  (reserved) */
    jairs0_tmln,            /* line descriptor array */
    NULL,                   /* line connection order */
    NULL                    /* multiplexer device (derived internally) */
};

static JAIR_PORT_CTX jairs0_ctx = {
    {0, 0, JAIR_UART0, 8}, 0, 9600, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    jairs0_tmln, &jairs0_tmxr
};

static REG jairs0_reg[] = {
    { DRDATAD (BAUD0, jairs0_ctx.baud, 16, "Serial port baud register"), },
    { HRDATAD (TXP0, jairs0_ctx.txp, 1, "Serial port TX data pending"), },
    { HRDATAD (TXD0, jairs0_ctx.txd, 8, "Serial port TX data register"), },
    { HRDATAD (RDR0, jairs0_ctx.rdr, 1, "Serial port RX data ready"), },
    { HRDATAD (RXD0, jairs0_ctx.rxd, 8, "Serial port RX register"), },
    { HRDATAD (BUFIN0, jairs0_ctx.iobufin, 16, "Serial port buffer in ptr"), },
    { HRDATAD (BUFOUT0, jairs0_ctx.iobufout, 16, "Serial port buffer out ptr"), },
    { HRDATAD (LSR0, jairs0_ctx.lsr, 8, "Serial port line status register"), },
    { HRDATAD (MSR0, jairs0_ctx.msr, 8, "Serial port modem status register"), },
    { NULL }
};

static UNIT jairs0_unit[JAIR_PORT_UNITS] = {
    { UDATA (&jair_svc, UNIT_ATTABLE | UNIT_JAIR_CONSOLE, 0), JAIR_STAT_WAIT },
    { UDATA (&jair_rx_svc, UNIT_DIS | UNIT_JAIR_CONSOLE, 0), JAIR_IO_WAIT },
    { UDATA (&jair_tx_svc, UNIT_DIS | UNIT_JAIR_CONSOLE, 0), JAIR_IO_WAIT }
};

static MTAB jairs0_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "PORT", "PORT",
        NULL, jair_show_ports, NULL, "Show serial I/O ports" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "BAUD", "BAUD",
        &jair_set_baud, &jair_show_baud, NULL, "Set baud rate (default=9600)" },
    { 0 }
};

DEVICE jairs0_dev = {
    JAIRS0_SNAME,                                  /* name */
    jairs0_unit,                                   /* unit */
    jairs0_reg,                                    /* registers */
    jairs0_mod,                                    /* modifiers */
    JAIR_PORT_UNITS,                               /* # units */
    10,                                            /* address radix */
    31,                                            /* address width */
    1,                                             /* addr increment */
    8,                                             /* data radix */
    8,                                             /* data width */
    NULL,                                          /* examine routine */
    NULL,                                          /* deposit routine */
    &jair_port_reset,                              /* reset routine */
    NULL,                                          /* boot routine */
    &jair_attach_mux,                              /* attach routine */
    &jair_detach_mux,                              /* detach routine */
    &jairs0_ctx,                                   /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX), /* flags */
    ERROR_MSG,                                     /* debug control */
    jair_dt,                                       /* debug flags */
    NULL,                                          /* mem size routine */
    NULL,                                          /* logical name */
    NULL,                                          /* help */
    NULL,                                          /* attach help */
    &jairs0_tmxr,                                  /* context for help */
    &jairs0_description                            /* description */
};

/************************/
/* JAIRS1 Serial Device */
/************************/

#define JAIRS1_NAME  "JAIR Serial Port 1"
#define JAIRS1_SNAME "JAIRS1"

static TMLN jairs1_tmln[1] = { /* line descriptors */
    { 0 }
};

static TMXR jairs1_tmxr = {  /* multiplexer descriptor */
    1,                       /* number of terminal lines */
    0,                       /* listening port (reserved) */
    0,                       /* master socket  (reserved) */
    jairs1_tmln,             /* line descriptor array */
    NULL,                    /* line connection order */
    NULL                     /* multiplexer device (derived internally) */
};

static JAIR_PORT_CTX jairs1_ctx = {
    {0, 0, JAIR_UART1, 8}, 0, 9600, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    jairs1_tmln, &jairs1_tmxr
};

static REG jairs1_reg[] = {
    { DRDATAD (BAUD1, jairs1_ctx.baud, 16, "Serial port baud register"), },
    { HRDATAD (TXP1, jairs1_ctx.txp, 1, "Serial port TX data pending"), },
    { HRDATAD (TXD1, jairs1_ctx.txd, 8, "Serial port TX data register"), },
    { HRDATAD (RDR1, jairs1_ctx.rdr, 1, "Serial port RX data ready"), },
    { HRDATAD (RXD1, jairs1_ctx.rxd, 8, "Serial port RX register"), },
    { HRDATAD (BUFIN1, jairs1_ctx.iobufin, 16, "Serial port buffer in ptr"), },
    { HRDATAD (BUFOUT1, jairs1_ctx.iobufout, 16, "Serial port buffer out ptr"), },
    { HRDATAD (LSR1, jairs1_ctx.lsr, 8, "Serial port line status register"), },
    { HRDATAD (MSR1, jairs1_ctx.msr, 8, "Serial port modem status register"), },
    { NULL }
};

static UNIT jairs1_unit[JAIR_PORT_UNITS] = {
    { UDATA (&jair_svc, UNIT_ATTABLE, 0), JAIR_STAT_WAIT },
    { UDATA (&jair_rx_svc, UNIT_DIS, 0), JAIR_IO_WAIT },
    { UDATA (&jair_tx_svc, UNIT_DIS, 0), JAIR_IO_WAIT }
};

static MTAB jairs1_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "PORT", "PORT",
        NULL, jair_show_ports, NULL, "Show serial I/O ports" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "BAUD", "BAUD",
        &jair_set_baud, &jair_show_baud, NULL, "Set baud rate (default=9600)" },
    { 0 }
};

DEVICE jairs1_dev = {
    JAIRS1_SNAME,                                  /* name */
    jairs1_unit,                                   /* unit */
    jairs1_reg,                                    /* registers */
    jairs1_mod,                                    /* modifiers */
    JAIR_PORT_UNITS,                               /* # units */
    10,                                            /* address radix */
    31,                                            /* address width */
    1,                                             /* addr increment */
    8,                                             /* data radix */
    8,                                             /* data width */
    NULL,                                          /* examine routine */
    NULL,                                          /* deposit routine */
    &jair_port_reset,                              /* reset routine */
    NULL,                                          /* boot routine */
    &jair_attach_mux,                              /* attach routine */
    &jair_detach_mux,                              /* detach routine */
    &jairs1_ctx,                                   /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX), /* flags */
    ERROR_MSG,                                     /* debug control */
    jair_dt,                                       /* debug flags */
    NULL,                                          /* mem size routine */
    NULL,                                          /* logical name */
    NULL,                                          /* help */
    NULL,                                          /* attach help */
    &jairs1_tmxr,                                  /* context for help */
    &jairs1_description                            /* description */
};

/***********************/
/* JAIRP Parallel Port */
/***********************/

#define JAIRP_NAME  "JAIR Parallel Port"
#define JAIRP_SNAME "JAIRP"

static TMLN jairp_tmln[1] = { /* line descriptors */
    { 0 }
};

static TMXR jairp_tmxr = {  /* multiplexer descriptor */
    1,                       /* number of terminal lines */
    0,                       /* listening port (reserved) */
    0,                       /* master socket  (reserved) */
    jairp_tmln,             /* line descriptor array */
    NULL,                    /* line connection order */
    NULL                     /* multiplexer device (derived internally) */
};

static JAIR_PORT_CTX jairp_ctx = {
    {0, 0, JAIR_PPORT, 1}, 0, 9600, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    jairp_tmln, &jairp_tmxr
};

static REG jairp_reg[] = {
    { DRDATAD (BAUDP, jairp_ctx.baud, 16, "Serial port baud register"), },
    { HRDATAD (TXPP, jairp_ctx.txp, 1, "Printer port tx pending"), },
    { HRDATAD (TXDP, jairp_ctx.txd, 8, "Printer port data register"), },
    { HRDATAD (RDRP, jairp_ctx.rdr, 1, "Printer port RX data ready"), },
    { HRDATAD (RXDP, jairp_ctx.rxd, 8, "Printer port RX data register"), },
    { NULL }
};

static UNIT jairp_unit[JAIR_PORT_UNITS] = {
    { UDATA (&jair_svc, UNIT_ATTABLE, 0), JAIR_STAT_WAIT },
    { UDATA (&jair_rx_svc, UNIT_DIS, 0), JAIR_IO_WAIT },
    { UDATA (&jair_tx_svc, UNIT_DIS, 0), JAIR_IO_WAIT }
};

static MTAB jairp_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "BAUD", "BAUD",
        &jair_set_baud, &jair_show_baud, NULL, "Set baud rate (default=9600)" },
    { 0 }
};

DEVICE jairp_dev = {
    JAIRP_SNAME,                                   /* name */
    jairp_unit,                                    /* unit */
    jairp_reg,                                     /* registers */
    jairp_mod,                                     /* modifiers */
    JAIR_PORT_UNITS,                               /* # units */
    10,                                            /* address radix */
    31,                                            /* address width */
    1,                                             /* addr increment */
    8,                                             /* data radix */
    8,                                             /* data width */
    NULL,                                          /* examine routine */
    NULL,                                          /* deposit routine */
    &jair_port_reset,                              /* reset routine */
    NULL,                                          /* boot routine */
    &jair_attach_mux,                              /* attach routine */
    &jair_detach_mux,                              /* detach routine */
    &jairp_ctx,                                    /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX), /* flags */
    ERROR_MSG,                                     /* debug control */
    jair_dt,                                       /* debug flags */
    NULL,                                          /* mem size routine */
    NULL,                                          /* logical name */
    NULL,                                          /* help */
    NULL,                                          /* attach help */
    &jairp_tmxr,                                   /* context for help */
    &jairp_description                             /* description */
};

static const char* jair_description(DEVICE *dptr) {
    return JAIR_NAME;
}

static const char* jairs0_description(DEVICE *dptr) {
    return JAIRS0_NAME;
}

static const char* jairs1_description(DEVICE *dptr) {
    return JAIRS1_NAME;
}

static const char* jairp_description(DEVICE *dptr) {
    return JAIRP_NAME;
}

/*
 * Reset function for the main JAIR device.
 *
 * Enables all other Sol-20 devices
 */
static t_stat jair_reset(DEVICE *dptr)
{
    static t_bool first = TRUE;

    if (dptr->flags & DEV_DIS) { /* Disconnect Resources */
        sim_map_resource(jair_ctx.io_base, jair_ctx.io_size, RESOURCE_TYPE_IO, &jairio, "jairio", TRUE);
        sim_map_resource(jair_ctx.rom_base, jair_ctx.rom_size, RESOURCE_TYPE_MEMORY, &jair_shadow_rom, "jairrom", TRUE);
    }
    else {
        /* Connect I/O Ports at base address */
        if (sim_map_resource(jair_ctx.io_base, jair_ctx.io_size, RESOURCE_TYPE_IO, &jairio, "jairio", FALSE) != 0) {
            sim_debug(ERROR_MSG, &jair_dev, "Error mapping I/O resource at 0x%02x\n", jair_ctx.io_base);
            return SCPE_ARG;
        }
        if (sim_map_resource(jair_ctx.rom_base, jair_ctx.rom_size, RESOURCE_TYPE_MEMORY, &jair_shadow_rom, "jairrom", FALSE) != 0) {
            sim_debug(ERROR_MSG, &jair_dev, "Error mapping ROM resource at 0x%02x\n", jair_ctx.io_base);
            return SCPE_ARG;
        }

        if (first) {
            set_dev_enbdis(&jairs0_dev, NULL, 1, NULL);
            set_dev_enbdis(&jairs1_dev, NULL, 1, NULL);
            set_dev_enbdis(&jairp_dev, NULL, 1, NULL);

            first = FALSE;
        }
    }

    jair_ctx.sr_ena = TRUE;
    jair_ctx.spi_cs = TRUE;
    jair_ctx.sd_appcmd = FALSE;
    jair_ctx.sd_istate = JAIR_STATE_IDLE;
    jair_ctx.sd_ostate = JAIR_STATE_IDLE;

    sim_debug(STATUS_MSG, &jair_dev, "reset controller.\n");

    return SCPE_OK;
}

static t_stat jair_port_reset(DEVICE *dptr) {
    JAIR_PORT_CTX *port;
    char uname[12];
    uint32 u;

    port = (JAIR_PORT_CTX *) dptr->ctxt;

    for (u = 0; u < dptr->numunits; u++) {
        dptr->units[u].dptr = dptr;
    }

    if (dptr->flags & DEV_DIS) { /* Disconnect I/O Port(s) */
        sim_map_resource(port->pnp.io_base, port->pnp.io_size, RESOURCE_TYPE_IO, &jairio, dptr->name, TRUE);
        for (u = 0; u < dptr->numunits; u++) {
            sim_cancel(&dptr->units[u]);  /* cancel timer */
        }
    }
    else {
        /* Connect I/O Ports at base address */
        if (sim_map_resource(port->pnp.io_base, port->pnp.io_size, RESOURCE_TYPE_IO, &jairio, dptr->name, FALSE) != 0) {
            sim_debug(ERROR_MSG, dptr, "Error mapping I/O resource at 0x%02x\n", port->pnp.io_base);
            return SCPE_ARG;
        }

        /* Enable TMXR modem control passthrough */
        tmxr_set_modem_control_passthru(port->tmxr);
        tmxr_set_port_speed_control(port->tmxr);
        tmxr_set_line_unit(port->tmxr, 0, &dptr->units[JAIR_UNIT_RX]);
        tmxr_set_line_output_unit(port->tmxr, 0, &dptr->units[JAIR_UNIT_TX]);

        sprintf(uname, "%.6sRX", sim_uname(&dptr->units[JAIR_UNIT_RX]));
        sim_set_uname(&dptr->units[JAIR_UNIT_RX], uname);
        sprintf(uname, "%.6sTX", sim_uname(&dptr->units[JAIR_UNIT_TX]));
        sim_set_uname(&dptr->units[JAIR_UNIT_TX], uname);

        port->status = 0x00;
        port->rdr = FALSE;
        port->txp = FALSE;
        port->lsr = JAIR_TEMT | JAIR_THRE;
        port->msr = 0;
        port->iobufin = 0;
        port->iobufout = 0;

        for (u = 0; u < dptr->numunits; u++) {
            sim_activate_abs(&dptr->units[u], dptr->units[u].wait);  /* activate timer */
        }
    }

    return SCPE_OK;
}

/*
 * The BOOT command will enter the ROM at 0x0000
 */
static t_stat jair_boot(int32 unitno, DEVICE *dptr)
{
    sim_printf("%s: Booting using ROM at 0x%04x\n", JAIR_SNAME, jair_ctx.rom_base);

    *((int32 *) sim_PC->loc) = jair_ctx.rom_base;

    return SCPE_OK;
}

/*
 * JAIR service routines
 *
 * The JAIR simulator has 3 I/O devices
 *
 * JAIRS0 - Keyboard device that supports TMXR
 * JAIRS1 - Serial device that supports TMXR
 * JAIRP - Parallel device that supports TMXR
 */
static t_stat jair_svc(UNIT *uptr)
{
    JAIR_PORT_CTX *port;
    t_stat r = SCPE_OK;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    /* Check for new incoming connection */
    if (uptr->dynflags & UNIT_TM_POLL && !port->conn && uptr->flags & UNIT_ATT) {
        if (tmxr_poll_conn(port->tmxr) >= 0) {      /* poll connection */

            port->conn = 1;          /* set connected   */

            sim_debug(VERBOSE_MSG, &jair_dev, "new connection.\n");
        }
    }

    /* Update modem status register */
    if ((uptr->flags & UNIT_ATT) && port->conn) {
        jair_get_modem_status(uptr);
    }

    sim_activate_abs(uptr, uptr->wait);  /* reactivate timer */

    return r;
}

static t_stat jair_rx_svc(UNIT *uptr)
{
    UNIT *rxunit = uptr;
    JAIR_PORT_CTX *port;
    int32 c = 0;
    t_stat r = SCPE_OK;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    /* switch to unit 0 */
    uptr = uptr->dptr->units;

    /* Buffer any received data */
    if ((uptr->flags & UNIT_ATT) && port->conn) {
        tmxr_poll_rx(port->tmxr);

        while ((c = tmxr_getc_ln(&port->tmln[0])) & TMXR_VALID) {
            port->iobuf[port->iobufin++] = c;
            port->iobufin &= JAIR_IOBUF_MASK;
            if (port->iobufin == port->iobufout) {
                port->iobufin--;
                port->iobufin &= JAIR_IOBUF_MASK;
                port->lsr |= JAIR_OE;     /* Overrun Error */
            }
        }
    }

    sim_activate_abs(rxunit, rxunit->wait);  /* reactivate timer */

    return r;
}

static t_stat jair_tx_svc(UNIT *uptr)
{
    UNIT *txunit = uptr;
    JAIR_PORT_CTX *port;
    t_stat r = SCPE_OK;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    /* switch to unit 0 */
    uptr = uptr->dptr->units;

    /* TX byte pending? */
    if (port->txp == TRUE) {
        if (uptr->flags & UNIT_ATT) {
            if (uptr->fileref) {
                r = (sim_fwrite(&port->txd, 1, 1, uptr->fileref) == 1) ? SCPE_OK : SCPE_IOERR;
                port->txp = FALSE;
                port->lsr |= (JAIR_TEMT | JAIR_THRE);
            } else if (port->conn) {
                if ((r = tmxr_putc_ln(&port->tmln[0], port->txd)) == SCPE_OK) {
                    tmxr_poll_tx(port->tmxr);
                    port->txp = FALSE;
                } else if (r == SCPE_LOST) {
                    port->conn = 0;          /* Connection was lost */
                    sim_printf("%s: lost connection.\n", uptr->dptr->name);
                } else {
                    sim_printf("%s: tmxr_putc_ln error %d.\n", uptr->dptr->name, r);
                }
            }
        }
        else {
            sim_putchar(port->txd);
            port->txp = FALSE;
            port->lsr |= (JAIR_TEMT | JAIR_THRE);
        }
    }

    /* Update LSR if no character pending */
    if (port->txp == FALSE && port->conn && !(port->lsr & (JAIR_TEMT | JAIR_THRE))) {
        port->lsr |= tmxr_txdone_ln(port->tmln) ? (JAIR_TEMT | JAIR_THRE) : 0;
    }

    sim_activate_abs(txunit, txunit->wait);  /* reactivate timer */

    return r;
}

/* Attach routines */
static t_stat jair_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if (r != SCPE_OK) {             /* error?       */
        sim_debug(ERROR_MSG, &jair_dev, "ATTACH error=%d\n", r);
        return r;
    }

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    for (i = 0; i < JAIR_UNITS; i++) {
        if (jair_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= JAIR_UNITS) {
        jair_detach(uptr);

        return SCPE_ARG;
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    sim_debug(VERBOSE_MSG, uptr->dptr, "unit %d, attached to '%s' size=%d\n",
        i, cptr, uptr->capac);

    return SCPE_OK;
}

/* Detach routines */
static t_stat jair_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < JAIR_UNITS; i++) {
        if (jair_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= JAIR_UNITS) {
        return SCPE_ARG;
    }

    r = detach_unit(uptr);  /* detach unit */

    if (r != SCPE_OK) {
        return r;
    }

    jair_dev.units[i].fileref = NULL;

    sim_debug(VERBOSE_MSG, uptr->dptr, "unit %d detached.\n", i);

    return SCPE_OK;
}

/*
 * Used to attach (connect) MUX interfaces from the
 * JAIRS0, JAIRS1, and JAIRP devices
 */
static t_stat jair_attach_mux(UNIT *uptr, CONST char *cptr)
{
    JAIR_PORT_CTX *xptr;
    t_stat r;

    xptr = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    if ((r = tmxr_attach(xptr->tmxr, uptr, cptr)) == SCPE_OK) {
        xptr->tmln[0].rcve = 1;
        sim_debug(VERBOSE_MSG, uptr->dptr, "attached '%s' to interface.\n", cptr);
        tmxr_set_get_modem_bits(xptr->tmln, TMXR_MDM_DTR | TMXR_MDM_RTS, 0, NULL);
    }

    return r;
}

/*
 * Used to detach (disconnect) MUX interfaces from the
 * JAIRS0, JAIRS1, and JAIRP devices
 */
static t_stat jair_detach_mux(UNIT *uptr)
{
    JAIR_PORT_CTX *xptr;
    t_stat r;

    xptr = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    r = tmxr_detach(xptr->tmxr, uptr);

    return r;
}

static t_stat jair_show_ports(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    JAIR_PORT_CTX *port;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    fprintf(st, "I/O=0x%02X-0x%02X", port->pnp.io_base, port->pnp.io_base + port->pnp.io_size - 1);
    return SCPE_OK;
}

static t_stat jair_config_line(DEVICE *dev, TMLN *tmln, int baud)
{
    char config[20];
    t_stat r = SCPE_IERR;

    sprintf(config, "%d-8N1", baud);

    if (tmln->serport) {
        r = tmxr_set_config_line(tmln, config);
    }

    sim_debug(STATUS_MSG, dev, "port configuration set to '%s'.\n", config);

    return r;
}

static void jair_get_rxdata(UNIT *uptr)
{
    JAIR_PORT_CTX *port;
    int32 c = 0xff;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    if (uptr->flags & UNIT_ATT) {
        if (uptr->fileref) {
            if (sim_fread(&c, 1, 1, uptr->fileref) == 1) {
                c |= SCPE_KFLAG;
            }
        }
        else if (port->conn) {
            if (port->iobufin != port->iobufout) {
                c = port->iobuf[port->iobufout++];
                port->iobufout &= JAIR_IOBUF_MASK;
            }
        }
    }
    else if (uptr->flags & UNIT_JAIR_CONSOLE) {
        c = sim_poll_kbd();
    }

    if (c & (TMXR_VALID | SCPE_KFLAG)) {
        port->rxd = c & 0xff;
        port->rdr = TRUE;
        port->lsr |= JAIR_DR;
    }
}

static int jair_get_modem_status(UNIT *uptr)
{
    JAIR_PORT_CTX *port;
    uint8 msr;
    int32 s;
    t_stat r;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    r = tmxr_set_get_modem_bits(port->tmln, 0, 0, &s);

    msr = port->msr;

    port->msr &= ~JAIR_CTS;
    port->msr |= (s & TMXR_MDM_CTS) ? JAIR_CTS : 0;

    /* CTS status changed */
    if ((msr ^ port->msr) & JAIR_CTS) {
        port->msr |= JAIR_DCTS;
        sim_debug(STATUS_MSG, uptr->dptr, "CTS state changed to %s.\n", (port->msr & JAIR_CTS) ? "HIGH" : "LOW");
    }

    port->msr &= ~JAIR_DSR;
    port->msr |= (s & TMXR_MDM_DSR) ? JAIR_DSR : 0;

    /* DSR status changed */
    if ((msr ^ port->msr) & JAIR_DSR) {
        port->msr |= JAIR_DDSR;
        sim_debug(STATUS_MSG, uptr->dptr, "DSR state changed to %s.\n", (port->msr & JAIR_DSR) ? "HIGH" : "LOW");
    }

    port->msr &= ~JAIR_RNG;
    port->msr |= (s & TMXR_MDM_RNG) ? JAIR_RNG : 0;

    /* RI status changed */
    if ((msr ^ port->msr) & JAIR_RNG) {
        port->msr |= (port->msr & JAIR_RNG) ? 0 : JAIR_DRNG; /* trailing edge to low */
        sim_debug(STATUS_MSG, uptr->dptr, "RNG state changed to %s.\n", (port->msr & JAIR_RNG) ? "HIGH" : "LOW");
    }

    port->msr &= ~JAIR_DCD;
    port->msr |= (s & TMXR_MDM_DCD) ? JAIR_DCD : 0;

    /* DCD status changed */
    if ((msr ^ port->msr) & JAIR_DCD) {
        port->msr |= JAIR_DDCD;
        sim_debug(STATUS_MSG, uptr->dptr, "DCD state changed to %s.\n", (port->msr & JAIR_DCD) ? "HIGH" : "LOW");
    }

    return r;
}

static int jair_set_mc(TMLN *tmln, uint8 data)
{
    int s = 0;

    s |= (data & JAIR_DTR) ? TMXR_MDM_DTR : 0;
    s |= (data & JAIR_RTS) ? TMXR_MDM_RTS : 0;

    return tmxr_set_get_modem_bits(tmln, s, ~s & (TMXR_MDM_DTR | TMXR_MDM_RTS), NULL);
}

static int jair_new_baud(UNIT *uptr)
{
    JAIR_PORT_CTX *port;
    int divisor;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    divisor = (port->dlms << 8) + port->dlls;

    if (divisor) {
        port->baud = 115200 / divisor;
        jair_config_line(uptr->dptr, port->tmln, port->baud);
    }

    return port->baud;
}

static t_stat jair_set_baud(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    int32 baud;
    t_stat r = SCPE_ARG;
    JAIR_PORT_CTX *port;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    if (!(uptr->flags & UNIT_ATT)) {
        return SCPE_UNATT;
    }

    if (cptr != NULL) {
        if (sscanf(cptr, "%d", &baud)) {
            port->baud = baud;
            r = jair_config_line(uptr->dptr, port->tmln, port->baud);
        }
    }

    return r;
}

static t_stat jair_show_baud(FILE *st, UNIT *uptr, int32 value, const void *desc)
{
    JAIR_PORT_CTX *port;

    port = (JAIR_PORT_CTX *) uptr->dptr->ctxt;

    if (uptr->flags & UNIT_ATT) {
        fprintf(st, "Baud rate: %d", port->baud);
    }

    return SCPE_OK;
}

/*
 * Handles I/O input and output
 */
static int32 jairio(int32 addr, int32 rw, int32 data)
{
    if (rw == 0) {
        return(jair_io_in(addr));
    }
    else {
        return(jair_io_out(addr, data));
    }
}

static uint8 jair_io_in(uint32 addr)
{
    uint8 data = 0xff;

    switch(addr & 0xff) {
        case JAIR_UART0 + JAIR_LSR:
            if (!(jairs0_ctx.lsr & JAIR_DR)) {
                jair_get_rxdata(jairs0_unit);
            }
            data = jairs0_ctx.lsr;
            break;

        case JAIR_UART1 + JAIR_LSR:
            if (!(jairs1_ctx.lsr & JAIR_DR)) {
                jair_get_rxdata(jairs1_unit);
            }
            data = jairs1_ctx.lsr;
            break;

        case JAIR_UART0 + JAIR_SDATA:
            data = jairs0_ctx.rxd;
            jairs0_ctx.rdr = FALSE;
            jairs0_ctx.lsr &= ~(JAIR_DR | JAIR_OE);
            break;

        case JAIR_UART1 + JAIR_SDATA:
            data = jairs1_ctx.rxd;
            jairs1_ctx.rdr = FALSE;
            jairs1_ctx.lsr &= ~(JAIR_DR | JAIR_OE);
            break;

        case JAIR_UART0 + JAIR_MSR:
            data = jairs0_ctx.msr;
            jairs0_ctx.msr &= 0xf0;    /* Clear deltas */
            break;

        case JAIR_UART1 + JAIR_MSR:
            data = jairs1_ctx.msr;
            jairs1_ctx.msr &= 0xf0;    /* Clear deltas */
            break;

        case JAIR_UART0 + JAIR_SR:
            data = jairs0_ctx.sr;
            break;

        case JAIR_UART1 + JAIR_SR:
            data = jairs1_ctx.sr;
            break;

        case JAIR_PPORT:
            data = jairp_ctx.rxd;
            jairp_ctx.rdr = FALSE;
            break;

        case JAIR_SPI:
            if (jair_ctx.spi_cs) {    /* Chip is disabled */
                break;
            }

            switch (jair_ctx.sd_istate) {
                case JAIR_STATE_IDLE:
                    data = 0xff;
                    break;

                case JAIR_STATE_RESP:
                    if (jair_ctx.sd_resp_idx < jair_ctx.sd_resp_len) {  /* Receive next command byte */
                        data = jair_ctx.sd_resp[jair_ctx.sd_resp_idx++];
                    }
                    else {
                        jair_ctx.sd_istate = JAIR_STATE_IDLE;
                    }
                    break;

                default:
                    break;
            }
            break;

        case JAIR_SPI_SS:
            break;

        default:
            sim_debug(ERROR_MSG, &jair_dev, "READ Invalid I/O Address %02x (%02x)\n",
                addr & 0xFF, addr & 0x01);
            break;
    }

    return (data);
}

static uint8 jair_io_out(uint32 addr, int32 data)
{
    uint32 sd_addr;

    switch(addr & 0xff) {
        case JAIR_UART0 + JAIR_SDATA:
            if (jairs0_ctx.lcr & JAIR_DLAB) {
                jairs0_ctx.dlls = data;
                jair_new_baud(jairs0_unit);
            } else {
                jairs0_ctx.txd = data;
                jairs0_ctx.txp = TRUE;
                jairs0_ctx.lsr &= ~(JAIR_THRE | JAIR_TEMT);
            }
            break;

        case JAIR_UART1 + JAIR_SDATA:
            if (jairs1_ctx.lcr & JAIR_DLAB) {
                jairs1_ctx.dlls = data;
                jair_new_baud(jairs1_unit);
            } else {
                jairs1_ctx.txd = data;
                jairs1_ctx.txp = TRUE;
                jairs1_ctx.lsr &= ~(JAIR_THRE | JAIR_TEMT);
            }
            break;

        case JAIR_UART0 + JAIR_IER:
            if (jairs0_ctx.lcr & JAIR_DLAB) {
                jairs0_ctx.dlms = data;
                jair_new_baud(jairs0_unit);
            } else {
                jairs0_ctx.ier = data;
            }
            break;

        case JAIR_UART1 + JAIR_IER:
            if (jairs1_ctx.lcr & JAIR_DLAB) {
                jairs1_ctx.dlms = data;
                jair_new_baud(jairs1_unit);
            } else {
                jairs1_ctx.ier = data;
            }
            break;

        case JAIR_UART0 + JAIR_SR:
            jairs0_ctx.sr = data;
            break;

        case JAIR_UART1 + JAIR_SR:
            jairs1_ctx.sr = data;
            break;

        case JAIR_UART0 + JAIR_LCR:
            jairs0_ctx.lcr = data;
            break;

        case JAIR_UART1 + JAIR_LCR:
            jairs1_ctx.lcr = data;
            break;

        case JAIR_UART0 + JAIR_MCR:
            jairs0_ctx.mcr = data;
            jair_set_mc(jairs0_ctx.tmln, data);
            break;

        case JAIR_UART1 + JAIR_MCR:
            jairs1_ctx.mcr = data;
            jair_set_mc(jairs1_ctx.tmln, data);
            break;

        case JAIR_PPORT:
            jairp_ctx.txd = data;
            jairp_ctx.txp = TRUE;

            jair_ctx.sr_ena = (data & 0x01) ? FALSE : TRUE;
            break;

        case JAIR_SPI_SS:
            jair_ctx.spi_cs = data & 0x01;

            if (jair_ctx.spi_cs) {    /* If chip is disable, reset */
                jair_ctx.sd_appcmd = FALSE;
                jair_ctx.sd_istate = JAIR_STATE_IDLE;
                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
            }
            break;

        case JAIR_SPI:
            if (jair_ctx.spi_cs) {    /* Chip is disabled */
                break;
            }

            switch (jair_ctx.sd_ostate) {
                case JAIR_STATE_IDLE:
                    if ((data & 0xc0) == 0x40) {   /* Received start and transmission bits */
                        jair_ctx.sd_cmd[0] = data & 0x3f;
                        jair_ctx.sd_cmd_len = 6;
                        jair_ctx.sd_cmd_idx = 1;
                        jair_ctx.sd_ostate = JAIR_STATE_CMD;
                    }  
                    break;

                case JAIR_STATE_CMD:
                    if (jair_ctx.sd_cmd_idx < jair_ctx.sd_cmd_len) {  /* Receive next command byte */
                        jair_ctx.sd_cmd[jair_ctx.sd_cmd_idx++] = data;
                    }
                    else {
                        jair_ctx.sd_resp[0] = (jair_ctx.sd_cmd[5] & 0x01) ? 0x00 : 0x04;

                        if (jair_ctx.sd_appcmd) {
                            jair_ctx.sd_cmd[0] |= 0x80;
                            jair_ctx.sd_appcmd = FALSE;
                        }

                        switch (jair_ctx.sd_cmd[0]) {
                            case JAIR_CMD0:
                                jair_ctx.sd_resp[0] |= 0x01;
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            case JAIR_CMD13:
                                jair_ctx.sd_resp[0] = 0x00;
                                jair_ctx.sd_resp[1] = 0x00;
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 2;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            case JAIR_CMD17:
                                sd_addr = jair_ctx.sd_cmd[1] * 0x1000000;
                                sd_addr |= jair_ctx.sd_cmd[2] * 0x10000;
                                sd_addr |= (uint32) jair_ctx.sd_cmd[3] * 0x100;
                                sd_addr |= (uint32) jair_ctx.sd_cmd[4];
                                if (!(jair_unit[0].flags & UNIT_ATT)) {
                                    jair_ctx.sd_resp[0] |= 0x04;
                                    jair_ctx.sd_resp_len = 1;
                                }
                                else if (sim_fseek(jair_unit[0].fileref, sd_addr, SEEK_SET) != 0) {
                                    jair_ctx.sd_resp[0] |= 0x04;
                                    jair_ctx.sd_resp_len = 1;
                                }
                                else if (sim_fread(&jair_ctx.sd_resp[4], 1, 512, jair_unit[0].fileref) != 512) {
                                    jair_ctx.sd_resp[0] |= 0x04;
                                    jair_ctx.sd_resp_len = 1;
                                } else {
                                    jair_ctx.sd_resp[0] |= 0x00;
                                    jair_ctx.sd_resp[1] |= 0xff;
                                    jair_ctx.sd_resp[2] |= 0xff;
                                    jair_ctx.sd_resp[3] |= 0xfe;
                                    jair_ctx.sd_resp_len = 4 + 512 + 2;
                                }
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            case JAIR_CMD24:
                                jair_ctx.sd_resp[0] |= 0x00;
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_SBLK;
                                break;

                            case JAIR_CMD8:
                                jair_ctx.sd_resp[0] |= 0x04;  /* Illegal command **/
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            case JAIR_CMD55:
                                jair_ctx.sd_appcmd = TRUE;
                                jair_ctx.sd_resp[0] |= 0x01;
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            case JAIR_ACMD41:
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                break;

                            default:
                                jair_ctx.sd_resp[0] |= 0x04;  /* Illegal command **/
                                jair_ctx.sd_resp_idx = 0;
                                jair_ctx.sd_resp_len = 1;
                                jair_ctx.sd_istate = JAIR_STATE_RESP;
                                jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                                sim_debug(ERROR_MSG, &jair_dev, "Command not implemented: %d\n", jair_ctx.sd_cmd[0]);
                                break;
                        }
                    }
                    break;

                case JAIR_STATE_SBLK:
                    if (data == 0xfe) {
                        jair_ctx.sd_ostate = JAIR_STATE_WBLK;
                        jair_ctx.sd_cmd_len = 512;
                        jair_ctx.sd_cmd_idx = 0;
                    }
                    break;

                case JAIR_STATE_WBLK:
                    if (jair_ctx.sd_cmd_idx < jair_ctx.sd_cmd_len) {
                        jair_ctx.sd_cmd[6 + jair_ctx.sd_cmd_idx++] = data;    /* store block starting at offset 6 */
                    }
                    else {
                        sd_addr = jair_ctx.sd_cmd[1] * 0x1000000;
                        sd_addr |= jair_ctx.sd_cmd[2] * 0x10000;
                        sd_addr |= (uint32) jair_ctx.sd_cmd[3] * 0x100;
                        sd_addr |= (uint32) jair_ctx.sd_cmd[4];

                        if (!(jair_unit[0].flags & UNIT_ATT)) {
                            jair_ctx.sd_resp[0] = 0x0b;
                        }
                        else if (sim_fseek(jair_unit[0].fileref, sd_addr, SEEK_SET) != 0) {
                            jair_ctx.sd_resp[0] = 0x0b;
                        }
                        else if (sim_fwrite(&jair_ctx.sd_cmd[6], 1, 512, jair_unit[0].fileref) != 512) {
                            jair_ctx.sd_resp[0] = 0x0b;
                        } else {
                            jair_ctx.sd_resp[0] = 0x05;
                        }

                        jair_ctx.sd_resp_idx = 0;
                        jair_ctx.sd_resp_len = 1;
                        jair_ctx.sd_istate = JAIR_STATE_RESP;
                        jair_ctx.sd_ostate = JAIR_STATE_IDLE;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            sim_debug(ERROR_MSG, &jair_dev, "WRITE Invalid I/O Address %02x (%02x)\n",
                addr & 0xFF, addr & 0x01);
            break;
    }

    return(0xff);
}

static t_stat jair_set_rom(UNIT *uptr, int32 value, CONST char *cptr, void *desc) {
    jair_ctx.sr_ena = TRUE;
    return SCPE_OK;
}

static t_stat jair_set_norom(UNIT *uptr, int32 value, CONST char *cptr, void *desc) {
    jair_ctx.sr_ena = FALSE;
    return SCPE_OK;
}

/*
** The JAIR overlays the first 8K (minimum) of RAM with a ROM.
**
** If the ROM is enabled, writes to 0x0000-0x2000 are written to RAM, reads are
** read from the ROM.
**
** The ROM is enabled/disabled by writing a 0 (enable) or 1 (disable) to the parallel port.
*/
static int32 jair_shadow_rom(int32 Addr, int32 rw, int32 data)
{
    if (rw == JAIR_ROM_WRITE) {
        jair_ram[Addr & JAIR_ROM_MASK] = data;
    } else {
        if (jair_ctx.sr_ena == TRUE && Addr < JAIR_ROM_SIZE) {
            return(jair_rom_v25[Addr & JAIR_ROM_MASK]);
        } else {
            return(jair_ram[Addr & JAIR_ROM_MASK]);
        }
    }

    return 0xff;
}

/*
 * Display help
 */
static t_stat jair_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    return SCPE_OK;
}

