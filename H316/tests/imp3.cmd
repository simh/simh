;;  *** IMP NODE #3 SETUP ***

;   IMP #3 connects to IMP #2 and #4 both.  Modem line 1 on this end connects
; to line 1 on the IMP 2 end, and line 2 on this end connects to IMP 3 line 1.

; Set the simulator configuration ...
echo Creating standard configuration for IMP #3 ...
do impconfig.cmd
SET IMP NUM=3

; Load the IMP code ...
echo Loading IMP code ...
do impcode.cmd

; Start up the modem links!
echo Attaching modem links ...
SET MI2 ENABLED
ATTACH MI1 4431::4421
ATTACH MI2 4432::4441

; And we're done ..
echo Type GO to start ...
