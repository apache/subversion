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
  inc = sysconfig.get_python_inc()
  plat = sysconfig.get_python_inc(plat_specific=1)
  if inc == plat:
    print "-I" + inc
  else:
    print "-I%s -I%s" % (inc, plat)
  sys.exit(0)

if sys.argv[1] == '--compile':
  cc, opt, ccshared = sysconfig.get_config_vars('CC', 'OPT', 'CCSHARED')
  print cc, opt, ccshared
  sys.exit(0)

if sys.argv[1] == '--link':
  ### why the hell is this a list?!
  print sysconfig.get_config_vars('LDSHARED')[0]
  sys.exit(0)

usage()
