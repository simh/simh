simh_cdc1700_sources = [
  'cdc1700_cpu.c',
  'cdc1700_dis.c',
  'cdc1700_io.c',
  'cdc1700_sys.c',
  'cdc1700_dev1.c',
  'cdc1700_mt.c',
  'cdc1700_dc.c',
  'cdc1700_iofw.c',
  'cdc1700_lp.c',
  'cdc1700_dp.c',
  'cdc1700_cd.c',
  'cdc1700_sym.c',
  'cdc1700_rtc.c',
  'cdc1700_drm.c',
  'cdc1700_msos5.c'
]

executable('simh-cdc1700', simh_cdc1700_sources,
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

