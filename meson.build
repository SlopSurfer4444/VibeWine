project('dxmt', ['c', 'cpp'], version : 'v0.1', meson_version: '>=1.3.0', default_options : [ 
  'cpp_std=c++20', 
  'warning_level=2', 
  'build.cpp_std=c++20',
  'b_ndebug=if-release',
])

fs = import('fs')

dxmt_crossbuild = meson.is_cross_build()

cpp = meson.get_compiler('cpp')
cc = meson.get_compiler('c')
clang = meson.get_compiler('c', native: true)
clangpp = meson.get_compiler('cpp', native: true)

cpu_family = host_machine.cpu_family()

dxmt_include_dirs = [
  './include',
  './libs'
]

dxmt_native_include_dirs = [
  './include/native/windows',
  './include/native/directx',
]

if not dxmt_crossbuild
  dxmt_include_dirs += dxmt_native_include_dirs
endif

source_prefix_path = fs.relative_to(
    meson.project_source_root(),
    meson.project_build_root()
)

compiler_args = [
  '-Wimplicit-fallthrough',
  # gcc
  '-Wno-missing-field-initializers',
  '-Wno-unused-parameter',
  '-Wno-cast-function-type', # Needed for GetProcAddress.
  # clang
  '-Wno-unused-private-field',
  '-Wno-microsoft-exception-spec',
  '-Wno-extern-c-compat',
  '-Wno-unused-const-variable',
  '-Wno-missing-braces',
  '-fblocks',
  '-fmacro-prefix-map=' + source_prefix_path + '/=',
]

if cpu_family == 'x86'
  compiler_args += [
    '-msse',
    '-msse2',
    '-msse3',
    '-mfpmath=sse',
  ]
endif

native_compiler_args = [
  '-Wimplicit-fallthrough',
  '-Wno-unused-parameter',
  '-Wno-unused-private-field',
  '-Wno-unused-const-variable',
  '-Wno-missing-braces',
  '-fblocks',
]

link_args = []

compiler_args += [
  '-DNOMINMAX',
  '-D_WIN32_WINNT=0xa00',
]

link_args += [
  '-static',
  # -static is sufficient for clang, while c runtime will be linked to api-ms-win-crt-* (ucrt)
  # '-static-libgcc',
  # '-static-libstdc++',
  # We need to set the section alignment for debug symbols to
  # work properly as well as avoiding a memcpy from the Wine loader.
  '-Wl,--file-alignment=4096',
]

# Wine's built-in back traces only work with dwarf4 symbols
if get_option('debug')
  compiler_args += [
    '-gdwarf-4',
  ]
endif

dxmt_include_path = include_directories(dxmt_include_dirs)
dxmt_native_include_path = include_directories(dxmt_native_include_dirs)

if not fs.is_file('include/native/directx/d3d11.h')
  error('Don\'t forget to clone submodules. `git submodule update --init --recursive`')
endif

if dxmt_crossbuild
  res_ext = '.o'
  wrc = find_program('windres')
  wrc_generator = generator(wrc,
    output    : [ '@BASENAME@' + res_ext ],
    arguments : [ '-i', '@INPUT@', '-o', '@OUTPUT@' ],
  )
else
  wrc           = find_program('touch')
  wrc_generator = generator(wrc, output : [ '@BASENAME@_ignored.h' ], arguments : [ '@OUTPUT@' ] )
endif

xcrun = find_program('xcrun')

metalir_generator = generator(xcrun,
  output    : [ '@BASENAME@.air'],
  arguments : [ '-sdk', 'macosx', 'metal',
    '-fmodules-cache-path=' + meson.project_build_root() / 'metal-module-cache',
    '-o', '@OUTPUT@', '-c', '@INPUT@', '@EXTRA_ARGS@'],
)

metallib_generator = generator(xcrun,
  output    : [ '@BASENAME@.metallib'],
  arguments : [ '-sdk', 'macosx', 'metallib', '-o', '@OUTPUT@', '@INPUT@'],
)

xxd = find_program('xxd')

hexdump_generator = generator(xxd,
  output    : [ '@BASENAME@.h'],
  arguments : [ '-n', '@BASENAME@', '-i', '@INPUT@', '@OUTPUT@'],
)

dxmt_debug = get_option('dxmt_debug')

# Keep the experiment selection identical in PE and native compilation. This
# changes submission grouping only; the default retains the existing grouping.
vibewine_scene_define = '-DDXMT_SCENE_SUBMISSION=' + get_option('vibewine_scene_submission').to_int().to_string()
add_project_arguments(vibewine_scene_define, language: ['c', 'cpp'])
if dxmt_crossbuild
  add_project_arguments(vibewine_scene_define, language: ['c', 'cpp'], native: true)
endif

