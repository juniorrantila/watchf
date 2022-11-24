subdir('Ty')
subdir('Core')
subdir('Mem')
subdir('CLI')
subdir('Main')

watchf_exe = executable('watchf', [
    'main.cpp',
  ],
  include_directories: '.',
  dependencies: [
    cli_dep,
    core_dep,
    main_dep,
    mem_dep,
    ty_dep,
  ], install: true)
