# -*- Mode: python -*-
#
# Copyright (C) 2000-2002 The ViewCVS Group. All Rights Reserved.
#
# By using this file, you agree to the terms and conditions set forth in
# the LICENSE.html file which can be found at the top level of the ViewCVS
# distribution or at http://viewcvs.sourceforge.net/license-1.html.
#
# Contact information:
#   Greg Stein, PO Box 760, Palo Alto, CA, 94302
#   gstein@lyra.org, http://viewcvs.sourceforge.net/
#
# -----------------------------------------------------------------------
#
# compat.py: compatibility functions for operation across Python 1.5.x to 2.2.x
#
# -----------------------------------------------------------------------
#

import urllib
import string
import time
import calendar
import re
import os


#
# urllib.urlencode() is new to Python 1.5.2
#
try:
  urlencode = urllib.urlencode
except AttributeError:
  def urlencode(dict):
    "Encode a dictionary as application/x-url-form-encoded."
    if not dict:
      return ''
    quote = urllib.quote_plus
    keyvalue = [ ]
    for key, value in dict.items():
      keyvalue.append(quote(key) + '=' + quote(str(value)))
    return string.join(keyvalue, '&')

#
# time.strptime() is new to Python 1.5.2
#
if hasattr(time, 'strptime'):
  def cvs_strptime(timestr):
    'Parse a CVS-style date/time value.'
    return time.strptime(timestr, '%Y/%m/%d %H:%M:%S')
else:
  _re_rev_date = re.compile('([0-9]{4})/([0-9][0-9])/([0-9][0-9]) '
                            '([0-9][0-9]):([0-9][0-9]):([0-9][0-9])')
  def cvs_strptime(timestr):
    'Parse a CVS-style date/time value.'
    matches = _re_rev_date.match(timestr).groups()
    return tuple(map(int, matches)) + (0, 1, 0)

#
# os.makedirs() is new to Python 1.5.2
#
try:
  makedirs = os.makedirs
except AttributeError:
  def makedirs(path, mode=0777):
    head, tail = os.path.split(path)
    if head and tail and not os.path.exists(head):
      makedirs(head, mode)
    os.mkdir(path, mode)

# 
# calendar.timegm() is new to Python 2.x and 
# calendar.leapdays() was wrong in Python 1.5.2
#
try:
  timegm = calendar.timegm 
except AttributeError:
  def leapdays(year1, year2):
    """Return number of leap years in range [year1, year2).
       Assume year1 <= year2."""
    year1 = year1 - 1
    year2 = year2 - 1
    return (year2/4 - year1/4) - (year2/100 - 
                                  year1/100) + (year2/400 - year1/400)

  EPOCH = 1970
  def timegm(tuple):
    """Unrelated but handy function to calculate Unix timestamp from GMT."""
    year, month, day, hour, minute, second = tuple[:6]
    # assert year >= EPOCH
    # assert 1 <= month <= 12
    days = 365*(year-EPOCH) + leapdays(EPOCH, year)
    for i in range(1, month):
      days = days + calendar.mdays[i]
    if month > 2 and calendar.isleap(year):
      days = days + 1
    days = days + day - 1
    hours = days*24 + hour
    minutes = hours*60 + minute
    seconds = minutes*60 + second
    return seconds


# 
# the following stuff is *ONLY* needed for standalone.py.
# For that reason I've encapsulated it into a function.
#

def for_standalone():
  import SocketServer
  if not hasattr(SocketServer.TCPServer, "close_request"):
    #
    # method close_request() was missing until Python 2.1
    #
    class TCPServer(SocketServer.TCPServer):
      def process_request(self, request, client_address):
        """Call finish_request.

        Overridden by ForkingMixIn and ThreadingMixIn.

        """
        self.finish_request(request, client_address)
        self.close_request(request)

      def close_request(self, request):
        """Called to clean up an individual request."""
        request.close()

    SocketServer.TCPServer = TCPServer
