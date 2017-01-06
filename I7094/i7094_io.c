/* i7094_io.c: IBM 7094 I/O subsystem (channels)

   Copyright (c) 2003-2012, Robert M. Supnik

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

   chana..chanh         I/O channels

   19-Mar-12    RMS     Fixed declaration of breakpoint variables (Mark Pizzolato)

   Notes on channels and CTSS.

   - CTSS B-core is supported by the addition of a 16th bit to the current
     address field of the channel command.  Both the channel location counter
     and the channel current address register are widened to 16b.  Thus,
     channel programs can run in B-core, and channel transfers can access B-core.
     CTSS assumes that a channel command which starts a transfer in B-core
     will not access A-core; the 16th bit does not increment.
   - The channel start commands (RCHx and LCHx) incorporate the A-core/B-core
     select as part of effective address generation.  CTSS does not relocate
     RCHx and LCHx target addresses; because the relocation indicator is
     always zero, it's impossible to tell whether the protection indicator
     affects address generation.
   - The CTSS protection RPQ does not cover channel operations.  Thus, CTSS
     must inspect and vet all channel programs initiated by user mode programs,
     notably the background processor FMS.  CTSS inspects in-progress 7607
     channel programs to make sure than either the nostore bit or the B-core
     bit is set; thus, SCHx must store all 16b of the current address.
*/

#include "i7094_defs.h"

#define CHAMASK         ((cpu_model & I_CT)? PAMASK: AMASK) /* chan addr mask */
#define CHAINC(x)       (((x) & ~AMASK) | (((x) + 1) & AMASK))

typedef struct {
    const char  *name;
    uint32      flags;
    } DEV_CHAR;

uint32 ch_sta[NUM_CHAN];                                /* channel state */
uint32 ch_dso[NUM_CHAN];                                /* data select op */
uint32 ch_dsu[NUM_CHAN];                                /* data select unit */
uint32 ch_ndso[NUM_CHAN];                               /* non-data select op */
uint32 ch_ndsu[NUM_CHAN];                               /* non-data select unit */
uint32 ch_flags[NUM_CHAN];                              /* flags */
uint32 ch_clc[NUM_CHAN];                                /* chan loc ctr */
uint32 ch_op[NUM_CHAN];                                 /* channel op */
uint32 ch_wc[NUM_CHAN];                                 /* word count */
uint32 ch_ca[NUM_CHAN];                                 /* core address */
uint32 ch_lcc[NUM_CHAN];                                /* control cntr (7909) */
uint32 ch_cnd[NUM_CHAN];                                /* cond reg (7909) */
uint32 ch_sms[NUM_CHAN];                                /* cond mask reg (7909) */
t_uint64 ch_ar[NUM_CHAN];                               /* assembly register */
uint32 ch_idf[NUM_CHAN];                                /* channel input data flags */
DEVICE *ch2dev[NUM_CHAN] = { NULL };
uint32 ch_tpoll = 5;                                    /* channel poll */

extern t_uint64 *M;
extern uint32 cpu_model, data_base;
extern uint32 hst_ch;
extern uint32 ch_req;
extern uint32 chtr_inht, chtr_inhi, chtr_enab;
extern uint32 ind_ioc;
extern uint32 chtr_clk;
extern DEVICE cdr_dev, cdp_dev;
extern DEVICE lpt_dev;
extern DEVICE mt_dev[NUM_CHAN];
extern DEVICE drm_dev;
extern DEVICE dsk_dev;
extern DEVICE com_dev;

t_stat ch_reset (DEVICE *dptr);
t_stat ch6_svc (UNIT *uptr);
t_stat ch_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ch_set_disable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ch_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
DEVICE *ch_find_dev (uint32 ch, uint32 unit);
t_stat ch6_sel (uint32 ch, uint32 sel, uint32 unit, uint32 sta);
t_bool ch6_rd_putw (uint32 ch);
t_stat ch6_wr_getw (uint32 ch, t_bool eorz);
t_stat ch6_new_cmd (uint32 ch, t_bool ch_ld);
t_stat ch6_ioxt (uint32 ch);
void ch6_iosp_cclr (uint32 ch);
t_stat ch9_new_cmd (uint32 ch);
t_stat ch9_exec_cmd (uint32 ch, t_uint64 ir);
t_stat ch9_sel (uint32 ch, uint32 sel);
t_stat ch9_wr (uint32 ch, t_uint64 dat, uint32 fl);
t_stat ch9_rd_putw (uint32 ch);
t_stat ch9_wr_getw (uint32 ch);
void ch9_eval_int (uint32 ch, uint32 iflags);
DEVICE *ch_map_flags (uint32 ch, int32 fl);

extern t_stat ch_bkpt (uint32 ch, uint32 clc);

const uint32 col_masks[12] = {                          /* row 9,8,..,0,11,12 */
    00001, 00002, 00004,
    00010, 00020, 00040,
    00100, 00200, 00400,
    01000, 02000, 04000
    };

const t_uint64 bit_masks[36] = {
    0000000000001, 0000000000002, 0000000000004,
    0000000000010, 0000000000020, 0000000000040,
    0000000000100, 0000000000200, 0000000000400,
    0000000001000, 0000000002000, 0000000004000,
    0000000010000, 0000000020000, 0000000040000,
    0000000100000, 0000000200000, 0000000400000,
    0000001000000, 0000002000000, 0000004000000,
    0000010000000, 0000020000000, 0000040000000,
    0000100000000, 0000200000000, 0000400000000,
    0001000000000, 0002000000000, 0004000000000,
    INT64_C(0010000000000), INT64_C(0020000000000), INT64_C(0040000000000),
    INT64_C(0100000000000), INT64_C(0200000000000), INT64_C(0400000000000)
    };

const DEV_CHAR dev_table[] = {
    { "729", 0 },
    { "TAPE", 0 },
    { "7289", DEV_7289 },
    { "DRUM", DEV_7289 },
    { "7631", DEV_7909|DEV_7631 },
    { "FILE", DEV_7909|DEV_7631 },
    { "7750", DEV_7909|DEV_7750 },
    { "COMM", DEV_7909|DEV_7750 },
    { NULL },
    };

const char *sel_name[] = {
    "UNK", "RDS", "WRS", "SNS", "CTL", "FMT", "UNK", "UNK",
    "WEF", "WBT", "BSR", "BSF", "REW", "RUN", "SDN", "UNK"
    };

/* Channel data structures */

UNIT ch_unit[NUM_CHAN] = {
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) },
    { UDATA (&ch6_svc, 0, 0) }
    };

MTAB ch_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "TYPE", NULL,
      NULL, &ch_show_type, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "ENABLED",
      &ch_set_enable, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLED",
      &ch_set_disable, NULL, NULL },
    { 0 }
    };

REG cha_reg[] = {
    { ORDATA (STA, ch_sta[CH_A], 8) },
    { ORDATA (DSC, ch_dso[CH_A], 4) },
    { ORDATA (DSU, ch_dsu[CH_A], 9) },
    { ORDATA (NDSC, ch_ndso[CH_A], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_A], 9) },
    { ORDATA (FLAGS, ch_flags[CH_A], 30) },
    { ORDATA (IDF, ch_idf[CH_A], 2) },
    { ORDATA (OP, ch_op[CH_A], 5) },
    { ORDATA (CLC, ch_clc[CH_A], 16) },
    { ORDATA (WC, ch_wc[CH_A], 15) },
    { ORDATA (CA, ch_ca[CH_A], 16) },
    { ORDATA (AR, ch_ar[CH_A], 36) },
    { ORDATA (CND, ch_cnd[CH_A], 6), REG_HRO },
    { ORDATA (LCC, ch_lcc[CH_A], 6), REG_HRO },
    { ORDATA (SMS, ch_sms[CH_A], 7), REG_HRO },
    { 0 }
    };

REG chb_reg[] = {
    { ORDATA (STATE, ch_sta[CH_B], 8) },
    { ORDATA (DSC, ch_dso[CH_B], 4) },
    { ORDATA (DSU, ch_dsu[CH_B], 9) },
    { ORDATA (NDSC, ch_ndso[CH_B], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_B], 9) },
    { ORDATA (FLAGS, ch_flags[CH_B], 30) },
    { ORDATA (IDF, ch_idf[CH_B], 2) },
    { ORDATA (OP, ch_op[CH_B], 5) },
    { ORDATA (CLC, ch_clc[CH_B], 16) },
    { ORDATA (WC, ch_wc[CH_B], 15) },
    { ORDATA (CA, ch_ca[CH_B], 16) },
    { ORDATA (AR, ch_ar[CH_B], 36) },
    { ORDATA (CND, ch_cnd[CH_B], 6) },
    { ORDATA (LCC, ch_lcc[CH_B], 6) },
    { ORDATA (SMS, ch_sms[CH_B], 7) },
    { 0 }
    };

