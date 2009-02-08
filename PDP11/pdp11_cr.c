/* pdp11_cr.c: CR/CM/CD-11 card reader simulator

   Copyright (c) 2005-2007, John A. Dundas III
   Portions derived from work by Douglas W. Jones, jones@cs.uiowa.edu
   Portions derived from work by Robert M Supnik

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

   Except as contained in this notice, the name of the Author shall
   not be used in advertising or otherwise to promote the sale, use
   or other dealings in this Software without prior written
   authorization from the Author.

  ------------------------------------------------------------------------------

   cr        CR11/CD11 punched and mark sense card reader for SIMH
   The CR11 controller is also compatible with the CM11-F, CME11, and CMS11.

  Information necessary to create this simulation was gathered from
  a number of sources including:

    CR11 Card Reader System Manual, DEC-11-HCRB-D
      http://www.bitsavers.org/pdf/dec/unibus/DEC-11-HCRB-D_CR11_Mar72.pdf
    Various editions of the Peripherals Handbook
    OpenVMS VAX Card Reader, Line Printer, and LPA11-K I/O User's
      Reference Manual, AA-PVXGA-TE
    http://h71000.www7.hp.com/DOC/73final/documentation/pdf/OVMS_VAX_CARD_LP_REF.pdf
    OpenVMS System Manager's Manual, Volume 1: Essentials
      http://h71000.www7.hp.com/DOC/732FINAL/aa-pv5mh-tk/aa-pv5mh-tk.PDF
    CRDRIVER.LIS - CR11 Card Reader Driver, X-9, graciously made available
      by HP
    Various RSTS manuals
    RT-11 Software Support Manual
    RT-11 System Reference Manual, DEC-11-ORUGA-C-D
    Professor Douglas W. Jones's web site:
      http://www.cs.uiowa.edu/~jones/cards/
    Paul Mattes' x026 keypunch simulator
      http://x3270.bgp.nu/x026.html
    CD2SER.MAC - TOPS card reader driver source
      http://pdp-10.trailing-edge.com/custsupcuspmar86_bb-x130b-sb/02/cd2ser.mac

  The Card Image format code and documentation is adapted from Prof.
  Jones's site, with his permission.  Please see his site for additional
  documentation as well as the card image utilities referenced in
  his documentation (cardmake, cardlist, etc.).
    http://www.cs.uiowa.edu/~jones/cards/format.html

  Known limitations:
    1. Need a copy of the CR bootstrap (and some way to test it)
    2. Need a copy of the XXDP+ test deck
    3. No testing under RSX; volunteers needed
    4. No testing under Ultrix or Unix for PDP-11; volunteers needed
    5. No testing under Ultrix or Unix for VAX; volunteers needed
    6. The simulator implements a single controller/reader combination

  Operating System Notes

    RT-11 (and CTS-300) support one CR11 or CM11, but no CD11.

    VMS supports multiple CR11 controllers, but no CD11.

    RSTS/E supports either the CR11/CM11 or CD11 but not both in
    the same SIL.  It appears to support only one unit.

    For RSX there exists a CR/CM task handler.  Is there a CD
    handler?

    Don't have any information about Unix or Ultrix-11 yet.  Same
    for VAX Unices.

    TOPS: only the CD11 is supported, under the name CD20.

  Revision History:

   01-Feb-07    RMS     Added PDP-10 support
   12-May-06    JAD     Modify the DEBUG code to use the SIMH DEBUG_x
                        macros.  Modify the UNIT structure to include
                        the DEBUG bit.
                        Mark the trans[] array contents constant.
                        Make device data structures static and constant
                        as appropriate.
   18-Mar-05    JAD     Slight optimization for blank punches recognizing
                        that blank is 0 in all character encodings.
   17-Mar-05    JAD     Completely initialize ascii_code correctly.
                        Define the end of deck punch code separately from
                        the cardcode.i file.
                        Make initTranslation() set a pointer to the correct
                        punch code table to use.  Modify card read functions
                        to use this table pointer.
   16-Mar-05    JAD     Make certain switches passed to the ATTACH command
                        are valid; return error on any others.
                        Make default unit wait time compatible with default
                        device specification.
                        Implement SET TRANSLATION=value.  Still need to
                        modify the H2ASCII table used for text files;
                        currently hard-coded to 029.
   24-Feb-05    JAD     Allow the maintenance bits in CRM to clear as
                        well as set status bits.  Not sure this is the
                        correct behavior, though, without more documentation.
                        Catch three more places to spin down the blower
                        correctly.
                        Zero the CDDB and CRM at INIT.
   17-Feb-05    JAD     When the hopper empties, a pick check should
                        be generated 300ms later.  They are simultaneous
                        for now.
                        Make sure readColumnBinary() generates a complete
                        EOF card.
   08-Feb-05    JAD     Replace blowerWait with different times for blower
                        spin up and down.
   06-Feb-05    JAD     After DETACH: mark CD offline, set appropriate
                        blower state.
                        Make sure unit wait time is recalculated every
                        time cpm is set.
   04-Feb-05    JAD     Better tracking of blower state throughout driver.
                        Make sure IE gets cleared for CR at INIT.
                        Normalize error response in read routines.
                        Finish condition handling for column binary.
   02-Feb-05    JAD     Remove Qbus support; Unibus only.
                        Support ATTACH switches:
                        A - ASCII, B - column binary, I - Card Image
                        If none given, check for .TXT or .CBN; if none,
                        examine file for magic header.
                        Finer granularity to blower state.  Expose this
                        variable to examine/deposit from SIMH.
                        Preliminary implementation of support for
                        column binary format.
   24-Jan-05    JAD     Make AUTOEOF work as a surrogate for the EOF
                        button of a CD11 reader.  May need to separate
                        this later, though.
                        Partial implementation of DATAERR for CD11.
                        Implement the Rev. J mods (as best I understand
                        them) to the CD11 affecting the CDDB used as a
                        second status register.
   23-Jan-05    JAD     Preliminary clean-up of CD state transitions.
                        Tested with RSTS/E (V9.1-05).
   22-Jan-05    JAD     Finish CR state transitions; should be close now.
                        Tested with RSTS/E (V9.1-05), RT-11 (V5.3), and
                        VAX/VMS (V7.2).
   19-Jan-05    JAD     Add bounds to the RATE command; also default and
                        help a la the XQ driver.
                        Improved handling of empty files.
   17-Jan-05    JAD     Add the CR maintenance register.
   16-Jan-05    JAD     Add preliminary CD11 support.
                        Simulate the STOP and RESET switches.
   14-Jan-05    JAD     Add the ability to automatically generate an 'EOF'
                        card recognized by DEC operating systems when
                        reading ASCII files.
   08-Jan-05    JAD     Original creation and testing
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"
extern int32 int_req;
#define DFLT_DIS        (DEV_DIS)
#define DFLT_CR11       (0)                             /* CD11 only */
#define DFLT_CPM        1000

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
extern int32 int_req[IPL_HLVL];
#define DFLT_DIS        (0)
#define DFLT_CR11       (UNIT_CR11)
#define DFLT_CPM        285

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
extern int32 int_req[IPL_HLVL];
#define DFLT_DIS        (0)
#define DFLT_CR11       (UNIT_CR11)
#define DFLT_CPM        285
#endif

