/* B5500_io.c: Burroughs 5500 I/O System.

   Copyright (c) 2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "b5500_defs.h"

#define EOR             1
#define USEGM           2

t_stat              chan_reset(DEVICE * dptr);

/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

t_uint64            D[NUM_CHAN];                /* Current I/O instruction */
uint8               CC[NUM_CHAN];               /* Channel character count */
t_uint64            W[NUM_CHAN];                /* Assembly register */
uint8               status[NUM_CHAN];           /* Channel status */
uint8               cstatus;                    /* Active status */

#define WC(x)       (uint16)(((x) & DEV_WC) >> DEV_WC_V)
#define toWC(x)     (((t_uint64)(x) << DEV_WC_V) & DEV_WC)


UNIT                chan_unit[] = {
    /* Normal channels */
    {UDATA(NULL,UNIT_DISABLE,0)},/* A */
    {UDATA(NULL,UNIT_DISABLE,0)},/* B */
    {UDATA(NULL,UNIT_DISABLE,0)},/* C */
    {UDATA(NULL,UNIT_DISABLE,0)},/* D */
};

REG                 chan_reg[] = {
    {BRDATA(D, D, 8, 48, NUM_CHAN), REG_RO},
    {BRDATA(CC, CC, 7, 6, NUM_CHAN), REG_RO},
    {BRDATA(W, W, 8, 48, NUM_CHAN), REG_RO},
    {NULL}
};


/* Simulator debug controls */
DEBTAB              chn_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"DETAIL", DEBUG_DETAIL},
    {"DATA", DEBUG_DATA},
    {"CH0", 0x0100 << 0},
    {"CH1", 0x0100 << 1},
    {"CH2", 0x0100 << 2},
    {"CH3", 0x0100 << 3},
    {0, 0}
};

DEVICE              chan_dev = {
    "IO", chan_unit, chan_reg, NULL,
    NUM_CHAN, 10, 18, 1, 10, 44,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, chn_debug
};



