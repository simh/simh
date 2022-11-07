/* 3b2_iu.c: SCN2681A Dual UART

   Copyright (c) 2017-2022, Seth J. Morabito

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

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_dmac.h"
#include "3b2_mem.h"
#include "3b2_stddev.h"
#include "3b2_timer.h"

/*
 * The 3B2/400 and 3B2/700 both have two on-board serial ports,
 * labeled CONSOLE and CONTTY. The CONSOLE port is the system console.
 * The CONTTY port serves as a secondary serial port for one
 * additional terminal.
 *
 * These lines are driven by an SCN2681A Dual UART, with two receivers
 * and two transmitters.
 *
 * In addition to the two TX/RX ports, the SCN27681A also has one
 * programmable timer that is used in the 3B2 for various one-shot
 * timing tasks.
 *
 * The SCN2681A UART is represented here by four devices:
 *
 *   - Console TTI (Console Input, port A)
 *   - Console TTO (Console Output, port A)
 *   - CONTTY (I/O, port B. Terminal multiplexer with one line)
 *   - IU Timer
 */

#if defined(REV3)
#define DMA_INT  INT_UART_DMA
#else
#define DMA_INT  INT_DMA
#endif

#define UPDATE_IRQ do {                         \
        if (iu_state.imr & iu_state.isr) {      \
            CPU_SET_INT(INT_UART);              \
            SET_CSR(CSRUART);                   \
        } else {                                \
            CPU_CLR_INT(INT_UART);              \
            CLR_CSR(CSRUART);                   \
        }                                       \
    } while (0)

#define SET_DMA_INT do {                        \
        CPU_SET_INT(DMA_INT);                   \
        SET_CSR(CSRDMA);                        \
    } while (0)

#define CLR_DMA_INT do {                        \
        CPU_CLR_INT(DMA_INT);                   \
        CLR_CSR(CSRDMA);                        \
    } while (0)

#define LOOPBACK(P)    (((P)->mode[1] & 0xc0) == 0x80)
#define TX_ENABLED(P)  ((P).conf & TX_EN)
#define PORTNO(P)      ((P) == &iu_console ? PORT_A : PORT_B)

/* Static function declarations */
static void iu_w_cmd(IU_PORT *port, uint8 val);
static t_stat iu_tx(IU_PORT *port, uint8 val);
static void iu_rx(IU_PORT *port, uint8 val);
static uint8 iu_rx_getc(IU_PORT *port);

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

double iu_timer_multiplier = IU_TIMER_MULTIPLIER;

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

UNIT tti_unit = { UDATA(&iu_svc_tti, UNIT_IDLE|TT_MODE_8B, 0), SERIAL_IN_WAIT };

REG tti_reg[] = {
    { HRDATADF(SRA, iu_console.sr, 8, "Status", sr_bits) },
    { HRDATADF(CONF, iu_console.conf, 8, "Config", conf_bits) },
    { BRDATAD(DATA, iu_console.rxbuf, 16, 8, IU_BUF_SIZE, "Data") },
    { DRDATAD(POS, tti_unit.pos, T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD(TIME, tti_unit.wait, 24, "input polling interval"), PV_LEFT },
    { NULL }
};

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, NULL,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

/* TTO (Console) data structures */

UNIT tto_unit = { UDATA(&iu_svc_tto, UNIT_IDLE|TT_MODE_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { HRDATADF(SRA,   iu_console.sr,    8, "Status Register", sr_bits) },
    { HRDATADF(ISR,   iu_state.isr,     8, "Interrupt Status", isr_bits) },
    { HRDATAD(IMR,    iu_state.imr,     8, "Interrupt Mask") },
    { HRDATADF(ACR,   iu_state.acr,     8, "Aux. Control Register", acr_bits) },
    { NULL }
};

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

TMLN contty_ldsc[1] = { 0 };
TMXR contty_desc = { 1, 0, 0, contty_ldsc };  /* One fixed line */

UNIT contty_unit[2] = {
    { UDATA(&iu_svc_contty, UNIT_IDLE|UNIT_ATTABLE|TT_MODE_8B, 0), SERIAL_IN_WAIT },
    { UDATA(&iu_svc_contty_xmt, UNIT_IDLE|UNIT_DIS, 0), SERIAL_OUT_WAIT }
};

REG contty_reg[] = {
    { HRDATADF(SRB,   iu_contty.sr,    8, "Status Register", sr_bits) },
    { HRDATADF(CONF,  iu_contty.conf,  8, "Config", conf_bits) },
    { BRDATAD(RXDATA, iu_contty.rxbuf, 16, 8, IU_BUF_SIZE, "RX Data") },
    { HRDATADF(ISR,   iu_state.isr,    8, "Interrupt Status", isr_bits) },
    { HRDATAD(IMR,    iu_state.imr,    8, "Interrupt Mask") },
    { HRDATADF(ACR,   iu_state.acr,    8, "Auxiliary Control Register", acr_bits) },
    { DRDATAD(TIME,   contty_unit[1].wait, 24, "output character delay"), PV_LEFT },
    { NULL }
};

MTAB contty_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *)&contty_desc, "Display a summary of line state" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *)&contty_desc, "Display current connection" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *)&contty_desc, "Display CONTTY statistics" }
};

