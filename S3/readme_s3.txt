    Welcome to the IBM System/3 Model 10 SIMH simulator.
    ---------------------------------------------------

	To compile under linux:

	cc s3*.c scp*.c sim_rev.c -o s3

	This code can be compiled and run as a console application using
	Microsoft Visual C++.



	To IPL the provided SCP distribution disk:

	./s3
	sim> at r1 m10scp.dsk
	sim> at f1 f1f1f1.dsk
	sim> at lpt print.txt
	sim> d sr 5471
	sim> boot r1


	 // DATE 06/14/01
	 // NOHALT
	 // LOAD $MAINT,R1
	 // RUN
	 // COPY FROM-R1,LIBRARY-ALL,NAME-DIR,TO-PRINT
	 // END


	(A printout of the libraries and directories on the SCP DTR
	disk will be in the file print.txt)


	The text file "system3.txt" gives details on the simulators
	implementation of System/3 hardware.

	A write up on the use of the SCP and the OCL job control language is
	in the text file "userguide.txt".  This includes examples of using the
	utility programs, and a tutorial guiding you thru a sysgen.

	A nearly complete listing of all possible SCP halts is in the 
	document "haltguide.txt".

	IMPORTANT NOTES:

	1) How to correct typing errors when using the System/3 console:
	If you make an error, press ESC, which will cancel the current
	line being typed and print a quote in position 1.  Then you
	can use CTRL/R to retype characters up until the error, then
	type correctly.  Or simply retype the line. BACKSPACE DOES NOT
	WORK with the SCP.
 
	2) While the simulator allows disk images to be independently
	attached to any disk unit, on the real hardware R1 and F1 were on
	a single spindle, and R2 and F2 likewise.  It is not possible using
	SCP to attach R1 without attaching a disk image to F1 also, because
	SCP will always look at F1 even when IPLed off R1.

	The OS distributed with the simulator is version 16 of the Model 
	10 SCP.  This is sysgenned with support only for R1 and F1.  If you
	do a sysgen to support R2 amd F2 also, you must have images attached
	to all 4 disks when you IPL, because SCP looks at all drives when
	it starts up, and you will get an "Unattached Unit" error if you
	fail to have one attached.

	3) The 1442 card reader had in reality one card input hopper
	and two stackers.  This means the same path is used for reading and
	punching cards.  When punching cards, SCP does a read operation
	and inspects the card read for blanks, and if it is not blank,
	issues a YH halt.  SCP will not punch data onto non-blank cards.
	This feature causes problems in the simulator, and as a result
	if you punch cards from SCP, YOU MUST not have any file attached
	to the CDR device.  Leaving this device unattached presents an
	infinite supply of blank cards to SCP for punching.  


 -- End of README_S3.txt --
