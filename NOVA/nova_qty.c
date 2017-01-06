/* nova_qty.c: NOVA multiplexor (QTY/ALM) simulator

   Copyright (c) 2000-2015, Robert M. Supnik
   Written by Bruce Ray and used with his gracious permission.

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   qty          multiplexor: QTY = 4060, ALM = 42xx

   28-Mar-15    RMS     Revised to use sim_printf
   14-Mar-12    RMS     Fixed dangling else clauses
   04-Jul-07    BKR     Fixed QTY output line number calculation (affected higher line numbers),
   25-Mar-04    RMS     Updated for V3.2
   12-Jan-04    BKR     Initial release
                        includes both original DG "quad" multiplexor (QTY)
                        and later Asynchronous Line Multiplexor (ALM) support.
*/


/*----------------------------------------------------------------------*/
/*                      QTY [4060-compatible] multiplexor               */
/*----------------------------------------------------------------------*/

/*
 *      Emulate the DG 4060 "quad" (QTY) serial port multiplexor.  DG modem
 *      control is not supported in this revision due to its obtuse nature
 *      of using a separate [semi-secret] device MDM which is actually part
 *      of the DG 4026/4027 multiplexor hardware(!).
 *      (Full modem support is provided in the ALM driver.)
 *
 *
 *      4060 Hardware
 *
 *      device code:    030 [primary],
 *                      070 [secondary]
 *      interrupt mask: B14 [000002]
 *      ASM mnemonic:   QTY
 *
 *
 *      4060 Input/Output Word Format:
 *
 *      _________________________________________________________________
 *      | RI| TI|        channel        |           character           |
 *      ----+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *         0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
 *
 *
 *              RI      - receiver interrupt
 *              TI      - transmitter interrupt
 *              channel - channel number, 0 - 63.
 *              character- character (valid if receiver interrupt, undefined if transmitter)
 *
 *      Notes:
 *
 *      Maximum 64 lines supported.
 *      DONE set whenever any received character fully assembled and ready,
 *              or when any output character transmitted and line is ready
 *              to accept next output character.
 *      BUSY set whenever output character is being sent on any line.
 *      Note that early 4060s did NOT have a busy flag!
 *      IORST clears device Done, no other user instruction does.
 *      IORST clears each line's individual R.I. and T.I.
 *
 *
 *      Instructions:
 *
 *      DIA     get multiplexor status word [format defined above]
 *      DOA     send character to QTY line [format defined above, RI & SI ]
 *      DIB     <ignored> [returns backplane bus noise]
 *      DOB     clear QTY line
 *      DIC     <ignored> [returns backplace bus noise]
 *      DOC     <ignored>
 *      'C'     clears global done, then checks for RI and TI;
 *      'P'     <ignored>
 *      'S'     <ignored>
 */


#include "nova_defs.h"

#include "sim_sock.h"
#include "sim_tmxr.h"


#define UNIT_V_8B   (UNIT_V_UF + 0)                     /* 8b output */
#define UNIT_8B     (1 << UNIT_V_8B)



extern int32    int_req, dev_busy, dev_done, dev_disable ;
extern int32    tmxr_poll ;                             /* calibrated delay */

t_stat  qty_setnl   ( UNIT * uptr, int32 val, CONST char * cptr, void * desc ) ;

t_stat  qty_attach  ( UNIT * uptr, CONST char * cptr ) ;
t_stat  qty_detach  ( UNIT * uptr ) ;
t_stat  qty_reset   ( DEVICE * dptr ) ;
t_stat  qty_svc     ( UNIT * uptr ) ;
int32   qty         ( int32 pulse, int32 code, int32 AC ) ;

t_stat  alm_reset   ( DEVICE * dptr ) ;
t_stat  alm_svc     ( UNIT * uptr ) ;
int32   alm         ( int32 pulse, int32 code, int32 AC ) ;

extern DEVICE  alm_dev ;


#define QTY_MAX     64                          /*  max number of QTY lines - hardware  */


int32   qty_brkio   = SCPE_OK ;                         /*  default I/O status code     */
int32   qty_max     = QTY_MAX ;                         /*  max # QTY lines - user      */
                                                        /*  controllable                */
int32   qty_mdm     = 0 ;                               /*  QTY modem control active?   */
int32   qty_auto    = 0 ;                               /*  QTY auto disconnect active? */
int32   qty_polls   = 0 ;                               /*  total 'qty_svc' polls       */


TMLN    qty_ldsc[ QTY_MAX ] = { {0} } ;                 /*  QTY line descriptors        */
TMXR    qty_desc    = { QTY_MAX, 0, 0, qty_ldsc } ;     /*  mux descriptor      */
int32   qty_status[ QTY_MAX ] = { 0 } ;                 /*  QTY line status             */
                                                        /*  (must be at least 32 bits)  */
