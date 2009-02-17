/*
 * externals.c:  handle the svn:externals property
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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

#include <apr_uri.h>
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
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
  const char *parent_dir;

  /* Access baton for parent_dir.  If the external is an export, this
     this must be NULL, otherwise it must be non-NULL. */
  svn_wc_adm_access_t *adm_access;

  /* The URL for the directory that has this externals property. */
  const char *parent_dir_url;

  /* The URL for the repository root. */
  const char *repos_root_url;

  /* Passed through to svn_client_* functions. */
  svn_client_ctx_t *ctx;

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


/* Remove the directory at PATH from revision control, and do the same
 * to any revision controlled directories underneath PATH (including
 * directories not referred to by parent svn administrative areas);
 * then if PATH is empty afterwards, remove it, else rename it to a
 * unique name in the same parent directory.
 *
 * Pass CANCEL_FUNC, CANCEL_BATON to svn_wc_remove_from_revision_control.
 *
 * Use POOL for all temporary allocation.
 *
 * Note: this function is not passed a svn_wc_adm_access_t.  Instead,
 * it separately opens the object being deleted, so that if there is a
 * lock on that object, the object cannot be deleted.
 */
static svn_error_t *
relegate_dir_external(const char *path,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, TRUE, -1, cancel_func,
                           cancel_baton, pool));
  err = svn_wc_remove_from_revision_control(adm_access,
                                            SVN_WC_ENTRY_THIS_DIR,
                                            TRUE, FALSE,
                                            cancel_func,
                                            cancel_baton,
                                            pool);

  /* ### Ugly. Unlock only if not going to return an error. Revisit */
  if (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    {
      const char *parent_dir;
      const char *dirname;
      const char *new_path;

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      svn_path_split(path, &parent_dir, &dirname, pool);

      /* Reserve the new dir name. */
      SVN_ERR(svn_io_open_uniquely_named(NULL, &new_path,
                                         parent_dir, dirname, ".OLD",
                                         svn_io_file_del_none, pool, pool));

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
      svn_error_clear(svn_io_remove_file(new_path, pool));

      /* Rename. */
      SVN_ERR(svn_io_file_rename(path, new_path, pool));
    }

  return err;
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

  /* If path is a directory, try to update/switch to the correct URL
     and revision. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, TRUE, 0,
                               ctx->cancel_func, ctx->cancel_baton, subpool));
      SVN_ERR(svn_wc_entry(&entry, path, adm_access,
                           FALSE, subpool));
      SVN_ERR(svn_wc_adm_close2(adm_access, subpool));

      if (entry && entry->url)
        {
          /* If we have what appears to be a version controlled
             subdir, and its top-level URL matches that of our
             externals definition, perform an update. */
          if (strcmp(entry->url, url) == 0)
            {
              SVN_ERR(svn_client__update_internal(NULL, path, revision,
                                                  svn_depth_unknown, FALSE,
                                                  FALSE, FALSE,
                                                  timestamp_sleep, TRUE,
                                                  ctx, subpool));
              svn_pool_destroy(subpool);
              return SVN_NO_ERROR;
            }
          else if (entry->repos)
            {
              /* URLs don't match.  Try to relocate (if necessary) and then
                 switch. */
              if (! svn_path_is_ancestor(entry->repos, url))
                {
                  const char *repos_root;
                  svn_ra_session_t *ra_session;

                  /* Get the repos root of the new URL. */
                  SVN_ERR(svn_client__open_ra_session_internal
                          (&ra_session, url, NULL, NULL, NULL, FALSE, TRUE,
                           ctx, subpool));
                  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root,
                                                 subpool));

                  err = svn_client_relocate(path, entry->repos, repos_root,
                                            TRUE, ctx, subpool);
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
                    return err;
                }

              SVN_ERR(svn_client__switch_internal(NULL, path, url,
                                                  peg_revision, revision,
                                                  NULL, svn_depth_infinity,
                                                  TRUE, timestamp_sleep,
                                                  FALSE, FALSE, ctx, subpool));

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
    /* Buh-bye, old and busted ... */
    SVN_ERR(relegate_dir_external(path,
                                  ctx->cancel_func,
                                  ctx->cancel_baton,
                                  pool));
  else
    {
      /* The target dir might have multiple components.  Guarantee
         the path leading down to the last component. */
      const char *parent = svn_path_dirname(path, pool);
      SVN_ERR(svn_io_make_dir_recursively(parent, pool));
    }

  /* ... Hello, new hotness. */
  return svn_client__checkout_internal(NULL, url, path, peg_revision,
                                       revision, NULL,
                                       SVN_DEPTH_INFINITY_OR_FILES(TRUE),
                                       FALSE, FALSE, timestamp_sleep,
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
                     svn_wc_adm_access_t *adm_access,
                     svn_ra_session_t *ra_session,
                     const char *ra_session_url,
                     svn_revnum_t ra_revnum,
                     const char *repos_root_url,
                     svn_boolean_t *timestamp_sleep,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_wc_adm_access_t *target_adm_access;
  const char *anchor;
  const char *target;
  const svn_wc_entry_t *entry;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING) : NULL;
  svn_boolean_t use_commit_times;
  svn_boolean_t unlink_file = FALSE;
  svn_boolean_t revert_file = FALSE;
  svn_boolean_t remove_from_revision_control = FALSE;
  svn_boolean_t close_adm_access = FALSE;
  svn_error_t *err = NULL;

  /* There must be a working copy to place the file external into. */
  SVN_ERR(svn_wc_get_actual_target(path, &anchor, &target, subpool));

  /* Try to get a access baton for the anchor using the input access
     baton.  If this fails and returns SVN_ERR_WC_NOT_LOCKED, then try
     to get a new access baton to support inserting a file external
     into a directory external. */
  err = svn_wc_adm_retrieve(&target_adm_access, adm_access, anchor, subpool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_NOT_LOCKED)
        {
          const char *dest_wc_repos_root_url;
          svn_opt_revision_t peg_rev;

          svn_error_clear(err);
          close_adm_access = TRUE;
          SVN_ERR(svn_wc_adm_open3(&target_adm_access, NULL, anchor, TRUE, 1,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   subpool));

          /* Check that the repository root URL for the newly opened
             wc is the same as the file external. */
          peg_rev.kind = svn_opt_revision_base;
          SVN_ERR(svn_client__get_repos_root(&dest_wc_repos_root_url,
                                             anchor, &peg_rev,
                                             target_adm_access, ctx, subpool));

          if (0 != strcmp(repos_root_url, dest_wc_repos_root_url))
            return svn_error_createf
              (SVN_ERR_RA_REPOS_ROOT_URL_MISMATCH, NULL,
               _("Cannot insert a file external from '%s' into a working "
                 "copy from a different repository rooted at '%s'"),
               url, dest_wc_repos_root_url);
        }
      else
        return err;
    }

  SVN_ERR(svn_wc_entry(&entry, path, target_adm_access, FALSE, subpool));

  /* Only one notification is done for the external, so don't notify
     for any following steps.  Use the following trick to add the file
     then switch it to the external URL. */

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* If there is a versioned item with this name, ensure it's a file
     external before working with it.  If there is no entry in the
     working copy, then create an empty file and add it to the working
     copy. */
  if (entry)
    {
      if (! entry->file_external_path)
        {
          if (close_adm_access)
            SVN_ERR(svn_wc_adm_close2(target_adm_access, subpool));

          return svn_error_createf
            (SVN_ERR_CLIENT_FILE_EXTERNAL_OVERWRITE_VERSIONED, 0,
             _("The file external from '%s' cannot overwrite the existing "
               "versioned item at '%s'"),
             url, path);
        }
    }
  else
    {
      const svn_wc_entry_t *anchor_dir_entry;
      apr_file_t *f;
      svn_boolean_t text_conflicted;
      svn_boolean_t prop_conflicted;
      svn_boolean_t tree_conflicted;

      /* Check for a conflict on the containing directory.  Because a
         switch is done on the added file later, it will leave a
         conflict on the directory.  To prevent resolving a conflict
         due to another change on the directory, do not allow a file
         external to be added when one exists. */
      SVN_ERR(svn_wc__entry_versioned(&anchor_dir_entry, anchor,
                                      target_adm_access, FALSE, subpool));
      SVN_ERR(svn_wc_conflicted_p2(&text_conflicted, &prop_conflicted,
                                   &tree_conflicted, anchor, target_adm_access,
                                   subpool));
      if (text_conflicted || prop_conflicted || tree_conflicted)
        return svn_error_createf
          (SVN_ERR_WC_FOUND_CONFLICT, 0,
           _("The file external from '%s' cannot be written to '%s' while "
             "'%s' remains in conflict"),
          url, path, anchor);

      /* Try to create an empty file.  If there is a file already
         there, then don't touch it. */
      SVN_ERR(svn_io_file_open(&f,
                               path,
                               APR_WRITE | APR_CREATE | APR_EXCL,
                               APR_OS_DEFAULT,
                               subpool));
      unlink_file = TRUE;
      err = svn_io_file_close(f, pool);
      if (err)
        goto cleanup;

      err = svn_wc_add3(path, target_adm_access, svn_depth_infinity,
                        NULL, /* const char *copyfrom_url */
                        SVN_INVALID_REVNUM, /* svn_revnum_t copyfrom_rev */
                        ctx->cancel_func, ctx->cancel_baton,
                        NULL, /* svn_wc_notify_func2_t notify_func */
                        NULL, /* void *notify_baton */
                        subpool);
      if (err)
        goto cleanup;
      revert_file = TRUE;

      err = svn_wc__set_file_external_location(target_adm_access, target,
                                               url, peg_revision, revision,
                                               repos_root_url, subpool);
      if (err)
        goto cleanup;
    }

  err = svn_client__switch_internal(NULL, path, url, peg_revision, revision,
                                    target_adm_access, svn_depth_empty,
                                    FALSE, /* depth_is_sticky */
                                    timestamp_sleep,
                                    TRUE, /* ignore_externals */
                                    FALSE, /* allow_unver_obstructions */
                                    ctx,
                                    pool);
  if (err)
    goto cleanup;

  /* Do some additional work if the file external is newly added to
     the wc. */
  if (unlink_file)
    {
      /* At this point the newly created file external is switched, so
         if there is an error, to back everything out the file cannot
         be reverted, it needs to be removed forcibly from the wc.. */
      revert_file = FALSE;
      remove_from_revision_control = TRUE;

      if (err)
        goto cleanup;
  }

  if (close_adm_access)
    SVN_ERR(svn_wc_adm_close2(target_adm_access, subpool));

  /* ### should destroy the subpool... */

  return SVN_NO_ERROR;

 cleanup:
  if (revert_file)
    {
      svn_error_t *e =
        svn_wc_revert3(path, target_adm_access, svn_depth_empty,
                       use_commit_times,
                       NULL, /* apr_array_header_t *changelists */
                       ctx->cancel_func,
                       ctx->cancel_baton,
                       NULL, /* svn_wc_notify_func2_t */
                       NULL, /* void *notify_baton */
                       subpool);
      if (e)
        svn_error_clear(e);
    }

  if (remove_from_revision_control)
    {
      svn_error_t * e = svn_wc_remove_from_revision_control(target_adm_access,
                                                            target,
                                                            TRUE, FALSE,
                                                            ctx->cancel_func,
                                                            ctx->cancel_baton,
                                                            subpool);
      if (e)
        svn_error_clear(e);
    }

  if (unlink_file)
    {
      svn_error_t *e = svn_io_remove_file(path, subpool);
      if (e)
        svn_error_clear(e);
    }

  if (close_adm_access)
    SVN_ERR(svn_wc_adm_close2(target_adm_access, subpool));

  /* ### should destroy the subpool */

  return err;
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
  const char *uncanonicalized_url = item->url;
  const char *canonicalized_url;
  apr_uri_t parent_dir_parsed_uri;
  apr_status_t status;

  canonicalized_url = svn_path_canonicalize(uncanonicalized_url, pool);

  /* If the URL is already absolute, there is nothing to do. */
  if (svn_path_is_url(canonicalized_url))
    {
      item->url = canonicalized_url;
      return SVN_NO_ERROR;
    }

  /* Parse the parent directory URL into its parts. */
  status = apr_uri_parse(pool, parent_dir_url, &parent_dir_parsed_uri);
  if (status)
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("Illegal parent directory URL '%s'"),
                             parent_dir_url);

  /* If the parent directory URL is at the server root, then the URL
     may have no / after the hostname so apr_uri_parse() will leave
     the URL's path as NULL. */
  if (! parent_dir_parsed_uri.path)
    parent_dir_parsed_uri.path = apr_pstrmemdup(pool, "/", 1);

  /* Handle URLs relative to the current directory or to the
     repository root.  The backpaths may only remove path elements,
     not the hostname.  This allows an external to refer to another
     repository in the same server relative to the location of this
     repository, say using SVNParentPath. */
  if ((0 == strncmp("../", uncanonicalized_url, 3)) ||
      (0 == strncmp("^/", uncanonicalized_url, 2)))
    {
      apr_array_header_t *base_components;
      apr_array_header_t *relative_components;
      int i;

      /* Decompose either the parent directory's URL path or the
         repository root's URL path into components.  */
      if (0 == strncmp("../", uncanonicalized_url, 3))
        {
          base_components = svn_path_decompose(parent_dir_parsed_uri.path,
                                               pool);
          relative_components = svn_path_decompose(canonicalized_url, pool);
        }
      else
        {
          apr_uri_t repos_root_parsed_uri;

          status = apr_uri_parse(pool, repos_root_url, &repos_root_parsed_uri);
          if (status)
            return svn_error_createf(SVN_ERR_BAD_URL, 0,
                                     _("Illegal repository root URL '%s'"),
                                     repos_root_url);

          /* If the repository root URL is at the server root, then
             the URL may have no / after the hostname so
             apr_uri_parse() will leave the URL's path as NULL. */
          if (! repos_root_parsed_uri.path)
            repos_root_parsed_uri.path = apr_pstrmemdup(pool, "/", 1);

          base_components = svn_path_decompose(repos_root_parsed_uri.path,
                                               pool);
          relative_components = svn_path_decompose(canonicalized_url + 2,
                                                   pool);
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

      parent_dir_parsed_uri.path = (char *)svn_path_compose(base_components,
                                                            pool);
      parent_dir_parsed_uri.query = NULL;
      parent_dir_parsed_uri.fragment = NULL;

      item->url = apr_uri_unparse(pool, &parent_dir_parsed_uri, 0);

      return SVN_NO_ERROR;
    }

  /* The remaining URLs are relative to the either the scheme or
     server root and can only refer to locations inside that scope, so
     backpaths are not allowed. */
  if (svn_path_is_backpath_present(canonicalized_url + 2))
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("The external relative URL '%s' cannot have "
                               "backpaths, i.e. '..'"),
                             uncanonicalized_url);

  /* Relative to the scheme. */
  if (0 == strncmp("//", uncanonicalized_url, 2))
    {
      const char *scheme;

      SVN_ERR(uri_scheme(&scheme, repos_root_url, pool));
      item->url = svn_path_canonicalize(apr_pstrcat(pool,
                                                    scheme,
                                                    ":",
                                                    uncanonicalized_url,
                                                    NULL),
                                        pool);
      return SVN_NO_ERROR;
    }

  /* Relative to the server root. */
  if (uncanonicalized_url[0] == '/')
    {
      parent_dir_parsed_uri.path = (char *)canonicalized_url;
      parent_dir_parsed_uri.query = NULL;
      parent_dir_parsed_uri.fragment = NULL;

      item->url = apr_uri_unparse(pool, &parent_dir_parsed_uri, 0);

      return SVN_NO_ERROR;
    }

  return svn_error_createf(SVN_ERR_BAD_URL, 0,
                           _("Unrecognized format for the relative external "
                             "URL '%s'"),
                           uncanonicalized_url);
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
  const char *parent;
  const char *path = svn_path_join(ib->parent_dir,
                                   (const char *) key, ib->iter_pool);
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
                                               ib->pool));

      SVN_ERR(svn_ra_get_uuid2(ra_session, &ra_cache.repos_uuid, ib->pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &ra_cache.repos_root_url,
                                     ib->pool));
      SVN_ERR(svn_ra_check_path(ra_session, "", ra_cache.ra_revnum, &kind,
                                ib->pool));

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

      /* First notify that we're about to handle an external. */
      if (ib->ctx->notify_func2)
        (*ib->ctx->notify_func2)
          (ib->ctx->notify_baton2,
           svn_wc_create_notify(path, svn_wc_notify_update_external,
                                ib->iter_pool), ib->iter_pool);

      switch (*ra_cache.kind_p)
        {
        case svn_node_dir:
          /* The target dir might have multiple components.  Guarantee
             the path leading down to the last component. */
          svn_path_split(path, &parent, NULL, ib->iter_pool);
          SVN_ERR(svn_io_make_dir_recursively(parent, ib->iter_pool));

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
            SVN_ERR(svn_client_export4(NULL, new_item->url, path,
                                       &(new_item->peg_revision),
                                       &(new_item->revision),
                                       TRUE, FALSE, svn_depth_infinity, NULL,
                                       ib->ctx, ib->iter_pool));
          else
            SVN_ERR(svn_client__checkout_internal
                    (NULL, new_item->url, path,
                     &(new_item->peg_revision), &(new_item->revision),
                     &ra_cache,
                     SVN_DEPTH_INFINITY_OR_FILES(TRUE),
                     FALSE, FALSE, ib->timestamp_sleep, ib->ctx,
                     ib->iter_pool));
          break;
        case svn_node_file:
          if (ib->is_export)
            /* Do not overwrite an existing file with this file
               external. */
            SVN_ERR(svn_client_export4(NULL, new_item->url, path,
                                       &(new_item->peg_revision),
                                       &(new_item->revision),
                                       FALSE, TRUE, svn_depth_infinity, NULL,
                                       ib->ctx, ib->iter_pool));
          else
            SVN_ERR(switch_file_external(path,
                                         new_item->url,
                                         &new_item->peg_revision,
                                         &new_item->revision,
                                         ib->adm_access,
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
      svn_wc_adm_access_t *adm_access;
      svn_boolean_t close_access_baton_when_done;

      const char *what_to_remove;

      /* Determine if a directory or file external is being removed.
         Try to handle the case when the user deletes the external by
         hand or it is missing.  First try to open the directory for a
         directory external.  If that doesn't work, get the access
         baton for the parent directory and remove the entry for the
         external. */
      err = svn_wc_adm_open3(&adm_access, NULL, path, TRUE, -1,
                             ib->ctx->cancel_func, ib->ctx->cancel_baton,
                             ib->iter_pool);
      if (err)
        {
          const char *anchor;
          const char *target;
          svn_error_t *err2;

          SVN_ERR(svn_wc_get_actual_target(path, &anchor, &target,
                                           ib->iter_pool));

          err2 = svn_wc_adm_retrieve(&adm_access, ib->adm_access, anchor,
                                     ib->iter_pool);
          if (err2)
            {
              svn_error_clear(err2);
              return err;
            }
          else
            {
              svn_error_clear(err);
            }
          close_access_baton_when_done = FALSE;
          what_to_remove = target;
        }
      else
        {
          close_access_baton_when_done = TRUE;
          what_to_remove = SVN_WC_ENTRY_THIS_DIR;
        }

      /* We don't use relegate_dir_external() here, because we know that
         nothing else in this externals description (at least) is
         going to need this directory, and therefore it's better to
         leave stuff where the user expects it. */
      err = svn_wc_remove_from_revision_control
        (adm_access, what_to_remove, TRUE, FALSE,
         ib->ctx->cancel_func, ib->ctx->cancel_baton, ib->iter_pool);

      /* ### Ugly. Unlock only if not going to return an error. Revisit */
      if (close_access_baton_when_done &&
          (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
        {
          svn_error_t *err2 = svn_wc_adm_close2(adm_access, ib->iter_pool);
          if (err2)
            {
              if (!err)
                err = err2;
              else
                svn_error_clear(err2);
            }
        }

      if (err && (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD))
        return err;
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
           svn_wc_create_notify(path, svn_wc_notify_update_external,
                                ib->iter_pool), ib->iter_pool);

      /* Either the URL changed, or the exact same item is present in
         both hashes, and caller wants to update such unchanged items.
         In the latter case, the call below will try to make sure that
         the external really is a WC pointing to the correct
         URL/revision. */
      switch (*ra_cache.kind_p)
        {
        case svn_node_dir:
          SVN_ERR(switch_dir_external(path, new_item->url,
                                      &(new_item->revision),
                                      &(new_item->peg_revision),
                                      ib->timestamp_sleep, ib->ctx,
                                      ib->iter_pool));
          break;
        case svn_node_file:
          SVN_ERR(switch_file_external(path,
                                       new_item->url,
                                       &new_item->peg_revision,
                                       &new_item->revision,
                                       ib->adm_access,
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
  if (err && ib->ctx->notify_func2)
    {
      const char *path = svn_path_join(ib->parent_dir, key, ib->iter_pool);
      svn_wc_notify_t *notifier =
        svn_wc_create_notify(path,
                             svn_wc_notify_failed_external,
                             ib->pool);
      notifier->err = err;
      ib->ctx->notify_func2(ib->ctx->notify_baton2, notifier, ib->pool);
    }
  svn_error_clear(err);
  return SVN_NO_ERROR;
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
  const char *to_path;

  /* Passed through to handle_external_item_change_baton. */
  svn_wc_adm_access_t *adm_access;
  svn_client_ctx_t *ctx;
  const char *repos_root_url;
  svn_boolean_t *timestamp_sleep;
  svn_boolean_t is_export;

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

  if (cb->is_export)
    SVN_ERR_ASSERT(!cb->adm_access);
  else
    SVN_ERR_ASSERT(cb->adm_access);

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
  ib.parent_dir        = (const char *) key;
  ib.repos_root_url    = cb->repos_root_url;
  ib.adm_access        = cb->adm_access;
  ib.ctx               = cb->ctx;
  ib.is_export         = cb->is_export;
  ib.timestamp_sleep   = cb->timestamp_sleep;
  ib.pool              = cb->pool;
  ib.iter_pool         = svn_pool_create(cb->pool);

  /* Get the URL of the parent directory by appending a portion of
     parent_dir to from_url.  from_url is the URL for to_path and
     to_path is a substring of parent_dir, so append any characters in
     parent_dir past strlen(to_path) to from_url (making sure to move
     past a '/' in parent_dir, otherwise svn_path_url_add_component()
     will error. */
  len = strlen(cb->to_path);
  if (ib.parent_dir[len] == '/')
    ++len;
  ib.parent_dir_url = svn_path_url_add_component2(cb->from_url,
                                                  ib.parent_dir + len,
                                                  cb->pool);

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
svn_client__handle_externals(svn_wc_adm_access_t *adm_access,
                             svn_wc_traversal_info_t *traversal_info,
                             const char *from_url,
                             const char *to_path,
                             const char *repos_root_url,
                             svn_depth_t requested_depth,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new, *ambient_depths;
  struct handle_externals_desc_change_baton cb = { 0 };

  svn_wc_edited_externals(&externals_old, &externals_new, traversal_info);
  svn_wc_traversed_depths(&ambient_depths, traversal_info);

  /* Sanity check; see r30124. */
  if (! svn_path_is_url(from_url))
    return svn_error_createf
      (SVN_ERR_BAD_URL, NULL, _("'%s' is not a URL"), from_url);

  cb.externals_new     = externals_new;
  cb.externals_old     = externals_old;
  cb.requested_depth   = requested_depth;
  cb.ambient_depths    = ambient_depths;
  cb.from_url          = from_url;
  cb.to_path           = to_path;
  cb.repos_root_url    = repos_root_url;
  cb.adm_access        = adm_access;
  cb.ctx               = ctx;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.is_export         = FALSE;
  cb.pool              = pool;

  return svn_hash_diff(cb.externals_old, cb.externals_new,
                       handle_externals_desc_change, &cb, pool);
}


svn_error_t *
svn_client__fetch_externals(apr_hash_t *externals,
                            const char *from_url,
                            const char *to_path,
                            const char *repos_root_url,
                            svn_depth_t requested_depth,
                            svn_boolean_t is_export,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  struct handle_externals_desc_change_baton cb = { 0 };

  cb.externals_new     = externals;
  cb.externals_old     = apr_hash_make(pool);
  cb.requested_depth   = requested_depth;
  cb.ambient_depths    = NULL;
  cb.adm_access        = NULL;
  cb.ctx               = ctx;
  cb.from_url          = from_url;
  cb.to_path           = to_path;
  cb.repos_root_url    = repos_root_url;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.is_export         = is_export;
  cb.pool              = pool;

  return svn_hash_diff(cb.externals_old, cb.externals_new,
                       handle_externals_desc_change, &cb, pool);
}


svn_error_t *
svn_client__do_external_status(svn_wc_traversal_info_t *traversal_info,
                               svn_wc_status_func3_t status_func,
                               void *status_baton,
                               svn_depth_t depth,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Get the values of the svn:externals properties. */
  svn_wc_edited_externals(&externals_old, &externals_new, traversal_info);

  /* Loop over the hash of new values (we don't care about the old
     ones).  This is a mapping of versioned directories to property
     values. */
  for (hi = apr_hash_first(pool, externals_new);
       hi;
       hi = apr_hash_next(hi))
    {
      apr_array_header_t *exts;
      const void *key;
      void *val;
      const char *path;
      const char *propval;
      apr_pool_t *iterpool;
      int i;

      /* Clear the subpool. */
      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      propval = val;

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
          fullpath = svn_path_join(path, external->target_dir, iterpool);

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
          SVN_ERR(svn_client_status4(NULL, fullpath,
                                     &(external->revision),
                                     status_func, status_baton,
                                     depth, get_all, update,
                                     no_ignore, FALSE, NULL, ctx, iterpool));
        }
    }

  /* Destroy SUBPOOL and (implicitly) ITERPOOL. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