REG chc_reg[] = {
    { ORDATA (STATE, ch_sta[CH_C], 8) },
    { ORDATA (DSC, ch_dso[CH_C], 4) },
    { ORDATA (DSU, ch_dsu[CH_C], 9) },
    { ORDATA (NDSC, ch_ndso[CH_C], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_C], 9) },
    { ORDATA (FLAGS, ch_flags[CH_C], 30) },
    { ORDATA (IDF, ch_idf[CH_C], 2) },
    { ORDATA (OP, ch_op[CH_C], 5) },
    { ORDATA (CLC, ch_clc[CH_C], 16) },
    { ORDATA (WC, ch_wc[CH_C], 15) },
    { ORDATA (CA, ch_ca[CH_C], 16) },
    { ORDATA (AR, ch_ar[CH_C], 36) },
    { ORDATA (CND, ch_cnd[CH_C], 6) },
    { ORDATA (LCC, ch_lcc[CH_C], 6) },
    { ORDATA (SMS, ch_sms[CH_C], 7) },
    { 0 }
    };

REG chd_reg[] = {
    { ORDATA (STATE, ch_sta[CH_D], 8) },
    { ORDATA (DSC, ch_dso[CH_D], 4) },
    { ORDATA (DSU, ch_dsu[CH_D], 9) },
    { ORDATA (NDSC, ch_ndso[CH_D], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_D], 9) },
    { ORDATA (FLAGS, ch_flags[CH_D], 30) },
    { ORDATA (IDF, ch_idf[CH_D], 2) },
    { ORDATA (OP, ch_op[CH_D], 5) },
    { ORDATA (CLC, ch_clc[CH_D], 16) },
    { ORDATA (WC, ch_wc[CH_D], 15) },
    { ORDATA (CA, ch_ca[CH_D], 16) },
    { ORDATA (AR, ch_ar[CH_D], 36) },
    { ORDATA (CND, ch_cnd[CH_D], 6) },
    { ORDATA (LCC, ch_lcc[CH_D], 6) },
    { ORDATA (SMS, ch_sms[CH_D], 7) },
    { 0 }
    };

REG che_reg[] = {
    { ORDATA (STATE, ch_sta[CH_E], 8) },
    { ORDATA (DSC, ch_dso[CH_E], 4) },
    { ORDATA (DSU, ch_dsu[CH_E], 9) },
    { ORDATA (NDSC, ch_ndso[CH_E], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_E], 9) },
    { ORDATA (FLAGS, ch_flags[CH_E], 30) },
    { ORDATA (IDF, ch_idf[CH_E], 2) },
    { ORDATA (OP, ch_op[CH_E], 5) },
    { ORDATA (CLC, ch_clc[CH_E], 16) },
    { ORDATA (WC, ch_wc[CH_E], 15) },
    { ORDATA (CA, ch_ca[CH_E], 16) },
    { ORDATA (AR, ch_ar[CH_E], 36) },
    { ORDATA (CND, ch_cnd[CH_E], 6) },
    { ORDATA (LCC, ch_lcc[CH_E], 6) },
    { ORDATA (SMS, ch_sms[CH_E], 7) },
    { 0 }
    };

REG chf_reg[] = {
    { ORDATA (STATE, ch_sta[CH_F], 8) },
    { ORDATA (DSC, ch_dso[CH_F], 4) },
    { ORDATA (DSU, ch_dsu[CH_F], 9) },
    { ORDATA (NDSC, ch_ndso[CH_F], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_F], 9) },
    { ORDATA (FLAGS, ch_flags[CH_F], 30) },
    { ORDATA (IDF, ch_idf[CH_F], 2) },
    { ORDATA (OP, ch_op[CH_F], 5) },
    { ORDATA (CLC, ch_clc[CH_F], 16) },
    { ORDATA (WC, ch_wc[CH_F], 15) },
    { ORDATA (CA, ch_ca[CH_F], 16) },
    { ORDATA (AR, ch_ar[CH_F], 36) },
    { ORDATA (CND, ch_cnd[CH_F], 6) },
    { ORDATA (LCC, ch_lcc[CH_F], 6) },
    { ORDATA (SMS, ch_sms[CH_F], 7) },
    { 0 }
    };

REG chg_reg[] = {
    { ORDATA (STATE, ch_sta[CH_G], 8) },
    { ORDATA (DSC, ch_dso[CH_G], 4) },
    { ORDATA (DSU, ch_dsu[CH_G], 9) },
    { ORDATA (NDSC, ch_ndso[CH_G], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_G], 9) },
    { ORDATA (FLAGS, ch_flags[CH_G], 30) },
    { ORDATA (IDF, ch_idf[CH_G], 2) },
    { ORDATA (OP, ch_op[CH_G], 5) },
    { ORDATA (CLC, ch_clc[CH_G], 16) },
    { ORDATA (WC, ch_wc[CH_G], 15) },
    { ORDATA (CA, ch_ca[CH_G], 16) },
    { ORDATA (AR, ch_ar[CH_G], 36) },
    { ORDATA (CND, ch_cnd[CH_G], 6) },
    { ORDATA (LCC, ch_lcc[CH_G], 6) },
    { ORDATA (SMS, ch_sms[CH_G], 7) },
    { 0 }
    };

REG chh_reg[] = {
    { ORDATA (STATE, ch_sta[CH_H], 8) },
    { ORDATA (DSC, ch_dso[CH_H], 4) },
    { ORDATA (DSU, ch_dsu[CH_H], 9) },
    { ORDATA (NDSC, ch_ndso[CH_H], 4) },
    { ORDATA (NDSU, ch_ndsu[CH_H],9) },
    { ORDATA (FLAGS, ch_flags[CH_H], 30) },
    { ORDATA (IDF, ch_idf[CH_H], 2) },
    { ORDATA (OP, ch_op[CH_H], 5) },
    { ORDATA (CLC, ch_clc[CH_H], 16) },
    { ORDATA (WC, ch_wc[CH_H], 15) },
    { ORDATA (CA, ch_ca[CH_H], 16) },
    { ORDATA (AR, ch_ar[CH_H], 36) },
    { ORDATA (CND, ch_cnd[CH_H], 6) },
    { ORDATA (LCC, ch_lcc[CH_H], 6) },
    { ORDATA (SMS, ch_sms[CH_H], 7) },
    { 0 }
    };