int32   qty_tx_chr[ QTY_MAX ] = { 0 } ;                 /*  QTY line output character   */


/* QTY data structures

   qty_dev      QTY device descriptor
   qty_unit     QTY unit descriptor
   qty_reg      QTY register list
*/

DIB qty_dib = { DEV_QTY, INT_QTY, PI_QTY, &qty } ;

UNIT    qty_unit =
        {
        UDATA (&qty_svc, (UNIT_ATTABLE), 0)
        } ;

REG qty_reg[] =  /*  ('alm_reg' should be similar to this except for device code related items)  */
        {
        { ORDATA (BUF, qty_unit.buf, 8) },
        { FLDATA (BUSY, dev_busy, INT_V_QTY) },
        { FLDATA (DONE, dev_done, INT_V_QTY) },
        { FLDATA (DISABLE, dev_disable, INT_V_QTY) },
        { FLDATA (INT, int_req, INT_V_QTY) },

        { FLDATA (MDMCTL, qty_mdm,  0) },
        { FLDATA (AUTODS, qty_auto, 0) },
        { DRDATA (POLLS, qty_polls, 32) },
        { NULL }
        } ;

MTAB    qty_mod[] =
        {
        { UNIT_8B, 0, "7b", "7B", NULL },
        { UNIT_8B, UNIT_8B, "8b", "8B", NULL },
        { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
            &tmxr_dscln, NULL, (void *)&qty_desc },
        { UNIT_ATT, UNIT_ATT, "connections", NULL,
          NULL, &tmxr_show_summ, (void *)&qty_desc },
        { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
            NULL, &tmxr_show_cstat, (void *)&qty_desc },
        { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
            NULL, &tmxr_show_cstat, (void *)&qty_desc },
        { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
            &qty_setnl, &tmxr_show_lines, (void *) &qty_desc },
        { 0 }
        } ;

DEVICE  qty_dev =
        {
        "QTY", &qty_unit, qty_reg, qty_mod,
        1, 10, 31, 1, 8, 8,
        NULL, NULL, &qty_reset,
        NULL, &qty_attach, &qty_detach,
        &qty_dib, (DEV_DISABLE | DEV_DIS | DEV_MUX)
        };

#define DG_RETURN( status, data )   (int32)(((status) << IOT_V_REASON) | ((data) & 0x0FFFF) )

/*
 *      QTY_S_xxx               QTY device status reference
 *      QTY_L_xxx               QTY line status word reference (qty_status[])
 */

        /*----------------------------------------------*/
        /*                QTY device status             */
        /*----------------------------------------------*/

#define QTY_S_RI        0x8000                          /*  Receiver Interrupt          */
#define QTY_S_TI        0x4000                          /*  Transmitter interrupt       */
#define QTY_S_LMASK     0x3F00                          /*  line mask                   */
#define QTY_S_DMASK     0x00FF                          /*  data mask (received char)   */



#define QTY_MASTER_ACTIVE( desc )   ( (desc)->master )

#define QTY_LINE_EXTRACT( x )       (((x) & QTY_S_LMASK) >> 8)

#define QTY_LINE_TX_CHAR( line )    qty_tx_chr[ ((line) % QTY_MAX) ]
#define QTY_LINE_RX_CHAR( line )    (qty_status[ (line) ] & QTY_S_DMASK)
#define QTY_UNIT_ACTIVE( unitp )    ( (unitp)->conn )

#define QTY_LINE_BITS( line, bits ) (qty_status[ (line) ] & bits)

#define QTY_LINE_SET_BIT(   line, bit )  qty_status[ (line) ] |=  (bit)  ;
#define QTY_LINE_CLEAR_BIT( line, bit )  qty_status[ (line) ] &= ~(bit)  ;
#define QTY_LINE_BIT_SET( line, bit )   (qty_status[ (line) ] &   (bit))


        /*----------------------------------------------*/
        /*                  QTY line status             */
        /*----------------------------------------------*/

#define QTY_L_RXE       0x800000                        /*  receiver enabled?           */
#define QTY_L_RXBZ      0x400000                        /*  receiver busy?              */
#define QTY_L_RXDN      0x200000                        /*  receiver done?              */
#define QTY_L_TXE       0x080000                        /*  transmitter enabled?        */
#define QTY_L_TXBZ      0x040000                        /*  transmitter busy?           */
#define QTY_L_TXDN      0x020000                        /*  transmitter done?           */

