/*
 * serf.c :  entry point for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006-2008 CollabNet.  All rights reserved.
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



#define APR_WANT_STRFUNC
#include <apr_want.h>

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
#include "svn_time.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_dep_compat.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/** Capabilities exchange. */

/* Both server and repository support the capability. */
static const char *capability_yes = "yes";
/* Either server or repository does not support the capability. */
static const char *capability_no = "no";
/* Server supports the capability, but don't yet know if repository does. */
static const char *capability_server_yes = "server-yes";

/* Baton type for parsing capabilities out of "OPTIONS" response headers. */
struct capabilities_response_baton
{
  /* This session's capabilities table. */
  apr_hash_t *capabilities;

  /* Signaler for svn_ra_serf__context_run_wait(). */
  svn_boolean_t done;

  /* For temporary work only. */
  apr_pool_t *pool;
};


/* This implements serf_bucket_headers_do_callback_fn_t.
 * BATON is a 'struct capabilities_response_baton *'.
 */
static int
capabilities_headers_iterator_callback(void *baton,
                                       const char *key,
                                       const char *val)
{
  struct capabilities_response_baton *crb = baton;

  if (svn_cstring_casecmp(key, "dav") == 0)
    {
      /* Each header may contain multiple values, separated by commas, e.g.:
           DAV: version-control,checkout,working-resource
           DAV: merge,baseline,activity,version-controlled-collection
           DAV: http://subversion.tigris.org/xmlns/dav/svn/depth */
      apr_array_header_t *vals = svn_cstring_split(val, ",", TRUE, crb->pool);

      /* Right now we only have a few capabilities to detect, so just
         seek for them directly.  This could be written slightly more
         efficiently, but that wouldn't be worth it until we have many
         more capabilities. */

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_DEPTH, vals))
        {
          apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_DEPTH,
                       APR_HASH_KEY_STRING, capability_yes);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_MERGEINFO, vals))
        {
          /* The server doesn't know what repository we're referring
             to, so it can't just say capability_yes. */
          apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
                       APR_HASH_KEY_STRING, capability_server_yes);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_LOG_REVPROPS, vals))
        {
          apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_LOG_REVPROPS,
                       APR_HASH_KEY_STRING, capability_yes);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_PARTIAL_REPLAY, vals))
        {
          apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_PARTIAL_REPLAY,
                       APR_HASH_KEY_STRING, capability_yes);
        }
    }

  return 0;
}


/* This implements serf_response_handler_t.
 * HANDLER_BATON is a 'struct capabilities_response_baton *'.
 */
static apr_status_t
capabilities_response_handler(serf_request_t *request,
                              serf_bucket_t *response,
                              void *handler_baton,
                              apr_pool_t *pool)
{
  struct capabilities_response_baton *crb = handler_baton;
  serf_bucket_t *hdrs = serf_bucket_response_get_headers(response);

  /* Start out assuming all capabilities are unsupported. */
  apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_DEPTH,
               APR_HASH_KEY_STRING, capability_no);
  apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
               APR_HASH_KEY_STRING, capability_no);
  apr_hash_set(crb->capabilities, SVN_RA_CAPABILITY_LOG_REVPROPS,
               APR_HASH_KEY_STRING, capability_no);

  /* Then see which ones we can discover. */
  serf_bucket_headers_do(hdrs, capabilities_headers_iterator_callback, crb);

  /* Bunch of exit conditions to set before we go. */
  crb->done = TRUE;
  serf_request_set_handler(request, svn_ra_serf__handle_discard_body, NULL);
  return APR_SUCCESS;
}


/* Exchange capabilities with the server, by sending an OPTIONS
   request announcing the client's capabilities, and by filling
   SERF_SESS->capabilities with the server's capabilities as read
   from the response headers.  Use POOL only for temporary allocation. */
