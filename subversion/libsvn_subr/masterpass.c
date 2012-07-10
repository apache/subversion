/*
 * masterpass.c: master passphrase support functions for Subversion
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
#include <apr_tables.h>
#include <apr_strings.h>

#include "svn_string.h"
#include "svn_error.h"
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_private_config.h"
#include "svn_dso.h"
#include "svn_base64.h"
#include "svn_version.h"
#include "private/svn_auth_private.h"

#include "crypto.h"

static svn_error_t *
get_masterpass_provider(svn_auth__masterpass_provider_object_t **provider,
                        const char *provider_name,
                        apr_pool_t *pool)
{
  *provider = NULL;

  if (apr_strnatcmp(provider_name, "gnome_keyring") == 0 ||
      apr_strnatcmp(provider_name, "kwallet") == 0)
    {
#if defined(SVN_HAVE_GNOME_KEYRING) || defined(SVN_HAVE_KWALLET)
      apr_dso_handle_t *dso;
      apr_dso_handle_sym_t provider_function_symbol, version_function_symbol;
      const char *library_label, *library_name;
      const char *provider_function_name, *version_function_name;
      library_name = apr_psprintf(pool,
                                  "libsvn_auth_%s-%d.so.0",
                                  provider_name,
                                  SVN_VER_MAJOR);
      library_label = apr_psprintf(pool, "svn_%s", provider_name);
      provider_function_name =
        apr_psprintf(pool, "svn_auth_get_%s_masterpass_provider",
                     provider_name);
      version_function_name = apr_psprintf(pool, "svn_auth_%s_version",
                                           provider_name);
      SVN_ERR(svn_dso_load(&dso, library_name));
      if (dso)
        {
          if (apr_dso_sym(&version_function_symbol,
                          dso,
                          version_function_name) == 0)
            {
              svn_version_func_t version_function
                = version_function_symbol;
              const svn_version_checklist_t check_list[] =
                {
                  { library_label, version_function },
                  { NULL, NULL }
                };
              SVN_ERR(svn_ver_check_list(svn_subr_version(), check_list));
            }
          if (apr_dso_sym(&provider_function_symbol,
                          dso,
                          provider_function_name) == 0)
            {
              svn_auth__masterpass_provider_func_t provider_function =
                provider_function_symbol;
              provider_function(provider, pool);
            }
        }
#endif
    }
  else
    {
#if defined(SVN_HAVE_GPG_AGENT)
      if (strcmp(provider_name, "gpg_agent") == 0)
        {
          svn_auth__get_gpg_agent_masterpass_provider(provider, pool);
        }
#endif
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
      /* ### TODO: Implement MacOS X Keychain provider. */
