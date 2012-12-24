/*
 * commit.c:  wrappers around wc commit functionality.
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
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_subst.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_props.h"

#include "client.h"
#include "private/svn_wc_private.h"
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
     svn:inheritable-auto-props.  A hash mapping const char * file patterns to a
     second hash which maps const char * property names to const char *
     property values.  Properties which don't have a value, e.g. svn:executable,
     simply map the property name to an empty string. */
  apr_hash_t *autoprops;
} import_ctx_t;


/* Apply LOCAL_ABSPATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.
   PROPERTIES is the set of node properties set on this file.

   Fill DIGEST with the md5 checksum of the sent file; DIGEST must be
   at least APR_MD5_DIGESTSIZE bytes long. */

/* ### how does this compare against svn_wc_transmit_text_deltas2() ??? */

static svn_error_t *
send_file_contents(const char *local_abspath,
                   void *file_baton,
                   const svn_delta_editor_t *editor,
                   apr_hash_t *properties,
                   unsigned char *digest,
                   apr_pool_t *pool)
{
  svn_stream_t *contents;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  const svn_string_t *eol_style_val = NULL, *keywords_val = NULL;
  svn_boolean_t special = FALSE;
  svn_subst_eol_style_t eol_style;
  const char *eol;
  apr_hash_t *keywords;

  /* If there are properties, look for EOL-style and keywords ones. */
  if (properties)
    {
      eol_style_val = apr_hash_get(properties, SVN_PROP_EOL_STYLE,
                                   sizeof(SVN_PROP_EOL_STYLE) - 1);
      keywords_val = apr_hash_get(properties, SVN_PROP_KEYWORDS,
                                  sizeof(SVN_PROP_KEYWORDS) - 1);
      if (apr_hash_get(properties, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING))
        special = TRUE;
    }

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR(editor->apply_textdelta(file_baton, NULL, pool,
                                  &handler, &handler_baton));

  if (eol_style_val)
    svn_subst_eol_style_from_value(&eol_style, &eol, eol_style_val->data);
  else
    {
      eol = NULL;
      eol_style = svn_subst_eol_style_none;
    }

  if (keywords_val)
    SVN_ERR(svn_subst_build_keywords2(&keywords, keywords_val->data,
                                      APR_STRINGIFY(SVN_INVALID_REVNUM),
                                      "", 0, "", pool));
  else
    keywords = NULL;

  if (special)
    {
      SVN_ERR(svn_subst_read_specialfile(&contents, local_abspath,
                                         pool, pool));
    }
  else
    {
      /* Open the working copy file. */
      SVN_ERR(svn_stream_open_readonly(&contents, local_abspath, pool, pool));

      /* If we have EOL styles or keywords, then detranslate the file. */
      if (svn_subst_translation_required(eol_style, eol, keywords,
                                         FALSE, TRUE))
        {
          if (eol_style == svn_subst_eol_style_unknown)
            return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                    _("%s property on '%s' contains "
                                      "unrecognized EOL-style '%s'"),
                                    SVN_PROP_EOL_STYLE,
                                    svn_dirent_local_style(local_abspath,
                                                           pool),
                                    eol_style_val->data);

          /* We're importing, so translate files with 'native' eol-style to
           * repository-normal form, not to this platform's native EOL. */
          if (eol_style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;

          /* Wrap the working copy stream with a filter to detranslate it. */
          contents = svn_subst_stream_translated(contents,
                                                 eol,
                                                 TRUE /* repair */,
                                                 keywords,
                                                 FALSE /* expand */,
                                                 pool);
        }
    }

  /* Send the file's contents to the delta-window handler. */
  return svn_error_trace(svn_txdelta_send_stream(contents, handler,
                                                 handler_baton, digest,
                                                 pool));
}


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
import_file(const svn_delta_editor_t *editor,
            void *dir_baton,
            const char *local_abspath,
            const char *edit_path,
            const svn_io_dirent2_t *dirent,
            import_ctx_t *import_ctx,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  void *file_baton;
  const char *mimetype = NULL;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  const char *text_checksum;
  apr_hash_t* properties;
  apr_hash_index_t *hi;

  SVN_ERR(svn_path_check_valid(local_abspath, pool));

  /* Add the file, using the pool from the FILES hash. */
  SVN_ERR(editor->add_file(edit_path, dir_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));

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
  else
    properties = apr_hash_make(pool);

  if (properties)
    {
      for (hi = apr_hash_first(pool, properties); hi; hi = apr_hash_next(hi))
        {
          const char *pname = svn__apr_hash_index_key(hi);
          const svn_string_t *pval = svn__apr_hash_index_val(hi);

          SVN_ERR(editor->change_file_prop(file_baton, pname, pval, pool));
        }
    }

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
    {
      apr_hash_set(properties, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING,
                   svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool));
      SVN_ERR(editor->change_file_prop(file_baton, SVN_PROP_SPECIAL,
                                       apr_hash_get(properties,
                                                    SVN_PROP_SPECIAL,
                                                    APR_HASH_KEY_STRING),
                                       pool));
    }

  /* Now, transmit the file contents. */
  SVN_ERR(send_file_contents(local_abspath, file_baton, editor,
                             properties, digest, pool));

  /* Finally, close the file. */
  text_checksum =
    svn_checksum_to_cstring(svn_checksum__from_digest_md5(digest, pool), pool);

  return editor->close_file(file_baton, text_checksum, pool);
}


