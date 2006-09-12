/*
 * simple_providers.c: providers for SVN_AUTH_CRED_SIMPLE
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
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_user.h"

#include "svn_private_config.h"

/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* The keys that will be stored on disk */
#define SVN_AUTH__AUTHFILE_USERNAME_KEY            "username"
#define SVN_AUTH__AUTHFILE_PASSWORD_KEY            "password"
#define SVN_AUTH__AUTHFILE_PASSTYPE_KEY            "passtype"

#define SVN_AUTH__SIMPLE_PASSWORD_TYPE             "simple"
#define SVN_AUTH__WINCRYPT_PASSWORD_TYPE           "wincrypt"
#define SVN_AUTH__KEYCHAIN_PASSWORD_TYPE           "keychain"


/* A function that stores PASSWORD (or some encrypted version thereof)
   either directly in CREDS, or externally using REALMSTRING and USERNAME
   as keys into the external store.  If NON_INTERACTIVE is set, the user
   must not be involved in the storage process.  POOL is used for any
   necessary allocation. */
typedef svn_boolean_t (*password_set_t)(apr_hash_t *creds,
                                        const char *realmstring,
                                        const char *username,
                                        const char *password,
                                        svn_boolean_t non_interactive,
                                        apr_pool_t *pool);

/* A function that stores in *PASSWORD (potentially after decrypting it)
   the user's password.  It might be obtained directly from CREDS, or
   from an external store, using REALMSTRING and USERNAME as keys.
   If NON_INTERACTIVE is set, the user must not be involved in the
   retrieval process.  POOL is used for any necessary allocation. */
typedef svn_boolean_t (*password_get_t)(const char **password,
                                        apr_hash_t *creds,
                                        const char *realmstring,
                                        const char *username,
                                        svn_boolean_t non_interactive,
                                        apr_pool_t *pool);



/* Implementation of password_get_t that retrieves the plaintext password
   from CREDS. */
static svn_boolean_t
simple_password_get(const char **password,
                    apr_hash_t *creds,
                    const char *realmstring,
                    const char *username,
                    svn_boolean_t non_interactive,
                    apr_pool_t *pool)
{
  svn_string_t *str;
  str = apr_hash_get(creds, SVN_AUTH__AUTHFILE_PASSWORD_KEY,
                     APR_HASH_KEY_STRING);
  if (str && str->data)
    {
      *password = str->data;
      return TRUE;
    }
  return FALSE;
}

/* Implementation of password_set_t that store the plaintext password
   in CREDS. */
static svn_boolean_t
simple_password_set(apr_hash_t *creds,
                    const char *realmstring,
                    const char *username,
                    const char *password,
                    svn_boolean_t non_interactive,
                    apr_pool_t *pool)
{
  apr_hash_set(creds, SVN_AUTH__AUTHFILE_PASSWORD_KEY, APR_HASH_KEY_STRING,
               svn_string_create(password, pool));
  return TRUE;
}

/* Common implementation for simple_first_creds and
   windows_simple_first_creds. Uses PARAMETERS, REALMSTRING and the
   simple auth provider's username and password cache to fill a set of
   CREDENTIALS. PASSWORD_GET is used to obtain the password value.
   PASSTYPE identifies the type of the cached password. CREDENTIALS are
   allocated from POOL. */
