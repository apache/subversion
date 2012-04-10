/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_auth_private.h
 * @brief Subversion's authentication system - Internal routines
 */

#ifndef SVN_AUTH_PRIVATE_H
#define SVN_AUTH_PRIVATE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* If you add a password type for a provider which stores
 * passwords on disk in encrypted form, remember to update
 * svn_auth__simple_save_creds_helper. Otherwise it will be
 * assumed that your provider stores passwords in plaintext. */
#define SVN_AUTH__SIMPLE_PASSWORD_TYPE             "simple"
#define SVN_AUTH__WINCRYPT_PASSWORD_TYPE           "wincrypt"
#define SVN_AUTH__KEYCHAIN_PASSWORD_TYPE           "keychain"
#define SVN_AUTH__KWALLET_PASSWORD_TYPE            "kwallet"
#define SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE      "gnome-keyring"
#define SVN_AUTH__GPG_AGENT_PASSWORD_TYPE          "gpg-agent"

/* A function that stores in *PASSWORD (potentially after decrypting it)
   the user's password.  It might be obtained directly from CREDS, or
   from an external store, using REALMSTRING and USERNAME as keys.
   (The behavior is undefined if REALMSTRING or USERNAME are NULL.)
   If NON_INTERACTIVE is set, the user must not be involved in the
   retrieval process.  Set *DONE to TRUE if a password was stored
   in *PASSWORD, to FALSE otherwise. POOL is used for any necessary
   allocation. */
