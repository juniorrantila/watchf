project('watchf', ['cpp', 'c'],
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'default_library=static',
    'buildtype=release',
    'cpp_rtti=false',
    'cpp_eh=none',
  ])

if not meson.is_subproject()
  add_global_arguments([
      '-Wno-c99-extensions',
      '-Wno-dollar-in-identifier-extension', 
      '-Wno-gnu-auto-type',
      '-Wno-gnu-case-range', 
      '-Wno-gnu-conditional-omitted-operand', 
      '-Wno-gnu-statement-expression', 
      '-Wno-gnu-zero-variadic-macro-arguments',
      '-Wno-user-defined-literals',
      '-nostdinc++',
      '-nostdlib++',
    ], language: 'cpp')
endif

subdir('src')