extern FILE *sim_deb;                                   /* sim_console.c */

/* create a int32 constant from four characters */
#define I4C(a,b,c,d)    (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define I4C_CBN         I4C ('C','B','N',' ')
#define I4C_H80         I4C ('H','8','0',' ')
#define I4C_H82         I4C ('H','8','2',' ')
#define I4C_H40         I4C ('H','4','0',' ')

#define    UNIT_V_CR11      (UNIT_V_UF + 0)
#define    UNIT_CR11        (1u << UNIT_V_CR11)
#define    UNIT_V_AUTOEOF   (UNIT_V_UF + 1)
#define    UNIT_AUTOEOF     (1u << UNIT_V_AUTOEOF)

#include <assert.h>
#define    ERROR            (00404)
#include "pdp11_cr_dat.h"
#define    PUNCH_EOD        (07417)
#define    PUNCH_SPACE      (0)                          /* same for all encodings */

/* CR */
/* also use CSR_ERR, CSR_IE, and CSR_GO */
#define    CRCSR_V_CRDDONE  14                          /* card done */
#define    CRCSR_V_SUPPLY   13                          /* supply error */
#define    CRCSR_V_RDCHK    12                          /* reader check */
#define    CRCSR_V_TIMERR   11                          /* timing error */
#define    CRCSR_V_ONLINE   10                          /* on line */
#define    CRCSR_V_BUSY     9                           /* busy reading */
#define    CRCSR_V_OFFLINE  8                           /* off line AKA READY? */
#define    CRCSR_V_COLRDY   7                           /* column ready */
#define    CRCSR_V_EJECT    1                           /* ignore card */

#define    CRCSR_CRDDONE    (1u << CRCSR_V_CRDDONE)
#define    CRCSR_SUPPLY     (1u << CRCSR_V_SUPPLY)
#define    CRCSR_RDCHK      (1u << CRCSR_V_RDCHK)
#define    CRCSR_TIMERR     (1u << CRCSR_V_TIMERR)
#define    CRCSR_ONLINE     (1u << CRCSR_V_ONLINE)
#define    CRCSR_BUSY       (1u << CRCSR_V_BUSY)
#define    CRCSR_OFFLINE    (1u << CRCSR_V_OFFLINE)
#define    CRCSR_COLRDY     (1u << CRCSR_V_COLRDY)
#define    CRCSR_EJECT      (1u << CRCSR_V_EJECT)

#define CRCSR_IMP           (CSR_ERR | CRCSR_CRDDONE | CRCSR_SUPPLY |    \
                             CRCSR_RDCHK | CRCSR_TIMERR | CRCSR_ONLINE | \
                             CRCSR_BUSY | CRCSR_OFFLINE | CRCSR_COLRDY | \
                             CSR_IE | CRCSR_EJECT)
#define CRCSR_RW            (CSR_IE | CRCSR_EJECT | CSR_GO)    /* read/write */

#define    CRM_V_MAINT      15                          /* enable maint funct */
#define    CRM_V_BUSY       14
#define    CRM_V_READY      13
#define    CRM_V_HOPPER     12

#define    CRM_MAINT        (1u << CRM_V_MAINT)
#define    CRM_BUSY         (1u << CRM_V_BUSY)
#define    CRM_READY        (1u << CRM_V_READY)
#define    CRM_HOPPER       (1u << CRM_V_HOPPER)

/* CD */
/* also use CSR_ERR, CSR_IE, and CSR_GO */
#define    CDCSR_V_RDRCHK   14                          /* reader check */
#define    CDCSR_V_EOF      13                          /* CD11-E EOF button */
#define    CDCSR_V_OFFLINE  12                          /* off line */
#define    CDCSR_V_DATAERR  11                          /* data error */
#define    CDCSR_V_LATE     10                          /* data late */
#define    CDCSR_V_NXM      9                           /* non-existent memory */
#define    CDCSR_V_PWRCLR   8                           /* power clear */
#define    CDCSR_V_RDY      7                           /* ready */
#define    CDCSR_V_XBA17    5
#define    CDCSR_V_XBA16    4
#define    CDCSR_V_ONLINE   3                           /* on line transition */
#define    CDCSR_V_HOPPER   2                           /* hopper check */
#define    CDCSR_V_PACK     1                           /* data packing */

