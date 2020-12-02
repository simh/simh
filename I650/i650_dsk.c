/* i650_dsk.c: IBM 650 RAMAC Disk Dotrage

   Copyright (c) 2018, Roberto Sancho

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
   ROBERTO SANCHO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   
*/

#include "i650_defs.h"

#define UNIT_DSK       UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX

#define DISK_SIZE     (12*60*100)    // a physical disk plate size: 12 bytes per word x 60 words per track x 100 tracks per disk
                                    // there are 100 like this in each unit

#define UPDATE_RAMAC        10        // update ramac arm movement each 10 msec of simulted time
                                    // time pregress as drum wordcount progresses

/* Definitions */
uint32              dsk_cmd(int opcode, int32 addr, uint16 fast);
t_stat              dsk_srv(UNIT *);
void                dsk_ini(UNIT *, t_bool f);
t_stat              dsk_reset(DEVICE *);
t_stat              dsk_attach(UNIT *, CONST char *);
t_stat              dsk_detach(UNIT *);
t_stat              dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char          *dsk_description (DEVICE *dptr);

UNIT                dsk_unit[4] = {
    {UDATA(&dsk_srv, UNIT_DSK, 0), 0}, /* 0 */
    {UDATA(&dsk_srv, UNIT_DSK, 0), 0}, /* 1 */
    {UDATA(&dsk_srv, UNIT_DSK, 0), 0}, /* 2 */
    {UDATA(&dsk_srv, UNIT_DSK, 0), 0}, /* 3 */
};

DEVICE              dsk_dev = {
    "DSK", dsk_unit, NULL, NULL,
    4, 8, 15, 1, 8, 8,
    NULL, NULL, &dsk_reset, NULL, &dsk_attach, &dsk_detach,
    &dsk_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dsk_help, NULL, NULL, &dsk_description
};


// array for disc units (4) arm's positions (3 arms per unit)
struct armrec {    
    int current_disk, current_track;        // current disk plate/track where the arm is positioned
    int dest_disk, dest_track;              // destination position where the arm should go
    int cmd;                                // opcode being executed (OP_SDS, OP_RDS, OP_WDS) 
    t_int64 InitTime;                       // timestamp using global wordTime counter when operation starts
    struct armmov {
        int disk, track;                    // disk plate/track where the arm is positioned in this point of movement sequence
        int msec;                            // time in msec arm stay in this position
    } seq[1+100+50+100+1];                  // sequeece of arm movement. If =0 -> end of sequence
} Arm[4][3];


int dsk_read_numeric_word(char * buf, t_int64 * d, int * ZeroNeg)
{
    int i, neg; 
    char c;

    neg = 0;
    *d = 0;
    if (ZeroNeg != NULL) *ZeroNeg = 0;
    for (i=0;i<10;i++) {
        c = *buf++;
        if ((c < '0') || (c > '9')) c='0';
        *d = *d * 10 + (c - '0');
    }
    if (*buf++ == '-') neg=1;
    if (neg) *d = -*d;
    if (ZeroNeg != NULL) *ZeroNeg = ((neg) && (*d == 0)) ? 1:0;
    return 0;
}



void dsk_write_numeric_word(char * buf, t_int64 d, int ZeroNeg)
{
    int i, neg; 
    char c;

    neg = 0;
    if (d < 0) {neg=1; d=-d;}
    if (ZeroNeg) neg=1;
    for (i=0;i<10;i++) {
        c = Shift_Digits(&d,1) + '0';
        *buf++ = c;
    }
    *buf++ = neg ? '-':'+';
}


