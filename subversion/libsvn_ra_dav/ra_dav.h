/*
 * ra_dav.h :  private declarations for the RA/DAV module
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



#ifndef RA_DAV_H
#define RA_DAV_H

#include <apr_pools.h>

#include <http_request.h>
#include <uri.h>


typedef struct {
  apr_pool_t *pool;

  struct uri root;              /* repository root */

  http_session *sess;           /* HTTP session to server */

} svn_ra_session_t;

/* Declare the initialization function here to prevent a GCC warning, but
   nobody really uses this prototype right now. */
/* ### hmm. when we statically link these, this prototype may be handy */
svn_error_t *svn_ra_dav_init(int abi_version,
                             svn_ra_init_params *params);


/** plugin function prototypes */

svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_edit_fns_t **editor,
  void **edit_baton,
  svn_revnum_t *new_revision);
svn_error_t * svn_ra_dav__checkout (void *session_baton,
                                    const svn_delta_edit_fns_t *editor,
                                    void *edit_baton,
                                    svn_string_t *URL);

#endif  /* RA_DAV_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
