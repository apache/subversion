import sys
if sys.hexversion < 0x2000000:
  print "pycheck: WARNING, Python 2.X or newer is required to run tests."
  sys.exit(1)
sys.exit(0)
