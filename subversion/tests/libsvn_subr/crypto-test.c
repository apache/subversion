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
test_encrypt_decrypt(apr_pool_t *pool)
{
  /* ### TODO:  Anything! */

  return SVN_NO_ERROR;
}



#endif  /* APU_HAVE_CRYPTO */


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
#ifdef APU_HAVE_CRYPTO
    SVN_TEST_PASS2(test_encrypt_decrypt,
                   "basic encryption/decryption test"),
#endif  /* APU_HAVE_CRYPTO */
    SVN_TEST_NULL
  };
