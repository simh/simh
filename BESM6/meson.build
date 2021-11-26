simh_besm6_sources = [
  'besm6_cpu.c',
  'besm6_sys.c',
  'besm6_mmu.c',
  'besm6_arith.c',
  'besm6_disk.c',
  'besm6_drum.c',
  'besm6_tty.c',
  'besm6_panel.c',
  'besm6_printer.c',
  'besm6_pl.c',
  'besm6_punch.c',
  'besm6_punchcard.c',
  'besm6_vu.c'
]

simh_besm6_def = ['-DUSE_INT64']

executable('simh-besm6', simh_besm6_sources,
  c_args: executable_cflags + simh_besm6_def,
  include_directories: [platform_inc],
  dependencies: [
    simh_common_dep,
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)

