/*	altairz80_sys.c: MITS Altair system interface
		Written by Peter Schorn, 2001
		Based on work by Charles E Owen ((c) 1997 - Commercial use prohibited)
		Disassembler from Marat Fayzullin ((c) 1995, 1996, 1997 - Commercial use prohibited)
*/

#include <ctype.h>
#include "altairZ80_defs.h"

extern DEVICE cpu_dev;
extern DEVICE dsk_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern DEVICE sio_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern uint8 M[];
extern int32 saved_PC;

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int32 flag);
int32 fprint_sym (FILE *of, int32 addr, uint32 *val, UNIT *uptr, int32 sw);
int32 checkbase(char ch, char *numString);
int32 numok(char ch, char **numString, int32 minvalue, int32 maxvalue, int32 requireSign, int32 *result);
int32 match(char *pattern, char *input, char *xy, int32 *number, int32 *star, int32 *at, int32 *hat);
int32 parse_X80(char *cptr, int32 addr, uint32 *val, char *Mnemonics[]);
int32 parse_sym (char *cptr, int32 addr, UNIT *uptr, uint32 *val, int32 sw);
int32 DAsm(char *S, uint32 *val, int32 useZ80Mnemonics);

/* SCP data structures
	sim_name						simulator name string
	sim_PC							pointer to saved PC register descriptor
	sim_emax						number of words needed for examine
	sim_devices					array of pointers to simulated devices
	sim_stop_messages		array of pointers to stop messages
	sim_load						binary loader
*/

char		sim_name[]			= "Altair 8800 (Z80)";
REG			*sim_PC					= &cpu_reg[0];
int32		sim_emax				= 4;
DEVICE	*sim_devices[]	= { &cpu_dev, &sio_dev, &ptr_dev, &ptp_dev, &dsk_dev, NULL };

const char *sim_stop_messages[] = {
	"Unknown error",
	"Unknown I/O Instruction",
	"HALT instruction",
	"Breakpoint",
	"Invalid Opcode" };

static char *Mnemonics8080[] = {
/*0/8							1/9								2/A						3/B							4/C							5/D							6/E							7/F										*/
	"NOP",					"LXI B,#h",				"STAX B",			"INX B",				"INR B",				"DCR B",				"MVI B,*h",			"RLC",			/*	00-07	*/
	"DB 09h",				"DAD B",					"LDAX B",			"DCX B",				"INR C",				"DCR C",				"MVI C,*h",			"RRC",			/*	08-0f	*/
	"DB 10h",				"LXI D,#h",				"STAX D",			"INX D",				"INR D",				"DCR D",				"MVI D,*h",			"RAL",			/*	10-17	*/
	"DB 18h",				"DAD D",					"LDAX D",			"DCX D",				"INR E",				"DCR E",				"MVI E,*h",			"RAR",			/*	18-1f */
	"DB 20h",				"LXI H,#h",				"SHLD #h",		"INX H",				"INR H",				"DCR H",				"MVI H,*h",			"DAA",			/*	20-27 */
	"DB 28h",				"DAD H",					"LHLD #h",		"DCX H",				"INR L",				"DCR L",				"MVI L,*h",			"CMA",			/*	28-2f	*/
	"DB 30h",				"LXI SP,#h",			"STA #h",			"INX SP",				"INR M",				"DCR M",				"MVI M,*h",			"STC",			/*	30-37 */
	"DB 38h",				"DAD SP",					"LDA #h",			"DCX SP",				"INR A",				"DCR A",				"MVI A,*h",			"CMC",			/*	38-3f */
	"MOV B,B",			"MOV B,C",				"MOV B,D",		"MOV B,E",			"MOV B,H",			"MOV B,L",			"MOV B,M",			"MOV B,A",	/*	40-47 */
	"MOV C,B",			"MOV C,C",				"MOV C,D",		"MOV C,E",			"MOV C,H",			"MOV C,L",			"MOV C,M",			"MOV C,A",	/*	48-4f */
	"MOV D,B",			"MOV D,C",				"MOV D,D",		"MOV D,E",			"MOV D,H",			"MOV D,L",			"MOV D,M",			"MOV D,A",	/*	50-57 */
	"MOV E,B",			"MOV E,C",				"MOV E,D",		"MOV E,E",			"MOV E,H",			"MOV E,L",			"MOV E,M",			"MOV E,A",	/*	58-5f */
	"MOV H,B",			"MOV H,C",				"MOV H,D",		"MOV H,E",			"MOV H,H",			"MOV H,L",			"MOV H,M",			"MOV H,A",	/*	60-67 */
	"MOV L,B",			"MOV L,C",				"MOV L,D",		"MOV L,E",			"MOV L,H",			"MOV L,L",			"MOV L,M",			"MOV L,A",	/*	68-6f */
	"MOV M,B",			"MOV M,C",				"MOV M,D",		"MOV M,E",			"MOV M,H",			"MOV M,L",			"HLT",					"MOV M,A",	/*	70-77 */
	"MOV A,B",			"MOV A,C",				"MOV A,D",		"MOV A,E",			"MOV A,H",			"MOV A,L",			"MOV A,M",			"MOV A,A",	/*	78-7f */
	"ADD B",				"ADD C",					"ADD D",			"ADD E",				"ADD H",				"ADD L",				"ADD M",				"ADD A",		/*	80-87 */
	"ADC B",				"ADC C",					"ADC D",			"ADC E",				"ADC H",				"ADC L",				"ADC M",				"ADC A",		/*	88-8f */
	"SUB B",				"SUB C",					"SUB D",			"SUB E",				"SUB H",				"SUB L",				"SUB M",				"SUB A",		/*	90-97 */
	"SBB B",				"SBB C",					"SBB D",			"SBB E",				"SBB H",				"SBB L",				"SBB M",				"SBB A",		/*	98-9f */
	"ANA B",				"ANA C",					"ANA D",			"ANA E",				"ANA H",				"ANA L",				"ANA M",				"ANA A",		/*	a0-a7 */
	"XRA B",				"XRA C",					"XRA D",			"XRA E",				"XRA H",				"XRA L",				"XRA M",				"XRA A",		/*	a8-af */
	"ORA B",				"ORA C",					"ORA D",			"ORA E",				"ORA H",				"ORA L",				"ORA M",				"ORA A",		/*	b0-b7 */
	"CMP B",				"CMP C",					"CMP D",			"CMP E",				"CMP H",				"CMP L",				"CMP M",				"CMP A",		/*	b8-bf */
	"RNZ",					"POP B",					"JNZ #h",			"JMP #h",				"CNZ #h",				"PUSH B",				"ADI *h",				"RST 0",		/*	c0-c7 */
	"RZ",						"RET",						"JZ #h",			"DB CBh",				"CZ #h",				"CALL #h",			"ACI *h",				"RST 1",		/*	c8-cf */
	"RNC",					"POP D",					"JNC #h",			"OUT *h",				"CNC #h",				"PUSH D",				"SUI *h",				"RST 2",		/*	d0-d7 */
	"RC",						"DB D9h",					"JC #h",			"IN *h",				"CC #h",				"DB DDh",				"SBI *h",				"RST 3",		/*	d8-df */
	"RPO",					"POP H",					"JPO #h",			"XTHL",					"CPO #h",				"PUSH H",				"ANI *h",				"RST 4",		/*	e0-e7 */
	"RPE",					"PCHL",						"JPE #h",			"XCHG",					"CPE #h",				"DB EDh",				"XRI *h",				"RST 5",		/*	e8-ef */
	"RP",						"POP PSW",				"JP #h",			"DI",						"CP #h",				"PUSH PSW",			"ORI *h",				"RST 6",		/*	f0-f7 */
	"RM",						"SPHL",						"JM #h",			"EI",						"CM #h",				"DB FDh",				"CPI *h",				"RST 7"			/*	f8-ff */
 };

