/*
 * ra.c :  routines for interacting with the RA layer
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <apr_pools.h>

#include "svn_ra.h"
#include "svn_client.h"

#include "client.h"


svn_error_t * svn_client__open_ra_session (void **session_baton,
                                           const svn_ra_plugin_t *ra_lib,
                                           svn_stringbuf_t *repos_URL,
                                           svn_stringbuf_t *base_dir,
                                           svn_boolean_t do_store,
                                           svn_boolean_t use_admin,
                                           void *auth_baton,
                                           apr_pool_t *pool)
{
  svn_ra_callbacks_t *ra_callbacks;
  void *cb_baton;

  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton, base_dir,
                                         do_store, use_admin,
                                         pool));

  SVN_ERR (ra_lib->open (session_baton, repos_URL,
                         ra_callbacks, cb_baton,
                         pool));

  return SVN_NO_ERROR;
}
                                        


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
