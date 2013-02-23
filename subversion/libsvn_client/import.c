/*
 * import.c:  wrappers around import functionality.
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

#include <string.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_md5.h>

#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_subst.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_hash.h"
#include "svn_props.h"

#include "client.h"
#include "private/svn_subr_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_magic.h"

#include "svn_private_config.h"

/* Import context baton.

   ### TODO:  Add the following items to this baton:
      /` import editor/baton. `/
      const svn_delta_editor_t *editor;
      void *edit_baton;

      /` Client context baton `/
      svn_client_ctx_t `ctx;

      /` Paths (keys) excluded from the import (values ignored) `/
      apr_hash_t *excludes;
*/
typedef struct import_ctx_t
{
  /* Whether any changes were made to the repository */
  svn_boolean_t repos_changed;

  /* A magic cookie for mime-type detection. */
  svn_magic__cookie_t *magic_cookie;

  /* Collection of all possible configuration file dictated auto-props and
     svn:auto-props.  A hash mapping const char * file patterns to a
     second hash which maps const char * property names to const char *
     property values.  Properties which don't have a value, e.g.
     svn:executable, simply map the property name to an empty string.
     May be NULL if autoprops are disabled. */
  apr_hash_t *autoprops;
} import_ctx_t;


/* Import file PATH as EDIT_PATH in the repository directory indicated
 * by DIR_BATON in EDITOR.
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON
 * for each file.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file(svn_editor_t *editor,
            const char *local_abspath,
            const char *relpath,
            const svn_io_dirent2_t *dirent,
            import_ctx_t *import_ctx,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  const char *mimetype = NULL;
  svn_stream_t *contents;
  svn_checksum_t *checksum;
  apr_hash_t* properties = NULL;

  SVN_ERR(svn_path_check_valid(local_abspath, pool));

  /* Remember that the repository was modified */
  import_ctx->repos_changed = TRUE;

  if (! dirent->special)
    {
      /* add automatic properties */
      SVN_ERR(svn_client__get_paths_auto_props(&properties, &mimetype,
                                               local_abspath,
                                               import_ctx->magic_cookie,
                                               import_ctx->autoprops,
                                               ctx, pool, pool));
    }

  if (!properties)
    properties = apr_hash_make(pool);

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_commit_added,
                               pool);
      notify->kind = svn_node_file;
      notify->mime_type = mimetype;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  /* If this is a special file, we need to set the svn:special
     property and create a temporary detranslated version in order to
     send to the server. */
  if (dirent->special)
    apr_hash_set(properties, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING,
                 svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool));

  /* Now, transmit the file contents. */
  SVN_ERR(svn_client__get_detranslated_stream(&contents, &checksum, NULL,
                                              local_abspath, properties, TRUE,
                                              pool, pool));

  SVN_ERR(svn_editor_add_file(editor, relpath, checksum, contents, properties,
                              SVN_INVALID_REVNUM));

  return SVN_NO_ERROR;
}


/* Return in CHILDREN a mapping of basenames to dirents for the importable
 * children of DIR_ABSPATH.  EXCLUDES is a hash of absolute paths to filter
 * out.  IGNORES and GLOBAL_IGNORES, if non-NULL, are lists of basename
 * patterns to filter out.
 * FILTER_CALLBACK and FILTER_BATON will be called for each absolute path,
 * allowing users to further filter the list of returned entries.
 *
 * Results are returned in RESULT_POOL; use SCRATCH_POOL for temporary data.*/
