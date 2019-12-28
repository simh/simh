#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=
# Manage the networking dependencies
#
# (a) Try to locate the system's installed libraries.
# (b) Build source libraries, if not found.
#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=

include (ExternalProject)

# pcap networking (slirp is handled in its own directory):
add_library(pcap INTERFACE)

if (WITH_NETWORK)
    if (WITH_PCAP)
        include (FindPCAP)

        if (PCAP_FOUND)
            target_compile_definitions(pcap INTERFACE USE_SHARED HAVE_PCAP_NETWORK)
            target_include_directories(pcap INTERFACE "${PCAP_INCLUDE_DIRS}")
            target_link_libraries(pcap INTERFACE "${PCAP_LIBRARIES}")

            set(NETWORK_PKG_STATUS "installed PCAP")
        else (PCAP_FOUND)
            # Extract the npcap headers and libraries
            set(NPCAP_ARCHIVE ${CMAKE_SOURCE_DIR}/dep-patches/libpcap/npcap-sdk-1.04.zip)

            execute_process(
                    COMMAND ${CMAKE_COMMAND} -E tar xzf ${NPCAP_ARCHIVE} Include/ Lib/
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/build-stage/
            )

            ExternalProject_Add(pcap-dep
                    # GIT_REPOSITORY https://github.com/the-tcpdump-group/libpcap.git
                    # GIT_TAG libpcap-1.9.0
                    GIT_REPOSITORY https://github.com/bscottm/libpcap.git
                    GIT_TAG cmake_library_architecture
                    CMAKE_ARGS 
                        -DCMAKE_INSTALL_PREFIX=${SIMH_DEP_TOPDIR}
                        -DCMAKE_PREFIX_PATH=${SIMH_PREFIX_PATH_LIST}
                        -DCMAKE_INCLUDE_PATH=${SIMH_INCLUDE_PATH_LIST}
            )

            list(APPEND SIMH_BUILD_DEPS "pcap")
            message(STATUS "Building PCAP from github repository")
            set(NETWORK_PKG_STATUS "PCAP source build")
        endif (PCAP_FOUND)
    endif ()
else ()
    set(NETWORK_STATUS "networking disabled")
endif ()
