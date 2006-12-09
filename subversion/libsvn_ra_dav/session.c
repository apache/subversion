/*
 * session.c :  routines for maintaining sessions state (to the DAV server)
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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



#include <assert.h>
#include <ctype.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_xml.h>

#include <ne_socket.h>
#include <ne_request.h>
#include <ne_uri.h>
#include <ne_auth.h>
#include <ne_locks.h>
#include <ne_alloc.h>
#include <ne_utils.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_xml.h"
#include "svn_private_config.h"

#include "ra_dav.h"

#define DEFAULT_HTTP_TIMEOUT 3600


/* a cleanup routine attached to the pool that contains the RA session
   baton. */
static apr_status_t cleanup_session(void *sess)
{
  ne_session_destroy(sess);
  return APR_SUCCESS;
}

/* a cleanup routine attached to the pool that contains the RA session
   root URI. */
static apr_status_t cleanup_uri(void *uri)
{
  ne_uri_free(uri);
  return APR_SUCCESS;
}

/* A neon-session callback to 'pull' authentication data when
   challenged.  In turn, this routine 'pulls' the data from the client
   callbacks if needed.  */
static int request_auth(void *userdata, const char *realm, int attempt,
                        char *username, char *password)
{
  svn_error_t *err;
  svn_ra_dav__session_t *ras = userdata;
  void *creds;
  svn_auth_cred_simple_t *simple_creds;  

  /* Start by clearing the cache of any previously-fetched username. */
  ras->auth_username = NULL;

  /* No auth_baton?  Give up. */
  if (! ras->callbacks->auth_baton)
    return -1;

  /* Neon automatically tries some auth protocols and bumps the attempt
     count without using Subversion's callbacks, so we can't depend
     on attempt == 0 the first time we are called -- we need to check
     if the auth state has been initted as well.  */
  if (attempt == 0 || ras->auth_iterstate == NULL)
    {
      const char *realmstring;

      /* <https://svn.collab.net:80> Subversion repository */
      realmstring = apr_psprintf(ras->pool, "<%s://%s:%d> %s",
                                 ras->root.scheme, ras->root.host,
                                 ras->root.port, realm);

      err = svn_auth_first_credentials(&creds,
                                       &(ras->auth_iterstate), 
                                       SVN_AUTH_CRED_SIMPLE,
                                       realmstring,
                                       ras->callbacks->auth_baton,
                                       ras->pool);
    }

  else /* attempt > 0 */
    /* ### TODO:  if the http realm changed this time around, we
       should be calling first_creds(), not next_creds(). */
    err = svn_auth_next_credentials(&creds,
                                    ras->auth_iterstate,
                                    ras->pool);
  if (err || ! creds)
    {
      svn_error_clear(err);
      return -1;
    }
  simple_creds = creds;
  
  /* ### silently truncates username/password to 256 chars. */
  apr_cpystrn(username, simple_creds->username, NE_ABUFSIZ);
  apr_cpystrn(password, simple_creds->password, NE_ABUFSIZ);

  /* Cache the fetched username in ra_session. */
  ras->auth_username = apr_pstrdup(ras->pool, simple_creds->username);

  return 0;
}


static const apr_uint32_t neon_failure_map[][2] =
{
  { NE_SSL_NOTYETVALID,        SVN_AUTH_SSL_NOTYETVALID },
  { NE_SSL_EXPIRED,            SVN_AUTH_SSL_EXPIRED },
  { NE_SSL_IDMISMATCH,         SVN_AUTH_SSL_CNMISMATCH },
  { NE_SSL_UNTRUSTED,          SVN_AUTH_SSL_UNKNOWNCA }
};

/* Convert neon's SSL failure mask to our own failure mask. */
static apr_uint32_t
convert_neon_failures(int neon_failures)
{
  apr_uint32_t svn_failures = 0;
  apr_size_t i;

  for (i = 0; i < sizeof(neon_failure_map) / (2 * sizeof(int)); ++i)
    {
      if (neon_failures & neon_failure_map[i][0])
        {
          svn_failures |= neon_failure_map[i][1];
          neon_failures &= ~neon_failure_map[i][0];
        }
    }

  /* Map any remaining neon failure bits to our OTHER bit. */
  if (neon_failures)
    {
      svn_failures |= SVN_AUTH_SSL_OTHER;
    }

  return svn_failures;
}

/* A neon-session callback to validate the SSL certificate when the CA
   is unknown (e.g. a self-signed cert), or there are other SSL
   certificate problems. */
static int
server_ssl_callback(void *userdata,
                    int failures,
                    const ne_ssl_certificate *cert)
{
  svn_ra_dav__session_t *ras = userdata;
  svn_auth_cred_ssl_server_trust_t *server_creds = NULL;
  void *creds;
  svn_auth_iterstate_t *state;
  apr_pool_t *pool;
  svn_error_t *error;
  char *ascii_cert = ne_ssl_cert_export(cert);
  char *issuer_dname = ne_ssl_readable_dname(ne_ssl_cert_issuer(cert));
  svn_auth_ssl_server_cert_info_t cert_info;
  char fingerprint[NE_SSL_DIGESTLEN];
  char valid_from[NE_SSL_VDATELEN], valid_until[NE_SSL_VDATELEN];
  const char *realmstring;
  apr_uint32_t *svn_failures = apr_palloc(ras->pool, sizeof(*svn_failures));

  /* Construct the realmstring, e.g. https://svn.collab.net:80 */
  realmstring = apr_psprintf(ras->pool, "%s://%s:%d", ras->root.scheme,
                             ras->root.host, ras->root.port);

  *svn_failures = convert_neon_failures(failures);
  svn_auth_set_parameter(ras->callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                         svn_failures);

  /* Extract the info from the certificate */
  cert_info.hostname = ne_ssl_cert_identity(cert);
  if (ne_ssl_cert_digest(cert, fingerprint) != 0)
    {
      strcpy(fingerprint, "<unknown>");
    }
  cert_info.fingerprint = fingerprint;
  ne_ssl_cert_validity(cert, valid_from, valid_until);
  cert_info.valid_from = valid_from;
  cert_info.valid_until = valid_until;
  cert_info.issuer_dname = issuer_dname;
  cert_info.ascii_cert = ascii_cert;

  svn_auth_set_parameter(ras->callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                         &cert_info);

  apr_pool_create(&pool, ras->pool);
  error = svn_auth_first_credentials(&creds, &state,
                                     SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                     realmstring,
                                     ras->callbacks->auth_baton,
                                     pool);
  if (error || ! creds)
    {
      svn_error_clear(error);
    }
  else
    {
      server_creds = creds;
      error = svn_auth_save_credentials(state, pool);
      if (error)
        {
          /* It would be nice to show the error to the user somehow... */
          svn_error_clear(error);
        }
    }

  free(issuer_dname);
  free(ascii_cert);
  svn_auth_set_parameter(ras->callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO, NULL);

  apr_pool_destroy(pool);
  return ! server_creds;
}