#define QTY_L_BREAK     0x008000                        /*  BREAK character received    */
#define QTY_L_RING      0x004000                        /*  Ring interrupt              */
#define QTY_L_CD        0x002000                        /*  Carrier Detect              */
#define QTY_L_DTR       0x001000                        /*  Data Terminal Ready         */
                                                        /*  <0x00FF = character>        */

#define QTY_L_LOOPBK    0x00010000                      /*  loopback mode               */
#define QTY_L_OVRERR    0x00020000                      /*  overrun error               */
#define QTY_L_FRMERR    0x00040000                      /*  framing error               */
#define QTY_L_PARERR    0x00080000                      /*  parity error                */


/* CD, CTS, DSR, RI */
                                                        /*  <future>  */

#define QTY_L_MODEM     0x0080                          /*  <not yet used>      */
#define QTY_L_TELNET    0x0040                          /*  <not yet used>      */
#define QTY_L_AUTODIS   0x0020                          /*  <not yet used>      */
#define QTY_L_PARITY
#define QTY_L_7BIT
#define QTY_L_BAUD                                      /*  <4 bits>            */


#define QTY_L_DMASK     0x000FF                         /*  data mask (always 8 bits)   */

/*  Note:  use at least an 'int32' for this guy  */

    /*------------------------------*/
    /*        qty_tmxr_putc         */
    /*------------------------------*/

int qty_tmxr_putc( int line, TMLN * lp, int kar )
    {
    int     a ;

    /*----------------------------------------------*/
    /*  Send character to given QTY/telnet line.    */
    /*                                              */
    /*  enter:      line    QTY line #              */
    /*              lp      Telnet unit def ptr     */
    /*              kar     character to send       */
    /*                                              */
    /*  return:     SCPE_OK                         */
    /*              SCPE_STALL                      */
    /*              SCPE_LOST                       */
    /*----------------------------------------------*/

    a = tmxr_putc_ln( lp, kar ) ;
    if ( a == SCPE_OK)
        {
        QTY_LINE_SET_BIT(   line, QTY_L_TXDN )
        QTY_LINE_CLEAR_BIT( line, QTY_L_TXBZ )
        }
    else if ( a == SCPE_STALL )
        {
        /*
         (should we try to output the buffer
         and then regroup...?)
         */
        QTY_LINE_SET_BIT(   line, QTY_L_TXBZ )
        QTY_LINE_CLEAR_BIT( line, QTY_L_TXDN )
        QTY_LINE_TX_CHAR( line ) = kar ;
        }
    else if ( a == SCPE_LOST )
        {
        /*  no connection - hangup?  */
        QTY_LINE_SET_BIT(   line, QTY_L_TXBZ )
        QTY_LINE_CLEAR_BIT( line, QTY_L_TXDN )
        QTY_LINE_TX_CHAR( line ) = kar ;
        }
    return ( a ) ;
    }   /*  end of 'qty_tmxr_putc'  */


    /*----------------------------------------------*/
    /*                 qty_update_rcvi              */
    /*----------------------------------------------*/

int qty_update_rcvi( TMXR * mp )
    {
    int     line ;
    TMLN *      lp ;
    int32       datum ;
    int     changes ;

    /*------------------------------------------------------*/
    /*  Search through connected telnet lines for any input */
    /*  activity.                                           */
    /*                                                      */
    /*  enter:      mp      master telnet qty desc ptr      */
    /*                                                      */
    /*  return:     int     change count (0 = none seen)    */
    /*------------------------------------------------------*/

    for ( changes = line = 0; line < mp->lines; ++line )
       if ( (lp=mp->ldsc+line)->conn && lp->rcve )
          if ( (datum=tmxr_getc_ln(lp)) )
        {
        if ( datum & SCPE_BREAK )
            {
            /*  what should we do here - set QTY_L_BREAK?  */
            datum = datum & 0x00FF ;
            }
        else
            {
            datum = datum & 0x00FF ;
            }
         /*  <check parity, masking, forced parity, CR/LF xlation>  */

        QTY_LINE_CLEAR_BIT( line, (QTY_L_RXBZ | QTY_L_DMASK) ) ;
        QTY_LINE_SET_BIT( line, (QTY_L_RXDN | datum) ) ;
        ++changes ;
        }
    return ( changes ) ;
    }   /*  end of 'qty_update_rcvi'  */


    /*----------------------------------------------*/
    /*                qty_update_xmti               */
    /*----------------------------------------------*/

