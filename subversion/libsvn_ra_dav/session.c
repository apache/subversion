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
 * ====================================================================
 */



#include <apr_pools.h>

#include <strings.h>    /* for strcasecmp() (### should come from APR) */

#include <nsocket.h>
#include <http_request.h>
#include <uri.h>

#include "svn_error.h"
#include "svn_ra.h"
#include "svn_private_config.h"

#include "ra_dav.h"


static apr_status_t cleanup_session(void *sess)
{
  http_session_destroy(sess);
  return APR_SUCCESS;
}

static svn_error_t * svn_ra_open (void **session_baton,
                                  svn_stringbuf_t *repository_name,
                                  apr_pool_t *pool)
{
  const char *repository = repository_name->data;
  apr_size_t len;

  http_session *sess;
  struct uri uri = { 0 };
  svn_ra_session_t *ras;

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

  sess = http_session_create();

  /* make sure we will eventually destroy the session */
  apr_pool_cleanup_register(pool, sess, cleanup_session, apr_pool_cleanup_null);

  http_set_useragent(sess, "SVN/" SVN_VERSION);

  /* we want to know if the repository is actually somewhere else */
  /* ### not yet: http_redirect_register(sess, ... ); */

  if (strcasecmp(uri.scheme, "https") == 0)
    {
      if (uri.port == -1)
        {
          uri.port = 443;
        }
      if (http_set_secure(sess, 1) || 
          http_set_accept_secure_upgrade(sess, 1))
        {
          return svn_error_create(SVN_ERR_RA_SOCK_INIT, 0, NULL, pool,
                                  "SSL is not supported");
        }
    }
  if (uri.port == -1)
    {
      uri.port = 80;
    }

  if (http_session_server(sess, uri.host, uri.port))
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

  *session_baton = ras;

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
  svn_ra_open,
  svn_ra_close,
  svn_ra_dav__get_latest_revnum,
  svn_ra_dav__get_commit_editor,
  svn_ra_dav__do_checkout,
  svn_ra_dav__do_update
};

svn_error_t *svn_ra_dav_init(int abi_version,
                             apr_pool_t *pconf,
                             const char **url_type,
                             const svn_ra_plugin_t **plugin)
{
  /* ### need a version number to check here... */
  if (abi_version != 0)
    ;

  *url_type = "http";
  *plugin = &dav_plugin;

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
