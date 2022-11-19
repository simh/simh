/* 3b2_iu.h: SCN2681A Dual UART

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

#ifndef __3B2_IU_H__
#define __3B2_IU_H__

#include "3b2_defs.h"

#define CMD_ERX         0x01              /* Enable receiver */
#define CMD_DRX         0x02              /* Disable receiver */
#define CMD_ETX         0x04              /* Enable transmitter */
#define CMD_DTX         0x08              /* Disable transmitter */
#define CMD_MISC_SHIFT  4                 /* Command */
#define CMD_MISC_MASK   0x7

#define IU_SPEED_REGS   2                 /* Two speed select registers, */
#define IU_SPEEDS       16                /* with 16 speeds each */

#define IU_PARITY_ODD   0
#define IU_PARITY_EVEN  1
#define IU_PARITY_NONE  2

#define STS_RXR         0x01              /* Receiver ready */
#define STS_FFL         0x02              /* FIFO full */
#define STS_TXR         0x04              /* Transmitter ready */
#define STS_TXE         0x08              /* Transmitter empty */
#define STS_OER         0x10              /* Overrun error */
#define STS_PER         0x20              /* Parity error */
#define STS_FER         0x40              /* Framing error */
#define STS_RXB         0x80              /* Received break */

#define ISTS_TXRA       0x01              /* Transmitter ready A */
#define ISTS_RXRA       0x02              /* Receiver ready A */
#define ISTS_DBA        0x04              /* Delta Break A */
#define ISTS_CRI        0x08              /* Counter ready */
#define ISTS_TXRB       0x10              /* Transmitter ready B */
#define ISTS_RXRB       0x20              /* Receiver ready B */
#define ISTS_DBB        0x40              /* Delta Break B */
#define ISTS_IPC        0x80              /* Interrupt port change */

#define MODE_V_CHM      6                 /* Channel mode */
#define MODE_M_CHM      0x3

/* Transmitter State bits */
#define T_HOLD        1
#define T_XMIT        2

/* Used by the DMAC */
#define IUA_DATA_REG  3
#define IUB_DATA_REG  11

/* Registers - Read */
#define SRA           1
#define RHRA          3
#define IPCR          4
#define ISR           5
#define CTU           6
#define CTL           7
#define SRB           9
#define RHRB          11
#define INPRT         13
#define START_CTR     14
#define STOP_CTR      15

/* Registers - Write */
#define CSRA          1
#define CRA           2
#define THRA          3
#define ACR           4
#define IMR           5
#define CTUR          6
#define CTLR          7
#define CSRB          9
#define CRB           10
#define THRB          11
#define OPCR          13
#define SOPR          14
#define ROPR          15

/* Registers - R/W */
#define MR12A         0
#define MR12B         8

/* Port configuration */
#define TX_EN         1
#define RX_EN         2

/* Control Register commands */
#define CR_RST_MR     1
#define CR_RST_RX     2
#define CR_RST_TX     3
#define CR_RST_ERR    4
#define CR_RST_BRK    5
#define CR_START_BRK  6
#define CR_STOP_BRK   7

/* IMR bits */
#define IMR_TXRA      0x01
#define IMR_RXRA      0x02
#define IMR_CTR       0x08
#define IMR_TXRB      0x10
#define IMR_RXRB      0x20

/* Power-off bit */
#define IU_KILLPWR    0x04

#define PORT_A            0
#define PORT_B            1

#define IU_MODE(x)    ((x & UM_MASK) >> UM_SHIFT)

#define IUBASE            0x49000
#define IUSIZE            0x100

#define IU_BUF_SIZE       3

/* Data Carrier Detect inputs and input change bits */
#if defined(REV3)
#define IU_DCDB_CH        0x80
#define IU_DCDA_CH        0x40
#define IU_DCDB           0x08
#define IU_DCDA           0x04
#else
#define IU_DCDB_CH        0x20
#define IU_DCDA_CH        0x10
#define IU_DCDB           0x02
#define IU_DCDA           0x01
#endif

/* Default baud rate generator (9600 baud) */
#define BRG_DEFAULT       11

/* The 2681 DUART includes a 16-bit timer/counter that can be used to
 * trigger an interrupt after a certain amount of time has passed.
 *
 * The 2681 uses a crystal with a frequency of 3.686400 MHz, and the
 * timer/counter uses this frequency divided by 16, giving a final
 * timer/counter frequency of 230,400 Hz. There are therefore 4.34
 * microseconds of wall time per tick of the timer.
 *
 * The multiplier defined below is a default that can be adjusted to
 * make IU timing faster, but less accurate, if desired */

#define IU_TIMER_MULTIPLIER  4

typedef struct iu_port {
    uint8 cmd;                /* Command */
    uint8 mode[2];            /* Two mode buffers */
    uint8 modep;              /* Point to mode[0] or mode[1] */
    uint8 conf;               /* Configuration bits */
    uint8 sr;                 /* Status Register */
    uint8 thr;                /* Transmit Holding Register */
    uint8 txr;                /* Transmit Shift Register */
    uint8 rxr;                /* Receive Shift Register */
    uint8 rxbuf[IU_BUF_SIZE]; /* Receive Holding Register (3 bytes) */
    uint8 w_p;                /* Receive Buffer Write Pointer */
    uint8 r_p;                /* Receive Buffer Read Pointer */
    uint8 tx_state;           /* Transmitting state flags (HOLD, XMIT) */
    t_bool dma;               /* DMA currently active */
    t_bool drq;               /* DMA request enabled */
    t_bool rxr_full;          /* Receive Shift Register is full */
} IU_PORT;

typedef struct iu_state {
    uint8 isr;            /* Interrupt Status Register */
    uint8 imr;            /* Interrupt Mask Register */
    uint8 acr;            /* Aux. Control Register */
    uint8 opcr;           /* Output Port Configuration */
    uint8 inprt;          /* Input Port Data */
    uint8 ipcr;           /* Input Port Change Register */
} IU_STATE;

typedef struct iu_timer_state {
    uint16 c_set;
    t_bool c_en;
} IU_TIMER_STATE;

/* Function prototypes */
t_stat contty_attach(UNIT *uptr, CONST char *cptr);
t_stat contty_detach(UNIT *uptr);
t_stat tti_reset(DEVICE *dptr);
t_stat contty_reset(DEVICE *dptr);
t_stat iu_timer_reset(DEVICE *dptr);
t_stat iu_timer_set_mult(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat iu_timer_show_mult(FILE *st, UNIT *uptr, int val, CONST void *desc);
t_stat iu_svc_tti(UNIT *uptr);
t_stat iu_svc_tto(UNIT *uptr);
t_stat iu_svc_contty(UNIT *uptr);
t_stat iu_svc_contty_xmt(UNIT *uptr);
t_stat iu_svc_timer(UNIT *uptr);
uint32 iu_read(uint32 pa, size_t size);
void iu_write(uint32 pa, uint32 val, size_t size);
void iua_drq_handled();
void iub_drq_handled();
void iu_txrdy_a_irq();
void iu_txrdy_b_irq();
void iu_dma_console(uint8 channel, uint32 service_address);
void iu_dma_contty(uint8 channel, uint32 service_address);
void increment_modep_a();
void increment_modep_b();

extern IU_PORT iu_console;
extern IU_PORT iu_contty;
extern t_bool iu_increment_a;
extern t_bool iu_increment_b;

#endif
