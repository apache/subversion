#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#
# USAGE:
#    gen-make.py [-s] [BUILD-CONFIG]
#

import os, sys

sys.path.insert(0, 'build')
import gen_make

def main(fname, verfname=None, oname=None, skip_depends=0):
  if oname is None:
    oname = os.path.splitext(os.path.basename(fname))[0] + '-outputs.mk'
  if verfname is None:
    verfname = os.path.join('subversion', 'include', 'svn_version.h')
  generator = gen_make.MakefileGenerator(fname, verfname, oname)
  generator.write()
  if not skip_depends:
    generator.write_depends()


def _usage_exit():
  "print usage, exit the script"
  print "usage:  gen-make.py [-s] conf-file\n"
  sys.exit(0)

if __name__ == '__main__':
  argc = len(sys.argv)

  fname = "build.conf"
  skip = 0
  optind = 1
  if argc > optind and sys.argv[optind] == '-s':
    skip = 1
    optind += 1
  if argc > optind:
    fname = sys.argv[optind]
    optind += 1
  if argc > optind:
    _usage_exit()
  main(fname, skip_depends=skip)


### End of file.
# local variables:
# eval: (load-file "tools/dev/svn-dev.el")
# end:
