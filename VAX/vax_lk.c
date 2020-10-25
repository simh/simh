/* vax_lk.c: DEC Keyboard (LK201)

   Copyright (c) 2013-2017, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   lk           LK201 keyboard

   11-Jun-2013  MB      First version

   Related documents:

        EK-104AA-TM-001 - VCB02 Technical Manual (chapter B.5)
*/

#if !defined(VAX_620)

#include "vax_lk.h"

/* States */

#define LK_IDLE           0
#define LK_RECV           1
#define LK_SEND           2

/* Key group moding */

#define LK_MODE_DOWN      0
#define LK_MODE_AUTODOWN  1
#define LK_MODE_NONE      2
#define LK_MODE_DOWNUP    3

#define LK_BUF_LEN        100

#define LK_SEND_CHAR(c)   lk_put_fifo (&lk_sndf, c)

static const char *lk_modes[] = {"DOWN", "AUTODOWN", "NONE", "DOWNUP"};

static const char *lk_states[] = {"DOWN", "UP", "REPEAT"};

typedef struct {
    int32 head;
    int32 tail;
    int32 count;
    uint8 buf[LK_BUF_LEN];
} LK_FIFO;

/* Scan codes */

typedef struct {
    int8 group;
    uint8 code;
} LK_KEYDATA;