int qty_update_xmti( TMXR * mp )
    {
    int     line ;
    TMLN *      lp ;
    int     changes ;

    /*------------------------------------------------------*/
    /*  Search through connected telnet lines for any de-   */
    /*  ferred output activity.                             */
    /*                                                      */
    /*  enter:      mp      master telnet qty desc ptr      */
    /*                                                      */
    /*  return:     int     change count (0 = none seen)    */
    /*------------------------------------------------------*/

    /*  any TX DONE flags set
     *  any TX BUSY flags set
     */

    for ( changes = line = 0; line < mp->lines; ++line )
       if ( QTY_LINE_BIT_SET(line,QTY_L_TXBZ) )
          if ( (lp=mp->ldsc+line)->conn && lp->xmte )
        {
        /*  why are we busy?  buffer was full?  */
        /*  now some space available - try
         *  to stuff pending character in
         *  buffer and free up the world
         */
        qty_tmxr_putc( line, lp, QTY_LINE_TX_CHAR(line) ) ;
        ++changes ;
        }
    return ( changes ) ;
    }   /*  end of 'qty_update_xmti'  */


    /*----------------------------------------------*/
    /*                qty_update_status             */
    /*----------------------------------------------*/

int qty_update_status( DIB * dibp, TMXR * tmxr_desc )
    {
    int     line ;
    int     status ;
    int     txbusy ;

    /*----------------------------------------------*/
    /*  return global device status for current qty */
    /*  state.                                      */
    /*                                              */
    /*  Receiver interrupts have higher priority    */
    /*  than transmitter interrupts according to DG */
    /*  but this routine could be modified to use   */
    /*  different priority criteria.                */
    /*                                              */
    /*  Round-robin polling could also be used in   */
    /*  some future release rather than starting    */
    /*  with line 0 each time.                      */
    /*                                              */
    /*  Return <QTY_S_RI + line # + character> of   */
    /*  first waiting character, else return        */
    /*  <QTY_S_TI + line #> of first finished line  */
    /*  output, else return 0.                      */
    /*                                              */
    /*  This routine does -not- clear input line    */
    /*  BZ/DN flags; caller should do this.         */
    /*                                              */
    /*  Global device done and busy flags are       */
    /*  updated.                    */
    /*----------------------------------------------*/

    for ( txbusy = status = line = 0 ; line < qty_max ; ++line )
        {
        txbusy |= (QTY_LINE_BIT_SET(line,QTY_L_TXBZ)) ;
        if ( QTY_LINE_BIT_SET(line,QTY_L_RXDN) )
            {
            if ( ! status )
                {
                status = QTY_LINE_BITS( line, QTY_S_DMASK ) | QTY_S_RI ;
                status = status | (line << 8) ;
                }
            break ;
            }
        else if ( QTY_LINE_BIT_SET(line,QTY_L_TXDN) )
            {
            if ( ! (status & QTY_S_RI) )
                if ( ! (status & QTY_S_RI) )
                {
                status = QTY_S_TI ;
                status = status | (line << 8) ;
                }
            }
        }
    /*  <we could check each line for TX busy to set DEV_SET_BUSY)?>  */
    DEV_CLR_BUSY( INT_QTY ) ;
    DEV_CLR_DONE( INT_QTY ) ;
    if ( txbusy )
        {
        DEV_SET_BUSY( INT_QTY ) ;
        }
    if ( status & (QTY_S_RI | QTY_S_TI) )
        {
        DEV_SET_DONE( INT_QTY ) ;
        }
    DEV_UPDATE_INTR ;                                   /*  update final intr status  */
        return ( status ) ;
    }   /*  end of 'qty_update_status'  */


    /*--------------------------------------------------------------*/
    /*                            qty_attach                        */
    /*--------------------------------------------------------------*/

t_stat qty_attach( UNIT * unitp, CONST char * cptr )
    {
    t_stat  r ;
    int a ;

    /*  switches:   A       auto-disconnect
     *              M       modem control
     */

    qty_mdm = qty_auto = 0;                             /* modem ctl off */
    r = tmxr_attach( &qty_desc, unitp, cptr ) ;         /* attach QTY */
    if ( r != SCPE_OK )
        {
        return ( r ) ;                                  /* error! */
        }
    if ( sim_switches & SWMASK('M') )                   /* modem control? */
        {
        qty_mdm = 1;
        sim_printf( "Modem control activated\n" ) ;
        if ( sim_switches & SWMASK ('A') )              /* autodisconnect? */
            {
            qty_auto = 1 ;
            sim_printf( "Auto disconnect activated\n" ) ;
            }
        }
    qty_polls = 0 ;
    for ( a = 0 ; a < QTY_MAX ; ++a )
        {
        /*  QTY lines are always enabled - force RX and TX to 'enabled' */
        qty_status[ a ] = (QTY_L_RXE | QTY_L_TXE) ;
        }
    sim_activate( unitp, tmxr_poll ) ;
    return ( SCPE_OK ) ;
    }   /*  end of 'qty_attach'  */


    /*--------------------------------------------------------------*/
    /*                            qty_detach                        */
    /*--------------------------------------------------------------*/

t_stat qty_detach( UNIT * unitp )
    {
    sim_cancel( unitp ) ;
    return ( tmxr_detach(&qty_desc,unitp) ) ;
    }   /*  end of 'qty_detach'  */


    /*--------------------------------------------------------------*/
    /*                             qty_clear                        */
    /*--------------------------------------------------------------*/

t_stat qty_clear( t_bool flag )
    {
    int line ;

    for ( line = 0 ; line < qty_max ; ++line )
        {
        qty_ldsc[line].xmte = 0 ;
        qty_ldsc[line].rcve = 0 ;
        if ( ! qty_ldsc[line].conn )
            {
            qty_ldsc[line].xmte = 1 ;                   /* set xmt enb */
            qty_ldsc[line].rcve = 1 ;                   /* clr rcv enb */
            }
        }
    return ( SCPE_OK ) ;
    }   /*  end of 'qty_clear'  */


    /*----------------------------------------------*/
    /*                 qty_common_reset             */
    /*----------------------------------------------*/

t_stat qty_common_reset( DIB * dibp, UNIT * unitp, DEVICE * dptr )
    {
    if ((dptr->flags & DEV_DIS) == 0)
        {
        if (dptr == &qty_dev) alm_dev.flags |= DEV_DIS;
        else qty_dev.flags |= DEV_DIS;
        }
    qty_clear( TRUE ) ;
    DEV_CLR_BUSY( INT_QTY ) ;                              /*  clear busy  */
    DEV_CLR_DONE( INT_QTY ) ;                              /*  clear done, int */
    DEV_UPDATE_INTR ;
    if ( QTY_MASTER_ACTIVE(&qty_desc) )
        {
        sim_activate( unitp, tmxr_poll ) ;
        }
    else
        {
        sim_cancel( unitp ) ;
        }
    return ( SCPE_OK ) ;
    }   /*  end of 'qty_common_reset'  */


    /*--------------------------------------------------------------*/
    /*                            qty_reset                         */
    /*--------------------------------------------------------------*/

t_stat qty_reset( DEVICE * dptr )
    {
    return ( qty_common_reset(&qty_dib,&qty_unit,dptr) ) ;
    }   /*  end of 'qty_reset'  */


/* Unit service routine

   The QTY/ALM polls to see if asynchronous activity has occurred and now
   needs to be processed.  The polling interval is controlled by the clock
   simulator, so for most environments, it is calibrated to real time.

   The simulator assumes that software enables all of the multiplexors,
   or none of them.
*/

    /*----------------------------------------------*/
    /*                  qty_common_svc              */
    /*----------------------------------------------*/

t_stat qty_common_svc( DIB * dibp, UNIT * unitp )
    {
    int     line ;
    int     newln ;
    TMLN *      tmlnp ;

    ++qty_polls ;                                       /*  another time 'round the track  */
    newln = tmxr_poll_conn( &qty_desc ) ;               /*  anybody knocking at the door?  */
    if ( (newln >= 0) && qty_mdm )
        {
        if ( newln >= qty_max )
            {
            return SCPE_IERR;                               /*  WTF - sanity check failed, over?  */
            }
        else
            {
            line = newln ;                                  /*  handle modem control  */
            tmlnp =&qty_ldsc[ line ] ;
            tmlnp->rcve = tmlnp->xmte = 1 ;
            /*  do QTY_LINE_ bit fiddling and state machine
             *  manipulation with modem control signals
             */
            }
        }

    tmxr_poll_rx( &qty_desc ) ;                         /*  poll input                          */
    qty_update_rcvi( &qty_desc ) ;                      /*  update receiver interrupt status    */

    tmxr_poll_tx( &qty_desc ) ;                         /*  poll output                         */
    qty_update_xmti( &qty_desc ) ;                      /*  update transmitter interrupt status */

    qty_update_status( dibp, &qty_desc ) ;              /*  update device status                */

    sim_activate( unitp, tmxr_poll ) ;                  /*  restart the bubble machine          */
    return ( SCPE_OK ) ;
    }                                                   /*  end of 'qty_common_svc'  */


    /*--------------------------------------------------------------*/
    /*                            qty_svc                           */
    /*--------------------------------------------------------------*/

t_stat qty_svc( UNIT * uptr )
    {
    return ( qty_common_svc(&qty_dib,uptr) ) ;
    }                                                   /*  end of 'qty_svc'  */


    /*--------------------------------------------------------------*/
    /*                              qty                             */
    /*--------------------------------------------------------------*/

