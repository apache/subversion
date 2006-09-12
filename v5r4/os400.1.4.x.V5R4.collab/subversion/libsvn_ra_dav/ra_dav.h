/*
 * ra_dav.h :  private declarations for the RA/DAV module
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_private_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Rename these types and constants to abstract from Neon */

#define SVN_RA_DAV__XML_VALID   (0)
#define SVN_RA_DAV__XML_INVALID (-1)
#define SVN_RA_DAV__XML_DECLINE (-2)

#define SVN_RA_DAV__XML_CDATA   (1<<1)
#define SVN_RA_DAV__XML_COLLECT ((1<<2) | SVN_RA_DAV__XML_CDATA)

typedef int svn_ra_dav__xml_elmid;

/** XML element */
typedef struct {
  /** XML namespace. */
  const char *nspace;

  /** XML tag name. */
  const char *name;

  /** XML tag id to be passed to a handler. */
  svn_ra_dav__xml_elmid id;

  /** Processing flags for this namespace:tag.
   *
   * 0 (zero)                - regular element, may have children,
   * SVN_RA_DAV__XML_CDATA   - child-less element,
   * SVN_RA_DAV__XML_COLLECT - complete contents of such element must be
   *                           collected as CDATA, includes *_CDATA flag. */
  unsigned int flags;

} svn_ra_dav__xml_elm_t;


/** (Neon 0.23) Callback to validate a new child element.
 *
 * @a parent and @a child are element ids found in the array of
 * elements, @a userdata is a user baton. Returns:
 *
 * SVN_RA_DAV__XML_VALID   - this is a valid element processed by this
 *                           handler;
 * SVN_RA_DAV__XML_INVALID - this is not a valid element, parsing should
 *                           stop;
 * SVN_RA_DAV__XML_DECLINE - this handler doesn't know about this element,
 *                           someone else may handle it.
 * 
 * (See @a shim_xml_push_handler in util.c for more information.) */
typedef int svn_ra_dav__xml_validate_cb(void *userdata,
                                        svn_ra_dav__xml_elmid parent,
                                        svn_ra_dav__xml_elmid child);

/** (Neon 0.23) Callback to start parsing a new child element.
 *
 * @a userdata is a user baton. @elm is a member of elements array,
 * and @a atts is an array of name-value XML attributes.
 * See @c svn_ra_dav__xml_validate_cb for return values. 
 *
 * (See @a shim_xml_push_handler in util.c for more information.) */
typedef int svn_ra_dav__xml_startelm_cb(void *userdata,
                                        const svn_ra_dav__xml_elm_t *elm,
                                        const char **atts);

/** (Neon 0.23) Callback to finish parsing a child element.
 *
 * Callback for @c svn_ra_dav__xml_push_handler. @a userdata is a user
 * baton. @elm is a member of elements array, and @a cdata is the contents
 * of the element.
 * See @c svn_ra_dav__xml_validate_cb for return values.
 *
 * (See @a shim_xml_push_handler in util.c for more information.) */
typedef int svn_ra_dav__xml_endelm_cb(void *userdata,
                                      const svn_ra_dav__xml_elm_t *elm,
                                      const char *cdata);





/* Context for neon request hooks; shared by the neon callbacks in
   session.c.  */
struct lock_request_baton
{
  /* The method neon is about to execute. */
  const char *method;

  /* The current working revision of item being locked. */
  svn_revnum_t current_rev;

  /* Whether client is "forcing" a lock or unlock. */
  svn_boolean_t force;

  /* The creation-date returned for newly created lock. */
  apr_time_t creation_date;

  /* The person who created the lock. */
  const char *lock_owner;

  /* A parser for handling <D:error> responses from mod_dav_svn. */
  ne_xml_parser *error_parser;

  /* If <D:error> is returned, here's where the parsed result goes. */
  svn_error_t *err;

  /* The neon request being executed */
  ne_request *request;

