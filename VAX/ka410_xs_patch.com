$!
$! This procedure patches KA410_XS.BIN (V1.3) Option ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA410_XS.BIN KA410_XS_ORIG.BIN
!
! Test 1 - NI
!
!   - Bypass network test
!
REPLACE/INSTRUCTION 00332 = 'BRB     00000384'
'NOP'
'NOP'
EXIT
!
UPDATE
EXIT
$
