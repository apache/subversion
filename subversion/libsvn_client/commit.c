/*
 * commit.c:  wrappers around wc commit functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include <assert.h>
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
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"
#include "svn_md5.h"
#include "svn_time.h"

#include "client.h"

#include "svn_private_config.h"

/* Apply PATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.
   PROPERTIES is the set of node properties set on this file.

   Fill DIGEST with the md5 checksum of the sent file; DIGEST must be
   at least APR_MD5_DIGESTSIZE bytes long. */
static svn_error_t *
send_file_contents (const char *path,
                    void *file_baton,
                    const svn_delta_editor_t *editor,
                    apr_hash_t *properties,
                    unsigned char *digest,
                    apr_pool_t *pool)
{
  const char *tmpfile_path = NULL;
  svn_stream_t *contents;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  apr_file_t *f = NULL;
  svn_error_t *err = SVN_NO_ERROR;
  const svn_string_t *eol_style_val = NULL, *keywords_val = NULL;
  svn_boolean_t special = FALSE;

  /* If there are properties, look for EOL-style and keywords ones. */
  if (properties)
    {
      eol_style_val = apr_hash_get (properties, SVN_PROP_EOL_STYLE,
                                    sizeof (SVN_PROP_EOL_STYLE) - 1);
      keywords_val = apr_hash_get (properties, SVN_PROP_KEYWORDS,
                                   sizeof (SVN_PROP_KEYWORDS) - 1);
      if (apr_hash_get (properties, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING))
        special = TRUE;
    }

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, NULL, pool,
                                    &handler, &handler_baton));

  /* If we have EOL styles or keywords to de-translate, do it.  Any
     EOL style gets translated to "\n", the repository-normal format.
     Keywords get unexpanded.  */
  if (eol_style_val || keywords_val || special)
    {
      svn_subst_keywords_t keywords = {0};
      const char *temp_dir;
      apr_file_t *tmp_f;

      /* Now create a new tempfile, and open a stream to it. */
      SVN_ERR (svn_io_temp_dir (&temp_dir, pool));
      SVN_ERR (svn_io_open_unique_file 
               (&tmp_f, &tmpfile_path,
                svn_path_join (temp_dir, "svn-import", pool),
                ".tmp", FALSE, pool));
      SVN_ERR (svn_io_file_close (tmp_f, pool));

      /* Generate a keyword structure. */
      if (keywords_val)
        SVN_ERR (svn_subst_build_keywords (&keywords, keywords_val->data, 
                                           APR_STRINGIFY(SVN_INVALID_REVNUM),
                                           "", 0, "", pool));
      
      if ((err = svn_subst_copy_and_translate2 (path, tmpfile_path,
                                                eol_style_val ? "\n" : NULL,
                                                FALSE,
                                                keywords_val ? &keywords : NULL,
                                                FALSE,
                                                special,
                                                pool)))
        goto cleanup;
    }

  /* Open our contents file, either the original path or the temporary
     copy we might have made above. */
  if ((err = svn_io_file_open (&f, tmpfile_path ? tmpfile_path : path, 
                               APR_READ, APR_OS_DEFAULT, pool)))
    goto cleanup;
  contents = svn_stream_from_aprfile (f, pool);

  /* Send the file's contents to the delta-window handler. */
  if ((err = svn_txdelta_send_stream (contents, handler, handler_baton,
                                      digest, pool)))
    goto cleanup;

  /* Close our contents file. */
  if ((err = svn_io_file_close (f, pool)))
    goto cleanup;

 cleanup:
  if (tmpfile_path)
    {
      /* If we used a tempfile, we need to close and remove it, too. */
      svn_error_t *err2 = svn_io_remove_file (tmpfile_path, pool);
      if (err && err2)
        svn_error_compose (err, err2);
      else if (err2)
        err = err2;
    }

  return err;
}


