/* i650_mt.c: IBM 650 Magnetic tape 

   Copyright (c) 2018, Roberto Sancho

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included ni
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERTO SANCHO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "i650_defs.h"
#include "sim_tape.h"

#define UNIT_MT         UNIT_ATTABLE | UNIT_ROABLE | UNIT_DISABLE

/* in u3 is tape medium length used on current position */
/* in u4 is tape medium max length (28800 for 2400 ft reel) */
/* in u5 holds the command being executed by tape unit */
#define MT_CMDMSK   0x00FF      /* Command being run */
#define MT_RDY      0x0100      /* Unit is ready for command */
#define MT_IND      0x0200      /* Unit has Indicator light on */

/* u6 holds the current buffer position */


/* Definitions */
uint32              mt_cmd(UNIT *, uint16, uint16);
t_stat              mt_srv(UNIT *);
void                mt_ini(UNIT *, t_bool);
t_stat              mt_reset(DEVICE *);
t_stat              mt_attach(UNIT *, CONST char *);
t_stat              mt_detach(UNIT *);
t_stat              mt_rew(UNIT * uptr, int32 val, CONST char *cptr,void *desc);
t_stat              mt_set_len (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat              mt_show_len (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat              mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char          *mt_description (DEVICE *dptr);

UNIT                mt_unit[6] = {
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0}, /* 5 */
};

MTAB                mt_mod[] = {
    {MTUF_WLK,            0, "write enabled", "WRITEENABLED", NULL, NULL, NULL, "Write ring in place"},
    {MTUF_WLK,     MTUF_WLK, "write locked", "LOCKED",        NULL, NULL, NULL, "No write ring in place"},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",              &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, 
                                                                                "Set/Display tape format (SIMH, E11, TPC, P7B)"},
    {MTAB_XTD | MTAB_VUN, 0, "LENGTH", "LENGTH",              &mt_set_len, &mt_show_len, NULL,
                                                                                "Set tape medium length (50 to 10000 foot)" },
    {MTAB_XTD | MTAB_VUN, 0, NULL,     "REWIND",              &mt_rew, NULL, NULL, "Rewind tape"},
    {0}
};

