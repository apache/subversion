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


/* Shared internals of import and commit. */

/* Apply PATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.  */
static svn_error_t *
send_file_contents (svn_stringbuf_t *path,
                    void *file_baton,
                    const svn_delta_edit_fns_t *editor,
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


/* Import file PATH as NAME in the repository directory indicated by
 * DIR_BATON in EDITOR.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file (apr_hash_t *committed_targets,
             const svn_delta_edit_fns_t *editor,
             void *dir_baton,
             svn_stringbuf_t *path,
             svn_stringbuf_t *name,
             apr_pool_t *pool)
{
  void *file_baton;
  const char *mimetype;
  svn_stringbuf_t *full_path 
    = svn_stringbuf_dup (path, apr_hash_pool_get (committed_targets));

  SVN_ERR (editor->add_file (name, dir_baton, NULL, SVN_INVALID_REVNUM, 
                             &file_baton));          
  SVN_ERR (svn_io_detect_mimetype (&mimetype, full_path->data, pool));
  if (mimetype)
    SVN_ERR (editor->change_file_prop 
             (file_baton,
              svn_stringbuf_create (SVN_PROP_MIME_TYPE, pool),
              svn_stringbuf_create (mimetype, pool)));

  apr_hash_set (committed_targets, full_path->data, full_path->len,
                (void *)file_baton);

  return SVN_NO_ERROR;
}
             

/* Import directory PATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  Don't call EDITOR->close_directory(DIR_BATON),
 * that's left for the caller.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_dir (apr_hash_t *committed_targets,
            const svn_delta_edit_fns_t *editor, 
            void *dir_baton,
            svn_stringbuf_t *path,
            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);  /* iteration pool */
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  apr_status_t apr_err;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  apr_err = apr_dir_open (&dir, path->data, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool, 
                              "unable to open directory %s", path->data);

  for (apr_err = apr_dir_read (&this_entry, flags, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       svn_pool_clear (subpool), apr_err = apr_dir_read (&this_entry,
                                                         flags, dir))
    {
      svn_stringbuf_t *new_path = svn_stringbuf_dup (path, subpool);
      svn_stringbuf_t *name = svn_stringbuf_create (this_entry.name, subpool);

      svn_path_add_component (new_path, name);

      if (this_entry.filetype == APR_DIR)
        {
          void *this_dir_baton;

          /* Skip entries for this dir and its parent.  
             ### kff todo: APR actually promises that they'll come
             first, so this guard could be moved outside the loop. */
          if ((strcmp (this_entry.name, ".") == 0)
              || (strcmp (this_entry.name, "..") == 0))
            continue;

          /* If someone's trying to import a tree with SVN/ subdirs,
             that's probably not what they wanted to do.  Someday we
             can take an option to make the SVN/ subdirs be silently
             ignored, but for now, seems safest to error. */
          if (strcmp (this_entry.name, SVN_WC_ADM_DIR_NAME) == 0)
            return svn_error_createf
              (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, subpool,
               "cannot import directory named \"%s\" (in `%s')",
               this_entry.name, path->data);

          /* Get descent baton from the editor. */
          SVN_ERR (editor->add_directory (name,
                                          dir_baton,
                                          NULL,
                                          SVN_INVALID_REVNUM, 
                                          &this_dir_baton));

          /* Recurse. */
          SVN_ERR (import_dir (committed_targets,
                               editor, this_dir_baton, new_path, subpool));
          SVN_ERR (editor->close_directory (this_dir_baton));
        }
      else if (this_entry.filetype == APR_REG)
        {
          SVN_ERR (import_file (committed_targets,
                                editor, dir_baton, new_path, name, subpool));
        }
      else
        {
          /* It's not a file or dir, so we can't import it (yet).
             No need to error, just ignore the thing. */
        }
    }

  /* Check that the loop exited cleanly. */
  if (! (APR_STATUS_IS_ENOENT (apr_err)))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, "error during import of `%s'", path->data);
    }
  else  /* Yes, it exited cleanly, so close the dir. */
    {
      apr_err = apr_dir_close (dir);
      if (! (APR_STATUS_IS_SUCCESS (apr_err)))
        return svn_error_createf
          (apr_err, 0, NULL, subpool, "error closing dir `%s'", path->data);
    }
      
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
import (svn_stringbuf_t *path,
        svn_stringbuf_t *new_entry,
        const svn_delta_edit_fns_t *editor,
        void *edit_baton,
        apr_pool_t *pool)
{
  void *root_baton;
  enum svn_node_kind kind;
  apr_hash_t *committed_targets = apr_hash_make (pool);
  apr_pool_t *subpool = svn_pool_create (pool); /* for post-fix textdeltas */
  apr_hash_index_t *hi;

  /* Basic sanity check. */
  if (new_entry && (strcmp (new_entry->data, "") == 0))
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL, pool,
       "new entry name may not be the empty string when importing");

  /* The repository doesn't know about the reserved. */
  if (new_entry && strcmp (new_entry->data, SVN_WC_ADM_DIR_NAME) == 0)
    return svn_error_createf
      (SVN_ERR_CL_ADM_DIR_RESERVED, 0, NULL, pool,
       "the name \"%s\" is reserved and cannot be imported",
       SVN_WC_ADM_DIR_NAME);

  /* Get a root dir baton.  We pass an invalid revnum to open_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR (svn_io_check_path (path->data, &kind, pool));

  /* Note that there is no need to check whether PATH's basename is
     "SVN".  It would be strange but not illegal to import the
     contents of a directory named SVN/, because the directory's own
     name is not part of those contents.  Of course, if something
     underneath it is also named "SVN", then we'll error. */

  if (kind == svn_node_file)
    {
      if (! new_entry)
        return svn_error_create
          (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
           "new entry name required when importing a file");

      SVN_ERR (import_file (committed_targets,
                            editor, root_baton, path, new_entry, pool));
    }
  else if (kind == svn_node_dir)
    {
      void *new_dir_baton = NULL;

      /* Grab a new baton, making two we'll have to close. */
      if (new_entry)
        SVN_ERR (editor->add_directory (new_entry, root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        &new_dir_baton));

      SVN_ERR (import_dir (committed_targets, editor,
                           new_dir_baton ? new_dir_baton : root_baton,
                           path,
                           pool));

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
  for (hi = apr_hash_first (pool, committed_targets); 
       hi; 
       hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *file_baton;
      svn_stringbuf_t *full_path;

      apr_hash_this (hi, &key, &keylen, &file_baton);
      full_path = svn_stringbuf_create ((char *) key, subpool);
      SVN_ERR (send_file_contents (full_path, file_baton, editor, subpool));
      SVN_ERR (editor->close_file (file_baton));

      /* Clear our per-iteration subpool. */
      svn_pool_clear (subpool);
    }
  
  /* Destroy the per-iteration subpool. */
  svn_pool_destroy (subpool);

  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}


