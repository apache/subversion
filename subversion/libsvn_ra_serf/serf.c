/*
 * serf.c :  entry point for ra_serf
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



#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"


static const svn_version_t *
ra_serf_version (void)
{
  SVN_VERSION_BODY;
}

#define RA_SERF_DESCRIPTION \
    N_("Access repository via WebDAV protocol through serf.")

static const char *
ra_serf_get_description (void)
{
  return _(RA_SERF_DESCRIPTION);
}

static const char * const *
ra_serf_get_schemes (apr_pool_t *pool)
{
  static const char *serf_ssl[] = { "http", "https", NULL };
  static const char *serf_no_ssl[] = { "http", NULL };

  /* TODO: Runtime detection. */
  return serf_ssl;
}

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

static serf_bucket_t *
conn_setup(apr_socket_t *sock,
           void *baton,
           apr_pool_t *pool)
{
  serf_bucket_t *bucket;
  serf_session_t *sess = baton;

  bucket = serf_bucket_socket_create(sock, sess->bkt_alloc);
  if (sess->using_ssl)
    {
      bucket = serf_bucket_ssl_decrypt_create(bucket, sess->ssl_context,
                                              sess->bkt_alloc);
    }

  return bucket;
}

static void
conn_closed (serf_connection_t *conn,
             void *closed_baton,
             apr_status_t why,
             apr_pool_t *pool)
{
  if (why)
    {
      abort();
    }
}

static apr_status_t cleanup_serf_session(void *data)
{
  serf_session_t *serf_sess = data;
  if (serf_sess->conn)
    {
      serf_connection_close(serf_sess->conn);
      serf_sess->conn = NULL;
    }
  return APR_SUCCESS;
}