#define    CDCSR_RDRCHK     (1u << CDCSR_V_RDRCHK)
#define    CDCSR_EOF        (1u << CDCSR_V_EOF)
#define    CDCSR_OFFLINE    (1u << CDCSR_V_OFFLINE)
#define    CDCSR_DATAERR    (1u << CDCSR_V_DATAERR)
#define    CDCSR_LATE       (1u << CDCSR_V_LATE)
#define    CDCSR_NXM        (1u << CDCSR_V_NXM)
#define    CDCSR_PWRCLR     (1u << CDCSR_V_PWRCLR)
#define    CDCSR_RDY        (1u << CDCSR_V_RDY)
#define    CDCSR_XBA17      (1u << CDCSR_V_XBA17)
#define    CDCSR_XBA16      (1u << CDCSR_V_XBA16)
#define    CDCSR_ONLINE     (1u << CDCSR_V_ONLINE)
#define    CDCSR_HOPPER     (1u << CDCSR_V_HOPPER)
#define    CDCSR_PACK       (1u << CDCSR_V_PACK)

#define    CDCSR_IMP        (CSR_ERR | CDCSR_RDRCHK | CDCSR_EOF | CDCSR_OFFLINE | \
                             CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM | \
                             CDCSR_PWRCLR | CDCSR_RDY | CSR_IE | \
                             CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_ONLINE | \
                             CDCSR_HOPPER | CDCSR_PACK | CSR_GO)

#define    CDCSR_RW         (CDCSR_PWRCLR | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | \
                             CDCSR_PACK | CSR_GO)

/* Blower state values */
#define    BLOW_OFF         (0)                         /* steady state off */
#define    BLOW_START       (1)                         /* starting up */
#define    BLOW_ON          (2)                         /* steady state on */
#define    BLOW_STOP        (3)                         /* shutting down */

/* Card Reader state */
static char     *cardFormat = "unknown";
static t_bool   (*readRtn)(FILE *, int16 *, char *, char *);
static char     ascii_code[4096];                       /* 2^12 possible values */
static int      currCol;                                /* current column when reading */
static int      colStart;                               /* starting column */
static int      colEnd;                                 /* ending column */
static int      table = 3;                              /* character translation table */
static const int *codeTbl = o29_code;                   /* punch translation table */
static int32    blowerState = BLOW_OFF;                 /* reader vacuum/blower motor */
static int32    spinUp = 3000;                          /* blower spin-up time: 3 seconds */
static int32    spinDown = 2000;                        /* blower spin-down time: 2 seconds */
static t_bool   EOFcard = FALSE;                        /* played special card yet? */
static int32    cpm = DFLT_CPM;                         /* reader rate: cards per minute */
/* card image in various formats */
static int16    hcard[82];                              /* Hollerith format */
static char     ccard[82];                              /* DEC compressed format */
static char     acard[82];                              /* ASCII format */
/* CR/CM registers */
static int32    crs = 0;                                /* control/status */
static int32    crb1 = 0;                               /* 12-bit Hollerith characters */
static int32    crb2 = 0;                               /* 8-bit compressed characters */
static int32    crm = 0;                                /* CMS maintenance register */
/* CD registers */
static int32    cdst = 0;                               /* control/status */
static int32    cdcc = 0;                               /* column count */
static int32    cdba = 0;                               /* current address, low 16 bits */
static int32    cddb = 0;                               /* data, 2nd status */

/* forward references */
DEVICE cr_dev;
static void setupCardFile (UNIT *, int32);
t_stat cr_rd (int32 *, int32, int32);
t_stat cr_wr (int32, int32, int32);
t_stat cr_svc (UNIT *);
t_stat cr_reset (DEVICE *);
t_stat cr_attach (UNIT *, char *);
t_stat cr_detach (UNIT *);
t_stat cr_set_type (UNIT *, int32, char *, void *);
t_stat cr_show_format (FILE *, UNIT *, int32, void *);
t_stat cr_set_rate (UNIT *, int32, char *, void *);
t_stat cr_show_rate (FILE *, UNIT *, int32, void *);
t_stat cr_set_reset (UNIT *, int32, char *, void *);
t_stat cr_set_stop (UNIT *, int32, char *, void *);
t_stat cr_set_trans (UNIT *, int32, char*, void *);
t_stat cr_show_trans (FILE *, UNIT *, int32, void *);

/* CR data structures

   cr_dib   CR device information block
   cr_unit  CR unit descriptor
   cr_reg   CR register list
   cr_mod   CR modifier table
   cr_dev   CR device descriptor
*/

static DIB cr_dib = { IOBA_CR, IOLN_CR, &cr_rd, &cr_wr,
        1, IVCL (CR), VEC_CR, { NULL } };

static UNIT cr_unit = {
    UDATA (&cr_svc,
      UNIT_ATTABLE+UNIT_SEQ+UNIT_ROABLE+UNIT_DISABLE+
      DFLT_CR11+UNIT_AUTOEOF, 0),
        (60 * 1000) / DFLT_CPM };