static char *MnemonicsZ80[256] =
{
	"NOP",					"LD BC,#h",			"LD (BC),A",		"INC BC",				"INC B",				"DEC B",				"LD B,*h",			"RLCA",	
	"EX AF,AF'",		"ADD HL,BC",		"LD A,(BC)",		"DEC BC",				"INC C",				"DEC C",				"LD C,*h",			"RRCA",	
	"DJNZ @h",			"LD DE,#h",			"LD (DE),A",		"INC DE",				"INC D",				"DEC D",				"LD D,*h",			"RLA",	
	"JR @h",				"ADD HL,DE",		"LD A,(DE)",		"DEC DE",				"INC E",				"DEC E",				"LD E,*h",			"RRA",	
	"JR NZ,@h",			"LD HL,#h",			"LD (#h),HL",		"INC HL",				"INC H",				"DEC H",				"LD H,*h",			"DAA",	
	"JR Z,@h",			"ADD HL,HL",		"LD HL,(#h)",		"DEC HL",				"INC L",				"DEC L",				"LD L,*h",			"CPL",	
	"JR NC,@h",			"LD SP,#h",			"LD (#h),A",		"INC SP",				"INC (HL)",			"DEC (HL)",			"LD (HL),*h",		"SCF",	
	"JR C,@h",			"ADD HL,SP",		"LD A,(#h)",		"DEC SP",				"INC A",				"DEC A",				"LD A,*h",			"CCF",	
	"LD B,B",				"LD B,C",				"LD B,D",				"LD B,E",				"LD B,H",				"LD B,L",				"LD B,(HL)",		"LD B,A",	
	"LD C,B",				"LD C,C",				"LD C,D",				"LD C,E",				"LD C,H",				"LD C,L",				"LD C,(HL)",		"LD C,A",	
	"LD D,B",				"LD D,C",				"LD D,D",				"LD D,E",				"LD D,H",				"LD D,L",				"LD D,(HL)",		"LD D,A",	
	"LD E,B",				"LD E,C",				"LD E,D",				"LD E,E",				"LD E,H",				"LD E,L",				"LD E,(HL)",		"LD E,A",	
	"LD H,B",				"LD H,C",				"LD H,D",				"LD H,E",				"LD H,H",				"LD H,L",				"LD H,(HL)",		"LD H,A",	
	"LD L,B",				"LD L,C",				"LD L,D",				"LD L,E",				"LD L,H",				"LD L,L",				"LD L,(HL)",		"LD L,A",	
	"LD (HL),B",		"LD (HL),C",		"LD (HL),D",		"LD (HL),E",		"LD (HL),H",		"LD (HL),L",		"HALT",					"LD (HL),A",	
	"LD A,B",				"LD A,C",				"LD A,D",				"LD A,E",				"LD A,H",				"LD A,L",				"LD A,(HL)",		"LD A,A",	
	"ADD B",				"ADD C",				"ADD D",				"ADD E",				"ADD H",				"ADD L",				"ADD (HL)",			"ADD A",	
	"ADC B",				"ADC C",				"ADC D",				"ADC E",				"ADC H",				"ADC L",				"ADC (HL)",			"ADC A",	
	"SUB B",				"SUB C",				"SUB D",				"SUB E",				"SUB H",				"SUB L",				"SUB (HL)",			"SUB A",	
	"SBC B",				"SBC C",				"SBC D",				"SBC E",				"SBC H",				"SBC L",				"SBC (HL)",			"SBC A",	
	"AND B",				"AND C",				"AND D",				"AND E",				"AND H",				"AND L",				"AND (HL)",			"AND A",	
	"XOR B",				"XOR C",				"XOR D",				"XOR E",				"XOR H",				"XOR L",				"XOR (HL)",			"XOR A",	
	"OR B",					"OR C",					"OR D",					"OR E",					"OR H",					"OR L",					"OR (HL)",			"OR A",	
	"CP B",					"CP C",					"CP D",					"CP E",					"CP H",					"CP L",					"CP (HL)",			"CP A",	
	"RET NZ",				"POP BC",				"JP NZ,#h",			"JP #h",				"CALL NZ,#h",		"PUSH BC",			"ADD *h",				"RST 00h",	
	"RET Z",				"RET",					"JP Z,#h",			"PFX_CB",				"CALL Z,#h",		"CALL #h",			"ADC *h",				"RST 08h",	
	"RET NC",				"POP DE",				"JP NC,#h",			"OUT (*h),A",		"CALL NC,#h",		"PUSH DE",			"SUB *h",				"RST 10h",	
	"RET C",				"EXX",					"JP C,#h",			"IN A,(*h)",		"CALL C,#h",		"PFX_DD",				"SBC *h",				"RST 18h",	
	"RET PO",				"POP HL",				"JP PO,#h",			"EX HL,(SP)",		"CALL PO,#h",		"PUSH HL",			"AND *h",				"RST 20h",	
	"RET PE",				"LD PC,HL",			"JP PE,#h",			"EX DE,HL",			"CALL PE,#h",		"PFX_ED",				"XOR *h",				"RST 28h",	
	"RET P",				"POP AF",				"JP P,#h",			"DI",						"CALL P,#h",		"PUSH AF",			"OR *h",				"RST 30h",
	"RET M",				"LD SP,HL",			"JP M,#h",			"EI",						"CALL M,#h",		"PFX_FD",				"CP *h",				"RST 38h"
};

