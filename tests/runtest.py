#!/usr/bin/env python

import sys
sys.path.append(".")

if len(sys.argv) > 1:
    __import__(sys.argv[1])
else:
    tests = [ 'test1' ]
    for test in tests:
	print "Running test", test
        __import__(test)