CONST char *brg_rates[IU_SPEED_REGS][IU_SPEEDS] = {
    {"50",    "110",   "134.5", "200",
     "300",   "600",   "1200",  "1050",
     "2400",  "4800",  "7200",  "9600",
     "38400", NULL,    NULL,    NULL},
    {"75",    "110",   "134.5", "150",
     "300",   "600",   "1200",  "2000",
     "2400",  "4800",  "1800",  "9600",
     "19200", NULL,    NULL,    NULL}
};

CONST char *parity[3] = {"O", "E", "N"};

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
    "CONTTY", contty_unit, contty_reg, contty_mod,
    2, 8, 32, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &contty_reset,
    NULL, &contty_attach, &contty_detach,
    NULL, DEV_DISABLE|DEV_DEBUG|DEV_MUX,
    0, contty_deb_tab, NULL, NULL,
    NULL, NULL,
    (void *)&contty_desc,
    NULL
};

/* IU Timer data structures */

MTAB iu_timer_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MULT", "MULT={1|2|3|4}",
      &iu_timer_set_mult, &iu_timer_show_mult, NULL, "Timer Multiplier" }
};

REG iu_timer_reg[] = {
    { HRDATAD(CTR_SET, iu_timer_state.c_set, 16, "Counter Setting") },
    { NULL }
};

UNIT iu_timer_unit = { UDATA(&iu_svc_timer, UNIT_IDLE, 0) };

