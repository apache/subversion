#
# wc.py : various utilities for interacting with the _wc module
#
######################################################################
#
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

import libsvn.wc

# copy the wrapper functions out of the extension module, dropping the
# 'svn_wc_' prefix.
for name in dir(libsvn.wc):
  if name[:7] == 'svn_wc_':
    vars()[name[7:]] = getattr(libsvn.wc, name)

  # XXX: For compatibility reasons, also include the prefixed name
  vars()[name] = getattr(libsvn.wc, name)

# we don't want these symbols exported
del name, libsvn
