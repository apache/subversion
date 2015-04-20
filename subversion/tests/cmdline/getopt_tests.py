#!/usr/bin/env python
#
#  getopt_tests.py:  testing the svn command line processing
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import sys, re, os.path, logging

logger = logging.getLogger()

# Our testing module
import svntest


######################################################################
# Tests

#----------------------------------------------------------------------

# This directory contains all the expected output from svn.
getopt_output_dir = os.path.join(os.path.dirname(sys.argv[0]),
                                 'getopt_tests_data')

# Naming convention for golden files: take the svn command line as a
# single string and apply the following sed transformations:
#   echo svn option1 option2 ... | sed -e 's/ /_/g' -e 's/_--/--/g'
# Then append either _stdout or _stderr for the file descriptor to
# compare against.

def load_expected_output(basename):
  "load the expected standard output and standard error"

  stdout_filename = os.path.join(getopt_output_dir, basename + '_stdout')
  stderr_filename = os.path.join(getopt_output_dir, basename + '_stderr')

  exp_stdout = open(stdout_filename, 'r').readlines()
  exp_stderr = open(stderr_filename, 'r').readlines()

  return exp_stdout, exp_stderr

# With plaintext password storage enabled, `svn --version' emits a warning:
warn_line_re = re.compile("WARNING: Plaintext password storage")

# This is a list of lines to delete.
del_lines_res = [
                 # In 'svn --version', the date line is variable, for example:
                 # "compiled Apr  5 2002, 10:08:45"
                 re.compile(r'\s+compiled\s+'),

                 # Also for 'svn --version':
                 re.compile(r"\* ra_(neon|local|svn|serf) :"),
                 re.compile(r"  - handles '(https?|file|svn)' scheme"),
                 re.compile(r"  - with Cyrus SASL authentication"),
                 re.compile(r"  - using serf \d+\.\d+\.\d+"),
                 re.compile(r"\* fs_(base|fs) :"),

                 # Remove 'svn --version' list of platform-specific
                 # auth cache providers.
                 re.compile(r"\* Wincrypt cache.*"),
                 re.compile(r"\* Plaintext cache.*"),
                 re.compile(r"\* Gnome Keyring"),
                 re.compile(r"\* GPG-Agent"),
                 re.compile(r"\* Mac OS X Keychain"),
                 re.compile(r"\* KWallet \(KDE\)"),
                ]

# This is a list of lines to search and replace text on.
rep_lines_res = [
                 # In 'svn --version', this line varies, for example:
                 # "Subversion Client, version 0.10.2-dev (under development)"
                 # "Subversion Client, version 0.10.2 (r1729)"
                 (re.compile(r'version \d+\.\d+\.\d+(-[^ ]*)? \(.*\)'),
                  'version X.Y.Z '),
                 # The copyright end date keeps changing; fix forever.
                 (re.compile(r'Copyright \(C\) 20\d\d The Apache '
                              'Software Foundation\.'),
                  'Copyright (C) YYYY The Apache Software Foundation'),
                 # In 'svn --version --quiet', we print only the version
                 # number in a single line.
                 (re.compile(r'^\d+\.\d+\.\d+(-[a-zA-Z0-9]+)?$'), 'X.Y.Z\n'),
                ]

# This is a trigger pattern that selects the secondary set of
# delete/replace patterns
switch_res_line = 'System information:'

# This is a list of lines to delete after having seen switch_res_line.
switched_warn_line_re = None
switched_del_lines_res = [
                          # In svn --version --verbose, dependent libs loaded
                          # shared libs are optional.
                          re.compile(r'^\* (loaded|linked)'),
                          # In svn --version --verbose, remove everything from
                          # the extended lists
                          re.compile(r'^  - '),
                         ]

# This is a list of lines to search and replace text on after having
# seen switch_res_line.
switched_rep_lines_res = [
                          # We don't care about the actual canonical host
                          (re.compile('^\* running on.*$'), '* running on'),
                         ]

