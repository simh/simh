/* pdp11_cr.c: CR/CM/CD-11/CD20 card reader simulator

   Copyright (c) 2005-2010, John A. Dundas III
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

   cr        CR11/CD11/CD20 punched and mark sense card reader for SIMH
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
    CD2SER.MAC - TOPS-10 card reader driver source
      http://pdp-10.trailing-edge.com/custsupcuspmar86_bb-x130b-sb/02/cd2ser.mac
    CDRIVE.MAC - TOPS GALAXY card reader spooler
        http://pdp-10.trailing-edge.com/BB-BT99U-BB_1990/03/10,7/galaxy/cdrive/cdrive.mac
    SPRINT.MAC - TOPS GALAXY control card interpreter
        http://pdp-10.trailing-edge.com/BB-H138C-BM/01/galaxy-sources/sprint.mac
    CDKSDV.MAC - TOPS-20 card reader driver source
        http://pdp-10.trailing-edge.com/BB-Y393K-SM/01/monitor-sources/cdksdv.mac
    PROKS.MAC - TOPS-20 bit definitions
        http://pdp-10.trailing-edge.com/BB-Y393K-SM/01/monitor-sources/proks.mac

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

    To-do (RSX): The CR11 unit works as a regular device (ie, 
    you can PIP from it) but it does not work well as a job
    input device (it works just once, somwhow the CRP processor
    gets stuck).
 
    Don't have any information about Unix or Ultrix-11 yet.  Same
    for VAX Unices.

    TOPS: only the CD20 variant of the CD11 is supported.  CD20 implies
    ECOs (at least) for Data Buffer status and augmented image mode.

  Revision History:
   23-Feb-13    JGP     Added DEC version of the 026 codepage
                        Fixed the handling of the CR11 error bits after
                        a control register write.
                        Added logic reset after RESET button press
                        Commented and reestructured code (to supress
                        dangling elses)
   03-Jan-10    JAD     Eliminate gcc warnings
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

/* Configuration notes:
 * Keep VM_arch symbols here and use them only to select features.  
 * CR attributes use generic symbols so device support is easy to change,
 * e.g. if software is discovered that uses a previously unsupported option. 
 * Conventions:
 * *_ONLY (AND *_req) means feature * is unconditionally present/required.
 * *_OK means feature * is selectable at runtime.
 * neither means feature is not present.
 * To support only one controller model, define <model>_ONLY.
 * To support more than one, define them all as <model>_OK.
 * Don't mix "_ONLY" and "_OK" for the same feature.  You won't like it.
 *
 * The CD/CR will work on any UNIBUS, and the CR will also work on a QBUS.
 * The configuration options used here are more restrictive to reflect 
 * known software support, as this reduces user configuration errors/confusion.
 */

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"
extern int32 int_req;
#define DFLT_DIS        (DEV_DIS)
#define DFLT_TYPE       (UNIT_CD20)                    /* CD20 (CD11) only */
#define CD20_ONLY       (1)
#define DFLT_CPM        1200
#define AIECO_REQ       (1)                             /* Requires Augmented Image ECO */
#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
extern int32 int_req[IPL_HLVL];
#define DFLT_DIS        (DEV_QBUS)                      /* CR11 is programmed I/O only, Qbus OK */
#define DFLT_TYPE       (UNIT_CR11)                     /* CR11 only */
#define CR11_ONLY       (1)
#define DFLT_CPM        285
#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
extern int32 int_req[IPL_HLVL];
#define DFLT_DIS        (DEV_QBUS)                      /* CR11 is programmed I/O only, Qbus OK */
#define DFLT_TYPE       (UNIT_CR11)                     /* Default, but changable */
#define DFLT_CPM        285
#define CD20_OK        (1)
#define AIECO_OK       (1)                              /* Augmented Image ECO optional */
#define CR11_OK        (1)
#define CD11_OK        (1)
#endif

/* **** No VM_xxx macros should be referenced after this line **** */

/* create a int32 constant from four characters */
#define I4C(a,b,c,d)    (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define I4C_CBN         I4C ('C','B','N',' ')
#define I4C_H80         I4C ('H','8','0',' ')
#define I4C_H82         I4C ('H','8','2',' ')
#define I4C_H40         I4C ('H','4','0',' ')

#define    UNIT_V_TYPE      (UNIT_V_UF + 0) /* Bit-encoded 2-bit field */
#define    UNIT_TYPE        (3u << UNIT_V_TYPE)
#define    UNIT_CR11        (1u << UNIT_V_TYPE)
#define    UNIT_CD20        (2u << UNIT_V_TYPE)

#define    UNIT_V_AUTOEOF   (UNIT_V_UF + 2)
#define    UNIT_AUTOEOF     (1u << UNIT_V_AUTOEOF)
#define    UNIT_V_RDCHECK   (UNIT_V_UF + 3)
#define    UNIT_RDCHECK     (1u << UNIT_V_RDCHECK)
#define    UNIT_V_AIECO     (UNIT_V_UF + 4)
#define    UNIT_AIECO       (1u << UNIT_V_AIECO)

/* Tests for which device is being emulated.
 * Note that CD20 is a CD11 + mandatory ECOs.  CD11_CTL will be true for both.
 */
#if defined (CD11_ONLY) || defined (CD20_ONLY)
#define CR11_CTL(up) (0)
#define CD11_CTL(up) (1)
#elif defined (CR11_ONLY)
#define CR11_CTL(up) (1)
#define CD11_CTL(up) (0)
#else
#define CR11_CTL(up) ((up)->flags & UNIT_CR11)
#define CD11_CTL(up) (!CR11_CTL(up))
#endif

#if defined (CD20_ONLY)
#define CD20_CTL(up) (1)
#elif defined (CD20_OK)
#define CD20_CTL(up) ((up)->flags & UNIT_CD20)
#else
#define CD20_CTL(up) (0)
#endif

/* Configuration */
#if defined (AIECO_REQ)
#define DFLT_AIECO   (UNIT_AIECO)
#else
#define DFLT_AIECO   (0)
#endif

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
/* ERR */
#define    CDCSR_V_RDRCHK   14                          /* reader check: HOPPER,STACK,PICK,READ */
#define    CDCSR_V_EOF      13                          /* CD11-E EOF button */
#define    CDCSR_V_OFFLINE  12                          /* off line */
#define    CDCSR_V_DATAERR  11                          /* data packing error */
#define    CDCSR_V_LATE     10                          /* data late */
#define    CDCSR_V_NXM      9                           /* non-existent memory */
#define    CDCSR_V_PWRCLR   8                           /* power clear */
#define    CDCSR_V_RDY      7                           /* ready */
/* IE */
#define    CDCSR_V_XBA17    5                           /* NPR bus address bits<17:16> */
#define    CDCSR_V_XBA16    4
#define    CDCSR_V_ONLINE   3                           /* on line transition */
#define    CDCSR_V_HOPPER   2                           /* hopper check */
#define    CDCSR_V_PACK     1                           /* data packing */
/* GO */

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

#define    CDCSR_ANYERR     (CDCSR_RDRCHK | CDCSR_EOF | CDCSR_OFFLINE | CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM)

#define    CDCSR_IMP        (CSR_ERR | CDCSR_RDRCHK | CDCSR_EOF | CDCSR_OFFLINE | \
                             CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM | \
                             CDCSR_PWRCLR | CDCSR_RDY | CSR_IE | \
                             CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_ONLINE | \
                             CDCSR_HOPPER | CDCSR_PACK | CSR_GO)

#define    CDCSR_RW         (CDCSR_PWRCLR | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | \
                             CDCSR_PACK | CSR_GO)

/* CD11 second status register bits.  Valid only when not busy.  All
 * also set CDCSR_RDRCK (and CSR_ERR)
 */

#define CDDB_V_READ        14 /* Read check (extra punches, not readER check) */
#define CDDB_V_PICK        13 /* Pick check (card present, not grabbed) */
#define CDDB_V_STACK       12 /* Card did not arrive in stacker */

/* N.B. Per TOPS-20 driver, which references CD11 manual and printset: 
 * Stacker full is indicated by:
 * CDCSR_RDRCHK && !(CDDB_READ|CDDB_PICK|CDDB_STACK)
 */
