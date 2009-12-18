/*
 * locking_commands.c:  Implementation of lock and unlock.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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



/*** Includes. ***/

#include "svn_client.h"
#include "client.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_pools.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* For use with store_locks_callback, below. */
struct lock_baton
{
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *urls_to_paths;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
};


/* This callback is called by the ra_layer for each path locked.
 * BATON is a 'struct lock_baton *', PATH is the path being locked,
 * and LOCK is the lock itself.
 *
 * If BATON->adm_access is not null, then this function either stores
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
  svn_wc_adm_access_t *adm_access;
  const char *abs_path;
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

  if (lb->adm_access)
    {
      const char *base_path = svn_wc_adm_access_path(lb->adm_access);
      char *path = apr_hash_get(lb->urls_to_paths, rel_url,
                                APR_HASH_KEY_STRING);
      abs_path = svn_path_join(base_path, path, lb->pool);

      SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, lb->adm_access,
                                        abs_path, lb->pool));

      /* Notify a valid working copy path */
      notify->path = abs_path;
      notify->path_prefix = base_path;

      if (do_lock)
        {
          if (!ra_err)
            {
              SVN_ERR(svn_wc_add_lock(abs_path, lock, adm_access, lb->pool));
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
              SVN_ERR(svn_wc_remove_lock(abs_path, adm_access, lb->pool));
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


/* Set *COMMON_PARENT to the nearest common parent of all TARGETS.  If
 * TARGETS are local paths, then the entry for each path is examined
 * and *COMMON_PARENT is set to the common parent URL for all the
 * targets (as opposed to the common local path).
 *
 * If all the targets are local paths within the same wc, i.e., they
 * share a common parent at some level, set *PARENT_ADM_ACCESS_P
 * to the adm_access of that common parent.  *PARENT_ADM_ACCESS_P will
 * be associated with adm_access objects for all the other paths,
 * which are locked in the working copy while we lock them in the
 * repository.
 *
 * If all the targets are URLs in the same repository, i.e. sharing a
 * common parent URL prefix, then set *PARENT_ADM_ACCESS_P to null.
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
organize_lock_targets(const char **common_parent,
                      svn_wc_adm_access_t **parent_adm_access_p,
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

  /* Get the common parent and all relative paths */
  SVN_ERR(svn_path_condense_targets(common_parent, &rel_targets, targets,
                                    FALSE, pool));

  /* svn_path_condense_targets leaves paths empty if TARGETS only had
     1 member, so we special case that. */
  if (apr_is_empty_array(rel_targets))
    {
      char *base_name = svn_path_basename(*common_parent, pool);
      *common_parent = svn_path_dirname(*common_parent, pool);

      APR_ARRAY_PUSH(rel_targets, char *) = base_name;
    }

  if (*common_parent == NULL || (*common_parent)[0] == '\0')
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("No common parent found, unable to operate on disjoint arguments"));

  if (svn_path_is_url(*common_parent))
    {
      svn_revnum_t *invalid_revnum;
      invalid_revnum = apr_palloc(pool, sizeof(*invalid_revnum));
      *invalid_revnum = SVN_INVALID_REVNUM;
      *parent_adm_access_p = NULL;

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
      int max_levels_to_lock = 0;
      apr_array_header_t *rel_urls;
      apr_array_header_t *urls = apr_array_make(pool, 1,
                                                sizeof(const char *));
      apr_hash_t *urls_hash = apr_hash_make(pool);
      const char *common_url;

      /* Calculate the maximum number of components in the rel_targets, which
         is the depth to which we need to lock the WC. */
      for (i = 0; i < rel_targets->nelts; ++i)
        {
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          int n = svn_path_component_count(target);

          if (n > max_levels_to_lock)
            max_levels_to_lock = n;
        }

      SVN_ERR(svn_wc_adm_probe_open3(parent_adm_access_p, NULL,
                                     *common_parent,
                                     TRUE, max_levels_to_lock,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));

      /* Get the url for each target and verify all paths. */
      for (i = 0; i < rel_targets->nelts; i++)
        {
          const svn_wc_entry_t *entry;
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          const char *abs_path;

          svn_pool_clear(subpool);

          abs_path = svn_path_join
            (svn_wc_adm_access_path(*parent_adm_access_p), target, subpool);

          SVN_ERR(svn_wc__entry_versioned(&entry, abs_path,
                                         *parent_adm_access_p, FALSE, subpool));

          if (! entry->url)
            return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                     _("'%s' has no URL"),
                                     svn_path_local_style(target, pool));

          APR_ARRAY_PUSH(urls, const char *) = apr_pstrdup(pool,
                                                           entry->url);
        }

      /* Condense our absolute urls and get the relative urls. */
      SVN_ERR(svn_path_condense_targets(&common_url, &rel_urls, urls,
                                        FALSE, pool));

      /* svn_path_condense_targets leaves paths empty if TARGETS only had
         1 member, so we special case that (again). */
      if (apr_is_empty_array(rel_urls))
        {
          char *base_name = svn_path_basename(common_url, pool);
          common_url = svn_path_dirname(common_url, pool);
          APR_ARRAY_PUSH(rel_urls, char *) = base_name;
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
          const svn_wc_entry_t *entry;
          const char *target = APR_ARRAY_IDX(rel_targets, i, const char *);
          const char *url = APR_ARRAY_IDX(rel_urls, i, const char *);
          const char *abs_path;
          const char *decoded_url = svn_path_uri_decode(url, pool);

          svn_pool_clear(subpool);

          apr_hash_set(urls_hash, decoded_url,
                       APR_HASH_KEY_STRING,
                       apr_pstrdup(pool, target));

          abs_path = svn_path_join
            (svn_wc_adm_access_path(*parent_adm_access_p), target, subpool);

          SVN_ERR(svn_wc_entry(&entry, abs_path, *parent_adm_access_p, FALSE,
                               subpool));

          if (do_lock) /* Lock. */
            {
              svn_revnum_t *revnum;
              revnum = apr_palloc(pool, sizeof(* revnum));
              *revnum = entry->revision;

              apr_hash_set(rel_targets_ret, decoded_url,
                           APR_HASH_KEY_STRING, revnum);
            }
          else /* Unlock. */
            {
              /* If not force, get the lock token from the WC entry. */
              if (! force)
                {
                  if (! entry->lock_token)
                    return svn_error_createf
                      (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
                       _("'%s' is not locked in this working copy"), target);

                  apr_hash_set(rel_targets_ret, decoded_url,
                               APR_HASH_KEY_STRING,
                               apr_pstrdup(pool, entry->lock_token));
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
      *common_parent = common_url;
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
      const void *key;
      const char *path;
      svn_lock_t *lock;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, NULL);
      path = key;

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
  svn_wc_adm_access_t *adm_access;
  const char *common_parent;
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

  SVN_ERR(organize_lock_targets(&common_parent, &adm_access,
                                &path_revs, &urls_to_paths, targets, TRUE,
                                steal_lock, ctx, pool));

  /* Open an RA session to the common parent of TARGETS. */
  SVN_ERR(svn_client__open_ra_session_internal
          (&ra_session, common_parent,
           adm_access ? svn_wc_adm_access_path(adm_access) : NULL,
           adm_access, NULL, FALSE, FALSE, ctx, pool));

  cb.pool = pool;
  cb.adm_access = adm_access;
  cb.urls_to_paths = urls_to_paths;
  cb.ctx = ctx;

  /* Lock the paths. */
  SVN_ERR(svn_ra_lock(ra_session, path_revs, comment,
                      steal_lock, store_locks_callback, &cb, pool));

  /* Unlock the wc. */
  if (adm_access)
    return svn_wc_adm_close2(adm_access, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unlock(const apr_array_header_t *targets,
                  svn_boolean_t break_lock,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *common_parent;
  svn_ra_session_t *ra_session;
  apr_hash_t *path_tokens, *urls_to_paths;
  struct lock_baton cb;

  if (apr_is_empty_array(targets))
    return SVN_NO_ERROR;

  SVN_ERR(organize_lock_targets(&common_parent, &adm_access,
                                &path_tokens, &urls_to_paths, targets,
                                FALSE, break_lock, ctx, pool));

  /* Open an RA session. */
  SVN_ERR(svn_client__open_ra_session_internal
          (&ra_session, common_parent,
           adm_access ? svn_wc_adm_access_path(adm_access) : NULL,
           adm_access, NULL, FALSE, FALSE, ctx, pool));

  /* If break_lock is not set, lock tokens are required by the server.
     If the targets were all URLs, ensure that we provide lock tokens,
     so the repository will only check that the user owns the
     locks. */
  if (! adm_access && !break_lock)
    SVN_ERR(fetch_tokens(ra_session, path_tokens, pool));

  cb.pool = pool;
  cb.adm_access = adm_access;
  cb.urls_to_paths = urls_to_paths;
  cb.ctx = ctx;

  /* Unlock the paths. */
  SVN_ERR(svn_ra_unlock(ra_session, path_tokens, break_lock,
                        store_locks_callback, &cb, pool));

  /* Unlock the wc. */
  if (adm_access)
    return svn_wc_adm_close2(adm_access, pool);

  return SVN_NO_ERROR;
}

