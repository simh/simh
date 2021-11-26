simh_alpha_sources = [
  'alpha_500au_syslist.c',
  'alpha_cpu.c',
  'alpha_ev5_cons.c',
  'alpha_ev5_pal.c',
  'alpha_ev5_tlb.c',
  'alpha_fpi.c',
  'alpha_fpv.c',
  'alpha_io.c',
  'alpha_mmu.c',
  'alpha_sys.c'
]

simh_alpha_def = ['-DHAVE_INT64', '-DUSE_ADDR64']

executable('simh-alpha', simh_alpha_sources,
  c_args: executable_cflags + simh_alpha_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

