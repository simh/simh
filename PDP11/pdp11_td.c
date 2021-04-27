/* pdp11_td.c: TU58 simulator

   Copyright (c) 2015, Mark Pizzolato

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

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.

   td           TU58 DECtape

   26-Jun-15    MP      Initial Unibus/Qbus implemention merged from 
                        vax730_stddev.c (done by Matt Burke) and pdp11_dl.c
                        Added support for multiple concurrent TU58 devices
                        This module implements the TU58 functionality for the
                        VAX730 and VAX750 console devices as well as 
                        Unibus/Qbus connected dual drive TU58s.

   PDP-11 TU58 DECtapes are represented in memory by fixed length buffer of 32b words.

        16b                     256 words per block [256 x 16b]

Extracted from the TU58 DECtape II User's Guide - Programming Chapter:

3.1 GENERAL PRINCIPLES

The TU58 is controlled by a microprocessor that frees the host computer from device-related operations,
such as tape positioning and error retry. Only one high-level command to the microprocessor is necessary to
initiate a complex operation. The host and ru58 communicate via strings of one or more bytes called
packets. One brief packet can contain a message which completely describes a high-level command. The
handshaking sequences between host and TU58 as well as packet format are defined by the radial serial
protocol (RSP), or the modified radial serial protocol (MRSP), and were designed to be suitable for transmission
by asynchronous interfaces.

3.1.1 Block Number, Byte Count, and Drive Number
The TU58 uses a drive number, block number, and byte count to write or read data.  1-4 (Chapter 1)
shows the locations of blocks on the tape. If all of the desired data is contained within a single 512-byte
block, the byte count will be 512 or less. When the host asks for a particular block and a 512-or-Iess byte
count, the TU58 positions the specified drive (unit) at that block and transfers the number of bytes specified.
If the host asks for a block and also a byte count greater than that of the 512-byte boundary, the
TU58 reads as many sequential blocks as are needed to fulfill the byte count. The same process applies to
the write function. This means that the host software or an on-tape file directory need only store the number
of the first block in a file and the file's byte count to read or write all the data without having to know
the additional block numbers.

3.1.2 Special Handler Functions
Some device-related functions are not dealt with directly in the RSP, the MRSP, or in the ru58 firmware.
     1. A short routine called Newtape (Appendix B) should be included in a TU58 handler to provide a
        complete wind-rewind for new or environmentally stressed tape cartridges. This procedure
        brings the tape to proper operating tension levels.
     2. A TU58 handler should check the success code (byte 3 of the RSP or MRSP end message) for
        the presence of soft errors. This enables action to be taken before hard errors (permanent data
        losses) occur.

3.2 RADIAL SERIAL PROTOCOL (RSP) AND MODIFIED RSP (MRSP)

3.2.1 Packets
All communication between the host and the TU58 is accomplished via sequences of bytes called packets.
There are two types of multi-byte packets: Control (Command) and Data. Either RSP or MRSP may be
selected using the command packet switch byte. In addition, there are three single-byte packets used to
manage protocol and control the state of the system: INIT, Continue, and XOFF.

Control (Command) - A Control packet is sent to the TU58 to initiate all operations. The packet
    contains a message completely describing the operation to be performed. In the case of a read or
    write operation, for example, the message includes the function to be performed, unit (drive) number,
    byte count and block number.

    A special case of the Control packet, called an End packet, is sent from the TU58 to the host after
    completion of an operation or on an error. The End packet includes the status of the completed or
    aborted operation.

Data - The Data packet holds messages of between 1 and 128 bytes. This message is actually the
    data transferred from or to the TU58 during a read or write operation. For transmissions of larger
    than 128 bytes, the transfer is broken up and sent 128 bytes at a time.

INIT - This single-byte packet is sent to the TU58 to cause the power-up sequence. The TU58
    returns Continue after completion, to indicate that the power-up sequence has occurred. When the
    TU5S makes a protocol error or receives an invalid command, it reinitializes and sends INIT continuously
    to the host. When the host recognizes INIT, it sends Break to the TU58 to restore the
    protocol.

Bootstrap - A flag byte saying Bootstrap (octal 10), followed by a byte containing a drive number,
    causes the TU58 to read block 0 of the selected drive. It returns the 512 bytes without radial serial
    packaging. This simplifies bootstrap operations. Bootstrap may be sent by the host instead of a
    second INIT as part of the initialization process described below.

Continue - Before the host sends a Data packet to the TU58, it must wait until the TUS8 sends
    Continue. This permits the TU58 to control the rate that data packets are sent to it.

XON - An alternate term for Continue.

XOFF - Ordinarily, the TU58 does not have to wait between messages to the host. However, if the
    host is unable to receive all of a message from the peripheral at once, it may send XOFF. The
    TU58 stops transmitting immediately and waits until the host sends Continue to complete the
    transfer when it is ready. (Two characters may be sent by the UART to the host after the TUS8
    receives XOFF.)

3.2.1.1 Packet Usage - Position within the packet determines the meaning of each byte. All packets
begin with a flag byte, which announces the type of packet to follow. Flag byte numeric assignments
are as follows.

    Packet Type         Flag Byte Value
                        Octal   Binary
    Data                01      00001
    Control (Command)   02      00010
    INIT                04      00100
    Bootstrap           10      01000
    Continue            20      10000
    XON                 21      10001
    XOFF                23      10011

(Bits 5 - 7 of the nag byte are reserved.)

Multiple-byte (Control and Data) packets also contain a byte count byte, message bytes, and two checksum
bytes. The byte count byte is the number of message bytes in the packet. The two checksum bytes
are a 16-bit checksum. The checksum is formed by summing successive byte-pairs taken as 16-bit words
while adding any carry back into the sum (end-around carry), The flag and byte count bytes are included
in the checksum. (See example in Appendix 8.)

3.2.1 Break and Initialization
Break is a unique logic entity that can be interpreted by the TU58 and the host regardless of the state
of the protocol. This is the logical equivalent of a bus init or a master reset. Break is transmitted when
the serial line, which normally switches between two logic states called mark and space, is kept in the
space condition for at least one character time. This causes the TU58's UART to set its framing error
bit. The TU58 interprets the framing error as Break.

If communications breakdown, due to any transient problem, the host may restore order by sending
Break and IN IT as outlined above. The faulty operations are cancelled, and the TU58 reinitializes itself,
returns Continue, and waits for instructions.

With DIGITAL serial interfaces, the initialize sequence may be sent by the following sequence of operations.
Set the Break bit in the transmit control status register, then send two null characters. When the
transmit ready flag is set again, remove the Break bit. This times Break to be one character time long.
The second character is discarded by the TU58controller. Next, send two INIT characters. The first is
discarded by the TU58. The TU58 responds to the second INIT by sending Continue. When Continue
has been received, the initialize sequence is complete and any command packet may follow.

3.2.3 Command Packets
The command packet format is shown in Table 3-1. Bytes 0, 1, 12, and 13 are the message delivery
bytes. Their definitions follow.

    Table 3-1 Command Packet Structure
    Byte        Byte Contents
    o           Flag = 0000 0010(028)
    1           Message Byte Count = 0000 101 O( 128)
    2           Op Code
    3           Modifier
    4           Unit Number
    5           Switches
    6           Sequence Number - Low
    7           Sequence Number - High
    8           Byte Count - Low
    9           Byte Count - High
    10          Block Number - Low
    11          Block Number - High
    12          Checksum - Low
    13          Checksum - High

    0           Flag                    This byte is set to 00000010 to indicate 
                                        that the packet is a Command packet.
    1           Message Byte Count      Number of bytes in the packet, excluding the four message delivery
                                        bytes. This is decimal 10 for all command packets.
    12, 13      Checksum                The 16-bit checksum of bytes 0 through 11. The checksum is
                                        formed by treating each pair of bytes as a word and summing
                                        words with end-around carry.

    The remaining bytes are defined below.
    2           Op Code                 Operation being commanded. (See Table 34 and Paragraph 3.3
                                        for definitions.)
    3           Modifier                Permits variations of commands.
    4           Unit Number             Selects drive 0 or I.
    5           Switches                Selects maintenance mode and specifies RSP or MRSP.
    6,7         Sequence Number         Always zero for TU58.
    8,9         Byte Count              Number of bytes to be transferred by a read or write command.
                                        Ignored by other commands.
    10,11       Block Numbet            The block number to be used by commands requiring tape positioning.

3.1.3.1 Maintenance Mode - Setting bit 4 of the switches byte (byte 5) to I in a read command inhibits
retries on data errors. Instead, the incorrect data is delivered to the host followed by an end packet.
The success code in the end packet indicates a hard dt~.ta error. Since data is transmitted in 128-byte
packets, a multiple packet read progresses normally until a checksum mismatch occurs. Then the bad
data packet is transmitted, followed by the end packet, and the operation terminates.

3.1.3.1 Special Address Mode - Setting the most significant bit of the modifier byte (byte 3) to 1
selects special address mode. In this mode all tape positioning operations are addressed by 128-byte
records (0-2047) instead of 512-byte blocks (0-511). Zero-fill in a write operation only fills out to a 128-
byte boundary in this mode. To translate between normal addressing and special addressing, multiply
the normal address by 4. The result is the address of the first I 28-byte record of the block. Add I, 2, or
3 to get to the next three 128-byte records.

3.1.4 Data Packets

3.1.4.1 Radial Serial Protocol-A data transfer operation uses three or more message packets. The first
packet is the command packet from host to the TU58. Next, the data is transferred in 128-byte packets in
either direction (as required by read or write). After all data is transferred, the TU58 sends an end packet.
If the TUS8 encounters a failure before all data has been transferred, it sends the end packet as soon as the
failure occurs.

The data packet is shown in Table 3-2. The flag byte is set to 0018. The number of data bytes may be
between 1 and 128 bytes. For data transfers larger than 128 bytes, the transaction is broken up and sent
128 bytes at a time. The host is assumed to have enough buffer capacity to accept the entire transaction,
whereas the TU58 only has 128 bytes of buffer space. For write commands, the host must wait between
message packets for the TU58 to send the Continue flag 0208 before sending the next packet. Because the
host has enough buffer space, the TU58 does not wait for a Continue flag between message packets when it
sends back read data.

3.1.4.2 Modified Radial Serial Protocol- When the host does not have sufficient buffer space to accept
entire transactions at the hardware selected data transfer rate, modified radial serial protocol (MRSP) may
be specified using the command packet switch byte. Bit 3 of the switch byte is set to specify the MRSP. Bit
3 remains set until intentionally cleared or cleared during power up. A good practice is to set bit 3 in every
MRSP command packet.
MRSP is identical to RSP except during transmission to the host. When a command packet specifies
MRSP for the first time (that is, bit 3 of the switch byte was previously cleared or cleared during power
up), the ru58 will send one data or end packet byte (whichever occurs first). The subsequent bytes, up to
and including the last byte of the end packet, will not be transmitted until a Continue or an XON is
received from the host. To prevent a protocol error from occurring, it is necessary to transmit Continue or .
XON before transmitting any command packets. If a protocol error is detected, continuous INITs are sent
with the Continue handshake. If a bootstrap is being transmitted, however, no handshake is employed.

3.2.5 End Packets
The end packet is sent to the host by the ru58 after completion or termination of an operation or an error.
End packets are sent using RSP or MRSP as specified by the last command packet. The end packet is
shown in Table 3-3.

    Table 3-1 Data Packets
    Byte            Byte Contents
    0               Flag = 0000 0001
    1               Byte Count = M
    -----------------
    2               First Data Byte 
    3               Data

    M               Data
    M+1             Last Data Byte
    -----------------
    M+2             Checksum L
    M+3             Checksum H

    Table 3-3 End Packet
    Byte            Byte Contents
    0               Flag = 0000 0010
    1               Byte Count = 0000 1010
    -----------------
    2               Op Code - 0100 0000
    3               Success Code
    4               Unit
    5               Not Used
    6               Sequence No. L
    7               Sequence No. H
    8               Actual Byte Count L
    9               Actual Byte Count H
    10              Summary Status L
    11              Summary Status H
    -----------------
    12              Checksum L
    13              Checksum H

The definition of bytes 0, 1, 12, and 13 are the same as for the command packet. The remaining bytes
are defined as follows.

Byte 2              Op Code - 0100 0000 for end packet
Byte 3              Success Code
                        Octal   Decimal
                        0       0   = Normal success
                        1       1   = Success but with retries
                        377     -1  = Failed self test
                        376     -2  = Partial operation (end of medium)
                        370     -8  = Bad unit number
                        367     -9  = No cartridge
                        365     -11 = Write protected
                        357     -17 = Data check error
                        340     -32 = Seek error (block not found)
                        337     -33 = Motor stopped
                        320     -48 = Bad opcode
                        311     -55 = Bad block number (> 511)
Byte 4              Unit Number 0 or 1 for drive number.
Byte 5              Always 0
Bytes 6,7           Sequence number - always 0 as in command packet.
Bytes 8,9           Actual byte count - number of bytes handled in transaction. In a <good operation,
                    this is the same as the data byte count in the command packet.

Bytes 10,11         Summary Status
                        Byte 10
                            Bit 0       Reserved
                             ...
                            Bit 7       Reserved
                        Byte 11
                            Bit 0       Reserved
                            Bit 1       Reserved
                            Bit 2       Reserved
                            Bit 3       Reserved
                            Bit 4       Logic error
                            Bit 5       Motion error
                            Bit 6       Transfer error
                            Bit 7       Special condition (errors)

3.3 INSTRUCTION SET
The operation performed by the TU58 when it receives a Control (command) packet is determined by the
op code byte in the control packet message. Note that while any command can specify modified radial
serial protocol with the switch byte, the response will not be MRSP if a boot operation is being performed.
Instruction set op code byte assignments are listed in Table 3-4.

To allow for future development, certain op codes in the command set have been reserved. These commands
have unpredictable results and should not be used. Op codes not listed in the command set are illegal
and result in the return of an end packet with the "bad op code" success code.

        Table 3-4 Instruction Set
        OpCode  OpCode
        Decimal Octal   Instuction Set
        -----------------
        0       0       NOP
        1       1       INIT
        2       2       Read
        3       3       Write
        4       4       (Reserved)
        5       5       Position
        6       6       (Reserved)
        7       7       Diagnose
        8       10      Gctstatus
        9       11      Set status
        10      12      (Reserved)
        11      13      (Reserved)

The following is a brief description and usage example of each.

OP CODE`O NOP
This instruction causes the TU58 to return an end packet. There are no modifiers to NOP. The NOP
packet is shown below.

    BYTE
    0   0000 0010   FLAG
    1   0000 1010   MESSAGE BYTE CNT
    2   0000 0000   OPCODE
    3   0000 0000   MODIFIER
    4   0000 OOOX   UNIT NUMBER (IGNORED)
    5   0000 0000   SWITCHES (NOT USED)
    6   0000 0000   SEQ NO. (NOT USED)
    7   0000 0000   SEQ NO. (NOT USED)
    8   0000 0000   BYTE COUNT L NO DATA
    9   0000 0000   BYTE COUNT H INVOLVED
    10  0000 0000   BLOCKNO L NO TAPE
    11  0000 0000   BLOCKNO H POSITION
    12  0000 00IX   CHECKSUM L
    13  0000 1010   CHECKSUM H

The TUS8 returns the following end packet.

    0   0000 0010   FLAG
    1   0000 1010   MESSAGE BYTE CNT
    2   0100 0000   OPCOPE .
    3   0000 0000   SUCCESS CODE
    4   0000 OOOX   UNIT (IGNORED)
    S   0000 0000   NOT USED
    6   0000 0000   SEQ L
    7   0000 0000   SEQ H ....
    8   0000 0000   .. ACTUAL BYTE CNT L NO DATA
    9   0000 0000   ACTUAL BYTE CNT H INVOLVED
    10  0000 0000   SUMMARY STATUS L
    11  XXXX XXXX   SUMMARY STATUS H
    12  ooox XXXX   CHECKSUM L
    13  XXXX XXXX   CHECKSUM H

OP CODE 1 INIT
This instruction causes the TU58 controller to reset itself to a ready state. No tape positioning results
from this operation. The command 'packet is tbe same as for NOP except for the op code and the resultant
change to the low order checksum byte. The TU58 sends the same end packet as for NOP after
reinitializing itself. There are no modifiers to IN IT.

OP CODE 2 Read, and Read with Decreased Sensitivity
This instruction causes the TU58 to position the tape in the drive selected by Unit Number to the block
designated by the block number bytes. It reads data starting at the designated block and continues
reading until the byte count (command bytes 8 and 9) is satisfied. After data has been sent, the TU58
sends an end packet. Byte 3 indicates success, success with retries, or failure of the operation. In the
event of failure, the end packet is sent at the time of failure without filling up the data count. The end
packet is recognized by the host by the flag byte. The host sees a command flag (0000 0010) instead of
a data flag (0000 0001).

There are two modifiers to the read command. Setting the least significant bit of byte 3 to 1 causes the
TU58 to read the tape with decreased sensitivity in the read amplifier. This makes the read amplifier
miss data if any weak spots are present. Thus, if the TU58 can read error-free in this mode, the data is
healthy. The read transaction between TU58 and host is shown for 510 bytes (just under a full block) in
Figure 3-1. Setting the most significant bit of byte 3 to 1 selects special address mode. See Paragraphs
3.2.3.1 and 3.2.3.2.

OP CODE 3 Write, and Write and Read Verify
This op code causes the TU58 to position the tape in the selected driveto the block specified by the
number in bytes 10,11 of the command packet and write data from the first data packet into that block.
It writes data from subsequent data packets into one or more. blocks until the byte count called out in
bytes 8, 9 of the command packet has been satisfied.

The controller automatically zero-fills any remaining bytes ina 512-byte tape block.

There are two modifiers pennitted with the write command. Setting the least significant bit of byte
3 to 1 causes the TU58 to write all of the data and then back up and read the data just written with
decreased sensitivity and test the checksum of each record. If all of the checksums are correct, the
TU58 sends an end packet with the success code set to 0 (or 1 if retries were necessary to read the
data). Failure to read correct data results in a success code of - 17 (3578 ) to indicate a hard read
error. Setting the most significant bit of byte 3 to 1 selects special address mode. See Paragraph
3.2.3.2. . .

The write operation has to cope with the fact that the TU58 only has 128 bytes of buffer space. It is
necessary for the host to send a data packet and wait for the TU58 to write it before sending the next data
packet. This is accomplished using the continue flag. The continue flag is a single byte response of 000 1
0000 from TU58 to host. The RSP write transaction for both write and write/verify operations is shown in
Figure 3.2. The MRSP write transaction for both write and write/verify operations is shown in Figure 3.3.

OP CODE 4 (Resened)

OP CODE 5 Position
This command causes the TU58 to position tape on the selected drive to the block designated by bytes 10,
11. After reaching the selected block, it sends an end packet. See Paragraph 3.2.3.2.

OP CODE 6 (Reserved)

OP CODE 7 Diagnose
This command causes the TU58 to run its internal diagnostic program which tests the processor, ROM,
and RAM. Upon completion, TU58 sends an end packet with appropriate success code (0 = Pass, -1 =
Fail). Note that if the bootstrap hardware option is selected, boot information will be transmitted without
handshaking even if the switch byte specifies MRSP.

OP CODE 8 Get Status
This command is treated as a NOP. The TU58 returns an end packet.

OP CODE 9 Set Status
This command is treated as a NOP because TU58 status cannot be set from the host. The TU58 returns
an end packet.

OP CODE 10 (Resened)

OP CODE 11 (Resened)

*/

