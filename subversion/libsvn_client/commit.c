/*
 * commit.c:  wrappers around wc commit functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"

#include "client.h"



/* Hash value for FILES hash in the import routines. */
struct imported_file
{
  apr_pool_t *subpool;
  void *file_baton;
};


/* Apply PATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.  */
static svn_error_t *
send_file_contents (svn_stringbuf_t *path,
                    void *file_baton,
                    const svn_delta_editor_t *editor,
                    apr_pool_t *pool)
{
  svn_stream_t *contents;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  apr_file_t *f = NULL;
  apr_status_t apr_err;

  /* Get an apr file for PATH. */
  apr_err = apr_file_open (&f, path->data, APR_READ, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, NULL, pool, 
                              "error opening `%s' for reading", path->data);
  
  /* Get a readable stream of the file's contents. */
  contents = svn_stream_from_aprfile (f, pool);

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Send the file's contents to the delta-window handler. */
  SVN_ERR (svn_txdelta_send_stream (contents, handler, handler_baton, pool));

  /* Close the file. */
  apr_err = apr_file_close (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool, "error closing `%s'", path->data);

  return SVN_NO_ERROR;
}


/* Import file PATH as EDIT_PATH in the repository directory indicated
 * by DIR_BATON in EDITOR.  
 *
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_file (apr_hash_t *files,
             const svn_delta_editor_t *editor,
             void *dir_baton,
             const svn_stringbuf_t *path,
             const char *edit_path,
             apr_pool_t *pool)
{
  void *file_baton;
  const char *mimetype;
  apr_pool_t *hash_pool = apr_hash_pool_get (files);
  apr_pool_t *subpool = svn_pool_create (hash_pool);
  svn_stringbuf_t *filepath = svn_stringbuf_dup (path, hash_pool);
  struct imported_file *value = apr_palloc (hash_pool, sizeof (*value));

  /* Add the file, using the pool from the FILES hash. */
  SVN_ERR (editor->add_file (edit_path, dir_baton, NULL, SVN_INVALID_REVNUM, 
                             subpool, &file_baton));

  /* If the file has a discernable mimetype, add that as a property to
     the file. */
  SVN_ERR (svn_io_detect_mimetype (&mimetype, path->data, pool));
  if (mimetype)
    SVN_ERR (editor->change_file_prop (file_baton, SVN_PROP_MIME_TYPE,
                                       svn_string_create (mimetype, pool), 
                                       pool));
  
  /* Finally, add the file's path and baton to the FILES hash. */
  value->subpool = subpool;
  value->file_baton = file_baton;
  apr_hash_set (files, filepath->data, filepath->len, (void *)value);

  return SVN_NO_ERROR;
}
             

