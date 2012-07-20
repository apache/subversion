/*
 * pathetic_auth_store.c: A pathetic implementation of an encrypted
 *                        auth store.
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

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_auth.h"
#include "svn_base64.h"
#include "private/svn_skel.h"

#include "crypto.h"
#include "auth_store.h"
#include "config_impl.h"

#include "svn_private_config.h"

/* This module implements an encrypted auth store using the popular
 * serialized hash format, whose contents look like so:
 *
 * hash = {
 *   "checktext"          ==> base64(skel(CIPHERTEXT, IV, SALT, CHECKTEXT)),
 *   KIND ":" REALMSTRING ==> base64(skel(CREDCIPHERTEXT, IV, SALT)),
 *   ...
 *   }
 *
 * The decrypted CREDCIPHERTEXT is a base64-encoded skel string
 * containing authn-provider-specific data.
 *
 * KIND is a provider type string ("svn.simple", "svn.username", ...).
 *
 * Oh, it ain't pretty.  It ain't supposed to be. 
 */




typedef struct pathetic_auth_store_baton_t
{
  /* On-disk path of this store. */
  const char *path;

  /* Cryptographic context. */
  svn_crypto__ctx_t *crypto_ctx;

  /* Callback/baton for fetching the master passphrase (aka crypto
     secret). */
  svn_auth__master_passphrase_fetch_t secret_func;
  void *secret_baton;

  /* Crypto secret (may be NULL if not yet provided). */
  const svn_string_t *secret;

  /* Skel containing checktext bits: (CIPHERTEXT, IV, SALT,
     CHECKTEXT).  This needs to be unparsed (stringified) and
     base64-encoded before storage.  */
  svn_skel_t *checktext_skel;

  /* Hash, mapping kind/realmstring keys to skels with credential
     details: (CIPHERTEXT, IV, SALT).  The skels need to be unparsed
     and base64-encoded before storage.  */
  apr_hash_t *realmstring_skels;

  /* Pool for holding all this fun stuff. */
  apr_pool_t *pool;

} pathetic_auth_store_baton_t;


/* Parse the contents of the auth store file represented by
   AUTH_STORE.  */
