/* swtp_sys.c: SWTP 6800 system interface

   Copyright (c) 2005, William Beech

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
   WILLIAM A BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A. Beech shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A. Beech.

   Based on work by Charles E Owen (c) 1997 and Peter Schorn (c) 2002-2005

*/

#include <ctype.h>
#include <string.h>
#include "swtp_defs.h"

/* externals */

extern DEVICE cpu_dev;
extern DEVICE dsk_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern DEVICE sio_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE lpt_dev;
extern unsigned char M[];
extern int32 saved_PC;
extern int32 sim_switches;
//extern int32 (*sim_vm_fprint_addr)(FILE*, DEVICE*,t_addr);

/* prototypes */

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int flag);
int32 fprint_sym (FILE *of, int32 addr, uint32 *val,
	UNIT *uptr, int32 sw);
t_addr fprint_addr(FILE *stream, DEVICE *dptr, t_addr addr);
int32 parse_sym (char *cptr, int32 addr, UNIT *uptr, uint32 *val, int32 sw);
void sim_special_init (void);

/* links into scp */

void (*sim_vm_init)(void) = &sim_special_init;

/* SCP data structures

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		number of words needed for examine
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

char sim_name[] = "SWTP 6800";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 16;

DEVICE *sim_devices[] = { &cpu_dev, &sio_dev, &ptp_dev, &ptr_dev, &dsk_dev, NULL };

const char *sim_stop_messages[] = {
	"Unknown error",
    "Unknown I/O Instruction",
	"HALT instruction",
    "Breakpoint",
    "Invalid Opcode",
	"Invalid Memory" };

static const char *opcode[] = {
"???", "NOP", "???", "???",				//0x00
"???", "???", "TAP", "TPA", 
"INX", "DEX", "CLV", "SEV",
"CLC", "SEC", "CLI", "SEI",
"SBA", "CBA", "???", "???",				//0x10
"???", "???", "TAB", "TBA",
"???", "DAA", "???", "ABA",
"???", "???", "???", "???",
"BRA", "???", "BHI", "BLS",				//0x20
"BCC", "BCS", "BNE", "BEQ",
"BVC", "BVS", "BPL", "BMI",
"BGE", "BLT", "BGT", "BLE", 
"TSX", "INS", "PULA", "PULB",			//0x30
"DES", "TXS", "PSHA", "PSHB",
"???", "RTS", "???", "RTI",
"???", "???", "WAI", "SWI",
"NEGA", "???", "???", "COMA",			//0x40 
"LSRA", "???", "RORA", "ASRA",
"ASLA", "ROLA", "DECA", "???",
"INCA", "TSTA", "???", "CLRA",
"NEGB", "???", "???", "COMB",			//0x50
"LSRB", "???", "RORB", "ASRB",
"ASLB", "ROLB", "DECB", "???",
"INCB", "TSTB", "???", "CLRB",
"NEG", "???", "???", "COM",				//0x60
"LSR", "???", "ROR", "ASR",
"ASL", "ROL", "DEC", "???",
"INC", "TST", "JMP", "CLR",
"NEG", "???", "???", "COM",				//0x70
"LSR", "???", "ROR", "ASR",
"ASL", "ROL", "DEC", "???",
"INC", "TST", "JMP", "CLR",
"SUBA", "CMPA", "SBCA", "???",		//0x80
"ANDA", "BITA", "LDAA", "???",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "BSR", "LDS", "???",
"SUBA", "CMPA", "SBCA", "???",		//0x90
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "???", "LDS", "STS",
"SUBA", "CMPA", "SBCA", "???",		//0xA0
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX X", "JSR X", "LDS X", "STS X",
"SUBA", "CMPA", "SBCA", "???",		//0xB0
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "JSR", "LDS", "STS",
"SUBB", "CMPB", "SBCB", "???",		//0xC0
"ANDB", "BITB", "LDAB", "???",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "???",
"SUBB", "CMPB", "SBCB", "???",		//0xD0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
"SUBB", "CMPB", "SBCB", "???",		//0xE0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
"SUBB", "CMPB", "SBCB", "???",		//0xF0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
 };

int32 oplen[256] = {
0,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,		//0x00
1,1,0,0,0,0,1,1,0,1,0,1,0,0,0,0,
2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
1,1,1,1,1,1,1,1,0,1,0,1,0,0,1,1,
1,0,0,1,1,0,1,1,1,1,1,0,1,1,0,1,		//0x40
1,0,0,1,1,0,1,1,1,1,1,0,1,1,0,1,
2,0,0,2,2,0,2,2,2,2,2,0,2,2,2,2,
3,0,0,3,3,0,3,3,3,3,3,0,3,3,3,3,
2,2,2,0,2,2,2,0,2,2,2,2,3,2,3,0,		//0x80
2,2,2,0,2,2,2,2,2,2,2,2,2,0,2,2,
2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,
3,3,3,0,3,3,3,3,3,3,3,3,3,3,3,3,
2,2,2,0,2,2,2,0,2,2,2,2,0,0,3,0,		//0xC0
2,2,2,0,2,2,2,2,2,2,2,2,0,0,2,2,
2,2,2,0,2,2,2,2,2,2,2,2,0,0,2,2,
3,3,3,0,3,3,3,3,3,3,3,3,0,0,3,3 };

/* This is the dumper/loader. This command uses the -h to signify a
	hex dump/load vice a binary one.  If no address is given to load, it 
	takes the address from the hex record or the current PC for binary.
*/

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
	int32 i, cnt = 0, addr = 0, start = 0x10000, end = 0, bytecnt, 
		cksum1, cksum, bytes[250];
	char buffer[256];

	sscanf(cptr," %x-%x", &start, &end);
	if (flag) {							// dump
		if (start == 0x10000)			// no address parameter
			return SCPE_2FARG;
		if (sim_switches & 0x80) {		// hex dump
			addr = start;
			while (addr <= end) {		// more records to write
				if ((addr + 16) <= end)	// how many bytes this record
					bytecnt = 16 + 3;
				else
					bytecnt = end - addr + 4;
				cksum = -1 - (bytecnt) - (addr >> 8) - (addr & 0xFF); //init cksum
				fprintf(fileref, "S1%02X%02X%02X", bytecnt, addr>>8, addr&0xFF); //header
				for (i=0; i<bytecnt-3; i++, addr++, cnt++) { // data
					fprintf(fileref, "%02X", M[addr]);
					cksum -= M[addr];
				}
				fprintf(fileref, "%02X\r\n", cksum & 0xff);	// eor
			}
			fprintf(fileref, "S9\r\n");	// eof
		} else {						// binary dump
			for (addr = start; addr <= end; addr++, cnt++) {
				putc(M[addr], fileref);
			}
		}
		printf ("%d Bytes dumped starting at %04X\n", cnt, start);
	} else {							// load
		if (sim_switches & 0x80) {		// hex load
			while ((fgets(buffer, 255, fileref)) != NULL) {
				if (buffer[0] != 'S')
					printf("Not a Motorola hex format file\n");
				else {
					if (buffer[0] == '0') // name record
						printf("Name record found and ignored\n");
					else if (buffer[1] == '1') { // another record
						sscanf(buffer+2,"%2x%4x", &bytecnt, &addr);
						if (start == 0x10000)
							start = addr;
						for (i=0; i < bytecnt-3; i++)
							sscanf(buffer+8+(2*i), "%2x", &bytes[i]);
						sscanf(buffer+8+(2*i), "%2x", &cksum1);
						cksum = -1 - (bytecnt) - (addr >> 8) - (addr & 0xFF); //init cksum
						for (i=0; i < bytecnt-3; i++)
							cksum -= bytes[i];
						cksum &= 0xFF;
						if (cksum != cksum1)
							printf("Checksum error\n");
						else {
							for (i=0; i < bytecnt-3; i++) {
								M[addr++] = bytes[i];
								cnt++;
							}
						}
					} else if (buffer[1] == '9')  // end of file
						printf("End of file\n");
				}
			}
		} else {						// binary load
			if (start == 0x10000)		// no starting address
				addr = saved_PC;
			else
				addr = start;
			start = addr;
			while ((i = getc (fileref)) != EOF) {
				M[addr] = i;
				addr++;
				cnt++;
			}
		}
		printf ("%d Bytes loaded starting at %04X\n", cnt, start);
	}
	return (SCPE_OK);
}

