/*
 * commit.c:  wrappers around wc commit functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include "svn_iter.h"

#include "client.h"

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

} import_ctx_t;


/* Apply PATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.
   PROPERTIES is the set of node properties set on this file.

   Fill DIGEST with the md5 checksum of the sent file; DIGEST must be
   at least APR_MD5_DIGESTSIZE bytes long. */

/* ### how does this compare against svn_wc_transmit_text_deltas2() ??? */

static svn_error_t *
send_file_contents(const char *path,
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
      SVN_ERR(svn_subst_read_specialfile(&contents, path, pool, pool));
    }
  else
    {
      /* Open the working copy file. */
      SVN_ERR(svn_stream_open_readonly(&contents, path, pool, pool));

      /* If we have EOL styles or keywords, then detranslate the file. */
      if (svn_subst_translation_required(eol_style, eol, keywords,
                                         FALSE, TRUE))
        {
          svn_boolean_t repair = FALSE;

          if (eol_style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;
          else if (eol_style == svn_subst_eol_style_fixed)
            repair = TRUE;
          else if (eol_style != svn_subst_eol_style_none)
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

          /* Wrap the working copy stream with a filter to detranslate it. */
          contents = svn_subst_stream_translated(contents,
                                                 eol,
                                                 repair,
                                                 keywords,
                                                 FALSE /* expand */,
                                                 pool);
        }
    }

  /* Send the file's contents to the delta-window handler. */
  return svn_txdelta_send_stream(contents, handler, handler_baton,
                                 digest, pool);
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
            const char *path,
            const char *edit_path,
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
  svn_node_kind_t kind;
  svn_boolean_t is_special;

  SVN_ERR(svn_path_check_valid(path, pool));

  SVN_ERR(svn_io_check_special_path(path, &kind, &is_special, pool));

  /* Add the file, using the pool from the FILES hash. */
  SVN_ERR(editor->add_file(edit_path, dir_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));

  /* Remember that the repository was modified */
  import_ctx->repos_changed = TRUE;

  if (! is_special)
    {
      /* add automatic properties */
      SVN_ERR(svn_client__get_auto_props(&properties, &mimetype, path, ctx,
                                         pool));
    }
  else
    properties = apr_hash_make(pool);

  if (properties)
    {
      for (hi = apr_hash_first(pool, properties); hi; hi = apr_hash_next(hi))
        {
          const void *pname;
          void *pval;

          apr_hash_this(hi, &pname, NULL, &pval);
          SVN_ERR(editor->change_file_prop(file_baton, pname, pval, pool));
        }
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(path, svn_wc_notify_commit_added, pool);
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
  if (is_special)
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
  SVN_ERR(send_file_contents(path, file_baton, editor,
                             properties, digest, pool));

  /* Finally, close the file. */
  text_checksum =
    svn_checksum_to_cstring(svn_checksum__from_digest(digest, svn_checksum_md5,
                                                      pool), pool);

  return editor->close_file(file_baton, text_checksum, pool);
}


/* Import directory PATH into the repository directory indicated by
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
 * If NO_IGNORE is FALSE, don't import files or directories that match
 * ignore patterns.
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for each
 * directory.
 *
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_dir(const svn_delta_editor_t *editor,
           void *dir_baton,
           const char *path,
           const char *edit_path,
           svn_depth_t depth,
           apr_hash_t *excludes,
           svn_boolean_t no_ignore,
           svn_boolean_t ignore_unknown_node_types,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);  /* iteration pool */
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *ignores;

  SVN_ERR(svn_path_check_valid(path, pool));

  if (!no_ignore)
    SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));

  SVN_ERR(svn_io_get_dirents2(&dirents, path, pool));

  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *this_path, *this_edit_path, *abs_path;
      const svn_io_dirent_t *dirent;
      const char *filename;
      const void *key;
      void *val;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);

      filename = key;
      dirent = val;

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (svn_wc_is_adm_dir(filename, subpool))
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
                = svn_wc_create_notify(svn_path_join(path, filename,
                                                     subpool),
                                       svn_wc_notify_skip, subpool);
              notify->kind = svn_node_dir;
              notify->content_state = notify->prop_state
                = svn_wc_notify_state_inapplicable;
              notify->lock_state = svn_wc_notify_lock_state_inapplicable;
              (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
            }
          continue;
        }

      /* Typically, we started importing from ".", in which case
         edit_path is "".  So below, this_path might become "./blah",
         and this_edit_path might become "blah", for example. */
      this_path = svn_path_join(path, filename, subpool);
      this_edit_path = svn_path_join(edit_path, filename, subpool);

      /* If this is an excluded path, exclude it. */
      SVN_ERR(svn_path_get_absolute(&abs_path, this_path, subpool));
      if (apr_hash_get(excludes, abs_path, APR_HASH_KEY_STRING))
        continue;

      if ((!no_ignore) && svn_wc_match_ignore_list(filename, ignores,
                                                   subpool))
        continue;

      if (dirent->kind == svn_node_dir && depth >= svn_depth_immediates)
        {
          void *this_dir_baton;

          /* Add the new subdirectory, getting a descent baton from
             the editor. */
          SVN_ERR(editor->add_directory(this_edit_path, dir_baton,
                                        NULL, SVN_INVALID_REVNUM, subpool,
                                        &this_dir_baton));

          /* Remember that the repository was modified */
          import_ctx->repos_changed = TRUE;

          /* By notifying before the recursive call below, we display
             a directory add before displaying adds underneath the
             directory.  To do it the other way around, just move this
             after the recursive call. */
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify
                = svn_wc_create_notify(this_path, svn_wc_notify_commit_added,
                                       subpool);
              notify->kind = svn_node_dir;
              notify->content_state = notify->prop_state
                = svn_wc_notify_state_inapplicable;
              notify->lock_state = svn_wc_notify_lock_state_inapplicable;
              (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
            }

          /* Recurse. */
          {
            svn_depth_t depth_below_here = depth;
            if (depth == svn_depth_immediates)
              depth_below_here = svn_depth_empty;

            SVN_ERR(import_dir(editor, this_dir_baton, this_path,
                               this_edit_path, depth_below_here, excludes,
                               no_ignore, ignore_unknown_node_types,
                               import_ctx, ctx,
                               subpool));
          }

          /* Finally, close the sub-directory. */
          SVN_ERR(editor->close_directory(this_dir_baton, subpool));
        }
      else if (dirent->kind == svn_node_file && depth >= svn_depth_files)
        {
          SVN_ERR(import_file(editor, dir_baton, this_path,
                              this_edit_path, import_ctx, ctx, subpool));
        }
      else if (dirent->kind != svn_node_dir && dirent->kind != svn_node_file)
        {
          if (ignore_unknown_node_types)
            {
              /*## warn about it*/
              if (ctx->notify_func2)
                {
                  svn_wc_notify_t *notify
                    = svn_wc_create_notify(this_path,
                                           svn_wc_notify_skip, subpool);
                  notify->kind = svn_node_dir;
                  notify->content_state = notify->prop_state
                    = svn_wc_notify_state_inapplicable;
                  notify->lock_state = svn_wc_notify_lock_state_inapplicable;
                  (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
                }
            }
          else
            return svn_error_createf
              (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
               _("Unknown or unversionable type for '%s'"),
               svn_path_local_style(this_path, subpool));
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Recursively import PATH to a repository using EDITOR and
 * EDIT_BATON.  PATH can be a file or directory.
 *
 * DEPTH is the depth at which to import PATH; it behaves as for
 * svn_client_import3().
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
import(const char *path,
       apr_array_header_t *new_entries,
       const svn_delta_editor_t *editor,
       void *edit_baton,
       svn_depth_t depth,
       apr_hash_t *excludes,
       svn_boolean_t no_ignore,
       svn_boolean_t ignore_unknown_node_types,
       svn_client_ctx_t *ctx,
       apr_pool_t *pool)
{
  void *root_baton;
  svn_node_kind_t kind;
  apr_array_header_t *ignores;
  apr_array_header_t *batons = NULL;
  const char *edit_path = "";
  import_ctx_t *import_ctx = apr_pcalloc(pool, sizeof(*import_ctx));

  /* Get a root dir baton.  We pass an invalid revnum to open_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));

  /* Make the intermediate directory components necessary for properly
     rooting our import source tree.  */
  if (new_entries->nelts)
    {
      int i;

      batons = apr_array_make(pool, new_entries->nelts, sizeof(void *));
      for (i = 0; i < new_entries->nelts; i++)
        {
          const char *component = APR_ARRAY_IDX(new_entries, i, const char *);
          edit_path = svn_path_join(edit_path, component, pool);

          /* If this is the last path component, and we're importing a
             file, then this component is the name of the file, not an
             intermediate directory. */
          if ((i == new_entries->nelts - 1) && (kind == svn_node_file))
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
  else if (kind == svn_node_file)
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

  if (kind == svn_node_file)
    {
      svn_boolean_t ignores_match = FALSE;

      if (!no_ignore)
        {
          SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));
          ignores_match = svn_wc_match_ignore_list(path, ignores, pool);
        }
      if (!ignores_match)
        SVN_ERR(import_file(editor, root_baton, path, edit_path,
                            import_ctx, ctx, pool));
    }
  else if (kind == svn_node_dir)
    {
      SVN_ERR(import_dir(editor, root_baton, path, edit_path,
                         depth, excludes, no_ignore,
                         ignore_unknown_node_types, import_ctx, ctx, pool));

    }
  else if (kind == svn_node_none
           || kind == svn_node_unknown)
    {
      return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' does not exist"),
                               svn_path_local_style(path, pool));
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


static svn_error_t *
get_ra_editor(svn_ra_session_t **ra_session,
              const svn_delta_editor_t **editor,
              void **edit_baton,
              svn_client_ctx_t *ctx,
              const char *base_url,
              const char *base_dir,
              svn_wc_adm_access_t *base_access,
              const char *log_msg,
              apr_array_header_t *commit_items,
              const apr_hash_t *revprop_table,
              svn_commit_info_t **commit_info_p,
              svn_boolean_t is_commit,
              apr_hash_t *lock_tokens,
              svn_boolean_t keep_locks,
              apr_pool_t *pool)
{
  void *commit_baton;
  apr_hash_t *commit_revprops;

  /* Open an RA session to URL. */
  SVN_ERR(svn_client__open_ra_session_internal(ra_session,
                                               base_url, base_dir,
                                               base_access, commit_items,
                                               is_commit, !is_commit,
                                               ctx, pool));

  /* If this is an import (aka, not a commit), we need to verify that
     our repository URL exists. */
  if (! is_commit)
    {
      svn_node_kind_t kind;

      SVN_ERR(svn_ra_check_path(*ra_session, "", SVN_INVALID_REVNUM,
                                &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf(SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
                                 _("Path '%s' does not exist"),
                                 base_url);
    }

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_client__commit_get_baton(&commit_baton, commit_info_p, pool));
  return svn_ra_get_commit_editor3(*ra_session, editor, edit_baton,
                                   commit_revprops,
                                   svn_client__commit_callback,
                                   commit_baton, lock_tokens, keep_locks,
                                   pool);
}


/*** Public Interfaces. ***/

svn_error_t *
svn_client_import3(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const char *log_msg = "";
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_session_t *ra_session;
  apr_hash_t *excludes = apr_hash_make(pool);
  svn_node_kind_t kind;
  const char *base_dir = path;
  apr_array_header_t *new_entries = apr_array_make(pool, 4,
                                                   sizeof(const char *));
  const char *temp;
  const char *dir;
  apr_pool_t *subpool;

  /* Create a new commit item and add it to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* If there's a log message gatherer, create a temporary commit
         item array solely to help generate the log message.  The
         array is not used for the import itself. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, 1, sizeof(item));

      item = svn_client_commit_item3_create(pool);
      item->path = apr_pstrdup(pool, path);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
      if (tmp_file)
        {
          const char *abs_path;
          SVN_ERR(svn_path_get_absolute(&abs_path, tmp_file, pool));
          apr_hash_set(excludes, abs_path, APR_HASH_KEY_STRING, (void *)1);
        }
    }

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_file)
    svn_path_split(path, &base_dir, NULL, pool);

  /* Figure out all the path components we need to create just to have
     a place to stick our imported tree. */
  subpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(subpool);

      /* See if the user is interested in cancelling this operation. */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (err)
        {
          /* If get_ra_editor below failed we either tried to open
             an invalid url, or else some other kind of error.  In case
             the url was bad we back up a directory and try again. */

          if (err->apr_err != SVN_ERR_FS_NO_SUCH_ENTRY)
            return err;
          else
            svn_error_clear(err);

          svn_path_split(url, &temp, &dir, pool);
          APR_ARRAY_PUSH(new_entries, const char *) =
            svn_path_uri_decode(dir, pool);
          url = temp;
        }
    }
  while ((err = get_ra_editor(&ra_session,
                              &editor, &edit_baton, ctx, url, base_dir,
                              NULL, log_msg, NULL, revprop_table,
                              commit_info_p, FALSE, NULL, TRUE, subpool)));

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

  /* An empty NEW_ENTRIES list the first call to get_ra_editor() above
     succeeded.  That means that URL corresponds to an already
     existing filesystem entity. */
  if (kind == svn_node_file && (! new_entries->nelts))
    return svn_error_createf
      (SVN_ERR_ENTRY_EXISTS, NULL,
       _("Path '%s' already exists"), url);

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
                           pool))
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, NULL,
       _("'%s' is a reserved name and cannot be imported"),
       /* ### Is svn_path_local_style() really necessary for this? */
       svn_path_local_style(temp, pool));


  /* If an error occurred during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if ((err = import(path, new_entries, editor, edit_baton,
                    depth, excludes, no_ignore,
                    ignore_unknown_node_types, ctx, subpool)))
    {
      svn_error_clear(editor->abort_edit(edit_baton, subpool));
      return err;
    }

  /* Transfer *COMMIT_INFO from the subpool to the callers pool */
  if (*commit_info_p)
    *commit_info_p = svn_commit_info_dup(*commit_info_p, pool);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
remove_tmpfiles(apr_hash_t *tempfiles,
                apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Make a subpool. */
  subpool = svn_pool_create(pool);

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first(pool, tempfiles); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_error_t *err;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);

      err = svn_io_remove_file((const char *)key, subpool);

      if (err)
        {
          if (! APR_STATUS_IS_ENOENT(err->apr_err))
            return err;
          else
            svn_error_clear(err);
        }
    }

  /* Remove the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



static svn_error_t *
reconcile_errors(svn_error_t *commit_err,
                 svn_error_t *unlock_err,
                 svn_error_t *bump_err,
                 svn_error_t *cleanup_err,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || bump_err || cleanup_err))
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

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap
        (cleanup_err, _("Error in post-commit clean-up (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, cleanup_err);
    }

  return err;
}

/* Remove redundancies by removing duplicates from NONRECURSIVE_TARGETS,
 * and removing any target that either is, or is a descendant of, a path in
 * RECURSIVE_TARGETS.  Return the result in *PUNIQUE_TARGETS.
 */
