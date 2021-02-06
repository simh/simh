#ifdef USE_DISPLAY
/* pdp11_vt.c: PDP-11 VT11/VS60 Display Processor Simulation

   Copyright (c) 2003-2004, Philip L. Budne, Douglas A. Gwyn
   Copyright (c) 1993-2003, Robert M Supnik

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   vt           VT11/VS60 Display Processor

   05-Feb-04    DAG     Improved VT11 emulation
                        Added VS60 support
   14-Sep-03    PLB     Start from pdp11_lp.c
*/

/*
 * this file is just a thin layer of glue to the simulator-
 * independent XY Display simulation
 */

#if defined (VM_VAX)                                    /* VAX version */
#include "vax_defs.h"
#elif defined(VM_PDP11)                                 /* PDP-11 version */
#include "pdp11_defs.h"
#else
#error "VT11/VS60 is supported only on the PDP-11 and VAX"
#endif

#include "display/display.h"
#include "display/vt11.h"
#include "sim_video.h"

/*
 * Timing parameters.  Should allow some runtime adjustment,
 * since several different configurations were shipped, including:
 *
 * GT40: PDP-11/05 with VT11 display processor
 * GT44: PDP-11/40 with VT11 display processor
 * GT46: PDP-11/34 with VT11 display processor
 * GT62: PDP-11/34a with VS60 display system
 */

/*
 * run a VT11/VS60 cycle every this many PDP-11 "cycle" times;
 *
 * this includes phosphor aging and polling for events (mouse movement) 
 * every refresh_interval (determined internally while processing the 
 * VT11/VS60 cycle).
 */
#define VT11_DELAY 1

/*
 * memory cycle time
 */
#define MEMORY_CYCLE 1                  /* either .98 or 1.2 us? */

/*
 * delay in microseconds between VT11/VS60 cycles:
 * VT11/VS60 and PDP-11 CPU's share the same memory bus,
 * and each VT11/VS60 instruction requires a memory reference;
 * figure each PDP11 instruction requires two memory references
 */
#define CYCLE_US (MEMORY_CYCLE*(VT11_DELAY*2+1))

#define UNIT_V_NOSPACEWAR  (UNIT_V_UF + 0)
#define UNIT_NOSPACEWAR    (1 << UNIT_V_NOSPACEWAR)

extern int32 int_vec[IPL_HLVL][32];