/* Fetch an EDITOR/EDIT_BATON that will allow us to commit to
   XML_STREAM.  If REVISION is a valid revision, the committed items
   will be "bumped" (using the CCB baton) to that revision after the
   commit. */
static svn_error_t *
get_xml_editor (const svn_delta_editor_t **editor,
                void **edit_baton,
                svn_stream_t *xml_stream,
                struct svn_wc_close_commit_baton *ccb,
                svn_revnum_t revision,
                apr_pool_t *pool)
{
  const svn_delta_editor_t *cmt_editor, *trk_editor;
  void *cmt_edit_baton, *trk_edit_baton;

  /* ### kff todo: imports are not known to work with xml yet. 
     They should someday. */

  /* Fetch the xml commit editor. */
  SVN_ERR (svn_delta_get_xml_editor (xml_stream, &cmt_editor, 
                                     &cmt_edit_baton, pool));

  /* If we're not supposed to bump revisions, just return the commit
     editor and baton. */
  if (! SVN_IS_VALID_REVNUM (revision))
    {
      *editor = cmt_editor;
      *edit_baton = cmt_edit_baton;
      return SVN_NO_ERROR;
    }

  /* Else, we're supposed to bump revisions to REVISION.  So fetch the
     tracking editor and compose it.  Committed targets will be stored
     in a hash, and bumped by svn_wc_process_committed().  */
  SVN_ERR (svn_delta_get_commit_track_editor 
           (&trk_editor, &trk_edit_baton, pool, apr_hash_make (pool),
            revision, svn_wc_process_committed, ccb));

  svn_delta_compose_editors (editor, edit_baton,
                             cmt_editor, cmt_edit_baton,
                             trk_editor, trk_edit_baton, 
                             pool);

  return SVN_NO_ERROR;
}