#if defined (VM_VAX)                                    /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#include "pdp11_td.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/* DL Definitions */

/* registers */

#define DLICSR_RD       (CSR_DONE|CSR_IE)               /* DL11C */
#define DLICSR_WR       (CSR_IE)
#define DLIBUF_ERR      0100000
#define DLIBUF_OVR      0040000
#define DLIBUF_RBRK     0020000
#define DLIBUF_RD       (DLIBUF_ERR|DLIBUF_OVR|DLIBUF_RBRK|0377)
#define DLOCSR_XBR      0000001                         /* xmit brk, RWNI */
#define DLOCSR_RD       (CSR_DONE|CSR_IE|DLOCSR_XBR)
#define DLOCSR_WR       (CSR_IE|DLOCSR_XBR)

static BITFIELD rx_csr_bits[] = {
    BITNCF(6),                          /* unused */
    BIT(IE),                            /* Interrupt Enable */
    BIT(DONE),                          /* Xmit Ready */
    BITNCF(8),                          /* unused */
    ENDBITS
};

static BITFIELD rx_buf_bits[] = {
    BITF(DAT,8),                        /* data buffer */
    BITNCF(5),                          /* unused */
    BIT(RBRK),
    BIT(OVR),
    BIT(ERR),
    ENDBITS
};

