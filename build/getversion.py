#
# getversion.py - Parse version numbers from C header files.
#


import re
from xreadlines import xreadlines

__all__ = ['Parser', 'Result']

class Result: pass

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
    for line in stream.xreadlines():
      match = regex.match(line)
      if match:
        try: name = self.patterns[match.group(1)]
        except: continue
        setattr(result, name, int(match.group(2)))
    return result


if __name__ == '__main__':
  # Example: Get the version number from svn_version.h
  p = Parser()
  p.search('SVN_VER_MAJOR', 'major')
  p.search('SVN_VER_MINOR', 'minor')
  p.search('SVN_VER_MICRO', 'patch')
  p.search('SVN_VER_LIBRARY', 'libver')

  import os, sys
  r = p.parse(os.path.join(os.path.dirname(sys.argv[0]),
                           '../subversion/include/svn_version.h'))
  print "Subversion %d.%d.%d" % (r.major, r.minor, r.patch)
  print "Library version %d" % r.libver


### End of file.
# local variables:
# eval: (load-file "../../tools/dev/svn-dev.el")
# end:
