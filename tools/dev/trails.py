#!/usr/bin/env python

import re
import sys
import operator


_re_table_op = re.compile('\(([a-z]*), ([a-z]*)\)')

def parse_trails_log(infile):
  trails = []
  for line in infile.readlines():
    trails.append(_re_table_op.findall(line))

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


def output_frequencies(trails, outfile):
  ops = map(len, trails)

  frequencies = {}
  for rank in ops:
    frequencies[rank] = frequencies.get(rank, 0) + 1

  total_trails = len(ops)
 
  # custom compare function
  def rf_cmp((a, b), (c, d)):
    c = cmp(d, b)
    if not c:
      c = cmp(a, c)
    return c

  ranks = frequencies.items()
  ranks.sort(rf_cmp)

  outfile.write('ops/trail   frequency   percentage\n')
  for (r, f) in ranks:
    p = float(f) * 100 / total_trails
    outfile.write('%4i         %6i       %5.2f\n' % (r, f, p))


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
  output_frequencies(trails, sys.stdout)
