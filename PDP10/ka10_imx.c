/* ka10_imx.c: Input multplexor for A/D.

   Copyright (c) 2018,2020, Lars Brinkhoff

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

   This is a device which has 128 A/D channels.  It's specific to the MIT
   AI lab PDP-10.
*/

#include "kx10_defs.h"
#include "sim_video.h"

#ifndef NUM_DEVS_IMX
#define NUM_DEVS_IMX 0
#endif

#if (NUM_DEVS_IMX > 0)

#define IMX_DEVNUM      0574

#define IMX_PIA         0000007
#define IMX_DONE        0000010
#define IMX_PACK        0000040
#define IMX_SEQUENCE    0000100
#define IMX_TEST        0000200
#define IMX_RATE        0377000
#define IMX_ASSIGNED    0400000000000LL

#define IMX_CONO        (IMX_PIA | IMX_PACK | IMX_SEQUENCE | IMX_RATE)
#define IMX_CONI        (IMX_PIA | IMX_DONE | IMX_PACK | IMX_SEQUENCE | IMX_TEST | IMX_ASSIGNED)

#define IMX_CHANNEL     0000177

#define JOY_MAX_UNITS     4
#define JOY_MAX_AXES      4
#define JOY_NO_CHAN       (IMX_CHANNEL + 1)

t_stat      imx_devio(uint32 dev, uint64 *data);
t_stat      imx_svc (UNIT *uptr);
t_stat      imx_reset (DEVICE *dptr);
const char *imx_description (DEVICE *dptr);
#if MPX_DEV
t_stat      imx_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
t_stat      imx_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#endif
t_stat      imx_show_channel (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat      imx_set_channel (UNIT* uptr, int32 val, CONST char* cptr, void* desc);

static uint64 status = IMX_ASSIGNED;
static uint64 imx_data;
static uint64 imx_samples;
static int initial_channel = 0;
static int current_channel = 0;
static int imx_mpx_lvl;

static int imx_inputs[0200];
static int imx_map[JOY_MAX_UNITS][JOY_MAX_AXES];

UNIT                imx_unit[] = {
    { UDATA (&imx_svc, UNIT_IDLE, 0) }
};
DIB imx_dib = {IMX_DEVNUM, 1, &imx_devio, NULL};

MTAB imx_mod[] = {
#if MPX_DEV
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MPX", "MPX",
     &imx_set_mpx, &imx_show_mpx, NULL},
#endif
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "CHANNEL", "CHANNEL",
     &imx_set_channel, &imx_show_channel, NULL},
    { 0 }
    };

DEVICE              imx_dev = {
    "IMX", imx_unit, NULL, imx_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, imx_reset, NULL, NULL, NULL,
    &imx_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &imx_description
};

static void imx_joy_motion(int which, int axis, int value)
{
  int chan;

  if (which < JOY_MAX_UNITS && axis < JOY_MAX_AXES) {
    chan = imx_map[which][axis];
    if (chan == JOY_NO_CHAN)
      return;

    value += 32768;

    if (chan < 0) {
      chan = -chan;
      value = 65535 - value;
    }
    imx_inputs[chan] = value >> 5;
    sim_debug(DEBUG_DETAIL, &imx_dev, "Channel %d value %o\n",
              chan, imx_inputs[chan]);
  }
}

static int imx_sample (void)
{
    int sample = imx_inputs[current_channel];
    if (status & IMX_SEQUENCE)
        current_channel = (current_channel + 1) & IMX_CHANNEL;
    else
        current_channel = initial_channel;
    return sample;
}

static void imx_activate (void)
{
    int micros;

    if (status & IMX_DONE) {
        sim_cancel (imx_unit);
        sim_debug(DEBUG_IRQ, &imx_dev, "Cancel\n");
        return;
    }

    micros = (status >> 16) & 0377;
    if (micros < 10)
        micros = 10;
    sim_activate_after (imx_unit, micros);
    sim_debug(DEBUG_IRQ, &imx_dev, "Activate\n");
}

t_stat imx_reset (DEVICE *dptr)
{
    static int init = 1;
    int i, j;

    if (dptr->flags & DEV_DIS) {
        imx_samples = 0;
        imx_data = 0;

        for (i = 0; i <= IMX_CHANNEL; i++)
            imx_inputs[i] = 1000;

        for (i = 0; i < JOY_MAX_UNITS; i++) {
            for (j = 0; j < JOY_MAX_UNITS; j++)
                imx_map[i][j] = JOY_NO_CHAN;
      }
    } else {
        if (init) {
          vid_register_gamepad_motion_callback (imx_joy_motion);
          init = 0;
        }
    } 
    return SCPE_OK;
}

