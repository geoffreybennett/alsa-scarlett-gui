project('alsa-scarlett-gui','c',
        version: '0.4.0',
        license: 'GPLv3, LGPLv3',
        meson_version: '>= 1.0.0',
  )

#add_global_arguments( '-DSOME_TOKEN=value', language : 'c')
add_project_arguments( '-DVERSION="@0@"'.format(meson.project_version()), language : 'c')

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required : false)

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localedir = join_paths(prefix, get_option('localedir'))

glib_dep = dependency('glib-2.0', version : '>= 2.56.0')
gtk4_dep = dependency('gtk4', version : '>= 4.5.0')
alsa_dep = dependency('alsa', version : '>= 1.2.0')

root_inc = include_directories('.')

subdir('src')
subdir('demo')
subdir('img')
subdir('docs')

