#!/usr/bin/env python2.5

# This script converts a Subverson error code number to its symbolic
# name.

import csvn.core
import sys

if len(sys.argv) != 2:
    print "usage: python error-decode.py ERRORCODE"
    sys.exit(1)

code = int(sys.argv[1])

for name in dir(csvn.core):
    if name.startswith('SVN_ERR_'):
        if code == csvn.core.__dict__[name]:
            print name

