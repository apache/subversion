/*
 * crypto.c :  cryptographic routines
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

#include "crypto.h"

#if APU_HAVE_CRYPTO
#include <apr_random.h>
#include <apr_crypto.h>

#include "svn_types.h"

#include "svn_private_config.h"
#include "private/svn_atomic.h"


struct svn_crypto__ctx_t {
  apr_crypto_t *crypto;
};


static volatile svn_atomic_t crypto_init_state = 0;

#define CRYPTO_INIT(scratch_pool) \
  SVN_ERR(svn_atomic__init_once(&crypto_init_state, \
                                crypto_init, NULL, (scratch_pool)))

/* Initialize the APR cryptography subsystem (if available), using
   ANY_POOL's ancestor root pool for the registration of cleanups,
   shutdowns, etc.   */
/* Don't call this function directly!  Use svn_atomic__init_once(). */
static svn_error_t *
crypto_init(void *baton, apr_pool_t *any_pool)
{
#if APU_HAVE_CRYPTO
  /* NOTE: this function will locate the topmost ancestor of ANY_POOL
     for its cleanup handlers. We don't have to worry about ANY_POOL
     being cleared.  */
  apr_status_t apr_err = apr_crypto_init(any_pool);

  if (apr_err)
    return svn_error_wrap_apr(
             apr_err,
             _("Failed to initialize cryptography subsystem"));
#endif /* APU_HAVE_CRYPTO  */

  return SVN_NO_ERROR;
}


/* If APU_ERR is non-NULL, create and return a Subversion error using
   APR_ERR and APU_ERR. */
static svn_error_t *
err_from_apu_err(apr_status_t apr_err,
                 const apu_err_t *apu_err)
{
  if (apu_err)
    return svn_error_createf(apr_err, NULL,
                             _("code (%d), reason (\"%s\"), msg (\"%s\")"),
                             apu_err->rc,
                             apu_err->reason ? apu_err->reason : "",
                             apu_err->msg ? apu_err->msg : "");
  return SVN_NO_ERROR;
}


static svn_error_t *
get_random_bytes(void **rand_bytes,
                 apr_size_t rand_len,
                 apr_pool_t *pool)
{
  apr_random_t *apr_rand;
  apr_status_t apr_err;

  *rand_bytes = apr_palloc(pool, rand_len);
  apr_rand = apr_random_standard_new(pool);
  apr_err = apr_random_secure_bytes(apr_rand, *rand_bytes, rand_len);
  if (apr_err != APR_SUCCESS)
    return svn_error_create(apr_err, NULL, _("Error obtaining random data"));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_crypto__context_create(svn_crypto__ctx_t **ctx,
                           apr_pool_t *result_pool)
{
  apr_status_t apr_err;
  const apu_err_t *apu_err = NULL;
  const apr_crypto_driver_t *driver;

  CRYPTO_INIT(result_pool);

  *ctx = apr_palloc(result_pool, sizeof(**ctx));

  /* ### TODO: So much for abstraction.  APR's wrappings around NSS
         and OpenSSL aren't quite as opaque as I'd hoped, requiring us
         to specify a driver type and then params to the driver.  We
         *could* use APU_CRYPTO_RECOMMENDED_DRIVER for the driver bit,
         but we'd still have to then dynamically ask APR which driver
         it used and then figure out the parameters to send to that
         driver at apr_crypto_make() time.  Or maybe I'm just
         overlooking something...   -- cmpilato  */

  apr_err = apr_crypto_get_driver(&driver, "openssl", NULL, &apu_err,
                                  result_pool);
  if (apr_err != APR_SUCCESS || driver == NULL)
    return svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                            _("OpenSSL crypto driver error"));

  apr_err = apr_crypto_make(&(*ctx)->crypto, driver, "engine=openssl",
                            result_pool);
  if (apr_err != APR_SUCCESS || (*ctx)->crypto == NULL)
    return svn_error_create(apr_err, NULL,
                            _("Error creating OpenSSL crypto context"));

  return SVN_NO_ERROR;
}


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
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_crypto_key_t *key = NULL;
  const apu_err_t *apu_err = NULL;
  apr_status_t apr_err;
  const unsigned char *prefix;
  apr_crypto_block_t *block_ctx = NULL;
  apr_size_t block_size = 0, encrypted_len = 0;
 
  SVN_ERR_ASSERT(ctx != NULL);

  /* Generate the salt. */
  *salt_len = 8;
  SVN_ERR(get_random_bytes((void **)salt, *salt_len, result_pool));

  /* Initialize the passphrase.  */
  apr_err = apr_crypto_passphrase(&key, NULL, secret, strlen(secret),
                                  *salt, 8 /* salt_len */,
                                  APR_KEY_AES_256, APR_MODE_CBC,
                                  1 /* doPad */, 4096, ctx->crypto,
                                  scratch_pool);
  if (apr_err != APR_SUCCESS)
    {
      apr_crypto_error(&apu_err, ctx->crypto);
      return svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                              _("Error creating derived key"));
    }

  /* Generate a 4-byte prefix. */
  SVN_ERR(get_random_bytes((void **)&prefix, 4, scratch_pool));

  /* Initialize block encryption. */
  apr_err = apr_crypto_block_encrypt_init(&block_ctx, iv, key, &block_size,
                                          result_pool);
  if ((apr_err != APR_SUCCESS) || (! block_ctx))
    {
      apr_crypto_error(&apu_err, ctx->crypto);
      return svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                              _("Error initializing block encryption"));
    }

  /* ### FIXME:  We need to actually use the prefix! */

  /* Encrypt the block. */
  apr_err = apr_crypto_block_encrypt(ciphertext, ciphertext_len,
                                     (unsigned char *)plaintext,
                                     strlen(plaintext) + 1, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      apr_crypto_error(&apu_err, ctx->crypto);
      err = svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                             _("Error encrypting block"));
      goto cleanup;
    }

  /* Finalize the block encryption. */
  apr_err = apr_crypto_block_encrypt_finish(*ciphertext + *ciphertext_len,
                                            &encrypted_len, block_ctx);
  if (apr_err != APR_SUCCESS)
    {
      apr_crypto_error(&apu_err, ctx->crypto);
      err = svn_error_create(apr_err, err_from_apu_err(apr_err, apu_err),
                             _("Error finalizing block encryption"));
      goto cleanup;
    }
  
 cleanup:
  apr_crypto_block_cleanup(block_ctx);
  return err;
}


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
                            apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

#endif  /* APU_HAVE_CRYPTO */