/* Symbolic output

   Inputs:
	*of   =	output stream
	addr	=	current PC
	*val	=	pointer to values
	*uptr	=	pointer to unit
	sw	=	switches
   Outputs:
	status	=	error code
*/

int32 fprint_sym (FILE *of, int32 addr, uint32 *val,
	UNIT *uptr, int32 sw)
{
	int32 i, inst, inst1;

	if (sw & SWMASK ('D')) {		// dump memory
		for (i=0; i<16; i++)
			fprintf(of, "%02X ", val[i]);
		fprintf(of, "  ");
		for (i=0; i<16; i++)
			if (isprint(val[i]))
				fprintf(of, "%c", val[i]);
			else
				fprintf(of, ".");
		return -15;
	} else if (sw & SWMASK ('M')) { 	// dump instruction mnemonic
		inst = val[0];
		if (!oplen[inst]) {				// invalid opcode
			fprintf(of, "%02X", inst);
			return 0;
		}
		inst1 = inst & 0xF0;
		fprintf (of, "%s", opcode[inst]); // mnemonic
		if (strlen(opcode[inst]) == 3)
			fprintf(of, " ");
		if (inst1 == 0x20 || inst == 0x8D) { // rel operand
			inst1 = val[1];
			if (val[1] & 0x80)
				inst1 |= 0xFF00;
			fprintf(of, " $%04X", (addr + inst1 + 2) & ADDRMASK);
		} else if (inst1 == 0x80 || inst1 == 0xC0) { // imm operand
			if ((inst & 0x0F) < 0x0C)
				fprintf(of, " #$%02X", val[1]);
			else
				fprintf(of, " #$%02X%02X", val[1], val[2]);
		} else if (inst1 == 0x60 || inst1 == 0xA0 || inst1 == 0xE0) // ind operand
			fprintf(of, " %d,X", val[1]);
		else if (inst1 == 0x70 || inst1 == 0xb0 || inst1 == 0xF0) // ext operand
			fprintf(of, " $%02X%02X", val[1], val[2]);
		return (-(oplen[inst] - 1));
	} else 
		return SCPE_ARG;
}

