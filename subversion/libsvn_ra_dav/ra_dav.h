/*
 * ra_dav.h :  private declarations for the RA/DAV module
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#ifndef SVN_LIBSVN_RA_DAV_H
#define SVN_LIBSVN_RA_DAV_H

#include <apr_pools.h>
#include <apr_tables.h>

#include <ne_request.h>
#include <ne_uri.h>
#include <ne_207.h>            /* for NE_ELM_207_UNUSED */
#include <ne_props.h>          /* for ne_propname */

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct {
  apr_pool_t *pool;

  const char *url;              /* original, unparsed url for this session */
  ne_uri root;                  /* parsed version of above */

  ne_session *sess;           /* HTTP session to server */
  ne_session *sess2;
  
  const svn_ra_callbacks_t *callbacks;  /* callbacks to get auth data */
  void *callback_baton;

} svn_ra_session_t;


#ifdef SVN_DEBUG
#define DEBUG_CR "\n"
#else
#define DEBUG_CR ""
#endif


/** plugin function prototypes */

svn_error_t *svn_ra_dav__get_latest_revnum(void *session_baton,
                                           svn_revnum_t *latest_revnum);

svn_error_t *svn_ra_dav__get_dated_revision (void *session_baton,
                                             svn_revnum_t *revision,
                                             apr_time_t timestamp);

svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_editor_t **editor,
  void **edit_baton,
  svn_revnum_t *new_rev,
  const char **committed_date,
  const char **committed_author,
  const char *log_msg);

svn_error_t * svn_ra_dav__get_file(
  void *session_baton,
  const char *path,
  svn_revnum_t revision,
  svn_stream_t *stream,
  svn_revnum_t *fetched_rev,
  apr_hash_t **props);

svn_error_t * svn_ra_dav__abort_commit(
 void *session_baton,
 void *edit_baton);

svn_error_t * svn_ra_dav__do_checkout (
  void *session_baton,
  svn_revnum_t revision,
  svn_boolean_t recurse,
  const svn_delta_editor_t *editor,
  void *edit_baton);

svn_error_t * svn_ra_dav__do_update(
  void *session_baton,
  const svn_ra_reporter_t **reporter,
  void **report_baton,
  svn_revnum_t revision_to_update_to,
  const char *update_target,
  svn_boolean_t recurse,
  const svn_delta_edit_fns_t *wc_update,
  void *wc_update_baton);

svn_error_t * svn_ra_dav__do_status(
  void *session_baton,
  const svn_ra_reporter_t **reporter,
  void **report_baton,
  const char *status_target,
  svn_boolean_t recurse,
  const svn_delta_edit_fns_t *wc_status,
  void *wc_status_baton);

svn_error_t * svn_ra_dav__do_switch(
  void *session_baton,
  const svn_ra_reporter_t **reporter,
  void **report_baton,
  svn_revnum_t revision_to_update_to,
  const char *update_target,
  svn_boolean_t recurse,
  const char *switch_url,
  const svn_delta_edit_fns_t *wc_update,
  void *wc_update_baton);

svn_error_t * svn_ra_dav__do_diff(
  void *session_baton,
  const svn_ra_reporter_t **reporter,
  void **report_baton,
  svn_revnum_t revision,
  const char *diff_target,
  svn_boolean_t recurse,
  const char *versus_url,
  const svn_delta_edit_fns_t *wc_diff,
  void *wc_diff_baton);

svn_error_t * svn_ra_dav__get_log(
  void *session_baton,
  const apr_array_header_t *paths,
  svn_revnum_t start,
  svn_revnum_t end,
  svn_boolean_t discover_changed_paths,
  svn_boolean_t strict_node_history,
  svn_log_message_receiver_t receiver,
  void *receiver_baton);

svn_error_t *svn_ra_dav__do_check_path(
  svn_node_kind_t *kind,
  void *session_baton,
  const char *path,
  svn_revnum_t revision);

/*
** SVN_RA_DAV__LP_*: local properties for RA/DAV
**
** ra_dav stores properties on the client containing information needed
** to operate against the SVN server. Some of this informations is strictly
** necessary to store, and some is simply stored as a cached value.
*/

#define SVN_RA_DAV__LP_NAMESPACE SVN_PROP_WC_PREFIX "ra_dav:"

/* store the URL where Activities can be created */
/* ### should fix the name to be "activity-coll" at some point */
#define SVN_RA_DAV__LP_ACTIVITY_COLL SVN_RA_DAV__LP_NAMESPACE "activity-url"

