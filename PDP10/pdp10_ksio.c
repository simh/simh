/* pdp10_ksio.c: PDP-10 KS10 I/O subsystem simulator

   Copyright (c) 1993-2008, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   uba          Unibus adapters

   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   25-Jan-04    RMS     Added stub floating address routine
   12-Mar-03    RMS     Added logical name support
   10-Oct-02    RMS     Revised for dynamic table generation
                        Added SHOW IOSPACE routine
   29-Sep-02    RMS     Added variable vector, central map support
   25-Jan-02    RMS     Revised for multiple DZ11's
   06-Jan-02    RMS     Revised enable/disable support
   23-Sep-01    RMS     New IO page address constants
   07-Sep-01    RMS     Revised device disable mechanism
   25-Aug-01    RMS     Enabled DZ11
   21-Aug-01    RMS     Updated DZ11 disable
   01-Jun-01    RMS     Updated DZ11 vectors
   12-May-01    RMS     Fixed typo

   The KS10 uses the PDP-11 Unibus for its I/O, via adapters.  While
   nominally four adapters are supported, in practice only 1 and 3
   are implemented.  The disks are placed on adapter 1, the rest of
   the I/O devices on adapter 3. (adapter 4 IS used in some supported
   configurations, but those devices haven't been emulated yet.)

   In theory, we should maintain completely separate Unibuses, with
   distinct PI systems.  In practice, this simulator has so few devices
   that we can get away with a single PI system, masking for which
   devices are on adapter 1, and which on adapter 3.  The Unibus
   implementation is modeled on the Qbus in the PDP-11 simulator and
   is described there.

   The I/O subsystem is programmed by I/O instructions which create
   Unibus operations (read, read pause, write, write byte).  DMA is
   the responsibility of the I/O device simulators, which also implement
   Unibus to physical memory mapping.

   The priority interrupt subsystem (and other privileged functions)
   is programmed by I/O instructions with internal devices codes
   (opcodes 700-702).  These are dispatched here, although many are
   handled in the memory management unit or elsewhere.

   The ITS instructions are significantly different from the TOPS-10/20
   instructions.  They do not use the extended address calculation but
   instead provide instruction variants (Q for Unibus adapter 1, I for
   Unibus adapter 3) which insert the Unibus adapter number into the
   effective address.
*/

#include "pdp10_defs.h"
#include <setjmp.h>
#include <assert.h>
#include "sim_sock.h"
#include "sim_tmxr.h"

#define AUTO_MAXC       32              /* Maximum number of controllers */
#define AUTO_CSRBASE    0010
#define AUTO_CSRMAX    04000
#define AUTO_VECBASE    0300

#define UBMPAGE(x) (x & (PAG_VPN<<2))                   /* UBA Map page field of 11 address */
#define XBA_MBZ         0400000                         /* ba mbz */
#define eaRB            (ea & ~1)
#define GETBYTE(ea,x)   ((((ea) & 1)? (x) >> 8: (x)) & 0377)
#define UBNXM_FAIL(pa,op) \
                        n = ADDR2UBA (pa); \
                        if (n >= 0) \
                            ubcs[n] = ubcs[n] | UBCS_TMO | UBCS_NXD; \
                        pager_word = PF_HARD | PF_VIRT | PF_IO | \
                            ((op == WRITEB)? PF_BYTE: 0) | \
                            (TSTF (F_USR)? PF_USER: 0) | (pa); \
                        ABORT (PAGE_FAIL)
/* Is Unibus address mapped to -10 memory */
#define TEN_MAPPED(ub,ba) ((ubmap[ub][PAG_GETVPN(((ba) & 0777777) >> 2)] & UMAP_VLD) != 0)

/* Translate UBA number in a PA to UBA index.  1,,* -> ubmap[0], all others -> ubmap[1] */
#define ADDR2UBA(x) (iocmap[GET_IOUBA (x)])

/* Unibus adapter data */

int32 ubcs[UBANUM] = { 0 };                             /* status registers */
int32 ubmap[UBANUM][UMAP_MEMSIZE] = {{ 0 }};            /* Unibus maps */
int32 int_req = 0;                                      /* interrupt requests */

int32 autcon_enb = 1;                                   /* auto configure enabled */


/* Map IO controller numbers to Unibus adapters: -1 = non-existent */

static int iocmap[IO_N_UBA] = {                         /* map I/O ext to UBA # */
 -1, 0, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
 }; 

static const int32 ubabr76[UBANUM] = {
    INT_UB1 & (INT_IPL7 | INT_IPL6), INT_UB3 & (INT_IPL7 | INT_IPL6)
    };
static const int32 ubabr54[UBANUM] = {
    INT_UB1 & (INT_IPL5 | INT_IPL4), INT_UB3 & (INT_IPL5 | INT_IPL4)
    };

/* Masks for Unibus quantities */
#define M_BYTE   (0xFF)
#define M_WORD   (0xFFFF)
#define M_WORD18 (0777777)
#define M_LH     (0777777000000)
#define M_RH     (0000000777777)

/* Bits to shift for each Unibus byte */
#define V_BYTE0 (18)
#define V_BYTE1 (26)
#define V_BYTE2 (0)
#define V_BYTE3 (8)

#define V_WORD0 V_BYTE0
#define V_WORD1 V_BYTE2

#if 0
static const int32 ubashf[4] = { V_BYTE0, V_BYTE1, V_BYTE2, V_BYTE3 };
#endif

/* Bits to preserve when writing each Unibus byte.
 * This excludes the XX bits so they are cleared.
 */
#define M_BYTE0 (~INT64_C (0000377000000)) /* Clear byte 0 */
#define M_BYTE1 (~INT64_C (0777400000000)) /* Clear byte 1 + XX */
#define M_BYTE2 (~INT64_C (0000000000377)) /* Clear byte 2 */
#define M_BYTE3 (~INT64_C (0000000777400)) /* Clear byte 3 + XX */

#define M_WORD0 (~INT64_C (0777777000000)) /* Clear word 0 + XX */
#define M_WORD1 (~INT64_C (0000000777777)) /* Clear word 1 + XX */

#if 0
static const d10 ubamask[4] = { M_BYTE0, M_BYTE1, M_BYTE2, M_BYTE3 };
#endif

extern d10 *M;                                          /* main memory */
extern d10 *ac_cur;
extern d10 pager_word;
extern int32 flags;
extern const int32 pi_l2bit[8];
extern UNIT cpu_unit;
extern jmp_buf save_env;

extern int32 pi_eval (void);
extern int32 rp_inta (void);
extern int32 tu_inta (void);
extern int32 lp20_inta (void);
extern int32 dz_rxinta (void);
extern int32 dz_txinta (void);

t_stat ubmap_rd (int32 *data, int32 addr, int32 access);
t_stat ubmap_wr (int32 data, int32 addr, int32 access);
t_stat ubs_rd (int32 *data, int32 addr, int32 access);
t_stat ubs_wr (int32 data, int32 addr, int32 access);
t_stat rd_zro (int32 *data, int32 addr, int32 access);
t_stat wr_nop (int32 data, int32 addr, int32 access);
t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_reset (DEVICE *dptr);
d10 ReadIO (a10 ea);
void WriteIO (a10 ea, d10 val, int32 mode);

/* Unibus adapter data structures

   uba_dev      UBA device descriptor
   uba_unit     UBA units
   uba_reg      UBA register list
*/

DIB ubmp1_dib = { IOBA_UBMAP1, IOLN_UBMAP1, &ubmap_rd, &ubmap_wr, 0 };
DIB ubmp3_dib = { IOBA_UBMAP3, IOLN_UBMAP3, &ubmap_rd, &ubmap_wr, 0 };
DIB ubcs1_dib = { IOBA_UBCS1, IOLN_UBCS1, &ubs_rd, &ubs_wr, 0 };
DIB ubcs3_dib = { IOBA_UBCS3, IOLN_UBCS3, &ubs_rd, &ubs_wr, 0 };
DIB ubmn1_dib = { IOBA_UBMNT1, IOLN_UBMNT1, &rd_zro, &wr_nop, 0 };
DIB ubmn3_dib = { IOBA_UBMNT3, IOLN_UBMNT3, &rd_zro, &wr_nop, 0 };
DIB msys_dib = { 00100000, 1, &rd_zro, &wr_nop, 0 };

