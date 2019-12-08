## The Intel simulator family:


add_simulator(isys8010
    SOURCES
        common/i8080.c
        isys8010/isys8010_sys.c
        common/i8251.c
        common/i8255.c
        common/ieprom.c
        common/iram8.c
        common/multibus.c
        isys8010/isbc8010.c
        common/isbc064.c
        common/isbc202.c
        common/isbc201.c
        common/zx200a.c
        common/isbc206.c
        common/isbc464.c
        common/isbc208.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/isys8010
    BUILDROMS)


add_simulator(isys8020
    SOURCES
        common/i8080.c
        isys8020/isys8020_sys.c
        common/i8251.c
        common/i8255.c
        common/ieprom.c
        common/iram8.c
        common/multibus.c
        isys8020/isbc8020.c
        common/isbc064.c
        common/i8259.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/zx200a.c
        common/i8253.c
        common/isbc208.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/isys8020
    BUILDROMS)


add_simulator(isys8024
    SOURCES
        common/i8080.c
        isys8024/isys8024_sys.c
        common/i8251.c
        common/i8253.c
        common/i8255.c
        common/i8259.c
        common/ieprom.c
        common/iram8.c
        common/multibus.c
        isys8024/isbc8024.c
        common/isbc064.c
        common/isbc208.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/zx200a.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/isys8024
    BUILDROMS)


add_simulator(isys8030
    SOURCES
        common/i8080.c
        isys8030/isys8030_sys.c
        common/i8251.c
        common/i8255.c
        common/i8259.c
        common/i8253.c
        common/ieprom.c
        common/iram8.c
        common/multibus.c
        isys8030/isbc8030.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/isbc064.c
        common/zx200a.c
        common/isbc208.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/isys8030
    BUILDROMS)


add_simulator(imds-210
    SOURCES
        common/i8080.c
        imds-210/imds-210_sys.c
        common/i8251.c
        common/i8255.c
        common/i8259.c
        common/i8253.c
        common/ieprom.c
        common/iram8.c
        common/ipbmultibus.c
        common/ipb.c
        common/ipc-cont.c
        common/ioc-cont.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/zx200a.c
        common/isbc064.c
    INCLUDES
        ${CMAKE_SOURCE_DIR}/Intel-Systems/imds-210
    BUILDROMS)


add_simulator(imds-220
    SOURCES
        common/i8080.c
        imds-220/imds-220_sys.c
        common/i8251.c
        common/i8255.c
        common/i8259.c
        common/i8253.c
        common/ieprom.c
        common/iram8.c
        common/ipbmultibus.c
        common/ipb.c
        common/ipc-cont.c
        common/ioc-cont.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/zx200a.c
        common/isbc064.c
    INCLUDES
        ${CMAKE_SOURCE_DIR}/Intel-Systems/imds-220
    BUILDROMS)


add_simulator(imds-225
    SOURCES
        common/i8080.c
        imds-225/imds-225_sys.c
        common/i8251.c
        common/i8255.c
        common/i8259.c
        common/i8253.c
        common/ieprom.c
        common/iram8.c
        common/ipcmultibus.c
        common/ipc.c
        common/ipc-cont.c
        common/ioc-cont.c
        common/isbc064.c
        common/isbc202.c
        common/isbc201.c
        common/zx200a.c
        common/isbc464.c
        common/isbc206.c
        common/isbc208.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/imds-225
    BUILDROMS)


add_simulator(imds-230
    SOURCES
        common/i8080.c
        imds-230/imds-230_sys.c
        common/i8251.c
        common/i8255.c
        common/i8259.c
        common/i8253.c
        common/ieprom.c
        common/iram8.c
        common/ipbmultibus.c
        common/ipb.c
        common/ipc-cont.c
        common/ioc-cont.c
        common/isbc202.c
        common/isbc201.c
        common/isbc206.c
        common/isbc464.c
        common/zx200a.c
        common/isbc064.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/imds-230
    BUILDROMS)


add_simulator(imds-800
    SOURCES
        common/i8080.c
        imds-800/imds-800_sys.c
        imds-800/cpu.c
        imds-800/front_panel.c
        imds-800/monitor.c
        common/ieprom1.c
        common/i8251.c
        common/ieprom.c
        common/m800multibus.c
        common/isbc064.c
        common/isbc202.c
        common/isbc201.c
        common/zx200a.c
        common/isbc464.c
        common/isbc206.c
        common/isbc208.c
        common/i3214.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/imds-800
    BUILDROMS)


add_simulator(imds-810
    SOURCES
        common/i8080.c
        imds-810/imds-810_sys.c
        imds-810/cpu.c
        imds-810/front_panel.c
        imds-810/monitor.c
        common/ieprom1.c
        common/i8251.c
        common/ieprom.c
        common/m800multibus.c
        common/isbc064.c
        common/isbc202.c
        common/isbc201.c
        common/zx200a.c
        common/isbc464.c
        common/isbc206.c
        common/isbc208.c
        common/i3214.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/imds-810
    BUILDROMS)

## -- Works in progress --
# add_simulator(ibmpc
#     SOURCES
# 	common/i8255.c
# 	ibmpc/ibmpc.c
# 	common/i8088.c
# 	ibmpc/ibmpc_sys.c
# 	common/i8253.c
# 	common/i8259.c
# 	common/pceprom.c
# 	common/pcram8.c
# 	common/i8237.c
# 	common/pcbus.c
#     INCLUDES
# 	${CMAKE_SOURCE_DIR}/Intel-Systems/ibmpc
#     BUILDROMS)
# 
# add_simulator(ibmpcxt
#     SOURCES
# 	common/i8088.c
# 	ibmpcxt/ibmpcxt_sys.c
# 	common/i8253.c
# 	common/i8259.c
# 	common/i8255.c
# 	ibmpcxt/ibmpcxt.c
# 	common/pceprom.c
# 	common/pcram8.c
# 	common/pcbus.c
# 	common/i8237.c
#     INCLUDES
# 	${CMAKE_SOURCE_DIR}/Intel-Systems/ibmpcxt
#     BUILDROMS)

add_simulator(scelbi
    SOURCES
	common/i8008.c
	scelbi/scelbi_sys.c
	scelbi/scelbi_io.c
    INCLUDES
	${CMAKE_SOURCE_DIR}/Intel-Systems/scelbi)
