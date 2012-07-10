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
  svn_auth__store_cb_fetch_t fetch_func;
  svn_auth__store_cb_store_t store_func;

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
svn_auth__store_set_fetch(svn_auth__store_t *auth_store,
                          svn_auth__store_cb_fetch_t func)
{
  auth_store->fetch_func = func;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_set_store(svn_auth__store_t *auth_store,
                          svn_auth__store_cb_store_t func)
{
  auth_store->store_func = func;
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
svn_auth__store_fetch_creds(const void **creds,
                            svn_auth__store_t *auth_store,
                            const char *cred_kind,
                            const char *realmstring,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(auth_store->is_open);
  *creds = NULL;
  if (auth_store->fetch_func)
    SVN_ERR(auth_store->fetch_func(creds, auth_store->store_baton, cred_kind,
                                   realmstring, result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__store_store_creds(svn_boolean_t *stored,
                            svn_auth__store_t *auth_store,
                            const char *cred_kind,
                            const char *realmstring,
                            const void *creds,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(auth_store->is_open);
  *stored = FALSE;
  if (auth_store->store_func)
    SVN_ERR(auth_store->store_func(stored, auth_store->store_baton, cred_kind,
                                   realmstring, creds, scratch_pool));
  return SVN_NO_ERROR;
}