LK_KEYDATA LK_KEY_UNKNOWN    = { 0, LK_UNKNOWN };
LK_KEYDATA LK_KEY_TR_0       = { 1, LK_TR_0 };
LK_KEYDATA LK_KEY_TR_1       = { 1, LK_TR_1 };
LK_KEYDATA LK_KEY_TR_2       = { 1, LK_TR_2 };
LK_KEYDATA LK_KEY_TR_3       = { 1, LK_TR_3 };
LK_KEYDATA LK_KEY_TR_4       = { 1, LK_TR_4 };
LK_KEYDATA LK_KEY_TR_5       = { 1, LK_TR_5 };
LK_KEYDATA LK_KEY_TR_6       = { 1, LK_TR_6 };
LK_KEYDATA LK_KEY_TR_7       = { 1, LK_TR_7 };
LK_KEYDATA LK_KEY_TR_8       = { 1, LK_TR_8 };
LK_KEYDATA LK_KEY_TR_9       = { 1, LK_TR_9 };
LK_KEYDATA LK_KEY_A          = { 1, LK_A };
LK_KEYDATA LK_KEY_B          = { 1, LK_B };
LK_KEYDATA LK_KEY_C          = { 1, LK_C };
LK_KEYDATA LK_KEY_D          = { 1, LK_D };
LK_KEYDATA LK_KEY_E          = { 1, LK_E };
LK_KEYDATA LK_KEY_F          = { 1, LK_F };
LK_KEYDATA LK_KEY_G          = { 1, LK_G };
LK_KEYDATA LK_KEY_H          = { 1, LK_H };
LK_KEYDATA LK_KEY_I          = { 1, LK_I };
LK_KEYDATA LK_KEY_J          = { 1, LK_J };
LK_KEYDATA LK_KEY_K          = { 1, LK_K };
LK_KEYDATA LK_KEY_L          = { 1, LK_L };
LK_KEYDATA LK_KEY_M          = { 1, LK_M };
LK_KEYDATA LK_KEY_N          = { 1, LK_N };
LK_KEYDATA LK_KEY_O          = { 1, LK_O };
LK_KEYDATA LK_KEY_P          = { 1, LK_P };
LK_KEYDATA LK_KEY_Q          = { 1, LK_Q };
LK_KEYDATA LK_KEY_R          = { 1, LK_R };
LK_KEYDATA LK_KEY_S          = { 1, LK_S };
LK_KEYDATA LK_KEY_T          = { 1, LK_T };
LK_KEYDATA LK_KEY_U          = { 1, LK_U };
LK_KEYDATA LK_KEY_V          = { 1, LK_V };
LK_KEYDATA LK_KEY_W          = { 1, LK_W };
LK_KEYDATA LK_KEY_X          = { 1, LK_X };
LK_KEYDATA LK_KEY_Y          = { 1, LK_Y };
LK_KEYDATA LK_KEY_Z          = { 1, LK_Z };
LK_KEYDATA LK_KEY_SPACE      = { 1, LK_SPACE };
LK_KEYDATA LK_KEY_SEMICOLON  = { 1, LK_SEMICOLON };
LK_KEYDATA LK_KEY_PLUS       = { 1, LK_PLUS };
LK_KEYDATA LK_KEY_COMMA      = { 1, LK_COMMA };
LK_KEYDATA LK_KEY_UBAR       = { 1, LK_UBAR };
LK_KEYDATA LK_KEY_PERIOD     = { 1, LK_PERIOD };
LK_KEYDATA LK_KEY_QMARK      = { 1, LK_QMARK };
LK_KEYDATA LK_KEY_QUOTE      = { 1, LK_QUOTE };
LK_KEYDATA LK_KEY_LBRACE     = { 1, LK_LBRACE };
LK_KEYDATA LK_KEY_RBRACE     = { 1, LK_RBRACE };
LK_KEYDATA LK_KEY_VBAR       = { 1, LK_VBAR };
LK_KEYDATA LK_KEY_TILDE      = { 1, LK_TILDE };
LK_KEYDATA LK_KEY_KP_0       = { 2, LK_KP_0 };
LK_KEYDATA LK_KEY_KP_1       = { 2, LK_KP_1 };
LK_KEYDATA LK_KEY_KP_2       = { 2, LK_KP_2 };
LK_KEYDATA LK_KEY_KP_3       = { 2, LK_KP_3 };
LK_KEYDATA LK_KEY_KP_4       = { 2, LK_KP_4 };
LK_KEYDATA LK_KEY_KP_5       = { 2, LK_KP_5 };
LK_KEYDATA LK_KEY_KP_6       = { 2, LK_KP_6 };
LK_KEYDATA LK_KEY_KP_7       = { 2, LK_KP_7 };
LK_KEYDATA LK_KEY_KP_8       = { 2, LK_KP_8 };
LK_KEYDATA LK_KEY_KP_9       = { 2, LK_KP_9 };
LK_KEYDATA LK_KEY_KP_PF1     = { 2, LK_KP_PF1 };
LK_KEYDATA LK_KEY_KP_PF2     = { 2, LK_KP_PF2 };
LK_KEYDATA LK_KEY_KP_PF3     = { 2, LK_KP_PF3 };
LK_KEYDATA LK_KEY_KP_PF4     = { 2, LK_KP_PF4 };
LK_KEYDATA LK_KEY_KP_HYPHEN  = { 2, LK_KP_HYPHEN };
LK_KEYDATA LK_KEY_KP_COMMA   = { 2, LK_KP_COMMA };
LK_KEYDATA LK_KEY_KP_PERIOD  = { 2, LK_KP_PERIOD };
LK_KEYDATA LK_KEY_KP_ENTER   = { 2, LK_KP_ENTER };
LK_KEYDATA LK_KEY_DELETE     = { 3, LK_DELETE };
LK_KEYDATA LK_KEY_TAB        = { 3, LK_TAB };
LK_KEYDATA LK_KEY_RETURN     = { 4, LK_RETURN };
LK_KEYDATA LK_KEY_META       = { 5, LK_META };
LK_KEYDATA LK_KEY_LOCK       = { 5, LK_LOCK };
LK_KEYDATA LK_KEY_SHIFT      = { 6, LK_SHIFT };
LK_KEYDATA LK_KEY_CTRL       = { 6, LK_CTRL };
LK_KEYDATA LK_KEY_LEFT       = { 7, LK_LEFT };
LK_KEYDATA LK_KEY_RIGHT      = { 7, LK_RIGHT };
LK_KEYDATA LK_KEY_UP         = { 8, LK_UP };
LK_KEYDATA LK_KEY_DOWN       = { 8, LK_DOWN };
LK_KEYDATA LK_KEY_REMOVE     = { 9, LK_REMOVE };
LK_KEYDATA LK_KEY_NEXT_SCREEN= { 9, LK_NEXT_SCREEN };
LK_KEYDATA LK_KEY_PREV_SCREEN= { 9, LK_PREV_SCREEN };
LK_KEYDATA LK_KEY_INSERT_HERE= { 9, LK_INSERT_HERE };
LK_KEYDATA LK_KEY_FIND       = { 9, LK_FIND };
LK_KEYDATA LK_KEY_SELECT     = { 9, LK_SELECT };
LK_KEYDATA LK_KEY_F1         = { 10, LK_F1 };
LK_KEYDATA LK_KEY_F2         = { 10, LK_F2 };
LK_KEYDATA LK_KEY_F3         = { 10, LK_F3 };
LK_KEYDATA LK_KEY_F4         = { 10, LK_F4 };
LK_KEYDATA LK_KEY_F5         = { 10, LK_F5 };
LK_KEYDATA LK_KEY_F6         = { 11, LK_F6 };
LK_KEYDATA LK_KEY_F7         = { 11, LK_F7 };
LK_KEYDATA LK_KEY_F8         = { 11, LK_F8 };
LK_KEYDATA LK_KEY_F9         = { 11, LK_F9 };
LK_KEYDATA LK_KEY_F10        = { 11, LK_F10 };
LK_KEYDATA LK_KEY_F11        = { 12, LK_F11 };
LK_KEYDATA LK_KEY_F12        = { 12, LK_F12 };

