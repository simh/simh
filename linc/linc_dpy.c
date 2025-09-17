#include "linc_defs.h"

/* Debug */
#define DBG             0001

static DEBTAB dpy_deb[] = {
  { "DBG",  DBG },
  { NULL, 0 }
};

DEVICE dpy_dev = {
  "DPY", NULL, NULL, NULL,
  0, 8, 12, 1, 8, 12,
  NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, DEV_DEBUG, 0, dpy_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

void dpy_dis(uint16 h, uint16 x, uint16 y)
{
  sim_debug(DBG, &dpy_dev, "DIS %u;%03o, A=%03o\n", h, x, y);
  /* Y coordinate +0 and -0 both refer to the same vertical position. */
  if (y < 256)
    y += 255;
  else
    y -= 256;
  crt_point(x, y);
}

/* Called from display library to get data switches. */
void
cpu_get_switches (unsigned long *p1, unsigned long *p2)
{
}

/* Called from display library to set data switches. */
void
cpu_set_switches (unsigned long p1, unsigned long p2)
{
}
