;;  *** IMP LINE FOUR (ONLY!) LOOPBACK TEST ***

; Set the simulator configuration ...
echo IMP line four loopback test...
do impconfig.cmd
SET IMP NUM=2

; Load the IMP code ...
echo Loading IMP code ...
do impcode.cmd

; Start up the modem links!
SET MI1 DISABLED
SET MI2 DISABLED
SET MI3 DISABLED
SET MI4 ENABLED
DEPOSIT MI4 ILOOP 1
SET MI5 DISABLED

; And we're done ..
echo Type GO to start ...
