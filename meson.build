project('alsa-scarlett-gui','c',
        version: run_command('git', 'describe', '--abbrev=4', '--dirty', '--always', check: true).stdout().strip(),
        license: 'GPLv3, LGPLv3',
        meson_version: '>= 1.0.0',
  )

#add_global_arguments( '-DSOME_TOKEN=value', language : 'c')
add_project_arguments( '-DVERSION="@0@"'.format(meson.project_version()), language : 'c')

cc = meson.get_compiler('c')

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localedir = join_paths(prefix, get_option('localedir'))

# Compile time and link time dependencies.
m_dep = cc.find_library('m', required : false)
crypto_dep = dependency('libcrypto', required : true)

glib_dep = dependency('glib-2.0', version : '>= 2.56.0')
gtk4_dep = dependency('gtk4', version : '>= 4.5.0')
alsa_dep = dependency('alsa', version : '>= 1.2.0')

root_inc = include_directories('.')

subdir('src')
subdir('demo')
subdir('img')
subdir('docs')

