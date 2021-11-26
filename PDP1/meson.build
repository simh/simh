simh_pdp1_sources = [
  'pdp1_clk.c',
  'pdp1_cpu.c',
  'pdp1_dcs.c',
  'pdp1_dpy.c',
  'pdp1_drm.c',
  'pdp1_dt.c',
  'pdp1_lp.c',
  'pdp1_stddev.c',
  'pdp1_sys.c'
]

executable('simh-pdp1', simh_pdp1_sources,
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