#endif
#if defined(WIN32) && !defined(__MINGW32__)
      /* ### TODO: Can Windows actually use this?  It's less of a
         ### store and more of a service.  */
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_auth__get_masterpass_providers(apr_array_header_t **providers,
                                   svn_config_t *config,
                                   apr_pool_t *pool)
{
  svn_auth__masterpass_provider_object_t *provider;
  const char *password_stores_config_option;
  apr_array_header_t *password_stores;
  int i;

#define SVN__DEFAULT_AUTH_PROVIDER_LIST \
         "gnome-keyring,kwallet,keychain,gpg-agent,windows-cryptoapi"

  if (config)
    {
      svn_config_get(config, &password_stores_config_option,
                     SVN_CONFIG_SECTION_AUTH,
                     SVN_CONFIG_OPTION_PASSWORD_STORES,
                     SVN__DEFAULT_AUTH_PROVIDER_LIST);
    }
  else
    {
      password_stores_config_option = SVN__DEFAULT_AUTH_PROVIDER_LIST;
    }

  *providers = apr_array_make(pool, 12, sizeof(svn_auth_provider_object_t *));

  password_stores = svn_cstring_split(password_stores_config_option,
                                      " ,", TRUE, pool);

  for (i = 0; i < password_stores->nelts; i++)
    {
      const char *password_store =
        APR_ARRAY_IDX(password_stores, i, const char *);

      /* GNOME Keyring */
      if (apr_strnatcmp(password_store, "gnome-keyring") == 0)
        {
          SVN_ERR(get_masterpass_provider(&provider, "gnome_keyring", pool));
          if (provider)
            APR_ARRAY_PUSH(*providers,
                           svn_auth__masterpass_provider_object_t *) = provider;
          continue;
        }

      /* GPG-AGENT */
      if (apr_strnatcmp(password_store, "gpg-agent") == 0)
        {
          SVN_ERR(get_masterpass_provider(&provider, "gpg_agent", pool));
          if (provider)
            APR_ARRAY_PUSH(*providers,
                           svn_auth__masterpass_provider_object_t *) = provider;
          continue;
        }

      /* KWallet */
      if (apr_strnatcmp(password_store, "kwallet") == 0)
        {
          SVN_ERR(get_masterpass_provider(&provider, "kwallet", pool));
          if (provider)
            APR_ARRAY_PUSH(*providers,
                           svn_auth__masterpass_provider_object_t *) = provider;
          continue;
        }

      /* Keychain */
      if (apr_strnatcmp(password_store, "keychain") == 0)
        {
          SVN_ERR(get_masterpass_provider(&provider, "keychain", pool));
          if (provider)
            APR_ARRAY_PUSH(*providers,
                           svn_auth__masterpass_provider_object_t *) = provider;
          continue;
        }

      /* Windows */
      if (apr_strnatcmp(password_store, "windows-cryptoapi") == 0)
        {
          SVN_ERR(get_masterpass_provider(&provider, "windows", pool));
          if (provider)
            APR_ARRAY_PUSH(*providers,
                           svn_auth__masterpass_provider_object_t *) = provider;
          continue;
        }

      return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                               _("Invalid config: unknown password store '%s'"),
                               password_store);
    }

  return SVN_NO_ERROR;
}


/*** Master Passphrase ***/

#define AUTHN_MASTER_PASS_KNOWN_TEXT  "Subversion"
#define AUTHN_FAUX_REALMSTRING        "localhost.localdomain"
#define AUTHN_CHECKTEXT_KEY           "checktext"
#define AUTHN_PASSTYPE_KEY            "passtype"


/* Use SECRET to encrypt TEXT, returning the result (allocated from
   RESULT_POOL) in *CRYPT_TEXT.  Use SCRATCH_POOL for temporary
   allocations. */
static svn_error_t *
encrypt_text(const svn_string_t **crypt_text,
             const svn_string_t *text,
             const char *secret,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  /* ### FIXME!  This a mindless temporary implementation, offering
         all the security and privacy of a glass bathroom!  ***/

  SVN_ERR_ASSERT(text && text->data);
  SVN_ERR_ASSERT(secret);

  *crypt_text = svn_base64_encode_string2(svn_string_createf(scratch_pool,
                                                             "%s+%s",
                                                             text->data,
                                                             secret),
                                          FALSE, result_pool);
  return SVN_NO_ERROR;
}


/* Use SECRET to decrypt CRYPT_TEXT, returning the result (allocated
   from RESULT_POOL) in *TEXT.  Use SCRATCH_POOL for temporary
   allocations. */
static svn_error_t *
decrypt_text(const svn_string_t **text,
             const svn_string_t *crypt_text,
             const char *secret,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  /* ### FIXME!  This a mindless temporary implementation, offering
         all the security and privacy of a glass bathroom!  ***/

  const svn_string_t *work_text;
  int secret_len, text_len;

  SVN_ERR_ASSERT(crypt_text && crypt_text->data);
  SVN_ERR_ASSERT(secret);

  secret_len = strlen(secret);
  work_text = svn_base64_decode_string(crypt_text, scratch_pool);
  if (work_text->len < (secret_len + 1))
    return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                            "Invalid master passphrase.");
  text_len = work_text->len - secret_len - 1;
  if (work_text->data[text_len] != '+')
    return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                            "Invalid master passphrase.");
  if (strcmp(work_text->data + text_len + 1, secret) != 0)
    return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                            "Invalid master passphrase.");
  *text = svn_string_ncreate(work_text->data,
                             work_text->len - secret_len - 1,
                             result_pool);
  return SVN_NO_ERROR;
}
             

