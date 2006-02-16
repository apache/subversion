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

#include "ra_serf.h"


const char *
get_ver_prop(apr_hash_t *props,
             const char *path,
             svn_revnum_t rev,
             const char *ns,
             const char *name)
{
  apr_hash_t *ver_props, *path_props, *ns_props;
  const char *val = NULL;

  ver_props = apr_hash_get(props, &rev, sizeof(rev));
  if (ver_props)
    {
      path_props = apr_hash_get(ver_props, path, strlen(path));

      if (path_props)
        {
          ns_props = apr_hash_get(path_props, ns, strlen(ns));
          if (ns_props)
            {
              val = apr_hash_get(ns_props, name, strlen(name));
            }
        }
    }

  return val;
}

const char *
get_prop(apr_hash_t *props,
         const char *path,
         const char *ns,
         const char *name)
{
  return get_ver_prop(props, path, SVN_INVALID_REVNUM, ns, name);
}

void
set_ver_prop(apr_hash_t *props,
             const char *path, svn_revnum_t rev,
             const char *ns, const char *name,
             const char *val, apr_pool_t *pool)
{
  apr_hash_t *ver_props, *path_props, *ns_props;
  apr_size_t path_len, ns_len, name_len;

  path_len = strlen(path);
  ns_len = strlen(ns);
  name_len = strlen(name);

  ver_props = apr_hash_get(props, &rev, sizeof(rev));
  if (!ver_props)
    {
      ver_props = apr_hash_make(pool);
      apr_hash_set(props, apr_pmemdup(pool, &rev, sizeof(rev)), sizeof(rev),
                   ver_props);
    }

  path_props = apr_hash_get(ver_props, path, path_len);

  if (!path_props)
    {
      path_props = apr_hash_make(pool);
      apr_hash_set(ver_props, path, path_len, path_props);

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

void
set_prop(apr_hash_t *props,
         const char *path,
         const char *ns, const char *name,
         const char *val, apr_pool_t *pool)
{
  return set_ver_prop(props, path, SVN_INVALID_REVNUM, ns, name, val, pool);
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
  prop_name = expand_ns(ctx->ns_list, name);

  if (ctx->in_prop && !ctx->attr_name)
    {
      ctx->ns = prop_name.namespace;
      ctx->attr_name = apr_pstrdup(ctx->pool, prop_name.name);
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

          ctx->attr_val = apr_pstrdup(ctx->pool, name);
        }

      /* set the return props and update our cache too. */
      set_ver_prop(ctx->ret_props,
                   ctx->path, ctx->rev,
                   ctx->ns, ctx->attr_name, ctx->attr_val,
                   ctx->pool);
      if (ctx->cache_props)
        {
          set_ver_prop(ctx->sess->cached_props,
                       ctx->path, ctx->rev,
                       ctx->ns, ctx->attr_name,
                       apr_pstrdup(ctx->sess->pool, ctx->attr_val),
                       ctx->sess->pool);
        }

      /* we're done with it. */
      ctx->collect_cdata = FALSE;
      ctx->attr_name = NULL;
      ctx->attr_val = NULL;
    }
  /* FIXME: destroy namespaces as we end a handler */
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
setup_propfind(serf_request_t *request,
               void *setup_baton,
               serf_bucket_t **req_bkt,
               serf_response_acceptor_t *acceptor,
               void **acceptor_baton,
               serf_response_handler_t *handler,
               void **handler_baton,
               apr_pool_t *pool)
{
  propfind_context_t *ctx = setup_baton;

  *req_bkt = serf_bucket_propfind_create(ctx->sess->repos_url.hostinfo,
                                         ctx->path,
                                         ctx->label,
                                         ctx->depth,
                                         ctx->find_props,
                                         serf_request_get_alloc(request));

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->sess;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_propfind(serf_bucket_t *response,
                void *handler_baton,
                apr_pool_t *pool)
{
  propfind_context_t *ctx = handler_baton;
  apr_status_t status;

  if (!response)
    {
      /* uh-oh, we lost our connection! */
      deliver_props(&ctx,
                    ctx->ret_props,
                    ctx->sess,
                    ctx->path,
                    ctx->rev,
                    ctx->depth,
                    ctx->find_props,
                    ctx->pool);
      if (ctx->xmlp)
        {
          XML_ParserFree(ctx->xmlp);
          ctx->xmlp = NULL;
        }
      return APR_SUCCESS;
    }

  if (!ctx->xmlp)
    {
      ctx->xmlp = XML_ParserCreate(NULL);
      XML_SetUserData(ctx->xmlp, ctx);
      XML_SetElementHandler(ctx->xmlp, start_propfind, end_propfind);
      XML_SetCharacterDataHandler(ctx->xmlp, cdata_propfind);
    }

  status = handle_xml_parser(response, ctx->xmlp, &ctx->done, pool);

  if (ctx->done)
    {
      XML_ParserFree(ctx->xmlp);
    }

  return status;
}

static svn_boolean_t
check_cache(apr_hash_t *ret_props,
            ra_serf_session_t *sess,
            const char *path,
            svn_revnum_t rev,
            const dav_props_t *find_props,
            apr_pool_t *pool)
{
  svn_boolean_t cache_hit = TRUE;
  const dav_props_t *prop;

  /* check to see if we have any of this information cached */
  prop = find_props;
  while (prop && prop->namespace)
    {
      const char *val;

      val = get_ver_prop(sess->cached_props, path, rev, prop->namespace,
                         prop->name);
      if (val)
        {
          set_ver_prop(ret_props, path, rev, prop->namespace, prop->name, val,
                       pool);
        }
      else
        {
          cache_hit = FALSE;
        }
      prop++;
    }

  return cache_hit;
}

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
deliver_props(propfind_context_t **prop_ctx,
              apr_hash_t *ret_props,
              ra_serf_session_t *sess,
              const char *path,
              svn_revnum_t rev,
              const char *depth,
              const dav_props_t *find_props,
              apr_pool_t *pool)
{
  const dav_props_t *prop;
  serf_bucket_t *req_bkt;
  serf_request_t *request;
  propfind_context_t *new_prop_ctx;
  apr_status_t status;

  if (!*prop_ctx)
    {
      svn_boolean_t cache_satisfy;

      cache_satisfy = check_cache(ret_props, sess, path, rev, find_props, pool);

      if (cache_satisfy)
        {
          *prop_ctx = NULL;
          return SVN_NO_ERROR;
        }

      new_prop_ctx = apr_pcalloc(pool, sizeof(*new_prop_ctx));
      new_prop_ctx->cache_props = TRUE;

      new_prop_ctx->pool = pool;
      new_prop_ctx->path = path;
      new_prop_ctx->find_props = find_props;
      new_prop_ctx->ret_props = ret_props;
      new_prop_ctx->depth = depth;
      new_prop_ctx->done = FALSE;
      new_prop_ctx->sess = sess;
      new_prop_ctx->rev = rev;
      new_prop_ctx->acceptor = accept_response;
      new_prop_ctx->handler = handle_propfind;

      if (SVN_IS_VALID_REVNUM(rev))
        {
          new_prop_ctx->label = apr_ltoa(pool, rev);
        }
      else
        {
          new_prop_ctx->label = NULL;
        }

      *prop_ctx = new_prop_ctx;
    }

  /* create and deliver request */
  serf_connection_request_create(sess->conn, setup_propfind, *prop_ctx);

  return SVN_NO_ERROR;
}

/**
 * This helper function will block until the PROP_CTX indicates that is done
 * or another error is returned.
 */
svn_error_t *
wait_for_props(propfind_context_t *prop_ctx,
               ra_serf_session_t *sess,
               apr_pool_t *pool)
{
  return context_run_wait(&prop_ctx->done, sess, pool);
}

/**
 * This is a blocking version of deliver_props.
 */
svn_error_t *
retrieve_props(apr_hash_t *prop_vals,
               ra_serf_session_t *sess,
               const char *url,
               svn_revnum_t rev,
               const char *depth,
               const dav_props_t *props,
               apr_pool_t *pool)
{
  propfind_context_t *prop_ctx = NULL;

  SVN_ERR(deliver_props(&prop_ctx, prop_vals, sess, url, rev, depth, props,
                        pool));
  if (prop_ctx)
    {
      SVN_ERR(wait_for_props(prop_ctx, sess, pool));
    }

  return SVN_NO_ERROR;
}

void
walk_all_props(apr_hash_t *props,
               const char *name,
               svn_revnum_t rev,
               walker_visitor_t walker,
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

