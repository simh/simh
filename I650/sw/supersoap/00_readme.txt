
Restoration comments May/2020
By Roberto Sancho

SuperSoap
From Computer History Museum archive 
SuperSoap manual: 102784983-05-01-acc.pdf and
SuperSoap Listing: 102784987-05-01-acc.pdf

After OCR'ing the listing (ssoap.txt), we generates two 1-word format decks 
using the assembler output for lines 1 to 1553 (locafier 3800-33). This is 
the main program that executes from drum (address 0000-1999) This deck allows 
to assemble a supersoap source prog that does not uses fancy pseudo-ops, and 
will be called ssoap_main.dck deck

To allow main deck to run standalone, the FIL pseudo op must be manually 
expanded to its values

with supersoap listing, lines 1554 up to the end another 1-word card is 
generated. These deck contains pseudo ops that are executed in core mem when 
loaded from ramac under the control of main deck. This deck will be 
called ssoap_core.dck deck.

To build supersoap in ramac, a loader routine is necessary. There is one in 
manual (p52-p53), but should be adapted to be assembled by ssoap main deck. 
Then a build program should be developed from scratch. This program is 
build_ssoap_ramac_src.txt and reads
   - load routine 1-word assembled deck
   - ssoap_main 1-word format deck
   - ssoap_core 1-word format deck
And saves it into ramac disc at proper place
   - load routine: at disk 38, track 00, words 00-40 (out of 60 available 
                                                      in track)
   - ssoap_main 1-word format deck (words 41-60 at track 00 disk 38, up to 
                                    track 33 in disk 38)
   - ssoap_core 1-word format deck (tracks 34 up to 66. the cdd pseudo op 
                                    indicates the track where the routine 
                                    should be stored)

Now supersoap is available in ramac, all the pseudo-ops can be used.

On testing supersoap, some bugs should be fixed:

  
    3196                SDS   9000                    9001   +85  9000  9017
                        RSB   1                       9017   +83  0001  9003
    3197                STL   9000                    9002   +20  9000  9003

      should be

    3196                SDS   9000                    9001   +85  9000  9017
                        RSB   1                       9017   +83  0001  9002
    3197                STL   9000                    9002   +20  9000  9003

    this bug was preventing PLR to work


    2577        9001    RAB   49    1F                9001   +82  0049  9001
      should be 
    2577F       9000    RAB   49    1F                9000   +82  0049  9001

    this bug was preventing DEK to work. 
     

Also, supersoap relies on some cpu opcode features that are not well 
documented in ibm manuals. These features should be implemented in order to 
make supersoap to work properly

    RDS/WDS sets IAS to 0 (not stated in ramac manual)
    index arithmetic sets distributor only when DA >= 8000 (not stated in 
        cpu extension manual)

Now it is possible to create from ssoap.txt supersoap OCR'd listing a source 
deck ssoap_example_src.txt 

Some changes should be applied: 
   - apply supersoap fixes for PLR and DEK
   - remove fil output 
   - remove pal output
   - comment line 8 (pseudo op hmo) as this will not be a hand optimized 
     source code

Now it is possible to assemble ssoap_example_src.txt with supersoap.
Note that the assembling does not generates same address as in ssoap.txt listing 
because it is not hand optimized

===============

Annotated execution of supersoap processing (assembling) the card

                                           1221rau      aa
                                               end
Produces this output

  1221    RAU           AA                       1221    60  0024  0029
          END


8001: 70 RD1 1999 1998 
 ... Read Card Unit CDR1                               1221rau      aa
 ... Read Card 1951: 0091929291+ ' 1221'  location
 ... Read Card 1952: 0000000000+ '     '  DA
 ... Read Card 1953: 6161000000+ 'AA   '  IA
 ... Read Card 1954: 7961840000+ 'RAU  '  op.co.de.TD.TI  TD=tag DA, TI=tag TI
 ... Read Card 1955: 0000000000+ '     '
 ... Read Card 1956: 0000000000+ '     '
 ... Read Card 1957: 0000001221+ '   ~~'
 ... Read Card 1958: 0000009999+ '   99'
 ... Read Card 1959: 0000009999+ '   99'
 ... Read Card 1960: 0000908000+ '  0~ '  control word: T000908000+, T=card type

1998: 60 RAU 1960 0015 symb:   start rau  1960              0127-start of processing the read card
0085: 20 STL 0046 0249 symb:   1     stl 533tl              0129-set 533tl=0
0249: 30 SRT 0009 0055 symb:         srt  9                 0130-AccUp=card type (=0)
0055: 80 RAA 8003 0077 symb:         raa  8003              0131-set IRA=Card Type=0
0077: 51 SXA 0005 0084 symb:         sxa  5                 0132-
0084: 41 BMA 2005 0000 symb:         bma  5   a  0          0133-depending on card type jmp to addr 0,1,2,3,4 (5..9 jmp to 0)

                                                                init processing type 0 card

0000: 60 RAU 1954 0009 symb:    0    rau  1954  1f          0134-AccUp=opcode 'RAU', OV=0
0009: 80 RAA 0000 0014 symb:   1     raa  0                 0141-Set IRA=0

                                                                process Tag for DA and IA

0014: 82 RAB 0001 0019 symb:         rab  1     0f          0142-Set IRB=1 -> process IA tag
0019: 30 SRT 0002 0075 symb:   0     srt  2                 0143-AccLo=TI 0000 0000 -> tag IA, Now ACC: 0079618400 0000000000+, OV: 0 (00.RR.AA.UU.TD  TI 0000 0000)
0075: 11 SUP 8003 0083 symb:         sup  8003              0144-clear AccUp
0083: 45 NZE 0086 0237 symb:         nze        9f          0145-jmp to 237 if NO tag, continue if tag
0237: 10 AUP 8001 0095 symb:   9     aup  8001  4f          0156-Load again AccUp=00.op.co.de.TD, Now ACC: 0079618400 0000000000+, OV: 0
0095: 69 LDD 0248 0051 symb:   4     lod ldi41  3f          0158-DIST=load code in IAS, jmp to 9006
0051: 23 SIA 4307 0061 symb:   3     sia dtaggb             0162-store in dtagg+IRB=0307+0001 the tag processed (Write 0308: 090780 0000+) tag is 4 last digits
0061: 42 NZB 0114 0065 symb:         nzb        1f          0163-jmp to 1f if IA&DA tags processed (for now, continue as IRB=1)
0114: 53 SXB 0001 0019 symb:         sxb  1     0b          0164-dec IRB, Now IRB=0 -> process IA tag
                                                                 loop again
0019: 30 SRT 0002 0075 symb:   0     srt  2                 0143-AccLo=TD 0000 0000 -> tag DA, Now ACC: 0000796184 0000000000+, OV: 0 (00.00.RR.AA.UU  TD 0000 0000)
0075: 11 SUP 8003 0083 symb:         sup  8003              0144-       
0083: 45 NZE 0086 0237 symb:         nze        9f          0145-       
0237: 10 AUP 8001 0095 symb:   9     aup  8001  4f          0156-       
0095: 69 LDD 0248 0051 symb:   4     lod ldi41  3f          0158-       
0051: 23 SIA 4307 0061 symb:   3     sia dtaggb             0162-store in dtagg+IRB=0307+0000 the tag processed (Write 0307: 090780 0000+) tag is 4 last digits
0061: 42 NZB 0114 0065 symb:         nzb        1f          0163-IRB=0 -> Both Tad in DA&IA processed, continue

                                                                end of tag processing, 
                                                                search for opcode, get instruction code NN

0065: 44 NZU 0229 0001 symb:   1     nzu         1          0165-Now AccUp=0000op.co.de. Jmp to 1 if opcode blank. Now ACC: 0000796184 0000000000+, OV: 0
0229: 30 SRT 0004 0238 symb:         srt  4                 0166-Now Acc=0..0.op  co.de, ACC: 0000000079 6184000000+, OV: 0 (0..0RR AAUU000000)
0238: 44 NZU 0043 0094 symb:         nzu        2f          0167-jmp to 2f if first char of opcode is blank (so not a mnemoci)
0043: 30 SRT 0002 0499 symb:         srt  2                 0168-Now ACC: 0000000000 7961840000+, OV: 0 (0..0  RR.AA.UU.00.00)
0499: 16 SLO 8002 0007 symb:         slo  8002              0169-Clear Acc, DIST=Opcode
0007: 24 STD 1711 0197 symb:         std otend              0170-Save as sentined at end of search table: Write 1711: 7961840000+
0197: 63 TLE 1550 0214 symb:         tle o0001              0171-Search opcode table
                                         ... Search DIST: 7961840000+ 'RAU  '
                                         ...  Found 1610: 7961840000+ 'RAU  '
                                         ... Result ACC: 0000000000 0016100000+, OV: 0 found at address 1610 (DA part of AccLo)
