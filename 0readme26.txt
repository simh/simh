Release notes for simh V2.6

1. Register arrays

The simulator has supported register arrays for some time, but their contents
were always hidden, and register arrays had names like *BUF.  Register arrays
can now be examined and modified, and the names have changed to normal form.
As a result, SAVE FILES FROM PRIOR RELEASES WILL NOT RESTORE PROPERLY, because
the previous array names won't be found.  These errors will occur AFTER main
memory has been restored, so memory contents can be salvaged; but most device
state will be lost.

2. USE_INT64 instead of _INT64

As a #define, _INT64 conflicts with some compiler implementations.  Therefore,
the enable switch for 64b has been changed to USE_INT64, e.g.,

	% cc -o pdp10 -DUSE_INT64 pdp10_*.c,scp*.c -lm

3. int64 definition defaults to long long

If 64b is specified, the default compiler declaration for int64 is 'long long',
with exceptions for Win32 (_int64) and Digital UNIX (long).

4. Real-time clock calibration

Many of the simulators now calibrate their real-time clocks to match wall
time.  This allows simulated operating systems to track wall time.

5. Calling sequence change

The calling sequence for sim_load has been changed to include the file name.
This allows simulator loaders to use different formats depending on the
extension of the load file.

