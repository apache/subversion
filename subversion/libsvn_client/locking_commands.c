/*
 * locking_commands.c:  Implementation of lock and unlock.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_pools.h"

#include "svn_private_config.h"
#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* For use with store_locks_callback, below. */
struct lock_baton
{
  const char *base_path;
  apr_hash_t *urls_to_paths;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
};


/* This callback is called by the ra_layer for each path locked.
 * BATON is a 'struct lock_baton *', PATH is the path being locked,
 * and LOCK is the lock itself.
 *
 * If BATON->base_path is not null, then this function either stores
 * the LOCK on REL_URL or removes any lock tokens from REL_URL
 * (depending on whether DO_LOCK is true or false respectively), but
 * only if RA_ERR is null, or (in the unlock case) is something other
 * than SVN_ERR_FS_LOCK_OWNER_MISMATCH.
 */
static svn_error_t *
store_locks_callback(void *baton,
                     const char *rel_url,
                     svn_boolean_t do_lock,
                     const svn_lock_t *lock,
                     svn_error_t *ra_err, apr_pool_t *pool)
{
  struct lock_baton *lb = baton;
  svn_wc_notify_t *notify;

  /* Create the notify struct first, so we can tweak it below. */
  notify = svn_wc_create_notify(rel_url,
                                do_lock
                                ? (ra_err
                                   ? svn_wc_notify_failed_lock
                                   : svn_wc_notify_locked)
                                : (ra_err
                                   ? svn_wc_notify_failed_unlock
                                   : svn_wc_notify_unlocked),
                                pool);
  notify->lock = lock;
  notify->err = ra_err;

  if (lb->base_path)
    {
      char *path = apr_hash_get(lb->urls_to_paths, rel_url,
                                APR_HASH_KEY_STRING);
      const char *local_abspath;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      svn_dirent_join(lb->base_path,
                                                      path, pool),
                                      pool));

      /* Notify a valid working copy path */
      notify->path = local_abspath;
      notify->path_prefix = lb->base_path;

      if (do_lock)
        {
          if (!ra_err)
            {
              SVN_ERR(svn_wc_add_lock2(lb->ctx->wc_ctx, local_abspath, lock,
                                       lb->pool));
              notify->lock_state = svn_wc_notify_lock_state_locked;
            }
          else
            notify->lock_state = svn_wc_notify_lock_state_unchanged;
        }
      else /* unlocking */
        {
          /* Remove our wc lock token either a) if we got no error, or b) if
             we got any error except for owner mismatch.  Note that the only
             errors that are handed to this callback will be locking-related
             errors. */

          if (!ra_err ||
              (ra_err && (ra_err->apr_err != SVN_ERR_FS_LOCK_OWNER_MISMATCH)))
            {
              SVN_ERR(svn_wc_remove_lock2(lb->ctx->wc_ctx, local_abspath,
                                          lb->pool));
              notify->lock_state = svn_wc_notify_lock_state_unlocked;
            }
          else
            notify->lock_state = svn_wc_notify_lock_state_unchanged;
        }
    }
  else
    notify->url = rel_url; /* Notify that path is actually a url  */

  if (lb->ctx->notify_func2)
    lb->ctx->notify_func2(lb->ctx->notify_baton2, notify, pool);

  return SVN_NO_ERROR;
}


/* Set *COMMON_PARENT_URL to the nearest common parent URL of all TARGETS.
 * If TARGETS are local paths, then the entry for each path is examined
 * and *COMMON_PARENT is set to the common parent URL for all the
 * targets (as opposed to the common local path).
 *
 * If there is no common parent, either because the targets are a
 * mixture of URLs and local paths, or because they simply do not
 * share a common parent, then return SVN_ERR_UNSUPPORTED_FEATURE.
 *
 * DO_LOCK is TRUE for locking TARGETS, and FALSE for unlocking them.
 * FORCE is TRUE for breaking or stealing locks, and FALSE otherwise.
 *
 * Each key stored in *REL_TARGETS_P is a path relative to
 * *COMMON_PARENT.  If TARGETS are local paths, then: if DO_LOCK is
 * true, the value is a pointer to the corresponding base_revision
 * (allocated in POOL) for the path, else the value is the lock token
 * (or "" if no token found in the wc).
 *
 * If TARGETS is an array of urls, REL_FS_PATHS_P is set to NULL.
 * Otherwise each key in REL_FS_PATHS_P is an repository path (relative to
 * COMMON_PARENT) mapped to the target path for TARGET (relative to
 * the common parent WC path). working copy targets that they "belong" to.
 *
 * If *COMMON_PARENT is a URL, then the values are a pointer to
 * SVN_INVALID_REVNUM (allocated in pool) if DO_LOCK, else "".
 *
 * TARGETS may not be empty.
 */
