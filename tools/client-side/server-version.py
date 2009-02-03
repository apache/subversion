#!/usr/bin/env python
#
# server-version.py: print a Subversion server's version number
#
# USAGE: server-version.py URL
#
# The URL can contain any path on the server, as we are simply looking
# for Apache's response to OPTIONS, and its Server: header.
#
# EXAMPLE:
#
#   $ ./server-version.py http://svn.collab.net/
#                   or
#   $ ./server-version.py https://svn.collab.net/
#

import sys
import httplib
import urlparse


def print_version(url):
  scheme, netloc, path, params, query, fragment = urlparse.urlparse(url)
  if scheme == 'http':
    conn = httplib.HTTPConnection(netloc)
  elif scheme == 'https':
    conn = httplib.HTTPSConnection(netloc)
  else:
    print('ERROR: this script only supports "http" and "https" URLs')
    sys.exit(1)
  conn.putrequest('OPTIONS', path)
  conn.putheader('Host', netloc)
  conn.endheaders()
  resp = conn.getresponse()
  status, msg, server = (resp.status, resp.msg, resp.getheader('Server'))
  conn.close()

  # Handle "OK" and Handle redirect requests, if requested resource
  # resides temporarily under a different URL
  if status != 200 and status != 302:
    print('ERROR: bad status response: %s %s' % (status, msg))
    sys.exit(1)
  if not server:
    # a missing Server: header. Bad, bad server! Go sit in the corner!
    print('WARNING: missing header')
  else:
    for part in server.split(' '):
      if part[:4] == 'SVN/':
        print(part[4:])
        break
    else:
      # the server might be configured to hide this information, or it
      # might not have mod_dav_svn loaded into it.
      print('NOTICE: version unknown')


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print('USAGE: %s URL' % sys.argv[0])
    sys.exit(1)
  print_version(sys.argv[1])
