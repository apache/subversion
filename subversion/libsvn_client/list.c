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

#include <apr_fnmatch.h>

#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_tree.h"

#include "client.h"

#include "private/svn_fspath.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_utf_private.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"

/* Prototypes for referencing before declaration */
static svn_error_t *
list_externals(apr_hash_t *externals,
               const apr_array_header_t *patterns,
               svn_depth_t depth,
               apr_uint32_t dirent_fields,
               svn_boolean_t fetch_locks,
               svn_client_list_func2_t list_func,
               void *baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool);

/* Return TRUE if S matches any of the const char * in PATTERNS.
 * Note that any S will match if PATTERNS is empty.
 * Use SCRATCH_BUFFER for temporary string contents. */
static svn_boolean_t
match_patterns(const char *s,
               const apr_array_header_t *patterns,
               svn_membuf_t *scratch_buffer)
{
  return patterns
       ? svn_utf__fuzzy_glob_match(s, patterns, scratch_buffer)
       : TRUE;
}

/* Baton type to be used with list_receiver. */
typedef struct receiver_baton_t
{
  /* Wrapped callback function to invoke. */
  svn_client_list_func2_t list_func;

  /* Baton to be used with LIST_FUNC. */
  void *list_baton;

  /* Client context providing cancellation support. */
  svn_client_ctx_t *ctx;

  /* All locks found for the whole tree; pick yours. */
  apr_hash_t *locks;

  /* Start path of the operation. */
  const char *fs_base_path;
} receiver_baton_t;

/* Implement svn_ra_dirent_receiver_t.
   The BATON type must be a receiver_baton_t. */
static svn_error_t *
list_receiver(const char *rel_path,
              svn_dirent_t *dirent,
              void *baton,
              apr_pool_t *pool)
{
  receiver_baton_t *b = baton;
  const svn_lock_t *lock = NULL;

  /* We only report the path relative to the start path. */
  rel_path = svn_dirent_skip_ancestor(b->fs_base_path, rel_path);

  if (b->locks)
    {
      const char *abs_path = svn_dirent_join(b->fs_base_path, rel_path, pool);
      lock = svn_hash_gets(b->locks, abs_path);
    }

  if (b->ctx->cancel_func)
    SVN_ERR(b->ctx->cancel_func(b->ctx->cancel_baton));

  SVN_ERR(b->list_func(b->list_baton, rel_path, dirent, lock,
                       b->fs_base_path, NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* Info for list_callback(). */
struct list_baton_t
{
  /* The absolute filesystem path of the RA session. */
  const char *fs_path;
  apr_uint32_t dirent_fields;
  /* If non-NULL, maps (const char *) paths to svn_lock_t objects. */
  apr_hash_t *locks;
  svn_client_list_func2_t list_func;
  void *list_baton;
  const apr_array_header_t *patterns;
  svn_membuf_t *scratch_buffer;

  const char *session_root_url;
  apr_hash_t *externals;
  const char *external_parent_url;
  const char *external_target;

  svn_boolean_t any_node_found;
};

/* List the entries in DIR_NODE.
 * An svn_tree_walk_dir_callback function.
 */
static svn_error_t *list_callback(svn_tree_node_t *node,
                                  void *baton,
                                  apr_pool_t *scratch_pool)
{
  struct list_baton_t *lb = baton;
  const char *relpath;
  const char *basename;
  svn_dirent_t *dirent;

  lb->any_node_found = TRUE;

  SVN_ERR(svn_tree_node_get_relpath(node, &relpath, scratch_pool, scratch_pool));
  basename = svn_relpath_basename(relpath, scratch_pool);
  SVN_ERR(svn_tree_node_get_dirent(node, &dirent, scratch_pool, scratch_pool));

  if (match_patterns(basename, lb->patterns, lb->scratch_buffer))
    {
      svn_lock_t *lock = NULL;

      if (lb->locks)
        {
          const char *abs_fspath = svn_fspath__join(lb->fs_path, relpath,
                                                    scratch_pool);
          lock = apr_hash_get(lb->locks, abs_fspath, APR_HASH_KEY_STRING);
        }

      SVN_ERR(lb->list_func(lb->list_baton, relpath, dirent, lock,
                            lb->fs_path,
                            lb->external_parent_url, lb->external_target,
                            scratch_pool));
    }

  /* Collect any 'externals' definitions if this is a directory */
  if (lb->externals && dirent->kind == svn_node_dir)
    {
      apr_hash_t *prop_hash = NULL;
      const svn_string_t *prop_val = NULL;

      SVN_ERR(svn_tree_node_read_dir(node, NULL /*children*/, &prop_hash,
                                     scratch_pool, scratch_pool));
      prop_val = svn_hash_gets(prop_hash, SVN_PROP_EXTERNALS);
      if (prop_val)
        {
          apr_pool_t *pool = apr_hash_pool_get(lb->externals);

          svn_hash_sets(lb->externals,
                        svn_path_url_add_component2(lb->session_root_url,
                                                    relpath, pool),
                        svn_string_dup(prop_val, pool));
        }
    }

  return SVN_NO_ERROR;
}

/* List the file/directory entries for PATH_OR_URL at REVISION.
   The actual node revision selected is determined by the path as
   it exists in PEG_REVISION.

   If DEPTH is svn_depth_infinity, then list all file and directory entries
   recursively.  Else if DEPTH is svn_depth_files, list all files under
   PATH_OR_URL (if any), but not subdirectories.  Else if DEPTH is
   svn_depth_immediates, list all files and include immediate
   subdirectories (at svn_depth_empty).  Else if DEPTH is
   svn_depth_empty, just list PATH_OR_URL with none of its entries.

   If PATTERNS is not NULL, the last path segments must match at least
   one of const char * patterns in it or the respective dirent will not
   be reported.

   DIRENT_FIELDS controls which fields in the svn_dirent_t's are
   filled in.  To have them totally filled in use SVN_DIRENT_ALL,
   otherwise simply bitwise OR together the combination of SVN_DIRENT_*
   fields you care about.

   If FETCH_LOCKS is TRUE, include locks when reporting directory entries.

   If INCLUDE_EXTERNALS is TRUE, also list all external items
   reached by recursion.  DEPTH value passed to the original list target
   applies for the externals also.  EXTERNAL_PARENT_URL is url of the
   directory which has the externals definitions.  EXTERNAL_TARGET is the
   target subdirectory of externals definitions.

   Report directory entries by invoking LIST_FUNC/BATON.
   Pass EXTERNAL_PARENT_URL and EXTERNAL_TARGET to LIST_FUNC when external
   items are listed, otherwise both are set to NULL.

   Use authentication baton cached in CTX to authenticate against the
   repository.

   Use POOL for all allocations.
*/
static svn_error_t *
list_internal(const char *path_or_url,
              const svn_opt_revision_t *peg_revision,
              const svn_opt_revision_t *revision,
              const apr_array_header_t *patterns,
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
  const char *fs_path;
  svn_error_t *err;
  apr_hash_t *locks;
  apr_hash_t *externals;
  svn_membuf_t scratch_buffer;

  if (include_externals)
    externals = apr_hash_make(pool);
  else
    externals = NULL;

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            path_or_url, NULL,
                                            peg_revision,
                                            revision, ctx, pool));

  fs_path = svn_client__pathrev_fspath(loc, pool);

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

  /* Try to use the efficient and fully authz-filtered code path. */
  if (!include_externals)
    {
      receiver_baton_t receiver_baton;
      receiver_baton.list_baton = baton;
      receiver_baton.ctx = ctx;
      receiver_baton.list_func = list_func;
      receiver_baton.locks = locks;
      receiver_baton.fs_base_path = fs_path;

      err = svn_ra_list(ra_session, "", loc->rev, patterns, depth,
                        dirent_fields, list_receiver, &receiver_baton, pool);

      if (svn_error_find_cause(err, SVN_ERR_UNSUPPORTED_FEATURE))
        svn_error_clear(err);
      else
        return svn_error_trace(err);
    }

  /* We need a scratch buffer for temporary string data.
   * Create one with a reasonable initial size. */
  svn_membuf__create(&scratch_buffer, 256, pool);

  /* Walk the tree */
  {
    struct list_baton_t lb;
    svn_tree_t *tree;

    lb.fs_path = fs_path;
    lb.dirent_fields = dirent_fields;
    lb.locks = locks;
    lb.list_func = list_func;
    lb.list_baton = baton;
    lb.patterns = patterns;
    lb.scratch_buffer = &scratch_buffer;
    lb.session_root_url = loc->url;
    lb.externals = externals;
    lb.external_parent_url = external_parent_url;
    lb.external_target = external_target;
    lb.any_node_found = FALSE;

    SVN_ERR(svn_ra__open_tree(&tree, ra_session, loc->rev, pool));

    SVN_ERR(svn_tree_walk(tree, depth, list_callback, &lb,
                          ctx->cancel_func, ctx->cancel_baton, pool));
    if (!lb.any_node_found)
      return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               _("URL '%s' non-existent in revision %ld"),
                               loc->url, loc->rev);
  }

  /* We handle externals after listing entries under path_or_url, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation. */
  if (include_externals && apr_hash_count(externals))
    {
      /* The 'externals' hash populated by get_dir_contents() is processed
         here. */
      SVN_ERR(list_externals(externals, patterns, depth, dirent_fields,
                             fetch_locks, list_func, baton,
                             ctx, pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_list_error(const svn_client_ctx_t *ctx,
                const char *target_abspath,
                svn_error_t *err,
                apr_pool_t *scratch_pool)
{
  if (err && err->apr_err != SVN_ERR_CANCELLED)
    {
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notifier = svn_wc_create_notify(
                                            target_abspath,
                                            svn_wc_notify_failed_external,
                                            scratch_pool);
          notifier->err = err;
          ctx->notify_func2(ctx->notify_baton2, notifier, scratch_pool);
        }
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return err;
}


/* Walk through all the external items and list them. */
static svn_error_t *
list_external_items(apr_array_header_t *external_items,
                    const char *externals_parent_url,
                    const apr_array_header_t *patterns,
                    svn_depth_t depth,
                    apr_uint32_t dirent_fields,
                    svn_boolean_t fetch_locks,
                    svn_client_list_func2_t list_func,
                    void *baton,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  const char *externals_parent_repos_root_url;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(svn_client_get_repos_root(&externals_parent_repos_root_url,
                                    NULL /* uuid */,
                                    externals_parent_url, ctx,
                                    scratch_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < external_items->nelts; i++)
    {
      const char *resolved_url;

      svn_wc_external_item2_t *item =
          APR_ARRAY_IDX(external_items, i, svn_wc_external_item2_t *);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__resolve_relative_external_url(
                  &resolved_url,
                  item,
                  externals_parent_repos_root_url,
                  externals_parent_url,
                  iterpool, iterpool));

      /* List the external */
      SVN_ERR(wrap_list_error(ctx, item->target_dir,
                              list_internal(resolved_url,
                                            &item->peg_revision,
                                            &item->revision,
                                            patterns,
                                            depth, dirent_fields,
                                            fetch_locks,
                                            TRUE,
                                            externals_parent_url,
                                            item->target_dir,
                                            list_func, baton, ctx,
                                            iterpool),
                              iterpool));

    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* List external items defined on each external in EXTERNALS, a const char *
   externals_parent_url(url of the directory which has the externals
   definitions) of all externals mapping to the svn_string_t * externals_desc
   (externals description text). All other options are the same as those
   passed to svn_client_list(). */
static svn_error_t *
list_externals(apr_hash_t *externals,
               const apr_array_header_t *patterns,
               svn_depth_t depth,
               apr_uint32_t dirent_fields,
               svn_boolean_t fetch_locks,
               svn_client_list_func2_t list_func,
               void *baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *externals_parent_url = apr_hash_this_key(hi);
      svn_string_t *externals_desc = apr_hash_this_val(hi);
      apr_array_header_t *external_items;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc_parse_externals_description3(&external_items,
                                                  externals_parent_url,
                                                  externals_desc->data,
                                                  FALSE, iterpool));

      if (! external_items->nelts)
        continue;

      SVN_ERR(list_external_items(external_items, externals_parent_url,
                                  patterns, depth, dirent_fields,
                                  fetch_locks, list_func, baton, ctx,
                                  iterpool));

    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_list4(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 const apr_array_header_t *patterns,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_boolean_t include_externals,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{

  return svn_error_trace(list_internal(path_or_url, peg_revision,
                                       revision, patterns,
                                       depth, dirent_fields,
                                       fetch_locks,
                                       include_externals,
                                       NULL, NULL, list_func,
                                       baton, ctx, scratch_pool));
}