/* Debugging Bitmaps */

#define DBG_SERIAL      0x0001                          /* serial port data */
#define DBG_CMD         0x0002                          /* commands */

t_bool lk_repeat = TRUE;                                /* autorepeat flag */
t_bool lk_trpti = FALSE;                                /* temp repeat inhibit */
int32 lk_keysdown = 0;                                  /* no of keys held down */
LK_FIFO lk_sndf;                                        /* send FIFO */
LK_FIFO lk_rcvf;                                        /* receive FIFO */
int32 lk_mode[16];                                      /* mode of each key group */

t_stat lk_wr (uint8 c);
t_stat lk_rd (uint8 *c);
t_stat lk_reset (DEVICE *dptr);
void lk_reset_mode (void);
void lk_cmd (void);
const char *lk_description (DEVICE *dptr);
t_stat lk_put_fifo (LK_FIFO *fifo, uint8 data);
t_stat lk_get_fifo (LK_FIFO *fifo, uint8 *data);
void lk_clear_fifo (LK_FIFO *fifo);

/* LK data structures

   lk_dev       LK device descriptor
   lk_unit      LK unit list
   lk_reg       LK register list
   lk_mod       LK modifier list
   lk_debug     LK debug list
*/

DEBTAB lk_debug[] = {
    {"SERIAL", DBG_SERIAL,  "Serial port data"},
    {"CMD",    DBG_CMD,     "Commands"},
    {0}
    };

UNIT lk_unit = { UDATA (NULL, 0, 0) };

REG lk_reg[] = {
    { NULL }
    };

MTAB lk_mod[] = {
    { 0 }
    };

DEVICE lk_dev = {
    "LK", &lk_unit, lk_reg, lk_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &lk_reset,
    NULL, NULL, NULL,
    NULL, DEV_DIS | DEV_DEBUG, 0,
    lk_debug, NULL, NULL, NULL, NULL, NULL,
    &lk_description
    };

/* Incoming data on serial line */

t_stat lk_wr (uint8 c)
{
sim_debug (DBG_SERIAL, &lk_dev, "vax -> lk: %02X\n", c);
if (c == 0)
    return SCPE_OK;
if (lk_put_fifo (&lk_rcvf, c) != SCPE_OK) {             /* too long? */
    lk_clear_fifo (&lk_rcvf);
    LK_SEND_CHAR(LK_INERR);                             /* input error */
    return SCPE_OK;
    }
if (c & 0x80)                                           /* cmd terminator? */
    lk_cmd();                                           /* process cmd */
return SCPE_OK;
}

/* Outgoing data on serial line */

t_stat lk_rd (uint8 *c)
{
t_stat r;

r = lk_get_fifo (&lk_sndf, c);
if (r == SCPE_OK)
    sim_debug (DBG_SERIAL, &lk_dev, "lk -> vax: %02X (%s)\n", *c,
        (lk_sndf.count > 0) ? "more" : "end");
return r;
}

t_stat lk_put_fifo (LK_FIFO *fifo, uint8 data)
{
if (fifo->count < LK_BUF_LEN) {
    fifo->buf[fifo->head++] = data;
    if (fifo->head == LK_BUF_LEN)
        fifo->head = 0;
    fifo->count++;
    return SCPE_OK;
    }
else
    return SCPE_EOF;
}

t_stat lk_get_fifo (LK_FIFO *fifo, uint8 *data)
{
if (fifo->count > 0) {
    *data = fifo->buf[fifo->tail++];
    if (fifo->tail == LK_BUF_LEN)
        fifo->tail = 0;
    fifo->count--;
    return SCPE_OK;
    }
else {
    *data = 0;
    return SCPE_EOF;
    }
}

void lk_clear_fifo (LK_FIFO *fifo)
{
fifo->head = 0;
fifo->tail = 0;
fifo->count = 0;
}

void lk_cmd ()
{
int32 i, group, mode;
uint8 data;

lk_get_fifo (&lk_rcvf, &data);

if (data & 1) {                                         /* peripheral command */
    switch (data) {

        case 0x11:
            sim_debug (DBG_CMD, &lk_dev, "LED on\n");
            break;

        case 0x13:
            sim_debug (DBG_CMD, &lk_dev, "LED off\n");
            break;

        case 0x89:
            sim_debug (DBG_CMD, &lk_dev, "inhibit keyboard transmission\n");
            break;

        case 0x8B:
            sim_debug (DBG_CMD, &lk_dev, "resume keyboard transmission\n");
            lk_clear_fifo (&lk_sndf);
            break;

        case 0x99:
            sim_debug (DBG_CMD, &lk_dev, "disable keyclick\n");
            break;

        case 0x1B:
            sim_debug (DBG_CMD, &lk_dev, "enable keyclick, volume = \n");
            break;

        case 0xB9:
            sim_debug (DBG_CMD, &lk_dev, "disable ctrl keyclick\n");
            break;

        case 0xBB:
            sim_debug (DBG_CMD, &lk_dev, "enable ctrl keyclick\n");
            break;

        case 0x9F:
            sim_debug (DBG_CMD, &lk_dev, "sound keyclick\n");
            break;

        case 0xA1:
            sim_debug (DBG_CMD, &lk_dev, "disable bell\n");
            break;

        case 0x23:
            sim_debug (DBG_CMD, &lk_dev, "enable bell, volume = \n");
            break;

        case 0xA7:
            sim_debug (DBG_CMD, &lk_dev, "sound bell\n");
            vid_beep ();
            break;

        case 0xC1:
            sim_debug (DBG_CMD, &lk_dev, "temporary auto-repeat inhibit\n");
            lk_trpti = TRUE;
            break;

        case 0xE3:
            sim_debug (DBG_CMD, &lk_dev, "enable auto-repeat across keyboard\n");
            lk_repeat = TRUE;
            break;

        case 0xE1:
            sim_debug (DBG_CMD, &lk_dev, "disable auto-repeat across keyboard\n");
            lk_repeat = FALSE;
            break;

        case 0xD9:
            sim_debug (DBG_CMD, &lk_dev, "change all auto-repeat to down only\n");
            for (i = 0; i <= 15; i++) {
                if (lk_mode[i] == LK_MODE_AUTODOWN)
                    lk_mode[i] = LK_MODE_DOWN;
                }
            break;

        case 0xAB:
            sim_debug (DBG_CMD, &lk_dev, "request keyboard ID\n");
            LK_SEND_CHAR (0x01);
            LK_SEND_CHAR (0x00);
            break;

        case 0xFD:
            sim_debug (DBG_CMD, &lk_dev, "jump to power-up\n");
            LK_SEND_CHAR (0x01);
            LK_SEND_CHAR (0x00);
            LK_SEND_CHAR (0x00);
            LK_SEND_CHAR (0x00);
            break;

        case 0xCB:
            sim_debug (DBG_CMD, &lk_dev, "jump to test mode\n");
            break;

        case 0xD3:
            sim_debug (DBG_CMD, &lk_dev, "reinstate defaults\n");
            lk_reset_mode ();
            lk_repeat = TRUE;
            lk_trpti = FALSE;
            LK_SEND_CHAR (LK_MODEACK);                  /* Mode change ACK */
            break;

        default:
            sim_printf ("lk: unknown cmd %02X\n", data);
            break;
            }
    }
else {
    group = (data >> 3) & 0xF;
    if (group < 15) {
        mode = (data >> 1) & 0x3;
        sim_debug (DBG_CMD, &lk_dev, "set group %d, mode = %s\n", group, lk_modes[mode]);
        lk_mode[group] = mode;
        LK_SEND_CHAR (LK_MODEACK);                      /* Mode change ACK */
        }
    else
        sim_debug (DBG_CMD, &lk_dev, "set auto-repeat timing\n");
    }
lk_clear_fifo (&lk_rcvf);
}

LK_KEYDATA lk_map_key (int key)
{
LK_KEYDATA lk_key;

switch (key) {

    case SIM_KEY_F1:
        lk_key = LK_KEY_F1;
        break;

    case SIM_KEY_F2:
        lk_key = LK_KEY_F2;
        break;

    case SIM_KEY_F3:
        lk_key = LK_KEY_F3;
        break;

    case SIM_KEY_F4:
        lk_key = LK_KEY_F4;
        break;

    case SIM_KEY_F5:
        lk_key = LK_KEY_F5;
        break;

    case SIM_KEY_F6:
        lk_key = LK_KEY_F6;
        break;

    case SIM_KEY_F7:
        lk_key = LK_KEY_F7;
        break;

    case SIM_KEY_F8:
        lk_key = LK_KEY_F8;
        break;

    case SIM_KEY_F9:
        lk_key = LK_KEY_F9;
        break;

    case SIM_KEY_F10:
        lk_key = LK_KEY_F10;
        break;

    case SIM_KEY_F11:
        lk_key = LK_KEY_F11;
        break;

    case SIM_KEY_F12:
        lk_key = LK_KEY_F12;
        break;

    case SIM_KEY_0:
        lk_key = LK_KEY_TR_0;
        break;

    case SIM_KEY_1:
        lk_key = LK_KEY_TR_1;
        break;

    case SIM_KEY_2:
        lk_key = LK_KEY_TR_2;
        break;

    case SIM_KEY_3:
        lk_key = LK_KEY_TR_3;
        break;

    case SIM_KEY_4:
        lk_key = LK_KEY_TR_4;
        break;

    case SIM_KEY_5:
        lk_key = LK_KEY_TR_5;
        break;

    case SIM_KEY_6:
        lk_key = LK_KEY_TR_6;
        break;

    case SIM_KEY_7:
        lk_key = LK_KEY_TR_7;
        break;

    case SIM_KEY_8:
        lk_key = LK_KEY_TR_8;
        break;

    case SIM_KEY_9:
        lk_key = LK_KEY_TR_9;
        break;

    case SIM_KEY_A:
        lk_key = LK_KEY_A;
        break;

    case SIM_KEY_B:
        lk_key = LK_KEY_B;
        break;

    case SIM_KEY_C:
        lk_key = LK_KEY_C;
        break;

    case SIM_KEY_D:
        lk_key = LK_KEY_D;
        break;

    case SIM_KEY_E:
        lk_key = LK_KEY_E;
        break;

    case SIM_KEY_F:
        lk_key = LK_KEY_F;
        break;

    case SIM_KEY_G:
        lk_key = LK_KEY_G;
        break;

    case SIM_KEY_H:
        lk_key = LK_KEY_H;
        break;

    case SIM_KEY_I:
        lk_key = LK_KEY_I;
        break;

    case SIM_KEY_J:
        lk_key = LK_KEY_J;
        break;

    case SIM_KEY_K:
        lk_key = LK_KEY_K;
        break;

    case SIM_KEY_L:
        lk_key = LK_KEY_L;
        break;

    case SIM_KEY_M:
        lk_key = LK_KEY_M;
        break;

    case SIM_KEY_N:
        lk_key = LK_KEY_N;
        break;

    case SIM_KEY_O:
        lk_key = LK_KEY_O;
        break;

    case SIM_KEY_P:
        lk_key = LK_KEY_P;
        break;

    case SIM_KEY_Q:
        lk_key = LK_KEY_Q;
        break;

    case SIM_KEY_R:
        lk_key = LK_KEY_R;
        break;

    case SIM_KEY_S:
        lk_key = LK_KEY_S;
        break;

    case SIM_KEY_T:
        lk_key = LK_KEY_T;
        break;

    case SIM_KEY_U:
        lk_key = LK_KEY_U;
        break;

    case SIM_KEY_V:
        lk_key = LK_KEY_V;
        break;

    case SIM_KEY_W:
        lk_key = LK_KEY_W;
        break;

    case SIM_KEY_X:
        lk_key = LK_KEY_X;
        break;

    case SIM_KEY_Y:
        lk_key = LK_KEY_Y;
        break;

    case SIM_KEY_Z:
        lk_key = LK_KEY_Z;
        break;

    case SIM_KEY_BACKQUOTE:
        lk_key = LK_KEY_TILDE;
        break;

    case SIM_KEY_MINUS:
        lk_key = LK_KEY_UBAR;
        break;

    case SIM_KEY_EQUALS:
        lk_key = LK_KEY_PLUS;
        break;

    case SIM_KEY_LEFT_BRACKET:
        lk_key = LK_KEY_LBRACE;
        break;

    case SIM_KEY_RIGHT_BRACKET:
        lk_key = LK_KEY_RBRACE;
        break;

    case SIM_KEY_SEMICOLON:
        lk_key = LK_KEY_SEMICOLON;
        break;

    case SIM_KEY_SINGLE_QUOTE:
        lk_key = LK_KEY_QUOTE;
        break;

    case SIM_KEY_BACKSLASH:
        lk_key = LK_KEY_VBAR;
        break;

    case SIM_KEY_LEFT_BACKSLASH:
    case SIM_KEY_COMMA:
        lk_key = LK_KEY_COMMA;
        break;

    case SIM_KEY_PERIOD:
        lk_key = LK_KEY_PERIOD;
        break;

    case SIM_KEY_SLASH:
        lk_key = LK_KEY_QMARK;
        break;

    /* case SIM_KEY_PRINT: */
    /* case SIM_KEY_PAUSE: */
    /* case SIM_KEY_ESC: */

    case SIM_KEY_BACKSPACE:
        lk_key = LK_KEY_DELETE;
        break;

    case SIM_KEY_TAB:
        lk_key = LK_KEY_TAB;
        break;

    case SIM_KEY_ENTER:
        lk_key = LK_KEY_RETURN;
        break;

    case SIM_KEY_SPACE:
        lk_key = LK_KEY_SPACE;
        break;

    case SIM_KEY_INSERT:
        lk_key = LK_KEY_FIND;
        break;

    case SIM_KEY_DELETE:
        lk_key = LK_KEY_SELECT;
        break;

    case SIM_KEY_HOME:
        lk_key = LK_KEY_INSERT_HERE;
        break;

    case SIM_KEY_END:
        lk_key = LK_KEY_PREV_SCREEN;
        break;

    case SIM_KEY_PAGE_UP:
        lk_key = LK_KEY_REMOVE;
        break;

    case SIM_KEY_PAGE_DOWN:
        lk_key = LK_KEY_NEXT_SCREEN;
        break;

    case SIM_KEY_UP:
        lk_key = LK_KEY_UP;
        break;

    case SIM_KEY_DOWN:
        lk_key = LK_KEY_DOWN;
        break;

    case SIM_KEY_LEFT:
        lk_key = LK_KEY_LEFT;
        break;

    case SIM_KEY_RIGHT:
        lk_key = LK_KEY_RIGHT;
        break;

    case SIM_KEY_CAPS_LOCK:
        lk_key = LK_KEY_LOCK;
        break;

    case SIM_KEY_NUM_LOCK:
        lk_key = LK_KEY_KP_PF1;
        break;

    case SIM_KEY_SCRL_LOCK:

    case SIM_KEY_ALT_L:
    case SIM_KEY_ALT_R:
        lk_key = LK_KEY_META;
        break;

    case SIM_KEY_CTRL_L:
    case SIM_KEY_CTRL_R:
        lk_key = LK_KEY_CTRL;
        break;

    case SIM_KEY_SHIFT_L:
    case SIM_KEY_SHIFT_R:
        lk_key = LK_KEY_SHIFT;
        break;

    case SIM_KEY_WIN_L:
    case SIM_KEY_WIN_R:
    case SIM_KEY_MENU:
        lk_key = LK_KEY_UNKNOWN;
        break;

    case SIM_KEY_KP_ADD:
    case SIM_KEY_KP_SUBTRACT:
    case SIM_KEY_KP_END:
    case SIM_KEY_KP_DOWN:
    case SIM_KEY_KP_PAGE_DOWN:
    case SIM_KEY_KP_LEFT:
    case SIM_KEY_KP_RIGHT:
    case SIM_KEY_KP_HOME:
    case SIM_KEY_KP_UP:
    case SIM_KEY_KP_PAGE_UP:
    case SIM_KEY_KP_INSERT:
    case SIM_KEY_KP_DELETE:
    case SIM_KEY_KP_5:
    case SIM_KEY_KP_ENTER:
    case SIM_KEY_KP_MULTIPLY:
    case SIM_KEY_KP_DIVIDE:

    case SIM_KEY_UNKNOWN:
        lk_key = LK_KEY_UNKNOWN;
        break;

    default:
        lk_key = LK_KEY_UNKNOWN;
        break;
    }
return lk_key;
}

void lk_reset_mode (void)
{
lk_mode[1]  = LK_MODE_AUTODOWN;                         /* 1  = 48 graphic keys, spacebar */
lk_mode[2]  = LK_MODE_AUTODOWN;                         /* 2  = numeric keypad */
lk_mode[3]  = LK_MODE_AUTODOWN;                         /* 3  = delete character */
lk_mode[4]  = LK_MODE_DOWN;                             /* 4  = return, tab */
lk_mode[5]  = LK_MODE_DOWN;                             /* 5  = lock, compose */
lk_mode[6]  = LK_MODE_DOWNUP;                           /* 6  = shift, ctrl */
lk_mode[7]  = LK_MODE_AUTODOWN;                         /* 7  = horizontal cursors */
lk_mode[8]  = LK_MODE_AUTODOWN;                         /* 8  = vertical cursors */
lk_mode[9]  = LK_MODE_DOWNUP;                           /* 9  = six basic editing keys */
lk_mode[10] = LK_MODE_AUTODOWN;                         /* 10 = function keys: f1 - f5 */
lk_mode[11] = LK_MODE_AUTODOWN;                         /* 11 = function keys: f6 - f10 */
lk_mode[12] = LK_MODE_AUTODOWN;                         /* 12 = function keys: f11 - f14 */
lk_mode[13] = LK_MODE_AUTODOWN;                         /* 13 = function keys: help, do */
lk_mode[14] = LK_MODE_AUTODOWN;                         /* 14 = function keys: f17 - f20 */
}

t_stat lk_reset (DEVICE *dptr)
{
lk_clear_fifo (&lk_sndf);
lk_clear_fifo (&lk_rcvf);
lk_keysdown = 0;
lk_repeat = TRUE;
lk_trpti = FALSE;
lk_reset_mode ();
return SCPE_OK;
}

void lk_event (SIM_KEY_EVENT *ev)
{
LK_KEYDATA lk_key;
int32 mode;

lk_key = lk_map_key (ev->key);
mode  = lk_mode[lk_key.group];

sim_debug (DBG_SERIAL, &lk_dev, "lk_poll() Event - Key: (group=%d, code=%02X), Mode: %s - auto-repeat inhibit: %s - state: %s\n", lk_key.group, lk_key.code, lk_modes[mode], lk_trpti ? "TRUE" : "FALSE", lk_states[ev->state]);

if (lk_trpti && (ev->state != SIM_KEYPRESS_REPEAT))
    lk_trpti = FALSE;

switch (mode) {

    case LK_MODE_DOWN:
        if (ev->state == SIM_KEYPRESS_DOWN) {
            LK_SEND_CHAR (lk_key.code);
            }
        break;

    case LK_MODE_AUTODOWN:
        if (ev->state == SIM_KEYPRESS_DOWN) {
            LK_SEND_CHAR (lk_key.code);
            }
        else if ((ev->state == SIM_KEYPRESS_REPEAT) && lk_repeat && !lk_trpti) {
            LK_SEND_CHAR (LK_METRONOME);
            }
        break;

    case LK_MODE_DOWNUP:
        if (ev->state == SIM_KEYPRESS_DOWN) {
            lk_keysdown++;
            LK_SEND_CHAR (lk_key.code);
            }
        else if (ev->state == SIM_KEYPRESS_UP) {
            lk_keysdown--;
            if (lk_keysdown > 0) {
                LK_SEND_CHAR (lk_key.code);
                }
            else {
                LK_SEND_CHAR (LK_ALLUP);
                }
            }
        break;
    }
}

const char *lk_description (DEVICE *dptr)
{
return "  VCB01 - LK Keyboard interface";
}

#else /* defined(VAX_620) */
static const char *dummy_declaration = "Something to compile";
#endif /* !defined(VAX_620) */
