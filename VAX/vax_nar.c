/* vax_nar.c: Network address ROM simulator

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

   nar          Network address ROM
*/

#include "vax_defs.h"
#include "sim_ether.h"

uint32 nar[NARSIZE];                                    /* network address ROM */
ETH_MAC nar_mac = {0x08, 0x00, 0x2B, 0xCC, 0xDD, 0xEE};
t_bool nar_init = FALSE;

t_stat nar_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat nar_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat nar_reset (DEVICE *dptr);
t_stat nar_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *nar_description (DEVICE *dptr);
t_stat nar_showmac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat nar_setmac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);

/* NAR data structures

   nar_dev      NAR device descriptor
   nar_unit     NAR units
   nar_reg      NAR register list
*/

UNIT nar_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, NARSIZE) };

REG nar_reg[] = {
    { NULL }
    };

MTAB nar_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &nar_setmac, &nar_showmac, NULL, "MAC address" },
    { 0 }
    };

DEVICE nar_dev = {
    "NAR", &nar_unit, nar_reg, nar_mod,
    1, 16, NARAWIDTH, 4, 16, 32,
    &nar_ex, &nar_dep, &nar_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &nar_help, NULL, NULL, 
    &nar_description
    };

/* NAR read */

int32 nar_rd (int32 pa)
{
int32 rg = (pa >> 2) & 0x1F;
return nar[rg];
}

t_stat nar_showmac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
char buffer[20];

eth_mac_fmt ((ETH_MAC*)nar_mac, buffer);
fprintf (st, "MAC=%s", buffer);
return SCPE_OK;
}

t_stat nar_setmac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
t_stat status;

if (!cptr)
    return SCPE_IERR;
status = eth_mac_scan (&nar_mac, cptr);
if (status != SCPE_OK)
    return status;
nar_reset (&nar_dev);
return SCPE_OK;
}

/* NAR examine */

t_stat nar_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if ((vptr == NULL) || (addr & 03))
    return SCPE_ARG;
if (addr >= NARSIZE)
    return SCPE_NXM;
*vptr = nar[addr];
return SCPE_OK;
}

/* NAR deposit */

t_stat nar_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr & 03)
    return SCPE_ARG;
if (addr >= NARSIZE)
    return SCPE_NXM;
nar[addr] = (uint32) val;
return SCPE_OK;
}

/* NAR reset */

t_stat nar_reset (DEVICE *dptr)
{
uint16 i, c, w;
t_stat r;

if (!nar_init) {                                        /* set initial MAC */
    nar_init = TRUE;
    r = eth_mac_scan (&nar_mac, "08:00:2B:00:00:00/24");
    if (r != SCPE_OK)
        return r;
    }

for (i = c = 0; i < 6; i += 2) {
    c = c + c + ((uint32)((uint32)c + (uint32)c) > 0xFFFF ? 1 : 0);
    w = (nar_mac[i] << 8) | nar_mac[i + 1];
    c = c + w + ((uint32)((uint32)c + (uint32)w) > 0xFFFF ? 1 : 0);
    }
nar[0] = nar_mac[0];                                    /* MAC Address */
nar[1] = nar_mac[1];
nar[2] = nar_mac[2];
nar[3] = nar_mac[3];
nar[4] = nar_mac[4];
nar[5] = nar_mac[5];
nar[6] = (c >> 8) & 0xFF;                               /* checksum */
nar[7] = c & 0xFF;
nar[8] = c & 0xFF;                                      /* same in reverse */
nar[9] = (c >> 8) & 0xFF;
nar[10] = nar_mac[5];
nar[11] = nar_mac[4];
nar[12] = nar_mac[3];
nar[13] = nar_mac[2];
nar[14] = nar_mac[1];
nar[15] = nar_mac[0];
nar[16] = nar_mac[0];                                   /* same again forwards */
nar[17] = nar_mac[1];
nar[18] = nar_mac[2];
nar[19] = nar_mac[3];
nar[20] = nar_mac[4];
nar[21] = nar_mac[5];
nar[22] = (c >> 8) & 0xFF;
nar[23] = c & 0xFF;
nar[24] = 0xFF;                                         /* manufacturing check data */
nar[25] = 0x0;
nar[26] = 0x55;
nar[27] = 0xAA;
nar[28] = 0xFF;
nar[29] = 0x0;
nar[30] = 0x55;
nar[31] = 0xAA;
return SCPE_OK;
}

t_stat nar_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Network address ROM\n\n");
fprintf (st, "The ROM consists of a single unit, simulating the 32 byte\n");
fprintf (st, "network address ROM.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nMAC address octets must be delimited by dashes, colons or periods.\n");
fprintf (st, "The controller defaults to a relatively unique MAC address in the range\n");
fprintf (st, "08-00-2B-00-00-00 thru 08-00-2B-FF-FF-FF, which should be sufficient\n");
fprintf (st, "for most network environments.  If desired, the simulated MAC address\n");
fprintf (st, "can be directly set.\n");
return SCPE_OK;
}

const char *nar_description (DEVICE *dptr)
{
return "network address ROM";
}
