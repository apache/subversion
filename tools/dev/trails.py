#!/usr/bin/env python

import re
import sys
import operator


_re_table_op = re.compile('\(([a-z]*), ([a-z]*)\)')

def parse_trails_log(infile):
  trails = []
  for line in infile.readlines():
    trail = _re_table_op.findall(line)
    trail.reverse()
    trails.append(trail)

  return trails


def output_summary(trails, outfile):
  ops = map(len, trails)
  ops.sort()

  total_trails = len(ops)
  total_ops = reduce(operator.add, ops)
  max_ops = ops[-1]
  median_ops = ops[total_trails / 2]
  average_ops = float(total_ops) / total_trails
  
  outfile.write('Total number of trails: %10i\n' % total_trails)
  outfile.write('Total number of ops:    %10i\n' % total_ops)
  outfile.write('max ops/trail:          %10i\n' % max_ops)
  outfile.write('median ops/trail:       %10i\n' % median_ops)
  outfile.write('average ops/trail:      %10.2f\n' % average_ops)
  outfile.write('\n')


# custom compare function
def freqtable_cmp((a, b), (c, d)):
  c = cmp(d, b)
  if not c:
    c = cmp(a, c)
  return c

def output_trail_length_frequencies(trails, outfile):
  ops = map(len, trails)

  frequencies = {}
  for rank in ops:
    frequencies[rank] = frequencies.get(rank, 0) + 1

  total_trails = len(ops)
 
  ranks = frequencies.items()
  ranks.sort(freqtable_cmp)

  outfile.write('ops/trail   frequency   percentage\n')
  for (r, f) in ranks:
    p = float(f) * 100 / total_trails
    outfile.write('%4i         %6i       %5.2f\n' % (r, f, p))
  outfile.write('\n')


def output_trail_frequencies(trails, outfile):

  total_trails = len(trails)

  # Since lists can't be keys, turn all trails into tuples
  ttrails = map(tuple, trails)
  
  freqs = {}
  for trail in ttrails:
    freqs[trail] = freqs.get(trail, 0) + 1

  ops = freqs.items()
  ops.sort(freqtable_cmp)

  outfile.write('frequency   percentage   ops/trail   trail\n')
  for (tt, f) in ops:
    p = float(f) * 100 / total_trails
    t = list(tt)
    outfile.write('%6i        %5.2f       %4i       ' % (f, p, len(t)))
    
    ### Output the trail itself, in it's own column
    line = str(t[0])
    for op in t[1:]:
      op_str = str(op)
      if len(line) + len(op_str) > 50:
        outfile.write('%s,\n' % line)
        outfile.write('                                     ')
        line = op_str
      else:
        line = line + ', ' + op_str
    outfile.write('%s\n' % line)
    
  outfile.write('\n')


if __name__ == '__main__':
  if len(sys.argv) > 2:
    sys.stderr.write('USAGE: %s [LOG-FILE]\n'
      % sys.argv[0])
    sys.exit(1)

  if len(sys.argv) == 1:
    infile = sys.stdin
  else:
    infile = open(sys.argv[1])

  trails = parse_trails_log(infile)

  output_summary(trails, sys.stdout)
  output_trail_length_frequencies(trails, sys.stdout)
  output_trail_frequencies(trails, sys.stdout)
