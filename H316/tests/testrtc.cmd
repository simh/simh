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
set env -a RTC_INTERVAL=RTC.INTERVAL
set env -a RTC_QUANTUM=RTC.QUANTUM
set env -a RTC_TPS=RTC.TPS
set env -a RTC_DELAY=(1000*(10*(65536/RTC_QUANTUM)))/RTC_TPS
set env -a RTC_DELAY_SECS=RTC_DELAY/1000
set env -a RTC_DELAY_MSECS=RTC_DELAY%1000
echo The RTC runs at %RTC_TPS% ticks per second an increment of %RTC_QUANTUM%
echo on each tick.
echo The program should halt in approximately %RTC_DELAY_SECS%.%RTC_DELAY_MSECS% seconds ...
echo Starting at: %TIME%.%TIME_MSEC%
set env -a S_TIME_MSEC=((UTIME%1000000)*1000)+TIME_MSEC
GO
set env -a E_TIME_MSEC=((UTIME%1000000)*1000)+TIME_MSEC
echo Done at: %TIME%.%TIME_MSEC%
set env -a D_TIME_MSEC=E_TIME_MSEC-S_TIME_MSEC
set env -a ESECS=D_TIME_MSEC/1000
set env -a EMSECS=D_TIME_MSEC%1000
echo Elapsed Time: %ESECS%.%EMSECS% seconds
set env -a IPS=TICK_RATE_0*TICK_SIZE_0
echo Running at %IPS% instructions per second