/* Return in CHILDREN a mapping of basenames to dirents for the importable
 * children of DIR_ABSPATH.  EXCLUDES is a hash of absolute paths to filter
 * out.  IGNORES and MANDATORY_IGNORES, if non-NULL, are lists of basename
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
                      apr_array_header_t *mandatory_ignores,
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

      if (svn_wc_match_ignore_list(base_name, mandatory_ignores, iterpool))
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
import_dir(const svn_delta_editor_t *editor,
           void *dir_baton,
           const char *local_abspath,
           const char *edit_path,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *mandatory_ignores,
           svn_boolean_t no_ignore,
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
                const char *edit_path,
                apr_hash_t *dirents,
                const svn_delta_editor_t *editor,
                void *dir_baton,
                svn_depth_t depth,
                apr_hash_t *excludes,
                apr_array_header_t *mandatory_ignores,
                svn_boolean_t no_ignore,
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
      const char *this_abspath, *this_edit_path;
      svn_sort__item_t item = APR_ARRAY_IDX(sorted_dirents, i,
                                            svn_sort__item_t);
      const char *filename = item.key;
      const svn_io_dirent2_t *dirent = item.value;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Typically, we started importing from ".", in which case
         edit_path is "".  So below, this_path might become "./blah",
         and this_edit_path might become "blah", for example. */
      this_abspath = svn_dirent_join(dir_abspath, filename, iterpool);
      this_edit_path = svn_relpath_join(edit_path, filename, iterpool);

      if (dirent->kind == svn_node_dir && depth >= svn_depth_immediates)
        {
          /* Recurse. */
          svn_depth_t depth_below_here = depth;
          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(import_dir(editor, dir_baton, this_abspath,
                             this_edit_path, depth_below_here, excludes,
                             mandatory_ignores, no_ignore,
                             ignore_unknown_node_types, filter_callback,
                             filter_baton, import_ctx, ctx, iterpool));
        }
      else if (dirent->kind == svn_node_file && depth >= svn_depth_files)
        {
          SVN_ERR(import_file(editor, dir_baton, this_abspath,
                              this_edit_path, dirent,
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
                    = svn_wc_create_notify(this_abspath,
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
               svn_dirent_local_style(this_abspath, iterpool));
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
 * MANDATORY_IGNORES is an array of const char * ignore patterns.  Any child
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
import_dir(const svn_delta_editor_t *editor,
           void *dir_baton,
           const char *local_abspath,
           const char *edit_path,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *mandatory_ignores,
           svn_boolean_t no_ignore,
           svn_boolean_t ignore_unknown_node_types,
           svn_client_import_filter_func_t filter_callback,
           void *filter_baton,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_array_header_t *ignores = NULL;
  void *this_dir_baton;

  SVN_ERR(svn_path_check_valid(local_abspath, pool));

  if (!no_ignore)
    SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));

  SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes, ignores,
                                mandatory_ignores, filter_callback,
                                filter_baton, ctx, pool, pool));

  /* Import this directory, but not yet its children. */
  {
    /* Add the new subdirectory, getting a descent baton from the editor. */
    SVN_ERR(editor->add_directory(edit_path, dir_baton, NULL,
                                  SVN_INVALID_REVNUM, pool, &this_dir_baton));

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
  }

  /* Now import the children recursively. */
  SVN_ERR(import_children(local_abspath, edit_path, dirents, editor,
                          this_dir_baton, depth, excludes, mandatory_ignores,
                          no_ignore, ignore_unknown_node_types,
                          filter_callback, filter_baton,
                          import_ctx, ctx, pool));

  /* Finally, close the sub-directory. */
  SVN_ERR(editor->close_directory(this_dir_baton, pool));

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
 * svn:inheritable-auto-props inherited by the import target, see the
 * IMPORT_CTX member of the same name.
 *
 * LOCAL_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:ignore property (if any) set on the root of the
 * repository target and thus dictates which immediate children of that
 * target should be ignored and not imported.
 *
 * MANDATORY_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:inheritable-ignores properties (if any) set on
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
       const svn_delta_editor_t *editor,
       void *edit_baton,
       svn_depth_t depth,
       apr_hash_t *excludes,
       apr_hash_t *autoprops,
       apr_array_header_t *local_ignores,
       apr_array_header_t *mandatory_ignores,
       svn_boolean_t no_ignore,
       svn_boolean_t ignore_unknown_node_types,
       svn_client_import_filter_func_t filter_callback,
       void *filter_baton,
       svn_client_ctx_t *ctx,
       apr_pool_t *pool)
{
  void *root_baton;
  apr_array_header_t *ignores = NULL;
  apr_array_header_t *batons = NULL;
  const char *edit_path = "";
  import_ctx_t *import_ctx = apr_pcalloc(pool, sizeof(*import_ctx));
  const svn_io_dirent2_t *dirent;

  import_ctx->autoprops = autoprops;
  svn_magic__init(&import_ctx->magic_cookie, pool);

  /* Get a root dir baton.  We pass an invalid revnum to open_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR(svn_io_stat_dirent(&dirent, local_abspath, FALSE, pool, pool));

  /* Make the intermediate directory components necessary for properly
     rooting our import source tree.  */
  if (new_entries->nelts)
    {
      int i;

      batons = apr_array_make(pool, new_entries->nelts, sizeof(void *));
      for (i = 0; i < new_entries->nelts; i++)
        {
          const char *component = APR_ARRAY_IDX(new_entries, i, const char *);
          edit_path = svn_relpath_join(edit_path, component, pool);

          /* If this is the last path component, and we're importing a
             file, then this component is the name of the file, not an
             intermediate directory. */
          if ((i == new_entries->nelts - 1) && (dirent->kind == svn_node_file))
            break;

          APR_ARRAY_PUSH(batons, void *) = root_baton;
          SVN_ERR(editor->add_directory(edit_path,
                                        root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        pool, &root_baton));

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
        {
          SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));
          ignores_match =
            (svn_wc_match_ignore_list(local_abspath, ignores, pool)
             || svn_wc_match_ignore_list(local_abspath, local_ignores, pool));
        }
      if (!ignores_match)
        SVN_ERR(import_file(editor, root_baton, local_abspath, edit_path,
                            dirent, import_ctx, ctx, pool));
    }
  else if (dirent->kind == svn_node_dir)
    {
      apr_hash_t *dirents;

      if (!no_ignore)
        {
          int i;

          SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));

          /* If we are not creating new repository paths, then we are creating
             importing new paths to an existing directory.  If that directory
             has the svn:ignore property set on it, then we want to ignore
             immediate children that match the pattern(s) defined by that
             property. */
          if (!new_entries->nelts)
            {
              for (i = 0; i < local_ignores->nelts; i++)
                {
                  const char *ignore = APR_ARRAY_IDX(local_ignores, i,
                                                     const char *);
                  APR_ARRAY_PUSH(ignores, const char *) = ignore;
                }          
            }
        }

      SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes,
                                    ignores, mandatory_ignores,
                                    filter_callback, filter_baton, ctx,
                                    pool, pool));

      SVN_ERR(import_children(local_abspath, edit_path, dirents, editor,
                              root_baton, depth, excludes, mandatory_ignores,
                              no_ignore, ignore_unknown_node_types,
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
  SVN_ERR(editor->close_directory(root_baton, pool));
  if (batons && batons->nelts)
    {
      void **baton;
      while ((baton = (void **) apr_array_pop(batons)))
        {
          SVN_ERR(editor->close_directory(*baton, pool));
        }
    }

  if (import_ctx->repos_changed)
    return editor->close_edit(edit_baton, pool);
  else
    return editor->abort_edit(edit_baton, pool);
}


struct capture_baton_t {
  svn_commit_callback2_t original_callback;
  void *original_baton;

  svn_commit_info_t **info;
  apr_pool_t *pool;
};


static svn_error_t *
capture_commit_info(const svn_commit_info_t *commit_info,
                    void *baton,
                    apr_pool_t *pool)
{
  struct capture_baton_t *cb = baton;

  *(cb->info) = svn_commit_info_dup(commit_info, cb->pool);

  if (cb->original_callback)
    SVN_ERR((cb->original_callback)(commit_info, cb->original_baton, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_ra_editor(const svn_delta_editor_t **editor,
              void **edit_baton,
              svn_ra_session_t *ra_session,
              svn_client_ctx_t *ctx,
              const char *log_msg,
              const apr_array_header_t *commit_items,
              const apr_hash_t *revprop_table,
              apr_hash_t *lock_tokens,
              svn_boolean_t keep_locks,
              svn_commit_callback2_t commit_callback,
              void *commit_baton,
              apr_pool_t *pool)
{
  apr_hash_t *commit_revprops;
  apr_hash_t *relpath_map = NULL;

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

#ifdef ENABLE_EV2_SHIMS
  if (commit_items)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      relpath_map = apr_hash_make(pool);
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item = APR_ARRAY_IDX(commit_items, i,
                                                  svn_client_commit_item3_t *);
          const char *relpath;

          if (!item->path)
            continue;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc__node_get_origin(NULL, NULL, &relpath, NULL, NULL, NULL,
                                          ctx->wc_ctx, item->path, FALSE, pool,
                                          iterpool));
          if (relpath)
            apr_hash_set(relpath_map, relpath, APR_HASH_KEY_STRING, item->path);
        }
      svn_pool_destroy(iterpool);
    }
#endif

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       relpath_map, pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, editor, edit_baton,
                                    commit_revprops, commit_callback,
                                    commit_baton, lock_tokens, keep_locks,
                                    pool));

  return SVN_NO_ERROR;
}


