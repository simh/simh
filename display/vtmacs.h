/*
 * $Id: vtmacs.h,v 1.5 2005/01/12 18:10:13 phil Exp $
 * macros for coding a VT11/VS60 display file (instructions and data)
 * for standalone use of vt11.c (not embedded in PDP-11 simulator)
 * Douglas A. Gwyn <gwyn@arl.army.mil>
 * September 03, 2004
 *
 *      XXX -- assumes ASCII host character set
 */

/* helper macros (not for use outside this header): */
#define SGN_(x) ((x) < 0)
#define MAG_(x) ((x) >= 0 ? (x) : -(x)) /* -0 not expressible directly in C */
#if 0                                   /* manual seems to say this; wrong! */
#define JDL_(x) ((SGN_(raddr) << 8) | MAG_(raddr))
#else                                   /* sign extend, 9-bit twos complement */
#define JDL_(x) ((x) >= 0 ? (x) : ((~(unsigned)-(x))+1) & 0777)
#endif

/* control instructions: */

/* load status register A: */
#define LSRA(stop,stop_intr,lp_hit_chg,ital,refresh,menu) \
                0170000 | stop | stop_intr | lp_hit_chg | ital | refresh | menu
        /* display stop: */
#define         ST_SAME         00000   /* don't stop display */
#define         ST_STOP         02000   /* stop display */
        /* stop interrupt: */
#define         SI_SAME         00000   /* no change */
#define         SI_INHIBIT      01000   /* inhibit interrupt on stop */
#define         SI_GENERATE     01400   /* generate interrupt on stop */
        /* light pen hit intensify (bright-down on VS60): */
#define         LI_SAME         0000    /* no change */
#define         LI_INTENSIFY    0200    /* enable intensify on hit (VT11) */
#define         LI_BRIGHTDOWN   0200    /* enable bright down on hit (VS60) */
#define         LI_NOINTENSIFY  0300    /* inhibit intensify on hit (VT11) */
#define         LI_NOBRIGHTDOWN 0300    /* inhibit bright down on hit (VS60) */
        /* italic font: */
#define         IT_SAME         000     /* no change */
#define         IT_NORMAL       040     /* normal font */
#define         IT_ITALIC       060     /* italic font */
        /* refresh rate: */
#define         RF_UNSYNC       000     /* unsynchronized  */
#define         RF_SAME         000     /* (happens to work like that) */
#define         RF_LINE         004     /* sync with line (VT11) */
#define         RF_30           004     /* 30 frames/sec (VS60) */
#define         RF_40           010     /* 40 frames/sec (VS60) */
#define         RF_EXT          014     /* external sync (VS60) */
        /* menu/main area (VS60): */
#define         MN_SAME         0       /* no change */
#define         MN_MAIN         2       /* major screen area */
#define         MN_MENU         3       /* menu area */

/* load status register B: */
#define LSRB(color,set_step,step) \
                0174000 | color | set_step | (step)
        /* color select (VS60): */
#define         CL_SAME         00000   /* no change */
#define         CL_GREEN        01000   /* green */
#define         CL_YELLOW       01200   /* yellow */
#define         CL_ORANGE       01400   /* orange */
#define         CL_RED          01600   /* red */
        /* graphplot increment register change enable: */
#define         SS_SAME         0000    /* no change (step value ignored) */
#define         SS_CHANGE       0100    /* write step value into register */

/* load status register BB (VS60): */
#define LSRBB(z_data,edge_intr,depth_cue,char_esc) \
                0176000 | z_data | edge_intr | depth_cue | char_esc
        /* file Z data: */
#define         ZD_SAME         000     /* no change */
#define         ZD_NO           010     /* d.file does not contain Z coords. */
#define         ZD_YES          014     /* d.file contains Z coordinates */
        /* edge interrupts enable: */
#define         ED_SAME         000     /* no change */
#define         ED_DIS          040     /* disable intr. on edge transition */
#define         ED_ENA          060     /* enable intr. on edge transition */
        /* depth cue processing: */
#define         DQ_SAME         0000    /* no change */
#define         DQ_OFF          0200    /* disable depth cueing (Z intensity) */
#define         DQ_ON           0300    /* enable depth cueing (Z intensity) */
        /* escape on terminating character: */
#define         ES_SAME         0       /* no change */
#define         ES_NO           2       /* disable POPR on terminating char. */
#define         ES_YES          3       /* enable POPR on terminating char. */

/* load status register C (VS60): */
#define LSRC(rotate,cs_change,cscale,vs_change,vscale) \
                0154000 | rotate | cs_change | ((cscale)<<5) | \
                                   vs_change |  (vscale)
        /* character rotation: */
#define         RO_SAME         00000   /* no change */
#define         RO_HORIZONTAL   01000   /* no text rotation */
#define         RO_VERTICAL     01400   /* rotate text 90 degrees CCW */
        /* character scale change enable: */
#define         CS_SAME         0000    /* no change (cscale value ignored) */
#define         CS_CHANGE       0200    /* set character scale */
        /* vector scale change enable: */
#define         VS_SAME         000     /* no change (vscale value ignored) */
#define         VS_CHANGE       020     /* set vector scale */

/* load scope selection register (VS60): */
#define LSSR(console,disp,lp_intr,sw_intr) \
                0164000 | console | disp | lp_intr | sw_intr
        /* console to which this instruction applies: */
#define         CN_0            0000    /* console # 0 */
#define         CN_1            0400    /* console # 1 */
        /* display enable: */
#define         DS_SAME         0000    /* no change */
#define         DS_DIS          0200    /* disable display (blank CRT) */
#define         DS_ENA          0300    /* enable display (use CRT) */
        /* light-pen hit interrupt enable: */
#define         LH_SAME         0000    /* no change */
#define         LH_DIS          0040    /* light-pen hit interrupt disabled */
#define         LH_ENA          0060    /* light-pen hit interrupt enabled */
        /* tip-switch transition interrupt enable: */
#define         SW_SAME         0000    /* no change */
#define         SW_DIS          0010    /* tip-switch interrupt disabled */
#define         SW_ENA          0014    /* tip-switch hit interrupt enabled */

/* load name register (VS60): */
#define LNR(name) \
                0150000 | (name)

/* set graphic mode: */
#define SGM(mode,intens,lp_intr,blink,line_type)        \
                0100000 | mode | intens | lp_intr | blink | line_type
        /* graphic mode: */
#define         GM_CHAR         000000  /* character */
#define         GM_SVECT        004000  /* short vector */
#define         GM_LVECT        010000  /* long vector */
#define         GM_APOINT       014000  /* absolute point, or offset */
#define         GM_GRAPHX       020000  /* graphplot X, or basic long vector */
#define         GM_GRAPHY       024000  /* graphplot Y, or basic long vector */
#define         GM_RPOINT       030000  /* relative point */
#define         GM_BSVECT       034000  /* basic short vector */
#define         GM_ARC          040000  /* circle/arc */
#define         GM_AVECT        044000  /* absolute vector */
        /* intensity: */
#define         IN_SAME         00000   /* no change */
#define         IN_0            02000   /* intensity level 0 (dimmest) */
#define         IN_1            02200   /* intensity level 1 */
#define         IN_2            02400   /* intensity level 2 */
#define         IN_3            02600   /* intensity level 3 */
#define         IN_4            03000   /* intensity level 4 */
#define         IN_5            03200   /* intensity level 5 */
#define         IN_6            03400   /* intensity level 6 */
#define         IN_7            03600   /* intensity level 7 (brightest) */
        /* light pen interrupt: */
#define         LP_SAME         0000    /* no change */
#define         LP_DIS          0100    /* light-pen hit interrupt disabled */
#define         LP_ENA          0140    /* light-pen hit interrupt enabled */
        /* blink: */
#define         BL_SAME         000     /* no change */
#define         BL_OFF          020     /* blink off */
#define         BL_ON           030     /* blink on */
        /* line type: */
#define         LT_SAME         00      /* no change */
#define         LT_SOLID        04      /* solid */
#define         LT_LDASH        05      /* long dash */
#define         LT_SDASH        06      /* short dash */
#define         LT_DDASH        07      /* dot dash */

/* display jump absolute: */
#define DJMP_ABS(addr)  \
        0160000, \
        (addr) & ~1

/* display jump relative (VS60) [raddr in words]: */
#define DJMP_REL(raddr) \
        0161000 | JDL_(raddr)

/* display jump to subroutine absolute (VS60): */
#define DJSR_ABS(addr)  \
        0162000, \
        (addr) & ~1

/* display jump to subroutine relative (VS60) [raddr in words]: */
#define DJSR_REL(raddr) \
        0163000 | JDL_(raddr)

/* display no-op: */
#define DNOP    \
        0164000

/* display pop, no restore (VS60): */
#define DPOP_NR \
        0165000

/* display pop, restore (VS60): */
#define DPOP_R  \
        0165000

/* display stop: */
#define DSTOP   LSRA(ST_STOP,SI_SAME,LI_SAME,IT_SAME,RF_UNSYNC,MN_SAME)

/* graphic data: */

        /* intensify enable (common to all modes exept CHAR and OFFSET): */
#define         I_OFF           000000  /* beam off */
#define         I_ON            040000  /* beam on */

/* Note: when VS60 "file Z data" is enabled,
   use the *3() macros instead of the corresponding normal ones. */

