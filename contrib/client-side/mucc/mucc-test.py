#!/usr/bin/env python

import sys
import os
import re
import shutil

# calculate the absolute directory in which this test script lives
this_dir = os.path.dirname(os.path.abspath(sys.argv[0]))

# add the Subversion Python test suite libraries to the path, and import
sys.path.insert(0, '%s/../../../subversion/tests/cmdline' % (this_dir))
import svntest

# where lives mucc?
mucc_binary = os.path.abspath('%s/mucc' % (this_dir))

# override some svntest binary locations
svntest.main.svn_binary = \
   os.path.abspath('%s/../../../subversion/svn/svn' % (this_dir))
svntest.main.svnlook_binary = \
   os.path.abspath('%s/../../../subversion/svnlook/svnlook' % (this_dir))
svntest.main.svnadmin_binary = \
   os.path.abspath('%s/../../../subversion/svnadmin/svnadmin' % (this_dir))

# where lives the test repository?
repos_path = os.path.abspath(('%s/mucc-test-repos' % (this_dir)))
repos_url = 'file://' + repos_path


def die(msg):
  """Write MSG (formatted as a failure) to stderr, and exit with a
  non-zero errorcode."""
  
  sys.stderr.write("FAIL: " + msg + "\n")
  sys.exit(1)


_mucc_re = re.compile('^(r[0-9]+) committed by muccuser at (.*)$')
_log_re = re.compile('^   ([ADRM] /[^\(]+($| \(from .*:[0-9]+\)$))')

def run_mucc(expected_path_changes, *varargs):
  """Run mucc with the list of MUCC_ARGS arguments.  Verify that its
  run results in a new commit with 'svn log -rHEAD' changed paths that
  match the list of EXPECTED_PATH_CHANGES."""

  # First, run mucc.
  outlines, errlines = svntest.main.run_command(mucc_binary, 1, 0,
                                                '-U', repos_url,
                                                '-u', 'muccuser',
                                                *varargs)
  if errlines:
    raise svntest.main.SVNCommitFailure(str(errlines))
  if len(outlines) != 1 or not _mucc_re.match(outlines[0]):
    raise svntest.main.SVNLineUnequal(str(outlines))
  
  # Now, run 'svn log -vq -rHEAD'
  changed_paths = []
  outlines, errlines = svntest.main.run_svn(None, 'log', '-vqrHEAD', repos_url)
  if errlines:
    raise svntest.Failure("Unable to verify commit with 'svn log': %s"
                          % (str(errlines)))
  for line in outlines:
    match = _log_re.match(line)
    if match:
      changed_paths.append(match.group(1).rstrip('\n\r'))

  expected_path_changes.sort()
  changed_paths.sort()
  if changed_paths != expected_path_changes:
    raise svntest.Failure("Logged path changes differ from expectations\n"
                          "   expected: %s\n"
                          "     actual: %s" % (str(expected_path_changes),
                                               str(changed_paths)))


