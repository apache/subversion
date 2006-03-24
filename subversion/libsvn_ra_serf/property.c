/*
 * property.c : property routines for ra_serf
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

#include "svn_path.h"
#include "svn_base64.h"

#include "ra_serf.h"


/*
 * This structure represents a pending PROPFIND response.
 */
struct svn_ra_serf__propfind_context_t {
  /* pool to issue allocations from */
  apr_pool_t *pool;

  svn_ra_serf__handler_t *handler;

  /* associated serf session */
  svn_ra_serf__session_t *sess;
  svn_ra_serf__connection_t *conn;

  /* the requested path */
  const char *path;

  /* the requested version (number and string form) */
  svn_revnum_t rev;
  const char *label;

  /* the request depth */
  const char *depth;

  /* the list of requested properties */
  const svn_ra_serf__dav_props_t *find_props;

  /* should we cache the values of this propfind in our session? */
  svn_boolean_t cache_props;

  /* hash table that will be updated with the properties
   *
   * This can be shared between multiple svn_ra_serf__propfind_context_t
   * structures
   */
  apr_hash_t *ret_props;

  /* Current namespace list */
  svn_ra_serf__ns_t *ns_list;

  /* TODO use the state object as in the report */
  /* Are we parsing a property right now? */
  svn_boolean_t in_prop;
  svn_boolean_t in_response;

  /* Should we be harvesting the CDATA elements */
  svn_boolean_t collect_cdata;

  /* If we're dealing with a Depth: 1 response,
   * we may be dealing with multiple paths.
   */
  const char *current_path;

  /* Current ns, attribute name, and value of the property we're parsing */
  const char *ns;
  const char *attr_name;
  const char *attr_val;
  apr_size_t attr_val_len;
  const char *attr_encoding;

  /* Returned status code. */
  int status_code;

  /* Are we done issuing the PROPFIND? */
  svn_boolean_t done;

  /* If not-NULL, add us to this list when we're done. */
  svn_ra_serf__list_t **done_list;

  svn_ra_serf__list_t done_item;
};

const svn_string_t *
svn_ra_serf__get_ver_prop_string(apr_hash_t *props,
                                 const char *path,
                                 svn_revnum_t rev,
                                 const char *ns,
                                 const char *name)
{
  apr_hash_t *ver_props, *path_props, *ns_props;
  void *val = NULL;

  ver_props = apr_hash_get(props, &rev, sizeof(rev));
  if (ver_props)
    {
      path_props = apr_hash_get(ver_props, path, APR_HASH_KEY_STRING);

      if (path_props)
        {
          ns_props = apr_hash_get(path_props, ns, APR_HASH_KEY_STRING);
          if (ns_props)
            {
              val = apr_hash_get(ns_props, name, APR_HASH_KEY_STRING);
            }
        }
    }

  return val;
}

const char *
svn_ra_serf__get_ver_prop(apr_hash_t *props,
                          const char *path,
                          svn_revnum_t rev,
                          const char *ns,
                          const char *name)
{
  const svn_string_t *val;

  val = svn_ra_serf__get_ver_prop_string(props, path, rev, ns, name);

  if (val)
    {
      return val->data;
    }

  return NULL;
}

const char *
svn_ra_serf__get_prop(apr_hash_t *props,
                      const char *path,
                      const char *ns,
                      const char *name)
{
  return svn_ra_serf__get_ver_prop(props, path, SVN_INVALID_REVNUM, ns, name);
}

