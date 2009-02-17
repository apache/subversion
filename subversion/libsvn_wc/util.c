/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
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

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_io.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"

#include "wc.h"   /* just for prototypes of things in this .c file */
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


svn_error_t *
svn_wc__ensure_directory(const char *path,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err = svn_io_check_path(path, &kind, pool);

  if (err)
    return err;

  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_error_createf(APR_ENOTDIR, NULL,
                               _("'%s' is not a directory"),
                               svn_path_local_style(path, pool));
    }
  else if (kind == svn_node_none)
    {
      /* The dir doesn't exist, and it's our job to change that. */

      err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);

      if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* Tried to create the dir, and encountered some problem
             other than non-existence of intermediate dirs.  We can't
             ensure the desired directory's existence, so just return
             the error. */
          return err;
        }
      else if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        /* (redundant conditional and comment) */
        {
          /* Okay, so the problem is a missing intermediate
             directory.  We don't know which one, so we recursively
             back up one level and try again. */
          const char *shorter = svn_path_dirname(path, pool);

          /* Clear the error. */
          svn_error_clear(err);

          if (shorter[0] == '\0')
            {
              /* A weird and probably rare situation. */
              return svn_error_create(0, NULL,
                                      _("Unable to make any directories"));
            }
          else  /* We have a valid path, so recursively ensure it. */
            {
              err = svn_wc__ensure_directory(shorter, pool);

              if (err)
                return (err);
              else
                return svn_wc__ensure_directory(path, pool);
            }
        }

      if (err)
        return err;
    }
  else  /* No problem, the dir already existed, so just leave. */
    SVN_ERR_ASSERT(kind == svn_node_dir);

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_wc_version(void)
{
  SVN_VERSION_BODY;
}

svn_wc_notify_t *
svn_wc_create_notify(const char *path,
                     svn_wc_notify_action_t action,
                     apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_pcalloc(pool, sizeof(*ret));
  ret->path = path;
  ret->action = action;
  ret->kind = svn_node_unknown;
  ret->content_state = ret->prop_state = svn_wc_notify_state_unknown;
  ret->lock_state = svn_wc_notify_lock_state_unknown;
  ret->revision = SVN_INVALID_REVNUM;

  return ret;
}

svn_wc_notify_t *
svn_wc_create_notify_url(const char *url,
                         svn_wc_notify_action_t action,
                         apr_pool_t *pool)
{
  svn_wc_notify_t *ret = svn_wc_create_notify(".", action, pool);
  ret->url = url;

  return ret;
}

/* Pool cleanup function to clear an svn_error_t *. */
static apr_status_t err_cleanup(void *data)
{
  svn_error_clear(data);

  return APR_SUCCESS;
}

svn_wc_notify_t *
svn_wc_dup_notify(const svn_wc_notify_t *notify,
                  apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc(pool, sizeof(*ret));

  *ret = *notify;

  if (ret->path)
    ret->path = apr_pstrdup(pool, ret->path);
  if (ret->mime_type)
    ret->mime_type = apr_pstrdup(pool, ret->mime_type);
  if (ret->lock)
    ret->lock = svn_lock_dup(ret->lock, pool);
  if (ret->err)
    {
      ret->err = svn_error_dup(ret->err);
      apr_pool_cleanup_register(pool, ret->err, err_cleanup,
                                apr_pool_cleanup_null);
    }
  if (ret->changelist_name)
    ret->changelist_name = apr_pstrdup(pool, ret->changelist_name);
  if (ret->merge_range)
    ret->merge_range = svn_merge_range_dup(ret->merge_range, pool);
  if (ret->url)
    ret->url = apr_pstrdup(pool, ret->url);
  if (ret->path_prefix)
    ret->path_prefix = apr_pstrdup(pool, ret->path_prefix);
  if (ret->prop_name)
    ret->prop_name = apr_pstrdup(pool, ret->prop_name);
  if (ret->rev_props)
    ret->rev_props = svn_prop_hash_dup(ret->rev_props, pool);

  return ret;
}

svn_error_t *
svn_wc_external_item_create(const svn_wc_external_item2_t **item,
                            apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_wc_external_item2_t));
  return SVN_NO_ERROR;
}

svn_wc_external_item_t *
svn_wc_external_item_dup(const svn_wc_external_item_t *item,
                         apr_pool_t *pool)
{
  svn_wc_external_item_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}

svn_wc_external_item2_t *
svn_wc_external_item2_dup(const svn_wc_external_item2_t *item,
                          apr_pool_t *pool)
{
  svn_wc_external_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}

void svn_wc__compat_call_notify_func(void *baton,
                                     const svn_wc_notify_t *n,
                                     apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t *nb = baton;

  if (nb->func)
    (*nb->func)(nb->baton, n->path, n->action, n->kind, n->mime_type,
                n->content_state, n->prop_state, n->revision);
}

svn_boolean_t
svn_wc_match_ignore_list(const char *str, apr_array_header_t *list,
                         apr_pool_t *pool)
{
  /* For now, we simply forward to svn_cstring_match_glob_list. In the
     future, if we support more complex ignore patterns, we would iterate
     over 'list' ourselves, and decide for each pattern how to handle
     it. */

  return svn_cstring_match_glob_list(str, list);
}

