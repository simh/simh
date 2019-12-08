#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=
# Manage the pthreads dependency
#
# (a) Try to locate the system's installed pthreads library, which is very
#     platform dependent (MSVC -> Pthreads4w, MinGW -> pthreads, *nix -> pthreads.)
# (b) MSVC: Build Pthreads4w as a dependent
#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=

include(ExternalProject)
add_library(thread_lib INTERFACE)

if (WIN32)
    if (NOT MINGW)
        # Pthreads4w: pthreads for windows
        include (FindPthreads4w)

        if (PTW_FOUND)
            target_compile_definitions(thread_lib INTERFACE USE_READER_THREAD SIM_ASYNCH_IO PTW32_STATIC_LIB)
            target_include_directories(thread_lib INTERFACE ${PTW_INCLUDE_DIRS})
            target_link_libraries(thread_lib INTERFACE ${PTW_C_LIBRARY})

            set(THREADING_PKG_STATUS "installed pthreads4w")
        else (PTW_FOUND)
            # set(PTHREADS4W_URL "https://github.com/jwinarske/pthreads4w")
            set(PTHREADS4W_URL "https://github.com/bscottm/pthreads4w.git")

            ExternalProject_Add(pthreads4w-ext
                GIT_REPOSITORY ${PTHREADS4W_URL}
                GIT_TAG mingw
                CMAKE_ARGS 
                    -DCMAKE_INSTALL_PREFIX=${SIMH_DEP_TOPDIR}
                    -DCMAKE_PREFIX_PATH=${SIMH_PREFIX_PATH_LIST}
                    -DCMAKE_INCLUDE_PATH=${SIMH_INCLUDE_PATH_LIST}
            )

            list(APPEND SIMH_BUILD_DEPS pthreads4w)
            message(STATUS "Building Pthreads4w from Git repository ${PTHREADS4W_URL}")
            set(THREADING_PKG_STATUS "pthreads4w source build")
        endif (PTW_FOUND)
    else (NOT MINGW)
        set(PTW_FOUND FALSE)

        # Use MinGW's threads instead
        target_compile_definitions(thread_lib INTERFACE USE_READER_THREAD SIM_ASYNCH_IO)
        target_compile_options(thread_lib INTERFACE "-pthread")
        target_link_libraries(thread_lib INTERFACE pthread)

        set(THREADING_PKG_STATUS "MinGW builtin pthreads")
    endif (NOT MINGW)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # Linux uses gcc, which has pthreads:
    target_compile_definitions(thread_lib INTERFACE USE_READER_THREAD SIM_ASYNCH_IO)
    target_compile_options(thread_lib INTERFACE "-pthread")
    target_link_libraries(thread_lib INTERFACE pthread)

    set(THREADING_PKG_STATUS "Linux builtin pthreads")
endif (WIN32)
