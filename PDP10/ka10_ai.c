/* ka10_ai.c: Systems Concepts DC-10 disk control

   Copyright (c) 2019, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This disk controller was probably only ever used with the MIT AI
   Lab PDP-10.  Since the device name DC is alreay claimed, we call
   this AI.
*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_AI
#define NUM_DEVS_AI 0
#endif


#if (NUM_DEVS_AI > 0)

/* Disk pack geometry.  The track format is software defined.  ITS and
   SALV makes it hold two sectors with 1024 words of regular data and
   4 extra words. */
#define SECTOR_SIZE        1024
#define SECTORS            2
#define SURFACES           20
#define MEMOREX_CYLINDERS  203
#define CALCOMP_CYLINDERS  (2 * MEMOREX_CYLINDERS)
#define CYLINDER_SIZE      (SECTOR_SIZE * SECTORS * SURFACES)
#define MEMOREX_SIZE       (CYLINDER_SIZE * MEMOREX_CYLINDERS)
#define CALCOMP_SIZE       (CYLINDER_SIZE * CALCOMP_CYLINDERS)

/* The real sector size, including 2 header words, 4 extra data words,
   and 2 checksum words. */
#define SECTOR_REAL_SIZE   (SECTOR_SIZE + 8)
/* A track actually has some more free space.  Cylinder 0, surface 0
   has a readin block there. */
#define TRACK_REAL_SIZE    ((SECTORS + 1) * SECTOR_REAL_SIZE)
#define CYLINDER_REAL_SIZE (SURFACES * TRACK_REAL_SIZE)

#define AI_DEVNUM       0610    /* First device number; 614 also used. */
#define AI_NAME         "AI"
#define NUM_UNITS       16      /* Hardware units, but ITS only supports 8. */

/* All bit definitions are from the ITS file SYSTEM; DC10 DEFS27. */

/* CONI DC0 */
#define DASSGN  0400000000000LL /* ASSIGNED TO PROC (WITH SWITCH) */
#define DPIRQC  0400000 /* PI REQ BEING GENERATED */
#define DSSRQ   0200000 /* SEEK REQUEST */
#define DSDEEB  0010000 /* ENABLE INTERRUPT ON DATA ERROR OR READ/ COMP ERROR */
#define DSSERR  0004000 /* ERROR FLAG */
#define DSSAEB  0002000 /* ATTENTION ENABLE FLAG */
#define DSSATT  0001000 /* ATTENTION FLAG */
#define DSIENB  0000400 /* IDLE FLAG ENABLE */
#define DSSRUN  0000200 /* RUN */
#define DSSACT  0000100 /* ACTIVE */
#define DSSCEB  0000040 /* CHANNEL ENABLE */
#define DSSCHF  0000020 /* CHANNEL FLAG */
#define DSSCFL  0000010 /* CPU FLAG */

/* CONO DC0 */
#define DCSET   0400000 /* SET SELECTED */
#define DCCLR   0200000 /* CLEAR SELECTED */
#define DCCSET  0600000 /* RESET CONTROLLER THEN SET SELECTED */
#define DCDENB  0010000 /* DATA ERROR ENABLE */
#define DCERR   0004000 /* SET ERROR FLAG OR CLEAR ALL ERRORS */
#define DCATEB  0002000 /* ATTENTION ENABLE */
#define DCCATT  0001000 /* CLEAR ATTENTION */
#define DCSSRQ  0001000 /* SET SEEK REQUEST */
#define DCIENB  0000400 /* IDLE ENABLE */
#define DCSTAR  0000200 /* START (SET) */
#define DCSSTP  0000200 /* STOP (CLEAR) */
#define DCSGL   0000100 /* DO SINGLE COMMAND */
#define DCCENB  0000040 /* CHANNEL ENABLE */
#define DCCFLG  0000020 /* CHANNEL FLAG */
#define DCCPUF  0000010 /* CPU FLAG */

/* Bits to set or clear with DCSET or DCCLR. */
#define SET_MASK (DCDENB|DCERR|DCATEB|DCIENB|DCSTAR)
#define CLEAR_MASK (DCDENB|DCERR|DCATEB|DCCATT|DCIENB|DCSSTP)

