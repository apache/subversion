/*
 * session.c :  routines for maintaining sessions state (to the DAV server)
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>

#include <ne_socket.h>
#include <ne_request.h>
#include <ne_uri.h>
#include <ne_auth.h>

#include "svn_error.h"
#include "svn_ra.h"
#include "svn_config.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_ra_dav.h"
#include "ra_dav.h"


/** Retrieve value corresponding to @a option_name for a given
 *  @a server_group in @a cfg , or return @a default_value if none is found.
 *
 *  The config will first be checked for a default, then will be checked for
 *  an override in a server group.
 */
static const char*
get_server_setting(svn_config_t *cfg,
                   const char* server_group,
                   const char* option_name,
                   const char* default_value)
{
  const char* retval;
  svn_config_get(cfg, &retval, "default", option_name, default_value);
  if (server_group)
    {
      svn_config_get(cfg, &retval, server_group, option_name, retval);
    }
  return retval;
}

/** Retrieve value into @a result_value corresponding to @a option_name for a
 *  given @a server_group in @a cfg, or return @a default_value if none is
 *  found.
 *
 *  The config will first be checked for a default, then will be checked for
 *  an override in a server group. If the value found is not a valid integer,
 *  a @c svn_error_t* will be returned.
 */
static svn_error_t*
get_server_setting_int(svn_config_t *cfg,
                       const char *server_group,
                       const char *option_name,
                       apr_int64_t default_value,
                       apr_int64_t *result_value,
                       apr_pool_t *pool)
{
  const char* tmp_value;
  char* end_pos;
  char *default_value_str = apr_psprintf(pool,
                                         "%" APR_INT64_T_FMT,
                                         default_value); 
  tmp_value = get_server_setting(cfg, server_group,
                                 option_name, default_value_str);

  /* read tmp_value as an int now */
  *result_value = apr_strtoi64(tmp_value, &end_pos, 0);
  
  if (*end_pos != 0) 
    {
      return svn_error_create(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                              "non-integer in integer option");
    }
  return NULL;
}


/* a cleanup routine attached to the pool that contains the RA session
   baton. */
static apr_status_t cleanup_session(void *sess)
{
  ne_session_destroy(sess);
  return APR_SUCCESS;
}


/* A neon-session callback to 'pull' authentication data when
   challenged.  In turn, this routine 'pulls' the data from the client
   callbacks if needed.  */
static int request_auth(void *userdata, const char *realm, int attempt,
                        char *username, char *password)
{
  svn_error_t *err;
  svn_ra_session_t *ras = userdata;
  svn_auth_cred_simple_t *creds;  

  /* No auth_baton?  Give up. */
  if (! ras->callbacks->auth_baton)
    return -1;

  if (attempt == 0)
    err = svn_auth_first_credentials ((void **) &creds,
                                      &(ras->auth_iterstate), 
                                      SVN_AUTH_CRED_SIMPLE,
                                      ras->callbacks->auth_baton,
                                      ras->pool);
  else /* attempt > 0 */
    err = svn_auth_next_credentials ((void **) &creds,
                                     ras->auth_iterstate,
                                     ras->pool);
  if (err || ! creds)
    return -1;
  
  /* ### silently truncates username/password to 256 chars. */
  apr_cpystrn(username, creds->username, NE_ABUFSIZ);
  apr_cpystrn(password, creds->password, NE_ABUFSIZ);

  return 0;
}



/* retieve ssl server CA failure overrides (if any) from servers
   config */
