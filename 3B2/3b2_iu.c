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
 * Registers
 */

/* The IU state */
IU_STATE iu_state;

t_bool iu_increment_a = FALSE;
t_bool iu_increment_b = FALSE;

extern uint16 csr_data;

UNIT iu_unit[] = {
    { UDATA(&iu_svc_tti, UNIT_IDLE, 0), TMLN_SPD_9600_BPS },
    { UDATA(&iu_svc_tto, TT_MODE_8B, 0), SERIAL_OUT_WAIT },
    { UDATA(&iu_svc_timer, 0, 0) },
    { NULL }
};

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

REG iu_reg[] = {
    { HRDATADF(ISTAT,  iu_state.istat,          8, "Interrupt Status", isr_bits)           },
    { HRDATAD(IMR,     iu_state.imr,            8, "Interrupt Mask")                       },
    { HRDATADF(ACR,    iu_state.acr,            8, "Auxiliary Control Register", acr_bits) },
    { HRDATAD(CTR,     iu_state.c_set,         16, "Counter Setting")                      },
    { HRDATAD(IP,      iu_state.inprt,          8, "Input Port")                           },
    { HRDATADF(STAT_A, iu_state.port[0].stat,   8, "Status  (Port A)", sr_bits)            },
    { HRDATAD(DATA_A,  iu_state.port[0].buf,    8, "Data    (Port A)")                     },
    { HRDATADF(CONF_A, iu_state.port[0].conf,   8, "Config  (Port A)", conf_bits)          },
    { HRDATADF(STAT_B, iu_state.port[1].stat,   8, "Status  (Port B)", sr_bits)            },
    { HRDATAD(DATA_B,  iu_state.port[1].buf,    8, "Data    (Port B)")                     },
    { HRDATADF(CONF_B, iu_state.port[1].conf,   8, "Config  (Port B)", conf_bits)          },
    { NULL }
};

DEVICE iu_dev = {
    "IU", iu_unit, iu_reg, NULL,
    3, 8, 32, 1, 8, 8,
    NULL, NULL, &iu_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

void increment_modep_a()
{
    iu_increment_a = FALSE;
    iu_state.port[PORT_A].modep++;

    if (iu_state.port[PORT_A].modep > 1) {
        iu_state.port[PORT_A].modep = 0;
    }
}

void increment_modep_b()
{
    iu_increment_b = FALSE;
    iu_state.port[PORT_B].modep++;

    if (iu_state.port[PORT_B].modep > 1) {
        iu_state.port[PORT_B].modep = 0;
    }
}

void iu_txrdy_irq(uint8 portno) {
    uint8 irq_mask = (uint8) (1u << (portno * 4));

    if ((iu_state.imr & irq_mask) &&
        (iu_state.port[portno].conf & TX_EN) &&
        (iu_state.port[portno].stat & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &iu_dev,
                  "Firing IU TTY IRQ 13 ON TX/State Change\n");
        csr_data |= CSRUART;
    }
}

t_stat iu_reset(DEVICE *dptr)
{
    uint8 portno;

    memset(&iu_state, 0, sizeof(struct iu_state));

    iu_state.opcr = 0;

    if (!sim_is_active(&iu_unit[UNIT_CONSOLE_TTI])) {
        iu_unit[UNIT_CONSOLE_TTI].wait = IU_TTY_DELAY;
        sim_activate(&iu_unit[UNIT_CONSOLE_TTI],
                     iu_unit[UNIT_CONSOLE_TTI].wait);
    }

    for (portno = 0; portno < 2; portno++) {
        iu_state.port[portno].buf = 0;
        iu_state.port[portno].modep = 0;
        iu_state.port[portno].conf = 0;
        iu_state.port[portno].stat = 0;
    }

    return SCPE_OK;
}

t_stat iu_svc_tti(UNIT *uptr)
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

    if (iu_state.port[PORT_A].conf & RX_EN) {
        iu_state.port[PORT_A].buf = (temp & 0xff);
        iu_state.port[PORT_A].stat |= STS_RXR;
        iu_state.istat |= ISTS_RAI;
        if (iu_state.imr & 0x02) {
            sim_debug(EXECUTE_MSG, &iu_dev,
                      "Firing IU TTY IRQ 13 ON RECEIVE (%c)\n",
                      (temp & 0xff));
            csr_data |= CSRUART;
        }
    }

    return SCPE_OK;
}