static const REG cr_reg[] = {
    { GRDATA (BUF, cr_unit.buf, DEV_RDX, 8, 0) },
    { GRDATA (CRS,  crs,  DEV_RDX, 16, 0) },
    { GRDATA (CRB1, crb1, DEV_RDX, 16, 0) },
    { GRDATA (CRB2, crb2, DEV_RDX, 16, 0) },
    { GRDATA (CRM,  crm,  DEV_RDX, 16, 0) },
    { GRDATA (CDST, cdst, DEV_RDX, 16, 0) },
    { GRDATA (CDCC, cdcc, DEV_RDX, 16, 0) },
    { GRDATA (CDBA, cdba, DEV_RDX, 16, 0) },
    { GRDATA (CDDB, cddb, DEV_RDX, 16, 0) },
        { GRDATA (BLOWER, blowerState, DEV_RDX, 2, 0) },
    { FLDATA (INT, IREQ (CR), INT_V_CR) },
    { FLDATA (ERR, crs, CSR_V_ERR) },
    { FLDATA (IE, crs, CSR_V_IE) },
    { DRDATA (POS, cr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, cr_unit.wait, 24), PV_LEFT },
    { GRDATA (DEVADDR, cr_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, cr_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }  };

static const MTAB cr_mod[] = {
#if defined (VM_PDP11)
    { UNIT_CR11, UNIT_CR11, "CR11", "CR11", &cr_set_type },
    { UNIT_CR11,         0, "CD11", "CD11", &cr_set_type },
#else
    { UNIT_CR11, UNIT_CR11, "CR11", NULL },
    { UNIT_CR11,         0, "CD11", NULL },
#endif
    { UNIT_AUTOEOF, UNIT_AUTOEOF, "auto EOF", "AUTOEOF", NULL },
    { UNIT_AUTOEOF,            0, "no auto EOF", "NOAUTOEOF", NULL },
    /* card reader RESET switch */
    { MTAB_XTD|MTAB_VDV, 0, NULL, "RESET",
        &cr_set_reset, NULL, NULL },
    /* card reader STOP switch */
    { MTAB_XTD|MTAB_VDV, 0, NULL, "STOP",
        &cr_set_stop, NULL, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", NULL,
        NULL, &cr_show_format, NULL },
    { MTAB_XTD|MTAB_VDV, 006, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
        &set_vec, &show_vec, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "RATE", "RATE={DEFAULT|200..1200}",
        &cr_set_rate, &cr_show_rate, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "TRANSLATION",
        "TRANSLATION={DEFAULT|026|026FTN|029|EBCDIC}",
        &cr_set_trans, &cr_show_trans, NULL },
    { 0 }  };

DEVICE cr_dev = {
    "CR", &cr_unit, (REG *)  &cr_reg, (MTAB *) &cr_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &cr_reset,
    NULL, &cr_attach, &cr_detach,
    &cr_dib, DEV_DISABLE | DFLT_DIS | DEV_UBUS | DEV_DEBUG };

/* Utility routines */

/*
These functions read a "card" from a virtual deck file (passed in
fp) and fill in three arrays.  The first array 'hcard' contains the
12-bit binary image of the punch in each column; the second array
'ccard' contains the 8-bit DEC encoded representation of the
corresponding column; the third array 'acard' contains the ASCII
representation (if possible) of the character.  The routines return
TRUE if a card was read (possibly with errors) and FALSE if the
"hopper is empty" (EOF) or fatal file errors prevented any portion
of a card from being read.

Errors other than EOF are signaled out of band in the controller
state variables.  Possible errors are data in columns 0 or 81
(signalled as read check; currently these columns are ignored), or
any file errors (signalled as motion check).

Might rethink this.  Should probably treat file errors as "pick
check".  Retry 3 times.  After that, give up with error.

*/

static t_bool readCardImage (   FILE    *fp,
                                int16   *hcard,
                                char    *ccard,
                                char    *acard    )
{
    int    c1, c2, c3, col;

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "readCardImage pos %d\n", (int) ftell (fp));
    /* get card header bytes */
    c1 = fgetc (fp);
    c2 = fgetc (fp);
    c3 = fgetc (fp);
    cr_unit.pos = ftell (fp);
    /* check for EOF */
    if (c1 == EOF) {
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "hopper empty\n");
        if (!EOFcard && (cr_unit.flags & UNIT_AUTOEOF)) {
            EOFcard = TRUE;
            for (col = 1; col <= 8; col++) {
                hcard[col] = PUNCH_EOD;
                ccard[col] = h2c_code[PUNCH_EOD];
                acard[col] = ' ';
            }
            while (col <= colEnd) {
                hcard[col] = PUNCH_SPACE;
                ccard[col] = PUNCH_SPACE;
                acard[col] = ' ';
                col++;
            }
            return (TRUE);
        }
        crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY | CRCSR_OFFLINE;
        crs &= ~(CRCSR_COLRDY | CRCSR_ONLINE);
        cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
        if (cr_unit.flags & UNIT_AUTOEOF)
            cdst |= CDCSR_EOF;
        blowerState = BLOW_STOP;
        return (FALSE);
    }
    /* check for valid header */
    if ((c2 == EOF) || (c3 == EOF) || ((c1 & 0x80) == 0) ||
        ((c2 & 0x80) == 0) || ((c3 & 0x80) == 0)) {
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "header error\n");
        /* unexpected EOF or format problems */
        crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_OFFLINE;
        crs &= ~CRCSR_ONLINE;
        cdst |= CSR_ERR | CDCSR_RDRCHK;
        blowerState = BLOW_STOP;
        return (FALSE);
    }
    assert (colStart < colEnd);
    assert (colStart >= 0);
    assert (colEnd <= 81);
    for (col = colStart; col < colEnd; ) {
        int16    i;
        /* get 3 bytes */
        c1 = fgetc (fp);
        c2 = fgetc (fp);
        c3 = fgetc (fp);
        cr_unit.pos = ftell (fp);
        if (ferror (fp) || feof (fp)) {
            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "file error\n");
/* signal error; unexpected EOF, format problems, or file error(s) */
            crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_OFFLINE;
            crs &= ~CRCSR_ONLINE;
            cdst |= CSR_ERR | CDCSR_RDRCHK;
            blowerState = BLOW_STOP;
            return (FALSE);
        }
        /* convert to 2 columns */
        i = ((c1 << 4) | ( c2 >> 4)) & 0xFFF;
        hcard[col] = i;
        ccard[col] = h2c_code[i];
        acard[col] = ascii_code[i];
        col++;

        i = (((c2 & 017) << 8) | c3) & 0xFFF;
        hcard[col] = i;
        ccard[col] = h2c_code[i];
        acard[col] = ascii_code[i];
        col++;
    }
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "successfully loaded card\n");
    return (TRUE);
}

