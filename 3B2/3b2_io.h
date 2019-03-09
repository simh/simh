/* 3b2_cpu.h: AT&T 3B2 Model 400 IO dispatch (Header)

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/



/* Reference Documentation
 * =======================
 *
 * All communication between the system board and feature cards is
 * done through in-memory queues, and causing interrupts in the
 * feature card by accessing the Control or ID/VEC memory-mapped IO
 * addresses. The structure of these queues is defined below in
 * tables.
 *
 * Sysgen Block
 * ------------
 *
 * Pointed to by address at 0x2000000 after an INT0/INT1 combo
 *
 *
 * |    Address    | Size |  Contents                               |
 * +---------------+------+-----------------------------------------+
 * | SYSGEN_P      |  4   | Address of request queue                |
 * | SYSGEN_P + 4  |  4   | Address of completion queue             |
 * | SYSGEN_P + 8  |  1   | Number of entries in request queue      |
 * | SYSGEN_P + 9  |  1   | Number of entries in completion queue   |
 * | SYSGEN_P + 10 |  1   | Interrupt Vector number                 |
 * | SYSGEN_P + 11 |  1   | Number of request queues                |
 *
 *
 * Queue Entry
 * -----------
 *
 * Each queue has one Express Entry, and n regular entries.
 *
 * |    Address    | Size |  Contents                               |
 * +---------------+------+-----------------------------------------+
 * | ENTRY_P       |  2   | Byte Count                              |
 * | ENTRY_P + 2   |  1   | Subdevice [1]                           |
 * | ENTRY_P + 3   |  1   | Opcode                                  |
 * | ENTRY_P + 4   |  4   | Address / Data                          |
 * | ENTRY_P + 8   |  4   | Application Specific Data               |
 *
 * [1] The "Subdevice" entry is further divided into a bitset:
 *     Bit 7:   Command (1) / Status (0)
 *     Bit 6:   Sequence Bit
 *     Bit 5-1: Subdevice
 *
 *
 * Queue
 * -----
 *
 * The Queue structures (one for request, one for completion) hold:
 *    - An express entry
 *
 * And then one or more queues, each queue consiting of
 *    - A set of pointers for load and unload from the queue
 *    - One or more Queue Entries
 *
 * |    Address    | Size |  Contents                               |
 * +---------------+------+-----------------------------------------+
 * | QUEUE_P       |  12  | Express Queue Entry [1]                 |
 * +---------------+------+-----------------------------------------+
 * | QUEUE_P + 12  |  2   | Load Pointer for Queue 0                |
 * | QUEUE_P + 14  |  2   | Unload Pointer for Queue 0              |
 * | QUEUE_P + 16  |  12  | Queue 0 Entry 0 [1]                     |
 * | QUEUE_P + 28  |  12  | Queue 0 Entry 1 [1]                     |
 * | ...           |  ... | ...                                     |
 * +---------------+------+-----------------------------------------+
 * | QUEUE_P + n   |  2   | Load Pointer for Queue 1                |
 * | QUEUE_P + n   |  2   | Unload Pointer for Queue 1              |
 * | QUEUE_P + n   |  12  | Queue 1 Entry 0 [1]                     |
 * | QUEUE_P + n   |  12  | Queue 1 Entry 1 [1]                     |
 * | ...           |  ... | ...                                     |
 *
 *  [1] See Queue Entry above
 *
 * NB: There are multiple Request queues, usually one per subdevice,
 * and EACH Request queue starts with a Load Pointer, an Unload
 * Pointer, and then 'n' Queue Entries.
 *
 */

#ifndef _3B2_IO_H_
#define _3B2_IO_H_

#include "3b2_sysdev.h"
#include "3b2_iu.h"
#include "3b2_if.h"
#include "3b2_id.h"
#include "3b2_dmac.h"
#include "3b2_mmu.h"

#include "sim_tmxr.h"

#define IOF_ID          0
#define IOF_VEC         1
#define IOF_CTRL        3
#define IOF_STAT        5

#define SYSGEN_PTR      PHYS_MEM_BASE

/* CIO opcodes */
#define CIO_DLM         1
#define CIO_ULM         2
#define CIO_FCF         3
#define CIO_DOS         4
#define CIO_DSD         5

/* Response */
#define CIO_SUCCESS     0
#define CIO_FAILURE     2
#define CIO_SYSGEN_OK   3