/* Import directory PATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  ROOT_PATH is the path imported as the root
 * directory, so all edits are relative to that.
 *
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_dir (apr_hash_t *files,
            const svn_delta_editor_t *editor, 
            void *dir_baton,
            const svn_stringbuf_t *path,
            const svn_stringbuf_t *edit_path,
            svn_boolean_t nonrecursive,
            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);  /* iteration pool */
  apr_dir_t *dir;
  apr_finfo_t finfo;
  apr_status_t apr_err;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;
  svn_stringbuf_t *this_path, *this_edit_path;

  if ((apr_err = apr_dir_open (&dir, path->data, pool)))
    return svn_error_createf (apr_err, 0, NULL, pool, 
                              "unable to open directory %s", path->data);

  this_path = svn_stringbuf_dup (path, pool);
  this_edit_path = svn_stringbuf_dup (edit_path, pool);

  for (apr_err = apr_dir_read (&finfo, flags, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       svn_pool_clear (subpool), apr_err = apr_dir_read (&finfo, flags, dir))
    {
      svn_stringbuf_t *name;

      if (finfo.filetype == APR_DIR)
        {
          /* Skip entries for this dir and its parent.  
             ### kff todo: APR actually promises that they'll come first,
             so this guard could be moved outside the loop. */
          if (! (strcmp (finfo.name, ".") && strcmp (finfo.name, "..")))
            continue;

          /* If someone's trying to import a directory named the same
             as our administrative directories, that's probably not
             what they wanted to do.  Someday we can take an option to
             make these subdirs be silently ignored, but for now,
             seems safest to error. */
          if (strcmp (finfo.name, SVN_WC_ADM_DIR_NAME) == 0)
            return svn_error_createf
              (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, subpool,
               "cannot import directory named \"%s\" (in `%s')",
               finfo.name, path->data);
        }

      /* Make a stringbuf version of the entry name, and append it as
         a path component to THIS_PATH and THIS_EDIT_PATH. */
      name = svn_stringbuf_create (finfo.name, subpool);
      svn_path_add_component (this_path, name);
      svn_path_add_component (this_edit_path, name);

      /* We only import subdirectories when we're doing a regular
         recursive import. */
      if ((finfo.filetype == APR_DIR) && (! nonrecursive))
        {
          void *this_dir_baton;

          /* Add the new subdirectory, getting a descent baton from
             the editor. */
          SVN_ERR (editor->add_directory (this_edit_path->data, dir_baton, 
                                          NULL, SVN_INVALID_REVNUM, subpool,
                                          &this_dir_baton));

          /* Recurse. */
          SVN_ERR (import_dir (files, editor, this_dir_baton, 
                               this_path, this_edit_path, 
                               FALSE, subpool));

          /* Finally, close the sub-directory. */
          SVN_ERR (editor->close_directory (this_dir_baton));
        }
      else if (finfo.filetype == APR_REG)
        {
          /* Import a file. */
          SVN_ERR (import_file (files, editor, dir_baton, 
                                this_path, this_edit_path->data, subpool));
        }
      /* ### We're silently ignoring things that aren't files or
         directories.  If we stop doing that, here is the place to
         change your world.  */
      
      /* Hack THIS_PATH and THIS_EDIT_PATH back to their original sizes. */
      svn_stringbuf_chop (this_path, 
                          (path->len ? name->len + 1 : name->len));
      svn_stringbuf_chop (this_edit_path, 
                          (edit_path->len ? name->len + 1 : name->len));
    }

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (apr_err)))
    return svn_error_createf
      (apr_err, 0, NULL, subpool, "error during import of `%s'", path->data);

  /* Yes, it exited cleanly, so close the dir. */
  else if ((apr_err = apr_dir_close (dir)))
    return svn_error_createf
      (apr_err, 0, NULL, subpool, "error closing dir `%s'", path->data);
      
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

/* Recursively import PATH to a repository using EDITOR and
 * EDIT_BATON.  PATH can be a file or directory.
 * 
 * NEW_ENTRY is the name to use in the repository.  If PATH is a
 * directory, NEW_ENTRY may be null, which creates as many new entries
 * in the top repository target directory as there are entries in the
 * top of PATH; but if NEW_ENTRY is non-null, it is the name of a new
 * subdirectory in the repository to hold the import.  If PATH is a
 * file, NEW_ENTRY may not be null.
 * 
 * NEW_ENTRY can never be the empty string.
 * 
 * Use POOL for any temporary allocation.
 *
 * Note: the repository directory receiving the import was specified
 * when the editor was fetched.  (I.e, when EDITOR->open_root() is
 * called, it returns a directory baton for that directory, which is
 * not necessarily the root.)
 */