/* CONI DC1 */
#define DIPE    04000   /* INTERNAL PARITY ERROR */
#define DRLNER  02000   /* RECORD LENGTH */
#define DRCER   01000   /* READ COMPARE ERROR */
#define DOVRRN  00400   /* OVERRUN */
#define DCKSER  00200   /* CKSUM OR DECODER ERR */
#define DWTHER  00100   /* WATCHDOG TIMER */
#define DFUNSF  00040   /* FILE UNSAFE, SEEK INCOMPLETE OR END OR DSK */
#define DOFFL   00020   /* OFF LINE OR MULT SEL */
#define DPROT   00010   /* WRT KEY OR RD ONLY OR PROTECT */
#define DDOBSY  00004   /* DATAO WHEN BSY */
#define DNXM    00002   /* NON-EX MEM */
#define DCPERR  00001   /* CORE PARITY ERR */

/* Channel commands */
#define DUNENB  0020000000000LL /* ENABLE LOAD UNIT FIELD */
#define DCMD    0740000000000LL
#define DCOPY   0040000000000LL /* COPY */
#define DCCOMP  0100000000000LL /* COMPARE */
#define DCSKIP  0140000000000LL /* SKIP */
#define DOPR    0200000000000LL /* BASIC OPR */
#define DSDRST  0240000000000LL /* STORE DRIVE STATUS */
#define DALU    0300000000000LL /* BASIC ALU OP CODE */
#define DRC     0400000000000LL /* READ COMPARE */
#define DWRITE  0440000000000LL /* WRITE */
#define DREAD   0500000000000LL /* READ */
#define DSEEK   0540000000000LL /* SEEK */
#define DRCC    0600000000000LL /* READ COMPARE CONTINUOUS */
#define DWRITC  0640000000000LL /* WRITE CONTINUOUS */
#define DREADC  0700000000000LL /* READ CONTINUOUS */
#define DSPC    0740000000000LL /* Special command. */

#define DHLT    0               /* 0 IN 4.9-4.5 = JUMP AND IN 3.5,3.6 = HALT */
#define DXCT    0000020000000LL /* XCT */
#define DJMP    0000040000000LL /* JUMP */
#define DJSR    0000060000000LL /* JSR */
#define DJMASK  0000060000000LL

/* OPR */
#define DOHXFR  0400000000LL    /* HALT DURING XFER (SO MB WILL BE SAFE) */

/* Special command, E condition (wait). */
#define DSWIDX  0020000000LL    /* WAIT UNTIL INDEX PULSE */
#define DSWSEC  0040000000LL    /* WAIT UNTIL SECTOR PULSE */
#define DSWINF  0060000000LL    /* NEVER (USE WITH G=3 OR 7) */
/* Special command, F condition (other wait). */
#define DSWNUL  0014000000LL    /* NO WAIT */
/* Special command, G operation. */
#define DSCRHD  0200000000LL    /* READ HEADER WORDS */
#define DSRCAL  0300000000LL     /* (RECALIBRATE) */
#define DSCWIM  0500000000LL    /* WRITE IMAGE */

/* ALU */
#define DLCC    010000000LL     /* OP FROM CC, STORE IN CC */
#define DLDBWC  030000000LL     /* OP A FROM DB, STORE IN WC */

#define WC      0037774000000LL /* Word count. */
#define ADDR    0000003777777LL /* Address field. */

/* Drive status. */

#define DDSWC   040000000LL             /* WRITE CURRENT SENSED */
#define DDSUNS  020000000LL             /* DRIVE UNSAFE */
#define DDSRDO  010000000LL             /* READ ONLY */
#define DDSSIC  004000000LL             /* SEEK INCOMPLETE */
#define DDSRDY  002000000LL             /* DRIVE READY */
#define DDSONL  001000000LL             /* DRIVE ON LINE */
#define DDSSEL  000400000LL             /* DRIVE SELECTED */

enum {
    MODE_ERROR = 0,
    MODE_WRITE,                 /* Write sector data. */
    MODE_READ,                  /* Read sector data. */
    MODE_READ_HEADERS,          /* Read sector headers. */
    MODE_COMPARE,               /* Compare sector data. */
    MODE_IMAGE                  /* Write raw image. */
};

enum image_state {
    IMAGE_GAP,                  /* Empty bits (ones) between sectors. */
    IMAGE_PREAMBLE,             /* Bit pattern before sector header. */
    IMAGE_HEADER,               /* Sector header, in FM encoding. */
    IMAGE_POSTAMBLE,            /* Empty bits (ones). */
    IMAGE_POSTAMBLE2,           /* A "01" to start the sector data. */
    IMAGE_SECTOR,               /* Sector data, in FM encoding. */
    IMAGE_ERROR,
};

static enum image_state image_state = IMAGE_ERROR;
static int image_count, image_sector_length;

static t_stat ai_devio(uint32 dev, uint64 *data);
static t_stat ai_svc(UNIT *);
static t_stat ai_reset(DEVICE *);
static t_stat ai_attach(UNIT *, CONST char *);
static t_stat ai_detach(UNIT *);
static t_stat ai_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                       const char *cptr);
static const char *ai_description (DEVICE *dptr);


UNIT ai_unit[] = {
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
    { UDATA (&ai_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, CALCOMP_SIZE) },
};

DIB ai_dib = { AI_DEVNUM, 2, &ai_devio, NULL };

MTAB ai_mod[] = {
    {0}
};

DEBTAB ai_debug[] = {
    {"IRQ", DEBUG_IRQ, "Debug IRQ requests"},
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show CONI instructions"},
    {"CONO", DEBUG_CONO, "Show CONO instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show DATAI and DATAO instructions"},
    {0, 0}
};

static UNIT *channel_unit = ai_unit;
static int latency_unit = 0;
static int channel_pc = 0;
static int channel_status = 0;
static uint64 channel_errors = 0;
static int channel_cc = 0;
static int channel_wc = 0;
static int channel_mode = MODE_ERROR;
static int channel_delay;
static int channel_default_delay = 1000;
static int channel_seek_initial = 25000; /* Milliseconds. */
static int channel_seek_delay = 500; /* Per cylinder travelled. */
static int channel_cylinder = 0;

REG ai_reg[] = {
    {ORDATA(PC, channel_pc, 20)},
    {ORDATA(STS, channel_status, 18)},
    {ORDATA(ERR, channel_errors, 12)},
    {ORDATA(CC, channel_cc, 20)},
    {ORDATA(WC, channel_wc, 12)},
    {ORDATA(SI, channel_seek_initial, 32)},
    {ORDATA(SD, channel_seek_delay, 32)},
    {ORDATA(CYL, channel_cylinder, 9)},
    {0}
};

DEVICE ai_dev = {
    AI_NAME, ai_unit, ai_reg, ai_mod,
    NUM_UNITS, 8, 18, 1, 8, 36,
    NULL, NULL, &ai_reset, NULL, &ai_attach, &ai_detach,
    &ai_dib, DEV_DISABLE | DEV_DEBUG, 0, ai_debug,
    NULL, NULL, &ai_help, NULL, NULL, &ai_description
};

static void clear_interrupt (void)
{
    if ((channel_status & (DSDEEB|DSSERR)) == (DSDEEB|DSSERR)
        || (channel_status & (DSSAEB|DSSATT)) == (DSSAEB|DSSATT)
        || (channel_status & (DSIENB|DSSRUN)) == DSIENB) {
        channel_status |= DPIRQC;
        sim_debug(DEBUG_IRQ, &ai_dev, "Set interrupt: %06o\n", channel_status);
        set_interrupt (AI_DEVNUM, channel_status);
    } else {
        channel_status &= ~DPIRQC;
       sim_debug(DEBUG_IRQ, &ai_dev, "Clear interrupt\n");
        clr_interrupt (AI_DEVNUM);
    }
}

static void channel_error (int errors)
{
    channel_errors |= errors;
    channel_status |= DSSERR;
    if (channel_status & DSDEEB) {
        channel_status |= DPIRQC;
        sim_debug(DEBUG_IRQ, &ai_dev, "Set error interrupt\n");
        set_interrupt (AI_DEVNUM, channel_status);
    }
}

static void channel_seek (const char *cmd, uint64 data, int offset)
{
    int cyl, sur, sec, x;
    int da;

    if (data & DUNENB)
        channel_unit = &ai_unit[(data >> 033) & 017];

    cyl = (data >> 11) & 0777;
    sur = (data >> 6) & 037;
    sec = data & 077;

    if (cyl >= CALCOMP_CYLINDERS && sur >= SURFACES && sec >= SECTORS) {
        sim_debug(DEBUG_EXP, &ai_dev, "Seek outside geometry\n");
        channel_error (DOVRRN);
        return;
    }

    da = SECTOR_REAL_SIZE * sec;
    da += TRACK_REAL_SIZE * sur;
    da += CYLINDER_REAL_SIZE * cyl;
    da += offset;
    if (channel_unit->flags & UNIT_ATT) {
        (void)sim_fseek(channel_unit->fileref, da * sizeof(uint64), SEEK_SET);
        x = channel_cylinder - cyl;
        if (x < 0)
            x = -x;
        if (x > 0)
            channel_delay = channel_seek_initial + x * channel_seek_delay;
        channel_cylinder = cyl;
        sim_debug(DEBUG_CMD, &ai_dev, "%s: unit %d seek %d (%d,%d,%d)\n",
                  cmd, (int)(channel_unit - ai_unit), channel_delay,
                  cyl, sur, sec);
    } else {
        sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
        channel_error (DOFFL);
    }
}

