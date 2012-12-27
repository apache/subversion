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

#include "private/svn_fspath.h"
#include "private/svn_ra_private.h"
#include "svn_private_config.h"

/* Get the directory entries of DIR at REV (relative to the root of
   RA_SESSION), getting at least the fields specified by DIRENT_FIELDS.
   Use the cancellation function/baton of CTX to check for cancellation.

   If DEPTH is svn_depth_empty, return immediately.  If DEPTH is
   svn_depth_files, invoke LIST_FUNC on the file entries with BATON;
   if svn_depth_immediates, invoke it on file and directory entries;
   if svn_depth_infinity, invoke it on file and directory entries and
   recurse into the directory entries with the same depth.

   LOCKS, if non-NULL, is a hash mapping const char * paths to svn_lock_t
   objects and FS_PATH is the absolute filesystem path of the RA session.
   Use SCRATCH_POOL for temporary allocations.

   If the caller passes EXTERNALS as non-NULL, populate the EXTERNALS 
   hash table whose keys are URLs of the directory which has externals
   definitions, and whose values are the externals description text. 
   Allocate the hash's keys and values in RESULT_POOL.

   EXTERNAL_PARENT_URL and EXTERNAL_TARGET are set when external items
   are listed, otherwise both are set to NULL by the caller.
*/
static svn_error_t *
get_dir_contents(apr_uint32_t dirent_fields,
                 const char *dir,
                 svn_revnum_t rev,
                 svn_ra_session_t *ra_session,
                 apr_hash_t *locks,
                 const char *fs_path,
                 svn_depth_t depth,
                 svn_client_ctx_t *ctx,
                 apr_hash_t *externals,
                 const char *external_parent_url,
                 const char *external_target,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_hash_t *tmpdirents;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *array;
  svn_error_t *err;
  apr_hash_t *prop_hash = NULL;
  const svn_string_t *prop_val = NULL;
  int i;

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;
  
  /* Get the directory's entries. If externals hash is non-NULL, get its
     properties also. Ignore any not-authorized errors.  */
  err = svn_ra_get_dir2(ra_session, &tmpdirents, NULL, 
                        externals ? &prop_hash : NULL,
                        dir, rev, dirent_fields, scratch_pool);
      
  if (err && ((err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED) ||
              (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN)))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);
 
 /* Filter out svn:externals from all properties hash. */ 
  if (prop_hash) 
    prop_val = apr_hash_get(prop_hash, SVN_PROP_EXTERNALS, 
                            APR_HASH_KEY_STRING);
  if (prop_val)
    {
      const char *url;

      SVN_ERR(svn_ra_get_session_url(ra_session, &url, scratch_pool));
      
      apr_hash_set(externals, svn_path_url_add_component2(url, dir, 
                                                          result_pool),
                   APR_HASH_KEY_STRING, svn_string_dup(prop_val, 
                                                       result_pool));
    }

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  /* Sort the hash, so we can call the callback in a "deterministic" order. */
  array = svn_sort__hash(tmpdirents, svn_sort_compare_items_lexically, 
                         scratch_pool);
  for (i = 0; i < array->nelts; ++i)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(array, i, svn_sort__item_t);
      const char *path;
      svn_dirent_t *the_ent = item->value;
      svn_lock_t *lock;

      svn_pool_clear(iterpool);

      path = svn_relpath_join(dir, item->key, iterpool);

      if (locks)
        {
          const char *abs_path = svn_fspath__join(fs_path, path, iterpool);
          lock = apr_hash_get(locks, abs_path, APR_HASH_KEY_STRING);
        }
      else
        lock = NULL;

      if (the_ent->kind == svn_node_file
          || depth == svn_depth_immediates
          || depth == svn_depth_infinity)
        SVN_ERR(list_func(baton, path, the_ent, lock, fs_path,
                          external_parent_url, external_target, iterpool));
      
      /* If externals is non-NULL, populate the externals hash table 
         recursively for all directory entries. */
      if (depth == svn_depth_infinity && the_ent->kind == svn_node_dir)
        SVN_ERR(get_dir_contents(dirent_fields, path, rev,
                                 ra_session, locks, fs_path, depth, ctx, 
                                 externals, external_parent_url,
                                 external_target, list_func, baton,
                                 result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Like svn_ra_stat() but with a compatibility hack for pre-1.2 svnserve. */
/* ### Maybe we should move this behavior into the svn_ra_stat wrapper? */
svn_error_t *
svn_client__ra_stat_compatible(svn_ra_session_t *ra_session,
                               svn_revnum_t rev,
                               svn_dirent_t **dirent_p,
                               apr_uint32_t dirent_fields,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_ra_stat(ra_session, "", rev, dirent_p, pool);

  /* svnserve before 1.2 doesn't support the above, so fall back on
     a less efficient method. */
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      const char *repos_root_url;
      const char *session_url;
      svn_node_kind_t kind;
      svn_dirent_t *dirent;

      svn_error_clear(err);

      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url, pool));
      SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));

      SVN_ERR(svn_ra_check_path(ra_session, "", rev, &kind, pool));

      if (kind != svn_node_none)
        {
          if (strcmp(session_url, repos_root_url) != 0)
            {
              svn_ra_session_t *parent_session;
              apr_hash_t *parent_ents;
              const char *parent_url, *base_name;
              apr_pool_t *subpool = svn_pool_create(pool);

              /* Open another session to the path's parent.  This server
                 doesn't support svn_ra_reparent anyway, so don't try it. */
              svn_uri_split(&parent_url, &base_name, session_url, subpool);

              SVN_ERR(svn_client__open_ra_session_internal(&parent_session,
                                                           NULL, parent_url,
                                                           NULL, NULL, FALSE,
                                                           TRUE, ctx, subpool));

              /* Get all parent's entries, no props. */
              SVN_ERR(svn_ra_get_dir2(parent_session, &parent_ents, NULL,
                                      NULL, "", rev, dirent_fields, subpool));

              /* Get the relevant entry. */
              dirent = apr_hash_get(parent_ents, base_name,
                                    APR_HASH_KEY_STRING);

              if (dirent)
                *dirent_p = svn_dirent_dup(dirent, pool);
              else
                *dirent_p = NULL;

              svn_pool_destroy(subpool); /* Close RA session */
            }
          else
            {
              /* We can't get the directory entry for the repository root,
                 but we can still get the information we want.
                 The created-rev of the repository root must, by definition,
                 be rev. */
              dirent = apr_palloc(pool, sizeof(*dirent));
              dirent->kind = kind;
              dirent->size = SVN_INVALID_FILESIZE;
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

              *dirent_p = dirent;
            }
        }
      else
        *dirent_p = NULL;
    }
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__list_internal(const char *path_or_url,
                          const svn_opt_revision_t *peg_revision,
                          const svn_opt_revision_t *revision,
                          svn_depth_t depth,
                          apr_uint32_t dirent_fields,
                          svn_boolean_t fetch_locks,
                          svn_boolean_t include_externals,
                          const char *external_parent_url,
                          const char *external_target,
                          svn_client_list_func2_t list_func,
                          void *baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *loc;
  svn_dirent_t *dirent;
  const char *fs_path;
  svn_error_t *err;
  apr_hash_t *locks;
  apr_hash_t *externals;

  if (include_externals)
    externals = apr_hash_make(pool);
  else
    externals = NULL;

  /* We use the kind field to determine if we should recurse, so we
     always need it. */
  dirent_fields |= SVN_DIRENT_KIND;

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            path_or_url, NULL,
                                            peg_revision,
                                            revision, ctx, pool));

  fs_path = svn_client__pathrev_fspath(loc, pool);

  SVN_ERR(svn_client__ra_stat_compatible(ra_session, loc->rev, &dirent,
                                         dirent_fields, ctx, pool));
  if (! dirent)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("URL '%s' non-existent in revision %ld"),
                             loc->url, loc->rev);

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

  /* Report the dirent for the target. */
  SVN_ERR(list_func(baton, "", dirent, locks
                    ? (apr_hash_get(locks, fs_path,
                                    APR_HASH_KEY_STRING))
                    : NULL, fs_path, external_parent_url, 
                    external_target, pool));

  if (dirent->kind == svn_node_dir
      && (depth == svn_depth_files
          || depth == svn_depth_immediates
          || depth == svn_depth_infinity))
    SVN_ERR(get_dir_contents(dirent_fields, "", loc->rev, ra_session, locks,
                             fs_path, depth, ctx, externals, 
                             external_parent_url, external_target, list_func,
                             baton, pool, pool));
  
  /* We handle externals after listing entries under path_or_url, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation. */
  if (include_externals && apr_hash_count(externals))
    {
      /* The 'externals' hash populated by get_dir_contents() is processed 
         here. */
      SVN_ERR(svn_client__list_externals(externals, depth, dirent_fields, 
                                         fetch_locks, list_func, baton,
                                         ctx, pool));
    } 
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_list3(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_boolean_t include_externals,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{

  return svn_error_trace(svn_client__list_internal(path_or_url, peg_revision,
                                                   revision, 
                                                   depth, dirent_fields, 
                                                   fetch_locks, 
                                                   include_externals, 
                                                   NULL, NULL, list_func,
                                                   baton, ctx, pool));
}
