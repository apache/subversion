/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_auth_private.h
 * @brief Subversion's authentication system - Internal routines
 */

#ifndef SVN_AUTH_PRIVATE_H
#define SVN_AUTH_PRIVATE_H

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

/* A function that stores in *PASSWORD (potentially after decrypting it)
   the user's password.  It might be obtained directly from CREDS, or
   from an external store, using REALMSTRING and USERNAME as keys.
   If NON_INTERACTIVE is set, the user must not be involved in the
   retrieval process.  POOL is used for any necessary allocation. */
typedef svn_boolean_t (*svn_auth__password_get_t)
  (const char **password,
   apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* A function that stores PASSWORD (or some encrypted version thereof)
   either directly in CREDS, or externally using REALMSTRING and USERNAME
   as keys into the external store.  If NON_INTERACTIVE is set, the user
   must not be involved in the storage process.  POOL is used for any
   necessary allocation. */
typedef svn_boolean_t (*svn_auth__password_set_t)
  (apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   const char *password,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* Common implementation for simple_first_creds and
   windows_simple_first_creds. Uses PARAMETERS, REALMSTRING and the
   simple auth provider's username and password cache to fill a set of
   CREDENTIALS. PASSWORD_GET is used to obtain the password value.
   PASSTYPE identifies the type of the cached password. CREDENTIALS are
   allocated from POOL. */
svn_error_t *
svn_auth__simple_first_creds_helper(void **credentials,
                                    void **iter_baton,
                                    void *provider_baton,
                                    apr_hash_t *parameters,
                                    const char *realmstring,
                                    svn_auth__password_get_t password_get,
                                    const char *passtype,
                                    apr_pool_t *pool);

/* Common implementation for simple_save_creds and
   windows_simple_save_creds. Uses PARAMETERS and REALMSTRING to save
   a set of CREDENTIALS to the simple auth provider's username and
   password cache. PASSWORD_SET is used to store the password.
   PASSTYPE identifies the type of the cached password. Allocates from POOL. */
svn_error_t *
svn_auth__simple_save_creds_helper(svn_boolean_t *saved,
                                   void *credentials,
                                   void *provider_baton,
                                   apr_hash_t *parameters,
                                   const char *realmstring,
                                   svn_auth__password_set_t password_set,
                                   const char *passtype,
                                   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_PRIVATE_H */
