# LINC emulator

This is an emulator for the classic LINC from 1965.

### Devices

- CPU - LINC processor.  If throttled to 125k, approximately original speed.
- CRT - CRT display.  It can be disabled to run headless.
- DPY - point-plotting display controller.
- KBD - keyboard.  Input comes from typing in the CRT window.
- SAM - sampled analog inputs.
- TAPE - four tape drives.
- TTY - teletype.

### LOAD

Software can be loaded into memory using the `LOAD` command.  It will
normally read a file with 16-bit little endian words; the most
significant four bits of each word should be zero.  If the `-O` switch
is supplied, the input is a text file with octal numbers separated by
whitespace.

To start reading from some particular position in the input file, add
`OFFSET=` after the file name and supply a number in octal.

To specify where in the memory to put the data, add `START=` after the
file name; the default is 0.  To specify how many words to load, use
`LENGTH=`; the default is to write until the end of memory.

A binary input file can be a tape image.  A plain image is 512 blocks
of 256 words each, totalling 262144 bytes.  If the `-E` switch is
used, there is a check for the extended image format that can have
empty guard blocks at the beginning and the end.  The last three words
in the extended format specify the block size, first forward block
number, and first reverse block number.

To state which tape block to start reading, add `BLOCK=` after the
file name and supply an octal number in the range 0-777.

### DO

The SIMH `DO` command has been modified.  With arguments, it will
execute a script file as normal.  Without argument, it acts like the
DO button on the LINC control panel.  This executes one instruction
from the left switches, with the right switches providing a second
word if needed.

### Tape drives

To mount a tape image *file* on drive *n*, type `ATTACH TAPE<n>
<file>`.  The plain or extended image format will be detected
automatically.  Tape drives are numbered 0, 1, 4, and 5.

The `BOOT TAPEn` command will act like entering a tape read command in
the switches and starting the computer.  The default is to read eight
blocks starting at 300, and start from location 20.  This is the
conventional way to run LAP6.  You can also add `RDC=` or `RCG=` to
boot some particular blocks.  `START=` can be used to specify the
start address; it defaults to 20.

### Keyboard

The keys `0-9`, `A-Z`, and `Space` are mapped to their corresponding
LINC keys.  `Enter` is mapped to `EOL`, `Delete` and `Backspace` are
mapped to `DEL`, and `Shift` is mapped to `CASE`.  To type an upper
case symbol on some key, press `CASE`, release it, and then type the
key.  For convenience, `Alt` is mapped to `META`.

The remaining keys are mapped thusly:
- `F1` is mapped to the `pu` key.
- `=` is mapped to the `i=` key.
- `-` and `,` are mapped to the `-,` key.
- `.` is mapped to the `+. key.
- `\` is mapped to the `|⊟` key.
- `[` and `left backslash` are mapped to the `*[` key.

The remaining upper case symbols:
- `CASE A` - `"`.
- `CASE B` - `„`.
- `CASE C` - `<`.
- `CASE D` - `>`.
- `CASE E` - `]`.
- `CASE F` - `*`.
- `CASE G` - `:`.
- `CASE Space` - `?`.

### Teletype

The TTY device implmenents a teletype for printing output.  When a
file is attached, it will receive text decoded at 110 baud from relay
output 0.

Some characters are translated by LAP6 from the LINC character to ASCII:
- `i` to `&`
- `p` to `'`
- `|` to `\`
- `u` to `%`
- `⊟` to `$`
- `_` to `@`
- `"` to `^`
- `„` to `;`

### CPU

Registers:
- P - Instruction location, 10 bits.
- C - Current instruction, 12 bits.
- S - Memory address, 12 bits - 11 for address, 1 for halfword select.
- B - Memory data buffer, 12 bits.
- A - Accumulator, 12 bits - one's complement.
- Z - Various, 12 bits.
- L - Link, 1 bit.
- OVF - Overflow, 1 bit.
- IBZ - Tape interblock zone, 1 bit.
- ENI - Interrupt enabled, 1 bit.
- PINFF - Pause Interrupt enabled, 1 bit.

Switches:
- LSW - Left switches, 12 bits.
- RSW - Right switches, 12 bits.
- SSW - Sense switches, 6 bits.

Inputs:
- INTREQ, Interrupt request, 1 bit.
- R - Relays, 6 bits.
- XL - External levels, 12 of 1 bit each.
- SAM - Sampled analog inputs, 16 of 8 bits each; one's complement.

### Documentation

Programming:
https://bitsavers.org/pdf/washingtonUniversity/linc/Programming_the_LINC_Second_Edition_Jan69.pdf

Using the LAP6 operating system, text editor, assembler, tape filing system:
https://bitsavers.org/pdf/washingtonUniversity/linc/LINC_Reference_Manuals/LINC_Vol_16_Section_3_LAP6_Handbook_May67.pdf
