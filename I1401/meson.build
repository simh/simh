simh_i1401_sources = [
  'i1401_lp.c',
  'i1401_cpu.c',
  'i1401_iq.c',
  'i1401_cd.c',
  'i1401_mt.c',
  'i1401_dp.c',
  'i1401_sys.c'
]

executable('simh-i1401', simh_i1401_sources,
  c_args: executable_cflags,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

