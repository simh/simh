/* 3b2_iu.c:  SCN2681A Dual UART Implementation

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#include "3b2_iu.h"

/*
 * The 3B2/400 has two on-board serial ports, labeled CONSOLE and
 * CONTTY. The CONSOLE port is (naturally) the system console.  The
 * CONTTY port serves as a secondary serial port for an additional
 * terminal.
 *
 * These lines are driven by an SCN2681A Dual UART, with two receivers
 * and two transmitters.
 *
 * In addition to the two TX/RX ports, the SCN27681A also has one
 * programmable timer.
 *
 * The SCN2681A UART is represented here by five devices:
 *
 *   - Console TTI (Input, port A)
 *   - Console TTO (Output, port A)
 *   - Contty  TTI (Input, port B)
 *   - Contty  TTO (Output, port B)
 *   - IU Timer
 */


/*
 * Registers
 */

/* The IU state shared between A and B */
IU_STATE iu_state;

/* The tx/rx state for ports A and B */
IU_PORT iu_port_a;
IU_PORT iu_port_b;

/* The timer state */
IU_TIMER_STATE iu_timer_state;

/* Flags for incrementing mode pointers */
t_bool iu_increment_a = FALSE;
t_bool iu_increment_b = FALSE;

extern uint16 csr_data;

BITFIELD sr_bits[] = {
    BIT(RXRDY),
    BIT(FFULL),
    BIT(TXRDY),
    BIT(TXEMT),
    BIT(OVRN_E),
    BIT(PRTY_E),
    BIT(FRM_E),
    BIT(BRK),
    ENDBITS
};

BITFIELD isr_bits[] = {
    BIT(TXRDYA),
    BIT(RXRDY_FFA),
    BIT(DLTA_BRKA),
    BIT(CTR_RDY),
    BIT(TXRDYB),
    BIT(RXRDY_FFB),
    BIT(DLTA_BRKB),
    BIT(IPC),
    ENDBITS
};

BITFIELD acr_bits[] = {
    BIT(BRG_SET),
    BITFFMT(TMR_MODE,3,%d),
    BIT(DLTA_IP3),
    BIT(DLTA_IP2),
    BIT(DLTA_IP1),
    BIT(DLTA_IP0),
    ENDBITS
};

BITFIELD conf_bits[] = {
    BIT(TX_EN),
    BIT(RX_EN),
    ENDBITS
};

/* TTI (Port A) data structures */

REG tti_a_reg[] = {
    { HRDATADF(STAT, iu_port_a.stat,   8, "Status", sr_bits)       },
    { HRDATADF(CONF, iu_port_a.conf,   8, "Config", conf_bits)     },
    { BRDATAD(DATA,  iu_port_a.rxbuf,  16, 8, IU_BUF_SIZE, "Data") },
    { NULL }
};

UNIT tti_a_unit = { UDATA(&iu_svc_tti_a, UNIT_IDLE, 0), TMLN_SPD_9600_BPS };