DEVICE              mt_dev = {
    "MT", mt_unit, NULL, mt_mod,
    6, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, NULL, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

// IBM 652 Control Unit internal state
int LastTapeSelected  = -1;   // last tape selected. =0 to 5, -1=none yet
int LastTapeIndicator = 0;    // last tape operation has some indication to tell to program/operator
int bFastMode = 0;            // =1 for FAST operation

const char * TapeIndicatorStr[11] = { "OK",
                                      "WRITE PROTECTED", 
                                      "IO CHECK",
                                      "END OF FILE",
                                      "END OF TAPE",
                                      "LONG RECORD",
                                      "SHORT RECORD",
                                      "NO TAPE UNIT AT THIS ADDRESS",
                                      "NO REEL LOADED",
                                      "NOT READY", 
                                      "BAD CHAR"};
                                                             
// return 1 if tape unit n (0..5) is ready to receive a command
int mt_ready(int n)
{
    if ((n < 0) || (n > 5)) return 0;
    if (mt_unit[n].u5 & MT_RDY) return 1;
    return 0;
}

/* Rewind tape drive */
t_stat mt_rew(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    /* If drive is offline or not attached return not ready */
    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_NOATT;
    uptr->u3 = 0;      // tape at begin of medium
    uptr->u5 = MT_RDY; // clear indicator flag, clear last command, set ready flag
    return sim_tape_rewind(uptr);
}

int mt_read_numeric_word(uint8 * buf, t_int64 * d, int * ZeroNeg)
{
    int i, neg; 
    char c;

    neg = 0;
    *d = 0;
    if (ZeroNeg != NULL) *ZeroNeg = 0;
    for (i=0;i<10;i++) {
        c = *buf++;
        if (i==9) { // is last word digit 
            if ((c >= '0') && (c <= '9')) return MT_IND_BADCHAR;          // last digit should have sign
            if (c == '?') c = '0';                                        // +0
            if ((c >= 'A') && (c <= 'I')) c = c - 'A' + '1';              // +1 to +9
            if ((c >= 'J') && (c <= 'R')) {c = c - 'J' + '1'; neg=1;}     // -1 to -9
            if (c == '!') {c = '0'; neg=1;}                               // -0
        } 
        if ((c < '0') || (c > '9')) return MT_IND_BADCHAR;
        *d = *d * 10 + (c - '0');
    }
    if (neg) *d = -*d;
    if (ZeroNeg != NULL) *ZeroNeg = ((neg) && (*d == 0)) ? 1:0;
    return 0;
}

int mt_read_alpha_word(uint8 * buf, t_int64 * d)
{
    int i, n; 
    char c;

    *d = 0;
    for (i=0;i<5;i++) {
        c = *buf++;
        n = ascii_to_NN(c);
        if ((n==0) && (c != ' ')) return MT_IND_BADCHAR;
        *d = *d * 100 + n;
    }
    return 0;
}

int mt_transfer_tape_rec_to_IAS(uint8 * buf, t_mtrlnt reclen, char mode)
{
    int n,ic,r, ZeroNeg;
    t_int64 d, CtrlWord;
    char s[6];
    t_mtrlnt expected_reclen; 
    
    if (mode == 'N') {
        // numeric mode
        expected_reclen = (60 - IAS_TimingRing) * 10; // record len expected
        // does expected record len match read record from tape?
        if (expected_reclen != reclen) {
            return (reclen > expected_reclen) ? MT_IND_LONG_REC : MT_IND_SHORT_REC;
        }
        // yes, record length match -> load IAS with tape record data
        ic = 0;
        while (1) {
            // read numeric word from tape
            r = mt_read_numeric_word(&buf[ic], &d, &ZeroNeg);
            if (r) return r;
            ic += 10;
            // store into IAS
            IAS[IAS_TimingRing] = d;
            IAS_NegativeZeroFlag[IAS_TimingRing] = ZeroNeg;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Tape to IAS %04d: %06d%04d%c '%s'\n", 
                        IAS_TimingRing+9000, printfw(d,ZeroNeg), 
                        word_to_ascii(s, 1, 5, d));
            // incr IAS_TimingRing, exit if arrived to end of IAS
            IAS_TimingRing = (IAS_TimingRing + 1) % 60;
            if (IAS_TimingRing == 0) break;
        }
        return 0;
    }
    // alphabetic mode
    // check tape record size limits
    if (reclen < 10 + 9*5) return MT_IND_SHORT_REC;
    if (reclen > 10 + 9*10) return MT_IND_LONG_REC;
    ic = 0;
    while(1) {
        // get control word
        if (ic + 10 > (int)reclen) return MT_IND_SHORT_REC;
        r = mt_read_numeric_word(&buf[ic], &CtrlWord, NULL);
        if (r) return r;
        ic += 10;
        // store it in IAS[nnn9]
        n = (IAS_TimingRing / 10) * 10 + 9;
        IAS[n] = CtrlWord;
        IAS_NegativeZeroFlag[n] = 0;
        // load rest of words
        for (n=0;n<9;n++) {
            if ((CtrlWord % 10) != 8) {
                // read a numeric word form tape
                if (ic + 10 > (int)reclen) return MT_IND_SHORT_REC;
                r = mt_read_numeric_word(&buf[ic], &d, &ZeroNeg);
                if (r) return r;
                ic += 10;
            } else {
                // read alphanumeric word from tape
                if (ic + 5 > (int)reclen) return MT_IND_SHORT_REC;
                r = mt_read_alpha_word(&buf[ic], &d); ZeroNeg=0;
                if (r) return r;
                ic += 5;
            }
            CtrlWord = CtrlWord / 10;
            // store into IAS
            IAS[IAS_TimingRing] = d;
            IAS_NegativeZeroFlag[IAS_TimingRing] = ZeroNeg;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Tape to IAS %04d: %06d%04d%c '%s'\n", 
                        IAS_TimingRing+9000, printfw(d,ZeroNeg), 
                        word_to_ascii(s, 1, 5, d));
            // incr IAS_TimingRing, exit if arrived to end of IAS
            IAS_TimingRing = (IAS_TimingRing + 1) % 60;
            if (IAS_TimingRing == 0) return MT_IND_LONG_REC;
        }
        IAS_TimingRing = (IAS_TimingRing + 1) % 60; // skip control word
        if ((IAS_TimingRing == 0) && (ic != reclen)) return MT_IND_LONG_REC;
        if (ic == reclen) {
            if (IAS_TimingRing != 0) return MT_IND_SHORT_REC;
            break;
        }
    }
    return 0;
}