// perform the operation (Read, Write) on RAMAC unit file
// init file if len=0 (flat format)
// 
t_stat dsk_operation(int cmd, int unit, int arm, int disk, int track)
{

    FILE *f;
    int flen, i, ic, ZeroNeg;
    char buf[DISK_SIZE+1]; 
    t_int64 d;
    char s[6];
                           // buf holds a full disk

    if ((unit < 0) || (unit > 3)) return 0;
    if ((arm < 0)  || (arm > 2) ) return 0;
    if ((disk < 0) || (disk > 99)) return 0;
    if ((track < 0) || (track > 99)) return 0;

    f = dsk_unit[unit].fileref; // get disk file from unit; 

    flen = sim_fsize(f);
    if (flen == 0) {
        // new file, fill it with blanks
        memset(buf, 32, sizeof(buf)); // fill with space
        for (i=1;i<1000;i++) buf[i*12*6-1]=13; // ad some cr lo allow text editor to vire ramac file
        buf[sizeof(buf)-1]=0;         // add string terminator
        for(i=0;i<100;i++) sim_fwrite(buf, 1, DISK_SIZE, f);
    }
    sim_fseek(f, DISK_SIZE * disk, SEEK_SET);
    sim_fread(buf, 1, DISK_SIZE, f); // read the entire disc (100 tracks)
    ic = 12 * 60 * track;            // ic is char at beginning of track
    sim_debug(DEBUG_DETAIL, &cpu_dev, "... RAMAC file at fseek %d, ic %d\n", DISK_SIZE * disk, ic);
    if (cmd==OP_RDS) {
        for(i=0;i<60;i++) {
            dsk_read_numeric_word(&buf[ic], &d, &ZeroNeg);
            ic += 12; // 12 bytes per word
            // store into IAS
            IAS[i] = d;
            IAS_NegativeZeroFlag[i] = ZeroNeg;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... RAMAC to IAS %04d: %06d%04d%c '%s'\n", 
                        i+9000, printfw(d,ZeroNeg), 
                        word_to_ascii(s, 1, 5, d));
        }
        // set IAS_TimingRing. Nothing said in RAMAC manual, but needed to make supersoap CDD pseudo op work properly
        IAS_TimingRing=0;
    } else if (cmd==OP_WDS) {
        for(i=0;i<60;i++) {
            // read IAS
            d = IAS[i];
            ZeroNeg = IAS_NegativeZeroFlag[i];
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IAS %04d to RAMAC: %06d%04d%c '%s'\n", 
                        i+9000, printfw(d,ZeroNeg), 
                        word_to_ascii(s, 1, 5, d));
            // write numeric to disk buf
            dsk_write_numeric_word(&buf[ic], d, ZeroNeg);
            ic += 12;
        }
        // set IAS_TimingRing. Nothing said in RAMAC manual, but needed to make supersoap CDD pseudo op work properly
        IAS_TimingRing=0;
        // write back disk to ramac unit file
          sim_fseek(f, DISK_SIZE * disk, SEEK_SET);
        sim_fwrite(buf, 1, DISK_SIZE, f); // write the entire disc (100 tracks)
    }
    // don't know if Seek Opcode (SDS) also sets TimingRing to zero

    return SCPE_OK;
}


// return 1 if disk unit n (0..3) and arm (0..2) is ready to receive  a command
int dsk_ready(int unit, int arm)
{
    if ((unit < 0) || (unit > 3)) return 0;
    if ((arm < 0)  || (arm > 2) ) return 0;
    if (Arm[unit][arm].cmd == 0) return 1; // arm has no cmd to execute -> it is ready to receive new command
    return 0;
}

