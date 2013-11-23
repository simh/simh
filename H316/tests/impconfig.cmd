;; ***** GENERIC IMP CONFIGURATION *****

;  This simh command file sets up the H316 simulator configuration for a generic
; IMP node.  Note that it doesn't load any IMP code (the caller is expected to
; do that) and it doesn't define any IMP node specific settings (e.g. modem
; links, IMP address, etc).
;
;							RLA [4-Jun-13]
RESET ALL

; Define the CPU configuration ...
; NOTE - real IMPs only had 16K of memory!
SET CPU 16K NOHSA DMA=0 DMC EXTINT=16

; Disable all the devices an IMP doesn't have ...
SET LPT DISABLED
SET MT DISABLED
SET CLK DISABLED
SET FHD DISABLED
SET DP DISABLED

; Enable the IMP device but leave the station address undefined ...
SET IMP ENABLED
;;SET IMP NUM=1

; Enable the RTC to count at 50kHz (20us intervals) ...
SET RTC ENABLED
SET RTC INTERVAL=20
SET RTC QUANTUM=32

; Enable the WDT but don't ever time out (we have enough problems!)...
SET WDT ENABLED
SET WDT DELAY=0

; Enable only modem line 1 and disable all the rest ...
SET MI1 ENABLED
SET MI2 DISABLED
SET MI3 DISABLED
SET MI4 DISABLED
SET MI5 DISABLED

; Enable only one host interface and disable all the rest ...
SET HI1 ENABLED
SET HI2 DISABLED
SET HI3 DISABLED
SET HI4 DISABLED

; Just ignore I/Os to disconnected devices ...
DEPOSIT CPU STOP_DEV 0

; SS4 ON is required to run DDT!
DEPOSIT CPU SS4 1

; Set the TTY speed to realistic values (about 9600BPS in this case) ...
DEPOSIT TTY KTIME 1000
DEPOSIT TTY TTIME 1000

;   Don't know for sure what SS2 does, but it appears to have something to do
; with the IMP startup.  Leave it ON for now...
DEPOSIT CPU SS2 1

; All done ....
SET CPU HISTORY=65000
SET CONSOLE DEBUG=STDERR
SET WDT DEBUG=LIGHTS