DEVICE ch_dev[NUM_CHAN] = {
    {
    "CHANA", &ch_unit[CH_A], cha_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, 0
    },
    {
    "CHANB", &ch_unit[CH_B], chb_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHANC", &ch_unit[CH_C], chc_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHAND", &ch_unit[CH_D], chd_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHANE", &ch_unit[CH_E], che_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHANF", &ch_unit[CH_F], chf_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHANG", &ch_unit[CH_G], chg_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    },
    {
    "CHANH", &ch_unit[CH_H], chh_reg, ch_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &ch_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    }
    };

/* 7607 channel overview

   Channel variables:

        ch_sta           channel state
        ch_dso, ch_dsu   operation and unit for current data select
        ch_ndso, ch_ndsu operation and unit for current non-data select
        ch_clc           current location counter
        ch_ca            memory addres
        ch_wc            word count
        ch_op            channel opcode (bits <S,1:2,19>)
        ch_flags         channel flags

   States of a channel

   IDLE - channel is not in operation

        RDS, WDS:     -> DSW if device is idle, schedule device
                         device timeout drives next transition
                      -> stall if device is busy
                         repeat until device is idle
        other I/O:    -> NDS if device is idle, schedule device
                         device timeout drives next transition
                      -> stall if device is busy
                         repeat until device is idle
        chan reset:   -> IDLE   

   PDS (PNDS) - channel is polling device to start data (non-data) select

        chan timeout: -> DSW (NDS) if device is idle
                         device timeout drives next transition
                      -> no change if device is busy, schedule channel
        chan reset:   -> IDLE   
        
   DSW - channel is waiting for channel start command
   
        dev timeout:  -> IDLE if no stacked non-data select
                      -> PNDS if stacked non-data select
                         channel timeout drives next transition
        start chan:   -> DSX if chan program transfers data
                         device timeout drives next transition
                      -> IDLE if channel disconnects, no stacked NDS
                      -> PNDS if channel disconnects, stacked NDS
                         channel timeout drives next transition
        chan reset:   -> IDLE   
                     
   DSX - channel is executing data select
   
        dev timeout:  -> DSX if transfer not complete, reschedule device
                         device timeout drives next transition
                      -> DSW if channel command completes, CHF_LDW set
                      -> IDLE if transfer complete, no stacked NDS, or
                         if channel command completes, CHF_LDW clear
                      -> PNDS if channel disconnects, stacked NDS
                         channel timeout drives next transition
        start chan:   -> DSX with CHF_LDW, CPU stall
        chan reset:   -> IDLE   

   NDS - channel is executing non-data select
   
        dev timeout:  -> IDLE if transfer complete, no stacked DS
                      -> PDS if channel disconnects, stacked DS
                         channel timeout drives next transition
        chan reset:   -> IDLE

   The channel has two interfaces to a device. The select routine:

        dev_select (uint32 ch, uint32 sel, uint32 unit)

   Returns can include device errors and ERR_STALL.  If ERR_STALL, the
   device is busy.  For I/O instructions, ERR_STALL stalls execution of
   the instruction until the device is not busy.  For stacked command
   polls, ERR_STALL causes the poll to be repeated after a delay.

   The device write routine is used to place output data in the device
   write buffer.

   Channel transfers are driven by the channel.  When a device needs to
   read or write data, it sets a channel request in ch_req.  The channel
   process transfers the data and updates channel control parameters
   accordingly.  Note that the channel may disconnect; in this case, the
   transfer completes 'correctly' from the point of view of the device.

   The channel transfer commands (IOxT) require the channel to 'hold'
   a new channel command in anticipation of the current transfer.  If
   the channel is currently executing (CH6S_DSX) and a channel start
   is issued by the CPU, a 'start pending' flag is set and the CPU is
   stalled.  When the channel reaches the end of an IOxT command, it
   checks the 'start pending' flag.  If the flag is set, the channel
   sets itself to waiting and then requeues itself for one cycle later.
   The CPU tries the channel start, sees that the channel is waiting,
   and issues the new channel command.

   state        op              device                  channel

   IDLE         RDS,WDS         start I/O               ->DSW

   DSW          LCHx            (timed wait)            ->DSX

   DSX          --              timeout, req svc
                                (timed wait)            transfer word
                                timeout, req svc
                                (timed wait)
                LCHx, stalls            :
                                timeout, EOR/EOC        IOxT: ->DSW, resched
   DSW          LCHx            (timed wait)            ->DSX, etc              

   7909 channel overview

   Channel variables:

        ch_sta          channel state
        ch_clc          current location counter
        ch_ca           memory addres
        ch_wc           word count
        ch_op           channel opcode (bits <S,1:3,19>)
        ch_sms          status mask
        ch_cond         interrupt conditions
        ch_lcc          control counter
        ch_flags        channel flags

   States of a channel

   IDLE - channel is not in operation

        RDCx, SDCx, interrupt -> DSX

   DSX - channel is executing data select

        TWT, WTR -> IDLE

   The 7909 is more capable than the 7607 but also simpler in some ways.
   It has many more instructions, built in counters and status checking,
   and interrupts.  But it has only two states and no concept of records.

   The 7909 read process is driven by the device:

        channel CTLR/SNS: send select
        device: schedule timeout
        device timeout: device to AR, request channel
            channel: AR to memory
        device timeout: device to AR, request channel
            channel: AR to memory
        :
        device timeout: set end, request channel
            channel: disconnect on CPYD, send STOP

   The 7909 write process is also driven by the device:

        channel CTL/CTLW: send select
        device: schedule timeout, request channel
        channel: memory to output buffer
            device timeout: output buffer to device, request channel
        channel: memory to output buffer
            device timeout: output buffer to device, request channel
        :
        channel: memory to output buffer
            device timeout: output buffer to device, set end, request channel
        channel: disconnect on CPYD, send STOP

    For both reads and writes, devices must implement an 'interblock' or
    'interrecord' state that is long enough for the channel to see the 
    end, disconnect, and send a stop signal.
*/

/* Data select - called by RDS or WDS instructions - 7607/7289 only

   - Channel is from address and has been corrected
   - Channel must be an enabled 7607
   - If data select already in use, stall CPU
   - If non-data select is a write end-of-file, stall CPU
   - If channel is busy, stack command
   - Otherwise, start IO, set channel to waiting */

t_stat ch_op_ds (uint32 ch, uint32 ds, uint32 unit)
{
t_stat r;

if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_DIS)                         /* disabled? stop */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_7909)                        /* 7909? stop */
    return STOP_7909;
if (ch_dso[ch])                                         /* DS in use? */
    return ERR_STALL;
if (ch_ndso[ch] == CHSL_WEF)                            /* NDS = WEF? */
    return ERR_STALL;
if (ch_sta[ch] == CHXS_IDLE) {                          /* chan idle? */
    r = ch6_sel (ch, ds, unit, CH6S_DSW);               /* select device */
    if (r != SCPE_OK)
        return r;
    }
ch_dso[ch] = ds;                                        /* set command, unit */
ch_dsu[ch] = unit;
ch_flags[ch] &= ~(CHF_LDW|CHF_EOR|CHF_CMD);             /* clear flags */
ch_idf[ch] = 0;
return SCPE_OK;
}

/* Non-data select - called by BSR, BSF, WEF, REW, RUN, SDS instructions - 7607 only

   - Channel is from address and has been corrected
   - Channel must be an enabled 7607
   - If non-data select already in use, stall CPU
   - If data select is card or printer, stall CPU
   - If channel is busy, stack command
   - Otherwise, start IO, set channel to waiting */

t_stat ch_op_nds (uint32 ch, uint32 nds, uint32 unit)
{
DEVICE *dptr;
t_stat r;

if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_DIS)                         /* disabled? stop */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_7909)                        /* 7909? stop */
    return STOP_7909;
if (ch_ndso[ch])                                        /* NDS in use? */
    return ERR_STALL;
if (ch_dso[ch] && (dptr = ch_find_dev (ch, ch_dsu[ch])) /* DS, cd or lpt? */
    && (dptr->flags & DEV_CDLP))
    return ERR_STALL;
if (ch_sta[ch] == CHXS_IDLE) {                          /* chan idle? */
    r = ch6_sel (ch, nds, unit, CH6S_NDS);              /* select device */
    if (r != SCPE_OK)
        return r;
    }
ch_ndso[ch] = nds;                                      /* set command, unit */
ch_ndsu[ch] = unit;
return SCPE_OK;
}

/* End of data select - called from channel - 7607/7289 only

   - If executing, set command trap flag
   - Set channel idle
   - If stacked nds, set up immediate channel timeout */

t_stat ch6_end_ds (uint32 ch)
{
if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return STOP_NXCHN;
ch_dso[ch] = ch_dsu[ch] = 0;                            /* no data select */
if (ch_ndso[ch]) {                                      /* stacked non-data sel? */
    sim_activate (ch_dev[ch].units, 0);                 /* immediate poll */
    ch_sta[ch] = CH6S_PNDS;                             /* state = polling */
    }
else ch_sta[ch] = CHXS_IDLE;                            /* else state = idle */
return SCPE_OK;
}

/* End of non-data select - called from I/O device completion - 7607/7289 only

   - Set channel idle
   - If stacked ds, set up immediate channel timeout */

t_stat ch6_end_nds (uint32 ch)
{
if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return STOP_NXCHN;
ch_ndso[ch] = ch_ndsu[ch] = 0;                          /* no non-data select */
if (ch_dso[ch]) {                                       /* stacked data sel? */
    sim_activate (ch_dev[ch].units, 0);                 /* immediate poll */
    ch_sta[ch] = CH6S_PDS;                              /* state = polling */
    }
else ch_sta[ch] = CHXS_IDLE;                            /* else state = idle */
return SCPE_OK;
}

/* Send select to device - 7607/7289 only */

t_stat ch6_sel (uint32 ch, uint32 sel, uint32 unit, uint32 sta)
{
DEVICE *dptr;
DIB *dibp;
t_stat r;

if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return STOP_NXCHN;
dptr = ch_find_dev (ch, unit);                          /* find device */
if (dptr == NULL)                                       /* invalid device? */
    return STOP_NXDEV;
dibp = (DIB *) dptr->ctxt;
r = dibp->chsel (ch, sel, unit);                        /* select device */
if (r == SCPE_OK)                                       /* set status */
    ch_sta[ch] = sta;
return r;
}

/* Channel unit service - called to start stacked command - 7607 only */

t_stat ch6_svc (UNIT *uptr)
{
uint32 ch = uptr - &ch_unit[0];                         /* get channel */
t_stat r;

if (ch >= NUM_CHAN)                                     /* invalid chan? */
    return SCPE_IERR;
switch (ch_sta[ch]) {                                   /* case on state */

    case CH6S_PDS:                                      /* polling for ds */
        r = ch6_sel (ch, ch_dso[ch], ch_dsu[ch], CH6S_DSW);
        break;

    case CH6S_PNDS:                                     /* polling for nds */
        r = ch6_sel (ch, ch_ndso[ch], ch_ndsu[ch], CH6S_NDS);
        break;

    default:
        return SCPE_OK;
        }

if (r == ERR_STALL) {                                   /* stalled? */
    sim_activate (uptr, ch_tpoll);                      /* continue poll */
    return SCPE_OK;
    }
return r;
}

/* Map channel and unit number to device - all channels */

DEVICE *ch_find_dev (uint32 ch, uint32 unit)
{
if (ch >= NUM_CHAN)                                     /* invalid arg? */
    return NULL;
if (ch_dev[ch].flags & (DEV_7909|DEV_7289))
    return ch2dev[ch];
unit = unit & 0777;
if (((unit >= U_MTBCD) && (unit <= (U_MTBCD + MT_NUMDR))) ||
    ((unit >= U_MTBIN) && (unit <= (U_MTBIN + MT_NUMDR))))
    return ch2dev[ch];
if (ch != 0)
    return NULL;
if (unit == U_CDR)
    return &cdr_dev;
if (unit == U_CDP)
    return &cdp_dev;
if ((unit == U_LPBCD) || (unit == U_LPBIN))
    return &lpt_dev;
return NULL;
}

