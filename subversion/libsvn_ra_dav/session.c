/*
 * session.c :  routines for maintaining sessions state (to the DAV server)
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#include "svn_private_config.h"

#include "ra_dav.h"


static apr_status_t cleanup_session(void *sess)
{
  ne_session_destroy(sess);
  return APR_SUCCESS;
}

static svn_error_t * auth_set_username (const char *username, void *auth_baton)
{
  svn_ra_session_t *ras = auth_baton;

  ras->username = apr_pstrdup(ras->pool, username);

  return SVN_NO_ERROR;
}

static svn_error_t * auth_set_password (const char *password, void *auth_baton)
{
  svn_ra_session_t *ras = auth_baton;

  ras->password = apr_pstrdup(ras->pool, password);

  return SVN_NO_ERROR;
}

static int request_auth(void *userdata, const char *realm,
                        char **username, char **password)
{
  svn_ra_session_t *ras = userdata;
  apr_size_t l;

  l = strlen(ras->username) + 1;
  *username = malloc(l);
  memcpy(*username, ras->username, l);

  if (ras->password == NULL)
    {
      *password = malloc(1);
      **password = '\0';
    }
  else
    {
      l = strlen(ras->password) + 1;
      *password = malloc(l);
      memcpy(*password, ras->password, l);
    }

  return 0;
}

static svn_error_t * auth_authenticate (void **session_baton, void *auth_baton)
{
  svn_ra_session_t *ras = auth_baton;

  ne_set_server_auth(ras->sess, request_auth, ras);

  *session_baton = ras;

  return SVN_NO_ERROR;
}

static const svn_ra_username_authenticator_t username_authenticator =
{
  auth_set_username,
  auth_authenticate
};
static const svn_ra_simple_password_authenticator_t userpass_authenticator =
{
  auth_set_username,
  auth_set_password,
  auth_authenticate
};

static svn_error_t * svn_ra_get_authenticator (const void **authenticator,
                                               void **auth_baton,
                                               svn_stringbuf_t *repos_URL,
                                               apr_uint64_t method,
                                               apr_pool_t *pool)
{
  const char *repository = repos_URL->data;
  apr_size_t len;
  ne_session *sess;
  struct uri uri = { 0 };
  svn_ra_session_t *ras;

  if ((method & (SVN_RA_AUTH_USERNAME | SVN_RA_AUTH_SIMPLE_PASSWORD)) == 0)
    {
      return svn_error_create(SVN_ERR_RA_UNKNOWN_AUTH, 0, NULL, pool,
                              "this RA module does not know that "
                              "authentication/authorization mechanism");
    }

  if (uri_parse(repository, &uri, NULL) 
      || uri.host == NULL || uri.path == NULL)
    {
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, 0, NULL, pool,
                              "illegal URL for repository");
    }

  if (sock_init() != 0) {
    return svn_error_create(SVN_ERR_RA_SOCK_INIT, 0, NULL, pool,
                            "network socket initialization failed");
  }

  sess = ne_session_create();

  /* make sure we will eventually destroy the session */
  apr_pool_cleanup_register(pool, sess, cleanup_session, apr_pool_cleanup_null);

  ne_set_useragent(sess, "SVN/" SVN_VERSION);

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  if (strcasecmp(uri.scheme, "https") == 0)
    {
      if (uri.port == -1)
        {
          uri.port = 443;
        }
      if (ne_set_secure(sess, 1))
        {
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

  if (ne_session_server(sess, uri.host, uri.port))
    {
      return svn_error_createf(SVN_ERR_RA_HOSTNAME_LOOKUP, 0, NULL, pool,
                               "Hostname not found: %s", uri.host);
    }

  /* clean up trailing slashes from the URL */
  len = strlen(uri.path);
  if (len > 1 && uri.path[len - 1] == '/')
    uri.path[len - 1] = '\0';

  ras = apr_pcalloc(pool, sizeof(*ras));
  ras->pool = pool;
  ras->root = uri;
  ras->sess = sess;

  if (method == SVN_RA_AUTH_USERNAME)
    *authenticator = &username_authenticator;
  else if (method == SVN_RA_AUTH_SIMPLE_PASSWORD)
    *authenticator = &userpass_authenticator;

  *auth_baton = ras;

  return NULL;
}

static svn_error_t *svn_ra_close (void *session_baton)
{
  svn_ra_session_t *ras = session_baton;

  (void) apr_pool_cleanup_run(ras->pool, ras->sess, cleanup_session);
  return NULL;
}

static const svn_ra_plugin_t dav_plugin = {
  "ra_dav",
  "Module for accessing a repository via WebDAV (DeltaV) protocol.",
  (apr_uint64_t) (SVN_RA_AUTH_SIMPLE_PASSWORD),
  svn_ra_get_authenticator,
  svn_ra_close,
  svn_ra_dav__get_latest_revnum,
  svn_ra_dav__get_dated_revision,
  svn_ra_dav__get_commit_editor,
  svn_ra_dav__do_checkout,
  svn_ra_dav__do_update
};

svn_error_t *svn_ra_dav_init(int abi_version,
                             apr_pool_t *pconf,
                             apr_hash_t *hash)
{
  /* ### need a version number to check here... */
  if (abi_version != 0)
    ;

  apr_hash_set (hash, "http", APR_HASH_KEY_STRING, &dav_plugin);

  /* ### should ask neon whether it supports SSL or not, and skip this
     ### if it doesn't. */
  apr_hash_set (hash, "https", APR_HASH_KEY_STRING, &dav_plugin);

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
