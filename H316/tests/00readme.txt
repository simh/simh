IMP DEMONSTRATION
-----------------

  This directory contains a version of the IMP firmware, ca 1973, and some simh
command files to help you run it.  These command files define a three IMP
network consisting of IMP nodes #2, 3 and 4.

  * IMP 2 has a single virtual modem line which connects to IMP 3.

  * IMP 3 has two modem lines, one connects to IMP 2 and the other to IMP 4.

  * IMP 4 has a single modem line, connected to IMP 3.

  A picture is worth a thousand words -

    IMP 2  <------->  IMP 3  <------->  IMP 4
    	     modem             modem

  The interesting thing to notice about this arrangement is that there is no
path between IMP 2 and IMP 4.  Any messages between those two machines must
pass thru IMP 3.  Sounds simple, but for that to work requires all IMPs to
discover their adjacent nodes, find out the neighbors of their neighbors, build
routing tables, and then figure out the path to a remote node.


SETUP
-----

  Assuming you've already built the H316 simulator with IMP support, open three
terminal windows and run on instance of H316 in each.

  Window #1 -> H316 imp2.cmd

  Window #2 -> H316 imp3.cmd

  Window #3 -> H316 imp4.cmd

  These command files will configure the hardware, load the IMP firmware,
set the IMP node numbers, and establish the modem connections.  These command
files DO NOT actually start the simulation, however, so after you've loaded
all three IMPs, revisit each of the three windows and type "GO".

  Initially you should see a message like this in all three windows

  	    DBG(737742)> WDT LIGHTS: changed to 177400

This message refers to the status of the lights on the IMP's display panel -
177400 means all lights are on.  After about 30 seconds (yes, 30 seconds!)
you'll see this message on IMP 2 and 4

       	   DBG(19558711)> WDT LIGHTS: changed to 077400

and these on IMP 3

    	   DBG(15492720)> WDT LIGHTS: changed to 077400
	   DBG(17096499)> WDT LIGHTS: changed to 037400

This means IMP 2 and 3 have turned off the light corresponding to modem line 1.
The IMP designers used these lights to indicate problems or errors, so a light
off means that all is well.  IMP 2 and 3 have just told you that their modem
line #1 is UP.  Remember that IMP 3 has two modems, so we see two lights turn
off there.

  BTW, the time it takes for the IMP lines to come up is controlled by
intentional delays in the IMP firmware and is driven by the RTC.  simh simulates
the RTC in real, wall clock, time, and the delay you see here is comparable to
the delay you'd see on real IMP hardware.  IT HAS NOTHING TO DO WITH THE SPEED
OF THE PC RUNNING THE SIMULATION!


NOW WHAT ?
----------

  Now that you've got your three IMP network running, what can you do with it?
Well, the IMPs had a very simple DDT like command language.  Typing "0/" will
display the contents of location 0 -

	0/ 0

  The "C" command was used to "cross patch" the terminal on your IMP to another
IMP.  For example, typing "2C" on IMP 4 

      2C

will connect the console keyboard on IMP 4 to the console printer on IMP 2.
Anything you type on IMP 4 will now be echoed on IMP 2 - try it...  In the
Olden Days ARPAnet sites would use this facility to communicate, operator to
operator.  

  By adding 100 (octal) to the destination node, you can cross patch to DDT.
For example, typing "102C" on IMP 4 will cross patch that TTY to DDT, not the
operator, on IMP 2.  This allowed guys at BBN and the NCC to debug IMP problems
remotely.


FILES INCLUDED
--------------

  The files included in this software kit are -

      00readme.txt     - this file
      impcode.cmd      - simh script to load the IMP firmware
      c-listing-ps.txt - assembly listing of the IMP firmware
      impconfig.cmd    - simh script for common IMP configuration
      imp2.cmd	       - IMP node #2 specific configuration
      imp3.cmd	       - IMP node #3 specific configuration
      imp4.cmd	       - IMP node #4 specific configuration
      imploop.cmd      - IMP single line loopback test
      imploop4.cmd     - IMP 4 line loopback test
      mdmtest1.cmd     - simh modem simulation test, part 1
      mdmtest2.cmd     - simh modem simulation test, part 2
      testrtc.cmd      - simh RTC simulation test


MORE READING
------------

  Dave Walden's website, 

       		http://www.walden-family.com/bbn/

has a wealth of BBN papers, proposals, reports, journals, and other material.
There is an entire section devoted to ARPAnet and IMP related content.


Bob Armstrong
bob@jfcl.com
30-Nov-2013

