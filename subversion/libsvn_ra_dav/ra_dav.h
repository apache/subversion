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

#include "svn_ra.h"


typedef struct {
  apr_pool_t *pool;

  struct uri root;              /* repository root */

  http_session *sess;           /* HTTP session to server */

} svn_ra_session_t;

/* Declare the initialization function here to prevent a GCC warning, but
   nobody really uses this prototype right now. */
/* ### hmm. when we statically link these, this prototype may be handy */
/*svn_error_t *svn_ra_dav_init(int abi_version,
  apr_pool_t *pconf,
  const svn_ra_plugin_t **plugin); */


/** plugin function prototypes */


svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_edit_fns_t **editor,
  void **edit_baton,
  svn_string_t *log_msg,
  svn_ra_close_commit_func_t close_func,
  svn_ra_set_wc_prop_func_t set_func,
  void *close_baton);

svn_error_t * svn_ra_dav__do_checkout (
  void *session_baton,
  svn_revnum_t revision,
  const svn_delta_edit_fns_t *editor,
  void *edit_baton);

svn_error_t * svn_ra_dav__do_update(
  void *session_baton,
  const svn_ra_reporter_t **reporter,
  void **report_baton,
  apr_array_header_t *targets,
  const svn_delta_edit_fns_t *wc_update,
  void *wc_update_baton);

/*
** SVN_RA_DAV__LP_*: local properties for RA/DAV
**
** ra_dav stores properties on the client containing information needed
** to operate against the SVN server. Some of this informations is strictly
** necessary to store, and some is simply stored as a cached value.
*/

#define SVN_RA_DAV__LP_NAMESPACE \
        "http://subversion.tigris.org/props/ra/dav/local/"

/* store the URL where Activities can be created */
#define SVN_RA_DAV__LP_ACTIVITY_URL     SVN_RA_DAV__LP_NAMESPACE "activity-url"

/* store the URL of the version resource (from the DAV:target property) */
#define SVN_RA_DAV__LP_VSN_URL          SVN_RA_DAV__LP_NAMESPACE "version-url"


#endif  /* RA_DAV_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