# Independent performance experiments use identical PE/native definitions.
# Every switch is off in the ordinary build; a combined candidate is explicit.
foreach option_name, define_name : {
  'vibewine_precise_bindings' : 'DXMT_PRECISE_BINDINGS',
  'vibewine_dead_attachment_stores' : 'DXMT_DEAD_ATTACHMENT_STORES',
  'vibewine_physical_residency_dedup' : 'DXMT_PHYSICAL_RESIDENCY_DEDUP',
  'vibewine_residency_sets' : 'DXMT_RESIDENCY_SETS',
  'vibewine_idle_retirement' : 'DXMT_IDLE_RETIREMENT',
  'vibewine_fixed_output_metalfx' : 'DXMT_FIXED_OUTPUT_METALFX',
  'vibewine_shader_cleanup' : 'DXMT_SHADER_CLEANUP',
  'vibewine_demand_scheduler' : 'DXMT_DEMAND_SCHEDULER',
  'vibewine_discard_hints' : 'DXMT_DISCARD_HINTS',
  'vibewine_bounded_encoder_optimizer' : 'DXMT_BOUNDED_ENCODER_OPTIMIZER',
  'vibewine_render_pass_fusion' : 'DXMT_RENDER_PASS_FUSION',
  'vibewine_compute_pass_fusion' : 'DXMT_COMPUTE_PASS_FUSION',
  'vibewine_packed_ir_growth' : 'DXMT_PACKED_IR_GROWTH',
}
  experiment_define = '-D' + define_name + '=' + get_option(option_name).to_int().to_string()
  add_project_arguments(experiment_define, language: ['c', 'cpp'])
  if dxmt_crossbuild
    add_project_arguments(experiment_define, language: ['c', 'cpp'], native: true)
  endif
endforeach

if dxmt_debug
add_project_arguments('-DDXMT_DEBUG=1', language: 'cpp')
endif

# Corrected performance controls change only call grouping/getter repetition.
# Emit the same selection for PE and native C/C++; ABI/cache versions stay shared.
vibewine_perf_reference = get_option('vibewine_perf_reference')
vibewine_perf_defines = [
  '-DDXMT_PERF_REF_W4_GETTERS=' + (vibewine_perf_reference in ['w4', 'getters']).to_int().to_string(),
  '-DDXMT_PERF_REF_W4_BATCHING=' + (vibewine_perf_reference in ['w4', 'batching']).to_int().to_string(),
]
add_project_arguments(vibewine_perf_defines, language: ['c', 'cpp'])
if dxmt_crossbuild
  add_project_arguments(vibewine_perf_defines, language: ['c', 'cpp'], native: true)
endif

add_project_arguments('-DDXMT_PAGE_SIZE=4096', language: 'cpp')

dxmt_version = vcs_tag(
  command: ['git', 'describe', '--always'],
  input:  'version.h.in',
  output: 'version.h',
)

if dxmt_crossbuild
  wine_builtin_dll = get_option('wine_builtin_dll')

  if cpu_family == 'x86'
    # Enable stdcall fixup on 32-bit
    link_args += [
      '-Wl,--enable-stdcall-fixup',
      '-Wl,--kill-at',
    ]
    # Fix stack alignment issues with mingw on 32-bit
    compiler_args += [
      '-mpreferred-stack-boundary=2'
    ]
    if wine_builtin_dll
      windows_native_install_dir = 'i386-windows'
    else
      windows_native_install_dir = 'syswow64'
    endif
    windows_builtin_install_dir = 'i386-windows'
  elif cpu_family == 'x86_64'
    if wine_builtin_dll
      windows_native_install_dir = 'x86_64-windows'
    else
      windows_native_install_dir = 'system32'
    endif
    windows_builtin_install_dir = 'x86_64-windows'
  elif cpu_family == 'aarch64'
    if wine_builtin_dll
      windows_native_install_dir = 'aarch64-windows'
    else
      windows_native_install_dir = 'system32'
    endif
    windows_builtin_install_dir = 'aarch64-windows'
  else
    error('invalid cpu family')
  endif

  unix_install_dir = cpu_family + '-unix'
else
  wine_builtin_dll = false
  windows_native_install_dir = ''
endif

native_compiler_arch = cpu_family == 'aarch64' ? 'arm64' : 'x86_64'

add_project_arguments(cpp.get_supported_arguments(compiler_args), language: 'cpp')
add_project_arguments(clangpp.get_supported_arguments(native_compiler_args), language: 'cpp', native: true)
add_project_arguments(cc.get_supported_arguments(compiler_args), language: 'c')
add_project_arguments(clang.get_supported_arguments(native_compiler_args), language: 'c', native: true)
add_project_link_arguments(cpp.get_supported_link_arguments(link_args), language: 'cpp')
add_project_link_arguments(cc.get_supported_link_arguments(link_args), language: 'c')
if dxmt_crossbuild
  add_project_arguments(['-arch', native_compiler_arch], language: 'c',  native: true)
  add_project_arguments(['-arch', native_compiler_arch], language: 'cpp',  native: true)
  add_project_link_arguments(['-arch', native_compiler_arch], language: 'cpp',  native: true)
endif

subdir('libs')
subdir('src')

if get_option('enable_tests')
subdir('tests')
endif
