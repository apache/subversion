#!/usr/bin/env python
#
#  utf8_tests.py:  testing the svn client's utf8 (i18n) handling
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2002 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import shutil, stat, string, sys, re, os.path

# Our testing module
import svntest
from svntest import wc

# (abbreviation)
Item = wc.StateItem

#--------------------------------------------------------------------
# Data

# Here's a filename and a log message which contain some high-ascii
# data.  In theory this data has different interpretations when
# converting from 2 different charsets into UTF-8.

i18n_filename = "b‘Á≈";

i18n_logmsg = "drie√´ntwintig keer was √©√©n keer teveel";


######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.


def basic_utf8_conversion(sbox):
  "conversion of paths and logs to/from utf8"

  if sbox.build():
    return 1

  wc_dir = sbox.wc_dir

  # Create the new i18n file and schedule it for addition
  svntest.main.file_append(os.path.join(wc_dir, i18n_filename), "hi")
  svntest.main.run_svn(None, 'add', os.path.join(wc_dir, i18n_filename))

  # Created expected output tree for 'svn ci'
  expected_output = wc.State(wc_dir, {
    i18n_filename : Item(verb='Adding'),
    })

  # Create expected status tree; all local revisions should be at 1,
  # but the new file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.tweak(wc_rev=1)
  expected_status.add({
    i18n_filename : Item(status='_ ', wc_rev=2, repos_rev=2),
    })

  return svntest.actions.run_and_verify_commit (wc_dir,
                                                expected_output,
                                                expected_status,
                                                None,
                                                None, None,
                                                None, None,
                                                wc_dir)
  
# Here's how the test should really work:

# 1. sh LC_ALL=ISO-8859-1 svn commit <filename> -m "<logmsg>"

# 2. sh LC_ALL=UTF-8 svn log -rHEAD > output

# 3. verify that output is the exact UTF-8 data that we expect.

# 4. repeat the process using some other locale other than ISO8859-1,
#    preferably some locale which will convert the high-ascii data to
#    *different* UTF-8.


  
#----------------------------------------------------------------------

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              basic_utf8_conversion
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
# local variables:
# eval: (load-file "../../../../tools/dev/svn-dev.el")
# end:
