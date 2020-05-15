
Restoration comments May/2018
By Roberto Sancho


Floating Point Interpretive System (BELL interpreter)
From Bitsavers Manual 28-4024_FltDecIntrpSys.pdf 

Do not uses the loader stated in manual.
Instead, I wrote a loader (SOAP source code is_sys_load_src.txt) 
that allows reading the original listing from manual and generating 
a 1-word per card load deck.

A new deck (deck 21) has been written to defined two new
O2 instructions (see is_set_loopbox.txt):

    set loopbox O2=800
    tr zero     O2=453

These instructions allows a more general use of index
loopbox concept (see is_example_1_src.txt)

Floating point numbers are encoded as

   2300000049 = 0.23
   1000000050 = 1.0
   1500000052 = 150.0

