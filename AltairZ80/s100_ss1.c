/*************************************************************************
 *                                                                       *
 * $Id: s100_ss1.c 1997 2008-07-18 05:29:52Z hharte $                    *
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

#include <time.h>

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define TRACE_MSG   (1 << 1)
#define PIC_MSG     (1 << 2)
#define TC_MSG      (1 << 3)
#define RTC_MSG     (1 << 4)
#define MATH_MSG    (1 << 5)
#define UART_MSG    (1 << 6)
#define IRQ_MSG     (1 << 7)

#define SS1_MAX_TIMERS    3

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} SS1_INFO;

static SS1_INFO ss1_info_data = { { 0x0, 0, 0x50, 16 } };

extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern uint32 PCX;

static t_stat ss1_reset(DEVICE *ss1_dev);
static t_stat ss1_svc (UNIT *uptr);
static uint8 SS1_Read(const uint32 Addr);
static uint8 SS1_Write(const uint32 Addr, uint8 cData);
static int32 ss1dev(const int32 port, const int32 io, const int32 data);
void raise_ss1_interrupt(uint8 isr_index);

/* SS1 Interrupt Controller notes:
 *
 * Msster 8259:
 * IRQ0 = VI0
 * IRQ1 = VI1       - DISK3 Interrupt
 * IRQ2 = VI2       - IF3 Rx Interrupt
 * IRQ3 = VI3       - IF3 Tx Interrupt
 * IRQ4 = VI4       - DISK1A
 * IRQ5 = VI5       - ?
 * IRQ6 = VI6
 * <Cascade>
 *
 * Slave 8259:
 * IRQ0 = VI7           0x48
 * IRQ1 = Timer0        0x49
 * IRQ2 = Timer1        0x4A
 * IRQ3 = Timer2        0x4B
 * IRQ4 = 9511 SVRQ     0x4C
 * IRQ5 = 9511 END      0x4D
 * IRQ6 = 2651 TxRDY    0x4E
 * IRQ7 = 2651 RxRDY    0x4F
 */
#define MASTER_PIC  0
#define SLAVE_PIC   1

#define VI0_IRQ_OFFSET      0
#define VI1_IRQ_OFFSET      1
#define VI2_IRQ_OFFSET      2
#define VI3_IRQ_OFFSET      3
#define VI4_IRQ_OFFSET      4
#define VI5_IRQ_OFFSET      5
#define VI6_IRQ_OFFSET      6
#define VI7_IRQ_OFFSET      0
#define TC0_IRQ_OFFSET      1
#define TC1_IRQ_OFFSET      2
#define TC2_IRQ_OFFSET      3
#define MSVRQ_IRQ_OFFSET    4
#define MEND_IRQ_OFFSET     5
#define TXRDY_IRQ_OFFSET    6
#define RXRDY_IRQ_OFFSET    7

typedef struct {
    uint8 config_cnt;
    uint8 ICW[5];
    uint8 IMR;      /* OCW1 = IMR */
    uint8 OCW2;
    uint8 OCW3;
    uint8 IRR;
    uint8 ISR;
} I8259_REGS;

I8259_REGS ss1_pic[2];

/* SS1 Timer notes:
 *
 * T0, T1, T2 inputs connected to 2MHz clock on SS1
 * T0 IRQ connected to Slave IRQ 1
 * T1 IRQ connected to Slave IRQ 2
 * T2 IRQ connected to Slave IRQ 3
 */
typedef struct {
    uint16 count[3];    /* Current counter value for each timer. */
    uint8 mode[3];      /* Current mode of each timer. */
    uint8 bcd[3];
    uint8 rl[3];
    uint8 CTL;
} I8253_REGS;

I8253_REGS ss1_tc[1];

#define I8253_CTL_SC_MASK   0xC0
#define I8253_CTL_RL_MASK   0x30
#define I8253_CTL_MODE_MASK 0x0E
#define I8253_CTL_BCD       0x01

