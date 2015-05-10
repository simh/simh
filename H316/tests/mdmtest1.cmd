; TEST1 - send a test modem message

; Set up the configuration ...
RESET ALL
SET CPU 32K NOHSA DMA=0 DMC EXTINT=16
SET LPT DISABLED
SET MT DISABLED
SET CLK DISABLED
SET FHD DISABLED
SET DP DISABLED
SET IMP DISABLED
SET RTC DISABLED
SET WDT DISABLED
SET MI1 ENABLED
SET MI2 DISABLED
SET MI3 DISABLED
SET MI4 DISABLED
SET MI5 DISABLED
SET HI1 DISABLED
SET HI2 DISABLED
SET HI3 DISABLED
SET HI4 DISABLED

; Deposit the test message in memory at 000100..000107 ...
DEPOSIT ALL 0
DEPOSIT 100 100000
DEPOSIT 101 011111
DEPOSIT 102 122222
DEPOSIT 103 033333
DEPOSIT 104 144444
DEPOSIT 105 055555
DEPOSIT 106 166666
DEPOSIT 107 077777

; Store a little program to set up the DMC and do start modem output ..
DEPOSIT 32 100
DEPOSIT 33 107
DEPOSIT -m 10 OCP 0071
DEPOSIT -m 11 HLT
DEPOSIT P 10

; Tell the world ...
echo 
echo Here are the DMC pointers before sending -
ex 32:33
echo
echo And here is the data we're sending -
ex 100:107

; Away we go!
echo
echo Starting simulation ...
ATTACH MI1 4431::4432
go

; All done...
echo
echo Here are the DMC pointers after sending ...
ex 32:33