def main():
  """Test mucc."""

  # revision 1
  run_mucc(['A /foo'
            ], # ---------
           'mkdir', 'foo')

  # revision 2
  run_mucc(['A /z.c',
            ], # ---------
           'put', '/dev/null', 'z.c')

  # revision 3
  run_mucc(['A /foo/z.c (from /z.c:2)',
            'A /foo/bar (from /foo:2)',
            ], # ---------
           'cp', '2', 'z.c', 'foo/z.c',
           'cp', '2', 'foo', 'foo/bar')

  # revision 4
  run_mucc(['A /zig (from /foo:3)',
            'D /zig/bar',
            'D /foo',
            'A /zig/zag (from /foo:3)',
            ], # ---------
           'cp', '3', 'foo', 'zig', 
           'rm',             'zig/bar',
           'mv',      'foo', 'zig/zag')

  # revision 5
  run_mucc(['D /z.c',
            'A /zig/zag/bar/y.c (from /z.c:4)',
            'A /zig/zag/bar/x.c (from /z.c:2)',
            ], # ---------
           'mv',      'z.c', 'zig/zag/bar/y.c',
           'cp', '2', 'z.c', 'zig/zag/bar/x.c')

  # revision 6
  run_mucc(['D /zig/zag/bar/y.c',
            'A /zig/zag/bar/y y.c (from /zig/zag/bar/y.c:5)',
            'A /zig/zag/bar/y%20y.c (from /zig/zag/bar/y.c:5)',
            ], # ---------
           'mv',         'zig/zag/bar/y.c', 'zig/zag/bar/y%20y.c',
           'cp', 'HEAD', 'zig/zag/bar/y.c', 'zig/zag/bar/y%2520y.c')

  # revision 7
  run_mucc(['D /zig/zag/bar/y y.c',
            'A /zig/zag/bar/z z1.c (from /zig/zag/bar/y y.c:6)',
            'A /zig/zag/bar/z%20z.c (from /zig/zag/bar/y%20y.c:6)',
            'A /zig/zag/bar/z z2.c (from /zig/zag/bar/y y.c:6)',
            ], #---------
           'mv',         'zig/zag/bar/y%20y.c',   'zig/zag/bar/z z1.c', 
           'cp', 'HEAD', 'zig/zag/bar/y%2520y.c', 'zig/zag/bar/z%2520z.c', 
           'cp', 'HEAD', 'zig/zag/bar/y y.c',     'zig/zag/bar/z z2.c')

  # revision 8
  run_mucc(['D /zig/zag',
            'A /zig/foo (from /zig/zag:7)',
            'D /zig/foo/bar/z%20z.c',
            'D /zig/foo/bar/z z2.c',
            'R /zig/foo/bar/z z1.c (from /zig/zag/bar/x.c:5)',
            ], #---------
           'mv',      'zig/zag',         'zig/foo', 
           'rm',                         'zig/foo/bar/z z1.c', 
           'rm',                         'zig/foo/bar/z%20z2.c', 
           'rm',                         'zig/foo/bar/z%2520z.c', 
           'cp', '5', 'zig/zag/bar/x.c', 'zig/foo/bar/z%20z1.c')

  # revision 9
  run_mucc(['R /zig/foo/bar (from /zig/z.c:8)',
            ], #---------
           'rm',                 'zig/foo/bar', 
           'cp', '8', 'zig/z.c', 'zig/foo/bar')

  # revision 10
  run_mucc(['R /zig/foo/bar (from /zig/foo/bar:8)',
            'D /zig/foo/bar/z z1.c',
            ], #---------
           'rm',                     'zig/foo/bar', 
           'cp', '8', 'zig/foo/bar', 'zig/foo/bar', 
           'rm',                     'zig/foo/bar/z%20z1.c')

  # revision 11
  run_mucc(['R /zig/foo (from /zig/foo/bar:10)',
            ], #---------
           'rm',                        'zig/foo', 
           'cp', 'head', 'zig/foo/bar', 'zig/foo')

  # revision 12
  run_mucc(['D /zig',
            'A /foo (from /foo:3)',
            'A /foo/foo (from /foo:3)',
            'A /foo/foo/foo (from /foo:3)',
            'D /foo/foo/bar',
            'R /foo/foo/foo/bar (from /foo:3)',
            ], #---------
           'rm',             'zig', 
           'cp', '3', 'foo', 'foo',
           'cp', '3', 'foo', 'foo/foo', 
           'cp', '3', 'foo', 'foo/foo/foo', 
           'rm',             'foo/foo/bar',
           'rm',             'foo/foo/foo/bar', 
           'cp', '3', 'foo', 'foo/foo/foo/bar')

  # revision 13
  run_mucc(['A /boozle (from /foo:3)',
            'A /boozle/buz',
            'A /boozle/buz/nuz',
            ], #---------
           'cp',    '3', 'foo', 'boozle',
           'mkdir',             'boozle/buz',  
           'mkdir',             'boozle/buz/nuz')

  # revision 14
  run_mucc(['A /boozle/buz/mucc-test.py',
            'A /boozle/guz (from /boozle/buz:13)',
            'A /boozle/guz/mucc-test.py',
            ], #---------
           'put',      '/dev/null',  'boozle/buz/mucc-test.py',
           'cp', '13', 'boozle/buz', 'boozle/guz',
           'put',      '/dev/null',  'boozle/guz/mucc-test.py')

  # revision 15
  run_mucc(['M /boozle/buz/mucc-test.py',
            'R /boozle/guz/mucc-test.py',
            ], #---------
           'put', sys.argv[0], 'boozle/buz/mucc-test.py',
           'rm',               'boozle/guz/mucc-test.py',
           'put', sys.argv[0], 'boozle/guz/mucc-test.py')

if __name__ == "__main__":
  try:
    # remove any previously existing repository, then create a new one
    if os.path.exists(repos_path):
      shutil.rmtree(repos_path)
    outlines, errlines = svntest.main.run_svnadmin('create', '--fs-type',
                                                   'fsfs', repos_path)
    if errlines:
      raise svntest.main.SVNRepositoryCreateFailure(repos_path)
    main()
  except SystemExit, e:
    raise
  except svntest.main.SVNCommitFailure, e:
    die("Error committing via mucc: %s" % (str(e)))
  except svntest.main.SVNLineUnequal, e:
    die("Unexpected mucc output line: %s" % (str(e)))
  except svntest.main.SVNRepositoryCreateFailure, e:
    die("Error creating test repository: %s" % (str(e)))
  except svntest.Failure, e:
    die("Test failed: %s" % (str(e)))
  except Exception, e:
    die("Something bad happened: %s" % (str(e)))

  # cleanup the repository on a successful run
  try:
    if os.path.exists(repos_path):
      shutil.rmtree(repos_path)
  except:
    pass
  print "SUCCESS!"
