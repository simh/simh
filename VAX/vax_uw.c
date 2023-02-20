/* vax_uw.c: M7452 Unibus window module for VAXstation 100

   Copyright (c) 2023, Lars Brinkhoff

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   uw           M7452 Unibus window module for VAXstation 100

   20-Feb-2023  LB      First version

   Related documents:

    - VAXstation 10 Engineering Spec (section 5.2)
    - 4.3BSD driver vs.c.
*/

#include "vax_defs.h"
#include "sim_tmxr.h"

#define UW_UNITS 1
#define IOLN_UW 16
#define DBG_REG  0x0001
#define DBG_INT  0x0002
#define DBG_FIB  0x0004

t_stat uw_svc (UNIT *uptr);
t_stat uw_wr (int32 data, int32 PA, int32 access);
t_stat uw_rd (int32 *data, int32 PA, int32 access);
int32 uw_inta (void);
t_stat uw_reset (DEVICE *dptr);
t_stat uw_attach (UNIT *uptr, CONST char *cptr);
t_stat uw_detach (UNIT *uptr);
const char *uw_description (DEVICE *dptr);

uint16 uw_csr[IOLN_UW];

#undef CSR_GO
#undef CSR_IE
#undef CSR_ERR
#undef CSR_DONE

#define CSR  uw_csr[0x00] //Control and status.
#define IRR  uw_csr[0x01] //Interrupt reason.
#define KBR  uw_csr[0x02] //Keyboard, peripheral event.
#define FP1  uw_csr[0x03] //Function parameter 1.
#define FP2  uw_csr[0x04] //Function parameter 2.
#define CXR  uw_csr[0x05] //Cursor x.
#define CYR  uw_csr[0x06] //Cursor y.
#define IVR  uw_csr[0x07] //Interrupt vector.
//efine IVR  uw_csr[0x0F] //Revision 2B.

#define CSR_GO    0x0001
#define CSR_FCN   0x003E
#define CSR_IE    0x0040 //Interrupt enable.
#define CSR_OWN   0x0080
#define CSR_DONE  0x0200 //Maintenance done.
#define CSR_CRC   0x0400 //CRC disable.
#define CSR_MAINT 0x0800 //Maintenance mode.
#define CSR_XMIT  0x1000 //Transmit on.
#define CSR_ERR   0x2000 //Link error.
#define CSR_LNK   0x4000 //Link available.
#define CSR_TRN   0x8000 //Link transition.

#define IRR_ID    0x0001 //Init done.
#define IRR_IC    0x0002 //Done.
#define IRR_SE    0x0004 //Start event.
#define IRR_BE    0x0008 //Button event.
#define IRR_MM    0x0010 //Mouse moved.
#define IRR_TM    0x0020 //Tablet moved.
#define IRR_PWR   0x0080 //Powerup complete.
#define IRR_DIAG  0x4000
#define IRR_ERR   0x8000

// Indicate XMIT on/off in debug messages.
#define XMIT  ((CSR & CSR_XMIT) ? "" : "DON'T ")

//Sender: V=VS100, H=Host.
#define FIBRE_XMIT_ON    1  //VH No data.
#define FIBRE_XMIT_OFF   2  //VH No data.
#define FIBRE_INT        3  //V  No data.
#define FIBRE_CSR        4  //VH 8-bit number, 16-bit data.
#define FIBRE_READ8      5  //V  32-bit address.
#define FIBRE_READ16     6  //V  32-bit address.
#define FIBRE_DATA       7  // H 16-bit data.
#define FIBRE_NXM        8  // H No data.
#define FIBRE_WRITE8     9  //V  32-bit address, 8-bit data.
#define FIBRE_WRITE16    10 //V  32-bit address, 16-bit data.

#define POLL_SLOW   100000 //Poll for connection every 100 ms.
#define POLL_FAST     1000 //Poll for data every 1 ms.
int32 uw_poll = POLL_SLOW;

//Max message size is 7.
uint8 uw_message[7];
uint8 uw_length;

DEBTAB uw_debug[] = {
    { "REG",  DBG_REG,  "Register access" },
    { "INT",  DBG_INT,  "Interrupt" },
    { "FIB",  DBG_FIB,  "Fibre data" },
    {0}
    };

TMLN uw_ldsc[UW_UNITS] = { 0 };
TMXR uw_desc = { UW_UNITS, 0, 0, uw_ldsc };
UNIT uw_unit[UW_UNITS] = { UDATA (uw_svc, UNIT_IDLE|UNIT_ATTABLE, 0) };

REG uw_reg[] = {
    { BRDATAD (CSR, uw_csr, 16, IOLN_UW, 16, "Control and status registers") },
    { NULL }
    };

MTAB uw_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
        NULL, &show_vec, NULL, "Interrupt vector" },
    { 0 }
    };