t_stat
chan_reset(DEVICE * dptr)
{
    int                 i;
    int                 j = 1;

    cstatus = 0;
    /* Clear channel assignment */
    for (i = 0; i < NUM_CHAN; i++) {
        status[i] = 0;
        D[i] = 0;
        W[i] = 0;
        CC[i] = 0;
        if (chan_unit[i].flags & UNIT_DIS) 
           cstatus |= j;
        j <<= 1;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
chan_boot(t_uint64 desc)
{
    M[020] = desc;
    M[010] = 020;
    loading = 1;
    start_io();
    return SCPE_OK;     
}

int 
find_chan() {
     int                i;
     int                chan;
    
     i = 1;
     for(chan = 0; chan < NUM_CHAN; chan++) {
         if ((cstatus & i) == 0)
            break;
         i <<= 1;
     }
    
     if (chan == NUM_CHAN) {
         return 0;
     }
     return chan + 1;
}

void
chan_release(int chan) {
     cstatus &= ~(1 << chan);
}

int
chan_advance(int chan) {
    uint16      addr = (uint16)(D[chan] & CORE);

    if (D[chan] & DEV_WCFLG) {
        uint16   wc = WC(D[chan]);
        if (wc == 0) {
            status[chan] |= EOR;
            return 1;
        }
        D[chan] &= ~DEV_WC;
        D[chan] |= toWC(wc-1);
    }
    if (addr > MEMSIZE) {
        D[chan] |= DEV_MEMERR;
        status[chan] |= EOR;
        return 1;
    }
    W[chan] = M[addr];
    D[chan] &= ~CORE;
    if (D[chan] & DEV_BACK) 
         D[chan] |= (t_uint64)((addr - 1) & CORE);
    else
         D[chan] |= (t_uint64)((addr + 1) & CORE);
    CC[chan] = 0;
    return 0;
}

void
start_io() {
     int                i;
     int                chan;
     t_stat             r;
     uint16             dev;
     uint16             cmd;
     uint16             wc;
     int                addr;
    
     chan = find_chan();

     addr = M[010] & CORE;
    
     if (chan == 0) {
         IAR |= IRQ_1;
         return;
     }
     chan--;
     i = 1 << chan;
     sim_debug(DEBUG_DETAIL, &chan_dev, "strtio(%016llo %d)\n", M[addr], chan);   
     D[chan] = M[addr] & D_MASK;
     CC[chan] = 0;
     W[chan] = 0;
     dev = (uint16)((D[chan] & DEVMASK) >> DEV_V);
     cmd = (uint16)((D[chan] & DEV_CMD) >> DEV_CMD_V);
     wc = WC(D[chan]);
     D[chan] &= ~DEV_RESULT;
     status[chan] = 0;
     if (dev & 1) {
#if (NUM_DEVS_MT > 0)
        status[chan] = USEGM;
        r = mt_cmd(cmd, dev, chan, &wc);
#else
        r = SCPE_UNATT;
#endif
     } else {
        switch(dev) {
#if (NUM_DEVS_DR > 0)
        case DRUM1_DEV:
        case DRUM2_DEV:
             r = drm_cmd(cmd, dev, chan, &wc, (uint8)((M[addr] & PRESENT)!=0));
             break;
#endif 

#if (NUM_DEVS_CDR > 0) | (NUM_DEVS_CDP > 0)
        case CARD1_DEV:
        case CARD2_DEV:
             r = card_cmd(cmd, dev, chan, &wc);
             break;
#endif

#if (NUM_DEVS_DSK > 0)
        case DSK1_DEV:
        case DSK2_DEV:
             /* Need to pass word count to identify interrogates */
             r = dsk_cmd(cmd, dev, chan, &wc);
             break;
#endif 

#if (NUM_DEVS_DTC > 0) 
        case DTC_DEV:
             status[chan] = USEGM;
             /* Word count is TTU and BUF number */
             r = dtc_cmd(cmd, dev, chan, &wc);
             if (r == SCPE_OK)
                D[chan] &= ~DEV_WC;
             D[chan] &= ~(DEV_BIN|DEV_WCFLG);
             wc = 0;
             break;
#endif 

#if (NUM_DEVS_LPR > 0)
        case PRT1_DEV:
        case PRT2_DEV:
             r = lpr_cmd(cmd, dev, chan, &wc);
             if (r == SCPE_OK)
                 D[chan] &= ~DEV_BACK;  /* Clear this bit, since printer 
                                           uses this to determine 120/132 
                                           char line */
             break;
#endif

#if (NUM_DEVS_CON > 0)
        case SPO_DEV:
             status[chan] = USEGM;
             r = con_cmd(cmd, dev, chan, &wc);
             break;
#endif
        default:
             r = SCPE_UNATT;
             break;
        }
    }   
    if (wc != 0) {
        D[chan] &= ~DEV_WC;
        D[chan] |= toWC(wc) | DEV_WCFLG;
    }
    switch(r) {
    case SCPE_OK:
        cstatus |= i;
        return;
    case SCPE_NXDEV:
    case SCPE_UNATT:
        D[chan] |= DEV_NOTRDY;
        break;
    case SCPE_BUSY:
        D[chan] |= DEV_BUSY;
        break;
    case SCPE_EOF:
        D[chan] |= DEV_EOF;
        break;
    }
    chan_set_end(chan);
}


void
chan_set_end(int chan) {
     uint16   dev;
     dev = (uint16)((D[chan] & DEVMASK) >> DEV_V);
     /* Set character count if reading and tape */
     if ((dev & 1) && (D[chan] & DEV_IORD)) {
        D[chan] &= ~((7LL)<<DEV_WC_V);
        /* If not data transfered, return zero code */
        if ((D[chan] & DEV_BACK) != 0 && 
                (status[chan] & EOR) != 0)
            D[chan] |= ((t_uint64)((7-CC[chan]) & 07)) << DEV_WC_V;
        else
            D[chan] |= ((t_uint64)(CC[chan] & 07)) << DEV_WC_V;
     }

     M[014+chan] = D[chan];
     if (loading == 0) 
        IAR |= IRQ_5 << chan;
     else
        loading = 0;
     sim_debug(DEBUG_DETAIL, &chan_dev, "endio (%016llo %o)\n", D[chan], chan);   
}

void
chan_set_eof(int chan) {
    D[chan] |= DEV_EOF;
}

void
chan_set_parity(int chan) {
    D[chan] |= DEV_PARITY;
}

void
chan_set_error(int chan) {
    D[chan] |= DEV_ERROR;
}

void
chan_set_wcflg(int chan) {
    D[chan] |= DEV_WCFLG;
}

void
chan_set_read(int chan) {
    D[chan] |= DEV_IORD;
}

void
chan_set_gm(int chan) {
    D[chan] |= DEV_BACK;
}

void
chan_set_notrdy(int chan) {
    D[chan] |= DEV_NOTRDY;
    chan_set_end(chan);
}

void
chan_set_eot(int chan) {
    D[chan] &= ~DEV_WC;
    D[chan] |= DEV_EOT;
}

void
chan_set_bot(int chan) {
    D[chan] &= ~DEV_WC;
    D[chan] |= DEV_BOT;
}

void
chan_set_blank(int chan) {
    D[chan] &= ~DEV_WC;
    D[chan] |= DEV_BLANK;
}

void
chan_set_wrp(int chan) {
    D[chan] |= DEV_ERROR|DEV_MEMERR;
}

void 
chan_set_wc(int chan, uint16 wc) {
    D[chan] &= ~DEV_WC;
    D[chan] |= toWC(wc);
}


/*
        Internal        BCD
        00 0000         00 1010  0000 -> 1010
        00 0001         00 0001
        00 0010         00 0010
        00 0011         00 0011
        00 0100         00 0100
        00 0101         00 0101
        00 0110         00 0110
        00 0111         00 0111
        00 1000         00 1000
        00 1001         00 1001
        00 1010         00 1011  1010 -> 1011
        00 1011         00 1100  1011 -> 1100
        00 1100         00 0000  1100 -> 0000
        00 1101         00 1101
        00 1110         00 1110
        00 1111         00 1111
        01 0000         11 1010  0000 -> 1010  10
        01 0001         11 0001                 1
        01 0010         11 0010                 2
        01 0011         11 0011                 3
        01 0100         11 0100
        01 0101         11 0101
        01 0110         11 0110
        01 0111         11 0111
        01 1000         11 1000                 8
        01 1001         11 1001                 9
        01 1010         11 1011  1010 -> 1011   10
        01 1011         11 1100  1011 -> 1100   11
        01 1100         11 0000  1100 -> 0000   12
        01 1101         11 1101                 13
        01 1110         11 1110                 14
        01 1111         11 1111                 15
        10 0000         10 1010  0000 -> 1010
        10 0001         10 0001
        10 0010         10 0010
        10 0011         10 0011
        10 0100         10 0100
        10 0101         10 0101
        10 0110         10 0110
        10 0111         10 0111
        10 1000         10 1000
        10 1001         10 1001
        10 1010         10 1011  1010 -> 1011
        10 1011         10 1100  1011 -> 1100
        10 1100         10 0000  1100 -> 0000
        10 1101         10 1101
        10 1110         10 1110
        10 1111         10 1111
        11 0000         01 0000  
        11 0001         01 0001
        11 0010         01 0010
        11 0011         01 0011
        11 0100         01 0100
        11 0101         01 0101
        11 0110         01 0110
        11 0111         01 0111
        11 1000         01 1000
        11 1001         01 1001
        11 1010         01 1011  1010 -> 1011
        11 1011         01 1100  1011 -> 1100
        11 1100         01 1010  1100 -> 1010
        11 1101         01 1101
        11 1110         01 1110
        11 1111         01 1111 */

/* returns 1 when channel can take no more characters. 
   A return of 1 indicates that the character was not
   processed.
*/
int chan_write_char(int chan, uint8 *ch, int flags) {
        uint8   c;

        if (status[chan] & EOR) 
           return 1;

        if (D[chan] & DEV_INHTRF) {
           status[chan] |= EOR;
           return 1;
        }

        /* Check if first word */
        if (CC[chan] == 0) {
            uint16      wc = WC(D[chan]);
                
            if (D[chan] & DEV_WCFLG && wc == 0) {
                    sim_debug(DEBUG_DATA, &chan_dev, "zerowc(%d)\n", chan);   
                    status[chan] |= EOR;
                    return 1;
            }
            W[chan] = 0;
        }

        c = *ch & 077;
        if ((D[chan] & DEV_BIN) == 0) {
                /* Translate BCL to BCD */
                uint8   cx = c & 060;
        
                c &= 017;
                switch(c) {
                case 0:
                        /* 11-0 -> 01 C */
                        /* 10-0 -> 10 C */
                        /* 01-0 -> 11 0 */
                        /* 00-0 -> 00 C */
                        if (cx != 020)
                            c = 0xc;
                        break;
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0xd:
                case 0xe:
                case 0xf:
                        break;
                case 0xa:
                        /* 11-A -> 01 0 */
                        /* 10-A -> 10 0 */
                        /* 01-A -> 11 C */
                        /* 00-A -> 00 0 */
                        if (cx == 020)
                            c = 0xc;
                        else
                            c = 0;
                        break;
                case 0xb:
                        c = 0xa;
                        break;
                case 0xc:
                        c = 0xb;
                        break;
                }
                c |= cx ^ ((cx & 020)<<1);
        }

        if (D[chan] & DEV_BACK) 
            W[chan] |= ((t_uint64)c) << ((CC[chan]) * 6);
        else
            W[chan] |= ((t_uint64)c) << ((7 - CC[chan]) * 6);
        CC[chan]++;

        if (CC[chan] == 8) {
            uint16      addr = (uint16)(D[chan] & CORE);
                
            M[addr] = W[chan];
            sim_debug(DEBUG_DATA, &chan_dev, "write(%d, %05o, %016llo)\n", 
                        chan, addr, W[chan]);   
        
            if (chan_advance(chan)) 
                return 1;
            W[chan] = 0;
        }
        if (flags) {
            if ((D[chan] & (DEV_BIN|DEV_WCFLG)) == 0) {
                 /* Insert group mark */
                 if (D[chan] & DEV_BACK) {
                     int i = CC[chan];
                     W[chan] |= (t_uint64)037LL << ((CC[chan]) * 6);
                     
                      while(i < 8) {
                          W[chan] |= (t_uint64)014LL << (i * 6);
                          i++;
                      }
                 } else {
                      W[chan] |= 037LL << ((7 - CC[chan]) * 6);
                 }
                 CC[chan]++;
            }
                
            /* Flush last word */
            if (CC[chan] != 0) {
                uint16      addr = (uint16)(D[chan] & CORE);

                M[addr] = W[chan];
                sim_debug(DEBUG_DATA, &chan_dev, "writef(%d, %05o, %016llo)\n",
                         chan, addr, W[chan]);   
                (void)chan_advance(chan);
                W[chan] = 0;
            }
            status[chan] |= EOR;
            return 1;
        }

        return 0;
}

/* Returns 1 on last character. If it returns 1, the
   character in ch is not valid. If flag is set to 1, then
   this is the last character the device will request.
*/
int chan_read_char(int chan, uint8 *ch, int flags) {
        uint8   c;
        int     gm;

        if (status[chan] & EOR) 
           return 1;

        if (D[chan] & DEV_INHTRF) {
           status[chan] |= EOR;
           return 1;
        }

        if (CC[chan] == 0) {
            uint16      addr = (uint16)(D[chan] & CORE);
            if (chan_advance(chan))
                return 1;
            sim_debug(DEBUG_DATA, &chan_dev, "read(%d, %05o, %016llo)\n", chan,
                 addr, W[chan]);   
        }

        if (D[chan] & DEV_BACK) 
            c = 077 & (W[chan] >> ((CC[chan]) * 6));
        else
            c = 077 & (W[chan] >> ((7 - CC[chan]) * 6));
        gm = (c == 037);
        CC[chan]++;
        if (CC[chan] == 8) {
            CC[chan] = 0;
        }
        if ((D[chan] & DEV_BIN) == 0) {
                /* Translate BCD to BCL */
                uint8   cx = c & 060;
                c &= 017;
                switch(c) {
                case 0:
                        if (cx != 060)
                            c = 0xa;
                        break;
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 0xd:
                case 0xe:
                case 0xf:
                        break;
                case 0xa:
                        c = 0xb;
                        break;
                case 0xb:
                        c = 0xc;
                        break;
                case 0xc:
                        if (cx == 060)
                           c = 0xa;
                        else
                           c = 0;
                        break;
                }
                c |= cx ^ ((cx & 020)<<1);
        }
        *ch = c;
        if ((status[chan] & USEGM) != 0 && (D[chan] & DEV_WCFLG) == 0 && gm) {
            status[chan] |= EOR;
            return 1;
        }
        if (flags) {
            status[chan] |= EOR;
        }
        return 0;
}

/* Same as chan_read_char, however we do not check word count 
   nor do we advance it. 
*/
int chan_read_disk(int chan, uint8 *ch, int flags) {
        uint8   c;

        if (CC[chan] == 0) {
            uint16      addr = (uint16)(D[chan] & CORE);
            if (addr > MEMSIZE) {
                D[chan] |= DEV_MEMERR;
                return 1;
            }
                
            W[chan] = M[addr];
            D[chan] &= ~CORE;
            D[chan] |= (addr + 1) & CORE;
        }

        c = 077 & (W[chan] >> ((7 - CC[chan]) * 6));
        CC[chan]++;
        *ch = c;
        if (CC[chan] == 8) {
            CC[chan] = 0;
            return 1;
        }
        return 0;
}

int
chan_advance_drum(int chan) {
    uint16      addr = (uint16)(D[chan] & CORE);
    uint16      wc = WC(D[chan]);
    if (wc == 0) {
        status[chan] |= EOR;
        return 1;
    }
    D[chan] &= ~DEV_WC;
    D[chan] |= toWC(wc-1);
    if (addr > MEMSIZE) {
        D[chan] |= DEV_MEMERR;
        status[chan] |= EOR;
        return 1;
    }
                
    W[chan] = M[addr];
    D[chan] &= ~CORE;
    D[chan] |= (addr + 1) & CORE;
    CC[chan] = 0;
    return 0;
}

/* returns 1 when channel can take no more characters. 
   A return of 1 indicates that the character was not
   processed.
*/
int chan_write_drum(int chan, uint8 *ch, int flags) {
        uint8   c;

        if (status[chan] & EOR) 
           return 1;

        /* Check if first word */
        if (CC[chan] == 0) {
            uint16      wc = WC(D[chan]);
                
            if (wc == 0) {
                    status[chan] |= EOR;
                    return 1;
            }
            W[chan] = 0;
        }

        c = *ch;
        c &= 077;

        W[chan] |= (t_uint64)c << ((7 - CC[chan]) * 6);
        CC[chan]++;

        if (CC[chan] == 8) {
            uint16      addr = (uint16)(D[chan] & CORE);
                
            M[addr] = W[chan];
            if (chan_advance_drum(chan)) 
                return 1;
        }
        if (flags) {
            /* Flush last word */
            if (CC[chan] != 0) {
                uint16      addr = (uint16)(D[chan] & CORE);

                M[addr] = W[chan];
                (void)chan_advance_drum(chan);
            }
            status[chan] |= EOR;
            return 1;
        }

        return 0;
}

/* Returns 1 on last character. If it returns 1, the
   character in ch is not valid. If flag is set to 1, then
   this is the last character the device will request.
*/
int chan_read_drum(int chan, uint8 *ch, int flags) {
        uint8   c;

        if (status[chan] & EOR) 
           return 1;


        if (CC[chan] == 0) {
            if (chan_advance_drum(chan))
                return 1;
        }

        c = 077 & (W[chan] >> ((7 - CC[chan]) * 6));
        CC[chan]++;
        if (CC[chan] == 8) {
            CC[chan] = 0;
        }
        *ch = c;
        if (flags) {
            status[chan] |= EOR;
        }
        return 0;
}