0214: 16 SLO 0017 0071 symb:         slo obase              0172-Substract table to get index on table (Acc=1610-1650=00 0040 0000-)
0071: 46 BMI 0227 0225 symb:         bmi 3f                 0173-jmp to 3f if >=0 -> below 1650 (most of pseudo instr, or not found). RAU is negative
0227: 15 ALO 0393 0548 symb:   3     alo n0052              0182-use index in opcode table to lookup in n0000 optimization table: n0052=00 0100 0588 + (00 0040 0000-) = 00 0060 0588
0548: 35 SLT 0004 0059 symb:         slt  4     machn       0183-set format, jmp to machine instr processing Now ACC: 0000000000 6005880000+, OV: 0 (60=opcode)

                                                                process the opcode intruction code NN get optimization L+NN 

0059: 20 STL 0314 0074 symb:   machn stl instr              0184-instr (addr 0314)=NN xxxx xxxx, NN=instr code. Now Write 0314: 6005880000+
0074: 10 AUP 0188 0245 symb:         aup optim              0185-optim = Read 0188: 0000000000+ this is a NXT NNMM card for hand optimization 
0245: 44 NZU 0100 0549 symb:         nzu 3f                 0186-optim=0, continue, Now ACC: 0000000000 6005880000+, OV: 0
0549: 35 SLT 0002 0105 symb:         slt  2                 0187-AccUp=instr code, Now ACC: 0000000060 0588000000+, OV: 0
0105: 88 RAC 8003 0013 symb:         rac  8003              0188-Set IRC=60 (=Instr code for RAU)       
0013: 60 RAU 6342 0012 symb:         rau n0001c             0189-Use instr code as index in n0001 table to get optimization for DA and IA
                                         n0001 (=342) + IRC (=60) = 0402  
                                         ... Read 0402: 0303050499+ this is the optimization code
                                         ... ACC: 0303050499 0000000000+, OV: 0
0012: 69 LDD 8005 0020 symb:         lod  8005              0190-DIST=IRA=0
0020: 24 STD 0073 0076 symb:         std lincr  procl       0191-lincr=0 = ???

                                        processing of instructions
                                        check location of instr is blank/non blank depending on prev instr