/* Import file PATH as EDIT_PATH in the repository directory indicated
 * by DIR_BATON in EDITOR.  
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for each
 * file.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file (const svn_delta_editor_t *editor,
             void *dir_baton,
             const char *path,
             const char *edit_path,
             svn_client_ctx_t *ctx,
             apr_pool_t *pool)
{
  void *file_baton;
  const char *mimetype;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  const char *text_checksum;
  apr_hash_t* properties;
  apr_hash_index_t *hi;
  svn_node_kind_t kind;

  /* Add the file, using the pool from the FILES hash. */
  SVN_ERR (editor->add_file (edit_path, dir_baton, NULL, SVN_INVALID_REVNUM, 
                             pool, &file_baton));

  SVN_ERR (svn_io_check_special_path (path, &kind, pool));

  if (kind != svn_node_special)
    {
      /* add automatic properties */
      SVN_ERR (svn_client__get_auto_props (&properties, &mimetype, path, ctx,
                                           pool));
    }
  else
    properties = apr_hash_make (pool);
      
  if (properties)
    {
      for (hi = apr_hash_first (pool, properties); hi; hi = apr_hash_next (hi))
        {
          const void *pname;
          void *pval;

          apr_hash_this (hi, &pname, NULL, &pval);
          SVN_ERR (editor->change_file_prop (file_baton, pname, pval, pool));
        }
    }

  if (ctx->notify_func)
    (*ctx->notify_func) (ctx->notify_baton,
                         path,
                         svn_wc_notify_commit_added,
                         svn_node_file,
                         mimetype,
                         svn_wc_notify_state_inapplicable,
                         svn_wc_notify_state_inapplicable,
                         SVN_INVALID_REVNUM);

  /* If this is a special file, we need to set the svn:special
     property and create a temporary detranslated version in order to
     send to the server. */
  if (kind == svn_node_special)
    {
      apr_hash_set (properties, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING,
                    svn_string_create (SVN_PROP_SPECIAL_VALUE, pool));
      SVN_ERR (editor->change_file_prop (file_baton, SVN_PROP_SPECIAL,
                                         apr_hash_get (properties,
                                                       SVN_PROP_SPECIAL,
                                                       APR_HASH_KEY_STRING),
                                         pool));
    }

  /* Now, transmit the file contents. */
  SVN_ERR (send_file_contents (path, file_baton, editor, 
                               properties, digest, pool));

  /* Finally, close the file. */
  text_checksum = svn_md5_digest_to_cstring (digest, pool);
  SVN_ERR (editor->close_file (file_baton, text_checksum, pool));

  return SVN_NO_ERROR;
}
             

/* Import directory PATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  EDIT_PATH is the path imported as the root
 * directory, so all edits are relative to that.
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for each
 * directory.
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 * 
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_dir (const svn_delta_editor_t *editor, 
            void *dir_baton,
            const char *path,
            const char *edit_path,
            svn_boolean_t nonrecursive,
            apr_hash_t *excludes,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);  /* iteration pool */
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *ignores;

  SVN_ERR (svn_wc_get_default_ignores (&ignores, ctx->config, pool));

  SVN_ERR (svn_io_get_dirents (&dirents, path, pool));

  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const char *this_path, *this_edit_path, *abs_path;
      const svn_node_kind_t *filetype;
      const char *filename;
      const void *key;
      void *val;
      
      svn_pool_clear (subpool);

      apr_hash_this (hi, &key, NULL, &val);

      filename = key;
      filetype = val;

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

      if (strcmp (filename, SVN_WC_ADM_DIR_NAME) == 0)
        {
          /* If someone's trying to import a directory named the same
             as our administrative directories, that's probably not
             what they wanted to do.  If they are importing a file
             with that name, something is bound to blow up when they
             checkout what they've imported.  So, just skip items with
             that name.  */
          if (ctx->notify_func)
            (*ctx->notify_func) (ctx->notify_baton,
                                 svn_path_join (path, filename, subpool),
                                 svn_wc_notify_skip,
                                 svn_node_dir,
                                 NULL,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable,
                                 SVN_INVALID_REVNUM);
          continue;
        }

      /* Typically, we started importing from ".", in which case
         edit_path is "".  So below, this_path might become "./blah",
         and this_edit_path might become "blah", for example. */
      this_path = svn_path_join (path, filename, subpool);
      this_edit_path = svn_path_join (edit_path, filename, subpool);

      /* If this is an excluded path, exclude it. */
      SVN_ERR (svn_path_get_absolute (&abs_path, this_path, subpool));
      if (apr_hash_get (excludes, abs_path, APR_HASH_KEY_STRING))
        continue;

      if (svn_cstring_match_glob_list (filename, ignores))
        continue;

      /* We only import subdirectories when we're doing a regular
         recursive import. */
      if ((*filetype == svn_node_dir) && (! nonrecursive))
        {
          void *this_dir_baton;

          /* Add the new subdirectory, getting a descent baton from
             the editor. */
          SVN_ERR (editor->add_directory (this_edit_path, dir_baton, 
                                          NULL, SVN_INVALID_REVNUM, subpool,
                                          &this_dir_baton));

          /* By notifying before the recursive call below, we display
             a directory add before displaying adds underneath the
             directory.  To do it the other way around, just move this
             after the recursive call. */
          if (ctx->notify_func)
            (*ctx->notify_func) (ctx->notify_baton,
                                 this_path,
                                 svn_wc_notify_commit_added,
                                 svn_node_dir,
                                 NULL,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable,
                                 SVN_INVALID_REVNUM);

          /* Recurse. */
          SVN_ERR (import_dir (editor, this_dir_baton, 
                               this_path, this_edit_path, 
                               FALSE, excludes, ctx, subpool));

          /* Finally, close the sub-directory. */
          SVN_ERR (editor->close_directory (this_dir_baton, subpool));
        }
      else if (*filetype == svn_node_file)
        {
          /* Import a file. */
          SVN_ERR (import_file (editor, dir_baton, 
                                this_path, this_edit_path, ctx, subpool));
        }
      /* We're silently ignoring things that aren't files or
         directories.  If we stop doing that, here is the place to
         change your world.  */
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* Recursively import PATH to a repository using EDITOR and
 * EDIT_BATON.  PATH can be a file or directory.
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
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for 
 * each imported path, passing actions svn_wc_notify_commit_added.
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 * 
 * Use POOL for any temporary allocation.
 *
 * Note: the repository directory receiving the import was specified
 * when the editor was fetched.  (I.e, when EDITOR->open_root() is
 * called, it returns a directory baton for that directory, which is
 * not necessarily the root.)
 */