static svn_error_t *
get_filtered_children(apr_hash_t **children,
                      const char *dir_abspath,
                      apr_hash_t *excludes,
                      apr_array_header_t *ignores,
                      apr_array_header_t *global_ignores,
                      svn_client_import_filter_func_t filter_callback,
                      void *filter_baton,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_io_get_dirents3(&dirents, dir_abspath, TRUE, result_pool,
                              scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *base_name = svn__apr_hash_index_key(hi);
      const svn_io_dirent2_t *dirent = svn__apr_hash_index_val(hi);
      const char *local_abspath;

      svn_pool_clear(iterpool);

      local_abspath = svn_dirent_join(dir_abspath, base_name, iterpool);

      if (svn_wc_is_adm_dir(base_name, iterpool))
        {
          /* If someone's trying to import a directory named the same
             as our administrative directories, that's probably not
             what they wanted to do.  If they are importing a file
             with that name, something is bound to blow up when they
             checkout what they've imported.  So, just skip items with
             that name.  */
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify
                = svn_wc_create_notify(svn_dirent_join(local_abspath, base_name,
                                                       iterpool),
                                       svn_wc_notify_skip, iterpool);
              notify->kind = svn_node_dir;
              notify->content_state = notify->prop_state
                = svn_wc_notify_state_inapplicable;
              notify->lock_state = svn_wc_notify_lock_state_inapplicable;
              (*ctx->notify_func2)(ctx->notify_baton2, notify, iterpool);
            }

          apr_hash_set(dirents, base_name, APR_HASH_KEY_STRING, NULL);
          continue;
        }
            /* If this is an excluded path, exclude it. */
      if (apr_hash_get(excludes, local_abspath, APR_HASH_KEY_STRING))
        {
          apr_hash_set(dirents, base_name, APR_HASH_KEY_STRING, NULL);
          continue;
        }

      if (ignores && svn_wc_match_ignore_list(base_name, ignores, iterpool))
        {
          apr_hash_set(dirents, base_name, APR_HASH_KEY_STRING, NULL);
          continue;
        }

      if (global_ignores &&
          svn_wc_match_ignore_list(base_name, global_ignores, iterpool))
        {
          apr_hash_set(dirents, base_name, APR_HASH_KEY_STRING, NULL);
          continue;
        }

      if (filter_callback)
        {
          svn_boolean_t filter = FALSE;

          SVN_ERR(filter_callback(filter_baton, &filter, local_abspath,
                                  dirent, iterpool));

          if (filter)
            {
              apr_hash_set(dirents, base_name, APR_HASH_KEY_STRING, NULL);
              continue;
            }
        }
    }
  svn_pool_destroy(iterpool);

  *children = dirents;
  return SVN_NO_ERROR;
}

static svn_error_t *
import_dir(svn_editor_t *editor,
           const char *local_abspath,
           const char *relpath,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *global_ignores,
           svn_boolean_t no_ignore,
           svn_boolean_t no_autoprops,
           svn_boolean_t ignore_unknown_node_types,
           svn_client_import_filter_func_t filter_callback,
           void *filter_baton,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool);


/* Import the children of DIR_ABSPATH, with other arguments similar to
 * import_dir(). */
