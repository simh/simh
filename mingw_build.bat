rem Compile all of SIMH using MINGW gcc environment
rem Master sources are in c:\sim
rem Individual simulator sources are in c:\sim\simulator_name
rem Mingw system is in C:\Mingw\bin
rem
path c:\mingw\bin;%path
cd c:\sim\pdp1
gcc -o ..\bin\pdp1 -I.. ..\scp*.c pdp1*.c
cd c:\sim\pdp11
gcc -o ..\bin\pdp11 -I.. ..\scp*.c ..\sim*.c pdp11*.c -lm -lwsock32
cd c:\sim\pdp8
gcc -o ..\bin\pdp8 -I.. ..\scp*.c ..\sim*.c pdp8*.c -lm -lwsock32
cd c:\sim\pdp18b
gcc -DPDP4 -o ..\bin\pdp4 -I.. ..\scp*.c ..\sim*.c pdp18b*.c -lm -lwsock32
gcc -DPDP7 -o ..\bin\pdp7 -I.. ..\scp*.c ..\sim*.c pdp18b*.c -lm -lwsock32
gcc -DPDP9 -o ..\bin\pdp9 -I.. ..\scp*.c ..\sim*.c pdp18b*.c -lm -lwsock32
gcc -DPDP15 -o ..\bin\pdp15 -I.. ..\scp*.c ..\sim*.c pdp18b*.c -lm -lwsock32
cd c:\sim\pdp10
gcc -DUSE_INT64 -o ..\bin\pdp10 -I.. ..\scp*.c ..\sim*.c pdp10*.c -lm -lwsock32
cd c:\sim\vax
gcc -DUSE_INT64 -o ..\bin\vax -I.. -I. ..\scp*.c ..\sim*.c ..\pdp11\pdp11_rl.c ..\pdp11\pdp11_rq.c ..\pdp11\pdp11_dz.c ..\pdp11\pdp11_lp.c ..\pdp11\pdp11_ts.c vax*.c -lm -lwsock32
cd c:\sim\nova
gcc -o ..\bin\nova -I.. ..\scp*.c ..\sim*.c nova*.c -lm -lwsock32
cd c:\sim\altair
gcc -o ..\bin\altair -I.. ..\scp*.c altair*.c
cd c:\sim\h316
gcc -o ..\bin\h316 -I.. ..\scp*.c h316*.c
cd c:\sim\hp2100
gcc -o ..\bin\hp2100 -I.. ..\scp*.c hp2100*.c
cd c:\sim\i1401
gcc -o ..\bin\i1401 -I.. ..\scp*.c i1401*.c
cd c:\sim\id4
gcc -o ..\bin\id4 -I.. ..\scp*.c id4*.c
cd c:\sim\s3
gcc -o ..\bin\s3 -I.. ..\scp*.c s3*.c
cd c:\sim\sds
gcc -o ..\bin\sds -I.. ..\scp*.c sds*.c -lm
cd ..