static void channel_special (uint64 data)
{
    if (data & DUNENB)
        channel_unit = &ai_unit[(data >> 033) & 017];

    switch (data & 0700000000LL) {
    case DSCRHD:
        channel_mode = MODE_READ_HEADERS;
        channel_seek ("READ HEADER WORDS", data, 0);
        break;
    case DSRCAL:
        sim_debug(DEBUG_CMD, &ai_dev, "Command: (RECALIBRATE)\n");
        channel_status |= DSSATT;
        channel_errors &= ~(017LL << 036);
        channel_errors |= (channel_unit - ai_unit) << 036;
        if (channel_status & DSSAEB) {
            channel_status |= DPIRQC;
            sim_debug(DEBUG_IRQ, &ai_dev, "Set attention interrupt\n");
            set_interrupt (AI_DEVNUM, channel_status);
        }
        break;
    case DSCWIM:
        if ((channel_unit->flags & UNIT_ATT) == 0) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
            channel_error (DOFFL);
        } else if (channel_unit->flags & UNIT_RO) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive read only\n");
            channel_error (DPROT);
        } else {
            channel_mode = MODE_IMAGE;
            image_state = IMAGE_GAP;
            image_count = 0;
            channel_seek ("WRITE IMAGE", data, 0);
        }
        break;
    default:
        sim_debug(DEBUG_CMD, &ai_dev, "(unknown special: %012llo)\n", data);
        break;
    }
}

static void channel_alu (uint64 data)
{
    switch (data & 034000000LL) {
    case DLCC:
        channel_cc = data & ADDR;
        sim_debug(DEBUG_CMD, &ai_dev, "ALU: OP FROM CC, STORE IN CC: %o\n", channel_cc);
        break;
    case DLDBWC:
        channel_wc = data & 07777;
        sim_debug(DEBUG_CMD, &ai_dev, "ALU: OP A FROM DB, STORE IN WC: %o\n", channel_wc);
        break;
    default:
        sim_debug(DEBUG_CMD, &ai_dev, "ALU: (unkownn)\n");
        break;
    }
}

static void print_data (uint64 *data, int n)
{
    int i;
    for (i = 0; i < n; i++)
        sim_debug(DEBUG_DATA, &ai_dev, "Data %012llo\n",
                  *data++);
}

