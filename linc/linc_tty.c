#include "linc_defs.h"


/* Data bits, 110 baud rate. */
#define BIT_TIME       1120
/* Sample rate to find the start bit edge. */
#define START_TIME     (BIT_TIME / 5)
/* After finding the edge, wait until the middle of the first data bit. */
#define FIRST_TIME     (BIT_TIME + (BIT_TIME - START_TIME) / 2)

#define R (*(uint16 *)cpu_reg[5].loc)

/* Debug */
#define DBG             0001
#define DBG_BIT         0002

#define DATA      u3    /* Character being assembled. */
#define STATE     u4    /* 0 for start bit, 1 for stop bit, otherwise data. */
#define PREVIOUS  u5    /* Previous level seen. */

/* When a start bit is found, the state is set to 10 and then
   decremented for each bit that is processed. */
#define STATE_START   0
#define STATE_STOP    1
/*      STATE_DATA    2-9 */
#define STATE_FIRST   10

/* Function declaration. */
static t_stat tty_svc(UNIT *uptr);
static t_stat tty_attach(UNIT *uptr, CONST char *cptr);
static t_stat tty_detach(UNIT *uptr);

static UNIT tty_unit = {
  UDATA(&tty_svc, UNIT_IDLE | UNIT_ATTABLE, 0)
};

static DEBTAB tty_deb[] = {
  { "DBG",  DBG },
  { "BIT",  DBG_BIT },
  { NULL, 0 }
};

DEVICE tty_dev = {
  "TTY", &tty_unit, NULL, NULL,
  1, 8, 12, 1, 8, 12,
  NULL, NULL, NULL,
  NULL, &tty_attach, &tty_detach,
  NULL, DEV_DISABLE | DEV_DEBUG, 0, tty_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static void tty_output(UNIT *uptr)
{
  uint8 ch = uptr->DATA;
  sim_debug(DBG, &tty_dev, "Character %03o '%c'\n", ch, ch & 0177);
  fputc(ch & 0177, uptr->fileref);
  fflush(uptr->fileref);
}

static t_stat tty_svc(UNIT *uptr)
{
  switch (uptr->STATE) {
  case STATE_START:
    if (uptr->PREVIOUS == 0 || (R & 1) == 1) {
      /* Keep looking for start bit. */
      uptr->PREVIOUS = R & 1;
      sim_activate(uptr, START_TIME);
      return SCPE_OK;
    }

    sim_debug(DBG_BIT, &tty_dev, "Start bit edge found.\n");
    uptr->STATE = STATE_FIRST;
    uptr->DATA = 0;
    /* Wait until the middle of the first data bit.  Since the edge
       was just seen, this is a little longer than the time between
       data bits. */
    sim_activate(uptr, FIRST_TIME);
    break;

  default:
    sim_debug(DBG_BIT, &tty_dev, "Data bit %d is %d\n",
              STATE_FIRST - 1 - uptr->STATE, R & 1);
    uptr->DATA >>= 1;
    uptr->DATA |= (R & 1) << 7;
    sim_activate(uptr, BIT_TIME);
    break;

  case STATE_STOP:
    sim_debug(DBG_BIT, &tty_dev, "Stop bit is %d\n", R & 1);
    if (R & 1)
      tty_output(uptr);
    else
      sim_debug(DBG, &tty_dev, "Framing error.\n");
    uptr->PREVIOUS = R & 1;
    /* Look for next start bit. */
    sim_activate(uptr, START_TIME);
    break;
  }

  /* Decrease the state counter, first through the data bits, then
     the stop bit, and finally the start bit. */
  uptr->STATE--;
  return SCPE_OK;
}

static t_stat tty_attach(UNIT *uptr, CONST char *cptr)
{
  t_stat stat = attach_unit(uptr, cptr);
  if (stat != SCPE_OK)
    return stat;
  uptr->STATE = 0;
  uptr->PREVIOUS = 0;
  sim_activate(uptr, 1);
  return SCPE_OK;
}

static t_stat tty_detach(UNIT *uptr)
{
  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;
  if (sim_is_active(uptr))
    sim_cancel(uptr);
  return detach_unit(uptr);
}
