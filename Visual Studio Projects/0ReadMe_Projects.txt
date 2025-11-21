This directory contains a set of Visual Studio 2008 build projects for the 
current simh code base.  When used (with Visual Studio Express 2008 or 
or a later Visual Studio version) it populates a directory tree under the 
BIN directory of the Simh distribution for temporary build files and 
produces resulting executables in the BIN/NT/Win32-Debug or 
BIN/NT/Win32-Release directories (depending on whether you target a Debug 
or Release build).

These projects, when used with Visual Studio 2008, will produce Release 
build binaries that will run on Windows versions from XP onward.  Building
with later versions of Visual Studio will have different Windows version
compatibility.

The Visual Studio Projects expect that various dependent packages that
the simh code depends on are available in a directory parallel to the 
simh directory.  

For Example, the directory structure should look like:

    .../simh/sim-master/VAX/vax_cpu.c
    .../simh/sim-master/scp.c
    .../simh/sim-master/Visual Studio Projects/simh.sln
    .../simh/sim-master/Visual Studio Projects/VAX.vcproj
    .../simh/sim-master/BIN/Nt/Win32-Release/vax.exe
    .../simh/windows-build/pthreads/pthread.h
    .../simh/windows-build/winpcap/WpdPack/Include/pcap.h
    .../simh/windows-build/libSDL/SDL2-2.0.3/include/SDL.h

If you have a command line version of git installed in your environment
then the windows-build repository will be downloaded and updated 
automatically.  Git for windows can be downloaded from:

    https://git-scm.com/download/win
    
If git isn't available, and you're running on a recent version of 
Windows (10 or 11), then the contents of the windows-build 
directory will be automatically downloaded and installed in the 
appropriate location relative to the simh source.

Network devices are capable of using pthreads to enhance their performance.  
To realize these benefits, you must build the desire simulator with 
USE_READER_THREAD defined.  The relevant simulators which have network 
support are all of the VAX simulators, the PDP11 simulator and the various
PDP10 simulators.

Additionally, simulators which contain devices that use the asynchronous
APIs in sim_disk.c and sim_tape.c can also achieve greater performance by
leveraging pthreads to perform blocking I/O in separate threads.  Currently
the simulators which have such devices are all of the VAX simulators and 
the PDP11.  To achieve these benefits the simulators must be built with 
SIM_ASYNCH_IO defined.

The project files in this directory build these simulators with support for
both network and asynchronous I/O.

To build any of the supported simulators you should open the simh.sln file 
in this directory or build directly from a Windows Command prompt using
the build_vstudio.bat script.

The installer for Visual Studio 2008 SP1 is available from:

https://download.microsoft.com/download/E/8/E/E8EEB394-7F42-4963-A2D8-29559B738298/VS2008ExpressWithSP1ENUX1504728.iso

Then install Visual Studio Express Visual C++ by executing VCExpress\setup.exe 
on that DVD image.  No need to install "Silverlight Runtime" or 
"Microsoft SQL Server 2008 Express Edition".  Depending on your OS Version 
you may be prompted to install an older version of .NET Framework which should 
be installed.  Once that install completes, you will need to run Windows-Update
(with "Receive updates for other Microsoft products enabled") to completely 
update the Visual Studio environment you've just installed.

Visual Studio Express 2008 will build executables that will run on all Windows 
versions from XP onward with equivalent functionality.

Note: VS2008 can readily coexist on Windows systems that also have later 
versions of Visual Studio installed.

If you are using a version of Visual Studio beyond Visual Studio 2008, then 
your later version of Visual Studio will automatically convert the Visual 
Studio 2008 project files.  You should ignore any warnings produced by the 
conversion process.

If you have a version of Visual Studio installed and want to build all the
simulators from a command prompt, the file build_vstudio.bat in the root
of the simh source tree will do that without any further interaction.  In 
fact, the best way to convert the VS2008 solution and project files is by 
using build_vstudio.bat since it will create a new solution file named 
Simh-2022.sln or Simh-2026.sln that can then be used directly by the 
Visual Studio IDE.

Almost all newer Visual Studio versions after VS2008 will build executables 
that only run on the system that built it or one running the same OS.

If you're using Visual Studio Community 2022 or 2026, and you follow these 
installation instructions, you can also build simulator executables using
the newer Visual Studio IDE and tools while not necessarily needing updated
windows_build support.  This is convenient since both VS2022 and VS2026
come up with updates possibly many times per month and once an update is
installed, the windows_update build support won't be available for the 
latest version.  This problem only affects simulators compiled in Release
mode.  Once projects are converted, by the build_vstudio.bat file directly
with the IDE, the Visual Studio 2022 or 2026 IDE for further development.

- New install
  - In the "Workloads" pane, check "Desktop development with C++" workload's
    checkbox, if not already checked.
  - Click on the tab labeled "Individual components"
  - In the "Individual components" pane:
    - Scroll down and check the box next to "MSVC v141 - VS 2017 C++ x64/x86 build tools (v14.16)"
  - Continue to customize your VS 2022 installation as needed.
  - Click on "Install" in the lower right hand corner

- Modifying an existing VS2022 installation
  - Click on the Visual Studio 2022 `Modify` button.
  - In the "Modifying --" window, click on "Individual Components"
  - Click on the tab labeled "Individual components"
  - In the "Individual components" pane:
  - In the "Individual components" pane:
    - Scroll down and check the box next to "MSVC v141 - VS 2017 C++ x64/x86 build tools (v14.16)"
  - Continue to customize your VS 2022 installation as needed.
  - Click on the "Modify" button in the lower right corner of the Window.

