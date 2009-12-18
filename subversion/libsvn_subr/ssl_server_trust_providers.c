/*
 * ssl_server_trust_providers.c: providers for
 * SVN_AUTH_CRED_SSL_SERVER_TRUST
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

#include <apr_pools.h>

#include "svn_auth.h"
#include "svn_error.h"
#include "svn_config.h"
#include "svn_string.h"


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* The keys that will be stored on disk.  These serve the same role as
   similar constants in other providers. */
#define AUTHN_ASCII_CERT_KEY            "ascii_cert"
#define AUTHN_FAILURES_KEY              "failures"


/* retrieve ssl server CA failure overrides (if any) from servers
   config */
static svn_error_t *
ssl_server_trust_file_first_credentials(void **credentials,
                                        void **iter_baton,
                                        void *provider_baton,
                                        apr_hash_t *parameters,
                                        const char *realmstring,
                                        apr_pool_t *pool)
{
  apr_uint32_t *failures = apr_hash_get(parameters,
                                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                                        APR_HASH_KEY_STRING);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    apr_hash_get(parameters,
                 SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                 APR_HASH_KEY_STRING);
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *error = SVN_NO_ERROR;

  *credentials = NULL;
  *iter_baton = NULL;

  /* Check if this is a permanently accepted certificate */
  config_dir = apr_hash_get(parameters,
                            SVN_AUTH_PARAM_CONFIG_DIR,
                            APR_HASH_KEY_STRING);
  error =
    svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SSL_SERVER_TRUST,
                              realmstring, config_dir, pool);
  svn_error_clear(error);
  if (! error && creds_hash)
    {
      svn_string_t *trusted_cert, *this_cert, *failstr;
      apr_uint32_t last_failures = 0;

      trusted_cert = apr_hash_get(creds_hash, AUTHN_ASCII_CERT_KEY,
                                  APR_HASH_KEY_STRING);
      this_cert = svn_string_create(cert_info->ascii_cert, pool);
      failstr = apr_hash_get(creds_hash, AUTHN_FAILURES_KEY,
                             APR_HASH_KEY_STRING);

      if (failstr)
        {
          char *endptr;
          unsigned long tmp_ulong = strtoul(failstr->data, &endptr, 10);

          if (*endptr == '\0')
            last_failures = (apr_uint32_t) tmp_ulong;
        }

      /* If the cert is trusted and there are no new failures, we
       * accept it by clearing all failures. */
      if (trusted_cert &&
          svn_string_compare(this_cert, trusted_cert) &&
          (*failures & ~last_failures) == 0)
        {
          *failures = 0;
        }
    }

  /* If all failures are cleared now, we return the creds */
  if (! *failures)
    {
      svn_auth_cred_ssl_server_trust_t *creds =
        apr_pcalloc(pool, sizeof(*creds));
      creds->may_save = FALSE; /* No need to save it again... */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
ssl_server_trust_file_save_credentials(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  svn_auth_cred_ssl_server_trust_t *creds = credentials;
  const svn_auth_ssl_server_cert_info_t *cert_info;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;

  if (! creds->may_save)
    return SVN_NO_ERROR;

  config_dir = apr_hash_get(parameters,
                            SVN_AUTH_PARAM_CONFIG_DIR,
                            APR_HASH_KEY_STRING);

  cert_info = apr_hash_get(parameters,
                           SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                           APR_HASH_KEY_STRING);

  creds_hash = apr_hash_make(pool);
  apr_hash_set(creds_hash, AUTHN_ASCII_CERT_KEY, APR_HASH_KEY_STRING,
               svn_string_create(cert_info->ascii_cert, pool));
  apr_hash_set(creds_hash, AUTHN_FAILURES_KEY, APR_HASH_KEY_STRING,
               svn_string_createf(pool, "%lu", (unsigned long)
                                  creds->accepted_failures));

  SVN_ERR(svn_config_write_auth_data(creds_hash,
                                     SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                     realmstring,
                                     config_dir,
                                     pool));
  *saved = TRUE;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_server_trust_file_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  &ssl_server_trust_file_first_credentials,
  NULL,
  &ssl_server_trust_file_save_credentials,
};


/*** Public API to SSL file providers. ***/
void
svn_auth_get_ssl_server_trust_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &ssl_server_trust_file_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for prompting to verify server ssl creds.
   There is no iteration baton type. */
typedef struct
{
  svn_auth_ssl_server_trust_prompt_func_t prompt_func;
  void *prompt_baton;
} ssl_server_trust_prompt_provider_baton_t;


static svn_error_t *
ssl_server_trust_prompt_first_cred(void **credentials_p,
                                   void **iter_baton,
                                   void *provider_baton,
                                   apr_hash_t *parameters,
                                   const char *realmstring,
                                   apr_pool_t *pool)
{
  ssl_server_trust_prompt_provider_baton_t *pb = provider_baton;
  apr_uint32_t *failures = apr_hash_get(parameters,
                                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                                        APR_HASH_KEY_STRING);
  const char *no_auth_cache = apr_hash_get(parameters,
                                           SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                           APR_HASH_KEY_STRING);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    apr_hash_get(parameters,
                 SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                 APR_HASH_KEY_STRING);

  SVN_ERR(pb->prompt_func((svn_auth_cred_ssl_server_trust_t **)
                          credentials_p, pb->prompt_baton, realmstring,
                          *failures, cert_info, ! no_auth_cache &&
                          ! (*failures & SVN_AUTH_SSL_OTHER), pool));

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_server_trust_prompt_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  ssl_server_trust_prompt_first_cred,
  NULL,
  NULL
};


/*** Public API to SSL prompting providers. ***/
void
svn_auth_get_ssl_server_trust_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_server_trust_prompt_func_t prompt_func,
   void *prompt_baton,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  ssl_server_trust_prompt_provider_baton_t *pb =
    apr_palloc(pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &ssl_server_trust_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
