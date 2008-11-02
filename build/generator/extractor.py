#!/usr/bin/env python
#
# extractor.py: extract function names from declarations in header files
#
# ====================================================================
# Copyright (c) 2000-2006, 2008 CollabNet.  All rights reserved.
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
_funcs = re.compile(r'^(?:(?:(?:\w+|\*) )+\*?)?((?:svn|apr)_[a-z_0-9]+)\s*\(', re.M)

def extract_funcs(fname):
  funcs = [ ]
  for name in _funcs.findall(open(fname).read()):
    if name not in _filter_names:
      funcs.append(name)
  return funcs

_filter_names = [
  'svn_boolean_t',  # svn_config_enumerator_t looks like (to our regex) a
                    # function declaration for svn_boolean_t

  # Not available on Windows
  'svn_auth_get_keychain_simple_provider',
  'svn_auth_get_keychain_ssl_client_cert_pw_provider',
  'svn_auth_get_gnome_keyring_simple_provider',
  'svn_auth_get_gnome_keyring_ssl_client_cert_pw_provider',
  'svn_auth_get_kwallet_simple_provider',
  'svn_auth_get_kwallet_ssl_client_cert_pw_provider',
  'svn_auth_gnome_keyring_version',
  'svn_auth_kwallet_version',
  ]

if __name__ == '__main__':
  # run the extractor over each file mentioned
  import sys
  print("EXPORTS")
  for fname in sys.argv[1:]:
    for func in extract_funcs(fname):
      print(func)
    if os.path.basename(fname) == 'svn_ctype.h':
      print('svn_ctype_table = svn_ctype_table_internal CONSTANT')
