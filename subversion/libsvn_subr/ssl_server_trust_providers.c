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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_pools.h>
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_config.h"


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


/*-----------------------------------------------------------------------*/
/* Windows SSL server trust provider, validates ssl certificate using    */
/* CryptoApi.                                                            */
/*-----------------------------------------------------------------------*/

#if defined(WIN32) && !defined(__MINGW32__)
#include <wincrypt.h>
#include <apr_base64.h>

typedef PCCERT_CONTEXT (WINAPI *createcertcontext_fn_t)(
    DWORD dwCertEncodingType,
    const BYTE *pbCertEncoded,
    DWORD cbCertEncoded);

typedef BOOL (WINAPI *getcertchain_fn_t)(
  HCERTCHAINENGINE hChainEngine,
  PCCERT_CONTEXT pCertContext,
  LPFILETIME pTime,
  HCERTSTORE hAdditionalStore,
  PCERT_CHAIN_PARA pChainPara,
  DWORD dwFlags,
  LPVOID pvReserved,
  PCCERT_CHAIN_CONTEXT* ppChainContext);

typedef VOID (WINAPI *freecertchain_fn_t)(
  PCCERT_CHAIN_CONTEXT pChainContext);

typedef BOOL (WINAPI *freecertcontext_fn_t)(
  PCCERT_CONTEXT pCertContext);

typedef struct {
  HINSTANCE cryptodll;
  createcertcontext_fn_t createcertcontext;
  getcertchain_fn_t getcertchain;
  freecertchain_fn_t freecertchain;
  freecertcontext_fn_t freecertcontext;
} windows_ssl_server_trust_provider_baton_t;

/* Retrieve ssl server CA failure overrides (if any) from CryptoApi. */
static svn_error_t *
windows_ssl_server_trust_first_credentials(void **credentials,
                                           void **iter_baton,
                                           void *provider_baton,
                                           apr_hash_t *parameters,
                                           const char *realmstring,
                                           apr_pool_t *pool)
{
  PCCERT_CONTEXT cert_context = NULL;
  CERT_CHAIN_PARA chain_para;
  PCCERT_CHAIN_CONTEXT chain_context = NULL;
  svn_boolean_t ok = TRUE;
  windows_ssl_server_trust_provider_baton_t *pb = provider_baton;

  apr_uint32_t *failures = apr_hash_get(parameters,
                                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                                        APR_HASH_KEY_STRING);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    apr_hash_get(parameters,
                 SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                 APR_HASH_KEY_STRING);

  if (*failures & ~SVN_AUTH_SSL_UNKNOWNCA)
    {
      /* give up, go on to next provider; the only thing we can accept
         is an unknown certificate authority. */

      *credentials = NULL;
      return SVN_NO_ERROR;
    }

  if (!pb->cryptodll)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }

  if (!pb->createcertcontext || !pb->getcertchain || !pb->freecertchain
      || !pb->freecertcontext)
    ok = FALSE;

  if (ok)
    {
      int cert_len;
      char *binary_cert;

      /* Use apr-util as CryptStringToBinaryA is available only on XP+. */
      binary_cert = apr_palloc(pool,
                               apr_base64_decode_len(cert_info->ascii_cert));
      cert_len = apr_base64_decode(binary_cert, cert_info->ascii_cert);

      /* Parse the certificate into a context. */
      cert_context = pb->createcertcontext
        (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, binary_cert, cert_len);

      if (!cert_context)
        ok = FALSE; /* Windows does not think the certificate is valid. */
    }

  if (ok)
    {
       /* Retrieve the certificate chain of the certificate
          (a certificate without a valid root does not have a chain). */
       memset(&chain_para, 0, sizeof(chain_para));
       chain_para.cbSize = sizeof(chain_para);

       if (pb->getcertchain(NULL, cert_context, NULL, NULL, &chain_para,
                        CERT_CHAIN_CACHE_END_CERT,
                        NULL, &chain_context))
         {
           if (chain_context->rgpChain[0]->TrustStatus.dwErrorStatus
               != CERT_TRUST_NO_ERROR)
            {
              /* The certificate is not 100% valid, just fall back to the
                 Subversion certificate handling. */

              ok = FALSE;
            }
         }
       else
         ok = FALSE;
    }

  if (chain_context)
    pb->freecertchain(chain_context);
  if (cert_context)
    pb->freecertcontext(cert_context);

  if (!ok)
    {
      /* go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  else
    {
      svn_auth_cred_ssl_server_trust_t *creds =
        apr_pcalloc(pool, sizeof(*creds));
      creds->may_save = FALSE; /* No need to save it. */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
}

static apr_status_t
windows_ssl_server_trust_cleanup(void *baton)
{
  windows_ssl_server_trust_provider_baton_t *pb = baton;
  if (pb->cryptodll)
    {
      FreeLibrary(pb->cryptodll);
      pb->cryptodll = NULL;
      pb->createcertcontext = NULL;
      pb->freecertchain = NULL;
      pb->freecertcontext = NULL;
      pb->getcertchain = NULL;
    }
  return APR_SUCCESS;
}

static const svn_auth_provider_t windows_server_trust_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  windows_ssl_server_trust_first_credentials,
  NULL,
  NULL,
};

/* Public API */
void
svn_auth_get_windows_ssl_server_trust_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  windows_ssl_server_trust_provider_baton_t *pb =
    apr_pcalloc(pool, sizeof(*pb));

  /* In case anyone wonders why we use LoadLibraryA here: This will
     always work on Win9x/Me, whilst LoadLibraryW may not. */
  pb->cryptodll = LoadLibraryA("Crypt32.dll");
  if (pb->cryptodll)
    {
      pb->createcertcontext =
        (createcertcontext_fn_t)GetProcAddress(pb->cryptodll,
                                               "CertCreateCertificateContext");
      pb->getcertchain =
        (getcertchain_fn_t)GetProcAddress(pb->cryptodll,
                                          "CertGetCertificateChain");
      pb->freecertchain =
        (freecertchain_fn_t)GetProcAddress(pb->cryptodll,
                                           "CertFreeCertificateChain");
      pb->freecertcontext =
        (freecertcontext_fn_t)GetProcAddress(pb->cryptodll,
                                             "CertFreeCertificateContext");
      apr_pool_cleanup_register(pool, pb, windows_ssl_server_trust_cleanup,
                                apr_pool_cleanup_null);
    }

  po->vtable = &windows_server_trust_provider;
  po->provider_baton = pb;
  *provider = po;
}


#endif /* WIN32 */
