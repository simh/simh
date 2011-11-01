$ if f$search("pcap.olb") .eqs. ""
$ then
$	libr/crea pcap.olb
$ endif
$ if p1 .eqs. "DEBUG"
$ then
$    opt = "/debug/noopt"
$ else
$! Nodebug
$    opt = "/opt=(level=4)"
$ endif
$ cc/opt/name=(as_is, shortened)/nomember_align'opt/include=sys$disk:[] -
  pcapvci+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap pcapvci
$ cc/opt/name=(as_is, shortened)/nomember_align'opt/include=sys$disk:[] -
  vcmutil+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap vcmutil
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 bpf_dump+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap bpf_dump
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 bpf_filter+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap bpf_filter
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 bpf_image+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap bpf_image
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
  etherent+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap etherent
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 fad-gifc+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap fad-gifc
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 gencode+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap gencode
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 grammar+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap grammar
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 inet+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap inet
$ cc/name=( as_is,shortened)/opt/member_align'opt/include=sys$disk:[] -
  nametoaddr+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap nametoaddr
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] - 
 optimize+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap optimize
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 pcap+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap pcap
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] - 
 savefile+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap savefile
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] - 
 scanner+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap scanner
$ cc/name=( as_is,shortened)/opt/nomember_align'opt/include=sys$disk:[] -
 snprintf+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap snprintf
$ cc/name=( as_is,shortened)/opt/nomember_alig'opt/include=sys$disk:[] -
 pcap-vms+sys$share:sys$lib_c.tlb/lib
$ libr/replace pcap pcap-vms
$ exit
