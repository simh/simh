## IBM 1401 simulator

add_simulator(i1401
    SOURCES
	i1401_lp.c
	i1401_cpu.c
	i1401_iq.c
	i1401_cd.c
	i1401_mt.c
	i1401_dp.c
	i1401_sys.c
    INCLUDES
	${I1401D})