UNIT uba_unit[] = {
    { UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) },
    { UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) }
    };

REG uba_reg[] = {
    { ORDATA (INTREQ, int_req, 32), REG_RO },
    { ORDATA (UB1CS, ubcs[0], 18) },
    { ORDATA (UB3CS, ubcs[1], 18) },
    { NULL }
    };

DEVICE uba_dev = {
    "UBA", uba_unit, uba_reg, NULL,
    UBANUM, 8, UMAP_ASIZE, 1, 8, 32,
    &uba_ex, &uba_dep, &uba_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* PDP-11 I/O structures */

DIB *dib_tab[DIB_MAX];                                  /* run-time DIBs */

int32 (*int_ack[32])(void);                             /* int ack routines */

int32 int_vec[32];                                      /* int vectors */

DIB *std_dib[] = {                                      /* standard DIBs */
    &ubmp1_dib,
    &ubmp3_dib,
    &ubcs1_dib,
    &ubcs3_dib,
    &ubmn1_dib,
    &ubmn3_dib,
    &msys_dib,
    NULL
    };

/* IO 710       (DEC) TIOE - test I/O word, skip if zero
                (ITS) IORDI - read word from Unibus 3
                returns TRUE if skip, FALSE otherwise
*/

t_bool io710 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS)                                              /* IORDI */
    AC(ac) = ReadIO (IO_UBA3 | ea);
else {                                                  /* TIOE */
    val = ReadIO (ea);                                  /* read word */
    if ((AC(ac) & val) == 0)
        return TRUE;
    }
return FALSE;
}

/* IO 711       (DEC) TION - test I/O word, skip if non-zero
                (ITS) IORDQ - read word from Unibus 1
                returns TRUE if skip, FALSE otherwise
*/

t_bool io711 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS)                                              /* IORDQ */
    AC(ac) = ReadIO (IO_UBA1 | ea);
else {                                                  /* TION */
    val = ReadIO (ea);                                  /* read word */
    if ((AC(ac) & val) != 0)
        return TRUE;
    }
return FALSE;
}

/* IO 712       (DEC) RDIO - read I/O word, addr in ea
                (ITS) IORD - read I/O word, addr in M[ea]
*/

d10 io712 (a10 ea)
{
return ReadIO (ea);                                     /* RDIO, IORD */
}

/* IO 713       (DEC) WRIO - write I/O word, addr in ea
                (ITS) IOWR - write I/O word, addr in M[ea]
*/

void io713 (d10 val, a10 ea)
{
WriteIO (ea, val, WRITE);                     /* WRIO, IOWR */
return;
}

/* IO 714       (DEC) BSIO - set bit in I/O address
                (ITS) IOWRI - write word to Unibus 3
*/

void io714 (d10 val, a10 ea)
{
d10 temp;

if (Q_ITS)                                              /* IOWRI */
    WriteIO (IO_UBA3 | ea, val, WRITE);
else {
    temp = ReadIO (ea);                                 /* BSIO */
    temp = temp | val;
    WriteIO (ea, temp, WRITE);
    }
return;
}

/* IO 715       (DEC) BCIO - clear bit in I/O address
                (ITS) IOWRQ - write word to Unibus 1
*/

void io715 (d10 val, a10 ea)
{
d10 temp;

if (Q_ITS)                                              /* IOWRQ */
    WriteIO (IO_UBA1 | ea, val, WRITE);
else {
    temp = ReadIO (ea);                                 /* BCIO */
    temp = temp & ~val;
    WriteIO (ea, temp, WRITE);
    }
return;
}

/* IO 720       (DEC) TIOEB - test I/O byte, skip if zero
                (ITS) IORDBI - read byte from Unibus 3
                returns TRUE if skip, FALSE otherwise
*/

t_bool io720 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS) {                                            /* IORDBI */
    val = ReadIO (IO_UBA3 | eaRB);
    AC(ac) = GETBYTE (ea, val);
    }
else {                                                  /* TIOEB */
    val = ReadIO (eaRB);
    val = GETBYTE (ea, val);
    if ((AC(ac) & val) == 0)
        return TRUE;
    }
return FALSE;
}

/* IO 721       (DEC) TIONB - test I/O word, skip if non-zero
                (ITS) IORDBQ - read word from Unibus 1
                returns TRUE if skip, FALSE otherwise
*/

t_bool io721 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS) {                                            /* IORDBQ */
    val = ReadIO (IO_UBA1 | eaRB);
    AC(ac) = GETBYTE (ea, val);
    }
else {                                                  /* TIONB */
    val = ReadIO (eaRB);
    val = GETBYTE (ea, val);
    if ((AC(ac) & val) != 0)
        return TRUE;
    }
return FALSE;
}

/* IO 722       (DEC) RDIOB - read I/O byte, addr in ea
                (ITS) IORDB - read I/O byte, addr in M[ea]
*/

d10 io722 (a10 ea)
{
d10 val;

val = ReadIO (eaRB);                                    /* RDIOB, IORDB */
return GETBYTE (ea, val);
}

/* IO 723       (DEC) WRIOB - write I/O byte, addr in ea
                (ITS) IOWRB - write I/O byte, addr in M[ea]
*/

void io723 (d10 val, a10 ea)
{
WriteIO (ea, val & M_BYTE, WRITEB);                       /* WRIOB, IOWRB */
return;
}

/* IO 724       (DEC) BSIOB - set bit in I/O byte address
                (ITS) IOWRBI - write byte to Unibus 3
*/

void io724 (d10 val, a10 ea)
{
d10 temp;

val = val & M_BYTE;
if (Q_ITS)                                              /* IOWRBI */
    WriteIO (IO_UBA3 | ea, val, WRITEB);
else {
    temp = ReadIO (eaRB);                               /* BSIOB */
    temp = GETBYTE (ea, temp);
    temp = temp | val;
    WriteIO (ea, temp, WRITEB);
    }
return;
}

/* IO 725       (DEC) BCIOB - clear bit in I/O byte address
                (ITS) IOWRBQ - write byte to Unibus 1
*/

void io725 (d10 val, a10 ea)
{
d10 temp;

val = val & M_BYTE;
if (Q_ITS)                                              /* IOWRBQ */
    WriteIO (IO_UBA1 | ea, val, WRITEB);
else {
    temp = ReadIO (eaRB);                               /* BCIOB */
    temp = GETBYTE (ea, temp);
    temp = temp & ~val;
    WriteIO (ea, temp, WRITEB);
    }
return;
}

/* Read and write I/O devices.
   These routines are the linkage between the 64b world of the main
   simulator and the 32b world of the device simulators.
*/

/* UBReadIO and UBWriteIO handle the device lookup and access
 * These are used for all IO space accesses.  They return status.
 *
 * ReadIO and WriteIO are used by the CPU instructions, and generate
 * UBA NXM page fails for unassigned IO addresses.
 */

static t_stat UBReadIO (int32 *data, int32 ba, int32 access)
{
uint32 pa = (uint32) ba;
int32 i, val;
DIB *dibp;

for (i = 0; (dibp = dib_tab[i]); i++ ) {
    if ((pa >= dibp->ba) &&
       (pa < (dibp->ba + dibp->lnt))) {
        dibp->rd (&val, pa, access);
        pi_eval ();
        *data = val;
        return SCPE_OK;
        }
    }
return SCPE_NXM;
}

d10 ReadIO (a10 ea)
{
uint32 pa = (uint32) ea;
int32 n, val;

    if (UBReadIO (&val, pa, READ) == SCPE_OK)
        return ((d10) val);
    UBNXM_FAIL (pa, READ);
}


