/* 3b2_iu.h:  SCN2681A Dual UART Header

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

#ifndef __3B2_IU_H__
#define __3B2_IU_H__

#include "3b2_defs.h"
#include "3b2_sysdev.h"

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

#define ISTS_TAI        0x01              /* Transmitter ready A */
#define ISTS_RAI        0x02              /* Receiver ready A */
#define ISTS_CBA        0x04              /* Change in break A */
#define ISTS_CRI        0x08              /* Counter ready */
#define ISTS_TBI        0x10              /* Transmitter ready B */
#define ISTS_RBI        0x20              /* Receiver ready B */
#define ISTS_CBB        0x40              /* Change in break B */
#define ISTS_IPC        0x80              /* Interrupt port change */

#define MODE_V_CHM      6                 /* Channel mode */
#define MODE_M_CHM      0x3

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
#define INPRT         13  /* Input port data */
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

#define UM_CTR_EXT    0
#define UM_CTR_TXCA   1
#define UM_CTR_TXCB   2
#define UM_CTR_DIV16  3
#define UM_TMR_EXT    4
#define UM_TMR_EXT16  5
#define UM_TMR_XTL    6
#define UM_TMR_XTL16  7
#define UM_MASK       0x70
#define UM_SHIFT      4

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

extern DEVICE tti_dev;
extern DEVICE tto_dev;
extern DEVICE contty_dev;
extern DEVICE iu_timer_dev;

#define IUBASE            0x49000
#define IUSIZE            0x100

#define IU_BUF_SIZE       3

#define IU_DCDA           0x01
#define IU_DCDB           0x02
#define IU_DTRA           0x01
#define IU_DTRB           0x02

#define DMA_NONE   0
#define DMA_VERIFY 1
#define DMA_WRITE  2
#define DMA_READ   4

/* Default baud rate generator (9600 baud) */
#define BRG_DEFAULT       11

#define IU_TIMER_RATE     2.114 /* microseconds per step */


typedef struct iu_port {
    uint8 stat;               /* Port Status */
    uint8 cmd;                /* Command */
    uint8 mode[2];            /* Two mode buffers */
    uint8 modep;              /* Point to mode[0] or mode[1] */
    uint8 conf;               /* Configuration bits */
    uint8 txbuf;              /* Transmit Holding Register */
    uint8 rxbuf[IU_BUF_SIZE]; /* Receive Holding Register (3 bytes) */
    uint8 w_p;                /* Buffer Write Pointer */
    uint8 r_p;                /* Buffer Read Pointer */
    uint8 dma;                /* Currently active DMA mode */
    t_bool drq;               /* DMA request enabled */
} IU_PORT;

typedef struct iu_state {
    uint8 istat;          /* Interrupt Status */
    uint8 imr;            /* Interrupt Mask Register */
    uint8 acr;
    uint8 opcr;           /* Output Port Configuration */
    uint8 inprt;          /* Input Port Data */
    uint8 ipcr;           /* Input Port Change Register */
} IU_STATE;

typedef struct iu_timer_state {
    uint16 c_set;
    t_bool c_en;
} IU_TIMER_STATE;

extern IU_PORT  iu_console;
extern IU_PORT  iu_contty;

/* Function prototypes */
t_stat contty_attach(UNIT *uptr, CONST char *cptr);
t_stat contty_detach(UNIT *uptr);
t_stat tti_reset(DEVICE *dptr);
t_stat contty_reset(DEVICE *dptr);
t_stat iu_timer_reset(DEVICE *dptr);
t_stat iu_svc_tti(UNIT *uptr);
t_stat iu_svc_tto(UNIT *uptr);
t_stat iu_svc_contty_rcv(UNIT *uptr);
t_stat iu_svc_contty_xmt(UNIT *uptr);
t_stat iu_svc_timer(UNIT *uptr);
t_stat iu_tx(uint8 portno, uint8 val);
uint32 iu_read(uint32 pa, size_t size);
void iu_write(uint32 pa, uint32 val, size_t size);
void iua_drq_handled();
void iub_drq_handled();
void iu_txrdy_a_irq();
void iu_txrdy_b_irq();
void iu_dma(uint8 channel, uint32 service_address);

static SIM_INLINE void iu_w_buf(uint8 portno, uint8 val);
static SIM_INLINE void iu_w_cmd(uint8 portno, uint8 val);
static SIM_INLINE void iu_update_rxi(uint8 c);
static SIM_INLINE void iu_update_txi();

#endif
