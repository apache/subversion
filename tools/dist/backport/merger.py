#!/usr/bin/env python3

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""
backport.merger - library for running STATUS merges
"""

import backport.status

import contextlib
import functools
import logging
import os
import re
import subprocess
import sys
import tempfile
import time
import unittest

logger = logging.getLogger(__name__)

# The 'svn' binary
SVN = os.getenv('SVN', 'svn')
# TODO: maybe run 'svn info' to check if it works / fail early?


class UnableToMergeException(Exception):
  pass


def invoke_svn(argv):
  "Run svn with ARGV as argv[1:].  Return (exit_code, stdout, stderr)."
  # TODO(interactive mode): disable --non-interactive
  child_env = os.environ.copy()
  child_env.update({'LC_ALL': 'C'})
  argv = [SVN, '--non-interactive', '--config-option=config:miscellany:log-encoding=UTF-8'] + argv
  child = subprocess.Popen(argv,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           env=child_env)
  stdout, stderr = child.communicate()
  return child.returncode, stdout.decode('UTF-8'), stderr.decode('UTF-8')

def run_svn(argv, expected_stderr=None):
  """Run svn with ARGV as argv[1:].  If EXPECTED_STDERR is None, raise if the
  exit code is non-zero or stderr is non-empty.  Else, treat EXPECTED_STDERR as
  a regexp, and ignore an errorful exit or stderr messages if the latter match
  the regexp.  Return exit_code, stdout, stderr."""

  exit_code, stdout, stderr = invoke_svn(argv)
  if exit_code == 0 and not stderr:
    return exit_code, stdout, stderr
  elif expected_stderr and re.compile(expected_stderr).search(stderr):
    return exit_code, stdout, stderr
  else:
    logger.warning("Unexpected stderr: %r", stderr)
    # TODO: pass stdout/stderr to caller?
    raise subprocess.CalledProcessError(returncode=exit_code,
                                        cmd=[SVN] + argv)

def run_svn_quiet(argv, *args, **kwargs):
  "Wrapper for run_svn(-q)."
  return run_svn(['-q'] + argv, *args, **kwargs)

class Test_invoking_cmdline_client(unittest.TestCase):
  def test_run_svn(self):
    _, stdout, _ = run_svn(['--version', '-q'])
    self.assertRegex(stdout, r'^1\.[0-9]+\.[0-9]+')

    run_svn(['--version', '--no-such-option'], "invalid option")

    with self.assertLogs() as cm:
      with self.assertRaises(subprocess.CalledProcessError):
        run_svn(['--version', '--no-such-option'])
      self.assertRegex(cm.output[0], "Unexpected stderr.*")

  def test_svn_version(self):
    self.assertGreaterEqual(svn_version(), (1, 0))


@functools.lru_cache(maxsize=1)
def svn_version():
  "Return the version number of the 'svn' binary as a (major, minor) tuple."
  _, stdout, _ = run_svn(['--version', '-q'])
  match = re.compile(r'(\d+)\.(\d+)').match(stdout)
  assert match
  return tuple(map(int, match.groups()))

def run_revert():
  return run_svn(['revert', '-q', '-R', './'])

def last_changed_revision(path_or_url):
  "Return the 'Last Changed Rev:' of PATH_OR_URL."

  if svn_version() >= (1, 9):
    return int(run_svn(['info', '--show-item=last-changed-revision', '--',
                        path_or_url])[1])
  else:
    _, lines, _ = run_svn(['info', '--', path_or_url]).splitlines()
    for line in lines:
      if line.startswith('Last Changed Rev:'):
        return int(line.split(':', 1)[1])
    else:
      raise Exception("'svn info' did not print last changed revision")

def no_local_mods(path):
  "Check PATH for local mods.  Raise if there are any."
  if run_svn(['status', '-q', '--', path])[1]:
    raise UnableToMergeException("Local mods on {!r}".format(path))

def _includes_only_svn_mergeinfo_changes(status_output):
  """Return TRUE iff there is exactly one local mod, and it is an svn:mergeinfo
  change.  Use the provided `status -q` output."""

  if len(status_output.splitlines()) != 1:
    return False

  _, diff_output, _ = run_svn(['diff'])

  pattern = re.compile(r'^(Added|Modified|Deleted): ')
  targets = (line.split(':', 1)[1].strip()
             for line in diff_output.splitlines()
               if pattern.match(line))
  if set(targets) == {'svn:mergeinfo'}:
    return True

  return False


@contextlib.contextmanager
def log_message_file(logmsg):
  "Context manager that returns a file containing the text LOGMSG."
  with tempfile.NamedTemporaryFile(mode='w+', encoding="UTF-8") as logmsg_file:
    logmsg_file.write(logmsg)
    logmsg_file.flush()
    yield logmsg_file.name
  
def merge(entry, expected_stderr=None, *, commit=False):
  """Merges ENTRY into the working copy at cwd.

  Do not commit the result, unless COMMIT is true.  When committing,
  remove ENTRY from its STATUS file prior to committing.
  
  EXPECTED_STDERR will be passed to run_svn() for the actual 'merge' command."""

  assert isinstance(entry, backport.status.StatusEntry)
  assert entry.valid()
  assert entry.status_file

  sf = entry.status_file

  # TODO(interactive mode): catch the exception
  validate_branch_contains_named_revisions(entry)

  # Prepare mergeargs and logmsg.
  logmsg = ""
  if entry.branch:
    branch_url = sf.branch_url(entry.branch)
    if svn_version() >= (1, 8):
      mergeargs = ['--', branch_url]
      logmsg = "Merge {}:\n".format(entry.noun())
      reintegrated_word = "merged"
    else:
      mergeargs = ['--reintegrate', '--', branch_url]
      logmsg = "Reintegrate {}:\n".format(entry.noun())
      reintegrated_word = "reintegrated"
    logmsg += "\n"
  elif entry.revisions:
    mergeargs = []
    if entry.accept:
      mergeargs.append('--accept=%s' % (entry.accept,))
      logmsg += "Merge {} from trunk, with --accept={}:\n".\
                    format(entry.noun(), entry.accept)
    else:
      logmsg += "Merge {} from trunk:\n".format(entry.noun())
    logmsg += "\n"
    mergeargs.extend('-c' + str(revision) for revision in entry.revisions)
    mergeargs.extend(['--', sf.trunk_url()])
  logmsg += entry.raw

  no_local_mods('.')

  # TODO: use select() to restore interweaving of stdout/stderr
  _, stdout, stderr = run_svn_quiet(['merge'] + mergeargs, expected_stderr)
  sys.stdout.write(stdout)
  sys.stderr.write(stderr)

  _, stdout, _ = run_svn(['status', '-q'])
  if _includes_only_svn_mergeinfo_changes(stdout):
    raise UnableToMergeException("Entry %s includes only svn:mergeinfo changes"
                                 % entry)

  if commit:
    sf.remove(entry)
    sf.unparse(open('./STATUS', 'w'))

    # HACK to make backport_tests pass - the tests should be changed!
    s = open('./STATUS').read()
    if s.endswith('\n\n'):
      s = s[:-1]
    open('./STATUS', 'w').write(s)

    # Don't assume we can pass UTF-8 in argv.
    with log_message_file(logmsg) as logmsg_filename:
      run_svn_quiet(['commit', '-F', logmsg_filename])

  # TODO(interactive mode): add the 'svn status' display

  if entry.branch:
    revnum = last_changed_revision('./STATUS')
    
    if commit:
      # Sleep to avoid out-of-order commit notifications
      if not os.getenv("SVN_BACKPORT_DONT_SLEEP"): # enabled by the test suite
          time.sleep(15)
      second_logmsg = "Remove the {!r} branch, {} in r{}."\
                          .format(entry.branch, reintegrated_word, revnum)
      run_svn(['rm', '-m', second_logmsg, '--', branch_url])
      time.sleep(1)

def validate_branch_contains_named_revisions(entry):
  """Validate that every revision explicitly named in ENTRY has either been
  merged to its backport branch from trunk, or has been committed directly to
  its backport branch.  Entries that declare no backport branches are
  considered valid.  Return on success, raise on failure."""
  if not entry.branch:
    return # valid

  if svn_version() < (1,5): # doesn't have 'svn mergeinfo' subcommand
    return # skip check

  sf = entry.status_file
  branch_url = sf.branch_url(entry.branch)
  present_str = (
    run_svn(['mergeinfo', '--show-revs=merged', '--', sf.trunk_url(), branch_url])[1]
    +
    run_svn(['mergeinfo', '--show-revs=eligible', '--', branch_url])[1]
  )

  present = map(int, re.compile(r'(\d+)').findall(present_str))

  absent = set(entry.revisions) - set(present)

  if absent:
    raise UnableToMergeException("Revisions '{}' nominated but not included "
                                 "in branch".format(
                                   ', '.join('r%d' % revno
                                             for revno in absent)))



def setUpModule():
  "Set-up function, invoked by 'python -m unittest'."
  # Suppress warnings generated by the test data.
  # TODO: some test functions assume .assertLogs is available, they fail with
  # AttributeError if it's absent (e.g., on python < 3.4).
  try:
    unittest.TestCase.assertLogs
  except AttributeError:
    logger.setLevel(logging.ERROR)

if __name__ == '__main__':
  unittest.main()