static t_stat UBWriteIO (int32 data, int32 ba, int32 access)
{
uint32 pa = (uint32) ba;
int32 i;
DIB *dibp;

for (i = 0; (dibp = dib_tab[i]); i++ ) {
    if ((pa >= dibp->ba) &&
       (pa < (dibp->ba + dibp->lnt))) {
        if ((dibp->flags & DIB_M_REGSIZE) == DIB_REG16BIT) {
            data &= M_WORD;
            }
        dibp->wr (data, ba, access);
        pi_eval ();
        return SCPE_OK;
        } 
    }
return SCPE_NXM;
}

void WriteIO (a10 ea, d10 val, int32 mode)
{
uint32 pa = (uint32) ea;
int32 n;

if (UBWriteIO ((int32) val, (int32) pa, mode) == SCPE_OK)
    return;
UBNXM_FAIL (pa, mode);
}

/* Mapped read and write routines - used by standard Unibus devices on Unibus 1
 * I/O space accesses will work.  Note that Unibus addresses with bit 17 set can
 * not be mapped by the UBA, so I/O space (and more) can not be mapped to -10 memory.
 */

static a10 Map_Addr10 (a10 ba, int32 ub, int32 *ubmp)
{
a10 pa10;
int32 vpn = PAG_GETVPN (ba >> 2);                       /* get PDP-10 page number */
int32 ubm;

if ((vpn >= UMAP_MEMSIZE) || (ba & XBA_MBZ)) {          /* Validate bus address */
    if (ubmp)
        *ubmp = 0;
    return -1;
}
ubm =  ubmap[ub][vpn];
if (ubmp)
    *ubmp = ubm;

if ((ubm & UMAP_VLD) == 0)                              /* Ensure map entry is valid */
    return -1;
pa10 = (ubm + PAG_GETOFF (ba >> 2)) & PAMASK;
return pa10;
}

/* Routines for Bytes, Words (16-bit) and Words (18-bit).
 *
 * Note that the byte count argument is always BYTES, even if
 * the unit transfered is a word.  This is for compatibility with
 * the 11/VAX system Unibus; these routines abstract DMA for all
 * U/Q device simulations.
 *
 * All return the number of bytes NOT transferred; 0 means success.
 * A non-zero return implies a NXM was encountered.
 *
 * Unaligned accesses to 16/18-bit words in IOSPACE are a STOP condition.
 * (Should be in memory too, but some devices are lazy.)
 *
 * Unibus memory is mapped into 36-bit words so that 16-bit
 * values appear in 18-bit half-words, and PDP10 byte pointers will
 * increment through 16-bit (but not 8-bit) data.  Viewed as bytes or
 * words from the PDP10, memory looks like this:
 *
 * +-----+-----------+------------+-------+------------+------------+
 * | 0 1 | 2       9 | 10      17 | 18 19 | 20       27| 28      35 | PDP10 bits
 * +-----+-----------+------------+-------+------------+------------+
 * | X X | BYTE 1<01>| BYTE 0<00> |  X X  | BYTE 3<11> | BYTE 2<10> | PDP11 bytes
 * +-----+-----------+------------+-------+------------+------------+
 * | X X |        WORD 0     <00> |  X X  |        WORD 1      <10> | PDP11 words
 * +-----+-----------+------------+-------+------------+------------+
 *
 * <nn> are the values of the two low-order address bits as viewed on
 * the Unibus.
 *
 * The bits marked XX are written as zero for 8 and 16 bit transfers
 * and with data from the Unibus parity lines for 18 bit transfers.
 * In a -10 read-modify-write cycle, they are cleared if the high byte
 * of the adjacent word is written, and preserved otherwise.
 *
 * Unibus addressing does not change with 18-bit transfers; they are
 * accounted for as 2 bytes.  <0:1> are bits <17:16> of word 0; 
 * <18:19> are bits <17:16> of word 1.
 *
 * Normal writes assume that DMA will access sequential Unibus addresses.
 * The UBA optimizes this by writing NPR data to <00> addresses
 * without preserving the rest of the -10 word.  This allows a memory
 * write cycle, rather than the read-modify-write cycle required to
 * preserve the rest of the word.  The 'read reverse' bit in the UBA
 * map forces a read-modify-write on all addresses.
 *
 * 16-bit transfers (the d18 bit in the map selects) write 0s into
 * the correspnding X bits when <00> or <10> are written.
 *
 * Address mapping uses bits <1:0> of the Unibus address to select
 * the byte as indicated above.  Bits <10:2> are the offset within
 * the PDP10 page; thus Unibus addressing assumes 4 bytes/PDP10 word.
 *
 * 9 bits = 512 words/PDP10 page = 2048 bytes / Unibus page 
 *
 * Bits 16:11 select a UBA mapping register, which indicates whether
 * PDP10 memory at that address is accessible, and if so, provides
 * PDP10 bus address bits that replace and extend the Unibus bits.
 *
 * Unibus addresses with bit 17 set do not map PDP10 memory.  The
 * high end is reserved for Unibus IO space.  The rest is used for
 * UBA maintenance modes (not simulated).
 * 
 * IO space accesses may have side effects in the device; an aligned
 * read of two bytes is NOT equivalent to two one byte reads of the
 * same addresses.
 *
 * The memory access in these routines is optimized to minimize UBA
 * page table lookups and shift/merge operations with PDP10 memory.
 *
 * Memory transfers happen in up to 3 pieces:
 *   head : 0-3 bytes to an aligned PDP10 word (UB address 000b)
 *   body : As many PDP10 whole words as possible (4 bytes 32/36 bits)
 *   tail : 0-3 bytes remaining after the body.
 */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 ea, ofs, cp, np;
int32 seg;
a10 pa10 = ~0u;
d10 m;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000) {
    /* IOPAGE: device register read */
    int32 csr;

    while (bc) {
        if (UBReadIO (&csr, ba & ~1, READ) != SCPE_OK)
            break;
        *buf++ = (ba & 1)? ((csr >> 8) & 0xff): csr & 0xff;
        ba++;
        bc--;
        }
    return bc;
    }

/* Memory */

if (bc == 0)
    return 0;

cp = ~ba;
ofs = ba & 3;
seg = (4 - ofs) & 3;

if (seg) {                                              /* Unaligned head */
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                                  /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, NULL);                    /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {            /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;                   /* UBA timeout */
        return bc;                                      /* return bc */
        }
    m = M[pa10++];
    ba += seg;
    bc -= seg;
    switch (ofs) {
    case 1:
        *buf++ = (uint8) ((m >> V_BYTE1) & M_BYTE);
        if (!--seg)
            break;
    case 2:
        *buf++ = (uint8) (m & M_BYTE); /* V_BYTE2 */
        if (!--seg)
            break;
    case 3:
        *buf++ = (uint8) ((m >> V_BYTE3) & M_BYTE);
        --seg;
        break;
    default:
        assert (FALSE);
        }
    if (bc == 0)
        return 0;
    } /* Head */

/* At this point, ba is aligned.  Therefore, ea<1:0> are the tail's length */
ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) { /* Body: Whole PDP-10 words, 4 bytes */
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, NULL);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        m = M[pa10++];                          /* Next word from -10 */
        buf[2] = (uint8) (m & M_BYTE);          /* Byte 2 */
        m >>= 8;
        buf[3] = (uint8) (m & M_BYTE);          /* Byte 3 */
        m >>= 10;
        buf[0] = (uint8) (m & M_BYTE);          /* Byte 0 */
        m >>= 8;
        buf[1] = (uint8) (m & M_BYTE);          /* Byte 1 */
        buf += 4;
        }
    } /* Body */

 /* Tail: partial -10 word, must be aligned. 1-3 bytes */
assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc <= 3);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, NULL);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {/* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
    }
    m = M[pa10];
    switch (bc) {
    case 3:
        buf[2] = (uint8) (m & M_BYTE);          /* V_BYTE2 */
    case 2:
        buf[1] = (uint8) ((m >> V_BYTE1) & M_BYTE);
    case 1:
        buf[0] = (uint8) ((m >> V_BYTE0) & M_BYTE);
        break;
    default:
        assert (FALSE);
        }
    }

