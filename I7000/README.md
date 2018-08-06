Latest status for I7000 Cpus: 

## i701

   * Largely untested.  

## i704
   * SAP works.  
   * Fort2 working.

## i7010
   * PR155 works.
   * PR108 works.
   * Most Diags appear to pass without serious error.
   * Protection mode has some errors left.  
   * Protection mode does not handle setting H or L to 0.  

## i7070
   * Will load Diags, need to remember how to run them to run
   * tests on machine.   

## i7080
   * Sort of working.   
   * RWW, ECB untested.  
   * TLx instructions implimented, untested, see 8SE  
   * Will boot from card.  
   * Tape system appear to be working.  

   * 8CU10B errors:  
	410, 412, 413, 414, 418, 419, 420-427 error becuase
		storage is not parity checked.   
	440 divide not producing correct sign on error.  

## i7090
   * Working with exceptions.  

   * Known bugs:  

      * DFDP/DFMP     Sometimes off by +/-1 or 2 in least signifigant part of result.  
      * EAD           +n + -n should be -0 is +0
      * Not all channel skips working for 9P01C.
      * HTx	Not sure what problems are, does not quite work.  
      * DKx	Sometimes fails diagnostics with missing inhibit of interrupt.   

   * CTSS    works.  
  
   * IBSYS   works.  
  
   * Stand alone assembler works.  

   * Lisp 1.5 works.  

   * Signifigence mode Not tested, Test Code Needed.  