static svn_error_t *
import_children(const char *dir_abspath,
                const char *dir_relpath,
                apr_hash_t *dirents,
                svn_editor_t *editor,
                svn_depth_t depth,
                apr_hash_t *excludes,
                apr_array_header_t *global_ignores,
                svn_boolean_t no_ignore,
                svn_boolean_t no_autoprops,
                svn_boolean_t ignore_unknown_node_types,
                svn_client_import_filter_func_t filter_callback,
                void *filter_baton,
                import_ctx_t *import_ctx,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *sorted_dirents;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  sorted_dirents = svn_sort__hash(dirents, svn_sort_compare_items_lexically,
                                  scratch_pool);
  for (i = 0; i < sorted_dirents->nelts; i++)
    {
      const char *local_abspath;
      const char *relpath;
      svn_sort__item_t item = APR_ARRAY_IDX(sorted_dirents, i,
                                            svn_sort__item_t);
      const char *base_name = item.key;
      const svn_io_dirent2_t *dirent = item.value;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Typically, we started importing from ".", in which case
         edit_path is "".  So below, this_path might become "./blah",
         and this_edit_path might become "blah", for example. */
      local_abspath = svn_dirent_join(dir_abspath, base_name, iterpool);
      relpath = svn_relpath_join(dir_relpath, base_name, iterpool);

      if (dirent->kind == svn_node_dir && depth >= svn_depth_immediates)
        {
          /* Recurse. */
          svn_depth_t depth_below_here = depth;
          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(import_dir(editor, local_abspath, relpath,
                             depth_below_here, excludes, global_ignores,
                             no_ignore, no_autoprops,
                             ignore_unknown_node_types,
                             filter_callback, filter_baton,
                             import_ctx, ctx, iterpool));
        }
      else if (dirent->kind == svn_node_file && depth >= svn_depth_files)
        {
          SVN_ERR(import_file(editor, local_abspath, relpath, dirent,
                              import_ctx, ctx, iterpool));
        }
      else if (dirent->kind != svn_node_dir && dirent->kind != svn_node_file)
        {
          if (ignore_unknown_node_types)
            {
              /*## warn about it*/
              if (ctx->notify_func2)
                {
                  svn_wc_notify_t *notify
                    = svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_skip, iterpool);
                  notify->kind = svn_node_dir;
                  notify->content_state = notify->prop_state
                    = svn_wc_notify_state_inapplicable;
                  notify->lock_state = svn_wc_notify_lock_state_inapplicable;
                  (*ctx->notify_func2)(ctx->notify_baton2, notify, iterpool);
                }
            }
          else
            return svn_error_createf
              (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
               _("Unknown or unversionable type for '%s'"),
               svn_dirent_local_style(local_abspath, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Import directory LOCAL_ABSPATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  EDIT_PATH is the path imported as the root
 * directory, so all edits are relative to that.
 *
 * DEPTH is the depth at this point in the descent (it may be changed
 * for recursive calls).
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 *
 * GLOBAL_IGNORES is an array of const char * ignore patterns.  Any child
 * of LOCAL_ABSPATH which matches one or more of the patterns is not imported.
 *
 * If NO_IGNORE is FALSE, don't import files or directories that match
 * ignore patterns.
 *
 * If FILTER_CALLBACK is not NULL, call it with FILTER_BATON on each to be
 * imported node below LOCAL_ABSPATH to allow filtering nodes.
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for each
 * directory.
 *
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_dir(svn_editor_t *editor,
           const char *local_abspath,
           const char *relpath,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *global_ignores,
           svn_boolean_t no_ignore,
           svn_boolean_t no_autoprops,
           svn_boolean_t ignore_unknown_node_types,
           svn_client_import_filter_func_t filter_callback,
           void *filter_baton,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_array_header_t *children;
  apr_hash_t *props = apr_hash_make(pool);

  SVN_ERR(svn_path_check_valid(local_abspath, pool));
  SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes, NULL,
                                global_ignores, filter_callback,
                                filter_baton, ctx, pool, pool));

  /* Import this directory, but not yet its children. */
  SVN_ERR(svn_hash_keys(&children, dirents, pool));
  SVN_ERR(svn_editor_add_directory(editor, relpath, children, props,
                                   SVN_INVALID_REVNUM));

  /* Remember that the repository was modified */
  import_ctx->repos_changed = TRUE;

  /* By notifying before the recursive call below, we display
     a directory add before displaying adds underneath the
     directory.  To do it the other way around, just move this
     after the recursive call. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_commit_added,
                               pool);
      notify->kind = svn_node_dir;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  /* Now import the children recursively. */
  SVN_ERR(import_children(local_abspath, relpath, dirents, editor, depth,
                          excludes, global_ignores, no_ignore, no_autoprops,
                          ignore_unknown_node_types,
                          filter_callback, filter_baton,
                          import_ctx, ctx, pool));

  return SVN_NO_ERROR;
}


/* Recursively import PATH to a repository using EDITOR and
 * EDIT_BATON.  PATH can be a file or directory.
 *
 * DEPTH is the depth at which to import PATH; it behaves as for
 * svn_client_import4().
 *
 * NEW_ENTRIES is an ordered array of path components that must be
 * created in the repository (where the ordering direction is
 * parent-to-child).  If PATH is a directory, NEW_ENTRIES may be empty
 * -- the result is an import which creates as many new entries in the
 * top repository target directory as there are importable entries in
 * the top of PATH; but if NEW_ENTRIES is not empty, its last item is
 * the name of a new subdirectory in the repository to hold the
 * import.  If PATH is a file, NEW_ENTRIES may not be empty, and its
 * last item is the name used for the file in the repository.  If
 * NEW_ENTRIES contains more than one item, all but the last item are
 * the names of intermediate directories that are created before the
 * real import begins.  NEW_ENTRIES may NOT be NULL.
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 *
 * AUTOPROPS is hash of all config file autoprops and
 * svn:auto-props inherited by the import target, see the
 * IMPORT_CTX member of the same name.
 *
 * LOCAL_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:ignore property (if any) set on the root of the
 * repository target and thus dictates which immediate children of that
 * target should be ignored and not imported.
 *
 * GLOBAL_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:global-ignores properties (if any) set on
 * the root of the repository target or inherited by it.
 *
 * If NO_IGNORE is FALSE, don't import files or directories that match
 * ignore patterns.
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for
 * each imported path, passing actions svn_wc_notify_commit_added.
 *
 * Use POOL for any temporary allocation.
 *
 * Note: the repository directory receiving the import was specified
 * when the editor was fetched.  (I.e, when EDITOR->open_root() is
 * called, it returns a directory baton for that directory, which is
 * not necessarily the root.)
 */
static svn_error_t *
import(const char *local_abspath,
       const apr_array_header_t *new_entries,
       svn_editor_t *editor,
       const char *edit_relpath,
       svn_depth_t depth,
       apr_hash_t *excludes,
       apr_hash_t *autoprops,
       apr_array_header_t *local_ignores,
       apr_array_header_t *global_ignores,
       svn_boolean_t no_ignore,
       svn_boolean_t no_autoprops,
       svn_boolean_t ignore_unknown_node_types,
       svn_client_import_filter_func_t filter_callback,
       void *filter_baton,
       svn_client_ctx_t *ctx,
       apr_pool_t *pool)
{
  const char *relpath = edit_relpath == NULL ? "" : edit_relpath;
  import_ctx_t *import_ctx = apr_pcalloc(pool, sizeof(*import_ctx));
  const svn_io_dirent2_t *dirent;
  apr_hash_t *props = apr_hash_make(pool);

  import_ctx->autoprops = autoprops;
  svn_magic__init(&import_ctx->magic_cookie, pool);

  /* Import a file or a directory tree. */
  SVN_ERR(svn_io_stat_dirent(&dirent, local_abspath, FALSE, pool, pool));

  /* Make the intermediate directory components necessary for properly
     rooting our import source tree.  */
  if (new_entries->nelts)
    {
      int i;
      apr_hash_t *dirents;
      apr_pool_t *iterpool = svn_pool_create(pool);

      if (dirent->kind == svn_node_dir)
        {
          /* If we are creating a new repository directory path to import to,
             then we disregard any svn:ignore property. */
          if (!no_ignore && new_entries->nelts)
            local_ignores = NULL;

          SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes,
                                        local_ignores, global_ignores,
                                        filter_callback, filter_baton,
                                        ctx, pool, pool));
        }

      for (i = 0; i < new_entries->nelts; i++)
        {
          apr_array_header_t *children;
          const char *component = APR_ARRAY_IDX(new_entries, i, const char *);

          svn_pool_clear(iterpool);
          relpath = svn_relpath_join(relpath, component, pool);

          /* If this is the last path component, and we're importing a
             file, then this component is the name of the file, not an
             intermediate directory. */
          if ((i == new_entries->nelts - 1) && (dirent->kind == svn_node_file))
            break;

          if (i < new_entries->nelts - 1)
            {
              children = apr_array_make(iterpool, 1, sizeof(const char *));
              APR_ARRAY_PUSH(children, const char *) =
                               APR_ARRAY_IDX(new_entries, i + 1, const char *);
            }
          else
            SVN_ERR(svn_hash_keys(&children, dirents, iterpool));

          SVN_ERR(svn_editor_add_directory(editor, relpath, children, props,
                                           SVN_INVALID_REVNUM));

          /* Remember that the repository was modified */
          import_ctx->repos_changed = TRUE;
        }
    }
  else if (dirent->kind == svn_node_file)
    {
      return svn_error_create
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("New entry name required when importing a file"));
    }

  /* Note that there is no need to check whether PATH's basename is
     the same name that we reserve for our administrative
     subdirectories.  It would be strange -- though not illegal -- to
     import the contents of a directory of that name, because the
     directory's own name is not part of those contents.  Of course,
     if something underneath it also has our reserved name, then we'll
     error. */

  if (dirent->kind == svn_node_file)
    {
      /* This code path ignores EXCLUDES and FILTER, but they don't make
         much sense for a single file import anyway. */
      svn_boolean_t ignores_match = FALSE;

      if (!no_ignore)
        ignores_match =
          (svn_wc_match_ignore_list(local_abspath, global_ignores, pool)
           || svn_wc_match_ignore_list(local_abspath, local_ignores, pool));

      if (!ignores_match)
        SVN_ERR(import_file(editor, local_abspath, relpath,
                            dirent, import_ctx, ctx, pool));
    }
  else if (dirent->kind == svn_node_dir)
    {
      apr_hash_t *dirents;

      /* If we are creating a new repository directory path to import to,
         then we disregard any svn:ignore property. */
      if (!no_ignore && new_entries->nelts)
        local_ignores = NULL;

      SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes,
                                    local_ignores, global_ignores,
                                    filter_callback, filter_baton, ctx,
                                    pool, pool));

      SVN_ERR(import_children(local_abspath, relpath, dirents, editor,
                              depth, excludes, global_ignores, no_ignore,
                              no_autoprops, ignore_unknown_node_types,
                              filter_callback, filter_baton,
                              import_ctx, ctx, pool));

    }
  else if (dirent->kind == svn_node_none
           || dirent->kind == svn_node_unknown)
    {
      return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' does not exist"),
                               svn_dirent_local_style(local_abspath, pool));
    }

  /* Close up shop; it's time to go home. */
  if (import_ctx->repos_changed)
    return svn_error_trace(svn_editor_complete(editor));
  else
    return svn_error_trace(svn_editor_abort(editor));
}



