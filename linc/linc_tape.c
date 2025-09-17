#include "linc_defs.h"

#define POS     u3
#define SPEED   u4
#define ACC     u5
#define OFFSET  u6

#define C (*(uint16 *)cpu_reg[1].loc)
#define A (*(uint16 *)cpu_reg[2].loc)
#define S (*(uint16 *)cpu_reg[6].loc)
#define B (*(uint16 *)cpu_reg[7].loc)
#define paused (*(int *)cpu_reg[11].loc)
#define IBZ (*(int *)cpu_reg[12].loc)

#define ACC_START    3
#define ACC_REVERSE  6
#define ACC_STOP     1
#define MAX_SPEED    (ACC_START * 625)   /* 0.1s / 160µs */
#define IBZ_WORDS    5
#define DATA_WORDS   256
#define OTHER_WORDS  7
#define BLOCK_WORDS  (IBZ_WORDS + DATA_WORDS + OTHER_WORDS)
#define START_POS    (ACC_START * (625 + (625 * 625))/2)
#define MAX_BLOCKS   512
#define MAX_POS      ((BLOCK_WORDS * MAX_BLOCKS + IBZ_WORDS) * MAX_SPEED)

#define RDC  0  /* read tape and check */
#define RCG  1  /* read tape group */
#define RDE  2  /* read tape */
#define MTB  3  /* move toward block */
#define WRC  4  /* write tape and check */
#define WCG  5  /* write tape group */
#define WRI  6  /* write tape */
#define CHK  7  /* check tape */

#define DBG            0001
#define DBG_SEEK       0002
#define DBG_READ       0004
#define DBG_WRITE      0010
#define DBG_POS        0020

static uint16 GROUP;
static int16 CURRENT_BLOCK;
static int16 WANTED_BLOCK;

static t_stat tape_svc(UNIT *uptr);
static t_stat tape_reset(DEVICE *dptr);
static t_stat tape_boot(int32 u, DEVICE *dptr);
static t_stat tape_attach(UNIT *uptr, CONST char *cptr);
static t_stat tape_detach(UNIT *uptr);

#define UNIT_FLAGS (UNIT_IDLE|UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE)
#define CAPACITY (MAX_BLOCKS * DATA_WORDS)

static UNIT tape_unit[] = {
  { UDATA(&tape_svc, UNIT_FLAGS, CAPACITY) },
  { UDATA(&tape_svc, UNIT_FLAGS, CAPACITY) },
  { UDATA(&tape_svc, UNIT_DIS,   0) },
  { UDATA(&tape_svc, UNIT_DIS,   0) },
  { UDATA(&tape_svc, UNIT_FLAGS, CAPACITY) },
  { UDATA(&tape_svc, UNIT_FLAGS, CAPACITY) }
};

static DEBTAB tape_deb[] = {
  { "DBG",      DBG },
  { "SEEK",     DBG_SEEK },
  { "READ",     DBG_READ },
  { "WRITE",    DBG_WRITE },
  { "POSITION", DBG_POS },
  { NULL, 0 }
};