void mt_write_numeric_word(uint8 * buf, t_int64 d, int ZeroNeg)
{
    int i, neg; 
    char c;

    neg = 0;
    if (d < 0) {neg=1; d=-d;}
    if (ZeroNeg) neg=1;
    for (i=0;i<10;i++) {
        c = Shift_Digits(&d,1) + '0';
        if (i==9) {
            if (neg==0) { // last digit has sign
                if (c == '0') c = '?';                                        // +0
                if ((c >= '1') && (c <= '9')) c = c - '1' + 'A';              // +1 to +9
            } else {
                if ((c >= '1') && (c <= '9')) {c = c - '1' + 'J';}            // -1 to -9
                if (c == '0') {c = '!';}                                      // -0
            }
        } 
        *buf++ = c;
    }
}

void mt_write_alpha_word(uint8 * buf, t_int64 d)
{
    int i, n; 
    char c;

    for (i=0;i<5;i++) {
        n = Shift_Digits(&d,2);
        c = mem_to_ascii[n];
        *buf++ = c;
    }
}

void mt_transfer_IAS_to_tape_rec(uint8 * buf, t_mtrlnt * reclen, char mode)
{
    int n,ic,ZeroNeg;
    t_int64 d, CtrlWord;
    char s[6];

    if (mode == 'N') {
        // numeric mode
        ic = 0;
        while (1) {
            // read IAS
            d = IAS[IAS_TimingRing];
            ZeroNeg = IAS_NegativeZeroFlag[IAS_TimingRing];
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IAS %04d to Tape: %06d%04d%c '%s'\n", 
                        IAS_TimingRing+9000, printfw(d,ZeroNeg), 
                        word_to_ascii(s, 1, 5, d));
            // write numeric to tape buf
            mt_write_numeric_word(&buf[ic], d, ZeroNeg);
            ic += 10;
            // incr IAS_TimingRing, exit if arrived to end of IAS
            IAS_TimingRing = (IAS_TimingRing + 1) % 60;
            if (IAS_TimingRing == 0) break;
        }
        *reclen = (t_mtrlnt) ic;
        return;
    }
    // alphabetic mode
    ic = 0;
    while(1) {
        // get control word form IAS[nnn9]
        n = (IAS_TimingRing / 10) * 10 + 9;
        CtrlWord = IAS[n];
        // write control word in tape buf
        mt_write_numeric_word(&buf[ic], CtrlWord, 0);
        ic += 10;
        // write rest of words
        for (n=0;n<9;n++) {
            // read from IAS
            d = IAS[IAS_TimingRing];
            ZeroNeg = IAS_NegativeZeroFlag[IAS_TimingRing];
            if ((CtrlWord % 10) != 8) {
                // write a numeric word to tape buf
                mt_write_numeric_word(&buf[ic], d, ZeroNeg);
                ic += 10;
            } else {
                // write alphanumeric word to tape buf
                mt_write_alpha_word(&buf[ic], d); 
                ic += 5;
            }
            CtrlWord = CtrlWord / 10;
            // incr IAS_TimingRing, exit if arrived to end of IAS
            IAS_TimingRing = (IAS_TimingRing + 1) % 60;
            if (IAS_TimingRing == 0) break;
        }
        if (IAS_TimingRing == 0) break;
        IAS_TimingRing = (IAS_TimingRing + 1) % 60; // skip control word
        if (IAS_TimingRing == 0) break;
    }
    *reclen = (t_mtrlnt) ic;
}

