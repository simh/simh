$!
$! This procedure patches KA630.BIN (V1.3) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA630.BIN KA630_ORIG.BIN
!
! Test D - ROM checksum & TOY RAM
!
!   - ROM checksum needs to be updated to reflect changes below
!
REPLACE/WORD 0B888 = 0EAAA
0FAB3
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
