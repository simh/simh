rem 22-Aug-02	rms	Telnet console support
rem 18-May-02	rms	VT emulation support
rem
rem Compile all of SIMH using MINGW gcc environment
rem Individual simulator sources are in .\simulator_name
rem Individual simulator executables are to .\bin
rem
rem If needed, define the path for the MINGW bin directory.
rem (this should already be set if MINGW was installed correctly)
rem
path c:\mingw\bin;%path%
rem
rem PDP-1
rem
gcc -O0 -o .\bin\pdp1 -I. -I.\pdp1 scp*.c sim*.c .\pdp1\pdp1*.c -lwsock32
rem
rem PDP-11
rem
gcc -O0 -o .\bin\pdp11 -I. -I.\pdp11 scp*.c sim*.c .\pdp11\pdp11*.c -lm -lwsock32
rem
rem PDP-8
rem
gcc -O0 -o .\bin\pdp8 -I. -I.\pdp8 scp*.c sim*.c .\pdp8\pdp8*.c -lm -lwsock32
rem
rem PDP-4, PDP-7, PDP-9, PDP-15
rem
gcc -O0 -DPDP4 -o .\bin\pdp4 -I. -I.\pdp18b scp*.c sim*.c .\pdp18b\pdp18b*.c -lm -lwsock32
gcc -O0 -DPDP7 -o .\bin\pdp7 -I. -I.\pdp18b scp*.c sim*.c .\pdp18b\pdp18b*.c -lm -lwsock32
gcc -O0 -DPDP9 -o .\bin\pdp9 -I. -I.\pdp18b scp*.c sim*.c .\pdp18b\pdp18b*.c -lm -lwsock32
gcc -O0 -DPDP15 -o .\bin\pdp15 -I. -I.\pdp18b scp*.c sim*.c .\pdp18b\pdp18b*.c -lm -lwsock32
rem
rem PDP-10
rem
gcc -O0 -DUSE_INT64 -o .\bin\pdp10 -I.\pdp11 -I. -I.\pdp10 scp*.c sim*.c .\pdp10\pdp10*.c .\pdp11\pdp11_ry.c -lm -lwsock32
rem
rem Nova, Eclipse
rem
gcc -O0 -o .\bin\nova -I. -I.\nova scp*.c sim*.c .\nova\nova*.c -lm -lwsock32
gcc -O0 -DECLIPSE -o .\bin\eclipse -I. -I.\nova scp*.c sim*.c .\nova\eclipse_cpu.c .\nova\nova_clk.c .\nova\nova_dkp.c .\nova\nova_dsk.c .\nova\nova_lp.c .\nova\nova_mta.c .\nova\nova_plt.c .\nova\nova_pt.c .\nova\nova_sys.c .\nova\nova_tt.c .\nova\nova_tt1.c -lm -lwsock32
rem
rem Altair
rem
gcc -O0 -o .\bin\altair -I. -I.\altair scp*.c sim*.c .\altair\altair*.c -lwsock32
rem
rem AltairZ80
rem
gcc -O0 -o .\bin\altairz80 -I. -I.\altairz80 scp*.c sim*.c .\altairz80\altair*.c -lwsock32
rem
rem H316
rem
gcc -O0 -o .\bin\h316 -I. -I.\h316 scp*.c sim*.c .\h316\h316*.c -lwsock32
rem
rem HP2100
rem
gcc -O0 -o .\bin\hp2100 -I. -I.\hp2100 scp*.c sim*.c .\hp2100\hp2100*.c -lwsock32
rem
rem IBM 1401
rem
gcc -O0 -o .\bin\i1401 -I. -I.\i1401 scp*.c sim*.c .\i1401\i1401*.c -lwsock32
rem
rem IBM 1620
rem
gcc -O0 -o .\bin\i1620 -I. -I.\i1620 scp*.c sim*.c .\i1620\i1620*.c -lwsock32
rem
rem IBM System 3
rem
gcc -O0 -o .\bin\s3 -I. -I.\s3 scp*.c sim*.c .\s3\s3*.c -lwsock32
rem
rem IBM 1130
rem
gcc -O0 -o .\bin\ibm1130 -I. -I.\ibm1130 scp*.c sim*.c .\ibm1130\ibm1130*.c -lwsock32
rem
rem VAX
rem
gcc -O0 -DUSE_INT64 -o .\bin\vax -I. -I.\vax -I.\pdp11 scp*.c sim*.c .\pdp11\pdp11_pt.c .\pdp11\pdp11_rl.c .\pdp11\pdp11_rq.c .\pdp11\pdp11_dz.c .\pdp11\pdp11_lp.c .\pdp11\pdp11_ts.c .\pdp11\pdp11_tq.c .\pdp11\pdp11_xq.c .\vax\vax*.c -lm -lwsock32
rem
rem GRI
rem
gcc -O0 -o .\bin\gri -I. -I.\gri scp*.c sim*.c .\gri\gri*.c -lwsock32
rem
rem Placeholders for future simulators
rem
rem gcc -O0 -o .\bin\sds -I. -I.\sds scp*.c sim*.c .\sds\sds*.c -lm -lwsock32
rem gcc -O0 -o .\bin\id16 -I. -I.\interdata scp*.c sim*.c .\interdata\id16*.c .\interdata\id_*.c -lwsock32
rem gcc -O0 -o .\bin\id32 -I. -I.\interdata scp*.c sim*.c .\interdata\id32*.c .\interdata\id_*.c -lwsock32