static svn_error_t *
exchange_capabilities(svn_ra_serf__session_t *serf_sess, apr_pool_t *pool)
{
  svn_ra_serf__handler_t *handler;
  struct capabilities_response_baton crb;

  crb.pool = pool;
  crb.done = FALSE;
  crb.capabilities = serf_sess->capabilities;

  /* No obvious advantage to using svn_ra_serf__create_options_req() here. */
  handler = apr_pcalloc(pool, sizeof(*handler));
  handler->method = "OPTIONS";
  handler->path = serf_sess->repos_url_str;
  handler->body_buckets = NULL;
  handler->response_handler = capabilities_response_handler;
  handler->response_baton = &crb;
  handler->session = serf_sess;
  handler->conn = serf_sess->conns[0];

  /* Client capabilities are sent automagically with every request;
     that's why we don't set up a handler->header_delegate above.
     See issue #3255 for more. */
  svn_ra_serf__request_create(handler);

  return svn_ra_serf__context_run_wait(&(crb.done), serf_sess, pool);
}


svn_error_t *
svn_ra_serf__has_capability(svn_ra_session_t *ra_session,
                            svn_boolean_t *has,
                            const char *capability,
                            apr_pool_t *pool)
{
  svn_ra_serf__session_t *serf_sess = ra_session->priv;
  const char *cap_result;

  /* This capability doesn't rely on anything server side. */
  if (strcmp(capability, SVN_RA_CAPABILITY_COMMIT_REVPROPS) == 0)
    {
      *has = TRUE;
      return SVN_NO_ERROR;
    }

  cap_result = apr_hash_get(serf_sess->capabilities,
                            capability,
                            APR_HASH_KEY_STRING);

  /* If any capability is unknown, they're all unknown, so ask. */
  if (cap_result == NULL)
    SVN_ERR(exchange_capabilities(serf_sess, pool));

  /* Try again, now that we've fetched the capabilities. */
  cap_result = apr_hash_get(serf_sess->capabilities,
                            capability, APR_HASH_KEY_STRING);

  /* Some capabilities depend on the repository as well as the server.
     NOTE: ../libsvn_ra_neon/session.c:svn_ra_neon__has_capability()
     has a very similar code block.  If you change something here,
     check there as well. */
  if (cap_result == capability_server_yes)
    {
      if (strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO) == 0)
        {
          /* Handle mergeinfo specially.  Mergeinfo depends on the
             repository as well as the server, but the server routine
             that answered our exchange_capabilities() call above
             didn't even know which repository we were interested in
             -- it just told us whether the server supports mergeinfo.
             If the answer was 'no', there's no point checking the
             particular repository; but if it was 'yes, we still must
             change it to 'no' iff the repository itself doesn't
             support mergeinfo. */
          svn_mergeinfo_catalog_t ignored;
          svn_error_t *err;
          apr_array_header_t *paths = apr_array_make(pool, 1,
                                                     sizeof(char *));
          APR_ARRAY_PUSH(paths, const char *) = "";

          err = svn_ra_serf__get_mergeinfo(ra_session, &ignored, paths, 0,
                                           FALSE, FALSE, pool);

          if (err)
            {
              if (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
                {
                  svn_error_clear(err);
                  cap_result = capability_no;
                }
              else if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                {
                  /* Mergeinfo requests use relative paths, and
                     anyway we're in r0, so this is a likely error,
                     but it means the repository supports mergeinfo! */
                  svn_error_clear(err);
                  cap_result = capability_yes;
                }
              else
                return err;
            }
          else
            cap_result = capability_yes;

          apr_hash_set(serf_sess->capabilities,
                       SVN_RA_CAPABILITY_MERGEINFO, APR_HASH_KEY_STRING,
                       cap_result);
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
             _("Don't know how to handle '%s' for capability '%s'"),
             capability_server_yes, capability);
        }
    }

  if (cap_result == capability_yes)
    {
      *has = TRUE;
    }
  else if (cap_result == capability_no)
    {
      *has = FALSE;
    }
  else if (cap_result == NULL)
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
         _("Don't know anything about capability '%s'"), capability);
    }
  else  /* "can't happen" */
    {
      /* Well, let's hope it's a string. */
      return svn_error_createf
        (SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
         _("Attempt to fetch capability '%s' resulted in '%s'"),
         capability, cap_result);
    }

  return SVN_NO_ERROR;
}



