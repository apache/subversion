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
                   svn_string_t *xml_dst,
                   svn_revnum_t revision,  /* this param is temporary */
                   apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *dst = NULL; /* old habits die hard */
  const svn_delta_edit_fns_t *commit_editor, *tracking_editor;
  void *commit_edit_baton, *tracking_edit_baton;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_hash_t *locks = NULL;
  apr_hash_t *targets = NULL;

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
          /* Fetch tracking editor WITH revision bumping enabled. */
          tracking_editor = svn_delta_default_editor (pool);
        }
      else
        {
          /* Fetch tracking editor WITHOUT revision bumping enabled. */
          tracking_editor = svn_delta_default_editor (pool);
        }
        
    }
  else
    {
      /* Construct full URL from PATH. */

      /* Get the RA plugin from the URL. */

      /* Open RA session to URL */

      /* Fetch RA editor, passing it svn_wc_set_revision(). */

      /* Fetch tracking editor WITHOUT revision bumping enabled. */
      tracking_editor = svn_delta_default_editor (pool);
    }

  /* Compose the "real" commit editor with the tracking editor. */
  svn_delta_compose_editors (&editor, &edit_baton,
                             commit_editor, commit_edit_baton,
                             tracking_editor, tracking_edit_baton, pool);


  /* Wrap the resulting editor with BEFORE and AFTER editors. */
  svn_delta_wrap_editor (&editor, &edit_baton,
                         before_editor, before_edit_baton,
                         editor, edit_baton, 
                         after_editor, after_edit_baton, pool);


  /* Crawl local mods. */
  SVN_ERR (svn_wc_crawl_local_mods (&targets, &locks,
                                    path, editor, edit_baton, pool));


  if (xml_dst && xml_dst->data)
    {
      apr_err = apr_file_close (dst);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "error closing %s", xml_dst->data);      
    }


  /* THIS NEEDS TO GO AWAY!! ... as soon as we know that either the
     tracking editor or the RA layer is bumping revision numbers. */
  SVN_ERR (svn_wc_close_commit (path, revision, targets, pool));

  /* Cleanup all returned locks. */

  /* THE END. */

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
