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

  /* Get a subpool for our allocations. */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Get an apr file for PATH. */
  apr_err = apr_file_open (&f, path->data, APR_READ, APR_OS_DEFAULT, subpool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, 
         "error opening `%s' for reading", path->data);
    }
  
  /* Get a readable stream of the file's contents. */
  contents = svn_stream_from_aprfile (f, subpool);

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Send the file's contents to the delta-window handler. */
  SVN_ERR (svn_txdelta_send_stream (contents, handler, handler_baton,
                                    subpool));

  /* Close the file. */
  apr_err = apr_file_close (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      return svn_error_createf
        (apr_err, 0, NULL, subpool, "error closing `%s'", path->data);
    }
  
  /* Destroy our subpool. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* Import file PATH as NAME in the repository directory indicated by
 * DIR_BATON in EDITOR.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file (const svn_delta_edit_fns_t *editor,
             void *dir_baton,
             svn_stringbuf_t *path,
             svn_stringbuf_t *name,
             apr_pool_t *pool)
{
  void *file_baton;

  SVN_ERR (editor->add_file (name, dir_baton,
                             NULL, SVN_INVALID_REVNUM, 
                             &file_baton));          
  SVN_ERR (send_file_contents (path, file_baton, editor, pool));
  
  /* Try to detect the mime-type of this new addition. */
  {
    const char *mimetype;

    SVN_ERR (svn_io_detect_mimetype (&mimetype, path->data, pool));
    if (mimetype)
      SVN_ERR (editor->change_file_prop 
               (file_baton,
                svn_stringbuf_create (SVN_PROP_MIME_TYPE, pool),
                svn_stringbuf_create (mimetype, pool)));
  }

  SVN_ERR (editor->close_file (file_baton));

  return SVN_NO_ERROR;
}
             

/* Import directory PATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  Don't call EDITOR->close_directory(DIR_BATON),
 * that's left for the caller.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_dir (const svn_delta_edit_fns_t *editor, 
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
          SVN_ERR (import_dir (editor, this_dir_baton, new_path, subpool));
          SVN_ERR (editor->close_directory (this_dir_baton));
        }
      else if (this_entry.filetype == APR_REG)
        {
          SVN_ERR (import_file (editor, dir_baton, new_path, name, subpool));
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
  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* Note that there is no need to check whether PATH's basename is
     "SVN".  It would be strange but not illegal to import the
     contents of a directory named SVN/, because the directory's own
     name is not part of those contents.  Of course, if something
     underneath it is also named "SVN", then we'll error. */

  if (kind == svn_node_file)
    {
      if (! new_entry)
        {
          return svn_error_create
            (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
             "new entry name required when importing a file");
        }

      SVN_ERR (import_file (editor, root_baton, path, new_entry, pool));
    }
  else if (kind == svn_node_dir)
    {
      void *new_dir_baton = NULL;

      /* Grab a new baton, making two we'll have to close. */
      if (new_entry)
        SVN_ERR (editor->add_directory (new_entry, root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        &new_dir_baton));

      SVN_ERR (import_dir (editor,
                           new_dir_baton ? new_dir_baton : root_baton,
                           path,
                           pool));

      /* Close one baton or two. */
      if (new_dir_baton)
        SVN_ERR (editor->close_directory (new_dir_baton));
      SVN_ERR (editor->close_directory (root_baton));
    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
         "'%s' does not exist.", path->data);  
    }
  
  SVN_ERR (editor->close_edit (edit_baton));

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
               const svn_delta_edit_fns_t *before_editor,
               void *before_edit_baton,
               const svn_delta_edit_fns_t *after_editor,
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
  apr_status_t apr_err;
  svn_error_t *err;
  apr_file_t *dst = NULL; /* old habits die hard */
  const svn_delta_editor_t *track_editor, *commit_editor;
  const svn_delta_edit_fns_t *wrap_cmt_editor, *wrap_trk_editor;
  void *commit_edit_baton, *track_edit_baton;
  void *wrap_cmt_edit_baton, *wrap_trk_edit_baton;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
#if 0  /* restore post-M2, see related #if farther down */
  const svn_delta_edit_fns_t *test_editor;
  void *test_edit_baton;
