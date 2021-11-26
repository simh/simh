simh_s3_sources = [
  's3_cd.c',
  's3_cpu.c',
  's3_disk.c',
  's3_lp.c',
  's3_pkb.c',
  's3_sys.c'
]

executable('simh-s3', simh_s3_sources,
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