static t_stat sim_fcompare (void *x, size_t m, size_t n, FILE *f)
{
    static uint64 buf[10240];

    if ((channel_unit->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
        channel_error (DOFFL);
        return SCPE_OK;
    }

    (void)sim_fread (buf, m, n, f);
    sim_debug(DEBUG_DATA, &ai_dev, "Memory contents:\n");
    print_data ((uint64 *)x, n);
    sim_debug(DEBUG_DATA, &ai_dev, "Disk contents:\n");
    print_data (buf, n);
    if (memcmp (x, buf, m * n) != 0) {
        sim_debug(DEBUG_EXP, &ai_dev, "Compare failed.\n");
        channel_error (DRCER);
    }
    return SCPE_OK;
}

/* The WRITE IMAGE command writes the sector headers as 56 continuous
   bits.  However, the READ HEADERS command presents them as 28-bit
   halves, each right aligned in a 36-bit word.  The image file stores
   the first format, so here we need to split the words apart.  Also
   skip over the sector data to get to next header. */
static t_stat sim_freadh (uint64 *x, size_t n, FILE *f)
{
    uint64 buf[2];
    size_t i;

    if ((channel_unit->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
        channel_error (DOFFL);
        return SCPE_OK;
    }

    for (i = 0; i < n; i++) {
        if ((i & 1) == 0) {
            (void)sim_fread (buf, sizeof (uint64), 2, f);
            (void)sim_fseek(f, (SECTOR_REAL_SIZE-2) * sizeof(uint64), SEEK_CUR);
            x[i] = buf[0] >> 8;
        } else {
            x[i] = (buf[0] & 0377) << 20;
            x[i] |= buf[1] >> 16;
        }
    }

    return SCPE_OK;
}

/* The track data fields are in somthing close to FM encoding.  Here
   we decode three bits at a time to yeild two bits of data.  When 36
   data bits have been decoded, output a word to the image file. */
static void decode_fm (int bit, FILE *f)
{
    static int state = 0;
    static uint64 word = 0;
    static int n = 0;
    static int bits = 1;

    bits = (bits << 1) + bit;
    state++;

    if (state != 3)
        return;

    word <<= 2;

    switch (bits & 017) {
    case 005:
    case 007:
        word |= (bits >> 4) & 2;
        word |= (bits >> 1) & 1;
        break;
    case 012:
    case 016:
        break;
    case 013:
    case 015:
    case 017:
        word |= (bits >> 1) & 3;
        break;
    default:
        sim_debug(DEBUG_EXP, &ai_dev, "Error in FM encoding: %o\n", bits);
        channel_error (DCKSER);
        break;
    }

    state = 0;
    n += 2;

    //sim_debug(DEBUG_DETAIL, &ai_dev, "FM: %o, %d, %012llo\n",
    //          bits, n, word);

    if (n == 36) {
        //sim_debug(DEBUG_DETAIL, &ai_dev, "Data: %012llo\n", word);
        (void)sim_fwrite (&word, sizeof word, 1, f);
        n = 0;
        word = 0;
    }
}

/* Decode a bit stream from the WRITE IMAGE command. */
static void decode_bit (int bit, FILE *f)
{
    static const int preamble_bits[] = { 1, 0, 1, 0, 1 };

    //sim_debug(DEBUG_DETAIL, &ai_dev, "Image: bit %o\n", bit);

    switch (image_state) {
    case IMAGE_GAP:
        if (bit == 0) {
            sim_debug(DEBUG_DETAIL, &ai_dev, "Image: %d gap bits\n",
                      image_count);
            image_state = IMAGE_PREAMBLE;
            image_count = 0;
        } else {
            image_count++;
        }
        break;
    case IMAGE_PREAMBLE:
        if (bit != preamble_bits[image_count % 5]) {
            sim_debug(DEBUG_DETAIL, &ai_dev, "Image: error in preamble bit %d\n",
                      image_count);
            image_state = IMAGE_ERROR;
            break;
        }
        image_count++;
        if (image_count == 5*8) {
            sim_debug(DEBUG_DETAIL, &ai_dev, "Image: preamble ok\n");
            image_state = IMAGE_HEADER;
            image_count = 0;
        }
        break;
    case IMAGE_HEADER:
        decode_fm (bit, f);
        image_count++;
        if (image_count == 108) {
            t_offset pos;
            uint64 header[2];
            image_state = IMAGE_POSTAMBLE;
            image_count = 0;
            pos = sim_ftell (channel_unit->fileref);
            (void)sim_fseeko(channel_unit->fileref, pos - 2 * sizeof(uint64), SEEK_SET);
            (void)sim_fread(header, sizeof(uint64), 2, channel_unit->fileref);
            (void)sim_fseeko(channel_unit->fileref, pos, SEEK_SET);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: key %03llo\n",
                      (header[0] >> 28) & 0377);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: cylinder %lld\n",
                      (header[0] >> 19) & 0777);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: surface %lld\n",
                      (header[0] >> 14) & 037);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: sector %lld\n",
                      (header[0] >> 8) & 077);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: indirect %llo\n",
                      (header[0] >> 7) & 1);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: software protect %llo\n",
                      (header[0] >> 6) & 1);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: hardware protect %llo\n",
                      (header[0] >> 5) & 1);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: parity %llo\n",
                      header[0] & 3);
            image_sector_length = 040000 - ((header[1] >> 16) & 037777);
            sim_debug(DEBUG_DETAIL, &ai_dev, "Header: length %o\n",
                      image_sector_length);
            if (image_sector_length > 02004) {
                  sim_debug(DEBUG_EXP, &ai_dev, "Record length error\n");
                  channel_error (DRLNER);
                  image_state = IMAGE_SECTOR;
            }
            image_sector_length += 2; /* Checksum */
            image_sector_length *= 54; /* 36-bit words, FM coded. */
        }
        break;
    case IMAGE_POSTAMBLE:
      image_count++;
      if (bit == 0) {
          sim_debug(DEBUG_DETAIL, &ai_dev, "Image: %d gap bits\n",
                    image_count);
          image_state = IMAGE_POSTAMBLE2;
          image_count = 0;
      }
      break;
    case IMAGE_POSTAMBLE2:
      if (bit == 0) {
          sim_debug(DEBUG_DETAIL, &ai_dev, "Image: error in postamble\n");
          image_state = IMAGE_ERROR;
      } else {
          image_state = IMAGE_SECTOR;
      }
      break;
    case IMAGE_SECTOR:
      decode_fm (bit, f);
      image_count++;
      if (image_count == image_sector_length) {
          image_state = IMAGE_GAP;
          image_count = 0;
      }
      break;
    case IMAGE_ERROR:
      break;
    }
}