return 0;
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 ea, cp, np;
int32 seg;
a10 pa10 = ~0u;
d10 m;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000) {
    /* IOPAGE: device register read */
    int32 csr;

    if ((ba | bc) & 1)
        ABORT (STOP_IOALIGN);

    while (bc) {
        if (UBReadIO (&csr, ba, READ) != SCPE_OK)
            break;
        *buf++ = (uint16)csr;
        ba += 2;
        bc -= 2;
        }
    return bc;
    }

/* Memory */

if (bc == 0)
    return 0;

ba &= ~1;
if (bc & 1)
    ABORT (STOP_IOALIGN);

cp = ~ba;
seg = (4 - (ba & 3)) & 3;

if (seg) {                                      /* Unaligned head, can only be WORD1 */
    assert ((ba & 2) && (seg == 2));
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                          /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, NULL);            /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;           /* UBA timeout */
        return bc;                              /* return bc */
        }
    ba += seg;
    *buf++ = (uint16) (M[pa10++] & M_WORD);
    if ((bc -= seg) == 0)
        return 0;
    } /* Head */

ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) {
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, NULL);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        m = M[pa10++];                          /* Next word from -10 */
        buf[1] = (uint16) (m & M_WORD);         /* Bytes 3,,2 */
        m >>= 18;
        buf[0] = (uint16) (m & M_WORD);         /* Bytes 1,,0 */
        buf += 2;
        }
    } /* Body */

/* Tail: partial word, must be aligned, can only be WORD0 */
assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc == 2);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, NULL);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {/* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
        }
    *buf = (uint16) ((M[pa10] >> V_WORD0) & M_WORD);
    }

return 0;
}

/* Word reads returning 18-bit data
 *
 * Identical to 16-bit reads except that buffer is uint32
 * and masked to 18 bits.
 */

int32 Map_ReadW18 (uint32 ba, int32 bc, uint32 *buf)
{
uint32 ea, cp, np;
int32 seg;
a10 pa10 = ~0u;
d10 m;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000) {
    /* IOPAGE: device register read */
    int32 csr;

    if ((ba | bc) & 1)
        ABORT (STOP_IOALIGN);

    while (bc) {
        if (UBReadIO (&csr, ba, READ) != SCPE_OK)
            break;
        *buf++ = (uint32)csr;
        ba += 2;
        bc -= 2;
        }
    return bc;
    }

/* Memory */

if (bc == 0)
    return 0;

ba &= ~1;
if (bc & 1)
    ABORT (STOP_IOALIGN);

cp = ~ba;
seg = (4 - (ba & 3)) & 3;

if (seg) {                                      /* Unaligned head */
    assert ((ba & 2) && (seg == 2));
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                          /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, NULL);            /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;           /* UBA timeout */
        return bc;                              /* return bc */
        }
    ba += seg;
    *buf++ = (uint32) (M[pa10++] & M_RH);
    if ((bc -= seg) == 0)
        return 0;
    } /* Head */

ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) {
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, NULL);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        m = M[pa10++];                          /* Next word from -10 */
        buf[1] = (uint32) (m & M_RH);           /* Bytes 3,,2 */
        m >>= 18;
        buf[0] = (uint32) (m & M_RH);           /* Bytes 1,,0 */
        buf += 2;
        }
    } /* Body */

/* Tail: partial word, must be aligned */
assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc == 2);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, NULL);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) { /* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
        }
    *buf++ = (uint32) ((M[pa10] >> V_WORD0) & M_RH);
    }

return 0;
}

/* Byte-mode writes */

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 ea, ofs, cp, np;
int32 seg, ubm = 0;
a10 pa10 = ~0u;
d10 m;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000) {
    /* IOPAGE: device register write */

    while (bc) {
        if (UBWriteIO (*buf++ & 0xff, ba, WRITEB) != SCPE_OK)
            break;
        ba++;
        bc--;
        }
    return bc;
    }

/* Memory */

if (bc == 0)
    return 0;

cp = ~ba;
ofs = ba & 3;
seg = (4 - ofs) & 3;

if (seg) {                                      /* Unaligned head */
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                          /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, &ubm);            /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;           /* UBA timeout */
        return bc;                              /* return bc */
        }
    m = M[pa10];
    ba += seg;
    bc -= seg;
    switch (ofs) {
    case 1:
        m = (m & M_BYTE1) | (((d10) (*buf++)) << V_BYTE1);
        if (!--seg)
            break;
    case 2:
        m = (m & M_BYTE2) | ((d10) (*buf++)); /* V_BYTE2 */
        if (!--seg)
            break;
    case 3:
        m = (m & M_BYTE3) | (((d10) (*buf++)) << V_BYTE3);
        --seg;
        break;
    default:
        assert (FALSE);
        }
    M[pa10++] = m;
    if (bc == 0)
        return 0;
    } /* Head */

ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) {
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, &ubm);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        M[pa10++] = (((d10)((buf[1] << 8) | buf[0])) << 18) | /* <0:1,18:19> = 0 */
                           ((buf[3] << 8) | buf[2]);
        buf += 4;
        }
    } /* Body */

/* Tail: partial word, must be aligned */

assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc <= 3);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, &ubm);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) { /* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
    }
    m = M[pa10];
    if ((ubm & UMAP_RRV )) { /* RMW */
        switch (bc) {
        case 3:
            m = (m & M_BYTE2) | ((d10) (buf[2])); /* V_BYTE2 */
        case 2:
            m = (m & M_BYTE1) | (((d10) (buf[1])) << V_BYTE1);
        case 1:
            m = (m & M_BYTE0) | (((d10) (buf[0])) << V_BYTE0);
            break;
        default:
            assert (FALSE);
            }
        }
    else {
        switch (bc) { /* Write byte 0 + RMW bytes 1 & 2 */
        case 3:
            m = (((d10) (buf[1])) << V_BYTE1) | (((d10) (buf[0])) << V_BYTE0) |
                                                 ((d10) (buf[2])); /* V_BYTE2 */
            break;
        case 2:
            m = (((d10) (buf[1])) << V_BYTE1) | (((d10) (buf[0])) << V_BYTE0);
            break;
        case 1:
            m = ((d10) (buf[0])) << V_BYTE0;
            break;
        default:
            assert (FALSE);
            }
        }
    M[pa10] = m;
    }

return 0;
}

/* Word mode writes; 16-bit data */

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 ea, cp, np;
int32 seg, ubm = 0;
a10 pa10 = ~0u;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000) {
    /* IOPAGE: device register write */

    if ((ba | bc) & 1)
        ABORT (STOP_IOALIGN);

    while (bc) {
        if (UBWriteIO (*buf++ & 0xffff, ba, WRITE) != SCPE_OK)
            break;
        ba += 2;
        bc -= 2;
        }
    return bc;
    }

/* Memory */

if (bc == 0)
    return 0;

ba &= ~1;
if (bc & 1)
    ABORT (STOP_IOALIGN);

cp = ~ba;
seg = (4 - (ba & 3)) & 3;

if (seg) {                                      /* Unaligned head */
    assert ((ba & 2) && (seg == 2));
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                          /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, &ubm);            /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;           /* UBA timeout */
        return bc;                              /* return bc */
        }
    M[pa10] = (M[pa10] & M_WORD1) | ((d10) (*buf++));
    pa10++;

    if ((bc -= seg) == 0)
        return 0;
    ba += seg;
    } /* Head */

ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) {
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, &ubm);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        M[pa10++] = (((d10)(buf[0])) << V_WORD0) | buf[1];/* <0:1,18:19> = 0
                                                           * V_WORD1
                                                           */
        buf += 2;
        }
    } /* Body */

/* Tail: partial word, must be aligned, can only be WORD0 */
assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc == 2);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, &ubm);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) { /* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
        }
    if (ubm & UMAP_RRV )                        /* Read reverse preserves RH */
        M[pa10] = (((d10)(buf[0])) << V_WORD0) | (M[pa10] & M_WORD0);
    else
        M[pa10] =  ((d10)(buf[0])) << V_WORD0;
}

return 0;
}