static svn_error_t *
import (const char *path,
        apr_array_header_t *new_entries,
        const svn_delta_editor_t *editor,
        void *edit_baton,
        svn_boolean_t nonrecursive,
        apr_hash_t *excludes,
        svn_client_ctx_t *ctx,
        apr_pool_t *pool)
{
  void *root_baton;
  svn_node_kind_t kind;
  apr_array_header_t *ignores;
  apr_array_header_t *batons = NULL;
  const char *edit_path = "";

  /* Get a root dir baton.  We pass an invalid revnum to open_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, 
                              pool, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* Make the intermediate directory components necessary for properly
     rooting our import source tree.  */
  if (new_entries->nelts)
    {
      int i;

      batons = apr_array_make (pool, new_entries->nelts, sizeof (void *));
      for (i = 0; i < new_entries->nelts; i++)
        {
          const char *component = APR_ARRAY_IDX (new_entries, i, const char *);
          edit_path = svn_path_join (edit_path, component, pool);

          /* If this is the last path component, and we're importing a
             file, then this component is the name of the file, not an
             intermediate directory. */
          if ((i == new_entries->nelts - 1) && (kind == svn_node_file))
            break;

          *((void **) apr_array_push (batons)) = root_baton;
          SVN_ERR (editor->add_directory (edit_path,
                                          root_baton,
                                          NULL, SVN_INVALID_REVNUM,
                                          pool, &root_baton));
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
      SVN_ERR (svn_wc_get_default_ignores (&ignores, ctx->config, pool));
      if (! svn_cstring_match_glob_list (path, ignores))
        SVN_ERR (import_file (editor, root_baton, path, edit_path,
                              ctx, pool));
    }
  else if (kind == svn_node_dir)
    {
      SVN_ERR (import_dir (editor, root_baton, path, edit_path,
                           nonrecursive, excludes, ctx, pool));

    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf (SVN_ERR_NODE_UNKNOWN_KIND, NULL, 
                                _("'%s' does not exist"), path);  
    }

  /* Close up shop; it's time to go home. */
  SVN_ERR (editor->close_directory (root_baton, pool));
  if (batons && batons->nelts)
    {
      void **baton;
      while ((baton = (void **) apr_array_pop (batons)))
        {
          SVN_ERR (editor->close_directory (*baton, pool));
        }
    }

  SVN_ERR (editor->close_edit (edit_baton, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_ra_editor (void **ra_baton, 
               void **session,
               svn_ra_plugin_t **ra_lib,
               svn_revnum_t *latest_rev,
               const svn_delta_editor_t **editor,
               void **edit_baton,
               svn_client_ctx_t *ctx,
               const char *base_url,
               const char *base_dir,
               svn_wc_adm_access_t *base_access,
               const char *log_msg,
               apr_array_header_t *commit_items,
               svn_client_commit_info_t **commit_info,
               svn_boolean_t is_commit,
               apr_pool_t *pool)
{
  void *commit_baton;

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (ra_lib, *ra_baton, 
                                  base_url, pool));
  
  /* Open an RA session to URL. */
  SVN_ERR (svn_client__open_ra_session (session, *ra_lib,
                                        base_url, base_dir, base_access,
                                        commit_items,
                                        is_commit, !is_commit,
                                        ctx, pool));

  /* If this is an import (aka, not a commit), we need to verify that
     our repository URL exists. */
  if (! is_commit)
    {
      svn_node_kind_t kind;

      SVN_ERR ((*ra_lib)->check_path (*session, "", SVN_INVALID_REVNUM,
                                      &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
                                  _("Path '%s' does not exist"),
                                  base_url);
    }

  /* Fetch the latest revision if requested. */
  if (latest_rev)
    SVN_ERR ((*ra_lib)->get_latest_revnum (*session, latest_rev, pool));
  
  /* Fetch RA commit editor. */
  SVN_ERR (svn_client__commit_get_baton (&commit_baton, commit_info, pool));
  return (*ra_lib)->get_commit_editor (*session, editor, edit_baton, log_msg,
                                       svn_client__commit_callback,
                                       commit_baton, pool);
}


/*** Public Interfaces. ***/

svn_error_t *
svn_client_import (svn_client_commit_info_t **commit_info,
                   const char *path,
                   const char *url,
                   svn_boolean_t nonrecursive,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const char *log_msg = "";
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  apr_hash_t *excludes = apr_hash_make (pool);
  svn_node_kind_t kind;
  const char *base_dir = path;
  apr_array_header_t *new_entries = apr_array_make (pool, 4, 
                                                    sizeof (const char *));
  const char *temp;
  const char *dir;
  apr_pool_t *subpool;


  /* Create a new commit item and add it to the array. */
  if (ctx->log_msg_func)
    {
      /* If there's a log message gatherer, create a temporary commit
         item array solely to help generate the log message.  The
         array is not used for the import itself. */
      svn_client_commit_item_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items 
        = apr_array_make (pool, 1, sizeof (item));
      
      item = apr_pcalloc (pool, sizeof (*item));
      item->path = apr_pstrdup (pool, path);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      (*((svn_client_commit_item_t **) apr_array_push (commit_items))) = item;
      
      SVN_ERR ((*ctx->log_msg_func) (&log_msg, &tmp_file, commit_items, 
                                     ctx->log_msg_baton, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
      if (tmp_file)
        {
          const char *abs_path;
          SVN_ERR (svn_path_get_absolute (&abs_path, tmp_file, pool));
          apr_hash_set (excludes, abs_path, APR_HASH_KEY_STRING, (void *)1);
        }
    }

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_file)
    svn_path_split (path, &base_dir, NULL, pool);

  /* Figure out all the path components we need to create just to have
     a place to stick our imported tree. */
  subpool = svn_pool_create (pool);
  do
    {
      svn_pool_clear (subpool);

      /* See if the user is interested in cancelling this operation. */
      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

      if (err)
        {
          /* If get_ra_editor below failed we either tried to open
             an invalid url, or else some other kind of error.  In case
             the url was bad we back up a directory and try again. */
          
          if (err->apr_err != SVN_ERR_FS_NO_SUCH_ENTRY)
            return err;
          else
            svn_error_clear (err);
          
          svn_path_split (url, &temp, &dir, pool);
          *((const char **) apr_array_push (new_entries)) = 
            svn_path_uri_decode (dir, pool);
          url = temp;
        }
    }
  while ((err = get_ra_editor (&ra_baton, &session, &ra_lib, NULL,
                               &editor, &edit_baton, ctx, url, base_dir,
                               NULL, log_msg, NULL, commit_info,
                               FALSE, subpool)));

  /* Reverse the order of the components we added to our NEW_ENTRIES array. */
  if (new_entries->nelts)
    {
      int i, j;
      const char *component;
      for (i = 0; i < (new_entries->nelts / 2); i++)
        {
          j = new_entries->nelts - i - 1;
          component = 
            APR_ARRAY_IDX (new_entries, i, const char *);
          APR_ARRAY_IDX (new_entries, i, const char *) =
            APR_ARRAY_IDX (new_entries, j, const char *);
          APR_ARRAY_IDX (new_entries, j, const char *) = 
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
  if (new_entries->nelts && 
      (strcmp (APR_ARRAY_IDX (new_entries, 
                              new_entries->nelts - 1, 
                              const char *), SVN_WC_ADM_DIR_NAME) == 0))
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, NULL,
       _("'%s' is a reserved name and cannot be imported"),
       SVN_WC_ADM_DIR_NAME);


  /* If an error occurred during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if ((err = import (path, new_entries, editor, edit_baton, 
                     nonrecursive, excludes, ctx, pool)))
    {
      svn_error_clear (editor->abort_edit (edit_baton, pool));
      return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
remove_tmpfiles (apr_hash_t *tempfiles,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Make a subpool. */
  subpool = svn_pool_create (pool);

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first (pool, tempfiles); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_error_t *err;

      svn_pool_clear (subpool);
      apr_hash_this (hi, &key, NULL, &val);

      err = svn_io_remove_file ((const char *)key, subpool);

      if (err)
        {
          if (! APR_STATUS_IS_ENOENT (err->apr_err))
            return err;
          else
            svn_error_clear (err);
        }
    }

  /* Remove the subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



static svn_error_t *
reconcile_errors (svn_error_t *commit_err,
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
    err = svn_error_create (SVN_ERR_BASE, NULL,
                            _("Commit succeeded, but other errors follow:"));

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap 
        (unlock_err, _("Error unlocking locked dirs (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose (err, unlock_err);
    }

  /* If there was a bumping error... */
  if (bump_err)
    {
      /* Wrap the error with some headers. */
      bump_err = svn_error_quick_wrap 
        (bump_err, _("Error bumping revisions post-commit (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose (err, bump_err);
    }

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap 
        (cleanup_err, _("Error in post-commit clean-up (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose (err, cleanup_err);
    }

  return err;
}

/* Return TRUE if one of the first PROCESSED items in COMMIT_ITEMS is a
   parent of PATH, return FALSE otherwise. */
static svn_boolean_t
have_processed_parent (apr_array_header_t *commit_items,
                       int processed,
                       const char *path,
                       apr_pool_t *pool)
{
  int i;
  for (i = 0; i < processed && i < commit_items->nelts; ++i)
    {
      svn_client_commit_item_t *item
        = ((svn_client_commit_item_t **) commit_items->elts)[i];

      if (svn_path_is_child (item->path, path, pool))
        return TRUE;
    }
  return FALSE;
}

/* qsort-ready comparison function. */
static int compare_paths (const void *a, const void *b)
{
  const char *item1 = *((const char * const *) a);
  const char *item2 = *((const char * const *) b);

  return svn_path_compare_paths (item1, item2);
}

/* Remove redundancies by removing duplicates from NONRECURSIVE_TARGETS,
 * and removing any target that either is, a decendant of, a path in
 * RECURSIVE_TARGETS.  Return the result in *PUNIQUE_TARGETS.
 */
static svn_error_t *
remove_redundancies (apr_array_header_t **punique_targets,
                     const apr_array_header_t *nonrecursive_targets,
                     const apr_array_header_t *recursive_targets,
                     apr_pool_t *pool)
{
  apr_pool_t *temp_pool;
  apr_array_header_t *abs_recursive_targets = NULL;
  apr_array_header_t *abs_targets;
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
  temp_pool = svn_pool_create (pool);
 
  /* Create our list of absolute paths for our "keepers" */
  abs_targets = apr_array_make (temp_pool, nonrecursive_targets->nelts,
                                sizeof (const char *));

  /* Create our list of absolute paths for our recursive targets */
  if (recursive_targets)
    {
      abs_recursive_targets = apr_array_make (temp_pool, 
                                              recursive_targets->nelts,
                                              sizeof (const char *));

      for (i = 0; i < recursive_targets->nelts; i++)
        {
          const char *rel_path = 
            APR_ARRAY_IDX (recursive_targets, i, const char *);
          const char *abs_path;

          /* Get the absolute path for this target. */
          SVN_ERR (svn_path_get_absolute (&abs_path, rel_path, temp_pool));

          APR_ARRAY_PUSH (abs_recursive_targets, const char *) = abs_path;
        }
    }
 
  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make (pool, nonrecursive_targets->nelts,
                                sizeof (const char *));
 
  /* For each target in our list we do the following:
 
     1. Calculate its absolute path (ABS_PATH).
     2. See if any of the keepers in RECURSIVE_TARGETS is a parent of, or
        is the same path as, ABS_PATH.  If so, we ignore this
        target.  If not, however, add this target's original path to
        REL_TARGETS. */
  for (i = 0; i < nonrecursive_targets->nelts; i++)
    {
      const char *rel_path = APR_ARRAY_IDX (nonrecursive_targets, i,
                                            const char *);
      const char *abs_path;
      int j;
      svn_boolean_t keep_me;
 
      /* Get the absolute path for this target. */
      SVN_ERR (svn_path_get_absolute (&abs_path, rel_path, temp_pool));
 
      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;

      if (abs_recursive_targets)
        {
          for (j = 0; j < abs_recursive_targets->nelts; j++)
            {
              const char *keeper = APR_ARRAY_IDX (abs_recursive_targets, j,
                                                  const char *);
 
              /* Quit here if we find this path already in the keepers. */
              if (strcmp (keeper, abs_path) == 0)
                {
                  keep_me = FALSE;
                  break;
                }

              /* Quit here if this path is a child of one of the keepers. */
              if (svn_path_is_child (keeper, abs_path, temp_pool))
                {
                  keep_me = FALSE;
                  break;
                }
            }
        }

      if (keep_me)
        {
          for (j = 0; j < abs_targets->nelts; j++)
            {
              const char *keeper = 
                APR_ARRAY_IDX (abs_targets, j, const char *);
 
              /* Quit here if we find this path already in the keepers. */
              if (strcmp (keeper, abs_path) == 0)
                {
                  keep_me = FALSE;
                  break;
                }
            }
        }
 
      /* If this is a new keeper, add its absolute path to ABS_TARGETS
         and its original path to REL_TARGETS. */
      if (keep_me)
        {
          APR_ARRAY_PUSH (rel_targets, const char *) = rel_path;
          APR_ARRAY_PUSH (abs_targets, const char *) = abs_path;
        }
    }
 
  /* Destroy our temporary pool. */
  svn_pool_destroy (temp_pool);
 
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
adjust_rel_targets (const char **pbase_dir,
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
      target = APR_ARRAY_IDX (rel_targets, i, const char *);

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
              
      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, &name, pool));

      if (*name)
        {
          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          base_dir = apr_pstrdup (pool, parent_dir);

          new_rel_targets = apr_array_make (pool, rel_targets->nelts, 
                                            sizeof (name));
          for (i = 0; i < rel_targets->nelts; i++)
            {
              target = APR_ARRAY_IDX (rel_targets, i, const char *);
              target = svn_path_join (name, target, pool);
              APR_ARRAY_PUSH (new_rel_targets, const char *) = target;
            }
         }
    }

  *pbase_dir = base_dir;
  *prel_targets = new_rel_targets;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_commit (svn_client_commit_info_t **commit_info,
                   const apr_array_header_t *targets,
                   svn_boolean_t nonrecursive,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *ra_baton, *session;
  const char *log_msg;
  svn_ra_plugin_t *ra_lib;
  const char *base_dir;
  const char *base_url;
  const char *target;
  apr_array_header_t *rel_targets;
  apr_array_header_t *dirs_to_lock;
  apr_array_header_t *dirs_to_lock_recursive;
  svn_boolean_t lock_base_dir_recursive = FALSE;
  apr_hash_t *committables, *tempfiles = NULL;
  svn_wc_adm_access_t *base_dir_access;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR, *unlock_err = SVN_NO_ERROR;
  svn_error_t *bump_err = SVN_NO_ERROR, *cleanup_err = SVN_NO_ERROR;
  svn_boolean_t commit_in_progress = FALSE;
  const char *display_dir = "";
  int i;

  /* Committing URLs doesn't make sense, so error if it's tried. */
  for (i = 0; i < targets->nelts; i++)
    {
      target = APR_ARRAY_IDX (targets, i, const char *);
      if (svn_path_is_url (target))
        return svn_error_createf
          (SVN_ERR_ILLEGAL_TARGET, NULL,
           _("'%s' is a URL, but URLs cannot be commit targets"), target);
    }

  /* Condense the target list. */
  SVN_ERR (svn_path_condense_targets (&base_dir, &rel_targets, targets,
                                      nonrecursive ? FALSE : TRUE,
                                      pool));

  /* No targets means nothing to commit, so just return. */
  if (! base_dir)
    return SVN_NO_ERROR;

  /* Prepare an array to accumulate dirs to lock */
  dirs_to_lock = apr_array_make (pool, 1, sizeof (target));
  dirs_to_lock_recursive = apr_array_make (pool, 1, sizeof (target));

  /* If we calculated only a base_dir and no relative targets, this
     must mean that we are being asked to commit (effectively) a
     single path. */
  if ((! rel_targets) || (! rel_targets->nelts))
    {
      const char *parent_dir, *name;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, &name, pool));
      if (*name)
        {
          svn_node_kind_t kind;

          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          base_dir = apr_pstrdup (pool, parent_dir);

          /* Make the array if it wasn't already created. */
          if (! rel_targets)
            rel_targets = apr_array_make (pool, targets->nelts, sizeof (name));

          /* Now, push this name as a relative path to our new
             base directory. */
          APR_ARRAY_PUSH (rel_targets, const char *) = name;

          target = svn_path_join (base_dir, name, pool);
          SVN_ERR (svn_io_check_path (target, &kind, pool));

          /* If the final target is a dir, we want to recursively lock it */
          if (kind == svn_node_dir)
            {
              if (nonrecursive)
                APR_ARRAY_PUSH (dirs_to_lock, const char *) = target;
              else
                APR_ARRAY_PUSH (dirs_to_lock_recursive, const char *) = target;
            }
        }
      else
        {
          /* This will recursively lock the base_dir further down */
          lock_base_dir_recursive = TRUE;
        }
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      SVN_ERR (adjust_rel_targets (&base_dir, &rel_targets,
                                   base_dir, rel_targets,
                                   pool));

      for (i = 0; i < rel_targets->nelts; i++)
        {
          const char *parent_dir, *name;

          svn_pool_clear (subpool);
          target = svn_path_join (base_dir,
                                  APR_ARRAY_IDX (rel_targets, i, const char *),
                                  subpool);
          SVN_ERR (svn_wc_get_actual_target (target, &parent_dir, 
                                             &name, subpool));

          if (*name)
            {
              svn_node_kind_t kind;

              target = svn_path_join (parent_dir, name, subpool);
          
              SVN_ERR (svn_io_check_path (target, &kind, subpool));

              /* If the final target is a dir, we want to recursively
                 lock it */
              if (kind == svn_node_dir)
                {
                  if (nonrecursive)
                    APR_ARRAY_PUSH (dirs_to_lock, 
                                    const char *) = apr_pstrdup (pool, target);
                  else
                    APR_ARRAY_PUSH (dirs_to_lock_recursive, 
                                    const char *) = apr_pstrdup (pool, target);
                }
            }

          target = parent_dir;
          while (strcmp (target, base_dir))
            {
              if (target[0] == '/' && target[1] == '\0')
                abort();

              APR_ARRAY_PUSH (dirs_to_lock,
                              const char *) = apr_pstrdup (pool, target);
              target = svn_path_dirname(target, subpool);
            }
        }
      svn_pool_destroy (subpool);
    }

  SVN_ERR (svn_wc_adm_open2 (&base_dir_access, NULL, base_dir,
                             TRUE,  /* Write lock */
                             lock_base_dir_recursive ? -1 : 0, /* Depth */
                             pool));

  if (!lock_base_dir_recursive)
    {
      svn_wc_adm_access_t *adm_access;
      apr_array_header_t *unique_dirs_to_lock;

      /* Sort the paths in a depth-last directory-ish order. */
      qsort (dirs_to_lock->elts, dirs_to_lock->nelts,
             dirs_to_lock->elt_size, compare_paths);
      qsort (dirs_to_lock_recursive->elts, dirs_to_lock_recursive->nelts,
             dirs_to_lock_recursive->elt_size, compare_paths);

      /* Remove any duplicates */
      SVN_ERR (svn_path_remove_redundancies (&unique_dirs_to_lock,
                                             dirs_to_lock_recursive,
                                             pool));
      dirs_to_lock_recursive = unique_dirs_to_lock;

      /* Remove dirs and descendants from dirs_to_lock if there is
         any ancestor in dirs_to_lock_recursive */
      SVN_ERR (remove_redundancies (&unique_dirs_to_lock,
                                    dirs_to_lock,
                                    dirs_to_lock_recursive,
                                    pool));
      dirs_to_lock = unique_dirs_to_lock;

      /* First lock all the dirs to be locked non-recursively */
      if (dirs_to_lock)
        {
          for (i = 0; i < dirs_to_lock->nelts ; ++i)
            {
              target = APR_ARRAY_IDX (dirs_to_lock, i, const char *);

              SVN_ERR (svn_wc_adm_open2 (&adm_access, base_dir_access,
                                         target,
                                         TRUE,  /* Write lock */
                                         0,     /* Depth */
                                         pool));
            }
        }

      /* Lock the rest of the targets (recursively) */
      if (dirs_to_lock_recursive)
        {
          for (i = 0; i < dirs_to_lock_recursive->nelts ; ++i)
            {
              target = APR_ARRAY_IDX (dirs_to_lock_recursive, i, const char *);

              SVN_ERR (svn_wc_adm_open2 (&adm_access, base_dir_access,
                                         target,
                                         TRUE, /* Write lock */
                                         -1,   /* Depth */
                                         pool));
            }
        }
    }

  /* One day we might support committing from multiple working copies, but
     we don't yet.  This check ensures that we don't silently commit a
     subset of the targets.

     At the same time, if a non-recursive commit is desired, do not
     allow a deleted directory as one of the targets. */
  for (i = 0; i < targets->nelts; ++i)
    {
      svn_wc_adm_access_t *adm_access;

      SVN_ERR (svn_path_get_absolute (&target,
                                      APR_ARRAY_IDX (targets, i, const char *),
                                      pool));
      SVN_ERR_W (svn_wc_adm_probe_retrieve (&adm_access, base_dir_access,
                                            target, pool),
                 _("Are all the targets part of the same working copy?"));

      if (nonrecursive)
        {
          svn_wc_status_t *status;
          svn_node_kind_t kind;
          
          SVN_ERR (svn_io_check_path (target, &kind, pool));

          if (kind == svn_node_dir)
            {
              SVN_ERR (svn_wc_status (&status, target, adm_access, pool));
              if (status->text_status == svn_wc_status_deleted ||
                  status->text_status == svn_wc_status_replaced)
                return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                         _("Cannot non-recursively commit a "
                                           "directory deletion"));
            }
        }
    }

  /* Crawl the working copy for commit items. */
  if ((cmt_err = svn_client__harvest_committables (&committables, 
                                                   base_dir_access,
                                                   rel_targets, 
                                                   nonrecursive,
                                                   ctx,
                                                   pool)))
    goto cleanup;

  /* ### todo: Currently there should be only one hash entry, which
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away
     and be replaced with a canonical repos URL, and from there we
     are poised to started handling nested working copies.  See
     http://subversion.tigris.org/issues/show_bug.cgi?id=960. */
  if (! ((commit_items = apr_hash_get (committables,
                                       SVN_CLIENT__SINGLE_REPOS_NAME, 
                                       APR_HASH_KEY_STRING))))
    goto cleanup;

  /* Go get a log message.  If an error occurs, or no log message is
     specified, abort the operation. */
  if (ctx->log_msg_func)
    {
      const char *tmp_file;
      cmt_err = (*ctx->log_msg_func)(&log_msg, &tmp_file, commit_items, 
                                     ctx->log_msg_baton, pool);
      if (cmt_err || (! log_msg))
        goto cleanup;
    }
  else
    log_msg = "";

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items (&base_url,
                                                    commit_items,
                                                    pool)))
    goto cleanup;

  if ((cmt_err = get_ra_editor (&ra_baton, &session, &ra_lib, NULL,
                                &editor, &edit_baton, ctx,
                                base_url, base_dir, base_dir_access,
                                log_msg, commit_items, commit_info,
                                TRUE, pool)))
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  /* Determine prefix to strip from the commit notify messages */
  if ((cmt_err = svn_path_get_absolute (&display_dir,
                                        display_dir, pool)))
    goto cleanup;
  display_dir = svn_path_get_longest_ancestor (display_dir, base_dir, pool);

  /* Perform the commit. */
  cmt_err = svn_client__do_commit (base_url, commit_items, base_dir_access,
                                   editor, edit_baton, 
                                   display_dir,
                                   &tempfiles, ctx, pool);

  /* Handle a successful commit. */
  if ((! cmt_err)
      || (cmt_err->apr_err == SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      /* Make a note that our commit is finished. */
      commit_in_progress = FALSE;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          svn_boolean_t recurse = FALSE;
          const char *adm_access_path;
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *entry;

          /* Clear the subpool here because there are some 'continue'
             statements in this loop. */
          svn_pool_clear (subpool);

          if (item->kind == svn_node_dir)
            adm_access_path = item->path;
          else
            svn_path_split (item->path, &adm_access_path, NULL, subpool);

          bump_err = svn_wc_adm_retrieve (&adm_access, base_dir_access,
                                          adm_access_path, subpool);
          if (bump_err)
            {
              if (bump_err->apr_err == SVN_ERR_WC_NOT_LOCKED)
                {
                  if (have_processed_parent (commit_items, i,
                                             item->path, subpool))
                    {
                      /* This happens when the item is a directory that is
                         deleted, and it has been processed as a child of an
                         earlier item. */
                      svn_error_clear (bump_err);
                      bump_err = SVN_NO_ERROR;
                      continue;
                    }

                  /* Is it a directory that was deleted in the commit? */
                  if (item->kind == svn_node_dir
                      && item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
                    {
                      /* It better be missing then.  Assuming it is,
                         mark as deleted in parent.  If not, then
                         something is way bogus. */
                      SVN_ERR (svn_wc_mark_missing_deleted (item->path,
                                                            base_dir_access,
                                                            subpool));
                      svn_error_clear (bump_err);
                      bump_err = SVN_NO_ERROR;
                      continue;                      
                    }                  
                }
              goto cleanup;              
            }
          if ((bump_err = svn_wc_entry (&entry, item->path, adm_access, TRUE,
                                        subpool)))
            goto cleanup;

          if (! entry
              && have_processed_parent (commit_items, i, item->path, subpool))
            /* This happens when the item is a file that is deleted, and it
               has been processed as a child of an earlier item. */
            continue;

          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD) 
              && (item->kind == svn_node_dir)
              && (item->copyfrom_url))
            recurse = TRUE;

          assert (*commit_info);
          if ((bump_err = svn_wc_process_committed (item->path, adm_access,
                                                    recurse,
                                                    (*commit_info)->revision,
                                                    (*commit_info)->date,
                                                    (*commit_info)->author,
                                                    item->wcprop_changes,
                                                    subpool)))
            break;
        }

      /* Destroy the subpool. */
      svn_pool_destroy (subpool);
    }

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps ();

 cleanup:
  /* Abort the commit if it is still in progress. */
  if (commit_in_progress)
    svn_error_clear (editor->abort_edit (edit_baton, pool));

  /* A bump error is likely to occur while running a working copy log file,
     explicitly unlocking and removing temporary files would be wrong in
     that case.  A commit error (cmt_err) should only occur before any
     attempt to modify the working copy, so it doesn't prevent explict
     clean-up. */
  if (! bump_err)
    {
      unlock_err = svn_wc_adm_close (base_dir_access);

      if (! unlock_err)
        cleanup_err = remove_tmpfiles (tempfiles, pool);
    }

  return reconcile_errors (cmt_err, unlock_err, bump_err, cleanup_err, pool);
}
