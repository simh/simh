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
#include "sim_tmxr.h"

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
 * The SCN2681A UART is represented here by four devices:
 *
 *   - Console TTI (Input, port A)
 *   - Console TTO (Output, port A)
 *   - Contty (I/O, port B. Terminal multiplexer with one line)
 *   - IU Timer
 */


/*
 * Registers
 */

/* The IU state shared between A and B */
IU_STATE iu_state;

/* The tx/rx state for ports A and B */
IU_PORT iu_console;
IU_PORT iu_contty;

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

/* TTI (Console) data structures */

REG tti_reg[] = {
    { HRDATADF(STAT, iu_console.stat,   8, "Status", sr_bits)       },
    { HRDATADF(CONF, iu_console.conf,   8, "Config", conf_bits)     },
    { BRDATAD(DATA,  iu_console.rxbuf,  16, 8, IU_BUF_SIZE, "Data") },
    { NULL }
};

UNIT tti_unit = { UDATA(&iu_svc_tti, UNIT_IDLE, 0), TMLN_SPD_9600_BPS };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* TTO (Console) data structures */

REG tto_reg[] = {
    { HRDATADF(STAT,  iu_console.stat, 8, "Status", sr_bits)                      },
    { HRDATADF(ISTAT, iu_state.istat,  8, "Interrupt Status", isr_bits)           },
    { HRDATAD(IMR,    iu_state.imr,    8, "Interrupt Mask")                       },
    { HRDATADF(ACR,   iu_state.acr,    8, "Auxiliary Control Register", acr_bits) },
    { HRDATAD(DATA,   iu_console.txbuf, 8, "Data")                                },
    { NULL }
};

