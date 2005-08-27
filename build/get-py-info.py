#
# get-py-info.py: get various Python info (for building)
#
# This should be loaded/run by the appropriate Python, rather than executed
# directly as a program. In other words, you should:
#
#    $ python2 get-py-info.py --includes
#

import sys
import os
import string
from distutils import sysconfig

def usage():
  print 'USAGE: %s WHAT' % sys.argv[0]
  print '  Returns information about how to build Python extensions.'
  print '  WHAT may be one of:'
  print "    --includes : return -I include flags"
  print "    --compile  : return a compile command"
  print "    --link     : return a link command"
  print "    --libs     : return just the library options for linking"
  sys.exit(1)

if len(sys.argv) != 2:
  usage()

if sys.argv[1] == '--includes':
  inc = sysconfig.get_python_inc()
  plat = sysconfig.get_python_inc(plat_specific=1)
  if inc == plat:
    print "-I" + inc
  else:
    print "-I%s -I%s" % (inc, plat)
  sys.exit(0)

if sys.argv[1] == '--compile':
  cc, basecflags, opt, ccshared = \
      sysconfig.get_config_vars('CC', 'BASECFLAGS', 'OPT', 'CCSHARED')
  if basecflags:
    opt = basecflags + ' ' + opt
  print cc, opt, ccshared
  sys.exit(0)

def ldshared_process(just_libs = None):
  libdir = sysconfig.get_config_var('LIBDIR')
  ldshared = sysconfig.get_config_var('LDSHARED')
  ldlibrary = sysconfig.get_config_var('LDLIBRARY')
  libpyfwdir = sysconfig.get_config_var('PYTHONFRAMEWORKDIR')
  ldshared_elems = string.split(ldshared, " ")
  libs_elems = []
  for i in range(len(ldshared_elems)):
    if ldshared_elems[i] == '-framework':
      ldshared_elems[i] = '-Wl,' + ldshared_elems[i]
      ldshared_elems[i+1] = '-Wl,' + ldshared_elems[i+1]
      libs_elems.append(ldshared_elems[i])
      libs_elems.append(ldshared_elems[i+1])
    elif ldshared_elems[i][:2] == '-L':
      if ldshared_elems[i][:3] != '-L:':
        libs_elems.append(ldshared_elems[i])
    elif ldshared_elems[i][:2] == '-l':
      libs_elems.append(ldshared_elems[i])
  ldlibpath = os.path.join(libdir, ldlibrary)
  if libpyfwdir and libpyfwdir != "no-framework":
    libpyfw = sysconfig.get_config_var('PYTHONFRAMEWORK')
    py_lopt = "-framework " + libpyfw
    libs_elems.append(py_lopt)
    ldshared_elems.append(py_lopt)
  elif (os.path.exists(ldlibpath)):
    if libdir != '/usr/lib':
      py_Lopt = "-L" + libdir
      libs_elems.append(py_Lopt)
      ldshared_elems.append(py_Lopt)
    ldlibname, ldlibext = os.path.splitext(ldlibrary)
    if ldlibname[:3] == 'lib' and ldlibext == '.so':
      py_lopt = '-l' + ldlibname[3:]
    else:
      py_lopt = ldlibrary
    libs_elems.append(py_lopt)
    ldshared_elems.append(py_lopt)
  else:
    python_version = sys.version[:3]
    py_Lopt = "-L" + os.path.join(sys.prefix, "lib", "python" +
       python_version, "config")
    py_lopt = "-lpython" + python_version
    libs_elems.append(py_Lopt)
    libs_elems.append(py_lopt)
    ldshared_elems.append(py_Lopt)
    ldshared_elems.append(py_lopt)
  if just_libs:
    return string.join(libs_elems, " ")
  else:
    return string.join(ldshared_elems, " ")

if sys.argv[1] == '--link':
  print ldshared_process()
  sys.exit(0)

if sys.argv[1] == '--libs':
  print ldshared_process(just_libs = 1)
  sys.exit(0)

usage()
