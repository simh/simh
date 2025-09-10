/* ka10_dd.c: Data Disc 6600 Television Display System, with
   PDP-10 interface and video switch made at Stanford AI lab.

   Copyright (c) 2022-2023, Lars Brinkhoff

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

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.
*/

#include "kx10_defs.h"
#ifndef NUM_DEVS_DD
#define NUM_DEVS_DD 0
#endif

#if NUM_DEVS_DD > 0
#include "sim_video.h"

#define DD_DEVNUM         0510
#define VDS_DEVNUM        0340

#define DD_WIDTH     512                    /* Display width. */
#define DD_HEIGHT    480                    /* Display height. */
#define DD_PIXELS    (DD_WIDTH * DD_HEIGHT) /* Total number of pixels. */
#define DD_CHANNELS  32                     /* Data Disc channels. */
#define DD_COLUMNS   85
#define FONT_WIDTH   6
#define FONT_HEIGHT  12
#define MARGIN       2
#define VDS_OUTPUTS  64                     /* Video switch outputs. */
#define III_DISPLAYS 6

#define STATUS            u3
#define MA                u4    /* Current memory address. */
#define PIA               u5
#define COLUMN            u6
#define LINE              us9
#define CHANNEL           us10

/* CONI/O Bits */
#define DD_HALT     000000010    /* CONI: Halted. */
#define DD_RESET    000000010    /* CONO: Reset. */
#define DD_INT      000000020    /* CONI: Interrupting. */
#define DD_FORCE    000000020    /* CONO: Force field. */
#define DD_FIELD    000000040    /* CONI: Field. */
#define DD_HALT_ENA 000000100    /* CONI: Halt interrupt enabled. */
#define DD_DDGO     000000100    /* CONO: Go. */
#define DD_LATE     000000200    /* CONI: Late. */
#define DD_SPGO     000000200    /* CONO */
#define DD_LATE_ENA 000000400    /* Late interrupt enabled. */
#define DD_USER     000001000    /* User mode. */
#define DD_NXM      000002000    /* CONI: Accessed non existing memory. */

/* Function codes. */
#define FC_GRAPHICS   001  /* Graphics mode. */
#define FC_WRITE      002  /* Write to disc. */
#define FC_DARK       004  /* Dark background. */
#define FC_DOUBLE_W   010  /* Double width. */
#define FC_ERASE      010  /* Erase graphics. */
#define FC_ADDITIVE   020  /* Additive graphics. */
#define FC_SINGLE_H   040  /* Single height. */

/* There are 64 displays, fed from the video switch. */
static uint32 vds_surface[VDS_OUTPUTS][DD_PIXELS];
static uint32 vds_palette[VDS_OUTPUTS][2];
static VID_DISPLAY *vds_vptr[VDS_OUTPUTS];

/* There are 32 channels on the Data Disc. */
static uint8 dd_channel[DD_CHANNELS][DD_PIXELS];
static uint8 dd_changed[DD_CHANNELS];
static int dd_windows = 1;

static uint8 dd_function_code = 0;
static uint16 dd_line_buffer[DD_COLUMNS + 1];
static int dd_line_buffer_address = 0;
static int dd_line_buffer_written = 0;
#define WRITTEN 0400

uint32 vds_channel;              /* Currently selected video outputs. */
uint8  vds_changed[VDS_OUTPUTS];
uint32 vds_selection[VDS_OUTPUTS];        /* Data Disc channels. */
uint32 vds_sync_inhibit[VDS_OUTPUTS];
uint32 vds_analog[VDS_OUTPUTS];           /* Analog channel. */

#include "ka10_dd_font.h"

static t_stat dd_set_windows (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
static t_stat dd_show_windows (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static void dd_chargen (uint16 c, int column);
static void dd_graphics (uint8 data, int column);
static t_stat dd_devio(uint32 dev, uint64 *data);
static t_stat vds_devio(uint32 dev, uint64 *data);
static t_stat dd_svc(UNIT *uptr);
static t_stat vds_svc(UNIT *uptr);
static t_stat dd_reset(DEVICE *dptr);
static t_stat vds_reset(DEVICE *dptr);
static t_stat dd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat vds_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *dd_description (DEVICE *dptr);
static const char *vds_description (DEVICE *dptr);

DIB dd_dib = { DD_DEVNUM, 1, dd_devio, NULL};

UNIT dd_unit = {
    UDATA (&dd_svc, UNIT_IDLE, 0)
};

MTAB dd_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "WINDOWS", "WINDOWS",
      &dd_set_windows, &dd_show_windows, NULL},
    { 0 }
    };

