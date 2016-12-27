$!
$! This procedure patches KA620.BIN (V1.1) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA620.BIN KA620_ORIG.BIN
!
! Test D - ROM checksum & TOY RAM
!
!   - ROM checksum needs to be updated to reflect changes below
!
REPLACE/WORD 0B888 = 0279F
0163D
EXIT
!
! Test 3 - Interrupt tests
!
!   - Need to skip console loopback test and memory parity test
!
REPLACE/INSTRUCTION 0547C = 'MOVAL   W^0000571A,B^04(R11)'
'BRW  05CCC'
EXIT
!
UPDATE
EXIT
$