static BITFIELD tx_csr_bits[] = {
    BIT(XBR),                           /* Break */
    BITNC,                              /* unused */
    BIT(MAINT),                         /* Maint */
    BITNCF(3),                          /* unused */
    BIT(IE),                            /* Interrupt Enable */
    BIT(DONE),                          /* Xmit Ready */
    BITNCF(8),                          /* unused */
    ENDBITS
};

static BITFIELD tx_buf_bits[] = {
    BITF(DAT,8),                        /* data buffer */
    BITNCF(8),                          /* unused */
    ENDBITS
};

static BITFIELD *td_reg_bits[] = {
    rx_csr_bits,
    rx_buf_bits,
    tx_csr_bits,
    tx_buf_bits,
    };

static const char *tdc_regnam[] =
    {
    "RX_CSR",
    "RX_BUF",
    "TX_CSR",
    "TX_BUF"
    };
/* TU58 definitions */

#define TD_NUMCTLR      16                              /* #controllers */

#define TD_NUMBLK       512                             /* blocks/tape */
#define TD_NUMBY        512                             /* bytes/block */
#define TD_SIZE         (TD_NUMBLK * TD_NUMBY)          /* bytes/tape */

#define TD_OPDAT        001                             /* Data */
#define TD_OPCMD        002                             /* Command */
#define TD_OPINI        004                             /* INIT */
#define TD_OPBOO        010                             /* Bootstrap */
#define TD_OPCNT        020                             /* Continue */
#define TD_OPXOF        023                             /* XOFF */

#define TD_CMDNOP       0000                            /* NOP */
#define TD_CMDINI       0001                            /* INIT */
#define TD_CMDRD        0002                            /* Read */
#define TD_CMDWR        0003                            /* Write */
#define TD_CMDPOS       0005                            /* Position */
#define TD_CMDDIA       0007                            /* Diagnose */
#define TD_CMDGST       0010                            /* Get Status */
#define TD_CMDSST       0011                            /* Set Status */
#define TD_CMDMRSP      0012                            /* MRSP Request */
#define TD_CMDEND       0100                            /* END */

#define TD_STSOK        0000                            /* Normal success */
#define TD_STSRTY       0001                            /* Success with retries */
#define TD_STSFAIL      0377                            /* Failed selftest */
#define TD_STSPO        0376                            /* Partial operation (end of medium) */
#define TD_STSBUN       0370                            /* Bad unit number */
#define TD_STSNC        0367                            /* No cartridge */
#define TD_STSWP        0365                            /* Write protected */
#define TD_STSDCE       0357                            /* Data check error */
#define TD_STSSE        0340                            /* Seek error (block not found) */
#define TD_STSMS        0337                            /* Motor stopped */
#define TD_STSBOP       0320                            /* Bad opcode */
#define TD_STSBBN       0311                            /* Bad block number (>511) */

#define TD_GETOPC       0                               /* get opcode state */
#define TD_GETLEN       1                               /* get length state */
#define TD_GETDATA      2                               /* get data state */

#define TD_IDLE         0                               /* idle state */
#define TD_READ         1                               /* read */
#define TD_READ1        2                               /* fill buffer */
#define TD_READ2        3                               /* empty buffer */
#define TD_WRITE        4                               /* write */
#define TD_WRITE1       5                               /* write */
#define TD_WRITE2       6                               /* write */
#define TD_END          7                               /* empty buffer */
#define TD_END1         8                               /* empty buffer */
#define TD_INIT         9                               /* empty buffer */
#define TD_BOOTSTRAP    10                              /* bootstrap read */
#define TD_POSITION     11                              /* position */

static const char *td_states[] = {
    "IDLE",     "READ",     "READ1",    "READ2", 
    "WRITE",    "WRITE1",   "WRITE2",   "END", 
    "END1",     "INIT",     "BOOTSTRAP","POSITION"
    };

static const char *td_ops[] = {
    "NOP", "INI",   "RD",  "WR", "004", "POS", "006", "DIA", 
    "GST", "SST", "MRSP", "013", "014", "015", "016", "017",
    "020", "021",  "022", "023", "024", "025", "026", "027",
    "030", "031",  "032", "033", "034", "035", "036", "037",
    "040", "041",  "042", "043", "044", "045", "046", "047",
    "050", "051",  "052", "053", "054", "055", "056", "057",
    "060", "061",  "062", "063", "064", "065", "066", "067",
    "070", "071",  "072", "073", "074", "075", "076", "077",
    "END"
    };

static const char *td_csostates[] = {
    "GETOPC", "GETLEN", "GETDATA"
    };

static int32 td_stime = 100;                            /* seek, per block */
static int32 td_ctime = 150;                            /* command time */
static int32 td_xtime = 180;                            /* tr set time */
static int32 td_itime = 180;                            /* init time */

static int32 td_ctrls = 1;                              /* number of enabled controllers */

static uint32 tdi_ireq = 0;
static uint32 tdo_ireq = 0;

struct CTLR {
    DEVICE *dptr;
    UNIT *uptr;
    uint16 rx_csr;
    uint16 rx_buf;
    void (*rx_set_int) (int32 ctlr_num, t_bool val);
    uint16 tx_csr;
    uint16 tx_buf;
    void (*tx_set_int) (int32 ctlr_num, t_bool val);
    uint8 ibuf[TD_NUMBY+1];                 /* input buffer */
    int32 ibptr;                            /* input buffer pointer */
    int32 ilen;                             /* input length */
    uint8 obuf[TD_NUMBY+1];                 /* output buffer */
    int32 obptr;                            /* output buffer pointer */
    int32 olen;                             /* output length */
    int32 block;                            /* current block number */
    int32 txsize;                           /* remaining transfer size */
    int32 offset;                           /* offset into current transfer */
    int32 p_state;                          /* protocol state */
    int32 o_state;                          /* output state */
    int32 unitno;                           /* active unit number */
    int32 ecode;                            /* end packet success code */
    };

static CTLR td_ctlr[TD_NUMCTLR+1];          /* one for each DL based TU58 plus console */

static t_stat td_rd (int32 *data, int32 PA, int32 access);
static t_stat td_wr (int32 data, int32 PA, int32 access);
static t_stat td_svc (UNIT *uptr);
static t_stat td_reset (DEVICE *dptr);
static t_stat td_set_ctrls (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat td_show_ctlrs (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat td_boot (int32 unitno, DEVICE *dptr);
static t_stat td_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static void tdi_set_int (int32 ctlr, t_bool val);
static int32 tdi_iack (void);
static void tdo_set_int (int32 ctlr, t_bool val);
static int32 tdo_iack (void);

static const char *td_description (DEVICE *dptr);

static void td_process_packet(CTLR *ctrl);
static t_bool td_test_xfr (UNIT *uptr, int32 state);

/* TU58 data structures

   td_dev       TD device descriptor
   td_unit      TD unit list
   td_reg       TD register list
   td_mod       TD modifier list
*/

#define IOLN_DL         010

static DIB td_dib = {
    IOBA_AUTO, IOLN_DL, &td_rd, &td_wr,
    2, IVCL (TDRX), VEC_AUTO, { &tdi_iack, &tdo_iack }, IOLN_DL,
    };

static UNIT td_unit[2*TD_NUMCTLR];

static REG td_reg[] = {
    { DRDATAD (CTRLRS, td_ctrls,  4, "number of controllers"), REG_HRO },

    { DRDATAD (CTIME,  td_ctime,24, "command time"), PV_LEFT },
    { DRDATAD (STIME,  td_stime,24, "seek, per block"), PV_LEFT },
    { DRDATAD (XTIME,  td_xtime,24, "tr set time"), PV_LEFT },
    { DRDATAD (ITIME,  td_itime,24, "init time"), PV_LEFT },

#define RDATA(nm,loc,wd,desc) STRDATAD(nm,td_ctlr[0].loc,16,wd,0,TD_NUMCTLR+1,sizeof(CTLR),REG_RO,desc)
#define RDATAF(nm,loc,wd,desc,flds) STRDATADF(nm,td_ctlr[0].loc,16,wd,0,TD_NUMCTLR+1,sizeof(CTLR),REG_RO,desc,flds)

    { RDATA  (ECODE,  ecode,  32, "end packet success code") },
    { RDATA  (BLOCK,  block,  32, "current block number") },
    { RDATAF (RX_CSR, rx_csr, 16, "input control/status register",  rx_csr_bits) },
    { RDATAF (RX_BUF, rx_buf, 16, "input buffer register",          rx_buf_bits) },
    { RDATAF (TX_CSR, tx_csr, 16, "output control/status register", tx_csr_bits) },
    { RDATAF (TX_BUF, tx_buf, 16, "output buffer register",         tx_buf_bits) },
    { RDATA  (P_STATE,p_state,32, "protocol state") },
    { RDATA  (O_STATE,o_state,32, "output state") },
    { RDATA  (IBPTR,  ibptr,  32, "input buffer pointer") },
    { RDATA  (OBPTR,  obptr,  32, "output buffer pointer") },
    { RDATA  (ILEN,   ilen,   32, "input length") },
    { RDATA  (OLEN,   olen,   32, "output length") },
    { RDATA  (TXSIZE, txsize, 32, "remaining transfer size") },
    { RDATA  (OFFSET, offset, 32, "offset into current transfer") },
    { RDATA  (UNITNO, unitno, 32, "active unit number") },
/*
  REG entries for each controller's IBUF and OBUF are dynamically established 
  on first call to td_reset.
*/
    { NULL }
    };

static MTAB td_mod[] = {
    { MTAB_XTD|MTAB_VUN,   0, "write enabled", "WRITEENABLED",  &set_writelock, &show_writelock,   NULL, "Write enable TU58 drive" },
    { MTAB_XTD|MTAB_VUN,   1, NULL,             "LOCKED",       &set_writelock, NULL,              NULL, "Write lock TU58 drive" },
    { MTAB_XTD | MTAB_VDV, 0, "CONTROLLERS",    "CONTROLLERS",  &td_set_ctrls, &td_show_ctlrs,     NULL, "Number of Controllers" },
    { MTAB_XTD|MTAB_VDV,   0, "ADDRESS",        NULL,           &set_addr,     &show_addr,         NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV,   1, "VECTOR",         NULL,           &set_vec,      &show_vec,          NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE tdc_dev = {
    "TDC", td_unit, td_reg, td_mod,
    2*TD_NUMCTLR, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &td_reset,
    &td_boot, NULL, NULL,
    &td_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS | DEV_DEBUG, 0,
    td_deb, NULL, NULL, &td_help, NULL, NULL,
    &td_description
    };

#define CSI_CLR_INT ctlr->rx_set_int (ctlr-td_ctlr, 0)
#define CSI_SET_INT ctlr->rx_set_int (ctlr-td_ctlr, 1)
#define CSO_CLR_INT ctlr->tx_set_int (ctlr-td_ctlr, 0)
#define CSO_SET_INT ctlr->tx_set_int (ctlr-td_ctlr, 1)

t_stat td_rd_i_csr (CTLR *ctlr, int32 *data)
{
*data = ctlr->rx_csr & DLICSR_RD;
sim_debug_bits_hdr(TDDEB_IRD, ctlr->dptr, "RX_CSR", rx_csr_bits, *data, *data, 1);
return SCPE_OK;
}

t_stat td_wr_i_csr (CTLR *ctlr, int32 data)
{
if ((data & CSR_IE) == 0)
    CSI_CLR_INT;
else {
    if ((ctlr->rx_csr & (CSR_DONE | CSR_IE)) == CSR_DONE)
        CSI_SET_INT;
    }
sim_debug_bits_hdr(TDDEB_IWR, ctlr->dptr, "RX_CSR", rx_csr_bits, ctlr->rx_csr, data, 1);
ctlr->rx_csr = (ctlr->rx_csr & ~DLICSR_WR) | (data & DLICSR_WR);
return SCPE_OK;
}

t_stat td_rd_i_buf (CTLR *ctlr, int32 *data)
{
int32 t = ctlr->rx_buf;

ctlr->rx_csr &= ~CSR_DONE;                          /* clr done */
ctlr->rx_buf &= BMASK;                              /* clr errors */
sim_debug_bits_hdr(TDDEB_IRD, ctlr->dptr, "RX_BUF", rx_buf_bits, t, ctlr->rx_buf, 1);
CSI_CLR_INT;
*data = t;
return SCPE_OK;
}

t_stat td_wr_i_buf (CTLR *ctlr, int32 data)
{
sim_debug_bits_hdr(TDDEB_IWR, ctlr->dptr, "RX_BUF", rx_buf_bits, ctlr->rx_buf, ctlr->rx_buf, 1);
return SCPE_OK;
}

t_stat td_rd_o_csr (CTLR *ctlr, int32 *data)
{
sim_debug_bits_hdr(TDDEB_ORD, ctlr->dptr, "TX_CSR", tx_csr_bits, ctlr->tx_csr, ctlr->tx_csr, 1);
*data = ctlr->tx_csr & DLOCSR_RD;
return SCPE_OK;
}

t_stat td_wr_o_csr (CTLR *ctlr, int32 data)
{
sim_debug_bits_hdr(TDDEB_OWR, ctlr->dptr, "TX_CSR", tx_csr_bits, data, data, 1);
if ((ctlr->tx_csr & DLOCSR_XBR) && !(data & DLOCSR_XBR)) {
    ctlr->ibptr = 0;
    ctlr->ibuf[ctlr->ibptr++] = TD_OPINI;
    td_process_packet(ctlr);                             /* check packet */
    }
if ((data & CSR_IE) == 0)
    CSO_CLR_INT;
else if ((ctlr->tx_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    CSO_SET_INT;
ctlr->tx_csr = (ctlr->tx_csr & ~DLOCSR_WR) | (data & DLOCSR_WR);
return SCPE_OK;
}

t_stat td_rd_o_buf (CTLR *ctlr, int32 *data)
{
*data = 0;
sim_debug_bits_hdr(TDDEB_ORD, ctlr->dptr, "TX_BUF", tx_buf_bits, *data, *data, 1);
return SCPE_OK;
}

t_stat td_wr_o_buf (CTLR *ctlr, int32 data)
{
sim_debug (TDDEB_OWR, ctlr->dptr, "td_wr_o_buf() %s o_state=%s, ibptr=%d, ilen=%d\n", (ctlr->tx_csr & DLOCSR_XBR) ? "XMT-BRK" : "", td_csostates[ctlr->o_state], ctlr->ibptr, ctlr->ilen);
sim_debug_bits_hdr(TDDEB_OWR, ctlr->dptr, "TX_BUF", tx_buf_bits, data, data, 1);
ctlr->tx_buf = data & BMASK;                            /* save data */
ctlr->tx_csr &= ~CSR_DONE;                              /* clear flag */
CSO_CLR_INT;

switch (ctlr->o_state) {

    case TD_GETOPC:
        ctlr->ibptr = 0;
        ctlr->ibuf[ctlr->ibptr++] = ctlr->tx_buf & BMASK;
        td_process_packet(ctlr);                        /* check packet */
        break;

    case TD_GETLEN:
        ctlr->ibuf[ctlr->ibptr++] = ctlr->tx_buf & BMASK;
        ctlr->ilen = ctlr->tx_buf + 4;                  /* packet length + header + checksum */
        ctlr->o_state = TD_GETDATA;
        break;

    case TD_GETDATA:
        ctlr->ibuf[ctlr->ibptr++] = ctlr->tx_buf & BMASK;
        if (ctlr->ibptr >= ctlr->ilen) {
            ctlr->o_state = TD_GETOPC;
            td_process_packet(ctlr);
            }
        break;
    }
ctlr->tx_csr |= CSR_DONE;                               /* set input flag */
if (ctlr->tx_csr & CSR_IE)
    CSO_SET_INT;
return SCPE_OK;
}


static const char *reg_access[] = {"Read", "ReadC", "Write", "WriteC", "WriteB"};

typedef t_stat (*reg_read_routine) (CTLR *ctlr, int32 *data);

static reg_read_routine td_rd_regs[] = {
    td_rd_i_csr, 
    td_rd_i_buf,
    td_rd_o_csr, 
    td_rd_o_buf
    };

t_stat td_rd (int32 *data, int32 PA, int32 access)
{
int32 ctlr = ((PA - td_dib.ba) >> 3);

if (ctlr > td_ctrls)                                    /* validate controller number */
    return SCPE_IERR;

if (PA & 1)                                             /* odd address reference? */
    return SCPE_OK;

sim_debug (TDDEB_RRD, &tdc_dev, "td_rd(PA=%o(%s), access=%d-%s)\n", PA, tdc_regnam[(PA >> 1) & 03], access, reg_access[access]);

return (td_rd_regs[(PA >> 1) & 03])(&td_ctlr[ctlr], data);
}

typedef t_stat (*reg_write_routine) (CTLR *ctlr, int32 data);

static reg_write_routine td_wr_regs[] = {
    td_wr_i_csr, 
    td_wr_i_buf,
    td_wr_o_csr, 
    td_wr_o_buf
    };

static t_stat td_wr (int32 data, int32 PA, int32 access)
{
int32 ctrl = ((PA - td_dib.ba) >> 3);

if (ctrl > td_ctrls)                                    /* validate line number */
    return SCPE_IERR;

sim_debug (TDDEB_RWR, &tdc_dev, "td_wr(PA=%o(%s), access=%d-%s, data=%X)\n", PA, tdc_regnam[(PA >> 1) & 03], access, reg_access[access], data);

if (PA & 1)                                             /* odd address reference? */
    return SCPE_OK;

sim_debug_bits_hdr (TDDEB_RWR, &tdc_dev, tdc_regnam[(PA >> 1) & 03], td_reg_bits[(PA >> 1) & 03], data, data, 1);

return td_wr_regs[(PA >> 1) & 03](&td_ctlr[ctrl], data);
}

static void td_process_packet(CTLR *ctlr)
{
uint32 unit;
int32 opcode = ctlr->ibuf[0];
const char *opcode_name, *command_name;

switch (opcode) {
    case TD_OPDAT:
        opcode_name = "OPDAT";
        break;
    case TD_OPCMD:
        opcode_name = "OPCMD";
        break;
    case TD_OPINI:
        opcode_name = "OPINI";
        break;
    case TD_OPBOO:
        opcode_name = "OPBOO";
        break;
    case TD_OPCNT:
        opcode_name = "OPCNT";
        break;
    case TD_OPXOF:
        opcode_name = "OPXOF";
        break;
    default:
        opcode_name = "unknown";
    }
sim_debug (TDDEB_TRC, ctlr->dptr, "td_process_packet() Opcode=%s(%d)\n", opcode_name, opcode);
switch (opcode) {

    case TD_OPDAT:
        if (ctlr->p_state != TD_WRITE1) {                   /* expecting data? */
            sim_debug (TDDEB_ERR, ctlr->dptr, "td_process_packet() Opcode=%s(%d) - TU58 protocol error 1 - Not Expecting Data\n", opcode_name, opcode);
            return;
            }
        if (ctlr->ibptr < 2) {                              /* whole packet read? */
            ctlr->o_state = TD_GETLEN;                      /* get rest of packet */
            return;
            }
        ctlr->p_state = TD_WRITE2;
        sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);   /* sched command */
        break;

    case TD_OPCMD:
        if (ctlr->p_state != TD_IDLE) {                     /* expecting command? */
            sim_debug (TDDEB_ERR, ctlr->dptr, "td_process_packet() Opcode=%s(%d) - TU58 protocol error 2 - Not Expecting Command\n", opcode_name, opcode);
            return;
            }
        if (ctlr->ibptr < 2) {                              /* whole packet read? */
            ctlr->o_state = TD_GETLEN;                      /* get rest of packet */
            return;
            }
        if (ctlr->ibuf[2] > TD_CMDEND)
            command_name = "Unknown";
        else
            command_name = td_ops[ctlr->ibuf[2]];
        sim_debug (TDDEB_OPS, ctlr->dptr, "strt: fnc=%d(%s), len=%d, unit=%d, block=%d, size=%d\n", ctlr->ibuf[2], command_name, ctlr->ibuf[1], ctlr->ibuf[4], ((ctlr->ibuf[11] << 8) | ctlr->ibuf[10]), ((ctlr->ibuf[9] << 8) | ctlr->ibuf[8]));
        switch (ctlr->ibuf[2]) {
            case TD_CMDNOP:                              /* NOP */
            case TD_CMDGST:                              /* Get status */
            case TD_CMDSST:                              /* Set status */
            case TD_CMDINI:                              /* INIT */
            case TD_CMDDIA:                              /* Diagnose */
                ctlr->unitno = ctlr->ibuf[4];
                ctlr->p_state = TD_END;                  /* All treated as NOP */
                ctlr->ecode = TD_STSOK;
                ctlr->offset = 0;
                sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);/* sched command */
                break;
               
            case TD_CMDRD:
                ctlr->unitno = ctlr->ibuf[4];
                ctlr->block = ((ctlr->ibuf[11] << 8) | ctlr->ibuf[10]);
                ctlr->txsize = ((ctlr->ibuf[9] << 8) | ctlr->ibuf[8]);
                ctlr->p_state = TD_READ;
                ctlr->offset = 0;
                sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);/* sched command */
                break;
               
            case TD_CMDWR:
                ctlr->unitno = ctlr->ibuf[4];
                ctlr->block = ((ctlr->ibuf[11] << 8) | ctlr->ibuf[10]);
                ctlr->txsize = ((ctlr->ibuf[9] << 8) | ctlr->ibuf[8]);
                ctlr->p_state = TD_WRITE;
                ctlr->offset = 0;
                sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);/* sched command */
                break;
               
            case TD_CMDPOS:
                ctlr->unitno = ctlr->ibuf[4];
                ctlr->block = ((ctlr->ibuf[11] << 8) | ctlr->ibuf[10]);
                ctlr->txsize = 0;
                ctlr->p_state = TD_POSITION;
                ctlr->offset = 0;
                sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);/* sched command */
                break;
               
            case TD_CMDMRSP:
                ctlr->rx_buf = TD_OPDAT;
                ctlr->rx_csr |= CSR_DONE;               /* set input flag */
                if (ctlr->rx_csr & CSR_IE)
                    CSI_SET_INT;
                break;
            }
        break;

    case TD_OPINI:
        for (unit=0; unit < MIN(ctlr->dptr->numunits, 2); unit++)
            sim_cancel (ctlr->uptr+unit);
        ctlr->ibptr = 0;
        ctlr->obptr = 0;
        ctlr->olen = 0;
        ctlr->offset = 0;
        ctlr->txsize = 0;
        ctlr->o_state = TD_GETOPC;
        ctlr->p_state = TD_INIT;
        sim_activate (ctlr->uptr, td_itime);            /* sched command */
        break;

    case TD_OPBOO:
        if (ctlr->ibptr < 2) {                          /* whole packet read? */
            ctlr->ilen = 2;
            ctlr->o_state = TD_GETDATA;                 /* get rest of packet */
            return;
            }
        else {
            int8 *fbuf;
            int i;

            sim_debug (TDDEB_TRC, ctlr->dptr, "td_process_packet(OPBOO) Unit=%d\n", ctlr->ibuf[4]);
            ctlr->unitno = ctlr->ibuf[1];
            fbuf = (int8 *)ctlr->uptr[ctlr->unitno].filebuf;
            if (fbuf == NULL) {                         /* attached? */
                sim_debug (TDDEB_ERR, ctlr->dptr, "td_process_packet(OPBOO) Unit=%d - NOT ATTACHED\n", ctlr->ibuf[4]);
                break;
                }
            ctlr->block = 0;
            ctlr->txsize = 0;
            ctlr->p_state = TD_BOOTSTRAP;
            ctlr->offset = 0;
            ctlr->obptr = 0;

            for (i=0; i < TD_NUMBY; i++)
                ctlr->obuf[i] = fbuf[i];
            ctlr->olen = TD_NUMBY;
            ctlr->rx_buf = ctlr->obuf[ctlr->obptr++];   /* get first byte */
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)                  /* interrupt if enabled */
                CSI_SET_INT;
            sim_data_trace(ctlr->dptr, &ctlr->uptr[ctlr->unitno], ctlr->obuf, "Boot Block Data", ctlr->olen, "", TDDEB_DAT);
            sim_activate (ctlr->uptr+ctlr->unitno, td_ctime);/* sched command */
            }
        break;

    case TD_OPCNT:
        break;

    default:
        sim_debug (TDDEB_TRC, ctlr->dptr, "td_process_packet(%s) Unit=%d Unknown Opcode: %d\n", opcode_name, ctlr->ibuf[4], opcode);
        break;
    }
}