DEVICE dd_dev = {
    "DD", &dd_unit, NULL, dd_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, dd_reset,
    NULL, NULL, NULL, &dd_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS | DEV_DISPLAY, 0, dev_debug,
    NULL, NULL, &dd_help, NULL, NULL, &dd_description
    };

UNIT vds_unit = {
    UDATA (&vds_svc, UNIT_IDLE, 0)
};

DIB vds_dib = { VDS_DEVNUM, 1, vds_devio, NULL};

DEVICE vds_dev = {
    "VDS", &vds_unit, NULL, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, vds_reset,
    NULL, NULL, NULL, &vds_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS | DEV_DISPLAY, 0, dev_debug,
    NULL, NULL, &vds_help, NULL, NULL, &vds_description
    };

static void
dd_hang (const char *msg)
{
    sim_debug(DEBUG_CMD, &dd_dev, "HANG: %s\n", msg);
    dd_unit.STATUS |= DD_HALT;
}

static void
dd_halt (const char *msg)
{
    sim_debug(DEBUG_CMD, &dd_dev, "HALT: %s\n", msg);
    dd_unit.STATUS |= DD_HALT;
}

static void
dd_execute (const char *msg)
{
    int i;
    sim_debug(DEBUG_CMD, &dd_dev, "%s; %s mode\n",
              msg, (dd_function_code & FC_GRAPHICS) ? "graphics" : "text");
    if (dd_unit.LINE >= DD_HEIGHT)
        return;

    if (dd_function_code & FC_GRAPHICS) {
        for (i = 1; i <= 64; i++) {
            if (dd_line_buffer[i] & WRITTEN)
                dd_graphics (dd_line_buffer[i] & 0377, i - 1);
        }
    } else {
        for (i = 1; i <= DD_COLUMNS; i++) {
            if (dd_line_buffer[i] & WRITTEN)
                dd_chargen (dd_line_buffer[i] & 0177, i - 1);
        }
    }

    memset (dd_line_buffer, 0, sizeof dd_line_buffer);
    dd_line_buffer_address = 1;
    dd_line_buffer_written = 0;
}

static t_stat
dd_devio(uint32 dev, uint64 *data) {
     UNIT *uptr = &dd_unit;
     switch(dev & 3) {
     case CONI:
        *data = uptr->PIA | uptr->STATUS;
        sim_debug (DEBUG_CONI, &dd_dev, "%06llo (%6o)\n", *data, PC);
        break;
     case CONO:
        sim_debug (DEBUG_CONO, &dd_dev, "%06llo (%6o)\n", *data, PC);
        uptr->STATUS &= ~DD_HALT;
        clr_interrupt (DD_DEVNUM);
        uptr->PIA = (uint32)(*data & 7);
        if (*data & DD_RESET) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "Reset.\n");
            uptr->PIA = 0;
            uptr->STATUS = 0;
            uptr->COLUMN = dd_line_buffer_address = 1;
            uptr->LINE = 0;
            dd_function_code = 0;
            sim_cancel (uptr);
        }
        if (*data & DD_FORCE) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "Force field.\n");
        }
        if (*data & 040) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "Halt interrupt enabled.\n");
            uptr->STATUS |= DD_HALT_ENA;
        }
        if (*data & DD_DDGO) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "Go.\n");
        }
        if (*data & DD_SPGO) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "SPGO\n");
        }
        if (*data & DD_LATE_ENA) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "Late interrupt enabled.\n");
            uptr->STATUS |= DD_LATE_ENA;
        }
        if (*data & DD_USER) {
            sim_debug(DEBUG_DETAIL, &dd_dev, "User mode.\n");
            uptr->STATUS |= DD_USER;
        }
        break;
     case DATAI:
        *data = 0;
        sim_debug(DEBUG_DATAIO, &dd_dev, "DATAI (%6o)\n", PC);
        break;
    case DATAO:
        uptr->MA = *data & 0777777;
        sim_debug(DEBUG_DATAIO, &dd_dev, "DATAO %06o (%6o)\n", uptr->MA, PC);
        if (uptr->STATUS & DD_DDGO)
            sim_activate (uptr, 1);
        break;
    }
    return SCPE_OK;
}

static void
dd_pixel (int x, int y, uint8 pixel)
{
    if (x >= DD_WIDTH)
      return;
    if (y >= DD_HEIGHT)
      return;
    pixel &= 1;
    if (!(dd_function_code & FC_DARK))
        pixel ^= 1;
    if (dd_function_code & FC_ADDITIVE)
        dd_channel[dd_unit.CHANNEL][DD_WIDTH * y + x] |= pixel;
    else
        dd_channel[dd_unit.CHANNEL][DD_WIDTH * y + x] = pixel;
    dd_changed[dd_unit.CHANNEL] = 1;
}

static void
dd_chargen (uint16 c, int column)
{
    int i, j;
    uint8 pixels;
    int line = dd_unit.LINE;
    int field = line & 1;

    if (line >= DD_HEIGHT || column >= DD_COLUMNS)
        return;

    sim_debug (DEBUG_DETAIL, &dd_dev, "CHARGEN %03o %d@(%d,%d)\n",
               c, dd_unit.CHANNEL, column, dd_unit.LINE);

    for (i = 0; i < FONT_HEIGHT-1; i += 2, line += 2) {
        pixels = font[c][i + field];
        for (j = 0; j < FONT_WIDTH-1; j++) {
            dd_pixel (6 * column + j, line, pixels >> 4);
            pixels <<= 1;
        }
    }
}

static void
dd_byte (uint8 data)
{
    int max = (dd_function_code & FC_GRAPHICS) ? 64 : DD_COLUMNS;
    if (dd_line_buffer_address <= max) {
        sim_debug(DEBUG_DETAIL, &dd_dev, "Buffer[%d] %03o\n",
                  dd_line_buffer_address, data);
        dd_line_buffer[dd_line_buffer_address] = data | WRITTEN;
    }
    dd_line_buffer_address++;
    dd_line_buffer_address &= 0177;
}

static void
dd_text (uint64 insn)
{
    int rubout = 0;
    char text[6];
    int i;

    text[0] = (insn >> 29) & 0177;
    text[1] = (insn >> 22) & 0177;
    text[2] = (insn >> 15) & 0177;
    text[3] = (insn >>  8) & 0177;
    text[4] = (insn >>  1) & 0177;
    text[5] = 0;

    sim_debug(DEBUG_CMD, &dd_dev, "TEXT \"%s\" to %d@(%d,%d)\n",
              text, dd_unit.CHANNEL, dd_unit.COLUMN, dd_unit.LINE);

    for (i = 0; i < 5; i++) {
        switch (text[i]) {
        case 000:
        case 0177:
            sim_debug (DEBUG_DETAIL, &dd_dev, "CHAR %03o ignored\n", text[i]);
            break;
        case 012:
            if (rubout)
                goto print;
            if (dd_line_buffer_written) {
                sim_debug (DEBUG_DETAIL, &dd_dev, "LF clear rest of line\n");
                while (dd_line_buffer_address <= DD_COLUMNS)
                    dd_byte (040);
                dd_execute ("LF execute");
            }
            dd_unit.LINE += FONT_HEIGHT;
            if (!(dd_function_code & FC_SINGLE_H))
                dd_unit.LINE += FONT_HEIGHT;
            dd_unit.LINE &= 0777;
            sim_debug (DEBUG_DETAIL, &dd_dev, "CHAR 012 LF -> (%d,%d)\n",
                       dd_unit.COLUMN, dd_unit.LINE);
            break;
        case 015:
            if (rubout)
                goto print;
            if (dd_line_buffer_written) {
                sim_debug (DEBUG_DETAIL, &dd_dev, "CR clear rest of line\n");
                while (dd_line_buffer_address <= DD_COLUMNS)
                    dd_byte (040);
                dd_execute ("CR execute");
            }
            dd_unit.COLUMN = dd_line_buffer_address = MARGIN;
            sim_debug (DEBUG_DETAIL, &dd_dev, "CHAR 015 CR -> (%d,%d)\n",
                       dd_unit.COLUMN, dd_unit.LINE);
            break;
        case 010:
        case 011:
            if (!rubout)
                break;
            /* Fall through. */
        default:
        print:
            {
                char ch[2];
                memset (ch, 0, 2);
                if (text[i] > 040 && text[i] < 0177)
                  ch[0] = text[i];
                sim_debug (DEBUG_DETAIL, &dd_dev, "CHAR %03o %s (%d,%d)\n",
                           text[i], ch, dd_line_buffer_address, dd_unit.LINE);
            }
            dd_byte (text[i]);
            dd_line_buffer_written = 1;
            dd_unit.COLUMN++;
            dd_unit.COLUMN &= 0177;
            break;
        }
        rubout = (text[i] == 0177);
    }
}

static void
dd_graphics (uint8 data, int column)
{
    int i;

    sim_debug (DEBUG_CMD, &dd_dev, "GRAPHICS %03o %d@(%d,%d)\n",
               data, dd_unit.CHANNEL, column, dd_unit.LINE);

    column = 8 * column + 4;
    for (i = 0; i < 8; i++) {
        dd_pixel (column, dd_unit.LINE, data >> 7);
        column++;
        data <<= 1;
    }
}

static void
dd_function (uint8 data)
{
    dd_function_code = data;
    sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: function code %03o\n", data);
    if (data & FC_GRAPHICS)
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: graphics mode\n");
    else
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: text mode\n");
    if (data & FC_WRITE)
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: write to disc\n");
    else
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: write to display\n");
    if (data & FC_DARK)
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: dark background\n");
    else
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: light background\n");
    switch (data & (FC_GRAPHICS|FC_DOUBLE_W)) {
    case 000:
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: single width\n");
        break;
    case FC_DOUBLE_W:
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: double width\n");
        break;
    case FC_GRAPHICS|FC_ERASE:
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: erase\n");
        break;
    }
    if (data & FC_ADDITIVE)
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: additive\n");
    else
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: replace\n");
    if (data & FC_SINGLE_H)
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: single height\n");
    else
        sim_debug(DEBUG_DETAIL, &dd_dev, "Function: double height\n");
}

static void
dd_command (uint32 command, uint8 data)
{
    int i;
    switch (command) {
    case 0:
        dd_execute ("COMMAND: execute");
        break;
    case 1:
        dd_function (data);
        break;
    case 2:
        dd_unit.CHANNEL = data & 077;
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: channel select %d\n",
                  dd_unit.CHANNEL);
        if ((dd_function_code & (FC_GRAPHICS|FC_ERASE)) == (FC_GRAPHICS|FC_ERASE)) {
            sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: erase channel %d\n",
                      dd_unit.CHANNEL);
            dd_changed[dd_unit.CHANNEL] = 1;
            for (i = 0; i < DD_PIXELS; i++)
                dd_channel[dd_unit.CHANNEL][i] = 0;
        }
        break;
    case 3:
        dd_unit.COLUMN = dd_line_buffer_address = data & 0177;
        if (dd_unit.COLUMN == 0 || dd_unit.COLUMN > DD_COLUMNS)
            dd_hang ("Text column outside bounds");
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: column select %d\n",
                  dd_unit.COLUMN);
        break;
    case 4:
        dd_unit.LINE = ((data & 037) << 4) | (dd_unit.LINE & 017);
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: high order line address -> %d\n",
                  dd_unit.LINE);
        break;
    case 5:
        dd_unit.LINE = (data & 017) | (dd_unit.LINE & 0760);
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: low order line address -> %d\n",
                  dd_unit.LINE);
        break;
    case 6:
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: write directly %03o (%d,%d)\n",
                  data, dd_unit.COLUMN, dd_unit.LINE);
        dd_unit.COLUMN++;
        dd_unit.COLUMN &= 0177;
        break;
    case 7:
        dd_line_buffer_address = data & 0177;
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: line buffer address %03o\n",
                  dd_line_buffer_address);
        break;
    }
}

static void
dd_decode (uint64 insn)
{
    switch (insn & 077) {
    case 001: case 003: case 005: case 007:
    case 011: case 013: case 015: case 017:
    case 021: case 023: case 025: case 027:
    case 031: case 033: case 035: case 037:
    case 041: case 043: case 045: case 047:
    case 051: case 053: case 055: case 057:
    case 061: case 063: case 065: case 067:
    case 071: case 073: case 075: case 077:
        dd_text (insn);
        break;
    case 002: case 022: case 042: case 062:
        sim_debug(DEBUG_CMD, &dd_dev, "COMMAND: graphics %012llo\n", insn >> 4);
        dd_byte ((insn >> 28) & 0377);
        dd_byte ((insn >> 20) & 0377);
        dd_byte ((insn >> 12) & 0377);
        dd_byte ((insn >>  4) & 0377);
        break;
    case 000: case 040: case 060:
        dd_halt ("halt instruction");
        break;
    case 020:
        dd_unit.MA = (int32)(insn >> 18);
        sim_debug(DEBUG_CMD, &dd_dev, "JUMP %06o\n", dd_unit.MA);
        break;
    case 006: case 016: case 026: case 036:
    case 046: case 056: case 066: case 076:
    case 012: case 032: case 052: case 072:
        sim_debug(DEBUG_CMD, &dd_dev, "NOP\n");
        break;
    case 010: case 030: case 050: case 070:
        sim_debug(DEBUG_CMD, &dd_dev, "(weird command)\n");
    case 004: case 014: case 024: case 034:
    case 044: case 054: case 064: case 074:
        dd_command ((insn >> 9) & 7, (insn >> 28) & 0377);
        dd_command ((insn >> 6) & 7, (insn >> 20) & 0377);
        dd_command ((insn >> 3) & 7, (insn >> 12) & 0377);
        break;
    default:
        sim_debug(DEBUG_CMD, &dd_dev, "(UNDOCUMENTED %012llo)\n", insn);
        break;
    }
}

static t_stat
dd_svc (UNIT *uptr)
{
    if ((t_addr)uptr->MA >= MEMSIZE) {
        uptr->STATUS |= DD_NXM;
        dd_halt ("NXM");
     } else {
        dd_decode (M[uptr->MA++]);
    }
 
    if (uptr->STATUS & DD_HALT) {
        uptr->STATUS |= DD_INT;
        if (uptr->STATUS & DD_HALT_ENA) {
            sim_debug(DEBUG_IRQ, &dd_dev, "Interrupt: halt\n");
            set_interrupt (DD_DEVNUM, uptr->PIA);
        }
    } else
        sim_activate_after (uptr, 100);

    return SCPE_OK;
}

static void
dd_display (int n)
{
    uint32 selection = vds_selection[n];
    int i, j;

    if (selection == 0) {
        sim_debug(DEBUG_DETAIL, &vds_dev, "Output %d displays no channels\n", n);
        return;
    }

#if 1
    if (!(selection & (selection - 1))) {
        for (i = 0; (selection & 020000000000) == 0; i++)
            selection <<= 1;
        if (!dd_changed[i] && !vds_changed[n])
            return;
        sim_debug(DEBUG_DETAIL, &vds_dev, "Output %d from channel %d\n", n, i);
        for (j = 0; j < DD_PIXELS; j++)
            vds_surface[n][j] = vds_palette[n][dd_channel[i][j]];
    } else {
#endif

#if 0
    for (j = 0; j < DD_PIXELS; j++) {
        uint8 pixel = 0;
        for (i = 0; i < DD_CHANNELS; i++, selection <<= 1) {
            if (selection & 020000000000)
                pixel |= dd_channel[i][j];
            vds_surface[n][j] = vds_palette[n][pixel];
        }
    }
#else
    }
#endif

    vid_draw_window (vds_vptr[n], 0, 0, DD_WIDTH, DD_HEIGHT, vds_surface[n]);
    vid_refresh_window (vds_vptr[n]);
    sim_debug (DEBUG_DETAIL, &vds_dev, "Refresh window %p\n", vds_vptr[n]);
}

static t_stat
vds_svc (UNIT *uptr)
{
    int i;
    for (i = III_DISPLAYS; i < dd_windows + III_DISPLAYS; i++)
        dd_display (i);
    for (i = 0; i < DD_CHANNELS; i++)
        dd_changed[i] = 0;
    for (i = 0; i < VDS_OUTPUTS; i++)
        vds_changed[i] = 0;

    sim_activate_after (uptr, 33333);
    return SCPE_OK;
}

uint32 dd_keyboard_line (void *p)
{
    int i;
    VID_DISPLAY *vptr = (VID_DISPLAY *)p;
    sim_debug(DEBUG_DETAIL, &vds_dev, "Key event on window %p\n", vptr);
    for (i = 0; i < VDS_OUTPUTS; i++) {
        if (vptr == vds_vptr[i])
            return i;
    }
    return ~0U;
}

static t_stat
dd_reset (DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS || sim_switches & SWMASK('P')) {
        sim_cancel (&dd_unit);
        memset (dd_channel, 0, sizeof dd_channel);
        memset (dd_changed, 0, sizeof dd_changed);
        return SCPE_OK;
    }
    if (dptr->flags & DEV_DIS)
        set_cmd (0, "VDS DISABLED");
    else
        set_cmd (0, "VDS ENABLED");

    return SCPE_OK;
}

static t_stat dd_set_windows (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value x;
    t_stat r;
    if (cptr == NULL)
        return SCPE_ARG;
    x = get_uint (cptr, 10, 32, &r);
    if (r != SCPE_OK)
        return r;
    dd_windows = (int)x;
    return SCPE_OK;
}

static t_stat dd_show_windows (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf (st, "WINDOWS=%d", dd_windows);
    return SCPE_OK;
}

static t_stat
dd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    return SCPE_OK;
}

static const char *
dd_description (DEVICE *dptr)
{
    return "Data Disc Television Display System";
}

static t_stat
vds_reset (DEVICE *dptr)
{
    t_stat r;
    int i;
    if (dptr->flags & DEV_DIS || sim_switches & SWMASK('P')) {
        for (i = 0; i < VDS_OUTPUTS; i++) {
            if (vds_vptr[i] != NULL)
                vid_close_window (vds_vptr[i]);
        }
        vds_channel = 0;
        memset (vds_vptr, 0, sizeof vds_vptr);
        memset (vds_palette, 0, sizeof vds_palette);
        memset (vds_selection, 0, sizeof vds_selection);
        memset (vds_sync_inhibit, 0, sizeof vds_sync_inhibit);
        memset (vds_analog, 0, sizeof vds_analog);
        sim_cancel (&vds_unit);
        return SCPE_OK;
    }

    for (i = III_DISPLAYS; i < dd_windows + III_DISPLAYS; i++) {
        if (vds_vptr[i] == NULL) {
            char title[40];
            snprintf (title, sizeof title, "Data Disc display %d", i);
            r = vid_open_window (&vds_vptr[i], &dd_dev, title, DD_WIDTH, DD_HEIGHT, 0);
            if (r != SCPE_OK)
                return r;
            fprintf(stderr, "Window %d is %p\r\n", i, vds_vptr[i]);
            vds_palette[i][0] = vid_map_rgb_window (vds_vptr[i], 0x00, 0x00, 0x00);
            vds_palette[i][1] = vid_map_rgb_window (vds_vptr[i], 0x00, 0xFF, 0x30);
        }
    }

    sim_activate (&vds_unit, 1);
    return SCPE_OK;
}

static t_stat
vds_devio(uint32 dev, uint64 *data)
{
    switch(dev & 3) {
    case CONO:
        sim_debug(DEBUG_CONO, &vds_dev, "%012llo (%6o)\n", *data, PC);
        vds_channel = *data & 077;
        break;
    case DATAO:
        sim_debug(DEBUG_DATAIO, &vds_dev, "%012llo (%6o)\n", *data, PC);
        vds_changed[vds_channel] = 1;
        vds_selection[vds_channel] = (uint32)(*data >> 4);
        vds_sync_inhibit[vds_channel] = (*data >> 3) & 1;
        vds_analog[vds_channel] = *data & 7;
        sim_debug(DEBUG_DETAIL, &vds_dev, "Output %d selection %011o\n",
                  vds_channel, vds_selection[vds_channel]);
#if 0
        sim_debug(DEBUG_DETAIL, &vds_dev, "Sync inhibit %d\n",
                  vds_sync_inhibit[vds_channel]);
        sim_debug(DEBUG_DETAIL, &vds_dev, "Analog %d\n",
                  vds_analog[vds_channel]);
#endif
        break;
    }
    return SCPE_OK;
}

static t_stat
vds_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    return SCPE_OK;
}

static const char *
vds_description (DEVICE *dptr)
{
    return "Video Switch";
}
#endif