/* Start channel - channel is from opcode

   7607: channel should have a data select operation pending (DSW state)
   7909: channel should be idle (IDLE state) */

t_stat ch_op_start (uint32 ch, uint32 clc, t_bool reset)
{
t_uint64 ir;
t_stat r;

clc = clc | data_base;                                  /* add A/B select */
if (ch >= NUM_CHAN)                                     /* invalid argument? */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_DIS)                         /* disabled? stop */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_7909) {                      /* 7909? */
    if (ch_sta[ch] != CHXS_IDLE)                        /* must be idle */
            return ERR_STALL;
    if (reset) {                                        /* RDCx? */
        ch_cnd[ch] = 0;                                 /* clear conditions */
        ch_clc[ch] = clc;                               /* set clc */
        }
    else {                                              /* SDCx */
        if (BIT_TST (chtr_enab, CHTR_V_TWT + ch) &&     /* pending trap? */
            (ch_flags[ch] & CHF_TWT))
            return ERR_STALL;
        ch_clc[ch] = ch_ca[ch] & CHAMASK;               /* finish WTR, TWT */
        }
    ch_flags[ch] &= ~CHF_CLR_7909;                      /* clear flags, not IP */
    ch_idf[ch] = 0;
    ch_sta[ch] = CHXS_DSX;                              /* set state */
    return ch9_new_cmd (ch);                            /* start executing */
    }
                                                        /* 7607, 7289 */
if (reset) {                                            /* reset? */
    if (ch_sta[ch] == CHXS_DSX)
        ch_sta[ch] = CH6S_DSW;
    ch_flags[ch] &= ~(CHF_LDW|CHF_EOR|CHF_TRC|CHF_CMD);
    ch_idf[ch] = 0;
    }

switch (ch_sta[ch]) {                                   /* case on chan state */

    case CHXS_IDLE:                                     /* idle */
        ind_ioc = 1;                                    /* IO check */
        ir = ReadP (clc);                               /* get chan word */
        ch_clc[ch] = CHAINC (clc);                      /* incr chan pc */
        ch_wc[ch] = GET_DEC (ir);                       /* get word cnt */
        ch_ca[ch] = ((uint32) ir) & CHAMASK;            /* get address */
        ch_op[ch] = (GET_OPD (ir) << 1) |               /* get opcode */
            ((((uint32) ir) & CH6I_NST)? 1: 0);         /* plus 'no store' */
        break;

    case CH6S_PNDS:                                     /* NDS polling */
    case CH6S_PDS:                                      /* DS polling */
    case CH6S_NDS:                                      /* NDS executing */
        return ERR_STALL;                               /* wait it out */

    case CH6S_DSW:                                      /* expecting command */
        ch_sta[ch] = CHXS_DSX;                          /* update state */
        if (ch_dev[ch].flags & DEV_7289) {              /* drum channel? */
            ir = ReadP (clc);                           /* read addr */
            ch_clc[ch] = CHAINC (clc);                  /* incr chan pc */
            if ((r = ch9_wr (ch, ir, 0)))               /* write to dev */
                return r;
            }
        else ch_clc[ch] = clc;                          /* set clc */
        return ch6_new_cmd (ch, TRUE);                  /* start channel */

    case CHXS_DSX:                                      /* executing */
        ch_flags[ch] = ch_flags[ch] | CHF_LDW;          /* flag pending LCH */
        return ERR_STALL;                               /* stall */
        }

return SCPE_OK;
}

/* Store channel 

   7607/7289 stores op,ca,nostore,clc
   7909 stores clc,,ca */

t_stat ch_op_store (uint32 ch, t_uint64 *dat)
{
if ((ch >= NUM_CHAN) || (ch_dev[ch].flags & DEV_DIS))
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_7909)
    *dat = (((t_uint64) ch_ca[ch] & CHAMASK) << INST_V_DEC) |
        (((t_uint64) ch_clc[ch] & CHAMASK) << INST_V_ADDR);
else *dat = (((t_uint64) ch_clc[ch] & CHAMASK) << INST_V_DEC) |
    (((t_uint64) ch_ca[ch] & CHAMASK) << INST_V_ADDR) |
    (((t_uint64) (ch_op[ch] & 1)) << 16) |
    (((t_uint64) (ch_op[ch] & 016)) << 32);
return SCPE_OK;
}

/* Store channel diagnostic 

   7607 is undefined
   7289 stores IOC+???
   7909 stores 7909 lcc+flags */

t_stat ch_op_store_diag (uint32 ch, t_uint64 *dat)
{
extern t_uint64 drm_sdc (uint32 ch);

if ((ch >= NUM_CHAN) || (ch_dev[ch].flags & DEV_DIS))
    return STOP_NXCHN;
if (ch_flags[ch] & DEV_7289)
    *dat = drm_sdc (ch);
else if (ch_flags[ch] & DEV_7909)
    *dat = (((t_uint64) (ch_lcc[ch] & CHF_M_LCC)) << CHF_V_LCC) | 
        (ch_flags[ch] & CHF_SDC_7909);
else *dat = 0;
return SCPE_OK;
}

/* Reset data channel 

   7607 responds to RDC
   7909 responds to RIC */

t_stat ch_op_reset (uint32 ch, t_bool ch7909)
{
DEVICE *dptr;

if (ch >= NUM_CHAN)                                     /* invalid argument? */
    return STOP_NXCHN;
if (ch_dev[ch].flags & DEV_DIS)                         /* disabled? ok */
    return SCPE_OK;
if (ch_dev[ch].flags & DEV_7909) {                      /* 7909? */
    if (!ch7909)                                        /* wrong reset is NOP */
        return SCPE_OK;
    dptr = ch2dev[ch];                                  /* get device */
    }
else {                                                  /* 7607, 7289 */
    if (ch7909)                                         /* wrong reset is err */
        return STOP_NT7909;
    dptr = ch_find_dev (ch, ch_ndsu[ch]);               /* find device */
    }
ch_reset (&ch_dev[ch]);                                 /* reset channel */
if (dptr && dptr->reset)                                /* reset device */
    dptr->reset (dptr);
return SCPE_OK;
}

/* Channel process - called from main CPU loop.  If the channel is unable
   to get a valid command, it will reschedule itself for the next cycle.

   The read process is basically synchronous with the device timeout routine.
   The device requests the channel and supplies the word to be stored in memory.
   In the next time slot, the channel stores the word in memory. */

