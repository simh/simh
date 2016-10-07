/* hp3000_defs.h: HP 3000 simulator general declarations

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

   03-Sep-16    JDB     Added the STOP_POWER and STOP_ARSINH codes
   13-May-16    JDB     Modified for revised SCP API function parameter types
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   19-Mar-16    JDB     Added UNDEFs for the additional register macros
   04-Feb-16    JDB     First release version
   11-Dec-12    JDB     Created


   This file provides the general declarations used throughout the HP 3000
   simulator.  It is required by all modules.

   The author gratefully acknowledges the help of Frank McConnell in answering
   questions about the HP 3000.


   -----------------------------------------------------
   Implementation Note -- Compiling the Simulator as C++
   -----------------------------------------------------

   Although simulators are written in C, the SIMH project encourages developers
   to compile them with a C++ compiler to obtain the more careful type checking
   provided.  To obtain successful compilations, the simulator must be written
   in the subset of C that is also valid C++.  Using valid C features beyond
   that subset, as the HP 3000 simulator does, will produce C++ compiler errors.

   The standard C features used by the simulator that prevent error-free C++
   compilation are:

    1. Incomplete types.

       In C, mutually recursive type definitions are allowed by the use of
       incomplete type declarations, such as "DEVICE ms_dev;" followed later by
       "DEVICE ms_dev {...};".  Several HP device simulators use this feature to
       place a pointer to the device structure in the "desc" field of an MTAB
       array element, typically when the associated validation or display
       routine handles multiple devices.  As the DEVICE contains a pointer to
       the MTAB array, and an MTAB array element contains a pointer to the
       DEVICE, the definitions are mutually recursive, and incomplete types are
       employed.  C++ does not permit incomplete types.

    2. Implicit conversion of ints to enums.

       In C, enumeration types are compatible with integer types, and its
       members are constants having type "int".  As such, they are semantically
       equivalent to and may be used interchangeably with integers.  For the
       developer, though, C enumerations have some advantages.  In particular,
       the compiler may check a "switch" statement to ensure that all of the
       enumeration cases are covered.  Also, a mathematical set may be modeled
       by an enumeration type with disjoint enumerator values, with the bitwise
       integer OR and AND operators modeling the set union and intersection
       operations.  The latter has direct support in the "gdb" debugger, which
       will display an enumerated type value as a union of the various
       enumerators.  The HP simulator makes extensive use of both features to
       model hardware signal buses (e.g., INBOUND_SET, OUTBOUND_SET) and so
       performs bitwise integer operations on the enumerations to model signal
       assertion and denial.  In C++, implicit conversion from enumerations to
       integers is allowed, but conversion from integers to enumerations is
       illegal without explicit casts.  Therefore, the idiom employed by the
       simulator to assert a signal (e.g., "outbound_signals |= INTREQ") is
       rejected by the C++ compiler.

    3. Implicit increment operations on enums.

       Because enums are compatible with integers in C, no special enumerator
       increment operator is provided.  To cycle through the range of an
       enumeration type, e.g. in a "for" statement, the standard integer
       increment operator, "++", is used.  In C++, the "++" operator must be
       overloaded with a version specific to the enumeration type; applying the
       integer "++" to an enumeration is illegal.

    4. Use of C++ keywords as variable names.

       C++ reserves a number of additional keywords beyond those reserved by C.
       Use of any of these keywords as a variable or type name is legal C but
       illegal C++.  The HP simulator uses variables named "class" and
       "operator", which are keywords in C++.

   The HP simulator is written in ISO standard C and will compile cleanly with a
   compiler implementing the 1999 C standard.  Compilation as C++ is not a goal
   of the simulator and cannot work, given the incompatibilities listed above.
*/



#ifndef HP3000_DEFS_H_
#define HP3000_DEFS_H_


#include "sim_rev.h"
#include "sim_defs.h"



/* The following pragmas quell clang and Microsoft Visual C++ warnings that are
   on by default but should not be, in my opinion.  They warn about the use of
   perfectly valid code and require the addition of redundant parentheses and
   braces to silence them.  Rather than clutter up the code with scores of extra
   symbols that, in my view, make the code harder to read and maintain, I elect
   to suppress these warnings.

   VC++ 2008 warning descriptions:

    - 4114: "same type qualifier used more than once" [legal per C99]

    - 4554: "check operator precedence for possible error; use parentheses to
            clarify precedence"

    - 4996: "function was declared deprecated"
*/