static t_stat td_svc (UNIT *uptr)
{
int32 i, t, data_size;
uint16 c, w;
uint32 da;
int8 *fbuf = (int8 *)uptr->filebuf;
CTLR *ctlr = (CTLR *)uptr->up7;

sim_debug (TDDEB_TRC, ctlr->dptr, "td_svc(%s, p_state=%s)\n", sim_uname(uptr), td_states[ctlr->p_state]);
switch (ctlr->p_state) {                                /* case on state */

    case TD_IDLE:                                       /* idle */
        return SCPE_IERR;                               /* done */

    case TD_READ: case TD_WRITE:                        /* read, write */
        if (td_test_xfr (uptr, ctlr->p_state)) {        /* transfer ok? */
            t = abs (ctlr->block - 0);                  /* # blocks to seek */
            if (t == 0)                                 /* minimum 1 */
                t = 1;
            ctlr->p_state++;                            /* set next state */
            sim_activate (uptr, td_stime * t);          /* schedule seek */
            break;
            }
        else 
            ctlr->p_state = TD_END;
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_POSITION:                                   /* position */
        if (td_test_xfr (uptr, ctlr->p_state)) {        /* transfer ok? */
            t = abs (ctlr->block - 0);                  /* # blocks to seek */
            if (t == 0)                                 /* minimum 1 */
                t = 1;
            ctlr->p_state = TD_END;                     /* set next state */
            sim_activate (uptr, td_stime * t);          /* schedule seek */
            break;
            }
        else 
            ctlr->p_state = TD_END;
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_READ1:                                      /* build data packet */
        da = (ctlr->block * 512) + ctlr->offset;        /* get tape address */
        if (ctlr->txsize > 128)                         /* Packet length */
            data_size = 128;
        else 
            data_size = ctlr->txsize;
        ctlr->txsize = ctlr->txsize - data_size;
        ctlr->offset = ctlr->offset + data_size;
        
        ctlr->obptr = 0;
        ctlr->obuf[ctlr->obptr++] = TD_OPDAT;           /* Data packet */
        ctlr->obuf[ctlr->obptr++] = data_size;          /* Data length */
        for (i = 0; i < data_size; i++)                 /* copy sector to buf */
            ctlr->obuf[ctlr->obptr++] = fbuf[da + i];
        c = 0;
        for (i = 0; i < (data_size + 2); i++) {         /* Calculate checksum */
            w = (ctlr->obuf[i] << ((i & 0x1) ? 8 : 0));
            c = c + w + ( (uint32)((uint32)c + (uint32)w) > 0xFFFF ? 1 : 0);
            }
        ctlr->obuf[ctlr->obptr++] = (c & 0xFF);         /* Checksum L */
        ctlr->obuf[ctlr->obptr++] = ((c >> 8) & 0xFF);  /* Checksum H */
        ctlr->olen = ctlr->obptr;
        ctlr->obptr = 0;
        ctlr->p_state = TD_READ2;                       /* go empty */
        sim_data_trace(ctlr->dptr, &ctlr->uptr[ctlr->unitno], ctlr->obuf, "Sending Read Data Packet", ctlr->olen, "", TDDEB_DAT);
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_READ2:                                      /* send data packet to host */
        if ((ctlr->rx_csr & CSR_DONE) == 0) {           /* prev data taken? */
            ctlr->rx_buf = ctlr->obuf[ctlr->obptr++];   /* get next byte */
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)
                CSI_SET_INT;
            if (ctlr->obptr >= ctlr->olen) {            /* buffer empty? */
                if (ctlr->txsize > 0)
                    ctlr->p_state = TD_READ1;
                else
                    ctlr->p_state = TD_END;
                }
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_WRITE1:                                     /* send continue */
        if ((ctlr->rx_csr & CSR_DONE) == 0) {           /* prev data taken? */
            ctlr->rx_buf = TD_OPCNT;
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)
                CSI_SET_INT;
            break;
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_WRITE2:                                     /* write data to buffer */
        da = (ctlr->block * 512) + ctlr->offset;        /* get tape address */
        ctlr->olen = ctlr->ibuf[1];
        for (i = 0; i < ctlr->olen; i++)                /* write data to buffer */
            fbuf[da + i] = ctlr->ibuf[i + 2];
        ctlr->offset += ctlr->olen;
        ctlr->txsize -= ctlr->olen;
        da = da + ctlr->olen;
        if (da > uptr->hwmark)                          /* update hwmark */
            uptr->hwmark = da;
        if (ctlr->txsize > 0)
            ctlr->p_state = TD_WRITE1;
        else {                                          /* check whole number of blocks written */
            if ((ctlr->olen = (512 - (ctlr->offset % 512))) != 512) {
                for (i = 0; i < ctlr->olen; i++)
                    fbuf[da + i] = 0;                   /* zero fill */
                da = da + ctlr->olen;
                if (da > uptr->hwmark)                  /* update hwmark */
                    uptr->hwmark = da;
                }
            ctlr->p_state = TD_END;
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;
        
    case TD_BOOTSTRAP:                                  /* send data to host */
        if ((ctlr->rx_csr & CSR_DONE) == 0) {           /* prev data taken? */
            ctlr->rx_buf = ctlr->obuf[ctlr->obptr++];   /* get next byte */
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)
                CSI_SET_INT;
            if (ctlr->obptr >= ctlr->olen) {            /* buffer empty? */
                ctlr->p_state = TD_IDLE;
                break;
                }
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_END:                                        /* build end packet */
        ctlr->obptr = 0;
        ctlr->obuf[ctlr->obptr++] = TD_OPCMD;           /* Command packet */
        ctlr->obuf[ctlr->obptr++] = 0xA;                /* ** Need definition ** */
        ctlr->obuf[ctlr->obptr++] = TD_CMDEND;
        ctlr->obuf[ctlr->obptr++] = ctlr->ecode;        /* Success code */
        ctlr->obuf[ctlr->obptr++] = ctlr->unitno;       /* Unit number */
        ctlr->obuf[ctlr->obptr++] = 0;                  /* Not used */
        ctlr->obuf[ctlr->obptr++] = 0;                  /* Sequence L (not used) */
        ctlr->obuf[ctlr->obptr++] = 0;                  /* Sequence H (not used) */
        ctlr->obuf[ctlr->obptr++] = (ctlr->offset & 0xFF);/* Byte count L */
        ctlr->obuf[ctlr->obptr++] = ((ctlr->offset >> 8) & 0xFF);/* Byte count H */
        ctlr->obuf[ctlr->obptr++] = 0;                  /* Summary status L */
        ctlr->obuf[ctlr->obptr++] = 0;                  /* Summary status H */
        c = 0;
        for (i = 0; i < (0xA + 2); i++) {               /* Calculate checksum */
            w = (ctlr->obuf[i] << ((i & 0x1) ? 8 : 0));
            c = c + w + ( (uint32)((uint32)c + (uint32)w) > 0xFFFF ? 1 : 0);
            }
        ctlr->obuf[ctlr->obptr++] = c & 0xFF;           /* Checksum L */
        ctlr->obuf[ctlr->obptr++] = (c >> 8) & 0xFF;    /* Checksum H */
        ctlr->olen = ctlr->obptr;
        ctlr->obptr = 0;
        ctlr->p_state = TD_END1;                        /* go empty */
        sim_debug(TDDEB_PKT, ctlr->dptr, "END PKT: %s Generated - Unit: %d, Success Code: %X\n", sim_uname(uptr), ctlr->unitno, ctlr->ecode);
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_END1:                                       /* send end packet to host */
        if ((ctlr->rx_csr & CSR_DONE) == 0) {           /* prev data taken? */
            ctlr->rx_buf = ctlr->obuf[ctlr->obptr++];   /* get next byte */
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)
                CSI_SET_INT;
            if (ctlr->obptr >= ctlr->olen) {            /* buffer empty? */
                sim_debug(TDDEB_PKT, ctlr->dptr, "END PKT: %s Sent. Unit=%d\n", sim_uname(uptr), ctlr->unitno);
                ctlr->p_state = TD_IDLE;
                break;
                }
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;

    case TD_INIT:
        if ((ctlr->rx_csr & CSR_DONE) == 0) {           /* prev data taken? */
            ctlr->rx_buf = TD_OPCNT;
            ctlr->rx_csr |= CSR_DONE;                   /* set input flag */
            if (ctlr->rx_csr & CSR_IE)
                CSI_SET_INT;
            ctlr->p_state = TD_IDLE;
            break;
            }
        sim_activate (uptr, td_xtime);                  /* schedule next */
        break;
    }
return SCPE_OK;
}

/* Test for data transfer okay */

static t_bool td_test_xfr (UNIT *uptr, int32 state)
{
CTLR *ctlr = (CTLR *)uptr->up7;

if ((uptr->flags & UNIT_BUF) == 0)                      /* not buffered? */
    ctlr->ecode = TD_STSNC;
else if (ctlr->block >= TD_NUMBLK)                      /* bad block? */
    ctlr->ecode = TD_STSBBN;
else if ((state == TD_WRITE) && (uptr->flags & UNIT_WPRT))               /* write and locked? */
    ctlr->ecode = TD_STSWP;
else {
    ctlr->ecode = TD_STSOK;
    return TRUE;
    }
return FALSE;
}

/* Interrupt routines */

static void tdi_set_int (int32 ctlr, t_bool val)
{
if ((tdi_ireq & (1 << ctlr)) ^ (val << ctlr)) {
    sim_debug (TDDEB_INT, &tdc_dev, "tdi_set_int(%d, %d)\n", ctlr, val);
    if (val)
        tdi_ireq |= (1 << ctlr);                        /* set rcv int */
    else
        tdi_ireq &= ~(1 << ctlr);                       /* clear rcv int */
    if (tdi_ireq == 0)                                  /* all clr? */
        CLR_INT (TDRX);
    else
        SET_INT (TDRX);                                 /* no, set intr */
    }
}

static int32 tdi_iack (void)
{
int32 ctlr;

sim_debug (TDDEB_INT, &tdc_dev, "tdi_iack()\n");
for (ctlr = 0; ctlr < TD_NUMCTLR; ctlr++) {             /* find 1st line */
    if (tdi_ireq & (1 << ctlr)) {
        tdi_set_int (ctlr, 0);                          /* clr req */
        return (td_dib.vec + (ctlr * 010));             /* return vector */
        }
    }
return 0;
}

static void tdo_set_int (int32 ctlr, t_bool val)
{
if ((tdo_ireq & (1 << ctlr)) ^ (val << ctlr)) {
    sim_debug (TDDEB_INT, &tdc_dev, "tdo_set_int(%d, %d)\n", ctlr, val);
    if (val)
        tdo_ireq |= (1 << ctlr);                        /* set xmt int */
    else
        tdo_ireq &= ~(1 << ctlr);                       /* clear xmt int */
    if (tdo_ireq == 0)                                  /* all clr? */
        CLR_INT (TDTX);
    else
        SET_INT (TDTX);                                 /* no, set intr */
    }
}

static int32 tdo_iack (void)
{
int32 ctlr;

sim_debug (TDDEB_INT, &tdc_dev, "tdo_iack()\n");
for (ctlr = 0; ctlr < TD_NUMCTLR; ctlr++) {            /* find 1st line */
    if (tdo_ireq & (1 << ctlr)) {
        tdo_set_int (ctlr, 0);                         /* clear intr */
        return (td_dib.vec + (ctlr * 010) + 4);        /* return vector */
        }
    }
return 0;
}

static t_stat td_reset_ctlr (CTLR *ctlr)
{
ctlr->tx_buf = 0;
ctlr->tx_csr = CSR_DONE;
CSI_CLR_INT;
ctlr->o_state = TD_GETOPC;
ctlr->ibptr = 0;
ctlr->obptr = 0;
ctlr->ilen = 0;
ctlr->olen = 0;
ctlr->offset = 0;
ctlr->txsize = 0;
ctlr->p_state = 0;
ctlr->ecode = 0;
return SCPE_OK;
}

/* Reset */

static t_stat td_reset (DEVICE *dptr)
{
CTLR *ctlr;
int ctl;
static t_bool td_enabled_reset = FALSE;
static t_bool td_regs_inited = FALSE;

if (!td_regs_inited) {
    int regs;
    int reg;
    REG *registers;

    /* Count initial register array */
    for (regs = 0; dptr->registers [regs].name != NULL; regs++)
        ;
    /* Allocate new register array with room for input and output buffer registers */
    registers = (REG *)calloc (regs + 2 * (TD_NUMCTLR + 1) + 1, sizeof (*registers));
    if (registers == NULL)
        return SCPE_MEM;
    /* Copy initial register array */
    for (reg = 0; reg < regs; reg++)
        registers[reg] = dptr->registers[reg];
    /* For each controller add input and output buffer register entries */
    for (ctl = 0; ctl < TD_NUMCTLR + 1; ctl++) {
        char reg_name[32];
        char reg_desc[64];
        static REG reg_template[] = {
            { BRDATAD(TBUF, td_ctlr[0].ibuf, 16, 8, TD_NUMBY + 1, "input buffer") },
            { NULL } };

        snprintf(reg_name, sizeof(reg_name), "IBUF_%d", ctl);
        registers[reg] = reg_template[0];
        registers[reg].name = (char *)calloc (strlen (reg_name) + 1, sizeof (char));
        strcpy ((char *)registers[reg].name, reg_name);
        snprintf(reg_desc, sizeof(reg_desc), "input buffer for %s%d", dptr->name, ctl);
        registers[reg].desc = (char*)calloc(strlen(reg_desc) + 1, sizeof(char));
        strcpy((char*)registers[reg].desc, reg_desc);
        registers[reg].loc = td_ctlr[ctl].ibuf;
        snprintf(reg_name, sizeof(reg_name), "OBUF_%d", ctl);
        registers[reg + 1] = reg_template[0];
        registers[reg + 1].name = (char*)calloc(strlen(reg_name) + 1, sizeof(char));
        strcpy((char*)registers[reg + 1].name, reg_name);
        snprintf(reg_desc, sizeof(reg_desc), "output buffer for %s%d", dptr->name, ctl);
        registers[reg + 1].desc = (char*)calloc(strlen(reg_desc) + 1, sizeof(char));
        strcpy((char*)registers[reg + 1].desc, reg_desc);
        registers[reg + 1].loc = td_ctlr[ctl].obuf;
        reg += 2;
        }
    dptr->registers = registers;
    td_regs_inited = TRUE;
    }

if (dptr->flags & DEV_DIS)
    td_enabled_reset = FALSE;
else {
    /* When the TDC device is just being enabled, */
    if (!td_enabled_reset) {
        char num[16];

        td_enabled_reset = TRUE;
        /* make sure to bound the number of DLI devices */
        sprintf (num, "%d", td_ctrls);
        td_set_ctrls (dptr->units, 0, num, NULL);
        }
    }

sim_debug (TDDEB_INT, dptr, "td_reset()\n");
for (ctl=0; ctl<TD_NUMCTLR; ctl++) {
    ctlr = &td_ctlr[ctl];
    ctlr->dptr = &tdc_dev;
    ctlr->uptr = td_unit + 2*ctl;
    ctlr->rx_set_int = tdi_set_int;
    ctlr->tx_set_int = tdo_set_int;
    td_unit[2*ctl+0].action = &td_svc;
    td_unit[2*ctl+0].flags |= UNIT_FIX|UNIT_ATTABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_DIS;
    td_unit[2*ctl+0].capac = TD_SIZE;
    td_unit[2*ctl+0].up7 = ctlr;
    td_unit[2*ctl+1].action = &td_svc;
    td_unit[2*ctl+1].flags |= UNIT_FIX|UNIT_ATTABLE|UNIT_BUFABLE|UNIT_MUSTBUF|UNIT_DIS;
    td_unit[2*ctl+1].capac = TD_SIZE;
    td_unit[2*ctl+1].up7 = ctlr;
    td_reset_ctlr (ctlr);
    sim_cancel (&td_unit[2*ctl]);
    sim_cancel (&td_unit[2*ctl+1]);
    }
for (ctl=0; ctl<td_ctrls; ctl++) {
    td_unit[2*ctl+0].flags &= ~UNIT_DIS;
    td_unit[2*ctl+1].flags &= ~UNIT_DIS;
    }
return auto_config (tdc_dev.name, td_ctrls);    /* auto config */
}

static const char *td_description (DEVICE *dptr)
{
return "TU58 cartridge";
}

/* Change number of controllers */

static t_stat td_set_ctrls (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i;
t_stat r;
DEVICE *dli_dptr = find_dev ("DLI");

if (cptr == NULL)
    return SCPE_ARG;
newln = (int32)get_uint (cptr, 10, TD_NUMCTLR, &r);
if (r != SCPE_OK)
    return r;
if (newln == 0)
    return SCPE_ARG;
if (newln < td_ctrls) {
    for (i = newln; i < td_ctrls; i++)
        if ((td_unit[2*i].flags & UNIT_ATT) || 
            (td_unit[2*i+1].flags & UNIT_ATT))
            return SCPE_ALATT;
    }
td_ctrls = newln;
td_dib.lnt = td_ctrls * td_dib.ulnt;            /* upd IO page lnt */
/* Make sure that the number of TU58 controllers plus DL devices is 16 or less */
if ((dli_dptr != NULL) && !(dli_dptr->flags & DEV_DIS) && 
    ((((DIB *)dli_dptr->ctxt)->numc + td_ctrls) > 16)) { /* Too many? */
    dli_dptr->flags |= DEV_DIS;                 /* First disable DL devices */
    dli_dptr->reset (dli_dptr);                 /* Notify of the disable */
    if (td_ctrls < 16) {                        /* Room for some DL devices? */
        dli_dptr->flags &= ~DEV_DIS;            /* Re-Enable DL devices */
        dli_dptr->reset (dli_dptr);             /* Notify of the enable which forces sizing */
        }
    }
return td_reset (&tdc_dev);
}

/* Show number of controllers */

t_stat td_show_ctlrs (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "controllers=%d", td_ctrls);
return SCPE_OK;
}

