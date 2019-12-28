if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message("")
  message("*** Do NOT build or generate CMake artifacts in the SIMH source directory! ***")
  message("")
  message("Create a subdirectory and build in that subdirectory, e.g.:")
  message("")
  message("  $ mkdir cmake-build")
  message("  $ cd cmake-build")
  message("  $ cmake -G \"your generator here\" ..")
  message("")
  message(FATAL_ERROR "Preventing in-tree source build.")
endif ()

include(CheckCSourceCompiles)
include(CheckCCompilerFlag)
include (ExternalProject)
find_package(PkgConfig)

cmake_minimum_required(VERSION 3.10)

set(SIMH_VERSION "4.0.0")
project(simh VERSION "${SIMH_VERSION}" LANGUAGES C)

##-- Options --##
option(WITH_NETWORK
       "Enable (=1)/disable (=0) simulator networking support. (def: enabled)"
       TRUE)
option(WITH_PCAP
       "Enable (=1)/disable (=0) libpcap (packet capture) support. (def: enabled)"
       TRUE)
option(WITH_SLIRP
       "Enable (=1)/disable (=0) SLIRP network support. (def: enabled)"
       TRUE)
option(WITH_VIDEO
       "Enable (=1)/disable (=0) simulator display and graphics support (def: enabled)"
       TRUE)
option(WITH_ASYNC
       "Enable (=1)/disable (=0) simulator asynchronous I/O (def: enabled)"
       TRUE)
option(PANDA_LIGHTS
       "Enable (=1)/disable (=0) KA-10/KI-11 simulator's Panda display. (def: disabled)"
       FALSE)
option(DONT_USE_ROMS
       "Enable (=1)/disable (=0) building support ROMs. (def: disabled)"
       FALSE)
option(ENABLE_CPPCHECK
       "Enable (=1)/disable (=0) 'cppcheck' static code analysis. (def: disabled.)"
       FALSE)

# For 64-bit builds (and this is especially true for MSVC), set the library
# architecture.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # For the WinPcap and Npcap SDKs, the Lib subdirectory of the top-level
    # directory contains 32-bit libraries; the 64-bit libraries are in the
    # Lib/x64 directory.
    #
    # The only way to *FORCE* CMake to look in the Lib/x64 directory
    # without searching in the Lib directory first appears to be to set
    # CMAKE_LIBRARY_ARCHITECTURE to "x64".
    #
    if (MSVC OR MINGW)
        set(CMAKE_C_LIBRARY_ARCHITECTURE "x64")
        set(CMAKE_LIBRARY_ARCHITECTURE "x64")
    else (EXISTS /usr/lib/x86_64-linux-gnu)
        # Linux-ism
        set(CMAKE_C_LIBRARY_ARCHITECTURE "x86_64-linux-gnu")
        set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-linux-gnu")
    endif ()
endif()

# SIMH_DEP_TOPDIR: This is the top-level directory where dependencies live:
set(SIMH_DEP_TOPDIR ${CMAKE_BINARY_DIR}/build-stage)
message(STATUS "SIMH_DEP_TOPDIR is ${SIMH_DEP_TOPDIR}")

message(STATUS "Creating build-stage directory hierarchy (include, lib, bin)")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory build-stage build-stage/include build-stage/lib build-stage/bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

## Default install location is ${CMAKE_SOURCE_DIR}/BIN if not otherwise set
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/" CACHE PATH "${CMAKE_SOURCE_DIR} default install prefix" FORCE)
  set(CMAKE_INSTALL_BINDIR BIN CACHE PATH "BIN default binary install directory" FORCE)

  if (WIN32 AND MSVC)
    set(CMAKE_INSTALL_BINDIR "BIN/Win32/$<CONFIG>" CACHE PATH "BIN/Win32/<config> default binary install directory" FORCE)
  endif ()
endif()

# Places to look for CMake modules/includes
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Places where CMake should look for dependent package configuration fragments and artifacts:
set(SIMH_PREFIX_PATH_LIST ${SIMH_DEP_TOPDIR})
list(APPEND CMAKE_PREFIX_PATH ${SIMH_PREFIX_PATH_LIST})

set(SIMH_INCLUDE_PATH_LIST ${SIMH_DEP_TOPDIR})
list(APPEND CMAKE_INCLUDE_PATH ${SIMH_INCLUDE_PATH_LIST})

# Make sure we can include and link from the top level source...
include_directories(${CMAKE_SOURCE_DIR} ${SIMH_DEP_TOPDIR}/include)
link_directories(${SIMH_DEP_TOPDIR}/lib)

