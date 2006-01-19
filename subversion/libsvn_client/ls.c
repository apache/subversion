/*
 * ls.c:  list local and remote directory entries.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_pools.h"

#include "svn_private_config.h"

static svn_error_t *
get_dir_contents (apr_uint32_t dirent_fields,
                  apr_hash_t *dirents,
                  const char *dir,
                  svn_revnum_t rev,
                  svn_ra_session_t *ra_session,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_dirent_t *the_ent;
  apr_hash_index_t *hi;

  /* Get the directory's entries, but not its props. */
  SVN_ERR (svn_ra_get_dir2 (ra_session, dir, rev, dirent_fields, &tmpdirents, 
                            NULL, NULL, pool));

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
        SVN_ERR (get_dir_contents (dirent_fields, dirents, path, rev,
                                   ra_session, recurse, ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls4 (apr_hash_t **dirents,
                apr_hash_t **locks,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,
                apr_uint32_t dirent_fields,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_revnum_t rev;
  svn_dirent_t *dirent;
  const char *url;
  const char *repos_root;
  const char *rel_path;
  svn_error_t *err;

  /* We use the kind field to determine if we should recurse, so we
     always need it. */
  dirent_fields |= SVN_DIRENT_KIND;

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR (svn_client__ra_session_from_path (&ra_session, &rev,
                                             &url, path_or_url, peg_revision,
                                             revision, ctx, pool));

  SVN_ERR (svn_ra_get_repos_root (ra_session, &repos_root, pool));

  /* Get path relative to repository root. */
  rel_path = svn_path_is_child (repos_root, url, pool);

  err = svn_ra_stat (ra_session, "", rev, &dirent, pool);

  /* svnserve before 1.2 doesn't support the above, so fall back on
     a less efficient method. */
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_node_kind_t url_kind;

      svn_error_clear (err);

      SVN_ERR (svn_ra_check_path (ra_session, "", rev, &url_kind, pool));

      if (url_kind == svn_node_dir)
        {
          /* Fake a dirent; we only use the kind field below. */
          dirent = apr_palloc (pool, sizeof (*dirent));
          dirent->kind = svn_node_dir;
        }
      else if (url_kind == svn_node_file)
        {
          svn_ra_session_t *parent_session;
          apr_hash_t *parent_ents;
          const char *parent_url, *base_name;

          /* Open another session to the file's parent.
             This server doesn't support svn_ra_reparent anyway, so don't
             try it. */
          svn_path_split (url, &parent_url, &base_name, pool);

          /* 'base_name' is now the last component of an URL, but we want
             to use it as a plain file name. Therefore, we must URI-decode
             it. */
          base_name = svn_path_uri_decode (base_name, pool);
          SVN_ERR (svn_client__open_ra_session_internal (&parent_session,
                                                         parent_url, NULL,
                                                         NULL, NULL, FALSE,
                                                         TRUE, ctx, pool));

          /* Get all parent's entries, no props. */
          SVN_ERR (svn_ra_get_dir2 (parent_session, "", rev, dirent_fields,
                                    &parent_ents, NULL, NULL, pool));

          /* Get the relevant entry. */
          dirent = apr_hash_get (parent_ents, base_name, APR_HASH_KEY_STRING);
        }
      else
        dirent = NULL;
    }
  else if (err)
    return err;

  if (! dirent)
    return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                              _("URL '%s' non-existent in that revision"),
                              url);

  *dirents = apr_hash_make (pool);

  if (dirent->kind == svn_node_dir)
    SVN_ERR (get_dir_contents (dirent_fields, *dirents, "", rev, ra_session,
                               recurse, ctx, pool));
  else if (dirent->kind == svn_node_file)
    {
      const char *base_name = svn_path_uri_decode (svn_path_basename (url,
                                                                      pool),
                                                   pool);
      apr_hash_set (*dirents, base_name, APR_HASH_KEY_STRING, dirent);
    }

  if (locks)
    {
      apr_hash_t *new_locks;
      apr_hash_index_t *hi;

      /* Add a leading slash to match the paths from svn_ra_get_locks(). */
      rel_path = apr_psprintf (pool, "/%s", rel_path ? rel_path : "");

      /* If we have a file, we want the lock added to the hash table
         below to have the basename of the file, so strip off the basename
         of rel_path. */
      if (dirent->kind == svn_node_file)
        rel_path = svn_path_dirname (rel_path, pool);

      /* Get locks. */
      err = svn_ra_get_locks (ra_session, locks, "", pool);

      if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
        {
          svn_error_clear (err);
          *locks = apr_hash_make (pool);
        }
      else if (err)
        return err;

      new_locks = apr_hash_make (pool);
      for (hi = apr_hash_first (pool, *locks); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *newkey;

          apr_hash_this (hi, &key, NULL, &val);
          newkey = svn_path_is_child (rel_path, key, pool);
          if (newkey)
            apr_hash_set (new_locks, newkey, APR_HASH_KEY_STRING, val);
        }

      *locks = new_locks;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls3 (apr_hash_t **dirents,
                apr_hash_t **locks,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_ls4 (dirents, locks, path_or_url, peg_revision,
                         revision, recurse, SVN_DIRENT_ALL, ctx, pool);
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

  return svn_client_ls3 (dirents, NULL, path_or_url, peg_revision,
                         revision, recurse, ctx, pool);
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
