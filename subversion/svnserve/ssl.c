/*
 * ssl.c :  SSL helper functions for svnserve
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include "svn_private_config.h"

#include <stdlib.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"

#include "server.h"

#ifdef SVN_HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>

/* List of ciphers that we allow for SSL connections. */
#define CIPHER_LIST "ALL:!LOW"

/* Helper method for more verbose SSL errors. */
static const char *ssl_last_error(apr_pool_t *pool)
{
  unsigned long ssl_error = ERR_get_error();
  char *buffer;
  
  if (ssl_error == 0)
    return strerror(errno);

  buffer = apr_pcalloc(pool, 256);  
  ERR_error_string_n(ssl_error, buffer, 256);
  return buffer;
}

/* Initializes the SSL context to be used by the server. */
svn_error_t *ssl_init(const char *cert, const char *key, void **baton,
                      apr_pool_t *pool)
{
  SSL_CTX *ctx;

  SSL_load_error_strings();
  SSL_library_init();
  
  /* TODO :  Seed the randum number generator (RNG)
   * for those operating systems that does not have /dev/urandom. */

  ctx = SSL_CTX_new(SSLv23_server_method());
  if (!ctx)
    return svn_error_create(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                            "Could not obtain an SSL context.");
  
  if (SSL_CTX_set_cipher_list(ctx, CIPHER_LIST) != 1)
    return svn_error_createf(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                             "Could not set SSL cipher list to '%s'.",
                             CIPHER_LIST);

  if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
    return svn_error_createf(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                             "Could not load SSL certificate from '%s': %s.",
                             cert, ssl_last_error(pool));

  if (SSL_CTX_use_RSAPrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1)
    return svn_error_createf(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                             "Could not load SSL key from '%s': %s.",
                             key, ssl_last_error(pool));

  if (!SSL_CTX_check_private_key(ctx))
    return svn_error_createf(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                             "Could not verify SSL key: %s.",
                             ssl_last_error(pool));

  *baton = ctx;
  return SVN_NO_ERROR;
}

#else

svn_error_t *ssl_init(const char *cert, const char *key, void **baton,
                      apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_SVN_SSL_INIT, NULL,
                          "This server was not built with SSL support.");
}

#endif
