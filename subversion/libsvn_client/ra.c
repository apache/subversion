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
#include "svn_path.h"

#include "client.h"


static svn_error_t *
open_admin_tmp_file (apr_file_t **fp,
                     void *callback_baton)
{
  svn_client_auth_baton_t *cb = 
    (svn_client_auth_baton_t *) callback_baton;
  
  SVN_ERR (svn_wc_create_tmp_file (fp, cb->path, cb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
open_tmp_file (apr_file_t **fp,
               void *callback_baton)
{
  svn_client_auth_baton_t *cb = (svn_client_auth_baton_t *) callback_baton;
  svn_stringbuf_t *truepath = svn_stringbuf_dup (cb->path, cb->pool);
  svn_stringbuf_t *ignored_filename;

  /* Tack on a made-up filename. */
  svn_path_add_component_nts (truepath, "tempfile", svn_path_local_style);

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    truepath, ".tmp", TRUE, cb->pool));

  return SVN_NO_ERROR;
}


/* Fetch a vtable of CALLBACKS/CALLBACK_BATON suitable for passing
   to RA->open().  AUTH_BATON is originally provided by the calling
   application.  Do allocation in POOL.

   The calling libsvn_client routine customizes these callbacks, based
   on what it's about to do with the RA session:

      - PATH customizes the callbacks to operate on a specific path in
        the working copy.  

      - DO_STORE indicates whether the RA layer should attempt to
        store authentication info.

      - USE_ADMIN indicates that the RA layer should create tempfiles
        in the administrative area instead of in the working copy itself.

*/
static svn_error_t *
get_ra_callbacks (svn_ra_callbacks_t **callbacks,
                  void **callback_baton,
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *path,
                  svn_boolean_t do_store,
                  svn_boolean_t use_admin,
                  apr_pool_t *pool)
{
  svn_ra_callbacks_t *cbtable = 
    (svn_ra_callbacks_t *) apr_pcalloc (pool, sizeof(*cbtable));
  
  cbtable->open_tmp_file = use_admin ? open_admin_tmp_file : open_tmp_file;
  cbtable->get_authenticator = svn_client__get_authenticator;

  /* Just copy the PATH and DO_STORE into the baton, so callbacks can
     see them later. */
  auth_baton->path = path;
  auth_baton->do_store = do_store;
  
  /* This is humorous; at present, we use the application-provided
     auth_baton as the baton for whole the callbacks-vtable!  This
     might not always be so.  For now, it's just easier that
     svn_client_auth_baton_t be shared by the application and client
     both, rather than wrapping one baton in another. */
  *callback_baton = auth_baton;

  *callbacks = cbtable;
  return SVN_NO_ERROR;
}


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

  SVN_ERR (get_ra_callbacks (&ra_callbacks, &cb_baton,
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