DEVICE iu_timer_dev = {
    "IUTIMER", &iu_timer_unit, iu_timer_reg, iu_timer_mod,
    1, 8, 32, 1, 8, 8,
    NULL, NULL, &iu_timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat iu_timer_show_mult(FILE *st, UNIT *uptr, int val, const void *desc)
{
    fprintf(st, "mult=%d", (int) iu_timer_multiplier);
    return SCPE_OK;
}

t_stat iu_timer_set_mult(UNIT *uptr, int32 val, const char *cptr, void *desc)
{
    t_stat r;
    t_value v;
    v = get_uint(cptr, 10, 8, &r);
    if (r != SCPE_OK) {
        return r;
    }
    if (v < 1 || v > 4) {
        return SCPE_ARG;
    }
    iu_timer_multiplier = (uint32) v;
    return SCPE_OK;

}

uint8 brg_reg = 0;       /* Selected baud-rate generator register */
uint8 brg_clk = 11;      /* Selected baud-rate generator clock */
uint8 parity_sel = 1;    /* Selected parity */
uint8 bits_per_char = 7;

t_stat contty_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    TMLN *lp;
    char line_config[16];

    /* Set initial line speed */
    brg_reg = 0;
    brg_clk = BRG_DEFAULT;
    parity_sel = IU_PARITY_EVEN;
    bits_per_char = 7;

    sprintf(line_config, "%s-%d%s1",
            brg_rates[brg_reg][brg_clk],
            bits_per_char,
            parity[parity_sel]);

    tmxr_set_config_line(&contty_ldsc[0], line_config);

    if ((sim_switches & SWMASK('M'))) {
        tmxr_set_modem_control_passthru(&contty_desc);
    }

    tmxr_set_line_output_unit(&contty_desc, 0, &contty_unit[1]);

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

t_stat tti_reset(DEVICE *dptr)
{
    memset(&iu_state, 0, sizeof(IU_STATE));
    memset(&iu_console, 0, sizeof(IU_PORT));

    /* Input Port is active low */
    iu_state.inprt = ~IU_DCDA;
    iu_state.ipcr = IU_DCDA_CH | (0xf & ~IU_DCDA);

    tmxr_set_console_units(&tti_unit, &tto_unit);

    /* Start the Console TTI polling loop */
    sim_activate_after(&tti_unit, tti_unit.wait);

    return SCPE_OK;
}

t_stat contty_reset(DEVICE *dtpr)
{
    sim_set_uname(&contty_unit[0], "CONTTY-RCV");
    sim_set_uname(&contty_unit[1], "CONTTY-XMT");

    tmxr_set_port_speed_control(&contty_desc);

    memset(&iu_contty, 0, sizeof(IU_PORT));

    if (contty_unit[0].flags & UNIT_ATT) {
        sim_activate_after(&contty_unit[0], contty_unit[0].wait);
    } else {
        sim_cancel(&contty_unit[0]);
    }

    return SCPE_OK;
}

static void iu_rx(IU_PORT *port, uint8 val)
{
    if (port->conf & RX_EN) {
        if (!(port->sr & STS_FFL)) {
            /* If not full, we're reading into the FIFO */
            port->rxbuf[port->w_p] = val;
            port->w_p = (port->w_p + 1) % IU_BUF_SIZE;
            if (port->w_p == port->r_p) {
                port->sr |= STS_FFL;
            }
        } else {
            /* FIFO is full, and now we're going to have to hold a
             * character in the shift register until space is
             * available */

            /* If the register already had data, it's going to be
             * overwritten, so we have to set the overflow flag */
            if (port->rxr_full) {
                port->sr |= STS_OER;
            }

            /* Save the character */
            port->rxr = val;
            port->rxr_full = TRUE;
        }

        port->sr |= STS_RXR;
        iu_state.isr |= (port == &iu_console) ? ISTS_RXRA : ISTS_RXRB;
    }

    UPDATE_IRQ;
}

static uint8 iu_rx_getc(IU_PORT *port)
{
    uint8 val = 0;

    if (port->conf & RX_EN) {
        val = port->rxbuf[port->r_p];
        port->r_p = (port->r_p + 1) % IU_BUF_SIZE;
        /* No longer full */
        port->sr &= ~STS_FFL;
        if (port->r_p == port->w_p) {
            /* Empty FIFO, nothing left to read */
            port->sr &= ~STS_RXR;
            iu_state.isr &= (port == &iu_console) ? ~ISTS_RXRA : ~ISTS_RXRB;
        }

        if (port->rxr_full) {
            /* Need to shift data from the Receive Shift Register into
               the space that we just freed up */
            port->rxbuf[port->w_p] = port->rxr;
            port->w_p = (port->w_p + 1) % IU_BUF_SIZE;
            /* FIFO can logically never be full here, since we just
             * freed up space above. */
            port->rxr_full = FALSE;
        }

        if (!(port->mode[0] & 0x20)) {
            /* Receiver is in "Char Error" mode, so reset SR error
               bits on read */
            port->sr &= ~(STS_RXB|STS_FER|STS_PER);
        }
    }

    UPDATE_IRQ;

    return val;
}

t_stat iu_timer_reset(DEVICE *dptr)
{
    memset(&iu_timer_state, 0, sizeof(IU_TIMER_STATE));

    return SCPE_OK;
}

/* Service routines */

t_stat iu_svc_tti(UNIT *uptr)
{
    int32 c;

    tmxr_clock_coschedule(uptr, tmxr_poll);

    if ((c = sim_poll_kbd()) < SCPE_KFLAG) {
        return c;
    }

    iu_rx(&iu_console, (uint8) c);

    return SCPE_OK;
}


t_stat iu_svc_tto(UNIT *uptr)
{
    t_stat result;
    dma_channel *chan = &dma_state.channels[DMA_IUA_CHAN];

    /* Check for data in the transmitter shift register that's ready
     * to go out to the TX line */
    if (iu_console.tx_state & T_XMIT) {
        if (LOOPBACK(&iu_console)) {
            /* Handle loopback mode if set */
            sim_debug(EXECUTE_MSG, &tto_dev, "iu_svc_tto: CONSOLE is in loopback.\n");
            iu_console.tx_state &= ~T_XMIT;

            iu_rx(&iu_console, iu_console.txr);

            if (TX_ENABLED(iu_console) && !(iu_console.tx_state & T_HOLD)) {
                iu_console.sr |= STS_TXE;
            }
        } else {
            /* Direct mode, no loopback */
            result = sim_putchar_s(iu_console.txr);
            if (result == SCPE_STALL) {
                sim_debug(EXECUTE_MSG, &tto_dev, "iu_svc_tto: CONSOLE PUTC STALL\n");
                sim_activate_after(uptr, 1000);
                return SCPE_OK;
            } else {
                iu_console.tx_state &= ~T_XMIT;
                if (TX_ENABLED(iu_console) && !(iu_console.tx_state & T_HOLD)) {
                    iu_console.sr |= STS_TXE;
                }
            }
        }
    }

    /* Check for data in the holding register that's ready to go
     * out to the transmitter shift register */
    if (iu_console.tx_state & T_HOLD) {
        iu_console.tx_state &= ~T_HOLD;
        iu_console.tx_state |= T_XMIT;
        iu_console.txr = iu_console.thr;
        /* If the transmitter is currently enabled, we need to update
         * the TxRDY and TxEMT flags */
        if (TX_ENABLED(iu_console)) {
            iu_console.sr &= ~STS_TXE;
            iu_console.sr |= STS_TXR;
            iu_state.isr |= ISTS_TXRA;
            /* DRQ is always tied to TxRDY */
            iu_console.drq = TRUE;
        }

        sim_activate_after_abs(uptr, uptr->wait);
    }

    UPDATE_IRQ;

    /* If we're done transmitting and there's more DMA to do,
     * do it. */
    if (!iu_console.tx_state &&
        chan->wcount_c >= 0 &&
        ((dma_state.mask >> DMA_IUA_CHAN) & 0x1) == 0) {
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "iu_svc_tto: Triggering next DMA\n");
        iu_dma_console(DMA_IUA_CHAN, IUBASE+IUA_DATA_REG);
    }

    return SCPE_OK;
}

t_stat iu_svc_contty(UNIT *uptr)
{
    int32 c;

    if ((uptr->flags & UNIT_ATT) == 0) {
        return SCPE_OK;
    }

    /* Check for connect of our single line */
    if (tmxr_poll_conn(&contty_desc) == 0) {
        contty_ldsc[0].rcve = 1;

        iu_state.inprt &= ~IU_DCDB;
        iu_state.ipcr &= ~IU_DCDB;
        iu_state.ipcr |= IU_DCDB_CH;

        UPDATE_IRQ;
    }

    tmxr_poll_tx(&contty_desc);
    tmxr_poll_rx(&contty_desc);

    /* Check for disconnect */
    if (!contty_ldsc[0].conn && contty_ldsc[0].rcve) {
        contty_ldsc[0].rcve = 0;
        iu_state.inprt |= IU_DCDB;
        iu_state.ipcr |= (IU_DCDB_CH|IU_DCDB);

        UPDATE_IRQ;
    }

    /* Check for RX */
    if ((iu_contty.conf & RX_EN) && contty_ldsc[0].conn) {
        c = tmxr_getc_ln(&contty_ldsc[0]);
        if (c && !(c & SCPE_BREAK)) {
            iu_rx(&iu_contty, (uint8) c);
        }
    }

    tmxr_clock_coschedule(uptr, tmxr_poll);

    return SCPE_OK;
}

