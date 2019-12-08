## S3 simulator:

add_simulator(s3 "${S3_SOURCES}" "${S3_DEFINES}" "${S3_INCLUDES}"
    SOURCES
	s3_cd.c
	s3_cpu.c
	s3_disk.c
	s3_lp.c
	s3_pkb.c
	s3_sys.c
    INCLUDES
	S3)