/* store the URL of the version resource (from the DAV:checked-in property) */
#define SVN_RA_DAV__LP_VSN_URL          SVN_RA_DAV__LP_NAMESPACE "version-url"

/* The entry committed rev an item must have for us to consider that
   item's SVN_RA_DAV__LP_VSN_URL valid. */
#define SVN_RA_DAV__LP_VSN_URL_REV  SVN_RA_DAV__LP_NAMESPACE "version-url-rev"


/*
** SVN_RA_DAV__PROP_*: properties that we fetch from the server
**
** These are simply symbolic names for some standard properties that we fetch.
*/
#define SVN_RA_DAV__PROP_BASELINE_COLLECTION    "DAV:baseline-collection"
#define SVN_RA_DAV__PROP_CHECKED_IN     "DAV:checked-in"
#define SVN_RA_DAV__PROP_VCC            "DAV:version-controlled-configuration"
#define SVN_RA_DAV__PROP_VERSION_NAME   "DAV:version-name"
#define SVN_RA_DAV__PROP_CREATIONDATE   "DAV:creationdate"
#define SVN_RA_DAV__PROP_CREATOR_DISPLAYNAME "DAV:creator-displayname"

#define SVN_RA_DAV__PROP_BASELINE_RELPATH \
    SVN_PROP_PREFIX "baseline-relative-path"

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

  apr_pool_t *pool;

} svn_ra_dav_resource_t;

/* ### WARNING: which_props can only identify properties which props.c
   ### knows about. see the elem_definitions[] array. */

/* fetch a bunch of properties from the server. */
svn_error_t * svn_ra_dav__get_props(apr_hash_t **results,
                                    ne_session *sess,
                                    const char *url,
                                    int depth,
                                    const char *label,
                                    const ne_propname *which_props,
                                    apr_pool_t *pool);

/* fetch a single resource's props from the server. */
svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             const ne_propname *which_props,
                                             apr_pool_t *pool);

/* fetch a single resource's starting props from the server. */
svn_error_t * svn_ra_dav__get_starting_props(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             apr_pool_t *pool);

/* fetch a single property from a single resource */
svn_error_t * svn_ra_dav__get_one_prop(const svn_string_t **propval,
                                       ne_session *sess,
                                       const char *url,
                                       const char *label,
                                       const ne_propname *propname,
                                       apr_pool_t *pool);

/* Get various Baseline-related information for a given "public" URL.

   Given a Neon session SESS and a URL, return whether the URL is a
   directory in *IS_DIR.  IS_DIR may be NULL if this flag is unneeded.

   REVISION may be SVN_INVALID_REVNUM to indicate that the operation
   should work against the latest (HEAD) revision, or whether it should
   return information about that specific revision.

   If BC_URL is not NULL, then it will be filled in with the URL for
   the Baseline Collection for the specified revision, or the HEAD.

   If BC_RELATIVE is not NULL, then it will be filled in with a
   relative pathname for the baselined resource corresponding to the
   revision of the resource specified by URL.

   If LATEST_REV is not NULL, then it will be filled in with the revision
   that this information corresponds to. Generally, this will be the same
   as the REVISION parameter, unless we are working against the HEAD. In
   that case, the HEAD revision number is returned.

   Allocation for BC_URL->data, BC_RELATIVE->data, and temporary data,
   will occur in POOL.

   Note: a Baseline Collection is a complete tree for a specified Baseline.
   DeltaV baselines correspond one-to-one to Subversion revisions. Thus,
   the entire state of a revision can be found in a Baseline Collection.
*/
svn_error_t *svn_ra_dav__get_baseline_info(svn_boolean_t *is_dir,
                                           svn_string_t *bc_url,
                                           svn_string_t *bc_relative,
                                           svn_revnum_t *latest_rev,
                                           ne_session *sess,
                                           const char *url,
                                           svn_revnum_t revision,
                                           apr_pool_t *pool);

extern const ne_propname svn_ra_dav__vcc_prop;
extern const ne_propname svn_ra_dav__checked_in_prop;




/* send an OPTIONS request to fetch the activity-collection-set */
svn_error_t * svn_ra_dav__get_activity_collection(
  const svn_string_t **activity_coll,
  svn_ra_session_t *ras,
  const char *url,
  apr_pool_t *pool);


