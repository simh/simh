/*************************************************************************
 *                                                                       *
 * $Id: s100_ss1.c 1773 2008-01-11 05:46:19Z hharte $                    *
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
 *     CompuPro System Support 1 module for SIMH.                        *
 * Note this does not include the Boot ROM on the System Support 1 Card  *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#ifdef DBG_MSG
#define DBG_PRINT(args) printf args
#else
#define DBG_PRINT(args)
#endif

#define TRACE_MSG   0x01
#define DMA_MSG     0x02

#define SS1_MAX_DRIVES    1

#define UNIT_V_SS1_VERBOSE     (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_SS1_VERBOSE       (1 << UNIT_V_SS1_VERBOSE)

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} SS1_INFO;

static SS1_INFO ss1_info_data = { { 0x0, 0, 0x50, 12 } };
/* static SS1_INFO *ss1_info = &ss1_info_data;*/

extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint32 PCX;
extern REG *sim_PC;

/* These are needed for DMA.  PIO Mode has not been implemented yet. */
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);

static t_stat ss1_reset(DEVICE *ss1_dev);
static uint8 SS1_Read(const uint32 Addr);
static uint8 SS1_Write(const uint32 Addr, uint8 cData);


static int32 ss1dev(const int32 port, const int32 io, const int32 data);

static int32 trace_level    = 0x00;     /* Disable all tracing by default */

static UNIT ss1_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ROABLE, 0) }
};

static REG ss1_reg[] = {
    { HRDATA (TRACELEVEL,   trace_level,           16), },
    { NULL }
};

static MTAB ss1_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,              "IOBASE",   "IOBASE",   &set_iobase, &show_iobase, NULL },
    /* quiet, no warning messages       */
    { UNIT_SS1_VERBOSE, 0,                  "QUIET",    "QUIET",    NULL },
    /* verbose, show warning messages   */
    { UNIT_SS1_VERBOSE, UNIT_SS1_VERBOSE,   "VERBOSE",  "VERBOSE",  NULL },
    { 0 }
};

DEVICE ss1_dev = {
    "SS1", ss1_unit, ss1_reg, ss1_mod,
    SS1_MAX_DRIVES, 10, 31, 1, SS1_MAX_DRIVES, SS1_MAX_DRIVES,
    NULL, NULL, &ss1_reset,
    NULL, NULL, NULL,
    &ss1_info_data, (DEV_DISABLE | DEV_DIS), 0,
    NULL, NULL, NULL
};

/* Reset routine */
static t_stat ss1_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ss1dev, TRUE);
    } else {
        /* Connect SS1 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ss1dev, FALSE) != 0) {
            printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

static int32 ss1dev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("SS1: IO %s, Port %02x\n", io ? "WR" : "RD", port));
    if(io) {
        SS1_Write(port, data);
        return 0;
    } else {
        return(SS1_Read(port));
    }
}

#define SS1_M8259_L     0x00
#define SS1_M8259_H     0x01
#define SS1_S8259_L     0x02
#define SS1_S8259_H     0x03
#define SS1_8253_TC0    0x04
#define SS1_8253_TC1    0x05
#define SS1_8253_TC2    0x06
#define SS1_8253_CTL    0x07
#define SS1_9511A_DATA  0x08
#define SS1_9511A_CMD   0x09
#define SS1_RTC_CMD     0x0A
#define SS1_RTC_DATA    0x0B
#define SS1_UART_DATA   0x0C
#define SS1_UART_STAT   0x0D
#define SS1_UART_MODE   0x0E
#define SS1_UART_CMD    0x0F

extern int32 sio0d(const int32 port, const int32 io, const int32 data);
extern int32 sio0s(const int32 port, const int32 io, const int32 data);

static uint8 SS1_Read(const uint32 Addr)
{
    uint8 cData = 0x00;

    switch(Addr & 0x0F) {
        case SS1_M8259_L:
        case SS1_M8259_H:
        case SS1_S8259_L:
        case SS1_S8259_H:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " RD: Interrupt Controller not Implemented." NLP, PCX));
            break;
        case SS1_8253_TC0:
        case SS1_8253_TC1:
        case SS1_8253_TC2:
        case SS1_8253_CTL:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " RD: Timer not Implemented." NLP, PCX));
            break;
        case SS1_9511A_DATA:
        case SS1_9511A_CMD:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " RD: Math Coprocessor not Implemented." NLP, PCX));
            break;
        case SS1_RTC_CMD:
        case SS1_RTC_DATA:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " RD: RTC not Implemented." NLP, PCX));
            break;
        case SS1_UART_DATA:
            cData = sio0d(Addr, 0, 0);
            break;
        case SS1_UART_STAT:
            cData = sio0s(Addr, 0, 0);
            break;
        case SS1_UART_MODE:
        case SS1_UART_CMD:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " RD: UART not Implemented." NLP, PCX));
            break;
    }

    return (cData);

}

static uint8 SS1_Write(const uint32 Addr, uint8 cData)
{

    switch(Addr & 0x0F) {
        case SS1_M8259_L:
        case SS1_M8259_H:
        case SS1_S8259_L:
        case SS1_S8259_H:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " WR: Interrupt Controller not Implemented." NLP, PCX));
            break;
        case SS1_8253_TC0:
        case SS1_8253_TC1:
        case SS1_8253_TC2:
        case SS1_8253_CTL:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " WR: Timer not Implemented." NLP, PCX));
            break;
        case SS1_9511A_DATA:
        case SS1_9511A_CMD:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " WR: Math Coprocessor not Implemented." NLP, PCX));
            break;
        case SS1_RTC_CMD:
        case SS1_RTC_DATA:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " WR: RTC not Implemented." NLP, PCX));
            break;
        case SS1_UART_DATA:
            sio0d(Addr, 1, cData);
            break;
        case SS1_UART_STAT:
            sio0s(Addr, 1, cData);
            break;
        case SS1_UART_MODE:
        case SS1_UART_CMD:
            TRACE_PRINT(TRACE_MSG, ("SS1: " ADDRESS_FORMAT " WR: UART not Implemented." NLP, PCX));
            break;
    }

    return(0);
}

