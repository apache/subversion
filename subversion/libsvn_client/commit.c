/*
 * commit.c:  wrappers around wc commit functionality.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"




/* Shared internals of import and commit. */

/* Import a tree or commit changes from a working copy.
 *
 * BEFORE_EDITOR, BEFORE_EDIT_BATON and AFTER_EDITOR, AFTER_EDIT_BATON
 * are optional pre- and post-commit editors, wrapped around the
 * committing editor.
 *
 * Record LOG_MSG as the log message for the new revision.
 * 
 * PATH is the path to local import source tree or working copy.  It
 * can be a file or directory.
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
 *   If PATH is a file, that file is imported as NEW_ENTRY.  If PATH
 *   is a directory, the contents of that directory are imported,
 *   under a new directory the NEW_ENTRY in the repository.  Note and
 *   the directory itself is not imported; that is, the basename of
 *   PATH is not part of the import.
 *
 *   If PATH is a directory and NEW_ENTRY is null, then the contents
 *   of PATH are imported directly into the repository directory
 *   identified by URL.  NEW_ENTRY may not be the empty string.
 *
 *   If NEW_ENTRY already exists in the youngest revision, return error.
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
               svn_string_t *path,
               svn_string_t *url,            /* null unless importing */
               svn_string_t *new_entry,      /* null except when importing */
               svn_string_t *log_msg,
               svn_string_t *xml_dst,
               svn_revnum_t revision,
               apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *dst = NULL; /* old habits die hard */
  svn_delta_edit_fns_t *track_editor;
  const svn_delta_edit_fns_t *commit_editor;
  void *commit_edit_baton, *track_edit_baton;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_boolean_t is_import;
  struct svn_wc_close_commit_baton ccb = {path, pool};
  apr_array_header_t *tgt_array = apr_array_make (pool, 1,
                                                  sizeof(svn_string_t *));
  
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
          SVN_ERR (svn_wc_entry (&entry, path, pool));
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
      
      /*
       * ### kff todo fooo working here.
       *
       * Pass a no-op revision bumping routine to get_commit_editor
       * below, in the import case.
       */

      /* Fetch RA commit editor, giving it svn_wc_set_revision(). */
      SVN_ERR (ra_lib->get_commit_editor
               (session,
                &editor, &edit_baton,
                log_msg,
                svn_wc_set_revision, /* revision bumping routine */
                NULL,                /* todo: func that sets WC props */
                &ccb));              /* baton for both funcs */
    }


  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);
  
  /* Do the commit. */
  if (is_import)
    {
      /* Crawl a directory tree, importing. 
       * ### kff todo fooo working here.
       */
      /*
        SVN_ERR (svn_wc_import_tree (path, editor, edit_baton, pool));
      */
    }
  else
    {
      /* Crawl local mods and report changes to EDITOR.  When
         close_edit() is called, revisions will be bumped. */
      SVN_ERR (svn_wc_crawl_local_mods (path, editor, edit_baton, pool));
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
                   svn_string_t *path,
                   svn_string_t *url,
                   svn_string_t *new_entry,
                   svn_string_t *log_msg,
                   svn_string_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  SVN_ERR (send_to_repos (before_editor, before_edit_baton,
                          after_editor, after_edit_baton,                   
                          path, url, new_entry,
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
                   svn_string_t *path,
                   svn_string_t *log_msg,
                   svn_string_t *xml_dst,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  SVN_ERR (send_to_repos (before_editor, before_edit_baton,
                          after_editor, after_edit_baton,                   
                          path, NULL, NULL,  /* NULLs because not importing */
                          log_msg,
                          xml_dst, revision,
                          pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