void
svn_ra_serf__set_ver_prop(apr_hash_t *props,
                          const char *path, svn_revnum_t rev,
                          const char *ns, const char *name,
                          const svn_string_t *val, apr_pool_t *pool)
{
  apr_hash_t *ver_props, *path_props, *ns_props;

  ver_props = apr_hash_get(props, &rev, sizeof(rev));
  if (!ver_props)
    {
      ver_props = apr_hash_make(pool);
      apr_hash_set(props, apr_pmemdup(pool, &rev, sizeof(rev)), sizeof(rev),
                   ver_props);
    }

  path_props = apr_hash_get(ver_props, path, APR_HASH_KEY_STRING);

  if (!path_props)
    {
      path_props = apr_hash_make(pool);
      path = apr_pstrdup(pool, path);
      apr_hash_set(ver_props, path, APR_HASH_KEY_STRING, path_props);

      /* todo: we know that we'll fail the next check, but fall through
       * for now for simplicity's sake.
       */
    }

  ns_props = apr_hash_get(path_props, ns, APR_HASH_KEY_STRING);
  if (!ns_props)
    {
      ns_props = apr_hash_make(pool);
      ns = apr_pstrdup(pool, ns);
      apr_hash_set(path_props, ns, APR_HASH_KEY_STRING, ns_props);
    }

  apr_hash_set(ns_props, name, APR_HASH_KEY_STRING, val);
}

void
svn_ra_serf__set_prop(apr_hash_t *props,
                      const char *path,
                      const char *ns, const char *name,
                      const svn_string_t *val, apr_pool_t *pool)
{
  return svn_ra_serf__set_ver_prop(props, path, SVN_INVALID_REVNUM, ns, name,
                                   val, pool);
}

/*
 * Expat callback invoked on a start element tag for a PROPFIND response.
 */
static void XMLCALL
start_propfind(void *userData, const char *name, const char **attrs)
{
  svn_ra_serf__propfind_context_t *ctx = userData;
  svn_ra_serf__dav_props_t prop_name;

  /* check for new namespaces */
  svn_ra_serf__define_ns(&ctx->ns_list, attrs, ctx->pool);

  /* look up name space if present */
  prop_name = svn_ra_serf__expand_ns(ctx->ns_list, name);

  if (!ctx->in_response && strcmp(prop_name.name, "response") == 0)
    {
      ctx->in_response = TRUE;
    }
  else if (ctx->in_response && strcmp(prop_name.name, "href") == 0)
    {
      if (strcmp(ctx->depth, "1") == 0)
        {
          ctx->collect_cdata = TRUE;
        }
      else
        {
          ctx->current_path = ctx->path;
        }
    }
  else if (ctx->in_response && strcmp(prop_name.name, "prop") == 0)
    {
      ctx->in_response = FALSE;
      ctx->in_prop = TRUE;
    }
  else if (ctx->in_prop && !ctx->attr_name)
    {
      ctx->ns = prop_name.namespace;
      ctx->attr_name = apr_pstrdup(ctx->pool, prop_name.name);
      ctx->attr_encoding = svn_ra_serf__find_attr(attrs, "V:encoding");
      /* we want to flag the cdata handler to pick up what's next. */
      ctx->collect_cdata = TRUE;
    }
}

/*
 * Expat callback invoked on an end element tag for a PROPFIND response.
 */
