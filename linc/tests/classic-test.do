cd %~p0

# The tests check writing to the tape, so use a copy.
copy classic-test.linc clobbered.linc
attach tape0 clobbered.linc

echo CONTRL
load -e classic-test.linc block=0 start=0 length=400
break 34

# Special treatment for this test, because it's supposed to halt.
echo *** Test: 70 - HLTTST ***
load -e classic-test.linc block=1 start=400 length=400
assert 400==70
go 401
assert P==402
continue
assert P==34

deposit RSW 0300
deposit LSW 0700
deposit SSW 77
deposit SAM[0] 177
deposit XL[0]  1
deposit XL[1]  1
deposit XL[2]  1
deposit XL[3]  1
deposit XL[4]  1
deposit XL[5]  1
deposit XL[6]  1
deposit XL[7]  1
deposit XL[8]  1
deposit XL[9]  1
deposit XL[10] 1
deposit XL[11] 1

call test   1 SAETST 002
call test   2 BCLTST 003
call test   3 BSETST 004
call test   4 BCOTST 005
call test   5 ROTL1  006
call test   6 ROTL2  007
call test   7 ROTL3  010
call test  10 ROTL4  011
call test  11 ROTL5  012
call test  12 ROTR1  013
call test  13 ROTR2  014
call test  14 ROTR3  015
call test  15 ROTR4  016
call test  16 ROTR5  017
call test  17 CLRTST 020
call test  20 ADDONE 021
call test  21 COMT1  022
call test  22 SCRT1  023
call test  23 SCRT2  024
call test  24 SCRT3  025
call test  25 SCRT4  027
call test  26 ADDT1  031
call test  27 FADRT1 032
call test  30 FADRT2 033
call test  31 iBETA1 035
call test  32 iBETA2 036
call test  33 iBETA3 037
call test  34 iBETA4 040
call test  35 LDAT1  041
call test  36 STAT1  042
call test  37 ADMT1  043
call test  40 LAMT1  044
call test  41 MULT1  045
call test  42 SROT1  046
call test  43 SETT1  047
call test  44 SETT2  050
call test  45 XSKT1  051
call test  46 XSKT2  052
call test  47 AZET1  053
call test  50 APOT1  054
call test  51 LZET1  055
call test  52 HWCT1  056
call test  53 HWCT2  057
call test  54 HWCT3  060
call test  55 HWCT4  061
call test  56 HWCT5  062
call test  57 RANADD 063
call test  60 ATRT1  064
call test  61 IBZT1  065
call test  62 JMPUP  066
call test  63 JMPDWN 067
call test  64 TAPETS 070
call test  65 MTBTST 101
call test  66 DISTST 102
call test  67 DSCTST 103
call test 700 OVFT1  104
call test 701 ZTAT1  105
call test 702 ZCLR1  106
call test 703 ZCLRT2 107
deposit INTREQ 1
call test 704 ENIT1  110
call test  71 MISCTS 111

echo DIAGNOSTICS PASSED
quit

:test
echo *** Test: %1 - %2 ***
load -e classic-test.linc block=%3 start=400 length=400
assert 400==%1
deposit 21 1%3
go 401
assert P==34
return