DEVICE tape_dev = {
  "TAPE", tape_unit, NULL, NULL,
  6, 8, 12, 1, 8, 12,
  NULL, NULL, &tape_reset,
  &tape_boot, &tape_attach, &tape_detach,
  NULL, DEV_DEBUG, 0, tape_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

void tape_op(void)
{
  uint16 u = (C & 050) >> 3;
  UNIT *uptr = &tape_unit[u];

  if ((uptr->flags & UNIT_ATT) == 0)
    return;

  if (uptr->SPEED < 0) {
    if ((C & 7) != MTB) {
      sim_debug(DBG_SEEK, &tape_dev, "Reverse to forward\n");
      uptr->ACC = ACC_REVERSE;
    }
  } else if (uptr->POS >= MAX_POS) {
    sim_debug(DBG_SEEK, &tape_dev, "End zone; reverse\n");
    uptr->ACC = ACC_REVERSE;
  } else if (uptr->SPEED < MAX_SPEED || uptr->ACC < 0) {
    sim_debug(DBG_SEEK, &tape_dev, "Speed up\n");
    uptr->ACC = ACC_START;
  }
  if (!sim_is_active(uptr))
    sim_activate_after(uptr, 1);
  paused = 1;
  A = 0;
  WANTED_BLOCK = B & 0777;

  switch (C & 7) {
  case RDC: case RDE: case WRC: case WRI: case CHK:
    S = 256 * (B >> 9);
    GROUP = 0;
    sim_debug(DBG, &tape_dev, "Single tranfer: S=%04o, BN=%03o\n",
              S, WANTED_BLOCK);
    break;
  case RCG: case WCG:
    S = 256 * (B & 7);
    GROUP = B >> 9;
    sim_debug(DBG, &tape_dev, "Group transfer: S=%04o, BN=%03o/%o\n",
              S, WANTED_BLOCK, GROUP+1);
    break;
  case MTB:
    sim_debug(DBG, &tape_dev, "Move towards block %03o\n", WANTED_BLOCK);
    break;
  }
}

static t_stat tape_seek(UNIT *uptr, t_addr block, t_addr offset)
{
  offset = DATA_WORDS * block + offset;
  offset *= 2;
  if (sim_fseek(uptr->fileref, offset, SEEK_SET) == -1)
    return SCPE_IOERR;
  return SCPE_OK;
}

static uint16 read_word(UNIT *uptr, t_addr block, t_addr offset)
{
  t_stat stat;
  uint8 data[2];
  uint16 word;

  stat = tape_seek(uptr, block, offset);
  if (stat != SCPE_OK)
    ;
  if (sim_fread(data, 1, 2, uptr->fileref) != 2)
    ;
  if (data[1] & 0xF0)
    ;
  word = data[1];
  word <<= 8;
  word |= data[0];
  return word;
}

static void write_word(UNIT *uptr, t_addr block, t_addr offset, uint16 word)
{
  t_stat stat;
  uint8 data[2];

  stat = tape_seek(uptr, block, offset);
  if (stat != SCPE_OK)
    ;
  data[0] = word & 0xFF;
  data[1] = word >> 8;
  if (sim_fwrite(data, 1, 2, uptr->fileref) != 2)
    ;
}

/*
  IBZ BN G block CS C C G BN IBZ
   5  1  1  256  1  1 1 1 1   5
      ---------------------
            263
      --------------------------
            268
         

  start - 100 ms
  stop - 300 ms
  reverse - 100 ms
  BN to BN at 60 ips - 43 ms
    block length = 43 ms * 60 inch/s = 2.58 inch

  per word - 160 µs
    word length = 0.0096 inch
    words per inch = 104
    words per second = 6250
  end zone to end zone - 23 s
    tape length = 23 * 60 = 1380 inch = 115 feet
    end zone length = 5 feet

 */

static void tape_done(UNIT *uptr)
{
  sim_debug(DBG, &tape_dev, "Done with block\n");

  switch (C & 7) {
  case RDC: case RCG: case RDE: case CHK:
    A = 07777;
    break;
  case WRI:
    A = A ^ 07777;
    A++;
    A &= 07777;
    break;
  case MTB:
    A = (WANTED_BLOCK + ~CURRENT_BLOCK);
    A += A >> 12;
    A &= 07777;
    break;
  }

  switch (C & 7) {
  case RDC:
    if (A != 07777) {
      sim_debug(DBG, &tape_dev, "Check failed; read again\n");
      S &= ~0377;
    } else {
      sim_debug(DBG, &tape_dev, "Check passed\n");
      paused = 0;
    }
    break;
  case WRC:
    sim_debug(DBG, &tape_dev, "Block written, go back and check\n");
    // For now, done.
    A = 07777;
    paused = 0;
    break;
  case RCG: case WCG:
    if (GROUP == 0) {
      sim_debug(DBG, &tape_dev, "Done with group\n");
      paused = 0;
    } else {
      sim_debug(DBG, &tape_dev, "Blocks left in group: %d\n", GROUP);
      GROUP--;
    }
    WANTED_BLOCK = (WANTED_BLOCK + 1) & 0777;
    break;
  case RDE: case WRI:
    sim_debug(DBG, &tape_dev, "Transfer done\n");
    paused = 0;
    break;
  case MTB:
    sim_debug(DBG, &tape_dev, "Move towards block done, result %04o\n", A);
    paused = 0;
    break;
  case CHK:
    sim_debug(DBG, &tape_dev, "Check done\n");
    paused = 0;
    break;
  }

  if (paused)
    ;
  else if ((C & 020) == 0) {
    sim_debug(DBG_SEEK, &tape_dev, "Instruction done, stop tape\n");
    uptr->ACC = uptr->SPEED > 0 ? -ACC_STOP : ACC_STOP;
  } else {
    sim_debug(DBG_SEEK, &tape_dev, "Instruction done, keep moving\n");
  }
}

static void tape_word(UNIT *uptr, uint16 block, uint16 offset)
{
  switch (C & 7) {
  case RDC: case RCG: case RDE: case CHK:
    B = read_word(uptr, block, offset);
    sim_debug(DBG_READ, &tape_dev,
              "Read block %03o offset %03o data %04o address %04o\n",
              block, offset, B, S);
    if ((C & 7) != CHK)
      M[S] = B;
    break;
  case WRC: case WCG: case WRI:
    B = M[S];
    sim_debug(DBG_WRITE, &tape_dev,
              "Write block %03o offset %03o data %04o address %04o\n",
              block, offset, B, S);
    write_word(uptr, block, offset, B);
    break;
  }
  S = (S+1) & 03777;
  A += B;
  A &= 07777;
}

static t_stat tape_svc(UNIT *uptr)
{
  long pos, block, offset;

  uptr->SPEED += uptr->ACC;
  if (uptr->SPEED >= MAX_SPEED) {
    uptr->SPEED = MAX_SPEED;
    uptr->ACC = 0;
  }
  else if (uptr->SPEED <= -MAX_SPEED) {
    uptr->SPEED = -MAX_SPEED;
    uptr->ACC = 0;
  } else if (uptr->SPEED == 0 && (uptr->ACC == ACC_STOP || uptr->ACC == -ACC_STOP))
    uptr->ACC = 0;
  uptr->POS += uptr->SPEED;
  sim_debug(DBG_POS, &tape_dev, "Speed %d, position %d (block %03o)\n",
            uptr->SPEED, uptr->POS, uptr->POS / MAX_SPEED / BLOCK_WORDS);

  if (uptr->POS < 0 && uptr->ACC <= 0) {
    sim_debug(DBG_SEEK, &tape_dev, "End zone; stop tape\n");
    uptr->ACC = ACC_STOP;
  } else if(uptr->POS >= MAX_POS && uptr->ACC >= 0) {
    sim_debug(DBG_SEEK, &tape_dev, "End zone; stop tape\n");
    uptr->ACC = -ACC_STOP;
  }

  if (uptr->SPEED != 0)
    /* The tape takes 160 microseconds between words.  This is
       approximately 20 memory cycles, 8 microseconds each. */
    sim_activate(uptr, 20);

  pos = uptr->POS / MAX_SPEED;
  if (pos < 0)
    return SCPE_OK;

  block = pos / BLOCK_WORDS;
  offset = pos % BLOCK_WORDS;
  if (block >= MAX_BLOCKS)
    return SCPE_OK;

  IBZ = offset < IBZ_WORDS;
  if (IBZ)
    sim_debug(DBG, &tape_dev, "Interblock zone\n");

  if (uptr->SPEED > -MAX_SPEED && uptr->SPEED < MAX_SPEED)
    return SCPE_OK;

  if (!paused)
    return SCPE_OK;

  if (uptr->SPEED > 0) {
    if (offset == 5) {
      /* Forward block number. */
      CURRENT_BLOCK = (uint16)(block + uptr->OFFSET);
      sim_debug(DBG_SEEK, &tape_dev,
                "Found block number %03o; looking for %03o\n",
                CURRENT_BLOCK, WANTED_BLOCK);
      if (CURRENT_BLOCK > WANTED_BLOCK) {
        sim_debug(DBG_SEEK, &tape_dev, "Reverse to find lower block numbers\n");
        uptr->ACC = -ACC_REVERSE;
      }
      if ((C & 7) == MTB)
        tape_done(uptr);
    /* Word 6 is a guard. */
    } else if (offset >= 7 && offset < 263) {
      if (CURRENT_BLOCK == WANTED_BLOCK)
        tape_word(uptr, (uint16)block, (uint16)(offset - 7));
    }
    else if (offset == 263 && CURRENT_BLOCK == WANTED_BLOCK)
      /* Checksum here. */
      tape_done(uptr);
  }
  /* Word 264-265 are "C". */
  /* Word 266 is a guard. */
  else if (offset == 267 && uptr->SPEED < 0) {
    /* Reverse block number. */
    CURRENT_BLOCK = (uint16)(block + uptr->OFFSET);
    sim_debug(DBG_SEEK, &tape_dev,
              "Found reverse block number %03o; looking for %03o\n",
              CURRENT_BLOCK, WANTED_BLOCK);
    if (CURRENT_BLOCK <= WANTED_BLOCK) {
      sim_debug(DBG_SEEK, &tape_dev, "Reverse to find higher block numbers\n");
      uptr->ACC = ACC_REVERSE;
      uptr->POS -= MAX_SPEED * BLOCK_WORDS;
    }
    if ((C & 7) == MTB)
      tape_done(uptr);
  }

  return SCPE_OK;
}

static t_stat tape_reset(DEVICE *dptr)
{
  return SCPE_OK;
}

static t_stat tape_boot(int32 unit_num, DEVICE *dptr)
{
  if (unit_num >= 2 && unit_num <= 3)
    return SCPE_ARG;

  //LSW = 0701 + (unit_num << 3);
  //RSW = (7 << 9) | 0300;
  //P = 020;
  return SCPE_OK;
}

static t_stat tape_attach(UNIT *uptr, CONST char *cptr)
{
  t_stat stat;
  t_offset size;
  if (uptr - tape_unit >= 2 && uptr - tape_unit <= 3)
    return SCPE_ARG;
  stat = attach_unit(uptr, cptr);
  if (stat != SCPE_OK)
    return stat;
  size = sim_fsize(uptr->fileref);
  if (size == MAX_BLOCKS * DATA_WORDS * 2)
    uptr->OFFSET = 0; /* Plain image. */
  else if ((size % (2 * DATA_WORDS)) == 6) {
    /* Extended image with additional meta data. */
    uint16 metadata = (uint16)(size / (2 * DATA_WORDS));
    uint16 block_size = read_word(uptr, metadata, 0);
    int16 forward_offset = (int16)read_word(uptr, metadata, 1);
    int16 reverse_offset = (int16)read_word(uptr, metadata, 2);
    sim_debug(DBG, &tape_dev,
              "Extended image with block size %o, block offset %d/%d\r\n",
              block_size, forward_offset, reverse_offset);
    if (block_size != DATA_WORDS)
      return SCPE_FMT;
    if (forward_offset != reverse_offset)
      return SCPE_FMT;
    uptr->OFFSET = forward_offset;
  } else
    return SCPE_FMT;
    
  uptr->POS = -2 * START_POS;
  uptr->SPEED = 0;
  return SCPE_OK;
}

static t_stat tape_detach(UNIT *uptr)
{
  if (uptr - tape_unit >= 2 && uptr - tape_unit <= 3)
    return SCPE_ARG;
  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;
  if (sim_is_active(uptr))
    sim_cancel(uptr);
  return detach_unit(uptr);
}