# A bunch of Windows customizations
if (WIN32)
    set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${SIMH_DEP_TOPDIR}/lib)
    set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${SIMH_DEP_TOPDIR}/include)

    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    if (MSVC_VERSION GREATER_EQUAL 1920)
	    add_compile_definitions(USE_ISO_C99_NAMES)
    endif ()
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # For some reason, the MSVC solution builds as 32-bit, but none of the
    # *nix platforms do.
    #
    # If 32-bit compiles have to be added back, uncomment the following 2 lines:
    #
    # add_compile_options("-m32")
    # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
endif (WIN32)

# Tweak GNU's flags
set(EXTRA_CFLAGS "")
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    include(fpintrin)
    # Turn on warnings about strict overflow/potential overflows.
    if (NOT DEFINED CMAKE_BUILD_TYPE OR "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        ## LIST(APPEND EXTRA_CFLAGS "-Wall" "-fno-inline" "-fstrict-overflow" "-Wstrict-overflow=3")

        # 'O3' optimization and strict overflow cause all kinds of simulator issues, especially inside
        # the VAX simulators. Reduce optimization and ensure strict overflow is turned off.
        string(REPLACE " " ";" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
        list(REMOVE_ITEM CMAKE_C_FLAGS_RELEASE "-O3")
        list(REMOVE_ITEM CMAKE_C_FLAGS_RELEASE "-fstrict-overflow")
        list(INSERT CMAKE_C_FLAGS_RELEASE 0 "-O2" "-fno-strict-overflow")
        string(REPLACE ";" " " CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    endif ()
endif (CMAKE_C_COMPILER_ID STREQUAL "GNU")

# Find packages, arrange for dependency download/compile/install:
#
# SIMH_BUILD_DEPS is a list that holds dependencies that we need to build,
# which is our super build approach.
set(SIMH_BUILD_DEPS)

set(THREADING_PKG_STATUS "unknown")
set(ZLIB_PKG_STATUS "unknown")
set(PCRE_PKG_STATUS "unknown")
set(VIDEO_PKG_STATUS "unknown")
set(NETWORK_PKG_STATUS "unknown")

if (WITH_ASYNC)
    include(pthreads-dep)
else (WITH_ASYNC)
    set(THREADING_PKG_STATUS "skipped")
endif (WITH_ASYNC)

include(zlib-dep)
include(pcre-dep)

if (WITH_VIDEO)
    include(video-deps)
    set(BUILD_WITH_VIDEO TRUE)
else (WITH_VIDEO)
    set(VIDEO_PKG_STATUS "skipped")
    set(BUILD_WITH_VIDEO FALSE)
endif (WITH_VIDEO)

if (WITH_NETWORK)
    include(network-deps)
    set(BUILD_WITH_NETWORK TRUE)
else (WITH_NETWORK)
    set(NETWORK_PKG_STATUS "skipped")
    set(BUILD_WITH_NETWORK FALSE)
endif (WITH_NETWORK)

set(CPPCHECK_STATUS "disabled.")

find_program(cppcheck_cmd NAMES cppcheck)
if (cppcheck_cmd)
    list(APPEND cppcheck_cmd
         "--language=c"
         "--enable=warning,style,performance,portability,information,missingInclude"
         "--suppress=missingIncludeSystem"
         "--inline-suppr"
         "--std=c99"
         "--force")
    set(CPPCHECK_STATUS "enabled.")
    if (WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND cppcheck_cmd
                 "--platform=win64")
            set(CPPCHECK_STATUS "enabled, Win64 platform.")
        else ()
            list(APPEND cppcheck_cmd
                 "--platform=win32A")
            set(CPPCHECK_STATUS "enabled, Win32 ASCII platform.")
        endif ()
    endif ()
else (cppcheck_cmd)
    set(CPPCHECK_STATUS "'cppcheck' not installed.")
endif ()

message(STATUS "")
message(STATUS "Libraries and features:")
message(STATUS "  Thread support .................. ${THREADING_PKG_STATUS}")
message(STATUS "  zlib ............................ ${ZLIB_PKG_STATUS}")
message(STATUS "  Perl-Compatible RegExps.......... ${PCRE_PKG_STATUS}")
message(STATUS "  PNG, Freetype, SDL2, SDL2_ttf ... ${VIDEO_PKG_STATUS}")
message(STATUS "  Packet Capture (PCAP) ........... ${NETWORK_PKG_STATUS}")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  Video/graphics support ....... ${BUILD_WITH_VIDEO}")
message(STATUS "  Networking ................... ${BUILD_WITH_NETWORK}")
message(STATUS "  Asynchronous I/O ............. ${WITH_ASYNC}")
if (DONT_SET_ROMS)
    set(ROM_STATUS "No")
else ()
    set(ROM_STATUS "Yes")
endif ()
message(STATUS "  Build internal ROMS .......... ${ROM_STATUS}")
message(STATUS "  'cppcheck' ................... ${CPPCHECK_STATUS}")
message(STATUS "")

unset(ROM_STATUS)

if (NOT SIMH_BUILD_DEPS)
    ## The actual simulators:

    include (add_simulator)

    ## The various video outputs for simulators:
    set(DISPLAYL   "${CMAKE_SOURCE_DIR}/display/display.c" "${CMAKE_SOURCE_DIR}/display/sim_ws.c")
    set(DISPLAYVT  "${CMAKE_SOURCE_DIR}/display/vt11.c")
    set(DISPLAY340 "${CMAKE_SOURCE_DIR}/display/type340.c")
    set(DISPLAYNG  "${CMAKE_SOURCE_DIR}/display/ng.c")

    ## Source/include directories:
    set(ALTAIRD    "${CMAKE_SOURCE_DIR}/ALTAIR")
    set(ALTAIRZ80D "${CMAKE_SOURCE_DIR}/AltairZ80")
    set(HP2100D    "${CMAKE_SOURCE_DIR}/HP2100")
    set(HP3000D    "${CMAKE_SOURCE_DIR}/HP3000")
    set(I1401D     "${CMAKE_SOURCE_DIR}/I1401")
    set(I1620D     "${CMAKE_SOURCE_DIR}/I1620")
    set(I7094D     "${CMAKE_SOURCE_DIR}/I7094")
    set(ID16D      "${CMAKE_SOURCE_DIR}/Interdata")
    set(ID32D      "${CMAKE_SOURCE_DIR}/Interdata")
    set(NOVAD      "${CMAKE_SOURCE_DIR}/NOVA")
    set(PDP1D      "${CMAKE_SOURCE_DIR}/PDP1")
    set(PDP10D     "${CMAKE_SOURCE_DIR}/PDP10")
    set(PDP11D     "${CMAKE_SOURCE_DIR}/PDP11")
    set(PDP18BD    "${CMAKE_SOURCE_DIR}/PDP18B")
    set(VAXD       "${CMAKE_SOURCE_DIR}/VAX")

    ## And here's where the actual work gets done:
    add_subdirectory(slirp)
    add_subdirectory(PDP1)
    add_subdirectory(PDP18B)
    add_subdirectory(PDP8)
    add_subdirectory(PDP10)
    add_subdirectory(PDP11)
    add_subdirectory(VAX)
    add_subdirectory(NOVA)
    add_subdirectory(HP2100)
    add_subdirectory(HP3000)
    add_subdirectory(I1401)
    add_subdirectory(I1620)
    add_subdirectory(S3)
    add_subdirectory(ALTAIR)
    add_subdirectory(AltairZ80)
    add_subdirectory(GRI)
    add_subdirectory(I7094)
    add_subdirectory(Ibm1130)
    add_subdirectory(Interdata)
    add_subdirectory(SDS)
    add_subdirectory(LGP)
    add_subdirectory(H316)
    add_subdirectory(swtp6800)
    add_subdirectory(TX-0)
    add_subdirectory(SSEM)
    add_subdirectory(B5500)
    add_subdirectory(Intel-Systems)
    add_subdirectory(3B2)
    add_subdirectory(I7000)
    add_subdirectory(sigma)

    # Experimental simulator:
    add_subdirectory(CDC1700)

    # Install the DLLs... including Packet.DLL
    if (WIN32 AND EXISTS ${CMAKE_BINARY_DIR}/build-stage/bin)
	file(GLOB SIMH_DLLS ${CMAKE_BINARY_DIR}/build-stage/bin/*.dll)

	install(
	    FILES ${SIMH_DLLS}
	    DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}
	)
    endif (WIN32 AND EXISTS ${CMAKE_BINARY_DIR}/build-stage/bin)
else (NOT SIMH_BUILD_DEPS)
    message(STATUS "")
    message(STATUS "The following dependencies need to be built:")
    message(STATUS "")
    message(STATUS "  ${SIMH_BUILD_DEPS}")
    message(STATUS "")
    message(STATUS "Execute 'cmake --build . --config <Release|Debug|...>'")
    message(STATUS "")
    message(STATUS "Reconfigure CMake after the build is complete and successful (no errors)")
    message(STATUS "")
endif (NOT SIMH_BUILD_DEPS)
