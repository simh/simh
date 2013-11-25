;;  *** IMP FIVE MODEM LINE LOOPBACK TEST ***

; Set the simulator configuration ...
echo IMP five modem line interface loopback test...
do impconfig.cmd
SET IMP NUM=2

; Load the IMP code ...
echo Loading IMP code ...
do impcode.cmd

; Start up the modem links!
SET MI1 ENABLED
DEPOSIT MI1 ILOOP 1
SET MI2 ENABLED
DEPOSIT MI2 ILOOP 1
SET MI3 ENABLED
DEPOSIT MI3 ILOOP 1
SET MI4 ENABLED
DEPOSIT MI4 ILOOP 1
SET MI5 ENABLED
DEPOSIT MI5 ILOOP 1

; And we're done ..
echo Type GO to start ...
