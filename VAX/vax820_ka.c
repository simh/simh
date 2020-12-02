/* vax820_ka.c: VAX 8200 CPU

   Copyright (c) 2019, Matt Burke

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

   This module contains the VAX 8200 CPU registers and devices.

   ka0, ka1         KA820 CPU
*/

#include "vax_defs.h"

#define PCSR_RSTH       0x80000000                      /* restart halt */
#define PCSR_LCON       0x40000000                      /* logical console */
#define PCSR_CONEN      0x20000000                      /* console enable */
#define PCSR_BIRST      0x10000000                      /* BI reset */
#define PCSR_BISTF      0x08000000                      /* self test fast/slow */
#define PCSR_ENAPT      0x04000000                      /* APT connection status */
#define PCSR_STPASS     0x02000000                      /* self test pass */
#define PCSR_RUN        0x01000000                      /* pgm mode run */
#define PCSR_WWPE       0x00800000                      /* write wrong parity, even */
#define PCSR_EVLCK      0x00400000                      /* event lock */
#define PCSR_WMEM       0x00200000                      /* write mem status */
#define PCSR_V_EVENT    16
#define PCSR_M_EVENT    0xF
#define PCSR_EVENT      (PCSR_M_EVENT << PCSR_V_EVENT)  /* BI event */
#define PCSR_WWPO       0x00008000                      /* write wrong parity, odd */
#define PCSR_PER        0x00004000                      /* parity error */
#define PCSR_ENPIPE     0x00002000                      /* enable BI pipeline */
#define PCSR_TIMEOUT    0x00001000                      /* timeout */
#define PCSR_RSVD       0x00000800                      /* reserved */
#define PCSR_CONIE      0x00000400                      /* console interrupt enable */
#define PCSR_CONCLR     0x00000200                      /* clear console interrupt */
#define PCSR_V_CONINT   8
#define PCSR_CONINT     (1u << PCSR_V_CONINT)           /* console interrupt req */
#define PCSR_RXIE       0x00000080                      /* RX50 interrupt enable */
#define PCSR_RXCLR      0x00000040                      /* clear RX50 interrupt */
#define PCSR_RXINT      0x00000020                      /* RX50 interrupt request */
#define PCSR_IPCLR      0x00000010                      /* clear IP interrupt */
#define PCSR_V_IPINT    3
#define PCSR_IPINT      (1u << PCSR_V_IPINT)            /* IP interrupt request */
#define PCSR_CRDEN      0x00000004                      /* enable CRD interrupts */
#define PCSR_CRDCLR     0x00000002                      /* clear CRD interrupt */
#define PCSR_CRDINT     0x00000001                      /* CRD interrupt request */
#define PCSR_WR         (PCSR_RUN | PCSR_WWPE | PCSR_WWPO | \
                         PCSR_ENPIPE | PCSR_CONIE | PCSR_RXIE | \
                         PCSR_CRDEN)
#define PCSR_W1C        (PCSR_EVLCK | PCSR_PER | PCSR_TIMEOUT)

int32 rxcd_count = 0;
char rxcd_ibuf[20];
char rxcd_obuf[20];
int32 rxcd_iptr = 0;
int32 rxcd_optr = 0;
char rxcd_char = '\0';
BIIC ka_biic[KA_NUM];
uint32 ka_rxcd[KA_NUM];
uint32 ka_pcsr[KA_NUM];

extern int32 rxcd_int;
extern int32 ipir;
#if defined (VAX_MP)
extern int32 cur_cpu;
#else
int32 cur_cpu;
#endif

t_stat ka_reset (DEVICE *dptr);
t_stat ka_rdreg (int32 *val, int32 pa, int32 mode);
t_stat ka_wrreg (int32 val, int32 pa, int32 mode);
t_stat ka_svc (UNIT *uptr);

#if defined (VAX_MP)
extern void cpu_setreg (int32 cpu, int32 rg, int32 val);
extern void cpu_start (int32 cpu, uint32 addr);
#endif