static svn_error_t *
read_auth_store(pathetic_auth_store_baton_t *auth_store,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  apr_hash_t *hash, *realmstring_skels;
  const svn_string_t *str;
  svn_skel_t *checktext_skel = NULL;
  apr_hash_index_t *hi;
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(auth_store->path, &kind, scratch_pool));
  if (kind == svn_node_none)
    return svn_error_create(SVN_ERR_NODE_NOT_FOUND, NULL,
                            _("Pathetic auth store not found"));
  else if (kind != svn_node_file)
    return svn_error_create(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                            _("Unexpected node kind for pathetic auth store"));

  SVN_ERR_W(svn_stream_open_readonly(&stream, auth_store->path,
                                     scratch_pool, scratch_pool),
            _("Unable to open pathetic auth store for reading"));

  hash = apr_hash_make(scratch_pool);
  err = svn_hash_read2(hash, stream, SVN_HASH_TERMINATOR, scratch_pool);
  if (err)
    return svn_error_createf(err->apr_err, err,
                             _("Error parsing '%s'"),
                             svn_dirent_local_style(auth_store->path,
                                                    scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  str = apr_hash_get(hash, "checktext", APR_HASH_KEY_STRING);
  if (str)
    {
      str = svn_base64_decode_string(str, scratch_pool);
      checktext_skel = svn_skel__parse(str->data, str->len, auth_store->pool);
      apr_hash_set(hash, "checktext", APR_HASH_KEY_STRING, NULL);
    }

  realmstring_skels = apr_hash_make(auth_store->pool);
  for (hi = apr_hash_first(scratch_pool, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      
      apr_hash_this(hi, &key, &klen, &val);
      str = svn_base64_decode_string(val, scratch_pool);
      apr_hash_set(realmstring_skels, apr_pstrdup(auth_store->pool, key), klen,
                   svn_skel__parse(str->data, str->len, auth_store->pool));
    }
  
  auth_store->checktext_skel = checktext_skel;
  auth_store->realmstring_skels = realmstring_skels;

  return SVN_NO_ERROR;
}


/* Unparse the contents of AUTH_STORE to the appropriate on-disk
   location.  If there's no appropriate on-disk location to flush to
   (because there's no configuration directory provided), do nothing.  */
static svn_error_t *
write_auth_store(pathetic_auth_store_baton_t *auth_store,
                 apr_pool_t *scratch_pool)
{
  apr_file_t *authfile = NULL;
  svn_stream_t *stream;
  apr_hash_t *hash = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;
  const svn_string_t *str;

  SVN_ERR_ASSERT(auth_store->checktext_skel);

  SVN_ERR_W(svn_io_file_open(&authfile, auth_store->path,
                             (APR_WRITE | APR_CREATE | APR_TRUNCATE
                              | APR_BUFFERED),
                             APR_OS_DEFAULT, scratch_pool),
            _("Unable to open auth file for writing"));

  str = svn_base64_encode_string2(
            svn_string_create_from_buf(
                svn_skel__unparse(auth_store->checktext_skel, scratch_pool),
                scratch_pool),
            FALSE, scratch_pool);
  apr_hash_set(hash, "checktext", APR_HASH_KEY_STRING, str);
  for (hi = apr_hash_first(scratch_pool, auth_store->realmstring_skels);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      
      apr_hash_this(hi, &key, &klen, &val);
      str = svn_base64_encode_string2(
                svn_string_create_from_buf(svn_skel__unparse(val,
                                                             scratch_pool),
                                           scratch_pool),
                FALSE, scratch_pool);
      apr_hash_set(hash, key, klen, str);
    }

  stream = svn_stream_from_aprfile2(authfile, FALSE, scratch_pool);
  SVN_ERR_W(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, scratch_pool),
            apr_psprintf(scratch_pool, _("Error writing hash to '%s'"),
                         svn_dirent_local_style(auth_store->path,
                                                scratch_pool)));

  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}


/* Create a pathetic auth store file at the path registered with
   the AUTH_STORE object.  */
static svn_error_t *
create_auth_store(pathetic_auth_store_baton_t *auth_store,
                  apr_pool_t *scratch_pool)
{
  const svn_string_t *ciphertext, *iv, *salt;
  const char *checktext;

  SVN_ERR(svn_crypto__generate_secret_checktext(&ciphertext, &iv,
                                                &salt, &checktext,
                                                auth_store->crypto_ctx,
                                                auth_store->secret,
                                                scratch_pool, scratch_pool));

  auth_store->checktext_skel = svn_skel__make_empty_list(auth_store->pool);
  svn_skel__prepend(svn_skel__str_atom(checktext,
                                       auth_store->pool),
                    auth_store->checktext_skel);
  svn_skel__prepend(svn_skel__mem_atom(salt->data, salt->len,
                                       auth_store->pool),
                    auth_store->checktext_skel);
  svn_skel__prepend(svn_skel__mem_atom(iv->data, iv->len,
                                       auth_store->pool),
                    auth_store->checktext_skel);
  svn_skel__prepend(svn_skel__mem_atom(ciphertext->data, ciphertext->len,
                                       auth_store->pool),
                    auth_store->checktext_skel);

  auth_store->realmstring_skels = apr_hash_make(auth_store->pool);
  SVN_ERR(write_auth_store(auth_store, scratch_pool));

  return SVN_NO_ERROR;
}


/* ### TODO: document  */
static svn_error_t *
get_cred_hash(apr_hash_t **cred_hash,
              struct pathetic_auth_store_baton_t *auth_store,
              const char *cred_kind_string,
              const char *realmstring,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *key, *plaintext;
  svn_skel_t *realmstring_skel, *proplist_skel;
  svn_skel_t *cipher_skel, *iv_skel, *salt_skel;
  const svn_string_t *skel_str;

  *cred_hash = NULL;

  SVN_ERR_ASSERT(realmstring);
  SVN_ERR_ASSERT(cred_kind_string);

  key = apr_pstrcat(scratch_pool, cred_kind_string, ":", realmstring, NULL);
  realmstring_skel = apr_hash_get(auth_store->realmstring_skels,
                                  key, APR_HASH_KEY_STRING);
  if (! realmstring_skel)
    return SVN_NO_ERROR;

  cipher_skel = realmstring_skel->children;
  iv_skel     = realmstring_skel->children->next;
  salt_skel   = realmstring_skel->children->next->next;

  SVN_ERR(svn_crypto__decrypt_password(&plaintext,
                                       auth_store->crypto_ctx,
                                       svn_string_ncreate(cipher_skel->data,
                                                          cipher_skel->len,
                                                          scratch_pool),
                                       svn_string_ncreate(iv_skel->data,
                                                          iv_skel->len,
                                                          scratch_pool),
                                       svn_string_ncreate(salt_skel->data,
                                                          salt_skel->len,
                                                          scratch_pool),
                                       auth_store->secret,
                                       scratch_pool, scratch_pool));

  skel_str = svn_base64_decode_string(svn_string_create(plaintext,
                                                        scratch_pool),
                                      scratch_pool);
  proplist_skel = svn_skel__parse(skel_str->data, skel_str->len, scratch_pool);
  SVN_ERR(svn_skel__parse_proplist(cred_hash, proplist_skel, result_pool));

  return SVN_NO_ERROR;
}


/* ### TODO: document  */
static svn_error_t *
set_cred_hash(struct pathetic_auth_store_baton_t *auth_store,
              const char *cred_kind_string,
              const char *realmstring,
              apr_hash_t *cred_hash,
              apr_pool_t *scratch_pool)
{
  const char *key;
  svn_skel_t *proplist_skel, *realmstring_skel;
  svn_stringbuf_t *skel_buf;
  const svn_string_t *skel_str;
  const svn_string_t *ciphertext, *iv, *salt;

  SVN_ERR(svn_skel__unparse_proplist(&proplist_skel, cred_hash, scratch_pool));
  skel_buf = svn_skel__unparse(proplist_skel, scratch_pool);
  skel_str = svn_base64_encode_string2(svn_string_ncreate(skel_buf->data,
                                                          skel_buf->len,
                                                          scratch_pool),
                                       FALSE, scratch_pool);
                        
  SVN_ERR(svn_crypto__encrypt_password(&ciphertext, &iv, &salt,
                                       auth_store->crypto_ctx, skel_str->data,
                                       auth_store->secret, auth_store->pool,
                                       scratch_pool));
  
  realmstring_skel = svn_skel__make_empty_list(auth_store->pool);
  svn_skel__prepend(svn_skel__mem_atom(salt->data, salt->len,
                                       auth_store->pool),
                    realmstring_skel);
  svn_skel__prepend(svn_skel__mem_atom(iv->data, iv->len,
                                       auth_store->pool),
                    realmstring_skel);
  svn_skel__prepend(svn_skel__mem_atom(ciphertext->data, ciphertext->len,
                                       auth_store->pool),
                    realmstring_skel);

  key = apr_pstrcat(auth_store->pool, cred_kind_string, ":",
                    realmstring, NULL);
  apr_hash_set(auth_store->realmstring_skels, key,
               APR_HASH_KEY_STRING, realmstring_skel);

  SVN_ERR(write_auth_store(auth_store, scratch_pool));

  return SVN_NO_ERROR;
}


/*** svn_auth__store_t Callback Functions ***/

/* Implements svn_auth__store_cb_open_t. */
static svn_error_t *
pathetic_store_open(void *baton,
                    svn_boolean_t create,
                    apr_pool_t *scratch_pool)
{
  pathetic_auth_store_baton_t *auth_store = baton;
  svn_error_t *err;
  svn_skel_t *cipher_skel, *iv_skel, *salt_skel, *check_skel;
  svn_boolean_t valid_secret;

  SVN_ERR(auth_store->secret_func(&(auth_store->secret),
                                  auth_store->secret_baton,
                                  auth_store->pool,
                                  scratch_pool));

  err = read_auth_store(auth_store, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_NODE_NOT_FOUND)
        {
          if (create)
            {
              svn_error_clear(err);
              return svn_error_trace(create_auth_store(auth_store,
                                                       scratch_pool));
            }
          else
            {
              return err;
            }
        }
      else
        {
          return err;
        }
    }
                            
  cipher_skel = auth_store->checktext_skel->children;
  iv_skel     = auth_store->checktext_skel->children->next;
  salt_skel   = auth_store->checktext_skel->children->next->next;
  check_skel  = auth_store->checktext_skel->children->next->next->next;

  SVN_ERR(svn_crypto__verify_secret(&valid_secret,
                                    auth_store->crypto_ctx,
                                    auth_store->secret,
                                    svn_string_ncreate(cipher_skel->data,
                                                       cipher_skel->len,
                                                       scratch_pool),
                                    svn_string_ncreate(iv_skel->data,
                                                       iv_skel->len,
                                                       scratch_pool),
                                    svn_string_ncreate(salt_skel->data,
                                                       salt_skel->len,
                                                       scratch_pool),
                                    apr_pstrmemdup(scratch_pool,
                                                   check_skel->data,
                                                   check_skel->len),
                                    scratch_pool));
  if (! valid_secret)
    return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL, _("Invalid secret"));

  return SVN_NO_ERROR;
}

