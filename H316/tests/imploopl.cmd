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
ATTACH MI1 4421:4431
SET MI1 LOOPLINE
SET MI2 ENABLED
ATTACH MI2 4422:4432
SET MI2 LOOPLINE
SET MI3 ENABLED
ATTACH MI3 4423:4433
SET MI3 LOOPLINE
SET MI4 ENABLED
ATTACH MI4 4424:4434
SET MI4 LOOPLINE
SET MI5 ENABLED
ATTACH MI5 4425:4435
SET MI5 LOOPLINE

; And we're done ..
echo Type GO to start ...
