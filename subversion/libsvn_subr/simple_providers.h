/*
 * simple_providers.h: providers for SVN_AUTH_CRED_SIMPLE
 *
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
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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

/* Common implementation for simple_first_creds and
   windows_simple_first_creds. Uses PARAMETERS, REALMSTRING and the
   simple auth provider's username and password cache to fill a set of
   CREDENTIALS. PASSWORD_GET is used to obtain the password value.
   PASSTYPE identifies the type of the cached password. CREDENTIALS are
   allocated from POOL. */
#ifdef __cplusplus
extern
#endif /* __cplusplus */
svn_error_t *
simple_first_creds_helper(void **credentials,
                          void **iter_baton,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          password_get_t password_get,
                          const char *passtype,
                          apr_pool_t *pool);

/* Common implementation for simple_save_creds and
   windows_simple_save_creds. Uses PARAMETERS and REALMSTRING to save
   a set of CREDENTIALS to the simple auth provider's username and
   password cache. PASSWORD_SET is used to store the password.
   PASSTYPE identifies the type of the cached password. Allocates from POOL. */
#ifdef __cplusplus
extern
#endif /* __cplusplus */
svn_error_t *
simple_save_creds_helper(svn_boolean_t *saved,
                         void *credentials,
                         void *provider_baton,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         password_set_t password_set,
                         const char *passtype,
                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */
