/*
 * auth_store.h:  Storage routines for authentication credentials
 *
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
 */

#ifndef SVN_LIBSVN_SUBR_AUTH_STORE_H
#define SVN_LIBSVN_SUBR_AUTH_STORE_H

#include "svn_types.h"
#include "svn_string.h"
#include "svn_auth.h"
#include "crypto.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Callback type used by an auth store's iterate_creds() function to
   iterate over stored credentials.  Implementations may return
   SVN_ERR_CEASE_INVOCATION to halt iteration of credentials without
   causing an error return from the iterate_creds() function.  */
typedef svn_error_t *(*svn_auth__store_iterate_creds_func_t)(
  void *iterate_creds_baton,
  const char *cred_kind,
  const char *realmstring,
  apr_hash_t *cred_hash,
  apr_pool_t *scratch_pool);



/*** Authentication credential store objects. ***/

/* Authentication credential store object. */
typedef struct svn_auth__store_t svn_auth__store_t;

/* Callback type: Open an authentication store. */
typedef svn_error_t *(*svn_auth__store_cb_open_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/* Callback type: Close an authentication store. */
typedef svn_error_t *(*svn_auth__store_cb_close_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/* Callback type: Delete an authentication store. */
typedef svn_error_t *(*svn_auth__store_cb_delete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/* Callback type: Set *CRED_HASH to a hash of authentication
   credential bits for the credentials of kind CRED_KIND and
   identified by REALMSTRING found in AUTH_STORE. */
typedef svn_error_t *(*svn_auth__store_cb_get_cred_hash_t)(
  apr_hash_t **cred_hash, 
  void *baton,
  const char *cred_kind,
  const char *realmstring,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/* Callback type: Store in AUTH_STORE a hash of authentication
   credential bits (CRED_HASH) for the credentials of kind CRED_KIND
   and identified by REALMSTRING, setting *STORED to TRUE iff the
   storage occurs successfully.  CRED_HASH may be NULL to indicate a
   desire to remove the relevant credentials from the store. */
typedef svn_error_t *(*svn_auth__store_cb_set_cred_hash_t)(
  svn_boolean_t *stored, 
  void *baton,
  const char *cred_kind,
  const char *realmstring,
  apr_hash_t *cred_hash,
  apr_pool_t *scratch_pool);

/* Callback type: Call ITERATE_CREDS_FUNC with ITERATE_CREDS_BATON for
   each set of credentials stored in the auth store. */
typedef svn_error_t *(*svn_auth__store_cb_iterate_creds_t)(
  void *baton,
  svn_auth__store_iterate_creds_func_t iterate_creds_func,
  void *iterate_creds_baton,
  apr_pool_t *scratch_pool);


/*** Auth Store Factory Stuff and Functionality. ***/

/* Create a generic authentication store object. */
svn_error_t *
svn_auth__store_create(svn_auth__store_t **auth_store,
                       apr_pool_t *result_pool);

/* Set the private context baton for AUTH_STORE to PRIV_BATON. */
svn_error_t *
svn_auth__store_set_baton(svn_auth__store_t *auth_store,
                          void *priv_baton);

/* Set the `open' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_open(svn_auth__store_t *auth_store,
                         svn_auth__store_cb_open_t func);

/* Set the `close' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_close(svn_auth__store_t *auth_store,
                          svn_auth__store_cb_close_t func);

/* Set the `delete' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_delete(svn_auth__store_t *auth_store,
                           svn_auth__store_cb_delete_t func);

/* Set the `get_cred_hash' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_get_cred_hash(svn_auth__store_t *auth_store,
                                  svn_auth__store_cb_get_cred_hash_t func);

/* Set the `set_cred_hash' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_set_cred_hash(svn_auth__store_t *auth_store,
                                  svn_auth__store_cb_set_cred_hash_t func);

/* Set the `iterate_creds' callback function for AUTH_STORE to FUNC. */
svn_error_t *
svn_auth__store_set_iterate_creds(svn_auth__store_t *auth_store,
                                  svn_auth__store_cb_iterate_creds_t func);


/* Open the authentication credential store identified by AUTH_STORE. */
svn_error_t *
svn_auth__store_open(svn_auth__store_t *auth_store,
                     apr_pool_t *scratch_pool);
                     
/* Close the auth store represented by AUTH_STORE. */
svn_error_t *
svn_auth__store_close(svn_auth__store_t *auth_store,
                      apr_pool_t *scratch_pool);


/* Delete the on-disk auth store represented by AUTH_STORE. */
svn_error_t *
svn_auth__store_delete(svn_auth__store_t *auth_store,
                       apr_pool_t *scratch_pool);

/* Set *CRED_HASH to a hash of authentication credential bits for the
   credentials of kind CRED_KIND and identified by REALMSTRING found
   in AUTH_STORE. */
svn_error_t *
svn_auth__store_get_cred_hash(apr_hash_t **cred_hash,
                              svn_auth__store_t *auth_store,
                              const char *cred_kind,
                              const char *realmstring,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Store in AUTH_STORE a hash of authentication credential bits
   (CRED_HASH) for the credentials of kind CRED_KIND and identified by
   REALMSTRING, setting *STORED to TRUE iff the storage occurs
   successfully.  CRED_HASH may be NULL to indicate a desire to remove
   the relevant credentials from the store.*/
svn_error_t *
svn_auth__store_set_cred_hash(svn_boolean_t *stored,
                              svn_auth__store_t *auth_store,
                              const char *cred_kind,
                              const char *realmstring,
                              apr_hash_t *cred_hash,
                              apr_pool_t *scratch_pool);

/* Iterate over the credentials stored in AUTH_STORE, calling
   ITERATE_CREDS_FUNC with ITERATE_CREDS_BATON for each set. */
svn_error_t *
svn_auth__store_iterate_creds(svn_auth__store_t *auth_store,
                              svn_auth__store_iterate_creds_func_t iterate_creds_func,
                              void *iterate_creds_baton,
                              apr_pool_t *scratch_pool);



/*** Pathetic Encrypted Authentication Store ***/

/* Set *AUTH_STORE_P to an object which describes the encrypted
   authentication credential store located at AUTH_STORE_PATH.

   CRYPTO_CTX is the cryptographic context which the store will use
   for related functionality.

   Use the providers registered with SECRET_AUTH_BATON to acquire
   (when needed) the master passphrase used to encrypt the sensitive
   contents of the store.  Any of the store-related functions may
   return SVN_ERR_AUTHN_FAILED if the secret provided by SECRET_FUNC
   does not validate against an existing store's checktext.

   ### TODO:  This is expected to be experimental code! ###
*/
svn_error_t *
svn_auth__pathetic_store_get(svn_auth__store_t **auth_store_p,
                             const char *auth_store_path,
                             svn_auth_baton_t *secret_auth_baton,
                             svn_crypto__ctx_t *crypto_ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Create an encrypted authentication store at AUTH_STORE_PATH, using
   CRYPTO_CTX and an initial master passphrase of SECRET.  */
svn_error_t *
svn_auth__pathetic_store_create(const char *auth_store_path,
                                svn_crypto__ctx_t *crypto_ctx,
                                const svn_string_t *secret,
                                apr_pool_t *scratch_pool);

/* Re-encrypt the contents of the authentication store located at
   AUTH_STORE_PATH using NEW_SECRET as the new master passphrase.
   OLD_SECRET is the current master passphrase.

   CRYPTO_CTX is the cryptographic context which the store will use
   for related functionality.

   Return SVN_ERR_AUTHN_FAILED if OLD_SECRET does not validate against
   an existing store's checktext.  */
svn_error_t *
svn_auth__pathetic_store_reencrypt(const char *auth_store_path,
                                   svn_crypto__ctx_t *crypto_ctx,
                                   const svn_string_t *old_secret,
                                   const svn_string_t *new_secret,
                                   apr_pool_t *scratch_pool);



/*** Runtime-config-based Authentication Store (aka, "the old way") ***/

/* Set *AUTH_STORE_P to an object which describes the
   runtime-config-based authentication credential store located at
   AUTH_STORE_PATH.  CFG is the configuration object with which the
   store is associated.

   NOTE: This auth-store will be automatically created if not already
   present on disk.
*/
svn_error_t *
svn_auth__config_store_get(svn_auth__store_t **auth_store_p,
                           const char *config_dir,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);




/*** Store Functionality ***/

/* Set *CREDS_P to the "username" credentials from AUTH_STORE which
   match REALMSTRING, if any.

   NOTE: Only the 'username' member of *CREDS_P will be populated.
*/
svn_error_t *
svn_auth__store_get_username_creds(svn_auth_cred_username_t **creds_p,
                                   svn_auth__store_t *auth_store,
                                   const char *realmstring,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);


/* Store CREDS as "username" credentials in AUTH_STORE, associated
   with REALMSTRING, setting *STORED iff the storage was successful.

   NOTE: Only the 'username' member of CREDS will be stored.
*/
svn_error_t *
svn_auth__store_set_username_creds(svn_boolean_t *stored,
                                   svn_auth__store_t *auth_store,
                                   const char *realmstring,
                                   svn_auth_cred_username_t *creds,
                                   apr_pool_t *scratch_pool);


/* Set *CREDS_P to the "simple" credentials from AUTH_STORE which
   match REALMSTRING, if any.

   NOTE: Only the 'username' and 'password' members of *CREDS_P will
   be populated.
*/
svn_error_t *
svn_auth__store_get_simple_creds(svn_auth_cred_simple_t **creds_p,
                                 svn_auth__store_t *auth_store,
                                 const char *realmstring,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


/* Store CREDS as "simple" credentials in AUTH_STORE, associated with
   REALMSTRING, setting *STORED iff the storage was successful.

   NOTE: Only the 'username' and 'password' members of CREDS will be
   stored.
*/
svn_error_t *
svn_auth__store_set_simple_creds(svn_boolean_t *stored,
                                 svn_auth__store_t *auth_store,
                                 const char *realmstring,
                                 svn_auth_cred_simple_t *creds,
                                 apr_pool_t *scratch_pool);



/*** Convenience/compatibility functions ***/

/* Set *AUTH_STORE to the authentication store object found in
   PARAMETERS, if any; otherwise, open a config-based store, cache it
   in PARAMETERS, and return it. */
svn_error_t *
svn_auth__get_store_from_parameters(svn_auth__store_t **auth_store,
                                    apr_hash_t *parameters,
                                    apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_SUBR_AUTH_STORE_H */