int32 qty( int32 pulse, int32 code, int32 AC )
    {
    int32       iodata ;
    int32       ioresult ;
    int     line ;
    TMLN *      tmlnp ;
    int     a ;
    int     kar ;

    /*--------------------------------------------------------------*/
    /*  DG 4060[-compatible] "quad" multiplexor instruction handler */
    /*--------------------------------------------------------------*/

    ioresult= qty_brkio ;   /*  (assume returning I/O break value   */
    iodata  = 0 ;           /*  (assume 16-bit Nova/Eclipse bus)    */
    switch ( code )
        {
    case ioNIO :    /*  <no operation>  */
        break ;

    case ioDIA :    /*  get current QTY status  */
        iodata = qty_update_status( &qty_dib, &qty_desc ) ;
        if ( iodata & QTY_S_RI )
            {                                           /*  clear line's input buffer  */
            QTY_LINE_CLEAR_BIT( (QTY_LINE_EXTRACT(iodata)), (QTY_L_RXBZ | QTY_L_RXDN) )
            /*
            character masking ;
            parity checking ;
            parity generating ;
             */
            }
        qty_update_status( &qty_dib, &qty_desc ) ;
        break ;

    case ioDOA :    /*  send character to QTY  */
        line = QTY_LINE_EXTRACT( AC ) ;
        if ( line < qty_max )
            if ( QTY_LINE_BIT_SET(line,QTY_L_TXE) )
            {
            /*
            perform any character translation:
            7 bit/ 8 bit
            parity generation
             */
            kar = AC & ((qty_unit.flags & UNIT_8B)? 0377: 0177) ;
            /*  do any parity calculations also  */

            tmlnp = &qty_ldsc[ line ] ;
            a = qty_tmxr_putc( line, tmlnp, kar ) ;
            if ( a != SCPE_OK)
                {
                /*  do anything at this point?  */
                }
            qty_update_status( &qty_dib, &qty_desc ) ;
            }
        break ;

    case ioDIB :    /*  no QTY function - return bus noise in AC  */
        break ;

    case ioDOB :    /*  clear QTY output channel busy and done flag  */
        QTY_LINE_CLEAR_BIT( (QTY_LINE_EXTRACT(AC)), (QTY_L_TXBZ | QTY_L_TXDN) )
        qty_update_status( &qty_dib, &qty_desc ) ;
        break ;

    case ioDIC :    /*  no QTY function - return bus noise in AC  */
        break ;

    case ioDOC :    /*  no QTY function - ignore  */
        break ;

    case ioSKP :    /*  I/O skip test - should never come here  */
        break ;

    default :
        /*  <illegal I/O operation value>  */
        break ;
        }

    switch ( pulse )
        {
    case iopN :     /*  <ignored (of course)>  */
        break ;

    case iopS :     /*  <ignored>  */
        break ;

    case iopP :     /*  <ignored>  */
        break ;

    case iopC :
        qty_update_status( &qty_dib, &qty_desc ) ;
        break ;

    default :
        /*  <illegal pulse value>  */
        break ;
        }

    return ( DG_RETURN( ioresult, iodata ) ) ;
    }   /*  end of 'qty'  */

    /*--------------------------------------------------------------*/
    /*                             qty_setnl                        */
    /*--------------------------------------------------------------*/

t_stat qty_setnl( UNIT * uptr, int32 val, CONST char * cptr, void * desc )
    {
    int32   newln, i, t ;

    t_stat  r ;
    if ( cptr == NULL )
        {
        return ( SCPE_ARG ) ;
        }
    newln = (int32) get_uint( cptr, 10, QTY_MAX, &r ) ;
    if ( (r != SCPE_OK) || (newln == qty_desc.lines) )
        {
        return ( r ) ;
        }
    if ( (newln == 0) || (newln > QTY_MAX) )
        {
        return ( SCPE_ARG ) ;
        }
    if ( newln < qty_desc.lines )
        {
        for ( i = newln, t = 0 ; i < qty_desc.lines ; ++i )
            {
            t = t | qty_ldsc[i].conn ;
            }
        if ( t && ! get_yn("This will disconnect users; proceed [N]?", FALSE) )
            {
            return ( SCPE_OK ) ;
            }
        for ( i = newln ; i < qty_desc.lines ; ++i )
            {
            if ( qty_ldsc[i].conn )
                {                                       /* reset line */
                tmxr_msg( qty_ldsc[i].conn, "\r\nOperator disconnected line\r\n" ) ;
                tmxr_reset_ln( &qty_ldsc[i] ) ;
                }
            qty_clear( TRUE ) ;                         /* reset mux */
            }
        }
    qty_max = qty_desc.lines = newln ;
        /*  Huh, I don't understand this yet...
        qty_max = ((qty_dev.flags & DEV_DIS)? 0 : (qty_desc.lines / QTY_MAX)) ;
         */
    return ( SCPE_OK ) ;
    }   /*  end of 'qty_setnl'  */