#define CDDB_READ       (1U << CDDB_V_READ)
#define CDDB_PICK       (1u << CDDB_V_PICK)
#define CDDB_STACK      (1u << CDDB_V_STACK)


/* Blower state values */
#define    BLOW_OFF         (0)                         /* steady state off */
#define    BLOW_START       (1)                         /* starting up */
#define    BLOW_ON          (2)                         /* steady state on */
#define    BLOW_STOP        (3)                         /* shutting down */

/* Card Reader state */
static char     *cardFormat = "unknown";
static t_bool   (*readRtn)(UNIT *, int16 *, char *, char *);
static char     ascii_code[4096];                       /* 2^12 possible values */
static int      currCol;                                /* current column when reading */
static int      colStart;                               /* starting column */
static int      colEnd;                                 /* ending column */
static const int *codeTbl =                            /* punch translation table */
#if defined(CD20_ONLY) || (defined(DFLT_TYPE) && (DFLT_TYPE == UNIT_CD20))
    o29_decascii_code;
#else
    o29_code;
#endif
static  struct trans {
    const char *const name;
    const int  *table;
} transcodes[] = {
    { "DEFAULT", o29_code, },
    { "026", o26_dec_code, },
    { "026FTN", o26_ftn_code, },
    { "026DECASCII", o26_decascii_code, },
    { "029", o29_code, },
    { "EBCDIC", EBCDIC_code, },
    { "026DEC", o26_dec_code, },
    { "029DECASCII", o29_decascii_code },
};
#define NTRANS (sizeof transcodes /sizeof transcodes[0])

static int32    blowerState = BLOW_OFF;                 /* reader vacuum/blower motor */
static int32    spinUp = 3000000;                       /* blower spin-up time: 3 seconds (usec) */
static int32    spinDown = 2000000;                     /* blower spin-down time: 2 seconds (usec) */
static int      EOFcard = 0;                            /* played special card yet? */
static t_bool   eofPending = FALSE;                     /* Manual EOF switch pressed */
static int32    cpm = DFLT_CPM;                         /* reader rate: cards per minute */
static int      schedule_svc=0;                         /* Re-schedule service if true */
/* card image in various formats */
static int16    hcard[82];                              /* Hollerith format */
static char     ccard[82];                              /* DEC compressed format */
static char     acard[82];                              /* ASCII format */
/* CR/CM registers */
static int32    crs = CSR_ERR | CRCSR_OFFLINE | CRCSR_SUPPLY; /* control/status */
static int32    crb1 = 0;                               /* 12-bit Hollerith characters */
static int32    crb2 = 0;                               /* 8-bit compressed characters */
static int32    crm = 0;                                /* CMS maintenance register */
/* CD registers */
static int32    cdst = CSR_ERR | CDCSR_OFFLINE | CDCSR_HOPPER; /* Control/status - off-line until attached */
static int32    cdcc = 0;                               /* column count */
static int32    cdba = 0;                               /* current address, low 16 bits */
static int32    cddb = 0;                               /* data, 2nd status */
static int32    cddbs = 0;                              /* second status bits (or with cddb) */

/* forward references */
DEVICE cr_dev;
static void setupCardFile (UNIT *, int32);
t_stat cr_rd (int32 *, int32, int32);
t_stat cr_wr (int32, int32, int32);
int32  cr_intac(void);
t_stat cr_svc (UNIT *);
t_stat cr_reset (DEVICE *);
t_stat cr_attach (UNIT *, char *);
t_stat cr_detach (UNIT *);
t_stat cr_set_type (UNIT *, int32, char *, void *);
t_stat cr_set_aieco (UNIT *, int32, char *, void *);
t_stat cr_show_format (FILE *, UNIT *, int32, void *);
t_stat cr_set_rate (UNIT *, int32, char *, void *);
t_stat cr_show_rate (FILE *, UNIT *, int32, void *);
t_stat cr_set_reset (UNIT *, int32, char *, void *);
t_stat cr_set_stop (UNIT *, int32, char *, void *);
t_stat cr_set_eof (UNIT *, int32, char *, void *);
t_stat cr_show_eof (FILE *, UNIT *, int32, void *);
t_stat cr_set_trans (UNIT *, int32, char*, void *);
t_stat cr_show_trans (FILE *, UNIT *, int32, void *);
static t_stat cr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *cr_description (DEVICE *dptr);


/* CR data structures

   cr_dib   CR device information block
   cr_unit  CR unit descriptor
   cr_reg   CR register list
   cr_mod   CR modifier table
   cr_dev   CR device descriptor
*/

#define IOLN_CR         010

static DIB cr_dib = { IOBA_AUTO, IOLN_CR, &cr_rd, &cr_wr,
        1, IVCL (CR), VEC_AUTO, { cr_intac } };

static UNIT cr_unit = {
    UDATA (&cr_svc,
      UNIT_ATTABLE+UNIT_SEQ+UNIT_ROABLE+UNIT_DISABLE+
      DFLT_TYPE+UNIT_AUTOEOF+UNIT_RDCHECK+DFLT_AIECO, 0),
        (60 * 1000000) / (DFLT_CPM * 80) };

static const REG cr_reg[] = {
    { GRDATAD (BUF,    cr_unit.buf, DEV_RDX,  8, 0, "ASCII value of last column processed") },
#if defined (CR11_OK) || defined (CR11_ONLY)
    { GRDATAD (CRS,            crs, DEV_RDX, 16, 0, "CR11 status register") },
    { GRDATAD (CRB1,          crb1, DEV_RDX, 16, 0, "CR11 12-bit Hollerith character") },
    { GRDATAD (CRB2,          crb2, DEV_RDX, 16, 0, "CR11 8-bit compressed character") },
    { GRDATAD (CRM,            crm, DEV_RDX, 16, 0, "CR11 maintenance register") },
#endif
#if defined (CD11_OK) || defined (CD11_ONLY) || defined (CD20_OK) || defined (CD20_ONLY)
    { GRDATAD (CDST,          cdst, DEV_RDX, 16, 0, "CD11 control/status register") },
    { GRDATAD (CDCC,          cdcc, DEV_RDX, 16, 0, "CD11 column count") },
    { GRDATAD (CDBA,          cdba, DEV_RDX, 16, 0, "CD11 current bus address") },
    { GRDATAD (CDDB,          cddb, DEV_RDX, 16, 0, "CD11 data buffer, 2nd status") },
#endif
    { GRDATAD (BLOWER, blowerState, DEV_RDX,  2, 0, "blower state value") },
    { FLDATAD (INT,      IREQ (CR), INT_V_CR,       "interrupt pending flag") },
    { FLDATAD (ERR,            crs, CSR_V_ERR,      "error flag (CRS<15>)") },
    { FLDATAD (IE,             crs, CSR_V_IE,       "interrupt enable flag (CRS<6>)") },
    { DRDATAD (POS,    cr_unit.pos, T_ADDR_W,       "file position - do not alter"), PV_LEFT },
    { DRDATAD (TIME,  cr_unit.wait, 24,             "delay time between columns"), PV_LEFT },
    { GRDATA  (DEVADDR,  cr_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,  cr_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }  };

static char *translation_help = NULL;
static MTAB cr_mod[] = {
#if defined (CR11_OK)
    { UNIT_TYPE, UNIT_CR11, "CR11", "CR11", 
        &cr_set_type, NULL, NULL, "Set device type to CR11" },
#endif
#if defined (CD11_OK)
    { UNIT_TYPE,         0, "CD11", "CD11", 
        &cr_set_type, NULL, NULL, "Set device type to CD11" },
#endif
#if defined (CD20_OK)
    { UNIT_TYPE, UNIT_CD20, "CD20", "CD20", 
        &cr_set_type, NULL, NULL, "Set device type to CD20" },
#endif
#if defined (CR11_ONLY) || defined (CD11_ONLY) || defined (CD20_ONLY)
    { UNIT_TYPE, UNIT_CR11, "CR11", NULL, },
    { UNIT_TYPE,         0, "CD11", NULL, },
    { UNIT_TYPE, UNIT_CD20, "CD20", NULL, },
#endif
#if defined (AIECO_OK)
    { (UNIT_TYPE|UNIT_AIECO), (UNIT_CD20|UNIT_AIECO), "augmented image ECO", "AIECO",
        &cr_set_aieco, NULL, NULL, "Enable CD20 augmented image ECO" },
    { (UNIT_TYPE|UNIT_AIECO), (UNIT_CD20|0),          "standard", "NOAIECO",
        &cr_set_aieco, NULL, NULL, "Disable CD20 augmented image ECO" },
#endif
    { UNIT_AUTOEOF, UNIT_AUTOEOF, "auto EOF", "AUTOEOF", 
        NULL, NULL, NULL, "Enable auto EOF mode" },
    { UNIT_AUTOEOF,            0, "no auto EOF", "NOAUTOEOF", 
        NULL, NULL, NULL, "Disable auto EOF mode" },
#if !defined (CR11_ONLY)
    { UNIT_RDCHECK, UNIT_RDCHECK, "read check", "RDCHECK",
      NULL, NULL, NULL, "Enable read check errors" },
    { UNIT_RDCHECK,            0, "no read check", "NORDCHECK",
      NULL, NULL, NULL, "Disable read check errors" },
#endif
      /* card reader STOP switch */
    { MTAB_XTD|MTAB_VDV, 0, NULL, "STOP",
        &cr_set_stop, NULL, NULL, "Pulse reader Stop button" },
    /* card reader RESET switch */
    { MTAB_XTD|MTAB_VDV, 0, NULL, "RESET",
        &cr_set_reset, NULL, NULL, "Pulse reader reset button" },
#if !defined (CR11_ONLY)
    /* card reader EOF switch */
    { MTAB_XTD|MTAB_VDV, MTAB_XTD|MTAB_VDV, "EOF pending", "EOF",
        &cr_set_eof, &cr_show_eof, NULL, "Pulse reader EOF button" },
#endif
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", NULL,
        NULL, &cr_show_format, NULL, "Set reader input format" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 006, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec, &show_vec, NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV, 0, "RATE", "RATE={DEFAULT|200..1200}",
        &cr_set_rate, &cr_show_rate, NULL, "Display input rate" },
    { MTAB_XTD|MTAB_VDV, 0, "TRANSLATION",
        NULL,
        &cr_set_trans, &cr_show_trans, NULL, "Display translation mode" },
    { 0 }  };