static svn_boolean_t
client_ssl_decrypt_cert(svn_ra_dav__session_t *ras,
                        const char *cert_file,
                        ne_ssl_client_cert *clicert)
{
  svn_auth_iterstate_t *state;
  svn_error_t *error;
  apr_pool_t *pool;
  svn_boolean_t ok = FALSE;
  void *creds;
  int try;

  apr_pool_create(&pool, ras->pool);
  for (try = 0; TRUE; ++try)
    {
      if (try == 0)
        {
          error = svn_auth_first_credentials(&creds, &state,
                                             SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                             cert_file,
                                             ras->callbacks->auth_baton,
                                             pool);
        }
      else
        {
          error = svn_auth_next_credentials(&creds, state, pool);
        }

      if (error || ! creds)
        {
          /* Failure or too many attempts */
          svn_error_clear(error);
          break;
        }
      else
        {
          svn_auth_cred_ssl_client_cert_pw_t *pw_creds = creds;

          if (ne_ssl_clicert_decrypt(clicert, pw_creds->password) == 0)
            {
              /* Success */
              ok = TRUE;
              break;
            }
        }
    }
  apr_pool_destroy(pool);

  return ok;
}


static void
client_ssl_callback(void *userdata, ne_session *sess,
                    const ne_ssl_dname *const *dnames,
                    int dncount)
{
  svn_ra_dav__session_t *ras = userdata;
  ne_ssl_client_cert *clicert = NULL;
  void *creds;
  svn_auth_iterstate_t *state;
  const char *realmstring;
  apr_pool_t *pool;
  svn_error_t *error;
  int try;

  apr_pool_create(&pool, ras->pool);

  realmstring = apr_psprintf(pool, "%s://%s:%d", ras->root.scheme,
                             ras->root.host, ras->root.port);

  for (try = 0; TRUE; ++try)
    {
      if (try == 0)
        {
          error = svn_auth_first_credentials(&creds, &state,
                                             SVN_AUTH_CRED_SSL_CLIENT_CERT,
                                             realmstring,
                                             ras->callbacks->auth_baton,
                                             pool);
        }
      else
        {
          error = svn_auth_next_credentials(&creds, state, pool);
        }

      if (error || ! creds)
        {
          /* Failure or too many attempts */
          svn_error_clear(error);
          break;
        }
      else
        {
          svn_auth_cred_ssl_client_cert_t *client_creds = creds;

          clicert = ne_ssl_clicert_read(client_creds->cert_file);
          if (clicert)
            {
              if (! ne_ssl_clicert_encrypted(clicert) ||
                  client_ssl_decrypt_cert(ras, client_creds->cert_file,
                                          clicert))
                {
                  ne_ssl_set_clicert(sess, clicert);
                }
              break;
            }
        }
    }

  apr_pool_destroy(pool);
}

/* Set *PROXY_HOST, *PROXY_PORT, *PROXY_USERNAME, *PROXY_PASSWORD,
 * *TIMEOUT_SECONDS, *NEON_DEBUG, *COMPRESSION, and *NEON_AUTO_PROTOCOLS
 * to the information for REQUESTED_HOST, allocated in POOL, if there is
 * any applicable information.  If there is no applicable information or
 * if there is an error, then set *PROXY_PORT to (unsigned int) -1,
 * *TIMEOUT_SECONDS and *NEON_DEBUG to zero, *COMPRESSION to TRUE,
 * *NEON_AUTH_TYPES is left untouched, and the rest are set to NULL.
 * This function can return an error, so before examining any values,
 * check the error return value.
 */
static svn_error_t *get_server_settings(const char **proxy_host,
                                        unsigned int *proxy_port,
                                        const char **proxy_username,
                                        const char **proxy_password,
                                        int *timeout_seconds,
                                        int *neon_debug,
                                        svn_boolean_t *compression,
                                        unsigned int *neon_auth_types,
                                        svn_config_t *cfg,
                                        const char *requested_host,
                                        apr_pool_t *pool)
{
  const char *exceptions, *port_str, *timeout_str, *server_group;
  const char *debug_str, *http_auth_types;
  svn_boolean_t is_exception = FALSE;
  /* If we find nothing, default to nulls. */
  *proxy_host     = NULL;
  *proxy_port     = (unsigned int) -1;
  *proxy_username = NULL;
  *proxy_password = NULL;
  port_str        = NULL;
  timeout_str     = NULL;
  debug_str       = NULL;
  http_auth_types = NULL;

  /* If there are defaults, use them, but only if the requested host
     is not one of the exceptions to the defaults. */
  svn_config_get(cfg, &exceptions, SVN_CONFIG_SECTION_GLOBAL, 
                 SVN_CONFIG_OPTION_HTTP_PROXY_EXCEPTIONS, NULL);
  if (exceptions)
    {
      apr_array_header_t *l = svn_cstring_split(exceptions, ",", TRUE, pool);
      is_exception = svn_cstring_match_glob_list(requested_host, l);
    }
  if (! is_exception)
    {
      svn_config_get(cfg, proxy_host, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_HOST, NULL);
      svn_config_get(cfg, &port_str, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_PORT, NULL);
      svn_config_get(cfg, proxy_username, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME, NULL);
      svn_config_get(cfg, proxy_password, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD, NULL);
      svn_config_get(cfg, &timeout_str, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_TIMEOUT, NULL);
      SVN_ERR(svn_config_get_bool(cfg, compression, SVN_CONFIG_SECTION_GLOBAL,
                                  SVN_CONFIG_OPTION_HTTP_COMPRESSION, TRUE));
      svn_config_get(cfg, &debug_str, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_NEON_DEBUG_MASK, NULL);
#ifdef SVN_NEON_0_26
      svn_config_get(cfg, &http_auth_types, SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_AUTH_TYPES, NULL);
#endif
    }

  if (cfg)
    server_group = svn_config_find_group(cfg, requested_host, 
                                         SVN_CONFIG_SECTION_GROUPS, pool);
  else
    server_group = NULL;

  if (server_group)
    {
      svn_config_get(cfg, proxy_host, server_group, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_HOST, *proxy_host);
      svn_config_get(cfg, &port_str, server_group, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_PORT, port_str);
      svn_config_get(cfg, proxy_username, server_group, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME, *proxy_username);
      svn_config_get(cfg, proxy_password, server_group, 
                     SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD, *proxy_password);
      svn_config_get(cfg, &timeout_str, server_group, 
                     SVN_CONFIG_OPTION_HTTP_TIMEOUT, timeout_str);
      SVN_ERR(svn_config_get_bool(cfg, compression, server_group,
                                  SVN_CONFIG_OPTION_HTTP_COMPRESSION,
                                  *compression));
      svn_config_get(cfg, &debug_str, server_group, 
                     SVN_CONFIG_OPTION_NEON_DEBUG_MASK, debug_str);
#ifdef SVN_NEON_0_26
      svn_config_get(cfg, &http_auth_types, SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_AUTH_TYPES, NULL);
#endif
    }

  /* Special case: convert the port value, if any. */
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
      *proxy_port = port;
    }
  else
    *proxy_port = 80;

  if (timeout_str)
    {
      char *endstr;
      const long int timeout = strtol(timeout_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                                _("Invalid config: illegal character in "
                                  "timeout value"));
      if (timeout < 0)
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                                _("Invalid config: negative timeout value"));
      *timeout_seconds = timeout;
    }
  else
    *timeout_seconds = 0;

  if (debug_str)
    {
      char *endstr;
      const long int debug = strtol(debug_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                                _("Invalid config: illegal character in "
                                  "debug mask value"));

      *neon_debug = debug;
    }
  else
    *neon_debug = 0;

