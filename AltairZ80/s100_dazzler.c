/* s100_dazzler.c: Cromemco DAZZLER and JS-1 Joystick

   Copyright (c) 2024, Patrick Linstruth

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

   ----

   This device simulates the Cromemco Dazzler and D+7A with JS-1 Joystick
   Console.
*/

#include "altairz80_defs.h"
#include "sim_video.h"

#define DAZ_PIXELS      (128 * 128)    /* total number of pixels */

#define DAZ_IO_BASE     0x0e
#define DAZ_IO_SIZE     2
#define DAZ_MEM_SIZE    2048
#define DAZ_MEM_MASK    (2048 - 1)

#define DAZ_ON          0x80   /* On/Off */
#define DAZ_RESX4       0x40   /* Resolution X 4 */
#define DAZ_2K          0x20   /* Picture in 2K bytes of memory */
#define DAZ_COLOR       0x10   /* Picture in 2K bytes of memory */
#define DAZ_HIGH        0x08   /* High intensity color */
#define DAZ_BLUE        0x04   /* Blue */
#define DAZ_GREEN       0x02   /* Green */
#define DAZ_RED         0x01   /* Red */
#define DAZ_EOF         0x40   /* End of Frame */
#define DAZ_EVEN        0x80   /* Even Line */

#define JS1_IO_BASE     0x18
#define JS1_IO_SIZE     8

#define JS1_NUM_STICKS   2
#define JS1_NUM_BUTTONS  4

/*
** Public VID_DISPLAY for other devices that may want
** to access the video display directly, such as joystick
** events.
*/
VID_DISPLAY *daz_vptr = NULL;

static t_bool daz_0e = 0x00;
static t_bool daz_0f = 0x80;
static uint32 daz_addr = 0x0000;
static t_bool daz_frame = 0x3f;
static uint8 daz_res = 32;
static uint16 daz_pages = 1;
static uint16 daz_window_width = 640;
static uint16 daz_window_height = 640;
static uint16 daz_screen_width = 32;
static uint16 daz_screen_height = 32;
static uint16 daz_screen_pixels = 32 * 32;
static uint8 daz_color = 0;
static uint32 daz_surface[DAZ_PIXELS];
static uint32 daz_cpalette[16];
static uint32 daz_gpalette[16];

static uint8 js1_buttons[JS1_NUM_STICKS] = {0x0f, 0x0f};
static uint8 js1_joyx[JS1_NUM_STICKS];
static uint8 js1_joyy[JS1_NUM_STICKS];

#define DAZ_SHOW_VIDEO(b) (b & DAZ_ON) ? "ON" : "OFF"
#define DAZ_SHOW_RES(b) (b & DAZ_RESX4) ? "X4" : "NORMAL"
#define DAZ_SHOW_MEMSIZE(b) (b & DAZ_2K) ? "2K" : "512"
#define DAZ_SHOW_COLOR(b) (b & DAZ_COLOR) ? "COLOR" : "B/W"

extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat exdep_cmd(int32 flag, CONST char *cptr);
extern uint8 GetBYTEWrapper(const uint32 Addr);

