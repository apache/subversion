/*
 * list.c:  list local and remote directory entries.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_props.h"

#include "client.h"
#include "tree.h"

#include "private/svn_fspath.h"
#include "svn_private_config.h"

/* Get the directory entries of DIR at REV (relative to the root of
   RA_SESSION), getting at least the fields specified by DIRENT_FIELDS.

   Ignore any not-authorized errors.

   Call the list callback in a deterministic order: depth-first traversal
   with the entries in each directory sorted lexicographically.

   Use POOL for temporary allocations.
*/
static svn_error_t *
get_dir_contents(apr_uint32_t dirent_fields,
                 const char *dir,
                 svn_revnum_t rev,
                 svn_ra_session_t *ra_session,
                 apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_error_t *err;

  /* Get the directory's entries, but not its props.  Ignore any
     not-authorized errors.  */
  err = svn_ra_get_dir2(ra_session, &tmpdirents, NULL, NULL,
                        dir, rev, dirent_fields, pool);
  if (err && ((err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED) ||
              (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN)))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/*
 */
struct list_baton_t
{
  /* The absolute filesystem path of the RA session. */
  const char *fs_path;
  apr_uint32_t dirent_fields;
  /* If non-NULL, maps (const char *) paths to svn_lock_t objects. */
  apr_hash_t *locks;
  svn_client_list_func_t list_func;
  void *list_baton;
  svn_client_ctx_t *ctx;
};

/* */
static svn_error_t *list_callback(svn_tree_t *tree,
                                  const char *relpath,
                                  svn_kind_t kind,
                                  void *baton,
                                  apr_pool_t *scratch_pool)
{
  struct list_baton_t *b = baton;
  svn_dirent_t dirent = { 0 };
  svn_lock_t *lock;

  SVN_ERR_ASSERT(kind != svn_kind_none);
  if (kind == svn_kind_unknown)
    {
      /* Unauthorized path -- ignore it. */
      return SVN_NO_ERROR;
    }

  if (b->locks)
    {
      const char *abs_fspath = svn_fspath__join(b->fs_path, relpath, scratch_pool);
      lock = apr_hash_get(b->locks, abs_fspath, APR_HASH_KEY_STRING);
    }
  else
    lock = NULL;

  /* SVN_ERR(svn_tree_get_kind(tree, &kind, relpath, pool)); */
  dirent.kind = svn__node_kind_from_kind(kind);
  /* if (dirent.kind == svn_kind_file)
    dirent.size = svn_tree_stat ... */

  SVN_ERR(b->list_func(b->list_baton, relpath, &dirent, lock,
                       b->fs_path, scratch_pool));
  return SVN_NO_ERROR;
}

/* Like svn_ra_stat() but with a compatibility hack for pre-1.2 svnserve. */
static svn_error_t *
ra_stat_compatible(svn_ra_session_t *ra_session,
                   svn_revnum_t rev,
                   svn_dirent_t **dirent_p,
                   apr_uint32_t dirent_fields,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *repos_root, *url;
  svn_error_t *err;
  svn_dirent_t *dirent;

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));
  SVN_ERR(svn_ra_get_session_url(ra_session, &url, pool));

  err = svn_ra_stat(ra_session, "", rev, &dirent, pool);

  /* svnserve before 1.2 doesn't support the above, so fall back on
     a less efficient method. */
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_node_kind_t kind;

      svn_error_clear(err);

      SVN_ERR(svn_ra_check_path(ra_session, "", rev, &kind, pool));

      if (kind != svn_node_none)
        {
          if (strcmp(url, repos_root) != 0)
            {
              svn_ra_session_t *parent_session;
              apr_hash_t *parent_ents;
              const char *parent_url, *base_name;

              /* Open another session to the path's parent.  This server
                 doesn't support svn_ra_reparent anyway, so don't try it. */
              svn_uri_split(&parent_url, &base_name, url, pool);

              SVN_ERR(svn_client__open_ra_session_internal(&parent_session,
                                                           NULL, parent_url,
                                                           NULL, NULL, FALSE,
                                                           TRUE, ctx, pool));

              /* Get all parent's entries, no props. */
              SVN_ERR(svn_ra_get_dir2(parent_session, &parent_ents, NULL,
                                      NULL, "", rev, dirent_fields, pool));

              /* Get the relevant entry. */
              dirent = apr_hash_get(parent_ents, base_name,
                                    APR_HASH_KEY_STRING);
            }
          else
            {
              /* We can't get the directory entry for the repository root,
                 but we can still get the information we want.
                 The created-rev of the repository root must, by definition,
                 be rev. */
              dirent = apr_palloc(pool, sizeof(*dirent));
              dirent->kind = kind;
              dirent->size = 0;
              if (dirent_fields & SVN_DIRENT_HAS_PROPS)
                {
                  apr_hash_t *props;
                  SVN_ERR(svn_ra_get_dir2(ra_session, NULL, NULL, &props,
                                          "", rev, 0 /* no dirent fields */,
                                          pool));
                  dirent->has_props = (apr_hash_count(props) != 0);
                }
              dirent->created_rev = rev;
              if (dirent_fields & (SVN_DIRENT_TIME | SVN_DIRENT_LAST_AUTHOR))
                {
                  apr_hash_t *props;
                  svn_string_t *val;

                  SVN_ERR(svn_ra_rev_proplist(ra_session, rev, &props,
                                              pool));
                  val = apr_hash_get(props, SVN_PROP_REVISION_DATE,
                                     APR_HASH_KEY_STRING);
                  if (val)
                    SVN_ERR(svn_time_from_cstring(&dirent->time, val->data,
                                                  pool));
                  else
                    dirent->time = 0;

                  val = apr_hash_get(props, SVN_PROP_REVISION_AUTHOR,
                                     APR_HASH_KEY_STRING);
                  dirent->last_author = val ? val->data : NULL;
                }
            }
        }
      else
        dirent = NULL;
    }
  else if (err)
    return svn_error_trace(err);

  *dirent_p = dirent;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_list2(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_client_list_func_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_revnum_t rev;
  svn_dirent_t *dirent;
  const char *url;
  const char *repos_root;
  const char *fs_path;
  svn_error_t *err;
  apr_hash_t *locks;

  {
    static svn_opt_revision_t head_rev = { svn_opt_revision_head, { 0 } };
    static svn_opt_revision_t work_rev = { svn_opt_revision_working, { 0 } };

    /* Look at the local tree if given a local path.  This is a departure
     * from the semantics svn <= 1.7 which always looked at the repository. */
    if (peg_revision->kind == svn_opt_revision_unspecified)
      peg_revision = svn_path_is_url(path_or_url) ? &head_rev : &work_rev;
    
    /* Operative revision defaults to peg. */
    if (revision->kind == svn_opt_revision_unspecified)
      revision = peg_revision;
  }

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path(&ra_session, &rev,
                                           &url, path_or_url, NULL,
                                           peg_revision,
                                           revision, ctx, pool));

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));

  SVN_ERR(svn_client__path_relative_to_root(&fs_path, ctx->wc_ctx, url,
                                            repos_root, TRUE, ra_session,
                                            pool, pool));

  SVN_ERR(ra_stat_compatible(ra_session, rev, &dirent, dirent_fields,
                             ctx, pool));
  if (! dirent)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("URL '%s' non-existent in revision %ld"),
                             url, rev);

  /* Maybe get all locks under url. */
  if (fetch_locks)
    {
      /* IMPORTANT: If locks are stored in a more temporary pool, we need
         to fix store_dirent below to duplicate the locks. */
      err = svn_ra_get_locks2(ra_session, &locks, "", depth, pool);

      if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
        {
          svn_error_clear(err);
          locks = NULL;
        }
      else if (err)
        return svn_error_trace(err);
    }
  else
    locks = NULL;

  {
    svn_tree_t *tree;
    struct list_baton_t b = { fs_path, dirent_fields, locks,
                              list_func, baton, ctx };

    SVN_ERR(svn_client__open_tree(&tree, path_or_url, revision, peg_revision,
                                  ctx, pool, pool));
    SVN_ERR(svn_tree_walk(tree, "", depth, list_callback, &b,
                          ctx->cancel_func, ctx->cancel_baton, pool));
    return SVN_NO_ERROR;
  }

  return SVN_NO_ERROR;
}
