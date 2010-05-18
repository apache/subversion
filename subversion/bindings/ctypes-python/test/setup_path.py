#!/usr/bin/python -i

# Set things up to use the bindings in the build directory.
# Used by the testsuite, but you may also run:
#   $ python -i tests/setup_path.py
# to start an interactive interpreter.

import sys
import os

src_swig_python_tests_dir = os.path.dirname(os.path.dirname(__file__))
sys.path[0:0] = [ src_swig_python_tests_dir ]

import csvn.core
csvn.core.svn_cmdline_init("", csvn.core.stderr)