/*** Public Interfaces. ***/

svn_error_t *
svn_client_import5(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
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
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_session_t *ra_session;
  apr_hash_t *excludes = apr_hash_make(scratch_pool);
  svn_node_kind_t kind;
  const char *local_abspath;
  apr_array_header_t *new_entries = apr_array_make(scratch_pool, 4,
                                                   sizeof(const char *));
  const char *temp;
  const char *dir;
  apr_hash_t *commit_revprops;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *autoprops;
  apr_array_header_t *mandatory_ignores;
  svn_opt_revision_t rev;
  apr_hash_t *local_ignores_hash;
  apr_array_header_t *local_ignores_arr;

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
      svn_pool_clear(iterpool);

      svn_uri_split(&temp, &dir, url, scratch_pool);
      APR_ARRAY_PUSH(new_entries, const char *) = dir;
      url = temp;
      SVN_ERR(svn_ra_reparent(ra_session, url, iterpool));

      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                                iterpool));
    }

  /* Reverse the order of the components we added to our NEW_ENTRIES array. */
  if (new_entries->nelts)
    {
      int i, j;
      const char *component;
      for (i = 0; i < (new_entries->nelts / 2); i++)
        {
          j = new_entries->nelts - i - 1;
          component =
            APR_ARRAY_IDX(new_entries, i, const char *);
          APR_ARRAY_IDX(new_entries, i, const char *) =
            APR_ARRAY_IDX(new_entries, j, const char *);
          APR_ARRAY_IDX(new_entries, j, const char *) =
            component;
        }
    }

  /* The repository doesn't know about the reserved administrative
     directory. */
  if (new_entries->nelts
      /* What's this, what's this?  This assignment is here because we
         use the value to construct the error message just below.  It
         may not be aesthetically pleasing, but it's less ugly than
         calling APR_ARRAY_IDX twice. */
      && svn_wc_is_adm_dir(temp = APR_ARRAY_IDX(new_entries,
                                                new_entries->nelts - 1,
                                                const char *),
                           scratch_pool))
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, NULL,
       _("'%s' is a reserved name and cannot be imported"),
       svn_dirent_local_style(temp, scratch_pool));

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, scratch_pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       NULL, scratch_pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops, commit_callback,
                                    commit_baton, NULL, TRUE,
                                    scratch_pool));

  /* Get inherited svn:inheritable-auto-props, svn:inheritable-ignores, and
     svn:ignores for the location we are importing to. */
  SVN_ERR(svn_client__get_all_auto_props(&autoprops, url, ctx,
                                         scratch_pool, iterpool));
  SVN_ERR(svn_client__get_inherited_ignores(&mandatory_ignores, url,
                                            ctx, scratch_pool, iterpool));
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

  /* If an error occurred during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if ((err = import(local_abspath, new_entries, editor, edit_baton,
                    depth, excludes, autoprops, local_ignores_arr,
                    mandatory_ignores, no_ignore, ignore_unknown_node_types,
                    filter_callback, filter_baton, ctx, iterpool)))
    {
      svn_error_clear(editor->abort_edit(edit_baton, iterpool));
      return svn_error_trace(err);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
reconcile_errors(svn_error_t *commit_err,
                 svn_error_t *unlock_err,
                 svn_error_t *bump_err,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || bump_err))
    return SVN_NO_ERROR;

  /* If there was a commit error, start off our error chain with
     that. */
  if (commit_err)
    {
      commit_err = svn_error_quick_wrap
        (commit_err, _("Commit failed (details follow):"));
      err = commit_err;
    }

  /* Else, create a new "general" error that will lead off the errors
     that follow. */
  else
    err = svn_error_create(SVN_ERR_BASE, NULL,
                           _("Commit succeeded, but other errors follow:"));

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap
        (unlock_err, _("Error unlocking locked dirs (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, unlock_err);
    }

  /* If there was a bumping error... */
  if (bump_err)
    {
      /* Wrap the error with some headers. */
      bump_err = svn_error_quick_wrap
        (bump_err, _("Error bumping revisions post-commit (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, bump_err);
    }

  return err;
}

/* For all lock tokens in ALL_TOKENS for URLs under BASE_URL, add them
   to a new hashtable allocated in POOL.  *RESULT is set to point to this
   new hash table.  *RESULT will be keyed on const char * URI-decoded paths
   relative to BASE_URL.  The lock tokens will not be duplicated. */
static svn_error_t *
collect_lock_tokens(apr_hash_t **result,
                    apr_hash_t *all_tokens,
                    const char *base_url,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *result = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, all_tokens); hi; hi = apr_hash_next(hi))
    {
      const char *url = svn__apr_hash_index_key(hi);
      const char *token = svn__apr_hash_index_val(hi);
      const char *relpath = svn_uri_skip_ancestor(base_url, url, pool);

      if (relpath)
        {
          apr_hash_set(*result, relpath, APR_HASH_KEY_STRING, token);
        }
    }

  return SVN_NO_ERROR;
}

