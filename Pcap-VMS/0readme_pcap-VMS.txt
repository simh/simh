# The simh source provides a descrip.mms file which builds all of the
# respective simulators on a VMS host platform.  
#
# The VAX and PDP11 simulators can include network support which depends
# on the PCAP-VMS package provided here.
#
# The PCAP-VMS components are presumed to be located in a directory at 
# the same level as the directory containing the simh source files.  
# For example, if these exist here:
#
#   []descrip.mms
#   []scp.c
#   etc.
#
# Then the following should exist:
#   [-.PCAP-VMS]BUILD_ALL.COM
#   [-.PCAP-VMS.PCAP-VCI]
#   [-.PCAP-VMS.PCAPVCM]
#   etc.
