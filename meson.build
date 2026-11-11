project(
  'packet-processor',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=gnu23',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')

# GNU/Linux-specific feature definitions.
add_project_arguments(
  '-D_GNU_SOURCE',
  language: 'c',
)

# Compiler warnings.
add_project_arguments(
  '-Wshadow',
  '-Wconversion',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wformat=2',
  '-Wfunction-effects',
  language: 'c',
)

# Dependencies.
threads = dependency('threads')

# Sources.
sources = files(
  'source/allocator/slab.c',
  'source/dpdk/dpdk.c',
  'source/io_uring/ring_init.c',
  'source/main.c',
  'source/sz/arena.c',
  'source/sz/freelist.c',
  'source/sz/list.c',
  'source/sz/ring.c',
)

inc = include_directories('source/')
dpdk = dependency('libdpdk')

executable(
  'packet-processor',
  sources,
  include_directories: inc,
  dependencies: [threads, dpdk],
  install: true,
)
