/*
 * session.c :  routines for maintaining sessions state (to the DAV server)
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */



#include <apr_pools.h>

#include <strings.h>    /* for strcasecmp() (### should come from APR) */

#include <nsocket.h>
#include <http_request.h>
#include <uri.h>

#include "svn_error.h"
#include "svn_ra.h"

#include "ra_session.h"

/* ### need to pick this up from somewhere else... */
#define SVN_VERSION "0.1"


static apr_status_t cleanup_session(void *sess)
{
  http_session_destroy(sess);
  return APR_SUCCESS;
}

svn_error_t *
svn_ra_open (svn_ra_session_t **p_ras,
             const char *repository,
             apr_pool_t *pool)
{
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
  apr_register_cleanup(pool, sess, cleanup_session, apr_null_cleanup);

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

  ras = apr_pcalloc(pool, sizeof(*ras));
  ras->pool = pool;
  ras->root = uri;
  ras->sess = sess;

  *p_ras = ras;

  return NULL;
}

void
svn_ra_close (svn_ra_session_t *ras)
{
  apr_run_cleanup(ras->pool, ras->sess, cleanup_session);
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
