#
# extractor.py: extract function names from declarations in header files
#
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================
#

import os
import re
import string


#
# This parses the following two types of declarations:
#
#    void
#    svn_foo_bar (args)
# or
#    void svn_foo_bar (args)
#
# The space is optional, as some of our headers (currently) omit the
# space before the open parenthesis.
#
_funcs = re.compile('^(?:(svn_[a-z_0-9]+)|[a-z].*?(svn_[a-z_0-9]+)) ?\(')

def extract_funcs(fname):
  funcs = [ ]
  for line in open(fname).readlines():
    match = _funcs.match(line)
    if match:
      name = match.group(1) or match.group(2)
      if not name:
        print 'WTF?', match.groups()
      elif name not in _filter_names:
        funcs.append(name)
  return funcs

_filter_names = [
  'svn_boolean_t',  # svn_config_enumerator_t looks like (to our regex) a
                    # function declaration for svn_boolean_t
  ]

def scan_headers(include_dir):
  "Return a dictionary mapping library basenames to a list of func names."

  libs = { }

  for header, lib in headers_to_libraries.items():
    if not lib:
      continue

    fname = os.path.join(include_dir, 'svn_%s.h' % header)
    funcs = extract_funcs(fname)

    libname = 'libsvn_%s' % lib
    if libs.has_key(libname):
      libs[libname].extend(funcs)
    else:
      libs[libname] = funcs

  ### do some cleanup tweaks

  return libs

#
# Map header names to the libraries which contain the named functions
#
# Note: we could also use the secondary prefix (e.g. FOO in svn_FOO_), but
# there are more of those than headers.
#
headers_to_libraries = {
  'auth' : 'subr',
  'base64' : 'subr',
  'client' : 'client',
  'config' : 'subr',
  'dav' : None,
  'delta' : 'delta',
  'diff' : 'diff',
  'error' : 'subr',
  'error_codes' : None,
  'fs' : 'fs',
  'hash' : 'subr',
  'io' : 'subr',
  'md5' : 'subr',
  'opt' : 'subr',
  'path' : 'subr',
  'pools' : 'subr',
  'props' : 'subr',
  'quoprint' : 'subr',
  'ra' : 'ra',
  'ra_svn' : 'ra_svn',
  'repos' : 'repos',
  'sorts' : 'subr',
  'string' : 'subr',
  'subst' : 'subr',
  'test' : 'test',  # do we need symbols here? or just always build static?
  'time' : 'subr',
  'types' : 'subr',
  'utf' : 'subr',
  'version' : None,
  'wc' : 'wc',
  'xml' : 'subr',
  }


if __name__ == '__main__':
  # run the extractor over each file mentioned
  import sys
  if len(sys.argv) == 1:
    # no header files provided. assume we're in the include dir and extract
    # all the function names
    libs = scan_headers('.')
    for lib, funcs in libs.items():
      for f in funcs:
        print lib, f

    # compare the above output to the actual libraries using:
    # for f in libsvn_*-1.so ; do b="`echo $f | sed 's/-.*//'`" ; nm $f | sed -n -e '/__/d' -e "/T svn_/s/.* T/$b/p" ; done

  else:
    for fname in sys.argv[1:]:
      funcs = extract_funcs(fname)
      if funcs:
        print string.join(funcs, '\n')
