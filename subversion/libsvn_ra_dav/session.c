/*
 * session.c :  routines for maintaining sessions state (to the DAV server)
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include "ra_dav.h"


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
  void *a, *auth_baton;
  char *uname, *pword;
  svn_ra_simple_password_authenticator_t *authenticator = NULL;
  svn_ra_session_t *ras = userdata;

  /* No authenticator callback?  Give up. */
  if (! ras->callbacks->get_authenticator)
    return -1;

  /* Only use two retries. */
  if (attempt > 1) 
    return -1;

  /* Get an authenticator object. */
  if (ras->callbacks->get_authenticator (&a, &auth_baton, 
                                         svn_ra_auth_simple_password, 
                                         ras->callback_baton,
                                         ras->pool))
    return -1;

  /* Verify that we have a query callback. */
  authenticator = (svn_ra_simple_password_authenticator_t *) a;      
  if (! authenticator->get_user_and_pass)
    return -1;

  /* Use the authenticator to query for a username and password. */
  if (authenticator->get_user_and_pass (&uname, &pword,
                                        auth_baton, 
                                        /* possibly force a user-prompt: */
                                        attempt ? TRUE : FALSE,
                                        ras->pool))
    return -1;

  /* ### silently truncates username/password to 256 chars. */
  apr_cpystrn(username, uname, NE_ABUFSIZ);
  apr_cpystrn(password, pword, NE_ABUFSIZ);

  return 0;
}


/* A neon-session callback to validate the SSL certificate when the CA
   is unknown or there are other SSL certificate problems. */
static int ssl_set_verify_callback(void *userdata, int failures,
                                   const ne_ssl_certificate *cert)
{
  /* XXX Right now this accepts any SSL server certificates.
     Subversion should perform checks of the SSL certificates and keep
     any information related to the certificates in $HOME/.subversion
     and not in the .svn directories so that the same information can
     be used for multiple working copies.

     Upon connecting to an SSL svn server, this is was subversion
     should do:

     1) Check if a copy of the SSL certificate exists for the given
     svn server hostname in $HOME/.subversion.  If it is there, then
     just continue processing the svn request.  Otherwise, print all
     the information about the svn server's SSL certificate and ask if
     the user wants to:
     a) Cancel the request.
     b) Continue this request but do the store the SSL certificate so
        that the next request will require the same revalidation.
     c) Accept the SSL certificate forever.  Store a copy of the
        certificate in $HOME/.subversion.

     Also, when checking the certificate, warn if the certificate is
     not properly signed by a CA.
   */
  return 0;
}


/* Set *PROXY_HOST, *PROXY_PORT, *PROXY_USERNAME, *PROXY_PASSWORD and
 * *TIMEOUT_SECONDS to the proxy information for REQUESTED_HOST, allocated
 * in POOL, if there is any applicable information.  If there is no
 * applicable information or if there is an error, then set *PROXY_PORT to
 * (unsigned int) -1 and the rest to NULL.  This function can return an
 * error, so before checking *PROXY_*, check for error return value.
 */
