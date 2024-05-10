#!/usr/bin/python3
#
# USAGE:
#   $ ./tests/mailer-t3.py REPOS_DIR
#
# where REPOS_DIR might be constructed by "mailer-init.sh" and would
# look like "./tests/mailer-init.12345/repos"
#
# This script expects two files for input:
#
#    ./t3/asf-mailer.conf
#    ./t3/parsed.original
#
# The test produces ./t3/parsed.new and compares that to the original.
#
# NOTE: the input files are private to the ASF and are not present
# within the public Apache Subversion repository. However, this test
# can be repurposed to other organizations with complicated configs
# to test changes to mailer.py to ensure that the parsing of the config
# remains consistent across development changes to mailer.py
#

import sys
import os
import pprint

# SCRIPT_DIR should be mailer/tests/
SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
T3_DIR = os.path.join(SCRIPT_DIR, 't3')

# We should find mailer.py in SCRIPT_DIR's parent
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))
import mailer

# Static input files.
DEFAULT_CONFIG = 'asf-mailer.conf'
DEFAULT_ORIGINAL = 'parsed.original'

# Generated output file, for comparison to DEFAULT_ORIGINAL.
PARSED_OUTPUT = 'parsed.new'


def test_config_parsing(repos_dir):

    cfg = mailer.Config(os.path.join(T3_DIR, DEFAULT_CONFIG),
                        repos_dir,
                        { 'author': 'johndoe',
                          'repos_basename': os.path.basename(repos_dir),
                          })
    fp = open(os.path.join(T3_DIR, PARSED_OUTPUT), 'w')
    pprint.pprint(cfg._default_params, stream=fp)
    pprint.pprint(cfg._groups, stream=fp)
    pprint.pprint(cfg.__dict__.keys(), stream=fp)
    pprint.pprint(cfg.maps, stream=fp)
    pprint.pprint(cfg._group_re, stream=fp)


if __name__ == '__main__':
    repos_dir = sys.argv[1]
    test_config_parsing(repos_dir)