static char *MnemonicsCB[256] =
{
	"RLC B",				"RLC C",				"RLC D",				"RLC E",				"RLC H",				"RLC L",				"RLC (HL)",				"RLC A",		
	"RRC B",				"RRC C",				"RRC D",				"RRC E",				"RRC H",				"RRC L",				"RRC (HL)",				"RRC A",		
	"RL B",					"RL C",					"RL D",					"RL E",					"RL H",					"RL L",					"RL (HL)",				"RL A",		
	"RR B",					"RR C",					"RR D",					"RR E",					"RR H",					"RR L",					"RR (HL)",				"RR A",		
	"SLA B",				"SLA C",				"SLA D",				"SLA E",				"SLA H",				"SLA L",				"SLA (HL)",				"SLA A",		
	"SRA B",				"SRA C",				"SRA D",				"SRA E",				"SRA H",				"SRA L",				"SRA (HL)",				"SRA A",		
	"SLL B",				"SLL C",				"SLL D",				"SLL E",				"SLL H",				"SLL L",				"SLL (HL)",				"SLL A",
	"SRL B",				"SRL C",				"SRL D",				"SRL E",				"SRL H",				"SRL L",				"SRL (HL)",				"SRL A",
	"BIT 0,B",			"BIT 0,C",			"BIT 0,D",			"BIT 0,E",			"BIT 0,H",			"BIT 0,L",			"BIT 0,(HL)",			"BIT 0,A",		
	"BIT 1,B",			"BIT 1,C",			"BIT 1,D",			"BIT 1,E",			"BIT 1,H",			"BIT 1,L",			"BIT 1,(HL)",			"BIT 1,A",		
	"BIT 2,B",			"BIT 2,C",			"BIT 2,D",			"BIT 2,E",			"BIT 2,H",			"BIT 2,L",			"BIT 2,(HL)",			"BIT 2,A",		
	"BIT 3,B",			"BIT 3,C",			"BIT 3,D",			"BIT 3,E",			"BIT 3,H",			"BIT 3,L",			"BIT 3,(HL)",			"BIT 3,A",		
	"BIT 4,B",			"BIT 4,C",			"BIT 4,D",			"BIT 4,E",			"BIT 4,H",			"BIT 4,L",			"BIT 4,(HL)",			"BIT 4,A",		
	"BIT 5,B",			"BIT 5,C",			"BIT 5,D",			"BIT 5,E",			"BIT 5,H",			"BIT 5,L",			"BIT 5,(HL)",			"BIT 5,A",		
	"BIT 6,B",			"BIT 6,C",			"BIT 6,D",			"BIT 6,E",			"BIT 6,H",			"BIT 6,L",			"BIT 6,(HL)",			"BIT 6,A",		
	"BIT 7,B",			"BIT 7,C",			"BIT 7,D",			"BIT 7,E",			"BIT 7,H",			"BIT 7,L",			"BIT 7,(HL)",			"BIT 7,A",		
	"RES 0,B",			"RES 0,C",			"RES 0,D",			"RES 0,E",			"RES 0,H",			"RES 0,L",			"RES 0,(HL)",			"RES 0,A",		
	"RES 1,B",			"RES 1,C",			"RES 1,D",			"RES 1,E",			"RES 1,H",			"RES 1,L",			"RES 1,(HL)",			"RES 1,A",		
	"RES 2,B",			"RES 2,C",			"RES 2,D",			"RES 2,E",			"RES 2,H",			"RES 2,L",			"RES 2,(HL)",			"RES 2,A",		
	"RES 3,B",			"RES 3,C",			"RES 3,D",			"RES 3,E",			"RES 3,H",			"RES 3,L",			"RES 3,(HL)",			"RES 3,A",		
	"RES 4,B",			"RES 4,C",			"RES 4,D",			"RES 4,E",			"RES 4,H",			"RES 4,L",			"RES 4,(HL)",			"RES 4,A",		
	"RES 5,B",			"RES 5,C",			"RES 5,D",			"RES 5,E",			"RES 5,H",			"RES 5,L",			"RES 5,(HL)",			"RES 5,A",		
	"RES 6,B",			"RES 6,C",			"RES 6,D",			"RES 6,E",			"RES 6,H",			"RES 6,L",			"RES 6,(HL)",			"RES 6,A",		
	"RES 7,B",			"RES 7,C",			"RES 7,D",			"RES 7,E",			"RES 7,H",			"RES 7,L",			"RES 7,(HL)",			"RES 7,A",		
	"SET 0,B",			"SET 0,C",			"SET 0,D",			"SET 0,E",			"SET 0,H",			"SET 0,L",			"SET 0,(HL)",			"SET 0,A",		
	"SET 1,B",			"SET 1,C",			"SET 1,D",			"SET 1,E",			"SET 1,H",			"SET 1,L",			"SET 1,(HL)",			"SET 1,A",		
	"SET 2,B",			"SET 2,C",			"SET 2,D",			"SET 2,E",			"SET 2,H",			"SET 2,L",			"SET 2,(HL)",			"SET 2,A",		
	"SET 3,B",			"SET 3,C",			"SET 3,D",			"SET 3,E",			"SET 3,H",			"SET 3,L",			"SET 3,(HL)",			"SET 3,A",		
	"SET 4,B",			"SET 4,C",			"SET 4,D",			"SET 4,E",			"SET 4,H",			"SET 4,L",			"SET 4,(HL)",			"SET 4,A",		
	"SET 5,B",			"SET 5,C",			"SET 5,D",			"SET 5,E",			"SET 5,H",			"SET 5,L",			"SET 5,(HL)",			"SET 5,A",		
	"SET 6,B",			"SET 6,C",			"SET 6,D",			"SET 6,E",			"SET 6,H",			"SET 6,L",			"SET 6,(HL)",			"SET 6,A",
	"SET 7,B",			"SET 7,C",			"SET 7,D",			"SET 7,E",			"SET 7,H",			"SET 7,L",			"SET 7,(HL)",			"SET 7,A"
};

