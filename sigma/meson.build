simh_sigma_sources = [
  'sigma_cpu.c',
  'sigma_sys.c',
  'sigma_cis.c',
  'sigma_coc.c',
  'sigma_dk.c',
  'sigma_dp.c',
  'sigma_fp.c',
  'sigma_io.c',
  'sigma_lp.c',
  'sigma_map.c',
  'sigma_mt.c',
  'sigma_pt.c',
  'sigma_rad.c',
  'sigma_rtc.c',
  'sigma_tt.c'
]

executable('simh-sigma', simh_sigma_sources,
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

