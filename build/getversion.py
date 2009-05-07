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

def svn_extractor(parser, include_file):
  '''Pull values from svn.version.h'''
  p.search('SVN_VER_MAJOR', 'major')
  p.search('SVN_VER_MINOR', 'minor')
  p.search('SVN_VER_PATCH', 'patch')

  try:
    r = p.parse(include_file)
  except IOError, e:
    usage_and_exit(str(e))
  sys.stdout.write("%d.%d.%d" % (r.major, r.minor, r.patch))


def sqlite_extractor(parser, include_file):
  '''Pull values from sqlite3.h'''
  p.search('SQLITE_VERSION_NUMBER', 'version')

  try:
    r = p.parse(include_file)
  except IOError, e:
    usage_and_exit(str(e))
  major = r.version / 1000000
  minor = (r.version - (major * 1000000)) / 1000
  micro = (r.version - (major * 1000000) - (minor * 1000))
  sys.stdout.write("%d.%d.%d" % (major, minor, micro))


extractors = {
  'SVN' : svn_extractor,
  'SQLITE' : sqlite_extractor,
  }

def usage_and_exit(msg):
  if msg:
    sys.stderr.write("%s\n\n" % msg)
  sys.stderr.write("usage: %s [SVN|SQLITE] [header_file]\n" % \
    os.path.basename(sys.argv[0]))
  sys.stderr.flush()
  sys.exit(1)


if __name__ == '__main__':
  if len(sys.argv) == 3:
    extractor = extractors[sys.argv[1]]
    include_file = sys.argv[2]
  else:
    usage_and_exit("Incorrect number of arguments")

  # Extract and print the version number
  p = Parser()
  extractor(p, include_file)