static char *MnemonicsED[256] =
{
	"DB EDh,00h",		"DB EDh,01h",		"DB EDh,02h",		"DB EDh,03h",		"DB EDh,04h",		"DB EDh,05h",		"DB EDh,06h",			"DB EDh,07h",
	"DB EDh,08h",		"DB EDh,09h",		"DB EDh,0Ah",		"DB EDh,0Bh",		"DB EDh,0Ch",		"DB EDh,0Dh",		"DB EDh,0Eh",			"DB EDh,0Fh",
	"DB EDh,10h",		"DB EDh,11h",		"DB EDh,12h",		"DB EDh,13h",		"DB EDh,14h",		"DB EDh,15h",		"DB EDh,16h",			"DB EDh,17h",
	"DB EDh,18h",		"DB EDh,19h",		"DB EDh,1Ah",		"DB EDh,1Bh",		"DB EDh,1Ch",		"DB EDh,1Dh",		"DB EDh,1Eh",			"DB EDh,1Fh",
	"DB EDh,20h",		"DB EDh,21h",		"DB EDh,22h",		"DB EDh,23h",		"DB EDh,24h",		"DB EDh,25h",		"DB EDh,26h",			"DB EDh,27h",
	"DB EDh,28h",		"DB EDh,29h",		"DB EDh,2Ah",		"DB EDh,2Bh",		"DB EDh,2Ch",		"DB EDh,2Dh",		"DB EDh,2Eh",			"DB EDh,2Fh",
	"DB EDh,30h",		"DB EDh,31h",		"DB EDh,32h",		"DB EDh,33h",		"DB EDh,34h",		"DB EDh,35h",		"DB EDh,36h",			"DB EDh,37h",
	"DB EDh,38h",		"DB EDh,39h",		"DB EDh,3Ah",		"DB EDh,3Bh",		"DB EDh,3Ch",		"DB EDh,3Dh",		"DB EDh,3Eh",			"DB EDh,3Fh",
	"IN B,(C)",			"OUT (C),B",		"SBC HL,BC",		"LD (#h),BC",		"NEG",					"RETN",					"IM 0",						"LD I,A",
	"IN C,(C)",			"OUT (C),C",		"ADC HL,BC",		"LD BC,(#h)",		"DB EDh,4Ch",		"RETI",					"DB EDh,4Eh",			"LD R,A",
	"IN D,(C)",			"OUT (C),D",		"SBC HL,DE",		"LD (#h),DE",		"DB EDh,54h",		"DB EDh,55h",		"IM 1",						"LD A,I",
	"IN E,(C)",			"OUT (C),E",		"ADC HL,DE",		"LD DE,(#h)",		"DB EDh,5Ch",		"DB EDh,5Dh",		"IM 2",						"LD A,R",
	"IN H,(C)",			"OUT (C),H",		"SBC HL,HL",		"LD (#h),HL",		"DB EDh,64h",		"DB EDh,65h",		"DB EDh,66h",			"RRD",
	"IN L,(C)",			"OUT (C),L",		"ADC HL,HL",		"LD HL,(#h)",		"DB EDh,6Ch",		"DB EDh,6Dh",		"DB EDh,6Eh",			"RLD",
	"IN F,(C)",			"DB EDh,71h",		"SBC HL,SP",		"LD (#h),SP",		"DB EDh,74h",		"DB EDh,75h",		"DB EDh,76h",			"DB EDh,77h",
	"IN A,(C)",			"OUT (C),A",		"ADC HL,SP",		"LD SP,(#h)",		"DB EDh,7Ch",		"DB EDh,7Dh",		"DB EDh,7Eh",			"DB EDh,7Fh",
	"DB EDh,80h",		"DB EDh,81h",		"DB EDh,82h",		"DB EDh,83h",		"DB EDh,84h",		"DB EDh,85h",		"DB EDh,86h",			"DB EDh,87h",
	"DB EDh,88h",		"DB EDh,89h",		"DB EDh,8Ah",		"DB EDh,8Bh",		"DB EDh,8Ch",		"DB EDh,8Dh",		"DB EDh,8Eh",			"DB EDh,8Fh",
	"DB EDh,90h",		"DB EDh,91h",		"DB EDh,92h",		"DB EDh,93h",		"DB EDh,94h",		"DB EDh,95h",		"DB EDh,96h",			"DB EDh,97h",
	"DB EDh,98h",		"DB EDh,99h",		"DB EDh,9Ah",		"DB EDh,9Bh",		"DB EDh,9Ch",		"DB EDh,9Dh",		"DB EDh,9Eh",			"DB EDh,9Fh",
	"LDI",					"CPI",					"INI",					"OUTI",					"DB EDh,A4h",		"DB EDh,A5h",		"DB EDh,A6h",			"DB EDh,A7h",
	"LDD",					"CPD",					"IND",					"OUTD",					"DB EDh,ACh",		"DB EDh,ADh",		"DB EDh,AEh",			"DB EDh,AFh",
	"LDIR",					"CPIR",					"INIR",					"OTIR",					"DB EDh,B4h",		"DB EDh,B5h",		"DB EDh,B6h",			"DB EDh,B7h",
	"LDDR",					"CPDR",					"INDR",					"OTDR",					"DB EDh,BCh",		"DB EDh,BDh",		"DB EDh,BEh",			"DB EDh,BFh",
	"DB EDh,C0h",		"DB EDh,C1h",		"DB EDh,C2h",		"DB EDh,C3h",		"DB EDh,C4h",		"DB EDh,C5h",		"DB EDh,C6h",			"DB EDh,C7h",
	"DB EDh,C8h",		"DB EDh,C9h",		"DB EDh,CAh",		"DB EDh,CBh",		"DB EDh,CCh",		"DB EDh,CDh",		"DB EDh,CEh",			"DB EDh,CFh",
	"DB EDh,D0h",		"DB EDh,D1h",		"DB EDh,D2h",		"DB EDh,D3h",		"DB EDh,D4h",		"DB EDh,D5h",		"DB EDh,D6h",			"DB EDh,D7h",
	"DB EDh,D8h",		"DB EDh,D9h",		"DB EDh,DAh",		"DB EDh,DBh",		"DB EDh,DCh",		"DB EDh,DDh",		"DB EDh,DEh",			"DB EDh,DFh",
	"DB EDh,E0h",		"DB EDh,E1h",		"DB EDh,E2h",		"DB EDh,E3h",		"DB EDh,E4h",		"DB EDh,E5h",		"DB EDh,E6h",			"DB EDh,E7h",
	"DB EDh,E8h",		"DB EDh,E9h",		"DB EDh,EAh",		"DB EDh,EBh",		"DB EDh,ECh",		"DB EDh,EDh",		"DB EDh,EEh",			"DB EDh,EFh",
	"DB EDh,F0h",		"DB EDh,F1h",		"DB EDh,F2h",		"DB EDh,F3h",		"DB EDh,F4h",		"DB EDh,F5h",		"DB EDh,F6h",			"DB EDh,F7h",
	"DB EDh,F8h",		"DB EDh,F9h",		"DB EDh,FAh",		"DB EDh,FBh",		"DB EDh,FCh",		"DB EDh,FDh",		"DB EDh,FEh",			"DB EDh,FFh"
};

