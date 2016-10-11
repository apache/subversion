#!/usr/bin/env python
#
#  mod_dav_svn_tests.py:  testing mod_dav_svn
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import os, logging, base64, functools

try:
  # Python <3.0
  import httplib
except ImportError:
  # Python >=3.0
  import http.client as httplib

logger = logging.getLogger()

# Our testing module
import svntest

# (abbreviation)
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco

######################################################################
# Helper routines

def compare(lhs, rhs):
  """Implements cmp() for Python 2 and 3 alike"""
  if lhs == None:
    if rhs == None:
      return 0
    else:
      return -1
  else:
    if rhs == None:
      return 1
    else:
      return (lhs > rhs) - (lhs < rhs)

def compare_dict(lhs, rhs):
  """Implements dictionary comparison for Python 2 and 3 alike"""
  lhs_sorted = sorted(lhs, key=lambda x:sorted(x.keys()))
  rhs_sorted = sorted(rhs, key=lambda x:sorted(x.keys()))
  return (lhs_sorted > rhs_sorted) - (lhs_sorted < rhs_sorted)

def compare_xml_elem(a, b):
  """Recursively compare two xml.etree.ElementTree.Element objects.
  Return a 3-tuple made out of (cmp, elem_a, elem_b), where cmp is
  the integer result of the comparison (negative, zero or positive),
  and elem_a and elem_b point to mismatching elements.  Iff cmp is
  zero, elem_a and elem_b are None. """

  # Compare tags, attributes, inner text, tail attribute and the
  # number of child elements.
  res = compare(a.tag, b.tag)
  if res != 0:
    return res, a, b
  # Don't care about the order of the attributes.
  res = compare_dict(a.attrib, b.attrib)
  if res != 0:
    return res, a, b
  res = compare(a.text, b.text)
  if res != 0:
    return res, a, b
  res = compare(a.tail, b.tail)
  if res != 0:
    return res, a, b
  res = compare(len(a), len(b))
  if res != 0:
    return res, a, b

  # Prior to recursing, order child elements using the same comparator.
  # Right now we don't care about the order of the elements.  For instance,
  # <D:response>'s in PROPFIND *need* to be compared without a particular
  # order, since the server returns them in an unstable order of the hash
  # iteration.
  def sortcmp(x, y):
    return compare_xml_elem(x, y)[0]
 
  a_children = sorted(list(a), key=functools.cmp_to_key(sortcmp))
  b_children = sorted(list(b), key=functools.cmp_to_key(sortcmp))

  for a_child, b_child in zip(a_children, b_children):
    res = compare_xml_elem(a_child, b_child)
    if res[0] != 0:
      return res

  # Elements are equal.
  return 0, None, None

def verify_xml_response(expected_xml, actual_xml):
  """Parse and compare two XML responses, raise svntest.Failure
  in case EXPECTED_XML doesn't match ACTUAL_XML. """

  import xml.etree.ElementTree as ET

  expected_root = ET.fromstring(expected_xml)
  actual_root = ET.fromstring(actual_xml)
  res, expected_elem, actual_elem = compare_xml_elem(expected_root,
                                                     actual_root)
  if res != 0:
    # The actual response doesn't match our expectations; dump it for
    # debugging purposes, and highlight the mismatching xml element.
    logger.warn("Response:\n%s" % actual_xml)
    raise svntest.Failure("Unexpected response part\n"
                          "  Expected: '%s'\n    Actual: '%s'\n"
                          % (ET.tostring(expected_elem),
                             ET.tostring(actual_elem)))

######################################################################
# Tests

