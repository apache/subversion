#!/usr/bin/env python
#
# getversion.py - Parse version numbers from C header files.
#

import re

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


if __name__ == '__main__':
  # Extract and print the version number
  p = Parser()
  p.search('SVN_VER_MAJOR', 'major')
  p.search('SVN_VER_MINOR', 'minor')
  p.search('SVN_VER_PATCH', 'patch')

  import os, sys
  r = p.parse(sys.argv[1])
  sys.stdout.write("%d.%d.%d" % (r.major, r.minor, r.patch))


### End of file.
