VT11/GT40 Lunar Lander files
preliminary README phil

Phil Budne
February 9, 2004

Lunar lander startup can take forever (lander uses spin loop to count
down time for display of starting screen.  This may be due to the fact
that SIMH only tracks cycles in terms of instructions, not execution
time).  To speed up display of the startup screen, deposit a 1 in
location 32530 after loading the lander program, and before starting
it;

	sim> ! Set CPU to a Unibus system type
	sim> set cpu 11/70
	sim> ! Enable DLI device so VT device autoconfigures 
	sim> ! with a starting vector of 320
	sim> set dli enable
	sim> set dli line=2
	sim> ! Enable VT device
	sim> set vt enable
	sim> load lunar.lda
	sim> dep 32530 1
	sim> run

Lunar lander only needs a small screen area, and can run using a
simulated "VR14" display, which can fit on many computer screens
without scaling:

	sim> set vt crt=vr14
	sim> set vt scale=1

For more information on the VT11/GT40 see
	http://www.brouhaha.com/~eric/retrocomputing/dec/gt40/

lunar.txt
	Lunar lander instructions
	(from ???)

lunar.lda
	PDP-11 Paper Tape (LDA) format
	http://www.brouhaha.com/~eric/retrocomputing/dec/gt40/software/moonlander/lunar.lda

lunar.dag
	PDP-11 Paper Tape (LDA) format
	above(?) as patched by Doug Gwyn to fix a spelling error?
	load fails with bad checksum under 3.2-preview2?

gtlem.mac
	Does not match above binaries??
	http://www.brouhaha.com/~eric/retrocomputing/dec/gt40/software/moonlander/gtlem.mac

