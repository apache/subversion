#
# ra.py: public Python interface for ra components
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

from libsvn.ra import *
from svn.core import _unprefix_names
_unprefix_names(locals(), 'svn_ra_')
_unprefix_names(locals(), 'SVN_RA_')
del _unprefix_names

class Callbacks:
  """Base class for callbacks structure for svn.ra.open2.

  Ra users may pass an instance of this class as is to svn.ra.open2
  for some simple operations: as long as authentication is not
  required, auth_baton may be None, and some ra implementations do not
  use open_tmp_file at all.  These are not guarantees, however, and
  all but the simplest scripts should fill even these in.

  The wc_prop slots, on the other hand, are only necessary for commits
  and updates, and progress_func and cancel_func are always optional.

  A simple example:

  class Callbacks(svn.ra.Callbacks):
    def __init__(self, wc, username, password):
      self.wc = wc
      self.auth_baton = svn.core.svn_auth_open([
          svn.client.get_simple_provider(),
          svn.client.get_username_provider(),
          ])
      svn.core.svn_auth_set_parameter(self.auth_baton,
                                      svn.core.SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                      username)
      svn.core.svn_auth_set_parameter(self.auth_baton,
                                      svn.core.SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                      password)
    def open_tmp_file(self, pool):
      path = '/'.join([self.wc, svn.wc.get_adm_dir(pool), 'tmp'])
      (fd, fn) = tempfile.mkstemp(dir=path)
      os.close(fd)
      return fn
    def cancel_func(self):
      if some_condition():
        return svn.core.SVN_ERR_CANCELLED
      return 0
  """
  open_tmp_file = None
  auth_baton = None
  get_wc_prop = None
  set_wc_prop = None
  push_wc_prop = None
  invalidate_wc_props = None
  progress_func = None
  cancel_func = None
  get_client_string = None