#ifdef SVN_NEON_0_26
  if (http_auth_types)
    {
      char *token, *last;
      char *auth_types_list = apr_palloc(pool, strlen(http_auth_types) + 1);
      apr_collapse_spaces(auth_types_list, http_auth_types);
      while ((token = apr_strtok(auth_types_list, ";", &last)) != NULL)
        {
          auth_types_list = NULL;
          if (strcasecmp("basic", token) == 0)
            *neon_auth_types |= NE_AUTH_BASIC;
          else if (strcasecmp("digest", token) == 0)
            *neon_auth_types |= NE_AUTH_DIGEST;
          else if (strcasecmp("negotiate", token) == 0)
            *neon_auth_types |= NE_AUTH_NEGOTIATE;
          else
            return svn_error_createf(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                                     _("Invalid config: unknown http auth"
                                       "type '%s'"), token);
      }
    }
#endif /* SVN_NEON_0_26 */

  return SVN_NO_ERROR;
}


/* Userdata for the `proxy_auth' function. */
struct proxy_auth_baton
{
  const char *username;  /* Cannot be NULL, but "" is okay. */
  const char *password;  /* Cannot be NULL, but "" is okay. */
};


/* An `ne_request_auth' callback, see ne_auth.h.  USERDATA is a
 * `struct proxy_auth_baton *'.
 *
 * If ATTEMPT < 10, copy USERDATA->username and USERDATA->password
 * into USERNAME and PASSWORD respectively (but do not copy more than
 * NE_ABUFSIZ bytes of either), and return zero to indicate to Neon
 * that authentication should be attempted.
 *
 * If ATTEMPT >= 10, copy nothing into USERNAME and PASSWORD and
 * return 1, to cancel further authentication attempts.
 *
 * Ignore REALM.
 *
 * ### Note: There is no particularly good reason for the 10-attempt
 * limit.  Perhaps there should only be one attempt, and if it fails,
 * we just cancel any further attempts.  I used 10 just in case the
 * proxy tries various times with various realms, since we ignore
 * REALM.  And why do we ignore REALM?  Because we currently don't
 * have any way to specify different auth information for different
 * realms.  (I'm assuming that REALM would be a realm on the proxy
 * server, not on the Subversion repository server that is the real
 * destination.)  Do we have any need to support proxy realms?
 */
static int proxy_auth(void *userdata,
                      const char *realm,
                      int attempt,
                      char *username,
                      char *password)
{
  struct proxy_auth_baton *pab = userdata;

  if (attempt >= 10)
    return 1;

  /* Else. */

  apr_cpystrn(username, pab->username, NE_ABUFSIZ);
  apr_cpystrn(password, pab->password, NE_ABUFSIZ);

  return 0;
}

#define RA_DAV_DESCRIPTION \
  N_("Module for accessing a repository via WebDAV (DeltaV) protocol.")

static const char *
ra_dav_get_description(void)
{
  return _(RA_DAV_DESCRIPTION);
}

static const char * const *
ra_dav_get_schemes(apr_pool_t *pool)
{
  static const char *schemes_no_ssl[] = { "http", NULL };
  static const char *schemes_ssl[] = { "http", "https", NULL };

  return ne_has_support(NE_FEATURE_SSL) ? schemes_ssl : schemes_no_ssl;
}

typedef struct neonprogress_baton_t
{
  svn_ra_progress_notify_func_t progress_func;
  void *progress_baton;
  apr_pool_t *pool;
} neonprogress_baton_t;

static void
ra_dav_neonprogress(void *baton, off_t progress, off_t total)
{
  const neonprogress_baton_t *neonprogress_baton = baton;
  if (neonprogress_baton->progress_func)
    {
      neonprogress_baton->progress_func(progress, total,
                                        neonprogress_baton->progress_baton,
                                        neonprogress_baton->pool);
    }
}


/* ### need an ne_session_dup to avoid the second gethostbyname
 * call and make this halfway sane. */


/* Parse URL into *URI, doing some sanity checking and initializing the port
   to a default value if it wasn't specified in URL.  */
