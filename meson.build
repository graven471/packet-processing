project(
  'packet-processor',
  'c',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=gnu23',
    'cpp_std=c++23',
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
  # '-Wconversion',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wformat=2',
  '-Wfunction-effects',
  language: 'c',
)

if get_option('buildtype').startswith('debug')
  add_project_arguments('-DDEBUG_BUILD=1', language: 'c')
endif

# Dependencies.
threads = dependency('threads')

# Sources.
c_sources = files(
  'source/allocator/slab.c',
  'source/dpdk/dpdk.c',
  'source/h264/nal.c',
  'source/io_uring/ring_init.c',
  'source/io_uring/ring_ops.c',
  'source/main.c',
  'source/rtp/parser.c',
  'source/sz/arena.c',
  'source/sz/freelist.c',
  'source/sz/list.c',
  'source/sz/ring.c',
  'source/vk/vk_init.c',
  'source/vk/volk_impl.c',
)

inc = include_directories('source/')
dpdk = dependency('libdpdk')
vulkan = dependency('vulkan')

executable(
  'packet-processor',
  c_sources,
  include_directories: inc,
  dependencies: [threads, dpdk, vulkan],
  install: true,
)