def process_lines(lines):
  "delete lines that should not be compared and search and replace the rest"
  output = [ ]
  warn_re = warn_line_re
  del_res = del_lines_res
  rep_res = rep_lines_res

  skip_next_line = 0
  for line in lines:
    if skip_next_line:
      skip_next_line = 0
      continue

    if line.startswith(switch_res_line):
      warn_re = switched_warn_line_re
      del_res = switched_del_lines_res
      rep_res = switched_rep_lines_res

    # Skip these lines from the output list.
    delete_line = 0
    if warn_re and warn_re.match(line):
      delete_line = 1
      skip_next_line = 1     # Ignore the empty line after the warning
    else:
      for delete_re in del_res:
        if delete_re.match(line):
          delete_line = 1
          break
    if delete_line:
      continue

    # Search and replace text on the rest.
    for replace_re, replace_str in rep_res:
      line = replace_re.sub(replace_str, line)

    output.append(line)

  return output

def run_one_test(sbox, basename, *varargs):
  "run svn with args and compare against the specified output files"

  ### no need to use sbox.build() -- we don't need a repos or working copy
  ### for these tests.

  exp_stdout, exp_stderr = load_expected_output(basename)

  # special case the 'svn' test so that no extra arguments are added
  if basename != 'svn':
    exit_code, actual_stdout, actual_stderr = svntest.main.run_svn(1, *varargs)
  else:
    exit_code, actual_stdout, actual_stderr = svntest.main.run_command(svntest.main.svn_binary,
                                                                       1, False, *varargs)

  # Delete and perform search and replaces on the lines from the
  # actual and expected output that may differ between build
  # environments.
  exp_stdout    = process_lines(exp_stdout)
  exp_stderr    = process_lines(exp_stderr)
  actual_stdout = process_lines(actual_stdout)
  actual_stderr = process_lines(actual_stderr)

  svntest.verify.compare_and_display_lines("Standard output does not match.",
                                           "STDOUT", exp_stdout, actual_stdout)

  svntest.verify.compare_and_display_lines("Standard error does not match.",
                                           "STDERR", exp_stderr, actual_stderr)

def getopt_no_args(sbox):
  "run svn with no arguments"
  run_one_test(sbox, 'svn')

def getopt__version(sbox):
  "run svn --version"
  run_one_test(sbox, 'svn--version', '--version')

def getopt__version__quiet(sbox):
  "run svn --version --quiet"
  run_one_test(sbox, 'svn--version--quiet', '--version', '--quiet')

def getopt__version__verbose(sbox):
  "run svn --version --verbose"
  run_one_test(sbox, 'svn--version--verbose', '--version', '--verbose')

def getopt__help(sbox):
  "run svn --help"
  run_one_test(sbox, 'svn--help', '--help')

def getopt_help(sbox):
  "run svn help"
  run_one_test(sbox, 'svn_help', 'help')

def getopt_help_log_switch(sbox):
  "run svn help log switch"
  run_one_test(sbox, 'svn_help_log_switch', 'help', 'log', 'switch')

def getopt_help_bogus_cmd(sbox):
  "run svn help bogus-cmd"
  run_one_test(sbox, 'svn_help_bogus-cmd', 'help', 'bogus-cmd')

def getopt_config_option(sbox):
  "--config-option's spell checking"
  sbox.build(create_wc=False, read_only=True)
  expected_stderr = '.*W205000.*did you mean.*'
  expected_stdout = svntest.verify.AnyOutput
  svntest.actions.run_and_verify_svn2(expected_stdout, expected_stderr, 0,
                                      'info', 
                                      '--config-option',
                                      'config:miscellanous:diff-extensions=' +
                                        '-u -p',
                                      sbox.repo_url)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              getopt_no_args,
              getopt__version,
              getopt__version__quiet,
              getopt__version__verbose,
              getopt__help,
              getopt_help,
              getopt_help_bogus_cmd,
              getopt_help_log_switch,
              getopt_config_option,
            ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