svn_error_t *
svn_auth_master_passphrase_get(const char **passphrase,
                               svn_auth_baton_t *auth_baton,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *creds_hash;
  const svn_string_t *check_text;
  const char *config_dir = svn_auth_get_parameter(auth_baton,
                                                  SVN_AUTH_PARAM_CONFIG_DIR);
  const char *default_passphrase =
    svn_auth_get_parameter(auth_baton,
                           SVN_AUTH_PARAM_DEFAULT_MASTER_PASSPHRASE);

  /* Read the existing passphrase storage record so we can validate
     any master passphrase we have or fetch. If there's no check text,
     we must assume that there's no global master passphrase set, so
     we'll just return that fact. */
  SVN_ERR(svn_config_read_auth_data(&creds_hash,
                                    SVN_AUTH_CRED_MASTER_PASSPHRASE,
                                    AUTHN_FAUX_REALMSTRING, config_dir,
                                    scratch_pool));
  check_text = apr_hash_get(creds_hash, AUTHN_CHECKTEXT_KEY,
                            APR_HASH_KEY_STRING);
  if (! check_text)
    {
      *passphrase = NULL;
      return SVN_NO_ERROR;
    }

  /* If there's a default passphrase, verify that it matches the
     stored known-text.  */
  if (default_passphrase)
    {
      const svn_string_t *crypt_text;
      SVN_ERR(encrypt_text(&crypt_text,
                           svn_string_create(AUTHN_MASTER_PASS_KNOWN_TEXT,
                                             scratch_pool),
                           default_passphrase, scratch_pool, scratch_pool));
      if (svn_string_compare(crypt_text, check_text))
        {
          *passphrase = apr_pstrdup(result_pool, default_passphrase);
          return SVN_NO_ERROR;
        }
      default_passphrase = NULL;
    }

  /* We do not yet know the master passphrase, so we need to consult
     the providers.  */
  /* ### TODO ### */

  default_passphrase = NULL;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_auth_master_passphrase_set(svn_auth_baton_t *auth_baton,
                               const char *new_passphrase,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *creds_hash;
  const char *config_dir = svn_auth_get_parameter(auth_baton,
                                                  SVN_AUTH_PARAM_CONFIG_DIR);
  const char *old_passphrase;
  const svn_string_t *old_check_text, *new_check_text;

  /* First, fetch the existing passphrase. */
  SVN_ERR(svn_auth_master_passphrase_get(&old_passphrase, auth_baton,
                                         scratch_pool, scratch_pool));

  /* Now, read the existing passphrase storage record and grab the
     current checkidentify. */
  SVN_ERR(svn_config_read_auth_data(&creds_hash,
                                    SVN_AUTH_CRED_MASTER_PASSPHRASE,
                                    AUTHN_FAUX_REALMSTRING, config_dir,
                                    scratch_pool));
  old_check_text = apr_hash_get(creds_hash, AUTHN_CHECKTEXT_KEY,
                                APR_HASH_KEY_STRING);

  SVN_ERR(svn_config_write_auth_data(creds_hash,
                                     SVN_AUTH_CRED_MASTER_PASSPHRASE,
                                     AUTHN_FAUX_REALMSTRING, config_dir,
                                     scratch_pool));

  if (new_passphrase)
    {
      /* Encrypt the known text with NEW_PASSPHRASE to form the crypttext,
         and stuff that into the CREDS_HASH. */
      SVN_ERR(encrypt_text(&new_check_text,
                           svn_string_create(AUTHN_MASTER_PASS_KNOWN_TEXT,
                                             scratch_pool),
                           new_passphrase, scratch_pool, scratch_pool));
      apr_hash_set(creds_hash, AUTHN_CHECKTEXT_KEY,
                   APR_HASH_KEY_STRING, new_check_text);
    }
  else
    {
      apr_hash_set(creds_hash, AUTHN_CHECKTEXT_KEY, APR_HASH_KEY_STRING, NULL);
    }

  /* Re-encrypt all stored credentials in light of NEW_PASSPHRASE. */
  /* ### TODO ### */

  /* Save credentials to disk. */
  return svn_config_write_auth_data(creds_hash,
                                    SVN_AUTH_CRED_MASTER_PASSPHRASE,
                                    AUTHN_FAUX_REALMSTRING, config_dir,
                                    scratch_pool);
}