UNIT tto_unit = { UDATA(&iu_svc_tto, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* CONTTY data structures */

/*
 * The CONTTY "multiplexer" is a bit unusual in that it serves only a
 * single line, representing the built-in CONTTY port. On a real
 * 3B2/400, the system board's dual UART serves both CONSOLE and
 * CONTTY lines, giving support for two terminals. In the simulator,
 * the CONSOLE is served by TTI and TTO devices, whereas the CONTTY is
 * served by a TMXR multiplexer.
 */

TMLN *contty_ldsc = NULL;
TMXR contty_desc = { 1, 0, 0, NULL };

REG contty_reg[] = {
    { HRDATADF(STAT,  iu_contty.stat,  8, "Status", sr_bits) },
    { HRDATADF(CONF,  iu_contty.conf,  8, "Config", conf_bits) },
    { BRDATAD(RXDATA, iu_contty.rxbuf, 16, 8, IU_BUF_SIZE, "RX Data") },
    { HRDATAD(TXDATA, iu_contty.txbuf, 8, "TX Data") },
    { HRDATADF(ISTAT, iu_state.istat,  8, "Interrupt Status", isr_bits) },
    { HRDATAD(IMR,    iu_state.imr,    8, "Interrupt Mask") },
    { HRDATADF(ACR,   iu_state.acr,    8, "Auxiliary Control Register", acr_bits) },
    { NULL }
};

CONST char *brg_rates[IU_SPEED_REGS][IU_SPEEDS] = {
    {NULL,    "110",   NULL,   NULL,
     "300",   NULL,    NULL,   "1200",
     "2400",  "4800",  NULL,   "9600",
     "38400", NULL,    NULL,   NULL},
    {NULL,    "110",   NULL,   NULL,
     "300",   NULL,    "1200", NULL,
     NULL,    "2400",  "4800", "9600",
     "19200", NULL,    NULL,   NULL}
};

CONST char *parity[3] = {"O", "E", "N"};

UNIT contty_unit[2] = {
    { UDATA(&iu_svc_contty_rcv, UNIT_ATTABLE, 0) },
    { UDATA(&iu_svc_contty_xmt, TT_MODE_8B, 0), SERIAL_OUT_WAIT }
};

UNIT *contty_rcv_unit = &contty_unit[0];
UNIT *contty_xmt_unit = &contty_unit[1];

DEBTAB contty_deb_tab[] = {
    {"EXEC", EXECUTE_MSG, "Execute"},
    {"XMT",  TMXR_DBG_XMT,  "Transmitted Data"},
    {"RCV",  TMXR_DBG_RCV,  "Received Data"},
    {"MDM",  TMXR_DBG_MDM,  "Modem Signals"},
    {"CON",  TMXR_DBG_CON,  "connection activities"},
    {"TRC",  TMXR_DBG_TRC,  "trace routine calls"},
    {"ASY",  TMXR_DBG_ASY,  "Asynchronous Activities"},
    {0}
};


DEVICE contty_dev = {
    "CONTTY", contty_unit, contty_reg, NULL,
    1, 8, 32, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &contty_reset,
    NULL, &contty_attach, &contty_detach,
    NULL, DEV_DISABLE|DEV_DEBUG|DEV_MUX,
    0, contty_deb_tab, NULL, NULL,
    NULL, NULL,
    (void *)&contty_desc,
    NULL
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

uint8 brg_reg = 0;       /* Selected baud-rate generator register */
uint8 brg_clk = 11;      /* Selected baud-rate generator clock */
uint8 parity_sel = 1;    /* Selected parity */
uint8 bits_per_char = 7;


t_stat contty_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    TMLN *lp;

    tmxr_set_modem_control_passthru(&contty_desc);

    r = tmxr_attach(&contty_desc, uptr, cptr);
    if (r != SCPE_OK) {
        tmxr_clear_modem_control_passthru(&contty_desc);
        return r;
    }

    lp = &contty_ldsc[0];
    tmxr_set_get_modem_bits(lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);

    return SCPE_OK;
}

t_stat contty_detach(UNIT *uptr)
{
    t_stat r = tmxr_detach(&contty_desc, uptr);
    tmxr_clear_modem_control_passthru(&contty_desc);

    return r;
}

void increment_modep_a()
{
    iu_increment_a = FALSE;
    iu_console.modep++;

    if (iu_console.modep > 1) {
        iu_console.modep = 0;
    }
}

void increment_modep_b()
{
    iu_increment_b = FALSE;
    iu_contty.modep++;

    if (iu_contty.modep > 1) {
        iu_contty.modep = 0;
    }
}

void iu_txrdy_a_irq() {
    if ((iu_state.imr & IMR_TXRA) &&
        (iu_console.conf & TX_EN) &&
        (iu_console.stat & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "[iu_txrdy_a_irq()] Firing IRQ after transmit of %02x (%c)\n",
                  (uint8) iu_console.txbuf, (char) iu_console.txbuf);
        csr_data |= CSRUART;
    }
}

void iu_txrdy_b_irq() {
    if ((iu_state.imr & IMR_TXRB) &&
        (iu_contty.conf & TX_EN) &&
        (iu_contty.stat & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &contty_dev,
                  "[iu_txrdy_b_irq()] Firing IRQ after transmit of %02x (%c)\n",
                  (uint8) iu_contty.txbuf, (char) iu_contty.txbuf);
        csr_data |= CSRUART;
    }
}

t_stat tti_reset(DEVICE *dptr)
{
    memset(&iu_state, 0, sizeof(IU_STATE));
    memset(&iu_console, 0, sizeof(IU_PORT));

    /* Input Port logic is inverted - 0 means set */
    iu_state.inprt = ~(IU_DCDA);

    /* Start the Console TTI polling loop */
    if (!sim_is_active(&tti_unit)) {
        sim_activate_after(&tti_unit, tti_unit.wait);
    }

    return SCPE_OK;
}

t_stat contty_reset(DEVICE *dtpr)
{
    char line_config[16];

    if (contty_ldsc == NULL) {
        contty_desc.ldsc =
            contty_ldsc =
            (TMLN *)calloc(1, sizeof(*contty_ldsc));
    }

    tmxr_set_port_speed_control(&contty_desc);
    tmxr_set_line_unit(&contty_desc, 0, contty_rcv_unit);
    tmxr_set_line_output_unit(&contty_desc, 0, contty_xmt_unit);
    tmxr_set_console_units(&tti_unit, &tto_unit);

    memset(&iu_state, 0, sizeof(IU_STATE));
    memset(&iu_contty, 0, sizeof(IU_PORT));

    /* DCD is off (inverted logic, 1 means off) */
    iu_state.inprt |= IU_DCDB;

    brg_reg = 0;
    brg_clk = BRG_DEFAULT;
    parity_sel = IU_PARITY_EVEN;
    bits_per_char = 7;

    sprintf(line_config, "%s-%d%s1",
            brg_rates[brg_reg][brg_clk],
            bits_per_char,
            parity[parity_sel]);

    tmxr_set_config_line(&contty_ldsc[0], line_config);

    /* Start the CONTTY polling loop */
    if (!sim_is_active(contty_rcv_unit)) {
        sim_activate_after(contty_rcv_unit, contty_rcv_unit->wait);
    }

    return SCPE_OK;
}

t_stat iu_timer_reset(DEVICE *dptr)
{
    memset(&iu_timer_state, 0, sizeof(IU_TIMER_STATE));

    return SCPE_OK;
}

/* Service routines */

t_stat iu_svc_tti(UNIT *uptr)
{
    int32 temp;

    sim_clock_coschedule(uptr, tmxr_poll);

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

    if (iu_console.conf & RX_EN) {
        if ((iu_console.stat & STS_FFL) == 0) {
            iu_console.rxbuf[iu_console.w_p] = (temp & 0xff);
            iu_console.w_p = (iu_console.w_p + 1) % IU_BUF_SIZE;
            if (iu_console.w_p == iu_contty.w_p) {
                iu_console.stat |= STS_FFL;
            }
        }
        iu_console.stat |= STS_RXR;
        iu_state.istat |= ISTS_RAI;
        if (iu_state.imr & IMR_RXRA) {
            csr_data |= CSRUART;
        }
    }

    return SCPE_OK;
}


t_stat iu_svc_tto(UNIT *uptr)
{
    /* If there's more DMA to do, do it */
    if (iu_console.dma && ((dma_state.mask >> DMA_IUA_CHAN) & 0x1) == 0) {
        iu_dma(DMA_IUA_CHAN, IUBASE+IUA_DATA_REG);
    } else {
        /* The buffer is now empty, we've transmitted, so set TXR */
        iu_console.stat |= STS_TXR;
        iu_state.istat |= 1;
        iu_txrdy_a_irq();
    }

    return SCPE_OK;
}

t_stat iu_svc_contty_rcv(UNIT *uptr)
{
    int32 temp, ln;

    if ((uptr->flags & UNIT_ATT) == 0) {
        return SCPE_OK;
    }

    /* Check for connect */
    if ((ln = tmxr_poll_conn(&contty_desc)) >= 0) {
        contty_ldsc[ln].rcve = 1;
        iu_state.inprt &= ~(IU_DCDB);
        iu_state.ipcr |= IU_DCDB;
        csr_data |= CSRUART;
    }

    /* Check for disconnect */
    if (!contty_ldsc[0].conn && (iu_state.inprt & IU_DCDB) == 0) {
        contty_ldsc[0].rcve = 0;
        iu_state.inprt |= IU_DCDB;
        iu_state.ipcr |= IU_DCDB;
        csr_data |= CSRUART;
    } else if (iu_contty.conf & RX_EN) {
        tmxr_poll_rx(&contty_desc);

        if (contty_ldsc[0].conn) {
            temp = tmxr_getc_ln(&contty_ldsc[0]);
            if (temp && !(temp & SCPE_BREAK)) {
                if ((iu_contty.stat & STS_FFL) == 0) {
                    iu_contty.rxbuf[iu_contty.w_p] = (temp & 0xff);
                    iu_contty.w_p = (iu_contty.w_p + 1) % IU_BUF_SIZE;
                    if (iu_contty.w_p == iu_contty.r_p) {
                        iu_contty.stat |= STS_FFL;
                    }
                }
                iu_contty.stat |= STS_RXR;
                iu_state.istat |= ISTS_RBI;
                if (iu_state.imr & IMR_RXRB) {
                    csr_data |= CSRUART;
                }
            }
        }
    }

    tmxr_clock_coschedule(uptr, tmxr_poll);

    return SCPE_OK;
}

t_stat iu_svc_contty_xmt(UNIT *uptr)
{
    dma_channel *chan = &dma_state.channels[DMA_IUB_CHAN];

    tmxr_poll_tx(&contty_desc);

    if (chan->wcount_c >= 0) {
        /* More DMA to do */
        iu_dma(DMA_IUB_CHAN, IUBASE+IUB_DATA_REG);
    } else {
        /* The buffer is now empty, we've transmitted, so set TXR */
        iu_contty.stat |= STS_TXR;
        iu_state.istat |= 0x10;
        iu_txrdy_b_irq();
    }

    return SCPE_OK;
}

t_stat iu_svc_timer(UNIT *uptr)
{
    iu_state.istat |= ISTS_CRI;

    if (iu_state.imr & IMR_CTR) {
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
    uint32 data;

    reg = (uint8) (pa - IUBASE);

    switch (reg) {
    case MR12A:
        modep = iu_console.modep;
        data = iu_console.mode[modep];
        iu_increment_a = TRUE;
        break;
    case SRA:
        data = iu_console.stat;
        break;
    case RHRA:
        if (iu_console.conf & RX_EN) {
            data = iu_console.rxbuf[iu_console.r_p];
            iu_console.r_p = (iu_console.r_p + 1) % IU_BUF_SIZE;
            /* If the FIFO is not empty, we must cause another interrupt
             * to continue reading */
            if (iu_console.r_p == iu_console.w_p) {
                iu_console.stat &= ~(STS_RXR|STS_FFL);
                iu_state.istat &= ~ISTS_RAI;
            } else if (iu_state.imr & IMR_RXRA) {
                csr_data |= CSRUART;
            }
        }
        break;
    case IPCR:
        data = iu_state.ipcr;
        /* Reading the port resets it */
        iu_state.ipcr = 0;
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
        modep = iu_contty.modep;
        data = iu_contty.mode[modep];
        iu_increment_b = TRUE;
        break;
    case SRB:
        data = iu_contty.stat;
        break;
    case RHRB:
        if (iu_contty.conf & RX_EN) {
            data = iu_contty.rxbuf[iu_contty.r_p];
            iu_contty.r_p = (iu_contty.r_p + 1) % IU_BUF_SIZE;
            /* If the FIFO is not empty, we must cause another interrupt
             * to continue reading */
            if (iu_contty.r_p == iu_contty.w_p) {
                iu_contty.stat &= ~(STS_RXR|STS_FFL);
                iu_state.istat &= ~ISTS_RBI;
            } else if (iu_state.imr & IMR_RXRB) {
                csr_data |= CSRUART;
            }
        }
        break;
    case INPRT:
        data = iu_state.inprt;
        break;
    case START_CTR:
        data = 0;
        iu_state.istat &= ~ISTS_CRI;
        sim_activate_abs(&iu_timer_unit, (int32)(iu_timer_state.c_set * IU_TIMER_RATE));
        break;
    case STOP_CTR:
        data = 0;
        iu_state.istat &= ~ISTS_CRI;
        csr_data &= ~CSRUART;
        sim_cancel(&iu_timer_unit);
        break;
    case 17: /* Clear DMAC interrupt */
        data = 0;
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
    uint8 bval = (uint8) val;
    char  line_config[16];

    reg = (uint8) (pa - IUBASE);

    switch (reg) {
    case MR12A:
        modep = iu_console.modep;
        iu_console.mode[modep] = bval;
        iu_increment_a = TRUE;
        break;
    case CSRA:
        if (brg_rates[brg_reg][brg_clk] != NULL) {
            sprintf(line_config, "%s-%d%s1",
                    brg_rates[brg_reg][brg_clk],
                    bits_per_char,
                    parity[parity_sel]);

            sim_debug(EXECUTE_MSG, &contty_dev,
                      "Setting CONTTY line to %s\n",
                      line_config);

            tmxr_set_config_line(&contty_ldsc[0], line_config);
        }
        break;
    case CRA:  /* Command A */
        iu_w_cmd(PORT_A, bval);
        break;
    case THRA:  /* TX/RX Buf A */
        iu_tx(PORT_A, bval);
        sim_activate_abs(&tto_unit, tto_unit.wait);
        break;
    case ACR:  /* Auxiliary Control Register */
        iu_state.acr = bval;
        brg_reg = (bval >> 7) & 1;
        break;
    case IMR:
        iu_state.imr = bval;
        csr_data &= ~CSRUART;
        /* Possibly cause an interrupt */
        iu_txrdy_a_irq();
        iu_txrdy_b_irq();
        break;
    case CTUR:  /* Counter/Timer Upper Preset Value */
        /* Clear out high byte */
        iu_timer_state.c_set &= 0x00ff;
        /* Set high byte */
        iu_timer_state.c_set |= ((uint16) bval << 8);
        break;
    case CTLR:  /* Counter/Timer Lower Preset Value */
        /* Clear out low byte */
        iu_timer_state.c_set &= 0xff00;
        /* Set low byte */
        iu_timer_state.c_set |= bval;
        break;
    case MR12B:
        modep = iu_contty.modep;
        iu_contty.mode[modep] = bval;
        iu_increment_b = TRUE;
        if (modep == 0) {
            if ((bval >> 4) & 1) {
                /* No parity */
                parity_sel = IU_PARITY_NONE;
            } else {
                /* Parity enabled */
                if (bval & 4) {
                    parity_sel = IU_PARITY_ODD;
                } else {
                    parity_sel = IU_PARITY_EVEN;
                }
            }

            bits_per_char = (bval & 3) + 5;
        }
        break;
    case CRB:  /* Command B */
        iu_w_cmd(PORT_B, bval);
        break;
    case CSRB:
        brg_clk = (bval >> 4) & 0xf;

        if (brg_rates[brg_reg][brg_clk] != NULL) {
            sprintf(line_config, "%s-%d%s1",
                    brg_rates[brg_reg][brg_clk],
                    bits_per_char,
                    parity[parity_sel]);

            sim_debug(EXECUTE_MSG, &contty_dev,
                      "Setting CONTTY line to %s\n",
                      line_config);

            tmxr_set_config_line(&contty_ldsc[0], line_config);
        }

        break;
    case THRB: /* TX/RX Buf B */
        iu_tx(PORT_B, bval);
        sim_activate_abs(contty_xmt_unit, contty_ldsc[0].txdelta);
        break;
    case OPCR:
        iu_state.opcr = bval;
        break;
    case SOPR:
        /* Bit 2 of the IU output register is used as a soft power
         * switch. When set, the machine will power down
         * immediately. */
        if (bval & IU_KILLPWR) {
            stop_reason = STOP_POWER;
        }
        break;
    case ROPR:
        break;
    default:
        break;
    }
}

t_stat iu_tx(uint8 portno, uint8 val)
{
    IU_PORT *p = (portno == PORT_A) ? &iu_console : &iu_contty;
    UNIT *uptr = (portno == PORT_A) ? &tto_unit : contty_xmt_unit;
    uint8 ists = (portno == PORT_A) ? ISTS_RAI : ISTS_RBI;
    uint8 imr_mask = (portno == PORT_A) ? IMR_RXRA : IMR_RXRB;
    int32 c;
    t_stat status = SCPE_OK;

    if (p->conf & TX_EN) {
        if ((p->mode[1] & 0xc0) == 0x80) {            /* Loopback mode */
            p->txbuf = val;

            /* This is also a Receive */
            if ((p->stat & STS_FFL) == 0) {
                p->rxbuf[p->w_p] = val;
                p->w_p = (p->w_p + 1) % IU_BUF_SIZE;
                if (p->w_p == p->r_p) {
                    p->stat |= STS_FFL;
                }
            }

            p->stat |= STS_RXR;
            if (iu_state.imr & imr_mask) {
                iu_state.istat |= ists;
                csr_data |= CSRUART;
            }

            return SCPE_OK;
        } else {                                      /* Direct mode */
            c = sim_tt_outcvt(val, TTUF_MODE_8B);

            if (c >= 0) {
                p->txbuf = c;
                p->stat &= ~(STS_TXR|STS_TXE);
                iu_state.istat &= ~(1 << (portno*4));

                if (portno == PORT_A) {
                    /* Write the character to the SIMH console */
                    sim_debug(EXECUTE_MSG, &tto_dev,
                              "[iu_tx] CONSOLE transmit %02x (%c)\n",
                              (uint8) c, (char) c);
                    status = sim_putchar_s(c);
                } else {
                    sim_debug(EXECUTE_MSG, &contty_dev,
                              "[iu_tx] CONTTY transmit %02x (%c)\n",
                              (uint8) c, (char) c);
                    status = tmxr_putc_ln(&contty_ldsc[0], c);
                }
            }
        }
    }

    return status;
}

static SIM_INLINE void iu_w_cmd(uint8 portno, uint8 cmd)
{

    IU_PORT *p;

    if (portno == 0) {
        p = &iu_console;
    } else {
        p = &iu_contty;
    }

    /* Enable or disable transmitter        */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DTX) {
        p->conf &= ~TX_EN;
        p->stat &= ~STS_TXR;
        p->stat &= ~STS_TXE;
        p->drq = FALSE;
        p->dma = FALSE;
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

/*
 * Initiate DMA transfer or continue one already in progress.
 */
void iu_dma(uint8 channel, uint32 service_address)
{
    uint8 data;
    uint32 addr;
    t_stat status = SCPE_OK;
    dma_channel *chan = &dma_state.channels[channel];
    UNIT *uptr = (channel == DMA_IUA_CHAN) ? &tto_unit : contty_xmt_unit;
    IU_PORT *port = (channel == DMA_IUA_CHAN) ? &iu_console : &iu_contty;

    /* Immediate acknowledge of DMA */
    port->drq = FALSE;

    if (!port->dma) {
        /* Set DMA transfer type */
        port->dma = 1u << ((dma_state.mode >> 2) & 0xf);
    }

    if (port->dma == DMA_READ) {
        addr = dma_address(channel, chan->ptr, TRUE);
        chan->addr_c = chan->addr + chan->ptr + 1;
        data = pread_b(addr);
        status = iu_tx(channel - 2, data);
        if (status == SCPE_OK) {
            chan->ptr++;
            chan->wcount_c--;
        } else if (status == SCPE_LOST) {
            chan->ptr = 0;
            chan->wcount_c = -1;
        }

        sim_activate_abs(uptr, uptr->wait);

        if (chan->wcount_c >= 0) {
            /* Return early so we don't finish DMA */
            return;
        }
    }

    /* Done with DMA */
    port->dma = DMA_NONE;

    dma_state.mask |= (1 << channel);
    dma_state.status |= (1 << channel);
    csr_data |= CSRDMA;
}
