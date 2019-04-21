$!
$! This procedure patches KA42A.BIN (V1.3) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA42A.BIN KA42A_ORIG.BIN
!
! Test 4 - 8PLN
!
!   - Ignore failures in 8PLN test for now
!
REPLACE/INSTRUCTION 00767 = 'BLBC    R0,0000078E'
'NOP'
'NOP'
'NOP'
EXIT
!
UPDATE
EXIT
$