t_stat iu_svc_contty_xmt(UNIT *uptr)
{
    dma_channel *chan = &dma_state.channels[DMA_IUB_CHAN];
    TMLN *lp = &contty_ldsc[0];
    t_stat result;

    /* Check for data in the transmitter shift register that's ready
     * to go out to the TX line */
    if (iu_contty.tx_state & T_XMIT) {
        if (LOOPBACK(&iu_contty)) {
            /* Handle loopback mode if set */
            sim_debug(EXECUTE_MSG, &contty_dev, "iu_svc_contty: CONTTY is in loopback.\n");
            iu_contty.tx_state &= ~T_XMIT;

            iu_rx(&iu_contty, iu_contty.txr);

            if (TX_ENABLED(iu_contty) && !(iu_contty.tx_state & T_HOLD)) {
                iu_contty.sr |= STS_TXE;
            }
        } else {
            /* Direct mode, no loopback */
            result = tmxr_putc_ln(lp, iu_contty.txr);
            if (result == SCPE_STALL) {
                sim_debug(EXECUTE_MSG, &contty_dev, "iu_svc_contty: CONTTY PUTC STALL: %d\n", result);
                sim_activate_after(uptr, 1000);
                return SCPE_OK;
            } else {
                tmxr_poll_tx(&contty_desc);
                iu_contty.tx_state &= ~T_XMIT;
                if (TX_ENABLED(iu_contty) && !(iu_contty.tx_state & T_HOLD)) {
                    iu_contty.sr |= STS_TXE;
                }
            }
        }
    }

    /* Check for data in the holding register that's ready to go
     * out to the transmitter shift register */
    if (iu_contty.tx_state & T_HOLD) {
        sim_debug(EXECUTE_MSG, &contty_dev,
                  "THRB->TXRB: 0x%02x (%c)\n",
                  iu_contty.thr, PCHAR(iu_contty.thr));
        iu_contty.tx_state &= ~T_HOLD;
        iu_contty.tx_state |= T_XMIT;
        iu_contty.txr = iu_contty.thr;
        /* If the transmitter is currently enabled, we need to update
         * the TxRDY and TxEMT flags */
        if (TX_ENABLED(iu_contty)) {
            iu_contty.sr &= ~STS_TXE;
            iu_contty.sr |= STS_TXR;
            iu_state.isr |= ISTS_TXRB;
            /* DRQ is always tied to TxRDY */
            iu_contty.drq = TRUE;
        }

        sim_activate_after_abs(uptr, uptr->wait);
    }

    UPDATE_IRQ;

    /* If we're done transmitting and there's more DMA to do,
     * do it. */
    if (!iu_contty.tx_state &&
        chan->wcount_c >= 0 &&
        ((dma_state.mask >> DMA_IUB_CHAN) & 0x1) == 0) {
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "iu_svc_contty_xmt: Triggering next DMA\n");
        iu_dma_contty(DMA_IUB_CHAN, IUBASE+IUB_DATA_REG);
    }

    return SCPE_OK;
}

t_stat iu_svc_timer(UNIT *uptr)
{
    iu_state.isr |= ISTS_CRI;

    sim_debug(EXECUTE_MSG, &iu_timer_dev,
              "[iu_svc_timer] IMR=%02x ISR=%02x => %02x\n",
              iu_state.imr, iu_state.isr, (iu_state.imr & iu_state.isr));

    UPDATE_IRQ;
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
    uint32 data = 0;

    reg = (uint8) (pa - IUBASE);

    switch (reg) {
    case MR12A:
        modep = iu_console.modep;
        data = iu_console.mode[modep];
        iu_increment_a = TRUE;
        break;
    case SRA:
        data = iu_console.sr;
        break;
    case RHRA:
        data = iu_rx_getc(&iu_console);
        break;
    case IPCR:
        data = iu_state.ipcr;
        /* Reading the port resets the top 4 bits */
        iu_state.ipcr &= 0xf;
        break;
    case ISR:
        data = iu_state.isr;
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
        data = iu_contty.sr;
        break;
    case RHRB:
        data = iu_rx_getc(&iu_contty);
        break;
    case INPRT:
        data = iu_state.inprt;
        break;
    case START_CTR:
        data = 0;
        iu_state.isr &= ~ISTS_CRI;
        sim_debug(EXECUTE_MSG, &iu_timer_dev,
                  "ACR=%02x : Activating IU Timer in %d ticks / %d microseconds\n",
                  iu_state.acr, iu_timer_state.c_set, (int32)(iu_timer_state.c_set * iu_timer_multiplier));
        sim_activate_after(&iu_timer_unit, (int32)(iu_timer_state.c_set * iu_timer_multiplier));
        break;
    case STOP_CTR:
        data = 0;
        iu_state.isr &= ~ISTS_CRI;
        UPDATE_IRQ;
        sim_cancel(&iu_timer_unit);
        break;
    case 17: /* Clear DMA interrupt */
        data = 0;
        CLR_DMA_INT;
        break;
    default:
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
        /* Nothing supported */
        break;
    case CRA:  /* Command A */
        iu_w_cmd(&iu_console, bval);
        break;
    case THRA:  /* TX/RX Buf A */
        iu_tx(&iu_console, bval);
        break;
    case ACR:  /* Auxiliary Control Register */
        iu_state.acr = bval;
        brg_reg = (bval >> 7) & 1;
        break;
    case IMR:
        iu_state.imr = bval;
        UPDATE_IRQ;
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
        sim_debug(EXECUTE_MSG, &tto_dev, "MR12B: Page %d Mode = %02x\n", modep, bval);
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
    case CRB:
        iu_w_cmd(&iu_contty, bval);
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
    case THRB:
        iu_tx(&iu_contty, bval);
        break;
    case OPCR:
        iu_state.opcr = bval;
        break;
    case SOPR:
#if defined (REV2)
        /* Bit 2 of the IU output register is used as a soft power
         * switch. When set, the machine will power down
         * immediately. */
        if (bval & IU_KILLPWR) {
            stop_reason = STOP_POWER;
        }
#endif
        break;
    case ROPR:
        break;
    case 17: /* Clear DMA interrupt */
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "[WRITE] Clear DMA interrupt in UART\n");
        CLR_DMA_INT;
        break;
    default:
        break;
    }
}

