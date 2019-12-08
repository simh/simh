## AT&T 3b2 simulator:

add_simulator(3b2
    SOURCES
	3b2_cpu.c 
	3b2_mmu.c
	3b2_iu.c 
	3b2_if.c
	3b2_id.c 
	3b2_dmac.c
	3b2_sys.c 
	3b2_io.c
	3b2_ports.c
	3b2_ctc.c
	3b2_ni.c 
	3b2_mau.c
	3b2_sysdev.c
    INCLUDES
	3B2
    FULL64
    BUILDROMS)
