SIM_ASYNCH_IO

Theory of operation.

Features.
   - Optional Use.  Build with or without SIM_ASYNCH_IO defined and
     simulators will still build and perform correctly when run.
     Additionally, a simulator built with SIM_ASYNCH_IO defined can
     dynamically disable and reenable asynchronous operation with
     the scp commands SET NOASYNCH and SET ASYNCH respectively.
   - Consistent Save/Restore state.  The state of a simulator saved
     on a simulator with (or without) Asynch support can be restored
     on any simulator of the same version with or without Asynch
     support.
   - Optimal behavior/performance with simulator running with or
     without CPU idling enabled.
   - Consistent minimum instruction scheduling delays when operating
     with or without SIM_ASYNCH_IO.  When SIM_ASYNCH_IO is enabled,
     any operation which would have been scheduled to occur in 'n'
     instructions will still occur (from the simulated computer's
     point of view) at least 'n' instructions after it was initiated.

Benefits.
   - Allows a simulator to execute simulated instructions concurrently
     with I/O operations which may take numerous milliseconds to perform.
   - Allows a simulated device to potentially avoid polling for the
     arrival of data.  Polling consumes host processor CPU cycles which
     may better be spent executing simulated instructions or letting
     other host processes run.  Measurements made of available
     instruction execution easily demonstrate the benefits of parallel
     instruction and I/O activities.  A VAX simulator with a process
     running a disk intensive application in one process was able to
     run (in another process) 11 times the number of Dhrystone operations
     with Asynch I/O enabled vs not enabled.
   - Allows simulator clock ticks to track wall clock was precisely as
     possible under varying I/O load and activities.

SimH Libraries which provide Asynch I/O support:
   sim_disk
   sim_tape
   sim_ether
   sim_console
   sim_tmxr

Requirements to use:
The Simulator's instruction loop needs to be modified to include a single
line which checks for asynchronously arrived events.  The vax_cpu.c
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

Programming Disk and Tape devices to leverage Asynch I/O

Asynch disk and tape I/O is provided through a callback model.  The callback
is invoked when the desired I/O operation has completed.

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
routine (and the actual I/O) may have been executed.  When the callback
routine is called, it will already be on the simulator event queue with
an event time which was specified when the unit was attached or via a
call to sim_disk_set_async.  If no value has been specified then it
will have been scheduled with a delay time of 0.  If a different event
firing time is desired, then the callback completion routine should
call sim_activate_abs to schedule the event at the appropriate time.

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

Sample Asynch I/O device implementations.
The pdp11_rq.c module has been refactored to leverage the asynch I/O
features of the sim_disk library.  The impact to this code to adopt the
asynch I/O paradigm was quite minimal.
The pdp11_rp.c module has also been refactored to leverage the asynch I/O
features of the sim_disk library.  The impact to this code to adopt the
asynch I/O paradigm was also quite minimal.  After conversion a latent
bug in the VAX Massbus adapter implementation was illuminated due to the
more realistic delays to perform I/O operations.
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

Programming Console and Multiplexer devices to leverage Asynch I/O to
minimize 'unproductive' polling.