static svn_error_t *
simple_first_creds_helper(void **credentials,
                          void **iter_baton,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          password_get_t password_get,
                          const char *passtype,
                          apr_pool_t *pool)
{
  const char *config_dir = apr_hash_get(parameters,
                                        SVN_AUTH_PARAM_CONFIG_DIR,
                                        APR_HASH_KEY_STRING);
  const char *username = apr_hash_get(parameters,
                                      SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                      APR_HASH_KEY_STRING);
  const char *password = apr_hash_get(parameters,
                                      SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                      APR_HASH_KEY_STRING);
  svn_boolean_t non_interactive = apr_hash_get(parameters,
                                               SVN_AUTH_PARAM_NON_INTERACTIVE,
                                               APR_HASH_KEY_STRING) != NULL;

  svn_boolean_t may_save = username || password;
  svn_error_t *err;

  /* If we don't have a usename and a password yet, we try the auth cache */
  if (! (username && password))
    {
      apr_hash_t *creds_hash = NULL;

      /* Try to load credentials from a file on disk, based on the
         realmstring.  Don't throw an error, though: if something went
         wrong reading the file, no big deal.  What really matters is that
         we failed to get the creds, so allow the auth system to try the
         next provider. */
      err = svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                      realmstring, config_dir, pool);
      svn_error_clear(err);
      if (! err && creds_hash)
        {
          svn_string_t *str;
          if (! username)
            {
              str = apr_hash_get(creds_hash,
                                 SVN_AUTH__AUTHFILE_USERNAME_KEY,
                                 APR_HASH_KEY_STRING);
              if (str && str->data)
                username = str->data;
            }

          if (! password)
            {
              svn_boolean_t have_passtype;
              /* The password type in the auth data must match the
                 mangler's type, otherwise the password must be
                 interpreted by another provider. */
              str = apr_hash_get(creds_hash,
                                 SVN_AUTH__AUTHFILE_PASSTYPE_KEY,
                                 APR_HASH_KEY_STRING);
              have_passtype = (str && str->data);
              if (have_passtype && passtype
                  && 0 != strcmp(str->data, passtype))
                password = NULL;
              else
                {
                  if (!password_get(&password, creds_hash, realmstring,
                                    username, non_interactive, pool))
                    password = NULL;

                  /* If the auth data didn't contain a password type,
                     force a write to upgrade the format of the auth
                     data file. */
                  if (password && passtype && !have_passtype)
                    may_save = TRUE;
                }
            }
        }
    }

  /* Ask the OS for the username if we have a password but no
     username. */
  if (password && ! username)
    username = svn_user_get_name(pool);

  if (username && password)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc(pool, sizeof(*creds));
      creds->username = username;
      creds->password = password;
      creds->may_save = may_save;
      *credentials = creds;
    }
  else
    *credentials = NULL;

  *iter_baton = NULL;

  return SVN_NO_ERROR;
}


/* Common implementation for simple_save_creds and
   windows_simple_save_creds. Uses PARAMETERS and REALMSTRING to save
   a set of CREDENTIALS to the simple auth provider's username and
   password cache. PASSWORD_SET is used to store the password.
   PASSTYPE identifies the type of the cached password. Allocates from POOL. */
static svn_error_t *
simple_save_creds_helper(svn_boolean_t *saved,
                         void *credentials,
                         void *provider_baton,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         password_set_t password_set,
                         const char *passtype,
                         apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *err;
  const char *dont_store_passwords =
    apr_hash_get(parameters,
                 SVN_AUTH_PARAM_DONT_STORE_PASSWORDS,
                 APR_HASH_KEY_STRING);
  svn_boolean_t non_interactive = apr_hash_get(parameters,
                                               SVN_AUTH_PARAM_NON_INTERACTIVE,
                                               APR_HASH_KEY_STRING) != NULL;
  svn_boolean_t password_stored = TRUE;

  *saved = FALSE;

  if (! creds->may_save)
    return SVN_NO_ERROR;

  config_dir = apr_hash_get(parameters,
                            SVN_AUTH_PARAM_CONFIG_DIR,
                            APR_HASH_KEY_STRING);

  /* Put the credentials in a hash and save it to disk */
  creds_hash = apr_hash_make(pool);
  apr_hash_set(creds_hash, SVN_AUTH__AUTHFILE_USERNAME_KEY,
               APR_HASH_KEY_STRING,
               svn_string_create(creds->username, pool));
  if (! dont_store_passwords)
    {
      password_stored = password_set(creds_hash, realmstring, creds->username,
                                     creds->password, non_interactive, pool);
      if (password_stored)
        {
          /* Store the password type with the auth data, so that we
             know which provider owns the password. */
          if (passtype)
            {
              apr_hash_set(creds_hash, SVN_AUTH__AUTHFILE_PASSTYPE_KEY,
                           APR_HASH_KEY_STRING,
                           svn_string_create(passtype, pool));
            }
        }
      else
        *saved = FALSE;
    }

  if (password_stored)
    {
      err = svn_config_write_auth_data(creds_hash, SVN_AUTH_CRED_SIMPLE,
                                       realmstring, config_dir, pool);
      svn_error_clear(err);
      *saved = ! err;
    }

  return SVN_NO_ERROR;
}

/* Get cached (unencrypted) credentials from the simple provider's cache. */
static svn_error_t *
simple_first_creds(void **credentials,
                   void **iter_baton,
                   void *provider_baton,
                   apr_hash_t *parameters,
                   const char *realmstring,
                   apr_pool_t *pool)
{
  return simple_first_creds_helper(credentials,
                                   iter_baton, provider_baton,
                                   parameters, realmstring,
                                   simple_password_get,
                                   SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                   pool);
}

