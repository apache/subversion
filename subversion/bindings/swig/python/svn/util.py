#
# svn.util: public Python interface for miscellaneous bindings
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

# bring all the symbols up into this module
### in the future, we may want to limit this, rename things, etc
from _util import *

def run_app(func, *args, **kw):
  '''Run a function as an "APR application".

  APR is initialized, and an application pool is created. Cleanup is
  performed as the function exits (normally or via an exception.
  '''
  apr_initialize()
  try:
    pool = svn_pool_create(None)
    try:
      return apply(func, (pool,) + args, kw)
    finally:
      svn_pool_destroy(pool)
  finally:
    apr_terminate()

# some minor patchups
svn_pool_destroy = apr_pool_destroy
