;   This is a super simple simh script to test the IMP RTC and verify that it is
; incrementing at the correct 100us interval.  It simply waits for the clock
; count to overflow (which takes 65535 * 100us or about 6.5 seconds) and then
; repeats for a total of 10 iterations.  If all is well, this loop should take
; pretty close to 65 seconds to complete.  
;
;						RLA [15-Jun-13]
echo
echo SIMH IMP RTC INTERVAL CALIBRATION TEST

; Turn on the RTC (this requires extended interrupt support)
set cpu extint=16
set rtc enabled

; Turn the clock on (OCP 40 ==> CLKON) ...
d    1000 030040

; Loop reading the clock register until it becomes negative ...
d    1001 131040
d -m 1002 HLT
d -m 1003 SMI
d -m 1004 JMP 1001

; Loop reading the clock register until it becomes positive again ...
d    1005 131040
d -m 1006 HLT
d -m 1007 SPL
d -m 1010 JMP 1005

; And repeat the above for ten iterations ...
d -m 1011 IRS 1015
d -m 1012 JMP 1001
d -m 1013 HLT
d -m 1014 0
d -m 1015 177766

; That's it...
d p 1000
echo Start your stopwatch and at the same moment type "GO".
echo The program should halt in exactly 65 seconds ...


