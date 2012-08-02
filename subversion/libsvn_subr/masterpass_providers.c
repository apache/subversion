/*
 * masterpass_providers.c: providers for SVN_AUTH_CRED_MASTER_PASSPHRASE
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


#include <apr_pools.h>

#include "svn_auth.h"
#include "svn_error.h"
#include "svn_config.h"
#include "svn_string.h"
#include "auth_store.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"



/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for master passphrase prompting. */
typedef struct master_passphrase_prompt_provider_baton_t
{
  /* Prompting function/baton pair. */
  svn_auth_master_passphrase_prompt_func_t prompt_func;
  void *prompt_baton;

  /* How many times to re-prompt? */
  int retry_limit;

} master_passphrase_prompt_provider_baton_t;


/* Iteration baton. */
typedef struct master_passphrase_prompt_iter_baton_t
{
  /* The original provider baton */
  master_passphrase_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* How many times have we reprompted? */
  int retries;

} master_passphrase_prompt_iter_baton_t;


static svn_error_t *
master_passphrase_prompt_first_cred(void **credentials_p,
                                    void **iter_baton,
                                    void *provider_baton,
                                    apr_hash_t *parameters,
                                    const char *realmstring,
                                    apr_pool_t *pool)
{
  master_passphrase_prompt_provider_baton_t *pb = provider_baton;
  master_passphrase_prompt_iter_baton_t *ib = apr_pcalloc(pool, sizeof(*ib));
  const char *no_auth_cache = apr_hash_get(parameters,
                                           SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                           APR_HASH_KEY_STRING);

  SVN_ERR(pb->prompt_func((svn_auth_cred_master_passphrase_t **)
                          credentials_p, pb->prompt_baton, realmstring,
                          ! no_auth_cache, pool));

  ib->pb = pb;
  ib->realmstring = apr_pstrdup(pool, realmstring);
  ib->retries = 0;
  *iter_baton = ib;

  return SVN_NO_ERROR;
}


static svn_error_t *
master_passphrase_prompt_next_cred(void **credentials_p,
                                   void *iter_baton,
                                   void *provider_baton,
                                   apr_hash_t *parameters,
                                   const char *realmstring,
                                   apr_pool_t *pool)
{
  master_passphrase_prompt_iter_baton_t *ib = iter_baton;
  const char *no_auth_cache = apr_hash_get(parameters,
                                           SVN_AUTH_PARAM_NO_AUTH_CACHE,
                                           APR_HASH_KEY_STRING);

  if ((ib->pb->retry_limit >= 0) && (ib->retries >= ib->pb->retry_limit))
    {
      /* Give up and go on to the next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  return ib->pb->prompt_func((svn_auth_cred_master_passphrase_t **)
                             credentials_p, ib->pb->prompt_baton,
                             ib->realmstring, ! no_auth_cache, pool);
}


static const svn_auth_provider_t master_passphrase_prompt_provider = {
  SVN_AUTH_CRED_MASTER_PASSPHRASE,
  master_passphrase_prompt_first_cred,
  master_passphrase_prompt_next_cred,
  NULL
};


void svn_auth_get_master_passphrase_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_master_passphrase_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  master_passphrase_prompt_provider_baton_t *pb =
    apr_palloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &master_passphrase_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