/* character data: */
#define CHAR(c1,c2)     \
        ((c2) << 8) | (c1)      /* 7-bit ASCII assumed */

/* short vector data: */
#define SVECT(i,dx,dy)  \
        i | (SGN_(dx) << 13) | (MAG_(dx) << 7) | (SGN_(dy) << 6) | MAG_(dy)
#define SVECT3(i,dx,dy,dz)      \
        i | (SGN_(dx) << 13) | (MAG_(dx) << 7) | (SGN_(dy) << 6) | MAG_(dy), \
        (SGN_(dz) << 13) | (MAG_(dz) << 2)

/* long vector data: */
#define LVECT(i,dx,dy)  \
        i | (SGN_(dx) << 13) | MAG_(dx), \
        (SGN_(dy) << 13) | MAG_(dy)
#define LVECT3(i,dx,dy,dz)      \
        i | (SGN_(dx) << 13) | MAG_(dx), \
        (SGN_(dy) << 13) | MAG_(dy), \
        (SGN_(dz) << 13) | (MAG_(dz) << 2)

/* rotation data (VS60, probably unimplemented): */
#define ROTATE(i,a,b)   \
        i | (SGN_(a) << 13) | 010000 | MAG_(a), \
        (SGN_(b) << 13) | MAG_(b)
#define ROTATE3(i,a,b,c)        \
        i | (SGN_(a) << 13) | 010000 | MAG_(a), \
        (SGN_(b) << 13) | MAG_(b), \
        (SGN_(c) << 13) | (MAG_(c) << 2)

/* absolute point data: */
#define APOINT(i,x,y)   \
        i | (SGN_(x) << 13) | MAG_(x), \
        (SGN_(y) << 13) | MAG_(y)
#define APOINT3(i,x,y,z)        \
        i | (SGN_(x) << 13) | MAG_(x), \
        (SGN_(y) << 13) | MAG_(y), \
        (SGN_(z) << 13) | (MAG_(z) << 2)

/* offset data (VS60): */
#define OFFSET(x,y)     \
        (SGN_(x) << 13) | 010000 | MAG_(x), \
        (SGN_(y) << 13) | 010000 | MAG_(y)
#define OFFSET3(x,y,z)  \
        (SGN_(x) << 13) | 010000 | MAG_(x), \
        (SGN_(y) << 13) | 010000 | MAG_(y), \
        (SGN_(z) << 13) | 010000 | (MAG_(z) << 2)

/* graphplot X data: */
#define GRAPHX(i,x)     \
        i | (x)

/* graphplot Y data: */
#define GRAPHY(i,y)     \
        i | (y)

/* basic long vector data (VS60): */
#define BLVECT(i,dir,len)       \
        i | ((dir) << 11) | 02000 | (len)

/* relative point data: */
#define RPOINT(i,dx,dy) \
        i | (SGN_(dx) << 13) | (MAG_(dx) << 7) | (SGN_(dy) << 6) | MAG_(dy)
#define RPOINT3(i,dx,dy,dz)     \
        i | (SGN_(dx) << 13) | (MAG_(dx) << 7) | (SGN_(dy) << 6) | MAG_(dy), \
        (SGN_(dz) << 13) | (MAG_(dz) << 2)

/* basic short vector data (VS60): */
#define BSVECT(i,dir1,len1,dir2,len2)   \
        i | ((dir2) << 11) | ((len2) << 7) | ((dir1) << 4) | (len1)

/* circle/arc data (VS60, option): */
#define ARC(i,dcx,dcy,dex,dey)  \
        i | (SGN_(dcx) << 13) | MAG_(dcx), \
        (SGN_(dcy) << 13) | MAG_(dcy), \
        (SGN_(dex) << 13) | MAG_(dex), \
        (SGN_(dey) << 13) | MAG_(dey)
#define ARC3(i,dcx,dcy,cz,dex,dey,ez)   \
        i | (SGN_(dcx) << 13) | MAG_(dcx), \
        (SGN_(dcy) << 13) | MAG_(dcy), \
        (SGN_(cz) << 13) | (MAG_(cz) << 2), \
        (SGN_(dex) << 13) | MAG_(dex), \
        (SGN_(dey) << 13) | MAG_(dey), \
        (SGN_(ez) << 13) | (MAG_(ez) << 2)

/* absolute vector data (VS60): */
#define AVECT(i,x,y)    \
        i | (SGN_(x) << 13) | MAG_(x), \
        (SGN_(y) << 13) | MAG_(y)
#define AVECT3(i,x,y,z) \
        i | (SGN_(x) << 13) | MAG_(x), \
        (SGN_(y) << 13) | MAG_(y), \
        (SGN_(z) << 13) | (MAG_(z) << 2)