DEVICE cr_dev = {
    "CR", &cr_unit, (REG *)  &cr_reg, (MTAB *) &cr_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &cr_reset,
    NULL, &cr_attach, &cr_detach,
    &cr_dib, DEV_DISABLE | DFLT_DIS | DEV_UBUS | DEV_DEBUG, 0,
    NULL, NULL, NULL, &cr_help, NULL, NULL,
    &cr_description
    };

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

Note that the hopper becomes empty when the last card moves to the
read station.  Thus hopper empty without an error means that data
from that card is valid.  Thus hopper empty is first signalled when
the NEXT card read would return EOF.  Reads after that will return
some error bit.

Errors other than EOF are signaled out of band in the controller
state variables.  Possible errors are data in columns 0 or 81
(signalled as read check; currently these columns are ignored), or
any file errors (signalled as motion check).

Might rethink this.  Should probably treat file errors as "pick
check".  Retry 3 times.  After that, give up with error.

*/

/* Common handling for end of file and errors on input */

static t_bool fileEOF ( UNIT  *uptr,
                        int16 *hcard,
                        char  *ccard,
                        char  *acard,
                        int32  cddbsBits )
{
    int col;

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "hopper empty-eof\n");

    if (!EOFcard && (uptr->flags & UNIT_AUTOEOF) && !ferror(uptr->fileref)) {
        EOFcard = -1;
        /* Generate EOD card, which empties the hopper */
        for (col = 1; col <= 8; col++) {
            hcard[col] = PUNCH_EOD;
            ccard[col] = (char)h2c_code[PUNCH_EOD];
            acard[col] = ' ';
        }
        while (col <= colEnd) {
            hcard[col] = PUNCH_SPACE;
            ccard[col] = PUNCH_SPACE;
            acard[col] = ' ';
            col++;
        }
        /* The CR11 doesn't set SUPPPLY at this time, but waits until the EOF card is done. */
        cdst |= CDCSR_HOPPER;
        return (TRUE);
    }
    
    /* Not auto EOF, or EOF already handled. This is an attempt to read
     * with an empty hopper.  Report a pick, read or stacker check as well
     * as hopper empty to indicate that no data was transfered.  One might
     * think that cdcc unchanged would be sufficient, but that's not what
     * the OSs check.
     */

    crs |= CSR_ERR | CRCSR_SUPPLY | CRCSR_OFFLINE;
    crs &= ~(CRCSR_COLRDY | CRCSR_ONLINE);

    cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
    cddbs |= cddbsBits;

    if (((uptr->flags & UNIT_AUTOEOF) || eofPending) && !ferror(uptr->fileref)) {
        cdst |= CDCSR_EOF;
        eofPending = FALSE;
    }
    return (FALSE);
}

static t_bool readCardImage (   UNIT    *uptr,
                                int16   *hcard,
                                char    *ccard,
                                char    *acard    )
{
    int    c1, c2, c3, col;
    FILE   *fp = uptr->fileref;

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "readCardImage pos %d\n", (int) ftell (fp));
    do {
        /* get card header bytes */
        c1 = fgetc (fp);
        c2 = fgetc (fp);
        c3 = fgetc (fp);
        uptr->pos = ftell (fp);
        /* check for EOF */
        if (c1 == EOF)
            return fileEOF (uptr, hcard, ccard, acard, CDDB_PICK);

        /* check for valid card header */
        if ((c2 == EOF) || (c3 == EOF) || ((c1 & c2 & c3 & 0x80) == 0) ) {
            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "header error\n");
            /* unexpected EOF or format problems */
            return fileEOF (uptr, hcard, ccard, acard, CDDB_READ);
        }

        /* Read card image into internal buffer */

        assert (colStart < colEnd);
        assert (colStart >= 0);
        assert (colEnd <= 81);
        for (col = colStart; col < colEnd; ) {
            int16    i;
            int    c1, c2, c3;
            /* get 3 bytes */
            c1 = fgetc (fp);
            c2 = fgetc (fp);
            c3 = fgetc (fp);
            uptr->pos = ftell (fp);
            if (ferror (fp) || (c1 == EOF) || (c2 == EOF) || (c3 == EOF)) {
                if (DEBUG_PRS (cr_dev))
                    fprintf (sim_deb, "file error\n");
                    /* signal error; unexpected EOF, format problems, or file error(s) */
                return  fileEOF (uptr, hcard, ccard, acard, ferror(fp)? CDDB_READ: CDDB_PICK);
            }
            /* convert to 2 columns */
            i = ((c1 << 4) | ( c2 >> 4)) & 0xFFF;
            hcard[col] = i;
            ccard[col] = (char)h2c_code[i];
            acard[col] = ascii_code[i];
            col++;

            i = (((c2 & 017) << 8) | c3) & 0xFFF;
            hcard[col] = i;
            ccard[col] = (char)h2c_code[i];
            acard[col] = ascii_code[i];
            col++;
        }
    } while ((c3 & 0x3f) == 0x3f); /* Skip metacards (Revised Jones spec) */

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "successfully loaded card\n");
    return (TRUE);
}

static t_bool readColumnBinary (    UNIT    *uptr,
                                    int16   *hcard,
                                    char    *ccard,
                                    char    *acard    )
{
    int    col;
    FILE   *fp = uptr->fileref;

    for (col = colStart; col <= colEnd; col++) {
        int c1, c2;
        uint16 i;
        c1 = fgetc (fp);
        c2 = fgetc (fp);
        uptr->pos = ftell (fp);
        if (c1 == EOF)
            return fileEOF (uptr, hcard, ccard, acard, CDDB_PICK);
        if ((c2 == EOF) || ferror(fp))
            return fileEOF (uptr, hcard, ccard, acard, CDDB_READ);
        i = (c1 & 077) | ((c2 & 077) << 6);
        hcard[col] = i;
        ccard[col] = (char)h2c_code[i];
        acard[col] = ascii_code[i];
    }
    return (TRUE);
}

/*

Should this routine perform special handling of non-printable,
(e.g., control) characters or characters that have no encoded
representation? (In DEC026/DEC029 they all do...)

*/

static t_bool readCardASCII (   UNIT    *uptr,
                                int16   *hcard,
                                char    *ccard,
                                char    *acard    )
{
    int    c = 0, col, peek;
    FILE   *fp = uptr->fileref;

    assert (colStart < colEnd);
    assert (colStart >= 1);
    assert (colEnd <= 80);

    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "readCardASCII\n");
    for (col = colStart; col <= colEnd; ) {
        switch (c = fgetc (fp)) {
        case EOF:
            if (ferror (fp)) {
                uptr->pos = ftell (fp);
                return fileEOF (uptr, hcard, ccard, acard, CDDB_READ);
            }
            if (col == colStart) {
                if (DEBUG_PRS (cr_dev))
                    fprintf (sim_deb, "hopper empty\n");
                uptr->pos = ftell (fp);
                return fileEOF (uptr, hcard, ccard, acard, CDDB_PICK);
            }
            /* fall through */
        case '\r':
            peek = fgetc (uptr->fileref);
            if ((peek != EOF) && (peek != '\n'))
                ungetc (peek, uptr->fileref);
            goto fill;
        case '\n':
            peek = fgetc (uptr->fileref);
            if ((peek != EOF) && (peek != '\r'))
                ungetc (peek, uptr->fileref);
          fill:
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
            hcard[col] = (uint16)codeTbl[c & 0177];
            /* check for unrepresentable ASCII characters */
            if (hcard[col] == ERROR) {
                cdst |= CDCSR_DATAERR;
                if (DEBUG_PRS (cr_dev))
                    fprintf (sim_deb,
                        "error character at column %d (%c)\n",
                            col, c & 0177);
            }
            ccard[col] = (char)h2c_code[hcard[col]];
            acard[col] = (char)c;
            col++;
            break;
        }
    }
    /* silently truncate/flush long lines, or flag over-length card? */
    if (c != '\n' && c != '\r') {
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "truncating card\n");
        c = fgetc (fp);
        while (c != EOF) {
            if ((c == '\n') || (c == '\r')) {
                peek = fgetc (uptr->fileref);
                if (peek == EOF)
                    break;
                if (((c == '\n') && (peek != '\r')) || ((c == '\r') && (peek != '\n')))
                    ungetc (peek, uptr->fileref);
                break;
            }
            c = fgetc (fp);
        }
    }
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "successfully loaded card\n");
    uptr->pos = ftell (fp);
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
    for (i = 0; i < 0177; i++)
        ascii_code[codeTbl[i]] = (char)i;
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
        if (cdst & (CDCSR_ANYERR))
            cdst |= CSR_ERR;
        else
            cdst &= ~CSR_ERR;
        *data = (CR11_CTL(&cr_unit)) ?
            crs & CRCSR_IMP : cdst & CDCSR_IMP;
        /* CR: if error removed, clear 15, 14, 11, 10 */
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_rd crs %06o cdst %06o\n",
                crs, cdst);
        break;
    case 1:
        /* Get word of data from crb1 (Hollerith code) or CD11 CC */
        *data = (CR11_CTL(&cr_unit)) ? crb1 : cdcc;
        crs &= ~CRCSR_COLRDY;
        if (DEBUG_PRS (cr_dev)) {
        if (CR11_CTL(&cr_unit))
            fprintf (sim_deb, "cr_rd crb1 %06o '%c' %d\n",
                     crb1, cr_unit.buf, cr_unit.buf);
        else
            fprintf (sim_deb, "cr_rd cdcc %06o\n", cdcc);
        }
        /* Does crb1 clear after read? Implied by VMS driver. */
        crb1 = 0;
        break;
    case 2:
        /* Get word of data from crb2 (DEC Compressed) or CD11 BA */
        *data = (CR11_CTL(&cr_unit)) ? crb2 : cdba;
        crs &= ~CRCSR_COLRDY;
        if (DEBUG_PRS (cr_dev)) {
            if (CR11_CTL(&cr_unit))
                fprintf (sim_deb, "cr_rd crb2 %06o\n", crb2);
            else
                fprintf (sim_deb, "\r\ncr_rd cdba %06o\n", cdba);
        }
       crb2 = 0;    /* see note for crb1 */ 
       break;
    case 3:
    default:
        if (CR11_CTL(&cr_unit)) /* CR11 maintenance */
            *data = crm;
        else /* CD11 data buffer/status.  Note this implementation returns extended 
              * status even while busy (rather than the zone).  Might be wrong.
              */
            *data = 0100000 | (cddbs & (CDDB_READ|CDDB_PICK|CDDB_STACK)) |
                 ((crs & CRCSR_BUSY) ?
                cddb & 0777 : 0777);
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
    int curr_crs = crs;     /* Save current crs to recover status */

    switch ((PA >> 1) & 03) {
    case 0:
        if (CR11_CTL(&cr_unit)) {
            /* ignore high-byte writes */
            if (PA & 1)
                break;
            /* fixup data for low byte write */
            if (access == WRITEB)
                data = (crs & ~0377) | (data & 0377); 
            if (!(data & CSR_IE))
                CLR_INT (CR);
            crs = (crs & ~CRCSR_RW) | (data & CRCSR_RW);
            /* Clear status bits after CSR load */
            crs &= ~(CSR_ERR | CRCSR_ONLINE | CRCSR_CRDDONE | CRCSR_TIMERR);
            if (crs & CRCSR_OFFLINE)
                crs |= CSR_ERR;
            /* 
             * Read card requested:
             * Check if there was any error which required an operator
             * intervention, and if so, reassert the corresponding
             * error bits and assert interrupt.
             * (Expected by the VMS CRDRIVER)
             */
            if (data & CSR_GO) {
                if (curr_crs & (CRCSR_SUPPLY | CRCSR_RDCHK | CRCSR_OFFLINE)) {
                    crs |= CSR_ERR | (curr_crs & (CRCSR_SUPPLY | CRCSR_RDCHK |
                                                  CRCSR_OFFLINE));
                    if (crs & CSR_IE) SET_INT(CR);
                }
                if (blowerState != BLOW_ON) {
                    blowerState = BLOW_START;
                    sim_activate_after (&cr_unit, spinUp);
                } else {
                    sim_activate_after (&cr_unit, cr_unit.wait);
                }
            }
            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "cr_wr data %06o crs %06o\n",
                         data, crs);
        } else { /* CD11 */
            if (access == WRITEB)
                data = (PA & 1)? (((data & 0xff)<<8) | (cdst & 0x00ff)):
                                  ((data & 0x00ff) | (cdst & 0xFF00));

            if (data & CDCSR_PWRCLR) {
                CLR_INT (CR);
                sim_cancel (&cr_unit);
                cdcc = 0;
                cdba = 0;
                cddb = 0;
                cddbs = 0;
                if (!(cr_unit.flags & UNIT_ATT)) { /* Clear troublesome bits, but leave error/offline */
                    cdst &= ~(CSR_IE | CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM | CDCSR_RDY |
                              CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_ONLINE | CDCSR_PACK);
                    cdst |= CSR_ERR | CDCSR_OFFLINE | CDCSR_RDRCHK;
                    cddbs |= CDDB_STACK;
                    break;
                }

                crs &= ~CRCSR_BUSY;
                cdst &= (CDCSR_OFFLINE | CDCSR_RDY | CDCSR_HOPPER);
                if( (cr_unit.flags & UNIT_ATT) && !feof(cr_unit.fileref) && !ferror(cr_unit.fileref) )
                    cdst &= ~(CDCSR_HOPPER);
                if (cdst & (CDCSR_ANYERR))
                    cdst |= CSR_ERR;
                cdst |= CDCSR_RDY;
                break;
            }

            if (data & CSR_GO) {
                /* To simplify the service code, don't start if CDCC == 0.
                 * In the hardware, it's not sensible...
                 */
                if ((crs & CRCSR_BUSY) || (cdcc == 0)) {
                    cdst |= (CDCSR_RDRCHK | CDCSR_HOPPER | CSR_ERR);
                } else {
                    cdst &= ~(CDCSR_RDRCHK | CDCSR_DATAERR | CDCSR_LATE | CDCSR_NXM | CDCSR_RDY | CDCSR_ONLINE);
                    cdst = (cdst & ~(CDCSR_EOF | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_PACK | CDCSR_HOPPER))
                         | (data &  (CDCSR_EOF | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_PACK));
                    cddbs &= ~(CDDB_READ|CDDB_PICK|CDDB_STACK);

                    /* Always attempt to start.  If not attached, errors will set after delay */
                    if (!(cdst & CDCSR_HOPPER) )
                        cdst &= ~(CSR_ERR);
                    if (blowerState != BLOW_ON) {
                        blowerState = BLOW_START;
                        sim_activate_after (&cr_unit, spinUp);
                    } else {
                        sim_activate_after (&cr_unit, cr_unit.wait);
                    }
                }
            } else {
                cdst = (cdst & ~(CSR_ERR | CDCSR_RDRCHK | CDCSR_EOF | CDCSR_DATAERR | CDCSR_LATE | 
                                 CDCSR_NXM | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_ONLINE | CDCSR_PACK))
                      |(data &  (CSR_ERR | CDCSR_RDRCHK | CDCSR_EOF | CDCSR_DATAERR | CDCSR_LATE | 
                                 CDCSR_NXM | CSR_IE | CDCSR_XBA17 | CDCSR_XBA16 | CDCSR_ONLINE | CDCSR_PACK));
            }
            /* Apparently the hardware does not SET_INT if ready/online are already set.  If it did, TOPS-10's driver wouldn't work */
            if (!(cdst & CSR_IE))
                CLR_INT (CR);

            if (DEBUG_PRS (cr_dev))
                fprintf (sim_deb, "cr_wr data %06o cdst %06o\n",
                    data, cdst);
        }
        break;
    case 1:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr cdcc %06o\n", data);
        if (CD11_CTL(&cr_unit))
            cdcc = data & 0177777;
        break;
    case 2:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr crba %06o\n", data);
        if (CD11_CTL(&cr_unit))
            cdba = data & 0177777;
        break;
    case 3:
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_wr cddb/crm %06o\n", data);
        /* ignore writes to cddb */
        if (CD11_CTL(&cr_unit))
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
 * Interrupt acknowledge routine
 * Reschedule service routine if needed (based on 
 * schedule_svc flag).
 * Do the actual scheduling just for the CR11 (VAX/PDP11). The PDP10 does
 * not seem to call this entry point.
 */

int32 cr_intac() {
    if CR11_CTL(&cr_unit) {
        if (schedule_svc) {
            sim_activate_after (&cr_unit, cr_unit.wait);
            schedule_svc = 0;
        }
    }
    return cr_dib.vec;      /* Constant interrupt vector */
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
    uint16   w;
    int      n;

    /* Blower stopping: set it to OFF and do nothing */
    if (blowerState == BLOW_STOP) {
        blowerState = BLOW_OFF;
        return (SCPE_OK);
    }
    /* Blower starting: set it to ON and do regular service */
    if (blowerState == BLOW_START)
        blowerState = BLOW_ON;

    /* (almost) anything we do now will cause a CR (But not a CD) interrupt */
    if ((CR11_CTL(uptr)) && (crs & CSR_IE))
       SET_INT (CR);
    
    /* Unit not attached, or error status while idle */
    if (!(uptr->flags & UNIT_ATT) || (!(crs & CRCSR_BUSY) && ((CR11_CTL(uptr)?crs : cdst) & CSR_ERR))) {
        if (CD11_CTL(uptr)) {
            if (!(uptr->flags & UNIT_ATT)){
                cdst |= (CDCSR_HOPPER | CDCSR_RDRCHK | CDCSR_OFFLINE | CSR_ERR);
                cddbs |= CDDB_STACK;
            }
            if (cdst & CSR_IE)
                SET_INT (CR);
        }
        return (SCPE_OK);
    }

    /* End of card: unit busy and column past end column */
    if ((crs & CRCSR_BUSY) && (currCol > colEnd)) {
        /* clear busy state and set card done bit */
        crs &= ~(CRCSR_BUSY | CRCSR_COLRDY);
        crs |= CRCSR_CRDDONE;

         if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_svc card done\n");
         
         /* Check CD11 error status that stops transfers */
        if (CD11_CTL(uptr) && (cdst & (CDCSR_LATE | CDCSR_NXM))) {
            cdst |= CSR_ERR | CDCSR_OFFLINE | CDCSR_RDY | CDCSR_RDRCHK;
            SET_INT (CR);
            return (SCPE_OK);
        }

        if (CR11_CTL(uptr))
            return (SCPE_OK);

        /* If a CD11 gets this far, an interrupt is required.  If CDCC != 0,
         * continue reading the next card.
         */
        SET_INT (CR);
        if (cdcc == 0)
            return (SCPE_OK);
    }

    /* If unit is not busy: try to read a card */ 
    if (!(crs & CRCSR_BUSY)) {
        crs &= ~CRCSR_CRDDONE; /* This line WAS commented out - JGP 2013.02.05 */

        /* Call the appropriate read card routine.
         * If no card is read (FALSE return), we tried to read with an empty hopper.
         * The card read routine set the appropriate error bits.  Shutdown. 
         */
        if (!readRtn (uptr, hcard, ccard, acard)) {
            blowerState = BLOW_STOP;
            if (CD11_CTL(uptr)) {
readFault:
                cdst |= CDCSR_RDY;
                if (cdst & (CDCSR_RDRCHK | CDCSR_HOPPER))
                    cdst |= CSR_ERR | CDCSR_OFFLINE;
                if (cdst & CSR_IE)
                    SET_INT (CR);

            } else {
                /*
                 * CR11 handling: assert SUPPLY and ERROR bits,
                 * put de device offline and DO NOT TRIGGER AN INTERRUPT
                 * (if the interrupt is asserted RSX and VMS will get 80
                 * bytes of garbage, and RSX could crash).
                 */
                if (crs & (CRCSR_RDCHK | CRCSR_SUPPLY)) {
                    crs |= CSR_ERR | CRCSR_OFFLINE;
                    crs &= ~(CRCSR_ONLINE | CRCSR_BUSY | CRCSR_CRDDONE);
                    CLR_INT(CR);
                }
            }
            sim_activate_after (uptr, spinDown);
            return (SCPE_OK);
        }
        
        /* Card read: reset column counter and assert BUSY */
        currCol = colStart;
        crs |= CRCSR_BUSY;

        /* Update status if this read emptied hopper.
         * The CR11 doesn't set SUPPLY until after the last card is read.
         */
       
        /* I/O error status bits have been set during read.
         * Look ahead to see if another card is in file.
         */
        n = feof (uptr->fileref);
        if (n)
            n = EOF;
        else {
            n = fgetc (uptr->fileref);
            if (n != EOF)
                ungetc (n, uptr->fileref);
        }

        if ((n == EOF) && ((EOFcard > 0) || !(uptr->flags & UNIT_AUTOEOF))) {
            /* EOF and generated EOFcard sent or not an autoEOF unit.
             * Set status to reflect last card taken.
             */
            cdst |= (CDCSR_RDRCHK | CSR_ERR | CDCSR_OFFLINE | CDCSR_HOPPER);
            if (eofPending) {
                cdst |= CDCSR_EOF;
                eofPending = FALSE;
            }
        }
        
        if (EOFcard)
            EOFcard = 1;
        
        
        if (CD11_CTL(uptr)) {
             /* Handle read check: punches in col 0 or 81/last (DEC only did 80 cols, but...) */
            if ((uptr->flags & UNIT_RDCHECK) && 
                (((colStart == 0) && (hcard[0] != 0)) || ((colEnd & 1) && (hcard[colEnd] != 0)))) {
                cdst |= (CDCSR_RDRCHK | CSR_ERR);
                cddbs |= CDDB_READ;
                if (1) /* 0 if read check should transfer card */
                    goto readFault;
            }
            /* CDDB_PICK, CDDB_STACK, <stacker full) */
        }
    }

    /* check for overrun (timing error) */
    if (CR11_CTL(uptr) && (crs & CRCSR_COLRDY))
        crs |= CSR_ERR | CRCSR_TIMERR;

    /* Update the "buffer" registers with current column */
    crb1 = hcard[currCol] & 07777;      /* Hollerith value */
    crb2 = ccard[currCol] & 0377;       /* DEC compressed hollerith value */
    uptr->buf = acard[currCol] & 0377;  /* Helpful for debug: ASCII value */

    /* CD11 specific code follows */
    if (CD11_CTL(uptr)) {
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
        if (cdst & CDCSR_PACK)
            cddb = c = ccard[currCol] & 0377;
        else
            cddb = w = hcard[currCol] & 07777;  /* Punched zones: <12><11><0><1><2><3><4><5><6><7><8><9> */

        if (cdcc == 0) /* Transfer requires CC non-zero */
             cdst |= CDCSR_LATE;
        else {
            if (cdst & CDCSR_PACK) {
                if (Map_WriteB (pa, 1, &c))
                    cdst |= CDCSR_NXM;
                pa = (pa + 1) & 0777777;
            } else {
                /* "Augmented Image" - provides full column binary and packed encoding in 15 bits.
                 * Bits <14:12> encode which zone, if any, of 1-7 is punched.  0 => none, otherwise zone #.
                 * Bit 15 set indicates that more than one punch occured in zones 1-7; in this case the packed
                 * encoding is not valid. (Card may be binary data.)  
                 * This was probably an ECO to the CD11.  TOPS-10/20 depend on it, so it's definitely in the CD20.
                 */
                if (uptr->flags & UNIT_AIECO) {
                    uint16 z;
                    w |= ((ccard[currCol] & 07) << 12);     /* Encode zones 1..7 - same as 'packed' format */
                    z = w & 0774;
                    if ((z & -z) != z)                      /* More than one punch in 1..7 */
                        w |= 0100000;                       /* sets Hollerith (encoding) failure (not an error) */
                }
                if (Map_WriteW (pa, 2, &w))
                    cdst |= CDCSR_NXM;
                pa = (pa + 2) & 0777777;
            }
            cdba = pa & 0177777;
            cdst = (cdst & ~(CDCSR_XBA17|CDCSR_XBA16)) |
                ((pa & 0600000) >> 12);
            cdcc = (cdcc + 1) & 0177777;
            /* Interrupt at end of buffer; read continues to end of card.
             * If this is the last column, defer interrupt so end doesn't interrupt again.
             */
            if ((cdcc == 0) && (cdst & CSR_IE) && (currCol < colEnd))
                SET_INT (CR);
        }
    } else { /* CR11 */
        /* Handle EJECT bit: if set DO NOT assert COLRDY */
        /* nor interrupt                                 */
        if ((crs & CRCSR_EJECT)) {
            CLR_INT (CR);
        } else {
            crs |= CRCSR_COLRDY;
        }
    }

    /* CD11 and CR11 */
    currCol++;    /* advance the column counter */

    /* Schedule next service cycle */
    /* CR11 (VAX/PDP11): just raise the schedule_svc flag; the intack 
     * routine will do the actual rescheduling.
     * CD11/20 (PDP10): Do the rescheduling (the intack seems to do nothing)
     */
    if (CD11_CTL(uptr)) {
        sim_activate_after(uptr, uptr->wait);
    } else {
        schedule_svc = 1;
    }
    return (SCPE_OK);
}

