#!/usr/bin/env python
#
# run-repos-tests.py: run repository test programs

# Fix the import path
import os, sys
python_libs = os.path.join(os.path.dirname(sys.argv[0]), '../python-libs')
sys.path.insert(0, python_libs)

# Run the tests
import exectest
errors = exectest.run_tests(['repos-test'])
sys.exit(errors)


### End of file.
