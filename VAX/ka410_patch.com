$!
$! This procedure patches KA410.BIN (V2.3) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA410.BIN KA410_ORIG.BIN
!
! Test B - MEM
!
!   - Memory parity is not implemented
!
REPLACE/INSTRUCTION 02C67 = 'CLRL    W^0168(R11)'
'BRW  00002D01'
'NOP'
EXIT
!
! Test A - MM
!
!   - Memory management fails selftest for an unknown reason
!
REPLACE/INSTRUCTION 0E488 = 'JMP     @#2004E61B'
'JMP  @#2004E600'
EXIT
!
! Test 8 - IT
!
!   - Interval timer fails selftest
!
REPLACE/INSTRUCTION 03252 = 'MOVZBL  #02,R0'
'MOVZBL  #01,R0'
EXIT
!
! Test 5 - SYS
!
!   - Ignore ROM checksum errors due to other changes
!
REPLACE/INSTRUCTION 0118A = 'MOVZWL  #0FFFE,R0'
'MOVL    #01,R0'
'NOP'
'NOP'
EXIT
!
! Test 4 - 4PLN
!
!   - Ignore failures in 4PLN test for now
!
REPLACE/INSTRUCTION 006BC = 'BLBC    R0,000006E3'
'NOP'
'NOP'
'NOP'
EXIT
!
UPDATE
EXIT
$
