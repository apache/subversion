/*
 * ra_local.h : shared internal declarations for ra_local module
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_ra.h"



/** Structures **/

/* A baton which represents a single ra_local session. */
typedef struct svn_ra_local__session_baton_t
{
  /* Each ra_local session does ALL allocation from this pool!  Kind
     of like an Apache transaction, I guess. :) */
  apr_pool_t *pool;
  
  /* A `file://' URL containing a local repository and path. */
  svn_stringbuf_t *repository_URL;

  /* The user accessing the repository. */
  char *username;

  /* The URL above, split into two components. */
  svn_stringbuf_t *repos_path;
  svn_stringbuf_t *fs_path;

  /* A repository filesystem */
  svn_fs_t *fs;

} svn_ra_local__session_baton_t;



/* A device to record the targets of commits, and ensuring that proper
   commit closure happens on them (namely, revision setting and wc
   property setting).  This is passed to the `commit hook' routine by
   svn_fs_get_editor.  (   ) */
typedef struct svn_ra_local__commit_closer_t
{
  /* Allocation for this baton, as well as all committed_targets */
  apr_pool_t *pool;

  /* A list of target paths that have been committed */
  apr_array_header_t *target_array;

  /* A function given to RA by the client;  allows RA to bump WC
     revision numbers of targets. */
  svn_ra_close_commit_func_t close_func;
  
  /* A function given to RA by the client;  allows RA to store WC
     properties on targets.  (Wonder if ra_local will ever use this?!?) */
  svn_ra_set_wc_prop_func_t set_func;

  /* The baton to use with above functions */
  void *close_baton;

} svn_ra_local__commit_closer_t;






/** Private routines **/

    


/* Given a `file://' URL, figure out which portion specifies a
   repository on local disk, and return in REPOS_PATH; return the
   remainder (the path *within* the repository's filesystem) in
   FS_PATH.  Allocate the return values in POOL.  Currently, we are
   not expecting to handle `file://hostname/'-type URLs; hostname, in
   this case, is expected to be the empty string.  Also, the path
   which follows the */
svn_error_t *
svn_ra_local__split_URL (svn_stringbuf_t **repos_path,
                         svn_stringbuf_t **fs_path,
                         svn_stringbuf_t *URL,
                         apr_pool_t *pool);




/* Recursively walk over REVNUM:PATH inside an already-open repository
   FS, and drive a checkout EDITOR.  URL is the base ancestry that
   will be stored in the working copy.  Allocate all data in POOL. */
svn_error_t *
svn_ra_local__checkout (svn_fs_t *fs, 
                        svn_revnum_t revnum, 
                        svn_stringbuf_t *URL,
                        svn_stringbuf_t *fs_path,
                        const svn_delta_edit_fns_t *editor, 
                        void *edit_baton,
                        apr_pool_t *pool);









/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