t_stat cr_reset (   DEVICE  *dptr    )
{
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "cr_reset\n");

    if (!translation_help) {
        size_t i;
        const char trans_hlp[] = "TRANSLATION={";
        size_t size = sizeof(trans_hlp) +1;

        for ( i = 0; i < NTRANS; i++ )
            size += strlen (transcodes[i].name)+1;
        translation_help = (char *)malloc (size );
        strcpy(translation_help, trans_hlp);
        for (i = 0; i < NTRANS; i++) {
            strcat(translation_help, transcodes[i].name);
            strcat(translation_help,"|");
        }
        strcpy(translation_help+strlen(translation_help)-1, "}");
        for (i = 0; i < (sizeof cr_mod / sizeof cr_mod[0]); i++ ) 
            if (cr_mod[i].pstring && !strcmp(cr_mod[i].pstring, "TRANSLATION")) {
                cr_mod[i].mstring = translation_help;
                break;
            }
    }
    cr_unit.buf = 0;
    currCol = 1;
    crs &= ~(CSR_ERR|CRCSR_CRDDONE|CRCSR_TIMERR|CRCSR_ONLINE|CRCSR_BUSY|
         CRCSR_COLRDY|CSR_IE|CRCSR_EJECT|CSR_GO);
    if (crs & (CRCSR_OFFLINE))
        crs |= CSR_ERR;
    crb1 = 0;
    crb2 = 0;
    crm = 0;
    cdst &= ~(CSR_ERR|CDCSR_RDRCHK|CDCSR_EOF|CDCSR_DATAERR|CDCSR_LATE|
          CDCSR_NXM|CSR_IE|CDCSR_XBA17|CDCSR_XBA16|CDCSR_ONLINE|
          CDCSR_PACK|CSR_GO);
    cdst |= CDCSR_RDY;
    if (cdst & CDCSR_ANYERR)
        cdst |= CSR_ERR;
    cdcc = 0;
    cdba = 0;
    cddb = 0;
    /* ATTACHed doesn't mean ONLINE; set CR reset (pushing the reset switch)
     * is what puts the reader on-line.  Reset doesn't control fingers.
     */
    if ((cr_unit.flags & UNIT_ATT) && !feof (cr_unit.fileref)) {
        if (!(crs & CRCSR_OFFLINE))
            crs |= CRCSR_ONLINE;    /* non-standard */
        crs &= ~(CRCSR_RDCHK | CRCSR_SUPPLY );
        cdst &= ~(CDCSR_RDRCHK | CDCSR_HOPPER);
        cddbs = 0;
    } else {
        cdst |= CSR_ERR | CDCSR_RDRCHK | CDCSR_HOPPER;
        cddbs |= CDDB_STACK;
        crs |= CSR_ERR | CRCSR_RDCHK | CRCSR_SUPPLY;
    }
    sim_cancel (&cr_unit);        /* deactivate unit */
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
        sim_activate_after (&cr_unit, spinDown);
    }
    EOFcard = 0;
    CLR_INT (CR);
    /* TBD: flush current card */
    /* init uptr->wait ? */
    return auto_config (dptr->name, 1);
}

/*
Handle the interface status and SIMH portion of the ATTACH.  Another
routine is used to evaluate the file and initialize other state
globals correctly.
*/

#define    MASK    (SWMASK('A')|SWMASK('B')|SWMASK('I')|SWMASK('R'))

/* Attach unit                                                              */
/* This should simulate physically putting a stack of cards into the hopper */
/* No bits should change, nor an interrupt should be asserted               */
/* This is a change of behaviour respect to the previous code               */
t_stat cr_attach (  UNIT    *uptr,
                    char    *cptr    )
{
    t_stat        reason;

    if (sim_switches & ~MASK)
        return (SCPE_INVSW);
    /* file must previously exist; kludge */
    sim_switches |= SWMASK ('R');
    reason = attach_unit (uptr, cptr);
    if(uptr->flags & UNIT_ATT) {
        setupCardFile(uptr, sim_switches);
    }
    
    return (reason);
}

/* Detach unit: assert SUPPLY and OFFLINE bits (and ERR) */
t_stat cr_detach (  UNIT    *uptr    )
{
    crs |= CSR_ERR | CRCSR_SUPPLY | CRCSR_OFFLINE;
    /* interrupt? */
    crs &= ~CRCSR_ONLINE;
    cdst |= CSR_ERR | CDCSR_HOPPER | CDCSR_OFFLINE;
    cardFormat = "unknown";
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
        sim_activate_after (uptr, spinDown);
    }
    return (detach_unit (uptr));
}