static svn_error_t *
remove_redundancies(apr_array_header_t **punique_targets,
                    const apr_array_header_t *nonrecursive_targets,
                    const apr_array_header_t *recursive_targets,
                    apr_pool_t *pool)
{
  apr_pool_t *temp_pool;
  apr_array_header_t *abs_recursive_targets = NULL;
  apr_hash_t *abs_targets;
  apr_array_header_t *rel_targets;
  int i;

  if ((nonrecursive_targets->nelts <= 0) || (! punique_targets))
    {
      /* No targets or no place to store our work means this function
         really has nothing to do. */
      if (punique_targets)
        *punique_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Initialize our temporary pool. */
  temp_pool = svn_pool_create(pool);

  /* Create our list of absolute paths for our "keepers" */
  abs_targets = apr_hash_make(temp_pool);

  /* Create our list of absolute paths for our recursive targets */
  if (recursive_targets)
    {
      abs_recursive_targets = apr_array_make(temp_pool,
                                             recursive_targets->nelts,
                                             sizeof(const char *));

      for (i = 0; i < recursive_targets->nelts; i++)
        {
          const char *rel_path =
            APR_ARRAY_IDX(recursive_targets, i, const char *);
          const char *abs_path;

          /* Get the absolute path for this target. */
          SVN_ERR(svn_path_get_absolute(&abs_path, rel_path, temp_pool));

          APR_ARRAY_PUSH(abs_recursive_targets, const char *) = abs_path;
        }
    }

  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make(pool, nonrecursive_targets->nelts,
                               sizeof(const char *));

  /* For each target in our list we do the following:

     1. Calculate its absolute path (ABS_PATH).
     2. See if any of the keepers in RECURSIVE_TARGETS is a parent of, or
        is the same path as, ABS_PATH.  If so, we ignore this
        target.  If not, however, add this target's original path to
        REL_TARGETS. */
  for (i = 0; i < nonrecursive_targets->nelts; i++)
    {
      const char *rel_path = APR_ARRAY_IDX(nonrecursive_targets, i,
                                           const char *);
      const char *abs_path;
      int j;
      svn_boolean_t keep_me;

      /* Get the absolute path for this target. */
      SVN_ERR(svn_path_get_absolute(&abs_path, rel_path, temp_pool));

      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;

      if (abs_recursive_targets)
        {
          for (j = 0; j < abs_recursive_targets->nelts; j++)
            {
              const char *keeper = APR_ARRAY_IDX(abs_recursive_targets, j,
                                                 const char *);

              /* Quit here if we find this path already in the keepers. */
              if (strcmp(keeper, abs_path) == 0)
                {
                  keep_me = FALSE;
                  break;
                }

              /* Quit here if this path is a child of one of the keepers. */
              if (svn_path_is_child(keeper, abs_path, temp_pool))
                {
                  keep_me = FALSE;
                  break;
                }
            }
        }

      /* If this is a new keeper, add its absolute path to ABS_TARGETS
         and its original path to REL_TARGETS. */
      if (keep_me
          && apr_hash_get(abs_targets, abs_path, APR_HASH_KEY_STRING) == NULL)
        {
          APR_ARRAY_PUSH(rel_targets, const char *) = rel_path;
          apr_hash_set(abs_targets, abs_path, APR_HASH_KEY_STRING, abs_path);
        }
    }

  /* Destroy our temporary pool. */
  svn_pool_destroy(temp_pool);

  /* Make sure we return the list of untainted keeper paths. */
  *punique_targets = rel_targets;

  return SVN_NO_ERROR;
}

/* Adjust relative targets.  If there is an empty string in REL_TARGETS
 * get the actual target anchor point.  It is likely that this is one dir up
 * from BASE_DIR, therefor we need to prepend the name part of the actual
 * target to all paths in REL_TARGETS.  Return the new anchor in *PBASE_DIR,
 * and the adjusted relative paths in *PREL_TARGETS.
 */
static svn_error_t *
adjust_rel_targets(const char **pbase_dir,
                   apr_array_header_t **prel_targets,
                   const char *base_dir,
                   apr_array_header_t *rel_targets,
                   apr_pool_t *pool)
{
  const char *target;
  int i;
  svn_boolean_t anchor_one_up = FALSE;
  apr_array_header_t *new_rel_targets;

  for (i = 0; i < rel_targets->nelts; i++)
    {
      target = APR_ARRAY_IDX(rel_targets, i, const char *);

      if (target[0] == '\0')
        {
          anchor_one_up = TRUE;
          break;
        }
    }

  /* Default to not doing anything */
  new_rel_targets = rel_targets;

  if (anchor_one_up)
    {
      const char *parent_dir, *name;

      SVN_ERR(svn_wc_get_actual_target(base_dir, &parent_dir, &name, pool));

      if (*name)
        {
          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          base_dir = apr_pstrdup(pool, parent_dir);

          new_rel_targets = apr_array_make(pool, rel_targets->nelts,
                                           sizeof(name));
          for (i = 0; i < rel_targets->nelts; i++)
            {
              target = APR_ARRAY_IDX(rel_targets, i, const char *);
              target = svn_path_join(name, target, pool);
              APR_ARRAY_PUSH(new_rel_targets, const char *) = target;
            }
         }
    }

  *pbase_dir = base_dir;
  *prel_targets = new_rel_targets;

  return SVN_NO_ERROR;
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
  size_t base_len = strlen(base_url);

  *result = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, all_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *url;
      const char *token;

      apr_hash_this(hi, &key, NULL, &val);
      url = key;
      token = val;

      if (strncmp(base_url, url, base_len) == 0
          && (url[base_len] == '\0' || url[base_len] == '/'))
        {
          if (url[base_len] == '\0')
            url = "";
          else
            url = svn_path_uri_decode(url + base_len + 1, pool);
          apr_hash_set(*result, url, APR_HASH_KEY_STRING, token);
        }
    }

  return SVN_NO_ERROR;
}

struct post_commit_baton
{
  svn_wc_committed_queue_t *queue;
  apr_pool_t *qpool;
  svn_wc_adm_access_t *base_dir_access;
  svn_boolean_t keep_changelists;
  svn_boolean_t keep_locks;
  apr_hash_t *checksums;
};

static svn_error_t *
post_process_commit_item(void *baton, void *this_item, apr_pool_t *pool)
{
  struct post_commit_baton *btn = baton;
  apr_pool_t *subpool = btn->qpool;

  svn_client_commit_item3_t *item =
    *(svn_client_commit_item3_t **)this_item;
  svn_boolean_t loop_recurse = FALSE;
  const char *adm_access_path;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t remove_lock;
  svn_error_t *bump_err;

  if (item->kind == svn_node_dir)
    adm_access_path = item->path;
  else
    svn_path_split(item->path, &adm_access_path, NULL, pool);

  bump_err = svn_wc_adm_retrieve(&adm_access, btn->base_dir_access,
                                 adm_access_path, pool);
  if (bump_err
      && bump_err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* Is it a directory that was deleted in the commit?
         Then we probably committed a missing directory. */
      if (item->kind == svn_node_dir
          && item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        {
          /* Mark it as deleted in the parent. */
          svn_error_clear(bump_err);
          return svn_wc_mark_missing_deleted(item->path,
                                             btn->base_dir_access, pool);
        }
    }
  if (bump_err)
    return bump_err;


  if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
      && (item->kind == svn_node_dir)
      && (item->copyfrom_url))
    loop_recurse = TRUE;

  remove_lock = (! btn->keep_locks && (item->state_flags
                                       & SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN));

  /* Allocate the queue in a longer-lived pool than (iter)pool:
     we want it to survive the next iteration. */
  return svn_wc_queue_committed2(btn->queue, item->path, adm_access,
                                 loop_recurse, item->incoming_prop_changes,
                                 remove_lock, !btn->keep_changelists,
                                 apr_hash_get(btn->checksums,
                                              item->path,
                                              APR_HASH_KEY_STRING),
                                 subpool);
}