/* Word mode writes; 18-bit data */

int32 Map_WriteW18 (uint32 ba, int32 bc, uint32 *buf)
{
uint32 ea, cp, np;
int32 seg, ubm = 0;
a10 pa10 = ~0u;

if ((ba & ~((IO_M_UBA<<IO_V_UBA)|0017777)) == 0760000)
{ /* IOPAGE: device register write */

    if ((ba | bc) & 1)
        ABORT (STOP_IOALIGN);

    while (bc) {
        if (UBWriteIO (*buf++ & M_RH, ba, WRITE) != SCPE_OK)
            break;
        ba += 2;
        bc -= 2;
        }
    return bc;
}

/* Memory */

if (bc == 0)
    return 0;

ba &= ~1;
if (bc & 1)
    ABORT (STOP_IOALIGN);

cp = ~ba;
seg = (4 - (ba & 3)) & 3;

if (seg) {                                      /* Unaligned head */
    assert ((ba & 2) && (seg == 2));
    if (seg > bc)
        seg = bc;
    cp = UBMPAGE (ba);                          /* Only one word, can't cross page */
    pa10 = Map_Addr10 (ba, 1, &ubm);            /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;           /* UBA timeout */
        return bc;                              /* return bc */
        }
    M[pa10] = (M[pa10] & M_WORD1) | ((d10) (M_WORD18 & *buf++)); /* V_WORD1 */
    pa10++;

    if ((bc -= seg) == 0)
        return 0;
    ba += seg;
    } /* Head */

ea = ba + bc;
seg = bc - (ea & 3);

if (seg > 0) {
    assert (((seg & 3) == 0) && (bc >= seg));
    bc -= seg;
    for ( ; seg; seg -= 4, ba += 4) {           /* aligned longwords */
        np = UBMPAGE (ba);
        if (np != cp) {                         /* New (or first) page? */
            pa10 = Map_Addr10 (ba, 1, &ubm);    /* map addr */
            if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {    /* inv map or NXM? */
                ubcs[1] = ubcs[1] | UBCS_TMO;   /* UBA timeout */
                return (bc + seg);              /* return bc */
                }
            cp = np;
            }
        M[pa10++] = (((d10)(M_WORD18 & buf[0])) << V_WORD0) | (M_WORD18 & buf[1]);/* V_WORD1 */
        buf += 2;
        }
    } /* Body */

/* Tail: partial word, must be aligned */
assert ((bc >= 0) && ((ba & 3) == 0));
if (bc) {
    assert (bc == 2);
    np = UBMPAGE (ba);                          /* Only one word, last possible page crossing */
    if (np != cp) {                             /* New (or first) page? */
        pa10 = Map_Addr10 (ba, 1, &ubm);        /* map addr */
        if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) { /* inv map or NXM? */
            ubcs[1] = ubcs[1] | UBCS_TMO;       /* UBA timeout */
            return (bc);                        /* return bc */
            }
        }
    if (ubm & UMAP_RRV )                        /* Read reverse preserves RH */
        M[pa10] = (M[pa10] & M_WORD0) | (((d10)(M_WORD18 & buf[0])) << V_WORD0);
    else
        M[pa10] = ((d10)(M_WORD18 & buf[0])) << V_WORD0;
}

return 0;
}


/* Evaluate Unibus priority interrupts */

int32 pi_ub_eval ()
{
int32 i, lvl;

for (i = lvl = 0; i < UBANUM; i++) {
    if (int_req & ubabr76[i])
        lvl = lvl | pi_l2bit[UBCS_GET_HI (ubcs[i])];
    if (int_req & ubabr54[i])
        lvl = lvl | pi_l2bit[UBCS_GET_LO (ubcs[i])];
    }
return lvl;
}

/* Return Unibus device vector

   Takes as input the request level calculated by pi_eval
   If there is an interrupting Unibus device at that level, return its vector,
        otherwise, returns 0
*/

int32 pi_ub_vec (int32 rlvl, int32 *uba)
{
int32 i, masked_irq;

for (i = masked_irq = 0; i < UBANUM; i++) {
    if ((rlvl == UBCS_GET_HI (ubcs[i])) &&              /* req on hi level? */
        (masked_irq = int_req & ubabr76[i]))
        break;
    if ((rlvl == UBCS_GET_LO (ubcs[i])) &&              /* req on lo level? */
        (masked_irq = int_req & ubabr54[i]))
        break;
    }
*uba = (i << 1) + 1;                                    /* store uba # */
for (i = 0; (i < 32) && masked_irq; i++) {              /* find hi pri req */
    if ((masked_irq >> i) & 1) {
        int_req = int_req & ~(1u << i);                 /* clear req */
        if (int_ack[i])
            return int_ack[i]();
        return int_vec[i];                              /* return vector */
        }
    }
return 0;
}

/* Unibus adapter map routines */

t_stat ubmap_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
*val = ubmap[n][pa & UMAP_AMASK];
return SCPE_OK;
}

t_stat ubmap_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
ubmap[n][pa & UMAP_AMASK] = UMAP_POSFL (val) | UMAP_POSPN (val);
return SCPE_OK;
}

/* Unibus adapter control/status routines */

t_stat ubs_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
if (int_req & ubabr76[n])
    ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n])
    ubcs[n] = ubcs[n] | UBCS_LO;
*val = ubcs[n] = ubcs[n] & ~UBCS_RDZ;
return SCPE_OK;
}

t_stat ubs_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
if (val & UBCS_INI) {
    reset_all (5);                                      /* start after UBA */
    ubcs[n] = val & UBCS_DXF;
    }
else ubcs[n] = val & UBCS_RDW;
if (int_req & ubabr76[n])
    ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n])
    ubcs[n] = ubcs[n] | UBCS_LO;
return SCPE_OK;
}

/* Unibus adapter read zero/write ignore routines */

t_stat rd_zro (int32 *val, int32 pa, int32 mode)
{
*val = 0;
return SCPE_OK;
}

t_stat wr_nop (int32 val, int32 pa, int32 mode)
{
return SCPE_OK;
}

/* Simulator interface routines */

t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE)
    return SCPE_NXM;
*vptr = ubmap[uba][addr];
return SCPE_OK;
}

t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE)
    return SCPE_NXM;
ubmap[uba][addr] = (int32) val & UMAP_MASK;
return SCPE_OK;
}

t_stat uba_reset (DEVICE *dptr)
{
int32 i, uba;

int_req = 0;
for (uba = 0; uba < UBANUM; uba++) {
    ubcs[uba] = 0;
    for (i = 0; i < UMAP_MEMSIZE; i++)
        ubmap[uba][i] = 0;
    }
pi_eval ();
return SCPE_OK;
}

/* Change device address */

t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newba;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if ((val == 0) || (uptr == NULL))
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newba = (uint32) get_uint (cptr, 8, PAMASK, &r);        /* get new */
if ((r != SCPE_OK) || (newba == dibp->ba))
    return r;
if (GET_IOUBA (newba) != GET_IOUBA (dibp->ba))
    return SCPE_ARG;
if (newba % ((uint32) val))                             /* check modulus */
    return SCPE_ARG;
dibp->ba = newba;                                       /* store */
autcon_enb = 0;                                         /* autoconfig off */
return SCPE_OK;
}

/* Show device address */

t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
if (((dibp->ba>>IO_V_UBA) != 1) &&
    ((dibp->ba>>IO_V_UBA) != 3))
    return SCPE_IERR;
fprintf (st, "address=%07o", dibp->ba);
if (dibp->lnt > 1)
    fprintf (st, "-%07o", dibp->ba + dibp->lnt - 1);
if ((dibp->ba & ((1 << IO_V_UBA) - 1)) < AUTO_CSRBASE + AUTO_CSRMAX)
    fprintf (st, "*");
return SCPE_OK;
}

/* Change device vector */

