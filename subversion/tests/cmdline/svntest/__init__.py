__all__ = ["main", "tree", "actions"]

import sys
if sys.hexversion < 0x2040000:
  sys.stderr.write('[SKIPPED] at least Python 2.4 is required')

  # note: exiting is a bit harsh for a library module, but we really do
  # require Python 2.4. this package isn't going to work otherwise.

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

import main, tree, verify, actions, wc
