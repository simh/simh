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
SET MI1 LOOPINTERFACE
SET MI2 ENABLED
SET MI2 LOOPINTERFACE
SET MI3 ENABLED
SET MI3 LOOPINTERFACE
SET MI4 ENABLED
SET MI4 LOOPINTERFACE
SET MI5 ENABLED
SET MI5 LOOPINTERFACE

; And we're done ..
echo Type GO to start ...