static void XMLCALL
end_propfind(void *userData, const char *name)
{
  svn_ra_serf__propfind_context_t *ctx = userData;
  svn_ra_serf__dav_props_t prop_name;

  /* look up name space if present */
  prop_name = svn_ra_serf__expand_ns(ctx->ns_list, name);

  if (ctx->in_response && strcmp(prop_name.name, "response") == 0)
    {
      ctx->in_response = FALSE;
    }
  if (ctx->in_prop && strcmp(prop_name.name, "prop") == 0)
    {
      ctx->in_prop = FALSE;
      ctx->in_response = TRUE;
    }
  else if (ctx->collect_cdata)
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

          ctx->attr_val = apr_pstrdup(ctx->pool, name);
          ctx->attr_val_len = strlen(ctx->attr_val);
        }
   
      
      if (ctx->in_response && strcmp(prop_name.name, "href") == 0)
        {
          apr_pool_t *pool;
          const char *canon_path;

          if (!ctx->cache_props)
            {
              pool = ctx->pool;
            }
          else
            {
              pool = ctx->sess->pool;
            }

          ctx->current_path = svn_path_canonicalize(ctx->attr_val, pool);
        }
      else
        {
          if (ctx->attr_encoding)
            {
              if (strcmp(ctx->attr_encoding, "base64") == 0)
                {
                  svn_string_t encoded;
                  const svn_string_t *decoded;

                  encoded.data = ctx->attr_val;
                  encoded.len = ctx->attr_val_len;

                  decoded = svn_base64_decode_string(&encoded, ctx->pool);
                  ctx->attr_val = decoded->data;
                  ctx->attr_val_len = decoded->len;
                }
              else
                {
                  abort();
                }
            }

          /* set the return props and update our cache too. */
          svn_ra_serf__set_ver_prop(ctx->ret_props,
                                    ctx->current_path, ctx->rev,
                                    ctx->ns, ctx->attr_name,
                                    svn_string_ncreate(ctx->attr_val,
                                                       ctx->attr_val_len,
                                                       ctx->pool),
                                    ctx->pool);
          if (ctx->cache_props)
            {
              const char *name, *val;
              svn_string_t *val_str;

              name = apr_pstrdup(ctx->sess->pool, ctx->attr_name);
              val = apr_pmemdup(ctx->sess->pool, ctx->attr_val,
                                ctx->attr_val_len);
              val_str = svn_string_ncreate(val, ctx->attr_val_len,
                                           ctx->sess->pool);

              svn_ra_serf__set_ver_prop(ctx->sess->cached_props,
                                        ctx->current_path, ctx->rev, ctx->ns,
                                        name, val_str,
                                        ctx->sess->pool);
            }
        }

      /* we're done with it. */
      ctx->collect_cdata = FALSE;
      ctx->attr_name = NULL;
      ctx->attr_val = NULL;
      ctx->attr_encoding = NULL;
    }
  /* FIXME: destroy namespaces as we end a handler */
}

/*
 * Expat callback invoked on CDATA elements in a PROPFIND response.
 *
 * This callback can be called multiple times.
 */
static void XMLCALL
cdata_propfind(void *userData, const char *data, int len)
{
  svn_ra_serf__propfind_context_t *ctx = userData;
  if (ctx->collect_cdata)
    {
      svn_ra_serf__expand_string(&ctx->attr_val, &ctx->attr_val_len,
                                 data, len, ctx->pool);
    }

}

static apr_status_t
setup_propfind(serf_request_t *request,
               void *setup_baton,
               serf_bucket_t **req_bkt,
               serf_response_acceptor_t *acceptor,
               void **acceptor_baton,
               serf_response_handler_t *handler,
               void **handler_baton,
               apr_pool_t *pool)
{
  svn_ra_serf__propfind_context_t *ctx = setup_baton;
  svn_ra_serf__xml_parser_t *parser_ctx;

  *req_bkt =
      svn_ra_serf__bucket_propfind_create(ctx->conn, ctx->path, ctx->label,
                                          ctx->depth, ctx->find_props,
                                          serf_request_get_alloc(request));

  if (ctx->conn->using_ssl)
    {
      *req_bkt =
          serf_bucket_ssl_encrypt_create(*req_bkt, ctx->conn->ssl_context,
                                         serf_request_get_alloc(request));

      if (!ctx->conn->ssl_context)
        {
          ctx->conn->ssl_context =
              serf_bucket_ssl_encrypt_context_get(*req_bkt);
        }
    }

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->user_data = ctx;
  parser_ctx->start = start_propfind;
  parser_ctx->end = end_propfind;
  parser_ctx->cdata = cdata_propfind;
  parser_ctx->status_code = &ctx->status_code;
  parser_ctx->done = &ctx->done;
  parser_ctx->done_list = ctx->done_list;
  parser_ctx->done_item = &ctx->done_item;

  *handler = svn_ra_serf__handle_xml_parser;
  *handler_baton = parser_ctx;

  return APR_SUCCESS;
}

