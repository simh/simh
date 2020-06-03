$!
$! This procedure patches the base KA655.BIN Boot ROM image to work under
$! the SIMH simulator
$
$! The second part of the patch adds support for Extended Memory in the 
$! simulator.  A simulated system can have up to 512MB of RAM.
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=cp$exe:KA655x.BIN cp$src:ka655_orig.BIN
! CVAX Bootstrap Notes
! 
! [2004c87e] - de$read_script
! [2004c916] - launch next test, r2 = test #, r4 = header, r5 = offset to main routine
! 
! Script BA
! ---------
! 
! Test 9D - Utility - ok
! Test 42 - Wait for interrupt - ok
! Test C6 - SSC register check - ok
! Test 60 [2004de37] - Serial line - requires diagnostic loopback mode and
! 	break detection - bypass
! 	2004de99/	brw 2004e0a7
Replace/Instruction 0DE99 = 'MOVB    #03,B^76(R9)'
'BRW     0000E0A7'
Exit
! Test 62 - QDSS disable - ok
! 
! Script BC
! ---------
! 
! 40. Test 91 - CQBIC init check - ok
! 39. Test 90 [2004d748] - CQBIC registers - ok
! 38. Test 33 [2004d54e] - CMCTL init check - ok
! 37. Test 32 [2004d5b0] - CMCTL registers - ok
! 36. Test 31 [200512d0] - CMCTL CSR setup to map memory - ok
! 35. Test 49 [20052a4b] - CMCTL FDM test - requires fast diagnostic mode
! 	and XMX counters - bypass
! 	20052a55:20052a58/	nop
Delete/Instruction 12A55 = 'BBC     #26,(R9),00012A5C'
! 34. Test 30 [20051909] - init map - ok
! 
! Script BD
! ---------
! 
! 33. Test 52 [2004e656] - prog timer 0 - ok
! 32. Test 52 [2004e656] - prog timer 1 - ok
!     Interval Timer Patch
!     In operational environments, Test 52 subtests 17 and 20 have been observed
!     to occasionally fail. Disable the timer portion of the interval timer tests.
! Subsequent changes to vax_sysdev.c and scp timer services should no longer 
! require this test to be disabled.  Occasionally this test still fails.
!	2004e7c1/	brw 2004e870
Replace/Instruction 0e7c1 = 'MOVB    #10,B^76(R9)'
'BRW	0000E870'
Exit
! 31. Test 53 [2004e918] - toy clock - ok
! 30. Test C1 [2004f8f1] - SSC RAM - ok
! 29. Test 34 [2004d4a0] - ROM - checksum off due to other patches - patch
! 	2004d52c:2004d52d/	nop
Delete/Instruction 0D52C = 'BNEQ    0000D531'		! 2004D52C
! 28. Test C5 [2004fc0e] - SSC registers - ok
! 27. Test 55 [2004ea8c] - interval timer - ok
! 26. Test 51 [2004e42d] - CFPA - ok
! 25. Test C7 [2004D3D3] - CBTCR<31:30> - ok
! 24. Test 46 [2004ef1a] - L1 cache diagnostic mode - bypass
! 	2004ef80/	brw 2004f47a
Replace/Instruction 0EF80 = 'MOVB    #06,B^76(R9)'
'BRW     0000F47A'		! 2004FE80
Exit
! 23. Test 35 [20050554] - L2 cache integrity test - bypass
! 	20050554/	brw 20050a48
Replace/Instruction 10554 = 'INSV    #00,#10,#02,(R9)'
'BRW     00010A48'		! 20050554
Exit
! 22. Test 43 [20050d65] - L1 with L2 test - bypass
! 	20050d65/	brw 20050fca
Replace/Instruction 10D65 = 'MOVAL   B^00010D65,W^0080(R9)'
'BRW     00010FCA'		! 20050D65
Exit
! 21. (Rerun of C2)
! 20. Test 4F [20051d4f] - memory data - bypass, run from ROM
! 	20055205/	0
Replace/Byte 15205 = 3
0					! 20055205
Exit
! 	20051d4f/	brw 2005163a
Replace/Instruction 11D4F = 'MOVAL   B^00011D4F,W^0080(R9)'
'BRW     0001163A'		! 20051D4F
Exit
! 19. Test 4E [20051edb] - memory byte write - ok, run from ROM
! 	2005521c/	0
Replace/Byte 1521C = 3
0					! 2005521C
Exit
! 18. Test 4D [20051ff3] - memory addressing - ok, run from ROM
! 	20055233/	0
Replace/Byte 15233 = 3
0					! 20055233
Exit
! 17. Test 4C [20052190] - ECC test - bypass, run from ROM
! 	2005524a/	0
Replace/Byte 1524A = 3
0					! 2005524A
Exit
! 	20052190/	brw 2005163a
Replace/Instruction 12190 = 'MOVAL   B^00012190,W^0080(R9)'
'BRW     0001163A'		! 20052190
Exit
! 16. Test 4B [2005264e] - masked writes with errors - bypass, run from ROM
! 	20055261/	0
Replace/Byte 15261 = 3
0					! 20055261
Exit
! 	2005264e/	brw 2005163a
Replace/Instruction 1264E = 'MOVAL   B^0001264E,W^0080(R9)'
'BRW     0001163A'		! 2005264E
Exit
! 15. Test 4A [20052823] - single bit correction - bypass, run from ROM
! 	20055278/	0
Replace/Byte 15278 = 3
0					! 20055278
Exit
! 	20052823/	brw 2005163a
Replace/Instruction 12823 = 'MOVAL   B^00012823,W^0080(R9)'
'BRW     0001163A'		! 20052823
Exit
! 14. Test 48 [20053062] - memory address shorts - bypass, run from ROM
! 	2005528f/	0
Replace/Byte 1528F = 3
0					! 2005528F
Exit
! 	20053062/	brw 2005163a
Replace/Instruction 13062 = 'MOVAL   B^00013062,W^0080(R9)'
'BRW     0001163A'		! 20053062
Exit
! 13. Test 47 [200536c3] - verify refresh - run from ROM
! 	200552aa/	0
Replace/Byte 152AA = 3
0					! 200552AA
Exit
! 12. Test 41 [] - count bad pages, relocate bit map
! 11. Test 44 [20050d34] - L1 with memory - bypass
! 	20050d34/	brw 20050fca
Replace/Instruction 10D34 = 'MOVAL   B^00010D34,W^0080(R9)'
'BRW     00010FCA'		! 20050D34
Exit
! 10. Test 36 [2004ffdf] - L2 with memory - bypass
! 	2004ffdf/	brw 20050428
Replace/Instruction 0FFDF = 'JSB     L^0000CEFD'
'BRW     00010428'		! 2004FFDF
Exit
! 9. Test 80 [2004d7de] - CQBIC memory - bypass last 2 subtests, run from ROM
! 	2004dbc0/	brw 2004dd8a
Replace/Instruction 0DBC0 = 'MOVB    #1B,B^76(R9)'
'BRW     0000DD8A'		! 2004DBC0
Exit
! 	200552f6/	0
Replace/Byte 152F6 = 3
0					! 200552F6
Exit
! 8. Test 54 [] - virtual mode - ok
! 7. Test 34 [] - ROM in virtual mode - see previous notes
! 6. Test C5 [] - SSC registers in virtual mode - ok
! 5. Test 45 [2004ec5d] - cache, memory, CQBIC - bypass
! 	2004ec5d/	brw 2004ee90
Replace/Instruction 0EC5D = 'BICL2   #03,B^28(R9)'
'BRW     0000EE90'		! 2004EC5D
Exit
! 4. Test 5A [2004eb5f] - CVAX CMCTL DAL drivers - ok
! 3. Test 41 [20051096] - reset board
!
! ===========================================================================
!
!
! All of the above patches were done against the base ROM image extracted
! from a genuine MicroVAX 3900.  These were all part of SIMH prior to 
! extended memory support.
!
! The Diagnostic State Variable DST$W_BITMAP_LENGTH, being 16 bits, can only
! describe a PFN Bitmap describing up to, but NOT including 256MB of RAM.  To
! get to 256MB and beyond, we must correctly determine a correct bitmap size.
! all of the Diagnostic state space is in use, either since it is already 
! defined, and the space beyond that is used for stack.  So, we change the
! references to DST$W_BITMAP_LENGTH to call a subroutine to properly determine
! the PFN BITMAP size.
!
! Most of the code which references DST$W_BITMAP_LENGTH are done from a 
! diagnostic test routine which may be relocated to RAM before execution.  
! The assumption of such relocating activity is that none of the relocated code 
! references any other instructions or data in the ROM image via any PC 
! relative addressing modes.  Given this requirement, each of the above 
! patches must be done with a JSB to an explicit ROM address.  As a 
! consequence, the patched instruction will generally be longer than the 
! instruction which is being replaced.  To cleanly affect this
! we must overwrite multiple instructions and incorporate the activities of
! each of the overwritten instructions into the target subroutine.
! Additionally, even without the relocation concerns, numerous places which 
! reference these extra routines may be beyond a PC relative displacement
! due to the size of the ROM.
!
! The KA655 ROM size is 128KB.  As it turns out, the ROM image is only using
! approximately 105,136 bytes of the total 131072 bytes.  We use this unused
! ROM space to store code which implements the required extra functionality
!
Define PATCH_BASE 	= 00019C00
Define P$END		= PATCH_BASE
Define CP$K_MEMSIZE 	= 20080148
Define CP$K_QBMBR	= 20080010
Define DST_BASE		= 20140758
Define CTX_BASE		= 201404B2
Define CTX$L_R2		= 8
Define CTX$A_GPTR	= 66
Define CTX$L_ERROR_COUNT = 54
Define CTX$L_ERROR_STATUS = 58
Define DST$W_BITMAP_LENGTH = 20
Define DST$A_BITMAP	= 1C
Define DST$A_BUSMAP	= 24
Define DST$W_BITMAP_CHECKSUM = 22
Define CP$CHECKSUM_RTN 	= 20041C6A
Define GUARD$S_GUARD_BLOCK = 12
Define GUARD$l_pc	= 0
Define GUARD$a_gptr	= 4
Define GUARD$w_rmask	= 8
Define GUARD$l_save_error_count = 0A
Define GUARD$l_save_error_status = 0E
!
! This routine determines the memory size of the current system.  This is
! done by referencing the CMCTL18 memory size register.  On an older simulator
! or one with less than 64MB of RAM configured, this register may not exist. 
! If it doesn't exist the machine check generated by the reference to the 
! non-existant register is caught, and the appropriate memory size is 
! determined from the existing PFN Bitmap size.
!
DEFINE GETMEMSIZE_R0 = P$End+1
Deposit/Instruction GETMEMSIZE_R0
'	pushr	#0			'
'	subl2	#guard$s_guard_block, sp'
'	movw	#0, B^guard$w_rmask (sp)'
'	movab	B^G$ERR, B^guard$l_pc (sp)'
'	movl	@#<ctx_base + ctx$a_gptr>, B^guard$a_gptr (sp)'
'	movl	@#<ctx_base + ctx$l_error_count>, B^guard$l_save_error_count (sp)'
'	movl	@#<ctx_base + ctx$l_error_status>, B^guard$l_save_error_status (sp)'
'	movl	sp, @#<ctx_base + ctx$a_gptr>'
'	brb	G$RD			'
'G$ERR:	movzwl	@#<DST_BASE+DST$W_BITMAP_LENGTH>,R0'
'	clrl	@#<ctx_base + ctx$l_error_count>'
'	clrl	@#<ctx_base + ctx$l_error_status>'
'	ashl	#^d12,r0,r0		'
'	brb	G$DON			'	
'G$RD:	movl	@#CP$K_MEMSIZE,R0	'
'G$DON:	movl	@#<ctx_base + ctx$a_gptr>, sp'
'	movl	B^guard$a_gptr (sp), @#<ctx_base + ctx$a_gptr>'
'	movl	B^guard$l_save_error_count (sp), @#<ctx_base + ctx$l_error_count>'
'	movl	B^guard$l_save_error_status (sp), @#<ctx_base + ctx$l_error_status>'
'	addl2	#guard$s_guard_block - 2, sp'
'	popr	(sp)+			'
'P$End:	rsb				'	
Exit
!
Define GETMAPENDADDR_R6 = P$End+1
Deposit/Instruction GETMAPENDADDR_R6
'	MOVZWL	@#<DST_BASE+DST$W_BITMAP_LENGTH>,R6'
'	BNEQ	X$1			'
'	MOVL	R0, -(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ashl	#-^D12,R0,R6		'
'	MOVL	(SP)+, R0		'
'X$1:	addl	@#<DST_BASE+DST$A_BITMAP>,R6'
'P$End:	rsb				'
Exit

