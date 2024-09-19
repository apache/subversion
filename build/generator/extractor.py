#!/usr/bin/env python
#
# extractor.py: extract function names from declarations in header files
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
# ====================================================================
#

import os
import re

#
# This parses the following two types of declarations:
#
#    void
#    svn_foo_bar (args)
# or
#    void svn_foo_bar (args)
#
_funcs = re.compile(r'^(?:(?:(?:\w+|\*) )+\*?)?((?:svn|apr)_[A-Za-z0-9_]+)\s*\(', re.M)

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
  'svn_auth_get_gpg_agent_simple_provider',
  'svn_auth_gpg_agent_version',

  # Unavailable in release mode
  'svn_fs_base__trail_debug',
  ]

if __name__ == '__main__':
  # run the extractor over each file mentioned
  import sys
  print("EXPORTS")
  for fname in sys.argv[1:]:
    for func in extract_funcs(fname):
      print(func)
    if os.path.basename(fname) == 'svn_ctype.h':
      # See libsvn_subr/ctype.c for an explanation why we use CONSTANT and not
      # DATA, even though it causes an LNK4087 warning!
      print('svn_ctype_table = svn_ctype_table_internal CONSTANT')
    elif os.path.basename(fname) == 'svn_wc_private.h':
      # svn_wc__internal_walk_children() is now internal to libsvn_wc
      # but entries-dump.c still calls it
      print('svn_wc__internal_walk_children')