#endif /* 0 */
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_boolean_t is_import;
  struct svn_wc_close_commit_baton ccb;
  apr_hash_t *committed_targets;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  
  ccb.prefix_path	= base_path;
  committed_targets	= apr_hash_make (pool);

  if (url) 
    is_import = TRUE;
  else
    is_import = FALSE;

  /* Sanity check: if this is an import, then NEW_ENTRY can be null or
     non-empty, but it can't be empty. */ 
  if (is_import && (new_entry && (strcmp (new_entry->data, "") == 0)))
    {
      return svn_error_create (SVN_ERR_FS_PATH_SYNTAX, 0, NULL, pool,
                               "empty string is an invalid entry name");
    }

  /* If we're committing to XML... */
  if (xml_dst && xml_dst->data)
    {
      /* ### kff todo: imports are not known to work with xml yet.
         They should someday. */

      /* Open the xml file for writing. */
      apr_err = apr_file_open (&dst, xml_dst->data,
                               (APR_WRITE | APR_CREATE),
                               APR_OS_DEFAULT,
                               pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error opening %s", xml_dst->data);
      

      /* Fetch the xml commit editor. */
      SVN_ERR (svn_delta_get_xml_editor (svn_stream_from_aprfile (dst, pool),
                                         &commit_editor, &commit_edit_baton,
                                         pool));

      /* ### todo: This is a TEMPORARY wrapper around our editor so we
         can use it with an old driver. */
      svn_delta_compat_wrap (&wrap_cmt_editor, &wrap_cmt_edit_baton, 
                             commit_editor, commit_edit_baton, pool);


      if (! SVN_IS_VALID_REVNUM (revision))
        {
          editor = wrap_cmt_editor;
          edit_baton = wrap_cmt_edit_baton;
        }
      else
        {
          /* If we're supposed to bump revisions to REVISION, then
             fetch tracking editor and compose it.  Committed targets
             will be stored in committed_targets, and bumped by
             svn_wc_process_committed().  */
          SVN_ERR (svn_delta_get_commit_track_editor (&track_editor,
                                                      &track_edit_baton,
                                                      pool,
                                                      committed_targets,
                                                      revision,
                                                      svn_wc_process_committed,
                                                      &ccb));

          /* ### todo:  This is a TEMPORARY wrapper around our editor so we
             can use it with an old driver. */
          svn_delta_compat_wrap (&wrap_trk_editor, &wrap_trk_edit_baton, 
                                 track_editor, track_edit_baton, pool);
                                                      
          svn_delta_compose_editors (&editor, &edit_baton,
                                     wrap_cmt_editor, wrap_cmt_edit_baton,
                                     wrap_trk_editor, wrap_trk_edit_baton, 
                                     pool);
        }        
    }
  else   /* Else we're committing to an RA layer. */
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
                                            !is_import, !is_import,
                                            auth_baton, pool));

      
      /* Fetch RA commit editor, giving it svn_wc_process_committed(). */
      SVN_ERR (ra_lib->get_commit_editor
               (session,
                &editor, &edit_baton,
                &committed_rev,
                &committed_date,
                &committed_author,
                log_msg,
                /* wc prop fetching routine */
                is_import ? NULL : svn_wc_get_wc_prop,
                /* wc prop setting routine */
                is_import ? NULL : svn_wc_set_wc_prop,
                /* revision bumping routine */
                is_import ? NULL : svn_wc_process_committed,
                /* baton for the three funcs */
                &ccb));
    }

#if 0
  /* ### kff todo: after M2, put this back wrapped in a conditional,
     for implementing 'svn --trace' */
  SVN_ERR (svn_test_get_editor (&test_editor,
                                &test_edit_baton,
                                svn_stream_from_stdio (stdout, pool),
                                0,
                                base_path,
                                pool));
  
  svn_delta_compose_editors (&editor, &edit_baton,
                             editor, edit_baton,
                             test_editor, test_edit_baton, pool);
#endif /* 0 */


  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);
  
  /* Do the commit. */
  if (is_import)
    {
      /* Crawl a directory tree, importing. */
      err = import (base_path, new_entry, editor, edit_baton, pool);
      if (err)
        {
          /* ignoring the return value of this.  we're *already* about
             to die.  we just want to give the editor a chance to
             clean up the fs transaction. */
          editor->abort_edit (edit_baton);
          return err;
        }
    }
  else
    {
      /* Crawl local mods and report changes to EDITOR.  When
         close_edit() is called, revisions will be bumped. */

      if (xml_dst && xml_dst->data)
        /* committing to XML */
        err = svn_wc_crawl_local_mods (base_path,
                                       condensed_targets,
                                       editor, edit_baton,
                                       NULL, NULL,
                                       pool);
      else 
        /* committing to RA layer */
        err = svn_wc_crawl_local_mods (base_path,
                                       condensed_targets,
                                       editor, edit_baton,
                                       &(ra_lib->get_latest_revnum), session,
                                       pool);

      if (err)
        {
          /* ignoring the return value of this.  we're *already* about
             to die.  we just want to give the editor a chance to
             clean up the fs transaction. */
          editor->abort_edit (edit_baton);
          return err;
        }
    }


  if (xml_dst && xml_dst->data)
    {
      /* If we were committing into XML, close the xml file. */      
      apr_err = apr_file_close (dst);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error closing %s", xml_dst->data);
      
      /* Use REVISION for COMMITTED_REV. */
      committed_rev = revision;
    }
  else  
    /* We were committing to RA, so close the session. */
    SVN_ERR (ra_lib->close (session));

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
                   const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
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
                   const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
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