typedef struct {
    uint8 digit_sel;
    uint8 flags;
} RTC_REGS;

RTC_REGS ss1_rtc[1];

static UNIT ss1_unit[] = {
    { UDATA (&ss1_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) },
    { UDATA (&ss1_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) },
    { UDATA (&ss1_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) },
    { UDATA (&ss1_svc, UNIT_FIX | UNIT_DISABLE | UNIT_ROABLE, 0) }
};

static REG ss1_reg[] = {
    { HRDATAD (MPIC_IMR,   ss1_pic[MASTER_PIC].IMR,            8, "Master IMR register"),       },
    { HRDATAD (MPIC_IRR,   ss1_pic[MASTER_PIC].IRR,            8, "Master IRR register"),       },
    { HRDATAD (MPIC_ISR,   ss1_pic[MASTER_PIC].ISR,            8, "Master ISR register"),       },
    { HRDATAD (MPIC_OCW2,  ss1_pic[MASTER_PIC].OCW2,           8, "Master OCW2 register"),      },
    { HRDATAD (MPIC_OCW3,  ss1_pic[MASTER_PIC].OCW3,           8, "Master OCW3 register"),      },

    { HRDATAD (SPIC_IMR,   ss1_pic[SLAVE_PIC].IMR,             8, "Slave IMR register"),        },
    { HRDATAD (SPIC_IRR,   ss1_pic[SLAVE_PIC].IRR,             8, "Slave IRR register"),        },
    { HRDATAD (SPIC_ISR,   ss1_pic[SLAVE_PIC].ISR,             8, "Slave ISR register"),        },
    { HRDATAD (SPIC_OCW2,  ss1_pic[SLAVE_PIC].OCW2,            8, "Slave OCW2 register"),       },
    { HRDATAD (SPIC_OCW3,  ss1_pic[SLAVE_PIC].OCW3,            8, "Slave OCW3 register"),       },

    { HRDATAD (T0_MODE,    ss1_tc[0].mode,                     3, "Timer 0 mode register"),     },
    { HRDATAD (T0_COUNT,   ss1_tc[0].count,                    16, "Timer 0 count register"),   },
    { HRDATAD (T1_MODE,    ss1_tc[1].mode,                     3, "Timer 1 mode register"),     },
    { HRDATAD (T1_COUNT,   ss1_tc[1].count,                    16, "Timer 1 count register"),   },
    { HRDATAD (T2_MODE,    ss1_tc[2].mode,                     3, "Timer 2 mode register"),     },
    { HRDATAD (T2_COUNT,   ss1_tc[2].count,                    16, "Timer 2 count register"),   },

    { HRDATAD (RTC_DIGIT,  ss1_rtc[0].digit_sel,               4, "Digit selector register"),   },
    { HRDATAD (RTC_FLAGS,  ss1_rtc[0].flags,                   4, "Flags register"),            },

    { NULL }
};

static MTAB ss1_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,              "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets system support module base address" },
    { 0 }
};

/* Debug Flags */
static DEBTAB ss1_dt[] = {
    { "ERROR",  ERROR_MSG,  "Error messages"    },
    { "TRACE",  TRACE_MSG,  "Trace messages"    },
    { "PIC",    PIC_MSG,    "PIC messages"      },
    { "TC",     TC_MSG,     "TC messages"       },
    { "RTC",    RTC_MSG,    "RTC messages"      },
    { "MATH",   MATH_MSG,   "Math messages"     },
    { "UART",   UART_MSG,   "UART messages"     },
    { "IRQ",    IRQ_MSG,    "IRQ messages"      },
    { NULL,     0                               }
};