/* Send a METHOD request (e.g., "MERGE", "REPORT", "PROPFIND") to URL
 * in session RAS, and parse the response.  If BODY is non-null, it is
 * the body of the request, else use the contents of file FD as the body.
 *
 * ELEMENTS is the set of xml elements to recognize in the response.
 *
 * VALIDATE_CB, STARTELM_CB, and ENDELM_CB are Neon validation, start
 * element, and end element handlers, respectively.  BATON is passed
 * to each as userdata.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *svn_ra_dav__parsed_request(svn_ra_session_t *ras,
                                        const char *method,
                                        const char *url,
                                        const char *body,
                                        int fd,
                                        const struct ne_xml_elm *elements, 
                                        ne_xml_validate_cb validate_cb,
                                        ne_xml_startelm_cb startelm_cb, 
                                        ne_xml_endelm_cb endelm_cb,
                                        void *baton,
                                        apr_pool_t *pool);


/* ### add SVN_RA_DAV_ to these to prefix conflicts with (sys) headers? */
enum {
  /* DAV elements */
  ELEM_activity_coll_set = NE_ELM_207_UNUSED,
  ELEM_baseline,
  ELEM_baseline_coll,
  ELEM_checked_in,
  ELEM_collection,
  ELEM_comment,
  ELEM_creationdate,
  ELEM_creator_displayname,
  ELEM_ignored_set,
  ELEM_merge_response,
  ELEM_merged_set,
  ELEM_options_response,
  ELEM_remove_prop,
  ELEM_resourcetype,
  ELEM_updated_set,
  ELEM_vcc,
  ELEM_version_name,
  ELEM_error,

  /* SVN elements */
  ELEM_add_directory,
  ELEM_add_file,
  ELEM_baseline_relpath,
  ELEM_deleted_path,  /* used in log reports */
  ELEM_added_path,    /* used in log reports */
  ELEM_changed_path,  /* used in log reports */
  ELEM_delete_entry,
  ELEM_fetch_file,
  ELEM_fetch_props,
  ELEM_log_date,
  ELEM_log_item,
  ELEM_log_report,
  ELEM_open_directory,
  ELEM_open_file,
  ELEM_target_revision,
  ELEM_update_report,
  ELEM_resource_walk,
  ELEM_resource,
  ELEM_prop,
  ELEM_name_version_name,
  ELEM_name_creationdate,
  ELEM_name_creator_displayname,
  ELEM_svn_error,
  ELEM_human_readable
};

/* ### docco */
svn_error_t * svn_ra_dav__merge_activity(
    svn_revnum_t *new_rev,
    const char **committed_date,
    const char **committed_author,
    svn_ra_session_t *ras,
    const char *repos_url,
    const char *activity_url,
    apr_hash_t *valid_targets,
    apr_pool_t *pool);


/* Make a buffer for repeated use with svn_stringbuf_set().
   ### it would be nice to start this buffer with N bytes, but there isn't
   ### really a way to do that in the string interface (yet), short of
   ### initializing it with a fake string (and copying it) */
#define MAKE_BUFFER(p) svn_stringbuf_ncreate("", 0, (p))

void svn_ra_dav__copy_href(svn_stringbuf_t *dst, const char *src);



/* If RAS contains authentication info, attempt to store it via client
   callbacks.  */
svn_error_t *
svn_ra_dav__maybe_store_auth_info (svn_ra_session_t *ras);


/* Create an error object for an error from neon in the given session,
   where the return code from neon was RETCODE, and CONTEXT describes
   what was being attempted. */
svn_error_t *svn_ra_dav__convert_error(ne_session *sess,
                                       const char *context,
                                       int retcode,
                                       apr_pool_t *pool);


/* Given a neon REQUEST and SESSION, run the request and return the
   http status code in *CODE.  Return any resulting error (from neon,
   a <D:error> body response, or any non-2XX status code) as an
   svn_error_t, otherwise return NULL.  The request will be freed
   either way.

   SESSION, METHOD, and URL are required as well, as they are used to
   describe the possible error.  The error will be allocated in POOL.

   OKAY_1 and OKAY_2 are the "acceptable" result codes. Anything other
   than one of these will generate an error. OKAY_1 should always be
   specified (e.g. as 200); use 0 for OKAY_2 if a second result code is
   not allowed.

   ### not super sure on this "okay" stuff, but it means that the request
   ### dispatching code can generate much better errors than the callers
   ### when something goes wrong. if we need more than two, then we could
   ### add another param, switch to an array, or do something entirely
   ### different...
 */
svn_error_t *
svn_ra_dav__request_dispatch(int *code,
                             ne_request *request,
                             ne_session *session,
                             const char *method,
                             const char *url,
                             int okay_1,
                             int okay_2,
                             apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_RA_DAV_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