0076: 83 RSB 0001 0232 symb:   procl rsb  1                 0375-Set IRB=-1 (=processing location addr)       
0232: 21 STU 0188 0041 symb:         stu optim              0376-optim (addr 0188)=NN MM xx xxxx Optimize next word to L+NN if even, L+MM if odd (Now=0303050499+) optimization word
0041: 89 RSC 0275 0247 symb:         rsc  275               0377-IRC=-275 -> set return addr to to return to lin 0388- (addr 300+(-275)=25
0247: 65 RAL 1951 0056 symb:         ral  1951              0378-get soap Location from card read area = ' 1221' (Now ACC: 0000000000 0091929291+, OV: 0)
0056: 69 LDD 0460 0063 symb:         lod carry              0379-Read 0460: 8888888888+ is the previous assembled instr
0063: 45 NZE 0116 0070 symb:         nze        1f          0380-jmp to 1f if location is blank (Now ACC: 0000000000 0091929291+, OV: 0)
0116: 96 BD6 0069 0224 symb:         bd6 mastr  hlt77       0381-carry has 8-> no blank addr in prev instr -> jmp to mastr to resolve addr (if blank addr in prev instr error because references a next instr (this) that no blank location)

                                        master address calc
                                        for location 

                                        determine type of location

0069: 20 STL 0473 0676 symb:   mastr stl temp               0480-temp=location as stated in read card (= absolute addr ' 1221' here) ACC: 0000000000 0091929291+, OV: 0
0676: 69 LDD 8007 0632 symb:         lod  8007              0481-DIST=IRC= -275 -> to return to lin 0388-
0632: 24 STD 0334 0490 symb:         std 00032              0482-Save IRC (value to calc return addr from master calculation)
0490: 35 SLT 0002 1149 symb:         slt  2                 0483-AccUp=first char of location
1149: 27 SET 9000 0106 symb:         set  9000              0484-       
0106: 09 LDI 0117 0103 symb:         ldi q0001              0485-       
                                         ... Copy 0117-0149 to 9000-9032 (33 words)
0103: 09 LDI 0556 0535 symb:         ldi z0001              0486-       
                                         ... Copy 0556-0582 to 9033-9059 (27 words)
0535: 45 NZE 9002 0489 symb:         nze  9002              0487-jmp to 9002 if some location, continue if location is blank (ACC: 0000000000 9192929100+, OV: 0)
9002: 44 NZU 9007 9008 symb:    9002 nzu        1f          0493-jmp to 9008 if absolute (first char blank), continue if symbolic/relative/program point

                                        absolute location
                                        convert chars to number

9008: 11 SUP 8003 9010 symb:   1     sup  8003              0495-clear AccUp, AccLo=loc aboslute as chars (ACC: 0000000000 9192929100+, OV: 0 = '1221 '
9010: 80 RAA 8001 9011 symb:         raa  8001  1f          0496-IRA=0 -> addr type: 0=absolute addr 
9011: 11 SUP 8003 9012 symb:   1     sup  8003              0497-       
9012: 45 NZE 9013 9014 symb:         nze        1f          0498-jmp to 1f if addr is blank (conversion finished) (ACC: 0000000000 9192929100+, OV: 0)
9013: 24 STD 9006 9015 symb:         std  9006              0499-addr 9006=0 (this is the addr converted to numeric result)      
9015: 15 ALO 9016 9017 symb:         alo 100p               0500-add 100 -> remove leadin 9 if leftmost char of acclo (Now ACC: 0000000001 0192929100+, OV: 0)
9017: 44 NZU 9018 9019 symb:         nzu        2f          0501-jmp to 2f if char not number 9X
9018: 11 SUP 8003 9020 symb:         sup  8003              0502-clear accup, now ACC: 0000000000 0192929100+, OV: 0
9020: 35 SLT 0001 9021 symb:         slt  1                 0503-Now ACC: 0000000000 1929291000+, OV: 0
9021: 10 AUP 9006 9022 symb:         aup  9006              0504-load current result       
9022: 35 SLT 0001 9011 symb:         slt  1     1b          0505-Add digit, loop (Now ACC: 0000000001 9292910000+, OV: 0
                                     ... loop to lin 497
                                         
9014: 65 RAL 8001 9023 symb:   1     ral  8001              0506-Coversion finished in AccLo, Now  ACC: 0000000000 0000001221+, OV: 0
9023: 40 NZA 9024 9005 symb:         nza         9005       0507-jmp to 9005 if abs addr parsed (continue if regional addr), now IRA=0=absolute
9005: 47 BOV 1997 9000 symb:    9005 bov hlt88d  9000       0512-if OV there is an error

                                        check if resolved addr is in drum/ias

9000: 09 LDI 0303 9029 symb:    9000 ldi 00001   9029       0518-       
                                     ... Copy 0303-0349 to 9000-9046 (47 words)
9029: 88 RAC 9031 9018 symb:    9029 rac  9031   9018       0582-addr 9031 comes from addr 0334 = save value of IRC = -275 = Save IRC (value to calc return addr from master calculation)
9018: 20 STL 9050 9021 symb:    9018 stl  9050              0590-save absolute location addr Write 9050: 0000001221+
9021: 35 SLT 0006 9022 symb:         slt  6                 0591-Now ACC: 0000000000 1221000000+, OV: 0
9022: 16 SLO 8002 9023 symb:         slo  8002              0592-AccLo to DIST, Acc=0
9023: 84 TLU 9002 9024 symb:         tlu  9002              0593-Search in memory map at lines 0583-0589       
                                          ... Search DIST: 1221000000+ '~~   '
                                          ...  Found 9002: 1999000001- ')9  ~'
                                          ... Result ACC: 0000000000 0090020000+, OV: 0 -> AccLo = 00 ADDR 0000 addr of datafound
9024: 15 ALO 9025 8002 symb:         alo         8002       0594-AccLo=AccLo+80 0000 9026=80 9002 9026=RAA 9002 9026=
8002: 80 RAA 9002 9026                                           execute created inst: IRA=last 4 digits of found addr=0001-
9026: 41 BMA 9016 9012 symb:         bma  9016   9012       0596-jmp if IRA<0 same as found rlu word <0. <0 if aadr is in range 0000-1999 or 9000-9099
9016: 67 RAM 9404 9216 symb:    9016 ram  9004b  9016a      0597-IRB=-1 (=processing location addr), IRA=-1 (other options: =0 if < 8000, =1 if <8007, =0 if <9000, =-2 if < 9060, -3 if <= 9099, =0 <= 9999)
                                          9003   9015  (developed addr) ... Read 9003: 7999800000+, Now ACC: 0000000000 7999800000+, OV: 0
9015: 17 AML 9050 9027 symb:    9015 aml  9050  1f          0600-add saved location (saved at lin 590-): Read 9050: 0000001221+, Now ACC: 0000000000 7999801221+, OV: 0
9027: 69 LDD 8003 9028 symb:   1     lod  8003              0602-clear distrib       
9028: 23 SIA 9050 9001 symb:         sia  9050   9001       0603-Write 9050: 0000001221+
9001: 65 RAL 8001 6300 symb:    9001 ral  8001   300 c      0604-Acc=location, jmp to 300+IRC, here IRC=-275 -> jmp to addr 0025 lin 388- (processing the instruction)

                                            back to processing the instruction

0025: 20 STL 0780 2038 symb:    25   stl locus   38  a      0388-save location in locus, jmp to 26,27,28,39 depending on addr type in IRA (here IRA=-1=drum/ias), Write 0780: 0000001221+
0037: 10 AUP 8001 0545 symb:    37   aup  8001              0391-nOW ACC: 0000001221 0000001221+, OV: 0
0545: 15 ALO 0798 0320 symb:         alo ddiff  1f          0392-??? posible regional offset? posible tag offset? Now ddiff=0      
0320: 10 AUP 0073 0030 symb:   1     aup lincr              0397-??? Now lincr=0
0030: 21 STU 0190 0243 symb:         stu basex              0398-basex=instr location as 00 0000 NNNN
0243: 35 SLT 0006 0505 symb:         slt  6                 0399-       
0505: 20 STL 1961 0018 symb:         stl locat              0400-locat=instr location as NNNN 000000
0018: 65 RAL 9011 0226 symb:         ral  9011              0401-addr 9011 is copied from addr 0314=instr, set in line 0184-instr (addr 0314)=NN xxxx xxxx, NN=instr code. Now ACC: 0000000000 6005880000+, OV: 0
0226: 16 SLO 0235 0239 symb:         slo sudom              0402-??? sudom (addr 0235) = 88 0200 0950+ = instr RAC 200 950
0239: 45 NZE 0443 0948 symb:         nze procd  alfot       0403-jmp to alfot if instr = sudom 

                                        now process Data Address of instructuon  

0443: 89 RSC 0255 0649 symb:   procd rsc  255   dmast       0412-IRC=-255 -> set return addr to to return to lin 0415- (addr 300+(-255)=0045)
0649: 65 RAL 1952 0614 symb:   dmast ral  1952  dmst1       0413-Acc=Data Address in soap source, ACC: 0000000000 0000000000+, OV: 0
0614: 82 RAB 0000 0069 symb:   dmst1 rab  0     mastr       0414-IRB=0  (=processing DA data addr)

                                        mastr address calc
                                        for data addr (DA)

                                        determine type of location
  
0069: 20 STL 0473 0676 symb:   mastr stl temp               0480-temp=location as stated in read card (= blank addr '    ' here) ACC: 0000000000 0000000000+, OV: 0       
0676: 69 LDD 8007 0632 symb:         lod  8007              0481-DIST=IRC= -255 -> to return to lin 0415-       
0632: 24 STD 0334 0490 symb:         std 00032              0482-Save IRC (value to calc return addr from master calculation)
0490: 35 SLT 0002 1149 symb:         slt  2                 0483-AccUp=first char of location       
1149: 27 SET 9000 0106 symb:         set  9000              0484-       
0106: 09 LDI 0117 0103 symb:         ldi q0001              0485-       
                                         ... Copy 0117-0149 to 9000-9032 (33 words)
0103: 09 LDI 0556 0535 symb:         ldi z0001              0486-       
                                         ... Copy 0556-0582 to 9033-9059 (27 words)
0535: 45 NZE 9002 0489 symb:         nze  9002              0487-jmp to 9002 if some location, continue if location is blank (ACC: 0000000000 0000000000+, OV: 0)
0489: 49 BMC 1048 9005 symb:         bmc         9005       0488-IRC=-255 (processing DA), continue

                                         blank DA addr

1048: 65 RAL 0460 0665 symb:         ral carry              0489-Read 0460: 8888888888+ is the previous assembled instr
0665: 96 BD6 0068 9000 symb:         bd6         9000       0490-carry has 8-> no blank addr in prev instr -> continue to resolve addr
0068: 81 RSA 0053 9040 symb:         rsa  53     9040       0491-Set IRA: 0053- ???
9040: 36 SCT 0001 9001 symb:    9040 sct  1      9001       0535-Se overflow! Now ACC: 0000000008 8888888810+, OV: 1
9001: 09 LDI 0150 0202 symb:    9001 ldi 10001       d      0519-       
                                          ... Copy 0150-0199 to 9001-9050 (50 words)
0202: 69 LDD 4307 0112 symb:         lod dtaggb farbld      0520-read from dtagg+IRB=0307+0000 the tag processed (Read 0307: 090780 0000+) tag is 4 last digits

                                        optimization routines
                                        farbl = fix addr blank -> calc the addr for blank DA

0112: 96 BD6 9058 0221 symb:   farbl bd6  9058  7f          0640- ??? (check DIST: 0907800000+ = tag for DA)
9058: 88 RAC 9007 9057 symb:    9058 rac  9007              0553-IRC=0, addr 9007 copies from addr 0156 = modet defined at line 1454- modet +00 0000 0000, so IRC=modet=0
9057: 48 NZC 0607 0829 symb:         nzc farind  829        0554-IRC is 0 -> jmp to 829       
0829: 43 BMB 0682 9005 symb:    829  bmb         9005       0656-IRB is 0 (=processing DA) so jmp 9005       
9005: 65 RAL 9039 9015 symb:    9005 ral  9039              0668-Addr 9039 is copied from 0188=optim=optimization word, Now ACC: 0000000000 0303050499+, OV: 1
9015: 42 NZB 9016 9017 symb:         nzb        2f          0669-IRB is 0 (=processing DA), so continue 
9017: 20 STL 9058 9025 symb:   2     stl  9058              0689-Save optimization word in 9058
9025: 65 RAL 9041 9026 symb:         ral  9041              0690-Addr 9041 is copied from 0190=basex=instr location as 00 0000 NNNN, now ACC: 0000000000 0000001221+, OV: 1
9026: 14 DIV 9008 0832 symb:         div  9008       d      0691-Div basex by 2 (ACC: 0000000001 0000000610+, OV: 1
0832: 44 NZU 9027 9028 symb:         nzu        4f          0692-continue if basex odd, jmp to 4f if basex is even
9027: 67 RAM 9058 9029 symb:         ram  9058              0693-Acc=basex, now ACC: 0000000000 0303050499+, OV: 1   NN MM 000000 -> NN/MM is L+NN/MM fpr next word to select depending on L odd/even
9029: 35 SLT 0002 9030 symb:         slt  2                 0694-       
9030: 11 SUP 8003 9031 symb:         sup  8003  5f          0695-clear accup , ACC: 0000000000 0305049900+, OV: 1
9031: 30 SRT 0008 9032 symb:   5     srt  8                 0697-ACC: 0000000000 0000000003+, OV: 1 -> this is the optimization offset to be added to Llocation of instr
9032: 15 ALO 9041 9004 symb:         alo  9041   9004       0698-AccLo=basex+NN -> optimized word for blank address, now ACC: 0000000000 0000001224+, OV: 1

                                         get the addr to reserve 00..49 (first band)

9004: 14 DIV 9033 0491 symb:    9004 div 50i    farofd      0699-div addr by 50, Div result ACC: 0000000024 0000000024+, OV: 1 (AccLo=remainder)

                                         reserve addr in AccUp

0491: 65 RAL 8003 9049 symb:   farof ral  8003   9049       0700-Acc=24 = addr to reserve (the remainder of div by 50)
9049: 15 ALO 8002 9034 symb:    9049 alo  8002              0704-    
9034: 15 ALO 8002 9035 symb:         alo  8002              0705-Acc=96=24x4
9035: 69 LDD 8006 9036 symb:         lod  8006              0706-DIST=IRB=0 (=0 means processing DA)
9036: 24 STD 0257 0661 symb:         std 30007       d      0707-Save IRB=0 in 30007 (addr 0257)
0661: 82 RAB 0004 9613 symb:         rab  4      9013c      0708-Set IRB=4, jmp 9013 (IRC=0, set at line 0554 with value of modet)
9013: 88 RAC 0000 9037 symb:    9013 rac  0     1f          0709-IRC=0
9037: 16 SLO 9033 9038 symb:   1     slo 50i                0711-Acc=96-50=46
9038: 46 BMI 9050 9040 symb:         bmi  9050              0712-acc=46>0 -> continue
9040: 58 AXC 0050 9037 symb:         axc  50    1b          0713-IRC=IRC+50=50, jmp to 9037 to continue subtracting
9037: 16 SLO 9033 9038 symb:   1     slo 50i                0711-Acc=46-50-4       
9038: 46 BMI 9050 9040 symb:         bmi  9050              0712-exit loop       
9050: 09 LDI 0917 9051 symb:    9050 ldi 20001   9051       0714-Copy 0917-0926 to 9050-9059 (10 words)
9051: 35 SLT 0004 9050 symb:    9051 slt  4      9050       0730-Acc = -0004 0000
9050: 15 ALO 9053 9054 symb:    9050 alo 1f     2f          0731-Acc =  TLU 9052+Acc = TLU 9048 9058
9054: 20 STL 9055 9056 symb:   2     stl hld                0732-Store TLU instr at hld=9055
9056: 69 LDD 9052 9057 symb:         lod  9052              0733-DIST=1       
9057: 27 SET 9002 0496 symb:         set  9002       d      0734-       
0496: 09 LDI 7750 9055 symb:         ldi a0001c hld         0735-copy from 1750+IRC=1800 Copy 1800-1849 to 9002-9051 (50 words) = 8888888888
9055: 84 TLU 9048 9058 symb:    ... Search DIST: 0000000001+ '    ~'
                                ...  Found 9048: 8888888888+ 'YYYYY' -> is equiv to 1846, availabiolity for Add 0024
                                ... Result ACC: 0000000000 8490489058+, OV: 1
9058: 16 SLO 9053 9059 symb:   3     slo 1b                 0738-Acc=acc-xx9052xxxx=-00 0004 0000: slo TLU base addr to get index on table
9059: 46 BMI 9001 0525 symb:         bmi  9001       d      0739-jmp to 9001 if availability found (acc < 0)
9001: 09 LDI 0250 9001 symb:    9001 ldi 30000   9001       0723-Copy 0250-0299 to 9001-9050 (50 words)
9001: 30 SRT 0004 9016 symb:    9001 srt  4                 0752-Acc=-4
9016: 58 AXC 8002 9017 symb:         axc  8002              0753-IRC=50+Acc=50-4=46       
9017: 58 AXC 0050 9002 symb:         axc  50     9002       0754-IRC=IRC+50=56+50=96       
9002: 60 RAU 7750 9018 symb:    9002 rau a0001c             0755-Now Acc=(1750+IRC)=(1846)=8888888888 0000000000+, OV: 1
9018: 36 SCT 0000 9019 symb:         sct  0                 0756-Now Acc=8888888888 0000000000+, OV: 1
9019: 82 RAB 8002 9020 symb:         rab  8002              0757-IRB=0       
9020: 35 SLT 0001 9021 symb:         slt  1                 0758-Now Acc=8888888880 0000000000+, OV: 1
9021: 31 SRD 4001 9022 symb:         srd  1   b             0759-Now Acc=0888888888 0000000000+, OV: 1
9022: 21 STU 7750 9023 symb:         stu a0001c             0760-Store Avail data with addr reserved: Write 1846: 0888888888+ this reservation for addr 00024
9023: 60 RAU 8006 9024 symb:         rau  8006              0761-AccUp=IRB=0
9024: 19 MPY 9025 9026 symb:         mpy 50i                0762-Acc=0x50=0       
9026: 82 RAB 8002 9027 symb:         rab  8002              0763-IRB=Acc  -> IRB=IRB x 50
9027: 65 RAL 8007 0647 symb:         ral  8007       d      0764-Acc=IRC=96       
0647: 14 DIV 0750 0553 symb:         div 4i   d      d      0765-Acc=24
0553: 19 MPY 9028 0546 symb:         mpy 500i        d      0766-Acc=AccUpx500+AccLo=0x500+24=24 en AccUp
0546: 15 ALO 8003 9029 symb:         alo  8003              0767-Now Acc=0000000024 0000000024+, OV: 1
9029: 15 ALO 8006 9003 symb:         alo  8006   9003       0768-AccLo=AccLo+B = 24+0=24
9003: 82 RAB 9008 9005 symb:    9003 rab  9008   9005       0769-IRB=0       
9005: 47 BOV 9030 9004 symb:    9005 bov 1f      9004       0770-OV Set (set on line 0535- because addr blank)-> Branch Taken
9030: 43 BMB 9000 9050 symb:   1     bmb  9000              0796-IRB=0 (processing DA), continue        
9050: 69 LDD 1652 0655 symb:         lod o0103       d      0797-Read 1652: 6264980000+??? 0313- ALF BD8 THINK   1652  +62  6498  0000 
0655: 23 SIA 2513 9000 symb:         sia f0000a  9000       0798-Write 0460=carry= 626498 <0024+>: the IA part of carry is addr reserved

                                        check if resolved addr is in drum/ias

9000: 09 LDI 0303 9029 symb:    9000 ldi 00001   9029       0518-       
                                     ... Copy 0303-0349 to 9000-9046 (47 words)
9029: 88 RAC 9031 9018 symb:    9029 rac  9031   9018       0582-addr 9031 comes from addr 0334 = save value of IRC = -255 = Save IRC (value to calc return addr from master calculation)
9018: 20 STL 9050 9021 symb:    9018 stl  9050              0590-save absolute location Write 9050: 0000000024+
9021: 35 SLT 0006 9022 symb:         slt  6                 0591-Now ACC: 0024000000 0024000000+, OV: 0
9022: 16 SLO 8002 9023 symb:         slo  8002              0592-AccLo to DIST, Acc=0
9023: 84 TLU 9002 9024 symb:         tlu  9002              0593-Search in memory map at lines 0583-0589       
                                          ... Search DIST: 0024000000+ '~~   '
                                          ...  Found 9002: 1999000001- ')9  ~'
                                          ... Result ACC: 0024000000 0090020000+, OV: 0 -> AccLo = 00 ADDR 0000 addr of datafound
9024: 15 ALO 9025 8002 symb:         alo         8002       0594-AccLo=AccLo+80 0000 9026=80 9002 9026=RAA 9002 9026=
8002: 80 RAA 9002 9026                                           execute created inst: IRA=last 4 digits of found addr=0001- (-> is addr in drum/ias)
9026: 41 BMA 9016 9012 symb:         bma  9016   9012       0596-jmp if IRA<0 same as found rlu word <0. <0 if aadr is in range 0000-1999 or 9000-9099
9016: 67 RAM 9404 9216 symb:    9016 ram  9004b  9016a      0597-IRB=0 (=processing DA), IRA=-1 (other options: =0 if < 8000, =1 if <8007, =0 if <9000, =-2 if < 9060, -3 if <= 9099, =0 <= 9999)
                                          9004   9015  (developed addr) ... Read 9004: 0907800000+, Now ACC: 0000000000 0907800000+, OV: 0
9015: 17 AML 9050 9027 symb:    9015 aml  9050  1f          0600-add saved location (saved at lin 590-): Read 9050: 0000000024+, Now ACC: 0000000000 0907800024+, OV: 0
9027: 69 LDD 8003 9028 symb:   1     lod  8003              0602-clear distrib       
9028: 23 SIA 9050 9001 symb:         sia  9050   9001       0603-Write 9050: 0000000024+
9001: 65 RAL 8001 6300 symb:    9001 ral  8001   300 c      0604-Acc=DA addr=0000000024, jmp to 300+IRC, here IRC=-255 -> jmp to addr 0045 lin 415- (processing the instruction)

                                        back to processing the instruction

0045: 69 LDD 9011 0101 symb:    45   lod  9011              0415-addr 9011 is copied from addr 0314=instr, set in line 0184-instr (addr 0314)=NN xxxx xxxx, NN=instr code. Now DIST: 6005880000+
0101: 35 SLT 0004 0109 symb:         slt  4                 0416-Now ACC: 0000000000 0000240000+, OV: 0
0109: 22 SDA 0314 0022 symb:         sda instr              0417-Set DA part of instr: Write 0314: 6000240000+
0022: 30 SRT 0004 2034 symb:         srt  4      34  a      0418-IRA=-1 (da addr is in drum/ias). jmp to 33 if addr in drum/ias, jmp to 34 if addr < 8000, to 35 if <8007, to 34 if <9000, to 32 if < 9060, to 31 if <= 9099, to 34 if <= 9999. here, jmp to 33, ACC: 0000000000 0000000024+, OV: 0
0033: 69 LDD 0188 0091 symb:    33   lod optim  3f          0433-DIST=optim word=NN MM xxxxxx 8to optimize to L+NN/MM). Here Read 0188: 0303050499+
0091: 91 BD1 0244 0062 symb:   3     bd1 proci              0435-Last DIST digit=9 -> continue. jmp to proci If =8 (not an addr for opcode, just a value as number of shifts in SLT)
0062: 20 STL 0190 0244 symb:         stl basex  proci       0436-save in basex the addr for DA (Write 0190: 0000000024+)

                                        now process Instr Address of instructuon  

0244: 89 RSC 0256 0550 symb:   proci rsc  256   imast       0438-IRC=-256 -> set return addr to to return to lin 0441- (addr 300+(-256)=0044)
0550: 65 RAL 1953 0664 symb:   imast ral  1953  imst1       0439-Acc=inst Address in soap source, ACC: 0000000000 6161000000+, OV: 0
0664: 82 RAB 0001 0069 symb:   imst1 rab  1     mastr       0440-IRB=1  (=processing IA data addr)    

                                        master address calc
                                        for data addr (IA)

                                        determine type of location
  
0069: 20 STL 0473 0676 symb:   mastr stl temp               0480-temp=location as stated in read card (= symbolic addr 'AA   ' here) ACC: 0000000000 6161000000+, OV: 0              
0676: 69 LDD 8007 0632 symb:         lod  8007              0481-DIST=IRC= -256 -> to return to lin 0441-              
0632: 24 STD 0334 0490 symb:         std 00032              0482-Save IRC (value to calc return addr from master calculation)       
0490: 35 SLT 0002 1149 symb:         slt  2                 0483-AccUp=first char of location, now ACC: 0000000061 6100000000+, OV: 0
1149: 27 SET 9000 0106 symb:         set  9000              0484-       
0106: 09 LDI 0117 0103 symb:         ldi q0001              0485-       
                                         ... Copy 0117-0149 to 9000-9032 (33 words)
0103: 09 LDI 0556 0535 symb:         ldi z0001              0486-       
                                         ... Copy 0556-0582 to 9033-9059 (27 words)
0535: 45 NZE 9002 0489 symb:         nze  9002              0487-jmp to 9002 if IA set, continue if IA is blank. Here, jmp to 9002
9002: 44 NZU 9007 9008 symb:    9002 nzu        1f          0493-jmp to 1f if abs addr, continue if symbolic/regional/program point
9007: 15 ALO 9009 9008 symb:         alo 90i    1f          0494-ACC: 0000000061 6100000090+, OV: 0
9008: 11 SUP 8003 9010 symb:   1     sup  8003              0495-ACC: 0000000000 6100000090+, OV: 0
9010: 80 RAA 8001 9011 symb:         raa  8001  1f          0496-Set IRA with char1 of IA (here = 0061+)
9011: 11 SUP 8003 9012 symb:   1     sup  8003              0497-ACC: 0000000000 6100000090+, OV: 0 this is a posible regional addr
9012: 45 NZE 9013 9014 symb:         nze        1f          0498-jmp to 1f if absolute
9013: 24 STD 9006 9015 symb:         std  9006              0499-addr 9006=0 (this is the addr converted to numeric result)      
9015: 15 ALO 9016 9017 symb:         alo 100p               0500-AccLo=AccLo+1000000 = 6100000090+1000000000->ACC: 0000000000 7100000090+, OV: 0
9017: 44 NZU 9018 9019 symb:         nzu        2f          0501-continue if char2 is numeric -> regional addr, jmp to 2f y not regional
9019: 51 SXA 0090 9028 symb:   2     sxa  90                0513-IRA=61 (the char1 of IA)-90 (code for '0')=-29       
9028: 41 BMA 0620 9029 symb:         bma symbld             0514-if <0 -> char1 not numeric -> is symbolic (not prog point) -> jmp to symbl       

                                        symbolic addr
                                        search if symbol already defined

0620: 60 RAU 0473 0230 symb:   symbl rau temp   symb1       0559-Acc=' AA ' symbolic addr, ACC: 6161000000 0000000000+, OV: 0
0230: 35 SLT 0008 0547 symb:   symb1 slt  8                 0560-ACC: 0000000000 0000000000+, OV: 0
0547: 44 NZU 0203 0204 symb:         nzu 1f                 0561-jmp to 1f if symbol has 5 chars. here continue       
0204: 60 RAU 8001 0611 symb:         rau  8001              0562-reload symb ACC: 6161000000 0000000000+, OV: 0
0611: 10 AUP 0914 0769 symb:         aup 0000h              0563-Add 0 ???
0769: 11 SUP 8003 0540 symb:         sup  8003  2f          0564-Clear Acc, DIST=symbol
0540: 24 STD 1493 0196 symb:   2     std s0294              0566-Save as last Symbol to act as sentinel 
0196: 63 TLE 1200 0617 symb:         tle s0001              0567-       
                                         ... Search DIST: 6161000000+ 'AA   '
                                         ...  Found 1493: 6161000000+ 'AA   '
                                         ... Result ACC: 0000000000 0014930000+, OV: 0
0617: 16 SLO 0120 0475 symb:         slo q0004              0568-q0004 is last addr of symb table (defined at lin 0579- Q0004     00  S0294   0): Acc=Acc-00 1493 0000 -> Acc=0
0475: 47 BOV 0590 0530 symb:         bov equsy              0569- ???      
0530: 46 BMI 0233 0534 symb:         bmi 3f                 0570-if <0 -> symb found -> jmp to 3f
0534: 49 BMC 9001 0538 symb:         bmc  9001              0571-IRC=-0256 (= processing IA) -> jmp to 9001
9001: 09 LDI 0150 0202 symb:    9001 ldi 10001       d      0519-Symb not found -> must reserve an address and define symbol
                                         ... Copy 0150-0199 to 9001-9050 (50 words)
0202: 69 LDD 4307 0112 symb:         lod dtaggb farbld      0520-read from dtagg+IRB=0307+0001 the tag processed (Read 0308: 0907800000+) tag is 4 last digits

                                        optimization routines
                                        farbl = fix addr blank -> calc the addr for blank IA

0112: 96 BD6 9058 0221 symb:   farbl bd6  9058  7f          0640- ??? (check DIST: 0907800000+ = tag for DA)
9058: 88 RAC 9007 9057 symb:    9058 rac  9007              0553-IRC=0, addr 9007 copies from addr 0156 = modet defined at line 1454- modet +00 0000 0000, so IRC=modet=0       
9057: 48 NZC 0607 0829 symb:         nzc farind  829        0554-IRC is 0 -> jmp to 829       
0829: 43 BMB 0682 9005 symb:    829  bmb         9005       0656-IRB is 1 (=processing IA) so jmp 9005       
9005: 65 RAL 9039 9015 symb:    9005 ral  9039              0668-Addr 9039 is copied from 0188=optim=optimization word, Now ACC: 0000000000 0303050499+, OV: 1
9015: 42 NZB 9016 9017 symb:         nzb        2f          0669-IRB is 1 (=processing IA), so jmp to 2f
9016: 46 BMI 9018 9019 symb:         bmi 1f                 0670-??? depending on optimiz word (optimiz type?) ACC: 0000000000 0303050499+, OV: 0
9019: 92 BD2 0212 9020 symb:         bd2      d 7f          0671-Check DIST: 0303050499+ Digit is 9 -> jmp to 7f
9020: 35 SLT 0004 9017 symb:   7     slt  4     2f          0688-ACC: 0000000303 0504990000+, OV: 0
9017: 20 STL 9058 9025 symb:   2     stl  9058              0689-Save optimization word in 9058: Write 9058: 0504990000+
9025: 65 RAL 9041 9026 symb:         ral  9041              0690-Addr 9041 is copied from 0190=basex=DA location as 00 0000 NNNN, now ACC: 0000000000 0000000024+, OV: 0       
9026: 14 DIV 9008 0832 symb:         div  9008       d      0691-Div basex by 2 (ACC: 0000000000 0000000012+, OV: 0
0832: 44 NZU 9027 9028 symb:         nzu        4f          0692-continue if basex odd, jmp to 4f if basex is even       
9028: 67 RAM 9058 9031 symb:   4     ram  9058  5f          0696-Acc=basex, now ACC: 0000000000 0504990000+, OV: 0   NN MM 000000 -> NN/MM is L+NN/MM fpr next word to select depending on L odd/even       
9031: 30 SRT 0008 9032 symb:   5     srt  8                 0697-ACC: 0000000000 0000000005+, OV: 0 -> this is the optimization offset to be added to DA
9032: 15 ALO 9041 9004 symb:         alo  9041   9004       0698-AccLo=basex+NN=14+5=29 -> optimized word for symbolic address, now ACC: 0000000000 0000000029+, OV: 0

                                         get the addr to reserve 00..49 (first band)

9004: 14 DIV 9033 0491 symb:    9004 div 50i    farofd      0699-div addr by 50, Div result ACC: 0000000029 0000000000+, OV: 0 (AccLo=remainder)       

                                         reserve addr in AccUp

0491: 65 RAL 8003 9049 symb:   farof ral  8003   9049       0700-Acc=29 = addr to reserve (the remainder of div by 50)       
9049: 15 ALO 8002 9034 symb:    9049 alo  8002              0704-       
9034: 15 ALO 8002 9035 symb:         alo  8002              0705-Acc=116=29x4
9035: 69 LDD 8006 9036 symb:         lod  8006              0706-DIST=IRB=1 (=1 means processing IA)
9036: 24 STD 0257 0661 symb:         std 30007       d      0707-Save IRB=1 in 30007 (addr 0257)       
0661: 82 RAB 0004 9613 symb:         rab  4      9013c      0708-Set IRB=4, jmp 9013 (IRC=0, set at line 0554 with value of modet)       
9013: 88 RAC 0000 9037 symb:    9013 rac  0     1f          0709-IRC=0
9037: 16 SLO 9033 9038 symb:   1     slo 50i                0711-Acc=116-50=66
9038: 46 BMI 9050 9040 symb:         bmi  9050              0712-acc=66>0 -> continue       
9040: 58 AXC 0050 9037 symb:         axc  50    1b          0713-IRC=IRC+50=50, jmp to 9037 to continue subtracting       
9037: 16 SLO 9033 9038 symb:   1     slo 50i                0711-Acc=66-50=16         
9038: 46 BMI 9050 9040 symb:         bmi  9050              0712-loop again
9040: 58 AXC 0050 9037 symb:         axc  50    1b          0713-IRC=IRC+50=100, jmp to 9037 to continue subtracting       
9037: 16 SLO 9033 9038 symb:   1     slo 50i                0711-Acc=16-50=-34
9038: 46 BMI 9050 9040 symb:         bmi  9050              0712-exit loop       
9050: 09 LDI 0917 9051 symb:    9050 ldi 20001   9051       0714-Copy 0917-0926 to 9050-9059 (10 words)
9051: 35 SLT 0004 9050 symb:    9051 slt  4      9050       0730-Acc = -0034 0000, OV: 0
9050: 15 ALO 9053 9054 symb:    9050 alo 1f     2f          0731-Acc =  TLU 9052+Acc = TLU 9018 9058
9054: 20 STL 9055 9056 symb:   2     stl hld                0732-Store TLU instr at hld=9055
9056: 69 LDD 9052 9057 symb:         lod  9052              0733-DIST=1
9057: 27 SET 9002 0496 symb:         set  9002       d      0734-       
0496: 09 LDI 7750 9055 symb:         ldi a0001c hld         0735-copy from 1750+IRC=1850 Copy 1850-1899 to 9002-9051 (50 words) = 8888888888       
9055: 84 TLU 9018 9058 symb:          ... Search DIST: 0000000001+ '    ~'
                                      ...  Found 9018: 8888888888+ 'YYYYY' -> is equiv to 1866, availability for Addr 0029
                                      ... Result ACC: 0000000000 8490189058+, OV: 0
9058: 16 SLO 9053 9059 symb:   3     slo 1b                 0738-Acc=acc-xx9052xxxx=-00 0034 0000: slo TLU base addr to get index on table
9059: 46 BMI 9001 0525 symb:         bmi  9001       d      0739-jmp to 9001 if availability found (acc < 0)       
9001: 09 LDI 0250 9001 symb:    9001 ldi 30000   9001       0723-Copy 0250-0299 to 9001-9050 (50 words)
9001: 30 SRT 0004 9016 symb:    9001 srt  4                 0752-Acc=-34       
9016: 58 AXC 8002 9017 symb:         axc  8002              0753-IRC=100+Acc=100-34=66              
9017: 58 AXC 0050 9002 symb:         axc  50     9002       0754-IRC=IRC+50=66+50=116          
9002: 60 RAU 7750 9018 symb:    9002 rau a0001c             0755-Now Acc=(1750+IRC)=(1866)=8888888888 0000000000+, OV: 0       
9018: 36 SCT 0000 9019 symb:         sct  0                 0756-ACC: 8888888888 0000000000+, OV: 0
9019: 82 RAB 8002 9020 symb:         rab  8002              0757-IRB=0
9020: 35 SLT 0001 9021 symb:         slt  1                 0758-Now ACC: 8888888880 0000000000+, OV: 0
9021: 31 SRD 4001 9022 symb:         srd  1   b             0759-Now ACC: 0888888888 0000000000+, OV: 0
9022: 21 STU 7750 9023 symb:         stu a0001c             0760-Store Avail data with addr reserved: Write 1866: 0888888888+ this reservation for addr 0029
9023: 60 RAU 8006 9024 symb:         rau  8006              0761-AccUp=IRB=0
9024: 19 MPY 9025 9026 symb:         mpy 50i                0762-Acc=0x50=0              
9026: 82 RAB 8002 9027 symb:         rab  8002              0763-IRB=Acc  -> IRB=IRB x 50
9027: 65 RAL 8007 0647 symb:         ral  8007       d      0764-Acc=IRC=116  
0647: 14 DIV 0750 0553 symb:         div 4i   d      d      0765-ACC: 0000000000 0000000029+, OV: 0
0553: 19 MPY 9028 0546 symb:         mpy 500i        d      0766-Acc=AccUpx500+AccLo=0x500+29=29 en AccUp       
0546: 15 ALO 8003 9029 symb:         alo  8003              0767-Now ACC: 0000000029 0000000029+, OV: 0
9029: 15 ALO 8006 9003 symb:         alo  8006   9003       0768-AccLo=AccLo+B = 29+0=29, Now ACC: 0000000029 0000000029+, OV: 0
9003: 82 RAB 9008 9005 symb:    9003 rab  9008   9005       0769-IRB=1 (restore IRB=1=processing IA)
9005: 47 BOV 9030 9004 symb:    9005 bov 1f      9004       0770-OV Not Set (whould been set on line 0535- if addr blank)-> Branch Not Taken       
9004: 20 STL 9006 9031 symb:    9004 stl  9006  findx       0771-Save addr to assign to symbol: Write 9006: 0000000029+


                                         add new symbol to symbol table, 
                                         add symbol addr to symbol addr table


9031: 60 RAU 9007 9032 symb:   findx rau  9007              0772-addr 9007 copies from addr 0256 = level defined at line 0804- level alf +00 0000 0000, so Acc=level=0              
                                                                 last copy operation: 
                                                                        line 723-Copy 0250-0299 to 9001-9050 (50 words) 
9032: 88 RAC 8001 9033 symb:         rac  8001              0773-IRC=Acc=0=last 4 digits of level=first symbol free in symbol table
9033: 30 SRT 0002 9034 symb:         srt  0002              0774-Now ACC: 0000000000 0000000000+, OV: 0
9034: 16 SLO 8002 9035 symb:         slo  8002              0775-Clear AccLo       
9035: 84 TLU 9011 9036 symb:         tlu  9011              0776-addr 9011 copies from addr 0260:
                                                                line 1305   30010 BMI  898   3      0260 +46 0898 0003                                   
                                                                line 1206   30011 BOV        1      0261 +47 1966 0001                                   
                                                                line 1474   30012 BD6 1F            0262 +96 1539 1739                                   
                                                                line 1446   30013 WTM  0   B 3      0263 +56 4000 0003                                   
                                                                line 1095   30014 BD7  9008         0264 +97 9008 9014                                   

                                           ... Search DIST: 0000000000+ '     '
                                           ...  Found 9011: 4608980003+ '~~8 ~'
                                           ... Result ACC: 0000000000 0090110000+, OV: 0
9036: 15 ALO 9037 8002 symb:         alo         8002       0777-Add to located addr the instr 64 9999 0594 -> 
                                                                 AccLo=00 9011 0000 + 64 9999 0594 
                                                                      = 6590100594 = RAL 9010 0594
8002: 65 RAL 9010 0594                                          -addr 9010 copies from addr 0259      
                                                                 line 1490   30009 STD  RAMSW   1    0259 +24 0773 0001
                                           ... Read 9010: 24 0773 0001+
                                           ... ACC: 0000000000 2407730001+, OV: 0
0594: 15 ALO 9007 0501 symb:         alo  9007       d      0779-acclo=acclo + level. As level=0, ACC: 0000000000 24 0773 0001+, OV: 0
0501: 20 STL 0256 0509 symb:         stl level       d      0780-set level: Write 0256: 24 0773 0001+ (=STD RAMSW 0001)
0509: 59 SXC 0293 0915 symb:         sxc  293        d      0781-set IRC=irc (that is =level)-293       
0915: 49 BMC 9038 0827 symb:         bmc store  hlt11       0782-if IRC >= 0 -> symb table full
9038: 69 LDD 1493 0596 symb:   store lod s0294       d      0783-DIST=symbol to add to table: Read 1493: 6161000000+ 'AA    ' (s0294=sentinel for TLU=symb searched)
0596: 24 STD 7493 9039 symb:         std s0294c             0784-Store new symbol in symbol table indexed by IRC, -> STD    1200 9039  (developed addr)
9039: 66 RSL 8007 9040 symb:         rsl  8007              0785-Set Acc=IRC=index on symbol table, Now ACC: 0000000000 0000000293+, OV: 0
9040: 14 DIV 9041 0206 symb:         div 2i          d      0786-symbol addr table at e0147. Stores two symbol addr (in DA&IA) per word -> this is why index on e0147 = index on Symb table /2. remainder is used to select DA or IA
                                                                 293/2 -> Div result ACC: 0000000001 0000000146+, OV: 0
0206: 89 RSC 8002 9042 symb:         rsc  8002              0787-IRC=Index on symb table addr=-146
9042: 16 SLO 8001 9043 symb:         slo  8001              0788-clear acclo: Read 8001: 0000000146+, Now ACC: 0000000001 0000000000+, OV: 0
9043: 15 ALO 9006 9044 symb:         alo  9006              0789-AccLo=symbol addr: Read 9006: 0000000029+, Now ACC: 0000000001 0000000029+, OV: 0
9044: 69 LDD 7196 9045 symb:         lod e0147c             0790-e0001=symbol addr table (at addr 1196). IRC=-146 -> LDD    1050 9045  (developed addr), Read 1050: 0000000000+
9045: 44 NZU 9046 9047 symb:         nzu        2f          0791-2 addr per word. result of div by 2 set if addr symbol is on DA side or IA side. Now ACC: 0000000001 0000000029+, OV: 0
9046: 35 SLT 0004 9009 symb:         slt  4      9009       0792-Store on DA -> shift to DA position. Now ACC: 0000010000 0000290000+, OV: 0
9009: 22 SDA 7196 9048 symb:   9009  sda e0147c             0793-Store DA on Symbol Addr table       
9048: 30 SRT 0004 9049 symb:         srt  4     3f          0794-restore addr, Now ACC: 0000000001 0000000029+, OV: 0
9049: 47 BOV 4002 9000 symb:   3     bov  2   b  9000       0799-IRB=1, but OV=0 -> return to 9000

                                        check if resolved addr is in drum/ias


9000: 09 LDI 0303 9029 symb:    9000 ldi 00001   9029       0518-       
                                         ... Copy 0303-0349 to 9000-9046 (47 words)
9029: 88 RAC 9031 9018 symb:    9029 rac  9031   9018       0582-addr 9031 comes from addr 0334 = save value of IRC = -256 = Save IRC (value to calc return addr from master calculation)       
9018: 20 STL 9050 9021 symb:    9018 stl  9050              0590-save absolute location addr Write 9050: 0000000029+       
9021: 35 SLT 0006 9022 symb:         slt  6                 0591-Now ACC: 0001000000 0029000000+, OV: 0
9022: 16 SLO 8002 9023 symb:         slo  8002              0592-AccLo to DIST, ACC=0001000000 0000000000+, OV: 0
9023: 84 TLU 9002 9024 symb:         tlu  9002              0593-Search in memory map at lines 0583-0589        
                                          ... Search DIST: 0029000000+ ' *   '
                                          ...  Found 9002: 1999000001- ')9  ~'
                                          ... Result ACC: 0001000000 0090020000+, OV: 0 -> AccLo = 00 ADDR 0000 addr of data found
9024: 15 ALO 9025 8002 symb:         alo         8002       0594-Read 9025: 8000009026+, ACC: 0001000000 8090029026+, OV: 0
8002: 80 RAA 9002 9026                                          Read 9002: 1999000001-, IRA: 0001-
                                                                 Set IRA=contents of addr 9002=-1. 
9026: 41 BMA 9016 9012 symb:         bma  9016   9012       0596-       
9016: 67 RAM 9404 9216 symb:    9016 ram  9004b  9016a      0597-ACC: 0000000000 0907800000+, OV: 0
9015: 17 AML 9050 9027 symb:    9015 aml  9050  1f          0600-Read 9050: 0000000029+, ACC: 0000000000 0907800029+, OV: 0
9027: 69 LDD 8003 9028 symb:   1     lod  8003              0602-clear distrib       
9028: 23 SIA 9050 9001 symb:         sia  9050   9001       0603-Write 9050: 0000000029+
9001: 65 RAL 8001 6300 symb:    9001 ral  8001   300 c      0604-Acc=addr, jmp to 300+IRC = 300+(-256) = 44 (line 0441-)

                                        back to processing the instruction

0044: 69 LDD 9011 0600 symb:    44   lod  9011  alfin       0441-The addr 9011 holds the inst being assmbled: Read 9011: 60 0024 0000+ already has OpCode (RAU=60), DA (=0024). Now will set IA
0600: 23 SIA 9011 0207 symb:   alfin sia  9011              0442-Set IA on instr assembled: Write 9011: 6000240029+
0207: 69 LDD 1960 0764 symb:         lod  1960              0443-Read 1960: 0000908000+ source code control word
0764: 96 BD6 0622 0527 symb:         bd6 1f                 0444- 8-> instr is negative, 9=positive, Now DIST: 0000908000+
0527: 65 RAL 9011 0241 symb:         ral  9011  2f          0445-get inst assembled as positive value: Read 9011: 6000240029+
0241: 15 ALO 9000 0008 symb:   2     alo  9000  3f          0451-addr 9000 comes from addr 0303 adend set at line 0470-; Now Read 9000: 0000000000+, ACC: 0000000000 6000240029+, OV: 0
0008: 20 STL 0314 2219 symb:   3     stl instr   219 a      0452-save instr assembled. IRA=output mode: -1 -> 1-card output format

                                        select output mode

0218: 65 RAL 0477 0081 symb:    218  ral fivtg  pnch1       0453-Read 0477: 8888888888-, ACC: 0000000000 8888888888-, OV: 0
0081: 46 BMI 0642 0635 symb:   pnch1 bmi 9f     2f          0921-jmp to 2f if is 5 word per card mode
0642: 65 RAL 0046 0601 symb:   9     ral 533tl              0943-now 533tl=0       
0601: 45 NZE 0684 0705 symb:         nze corof              0944-jmp to corof if NO core mode
0705: 65 RAL 1961 0766 symb:         ral locat              0945-assembled instr location: Read 1961: 1221000000+, ACC: 0000000000 1221000000+, OV: 0
0766: 16 SLO 0368 0773 symb:         slo n0027  ramsw       0946-Read 0368: 4905050598+, now ACC: 0000000000 3684050598-, OV: 0
0773: 46 BMI 0684 0685 symb:   ramsw bmi corof  corsw       0985-jmp to NO core mode (corof)       
0684: 88 RAC 0209 0890 symb:   corof rac finis  prone       1000-jmp to prone (print one card), the jmp to finis (IRC=0209)

                                        print/punch one card
    
0890: 81 RSA 0001 1496 symb:   prone rsa  1                 1002-IRA=-1
1496: 69 LDD 1449 0757 symb:         lod onesw              1003-Read 1449: 9999999999-
0757: 92 BD2 0830 1017 symb:         bd2 9f                 1004-       
1017: 50 AXA 0001 0026 symb:         axa  1     90001       1005-IRA=0       
0026: 67 RAM 0780 0893 symb:   90001 ram locus              1010-Read 0780: 0000001221+ <-- addr of assembled instr
0893: 35 SLT 0005 0656 symb:         slt  5                 1011-ACC: 0000000000 0122100000+, OV: 0
0656: 27 SET 9057 0930 symb:         set  9057              1012-       
0930: 15 ALO 0585 0694 symb:         alo ccnt1              1013-ccnt1 = card count <- number of cards punched
0694: 15 ALO 0897 0604 symb:         alo 1ixxx              1014-incr, now ACC: 0000000000 0122100001+, OV: 0
0604: 29 STI 1957 0770 symb:         sti  1957              1015-       
                                          ... Copy 9057-9059 to 1957-1959 (3 words)
0770: 23 SIA 0585 0940 symb:         sia ccnt1              1016-store updated ccnt1: Write 0585: 0000000001+
0940: 27 SET 9040 1045 symb:         set  9040              1017-       
1045: 09 LDI 1951 0672 symb:         ldi  1951              1018-copy read card area to punch card area
                                     ... Copy 1951-1970 to 9040-9059 (20 words)
0672: 20 STL 9048 1030 symb:         stl  9048              1019-Write 9048: 0122100001+
1030: 65 RAL 9050 0687 symb:         ral  9050              1020-Read 9050: 1221000000+, ACC: 0000000000 1221000000+, OV: 0
0687: 10 AUP 9049 1495 symb:         aup  9049              1021-Read 9049: 0000908000+, ACC: 0000908000 1221000000+, OV: 0
1495: 35 SLT 0003 0703 symb:         slt  3                 1022-       
0703: 30 SRT 0003 0762 symb:         srt  3                 1023-       
0762: 47 BOV 0916 0633 symb:         bov 1f                 1024- ???      
0633: 30 SRT 0002 0639 symb:         srt  2                 1025-ACC: 0000009080 0012210000+, OV: 0
0639: 22 SDA 9047 0706 symb:         sda  9047              1026-Write 9047: 0012218000+ <- location and card type
0706: 69 LDD 0314 1018 symb:         lod instr              1027-Read 0314: 6000240029+ <- assembled instr
1018: 24 STD 9046 0683 symb:         std  9046              1028-       
0683: 35 SLT 0002 0939 symb:         slt  2                 1029-ACC: 0000908000 1221000000+, OV: 0
0939: 17 AML 0046 0901 symb:         aml 533tl  2f          1030-read 0046: 0000000000+
0901: 16 SLO 9050 0709 symb:   2     slo  9050              1033-Read 9050: 1221000000+, Now ACC: 0000908000 0000000000+, OV: 0
0709: 69 LDD 8003 0671 symb:         lod  8003              1034-Read 8003: 0000908000+
0671: 23 SIA 9049 2028 symb:         sia  9049  90003a      1035-Set punch control word (Write 9049: 0000900000), jmp to 0028+IRA, as IRA=* -> jmp to 0028
0028: 71 WR1 9040 0830 symb:   90003 wr1  9040  9f          1006-punch at last
L: ... Punch Card Unit CDP1
L: ... Punch Card 9040: 0091929291+ ' 1221' 
L: ... Punch Card 9041: 0000000000+ '     '
L: ... Punch Card 9042: 6161000000+ 'AA   '
L: ... Punch Card 9043: 7961840000+ 'RAU  '
L: ... Punch Card 9044: 0000000000+ '     '
L: ... Punch Card 9045: 0000000000+ '     '
L: ... Punch Card 9046: 6000240029+ '~ ~ *' <- instr
L: ... Punch Card 9047: 0012218000+ ' ~~~ ' <-- xx NNNN xxxx  location
L: ... Punch Card 9048: 0122100001+ '~~~ ~' <-- xx xxxx NNNN card count
L: ... Punch Card 9049: 0000900000+ '  0  '
L: Punch Card: 6I1954195C      0001241221800?600024002I   1221rau      aa              

0830: 69 LDD 1448 0851 symb:   9     lod prtsw              1007-Read 1448: 8888888888-
0851: 92 BD2 0554 2027 symb:         bd2 9f     90002a      1008-jmp to 9f (no PTR output selected)
0554: 47 BOV 6000 6000 symb:   9     bov  0   c  0   c      1036-clear OV, jmp to IRC=209
0209: 65 RAL 1962 0067 symb:   finis ral progp              0458-finish instr processing. progp=0 -> Acc=0
0067: 45 NZE 0121 0949 symb:         nze        1f          0459-Acc=0 -> jmp to 1f
0949: 60 RAU 0256 0511 symb:   1     rau level              0465-Now ACC: 2407730001 0000000000+, OV: 0
0511: 24 STD 0215 0674 symb:         std kee                0466-???       
0674: 60 RAU 0927 0532 symb:         rau corec              0467-???, Now Acc: 0000008999 0000000000+, OV: 0
0532: 20 STL 0188 0542 symb:         stl optim              0468-clear optim var
0542: 21 STU 0096 1049 symb:         stu keepp              0469-???, Write 0096: 0000008999+
1049: 20 STL 0303 0507 symb:         stl adend              0470-???, Write 0303: 0000000000+
0507: 69 LDD 0460 0615 symb:         lod carry              0471-Read 0460: 6264980024+
0615: 24 STD 0223 0526 symb:         std keep   ssout       0472-???

0526: 70 RD1 1999 1998 symb:   ssout rd1  1999  start       0474-       
L: Read Card:                                                rauaa
L: ... Read Card Unit CDR1
L: ... Read Card 1951: 0000000000+ '     '
L: ... Read Card 1952: 6161000000+ 'AA   '
L: ... Read Card 1953: 0000000000+ '     '
L: ... Read Card 1954: 7961840000+ 'RAU  '
L: ... Read Card 1955: 0000000000+ '     '
L: ... Read Card 1956: 0000000000+ '     '
L: ... Read Card 1957: 0000009999+ '   99'
L: ... Read Card 1958: 0000009999+ '   99'
L: ... Read Card 1959: 0000009999+ '   99'
L: ... Read Card 1960: 0000908000+ '  0~ '

1998: 60 RAU 1960 0015 symb:   start rau  1960              0127-       



