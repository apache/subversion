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


sys.path.insert(0, 'build')
import gen_make

def main(fname, verfname=None, oname=None, skip_depends=0):
  if oname is None:
    oname = os.path.splitext(os.path.basename(fname))[0] + '-outputs.mk'
  if verfname is None:
    verfname = os.path.join('subversion', 'include', 'svn_version.h')
  generator = gen_make.MakefileGenerator(fname, verfname, oname)
  if not skip_depends:
    generator.compute_hdr_deps()
  generator.write()


def _usage_exit():
  "print usage, exit the script"
  print "usage:  gen-make.py [-s] [conf-file]\n"
  sys.exit(0)

if __name__ == '__main__':
  opts, args = getopt.getopt(sys.argv[1:], 's')
  if len(args) > 1:
    _usage_exit()

  if args:
    fname = args[0]
  else:
    fname = 'build.conf'

  if ('-s', '') in opts:
    skip = 1
  else:
    skip = 0

  main(fname, skip_depends=skip)


### End of file.