static t_bool readColumnBinary (    FILE    *fp,
                                    int16   *hcard,
                                    char    *ccard,
                                    char    *acard    )
{
    int    col;

    for (col = colStart; col <= colEnd; col++) {
        int16    i;
        i = fgetc (fp) & 077;
        i |= ((fgetc (fp) & 077) << 6);
        cr_unit.pos = ftell (fp);
        if (feof (fp)) {
            if (!EOFcard && (cr_unit.flags & UNIT_AUTOEOF)) {
                EOFcard = TRUE;
                for (col = 1; col <= 8; col++) {
                    hcard[col] = PUNCH_EOD;
                    ccard[col] = h2c_code[PUNCH_EOD];
                    acard[col] = ' ';
                }
                while (col <= colEnd) {
                    hcard[col] = PUNCH_SPACE;
                    ccard[col] = PUNCH_SPACE;
                    acard[col] = ' ';
                    col++;
                }
                return (TRUE);
            }
            crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY |
                   CRCSR_OFFLINE;
            crs &= ~(CRCSR_COLRDY | CRCSR_ONLINE);
            cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
            if (cr_unit.flags & UNIT_AUTOEOF)
                cdst |= CDCSR_EOF;
            blowerState = BLOW_STOP;
            return (FALSE);
        }
        if (ferror (fp)) {
            /* signal error */
            crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_OFFLINE;
            crs &= ~CRCSR_ONLINE;
            cdst |= CSR_ERR | CDCSR_RDRCHK;
            blowerState = BLOW_STOP;
            return (FALSE);
        }
        hcard[col] = i;
        ccard[col] = h2c_code[i];
        acard[col] = ascii_code[i];
    }
    return (TRUE);
}

/*

Should this routine perform special handling of non-printable,
(e.g., control) characters or characters that have no encoded
representation?

*/

static t_bool readCardASCII (   FILE    *fp,
                                int16   *hcard,
                                char    *ccard,
                                char    *acard    )
{
    int    c, col;

    assert (colStart < colEnd);
    assert (colStart >= 1);
    assert (colEnd <= 80);

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "readCardASCII\n");
    for (col = colStart; col <= colEnd; ) {
        switch (c = fgetc (fp)) {
        case EOF:
            if (ferror (fp)) {
                /* signal error */
                crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_OFFLINE;
                crs &= ~CRCSR_ONLINE;
                cdst |= CSR_ERR | CDCSR_RDRCHK;
                blowerState = BLOW_STOP;
                cr_unit.pos = ftell (fp);
                return (FALSE);
            }
            if (col == colStart) {
                if (DEBUG_PRS (cr_dev))
                    fprintf (sim_deb, "hopper empty\n");
                if (!EOFcard && (cr_unit.flags & UNIT_AUTOEOF)) {
                    EOFcard = TRUE;
                    for (col = 1; col <= 8; col++) {
                        hcard[col] = PUNCH_EOD;
                        ccard[col] = h2c_code[PUNCH_EOD];
                        acard[col] = ' ';
                    }
                    c = '\n';
                    goto fill_card;
                }
                crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY | CRCSR_OFFLINE;
                crs &= ~(CRCSR_COLRDY | CRCSR_ONLINE);
                cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
                if (cr_unit.flags & UNIT_AUTOEOF)
                    cdst |= CDCSR_EOF;
                blowerState = BLOW_STOP;
                cr_unit.pos = ftell (fp);
                return (FALSE);
            }
            /* fall through */
        case '\r':
        case '\n':
        fill_card:
            while (col <= colEnd) {
                hcard[col] = PUNCH_SPACE;
                ccard[col] = PUNCH_SPACE;
                acard[col] = ' ';
                col++;
            }
            break;
        case '\t':
            do {
                hcard[col] = PUNCH_SPACE;
                ccard[col] = PUNCH_SPACE;
                acard[col] = ' ';
                col++;
            } while (((col & 07) != 1) && (col <= colEnd));
            break;
        default:
            hcard[col] = codeTbl[c & 0177];
            /* check for unrepresentable ASCII characters */
            if (hcard[col] == ERROR) {
                cdst |= CDCSR_DATAERR;
                if (DEBUG_PRS (cr_dev))
                    fprintf (sim_deb,
                        "error character at column %d\n",
                        col);
            }
            ccard[col] = h2c_code[hcard[col]];
            acard[col] = c;
            col++;
            break;
        }
    }
    /* silently truncate/flush long lines, or flag over-length card? */
    if (c != '\n') {
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "truncating card\n");
        do c = fgetc (fp);
            while ((c != EOF) && (c != '\n') && (c != '\r'));
    }
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "successfully loaded card\n");
    cr_unit.pos = ftell (fp);
    return (TRUE);
}

/*

Initialize the binary translation table.  Generally called when a
new deck is attached but could be set manually as well.

*/

static void initTranslation (void)
{
    int32    i;

    memset (ascii_code, '~', sizeof (ascii_code));
     switch (table) {
    case 1:
        codeTbl = o26_comm_code;
        for (i = ' '; i < '`'; i++)
            ascii_code[o26_comm_code[i]] = i;
        break;
    case 2:
        codeTbl = o26_ftn_code;
        for (i = ' '; i < '`'; i++)
            ascii_code[o26_ftn_code[i]] = i;
        break;
    case 3:
        codeTbl = o29_code;
        for (i = ' '; i < '`'; i++)
            ascii_code[o29_code[i]] = i;
        break;
    case 4:
        codeTbl = EBCDIC_code;
        for (i = 0; i < 0177; i++)
            ascii_code[EBCDIC_code[i]] = i;
        break;
    default:
        /* can't happen */
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb,
                "bad CR translation initialization value\n");
        break;
    }
}

/*

Examine the command switches, file extension, and virtual card deck
file to determine the format.  Set up the global variables
appropriately.  Rewind ASCII files to the beginning

*/

static void setupCardFile ( UNIT    *uptr,
                            int32   switches    )
{
    int32    i;

    if (switches & SWMASK ('A'))
        i = 0;
    else if (switches & SWMASK ('B'))
        i = I4C_CBN;
    else if (switches & SWMASK ('I'))
        goto read_header;
    else if (match_ext (uptr->filename, "TXT"))
        i = 0;
    else if (match_ext (uptr->filename, "CBN"))
        i = I4C_CBN;
    else {
read_header:
        /* look for card image magic file number */
        i = fgetc (uptr->fileref);
        i = (i << 8) | fgetc (uptr->fileref);
        i = (i << 8) | fgetc (uptr->fileref);
        i = (i << 8) | ' ';
    }
    switch (i) {
    case I4C_H80:
        colStart = 1;
        colEnd = 80;
        cardFormat = "card image";
        readRtn = readCardImage;
        break;
    case I4C_H82:
        colStart = 0;
        colEnd = 81;
        cardFormat = "card image";
        readRtn = readCardImage;
        break;
    case I4C_H40:
        colStart = 1;
        colEnd = 40;
        cardFormat = "card image";
        readRtn = readCardImage;
        break;
    case I4C_CBN:
        colStart = 1;
        colEnd = 80;
        cardFormat = "column binary";
        readRtn = readColumnBinary;
        break;
    default:
        colStart = 1;
        colEnd = 80;
        cardFormat = "ASCII";
        readRtn = readCardASCII;
        fseek (uptr->fileref, 0L, SEEK_SET);
        break;
    }
    initTranslation ();
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "colStart = %d, colEnd = %d\n",
            colStart, colEnd);
    cr_unit.pos = ftell (uptr->fileref);
}

/* Card reader routines

   cr_rd        I/O page read
   cr_wr        I/O page write
   cr_svc       process event (reader ready)
   cr_reset     process reset
   cr_attach    process attach
   cr_detach    process detach
*/

t_stat cr_rd (  int32   *data,
                int32   PA,
                int32   access    )
{
    switch ((PA >> 1) & 03) {
    case 0:        /* CSR */
        if (cdst & (077000))
            cdst |= CSR_ERR;
        else
            cdst &= ~CSR_ERR;
        *data = (cr_unit.flags & UNIT_CR11) ?
            crs & CRCSR_IMP : cdst & CDCSR_IMP;
        /* CR: if error removed, clear 15, 14, 11, 10 */
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_rd crs %06o cdst %06o\n",
                crs, cdst);
        break;
    case 1:
        *data = (cr_unit.flags & UNIT_CR11) ? crb1 : cdcc;
        /* Does crb1 clear after read? Implied by VMS driver. */
        crb1 = 0;
        crs &= ~CRCSR_COLRDY;
        if (DEBUG_PRS (cr_dev)) {
            if (cr_unit.flags & UNIT_CR11)
                fprintf (sim_deb, "cr_rd crb1 %06o '%c' %d\n",
                    crb1, cr_unit.buf, cr_unit.buf);
            else
                fprintf (sim_deb, "cr_rd cdcc %06o\n", cdcc);
        }
        break;
    case 2:
        *data = (cr_unit.flags & UNIT_CR11) ? crb2 : cdba;
        crb2 = 0;    /* see note for crb1 */
        crs &= ~CRCSR_COLRDY;
        if (DEBUG_PRS (cr_dev)) {
            if (cr_unit.flags & UNIT_CR11)
                fprintf (sim_deb, "cr_rd crb2 %06o\n", crb2);
            else
                fprintf (sim_deb, "\r\ncr_rd cdba %06o\n", cdba);
        }
        break;
    case 3:
    default:
        if (cr_unit.flags & UNIT_CR11)
            *data = crm;
        else
            *data = 0100000 | (cdst & CDCSR_RDRCHK) |
                 (cdst & CDCSR_OFFLINE) ?
                cddb & 0777 : 0777;
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_rd crm %06o cddb %06o data %06o\n",
                crm, cddb, *data);
        break;
    }
    return (SCPE_OK);
}

t_stat cr_wr (  int32   data,
                int32   PA,
                int32   access    )
{
    switch ((PA >> 1) & 03) {
    case 0:
        if (cr_unit.flags & UNIT_CR11) {
            /* ignore high-byte writes */
            if (PA & 1)
                break;
            /* fixup data for low byte write */
            if (access == WRITEB)
                data = (crs & ~0377) | (data & 0377); 
            if (!(data & CSR_IE))
                CLR_INT (CR);
            crs = (crs & ~CRCSR_RW) | (data & CRCSR_RW);
            crs &= ~(CSR_ERR | CRCSR_CRDDONE | CRCSR_TIMERR);
            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "cr_wr data %06o crs %06o\n",
                    data, crs);
            if (data & CSR_GO) {
                if (blowerState != BLOW_ON) {
                    sim_activate (&cr_unit, spinUp);
                    blowerState = BLOW_START;
                } else
                    sim_activate (&cr_unit, cr_unit.wait);
            }
        } else {
            if (data & CDCSR_PWRCLR) {
                CLR_INT (CR);
                sim_cancel (&cr_unit);
                cdst &= ~(CDCSR_RDRCHK |CDCSR_OFFLINE |
                      CDCSR_RDY | CDCSR_HOPPER);
                cdst |= CDCSR_RDY;
                cdcc = 0;
                cdba = 0;
                break;
            }
            if (!(data & CSR_IE))
                CLR_INT (CR);
            cdst = (cdst & ~CDCSR_RW) | (data & CDCSR_RW);
            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "cr_wr data %06o cdst %06o\n",
                    data, cdst);
            if (data & CSR_GO) {
                if (blowerState != BLOW_ON) {
                    sim_activate (&cr_unit, spinUp);
                    blowerState = BLOW_START;
                } else
                    sim_activate (&cr_unit, cr_unit.wait);
            }
        }
        break;
    case 1:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr cdcc %06o\n", data);
        if (cr_unit.flags & UNIT_CR11)
            break;
        cdcc = data & 0177777;
        break;
    case 2:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr crba %06o\n", data);
        if (cr_unit.flags & UNIT_CR11)
            break;
        cdba = data & 0177777;
        break;
    case 3:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr cddb/crm %06o\n", data);
        /* ignore writes to cddb */
        if (!(cr_unit.flags & UNIT_CR11))
            break;
        /* fixup data for byte writes and read-modify-write */
        if (access == WRITEB)
            data = (PA & 1) ?
                (crm & 0377) | (data << 8) :
                (crm & ~0377) | (data & 0377);
        crm = data & 0177777;
        /* not 100% certain how these work */
        if (!(crm & CRM_MAINT))
            break;
        crs = (crm & CRM_BUSY) ?
            (crs | CRCSR_BUSY) : (crs & ~CRCSR_BUSY);
        crs = (crm & CRM_READY) ?
            (crs | CRCSR_OFFLINE) : (crs & ~CRCSR_OFFLINE);
        crs = (crm & CRM_HOPPER) ?
            (crs | CRCSR_SUPPLY | CRCSR_RDCHK) :
            (crs & ~(CRCSR_SUPPLY | CRCSR_RDCHK));
        crb1 = crm & 07777;    /* load low 12 bits */
        break;
    default:
        /* can't happen */
        break;
    }
    return (SCPE_OK);
}

