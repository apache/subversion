# A class that maps out all combinations of a given set
# This is not a test, this is included by tests,
# e.g. tree-conflicts-add-vs-add.py
#
# See an example below.
# To run the example, type
#   python permutations.py

import types

class Permutations:
  def __init__(self, *tokens):
    self.tokens = tokens
    self.skip = None
    self.reset()

  def next(self):
    if self.eol:
      return False

    self.row = [ self.tokens[n][self.counter[n]]
                   for n in range(len(self.tokens)) ]
    
    self.inc()
    
    if isinstance(self.skip, types.FunctionType) and self.skip(self.row):
      return self.next()
    return True

  # Add-with-carry
  def inc(self, digit=0):
    n = len(self.tokens) - digit - 1
    self.counter[n] += 1
    if self.counter[n] >= len(self.tokens[n]):
      self.counter[n] -= len(self.tokens[n])
      if digit + 1 < len(self.tokens):
        self.inc(digit + 1)
      else:
        self.eol = True

  def get(self):
    return self.row

  def reset(self):
    self.counter = [0] * len(self.tokens)
    self.row = None
    self.eol = False


if __name__ == '__main__':
  print "Example for using the Permutations class."
  class Foo:
    str = 'foo'

  class Bar:
    str = 'bar'

  x = Foo()
  y = Bar()

  p = Permutations(('A', 'B'), (1, 2, 3), ('-',), (x, y))
  print "All items:"
  while p.next():
    print p.row

  print "\nDefining some items to be skipped:"
  p.reset()
  p.skip = lambda row: row[0] == 'B' and row[3] == y
  while p.next():
    print p.row


