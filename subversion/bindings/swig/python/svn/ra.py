#
# ra.py : various utilities for interacting with the _ra module
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

import libsvn.ra

# copy the wrapper functions out of the extension module, dropping the
# 'svn_ra_' prefix.
for name in dir(libsvn.ra):
  if name[:7] == 'svn_ra_':
    vars()[name[7:]] = getattr(libsvn.ra, name)

  # XXX: For compatibility reasons, also include the prefixed name
  vars()[name] = getattr(libsvn.ra, name)

# we don't want these symbols exported
del name, libsvn
