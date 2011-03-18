/*
 * externals.c:  handle the svn:externals property
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

#include <apr_uri.h>
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_config.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/* Closure for handle_external_item_change. */
struct handle_external_item_change_baton
{
  /* As returned by svn_wc_parse_externals_description(). */
  apr_hash_t *new_desc;
  apr_hash_t *old_desc;

  /* The directory that has this externals property. */
  const char *parent_dir_abspath;

  /* The URL for the directory that has this externals property. */
  const char *parent_dir_url;

  /* The URL for the repository root. */
  const char *repos_root_url;

  /* Passed through to svn_client_* functions. */
  svn_client_ctx_t *ctx;

  /* Passed to svn_client_exportX() */
  const char *native_eol;

  svn_boolean_t *timestamp_sleep;
  svn_boolean_t is_export;

  /* A long lived pool.  Put anything in here that needs to outlive
     the hash diffing callback, such as updates to the hash
     entries. */
  apr_pool_t *pool;

  /* A scratchwork pool -- do not put anything in here that needs to
     outlive the hash diffing callback! */
  apr_pool_t *iter_pool;
};


struct relegate_dir_external_with_write_lock_baton {
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

/* Note: All arguments are in the baton above.
 *
 * Remove the directory at LOCAL_ABSPATH from revision control, and do the
 * same to any revision controlled directories underneath LOCAL_ABSPATH
 * (including directories not referred to by parent svn administrative areas);
 * then if LOCAL_ABSPATH is empty afterwards, remove it, else rename it to a
 * unique name in the same parent directory.
 *
 * Pass CANCEL_FUNC, CANCEL_BATON to svn_wc_remove_from_revision_control.
 *
 * Use SCRATCH_POOL for all temporary allocation.
 */
static svn_error_t *
relegate_dir_external(void *baton,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  struct relegate_dir_external_with_write_lock_baton *b = baton;
  svn_error_t *err = SVN_NO_ERROR;

  err = svn_wc_remove_from_revision_control2(b->wc_ctx, b->local_abspath,
                                             TRUE, FALSE,
                                             b->cancel_func, b->cancel_baton,
                                             scratch_pool);
  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    {
      const char *parent_dir;
      const char *dirname;
      const char *new_path;

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      svn_dirent_split(&parent_dir, &dirname, b->local_abspath, scratch_pool);

      /* Reserve the new dir name. */
      SVN_ERR(svn_io_open_uniquely_named(NULL, &new_path,
                                         parent_dir, dirname, ".OLD",
                                         svn_io_file_del_none,
                                         scratch_pool, scratch_pool));

      /* Sigh...  We must fall ever so slightly from grace.

         Ideally, there would be no window, however brief, when we
         don't have a reservation on the new name.  Unfortunately,
         at least in the Unix (Linux?) version of apr_file_rename(),
         you can't rename a directory over a file, because it's just
         calling stdio rename(), which says:

            ENOTDIR
              A  component used as a directory in oldpath or newpath
              path is not, in fact, a directory.  Or, oldpath  is
              a directory, and newpath exists but is not a directory

         So instead, we get the name, then remove the file (ugh), then
         rename the directory, hoping that nobody has gotten that name
         in the meantime -- which would never happen in real life, so
         no big deal.
      */
      /* Do our best, but no biggy if it fails. The rename will fail. */
      svn_error_clear(svn_io_remove_file2(new_path, TRUE, scratch_pool));

      /* Rename. */
      SVN_ERR(svn_io_file_rename(b->local_abspath, new_path, scratch_pool));
    }

  return svn_error_return(err);
}

/* Try to update a directory external at PATH to URL at REVISION.
   Use POOL for temporary allocations, and use the client context CTX. */
static svn_error_t *
switch_dir_external(const char *path,
                    const char *url,
                    const svn_opt_revision_t *revision,
                    const svn_opt_revision_t *peg_revision,
                    svn_boolean_t *timestamp_sleep,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* If path is a directory, try to update/switch to the correct URL
     and revision. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      const char *node_url;

      /* Doubles as an "is versioned" check. */
      err = svn_wc__node_get_url(&node_url, ctx->wc_ctx, local_abspath,
                                 pool, subpool);
      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          goto relegate;
        }
      else if (err)
        return svn_error_return(err);

      if (node_url)
        {
          const char *repos_root_url;

          /* If we have what appears to be a version controlled
             subdir, and its top-level URL matches that of our
             externals definition, perform an update. */
          if (strcmp(node_url, url) == 0)
            {
              SVN_ERR(svn_client__update_internal(NULL, local_abspath,
                                                  revision, svn_depth_unknown,
                                                  FALSE, FALSE, FALSE,
                                                  timestamp_sleep, TRUE, FALSE,
                                                  ctx, subpool));
              svn_pool_destroy(subpool);
              return SVN_NO_ERROR;
            }

          SVN_ERR(svn_wc__node_get_repos_info(&repos_root_url, NULL,
                                              ctx->wc_ctx, local_abspath,
                                              FALSE, FALSE, pool, subpool));
          if (repos_root_url)
            {
              /* URLs don't match.  Try to relocate (if necessary) and then
                 switch. */
              if (! svn_uri_is_ancestor(repos_root_url, url))
                {
                  const char *repos_root;
                  svn_ra_session_t *ra_session;

                  /* Get the repos root of the new URL. */
                  SVN_ERR(svn_client__open_ra_session_internal
                          (&ra_session, NULL, url, NULL, NULL,
                           FALSE, TRUE, ctx, subpool));
                  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root,
                                                 subpool));

                  err = svn_client_relocate2(path, repos_root_url, repos_root,
                                             FALSE, ctx, subpool);
                  /* If the relocation failed because the new URL points
                     to another repository, then we need to relegate and
                     check out a new WC. */
                  if (err
                      && (err->apr_err == SVN_ERR_WC_INVALID_RELOCATION
                          || (err->apr_err
                              == SVN_ERR_CLIENT_INVALID_RELOCATION)))
                    {
                      svn_error_clear(err);
                      goto relegate;
                    }
                  else if (err)
                    return svn_error_return(err);
                }

