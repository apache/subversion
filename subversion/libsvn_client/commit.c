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




/*** Public Interface. ***/

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
  apr_status_t apr_err;
  apr_file_t *dst = NULL; /* old habits die hard */
  svn_delta_edit_fns_t *track_editor;
  const svn_delta_edit_fns_t *commit_editor;
  void *commit_edit_baton, *track_edit_baton;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_hash_t *targets = NULL;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  struct svn_wc_close_commit_baton ccb = {path, pool};
  apr_array_header_t *tgt_array = apr_array_make (pool, 1,
                                                  sizeof(svn_string_t *));
  
  /* If we're committing to XML... */
  if (xml_dst && xml_dst->data)
    {
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
  else /* We're committing to an RA layer */
    {
      svn_wc_entry_t *entry;
      const char *URL;

      /* Construct full URL from PATH. */
      /* todo:  API here??  need to get from working copy. */
      SVN_ERR (svn_wc_entry (&entry, path, pool));
      URL = entry->ancestor->data;

      /* Make sure our log message at least exists, even if empty. */
      if (! log_msg)
        log_msg = svn_string_create ("", pool);

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      /* Open an RA session to URL */
      SVN_ERR (ra_lib->open (&session, svn_string_create (URL, pool), pool));
      
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

  /* Crawl local mods and report changes to EDITOR.  When close_edit()
     is called, revisions will be bumped. */
  SVN_ERR (svn_wc_crawl_local_mods (&targets, path, editor, edit_baton, pool));

  if (xml_dst && xml_dst->data)
    {
      /* If we were committing into XML, close the xml file. */      
      apr_err = apr_file_close (dst);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error closing %s", xml_dst->data);      
    }
  else
    /* We were committing to RA, so close the session. */
    SVN_ERR (ra_lib->close (session));
    
  /* THE END. */

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