t_stat ch_proc (uint32 ch)
{
t_stat r;

if (ch >= NUM_CHAN)                                     /* bad channel? */
    return SCPE_IERR;
ch_req &= ~REQ_CH (ch);                                 /* clear request */
if (ch_dev[ch].flags & DEV_DIS)                         /* disabled? */
    return SCPE_IERR;
if (ch_dev[ch].flags & DEV_7909) {                      /* 7909 */

    t_uint64 sr;
    uint32 csel, sc, tval, mask, ta;
    t_bool xfr;

    if (ch_flags[ch] & CHF_IRQ) {                       /* interrupt? */
        ta = CHINT_CHA_SAV + (ch << 1);                 /* save location */
        if (ch_sta[ch] == CHXS_IDLE)                    /* waiting? */
            sr = (((t_uint64) ch_ca[ch] & CHAMASK) << INST_V_DEC) |
                ((t_uint64) ch_clc[ch] & CHAMASK);      /* save CLC */
        else sr = (((t_uint64) ch_ca[ch] & CHAMASK) << INST_V_DEC) |
            ((t_uint64) CHAINC (ch_clc[ch]));           /* no, save CLC+1 */
        ch_sta[ch] = CHXS_DSX;                          /* set running */
        ch_flags[ch] = (ch_flags[ch] | CHF_INT) &       /* set intr state */
            ~(CHF_IRQ|CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS); /* clr flags */
        WriteP (ta, sr);                                /* write ca,,clc */
        sr = ReadP (ta + 1);                            /* get chan cmd */
        return ch9_exec_cmd (ch, sr);                   /* exec cmd */
        }

    switch (ch_op[ch] & CH9_OPMASK) {                   /* switch on op */

    case CH9_TWT:                                       /* transfer of TWT */
    case CH9_WTR:                                       /* transfer of WTR */
    case CH9_TCH:                                       /* transfer */
        ch_clc[ch] = ch_ca[ch] & CHAMASK;               /* change CLC */
        break;

    case CH9_TDC:                                       /* decr & transfer */
        if (ch_lcc[ch] != 0) {                          /* counter != 0? */
            ch_lcc[ch]--;                               /* decr counter */
            ch_clc[ch] = ch_ca[ch] & CHAMASK;           /* change CLC */
            }
        break;

    case CH9_TCM:                                       /* transfer on cond */
        csel = CH9D_COND (ch_wc[ch]);
        mask = CH9D_MASK (ch_wc[ch]);
        if (csel == 7)                                  /* C = 7? mask mbz */
            xfr = (mask == 0);
        else {                                          /* C = 0..6 */
            if (csel == 0)                              /* C = 0? test cond */
                tval = ch_cnd[ch];
            else tval = (uint32) (ch_ar[ch] >> (6 * (6 - csel))) & 077;
            if (ch_wc[ch] & CH9D_B11)
                xfr = ((tval & mask) == mask);
            else xfr = (tval == mask);
            }
        if (xfr)                                         /* change CLC */
            ch_clc[ch] = ch_ca[ch] & CHAMASK;
        break;

    case CH9_LIP:                                       /* leave interrupt */
        ta = CHINT_CHA_SAV + (ch << 1);                 /* save location */
        ch_flags[ch] &= ~(CHF_INT|CHF_IRQ);             /* clear intr */
        ch_cnd[ch] = 0;                                 /* clear channel cond */
        ch_clc[ch] = (uint32) ReadP (ta) & CHAMASK;
        break;

    case CH9_LIPT:                                      /* leave intr, transfer */
        ch_flags[ch] &= ~(CHF_INT|CHF_IRQ);             /* clear intr */
        ch_cnd[ch] = 0;                                 /* clear channel cond */
        ch_clc[ch] = ch_ca[ch] & CHAMASK;               /* change CLC */
        break;

    case CH9_LAR:                                       /* load assembly reg */
        ch_ar[ch] = ReadP (ch_ca[ch]);
        break;

    case CH9_SAR:                                       /* store assembly reg */
        WriteP (ch_ca[ch], ch_ar[ch]);
        break;

    case CH9_SMS:                                       /* load SMS reg */
        ch_sms[ch] = CH9A_SMS (ch_ca[ch]);              /* from eff addr */
        if (!(ch_sms[ch] & CHSMS_IATN1) &&              /* atn inhbit off */
            (ch_flags[ch] & CHF_ATN1))                  /* and atn pending? */
            ch9_eval_int (ch, 0);                       /* force int eval */
        break;

    case CH9_LCC:                                       /* load control cntr */
        ch_lcc[ch] = CH9A_LCC (ch_ca[ch]);              /* from eff addr */
        break;

    case CH9_ICC:                                       /* insert control cntr */
    case CH9_ICCA:
        csel = CH9D_COND (ch_wc[ch]);                   /* get C */
        if (csel == 0) ch_ar[ch] =                      /* C = 0? read SMS */
            (ch_ar[ch] & INT64_C(0777777770000)) | ((t_uint64) ch_sms[ch]);
        else if (csel < 7) {                            /* else read cond cntr */
            sc = 6 * (6 - csel);
            ch_ar[ch] = (ch_ar[ch] & ~(((t_uint64) 077) << sc)) |
                (((t_uint64) ch_lcc[ch]) << sc);
            }
        break;

    case CH9_XMT:                                       /* transmit */
        if (ch_wc[ch] == 0)
            break;
        sr = ReadP (ch_clc[ch]);                        /* next word */
        WriteP (ch_ca[ch], sr);
        ch_clc[ch] = CHAINC (ch_clc[ch]);               /* incr pointers */
        ch_ca[ch] = CHAINC (ch_ca[ch]);
        ch_wc[ch] = ch_wc[ch] - 1;                      /* decr count */
        ch_req |= REQ_CH (ch);                          /* go again */
        return SCPE_OK;

    case CH9_SNS:                                       /* sense */
        if ((r = ch9_sel (ch, CHSL_SNS)))               /* send sense to dev */
            return r;
        ch_flags[ch] |= CHF_PRD;                        /* prepare to read */
        break;                                          /* next command */

    case CH9_CTL:
    case CH9_CTLR:
    case CH9_CTLW:                                      /* control */
        if (((ch_wc[ch] & CH9D_NST) == 0) &&            /* N = 0 and */
            !(ch_flags[ch] & CHF_EOR)) {                /* end not set? */
            sr = ReadP (ch_ca[ch]);
            ch_ca[ch] = CHAINC (ch_ca[ch]);             /* incr ca */
            return ch9_wr (ch, sr, 0);                  /* write ctrl wd */
            }
        ch_flags[ch] &= ~CHF_EOR;                       /* clear end */
        if (ch_op[ch] == CH9_CTLR) {                    /* CTLR? */
            if ((r = ch9_sel (ch, CHSL_RDS)))           /* send read sel */
                return r;
            ch_flags[ch] |= CHF_PRD;                    /* prep to read */
            ch_idf[ch] = 0;
            }
        else if (ch_op[ch] == CH9_CTLW) {               /* CTLW? */
            if ((r = ch9_sel (ch, CHSL_WRS)))           /* end write sel */
                return r;
            ch_flags[ch] |= CHF_PWR;                    /* prep to write */
            }
        break;

    case CH9_CPYD:                                      /* copy & disc */
        if ((ch_wc[ch] == 0) || (ch_flags[ch] & CHF_EOR)) { /* wc == 0 or EOR? */
            if (ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS)) {
                ch_flags[ch] &= ~(CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS);
                if ((r = ch9_wr (ch, 0, CH9DF_STOP)))   /* send stop */
                    return r;
                }
            if (ch_flags[ch] & CHF_EOR) {               /* EOR? */
                ch_flags[ch] &= ~CHF_EOR;               /* clear flag */
                break;                                  /* new command */
                }
            return SCPE_OK;                             /* wait for end */
            }   
        if (ch_flags[ch] & CHF_RDS)                     /* read? */
            return ch9_rd_putw (ch);
        return ch9_wr_getw (ch);                        /* no, write */

    case CH9_CPYP:                                      /* anything to do? */
        if (ch_wc[ch] == 0)                             /* (new, wc = 0) next */
            break;
        if (ch_flags[ch] & CHF_EOR)                     /* end? */
            ch_flags[ch] &= ~CHF_EOR;                   /* ignore */
        else if (ch_flags[ch] & CHF_RDS)                /* read? */
            ch9_rd_putw (ch);
        else if ((r = ch9_wr_getw (ch)))                /* no, write */
            return r;
        if (ch_wc[ch] == 0)                             /* done? get next */
            break;
        return SCPE_OK;                                 /* more to do */
            
    default:
        return STOP_ILLIOP;
        }

    return ch9_new_cmd (ch);                            /* next command */
    }

else if (ch_flags[ch] & CHF_RDS) {                      /* 7607 read? */

    if (ch_sta[ch] != CHXS_DSX)                         /* chan exec? no, disc */
        return ch6_end_ds (ch);
    switch (ch_op[ch] & CH6_OPMASK) {                   /* switch on op */

    case CH6_TCH:                                       /* transfer */
        ch_clc[ch] = ch_ca[ch] & CHAMASK;               /* change clc */
        return ch6_new_cmd (ch, FALSE);                 /* unpack new cmd */

    case CH6_IOCD:                                      /* IOCD */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if (ch6_rd_putw (ch))                       /* store; more? cont */
                return SCPE_OK;
            }
        return ch6_end_ds (ch);                         /* no, disconnect */

    case CH6_IOCP:                                      /* IOCP */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if (ch6_rd_putw (ch))                       /* store; more? cont */
                return SCPE_OK;
            }
        return ch6_new_cmd (ch, FALSE);                 /* unpack new cmd */

    case CH6_IOCT:                                      /* IOCT */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if (ch6_rd_putw (ch))                       /* store; more? cont */
                return SCPE_OK;
            }
        return ch6_ioxt (ch);                           /* unstall or disc */

    case CH6_IOSP:                                      /* IOSP */
        if (ch_flags[ch] & CHF_EOR) {                   /* (new) EOR set? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_new_cmd (ch, FALSE);             /* get next cmd */
            }
        if (ch_wc[ch]) {                                /* wc > 0? */
            if (ch6_rd_putw (ch) && !(ch_flags[ch] & CHF_EOR))
                 return SCPE_OK;                        /* yes, store; more? */
            ch6_iosp_cclr (ch);                         /* cond clear eor */
            }
        return ch6_new_cmd (ch, FALSE);                 /* next cmd */

    case CH6_IOST:                                      /* IOST */
         if (ch_flags[ch] & CHF_EOR) {                   /* (new) EOR set? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_ioxt (ch);                       /* get next cmd */
            }
       if (ch_wc[ch]) {                                /* wc > 0? */
            if (ch6_rd_putw (ch) && !(ch_flags[ch] & CHF_EOR))
                 return SCPE_OK;                        /* yes, store; more? */
            ch6_iosp_cclr (ch);                         /* cond clear eor */
            }
        return ch6_ioxt (ch);                           /* unstall or disc */

    case CH6_IORP:                                      /* IORP */
        if (ch_flags[ch] & CHF_EOR) {                   /* (new) EOR set? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_new_cmd (ch, FALSE);             /* get next cmd */
            }
        ch6_rd_putw (ch);                               /* store wd; ignore wc */ 
        if (ch_flags[ch] & CHF_EOR) {                   /* EOR? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_new_cmd (ch, FALSE);             /* get next cmd */
            }
        return SCPE_OK;                                 /* done */
        
    case CH6_IORT:                                      /* IORT */
        if (ch_flags[ch] & CHF_EOR) {                   /* (new) EOR set? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_ioxt (ch);                       /* get next cmd */
            }
        ch6_rd_putw (ch);                               /* store wd; ignore wc */ 
        if (ch_flags[ch] & CHF_EOR) {                   /* EOR? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;     /* clear flag */
            return ch6_ioxt (ch);                       /* unstall or disc */
            }
        return SCPE_OK;                                 /* done */

    default:
        return SCPE_IERR;
        }                                               /* end case */
    }                                                   /* end if read */

