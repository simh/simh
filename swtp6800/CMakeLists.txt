## SWTP 6800 simulators:

set(SWTP6800D "${CMAKE_SOURCE_DIR}/swtp6800/swtp6800")
set(SWTP6800C "${CMAKE_SOURCE_DIR}/swtp6800/common")

add_simulator(swtp6800mp-a
    SOURCES
	common/mp-a.c
	common/m6800.c
	common/m6810.c
	common/bootrom.c
	common/dc-4.c
	common/mp-s.c
	swtp6800/mp-a_sys.c
	common/mp-b2.c
	common/mp-8m.c
    INCLUDES
	${SWTP6800D}
    BUILDROMS)

add_simulator(swtp6800mp-a2
    SOURCES
	common/mp-a2.c
	common/m6800.c
	common/m6810.c
	common/bootrom.c
	common/dc-4.c
	common/mp-s.c
	swtp6800/mp-a2_sys.c
	common/mp-b2.c
	common/mp-8m.c
	common/i2716.c
    INCLUDES
	${SWTP6800D}
    BUILDROMS)