t_stat set_vec (UNIT *uptr, int32 arg, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newvec;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newvec = (uint32) get_uint (cptr, 8, VEC_Q + 01000, &r);
if ((r != SCPE_OK) || (newvec == VEC_Q) ||
    ((newvec + (dibp->vnum * 4)) >= (VEC_Q + 01000)) ||
    (newvec & ((dibp->vnum > 1)? 07: 03)))
    return SCPE_ARG;
dibp->vec = newvec;
autcon_enb = 0;                                         /* autoconfig off */
return SCPE_OK;
}

/* Show device vector */

t_stat show_vec (FILE *st, UNIT *uptr, int32 arg, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 vec, numvec;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
vec = dibp->vec;
if (arg)
    numvec = arg;
else numvec = dibp->vnum;
if (vec == 0)
    fprintf (st, "no vector");
else {
    fprintf (st, "vector=%o", vec);
    if (numvec > 1)
        fprintf (st, "-%o", vec + (4 * (numvec - 1)));
    }
if (vec >= AUTO_VECBASE)
    fprintf (st, "*");
return SCPE_OK;
}

/* Show vector for terminal multiplexor */

t_stat show_vec_mux (FILE *st, UNIT *uptr, int32 arg, void *desc)
{
TMXR *mp = (TMXR *) desc;

if ((mp == NULL) || (arg == 0))
    return SCPE_IERR;
return show_vec (st, uptr, ((mp->lines * 2) / arg), desc);
}

/* Test for conflict in device addresses */

t_bool dev_conflict (DIB *curr)
{
uint32 i, end;
DEVICE *dptr;
DIB *dibp;

end = curr->ba + curr->lnt - 1;                         /* get end */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((dibp == NULL) || (dibp == curr) ||
        (dptr->flags & DEV_DIS))
        continue;
    if (((curr->ba >= dibp->ba) &&                      /* overlap start? */
        (curr->ba < (dibp->ba + dibp->lnt))) ||
        ((end >= dibp->ba) &&                           /* overlap end? */
        (end < (dibp->ba + dibp->lnt)))) {
        printf ("Device %s address conflict at %08o\n",
            sim_dname (dptr), dibp->ba);
        if (sim_log)
            fprintf (sim_log, "Device %s address conflict at %08o\n",
                     sim_dname (dptr), dibp->ba);
        return TRUE;
        }
    }
return FALSE;
}

/* Build interrupt tables */

void build_int_vec (int32 vloc, int32 ivec, int32 (*iack)(void) )
{
if (iack != NULL)
    int_ack[vloc] = iack;
else int_vec[vloc] = ivec;
return;
}

/* Build dib_tab from device list */

t_bool build_dib_tab (void)
{
int32 i, j, k;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < 32; i++) {                              /* clear intr tables */
    int_vec[i] = 0;
    int_ack[i] = NULL;
    }
for (i = j = 0; (dptr = sim_devices[i]) != NULL; i++) { /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if (dibp->vnum > VEC_DEVMAX)
            return SCPE_IERR;
        for (k = 0; k < dibp->vnum; k++)                /* loop thru vec */
            build_int_vec (dibp->vloc + k,              /* add vector */
                dibp->vec + (k * 4), dibp->ack[k]);
        if (dibp->lnt != 0) {                           /* I/O addresses? */
            dib_tab[j++] = dibp;                        /* add DIB to dib_tab */
            if (j >= DIB_MAX)                           /* too many? */
                return SCPE_IERR;
            }   
        }                                               /* end if enabled */
    }                                                   /* end for */
for (i = 0; (dibp = std_dib[i]) != NULL; i++) {         /* loop thru std */
    dib_tab[j++] = dibp;                                /* add to dib_tab */
    if (j >= DIB_MAX)                                   /* too many? */
        return SCPE_IERR;
    }
dib_tab[j] = NULL;                                      /* end with NULL */
for (i = 0; (dibp = dib_tab[i]) != NULL; i++) {         /* test built dib_tab */
    if (dev_conflict (dibp))                            /* for conflicts */
        return SCPE_STOP;
    }
return SCPE_OK;
}

/* Show dib_tab */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, j, done = 0;
DEVICE *dptr;
DIB *dibt;

build_dib_tab ();                                       /* build table */
while (done == 0) {                                     /* sort ascending */
    done = 1;                                           /* assume done */
    for (i = 0; dib_tab[i + 1] != NULL; i++) {          /* check table */
        if (dib_tab[i]->ba > dib_tab[i + 1]->ba) {      /* out of order? */
            dibt = dib_tab[i];                          /* interchange */
            dib_tab[i] = dib_tab[i + 1];
            dib_tab[i + 1] = dibt;
            done = 0;                                   /* not done */
            }
        }
    }                                                   /* end while */
fprintf (st, "     Address       Vector  BR  # Device\n"
             "----------------- -------- -- -- ------\n");
for (i = 0; dib_tab[i] != NULL; i++) {                  /* print table */
    for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
        if (((DIB*) sim_devices[j]->ctxt) == dib_tab[i]) {
            dptr = sim_devices[j];
            break;
            }
        }
    fprintf (st, "%07o - %07o ", dib_tab[i]->ba,
            dib_tab[i]->ba + dib_tab[i]->lnt - 1);
    if (dib_tab[i]->vec == 0)
        fprintf (st, "        ");
    else {
        fprintf (st, "%03o", dib_tab[i]->vec);
        if (dib_tab[i]->vnum > 1)
            fprintf (st, "-%03o", dib_tab[i]->vec + (4 * (dib_tab[i]->vnum - 1)));
        else
            fprintf (st, "    ");
        fprintf (st, "%1s", (dib_tab[i]->vnum >= AUTO_VECBASE)? "*": " ");
        }
    if (dib_tab[i]->vec || dib_tab[i]->vloc)
        fprintf (st, " %2u", (dib_tab[i]->vloc<=3)? 7:
                            (dib_tab[i]->vloc<=7)? 6:
                            (dib_tab[i]->vloc<=19)? 5: 4);
    else
        fprintf (st, "   ");
    fprintf (st, " %2u %s\n", (dib_tab[i]->ulnt? dib_tab[i]->lnt/dib_tab[i]->ulnt:
                               (dptr? dptr->numunits: 1)), dptr? sim_dname (dptr): "CPU");
    }
return SCPE_OK;
}

/* Autoconfiguration

   The below table describes the fixed addresses for the currently 
   supported Unibus devices which are shared between the PDP11/VAX 
   Unibus and the PDP10.  This list isn't likely to change, but if
   need be, it can be extended to include as many devices as necessary.
   The full 'real' auto configuration table which describes both 
   devices with static addresses and addresses(and vectors) in floating
   address space is #ifdef'd out below.  These addresses have been 
   used historically in the PDP10 simulator so their fixed addresses 
   are retained for consistency with OS configurations which
   expect them to be using these fixed address and vectors.

   A minus number of vectors indicates a field that should be calculated
   but not placed in the DIB (RQ, TQ dynamic vectors)

   An amod value of 0 indicates that all addresses are FIXED
   An vmod value of 0 indicates that all vectors are FIXED */


typedef struct {
    char        *dnam[AUTO_MAXC];
    int32       numc;
    int32       numv;
    uint32      amod;
    uint32      vmod;
    uint32      fixa[AUTO_MAXC];
    uint32      fixv[AUTO_MAXC];
    } AUTO_CON;

