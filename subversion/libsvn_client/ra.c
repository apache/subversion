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
  const char *truepath;
  const char *ignored_filename;

  if (cb->base_dir)
    truepath = apr_pstrdup (cb->pool, cb->base_dir);
  else
    /* ### TODO: need better tempfile support */
    truepath = "";

  /* Tack on a made-up filename. */
  truepath = svn_path_join (truepath, "tempfile", cb->pool);

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    truepath, ".tmp", TRUE, cb->pool));

  return SVN_NO_ERROR;
}


/* This implements the `svn_ra_get_wc_prop_func_t' interface. */
static svn_error_t *
get_wc_prop (void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t **value,
             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;

  *value = NULL;

  /* If we have a list of commit_items, search through that for a
     match for this relative URL. */
  if (cb->commit_items)
    {
      int i;
      for (i = 0; i < cb->commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) cb->commit_items->elts)[i];
          if (! strcmp (relpath, 
                        svn_path_uri_decode (item->url, pool)))
            return svn_wc_prop_get (value, name, item->path, pool);
        }

      return SVN_NO_ERROR;
    }

  /* If we don't have a base directory, then there are no properties. */
  else if (cb->base_dir == NULL)
    return SVN_NO_ERROR;

  return svn_wc_prop_get (value, name,
                          svn_path_join (cb->base_dir, relpath, pool),
                          pool);
}

/* This implements the `svn_ra_push_wc_prop_func_t' interface. */
static svn_error_t *
push_wc_prop (void *baton,
              const char *relpath,
              const char *name,
              const svn_string_t *value,
              apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;

  /* If we're committing, search through the commit_items list for a
     match for this relative URL. */
  if (cb->commit_items)
    {
      int i;
      for (i = 0; i < cb->commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) cb->commit_items->elts)[i];

          if (strcmp (relpath, svn_path_uri_decode (item->url, pool)) == 0)
            {
              apr_pool_t *cpool = item->wcprop_changes->pool;
              svn_prop_t *prop = apr_palloc (cpool, sizeof (*prop));

              prop->name = apr_pstrdup (cpool, name);
              if (value)
                {
                  prop->value
                    = svn_string_ncreate (value->data, value->len, cpool);
                }
              else
                prop->value = NULL;

              /* Buffer the propchange to take effect during the
                 post-commit process. */

              *((svn_prop_t **) apr_array_push (item->wcprop_changes)) = prop;

              return SVN_NO_ERROR;
            }
        }
    }
  else
    {
      /* We could just set the property directly here; technically,
         this function's interface allows us to.  But right now, there
         should be no code paths that call this function without
         commit_items.   If someday there were, they would need to be
         carefully investigated for us to know they were safe.  See
         http://subversion.tigris.org/issues/show_bug.cgi?id=806 for
         details why.  In the meantime, return an error so anyone who
         starts down that road will encounter this comment. */

      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
         "Attempt to set wc property '%s' on '%s' in a non-commit operation",
         name, relpath);
    }

  return SVN_NO_ERROR;
}


svn_error_t * 
svn_client__open_ra_session (void **session_baton,
                             const svn_ra_plugin_t *ra_lib,
                             const char *base_url,
                             const char *base_dir,
                             svn_wc_adm_access_t *base_access,
                             apr_array_header_t *commit_items,
                             svn_boolean_t do_store,
                             svn_boolean_t use_admin,
                             svn_client_auth_baton_t *auth_baton,
                             apr_pool_t *pool)
{
  svn_ra_callbacks_t *cbtable = apr_pcalloc (pool, sizeof(*cbtable));
  svn_client__callback_baton_t *cb = apr_pcalloc (pool, sizeof(*cb));

  cbtable->open_tmp_file = use_admin ? open_admin_tmp_file : open_tmp_file;
  cbtable->get_authenticator = svn_client__get_authenticator;
  cbtable->get_wc_prop = use_admin ? get_wc_prop : NULL;
  cbtable->push_wc_prop = commit_items ? push_wc_prop : NULL;

  cb->auth_baton = auth_baton;
  cb->base_dir = base_dir;
  cb->base_access = base_access;
  cb->do_store = do_store;
  cb->pool = pool;
  cb->commit_items = commit_items;

  SVN_ERR (ra_lib->open (session_baton, base_url, cbtable, cb, pool));

  return SVN_NO_ERROR;
}
                                        


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
