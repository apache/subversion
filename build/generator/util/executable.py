#
# executable.py -- Utilities for dealing with external executables
#

import os
import subprocess

def exists(file):
  """Is this an executable file?"""
  return os.path.isfile(file) and os.access(file, os.X_OK)

def find(file, dirs=None):
  """Search for an executable in a given list of directories.
     If no directories are given, search according to the PATH
     environment variable."""
  if not dirs:
    dirs = os.environ["PATH"].split(os.pathsep)
  for path in dirs:
    if is_executable(os.path.join(path, file)):
      return os.path.join(path, file)
    elif is_executable(os.path.join(path, "%s.exe" % file)):
      return os.path.join(path, "%s.exe" % file)
  return None

def output(cmd, strip=None):
  """Run a command and collect all output"""
  # Check that cmd is in PATH (otherwise we'd get a generic OSError later)
  import distutils.spawn
  if type(cmd) == type(''):
    cmdname = cmd
  elif type(cmd) == type([]):
    cmdname = cmd[0]
  if distutils.spawn.find_executable(cmdname) is None:
    return None

  # Run it
  (output, empty_stderr) = subprocess.Popen(cmd, stdout=subprocess.PIPE, \
                             stderr=subprocess.STDOUT).communicate()
  if strip:
    return output.strip()
  else:
    return output

def run(cmd):
  """Run a command"""
  exit_code = os.system(cmd)
  assert(not exit_code)
