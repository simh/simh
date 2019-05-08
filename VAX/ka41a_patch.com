$!
$! This procedure patches KA41A.BIN (V1.6) Boot ROM image to work under
$! the SIMH simulator
$!
$ PATCH /ABSOLUTE /NEW_VERSION /OUTPUT=KA41A.BIN KA41A_ORIG.BIN
!
! Test D - NVR
!
!   - This appears to be a bug in the ROM code, which
!     causes an endless loop if the NVR is not initialised.
!     The subroutine loops until a particular value is found
!     in the SIE register however the SIE is a constant and
!     never matches the expected value. There are no other
!     exit conditions from this loop. The KA41-D V1.0 ROM
!     has the same subroutine however the loop is exited
!     when the expected value is not found in the SIE.
!
REPLACE/INSTRUCTION 0284C = 'BNEQ    0000280E'
'BNEQ    0000285B'
EXIT
!
UPDATE
EXIT
$
