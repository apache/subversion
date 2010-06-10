/*
 * win32_crypto.c: win32 providers for SVN_AUTH_*
 *
 * ====================================================================
 * Copyright (c) 2003-2006, 2008 CollabNet.  All rights reserved.
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

#if defined(WIN32) && !defined(__MINGW32__)

/*** Includes. ***/

#include <apr_pools.h>
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_user.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#include <wincrypt.h>
#include <apr_base64.h>

/*-----------------------------------------------------------------------*/
/* Windows simple provider, encrypts the password on Win2k and later.    */
/*-----------------------------------------------------------------------*/

/* The description string that's combined with unencrypted data by the
   Windows CryptoAPI. Used during decryption to verify that the
   encrypted data were valid. */
static const WCHAR description[] = L"auth_svn.simple.wincrypt";

/* Implementation of svn_auth__password_set_t that encrypts
   the incoming password using the Windows CryptoAPI. */
static svn_boolean_t
windows_password_encrypter(apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *in,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  svn_boolean_t crypted;

  blobin.cbData = strlen(in);
  blobin.pbData = (BYTE*) in;
  crypted = CryptProtectData(&blobin, description, NULL, NULL, NULL,
                             CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (crypted)
    {
      char *coded = apr_palloc(pool, apr_base64_encode_len(blobout.cbData));
      apr_base64_encode(coded, blobout.pbData, blobout.cbData);
      crypted = svn_auth__simple_password_set(creds, realmstring, username,
                                              coded, parameters,
                                              non_interactive, pool);
      LocalFree(blobout.pbData);
    }

  return crypted;
}

/* Implementation of svn_auth__password_get_t that decrypts
   the incoming password using the Windows CryptoAPI and verifies its
   validity. */
static svn_boolean_t
windows_password_decrypter(const char **out,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  LPWSTR descr;
  svn_boolean_t decrypted;
  char *in;

  if (!svn_auth__simple_password_get(&in, creds, realmstring, username,
                                     parameters, non_interactive, pool))
    return FALSE;

  blobin.cbData = strlen(in);
  blobin.pbData = apr_palloc(pool, apr_base64_decode_len(in));
  apr_base64_decode(blobin.pbData, in);
  decrypted = CryptUnprotectData(&blobin, &descr, NULL, NULL, NULL,
                                 CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (decrypted)
    {
      if (0 == lstrcmpW(descr, description))
        *out = apr_pstrndup(pool, blobout.pbData, blobout.cbData);
      else
        decrypted = FALSE;
      LocalFree(blobout.pbData);
      LocalFree(descr);
    }

  return decrypted;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
windows_simple_first_creds(void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  return svn_auth__simple_first_creds_helper(credentials,
                                             iter_baton,
                                             provider_baton,
                                             parameters,
                                             realmstring,
                                             windows_password_decrypter,
                                             SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
                                             pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
windows_simple_save_creds(svn_boolean_t *saved,
                          void *credentials,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  return svn_auth__simple_save_creds_helper(saved, credentials,
                                            provider_baton,
                                            parameters,
                                            realmstring,
                                            windows_password_encrypter,
                                            SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
                                            pool);
}

static const svn_auth_provider_t windows_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  windows_simple_first_creds,
  NULL,
  windows_simple_save_creds
};


/* Public API */
void
svn_auth_get_windows_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &windows_simple_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Windows SSL server trust provider, validates ssl certificate using    */
/* CryptoApi.                                                            */
/*-----------------------------------------------------------------------*/

/* Implementation of svn_auth__password_set_t that encrypts
   the incoming password using the Windows CryptoAPI. */
static svn_boolean_t
windows_ssl_client_cert_pw_encrypter(apr_hash_t *creds,
                                     const char *realmstring,
                                     const char *username,
                                     const char *in,
                                     apr_hash_t *parameters,
                                     svn_boolean_t non_interactive,
                                     apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  svn_boolean_t crypted;

  blobin.cbData = strlen(in);
  blobin.pbData = (BYTE*) in;
  crypted = CryptProtectData(&blobin, description, NULL, NULL, NULL,
                             CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (crypted)
    {
      char *coded = apr_palloc(pool, apr_base64_encode_len(blobout.cbData));
      apr_base64_encode(coded, blobout.pbData, blobout.cbData);
      crypted = svn_auth__ssl_client_cert_pw_set(creds, realmstring, username,
                                                 coded, parameters,
                                                 non_interactive, pool);
      LocalFree(blobout.pbData);
    }

  return crypted;
}

/* Implementation of svn_auth__password_get_t that decrypts
   the incoming password using the Windows CryptoAPI and verifies its
   validity. */
static svn_boolean_t
windows_ssl_client_cert_pw_decrypter(const char **out,
                                     apr_hash_t *creds,
                                     const char *realmstring,
                                     const char *username,
                                     apr_hash_t *parameters,
                                     svn_boolean_t non_interactive,
                                     apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  LPWSTR descr;
  svn_boolean_t decrypted;
  char *in;

  if (!svn_auth__ssl_client_cert_pw_get(&in, creds, realmstring, username,
                                        parameters, non_interactive, pool))
    return FALSE;

  blobin.cbData = strlen(in);
  blobin.pbData = apr_palloc(pool, apr_base64_decode_len(in));
  apr_base64_decode(blobin.pbData, in);
  decrypted = CryptUnprotectData(&blobin, &descr, NULL, NULL, NULL,
                                 CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (decrypted)
    {
      if (0 == lstrcmpW(descr, description))
        *out = apr_pstrndup(pool, blobout.pbData, blobout.cbData);
      else
        decrypted = FALSE;
      LocalFree(blobout.pbData);
      LocalFree(descr);
    }

  return decrypted;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
windows_ssl_client_cert_pw_first_creds(void **credentials,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
    return svn_auth__ssl_client_cert_pw_file_first_creds_helper
              (credentials,
               iter_baton,
               provider_baton,
               parameters,
               realmstring,
               windows_ssl_client_cert_pw_decrypter,
               SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
               pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
windows_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
                                      void *credentials,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool)
{
    return svn_auth__ssl_client_cert_pw_file_save_creds_helper
              (saved,
               credentials,
               provider_baton,
               parameters,
               realmstring,
               windows_ssl_client_cert_pw_encrypter,
               SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
               pool);
}

static const svn_auth_provider_t windows_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  windows_ssl_client_cert_pw_first_creds,
  NULL,
  windows_ssl_client_cert_pw_save_creds
};


/* Public API */
void
svn_auth_get_windows_ssl_client_cert_pw_provider
   (svn_auth_provider_object_t **provider,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &windows_ssl_client_cert_pw_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Windows SSL server trust provider, validates ssl certificate using    */
/* CryptoApi.                                                            */
/*-----------------------------------------------------------------------*/

/* Helper for windows_ssl_server_trust_first_credentials for validating
 * certificate using CryptoApi. Sets *OK_P to TRUE if base64 encoded ASCII_CERT
 * certificate considered as valid.
 */
static svn_error_t *
windows_validate_certificate(svn_boolean_t *ok_p,
                             const char *ascii_cert,
                             apr_pool_t *pool)
{
  PCCERT_CONTEXT cert_context = NULL;
  CERT_CHAIN_PARA chain_para;
  PCCERT_CHAIN_CONTEXT chain_context = NULL;
  int cert_len;
  char *binary_cert;

  *ok_p = FALSE;

  /* Use apr-util as CryptStringToBinaryA is available only on XP+. */
  binary_cert = apr_palloc(pool,
                           apr_base64_decode_len(ascii_cert));
  cert_len = apr_base64_decode(binary_cert, ascii_cert);

  /* Parse the certificate into a context. */
  cert_context = CertCreateCertificateContext
    (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, binary_cert, cert_len);

  if (cert_context)
    {
      /* Retrieve the certificate chain of the certificate
         (a certificate without a valid root does not have a chain). */
      memset(&chain_para, 0, sizeof(chain_para));
      chain_para.cbSize = sizeof(chain_para);

      if (CertGetCertificateChain(NULL, cert_context, NULL, NULL, &chain_para,
                                  CERT_CHAIN_CACHE_END_CERT |
                                  CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT,
                                  NULL, &chain_context))
        {
          CERT_CHAIN_POLICY_PARA policy_para;
          CERT_CHAIN_POLICY_STATUS policy_status;

          policy_para.cbSize = sizeof(policy_para);
          policy_para.dwFlags = 0;
          policy_para.pvExtraPolicyPara = NULL;

          policy_status.cbSize = sizeof(policy_status);

          if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                               chain_context, &policy_para,
                                               &policy_status))
            {
              if (policy_status.dwError == S_OK)
                {
                  /* Windows thinks the certificate is valid. */
                  *ok_p = TRUE;
                }
            }

          CertFreeCertificateChain(chain_context);
        }
      CertFreeCertificateContext(cert_context);
    }

  return SVN_NO_ERROR;
}

/* Retrieve ssl server CA failure overrides (if any) from CryptoApi. */
static svn_error_t *
windows_ssl_server_trust_first_credentials(void **credentials,
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

  *credentials = NULL;
  *iter_baton = NULL;

  /* We can accept only unknown certificate authority. */
  if (*failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      svn_boolean_t ok;

      SVN_ERR(windows_validate_certificate(&ok, cert_info->ascii_cert, pool));

      /* Windows thinks that certificate is ok. */
      if (ok)
        {
          /* Clear failure flag. */
          *failures &= ~SVN_AUTH_SSL_UNKNOWNCA;
        }
    }

  /* If all failures are cleared now, we return the creds */
  if (! *failures)
    {
      svn_auth_cred_ssl_server_trust_t *creds =
        apr_pcalloc(pool, sizeof(*creds));
      creds->may_save = FALSE; /* No need to save it. */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
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

  po->vtable = &windows_server_trust_provider;
  *provider = po;
}

#endif /* WIN32 */
