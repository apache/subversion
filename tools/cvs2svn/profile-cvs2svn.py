#!/usr/bin/env python
#
# Use this script to profile cvs2svn.py using Python's hotshot profiler.
#
# The profile data is stored in cvs2svn.hotshot.  To view the data using
# hotshot, run the following in python:
# 
#      import hotshot.stats
#      stats = hotshot.stats.load('cvs2svn.hotshot')
#      stats.strip_dirs()
#      stats.sort_stats('time', 'calls')
#      stats.print_stats(20)
# 
# It is also possible (and a lot better) to use kcachegrind to view the data.
# To do so, you must first convert the data to the cachegrind format using
# hotshot2cachegrind, which you can download from the following URL:
# 
# http://kcachegrind.sourceforge.net/cgi-bin/show.cgi/KcacheGrindContribPython
# 
# Convert the data using the following command:
# 
#      hotshot2cachegrind -o cachegrind.out cvs2svn.hotshot
# 
# Depending on the size of the repository, this can take a long time. When
# the conversion is done, simply open cachegrind.out in kcachegrind.

import cvs2svn, hotshot

prof = hotshot.Profile('cvs2svn.hotshot')
prof.runcall(cvs2svn.main)
prof.close()