DIB uw_dib = {
    IOBA_AUTO, 2*IOLN_UW, &uw_rd, &uw_wr,
    2, IVCL (UW), VEC_AUTO, { &uw_inta, &uw_inta }
    };

DEVICE uw_dev = {
    "UW", uw_unit, uw_reg, uw_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &uw_reset,
    NULL, &uw_attach, &uw_detach,
    &uw_dib, DEV_UBUS | DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0,
    uw_debug, NULL, NULL, NULL, NULL, NULL,
    &uw_description
    };


void uw_send(uint8 *data, int n) {
if (!uw_ldsc[0].conn)
    return;
if (!(CSR & CSR_XMIT))
    return;
while(n > 0) {
    if (tmxr_putc_ln (&uw_ldsc[0], *data) == SCPE_STALL) {
        tmxr_poll_tx (&uw_desc);
        continue;
        }
    data++;
    n--;
    }
tmxr_poll_tx (&uw_desc);
}

void uw_send_data(uint8 type, uint16 data)
{
uint8 message[3];
message[0] = type;
message[1] = data >> 8;
message[2] = data & 0xFF;
uw_send(message, sizeof message);
}

void uw_send_csr(uint8 type, uint8 reg, uint16 data)
{
uint8 message[4];
message[0] = type;
message[1] = reg;
message[2] = data >> 8;
message[3] = data & 0xFF;
uw_send(message, sizeof message);
}

void uw_set_int(void)
{
sim_debug (DBG_INT, &uw_dev, "Interupt%s\n",
           CSR & CSR_IE ? "" : " (disabled)");
if (CSR & CSR_IE)
    SET_INT(UW);
}

void uw_clr_int(void)
{
sim_debug (DBG_INT, &uw_dev, "Clear interupt\n");
CLR_INT(UW);
}

t_stat uw_wr (int32 data, int32 pa, int32 access)
{
uint8 message;
uint16 xmit_off = 0;
pa = (pa & 0x0F) >> 1;
switch(pa) {
case 0:
    if (IRR != 0)
        return SCPE_OK;
    if ((~CSR & data) & CSR_XMIT) {
        CSR |= CSR_XMIT;
        sim_debug (DBG_FIB, &uw_dev, "Send xmit on.\n");
        message = FIBRE_XMIT_ON;
        uw_send(&message, 1);
        }
    xmit_off = (CSR & ~data) & CSR_XMIT;
    if ((CSR & ~data) & CSR_TRN)
        uw_clr_int();
    if ((data & CSR_TRN) == 0)
        CSR &= ~CSR_ERR;
case 1:
    if (data == 0 && (data & CSR_TRN) == 0)
        uw_clr_int();
}
uw_csr[pa] = data;
sim_debug (DBG_REG, &uw_dev, "Write CSR%d: %04X\n", pa, data);
sim_debug (DBG_FIB, &uw_dev, "%sSend CSR%d %04X.\n", XMIT, pa, uw_csr[pa]);
uw_send_csr(FIBRE_CSR, pa, uw_csr[pa]);
if(xmit_off) {
    sim_debug (DBG_FIB, &uw_dev, "Send xmit off.\n");
    message = FIBRE_XMIT_OFF;
    CSR |= CSR_XMIT;
    uw_send(&message, 1);
    CSR &= ~CSR_XMIT;
    }
return SCPE_OK;
}

t_stat uw_rd (int32 *data, int32 pa, int32 access)
{
pa = (pa & 0x0F) >> 1;
*data = uw_csr[pa];
sim_debug (DBG_REG, &uw_dev, "Read CSR%d: %04X\n", pa, *data);
return SCPE_OK;
}

int32 uw_inta (void)
{
sim_debug (DBG_INT, &uw_dev, "Interrupt ack: %03o\n", IVR);
return IVR;
}

