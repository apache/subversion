#
# executable.py -- Utilities for dealing with external executables
#

import os, string

def exists(file):
  """Is this an executable file?"""
  return os.path.isfile(file) and os.access(file, os.X_OK)

def find(file, dirs=None):
  """Search for an executable in a given list of directories.
     If no directories are given, search according to the PATH
     environment variable."""
  if not dirs:
    dirs = string.split(os.environ["PATH"], os.pathsep)
  for path in dirs:
    if is_executable(os.path.join(path, file)):
      return os.path.join(path, file)
    elif is_executable(os.path.join(path, "%s.exe" % file)):
      return os.path.join(path, "%s.exe" % file)
  return None

def output(cmd, strip=None):
  """Run a command and collect all output"""
  stdin, stdout = os.popen4(cmd)
  assert(not stdin.close())
  output = stdout.read()
  assert(not stdout.close())
  if strip:
    return string.strip(output)
  else:
    return output

def run(cmd):
  """Run a command"""
  exit_code = os.system(cmd)
  assert(not exit_code)
