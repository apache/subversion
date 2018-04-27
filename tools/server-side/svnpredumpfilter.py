#!/usr/bin/env python

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
# ====================================================================
"""\
Usage:  1. {PROGRAM} [OPTIONS] include INCLUDE-PATH ...
        2. {PROGRAM} [OPTIONS] exclude EXCLUDE-PATH ...

Read a Subversion revision log output stream from stdin, analyzing its
revision log history to see what paths would need to be additionally
provided as part of the list of included/excluded paths if trying to
use Subversion's 'svndumpfilter' program to include/exclude paths from
a full dump of a repository's history.

The revision log stream should be the result of 'svn log -v' or 'svn
log -vq' when run against the root of the repository whose history
will be filtered by a user with universal read access to the
repository's data.  Do not use the --use-merge-history (-g) or
--stop-on-copy when generating this revision log stream.
Use the default ordering of revisions (that is, '-r HEAD:0').

Return errorcode 0 if there are no additional dependencies found, 1 if
there were; any other errorcode indicates a fatal error.

Paths in mergeinfo are not considered as additional dependencies so the
--skip-missing-merge-sources option of 'svndumpfilter' may be required
for successful filtering with the resulting path list.

Options:

   --help (-h)           Show this usage message and exit.

   --targets FILE        Read INCLUDE-PATHs and EXCLUDE-PATHs from FILE,
                         one path per line.

   --verbose (-v)        Provide more information.  May be used multiple
                         times for additional levels of information (-vv).
"""
import sys
import os
import getopt
import string

verbosity = 0

class LogStreamError(Exception): pass
class EOFError(Exception): pass

EXIT_SUCCESS = 0
EXIT_MOREDEPS = 1
EXIT_FAILURE = 2

def sanitize_path(path):
  return '/'.join(filter(None, path.split('/')))

def subsumes(path, maybe_child):
  if path == maybe_child:
    return True
  if maybe_child.startswith(path + '/'):
    return True
  return False

def compare_paths(path1, path2):
  # Are the paths exactly the same?
  if path1 == path2:
    return 0

  # Skip past common prefix
  path1_len = len(path1);
  path2_len = len(path2);
  min_len = min(path1_len, path2_len)
  i = 0
  while (i < min_len) and (path1[i] == path2[i]):
    i = i + 1

  # Children of paths are greater than their parents, but less than
  # greater siblings of their parents
  char1 = '\0'
  char2 = '\0'
  if (i < path1_len):
    char1 = path1[i]
  if (i < path2_len):
    char2 = path2[i]

  if (char1 == '/') and (i == path2_len):
    return 1
  if (char2 == '/') and (i == path1_len):
    return -1
  if (i < path1_len) and (char1 == '/'):
    return -1
  if (i < path2_len) and (char2 == '/'):
    return 1

  # Common prefix was skipped above, next character is compared to
  # determine order
  return cmp(char1, char2)

def log(msg, min_verbosity):
  if verbosity >= min_verbosity:
    if min_verbosity == 1:
      sys.stderr.write("[* ] ")
    elif min_verbosity == 2:
      sys.stderr.write("[**] ")
    sys.stderr.write(msg + "\n")

class DependencyTracker:
  def __init__(self, include_paths):
    self.include_paths = set(include_paths)
    self.dependent_paths = set()

  def path_included(self, path):
    for include_path in self.include_paths | self.dependent_paths:
      if subsumes(include_path, path):
        return True
    return False

  def include_missing_copies(self, path_copies):
    while True:
      log("Cross-checking %d included paths with %d copies "
          "for missing path dependencies..." % (
            len(self.include_paths) + len(self.dependent_paths),
            len(path_copies)),
          1)
      included_copies = []
      for path, copyfrom_path in path_copies:
        if self.path_included(path):
          log("Adding copy '%s' -> '%s'" % (copyfrom_path, path), 1)
          self.dependent_paths.add(copyfrom_path)
          included_copies.append((path, copyfrom_path))
      if not included_copies:
        log("Found all missing path dependencies", 1)
        break
      for path, copyfrom_path in included_copies:
        path_copies.remove((path, copyfrom_path))
      log("Found %d new copy dependencies, need to re-check for more"
        % len(included_copies), 1)

def readline(stream):
  line = stream.readline()
  if not line:
    raise EOFError("Unexpected end of stream")
  line = line.rstrip('\n\r')
  log(line, 2)
  return line

