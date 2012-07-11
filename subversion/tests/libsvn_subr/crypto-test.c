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

#include "svn_pools.h"
#include "svn_io.h"

#include "../svn_test.h"
#include "../../libsvn_subr/crypto.h"
#include "../../libsvn_subr/auth_store.h"


/*** Helper functions ***/

/* Encrypt PASSWORD within CTX using MASTER, then
   decrypt those results and ensure the original PASSWORD comes out
   the other end. */
static svn_error_t *
encrypt_decrypt(svn_crypto__ctx_t *ctx,
                const svn_string_t *master,
                const char *password,
                apr_pool_t *pool)
{
  const svn_string_t *ciphertext, *iv, *salt;
  const char *password_again;

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


/* Create and open an auth store within CONFIG_DIR, deleting any
   previous auth store at that location, and using CRYPTO_CTX and the
   master passphrase SECRET.  Set *AUTH_STORE_P to the resulting store
   object.  */
static svn_error_t *
create_ephemeral_auth_store(svn_auth__store_t **auth_store_p,
                            const char **auth_store_path,
                            svn_crypto__ctx_t *crypto_ctx,
                            const svn_string_t *secret,
                            apr_pool_t *pool)
{
  SVN_ERR(svn_io_open_uniquely_named(NULL, auth_store_path, NULL,
                                     "auth_store", NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     pool, pool));
  SVN_ERR(svn_io_remove_file2(*auth_store_path, TRUE, pool));
  SVN_ERR(svn_auth__pathetic_store_get(auth_store_p, *auth_store_path,
                                       crypto_ctx, secret, pool, pool));
  SVN_ERR(svn_auth__store_open(*auth_store_p, TRUE, pool));
  return SVN_NO_ERROR;
}



/*** Test functions ***/

static svn_error_t *
test_encrypt_decrypt_password(apr_pool_t *pool)
{
  svn_crypto__ctx_t *ctx;
  const svn_string_t *master = svn_string_create("Pastor Massword", pool);
  int i;
  apr_pool_t *iterpool;
  const char *passwords[] = {
    "3ncryptm!3", /* fits in one block */
    "this is a particularly long password", /* spans blocks */
    "mypassphrase", /* with 4-byte padding, should align on block boundary */
  };

  /* Skip this test if the crypto subsystem is unavailable. */
  if (! svn_crypto__is_available())
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  SVN_ERR(svn_crypto__context_create(&ctx, pool));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < (sizeof(passwords) / sizeof(const char *)); i++)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(encrypt_decrypt(ctx, master, passwords[i], iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
test_passphrase_check(apr_pool_t *pool)
{
  svn_crypto__ctx_t *ctx;
  int i;
  apr_pool_t *iterpool;
  const char *passwords[] = {
    "3ncryptm!3", /* fits in one block */
    "this is a particularly long password", /* spans blocks */
    "mypassphrase", /* with 4-byte padding, should align on block boundary */
  };
  const svn_string_t *ciphertext, *iv, *salt, *secret;
  const char *checktext;
  svn_boolean_t is_valid;
  int num_passwords = sizeof(passwords) / sizeof(const char *);

  /* Skip this test if the crypto subsystem is unavailable. */
  if (! svn_crypto__is_available())
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  SVN_ERR(svn_crypto__context_create(&ctx, pool));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < num_passwords; i++)
    {
      svn_pool_clear(iterpool);
      secret = svn_string_create(passwords[i], iterpool);
      SVN_ERR(svn_crypto__generate_secret_checktext(&ciphertext, &iv, &salt,
                                                    &checktext, ctx, secret,
                                                    iterpool, iterpool));
      SVN_ERR(svn_crypto__verify_secret(&is_valid, ctx, secret, ciphertext,
                                        iv, salt, checktext, iterpool));
      if (! is_valid)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Error validating secret against checktext");
    }

  /* Now check that a bogus secret causes the validation to fail.  We
     try to verify each secret against the checktext generated by the
     previous one.  */
  for (i = 0; i < num_passwords; i++)
    {
      int test_secret_index = (i + 1) % num_passwords;

      svn_pool_clear(iterpool);
      secret = svn_string_create(passwords[i], iterpool);
      SVN_ERR(svn_crypto__generate_secret_checktext(&ciphertext, &iv, &salt,
                                                    &checktext, ctx, secret,
                                                    iterpool, iterpool));
      secret = svn_string_create(passwords[test_secret_index], iterpool);
      SVN_ERR(svn_crypto__verify_secret(&is_valid, ctx, secret, ciphertext,
                                        iv, salt, checktext, iterpool));
      if (is_valid)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Expected secret validation failure; "
                                "got success");
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
test_auth_store_basic(apr_pool_t *pool)
{
  svn_error_t *err;
  svn_crypto__ctx_t *ctx;
  svn_auth__store_t *auth_store;
  const char *auth_store_path;
  const svn_string_t *secret = svn_string_create("My Secret", pool);
  const svn_string_t *bad_secret = svn_string_create("Not My Secret", pool);

  /* Skip this test if the crypto subsystem is unavailable. */
  if (! svn_crypto__is_available())
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  SVN_ERR(svn_crypto__context_create(&ctx, pool));
  SVN_ERR(create_ephemeral_auth_store(&auth_store, &auth_store_path,
                                      ctx, secret, pool));

  /* Close and reopen the auth store. */
  SVN_ERR(svn_auth__store_close(auth_store, pool));
  SVN_ERR(svn_auth__pathetic_store_get(&auth_store, auth_store_path, ctx,
                                       secret, pool, pool));
  SVN_ERR(svn_auth__store_open(auth_store, FALSE, pool));

  /* Close and reopen the auth store with a bogus secret. */
  SVN_ERR(svn_auth__store_close(auth_store, pool));
  SVN_ERR(svn_auth__pathetic_store_get(&auth_store, auth_store_path, ctx,
                                       bad_secret, pool, pool));
  err = svn_auth__store_open(auth_store, FALSE, pool);
  if (! err)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Successfully opened auth store with the wrong "
                            "secret");
  if (err && (err->apr_err == SVN_ERR_AUTHN_FAILED))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  SVN_ERR(err);

  return SVN_NO_ERROR;
}



static svn_error_t *
test_auth_store_get_set(apr_pool_t *pool)
{
  svn_crypto__ctx_t *ctx;
  apr_pool_t *iterpool;
  svn_auth__store_t *auth_store;
  const char *auth_store_path;
  const svn_string_t *secret = svn_string_create("My Secret", pool);
  const char *usernames[] = {
    "jrandom",
    "root",
    "John Boy"
  };
  const char *passwords[] = { /* sync with USERNAMES array length */
    "rayjandom",
    "l33th4x0r",
    "Billy"
  };
  int i;
  svn_auth_cred_username_t *username_creds;
  svn_auth_cred_simple_t *simple_creds;
  const char *realmstring;

  /* Skip this test if the crypto subsystem is unavailable. */
  if (! svn_crypto__is_available())
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);

  SVN_ERR(svn_crypto__context_create(&ctx, pool));
  SVN_ERR(create_ephemeral_auth_store(&auth_store, &auth_store_path,
                                      ctx, secret, pool));

  iterpool = svn_pool_create(pool);

  /* Store some simple and username creds. */
  for (i = 0; i < (sizeof(usernames) / sizeof(const char *)); i++)
    {
      svn_boolean_t stored;

      svn_pool_clear(iterpool);

      realmstring = usernames[i]; /* not schema-jiving */
      username_creds = apr_pcalloc(iterpool, sizeof(*username_creds));
      username_creds->username = usernames[i];
      SVN_ERR(svn_auth__store_set_username_creds(&stored, auth_store,
                                                 realmstring, username_creds,
                                                 iterpool));
      if (! stored)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Error storing username credentials");

    }
  for (i = 0; i < (sizeof(usernames) / sizeof(const char *)); i++)
    {
      svn_boolean_t stored;

      svn_pool_clear(iterpool);

      realmstring = usernames[i]; /* not schema-jiving */
      simple_creds = apr_pcalloc(iterpool, sizeof(*simple_creds));
      simple_creds->username = usernames[i];
      simple_creds->password = passwords[i];
      SVN_ERR(svn_auth__store_set_simple_creds(&stored, auth_store,
                                               realmstring, simple_creds,
                                               iterpool));
      if (! stored)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "Error storing simple credentials");
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_encrypt_decrypt_password,
                   "basic password encryption/decryption test"),
    SVN_TEST_PASS2(test_passphrase_check,
                   "password checktext generation/validation"),
    SVN_TEST_PASS2(test_auth_store_basic,
                   "basic auth store create/open test"),
    SVN_TEST_PASS2(test_auth_store_get_set,
                   "basic auth store get/set creds test"),
    SVN_TEST_NULL
  };
