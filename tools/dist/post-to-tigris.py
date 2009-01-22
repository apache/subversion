#!/usr/bin/env python
#
# Creates entries on the tigris file manager for a release, using the
# contents of:
#   md5sums


usage = '''\
post-to-tigris.py <username> <password> <folderId> <release>
    username - Your tigris.org username
    password - Your tigris.org password
    folderId - the folderId of the place to post the files.  This can be
      gleaned from the URL in the file manager.  For example:
      http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=1079
      has a folderId of 1079
    release - the full name of the release, such as 1.5.0-beta1
'''

import sys, cookielib, re
try:
  # Python >=3.0
  from urllib.parse import urlencode as urllib_parse_urlencode
  from urllib.request import build_opener as urllib_request_build_opener
  from urllib.request import HTTPCookieProcessor as urllib_request_HTTPCookieProcessor
  from urllib.request import Request as urllib_request_Request
except ImportError:
  # Python <3.0
  from urllib import urlencode as urllib_parse_urlencode
  from urllib2 import build_opener as urllib_request_build_opener
  from urllib2 import HTTPCookieProcessor as urllib_request_HTTPCookieProcessor
  from urllib2 import Request as urllib_request_Request

def login(username, password, folderId):
    '''Login to tigris.org, using the provided username and password.
       Return the OpenDirector object for future use.'''
    cj = cookielib.CookieJar()
    opener = urllib_request_build_opener(urllib_request_HTTPCookieProcessor(cj))

    folderURL = 'http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=%d' % folderId,
    params = {
        'detour' : folderURL,
        'loginID' : username,
        'password' : password,
      }
    request = urllib_request_Request('http://www.tigris.org/servlets/TLogin',
                                     urllib_parse_urlencode(params))
    # We open the above request, grabbing the appropriate credentials for
    # future interactions.
    opener.open(request)

    return opener


def get_md5sums():
    "Open 'md5sums', and return a filename->checksum hash of the contents"
    f = open('md5sums')
    sums = {}

    for line in f:
        line_parts = line.split()
        sums[line_parts[1]] = line_parts[0]

    return sums


def add_items(opener, folderId, release_name):
    "Add the 12(!) items for a release to the given folder"
    folder_add_url = 'http://subversion.tigris.org/servlets/ProjectDocumentAdd?folderID=%d&action=Add%%20document' % folderId

    if re.match('^\d*\.\d*\.\d*$', release_name):
      status = 'Stable'
    else:
      status = 'Draft'

    md5sums = get_md5sums()

    for ext in ['.zip', '.tar.gz', '.tar.bz2']:
        for deps in ['', '-deps']:
            filename = 'subversion%s-%s%s' % (deps, release_name, ext)
            desc = 'Subversion %s' % release_name
            if deps:
                desc = desc + ' Dependencies'

            params = {
                'name' : filename,
                'status' : status,
                'description' : '%s (MD5: %s)' % (desc, md5sums[filename]),
                'type': 'link',
                'url': 'http://subversion.tigris.org/downloads/%s' % filename,
                'maxDepth': '',
            }

            # Add file
            request = urllib_request_Request(folder_add_url,
                                             urllib_parse_urlencode(params))
            opener.open(request)

            # Add signature
            filename = filename + '.asc'
            params['name'] = filename
            params['description'] = 'PGP signatures for %s' % desc
            params['url'] = 'http://subversion.tigris.org/downloads/%s' % \
                                                                      filename
            request = urllib_request_Request(folder_add_url,
                                             urllib_parse_urlencode(params))
            opener.open(request)


def main():
    if len(sys.argv) < 5:
        print(usage)
        sys.exit(-1)

    folderId = int(sys.argv[3])
    opener = login(sys.argv[1], sys.argv[2], folderId)
    add_items(opener, folderId, sys.argv[4])


if __name__ == '__main__':
    main()
