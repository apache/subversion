#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#
# USAGE:
#    gen-make.py [-s] [BUILD-CONFIG]
#

import os
import sys
import getopt


sys.path.insert(0, os.path.join('build', 'generator'))

gen_modules = {
  'make' : ('gen_make', 'Makefiles for POSIX systems'),
  'dsp' : ('gen_msvc_dsp', 'DevStudio Project files'),
  'nmake-msvc' : ('gen_msvc_nmake', '### need description'),
  'mingw' : ('gen_mingw', 'Makefiles for mingw'),
  'vcproj' : ('gen_vcnet_vcproj', 'VC.Net Project files'),
  'nmake-vcnet' : ('gen_vcnet_nmake', '### need description'),
  'bpr' : ('gen_bcpp_bpr', '### need description'),
  'make-bcpp' : ('gen_bcpp_make', '### need description'),
  }

def main(fname, gentype, verfname=None, oname=None, skip_depends=0):
  if verfname is None:
    verfname = os.path.join('subversion', 'include', 'svn_version.h')

  try:
    gen_module = __import__(gen_modules[gentype][0])
  except ImportError:
    print 'ERROR: the "%s" generator is not yet implemented.' % gentype
    sys.exit(1)

  generator = gen_module.Generator(fname, verfname)
  if not skip_depends:
    generator.compute_hdr_deps()

  if oname is None:
    oname = generator.default_output(fname)

  generator.write(oname)


def _usage_exit():
  "print usage, exit the script"
  print "USAGE:  gen-make.py [-s] [conf-file] [TYPE]"
  print
  print "-s   skip dependency generation"
  print
  print "where TYPE is one of:"
  items = gen_modules.items()
  items.sort()
  for name, (module, desc) in items:
    print '%12s : %s' % (name, desc)
  print
  print "the default is 'make'"
  sys.exit(0)

if __name__ == '__main__':
  opts, args = getopt.getopt(sys.argv[1:], 's')
  if len(args) > 2:
    _usage_exit()

  conf = 'build.conf'
  gentype = 'make'

  if len(args) == 2:
    conf = args[0]
    gentype = args[1]
  elif args:
    if args[0] in gen_modules.keys():
      gentype = args[0]
    else:
      conf = args[0]
  if gentype not in gen_modules.keys():
    _usage_exit()

  if ('-s', '') in opts:
    skip = 1
  else:
    skip = 0

  main(conf, gentype, skip_depends=skip)


### End of file.
