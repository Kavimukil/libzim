

zimlibtest_sources = [
    'cluster.cpp',
    'dirent.cpp',
    'header.cpp',
    'main.cpp',
    'uuid.cpp',
    'zint.cpp',
    'template.cpp'
]

if zlib_dep.found()
    zimlibtest_sources += ['zlibstream.cpp']
endif
if lzma_dep.found()
    zimlibtest_sources += ['lzmastream.cpp']
endif

cxxtools_dep = dependency('cxxtools-unit', required:false)

if cxxtools_dep.found()
    zimlibtest = executable('zimlib-test', zimlibtest_sources,
                            include_directories : include_directory,
                            link_with : libzim,
                            dependencies : [cxxtools_dep])
                        
    test('zimlib test', zimlibtest)
else
    message('cxxtools-unit not found, tests are disabled.')
endif