static void decode_image (uint64 *data, int n, FILE *f)
{
  int i, j;
  for (i = 0; i < n; i++) {
      for (j = 35; j >= 0; j--)
          decode_bit ((int)(*data >> j) & 1, f);
      data++;
  }
}

static int check_nxm (uint64 data, int *n, uint64 *data2, int *n2)
{
    unsigned int addr = data & ADDR;
    *data2 = 0;
    *n2 = 0;
    if (addr + *n > MEMSIZE) {
        if (MEMSIZE < (ADDR+1)) {
            sim_debug(DEBUG_EXP, &ai_dev, "Access outside core memory\n");
            *n = MEMSIZE - addr;
            channel_error (DNXM);
            return 1;
        } else {
            /* Access wraps around 21-bit address. */
            *n2 = addr + *n - MEMSIZE;
            *data2 = 0;
            *n = MEMSIZE - addr;
            return 0;
        }
    }
    return 0;
}

/* Execute one channel instruction.  It may come from a channel
   program in core, or from a DATAO DC0, */
static void channel_command (uint64 data)
{
    struct timespec ts;
    int latency_timer;
    int n, n2;
    uint64 data2;
    int nxm = 0;

    if ((data & (DCMD|DUNENB)) == 0) {
        switch (data & DJMASK) {
        case DHLT:
            sim_debug(DEBUG_CMD, &ai_dev, "Command: DHLT\n");
            channel_status &= ~(DSSRUN|DSSACT);
            if (channel_status & DSIENB) {
                channel_status |= DPIRQC;
                sim_debug(DEBUG_IRQ, &ai_dev, "Set idle interrupt\n");
                set_interrupt (AI_DEVNUM, channel_status);
            }
            break;
        case DXCT:
            sim_debug(DEBUG_CMD, &ai_dev, "Command: XCT\n");
            break;
        case DJMP:
            channel_status |= DSSRUN|DSSACT;
            sim_activate(ai_unit, channel_default_delay);
            clear_interrupt ();
            if ((data & 014000000LL) == 004000000LL) {
              sim_debug(DEBUG_CMD, &ai_dev, "Command: JUMP DAOJNC: %o\n",
                        channel_cc);
              channel_cc++;
              if (channel_cc != ADDR+1)
                channel_pc = data & ADDR;
            } else {
                sim_debug(DEBUG_CMD, &ai_dev, "Command: JUMP\n");
                channel_pc = data & ADDR;
            }
            break;
        case DJSR:
            sim_debug(DEBUG_CMD, &ai_dev, "Command: JSR\n");
            n = 1;
            if (check_nxm (data, &n, &data2, &n2))
                break;
            M[data & ADDR] = (channel_pc + (channel_unit - ai_unit)) << 036;
            channel_pc++;
            channel_status |= DSSRUN|DSSACT;
            sim_activate(ai_unit, channel_default_delay);
            break;
        }
        return;
    }

    switch (data & DCMD) {
    case DCOPY:
        n = (data & WC) >> 20;
        if (n == 0)
            n = channel_wc;
        n = 010000 - n;
        sim_debug(DEBUG_CMD, &ai_dev, "COPY %d words to/from %012llo.\n",
                  n, data & ADDR);
        if ((channel_unit->flags & UNIT_ATT) == 0) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
            channel_error (DOFFL);
            break;
        }
        nxm = check_nxm (data, &n, &data2, &n2);
        switch (channel_mode) {
        case MODE_READ:
            (void)sim_fread (&M[data & ADDR], sizeof(uint64), n,
                             channel_unit->fileref);
            if (nxm)
                break;
            (void)sim_fread (&M[data2], sizeof(uint64), n2,
                             channel_unit->fileref);
            print_data (&M[data & ADDR], n);
            break;
        case MODE_READ_HEADERS:
            (void)sim_freadh (&M[data & ADDR], n, channel_unit->fileref);
            if (nxm)
                break;
            (void)sim_freadh (&M[data2], n2, channel_unit->fileref);
            break;
        case MODE_WRITE:
            if (channel_unit->flags & UNIT_RO) {
                sim_debug(DEBUG_EXP, &ai_dev, "Drive read only\n");
                channel_error (DPROT);
            } else {
                (void)sim_fwrite (&M[data & ADDR], sizeof(uint64), n,
                                  channel_unit->fileref);
                if (nxm)
                    break;
                (void)sim_fwrite (&M[data2], sizeof(uint64), n2,
                                  channel_unit->fileref);
            }
            break;
        case MODE_COMPARE:
            (void)sim_fcompare (&M[data & ADDR], sizeof(uint64), n,
                                channel_unit->fileref);
            if (nxm)
                break;
            (void)sim_fcompare (&M[data2], sizeof(uint64), n2,
                                channel_unit->fileref);
            /* If at the end of the sector, skip to next sector. */
            if ((sim_ftell (channel_unit->fileref) / sizeof(uint64))
                % SECTOR_REAL_SIZE == 1030)
              (void)sim_fseek(channel_unit->fileref, 4 * sizeof(uint64), SEEK_CUR);
            break;
        case MODE_IMAGE:
            decode_image (&M[data & ADDR], n, channel_unit->fileref);
            break;
        default:
            break;
        }
        break;
    case DCCOMP:
        n = (data & WC) >> 20;
        if (n == 0)
            n = channel_wc;
        n = 010000 - n;
        sim_debug(DEBUG_CMD, &ai_dev, "COMPARE %d words\n", n);
        if ((channel_unit->flags & UNIT_ATT) == 0) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive offline\n");
            channel_error (DOFFL);
            break;
        }
        nxm = check_nxm (data, &n, &data2, &n2);
        (void)sim_fcompare (&M[data & ADDR], sizeof(uint64), n,
                            channel_unit->fileref);
        if (nxm)
            break;
        (void)sim_fcompare (&M[data2], sizeof(uint64), n2,
                            channel_unit->fileref);
        break;
    case DCSKIP:
        n = 010000 - ((data & WC) >> 20);
        sim_debug(DEBUG_CMD, &ai_dev, "SKIP %o words\n", n);
        (void)sim_fseek(channel_unit->fileref, n * sizeof(uint64), SEEK_CUR);
        break;
    case DOPR:
        if (data & DOHXFR)
            sim_debug(DEBUG_CMD, &ai_dev, "OPR: Hang during xfer\n");
        else
            sim_debug(DEBUG_CMD, &ai_dev, "OPR ...\n");
        break;
    case DSDRST:
        if (data & DUNENB)
            channel_unit = &ai_unit[(data >> 033) & 017];
      
        sim_debug(DEBUG_CMD, &ai_dev,
                  "DSDRST, store unit %d status in %012llo.\n",
                  (int)(channel_unit - ai_unit), data & ADDR);

        n = 1;
        if (check_nxm (data, &n, &data2, &n2))
            break;

        (void)clock_gettime(CLOCK_REALTIME, &ts);
        latency_timer = ts.tv_nsec / 100000;
        latency_timer %= 254;
        M[data & ADDR] = latency_timer & 0377;
        M[data & ADDR] |= channel_cylinder << 8;
        if (channel_unit->flags & UNIT_ATT)
            /* Drive online. */
            M[data & ADDR] |= DDSONL;
        if (channel_unit->flags & UNIT_RO)
            /* Drive read-only. */
            M[data & ADDR] |= DDSRDO;
        break;
    case DALU:
        channel_alu (data);
        break;
    case DRC:
        channel_mode = MODE_COMPARE;
        channel_seek ("READ COMPARE", data, 2);
        break;
    case DWRITE:
        if (channel_unit->flags & UNIT_RO) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive read only\n");
            channel_error (DPROT);
            channel_mode = MODE_ERROR;
        } else {
            channel_mode = MODE_WRITE;
            channel_seek ("WRITE", data, 2);
        }
        break;
    case DREAD:
        channel_mode = MODE_READ;
        channel_seek ("READ", data, 2);
        break;
    case DSEEK:
        channel_seek ("SEEK", data, 2);
        break;
    case DRCC:
        channel_mode = MODE_COMPARE;
        channel_seek ("READ COMPARE CONTINUOUS", data, 2);
        break;
    case DWRITC:
        if (channel_unit->flags & UNIT_RO) {
            sim_debug(DEBUG_EXP, &ai_dev, "Drive read only\n");
            channel_error (DPROT);
            channel_mode = MODE_ERROR;
        } else {
            channel_mode = MODE_WRITE;
            channel_seek ("WRITE CONTINUOUS", data, 2);
        }
        break;
    case DREADC:
        channel_mode = MODE_READ;
        channel_seek ("READ CONTINUOUS", data, 2);
        break;
    case DSPC:
        channel_special (data);
        break;
    default:
        sim_debug(DEBUG_CMD, &ai_dev, "(unknown command: %012llo)\n", data);
        break;
    }
}