DEVICE tti_a_dev = {
    "TTIA", &tti_a_unit, tti_a_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &tti_a_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* TTO (Port A) data structures */

REG tto_a_reg[] = {
    { HRDATADF(STAT,  iu_port_a.stat,  8, "Status", sr_bits)                      },
    { HRDATADF(ISTAT, iu_state.istat,  8, "Interrupt Status", isr_bits)           },
    { HRDATAD(IMR,    iu_state.imr,    8, "Interrupt Mask")                       },
    { HRDATADF(ACR,   iu_state.acr,    8, "Auxiliary Control Register", acr_bits) },
    { HRDATAD(DATA,   iu_port_a.txbuf, 8, "Data")                                 },
    { NULL }
};

UNIT tto_a_unit = { UDATA(&iu_svc_tto_a, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

DEVICE tto_a_dev = {
    "TTOA", &tto_a_unit, tto_a_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* TTI (Port B) data structures */

REG tti_b_reg[] = {
    { HRDATADF(STAT, iu_port_b.stat,   8, "Status", sr_bits)       },
    { HRDATADF(CONF, iu_port_b.conf,   8, "Config", conf_bits)     },
    { BRDATAD(DATA,  iu_port_b.rxbuf,  16, 8, IU_BUF_SIZE, "Data") },
    { NULL }
};

UNIT tti_b_unit = { UDATA(&iu_svc_tti_b, UNIT_IDLE, 0), TMLN_SPD_9600_BPS };

DEVICE tti_b_dev = {
    "TTIB", &tti_b_unit, tti_b_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &tti_b_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* TTO (Port B) data structures */

REG tto_b_reg[] = {
    { HRDATADF(STAT,  iu_port_b.stat,  8, "Status", sr_bits)                      },
    { HRDATADF(ISTAT, iu_state.istat,  8, "Interrupt Status", isr_bits)           },
    { HRDATAD(IMR,    iu_state.imr,    8, "Interrupt Mask")                       },
    { HRDATADF(ACR,   iu_state.acr,    8, "Auxiliary Control Register", acr_bits) },
    { HRDATAD(DATA,   iu_port_b.txbuf, 8, "Data")                                 },
    { NULL }
};

UNIT tto_b_unit = { UDATA(&iu_svc_tto_b, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

DEVICE tto_b_dev = {
    "TTOB", &tto_b_unit, tto_b_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* IU Timer data structures */

REG iu_timer_reg[] = {
    { HRDATAD(CTR_SET, iu_timer_state.c_set, 16, "Counter Setting") },
    { NULL }
};

UNIT iu_timer_unit = { UDATA(&iu_svc_timer, 0, 0) };

DEVICE iu_timer_dev = {
    "IUTIMER", &iu_timer_unit, iu_timer_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &iu_timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};


void increment_modep_a()
{
    iu_increment_a = FALSE;
    iu_port_a.modep++;

    if (iu_port_a.modep > 1) {
        iu_port_a.modep = 0;
    }
}

void increment_modep_b()
{
    iu_increment_b = FALSE;
    iu_port_b.modep++;

    if (iu_port_b.modep > 1) {
        iu_port_b.modep = 0;
    }
}

void iu_txrdy_a_irq() {
    if ((iu_state.imr & ISTS_TAI) &&
        (iu_port_a.conf & TX_EN) &&
        (iu_port_a.stat & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &tto_a_dev,
                  "Firing IU TTY IRQ 13 ON TX/State Change: PORT A\n");
        csr_data |= CSRUART;
    }
}

void iu_txrdy_b_irq() {
    if ((iu_state.imr & ISTS_TBI) &&
        (iu_port_b.conf & TX_EN) &&
        (iu_port_b.stat & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &tto_b_dev,
                  "Firing IU TTY IRQ 13 ON TX/State Change: PORT B\n");
        csr_data |= CSRUART;
    }
}

t_stat tti_a_reset(DEVICE *dptr)
{
    memset(&iu_state, 0, sizeof(IU_STATE));
    memset(&iu_port_a, 0, sizeof(IU_PORT));

    /* Start the TTI A polling loop */
    if (!sim_is_active(&tti_a_unit)) {
        sim_activate(&tti_a_unit, tti_a_unit.wait);
    }

    return SCPE_OK;
}

t_stat tti_b_reset(DEVICE *dtpr)
{
    memset(&iu_state, 0, sizeof(IU_STATE));
    memset(&iu_port_b, 0, sizeof(IU_PORT));

    /* Start the TTI B polling loop */
    if (!sim_is_active(&tti_b_unit)) {
        sim_activate(&tti_b_unit, tti_b_unit.wait);
    }

    return SCPE_OK;
}

t_stat iu_timer_reset(DEVICE *dptr)
{
    memset(&iu_timer_state, 0, sizeof(IU_TIMER_STATE));

    return SCPE_OK;
}

/* Service routines */

t_stat iu_svc_tti_a(UNIT *uptr)
{
    int32 temp;

    sim_clock_coschedule_tmr_abs(uptr, TMR_CLK, 2);

    /* TODO:

       - If there has been a change on IP0-IP3, set the corresponding
       bits in IPCR, if configured to do so. We'll need to figure out
       how these are wired (DCD pin, etc?)

       - Update the Output Port pins (which are logically inverted)
       based on the contents of the OPR, OPCR, MR, and CR registers.
    */

    if ((temp = sim_poll_kbd()) < SCPE_KFLAG) {
         return temp;
    }

    sim_debug(READ_MSG, &tti_a_dev,
              ">>> TTIA: Receive %02x (%c)\n",
              temp & 0xff, temp & 0xff);

    if (iu_port_a.conf & RX_EN) {
        if ((iu_port_a.stat & STS_FFL) == 0) {
            iu_port_a.rxbuf[iu_port_a.w_p] = (temp & 0xff);
            iu_port_a.w_p = (iu_port_a.w_p + 1) % IU_BUF_SIZE;
            if (iu_port_a.w_p == iu_port_b.w_p) {
                sim_debug(READ_MSG, &tti_a_dev,
                          ">>> FIFO FULL ON KEYBOARD READ!!!! <<<\n");
                iu_port_a.stat |= STS_FFL;
            }
        }
        iu_port_a.stat |= STS_RXR;
        iu_state.istat |= ISTS_RAI;
        if (iu_state.imr & 0x02) {
            sim_debug(EXECUTE_MSG, &tti_a_dev,
                      "Firing IRQ 13 ON TTI A RECEIVE (%c)\n",
                      (temp & 0xff));
            csr_data |= CSRUART;
        }
    }

    return SCPE_OK;
}

t_stat iu_svc_tti_b(UNIT *uptr)
{
    sim_clock_coschedule_tmr_abs(uptr, TMR_CLK, 2);

    /* TODO: Handle TTIB as a terminal */

    return SCPE_OK;
}

t_stat iu_svc_tto_a(UNIT *uptr)
{
    iu_txrdy_a_irq();
    return SCPE_OK;
}

t_stat iu_svc_tto_b(UNIT *uptr)
{
    iu_txrdy_b_irq();
    return SCPE_OK;
}

t_stat iu_svc_timer(UNIT *uptr)
{
    iu_state.istat |= ISTS_CRI;

    if (iu_state.imr & 0x08) {
        csr_data |= CSRUART;
    }

    return SCPE_OK;
}

/*
 *     Reg |       Name (Read)       |        Name (Write)
 *    -----+-------------------------+----------------------------
 *      0  | Mode Register 1/2 A     | Mode Register 1/2 A
 *      1  | Status Register A       | Clock Select Register A
 *      2  | BRG Test                | Command Register A
 *      3  | Rx Holding Register A   | Tx Holding Register A
 *      4  | Input Port Change Reg.  | Aux. Control Register
 *      5  | Interrupt Status Reg.   | Interrupt Mask Register
 *      6  | Counter/Timer Upper Val | C/T Upper Preset Val.
 *      7  | Counter/Timer Lower Val | C/T Lower Preset Val.
 *      8  | Mode Register B         | Mode Register B
 *      9  | Status Register B       | Clock Select Register B
 *     10  | 1X/16X Test             | Command Register B
 *     11  | Rx Holding Register B   | Tx Holding Register B
 *     12  | *Reserved*              | *Reserved*
 *     13  | Input Ports IP0 to IP6  | Output Port Conf. Reg.
 *     14  | Start Counter Command   | Set Output Port Bits Cmd.
 *     15  | Stop Counter Command    | Reset Output Port Bits Cmd.
 */


uint32 iu_read(uint32 pa, size_t size)
{
    uint8 reg, modep;
    uint32 data, delay;

    reg = (uint8) (pa - IUBASE);

    switch (reg) {
    case MR12A:
        modep = iu_port_a.modep;
        data = iu_port_a.mode[modep];
        iu_increment_a = TRUE;
        break;
    case SRA:
        data = iu_port_a.stat;
        break;
    case RHRA:
        data = iu_port_a.rxbuf[iu_port_a.r_p];
        iu_port_a.r_p = (iu_port_a.r_p + 1) % IU_BUF_SIZE;
        sim_debug(READ_MSG, &tti_a_dev,
                  "[%08x] RHRA = %02x (%c)\n",
                  R[NUM_PC], (data & 0xff), (data & 0xff));
        iu_port_a.stat &= ~(STS_RXR|STS_FFL);
        iu_state.istat &= ~ISTS_RAI;
        csr_data &= ~CSRUART;
        break;
    case IPCR:
        data = iu_state.ipcr;
        /* Reading the port resets the upper four bits */
        iu_state.ipcr &= 0x0f;
        csr_data &= ~CSRUART;
        break;
    case ISR:
        data = iu_state.istat;
        break;
    case CTU:
        data = (iu_timer_state.c_set >> 8) & 0xff;
        break;
    case CTL:
        data = iu_timer_state.c_set & 0xff;
        break;
    case MR12B:
        modep = iu_port_b.modep;
        data = iu_port_b.mode[modep];
        iu_increment_b = TRUE;
        break;
    case SRB:
        data = iu_port_b.stat;
        sim_debug(READ_MSG, &tti_b_dev,
                  "[%08x] SRB = %02x\n",
                  R[NUM_PC], (data & 0xff));
        break;
    case RHRB:
        data = iu_port_b.rxbuf[iu_port_b.r_p];
        iu_port_b.r_p = (iu_port_b.r_p + 1) % IU_BUF_SIZE;
        sim_debug(READ_MSG, &tti_b_dev,
                  "[%08x] RHRB = %02x (%c)\n",
                  R[NUM_PC], (data & 0xff), (data & 0xff));
        iu_port_b.stat &= ~(STS_RXR|STS_FFL);
        iu_state.istat &= ~ISTS_RBI;
        break;
    case INPRT:
        /* TODO: Correct behavior for DCD on contty */
        /* For now, this enables DCD/DTR on console only */
        data = 0x8e;
        break;
    case START_CTR:
        data = 0;
        iu_state.istat &= ~ISTS_CRI;
        delay = (uint32) (IU_TIMER_STP * iu_timer_state.c_set);
        sim_activate_abs(&iu_timer_unit, (int32) DELAY_US(delay));
        sim_debug(READ_MSG, &iu_timer_dev,
                  "[%08x] Activating IU timer to fire in %04x steps\n",
                  R[NUM_PC], iu_timer_state.c_set);
        break;
    case STOP_CTR:
        data = 0;
        iu_state.istat &= ~ISTS_CRI;
        csr_data &= ~CSRUART;
        sim_cancel(&iu_timer_unit);
        sim_debug(READ_MSG, &iu_timer_dev,
                  "[%08x] Cancelling IU timer\n",
                  R[NUM_PC]);
        break;
    case 17: /* Clear DMAC interrupt */
        data = 0;
        iu_port_a.drq = FALSE;
        iu_port_b.drq = FALSE;
        csr_data &= ~CSRDMA;
        break;
    default:
        data = 0;
        break;
    }

    return data;
}

void iu_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg;
    uint8 modep;

    reg = (uint8) (pa - IUBASE);

    switch (reg) {
    case MR12A:
        modep = iu_port_a.modep;
        iu_port_a.mode[modep] = val & 0xff;
        iu_increment_a = TRUE;
        break;
    case CSRA:
        /* Set baud rate - not implemented */
        break;
    case CRA:  /* Command A */
        sim_debug(WRITE_MSG, &tti_a_dev,
                  "[%08x] CRA = %02x\n",
                  R[NUM_PC], (val & 0xff));
        iu_w_cmd(PORT_A, (uint8) val);
        break;
    case THRA:  /* TX/RX Buf A */
        sim_debug(WRITE_MSG, &tto_a_dev,
                  "[%08x] THRA = %02x (%c)\n",
                  R[NUM_PC], (val & 0xff), (val & 0xff));
        /* Loopback mode */
        if ((iu_port_a.mode[1] & 0xc0) == 0x80) {
            iu_port_a.txbuf = (uint8) val;

            /* This is also a Receive */
            if ((iu_port_a.stat & STS_FFL) == 0) {
                iu_port_a.rxbuf[iu_port_a.w_p] = (uint8) val;
                iu_port_a.w_p = (iu_port_a.w_p + 1) % IU_BUF_SIZE;
                if (iu_port_a.w_p == iu_port_b.r_p) {
                    sim_debug(WRITE_MSG, &tto_a_dev,
                              ">>> FIFO FULL ON LOOPBACK THRA! <<<");
                    iu_port_a.stat |= STS_FFL;
                }
            }

            iu_port_a.stat |= STS_RXR;
            iu_state.istat |= ISTS_RAI;
        } else {
            iu_tx(PORT_A, (uint8) val);
        }
        csr_data &= ~CSRUART;
        break;
    case ACR:  /* Auxiliary Control Register */
        iu_state.acr = (uint8) val;
        break;
    case IMR:
        sim_debug(WRITE_MSG, &tti_a_dev,
                  "[%08x] IMR = %02x\n",
                  R[NUM_PC], (val & 0xff));
        iu_state.imr = (uint8) val;
        csr_data &= ~CSRUART;
        /* Possibly cause an interrupt */
        iu_txrdy_a_irq();
        iu_txrdy_b_irq();
        break;
    case CTUR:  /* Counter/Timer Upper Preset Value */
        /* Clear out high byte */
        iu_timer_state.c_set &= 0x00ff;
        /* Set high byte */
        iu_timer_state.c_set |= (val & 0xff) << 8;
        break;
    case CTLR:  /* Counter/Timer Lower Preset Value */
        /* Clear out low byte */
        iu_timer_state.c_set &= 0xff00;
        /* Set low byte */
        iu_timer_state.c_set |= (val & 0xff);
        break;
    case MR12B:
        modep = iu_port_b.modep;
        iu_port_b.mode[modep] = val & 0xff;
        iu_increment_b = TRUE;
        break;
    case CRB:  /* Command B */
        sim_debug(WRITE_MSG, &tti_b_dev,
                  "[%08x] CRB = %02x\n",
                  R[NUM_PC], (val & 0xff));
        iu_w_cmd(PORT_B, (uint8) val);
        break;
    case CSRB:
        break;
    case THRB: /* TX/RX Buf B */
        sim_debug(WRITE_MSG, &tto_b_dev,
                  "[%08x] THRB = %02x (%c)\n",
                  R[NUM_PC], (val & 0xff), (val & 0xff));
        /* Loopback mode */
        if ((iu_port_b.mode[1] & 0xc0) == 0x80) {
            iu_port_a.txbuf = (uint8) val;

            /* This is also a Receive */
            if ((iu_port_b.stat & STS_FFL) == 0) {
                iu_port_b.rxbuf[iu_port_b.w_p] = (uint8) val;
                iu_port_b.w_p = (iu_port_b.w_p + 1) % IU_BUF_SIZE;
                if (iu_port_b.w_p == iu_port_b.r_p) {
                    sim_debug(WRITE_MSG, &tto_b_dev,
                              ">>> FIFO FULL ON LOOPBACK THRB! <<<");
                    iu_port_b.stat |= STS_FFL;
                }
            }

            iu_port_b.stat |= STS_RXR;
            iu_state.istat |= ISTS_RBI;
        } else {
            iu_tx(PORT_B, (uint8) val);
        }
        break;
    case OPCR:
        iu_state.opcr = (uint8) val;
        break;
    case SOPR:
        break;
    case ROPR:
        break;
    default:
        break;
    }
}

void iua_drq_handled()
{
    sim_debug(EXECUTE_MSG, &tto_a_dev,
              "Firing IU IRQ 13 on DRQ (A) Hanlded\n");
    csr_data |= CSRDMA;
}

void iub_drq_handled()
{
    sim_debug(EXECUTE_MSG, &tto_a_dev,
              "Firing IU IRQ 13 on DRQ (B) Hanlded\n");
    csr_data |= CSRDMA;
}

static SIM_INLINE void iu_tx(uint8 portno, uint8 val)
{
    IU_PORT *p;
    UNIT *uptr;

    if (portno == 0) {
        p = &iu_port_a;
        uptr = &tto_a_unit;
    } else {
        p = &iu_port_b;
        uptr = &tto_b_unit;
    }

    p->txbuf = val;

    if (p->conf & TX_EN) {
        p->stat &= ~(STS_TXR|STS_TXE);
        iu_state.istat &= ~(1 << (portno*4));

        if (portno == PORT_A) {
            /* Write the character to the SIMH console */
            sim_putchar(val);
        }

        /* The buffer is now empty, we've transmitted, so set TXR */
        p->stat |= STS_TXR;
        iu_state.istat |= (1 << (portno*4));

        /* Possibly cause an interrupt */
        sim_activate_abs(uptr, uptr->wait);
    }
}

static SIM_INLINE void iu_w_cmd(uint8 portno, uint8 cmd)
{

    IU_PORT *p;

    if (portno == 0) {
        p = &iu_port_a;
    } else {
        p = &iu_port_b;
    }

    /* Enable or disable transmitter        */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DTX) {
        p->conf &= ~TX_EN;
        p->stat &= ~STS_TXR;
        p->stat &= ~STS_TXE;
        p->drq = FALSE;
    } else if (cmd & CMD_ETX) {
        p->conf |= TX_EN;
        /* TXE and TXR are always set by an ENABLE */
        p->stat |= STS_TXR;
        p->stat |= STS_TXE;
        p->drq = TRUE;
        iu_state.istat |= 1 << (portno*4);
        if (portno == 0) {
            iu_txrdy_a_irq();
        } else {
            iu_txrdy_b_irq();
        }
    }

    /* Enable or disable receiver.          */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DRX) {
        p->conf &= ~RX_EN;
        p->stat &= ~STS_RXR;
    } else if (cmd & CMD_ERX) {
        p->conf |= RX_EN;
    }

    /* Command register bits 6-4 have special meaning */
    switch ((cmd >> CMD_MISC_SHIFT) & CMD_MISC_MASK) {
    case 1:
        /*  Causes the Channel A MR pointer to point to MR1. */
        p->modep = 0;
        break;
    case 2:
        /* Reset receiver. Resets the Channel's receiver as if a
           hardware reset had been applied. The receiver is disabled
           and the FIFO is flushed. */
        p->stat &= ~STS_RXR;
        p->conf &= ~RX_EN;
        p->w_p = 0;
        p->r_p = 0;
        break;
    case 3:
        /* Reset transmitter. Resets the Channel's transmitter as if a
           hardware reset had been applied. */
        p->stat &= ~STS_TXR;
        p->stat &= ~STS_TXE;
        p->conf &= ~TX_EN;
        p->w_p = 0;
        p->r_p = 0;
        break;
    case 4:
        /* Reset error status. Clears the Channel's Received Break,
           Parity Error, and Overrun Error bits in the status register
           (SRA[7:4]). Used in character mode to clear OE status
           (although RB, PE and FE bits will also be cleared) and in
           block mode to clear all error status after a block of data
           has been received. */
        p->stat &= ~(STS_FER|STS_PER|STS_OER);
        break;
    case 5:
        /* Reset Channel's break change interrupt. Causes the Channel
           A break detect change bit in the interrupt status register
           (ISR[2] for Chan. A, ISR[6] for Chan. B) to be cleared to
           zero. */
        iu_state.istat &= ~(1 << (2 + portno*4));
        break;
    case 6:
        /* Start break. Forces the TxDA output LOW (spacing). If the
           transmitter is empty the start of the break condition will
           be delayed up to two bit times. If the transmitter is
           active the break begins when transmission of the character
           is completed. If a character is in the THR, the start of
           the break will be delayed until that character, or any
           other loaded subsequently are transmitted. The transmitter
           must be enabled for this command to be accepted. */
        /* Not Implemented */
        break;
    case 7:
        /* Stop break. The TxDA line will go HIGH (marking) within two
           bit times. TxDA will remain HIGH for one bit time before
           the next character, if any, is transmitted. */
        /* Not Implemented */
        break;
    }
}