void dsk_set_mov_seq(int unit,int arm)
{
    // set arm movement sequence to its destination
    //
    // arm timing
    //    seek: 50 msec setup time 
    //          on same disk:
    //              2 msec per track in same disk (0-99)
    //              25 msec  sensing track gap (that identifies the start of track pos) a mean between 0-50 msec or
    //                       to extract arm outside disk for arm to go to another disk
    //          going to another physical disk:
    //               200 msec start arm vertical motion
    //                9  msec per physical disk (0 to 49)
    //               200 msec stop arm vertical motion
    //               
    //   read: 110 msec
    //   write: 135 msec
    //

    int cmd, nseq, i, d1, d2, dy, tr;

    cmd = Arm[unit][arm].cmd;
    nseq = 0;

    // seek or read/write but current arm pos not the addr selected for 
    // read/write -> must do a seek cycle 
    if ((cmd == OP_SDS) || 
        (Arm[unit][arm].current_disk  != Arm[unit][arm].dest_disk) ||
        (Arm[unit][arm].current_track != Arm[unit][arm].dest_track)) {
        // start seek sequence at current arm pos
        Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].current_disk;
        Arm[unit][arm].seq[nseq].track = tr = Arm[unit][arm].current_track;
        Arm[unit][arm].seq[nseq++].msec = 50; // msec needed for seek setup time
           // is arm already accessing physical destination disk?
        if ((d1=(Arm[unit][arm].current_disk % 50)) != (d2=(Arm[unit][arm].dest_disk % 50))) {
            // not yet, should move arm up or down
            // is arm outside physical disk stack?
            if (Arm[unit][arm].current_track >= 0) {
                // not yet, should move arm outside physical disk (up to -1)
                // move out arm track to track until outside of physical disk
                for (i=Arm[unit][arm].current_track;i>=0;i--) {
                    Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].current_disk;
                    Arm[unit][arm].seq[nseq].track = i;
                    Arm[unit][arm].seq[nseq++].msec = 2; // msec needed for horizontal arm movement of 1 track                    
                }
            }
            // now arm is outside disk stack, can move up and down
            Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].current_disk;
            Arm[unit][arm].seq[nseq].track = -1;
            Arm[unit][arm].seq[nseq++].msec = 200; // msec needed to setup vertical arm movement 
            // move out up/down on disk stack up to destination disk
            dy = (d1 < d2) ? +1:-1;
            i = Arm[unit][arm].current_disk;
            for (;;) {
                if (i % 50 == d2) break;
                Arm[unit][arm].seq[nseq].disk = i;
                Arm[unit][arm].seq[nseq].track = -1;
                Arm[unit][arm].seq[nseq++].msec = 9; // msec needed for vertical arm movement of 1 physical disk
                i=i+dy;
            }
            // stop motion and select destination disk (not physical disk)
            Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
            Arm[unit][arm].seq[nseq].track = tr = -1;
            Arm[unit][arm].seq[nseq++].msec = 200; // msec needed to stop vertical arm movement 
        }
           // now arm accessing physical destination disk
        // is arm at destination track?
        if (tr != (d2=Arm[unit][arm].dest_track)) {
            // not yet, should move arm horizontally
            dy = (tr < d2) ? +1:-1;
            for (;;) {
                if (tr == d2) break;
                Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
                Arm[unit][arm].seq[nseq].track = tr;
                Arm[unit][arm].seq[nseq++].msec = 2; // msec needed for horizontal arm movement of 1 track                    
                tr=tr+dy;
            }
        }
        // now arm is positioned on destination track, disk
        // sense the track gap to finish seek operation
        Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
        Arm[unit][arm].seq[nseq].track = Arm[unit][arm].dest_track;
        Arm[unit][arm].seq[nseq++].msec = 25; // msec needed for sensing track gap
    }

    // read operation
    if (cmd == OP_RDS) {
        Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
        Arm[unit][arm].seq[nseq].track = Arm[unit][arm].dest_track;
        Arm[unit][arm].seq[nseq++].msec = 110; // msec needed for reading entire track
    } else if (cmd == OP_WDS) {
        Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
        Arm[unit][arm].seq[nseq].track = Arm[unit][arm].dest_track;
        Arm[unit][arm].seq[nseq++].msec = 135; // msec needed for writing entire track
    }
    // set end of sequence
    Arm[unit][arm].seq[nseq].disk = Arm[unit][arm].dest_disk;
    Arm[unit][arm].seq[nseq].track = Arm[unit][arm].dest_track;
    Arm[unit][arm].seq[nseq++].msec = 0; // end of sequence mark
}