static svn_error_t *
parse_url(ne_uri *uri, const char *url)
{
  if (ne_uri_parse(url, uri) 
      || uri->host == NULL || uri->path == NULL || uri->scheme == NULL)
    {
      ne_uri_free(uri);
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                              _("Malformed URL for repository"));
    }
  if (uri->port == 0)
    uri->port = ne_uri_defaultport(uri->scheme);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_dav__open(svn_ra_session_t *session,
                 const char *repos_URL,
                 const svn_ra_callbacks2_t *callbacks,
                 void *callback_baton,
                 apr_hash_t *config,
                 apr_pool_t *pool)
{
  apr_size_t len;
  ne_session *sess, *sess2;
  ne_uri uri = { 0 };
  svn_ra_dav__session_t *ras;
  int is_ssl_session;
  svn_boolean_t compression;
  svn_config_t *cfg;
  const char *server_group;
  char *itr;
  unsigned int neon_auth_types;
  neonprogress_baton_t *neonprogress_baton =
    apr_pcalloc(pool, sizeof(*neonprogress_baton));

  /* Sanity check the URI */
  SVN_ERR(parse_url(&uri, repos_URL));

  /* Can we initialize network? */
  if (ne_sock_init() != 0)
    {
      ne_uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, NULL,
                              _("Network socket initialization failed"));
    }

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  /* HACK!  Neon uses strcmp when checking for https, but RFC 2396 says
   * we should be using case-insensitive comparisons when checking for 
   * URI schemes.  To allow our users to use WeIrd CasE HttPS we force
   * the scheme to lower case before we pass it on to Neon, otherwise we
   * would crash later on when we assume Neon has set up its https stuff
   * but it really didn't. */
  for (itr = uri.scheme; *itr; ++itr)
    *itr = tolower(*itr);

  is_ssl_session = (strcasecmp(uri.scheme, "https") == 0);
  if (is_ssl_session)
    {
      if (ne_has_support(NE_FEATURE_SSL) == 0)
        {
          ne_uri_free(&uri);
          return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, NULL,
                                  _("SSL is not supported"));
        }
    }
  /* Create two neon session objects, and set their properties... */
  sess = ne_session_create(uri.scheme, uri.host, uri.port);
  sess2 = ne_session_create(uri.scheme, uri.host, uri.port);

  cfg = config ? apr_hash_get(config, 
                              SVN_CONFIG_CATEGORY_SERVERS,
                              APR_HASH_KEY_STRING) : NULL;
  if (cfg)
    server_group = svn_config_find_group(cfg, uri.host,
                                         SVN_CONFIG_SECTION_GROUPS, pool);
  else
    server_group = NULL;
  
  /* If there's a timeout or proxy for this URL, use it. */
  {
    const char *proxy_host;
    unsigned int proxy_port;
    const char *proxy_username;
    const char *proxy_password;
    int timeout;
    int debug;
    svn_error_t *err;
    
#ifdef SVN_NEON_0_26
    neon_auth_types = NE_AUTH_BASIC | NE_AUTH_DIGEST;
    if (is_ssl_session)
      neon_auth_types |= NE_AUTH_NEGOTIATE;
#endif
    err = get_server_settings(&proxy_host,
                              &proxy_port,
                              &proxy_username,
                              &proxy_password,
                              &timeout,
                              &debug,
                              &compression,
                              &neon_auth_types,
                              cfg,
                              uri.host,
                              pool);
    if (err)
      {
        ne_uri_free(&uri);
        return err;
      }

    if (debug)
      ne_debug_init(stderr, debug);

    if (proxy_host)
      {
        ne_session_proxy(sess, proxy_host, proxy_port);
        ne_session_proxy(sess2, proxy_host, proxy_port);

        if (proxy_username)
          {
            /* Allocate the baton in pool, not on stack, so it will
               last till whenever Neon needs it. */
            struct proxy_auth_baton *pab = apr_palloc(pool, sizeof(*pab));

            pab->username = proxy_username;
            pab->password = proxy_password ? proxy_password : "";
        
            ne_set_proxy_auth(sess, proxy_auth, pab);
            ne_set_proxy_auth(sess2, proxy_auth, pab);
          }
      }

    if (!timeout)
      timeout = DEFAULT_HTTP_TIMEOUT;
    ne_set_read_timeout(sess, timeout);
    ne_set_read_timeout(sess2, timeout);
  }

  /* make sure we will eventually destroy the session */
  apr_pool_cleanup_register(pool, sess, cleanup_session,
                            apr_pool_cleanup_null);
  apr_pool_cleanup_register(pool, sess2, cleanup_session,
                            apr_pool_cleanup_null);

  ne_set_useragent(sess, "SVN/" SVN_VERSION);
  ne_set_useragent(sess2, "SVN/" SVN_VERSION);

  /* clean up trailing slashes from the URL */
  len = strlen(uri.path);
  if (len > 1 && uri.path[len - 1] == '/')
    uri.path[len - 1] = '\0';

  /* Create and fill a session_baton. */
  ras = apr_pcalloc(pool, sizeof(*ras));
  ras->pool = pool;
  ras->url = svn_stringbuf_create(repos_URL, pool);
  /* copies uri pointer members, they get free'd in __close. */
  ras->root = uri; 
  ras->sess = sess;
  ras->sess2 = sess2;  
  ras->callbacks = callbacks;
  ras->callback_baton = callback_baton;
  ras->compression = compression;
  /* save config and server group in the auth parameter hash */
  svn_auth_set_parameter(ras->callbacks->auth_baton,
                         SVN_AUTH_PARAM_CONFIG, cfg);
  svn_auth_set_parameter(ras->callbacks->auth_baton,
                         SVN_AUTH_PARAM_SERVER_GROUP, server_group);

  /* make sure we eventually destroy the uri */
  apr_pool_cleanup_register(pool, &ras->root, cleanup_uri,
                            apr_pool_cleanup_null);

  /* note that ras->username and ras->password are still NULL at this
     point. */


  /* Register an authentication 'pull' callback with the neon sessions */
#ifdef SVN_NEON_0_26
  ne_add_server_auth(sess, neon_auth_types, request_auth, ras);
  ne_add_server_auth(sess2, neon_auth_types, request_auth, ras);
#else
  ne_set_server_auth(sess, request_auth, ras);
  ne_set_server_auth(sess2, request_auth, ras);