static t_stat td_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s (%s)\n\n", dptr->description (dptr), dptr->name);
fprintf (st, "DECtape TU58 Cartridge .\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

t_stat td_connect_console_device (DEVICE *dptr,
                                  void (*rx_set_int) (int32 ctlr_num, t_bool val),
                                  void (*tx_set_int) (int32 ctlr_num, t_bool val))
{
uint32 i;
CTLR *ctlr = &td_ctlr[TD_NUMCTLR];

for (i=0; i<dptr->numunits; i++) {
    dptr->units[i].capac = TD_SIZE;
    dptr->units[i].action = td_svc;
    dptr->units[i].flags |= UNIT_FIX|UNIT_ATTABLE|UNIT_BUFABLE|UNIT_MUSTBUF;
    dptr->units[i].up7 = (void *)ctlr;
    sim_cancel (&dptr->units[i]);
    }
ctlr->dptr = dptr;
ctlr->uptr = dptr->units;
ctlr->rx_set_int = rx_set_int;
ctlr->tx_set_int = tx_set_int;
return td_reset_ctlr (ctlr);
}

/* Device bootstrap */

#if defined (VM_PDP11)

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 000)              /* entry */
#define BOOT_CSR        (BOOT_START + 002)              /* CSR */
#define BOOT_UNIT       (BOOT_START + 006)              /* unit number */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