t_stat imx_devio(uint32 dev, uint64 *data)
{
    switch(dev & 07) {
    case CONO|4:
        sim_debug(DEBUG_CONO, &imx_dev, "%06llo\n", *data);
        status &= ~(IMX_CONO|IMX_DONE);
        status |= *data & IMX_CONO;
        imx_data = 0;
        imx_samples = 0;
        current_channel = initial_channel;
        clr_interrupt (IMX_DEVNUM);
        imx_activate ();
        break;
    case CONI|4:
        *data = status & IMX_CONI;
        sim_debug(DEBUG_CONI, &imx_dev, "%012llo\n", *data);
        break;
    case DATAO|4:
        sim_debug(DEBUG_DATAIO, &imx_dev, "DATAO %012llo\n", *data);
        initial_channel = *data & IMX_CHANNEL;
        break;
    case DATAI|4:
        *data = imx_data;
        sim_debug(DEBUG_DATAIO, &imx_dev, "DATAI %012llo\n", *data);
        imx_data = 0;
        imx_samples = 0;
        status &= ~IMX_DONE;
        clr_interrupt (IMX_DEVNUM);
        sim_debug(DEBUG_IRQ, &imx_dev, "Clear interrupt\n");
        imx_activate ();
        break;
    }

    return SCPE_OK;
}

t_stat imx_svc (UNIT *uptr)
{
    uint64 max_samples;

    if (status & IMX_PACK) {
        max_samples = 3LL;
    } else {
        max_samples = 1LL;
    }

    if (imx_samples < max_samples) {
        imx_data <<= 12;
        imx_data |= imx_sample();
        imx_samples++;
    }

    if (imx_samples == max_samples) {
        status |= IMX_DONE;
        if (status & 7) {
            set_interrupt_mpx (IMX_DEVNUM, status & 7, imx_mpx_lvl);
            sim_debug(DEBUG_IRQ, &imx_dev, "Raise interrupt\n");
        }
    }

    imx_activate ();

    return SCPE_OK;
}

#if MPX_DEV
/* set MPX level number */
t_stat imx_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mpx;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    mpx = (int32) get_uint (cptr, 8, 8, &r);
    if (r != SCPE_OK)
        return r;
    imx_mpx_lvl = mpx;
    return SCPE_OK;
}

t_stat imx_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "MPX=%o", imx_mpx_lvl);
   return SCPE_OK;
}
#endif

t_stat imx_set_channel (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  int chan, unit, axis, negate = 0;
  char gbuf[CBUFSIZE];
  CONST char *tptr;
  t_stat r;

  if (cptr == NULL || *cptr == 0)
    return SCPE_ARG;

  tptr = get_glyph (cptr, gbuf, ';');
  if (tptr == NULL)
    return SCPE_ARG;
  chan = (int) get_uint (gbuf, 8, IMX_CHANNEL, &r);
  if (r != SCPE_OK)
    return r;
  
  tptr = get_glyph (tptr, gbuf, ';');
  if (tptr == NULL || strncasecmp (gbuf, "unit", 4) != 0)
    return SCPE_ARG;
  unit = (int) get_uint (gbuf + 4, 10, JOY_MAX_UNITS - 1, &r);
  if (r != SCPE_OK)
    return r;
  
  tptr = get_glyph (tptr, gbuf, ';');
  if (strncasecmp (gbuf, "axis", 4) != 0)
    return SCPE_ARG;
  axis = (int) get_uint (gbuf + 4, 10, JOY_MAX_AXES - 1, &r);
  if (r != SCPE_OK)
    return r;

  if (*tptr != 0) {
    if (strcasecmp (tptr, "negate") != 0)
      return SCPE_ARG;
    negate = 1;
  }

  if (negate)
    chan = -chan;
  imx_map[unit][axis] = chan;

  return SCPE_OK;
}

t_stat imx_show_channel (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  int nothing = 1;
  const char *negate, *comma = "";
  int chan, i, j;

  if (uptr == NULL)
    return SCPE_IERR;

  for (i = 0; i < JOY_MAX_UNITS; i++) {
    for (j = 0; j < JOY_MAX_AXES; j++) {
      chan = imx_map[i][j];
      if (chan != JOY_NO_CHAN) {
        if (chan < 0) {
          chan = -chan;
          negate = ";NEGATE";
        } else
          negate = "";

        fprintf (st, "%sCHANNEL=%o;JOY%d;AXIS%d%s", comma, chan, i, j, negate);
        comma = ", ";
        nothing = 0;
      }
    }
  }

  if (nothing)
    fprintf (st, "CHANNEL=(NO MAPPINGS)");

  return SCPE_OK;
}

const char *imx_description (DEVICE *dptr)
{
    return "A/D input multiplexor";
}
#endif