/* Start off a RAMAC command */
uint32 dsk_cmd(int cmd, int32 addr, uint16 fast)
{
    DEVICE             *dptr;
    UNIT               *uptr;
    int                 unit, disk, track, arm; 
    int                    time; 
    int                    bFastMode; 

    unit =(addr / 100000) % 10;
    disk =(addr / 1000)   % 100,     
    track=(addr /   10)   % 100,     
    arm  =(addr %   10);          

    time = 0;
    /* Make sure addr unit number */
    if ((unit > 3) || (unit < 0)) return STOP_ADDR;
    if ((arm  > 2) || (arm  < 0)) return STOP_ADDR;

    uptr = &dsk_unit[unit];
    dptr = find_dev_from_unit(uptr);
    
    // init IBM 652 Control Unit internal registers
    bFastMode = fast;

    /* If disk unit disabled return error */
    if (uptr->flags & UNIT_DIS) {
        sim_debug(DEBUG_EXP, dptr, "RAMAC command attempted on disabled unit %d\n", unit);
        // not stated in manual: what happends if command to non existant disk?
        // option 1 -> cpu halt (used this)
        // option 2 -> indictor flag set 
        return STOP_IO;
    }
    /* If disk unit has no file attached return error */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr, "RAMAC command attempted on unit %d that has no file attached\n", unit);
        return STOP_IO;
    }
    // init arm operation
    Arm[unit][arm].cmd = cmd;        // the command to execute: can be OP_SDS, OP_RDS, OP_WDS
    Arm[unit][arm].dest_disk  = disk;   // the destination address
    Arm[unit][arm].dest_track = track;
    sim_debug(DEBUG_CMD, dptr, "RAMAC unit %d, arm %d: %s on disk %d, track %d started\n", 
                                unit, arm, 
                                (cmd == OP_SDS) ? "SEEK" : (cmd == OP_RDS) ? "READ" : "WRITE",
                                Arm[unit][arm].dest_disk, Arm[unit][arm].dest_track);

    if (bFastMode) {
        time = 0; // no movement sequence. Just go to destination pos inmediatelly and exec command
        Arm[unit][arm].InitTime = -1; 
    } else {
        time = msec_to_wordtime(UPDATE_RAMAC); // sampling disk arm movement sequence each 10 msec
        Arm[unit][arm].InitTime = GlobalWordTimeCount; // when the movement sequence starts (in word time counts)
        // calculate the movement seqnece
        dsk_set_mov_seq(unit,arm);
    }
    // schedule command execution
    sim_cancel(uptr);
    sim_activate(uptr, time);
    return SCPE_OK_INPROGRESS;
}