else {                                                  /* 7607 write */

    if (ch_sta[ch] != CHXS_DSX)                         /* chan exec? no, disc */
        return ch6_end_ds (ch);
    switch (ch_op[ch] & CH6_OPMASK) {                   /* switch on op */

    case CH6_TCH:                                       /* transfer */
        ch_clc[ch] = ch_ca[ch] & CHAMASK;               /* change clc */
        return ch6_new_cmd (ch, FALSE);                 /* unpack new cmd */

    case CH6_IOCD:                                      /* IOCD */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if ((r = ch6_wr_getw (ch, TRUE)))           /* send wd to dev; err? */
                return r;
            if (ch_wc[ch])                              /* more to do? */
                return SCPE_OK;
            }
        return ch6_end_ds (ch);                         /* disconnect */

    case CH6_IOCP:                                      /* IOCP */
    case CH6_IOSP:                                      /* IOSP */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if ((r = ch6_wr_getw (ch, FALSE)))          /* send wd to dev; err? */
                return r;
            if (ch_wc[ch])                              /* more to do? */
                return SCPE_OK;
            }
        return ch6_new_cmd (ch, FALSE);                 /* get next cmd */

    case CH6_IOCT:                                      /* IOCT */
    case CH6_IOST:                                      /* IOST */
        if (ch_wc[ch]) {                                /* wc > 0? */
            if ((r = ch6_wr_getw (ch, FALSE)))          /* send wd to dev; err? */
                return r;
            if (ch_wc[ch])                              /* more to do? */
                return SCPE_OK;
            }
        return ch6_ioxt (ch);                           /* get next cmd */

    case CH6_IORP:                                      /* IORP */
        if (!(ch_flags[ch] & CHF_EOR) && ch_wc[ch]) {   /* not EOR? (cdp, lpt) */
            if ((r = ch6_wr_getw (ch, TRUE)))           /* send wd to dev; err? */
                return r;
            if (ch_wc[ch])                              /* more to do? */
                return SCPE_OK;
            }
        ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;         /* clear EOR */
        return ch6_new_cmd (ch, FALSE);                 /* get next cmd */

    case CH6_IORT:                                      /* IORT */
        if (!(ch_flags[ch] & CHF_EOR) && ch_wc[ch]) {   /* not EOR? (cdp, lpt) */
            if ((r = ch6_wr_getw (ch, TRUE)))           /* send wd to dev; err? */
                return r;
            if (ch_wc[ch])                              /* more to do? */
                return SCPE_OK;
            }
        ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;         /* clear EOR */
        return ch6_ioxt (ch);                           /* unstall or disc */

    default:
        return SCPE_IERR;
        }                                               /* end switch */
    }                                                   /* end else write */
}

/* 7607 channel support routines */

/* 7607 channel input routine - put one word to memory */

t_bool ch6_rd_putw (uint32 ch)
{
if (ch_idf[ch] & CH6DF_EOR)                             /* eor from dev? */
    ch_flags[ch] |= CHF_EOR;
else ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;            /* set/clr chan eor */
ch_idf[ch] = 0;                                         /* clear eor, valid */
if (ch_wc[ch]) {                                        /* wc > 0? */
    if ((ch_op[ch] & 1) == 0) {                         /* do store? */
        WriteP (ch_ca[ch], ch_ar[ch]);
        ch_ca[ch] = CHAINC (ch_ca[ch]);                 /* incr ca */
        }
    ch_wc[ch] = ch_wc[ch] - 1;
    }
return (ch_wc[ch]? TRUE: FALSE);
}

/* 7607 channel output routine - get one word from memory */

t_stat ch6_wr_getw (uint32 ch, t_bool eorz)
{
DEVICE *dptr;
DIB *dibp;
uint32 eorfl;

ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;                 /* clr eor */
if (ch_wc[ch]) {
    ch_ar[ch] = ReadP (ch_ca[ch]);                      /* get word */
    ch_ca[ch] = CHAINC (ch_ca[ch]);                     /* incr ca */
    ch_wc[ch] = ch_wc[ch] - 1;
    }
else ch_ar[ch] = 0;
if (eorz && (ch_wc[ch] == 0))                           /* eor on wc = 0? */
    eorfl = 1;
else eorfl = 0;
dptr = ch_find_dev (ch, ch_dsu[ch]);                    /* find device */
if (dptr &&                                             /* valid device? */
    (dibp = (DIB *) dptr->ctxt) &&                      /* with DIB? */
    dibp->write)                                        /* and write routine? */
    return dibp->write (ch, ch_ar[ch], eorfl);
return SCPE_IERR;                                       /* huh? */
}

/* 7607 channel new command - on channel load, check for disconnects

   The protocol for new commands is as follows:
   - If IOCD 0,,0, disconnect immediately
   - If IOCT 0,,0 or IOST 0,,0 and loaded by RCHA, disconnect immediately
   - If an effective NOP (TCH, IOCx 0,,0, IOSx 0,,0), force a channel
     cycle to retire the channel comand as quickly as possible.
   - If an IORx and EOR is set, force a channel cycle to retire the
     channel command as quickly as possible.
*/

t_stat ch6_new_cmd (uint32 ch, t_bool ch_ld)
{
t_uint64 ir;
uint32 op, t;

ir = ReadP (t = ch_clc[ch]);                            /* read cmd */
ch_wc[ch] = GET_DEC (ir);                               /* get word cnt */
ch_ca[ch] = ((uint32) ir) & CHAMASK;                    /* get address */
op = GET_OPD (ir) << 1;                                 /* get opcode */
ch_op[ch] = op | ((((uint32) ir) & CH6I_NST)? 1: 0);    /* plus 'no store' */
if ((ir & CHI_IND) && (ch_wc[ch] ||                     /* indirect? */
    ((op != CH6_IOCP) && (op != CH6_IOSP)))) {          /* wc >0, or !IOxP? */
    t_uint64 sr = ReadP (ch_ca[ch] & AMASK);            /* read indirect */
    ch_ca[ch] = ((uint32) sr) & ((cpu_model & I_CT)? PAMASK: AMASK);
    }
if (hst_ch)
    cpu_ent_hist (ch_clc[ch] | ((ch + 1) << HIST_V_CH), ch_ca[ch], ir, 0);
ch_clc[ch] = (ch_clc[ch] + 1) & AMASK;                  /* incr chan pc */

switch (op) {                                           /* case on opcode */

    case CH6_IOCD:                                      /* IOCD */
        if (ch_wc[ch] == 0)                             /* wc 0? end now */
            ch6_end_ds (ch);
        break;

    case CH6_IOST:                                      /* IOST */
        if (ch_flags[ch] & CHF_EOR)                     /* EOR set? immed ch req */
            ch_req |= REQ_CH (ch);
    case CH6_IOCT:                                      /* IOCT */
        if (ch_wc[ch] == 0) {                           /* wc 0? */
            if (ch_ld)                                  /* load? end now */
                ch6_end_ds (ch);
            else ch_req |= REQ_CH (ch);                 /* else immed ch req */
            }
        break;

    case CH6_IOSP:                                      /* IOSP */
        if (ch_flags[ch] & CHF_EOR)                     /* EOR set? immed ch req */
            ch_req |= REQ_CH (ch);
    case CH6_IOCP:                                      /* IOCP */
        if (ch_wc[ch] == 0)                             /* wc 0? immed ch req */
            ch_req |= REQ_CH (ch);
        break;

    case CH6_IORT:                                      /* IORT */
    case CH6_IORP:                                      /* IORP */
        if (ch_flags[ch] & CHF_EOR)                     /* EOR set? immed ch req */
            ch_req |= REQ_CH (ch);
        break;

    case CH6_TCH:                                       /* TCH */
        ch_req |= REQ_CH (ch);                          /* immed ch req */
        break;

    default:                                            /* all others */
        break;
    }                                                   /* end case */

if (sim_brk_summ && sim_brk_test (t, SWMASK ('E')))
    return ch_bkpt (ch, t);
return SCPE_OK;
}

/* 7607 channel IOxT: if LCH stall, set state back to DSW; else disconnect and trap */

t_stat ch6_ioxt (uint32 ch)
{
if (ch_flags[ch] & CHF_LDW) {                           /* LCH cmd pending? */
    ch_flags[ch] &= ~CHF_LDW;                           /* clr pending flag */
    ch_sta[ch] = CH6S_DSW;                              /* unstall pending LCH */
    }
else {
    ch_flags[ch] |= CHF_CMD;                            /* set cmd trap flag */
    ch6_end_ds (ch);                                    /* disconnect */
    }
return SCPE_OK;
}

/* 7607 conditionally clear EOR on IOSx completion */