void uw_receive(void)
{
uint32 addr;
uint16 data16;
uint8 data8;
switch(uw_message[0]) {
case FIBRE_XMIT_ON:
    sim_debug (DBG_FIB, &uw_dev, "Receive xmit on.\n");
    if (!(CSR & CSR_LNK)) {
        CSR |= CSR_TRN;
        uw_set_int();
        }
    CSR |= CSR_LNK;
    break;
case FIBRE_XMIT_OFF:
    sim_debug (DBG_FIB, &uw_dev, "Receive xmit off.\n");
    if (CSR & CSR_LNK) {
        CSR |= CSR_TRN;
        uw_set_int();
        }
    CSR &= ~CSR_LNK;
    break;
case FIBRE_INT:
    sim_debug (DBG_FIB, &uw_dev, "Receive interrupt.\n");
    uw_set_int();
    break;
case FIBRE_CSR:
    if (uw_length < 4)
        return;
    data16 = uw_message[2] << 8;
    data16 |= uw_message[3];
    sim_debug (DBG_FIB, &uw_dev, "Receive CSR%d %04X.\n",
               uw_message[1], data16);
    uw_csr[uw_message[1]] = data16;
    break;
case FIBRE_READ8:
    if (uw_length < 5)
        return;
    addr = uw_message[1] << 24;
    addr |= uw_message[2] << 16;
    addr |= uw_message[3] << 8;
    addr |= uw_message[4];
    sim_debug (DBG_FIB, &uw_dev, "Receive read8 %05X.\n", addr);
    Map_ReadB (addr, 1, &data8);
    sim_debug (DBG_FIB, &uw_dev, "Send data %02X.\n", data8);
    uw_send_data(FIBRE_DATA, data8);
    break;
case FIBRE_READ16:
    if (uw_length < 5)
        return;
    addr = uw_message[1] << 24;
    addr |= uw_message[2] << 16;
    addr |= uw_message[3] << 8;
    addr |= uw_message[4];
    sim_debug (DBG_FIB, &uw_dev, "Receive read16 %05X.\n", addr);
    Map_ReadW (addr, 2, &data16);
    sim_debug (DBG_FIB, &uw_dev, "Send data %04X.\n", data16);
    uw_send_data(FIBRE_DATA, data16);
    break;
case FIBRE_WRITE8:
    if (uw_length < 6)
        return;
    addr = uw_message[1] << 24;
    addr |= uw_message[2] << 16;
    addr |= uw_message[3] << 8;
    addr |= uw_message[4];
    data8 = uw_message[5];
    sim_debug (DBG_FIB, &uw_dev, "Receive write8 %05X %02X.\n",
               addr, data8);
    Map_WriteB (addr, 1, &data8);
    break;
case FIBRE_WRITE16:
    if (uw_length < 7)
        return;
    addr = uw_message[1] << 24;
    addr |= uw_message[2] << 16;
    addr |= uw_message[3] << 8;
    addr |= uw_message[4];
    data16 = uw_message[5] << 8;
    data16 |= uw_message[6];
    sim_debug (DBG_FIB, &uw_dev, "Receive write16 %05X %04X.\n",
               addr, data16);
    Map_WriteW (addr, 2, &data16);
    break;
default:
    sim_debug(DBG_FIB, &uw_dev, "Bad data %02X\n", uw_message[0]);
    tmxr_reset_ln (&uw_ldsc[0]);
    }
uw_length = 0;
}

t_stat uw_svc (UNIT *uptr)
{
int32 ch;
int i;

i = tmxr_poll_conn (&uw_desc);
if (i >= 0) {
    uint8 message = FIBRE_XMIT_ON;
    sim_debug(DBG_FIB, &uw_dev, "Connect %d\n", i);
    uw_ldsc[i].rcve = 1;
    uw_ldsc[i].xmte = 1;
    sim_debug (DBG_FIB, &uw_dev, "%sSend xmit on.\n", XMIT);
    uw_send(&message, 1);
    uw_poll = POLL_FAST;
    }

sim_activate_after(uw_unit, uw_poll);

if (!uw_ldsc[0].conn) {
    uw_ldsc[0].rcve = 0;
    uw_ldsc[0].xmte = 0;
    uw_poll = POLL_SLOW;
    return SCPE_OK;
    }

tmxr_poll_rx (&uw_desc);
for(;;) {
    ch = tmxr_getc_ln (&uw_ldsc[0]);
    if (!(ch & TMXR_VALID))
        break;
    uw_message[uw_length++] = ch;
    uw_receive();
   }

return SCPE_OK;
}

t_stat uw_reset (DEVICE *dptr)
{
memset(uw_csr, 0, sizeof uw_csr);
if (uw_unit->flags & UNIT_ATT)
    sim_activate (uw_unit, 1);
else
    sim_cancel (uw_unit);
IRR = 1;
return auto_config (dptr->name, (dptr->flags & DEV_DIS) ? 0 : 1);
}

t_stat uw_attach (UNIT *uptr, CONST char *cptr)
{
t_stat stat;
tmxr_set_notelnet (&uw_desc);
stat = tmxr_attach (&uw_desc, uptr, cptr);
uw_ldsc[0].rcve = 1;
uw_ldsc[0].xmte = 1;
uw_length = 0;
uw_poll = POLL_SLOW;
sim_activate (uw_unit, 1);
return stat;
}

t_stat uw_detach (UNIT *uptr)
{
t_stat stat = tmxr_detach (&uw_desc, uptr);
uw_ldsc[0].rcve = 0;
uw_ldsc[0].xmte = 0;
sim_cancel (uw_unit);
return stat;
}

const char *uw_description (DEVICE *dptr)
{
return "UW - M7452 Unibus window module for VAXstation 100";
}
