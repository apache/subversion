#!/usr/bin/env python
#
# graph-svn-dav.py by Brian W. Fitzpatrick <fitz@red-bean.com>
#
# This was originally a quick hack to make a pretty picture of svn DAV servers.
#
# I've dropped it in Subversion's repository at the request of Karl Fogel.
#
# Be warned this this script has many dependencies that don't ship with Python.

import sys
import fileinput
import datetime
import time
from datetime import datetime
from matplotlib.dates import date2num
import matplotlib
matplotlib.use('Agg')
from matplotlib.pylab import *

OUTPUT_FILE = '../../www/images/svn-dav-securityspace-survey.png'

STATS = """1/1/2003       70
2/1/2003                 158
3/1/2003                 222
4/1/2003                 250
5/1/2003                 308
6/1/2003                 369
7/1/2003                 448
8/1/2003                 522
9/1/2003                 665
10/1/2003                782
11/1/2003                969
12/1/2003               1009
1/1/2004                1162
2/1/2004                1307
3/1/2004                1424
4/1/2004                1792
5/1/2004                2113
6/1/2004                2502
7/1/2004                2941
8/1/2004                3863
9/1/2004                4174
10/1/2004               4187
11/1/2004               4783
12/1/2004               4995
1/1/2005                5565
2/1/2005                6505
3/1/2005                7897
4/1/2005                8751
5/1/2005                9793
6/1/2005               11534
7/1/2005               12808
8/1/2005               13545
9/1/2005               15233
10/1/2005              17588
11/1/2005              18893
12/1/2005              20278
1/1/2006               21084
2/1/2006               23861"""


def get_date(raw_date):
  date = time.strptime(raw_date, "%m/%d/%Y")
  date = datetime(date[0], date[1], date[2], date[3])
  return date


def get_ordinal_date(date):
  # This is the only way I can get matplotlib to do the dates right.
  return date2num(date)


def parse_stats(str):
  dates = []
  counts = []

  lines = str.split('\n')
  for line in lines:
    key, val = line.split(' ', 1)

    dates.append(int(get_ordinal_date(get_date(key))))
    counts.append(int(val.lstrip()))
  return dates, counts


def draw_graph(dates, counts):
  ###########################################################
  # Drawing takes place here.
  ax = subplot(111)
  ax.xaxis.set_major_formatter( DateFormatter('%b,%y') )
  ax.yaxis.set_major_formatter( FormatStrFormatter('%d') )

  line = bar(dates, counts,  color='r', width=24)

  ylabel('Total # of Public DAV Servers')

  lastdate = datetime.fromordinal(dates[len(dates) - 1]).strftime("%B %Y")
  xlabel("Data as of " + lastdate)
  title('Security Space Survey of Public Subversion DAV Servers')
  # End drawing
  ###########################################################
  png = open(OUTPUT_FILE, 'w')
  savefig(png)
  png.close()
  close()


if __name__ == '__main__':
  dates, counts = parse_stats(STATS);
  draw_graph(dates, counts)
  print "Don't forget to update ../../www/svn-dav-securityspace-survey.html!"
