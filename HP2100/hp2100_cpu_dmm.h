/* hp2100_cpu_dmm.h: HP 2100 CPU-to-DMA/MEM/MP interface declarations

   Copyright (c) 2018, J. David Bryan

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

   26-Jul-18    JDB     Created


   This file contains declarations used by the CPU to interface with the Direct
   Memory Access/Dual-Channel Port Controller, Memory Expansion Module, and
   Memory Protect accessories.
*/



#include "hp2100_io.h"                          /* include the I/O definitions for the I/O dispatcher */



/* I/O dispatcher return value */

typedef struct {                                /* the I/O dispatcher return structure */
    t_bool   skip;                              /*   TRUE if the interface asserted the SKF signal */
    HP_WORD  data;                              /*   the data value returned from the interface */
    } SKPF_DATA;


/* I/O Group control operations */

typedef enum {                                  /* derived from IR bits 11-6 */
    iog_HLT,                                    /*   xx0000 HLT */
    iog_STF,                                    /*   xx0001 STF */
    iog_SFC,                                    /*   xx0010 SFC */
    iog_SFS,                                    /*   xx0011 SFS */
    iog_MIx,                                    /*   xx0100 MIA/B */
    iog_LIx,                                    /*   xx0101 LIA/B */
    iog_OTx,                                    /*   xx0110 OTA/B */
    iog_STC,                                    /*   xx0111 STC */

    iog_HLT_C,                                  /*   xx1000 HLT,C */
    iog_CLF,                                    /*   xx1001 CLF */
    iog_SFC_C,                                  /*   xx1010 SFC,C */
    iog_SFS_C,                                  /*   xx1011 SFS,C */
    iog_MIx_C,                                  /*   xx1100 MIA/B,C */
    iog_LIx_C,                                  /*   xx1101 LIA/B,C */
    iog_OTx_C,                                  /*   xx1110 OTA/B,C */
    iog_STC_C,                                  /*   xx1111 STC,C */

    iog_CLC,                                    /*   1x0111 CLC */
    iog_CLC_C                                   /*   1x1111 CLC,C */
    } IO_GROUP_OP;


/* DMA global state */

extern uint32 dma_request_set;


/* DMA global utility routine declarations */

extern void dma_configure  (void);
extern void dma_assert_SRQ (uint32 select_code);
extern void dma_service    (void);


/* MEU Maps.


   Implementation notes:

    1. The System_Map and User_Map enumeration values must be 0 and 1, so that
       switching to the alternate map may be performed by an XOR with 1.
       Additionally, the four map enumeration values must be 0-3 to permit
       selection by the lower two bits of the instruction register for the SYx,
       USx, PAx, and PBx instructions.
*/

#define MEU_MAP_MASK        3
#define MEU_REG_COUNT       (1u << LP_WIDTH)    /* count of mapping registers -- one for each logical page */

typedef enum {                                  /* MEM map selector */
    System_Map = 0,                             /*   system map */
    User_Map   = 1,                             /*   user map */
    Port_A_Map = 2,                             /*   port A map */
    Port_B_Map = 3,                             /*   port B map */
    Current_Map,                                /*   current map (system or user) */
    Linear_Map                                  /*   linear access to all maps */
    } MEU_MAP_SELECTOR;

#define TO_MAP_SELECTOR(i)  ((MEU_MAP_SELECTOR) (i & MEU_MAP_MASK))


/* Memory definitions */

typedef enum {
    From_Memory,                                /* copy from memory to a buffer */
    To_Memory                                   /* copy from a buffer to memory */
    } COPY_DIRECTION;


/* Memory global data */

extern uint32 mem_size;                         /* size of memory in words */
extern uint32 mem_end;                          /* address of the first word beyond memory */


/* Memory Expansion Unit definitions */

typedef enum {                                  /* MEM privileged instruction violation conditions */
    Always,
    If_User_Map
    } MEU_CONDITION;

typedef enum {                                  /* MEM operational state */
    ME_Disabled,
    ME_Enabled
    } MEU_STATE;


/* Memory Expansion Unit global state */

extern char   meu_indicator;                    /* last map access indicator (S | U | A | B | -) */
extern uint32 meu_page;                         /* last physical page number accessed */


/* Memory Protect global state */

extern HP_WORD mp_fence;                        /* memory protect fence register */


/* I/O subsystem global utility routine declarations */

extern t_bool    io_control  (uint32 select_code, IO_GROUP_OP micro_op);
extern SKPF_DATA io_dispatch (uint32 select_code, INBOUND_SET inbound_signals, HP_WORD inbound_value);


/* Main memory global utility routine declarations */

extern t_stat mem_initialize (uint32 memory_size);

extern HP_WORD mem_read       (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address);
extern void    mem_write      (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address, HP_WORD value);
extern uint8   mem_read_byte  (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address);
extern void    mem_write_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address, uint8 value);

extern HP_WORD mem_fast_read (HP_WORD address, MEU_MAP_SELECTOR map);

extern void   mem_zero            (uint32 starting_address, uint32 fill_count);
extern t_bool mem_is_empty        (uint32 starting_address);
extern void   mem_copy_loader     (MEMORY_WORD *buffer, uint32 starting_address, COPY_DIRECTION mode);
extern t_bool mem_is_idle_loop    (void);
extern void   mem_trace_registers (FLIP_FLOP interrupt_system);

/* Main memory global utility routines declared in io.h

extern HP_WORD mem_examine  (uint32 address);
extern void    mem_deposit  (uint32 address, HP_WORD value);
*/


/* Memory Expansion Unit global utility routine declarations */

extern void    meu_configure        (MEU_STATE configuration);
extern HP_WORD meu_read_map         (MEU_MAP_SELECTOR map, uint32 index);
extern void    meu_write_map        (MEU_MAP_SELECTOR map, uint32 index, uint32 value);
extern void    meu_set_fence        (HP_WORD new_fence);
extern void    meu_set_state        (MEU_STATE operation, MEU_MAP_SELECTOR map);
extern HP_WORD meu_update_status    (void);
extern HP_WORD meu_update_violation (void);
extern void    meu_assert_IAK       (void);
extern void    meu_privileged       (MEU_CONDITION condition);
extern uint32  meu_breakpoint_type  (t_bool is_iak);
extern uint32  meu_map_address      (HP_WORD logical, int32 switches);


/* Memory Protect global utility routine declarations */

extern t_bool mp_initialize          (void);
extern void   mp_configure           (t_bool is_enabled, t_bool is_optional);
extern void   mp_check_jmp           (HP_WORD address, uint32 lower_bound);
extern void   mp_check_jsb           (HP_WORD address);
extern void   mp_check_io            (uint32 select_code, IO_GROUP_OP micro_op);
extern void   mp_violation           (void);
extern void   mp_disable             (void);
extern t_bool mp_is_on               (void);
extern t_bool mp_reenable_interrupts (void);
extern t_bool mp_trace_violation     (void);
