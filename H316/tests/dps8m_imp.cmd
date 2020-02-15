;;  *** IMP NODE #2 SETUP ***

;  IMP #2 connects to IMP #2 via modem line 1 on both ends ...
;  Also, a host is connected to host line 1

; Set the simulator configuration ...
echo Creating standard configuration for IMP #2 ...
do impconfig.cmd
SET IMP NUM=2

; Load the IMP code ...
echo Loading IMP code ...
do impcode.cmd

; Port numbering scheme.
;  The host is 45xx
;  IMPs are 44xx
;
; port abcd 
; iom
;   a 4
;   b 4
;   c iom number
;   d line # 1-5 mi1-mi5  6-9 hi1-h15
; host
;   a 4
;   b 5
;   c host number
;   d 0

; Start up the modem links!
echo Attaching modem links ...
ATTACH MI1 4421::4431

; Start up the host links!
echo Attaching host links ...
ATTACH HI1 4426::4500
;set hi1 debug=warn;udp;io
;set mi1 debug=udp
; And we're done ..
;echo Type GO to start ...
Go

