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
 * Look up the ATTRS array for namespace definitions and add each one
 * to the NS_LIST of namespaces.
 *
 * Temporary allocations are made in POOL.
 *
 * TODO: handle scoping of namespaces
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

/**
 * look for ATTR_NAME in the attrs array and return its value.
 *
 * Returns NULL if no matching name is found.
 */
static const char *
find_attr(const char **attrs, const char *attr_name)
{
  const char *attr_val = NULL;
  const char **tmp_attrs = attrs;

  while (*tmp_attrs)
    {
      if (strcmp(*tmp_attrs, attr_name) == 0)
        {
          attr_val = attrs[1];
          break;
        }
      tmp_attrs += 2;
    }

  return attr_val;
}

/**
 * Expat callback invoked on a start element tag for a PROPFIND response.
 */
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

/**
 * Expat callback invoked on an end element tag for a PROPFIND response.
 */
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

static void
expand_string(const char **cur, apr_size_t *cur_len,
              const char *new, apr_size_t new_len,
              apr_pool_t *pool)
{
  if (!*cur)
    {
      *cur = apr_pstrmemdup(pool, new, new_len);
      *cur_len = new_len;
    }
  else
    {
      char *new_cur;

      /* append the data we received before. */
      new_cur = apr_palloc(pool, *cur_len+new_len+1);

      memcpy(new_cur, *cur, *cur_len);
      memcpy(new_cur + *cur_len, new, new_len);

      /* NULL-term our new string */
      new_cur[*cur_len + new_len] = '\0';

      /* update our length */
      *cur_len += new_len;
      *cur = new_cur;
    }
}

/**
 * Expat callback invoked on CDATA elements in a PROPFIND response.
 *
 * This callback can be called multiple times.
 */
static void XMLCALL
cdata_propfind(void *userData, const char *data, int len)
{
  propfind_context_t *ctx = userData;
  if (ctx->collect_cdata)
    {
      expand_string(&ctx->attr_val, &ctx->attr_val_len,
                    data, len, ctx->pool);
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

#define PROPFIND_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\">"
#define PROPFIND_TRAILER "</propfind>"

/**
 * This function will deliver a PROP_CTX PROPFIND request in the SESS
 * serf context for the properties listed in LOOKUP_PROPS at URL for
 * DEPTH ("0","1","infinity").
 *
 * This function will not block waiting for the response.  Instead, the
 * caller is expected to call context_run and wait for the PROP_CTX->done
 * flag to be set.
 */
static svn_error_t *
deliver_props (propfind_context_t **prop_ctx,
               apr_hash_t *prop_vals,
               serf_session_t *sess,
               const char *url,
               const char *depth,
               const dav_props_t *lookup_props,
               apr_pool_t *pool)
{
  const dav_props_t *prop;
  apr_hash_t *ret_props = prop_vals;
  serf_bucket_t *props_bkt, *tmp, *req_bkt, *hdrs_bkt;
  serf_request_t *request;
  propfind_context_t *new_prop_ctx;
  apr_status_t status;
  svn_boolean_t requested_allprop = FALSE;

  props_bkt = NULL;

  /* check to see if we have any of this information cached */
  prop = lookup_props;
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

          /* special case the allprop case. */
          if (strcmp(prop->name, "allprop") == 0)
            {
              requested_allprop = TRUE;
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
      *prop_ctx = NULL;
      return SVN_NO_ERROR;
    }

  /* Cache didn't hit for everything, so generate a request now. */

  /* TODO: programatically add in the namespaces */

  /* If we're not doing an allprop, add <prop> tags. */
  if (requested_allprop == FALSE)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<prop>",
                                          sizeof("<prop>")-1,
                                          sess->bkt_alloc);
      serf_bucket_aggregate_prepend(props_bkt, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_HEADER,
                                      sizeof(PROPFIND_HEADER)-1,
                                      sess->bkt_alloc);

  serf_bucket_aggregate_prepend(props_bkt, tmp);

  if (requested_allprop == FALSE)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</prop>",
                                          sizeof("</prop>")-1,
                                          sess->bkt_alloc);
      serf_bucket_aggregate_append(props_bkt, tmp);
    }

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
  new_prop_ctx = apr_pcalloc(pool, sizeof(*new_prop_ctx));
  new_prop_ctx->pool = pool;
  new_prop_ctx->path = url;
  new_prop_ctx->find_props = lookup_props;
  new_prop_ctx->ret_props = ret_props;
  new_prop_ctx->done = FALSE;
  new_prop_ctx->sess = sess;

  new_prop_ctx->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(new_prop_ctx->xmlp, new_prop_ctx);
  XML_SetElementHandler(new_prop_ctx->xmlp, start_propfind, end_propfind);
  XML_SetCharacterDataHandler(new_prop_ctx->xmlp, cdata_propfind);

  serf_request_deliver(request, req_bkt,
                       accept_response, sess,
                       handle_propfind, new_prop_ctx);

  *prop_ctx = new_prop_ctx;

  return SVN_NO_ERROR;
}