static char *MnemonicsXX[256] =
{
	"NOP",					"LD BC,#h",			"LD (BC),A",		"INC BC",				"INC B",				"DEC B",				"LD B,*h",				"RLCA",
	"EX AF,AF'",		"ADD I%,BC",		"LD A,(BC)",		"DEC BC",				"INC C",				"DEC C",				"LD C,*h",				"RRCA",
	"DJNZ @h",			"LD DE,#h",			"LD (DE),A",		"INC DE",				"INC D",				"DEC D",				"LD D,*h",				"RLA",
	"JR @h",				"ADD I%,DE",		"LD A,(DE)",		"DEC DE",				"INC E",				"DEC E",				"LD E,*h",				"RRA",
	"JR NZ,@h",			"LD I%,#h",			"LD (#h),I%",		"INC I%",				"INC I%h",			"DEC I%h",			"LD I%h,*h",			"DAA",
	"JR Z,@h",			"ADD I%,I%",		"LD I%,(#h)",		"DEC I%",				"INC I%l",			"DEC I%l",			"LD I%l,*h",			"CPL",
	"JR NC,@h",			"LD SP,#h",			"LD (#h),A",		"INC SP",				"INC (I%+^h)",	"DEC (I%+^h)",	"LD (I%+^h),*h",	"SCF",
	"JR C,@h",			"ADD I%,SP",		"LD A,(#h)",		"DEC SP",				"INC A",				"DEC A",				"LD A,*h",				"CCF",
	"LD B,B",				"LD B,C",				"LD B,D",				"LD B,E",				"LD B,I%h",			"LD B,I%l",			"LD B,(I%+^h)",		"LD B,A",
	"LD C,B",				"LD C,C",				"LD C,D",				"LD C,E",				"LD C,I%h",			"LD C,I%l",			"LD C,(I%+^h)",		"LD C,A",
	"LD D,B",				"LD D,C",				"LD D,D",				"LD D,E",				"LD D,I%h",			"LD D,I%l",			"LD D,(I%+^h)",		"LD D,A",
	"LD E,B",				"LD E,C",				"LD E,D",				"LD E,E",				"LD E,I%h",			"LD E,I%l",			"LD E,(I%+^h)",		"LD E,A",
	"LD I%h,B",			"LD I%h,C",			"LD I%h,D",			"LD I%h,E",			"LD I%h,I%h",		"LD I%h,I%l",		"LD H,(I%+^h)",		"LD I%h,A",
	"LD I%l,B",			"LD I%l,C",			"LD I%l,D",			"LD I%l,E",			"LD I%l,I%h",		"LD I%l,I%l",		"LD L,(I%+^h)",		"LD I%l,A",
	"LD (I%+^h),B",	"LD (I%+^h),C",	"LD (I%+^h),D",	"LD (I%+^h),E",	"LD (I%+^h),H",	"LD (I%+^h),L",	"HALT",						"LD (I%+^h),A",
	"LD A,B",				"LD A,C",				"LD A,D",				"LD A,E",				"LD A,I%h",			"LD A,I%l",			"LD A,(I%+^h)",		"LD A,A",
	"ADD B",				"ADD C",				"ADD D",				"ADD E",				"ADD I%h",			"ADD I%l",			"ADD (I%+^h)",		"ADD A",
	"ADC B",				"ADC C",				"ADC D",				"ADC E",				"ADC I%h",			"ADC I%l",			"ADC (I%+^h)",		"ADC,A",
	"SUB B",				"SUB C",				"SUB D",				"SUB E",				"SUB I%h",			"SUB I%l",			"SUB (I%+^h)",		"SUB A",
	"SBC B",				"SBC C",				"SBC D",				"SBC E",				"SBC I%h",			"SBC I%l",			"SBC (I%+^h)",		"SBC A",
	"AND B",				"AND C",				"AND D",				"AND E",				"AND I%h",			"AND I%l",			"AND (I%+^h)",		"AND A",
	"XOR B",				"XOR C",				"XOR D",				"XOR E",				"XOR I%h",			"XOR I%l",			"XOR (I%+^h)",		"XOR A",
	"OR B",					"OR C",					"OR D",					"OR E",					"OR I%h",				"OR I%l",				"OR (I%+^h)",			"OR A",
	"CP B",					"CP C",					"CP D",					"CP E",					"CP I%h",				"CP I%l",				"CP (I%+^h)",			"CP A",
	"RET NZ",				"POP BC",				"JP NZ,#h",			"JP #h",				"CALL NZ,#h",		"PUSH BC",			"ADD *h",					"RST 00h",
	"RET Z",				"RET",					"JP Z,#h",			"PFX_CB",				"CALL Z,#h",		"CALL #h",			"ADC *h",					"RST 08h",
	"RET NC",				"POP DE",				"JP NC,#h",			"OUT (*h),A",		"CALL NC,#h",		"PUSH DE",			"SUB *h",					"RST 10h",
	"RET C",				"EXX",					"JP C,#h",			"IN A,(*h)",		"CALL C,#h",		"PFX_DD",				"SBC *h",					"RST 18h",
	"RET PO",				"POP I%",				"JP PO,#h",			"EX I%,(SP)",		"CALL PO,#h",		"PUSH I%",			"AND *h",					"RST 20h",
	"RET PE",				"LD PC,I%",			"JP PE,#h",			"EX DE,I%",			"CALL PE,#h",		"PFX_ED",				"XOR *h",					"RST 28h",
	"RET P",				"POP AF",				"JP P,#h",			"DI",						"CALL P,#h",		"PUSH AF",			"OR *h",					"RST 30h",
	"RET M",				"LD SP,I%",			"JP M,#h",			"EI",						"CALL M,#h",		"PFX_FD",				"CP *h",					"RST 38h"
};