static svn_error_t *
organize_lock_targets(const char **common_parent_url,
                      const char **base_dir,
                      apr_hash_t **rel_targets_p,
                      apr_hash_t **rel_fs_paths_p,
                      const apr_array_header_t *targets,
                      svn_boolean_t do_lock,
                      svn_boolean_t force,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  int i;
  apr_array_header_t *rel_targets = apr_array_make(pool, 1,
                                                   sizeof(const char *));
  apr_hash_t *rel_targets_ret = apr_hash_make(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_boolean_t url_mode;

  SVN_ERR(svn_client__assert_homogeneous_target_type(targets));

  url_mode = ((targets->nelts >= 1) &&
              svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *)));

  /* Get the common parent and all paths */
  if (url_mode)
    {
      SVN_ERR(svn_uri_condense_targets(common_parent_url, &rel_targets,
                                       targets, TRUE, pool, pool));
    }
  else
    {
      SVN_ERR(svn_dirent_condense_targets(common_parent_url, &rel_targets,
                                          targets, TRUE, pool, pool));
    }

  /* svn_path_condense_targets leaves paths empty if TARGETS only had
     1 member, so we special case that. */
  if (apr_is_empty_array(rel_targets))
    {
      const char *parent, *base;
      if (url_mode)
        svn_uri_split(&parent, &base, *common_parent_url, pool);
      else
        svn_dirent_split(&parent, &base, *common_parent_url, pool);

      *common_parent_url = parent;
      APR_ARRAY_PUSH(rel_targets, const char *) = base;
    }

  if (*common_parent_url == NULL || (*common_parent_url)[0] == '\0')
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("No common parent found, unable to operate on disjoint arguments"));

  if (url_mode)
    {
      svn_revnum_t *invalid_revnum;
      invalid_revnum = apr_palloc(pool, sizeof(*invalid_revnum));
      *invalid_revnum = SVN_INVALID_REVNUM;
      *base_dir = NULL;

      for (i = 0; i < rel_targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          apr_hash_set(rel_targets_ret, svn_path_uri_decode(target, pool),
                       APR_HASH_KEY_STRING,
                       do_lock ? (const void *) invalid_revnum
                       : (const void *) "");
        }
      *rel_fs_paths_p = NULL;
    }
  else  /* common parent is a local path */
    {
      apr_array_header_t *rel_urls;
      apr_array_header_t *urls = apr_array_make(pool, 1,
                                                sizeof(const char *));
      apr_hash_t *urls_hash = apr_hash_make(pool);
      const char *common_url;

      *base_dir = *common_parent_url;

      /* Get the url for each target and verify all paths. */
      for (i = 0; i < rel_targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          const char *local_abspath;
          const char *url;

          svn_pool_clear(subpool);

          local_abspath = svn_dirent_join(*common_parent_url, target, subpool);

          SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx, local_abspath,
                                       pool, subpool));

          if (! url)
            return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                     _("'%s' has no URL"),
                                     svn_dirent_local_style(target, pool));

          APR_ARRAY_PUSH(urls, const char *) = url;
        }

      /* Condense our absolute urls and get the relative urls. */
      SVN_ERR(svn_uri_condense_targets(&common_url, &rel_urls, urls,
                                       FALSE, pool, pool));

      /* svn_path_condense_targets leaves paths empty if TARGETS only had
         1 member, so we special case that (again). */
      if (apr_is_empty_array(rel_urls))
        {
          const char *base_name = svn_uri_basename(common_url, pool);
          common_url = svn_uri_dirname(common_url, pool);
          APR_ARRAY_PUSH(rel_urls, const char *) = base_name;
        }

      /* If we have no common URL parent, bail (cross-repos lock attempt) */
      if (common_url == NULL || (common_url)[0] == '\0')
        return svn_error_create
          (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("Unable to lock/unlock across multiple repositories"));

      /* Now that we've got the relative URLs, gather our targets and
         store the mapping between relative repository path and WC path. */
      for (i = 0; i < rel_targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          const char *url = APR_ARRAY_IDX(rel_urls, i, const char *);
          const char *abs_path;
          const char *decoded_url = svn_path_uri_decode(url, pool);

          svn_pool_clear(subpool);

          apr_hash_set(urls_hash, decoded_url,
                       APR_HASH_KEY_STRING,
                       apr_pstrdup(pool, target));

          abs_path = svn_dirent_join(*common_parent_url, target, subpool);

          if (do_lock) /* Lock. */
            {
              svn_revnum_t *revnum;
              revnum = apr_palloc(pool, sizeof(* revnum));
              SVN_ERR(svn_wc__node_get_base_rev(revnum, ctx->wc_ctx,
                                                abs_path, subpool));
              apr_hash_set(rel_targets_ret, decoded_url,
                           APR_HASH_KEY_STRING, revnum);
            }
          else /* Unlock. */
            {
              /* If not force, get the lock token. */
              if (! force)
                {
                  const char *lock_token;

                  SVN_ERR(svn_wc__node_get_lock_info(&lock_token, NULL, NULL,
                                                     NULL, ctx->wc_ctx,
                                                     abs_path, pool, subpool));
                  if (! lock_token)
                    return svn_error_createf
                      (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
                       _("'%s' is not locked in this working copy"), target);

                  apr_hash_set(rel_targets_ret, decoded_url,
                               APR_HASH_KEY_STRING,
                               lock_token);
                }
              else
                {
                  /* If breaking a lock, we shouldn't pass any lock token. */
                  apr_hash_set(rel_targets_ret, decoded_url,
                               APR_HASH_KEY_STRING, "");
                }
            }
        }

      *rel_fs_paths_p = urls_hash;
      *common_parent_url = common_url;
    }

  *rel_targets_p = rel_targets_ret;
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Fetch lock tokens from the repository for the paths in PATH_TOKENS,
   setting the values to the fetched tokens, allocated in pool. */