#if defined (__clang__)
  #pragma clang diagnostic ignored "-Wlogical-op-parentheses"
  #pragma clang diagnostic ignored "-Wbitwise-op-parentheses"
  #pragma clang diagnostic ignored "-Wshift-op-parentheses"
  #pragma clang diagnostic ignored "-Wdangling-else"

#elif defined (_MSC_VER)
  #pragma warning (disable: 4114 4554 4996)

#endif


/* Device register display mode flags */

#define REG_A               (1u << REG_V_UF + 0)        /* permit any display */
#define REG_B               (1u << REG_V_UF + 1)        /* permit binary display */
#define REG_M               (1u << REG_V_UF + 2)        /* default to instruction mnemonic display */
#define REG_S               (1u << REG_V_UF + 3)        /* default to status mnemonic display */


/* Register macros.

   These additional register definition macros are used to define:

     FBDATA -- a one-bit flag in an arrayed register
     SRDATA -- an array of bytes large enough to hold a structure
     YRDATA -- a binary register

   The FBDATA macro defines a flag that is replicated in the same bit position
   in each element of an array; the array element size is assumed to be the
   minimum necessary to hold the bit at the given offset.  The SRDATA macro is
   used solely to SAVE data stored in a structure so that it may be RESTOREd
   later.  The YRDATA macro extends the functionality of the ORDATA, DRDATA, and
   HRDATA macros to registers with binary (base 2) representation.


   Implementation notes:

    1. Use caution that multiple FBDATA registers referencing the same array
       have offsets that imply the same array element size.  For example,
       offsets of 3 and 5 can be used with an array of 8-bit elements, and
       offsets 13 and 15 can be used with an array of 16-bit elements.  However,
       offsets 3 and 13 cannot be used, as the first implies 8-bit elements, and
       the second implies 16-bit elements.

    2. The macro names are UNDEFed to avoid potential name clashes with
       sim_def.h macros.

    3. The REG structure for version 4.0 contains several fields that are not
       present in 3.x versions.  The 4.x REGDATA macro maps to the REG strcture
       in a field-order- and preprocessor-independent manner.  There is no
       corresponding macro in 3.x, so we must handle standard vs. non-standard
       preprocessor differences by creating our own REGMAP macro to handle the
       mapping.
*/

#undef FBDATA
#undef SRDATA
#undef YRDATA
#undef REGMAP

#if (SIM_MAJOR >= 4)
  #define REGMAP(nm,loc,rdx,wd,off,dep,fl) \
    REGDATA (nm, loc, rdx, wd, off, dep, NULL, NULL, fl, 0, 0)

#elif defined (__STDC__) || defined (_WIN32)
  #define REGMAP(nm,loc,rdx,wd,off,dep,fl) \
    #nm, &(loc), (rdx), (wd), (off), (dep), (fl), 0

#else
  #define REGMAP(nm,loc,rdx,wd,off,dep,fl) \
    "nm", &(loc), (rdx), (wd), (off), (dep), (fl), 0

#endif

/*              Macro                    name   loc    radix  width  offset    depth     flags */
/*      -------------------------        ----  ------  -----  -----  ------  ----------  ----- */
#define FBDATA(nm,loc,ofs,dep,fl) REGMAP (nm,  (loc),    2,     1,   (ofs),    (dep),    (fl)  )
#define SRDATA(nm,loc,fl)         REGMAP (nm,  (loc),    8,     8,     0,    sizeof loc, (fl)  )
#define YRDATA(nm,loc,wid,fl)     REGMAP (nm,  (loc),    2,   (wid),   0,        1,      (fl)  )


/* Debugging and console output.

   "dprintf" is used to write debugging messages.  It does an "fprintf" to the
   debug output stream if the stream is open and the debug "flag" is currently
   enabled in device "dev".  Otherwise, it's a NOP.  "..." is the format string
   and associated values.

   "dpprintf" is identical to "dprintf", except that a device pointer is passed
   instead of a device structure.

   "DPRINTING" and "DPPRINTING" implement the test conditions for device and
   device pointer debugging, respectively.  They are used explicitly only when
   several debug statements employing the same flag are required, and it is
   desirable to avoid repeating the stream and flag test for each one.

   "cprintf", "cputs", and "cputc" are used to write messages to the console
   and, if console logging is enabled, to the log output stream.  They do
   "(f)printf", "fputs", or "(f)putc", respectively.  "..." is the format string
   and associated values, "str" is the string to write, and "ch" is the
   character to write.


   Implementation notes:

    1. The "cputs" macro uses "fputs" for both console and log file output
       because "puts" appends a newline, whereas "fputs" does not.
*/