static char *MnemonicsXCB[256] =
{
	"RLC B",				"RLC C",				"RLC D",				"RLC E",				"RLC H",				"RLC L",				"RLC (I%@h)",			"RLC A",
	"RRC B",				"RRC C",				"RRC D",				"RRC E",				"RRC H",				"RRC L",				"RRC (I%@h)",			"RRC A",
	"RL B",					"RL C",					"RL D",					"RL E",					"RL H",					"RL L",					"RL (I%@h)",			"RL A",
	"RR B",					"RR C",					"RR D",					"RR E",					"RR H",					"RR L",					"RR (I%@h)",			"RR A",
	"SLA B",				"SLA C",				"SLA D",				"SLA E",				"SLA H",				"SLA L",				"SLA (I%@h)",			"SLA A",
	"SRA B",				"SRA C",				"SRA D",				"SRA E",				"SRA H",				"SRA L",				"SRA (I%@h)",			"SRA A",
	"SLL B",				"SLL C",				"SLL D",				"SLL E",				"SLL H",				"SLL L",				"SLL (I%@h)",			"SLL A",
	"SRL B",				"SRL C",				"SRL D",				"SRL E",				"SRL H",				"SRL L",				"SRL (I%@h)",			"SRL A",
	"BIT 0,B",			"BIT 0,C",			"BIT 0,D",			"BIT 0,E",			"BIT 0,H",			"BIT 0,L",			"BIT 0,(I%@h)",		"BIT 0,A",
	"BIT 1,B",			"BIT 1,C",			"BIT 1,D",			"BIT 1,E",			"BIT 1,H",			"BIT 1,L",			"BIT 1,(I%@h)",		"BIT 1,A",
	"BIT 2,B",			"BIT 2,C",			"BIT 2,D",			"BIT 2,E",			"BIT 2,H",			"BIT 2,L",			"BIT 2,(I%@h)",		"BIT 2,A",
	"BIT 3,B",			"BIT 3,C",			"BIT 3,D",			"BIT 3,E",			"BIT 3,H",			"BIT 3,L",			"BIT 3,(I%@h)",		"BIT 3,A",
	"BIT 4,B",			"BIT 4,C",			"BIT 4,D",			"BIT 4,E",			"BIT 4,H",			"BIT 4,L",			"BIT 4,(I%@h)",		"BIT 4,A",
	"BIT 5,B",			"BIT 5,C",			"BIT 5,D",			"BIT 5,E",			"BIT 5,H",			"BIT 5,L",			"BIT 5,(I%@h)",		"BIT 5,A",
	"BIT 6,B",			"BIT 6,C",			"BIT 6,D",			"BIT 6,E",			"BIT 6,H",			"BIT 6,L",			"BIT 6,(I%@h)",		"BIT 6,A",
	"BIT 7,B",			"BIT 7,C",			"BIT 7,D",			"BIT 7,E",			"BIT 7,H",			"BIT 7,L",			"BIT 7,(I%@h)",		"BIT 7,A",
	"RES 0,B",			"RES 0,C",			"RES 0,D",			"RES 0,E",			"RES 0,H",			"RES 0,L",			"RES 0,(I%@h)",		"RES 0,A",
	"RES 1,B",			"RES 1,C",			"RES 1,D",			"RES 1,E",			"RES 1,H",			"RES 1,L",			"RES 1,(I%@h)",		"RES 1,A",
	"RES 2,B",			"RES 2,C",			"RES 2,D",			"RES 2,E",			"RES 2,H",			"RES 2,L",			"RES 2,(I%@h)",		"RES 2,A",
	"RES 3,B",			"RES 3,C",			"RES 3,D",			"RES 3,E",			"RES 3,H",			"RES 3,L",			"RES 3,(I%@h)",		"RES 3,A",
	"RES 4,B",			"RES 4,C",			"RES 4,D",			"RES 4,E",			"RES 4,H",			"RES 4,L",			"RES 4,(I%@h)",		"RES 4,A",
	"RES 5,B",			"RES 5,C",			"RES 5,D",			"RES 5,E",			"RES 5,H",			"RES 5,L",			"RES 5,(I%@h)",		"RES 5,A",
	"RES 6,B",			"RES 6,C",			"RES 6,D",			"RES 6,E",			"RES 6,H",			"RES 6,L",			"RES 6,(I%@h)",		"RES 6,A",
	"RES 7,B",			"RES 7,C",			"RES 7,D",			"RES 7,E",			"RES 7,H",			"RES 7,L",			"RES 7,(I%@h)",		"RES 7,A",
	"SET 0,B",			"SET 0,C",			"SET 0,D",			"SET 0,E",			"SET 0,H",			"SET 0,L",			"SET 0,(I%@h)",		"SET 0,A",
	"SET 1,B",			"SET 1,C",			"SET 1,D",			"SET 1,E",			"SET 1,H",			"SET 1,L",			"SET 1,(I%@h)",		"SET 1,A",
	"SET 2,B",			"SET 2,C",			"SET 2,D",			"SET 2,E",			"SET 2,H",			"SET 2,L",			"SET 2,(I%@h)",		"SET 2,A",
	"SET 3,B",			"SET 3,C",			"SET 3,D",			"SET 3,E",			"SET 3,H",			"SET 3,L",			"SET 3,(I%@h)",		"SET 3,A",
	"SET 4,B",			"SET 4,C",			"SET 4,D",			"SET 4,E",			"SET 4,H",			"SET 4,L",			"SET 4,(I%@h)",		"SET 4,A",
	"SET 5,B",			"SET 5,C",			"SET 5,D",			"SET 5,E",			"SET 5,H",			"SET 5,L",			"SET 5,(I%@h)",		"SET 5,A",
	"SET 6,B",			"SET 6,C",			"SET 6,D",			"SET 6,E",			"SET 6,H",			"SET 6,L",			"SET 6,(I%@h)",		"SET 6,A",
	"SET 7,B",			"SET 7,C",			"SET 7,D",			"SET 7,E",			"SET 7,H",			"SET 7,L",			"SET 7,(I%@h)",		"SET 7,A"
};

