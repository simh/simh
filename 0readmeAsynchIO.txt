SIM_ASYNCH_IO

Theory of operation.

Features.
   - Optional Use.  Build with or without SIM_ASYNCH_IO defined and 
     simulators will still build and perform correctly when run.
     Additionmally, a simulator built with SIM_ASYNCH_IO defined can
     dynamically disable and reenable asynchronous operation with 
     the scp commands SET NOASYNCH and SET ASYNCH respectively.
   - Consistent Save/Restore state.  The state of a simulator saved 
     on a simulator with (or without) Asynch support can be restored
     on any simulator of the same version with or without Asynch 
     support.
   - Optimal behavior/performance with simulator running with or 
     without CPU idling enabled.
   - Consistent minimum instruction scheduling delays when operating 
     with or without SIM_ASYNCH_IO.  When SIM_ASYNCH_IO is emabled, 
     any operation which would have been scheduled to occurr in 'n' 
     instructions will still occur (from the simulated computer's 
     point of view) at least 'n' instructions after it was initiated.
   
Benefits.
   Allows a simulator to execute simulated instructions concurrently 
   with I/O operations which may take numerous milliseconds to perform.
   Allows a simulated device to potentially avoid polling for the arrival
   of data.  Polling consumes host processor CPU cycles which may better
   be spent executing simulated instructions or letting other host 
   processes run.  Measurements made of available instruction execution
   easily demonstrate the benefits of parallel instruction and I/O 
   activities.  A VAX simulator with a process running a disk intensive
   application in one process was able to process 11 X the number of 
   Dhrystone operations with Asynch I/O enabled.

Asynch I/O is provided through a callback model.  
SimH Libraries which provide Asynch I/O support:
   sim_disk
   sim_tape
   sim_ether

Requirements to use:
The Simulator's instruction loop needs to be modified to include a single
line which checks for asynchronouzly arrived events.  The vax_cpu.c 
module added the following line indicated by >>>:

		/* Main instruction loop */

        for ( ;; ) {

        [...]
>>>	        AIO_CHECK_EVENT;
        	if (sim_interval <= 0) {                /* chk clock queue */
        		temp = sim_process_event ();
        		if (temp)
        			ABORT (temp);
        		SET_IRQL;                           /* update interrupts */
        		}

A global variable (sim_asynch_latency) is used to indicate the "interrupt 
dispatch latency".  This variable is the number of nanoseconds between checks
for completed asynchronous I/O.  The default value is 4000 (4 usec) which
corresponds reasonably with simulated hardware.  This variable controls
the computation of sim_asynch_inst_latency which is the number of simulated
instructions in the sim_asynch_latency interval.  We are trying to avoid
checking for completed asynchronous I/O after every instruction since the 
actual checking every instruction can slow down execution.  Periodic checks
provide a balance which allows response similar to real hardware while also 
providing minimal impact on actual instruction execution.  Meanwhile, if 
maximal response is desired, then the value of sim_asynch_latency can be 
set sufficiently low to assure that sim_asynch_inst_latency computes to 1.
The sim_asynch_inst_latency is dynamically updated once per second in the 
sim_rtcn_calb routine where clock to instruction execution is dynamically 
determined.  A simulator would usually add register definitions
to enable viewing and setting of these variables via scp:

#if defined (SIM_ASYNCH_IO)
    { DRDATA (LATENCY, sim_asynch_latency, 32), PV_LEFT },
    { DRDATA (INST_LATENCY, sim_asynch_inst_latency, 32), PV_LEFT },
#endif


Naming conventions:
All of the routines implemented in sim_disk and sim_tape have been kept
in place.  All routines which perform I/O have a variant routine available
with a "_a" appended to the the routine name with the addition of a single
parameter which indicates the asynch completion callback routine.  For 
example there now exists the routines:
  t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max);
  t_stat sim_tape_rdrecf_a (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max, TAPE_PCALLBACK callback);

The Purpose of the callback function is to record the I/O completion status
and then to schedule the activation of the unit.

Considerations:
Avoiding multiple concurrent users of the unit structure.  While asynch
I/O is pending on a Unit, the unit should not otherwise be on the event 
queue.  The I/O completion will cause the Unit to be scheduled to run
immediately to actually dispatch control flow to the callback routine.
The callback routine is always called in the same thread which is 
executing instructions.  Since all simulator device data structures are 
only referenced from this thread there are no host multi-processor cache 
coherency issues to be concerned about.

Arguments to the callback routine:
UNIT *, and IO Status
Requirements of the Callback routine.
The callback routine must save the I/O completion status in a place
which the next invocation of the unit service routine will reference 
and act on it.  This allows device code to return error conditions
back to scp in a consistent way without regard to how the callback 
routine (and the actual I/O) may have been executed.

Required change in device coding.
Devices which wish to leverage the benefits of asynch I/O must rearrange
the code which implements the unit service routine.  This rearrangement
usually entails breaking the activities into two phases.  The first phase
(I'll call the top half) involves performing whatever is needed to 
initiate a call to perform an I/O operation with a callback argument.  
Control is then immediately returned to the scp event dispatcher.
The callback routine needs to be coded to stash away the io completion 
status and some indicator that an I/O has completed.
The top/bottom half separation of the unit service routine would be
coded to examine the I/O completion indicator and invoke the bottom half
code upon completion.  The bottom half code should clear the I/O 
completion indicator and then perform any activities which normally 
need to occur after the I/O completes.  Care should be taken while
performing these top/bottom half activities to return to the scp event
dispatcher with either SCPE_OK or an appropriate error code when needed.  
The need to return error indications to the scp event dispatcher is why 
the bottom half activities can't simply be performed in the 
callback routine (the callback routine does not return a status). 
Care should also be taken to realize that local variables in the 
unit service routine will not directly survive between the separate 
top and bottom half calls to the unit service routine.  If any such
information must be referenced in both the top and bottom half code paths
then it must either be recomputed prior to the top/bottom half check
or not stored in local variables of the unit service routine.

Run time requirements to use SIM_ASYNCH_IO.
The Posix threads API (pthreads) is required for asynchronous execution.
Most *nix platforms have these APIs available and on these platforms
simh is typically built with these available since on these platforms, 
pthreads is required for simh networking support.  Windows can also 
utilize the pthreads APIs if the compile and run time support for the
win32Pthreads package has been installed on the build system and the
run time dll is available in the execution environment.

Sample Asynch I/O device implementations.
The pdp11_rq.c module has been refactored to leverage the asynch I/O
features of the sim_disk library.  The impact to this code to adopt the
asynch I/O paradigm was quite minimal.
The pdp11_rp.c module has also been refactored to leverage the asynch I/O
features of the sim_disk library.
The pdp11_tq.c module has been refactored to leverage the asynch I/O
features of the sim_tape library.  The impact to this code to adopt the
asynch I/O paradigm was very significant. This was due to the two facts:
1) there are many different operations which can be requested of tape 
devices and 2) some of the tmscp operations required many separate 
operations on the physical device layer to perform a single tmscp request.
This issue was addressed by adding additional routines to the physical 
device layer (in sim_tape.c) which combined these multiple operations.
This approach will dovetail well with a potential future addition of 
operations on physical tapes as yet another supported tape format.

