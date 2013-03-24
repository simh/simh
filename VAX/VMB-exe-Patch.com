$!
$! This procedure patches the VMB.EXE provided with VAX/VMS V7.3 to
$! support the Boot Block boot functionality provided on all VAX
$! systems which run a ROM based VMB.
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=cp$exe:VMB.EXE cp$src:VMB_orig.EXE
! The native VMB.EXE program historically supported a Boot Block oriented
! boot if Bit 3 of the parameter register (R5) was set when VMB was invoked.
! This Boot Block boot operation reads sector 0 into memory and starts
! execution at offset 2 of the data block in memory.
! When portitions of VMB were migrated into ROM to support the earliest 
! MicroVAX system (MicroVAX I) and all subsequent ROM based VMB versions
! the concept of Boot Block booting was extended in these ROM VMB 
! implementations.  The change in boot block booting functionality included
! several features:
!   1) If a normal boot attempt to a device failed (due to VMB not being 
!      able to locate a secondary bootstrap program), a boot block boot is
!      automatically attempted.  If the Bit 3 of R5 was set, then the 
!      initial search for a secondary boot block was avoided and a boot 
!      block boot was immediately attempted.
!   2) When performing a boot block boot, the sector 0 contents are examined
!      and if these contents conform to the pattern defined for ROM based 
!      (PRA0) booting, the ROM format Offset, Size, and Starting address 
!      information is used directly by VMB to load a program into memory
!      and control is transferred to that program.  If the contents of 
!      sector 0 do not fit the pattern required for ROM based booting, then
!      the code in sector 0 is executed directly starting at offset 2,
!      the same as was originally done with the non ROM based VMB versions.
!      Note that this extended behavior allows sector 0 to contain very little 
!      information and quite possibly no actual boot code.
! Developers of alternate operating systems for VAX computers noticed the ROM
! based boot block behavior and changed their installation media AND the disk 
! structures to only provide the minimal boot information required on the 
! systems with VMB installed in ROM.
!
! Since, when this active development of these alternate operating systems for 
! VAX computers was happening, the vast majority of development and new system 
! deployments were to hardware which had ROM base VMB, no one noticed that
! older systems which booted with the non ROM based VMB could no longer boot
! from new install media or disks formatted with these operating systems.
!
! This patch addeds the ROM based VMB boot block boot functionality to the
! original dynamically loaded VMB.EXE used by the older systems to boot.
!
! The patch overwrites some VMB code which exists to support NVAX(1302) and 
! Neon-V(1701) systems.  If simh simulators for these systems are ever built
! an alternate location must be found to accomodate this extended logic
!
define PAA               = 08190             ! INIT_ADP_1302 (I think!)
define NEWFIL_OPNERR     = PAA+1F            ! Replacement to generate File Not Found Error Message
define TRY_BBLOCK        = PAA+2B
define OLDFIL_OPNERR     = 69A8              ! Original File Not Found Error
define ORIG_BBLOCK_BOOT  = 68B6              ! Original code that did Boot Block Boot
define RPB$V_BBLOCK      = 3
define RPB$L_BOOTR5      = 30
define READFILE          = 0120
define READIN_BOOT       = 6A37
define START_SECOND_HALT = 00BB
define SS$_FILESTRUCT    = 08C0
define SS$_BADCHKSUM     = 0808
define SS$_BADFILEHDR    = 0810
define SS$_BADDIRECTORY  = 0828
dep /long PAA    = 0
dep /long PAA+04 = 0
dep /long PAA+08 = 0
dep /long PAA+0C = 0
dep /long PAA+10 = 0
dep /long PAA+14 = 0
dep /long PAA+18 = 0
dep /long PAA+1C = 0
dep /long PAA+20 = 0
dep /long PAA+24 = 0
dep /long PAA+28 = 0
dep /long PAA+2C = 0
dep /long PAA+30 = 0
dep /long PAA+34 = 0
dep /long PAA+38 = 0
dep /long PAA+3C = 0
dep /long PAA+40 = 0
dep /long PAA+44 = 0
dep /long PAA+48 = 0
dep /long PAA+4C = 0
dep /long PAA+50 = 0
dep /long PAA+54 = 0
dep /long PAA+58 = 0
dep /long PAA+5C = 0
dep /long PAA+60 = 0
dep /long PAA+64 = 0
dep /long PAA+68 = 0
dep /long PAA+6C = 0
dep /long PAA+70 = 0
dep /long PAA+74 = 0
dep /long PAA+78 = 0
dep /long PAA+8C = 0
dep /long PAA+80 = 0
dep /long PAA+84 = 0
dep /long PAA+88 = 0
dep /long PAA+8C = 0
dep /long PAA+90 = 0
dep /long PAA+94 = 0
dep /long PAA+98 = 0
dep /long PAA+9C = 0
dep /ins OLDFIL_OPNERR    = '       BRW     PAA                   '
dep /ins PAA              = '       CMPW    R0, #SS$_BADCHKSUM    '
dep /ins PAA+05           = '       BEQL    TRY_BBLOCK            '
dep /ins PAA+07           = '       CMPW    R0, #SS$_FILESTRUCT   '
dep /ins PAA+0C           = '       BEQL    TRY_BBLOCK            '
dep /ins PAA+0E           = '       CMPW    R0, #SS$_BADFILEHDR   '
dep /ins PAA+13           = '       BEQL    TRY_BBLOCK            '
dep /ins PAA+15           = '       CMPW    R0, #SS$_BADDIRECTORY '
dep /ins PAA+1A           = '       BEQL    TRY_BBLOCK            '
dep /ins PAA+1C           = '       BRW     NEWFIL_OPNERR         '
dep /ins NEWFIL_OPNERR    = '       PUSHAB  L^000069AE            '
dep /ins NEWFIL_OPNERR+6  = '       JMP     L^00000294            '
dep /ins TRY_BBLOCK
'	CLRL	R8			' ! Block to read
'	MOVL	#1,R9			' ! Size to read
'	MOVL	R10,R6			' ! Start of free memory
'	JSB	L^READFILE		' ! Read the block to R10
'	BLBC	R0,TRY_OLD_BBLOCK	' ! Br if error
                                          !
                                          ! validate the boot block
                                          !
'	MOVZBL	B^2(R10),R2		' ! Get offset to secondary id field
'	CMPB	B^3(R10),#1		' ! Next field a BR instruction
'	BNEQ	TRY_OLD_BBLOCK      	' ! Br if no
'	MOVAW	(R10)[R2],R1		' ! Address next field
'	CMPW	(R1),#^x18		' ! VAX instruction set id?
'	BNEQ	TRY_OLD_BBLOCK  	' ! Br if no, error
'	ADDB3	#^x18,B^2(R1),R2	' ! Get optional value
'	MCOMB	R2,R2			' ! Ones's complement it
'	CMPB	R2,B^3(R1)		' ! Check the check sum byte
'	BNEQ	TRY_OLD_BBLOCK  	' ! Continue if no match
'	ADDL3	B^8(R1),B^0C(R1),R2	' ! Check other words
'	ADDL	B^10(R1),R2		' ! Get augment to load address
'	CMPL	R2,B^14(R1)		' ! Match?
'	BNEQ	TRY_OLD_BBLOCK  	' ! Br if no
'	ROTL	#10,B^4(R10),R8		' ! Get secondary image LBN
'	MOVL	B^8(R1),R9		' ! Get image size
'	ADDL	B^0C(R1),R10		' ! Compute load address
'	MOVL	B^10(R1),R5		' ! Compute transfer offset
'	BRW	READIN_BOOT		' ! Boot block is valid, read file 

'TRY_OLD_BBLOCK:BBS     #RPB$V_BBLOCK,B^RPB$L_BOOTR5(R11),OLD_BBLOCK_BOOT'
'	BRW     NEWFIL_OPNERR           ' ! Report error

'OLD_BBLOCK_BOOT:MOVL    #2,R5          ' ! Transfer address from start of block
'	JMP     L^START_SECOND_HALT     ' ! Do it.
exit
dep /ins ORIG_BBLOCK_BOOT = '       BRW     TRY_BBLOCK            ' ! make BOOT /R5:8 work the same way
update
exit