svn_error_t *
svn_wc__path_switched(const char *wc_path,
                      svn_boolean_t *switched,
                      const svn_wc_entry_t *entry,
                      apr_pool_t *pool)
{
  const char *wc_parent_path, *parent_child_url;
  const svn_wc_entry_t *parent_entry;
  svn_wc_adm_access_t *parent_adm_access;
  svn_error_t *err;

  SVN_ERR(svn_path_get_absolute(&wc_path, wc_path, pool));

  if (svn_dirent_is_root(wc_path, strlen(wc_path)))
    {
      *switched = FALSE;
      return SVN_NO_ERROR;
    }

  wc_parent_path = svn_path_dirname(wc_path, pool);
  err = svn_wc_adm_open3(&parent_adm_access, NULL, wc_parent_path, FALSE, 0,
                         NULL, NULL, pool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          *switched = FALSE;
        }

      return err;
    }

  SVN_ERR(svn_wc__entry_versioned(&parent_entry, wc_parent_path,
                                  parent_adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close2(parent_adm_access, pool));

  /* Without complete entries (and URLs) for WC_PATH and it's parent
     we return SVN_ERR_ENTRY_MISSING_URL. */
  if (!parent_entry->url || !entry->url)
    {
      const char *no_url_path = parent_entry->url ? wc_path : wc_parent_path;
      return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                               _("Cannot find a URL for '%s'"),
                               svn_path_local_style(no_url_path, pool));
    }

  parent_child_url
    = svn_path_url_add_component2(parent_entry->url,
                                  svn_path_basename(wc_path, pool), pool);
  *switched = strcmp(parent_child_url, entry->url) != 0;

  return SVN_NO_ERROR;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_text(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = svn_node_file;
  conflict->kind = svn_wc_conflict_kind_text;
  conflict->access = adm_access;
  conflict->action = svn_wc_conflict_action_edit;
  conflict->reason = svn_wc_conflict_reason_edited;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_prop(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        svn_node_kind_t node_kind,
                                        const char *property_name,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_property;
  conflict->access = adm_access;
  conflict->property_name = property_name;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_tree(
                            const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_node_kind_t node_kind,
                            svn_wc_operation_t operation,
                            svn_wc_conflict_version_t *src_left_version,
                            svn_wc_conflict_version_t *src_right_version,
                            apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_tree;
  conflict->access = adm_access;
  conflict->operation = operation;
  conflict->src_left_version = src_left_version;
  conflict->src_right_version = src_right_version;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc__conflict_description_dup(const svn_wc_conflict_description_t *conflict,
                                 apr_pool_t *pool)
{
  svn_wc_conflict_description_t *new_conflict;

  new_conflict = apr_pcalloc(pool, sizeof(*new_conflict));

  /* Shallow copy all members. */
  *new_conflict = *conflict;

  if (conflict->path)
    new_conflict->path = apr_pstrdup(pool, conflict->path);
  if (conflict->property_name)
    new_conflict->property_name = apr_pstrdup(pool, conflict->property_name);
  if (conflict->mime_type)
    new_conflict->mime_type = apr_pstrdup(pool, conflict->mime_type);
  /* NOTE: We cannot make a deep copy of adm_access. */
  if (conflict->base_file)
    new_conflict->base_file = apr_pstrdup(pool, conflict->base_file);
  if (conflict->their_file)
    new_conflict->their_file = apr_pstrdup(pool, conflict->their_file);
  if (conflict->my_file)
    new_conflict->my_file = apr_pstrdup(pool, conflict->my_file);
  if (conflict->merged_file)
    new_conflict->merged_file = apr_pstrdup(pool, conflict->merged_file);
  if (conflict->src_left_version)
    new_conflict->src_left_version =
      svn_wc_conflict_version_dup(conflict->src_left_version, pool);
  if (conflict->src_right_version)
    new_conflict->src_right_version =
      svn_wc_conflict_version_dup(conflict->src_right_version, pool);

  return new_conflict;
}

svn_wc_conflict_version_t *
svn_wc_conflict_version_create(const char *repos_url,
                               const char* path_in_repos,
                               svn_revnum_t peg_rev,
                               svn_node_kind_t node_kind,
                               apr_pool_t *pool)
{
  svn_wc_conflict_version_t *version;

  version = apr_pcalloc(pool, sizeof(*version));

  version->repos_url = repos_url;
  version->peg_rev = peg_rev;
  version->path_in_repos = path_in_repos;
  version->node_kind = node_kind;

  return version;
}


svn_wc_conflict_version_t *
svn_wc_conflict_version_dup(const svn_wc_conflict_version_t *version,
                            apr_pool_t *pool)
{

  svn_wc_conflict_version_t *new_version;

  new_version = apr_pcalloc(pool, sizeof(*new_version));

  /* Shallow copy all members. */
  *new_version = *version;

  if (version->repos_url)
    new_version->repos_url = apr_pstrdup(pool, version->repos_url);

  if (version->path_in_repos)
    new_version->path_in_repos = apr_pstrdup(pool, version->path_in_repos);

  return new_version;
}