AUTO_CON auto_tab[] = {/*c  #v  am vm  fxa   fxv */
#ifdef VM_PDP10
    { { "DZ" },          1,  2,  0, 0,
        {0000010}, {0340} },                             /* DZ11 - Fixed addresses and vectors in simulator */
    { { "RY" },          1,  1,  8, 4, 
        {0017170}, {0264} },                             /* RX11/RX211 - Fixed address and vector in simulator */
    { { "CR" },          1,  1,  0, 0, 
        {0017160}, {0230} },                             /* CR11 - fx CSR, fx VEC */
    { { "PTR" },         1,  1,  0, 0, 
        {0017550}, {0070} },                             /* PC11 reader - fx CSR, fx VEC */
    { { "PTP" },         1,  1,  0, 0, 
        {0017554}, {0074} },                             /* PC11 punch - fx CSR, fx VEC */
    { { "DUP" },         1,  2,  0, 0, 
        {0000300}, {0570} },                             /* DUP11 bit sync - fx CSR, fx VEC */
    { { "KDP" },         1,  2,  0, 0, 
        {0000540}, {0540} },                             /* KMC11-A comm IOP-DUP ucode - fx CSR, fx VEC */
    { { "DMR" },         1,  2,  0, 0, 
        {0000700}, {0440} },                             /* DMR11 comm - fx CSR, fx VEC */
#else
    { { "QBA" },         1,  0,  0, 0, 
        {017500} },                                     /* doorbell - fx CSR, no VEC */
    { { "MCTL" },        1,  0,  0, 0, 
        {012100} },                                     /* MSV11-P - fx CSR, no VEC */
    { { "KE" },          1,  0,  0, 0, 
        {017300} },                                     /* KE11-A - fx CSR, no VEC */
    { { "KG" },          1,  0,  0, 0, 
        {010700} },                                     /* KG11-A - fx CSR, no VEC */
    { { "RHA", "RHB" },  1,  1,  0, 0, 
        {016700, 012440}, {0254, 0224} },               /* RH11/RH70 - fx CSR, fx VEC */
    { { "CLK" },         1,  1,  0, 0, 
        {017546}, {0100} },                             /* KW11L - fx CSR, fx VEC */
    { { "PCLK" },        1,  1,  0, 0, 
        {012540}, {0104} },                             /* KW11P - fx CSR, fx VEC */
    { { "PTR" },         1,  1,  0, 0, 
        {017550}, {0070} },                             /* PC11 reader - fx CSR, fx VEC */
    { { "PTP" },         1,  1,  0, 0, 
        {017554}, {0074} },                             /* PC11 punch - fx CSR, fx VEC */
    { { "RK" },          1,  1,  0, 0, 
        {017400}, {0220} },                             /* RK11 - fx CSR, fx VEC */
    { { "TM" },          1,  1,  0, 0, 
        {012520}, {0224} },                             /* TM11 - fx CSR, fx VEC */
    { { "RC" },          1,  1,  0, 0, 
        {017440}, {0210} },                             /* RC11 - fx CSR, fx VEC */
    { { "RF" },          1,  1,  0, 0, 
        {017460}, {0204} },                             /* RF11 - fx CSR, fx VEC */
    { { "CR" },          1,  1,  0, 0, 
        {017160}, {0230} },                             /* CR11 - fx CSR, fx VEC */
    { { "HK" },          1,  1,  0, 0, 
        {017440}, {0210} },                             /* RK611 - fx CSR, fx VEC */
    { { "LPT" },         1,  1,  0, 0, 
        {017514, 004004, 004014, 004024, 004034}, 
        {0200,     0170,   0174,   0270,   0274} },     /* LP11 - fx CSR, fx VEC */
    { { "RB" },          1,  1,  0, 0, 
        {015606}, {0250} },                             /* RB730 - fx CSR, fx VEC */
    { { "RL" },          1,  1,  0, 0, 
        {014400}, {0160} },                             /* RL11 - fx CSR, fx VEC */
    { { "RL" },          1,  1,  0, 0, 
        {014400}, {0160} },                             /* RL11 - fx CSR, fx VEC */
    { { "DCI" },         1,  2,  0, 8, 
        {014000, 014010, 014020, 014030, 
         014040, 014050, 014060, 014070, 
         014100, 014110, 014120, 014130, 
         014140, 014150, 014160, 014170, 
         014200, 014210, 014220, 014230, 
         014240, 014250, 014260, 014270,
         014300, 014310, 014320, 014330, 
         014340, 014350, 014360, 014370} },             /* DC11 - fx CSRs */
    { { NULL },          1,  2,  0, 8, 
        {016500, 016510, 016520, 016530, 
         016540, 016550, 016560, 016570,
         016600, 016610, 016620, 016630,
         016640, 016650, 016660, 016670} },             /* TU58 - fx CSRs */
    { { NULL },          1,  1,  0, 4, 
        {015200, 015210, 015220, 015230, 
         015240, 015250, 015260, 015270,
         015300, 015310, 015320, 015330,
         015340, 015350, 015360, 015370} },             /* DN11 - fx CSRs */
    { { NULL },          1,  1,  0, 4, 
        {010500, 010510, 010520, 010530, 
         010540, 010550, 010560, 010570, 
         010600, 010610, 010620, 010630, 
         010640, 010650, 010660, 010670} },             /* DM11B - fx CSRs */
    { { NULL },          1,  2,  0, 8, 
        {007600, 007570, 007560, 007550, 
         007540, 007530, 007520, 007510,
         007500, 007470, 007460, 007450,
         007440, 007430, 007420, 007410} },             /* DR11C - fx CSRs */
    { { NULL },          1,  1,  0, 8, 
        {012600, 012604, 012610, 012614, 
         012620, 012624, 012620, 012624} },             /* PR611 - fx CSRs */
    { { NULL },          1,  1,  0, 8, 
        {017420, 017422, 017424, 017426, 
         017430, 017432, 017434, 017436} },             /* DT11 - fx CSRs */
    { { NULL },          1,  2,  0, 8,
      {016200, 016240} },                               /* DX11 */
    { { "DLI" },         1,  2,  0, 8, 
        {016500, 016510, 016520, 016530,
         016540, 016550, 016560, 016570,
         016600, 016610, 016620, 016630,
         016740, 016750, 016760, 016770} },             /* KL11/DL11/DLV11 - fx CSRs */
    { { NULL },          1,  2,  0, 8, { 0 } },         /* DLV11J - fx CSRs */
    { { NULL },          1,  2,  8, 8 },                /* DJ11 */
    { { NULL },          1,  2, 16, 8 },                /* DH11 */
    { { NULL },          1,  4,  0, 8,
      {012000, 012010, 012020, 012030} },               /* GT40 */
    { { NULL },          1,  2,  0, 8,
      {010400} },                                       /* LPS11 */
    { { NULL },          1,  2,  8, 8 },                /* DQ11 */
    { { NULL },          1,  2,  0, 8,
      {012400} },                                       /* KW11W */
    { { NULL },          1,  2,  8, 8 },                /* DU11 */
    { { NULL },          1,  2,  8, 8 },                /* DUP11 */
    { { NULL },          1,  3,  0, 8,
      {015000, 015040, 015100, 015140, }},              /* DV11 */
    { { NULL },          1,  2,  8, 8 },                /* LK11A */
    { { "DMC0", "DMC1", "DMC2", "DMC3" }, 
                         1,  2,  8, 8 },                /* DMC11 */
    { { "DZ" },          1,  2,  8, 8 },                /* DZ11 */
    { { NULL },          1,  2,  8, 8 },                /* KMC11 */
    { { NULL },          1,  2,  8, 8 },                /* LPP11 */
    { { NULL },          1,  2,  8, 8 },                /* VMV21 */
    { { NULL },          1,  2, 16, 8 },                /* VMV31 */
    { { NULL },          1,  2,  8, 8 },                /* DWR70 */
    { { "RL", "RLB"},    1,  1,  8, 4, 
        {014400}, {0160} },                             /* RL11 */
    { { "TS", "TSB", "TSC", "TSD"}, 
                         1,  1,  0, 4,                  /* TS11 */
        {012520, 012524, 012530, 012534},
        {0224} },
    { { NULL },          1,  2, 16, 8,
        {010460} },                                     /* LPA11K */
    { { NULL },          1,  2,  8, 8 },                /* KW11C */
    { { NULL },          1,  1,  8, 8 },                /* reserved */
    { { "RX", "RY" },    1,  1,  8, 4, 
        {017170} , {0264} },                            /* RX11/RX211 */
    { { NULL },          1,  1,  8, 4 },                /* DR11W */
    { { NULL },          1,  1,  8, 4, 
        {012410, 012410}, {0124} },                     /* DR11B - fx CSRs,vec */
    { { "DMP" },         1,  2,  8, 8 },                /* DMP11 */
    { { NULL },          1,  2,  8, 8 },                /* DPV11 */
    { { NULL },          1,  2,  8, 8 },                /* ISB11 */
    { { NULL },          1,  2, 16, 8 },                /* DMV11 */
    { { "XU", "XUB" },   1,  1,  8, 4, 
        {014510}, {0120} },                             /* DEUNA */
    { { "XQ", "XQB" },   1, -1,  0, 4,
        {014440, 014460, 014520, 014540}, {0120} },     /* DEQNA */
    { { "RQ", "RQB", "RQC", "RQD" }, 
                         1, -1,  4, 4,                  /* RQDX3 */
        {012150}, {0154} },
    { { NULL },          1,  8, 32, 4 },                /* DMF32 */
    { { NULL },          1,  3, 16, 8 },                /* KMS11 */
    { { NULL },          1,  2,  0, 8,
        {004200, 004240, 004300, 004340} },             /* PLC11 */
    { { NULL },          1,  1, 16, 4 },                /* VS100 */
    { { "TQ", "TQB" },   1, -1,  4, 4, 
        {014500}, {0260} },                             /* TQK50 */
    { { NULL },          1,  2, 16, 8 },                /* KMV11 */
    { { NULL },          1,  2,  0, 8,
        {004400, 004440, 004500, 004540} },             /* KTC32 */
    { { NULL },          1,  2,  0, 8,
        {004100} },                                     /* IEQ11 */
    { { "VH" },          1,  2, 16, 8 },                /* DHU11/DHQ11 */
    { { NULL },          1,  6, 32, 4 },                /* DMZ32 */
    { { NULL },          1,  6, 32, 4 },                /* CP132 */
    { { "TC" },          1,  1,  0, 0,
        {017340}, {0214} },                             /* TC11 */
    { { "TA" },          1,  1,  0, 0,
        {017500}, {0260} },                             /* TA11 */
    { { NULL },          1,  2, 64, 8, 
        {017200} },                                     /* QVSS - fx CSR */
    { { NULL },          1,  1,  8, 4 },                /* VS31 */
    { { NULL },          1,  1,  0, 4,
        {016200} },                                     /* LNV11 - fx CSR */
    { { NULL },          1,  1, 16, 4 },                /* LNV21/QPSS */
    { { NULL },          1,  1,  8, 4, 
        {012570} },                                     /* QTA - fx CSR */
    { { NULL },          1,  1,  8, 4 },                /* DSV11 */
    { { NULL },          1,  2,  8, 8 },                /* CSAM */
    { { NULL },          1,  2,  8, 8 },                /* ADV11C */
    { { NULL },          1,  0,  8, 8, 
        {010440} },                                     /* AAV11/AAV11C */
    { { NULL },          1,  2,  8, 8, 
        {016400}, {0140} },                             /* AXV11C - fx CSR,vec */
    { { NULL },          1,  2,  4, 8, 
        {010420} },                                     /* KWV11C - fx CSR */
    { { NULL },          1,  2,  8, 8, 
        {016410} },                                     /* ADV11D - fx CSR */
    { { NULL },          1,  2,  8, 8, 
        {016420} },                                     /* AAV11D - fx CSR */
    { { "QDSS" },        1,  3,  0, 16,
        {017400, 017402, 017404, 017406, 
         017410, 017412, 017414, 017416} },             /* VCB02 - QDSS - fx CSR */
    { { NULL },          1, 16,  0, 4, 
        {004160, 004140, 004120} },                     /* DRV11J - fx CSR */
    { { NULL },          1,  2, 16, 8 },                /* DRQ3B */
    { { NULL },          1,  1,  8, 4 },                /* VSV24 */
    { { NULL },          1,  1,  8, 4 },                /* VSV21 */
    { { NULL },          1,  1,  8, 4 },                /* IBQ01 */
    { { NULL },          1,  1,  8, 8 },                /* IDV11A */
    { { NULL },          1,  0,  8, 8 },                /* IDV11B */
    { { NULL },          1,  0,  8, 8 },                /* IDV11C */
    { { NULL },          1,  1,  8, 8 },                /* IDV11D */
    { { NULL },          1,  2,  8, 8 },                /* IAV11A */
    { { NULL },          1,  0,  8, 8 },                /* IAV11B */
    { { NULL },          1,  2,  8, 8 },                /* MIRA */
    { { NULL },          1,  2, 16, 8 },                /* IEQ11 */
    { { NULL },          1,  2, 32, 8 },                /* ADQ32 */
    { { NULL },          1,  2,  8, 8 },                /* DTC04, DECvoice */
    { { NULL },          1,  1, 32, 4 },                /* DESNA */
    { { NULL },          1,  2,  4, 8 },                /* IGQ11 */
    { { NULL },          1,  2, 32, 8 },                /* KMV1F */
    { { NULL },          1,  1,  8, 4 },                /* DIV32 */
    { { NULL },          1,  2,  4, 8 },                /* DTCN5, DECvoice */
    { { NULL },          1,  2,  4, 8 },                /* DTC05, DECvoice */
    { { NULL },          1,  2,  8, 8 },                /* KWV32 (DSV11) */
    { { NULL },          1,  1, 64, 4 },                /* QZA */
#endif
    { { NULL }, -1 }                                    /* end table */
};

