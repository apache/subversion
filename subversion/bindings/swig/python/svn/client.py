#
# client.py: public Python interface for client components
#
# Subversion is a tool for revision control. 
# See http://subversion.tigris.org for more information.
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

import libsvn.client

# copy the wrapper functions out of the extension module, dropping the
# 'svn_client_' prefix.
for name in dir(libsvn.client):
  if name[:11] == 'svn_client_':
    vars()[name[11:]] = getattr(libsvn.client, name)

  # XXX: For compatibility reasons, also include the prefixed name
  vars()[name] = getattr(libsvn.client, name)

# we don't want these symbols exported
del name, libsvn