/* Put ITEM onto QUEUE, allocating it in QUEUE's pool...
 * If a checksum is provided, it can be the MD5 and/or the SHA1. */
static svn_error_t *
post_process_commit_item(svn_wc_committed_queue_t *queue,
                         const svn_client_commit_item3_t *item,
                         svn_wc_context_t *wc_ctx,
                         svn_boolean_t keep_changelists,
                         svn_boolean_t keep_locks,
                         svn_boolean_t commit_as_operations,
                         const svn_checksum_t *sha1_checksum,
                         apr_pool_t *scratch_pool)
{
  svn_boolean_t loop_recurse = FALSE;
  svn_boolean_t remove_lock;

  if (! commit_as_operations
      && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
      && (item->kind == svn_node_dir)
      && (item->copyfrom_url))
    loop_recurse = TRUE;

  remove_lock = (! keep_locks && (item->state_flags
                                       & SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN));

  return svn_wc_queue_committed3(queue, wc_ctx, item->path,
                                 loop_recurse, item->incoming_prop_changes,
                                 remove_lock, !keep_changelists,
                                 sha1_checksum, scratch_pool);
}


static svn_error_t *
check_nonrecursive_dir_delete(svn_wc_context_t *wc_ctx,
                              const char *target_abspath,
                              svn_depth_t depth,
                              apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(depth != svn_depth_infinity);

  SVN_ERR(svn_wc_read_kind(&kind, wc_ctx, target_abspath, FALSE,
                           scratch_pool));


  /* ### TODO(sd): This check is slightly too strict.  It should be
     ### possible to:
     ###
     ###   * delete a directory containing only files when
     ###     depth==svn_depth_files;
     ###
     ###   * delete a directory containing only files and empty
     ###     subdirs when depth==svn_depth_immediates.
     ###
     ### But for now, we insist on svn_depth_infinity if you're
     ### going to delete a directory, because we're lazy and
     ### trying to get depthy commits working in the first place.
     ###
     ### This would be fairly easy to fix, though: just, well,
     ### check the above conditions!
     ###
     ### GJS: I think there may be some confusion here. there is
     ###      the depth of the commit, and the depth of a checked-out
     ###      directory in the working copy. Delete, by its nature, will
     ###      always delete all of its children, so it seems a bit
     ###      strange to worry about what is in the working copy.
  */
  if (kind == svn_node_dir)
    {
      svn_wc_schedule_t schedule;

      /* ### Looking at schedule is probably enough, no need for
         pristine compare etc. */
      SVN_ERR(svn_wc__node_get_schedule(&schedule, NULL,
                                        wc_ctx, target_abspath,
                                        scratch_pool));

      if (schedule == svn_wc_schedule_delete
          || schedule == svn_wc_schedule_replace)
        {
          const apr_array_header_t *children;

          SVN_ERR(svn_wc__node_get_children(&children, wc_ctx,
                                            target_abspath, TRUE,
                                            scratch_pool, scratch_pool));

          if (children->nelts > 0)
            return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                     _("Cannot delete the directory '%s' "
                                       "in a non-recursive commit "
                                       "because it has children"),
                                     svn_dirent_local_style(target_abspath,
                                                            scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


/* Given a list of committables described by their common base abspath
   BASE_ABSPATH and a list of relative dirents TARGET_RELPATHS determine
   which absolute paths must be locked to commit all these targets and
   return this as a const char * array in LOCK_TARGETS

   Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporary
   storage */
static svn_error_t *
determine_lock_targets(apr_array_header_t **lock_targets,
                       svn_wc_context_t *wc_ctx,
                       const char *base_abspath,
                       const apr_array_header_t *target_relpaths,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *wc_items; /* const char *wcroot -> apr_array_header_t */
  apr_hash_index_t *hi;
  int i;

  wc_items = apr_hash_make(scratch_pool);

  /* Create an array of targets for each working copy used */
  for (i = 0; i < target_relpaths->nelts; i++)
    {
      const char *target_abspath;
      const char *wcroot_abspath;
      apr_array_header_t *wc_targets;
      svn_error_t *err;
      const char *target_relpath = APR_ARRAY_IDX(target_relpaths, i,
                                                 const char *);

      svn_pool_clear(iterpool);
      target_abspath = svn_dirent_join(base_abspath, target_relpath,
                                       scratch_pool);

      err = svn_wc__get_wc_root(&wcroot_abspath, wc_ctx, target_abspath,
                                iterpool, iterpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              continue;
            }
          return svn_error_trace(err);
        }

      wc_targets = apr_hash_get(wc_items, wcroot_abspath, APR_HASH_KEY_STRING);

      if (! wc_targets)
        {
          wc_targets = apr_array_make(scratch_pool, 4, sizeof(const char *));
          apr_hash_set(wc_items, apr_pstrdup(scratch_pool, wcroot_abspath),
                       APR_HASH_KEY_STRING, wc_targets);
        }

      APR_ARRAY_PUSH(wc_targets, const char *) = target_abspath;
    }

  *lock_targets = apr_array_make(result_pool, apr_hash_count(wc_items),
                                 sizeof(const char *));

  /* For each working copy determine where to lock */
  for (hi = apr_hash_first(scratch_pool, wc_items);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *common;
      const char *wcroot_abspath = svn__apr_hash_index_key(hi);
      apr_array_header_t *wc_targets = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      if (wc_targets->nelts == 1)
        {
          const char *target_abspath;
          target_abspath = APR_ARRAY_IDX(wc_targets, 0, const char *);

          if (! strcmp(wcroot_abspath, target_abspath))
            {
              APR_ARRAY_PUSH(*lock_targets, const char *)
                      = apr_pstrdup(result_pool, target_abspath);
            }
          else
            {
              /* Lock the parent to allow deleting the target */
              APR_ARRAY_PUSH(*lock_targets, const char *)
                      = svn_dirent_dirname(target_abspath, result_pool);
            }
        }
      else if (wc_targets->nelts > 1)
        {
          SVN_ERR(svn_dirent_condense_targets(&common, &wc_targets, wc_targets,
                                              FALSE, iterpool, iterpool));

          qsort(wc_targets->elts, wc_targets->nelts, wc_targets->elt_size,
                svn_sort_compare_paths);

          if (wc_targets->nelts == 0
              || !svn_path_is_empty(APR_ARRAY_IDX(wc_targets, 0, const char*))
              || !strcmp(common, wcroot_abspath))
            {
              APR_ARRAY_PUSH(*lock_targets, const char *)
                    = apr_pstrdup(result_pool, common);
            }
          else
            {
              /* Lock the parent to allow deleting the target */
              APR_ARRAY_PUSH(*lock_targets, const char *)
                       = svn_dirent_dirname(common, result_pool);
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Baton for check_url_kind */
struct check_url_kind_baton
{
  apr_pool_t *pool;
  svn_ra_session_t *session;
  const char *repos_root_url;
  svn_client_ctx_t *ctx;
};

/* Implements svn_client__check_url_kind_t for svn_client_commit5 */
static svn_error_t *
check_url_kind(void *baton,
               svn_node_kind_t *kind,
               const char *url,
               svn_revnum_t revision,
               apr_pool_t *scratch_pool)
{
  struct check_url_kind_baton *cukb = baton;

  /* If we don't have a session or can't use the session, get one */
  if (!cukb->session || !svn_uri__is_ancestor(cukb->repos_root_url, url))
    {
      SVN_ERR(svn_client_open_ra_session(&cukb->session, url, cukb->ctx,
                                         cukb->pool));
      SVN_ERR(svn_ra_get_repos_root2(cukb->session, &cukb->repos_root_url,
                                     cukb->pool));
    }
  else
    SVN_ERR(svn_ra_reparent(cukb->session, url, scratch_pool));

  return svn_error_trace(
                svn_ra_check_path(cukb->session, "", revision,
                                  kind, scratch_pool));
}

/* Recurse into every target in REL_TARGETS, finding committable externals
 * nested within. Append these to REL_TARGETS itself. The paths in REL_TARGETS
 * are assumed to be / will be created relative to BASE_ABSPATH. The remaining
 * arguments correspond to those of svn_client_commit6(). */
static svn_error_t*
append_externals_as_explicit_targets(apr_array_header_t *rel_targets,
                                     const char *base_abspath,
                                     svn_boolean_t include_file_externals,
                                     svn_boolean_t include_dir_externals,
                                     svn_depth_t depth,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  int rel_targets_nelts_fixed;
  int i;
  apr_pool_t *iterpool;

  if (! (include_file_externals || include_dir_externals))
    return SVN_NO_ERROR;

  /* Easy part of applying DEPTH to externals. */
  if (depth == svn_depth_empty)
    {
      /* Don't recurse. */
      return SVN_NO_ERROR;
    }
  else if (depth != svn_depth_infinity)
    {
      include_dir_externals = FALSE;
      /* We slip in dir externals as explicit targets. When we do that,
       * depth_immediates should become depth_empty for dir externals targets.
       * But adding the dir external to the list of targets makes it get
       * handled with depth_immediates itself, and thus will also include the
       * immediate children of the dir external. So do dir externals only with
       * depth_infinity or not at all.
       * ### TODO: Maybe rework this (and svn_client_commit6()) into separate
       * ### target lists, "duplicating" REL_TARGETS: one for the user's
       * ### targets and one for the overlayed externals targets, and pass an
       * ### appropriate depth for the externals targets in a separate call to
       * ### svn_client__harvest_committables(). The only gain is correct
       * ### handling of this very specific case: during 'svn commit
       * ### --depth=immediates --include-externals', commit dir externals
       * ### (only immediate children of a target) with depth_empty instead of
       * ### not at all. No other effect. So not doing that for now. */
    }

  /* Iterate *and* grow REL_TARGETS at the same time. */
  rel_targets_nelts_fixed = rel_targets->nelts;

  iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < rel_targets_nelts_fixed; i++)
    {
      int j;
      const char *target;
      apr_array_header_t *externals = NULL;

      svn_pool_clear(iterpool);

      target = svn_dirent_join(base_abspath,
                               APR_ARRAY_IDX(rel_targets, i, const char *),
                               iterpool);

      /* ### TODO: Possible optimization: No need to do this for file targets.
       * ### But what's cheaper, stat'ing the file system or querying the db?
       * ### --> future. */

      SVN_ERR(svn_wc__committable_externals_below(&externals, ctx->wc_ctx,
                                                  target, depth,
                                                  iterpool, iterpool));

      if (externals != NULL)
        {
          const char *rel_target;

          for (j = 0; j < externals->nelts; j++)
            {
              svn_wc__committable_external_info_t *xinfo =
                         APR_ARRAY_IDX(externals, j,
                                       svn_wc__committable_external_info_t *);

              if ((xinfo->kind == svn_kind_file && ! include_file_externals)
                  || (xinfo->kind == svn_kind_dir && ! include_dir_externals))
                continue;

              rel_target = svn_dirent_skip_ancestor(base_abspath,
                                                    xinfo->local_abspath);

              SVN_ERR_ASSERT(rel_target != NULL && *rel_target != '\0');

              APR_ARRAY_PUSH(rel_targets, const char *) =
                                         apr_pstrdup(result_pool, rel_target);
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_commit6(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   svn_boolean_t include_file_externals,
                   svn_boolean_t include_dir_externals,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  struct capture_baton_t cb;
  svn_ra_session_t *ra_session;
  const char *log_msg;
  const char *base_abspath;
  const char *base_url;
  apr_array_header_t *rel_targets;
  apr_array_header_t *lock_targets;
  apr_array_header_t *locks_obtained;
  svn_client__committables_t *committables;
  apr_hash_t *lock_tokens;
  apr_hash_t *sha1_checksums;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR;
  svn_error_t *bump_err = SVN_NO_ERROR;
  svn_error_t *unlock_err = SVN_NO_ERROR;
  svn_boolean_t commit_in_progress = FALSE;
  svn_commit_info_t *commit_info = NULL;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *current_abspath;
  const char *notify_prefix;
  int i;

  SVN_ERR_ASSERT(depth != svn_depth_unknown && depth != svn_depth_exclude);

  /* Committing URLs doesn't make sense, so error if it's tried. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      if (svn_path_is_url(target))
        return svn_error_createf
          (SVN_ERR_ILLEGAL_TARGET, NULL,
           _("'%s' is a URL, but URLs cannot be commit targets"), target);
    }

  /* Condense the target list. This makes all targets absolute. */
  SVN_ERR(svn_dirent_condense_targets(&base_abspath, &rel_targets, targets,
                                      FALSE, pool, iterpool));

  /* No targets means nothing to commit, so just return. */
  if (base_abspath == NULL)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(rel_targets != NULL);

  /* If we calculated only a base and no relative targets, this
     must mean that we are being asked to commit (effectively) a
     single path. */
  if (rel_targets->nelts == 0)
    APR_ARRAY_PUSH(rel_targets, const char *) = "";

  SVN_ERR(append_externals_as_explicit_targets(rel_targets, base_abspath,
                                               include_file_externals,
                                               include_dir_externals,
                                               depth, ctx,
                                               pool, pool));

  SVN_ERR(determine_lock_targets(&lock_targets, ctx->wc_ctx, base_abspath,
                                 rel_targets, pool, iterpool));

  locks_obtained = apr_array_make(pool, lock_targets->nelts,
                                  sizeof(const char *));

  for (i = 0; i < lock_targets->nelts; i++)
    {
      const char *lock_root;
      const char *target = APR_ARRAY_IDX(lock_targets, i, const char *);

      svn_pool_clear(iterpool);

      cmt_err = svn_error_trace(
                    svn_wc__acquire_write_lock(&lock_root, ctx->wc_ctx, target,
                                           FALSE, pool, iterpool));

      if (cmt_err)
        goto cleanup;

      APR_ARRAY_PUSH(locks_obtained, const char *) = lock_root;
    }

  /* Determine prefix to strip from the commit notify messages */
  SVN_ERR(svn_dirent_get_absolute(&current_abspath, "", pool));
  notify_prefix = svn_dirent_get_longest_ancestor(current_abspath,
                                                  base_abspath,
                                                  pool);

  /* If a non-recursive commit is desired, do not allow a deleted directory
     as one of the targets. */
  if (depth != svn_depth_infinity && ! commit_as_operations)
    for (i = 0; i < rel_targets->nelts; i++)
      {
        const char *relpath = APR_ARRAY_IDX(rel_targets, i, const char *);
        const char *target_abspath;

        svn_pool_clear(iterpool);

        target_abspath = svn_dirent_join(base_abspath, relpath, iterpool);

        cmt_err = svn_error_trace(
          check_nonrecursive_dir_delete(ctx->wc_ctx, target_abspath,
                                        depth, iterpool));

        if (cmt_err)
          goto cleanup;
      }

  /* Crawl the working copy for commit items. */
  {
    struct check_url_kind_baton cukb;

    /* Prepare for when we have a copy containing not-present nodes. */
    cukb.pool = iterpool;
    cukb.session = NULL; /* ### Can we somehow reuse session? */
    cukb.repos_root_url = NULL;
    cukb.ctx = ctx;

    cmt_err = svn_error_trace(
                   svn_client__harvest_committables(&committables,
                                                    &lock_tokens,
                                                    base_abspath,
                                                    rel_targets,
                                                    depth,
                                                    ! keep_locks,
                                                    changelists,
                                                    check_url_kind,
                                                    &cukb,
                                                    ctx,
                                                    pool,
                                                    iterpool));

    svn_pool_clear(iterpool);
  }

  if (cmt_err)
    goto cleanup;

  if (apr_hash_count(committables->by_repository) == 0)
    {
      goto cleanup; /* Nothing to do */
    }
  else if (apr_hash_count(committables->by_repository) > 1)
    {
      cmt_err = svn_error_create(
             SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Commit can only commit to a single repository at a time.\n"
               "Are all targets part of the same working copy?"));
      goto cleanup;
    }

  {
    apr_hash_index_t *hi = apr_hash_first(iterpool,
                                          committables->by_repository);

    commit_items = svn__apr_hash_index_val(hi);
  }

  /* If our array of targets contains only locks (and no actual file
     or prop modifications), then we return here to avoid committing a
     revision with no changes. */
  {
    svn_boolean_t found_changed_path = FALSE;

    for (i = 0; i < commit_items->nelts; ++i)
      {
        svn_client_commit_item3_t *item =
          APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

        if (item->state_flags != SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
          {
            found_changed_path = TRUE;
            break;
          }
      }

    if (!found_changed_path)
      goto cleanup;
  }

  /* For every target that was moved verify that both halves of the
   * move are part of the commit. */
  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

      svn_pool_clear(iterpool);

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
        {
          const char *moved_from_abspath;
          const char *delete_op_root_abspath;

          cmt_err = svn_error_trace(svn_wc__node_was_moved_here(
                                      &moved_from_abspath,
                                      &delete_op_root_abspath,
                                      ctx->wc_ctx, item->path,
                                      iterpool, iterpool));
          if (cmt_err)
            goto cleanup;

          if (moved_from_abspath && delete_op_root_abspath &&
              strcmp(moved_from_abspath, delete_op_root_abspath) == 0)

            {
              svn_boolean_t found_delete_half =
                (apr_hash_get(committables->by_path, delete_op_root_abspath,
                               APR_HASH_KEY_STRING) != NULL);

              if (!found_delete_half)
                {
                  const char *delete_half_parent_abspath;

                  /* The delete-half isn't in the commit target list.
                   * However, it might itself be the child of a deleted node,
                   * either because of another move or a deletion.
                   *
                   * For example, consider: mv A/B B; mv B/C C; commit;
                   * C's moved-from A/B/C is a child of the deleted A/B.
                   * A/B/C does not appear in the commit target list, but
                   * A/B does appear.
                   * (Note that moved-from information is always stored
                   * relative to the BASE tree, so we have 'C moved-from
                   * A/B/C', not 'C moved-from B/C'.)
                   *
                   * An example involving a move and a delete would be:
                   * mv A/B C; rm A; commit;
                   * Now C is moved-from A/B which does not appear in the
                   * commit target list, but A does appear.
                   */

                  /* Scan upwards for a deletion op-root from the
                   * delete-half's parent directory. */
                  delete_half_parent_abspath =
                    svn_dirent_dirname(delete_op_root_abspath, iterpool);
                  if (strcmp(delete_op_root_abspath,
                             delete_half_parent_abspath) != 0)
                    {
                      const char *parent_delete_op_root_abspath;

                      cmt_err = svn_error_trace(
                                  svn_wc__node_get_deleted_ancestor(
                                    &parent_delete_op_root_abspath,
                                    ctx->wc_ctx, delete_half_parent_abspath,
                                    iterpool, iterpool));
                      if (cmt_err)
                        goto cleanup;

                      if (parent_delete_op_root_abspath)
                        found_delete_half =
                          (apr_hash_get(committables->by_path,
                                        parent_delete_op_root_abspath,
                                        APR_HASH_KEY_STRING) != NULL);
                    }
                }

              if (!found_delete_half)
                {
                  cmt_err = svn_error_createf(
                              SVN_ERR_ILLEGAL_TARGET, NULL,
                              _("Cannot commit '%s' because it was moved from "
                                "'%s' which is not part of the commit; both "
                                "sides of the move must be committed together"),
                              svn_dirent_local_style(item->path, iterpool),
                              svn_dirent_local_style(delete_op_root_abspath,
                                                     iterpool));
                  goto cleanup;
                }
            }
        }
      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        {
          const char *moved_to_abspath;
          const char *copy_op_root_abspath;

          cmt_err = svn_error_trace(svn_wc__node_was_moved_away(
                                      &moved_to_abspath,
                                      &copy_op_root_abspath,
                                      ctx->wc_ctx, item->path,
                                      iterpool, iterpool));
          if (cmt_err)
            goto cleanup;

          if (moved_to_abspath && copy_op_root_abspath &&
              strcmp(moved_to_abspath, copy_op_root_abspath) == 0 &&
              apr_hash_get(committables->by_path, copy_op_root_abspath,
                           APR_HASH_KEY_STRING) == NULL)
            {
              cmt_err = svn_error_createf(
                          SVN_ERR_ILLEGAL_TARGET, NULL,
                         _("Cannot commit '%s' because it was moved to '%s' "
                           "which is not part of the commit; both sides of "
                           "the move must be committed together"),
                         svn_dirent_local_style(item->path, iterpool),
                         svn_dirent_local_style(copy_op_root_abspath,
                                                iterpool));
              goto cleanup;
            }
        }
    }

  /* Go get a log message.  If an error occurs, or no log message is
     specified, abort the operation. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      const char *tmp_file;
      cmt_err = svn_error_trace(
                     svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                             ctx, pool));

      if (cmt_err || (! log_msg))
        goto cleanup;
    }
  else
    log_msg = "";

  /* Sort and condense our COMMIT_ITEMS. */
  cmt_err = svn_error_trace(svn_client__condense_commit_items(&base_url,
                                                              commit_items,
                                                              pool));

  if (cmt_err)
    goto cleanup;

  /* Collect our lock tokens with paths relative to base_url. */
  cmt_err = svn_error_trace(collect_lock_tokens(&lock_tokens, lock_tokens,
                                                base_url, pool));

  if (cmt_err)
    goto cleanup;

  cb.original_callback = commit_callback;
  cb.original_baton = commit_baton;
  cb.info = &commit_info;
  cb.pool = pool;

  /* Get the RA editor from the first lock target, rather than BASE_ABSPATH.
   * When committing from multiple WCs, BASE_ABSPATH might be an unrelated
   * parent of nested working copies. We don't support commits to multiple
   * repositories so using the first WC to get the RA session is safe. */
  cmt_err = svn_error_trace(
              svn_client__open_ra_session_internal(&ra_session, NULL, base_url,
                                                   APR_ARRAY_IDX(lock_targets,
                                                                 0,
                                                                 const char *),
                                                   commit_items,
                                                   TRUE, FALSE, ctx, pool));

  if (cmt_err)
    goto cleanup;

  cmt_err = svn_error_trace(
              get_ra_editor(&editor, &edit_baton, ra_session, ctx,
                            log_msg, commit_items, revprop_table,
                            lock_tokens, keep_locks, capture_commit_info,
                            &cb, pool));

  if (cmt_err)
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  /* Perform the commit. */
  cmt_err = svn_error_trace(
              svn_client__do_commit(base_url, commit_items, editor, edit_baton,
                                    notify_prefix, &sha1_checksums, ctx, pool,
                                    iterpool));

  /* Handle a successful commit. */
  if ((! cmt_err)
      || (cmt_err->apr_err == SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      svn_wc_committed_queue_t *queue = svn_wc_committed_queue_create(pool);

      /* Make a note that our commit is finished. */
      commit_in_progress = FALSE;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item
            = APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

          svn_pool_clear(iterpool);
          bump_err = post_process_commit_item(
                       queue, item, ctx->wc_ctx,
                       keep_changelists, keep_locks, commit_as_operations,
                       apr_hash_get(sha1_checksums,
                                    item->path,
                                    APR_HASH_KEY_STRING),
                       iterpool);
          if (bump_err)
            goto cleanup;
        }

      SVN_ERR_ASSERT(commit_info);
      bump_err = svn_wc_process_committed_queue2(
                   queue, ctx->wc_ctx,
                   commit_info->revision,
                   commit_info->date,
                   commit_info->author,
                   ctx->cancel_func, ctx->cancel_baton,
                   iterpool);
    }

  /* Sleep to ensure timestamp integrity. */
  svn_io_sleep_for_timestamps(base_abspath, pool);

 cleanup:
  /* Abort the commit if it is still in progress. */
  svn_pool_clear(iterpool); /* Close open handles before aborting */
  if (commit_in_progress)
    cmt_err = svn_error_compose_create(cmt_err,
                                       editor->abort_edit(edit_baton, pool));

  /* A bump error is likely to occur while running a working copy log file,
     explicitly unlocking and removing temporary files would be wrong in
     that case.  A commit error (cmt_err) should only occur before any
     attempt to modify the working copy, so it doesn't prevent explicit
     clean-up. */
  if (! bump_err)
    {
      /* Release all locks we obtained */
      for (i = 0; i < locks_obtained->nelts; i++)
        {
          const char *lock_root = APR_ARRAY_IDX(locks_obtained, i,
                                                const char *);

          svn_pool_clear(iterpool);

          unlock_err = svn_error_compose_create(
                           svn_wc__release_write_lock(ctx->wc_ctx, lock_root,
                                                      iterpool),
                           unlock_err);
        }
    }

  svn_pool_destroy(iterpool);

  return svn_error_trace(reconcile_errors(cmt_err, unlock_err, bump_err,
                                          pool));
}

svn_error_t *
svn_client_commit5(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_commit6(targets, depth, keep_locks, keep_changelists,
                            commit_as_operations,
                            TRUE,  /* include_file_externals */
                            FALSE, /* include_dir_externals */
                            changelists, revprop_table, commit_callback,
                            commit_baton, ctx, pool);
}

