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
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *dst = NULL; /* old habits die hard */
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_hash_t *targets = NULL;

  /* Step 1: look for local mods and send 'em out. */
  apr_err = apr_file_open (&dst, xml_dst->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error opening %s", xml_dst->data);

  /* Right now, we're committing to Greg Hudson's XML-output editor.
     This needs to be changed; instead, it needs to get the
     appropriate commit editor from the appropriate RA plugin
     module. */
  err = svn_delta_get_xml_editor (svn_stream_from_aprfile (dst, pool),
                                  &editor,
                                  &edit_baton,
                                  pool);
  if (err)
    return err;

  /* Compose the commit-editor with any other editors passed in */
  svn_delta_wrap_editor (&editor,
                         &edit_baton,
                         before_editor,
                         before_edit_baton,
                         editor,
                         edit_baton,
                         after_editor,
                         after_edit_baton,
                         pool);


  /* Drive the editor, reporting local changes. */
  err = svn_wc_crawl_local_mods (&targets,
                                 path,
                                 editor,
                                 edit_baton,
                                 pool);
  if (err)
    return err;

  apr_err = apr_file_close (dst);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error closing %s", xml_dst->data);
  
  /* Step 2: tell the working copy the commit succeeded. */
  err = svn_wc_close_commit (path, revision, targets, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
