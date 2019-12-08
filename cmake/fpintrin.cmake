## Check for various GNU-specific floating point math flags
##
## Not entirely sure that they will make a huge difference to code
## generation in the simulators.

set(EXTRA_CFLAGS)

set(CMAKE_REQUIRED_FLAGS "-msse")
check_c_source_compiles("
    #ifdef __MINGW32__
    #include <_mingw.h>
    #ifdef __MINGW64_VERSION_MAJOR
    #include <intrin.h>
    #else
    #include <xmmintrin.h>
    #endif
    #else
    #include <xmmintrin.h>
    #endif
    #ifndef __SSE__
    #error Assembler CPP flag not enabled
    #endif
    int main(int argc, char **argv) { }" HAVE_SSE)
if(HAVE_SSE)
  list(APPEND EXTRA_CFLAGS "-msse")
endif()
set(CMAKE_REQUIRED_FLAGS ${ORIG_CMAKE_REQUIRED_FLAGS})

set(CMAKE_REQUIRED_FLAGS "-msse2")
check_c_source_compiles("
    #ifdef __MINGW32__
    #include <_mingw.h>
    #ifdef __MINGW64_VERSION_MAJOR
    #include <intrin.h>
    #else
    #include <emmintrin.h>
    #endif
    #else
    #include <emmintrin.h>
    #endif
    #ifndef __SSE2__
    #error Assembler CPP flag not enabled
    #endif
    int main(int argc, char **argv) { }" HAVE_SSE2)
if(HAVE_SSE2)
  list(APPEND EXTRA_CFLAGS "-msse2")
endif()
set(CMAKE_REQUIRED_FLAGS ${ORIG_CMAKE_REQUIRED_FLAGS})

set(CMAKE_REQUIRED_FLAGS "-msse3")
check_c_source_compiles("
    #ifdef __MINGW32__
    #include <_mingw.h>
    #ifdef __MINGW64_VERSION_MAJOR
    #include <intrin.h>
    #else
    #include <pmmintrin.h>
    #endif
    #else
    #include <pmmintrin.h>
    #endif
    #ifndef __SSE3__
    #error Assembler CPP flag not enabled
    #endif
    int main(int argc, char **argv) { }" HAVE_SSE3)
if(HAVE_SSE3)
  list(APPEND EXTRA_CFLAGS "-msse3")
endif()
set(CMAKE_REQUIRED_FLAGS ${ORIG_CMAKE_REQUIRED_FLAGS})

if(SSE OR SSE2 OR SSE3)
  if(USE_GCC)
    check_c_compiler_flag(-mfpmath=387 HAVE_FP_387)
    if(HAVE_FP_387)
      list(APPEND EXTRA_CFLAGS "-mfpmath=387")
    endif()
  endif()
  set(HAVE_SSEMATH TRUE)
endif()
