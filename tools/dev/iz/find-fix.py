#!/usr/bin/env python
#
# find-fix.py: produce a find/fix report for Subversion's IZ database
#
# This can be run over the data file found at:
#   http://subversion.tigris.org/iz-data/query-set-1.tsv
#
# also, see:
#   http://subversion.tigris.org/iz-data/README
#

import string
import time
import operator


_types = [
  'DEFECT',
  'TASK',
  'FEATURE',
  'ENHANCEMENT',
  'PATCH',
  ]

ONE_WEEK = 7 * 24 * 60 * 60

_milestone_filter = [
  'Post-1.0',
  'cvs2svn-1.0',
  'cvs2svn-opt',
  'inapplicable',
  ]


def summary(datafile, d_start, d_end):
  "Prints a summary of activity within a specified date range."

  data = load_data(datafile)

  found, fixed, inval, dup, other = extract(data, 1, d_start, d_end)

  t_found = t_fixed = t_inval = t_dup = t_other = t_rem = 0
  for t in _types:
    print '%12s: found=%3d  fixed=%3d  inval=%3d  dup=%3d  ' \
          'other=%3d  remain=%3d' \
          % (t, found[t], fixed[t], inval[t], dup[t], other[t],
             found[t] - (fixed[t] + inval[t] + dup[t] + other[t]))
    t_found = t_found + found[t]
    t_fixed = t_fixed + fixed[t]
    t_inval = t_inval + inval[t]
    t_dup   = t_dup   + dup[t]
    t_other = t_other + other[t]
    t_rem = t_rem + found[t] - (fixed[t] + inval[t] + dup[t] + other[t])

  print '-' * 77
  print '%12s: found=%3d  fixed=%3d  inval=%3d  dup=%3d  ' \
        'other=%3d  remain=%3d' \
        % ('totals', t_found, t_fixed, t_inval, t_dup, t_other, t_rem)


def plot(datafile, outbase):
  "Generates data files intended for use by gnuplot."

  data = load_data(datafile)

  t_min = 1L<<32
  for issue in data:
    if issue.created < t_min:
      t_min = issue.created

  # break the time up into a tuple, then back up to Sunday
  t_start = time.localtime(t_min)
  t_start = time.mktime((t_start[0], t_start[1], t_start[2] - t_start[6] - 1,
                         0, 0, 0, 0, 0, 0))

  plots = { }
  for t in _types:
    # for each issue type, we will record per-week stats, compute a moving
    # average of the find/fix delta, and track the number of open issues
    plots[t] = [ [ ], MovingAverage(), 0 ]

  week = 0
  for date in range(t_start, time.time(), ONE_WEEK):
    ### this is quite inefficient, as we could just sort by date, but
    ### I'm being lazy
    found, fixed = extract(data, None, date, date + ONE_WEEK - 1)

    for t in _types:
      per_week, avg, open_issues = plots[t]
      delta = found[t] - fixed[t]
      per_week.append((week, date,
                       found[t], -fixed[t], avg.add(delta), open_issues))
      plots[t][2] = open_issues + delta

    week = week + 1

  for t in _types:
    week_data = plots[t][0]
    write_file(week_data, outbase, t, 'found', 2)
    write_file(week_data, outbase, t, 'fixed', 3)
    write_file(week_data, outbase, t, 'avg', 4)
    write_file(week_data, outbase, t, 'open', 5)


def write_file(week_data, base, type, tag, idx):
  f = open('%s.%s.%s' % (base, tag, type), 'w')
  for info in week_data:
    f.write('%s %s # %s\n' % (info[0], info[idx], time.ctime(info[1])))


class MovingAverage:
  "Helper class to compute moving averages."
  def __init__(self, n=4):
    self.n = n
    self.data = [ 0 ] * n
  def add(self, value):
    self.data.pop(0)
    self.data.append(float(value) / self.n)
    return self.avg()
  def avg(self):
    return reduce(operator.add, self.data)


def extract(data, details, d_start, d_end):
  """Extract found/fixed counts for each issue type within the data range.

  If DETAILS is false, then return two dictionaries:

    found, fixed

  ...each mapping issue types to the number of issues of that type
  found or fixed respectively.

  If DETAILS is true, return five dictionaries:

    found, fixed, invalid, duplicate, other

  The first is still the found issues, but the other four break down
  the resolution into 'FIXED', 'INVALID', 'DUPLICATE', and a grab-bag
  category for 'WORKSFORME', 'LATER', 'REMIND', and 'WONTFIX'."""

  found = { }
  fixed = { }
  invalid = { }
  duplicate = { }
  other = { }  # "WORKSFORME", "LATER", "REMIND", and "WONTFIX"
  for t in _types:
    found[t] = fixed[t] = invalid[t] = duplicate[t] = other[t] = 0

  for issue in data:
    # filter out post-1.0 issues
    if issue.milestone in _milestone_filter:
      continue

    # record the found/fixed counts
    if d_start <= issue.created <= d_end:
      found[issue.type] = found[issue.type] + 1
    if d_start <= issue.resolved <= d_end:
      if details:
        if issue.resolution == "FIXED":
          fixed[issue.type] = fixed[issue.type] + 1
        elif issue.resolution == "INVALID":
          invalid[issue.type] = invalid[issue.type] + 1
        elif issue.resolution == "DUPLICATE":
          duplicate[issue.type] = duplicate[issue.type] + 1
        else:
          other[issue.type] = other[issue.type] + 1
      else:
        fixed[issue.type] = fixed[issue.type] + 1

  if details:
    return found, fixed, invalid, duplicate, other
  else:
    return found, fixed


def load_data(datafile):
  "Return a list of Issue objects for the specified data."
  return map(Issue, open(datafile).readlines())


class Issue:
  "Represents a single issue from the exported IssueZilla data."

  def __init__(self, line):
    row = string.split(string.strip(line), '\t')

    self.id = int(row[0])
    self.type = row[1]
    self.reporter = row[2]
    if row[3] == 'NULL':
      self.assigned = None
    else:
      self.assigned = row[3]
    self.milestone = row[4]
    self.created = parse_time(row[5])
    self.resolution = row[7]
    if not self.resolution:
      # If the resolution is empty, then force the resolved date to None.
      # When an issue is reopened, there will still be activity showing
      # a "RESOLVED", thus we get a resolved date. But we simply want to
      # ignore that date.
      self.resolved = None
    else:
      self.resolved = parse_time(row[6])
    self.summary = row[8]


def parse_time(t):
  "Convert an exported MySQL timestamp into seconds since the epoch."

  if t == 'NULL':
    return None
  try:
    return time.mktime(time.strptime(t, '%Y-%m-%d %H:%M:%S'))
  except ValueError:
    print 'ERROR: bad time value:', t
    raise


if __name__ == '__main__':
  import sys

  if len(sys.argv) == 3:
    plot(sys.argv[1], sys.argv[2])
  else:
    summary(sys.argv[1],
            time.mktime(time.strptime(sys.argv[2], '%Y-%m-%d')),
            time.mktime(time.strptime(sys.argv[3], '%Y-%m-%d')),
            )