/* PDP11 Bootstrap adapted from 23-76589.mac.txt */

    static const uint16 boot_rom[] = {
                        /* RCSR = 0 offset from CSR in R1                   */
                        /* RBUF = 2 offset from CSR in R1                   */
                        /* TCSR = 4 offset from CSR in R1                   */
                        /* TBUF = 6 offset from CSR in R1                   */
                        /* BOOT_START:                                      */
    0012701, 0176500,   /*          MOV  #176500,R1  ; Set CSR              */
    0012700, 0000000,   /*          MOV  #0,R0       ; Set Unit Number      */
    0012706, BOOT_START,/*          MOV  #BOOT_START,SP ; Setup a Stack     */
    0005261, 0000004,   /*          INC  TCSR(R1)    ; Set BRK (Init)       */
    0005003,            /*          CLR  R3          ; data 000, 000        */
    0004767, 0000050,   /*          JSR  PC,10$      ; transmit many NULs   */
    0005061, 0000004,   /*          CLR  TCSR(R1)    ; Clear BRK            */
    0105761, 0000002,   /*          TSTB RBUF(R1)    ; Flush receive char   */
    0012703, 0004004,   /*          MOV  #<010*400>+004,r3; data 010,004    */
    0004767, 0000034,   /*          JSR  PC,12$      ; xmit 004(init) & 010(boot)*/
    0010003,            /*          MOV  R0,R3       ; get unit number      */
    0004767, 0000030,   /*          JSR  PC,13$      ; xmit unit number     */
                        /* ; setup complete, read data bytes                */
    0005003,            /*          CLR  R3          ; init load address    */
    0105711,            /* 1$:      TSTB RCSR(R1)    ; next ready?          */
    0100376,            /*          BPL  1$          ; not yet?             */
    0116123, 0000002,   /*          MOVB RBUF(R1),(R3)+ ; read next byte int memory */
    0022703, 0001000,   /*          CMP  #1000,R3     ; all done?           */
    0101371,            /*          BHI  1$           ; no, continue        */
    0005007,            /*          CLR  PC           ; Jump to bootstrap at 0 */
                        /* ; character Output routine                     */
    004717,             /* 10$:     JSR  PC,(PC)    ; Recurs call char replicate*/
    004717,             /* 11$:     JSR  PC,(PC)    ; Recurs call char replicate*/
    004717,             /* 12$:     JSR  PC,(PC)    ; Recurs call char replicate*/
    0105761, 0000004,   /* 13$:     TSTB TCSR(R1)   ; XMit Avail?               */
    0100375,            /*          BPL  13$        ; Wait for DONE             */
    0110361, 0000006,   /*          MOVB R3,TBUF(R1); Send Character            */
    0000303,            /*          SWAB R3         ; swap to other char        */
    0000207,            /*          RTS  PC         ; recurse or return         */
    };

static t_stat td_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    WrMemW (BOOT_START + (2 * i), boot_rom[i]);
WrMemW (BOOT_UNIT, unitno & 1);
WrMemW (BOOT_CSR, (td_dib.ba & DMASK) + (unitno >> 1) * 010);
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

static t_stat td_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif
