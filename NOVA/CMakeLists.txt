## The NOVA simulator:

add_simulator(nova
    SOURCES
	nova_sys.c
	nova_cpu.c
	nova_dkp.c
	nova_dsk.c
	nova_lp.c
	nova_mta.c
	nova_plt.c
	nova_pt.c
	nova_clk.c
	nova_tt.c
	nova_tt1.c
	nova_qty.c
    INCLUDES
	${NOVAD})

add_simulator(eclipse
    SOURCES
	eclipse_cpu.c
	eclipse_tt.c
	nova_sys.c
	nova_dkp.c
	nova_dsk.c
	nova_lp.c
	nova_mta.c
	nova_plt.c
	nova_pt.c
	nova_clk.c
	nova_tt1.c
	nova_qty.c
    DEFINES
	ECLIPSE
    INCLUDES
	${NOVAD})
