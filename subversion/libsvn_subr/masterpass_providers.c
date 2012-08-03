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
#include "svn_dso.h"
#include "svn_version.h"
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



/*-----------------------------------------------------------------------*/
/* Platform-specific providers                                           */
/*-----------------------------------------------------------------------*/

/* Set *PROVIDER to the master passphrase provider known by
   PROVIDER_NAME, if available for the plaform; set it to NULL
   otherwise.  */
static svn_error_t *
get_provider(svn_auth_provider_object_t **provider,
             const char *provider_name,
             apr_pool_t *pool)
{
  *provider = NULL;

  if (apr_strnatcmp(provider_name, "gnome_keyring") == 0 ||
      apr_strnatcmp(provider_name, "kwallet") == 0)
    {
#if (defined(SVN_HAVE_GNOME_KEYRING) || defined(SVN_HAVE_KWALLET))
      apr_dso_handle_t *dso;
      apr_dso_handle_sym_t provider_func_symbol, version_func_symbol;
      const char *provider_func_name, *version_func_name;
      const char *library_label, *library_name;

      library_name = apr_psprintf(pool, "libsvn_auth_%s-%d.so.0",
                                  provider_name, SVN_VER_MAJOR);
      library_label = apr_psprintf(pool, "svn_%s", provider_name);
      provider_func_name = 
        apr_psprintf(pool,
                     "svn_auth__get_%s_master_passphrase_provider",
                     provider_name);
      version_func_name = 
        apr_psprintf(pool, "svn_auth_%s_version", provider_name);
      SVN_ERR(svn_dso_load(&dso, library_name));
      if (dso)
        {
          if (apr_dso_sym(&version_func_symbol, dso, version_func_name) == 0)
            {
              svn_version_func_t version_func = version_func_symbol;
              svn_version_checklist_t check_list[2];

              check_list[0].label = library_label;
              check_list[0].version_query = version_func;
              check_list[1].label = NULL;
              check_list[1].version_query = NULL;
              SVN_ERR(svn_ver_check_list(svn_subr_version(), check_list));
            }
          if (apr_dso_sym(&provider_func_symbol, dso, provider_func_name) == 0)
            {
              svn_auth_master_passphrase_provider_func_t provider_func =
                provider_func_symbol;
              provider_func(provider, pool);
            }
        }
#endif /* defined(SVN_HAVE_GNOME_KEYRING) || defined(SVN_HAVE_KWALLET) */
    }
  else if (strcmp(provider_name, "gpg_agent") == 0)
    {
#if defined(SVN_HAVE_GPG_AGENT)
      svn_auth_get_gpg_agent_master_passphrase_provider(provider, pool);
#endif /* defined(SVN_HAVE_GPG_AGENT) */
    }
  else if (strcmp(provider_name, "keychain") == 0)
    {
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
      svn_auth_get_keychain_master_passphrase_provider(provider, pool);
#endif /* SVN_HAVE_KEYCHAIN_SERVICES */
    }

  return SVN_NO_ERROR;
}


/* If provider P is non-NULL, add it to the providers LIST. */
#define SVN__MAYBE_ADD_PROVIDER(list,p)                        \
  { if (p) APR_ARRAY_PUSH(list,                                \
                          svn_auth_provider_object_t *) = p; }

svn_error_t *
svn_auth_get_platform_specific_master_passphrase_providers(
  apr_array_header_t **providers,
  svn_config_t *config,
  apr_pool_t *pool)
{
  svn_auth_provider_object_t *provider;
  const char *password_stores_config_option = SVN_AUTH__DEFAULT_PROVIDER_LIST;
  apr_array_header_t *password_stores;
  int i;

  /* Initialize our output. */
  *providers = apr_array_make(pool, 12, sizeof(svn_auth_provider_object_t *));

  if (config)
    {
      svn_config_get(config, &password_stores_config_option,
                     SVN_CONFIG_SECTION_AUTH,
                     SVN_CONFIG_OPTION_PASSWORD_STORES,
                     SVN_AUTH__DEFAULT_PROVIDER_LIST);
    }

  password_stores = svn_cstring_split(password_stores_config_option,
                                      " ,", TRUE, pool);
  for (i = 0; i < password_stores->nelts; i++)
    {
      const char *password_store = APR_ARRAY_IDX(password_stores, i,
                                                 const char *);

      /* GNOME Keyring */
      if (apr_strnatcmp(password_store, "gnome-keyring") == 0)
        {
#if 0
          SVN_ERR(get_provider(&provider, "gnome_keyring", pool));
          SVN__MAYBE_ADD_PROVIDER(*providers, provider);
#endif
          continue;
        }

      /* GPG-Agent */
      if (apr_strnatcmp(password_store, "gpg-agent") == 0)
        {
          SVN_ERR(get_provider(&provider, "gpg_agent", pool));
          SVN__MAYBE_ADD_PROVIDER(*providers, provider);
          continue;
        }

      /* KWallet */
      if (apr_strnatcmp(password_store, "kwallet") == 0)
        {
#if 0
          SVN_ERR(get_provider(&provider, "kwallet", pool));
          SVN__MAYBE_ADD_PROVIDER(*providers, provider);
#endif
          continue;
        }

      /* Keychain */
      if (apr_strnatcmp(password_store, "keychain") == 0)
        {
#if 0
          SVN_ERR(get_provider(&provider, "keychain", pool));
          SVN__MAYBE_ADD_PROVIDER(*providers, provider);
#endif
          continue;
        }

      /* Windows (no master passphrase provider for this platform) */
      if (apr_strnatcmp(password_store, "windows-cryptoapi") == 0)
        continue;

      return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                               _("Invalid config: unknown password store '%s'"),
                               password_store);
    }

  return SVN_NO_ERROR;
}

#undef SVN__MAYBE_ADD_PROVIDER