/* Import a tree or commit changes from a working copy.  
 *
 * Set *COMMITTED_REVISION, *COMMITTED_DATE, and *COMMITTED_AUTHOR to
 * the number, server-side date, and author of the new revision,
 * respectively.  Any of these may be NULL, in which case not touched.
 * If not NULL, but the date/author information is unavailable, then
 * *COMMITTED_DATE and *COMMITTED_AUTHOR will be set to NULL.
 *
 * BEFORE_EDITOR, BEFORE_EDIT_BATON and AFTER_EDITOR, AFTER_EDIT_BATON
 * are optional pre- and post-commit editors, wrapped around the
 * committing editor.
 *
 * Record USER as the author of the new revision, and LOG_MSG as its
 * log message.
 * 
 * BASE_PATH is the common prefix of all the targets.
 * 
 * If committing, CONDENSED_TARGETS is all the targets under BASE_PATH
 * but with the BASE_PATH prefix removed; else if importing,
 * CONDENSED_TARGETS is required to be null (you can only import one
 * thing at a time anyway, so it must be BASE_PATH).
 *
 * URL is null if not importing.  If non-null, then this is an import,
 * and URL is the repository directory where the imported data is
 * placed, and NEW_ENTRY is the new entry created in the repository
 * directory identified by URL.
 * 
 * If XML_DST is non-null, it is a file in which to store the xml
 * result of the commit, and REVISION is used as the revision.  If
 * XML_DST is null, REVISION is ignored.
 * 
 * Use POOL for all allocation.
 *
 * If no error is returned, and *COMMITTED_REV is set to
 * SVN_INVALID_REVNUM, then the commit was a no-op; nothing needed to
 * be committed.
 *
 * When importing:
 *
 *   If BASE_PATH is a file, that file is imported as NEW_ENTRY.  If
 *   BASE_PATH is a directory, the contents of that directory are
 *   imported, under a new directory the NEW_ENTRY in the repository.
 *   Note that the directory itself is not imported; that is, the
 *   basename of BASE_PATH is not part of the import.
 *
 *   If BASE_PATH is a directory and NEW_ENTRY is null, then the
 *   contents of BASE_PATH are imported directly into the repository
 *   directory identified by URL.  NEW_ENTRY may not be the empty
 *   string.
 *
 *   If NEW_ENTRY already exists in the youngest revision, return
 *   error.
 * 
 *   ### kff todo: This import is similar to cvs import, in that it
 *   does not change the source tree into a working copy.  However,
 *   this behavior confuses most people, and I think eventually svn
 *   _should_ turn the tree into a working copy, or at least should
 *   offer the option. However, doing so is a bit involved, and we
 *   don't need it right now.
 */
