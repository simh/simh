$ if f$search("pcap.olb") .eqs. ""
$ then
$	libr/crea pcap.olb
$ endif
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] bpf_dump
$ libr/replace pcap bpf_dump
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] bpf_filter
$ libr/replace pcap bpf_filter
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] bpf_image
$ libr/replace pcap bpf_image
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] etherent
$ libr/replace pcap etherent
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] fad-gifc
$ libr/replace pcap fad-gifc
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] gencode
$ libr/replace pcap gencode
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] grammar
$ libr/replace pcap grammar
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] inet
$ libr/replace pcap inet
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] nametoaddr
$ libr/replace pcap nametoaddr
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] optimize
$ libr/replace pcap optimize
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] pcap
$ libr/replace pcap pcap
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] savefile
$ libr/replace pcap savefile
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] scanner
$ libr/replace pcap scanner
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] snprintf
$ libr/replace pcap snprintf
$ cc/name=(as_is,shortened)/debug/nomember_align/noopt/include=sys$disk:[] pcap-vms
$ libr/replace pcap pcap-vms
$ exit