t_stat vt_rd(int32 *data, int32 PA, int32 access);
t_stat vt_wr(int32 data, int32 PA, int32 access);
t_stat vt_svc(UNIT *uptr);
t_stat vt_reset(DEVICE *dptr);
t_stat vt_boot(int32 unit, DEVICE *dptr);
t_stat vt_set_crt(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vt_show_crt(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat vt_set_scale(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vt_show_scale(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat vt_set_hspace(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vt_show_hspace(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat vt_set_vspace(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vt_show_vspace(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat vt_set_kb(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat vt_show_kb(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat vt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *vt_description (DEVICE *dptr);

/* VT11/VS60 data structures

   vt_dev       VT11 device descriptor
   vt_unit      VT11 unit descriptor
   vt_reg       VT11 register list
   vt_mod       VT11 modifier list
*/
#define IOLN_VT11   010             /* VT11 */
#define IOLN_VS60   040             /* VS60 */
DIB vt_dib = { IOBA_AUTO, IOLN_VT11, &vt_rd, &vt_wr,
               4, IVCL(VTST), VEC_AUTO, {NULL}, IOLN_VT11 };
        /* (VT11 uses only the first 3 interrupt vectors) */

UNIT vt_unit = {
    UDATA (&vt_svc, 0, 0),      VT11_DELAY};

REG vt_reg[] = {
    { DRDATAD (CYCLE,  vt_unit.wait, 24, "VT11/VS60 cycle"), REG_NZ + PV_LEFT },
    { GRDATA (DEVADDR, vt_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, vt_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }  };

MTAB vt_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "CRT",     "CRT={VR14|VR17|VR48}",
                &vt_set_crt,    &vt_show_crt,    NULL, "CRT Type" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "SCALE",   "SCALE={1|2|4|8}",
                &vt_set_scale,  &vt_show_scale,  NULL, "Pixel Scale Factor" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "HSPACE",  "HSPACE={NARROW|NORMAL}",
                &vt_set_hspace, &vt_show_hspace, NULL, "Horizontal Spacing" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VSPACE",  "VSPACE={TALL|NORMAL}",
                &vt_set_vspace, &vt_show_vspace, NULL, "Vertical Spacing" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
                &set_addr,      &show_addr,      NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VECTOR",  "VECTOR",
                &set_vec,       &show_vec,       NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV,             0, NULL,      "AUTOCONFIGURE",
                &set_addr_flt,  NULL,            NULL, "Enable autoconfiguration of address & vector" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "KEYBOARD","KEYBOARD={SPACEWAR|NOSPACEWAR}",
                &vt_set_kb,     &vt_show_kb,     NULL, "Disable or Enable Spacewar switches" },
    { 0 }  };


static t_bool vt_stop_flag = FALSE;

static void vt_quit_callback (void)
{
vt_stop_flag = TRUE;
}

/* Debug detail levels */

#define DEB_OPS       0001                          /* transactions */
#define DEB_RRD       0002                          /* reg reads */
#define DEB_RWR       0004                          /* reg writes */
#define DEB_TRC       0010                          /* trace */
#define DEB_STINT     0020                          /* STOP interrupts */
#define DEB_LPINT     0040                          /* Light Pen interrupts */
#define DEB_CHINT     0100                          /* CHAR interrupts */
#define DEB_NMINT     0200                          /* NAME interrupts */
#define DEB_INT       0360                          /* All Interrupts */
#define DEB_VT11      0400                          /* VT11 activities */
#define DEB_VMOU      SIM_VID_DBG_MOUSE             /* Video mouse */
#define DEB_VKEY      SIM_VID_DBG_KEY               /* Video key */
#define DEB_VCUR      SIM_VID_DBG_CURSOR            /* Video cursor */
#define DEB_VVID      SIM_VID_DBG_VIDEO             /* Video */

DEBTAB vt_deb[] = {
    { "OPS",      DEB_OPS, "transactions" },
    { "RRD",      DEB_RRD, "register reads" },
    { "RWR",      DEB_RWR, "register writes" },
    { "INT",      DEB_INT, "All Interrupts" },
    { "STOP",   DEB_STINT, "STOP interrupts" },
    { "LPEN",   DEB_LPINT, "Light Pen interrupts" },
    { "CHAR",   DEB_CHINT, "CHAR interrupts" },
    { "NAME",   DEB_NMINT, "NAME interrupts" },
    { "VT11",    DEB_VT11, "VT11 activities" },
    { "TRACE",    DEB_TRC, "trace" },
    { "VMOU",    DEB_VMOU, "Video Mouse" },
    { "VKEY",    DEB_VKEY, "Video Key" },
    { "VCUR",    DEB_VCUR, "Video Cursor" },
    { "VVID",    DEB_VVID, "Video Video" },
    { NULL, 0 }
    };

DEVICE vt_dev = {
    "VT", &vt_unit, vt_reg, vt_mod,
    1, 8, 31, 1, DEV_RDX, 16,
    NULL, NULL, &vt_reset,
    &vt_boot, NULL, NULL,
    &vt_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_Q18 | DEV_DEBUG,
    0, vt_deb, NULL, NULL, NULL, &vt_help, NULL, 
    &vt_description
};

const char *vt_regnam[] = {
    "DPC",
    "MPR",
    "XPR",
    "YPR",
    "RR",
    "SPR",
    "XOR",
    "YOR",
    "ANR",
    "SCR",
    "NR",
    "SDR",
    "STR",
    "SAR",
    "ZPR",
    "ZOR",
    };

/* VT11/VS60 routines

   vt_rd        I/O page read
   vt_wr        I/O page write
   vt_svc       process event
   vt_reset     process reset
   vt_boot      bootstrap device
*/

t_stat
vt_rd(int32 *data, int32 PA, int32 access)
{
    t_stat stat = SCPE_OK;
    
    switch (PA & 036) {
    case 000:             *data = vt11_get_dpc(); break; 
    case 002:             *data = vt11_get_mpr(); break; 
    case 004:             *data = vt11_get_xpr(); break;
    case 006:             *data = vt11_get_ypr(); break;
    case 010:   if (VS60) *data = vt11_get_rr();  else stat = SCPE_NXM; break; 
    case 012:   if (VS60) *data = vt11_get_spr(); else stat = SCPE_NXM; break; 
    case 014:   if (VS60) *data = vt11_get_xor(); else stat = SCPE_NXM; break; 
    case 016:   if (VS60) *data = vt11_get_yor(); else stat = SCPE_NXM; break; 
    case 020:   if (VS60) *data = vt11_get_anr(); else stat = SCPE_NXM; break; 
    case 022:   if (VS60) *data = vt11_get_scr(); else stat = SCPE_NXM; break; 
    case 024:   if (VS60) *data = vt11_get_nr();  else stat = SCPE_NXM; break; 
    case 026:   if (VS60) *data = vt11_get_sdr(); else stat = SCPE_NXM; break; 
    case 030:   if (VS60) *data = vt11_get_str(); else stat = SCPE_NXM; break; 
    case 032:   if (VS60) *data = vt11_get_sar(); else stat = SCPE_NXM; break; 
    case 034:   if (VS60) *data = vt11_get_zpr(); else stat = SCPE_NXM; break; 
    case 036:   if (VS60) *data = vt11_get_zor(); else stat = SCPE_NXM; break; 
    default: stat = SCPE_NXM;
    }
    sim_debug (DEB_RRD, &vt_dev, "vt_rd(%s-PA=0%o,data=0x%X(0%o),access=%d)\n", vt_regnam[(PA & 036)>>1], (int)PA, (int)*data, (int)*data, (int)access);
    return stat;
}

t_stat
vt_wr(int32 data, int32 PA, int32 access)
{
    uint16 d = data & 0177777;          /* mask just in case */
    
    sim_debug (DEB_RWR, &vt_dev, "vt_wr(%s-PA=0%o,data=0x%X(0%o),access=%d)\n", vt_regnam[(PA & 036)>>1], (int)PA, (int)data, (int)data, (int)access);

    switch (PA & 037) {
    case 000:                           /* DPC */
        /* set the simulator PC */
        vt11_set_dpc(d);

        /* Interrupt requests are cleared as each vector is dispatched,
           so in general, no need to clear interrupt requests here.
           However, if software is running at high IPL (diagnostics maybe)
           clearing them here does no harm */
        if (INT_IS_SET(VTST)) {
            sim_debug (DEB_STINT, &vt_dev, "CLR_INT(all)\n");
            CLR_INT (VTST);
            }
        if (INT_IS_SET(VTLP)) {
            sim_debug (DEB_LPINT, &vt_dev, "CLR_INT(all)\n");
            CLR_INT (VTLP);
            }
        if (INT_IS_SET(VTCH)) {
            sim_debug (DEB_CHINT, &vt_dev, "CLR_INT(all)\n");
            CLR_INT (VTCH);
            }
        if (INT_IS_SET(VTNM)) {
            sim_debug (DEB_NMINT, &vt_dev, "CLR_INT(all)\n");
            CLR_INT (VTNM);
            }

        /* start the display processor by running a cycle */
        vt_svc (&vt_unit);
        return SCPE_OK;

    case 002:                     vt11_set_mpr(d); return SCPE_OK;
    case 004:                     vt11_set_xpr(d); return SCPE_OK;
    case 006:                     vt11_set_ypr(d); return SCPE_OK;
    case 010:   if (!VS60) break; vt11_set_rr(d);  return SCPE_OK; 
    case 012:   if (!VS60) break; vt11_set_spr(d); return SCPE_OK; 
    case 014:   if (!VS60) break; vt11_set_xor(d); return SCPE_OK; 
    case 016:   if (!VS60) break; vt11_set_yor(d); return SCPE_OK; 
    case 020:   if (!VS60) break; vt11_set_anr(d); return SCPE_OK; 
    case 022:   if (!VS60) break; vt11_set_scr(d); return SCPE_OK; 
    case 024:   if (!VS60) break; vt11_set_nr(d);  return SCPE_OK;
    case 026:   if (!VS60) break; vt11_set_sdr(d); return SCPE_OK;
    case 030:   if (!VS60) break; vt11_set_str(d); return SCPE_OK; 
    case 032:   if (!VS60) break; vt11_set_sar(d); return SCPE_OK; 
    case 034:   if (!VS60) break; vt11_set_zpr(d); return SCPE_OK;
    case 036:   if (!VS60) break; vt11_set_zor(d); return SCPE_OK; 
    }
    return SCPE_NXM;
}

/*
 * here to run a display processor cycle, called as a SIMH
 * "device service routine".
 */
t_stat
vt_svc(UNIT *uptr)
{
    sim_debug (DEB_TRC, &vt_dev, "vt_svc(wait=%d,DPC=0%o)\n", uptr->wait, vt11_get_dpc());
    if (vt11_cycle(uptr->wait, 0))
        sim_activate_after (uptr, uptr->wait);  /* running; reschedule */
    if (vt_stop_flag) {
        vt_stop_flag = FALSE;                   /* reset flag after we notice it */
        return SCPE_STOP;
        }
    return SCPE_OK;
}

t_stat
vt_reset(DEVICE *dptr)
{
    DEVICE *ng_dptr;

    ng_dptr = find_dev ("NG");
    if (!(dptr->flags & DEV_DIS) && 
        (ng_dptr != NULL) && !(ng_dptr->flags & DEV_DIS)) {
        dptr->flags |= DEV_DIS;
        return sim_messagef (SCPE_NOFNC, "VT and NG device can't both be enabled\n");
        }
    if (!(dptr->flags & DEV_DIS))
        vt11_reset(dptr, DEB_VT11);
    vid_register_quit_callback (&vt_quit_callback);
    sim_debug (DEB_INT, &vt_dev, "CLR_INT(all)\n");
    CLR_INT (VTST);
    CLR_INT (VTLP);
    CLR_INT (VTCH);
    CLR_INT (VTNM);
    sim_cancel (dptr->units);           /* deactivate unit */
    return auto_config ("VT", (dptr->flags & DEV_DIS) ? 0 : 1);
}

#include "pdp11_vt_lunar_rom.h"         /* Lunar Lander image */

/*
 * GT4x/GT62 bootstrap (acts as remote terminal)
 * 
 */

t_addr vt_rom_base = 017766000;

uint16 vt_boot_rom[] = {
                                      //                                 ;       .ASECT
                                      //                                 
                                      //                                 ;BOOTVT.S09  5/2/72
                                      //                                 
                                      //                                 
                                      //                                 ;       VT-40 BOOTSTRAP LOADER, VERSION S09, RELEASE R01, 5/2/72
                                      //                                 
                                      //                                 ;       COPYRIGHT 1972, DIGITAL EQUIPMENT CORPORATION
                                      //                                 ;       146 MAIN STREET
                                      //                                 ;       MAYNARD, MASSACHUSSETTS
                                      //                                 ;                               01754
                                      //                                 
                                      //                                 
                                      //                                 ;       WRITTEN BY JACK BURNESS, SENIOR SYSTEMS ARCHITECT!
                                      //                                 
                                      //                                 
                                      //                                 
                                      //                                 
                                      //                                 ;       THIS ROUTINE IS INTENDED TO BE LOADED IN THE ROM PORTION OF THE VT-40.
                                      //                                 
                                      //                                 
                                      //                                 ;       REGISTER DEFINITIONS:
                                      //                                 
                                      //         000000                          R0=%0
                                      //         000001                          R1=%1
                                      //         000002                          R2=%2
                                      //         000003                          R3=%3
                                      //         000004                          R4=%4
                                      //         000005                          R5=%5
                                      //         000006                          R6=%6
                                      //         000007                          R7=%7
                                      //                                 
                                      //         000006                          SP=R6
                                      //         000007                          PC=R7
                                      //                                 
                                      //         000000                          RET1=R0                         ;RETURN OF VALUE REGISTER.
                                      //         000001                          INP1=R1                         ;ARGUMENT FOR CALLED FUNCTION
                                      //         000002                          INP2=R2                         ;SECOND ARGUMENT.
                                      //         000003                          WORK1=R3                        ;FIRST WORK REGISTER.
                                      //         000004                          WORK2=R4                        ;SECOND WORKING REGISTER.
                                      //         000005                          SCR1=R5                         ;SCRATCH REGISTER.
                                      //                                 
                                      //         000003                          L.CKSM=WORK1                    ;OVERLAPPING DEFINITIONS FOR LOADER PORTION.
                                      //         000000                          L.BYT=RET1
                                      //         000005                          L.BC=SCR1
                                      //         000001                          L.ADR=INP1
                                      //                                 
                                      //                                 
                                      //                                 
                                      //                                 
                                      //         016000                          COREND=16000                    ;FIRST LOCATION OF NON-CORE.
                                      //                                 
                                      //                                 ;       ROMORG=037000                   ;WHERE THE ROM PROGRAM SHOULD GO.
                                      //         166000                          ROMORG=166000                   ;WHERE THE ROM PROGRAM SHOULD GO.
                                      //         000000                          STARTX=0                        ;WHERE TO START DISPLAYING THE X POSITIONS.
                                      //         001360                          STARTY=1360                     ;WHERE TO START DISPLAYING THE Y.
                                      //                                 
                                      //         172000                          VT40PC=172000                   ;VT40 PROGRAM COUNTER.
                                      //         177560                          KBDIS=177560                    ;TTY INPUT STATUS.
                                      //         175614                          P10OS=175614                    ;PDP-10 OUTPUT STATUS.
                                      //         175610                          P10IS=175610                    ;PDP-10 INPUT STATUS.
                                      //                                 
                                      //         177562                          KBDIB=KBDIS+2                   ;TTY INPUT BUFFER.
                                      //         175612                          P10IB=P10IS+2                   ;PDP-10 INPUT CHARACTER.
                                      //         175616                          P10OB=P10OS+2                   ;PDP-10 OUTPUT BUFFER.
                                      //                                 
                                      //                                 
                                      //         015776                          P10OC=COREND-2                  ;CHARACTER TO BE SENT TO THE PDP-10
                                      //         015772                          P10IC=P10OC-4                   ;INPUT CHARACTER FROM 10 PLUS ONE SAVE CHARACTER
                                      //         015770                          STKSRT=P10IC-2                  ;FIRST LOCATION OF STACK.
                                      //                                 
                                      //                                 
                                      //         160000                          JMPDIS=160000                   ;THE VT-40 DISPLAY JUMP INSTRUCTION.
                                      //                                 
                                      //                                 
                                      //         000024                          PWRFAL=24                       ;POWER FAIL RESTART LOCATION.
                                      //                                 
                                      //                                 
                                      //         166000                          .=ROMORG                        ;SET THE ORIGIN NOW!!!!
                                      //                                 
                                      //                                 
    0012705, 0000026,                 // 166000  012705  000026          START:  MOV     #PWRFAL+2,SCR1          ;PICK UP POINTER TO P.F. STATUS.
    0005015,                          // 166004  005015                          CLR     @SCR1                   ;CLEAR IT OUT TO BE SURE.
    0010745,                          // 166006  010745                          MOV     PC,-(SCR1)              ;SET UP THE RESTART LOCATION.
                                      //                                 
    0000005,                          // 166010  000005                          RESET                           ;RESET THE BUS.
                                      //                                 
    0012767, 0000007, 0007570,        // 166012  012767  000007  007570          MOV     #7,P10IS                ;INITIALIZE PDP-10 INPUT
    0012767, 0000001, 0011532,        // 166020  012767  000001  011532          MOV     #1,KBDIS                ;INITIALIZE TTY INPUT.
    0012767, 0000201, 0007560,        // 166026  012767  000201  007560          MOV     #201,P10OS              ;INITIALIZE PDP-10 OUTPUT.
                                      //                                 
                                      //                                 
                                      //                                 
    0012706, 0015770,                 // 166034  012706  015770          RESTRT: MOV     #STKSRT,SP              ;SET UP THE STACK NOW!
    0005001,                          // 166040  005001                          CLR     L.ADR                   ;CLEAR ADDRESS POINTER.
    0012702, 0160000,                 // 166042  012702  160000                  MOV     #JMPDIS,INP2            ;PLACE A DISPLAY JUMP INSTRUCTION IN A REGISTER.
                                      //                                 
                                      //                                 
    0010221,                          // 166046  010221                          MOV     INP2,(L.ADR)+           ;MOVE IT TO LOCATION 0.
    0012711, 0166756,                 // 166050  012711  166756                  MOV     #DISPRG,(L.ADR)         ;MOVE ADDRESS POINTER INTO 2.
    0012701, 0000030,                 // 166054  012701  000030                  MOV     #PWRFAL+4,L.ADR         ;SET UP WHERE WE WILL STORE CHARACTERS.
    0005000,                          // 166060  005000                          CLR     RET1                    ;PREPARE TO INSERT A ZERO CHARACTER.
    0004767, 0000022,                 // 166062  004767  000022                  JSR     PC,DOCHAR               ;INSERT IT NOW.
    0005067, 0003706,                 // 166066  005067  003706                  CLR     VT40PC                  ;CLEAR THE DISPLAY PROGRAM COUNTER AND START.
                                      //                                 
    0004767, 0000210,                 // 166072  004767  000210          MAJOR:  JSR     PC,GETCHR               ;GET A CHARACTER NOW.
    0000240,                          // 166076  000240                          NOP
    0000240,                          // 166100  000240                          NOP
    0000240,                          // 166102  000240                          NOP
    0012746, 0166072,                 // 166104  012746  166072                  MOV     #MAJOR,-(SP)            ;INSERT IN DISPLAY BUFFER NOW.
                                      //                                 
    0010105,                          // 166110  010105                  DOCHAR: MOV     L.ADR,SCR1              ;GET CURRENT BUFFER POSITION NOW.
    0022525,                          // 166112  022525                          CMP     (SCR1)+,(SCR1)+         ;BYPASS CURRENT DISPLAY JUMP.
    0005025,                          // 166114  005025                          CLR     (SCR1)+                 ;CLEAR FUTURE ADDRESS FOR JUMP.
    0010225,                          // 166116  010225                          MOV     INP2,(SCR1)+            ;STICK IN TEMPORARY JUMP WHILE WE REPLACE CURREN
    0005015,                          // 166120  005015                          CLR     (SCR1)                  ;A DISPLAY JUMP TO ZERO.
    0005011,                          // 166122  005011                          CLR     (L.ADR)                 ;NOW REPLACE CURRENT DISPLAY JUMP BY THE CHARACT
    0050021,                          // 166124  050021                          BIS     RET1,(L.ADR)+           ;IT'S DONE THIS WAY TO WASTE 2 CYCLES.
    0010211,                          // 166126  010211                          MOV     INP2,(L.ADR)            ;TO AVOID TIMING PROBLEMS WITH THE VT40.
    0000207,                          // 166130  000207                          RTS     PC                      ;AND FINALLY RETURN.
                                      //                                 
                                      //                                 
    0004767, 0000124,                 // 166132  004767  000124          GET8:   JSR     PC,GETSIX               ;GET SIX BITS NOW.
    0010046,                          // 166136  010046                          MOV     RET1,-(SP)              ;SAVE THE CHARACTER NOW.
    0000401,                          // 166140  000401                          BR      GETP84                  ;BYPASS THE 8'ER
    0005002,                          // 166142  005002                  GET84:  CLR     INP2                    ;RESET THE MAGIC REGISTER NOW.
    0005722,                          // 166144  005722                  GETP84: TST     (INP2)+                 ;INCREMENT WHERE TO GO.
    0066207, 0166250,                 // 166146  066207  166250                  ADD     GET8TB(INP2),PC         ;UPDATE PC NOW.
                                      //                                 
                                      //         166152                  GET8P=.
                                      //                                 
    0004767, 0000104,                 // 166152  004767  000104          GET81:  JSR     PC,GETSIX               ;GET A CHARACTER NOW.
    0010004,                          // 166156  010004                          MOV     RET1,WORK2              ;SAVE FOR A SECOND.
    0006300,                          // 166160  006300                          ASL     RET1
    0006300,                          // 166162  006300                          ASL     RET1                    ;SHIFT TO LEFT OF BYTE
    0106300,                          // 166164  106300                          ASLB    RET1
    0106116,                          // 166166  106116                          ROLB    @SP                     ;PACK THEM IN.
    0106300,                          // 166170  106300                          ASLB    RET1
                                      //                                 
                                      //                                 
    0106116,                          // 166172  106116                          ROLB    @SP                     ;A GOOD 8 BIT THING.
    0012600,                          // 166174  012600                          MOV     (SP)+,RET1              ;POP AND RETURN NOW.
    0000207,                          // 166176  000207                          RTS     PC
                                      //                                 
    0006300,                          // 166200  006300                  GET82:  ASL     RET1                    ;WORST CASE, SHIFT 4
    0006300,                          // 166202  006300                          ASL     RET1
    0106300,                          // 166204  106300                          ASLB    RET1
    0106104,                          // 166206  106104                          ROLB    WORK2
    0106300,                          // 166210  106300                          ASLB    RET1
    0106104,                          // 166212  106104                          ROLB    WORK2
    0106300,                          // 166214  106300                          ASLB    RET1
    0106104,                          // 166216  106104                          ROLB    WORK2
    0106300,                          // 166220  106300                          ASLB    RET1
    0106104,                          // 166222  106104                          ROLB    WORK2
    0010400,                          // 166224  010400                          MOV     WORK2,RET1
    0012604,                          // 166226  012604                          MOV     (SP)+,WORK2
    0000207,                          // 166230  000207                          RTS     PC
                                      //                                 
    0006100,                          // 166232  006100                  GET83:  ROL     RET1
    0006100,                          // 166234  006100                          ROL     RET1
    0006004,                          // 166236  006004                          ROR     WORK2
    0106000,                          // 166240  106000                          RORB    RET1
    0006004,                          // 166242  006004                          ROR     WORK2
    0106000,                          // 166244  106000                          RORB    RET1                    ;FINAL CHARACTER ASSEMBLED.
    0005726,                          // 166246  005726                          TST     (SP)+                   ;FUDGE STACK.
    0000207,                          // 166250  000207                          RTS     PC                      ;AND RETURN NOW.
                                      //                                 
                                      //         166250                  GET8TB  =       .-2                     ;PUSH ZERO CONDITION BACK INTO NEVER-NEVER LAND.
                                      //                                 
    0000000,                          // 166252  000000                          .WORD   GET81-GET8P
    0000026,                          // 166254  000026                          .WORD   GET82-GET8P
    0000060,                          // 166256  000060                          .WORD   GET83-GET8P
    0177770,                          // 166260  177770                          .WORD   GET84-GET8P
                                      //                                 
                                      //                                 
    0004767, 0000020,                 // 166262  004767  000020          GETSIX: JSR     PC,GETCHR
    0020027, 0000040,                 // 166266  020027  000040                  CMP     RET1,#40
    0002546,                          // 166272  002546                          BLT     L.BAD
    0020027, 0000137,                 // 166274  020027  000137                  CMP     RET1,#137
    0003143,                          // 166300  003143                          BGT     L.BAD
    0000207,                          // 166302  000207                          RTS     PC
                                      //                                 
                                      //                                 
                                      //                                 
    0005726,                          // 166304  005726                  GETCHP: TST     (SP)+                   ;UPDATE THE STACK.
                                      //                                 
    0012700, 0015772,                 // 166306  012700  015772          GETCHR: MOV     #P10IC,RET1             ;SET UP POINTER TO THE INPUT CHARACTER.
    0004767, 0000064,                 // 166312  004767  000064          GETCHL: JSR     PC,CHECK
    0005710,                          // 166316  005710                          TST     @RET1                   ;ANY CHARACTER THERE?
    0001774,                          // 166320  001774                          BEQ     GETCHL
    0011046,                          // 166322  011046                          MOV     @RET1,-(SP)             ;PUSH THE CHAR ON THE STACK.
    0005020,                          // 166324  005020                          CLR     (RET1)+                 ;CLEAR THE CHAR GOT FLAG NOW.
    0042716, 0177600,                 // 166326  042716  177600                  BIC     #-200,(SP)              ;CLEAR AWAY PARITY NOW.
    0001764,                          // 166332  001764                          BEQ     GETCHP                  ;IF ZERO, GET ANOTHER
                                      //                                 
                                      //                                 
    0022716, 0000177,                 // 166334  022716  000177                  CMP     #177,(SP)
    0001761,                          // 166340  001761                          BEQ     GETCHP                  ;ALSO IGNORE RUBOUTS.
    0022710, 0000175,                 // 166342  022710  000175                  CMP     #175,@RET1              ;WAS IT A "175"
    0001007,                          // 166346  001007                          BNE     GETNP                   ;NOPE.
    0011610,                          // 166350  011610                          MOV     (SP),@RET1              ;YEP. RESET IN CASE OF ABORT.
    0021027, 0000122,                 // 166352  021027  000122                  CMP     @RET1,#122              ;IS IT AN R
    0001626,                          // 166356  001626                          BEQ     RESTRT                  ;YEP. RESTART
    0021027, 0000114,                 // 166360  021027  000114                  CMP     @RET1,#114              ;IS IT AN L
    0001455,                          // 166364  001455                          BEQ     LOAD                    ;YEP. LOAD.
                                      //                                 
    0011610,                          // 166366  011610                  GETNP:  MOV     (SP),@RET1              ;NOW DO THE FUDGING.
    0012600,                          // 166370  012600                          MOV     (SP)+,RET1
    0020027, 0000175,                 // 166372  020027  000175                  CMP     RET1,#175
    0001743,                          // 166376  001743                          BEQ     GETCHR                  ;IF ALTMODE, LOOP
    0000207,                          // 166400  000207                          RTS     PC
                                      //                                 
                                      //                                 
    0005767, 0027370,                 // 166402  005767  027370          CHECK:  TST     P10OC                   ;DO WE WANT TO OUTPUT?
    0001410,                          // 166406  001410                          BEQ     CHECK1                  ;NO.
    0105767, 0007200,                 // 166410  105767  007200                  TSTB    P10OS                   ;WE DO. IS THE 10 READY?
    0100005,                          // 166414  100005                          BPL     CHECK1                  ;NOT QUITE.
    0016767, 0027354, 0007172,        // 166416  016767  027354  007172          MOV     P10OC,P10OB             ;IT'S READY. SEND THE CHARACTER.
    0005067, 0027346,                 // 166424  005067  027346                  CLR     P10OC                   ;AND THE SAVED CHARACTER.
                                      //                                 
    0105767, 0011124,                 // 166430  105767  011124          CHECK1: TSTB    KBDIS                   ;HEY, IS THE KEYBOARD READY?
    0100014,                          // 166434  100014                          BPL     CHECK3                  ;NOPE. NO LUCK.
    0116746, 0011120,                 // 166436  116746  011120                  MOVB    KBDIB,-(SP)             ;YEP. SAVE THE CHARACTER NOW.
    0012767, 0000001, 0011110,        // 166442  012767  000001  011110          MOV     #1,KBDIS                ;AND REENABLE THE COMMUNICATIONS DEVICE.
                                      //                                 
    0004767, 0177726,                 // 166450  004767  177726          CHECK2: JSR     PC,CHECK                ;IS THE OUTPUT READY?
    0005767, 0027316,                 // 166454  005767  027316                  TST     P10OC
    0001373,                          // 166460  001373                          BNE     CHECK2                  ;IF NOT, WAIT TILL DONE.
    0012667, 0007130,                 // 166462  012667  007130                  MOV     (SP)+,P10OB             ;AND THEN SEND OUT THE CHARACTER.
                                      //                                 
                                      //                                 
    0105767, 0007116,                 // 166466  105767  007116          CHECK3: TSTB    P10IS                   ;IS THE 10 TALKING TO ME.
    0100011,                          // 166472  100011                          BPL     CHECK4                  ;NOPE. EXIT.
    0116767, 0007112, 0027270,        // 166474  116767  007112  027270          MOVB    P10IB,P10IC             ;GET THE CHARACTER NOW.
    0052767, 0177400, 0027262,        // 166502  052767  177400  027262          BIS     #-400,P10IC             ;MAKE SURE IT'S NONE ZERO.
    0012767, 0000007, 0007072,        // 166510  012767  000007  007072          MOV     #7,P10IS                ;REINITIALIZE COMMUNICATION LINE.
                                      //                                 
    0000207,                          // 166516  000207                  CHECK4: RTS     PC                      ;AND RETURN.
                                      //                                 
                                      //                                 
                                      //                                 ;       THE   L  O  A  D  E  R
                                      //                                 
    0005002,                          // 166520  005002                  LOAD:   CLR    INP2                     ;RESET TO FIRST 8 BIT CHARACTER.
    0012712, 0172000,                 // 166522  012712  172000                  MOV    #172000,(INP2)           ;AND ALSO CLEVERLY STOP THE VT40.
    0012706, 0015770,                 // 166526  012706  015770                  MOV    #STKSRT,SP               ;RESET STACK POINTER NOW.
                                      //                                 
    0005003,                          // 166532  005003                  L.LD2:  CLR    L.CKSM                   ;CLEAR THE CHECKSUM
    0004767, 0000070,                 // 166534  004767  000070                  JSR    PC,L.PTR                 ;GET A BYTE NOW.
    0105300,                          // 166540  105300                          DECB   L.BYT                    ;IS IT ONE?
    0001373,                          // 166542  001373                          BNE    L.LD2                    ;NOPE. WAIT AWHILE
    0004767, 0000060,                 // 166544  004767  000060                  JSR    PC,L.PTR                 ;YEP. GET NEXT CHARACTER.
                                      //                                 
    0004767, 0000072,                 // 166550  004767  000072                  JSR    PC,L.GWRD                ;GET A WORD.
    0010005,                          // 166554  010005                          MOV    L.BYT,L.BC               ;GET THE COUNTER NOW.
    0162705, 0000004,                 // 166556  162705  000004                  SUB    #4,L.BC                  ;CHOP OFF EXTRA STUFF.
    0022705, 0000002,                 // 166562  022705  000002                  CMP    #2,L.BC                  ;NULL?
    0001437,                          // 166566  001437                          BEQ    L.JMP                    ;YEP. MUST BE END.
    0004767, 0000052,                 // 166570  004767  000052                  JSR    PC,L.GWRD                ;NOPE. GET THE ADDRESS.
    0010001,                          // 166574  010001                          MOV    L.BYT,L.ADR              ;AND REMEMBER FOR OLD TIMES SAKE.
                                      //                                 
    0004767, 0000026,                 // 166576  004767  000026          L.LD3:  JSR    PC,L.PTR                 ;GET A BYTE (DATA)
    0002010,                          // 166602  002010                          BGE    L.LD4                    ;ALL DONE WITH THE COUNTER?
    0105703,                          // 166604  105703                          TSTB   L.CKSM                   ;YEP. GOOD CHECK SUM?
    0001751,                          // 166606  001751                          BEQ    L.LD2                    ;NOPE. LOAD ERROR.
                                      //                                 
    0012700,                          // 166610  012700                  L.BAD:  MOV    (PC)+,RET1               ;SEND OUT SOME CHARACTERS NOW.
    0041175,                          // 166612     175          
                                      // 166613     102                          .BYTE  175,102                  ;"CTRL BAD"
    0004767, 0000110,                 // 166614  004767  000110                  JSR    PC,SENDIT
    0000167, 0177210,                 // 166620  000167  177210                  JMP    RESTRT
                                      //                                 
    0110021,                          // 166624  110021                  L.LD4:  MOVB   L.BYT,(L.ADR)+           ;PLACE THE BYTE IN CORE.
    0000763,                          // 166626  000763                          BR     L.LD3                    ;GET ANOTHER ONE.
                                      //                                 
    0004767, 0177276,                 // 166630  004767  177276          L.PTR:  JSR    PC,GET8                  ;GET 8 BITS NOW.
    0060003,                          // 166634  060003                          ADD    L.BYT,L.CKSM             ;UPDATE CHECKSUM
    0042700, 0177400,                 // 166636  042700  177400                  BIC    #177400,L.BYT            ;CLEAN UP THE BYTE NOW.
    0005305,                          // 166642  005305                          DEC    L.BC                     ;UPDATE THE COUNTER.
    0000207,                          // 166644  000207                          RTS    PC                       ;RETURN NOW.
                                      //                                 
    0004767, 0177756,                 // 166646  004767  177756          L.GWRD: JSR    PC,L.PTR                 ;GET A CHARACTER.
    0010046,                          // 166652  010046                          MOV    L.BYT,-(SP)              ;SAVE FOR A SECOND.
    0004767, 0177750,                 // 166654  004767  177750                  JSR    PC,L.PTR                 ;GET ANOTHER CHARACTER.
    0000300,                          // 166660  000300                          SWAB   L.BYT                    ;NOW ASSEMBLE THE WORD.
    0052600,                          // 166662  052600                          BIS    (SP)+,L.BYT              ;AND RETURN WITH A 16 BITER.
                                      //                                 
                                      //                                 
    0000207,                          // 166664  000207                          RTS    PC
                                      //                                 
    0004767, 0177754,                 // 166666  004767  177754          L.JMP:  JSR    PC,L.GWRD                ;GET A WORD
    0010046,                          // 166672  010046                          MOV    L.BYT,-(SP)              ;SAVE ON THE STACK.
    0004767, 0177730,                 // 166674  004767  177730                  JSR    PC,L.PTR                 ;GET A CHARACTER.
    0105703,                          // 166700  105703                          TSTB   L.CKSM                   ;IS IT ZERO?
    0001342,                          // 166702  001342                          BNE    L.BAD                    ;YEP. WHAT CRAP.
    0032716, 0000001,                 // 166704  032716  000001                  BIT    #1,(SP)                  ;IS IT ODD?
    0001406,                          // 166710  001406                          BEQ    L.JMP1                   ;YEP. START PROGRAM GOING NOW.
    0012700,                          // 166712  012700                          MOV    (PC)+,RET1               ;TELL PDP-10 WE'VE LOADED OK.
    0043575,                          // 166714     175          
                                      // 166715     107                          .BYTE  175,107
    0004767, 0000006,                 // 166716  004767  000006                  JSR    PC,SENDIT
    0000000,                          // 166722  000000                          HALT
    0000776,                          // 166724  000776                          BR      .-2
                                      //                                 
    0000136,                          // 166726  000136                  L.JMP1: JMP     @(SP)+                  ;AND AWAY WE GO.
                                      //                                 
                                      //                                 
    0004767, 0177446,                 // 166730  004767  177446          SENDIT: JSR     PC,CHECK                ;POLL THE OUTPUT DEVICE NOW.
    0005767, 0027036,                 // 166734  005767  027036                  TST     P10OC                   ;OUTPUT CLEAR?
    0001373,                          // 166740  001373                          BNE     SENDIT                  ;NOPE. LOOP AWHILE LONGER.
    0010067, 0006650,                 // 166742  010067  006650                  MOV     RET1,P10OB              ;SEND OUT THE CHARACTER.
    0105000,                          // 166746  105000                          CLRB    RET1                    ;CLEAR THE BYTE.
    0000300,                          // 166750  000300                          SWAB    RET1                    ;AND SWAP THEM NOW.
    0001366,                          // 166752  001366                          BNE     SENDIT                  ;IF NOT EQUAL, REPEAT.
    0000207,                          // 166754  000207                          RTS     PC
    0000000,                          // 166756  000000                                      //                                 
    0000000,                          // 166760  000000                                      //                                 
    0000000,                          // 166762  000000                                      //                                 
    0000000,                          // 166764  000000                                      //                                 
    0000000,                          // 166766  000000                                      //                                 
    0000000,                          // 166770  000000                                      //                                 
    0000000,                          // 166772  000000                                      //                                 
    0000000,                          // 166774  000000                                      //                                 
    0000000,                          // 166776  000000                                      //                                 
                                      //                                 
                                      //                                 ;               THIS IS THE INITIALIZING VT40 PROGRAM WHICH WILL
                                      //                                 ;               JUMP TO THE PROGRAM AFTER THE POWER FAIL LOCATIONS
                                      //                                 ;               WHICH WILL JUMP TO ZERO WHICH WILL JUMP BACK TO HERE.
                                      //                                 
                                      //                                 
                                      //                                 
    0170256,                          // 166756  170256                  DISPRG:         .WORD 170256            ;LOAD STATUS REGISTER FOR NORMAL OPERATION.
    0115124,                          // 166760  115124                                  .WORD 115124            ;SET POINT MODE. "NORMAL".
    0000000,                          // 166762  000000                                  .WORD STARTX            ;X COORDINATE
    0001360,                          // 166764  001360                                  .WORD STARTY            ;Y COORDINATE
    0100000,                          // 166766  100000                                  .WORD 100000            ;SET CHARACTER MODE.
    0160000,                          // 166770  160000                                  .WORD JMPDIS            ;THEN JUMP TO THE POWERFAIL LOCATION.
    0000030,                          // 166772  000030                                  .WORD PWRFAL+4          ;TO DISPLAY USERS CHARACTERS.
                                      //                                 
                                      //                                 
                                      //         000001                                  .END
    };

t_stat
vt_rom_rd(int32 *data, int32 PA, int32 access)
{
*data = (int32)vt_boot_rom[(((PA - vt_rom_base) & 0xFFFF) >> 1)];
return SCPE_OK;
}

DIB vt_rom_dib;

t_stat
vt_boot(int32 unit, DEVICE *dptr)
{
    t_stat r;
    char stability[32];
    extern int32 saved_PC;

    if (sim_switch_number == 40) {      /* GT40 Boot? */
        set_cmd (0, "CPU 11/05");
        set_cmd (0, "CPU 16k");
        set_cmd (0, "DLI ENABLE");
        set_cmd (0, "DLI LINES=1");
        set_cmd (0, "DLI ADDRESS=17775610");
        set_cmd (0, "VT CRT=VR14");
        set_cmd (0, "VT SCALE=1");
        set_cmd (0, "VT ADDRESS=17772000");
        vt_dib.next = &vt_rom_dib;
        vt_rom_dib.ba = vt_rom_base;
        vt_rom_dib.lnt = sizeof (vt_boot_rom);
        vt_rom_dib.rd = &vt_rom_rd;
        r = build_ubus_tab (&vt_dev, &vt_rom_dib);
        if (r != SCPE_OK)
            return r;
        cpu_set_boot (vt_rom_base);
        return r;
        }
    /* XXX  should do something like vt11_set_dpc(&appropriate_ROM_image) */

    /* Instead, since that won't be too useful.... */
    /* Load and start Lunar Lander which has the potential to be fun! */
    sim_set_memory_load_file (BOOT_CODE_ARRAY, BOOT_CODE_SIZE);
    r = load_cmd (0, BOOT_CODE_FILENAME);
    sim_set_memory_load_file (NULL, 0);
    /* Lunar Lander presumes a VT device vector base of 320 */
    if (0320 != vt_dib.vec) { /* If that is not the case, then copy the 320 vectors to the right place */
        WrMemW (vt_dib.vec + 000, RdMemW (0320 + 0));
        WrMemW (vt_dib.vec + 002, RdMemW (0320 + 2));
        WrMemW (vt_dib.vec + 004, RdMemW (0324 + 0));
        WrMemW (vt_dib.vec + 006, RdMemW (0324 + 2));
        WrMemW (vt_dib.vec + 010, RdMemW (0330 + 0));
        WrMemW (vt_dib.vec + 012, RdMemW (0330 + 2));
        WrMemW (vt_dib.vec + 014, RdMemW (0334 + 0));
        WrMemW (vt_dib.vec + 016, RdMemW (0334 + 2));
        }
    cpu_set_boot (saved_PC);
    set_cmd (0, "VT SCALE=1");
    set_cmd (0, "VT CRT=VR14");
    sprintf (stability, "%d", SIM_IDLE_STMIN);
    sim_set_idle (NULL, 0, stability, NULL);    /* force minimum calibration stability */
    sim_clr_idle (NULL, 0, stability, NULL);
    return r;
}

/* SET/SHOW VT options: */

t_stat
vt_set_crt(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];

    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "VR14") == 0)
        vt11_display = DIS_VR14;
    else if (strcmp(gbuf, "VR17") == 0)
        vt11_display = DIS_VR17;
    else if (strcmp(gbuf, "VR48") == 0)
        vt11_display = DIS_VR48;
    else
        return SCPE_ARG;
    vt_dib.lnt = (VS60) ? IOLN_VS60 : IOLN_VT11;
    deassign_device (&vt_dev);
    if (VS60)
        assign_device (&vt_dev, "VS60");
    return SCPE_OK;
}

t_stat
vt_show_crt(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "crt=VR%d", (int)vt11_display);
    return SCPE_OK;
}

t_stat
vt_set_scale(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    t_value v;
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    v = get_uint(cptr, 10, 8, &r);
    if (r != SCPE_OK)
        return r;
    if (v != 1 && v != 2 && v != 4 && v != 8)
        return SCPE_ARG;
    vt11_scale = v;
    return SCPE_OK;
}

t_stat
vt_show_scale(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "scale=%d", (int)vt11_scale);
    return SCPE_OK;
}

t_stat
vt_set_hspace(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "NARROW") == 0)
        vt11_csp_w = 12;
    else if (strcmp(gbuf, "NORMAL") == 0)
        vt11_csp_w = 14;
   else
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat
vt_show_hspace(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "hspace=%s", vt11_csp_w==12 ? "narrow" : "normal");
    return SCPE_OK;
}

t_stat
vt_set_vspace(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    char gbuf[CBUFSIZE];
    if (vt11_init)
        return SCPE_ALATT;              /* should be "changes locked out" */
    if (cptr == NULL)
        return SCPE_ARG;
    get_glyph(cptr, gbuf, 0);
    if (strcmp(gbuf, "TALL") == 0)
        vt11_csp_h = 26;
    else if (strcmp(gbuf, "NORMAL") == 0)
        vt11_csp_h = 24;
   else
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat
vt_show_vspace(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "vspace=%s", vt11_csp_h==26 ? "tall" : "normal");
    return SCPE_OK;
}

/* interface routines (called from display simulator) */

void
vt_stop_intr(void)
{
    sim_debug (DEB_STINT, &vt_dev, "SET_INT (VTST)\n");
    SET_INT (VTST);
}

void
vt_lpen_intr(void)
{
    sim_debug (DEB_LPINT, &vt_dev, "SET_INT (VTLP)\n");
    SET_INT (VTLP);
}

void
vt_char_intr(void)
{
    sim_debug (DEB_CHINT, &vt_dev, "SET_INT (CHAR)\n");
    SET_INT (VTCH);
}

void
vt_name_intr(void)
{
    sim_debug (DEB_NMINT, &vt_dev, "SET_INT (VTNM)\n");
    SET_INT (VTNM);
}

/* fetch memory */
int
vt_fetch(uint32 addr, vt11word *wp)
{
    /* On PDP-11 Unibus 22-bit systems, the VT11/VS60 behaves as
       an 18-bit Unibus peripheral and must go through the I/O map. */

    /* apply Unibus map, when appropriate */

    /* 
     * The Graphics processor only has a 16 bit program counter 
     * (i.e. address register).  If this address happens to be >= 56Kb 
     * then it is a reference to a graphics program running from 
     * ROM and in order for that to be found on an 18 bit Unibus
     * we need to sign extend the the address so it resides in
     * the I/O page.
     */
    if (addr >= (uint32)(IOPAGEBASE & DMASK)) {
        sim_debug (DEB_VT11, &vt_dev, "vt_fetch(addr=0%o) Adjusting ROM address to 0%o\n", (int)addr, (int)(addr | IOPAGEBASE));
        addr |= IOPAGEBASE;
        }
    if (Map_ReadW(addr, 2, wp) == 0) {
        sim_debug (DEB_VT11, &vt_dev, "vt_fetch(addr=0%o) - 0%o\n", (int)addr, (int)*wp);
        return 0;                       /* no problem */
        }
    sim_debug (DEB_VT11, &vt_dev, "vt_fetch(addr=0%o) - failed\n", (int)addr);
    /* else mapped address lies outside configured memory range */

    *wp = 0164000;                      /* DNOP; just updates DPC if used */
                                                /* which shouldn't happen */
    return 1;                           /* used to set "time_out" flag */
}

const char *vt_description (DEVICE *dptr)
{
return (VS60) ? "VS60 Display processor"
              : "VT11 Display processor";
}

#ifdef VM_PDP11
/* PDP-11 simulation provides this */
extern int32 SR;                        /* switch register */
#else
int32 SR;                               /* switch register */
#endif

t_stat vt_set_kb(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
char gbuf[CBUFSIZE];

if (cptr == NULL || *cptr == 0)
    return SCPE_ARG;
get_glyph(cptr, gbuf, 0);
if (MATCH_CMD(gbuf, "SPACEWAR") == 0)
    uptr->flags &= ~UNIT_NOSPACEWAR;
else if (MATCH_CMD(gbuf, "NOSPACEWAR") == 0)
    uptr->flags |= UNIT_NOSPACEWAR;
else
    return sim_messagef (SCPE_ARG, "Unexpected Keyboard setting: %s\n", gbuf);
return SCPE_OK;
}

t_stat vt_show_kb(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf(st, "keyboard=%s",
        (uptr->flags & UNIT_NOSPACEWAR) ? "nospacewar" : "spacewar");
return SCPE_OK;
}

void
cpu_set_switches(unsigned long v1, unsigned long v2)
{
if ((vt_unit.flags & UNIT_NOSPACEWAR) == 0)
    SR = v1 ^ v2;
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2)
{
if ((vt_unit.flags & UNIT_NOSPACEWAR) == 0) {
    *p1 = SR;
    *p2 = 0;
    }
}

t_stat vt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The VT11 is a calligraphic display-file device used in the GT4x series\n"
    " of workstations (PDP-11/04,34,40 based).\n\n"
    " The VS60 is an improved, extended, upward-compatible version of the\n"
    " VT11, used in the GT62 workstation (PDP-11/34 based).  It supports\n"
    " dual consoles (CRTs with light pens), multiple phosphor colors, 3D\n"
    " depth cueing, and circle/arc generator as options.  We do not know\n"
    " whether any of these options were ever implemented or delivered.\n"
    " Apparently a later option substituted a digitizing-tablet correlator\n"
    " for the light pen.  The VS60 has a 4-level silo (graphic data pipeline)\n"
    " which for reasons of simplicity is not implemented in this simulation;\n"
    " the only visible effect is that DZVSC diagnostic tests 110 & 111 will\n"
    " report failure.\n\n"
    " The VSV11/VS11 is a color raster display-file device (with joystick\n"
    " instead of light pen) with instructions similar to the VT11's but\n"
    " different enough that a separate emulation should be created by\n"
    " editing a copy of this source file rather than trying to hack it into\n"
    " this one.  Very likely, the display (phosphor decay) simulation will\n"
    " also require revision to handle multiple colors.\n\n"
    " There were further models in this series, but they appear to have\n"
    " little if any compatibility with the VT11.\n\n"
    " Much later, DEC produced a display system it called the VS60S, but\n"
    " that doesn't seem to bear any relationship to the original VS60.\n\n"
    " A PDP-11 system has at most one display controller attached.\n\n"
    "1 Booting\n"
    " There are two booting options for the GT40.\n"
    "2 Demo Mode - Lunar Lander\n"
    " The most famous software for the GT40 was the MOONLANDER\n"
    " game by Jack Burness, also known as LUNAR LANDER. This was\n"
    " undoubtedly the inspiration for Atari's coin-operated video\n"
    " game of that name.  To play the demo and play the game:\n\n"
    "++sim> set vt enable\n"
    "++sim> boot vt\n\n"
    "3 Playing Lunar Lander\n"
    " The object of moonlander is to land a lunar module on the\n"
    " surface of the moon.\n\n"
    " When the program is loaded, it will automatically start and\n"
    " display an \"introductory message\" on the screen.  Future\n"
    " restart of the program will not cause this message to be\n"
    " displayed.  All numbers, speeds, weights, etc., are actual\n"
    " numbers.  They are for real.  To make the game more possible\n"
    " for an average person to play, I have given him about 25 to\n"
    " 50%% more fuel in the final stages of landing than he would\n"
    " actually have.\n\n"
    " What the user sees on the screen is a broad and extremely\n"
    " mountainous view of the moon.  On the right is a list of data\n"
    " parameters which the user may examine.  They are height,\n"
    " altitude, angle, fuel left, thrust, weight, horizontal\n"
    " velocity, vertical velocity, horizontal acceleration, vertical\n"
    " acceleration, distance and seconds.  At the top of the screen,\n"
    " any four of the values may be displayed.  To display an item,\n"
    " the user points the mouse at the item he wishes to display.\n"
    " The item will then start blinking, to indicate that this is\n"
    " the item to be displayed.  The user then points the mouse at\n"
    " one of the previously displayed items at the top of the screen.\n"
    " The old item disappears and is replaced by the new item.\n"
    " Note that it is possible to display any item anywhere, and even\n"
    " possible to display one item four times at the top.  Anyway,\n"
    " the parameters mean the following.  Height is the height in\n"
    " feet above the surface (terrain) of the moon.  It is the \"radar\"\n"
    " height.  Altitude is the height above the \"mean\" height of the\n"
    " moon ( I guess you would call it \"mare\" level).  Thus altitude\n"
    " is not affected by terrain.  Angle is the angle of the ship in\n"
    " relationship to the vertical.  10 degrees, -70 degrees, etc.\n"
    " Fuel left is the amount of fuel left in pounds.  Thrust is the\n"
    " amount of thrust (pounds) currently being produced by the engine.\n"
    " Weight is the current earth weight of the ship.  As fuel is\n"
    " burned off, the acceleration will increase due to a lessening of\n"
    " weight.  The horizontal velocity is the current horizontal speed\n"
    " of the ship, in feet per second.  It is necessary to land at\n"
    " under 10 fps horizontal, or else the ship will tip over.\n"
    " Vertical velocity is the downward speed of the ship.  Try to\n"
    " keep it under 30 for the first few landings, until you get\n"
    " better.  A perfect landing is under 8 fps.  The horizontal\n"
    " and vertical accelerations are just those, in f/sec/sec.\n"
    " With no power, the vertical acceleration is about 5 fp/s/s\n"
    " down (-5).  Distance is the horizontal distance (X direction)\n"
    " you are from the projected landing site.  Try to stay within\n"
    " 500 feet of this distance, because there are not too many\n"
    " spots suitable for landing on the moon.  Seconds is just the\n"
    " time since you started trying to land.  Thus you now know how\n"
    " to display information and what they mean.\n\n"
    " To control the ship, two controls are provided.  The first\n"
    " controls the rolling or turning of the ship.  This is accom-\n"
    " plished by four arrows just above the display menu.  Two point\n"
    " left and two point right.  The two pointing left mean rotate\n"
    " left and the two pointing right mean rotate right.  There is\n"
    " a big and a little one in each direction.  The big one means\n"
    " to rotate \"fast\" and the small one means to rotate \"slow\".\n"
    " Thus to rotate fast left, you point the mouse at left arrow.\n"
    " To rotate slow right, you point the mouse at the small arrow\n"
    " pointing to the right.  The arrow will get slightly brighter\n"
    " to indicate you have chosen it.  Above the arrow there is a\n"
    " bright, solid bar.  This bar is your throttle bar.  To its\n"
    " left there is a number in percent (say 50%%).  This number\n"
    " indicates the percentage of full thrust your rocket engine\n"
    " is developing.  The engine can develop anywhere from\n"
    " 10%% to 100%% thrust - full thrust is 10,500 pounds.  The\n"
    " engine thrust cannot fall below 10%%.  That is the way Grumman\n"
    " built it (actually the subcontractor).  To increase or decrease\n"
    " your thrust, you merely slide the mouse up and down the bar.\n"
    " The indicated percentage thrust will change accordingly.\n\n"
    " Now we come to actually flying the beast.  The module appears\n"
    " in the upper left hand corner of the screen and is traveling\n"
    " down and to the right.  Your job is to land at the correct\n"
    " spot (for the time being, we will say this is when the\n"
    " distance and height both reach zero).  The first picture you\n"
    " see, with the module in the upper left hand corner, is not\n"
    " drawn to scale (the module appears too big in relationship\n"
    " to the mountains).  Should you successfully get below around\n"
    " 400 feet altitude, the view will now change to a closeup\n"
    " view of the landing site, and everything will be in scale.\n"
    " Remember, it is not easy to land the first few times, but\n"
    " don't be disappointed, you'll do it.  Be careful, the game\n"
    " is extremely addictive.  It is also quite dynamic.\n\n"
    " Incorporated in the game are just about everything the GT40\n"
    " can do.  Letters, italics, light pen letters, a light bar,\n"
    " dynamic motion, various line types and intensities (the moon\n"
    " is not all the same brightness you know).  It also shows that\n"
    " the GT40 can do a lot of calculations while maintaining a\n"
    " reasonable display.\n\n"
    " There are three possible landing sites on the Moon:\n\n"
    "+1.  On the extreme left of the landscape\n\n"
    "+2.  A small flat area to the right of the mountains\n\n"
    "+3.  In the large \"flat\" area on the right\n\n"
    " Good Luck!\n\n"
    "2 Terminal Mode\n"
    " The GT40 LSI 11/05 configuration had a boot ROM which allowed the\n"
    " VR14 to be used as a very minimal dumb terminal in order to allow\n"
    " the user to login to the host computer.  It watches for the host\n"
    " computer to send a special character sequence to initiate download\n"
    " mode.  In download mode it loads a series of memory areas supplied\n"
    " by the host. The data sent in download mode is encoded with six bits\n"
    " per character.\n\n"
    " GT40 Terminal Mode is initiated by:\n\n"
    "++sim> set dli enable\n"
    "++sim> attach dli Line=0,Connect=ipaddr:port\n"
    "++sim> set vt enable\n"
    "++sim> boot -40 vt\n\n"
    "1 Implementation Status\n"
    " Clipping is not implemented properly for arcs.\n\n"
    " This simulation passes all four MAINDEC VS60 diagnostics and the\n"
    " DEC/X11 VS60 system exerciser, with the following exceptions:\n\n"
    " MD-11-DZVSA-A, VS60 instruction test part I, test 161:\n"
    " Failure to time-out an access to a \"nonexistent\" bus address, when the\n"
    " system is configured with so much memory that the probed address\n"
    " actually responds; this is a deficiency in the diagnostic itself.\n\n"
    " MD-11-DZVSB-A, VS60 instruction test part II:\n"
    " No exceptions.\n\n"
    " MD-11-DZVSC-B, VS60 instruction test part III, tests 107,110,111:\n"
    " Memory address test fails under SIMH, due to SIMH not implementing\n"
    " KT11 \"maintenance mode\", in which the final destination address (only)\n"
    " is relocated.  When SIMH is patched to fix this, the test still fails\n"
    " due to a bug in the diagnostic itself, namely a call to DPCONV1 which\n"
    " tests a condition code that is supposed to pertain to R0 but which\n"
    " hasn't been set up.  Swapping the two instructions before the call to\n"
    " DPCONV1 corrects this, and then this test passes.\n"
    " Graphic silo content tests fail, since the silo pipeline is not\n"
    " simulated; there are no plans to fix this, since it serves no other\n"
    " purpose in this simulation and would adversely affect performance.\n\n"
    " MD-11-DZVSD-B, VS60 visual display test, frame 13:\n"
    " \"O\" character sizes are slightly off, due to optimization for raster\n"
    " display rather than true stroking; there are no plans to change this.\n\n"
    " MD-11-DZVSE-A0, XXDP VS60 visual display exerciser:\n"
    " No visible exceptions.  Light-pen interrupts might not be handled\n"
    " right, since they're reported as errors and cause display restart.\n"
    " (XXX  Need to obtain source listing to check this.)\n\n"
;
return scp_help (st, dptr, uptr, flag, helpString, cptr);
}
#else  /* USE_DISPLAY not defined */
char pdp11_vt_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