static const char *daz_description(DEVICE *dptr);
static t_stat daz_svc(UNIT *uptr);
static t_stat daz_reset(DEVICE *dptr);
static t_stat daz_boot(int32 unitno, DEVICE *dptr);
static void daz_set_0f(uint8 val);
static t_stat daz_set_video(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat daz_show_video(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat daz_set_resolution(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat daz_show_resolution(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat daz_set_memsize(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat daz_show_memsize(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat daz_set_color(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat daz_show_color(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static int32 daz_io(const int32 port, const int32 io, const int32 data);
static t_stat daz_open_video(void);
static t_stat daz_close_video(void);
static void daz_resize_video(void);
static void daz_refresh(void);
static void daz_render_normal(void);
static void daz_render_x4(void);
static int32 daz_quad_surfacex(int q);
static int32 daz_quad_surfacey(int q);

static const char *js1_description(DEVICE *dptr);
static t_stat js1_svc(UNIT *uptr);
static t_stat js1_reset(DEVICE *dptr);
static int32 js1_io(const int32 port, const int32 io, const int32 data);
static void js1_joy_motion (int device, int axis, int value);
static void js1_joy_button (int device, int button, int state);

/* Debug flags */
#define VERBOSE_MSG         (1 << 0)

/*
   DAZZLER data structures

   daz_dev      DAZ device descriptor
   daz_unit     DAZ unit descriptor
   daz_reg      DAZ register list
*/

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
} DAZ_CTX;

static DAZ_CTX daz_ctx = {{1, 0, DAZ_IO_BASE, DAZ_IO_SIZE}};

UNIT daz_unit = {
    UDATA (&daz_svc, 0, 0), 33000 /* 30 fps */
};

REG daz_reg[] = {
    { NULL }
};

DEBTAB daz_debug[] = {
    { "VERBOSE",   VERBOSE_MSG,           "Verbose messages" },
    { "JOYSTICK",  SIM_VID_DBG_JOYSTICK,  "Joystick messages" },
    { "VIDEO",     SIM_VID_DBG_VIDEO,     "Video messages" },
    { 0 }
};

MTAB daz_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "VIDEO",  "VIDEO",
        &daz_set_video, &daz_show_video, NULL, "DAZZLER Video [ ON | OFF ]"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "DAZZLER Base I/O Address"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "MEMSIZE",  "MEMSIZE",
        &daz_set_memsize, &daz_show_memsize, NULL, "DAZZLER Memory Size [ 512 | 2K ]"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "RESOLUTION",  "RESOLUTION",
        &daz_set_resolution, &daz_show_resolution, NULL, "DAZZLER Resolution [ NORMAL | HIGH ]"   },
    { MTAB_XTD|MTAB_VDV,    0,                      "COLOR",  "COLOR",
        &daz_set_color, &daz_show_color, NULL, "DAZZLER Color [ BW | COLOR ]"   },
    { 0 }
};

DEVICE daz_dev = {
    "DAZZLER", &daz_unit, daz_reg, daz_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &daz_reset,
    &daz_boot, NULL, NULL,
    &daz_ctx, DEV_DEBUG | DEV_DIS | DEV_DISABLE, 0,
    daz_debug, NULL, NULL, NULL, NULL, NULL,
    &daz_description
};

/*
   D+7A/JS-1 data structures

   js1_dev      D+7A device descriptor
   js1_unit     D+7A unit descriptor
   js1_reg      D+7A register list
*/

typedef struct {
    PNP_INFO pnp;        /* Must be first    */
} JS1_CTX;

static JS1_CTX js1_ctx = {{1, 0, JS1_IO_BASE, JS1_IO_SIZE}};

UNIT js1_unit = {
    UDATA (&js1_svc, 0, 0), 20000
};

REG js1_reg[] = {
    { NULL }
};

DEBTAB js1_debug[] = {
    { "VERBOSE",  VERBOSE_MSG,            "Verbose" },
    { 0 }
};

MTAB js1_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,           "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "DAZZLER base I/O address"   },
    { 0 }
};

DEVICE js1_dev = {
    "JS1", &js1_unit, js1_reg, js1_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &js1_reset,
    NULL, NULL, NULL,
    &js1_ctx, DEV_DEBUG | DEV_DIS | DEV_DISABLE, 0,
    js1_debug, NULL, NULL, NULL, NULL, NULL,
    &js1_description
};

/*
   DAZZLER routines

   daz_description
   daz_svc
   daz_reset
*/
static const char *daz_description (DEVICE *dptr)
{
    return "Cromemco Dazzler";
}

static t_stat daz_svc(UNIT *uptr)
{
    daz_refresh();

    sim_activate_after_abs(uptr, uptr->wait);

    return SCPE_OK;
}

static t_stat daz_reset(DEVICE *dptr)
{
    t_stat r = SCPE_OK;

    if (dptr->flags & DEV_DIS) {
        sim_map_resource(daz_ctx.pnp.io_base, daz_ctx.pnp.io_size, RESOURCE_TYPE_IO, &daz_io, "dazio", TRUE);

        sim_cancel(&daz_unit);

        if (daz_vptr != NULL) {
            return daz_close_video();
        }
    } else {
        r = sim_map_resource(daz_ctx.pnp.io_base, daz_ctx.pnp.io_size, RESOURCE_TYPE_IO, &daz_io, "dazio", FALSE);

        if (daz_vptr == NULL) {
            daz_open_video();
        } else {
            sim_activate_after_abs(&daz_unit, daz_unit.wait);
        }
    }

    return r;
}

static t_stat daz_boot(int32 unitno, DEVICE *dptr)
{
    if (chiptype == CHIP_TYPE_8080) {
        exdep_cmd(EX_D, "-m 100 MVI A,01H");
        exdep_cmd(EX_D, "-m 102 ORI 80H");
        exdep_cmd(EX_D, "-m 104 OUT 0EH");
        exdep_cmd(EX_D, "-m 106 MVI A,10H");
        exdep_cmd(EX_D, "-m 108 OUT 0FH");
        exdep_cmd(EX_D, "-m 10A LXI H,200H");
        exdep_cmd(EX_D, "-m 10D MVI C,32");
        exdep_cmd(EX_D, "-m 10F MVI B,16");
        exdep_cmd(EX_D, "-m 111 XRA A");
        exdep_cmd(EX_D, "-m 112 MOV M,A");
        exdep_cmd(EX_D, "-m 113 ADI 11H");
        exdep_cmd(EX_D, "-m 115 INX H");
        exdep_cmd(EX_D, "-m 116 DCR B");
        exdep_cmd(EX_D, "-m 117 JNZ 112H");
        exdep_cmd(EX_D, "-m 11A DCR C");
        exdep_cmd(EX_D, "-m 11B JNZ 10FH");
        exdep_cmd(EX_D, "-m 11E JMP 11EH");
    } else if (chiptype == CHIP_TYPE_Z80) {
        exdep_cmd(EX_D, "-m 100 LD A,01H");
        exdep_cmd(EX_D, "-m 102 OR 80H");
        exdep_cmd(EX_D, "-m 104 OUT (0EH),A");
        exdep_cmd(EX_D, "-m 106 LD A,10H");
        exdep_cmd(EX_D, "-m 108 OUT (0FH),A");
        exdep_cmd(EX_D, "-m 10A LD HL,200H");
        exdep_cmd(EX_D, "-m 10D LD C,32");
        exdep_cmd(EX_D, "-m 10F LD B,16");
        exdep_cmd(EX_D, "-m 111 XOR A");
        exdep_cmd(EX_D, "-m 112 LD (HL),A");
        exdep_cmd(EX_D, "-m 113 ADD A,11H");
        exdep_cmd(EX_D, "-m 115 INC HL");
        exdep_cmd(EX_D, "-m 116 DEC B");
        exdep_cmd(EX_D, "-m 117 JP NZ,112H");
        exdep_cmd(EX_D, "-m 11A DEC C");
        exdep_cmd(EX_D, "-m 11B JP NZ,10FH");
        exdep_cmd(EX_D, "-m 11E JP 11EH");
    }
    
    *((int32 *) sim_PC->loc) = 0x0100;

    return SCPE_OK;
}

static int32 daz_io(const int32 port, const int32 io, const int32 data)
{
    int32 p = port - daz_ctx.pnp.io_base;

    if (io == 0) {    /* IN */
        switch (p) {
            case 0x00: /* 0E */
                daz_frame = 0x7f;
                if ((sim_os_msec() % 30) > 25) {
                    daz_frame &= ~DAZ_EOF;
                } else {
                    daz_frame |= (sim_os_msec() & 1) ? 0x00 : DAZ_EVEN;
                }

                return daz_frame;

            case 0x01: /* 0F */
                sim_debug(VERBOSE_MSG, &daz_dev, "Unspecified IN 0x%02X\n", port);
                break;
        }
    } else {             /* OUT */
        switch (p) {
            case 0x00: /* 0E */
                daz_0e = data;
                daz_addr = (data & 0x7f) << 9;

                sim_debug(VERBOSE_MSG, &daz_dev, "New video address 0x%04X  Video is %s\n", daz_addr, daz_0e & DAZ_ON ? "ON" : "OFF");
                break;

            case 0x01: /* 0F */
                daz_set_0f(data);
                break;
        }
    }

    return 0xff;
}

static t_stat daz_open_video(void)
{
    t_stat r = SCPE_OK;
    int i;

    if (daz_vptr == NULL)  {
        sim_debug(VERBOSE_MSG, &daz_dev, "Opening new video window w:%d h:%d\n", daz_window_width, daz_window_height);

        r = vid_open_window(&daz_vptr, &daz_dev, "Display", daz_window_width, daz_window_height, SIM_VID_IGNORE_VBAR | SIM_VID_RESIZABLE); /* video buffer size */

        if (r != SCPE_OK) {
            sim_printf("Could not open video window r=%X\n", r);
            return r;
        }

        daz_resize_video();

        daz_cpalette[0] = vid_map_rgb_window(daz_vptr, 0x00, 0x00, 0x00);
        daz_cpalette[1] = vid_map_rgb_window(daz_vptr, 0x80, 0x00, 0x00);
        daz_cpalette[2] = vid_map_rgb_window(daz_vptr, 0x00, 0x80, 0x00);
        daz_cpalette[3] = vid_map_rgb_window(daz_vptr, 0x80, 0x80, 0x00);
        daz_cpalette[4] = vid_map_rgb_window(daz_vptr, 0x00, 0x00, 0x80);
        daz_cpalette[5] = vid_map_rgb_window(daz_vptr, 0x80, 0x00, 0x80);
        daz_cpalette[6] = vid_map_rgb_window(daz_vptr, 0x00, 0x80, 0x80);
        daz_cpalette[7] = vid_map_rgb_window(daz_vptr, 0x80, 0x80, 0x80);
        daz_cpalette[8] = vid_map_rgb_window(daz_vptr, 0x00, 0x00, 0x00);
        daz_cpalette[9] = vid_map_rgb_window(daz_vptr, 0xff, 0x00, 0x00);
        daz_cpalette[10] = vid_map_rgb_window(daz_vptr, 0x00, 0xff, 0x00);
        daz_cpalette[11] = vid_map_rgb_window(daz_vptr, 0xff, 0xff, 0x00);
        daz_cpalette[12] = vid_map_rgb_window(daz_vptr, 0x00, 0x00, 0xff);
        daz_cpalette[13] = vid_map_rgb_window(daz_vptr, 0xff, 0x00, 0xff);
        daz_cpalette[14] = vid_map_rgb_window(daz_vptr, 0x00, 0xff, 0xff);
        daz_cpalette[15] = vid_map_rgb_window(daz_vptr, 0xff, 0xff, 0xff);
        daz_gpalette[0] = vid_map_rgb_window(daz_vptr, 0x00, 0x00, 0x00);
        daz_gpalette[1] = vid_map_rgb_window(daz_vptr, 0x10, 0x10, 0x10);
        daz_gpalette[2] = vid_map_rgb_window(daz_vptr, 0x20, 0x20, 0x20);
        daz_gpalette[3] = vid_map_rgb_window(daz_vptr, 0x30, 0x30, 0x30);
        daz_gpalette[4] = vid_map_rgb_window(daz_vptr, 0x40, 0x40, 0x40);
        daz_gpalette[5] = vid_map_rgb_window(daz_vptr, 0x50, 0x50, 0x50);
        daz_gpalette[6] = vid_map_rgb_window(daz_vptr, 0x60, 0x60, 0x60);
        daz_gpalette[7] = vid_map_rgb_window(daz_vptr, 0x70, 0x70, 0x70);
        daz_gpalette[8] = vid_map_rgb_window(daz_vptr, 0x80, 0x80, 0x80);
        daz_gpalette[9] = vid_map_rgb_window(daz_vptr, 0x90, 0x90, 0x90);
        daz_gpalette[10] = vid_map_rgb_window(daz_vptr, 0xa0, 0xa0, 0xa0);
        daz_gpalette[11] = vid_map_rgb_window(daz_vptr, 0xb0, 0xb0, 0xb0);
        daz_gpalette[12] = vid_map_rgb_window(daz_vptr, 0xc0, 0xc0, 0xc0);
        daz_gpalette[13] = vid_map_rgb_window(daz_vptr, 0xd0, 0xd0, 0xd0);
        daz_gpalette[14] = vid_map_rgb_window(daz_vptr, 0xe0, 0xe0, 0xe0);
        daz_gpalette[15] = vid_map_rgb_window(daz_vptr, 0xff, 0xff, 0xff);

        for (i = 0; i < daz_screen_pixels; i++) {
            daz_surface[i] = 0;
        }

        vid_register_gamepad_motion_callback(js1_joy_motion);
        vid_register_gamepad_button_callback(js1_joy_button);
    }

    sim_activate_after_abs(&daz_unit, daz_unit.wait);

    return r;
}

static t_stat daz_close_video(void)
{
    t_stat r;

    sim_debug(VERBOSE_MSG, &daz_dev, "Closing video window\n");

    if ((r = vid_close_window(daz_vptr)) == SCPE_OK) {
        sim_cancel(&daz_unit);

        daz_vptr = NULL;
    }

    return r;
}

static void daz_resize_video(void)
{
    if (daz_vptr != NULL) {
        vid_render_set_logical_size(daz_vptr, daz_screen_width, daz_screen_height);
        if (!sim_is_running) {
            daz_refresh();
        }
    }
}

/*
 * Draw and refresh the screen in the video window
 */
static void daz_refresh(void) {
    if (daz_vptr != NULL) {
        if (daz_0f & DAZ_RESX4) {
            daz_render_x4();
        } else {
            daz_render_normal();
        }
        vid_draw_window(daz_vptr, 0, 0, daz_screen_width, daz_screen_height, daz_surface);
        vid_refresh_window(daz_vptr);
    }
}

static void daz_render_normal(void)
{
    int q, x, y;
    int32 maddr = daz_addr;
    int32 saddr = 0;

    for (q = 0; q < daz_pages; q++) {
        for (y = daz_quad_surfacey(q); y < daz_quad_surfacey(q) + 32; y++) {
            for (x = daz_quad_surfacex(q); x < daz_quad_surfacex(q) + 32; x+= 2) {
                saddr = (y * daz_res) + x;
                if (!(daz_0e & DAZ_ON)) {
                    daz_surface[saddr] = 0x00;
                    daz_surface[saddr+1] = 0x00;
                } else if (daz_0f & DAZ_COLOR) {
                    daz_surface[saddr] = daz_cpalette[GetBYTEWrapper(maddr) & 0x0f];
                    daz_surface[saddr+1] = daz_cpalette[(GetBYTEWrapper(maddr) & 0xf0) >> 4];
                } else {
                    daz_surface[saddr] = daz_gpalette[GetBYTEWrapper(maddr) & 0x0f];
                    daz_surface[saddr+1] = daz_gpalette[(GetBYTEWrapper(maddr) & 0xf0) >> 4];
                }
                maddr++;
            }
        }
    }
}

static void daz_render_x4(void)
{
    int b, q, x, y;
    int32 maddr = daz_addr;
    int32 saddr = 0;
    int32 soffset[] = {0, 1, daz_res, daz_res + 1, 2, 3, daz_res + 2, daz_res + 3};
    uint32 color;

    if (daz_0f & DAZ_COLOR) {
        color = daz_cpalette[daz_color];
    } else {
        color = daz_gpalette[daz_color];
    }

    for (q = 0; q < daz_pages; q++) {
        for (y = daz_quad_surfacey(q); y < daz_quad_surfacey(q) + 64; y+=2) {
            for (x = daz_quad_surfacex(q); x < daz_quad_surfacex(q) + 64; x += 4) {
                saddr = (y * daz_res) + x;
                for (b = 0; b < 8; b++) {
                    if (daz_0e & DAZ_ON) {
                        daz_surface[saddr + soffset[b]] = (GetBYTEWrapper(maddr) & (1 << b)) ? color : 0;
                    } else {
                        daz_surface[saddr + soffset[b]] = 0x00;
                    }
                }
                maddr++;
            }
        }
    }
}

static int32 daz_quad_surfacex(int q)
{
    if (q == 1 || q == 3) {
        return daz_res / ((daz_0f & DAZ_RESX4) ? 2 : 2);
    }

    return 0;
}

static int32 daz_quad_surfacey(int q)
{
    if (q == 2 || q == 3) {
        return daz_res / ((daz_0f & DAZ_RESX4) ? 2 : 2);
    }

    return 0;
}

static void daz_set_0f(uint8 val) {
    uint8 old = daz_0f;

    /* Update daz_0f register */
    daz_0f = val;
    daz_color = daz_0f & 0x0f;

    /* Did resolution change? */
    if ((daz_0f & (DAZ_RESX4 | DAZ_2K)) != (old & (DAZ_RESX4 | DAZ_2K))) {
        daz_res = 32;
        daz_pages = 1;
        if (daz_0f & DAZ_RESX4) {
            daz_res *= 2;
        }
        if (daz_0f & DAZ_2K) {
            daz_pages = 4;
            daz_res *= 2;
        }

        sim_debug(VERBOSE_MSG, &daz_dev, "Setting resolution to %02X %dx%d (%d pages) %s %s\n",
            daz_0f, daz_res, daz_res, daz_pages, DAZ_SHOW_RES(daz_0f), DAZ_SHOW_MEMSIZE(daz_0f));

        daz_screen_width = daz_res;
        daz_screen_height = daz_res;
        daz_screen_pixels = daz_screen_width * daz_screen_height;

        daz_resize_video();
    }

    if (!sim_is_running) {
        daz_refresh();
    }
}

static t_stat daz_set_video(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "OFF", strlen(cptr))) {
        daz_0e &= ~DAZ_ON;
    } else if (!strncmp(cptr, "ON", strlen(cptr))) {
        daz_0e |= DAZ_ON;
    } else {
        return SCPE_ARG;
    }

    if (!sim_is_running) {
        daz_refresh();
    }

    return SCPE_OK;
}

static t_stat daz_show_video(FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    if (!st) return SCPE_IERR;

    fprintf(st, "VIDEO=%s", DAZ_SHOW_VIDEO(daz_0e));

    return SCPE_OK;
}

static t_stat daz_set_resolution(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint8 old = daz_0f;

    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "NORMAL", strlen(cptr))) {
        old &= ~DAZ_RESX4;
    } else if (!strncmp(cptr, "HIGH", strlen(cptr))) {
        old |= DAZ_RESX4;
    } else {
        return SCPE_ARG;
    }

    daz_set_0f(old);

    return SCPE_OK;
}

