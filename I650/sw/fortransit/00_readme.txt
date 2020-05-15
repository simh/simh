
Restoration comments May/2018
By Roberto Sancho

Fortransit
From Bitsavers Manual CarnegieInternalTranslator.pdf (listings)
and fortransit.pdf (reference manual)

Fortansit comes in 4 versions: Fortransit I, I (S), II, II (S).
"(S)" means special character support on IBM 533 card read-punch.
"I" means basic IBM 650, "II" means IBM 650 + IBM 653 Storage Unit
that provides Floating Point and Index instructions.
We are using Version II (S).

In the original listing on IT for Fortransit II there an error
on lines 670, 671. These lines are in fact a bugfix replacement 
for lines 660 and 661. The missing 670 and 671 lines has been 
recovered for the IT for Fortransit I listing: 

   ALO 8001        1455 15 8001 1864  RSV: ADDED MISSING CARDS 
   ALO      UBSR   1864 15 0664 1419 

On the original manual, the following pieces of software are 
missing:

   SOAP-PACKAGE (SOAP II modified to be used by Fortransit)
   Subroutines PACKAGE (with built in functions to be called
      by object program in run-time)
   Add function title program

The compilation and run procedure has been slightly modified
in its implementation in run_fortransit.ini script from what 
it is stated in manual. This is to allow the use of standard 
SOAP II assembler and the Subroutines package.

Also an Add function title program (fortransit_addfn_listing.txt)
has been rewritten to allow the usage of function title cards 
as stated in manual, and to populate the standard fortransit 
functions to be recognized by the translator.

The missing subroutines PACKAGE has been re-created adapting the
available IT run-time PACKAGES P1, P2, P3 and P4 to 
FORTRANSIT. PUNCH and READ routines has been written from 
scratch according to functional description from manual.

The PACKAGE provides the subroutines stated in fortransit.pdf
in page 36, and also provides a set of functions to be 
used in fortransit source code:

   A=LOGF(B)         base 10 Logarithm: log 10
   A=EXPF(B)         base 10 exponent: 10^(B)
   A=LNF(B)          base e logarithm: neperian log e
   A=EXPNF(B)        base e exponent: e^(float)
   A=COSF(B)         cosine
   A=SINF(B)         sine
   A=SQRT(B)         square root
   A=ABSF(B)         absolute value
   A=INTF(B)         integer part
   A=MAXF(B,C,...)   returns maximum value of argument list

All functions has FLOAT arguments and returns FLOAT value.
If a FIXED argument is given, the program will stop with AR=9099

  9099  ALARM FUNCTION ARG IS FIX BUT SHOULD BE FLOAT

There is no check on number nor type of MAXF arguments.
A maximum of 10 is safe, more will overwrite the program.

FORTRANSIT object program only needs few functions to be 
present in run time PACKAGE. These are identified as
BUILT-IN SUBROUTINES (180 WORDS) in file pack_listing.txt

Any other function can be deleted from source code 
PACKAGE (pack_src.txt) to free storage for program or data. 

List of functions that can be removed to free drum memory:
   
   SOAP    Description    IT number
   label

   E00AK   FIX ** FIX        10
   E00AL   FLOAT ** FIX      11
   E00LQ   FLOAT ** FLOAT   302
   E00AB   LOGF               1
   E00AC   EXPF               2 
   E00LO   LNF              300
   E00LP   EXPNF            301                  
   E00AV   COSF              21
   E00AW   SINF              22
   E00AX   SQRTF             23
   E00AY   ABSF              24
   E00AZ   INTF              25
   E00BA   MAXF              26

If functions 10,11 are removed, the corresponding
** power operator with these types should not be used

FIX ** FLOAT or FLOAT ** FLOAT requires the presence of
302, 1 and 2 functions.

LNF requires LOGF, EXPNF requires EXPF.
SINF requires COSF.
All other functions are independent.

Any attempt to use a non present function will stop the 
program with AR=90nn where nn is the IT number of function:

  9010  ALARM FIX ** FIX UNDEF
  9011  ALARM FLOAT ** FIX UNDEF
  9302  ALARM FLOAT ** FLOAT UNDEF
  9001  ALARM LOGF UNDEF
  9002  ALARM EXPF UNDEF
  9300  ALARM LNF UNDEF
  9301  ALARM EXPNF UNDEF
  9021  ALARM COSF UNDEF
  9022  ALARM SINF UNDEF
  9023  ALARM SQRTF UNDEF
  9024  ALARM ABSF UNDEF
  9025  ALARM INTF UNDEF
  9026  ALARM MAXF UNDEF

The functions for power to a FIX value (10 and 11) provides 
exact values. Raise power to FLOAT value is calculated using
LOGF and EXPF, that are implemented with a polynomial 
approximation.

The type or a FIX**FIX is also FIX (10**I for example). This 
means that the maximum value allowed is 999999999. If the
computed value of a power to fix is bigger than this maximum
value, the program will stop with AR=0003 (so halt 0003 occurs 
on E00AK routine, not in E00LQ as stated in manual)

SQRTF square root function requires a zero or positive argument.
If argument is negative, program will stop with AR=0012

  0012  ALARM SQRT WITH NEGATIVE ARGUMENT

COSF and SINF function expects a float argument in radians
(thus cosf(pi/2) = 0, sinf(pi/2) = 0). If argument is greater 
that 1E10, program will stop with AR=0013

  0013  ALARM RADIAN ARG TOO BIG

As FORTRANSIT uses index register for DO loop variable control
only values in range 0..1999 are safe for start and end loop
values. Any other values can be used (e.g. DO 10, I=-5,15)
and will compiled without warning, but the generated compiler 
code is wrong. The construct

    J=-5
    DO 10 I=J,15
c   do stuff
10  continue

will compile and work fine iterating but only iterates once. 
This DO is implemented as

    i=j
    for(;;) {
       // do stuff
       if (i-15 < 0) break;       
    }

So DO does not support any negative initial value.
DO loop variable con only get vaues in
range 0 to 9999. For example, 

    J=10000
    K=15000
    DO 10 I=J,K

will iterate from 0 to 5000 (modulo 10000 on supplied values).

PACKAGE occupies drum address 1401 up to 1999, leaving
address 1 to 1400 for fortransit program and data. By deleting
non build in routines, the pack can be reduced to occupy
only locations 1780 to 1999.

Floating point numbers are encoded as

   2300000049 = 0.023
   1000000050 = 0.1
   1000000051 = 1.0
   1500000052 = 15.0


            
