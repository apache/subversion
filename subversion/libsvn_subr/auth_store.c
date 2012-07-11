/*
 * auth_store.c: Generic authentication credential storage routines.
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

#include "auth_store.h"

struct svn_auth__store_t
{
  void *store_baton;
  svn_boolean_t is_open;
  svn_auth__store_cb_open_t open_func;
  svn_auth__store_cb_close_t close_func;
  svn_auth__store_cb_delete_t delete_func;
  svn_auth__store_cb_get_cred_hash_t get_cred_hash_func;
  svn_auth__store_cb_set_cred_hash_t set_cred_hash_func;

};


svn_error_t *
svn_auth__store_create(svn_auth__store_t **auth_store,
                       apr_pool_t *result_pool)
{
  *auth_store = apr_pcalloc(result_pool, sizeof(**auth_store));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_baton(svn_auth__store_t *auth_store,
                          void *priv_baton)
{
  auth_store->store_baton = priv_baton;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_open(svn_auth__store_t *auth_store,
                         svn_auth__store_cb_open_t func)
{
  auth_store->open_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_close(svn_auth__store_t *auth_store,
                          svn_auth__store_cb_close_t func)
{
  auth_store->close_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_delete(svn_auth__store_t *auth_store,
                           svn_auth__store_cb_delete_t func)
{
  auth_store->delete_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_get_cred_hash(svn_auth__store_t *auth_store,
                                  svn_auth__store_cb_get_cred_hash_t func)
{
  auth_store->get_cred_hash_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_set_cred_hash(svn_auth__store_t *auth_store,
                                  svn_auth__store_cb_set_cred_hash_t func)
{
  auth_store->set_cred_hash_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_open(svn_auth__store_t *auth_store,
                     svn_boolean_t create,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(! auth_store->is_open);
  if (auth_store->open_func)
    {
      SVN_ERR(auth_store->open_func(auth_store->store_baton, create,
                                    scratch_pool));
      auth_store->is_open = TRUE;
    }
  else
    {
      return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
    }
  return SVN_NO_ERROR;
}
                     

svn_error_t *
svn_auth__store_close(svn_auth__store_t *auth_store,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(auth_store->is_open);
  if (auth_store->close_func)
    SVN_ERR(auth_store->close_func(auth_store->store_baton, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_delete(svn_auth__store_t *auth_store,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(! auth_store->is_open);
  if (auth_store->delete_func)
    SVN_ERR(auth_store->delete_func(auth_store->store_baton, scratch_pool));
  else
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_get_cred_hash(apr_hash_t **cred_hash,
                              svn_auth__store_t *auth_store,
                              const char *cred_kind,
                              const char *realmstring,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(auth_store->is_open);
  *cred_hash = NULL;
  if (auth_store->get_cred_hash_func)
    SVN_ERR(auth_store->get_cred_hash_func(cred_hash, auth_store->store_baton,
                                           cred_kind, realmstring,
                                           result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_cred_hash(svn_boolean_t *stored,
                              svn_auth__store_t *auth_store,
                              const char *cred_kind,
                              const char *realmstring,
                              apr_hash_t *cred_hash,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(auth_store->is_open);
  *stored = FALSE;
  if (auth_store->set_cred_hash_func)
    SVN_ERR(auth_store->set_cred_hash_func(stored, auth_store->store_baton,
                                           cred_kind, realmstring, cred_hash,
                                           scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_get_username_creds(svn_auth_cred_username_t **creds_p,
                                   svn_auth__store_t *auth_store,
                                   const char *realmstring,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  apr_hash_t *cred_hash;

  *creds_p = NULL;
  SVN_ERR(svn_auth__store_get_cred_hash(&cred_hash, auth_store,
                                        SVN_AUTH_CRED_USERNAME, realmstring,
                                        result_pool, scratch_pool));
  if (cred_hash)
    {
      const svn_string_t *prop;
      svn_auth_cred_username_t *creds =
        apr_pcalloc(result_pool, sizeof(*creds));

      prop = apr_hash_get(cred_hash, "username", APR_HASH_KEY_STRING);
      if (prop)
        creds->username = prop->data;
      *creds_p = creds;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_username_creds(svn_boolean_t *stored,
                                   svn_auth__store_t *auth_store,
                                   const char *realmstring,
                                   svn_auth_cred_username_t *creds,
                                   apr_pool_t *scratch_pool)
{
  apr_hash_t *cred_hash = apr_hash_make(scratch_pool);

  if (creds)
    {
      if (creds->username)
        apr_hash_set(cred_hash, "username", APR_HASH_KEY_STRING,
                     svn_string_create(creds->username, scratch_pool));
    }

  SVN_ERR(svn_auth__store_set_cred_hash(stored, auth_store,
                                        SVN_AUTH_CRED_USERNAME, realmstring,
                                        cred_hash, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_get_simple_creds(svn_auth_cred_simple_t **creds_p,
                                 svn_auth__store_t *auth_store,
                                 const char *realmstring,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *cred_hash;

  *creds_p = NULL;
  SVN_ERR(svn_auth__store_get_cred_hash(&cred_hash, auth_store,
                                        SVN_AUTH_CRED_SIMPLE, realmstring,
                                        result_pool, scratch_pool));
  if (cred_hash)
    {
      const svn_string_t *prop;
      svn_auth_cred_simple_t *creds
        = apr_pcalloc(result_pool, sizeof(*creds));

      prop = apr_hash_get(cred_hash, "username", APR_HASH_KEY_STRING);
      if (prop)
        creds->username = prop->data;
      prop = apr_hash_get(cred_hash, "password", APR_HASH_KEY_STRING);
      if (prop)
        creds->password = prop->data;
      *creds_p = creds;
    }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_auth__store_set_simple_creds(svn_boolean_t *stored,
                                 svn_auth__store_t *auth_store,
                                 const char *realmstring,
                                 svn_auth_cred_simple_t *creds,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *cred_hash = apr_hash_make(scratch_pool);

  if (creds)
    {
      if (creds->username)
        apr_hash_set(cred_hash, "username", APR_HASH_KEY_STRING,
                     svn_string_create(creds->username, scratch_pool));
      if (creds->password)
        apr_hash_set(cred_hash, "password", APR_HASH_KEY_STRING,
                     svn_string_create(creds->password, scratch_pool));
    }

  SVN_ERR(svn_auth__store_set_cred_hash(stored, auth_store,
                                        SVN_AUTH_CRED_SIMPLE, realmstring,
                                        cred_hash, scratch_pool));
  return SVN_NO_ERROR;
}
