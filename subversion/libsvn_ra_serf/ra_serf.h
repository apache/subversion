/*
 * ra_serf.h :  headers file for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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



#include <serf.h>
#include <expat.h>
#include <apr_uri.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_version.h"

#include "svn_dav.h"


/**
 * The master serf RA session.
 *
 * This is stored in the ra session ->priv field.
 */
typedef struct {
  /* Pool for allocations during this session */
  apr_pool_t *pool;

  /* The current context */
  serf_context_t *context;
  serf_ssl_context_t *ssl_context;

  /* Are we using ssl */
  svn_boolean_t using_ssl;

  /* Bucket allocator for this context. */
  serf_bucket_alloc_t *bkt_alloc;

  /* The current connection */
  serf_connection_t *conn;

  /* The URL that was passed into _open() */
  apr_uri_t repos_url;
  const char *repos_url_str;

  /* The actual discovered root */
  apr_uri_t repos_root;
  const char *repos_root_str;

  /* Cached properties */
  apr_hash_t *cached_props;

} ra_serf_session_t;

/**
 * Structure which represents a DAV element with a NAMESPACE and NAME.
 */
typedef struct {
  /* Element namespace */
  const char *namespace;
  /* Element name */
  const char *name;
} dav_props_t;

/**
 * Structure which represents an XML namespace.
 */
typedef struct ns_t {
  /* The assigned name. */
  const char *namespace;
  /* The full URL for this namespace. */
  const char *url;
  /* The next namespace in our list. */
  struct ns_t *next;
} ns_t;

/**
 * This structure represents a pending PROPFIND response.
 */
typedef struct propfind_context_t {
  /* pool to issue allocations from */
  apr_pool_t *pool;

  /* associated serf session */
  ra_serf_session_t *sess;

  /* the requested path */
  const char *path;

  /* the list of requested properties */
  const dav_props_t *find_props;

  /* hash table that will be updated with the properties
   *
   * This can be shared between multiple propfind_context_t structures
   */
  apr_hash_t *ret_props;

  /* the xml parser used */
  XML_Parser xmlp;

  /* Current namespace list */
  ns_t *ns_list;

  /* TODO use the state object as in the report */
  /* Are we parsing a property right now? */
  svn_boolean_t in_prop;

  /* Should we be harvesting the CDATA elements */
  svn_boolean_t collect_cdata;

  /* Current ns, attribute name, and value of the property we're parsing */
  const char *ns;
  const char *attr_name;
  const char *attr_val;
  apr_size_t attr_val_len;

  /* Are we done issuing the PROPFIND? */
  svn_boolean_t done;

  /* The next PROPFIND we have open. */
  struct propfind_context_t *next;
} propfind_context_t;

/** DAV property sets */
static const dav_props_t base_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  { "DAV:", "resourcetype" },
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path" },
  { SVN_DAV_PROP_NS_DAV, "repository-uuid" },
  NULL
};

static const dav_props_t checked_in_props[] =
{
  { "DAV:", "checked-in" },
  NULL
};

static const dav_props_t baseline_props[] =
{
  { "DAV:", "baseline-collection" },
  { "DAV:", "version-name" },
  NULL
};

static const dav_props_t all_props[] =
{
  { "DAV:", "allprop" },
  NULL
};

static const dav_props_t vcc_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  NULL
};

static const dav_props_t check_path_props[] =
{
  { "DAV:", "resourcetype" },
  NULL
};

static const dav_props_t uuid_props[] =
{
  { SVN_DAV_PROP_NS_DAV, "repository-uuid" },
  NULL
};

static const dav_props_t repos_root_props[] =
{
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path" },
  NULL
};

/** Serf utility functions **/

serf_bucket_t *
conn_setup(apr_socket_t *sock,
           void *baton,
           apr_pool_t *pool);

serf_bucket_t*
accept_response(serf_request_t *request,
                serf_bucket_t *stream,
                void *acceptor_baton,
                apr_pool_t *pool);

void
conn_closed (serf_connection_t *conn,
             void *closed_baton,
             apr_status_t why,
             apr_pool_t *pool);

apr_status_t
cleanup_serf_session(void *data);