  /* A place for allocating fields in this structure. */
  apr_pool_t *pool;
};


typedef struct {
  apr_pool_t *pool;
  svn_stringbuf_t *url;                 /* original, unparsed session url */
  ne_uri root;                          /* parsed version of above */
  const char *repos_root;               /* URL for repository root */

  ne_session *sess;                     /* HTTP session to server */
  ne_session *sess2;

  const svn_ra_callbacks2_t *callbacks; /* callbacks to get auth data */
  void *callback_baton;
 
  svn_auth_iterstate_t *auth_iterstate; /* state of authentication retries */
  const char *auth_username;            /* last authenticated username used */

  svn_boolean_t compression;            /* should we use http compression? */
  const char *uuid;                     /* repository UUID */

  
  struct lock_request_baton *lrb;       /* used by lock/unlock */

  struct copy_baton *cb;                /* used by COPY */

} svn_ra_dav__session_t;


/* Id used with ne_set_session_private() and ne_get_session_private()
   to retrieve the userdata (which is currently the RA session baton!) */
#define SVN_RA_NE_SESSION_ID   "SVN"


#ifdef SVN_DEBUG
#define DEBUG_CR "\n"
#else
#define DEBUG_CR ""
#endif


/** vtable function prototypes */

svn_error_t *svn_ra_dav__get_latest_revnum(svn_ra_session_t *session,
                                           svn_revnum_t *latest_revnum,
                                           apr_pool_t *pool);

svn_error_t *svn_ra_dav__get_dated_revision(svn_ra_session_t *session,
                                            svn_revnum_t *revision,
                                            apr_time_t timestamp,
                                            apr_pool_t *pool);

svn_error_t *svn_ra_dav__change_rev_prop(svn_ra_session_t *session,
                                         svn_revnum_t rev,
                                         const char *name,
                                         const svn_string_t *value,
                                         apr_pool_t *pool);

svn_error_t *svn_ra_dav__rev_proplist(svn_ra_session_t *session,
                                      svn_revnum_t rev,
                                      apr_hash_t **props,
                                      apr_pool_t *pool);

svn_error_t *svn_ra_dav__rev_prop(svn_ra_session_t *session,
                                  svn_revnum_t rev,
                                  const char *name,
                                  svn_string_t **value,
                                  apr_pool_t *pool);

svn_error_t * svn_ra_dav__get_commit_editor(svn_ra_session_t *session,
                                            const svn_delta_editor_t **editor,
                                            void **edit_baton,
                                            const char *log_msg,
                                            svn_commit_callback2_t callback,
                                            void *callback_baton,
                                            apr_hash_t *lock_tokens,
                                            svn_boolean_t keep_locks,
                                            apr_pool_t *pool);

svn_error_t * svn_ra_dav__get_file(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t revision,
                                   svn_stream_t *stream,
                                   svn_revnum_t *fetched_rev,
                                   apr_hash_t **props,
                                   apr_pool_t *pool);

svn_error_t *svn_ra_dav__get_dir(svn_ra_session_t *session,
                                 apr_hash_t **dirents,
                                 svn_revnum_t *fetched_rev,
                                 apr_hash_t **props,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_uint32_t dirent_fields,
                                 apr_pool_t *pool);

svn_error_t * svn_ra_dav__abort_commit(void *session_baton,
                                       void *edit_baton);

svn_error_t * svn_ra_dav__do_update(svn_ra_session_t *session,
                                    const svn_ra_reporter2_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    const char *update_target,
                                    svn_boolean_t recurse,
                                    const svn_delta_editor_t *wc_update,
                                    void *wc_update_baton,
                                    apr_pool_t *pool);

svn_error_t * svn_ra_dav__do_status(svn_ra_session_t *session,
                                    const svn_ra_reporter2_t **reporter,
                                    void **report_baton,
                                    const char *status_target,
                                    svn_revnum_t revision,
                                    svn_boolean_t recurse,
                                    const svn_delta_editor_t *wc_status,
                                    void *wc_status_baton,
                                    apr_pool_t *pool);