static svn_error_t *
import (const svn_stringbuf_t *path,
        const svn_stringbuf_t *new_entry,
        const svn_delta_editor_t *editor,
        void *edit_baton,
        svn_boolean_t nonrecursive,
        apr_pool_t *pool)
{
  void *root_baton;
  enum svn_node_kind kind;
  apr_hash_t *files = apr_hash_make (pool);
  apr_hash_index_t *hi;

  /* Get a root dir baton.  We pass an invalid revnum to open_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, 
                              pool, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));

  /* Note that there is no need to check whether PATH's basename is
     the same name that we reserve for our admistritave
     subdirectories.  It would be strange, but not illegal to import
     the contents of a directory of that name, because the directory's
     own name is not part of those contents.  Of course, if something
     underneath it also has our reserved name, then we'll error. */

  if (kind == svn_node_file)
    {
      if (! new_entry)
        return svn_error_create
          (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
           "new entry name required when importing a file");

      SVN_ERR (import_file (files, editor, root_baton, 
                            path, new_entry->data, pool));
    }
  else if (kind == svn_node_dir)
    {
      void *new_dir_baton = NULL;

      /* Grab a new baton, making two we'll have to close. */
      if (new_entry)
        SVN_ERR (editor->add_directory (new_entry->data, root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        pool, &new_dir_baton));
      
      SVN_ERR (import_dir 
               (files, editor, new_dir_baton ? new_dir_baton : root_baton, 
                path, new_entry ? new_entry : svn_stringbuf_create ("", pool), 
                nonrecursive, pool));

      /* Close one baton or two. */
      if (new_dir_baton)
        SVN_ERR (editor->close_directory (new_dir_baton));
    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
         "'%s' does not exist.", path->data);  
    }

  SVN_ERR (editor->close_directory (root_baton));

  /* Do post-fix textdeltas here! */
  for (hi = apr_hash_first (pool, files); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      struct imported_file *value;
      svn_stringbuf_t *full_path;
      
      apr_hash_this (hi, &key, &keylen, &val);
      value = val;
      full_path = svn_stringbuf_create (key, value->subpool);
      SVN_ERR (send_file_contents (full_path, value->file_baton, 
                                   editor, value->subpool));
      SVN_ERR (editor->close_file (value->file_baton));
      svn_pool_destroy (value->subpool);
    }

  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_xml_editor (apr_file_t **xml_hnd,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                const char *xml_file,
                apr_pool_t *pool)
{
  apr_status_t apr_err;

  /* Open the xml file for writing. */
  if ((apr_err = apr_file_open (xml_hnd, xml_file, (APR_WRITE | APR_CREATE),
                                APR_OS_DEFAULT, pool)))
    return svn_error_createf (apr_err, 0, NULL, pool, 
                              "error opening %s", xml_file);
  
  /* ... we need an XML commit editor. */
  return svn_delta_get_xml_editor (svn_stream_from_aprfile (*xml_hnd, pool), 
                                   editor, edit_baton, pool);
}


static svn_error_t *
get_ra_editor (void **ra_baton, 
               void **session,
               svn_ra_plugin_t **ra_lib,
               const svn_delta_editor_t **editor,
               void **edit_baton,
               svn_client_auth_baton_t *auth_baton,
               svn_stringbuf_t *base_url,
               svn_stringbuf_t *base_dir,
               svn_stringbuf_t *log_msg,
               apr_array_header_t *commit_items,
               svn_revnum_t *committed_rev,
               const char **committed_date,
               const char **committed_author,
               svn_boolean_t is_commit,
               apr_pool_t *pool)
{
  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (ra_lib, *ra_baton, 
                                  base_url->data, pool));
  
  /* Open an RA session to URL. */
  SVN_ERR (svn_client__open_ra_session (session, *ra_lib,
                                        base_url, base_dir,
                                        commit_items, is_commit,
                                        is_commit, !is_commit,
                                        auth_baton, pool));
  
  /* Fetch RA commit editor, giving it svn_wc_process_committed(). */
  return (*ra_lib)->get_commit_editor (*session, editor, edit_baton, 
                                       committed_rev, committed_date, 
                                       committed_author, log_msg);
}


/*** Public Interfaces. ***/

