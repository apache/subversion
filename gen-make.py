#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#
# USAGE:
#    gen-make.py [-s] BUILD-CONFIG
#

import os, sys

sys.path.insert(0, 'build')
import gen_base

def main(fname, oname=None, skip_depends=0):
  if oname is None:
    oname = os.path.splitext(os.path.basename(fname))[0] + '-outputs.mk'
  generator = gen_base.MakefileGenerator(fname, oname)
  generator.write()
  if not skip_depends:
    generator.write_depends()


def _usage_exit():
  "print usage, exit the script"
  print "usage:  gen-make.py [-s] conf-file\n"
  sys.exit(0)

if __name__ == '__main__':
  argc = len(sys.argv)

  if argc == 1:
    _usage_exit()
  if sys.argv[1] == '-s':
    if argc == 2:
      _usage_exit()
    skip = 1
    fname = sys.argv[2]
  else:
    skip = 0
    fname = sys.argv[1]
  main(fname, skip_depends=skip)