#define DPRINTING(d,f)      (sim_deb && ((d).dctrl & (f)))

#define DPPRINTING(d,f)     (sim_deb && ((d)->dctrl & (f)))

#define dprintf(dev, flag, ...) \
          if (DPRINTING (dev, flag)) \
              hp_debug (&(dev), (flag), __VA_ARGS__); \
          else \
              (void) 0

#define dpprintf(dptr, flag, ...) \
          if (DPPRINTING (dptr, flag)) \
              hp_debug ((dptr), (flag), __VA_ARGS__); \
          else \
              (void) 0

#define cprintf(...) \
          do { \
              printf (__VA_ARGS__); \
              if (sim_log) \
                  fprintf (sim_log, __VA_ARGS__); \
          } while (0)

#define cputs(str) \
          do { \
              fputs (str, stdout); \
              if (sim_log) \
                  fputs (str, sim_log); \
          } while (0)

#define cputc(ch) \
          do { \
              putc (ch); \
              if (sim_log) \
                  fputc (ch, sim_log); \
          } while (0)


/* Simulation stop codes.

   These VM-specific status codes stop the simulator.  The "sim_stop_messages"
   array in "hp3000_sys.c" contains the message strings that correspond
   one-for-one with the stop codes.


   Implementation notes:

    1. Codes before STOP_RERUN cause the instruction to be rerun, so P is backed
       up twice.  For codes after, P points to the next instruction to be
       executed (which is the current instruction for an infinite loop stop).
*/

#define STOP_SYSHALT        1                   /* system halt */
#define STOP_UNIMPL         2                   /* unimplemented instruction stop */
#define STOP_UNDEF          3                   /* undefined instruction stop */
#define STOP_PAUS           4                   /* PAUS instruction stop */

#define STOP_RERUN          4                   /* stops above here cause the instruction to be re-run */

#define STOP_HALT           5                   /* programmed halt */
#define STOP_BRKPNT         6                   /* breakpoint */
#define STOP_INFLOOP        7                   /* infinite loop stop */
#define STOP_CLOAD          8                   /* cold load complete */
#define STOP_CDUMP          9                   /* cold dump complete */
#define STOP_ARSINH         10                  /* auto-restart inhibited */
#define STOP_POWER          11                  /* power is off */


/* Modifier validation identifiers */

#define MTAB_XDV            (MTAB_XTD | MTAB_VDV)
#define MTAB_XUN            (MTAB_XTD | MTAB_VUN)

#define VAL_DEVNO           0                   /* validate DEVNO=0-127      */
#define VAL_INTMASK         1                   /* validate INTMASK=0-15/E/D */
#define VAL_INTPRI          2                   /* validate INTPRI=0-31      */
#define VAL_SRNO            3                   /* validate SRNO=0-15        */


/* I/O event timing.

   I/O events are scheduled for future service by specifying the desired delay
   in units of event ticks.  Typically, one event tick represents the execution
   of one CPU instruction, and this is the way event ticks are defined in the
   current simulator implementation.  However, while the average execution time
   of a typical instruction mix on a Series II is given as 2.57 microseconds,
   actual instruction times vary greatly, due to the presence of block move and
   loop instructions.  Variations of an order of magnitude are common, and two
   orders or more are possible for longer blocks.

   To accommodate possible future variable instruction timing, I/O service
   activation times must not assume a constant 2.57 microseconds per event tick.
   Delays should be defined in terms of the "uS" (microseconds), "mS"
   (milliseconds), and "S" (seconds) macros below.
*/

#define USEC_PER_EVENT      2.57                /* average CPU instruction time in microseconds */

#define uS(t)               (uint32) ((t) > USEC_PER_EVENT ? (t) / USEC_PER_EVENT + 0.5 : 1)
#define mS(t)               (uint32) (((t) * 1000.0)    / USEC_PER_EVENT + 0.5)
#define S(t)                (uint32) (((t) * 1000000.0) / USEC_PER_EVENT + 0.5)