/* KAx data structures

   kax_dev      KAx device descriptor
   kax_unit     KAx unit
   kax_reg      KAx register list
*/

DIB ka0_dib[] = { { TR_KA0, 0, &ka_rdreg, &ka_wrreg, 0 } };

UNIT ka0_unit = { UDATA (&ka_svc, 0, 0) };

REG ka0_reg[] = {
    { NULL }
    };

MTAB ka0_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_KA0, "NEXUS", NULL,
      NULL, &show_nexus },
    { 0 }
    };

DIB ka1_dib[] = { { TR_KA1, 0, &ka_rdreg, &ka_wrreg, 0 } };

UNIT ka1_unit = { UDATA (&ka_svc, 0, 0) };

MTAB ka1_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_KA1, "NEXUS", NULL,
      NULL, &show_nexus },
    { 0 }  };

REG ka1_reg[] = {
    { NULL }
    };

DEVICE ka_dev[] = {
    {
    "KA0", &ka0_unit, ka0_reg, ka0_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &ka_reset,
    NULL, NULL, NULL,
    &ka0_dib, DEV_NEXUS
    },
    {
    "KA1", &ka1_unit, ka1_reg, ka1_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &ka_reset,
    NULL, NULL, NULL,
    &ka1_dib, DEV_NEXUS | DEV_DISABLE | DEV_DIS
    }
    };

/* KA read */

t_stat ka_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 ka, ofs;

ka = NEXUS_GETNEX (pa) - TR_KA0;                        /* get CPU num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
switch (ofs) {

    case BI_DTYPE:
        *val = DTYPE_KA820;
        break;

    case BI_CSR:
        *val = ka_biic[ka].csr & BICSR_RD;
        break;

    case BI_BER:
        *val = ka_biic[ka].ber & BIBER_RD;
        break;

    case BI_EICR:
        *val = ka_biic[ka].eicr & BIECR_RD;
        break;

    case BI_IDEST:
        *val = ka_biic[ka].idest & BIID_RD;
        break;

    case BI_SA:
    case BI_EA:
        *val = 0;
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* KA write */

t_stat ka_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 ka, ofs;

ka = NEXUS_GETNEX (pa) - TR_KA0;                        /* get CPU num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
switch (ofs) {

    case BI_CSR:
        ka_biic[ka].csr = (ka_biic[ka].csr & ~BICSR_RW) | (val & BICSR_RW);
        break;

    case BI_BER:
        ka_biic[ka].ber = ka_biic[ka].ber & ~(val & BIBER_W1C);
        break;

    case BI_EICR:
        ka_biic[ka].eicr = (ka_biic[ka].eicr & ~BIECR_RW) | (val & BIECR_RW);
        ka_biic[ka].eicr = ka_biic[ka].eicr & ~(val & BIECR_W1C);
        break;

    case BI_IDEST:
        ka_biic[ka].idest = val & BIID_RW;
        break;

    case BI_IMSK:
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* KA reset */

t_stat ka_reset (DEVICE *dptr)
{
int32 i;

rxcd_count = 0;
ka_rxcd[0] = 0;
ka_rxcd[1] = 0;
for (i = 0; i < KA_NUM; i++) {
    ka_biic[i].csr = (1u << BICSR_V_IF) | BICSR_STS | ((TR_KA0 + i) & BICSR_NODE);
    ka_biic[i].ber = 0;
    ka_biic[i].eicr = 0;
    ka_biic[i].idest = 0;
    }
ka_pcsr[0] = PCSR_CONEN | PCSR_ENAPT | PCSR_STPASS | PCSR_RUN;
ka_pcsr[1] = PCSR_RSTH | PCSR_LCON | PCSR_CONEN | PCSR_ENAPT | PCSR_STPASS | PCSR_RUN;
sim_cancel (&ka0_unit);
sim_cancel (&ka1_unit);
return SCPE_OK;
}