void ch6_iosp_cclr (uint32 ch)
{
uint32 i, op;

if (ch_wc[ch] == 0) {                                   /* wc = 0? */
    uint32 ccnt = 5;                                    /* allow 5 for CPU */
    for (i = 0; i < NUM_CHAN; i++) {                    /* test channels */
        if (ch_sta[ch] != CHXS_DSX)                     /* idle? skip */
            continue;
        op = ch_op[ch] & ~1;                            /* get op */
        ccnt++;                                         /* 1 per active ch */
        if ((op == CH6_IOCP) || (op == CH6_IORP) ||     /* 1 per proceed */
            (op == CH6_IOSP))
            ccnt++;
        }
    if (ccnt <= 11)                                     /* <= 11? ok */
        return;
    }
ch_flags[ch] = ch_flags[ch] & ~CHF_EOR;                 /* clear eor */
return;
}

/* 7607 external interface routines */

/* Input - store word, request channel input service */

t_stat ch6_req_rd (uint32 ch, uint32 unit, t_uint64 val, uint32 fl)
{
if (ch6_qconn (ch, unit)) {                             /* ch conn to caller? */
    if (ch_idf[ch] & CH6DF_VLD)                         /* overrun? */
        ind_ioc = 1;
    ch_idf[ch] = CH6DF_VLD;                             /* set ar valid */
    if (fl)                                             /* set eor if requested */
        ch_idf[ch] |= CH6DF_EOR;
    ch_req |= REQ_CH (ch);                              /* request chan */
    ch_flags[ch] |= CHF_RDS;
    ch_ar[ch] = val & DMASK;                            /* save data */
    }
return SCPE_OK;
}

/* Disconnect on error */

t_stat ch6_err_disc (uint32 ch, uint32 unit, uint32 fl)
{
if (ch6_qconn (ch, unit)) {                             /* ch conn to caller? */
    ch_flags[ch] |= fl;                                 /* set flag */
    return ch6_end_ds (ch);                             /* disconnect */
    }
return SCPE_OK;
}

/* Output - request channel output service */

t_bool ch6_req_wr (uint32 ch, uint32 unit)
{
if (ch6_qconn (ch, unit)) {                             /* ch conn to caller? */
    ch_req |= REQ_CH (ch);
    ch_flags[ch] &= ~CHF_RDS;
    }
return SCPE_OK;
}

/* Set/read channel flags */

uint32 ch6_set_flags (uint32 ch, uint32 unit, uint32 flags)
{
if (ch6_qconn (ch, unit)) {                             /* ch conn to caller? */
    ch_flags[ch] = ch_flags[ch] | flags;
    return ch_flags[ch];
    }
return 0;
}

/* Channel connected to unit? */

t_bool ch6_qconn (uint32 ch, uint32 unit)
{
if ((ch < NUM_CHAN) &&                                  /* valid chan */
    (ch_dsu[ch] == unit))                               /* for right unit? */
    return TRUE;
return FALSE;
}

/* 7909 channel support routines */

/* 7909 channel input routine - put one word to memory */

t_stat ch9_rd_putw (uint32 ch)
{
ch_idf[ch] = 0;                                         /* invalidate */
if (ch_wc[ch]) {                                        /* wc > 0? */
    WriteP (ch_ca[ch], ch_ar[ch]);
    ch_ca[ch] = CHAINC (ch_ca[ch]);
    ch_wc[ch] = ch_wc[ch] - 1;
    }
return SCPE_OK;
}

/* 7909 channel output routine - get one word from memory */

t_stat ch9_wr_getw (uint32 ch)
{
if (ch_wc[ch]) {
    ch_ar[ch] = ReadP (ch_ca[ch]);                      /* get word */
    ch_ca[ch] = CHAINC (ch_ca[ch]);
    ch_wc[ch] = ch_wc[ch] - 1;
    }
else ch_ar[ch] = 0;
return ch9_wr (ch, ch_ar[ch], 0);                       /* write to device */
}

/* 7909 send select to device */

t_stat ch9_sel (uint32 ch, uint32 sel)
{
DEVICE *dptr = ch2dev[ch];
DIB *dibp;

if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp && dibp->chsel)
    return dibp->chsel (ch, sel, 0);
return SCPE_IERR;
}

/* 7909 send word to device */

t_stat ch9_wr (uint32 ch, t_uint64 dat, uint32 fl)
{
DEVICE *dptr = ch2dev[ch];
DIB *dibp;

if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp && dibp->write)
    return dibp->write (ch, dat, fl);
return SCPE_IERR;
}

/* 7909 channel new command */

t_stat ch9_new_cmd (uint32 ch)
{
t_uint64 ir;
uint32 t;
t_stat r;

ir = ReadP (t = ch_clc[ch]);                            /* read cmd */
r = ch9_exec_cmd (ch, ir);                              /* exec cmd */
if (ch_sta[ch] != CHXS_IDLE)                            /* chan running? */
    ch_clc[ch] = CHAINC (ch_clc[ch]);                   /* incr chan pc */
if ((r == SCPE_OK) && sim_brk_summ && sim_brk_test (t, SWMASK ('E')))
    return ch_bkpt (ch, t);
return r;
}

t_stat ch9_exec_cmd (uint32 ch, t_uint64 ir)
{
uint32 op;

ch_wc[ch] = GET_DEC (ir);                               /* get word cnt */
ch_ca[ch] = ((uint32) ir) & CHAMASK;                    /* get address */
op = (GET_OPD (ir) << 2);                               /* get opcode */
ch_op[ch] = op | ((((uint32) ir) & 0200000)? 1: 0) |    /* plus bit<19> */
    (((op & 010) && (ch_wc[ch] & 040000))? 2: 0);       /* plus bit 3 if used */
if (ir & CHI_IND) {                                     /* indirect? */
    t_uint64 sr = ReadP (ch_ca[ch] & CHAMASK);          /* read indirect */
    ch_ca[ch] = ((uint32) sr) & CHAMASK;                /* get address */
    }
if (hst_ch)
    cpu_ent_hist (ch_clc[ch] | ((ch + 1) << HIST_V_CH), ch_ca[ch], ir, 0);

switch (ch_op[ch]) {                                    /* check initial cond */

    case CH9_LAR:                                       /* misc processing */
    case CH9_SAR:
    case CH9_ICC:
    case CH9_ICCA:
    case CH9_XMT:
    case CH9_LCC:
    case CH9_SMS:
        if (ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS))
            ch9_eval_int (ch, CHINT_SEQC);              /* not during data */
                                                        /* fall through */
    case CH9_TCM:                                       /* jumps */
    case CH9_TCH:
    case CH9_TDC:
    case CH9_LIPT:
    case CH9_LIP:
        ch_req |= REQ_CH (ch);                          /* process in chan */
        break;

    case CH9_CTL:                                       /* control */
    case CH9_CTLR:
    case CH9_CTLW:
        if (ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS))
            ch9_eval_int (ch, CHINT_SEQC);              /* not during data */
        ch_flags[ch] &= ~CHF_EOR;
        if (ch_wc[ch] & CH9D_NST)                       /* N set? proc in chan */
            ch_req |= REQ_CH (ch);
        else return ch9_sel (ch, CHSL_CTL);             /* sel, dev sets ch_req! */
        break;

    case CH9_SNS:                                       /* sense */
        if (ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS))
            ch9_eval_int (ch, CHINT_SEQC);
        ch_flags[ch] &= ~CHF_EOR;
        ch_req |= REQ_CH (ch);                          /* process in chan */
        break;  

    case CH9_CPYD:                                      /* data transfers */
    case CH9_CPYP:
        if ((ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS)) == 0)
            ch9_eval_int (ch, CHINT_SEQC);              /* not unless data */
        if (ch_flags[ch] & CHF_PRD)
            ch_flags[ch] |= CHF_RDS;
        else if (ch_flags[ch] & CHF_PWR)
            ch_flags[ch] |= CHF_WRS;
        ch_flags[ch] &= ~(CHF_EOR|CHF_PRD|CHF_PWR);
        if ((ch_op[ch] == CH9_CPYP) && (ch_wc[ch] == 0))
            ch_req |= REQ_CH (ch);                      /* CPYP x,,0? */
        break;                                          /* dev sets ch_req! */

    case CH9_WTR:                                       /* wait */
        ch_sta[ch] = CHXS_IDLE;                         /* stop */
        break;

    case CH9_TWT:                                       /* trap and wait */
        ch_sta[ch] = CHXS_IDLE;                         /* stop */
        ch_flags[ch] |= CHF_TWT;                        /* set trap */
        break;

    default:
        return STOP_ILLIOP;
        }

return SCPE_OK;
}

/* 7909 external interface routines */

/* Input - store word, request channel input service */

t_stat ch9_req_rd (uint32 ch, t_uint64 val)
{
if (ch < NUM_CHAN) {                                    /* valid chan? */
    if (ch_idf[ch] & CH9DF_VLD)                         /* prev still valid? io chk */
        ch9_set_ioc (ch);
    ch_idf[ch] = CH9DF_VLD;                             /* set ar valid */
    ch_req |= REQ_CH (ch);                              /* request chan */
    ch_ar[ch] = val & DMASK;                            /* save data */
    }
return SCPE_OK;
}

