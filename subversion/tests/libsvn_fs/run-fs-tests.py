#!/usr/bin/env python
#
# run-fs-tests.py: run filesystem test programs

# Fix the import path
import os, sys
python_libs = os.path.join(os.path.dirname(sys.argv[0]), '../python-libs')
sys.path.insert(0, python_libs)

# Run the tests
import exectest
errors = exectest.run_tests(['locks-test'])
sys.exit(errors)


### End of file.
