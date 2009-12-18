/*
 * compat_providers.c: wrapper providers backwards compatibility
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_auth.h"
#include "svn_client.h"

void
svn_client_get_simple_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_simple_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_get_simple_prompt_provider(provider, prompt_func, prompt_baton,
                                      retry_limit, pool);
}

void
svn_client_get_username_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_username_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_get_username_prompt_provider(provider, prompt_func, prompt_baton,
                                        retry_limit, pool);
}



void svn_client_get_simple_provider(svn_auth_provider_object_t **provider,
                                    apr_pool_t *pool)
{
  svn_auth_get_simple_provider2(provider, NULL, NULL, pool);
}

#if defined(WIN32) && !defined(__MINGW32__)
void
svn_client_get_windows_simple_provider(svn_auth_provider_object_t **provider,
                                       apr_pool_t *pool)
{
  svn_auth_get_windows_simple_provider(provider, pool);
}
#endif /* WIN32 */

void svn_client_get_username_provider(svn_auth_provider_object_t **provider,
                                      apr_pool_t *pool)
{
  svn_auth_get_username_provider(provider, pool);
}

void
svn_client_get_ssl_server_trust_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_get_ssl_server_trust_file_provider(provider, pool);
}

void
svn_client_get_ssl_client_cert_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_get_ssl_client_cert_file_provider(provider, pool);
}

void
svn_client_get_ssl_client_cert_pw_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_get_ssl_client_cert_pw_file_provider2(provider, NULL, NULL, pool);
}

void
svn_client_get_ssl_server_trust_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_server_trust_prompt_func_t prompt_func,
   void *prompt_baton,
   apr_pool_t *pool)
{
  svn_auth_get_ssl_server_trust_prompt_provider(provider, prompt_func,
                                                prompt_baton, pool);
}

void
svn_client_get_ssl_client_cert_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_client_cert_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_get_ssl_client_cert_prompt_provider(provider, prompt_func,
                                               prompt_baton, retry_limit,
                                               pool);
}

void
svn_client_get_ssl_client_cert_pw_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_get_ssl_client_cert_pw_prompt_provider(provider, prompt_func,
                                                  prompt_baton, retry_limit,
                                                  pool);
}
