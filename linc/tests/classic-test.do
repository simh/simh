# The tests check writing to the tape, so use a copy.
copy classic-test.linc clobbered.linc
attach tape0 clobbered.linc

echo CONTRL
load classic-test.linc block=10 start=0 length=400
break 34

# Special treatment for this test, because it's supposed to halt.
echo *** Test: 70 - HLTTST ***
load classic-test.linc block=11 start=400 length=400
assert 400==70
go 401
assert P==402
continue
assert P==34

call test   1 SAETST 012 002
call test   2 BCLTST 013 003
call test   3 BSETST 014 004
call test   4 BCOTST 015 005
call test   5 ROTL1  016 006
call test   6 ROTL2  017 007
call test   7 ROTL3  020 010
call test  10 ROTL4  021 011
call test  11 ROTL5  022 012
call test  12 ROTR1  023 013
call test  13 ROTR2  024 014
call test  14 ROTR3  025 015
call test  15 ROTR4  026 016
call test  16 ROTR5  027 017
call test  17 CLRTST 030 020
call test  20 ADDONE 031 021
call test  21 COMT1  032 022
call test  22 SCRT1  033 023
call test  23 SCRT2  034 024
call test  24 SCRT3  035 025
call test  25 SCRT4  037 027
call test  26 ADDT1  041 031
call test  27 FADRT1 042 032
call test  30 FADRT2 043 033
call test  31 iBETA1 045 035
call test  32 iBETA2 046 036
call test  33 iBETA3 047 037
call test  34 iBETA4 050 040
call test  35 LDAT1  051 041
call test  36 STAT1  052 042
call test  37 ADMT1  053 043
call test  40 LAMT1  054 044
call test  41 MULT1  055 045
call test  42 SROT1  056 046
call test  43 SETT1  057 047
call test  44 SETT2  060 050
call test  45 XSKT1  061 051
call test  46 XSKT2  062 052
call test  47 AZET1  063 053
call test  50 APOT1  064 054
call test  51 LZET1  065 055
call test  52 HWCT1  066 056
call test  53 HWCT2  067 057
call test  54 HWCT3  070 060
call test  55 HWCT4  071 061
call test  56 HWCT5  072 062
call test  57 RANADD 073 063
call test  60 ATRT1  074 064
call test  61 IBZT1  075 065
call test  62 JMPUP  076 066
call test  63 JMPDWN 077 067
call test  64 TAPETS 100 070
call test  65 MTBTST 111 101
call test  66 DISTST 112 102
call test  67 DSCTST 113 103
call test 700 OVFT1  114 104
call test 701 ZTAT1  115 105
call test 702 ZCLR1  116 106
deposit SAM[0] 177
call test 703 ZCLRT2 117 107
;call test 704 ENIT1  120 110
deposit RSW 0300
deposit LSW 0700
deposit SSW 77
deposit XL[0] 1
deposit XL[1] 1
deposit XL[2] 1
deposit XL[3] 1
deposit XL[4] 1
deposit XL[5] 1
deposit XL[6] 1
deposit XL[7] 1
deposit XL[8] 1
deposit XL[9] 1
deposit XL[10] 1
deposit XL[11] 1
call test  71 MISCTS 121 111

echo DIAGNOSTICS PASSED
quit

:test
echo *** Test: %1 - %2 ***
load classic-test.linc block=%3 start=400 length=400
assert 400==%1
deposit 21 1%4
go 401
assert P==34
return
