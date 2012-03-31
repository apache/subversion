/*
 * crypto.h :  cryptographic routines
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

#ifndef SVN_LIBSVN_SUBR_CRYPTO_H
#define SVN_LIBSVN_SUBR_CRYPTO_H

#include <apu.h>  /* for APU_HAVE_CRYPTO */

#if APU_HAVE_CRYPTO

#include "svn_types.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Opaque context for cryptographic operations.  */
typedef struct svn_crypto__ctx_t svn_crypto__ctx_t;


/* Set *CTX to new Subversion cryptographic context, based on an
   APR-managed OpenSSL cryptography context object allocated
   within RESULT_POOL.  */
/* ### TODO: Should this be something done once with the resulting
   ### svn_crypto__ctx_t object stored in svn_client_ctx_t?  */
svn_error_t *
svn_crypto__context_create(svn_crypto__ctx_t **ctx,
                           apr_pool_t *result_pool);


svn_error_t *
svn_crypto__encrypt_cstring(unsigned char **ciphertext,
                            apr_size_t *ciphertext_len,
                            const unsigned char **iv,
                            apr_size_t *iv_len,
                            const unsigned char **salt,
                            apr_size_t *salt_len,
                            svn_crypto__ctx_t *ctx,
                            const char *plaintext,
                            const char *secret,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


svn_error_t *
svn_crypto__decrypt_cstring(const svn_string_t **plaintext,
                            svn_crypto__ctx_t *ctx,
                            const unsigned char *ciphertext,
                            apr_size_t ciphertext_len,
                            const unsigned char *iv,
                            apr_size_t iv_len,
                            const unsigned char *salt,
                            apr_size_t salt_len,
                            const svn_string_t *secret,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* APU_HAVE_CRYPTO */

#endif  /* SVN_CRYPTO_H */
