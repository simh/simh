
Restoration comments May/2018
By Roberto Sancho

Internal Translator (IT Compiler)

From Bitsavers Manual CarnegieInternalTranslator.pdf 

The run_it.ini script uses P1 run-time package that 
provides floating point +,-,/,* PUNCH and READ, that's 
all. In particular, it does not provides power functions
so using power operator in IT program will crash the
object program in run-time.

To allow the use of IT power operator, replace _P1 
package by _P2, _P3 or _P4 (depending on what is needed)

In the original listing found in manual, some opcodes has 
a different name of the standard SOAP II ones. 
They have been changed to regular SOAP names  

   Mnemonic in           Standard SOAP  
   original listing      equivalent mnemonic
                                            
   AAB            ->     AML    
   SAB            ->     SML     
   NZA            ->     NZE    
   RAB            ->     RAM    
   RSB            ->     RSM    
   RDS            ->     RD1    

IT compiler generates also these opcodes in object 
program, to be assembled by IT modified y SOAP I. As 
SOAP I is not available, the IT compiler has been 
modified to produce standard SOAP II opcodes.

These modifications are done in lines 394-410, file
it_compiler_listing.txt with a comment to signal it.

Also all the correction to the listing stated in the 
manual has been applied. They are stated at the
end of it_compiler_listing.txt file.

Original listing in manual describes the modifications
to apply to standard SOAP I deck in order to assemble
IT compiler produced SOAP code (soap_patch_listing.txt).

As SOAP I is not available, an equivalent set of modifications
has been written to be applies to SOAP II in order to 
allow to assemble IT produced compiled code (soapII_patch.txt)

Floating point numbers are encoded as

   2300000049 = 0.23
   1000000050 = 1.0
   1500000052 = 150.0



