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
                  apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* Implements pathetic_store_fetch_t. */
static svn_error_t *
config_store_get_cred_hash(apr_hash_t **cred_hash,
                           void *baton,
                           const char *cred_kind,
                           const char *realmstring,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *config_dir = baton;

  SVN_ERR(svn_config_read_auth_data(cred_hash, cred_kind, realmstring,
                                    config_dir, scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements pathetic_store_store_t. */
static svn_error_t *
config_store_set_cred_hash(svn_boolean_t *stored,
                           void *baton,
                           const char *cred_kind,
                           const char *realmstring,
                           apr_hash_t *cred_hash,
                           apr_pool_t *scratch_pool)
{
  const char *config_dir = baton;

  SVN_ERR(svn_config_write_auth_data(cred_hash, cred_kind, realmstring,
                                     config_dir, scratch_pool));
  *stored = TRUE;
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
  SVN_ERR(svn_auth__store_set_baton(auth_store, (void *)config_dir));
  SVN_ERR(svn_auth__store_set_open(auth_store, config_store_open));
  SVN_ERR(svn_auth__store_set_get_cred_hash(auth_store,
                                            config_store_get_cred_hash));
  SVN_ERR(svn_auth__store_set_set_cred_hash(auth_store,
                                            config_store_set_cred_hash));
  *auth_store_p = auth_store;

  return SVN_NO_ERROR;
}


