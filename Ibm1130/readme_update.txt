Version: 10 July 2003

History (partial):

2003-11-15	Changed default value of TTO STIME to 200. It was
		defined using a constant from sim_defs.h which was
		changed from 10 to 100 at some point. APL\1130 has a
		sychronization bug & hangs if the console output complete
		interrupt occurs between the XIO SENSE and WAIT instructions.
		This bug is hit frequently if the delay time is set to
		100 instructions. 10 worked reliably, but is really not realistic,
 		and 200 may not be adequate in all cases, but we'll try 200 for now.

2003-11-00	Updated GUI to allow drag and drop to simulated card
		reader, tear-off from simulated printer

2003-07-10	Fixed disk and console terminal bugs uncovered by
		APL\1130. Added APL keyboard and output font support
		to enable use of APL\1130. APL will be released soon.

2003-03-18	Fixed bug in asm1130 that produced an error message
		with a (legal) offset of +127 in MDX instructions.

		Fixed sign bug in 1130 emulator divide instruction.

Interim 1130 distribution:
--------------------------------------------

folders:
	.		sources
	winrel		windows executables
	windebug	windows executables
        utils           accessory programs
	utils\winrel	windows executables
	utils\windebug	windows executables
	sw		working directory for DMS build & execution
	sw\dmsR2V12	Disk Monitor System sources

programs:
	asm1130		cross assembler
	bindump		object deck dump tool, also used to sort decks by phase id
	checkdisk	DMS disk image check and dump
	diskview	work in progress, interpreted disk image dump
	ibm1130		emulator
	mkboot		object deck to IPL and core image converter
	viewdeck	binary to hollerith deck viewer if needed to view phase ID cards and ident fields

batch file:
	mkdms.bat	builds DMS objects and binary cards. Need a shell script version of this.

IBM1130 simulator DO command scripts:
	format		format a disk image named DMS.DSK
	loaddms		format and install DMS onto the formatted DMS.DSK
        for             run a Fortran program
        list            list the disk contents
        asm             assemble a program

ancillary files:
	loaddms.deck	list of files stacked into the card reader for loaddms
        *.deck          other sample deck files
