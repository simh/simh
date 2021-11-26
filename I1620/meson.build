simh_i1620_sources = [
  'i1620_cd.c',
  'i1620_dp.c',
  'i1620_pt.c',
  'i1620_tty.c',
  'i1620_cpu.c',
  'i1620_lp.c',
  'i1620_fp.c',
  'i1620_sys.c'
]

executable('simh-i1620', simh_i1620_sources,
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