@SkipUnless(svntest.main.is_ra_type_dav)
def cache_control_header(sbox):
  "verify 'Cache-Control' headers on responses"

  sbox.build(create_wc=False, read_only=True)

  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
  }

  h = svntest.main.create_http_connection(sbox.repo_url)

  # GET /repos/iota
  # Response depends on the youngest revision in the repository, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/iota', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/A/
  # Response depends on the youngest revision in the repository, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/A/', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/A/?p=1
  # Response for a pegged directory is a subject for authz filtering, and
  # can't be cached; expect to see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/A/?p=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?r=1
  # Response for a file URL with ?r=WORKINGREV is mutable, because the
  # line of history for this file can be replaced in the future (hence,
  # the same request will start producing another response).  Expect to
  # see Cache-Control: max-age=0.
  h.request('GET', sbox.repo_url + '/iota?r=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=0',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?p=1
  # Response for a pegged file is immutable; expect to see Cache-Control
  # with non-zero max-age.
  h.request('GET', sbox.repo_url + '/iota?p=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()

  # GET /repos/iota?p=1&r=1
  # Response for a file URL with both ?p=PEG_REV and ?r=WORKINGREV is
  # immutable; expect to see Cache-Control with non-zero max-age.
  h.request('GET', sbox.repo_url + '/iota?p=1&r=1', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()


  # GET /repos/!svn/rvr/1/iota
  # Response is immutable; expect to see Cache-Control with non-zero max-age.
  h.request('GET', sbox.repo_url + '/!svn/rvr/1/iota', None, headers)
  r = h.getresponse()
  if r.status != httplib.OK:
    raise svntest.Failure('Request failed: %d %s' % (r.status, r.reason))
  svntest.verify.compare_and_display_lines(None, 'Cache-Control',
                                           'max-age=604800',
                                           r.getheader('Cache-Control'))
  r.read()


@SkipUnless(svntest.main.is_ra_type_dav)
def simple_propfind(sbox):
  "verify simple PROPFIND responses"

  sbox.build(create_wc=False, read_only=True)
  repo_uripath = '/' + svntest.wc.svn_uri_quote(
    svntest.main.pristine_greek_repos_dir.replace(os.path.sep, '/'))
  h = svntest.main.create_http_connection(sbox.repo_url)

  # PROPFIND /repos/!svn/rvr/1, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
    '<prop><resourcetype xmlns="DAV:"/></prop>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype><D:collection/></lp1:resourcetype>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)

  # PROPFIND /repos/!svn/rvr/1, Depth = 1
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '1',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<prop><resourcetype xmlns="DAV:"/></prop>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype><D:collection/></lp1:resourcetype>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/A/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype><D:collection/></lp1:resourcetype>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/iota</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype/>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)

  # PROPFIND /repos/!svn/rvr/1/A/B/F, Depth = 1
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '1',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<prop><resourcetype xmlns="DAV:"/></prop>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1/A/B/F', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/A/B/F/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype><D:collection/></lp1:resourcetype>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)

  # PROPFIND /repos/!svn/rvr/1/iota, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<prop><resourcetype xmlns="DAV:"/></prop>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1/iota', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:lp1="DAV:" '
        'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/iota</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype/>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)


@SkipUnless(svntest.main.is_ra_type_dav)
def propfind_multiple_props(sbox):
  "verify multi-prop PROPFIND response"

  sbox.build(create_wc=False, read_only=True)
  repo_uripath = '/' + svntest.wc.svn_uri_quote(
    svntest.main.pristine_greek_repos_dir.replace(os.path.sep, '/'))
  h = svntest.main.create_http_connection(sbox.repo_url)

  # PROPFIND /repos/!svn/rvr/1/iota, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8" ?>\n'
    '<D:propfind xmlns:D="DAV:">\n'
      '<D:prop xmlns:S="http://subversion.tigris.org/xmlns/dav/">\n'
         '<D:resourcetype/>\n'
         '<S:md5-checksum/>\n'
      '</D:prop>\n'
    '</D:propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1/iota', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" '
                   'xmlns:ns1="http://subversion.tigris.org/xmlns/dav/" '
                   'xmlns:ns0="DAV:">\n'
      '<D:response xmlns:lp1="DAV:" '
                  'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
      '<D:href>' + repo_uripath + '/!svn/rvr/1/iota</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype/>\n'
            '<lp2:md5-checksum>'
              '2d18c5e57e84c5b8a5e9a6e13fa394dc'
            '</lp2:md5-checksum>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)


@SkipUnless(svntest.main.is_ra_type_dav)
def propfind_404(sbox):
  "verify PROPFIND for non-existing property"

  sbox.build(create_wc=False, read_only=True)
  repo_uripath = '/' + svntest.wc.svn_uri_quote(
    svntest.main.pristine_greek_repos_dir.replace(os.path.sep, '/'))
  h = svntest.main.create_http_connection(sbox.repo_url)

  # PROPFIND /repos/!svn/rvr/1, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<prop><nonexistingprop xmlns="DAV:"/></prop>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:g0="DAV:">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<g0:nonexistingprop/>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 404 Not Found</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)


@SkipUnless(svntest.main.is_ra_type_dav)
def propfind_allprop(sbox):
  "verify allprop PROPFIND response"

  sbox.build()
  repo_uripath = '/' + svntest.wc.svn_uri_quote(
    sbox.repo_dir.replace(os.path.sep, '/'))
  svntest.actions.enable_revprop_changes(sbox.repo_dir)
  # Ensure stable date and uuid
  svntest.main.run_svnadmin('setuuid', sbox.repo_dir,
                            'd7130b12-92f6-45c9-9217-b9f0472c3fab')
  svntest.actions.run_and_verify_svn(None, [],
                                     'propset', '--revprop', '-r', '1',
                                     'svn:date', '2015-01-01T00:00:00.0Z',
                                     sbox.wc_dir)

  h = svntest.main.create_http_connection(sbox.repo_url)

  # PROPFIND /repos/!svn/rvr/1, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<allprop/>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/1', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:S="http://subversion.tigris.org/xmlns/svn/" '
                  'xmlns:C="http://subversion.tigris.org/xmlns/custom/" '
                  'xmlns:V="http://subversion.tigris.org/xmlns/dav/" '
                  'xmlns:lp1="DAV:" '
                  'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/1/</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
            '<lp1:resourcetype><D:collection/></lp1:resourcetype>\n'
            '<lp1:getcontenttype>' +
              'text/html; charset=UTF-8' +
            '</lp1:getcontenttype>\n'
            '<lp1:getetag>W/"1//"</lp1:getetag>\n'
            '<lp1:creationdate>2015-01-01T00:00:00.0Z</lp1:creationdate>\n'
            '<lp1:getlastmodified>' +
              'Thu, 01 Jan 2015 00:00:00 GMT' +
            '</lp1:getlastmodified>\n'
            '<lp1:checked-in>'
              '<D:href>' + repo_uripath + '/!svn/ver/1/</D:href>'
            '</lp1:checked-in>\n'
            '<lp1:version-controlled-configuration>'
              '<D:href>' + repo_uripath + '/!svn/vcc/default</D:href>'
            '</lp1:version-controlled-configuration>\n'
            '<lp1:version-name>1</lp1:version-name>\n'
            '<lp1:creator-displayname>jrandom</lp1:creator-displayname>\n'
            '<lp2:baseline-relative-path/>\n'
            '<lp2:repository-uuid>' +
              'd7130b12-92f6-45c9-9217-b9f0472c3fab' +
            '</lp2:repository-uuid>\n'
            '<lp2:deadprop-count>0</lp2:deadprop-count>\n'
            '<D:lockdiscovery/>\n'
          '</D:prop>\n'
        '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)


@SkipUnless(svntest.main.is_ra_type_dav)
def propfind_propname(sbox):
  "verify propname PROPFIND response"

  sbox.build()
  sbox.simple_propset('a', 'b', 'iota')
  sbox.simple_commit()
  repo_uripath = '/' + svntest.wc.svn_uri_quote(
    sbox.repo_dir.replace(os.path.sep, '/'))

  h = svntest.main.create_http_connection(sbox.repo_url)

  # PROPFIND /repos/!svn/rvr/2/iota, Depth = 0
  headers = {
    'Authorization': 'Basic ' + base64.b64encode(b'jconstant:rayjandom').decode(),
    'Depth': '0',
  }
  req_body = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<propfind xmlns="DAV:">\n'
      '<propname/>\n'
    '</propfind>\n'
    )
  h.request('PROPFIND', sbox.repo_url + '/!svn/rvr/2/iota', req_body, headers)
  r = h.getresponse()
  if r.status != httplib.MULTI_STATUS:
    raise svntest.Failure('Unexpected status: %d %s' % (r.status, r.reason))

  expected_response = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">\n'
      '<D:response xmlns:S="http://subversion.tigris.org/xmlns/svn/" '
                  'xmlns:C="http://subversion.tigris.org/xmlns/custom/" '
                  'xmlns:V="http://subversion.tigris.org/xmlns/dav/" '
                  'xmlns:lp1="DAV:" '
                  'xmlns:lp2="http://subversion.tigris.org/xmlns/dav/">\n'
        '<D:href>' + repo_uripath + '/!svn/rvr/2/iota</D:href>\n'
        '<D:propstat>\n'
          '<D:prop>\n'
          '<C:a/>\n'
          '<lp1:resourcetype/>\n'
          '<lp1:getcontentlength/>\n'
          '<lp1:getcontenttype/>\n'
          '<lp1:getetag/>\n'
          '<lp1:creationdate/>\n'
          '<lp1:getlastmodified/>\n'
          '<lp1:checked-in/>\n'
          '<lp1:version-controlled-configuration/>\n'
          '<lp1:version-name/>\n'
          '<lp1:creator-displayname/>\n'
          '<lp2:baseline-relative-path/>\n'
          '<lp2:md5-checksum/>\n'
          '<lp2:repository-uuid/>\n'
          '<lp2:deadprop-count/>\n'
          '<lp2:sha1-checksum/>\n'
          '<D:supportedlock/>\n'
          '<D:lockdiscovery/>\n'
          '</D:prop>\n'
          '<D:status>HTTP/1.1 200 OK</D:status>\n'
        '</D:propstat>\n'
      '</D:response>\n'
    '</D:multistatus>\n'
    )
  actual_response = r.read()
  verify_xml_response(expected_response, actual_response)

########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              cache_control_header,
              simple_propfind,
              propfind_multiple_props,
              propfind_404,
              propfind_allprop,
              propfind_propname,
             ]
serial_only = True

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
