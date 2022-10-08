Example M68k Program
====================

As an example, I'll build an imaginary hardware platform.

The system is fairly simple, comprising a 68000, an input device, an output
device, a non-maskable-interrupt device, and an interrupt controller.

The input device receives input from the user and asserts its interrupt
request line until its value is read.  Reading from the input device's
memory-mapped port will both clear its interrupt request and read an ASCII
representation (8 bits) of what the user entered.

The output device reads value when it is selected through its memory-mapped
port and outputs it to a display.  The value it reads will be interpreted as
an ASCII value and output to the display. The output device is fairly slow
(it can only process 1 byte per second), and so it asserts its interrupt
request line when it is ready to receive a byte.  Writing to the output device
sends a byte to it.  If the output device is not ready, the write is ignored.
Reading from the output device returns 0 and clears its interrupt request line
until another byte is written to it and 1 second elapses.

The non-maskable-interrupt (NMI) device, as can be surmised from the name,
generates a non-maskable-interrupt.  This is connected to some kind of external
switch that the user can push to generate a NMI.

Since there are 3 devices interrupting the CPU, an interrupt controller is
needed.  The interrupt controller takes 7 inputs and encodes the highest
priority asserted line on the 3 output pins.  the input device is wired to IN2
and the output device is wired to IN1 on the controller.  The NMI device is
wired to IN7 and all the other inputs are wired low.

The bus is also connected to a 1K ROM and a 256 byte RAM.

Beware: This platform places ROM and RAM in the same address range and uses
        the FC pins to select the correct address space!
        (You didn't expect me to make it easy, did you? =)



Schematic
---------

     NMI     TIED
    SWITCH   LOW
      |       |
      | +-+-+-+
      | | | | | +------------------------------------------------+
      | | | | | | +------------------------------------+         |
      | | | | | | |                                    |         |
     +-------------+                                   |         |
     |7 6 5 4 3 2 1|                                   |         |
     |             |                                   |         |
     | INT CONTRLR |                                   |         |
     |             |                                   |         |
     |i i i        |                                   |         |
     |2 1 0        |                                   |         |
     +-------------+                                   |         |
      | | |                                            |         |
      | | |     +--------------------------------+--+  |         |
      o o o     |                                |  |  |         |
    +--------------+  +-------+  +----------+  +---------+  +----------+
    | I I I     a  |  |       |  |          |  | r  a  i |  |    i     |
    | 2 1 0    23  |  |       |  |          |  | e  c    |  |          |
    |              |  |       |  |          |  | a  k    |  |          |
    |              |  |       |  |          |  | d       |  |          |
    |              |  |       |  |          |  |         |  |          |
    |    M68000    |  |  ROM  |  |   RAM    |  |   IN    |  |   OUT    |
    |              |  |       |  |          |  |         |  |          |
    |            a9|--|a9     |--|          |--|         |--|          |
    |            a8|--|a8     |--|          |--|         |--|          |
    |            a7|--|a7     |--|a7        |--|         |--|          |
    |            a6|--|a6     |--|a6        |--|         |--|          |
    |            a5|--|a5     |--|a5        |--|         |--|          |
    |            a4|--|a4     |--|a4        |--|         |--|          |
    |            a3|--|a3     |--|a3        |--|         |--|          |
    |            a2|--|a2     |--|a2        |--|         |--|          |
    |            a1|--|a1     |--|a1        |--|         |--|          |
    |            a0|--|a0     |--|a0        |--|         |--|          |
    |              |  |       |  |          |  |         |  |          |
    |           d15|--|d15    |--|d15       |--|         |--|          |
    |           d14|--|d14    |--|d14       |--|         |--|          |
    |           d13|--|d13    |--|d13       |--|         |--|          |
    |           d12|--|d12    |--|d12       |--|         |--|          |
    |           d11|--|d11    |--|d11       |--|         |--|          |
    |           d10|--|d10    |--|d10       |--|         |--|          |
    |            d9|--|d9     |--|d9        |--|         |--|          |
    |            d8|--|d8     |--|d8        |--|         |--|          |
    |            d7|--|d7     |--|d7        |--|d7       |--|d7        |
    |            d6|--|d6     |--|d6        |--|d6       |--|d6        |
    |            d5|--|d5     |--|d5        |--|d5       |--|d5        |
    |            d4|--|d4     |--|d4        |--|d4       |--|d4        |
    |            d3|--|d3     |--|d3        |--|d3       |--|d3        |
    |            d2|--|d2     |--|d2        |--|d2       |--|d2        |
    |            d1|--|d1     |--|d1        |--|d1       |--|d1  w     |
    |            d0|--|d0     |--|d0        |--|d0       |--|d0  r     |
    |              |  |       |  |          |  |         |  |    i   a |
    | a      F F F |  |       |  |          |  |         |  |    t   c |
    |22  rW  2 1 0 |  |  cs   |  | cs   rW  |  |         |  |    e   k |
    +--------------+  +-------+  +----------+  +---------+  +----------+
      |   |  | | |        |         |    |                       |   |
      |   |  | | |        |         |    |                       |   |
      |   |  | | |    +-------+  +-----+ |                     +---+ |
      |   |  | | |    |  IC1  |  | IC2 | |                     |AND| |
      |   |  | | |    |a b c d|  |a b c| |                     +---+ |
      |   |  | | |    +-------+  +-----+ |                      | |  |
      |   |  | | |     | | | |    | | |  |                      | +--+
      |   |  | | |     | | | |    | | |  |                      | |
      |   |  | | |     | | | |    | | |  |                      | |
      |   |  | | |     | | | |    | | |  |                      | |
      |   |  | | +-----)-)-+-)----)-)-+  |                      | |
      |   |  | +-------)-+---)----)-+    |                      | |
      |   |  +---------+-----)----+      |                      | |
      |   |                  |           |                      | |
      |   +------------------+-----------+----------------------+ |
      |                                                           |
      +-----------------------------------------------------------+