! DE_QDSS_ANY [2004E2A8]		Uses R6 for BitMap Size
!	2004E390/ BSBW		GETMAPSIZE_R6
Replace/Instruction 0E390
'	MOVZWL  B^20(R9),R6		'
'	ADDL3   R6,B^1C(R9),R6		'
Exit
'	JSB	GETMAPENDADDR_R6	'
Exit
!
!
DEFINE GETMAPSIZEMAPADDR_STACK = P$End+1
Deposit/Instruction GETMAPSIZEMAPADDR_STACK 
'	PUSHL	@#<DST_BASE+DST$A_BITMAP>,'
'	MOVL	B^4(SP),-(SP)		'
'	MOVZWL	@#<DST_BASE+DST$W_BITMAP_LENGTH>,B^8(SP)'
'	BNEQ	X$2'
'	MOVL	R0, -(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D12,R0,B^0C(SP)	'
'	MOVL	(SP)+, R0		'
'X$2:	NOP				'
'P$END:	RSB'
Exit

! CP_FIND     [200419E8]		Uses (SP) for BitMap Size R1 Scratch
!	20041A16/ BSBW		GETMAPSIZE_STACK
Replace/Instruction 01A16
'	MOVZWL  B^20(R0),-(SP)		'
'	PUSHL   B^1C(R0)		'
Exit
'	JSB	GETMAPSIZEMAPADDR_STACK	'
Exit
!
! CP_SCAN     [200459D0]		Uses R3 for BitMap Size
DEFINE GETMBMEMSIZE_STACK = P$End+1
Deposit/Instruction GETMBMEMSIZE_STACK
'	MOVAB	L^0000AACF,-(SP)	'
'	MOVL	B^4(SP),-(SP)		'
'	MOVL	R0, -(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D20,R0,B^0C(SP)	'
'	MOVL	(SP)+, R0		'
'	RSB				'
'GETMAPSIZE_R3:	MOVZWL	@#<DST_BASE+DST$W_BITMAP_LENGTH>,R3'
'	BNEQ	X$3'
'	MOVL	R0, -(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D12,R0,R3		'
'	MOVL	(SP)+, R0		'
'X$3:	RSB'
'P$END:	NOP'
Exit
!	20045B05/ BSBW		GETMBMEMSIZE_STACK
Replace/Instruction 05B05
'	MOVL    R8,-(SP)		'
'	MOVAB   L^0000AACF,-(SP)	'
Exit
'	JSB	GETMBMEMSIZE_STACK	'
Exit
!	20045B80/ BSBW		GETMAPSIZE_R3
Replace/Instruction 05B80
'	MOVZWL  @#20140778,R3		'
Exit
'	JSB	GETMAPSIZE_R3		'
Exit
! DE_CQBIC_MEMORY    [2004D7B2]
DEFINE GETBITMAPPAGES_R5 = P$End+1
Deposit/Instruction GETBITMAPPAGES_R5 
'	MOVZWL	@#<DST_BASE+DST$W_BITMAP_LENGTH>,R5'
'	BNEQ	X$4			'
'	MOVL	R0,-(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D12,R0,R5		'
'	MOVL	(SP)+,R0		'
'X$4:	ASHL	#3,R5,R5		'
'	RSB				'
'GETBITMAPMEMSIZE_R3:	MOVZWL	@#<DST_BASE+DST$W_BITMAP_LENGTH>,R3'
'	BNEQ	X$5			'
'	MOVL	R0,-(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D12,R0,R3		'
'	MOVL	(SP)+,R0		'
'X$5:	ASHL	#^D12,R3,R3		'
'P$END:	RSB'
Exit
!	2004D8A5/ BSBW		GETMAPSIZE_R5
Replace/Instruction 0D8A5
'	MOVZWL  B^20(R9),R5		'
'	ASHL    #03,R5,R5		'
Exit
'	JSB	GETBITMAPPAGES_R5	'
Exit
!	2004DA41/ BSBW		GETMAPSIZE_R5
Replace/Instruction 0DA41
'	MOVZWL  B^20(R9),R5		'
'	ASHL    #03,R5,R5		'
Exit
'	JSB	GETBITMAPPAGES_R5	'
Exit
!	2004DA8C/ BSBW		GETMAPSIZE_R5
Replace/Instruction 0DA8C
'	MOVZWL  B^20(R9),R5		'
'	ASHL    #03,R5,R5		'
Exit
'	JSB	GETBITMAPPAGES_R5	'
Exit
! DE_CACHE_MEM_CQBIC	[2004EBF0]
!	2004ECD0/ BSBW		GETMAPSIZE_R3
Replace/Instruction 0ECD0
'	MOVZWL  B^20(R9),R3		'
'	ASHL    #0C,R3,R3		'
Exit
'	JSB	GETBITMAPMEMSIZE_R3	'
Exit
! CP_BOOTSTRAP
DEFINE GET_X_PFNMAP_SIZEADDR_STACK = P$End+1
Deposit/Instruction GET_X_PFNMAP_SIZEADDR_STACK
'	movl	B^dst$a_bitmap (r11), r2'
'	movzwl	B^dst$w_bitmap_length (r11), r1'
'	bneq	X$20 			' ! Zero Bitmap size means extended mem
'	ashl	#-^D12, @#CP$K_MEMSIZE, r1' !  Map Size = MEMSIZE/512/8
'X$10:	brw	X$70			' ! already fixed
'X$20:	cmpl	r1, #^D16384		' ! Original Map of 64MB?
'	blss	X$10			' ! Too Small For Extended
'	JSB	GETMEMSIZE_R0		'
'	ashl	#-^D12, R0, r1		' ! Map Size = MEMSIZE/512/8
'	cmpl	r1, #^D16384		'
'	beql	X$10			' ! Normal 64MB map
!;
!; First move the Console Scratch Area (16KB), and the Qbus Map (32KB)
!; to the end of physical memory.
!;
'	movl	@#CP$K_MEMSIZE, r1	' ! MEMSIZE
'	subl3	#^D48*^D1024, r1, r3	' ! New Destination Address
'	addl	#^D16384, r2		' ! Point at end of prior Map
'	clrl	r0			' ! Index
'X$63:	movb	(r2)[r0], (r3)[r0]	' ! Move the Console Scratch Pad and QBMRs
'	aoblss	#^D48*^D1024, r0, X$63	'
'	movab	W^4000(r3), B^DST$A_BUSMAP(r11)' ! Save Qbus Map Register Space
'	movab	W^4000(r3), @#CP$K_QBMBR' ! Save Qbus Map Register Space
!;
!; Fixup the boot device descriptor previously saved in the scratchpad RAM 
!;
'	subl3	#^D512, B^DST$A_BUSMAP (r11), r1'
'	movab	B^8(r1), B^4(r1)'
!;
!; Now we build a new bitmap, with all bits set except for the reserved
!; area containing the bitmap itself, and the console scratch area and
!; the Qbus Map.
!;
'	ashl	#-^D12, @#CP$K_MEMSIZE, r1' ! Map Size = MEMSIZE/512/8
'	subl3	r1, r3, r2		' ! Point at new Destination Address
'	movl	r2, B^dst$a_bitmap (r11)' ! Save Bitmap address
'	ashl	#-9, @#CP$K_MEMSIZE, r1 ' ! PFN count = MEMSIZE/512
'	ashl	#-^D12, @#CP$K_MEMSIZE, r0' ! Map Size = MEMSIZE/512/8
'	addl	#^D48*^D1024+^D511, r0	' ! Plus other reserved page size
'	ashl	#-9, r0, r0		'
'	subl	r0, r1			' ! Adjust for bitmap of reserved pages
'	clrl	r0			'
'	pushl	r1			' ! Save total Bit Count
'	ashl	#-5, r1, r1		' ! Convert limit to Longword Count
'X$632:	movl	#-1,(r2)[r0]		' ! Set bitmap entry for 32 pages
'	aoblss	r1, r0, X$632		'
'	ashl	#5, r0, r0		' ! Convert back to Bit Count
'	movl	(SP)+, r1		' ! Restore total Bit Count
'	cmpl	r0, r1'
'	bgeq	X$651'
'X$64:	bbss	r0, (r2), X$65		' ! Set page bitmap entry
'X$65:	aoblss	r1, r0, X$64		' ! Done ?
'X$651:	ashl	#-9, @#CP$K_MEMSIZE, r1 ' ! PFN count = MEMSIZE/512
'X$66:	bbcc	r0, (r2), X$67		' ! Clear reserved page bitmap entry
'X$67:	aoblss	r1, r0, X$66		' ! Done?
'	clrl	r0			' ! Zero Initial checksum value
'	ashl	#-^D12, @#CP$K_MEMSIZE, r1' ! Map Size = MEMSIZE/512/8
'	jsb	@#cp$checksum_rtn	' ! Compute checksum for revised bitmap
'	movw	r0, B^dst$w_bitmap_checksum (r11)' ! save it
'	clrw	B^dst$w_bitmap_length (r11)' ! Mark as extended bitmap
'	ashl	#-^D12, @#CP$K_MEMSIZE, r1' ! Map Size = MEMSIZE/512/8
'	movl	B^dst$a_bitmap (r11), r2'
'X$70:	jmp	GETMAPSIZEMAPADDR_STACK'
!
'GETMAPSIZE_CTX_R2: movzwl	@#<dst_base+dst$w_bitmap_length>,@#<ctx_base+ctx$l_r2>'
'	bneq	X$71'
'	MOVL	R0, -(SP)		'
'	JSB	GETMEMSIZE_R0		'
'	ASHL	#-^D12,R0,@#<ctx_base+ctx$l_r2>'
'	MOVL	(SP)+, R0		'
'X$71:	rsb'
Exit
Replace/Instruction 517F = 'movzwl	B^20(r11), @#201404BA'
'	jsb	GETMAPSIZE_CTX_R2	'
Exit
Replace/Instruction 514B
'	MOVZWL  B^20(R11),-(SP)		'
'	PUSHL   B^1C(R11)		'
Exit
'	JSB	GET_X_PFNMAP_SIZEADDR_STACK'
Exit
!
! DE_MEMORY	[200512AC]
! CP_UTIL	[]
!
! Identify the Extended Memory Mode in the Console Banner
! (i.e. KA655X-B vs KA655-B)
!
Replace 83D8 = 00303436
58353536
Exit
Replace/Byte 83DC = 4B
0
Exit
Deposit/Instruction 1C04
'	PUSHAB  L^000083E2		'
'	JSB	GETMEMSIZE_R0		'
'	CMPL	R0, #<^D64*^D1024*^D1024>'
'	BLEQ	B$1			'
'	MOVAB	L^000083D6,(SP)		'
'B$1:	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
'	NOP				'
Exit
!
! Extended Memory Patches:
! 9. Test 80 [2004d7de] - CQBIC memory - bypass last 2 subtests, run from ROM
! MP Revised to bypass tests starting at test of NXM reference through MAP
! 	2004db2e/	brw 2004dd8a
Replace/Instruction 0db2e = 'MOVB    #17,B^76(R9)'
'BRW	0DD8A'
EXIT
!
UPDATE
EXIT
$