static t_stat daz_show_resolution(FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    if (!st) return SCPE_IERR;

    fprintf(st, "RES=%s", DAZ_SHOW_RES(daz_0f));

    return SCPE_OK;
}

static t_stat daz_set_memsize(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint8 old = daz_0f;

    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "512", strlen(cptr))) {
        old &= ~DAZ_2K;
    } else if (!strncmp(cptr, "2K", strlen(cptr))) {
        old |= DAZ_2K;
    } else {
        return SCPE_ARG;
    }

    daz_set_0f(old);

    return SCPE_OK;
}

static t_stat daz_show_memsize(FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    if (!st) return SCPE_IERR;

    fprintf(st, "MEMSIZE=%s @ %04X", DAZ_SHOW_MEMSIZE(daz_0f), daz_addr);

    return SCPE_OK;
}

static t_stat daz_set_color(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint8 old = daz_0f;

    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "BW", strlen(cptr))) {
        old &= ~DAZ_COLOR;
    } else if (!strncmp(cptr, "COLOR", strlen(cptr))) {
        old |= DAZ_COLOR;
    } else {
        return SCPE_ARG;
    }

    daz_set_0f(old);

    return SCPE_OK;
}

static t_stat daz_show_color(FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    if (!st) return SCPE_IERR;

    fprintf(st, "%s", DAZ_SHOW_COLOR(daz_0f));

    return SCPE_OK;
}