/* Map a physical address to a card ID */
#define CID(pa)         (((((pa) >> 0x14) & 0x1f) / 2) - 1)
/* Map a card ID to a base address */
#define CADDR(bid)      (((((bid) + 1) * 2) << 0x14))

/* Offsets into the request/completion queues of various values */
#define LUSIZE          4    /* Load/Unload pointers size */
#define QESIZE          8    /* Queue entry is 8 bytes + application data */

#define CIO_STAT        0
#define CIO_CMD         1

/* Sysgen State */
#define CIO_INT_NONE    0
#define CIO_INT0        1
#define CIO_INT1        2
#define CIO_SYSGEN      3


typedef struct {
    uint16 id;                           /* Card ID                          */
    void   (*exp_handler)(uint8 cid);    /* Handler for express jobs         */
    void   (*full_handler)(uint8 cid);   /* Handler for full jobs            */
    void   (*sysgen)(uint8 cid);         /* Sysgen routine (optional)        */
    void   (*reset_handler)(uint8 cid);  /* RESET request handler (optional) */
    uint32 rqp;                          /* Request Queue Pointer            */
    uint32 cqp;                          /* Completion Queue Pointer         */
    uint8  rqs;                          /* Request queue size               */
    uint8  cqs;                          /* Completion queue size            */
    uint8  ivec;                         /* Interrupt Vector                 */
    uint8  no_rque;                      /* Number of request queues         */
    uint8  ipl;                          /* IPL that this card uses          */
    t_bool intr;                         /* Card needs to interrupt          */
    uint8  sysgen_s;                     /* Sysgen state                     */
    uint8  seqbit;                       /* Squence Bit                      */
    uint8  op;                           /* Last received opcode             */
} CIO_STATE;

typedef struct {
    uint16 byte_count;
    uint8  subdevice;
    uint8  opcode;
    uint32 address;
} cio_entry;

struct iolink {
    uint32      low;
    uint32      high;
    uint32      (*read)(uint32 pa, size_t size);
    void        (*write)(uint32 pa, uint32 val, size_t size);
};

/* Example pump structure
 * ----------------------
 *
 * Used during initial setup of PORTS card in slot 0:
 *
 *    dev     = 0100
 *    min     = 0000
 *    cmdcode = 0003
 *    options = 0000
 *    bufaddr = 808821A0
 *    ioaddr  = 00000500
 *    size    = 00000650
 *    numbrd  = 00000000
 *    retcode = 00000008   (PU_NULL)
 */

typedef struct {
    uint16 dev;
    uint16 min;
    uint16 cmdcode;
    uint16 options;
    uint32 bufaddr;
    uint32 ioaddr;
    uint32 size;
    uint32 numbrd;
    uint32 retcode;
} pump;

extern t_bool cio_skip_seqbit;
extern uint16 cio_ints;
extern CIO_STATE cio[CIO_SLOTS];

t_stat cio_reset(DEVICE *dptr);
t_stat cio_svc(UNIT *uptr);

void cio_clear(uint8 cid);
uint32 cio_crc32_shift(uint32 crc, uint8 data);
void cio_cexpress(uint8 cid, uint32 esize, cio_entry *cqe, uint8 *app_data);
void cio_cqueue(uint8 cid, uint8 cmd_stat, uint32 esize, cio_entry *cqe, uint8 *app_data);
t_bool cio_cqueue_avail(uint8 cid, uint32 esize);
void cio_rexpress(uint8 cid, uint32 esize, cio_entry *rqe, uint8 *app_data);
t_stat cio_rqueue(uint8 cid, uint32 qnum, uint32 esize, cio_entry *rqe, uint8 *app_data);
t_bool cio_rqueue_avail(uint8 cid, uint32 qnum, uint32 esize);
uint16 cio_r_lp(uint8 cid, uint32 qnum, uint32 esize);
uint16 cio_r_ulp(uint8 cid, uint32 qnum, uint32 esize);
uint16 cio_c_lp(uint8 cid, uint32 esize);
uint16 cio_c_ulp(uint8 cid, uint32 esize);
void cio_sysgen(uint8 cid);

void dump_entry(uint32 dbits, DEVICE *dev, CONST char *type,
                uint32 esize, cio_entry *entry, uint8 *app_data);

#endif
