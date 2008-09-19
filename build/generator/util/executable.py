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
  try:
    # Python >=2.4
    import subprocess
    (output, empty_stderr) = subprocess.Popen(cmd, stdout=subprocess.PIPE, \
                               stderr=subprocess.STDOUT).communicate()
  except ImportError:
    # Python <2.4
    (stdin, stdout) = os.popen4(cmd)
    assert(not stdin.close())
    output = stdout.read()
    assert(not stdout.close())
  except OSError:
    # Command probably not found
    output = ""
  if strip:
    return string.strip(output)
  else:
    return output

def run(cmd):
  """Run a command"""
  exit_code = os.system(cmd)
  assert(not exit_code)