static svn_error_t *
server_ssl_file_first_credentials(void **credentials,
				  void **iter_baton,
				  void *provider_baton,
				  apr_hash_t *parameters,
				  apr_pool_t *pool)
{
  const char *temp_setting;
  svn_ra_session_t *ras = provider_baton;
  ne_ssl_dname *server = apr_hash_get(parameters,
				      SVN_AUTH_PARAM_SSL_SERVER_DNAME,
				      APR_HASH_KEY_STRING);
  assert(server);
  svn_auth_cred_server_ssl_t *cred =
    apr_palloc(pool, sizeof(svn_auth_cred_server_ssl_t));

  cred->failures_allow = 0;
  temp_setting = get_server_setting(ras->cfg, ras->server_group, 
				    "ignore-ssl-unknown-ca", NULL);
  cred->failures_allow = temp_setting ? NE_SSL_UNKNOWNCA : 0;
  temp_setting = get_server_setting(ras->cfg, ras->server_group, 
				    "ignore-ssl-host-mismatch", NULL);
  cred->failures_allow |= temp_setting ? NE_SSL_CNMISMATCH : 0;
  temp_setting = get_server_setting(ras->cfg, ras->server_group, 
				    "ignore-ssl-invalid-date", NULL);
  cred->failures_allow |= temp_setting ? (NE_SSL_NOTYETVALID|NE_SSL_EXPIRED) : 0;
  return NULL;
}

/* retrieve and load the ssl client certificate file from servers
   config */
static svn_error_t *
client_ssl_cert_file_first_credentials(void **credentials,
				       void **iter_baton,
				       void *provider_baton,
				       apr_hash_t *parameters,
				       apr_pool_t *pool)
{
  /* does nothing so far */
  *credentials = NULL;
  return NULL;
}

/* retrieve and load a password for a client certificate from servers
   file */
static svn_error_t *
client_ssl_pw_file_first_credentials(void **credentials,
				     void **iter_baton,
				     void *provider_baton,
				     apr_hash_t *parameters,
				     apr_pool_t *pool)
{
  /* does nothing so far */
  *credentials = NULL;
  return NULL;
}

static const svn_auth_provider_t server_ssl_file_provider = 
  {
    SVN_AUTH_CRED_SERVER_SSL,
    &server_ssl_file_first_credentials,
    NULL,
    NULL
  };

static const svn_auth_provider_t client_ssl_cert_file_provider =
  {
    SVN_AUTH_CRED_CLIENT_SSL,
    client_ssl_cert_file_first_credentials,
    NULL,
    NULL
  };

static const svn_auth_provider_t client_ssl_pw_file_provider =
  {
    SVN_AUTH_CRED_CLIENT_PASS_SSL,
    client_ssl_pw_file_first_credentials,
    NULL,
    NULL
  };


void 
svn_ra_dav_get_ssl_server_file_provider (const svn_auth_provider_t **provider,
					 void **provider_baton,
					 apr_pool_t *pool)
{
  *provider = &server_ssl_file_provider;
}

void 
svn_ra_dav_get_ssl_client_file_provider (const svn_auth_provider_t **provider,
					 void **provider_baton,
					 apr_pool_t *pool)
{
  *provider = &client_ssl_cert_file_provider;
}

void
svn_ra_dav_get_ssl_client_password_file_provider (const svn_auth_provider_t **provider,
						  void **provider_baton,
						  apr_pool_t *pool)
{
  *provider = &client_ssl_pw_file_provider;
}


/* A neon-session callback to validate the SSL certificate when the CA
   is unknown or there are other SSL certificate problems. */
static int
server_ssl_callback(void *userdata,
		    int failures,
		    const ne_ssl_certificate *cert)
{
  svn_ra_session_t *ras = userdata;
  svn_auth_cred_server_ssl_t *credentials;
  svn_auth_iterstate_t *state;
  apr_pool_t *pool;
  svn_error_t *error;
  int failures_allowed;

  apr_pool_create(&pool, ras->pool);
  error = svn_auth_first_credentials((void**)&credentials, &state,
				     SVN_AUTH_CRED_SERVER_SSL,
				     ras->callbacks->auth_baton,
				     pool);
  failures_allowed = (credentials) ? credentials->failures_allow : 0;
  apr_pool_destroy(pool);

  return (failures & ~failures_allowed);
}

