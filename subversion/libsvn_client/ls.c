/*
 * ls.c:  list local and remote directory entries.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

/* ==================================================================== */



#include "client.h"
#include "svn_client.h"
#include "svn_path.h"

#include "svn_private_config.h"

static svn_error_t *
get_dir_contents (apr_hash_t *dirents,
                  const char *dir,
                  svn_revnum_t rev,
                  svn_ra_plugin_t *ra_lib,
                  void *session,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_dirent_t *the_ent;
  apr_hash_index_t *hi;

  /* Get the directory's entries, but not its props. */
  if (ra_lib->get_dir)
    SVN_ERR (ra_lib->get_dir (session, dir, rev, &tmpdirents, 
                              NULL, NULL, pool));
  else
    return svn_error_create (SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                             _("No get_dir() available for URL schema"));

  if (ctx->cancel_func)
    SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

  for (hi = apr_hash_first (pool, tmpdirents);
       hi;
       hi = apr_hash_next (hi))
    {
      const char *path;
      const void *key;
      void *val;

      apr_hash_this (hi, &key, NULL, &val);

      the_ent = val;

      path = svn_path_join (dir, key, pool);

      apr_hash_set (dirents, path, APR_HASH_KEY_STRING, val);

      if (recurse && the_ent->kind == svn_node_dir)
        SVN_ERR (get_dir_contents (dirents, path, rev, ra_lib, session,
                                   recurse, ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls2 (apr_hash_t **dirents,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,               
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *session;
  svn_revnum_t rev;
  svn_node_kind_t url_kind;
  const char *url;

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR (svn_client__ra_lib_from_path (&ra_lib, &session, &rev,
                                         &url, path_or_url, peg_revision,
                                         revision, ctx, pool));

  /* Decide if the URL is a file or directory. */
  SVN_ERR (ra_lib->check_path (session, "", rev, &url_kind, pool));

  if (url_kind == svn_node_dir)
    {
      *dirents = apr_hash_make (pool);

      SVN_ERR (get_dir_contents (*dirents, "", rev, ra_lib, session, recurse,
                                 ctx, pool));
    }
  else if (url_kind == svn_node_file)
    {
      apr_hash_t *parent_ents;
      const char *parent_url, *base_name;
      svn_dirent_t *the_ent;

      /* Re-open the session to the file's parent instead. */
      svn_path_split (url, &parent_url, &base_name, pool);
      /* 'base_name' is now the last component of an URL, but we want
         to use it as a plain file name. Therefore, we must URI-decode
         it. */
      base_name = svn_path_uri_decode(base_name, pool);
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, parent_url,
                                            NULL,
                                            NULL, NULL, FALSE, TRUE, 
                                            ctx, pool));

      /* Get all parent's entries, no props. */
      if (ra_lib->get_dir)
        SVN_ERR (ra_lib->get_dir (session, "", rev, &parent_ents, 
                                  NULL, NULL, pool));
      else
        return svn_error_create (SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                 _("No get_dir() available for URL schema"));

      /* Copy the relevant entry into the caller's hash. */
      *dirents = apr_hash_make (pool);
      the_ent = apr_hash_get (parent_ents, base_name, APR_HASH_KEY_STRING);
      if (the_ent == NULL)
        return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                                  _("URL '%s' non-existent in that revision"),
                                  url);

      apr_hash_set (*dirents, base_name, APR_HASH_KEY_STRING, the_ent);
    }
  else
    return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                              _("URL '%s' non-existent in that revision"),
                              url);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls (apr_hash_t **dirents,
               const char *path_or_url,
               svn_opt_revision_t *revision,
               svn_boolean_t recurse,               
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  return svn_client_ls2 (dirents, path_or_url, revision,
                         revision, recurse, ctx, pool);
}
