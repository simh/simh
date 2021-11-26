simh_3b2_common_sources = [
  '3b2_cpu.c',
  '3b2_sys.c',
  '3b2_dmac.c',
  '3b2_mem.c',
  '3b2_iu.c',
  '3b2_if.c',
  '3b2_io.c',
  '3b2_ports.c',
  '3b2_stddev.c',
  '3b2_ni.c'
]

simh_3b2m400_sources = simh_3b2_common_sources + [
  '3b2_rev2_sys.c',
  '3b2_rev2_mmu.c',
  '3b2_rev2_mau.c',
  '3b2_rev2_csr.c',
  '3b2_rev2_timer.c',
  '3b2_id.c',
  '3b2_ctc.c'
]

simh_3b2m600_sources = simh_3b2_common_sources + [
  '3b2_rev3_sys.c',
  '3b2_rev3_mmu.c',
  '3b2_rev2_mau.c',
  '3b2_rev3_csr.c',
  '3b2_rev3_timer.c',
  '3b2_scsi.c'
]

simh_3b2m400_def = ['-DHAVE_INT64', '-DUSE_ADDR64', '-DREV2']
simh_3b2m600_def = ['-DHAVE_INT64', '-DUSE_ADDR64', '-DREV3']

executable('simh-3b2m400', simh_3b2m400_sources,
  c_args: executable_cflags + simh_3b2m400_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

executable('simh-3b2m600', simh_3b2m600_sources,
  c_args: executable_cflags + simh_3b2m600_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)