/* address output routine */

t_addr fprint_addr(FILE *of, DEVICE *dptr, t_addr addr)
{
	fprintf(of, "%04X", addr);
	return 0;
}

/* Symbolic input

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	*uptr	=	pointer to unit
	*val	=	pointer to output values
	sw	=	switches
   Outputs:
	status	=	error status
*/

int32 parse_sym (char *cptr, int32 addr, UNIT *uptr, uint32 *val, int32 sw)
{
	int32 cflag, i = 0, j, r;
	char gbuf[CBUFSIZE];

	cflag = (uptr == NULL) || (uptr == &cpu_unit);
	while (isspace (*cptr)) cptr++;				/* absorb spaces */
	if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
		if (cptr[0] == 0) 
			return SCPE_ARG;		/* must have 1 char */
		val[0] = (uint32) cptr[0];
		return SCPE_OK;  
	}
	if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
		if (cptr[0] == 0) 
			return SCPE_ARG;		/* must have 1 char */
		val[0] = ((uint32) cptr[0] << 8) + (uint32) cptr[1];
	return SCPE_OK;
	}

/* An instruction: get opcode (all characters until null, comma,
   or numeric (including spaces).
*/

	while (1) {
		if (*cptr == ',' || *cptr == '\0' ||
    		isdigit(*cptr))
         		break;
		gbuf[i] = toupper(*cptr);
		cptr++;
		i++;
	}

/* Allow for RST which has numeric as part of opcode */

	if (toupper(gbuf[0]) == 'R' &&
		toupper(gbuf[1]) == 'S' &&
		toupper(gbuf[2]) == 'T') {
		gbuf[i] = toupper(*cptr);
		cptr++;
		i++;
	}

/* Allow for 'MOV' which is only opcode that has comma in it. */

	if (toupper(gbuf[0]) == 'M' &&
		toupper(gbuf[1]) == 'O' &&
		toupper(gbuf[2]) == 'V') {
		gbuf[i] = toupper(*cptr);
		cptr++;
		i++;
		gbuf[i] = toupper(*cptr);
		cptr++;
		i++;
	}

/* kill trailing spaces if any */
	gbuf[i] = '\0';
	for (j = i - 1; gbuf[j] == ' '; j--) {
		gbuf[j] = '\0';
	}

/* find opcode in table */
	for (j = 0; j < 256; j++) {
		if (strcmp(gbuf, opcode[j]) == 0)
    		break;
	}
	if (j > 255)        /* not found */
		return SCPE_ARG;

	val[0] = j;         /* store opcode */
	if (oplen[j] < 2)   /* if 1-byter we are done */
		return SCPE_OK;
	if (*cptr == ',') cptr++;
	cptr = get_glyph(cptr, gbuf, 0);	/* get address */
	sscanf(gbuf, "%o", &r);
	if (oplen[j] == 2) {
		val[1] = r & 0xFF;
	    return (-1);
	}
	val[1] = r & 0xFF;
	val[2] = (r >> 8) & 0xFF;
	return (-2);
}

/* initialize optional interfaces */

void sim_special_init (void)
{
//	*sim_vm_fprint_addr = &fprint_addr;
}

