#!/usr/bin/env python
#
# Creates entries on the tigris file manager for a release, using the
# contents of:
#   md5sums
#
# Note: this file is not guaranteed to work with Python 3.  In fact, it
# probably doesn't.  Since the RM is really the only guy who uses and 
# develops this script, and he currently has Python 2.x, let's forgo worrying
# about Python 3 compatibility until it becomes a real problem.  It makes
# the script simpler to develop and maintain.  -hkw


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

import sys, cookielib, urllib2, urllib, re


def login(username, password, folderId):
    '''Login to tigris.org, using the provided username and password.
       Return the OpenDirector object for future use.'''
    cj = cookielib.CookieJar()
    opener = urllib2.build_opener(urllib2.HTTPCookieProcessor(cj))

    folderURL = 'http://subversion.tigris.org/servlets/ProjectDocumentList?folderID=%d' % folderId,
    params = {
        'detour' : folderURL,
        'loginID' : username,
        'password' : password,
      }
    request = urllib2.Request('http://www.tigris.org/servlets/TLogin',
                               urllib.urlencode(params))
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


def encode_multipart_form(params, boundary):
    "Encode the given params to be output as a multipart POST"
    lines = []
    for key in params:
        lines.append('--' + boundary)
        lines.append('Content-Disposition: form-data; name="%s"' % key)
        lines.append('')
        lines.append(params[key])
    return '\r\n'.join(lines)


def add_items(opener, folderId, release_name):
    "Add the 12(!) items for a release to the given folder"
    folder_add_url = 'http://subversion.tigris.org/servlets/ProjectDocumentAdd?folderID=%d&action=Add%%20document' % folderId
    boundary = '----------A_boundary_goes_here_$'

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
                'Button': 'Submit',
            }

            headers = {
                'Referer' : 'http://subversion.tigris.org/servlets/ProjectDocumentAdd?folderID=%s' % folderId,
                'Content-Type' : 'multipart/form-data; boundary=%s' % boundary
              }

            # Add file
            data = encode_multipart_form(params, boundary)
            request = urllib2.Request(folder_add_url, data, headers)
            request.add_header('Content-Length', len(data))
            opener.open(request)

            # Add signature
            filename = filename + '.asc'
            params['name'] = filename
            params['description'] = 'PGP signatures for %s' % desc
            params['url'] = 'http://subversion.tigris.org/downloads/%s' % \
                                                                      filename
            data = encode_multipart_form(params, boundary)
            request = urllib2.Request(folder_add_url, data, headers)
            request.add_header('Content-Length', len(data))
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
