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
#include <apr_tables.h>

#include <http_request.h>
#include <uri.h>
#include <dav_207.h>            /* for dav_propname */

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"


typedef struct {
  apr_pool_t *pool;

  struct uri root;              /* repository root */

  http_session *sess;           /* HTTP session to server */

} svn_ra_session_t;



/** plugin function prototypes */

svn_error_t *svn_ra_dav__get_latest_revnum(void *session_baton,
                                           svn_revnum_t *latest_revnum);

svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_edit_fns_t **editor,
  void **edit_baton,
  svn_string_t *log_msg,
  svn_ra_get_wc_prop_func_t get_func,
  svn_ra_set_wc_prop_func_t set_func,
  svn_ra_close_commit_func_t close_func,
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
  svn_revnum_t revision_to_update_to,
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

/* store the URL of the version resource (from the DAV:checked-in property) */
#define SVN_RA_DAV__LP_VSN_URL          SVN_RA_DAV__LP_NAMESPACE "version-url"


/*
** SVN_RA_DAV__PROP_*: properties that we fetch from the server
**
** These are simply symbolic names for some standard properties that we fetch.
*/
#define SVN_RA_DAV__PROP_BASELINE_COLLECTION    "DAV:baseline-collection"
#define SVN_RA_DAV__PROP_CHECKED_IN     "DAV:checked-in"
#define SVN_RA_DAV__PROP_VCC            "DAV:version-controlled-configuration"
#define SVN_RA_DAV__PROP_VERSION_NAME   "DAV:version-name"

#define SVN_RA_DAV__PROP_BASELINE_RELPATH       "SVN:baseline-relative-path"

typedef struct {
  /* what is the URL for this resource */
  const char *url;

  /* is this resource a collection? (from the DAV:resourcetype element) */
  int is_collection;

  /* PROPSET: NAME -> VALUE (const char * -> const char *) */
  apr_hash_t *propset;

  /* --- only used during response processing --- */
  /* when we see a DAV:href element, what element is the parent? */
  int href_parent;

} svn_ra_dav_resource_t;

/* fetch a bunch of properties from the server. */
svn_error_t * svn_ra_dav__get_props(apr_hash_t **results,
                                    svn_ra_session_t *ras,
                                    const char *url,
                                    int depth,
                                    const char *label,
                                    const dav_propname *which_props,
                                    apr_pool_t *pool);

/* fetch a single resource's props from the server. */
svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             svn_ra_session_t *ras,
                                             const char *url,
                                             const char *label,
                                             const dav_propname *which_props,
                                             apr_pool_t *pool);

/* send an OPTIONS request to fetch the activity-collection-set */
svn_error_t * svn_ra_dav__get_activity_url(svn_string_t **activity_url,
                                           svn_ra_session_t *ras,
                                           const char *url,
                                           apr_pool_t *pool);

#endif  /* RA_DAV_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
