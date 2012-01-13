This dirctory contains a set of Visual Studio 2008 build projects for the 
current simh code base.  When used (with Visual Studio Express 2008 or 
Visual Studio Express 2010) it populates a directory tree under the BIN 
directory of the Simh distribution for temporary build files and produces 
resulting executables in the BIN/NT/Win32-Debug or BIN/NT/Win32-Release 
directories (depending on whether you target a Debug or Release build).  
It expects that a winpcap developer pack zip file is expanded in a directory 
parallel to the simh directory.  

For Example, the directory structure should look like:

    .../simh/simhv38-2-rc1/VAX/vax_cpu.c
    .../simh/simhv38-2-rc1/scp.c
    .../simh/simhv38-2-rc1/Visual Studio Projects/simh.sln
    .../simh/simhv38-2-rc1/Visual Studio Projects/VAX.vcproj
    .../simh/simhv38-2-rc1/BIN/Nt/Win32-Release/vax.exe
    .../simh/winpcap/WpdPack/Include/pcap.h


The winpcap developer pack can be found at:
       http://www.winpcap.org/devel.htm

The latest version of the WinPcap developer's pack is Version 4.1.2

Some features can be enabled if the pthreads API is available and contained 
also in a parallel place in the directory structure.


    .../simh/pthreads/Pre-built.2/include/include/pthreads.h


To install pthreads API, create the directory:

    .../simh/pthreads/

download the file: 
    ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-8-0-release.exe
to that directory and execute it.  Click on the Extract button.
Once installed, When running a simulator with pthreads support enabled, you 
will need a copy of the DLL file (simh\pthreads\Pre-built.2\lib\pthreadVC2.dll)
to exist in either the %windir%\System32 directory (or %windir%\SysWOW64 on 
x64 Windows environments) or your working directory while running a simh 
simulator.  The default working directory for included project files is the 
"Visual Studio Projects" directory.


Network devices are capable of using pthreads to enhance their performance.  
To realize these benefits, you must build the desire simulator with 
USE_READER_THREAD defined.  The relevant simulators which have network 
support are VAX, VAX780 and PDP11.

Additionally, simulators which contain devices which use the asynchronous
APIs in sim_disk.c and sim_tape.c can also achieve greater performance by
leveraging pthreads to perform blocking I/O in separate threads.  Currently
the simulators which have such devices are VAX, VAX780 and PDP11.  To 
achieve these benefits the simulators must be built with SIM_ASYNCH_IO 
defined.