/**
 * Create a REQUEST with an associated REQ_BKT in the SESSION.
 *
 * If HDRS_BKT is not-NULL, it will be set to a headers_bucket that
 * corresponds to the new request.
 *
 * The request will be METHOD at URL.
 *
 * If BODY_BKT is not-NULL, it will be sent as the request body.
 *
 * If CONTENT_TYPE is not-NULL, it will be sent as the Content-Type header.
 */
void
create_serf_req(serf_request_t **request,
                serf_bucket_t **req_bkt, serf_bucket_t **hdrs_bkt,
                ra_serf_session_t *session,
                const char *method, const char *url,
                serf_bucket_t *body_bkt, const char *content_type);

/**
 * This function will run the serf context in SESS until *DONE is TRUE.
 */
svn_error_t *
context_run_wait(svn_boolean_t *done,
                 ra_serf_session_t *sess,
                 apr_pool_t *pool);

/**
 * This function will feed the RESPONSE body into XMLP.  When parsing is
 * completed (i.e. an EOF is received), *DONE is set to TRUE.
 *
 * Temporary allocations are made in POOL.
 */
apr_status_t
handle_xml_parser(serf_bucket_t *response,
                  XML_Parser xmlp,
                  svn_boolean_t *done,
                  apr_pool_t *pool);

/** XML helper functions. **/

void
add_tag_buckets(serf_bucket_t *agg_bucket,
                const char *tag,
                const char *value,
                serf_bucket_alloc_t *bkt_alloc);

const char *
fetch_prop (apr_hash_t *props,
            const char *path,
            const char *ns,
            const char *name);


void
set_prop (apr_hash_t *props, const char *path,
          const char *ns, const char *name,
          const char *val, apr_pool_t *pool);

/**
 * Look up the ATTRS array for namespace definitions and add each one
 * to the NS_LIST of namespaces.
 *
 * Temporary allocations are made in POOL.
 *
 * TODO: handle scoping of namespaces
 */
void
define_ns(ns_t **ns_list,
          const char **attrs,
          apr_pool_t *pool);

/**
 * Look up NAME in the NS_LIST list for previously declared namespace
 * definitions and return a DAV_PROPS_T-tuple that has values that
 * has a lifetime tied to POOL.
 */
dav_props_t
expand_ns(ns_t *ns_list,
          const char *name,
          apr_pool_t *pool);

/**
 * look for ATTR_NAME in the attrs array and return its value.
 *
 * Returns NULL if no matching name is found.
 */
const char *
find_attr(const char **attrs,
          const char *attr_name);

void
expand_string(const char **cur, apr_size_t *cur_len,
              const char *new, apr_size_t new_len,
              apr_pool_t *pool);

/** PROPFIND-related functions **/

/**
 * This function will deliver a PROP_CTX PROPFIND request in the SESS
 * serf context for the properties listed in LOOKUP_PROPS at URL for
 * DEPTH ("0","1","infinity").
 *
 * This function will not block waiting for the response.  Instead, the
 * caller is expected to call context_run and wait for the PROP_CTX->done
 * flag to be set.
 */
svn_error_t *
deliver_props (propfind_context_t **prop_ctx,
               apr_hash_t *prop_vals,
               ra_serf_session_t *sess,
               const char *url,
               const char *depth,
               const dav_props_t *lookup_props,
               apr_pool_t *pool);

/**
 * This helper function will block until the PROP_CTX indicates that is done
 * or another error is returned.
 */
svn_error_t *
wait_for_props(propfind_context_t *prop_ctx,
               ra_serf_session_t *sess,
               apr_pool_t *pool);


/**
 * This is a blocking version of deliver_props.
 */
svn_error_t *
retrieve_props (apr_hash_t *prop_vals,
                ra_serf_session_t *sess,
                const char *url,
                const char *depth,
                const dav_props_t *props,
                apr_pool_t *pool);

/** Property walker functions */
typedef void (*walker_visitor_t)(void *baton,
                                 const void *ns, apr_ssize_t ns_len,
                                 const void *name, apr_ssize_t name_len,
                                 void *val,
                                 apr_pool_t *pool);

void
walk_all_props(apr_hash_t *props,
               const char *name,
               walker_visitor_t walker,
               void *baton,
               apr_pool_t *pool);

/** RA functions */

svn_error_t *
svn_ra_serf__get_log (svn_ra_session_t *session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool);

svn_error_t *
svn_ra_serf__do_update (svn_ra_session_t *ra_session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_update_to,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool);
