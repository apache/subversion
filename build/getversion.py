#!/usr/bin/env python
#
# getversion.py - Parse version numbers from C header files.
#

import os
import re
import sys

__all__ = ['Parser', 'Result']

class Result:
  pass

class Parser:
  def __init__(self):
    self.patterns = {}

  def search(self, define_name, value_name):
    'Add the name of a define to the list of search pattenrs.'
    self.patterns[define_name] = value_name

  def parse(self, file):
    'Parse the file, extracting defines into a Result object.'
    stream = open(file, 'rt')
    result = Result()
    regex = re.compile(r'^\s*#\s*define\s+(\w+)\s+(\d+)')
    for line in stream.readlines():
      match = regex.match(line)
      if match:
        try:
          name = self.patterns[match.group(1)]
        except:
          continue
        setattr(result, name, int(match.group(2)))
    stream.close()
    return result

def usage_and_exit(msg):
  if msg:
    sys.stderr.write("%s\n\n" % msg)
  sys.stderr.write("usage: %s SVN_VERSION.H\n" % \
    os.path.basename(sys.argv[0]))
  sys.stderr.flush()
  sys.exit(1)

if __name__ == '__main__':
  if len(sys.argv) == 2:
    include_file = sys.argv[1]
  else:
    usage_and_exit("Incorrect number of arguments")

  # Extract and print the version number
  p = Parser()
  p.search('SVN_VER_MAJOR', 'major')
  p.search('SVN_VER_MINOR', 'minor')
  p.search('SVN_VER_PATCH', 'patch')

  try:
    r = p.parse(include_file)
  except IOError, e:
    usage_and_exit(str(e))
  sys.stdout.write("%d.%d.%d" % (r.major, r.minor, r.patch))