#endif

  /* Store our RA session baton in Neon's private data slot so we can
     get at it in functions that take only ne_session_t *sess
     (instead of the full svn_ra_dav__session_t *ras). */
  ne_set_session_private(sess, SVN_RA_NE_SESSION_ID, ras);
  ne_set_session_private(sess2, SVN_RA_NE_SESSION_ID, ras);

  if (is_ssl_session)
    {
      const char *authorities, *trust_default_ca;
      authorities = svn_config_get_server_setting(
            cfg, server_group,
            SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES,
            NULL);
      
      if (authorities != NULL)
        {
          char *files, *file, *last;
          files = apr_pstrdup(pool, authorities);

          while ((file = apr_strtok(files, ";", &last)) != NULL)
            {
              ne_ssl_certificate *ca_cert;
              files = NULL;
              ca_cert = ne_ssl_cert_read(file);
              if (ca_cert == NULL)
                {
                  return svn_error_createf
                    (SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                     _("Invalid config: unable to load certificate file '%s'"),
                     svn_path_local_style(file, pool));
                }
              ne_ssl_trust_cert(sess, ca_cert);
              ne_ssl_trust_cert(sess2, ca_cert);
            }
        }

      /* When the CA certificate or server certificate has
         verification problems, neon will call our verify function before
         outright rejection of the connection.*/
      ne_ssl_set_verify(sess, server_ssl_callback, ras);
      ne_ssl_set_verify(sess2, server_ssl_callback, ras);
      /* For client connections, we register a callback for if the server
         wants to authenticate the client via client certificate. */

      ne_ssl_provide_clicert(sess, client_ssl_callback, ras);
      ne_ssl_provide_clicert(sess2, client_ssl_callback, ras);

      /* See if the user wants us to trust "default" openssl CAs. */
      trust_default_ca = svn_config_get_server_setting(
               cfg, server_group,
               SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA,
               "true");

      if (strcasecmp(trust_default_ca, "true") == 0)
        {
          ne_ssl_trust_default_ca(sess);
          ne_ssl_trust_default_ca(sess2);
        }
    }
  neonprogress_baton->pool = pool;
  neonprogress_baton->progress_baton = callbacks->progress_baton;
  neonprogress_baton->progress_func = callbacks->progress_func;
  ne_set_progress(sess, ra_dav_neonprogress, neonprogress_baton);
  ne_set_progress(sess2, ra_dav_neonprogress, neonprogress_baton);
  session->priv = ras;

  return SVN_NO_ERROR;
}


static svn_error_t *svn_ra_dav__reparent(svn_ra_session_t *session,
                                         const char *url,
                                         apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;
  ne_uri uri = { 0 };

  SVN_ERR(parse_url(&uri, url));
  ne_uri_free(&ras->root);
  ras->root = uri;
  svn_stringbuf_set(ras->url, url);
  return SVN_NO_ERROR;
}

static svn_error_t *svn_ra_dav__get_repos_root(svn_ra_session_t *session,
                                               const char **url,
                                               apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;

  if (! ras->repos_root)
    {
      svn_string_t bc_relative;
      svn_stringbuf_t *url_buf;

      SVN_ERR(svn_ra_dav__get_baseline_info(NULL, NULL, &bc_relative,
                                            NULL, ras, ras->url->data,
                                            SVN_INVALID_REVNUM, pool));

      /* Remove as many path components from the URL as there are components
         in bc_relative. */
      url_buf = svn_stringbuf_dup(ras->url, pool);
      svn_path_remove_components
        (url_buf, svn_path_component_count(bc_relative.data));
      ras->repos_root = apr_pstrdup(ras->pool, url_buf->data);
    }

  *url = ras->repos_root;
  return SVN_NO_ERROR; 
}


static svn_error_t *svn_ra_dav__do_get_uuid(svn_ra_session_t *session,
                                            const char **uuid,
                                            apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;

  if (! ras->uuid)
    {
      svn_ra_dav_resource_t *rsrc;
      const char *lopped_path;
      const svn_string_t *uuid_propval;

      SVN_ERR(svn_ra_dav__search_for_starting_props(&rsrc, &lopped_path,
                                                    ras, ras->url->data,
                                                    pool));
      SVN_ERR(svn_ra_dav__maybe_store_auth_info(ras, pool));

      uuid_propval = apr_hash_get(rsrc->propset,
                                  SVN_RA_DAV__PROP_REPOSITORY_UUID,
                                  APR_HASH_KEY_STRING);
      if (uuid_propval == NULL)
        /* ### better error reporting... */
        return svn_error_create(APR_EGENERAL, NULL,
                                _("The UUID property was not found on the "
                                  "resource or any of its parents"));

      if (uuid_propval && (uuid_propval->len > 0))
        ras->uuid = apr_pstrdup(ras->pool, uuid_propval->data); /* cache */
      else
        return svn_error_create
          (SVN_ERR_RA_NO_REPOS_UUID, NULL,
           _("Please upgrade the server to 0.19 or later"));
    }

  *uuid = ras->uuid;
  return SVN_NO_ERROR; 
}


/* A callback of type ne_create_request_fn;  called whenever neon
   creates a request. */
static void 
create_request_hook(ne_request *req,
                    void *userdata,
                    const char *method,
                    const char *requri)
{
  struct lock_request_baton *lrb = userdata;

  /* If a PROPFIND is happening, then remember the
     http method. */
  if ((strcmp(method, "PROPFIND") == 0))
    {
      lrb->method = apr_pstrdup(lrb->pool, method);
      lrb->request = req;
    }
}



/* A callback of type ne_pre_send_fn;  called whenever neon is just
   about to send a request. */
static void
pre_send_hook(ne_request *req,
              void *userdata,
              ne_buffer *header)
{
  struct lock_request_baton *lrb = userdata;

  if (! lrb->method)
    return;

  /* Possibly attach some custom headers to the request. */

  if ((strcmp(lrb->method, "PROPFIND") == 0))
    {
      /* Possibly add an X-SVN-Option: header indicating that the lock
         is being stolen.  */
      if (lrb->force)
        {
          char *hdr = apr_psprintf(lrb->pool, "%s: %s\r\n",
                                   SVN_DAV_OPTIONS_HEADER,
                                   SVN_DAV_OPTION_LOCK_STEAL);
          ne_buffer_zappend(header, hdr);
        }

      /* If we have a working-revision of the file, send it so that
         svn_fs_lock() can do an out-of-dateness check. */
      if (SVN_IS_VALID_REVNUM(lrb->current_rev))
        {
          char *buf = apr_psprintf(lrb->pool, "%s: %ld\r\n",
                                   SVN_DAV_VERSION_NAME_HEADER,
                                   lrb->current_rev);
          ne_buffer_zappend(header, buf);
        }
    }

  /* Register a response handler capable of parsing <D:error> */
  lrb->error_parser = ne_xml_create();
  svn_ra_dav__add_error_handler(req, lrb->error_parser,
                                &(lrb->err), lrb->pool);
}

/* A callback of type ne_post_send_fn;  called after neon has sent a
   request and received a response header back. */
static int
post_send_hook(ne_request *req,
               void *userdata,
               const ne_status *status)
{
  struct lock_request_baton *lrb = userdata;

  if (! lrb->method)
    return NE_OK;

  if ((strcmp(lrb->method, "PROPFIND") == 0))
    {
      const char *val;

      val = ne_get_response_header(req, SVN_DAV_CREATIONDATE_HEADER);
      if (val)
        {
          svn_error_t *err = svn_time_from_cstring(&(lrb->creation_date),
                                                   val, lrb->pool);

          NE_DEBUG(NE_DBG_HTTP, "got cdate %s for %s request...\n",
                   val, lrb->method);

          if (err)
            {
              svn_error_clear(err);
              lrb->creation_date = 0;
              /* ### Should we return NE_RETRY in this case?  And if
                 ### we were to do that, would we also set *status
                 ### and call ne_set_error? */
            }
        }

      val = ne_get_response_header(req, SVN_DAV_LOCK_OWNER_HEADER);
      if (val)
        lrb->lock_owner = apr_pstrdup(lrb->pool, val);
    }

  return NE_OK;
}