/* Implements pathetic_store_delete_t. */
static svn_error_t *
pathetic_store_delete(void *baton,
                      apr_pool_t *scratch_pool)
{
  pathetic_auth_store_baton_t *auth_store = baton;
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(auth_store->path, &kind, scratch_pool));
  if (kind == svn_node_none)
    return svn_error_create(SVN_ERR_NODE_NOT_FOUND, NULL,
                            _("Pathetic auth store not found"));
  else if (kind != svn_node_file)
    return svn_error_create(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                            _("Unexpected node kind for pathetic auth store"));
  
  SVN_ERR(svn_io_remove_file2(auth_store->path, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements pathetic_store_get_cred_hash_t. */
static svn_error_t *
pathetic_store_get_cred_hash(apr_hash_t **cred_hash, 
                             void *baton,
                             const char *cred_kind,
                             const char *realmstring,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  pathetic_auth_store_baton_t *auth_store = baton;

  SVN_ERR(get_cred_hash(cred_hash, auth_store, cred_kind,
                        realmstring, result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements pathetic_store_set_cred_hash_t. */
static svn_error_t *
pathetic_store_set_cred_hash(svn_boolean_t *stored,
                             void *baton,
                             const char *cred_kind,
                             const char *realmstring,
                             apr_hash_t *cred_hash,
                             apr_pool_t *scratch_pool)
{
  pathetic_auth_store_baton_t *auth_store = baton;

  SVN_ERR(set_cred_hash(auth_store, cred_kind, realmstring,
                        cred_hash, scratch_pool));
  *stored = TRUE;
    
  return SVN_NO_ERROR;
}



/*** Semi-public APIs ***/

svn_error_t *
svn_auth__pathetic_store_get(svn_auth__store_t **auth_store_p,
                             const char *auth_store_path,
                             svn_crypto__ctx_t *crypto_ctx,
                             svn_auth__master_passphrase_fetch_t secret_func,
                             void *secret_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_auth__store_t *auth_store;
  pathetic_auth_store_baton_t *pathetic_store;

  SVN_ERR_ASSERT(secret_func);

  if (! svn_crypto__is_available())
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            _("Encrypted auth store feature not available"));

  pathetic_store = apr_pcalloc(result_pool, sizeof(*pathetic_store));
  pathetic_store->pool = result_pool;
  pathetic_store->path = apr_pstrdup(result_pool, auth_store_path);
  pathetic_store->crypto_ctx = crypto_ctx;
  pathetic_store->secret_func = secret_func;
  pathetic_store->secret_baton = secret_baton;

  SVN_ERR(svn_auth__store_create(&auth_store, result_pool));
  SVN_ERR(svn_auth__store_set_baton(auth_store, pathetic_store));
  SVN_ERR(svn_auth__store_set_open(auth_store, pathetic_store_open));
  SVN_ERR(svn_auth__store_set_delete(auth_store, pathetic_store_delete));
  SVN_ERR(svn_auth__store_set_get_cred_hash(auth_store,
                                            pathetic_store_get_cred_hash));
  SVN_ERR(svn_auth__store_set_set_cred_hash(auth_store,
                                            pathetic_store_set_cred_hash));

  *auth_store_p = auth_store;

  return SVN_NO_ERROR;
}