/* Symbolic disassembler

	Inputs:
		*val						=	instructions to disassemble
		useZ80Mnemonics	=	> 0 iff Z80 mnemonics are to be used
	Outputs:
		*S							=	output text
		
	DAsm is Copyright (C) Marat Fayzullin 1995,1996,1997
		You are not allowed to distribute this software
		commercially.

*/

int32 DAsm(char *S, uint32 *val, int32 useZ80Mnemonics)
{
	char R[128], H[10], C= '\0', *T, *P;
	uint8 J = 0, Offset;
	uint16 B = 0;

	if (useZ80Mnemonics) {
		switch(val[B]) {
			case 0xCB:
				B++;
				T = MnemonicsCB[val[B++]];
				break;
			case 0xED:
				B++;
				T = MnemonicsED[val[B++]];
				break;
			case 0xDD:
			case 0xFD:
				C = (val[B] == 0xDD) ? 'X' : 'Y';
				B++;
				if (val[B] == 0xCB) {
					B++;
					Offset = val[B++];
					J = 1;
					T = MnemonicsXCB[val[B++]];
				}
				else {
					T = MnemonicsXX[val[B++]];
				}
				break;
			default:
				T = MnemonicsZ80[val[B++]];
		}
	}
	else {
		T = Mnemonics8080[val[B++]];
	}

	if (P = strchr(T, '^'))
	{
		strncpy(R, T, P - T);
		R[P - T] = '\0';
		sprintf(H, "%02X", val[B++]);
		strcat(R, H);
		strcat(R, P + 1);
	}
	else {
		strcpy(R, T);
	}
	if (P = strchr(R, '%')) {
		*P = C;
		if (P = strchr(P + 1, '%')) *P = C;
	}

	if(P = strchr(R, '*')) {
		strncpy(S, R, P - R);
		S[P - R] = '\0';
		sprintf(H, "%02X", val[B++]);
		strcat(S, H);
		strcat(S, P + 1);
	}
	else if (P = strchr(R, '@')) {
		strncpy(S, R, P - R);
		S[P - R] = '\0';
		if(!J) {
			Offset = val[B++];
		}
		strcat(S, Offset & 0x80 ? "-" : "+");
		J = Offset & 0x80 ? 256 - Offset : Offset;
		sprintf(H, "%02X", J);
		strcat(S, H);
		strcat(S, P + 1);
	}
	else if (P = strchr(R, '#')) {
		strncpy(S, R, P - R);
		S[P - R] = '\0';
		sprintf(H, "%04X", val[B] + 256*val[B + 1]);
		strcat(S, H);
		strcat(S, P + 1);
		B += 2;
	}
	else {
		strcpy(S, R);
	}
	return(B);
}

/* Symbolic output

	Inputs:
		*of		=	output stream
		addr	=	current PC
		*val	=	pointer to values
		*uptr	=	pointer to unit
		sw		=	switches
	Outputs:
		status	=	error code
*/

int32 fprint_sym (FILE *of, int32 addr, uint32 *val, UNIT *uptr, int32 sw)
{
	char disasm[128];
	int32 ch = val[0] & 0x7f;
	if (sw & (SWMASK ('A') | SWMASK ('C'))) {
		fprintf(of, ((0x20 <= ch) && (ch < 0x7f)) ? "'%c'" : "%02x", ch);
		return SCPE_OK;
	}
	if (!(sw & SWMASK ('M'))) {
		return SCPE_ARG;
	}
	ch = DAsm(disasm, val, cpu_unit.flags & UNIT_CHIP);
	fprintf(of, "%s", disasm);
	return (1-ch);	/* need to return additional bytes */
}

/* numString checks determines the base of the number (ch, *numString) 
	and returns FALSE if the number is bad */
int32 checkbase(char ch, char *numString) {
	int32 decimal = (ch <= '9');
	if (toupper(ch) == 'H') {
		return FALSE;
	}
	while (isxdigit(ch = *numString++)) {
		if (ch > '9') {
			decimal = FALSE;
		}
	}
	return toupper(ch) == 'H' ? 16 : (decimal ? 10 : FALSE);
}

int32 numok(char ch, char **numString, int32 minvalue, int32 maxvalue, int32 requireSign, int32 *result) {
	int32 sign = 1, value = 0, base;
	if (requireSign) {
		if (ch == '+') {
			ch = *(*numString)++;
		}
		else if (ch == '-') {
			sign = -1;
			ch = *(*numString)++;
		}
		else {
			return FALSE;
		}
	}
	if (!(base = checkbase(ch, *numString))) {
		return FALSE;
	}
	while (isxdigit(ch)) {
		value = base * value + ((ch <= '9') ? (ch - '0') : (toupper(ch) - 'A' + 10));
		ch = *(*numString)++;
	}
	if (toupper(ch) != 'H') {
		(*numString)--;
	}
	*result = value*sign;
	return (minvalue <= value) && (value <= maxvalue);
}

