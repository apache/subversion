import glob
from build.builder import SvnBuild
from build.ext_lib import SystemLibrary

build = SvnBuild()
build.ext_lib(SystemLibrary("apr", libs=["aprutil", "apr"],
                            parseconfig=["apr-1-config --libs --cflags "
                                         "--ldflags --cppflags --includes"]))
build.ext_lib(SystemLibrary("xml2", pkgconfig=["libxml-2.0"]))
build.ext_lib(SystemLibrary("zlib", libs=["z"]))

build.library('subr', ext_libs=['apr', 'xml2', 'zlib'])
build.library('delta', ext_libs=['apr', 'zlib'], svn_libs=['subr'])
build.library('diff', ext_libs=['apr'], svn_libs=['subr'])
#build.library('client', ext_libs=['apr'],
#              svn_libs=['wc', 'ra', 'delta', 'diff', 'subr'])
build.library('fs_fs', ext_libs=['apr'],
              svn_libs=['subr', 'delta'])
#build.library('ra_local', ext_libs['apr']
