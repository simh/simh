## IBM 1620 simulator

add_simulator(i1620
    SOURCES
	i1620_cd.c
	i1620_dp.c
	i1620_pt.c
	i1620_tty.c
	i1620_cpu.c
	i1620_lp.c
	i1620_fp.c
	i1620_sys.c
    INCLUDES
	${I1620D})
