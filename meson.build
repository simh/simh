project('simh', 'c',
  version: 'v0.4.0-git',
  meson_version: '>=0.55.0',
  default_options: [
    'buildtype=debugoptimized',
    'b_vscrt=from_buildtype',
  ]
)

is_static_build = get_option('static_runtime')
if is_static_build and get_option('default_library') != 'static'
  error('Cannot use `static_runtime` when libraries are dynamically built. Set `--default-library=static` if you want to build statically.')
endif

# Compiler options
cc = meson.get_compiler('c')

if cc.has_argument('--std=gnu99')
  add_project_arguments('--std=gnu99', language: ['c', 'cpp'])
elif cc.has_argument('--std=c99')
  add_project_arguments('--std=c99', language: ['c', 'cpp'])
endif

if cc.get_id() == 'clang-cl'
  if cc.has_argument('-fcommon')
    add_project_arguments('-fcommon', language: 'c')
  endif
  add_project_arguments('-D__STDC__=1', language: 'c')
  add_project_arguments('-D_CRT_DECLARE_NONSTDC_NAMES ', language: 'c')
  add_project_arguments('-D_CRT_SECURE_NO_WARNINGS', language: 'c')
  add_project_arguments('-D_CRT_NONSTDC_NO_DEPRECATE', language: 'c')
endif

# System dependencies
pth = dependency('threads', required: false, static: is_static_build)
if host_machine.system() == 'sunos'
  # workaround for Solaris until https://github.com/mesonbuild/meson/issues/4328 is fixed
  mth = declare_dependency(link_args: '-lm')
else
  mth = cc.find_library('m', required: false, static: is_static_build)
endif

lrt = dependency('', required: false)
if not cc.has_function('clock_gettime', prefix: '#include <time.h>') and cc.has_header_symbol('features.h', '__GLIBC__')
  lrt = cc.find_library('rt', required: true, static: is_static_build)
endif

have_lrt = not ['windows', 'darwin', 'openbsd', 'android', 'haiku'].contains(host_machine.system())
if have_lrt and not lrt.found()
  lrt = cc.find_library('rt', required: true, static: is_static_build)
endif

largefile = ''
have_largefile64 = host_machine.system() != 'solaris'
if have_largefile64
  largefile = '-D_LARGEFILE64_SOURCE'
else
  largefile = '-D_LARGEFILE_SOURCE'
endif

# handle pcap library
sys_pcap = dependency('pcap', required: get_option('use_sys_pcap'), static: is_static_build)


# rpath handling
use_rpath = false
use_rpath_absolute = false
if host_machine.system() != 'windows'
  if get_option('local') == 'enabled'
    use_rpath = true
  elif get_option('local') == 'absolute'
    use_rpath_absolute = true
  elif get_option('local') == 'auto' and get_option('prefix') != '/usr'
    if host_machine.system() == 'openbsd'
      # OpenBSD's $ORIGIN only works in the way we would need it when running an
      # executable by its actual path, but not when run through PATH.
      # So let's use absolute rpaths there.
      use_rpath_absolute = true
    else
      use_rpath = true
    endif
  endif
endif

rpath_exe = ''
rpath_lib = ''
rpath_summary = 'disabled'
if use_rpath
  rpath_exe = '$ORIGIN/../' + get_option('libdir')
  rpath_lib = '$ORIGIN'
  rpath_summary = 'relative'
elif use_rpath_absolute
  rpath_exe = get_option('prefix') / get_option('libdir')
  rpath_lib = get_option('prefix') / get_option('libdir')
  rpath_summary = 'absolute'
endif

# Common files
simh_common_sources = [
  'scp.c',
  'sim_console.c',
  'sim_fio.c',
  'sim_timer.c',
  'sim_sock.c',
  'sim_tmxr.c',
  'sim_ether.c',
  'sim_tape.c',
  'sim_disk.c',
  'sim_serial.c',
  'sim_video.c',
  'sim_imd.c',
  'sim_card.c',
  'sim_scsi.c'
]

simh_common_def = ['-DUSE_SIM_CARD', '-DUSE_SIM_VIDEO']
simh_common_dep = [pth, mth, lrt, sys_pcap]

executable_cflags = []
executable_linkflags = []
library_cflags = [largefile]
library_linkflags = []
b_sanitize_opt = get_option('b_sanitize')
if (b_sanitize_opt.contains('address') or b_sanitize_opt.contains('undefined')) and cc.get_id() == 'clang'
  executable_cflags += '-shared-libasan'
  executable_linkflags += '-shared-libasan'
  library_cflags += '-shared-libasan'
  library_linkflags += '-shared-libasan'
endif

platform_deps = []
platform_inc = ['.']
if host_machine.system() == 'windows'
  platform_deps = [
    cc.find_library('ws2_32'),
    cc.find_library('wininet'),
    cc.find_library('psapi'),
  ]
endif
platform_inc = include_directories(platform_inc)

simh_common = static_library('simh', simh_common_sources,
  c_args: library_cflags + simh_common_def,
  dependencies: [
	simh_common_dep,
	sys_pcap,
  ],
  install: true,
  implicit_include_directories: false,
  install_rpath: rpath_lib,
  link_args: library_linkflags
)

simh_common_dep = declare_dependency(link_with: simh_common)

# Emulator source files
subdir('3B2')
subdir('ALTAIR')
subdir('B5500')
subdir('BESM6')
subdir('CDC1700')
subdir('GRI')
subdir('H316')
subdir('HP2100')
subdir('HP3000')
subdir('I1401')
subdir('I1620')
subdir('I650')
subdir('I7000')
subdir('I7094')
subdir('Ibm1130')
subdir('Interdata')
subdir('LGP')
subdir('NOVA')
subdir('PDP1')
subdir('PDP10')
subdir('PDP11')
subdir('PDP18B')
subdir('PDP8')
subdir('PDQ-3')
subdir('S3')
subdir('SAGE')
subdir('SDS')
subdir('SSEM')
subdir('TX-0')
subdir('VAX')
subdir('alpha')
subdir('imlac')
subdir('sigma')
subdir('tt2500')


