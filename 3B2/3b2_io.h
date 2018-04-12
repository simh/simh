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
 *    - A set of pointers for load and unload from the queue
 *    - Zero or more Queue Entries
 *
 * |    Address    | Size |  Contents                               |
 * +---------------+------+-----------------------------------------+
 * | QUEUE_P       |  12  | Express Queue Entry [1]                 |
 * | QUEUE_P + 12  |  2   | Load Pointer                            |
 * | QUEUE_P + 14  |  2   | Unload Pointer                          |
 * | QUEUE_P + 16  |  12  | Entry 0 [1]                             |
 * | QUEUE_P + 28  |  12  | Entry 1 [1]                             |
 * | ...           |  ... | ...                                     |
 *
 *  [1] See Queue Entry above
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

#define SYSGEN_PTR       PHYS_MEM_BASE
#define CIO_LOAD_SIZE    0x4
#define CIO_ENTRY_SIZE   0x0c
#define CIO_QUE_OFFSET   0x10
#define CIO_SLOTS        12

/* CIO opcodes */
#define CIO_DLM          1
#define CIO_ULM          2
#define CIO_FCF          3
#define CIO_DOS          4
#define CIO_DSD          5

/* Map a physical address to a card ID */
#define CID(pa)         (((((pa) >> 0x14) & 0x1f) / 2) - 1)
/* Map a card ID to a base address */
#define CADDR(bid)      (((((bid) + 1) * 2) << 0x14))

#define CIO_INT0          0x1
#define CIO_INT1          0x2

/* Offsets into the request/completion queues of various values */
#define LOAD_OFFSET       12
#define ULOAD_OFFSET      14
#define QUE_OFFSET        16
#define QUE_E_SIZE        12

#define CIO_SYGEN_MASK    0x3

typedef struct {
    uint16 id;                         /* Card ID                       */
    void   (*exp_handler)(uint8 cid);  /* Handler for express jobs      */
    void   (*full_handler)(uint8 cid); /* Handler for full jobs         */
    void   (*sysgen)(uint8 cid);       /* Sysgen routine (optional)     */
    uint32 rqp;                        /* Request Queue Pointer         */
    uint32 cqp;                        /* Completion Queue Pointer      */
    uint8  rqs;                        /* Request queue size            */
    uint8  cqs;                        /* Completion queue size         */
    uint8  ivec;                       /* Interrupt Vector              */
    uint8  no_rque;                    /* Number of request queues      */
    uint8  ipl;                        /* IPL that this card uses       */
    t_bool intr;                       /* Card needs to interrupt       */
    uint8  cmdbits;                    /* Commands received since RESET */
    uint8  seqbit;                     /* Squence Bit                   */
    uint8  op;                         /* Last received opcode          */
    TMLN   *lines[4];                  /* Terminal Multiplexer lines    */
} CIO_STATE;

typedef struct {
    uint16 byte_count;
    uint8  subdevice;
    uint8  opcode;
    uint32 address;
    uint32 app_data;
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

extern uint16 cio_ints;
extern CIO_STATE cio[CIO_SLOTS];

t_stat cio_reset(DEVICE *dptr);
t_stat cio_svc(UNIT *uptr);

/* Put an entry into the Completion Queue's Express entry */
void cio_cexpress(uint8 cid, cio_entry *cqe);
/* Put an entry into the Completion Queue */
void cio_cqueue(uint8 cid, cio_entry *cqe);
/* Get an entry from the Request Queue */
void cio_rqueue(uint8 cid, cio_entry *cqe);
/* Perform a Sysgen */
void cio_sysgen(uint8 cid);
/* Debugging only */
void dump_entry(CONST char *type, cio_entry *entry);

#endif