/* Save (unencrypted) credentials to the simple provider's cache. */
static svn_error_t *
simple_save_creds(svn_boolean_t *saved,
                  void *credentials,
                  void *provider_baton,
                  apr_hash_t *parameters,
                  const char *realmstring,
                  apr_pool_t *pool)
{
  return simple_save_creds_helper(saved, credentials, provider_baton,
                                  parameters, realmstring,
                                  simple_password_set,
                                  SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                  pool);
}

static const svn_auth_provider_t simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_first_creds,
  NULL,
  simple_save_creds
};


/* Public API */
void
svn_auth_get_simple_provider(svn_auth_provider_object_t **provider,
                             apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &simple_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for username/password prompting. */
typedef struct
{
  svn_auth_simple_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} simple_prompt_provider_baton_t;


/* Iteration baton type for username/password prompting. */
typedef struct
{
  /* how many times we've reprompted */
  int retries;
} simple_prompt_iter_baton_t;



/*** Helper Functions ***/
static svn_error_t *
prompt_for_simple_creds(svn_auth_cred_simple_t **cred_p,
                        simple_prompt_provider_baton_t *pb,
                        apr_hash_t *parameters,
                        const char *realmstring,
                        svn_boolean_t first_time,
                        svn_boolean_t may_save,
                        apr_pool_t *pool)
{
  const char *def_username = NULL, *def_password = NULL;

  *cred_p = NULL;

  /* If we're allowed to check for default usernames and passwords, do
     so. */
  if (first_time)
    {
      def_username = apr_hash_get(parameters,
                                  SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                  APR_HASH_KEY_STRING);

      /* No default username?  Try the auth cache. */
      if (! def_username)
        {
          const char *config_dir = apr_hash_get(parameters,
                                                SVN_AUTH_PARAM_CONFIG_DIR,
                                                APR_HASH_KEY_STRING);
          apr_hash_t *creds_hash = NULL;
          svn_string_t *str;
          svn_error_t *err;

          err = svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                          realmstring, config_dir, pool);
          svn_error_clear(err);
          if (! err && creds_hash)
            {
              str = apr_hash_get(creds_hash,
                                 SVN_AUTH__AUTHFILE_USERNAME_KEY,
                                 APR_HASH_KEY_STRING);
              if (str && str->data)
                def_username = str->data;
            }
        }

      /* Still no default username?  Try the UID. */
      if (! def_username)
        def_username = svn_user_get_name(pool);

      def_password = apr_hash_get(parameters,
                                  SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                  APR_HASH_KEY_STRING);
    }

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (def_username && def_password)
    {
      *cred_p = apr_palloc(pool, sizeof(**cred_p));
      (*cred_p)->username = apr_pstrdup(pool, def_username);
      (*cred_p)->password = apr_pstrdup(pool, def_password);
      (*cred_p)->may_save = TRUE;
    }
  else
    {
      SVN_ERR(pb->prompt_func(cred_p, pb->prompt_baton, realmstring,
                              def_username, may_save, pool));
    }

  return SVN_NO_ERROR;
}