static svn_boolean_t
check_cache(apr_hash_t *ret_props,
            svn_ra_serf__session_t *sess,
            const char *path,
            svn_revnum_t rev,
            const svn_ra_serf__dav_props_t *find_props,
            apr_pool_t *pool)
{
  svn_boolean_t cache_hit = TRUE;
  const svn_ra_serf__dav_props_t *prop;

  /* check to see if we have any of this information cached */
  prop = find_props;
  while (prop && prop->namespace)
    {
      const svn_string_t *val;

      val = svn_ra_serf__get_ver_prop_string(sess->cached_props, path, rev,
                                             prop->namespace, prop->name);
      if (val)
        {
          svn_ra_serf__set_ver_prop(ret_props, path, rev,
                                    prop->namespace, prop->name, val, pool);
        }
      else
        {
          cache_hit = FALSE;
        }
      prop++;
    }

  return cache_hit;
}

/*
 * This function will deliver a PROP_CTX PROPFIND request in the SESS
 * serf context for the properties listed in LOOKUP_PROPS at URL for
 * DEPTH ("0","1","infinity").
 *
 * This function will not block waiting for the response.  Instead, the
 * caller is expected to call context_run and wait for the PROP_CTX->done
 * flag to be set.
 */
svn_error_t *
svn_ra_serf__deliver_props(svn_ra_serf__propfind_context_t **prop_ctx,
                           apr_hash_t *ret_props,
                           svn_ra_serf__session_t *sess,
                           svn_ra_serf__connection_t *conn,
                           const char *path,
                           svn_revnum_t rev,
                           const char *depth,
                           const svn_ra_serf__dav_props_t *find_props,
                           svn_boolean_t cache_props,
                           svn_ra_serf__list_t **done_list,
                           apr_pool_t *pool)
{
  const svn_ra_serf__dav_props_t *prop;
  serf_bucket_t *req_bkt;
  serf_request_t *request;
  svn_ra_serf__propfind_context_t *new_prop_ctx;
  apr_status_t status;

  if (!*prop_ctx)
    {
      svn_boolean_t cache_satisfy;
      svn_ra_serf__handler_t *handler;

      cache_satisfy = check_cache(ret_props, sess, path, rev, find_props, pool);

      if (cache_satisfy)
        {
          *prop_ctx = NULL;
          return SVN_NO_ERROR;
        }

      new_prop_ctx = apr_pcalloc(pool, sizeof(*new_prop_ctx));

      new_prop_ctx->pool = pool;
      new_prop_ctx->path = path;
      new_prop_ctx->cache_props = cache_props;
      new_prop_ctx->find_props = find_props;
      new_prop_ctx->ret_props = ret_props;
      new_prop_ctx->depth = depth;
      new_prop_ctx->done = FALSE;
      new_prop_ctx->sess = sess;
      new_prop_ctx->conn = conn;
      new_prop_ctx->rev = rev;
      new_prop_ctx->done_list = done_list;

      if (SVN_IS_VALID_REVNUM(rev))
        {
          new_prop_ctx->label = apr_ltoa(pool, rev);
        }
      else
        {
          new_prop_ctx->label = NULL;
        }

      handler = apr_pcalloc(pool, sizeof(*handler));

      handler->delegate = setup_propfind;
      handler->delegate_baton = new_prop_ctx;
      handler->session = new_prop_ctx->sess;
      handler->conn = new_prop_ctx->conn;

      new_prop_ctx->handler = handler;

      *prop_ctx = new_prop_ctx;
    }

  /* create request */
  svn_ra_serf__request_create((*prop_ctx)->handler);

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_ra_serf__propfind_is_done(svn_ra_serf__propfind_context_t *ctx)
{
  return ctx->done;
}

int
svn_ra_serf__propfind_status_code(svn_ra_serf__propfind_context_t *ctx)
{
  return ctx->status_code;
}

/*
 * This helper function will block until the PROP_CTX indicates that is done
 * or another error is returned.
 */
svn_error_t *
svn_ra_serf__wait_for_props(svn_ra_serf__propfind_context_t *prop_ctx,
                            svn_ra_serf__session_t *sess,
                            apr_pool_t *pool)
{
  return svn_ra_serf__context_run_wait(&prop_ctx->done, sess, pool);
}

/*
 * This is a blocking version of deliver_props.
 */
svn_error_t *
svn_ra_serf__retrieve_props(apr_hash_t *prop_vals,
                            svn_ra_serf__session_t *sess,
                            svn_ra_serf__connection_t *conn,
                            const char *url,
                            svn_revnum_t rev,
                            const char *depth,
                            const svn_ra_serf__dav_props_t *props,
                            apr_pool_t *pool)
{
  svn_ra_serf__propfind_context_t *prop_ctx = NULL;

  SVN_ERR(svn_ra_serf__deliver_props(&prop_ctx, prop_vals, sess, conn, url,
                                     rev, depth, props, TRUE, NULL, pool));
  if (prop_ctx)
    {
      SVN_ERR(svn_ra_serf__wait_for_props(prop_ctx, sess, pool));
    }

  return SVN_NO_ERROR;
}

void
svn_ra_serf__walk_all_props(apr_hash_t *props,
                            const char *name,
                            svn_revnum_t rev,
                            svn_ra_serf__walker_visitor_t walker,
                            void *baton,
                            apr_pool_t *pool)
{
  apr_hash_index_t *ns_hi;
  apr_hash_t *ver_props, *path_props;

  ver_props = apr_hash_get(props, &rev, sizeof(rev));

  if (!ver_props)
    {
      return;
    }

  path_props = apr_hash_get(ver_props, name, strlen(name));

  if (!path_props)
    {
      return;
    }

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

void
svn_ra_serf__walk_all_paths(apr_hash_t *props,
                            svn_revnum_t rev,
                            svn_ra_serf__path_rev_walker_t walker,
                            void *baton,
                            apr_pool_t *pool)
{
  apr_hash_index_t *path_hi;
  apr_hash_t *ver_props;

  ver_props = apr_hash_get(props, &rev, sizeof(rev));

  if (!ver_props)
    {
      return;
    }

  for (path_hi = apr_hash_first(pool, ver_props); path_hi;
       path_hi = apr_hash_next(path_hi))
    {
      void *path_props;
      const void *path_name;
      apr_ssize_t path_len;
      apr_hash_index_t *ns_hi;

      apr_hash_this(path_hi, &path_name, &path_len, &path_props);
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
              walker(baton, path_name, path_len, ns_name, ns_len,
                     prop_name, prop_len, prop_val, pool);
            }
        }
    }
}

