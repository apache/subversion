/*
 * commit.c:  wrappers around wc commit functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

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
  svn_txdelta_stream_t *delta_stream;
  svn_txdelta_window_t *window;
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

  /* Create a delta stream which converts an *empty* bytestream into the
     file's contents bytestream. */
  svn_txdelta (&delta_stream, svn_stream_empty (subpool), contents, subpool);

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Pull windows from the delta stream and feed to the consumer. */
  do 
    {
      SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
      SVN_ERR ((*handler) (window, handler_baton));
      if (window)
        svn_txdelta_free_window (window);
    } while (window);

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
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  apr_status_t apr_err;

  apr_err = apr_dir_open (&dir, path->data, subpool);
  for (apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir);
       APR_STATUS_IS_SUCCESS (apr_err);
       apr_err = apr_dir_read (&this_entry, APR_FINFO_NORM, dir))
    {
      svn_stringbuf_t *new_path = svn_string_dup (path, subpool);
      svn_path_add_component (new_path,
                              svn_string_create (this_entry.name, subpool),
                              svn_path_local_style);

      if (this_entry.filetype == APR_DIR)
        {
          svn_stringbuf_t *name = svn_string_create (this_entry.name, subpool);
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
        }
      else if (this_entry.filetype == APR_REG)
        {
          SVN_ERR (import_file (editor,
                                dir_baton,
                                new_path,
                                svn_string_create (this_entry.name, subpool),
                                subpool));
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
 * when the editor was fetched.  (I.e, when EDITOR->replace_root() is
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

  /* Get a root dir baton.  We pass an invalid revnum to replace_root
     to mean "base this on the youngest revision".  Should we have an
     SVN_YOUNGEST_REVNUM defined for these purposes? */
  SVN_ERR (editor->replace_root (edit_baton, SVN_INVALID_REVNUM, &root_baton));

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

  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}


/* Import a tree or commit changes from a working copy.
 *
 * BEFORE_EDITOR, BEFORE_EDIT_BATON and AFTER_EDITOR, AFTER_EDIT_BATON
 * are optional pre- and post-commit editors, wrapped around the
 * committing editor.
 *
 * Record LOG_MSG as the log message for the new revision.
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
send_to_repos (const svn_delta_edit_fns_t *before_editor,
               void *before_edit_baton,
               const svn_delta_edit_fns_t *after_editor,
               void *after_edit_baton,                   
               svn_stringbuf_t *base_dir,
               apr_array_header_t *condensed_targets,
               svn_stringbuf_t *url,            /* null unless importing */
               svn_stringbuf_t *new_entry,      /* null except when importing */
               svn_stringbuf_t *log_msg,
               svn_stringbuf_t *xml_dst,
               svn_revnum_t revision,
               apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;
  apr_file_t *dst = NULL; /* old habits die hard */
  svn_delta_edit_fns_t *track_editor;
  const svn_delta_edit_fns_t *commit_editor;
  void *commit_edit_baton, *track_edit_baton;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
#if 0  /* restore post-M2, see related #if farther down */
  const svn_delta_edit_fns_t *test_editor;
  void *test_edit_baton;
#endif /* 0 */
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_boolean_t is_import;
  struct svn_wc_close_commit_baton ccb = {base_dir, pool};
  apr_array_header_t *tgt_array
    = apr_array_make (pool, 1, sizeof (svn_stringbuf_t *));
  
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

      if (SVN_IS_VALID_REVNUM(revision))
        {
          /* If we're supposed to bump revisions to REVISION, then
             fetch tracking editor and compose it.  Committed targets
             will be stored in tgt_array, and bumped by
             svn_wc_set_revision().  */
          SVN_ERR (svn_delta_get_commit_track_editor (&track_editor,
                                                      &track_edit_baton,
                                                      pool,
                                                      tgt_array,
                                                      revision,
                                                      svn_wc_set_revision,
                                                      &ccb));
                                                      
          svn_delta_compose_editors (&editor, &edit_baton,
                                     commit_editor, commit_edit_baton,
                                     track_editor, track_edit_baton, pool);
        }        
    }
  else   /* Else we're committing to an RA layer. */
    {
      svn_wc_entry_t *entry;

      if (! is_import)
        {
          /* Construct full URL from PATH. */
          SVN_ERR (svn_wc_entry (&entry, base_dir, pool));
          url = entry->ancestor;
        }
      
      /* Make sure our log message at least exists, even if empty. */
      if (! log_msg)
        log_msg = svn_string_create ("", pool);
      
      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url->data, pool));
      
      /* Open an RA session to URL */
      SVN_ERR (ra_lib->open (&session, url, pool));
      
      /* Fetch RA commit editor, giving it svn_wc_set_revision(). */
      SVN_ERR (ra_lib->get_commit_editor
               (session,
                &editor, &edit_baton,
                log_msg,
                /* wc prop fetching routine */
                is_import ? NULL : svn_wc_get_wc_prop,
                /* wc prop setting routine */
                is_import ? NULL : svn_wc_set_wc_prop,
                /* revision bumping routine */
                is_import ? NULL : svn_wc_set_revision,
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
                                base_dir,
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
      err = import (base_dir, new_entry, editor, edit_baton, pool);
      if (err)
        {
          /* ignoring the return value of this.  we're *already*
             about to die.  we just want to give the RA layer a
             chance to clean up the fs transaction. */
          ra_lib->abort_commit (session, edit_baton);
          return err;
        }
    }
  else
    {
      /* Crawl local mods and report changes to EDITOR.  When
         close_edit() is called, revisions will be bumped. */
      err = svn_wc_crawl_local_mods (base_dir,
                                     condensed_targets,
                                     editor, edit_baton,
                                     pool);
      if (err)
        {
          /* ignoring the return value of this.  we're *already* about
             to die.  we just want to give the RA layer a chance to
             clean up the fs transaction. */
          ra_lib->abort_commit (session, edit_baton);
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
    }
  else  /* We were committing to RA, so close the session. */
    SVN_ERR (ra_lib->close (session));
  
  return SVN_NO_ERROR;
}




/*** Public Interfaces. ***/

svn_error_t *
svn_client_import (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,                   
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *url,
                   svn_stringbuf_t *new_entry,
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  SVN_ERR (send_to_repos (before_editor, before_edit_baton,
                          after_editor, after_edit_baton,                   
                          path, NULL,
                          url, new_entry,
                          log_msg, 
                          xml_dst, revision,
                          pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_commit (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,                   
                   const apr_array_header_t *targets,
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *base_dir;
  apr_array_header_t *condensed_targets;

  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      svn_path_local_style,
                                      pool));

  SVN_ERR (send_to_repos (before_editor, before_edit_baton,
                          after_editor, after_edit_baton,                   
                          base_dir,
                          condensed_targets,
                          NULL, NULL,  /* NULLs because not importing */
                          log_msg,
                          xml_dst, revision,
                          pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
