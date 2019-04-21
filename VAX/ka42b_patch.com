$!
$! This procedure patches KA42B.BIN (V1.5) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA42B.BIN KA42B_ORIG.BIN
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
