/*
 * ra_plugin.c : the main RA module for local repository access
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

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_ra.h"


/*

  Ben'z Notez

    Plan of attack for commits:

     - write an editor which remembers committed targets by stashing
     them in a list

     - get_commit_editor:  fetch the FS editor and compose our
     tracking editor with it as an "after" editor

     - client calls close_edit()

        - fs passes new revision to `hook' routine.

        - hook routine inserts final revision number into `tracking'
        editor's edit_baton

        - `tracking' edit's close_edit() is now called, which,
        noticing it has a valid revision number its edit_baton, calls
        the supplied close_func() on each target it tracked.
    

 */



/** The reporter routines **/


static svn_error_t *
set_directory (void *report_baton,
               svn_string_t *dir_path,
               svn_revnum_t revision)
{
  return SVN_NO_ERROR;
}
  

static svn_error_t *
set_file (void *report_baton,
          svn_string_t *file_path,
          svn_revnum_t revision)
{
  return SVN_NO_ERROR;
}
  


static svn_error_t *
finish_report (void *report_baton)
{
  return SVN_NO_ERROR;
}






/** The plugin routines **/


static svn_error_t *
open (void **session_baton,
      svn_string_t *repository_URL,
      apr_pool_t *pool)
{

  /* Stash info into the session baton */

  return SVN_NO_ERROR;
}


static svn_error_t *
close (void *session_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
get_latest_revnum (void *session_baton,
                   svn_revnum_t *latest_revnum)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
get_commit_editor (void *session_baton,
                   const svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_string_t *log_msg,
                   svn_ra_close_commit_func_t close_func,
                   svn_ra_set_wc_prop_func_t set_func,
                   void *close_baton)
{
  return SVN_NO_ERROR;
}



static svn_error_t *
do_checkout (void *session_baton,
             const svn_delta_edit_fns_t *editor,
             void *edit_baton)
{
  return SVN_NO_ERROR;
}



static svn_error_t *
do_update (void *session_baton,
           const svn_ra_reporter_t **reporter,
           void **report_baton,
           apr_array_header_t *targets,
           const svn_delta_edit_fns_t *update_editor,
           void *update_baton)
{
  return SVN_NO_ERROR;
}







/** The reporter and ra_plugin objects **/

static const svn_ra_reporter_t ra_local_reporter = 
{
  set_directory,
  set_file,
  finish_report
};


static const svn_ra_plugin_t ra_local_plugin = 
{
  "ra_local",
  "RA module for accessing file:// URLs",
  open,
  close,
  get_latest_revnum,
  get_commit_editor,
  do_checkout,
  do_update
};


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