#if defined (CR11_OK) || defined (CD11_OK) || defined (CD20_OK)
t_stat cr_set_type (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    DEVICE *dptr = find_dev_from_unit (uptr);

    /* disallow type change if currently attached */

    if (uptr->flags & UNIT_ATT)
        return (SCPE_NOFNC);
    if (val == UNIT_CR11) {
        dptr->flags |= DEV_QBUS;                    /* Can be a Qbus device - programmed I/O only */
    } else {                                        /* CD11/CD20 are 18bit DMA devices */
        if (!UNIBUS)
            return SCPE_NOFNC;
        dptr->flags &= ~DEV_QBUS;                   /* Not on a Qbus (22bit) */
    }
    cpm = (val & UNIT_CR11) ? 285 : ((val & UNIT_CD20)? 1200 :1000);
    uptr->wait = (60 * 1000000) / (cpm * 80);       /* Time between columns in usec.
                                                     * Readers are rated in card/min for 80 column cards */
    transcodes[0].table = (val & UNIT_CD20)? o29_decascii_code : o29_code;

    return (SCPE_OK);
}
#endif

#if defined (AIECO_OK)
t_stat cr_set_aieco (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    /* disallow eco change if currently attached or not CD20 */

    if (uptr->flags & UNIT_ATT || !CD20_CTL(uptr))
        return (SCPE_NOFNC);

    uptr->flags = (uptr->flags & ~UNIT_AIECO) | (val & UNIT_AIECO);
    return (SCPE_OK);
}
#endif

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
        i = CR11_CTL(uptr) ? 285 : (CD20_CTL(uptr)? 1200 :1000);
    else
        i = (int32) get_uint (cptr, 10, 0xFFFFFFFF, &status);
    if (status == SCPE_OK) {
        if (i < 200 || i > 1200)
            status = SCPE_ARG;
        else {
            cpm = i;
            uptr->wait = (60 * 1000000) / (cpm * 80);   /* Time between columns in usec.
                                                         * Readers are rated in card/min for 80 column cards */
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

/* simulate pressing the card reader RESET button  */
/* Per CR11 docs, transition to ONLINE, reset card */
/* reader logic.                                   */
/* RESET is somewhat of a misnomer; START is the function */

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
    /* Clear error bits                           */
    crs &= ~(CSR_ERR|CRCSR_CRDDONE|CRCSR_SUPPLY|CRCSR_RDCHK|CRCSR_TIMERR|
                     CRCSR_OFFLINE|CRCSR_BUSY|CRCSR_COLRDY|CRCSR_EJECT|CSR_GO);
    cdst |= CDCSR_ONLINE;
    cdst &= ~(CSR_ERR | CDCSR_OFFLINE | CDCSR_RDRCHK | CDCSR_HOPPER |
          CDCSR_EOF);
    /* I don't think the hardware clears these errors, but TOPS-10 seems to expect it.
     * Since we know the reader is idle, and this is OPR intervention, it seems safe.
    */
    cdst &= ~(CDCSR_LATE | CDCSR_NXM);

    /* Assert interrupt if interrupts enabled     */
    if ((CR11_CTL(uptr)?crs : cdst) & CSR_IE) {
        SET_INT (CR);
        if (DEBUG_PRS (cr_dev))
            fprintf (sim_deb, "cr_set_reset setting interrupt\n");
    }

    /* Reset controller status */
    cr_unit.buf = 0;
    currCol = 1;
    crb1 = 0;
    crb2 = 0;
    cdcc = 0;
    cdba = 0;
    cddb = 0;
    cddbs = 0;
    EOFcard = 0;
    
    /* start up the blower if the hopper is not empty 
    if (blowerState != BLOW_ON) {
        blowerState = BLOW_START;
        sim_activate_after(uptr, spinUp);
    }
    */
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
    cdst |= CSR_ERR | CDCSR_OFFLINE;
    /* CD11 does not appear to interrupt on STOP. */
    if (CR11_CTL(uptr) && (crs & CSR_IE))
        SET_INT (CR);
    if (blowerState != BLOW_OFF) {
        blowerState = BLOW_STOP;
    }
    return (SCPE_OK);
}

/* simulate pressing the card reader EOF button */

t_stat cr_set_eof (    UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    if (DEBUG_PRS (cr_dev))
        fprintf (sim_deb, "set_eof\n");
    eofPending = 1;

    return (SCPE_OK);
}

t_stat cr_show_eof ( FILE    *st,
                     UNIT    *uptr,
                     int32   val,
                     void    *desc    )
{
    fprintf (st, (eofPending? "EOF pending": "no EOF pending"));
    return (SCPE_OK);
}

t_stat cr_set_trans (   UNIT    *uptr,
                        int32   val,
                        char    *cptr,
                        void    *desc    )
{
    size_t  i;

    if (!cptr)
        return (SCPE_MISVAL);

    for (i = 0; i < NTRANS; i++) {
        if (strcmp (cptr, transcodes[i].name) == 0)
            break;
    }
    if (i >= NTRANS)
        return (SCPE_ARG);
    codeTbl = transcodes[i].table;
    initTranslation ();    /* reinitialize tables */
    return (SCPE_OK);
}

t_stat cr_show_trans (  FILE    *st,
                        UNIT    *uptr,
                        int32   val,
                        void    *desc    )
{
    size_t i;

    for (i = 1; i < NTRANS; i++ )
        if (transcodes[i].table == codeTbl) {
            fprintf (st, "translation=%s", transcodes[i].name);
            return SCPE_OK;
        }
    fprintf (st, "translation=%s", transcodes[0].name);
    return (SCPE_OK);
}

/* Only used from here to EOF, so not passing size of string.
 * This ugliness is more maintainable than a preprocessor mess.
 */

static void cr_supported ( char *string, int32 *bits )
{
int32 crtypes = 0;
#define MAXDESCRIP sizeof ("CR11/CD11/CD20/") /* sizeof includes \0 */
char devtype[MAXDESCRIP] = "";

#if defined (CR11_ONLY) || defined (CR11_OK)
    crtypes |= 1;
#endif
#if defined (CD11_ONLY) || defined (CD11_OK)
    crtypes |= 2;
#endif
#if defined (CD20_ONLY) || defined (CD20_OK)
    crtypes |= 4;
#endif

if (string) {
    if (crtypes & 1)
        strcat (devtype, "CR11/");
    if (crtypes & 2)
        strcat (devtype, "CD11/");
    if (crtypes & 4)
        strcat (devtype, "CD20/");
    devtype[strlen(devtype)-1] = '\0';
    strcpy (string, devtype);
}
if (bits)
    *bits = crtypes;
return;
}

static t_stat cr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
char devtype[MAXDESCRIP];
int32 crtypes;

cr_supported (devtype, &crtypes);
fprintf (st, "%s Card Reader (CR)\n\n", devtype);
fprintf (st, "The card reader (CR) implements a single controller (the model(s) shown\n");
fprintf (st, "above) and a card reader (e.g., Documation M200, GDI Model 100) by reading a\n");
fprintf (st, "file and presenting lines or cards to the simulator.  Card decks may be\n");
fprintf (st, "represented by plain text ASCII files, card image files, or column binary\n");
fprintf (st, "files.\n\n");

fprintf (st, "The controller is also compatible with the CM11-F, CME11, and CMS11.\n\n");

fprintf (st, "Card image files are a file format designed by Douglas W. Jones at the\n");
fprintf (st, "University of Iowa to support the interchange of card deck data.  These files\n");
fprintf (st, "have a much richer information carrying capacity than plain ASCII files.  Card\n");
fprintf (st, "Image files can contain such interchange information as card-stock color,\n");
fprintf (st, "corner cuts, special artwork, as well as the binary punch data representing all\n");
fprintf (st, "12 columns.  Complete details on the format, as well as sample code, are\n");
fprintf (st, "available at Prof. Jones's site: http://www.cs.uiowa.edu/~jones/cards/.\n\n");

if ((crtypes & -crtypes) != crtypes) {
    fprintf (st, "The card reader device an be configured to emulate the following\n");
    fprintf (st, "controller models with these commands:\n\n");
    if (crtypes & 1)
        fprintf (st, "    SET CR CR11       set controller type to CR11\n");
    if (crtypes & 2)
        fprintf (st, "    SET CR CD11       set controller type to CD11\n");
    if (crtypes & 4) {
        fprintf (st, "    SET CR CD20       set controller type to CD20\n");
#if defined (AIECO_OK)
        fprintf (st, "        SET CR AIECO  emulate the CD20 \"augmented image\" ECO\n");
        fprintf (st, "                      default is %semulated.\n", (DFLT_AIECO? "":"not "));
#endif
}
    fprintf (st, "\nThe controller type must be set before attaching a virtual card deck to the\n");
    fprintf (st, "device.  You may NOT change controller type once a file is attached.\n\n");
    fprintf (st, "The primary differences between the controllers are summarized in the\n");
    fprintf (st, "table below.  By default, %s simulation is selected.\n\n", 
                    (DFLT_TYPE & UNIT_CD20)? "CD20": ((DFLT_TYPE & UNIT_CR11)? "CR11" : "CD11"));
    fprintf (st, "                    CR11                CD11/CD20\n");
    fprintf (st, "    BR              6                   4\n");
    fprintf (st, "    registers       4                   3\n");
    fprintf (st, "    data transfer   BR                  DMA\n");
    fprintf (st, "    card rate       200-600         1000-1200\n");
    fprintf (st, "    hopper cap.     <= 1000         1000-2250\n");
    fprintf (st, "    cards           Mark-sense & punched only\n");
    fprintf (st, "                    punched\n\n");
    fprintf (st, "The CD11 simulation includes the Rev. J modification to make the CDDB act as\n");
    fprintf (st, "a second status register during non-data transfer periods.\n\n");
}
if (crtypes & 1) {
    fprintf (st, "Examples of the CR11 include the M8290 and M8291 (CMS11).  All card readers use\n");
    fprintf (st, "a common vector at 0230 and CSR at 177160.  Even though the CR11 is normally\n");
    fprintf (st, "configured as a BR6 device, it is configured for BR4 in this simulation.\n\n");
}
fprintf (st, "The card reader supports ASCII, card image, and column binary format card\n");
fprintf (st, "\"decks.\"  When reading plain ASCII files, lines longer than 80 characters are\n");
fprintf (st, "silently truncated.  Card image support is included for 80 column Hollerith,\n");
fprintf (st, "82 column Hollerith, and 40 column Hollerith (mark-sense) cards. \n");
fprintf (st, "Column binary supports 80 column card images only.\n");
if (crtypes & 6) {
    fprintf (st, "The CD11/CD20 optionally check columns 0/81/41 for punches, which produce\n");
    fprintf( st, "read check errors.  As verifiers may produce these, this can be controlled:\n");
    fprintf( st, "    SET CR RDCHECK   - Enable read check errors (default)\n");
    fprintf( st, "    SET CR NORDCHECK - Disable read check errors\n\n");
}
fprintf (st, "All files are attached read-only (as if the -R switch were given).\n");
fprintf (st, "    ATTACH -A CR <file>           file is ASCII text\n");
fprintf (st, "    ATTACH -B CR <file>           file is column binary\n");
fprintf (st, "    ATTACH -I CR <file>           file is card image format\n\n");

fprintf (st, "If no flags are given, the file extension is evaluated.  If the filename ends\n");
fprintf (st, "in .TXT, the file is treated as ASCII text.  If the filename ends in .CBN, the\n");
fprintf (st, "file is treated as column binary.  Otherwise, the CR driver looks for a card\n");
fprintf (st, "image header.  If a correct header is found the file is treated as card image\n");
fprintf (st, "format, otherwise it is treated as ASCII text.\n\n");

fprintf (st, "The correct character translation MUST be set if a plain text file is to be\n");
fprintf (st, "used for card deck input.  The correct translation SHOULD be set to allow\n");
fprintf (st, "correct ASCII debugging of a card image or column binary input deck.  Depending\n");
fprintf (st, "upon the operating system in use, how it was generated, and how the card data\n");
fprintf (st, "will be read and used, the translation must be set correctly so that the proper\n");
fprintf (st, "character set is used by the driver.  Use the following command to explicitly\n");
fprintf (st, "set the correct translation:\n\n");
fprintf (st, "    SET TRANSLATION={DEFAULT|026|026FTN|026DEC|026DECASCII|029|029DECASCII|EBCDIC}\n\n");
fprintf (st, "This command should be given after a deck is attached to the simulator.  The\n");
fprintf (st, "mappings above are completely described at\n");
fprintf (st, "    http://www.cs.uiowa.edu/~jones/cards/codes.html.\n");
fprintf (st, "Note that early DEC software typically used 029 or 026FTN mappings.\n");
fprintf (st, "Later systems used the 026DECASCII and/or 029DECASCII mappings, which include all 7-bit ASCII characters\n");
fprintf (st, "DEC operating systems used a variety of methods to determine the end of a deck\n");
fprintf (st, "(recognizing that 'hopper empty' does not necessarily mean the end of a deck.\n");
fprintf (st, "Below is a summary of the various operating system conventions for signaling\n");
fprintf (st, "end of deck (or end of file with multi-file batch systems):\n\n");
fprintf (st, "    RT-11:    12-11-0-1-6-7-8-9 punch in column 1\n");
fprintf (st, "    RSTS/E:   12-11-0-1 or 12-11-0-1-6-7-8-9 punch in column 1\n");
fprintf (st, "    RSX:      12-11-0-1-6-7-8-9 punch in first 8 columns\n");
fprintf (st, "    VMS:      12-11-0-1-6-7-8-9 punch in first 8 columns\n");
fprintf (st, "    TOPS:     12-11-0-1 or 12-11-0-1-6-7-8-9 punch in column 1\n\n");
fprintf (st, "Using the AUTOEOF setting, the card reader can be set to automatically generate\n");
fprintf (st, "an EOF card consisting of the 12-11-0-1-6-7-8-9 punch in columns 1-8.  ");
if (crtypes & 6) {
    fprintf (st, "When set,\nThe %s ", ((crtypes & 6) == 2)? "CD11": ((crtypes & 6) == 4)? "CD20": "CD11/CD20");

    fprintf (st,                    "will automatically set the EOF bit in the\n");
    fprintf (st, "controller after the EOF card has been processed.  By default AUTOEOF is enabled.\n");
    fprintf (st, "The controller also supports an EOF switch that will set the EOF bit when the\n");
    fprintf (st, "hopper empties.  The switch resets each time the hopper empties.  The SET EOF command emulates this.\n");
    if (crtypes &1)
        fprintf (st, "The CR11 does not support the EOF switch/bit.\n");
    else
        fprintf (st, "\n");
}
fprintf (st, "The default card reader rate for the ");
if (crtypes & 4) {
    fprintf (st,                                         "CD20 is 1200");
    if (crtypes != 4)
        fprintf (st, " and for the ");
}
if (crtypes & 3)
    fprintf (st,                                         "CR/CD11 is 285");
fprintf (st, " cpm.\n");
fprintf (st, "The reader rate can be set to its default value or to anywhere in the range\n");
fprintf (st, "of 200 to 1200 cpm.This rate may be changed while the unit is attached.\n\n");
fprintf (st, "It is standard operating procedure for operators to load a card deck and press\n");
fprintf (st, "the momentary action RESET button to clear any error conditions and alert the\n");
fprintf (st, "processor that a deck is available to read.  Use the SET CR RESET command to\n");
fprintf (st, "simulate pressing the card reader RESET button.\n\n");
fprintf (st, "Another common control of physical card readers is the STOP button.  An\n");
fprintf (st, "operator could use this button to finish the read operation for the current\n");
fprintf (st, "card and terminate reading a deck early.  Use the SET CR STOP command to\n");
fprintf (st, "simulate pressing the card reader STOP button.\n\n");
fprintf (st, "The simulator does not support the BOOT command.  The simulator does not\n");
fprintf (st, "stop on file I/O errors.  Instead the controller signals a reader check to\n");
fprintf (st, "the CPU.\n");

fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *cr_description (DEVICE *dptr)
{
  /* Not thread-safe, but malloc() would be leak. */
  static char desc[MAXDESCRIP+sizeof(" card reader")-1] = "";
  if (desc[0] == '\0') {
      cr_supported (desc, NULL);
      strcat (desc, " card reader");
  }
  return desc;
}