IC1: output=1 if a=0 and b=1 and c=0 and d=0
IC2: output=1 if a=0 and b=0 and c=1



Program Listing (program.bin)
-----------------------------

```
                        INPUT_ADDRESS   equ $800000
                        OUTPUT_ADDRESS  equ $400000
                        CIRCULAR_BUFFER equ $c0
                        CAN_OUTPUT      equ $d0
                        STACK_AREA      equ $100
                        
                        vector_table:
00000000 0000 0100          dc.l STACK_AREA             *  0: SP
00000004 0000 00c0          dc.l init                   *  1: PC
00000008 0000 0148          dc.l unhandled_exception    *  2: bus error
0000000c 0000 0148          dc.l unhandled_exception    *  3: address error
00000010 0000 0148          dc.l unhandled_exception    *  4: illegal instruction
00000014 0000 0148          dc.l unhandled_exception    *  5: zero divide
00000018 0000 0148          dc.l unhandled_exception    *  6: chk
0000001c 0000 0148          dc.l unhandled_exception    *  7: trapv
00000020 0000 0148          dc.l unhandled_exception    *  8: privilege violation
00000024 0000 0148          dc.l unhandled_exception    *  9: trace
00000028 0000 0148          dc.l unhandled_exception    * 10: 1010
0000002c 0000 0148          dc.l unhandled_exception    * 11: 1111
00000030 0000 0148          dc.l unhandled_exception    * 12: -
00000034 0000 0148          dc.l unhandled_exception    * 13: -
00000038 0000 0148          dc.l unhandled_exception    * 14: -
0000003c 0000 0148          dc.l unhandled_exception    * 15: uninitialized interrupt
00000040 0000 0148          dc.l unhandled_exception    * 16: -
00000044 0000 0148          dc.l unhandled_exception    * 17: -
00000048 0000 0148          dc.l unhandled_exception    * 18: -
0000004c 0000 0148          dc.l unhandled_exception    * 19: -
00000050 0000 0148          dc.l unhandled_exception    * 20: -
00000054 0000 0148          dc.l unhandled_exception    * 21: -
00000058 0000 0148          dc.l unhandled_exception    * 22: -
0000005c 0000 0148          dc.l unhandled_exception    * 23: -
00000060 0000 0148          dc.l unhandled_exception    * 24: spurious interrupt
00000064 0000 0136          dc.l output_ready           * 25: l1 irq
00000068 0000 010e          dc.l input_ready            * 26: l2 irq
0000006c 0000 0148          dc.l unhandled_exception    * 27: l3 irq
00000070 0000 0148          dc.l unhandled_exception    * 28: l4 irq
00000074 0000 0148          dc.l unhandled_exception    * 29: l5 irq
00000078 0000 0148          dc.l unhandled_exception    * 30: l6 irq
0000007c 0000 014e          dc.l nmi                    * 31: l7 irq
00000080 0000 0148          dc.l unhandled_exception    * 32: trap 0
00000084 0000 0148          dc.l unhandled_exception    * 33: trap 1
00000088 0000 0148          dc.l unhandled_exception    * 34: trap 2
0000008c 0000 0148          dc.l unhandled_exception    * 35: trap 3
00000090 0000 0148          dc.l unhandled_exception    * 36: trap 4
00000094 0000 0148          dc.l unhandled_exception    * 37: trap 5
00000098 0000 0148          dc.l unhandled_exception    * 38: trap 6
0000009c 0000 0148          dc.l unhandled_exception    * 39: trap 7
000000a0 0000 0148          dc.l unhandled_exception    * 40: trap 8
000000a4 0000 0148          dc.l unhandled_exception    * 41: trap 9
000000a8 0000 0148          dc.l unhandled_exception    * 42: trap 10
000000ac 0000 0148          dc.l unhandled_exception    * 43: trap 11
000000b0 0000 0148          dc.l unhandled_exception    * 44: trap 12
000000b4 0000 0148          dc.l unhandled_exception    * 45: trap 13
000000b8 0000 0148          dc.l unhandled_exception    * 46: trap 14
000000bc 0000 0148          dc.l unhandled_exception    * 47: trap 15
                        * This is the end of the useful part of the table.
                        * We will now do the Capcom thing and put code starting at $c0.
                        
                        init:
                        * Copy the exception vector table to RAM.
000000c0 227c 0000 0000     move.l  #0, a1                      * a1 is RAM index
000000c6 303c 002f          move.w  #47, d0                     * d0 is counter (48 vectors)
000000ca 41fa 0006          lea.l   (copy_table,PC), a0         * a0 is scratch
000000ce 2208               move.l  a0, d1                      * d1 is ROM index
000000d0 4481               neg.l   d1
                        copy_table:
000000d2 22fb 18fe          dc.l    $22fb18fe                   * stoopid as68k generates 020 code here
                        *   move.l  (copy_table,PC,d1.l), (a1)+
000000d6 5841               addq    #4, d1
000000d8 51c8 fff8          dbf     d0, copy_table
                        
                        main_init:
                        * Initialize main program
000000dc 11fc 0000 00d0     move.b  #0, CAN_OUTPUT
000000e2 4df8 00c0          lea.l   CIRCULAR_BUFFER, a6
000000e6 7c00               moveq   #0, d6                      * output buffer ptr
000000e8 7e00               moveq   #0, d7                      * input buffer ptr
000000ea 027c f8ff          andi    #$f8ff, SR                  * clear interrupt mask
                        main:
                        * Main program
000000ee 4a38 00d0          tst.b   CAN_OUTPUT                  * can we output?
000000f2 67fa               beq     main
000000f4 be06               cmp.b   d6, d7                      * is there data?
000000f6 67f6               beq     main
000000f8 11fc 0000 00d0     move.b  #0, CAN_OUTPUT
000000fe 13f6 6000 0040     move.b  (0,a6,d6.w), OUTPUT_ADDRESS * write data
         0000
00000106 5246               addq    #1, d6
00000108 0206 000f          andi.b  #15, d6                     * update circular buffer
0000010c 60e0               bra     main
                        
                        
                        input_ready:
0000010e 2f00               move.l  d0, -(a7)
00000110 2f01               move.l  d1, -(a7)
00000112 1239 0080 0000     move.b  INPUT_ADDRESS, d1           * read data
00000118 1007               move.b  d7, d0                      * check if buffer full
0000011a 5240               addq    #1, d0
0000011c 0200 000f          andi.b  #15, d0
00000120 bc00               cmp.b   d0, d6
00000122 6700 000c          beq     input_ready_quit            * throw away if full
00000126 1d81 7000          move.b  d1, (0,a6,d7.w)             * store the data
0000012a 5247               addq    #1, d7
0000012c 0207 000f          andi.b  #15, d7                     * update circular buffer
                        input_ready_quit:
00000130 221f               move.l  (a7)+, d1
00000132 201f               move.l  (a7)+, d0
00000134 4e73               rte
                        
                        output_ready:
00000136 2f00               move.l  d0, -(a7)
00000138 11fc 0001 00d0     move.b  #1, CAN_OUTPUT
0000013e 1039 0040 0000     move.b  OUTPUT_ADDRESS, d0          * acknowledge the interrupt
00000144 201f               move.l  (a7)+, d0
00000146 4e73               rte
                        
                        unhandled_exception:
00000148 4e72 2700          stop    #$2700                      * wait for NMI
0000014c 60fa               bra     unhandled_exception         * shouldn't get here
                        
                        nmi:
                        * perform a soft reset
0000014e 46fc 2700          move    #$2700, SR                  * set status register
00000152 2e7a feac          move.l  (vector_table,PC), a7       * reset stack pointer
00000156 4e70               reset                               * reset peripherals
00000158 4efa feaa          jmp     (vector_table+4,PC)         * reset program counter
                        
                        END
```


Compiling the example host environment
--------------------------------------

### DOS

`osd_dos.c`

This code was written a long time ago, in another era when DOS environments
were still a thing. Thus, the primary interface was designed around the DOS
console, which allowed easy control over keyboard input and console echo. If
you still have such an environment, compilers such as mingw or djgpp are
enough to compile it.

### Linux

`osd_linux.c`

There's now a Linux interface but it's very **very** basic. The terminal echoes
back everything you type immediately, which makes the output look weird as the
program output mixes with the terminal echo.

Unfortunately, I don't have enough free time to sort through the
termios/ncurses minefield to make it behave better. If you happen to know how
to do this sort of thing, please help out by submitting a PR!

### Other Platforms

You'll need to make an `osd_xyz.c` for your particular platform to implement
`osd_get_key()`, and update the Makefile to handle your particular environment.

### Building and running

You'll need a C compiler. GCC or Clang should work fine. MSVC will require you
to set up a project (use the makefile as a guide).

- Type `make`
- Type `./sim program.bin`
- Interact with program.bin using the keyboard.

#### Keys:

    ESC           - quits the simulator
    ~             - generates an NMI interrupt
    Any other key - Genearate input for the input device


### Note

I've cheated a bit in the emulation.  There is no speed control to set the
speed the CPU runs at; it simply runs as fast as your processor can run it.

To add speed control, you will need a high-precision timestamp function
(like the RDTSC instruction for newer Pentium CPUs) and a bit of arithmetic
to make the cycles argument for m68k_execute(). I'll leave that as an
excercise to the reader.