typedef svn_error_t * (*svn_auth__password_get_t)
  (svn_boolean_t *done,
   const char **password,
   apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   apr_hash_t *parameters,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* A function that stores PASSWORD (or some encrypted version thereof)
   either directly in CREDS, or externally using REALMSTRING and USERNAME
   as keys into the external store.  If NON_INTERACTIVE is set, the user
   must not be involved in the storage process. Set *DONE to TRUE if the
   password was store, to FALSE otherwise. POOL is used for any necessary
   allocation. */
typedef svn_error_t * (*svn_auth__password_set_t)
  (svn_boolean_t *done,
   apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   const char *password,
   apr_hash_t *parameters,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to set *CREDENTIALS to a set of
   pre-cached authentication credentials pulled from the simple
   credential cache store identified by PASSTYPE.  PASSWORD_GET is
   used to obtain the password value.  Allocate *CREDENTIALS from
   POOL.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.), typically in
   their "first_creds" implementation.  */
svn_error_t *
svn_auth__simple_creds_cache_get(void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_get_t password_get,
                                 const char *passtype,
                                 apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to save CREDENTIALS in the simple
   credential cache store identified by PASSTYPE.  PASSWORD_SET is
   used to do the actual storage.  Use POOL for necessary allocations.
   Set *SAVED according to whether or not the credentials were
   successfully stored.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.) typically in
   their "save_creds" implementation.  */
svn_error_t *
svn_auth__simple_creds_cache_set(svn_boolean_t *saved,
                                 void *credentials,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_set_t password_set,
                                 const char *passtype,
                                 apr_pool_t *pool);

/* Implementation of svn_auth__password_get_t that retrieves
   the plaintext password from CREDS when USERNAME matches the stored
   credentials. */
svn_error_t *
svn_auth__simple_password_get(svn_boolean_t *done,
                              const char **password,
                              apr_hash_t *creds,
                              const char *realmstring,
                              const char *username,
                              apr_hash_t *parameters,
                              svn_boolean_t non_interactive,
                              apr_pool_t *pool);

/* Implementation of svn_auth__password_set_t that stores
   the plaintext password in CREDS. */
svn_error_t *
svn_auth__simple_password_set(svn_boolean_t *done,
                              apr_hash_t *creds,
                              const char *realmstring,
                              const char *username,
                              const char *password,
                              apr_hash_t *parameters,
                              svn_boolean_t non_interactive,
                              apr_pool_t *pool);


/* Use PARAMETERS and REALMSTRING to set *CREDENTIALS to a set of
   pre-cached authentication credentials pulled from the SSL client
   certificate passphrase credential cache store identified by
   PASSTYPE.  PASSPHRASE_GET is used to obtain the passphrase value.
   Allocate *CREDENTIALS from POOL.

   NOTE:  This function is a common implementation of code used by
   several of the ssl client passphrase credential providers (the
   default disk cache mechanism, Windows CryptoAPI, GNOME Keyring,
   etc.), typically in their "first_creds" implementation.  */
svn_error_t *
svn_auth__ssl_client_cert_pw_cache_get(void **credentials,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_get_t passphrase_get,
                                       const char *passtype,
                                       apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to save CREDENTIALS in the SSL
   client certificate passphrase credential cache store identified by
   PASSTYPE.  PASSPHRASE_SET is used to do the actual storage.  Use
   POOL for necessary allocations.  Set *SAVED according to whether or
   not the credentials were successfully stored.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.) typically in
   their "save_creds" implementation.  */
svn_error_t *
svn_auth__ssl_client_cert_pw_cache_set(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_set_t passphrase_set,
                                       const char *passtype,
                                       apr_pool_t *pool);

/* This implements the svn_auth__password_get_t interface.
   Set **PASSPHRASE to the plaintext passphrase retrieved from CREDS;
   ignore other parameters. */
svn_error_t *
svn_auth__ssl_client_cert_pw_get(svn_boolean_t *done,
                                 const char **passphrase,
                                 apr_hash_t *creds,
                                 const char *realmstring,
                                 const char *username,
                                 apr_hash_t *parameters,
                                 svn_boolean_t non_interactive,
                                 apr_pool_t *pool);

/* This implements the svn_auth__password_set_t interface.
   Store PASSPHRASE in CREDS; ignore other parameters. */
svn_error_t *
svn_auth__ssl_client_cert_pw_set(svn_boolean_t *done,
                                 apr_hash_t *creds,
                                 const char *realmstring,
                                 const char *username,
                                 const char *passphrase,
                                 apr_hash_t *parameters,
                                 svn_boolean_t non_interactive,
                                 apr_pool_t *pool);


/*** Master Passphrase ***/

/** The master passphrase "provider" vtable. */
typedef struct svn_auth__masterpass_provider_t
{
   /* Set *PASSPHRASE to the value of the Subversion master passphrase
      hash digest string.  If NON_INTERACTIVE is set, do not prompt
      the user.  Set *DONE to TRUE if the passphrase is successfully
      fetched; to FALSE otherwise. */
  svn_error_t *
  (*svn_auth__masterpass_fetch_t)(const char **passphrase,
                                  svn_boolean_t non_interactive,
                                  void *provider_baton,
                                  apr_pool_t *pool);

   /* Store PASSPHRASE as the value of the Subversion master
      passphrase hash digest string.  If NON_INTERACTIVE is set, do
      not prompt the user.  Set *DONE to TRUE if the passphrase is
      successfully stored; to FALSE otherwise. */
  svn_error_t *
  (*svn_auth__masterpass_store_t)(const char *passphrase,
                                  svn_boolean_t non_interactive,
                                  void *provider_baton,
                                  apr_pool_t *pool);

} svn_auth__masterpass_provider_t;

/** A master passphrase provider object and baton. */
typedef struct svn__auth_masterpass_provider_object_t
{
  const svn_auth__masterpass_provider_t *vtable;
  void *provider_baton;

} svn_auth__masterpass_provider_object_t;

/** The type of function returning a master passphrase provider. */
typedef void (*svn_auth__masterpass_provider_func_t)(
    svn_auth__masterpass_provider_object_t **provider,
    apr_pool_t *pool);

/* Set *PROVIDERS to an array of svn_auth_provider_object_t's
   appropriate for the client platform and which honor the allowed
   providers specified in CONFIG.  Allocate providers from POOL.  */
svn_error_t *
svn_auth__get_masterpass_providers(apr_array_header_t **providers,
                                   svn_config_t *config,
                                   apr_pool_t *pool);

#if !defined(WIN32)
/* Set *PROVIDER to a master passphrase provider which uses the GPG
   Agent for storage/retrieval.  */
void svn_auth__get_gpg_agent_masterpass_provider(
    svn_auth__masterpass_provider_object_t **provider,
    apr_pool_t *pool);
#endif /* !defined(WIN32) */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_PRIVATE_H */