int32 match(char *pattern, char *input, char *xy, int32 *number, int32 *star,
	int32 *at, int32 *hat) {
	char pat = *pattern++;
	char inp = *input++;
	while ((pat) && (inp)) {
		switch(pat) {
			case ',':
				if (inp == ' ') {
					inp = *input++;
					continue;
				}
			case ' ':
				if (inp != pat) {
					return FALSE;
				}
				pat = *pattern++;
				inp = *input++;
				while (inp == ' ') {
					inp = *input++;
				}
				continue;
				break;
			case '%':
				inp = toupper(inp);
				if ((inp == 'X') || (inp == 'Y')) {
					*xy = inp;
				}
				else {
					return FALSE;
				}
				break;
			case '#':
				if (numok(inp, &input, 0, 65535, FALSE, number)) {
					pattern++; /* skip h */
				}
				else {
					return FALSE;
				}
				break;
			case '*':
				if (numok(inp, &input, 0, 255, FALSE, star)) {
					pattern++; /* skip h */
				}
				else {
					return FALSE;
				}
				break;
			case '@':
				if (numok(inp, &input, -128, 65535, TRUE, at)) {
					pattern++; /* skip h */
				}
				else {
					return FALSE;
				}
				break;
			case '^':
				if (numok(inp, &input, 0, 255, FALSE, hat)) {
					pattern++; /* skip h */
				}
				else {
					return FALSE;
				}
				break;
			default:
				if (toupper(pat) != toupper(inp)) {
					return FALSE;
				}
		}
		pat = *pattern++;
		inp = *input++;
	}
	while (inp == ' ') {
		inp = *input++;
	}
	return (pat == 0) && (inp == 0);
}

inline int32 checkXY(char xy) {
	if (xy == 'X') {
		return 0xDD;
	}
	else if (xy == 'Y') {
		return 0xFD;
	}
	else {
		printf("X or Y expected.\n");
		return FALSE;
	}
}

int32 parse_X80(char *cptr, int32 addr, uint32 *val, char *Mnemonics[]) {
	char xy;
	int32 op, number, star, at, hat;
	for (op = 0; op < 256; op++) {
		number = star = at = -129;
		if (match(Mnemonics[op], cptr, &xy, &number, &star, &at, &hat)) {
			val[0] = op;
			if (number >= 0) {
				val[1] = (0xff) & number;
				val[2] = (0xff) & (number >> 8);
				return (-2);					/* two additional bytes returned */
			}
			else if (star >= 0) {
				val[1] = (0xff) & star;
				return (-1);					/* one additional byte returned */
			}
			else if (at > -129) {
				if (at > 127) {				/* assume absolute address is meant	*/
					at -= addr + 2;			/* translate to relative address		*/
				}
				if ((-128 <= at) && (at < 127)) {
					val[1] = (int8)(at);
					return (-1);
				}
				else {
					return SCPE_ARG;
				}
			}
			else {
				return SCPE_OK;
			}
		}
	}
	if (Mnemonics == Mnemonics8080) {
		return SCPE_ARG;
	}
	
	for (op = 0; op < 256; op++) {
		if (match(MnemonicsCB[op], cptr, &xy, &number, &star, &at, &hat)) {
			val[0] = 0xCB;
			val[1] = op;
			return (-1);						/* one additional byte returned */
		}
	}
		
	for (op = 0; op < 256; op++) {
		number = -1;
		if (match(MnemonicsED[op], cptr, &xy, &number, &star, &at, &hat)) {
			val[0] = 0xED;
			val[1] = op;
			if (number >= 0) {
				val[2] = (0xff) & number;
				val[3] = (0xff) & (number >> 8);
				return (-3);					/* three additional bytes returned */
			}
			else {
				return (-1);					/* one additional byte returned */
			}
		}
	}
	
	for (op = 0; op < 256; op++) {
		number = star = hat = -1;
		xy = ' ';
		if (match(MnemonicsXX[op], cptr, &xy, &number, &star, &at, &hat)) {
			if (!(val[0] = checkXY(xy))) {
				return SCPE_ARG;
			}
			val[1] = op;
			if (number >= 0) {
				val[2] = (0xff) & number;
				val[3] = (0xff) & (number >> 8);
				return (-3);					/* three additional bytes returned */
			}
			else if ((star >= 0) && (hat >= 0)) {
				val[2] = (0xff) & hat;
				val[3] = (0xff) & star;
				return (-3);					/* three additional bytes returned */
			}
			else if (star >= 0) {
				val[2] = (0xff) & star;
				return (-2);					/* two additional bytes returned */
			}
			else if (hat >= 0) {
				val[2] = (0xff) & hat;
				return (-2);					/* two additional bytes returned */
			}
			else {
				return (-1);					/* one additional byte returned */
			}
		}
	}
	
	for (op = 0; op < 256; op++) {
		at = -129;
		xy = ' ';
		if (match(MnemonicsXCB[op], cptr, &xy, &number, &star, &at, &hat)) {
			if (!(val[0] = checkXY(xy))) {
				return SCPE_ARG;
			}
			val[1] = 0xCB;
			if (at > -129) {
				val[2] = (int8) (at);
			}
			else {
				printf("Offset expected.\n");
				return SCPE_ARG;
			}
			val[3] = op;
			return (-3);					/* three additional bytes returned */
		}
	}
	return SCPE_ARG;
}


/* Symbolic input

	Inputs:
		*cptr	=	pointer to input string
		addr	=	current PC
		*uptr	=	pointer to unit
		*val	=	pointer to output values
		sw		=	switches
	Outputs:
		status	=	error status
*/
int32 parse_sym (char *cptr, int32 addr, UNIT *uptr, uint32 *val, int32 sw)
{
	while (isspace (*cptr)) cptr++;				/* absorb spaces */
	if ((sw & (SWMASK ('A') | SWMASK ('C'))) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
		if (cptr[0] == 0) {
			return SCPE_ARG;		/* must have 1 char */
		}
		val[0] = (uint32) cptr[0];
		return SCPE_OK;
	}
	return (cpu_unit.flags & UNIT_CHIP) ?
		parse_X80(cptr, addr, val, MnemonicsZ80) : parse_X80(cptr, addr, val, Mnemonics8080);
}


/* This is the binary loader. The input file is considered to be
	 a string of literal bytes with no format special format. The
	 load starts at the current value of the PC.
*/

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int32 flag)
{
	int32 i, addr = 0, cnt = 0;
	
	if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
	addr = saved_PC;
	while ((i = getc (fileref)) != EOF) {
		M[addr++] = i;
		cnt++;
	}						/* end while */
	printf ("%d Bytes loaded.\n", cnt);
	return (SCPE_OK);
}