/*
   D+7A / JS-1 routines

   js1_description
   js1_svc
   js1_reset
*/
static const char *js1_description (DEVICE *dptr)
{
    return "Cromemco D+7A";
}

t_stat js1_svc(UNIT *uptr)
{
    return SCPE_OK;
}

static t_stat js1_reset(DEVICE *dptr)
{
    t_stat r = SCPE_OK;

    if (dptr->flags & DEV_DIS) {
        sim_map_resource(js1_ctx.pnp.io_base, js1_ctx.pnp.io_size, RESOURCE_TYPE_IO, &js1_io, "js1io", TRUE);
    } else {
        r = sim_map_resource(js1_ctx.pnp.io_base, js1_ctx.pnp.io_size, RESOURCE_TYPE_IO, &js1_io, "js1io", FALSE);
    }

    return r;
}

static int32 js1_io(const int32 port, const int32 io, const int32 data)
{
    int32 p = port - js1_ctx.pnp.io_base;

    if (io == 0) {    /* IN */
        switch (p) {
            case 0x00: /* 18 */
                return (js1_buttons[0] & 0x0f) + ((js1_buttons[1] & 0x0f) << 4);
                break;

            case 0x01: /* 19 */
                return js1_joyx[0];
                break;

            case 0x02: /* 1A */
                return js1_joyy[0];
                break;

            case 0x03: /* 1B */
                return js1_joyx[1];
                break;

            case 0x04: /* 1C */
                return js1_joyy[1];
                break;
        }
    } else {             /* OUT */
        switch (p) {
            case 0x01: /* 19 - Sound not supported... yet */
            case 0x03: /* 1B - Sound not supported... yet */
                break;
        }
    }

    return 0xff;
}

static void js1_joy_motion (int device, int axis, int value)
{
    uint8 v;

    if (device < JS1_NUM_STICKS && axis < 2) {
        if (value < -32000) value = -32000;
        if (value > 32000) value = 32000;

        v = (uint8) (value >> 8);

        if (axis == 0) {
            js1_joyx[device] = v;
            sim_debug(SIM_VID_DBG_JOYSTICK, &daz_dev, "Joystick device=%d, axis=%d, value=%d x=%02X\n", device, axis, value, js1_joyx[device]);
        } else {
            js1_joyy[device] = -v;
            sim_debug(SIM_VID_DBG_JOYSTICK, &daz_dev, "Joystick device=%d, axis=%d, value=%d y=%02X\n", device, axis, value, js1_joyy[device]);
        }
    }
}

static void js1_joy_button (int device, int button, int state)
{
    if (device < JS1_NUM_STICKS && button < JS1_NUM_BUTTONS) {
        js1_buttons[device] |= 1 << button;
        if (state) js1_buttons[device] &= ~(1 << button);
        sim_debug(SIM_VID_DBG_JOYSTICK, &daz_dev, "Button device=%d, button=%d, state=%d\n", device, button, state);
    }
}