/*
 * Transmit a single character
 */
t_stat iu_tx(IU_PORT *port, uint8 val)
{
    int32 c;
    uint8 tx_ists = (port == &iu_console) ? ISTS_TXRA : ISTS_TXRB;
    UNIT *uptr = (port == &iu_console) ? &tto_unit : &contty_unit[1];

    sim_debug(EXECUTE_MSG, &tto_dev,
              "iu_tx PORT=%d CHAR=%02x (%c)\n",
              PORTNO(port), val, PCHAR(val));

    if (!(port->conf & TX_EN) || !(port->sr & STS_TXR)) {
        sim_debug(EXECUTE_MSG, &tto_dev,
                  ">>> IGNORING TRANSMIT, NOT ENABLED OR NOT READY!!!\n");
        return SCPE_INCOMP;
    }

    c = sim_tt_outcvt(val, TTUF_MODE_8B);

    if (c >= 0) {
        port->tx_state |= T_HOLD;
        port->sr &= ~(STS_TXR|STS_TXE);
        port->drq = FALSE;
        iu_state.isr &= ~(tx_ists);
        port->thr = c;
        sim_activate_after(uptr, uptr->wait);
    }

    return SCPE_OK;
}

static void iu_w_cmd(IU_PORT *port, uint8 cmd)
{
    uint8 tx_ists = (port == &iu_console) ? ISTS_TXRA : ISTS_TXRB;
    uint8 dbk_ists = (port == &iu_console) ? ISTS_DBA : ISTS_DBB;

    /* Enable or disable transmitter        */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DTX) {
        port->conf &= ~TX_EN;
        port->sr &= ~(STS_TXR|STS_TXE);
        port->drq = FALSE;
        iu_state.isr &= ~tx_ists;
        UPDATE_IRQ;
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "DISABLE TX, PORT %d\n", PORTNO(port));
    } else if (cmd & CMD_ETX) {
        if (!(port->conf & TX_EN)) {
            /* TXE and TXR are always set by an ENABLE if prior state
               was DISABLED */
            port->sr |= (STS_TXR|STS_TXE);
            port->drq = TRUE;
        }
        port->conf |= TX_EN;
        iu_state.isr |= tx_ists;
        UPDATE_IRQ;
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "ENABLE TX, PORT %d\n", PORTNO(port));
    }

    /* Enable or disable receiver.          */
    /* Disable always wins, if both are set */
    if (cmd & CMD_DRX) {
        port->conf &= ~RX_EN;
        port->sr &= ~STS_RXR;
    } else if (cmd & CMD_ERX) {
        port->conf |= RX_EN;
    }

    /* Command register bits 6-4 have special meaning */
    switch ((cmd >> CMD_MISC_SHIFT) & CMD_MISC_MASK) {
    case CR_RST_MR:
        /*  Causes the Channel A MR pointer to point to MR1. */
        port->modep = 0;
        break;
    case CR_RST_RX:
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: RESET RX\n", PORTNO(port));
        /* Reset receiver. Resets the Channel's receiver as if a
           hardware reset had been applied. The receiver is disabled
           and the FIFO is flushed. */
        port->sr &= ~STS_RXR;
        port->conf &= ~RX_EN;
        port->w_p = 0;
        port->r_p = 0;
        break;
    case CR_RST_TX:
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: RESET TX\n", PORTNO(port));
        /* Reset transmitter. Resets the Channel's transmitter as if a
           hardware reset had been applied. */
        port->sr &= ~STS_TXR;
        port->sr &= ~STS_TXE;
        port->drq = FALSE;           /* drq is tied to TXR */
        port->conf &= ~TX_EN;
        break;
    case CR_RST_ERR:
        /* Reset error status. Clears the Channel's Received Break,
           Parity Error, Framing Error, and Overrun Error bits in the
           status register (SRn[7:4]). */
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: RESET ERROR\n", PORTNO(port));
        port->sr &= ~(STS_RXB|STS_FER|STS_PER|STS_OER);
        break;
    case CR_RST_BRK:
        /* Reset Channel's break change interrupt. Causes the Channel
           A break detect change bit in the interrupt status register
           (ISR[2] for Chan. A, ISR[6] for Chan. B) to be cleared to
           zero. */
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: RESET BREAK IRQ\n", PORTNO(port));
        iu_state.isr &= ~dbk_ists;
        break;
    case CR_START_BRK:
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: START BREAK. loopback=%d\n",
                  PORTNO(port), LOOPBACK(port));
        if (LOOPBACK(port)) {
            /* Set "Received Break" and "Parity Error" bits in
               SRA/SRB */
            port->sr |= (STS_RXB|STS_PER);
            /* Set "Delta Break" bit A or B in ISR */
            iu_state.isr |= dbk_ists;
        }
        break;
    case CR_STOP_BRK:
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "PORT %d Command: STOP BREAK. loopback=%d\n",
                  PORTNO(port), LOOPBACK(port));
        if (LOOPBACK(port)) {
            /* Set "Delta Break" bit A or B in ISR */
            iu_state.isr |= dbk_ists;
        }
        break;
    }

    UPDATE_IRQ;
}

