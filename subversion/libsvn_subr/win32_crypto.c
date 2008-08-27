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

/* Dynamically load the address of function NAME in PDLL into
   PFN. Return TRUE if the function name was found, otherwise
   FALSE. Equivalent to dlsym(). */
static svn_boolean_t
get_crypto_function(const char *name, HINSTANCE *pdll, FARPROC *pfn)
{
  /* In case anyone wonders why we use LoadLibraryA here: This will
     always work on Win9x/Me, whilst LoadLibraryW may not. */
  HINSTANCE dll = LoadLibraryA("Crypt32.dll");
  if (dll)
    {
      FARPROC fn = GetProcAddress(dll, name);
      if (fn)
        {
          *pdll = dll;
          *pfn = fn;
          return TRUE;
        }
      FreeLibrary(dll);
    }
  return FALSE;
}

/* Implementation of svn_auth__password_set_t that encrypts
   the incoming password using the Windows CryptoAPI. */
static svn_boolean_t
windows_password_encrypter(apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *in,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  typedef BOOL (CALLBACK *encrypt_fn_t)
    (DATA_BLOB *,                /* pDataIn */
     LPCWSTR,                    /* szDataDescr */
     DATA_BLOB *,                /* pOptionalEntropy */
     PVOID,                      /* pvReserved */
     CRYPTPROTECT_PROMPTSTRUCT*, /* pPromptStruct */
     DWORD,                      /* dwFlags */
     DATA_BLOB*);                /* pDataOut */

  HINSTANCE dll;
  FARPROC fn;
  encrypt_fn_t encrypt;
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  svn_boolean_t crypted;

  if (!get_crypto_function("CryptProtectData", &dll, &fn))
    return FALSE;
  encrypt = (encrypt_fn_t) fn;

  blobin.cbData = strlen(in);
  blobin.pbData = (BYTE*) in;
  crypted = encrypt(&blobin, description, NULL, NULL, NULL,
                    CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (crypted)
    {
      char *coded = apr_palloc(pool, apr_base64_encode_len(blobout.cbData));
      apr_base64_encode(coded, blobout.pbData, blobout.cbData);
      crypted = simple_password_set(creds, realmstring, username, coded,
                                    non_interactive, pool);
      LocalFree(blobout.pbData);
    }

  FreeLibrary(dll);
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
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  typedef BOOL (CALLBACK * decrypt_fn_t)
    (DATA_BLOB *,                /* pDataIn */
     LPWSTR *,                   /* ppszDataDescr */
     DATA_BLOB *,                /* pOptionalEntropy */
     PVOID,                      /* pvReserved */
     CRYPTPROTECT_PROMPTSTRUCT*, /* pPromptStruct */
     DWORD,                      /* dwFlags */
     DATA_BLOB*);                /* pDataOut */

  HINSTANCE dll;
  FARPROC fn;
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  LPWSTR descr;
  decrypt_fn_t decrypt;
  svn_boolean_t decrypted;
  char *in;

  if (!simple_password_get(&in, creds, realmstring, username,
                           non_interactive, pool))
    return FALSE;

  if (!get_crypto_function("CryptUnprotectData", &dll, &fn))
    return FALSE;
  decrypt = (decrypt_fn_t) fn;

  blobin.cbData = strlen(in);
  blobin.pbData = apr_palloc(pool, apr_base64_decode_len(in));
  apr_base64_decode(blobin.pbData, in);
  decrypted = decrypt(&blobin, &descr, NULL, NULL, NULL,
                      CRYPTPROTECT_UI_FORBIDDEN, &blobout);
  if (decrypted)
    {
      if (0 == lstrcmpW(descr, description))
        *out = apr_pstrndup(pool, blobout.pbData, blobout.cbData);
      else
        decrypted = FALSE;
      LocalFree(blobout.pbData);
    }

  FreeLibrary(dll);
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

#endif /* WIN32 */
