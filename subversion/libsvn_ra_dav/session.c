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
#include <apr_fnmatch.h>
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

  if (attempt > 1) 
    {
      /* Only use two retries. */
      return -1;
    }

  /* ### my only worry is that we're not catching any svn_errors from
     get_authenticator, get_username, get_password... */

  /* pull the username and password from the client */
  ras->callbacks->get_authenticator (&a, &auth_baton, 
                                     SVN_RA_AUTH_SIMPLE_PASSWORD, 
                                     ras->callback_baton, ras->pool);      
  authenticator = (svn_ra_simple_password_authenticator_t *) a;      
  authenticator->get_user_and_pass (&uname, &pword,
                                    auth_baton, 
                                    /* possibly force a user-prompt: */
                                    attempt ? TRUE : FALSE,
                                    ras->pool);

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


/* Baton for search_groups(). */
struct search_groups_baton
{
  const char *requested_host;  /* the host in the original uri */

  const char *proxy_group;     /* NULL unless/until we find a host
                                  match, in which case this is set to
                                  the name of the config file section
                                  where we can find proxy information
                                  for this host. */
  apr_pool_t *pool;
};


/* This is an `svn_config_enumerator_t' function, and BATON is a
 * `struct search_groups_baton *'.
 *
 * If an element of VALUE matches BATON->requested_host, then set
 * BATON->proxy_group to a copy of NAME allocated in BATON->pool, and
 * return false (to end the enumeration).  
 * 
 * VALUE is a comma-separated list of one or more expressions to match
 * a host, possibly using wildcards.  For example, these are all
 * valid VALUEs:
 *
 *   "svn.collab.net"
 *   "svn.collab.net, *.tigris.org"
 *   "*.collab.net, *.tigris.org, sp.red-bean.com"
 *
 * If no match, return true (to continue enumerating).
 */
static svn_boolean_t
search_groups (const char *name, const char *value, void *baton)
{
  struct search_groups_baton *b = baton;
  apr_array_header_t *subvals = svn_cstring_split (value, ',', TRUE, b->pool);
  int j;

  for (j = 0; j < subvals->nelts; j++)
    {
      const char *this_pattern = APR_ARRAY_IDX (subvals, j, char *);
      apr_status_t apr_err = apr_fnmatch (this_pattern, b->requested_host, 0);

      if (APR_STATUS_IS_SUCCESS (apr_err))
        {
          b->proxy_group = apr_pstrdup (b->pool, name);
          return FALSE;
        }
    }

  return TRUE;
}


/* Set *PROXY_HOST, *PROXY_PORT, *PROXY_USERNAME, and *PROXY_PASSWORD
 * the proxy information for REQUESTED_HOST, allocated in POOL, if
 * there is any applicable information.  Else set *PROXY_PORT to -1
 * and the rest to NULL.
 *
 * If return error, the effect on the return parameters is undefined.
 */
static svn_error_t *
get_proxy (const char **proxy_host,
           int *proxy_port,
           const char **proxy_username,
           const char **proxy_password,
           const char *requested_host,
           apr_pool_t *pool)
{
  struct search_groups_baton gb;
  svn_config_t *cfg;
  const char *port_str;

  SVN_ERR (svn_config_read_proxies (&cfg, pool));

  /* Start out with defaults. */
  svn_config_get (cfg, proxy_host, "default", "host", NULL);
  svn_config_get (cfg, &port_str, "default", "port", NULL);
  svn_config_get (cfg, proxy_username, "default", "username", NULL);
  svn_config_get (cfg, proxy_password, "default", "password", NULL);

  /* Search for a proxy pattern specific to this host. */
  gb.requested_host = requested_host;
  gb.proxy_group = NULL;
  gb.pool = pool;
  (void) svn_config_enumerate (cfg, "groups", search_groups, &gb);

  if (gb.proxy_group)
    {
      svn_config_get (cfg, proxy_host, gb.proxy_group, "host", *proxy_host);
      svn_config_get (cfg, &port_str, gb.proxy_group, "port", port_str);
      svn_config_get (cfg, proxy_username, gb.proxy_group, "username",
                      *proxy_username);
      svn_config_get (cfg, proxy_password, gb.proxy_group, "password",
                      *proxy_password);
    }

  /* Special case: convert the port value, if any. */
  if (port_str)
    *proxy_port = atoi (port_str);
  else
    *proxy_port = -1;

  return SVN_NO_ERROR;
}



/* ### need an ne_session_dup to avoid the second gethostbyname
 * call and make this halfway sane. */


static svn_error_t *
svn_ra_dav__open (void **session_baton,
                  svn_stringbuf_t *repos_URL,
                  const svn_ra_callbacks_t *callbacks,
                  void *callback_baton,
                  apr_pool_t *pool)
{
  const char *repository = repos_URL->data;
  apr_size_t len;
  ne_session *sess, *sess2;
  struct uri uri = { 0 };
  svn_ra_session_t *ras;
  int is_ssl_session;

  /* Sanity check the URI */
  if (uri_parse(repository, &uri, NULL) 
      || uri.host == NULL || uri.path == NULL)
    {
      uri_free(&uri);
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
                              "illegal URL for repository");
    }

  /* Can we initialize network? */
  if (sock_init() != 0) {
    uri_free(&uri);
    return svn_error_create(SVN_ERR_RA_SOCK_INIT, 0, NULL, pool,
                            "network socket initialization failed");
  }

#if 0
  /* #### enable this block for debugging output on stderr. */
  ne_debug_init(stderr, NE_DBG_HTTP|NE_DBG_HTTPBODY);
#endif

  /* Does the requested URL need to go through a proxy? */
  {
    const char *proxy_host;
    int proxy_port;
    const char *proxy_username;
    const char *proxy_password;
    
    SVN_ERR (get_proxy (&proxy_host,
                        &proxy_port,
                        &proxy_username,
                        &proxy_password,
                        uri.host,
                        pool));

#if 0  /* ### view proxy information for debugging */
    if (proxy_host)
      {
        printf ("%s ==>\n", uri.host);
        printf ("   %s:%d\n", proxy_host, proxy_port);
        printf ("   %s\n", proxy_username);
        printf ("   %s\n", proxy_password);
        printf ("\n");
      }
#endif /* 0 */
  }

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  is_ssl_session = (strcasecmp(uri.scheme, "https") == 0);
  if (is_ssl_session)
    {
      if (uri.port == -1)
        {
          uri.port = 443;
        }
      if (ne_supports_ssl() == 0)
        {
          uri_free(&uri);
          return svn_error_create(SVN_ERR_RA_SOCK_INIT, 0, NULL, pool,
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

  if (uri.port == -1)
    {
      uri.port = 80;
    }

  /* Create two neon session objects, and set their properties... */
  sess = ne_session_create(uri.scheme, uri.host, uri.port);
  sess2 = ne_session_create(uri.scheme, uri.host, uri.port);

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
  ras->url = apr_pstrdup (pool, repos_URL->data);
  ras->root = uri;
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
  return NULL;
}

static const svn_ra_plugin_t dav_plugin = {
  "ra_dav",
  "Module for accessing a repository via WebDAV (DeltaV) protocol.",
  svn_ra_dav__open,
  svn_ra_dav__close,
  svn_ra_dav__get_latest_revnum,
  svn_ra_dav__get_dated_revision,
  svn_ra_dav__get_commit_editor,
  svn_ra_dav__get_file,
  svn_ra_dav__do_checkout,
  svn_ra_dav__do_update,
  svn_ra_dav__do_switch,
  svn_ra_dav__do_status,
  NULL,
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


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
