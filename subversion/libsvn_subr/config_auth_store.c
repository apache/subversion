/*
 * config_auth_store.c: Implementation of an runtime-config auth store.
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

#include "auth_store.h"
#include "config_impl.h"

#include "svn_private_config.h"


/*** svn_auth__store_t Callback Functions ***/

/* Implements svn_auth__store_cb_open_t. */
static svn_error_t *
config_store_open(void *baton,
                  svn_boolean_t create,
                  apr_pool_t *scratch_pool)
{
  const svn_config_t *cfg = baton;
  return SVN_NO_ERROR;
}

/* Implements pathetic_store_fetch_t. */
static svn_error_t *
config_store_fetch(const void **creds_p, 
                   void *baton,
                   const char *cred_kind,
                   const char *realmstring,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *config_dir = baton;
  apr_hash_t *cred_hash;

  SVN_ERR(svn_config_read_auth_data(&cred_hash, cred_kind, realmstring,
                                    config_dir, scratch_pool));
  
  if (strcmp(cred_kind, SVN_AUTH_CRED_USERNAME) == 0)
    {
      svn_auth_cred_username_t *creds =
        apr_pcalloc(result_pool, sizeof(*creds));
      prop = apr_hash_get(cred_hash, "username", APR_HASH_KEY_STRING);
      if (prop)
        creds->username = prop->data;
      *creds_p = (const void *)creds;
    }
  else if (strcmp(cred_kind, SVN_AUTH_CRED_SIMPLE) == 0)
    {
      svn_auth_cred_simple_t *creds =
        apr_pcalloc(result_pool, sizeof(*creds));
      prop = apr_hash_get(cred_hash, "username", APR_HASH_KEY_STRING);
      if (prop)
        creds->username = prop->data;
      prop = apr_hash_get(cred_hash, "password", APR_HASH_KEY_STRING);
      if (prop)
        creds->username = prop->data;
      *creds_p = (const void *)creds;
    }
  else
    {
      *creds_p = NULL;
    }

  return SVN_NO_ERROR;
}

/* Implements pathetic_store_store_t. */
static svn_error_t *
config_store_store(svn_boolean_t *stored,
                   void *baton,
                   const char *cred_kind,
                   const char *realmstring,
                   const void *generic_creds,
                   apr_pool_t *scratch_pool)
{
  const const *config_dir = baton;
  apr_hash_t *cred_hash = NULL;

  *stored = FALSE;

  if (strcmp(cred_kind, SVN_AUTH_CRED_USERNAME) == 0)
    {
      const svn_auth_cred_username_t *creds = generic_creds;
      cred_hash = apr_hash_make(scratch_pool);
      if (creds->username)
        apr_hash_set(cred_hash, "username", APR_HASH_KEY_STRING,
                     svn_string_create(creds->username, scratch_pool));
    }
  else if (strcmp(cred_kind, SVN_AUTH_CRED_SIMPLE) == 0)
    {
      const svn_auth_cred_simple_t *creds = generic_creds;
      cred_hash = apr_hash_make(scratch_pool);
      if (creds->username)
        apr_hash_set(cred_hash, "username", APR_HASH_KEY_STRING,
                     svn_string_create(creds->username, scratch_pool));
      if (creds->password)
        apr_hash_set(cred_hash, "password", APR_HASH_KEY_STRING,
                     svn_string_create(creds->password, scratch_pool));
    }

  if (cred_hash)
    {
      SVN_ERR(svn_config_write_auth_data(cred_hash, cred_kind, realmstring,
                                         config_dir, scratch_pool));
      *stored = TRUE;
    }
    
  return SVN_NO_ERROR;
}



/*** Semi-public APIs ***/

svn_error_t *
svn_auth__config_store_get(svn_auth__store_t **auth_store_p,
                           const char *config_dir,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_auth__store_t *auth_store;

  SVN_ERR(svn_auth__store_create(&auth_store, result_pool));
  SVN_ERR(svn_auth__store_set_baton(auth_store, config_dir));
  SVN_ERR(svn_auth__store_set_open(auth_store, pathetic_store_open));
  SVN_ERR(svn_auth__store_set_fetch(auth_store, pathetic_store_fetch));
  SVN_ERR(svn_auth__store_set_store(auth_store, pathetic_store_store));

  *auth_store_p = auth_store;

  return SVN_NO_ERROR;
}