t_stat ka_svc (UNIT *uptr)
{
if ((rxcd_count > 0) && rxcd_int)
    sim_activate (uptr, 20);
rxcd_int = 1;
return SCPE_OK;
}

int32 rxcd_rd (void)
{
int32 val;

if (rxcd_count) {                                       /* data available? */
    val = rxcd_obuf[rxcd_optr] | (1 << 8);
    rxcd_optr++;
    rxcd_count--;
    if (rxcd_count)
        sim_activate (&ka0_unit, 20);
    }
else {
    val = 0x52584344;                                   /* "RXCD" */
    mxpr_cc_vc |= CC_V;                                 /* set overflow */
    }

return val;
}

void rxcd_wr (int32 val)
{
int32 cpu = (val >> 8) & 7;
int32 ch = val & 0xFF;
int32 rg;
int32 rval;
t_stat r;
char conv[10];

if (ka_rxcd[cpu] & 0x8000) {                            /* busy? */
    mxpr_cc_vc |= CC_V;                                 /* set overflow */
    return;
    }

switch (ch) {

    case 0x0D:                                          /* CR */
        rxcd_ibuf[rxcd_iptr++] = '\0';                  /* terminator */
        printf (">>> %s\n", &rxcd_ibuf[0]);
        if (rxcd_ibuf[0] == 'D') {                      /* DEPOSIT */
            conv[0] = rxcd_ibuf[4];
            conv[1] = '\0';
            rg = (int32)get_uint (conv, 16, 0xF, &r); /* get register number */
            strlcpy (conv, &rxcd_ibuf[6], 9);
            rval = (int32)get_uint (conv, 16, 0xFFFFFFFF, &r); /* get deposit value */
#if defined (VAX_MP)
            cpu_setreg (cpu, rg, rval);
#endif
            rxcd_count = 3;                             /* ready for next cmd */
            sprintf (&rxcd_obuf[0], ">>>");
            rxcd_optr = 0;
            }
        else if (rxcd_ibuf[0] == 'I') {                 /* INIT */
            rxcd_count = 3;                             /* ready for next cmd */
            sprintf (&rxcd_obuf[0], ">>>");
            rxcd_optr = 0;
            }
        else if (rxcd_ibuf[0] == 'S') {                 /* START */
            strlcpy (conv, &rxcd_ibuf[2], 9);
            rval = (int32)get_uint (conv, 16, 0xFFFFFFFF, &r);
#if defined (VAX_MP)
            cpu_start (cpu, rval);
#endif
            }
        rxcd_iptr = 0;
        break;

    case 0x10:                                          /* CTRL-P */
        rxcd_count = 3;                                 /* ready for next cmd */
        sprintf (&rxcd_obuf[0], ">>>");
        rxcd_optr = 0;
        break;

    default:
        rxcd_count = 1;
        rxcd_obuf[0] = ch;                              /* echo characters back */
        rxcd_optr = 0;
        rxcd_ibuf[rxcd_iptr++] = ch;                    /* store incoming charactes */
        break;
        }
if (rxcd_count) {
    if (cpu == 0)
        sim_activate (&ka0_unit, 20);
    else
        sim_activate (&ka1_unit, 20);
    }
return;
}

int32 pcsr_rd (int32 pa)
{
int32 data;
int32 ip_int = (ipir >> cur_cpu) & 0x1;
data = ka_pcsr[cur_cpu] | (rxcd_int << PCSR_V_CONINT) | (ip_int << PCSR_V_IPINT);
return data;
}

void pcsr_wr (int32 pa, int32 val, int32 lnt)
{
ka_pcsr[cur_cpu] &= ~(val & PCSR_W1C);
ka_pcsr[cur_cpu] &= ~(PCSR_WR) | (val & PCSR_WR);
if (val & PCSR_CONCLR)
    rxcd_int = 0;
if (val & PCSR_IPCLR)
    ipir &= ~(1u << cur_cpu);
}
