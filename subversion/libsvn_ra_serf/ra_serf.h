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


typedef struct {
  apr_pool_t *pool;
  serf_bucket_alloc_t *bkt_alloc;

  serf_context_t *context;
  serf_ssl_context_t *ssl_context;

  serf_connection_t *conn;

  svn_boolean_t using_ssl;

  apr_uri_t repos_url;
  const char *repos_url_str;

  apr_uri_t repos_root;
  const char *repos_root_str;

  apr_hash_t *cached_props;

} serf_session_t;

typedef struct {
  const char *namespace;
  const char *name;
} dav_props_t;

typedef struct ns_t {
  const char *namespace;
  const char *url;
  struct ns_t *next;
} ns_t;

/**
 * This structure represents a pending PROPFIND response.
 */
typedef struct propfind_context_t {
  /* pool to issue allocations from */
  apr_pool_t *pool;

  /* associated serf session */
  serf_session_t *sess;

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

/**
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
    OPEN_DIR,
    ADD_FILE,
    ADD_DIR,
    PROP,
    IGNORE_PROP_NAME,
    NEED_PROP_NAME,
} report_state_e;

/**
 * This structure represents the information for a directory.
 */
typedef struct report_dir_t
{
  /* The enclosing parent.
   *
   * This value is NULL when we are the root.
   */
  struct report_dir_t *parent_dir;

  /* the containing directory name */
  const char *name;

  /* temporary path buffer for this directory. */
  svn_stringbuf_t *name_buf;

  /* controlling dir baton */
  void *dir_baton;

  /* How many references to this directory do we still have open? */
  apr_size_t ref_count;

  /* The next directory we have open. */
  struct report_dir_t *next;
} report_dir_t;

/**
 * This structure represents the information for a file.
 *
 * This structure is created as we parse the REPORT response and
 * once the element is completed, we create a report_fetch_t structure
 * to give to serf to retrieve this file.
 */
typedef struct report_info_t
{
  /* The enclosing directory. */
  report_dir_t *dir;

  /* the name of the file. */
  const char *file_name;

  /* file name buffer */
  svn_stringbuf_t *file_name_buf;

  /* the canonical url for this path. */
  const char *file_url;

  /* hashtable that stores all of the properties for this path. */
  apr_hash_t *file_props;

  /* pool passed to update->add_file, etc. */
  apr_pool_t *editor_pool;

  /* controlling file_baton and textdelta handler */
  void *file_baton;
  svn_txdelta_window_handler_t textdelta;
  void *textdelta_baton;

  /* the in-progress property being parsed */
  const char *prop_ns;
  const char *prop_name;
  const char *prop_val;
  apr_size_t prop_val_len;
} report_info_t;

/**
 * This file structure represents a single file to fetch with its
 * associated Serf session.
 */
typedef struct report_fetch_t {
  /* Our pool. */
  apr_pool_t *pool;

  /* The session we should use to fetch the file. */
  serf_session_t *sess;

  /* Stores the information for the file we want to fetch. */
  report_info_t *info;

  /* Our update editor and baton. */
  const svn_delta_editor_t *update_editor;
  void *update_baton;

  /* Are we done fetching this file? */
  svn_boolean_t done;

  /* The next fetch we have open. */
  struct report_fetch_t *next;
} report_fetch_t;

typedef struct report_state_list_t {
   /* The current state that we are in now. */
  report_state_e state;

  /* Information */
  report_info_t *info;

  /* The previous state we were in. */
  struct report_state_list_t *prev;
} report_state_list_t;

typedef struct {
  serf_session_t *sess;

  const char *target;
  svn_revnum_t target_rev;

  svn_boolean_t recurse;

  const svn_delta_editor_t *update_editor;
  void *update_baton;

  serf_bucket_t *buckets;

  XML_Parser xmlp;
  ns_t *ns_list;

  /* our base rev. */
  svn_revnum_t base_rev;

  /* could allocate this as an array rather than a linked list. */
  report_state_list_t *state;
  report_state_list_t *free_state;

  /* pending GET requests */
  report_fetch_t *active_fetches;

  /* pending PROPFIND requests */
  propfind_context_t *active_propfinds;

  /* potentially pending dir baton closes */
  report_dir_t *pending_dir_close;

  /* free list of info structures */
  report_info_t *free_info;

  svn_boolean_t done;

} report_context_t;

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

svn_error_t *
context_run_wait(svn_boolean_t *done,
                 serf_session_t *sess,
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
               serf_session_t *sess,
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
               serf_session_t *sess,
               apr_pool_t *pool);


/**
 * This is a blocking version of deliver_props.
 */
svn_error_t *
retrieve_props (apr_hash_t *prop_vals,
                serf_session_t *sess,
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
svn_ra_serf__do_update (svn_ra_session_t *ra_session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_update_to,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool);
