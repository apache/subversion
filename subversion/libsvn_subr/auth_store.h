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


/* Opaque encrypted authentication credential store object. */
typedef struct svn_auth__store_t svn_auth__store_t;


/* Open (creating if necessary and if CREATE is set) an encrypted
   authentication credential store at AUTH_STORE_PATH, and set
   *AUTH_STORE_P to the object which describes it.

   CRYPTO_CTX is the cryptographic context which the store will use
   for related functionality.

   SECRET is the master passphrase used to encrypt the sensitive
   contents of the store.  When creating the store it is registered
   with the store as-is, but when opening a previously existing store,
   it is validated against the passphrase self-checking information in
   the store itself.  SVN_ERR_AUTHN_FAILED will be returned if SECRET
   does not validate against an existing store's checktext.
*/
svn_error_t *
svn_auth__store_open(svn_auth__store_t **auth_store_p,
                     const char *auth_store_path,
                     svn_crypto__ctx_t *crypto_ctx,
                     const svn_string_t *secret,
                     svn_boolean_t create,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);


/* Close the auth store represented by AUTH_STORE. */
svn_error_t *
svn_auth__store_close(svn_auth__store_t *auth_store,
                      apr_pool_t *scratch_pool);


/* Close the on-disk auth store represented by AUTH_STORE. */
svn_error_t *
svn_auth__store_delete(const char *auth_store_path,
                       apr_pool_t *scratch_pool);


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
   with REALMSTRING.

   NOTE: Only the 'username' member of CREDS will be stored.
*/
svn_error_t *
svn_auth__store_set_username_creds(svn_auth__store_t *auth_store,
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
   REALMSTRING.

   NOTE: Only the 'username' and 'password' members of CREDS will be
   stored.
*/
svn_error_t *
svn_auth__store_set_simple_creds(svn_auth__store_t *auth_store,
                                 const char *realmstring,
                                 svn_auth_cred_simple_t *creds,
                                 apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_SUBR_AUTH_STORE_H */
