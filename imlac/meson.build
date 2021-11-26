simh_imlac_sources = [
  'imlac_sys.c',
  'imlac_cpu.c',
  'imlac_dp.c',
  'imlac_crt.c',
  'imlac_kbd.c',
  'imlac_tty.c',
  'imlac_pt.c',
  'imlac_bel.c'
]

executable('simh-imlac', simh_imlac_sources,
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