/*----------------------------------------------------------------------*/
/*                       ALM [425x-compatible] multiplexor              */
/*----------------------------------------------------------------------*/

/*
 *      device code:    034 [primary],
 *                      074 [secondary]
 *      interrupt mask: B14 [000002]
 *      ASM mnemonic:   ALM
 *
 *      ALM [4255-4258] I/O instructions
 *
 *      DIA     read line and section requesting service
 *      DOA     select line and section (lines 0-255, 8-bits) + rcvr/xmit
 *      DIB     receive data
 *      DOB     00  transmit data
 *              01  transmit BREAK
 *              10  set modem control status
 *              11  <ignored>
 *      DIC     read receiver or modem status
 *      DOC     00  control line section and diag mode
 *              01  
 *              10  specify line characteristics
 *              11
 *
 *      undocumented DG "features":
 *
 *              NIOS sets board offline
 *              NIOC sets board online
 *              Modem control signal state change can signal interrupt
 *              explicit line select with DOA
 *              implicit line select with DIA
 *
 *      We support 64 lines maximum in this release although some ALM's could
 *      theoretically support up to 256.
 */


DIB alm_dib = { DEV_ALM, INT_ALM, PI_ALM, &alm } ;
UNIT    alm_unit =
        {
        UDATA (&alm_svc, (UNIT_ATTABLE), 0)
        } ;

REG alm_reg[] =  /*  ('qty_reg' should be similar to this except for device code related items)  */
        {
        { ORDATA (BUF, alm_unit.buf, 8) },
        { FLDATA (BUSY, dev_busy, INT_V_ALM) },
        { FLDATA (DONE, dev_done, INT_V_ALM) },
        { FLDATA (DISABLE, dev_disable, INT_V_ALM) },
        { FLDATA (INT, int_req, INT_V_ALM) },

        { FLDATA (MDMCTL, qty_mdm,  0) },
        { FLDATA (AUTODS, qty_auto, 0) },
        { DRDATA (POLLS, qty_polls, 32) },
        { NULL }
        } ;

DEVICE  alm_dev =
        {
        "ALM", &alm_unit, alm_reg, qty_mod,
        1, 10, 31, 1, 8, 8,
        NULL, NULL, &alm_reset,
        NULL, &qty_attach, &qty_detach,
        &alm_dib, (DEV_DISABLE | DEV_NET)
        } ;

int alm_section     = -1 ;      /*  current line "section" (0 = RCV, 1 = XMT)  */
int alm_line        = -1 ;      /*  current line [0-63]                 */
int alm_diag_mode   =  0 ;      /*  <not yet supported>                 */
int alm_line_mask   = 0x003F ;  /*  maximum of 64 lines in this rev     */


#define ALM_LINE_EXTRACT( x )       (((x) >> 1) & alm_line_mask)
#define ALM_SECT_EXTRACT( x )       ((x) & 0x0001)


    /*--------------------------------------------------------------*/
    /*                            alm_reset                         */
    /*--------------------------------------------------------------*/

t_stat alm_reset( DEVICE * dptr )
    {
    return ( qty_common_reset(&alm_dib,&alm_unit,dptr) ) ;
    }   /*  end of 'alm_reset'  */


    /*--------------------------------------------------------------*/
    /*                            alm_svc                           */
    /*--------------------------------------------------------------*/

t_stat alm_svc( UNIT * uptr )
    {
    return ( qty_common_svc(&alm_dib,uptr) ) ;
    }   /*  end of 'alm_svc'  */


    /*--------------------------------------------------------------*/
    /*                              alm                             */
    /*--------------------------------------------------------------*/