/* Architectural constants.

   These macros specify the width, sign location, value mask, and minimum and
   maximum signed and unsigned values for the data sizes supported by the
   simulator.  In addition, masks for 16-bit and 32-bit overflow are defined (an
   overflow is indicated if the masked bits are not all ones or all zeros).

   The HP_WORD type is used to declare variables that represent 16-bit registers
   or buses in hardware.


   Implementation notes:

    1. The HP_WORD type is a 32-bit unsigned type, instead of the more logical
       16-bit unsigned type.  There are two reasons for this.  First, SCP
       requires that scalars referenced by REG (register) entries be 32 bits in
       size.  Second, IA-32 processors execute instructions with 32-bit operands
       much faster than those with 16-bit operands.

       Using 16-bit operands omits the masking required for 32-bit values.  For
       example, the code generated for the following operations is as follows:

         uint16 a, b, c;
         a = b + c & 0xFFFF;

            movzwl  _b, %eax
            addw    _c, %ax
            movw    %ax, _a

         uint32 x, y, z;
         x = y + z & 0xFFFF;

            movl    _z, %eax
            addl    _y, %eax
            andl    $65535, %eax
            movl    %eax, _x

       However, the first case uses operand override prefixes, which require
       substantially more time to decode (6 clock cycles vs. 1 clock cycle).
       This time outweighs the additional 32-bit AND instruction, which executes
       in 1 clock cycle.

       On an Intel Core 2 Duo processor, defining HP_WORD as uint16 causes the
       HP 3000 memory diagnostic to run about 10% slower.
*/

typedef uint32              HP_WORD;                    /* HP 16-bit data word representation */

#define R_MASK              0177777u                    /* 16-bit register mask */

#define D8_WIDTH            8                           /* 8-bit data bit width */
#define D8_MASK             0377u                       /* 8-bit data mask */
#define D8_UMAX             0377u                       /* 8-bit unsigned maximum value */
#define D8_SMAX             0177u                       /* 8-bit signed maximum value */
#define D8_SMIN             0200u                       /* 8-bit signed minimum value */
#define D8_SIGN             0200u                       /* 8-bit sign */

#define D16_WIDTH           16                          /* 16-bit data bit width */
#define D16_MASK            0177777u                    /* 16-bit data mask */
#define D16_UMAX            0177777u                    /* 16-bit unsigned maximum value */
#define D16_SMAX            0077777u                    /* 16-bit signed maximum value */
#define D16_SMIN            0100000u                    /* 16-bit signed minimum value */
#define D16_SIGN            0100000u                    /* 16-bit sign */

#define D32_WIDTH           32                          /* 32-bit data bit width */
#define D32_MASK            037777777777u               /* 32-bit data mask */
#define D32_UMAX            037777777777u               /* 32-bit unsigned maximum value */
#define D32_SMAX            017777777777u               /* 32-bit signed maximum value */
#define D32_SMIN            020000000000u               /* 32-bit signed minimum value */
#define D32_SIGN            020000000000u               /* 32-bit sign */

#define D48_WIDTH           48                          /* 48-bit data bit width */
#define D48_MASK            07777777777777777uL         /* 48-bit data mask */
#define D48_UMAX            07777777777777777uL         /* 48-bit unsigned maximum value */
#define D48_SMAX            03777777777777777uL         /* 48-bit signed maximum value */
#define D48_SMIN            04000000000000000uL         /* 48-bit signed minimum value */
#define D48_SIGN            04000000000000000uL         /* 48-bit sign */

#define D64_WIDTH           64                          /* 64-bit data bit width */
#define D64_MASK            01777777777777777777777uL   /* 64-bit data mask */
#define D64_UMAX            01777777777777777777777uL   /* 64-bit unsigned maximum value */
#define D64_SMAX            00777777777777777777777uL   /* 64-bit signed maximum value */
#define D64_SMIN            01000000000000000000000uL   /* 64-bit signed minimum value */
#define D64_SIGN            01000000000000000000000uL   /* 64-bit sign */

#define S16_OVFL_MASK       ((uint32) D16_UMAX << D16_WIDTH | \
                              D16_SIGN)                 /* 16-bit signed overflow mask */

#define S32_OVFL_MASK       ((t_uint64) D32_UMAX << D32_WIDTH | \
                              D32_SIGN)                 /* 32-bit signed overflow mask */


/* Memory constants */