/**
 * This helper function will block until the PROP_CTX indicates that is done
 * or another error is returned.
 */
static svn_error_t *
wait_for_props(propfind_context_t *prop_ctx,
               serf_session_t *sess,
               apr_pool_t *pool)
{
  apr_status_t status;

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

  return SVN_NO_ERROR;
}

/**
 * This is a blocking version of deliver_props.
 */
static svn_error_t *
retrieve_props (apr_hash_t *prop_vals,
                serf_session_t *sess,
                const char *url,
                const char *depth,
                const dav_props_t *props,
                apr_pool_t *pool)
{
  propfind_context_t *prop_ctx;

  SVN_ERR(deliver_props(&prop_ctx, prop_vals, sess, url, depth, props, pool));
  if (prop_ctx)
    {
      SVN_ERR(wait_for_props(prop_ctx, sess, pool));
    }
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

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->repos_url.path, "0",
                         base_props, pool));

  vcc_url = fetch_prop(props, session->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Using the version-controlled-configuration, fetch the checked-in prop. */
  SVN_ERR(retrieve_props(props, session, vcc_url, "0", checked_in_props,
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
  SVN_ERR(retrieve_props(props, session, baseline_url, "0",
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

static void push_state(report_context_t *ctx, report_state_e state)
{
  report_state_list_t *new_state;

  if (!ctx->free_state)
    {
      new_state = apr_palloc(ctx->sess->pool, sizeof(*ctx->state));
    }
  else
    {
      new_state = ctx->free_state;
      ctx->free_state = ctx->free_state->prev;
    }
  new_state->state = state;

  if (state == OPEN_DIR)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      new_state->info->file_props = apr_hash_make(ctx->sess->pool);
      /* Create our root state now. */
      new_state->info->dir = apr_palloc(ctx->sess->pool,
                                        sizeof(*new_state->info->dir));
      new_state->info->dir->parent_dir = NULL;
      new_state->info->dir->ref_count = 0;
    }
  else if (state == ADD_DIR)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      new_state->info->file_props = ctx->state->info->file_props;
      new_state->info->dir =
          apr_palloc(ctx->sess->pool, sizeof(*new_state->info->dir));
      new_state->info->dir->parent_dir = ctx->state->info->dir;
      new_state->info->dir->parent_dir->ref_count++;
      new_state->info->dir->ref_count = 0;
    }
  else if (state == ADD_FILE)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      new_state->info->file_props = ctx->state->info->file_props;
      /* Point at our parent's directory state. */
      new_state->info->dir = ctx->state->info->dir;
      new_state->info->dir->ref_count++;
    }
  /* if we have state info from our parent, reuse it. */
  else if (ctx->state && ctx->state->info)
    {
      new_state->info = ctx->state->info;
    }
  else
    {
      abort();
    }

  /* Add it to the state chain. */
  new_state->prev = ctx->state;
  ctx->state = new_state;
}

static void pop_state(report_context_t *ctx)
{
  report_state_list_t *free_state;
  free_state = ctx->state;
  /* advance the current state */
  ctx->state = ctx->state->prev;
  free_state->prev = ctx->free_state;
  ctx->free_state = free_state;
  ctx->free_state->info = NULL;
}

typedef void (*walker_visitor_t)(void *baton,
                                 const void *ns, apr_ssize_t ns_len,
                                 const void *name, apr_ssize_t name_len,
                                 void *val,
                                 apr_pool_t *pool);

static void walk_all_props(apr_hash_t *props, const char *name,
                           walker_visitor_t walker,
                           void *baton,
                           apr_pool_t *pool)
{
  apr_hash_index_t *ns_hi;
  apr_hash_t *path_props;

  path_props = apr_hash_get(props, name, strlen(name));

  for (ns_hi = apr_hash_first(pool, path_props); ns_hi;
       ns_hi = apr_hash_next(ns_hi))
    {
      void *ns_val;
      const void *ns_name;
      apr_ssize_t ns_len;
      apr_hash_index_t *name_hi;
      apr_hash_this(ns_hi, &ns_name, &ns_len, &ns_val);
      for (name_hi = apr_hash_first(pool, ns_val); name_hi;
           name_hi = apr_hash_next(name_hi))
        {
          void *prop_val;
          const void *prop_name;
          apr_ssize_t prop_len;
          apr_hash_index_t *prop_hi;

          apr_hash_this(name_hi, &prop_name, &prop_len, &prop_val);
          /* use a subpool? */
          walker(baton, ns_name, ns_len, prop_name, prop_len, prop_val, pool);
        }
    }
}

static void
set_file_props(void *baton,
               const void *ns, apr_ssize_t ns_len,
               const void *name, apr_ssize_t name_len,
               void *val,
               apr_pool_t *pool)
{
  report_fetch_t *fetch = baton;
  const char *prop_name;
  svn_string_t *prop_val;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    prop_name = name;
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    prop_name = apr_pstrcat(pool, SVN_PROP_PREFIX, name, NULL);
  else if (strcmp(name, "version-name") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_REV;
  else if (strcmp(name, "creationdate") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_DATE;
  else if (strcmp(name, "creator-displayname") == 0)
    prop_name = SVN_PROP_ENTRY_LAST_AUTHOR;
  else if (strcmp(name, "repository-uuid") == 0)
    prop_name = SVN_PROP_ENTRY_UUID;
  else
    {
      /* do nothing for now? */
      return;
    }

  fetch->update_editor->change_file_prop(fetch->info->file_baton,
                                         prop_name,
                                         svn_string_create(val, pool),
                                         pool);
}

static apr_status_t
handle_fetch (serf_bucket_t *response,
              void *handler_baton,
              apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  report_fetch_t *fetch_ctx = handler_baton;

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
      svn_txdelta_window_t delta_window = { 0 };
      svn_txdelta_op_t delta_op;
      svn_string_t window_data;

      status = serf_bucket_read(response, 2048, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      /* construct the text delta window. */
      if (len)
        {
          window_data.data = data;
          window_data.len = len;

          delta_op.action_code = svn_txdelta_new;
          delta_op.offset = 0;
          delta_op.length = len;

          delta_window.tview_len = len;
          delta_window.num_ops = 1;
          delta_window.ops = &delta_op;
          delta_window.new_data = &window_data;

          /* write to the file located in the info. */
          fetch_ctx->info->textdelta(&delta_window,
                                     fetch_ctx->info->textdelta_baton);
        }

      if (APR_STATUS_IS_EOF(status))
        {
          fetch_ctx->info->textdelta(NULL,
                                     fetch_ctx->info->textdelta_baton);

          /* set all of the properties we received */
          walk_all_props(fetch_ctx->info->file_props,
                         fetch_ctx->info->file_url,
                         set_file_props,
                         fetch_ctx, fetch_ctx->sess->pool);

          fetch_ctx->update_editor->close_file(fetch_ctx->info->file_baton,
                                               NULL,
                                               fetch_ctx->sess->pool);

          fetch_ctx->done = TRUE;
          return APR_EOF;
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
    }
  /* not reached */

  abort();
}

static const dav_props_t all_props[] =
{
  { "DAV:", "allprop" },
  NULL
};

static void fetch_file(report_context_t *ctx, report_info_t *info)
{
  const char *checked_in_url, *checksum;
  serf_request_t *request;
  serf_bucket_t *req_bkt, *hdrs_bkt;
  report_fetch_t *fetch_ctx;
  propfind_context_t *prop_ctx;
  apr_hash_t *props;

  /* go fetch info->file_name from DAV:checked-in */
  checked_in_url = fetch_prop(info->file_props, info->file_name,
                         "DAV:", "checked-in");

  if (!checked_in_url)
    {
      abort();
    }

  info->file_url = checked_in_url;

  /* open the file and  */
  /* FIXME subpool */
  ctx->update_editor->add_file(info->file_name, info->dir->dir_baton,
                               NULL, SVN_INVALID_REVNUM, ctx->sess->pool,
                               &info->file_baton);

  ctx->update_editor->apply_textdelta(info->file_baton,
                                      NULL, ctx->sess->pool,
                                      &info->textdelta,
                                      &info->textdelta_baton);

  /* First, create the PROPFIND to retrieve the properties. */
  deliver_props(&prop_ctx, info->file_props, ctx->sess,
                info->file_url, "0", all_props, ctx->sess->pool);

  if (!prop_ctx)
    {
      abort();
    }

  prop_ctx->next = ctx->active_propfinds;
  ctx->active_propfinds = prop_ctx;

  /* create and deliver GET request */
  request = serf_connection_request_create(ctx->sess->conn);

  req_bkt = serf_bucket_request_create("GET", checked_in_url, NULL,
                                       serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", ctx->sess->repos_url.hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "ra_serf");
  /* serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip"); */

  /* Create the fetch context. */
  fetch_ctx = apr_pcalloc(ctx->sess->pool, sizeof(*fetch_ctx));
  fetch_ctx->pool = ctx->sess->pool;
  fetch_ctx->info = info;
  fetch_ctx->done = FALSE;
  fetch_ctx->sess = ctx->sess;
  fetch_ctx->update_editor = ctx->update_editor;
  fetch_ctx->update_baton = ctx->update_baton;

  serf_request_deliver(request, req_bkt,
                       accept_response, ctx->sess,
                       handle_fetch, fetch_ctx);

  /* add the GET to our active list. */
  fetch_ctx->next = ctx->active_fetches;
  ctx->active_fetches = fetch_ctx;
}

static void XMLCALL
start_report(void *userData, const char *name, const char **attrs)
{
  report_context_t *ctx = userData;
  dav_props_t prop_name;

  /* check for new namespaces */
  define_ns(&ctx->ns_list, attrs, ctx->sess->pool);

  /* look up name space if present */
  prop_name = expand_ns(ctx->ns_list, name, ctx->sess->pool);

  if (!ctx->state && strcmp(prop_name.name, "open-directory") == 0)
    {
      const char *rev = NULL;

      rev = find_attr(attrs, "rev");

      if (!rev)
        {
          abort();
        }

      push_state(ctx, OPEN_DIR);

      ctx->base_rev = apr_atoi64(rev);

      /* FIXME subpool */
      ctx->update_editor->open_root(ctx->update_baton, ctx->base_rev,
                                    ctx->sess->pool,
                                    &ctx->state->info->dir->dir_baton);
      ctx->state->info->dir->name_buf = svn_stringbuf_create("",
                                                             ctx->sess->pool);
      ctx->state->info->file_name = "";
    }
  else if (ctx->state && 
           (ctx->state->state == OPEN_DIR || ctx->state->state == ADD_DIR) &&
           strcmp(prop_name.name, "add-directory") == 0)
    {
      const char *dir_name = NULL;
      report_dir_t *dir_info;

      dir_name = find_attr(attrs, "name");

      push_state(ctx, ADD_DIR);

      dir_info = ctx->state->info->dir;

      dir_info->name_buf = svn_stringbuf_dup(dir_info->parent_dir->name_buf,
                                             ctx->sess->pool);
      svn_path_add_component(dir_info->name_buf, dir_name);
      dir_info->name = dir_info->name_buf->data;
      ctx->state->info->file_name = dir_info->name_buf->data;

      ctx->update_editor->add_directory(dir_info->name_buf->data,
                                        dir_info->parent_dir->dir_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        ctx->sess->pool,
                                        &dir_info->dir_baton);
    }
  else if (ctx->state &&
           (ctx->state->state == OPEN_DIR || ctx->state->state == ADD_DIR) &&
           strcmp(prop_name.name, "add-file") == 0)
    {
      const char *file_name = NULL;
      report_info_t *info;

      file_name = find_attr(attrs, "name");

      if (!file_name)
        {
          abort();
        }

      push_state(ctx, ADD_FILE);

      info = ctx->state->info;

      info->file_name_buf = svn_stringbuf_dup(info->dir->name_buf,
                                              ctx->sess->pool);
      svn_path_add_component(info->file_name_buf, file_name);
      info->file_name = info->file_name_buf->data;
    }
  else if (ctx->state && ctx->state->state == ADD_FILE)
    {
      if (strcmp(prop_name.name, "checked-in") == 0)
        {
          ctx->state->info->prop_ns = prop_name.namespace;
          ctx->state->info->prop_name = prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, IGNORE_PROP_NAME);
        }
      else if (strcmp(prop_name.name, "prop") == 0)
        {
          /* need to fetch it. */
          push_state(ctx, NEED_PROP_NAME);
        }
    }
  else if (ctx->state && ctx->state->state == ADD_DIR)
    {
      if (strcmp(prop_name.name, "checked-in") == 0)
        {
          ctx->state->info->prop_ns = prop_name.namespace;
          ctx->state->info->prop_name = prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, IGNORE_PROP_NAME);
        }
      else if (strcmp(prop_name.name, "set-prop") == 0)
        {
          const char *full_prop_name;
          dav_props_t new_prop_name;

          full_prop_name = find_attr(attrs, "name");
          new_prop_name = expand_ns(ctx->ns_list, full_prop_name,
                                    ctx->sess->pool);

          ctx->state->info->prop_ns = new_prop_name.namespace;
          ctx->state->info->prop_name = new_prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, PROP);
        }
    }
  else if (ctx->state && ctx->state->state == IGNORE_PROP_NAME)
    {
      push_state(ctx, PROP);
    }
  else if (ctx->state && ctx->state->state == NEED_PROP_NAME)
    {
      ctx->state->info->prop_ns = prop_name.namespace;
      ctx->state->info->prop_name = prop_name.name;
      ctx->state->info->prop_val = NULL;
      push_state(ctx, PROP);
    }
}

static void XMLCALL
end_report(void *userData, const char *raw_name)
{
  report_context_t *ctx = userData;
  dav_props_t name;

  name = expand_ns(ctx->ns_list, raw_name, ctx->sess->pool);

  if (ctx->state && ctx->state->state == OPEN_DIR &&
      (strcmp(name.name, "open-directory") == 0))
    {
      pop_state(ctx);
    }
  else if (ctx->state && ctx->state->state == ADD_DIR &&
           (strcmp(name.name, "add-directory") == 0))
    {
      pop_state(ctx);
    }
  else if (ctx->state && ctx->state->state == ADD_FILE)
    {
      /* We should have everything we need to fetch the file. */
      fetch_file(ctx, ctx->state->info);
      pop_state(ctx);
    }
  else if (ctx->state && ctx->state->state == PROP)
    {
      set_prop(ctx->state->info->file_props,
               ctx->state->info->file_name,
               ctx->state->info->prop_ns, ctx->state->info->prop_name,
               ctx->state->info->prop_val,
               ctx->sess->pool);
      pop_state(ctx);
    }
  else if (ctx->state &&
           (ctx->state->state == IGNORE_PROP_NAME ||
            ctx->state->state == NEED_PROP_NAME))
    {
      pop_state(ctx);
    }
}

static void XMLCALL
cdata_report(void *userData, const char *data, int len)
{
  report_context_t *ctx = userData;
  if (ctx->state && ctx->state->state == PROP)
    {
      expand_string(&ctx->state->info->prop_val,
                    &ctx->state->info->prop_val_len,
                    data, len, ctx->sess->pool);

    }
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
  report_fetch_t *active_fetch, *prev_fetch;
  propfind_context_t *active_propfind, *prev_propfind;
  const char *vcc_url;
  apr_hash_t *props;
  apr_status_t status;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:update-report>",
                                      sizeof("</S:update-report>")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, sess, sess->repos_url.path, "0",
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

  while (!report->done || report->active_fetches || report->active_propfinds)
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

      /* prune our propfind list if they are done. */
      active_propfind = report->active_propfinds;
      prev_propfind = NULL;
      while (active_propfind)
        {
          if (active_propfind->done == TRUE)
            {
              /* Remove us from the list. */
              if (prev_propfind)
                {
                  prev_propfind->next = active_propfind->next;
                }
              else
                {
                  report->active_propfinds = active_propfind->next;
                }
            }
          else
            {
              prev_propfind = active_propfind;
            }
          active_propfind = active_propfind->next;
        }

      /* prune our fetches list if they are done. */
      active_fetch = report->active_fetches;
      prev_fetch = NULL;
      while (active_fetch)
        {
          if (active_fetch->done == TRUE)
            {
              report_dir_t *parent_dir = active_fetch->info->dir;

              /* walk up and decrease our directory refcount. */
              do
                {
                  parent_dir->ref_count--;

                  if (parent_dir->ref_count)
                     break;

                  /* The easy path here is that we've finished the report. */
                  if (report->done == TRUE)
                    {
                      SVN_ERR(report->update_editor->close_directory(
                                                     parent_dir->dir_baton,
                                                     sess->pool));
                    }
                  else if (!parent_dir->next)
                    {
                      parent_dir->next = report->pending_dir_close;
                      report->pending_dir_close = parent_dir;
                    }

                  parent_dir = parent_dir->parent_dir;
                }
              while (parent_dir);

              /* Remove us from the list. */
              if (prev_fetch)
                {
                  prev_fetch->next = active_fetch->next;
                }
              else
                {
                  report->active_fetches = active_fetch->next;
                }
            }
          else
            {
              prev_fetch = active_fetch;
            }
          active_fetch = active_fetch->next;
        }

      if (report->done == TRUE)
        {
          report_dir_t *dir = report->pending_dir_close;

          while (dir)
            {
              if (!dir->ref_count)
                {
                  SVN_ERR(report->update_editor->close_directory
                          (dir->dir_baton, sess->pool));
                }

              dir = dir->next;
            }
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
    }

  /* FIXME subpool */
  SVN_ERR(report->update_editor->close_edit(report->update_baton, sess->pool));

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
  report->active_fetches = NULL;
  report->active_propfinds = NULL;

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

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, path, "0", check_path_props, pool));
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

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->repos_url.path, "0",
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

      props = apr_hash_make(pool);

      SVN_ERR(retrieve_props(props, session, session->repos_url.path, "0",
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