static svn_error_t *
send_to_repos (svn_client_commit_info_t **commit_info,
               const svn_delta_editor_t *before_editor,
               void *before_edit_baton,
               const svn_delta_editor_t *after_editor,
               void *after_edit_baton,                   
               svn_stringbuf_t *base_path,
               apr_array_header_t *condensed_targets,
               svn_stringbuf_t *url,        /* null unless importing */
               svn_stringbuf_t *new_entry,  /* null except when importing */
               svn_client_auth_baton_t *auth_baton,
               svn_stringbuf_t *log_msg,
               svn_stringbuf_t *xml_dst,
               svn_revnum_t revision,
               apr_pool_t *pool)
{
  /* Error values. */
  apr_status_t apr_err;
  svn_error_t *err;

  /* Editors/batons. */
  const svn_delta_editor_t *editor;
  void *edit_baton;
  const svn_delta_edit_fns_t *wrap_editor;
  void *wrap_edit_baton;
  struct svn_wc_close_commit_baton ccb;

  /* RA-layer stuff. */
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;

  /* Post-commit stuff. */
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  apr_file_t *xml_hnd;

  /* Some shortcut flags. */
  svn_boolean_t use_xml = (xml_dst && xml_dst->data) ? TRUE : FALSE;
  svn_boolean_t is_import = url ? TRUE : FALSE;


  /*** Setting up the commit/import ***/
  ccb.prefix_path = base_path;

  /* Sanity check: if this is an import, then NEW_ENTRY can be null or
     non-empty, but it can't be empty. */ 
  if (is_import && (new_entry && (strcmp (new_entry->data, "") == 0)))
    return svn_error_create (SVN_ERR_FS_PATH_SYNTAX, 0, NULL, pool,
                             "empty string is an invalid entry name");

  /* If we're committing to XML ... */
  if (use_xml)
    {
      /* Open the xml file for writing. */
      if ((apr_err = apr_file_open (&xml_hnd, xml_dst->data,
                                    (APR_WRITE | APR_CREATE),
                                    APR_OS_DEFAULT, pool)))
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error opening %s", xml_dst->data);
      
      /* ... we need an XML commit editor. */
      SVN_ERR (get_xml_editor (&editor, &edit_baton, 
                               svn_stream_from_aprfile (xml_hnd, pool),
                               &ccb, revision, pool));
    }

  /* Else we're committing to an RA layer. */
  else  
    {
      svn_wc_entry_t *entry;

      if (! is_import)
        {
          /* Construct full URL from PATH. */
          SVN_ERR (svn_wc_entry (&entry, base_path, pool));
          url = entry->url;

          if (entry->copied)
            return svn_error_createf
              (SVN_ERR_CL_COMMIT_IN_ADDED_DIR, 0, NULL, pool, 
               "%s was already scheduled for addition.", base_path->data);
        }
      
      /* Make sure our log message at least exists, even if empty. */
      if (! log_msg)
        log_msg = svn_stringbuf_create ("", pool);
      
      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url->data, pool));
      
      /* Open an RA session to URL. */
      /* (Notice that in the case of import, we do NOT want the RA
         layer to attempt to store auth info in the wc.) */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, base_path,
                                            !is_import, !is_import, is_import,
                                            auth_baton, pool));

      
      /* Fetch RA commit editor, giving it svn_wc_process_committed(). */
      SVN_ERR (ra_lib->get_commit_editor
               (session, &editor, &edit_baton, &committed_rev, 
                &committed_date, &committed_author, log_msg));
    }

  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);

  /* ### todo:  This is a TEMPORARY wrapper around our editor so we
     can use it with an old driver. */
  svn_delta_compat_wrap (&wrap_editor, &wrap_edit_baton, 
                         editor, edit_baton, pool);
  

  /*** Performing the commit/import ***/

  if (is_import)
    /* Crawl a directory tree, importing. */
    err = import (base_path, new_entry, wrap_editor, wrap_edit_baton, pool);
  else
    /* Crawl local mods and report changes to EDITOR.  When
       close_edit() is called, revisions will be bumped. */
    err = (svn_wc_crawl_local_mods 
           (base_path, condensed_targets, wrap_editor, wrap_edit_baton,
            use_xml ? NULL : &(ra_lib->get_latest_revnum),
            use_xml ? NULL : session, pool));

  /* If an error occured during the commit, abort the edit and return
     the error.  We don't even care if the abort itself fails.  */
  if (err)
    {
      wrap_editor->abort_edit (wrap_edit_baton);
      return err;
    }


  /*** Finishing the commit/import ***/

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
    /* We were committing to RA, so close the session. */
    SVN_ERR (ra_lib->close (session));


  /*** Telling the caller what we've accomplished ***/

  /* Fill in the commit_info structure. */
  *commit_info = svn_client__make_commit_info (committed_rev,
                                               committed_author,
                                               committed_date,
                                               pool);
  return SVN_NO_ERROR;
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
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  SVN_ERR (send_to_repos (commit_info,
                          before_editor, before_edit_baton,
                          after_editor, after_edit_baton,                   
                          path, NULL,
                          url, new_entry,
                          auth_baton,
                          log_msg,
                          xml_dst, revision,
                          pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_commit (svn_client_commit_info_t **commit_info,
                   const svn_delta_editor_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_editor_t *after_editor,
                   void *after_edit_baton, 
                   svn_client_auth_baton_t *auth_baton,
                   const apr_array_header_t *targets,
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *base_dir;
  apr_array_header_t *condensed_targets;
  svn_error_t *err;

  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      pool));

  /* If we calculated only a base_dir and no relative targets, this
     must mean that we are being asked to commit a single directory.
     In order to do this properly, we need to anchor our commit up one
     directory level, so long as our anchor is still a versioned
     directory. */
  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      svn_stringbuf_t *parent_dir, *basename;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, 
                                         &basename, pool));
      if (basename)
        {
          /* Our new "grandfather directory" is the parent directory
             of the former one. */
          svn_stringbuf_set (base_dir, parent_dir->data);

          /* Make the array if it wasn't already created. */
          if (! condensed_targets)
            condensed_targets = apr_array_make 
              (pool, targets->nelts, sizeof (svn_stringbuf_t *));

          /* Now, push this basename as a relative path to our new
             base directory. */
          (*((svn_stringbuf_t **)apr_array_push 
             (condensed_targets))) = basename;
        }
    }