svn_error_t * svn_ra_dav__do_switch(svn_ra_session_t *session,
                                    const svn_ra_reporter2_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision_to_update_to,
                                    const char *update_target,
                                    svn_boolean_t recurse,
                                    const char *switch_url,
                                    const svn_delta_editor_t *wc_update,
                                    void *wc_update_baton,
                                    apr_pool_t *pool);

svn_error_t * svn_ra_dav__do_diff(svn_ra_session_t *session,
                                  const svn_ra_reporter2_t **reporter,
                                  void **report_baton,
                                  svn_revnum_t revision,
                                  const char *diff_target,
                                  svn_boolean_t recurse,
                                  svn_boolean_t ignore_ancestry,
                                  svn_boolean_t text_deltas,
                                  const char *versus_url,
                                  const svn_delta_editor_t *wc_diff,
                                  void *wc_diff_baton,
                                  apr_pool_t *pool);

svn_error_t * svn_ra_dav__get_log(svn_ra_session_t *session,
                                  const apr_array_header_t *paths,
                                  svn_revnum_t start,
                                  svn_revnum_t end,
                                  int limit,
                                  svn_boolean_t discover_changed_paths,
                                  svn_boolean_t strict_node_history,
                                  svn_log_message_receiver_t receiver,
                                  void *receiver_baton,
                                  apr_pool_t *pool);

svn_error_t *svn_ra_dav__do_check_path(svn_ra_session_t *session,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_node_kind_t *kind,
                                       apr_pool_t *pool);

svn_error_t *svn_ra_dav__do_stat(svn_ra_session_t *session,
                                 const char *path,
                                 svn_revnum_t revision,
                                 svn_dirent_t **dirent,
                                 apr_pool_t *pool);

svn_error_t *svn_ra_dav__get_file_revs(svn_ra_session_t *session,
                                       const char *path,
                                       svn_revnum_t start,
                                       svn_revnum_t end,
                                       svn_ra_file_rev_handler_t handler,
                                       void *handler_baton,
                                       apr_pool_t *pool);


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
#define SVN_RA_DAV__PROP_GETCONTENTLENGTH "DAV:getcontentlength"

#define SVN_RA_DAV__PROP_BASELINE_RELPATH \
    SVN_DAV_PROP_NS_DAV "baseline-relative-path"

#define SVN_RA_DAV__PROP_MD5_CHECKSUM SVN_DAV_PROP_NS_DAV "md5-checksum"

#define SVN_RA_DAV__PROP_REPOSITORY_UUID SVN_DAV_PROP_NS_DAV "repository-uuid"

#define SVN_RA_DAV__PROP_DEADPROP_COUNT SVN_DAV_PROP_NS_DAV "deadprop-count"

