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
from distutils import sysconfig

def usage():
  print 'USAGE: %s WHAT' % sys.argv[0]
  print '  where WHAT may be one of:'
  print "    --includes : return the directory for Python's includes"
  sys.exit(1)

if len(sys.argv) != 2:
  usage()

if sys.argv[1] == '--includes':
  print sysconfig.get_python_inc()
  sys.exit(0)

usage()