              SVN_ERR(svn_client__switch_internal(NULL, path, url,
                                                  peg_revision, revision,
                                                  svn_depth_infinity,
                                                  TRUE, timestamp_sleep,
                                                  FALSE, FALSE, TRUE, ctx,
                                                  subpool));

              svn_pool_destroy(subpool);
              return SVN_NO_ERROR;
            }
        }
    }

 relegate:

  /* Fall back on removing the WC and checking out a new one. */

  /* Ensure that we don't have any RA sessions or WC locks from failed
     operations above. */
  svn_pool_destroy(subpool);

  if (kind == svn_node_dir)
    {
      struct relegate_dir_external_with_write_lock_baton baton;

      baton.local_abspath = local_abspath;
      baton.wc_ctx = ctx->wc_ctx;
      baton.cancel_func = ctx->cancel_func;
      baton.cancel_baton = ctx->cancel_baton;

      /* Buh-bye, old and busted ... */
      SVN_ERR(svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, local_abspath,
                                         FALSE, pool, pool));

      SVN_ERR(relegate_dir_external(&baton, pool, pool));
    }
  else
    {
      /* The target dir might have multiple components.  Guarantee
         the path leading down to the last component. */
      const char *parent = svn_dirent_dirname(path, pool);
      SVN_ERR(svn_io_make_dir_recursively(parent, pool));
    }

  /* ... Hello, new hotness. */
  return svn_client__checkout_internal(NULL, url, local_abspath, peg_revision,
                                       revision, NULL, svn_depth_infinity,
                                       FALSE, FALSE, TRUE, timestamp_sleep,
                                       ctx, pool);
}

/* Try to update a file external at PATH to URL at REVISION using a
   access baton that has a write lock.  Use POOL for temporary
   allocations, and use the client context CTX. */
