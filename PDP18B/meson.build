simh_pdp18b_sources = [
  'pdp18b_dt.c',
  'pdp18b_drm.c',
  'pdp18b_cpu.c',
  'pdp18b_lp.c',
  'pdp18b_mt.c',
  'pdp18b_rf.c',
  'pdp18b_rp.c',
  'pdp18b_stddev.c',
  'pdp18b_sys.c',
  'pdp18b_rb.c',
  'pdp18b_tt1.c',
  'pdp18b_fpp.c',
  'pdp18b_g2tty.c',
  'pdp18b_dr15.c'
]

simh_pdp7_sources = simh_pdp18b_sources + [
  'pdp18b_dpy.c'
]

simh_pdp4_def = ['-DPDP4']
simh_pdp7_def = ['-DPDP7']
simh_pdp9_def = ['-DPDP9']
simh_pdp15_def = ['-DPDP15']

executable('simh-pdp18b', simh_pdp18b_sources,
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

executable('simh-pdp4', simh_pdp18b_sources,
  c_args: executable_cflags + simh_pdp4_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp7', simh_pdp7_sources,
  c_args: executable_cflags + simh_pdp7_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp9', simh_pdp18b_sources,
  c_args: executable_cflags + simh_pdp9_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-pdp15', simh_pdp18b_sources,
  c_args: executable_cflags + simh_pdp15_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