/* Process one channel instruction and update the channel state. */
static void channel_run (void)
{
  uint64 data, data2;
    int n = 1, n2;
    if (check_nxm (channel_pc, &n, &data2, &n2))
        return;
    data = M[channel_pc];
    //sim_debug(DEBUG_CMD, &ai_dev, "Channel PC=%06o %012llo\n",
    //          channel_pc, data);
    channel_pc++;
    channel_command (data);
}

t_stat ai_devio(uint32 dev, uint64 *data) {
    struct timespec ts;
    int latency_timer;

    switch(dev & 7) {
    case CONI:
        *data = channel_status;
        sim_debug(DEBUG_CONI, &ai_dev, "DC0, PC=%06o %012llo\n", PC, *data);
        return SCPE_OK;

    case CONO:
        sim_debug(DEBUG_CONO, &ai_dev, "DC0, PC=%06o %012llo\n", PC, *data);
        if ((*data & DCCSET) == DCCSET) {
          sim_debug(DEBUG_CMD, &ai_dev, "Reset controller then set selected.\n");
          ai_reset (&ai_dev);
        }
        channel_status &= ~7;
        channel_status |= *data & 7;
        if (*data & DCSET) {
          channel_status |= *data & SET_MASK;
          if (*data & DCSSRQ)
            channel_status |= DSSRQ;
          sim_debug(DEBUG_CMD, &ai_dev, "Set bits: %012llo -> %06o\n",
                    *data & SET_MASK, channel_status);
        } else if (*data & DCCLR) {
          channel_status &= ~(*data & CLEAR_MASK);
          sim_debug(DEBUG_CMD, &ai_dev, "Clear bits: %012llo -> %06o\n",
                    *data & CLEAR_MASK, channel_status);
          if (*data & DCERR)
            channel_errors = 0;
        }
        clear_interrupt ();
        return SCPE_OK;

    case DATAI:
        *data = 0;
        sim_debug(DEBUG_DATAIO, &ai_dev, "DATAI DC0, PC=%06o %012llo\n",
                  PC, *data);
        return SCPE_OK;

    case DATAO:
        sim_debug(DEBUG_DATAIO, &ai_dev, "DATAO DC0, PC=%06o %012llo\n",
                  PC, *data);
        if (channel_status & (DSSRUN|DSSACT)) {
            sim_debug(DEBUG_EXP, &ai_dev, "DATAO when busy\n");
            channel_error (DDOBSY);
        } else
            channel_command (*data);
        return SCPE_OK;

    case CONI|4:
        /* Latency timer, timer unit, attention unit. */
        (void)clock_gettime(CLOCK_REALTIME, &ts);
        latency_timer = ts.tv_nsec / 100000;
        latency_timer %= 254;
        *data = (latency_timer << 022)
          | (latency_unit << 032) | channel_errors;
        sim_debug(DEBUG_CONI, &ai_dev, "DC1, PC=%06o %012llo\n", PC, *data);
        return SCPE_OK;

    case CONO|4:
        sim_debug(DEBUG_CONO, &ai_dev, "DC1, PC=%06o %012llo\n", PC, *data);
        sim_debug(DEBUG_CMD, &ai_dev, "Latency timer set to unit %llo\n", *data);
        latency_unit = *data & 7;
        return SCPE_OK;

    case DATAI|4:
        *data = 0;
        sim_debug(DEBUG_DATAIO, &ai_dev, "DATAI DC1, PC=%06o %012llo\n",
                  PC, *data);
        return SCPE_OK;

    case DATAO|4:
        sim_debug(DEBUG_DATAIO, &ai_dev, "DATAO DC1, PC=%06o %012llo\n",
                  PC, *data);
        return SCPE_OK;
    }
    return SCPE_OK; /* Unreached */
}

t_stat ai_svc (UNIT *uptr)
{
    int i;
    channel_delay = channel_default_delay;
    for (i = 0; (channel_status & DSSRUN) && i < 10; i++)
        channel_run ();
    if (channel_status & DSSRUN)
        sim_activate(uptr, channel_delay);
    return SCPE_OK;
}

t_stat
ai_reset(DEVICE *dptr)
{
    channel_status = 0;
    channel_errors = 0;
    channel_pc = 0;
    channel_cc = 0;
    channel_wc = 0;
    channel_mode = 0;
    return SCPE_OK;
}

/* Device attach */
t_stat ai_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    DEVICE *rptr;
    DIB *dib;

    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rptr = find_dev_from_unit(uptr);
    if (rptr == 0)
        return SCPE_OK;
    dib = (DIB *) rptr->ctxt;
    set_interrupt(dib->dev_num, 0);
    return SCPE_OK;
}

/* Device detach */
t_stat ai_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    return detach_unit (uptr);
}

t_stat ai_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Systems Concepts DC-10\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ai_description (DEVICE *dptr)
{
    return "Systems Concepts DC-10 disk controller";
}


#endif