/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds(void **credentials_p,
                          void **iter_baton,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = provider_baton;
  simple_prompt_iter_baton_t *ibaton = apr_pcalloc(pool, sizeof(*ibaton));
  const char *no_auth_cache = apr_hash_get(parameters,
                                           SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                           APR_HASH_KEY_STRING);

  SVN_ERR(prompt_for_simple_creds((svn_auth_cred_simple_t **) credentials_p,
                                  pb, parameters, realmstring, TRUE,
                                  ! no_auth_cache, pool));

  ibaton->retries = 0;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds(void **credentials_p,
                         void *iter_baton,
                         void *provider_baton,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;
  simple_prompt_provider_baton_t *pb = provider_baton;
  const char *no_auth_cache = apr_hash_get(parameters,
                                           SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                           APR_HASH_KEY_STRING);

  if (ib->retries >= pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR(prompt_for_simple_creds((svn_auth_cred_simple_t **) credentials_p,
                                  pb, parameters, realmstring, FALSE,
                                  ! no_auth_cache, pool));

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t simple_prompt_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_prompt_first_creds,
  simple_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_auth_get_simple_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_simple_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  simple_prompt_provider_baton_t *pb = apr_pcalloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &simple_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}



/*-----------------------------------------------------------------------*/
/* Windows simple provider, encrypts the password on Win2k and later.    */
/*-----------------------------------------------------------------------*/

#ifdef WIN32
#include <wincrypt.h>
#include <apr_base64.h>

/* The description string that's combined with unencrypted data by the
   Windows CryptoAPI. Used during decryption to verify that the
   encrypted data were valid. */
static const WCHAR description[] = L"auth_svn.simple.wincrypt";

/* The password type used by the windows simple auth provider, passed
   into simple_first_creds_helper and simple_save_creds_helper. */
static const char windows_crypto_type[] = "wincrypt";

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

/* Implementation of password_set_t that encrypts the incoming
   password using the Windows CryptoAPI. */
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

/* Implementation of password_get_t that decrypts the incoming
   password using the Windows CryptoAPI and verifies its validity. */
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
  return simple_first_creds_helper(credentials,
                                   iter_baton, provider_baton,
                                   parameters, realmstring,
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
  return simple_save_creds_helper(saved, credentials, provider_baton,
                                  parameters, realmstring,
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

/*-----------------------------------------------------------------------*/
/* keychain simple provider, puts passwords in the KeyChain              */
/*-----------------------------------------------------------------------*/

#ifdef SVN_HAVE_KEYCHAIN_SERVICES
#include <Security/Security.h>

/*
 * XXX (2005-12-07): If no GUI is available (e.g. over a SSH session),
 * you won't be prompted for credentials with which to unlock your
 * keychain.  Apple recognizes lack of TTY prompting as a known
 * problem.
 *
 *
 * XXX (2005-12-07): SecKeychainSetUserInteractionAllowed(FALSE) does
 * not appear to actually prevent all user interaction.  Specifically,
 * if the executable changes (for example, if it is rebuilt), the
 * system prompts the user to okay the use of the new executable.
 *
 * Worse than that, the interactivity setting is global per app (not
 * process/thread), meaning that there is a race condition in the
 * implementation below between calls to
 * SecKeychainSetUserInteractionAllowed() when multiple instances of
 * the same Subversion auth provider-based app run concurrently.
 */

/* Implementation of password_set_t that stores the password
   in the OS X KeyChain. */
static svn_boolean_t
keychain_password_set(apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      const char *password,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  OSStatus status;
  SecKeychainItemRef item;

  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(FALSE);

  status = SecKeychainFindGenericPassword(NULL, strlen(realmstring),
                                          realmstring, strlen(username),
                                          username, 0, NULL, &item);
  if (status)
    {
      if (status == errSecItemNotFound)
        status = SecKeychainAddGenericPassword(NULL, strlen(realmstring),
                                               realmstring, strlen(username),
                                               username, strlen(password),
                                               password, NULL);
    }
  else
    {
      status = SecKeychainItemModifyAttributesAndData(item, NULL,
                                                      strlen(password),
                                                      password);
      CFRelease(item);
    }

  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(TRUE);

  return status == 0;
}

/* Implementation of password_get_t that retrieves the password
   from the OS X KeyChain. */
static svn_boolean_t
keychain_password_get(const char **password,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  OSStatus status;
  UInt32 length;
  void *data;

  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(FALSE);

  status = SecKeychainFindGenericPassword(NULL, strlen(realmstring),
                                          realmstring, strlen(username),
                                          username, &length, &data, NULL);

  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(TRUE);

  if (status != 0)
    return FALSE;

  *password = apr_pstrmemdup(pool, data, length);
  SecKeychainItemFreeContent(NULL, data);
  return TRUE;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
keychain_simple_first_creds(void **credentials,
                            void **iter_baton,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  return simple_first_creds_helper(credentials,
                                   iter_baton, provider_baton,
                                   parameters, realmstring,
                                   keychain_password_get,
                                   SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                   pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
keychain_simple_save_creds(svn_boolean_t *saved,
                           void *credentials,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  return simple_save_creds_helper(saved, credentials, provider_baton,
                                  parameters, realmstring,
                                  keychain_password_set,
                                  SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                  pool);
}

static const svn_auth_provider_t keychain_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  keychain_simple_first_creds,
  NULL,
  keychain_simple_save_creds
};

/* Public API */
void
svn_auth_get_keychain_simple_provider(svn_auth_provider_object_t **provider,
                                      apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &keychain_simple_provider;
  *provider = po;
}

#endif /* SVN_HAVE_KEYCHAIN_SERVICES */