typedef struct {
  /* what is the URL for this resource */
  const char *url;

  /* is this resource a collection? (from the DAV:resourcetype element) */
  int is_collection;

  /* PROPSET: NAME -> VALUE (const char * -> const svn_string_t *) */
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

/* Shared helper func: given a public URL which may not exist in HEAD,
   use SESS to search up parent directories until we can retrieve a
   *RSRC (allocated in POOL) containing a standard set of "starting"
   props: {VCC, resourcetype, baseline-relative-path}.  

   Also return *MISSING_PATH (allocated in POOL), which is the
   trailing portion of the URL that did not exist.  If an error
   occurs, *MISSING_PATH isn't changed. */
svn_error_t * 
svn_ra_dav__search_for_starting_props(svn_ra_dav_resource_t **rsrc,
                                      const char **missing_path,
                                      ne_session *sess,
                                      const char *url,
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

/* Fetch a baseline resource populated with specific properties.
   
   Given a Neon session SESS and a URL, set *BLN_RSRC to a baseline of
   REVISION, populated with whatever properties are specified by
   WHICH_PROPS.  To fetch all properties, pass NULL for WHICH_PROPS.

   If BC_RELATIVE is not NULL, then it will be filled in with a
   relative pathname for the baselined resource corresponding to the
   revision of the resource specified by URL.
*/
svn_error_t *svn_ra_dav__get_baseline_props(svn_string_t *bc_relative,
                                            svn_ra_dav_resource_t **bln_rsrc,
                                            ne_session *sess,
                                            const char *url,
                                            svn_revnum_t revision,
                                            const ne_propname *which_props,
                                            apr_pool_t *pool);

/* Fetch the repository's unique Version-Controlled-Configuration url.
   
   Given a Neon session SESS and a URL, set *VCC to the url of the
   repository's version-controlled-configuration resource.
 */
svn_error_t *svn_ra_dav__get_vcc(const char **vcc,
                                 ne_session *sess,
                                 const char *url,
                                 apr_pool_t *pool);

/* Issue a PROPPATCH request on URL, transmitting PROP_CHANGES (a hash
   of const svn_string_t * values keyed on Subversion user-visible
   property names) and PROP_DELETES (an array of property names to
   delete).  Send any extra request headers in EXTRA_HEADERS. Use POOL
   for all allocations.  */
svn_error_t *svn_ra_dav__do_proppatch(svn_ra_dav__session_t *ras,
                                      const char *url,
                                      apr_hash_t *prop_changes,
                                      apr_array_header_t *prop_deletes,
                                      apr_hash_t *extra_headers,
                                      apr_pool_t *pool);

extern const ne_propname svn_ra_dav__vcc_prop;
extern const ne_propname svn_ra_dav__checked_in_prop;




/* send an OPTIONS request to fetch the activity-collection-set */
svn_error_t * svn_ra_dav__get_activity_collection
  (const svn_string_t **activity_coll,
   svn_ra_dav__session_t *ras,
   const char *url,
   apr_pool_t *pool);


/* Call ne_set_request_body_pdovider on REQ with a provider function
 * that pulls data from BODY_FILE.
 */
svn_error_t *svn_ra_dav__set_neon_body_provider(ne_request *req,
                                                apr_file_t *body_file);


/** Find a given element in the table of elements.
 *
 * The table of XML elements @a table is searched until element identified by
 * namespace @a nspace and name @a name is found. If no elements are found,
 * tries to find and return element identified by @c ELEM_unknown. If that is
 * not found, returns NULL pointer. */
const svn_ra_dav__xml_elm_t *
svn_ra_dav__lookup_xml_elem(const svn_ra_dav__xml_elm_t *table,
                            const char *nspace,
                            const char *name);


/* Send a METHOD request (e.g., "MERGE", "REPORT", "PROPFIND") to URL
 * in session SESS, and parse the response.  If BODY is non-null, it is
 * the body of the request, else use the contents of file BODY_FILE
 * as the body.
 *
 * VALIDATE_CB, STARTELM_CB, and ENDELM_CB are Neon validation, start
 * element, and end element handlers, respectively, from Neon > 0.24.
 * BATON is passed to each as userdata.
 *
 * SET_PARSER is a callback function which, if non-NULL, is called
 * with the XML parser and BATON.  This is useful for providers of
 * validation and element handlers which require access to the parser.
 *
 * EXTRA_HEADERS is a hash of (const char *) key/value pairs to be
 * inserted as extra headers in the request.  Can be NULL.
 *
 * STATUS_CODE is an optional 'out' parameter; if non-NULL, then set
 * *STATUS_CODE to the http status code returned by the server.  This
 * can be set to a useful value even when the function returns an error
 * however it is not always set when an error is returned.  So any caller
 * wishing to check *STATUS_CODE when an error has been returned must
 * initialise *STATUS_CODE before calling the function.
 *
 * If SPOOL_RESPONSE is set, the request response will be cached to
 * disk in a tmpfile (in full), then read back and parsed.
 *
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_ra_dav__parsed_request(ne_session *sess,
                           const char *method,
                           const char *url,
                           const char *body,
                           apr_file_t *body_file,
                           void set_parser(ne_xml_parser *parser,
                                           void *baton),
                           ne_xml_startelm_cb *startelm_cb,
                           ne_xml_cdata_cb *cdata_cb,
                           ne_xml_endelm_cb *endelm_cb,
                           void *baton,
                           apr_hash_t *extra_headers,
                           int *status_code,
                           svn_boolean_t spool_response,
                           apr_pool_t *pool);
  

/* Same as svn_ra_dav__parsed_request, except:
 *
 * ELEMENTS is the set of xml elements to recognize in the response.
 *
 * The callbacks VALIDATE_CB, STARTELM_CB, and ENDELM_CB, are written
 * for the Neon <= 0.23 API.
 */
svn_error_t *
svn_ra_dav__parsed_request_compat(ne_session *sess,
                                  const char *method,
                                  const char *url,
                                  const char *body,
                                  apr_file_t *body_file,
                                  void set_parser(ne_xml_parser *parser,
                                                  void *baton),
                                  const svn_ra_dav__xml_elm_t *elements, 
                                  svn_ra_dav__xml_validate_cb validate_cb,
                                  svn_ra_dav__xml_startelm_cb startelm_cb, 
                                  svn_ra_dav__xml_endelm_cb endelm_cb,
                                  void *baton,
                                  apr_hash_t *extra_headers,
                                  int *status_code,
                                  svn_boolean_t spool_response,
                                  apr_pool_t *pool);


/* ### add SVN_RA_DAV_ to these to prefix conflicts with (sys) headers? */
enum {
  /* Redefine Neon elements */
  /* With the new API, we need to be able to use element id also as a return
   * value from the new `startelm' callback, hence all element ids must be
   * positive. Root element id is the only id that is not positive, it's zero.
   * `Root state' is never returned by a callback, it's only passed into it.
   * Therefore, negative element ids are forbidden from now on. */
  ELEM_unknown = 1, /* was (-1), see above why it's (1) now */
  ELEM_root = NE_XML_STATEROOT, /* (0) */
  ELEM_UNUSED = 100,
  ELEM_207_first = ELEM_UNUSED,
  ELEM_multistatus = ELEM_207_first,
  ELEM_response = ELEM_207_first + 1,
  ELEM_responsedescription = ELEM_207_first + 2,
  ELEM_href = ELEM_207_first + 3,
  ELEM_propstat = ELEM_207_first + 4,
  ELEM_prop = ELEM_207_first + 5, /* `prop' tag in the DAV namespace */
  ELEM_status = ELEM_207_first + 6,
  ELEM_207_UNUSED = ELEM_UNUSED + 100,
  ELEM_PROPS_UNUSED = ELEM_207_UNUSED + 100,

  /* DAV elements */
  ELEM_activity_coll_set = ELEM_207_UNUSED,
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
  ELEM_set_prop,
  ELEM_remove_prop,
  ELEM_resourcetype,
  ELEM_get_content_length,
  ELEM_updated_set,
  ELEM_vcc,
  ELEM_version_name,
  ELEM_post_commit_err,
  ELEM_error,

  /* SVN elements */
  ELEM_absent_directory,
  ELEM_absent_file,
  ELEM_add_directory,
  ELEM_add_file,
  ELEM_baseline_relpath, 
  ELEM_md5_checksum,
  ELEM_deleted_path,  /* used in log reports */
  ELEM_replaced_path,  /* used in log reports */
  ELEM_added_path,    /* used in log reports */
  ELEM_modified_path,  /* used in log reports */
  ELEM_delete_entry,
  ELEM_fetch_file,
  ELEM_fetch_props,
  ELEM_txdelta,
  ELEM_log_date,
  ELEM_log_item,
  ELEM_log_report,
  ELEM_open_directory,
  ELEM_open_file,
  ELEM_target_revision,
  ELEM_update_report,
  ELEM_resource_walk,
  ELEM_resource,
  ELEM_SVN_prop, /* `prop' tag in the Subversion namespace */
  ELEM_dated_rev_report,
  ELEM_name_version_name,
  ELEM_name_creationdate,
  ELEM_name_creator_displayname,
  ELEM_svn_error,
  ELEM_human_readable,
  ELEM_repository_uuid,
  ELEM_get_locations_report,
  ELEM_location,
  ELEM_file_revs_report,
  ELEM_file_rev,
  ELEM_rev_prop,
  ELEM_get_locks_report,
  ELEM_lock,
  ELEM_lock_path,
  ELEM_lock_token,
  ELEM_lock_owner,
  ELEM_lock_comment,
  ELEM_lock_creationdate,
  ELEM_lock_expirationdate,
  ELEM_editor_report,
  ELEM_open_root,
  ELEM_apply_textdelta,
  ELEM_change_file_prop,
  ELEM_change_dir_prop,
  ELEM_close_file,
  ELEM_close_directory,
  ELEM_deadprop_count
};

/* ### docco */
svn_error_t * svn_ra_dav__merge_activity(svn_revnum_t *new_rev,
                                         const char **committed_date,
                                         const char **committed_author,
                                         const char **post_commit_err,
                                         svn_ra_dav__session_t *ras,
                                         const char *repos_url,
                                         const char *activity_url,
                                         apr_hash_t *valid_targets,
                                         apr_hash_t *lock_tokens,
                                         svn_boolean_t keep_locks,
                                         svn_boolean_t disable_merge_response,
                                         apr_pool_t *pool);


/* Make a buffer for repeated use with svn_stringbuf_set().
   ### it would be nice to start this buffer with N bytes, but there isn't
   ### really a way to do that in the string interface (yet), short of
   ### initializing it with a fake string (and copying it) */
#define MAKE_BUFFER(p) svn_stringbuf_ncreate("", 0, (p))

void svn_ra_dav__copy_href(svn_stringbuf_t *dst, const char *src);



/* If RAS contains authentication info, attempt to store it via client
   callbacks and using POOL for temporary allocations.  */
svn_error_t *
svn_ra_dav__maybe_store_auth_info(svn_ra_dav__session_t *ras,
                                  apr_pool_t *pool);


/* Like svn_ra_dav__maybe_store_auth_info(), but conditional on ERR.

   Attempt to store auth info only if ERR is NULL or if ERR->apr_err
   is not SVN_ERR_RA_NOT_AUTHORIZED.  If ERR is not null, return it no
   matter what, otherwise return the result of the attempt (if any) to
   store auth info, else return SVN_NO_ERROR. */
svn_error_t *
svn_ra_dav__maybe_store_auth_info_after_result(svn_error_t *err,
                                               svn_ra_dav__session_t *ras,
                                               apr_pool_t *pool);


/* Create an error object for an error from neon in the given session,
   where the return code from neon was RETCODE, and CONTEXT describes
   what was being attempted.  Do temporary allocations in POOL. */
svn_error_t *svn_ra_dav__convert_error(ne_session *sess,
                                       const char *context,
                                       int retcode,
                                       apr_pool_t *pool);


/* Callback to get data from a Neon request after it has been sent.

   REQUEST is the request, DISPATCH_RETURN_VAL is the value that
   ne_request_dispatch(REQUEST) returned to the caller.

   USERDATA is a closure baton. */
typedef svn_error_t *
svn_ra_dav__request_interrogator(ne_request *request,
                                 int dispatch_return_val,
                                 void *userdata);

/* Given a neon REQUEST and SESSION, run the request; if CODE_P is
   non-null, return the http status code in *CODE_P.  Return any
   resulting error (from neon, a <D:error> body response, or any
   non-2XX status code) as an svn_error_t, otherwise return NULL.  
   The request will be freed either way.

   SESSION, METHOD, and URL are required as well, as they are used to
   describe the possible error.  The error will be allocated in POOL.

   OKAY_1 and OKAY_2 are the "acceptable" result codes. Anything other
   than one of these will generate an error. OKAY_1 should always be
   specified (e.g. as 200); use 0 for OKAY_2 if a second result code is
   not allowed.

   #ifdef SVN_NEON_0_25

      If INTERROGATOR is non-NULL, invoke it with the Neon request,
      the dispatch result, and INTERROGATOR_BATON.  This is done
      regardless of whether the request appears successful or not.  If
      the interrogator has an error result, return that error
      immediately, after freeing the request.

   #endif // SVN_NEON_0_25

   ### not super sure on this "okay" stuff, but it means that the request
   ### dispatching code can generate much better errors than the callers
   ### when something goes wrong. if we need more than two, then we could
   ### add another param, switch to an array, or do something entirely
   ### different...
 */
svn_error_t *
svn_ra_dav__request_dispatch(int *code_p,
                             ne_request *request,
                             ne_session *session,
                             const char *method,
                             const char *url,
                             int okay_1,
                             int okay_2,
#ifdef SVN_NEON_0_25
                             svn_ra_dav__request_interrogator interrogator,
                             void *interrogator_baton,
#endif /* SVN_NEON_0_25 */
                             apr_pool_t *pool);


/* Give PARSER the ability to parse a mod_dav_svn <D:error> response
   body in the case of a non-2XX response to REQUEST.  If a <D:error>
   response is detected, then set *ERR to the parsed error.
*/
void
svn_ra_dav__add_error_handler(ne_request *request,
                              ne_xml_parser *parser,
                              svn_error_t **err,
                              apr_pool_t *pool);


/*
 * Implements the get_locations RA layer function. */
svn_error_t *
svn_ra_dav__get_locations(svn_ra_session_t *session,
                          apr_hash_t **locations,
                          const char *path,
                          svn_revnum_t peg_revision,
                          apr_array_header_t *location_revisions,
                          apr_pool_t *pool);


/*
 * Implements the get_locks RA layer function. */
svn_error_t *
svn_ra_dav__get_locks(svn_ra_session_t *session,
                      apr_hash_t **locks,
                      const char *path,
                      apr_pool_t *pool);

/*
 * Implements the get_lock RA layer function. */
svn_error_t *
svn_ra_dav__get_lock(svn_ra_session_t *session,
                     svn_lock_t **lock,
                     const char *path,
                     apr_pool_t *pool);

svn_error_t *
svn_ra_dav__replay(svn_ra_session_t *session,
                   svn_revnum_t revision,
                   svn_revnum_t low_water_mark,
                   svn_boolean_t send_deltas,
                   const svn_delta_editor_t *editor,
                   void *edit_baton,
                   apr_pool_t *pool);

/* Helper function.  Loop over LOCK_TOKENS and assemble all keys and
   values into a stringbuf allocated in POOL.  The string will be of
   the form

    <S:lock-token-list xmlns:S="svn:">
      <S:lock>
        <S:lock-path>path</S:lock-path>
        <S:lock-token>token</S:lock-token>
      </S:lock>
      [...]
    </S:lock-token-list>

   Callers can then send this in the request bodies, as a way of
   reliably marshalling potentially unbounded lists of locks.  (We do
   this because httpd has limits on how much data can be sent in 'If:'
   headers.)
 */
svn_error_t *
svn_ra_dav__assemble_locktoken_body(svn_stringbuf_t **body,
                                    apr_hash_t *lock_tokens,
                                    apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_RA_DAV_H */
