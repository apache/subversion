# Copyright (c) 2011 elego Software Solutions GmbH <info@elego.de>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# Python module helpful for writing hook scripts.
#
# This is mostly an example for calling 'svnlook' and 'svn' from Python hooks
# and for reading simple configuration files. While it's an example, this may
# also be useful in a production environment as is.
#
# To use this, place this file next to your python hook scripts (e.g. in
# 'repos/hooks/'). To be able use these functions, you may simply write:
#   from hook_toolbox import *

# NOTE: Adjust the BIN_PATH according to your operating system, to get valid
# paths to your binaries (e.g. '/usr/bin/svnlook'):
BIN_PATH = '/usr/bin'


import sys, os, subprocess, shlex


def get_log_message(*args):
  '''Synopsis:
       get_log_message(repos, '-t', transaction)   (from pre-commit hook)
       get_log_message(repos, '-r', revision)      (from post-commit hook)
     Returns the log message as a string.
  '''
  cmd = [os.path.join(BIN_PATH, 'svnlook'), 'propget', '--revprop']
  cmd.extend(args)
  cmd.append('svn:log')
  return run(*cmd)


def get_changed_paths(*args):
  '''Synopsis:
       get_changed_paths(repos, '-t', transaction)   (from pre-commit hook)
       get_changed_paths(repos, '-r', revision)      (from post-commit hook)
     Returns the list of changed paths, relative to the repository root.
  '''
  cmd = [os.path.join(BIN_PATH, 'svnlook'), 'changed']
  cmd.extend(args)
  changes = run(*cmd)
  #print changes

  # First four chars of each svnlook output line show the kind of change.
  # The rest of the line is the complete repository path. See:
  # http://svnbook.red-bean.com/nightly/en/svn.ref.svnlook.c.changed.html
  changed_paths = [ line[4:] for line in changes.split('\n') if len(line) > 4 ]

  #print '%s:\n '%(' '.join(args)), '\n  '.join(changed_paths)
  return changed_paths


def has_path_changed(path, changed_paths):
  '''Returns True if any changed path begins with the given path.
     path: A path relative to the repos root, without leading slash.
     changed_paths: A list obtained from the function get_changed_paths().'''

  for changed_path in changed_paths:
    if changed_path.startswith(path):
      return True
  return False


def read_config(repos, filename, expected_tokens_per_line=-1):
  '''Reads the file <repos>/conf/<filename> line wise and tokenizes each
  line according to shell syntax rules. For example, a file like

   # comments & blank lines ignored

   aaa bb
   cccc "d d d" ee
   ff

  would return a list

   [ ['aaa', 'bb'],
     ['cccc', 'd d d', 'ee'],
     ['ff']                   ]

  If expected_tokens_per_line is > 0, then only the lines matching the
  given number of tokens are returned. In above example, passing
  expected_tokens_per_line=2 would yield just:

   [ ['aaa', 'bb'] ]

  Returns an empty list if no such config file exists.
  '''
  path = os.path.join(repos, 'conf', filename)
  if not os.path.exists(path):
    print 'Not present:', path
    return []

  config_lines = open(path).readlines()

  tokenized_lines = [ shlex.split(line, True) for line in config_lines ]
  tokenized_lines = [ tokens for tokens in tokenized_lines if tokens ]

  if expected_tokens_per_line < 1:
    return tokenized_lines

  matching_lines = [ tokens for tokens in tokenized_lines
                     if len(tokens) == expected_tokens_per_line ]

  if len(matching_lines) < len(tokenized_lines):
    print '*** %d syntax errors in %s' % (
             len(tokenized_lines) - len(matching_lines),
             path)

  return matching_lines


def update_working_copy(wc_path):
  if not os.path.exists(wc_path):
    print '--> *** Cannot find working copy', wc_path
    return None
  return run(os.path.join(BIN_PATH, 'svn'), 'update', wc_path)


def run(*cmd):
  '''Call the given command & args and return what it printed to stdout.
     e.g. result = run('/usr/bin/svn', 'info', wc_dir_path) '''
  print '-->', ' '.join(cmd)
  stdout = subprocess.Popen(cmd, stdout=subprocess.PIPE).communicate()[0]
  print stdout.strip()
  return stdout