static svn_error_t *
switch_file_external(const char *path,
                     const char *url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_ra_session_t *ra_session,
                     const char *ra_session_url,
                     svn_revnum_t ra_revnum,
                     const char *repos_root_url,
                     svn_boolean_t *timestamp_sleep,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *anchor;
  const char *anchor_abspath;
  const char *local_abspath;
  const char *target;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING) : NULL;
  svn_boolean_t use_commit_times;
  svn_boolean_t locked_here;
  svn_error_t *err = NULL;
  svn_node_kind_t kind;

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* There must be a working copy to place the file external into. */
  SVN_ERR(svn_wc_get_actual_target2(&anchor, &target, ctx->wc_ctx, path,
                                    subpool, subpool));
  SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, subpool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, subpool));

  /* Try to get a access baton for the anchor using the input access
     baton.  If this fails and returns SVN_ERR_WC_NOT_LOCKED, then try
     to get a new access baton to support inserting a file external
     into a directory external. */
  SVN_ERR(svn_wc_locked2(&locked_here, NULL, ctx->wc_ctx, anchor_abspath,
                         subpool));
  if (!locked_here)
    {
      const char *dest_wc_repos_root_url;
      svn_opt_revision_t peg_rev;

      /* Check that the repository root URL for the newly opened
         wc is the same as the file external. */
      peg_rev.kind = svn_opt_revision_base;
      SVN_ERR(svn_client__get_repos_root(&dest_wc_repos_root_url,
                                         anchor_abspath, &peg_rev,
                                         ctx, subpool, subpool));

      if (0 != strcmp(repos_root_url, dest_wc_repos_root_url))
        return svn_error_createf
          (SVN_ERR_RA_REPOS_ROOT_URL_MISMATCH, NULL,
           _("Cannot insert a file external from '%s' into a working "
             "copy from a different repository rooted at '%s'"),
           url, dest_wc_repos_root_url);

      SVN_ERR(svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, anchor_abspath,
                                         FALSE, subpool, subpool));
    }

  err = svn_wc_read_kind(&kind, ctx->wc_ctx, local_abspath, FALSE, subpool);
  if (err)
    goto cleanup;


  /* Only one notification is done for the external, so don't notify
     for any following steps.  Use the following trick to add the file
     then switch it to the external URL. */

  /* If there is a versioned item with this name, ensure it's a file
     external before working with it.  If there is no entry in the
     working copy, then create an empty file and add it to the working
     copy. */
  if (kind != svn_node_none && kind != svn_node_unknown)
    {
      svn_boolean_t file_external;
      err = svn_wc__node_is_file_external(&file_external, ctx->wc_ctx,
                                          local_abspath, subpool);
      if (err)
        goto cleanup;

      if (! file_external)
        {
          if (!locked_here)
            SVN_ERR(svn_wc__release_write_lock(ctx->wc_ctx, anchor_abspath,
                                               subpool));

          return svn_error_createf(
              SVN_ERR_CLIENT_FILE_EXTERNAL_OVERWRITE_VERSIONED, 0,
             _("The file external from '%s' cannot overwrite the existing "
               "versioned item at '%s'"),
             url, svn_dirent_local_style(path, pool));
        }
    }
  else
    {
      svn_boolean_t text_conflicted;
      svn_boolean_t prop_conflicted;
      svn_boolean_t tree_conflicted;
      svn_node_kind_t disk_kind;

      /* Check for a conflict on the containing directory.  Because a
         switch is done on the added file later, it will leave a
         conflict on the directory.  To prevent resolving a conflict
         due to another change on the directory, do not allow a file
         external to be added when one exists. */
      err = svn_wc_conflicted_p3(&text_conflicted, &prop_conflicted,
                                 &tree_conflicted, ctx->wc_ctx,
                                 anchor_abspath, subpool);
      if (err)
        goto cleanup;

      if (text_conflicted || prop_conflicted || tree_conflicted)
        return svn_error_createf
          (SVN_ERR_WC_FOUND_CONFLICT, 0,
           _("The file external from '%s' cannot be written to '%s' while "
             "'%s' remains in conflict"),
          url, path, anchor);

      err = svn_io_check_path(local_abspath, &disk_kind, subpool);

      if (err)
        goto cleanup;

      if (kind == svn_node_file || kind == svn_node_dir)
        {
          err = svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                                  _("The file external '%s' can not be "
                                    "created because the node exists."),
                                  svn_dirent_local_style(local_abspath,
                                                         subpool));
        }

      err = svn_wc__register_file_external(ctx->wc_ctx, local_abspath, url,
                                           peg_revision, revision, subpool);
      if (err)
        goto cleanup;
    }

  err = svn_client__switch_internal(NULL, path, url, peg_revision, revision,
                                    svn_depth_empty,
                                    FALSE, /* depth_is_sticky */
                                    timestamp_sleep,
                                    TRUE, /* ignore_externals */
                                    FALSE, /* allow_unver_obstructions */
                                    FALSE, /* innerswitch */
                                    ctx,
                                    pool);
  if (err)
    goto cleanup;

  if (!locked_here)
    SVN_ERR(svn_wc__release_write_lock(ctx->wc_ctx, anchor_abspath, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;

 cleanup:
  if (!locked_here)
    svn_error_clear(svn_wc__release_write_lock(ctx->wc_ctx, anchor_abspath,
                                               subpool));

  svn_pool_destroy(subpool);
  return svn_error_return(err);
}

/* Return the scheme of @a uri in @a scheme allocated from @a pool.
   If @a uri does not appear to be a valid URI, then @a scheme will
   not be updated.  */
static svn_error_t *
uri_scheme(const char **scheme, const char *uri, apr_pool_t *pool)
{
  apr_size_t i;

  for (i = 0; uri[i] && uri[i] != ':'; ++i)
    if (uri[i] == '/')
      goto error;

  if (i > 0 && uri[i] == ':' && uri[i+1] == '/' && uri[i+2] == '/')
    {
      *scheme = apr_pstrmemdup(pool, uri, i);
      return SVN_NO_ERROR;
    }

error:
  return svn_error_createf(SVN_ERR_BAD_URL, 0,
                           _("URL '%s' does not begin with a scheme"),
                           uri);
}

/* If the URL for @a item is relative, then using the repository root
   URL @a repos_root_url and the parent directory URL @parent_dir_url,
   resolve it into an absolute URL and save it in @a item.

   Regardless if the URL is absolute or not, if there are no errors,
   the URL in @a item will be canonicalized.

   The following relative URL formats are supported:

     ../    relative to the parent directory of the external
     ^/     relative to the repository root
     //     relative to the scheme
     /      relative to the server's hostname

   The ../ and ^/ relative URLs may use .. to remove path elements up
   to the server root.

   The external URL should not be canonicalized otherwise the scheme
   relative URL '//host/some/path' would have been canonicalized to
   '/host/some/path' and we would not be able to match on the leading
   '//'. */
static svn_error_t *
resolve_relative_external_url(svn_wc_external_item2_t *item,
                              const char *repos_root_url,
                              const char *parent_dir_url,
                              apr_pool_t *pool)
{
  const char *url = item->url;
  apr_uri_t parent_dir_uri;
  apr_status_t status;

  /* If the URL is already absolute, there is nothing to do. */
  if (svn_path_is_url(url))
    {
      /* "http://server/path" */
      item->url = svn_uri_canonicalize(url, pool);
      return SVN_NO_ERROR;
    }

  if (url[0] == '/')
    {
      /* "/path", "//path", and "///path" */
      int num_leading_slashes = 1;
      if (url[1] == '/')
        {
          num_leading_slashes++;
          if (url[2] == '/')
            num_leading_slashes++;
        }

      /* "//schema-relative" and in some cases "///schema-relative".
         This last format is supported on file:// schema relative. */
      url = apr_pstrcat(pool,
                        apr_pstrndup(pool, url, num_leading_slashes),
                        svn_relpath_canonicalize(url + num_leading_slashes,
                                                 pool),
                        (char*)NULL);
    }
  else
    {
      /* "^/path" and "../path" */
      url = svn_relpath_canonicalize(url, pool);
    }

  /* Parse the parent directory URL into its parts. */
  status = apr_uri_parse(pool, parent_dir_url, &parent_dir_uri);
  if (status)
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("Illegal parent directory URL '%s'"),
                             parent_dir_url);

  /* If the parent directory URL is at the server root, then the URL
     may have no / after the hostname so apr_uri_parse() will leave
     the URL's path as NULL. */
  if (! parent_dir_uri.path)
    parent_dir_uri.path = apr_pstrmemdup(pool, "/", 1);
  parent_dir_uri.query = NULL;
  parent_dir_uri.fragment = NULL;

  /* Handle URLs relative to the current directory or to the
     repository root.  The backpaths may only remove path elements,
     not the hostname.  This allows an external to refer to another
     repository in the same server relative to the location of this
     repository, say using SVNParentPath. */
  if ((0 == strncmp("../", url, 3)) ||
      (0 == strncmp("^/", url, 2)))
    {
      apr_array_header_t *base_components;
      apr_array_header_t *relative_components;
      int i;

      /* Decompose either the parent directory's URL path or the
         repository root's URL path into components.  */
      if (0 == strncmp("../", url, 3))
        {
          base_components = svn_path_decompose(parent_dir_uri.path, pool);
          relative_components = svn_path_decompose(url, pool);
        }
      else
        {
          apr_uri_t repos_root_uri;

          status = apr_uri_parse(pool, repos_root_url, &repos_root_uri);
          if (status)
            return svn_error_createf(SVN_ERR_BAD_URL, 0,
                                     _("Illegal repository root URL '%s'"),
                                     repos_root_url);

          /* If the repository root URL is at the server root, then
             the URL may have no / after the hostname so
             apr_uri_parse() will leave the URL's path as NULL. */
          if (! repos_root_uri.path)
            repos_root_uri.path = apr_pstrmemdup(pool, "/", 1);

          base_components = svn_path_decompose(repos_root_uri.path,
                                               pool);
          relative_components = svn_path_decompose(url + 2, pool);
        }

      for (i = 0; i < relative_components->nelts; ++i)
        {
          const char *component = APR_ARRAY_IDX(relative_components,
                                                i,
                                                const char *);
          if (0 == strcmp("..", component))
            {
              /* Constructing the final absolute URL together with
                 apr_uri_unparse() requires that the path be absolute,
                 so only pop a component if the component being popped
                 is not the component for the root directory. */
              if (base_components->nelts > 1)
                apr_array_pop(base_components);
            }
          else
            APR_ARRAY_PUSH(base_components, const char *) = component;
        }

      parent_dir_uri.path = (char *)svn_path_compose(base_components, pool);
      item->url = svn_uri_canonicalize(apr_uri_unparse(pool, &parent_dir_uri,
                                                       0), pool);
      return SVN_NO_ERROR;
    }

  /* The remaining URLs are relative to the either the scheme or
     server root and can only refer to locations inside that scope, so
     backpaths are not allowed. */
  if (svn_path_is_backpath_present(url + 2))
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("The external relative URL '%s' cannot have "
                               "backpaths, i.e. '..'"),
                             item->url);

  /* Relative to the scheme: Build a new URL from the parts we know.  */
  if (0 == strncmp("//", url, 2))
    {
      const char *scheme;

      SVN_ERR(uri_scheme(&scheme, repos_root_url, pool));
      item->url = svn_uri_canonicalize(apr_pstrcat(pool, scheme, ":",
                                                   url, (char *)NULL), pool);
      return SVN_NO_ERROR;
    }

  /* Relative to the server root: Just replace the path portion of the
     parent's URL.  */
  if (url[0] == '/')
    {
      parent_dir_uri.path = (char *)url;
      item->url = svn_uri_canonicalize(apr_uri_unparse(pool, &parent_dir_uri,
                                                       0), pool);
      return SVN_NO_ERROR;
    }

  return svn_error_createf(SVN_ERR_BAD_URL, 0,
                           _("Unrecognized format for the relative external "
                             "URL '%s'"),
                           item->url);
}