#define LA_WIDTH            16                              /* logical address bit width */
#define LA_MASK             ((1u << LA_WIDTH) - 1)          /* logical address mask (2 ** 16 - 1) */
#define LA_MAX              ((1u << LA_WIDTH) - 1)          /* logical address maximum (2 ** 16 - 1) */

#define BA_WIDTH            4                               /* bank address bit width */
#define BA_MASK             ((1u << BA_WIDTH) - 1)          /* bank address mask (2 ** 4 - 1) */
#define BA_MAX              ((1u << BA_WIDTH) - 1)          /* bank address maximum (2 ** 4 - 1) */

#define PA_WIDTH            (LA_WIDTH + BA_WIDTH)           /* physical address bit width */
#define PA_MASK             ((1u << PA_WIDTH) - 1)          /* physical address mask (2 ** 20 - 1) */
#define PA_MAX              ((1u << PA_WIDTH) - 1)          /* physical address maximum (2 ** 20 - 1) */

#define DV_WIDTH            16                              /* data value bit width */
#define DV_MASK             ((1u << DV_WIDTH) - 1)          /* data value mask (2 ** 16 - 1) */
#define DV_SIGN             ( 1u << (DV_WIDTH - 1))         /* data value sign (2 ** 15) */
#define DV_UMAX             ((1u << DV_WIDTH) - 1)          /* data value unsigned maximum (2 ** 16 - 1) */
#define DV_SMAX             ((1u << (DV_WIDTH - 1)) - 1)    /* data value signed maximum  (2 ** 15 - 1) */


/* Memory address macros.

   These macros convert between logical and physical addresses.  The functions
   provided are:

     - TO_PA     -- merge a bank number and offset into a physical address
     - TO_BANK   -- extract the bank number part of a physical address
     - TO_OFFSET -- extract the offset part of a physical address


   Implementation notes:

    1. The TO_PA offset parameter is not masked to 16 bits, as this value is
       almost always derived from a value that is inherently 16 bits in size.
       In the few cases where it is not, explicit masking is required.
*/

#define TO_PA(b,o)          (((uint32) (b) & BA_MASK) << LA_WIDTH | (uint32) (o))
#define TO_BANK(p)          ((p) >> LA_WIDTH & BA_MASK)
#define TO_OFFSET(p)        ((p) & LA_MASK)


/* Portable conversions.

   SIMH is written with the assumption that the defined-size types (e.g.,
   uint16) are at least the required number of bits but may be larger.
   Conversions that otherwise would make inherent size assumptions must instead
   be coded explicitly.  For example, doing:

     negative_value_32 = (int32) negative_value_16;

   ...will not guarantee that bits 0-15 of "negative_value_32" are ones, whereas
   the supplied sign-extension macro will.

   The conversions available are:

     - SEXT  -- int16 sign-extended to int32
     - NEG16 -- int16 negated
     - NEG32 -- int32 negated
     - INT16 -- uint16 to int16
     - INT32 -- uint32 to int32


   Implementation notes:

    1. The routines assume that 16-bit values are masked to exactly 16 bits
       before invoking.
*/

#define SEXT(x)         (int32) ((x) & D16_SIGN ? (x) | ~D16_MASK : (x))

#define NEG16(x)        ((~(x) + 1) & D16_MASK)
#define NEG32(x)        ((~(x) + 1) & D32_MASK)

#define INT16(u)        ((u) > D16_SMAX ? (-(int16) (D16_UMAX - (u)) - 1) : (int16) (u))
#define INT32(u)        ((u) > D32_SMAX ? (-(int32) (D32_UMAX - (u)) - 1) : (int32) (u))


/* Byte accessors.

   These macros extract the upper and lower bytes from a word and form a word
   from upper and lower bytes.  Replacement of a byte within a word is also
   provided, as is an enumeration type that defines byte selection.

   The accessors are:

     - UPPER_BYTE     -- return the byte from the upper position of a word value
     - LOWER_BYTE     -- return the byte from the lower position of a word value
     - TO_WORD        -- return a word with the specified upper and lower bytes

     - REPLACE_UPPER  -- replace the upper byte of the word value
     - REPLACE_LOWER  -- replace the lower byte of the word value

*/

typedef enum {
    upper,                                      /* upper byte selected */
    lower                                       /* lower byte selected */
    } BYTE_SELECTOR;