static int
client_ssl_keypw_callback(void *userdata, char *pwbuf, size_t len)
{
  svn_ra_session_t *ras = userdata;
  svn_auth_cred_client_ssl_pass_t *credentials;
  svn_auth_iterstate_t *state;
  apr_pool_t *pool;
  svn_error_t *error;

  apr_pool_create(&pool, ras->pool);
  error = svn_auth_first_credentials((void**)&credentials, &state,
				     SVN_AUTH_CRED_CLIENT_PASS_SSL,
				     ras->callbacks->auth_baton,
				     pool);
  if (credentials)
    {
      strncpy(pwbuf, credentials->password, len);
    }
  apr_pool_destroy(pool);
  return (credentials == NULL);
}

static void
client_ssl_callback(void *userdata, ne_session *sess,
		    const ne_ssl_dname *server)
{
  svn_ra_session_t *ras = userdata;
  svn_auth_cred_client_ssl_t *credentials;
  svn_auth_iterstate_t *state;
  apr_pool_t *pool;
  svn_error_t *error;

  apr_pool_create(&pool, ras->pool);
  error = svn_auth_first_credentials((void**)&credentials, &state,
				     SVN_AUTH_CRED_CLIENT_SSL,
				     ras->callbacks->auth_baton,
				     pool);
  apr_pool_destroy(pool);
}

/* Set *PROXY_HOST, *PROXY_PORT, *PROXY_USERNAME, *PROXY_PASSWORD,
 * *TIMEOUT_SECONDS and *NEON_DEBUG to the information for REQUESTED_HOST,
 * allocated in POOL, if there is any applicable information.  If there is
 * no applicable information or if there is an error, then set *PROXY_PORT
 * to (unsigned int) -1, *TIMEOUT_SECONDS and *NEON_DEBUG to zero, and the
 * rest to NULL.  This function can return an error, so before checking any
 * values, check the error return value.
 */
static svn_error_t *get_server_settings(const char **proxy_host,
                                        unsigned int *proxy_port,
                                        const char **proxy_username,
                                        const char **proxy_password,
                                        int *timeout_seconds,
                                        int *neon_debug,
                                        svn_boolean_t *compression,
                                        svn_config_t *cfg,
                                        const char *requested_host,
                                        apr_pool_t *pool)
{
  const char *exceptions;
  const char *port_str, *timeout_str, *server_group, *debug_str, *compress_str;
  svn_boolean_t is_exception = FALSE;
  /* If we find nothing, default to nulls. */
  *proxy_host     = NULL;
  *proxy_port     = (unsigned int) -1;
  *proxy_username = NULL;
  *proxy_password = NULL;
  port_str        = NULL;
  timeout_str     = NULL;
  debug_str       = NULL;
  compress_str    = "yes";

  /* If there are defaults, use them, but only if the requested host
     is not one of the exceptions to the defaults. */
  svn_config_get(cfg, &exceptions, SVN_CONFIG_SECTION_GLOBAL, 
                 SVN_CONFIG_OPTION_HTTP_PROXY_EXCEPTIONS, NULL);
  if (exceptions)
    {
      apr_array_header_t *l = svn_cstring_split (exceptions, ",", TRUE, pool);
      is_exception = svn_cstring_match_glob_list (requested_host, l);
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
      svn_config_get(cfg, &compress_str, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_HTTP_COMPRESSION, NULL);
      svn_config_get(cfg, &debug_str, SVN_CONFIG_SECTION_GLOBAL, 
                     SVN_CONFIG_OPTION_NEON_DEBUG_MASK, NULL);
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
      svn_config_get(cfg, &compress_str, server_group, 
                     SVN_CONFIG_OPTION_HTTP_COMPRESSION, compress_str);
      svn_config_get(cfg, &debug_str, server_group, 
                     SVN_CONFIG_OPTION_NEON_DEBUG_MASK, debug_str);
    }

  /* Special case: convert the port value, if any. */
  if (port_str)
    {
      char *endstr;
      const long int port = strtol(port_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                "illegal character in proxy port number");
      if (port < 0)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                "negative proxy port number");
      if (port > 65535)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                "proxy port number greater than maximum TCP "
                                "port number 65535");
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
                                "illegal character in timeout value");
      if (timeout < 0)
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                                "negative timeout value");
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
                                "illegal character in debug mask value");

      *neon_debug = debug;
    }
  else
    *neon_debug = 0;

  if (compress_str)
    *compression = (strcasecmp(compress_str, "yes") == 0) ? TRUE : FALSE;
  else
    *compression = TRUE;

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


/* ### need an ne_session_dup to avoid the second gethostbyname
 * call and make this halfway sane. */


static svn_error_t *
svn_ra_dav__open (void **session_baton,
                  const char *repos_URL,
                  const svn_ra_callbacks_t *callbacks,
                  void *callback_baton,
                  apr_hash_t *config,
                  apr_pool_t *pool)
{
  apr_size_t len;
  ne_session *sess, *sess2;
  ne_uri uri = { 0 };
  svn_ra_session_t *ras;
  int is_ssl_session;
  svn_boolean_t compression;
  svn_config_t *cfg;
  const char *server_group;

  /* Sanity check the URI */
  if (ne_uri_parse(repos_URL, &uri) 
      || uri.host == NULL || uri.path == NULL)
    {
      ne_uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                              "illegal URL for repository");
    }

  /* Can we initialize network? */
  if (ne_sock_init() != 0)
    {
      ne_uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, NULL,
                              "network socket initialization failed");
    }

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  is_ssl_session = (strcasecmp(uri.scheme, "https") == 0);
  if (is_ssl_session)
    {
      if (ne_supports_ssl() == 0)
        {
          ne_uri_free(&uri);
          return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, NULL,
                                  "SSL is not supported");
        }
    }
#if 0
  else
    {
      /* accept server-requested TLS upgrades... useless feature
       * currently since there is no server support yet. */
      (void) ne_set_accept_secure_upgrade(sess, 1);
    }
#endif

  if (uri.port == 0)
    {
      uri.port = ne_uri_defaultport(uri.scheme);
    }

  /* Create two neon session objects, and set their properties... */
  sess = ne_session_create(uri.scheme, uri.host, uri.port);
  sess2 = ne_session_create(uri.scheme, uri.host, uri.port);

  cfg = config ? apr_hash_get (config, 
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
    
    err = get_server_settings(&proxy_host,
                              &proxy_port,
                              &proxy_username,
                              &proxy_password,
                              &timeout,
                              &debug,
                              &compression,
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
            struct proxy_auth_baton *pab = apr_palloc(pool, sizeof (*pab));

            pab->username = proxy_username;
            pab->password = proxy_password ? proxy_password : "";
        
            ne_set_proxy_auth(sess, proxy_auth, pab);
            ne_set_proxy_auth(sess2, proxy_auth, pab);
          }
      }

    if (timeout)
      {
        ne_set_read_timeout(sess, timeout);
        ne_set_read_timeout(sess2, timeout);
      }
  }

  if (is_ssl_session)
    {
      const char *authorities_file;
      authorities_file = get_server_setting(cfg, server_group,
                                            "ssl-authorities-file", NULL);
      
      if (authorities_file != NULL)
        {
          ne_ssl_load_ca(sess, authorities_file);
          ne_ssl_load_ca(sess2, authorities_file);
        }

      /* When the CA certificate or server certificate has
	 verification problems, neon will call our verify function before
	 outright rejection of the connection.*/
      ne_ssl_set_verify(sess, server_ssl_callback, ras);
      ne_ssl_set_verify(sess2, server_ssl_callback, ras);
      /* For client connections, we register a callback for if the server
         wants to authenticate the client via client certificate. */
      ne_ssl_provide_ccert(sess, client_ssl_callback, ras);
      ne_ssl_provide_ccert(sess2, client_ssl_callback, ras);
      /* For the certificate certificate, register a callback in case
	 a password is needed for the key. */
      ne_ssl_keypw_prompt(sess, client_ssl_keypw_callback, ras);
      ne_ssl_keypw_prompt(sess2, client_ssl_keypw_callback, ras);
    }

#if 0
  /* Turn off persistent connections. */
  ne_set_persist(sess, 0);
  ne_set_persist(sess2, 0);
#endif

  /* make sure we will eventually destroy the session */
  apr_pool_cleanup_register(pool, sess, cleanup_session, apr_pool_cleanup_null);
  apr_pool_cleanup_register(pool, sess2, cleanup_session, apr_pool_cleanup_null);

  ne_set_useragent(sess, "SVN/" SVN_VERSION);
  ne_set_useragent(sess2, "SVN/" SVN_VERSION);

  /* clean up trailing slashes from the URL */
  len = strlen(uri.path);
  if (len > 1 && uri.path[len - 1] == '/')
    uri.path[len - 1] = '\0';

  /* Create and fill a session_baton. */
  ras = apr_pcalloc(pool, sizeof(*ras));
  ras->pool = pool;
  ras->url = apr_pstrdup (pool, repos_URL);
  ras->cfg = cfg;
  ras->server_group = server_group;
  ras->root = uri; /* copies uri pointer members, they get free'd in __close. */
  ras->sess = sess;
  ras->sess2 = sess2;  
  ras->callbacks = callbacks;
  ras->callback_baton = callback_baton;
  ras->compression = compression;

  /* note that ras->username and ras->password are still NULL at this
     point. */


  /* Register an authentication 'pull' callback with the neon sessions */
  ne_set_server_auth(sess, request_auth, ras);
  ne_set_server_auth(sess2, request_auth, ras);


  *session_baton = ras;

  return SVN_NO_ERROR;
}



static svn_error_t *svn_ra_dav__close (void *session_baton)
{
  svn_ra_session_t *ras = session_baton;

  (void) apr_pool_cleanup_run(ras->pool, ras->sess, cleanup_session);
  (void) apr_pool_cleanup_run(ras->pool, ras->sess2, cleanup_session);
  ne_uri_free(&ras->root);
  return NULL;
}


static svn_error_t *svn_ra_dav__do_get_uuid(void *session_baton,
                                            const char **uuid)
{
  svn_ra_session_t *ras = session_baton;

  if (! ras->uuid)
    {
      apr_hash_t *props;
      const svn_string_t *value;
      SVN_ERR(svn_ra_dav__get_dir(ras, "", 0, NULL, NULL, &props));
      value = apr_hash_get(props, SVN_PROP_ENTRY_UUID, APR_HASH_KEY_STRING);
      ras->uuid = value->data;
    }

  *uuid = ras->uuid;
  return SVN_NO_ERROR; 
}



static const svn_ra_plugin_t dav_plugin = {
  "ra_dav",
  "Module for accessing a repository via WebDAV (DeltaV) protocol.",
  svn_ra_dav__open,
  svn_ra_dav__close,
  svn_ra_dav__get_latest_revnum,
  svn_ra_dav__get_dated_revision,
  svn_ra_dav__change_rev_prop,
  svn_ra_dav__rev_proplist,
  svn_ra_dav__rev_prop,
  svn_ra_dav__get_commit_editor,
  svn_ra_dav__get_file,
  svn_ra_dav__get_dir,
  svn_ra_dav__do_checkout,
  svn_ra_dav__do_update,
  svn_ra_dav__do_switch,
  svn_ra_dav__do_status,
  svn_ra_dav__do_diff,
  svn_ra_dav__get_log,
  svn_ra_dav__do_check_path,
  svn_ra_dav__do_get_uuid
};


svn_error_t *svn_ra_dav_init(int abi_version,
                             apr_pool_t *pconf,
                             apr_hash_t *hash)
{
  /* ### need a version number to check here... */
  if (abi_version != 0)
    ;

  apr_hash_set (hash, "http", APR_HASH_KEY_STRING, &dav_plugin);

  if (ne_supports_ssl())
    {
      /* Only add this if neon is compiled with SSL support. */
      apr_hash_set (hash, "https", APR_HASH_KEY_STRING, &dav_plugin);
    }

  return NULL;
}