DEVICE ss1_dev = {
    "SS1", ss1_unit, ss1_reg, ss1_mod,
    SS1_MAX_TIMERS, 10, 31, 1, SS1_MAX_TIMERS, SS1_MAX_TIMERS,
    NULL, NULL, &ss1_reset,
    NULL, NULL, NULL,
    &ss1_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    ss1_dt, NULL, "Compupro System Support 1 SS1"
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
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        } else {
            DBG_PRINT(("SS1: Mapped I/O resource at 0x%04x, len=%d\n", pnp->io_base, pnp->io_size));
            ss1_unit[0].u4 = 0;
            ss1_unit[1].u4 = 1;
            ss1_unit[2].u4 = 2;
            ss1_unit[3].u4 = 3;
            ss1_pic[MASTER_PIC].IMR = 0xFF;
            ss1_pic[SLAVE_PIC].IMR = 0xFF;
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

static struct tm currentTime;
static int32 toBCD(const int32 x);

static uint8 SS1_Read(const uint32 Addr)
{
    uint8 cData = 0x00;

    uint8 sel_pic = MASTER_PIC;
    uint8 sel_tc = 0;
    time_t now;

    switch(Addr & 0x0F) {
        case SS1_S8259_L:
            sel_pic = SLAVE_PIC;
        case SS1_M8259_L:
            if((ss1_pic[sel_pic].OCW3 & 0x03) == 0x03) {
                cData = ss1_pic[sel_pic].ISR;
                sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                          " RD: %s PIC ISR=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
            } else if((ss1_pic[sel_pic].OCW3 & 0x03) == 0x02) {
                cData = ss1_pic[sel_pic].IRR;
                sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                          " RD: %s PIC IRR=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
            } else {
                cData = 0xFF;
            }
            break;
        case SS1_S8259_H:
            sel_pic = SLAVE_PIC;
        case SS1_M8259_H:
            cData = ss1_pic[sel_pic].IMR;
            sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: %s PIC IMR=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
            ss1_pic[sel_pic].IMR = cData;
            break;
        case SS1_8253_CTL:
            cData = ss1_tc[0].CTL;
            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: TC CTL=0x%02x.\n", PCX, cData);
            break;
        case SS1_8253_TC2:
            sel_tc++;
        case SS1_8253_TC1:
            sel_tc++;
        case SS1_8253_TC0:
            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: TC [%d]=0x%02x.\n", PCX, sel_tc, cData);
            break;
        case SS1_9511A_DATA:
        case SS1_9511A_CMD:
            sim_debug(MATH_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: Math Coprocessor not Implemented.\n", PCX);
            break;
        case SS1_RTC_CMD:
            cData = 0xFF;
            sim_debug(RTC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: RTC  Cmd=0x%02x.\n", PCX, cData);
            break;
        case SS1_RTC_DATA:
                time(&now);
                currentTime = *localtime(&now);

            switch(ss1_rtc[0].digit_sel) {
            case 0:
                cData = toBCD(currentTime.tm_sec) & 0xF;
                break;
            case 1:
                cData = (toBCD(currentTime.tm_sec) >> 4) & 0xF;
                break;
            case 2:
                cData = toBCD(currentTime.tm_min) & 0xF;
                break;
            case 3:
                cData = (toBCD(currentTime.tm_min) >> 4) & 0xF;
                break;
            case 4:
                cData = toBCD(currentTime.tm_hour) & 0xF;
                break;
            case 5:
                cData = (toBCD(currentTime.tm_hour) >> 4) & 0x3;
                cData |= 0x08;  /* Set to 24-hour format */
                break;
            case 6:
                cData = toBCD(currentTime.tm_wday) & 0xF;
                break;
            case 7:
                cData = toBCD(currentTime.tm_mday) & 0xF;
                break;
            case 8:
                cData = (toBCD(currentTime.tm_mday) >> 4) & 0xF;
                break;
            case 9:
                cData = toBCD(currentTime.tm_mon+1) & 0xF;
                break;
            case 10:
                cData = (toBCD(currentTime.tm_mon+1) >> 4) & 0xF;
                break;
            case 11:
                cData = toBCD(currentTime.tm_year-22) & 0xF;
                break;
            case 12:
                cData = (toBCD(currentTime.tm_year-22) >> 4) & 0xF;
                break;
            default:
                cData = 0;
                break;
            }
            sim_debug(RTC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: RTC Data[%x]=0x%02x.\n", PCX, ss1_rtc[0].digit_sel, cData);

            break;
        case SS1_UART_DATA:
            cData = sio0d(Addr, 0, 0);
            sim_debug(UART_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: UART Data=0x%02x.\n", PCX, cData);
            break;
        case SS1_UART_STAT:
            cData = sio0s(Addr, 0, 0);
            sim_debug(UART_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: UART Stat=0x%02x.\n", PCX, cData);
            break;
        case SS1_UART_MODE:
        case SS1_UART_CMD:
            sim_debug(UART_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " RD: UART not Implemented.\n", PCX);
            break;
    }

    return (cData);

}

uint16 newcount = 0;
uint8 bc;

static void generate_ss1_interrupt(void);

static uint8 SS1_Write(const uint32 Addr, uint8 cData)
{

    uint8 sel_pic = MASTER_PIC;
    uint8 sel_tc = 0;
    uint8 sel_timer = 0;

    switch(Addr & 0x0F) {
        case SS1_S8259_L:
            sel_pic = SLAVE_PIC;
        case SS1_M8259_L:
            if(cData & 0x10) {
                sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                          " WR: %s PIC ICW1=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
                ss1_pic[sel_pic].ICW[1] = cData;
                ss1_pic[sel_pic].config_cnt=1;
            } else {
                if(cData & 0x08) { /* OCW3 */
                    sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                              " WR: %s PIC OCW3=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
                    ss1_pic[sel_pic].OCW3 = cData;
                } else { /* OCW2 */
                    sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                              " WR: %s PIC OCW2=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
                    ss1_pic[sel_pic].OCW2 = cData;
                }
            }
            break;
        case SS1_S8259_H:
            sel_pic = SLAVE_PIC;
        case SS1_M8259_H:
            if(ss1_pic[sel_pic].config_cnt == 0) {
                sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT " WR: %s PIC IMR=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), cData);
                ss1_pic[sel_pic].IMR = cData;
                generate_ss1_interrupt();
            } else {
                ss1_pic[sel_pic].config_cnt++;
                sim_debug(PIC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT " WR: %s PIC ICW%d=0x%02x.\n", PCX, (sel_pic ? "Slave " : "Master"), ss1_pic[sel_pic].config_cnt, cData);
                ss1_pic[sel_pic].ICW[ss1_pic[sel_pic].config_cnt] = cData;

                ss1_unit[0].u3 = ss1_pic[SLAVE_PIC].ICW[2]+TC0_IRQ_OFFSET;
                ss1_unit[1].u3 = ss1_pic[SLAVE_PIC].ICW[2]+TC1_IRQ_OFFSET;
                ss1_unit[2].u3 = ss1_pic[SLAVE_PIC].ICW[2]+TC2_IRQ_OFFSET;

                if(ss1_pic[sel_pic].config_cnt == 4) {
                    ss1_pic[sel_pic].config_cnt = 0;
                }
            }
            break;
        case SS1_8253_CTL:
            ss1_tc[0].CTL = cData;
            sel_timer = (ss1_tc[0].CTL & I8253_CTL_SC_MASK) >> 6;
            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: TC CTL=0x%02x.\n",
                      PCX, ss1_tc[0].CTL);
            if(ss1_tc[0].CTL & I8253_CTL_BCD) {
                sim_debug(ERROR_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                          " Timer %d: BCD Mode not supported: TC CTL=0x%02x.\n",
                          PCX, sel_timer, ss1_tc[0].CTL);
            }
            ss1_tc[0].bcd[sel_timer] = (ss1_tc[0].CTL & I8253_CTL_BCD);
            ss1_tc[0].mode[sel_timer] = (ss1_tc[0].CTL & I8253_CTL_MODE_MASK) >> 1;
            ss1_tc[0].rl[sel_timer] = (ss1_tc[0].CTL & I8253_CTL_RL_MASK) >> 4;
            sim_debug(TRACE_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " Timer %d: Mode: %d, RL=%d, %s.\n",
                      PCX, sel_timer, ss1_tc[0].mode[sel_timer],
                      ss1_tc[0].rl[sel_timer],
                      ss1_tc[0].bcd[sel_timer] ? "BCD" : "Binary");
            newcount = 0;
            bc=0;
            break;
        case SS1_8253_TC2:
            sel_tc++;
        case SS1_8253_TC1:
            sel_tc++;
        case SS1_8253_TC0:
            if(ss1_tc[0].rl[sel_timer] == 3) {
                if(bc==0) {
                    newcount = cData;
                }
                if(bc==1) {
                    newcount |= (cData << 8);
                    sim_activate(&ss1_unit[sel_tc], newcount);
                }
                bc++;
            }

            if(ss1_tc[0].rl[sel_timer] == 2) {
                newcount = (cData << 8);
                sim_activate(&ss1_unit[sel_tc], newcount);
            }

            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: TC [%d]=0x%02x.\n", PCX, sel_tc, cData);
            if(sel_tc == 0) {
            }
            break;
        case SS1_9511A_DATA:
        case SS1_9511A_CMD:
            sim_debug(TRACE_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: Math Coprocessor not Implemented.\n", PCX);
            break;
        case SS1_RTC_CMD:
            ss1_rtc[0].digit_sel = cData & 0x0F;
            ss1_rtc[0].flags = (cData >> 4) & 0x0F;
            sim_debug(RTC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: RTC  Cmd=0x%02x (%s%s%s SEL=%x)\n",
                      PCX, cData,
                      ss1_rtc[0].flags & 0x4 ? "HOLD " :"",
                      ss1_rtc[0].flags & 0x2 ? "WR" :"",
                      ss1_rtc[0].flags & 0x1 ? "RD" :"",
                      ss1_rtc[0].digit_sel);
            break;
        case SS1_RTC_DATA:
            sim_debug(RTC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: RTC Data=0x%02x\n", PCX, cData);
            break;
        case SS1_UART_DATA:
            sim_debug(UART_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: UART Data=0x%02x.\n", PCX, cData);
            sio0d(Addr, 1, cData);
            break;
        case SS1_UART_STAT:
            sim_debug(UART_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: UART Stat=0x%02x.\n", PCX, cData);
            sio0s(Addr, 1, cData);
            break;
        case SS1_UART_MODE:
        case SS1_UART_CMD:
            sim_debug(TRACE_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT
                      " WR: UART not Implemented.\n", PCX);
            break;
    }

    return(0);
}

void raise_ss1_interrupt(uint8 isr_index)
{
    uint8 irq_bit;
    if(isr_index < 7) { /* VI0-6 on master PIC */
        irq_bit = (1 << isr_index);
        ss1_pic[MASTER_PIC].ISR |= irq_bit;
        generate_ss1_interrupt();
    } else { /* VI7 is on slave PIC */
        ss1_pic[SLAVE_PIC].ISR |= 1;
        generate_ss1_interrupt();
    }
}
extern void cpu_raise_interrupt(uint32 irq);

static void generate_ss1_interrupt(void)
{
    uint8 irq, irq_pend, irq_index = 0, irq_bit = 0;

    uint8 pic;

    for(pic=MASTER_PIC;pic<=SLAVE_PIC;pic++) {
        irq_pend = (~ss1_pic[pic].IMR) & ss1_pic[pic].ISR;

        while(irq_pend) {

            irq_bit = irq_pend & 1;
            if(irq_bit) {
                ss1_pic[pic].IRR |= (irq_bit << irq_index);
                irq = ss1_pic[pic].ICW[2]+irq_index;
                sim_debug(IRQ_MSG, &ss1_dev, "Handling interrupt on %s PIC: IMR=0x%02x, ISR=0x%02x, IRR=0x%02x, index=%d\n", pic ? "SLAVE" : "MASTER", ss1_pic[pic].IMR, ss1_pic[pic].ISR, ss1_pic[pic].IRR, irq_index);
                cpu_raise_interrupt(irq);
                ss1_pic[pic].IRR &= ~(irq_bit << irq_index);
                ss1_pic[pic].ISR &= ~(irq_bit << irq_index);
                if(irq_pend & 0x7E) {
/*              sim_debug(IRQ_MSG, &ss1_dev, "Requeue interrupt on %s PIC: IMR=0x%02x, ISR=0x%02x, IRR=0x%02x, index=%d\n", pic ? "SLAVE" : "MASTER", ss1_pic[pic].IMR, ss1_pic[pic].ISR, ss1_pic[pic].IRR, irq_index);
*/
                    sim_activate(&ss1_unit[3], 1000);  /* requeue, because more interrupts are pending. */
                }
                break;
            } else {
                irq_index++;
                irq_pend = irq_pend >> 1;
            }
        }
    }
}


/* Unit service routine */
/* Unit 0-2 = Timer0-2, Unit3=ISR queue */
static t_stat ss1_svc (UNIT *uptr)
{
    uint8 cData;
    uint8 irq_bit = 0;

    /* Handle SS1 UART Rx interrupts here. */
    cData = sio0s(0x5D, 0, 0);
    if(cData & 2) { /* && ((ss1_pic[SLAVE_PIC].IMR & 0x80) == 0)) { */
        ss1_pic[SLAVE_PIC].ISR |= 0x80;
        generate_ss1_interrupt();
        sim_activate(uptr, 1000);  /* requeue, because we still need to handle the timer interrupt. */
    } else if((cData & 1) && ((ss1_pic[SLAVE_PIC].IMR & 0x40) == 0)) {
        sim_debug(IRQ_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT " Calling UART Tx ISR.\n", PCX);
        ss1_pic[SLAVE_PIC].ISR |= 0x40;
        generate_ss1_interrupt();
        sim_activate(uptr, 1000);   /* requeue, because we still need to handle the timer interrupt. */
    } else if (uptr->u4 == 0x3) {   /* ISR was requeued because additional interrupts were pending. */
        generate_ss1_interrupt();
    } else {
        switch(uptr->u4) {
            case 0:
                irq_bit = 2;
                break;
            case 1:
                irq_bit = 4;
                break;
            case 2:
                irq_bit = 8;
                break;
        }
        if(ss1_tc[0].mode[uptr->u4] == 0x0) {
            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT " Calling Timer%d ISR.\n", PCX, uptr->u4);
            ss1_pic[SLAVE_PIC].ISR |= irq_bit;
            generate_ss1_interrupt();
        }
        if(ss1_tc[0].mode[uptr->u4] == 0x3) {
            sim_debug(TC_MSG, &ss1_dev, "SS1: " ADDRESS_FORMAT " Calling Timer%d ISR.\n", PCX, uptr->u4);
            ss1_pic[SLAVE_PIC].ISR |= irq_bit;
            generate_ss1_interrupt();
            sim_debug(TC_MSG, &ss1_dev, "Timer %d, mode %d, reloading\n", uptr->u4, ss1_tc[0].mode[uptr->u4]);
                sim_activate(uptr, 33280);
        }
    }

    sim_activate(&ss1_unit[3], 1000000);  // requeue, because more interrupts are pending.

    return SCPE_OK;
}

static int32 toBCD(const int32 x) {
    return (x / 10) * 16 + (x % 10);
}