#define UPPER_BYTE(w)       (uint8)   ((w) >> D8_WIDTH & D8_MASK)
#define LOWER_BYTE(w)       (uint8)   ((w) &  D8_MASK)
#define TO_WORD(u,l)        (HP_WORD) (((u) & D8_MASK) << D8_WIDTH | (l) & D8_MASK)

#define REPLACE_UPPER(w,b)  ((w) & D8_MASK | ((b) & D8_MASK) << D8_WIDTH)
#define REPLACE_LOWER(w,b)  ((w) & D8_MASK << D8_WIDTH | (b) & D8_MASK)


/* Double-word accessors */

#define UPPER_WORD(d)       (HP_WORD) ((d) >> D16_WIDTH & D16_MASK)
#define LOWER_WORD(d)       (HP_WORD) ((d) &  D16_MASK)

#define TO_DWORD(u,l)       ((uint32) (u) << D16_WIDTH | (l))


/* Flip-flops */

typedef enum {
    CLEAR = 0,                                  /* the flip-flop is clear */
    SET   = 1                                   /* the flip-flop is set */
    } FLIP_FLOP;

#define TOGGLE(ff)          ff = (FLIP_FLOP) (ff ^ 1)   /* toggle a flip-flop variable */

#define D_FF(b)             (FLIP_FLOP) ((b) != 0)      /* use a Boolean expression for a D flip-flop */


/* Bitset formatting.

   See the comments at the "fmt_bitset" function (hp3000_sys.c) for details of
   the specification of bitset names and format structures.
*/

typedef enum {                                  /* direction of interpretation */
    msb_first,                                  /*   left-to-right */
    lsb_first                                   /*   right-to-left */
    } BITSET_DIRECTION;

typedef enum {                                  /* alternate names */
    no_alt,                                     /*   no alternates are present in the name array */
    has_alt                                     /*   the name array contains alternates */
    } BITSET_ALTERNATE;

typedef enum {                                  /* trailing separator */
    no_bar,                                     /*   omit a trailing separator */
    append_bar                                  /*   append a trailing separator */
    } BITSET_BAR;

typedef const char *const BITSET_NAME;          /* a bit name string pointer */

typedef struct {                                /* bit set format descriptor */
    uint32            name_count;               /*   count of bit names */
    BITSET_NAME       *names;                   /*   pointer to an array of bit names */
    uint32            offset;                   /*   offset from LSB to first bit */
    BITSET_DIRECTION  direction;                /*   direction of interpretation */
    BITSET_ALTERNATE  alternate;                /*   alternate interpretations presence */
    BITSET_BAR        bar;                      /*   trailing separator choice */
    } BITSET_FORMAT;

/* Bitset format specifier initialization */

#define FMT_INIT(names,offset,dir,alt,bar) \
          sizeof (names) / sizeof (names) [0], \
          (names), (offset), (dir), (alt), (bar)


/* System interface global data structures */

extern const HP_WORD odd_parity [256];          /* a table of parity bits for odd parity */

extern const BITSET_FORMAT inbound_format;      /* the inbound signal format structure */
extern const BITSET_FORMAT outbound_format;     /* the outbound signal format structure */


/* System interface global SCP support routines previously declared in scp.h */
/*
extern t_stat sim_load   (FILE       *fptr,  CONST char *cptr, CONST char *fnam, int     flag);
extern t_stat fprint_sym (FILE       *ofile, t_addr     addr,  t_value    *val,  UNIT    *uptr, int32 sw);
extern t_stat parse_sym  (CONST char *cptr,  t_addr     addr,  UNIT       *uptr, t_value *val,  int32 sw);
*/

/* System interface global SCP support routines */

extern t_stat hp_set_dib  (UNIT *uptr, int32 code,  CONST char *cptr, void       *desc);
extern t_stat hp_show_dib (FILE *st,   UNIT  *uptr, int32      code,  CONST void *desc);


/* System interface global utility routines */

extern t_bool hp_device_conflict (void);
extern t_stat fprint_cpu         (FILE *ofile, t_value *val, uint32 radix, int32 switches);

extern const char *fmt_status (uint32 status);
extern const char *fmt_char   (uint32 charval);
extern const char *fmt_bitset (uint32 bitset, const BITSET_FORMAT bitfmt);

extern void hp_debug (DEVICE *dptr, uint32 flag, ...);


#endif
