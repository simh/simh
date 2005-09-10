Here's the 1130 simulator as it stands now.

Status: 22Jul2003

 * Added support for APL\1130 output translations
   and some bug fixes uncovered by APL.

Status: 13Sep2002

 * Added support for 1403 printer. It's MUCH faster
   even in emulation. Not important for general use,
   but it will help the CGI version a lot.

Status: 16Aug2002

 * Disk Monitor System R2V12 is available including the
   Macro Assembler, Fortran Compiler and System Library.

 * There was a bug in the multiply instruction. This has
   been fixed, and now the single precision trig functions
   work correctly.

 * The card punch does not yet work correctly.

 * The card reader, punch and disk don't compute their device
   status word until an XIO requests it; this is probably bad
   as the "examine" command will show the wrong value. Doesn't
   affect functioning of emulated software, though.

 * Documentation is a work in progress, see ibm1130.doc
   in ibm1130software.zip.  We hope to have it finished in
   October. This is a Word document. Will distribute as a
   PDF when it's finished.

 * Thanks to Oscar E Wyss (www.cosecans.ch) for
   the DMS V12 source code listings and one card
   programs, to Douglas W. Jones for the DMS V10, 11 and
   12 microfiche (which will end up scanned on IBM1130.org).

 * Thanks to Robert Alan Byer for adding the 1130
   to the simh makefiles & testing the builds on several
   platforms.

 * For updated information about the 1130 and for
   future 1130 OS and application software developments,
   check www.ibm1130.org periodically.  Sign up for the
   mailing list to get updates as they occur!

 * Cross-assembler has been updated to handle card image input
   correctly. The DMS sources seems to mix up @ and '
   as a leading symbol in labels, I have to find out why
   this is.

BUILD NOTES: if you download this simulator directly from
IBM1130.org, the makefile, source, and binaries are all in
the main directory. If you use the version from Bob Supnik's
SIMH distribution, the makefile is in the main simh
directory, and the SCP files used are Bob's.  For a
Windows build, use the .mak file in the IBM1130 directory,
as this incorporates the GUI.

Make the utilities in the utils directory if you want
to actually build and load DMS from scratch.  Move the
executables to a common directory in your search path

Brian Knittel
brian@ibm1130.org

--------------------------------------------------------------------------
Some sample things to run:
(it's best to hit CHECK RESET or type "reset" between program runs!)

* Run a Fortran Program
        ibm1130
        do job roots
	do job csort

* List the monitor system disk's contents
        ibm1130 
	do job list

* Look into the files "job", "roots.job" and "csort.job" and "list.job"
  to see the actual input files

* When the jobs have run (stop at 2A with 1000 in the
  accumulator), detach the printer (det prt) and look at
  the output file: for.lst or asm.lst. The supplied "job"
  script displays the print output automatically on Windows
  builds.

--------------------------------------------------------------------------
Contents:

There are several programs:

	ibm1130		the simulator
	asm1130		cross assembler
	bindump		dumps contents of relocatable format object decks (xxx.bin)
	checkdisk	validates DMS disk format
	diskview	dumps contents of DMS disk directory
	mkboot		creates IPL and Core Image Format Decks from .bin
	viewdeck	displays contents of Hollerith-format binary decks

Files in the software (sw) directory:

   actual 1130 software:
        dms.dsk         disk image file containing Disk Monitor System
	zdcip.asm	disk cartridge initialization program
        zcrdumpc.asm    a cold-start-mode one card memory dump program
        dmsboot.asm     source code for the DMS cold start loader

   contributed software:
	onecard/*	one-card programs from Oscar Wyss

--------------------------------------------------------------------------
Status of the simulator:

* There is a reasonably fun console GUI available for Windows builds,
  as well as support for the 2250 graphical display.

* The card reader emulator now supports deck files with literal cards and
  breakpoints. The command "attach cr @filename" tells the simulator to
  read data from the files named in the specified file. Input lines are of
  the following form:

   filename a                 -- input file to be read as ascii text
   filename b                 -- input file to be read as binary card images
   !xyz...                    -- literal text xyz..., treated as a card
   !break                     -- halts the simulator
   #comment                   -- remarks

* The do command may have arguments after the filename. These may be
  interpolated in the script and in card reader deck files with %1, %2, etc

--------------------------------------------------------------------------
sample usage
--------------------------------------------------------------------------

ibm1130
	starts SIMH-based simulator. 
        Optional command line arguments: -q quiet mode, -g no GUI

	Enhancements:

	* Windows builds display a console window

	* CPU activity log

		the command "attach cpu file.log" will make the simulator
		write a detailed log of CPU and IO activity, good for
		debugging. Turn off with "detach cpu".

	* DO command [arg1 arg2...]
		reads file 'filename' for SIMH commands. Lets you write
		simh command files to be run from the prompt rather
		than just the command line. In the do command file, %1 will
                be replaced by the first command line argument, etc. This
                applies to the script run from the ibm1130 command line too.

        * DELETE filename
                deletes the named file

        * VIEW filename
                displays the named file with "notepad." (Windows only). 

--------------------------------------------------------------------------
asm1130 -l program.asm

	compiles source file, creates simulator load
	file (program.out) and listing file (program.lst)

	The cross assembler wants files either in strict column
	layout matching the IBM spec, or, if tabs are present in the
	source file,

	label<tab>opcode<tab>flags<tab>operand

	The output file is in the format used by the 1130 simulator's
	load command.

--------------------------------------------------------------------------

Note: the DMS disk is built with the Windows batch file "mkdms.bat".

Subnote: DMS cannot be built with the 1130's native assembler.

	
--------------------------------------------------------------------------
check www.ibm1130.org for updates...