/*
 * Initiate DMA transfer or continue one already in progress.
 */
void iu_dma_console(uint8 channel, uint32 service_address)
{
    uint8 data;
    uint32 addr;
    t_stat status = SCPE_OK;
    dma_channel *chan = &dma_state.channels[channel];
    IU_PORT *port = &iu_console;

    /* If we're doing DMA and we're done, end it */
    if (iu_console.dma && chan->wcount_c < 0) {
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "iu_svc_tto: DMA Complete.\n");
        iu_console.dma = FALSE;
        dma_state.mask |= (1 << DMA_IUA_CHAN);
        dma_state.status |= (1 << DMA_IUA_CHAN);
        SET_DMA_INT;
        return;
    }

    /* Mark that IUA is in DMA */
    port->dma = TRUE;

    switch (DMA_XFER(DMA_IUA_CHAN)) {
    case DMA_XFER_READ:
        addr = dma_address(channel, chan->ptr);
        chan->addr_c = addr;
        data = pread_b(addr, BUS_PER);
        status = iu_tx(port, data);
        if (status == SCPE_OK) {
            chan->ptr++;
            chan->wcount_c--;
        }
        break;
    default:
        sim_debug(EXECUTE_MSG, &tto_dev,
                  "iu_dma_console: Error, transfer type %d not supported\n",
                  DMA_XFER(DMA_IUA_CHAN));
        break;
    }
}

void iu_dma_contty(uint8 channel, uint32 service_address)
{
    uint8 data;
    uint32 addr;
    t_stat status = SCPE_OK;
    dma_channel *chan = &dma_state.channels[channel];
    IU_PORT *port = &iu_contty;

    /* If we're doing DMA and we're done, end it */
    if (iu_contty.dma && chan->wcount_c < 0) {
        sim_debug(EXECUTE_MSG, &contty_dev,
                  "iu_svc_contty_xmt: DMA Complete.\n");
        iu_contty.dma = FALSE;
        dma_state.mask |= (1 << DMA_IUB_CHAN);
        dma_state.status |= (1 << DMA_IUB_CHAN);
        SET_DMA_INT;
        return;
    }

    /* Mark that IUB is in DMA */
    port->dma = TRUE;

    switch (DMA_XFER(DMA_IUB_CHAN)) {
    case DMA_XFER_READ:
        addr = dma_address(channel, chan->ptr);
        chan->addr_c = addr;
        data = pread_b(addr, BUS_PER);
        status = iu_tx(port, data);
        if (status == SCPE_OK) {
            chan->ptr++;
            chan->wcount_c--;
        }
        break;
    default:
        sim_debug(EXECUTE_MSG, &contty_dev,
                  "iu_dma_contty: Error, transfer type %d not supported\n",
                  DMA_XFER(DMA_IUB_CHAN));
        break;
    }
}
