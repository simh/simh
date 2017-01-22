/* hp3000_mem.h: HP 3000 memory subsystem interface declarations

   Copyright (c) 2016, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   10-Oct-16    JDB     Created


   This file contains declarations used by the CPU, I/O Processor, Multiplexer
   Channel, and Selector Channel to interface with the HP 3000 memory subsystem.
*/



/* Debug flags.


   Implementation notes:

    1. Memory debug flags are allocated in descending order, as they may be used
       by other modules (e.g., CPU) that allocate their own flags in ascending
       order.  No check is made for overlapping values.
*/

#define DEB_MDATA           (1u << 31)          /* trace memory reads and writes */
#define DEB_MFETCH          (1u << 30)          /* trace memory instruction fetches */
#define DEB_MOPND           (1u << 29)          /* trace memory operand accesses */


/* Architectural constants.

   The type used to represent a main memory word value is defined.  An array of
   this type is used to simulate the CPU main memory.


   Implementation notes:

    1. The MEMORY_WORD type is a 16-bit unsigned type, corresponding with the
       16-bit main memory in the HP 3000.  Unlike the general data type, which
       is a 32-bit type for speed, main memory does not benefit from the faster
       32-bit execution on IA-32 processors, as only one instruction in the
       cpu_read_memory and cpu_write_memory routines has an operand override
       that invokes the slower instruction fetch path.  There is a negligible
       difference in the Memory Pattern Test diagnostic execution speeds for the
       uint32 vs. uint16 definition, whereas the VM requirements are doubled for
       the former.
*/

typedef uint16              MEMORY_WORD;        /* HP 16-bit memory word representation */


/* Byte accessors.

   The HP 3000 is a word-addressable machine.  Byte addressing is implemented by
   assuming that a memory of N physical words may be addressed as 2N bytes.  The
   "byte-capable" machine instructions use "relative byte addresses" that are
   used to obtain absolute word addresses by dividing by two and then accessing
   the upper or lower byte of the resulting word, depending on the LSB of the
   byte address.

   In simulation, this module provides a byte access structure and a set of
   routines that read or write the next byte in ascending byte-offset order.
   The structure is initialized with the starting byte offset from a specified
   base register value and then is passed as a parameter to the other routines,
   which update the fields accordingly for the access requested.  This relieves
   the caller from having to manage the continual logical-to-physical address
   translation, word buffering, byte selection, etc.

   Byte accessors are also used to provide debug traces of byte operands in
   memory.  Initializing an accessor sets a field containing the absolute byte
   memory address; this address may be passed to the byte formatters to print
   the operand.

   In most cases, operands are defined by starting byte addresses and byte
   counts.  However, some operands (e.g., EDIT instruction operands) are
   delineated only by the extents of the accesses.  For these operands, byte
   accessors maintain the lowest byte addresses and offsets actually accessed,
   as well as the lengths of the extent of the accesses.
*/

typedef struct {                                        /* byte access descriptor */
    HP_WORD       *byte_offset;                         /*   relative byte offset of the next byte */
    HP_WORD       data_word;                            /*   memory data word containing the current byte */
    ACCESS_CLASS  class;                                /*   memory access classification */
    uint32        word_address;                         /*   logical word address containing the next byte */
    t_bool        write_needed;                         /*   TRUE if the data word must be written to memory */
    uint32        count;                                /*   current count of bytes accessed */
    uint32        length;                               /*   (trace) length of extent of access */
    uint32        initial_byte_address;                 /*   (trace) initial absolute byte address */
    uint32        initial_byte_offset;                  /*   (trace) initial relative byte offset */
    uint32        first_byte_address;                   /*   (trace) lowest absolute byte address accessed */
    uint32        first_byte_offset;                    /*   (trace) lowest relative byte offset accessed */
    } BYTE_ACCESS;


/* Memory global SCP support routines */

t_stat mem_examine (t_value *eval_array, t_addr address, UNIT *uptr, int32 switches);
t_stat mem_deposit (t_value value,       t_addr address, UNIT *uptr, int32 switches);


/* Global memory functions.

   mem_initialize   : allocate main memory
   mem_is_empty     : check for a non-zero value within a range of memory locations
   mem_fill         : set all memory locations to a specified value

   mem_read         : read a word from main memory
   mem_write        : write a word to main memory

   mem_init_byte    : initialize a memory byte access structure
   mem_set_byte     : set the access structure to a new byte offset
   mem_lookup_byte  : return a byte at a specified index in a table
   mem_read_byte    : read the next byte from memory
   mem_write_byte   : write the next byte to memory
   mem_modify_byte  : replace the last byte written to memory
   mem_post_byte    : post the word containing the last byte modified in place to memory
   mem_update_byte  : rewrite the word containing the last byte written to memory

   fmt_byte_operand : format a byte operand in memory into a character string
   fmt_bcd_operand  : format a BCD operand in memory into a character string
*/

extern t_bool mem_initialize (uint32 memory_size);
extern t_bool mem_is_empty   (uint32 starting_address);
extern void   mem_fill       (uint32 starting_address, HP_WORD fill_value);

extern t_bool mem_read  (DEVICE *dptr, ACCESS_CLASS classification, uint32 offset, HP_WORD *value);
extern t_bool mem_write (DEVICE *dptr, ACCESS_CLASS classification, uint32 offset, HP_WORD  value);

extern void   mem_init_byte   (BYTE_ACCESS *bap, ACCESS_CLASS class, HP_WORD *byte_offset, uint32 block_length);
extern void   mem_set_byte    (BYTE_ACCESS *bap);
extern uint8  mem_lookup_byte (BYTE_ACCESS *bap, uint8 index);
extern uint8  mem_read_byte   (BYTE_ACCESS *bap);
extern void   mem_write_byte  (BYTE_ACCESS *bap, uint8 byte);
extern void   mem_modify_byte (BYTE_ACCESS *bap, uint8 byte);
extern void   mem_post_byte   (BYTE_ACCESS *bap);
extern void   mem_update_byte (BYTE_ACCESS *bap);

extern char   *fmt_byte_operand            (uint32 byte_address, uint32 byte_count);
extern char   *fmt_translated_byte_operand (uint32 byte_address, uint32 byte_count, uint32 table_address);
extern char   *fmt_bcd_operand             (uint32 byte_address, uint32 digit_count);