#if !defined(DEV_NEXUS) 
#if defined(DEV_MBUS)
#define DEV_NEXUS DEV_MBUS
#else
#define DEV_NEXUS 0
#endif
#endif
t_stat auto_config (char *name, int32 nctrl)
{
uint32 csr = IOPAGEBASE + AUTO_CSRBASE;
uint32 vec = VEC_Q + AUTO_VECBASE;
AUTO_CON *autp;
DEVICE *dptr;
DIB *dibp;
uint32 j, vmask, amask;

if (autcon_enb == 0)                                    /* enabled? */
    return SCPE_OK;
if (name) {                                             /* updating? */
    if (nctrl < 0)
        return SCPE_ARG;
    for (autp = auto_tab; autp->numc >= 0; autp++) {
        for (j = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
            if (strcmp (name, autp->dnam[j]) == 0)
                autp->numc = nctrl;
            }
        }
    }
for (autp = auto_tab; autp->numc >= 0; autp++) {        /* loop thru table */
    if (autp->amod) {                                   /* floating csr? */
        amask = autp->amod - 1;
        csr = (csr + amask) & ~amask;                   /* align csr */
        }
    for (j = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
        if (autp->dnam[j] == NULL)                      /* no device? */
            break;
        dptr = find_dev (autp->dnam[j]);                /* find ctrl */
        if ((dptr == NULL) ||                           /* enabled, not nexus? */
            (dptr->flags & DEV_DIS) ||
            (dptr->flags & DEV_NEXUS) )
            continue;
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp == NULL)                               /* not there??? */
            return SCPE_IERR;
        if (autp->fixa[j])                              /* fixed csr avail? */
            dibp->ba = IOPAGEBASE + autp->fixa[j];      /* use it */
        else {                                          /* no fixed left */
            dibp->ba = csr;                             /* set CSR */
            csr += (autp->numc * autp->amod);           /* next CSR */
            }                                           /* end else */
        if (autp->numv) {                               /* vec needed? */
            if (autp->fixv[j]) {                        /* fixed vec avail? */
                if (autp->numv > 0)
                    dibp->vec = VEC_Q + autp->fixv[j];  /* use it */
                }
            else {                                      /* no fixed left */
                uint32 numv = abs (autp->numv);         /* get num vec */
                vmask = autp->vmod - 1;
                vec = (vec + vmask) & ~vmask;           /* align vector */
                if (autp->numv > 0)
                    dibp->vec = vec;                    /* set vector */
                vec += (autp->numc * numv * 4);
                }                                       /* end else */
            }                                           /* end vec needed */
        }                                               /* end for j */
    if (autp->amod)                                     /* flt CSR? gap */
        csr = csr + 2;
    }                                                   /* end for i */
return SCPE_OK;
}


/* Set address floating */

t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;

if (cptr != NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
return auto_config (NULL, 0);                           /* autoconfigure */
}