/*
Enter the service routine once for each column read from the card.
CR state bits drive this primarily (see _BUSY and _CRDDONE).  However,
when in CD mode, also execute one column of DMA input.

*/

t_stat cr_svc ( UNIT    *uptr    )
{
    uint32    pa;
    uint8    c;
    uint16    w;

    if (blowerState == BLOW_STOP) {
        blowerState = BLOW_OFF;
        return (SCPE_OK);
    }
    if (blowerState == BLOW_START)
        blowerState = BLOW_ON;
    /* (almost) anything we do now will cause a CR interrupt */
    if (crs & CSR_IE)
        SET_INT (CR);
    if (!(uptr->flags & UNIT_ATT) || (crs & CSR_ERR) || (cdst & CSR_ERR))
        return (SCPE_OK);
    if ((crs & CRCSR_BUSY) && (currCol > colEnd)) {
        crs &= ~(CRCSR_BUSY | CSR_GO | CRCSR_COLRDY);
        crs |= CRCSR_CRDDONE;
        if (cdst & (CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM))
            cdst |= CSR_ERR;
        if (cdst & CSR_IE)
            SET_INT (CR);
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_svc card done\n");
        return (SCPE_OK);
    }
    if (!(crs & CRCSR_BUSY)) {
        /* try to read a card */
        /* crs &= ~CRCSR_CRDDONE; */
        if (!readRtn (uptr->fileref, hcard, ccard, acard)) {
            sim_activate (uptr, spinDown);
            return (SCPE_OK);
        }
        currCol = colStart;
        crs |= CRCSR_BUSY;    /* indicate reader busy */
    }
    /* check for overrun (timing error) */
    if ((uptr->flags & UNIT_CR11) && (crs & CRCSR_COLRDY))
        crs |= CSR_ERR | CRCSR_TIMERR;
    crb1 = hcard[currCol] & 07777;
    crb2 = ccard[currCol] & 0377;
    uptr->buf = acard[currCol] & 0377;    /* helpful for debugging */
    if (!(uptr->flags & UNIT_CR11)) {
        pa = cdba | ((cdst & 060) << 12);
/*
The implementation of _NXM here is not quite the same as I interpret
the (limited) documentaiton I have to indicate.  However the effect
should be similar.  Documentation indicates that once _NXM is set,
further NPR requests are inhibited though the card is allowed to
read until completion.  This implies that CDBA and the XBA bits are
incremented accordingly, even though no data transfer occurs.  This
code detects and flags the NXM condition but allows attempts at
subsequent memory writes, thus insuring the address registers are
incremented properly.  If this causes problems, I'll fix it.
*/
        if (cdst & CDCSR_PACK) {
            c = cddb = ccard[currCol] & 0377;
            if (Map_WriteB (pa, 1, &c))
                cdst |= CDCSR_NXM;
            pa = (pa + 1) & 0777777;
        } else {
            w = cddb = hcard[currCol] & 07777;
            if (Map_WriteW (pa, 2, &w))
                cdst |= CDCSR_NXM;
            pa = (pa + 2) & 0777777;
        }
        cdba = pa & 0177777;
        cdst = (cdst & ~(CDCSR_XBA17|CDCSR_XBA16)) |
            ((pa & 0600000) >> 12);
        cdcc = (cdcc + 1) & 0177777;
#if    0
        if (!(cdst & CSR_IE) && !(crs & CRCSR_CRDDONE))
            CLR_INT (CR);
#endif
    }
    currCol++;    /* advance the column counter */
    if (!(crs & CRCSR_EJECT))
        crs |= CRCSR_COLRDY;
    else
        CLR_INT (CR);
    sim_activate (uptr, uptr->wait);
    return (SCPE_OK);
}

t_stat cr_reset (   DEVICE  *dptr    )
{
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "cr_reset\n");
    cr_unit.buf = 0;
    currCol = 1;
    crs &= ~(CSR_ERR|CRCSR_CRDDONE|CRCSR_TIMERR|CRCSR_ONLINE|CRCSR_BUSY|
         CRCSR_COLRDY|CSR_IE|CRCSR_EJECT|CSR_GO);
    crb1 = 0;
    crb2 = 0;
    crm = 0;
    cdst &= ~(CSR_ERR|CDCSR_RDRCHK|CDCSR_EOF|CDCSR_DATAERR|CDCSR_LATE|
          CDCSR_NXM|CSR_IE|CDCSR_XBA17|CDCSR_XBA16|CDCSR_ONLINE|
          CDCSR_PACK|CSR_GO);
    cdst |= CDCSR_RDY;
    cdcc = 0;
    cdba = 0;
    cddb = 0;
    if ((cr_unit.flags & UNIT_ATT) && !feof (cr_unit.fileref)) {
        crs |= CRCSR_ONLINE;    /* non-standard */
        crs &= ~(CRCSR_RDCHK | CRCSR_SUPPLY | CRCSR_OFFLINE);
        cdst &= ~(CDCSR_RDRCHK | CDCSR_HOPPER);
    } else {
        cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
        crs = CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY | CRCSR_OFFLINE;
    }
    sim_cancel (&cr_unit);        /* deactivate unit */
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
        sim_activate (&cr_unit, spinDown);
    }
    EOFcard = FALSE;
    CLR_INT (CR);
    /* TBD: flush current card */
    /* init uptr->wait ? */
    return (SCPE_OK);
}

