/*
 * ra.c :  routines for interacting with the RA layer
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <assert.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"
#include "client.h"


static svn_error_t *
open_admin_tmp_file (apr_file_t **fp,
                     void *callback_baton)
{
  svn_client__callback_baton_t *cb = callback_baton;
  
  SVN_ERR (svn_wc_create_tmp_file (fp, cb->base_dir, TRUE, cb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
open_tmp_file (apr_file_t **fp,
               void *callback_baton)
{
  svn_client__callback_baton_t *cb = callback_baton;
  svn_stringbuf_t *truepath;
  svn_stringbuf_t *ignored_filename;

  if (cb->base_dir)
    truepath = svn_stringbuf_dup (cb->base_dir, cb->pool);
  else
    /* ### TODO: need better tempfile support */
    truepath = svn_stringbuf_create (".", cb->pool);

  /* Tack on a made-up filename. */
  svn_path_add_component_nts (truepath, "tempfile");

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    truepath->data, ".tmp", TRUE, cb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_wc_prop (void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t **value,
             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  struct svn_wc_close_commit_baton ccb;

  /* if we don't have a base directory, then there are no properties */
  if (cb->base_dir == NULL)
    {
      *value = NULL;
      return SVN_NO_ERROR;
    }

  /* ### this should go away, and svn_wc_get_wc_prop should just take this
     ### stuff as parameters */
  ccb.prefix_path = cb->base_dir;

  return svn_wc_get_wc_prop (&ccb, relpath, name, value, pool);
}

static svn_error_t *
set_wc_prop (void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  struct svn_wc_close_commit_baton ccb;

  /* if we don't have a base directory, that's a problem. */
  assert (cb->base_dir);

  /* ### this should go away, and svn_wc_set_wc_prop should just take this
     ### stuff as parameters */
  ccb.prefix_path = cb->base_dir;

  return svn_wc_set_wc_prop (&ccb, relpath, name, value, pool);
}


static svn_error_t *
close_commit (void *baton,
              svn_stringbuf_t *relpath,
              svn_boolean_t recurse,
              svn_revnum_t new_rev,
              const char *rev_date,
              const char *rev_author,
              apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  struct svn_wc_close_commit_baton ccb;

  /* if we don't have a base directory, that's a problem. */
  assert (cb->base_dir);

  /* ### this should go away, and svn_wc_process_committed should just
     take this ### stuff as parameters */
  ccb.prefix_path = cb->base_dir;

  return svn_wc_process_committed (&ccb, relpath, recurse, new_rev,
                                   rev_date, rev_author, pool);
}


svn_error_t * svn_client__open_ra_session (void **session_baton,
                                           const svn_ra_plugin_t *ra_lib,
                                           svn_stringbuf_t *repos_URL,
                                           svn_stringbuf_t *base_dir,
                                           svn_boolean_t do_store,
                                           svn_boolean_t use_admin,
                                           svn_boolean_t read_only_wc,
                                           void *auth_baton,
                                           apr_pool_t *pool)
{
  svn_ra_callbacks_t *cbtable = apr_pcalloc (pool, sizeof(*cbtable));
  svn_client__callback_baton_t *cb = apr_pcalloc (pool, sizeof(*cb));

  cbtable->open_tmp_file = use_admin ? open_admin_tmp_file : open_tmp_file;
  cbtable->get_authenticator = svn_client__get_authenticator;
  cbtable->get_wc_prop = get_wc_prop;
  cbtable->set_wc_prop = read_only_wc ? NULL : set_wc_prop;
  cbtable->close_commit = read_only_wc ? NULL : close_commit;

  cb->auth_baton = auth_baton;
  cb->base_dir = base_dir;
  cb->do_store = do_store;
  cb->pool = pool;

  SVN_ERR (ra_lib->open (session_baton, repos_URL, cbtable, cb, pool));

  return SVN_NO_ERROR;
}
                                        


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
