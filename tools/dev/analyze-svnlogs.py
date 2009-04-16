#!/usr/bin/python
#
# Generate a report of each area each committer has touched over all time.
#
# $ svn log -v ^/ > svnlogdata
# $ ./analyze-svnlogs.py < svnlogdata > report.txt
#
# NOTE: ./logdata.py is written with a cached version of the data extracted
#       from 'svnlogdata'. That data can be analyzed in many ways, beyond
#       what this script is reporting.
#

import sys
import re


RE_LOG_HEADER = re.compile('^(r[0-9]+) '
                           '\| ([^|]+) '
                           '\| ([^|]+) '
                           '\| ([0-9]+) line')
RE_PATH = re.compile(r'   [MARD] (.*?)( \(from .*\))?$')
SEPARATOR = '-' * 72


def parse_one_commit(logfile):
  line = logfile.readline().strip()
  if line != SEPARATOR:
    raise ParseError('missing separator: %s' % line)

  line = logfile.readline()
  if not line:
    # end of file!
    return None, None

  m = RE_LOG_HEADER.match(line)
  if not m:
    raise ParseError('could not match log header')
  revision = m.group(1)
  author = m.group(2)
  num_lines = int(m.group(4))
  paths = set()

  # skip "Changed paths:"
  line = logfile.readline().strip()
  if not line:
    # there were no paths. just a blank before the log message. continue on.
    sys.stderr.write('Funny revision: %s\n' % revision)
  else:
    if not line.startswith('Changed'):
      raise ParseError('log not run with -v. paths missing in %s' % revision)

    # gather all the affected paths
    while 1:
      line = logfile.readline().rstrip()
      if not line:
        # just hit end of the changed paths
        break
      m = RE_PATH.match(line)
      if not m:
        raise ParseError('bad path in %s: %s' % (revision, line))
      paths.add(m.group(1))

  # suck up the log message
  for i in range(num_lines):
    logfile.readline()

  return author, paths


def parse_file(logfile):
  authors = { }

  while True:
    author, paths = parse_one_commit(logfile)
    if author is None:
      return authors

    if author in authors:
      authors[author] = authors[author].union(paths)
    else:
      authors[author] = paths


def write_logdata(authors):
  out = open('logdata.py', 'w')
  out.write('authors = {\n')
  for author, paths in authors.items():
    out.write("  '%s': set([\n" % author)
    for path in paths:
      out.write('    %s,\n' % repr(path))
    out.write('  ]),\n')
  out.write('}\n')


def print_report(authors):
  for author, paths in sorted(authors.items()):
    topdirs = { }
    for path in paths:
      key = tuple(path.split('/', 3)[1:3])
      if key in topdirs:
        topdirs[key] += 1
      else:
        topdirs[key] = 1

    print author
    tags = [ ]
    branches = [ ]
    for topdir in sorted(topdirs):
      if len(topdir) == 1:
        assert topdirs[topdir] == 1
        print '  %s  (ROOT)' % topdir[0]
      else:
        if topdir[0] == 'tags':
          tags.append(topdir[1])
        elif topdir[0] == 'branches':
          branches.append(topdir[1])
        else:
          print '  %s (%d items)' % ('/'.join(topdir), topdirs[topdir])
    if tags:
      print '  TAGS:', ', '.join(tags)
    if branches:
      print '  BRANCHES:', ', '.join(branches)

    print


def run(logfile):
  try:
    import logdata
    authors = logdata.authors
  except ImportError:
    authors = parse_file(logfile)
    write_logdata(authors)

  print_report(authors)


class ParseError(Exception):
  pass


if __name__ == '__main__':
  if len(sys.argv) > 1:
    logfile = open(sys.argv[1])
  else:
    logfile = sys.stdin
  run(logfile)