static svn_error_t *get_server_settings(const char **proxy_host,
                                        unsigned int *proxy_port,
                                        const char **proxy_username,
                                        const char **proxy_password,
                                        int *timeout_seconds,
                                        const char *requested_host,
                                        apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *exceptions;
  const char *port_str, *timeout_str, *server_group;

  /* If we find nothing, default to nulls. */
  *proxy_host     = NULL;
  *proxy_port     = (unsigned int) -1;
  *proxy_username = NULL;
  *proxy_password = NULL;
  port_str        = NULL;
  timeout_str     = NULL;

  SVN_ERR( svn_config_read_servers(&cfg, pool) );

  /* If there are defaults, use them, but only if the requested host
     is not one of the exceptions to the defaults. */
  svn_config_get(cfg, &exceptions, "default", "no-http-proxy", NULL);
  if ((! exceptions) || (! svn_cstring_match_glob_list(requested_host,
                                                       exceptions, pool)))
    {
      svn_config_get(cfg, proxy_host, "default", "http-proxy-host", NULL);
      svn_config_get(cfg, &port_str, "default", "http-proxy-port", NULL);
      svn_config_get(cfg, proxy_username, "default", "http-proxy-username",
                     NULL);
      svn_config_get(cfg, proxy_password, "default", "http-proxy-password",
                     NULL);
      svn_config_get(cfg, &timeout_str, "default", "http-timeout", NULL);
    }

  server_group = svn_config_find_group(cfg, requested_host, "groups", pool);
  if (server_group)
    {
      svn_config_get(cfg, proxy_host, server_group, "http-proxy-host",
                     *proxy_host);
      svn_config_get(cfg, &port_str, server_group, "http-proxy-port",
                     port_str);
      svn_config_get(cfg, proxy_username, server_group, "http-proxy-username",
                     *proxy_username);
      svn_config_get(cfg, proxy_password, server_group, "http-proxy-password",
                     *proxy_password);
      svn_config_get(cfg, &timeout_str, server_group, "http-timeout",
                     timeout_str);
    }

  /* Special case: convert the port value, if any. */
  if (port_str)
    {
      char *endstr;
      const long int port = strtol(port_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                                "illegal character in proxy port number");
      if (port < 0)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                                "negative proxy port number");
      if (port > 65535)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
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
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_TIMEOUT, 0, NULL,
                                "illegal character in timeout value");
      if (timeout < 0)
        return svn_error_create(SVN_ERR_RA_DAV_INVALID_TIMEOUT, 0, NULL,
                                "negative timeout value");
      *timeout_seconds = timeout;
    }
  else
    *timeout_seconds = 0;

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
                  apr_pool_t *pool)
{
  apr_size_t len;
  ne_session *sess, *sess2;
  ne_uri uri = { 0 };
  svn_ra_session_t *ras;
  int is_ssl_session;

  /* Sanity check the URI */
  if (ne_uri_parse(repos_URL, &uri) 
      || uri.host == NULL || uri.path == NULL)
    {
      ne_uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL,
                              "illegal URL for repository");
    }

  /* Can we initialize network? */
  if (ne_sock_init() != 0)
    {
      ne_uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, 0, NULL,
                              "network socket initialization failed");
    }

#if 0
  /* #### enable this block for debugging output on stderr. */
  ne_debug_init(stderr, NE_DBG_HTTP|NE_DBG_HTTPBODY);
#endif

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  is_ssl_session = (strcasecmp(uri.scheme, "https") == 0);
  if (is_ssl_session)
    {
      if (ne_supports_ssl() == 0)
        {
          ne_uri_free(&uri);
          return svn_error_create(SVN_ERR_RA_DAV_SOCK_INIT, 0, NULL,
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

  /* If there's a timeout or proxy for this URL, use it. */
  {
    const char *proxy_host;
    unsigned int proxy_port;
    const char *proxy_username;
    const char *proxy_password;
    int timeout;
    svn_error_t *err;
    
    err = get_server_settings(&proxy_host,
                              &proxy_port,
                              &proxy_username,
                              &proxy_password,
                              &timeout,
                              uri.host,
                              pool);
    if (err)
      {
        ne_uri_free(&uri);
        return err;
      }

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

  /* For SSL connections, when the CA certificate is not known for the
     server certificate or the server cert has other verification
     problems, neon will fail the connection unless we add a callback
     to tell it to ignore the problem.  */
  if (is_ssl_session)
    {
      ne_ssl_set_verify(sess, ssl_set_verify_callback, NULL);
      ne_ssl_set_verify(sess2, ssl_set_verify_callback, NULL);
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
  ras->root = uri; /* copies uri pointer members, they get free'd in __close. */
  ras->sess = sess;
  ras->sess2 = sess2;  
  ras->callbacks = callbacks;
  ras->callback_baton = callback_baton;

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
  svn_ra_dav__do_check_path
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
