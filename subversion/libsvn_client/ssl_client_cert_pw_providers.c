/*
 * ssl_client_cert_pw_providers.c: providers for
 * SVN_AUTH_CRED_SSL_CLIENT_CERT_PW
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_pools.h>
#include "svn_client.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_config.h"


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* retrieve and load a password for a client certificate from servers file */
static svn_error_t *
ssl_client_cert_pw_file_first_credentials (void **credentials_p,
                                           void **iter_baton,
                                           void *provider_baton,
                                           apr_hash_t *parameters,
                                           const char *realmstring,
                                           apr_pool_t *pool)
{
  svn_config_t *cfg = apr_hash_get (parameters,
                                    SVN_AUTH_PARAM_CONFIG,
                                    APR_HASH_KEY_STRING);
  const char *server_group = apr_hash_get (parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP,
                                           APR_HASH_KEY_STRING);

  const char *password =
    svn_config_get_server_setting (cfg, server_group,
                                   SVN_CONFIG_OPTION_SSL_CLIENT_CERT_PASSWORD,
                                   NULL);
  if (password)
    {
      svn_auth_cred_ssl_client_cert_pw_t *cred
        = apr_palloc (pool, sizeof (*cred));
      cred->password = password;
      /* does nothing so far */
      *credentials_p = cred;
    }
  else *credentials_p = NULL;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_client_cert_pw_file_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  ssl_client_cert_pw_file_first_credentials,
  NULL,
  NULL
};


/*** Public API to SSL file providers. ***/
void
svn_client_get_ssl_client_cert_pw_file_provider (
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  po->vtable = &ssl_client_cert_pw_file_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for client passphrase prompting.
   There is no iteration baton type. */
typedef struct
{
  svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func;
  void *prompt_baton;
} ssl_client_cert_pw_prompt_provider_baton_t;


static svn_error_t *
ssl_client_cert_pw_prompt_first_cred (void **credentials_p,
                                      void **iter_baton,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool)
{
  ssl_client_cert_pw_prompt_provider_baton_t *pb = provider_baton;

  SVN_ERR (pb->prompt_func ((svn_auth_cred_ssl_client_cert_pw_t **)
                            credentials_p,
                            pb->prompt_baton, pool));

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t client_cert_pw_prompt_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  ssl_client_cert_pw_prompt_first_cred,
  NULL,
  NULL
};


void svn_client_get_ssl_client_cert_pw_prompt_provider (
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
  void *prompt_baton,
  apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  ssl_client_cert_pw_prompt_provider_baton_t *pb =
    apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &client_cert_pw_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