There are two goals for asynchronous Multiplexer I/O: 1) Minimize polling
to only happen when data is available, not arbitrarily on every clock tick,
and 2) to have polling actually happen as soon as data may be available.
In most cases no effort is required to add Asynch I/O support to a
multiplexer device emulation.  If a device emulation takes the normal
model of polling for arriving data on every simulated clock tick, then if
Asynch I/O is enabled, the device will operate asynchronously and behave
well.  There is one restriction in this model.  Specifically,  the device
emulation logic can't expect that there will be a particular number (clock
tick rate maybe) of invocations of a unit service routine to perform polls
in any interval of time (this is what we're trying to change, right?).
Therefore presumptions about measuring time by counting polls is not
valid.  If a device needs to manage time related activities, then the
device should create a separate unit which is dedicated to the timing
activities and which explicitly schedules a different unit service routine
for those activities as needed.  Such scheduled polling should only be
enabled when actual timing is required.

A device which is unprepared to operate asynchronously can specifically
disable multiplexer Asynch I/O for that device by explicitly defining
NO_ASYNCH_MUX at compile time.  This can be defined at the top of a
particular device emulation which isn't capable of asynch operation, or
it can be defined globally on the compile command line for the simulator.
Alternatively, if a specific Multiplexer device doesn't function correctly
under the multiplexer asynchronous environment and it will never be
revised to operate correctly, it may statically set the TMUF_NOASYNCH bit
in its unit flags field.

Some devices will need a small amount of extra coding to leverage the
Multiplexer Asynch I/O capabilities.  Devices which require extra coding
have one or more of the following characteristics:
- they poll for input data on a different unit (or units) than the unit
  which was provided when tmxr_attach was called.
- they poll for connections on a different unit than the unit which was
  provided when tmxr_attach was called.

The extra coding required for proper operation is to call
tmxr_set_line_unit() to associate the appropriate input polling unit to
the respective multiplexer line (ONLY if input polling is done by a unit
different than the unit specified when the MUX was attached). If output
polling is done on a different unit, then tmxr_set_line_output_unit()
should be called to describe that fact.

Console I/O can operate asynchronously if the simulator notifies the
tmxr/console subsystem which device unit is used by the simulator to poll
for console input and output units.  This is done by including sim_tmxr.h
in the source module which contains the console input device definition
and calling tmxr_set_console_units().  tmxr_set_console_units would usually
be called in a device reset routine.

sim_tmxr consumers:
  - Altair Z80 SIO   devices = 1, units = 1,      lines = 4,  flagbits = 8, Untested Asynch
  - HP2100 BACI      devices = 1, units = 1,      lines = 1,  flagbits = 3, Untested Asynch
  - HP2100 MPX       devices = 1, units = 10,     lines = 8,  flagbits = 2, Untested Asynch
  - HP2100 MUX       devices = 3, units = 1/16/1, lines = 16, flagbits = 4, Untested Asynch
  - I7094 COM        devices = 2, units = 4/33,   lines = 33, flagbits = 4, Untested Asynch
  - Interdata PAS    devices = 2, units = 1/32,   lines = 32, flagbits = 3, Untested Asynch
  - Nova QTY         devices = 1, units = 1,      lines = 64, flagbits = 1, Untested Asynch
  - Nova TT1         devices = 2, units = 1/1,    lines = 1,  flagbits = 1, Untested Asynch
  - PDP-1 DCS        devices = 2, units = 1/32,   lines = 32, flagbits = 0, Untested Asynch
  - PDP-8 TTX        devices = 2, units = 1/4,    lines = 4,  flagbits = 0, Untested Asynch
  - PDP-11 DC        devices = 2, units = 1/16,   lines = 16, flagbits = 5, Untested Asynch
  - PDP-11 DL        devices = 2, units = 1/16,   lines = 16, flagbits = 3, Untested Asynch
  - PDP-11 DZ        devices = 1, units = 1/1,    lines = 32, flagbits = 0, Good Asynch
  - PDP-11 VH        devices = 1, units = 4,      lines = 32, flagbits = 4, Good Asynch
  - PDP-18b TT1      devices = 2, units = 1/16,   lines = 16, flagbits = 0, Untested Asynch
  - SDS MUX          devices = 2, units = 1/32,   lines = 32, flagbits = 0, Untested Asynch
  - sim_console                                                             Good Asynch

Program Clock Devices to leverage Asynch I/O

simh's concept of time is calibrated by counting the number of
instructions which the simulator can execute in a given amount of wall
clock time.  Once this is determined, the appropriate value is continually
recalibrated and used throughout a simulator to schedule device time
related delays as needed.  Historically, this was fine until modern
processors started having dynamically variable processor clock rates.
On such host systems, the simulator's concept of time passing can vary
drastically.  This dynamic adjustment of the host system's execution rate
may cause dramatic drifting of the simulated operating system's concept
of time.  Once all devices are disconnected from the calibrated clock's
instruction count, the only concern for time in the simulated system is
that it's clock tick be as accurate as possible.  This has worked well
in the past, however each simulator was burdened with providing code
which facilitated managing the concept of the relationship between the
number of instructions executed and the passage of wall clock time.
To accomodate the needs of activities or events which should be measured
against wall clock time (vs specific number of instructions executed),
the simulator framework has been extended to specifically provide event
scheduling based on elapsed wall time. A new API can be used by devices
to schedule unit event delivery after the passage of a specific amount
of wall clock time.  The api sim_activate_after() provides this
capability.  This capability is not limited to being available ONLY when
compiling with SIM_SYNCH_IO defined.  When SIM_ASYNCH_IO is defined, this
facility is implemented by a thread which drives the delivery of these
events from the host system's clock ticks (interpolated as needed to
accomodate hosts with relatively large clock ticks).  When SIM_ASYNCH_IO
is not defined, this facility is implemented using the traditional simh
calibrated clock approach.  This new approach has been measured to provide
clocks which drift far less than the drift realized in prior simh versions.
Using the released simh v3.9-0 vax simulator with idling enabled, the clock
drifted some 4 minutes in 35 minutes time (approximately 10%).  The same OS
disk also running with idling enabled booted for 4 hours had less that 5
seconds of clock drift (approximately 0.03%).

Co-Scheduling Clock and Multiplexer (or other devices)

Many simulator devices have needs to periodically executed with timing on the
order of the simulated system's clock ticks.  There are numerous reasons for
this type of execution.  Meanwhile, many of these events aren't particular
about exactly when they execute as long as they execute frequently enough.
Frequently executing events has the potential to interfere with a simulator's
attempts to idle when the simulated system isn't actually doing useful work.

Interactions with attempts to 'co-schedule' multiplexer polling with clock
ticks can cause strange simulator behaviors.  These strange behaviors only
happen under a combination of conditions:
  1) a multiplexer device is defined in the simulator configuration,
  2) the multiplexor device is NOT attached, and thus is not being managed by
     the asynchronous multiplexer support
  3) the multiplexer device schedules polling (co-scheduled) when not
     attached (such polling will never produce any input, so this is probably
     a bug).
In prior simh versions support for clock co-scheduling was implemented
separately by each simulator, and usually was expressed by code of the form:
    sim_activate (uptr, clk_cosched (tmxr_poll));
As a part of asynchronous timer support, the simulator framework has been
extended to generically provide clock co-scheduling support.  The use of this
new capability requires an initial call (usually in the clock device reset
routing) of the form:
    sim_register_clock_unit (&clk_unit);
Once the clock unit has been registered, co-scheduling is achieved by replacing
the earlier sim_activate with the following:
    sim_clock_coschedule (&dz_unit, tmxr_poll);

Run time requirements to use SIM_ASYNCH_IO.
The Posix threads API (pthreads) is required for asynchronous execution.
Most *nix platforms have these APIs available and on these platforms
simh is typically built with these available since on these platforms,
pthreads is required for simh networking support.  Windows can also
utilize the pthreads APIs if the compile and run time support for the
win32Pthreads package has been installed on the build system.

