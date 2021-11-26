simh_sage_sources = [
  'sage_cpu.c',
  'sage_sys.c',
  'sage_stddev.c',
  'sage_cons.c',
  'sage_fd.c',
  'sage_lp.c',
  'm68k_cpu.c',
  'm68k_mem.c',
  'm68k_scp.c',
  'm68k_parse.tab.c',
  'm68k_sys.c',
  'i8251.c',
  'i8253.c',
  'i8255.c',
  'i8259.c',
  'i8272.c'
]

simh_sage_def = ['-DHAVE_INT64']

executable('simh-sage', simh_sage_sources,
  c_args: executable_cflags + simh_sage_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