static void
setup_neon_request_hook(svn_ra_dav__session_t *ras)
{
  /* We need to set up the lock callback once and only once per neon
     session creation. */

  if (! ras->lrb)
    {
      struct lock_request_baton *lrb;
      /* Build context for neon callbacks and then register them. */
      lrb = apr_pcalloc(ras->pool, sizeof(*lrb));

      ne_hook_create_request(ras->sess, create_request_hook, lrb);
      ne_hook_pre_send(ras->sess, pre_send_hook, lrb);
      ne_hook_post_send(ras->sess, post_send_hook, lrb);

      lrb->pool = ras->pool;
      ras->lrb = lrb;
    }
}

static const svn_ra_dav__xml_elm_t lock_elements[] =
{
  { "DAV:", "prop", ELEM_prop, 0 },
  { "DAV:", "lockdiscovery", ELEM_lock_discovery, 0 },
  { "DAV:", "activelock", ELEM_lock_activelock, 0 },
  { "DAV:", "locktype", ELEM_lock_type, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "lockscope", ELEM_lock_scope, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "depth", ELEM_lock_depth, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "owner", ELEM_lock_owner, SVN_RA_DAV__XML_COLLECT },
  { "DAV:", "timeout", ELEM_lock_timeout, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "locktoken", ELEM_lock_token, 0 },
  { "DAV:", "href", ELEM_lock_href, SVN_RA_DAV__XML_CDATA },
  { "", "", ELEM_unknown, SVN_RA_DAV__XML_COLLECT },
  { NULL }
};

typedef struct
{
  svn_stringbuf_t *cdata;
  apr_pool_t *pool;

  svn_stringbuf_t *owner;
  svn_stringbuf_t *timeout;
  svn_stringbuf_t *depth;
  svn_stringbuf_t *token;
} lock_baton_t;

static svn_error_t *
lock_start_element(int *elem, void *baton, int parent,
                   const char *nspace, const char *name, const char **atts)
{
  lock_baton_t *b = baton;
  const svn_ra_dav__xml_elm_t *elm =
    svn_ra_dav__lookup_xml_elem(lock_elements, nspace, name);

  if (! elm)
    {
      *elem = NE_XML_DECLINE;
      return SVN_NO_ERROR;
    }

  /* collect interesting element contents */
  /* owner, href inside locktoken, depth, timeout */
  switch (elm->id)
    {
    case ELEM_lock_owner:
    case ELEM_lock_timeout:
    case ELEM_lock_depth:
      b->cdata = svn_stringbuf_create("", b->pool);
      break;

    case ELEM_lock_href:
      if (parent == ELEM_lock_token)
        b->cdata = svn_stringbuf_create("", b->pool);
      break;

    default:
      b->cdata = NULL;
    }

  *elem = elm->id;
  return SVN_NO_ERROR;
}