/*** Public Interfaces. ***/

svn_error_t *
svn_client_import5(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t no_autoprops,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_import_filter_func_t filter_callback,
                   void *filter_baton,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const char *log_msg = "";
  svn_editor_t *editor;
  svn_ra_session_t *ra_session;
  apr_hash_t *excludes = apr_hash_make(scratch_pool);
  svn_node_kind_t kind;
  const char *local_abspath;
  apr_array_header_t *new_entries = apr_array_make(scratch_pool, 4,
                                                   sizeof(const char *));
  apr_hash_t *commit_revprops;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *autoprops = NULL;
  apr_array_header_t *global_ignores;
  apr_array_header_t *local_ignores_arr;
  const char *edit_relpath;
  const char *repos_root;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  /* Create a new commit item and add it to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* If there's a log message gatherer, create a temporary commit
         item array solely to help generate the log message.  The
         array is not used for the import itself. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(scratch_pool, 1, sizeof(item));

      item = svn_client_commit_item3_create(scratch_pool);
      item->path = apr_pstrdup(scratch_pool, path);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, scratch_pool));
      if (! log_msg)
        return SVN_NO_ERROR;
      if (tmp_file)
        {
          const char *abs_path;
          SVN_ERR(svn_dirent_get_absolute(&abs_path, tmp_file, scratch_pool));
          apr_hash_set(excludes, abs_path, APR_HASH_KEY_STRING, (void *)1);
        }
    }

  SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, url, NULL,
                                               NULL, FALSE, TRUE, ctx,
                                               scratch_pool));

  /* Figure out all the path components we need to create just to have
     a place to stick our imported tree. */
  SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                            iterpool));

  /* We can import into directories, but if a file already exists, that's
     an error. */
  if (kind == svn_node_file)
    return svn_error_createf
      (SVN_ERR_ENTRY_EXISTS, NULL,
       _("Path '%s' already exists"), url);

  while (kind == svn_node_none)
    {
      const char *dir;

      svn_pool_clear(iterpool);

      svn_uri_split(&url, &dir, url, scratch_pool);
      APR_ARRAY_PUSH(new_entries, const char *) = dir;
      SVN_ERR(svn_ra_reparent(ra_session, url, iterpool));

      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                                iterpool));
    }

  /* Reverse the order of the components we added to our NEW_ENTRIES array. */
  svn_sort__array_reverse(new_entries, scratch_pool);

  /* The repository doesn't know about the reserved administrative
     directory. */
  if (new_entries->nelts)
    {
      const char *last_component
        = APR_ARRAY_IDX(new_entries, new_entries->nelts - 1, const char *);

      if (svn_wc_is_adm_dir(last_component, scratch_pool))
        return svn_error_createf
          (SVN_ERR_CL_ADM_DIR_RESERVED, NULL,
           _("'%s' is a reserved name and cannot be imported"),
           svn_dirent_local_style(last_component, scratch_pool));
    }

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, scratch_pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__get_commit_ev2(&editor, ra_session,
                                 commit_revprops, commit_callback,
                                 commit_baton, NULL, TRUE,
                                 NULL, NULL, NULL, NULL,
                                 scratch_pool, scratch_pool));

  /* Get inherited svn:auto-props, svn:global-ignores, and
     svn:ignores for the location we are importing to. */
  if (!no_autoprops)
    SVN_ERR(svn_client__get_all_auto_props(&autoprops, url, ctx,
                                           scratch_pool, iterpool));
  if (no_ignore)
    {
      global_ignores = NULL;
      local_ignores_arr = NULL;
    }
  else
    {
      svn_opt_revision_t rev;
      apr_array_header_t *config_ignores;
      apr_hash_t *local_ignores_hash;

      SVN_ERR(svn_client__get_inherited_ignores(&global_ignores, url, ctx,
                                                scratch_pool, iterpool));
      SVN_ERR(svn_wc_get_default_ignores(&config_ignores, ctx->config,
                                         scratch_pool));
      global_ignores = apr_array_append(scratch_pool, global_ignores,
                                        config_ignores);

      rev.kind = svn_opt_revision_head;
      SVN_ERR(svn_client_propget5(&local_ignores_hash, NULL, SVN_PROP_IGNORE, url,
                                  &rev, &rev, NULL, svn_depth_empty, NULL, ctx,
                                  scratch_pool, scratch_pool));
      local_ignores_arr = apr_array_make(scratch_pool, 1, sizeof(const char *));

      if (apr_hash_count(local_ignores_hash))
        {
          svn_string_t *propval = apr_hash_get(local_ignores_hash, url,
                                               APR_HASH_KEY_STRING);
          if (propval)
            {
              svn_cstring_split_append(local_ignores_arr, propval->data,
                                       "\n\r\t\v ", FALSE, scratch_pool);
            }
        }
    }

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, scratch_pool));
  edit_relpath = svn_uri_skip_ancestor(repos_root, url, scratch_pool);

  /* If an error occurred during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if ((err = import(local_abspath, new_entries, editor, edit_relpath,
                    depth, excludes,
                    autoprops, local_ignores_arr, global_ignores,
                    no_ignore, no_autoprops, ignore_unknown_node_types,
                    filter_callback, filter_baton, ctx, iterpool)))
    {
      svn_error_clear(svn_editor_abort(editor));
      return svn_error_trace(err);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