int32 alm( int32 pulse, int32 code, int32 AC )
    {
    int32       iodata ;
    int32       ioresult ;
    TMLN *      tmlnp ;
    int     a ;
    int     kar ;

    /*--------------------------------------------------------------*/
    /*  DG 425x[-compatible] "ALM" multiplexor instruction handler  */
    /*--------------------------------------------------------------*/

    ioresult= qty_brkio ;   /*  (assume returning I/O break value   */
    iodata  = 0 ;           /*  (assume 16-bit Nova/Eclipse bus)    */
    switch ( code )
        {
    case ioNIO :    /*  <no operation>  */
        break ;

    case ioDIA :    /*  read line and section requesting service  */
        iodata = qty_update_status( &alm_dib, &qty_desc ) ;
        alm_line = (QTY_LINE_EXTRACT(iodata) & alm_line_mask) ;
        /*  (mask with 'alm_line_mask' in case ALM mask is different than QTY */
        alm_section = 0 ;
        if ( ! ( iodata & QTY_S_RI) )
          if ( iodata & QTY_S_TI )
            {
            alm_section = 1 ;                           /*  receiver quiet - transmitter done  */
            }
        iodata = (alm_line << 1) | alm_section ;
        break ;

    case ioDOA :    /*  set line and section  */
        alm_section = ALM_SECT_EXTRACT( AC ) ;
        alm_line    = ALM_LINE_EXTRACT( AC ) ;
        break ;

    case ioDIB :    /*  no ALM function - return bus noise in AC  */
        if ( alm_line < qty_max )
            {
            iodata = QTY_LINE_RX_CHAR( alm_line ) ;
            }
        break ;

    case ioDOB :    /*  output and modem control functions  */
        switch ( (AC >> 14) & 03 )
            {
        case 00 :   /*  transmit data  */
            if ( alm_line < qty_max )
                if ( QTY_LINE_BIT_SET(alm_line,QTY_L_TXE) )
                {
                /*
                perform any character translation:
                7 bit/ 8 bit
                parity generation
                 */
                kar = AC & ((alm_unit.flags & UNIT_8B)? 0377: 0177) ;
                /*  do any parity calculations also  */

                tmlnp = &qty_ldsc[ alm_line ] ;
                a = qty_tmxr_putc( alm_line, tmlnp, kar ) ;
                if ( a != SCPE_OK)
                    {
                    /*  do anything at this point?  */
                    }
                qty_update_status( &alm_dib, &qty_desc ) ;
                }
            break ;

        case 01 :   /*  transmit break  */
            if ( alm_line < qty_max )
                if ( QTY_LINE_BIT_SET(alm_line,QTY_L_TXE) )
                {
                tmlnp = &qty_ldsc[ alm_line ] ;
                /*
                a = qty_tmxr_putc( alm_line, tmlnp, kar ) ;
                if ( a != SCPE_OK)
                    {
                    }
                 */
                qty_update_status( &alm_dib, &qty_desc ) ;
                }
            break ;

        case 02 :   /*  set modem control status  */
            break ;

        case 03 :   /*  unused  */
            break ;
            }
        break ;

    case ioDIC :    /*  get modem or receiver status  */
        if ( alm_line < qty_max )
            {
            if ( alm_section )
                {
                /*  get modem section status  */
                if ( qty_ldsc[ alm_line ].xmte )
                    {
                    iodata = 0035 ;                     /*  set CD, CTS, DSR, MDM flags  */
                    }
                }
            else
                {
                /*  get receiver section status  */
                iodata = 0 ;                            /*  receiver error status - no errors by default  */
                }
            }
        break ;

    case ioDOC :    /*  set line attributes  */
        switch ( (AC >> 14) & 03 )
            {
        case 00 :   /*  control line section  */
            break ;

        case 01 :   /*  unused  */
            break ;

        case 02 :   /*  set line characteristics  */
            break ;

        case 03 :   /*  unused  */
            break ;
            }
        break ;

    case ioSKP :    /*  I/O skip test - should never come here  */
        break ;

    default :
        /*  <illegal I/O operation value>  */
        break ;
        }

    switch ( pulse )
        {
    case iopN : /*  <ignored (of course)>  */
        break ;

    case iopS : /*  set device busy
                 *  set all lines on board offline
                 *  clear each line's done
                 *  clear internal system
                 *  clear device busy
                 */
        for ( a = 0 ; a < qty_max ; ++a )
          if ( 1 /* (not yet optimized) */ )
            {
            QTY_LINE_CLEAR_BIT( a, (QTY_L_RXBZ | QTY_L_RXDN | QTY_L_TXBZ | QTY_L_TXDN) ) ;
            }
        qty_update_status( &alm_dib, &qty_desc ) ;
        break ;

    case iopP : /*  stop clock for all boards in off-line mode  */
        break ;

    case iopC :
        for ( a = 0 ; a < qty_max ; ++a )
          if ( 1 /* (not yet optimized) */ )
            {
            QTY_LINE_CLEAR_BIT( a, (QTY_L_RXBZ | QTY_L_RXDN | QTY_L_TXBZ | QTY_L_TXDN) ) ;
            }
        qty_update_status( &alm_dib, &qty_desc ) ;
        break ;

    default :
        /*  <illegal pulse value>  */
        break ;
        }

    return ( DG_RETURN( ioresult, iodata ) ) ;
    }   /*  end of 'alm'  */
