#!/usr/bin/env python
#
# server-vsn.py: print a Subversion server's version number
#
# USAGE: server-vsn.py URL
#
# The URL can contain any path on the server, as we are simply looking
# for Apache's response to OPTIONS, and its Server: header.
#
# EXAMPLE:
#
#   $ ./server-vsn.py http://svn.collab.net/
#
# Python 1.5.2 or later is required.
#

import sys
import httplib
import urlparse
import string


def print_version(url):
  scheme, netloc, path, params, query, fragment = urlparse.urlparse(url)
  if scheme != 'http':
    print 'ERROR: this script only supports "http" URLs'
    sys.exit(1)
  conn = httplib.HTTP(netloc)
  conn.putrequest('OPTIONS', path)
  conn.putheader('Host', netloc)
  conn.endheaders()
  status, msg, headers = conn.getreply()
  if status != 200:
    print 'ERROR: bad status response: %s %s' % (status, msg)
    sys.exit(1)
  server = headers.getheader('Server')
  if not server:
    # a missing Server: header. Bad, bad server! Go sit in the corner!
    print 'WARNING: missing header'
  else:
    for part in string.split(server):
      if part[:4] == 'SVN/':
        print part[4:]
        break
    else:
      # the server might be configured to hide this information, or it
      # might not have mod_dav_svn loaded into it.
      print 'NOTICE: version unknown'


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print 'USAGE: %s URL' % sys.argv[0]
    sys.exit(1)
  print_version(sys.argv[1])