static const svn_version_t *
ra_serf_version(void)
{
  SVN_VERSION_BODY;
}

#define RA_SERF_DESCRIPTION \
    N_("Module for accessing a repository via WebDAV protocol using serf.")

static const char *
ra_serf_get_description(void)
{
  return _(RA_SERF_DESCRIPTION);
}

static const char * const *
ra_serf_get_schemes(apr_pool_t *pool)
{
  static const char *serf_ssl[] = { "http", "https", NULL };
#if 0
  /* ### Temporary: to shut up a warning. */
  static const char *serf_no_ssl[] = { "http", NULL };
#endif

  /* TODO: Runtime detection. */
  return serf_ssl;
}

static svn_error_t *
load_config(svn_ra_serf__session_t *session,
            apr_hash_t *config_hash,
            apr_pool_t *pool)
{
  svn_config_t *config, *config_client;
  const char *server_group;
  const char *proxy_host = NULL;
  const char *port_str = NULL;
  unsigned int proxy_port;

  config = apr_hash_get(config_hash, SVN_CONFIG_CATEGORY_SERVERS,
                        APR_HASH_KEY_STRING);
  config_client = apr_hash_get(config_hash, SVN_CONFIG_CATEGORY_CONFIG,
                               APR_HASH_KEY_STRING);

  SVN_ERR(svn_config_get_bool(config, &session->using_compression,
                              SVN_CONFIG_SECTION_GLOBAL,
                              SVN_CONFIG_OPTION_HTTP_COMPRESSION, TRUE));

  svn_auth_set_parameter(session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG, config_client);
  svn_auth_set_parameter(session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS, config);

  /* Load the global proxy server settings, if set. */
  svn_config_get(config, &proxy_host, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_PROXY_HOST, NULL);
  svn_config_get(config, &port_str, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_PROXY_PORT, NULL);
  svn_config_get(config, &session->proxy_username, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME, NULL);
  svn_config_get(config, &session->proxy_password, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD, NULL);
  /* Load the global ssl settings, if set. */
  SVN_ERR(svn_config_get_bool(config, &session->trust_default_ca,
                              SVN_CONFIG_SECTION_GLOBAL,
                              SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA,
                              TRUE));
  svn_config_get(config, &session->ssl_authorities, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES, NULL);

  if (config)
    server_group = svn_config_find_group(config,
                                         session->repos_url.hostname,
                                         SVN_CONFIG_SECTION_GROUPS, pool);
  else
    server_group = NULL;

  if (server_group)
    {
      SVN_ERR(svn_config_get_bool(config, &session->using_compression,
                                  server_group,
                                  SVN_CONFIG_OPTION_HTTP_COMPRESSION,
                                  session->using_compression));
      svn_auth_set_parameter(session->wc_callbacks->auth_baton,
                             SVN_AUTH_PARAM_SERVER_GROUP, server_group);

      /* Load the group proxy server settings, overriding global settings. */
      svn_config_get(config, &proxy_host, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_HOST, NULL);
      svn_config_get(config, &port_str, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PORT, NULL);
      svn_config_get(config, &session->proxy_username, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME, NULL);
      svn_config_get(config, &session->proxy_password, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD, NULL);

      /* Load the group ssl settings. */
      SVN_ERR(svn_config_get_bool(config, &session->trust_default_ca,
                                  server_group,
                                  SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA,
                                  TRUE));
      svn_config_get(config, &session->ssl_authorities, server_group,
                     SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES, NULL);
    }

  /* Convert the proxy port value, if any. */
  if (port_str)
    {
      char *endstr;
      const long int port = strtol(port_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: illegal character in proxy "
                                  "port number"));
      if (port < 0)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: negative proxy port number"));
      if (port > 65535)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: proxy port number greater "
                                  "than maximum TCP port number 65535"));
      proxy_port = port;
    }
  else
    proxy_port = 80;

  if (proxy_host)
    {
      apr_sockaddr_t *proxy_addr;
      apr_status_t status;

      status = apr_sockaddr_info_get(&proxy_addr, proxy_host,
                                     APR_UNSPEC, proxy_port, 0,
                                     session->pool);
      session->using_proxy = TRUE;
      serf_config_proxy(session->context, proxy_addr);
    }
  else
    session->using_proxy = FALSE;

  return SVN_NO_ERROR;
}