svn_error_t *
svn_client_import (svn_client_commit_info_t **commit_info,
                   const svn_delta_editor_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_editor_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *url,
                   svn_stringbuf_t *new_entry,
                   svn_client_get_commit_log_t log_msg_func,
                   void *log_msg_baton,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   svn_boolean_t nonrecursive,
                   apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;
  svn_stringbuf_t *log_msg;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  apr_file_t *xml_hnd;
  svn_boolean_t use_xml = (xml_dst && xml_dst->data) ? TRUE : FALSE;

  /* Sanity check: NEW_ENTRY can be null or non-empty, but it can't be
     empty. */
  if (new_entry && (strcmp (new_entry->data, "") == 0))
    return svn_error_create (SVN_ERR_FS_PATH_SYNTAX, 0, NULL, pool,
                             "empty string is an invalid entry name");

  /* The repository doesn't know about the reserved. */
  if (new_entry && strcmp (new_entry->data, SVN_WC_ADM_DIR_NAME) == 0)
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, pool,
       "the name \"%s\" is reserved and cannot be imported",
       SVN_WC_ADM_DIR_NAME);

  /* Create a new commit item and add it to the array. */
  if (log_msg_func)
    {
      svn_client_commit_item_t *item;
      apr_array_header_t *commit_items 
        = apr_array_make (pool, 1, sizeof (item));
      
      item = apr_pcalloc (pool, sizeof (*item));
      item->path = svn_stringbuf_dup (path, pool);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      (*((svn_client_commit_item_t **) apr_array_push (commit_items))) 
        = item;
      
      SVN_ERR ((*log_msg_func) (&log_msg, commit_items, log_msg_baton, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = svn_stringbuf_create ("", pool);

  /* If we're importing to XML ... */
  if (use_xml)
    SVN_ERR (get_xml_editor (&xml_hnd, &editor, &edit_baton, 
                             xml_dst->data, pool));

  /* Else we're importing to an RA layer. */
  else  
    SVN_ERR (get_ra_editor (&ra_baton, &session, &ra_lib, 
                            &editor, &edit_baton, auth_baton, url, path,
                            log_msg, NULL, &committed_rev, &committed_date,
                            &committed_author, FALSE, pool));

  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);

  /* If an error occured during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if ((err = import (path, new_entry, editor, edit_baton, nonrecursive, pool)))
    {
      editor->abort_edit (edit_baton);
      return err;
    }

  /* Finish the import. */
  if (use_xml)
    {
      /* If we were committing into XML, close the xml file. */      
      if ((apr_err = apr_file_close (xml_hnd)))
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error closing %s", xml_dst->data);
      
      /* Use REVISION for COMMITTED_REV. */
      committed_rev = revision;
    }
  else  
    {
      /* We were committing to RA, so close the session. */
      SVN_ERR (ra_lib->close (session));
    }

  /* Finally, fill in the commit_info structure. */
  *commit_info = svn_client__make_commit_info (committed_rev,
                                               committed_author,
                                               committed_date,
                                               pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
unlock_dirs (apr_hash_t *locked_dirs,
             apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! locked_dirs)
    return SVN_NO_ERROR;

  /* Clean up any locks. */
  for (hi = apr_hash_first (pool, locked_dirs); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_stringbuf_t *strkey;

      apr_hash_this (hi, &key, &keylen, &val);
      strkey = svn_stringbuf_ncreate ((const char *)key, keylen, pool);
      SVN_ERR (svn_wc_unlock (strkey, pool));
    }

  return SVN_NO_ERROR;
}  


static svn_error_t *
remove_tmpfiles (apr_hash_t *tempfiles,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first (pool, tempfiles); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_node_kind_t kind;

      apr_hash_this (hi, &key, &keylen, &val);
      SVN_ERR (svn_io_check_path ((const char *)key, &kind, pool));
      if (kind == svn_node_file)
        SVN_ERR (svn_io_remove_file ((const char *)key, pool));
    }

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
        (commit_err, "Commit failed (details follow):");
      err = commit_err;
    }

  /* Else, create a new "general" error that will head off the errors
     that follow. */
  else
    err = svn_error_create (SVN_ERR_GENERAL, 0, NULL, pool,
                            "Commit succeeded, but other errors follow:");

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap 
        (unlock_err, "Error unlocking locked dirs (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, unlock_err);
    }

  /* If there was a bumping error... */
  if (bump_err)
    {
      /* Wrap the error with some headers. */
      bump_err = svn_error_quick_wrap 
        (bump_err, "Error bumping revisions post-commit (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, bump_err);
    }

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap 
        (cleanup_err, "Error in post-commit clean-up (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, cleanup_err);
    }

  return err;
}


svn_error_t *
svn_client_commit (svn_client_commit_info_t **commit_info,
                   const svn_delta_editor_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_editor_t *after_editor,
                   void *after_edit_baton,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   svn_client_auth_baton_t *auth_baton,
                   const apr_array_header_t *targets,
                   svn_client_get_commit_log_t log_msg_func,
                   void *log_msg_baton,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   svn_boolean_t nonrecursive,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *ra_baton, *session;
  svn_stringbuf_t *log_msg;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  svn_ra_plugin_t *ra_lib;
  svn_stringbuf_t *base_dir, *base_url;
  apr_array_header_t *rel_targets;
  apr_hash_t *committables, *locked_dirs, *tempfiles = NULL;
  apr_array_header_t *commit_items;
  apr_status_t apr_err = 0;
  apr_file_t *xml_hnd = NULL;
  svn_error_t *cmt_err = NULL, *unlock_err = NULL;
  svn_error_t *bump_err = NULL, *cleanup_err = NULL;
  svn_boolean_t use_xml = (xml_dst && xml_dst->data) ? TRUE : FALSE;
  svn_boolean_t commit_in_progress = FALSE;
  svn_stringbuf_t *display_dir = svn_stringbuf_create (".", pool);
  int notify_path_offset;
  int i;

  /* Condense the target list. */
  SVN_ERR (svn_path_condense_targets (&base_dir, &rel_targets, targets, pool));

  /* If we calculated only a base_dir and no relative targets, this
     must mean that we are being asked to commit a single directory.
     In order to do this properly, we need to anchor our commit up one
     directory level, so long as our anchor is still a versioned
     directory. */
  if ((! rel_targets) || (! rel_targets->nelts))
    {
      svn_stringbuf_t *parent_dir, *name;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, &name, pool));
      if (name)
        {
          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          svn_stringbuf_set (base_dir, parent_dir->data);

          /* Make the array if it wasn't already created. */
          if (! rel_targets)
            rel_targets = apr_array_make (pool, targets->nelts, sizeof (name));

          /* Now, push this name as a relative path to our new
             base directory. */
          (*((svn_stringbuf_t **)apr_array_push (rel_targets))) = name;
        }
    }

  /* Crawl the working copy for commit items. */
  if ((cmt_err = svn_client__harvest_committables (&committables, 
                                                   &locked_dirs,
                                                   base_dir,
                                                   rel_targets, 
                                                   nonrecursive,
                                                   pool)))
    goto cleanup;

  /* ### todo: Currently there should be only one hash entry, which
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away
     and be replaced with a canonical repos URL, and from there we
     are poised to started handling nested working copies. */
  if (! ((commit_items = apr_hash_get (committables, 
                                       SVN_CLIENT__SINGLE_REPOS_NAME, 
                                       APR_HASH_KEY_STRING))))
    goto cleanup;

  /* Go get a log message.  If an error occurs, or no log message is
     specified, abort the operation. */
  if (log_msg_func)
    {
      cmt_err = (*log_msg_func)(&log_msg, commit_items, log_msg_baton, pool);
      if (cmt_err || (! log_msg))
        goto cleanup;
    }
  else
    log_msg = svn_stringbuf_create ("", pool);

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items (&base_url, 
                                                    commit_items, 
                                                    pool)))
    goto cleanup;

  /* If we're committing to XML ... */
  if (use_xml)
    {
      if ((cmt_err = get_xml_editor (&xml_hnd, &editor, &edit_baton, 
                                     xml_dst->data, pool)))
        goto cleanup;

      /* Make a note that we have a commit-in-progress. */
      commit_in_progress = TRUE;
    }

  /* Else we're commit to RA */
  else
    {
      svn_revnum_t head = SVN_INVALID_REVNUM;

      if ((cmt_err = get_ra_editor (&ra_baton, &session, &ra_lib, 
                                    &editor, &edit_baton, auth_baton,
                                    base_url, base_dir, log_msg,
                                    commit_items, &committed_rev, 
                                    &committed_date, &committed_author, 
                                    TRUE, pool)))
        goto cleanup;

      /* Make a note that we have a commit-in-progress. */
      commit_in_progress = TRUE;

      /* ### Temporary: If we have any non-added directories with
         property mods, and we're not committing to an XML file, make
         sure those directories are up-to-date.  Someday this should
         just be protected against by the server.  */
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          if ((item->kind == svn_node_dir)
              && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
              && (! (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)))
            {
              if (! SVN_IS_VALID_REVNUM (head))
                {
                  if ((cmt_err = ra_lib->get_latest_revnum (session, &head)))
                    goto cleanup;
                }

              if (item->revision != head)
                {             
                  cmt_err = svn_error_createf 
                    (SVN_ERR_WC_NOT_UP_TO_DATE, 0, NULL, pool,
                     "Cannot commit propchanges for directory '%s'",
                     item->path->data);
                  goto cleanup;
                }
            }
        }
    }

  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);


  /* Determine prefix to strip from the commit notify messages */
  if ((cmt_err = svn_path_get_absolute (&display_dir, display_dir, pool)))
    goto cleanup;
  display_dir = svn_path_get_longest_ancestor (display_dir, base_dir, pool);
  notify_path_offset = display_dir->len ? display_dir->len + 1 : 0;

  /* Perform the commit. */
  cmt_err = svn_client__do_commit (base_url, commit_items, editor, edit_baton, 
                                   notify_func, notify_baton,
                                   notify_path_offset,
                                   &tempfiles, pool);

  /* Make a note that our commit is finished. */
  commit_in_progress = FALSE;

  /* Unlock the locked directories. */
  if (! ((unlock_err = unlock_dirs (locked_dirs, pool))))
    locked_dirs = NULL;
  
  /* Bump the revision if the commit went well. */
  if (! cmt_err)
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      if (use_xml)
        committed_rev = revision;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          svn_boolean_t recurse = FALSE;
          
          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD) 
              && (item->kind == svn_node_dir)
              && (item->copyfrom_url))
            recurse = TRUE;

          if ((bump_err = svn_wc_process_committed (item->path, recurse,
                                                    committed_rev, 
                                                    committed_date,
                                                    committed_author, 
                                                    subpool)))
            break;

          /* Clear the per-iteration subpool. */
          svn_pool_clear (subpool);
        }

      /* Destroy the subpool (unless an error occurred, since we'll
         need to keep the error around for a little while longer). */
      if (! bump_err)
        svn_pool_destroy (subpool);
    }

  /* If we were committing into XML, close the xml file. */      
  if (use_xml)
    {
      if ((apr_err = apr_file_close (xml_hnd)))
        {
          cleanup_err = svn_error_createf (apr_err, 0, NULL, pool,
                                           "error closing %s", xml_dst->data);
          goto cleanup;
        }

      /* Use REVISION for COMMITTED_REV. */
      committed_rev = revision;
    }
  else  
    {
      /* We were committing to RA, so close the session. */
      if ((cleanup_err = ra_lib->close (session)))
        goto cleanup;
    }

  /* Sleep for one second to ensure timestamp integrity. */
  apr_sleep (APR_USEC_PER_SEC * 1);

 cleanup:
  /* Abort the commit if it is still in progress. */
  if (commit_in_progress)
    editor->abort_edit (edit_baton); /* ignore return value */

  /* Unlock any remaining locked dirs. */
  if (locked_dirs)
    unlock_err = unlock_dirs (locked_dirs, pool);

  /* Remove any outstanding temporary text-base files. */
  cleanup_err = remove_tmpfiles (tempfiles, pool);

  /* Fill in the commit_info structure */
  *commit_info = svn_client__make_commit_info (committed_rev, 
                                               committed_author, 
                                               committed_date, pool);

  return reconcile_errors (cmt_err, unlock_err, bump_err, cleanup_err, pool);
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