static svn_error_t *
fetch_tokens(svn_ra_session_t *ra_session, apr_hash_t *path_tokens,
             apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const char *path = svn__apr_hash_index_key(hi);
      svn_lock_t *lock;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_get_lock(ra_session, &lock, path, iterpool));

      if (! lock)
        return svn_error_createf
          (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
           _("'%s' is not locked"), path);

      apr_hash_set(path_tokens, path, APR_HASH_KEY_STRING,
                   apr_pstrdup(pool, lock->token));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_lock(const apr_array_header_t *targets,
                const char *comment,
                svn_boolean_t steal_lock,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  const char *base_dir;
  const char *base_dir_abspath = NULL;
  const char *common_parent_url;
  svn_ra_session_t *ra_session;
  apr_hash_t *path_revs, *urls_to_paths;
  struct lock_baton cb;

  if (apr_is_empty_array(targets))
    return SVN_NO_ERROR;

  /* Enforce that the comment be xml-escapable. */
  if (comment)
    {
      if (! svn_xml_is_xml_safe(comment, strlen(comment)))
        return svn_error_create
          (SVN_ERR_XML_UNESCAPABLE_DATA, NULL,
           _("Lock comment contains illegal characters"));
    }

  SVN_ERR(organize_lock_targets(&common_parent_url, &base_dir, &path_revs,
                                &urls_to_paths, targets, TRUE, steal_lock,
                                ctx, pool));

  /* Open an RA session to the common parent of TARGETS. */
  if (base_dir)
    SVN_ERR(svn_dirent_get_absolute(&base_dir_abspath, base_dir, pool));
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL,
                                               common_parent_url, base_dir,
                                               NULL, FALSE, FALSE,
                                               ctx, pool));

  cb.base_path = base_dir;
  cb.urls_to_paths = urls_to_paths;
  cb.ctx = ctx;
  cb.pool = pool;

  /* Lock the paths. */
  SVN_ERR(svn_ra_lock(ra_session, path_revs, comment,
                      steal_lock, store_locks_callback, &cb, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unlock(const apr_array_header_t *targets,
                  svn_boolean_t break_lock,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *base_dir;
  const char *base_dir_abspath = NULL;
  const char *common_parent_url;
  svn_ra_session_t *ra_session;
  apr_hash_t *path_tokens, *urls_to_paths;
  struct lock_baton cb;

  if (apr_is_empty_array(targets))
    return SVN_NO_ERROR;

  SVN_ERR(organize_lock_targets(&common_parent_url, &base_dir, &path_tokens,
                                &urls_to_paths, targets, FALSE, break_lock,
                                ctx, pool));

  /* Open an RA session. */
  if (base_dir)
    SVN_ERR(svn_dirent_get_absolute(&base_dir_abspath, base_dir, pool));
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL,
                                               common_parent_url,
                                               base_dir_abspath, NULL, FALSE,
                                               FALSE, ctx, pool));

  /* If break_lock is not set, lock tokens are required by the server.
     If the targets were all URLs, ensure that we provide lock tokens,
     so the repository will only check that the user owns the
     locks. */
  if (! base_dir && !break_lock)
    SVN_ERR(fetch_tokens(ra_session, path_tokens, pool));

  cb.base_path = base_dir;
  cb.urls_to_paths = urls_to_paths;
  cb.ctx = ctx;
  cb.pool = pool;

  /* Unlock the paths. */
  SVN_ERR(svn_ra_unlock(ra_session, path_tokens, break_lock,
                        store_locks_callback, &cb, pool));

  return SVN_NO_ERROR;
}

