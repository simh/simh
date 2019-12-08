# Locate the PCRE library
#
# This module defines:
#
# ::
#
#   PCRE_LIBRARIES, the name of the library to link against
#   PCRE_INCLUDE_DIRS, where to find the headers
#   PCRE_FOUND, if false, do not try to link against
#   PCRE2_LIBRARIES, the name of the pcre2 library to link against
#   PCRE2_INCLUDE_DIRS, where to find the pcre2 headers
#   PCRE2_FOUND, if false, do not try to compile or link with pcre2
#   PCRE_VERSION_STRING - human-readable string containing the version of pcre or pcre2
#
# Tweaks:
# 1. PCRE_PATH: A list of directories in which to search
# 2. PCRE_DIR: An environment variable to the directory where you've unpacked or installed PCRE.
#
# "scooter me fecit"

find_path(PCRE_INCLUDE_DIR pcre.h
        HINTS
	  ENV PCRE_DIR
	# path suffixes to search inside ENV{PCRE_DIR}
	PATHS ${PCRE_PATH}
	PATH_SUFFIXES
	    include/pcre
	    include/PCRE
	    include
        )


find_path(PCRE2_INCLUDE_DIR pcre2.h
        HINTS
	  ENV PCRE_DIR
	# path suffixes to search inside ENV{PCRE_DIR}
	PATHS ${PCRE_PATH}
	PATH_SUFFIXES
	    include/pcre
	    include/PCRE
	    include
        )

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(LIB_PATH_SUFFIXES lib64 lib/x64 lib/amd64 lib/x86_64-linux-gnu lib)
else ()
  set(LIB_PATH_SUFFIXES lib/x86 lib)
endif ()

find_library(PCRE_LIBRARY_RELEASE
        NAMES pcre pcre_static
        HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
        PATHS ${PCRE_PATH}
        )

find_library(PCRE_LIBRARY_DEBUG
        NAMES pcred
        HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
        PATHS ${PCRE_PATH}
        )

find_library(PCRE2_LIBRARY_RELEASE
        NAMES pcre2-8
        HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
        PATHS ${PCRE_PATH}
        )

find_library(PCRE2_LIBRARY_DEBUG
        NAMES pcre2-8d
        HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
        PATHS ${PCRE_PATH}
        )

find_library(PCREPOSIX_LIBRARY_RELEASE
	NAMES pcreposix
	HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	PATHS ${PCRE_PATH}
	)

find_library(PCREPOSIX_LIBRARY_DEBUG
	NAMES pcreposixd
	HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	PATHS ${PCRE_PATH}
	)

find_library(PCRE2_POSIX_LIBRARY_RELEASE
	NAMES pcre2-posix
	HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	PATHS ${PCRE_PATH}
	)

find_library(PCRE2_POSIX_LIBRARY_DEBUG
	NAMES pcre2-posixd
	HINTS
	  ENV PCRE_DIR
	PATH_SUFFIXES ${LIB_PATH_SUFFIXES}
	PATHS ${PCRE_PATH}
	)

if (PCRE_INCLUDE_DIR OR PCRE2_INCLUDE_DIR)
    if (EXISTS "${PCRE_INCLUDE_DIR}/pcre.h")
	file(STRINGS "${PCRE_INCLUDE_DIR}/pcre.h" PCRE_VERSION_MAJOR_LINE REGEX "^#define[ \t]+PCRE_MAJOR[ \t]+[0-9]+$")
	file(STRINGS "${PCRE_INCLUDE_DIR}/pcre.h" PCRE_VERSION_MINOR_LINE REGEX "^#define[ \t]+PCRE_MINOR[ \t]+[0-9]+$")
    elseif (EXISTS "${PCRE2_INCLUDE_DIR}/pcre2.h")
	file(STRINGS "${PCRE2_INCLUDE_DIR}/pcre2.h" PCRE_VERSION_MAJOR_LINE REGEX "^#define[ \t]+PCRE2_MAJOR[ \t]+[0-9]+$")
	file(STRINGS "${PCRE2_INCLUDE_DIR}/pcre2.h" PCRE_VERSION_MINOR_LINE REGEX "^#define[ \t]+PCRE2_MINOR[ \t]+[0-9]+$")
    endif ()

    string(REGEX REPLACE "^#define[ \t]+PCRE2?_MAJOR[ \t]+([0-9]+)$" "\\1" PCRE_VERSION_MAJOR "${PCRE_VERSION_MAJOR_LINE}")
    string(REGEX REPLACE "^#define[ \t]+PCRE2?_MINOR[ \t]+([0-9]+)$" "\\1" PCRE_VERSION_MINOR "${PCRE_VERSION_MINOR_LINE}")

    set(PCRE_VERSION_STRING ${PCRE_VERSION_MAJOR}.${PCRE_VERSION_MINOR})
    set(PCRE_VERSION_TWEAK "")
    if (PCRE_VERSION_STRING MATCHES "[0-9]+\.[0-9]+")
	set(PCRE_VERSION_TWEAK "${CMAKE_MATCH_1}")
	string(APPEND PCRE_VERSION_STRING ".${PCRE_VERSION_TWEAK}")
    endif ()
    unset(PCRE_VERSION_MAJOR_LINE)
    unset(PCRE_VERSION_MINOR_LINE)
    unset(PCRE_VERSION_MAJOR)
    unset(PCRE_VERSION_MINOR)
endif ()

include(SelectLibraryConfigurations)

select_library_configurations(PCRE)
select_library_configurations(PCREPOSIX)
select_library_configurations(PCRE2)
select_library_configurations(PCRE2_POSIX)

set(PCRE_LIBRARIES ${PCRE_LIBRARY})
set(PCRE_INCLUDE_DIRS ${PCRE_INCLUDE_DIR})
set(PCREPOSIX_LIBRARIES ${PCREPOSIX_LIBRARY})

set(PCRE2_INCLUDE_DIRS ${PCRE2_INCLUDE_DIR})
set(PCRE2_LIBRARIES ${PCRE2_LIBRARY})
set(PCRE2_POSIX_LIBRARIES ${PCRE2_POSIX})

include(FindPackageHandleStandardArgs)

### Note: If the libpcre.cmake configuration file isn't installed,
### asking for a version is going to fail.
if (PCRE_INCLUDE_DIR)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(PCRE
	REQUIRED PCRE_LIBRARY PCREPOSIX_LIBRARY PCRE_INCLUDE_DIR
	# VERSION_VAR PCRE_VERSION_STRING
    )
endif (PCRE_INCLUDE_DIR)
if (PCRE2_INCLUDE_DIR)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(PCRE2
	REQUIRED PCRE2_LIBRARY PCRE2_POSIX_LIBRARY PCRE2_INCLUDE_DIR
	# VERSION_VAR PCRE_VERSION_STRING
    )
endif (PCRE2_INCLUDE_DIR)