/* Set attention */

void ch9_set_atn (uint32 ch)
{
if (ch < NUM_CHAN)
    ch9_eval_int (ch, CHINT_ATN1);
return;
}

/* Set IO check - UEND will occur at end - not recognized in int mode */

void ch9_set_ioc (uint32 ch)
{
if ((ch < NUM_CHAN) && !(ch_flags[ch] & CHF_INT)) {
    ind_ioc = 1;                                        /* IO check */
    ch_flags[ch] |= CHF_IOC;                            /* ch IOC for end */
    }
return;
}

/* Set end */

void ch9_set_end (uint32 ch, uint32 iflags)
{
if (ch < NUM_CHAN) {                                    /* valid chan? */
    ch_flags[ch] |= CHF_EOR;
    ch9_eval_int (ch, iflags);
    }
return;
}

/* Test connected */

t_bool ch9_qconn (uint32 ch)
{
if ((ch < NUM_CHAN) && (ch_sta[ch] == CHXS_DSX))
    return TRUE;
return FALSE;
}

/* Evaluate interrupts 

   - Interrupt requests set flags in the channel flags word
   - If an interrupt is not in progress, interrupt requests are evaluated
   - If an interrupt request is found, the interruptable flags are
     transferred to the channel condition register and cleared in
     the channel flags

   This provides an effective stage of buffering for interrupt requests
   that are not immediately serviced */

void ch9_eval_int (uint32 ch, uint32 iflags)
{
uint32 ireq;

ch_flags[ch] |= (iflags << CHF_V_COND);                 /* or into chan flags */
if ((ch_flags[ch] & CHF_INT) == 0) {                    /* int not in prog? */
    ireq = ((ch_flags[ch] >> CHF_V_COND) & CHF_M_COND) &
        ~(((ch_sms[ch] & CHSMS_IUEND)? CHINT_UEND: 0) |
          ((ch_sms[ch] & CHSMS_IATN1)? CHINT_ATN1: 0) |
          ((ch_sms[ch] & CHSMS_IATN2)? CHINT_ATN2: 0) |
          ((ch_flags[ch] & (CHF_PRD|CHF_PWR|CHF_RDS|CHF_WRS))? CHINT_SEQC: 0));
    if (ireq) {                                         /* int pending? */
        ch_cnd[ch] = ireq;                              /* set cond reg */
        ch_flags[ch] &= ~(ireq << CHF_V_COND);          /* clear chan flags */
        ch_flags[ch] |= CHF_IRQ;                        /* set int req */
        ch_req |= REQ_CH (ch);                          /* request channel */
        }
    }
return;
}

/* Test for all channels idle */

t_bool ch_qidle (void)
{
uint32 i;

for (i = 0; i < NUM_CHAN; i++) {
    if (ch_sta[i] != CHXS_IDLE)
        return FALSE;
    }
return TRUE;
}

/* Evaluate/execute channel traps */

uint32 chtr_eval (uint32 *decr)
{
uint32 i, cme;

if (!chtr_inht && !chtr_inhi && chtr_enab) {
    if (BIT_TST (chtr_enab, CHTR_V_CLK) && chtr_clk) {  /* clock trap? */
        if (decr) {                                     /* exec? */
            chtr_clk = 0;                               /* clr flag */
            *decr = 0;
            }
        return CHTR_CLK_SAV;
        }
    for (i = 0; i < NUM_CHAN; i++) {                    /* loop thru chan */
        cme = BIT_TST (chtr_enab, CHTR_V_CME + i);      /* cmd/eof enab? */
        if (cme && (ch_flags[i] & CHF_CMD)) {           /* cmd enab and set? */
            if (decr) {                                 /* exec? */
                ch_flags[i] &= ~CHF_CMD;                /* clr flag */
                *decr = CHTR_F_CMD;
                }
            return (CHTR_CHA_SAV + (i << 1));
            }
        if (cme && (ch_flags[i] & CHF_EOF)) {           /* eof enab and set? */
            if (decr) {                                 /* exec? */
                ch_flags[i] &= ~CHF_EOF;                /* clr flag */
                *decr = CHTR_F_EOF;
                }
            return (CHTR_CHA_SAV + (i << 1));
            }
        if (BIT_TST (chtr_enab, CHTR_V_TRC + i) &&      /* trc enab? */
            (ch_flags[i] & CHF_TRC)) {                  /* trc flag? */
            if (decr) {                                 /* exec? */
                ch_flags[i] &= ~CHF_TRC;                /* clr flag */
                *decr = CHTR_F_TRC;
                }
            return (CHTR_CHA_SAV + (i << 1));
            }                                           /* end if BIT_TST */
        }                                               /* end for */
    }                                                   /* end if !chtr_inht */
if (decr)
    *decr = 0;
return 0;
}

/* Channel reset */

t_stat ch_reset (DEVICE *dptr)
{
uint32 ch = dptr - &ch_dev[0];                          /* get channel */

if (ch == CH_A)                                         /* channel A fixed */
    ch2dev[ch] = &mt_dev[0];
ch_sta[ch] = 0;
ch_flags[ch] = 0;
ch_idf[ch] = 0;
ch_dso[ch] = 0;
ch_dsu[ch] = 0;
ch_ndso[ch] = 0;
ch_ndsu[ch] = 0;
ch_op[ch] = 0;
ch_clc[ch] = 0;
ch_wc[ch] = 0;
ch_ca[ch] = 0;
ch_ar[ch] = 0;
ch_sms[ch] = 0;
ch_cnd[ch] = 0;
ch_lcc[ch] = 0;
sim_cancel (&ch_unit[ch]);
return SCPE_OK;
}

/* Show channel type */

t_stat ch_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
DEVICE *dptr;

dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
if (dptr->flags & DEV_7909)
    fputs ("7909", st);
else if (dptr->flags & DEV_7289)
    fputs ("7289", st);
else fputs ("7607", st);
return SCPE_OK;
}

/* Enable channel, assign device */

t_stat ch_set_enable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
DEVICE *dptr, *dptr1;
char gbuf[CBUFSIZE];
uint32 i, ch;

dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
ch = dptr - &ch_dev[0];
if ((ch == 0) || !(dptr->flags & DEV_DIS))
    return SCPE_ARG;
if (cptr == NULL)
    cptr = "TAPE";
get_glyph (cptr, gbuf, 0);
for (i = 0; dev_table[i].name; i++) {
    if (strcmp (dev_table[i].name, gbuf) == 0) {
        dptr1 = ch_map_flags (ch, dev_table[i].flags);
        if (!dptr1 || !(dptr1->flags & DEV_DIS))
            return SCPE_ARG;
        dptr->flags &= ~(DEV_DIS|DEV_7909|DEV_7289|DEV_7750|DEV_7631);
        dptr->flags |= dev_table[i].flags;
        dptr1->flags &= ~DEV_DIS;
        ch2dev[ch] = dptr1;
        return reset_all (0);
        }
    }
return SCPE_ARG;
}

/* Map device flags to device pointer */

DEVICE *ch_map_flags (uint32 ch, int32 fl)
{
if (fl & DEV_7289)
    return &drm_dev;
if (!(fl & DEV_7909))
    return &mt_dev[ch];
if (fl & DEV_7631)
    return &dsk_dev;
if (fl & DEV_7750)
    return &com_dev;
return NULL;
}

/* Set up channel map */

void ch_set_map (void)
{
uint32 i;

for (i = 0; i < NUM_CHAN; i++) {
    if (ch_dev[i].flags & DEV_DIS)
        ch2dev[i] = NULL;
    else ch2dev[i] = ch_map_flags (i, ch_dev[i].flags);
    }
return;
}       

/* Disable channel, deassign device */

t_stat ch_set_disable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
DEVICE *dptr, *dptr1;
UNIT *uptr1;
uint32 i, ch;

dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
ch = dptr - &ch_dev[0];
if ((ch == 0) || (dptr->flags & DEV_DIS) || (cptr != NULL))
    return SCPE_ARG;
dptr1 = ch2dev[ch];
if (dptr1 == NULL)
    return SCPE_IERR;
if (dptr1->units) {
    for (i = 0; i < dptr1->numunits; i++) {
        uptr1 = dptr1->units + i;
        if (dptr1->detach)
            dptr1->detach (uptr1);
        else detach_unit (uptr1);
        }
    }
dptr->flags &= ~(DEV_7909|DEV_7289);
dptr->flags |= DEV_DIS;
dptr1->flags |= DEV_DIS;
return reset_all (0);
}

/* Show channel that device is on (tapes, 7289, 7909 only) */

t_stat ch_show_chan (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
DEVICE *dptr;
uint32 i;

dptr = find_dev_from_unit (uptr);
if (dptr) {
    for (i = 0; i < NUM_CHAN; i++) {
        if (ch2dev[i] == dptr) {
            fprintf (st, "channel %c", 'A' + i);
            return SCPE_OK;
            }
        }
    }
fprintf (st, "not assigned to channel");
return SCPE_OK;
}
