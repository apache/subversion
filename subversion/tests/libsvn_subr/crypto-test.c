/*
 * crypto-test.c -- test cryptographic routines
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

#include <stdio.h>
#include <string.h>

#include "../svn_test.h"
#include "../../libsvn_subr/crypto.h"

#ifdef APU_HAVE_CRYPTO

static svn_error_t *
test_encrypt_decrypt_password(apr_pool_t *pool)
{
  const svn_string_t *ciphertext, *iv, *salt, *master;
  const char *password, *password_again;
  svn_crypto__ctx_t *ctx;

  master = svn_string_create("Pastor Massword", pool);
  password = "3ncryptm3!";

  SVN_ERR(svn_crypto__context_create(&ctx, pool));
  SVN_ERR(svn_crypto__encrypt_password(&ciphertext, &iv, &salt, ctx,
                                       password, master, pool, pool));
  if (! ciphertext)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Encryption failed to return ciphertext");
  if (! salt)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Encryption failed to return salt");
  if (! iv)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Encryption failed to return initialization "
                            "vector");

  SVN_ERR(svn_crypto__decrypt_password(&password_again, ctx, ciphertext, iv,
                                       salt, master, pool, pool));

  if (! password_again)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Decryption failed to generate results");

  if (strcmp(password, password_again) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Encrypt/decrypt cycle failed to produce "
                             "original result\n"
                             "   orig (%s)\n"
                             "    new (%s)\n",
                             password, password_again);
  return SVN_NO_ERROR;
}



#endif  /* APU_HAVE_CRYPTO */


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
#ifdef APU_HAVE_CRYPTO
    SVN_TEST_XFAIL2(test_encrypt_decrypt_password,
                   "basic password encryption/decryption test"),
#endif  /* APU_HAVE_CRYPTO */
    SVN_TEST_NULL
  };
