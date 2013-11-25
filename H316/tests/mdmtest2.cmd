; TEST2 - receive a test modem message

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

; Clear the receiver buffer at 000100 ...
DEPOSIT ALL 0

; Store a little program to receive the message ...
DEPOSIT 20 100
DEPOSIT 21 177
DEPOSIT -m 10 OCP 0471
DEPOSIT -m 11 SKS 0271
DEPOSIT -m 12 JMP 11
DEPOSIT -m 13 HLT
DEPOSIT P 10

; Tell the world ...
echo 
echo Here are the DMC pointers before receiving -
ex 20:21

; and wait for "GO" ...
echo
echo Starting simulation ...
ATTACH MI1 4432::4431
go

; All done ...
echo
echo Here is the data we received -
ex 100:107
echo
echo And here are the DMC pointers after receiving -
ex 20:21
echo