static svn_error_t *
commit_item_is_changed(void *baton, void *this_item, apr_pool_t *pool)
{
  svn_client_commit_item3_t **item = this_item;

  if ((*item)->state_flags != SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
    svn_iter_break(pool);

  return SVN_NO_ERROR;
}

struct lock_dirs_baton
{
  svn_client_ctx_t *ctx;
  svn_wc_adm_access_t *base_dir_access;
  int levels_to_lock;
};

static svn_error_t *
lock_dirs_for_commit(void *baton, void *this_item, apr_pool_t *pool)
{
  struct lock_dirs_baton *btn = baton;
  svn_wc_adm_access_t *adm_access;

  return svn_wc_adm_open3(&adm_access, btn->base_dir_access,
                          *(const char **)this_item,
                          TRUE, /* Write lock */
                          btn->levels_to_lock,
                          btn->ctx->cancel_func,
                          btn->ctx->cancel_baton,
                          pool);
}

struct check_dir_delete_baton
{
  svn_wc_adm_access_t *base_dir_access;
  svn_depth_t depth;
};

static svn_error_t *
check_nonrecursive_dir_delete(void *baton, void *this_item, apr_pool_t *pool)
{
  struct check_dir_delete_baton *btn = baton;
  svn_wc_adm_access_t *adm_access;
  const char *target;

  SVN_ERR(svn_path_get_absolute(&target, *(const char **)this_item, pool));
  SVN_ERR_W(svn_wc_adm_probe_retrieve(&adm_access, btn->base_dir_access,
                                      target, pool),
            _("Are all the targets part of the same working copy?"));

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
  */
  if (btn->depth != svn_depth_infinity)
    {
      svn_wc_status2_t *status;
      svn_node_kind_t kind;

      SVN_ERR(svn_io_check_path(target, &kind, pool));

      if (kind == svn_node_dir)
        {
          SVN_ERR(svn_wc_status2(&status, target, adm_access, pool));
          if (status->text_status == svn_wc_status_deleted ||
              status->text_status == svn_wc_status_replaced)
            {
              apr_hash_t* entries;

              SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));

              if (apr_hash_count(entries) > 1)
                return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                    _("Cannot non-recursively commit a "
                                      "directory deletion of a directory "
                                      "with child nodes"));
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_commit4(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_session_t *ra_session;
  const char *log_msg;
  const char *base_dir;
  const char *base_url;
  const char *target;
  apr_array_header_t *rel_targets;
  apr_array_header_t *dirs_to_lock;
  apr_array_header_t *dirs_to_lock_recursive;
  svn_boolean_t lock_base_dir_recursive = FALSE;
  apr_hash_t *committables;
  apr_hash_t *lock_tokens;
  apr_hash_t *tempfiles = NULL;
  apr_hash_t *checksums;
  svn_wc_adm_access_t *base_dir_access;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR, *unlock_err = SVN_NO_ERROR;
  svn_error_t *bump_err = SVN_NO_ERROR, *cleanup_err = SVN_NO_ERROR;
  svn_boolean_t commit_in_progress = FALSE;
  const char *current_dir = "";
  const char *notify_prefix;
  int i;

  /* Committing URLs doesn't make sense, so error if it's tried. */
  for (i = 0; i < targets->nelts; i++)
    {
      target = APR_ARRAY_IDX(targets, i, const char *);
      if (svn_path_is_url(target))
        return svn_error_createf
          (SVN_ERR_ILLEGAL_TARGET, NULL,
           _("'%s' is a URL, but URLs cannot be commit targets"), target);
    }

  /* Condense the target list. */
  SVN_ERR(svn_path_condense_targets(&base_dir, &rel_targets, targets,
                                    depth == svn_depth_infinity, pool));

  /* No targets means nothing to commit, so just return. */
  if (! base_dir)
    goto cleanup;

  /* When svn_path_condense_targets() was written, we didn't have real
   * depths, we just had recursive / nonrecursive.
   *
   * Nowadays things are more complex.  If depth == svn_depth_files,
   * for example, and two targets are "foo" and "foo/bar", then
   * ideally we should condense out "foo/bar" if it's a file and not
   * if it's a directory.  And, of course, later when we get adm
   * access batons for the commit, we'd ideally lock directories to
   * precisely the depth required and no deeper.
   *
   * But for now we don't do that.  Instead, we lock recursively from
   * base_dir, if depth indicates that we might need anything below
   * there (but note that above, we don't condense away targets that
   * need to be named explicitly when depth != svn_depth_infinity).
   *
   * Here's a case where this all matters:
   *
   *    $ svn st -q
   *    M      A/D/G/rho
   *    M      iota
   *    $ svn ci -m "log msg" --depth=immediates . A/D/G
   *
   * If we don't lock base_dir recursively, then it will get an error...
   *
   *    subversion/libsvn_wc/lock.c:570: (apr_err=155004)
   *    svn: Working copy '/blah/blah/blah/wc' locked
   *    svn: run 'svn cleanup' to remove locks \
   *         (type 'svn help cleanup' for details)
   *
   * ...because later (see dirs_to_lock_recursively and dirs_to_lock)
   * we'd call svn_wc_adm_open3() to get access objects for "" and
   * "A/D/G", but the request for "" would fail because base_dir_access
   * would already be open for that directory.  (In that circumstance,
   * you're supposed to use svn_wc_adm_retrieve() instead; but it
   * would be clumsy to have a conditional path just to decide between
   * open3() and retrieve().)
   *
   * (Note that the results would be the same if even the working copy
   * were an explicit argument, e.g.:
   * 'svn ci -m "log msg" --depth=immediates wc wc/A/D/G'.)
   *
   * So we set lock_base_dir_recursive=TRUE now, and end up locking
   * more than we need to, but this keeps the code simple and correct.
   *
   * In an inspired bit of foresight, the adm locking code anticipated
   * the eventual addition of svn_depth_immediates, and allows us to
   * set the exact number of lock levels.  So optimizing the code here
   * at least shouldn't require any changes to the adm locking system.
   */
  if (depth == svn_depth_files || depth == svn_depth_immediates)
    {
      for (i = 0; i < rel_targets->nelts; ++i)
        {
          const char *rel_target = APR_ARRAY_IDX(rel_targets, i, const char *);

          if (rel_target[0] == '\0')
            {
              lock_base_dir_recursive = TRUE;
              break;
            }
        }
    }

  /* Prepare an array to accumulate dirs to lock */
  dirs_to_lock = apr_array_make(pool, 1, sizeof(target));
  dirs_to_lock_recursive = apr_array_make(pool, 1, sizeof(target));

  /* If we calculated only a base_dir and no relative targets, this
     must mean that we are being asked to commit (effectively) a
     single path. */
  if ((! rel_targets) || (! rel_targets->nelts))
    {
      const char *parent_dir, *name;

      SVN_ERR(svn_wc_get_actual_target(base_dir, &parent_dir, &name, pool));
      if (*name)
        {
          svn_node_kind_t kind;

          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          base_dir = apr_pstrdup(pool, parent_dir);

          /* Make the array if it wasn't already created. */
          if (! rel_targets)
            rel_targets = apr_array_make(pool, targets->nelts, sizeof(name));

          /* Now, push this name as a relative path to our new
             base directory. */
          APR_ARRAY_PUSH(rel_targets, const char *) = name;

          target = svn_path_join(base_dir, name, pool);
          SVN_ERR(svn_io_check_path(target, &kind, pool));

          /* If the final target is a dir, we want to recursively lock it */
          if (kind == svn_node_dir)
            {
              if (depth == svn_depth_infinity || depth == svn_depth_immediates)
                APR_ARRAY_PUSH(dirs_to_lock_recursive, const char *) = target;
              else
                APR_ARRAY_PUSH(dirs_to_lock, const char *) = target;
            }
        }
      else
        {
          /* Unconditionally lock recursively down from base_dir. */
          lock_base_dir_recursive = TRUE;
        }
    }
  else if (! lock_base_dir_recursive)
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      SVN_ERR(adjust_rel_targets(&base_dir, &rel_targets,
                                 base_dir, rel_targets,
                                 pool));

      for (i = 0; i < rel_targets->nelts; i++)
        {
          svn_node_kind_t kind;

          svn_pool_clear(subpool);

          target = svn_path_join(base_dir,
                                 APR_ARRAY_IDX(rel_targets, i, const char *),
                                 subpool);

          SVN_ERR(svn_io_check_path(target, &kind, subpool));

          /* If the final target is a dir, we want to lock it */
          if (kind == svn_node_dir)
            {
              /* Notice how here we test infinity||immediates, but up
                 in the call to svn_path_condense_targets(), we only
                 tested depth==infinity.  That's because condensation
                 and adm lock acquisition serve different purposes. */
              if (depth == svn_depth_infinity || depth == svn_depth_immediates)
                APR_ARRAY_PUSH(dirs_to_lock_recursive,
                               const char *) = apr_pstrdup(pool, target);
              else
                /* Don't lock if target is the base_dir, base_dir will be
                   locked anyway and we can't lock it twice */
                if (strcmp(target, base_dir) != 0)
                  APR_ARRAY_PUSH(dirs_to_lock,
                                 const char *) = apr_pstrdup(pool, target);
            }

          /* Now we need to iterate over the parent paths of this path
             adding them to the set of directories we want to lock.
             Do nothing if target is already the base_dir. */
          if (strcmp(target, base_dir) != 0)
            {
              target = svn_dirent_dirname(target, subpool);

              while (strcmp(target, base_dir) != 0)
                {
                  const char *parent_dir;

                  APR_ARRAY_PUSH(dirs_to_lock,
                                 const char *) = apr_pstrdup(pool, target);

                  parent_dir = svn_dirent_dirname(target, subpool);

                  if (strcmp(parent_dir, target) == 0)
                    break; /* Reached root directory */
                  
                  target = parent_dir;
                }
            }
        }

      svn_pool_destroy(subpool);
    }

  SVN_ERR(svn_wc_adm_open3(&base_dir_access, NULL, base_dir,
                           TRUE,  /* Write lock */
                           lock_base_dir_recursive ? -1 : 0, /* lock levels */
                           ctx->cancel_func, ctx->cancel_baton,
                           pool));

  if (!lock_base_dir_recursive)
    {
      apr_array_header_t *unique_dirs_to_lock;
      struct lock_dirs_baton btn;

      /* Sort the paths in a depth-last directory-ish order. */
      qsort(dirs_to_lock->elts, dirs_to_lock->nelts,
            dirs_to_lock->elt_size, svn_sort_compare_paths);
      qsort(dirs_to_lock_recursive->elts, dirs_to_lock_recursive->nelts,
            dirs_to_lock_recursive->elt_size, svn_sort_compare_paths);

      /* Remove any duplicates */
      SVN_ERR(svn_path_remove_redundancies(&unique_dirs_to_lock,
                                           dirs_to_lock_recursive,
                                           pool));
      dirs_to_lock_recursive = unique_dirs_to_lock;

      /* Remove dirs and descendants from dirs_to_lock if there is
         any ancestor in dirs_to_lock_recursive */
      SVN_ERR(remove_redundancies(&unique_dirs_to_lock,
                                  dirs_to_lock,
                                  dirs_to_lock_recursive,
                                  pool));
      dirs_to_lock = unique_dirs_to_lock;

      btn.base_dir_access = base_dir_access;
      btn.ctx = ctx;
      btn.levels_to_lock = 0;
      /* First lock all the dirs to be locked non-recursively */
      if (dirs_to_lock)
        SVN_ERR(svn_iter_apr_array(NULL, dirs_to_lock,
                                   lock_dirs_for_commit, &btn, pool));

      /* Lock the rest of the targets (recursively) */
      btn.levels_to_lock = -1;
      if (dirs_to_lock_recursive)
        SVN_ERR(svn_iter_apr_array(NULL, dirs_to_lock_recursive,
                                   lock_dirs_for_commit, &btn, pool));
    }

  /* One day we might support committing from multiple working copies, but
     we don't yet.  This check ensures that we don't silently commit a
     subset of the targets.

     At the same time, if a non-recursive commit is desired, do not
     allow a deleted directory as one of the targets. */
  {
    struct check_dir_delete_baton btn;

    btn.base_dir_access = base_dir_access;
    btn.depth = depth;
    SVN_ERR(svn_iter_apr_array(NULL, targets,
                               check_nonrecursive_dir_delete, &btn,
                               pool));
  }

  /* Crawl the working copy for commit items. */
  if ((cmt_err = svn_client__harvest_committables(&committables,
                                                  &lock_tokens,
                                                  base_dir_access,
                                                  rel_targets,
                                                  depth,
                                                  ! keep_locks,
                                                  changelists,
                                                  ctx,
                                                  pool)))
    goto cleanup;

  /* ### todo: Currently there should be only one hash entry, which
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away
     and be replaced with a canonical repos URL, and from there we
     are poised to started handling nested working copies.  See
     http://subversion.tigris.org/issues/show_bug.cgi?id=960. */
  if (! ((commit_items = apr_hash_get(committables,
                                      SVN_CLIENT__SINGLE_REPOS_NAME,
                                      APR_HASH_KEY_STRING))))
    goto cleanup;

  /* If our array of targets contains only locks (and no actual file
     or prop modifications), then we return here to avoid committing a
     revision with no changes. */
  {
    svn_boolean_t not_found_changed_path = TRUE;


    cmt_err = svn_iter_apr_array(&not_found_changed_path,
                                 commit_items,
                                 commit_item_is_changed, NULL, pool);
    if (not_found_changed_path || cmt_err)
      goto cleanup;
  }

  /* Go get a log message.  If an error occurs, or no log message is
     specified, abort the operation. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      const char *tmp_file;
      cmt_err = svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                        ctx, pool);

      if (cmt_err || (! log_msg))
        goto cleanup;
    }
  else
    log_msg = "";

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items(&base_url,
                                                   commit_items,
                                                   pool)))
    goto cleanup;

  /* Collect our lock tokens with paths relative to base_url. */
  if ((cmt_err = collect_lock_tokens(&lock_tokens, lock_tokens, base_url,
                                     pool)))
    goto cleanup;

  if ((cmt_err = get_ra_editor(&ra_session,
                               &editor, &edit_baton, ctx,
                               base_url, base_dir, base_dir_access, log_msg,
                               commit_items, revprop_table, commit_info_p,
                               TRUE, lock_tokens, keep_locks, pool)))
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  if ((cmt_err = svn_path_get_absolute(&current_dir,
                                       current_dir, pool)))
    goto cleanup;

  /* Determine prefix to strip from the commit notify messages */
  notify_prefix = svn_path_get_longest_ancestor(current_dir, base_dir, pool);

  /* Perform the commit. */
  cmt_err = svn_client__do_commit(base_url, commit_items, base_dir_access,
                                  editor, edit_baton,
                                  notify_prefix,
                                  &tempfiles, &checksums, ctx, pool);

  /* Handle a successful commit. */
  if ((! cmt_err)
      || (cmt_err->apr_err == SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      svn_wc_committed_queue_t *queue = svn_wc_committed_queue_create(pool);
      struct post_commit_baton btn;

      btn.queue = queue;
      btn.qpool = pool;
      btn.base_dir_access = base_dir_access;
      btn.keep_changelists = keep_changelists;
      btn.keep_locks = keep_locks;
      btn.checksums = checksums;

      /* Make a note that our commit is finished. */
      commit_in_progress = FALSE;

      bump_err = svn_iter_apr_array(NULL, commit_items,
                                    post_process_commit_item, &btn,
                                    pool);
      if (bump_err)
        goto cleanup;

      SVN_ERR_ASSERT(*commit_info_p);
      bump_err
        = svn_wc_process_committed_queue(queue, base_dir_access,
                                         (*commit_info_p)->revision,
                                         (*commit_info_p)->date,
                                         (*commit_info_p)->author,
                                         pool);
    }

  /* Sleep to ensure timestamp integrity. */
  svn_io_sleep_for_timestamps(base_dir, pool);

 cleanup:
  /* Abort the commit if it is still in progress. */
  if (commit_in_progress)
    svn_error_clear(editor->abort_edit(edit_baton, pool));

  /* A bump error is likely to occur while running a working copy log file,
     explicitly unlocking and removing temporary files would be wrong in
     that case.  A commit error (cmt_err) should only occur before any
     attempt to modify the working copy, so it doesn't prevent explicit
     clean-up. */
  if (! bump_err)
    {
      unlock_err = svn_wc_adm_close2(base_dir_access, pool);

      if (! unlock_err)
        cleanup_err = remove_tmpfiles(tempfiles, pool);
    }

  /* As per our promise, if *commit_info_p isn't set, provide a default where
     rev = SVN_INVALID_REVNUM. */
  if (! *commit_info_p)
    *commit_info_p = svn_create_commit_info(pool);

  return reconcile_errors(cmt_err, unlock_err, bump_err, cleanup_err, pool);
}
