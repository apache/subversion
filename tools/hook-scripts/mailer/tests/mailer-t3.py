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
import pathlib

# SCRIPT_DIR should be mailer/tests/
SCRIPT_DIR = pathlib.Path(__file__).parent.resolve()
T3_DIR = SCRIPT_DIR / 't3'

# We should find mailer.py in SCRIPT_DIR's parent
sys.path.insert(0, str(SCRIPT_DIR.parent))
import mailer

# Static input files.
DEFAULT_CONFIG = 'asf-mailer.conf'
DEFAULT_ORIGINAL = 'parsed.original'

# Generated output file, for comparison to DEFAULT_ORIGINAL.
PARSED_OUTPUT = 'parsed.new'


def test_config_parsing(repos_dir):

    cfg = mailer.Config(T3_DIR / DEFAULT_CONFIG,
                        repos_dir,
                        { 'author': 'johndoe',
                          'repos_basename': repos_dir.name,
                          })
    fp = open(os.path.join(T3_DIR, PARSED_OUTPUT), 'w')
    pprint.pprint(cfg._default_params, stream=fp)
    pprint.pprint(cfg._groups, stream=fp)
    pprint.pprint(sorted(cfg.__dict__.keys()), stream=fp)
    pprint.pprint(sorted(d for d in dir(cfg.maps) if not d.startswith('_')), stream=fp)
    pprint.pprint(cfg._group_re, stream=fp)

    # Try some particular lookups.
    groups = cfg.which_groups('/some/path', None)
    pprint.pprint(groups, stream=fp)
    pprint.pprint(cfg.get('to_addr', 't3-repos-1', groups[0][1]), stream=fp)
    pprint.pprint(cfg.get('from_addr', 't3-repos-1', groups[0][1]), stream=fp)


if __name__ == '__main__':
    repos_dir = pathlib.Path(sys.argv[1])
    test_config_parsing(repos_dir)