static void
svn_ra_serf__progress(void *progress_baton, apr_off_t read, apr_off_t written)
{
  const svn_ra_serf__session_t *serf_sess = progress_baton;
  if (serf_sess->wc_progress_func)
    {
      serf_sess->wc_progress_func(read + written, -1,
                                  serf_sess->wc_progress_baton,
                                  serf_sess->pool);
    }
}

static svn_error_t *
svn_ra_serf__open(svn_ra_session_t *session,
                  const char *repos_URL,
                  const svn_ra_callbacks2_t *callbacks,
                  void *callback_baton,
                  apr_hash_t *config,
                  apr_pool_t *pool)
{
  apr_status_t status;
  svn_ra_serf__session_t *serf_sess;
  apr_uri_t url;
  const char *client_string = NULL;

  serf_sess = apr_pcalloc(pool, sizeof(*serf_sess));
  apr_pool_create(&serf_sess->pool, pool);
  serf_sess->bkt_alloc = serf_bucket_allocator_create(serf_sess->pool, NULL,
                                                      NULL);
  serf_sess->cached_props = apr_hash_make(serf_sess->pool);
  serf_sess->wc_callbacks = callbacks;
  serf_sess->wc_callback_baton = callback_baton;
  serf_sess->wc_progress_baton = callbacks->progress_baton;
  serf_sess->wc_progress_func = callbacks->progress_func;

  /* todo: reuse serf context across sessions */
  serf_sess->context = serf_context_create(serf_sess->pool);

  status = apr_uri_parse(serf_sess->pool, repos_URL, &url);
  if (status)
    {
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("Illegal repository URL '%s'"),
                               repos_URL);
    }
  /* Contrary to what the comment for apr_uri_t.path says in apr-util 1.2.12 and
     older, for root paths url.path will be "", where serf requires "/". */
  if (url.path == NULL || url.path[0] == '\0')
    url.path = apr_pstrdup(serf_sess->pool, "/");

  serf_sess->repos_url = url;
  serf_sess->repos_url_str = apr_pstrdup(serf_sess->pool, repos_URL);

  if (!url.port)
    {
      url.port = apr_uri_port_of_scheme(url.scheme);
    }
  serf_sess->using_ssl = (svn_cstring_casecmp(url.scheme, "https") == 0);

  serf_sess->capabilities = apr_hash_make(serf_sess->pool);

  SVN_ERR(load_config(serf_sess, config, serf_sess->pool));

  /* register cleanups */
  apr_pool_cleanup_register(serf_sess->pool, serf_sess,
                            svn_ra_serf__cleanup_serf_session,
                            apr_pool_cleanup_null);

  serf_sess->conns = apr_palloc(serf_sess->pool, sizeof(*serf_sess->conns) * 4);

  serf_sess->conns[0] = apr_pcalloc(serf_sess->pool,
                                    sizeof(*serf_sess->conns[0]));
  serf_sess->conns[0]->bkt_alloc =
          serf_bucket_allocator_create(serf_sess->pool, NULL, NULL);
  serf_sess->conns[0]->session = serf_sess;
  serf_sess->conns[0]->last_status_code = -1;

  /* Unless we're using a proxy, fetch the DNS record for this host */
  if (! serf_sess->using_proxy)
    {
      status = apr_sockaddr_info_get(&serf_sess->conns[0]->address,
                                     url.hostname,
                                     APR_UNSPEC, url.port, 0, serf_sess->pool);
      if (status)
        {
          return svn_error_wrap_apr(status,
                                    _("Could not lookup hostname `%s'"),
                                    url.hostname);
        }
    }
  else
    {
      /* Create an address with unresolved hostname. */
      apr_sockaddr_t *sa = apr_pcalloc(serf_sess->pool, sizeof(apr_sockaddr_t));
      sa->pool = serf_sess->pool;
      sa->hostname = apr_pstrdup(serf_sess->pool, url.hostname);
      sa->port = url.port;
      sa->family = APR_UNSPEC;
      serf_sess->conns[0]->address = sa;
    }

  serf_sess->conns[0]->using_ssl = serf_sess->using_ssl;
  serf_sess->conns[0]->using_compression = serf_sess->using_compression;
  serf_sess->conns[0]->hostinfo = url.hostinfo;
  serf_sess->conns[0]->auth_header = NULL;
  serf_sess->conns[0]->auth_value = NULL;
  serf_sess->conns[0]->useragent = NULL;

  /* create the user agent string */
  if (callbacks->get_client_string)
    callbacks->get_client_string(callback_baton, &client_string, pool);

  if (client_string)
    serf_sess->conns[0]->useragent = apr_pstrcat(pool, USER_AGENT, "/",
                                                 client_string, NULL);
  else
    serf_sess->conns[0]->useragent = USER_AGENT;

  /* go ahead and tell serf about the connection. */
  serf_sess->conns[0]->conn =
      serf_connection_create(serf_sess->context, serf_sess->conns[0]->address,
                             svn_ra_serf__conn_setup, serf_sess->conns[0],
                             svn_ra_serf__conn_closed, serf_sess->conns[0],
                             serf_sess->pool);

  /* Set the progress callback. */
  serf_context_set_progress_cb(serf_sess->context, svn_ra_serf__progress,
                               serf_sess);

  serf_sess->num_conns = 1;

  session->priv = serf_sess;

  return exchange_capabilities(serf_sess, pool);
}

