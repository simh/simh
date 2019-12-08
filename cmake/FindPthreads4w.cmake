# Locate the pthreads4w library and pthreads.h header
#
# This module defines:
#
# ::
#
#   PTW_C_LIBRARY, the "pthread[GV]C3" library
#   PTW_CE_LIBRARY, the "pthread[GV]CE3" library
#   PTW_SE_LIBRARY, the "pthread[GV]SE3" library
#   PTW_INCLUDE_DIR, where to find the headers
#   PTW_FOUND, if false, do not try to link against
#
# Tweaks:
# 1. PTW_PATH: A list of directories in which to search
# 2. PTW_DIR: An environment variable to the directory where you've unpacked or installed PCRE.
#
# "scooter me fecit"

if (WIN32)
    include(SelectLibraryConfigurations)

    find_path(PTW_INCLUDE_DIR pthread.h
      HINTS
	ENV PTW_DIR
	# path suffixes to search inside ENV{PTW_DIR}
	PATH_SUFFIXES include include/pthreads4w
	PATHS ${PTW_PATH}
      )

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(LIB_PATH_SUFFIXES lib64 lib/x64 lib/amd64 lib/x86_64-linux-gnu lib)
    else ()
      set(LIB_PATH_SUFFIXES lib/x86 lib)
    endif ()

    foreach (flavor C CE SE)
	if (MSVC)
	    set(libflavor V${flavor}3)
	elseif (MINGW)
	    set(libflavor G${flavor}3)
	endif (MSVC)

	find_library(PTW_${flavor}_LIBRARY_RELEASE
	  NAMES 
	    libpthread${libflavor}  pthread${libflavor}
	  HINTS
	      ENV PTW_ DIR
	  PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	  PATHS ${PTW_PATH}
	)

	find_library(PTW_${flavor}_LIBRARY_DEBUG
	  NAMES 
	    libpthread${libflavor}d  pthread${libflavor}d
	  HINTS
	      ENV PTW_ DIR
	  PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	  PATHS ${PTW_PATH}
	)

	select_library_configurations(PTW_${flavor})
    endforeach ()

    include(FindPackageHandleStandardArgs)

    # Minimally, we want the include directory and the C library...
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(
	PTW
	REQUIRED_VARS PTW_C_LIBRARY PTW_INCLUDE_DIR
    )
endif ()