/* Handle processing of disk requests. */
t_stat dsk_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 time, msec, arm, cmd, nseq;
    t_int64                InitTime; 
    int                    bSequenceInProgress=0; 
    int                    bFastMode; 
    t_stat                r;

    // init IBM 652 Control Unit internal registers
    bFastMode = 0;
    // update arm movement for this unit
    for (arm=0;arm<3;arm++) {
        cmd = Arm[unit][arm].cmd;
        if (cmd == 0) continue; // RAMAC arm for this disk unit is stoped (=ready). 
                                // continue to Process next arm of this unit

        // arm in movement (=busy)
        // calc time in msec elapsed from start of comand execution
        InitTime=Arm[unit][arm].InitTime;
        if (InitTime<0) {
            bFastMode=1;
        } else {
            time=msec_elapsed(Arm[unit][arm].InitTime); 
               // examine sequence of arm movements to determine what is the current position 
            // or arm at this point of time
            nseq=0;
            for(;;) {
                msec=Arm[unit][arm].seq[nseq].msec;
                if (msec==0) break; // exit beacuse end of sequence
                time=time-msec;
                if (time<0) break; // exit beacuse we are at this point of sequence
                nseq++; 
            }
            if (time <0) {
                // sequence not finisehd: set current arm pos 
                Arm[unit][arm].current_disk=Arm[unit][arm].seq[nseq].disk;
                Arm[unit][arm].current_track=Arm[unit][arm].seq[nseq].track;
                bSequenceInProgress=1; // there is an arm in movement
                // arm not arrived to its destination yet. contiinue proceed with next arm
                sim_debug(DEBUG_CMD, dptr, "RAMAC unit %d, arm %d: now at disk %d, track %d\n", 
                                unit, arm, 
                                Arm[unit][arm].current_disk, Arm[unit][arm].current_track);
                continue; 
            }
        }
        // arm arrived to its destination position
        Arm[unit][arm].current_disk=Arm[unit][arm].dest_disk;
        Arm[unit][arm].current_track=Arm[unit][arm].dest_track;
        // execute command
        sim_debug(DEBUG_DETAIL, &cpu_dev, "... RAMAC unit %d, arm %d: %s on disk %d, track %d start execution \n", 
                                    unit, arm, 
                                    (cmd == OP_SDS) ? "SEEK" : (cmd == OP_RDS) ? "READ" : "WRITE",
                                    Arm[unit][arm].dest_disk, Arm[unit][arm].dest_track);
        r = dsk_operation(cmd, unit, arm, Arm[unit][arm].dest_disk, Arm[unit][arm].dest_track);
        if (r != SCPE_OK) return STOP_IO;
        // cmd execution finished, can free IAS interlock 
        sim_debug(DEBUG_DETAIL, &cpu_dev, "... RAMAC unit %d, arm %d: %s on disk %d, track %d finished\n", 
                                    unit, arm, 
                                    (cmd == OP_SDS) ? "SEEK" : (cmd == OP_RDS) ? "READ" : "WRITE",
                                    Arm[unit][arm].dest_disk, Arm[unit][arm].dest_track);
        if (((cmd==OP_RDS) || (cmd==OP_WDS)) && (InterLockCount[IL_IAS])) {
            // remove IAS Interlock
            InterLockCount[IL_IAS] = 0;
            sim_debug(DEBUG_CMD, dptr, "RAMAC unit %d, arm %d: free IAS interlock\n", unit, arm);
        }
        // set arm as ready, so it can accept new commands
        Arm[unit][arm].cmd = 0;
        sim_debug(DEBUG_CMD, dptr, "RAMAC unit %d, arm %d READY\n", unit, arm);
    }
    // if there is any arm in movement, re-schedulle event 
    sim_cancel(uptr);
    if (bSequenceInProgress) {
        if (bFastMode) {
            time = 0; // no movement sequence. Just go to destination pos inmediatelly and exec command
        } else {
            time = msec_to_wordtime(UPDATE_RAMAC); // sampling disk arm movement sequence each 10 msec
        }
        sim_activate(uptr, time);
    }
    return SCPE_OK;
}

void dsk_ini(UNIT * uptr, t_bool f)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);

    memset(&Arm[unit], 0, sizeof(Arm[unit])); // zeroes arm info for this unit
} 

t_stat dsk_reset(DEVICE * dptr)
{
    int i;
    for (i = 0; i < 4; i++) {
        dsk_ini(&dsk_unit[i], 0);
    }
    return SCPE_OK;
}

t_stat dsk_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    int                    flen;

    if ((r = attach_unit(uptr, file)) != SCPE_OK) return r;
    flen=sim_fsize(uptr->fileref);
    if ((flen > 0) && (flen != DISK_SIZE * 100)) {
        sim_messagef (SCPE_IERR, "Invalid RAMAC Unit file size\n");
        detach_unit (uptr); 
    }
    dsk_ini(uptr, 0);
    return SCPE_OK;
}

t_stat dsk_detach(UNIT * uptr)
{
    sim_cancel(uptr); // cancel any pending command
    dsk_ini(uptr, 0);
    return detach_unit (uptr);                             /* detach unit */
}

t_stat
dsk_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", dsk_description(dptr));
   fprintf (st, "RAMAC Magnetic storage disk.\n\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
dsk_description(DEVICE *dptr)
{
   return "IBM 355 RAMAC Disk Storage Unit";
}