static svn_error_t *
svn_ra_serf__open (svn_ra_session_t *session,
                   const char *repos_URL,
                   const svn_ra_callbacks2_t *callbacks,
                   void *callback_baton,
                   apr_hash_t *config,
                   apr_pool_t *pool)
{
  apr_status_t status;
  serf_session_t *serf_sess;
  apr_uri_t url;
  apr_sockaddr_t *address;

  serf_sess = apr_pcalloc(pool, sizeof(*serf_sess));
  apr_pool_create(&serf_sess->pool, pool);
  serf_sess->bkt_alloc = serf_bucket_allocator_create(pool, NULL, NULL);
  serf_sess->cached_props = apr_hash_make(pool);

  /* todo: reuse serf context across sessions */
  serf_sess->context = serf_context_create(pool);

  apr_uri_parse(serf_sess->pool, repos_URL, &url);
  serf_sess->repos_url = url;
  serf_sess->repos_url_str = apr_pstrdup(serf_sess->pool, repos_URL);

  if (!url.port)
    {
      url.port = apr_uri_port_of_scheme(url.scheme);
    }
  serf_sess->using_ssl = (strcasecmp(url.scheme, "https") == 0);

  /* register cleanups */
  apr_pool_cleanup_register(serf_sess->pool, serf_sess, cleanup_serf_session,
                            apr_pool_cleanup_null);

  /* fetch the DNS record for this host */
  status = apr_sockaddr_info_get(&address, url.hostname, APR_UNSPEC,
                                 url.port, 0, pool);
  if (status)
    {
      return svn_error_createf(status, NULL,
                               _("Could not lookup hostname: %s://%s"),
                               url.scheme, url.hostname);
    }

  /* go ahead and tell serf about the connection. */
  serf_sess->conn = serf_connection_create(serf_sess->context, address,
                                           conn_setup, serf_sess,
                                           conn_closed, serf_sess, pool);

  session->priv = serf_sess;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__reparent (svn_ra_session_t *ra_session,
                       const char *url,
                       apr_pool_t *pool)
{
  serf_session_t *session = ra_session->priv;
  apr_uri_t new_url;

  /* If it's the URL we already have, wave our hands and do nothing. */
  if (strcmp(session->repos_url_str, url) == 0)
    {
      return SVN_NO_ERROR;
    }

  /* Do we need to check that it's the same host and port? */
  apr_uri_parse(session->pool, url, &new_url);

  session->repos_url.path = new_url.path;
  session->repos_url_str = apr_pstrdup(pool, url);

  return SVN_NO_ERROR;
}

typedef struct {
  const char *namespace;
  const char *name;
} dav_props_t;

typedef struct ns_t {
  const char *namespace;
  const char *url;
  struct ns_t *next;
} ns_t;

static const char *
fetch_prop (apr_hash_t *props,
            const char *path,
            const char *ns,
            const char *name)
{
  apr_hash_t *path_props, *ns_props;
  const char *val = NULL;

  path_props = apr_hash_get(props, path, strlen(path));

  if (path_props)
    {
      ns_props = apr_hash_get(path_props, ns, strlen(ns));
      if (ns_props)
        {
          val = apr_hash_get(ns_props, name, strlen(name));
        }
    }

  return val;
}

static void
set_prop (apr_hash_t *props, const char *path,
          const char *ns, const char *name,
          const char *val, apr_pool_t *pool)
{
  apr_hash_t *path_props, *ns_props;
  apr_size_t path_len, ns_len, name_len;

  path_len = strlen(path);
  ns_len = strlen(ns);
  name_len = strlen(name);

  path_props = apr_hash_get(props, path, path_len);
  if (!path_props)
    {
      path_props = apr_hash_make(pool);
      apr_hash_set(props, path, path_len, path_props);

      /* todo: we know that we'll fail the next check, but fall through
       * for now for simplicity's sake.
       */
    }

  ns_props = apr_hash_get(path_props, ns, ns_len);
  if (!ns_props)
    {
      ns_props = apr_hash_make(pool);
      apr_hash_set(path_props, ns, ns_len, ns_props);
    }

  apr_hash_set(ns_props, name, name_len, val);
}

/* propfind bucket:
 *
 * <?xml version="1.0" encoding="utf-8"?>
 * <propfind xmlns="DAV:">
 *   <prop>
 *     *prop buckets*
 *   </prop>
 * </propfind>
 */

/* property bucket:
 *
 * <*propname* xmlns="*propns*"/>
 */

#define PROPFIND_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\"><prop>"
#define PROPFIND_TRAILER "</prop></propfind>"

static serf_bucket_t*
accept_response(serf_request_t *request,
                serf_bucket_t *stream,
                void *acceptor_baton,
                apr_pool_t *pool)
{
  serf_bucket_t *c;
  serf_bucket_alloc_t *bkt_alloc;

  bkt_alloc = serf_request_get_alloc(request);
  c = serf_bucket_barrier_create(stream, bkt_alloc);

  return serf_bucket_response_create(c, bkt_alloc);
}

typedef struct {
  apr_pool_t *pool;

  serf_session_t *sess;

  const char *path;

  const dav_props_t *find_props;
  apr_hash_t *ret_props;

  XML_Parser xmlp;
  ns_t *ns_list;

  svn_boolean_t in_prop;
  svn_boolean_t collect_cdata;
  const char *ns;
  const char *attr_name;
  const char *attr_val;

  svn_boolean_t done;
} propfind_context_t;

/**
 * Look up the ATTRS array for namespace definitions and add each one
 * to the NS_LIST of namespaces.
 *
 * Temporary allocations are made in POOL.
 */
static void
define_ns(ns_t **ns_list, const char **attrs, apr_pool_t *pool)
{
  const char **tmp_attrs = attrs;

  while (*tmp_attrs)
    {
      if (strncmp(*tmp_attrs, "xmlns", 5) == 0)
        {
          const char *attr, *attr_val;
          ns_t *new_ns;

          new_ns = apr_palloc(pool, sizeof(*new_ns));

          new_ns->namespace = apr_pstrdup(pool, tmp_attrs[0]+6);
          new_ns->url = apr_pstrdup(pool, tmp_attrs[1]);

          new_ns->next = *ns_list;

          *ns_list = new_ns;
        }
      tmp_attrs += 2;
    }
}

/**
 * Look up NAME in the NS_LIST list for previously declared namespace
 * definitions and return a DAV_PROPS_T-tuple that has values that
 * has a lifetime tied to POOL.
 */
static dav_props_t
expand_ns(ns_t *ns_list, const char *name, apr_pool_t *pool)
{
  char *colon;
  dav_props_t prop_name;

  colon = strchr(name, ':');
  if (colon)
    {
      char *stripped_name;
      ns_t *ns;

      stripped_name = apr_pstrmemdup(pool, name, colon-name);
      for (ns = ns_list; ns; ns = ns->next)
        {
          if (strcmp(ns->namespace, stripped_name) == 0)
            {
              prop_name.namespace = ns->url;
            }
        }
      if (!prop_name.namespace)
        {
          abort();
        }

      prop_name.name = apr_pstrdup(pool, colon + 1);
    }
  else
    {
      /* use default namespace for now */
      prop_name.namespace = "";
      prop_name.name = apr_pstrdup(pool, name);
    }

  return prop_name;
}

static void XMLCALL
start_propfind(void *userData, const char *name, const char **attrs)
{
  propfind_context_t *ctx = userData;
  dav_props_t prop_name;

  /* check for new namespaces */
  define_ns(&ctx->ns_list, attrs, ctx->pool);

  /* look up name space if present */
  prop_name = expand_ns(ctx->ns_list, name, ctx->pool);

  if (ctx->in_prop && !ctx->attr_name)
    {
      ctx->ns = prop_name.namespace;
      ctx->attr_name = prop_name.name;
      /* we want to flag the cdata handler to pick up what's next. */
      ctx->collect_cdata = TRUE;
    }

  /* check for 'prop' */
  if (!ctx->in_prop && strcmp(prop_name.name, "prop") == 0)
    {
      ctx->in_prop = TRUE;
    }
}

static void XMLCALL
end_propfind(void *userData, const char *name)
{
  propfind_context_t *ctx = userData;
  if (ctx->collect_cdata)
    {
      /* if we didn't see a CDATA element, we want the tag name. */
      if (!ctx->attr_val)
        {
          char *colon = strchr(name, ':');
          if (colon)
            {
              name = colon + 1;
            }

          /* However, if our element name is the same, we know we're empty. */
          if (strcmp(ctx->attr_name, name) == 0)
            {
              name = "";
            }

          ctx->attr_val = apr_pstrdup(ctx->sess->pool, name);
        }

      /* set the return props and update our cache too. */
      set_prop(ctx->ret_props,
               ctx->path, ctx->ns, ctx->attr_name, ctx->attr_val,
               ctx->pool);
      set_prop(ctx->sess->cached_props,
               ctx->path, ctx->ns, ctx->attr_name, ctx->attr_val,
               ctx->sess->pool);

      /* we're done with it. */
      ctx->collect_cdata = FALSE;
      ctx->attr_name = NULL;
      ctx->attr_val = NULL;
    }
  /* FIXME: destroy namespaces as we end a handler */
}

static void XMLCALL
cdata_propfind(void *userData, const char *data, int len)
{
  propfind_context_t *ctx = userData;
  if (ctx->collect_cdata)
    {
      /* FIXME append instead of setting. */
      ctx->attr_val = apr_pstrmemdup(ctx->pool, data, len);
    }

}

static apr_status_t
handle_propfind (serf_bucket_t *response,
                 void *handler_baton,
                 apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  enum XML_Status xml_status;
  propfind_context_t *ctx = handler_baton;

  status = serf_bucket_response_status(response, &sl);
  if (status)
    {
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
      abort();
    }

  while (1)
    {
      status = serf_bucket_read(response, 2048, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      /* parse the response
       *  <?xml version="1.0" encoding="utf-8"?>
       *  <D:multistatus xmlns:D="DAV:" xmlns:ns1="http://subversion.tigris.org/xmlns/dav/" xmlns:ns0="DAV:">
       *    <D:response xmlns:lp1="DAV:" xmlns:lp3="http://subversion.tigris.org/xmlns/dav/">
       *      <D:href>/repos/projects/serf/trunk/</D:href>
       *      <D:propstat>
       *        <D:prop>
       *          <lp1:version-controlled-configuration>
       *            <D:href>/repos/projects/!svn/vcc/default</D:href>
       *          </lp1:version-controlled-configuration>
       *          <lp1:resourcetype>
       *            <D:collection/>
       *          </lp1:resourcetype>
       *          <lp3:baseline-relative-path>
       *            serf/trunk
       *          </lp3:baseline-relative-path>
       *          <lp3:repository-uuid>
       *            61a7d7f5-40b7-0310-9c16-bb0ea8cb1845
       *          </lp3:repository-uuid>
       *        </D:prop>
       *        <D:status>HTTP/1.1 200 OK</D:status>
       *      </D:propstat>
       *    </D:response>
       *  </D:multistatus>
       */
      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR)
        {
          abort();
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          if (xml_status == XML_STATUS_ERROR)
            {
              abort();
            }

          XML_ParserFree(ctx->xmlp);

          ctx->done = TRUE;
          return APR_EOF;
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }

      /* feed me! */
    }
  /* not reached */
}

static svn_error_t *
retrieve_props (apr_hash_t **prop_vals,
                serf_session_t *sess,
                const char *url,
                const char *depth,
                const dav_props_t *props,
                apr_pool_t *pool)
{
  const dav_props_t *prop;
  apr_hash_t *ret_props;
  serf_bucket_t *props_bkt, *tmp, *req_bkt, *hdrs_bkt;
  serf_request_t *request;
  propfind_context_t *prop_ctx;
  apr_status_t status;

  ret_props = apr_hash_make(pool);
  props_bkt = NULL;

  /* check to see if we have any of this information cached */
  prop = props;
  while (prop && prop->namespace)
    {
      const char *val;

      val = fetch_prop(sess->cached_props, url, prop->namespace,
                       prop->name);
      if (val)
        {
          set_prop(ret_props, url, prop->namespace, prop->name, val, pool);
        }
      else
        {
          if (!props_bkt)
            {
              props_bkt = serf_bucket_aggregate_create(sess->bkt_alloc);
            }

          /* <*propname* xmlns="*propns*" /> */
          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, sess->bkt_alloc);
          serf_bucket_aggregate_append(props_bkt, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(prop->name, sess->bkt_alloc);
          serf_bucket_aggregate_append(props_bkt, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" xmlns=\"",
                                              sizeof(" xmlns=\"")-1,
                                              sess->bkt_alloc);
          serf_bucket_aggregate_append(props_bkt, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(prop->namespace, sess->bkt_alloc);
          serf_bucket_aggregate_append(props_bkt, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"/>", sizeof("\"/>")-1,
                                              sess->bkt_alloc);
          serf_bucket_aggregate_append(props_bkt, tmp);
        }

      prop++;
    }

  /* We satisfied all of the properties with our session cache.  Woo-hoo. */
  if (!props_bkt)
    {
      *prop_vals = ret_props;
      return SVN_NO_ERROR;
    }

  /* Cache didn't hit for everything, so generate a request now. */

  /* TODO: programatically add in the namespaces */
  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_HEADER,
                                      sizeof(PROPFIND_HEADER)-1,
                                      sess->bkt_alloc);
  serf_bucket_aggregate_prepend(props_bkt, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_TRAILER,
                                      sizeof(PROPFIND_TRAILER)-1,
                                      sess->bkt_alloc);
  serf_bucket_aggregate_append(props_bkt, tmp);

  /* create and deliver request */
  request = serf_connection_request_create(sess->conn);

  req_bkt = serf_bucket_request_create("PROPFIND", url, props_bkt,
                                       serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", sess->repos_url.hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "ra_serf");
  serf_bucket_headers_setn(hdrs_bkt, "Depth", depth);
  serf_bucket_headers_setn(hdrs_bkt, "Content-Type", "text/xml");
  /* serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip"); */

  /* Create the propfind context. */
  prop_ctx = apr_pcalloc(pool, sizeof(*prop_ctx));
  prop_ctx->pool = pool;
  prop_ctx->path = url;
  prop_ctx->find_props = props;
  prop_ctx->ret_props = ret_props;
  prop_ctx->done = FALSE;
  prop_ctx->sess = sess;

  prop_ctx->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(prop_ctx->xmlp, prop_ctx);
  XML_SetElementHandler(prop_ctx->xmlp, start_propfind, end_propfind);
  XML_SetCharacterDataHandler(prop_ctx->xmlp, cdata_propfind);

  serf_request_deliver(request, req_bkt,
                       accept_response, sess,
                       handle_propfind, prop_ctx);

  while (!prop_ctx->done)
    {
      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, "Error retrieving PROPFIND");
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
    }

  *prop_vals = ret_props;

  return SVN_NO_ERROR;
}

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

static svn_error_t *
svn_ra_serf__get_latest_revnum (svn_ra_session_t *ra_session,
                                svn_revnum_t *latest_revnum,
                                apr_pool_t *pool)
{
  apr_hash_t *props, *ns_props;
  serf_session_t *session = ra_session->priv;
  const char *vcc_url, *baseline_url, *version_name;

  SVN_ERR(retrieve_props(&props, session, session->repos_url.path, "0",
                         base_props, pool));

  vcc_url = fetch_prop(props, session->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Using the version-controlled-configuration, fetch the checked-in prop. */
  SVN_ERR(retrieve_props(&props, session, vcc_url, "0", checked_in_props,
                         pool));

  baseline_url = fetch_prop(props, vcc_url,
                            "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  /* Using the checked-in property, fetch:
   *    baseline-connection *and* version-name
   */
  SVN_ERR(retrieve_props(&props, session, baseline_url, "0",
                         baseline_props, pool));

  version_name = fetch_prop(props, baseline_url, "DAV:", "version-name");

  if (!version_name)
    {
      abort();
    }

  *latest_revnum = SVN_STR_TO_REV(version_name);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_dated_revision (svn_ra_session_t *session,
                                 svn_revnum_t *revision,
                                 apr_time_t tm,
                                 apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__change_rev_prop (svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__rev_proplist (svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t **props,
                           apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__rev_prop (svn_ra_session_t *session,
                       svn_revnum_t rev,
                       const char *name,
                       svn_string_t **value,
                       apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_commit_editor (svn_ra_session_t *session,
                                const svn_delta_editor_t **editor,
                                void **edit_baton,
                                const char *log_msg,
                                svn_commit_callback2_t callback,
                                void *callback_baton,
                                apr_hash_t *lock_tokens,
                                svn_boolean_t keep_locks,
                                apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_file (svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       svn_stream_t *stream,
                       svn_revnum_t *fetched_rev,
                       apr_hash_t **props,
                       apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_dir (svn_ra_session_t *session,
                      const char *path,
                      svn_revnum_t revision,
                      apr_uint32_t dirent_fields,
                      apr_hash_t **dirents,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      apr_pool_t *pool)
{
  abort();
}

typedef struct {
  serf_session_t *sess;

  const char *target;
  svn_revnum_t target_rev;

  svn_boolean_t recurse;

  const svn_delta_editor_t *update_editor;
  void *update_baton;

  serf_bucket_t *buckets;

  XML_Parser xmlp;

  svn_boolean_t done;

} report_context_t;

static void add_tag_buckets(serf_bucket_t *agg_bucket, const char *tag,
                            const char *value, serf_bucket_alloc_t *bkt_alloc)
{
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(value, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</", 2, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

static void XMLCALL
start_report(void *userData, const char *name, const char **attrs)
{
  report_context_t *ctx = userData;

  abort();
}

static void XMLCALL
end_report(void *userData, const char *name)
{
  report_context_t *ctx = userData;

  abort();
}

static void XMLCALL
cdata_report(void *userData, const char *data, int len)
{
  report_context_t *ctx = userData;

  abort();
}

static apr_status_t
handle_report (serf_bucket_t *response,
                 void *handler_baton,
                 apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  enum XML_Status xml_status;
  report_context_t *ctx = handler_baton;

  status = serf_bucket_response_status(response, &sl);
  if (status)
    {
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
      abort();
    }

  while (1)
    {
      status = serf_bucket_read(response, 2048, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      /* parse the response */
      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR)
        {
          abort();
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          if (xml_status == XML_STATUS_ERROR)
            {
              abort();
            }

          XML_ParserFree(ctx->xmlp);

          ctx->done = TRUE;
          return APR_EOF;
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }

      /* feed me! */
    }
  /* not reached */
}

static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:entry rev=\"",
                                      sizeof("<S:entry rev=\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(apr_ltoa(pool, revision),
                                  report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", sizeof("\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  if (lock_token)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" lock-token=\"",
                                          sizeof(" lock-token=\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(lock_token,
                                      report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", sizeof("\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);
    }

  if (start_empty)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" start-empty=\"true\"",
                                          sizeof(" start-empty=\"true\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", sizeof(">")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(path, report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:entry>",
                                      sizeof("</S:entry>")-1,
                                      report->sess->bkt_alloc);

  serf_bucket_aggregate_append(report->buckets, tmp);
  return APR_SUCCESS;
}

static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static const dav_props_t vcc_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  NULL
};

static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  serf_session_t *sess = report->sess;
  serf_request_t *request;
  serf_bucket_t *req_bkt, *hdrs_bkt, *tmp;
  const char *vcc_url;
  apr_hash_t *props;
  apr_status_t status;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:update-report>",
                                      sizeof("</S:update-report>")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  SVN_ERR(retrieve_props(&props, sess, sess->repos_url.path, "0",
                         vcc_props, pool));

  vcc_url = fetch_prop(props, sess->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* create and deliver request */
  request = serf_connection_request_create(sess->conn);

  req_bkt = serf_bucket_request_create("REPORT", vcc_url, report->buckets,
                                       serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", sess->repos_url.hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "ra_serf");
  serf_bucket_headers_setn(hdrs_bkt, "Content-Type", "text/xml");
  /* serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip"); */

  report->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(report->xmlp, report);
  XML_SetElementHandler(report->xmlp, start_report, end_report);
  XML_SetCharacterDataHandler(report->xmlp, cdata_report);

  serf_request_deliver(request, req_bkt,
                       accept_response, sess,
                       handle_report, report);

  while (!report->done)
    {
      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, _("Error retrieving REPORT"));
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static const svn_ra_reporter2_t ra_serf_reporter = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};

static svn_error_t *
svn_ra_serf__do_update (svn_ra_session_t *ra_session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_update_to,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
  report_context_t *report;
  serf_session_t *session = ra_session->priv;
  serf_bucket_t *tmp;

  report = apr_palloc(pool, sizeof(*report));
  report->sess = ra_session->priv;
  report->target = update_target;
  report->target_rev = revision_to_update_to;
  report->recurse = recurse;
  report->update_editor = update_editor;
  report->update_baton = update_baton;

  *reporter = &ra_serf_reporter;
  *report_baton = report;

  report->buckets = serf_bucket_aggregate_create(report->sess->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:update-report xmlns:S=\"",
                                      sizeof("<S:update-report xmlns:S=\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(SVN_XML_NAMESPACE,
                                      sizeof(SVN_XML_NAMESPACE)-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\">",
                                      sizeof("\">")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  add_tag_buckets(report->buckets,
                  "S:src-path", report->sess->repos_url.path,
                  report->sess->bkt_alloc);

  if (SVN_IS_VALID_REVNUM(revision_to_update_to))
    {
      add_tag_buckets(report->buckets,
                      "S:target-revision",
                      apr_ltoa(pool, revision_to_update_to),
                      report->sess->bkt_alloc);
    }

  if (*update_target)
    {
      add_tag_buckets(report->buckets,
                      "S:update-target", update_target,
                      report->sess->bkt_alloc);
    }

  if (!recurse)
    {
      add_tag_buckets(report->buckets,
                      "S:recursive", "no",
                      report->sess->bkt_alloc);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__do_switch (svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_switch_to,
                        const char *switch_target,
                        svn_boolean_t recurse,
                        const char *switch_url,
                        const svn_delta_editor_t *switch_editor,
                        void *switch_baton,
                        apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__do_status (svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        const char *status_target,
                        svn_revnum_t revision,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *status_editor,
                        void *status_baton,
                        apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__do_diff (svn_ra_session_t *session,
                      const svn_ra_reporter2_t **reporter,
                      void **report_baton,
                      svn_revnum_t revision,
                      const char *diff_target,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t text_deltas,
                      const char *versus_url,
                      const svn_delta_editor_t *diff_editor,
                      void *diff_baton,
                      apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_log (svn_ra_session_t *session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool)
{
  abort();
}

static const dav_props_t check_path_props[] =
{
  { "DAV:", "resourcetype" },
  NULL
};

static svn_error_t *
svn_ra_serf__check_path (svn_ra_session_t *ra_session,
                         const char *rel_path,
                         svn_revnum_t revision,
                         svn_node_kind_t *kind,
                         apr_pool_t *pool)
{
  serf_session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *path, *res_type;

  path = session->repos_url.path;

  /* If we have a relative path, append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  SVN_ERR(retrieve_props(&props, session, path, "0", check_path_props, pool));
  res_type = fetch_prop(props, path, "DAV:", "resourcetype");

  if (!res_type)
    {
      /* if the file isn't there, return none; but let's abort for now. */
      abort();
      *kind = svn_node_none;
    }
  else if (strcmp(res_type, "collection") == 0)
    {
      *kind = svn_node_dir;
    }
  else
    {
      *kind = svn_node_file;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__stat (svn_ra_session_t *session,
                   const char *path,
                   svn_revnum_t revision,
                   svn_dirent_t **dirent,
                   apr_pool_t *pool)
{
  abort();
}

static const dav_props_t uuid_props[] =
{
  { SVN_DAV_PROP_NS_DAV, "repository-uuid" },
  NULL
};

static svn_error_t *
svn_ra_serf__get_uuid (svn_ra_session_t *ra_session,
                       const char **uuid,
                       apr_pool_t *pool)
{
  serf_session_t *session = ra_session->priv;
  apr_hash_t *props;

  SVN_ERR(retrieve_props(&props, session, session->repos_url.path, "0",
                         uuid_props, pool));
  *uuid = fetch_prop(props, session->repos_url.path,
                     SVN_DAV_PROP_NS_DAV, "repository-uuid");

  if (!*uuid)
    {
      abort();
    }

  return SVN_NO_ERROR;
}

static const dav_props_t repos_root_props[] =
{
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path" },
  NULL
};

static svn_error_t *
svn_ra_serf__get_repos_root (svn_ra_session_t *ra_session,
                             const char **url,
                             apr_pool_t *pool)
{
  serf_session_t *session = ra_session->priv;

  if (!session->repos_root_str)
    {
      const char *baseline_url, *root_path;
      svn_stringbuf_t *url_buf;
      apr_hash_t *props;

      SVN_ERR(retrieve_props(&props, session, session->repos_url.path, "0",
                             repos_root_props, pool));
      baseline_url = fetch_prop(props, session->repos_url.path,
                                SVN_DAV_PROP_NS_DAV, "baseline-relative-path");

      if (!baseline_url)
        {
          abort();
        }

      /* If we see baseline_url as "", we're the root.  Otherwise... */
      if (*baseline_url == '\0')
        {
          root_path = session->repos_url.path;
          session->repos_root = session->repos_url;
          session->repos_root_str = session->repos_url_str;
        }
      else
        {
          url_buf = svn_stringbuf_create(session->repos_url.path, pool);
          svn_path_remove_components(url_buf,
                                     svn_path_component_count(baseline_url));
          root_path = apr_pstrdup(session->pool, url_buf->data);

          /* Now that we have the root_path, recreate the root_url. */
          session->repos_root = session->repos_url;
          session->repos_root.path = (char*)root_path;
          session->repos_root_str = apr_uri_unparse(session->pool,
                                                    &session->repos_root, 0);
        }
    }

  *url = session->repos_root_str;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_locations (svn_ra_session_t *session,
                            apr_hash_t **locations,
                            const char *path,
                            svn_revnum_t peg_revision,
                            apr_array_header_t *location_revisions,
                            apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_file_revs (svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_ra_file_rev_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__lock (svn_ra_session_t *session,
                   apr_hash_t *path_revs,
                   const char *comment,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func,
                   void *lock_baton,
                   apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__unlock (svn_ra_session_t *session,
                     apr_hash_t *path_tokens,
                     svn_boolean_t force,
                     svn_ra_lock_callback_t lock_func,
                     void *lock_baton,
                     apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_lock (svn_ra_session_t *session,
                       svn_lock_t **lock,
                       const char *path,
                       apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_locks (svn_ra_session_t *session,
                        apr_hash_t **locks,
                        const char *path,
                        apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__replay (svn_ra_session_t *session,
                     svn_revnum_t revision,
                     svn_revnum_t low_water_mark,
                     svn_boolean_t text_deltas,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool)
{
  abort();
}

static const svn_ra__vtable_t serf_vtable = {
  ra_serf_version,
  ra_serf_get_description,
  ra_serf_get_schemes,
  svn_ra_serf__open,
  svn_ra_serf__reparent,
  svn_ra_serf__get_latest_revnum,
  svn_ra_serf__get_dated_revision,
  svn_ra_serf__change_rev_prop,
  svn_ra_serf__rev_proplist,
  svn_ra_serf__rev_prop,
  svn_ra_serf__get_commit_editor,
  svn_ra_serf__get_file,
  svn_ra_serf__get_dir,
  svn_ra_serf__do_update,
  svn_ra_serf__do_switch,
  svn_ra_serf__do_status,
  svn_ra_serf__do_diff,
  svn_ra_serf__get_log,
  svn_ra_serf__check_path,
  svn_ra_serf__stat,
  svn_ra_serf__get_uuid,
  svn_ra_serf__get_repos_root,
  svn_ra_serf__get_locations,
  svn_ra_serf__get_file_revs,
  svn_ra_serf__lock,
  svn_ra_serf__unlock,
  svn_ra_serf__get_lock,
  svn_ra_serf__get_locks,
  svn_ra_serf__replay,
};

svn_error_t *
svn_ra_serf__init (const svn_version_t *loader_version,
                   const svn_ra__vtable_t **vtable,
                   apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };

  SVN_ERR(svn_ver_check_list(ra_serf_version(), checklist));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf
        (SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_serf"),
         loader_version->major);
    }

  *vtable = &serf_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for pre-1.2 subversions.  Needed? */
#define NAME "ra_serf"
#define DESCRIPTION RA_SERF_DESCRIPTION
#define VTBL serf_vtable
#define INITFUNC svn_ra_serf__init
#define COMPAT_INITFUNC svn_ra_serf_init
#include "../libsvn_ra/wrapper_template.h"