def svn_log_stream_get_dependencies(stream, included_paths):
  import re

  dt = DependencyTracker(included_paths)

  header_re = re.compile(r'^r([0-9]+) \|.*$')
  action_re = re.compile(r'^   [ADMR] /(.*)$')
  copy_action_re = re.compile(r'^   [AR] /(.*) \(from /(.*):[0-9]+\)$')
  line_buf = None
  last_revision = 0
  eof = False
  path_copies = set()
  found_changed_path = False

  while not eof:
    try:
      line = line_buf is not None and line_buf or readline(stream)
    except EOFError:
      break

    # We should be sitting at a log divider line.
    if line != '-' * 72:
      raise LogStreamError("Expected log divider line; not found.")

    # Next up is a log header line.
    try:
      line = readline(stream)
    except EOFError:
      break
    match = header_re.search(line)
    if not match:
      raise LogStreamError("Expected log header line; not found.")
    pieces = map(string.strip, line.split('|'))
    revision = int(pieces[0][1:])
    if last_revision and revision >= last_revision:
      raise LogStreamError("Revisions are misordered.  Make sure log stream "
                           "is from 'svn log' with the youngest revisions "
                           "before the oldest ones (the default ordering).")
    log("Parsing revision %d" % (revision), 1)
    last_revision = revision
    idx = pieces[-1].find(' line')
    if idx != -1:
      log_lines = int(pieces[-1][:idx])
    else:
      log_lines = 0

    # Now see if there are any changed paths.  If so, parse and process them.
    line = readline(stream)
    if line == 'Changed paths:':
      while 1:
        try:
          line = readline(stream)
        except EOFError:
          eof = True
          break
        match = copy_action_re.search(line)
        if match:
          found_changed_path = True
          path_copies.add((sanitize_path(match.group(1)),
                           sanitize_path(match.group(2))))
        elif action_re.search(line):
          found_changed_path = True
        else:
          break

    # Finally, skip any log message lines.  (If there are none,
    # remember the last line we read, because it probably has
    # something important in it.)
    if log_lines:
      for i in range(log_lines):
        readline(stream)
      line_buf = None
    else:
      line_buf = line

  if not found_changed_path:
    raise LogStreamError("No changed paths found; did you remember to run "
                         "'svn log' with the --verbose (-v) option when "
                         "generating the input to this script?")

  dt.include_missing_copies(path_copies)
  return dt

def analyze_logs(included_paths):
  print("Initial include paths:")
  for path in included_paths:
    print(" + /%s" % (path))

  dt = svn_log_stream_get_dependencies(sys.stdin, included_paths)

  if dt.dependent_paths:
    found_new_deps = True
    print("Dependent include paths found:")
    for path in dt.dependent_paths:
      print(" + /%s" % (path))
    print("You need to also include them (or one of their parents).")
  else:
    found_new_deps = False
    print("No new dependencies found!")
    parents = {}
    for path in dt.include_paths:
      while 1:
        parent = os.path.dirname(path)
        if not parent:
          break
        parents[parent] = 1
        path = parent
    parents = parents.keys()
    if parents:
      print("You might still need to manually create parent directories " \
            "for the included paths before loading a filtered dump:")
      parents.sort(compare_paths)
      for parent in parents:
        print("   /%s" % (parent))

  return found_new_deps and EXIT_MOREDEPS or EXIT_SUCCESS

def usage_and_exit(errmsg=None):
  program = os.path.basename(sys.argv[0])
  stream = errmsg and sys.stderr or sys.stdout
  stream.write(__doc__.replace("{PROGRAM}", program))
  if errmsg:
    stream.write("\nERROR: %s\n" % (errmsg))
  sys.exit(errmsg and EXIT_FAILURE or EXIT_SUCCESS)

def main():
  config_dir = None
  targets_file = None

  try:
    opts, args = getopt.getopt(sys.argv[1:], "hv",
                               ["help", "verbose", "targets="])
  except getopt.GetoptError as e:
    usage_and_exit(str(e))

  for option, value in opts:
    if option in ['-h', '--help']:
      usage_and_exit()
    elif option in ['-v', '--verbose']:
      global verbosity
      verbosity = verbosity + 1
    elif option in ['--targets']:
      targets_file = value

  if len(args) == 0:
    usage_and_exit("Not enough arguments")

  if targets_file is None:
    targets = args[1:]
  else:
    targets = map(lambda x: x.rstrip('\n\r'),
                  open(targets_file, 'r').readlines())
  if not targets:
    usage_and_exit("No target paths specified")

  try:
    if args[0] == 'include':
      sys.exit(analyze_logs(map(sanitize_path, targets)))
    elif args[0] == 'exclude':
      usage_and_exit("Feature not implemented")
    else:
      usage_and_exit("Valid subcommands are 'include' and 'exclude'")
  except SystemExit:
    raise
  except (LogStreamError, EOFError) as e:
    log("ERROR: " + str(e), 0)
    sys.exit(EXIT_FAILURE)
  except:
    import traceback
    exc_type, exc, exc_tb = sys.exc_info()
    tb = traceback.format_exception(exc_type, exc, exc_tb)
    sys.stderr.write(''.join(tb))
    sys.exit(EXIT_FAILURE)


if __name__ == "__main__":
    main()
