$!
$! This procedure builds the following:
$!
$! [.pcap-vci]
$!   A port of pcap using the VCI interface to the LAN driver. This
$!   version of pcap uses the PCAPVCM execlet and is faster than the
$!   QIO implementation of pcap. This version allows you to send packets
$!   as well as receiving packets.
$!
$! [.pcapvcm]
$!   The VMS execlet which uses the VCI interface to the LAN driver.
$!   Once built it must be placed in SYS$LOADABLE_IMAGES.
$!
$!
$! Author: Ankan, 20-Sep-2003
$ on warning then continue
$ set def [.pcap-vci]
$ write sys$output "Building VCI version of pcap..."
$ @vms_pcap
$ set def [-.pcapvcm]
$ write sys$output "Building the PCAP VCM execlet..."
$ write sys$output "In order to use it, place PCAPVCM.EXE in the"
$ write sys$output "SYS$LOADABLE_IMAGES directory."
$ @build_pcapvcm
$ set def [-]
$ write sys$output "Build done..."
$ exit