static svn_error_t *
lock_end_element(void *baton, int state, const char *nspace, const char *name)
{
  lock_baton_t *b = baton;

  if (b->cdata)
    switch (state)
      {
      case ELEM_lock_owner:
        b->owner = b->cdata;
        break;

      case ELEM_lock_timeout:
        b->timeout = b->cdata;
        break;

      case ELEM_lock_depth:
        b->depth = b->cdata;
        break;

      case ELEM_lock_href:
        b->token = b->cdata;
        break;
      }

  b->cdata = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
lock_cdata(void *baton, int state, const char *cdata, size_t len)
{
  lock_baton_t *b = baton;

  if (b->cdata)
    svn_stringbuf_appendbytes(b->cdata, cdata, len);

  return SVN_NO_ERROR;
}


static svn_error_t *
lock_from_baton(svn_lock_t **lock,
                svn_ra_dav__request_t *req,
                const char *path,
                lock_baton_t *lrb, apr_pool_t *pool)
{
  const char *val;
  svn_lock_t *lck = svn_lock_create(pool);

  val = ne_get_response_header(req->req, SVN_DAV_CREATIONDATE_HEADER);
  if (val)
    SVN_ERR_W(svn_time_from_cstring(&(lck->creation_date), val, pool),
              _("Invalid creation date header value in response."));

  val = ne_get_response_header(req->req, SVN_DAV_LOCK_OWNER_HEADER);
  if (val)
    lck->owner = apr_pstrdup(pool, val);
  if (lrb->owner)
    lck->comment = lrb->owner->data;
  if (lrb->token)
    lck->token = lrb->token->data;
  if (path)
    lck->path = path;
  if (lrb->timeout)
    {
      const char *timeout_str = lrb->timeout->data;

      if (strcmp(timeout_str, "Infinite") != 0)
        {
          if (strncmp("Second-", timeout_str, strlen("Second-")) == 0)
            {
              int time_offset = atoi(&(timeout_str[7]));

              lck->expiration_date = lck->creation_date
                + apr_time_from_sec(time_offset);
            }
          else
            return svn_error_create(SVN_ERR_RA_DAV_RESPONSE_HEADER_BADNESS,
                                    NULL, _("Invalid timeout value."));
        }
      else
        lck->expiration_date = 0;
    }
  *lock = lck;

  return SVN_NO_ERROR;
}

static svn_error_t *
do_lock(svn_lock_t **lock,
        svn_ra_session_t *session,
        const char *path,
        const char *comment,
        svn_boolean_t force,
        svn_revnum_t current_rev,
        apr_pool_t *pool)
{
  svn_ra_dav__request_t *req;
  svn_stringbuf_t *body;
  ne_uri uri;
  int code;
  const char *url;
  svn_string_t fs_path;
  ne_xml_parser *lck_parser;
  svn_ra_dav__session_t *ras = session->priv;
  lock_baton_t *lrb = apr_pcalloc(pool, sizeof(*lrb));

  /* To begin, we convert the incoming path into an absolute fs-path. */
  url = svn_path_url_add_component(ras->url->data, path, pool);
  SVN_ERR(svn_ra_dav__get_baseline_info(NULL, NULL, &fs_path, NULL, ras,
                                        url, SVN_INVALID_REVNUM, pool));


  ne_uri_parse(url, &uri);
  req = svn_ra_dav__request_create(ras, "LOCK", uri.path, pool);
  ne_uri_free(&uri);

  lrb->pool = pool;
  lck_parser = svn_ra_dav__xml_parser_create
    (req, lock_start_element, lock_cdata, lock_end_element, lrb);

  svn_ra_dav__add_response_body_reader(req, ne_accept_2xx,
                                       ne_xml_parse_v, lck_parser);

  body = svn_stringbuf_createf
    (req->pool,
     "<?xml version=\"1.0\" encoding=\"utf-8\" ?>" DEBUG_CR
     "<D:lockinfo xmlns:D=\"DAV:\">" DEBUG_CR
     " <D:lockscope><D:exclusive /></D:lockscope>" DEBUG_CR
     " <D:locktype><D:write /></D:locktype>" DEBUG_CR
     "%s" /* maybe owner */
     "</D:lockinfo>",
     (comment
      ? apr_psprintf(req->pool, " <D:owner>%s</D:owner>" DEBUG_CR, comment)
      : ""));

  /*### Attach a lock response reader to the request */

  ne_add_request_header(req->req, "Depth", "0");
  ne_add_request_header(req->req, "Timeout", "Infinite");
  ne_add_request_header(req->req, "Content-Type", "text/xml");
  ne_set_request_body_buffer(req->req, body->data, body->len);
  if (force)
    ne_add_request_header(req->req,
                          SVN_DAV_OPTIONS_HEADER, SVN_DAV_OPTION_LOCK_STEAL);
  if (SVN_IS_VALID_REVNUM(current_rev))
    ne_add_request_header(req->req,
                          SVN_DAV_VERSION_NAME_HEADER,
                          apr_psprintf(pool, "%ld", current_rev));

  SVN_ERR(svn_ra_dav__request_dispatch(&code, req, 200, 0, pool));

  /*###FIXME: we never verified whether we have received back the type
    of lock we requested: was it shared/exclusive? was it write/otherwise?
    How many did we get back? Only one? */
  SVN_ERR(lock_from_baton(lock, req, fs_path.data, lrb, pool));

  svn_ra_dav__request_destroy(req);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_dav__lock(svn_ra_session_t *session,
                 apr_hash_t *path_revs,
                 const char *comment,
                 svn_boolean_t force,
                 svn_ra_lock_callback_t lock_func, 
                 void *lock_baton,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_ra_dav__session_t *ras = session->priv;
  svn_error_t *ret_err = NULL;

  /* ### TODO for 1.3: Send all the locks over the wire at once.  This
     loop is just a temporary shim. */
  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_lock_t *lock;
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;
      svn_error_t *err, *callback_err = NULL;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      err = do_lock(&lock, session, path, comment, force, *revnum, iterpool);

      if (err && !SVN_ERR_IS_LOCK_ERROR(err))
        {
          ret_err = err;
          goto departure;
        }

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE, err ? NULL : lock,
                                 err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        {
          ret_err = callback_err;
          goto departure;
        }

    }

  svn_pool_destroy(iterpool);

 departure:
  return svn_ra_dav__maybe_store_auth_info_after_result(ret_err, ras, pool);
}


/* ###TODO for 1.3: Send all lock tokens to the server at once. */
static svn_error_t *
do_unlock(svn_ra_session_t *session,
          const char *path,
          const char *token,
          svn_boolean_t force,
          apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;
  int rv;
  const char *url;
  const char *url_path;
  struct ne_lock *nlock;

  apr_hash_t *extra_headers = apr_hash_make(pool);

  /* Make a neon lock structure containing token and full URL to unlock. */
  nlock = ne_lock_create();
  url = svn_path_url_add_component(ras->url->data, path, pool);  
  if ((rv = ne_uri_parse(url, &(nlock->uri))))
    {
      ne_lock_destroy(nlock);
      return svn_ra_dav__convert_error(ras->sess, _("Failed to parse URI"),
                                       rv, pool);
    }

  url_path = apr_pstrdup(pool, nlock->uri.path);
  ne_lock_destroy(nlock);
  /* In the case of 'force', we might not have a token at all.
     Unfortunately, ne_unlock() insists on sending one, and mod_dav
     insists on having a valid token for UNLOCK requests.  That means
     we need to fetch the token. */
  if (! token)
    {
      svn_lock_t *lock;

      SVN_ERR(svn_ra_dav__get_lock(session, &lock, path, pool));
      if (! lock)
        return svn_error_createf(SVN_ERR_RA_NOT_LOCKED, NULL,
                                 _("'%s' is not locked in the repository"),
                                 path);
      token = lock->token;
    }



  apr_hash_set(extra_headers, "Lock-Token", APR_HASH_KEY_STRING,
               apr_psprintf(pool, "<%s>", token));
  if (force)
    apr_hash_set(extra_headers, SVN_DAV_OPTIONS_HEADER, APR_HASH_KEY_STRING,
                 SVN_DAV_OPTION_LOCK_BREAK);

  return svn_ra_dav__simple_request(NULL, ras, "UNLOCK", url_path,
                                    extra_headers, NULL, 204, 0, pool);
}


static svn_error_t *
svn_ra_dav__unlock(svn_ra_session_t *session,
                   apr_hash_t *path_tokens,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func, 
                   void *lock_baton,
                   apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_ra_dav__session_t *ras = session->priv;
  svn_error_t *ret_err = NULL;

  /* ### TODO for 1.3: Send all the lock tokens over the wire at once.
        This loop is just a temporary shim. */
  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      const char *token;
      svn_error_t *err, *callback_err = NULL; 

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      /* Since we can't store NULL values in a hash, we turn "" to
         NULL here. */
      if (strcmp(val, "") != 0)
        token = val;
      else
        token = NULL;

      err = do_unlock(session, path, token, force, iterpool);

      if (err && !SVN_ERR_IS_UNLOCK_ERROR(err))
        {
          ret_err = err;
          goto departure;
        }

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        {
          ret_err = callback_err;
          goto departure;
        }
    }

  svn_pool_destroy(iterpool);

 departure:
  return svn_ra_dav__maybe_store_auth_info_after_result(ret_err, ras, pool);
}


/* A context for lock_receiver(). */
struct receiver_baton
{
  /* Set this if something goes wrong. */
  svn_error_t *err;
  
  /* The thing being retrieved and assembled. */
  svn_lock_t *lock;

  /* Our RA session. */
  svn_ra_dav__session_t *ras;