t_stat iu_svc_tto(UNIT *uptr)
{
    sim_debug(EXECUTE_MSG, &iu_dev,
              "Calling iu_txrdy_irq on iu_svc_tto\n");

    iu_txrdy_irq(PORT_A);

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
        modep = iu_state.port[PORT_A].modep;
        data = iu_state.port[PORT_A].mode[modep];
        iu_increment_a = TRUE;
        break;
    case SRA:
        data = iu_state.port[PORT_A].stat;
        break;
    case RHRA:
        data = iu_state.port[PORT_A].buf;
        iu_state.port[PORT_A].stat &= ~STS_RXR;
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
        data = (iu_state.c_set >> 8) & 0xff;
        break;
    case CTL:
        data = iu_state.c_set & 0xff;
        break;
    case MR12B:
        modep = iu_state.port[PORT_B].modep;
        data = iu_state.port[PORT_B].mode[modep];
        iu_increment_b = TRUE;
        break;
    case SRB:
        data = iu_state.port[PORT_B].stat;
        break;
    case RHRB:
        data = iu_state.port[PORT_B].buf;
        iu_state.port[PORT_B].stat &= ~STS_RXR;
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
        delay = (uint32) (IU_TIMER_STP * iu_state.c_set);
        sim_activate_abs(&iu_unit[UNIT_IU_TIMER], (int32) DELAY_US(delay));
        break;
    case STOP_CTR:
        data = 0;
        iu_state.istat &= ~ISTS_CRI;
        csr_data &= ~CSRUART;
        sim_cancel(&iu_unit[UNIT_IU_TIMER]);
        break;
    case 17: /* Clear DMAC interrupt */
        data = 0;
        iu_state.drqa = FALSE;
        iu_state.drqb = FALSE;
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
        modep = iu_state.port[PORT_A].modep;
        iu_state.port[PORT_A].mode[modep] = val & 0xff;
        iu_increment_a = TRUE;
        break;
    case CSRA:
        /* Set baud rate - not implemented */
        break;
    case CRA:  /* Command A */
        iu_w_cmd(PORT_A, (uint8) val);
        break;
    case THRA:  /* TX/RX Buf A */
        /* Loopback mode */
        if ((iu_state.port[PORT_A].mode[1] & 0xc0) == 0x80) {
            iu_state.port[PORT_A].buf = (uint8) val;
            iu_state.port[PORT_A].stat |= STS_RXR;
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
        iu_state.imr = (uint8) val;
        csr_data &= ~CSRUART;
        /* Possibly cause an interrupt */
        sim_debug(EXECUTE_MSG, &iu_dev,
                  ">>> calling iu_txrdy_irq() on IMR write.\n");
        iu_txrdy_irq(PORT_A);
        iu_txrdy_irq(PORT_B);
        break;
    case CTUR:  /* Counter/Timer Upper Preset Value */
        /* Clear out high byte */
        iu_state.c_set &= 0x00ff;
        /* Set high byte */
        iu_state.c_set |= (val & 0xff) << 8;
        break;
    case CTLR:  /* Counter/Timer Lower Preset Value */
        /* Clear out low byte */
        iu_state.c_set &= 0xff00;
        /* Set low byte */
        iu_state.c_set |= (val & 0xff);
        break;
    case MR12B:
        modep = iu_state.port[PORT_B].modep;
        iu_state.port[PORT_B].mode[modep] = val & 0xff;
        iu_increment_b = TRUE;
        break;
    case CRB:  /* Command B */
        iu_w_cmd(PORT_B, (uint8) val);
        break;
    case CSRB:
        break;
    case THRB: /* TX/RX Buf B */
        /* Loopback mode */
        if ((iu_state.port[PORT_B].mode[1] & 0xc0) == 0x80) {
            iu_state.port[PORT_B].buf = (uint8) val;
            iu_state.port[PORT_B].stat |= STS_RXR;
            iu_state.istat |= ISTS_RAI;
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
    sim_debug(EXECUTE_MSG, &iu_dev,
              "Firing IU TTY IRQ 13 On DRQ Handled\n");

    csr_data |= CSRDMA;
}

void iub_drq_handled()
{
    sim_debug(EXECUTE_MSG, &iu_dev,
              ">>> DRQB handled.\n");
}

static SIM_INLINE void iu_tx(uint8 portno, uint8 val)
{
    struct port *p;

    p = &iu_state.port[portno];

    p->buf = val;

    if (p->conf & TX_EN) {
        sim_debug(EXECUTE_MSG, &iu_dev,
                  "[%08x] TRANSMIT: %02x (%c)\n",
                  R[NUM_PC], val, val);
        p->stat &= ~(STS_TXR|STS_TXE);
        iu_state.istat &= ~(1 << (portno*4));

        /* Write the character to the SIMH console */
        sim_putchar(p->buf);

        /* The buffer is now empty, we've transmitted, so set TXR */
        p->stat |= STS_TXR;
        iu_state.istat |= (1 << (portno*4));

        /* Possibly cause an interrupt */
        sim_activate_abs(&iu_unit[UNIT_CONSOLE_TTO],
                         iu_unit[UNIT_CONSOLE_TTO].wait);
    }
}

static SIM_INLINE void iu_w_cmd(uint8 portno, uint8 cmd)
{
    /* Enable or disable transmitter        */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DTX) {
        iu_state.port[portno].conf &= ~TX_EN;
        iu_state.port[portno].stat &= ~STS_TXR;
        iu_state.port[portno].stat &= ~STS_TXE;
        iu_state.drqa = FALSE;
        sim_debug(EXECUTE_MSG, &iu_dev,
                  ">>> Disabling transmitter.\n");
    } else if (cmd & CMD_ETX) {
        iu_state.port[portno].conf |= TX_EN;
        /* TXE and TXR are always set by an ENABLE */
        iu_state.port[portno].stat |= STS_TXR;
        iu_state.port[portno].stat |= STS_TXE;
        iu_state.istat |= 1 << (portno*4);
        iu_state.drqa = TRUE;
        sim_debug(EXECUTE_MSG, &iu_dev,
                  ">>> Calling iu_txrdy_irq() on TX Enable\n");
        iu_txrdy_irq(portno);
    }

    /* Enable or disable receiver.          */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DRX) {
        iu_state.port[portno].conf &= ~RX_EN;
        iu_state.port[portno].stat &= ~STS_RXR;
    } else if (cmd & CMD_ERX) {
        iu_state.port[portno].conf |= RX_EN;
    }

    /* Command register bits 6-4 have special meaning */
    switch ((cmd >> CMD_MISC_SHIFT) & CMD_MISC_MASK) {
    case 1:
        /*  Causes the Channel A MR pointer to point to MR1. */
        iu_state.port[portno].modep = 0;
        break;
    case 2:
        /* Reset receiver. Resets the Channel's receiver as if a
           hardware reset had been applied. The receiver is disabled
           and the FIFO is flushed. */
        iu_state.port[portno].stat &= ~STS_RXR;
        iu_state.port[portno].conf &= ~RX_EN;
        iu_state.port[portno].buf = 0;
        break;
    case 3:
        /* Reset transmitter. Resets the Channel's transmitter as if a
           hardware reset had been applied. */
        iu_state.port[portno].stat &= ~STS_TXR;
        iu_state.port[portno].stat &= ~STS_TXE;
        iu_state.port[portno].conf &= ~TX_EN;
        iu_state.port[portno].buf = 0;
        break;
    case 4:
        /* Reset error status. Clears the Channel's Received Break,
           Parity Error, and Overrun Error bits in the status register
           (SRA[7:4]). Used in character mode to clear OE status
           (although RB, PE and FE bits will also be cleared) and in
           block mode to clear all error status after a block of data
           has been received. */
        iu_state.port[portno].stat &= ~(STS_FER|STS_PER|STS_OER);
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