#if 0
  /* This is temporary test code for cmpilato's new commit system.  It
     can be happily ignored. */
  {
    apr_hash_t *committables, *locked_dirs;
    apr_array_header_t *array;
    
    SVN_ERR (svn_client__harvest_committables (&committables, 
                                               &locked_dirs,
                                               base_dir,
                                               condensed_targets, 
                                               pool));
    
    if ((array = apr_hash_get (committables, SVN_CLIENT__SINGLE_REPOS_NAME, 
                               APR_HASH_KEY_STRING)))
      SVN_ERR (svn_client__do_commit (array, NULL, NULL, FALSE,
                                      NULL, NULL, pool));
  }
#endif /* 0 */


  err = send_to_repos (commit_info,
                       before_editor, before_edit_baton,
                       after_editor, after_edit_baton,                   
                       base_dir,
                       condensed_targets,
                       NULL, NULL,  /* NULLs because not importing */
                       auth_baton,
                       log_msg,
                       xml_dst, revision,
                       pool);
  
  /* Sleep for one second to ensure timestamp integrity. */
  apr_sleep (APR_USEC_PER_SEC * 1);

  return err;
}


svn_client_commit_info_t *
svn_client__make_commit_info (svn_revnum_t revision,
                              const char *author,
                              const char *date,
                              apr_pool_t *pool)
{
  svn_client_commit_info_t *info;

  if (date || author || SVN_IS_VALID_REVNUM (revision))
    {
      info = apr_palloc (pool, sizeof (*info));
      info->date = date ? apr_pstrdup (pool, date) : NULL;
      info->author = author ? apr_pstrdup (pool, author) : NULL;
      info->revision = revision;
      return info;
    }
  return NULL;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
