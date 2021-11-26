simh_id_common_sources = [
  'id_dp.c',
  'id_fd.c',
  'id_fp.c',
  'id_idc.c',
  'id_io.c',
  'id_lp.c',
  'id_mt.c',
  'id_pas.c',
  'id_pt.c',
  'id_tt.c',
  'id_uvc.c',
  'id_ttp.c'
]

simh_id16_sources = simh_id_common_sources + [
  'id16_cpu.c',
  'id16_sys.c',
  'id16_dboot.c',
]

simh_id32_sources = simh_id_common_sources + [
  'id32_cpu.c',
  'id32_sys.c',
  'id32_dboot.c'
]

executable('simh-id16', simh_id16_sources,
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

executable('simh-id32', simh_id32_sources,
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