/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct handle_external_item_change_baton *'.  */
static svn_error_t *
handle_external_item_change(const void *key, apr_ssize_t klen,
                            enum svn_hash_diff_key_status status,
                            void *baton)
{
  struct handle_external_item_change_baton *ib = baton;
  svn_wc_external_item2_t *old_item, *new_item;
  const char *local_abspath = svn_dirent_join(ib->parent_dir_abspath,
                                              (const char *) key,
                                              ib->iter_pool);
  svn_ra_session_t *ra_session;
  svn_node_kind_t kind;
  svn_client__ra_session_from_path_results ra_cache = { 0 };

  /* Don't bother to check status, since we'll get that for free by
     attempting to retrieve the hash values anyway.  */

  /* When creating the absolute URL, use the pool and not the
     iterpool, since the hash table values outlive the iterpool and
     any pointers they have should also outlive the iterpool.  */
  if ((ib->old_desc) && (! ib->is_export))
    {
      old_item = apr_hash_get(ib->old_desc, key, klen);
      if (old_item)
        SVN_ERR(resolve_relative_external_url(old_item, ib->repos_root_url,
                                              ib->parent_dir_url,
                                              ib->pool));
    }
  else
    old_item = NULL;

  if (ib->new_desc)
    {
      new_item = apr_hash_get(ib->new_desc, key, klen);
      if (new_item)
        SVN_ERR(resolve_relative_external_url(new_item, ib->repos_root_url,
                                              ib->parent_dir_url,
                                              ib->pool));
    }
  else
    new_item = NULL;

  /* We couldn't possibly be here if both values were null, right? */
  SVN_ERR_ASSERT(old_item || new_item);

  /* There's one potential ugliness.  If a target subdir changed, but
     its URL did not, then ideally we'd just rename the subdir, rather
     than remove the old subdir only to do a new checkout into the new
     subdir.

     We could solve this by "sneaking around the back" and looking in
     ib->new_desc, ib->old_desc to check if anything else in this
     parent_dir has the same URL.  Of course, if an external gets
     moved into some other directory, then we'd lose anyway.  The only
     way to fully handle this would be to harvest a global list based
     on urls/revs, and consult the list every time we're about to
     delete an external subdir: whenever a deletion is really part of
     a rename, then we'd do the rename on the spot.

     IMHO, renames aren't going to be frequent enough to make the
     extra bookkeeping worthwhile.
  */

  /* If the external is being checked out, exported or updated,
     determine if the external is a file or directory. */
  if (new_item)
    {
      /* Get the RA connection. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session,
                                               &ra_cache.ra_revnum,
                                               &ra_cache.ra_session_url,
                                               new_item->url, NULL,
                                               &(new_item->peg_revision),
                                               &(new_item->revision), ib->ctx,
                                               ib->iter_pool));

      SVN_ERR(svn_ra_get_uuid2(ra_session, &ra_cache.repos_uuid,
                               ib->iter_pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &ra_cache.repos_root_url,
                                     ib->iter_pool));
      SVN_ERR(svn_ra_check_path(ra_session, "", ra_cache.ra_revnum, &kind,
                                ib->iter_pool));

      if (svn_node_none == kind)
        return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                 _("URL '%s' at revision %ld doesn't exist"),
                                 ra_cache.ra_session_url,
                                 ra_cache.ra_revnum);

      if (svn_node_dir != kind && svn_node_file != kind)
        return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                 _("URL '%s' at revision %ld is not a file "
                                   "or a directory"),
                                 ra_cache.ra_session_url,
                                 ra_cache.ra_revnum);

      ra_cache.kind_p = &kind;
    }

  /* Not protecting against recursive externals.  Detecting them in
     the global case is hard, and it should be pretty obvious to a
     user when it happens.  Worst case: your disk fills up :-). */

  if (! old_item)
    {
      /* This branch is only used during a checkout or an export. */
      const char *parent_abspath;

      /* First notify that we're about to handle an external. */
      if (ib->ctx->notify_func2)
        (*ib->ctx->notify_func2)
          (ib->ctx->notify_baton2,
           svn_wc_create_notify(local_abspath, svn_wc_notify_update_external,
                                ib->iter_pool), ib->iter_pool);

      switch (*ra_cache.kind_p)
        {
        case svn_node_dir:
          /* The target dir might have multiple components.  Guarantee
             the path leading down to the last component. */
          parent_abspath = svn_dirent_dirname(local_abspath, ib->iter_pool);
          SVN_ERR(svn_io_make_dir_recursively(parent_abspath, ib->iter_pool));

          /* If we were handling renames the fancy way, then before
             checking out a new subdir here, we would somehow learn if
             it's really just a rename of an old one.  That would work in
             tandem with the next case -- this case would do nothing,
             knowing that the next case either already has, or soon will,
             rename the external subdirectory. */

          if (ib->is_export)
            /* ### It should be okay to "force" this export.  Externals
               only get created in subdirectories of versioned
               directories, so an external directory couldn't already
               exist before the parent export process unless a versioned
               directory above it did, which means the user would have
               already had to force these creations to occur. */
            SVN_ERR(svn_client_export4(NULL, new_item->url, local_abspath,
                                       &(new_item->peg_revision),
                                       &(new_item->revision),
                                       TRUE, FALSE, svn_depth_infinity,
                                       ib->native_eol,
                                       ib->ctx, ib->iter_pool));
          else
            SVN_ERR(svn_client__checkout_internal
                    (NULL, new_item->url, local_abspath,
                     &(new_item->peg_revision), &(new_item->revision),
                     &ra_cache,
                     SVN_DEPTH_INFINITY_OR_FILES(TRUE),
                     FALSE, FALSE, TRUE, ib->timestamp_sleep, ib->ctx,
                     ib->iter_pool));
          break;
        case svn_node_file:
          if (ib->is_export)
            /* Do not overwrite an existing file with this file
               external. */
            SVN_ERR(svn_client_export4(NULL, new_item->url, local_abspath,
                                       &(new_item->peg_revision),
                                       &(new_item->revision),
                                       FALSE, TRUE, svn_depth_infinity,
                                       ib->native_eol,
                                       ib->ctx, ib->iter_pool));
          else
            SVN_ERR(switch_file_external(local_abspath,
                                         new_item->url,
                                         &new_item->peg_revision,
                                         &new_item->revision,
                                         ra_session,
                                         ra_cache.ra_session_url,
                                         ra_cache.ra_revnum,
                                         ra_cache.repos_root_url,
                                         ib->timestamp_sleep, ib->ctx,
                                         ib->iter_pool));
          break;
        default:
          SVN_ERR_MALFUNCTION();
          break;
        }
    }
  else if (! new_item)
    {
      /* This branch is only used when an external is deleted from the
         repository and the working copy is updated. */

      /* See comment in above case about fancy rename handling.  Here,
         before removing an old subdir, we would see if it wants to
         just be renamed to a new one. */

      svn_error_t *err;
      svn_boolean_t lock_existed;

      SVN_ERR(svn_wc_locked2(&lock_existed, NULL, ib->ctx->wc_ctx,
                             local_abspath, ib->iter_pool));

      if (! lock_existed)
        {
          SVN_ERR(svn_wc__acquire_write_lock(NULL, ib->ctx->wc_ctx,
                                             local_abspath, FALSE,
                                             ib->iter_pool,
                                             ib->iter_pool));
        }

      /* We don't use relegate_dir_external() here, because we know that
         nothing else in this externals description (at least) is
         going to need this directory, and therefore it's better to
         leave stuff where the user expects it. */
      err = svn_wc_remove_from_revision_control2(
                        ib->ctx->wc_ctx, local_abspath, TRUE, FALSE,
                        ib->ctx->cancel_func, ib->ctx->cancel_baton,
                        ib->iter_pool);

      if (ib->ctx->notify_func2)
        {
          svn_wc_notify_t *notify =
              svn_wc_create_notify(local_abspath,
                                   svn_wc_notify_update_external_removed,
                                   ib->iter_pool);

          notify->kind = svn_node_dir;
          notify->err = err;

          (ib->ctx->notify_func2)(ib->ctx->notify_baton2,
                                  notify, ib->iter_pool);
        }

      /* ### Ugly. Unlock only if not going to return an error. Revisit */
      if (! lock_existed
          && (! err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
        {
          svn_error_t *err2 = svn_wc__release_write_lock(ib->ctx->wc_ctx,
                                                         local_abspath,
                                                         ib->iter_pool);
          if (err2)
            {
              if (! err)
                err = err2;
              else
                svn_error_clear(err2);
            }
        }

      if (err && (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD))
        return svn_error_return(err);
      svn_error_clear(err);

      /* ### If there were multiple path components leading down to
         that wc, we could try to remove them too. */
    }
  else
    {
      /* This branch handles all other changes. */

      /* First notify that we're about to handle an external. */
      if (ib->ctx->notify_func2)
        (*ib->ctx->notify_func2)
          (ib->ctx->notify_baton2,
           svn_wc_create_notify(local_abspath, svn_wc_notify_update_external,
                                ib->iter_pool), ib->iter_pool);

      /* Either the URL changed, or the exact same item is present in
         both hashes, and caller wants to update such unchanged items.
         In the latter case, the call below will try to make sure that
         the external really is a WC pointing to the correct
         URL/revision. */
      switch (*ra_cache.kind_p)
        {
        case svn_node_dir:
          SVN_ERR(switch_dir_external(local_abspath, new_item->url,
                                      &(new_item->revision),
                                      &(new_item->peg_revision),
                                      ib->timestamp_sleep, ib->ctx,
                                      ib->iter_pool));
          break;
        case svn_node_file:
          SVN_ERR(switch_file_external(local_abspath,
                                       new_item->url,
                                       &new_item->peg_revision,
                                       &new_item->revision,
                                       ra_session,
                                       ra_cache.ra_session_url,
                                       ra_cache.ra_revnum,
                                       ra_cache.repos_root_url,
                                       ib->timestamp_sleep, ib->ctx,
                                       ib->iter_pool));
          break;
        default:
          SVN_ERR_MALFUNCTION();
          break;
        }
    }

  /* Clear ib->iter_pool -- we only use it for scratchwork (and this will
     close any RA sessions still open in this pool). */
  svn_pool_clear(ib->iter_pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
handle_external_item_change_wrapper(const void *key, apr_ssize_t klen,
                                    enum svn_hash_diff_key_status status,
                                    void *baton)
{
  struct handle_external_item_change_baton *ib = baton;
  svn_error_t *err = handle_external_item_change(key, klen, status, baton);

  if (err && err->apr_err != SVN_ERR_CANCELLED)
    {
      if (ib->ctx->notify_func2)
        {
          const char *local_abspath = svn_dirent_join(ib->parent_dir_abspath,
                                                      key, ib->iter_pool);
          svn_wc_notify_t *notifier =
          svn_wc_create_notify(local_abspath,
                               svn_wc_notify_failed_external,
                               ib->iter_pool);
          notifier->err = err;
          ib->ctx->notify_func2(ib->ctx->notify_baton2, notifier,
                                ib->iter_pool);
        }
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return svn_error_return(err);
}


/* Closure for handle_externals_change. */
struct handle_externals_desc_change_baton
{
  /* As returned by svn_wc_edited_externals(). */
  apr_hash_t *externals_new;
  apr_hash_t *externals_old;

  /* The requested depth of the driving operation (e.g., update, switch). */
  svn_depth_t requested_depth;

  /* As returned by svn_wc_traversed_depths().  NULL means no ambient
     depths available (e.g., svn export). */
  apr_hash_t *ambient_depths;

  /* These two map a URL to a path where the URL is either checked out
     to or exported to.  The to_path must be a substring of the
     external item parent directory path. */
  const char *from_url;
  const char *to_abspath;

  /* Passed through to handle_external_item_change_baton. */
  svn_client_ctx_t *ctx;
  const char *repos_root_url;
  svn_boolean_t *timestamp_sleep;
  svn_boolean_t is_export;

  /* Passed to svn_client_exportX() */
  const char *native_eol;

  apr_pool_t *pool;
};


/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct handle_externals_desc_change_baton *'.
*/
static svn_error_t *
handle_externals_desc_change(const void *key, apr_ssize_t klen,
                             enum svn_hash_diff_key_status status,
                             void *baton)
{
  struct handle_externals_desc_change_baton *cb = baton;
  struct handle_external_item_change_baton ib = { 0 };
  const char *old_desc_text, *new_desc_text;
  apr_array_header_t *old_desc, *new_desc;
  apr_hash_t *old_desc_hash, *new_desc_hash;
  apr_size_t len;
  int i;
  svn_wc_external_item2_t *item;
  const char *ambient_depth_w;
  svn_depth_t ambient_depth;
  const char *url;
  svn_error_t *err;

  if (cb->ambient_depths)
    {
      ambient_depth_w = apr_hash_get(cb->ambient_depths, key, klen);
      if (ambient_depth_w == NULL)
        {
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT, NULL,
             _("Traversal of '%s' found no ambient depth"),
             (const char *) key);
        }
      else
        {
          ambient_depth = svn_depth_from_word(ambient_depth_w);
        }
    }
  else
    {
      ambient_depth = svn_depth_infinity;
    }

  /* Bag out if the depth here is too shallow for externals action. */
  if ((cb->requested_depth < svn_depth_infinity
       && cb->requested_depth != svn_depth_unknown)
      || (ambient_depth < svn_depth_infinity
          && cb->requested_depth < svn_depth_infinity))
    return SVN_NO_ERROR;

  if ((old_desc_text = apr_hash_get(cb->externals_old, key, klen)))
    SVN_ERR(svn_wc_parse_externals_description3(&old_desc, key, old_desc_text,
                                                FALSE, cb->pool));
  else
    old_desc = NULL;

  if ((new_desc_text = apr_hash_get(cb->externals_new, key, klen)))
    SVN_ERR(svn_wc_parse_externals_description3(&new_desc, key, new_desc_text,
                                                FALSE, cb->pool));
  else
    new_desc = NULL;

  old_desc_hash = apr_hash_make(cb->pool);
  new_desc_hash = apr_hash_make(cb->pool);

  /* Create hashes of our two externals arrays so that we can
     efficiently generate a diff for them. */
  for (i = 0; old_desc && (i < old_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(old_desc, i, svn_wc_external_item2_t *);

      apr_hash_set(old_desc_hash, item->target_dir,
                   APR_HASH_KEY_STRING, item);
    }

  for (i = 0; new_desc && (i < new_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(new_desc, i, svn_wc_external_item2_t *);

      apr_hash_set(new_desc_hash, item->target_dir,
                   APR_HASH_KEY_STRING, item);
    }

  ib.old_desc          = old_desc_hash;
  ib.new_desc          = new_desc_hash;
  ib.repos_root_url    = cb->repos_root_url;
  ib.ctx               = cb->ctx;
  ib.is_export         = cb->is_export;
  ib.native_eol        = cb->native_eol;
  ib.timestamp_sleep   = cb->timestamp_sleep;
  ib.pool              = cb->pool;
  ib.iter_pool         = svn_pool_create(cb->pool);
  SVN_ERR(svn_dirent_get_absolute(&ib.parent_dir_abspath, (const char *) key,
                                  cb->pool));

  err = svn_wc__node_get_url(&url, cb->ctx->wc_ctx, ib.parent_dir_abspath,
                             cb->pool, cb->pool);

  /* If we're doing an 'svn export' the current dir will not be a
     working copy. We can't get the parent_dir. */
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY ||
          err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          /* Get the URL of the parent directory by appending a portion of
             parent_dir to from_url.  from_url is the URL for to_path and
             to_path is a substring of parent_dir, so append any characters in
             parent_dir past strlen(to_path) to from_url (making sure to move
             past a '/' in parent_dir, otherwise svn_path_url_add_component()
             will error. */
          len = strlen(cb->to_abspath);
          if (ib.parent_dir_abspath[len] == '/')
            ++len;
          ib.parent_dir_url = svn_path_url_add_component2(cb->from_url,
                                           ib.parent_dir_abspath + len,
                                           cb->pool);
          svn_error_clear(err);
        }
      else
        return svn_error_return(err);
    }
  else
      ib.parent_dir_url = url;

  /* We must use a custom version of svn_hash_diff so that the diff
     entries are processed in the order they were originally specified
     in the svn:externals properties. */

  for (i = 0; old_desc && (i < old_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(old_desc, i, svn_wc_external_item2_t *);

      if (apr_hash_get(new_desc_hash, item->target_dir, APR_HASH_KEY_STRING))
        SVN_ERR(handle_external_item_change_wrapper(item->target_dir,
                                                    APR_HASH_KEY_STRING,
                                                    svn_hash_diff_key_both,
                                                    &ib));
      else
        SVN_ERR(handle_external_item_change_wrapper(item->target_dir,
                                                    APR_HASH_KEY_STRING,
                                                    svn_hash_diff_key_a,
                                                    &ib));
    }
  for (i = 0; new_desc && (i < new_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(new_desc, i, svn_wc_external_item2_t *);
      if (! apr_hash_get(old_desc_hash, item->target_dir, APR_HASH_KEY_STRING))
        SVN_ERR(handle_external_item_change_wrapper(item->target_dir,
                                                    APR_HASH_KEY_STRING,
                                                    svn_hash_diff_key_b,
                                                    &ib));
    }

  /* Now destroy the subpool we pass to the hash differ.  This will
     close any remaining RA sessions used by the hash diff callback. */
  svn_pool_destroy(ib.iter_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__handle_externals(apr_hash_t *externals_old,
                             apr_hash_t *externals_new,
                             apr_hash_t *ambient_depths,
                             const char *from_url,
                             const char *to_abspath,
                             const char *repos_root_url,
                             svn_depth_t requested_depth,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  struct handle_externals_desc_change_baton cb = { 0 };

  SVN_ERR_ASSERT(svn_dirent_is_absolute(to_abspath));

  /* Sanity check; see r870198. */
  if (! svn_path_is_url(from_url))
    return svn_error_createf
      (SVN_ERR_BAD_URL, NULL, _("'%s' is not a URL"), from_url);

  cb.externals_new     = externals_new;
  cb.externals_old     = externals_old;
  cb.requested_depth   = requested_depth;
  cb.ambient_depths    = ambient_depths;
  cb.from_url          = from_url;
  cb.to_abspath        = to_abspath;
  cb.repos_root_url    = repos_root_url;
  cb.ctx               = ctx;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.is_export         = FALSE;
  cb.native_eol        = NULL;
  cb.pool              = pool;

  return svn_hash_diff(cb.externals_old, cb.externals_new,
                       handle_externals_desc_change, &cb, pool);
}


svn_error_t *
svn_client__fetch_externals(apr_hash_t *externals,
                            const char *from_url,
                            const char *to_abspath,
                            const char *repos_root_url,
                            svn_depth_t requested_depth,
                            svn_boolean_t is_export,
                            const char *native_eol,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  struct handle_externals_desc_change_baton cb = { 0 };

  SVN_ERR_ASSERT(svn_dirent_is_absolute(to_abspath));

  cb.externals_new     = externals;
  cb.externals_old     = apr_hash_make(pool);
  cb.requested_depth   = requested_depth;
  cb.ambient_depths    = NULL;
  cb.ctx               = ctx;
  cb.from_url          = from_url;
  cb.to_abspath        = to_abspath;
  cb.repos_root_url    = repos_root_url;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.native_eol        = native_eol;
  cb.is_export         = is_export;
  cb.pool              = pool;

  return svn_hash_diff(cb.externals_old, cb.externals_new,
                       handle_externals_desc_change, &cb, pool);
}


svn_error_t *
svn_client__do_external_status(svn_client_ctx_t *ctx,
                               apr_hash_t *externals_new,
                               svn_depth_t depth,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_status_func_t status_func,
                               void *status_baton,
                               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Loop over the hash of new values (we don't care about the old
     ones).  This is a mapping of versioned directories to property
     values. */
  for (hi = apr_hash_first(pool, externals_new);
       hi;
       hi = apr_hash_next(hi))
    {
      apr_array_header_t *exts;
      const char *path = svn__apr_hash_index_key(hi);
      const char *propval = svn__apr_hash_index_val(hi);
      apr_pool_t *iterpool;
      int i;

      /* Clear the subpool. */
      svn_pool_clear(subpool);

      /* Parse the svn:externals property value.  This results in a
         hash mapping subdirectories to externals structures. */
      SVN_ERR(svn_wc_parse_externals_description3(&exts, path, propval,
                                                  FALSE, subpool));

      /* Make a sub-pool of SUBPOOL. */
      iterpool = svn_pool_create(subpool);

      /* Loop over the subdir array. */
      for (i = 0; exts && (i < exts->nelts); i++)
        {
          const char *fullpath;
          svn_wc_external_item2_t *external;
          svn_node_kind_t kind;

          svn_pool_clear(iterpool);

          external = APR_ARRAY_IDX(exts, i, svn_wc_external_item2_t *);
          fullpath = svn_dirent_join(path, external->target_dir, iterpool);

          /* If the external target directory doesn't exist on disk,
             just skip it. */
          SVN_ERR(svn_io_check_path(fullpath, &kind, iterpool));
          if (kind != svn_node_dir)
            continue;

          /* Tell the client we're staring an external status set. */
          if (ctx->notify_func2)
            (ctx->notify_func2)
              (ctx->notify_baton2,
               svn_wc_create_notify(fullpath, svn_wc_notify_status_external,
                                    iterpool), iterpool);

          /* And then do the status. */
          SVN_ERR(svn_client_status5(NULL, ctx, fullpath,
                                     &(external->revision),
                                     depth, get_all, update,
                                     no_ignore, FALSE, FALSE, NULL,
                                     status_func, status_baton,
                                     iterpool));
        }
    }

  /* Destroy SUBPOOL and (implicitly) ITERPOOL. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Implements the `svn_wc_externals_update_t' interface. */
svn_error_t *
svn_client__external_info_gatherer(void *baton,
                                   const char *local_abspath,
                                   const svn_string_t *old_value,
                                   const svn_string_t *new_value,
                                   svn_depth_t depth,
                                   apr_pool_t *scratch_pool)
{
  svn_client__external_func_baton_t *efb = baton;

  local_abspath = apr_pstrdup(efb->result_pool, local_abspath);

  if (efb->externals_old != NULL && old_value != NULL)
    apr_hash_set(efb->externals_old, local_abspath, APR_HASH_KEY_STRING,
                 apr_pstrndup(efb->result_pool,
                              old_value->data, old_value->len));

  if (efb->externals_new != NULL && new_value != NULL)
    apr_hash_set(efb->externals_new, local_abspath, APR_HASH_KEY_STRING,
                 apr_pstrndup(efb->result_pool,
                              new_value->data, new_value->len));

  if (efb->ambient_depths != NULL)
    apr_hash_set(efb->ambient_depths, local_abspath, APR_HASH_KEY_STRING,
                 svn_depth_to_word(depth));

  return SVN_NO_ERROR;
}


/* An implementation of svn_wc__proplist_receiver_t. Just squirrels away an
   svn:externals property value into BATON (which is an apr_hash_t *
   keyed on local absolute path).  */
static svn_error_t *
externals_crawl_proplist_receiver(void *baton,
                                  const char *local_abspath,
                                  apr_hash_t *props,
                                  apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *externals_hash = (apr_hash_t*)baton;

  for (hi = apr_hash_first(scratch_pool, props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *propname;

      propname = svn__apr_hash_index_key(hi);
      if (strcmp(propname, SVN_PROP_EXTERNALS) == 0)
        {
          apr_pool_t *hash_pool = apr_hash_pool_get(externals_hash);
          svn_string_t *propval = svn__apr_hash_index_val(hi);

          apr_hash_set(externals_hash, apr_pstrdup(hash_pool, local_abspath),
                       APR_HASH_KEY_STRING, svn_string_dup(propval, hash_pool));
          break;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__crawl_for_externals(apr_hash_t **externals_p,
                                const char *local_abspath,
                                svn_depth_t depth,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  apr_hash_t *externals_hash = apr_hash_make(result_pool);

  SVN_ERR(svn_wc__prop_list_recursive(ctx->wc_ctx, local_abspath, NULL, depth,
                                      FALSE, /* pristine */
                                      externals_crawl_proplist_receiver,
                                      externals_hash,
                                      ctx->cancel_func,
                                      ctx->cancel_baton,
                                      scratch_pool));

  *externals_p = externals_hash;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__gather_externals_in_locally_added_dirs(apr_hash_t *externals_new,
                                                   apr_hash_t *ambient_depths,
                                                   const char *anchor_abspath,
                                                   svn_depth_t requested_depth,
                                                   svn_client_ctx_t *ctx,
                                                   apr_pool_t *scratch_pool)
{
  apr_hash_t *all_externals;
  apr_hash_index_t *hi;

  /* If there was no requested depth for this operation, use infinity.
   * svn_client__crawl_for_externals() doesn't like depth 'unknown'. */
  if (requested_depth == svn_depth_unknown)
    requested_depth = svn_depth_infinity;

  SVN_ERR(svn_client__crawl_for_externals(&all_externals, anchor_abspath,
                                          requested_depth, ctx, scratch_pool,
                                          scratch_pool));

  for (hi = apr_hash_first(scratch_pool, all_externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath = svn__apr_hash_index_key(hi);

      if (! apr_hash_get(externals_new, local_abspath, APR_HASH_KEY_STRING))
        {
          apr_pool_t *hash_pool = apr_hash_pool_get(externals_new);
          svn_string_t *propval = svn__apr_hash_index_val(hi);

          apr_hash_set(externals_new, local_abspath, APR_HASH_KEY_STRING,
                       apr_pstrdup(hash_pool, propval->data));
          apr_hash_set(ambient_depths, local_abspath, APR_HASH_KEY_STRING,
                       svn_depth_to_word(svn_depth_infinity));
        }
    }

  return SVN_NO_ERROR;
}

