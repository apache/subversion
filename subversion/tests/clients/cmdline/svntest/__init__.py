__all__ = ["main", "tree", "actions"]

import sys
if sys.hexversion < 0x2000000:
  sys.stderr.write('[SKIPPED] at least Python 2.0 is required')

  # note: exiting is a bit harsh for a library module, but we really do
  # require Python 2.0. this package isn't going to work otherwise. and if
  # a user truly wants to use this package under 1.x somehow (or to clean
  # up in some way), then they can always trap the SystemExit exception

  # we're skipping this test, not failing, so exit with 0
  sys.exit(0)

# don't export this name
del sys

class Failure(Exception):
  'Base class for exceptions that indicate test failure'
  pass

class Skip(Exception):
  'Base class for exceptions that indicate test was skipped'
  pass

class SVNAnyOutput:
  """This class should be used to represent that you require output
  (whether stdout or stderr) from your test--regardless of what the
  output might be."""
  pass

import main, tree, actions, wc