/*
Handle the interface status and SIMH portion of the ATTACH.  Another
routine is used to evaluate the file and initialize other state
globals correctly.
*/

#define    MASK    (SWMASK('A')|SWMASK('B')|SWMASK('I')|SWMASK('R'))

t_stat cr_attach (  UNIT    *uptr,
                    char    *cptr    )
{
    t_stat        reason;
    extern int32    sim_switches;

    if (sim_switches & ~MASK)
        return (SCPE_INVSW);
    /* file must previously exist; kludge */
    sim_switches |= SWMASK ('R');
    reason = attach_unit (uptr, cptr);
    if (!(uptr->flags & UNIT_ATT)) {
        crs &= ~CRCSR_ONLINE;
        crs |= CSR_ERR | CRCSR_OFFLINE | CRCSR_RDCHK | CRCSR_SUPPLY;
        cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
    } else {
        setupCardFile (uptr, sim_switches);
        crs |= CRCSR_ONLINE;
        crs &= ~(CSR_ERR | CRCSR_OFFLINE | CRCSR_RDCHK | CRCSR_SUPPLY);
        cdst &= ~(CDCSR_RDRCHK | CDCSR_HOPPER);
        EOFcard = FALSE;
    }
    return (reason);
}

t_stat cr_detach (  UNIT    *uptr    )
{
    crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY | CRCSR_OFFLINE;
    /* interrupt? */
    crs &= ~CRCSR_ONLINE;
    cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER | CDCSR_OFFLINE;
    cardFormat = "unknown";
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
        sim_activate (uptr, spinDown);
    }
    return (detach_unit (uptr));
}

t_stat cr_set_type (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    /* disallow type change if currently attached */
    if (uptr->flags & UNIT_ATT)
        return (SCPE_NOFNC);
    cpm = (val & UNIT_CR11) ? 285 : 1000;
    uptr->wait = (60 * 1000) / cpm;
    return (SCPE_OK);
}

t_stat cr_show_format ( FILE    *st,
                        UNIT    *uptr,
                        int32   val,
                        void    *desc    )
{
    fprintf (st, "%s format", cardFormat);
    return (SCPE_OK);
}

t_stat cr_set_rate (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    t_stat    status = SCPE_OK;
    int32    i;

    if (!cptr)
        return (SCPE_MISVAL);
    if (strcmp (cptr, "DEFAULT") == 0)
        i = (uptr->flags & UNIT_CR11) ? 285 : 1000;
    else
        i = (int32) get_uint (cptr, 10, 0xFFFFFFFF, &status);
    if (status == SCPE_OK) {
        if (i < 200 || i > 1200)
            status = SCPE_ARG;
        else {
            cpm = i;
            uptr->wait = (60 * 1000) / cpm;
        }
    }
    return (status);
}

t_stat cr_show_rate (   FILE    *st,
                        UNIT    *uptr,
                        int32   val,
                        void    *desc    )
{
    fprintf (st, "%d cards per minute", cpm);
    return (SCPE_OK);
}

/* simulate pressing the card reader RESET button */

t_stat cr_set_reset (   UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "cr_set_reset\n");
/*
Ignore the RESET switch while a read cycle is in progress or the
unit simply is not attached.
*/
    if ((crs & CRCSR_BUSY) || !(uptr->flags & UNIT_ATT))
        return (SCPE_OK);
    /* if no errors, signal transition to on line */
    crs |= CRCSR_ONLINE;
    crs &= ~(CSR_ERR|CRCSR_CRDDONE|CRCSR_SUPPLY|CRCSR_RDCHK|CRCSR_TIMERR|
         CRCSR_BUSY|CRCSR_COLRDY|CRCSR_EJECT|CSR_GO);
    cdst |= CDCSR_ONLINE;
    cdst &= ~(CSR_ERR | CDCSR_OFFLINE | CDCSR_RDRCHK | CDCSR_HOPPER |
          CDCSR_EOF);
    if ((crs & CSR_IE) || (cdst & CSR_IE)) {
        SET_INT (CR);
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_set_reset setting interrupt\n");
    }
    /* start up the blower if the hopper is not empty */
    if (blowerState != BLOW_ON)
        blowerState = BLOW_START;
    return (SCPE_OK);
}

/* simulate pressing the card reader STOP button */

t_stat cr_set_stop (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "set_stop\n");
    crs &= ~CRCSR_ONLINE;
    crs |= CSR_ERR | CRCSR_OFFLINE;
    cdst |= CDCSR_OFFLINE;
    /* CD11 does not appear to interrupt on STOP. */
    if (crs & CSR_IE)
        SET_INT (CR);
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
        /* set timer to turn it off completely */
        sim_activate (uptr, spinDown);
    }
    return (SCPE_OK);
}

static const char * const trans[] = {
    "unknown", "026", "026FTN", "029", "EBCDIC"
};

t_stat cr_set_trans (   UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    int    i;

    if (!cptr)
        return (SCPE_MISVAL);
    if (strcmp (cptr, "DEFAULT") == 0)
        i = 3;
    else {
        for (i = 1; i < 5; i++) {
            if (strcmp (cptr, trans[i]) == 0)
                break;
        }
    }
    if (i < 1 || i > 4)
        return (SCPE_ARG);
    table = i;
    initTranslation ();    /* reinitialize tables */
    return (SCPE_OK);
}

t_stat cr_show_trans (  FILE    *st,
                        UNIT    *uptr,
                        int32   val,
                        void    *desc    )
{
    fprintf (st, "translation %s", trans[table]);
    return (SCPE_OK);
}
