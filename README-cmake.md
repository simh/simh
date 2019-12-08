# Build `simh` using CMake

<!-- TOC -->

- [Build simh using CMake](#build-simh-using-cmake)
  - [Intended Audience: Developers](#intended-audience-developers)
  - [Why CMake?](#why-cmake)
  - [Building simh With CMake](#building-simh-with-cmake)
    - [Before You Begin: Prerequisites](#before-you-begin-prerequisites)
      - [Supported C Compilers](#supported-c-compilers)
      - [Tool and Library Dependencies](#tool-and-library-dependencies)
    - [Building simh](#building-simh)
      - [Linux/*nix platforms](#linuxnix-platforms)
    - [Configuration Options](#configuration-options)
    - [CMake quickstart](#cmake-quickstart)
    - [Notes for Windows Visual Studio](#notes-for-windows-visual-studio)
  - [CMake Generators](#cmake-generators)
  - [Developer Notes](#developer-notes)
    - [Build Directories](#build-directories)
    - [add_simulator: Compiling simulators](#addsimulator-compiling-simulators)
  - [Motivation](#motivation)

<!-- /TOC -->

## Intended Audience: Developers

This document assumes that you, the reader, are a knowledgeable software developer:

  - You have a C/C++ compiler installed on your host or virtual machine
  - You know how to develop and build software artifacts (executables, shared
    objects, dynamically linked libraries).
  - You can install software development dependencies on your host or virtual machine.
    - Linux and *nix platforms: You know how to use `rpm`, `apt`, `pacman` or the appropriate
      package manager your Linux or *nix uses.
    - Windows: You use a package manager such as [Scoop][scoop] or [Chocolatey][chocolatey],
      or you are comfortable with installing software directly.
  - You known what tools such as `make`, `bison` and `flex` do.
  - You can diagnose your software development environment's issues/problems.
  
If you are not a reasonably knowledgeable software developer or are new to software
development, _PLEASE USE THE PRE-COMPILED SIMULATORS_. 

_Windows developers_: This document distinctly prefers the [Scoop][scoop]
package manager. While this is the author's preference, it does not have to be
yours. You always have the option to use another package manager, such as
[Chocolatey][chocolatey], or direct software installation from the package's
download site. If you use another package manager or directly install software,
you're ultimately responsible for diagnosing issues with your software
development environment.

## Why CMake?

[CMake][cmake] is a cross-platform "meta" build system that provides similar
functionality to GNU _autotools_ within a more integrated and platform-agnostic
framework. A sample of the supported build environments include:

  - Unix Makefiles
  - [MinGW Makefiles][mingw64]
  - [Ninja][ninja]
  - MS Visual Studio solutions (2015, 2017, 2019)
  - IDE build wrappers ([Sublime Text](https://www.sublimetext.com) and [CodeBlocks](http://www.codeblocks.org))

[CMake][cmake] is not intended to supplant, supercede or replace the existing
`simh` build infrastructure. If you like the existing `makefile` "poor man's
configure" approach, there's nothing to stop you from using it. [CMake][cmake]
is a parallel build system to the `simh` `makefile` and is just another way to
build `simh`'s simulators.

## Building `simh` With CMake

### Before You Begin: Prerequisites

#### Supported C Compilers

| Compiler                 | Notes                                                 |
| ------------------------ | ----------------------------------------------------- |
| _GNU C Compiler (gcc)_   | This is one of two compilers against which `simh` is routinely compiled. `gcc` is the default compiler for many Linux and *nix platforms; it can also be used for [Mingw-w64][mingw64]-based builds on Windows. |
| _Microsoft Visual C/C++_ | This is the other compiler against which `simh` is routinely compiled. |
| _CLang/LLVM_             | Building `simh` with `clang` and [CMake][cmake] is untested. It will probably work on Linux and *nix platforms without issues. This is completely untested on Windows. |

Your mileage may vary on other platforms where [CMake][cmake] is supported.

#### Tool and Library Dependencies

The table below lists the development tools and packages needed to build the
`simh` simulators, with corresponding `apt`, `rpm` and `Scoop` package names,
where available. Blank names indicate that the package is not offered via the
respective package manager.

| Prerequisite             | Category   | `apt` package   | `rpm` package  | [Scoop][scoop] package | Notes  |
| ------------------------ | ---------- | --------------- | -------------- | ---------------------- | :----: |
| [CMake][cmake]           | Dev. tool  | cmake           | cmake          | cmake                  | (1)    |
| [Git][gitscm]            | Dev. tool  | git             | git            | git                    | (1, 2) |
| [bison][bison]           | Dev. tool  | bison           | bison          | winflexbison           | (3)    |
| [flex][flex]             | Dev. tool  | flex            | flex           | winflexbison           |        |
| [Npcap][npcap]           | Runtime    |                 |                |                        | (4)    |
| [zlib][zlib]             | Dependency | zlib1g-dev      | zlib-devel     |                        | (5)    |
| [pcre2][pcre2]           | Dependency | libpcre2-dev    | pcre2-devel    |                        | (5)    |
| [libpng][libpng]         | Dependency | libpng-dev      | libpng-devel   |                        | (5)    |
| [FreeType][FreeType]     | Dependency | libfreetype-dev | freetype-devel |                        | (5)    |
| [libpcap][libpcap]       | Dependency | libpcap-dev     | libpcap-devel  |                        | (5)    |
| [SDL2][SDL2]             | Dependency | libsdl2-dev     | SDL2-devel     |                        | (5)    |
| [SDL_ttf][SDL2_ttf]      | Dependency | libsdl2-ttf-dev |                |                        | (5)    |
| [pthreads4w][pthreads4w] | Dependency |                 |                |                        | (6)    |

_Notes_:

(1) Required development tool.

(2) Tool might already be installed on your system.

(3) Tool might already be installed in Linux and *nix systems; [winflexbison][winflexbison] is package that installs both the
    [bison][bison] and [flex][flex] tools for Windows developers. [bison][bison] and [flex][flex] are _only_ required to compile
    the [libpcap][libpcap] packet capture library. If you do not need emulated native Ethernet networking, [bison][bison] and
    [flex][flex] are optional.

(4) [Npcap][npcap] is a Windows packet capture device driver. It is a runtime requirement used by simulators that emulate native Ethernet networking
    on Windows.

(5) If the package name is blank or you do not have the package installed, the `simh` [CMake][cmake] build process will download and compile the
    library dependency from source. [Scoop][scoop] does not provide these development library dependencies, so all dependencies will be built from
    source on Windows. Similarly, `SDL_ttf` support may not be available for RPM package-based systems (RedHat, CentOS, ArchLinux, ...), but will
    be compiled from source.

(6) [pthreads4w][pthreads4w] provides POSIX `pthreads` API using a native Windows implementation. This dependency is built only when using the
    _Visual Studio_ compiler. [Mingw-w64][mingw64] provides a `pthreads` library as a part of the `gcc` compiler toolchain.

### Building `simh`

It is basically up to you to choose the build system with which you're most comfortable and probably already use. [CMake][cmake] generates the files and
environment for your preferred build system from the `CMakeFiles.txt` configuration file. [CMake Generators](#cmake-generators) has more information
about which build systems your [CMake][cmake] installation supports.

The process to build `simh` with [CMake][cmake] follows the steps below:

1. Clone the `simh` Git repository, if you haven't done so already.
2. Create a subdirectory in which you will build the simulators.
 
    _Note_: This [CMake][cmake] configuration **will not allow you** to configure, build or compile `simh` in the source
    tree. Building `simh` simulators with [CMake][cmake] ***must*** be done in a separate directory, which is usually a
    subdirectory within the source tree. An informal convention for subdirectory names is `cmake-<something>`, e.g.,
    `cmake-unix` for Unix Makefile builds and `cmake-ninja` for [Ninja][ninja]-based builds (`.gitignore` ignores all
    subdirectories that start with `cmake-`.)

3. Compile the dependencies
  - Detect which dependencies need to be built
  - Generate the build environment to build the dependencies (if any)
  - Build dependencies (if any)
4. Compile the simulators
  - Configure and generate the build environment for the simulators
  - Build the simulators

#### Linux/*nix platforms

The following shell 

``` shell
# Install Ubuntu/Linux dependencies
$ sudo apt install cmake bison flex zlib1g-dev libpcre2-dev libpng-dev libpcap-dev libsdl2-dev libsdl2-ttf-dev

# Clone the simh repository (if you haven't done so already)
$ git clone  https://github.com/simh/simh.git simh
$ cd simh

# make a build directory and generate Unix Makefiles
$ mkdir cmake-unix
$ cd cmake-unix
$ cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..

# First time around, d/l and build dependency libraries, if they're
# not found (usually the case on Windows, YMMV on *nix):
$ cmake --build . --config Release

# Second time around: Reconfigure using the newly built dependency libraries
# and build simh's simulators
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
$ cmake --build . --config Release

# Alternatively, you can go into your favorite IDE and build from within your
# IDE. Or, with the Ninja build system, you could just type `ninja` instead of
# `cmake --build . --configure Release`. Or if you used "Unix Makefiles", you
# could just type `make`. (See the pattern yet?)

# Oh, so you want to run stuff? From inside the build?
# Need to add the build-stage/bin directory to your PATH:
$ PATH=`pwd`/build-stage/bin:$PATH

# For Windows Powershell:
# $env:PATH="$(Get-Location)\build-stage\bin;C:\Windows\System32\Npcap;$env:PATH"

# Run the vax simulator from inside the build:
$ VAX/vax

# Install will install to the `BIN` directory inside the source tree:
$ cmake --install .
```

### Configuration Options

The default `simh` [CMake][cmake] configuration is _"Batteries Included"_: all
options are enabled. The configuration options for `simh-cmake` generally
mirror those in the `makefile`:

* `WITH_NETWORK`: Enable (=1)/disable (=0) simulator networking support. (def: enabled)
* `WITH_PCAP`: Enable (=1)/disable (=0) libpcap (packet capture) support. (def: enabled)
* `WITH_SLIRP`: Enable (=1)/disable (=0) SLIRP network support. (def: enabled)
* `WITH_VIDEO`: Enable (=1)/disable (=0) simulator display and graphics support (def: enabled)
* `WITH_ASYNC`: Enable (=1)/disable (=0) simulator asynchronous I/O (def: enabled)
* `PANDA_LIGHTS`: Enable (=1)/disable (=0) KA-10/KI-11 simulator's Panda display. (def: disabled)
* `DONT_USE_ROMS`: Enable (=1)/disable (=0) building hardcoded support ROMs. (def: disabled)
* `ENABLE_CPPCHECK`: Enable (=1)/disable (=0) [cppcheck][cppcheck] static code checking rules.

[CMake][cmake] enables (or disables) options at configuration time:

```shell
# Assuming that you are in the cmake-ninja build subdirectory already.
# Remove the CMakeCache.txt file if you are reconfiguring your build system:
$ rm -f CMakeCache.txt

# Then reconfigure:
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_NETWORK=Off -DENABLE_CPPCHECK=Off

# Alteratively ("0" and "Off" are equivalent)
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_NETWORK=0 -DENABLE_CPPCHECK=0
```

### CMake quickstart

`simh-cmake` is a `CMake` "_superbuild_": It first downloads and builds
dependency libraries (SDL2, SDL2-ttf, pcre, zlib, freetype, ...), if they are
missing. These dependency libraries are staged in the `CMake` build directory.
Once all of the dependency libraries are satisfied, you reconfigure
`simh-cmake` to build the simulators. Unless you nuke the build directory, you
should only have to build the dependency libraries once.

```shell
# clone (if you haven't done so already)
$ git clone  https://github.com/simh/simh.git simh
$ cd simh

# make a build directory and generate a build environment (ex: Ninja on Windows 10)
$ mkdir cmake-ninja
$ cd cmake-ninja
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# First time around, d/l and build dependency libraries, if they're
# not found (usually the case on Windows, YMMV on *nix):
$ cmake --build . --config Release

# Second time around: Reconfigure using the newly built dependency libraries
# and build simh's simulators
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
$ cmake --build . --config Release

# Alternatively, you can go into your favorite IDE and build from within your
# IDE. Or, with the Ninja build system, you could just type `ninja` instead of
# `cmake --build . --configure Release`. Or if you used "Unix Makefiles", you
# could just type `make`. (See the pattern yet?)

# Oh, so you want to run stuff? From inside the build?
# Need to add the build-stage/bin directory to your PATH:
$ PATH=`pwd`/build-stage/bin:$PATH

# For Windows Powershell:
# $env:PATH="$(Get-Location)\build-stage\bin;C:\Windows\System32\Npcap;$env:PATH"

# Run the vax simulator from inside the build:
$ VAX/vax

# Install will install to the `BIN` directory inside the source tree:
$ cmake --install .
```

### Notes for Windows Visual Studio

The source tree versions of the Visual Studio project files/solutions build a
32-bit executable. To do this with [CMake][cmake], you have to specify the target
architecture at confiugration time as follows:

``` shell
# Visual Studio 2019 has an argument for architecture, "-A", and defaults
# to a 64-bit architecture:
PS> cmake -G "Visual Studio 16 2019" -A Win32 ..

# Prior Visual Studios don't and default to Win32
PS> cmake -G "Visual Studio 15 2017" ..
PS> cmake -G "Visual Studio 14 2015" ..
PS> cmake -G "Visual Studio 12 2013" ..
PS> cmake -G "Visual Studio 11 2012" ..
PS> cmake -G "Visual Studio 10 2010" ..
PS> cmake -G "Visual Studio 9 2008" ..
```

## `CMake` Generators

[CMake][cmake] generates environments for a wide variety of build systems. The available list of build systems in your installed [CMake][cmake] is always available via:

``` shell
$ cmake --help
# (Some help text elided here for brevity...)

Generators

The following generators are available on this platform (* marks default):
* Visual Studio 16 2019        = Generates Visual Studio 2019 project files.
                                 Use -A option to specify architecture.
  Visual Studio 15 2017 [arch] = Generates Visual Studio 2017 project files.
                                 Optional [arch] can be "Win64" or "ARM".
  Visual Studio 14 2015 [arch] = Generates Visual Studio 2015 project files.
                                 Optional [arch] can be "Win64" or "ARM".
  Visual Studio 12 2013 [arch] = Generates Visual Studio 2013 project files.
                                 Optional [arch] can be "Win64" or "ARM".
  Visual Studio 11 2012 [arch] = Generates Visual Studio 2012 project files.
                                 Optional [arch] can be "Win64" or "ARM".
  Visual Studio 10 2010 [arch] = Generates Visual Studio 2010 project files.
                                 Optional [arch] can be "Win64" or "IA64".
  Visual Studio 9 2008 [arch]  = Generates Visual Studio 2008 project files.
                                 Optional [arch] can be "Win64" or "IA64".
  Borland Makefiles            = Generates Borland makefiles.
  NMake Makefiles              = Generates NMake makefiles.
  NMake Makefiles JOM          = Generates JOM makefiles.
  MSYS Makefiles               = Generates MSYS makefiles.
  MinGW Makefiles              = Generates a make file for use with
                                 mingw32-make.
  Unix Makefiles               = Generates standard UNIX makefiles.
  Green Hills MULTI            = Generates Green Hills MULTI files
                                 (experimental, work-in-progress).
  Ninja                        = Generates build.ninja files.
  Watcom WMake                 = Generates Watcom WMake makefiles.
  CodeBlocks - MinGW Makefiles = Generates CodeBlocks project files.
  CodeBlocks - NMake Makefiles = Generates CodeBlocks project files.
  CodeBlocks - NMake Makefiles JOM
                               = Generates CodeBlocks project files.
  CodeBlocks - Ninja           = Generates CodeBlocks project files.
  CodeBlocks - Unix Makefiles  = Generates CodeBlocks project files.
  CodeLite - MinGW Makefiles   = Generates CodeLite project files.
  CodeLite - NMake Makefiles   = Generates CodeLite project files.
  CodeLite - Ninja             = Generates CodeLite project files.
  CodeLite - Unix Makefiles    = Generates CodeLite project files.
  Sublime Text 2 - MinGW Makefiles
                               = Generates Sublime Text 2 project files.
  Sublime Text 2 - NMake Makefiles
                               = Generates Sublime Text 2 project files.
  Sublime Text 2 - Ninja       = Generates Sublime Text 2 project files.
  Sublime Text 2 - Unix Makefiles
                               = Generates Sublime Text 2 project files.
  Kate - MinGW Makefiles       = Generates Kate project files.
  Kate - NMake Makefiles       = Generates Kate project files.
  Kate - Ninja                 = Generates Kate project files.
  Kate - Unix Makefiles        = Generates Kate project files.
  Eclipse CDT4 - NMake Makefiles
                               = Generates Eclipse CDT 4.0 project files.
  Eclipse CDT4 - MinGW Makefiles
                               = Generates Eclipse CDT 4.0 project files.
  Eclipse CDT4 - Ninja         = Generates Eclipse CDT 4.0 project files.
  Eclipse CDT4 - Unix Makefiles= Generates Eclipse CDT 4.0 project files.
```
## Developer Notes

### Build Directories

The `simh` [CMake][cmake]-based build infrastructure _does not support_ source tree
builds. All builds have to occur outside of the source tree. The name of the
build directory is not significant and does not have to be a subdirectory within
the source tree.

As a matter of convention, however, build subdirectories tend to have the form
`cmake-<build system>`, e.g., `cmake-ninja` for a [Ninja][ninja] build or
`cmake-vs2019` for a Visual Studio 2019 build. You can have as many of these
build subdirectories as you deem necessary. _Note_: you need to be careful
about your `PATH` environment variable when running simulators from within the
build directory.

The build directory has the following structure:

```
cmake-ninja
|- CMakeCache.txt               # CMake cache: Remove to completely reconfigure
|- build-stage                  # Staging area for dependencies
|  |- bin                       # Dependency executables and DLLs
|  |- include                   # Headers
|  |- lib                       # Libraries
|  |- man
|  |- share
|- 3B2                          # 3b2 simulator build
|  |- 3b2{.exe}                 # 3b2 simulator executabe
|  |- ...                       # 3b2 build products
|- ALTAIR
|- ...
|- VAX                          # VAX simulators build
|  |- ...
```

### `add_simulator`: Compiling simulators

If you hack the simulators and add (or remove) source files, you will have to
update the affected simulator's `CMakeLists.txt`. 

The `add_simulator` function sets up the individual simulator executable. For
example, in the `3B2` `CMakeLists.txt`, the 3b2 simulator's executable
`add_simulator` looks like:

``` cmake
add_simulator(3b2
    SOURCES
        3b2_cpu.c 
        3b2_mmu.c
        3b2_iu.c 
        3b2_if.c
        3b2_id.c 
        3b2_dmac.c
        3b2_sys.c 
        3b2_io.c
        3b2_ports.c
        3b2_ctc.c
        3b2_ni.c 
        3b2_mau.c
        3b2_sysdev.c
    INCLUDES
	    3B2
    FULL64
    BUILDROMS)
```

- `add_simulator`'s first argument is the simulator's executable name: `3b2`.
  This generates an executable named `3b2` on *nix platforms or `3b2.exe` on
  Windows platforms.
  
- Argument list keywords: `SOURCES`, `INCLUDES`, `DEFINES`
    - `SOURCES` enumerates the source files that comprise the simulator. The
      file names are relative to the simulator's source directory. In the
      `3b2`'s case, this is relative to the `3B2/` subdirectory where
      `3B2/CMakeLists.txt` is located.
    - `INCLUDES` enumerates the include directories where the header files
      needed by the simulator are located, i.e., subdirectories that follow the
      compiler's `-I` flag). These subdirectories are relative to the top level
      `simh` directory.
    - `DEFINES` enumerates command line manifest constants, i.e., values that
      follow the compiler's `-D` flags.

- Option keywords:
  - `INT64`: 64-bit integers, 32-bit pointers
  - `FULL64`: 64-bit integers, 64-bit pointers
  - `BUILDROMS`: Simulator depends on the `BuildROMs` utility to build the
    built-in boot ROMs.
  - `VIDEO`: Simulator video support.

Putting it all together:

``` cmake
add_simulator(my_simulator
    SOURCES
        my_sim.c
        my_sim_devs.c
        my_sim_support.c
    INCLUDES
        my_simulator                # Relative to the top-level source directory
        ${CMAKE_SOURCE_DIR}/PDP8    # Add top-level PDP8 subdirectory, explicit path
        VAX/                        # Add top-level VAX subdirectory
    DEFINES
        VM_SIMH_SIMULATOR
        SPECIAL_VALUE=0xdeadbeef
    INT64                           # 64-bit integer, 32-bit pointer option
    FULL64                          # 64-bit integer, 64-bit pointer option
    BUILDROMS                       # Boot ROM is built-in header file
    VIDEO                           # Video support
)
```


## Motivation

**Note**: This is personal opinion. There are many personal opinions like this,
but this one is mine.

`simh` is a difficult package to build from scratch, especially if you're on a
Windows platform. There's a separate directory tree you have to check out that
has to sit parallel to the main `simh` codebase (aka `sim-master`.) It's an
approach that I've used in the past and it's hard to maintain (viz. a recent
commit log entry that says that `git` forks should _not_ fork the
`windows-build` subdirectory.) In my That's not a particularly clean or intuitive way
of building software. It's also prone to errors and doesn't lend itself to
upgrading dependency libraries.

[cmake]: https://cmake.org
[cppcheck]: http://cppcheck.sourceforge.net/
[ninja]: https://ninja-build.org/
[scoop]: https://scoop.sh/
[gitscm]: https://git-scm.com/
[bison]: https://www.gnu.org/software/bison/
[flex]: https://github.com/westes/flex
[npcap]: https://nmap.org/npcap/
[zlib]: https://www.zlib.net
[pcre2]: https://pcre.org
[libpng]: http://www.libpng.org/pub/png/libpng.html
[FreeType]: https://www.freetype.org/
[libpcap]: https://www.tcpdump.org/
[SDL2]: https://www.libsdl.org/
[SDL2_ttf]: https://www.libsdl.org/projects/SDL_ttf/
[mingw64]: https://mingw-w64.org/
[winflexbison]: https://github.com/lexxmark/winflexbison
[pthreads4w]: https://github.com/jwinarske/pthreads4w
[chocolatey]: https://chocolatey.org/