static svn_error_t *
svn_ra_serf__reparent(svn_ra_session_t *ra_session,
                      const char *url,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_uri_t new_url;
  apr_status_t status;

  /* If it's the URL we already have, wave our hands and do nothing. */
  if (strcmp(session->repos_url_str, url) == 0)
    {
      return SVN_NO_ERROR;
    }

  /* Do we need to check that it's the same host and port? */
  status = apr_uri_parse(session->pool, url, &new_url);
  if (status)
    {
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("Illegal repository URL '%s'"), url);
    }

  session->repos_url.path = new_url.path;
  session->repos_url_str = apr_pstrdup(session->pool, url);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_session_url(svn_ra_session_t *ra_session,
                             const char **url,
                             apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  *url = apr_pstrdup(pool, session->repos_url_str);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_latest_revnum(svn_ra_session_t *ra_session,
                               svn_revnum_t *latest_revnum,
                               apr_pool_t *pool)
{
  const char *relative_url, *basecoll_url;
  svn_ra_serf__session_t *session = ra_session->priv;

  return svn_ra_serf__get_baseline_info(&basecoll_url, &relative_url, session,
                                        NULL, session->repos_url.path,
                                        SVN_INVALID_REVNUM, latest_revnum,
                                        pool);
}

static svn_error_t *
svn_ra_serf__rev_proplist(svn_ra_session_t *ra_session,
                          svn_revnum_t rev,
                          apr_hash_t **ret_props,
                          apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *vcc_url;

  props = apr_hash_make(pool);
  *ret_props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                     session, session->conns[0],
                                     session->repos_url.path, pool));

  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      vcc_url, rev, "0", all_props, pool));

  svn_ra_serf__walk_all_props(props, vcc_url, rev, svn_ra_serf__set_bare_props,
                              *ret_props, pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__rev_prop(svn_ra_session_t *session,
                      svn_revnum_t rev,
                      const char *name,
                      svn_string_t **value,
                      apr_pool_t *pool)
{
  apr_hash_t *props;

  SVN_ERR(svn_ra_serf__rev_proplist(session, rev, &props, pool));

  *value = apr_hash_get(props, name, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_path_props(svn_ra_serf__propfind_context_t **ret_prop_ctx,
                 apr_hash_t **ret_props,
                 const char **ret_path,
                 svn_revnum_t *ret_revision,
                 svn_ra_serf__session_t *session,
                 const char *rel_path,
                 svn_revnum_t revision,
                 const svn_ra_serf__dav_props_t *desired_props,
                 apr_pool_t *pool)
{
  svn_ra_serf__propfind_context_t *prop_ctx;
  apr_hash_t *props;
  const char *path;

  path = session->repos_url.path;

  /* If we have a relative path, append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  props = apr_hash_make(pool);

  prop_ctx = NULL;

  /* If we were given a specific revision, we have to fetch the VCC and
   * do a PROPFIND off of that.
   */
  if (!SVN_IS_VALID_REVNUM(revision))
    {
      svn_ra_serf__deliver_props(&prop_ctx, props, session, session->conns[0],
                                 path, revision, "0", desired_props, TRUE,
                                 NULL, session->pool);
    }
  else
    {
      const char *relative_url, *basecoll_url;

      SVN_ERR(svn_ra_serf__get_baseline_info(&basecoll_url, &relative_url,
                                             session, NULL, path,
                                             revision, NULL, pool));

      /* We will try again with our new path; however, we're now
       * technically an unversioned resource because we are accessing
       * the revision's baseline-collection.
       */
      prop_ctx = NULL;
      path = svn_path_url_add_component(basecoll_url, relative_url, pool);
      revision = SVN_INVALID_REVNUM;
      svn_ra_serf__deliver_props(&prop_ctx, props, session, session->conns[0],
                                 path, revision, "0",
                                 desired_props, TRUE,
                                 NULL, session->pool);
    }

  if (prop_ctx)
    {
      SVN_ERR(svn_ra_serf__wait_for_props(prop_ctx, session, pool));
    }

  *ret_path = path;
  *ret_prop_ctx = prop_ctx;
  *ret_props = props;
  *ret_revision = revision;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__check_path(svn_ra_session_t *ra_session,
                        const char *rel_path,
                        svn_revnum_t revision,
                        svn_node_kind_t *kind,
                        apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_ra_serf__propfind_context_t *prop_ctx;
  const char *path, *res_type;
  svn_revnum_t fetched_rev;

  svn_error_t *err = fetch_path_props(&prop_ctx, &props, &path, &fetched_rev,
                                      session, rel_path,
                                      revision, check_path_props, pool);

  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_node_none;
    }
  else
    {
      /* Any other error, raise to caller. */
      if (err)
        return err;

      res_type = svn_ra_serf__get_ver_prop(props, path, fetched_rev,
                                           "DAV:", "resourcetype");
      if (!res_type)
        {
          /* How did this happen? */
          return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                                  _("The OPTIONS response did not include the "
                                    "requested resourcetype value"));
        }
      else if (strcmp(res_type, "collection") == 0)
        {
          *kind = svn_node_dir;
        }
      else
        {
          *kind = svn_node_file;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
dirent_walker(void *baton,
              const char *ns, apr_ssize_t ns_len,
              const char *name, apr_ssize_t name_len,
              const svn_string_t *val,
              apr_pool_t *pool)
{
  svn_dirent_t *entry = baton;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    {
      entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    {
      entry->has_props = TRUE;
    }
  else if (strcmp(ns, "DAV:") == 0)
    {
      if (strcmp(name, SVN_DAV__VERSION_NAME) == 0)
        {
          entry->created_rev = SVN_STR_TO_REV(val->data);
        }
      else if (strcmp(name, "creator-displayname") == 0)
        {
          entry->last_author = val->data;
        }
      else if (strcmp(name, SVN_DAV__CREATIONDATE) == 0)
        {
          SVN_ERR(svn_time_from_cstring(&entry->time, val->data, pool));
        }
      else if (strcmp(name, "getcontentlength") == 0)
        {
          entry->size = apr_atoi64(val->data);
        }
      else if (strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(val->data, "collection") == 0)
            {
              entry->kind = svn_node_dir;
            }
          else
            {
              entry->kind = svn_node_file;
            }
        }
    }

  return SVN_NO_ERROR;
}

struct path_dirent_visitor_t {
  apr_hash_t *full_paths;
  apr_hash_t *base_paths;
  const char *orig_path;
};

static svn_error_t *
path_dirent_walker(void *baton,
                   const char *path, apr_ssize_t path_len,
                   const char *ns, apr_ssize_t ns_len,
                   const char *name, apr_ssize_t name_len,
                   const svn_string_t *val,
                   apr_pool_t *pool)
{
  struct path_dirent_visitor_t *dirents = baton;
  svn_dirent_t *entry;

  /* Skip our original path. */
  if (strcmp(path, dirents->orig_path) == 0)
    {
      return SVN_NO_ERROR;
    }

  entry = apr_hash_get(dirents->full_paths, path, path_len);

  if (!entry)
    {
      const char *base_name;

      entry = apr_pcalloc(pool, sizeof(*entry));

      apr_hash_set(dirents->full_paths, path, path_len, entry);

      base_name = svn_path_uri_decode(svn_path_basename(path, pool), pool);

      apr_hash_set(dirents->base_paths, base_name, APR_HASH_KEY_STRING, entry);
    }

  return dirent_walker(entry, ns, ns_len, name, name_len, val, pool);
}

static svn_error_t *
svn_ra_serf__stat(svn_ra_session_t *ra_session,
                  const char *rel_path,
                  svn_revnum_t revision,
                  svn_dirent_t **dirent,
                  apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_ra_serf__propfind_context_t *prop_ctx;
  const char *path;
  svn_revnum_t fetched_rev;
  svn_dirent_t *entry;
  svn_error_t *err;

  err = fetch_path_props(&prop_ctx, &props, &path, &fetched_rev,
                         session, rel_path, revision, all_props, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          *dirent = NULL;
          return SVN_NO_ERROR;
        }
      else
        return err;
    }

  entry = apr_pcalloc(pool, sizeof(*entry));

  svn_ra_serf__walk_all_props(props, path, fetched_rev, dirent_walker, entry,
                              pool);

  *dirent = entry;

  return SVN_NO_ERROR;
}

/* Reads the 'resourcetype' property from the list PROPS and checks if the
 * resource at PATH@REVISION really is a directory. Returns
 * SVN_ERR_FS_NOT_DIRECTORY if not.
 */
static svn_error_t *
resource_is_directory(apr_hash_t *props,
                      const char *path,
                      svn_revnum_t revision)
{
  const char *res_type;

  res_type = svn_ra_serf__get_ver_prop(props, path, revision,
                                       "DAV:", "resourcetype");
  if (!res_type)
    {
      /* How did this happen? */
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The PROPFIND response did not include the "
                                "requested resourcetype value"));
    }
  else if (strcmp(res_type, "collection") != 0)
    {
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_dir(svn_ra_session_t *ra_session,
                     apr_hash_t **dirents,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **ret_props,
                     const char *rel_path,
                     svn_revnum_t revision,
                     apr_uint32_t dirent_fields,
                     apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *path;

  path = session->repos_url.path;

  /* If we have a relative path, URI encode and append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  props = apr_hash_make(pool);

  /* If the user specified a peg revision other than HEAD, we have to fetch
     the baseline collection url for that revision. If not, we can use the
     public url. */
  if (SVN_IS_VALID_REVNUM(revision) || fetched_rev)
    {
      const char *relative_url, *basecoll_url;

      SVN_ERR(svn_ra_serf__get_baseline_info(&basecoll_url, &relative_url,
                                             session, NULL, path, revision,
                                             fetched_rev, pool));

      path = svn_path_url_add_component(basecoll_url, relative_url, pool);
      revision = SVN_INVALID_REVNUM;
    }

  /* If we're asked for children, fetch them now. */
  if (dirents)
    {
      struct path_dirent_visitor_t dirent_walk;

      SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                          path, revision, "1", all_props,
                                          session->pool));

      /* Check if the path is really a directory. */
      SVN_ERR(resource_is_directory (props, path, revision));

      /* We're going to create two hashes to help the walker along.
       * We're going to return the 2nd one back to the caller as it
       * will have the basenames it expects.
       */
      dirent_walk.full_paths = apr_hash_make(pool);
      dirent_walk.base_paths = apr_hash_make(pool);
      dirent_walk.orig_path = svn_path_canonicalize(path, pool);

      svn_ra_serf__walk_all_paths(props, revision, path_dirent_walker,
                                  &dirent_walk, pool);

      *dirents = dirent_walk.base_paths;
    }

  /* If we're asked for the directory properties, fetch them too. */
  if (ret_props)
    {
      props = apr_hash_make(pool);
      *ret_props = apr_hash_make(pool);

      SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                          path, revision, "0", all_props,
                                          pool));
      /* Check if the path is really a directory. */
      SVN_ERR(resource_is_directory (props, path, revision));

      svn_ra_serf__walk_all_props(props, path, revision,
                                  svn_ra_serf__set_flat_props,
                                  *ret_props, pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_repos_root(svn_ra_session_t *ra_session,
                            const char **url,
                            apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  if (!session->repos_root_str)
    {
      const char *vcc_url;

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                         session, session->conns[0],
                                         session->repos_url.path, pool));
    }

  *url = session->repos_root_str;
  return SVN_NO_ERROR;
}

/* TODO: to fetch the uuid from the repository, we need:
   1. a path that exists in HEAD
   2. a path that's readable

   get_uuid handles the case where a path doesn't exist in HEAD and also the
   case where the root of the repository is not readable.
   However, it does not handle the case where we're fetching path not existing
   in HEAD of a repository with unreadable root directory.
 */
static svn_error_t *
svn_ra_serf__get_uuid(svn_ra_session_t *ra_session,
                      const char **uuid,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;

  props = apr_hash_make(pool);

  if (!session->uuid)
    {
      const char *vcc_url, *relative_url;

      /* We're not interested in vcc_url and relative_url, but this call also
         stores the repository's uuid in the session. */
      SVN_ERR(svn_ra_serf__discover_root(&vcc_url,
                                         &relative_url,
                                         session, session->conns[0],
                                         session->repos_url.path, pool));
      if (!session->uuid)
        {
          return svn_error_create(APR_EGENERAL, NULL,
                                  _("The UUID property was not found on the "
                                    "resource or any of its parents"));
        }
    }

  *uuid = session->uuid;

  return SVN_NO_ERROR;
}


static const svn_ra__vtable_t serf_vtable = {
  ra_serf_version,
  ra_serf_get_description,
  ra_serf_get_schemes,
  svn_ra_serf__open,
  svn_ra_serf__reparent,
  svn_ra_serf__get_session_url,
  svn_ra_serf__get_latest_revnum,
  svn_ra_serf__get_dated_revision,
  svn_ra_serf__change_rev_prop,
  svn_ra_serf__rev_proplist,
  svn_ra_serf__rev_prop,
  svn_ra_serf__get_commit_editor,
  svn_ra_serf__get_file,
  svn_ra_serf__get_dir,
  svn_ra_serf__get_mergeinfo,
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
  svn_ra_serf__get_location_segments,
  svn_ra_serf__get_file_revs,
  svn_ra_serf__lock,
  svn_ra_serf__unlock,
  svn_ra_serf__get_lock,
  svn_ra_serf__get_locks,
  svn_ra_serf__replay,
  svn_ra_serf__has_capability,
  svn_ra_serf__replay_range,
  svn_ra_serf__get_deleted_rev
};

svn_error_t *
svn_ra_serf__init(const svn_version_t *loader_version,
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