  /* The baton used by the handle_creation_date() callback */
  struct lock_request_baton *lrb;
  
  /* The absolute FS path that we're querying. */
  const char *fs_path;

  /* A place to allocate the lock. */
  apr_pool_t *pool;
};


/* A callback of type ne_lock_result;  called by ne_lock_discover(). */
static void
lock_receiver(void *userdata,
              const struct ne_lock *lock,
#ifdef SVN_NEON_0_26
              const ne_uri *uri,
#else
              const char *uri,
#endif
              const ne_status *status)
{
  struct receiver_baton *rb = userdata;

  if (lock)
    {
      /* The post_send hook has not run at this stage; so grab the 
         response headers early.  As Joe Orton explains in Issue
         #2297: "post_send hooks run much later than the name might
         suggest.  I've noted another API change for a future neon
         release to make that easier." */
      if (post_send_hook(rb->lrb->request, rb->lrb, 
                         ne_get_status(rb->lrb->request)))
        {
          return;
        }

      if (!rb->lrb->lock_owner || !rb->lrb->creation_date)
        {
          rb->err = svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                     _("Incomplete lock data returned"));
          return;
        }
      /* Convert the ne_lock into an svn_lock_t. */
      rb->lock = svn_lock_create(rb->pool);
      rb->lock->token = apr_pstrdup(rb->pool, lock->token);
      rb->lock->path = rb->fs_path;
      if (lock->owner)
        rb->lock->comment = apr_pstrdup(rb->pool, lock->owner);
      rb->lock->owner = apr_pstrdup(rb->pool, rb->lrb->lock_owner);
      rb->lock->creation_date = rb->lrb->creation_date;
      if (lock->timeout == NE_TIMEOUT_INFINITE)
        rb->lock->expiration_date = 0;
      else if (lock->timeout > 0)
        rb->lock->expiration_date = rb->lock->creation_date + 
                                    apr_time_from_sec(lock->timeout);      
    }
  else
    {
      /* There's no lock... is that because the path isn't locked?  Or
         because of a real error?  */
      if (status->code != 404)
        rb->err = svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                                   status->reason_phrase);
    }
}


svn_error_t *
svn_ra_dav__get_lock(svn_ra_session_t *session,
                     svn_lock_t **lock,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;
  int rv;
  const char *url;
  struct receiver_baton *rb;
  svn_string_t fs_path;
  svn_error_t *err;
  ne_uri uri;

  /* To begin, we convert the incoming path into an absolute fs-path. */
  url = svn_path_url_add_component(ras->url->data, path, pool);  

  err = svn_ra_dav__get_baseline_info(NULL, NULL, &fs_path, NULL, ras,
                                      url, SVN_INVALID_REVNUM, pool);
  SVN_ERR(svn_ra_dav__maybe_store_auth_info_after_result(err, ras, pool));

  /* Build context for neon callbacks and then register them. */
  setup_neon_request_hook(ras);
  memset((ras->lrb), 0, sizeof(*ras->lrb));
  ras->lrb->pool = pool;

  /* Build context for the lock_receiver() callback. */
  rb = apr_pcalloc(pool, sizeof(*rb));
  rb->pool = pool;
  rb->ras = ras;
  rb->lrb = ras->lrb;
  rb->fs_path = fs_path.data;

  /* Ask neon to "discover" the lock (presumably by doing a PROPFIND
     for the DAV:supportedlock property). */
  
  /* ne_lock_discover wants just the path, so parse it out of the url. */
  if (ne_uri_parse(url, &uri) == 0)
    {
      url = apr_pstrdup(pool, uri.path);
      ne_uri_free(&uri);
    }

  rv = ne_lock_discover(ras->sess, url, lock_receiver, rb);

  /* Did we get a <D:error> response? */
  if (ras->lrb->err)
    {
      if (ras->lrb->error_parser)
        ne_xml_destroy(ras->lrb->error_parser);
      return ras->lrb->err;
    }

  /* Did lock_receiver() generate an error? */
  if (rb->err)
    {
      if (ras->lrb->error_parser)
        ne_xml_destroy(ras->lrb->error_parser);
      return rb->err;
    }

  /* Did we get some other sort of neon error? */
  if (rv)
    {
      if (ras->lrb->error_parser)
        ne_xml_destroy(ras->lrb->error_parser);
      return svn_ra_dav__convert_error(ras->sess,
                                       _("Failed to fetch lock information"),
                                       rv, pool);
    }  

  /* Free neon things. */
  if (ras->lrb->error_parser)
    ne_xml_destroy(ras->lrb->error_parser);
  
  *lock = rb->lock;
  return SVN_NO_ERROR;
}




static const svn_version_t *
ra_dav_version(void)
{
  SVN_VERSION_BODY;
}

static const svn_ra__vtable_t dav_vtable = {
  ra_dav_version,
  ra_dav_get_description,
  ra_dav_get_schemes,
  svn_ra_dav__open,
  svn_ra_dav__reparent,
  svn_ra_dav__get_latest_revnum,
  svn_ra_dav__get_dated_revision,
  svn_ra_dav__change_rev_prop,
  svn_ra_dav__rev_proplist,
  svn_ra_dav__rev_prop,
  svn_ra_dav__get_commit_editor,
  svn_ra_dav__get_file,
  svn_ra_dav__get_dir,
  svn_ra_dav__do_update,
  svn_ra_dav__do_switch,
  svn_ra_dav__do_status,
  svn_ra_dav__do_diff,
  svn_ra_dav__get_log,
  svn_ra_dav__do_check_path,
  svn_ra_dav__do_stat,
  svn_ra_dav__do_get_uuid,
  svn_ra_dav__get_repos_root,
  svn_ra_dav__get_locations,
  svn_ra_dav__get_file_revs,
  svn_ra_dav__lock,
  svn_ra_dav__unlock,
  svn_ra_dav__get_lock,
  svn_ra_dav__get_locks,
  svn_ra_dav__replay,
};

svn_error_t *
svn_ra_dav__init(const svn_version_t *loader_version,
                 const svn_ra__vtable_t **vtable,
                 apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };

  SVN_ERR(svn_ver_check_list(ra_dav_version(), checklist));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf
        (SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_dav"),
         loader_version->major);
    }

  *vtable = &dav_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for the 1.1 and before API. */
#define NAME "ra_dav"
#define DESCRIPTION RA_DAV_DESCRIPTION
#define VTBL dav_vtable
#define INITFUNC svn_ra_dav__init
#define COMPAT_INITFUNC svn_ra_dav_init
#include "../libsvn_ra/wrapper_template.h"
