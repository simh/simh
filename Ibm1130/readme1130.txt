Here's the 1130 simulator as it stands now.

Status: 10 April 2002

 * The 1132 printer now works (at least printing numbers)
   and there are some corrections to the assembler. The
   Disk Cartridge Initialiation Program (DCIP) is now
   included and works. See notes below.

 * For updated information about the 1130 and for
   future simulator, 1130 OS and application software
   developments, check www.ibm1130.org periodically.
   Sign up for the mailing list to get updates as they occur!

 * I still haven't written any documentation.

 * Thanks to Oscar E Wyss (www.cosecans.ch) for
   the DMS V12 source code listings and one card
   programs, to Douglas W. Jones for the DMS V10, 11 and
   12 microfiche (which will end up scanned on IBM1130.org).

 * Thanks to Robert Alan Byer for adding the 1130
   to the simh makefiles & testing the builds on several
   platforms.

 * I now have the source code for the 1130 Disk
   Monitor System and compilers in the software package.
   The asm1130 assembler is not quite up to the task of
   compiling it yet. We have located a copy of the binary
   disk load deck that will let us build a working disk
   image. I hope to have these available online and as part
   of this distribution, respectively, by Summer, 2002.
   At that point the source code will be included too.

 * Assembler has been updated to handle card image input
   correctly. The DMS sources seems to mix up @ and '
   as a leading symbol in labels, I have to find out why
   this is.

 * see bug list below

Brian Knittel
brian@ibm1130.org

--------------------------------------------------------------------------

Contents:

There are two programs:

	ibm1130		the simulator
	asm1130		cross assembler

   actual 1130 software:
	zdcip.asm	"disk cartridge initialization program"

        dmsboot.asm     the DMS cold start loader

        zcrdumpc.asm    a cold-start-mode one card memory dump program

And several files in the software (sw) directory:

	test.dsk	disk image, a formatted but empty 1130 disk
	onecard/*	one-card programs from Oscar Wyss

	boot2		script to boot the 1130
	boot2.ldr	an older DMS cold start loader
	boot1.ldr	APL cold start loader

	type.asm	program to type on console printer
	prtcr.asm	program to copy card deck to console printer

--------------------------------------------------------------------------
Status of the simulator:

* bugs:

  (1) Deck files may not work correctly; have to check. When second deck file
      is loaded it appears that the second file is not read correctly?

* the card punch is untested

* the card reader emulator now supports deck files: a list of multiple files from
  which to read; this makes it possible to assemble complex decks of mixed
  text and binary cards without having to actually combine the components
  into one file.

* the card reader, punch and disk don't compute their device status word
  until an XIO requests it; this is probably bad as the examine command
  will show the wrong value.

* there is a reasonably fun GUI available for Windows builds; this requires
  the use of modified scp.c and scp_tty.c.  These are enclosed.  You should
  merge the noted modifications into the current versions of scp and scp_tty.
  You will also need to define symbol GUI_SUPPORT during the builds; the Visual
  C makefile has this set.

--------------------------------------------------------------------------
Some sample things to run:

* Disk Cartridge Initialization:

	asm1130 zdcip.asm
	ibm1130

then:	attach dsk0 test.dsk
	attach prt 1132.lst
	load zdcip.out
	go

then:	on GUI:			on console:
	----------------	-----------------
	raise switch 6		dep ces 0200
	program start		go
	lower 6			dep ces 0
	program start		go
	raise 3, 6, 10, 11, 13	dep ces 1234
	program start		go
	program start		go

	(this formats the disk)

	program start		go

	lower all switches
	raise switch 2		dep ces 2000
	program start		go
	lower all switches	dep ces 0
	program start		go
	raise switch 14		dep ces 2
	program start		go

	(this dumps two sectors to printer output file 1132.lst)

	(now try to boot the disk)

	lower all switches	dep ces 0
	check reset		reset
	program load		load dmsboot.out
	program start		go
	
* echo console keyboard to console printer. This one is really fun
* with the GUI enabled; the lights flash in a pleasing manner.

	asm1130 type
	ibm1130
	load type.out
	go

* copy card deck to console printer

	asm1130 prtcr
	ibm1130
	load prtcr.out
	attach cr <filename of your choice>
	go


--------------------------------------------------------------------------
sample usage
--------------------------------------------------------------------------

asm1130 -l resmon.asm

	compiles source file, creates simulator load
	file (resmon.out) and listing file (resmon.lst)

	I had to type in the resident monitor, so it's missing
	the comments. I'll add them later.

	The cross assembler wants files either in strict column
	layout matching the IBM spec, or, if tabs are present in the
	source file,

	label<tab>opcode<tab>flags<tab>operand

	The output file is in the format used by the 1130 simulator's
	load command.

--------------------------------------------------------------------------
cardscan -x image.bmp

	where x =	b for binary interpretation
			a for ascii interpretation
			l for boot loader interpretation

--------------------------------------------------------------------------
ibm1130
	starts SIMH-based simulator. 

	Enhancements:

	* Displays a console window (you can hide with DISABLE CONSOLE)
	  with buttons & lights.  

	* CPU activity log

		the command "attach log file.log" will make the simulator
		write a detailed log of CPU and IO activity, good for
		debugging. Turn off with "detach log".

	* DO command
		reads file 'filename' for SIMH commands. Lets you write
		simh command files to be run from the prompt rather
		than just the command line. Bob Supnik has added this to
		the main simh code tree.

--------------------------------------------------------------------------
check www.ibm1130.org for updates...