svn_error_t *
svn_ra_serf__set_baton_props(svn_ra_serf__prop_set_t setprop, void *baton,
                             const char *ns, apr_ssize_t ns_len,
                             const char *name, apr_ssize_t name_len,
                             const svn_string_t *val,
                             apr_pool_t *pool)
{
  const char *prop_name;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    prop_name = name;
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    prop_name = apr_pstrcat(pool, SVN_PROP_PREFIX, name, NULL);
  else if (strcmp(ns, SVN_PROP_PREFIX) == 0)
    prop_name = apr_pstrcat(pool, SVN_PROP_PREFIX, name, NULL);
  else if (strcmp(ns, "") == 0)
    prop_name = name;
  else if (strcmp(name, "version-name") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_REV;
  else if (strcmp(name, "creationdate") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_DATE;
  else if (strcmp(name, "creator-displayname") == 0)
    prop_name = SVN_PROP_ENTRY_LAST_AUTHOR;
  else if (strcmp(name, "repository-uuid") == 0)
    prop_name = SVN_PROP_ENTRY_UUID;
  else if (strcmp(name, "checked-in") == 0)
    prop_name = SVN_RA_SERF__WC_CHECKED_IN_URL;
  else
    {
      /* do nothing for now? */
      return;
    }

  return setprop(baton, prop_name, val, pool);
}

svn_error_t *
set_hash_props(void *baton,
               const char *name,
               const svn_string_t *value,
               apr_pool_t *pool)
{
  apr_hash_t *props = baton;

  apr_hash_set(props, name, APR_HASH_KEY_STRING, value);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__set_flat_props(void *baton,
                            const char *ns, apr_ssize_t ns_len,
                            const char *name, apr_ssize_t name_len,
                            const svn_string_t *val,
                            apr_pool_t *pool)
{
  return svn_ra_serf__set_baton_props(set_hash_props, baton,
                                      ns, ns_len, name, name_len, val, pool);
}