/* Start off a mag tape command */
uint32 mt_cmd(UNIT * uptr, uint16 cmd, uint16 fast)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = uptr - &mt_unit[0];
    int                 i, ic, time;
    t_stat              r;   
    uint8               buf[1024];
    char                cbuf[100];
    t_mtrlnt            reclen;

    time = 0;
    /* Make sure valid drive number */
    if ((unit > 5) || (unit < 0)) return STOP_ADDR;
    // init IBM 652 Control Unit internal registers
    LastTapeSelected  = unit;
    LastTapeIndicator = 0;
    bFastMode = fast;
    /* If tape unit disabled return error */
    if (uptr->flags & UNIT_DIS) {
        sim_debug(DEBUG_EXP, dptr, "Tape %d: command %02d attempted on disabled tape\n", unit, cmd);
        LastTapeIndicator = MT_IND_DIS;
        // not stated in manual: what happends if command to non existant tape?
        // option 1 -> cpu halt
        // option 2 -> tape indictor flag set (used this)
        return SCPE_OK;
    }
    /* If tape has no file attached return error */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr, "Tape %d: command %02d attempted on tape without file attached\n", unit, cmd);
        LastTapeIndicator = MT_IND_NOATT;
        uptr->u5 |= MT_IND; // turn on tape indicator light to signal to operator the faulting tape
        return SCPE_OK;
    }
    uptr->u5 &= ~(MT_CMDMSK | MT_RDY | MT_IND); // remove last command sent to tape, remove ready flag, remove tape indicator flag
    uptr->u5 |= cmd; // set current command in execution
    switch (cmd) {
    case OP_RTC:
    case OP_RTA:
    case OP_RTN:
        sim_debug(DEBUG_DATA, dptr, "Tape unit %d: init read\n", unit);
        // actual simulated tape read
        reclen = 0;
        r = sim_tape_rdrecf(uptr, buf, &reclen, sizeof(buf));
        // calc tape pos:
        // each char uses 0,005 inches. at the end of record the IRG (inter gap record) uses 3/4 inchs (0.75)
        // scaled x1000 to use integer values
        uptr->u3 += (int32) ((reclen * 0.005 + 0.75) * 1000);
        // process result conditions
        if (r == MTSE_TMK) {
            sim_debug(DEBUG_EXP, dptr, "Tape unit %d: tape mark sensed\n", unit);
            LastTapeIndicator = MT_IND_EOF;
            uptr->u5 |= MT_IND; 
        } else if ((r == MTSE_EOM) || (uptr->u3 > uptr->u4*1000)) {
            sim_debug(DEBUG_EXP, dptr, "Tape unit %d: end of tape sensed\n", unit);
            LastTapeIndicator = MT_IND_EOT;
            uptr->u5 |= MT_IND; 
        } else if (r == MTSE_RECE) {
            // record header contains error flag
            sim_debug(DEBUG_EXP, dptr, "Tape unit %d: longitudinal or vertical check error\n", unit);
            LastTapeIndicator = MT_IND_IOCHECK;
            uptr->u5 |= MT_IND; 
        } else if (r != MTSE_OK) {
            sim_debug(DEBUG_EXP, dptr, "Tape unit %d: read error %d\n", unit, r);
            return STOP_IO;
        }
        // debug output: display buf as 50 chars per line
        sim_debug(DEBUG_DETAIL, dptr, "Read record (%d chars) from tape:\n", (int) reclen);
        ic = 0;
        while (1) {
            for (i=0;i<50;i++) {
                cbuf[i] = 0;
                if (ic == reclen) break;
                cbuf[i] = buf[ic++];
            }
            sim_debug(DEBUG_DETAIL, dptr, "... '%s'\n", cbuf);
            if (ic == reclen) break;
        }
        // calc wordcount time needed to finish tape operation
        time = msec_to_wordtime(11 + reclen * 0.068); 
        // transfer read data to IAS
        if ((cmd != OP_RTC) && (LastTapeIndicator == 0)) {            
            LastTapeIndicator = mt_transfer_tape_rec_to_IAS(buf, reclen, (cmd == OP_RTN) ? 'N':'A');
            if (LastTapeIndicator) {
                sim_debug(DEBUG_EXP, dptr, "Tape unit %d: decode error %s\n", unit, TapeIndicatorStr[LastTapeIndicator]); 
                uptr->u5 |= MT_IND; 
            }
        }
        break;
    case OP_WTM:
    case OP_WTA:
    case OP_WTN:
        sim_debug(DEBUG_CMD, dptr, "Tape unit %d: init write\n", unit);
        if (cmd == OP_WTM) {
            r = sim_tape_wrtmk(uptr);
            // calc tape pos:
            uptr->u3 += (int32) ((1 * 0.005 + 0.75) * 1000); // Tape Mark is 1 word long
            reclen=1;
            sim_debug(DEBUG_DETAIL, dptr, "Write Tape Mark\n");
        } else {
            sim_debug(DEBUG_DETAIL, dptr, "IAS TimingRing is %d\n", IAS_TimingRing+9000);
            mt_transfer_IAS_to_tape_rec(buf, &reclen, (cmd == OP_WTN) ? 'N':'A');
            // actual simulated tape write
            r = sim_tape_wrrecf(uptr, buf, reclen);
            // calc tape pos:
            uptr->u3 += (int32) ((reclen * 0.005 + 0.75) * 1000);
            // debug output: display buf as 50 chars per line
            sim_debug(DEBUG_DETAIL, dptr, "Write record (%d chars) to tape:\n", (int) reclen);
            ic = 0;
            while (1) {
                for (i=0;i<50;i++) {
                    cbuf[i] = 0;
                    if (ic == reclen) break;
                    cbuf[i] = buf[ic++];
                }
                sim_debug(DEBUG_DETAIL, dptr, "... '%s'\n", cbuf);
                if (ic == reclen) break;
            }
            sim_debug(DEBUG_DETAIL, dptr, "     IAS TimingRing is %d\n", IAS_TimingRing+9000);
        }
        // process result conditions
        if (r == MTSE_WRP) {
            LastTapeIndicator = MT_IND_WRT_PROT;
            uptr->u5 |= MT_IND; 
        } else if ((r == MTSE_EOM) || (uptr->u3 > uptr->u4*1000)) {
            LastTapeIndicator = MT_IND_EOT;
            uptr->u5 |= MT_IND; 
        } else if (r != MTSE_OK) {
            sim_debug(DEBUG_EXP, dptr, "Tape unit %d: write error %d\n", unit, r);
            return STOP_IO;
        }
        // calc wordcount time needed
        time = msec_to_wordtime(11 + reclen * 0.068); // time to remove Tape Control interlock
        break;
    case OP_BST:
    case OP_RWD:
        /* Check if at load point, quick return if so */
        if (sim_tape_bot(uptr)) {
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: at BOT\n", unit);
            uptr->u5 |= MT_RDY;
            uptr->u3 = 0;
            return SCPE_OK;
        }
        if (cmd == OP_RWD) {
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: init rewind\n", unit);
            sim_tape_rewind(uptr);
            uptr->u3 = 0;
            time = msec_to_wordtime(35); // 35 msec to remove Tape Control interlock
        }
        if (cmd == OP_BST) {
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: init backstep record\n", unit);
            r = sim_tape_sprecr(uptr, &reclen);
            if ((r != MTSE_OK) && (r != MTSE_TMK)) {
                return r;
            }
            uptr->u3 -= (int32) ((reclen * 0.005 + 0.75) * 1000);
            time = msec_to_wordtime(38.5 + reclen * 0.068); // time to remove Tape Control interlock
        }
        break;
    default:
        sim_debug(DEBUG_EXP, dptr, "Tape %d: unknown command %02d\n", unit, cmd);
        // should never occurs. just to catch it if so.
    }
    if (bFastMode) time = 0;
    sim_cancel(uptr);
    sim_activate(uptr, time);
    return SCPE_OK_INPROGRESS;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->u5 & MT_CMDMSK;
    int                 time;

    switch (cmd) {
    case OP_RTC:
    case OP_RTA:
    case OP_RTN:
    case OP_WTM:
    case OP_WTA:
    case OP_WTN:
        if (InterLockCount[IL_Tape]) {
            // remove Tape Control Interlock
            InterLockCount[IL_Tape] = 0;
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: free TCI interlock\n", unit);
        }
        if (InterLockCount[IL_IAS]) {
            // remove IAS Interlock
            InterLockCount[IL_IAS] = 0;
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: free IAS interlock\n", unit);
        }
        // command finished
        goto tape_done;
        break;
    case OP_BST:
    case OP_RWD:
        if (InterLockCount[IL_Tape]) {
            // remove Tape Control Interlock
            InterLockCount[IL_Tape] = 0;
            sim_debug(DEBUG_CMD, dptr, "Tape unit %d: free TCI interlock\n", unit);
            // calculate end of backspace/rew time
            if (cmd == OP_BST) {
                time = msec_to_wordtime(38.5 + 22); 
            } else {
                // max time to rew is 1.2 minutes.
                // get a rought aprox on % medium used (not exacta as not taking into account Hi/low speed rew)
                time = (int) ((uptr->u3 / (uptr->u4*1000.0))*1.2*60+1);  // number of seconds
                time = msec_to_wordtime(time * 1000);         // number of word times
            }
            if (bFastMode) time = 0;
            sim_cancel(uptr);
            sim_activate(uptr, time);
        } else {
            // command finished
            goto tape_done;
        }
        break;
    default: 
        return SCPE_ARG; // should never occurs. just to catch it if so.
    tape_done:
        sim_debug(DEBUG_CMD, dptr, "Tape unit %d: ready\n", unit);
        sim_debug(DEBUG_DETAIL, &cpu_dev, "... Tape %d done, used %4.2f%% of medium\n", 
            unit,
            (uptr->u3 / (uptr->u4*1000.0))*100.0
            );
        // set unit ready to accept new commands
        uptr->u5 |= MT_RDY;
        break;
    }
    return SCPE_OK;
}

void mt_ini(UNIT * uptr, t_bool f)
{
    if (uptr->flags & UNIT_ATT) {
        uptr->u5 = MT_RDY;
    } else {
        uptr->u5 = 0;
    }
    uptr->u3 = 0;
    if (uptr->u4 == 0) uptr->u4 = 28800; // default 2400 ft reel; 1 foot = 12 inches; 2400 ft = 28800 inches
} 

t_stat mt_reset(DEVICE * dptr)
{
    int i;
    for (i = 0; i < 6; i++) {
        mt_ini(&mt_unit[i], 0);
    }
    return SCPE_OK;
}

t_stat mt_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u3 = 0;
    uptr->u5 = MT_RDY;
    return SCPE_OK;
}

t_stat mt_detach(UNIT * uptr)
{
    uptr->u3 = 0;
    uptr->u5 = 0;
    sim_cancel(uptr); // cancel any pending command
    return sim_tape_detach(uptr);
}

/* Set tape length */

t_stat mt_set_len (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int len;
    t_stat r;

    if ((cptr == NULL) || (*cptr == 0))  return SCPE_ARG;
    len = (int) get_uint (cptr, 10, 10000, &r);
    if (r != SCPE_OK) return SCPE_ARG;
    if (len < 50) return SCPE_ARG;
    uptr->u4 = 28800 * len / 2400;
    return SCPE_OK;
}

/* Show tape length */

t_stat mt_show_len (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "length %d foot", uptr->u4 * 2400 / 28800);
    return SCPE_OK;
}


t_stat
mt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", mt_description(dptr));
   fprintf (st, "The magnetic tape assumes that all tapes are 7 track\n");
   fprintf (st, "with valid parity. Tapes are assumed to be 200 characters per\n");
   fprintf (st, "inch. \n\n");
   sim_tape_attach_help (st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
mt_description(DEVICE *dptr)
{
   return "IBM 727 Magnetic tape unit";
}


