/*
 * auth.c:  routines that drive "authenticator" objects received from RA.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include "svn_client.h"
#include "svn_auth.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "client.h"

/* The keys that will be stored on disk */
#define SVN_CLIENT__AUTHFILE_ASCII_CERT_KEY            "ascii_cert"
#define SVN_CLIENT__AUTHFILE_FAILURES_KEY              "failures"

/*-----------------------------------------------------------------------*/


svn_error_t *
svn_client__dir_if_wc (const char **dir_p,
                       const char *dir,
                       apr_pool_t *pool)
{
  int wc_format;
  
  SVN_ERR (svn_wc_check_wc (dir, &wc_format, pool));
  
  if (wc_format == 0)
    *dir_p = NULL;
  else
    *dir_p = dir;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__default_auth_dir (const char **auth_dir_p,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, path, pool));

      /* Handle unversioned dir in a versioned parent. */
      if (! *auth_dir_p)
        goto try_parent;
    }
  else if ((kind == svn_node_file) || (kind == svn_node_none))
    {
    try_parent:
      svn_path_split (path, auth_dir_p, NULL, pool);
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, *auth_dir_p, pool));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         "unknown node kind for '%s'", path);
    }
  
  return SVN_NO_ERROR;
}




/** Providers. **/

/* Baton type for username/password prompting. */
typedef struct
{
  svn_auth_simple_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;

} simple_prompt_provider_baton_t;


/* Iteration baton type for username/password prompting. */
typedef struct
{
  /* The original provider baton */
  simple_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;

} simple_prompt_iter_baton_t;


/* Baton type for username-only prompting. */
typedef struct
{
  svn_auth_username_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;

} username_prompt_provider_baton_t;


/* Iteration baton type for username-only prompting. */
typedef struct
{
  /* The original provider baton */
  username_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;

} username_prompt_iter_baton_t;



/*** Helper Functions ***/

static svn_error_t *
prompt_for_simple_creds (svn_auth_cred_simple_t **cred_p,
                         simple_prompt_provider_baton_t *pb,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         svn_boolean_t first_time,
                         apr_pool_t *pool)
{
  const char *def_username = NULL, *def_password = NULL;

  *cred_p = NULL;

  /* If we're allowed to check for default usernames and passwords, do
     so. */
  if (first_time)
    {
      def_username = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                   APR_HASH_KEY_STRING);

      /* No default username?  Try the UID. */
      if (! def_username)
        {
          char *un;
          apr_uid_t uid;
          apr_gid_t gid;
         
          if (! apr_uid_current (&uid, &gid, pool)
              && ! apr_uid_name_get (&un, uid, pool))
            SVN_ERR (svn_utf_cstring_to_utf8 (&def_username, un, pool));
        }

      def_password = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                   APR_HASH_KEY_STRING);
    }    

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (def_username && def_password)
    {
      *cred_p = apr_palloc (pool, sizeof (**cred_p));
      (*cred_p)->username = apr_pstrdup (pool, def_username);
      (*cred_p)->password = apr_pstrdup (pool, def_password);
    }
  else
    {
      SVN_ERR (pb->prompt_func
               (cred_p, pb->prompt_baton, realmstring, def_username, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
prompt_for_username_creds (svn_auth_cred_username_t **cred_p,
                           username_prompt_provider_baton_t *pb,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           svn_boolean_t first_time,
                           apr_pool_t *pool)
{
  const char *def_username = NULL;

  *cred_p = NULL;

  /* If we're allowed to check for default usernames, do so. */
  if (first_time)
    {
      def_username = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_USERNAME,
                                   APR_HASH_KEY_STRING);

      /* No default username?  Try the UID. */
      if (! def_username)
        {
          char *un;
          apr_uid_t uid;
          apr_gid_t gid;
         
          if (! apr_uid_current (&uid, &gid, pool)
              && ! apr_uid_name_get (&un, uid, pool))
            SVN_ERR (svn_utf_cstring_to_utf8 (&def_username, un, pool));
        }
    }    

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (def_username)
    {
      *cred_p = apr_palloc (pool, sizeof (**cred_p));
      (*cred_p)->username = apr_pstrdup (pool, def_username);
    }
  else
    {
      SVN_ERR (pb->prompt_func (cred_p, pb->prompt_baton, realmstring, pool));
    }

  return SVN_NO_ERROR;
}



/*** Simple Prompt Provider ***/

/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds (void **credentials_p,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = provider_baton;
  simple_prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    pb, parameters, realmstring, TRUE, pool));

  ibaton->retries = 0;
  ibaton->pb = pb;
  ibaton->realmstring = apr_pstrdup (pool, realmstring);
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds (void **credentials_p,
                          void *iter_baton,
                          apr_hash_t *parameters,
                          apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (prompt_for_simple_creds ((svn_auth_cred_simple_t **) credentials_p,
                                    ib->pb, parameters, ib->realmstring, FALSE,
                                    pool));

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t simple_prompt_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_prompt_first_creds,
  simple_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_client_get_simple_prompt_provider
   (svn_auth_provider_object_t **provider,
    svn_auth_simple_prompt_func_t prompt_func,
    void *prompt_baton,
    int retry_limit,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  simple_prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &simple_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}



/*** Username Prompt Provider ***/

/* Our first attempt will use any default username passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
username_prompt_first_creds (void **credentials_p,
                             void **iter_baton,
                             void *provider_baton,
                             apr_hash_t *parameters,
                             const char *realmstring,
                             apr_pool_t *pool)
{
  username_prompt_provider_baton_t *pb = provider_baton;
  username_prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));

  SVN_ERR (prompt_for_username_creds
           ((svn_auth_cred_username_t **) credentials_p,
            pb, parameters, realmstring, TRUE, pool));

  ibaton->retries = 0;
  ibaton->pb = pb;
  ibaton->realmstring = apr_pstrdup (pool, realmstring);
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default username
   value, and simply re-prompt for the username, up to a maximum of
   ib->pb->retry_limit. */
static svn_error_t *
username_prompt_next_creds (void **credentials_p,
                            void *iter_baton,
                            apr_hash_t *parameters,
                            apr_pool_t *pool)
{
  username_prompt_iter_baton_t *ib = iter_baton;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (prompt_for_username_creds
           ((svn_auth_cred_username_t **) credentials_p,
            ib->pb, parameters, ib->realmstring, FALSE, pool));

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t username_prompt_provider = {
  SVN_AUTH_CRED_USERNAME,
  username_prompt_first_creds,
  username_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_client_get_username_prompt_provider
   (svn_auth_provider_object_t **provider,
    svn_auth_username_prompt_func_t prompt_func,
    void *prompt_baton,
    int retry_limit,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  username_prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &username_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}



/*** SSL file providers. ***/
typedef struct {
  /* cache:  realmstring which identifies the credentials file */
  const char *realmstring;
} ssl_server_file_provider_baton_t;

/* retieve ssl server CA failure overrides (if any) from servers
   config */
static svn_error_t *
server_ssl_file_first_credentials (void **credentials,
                                   void **iter_baton,
                                   void *provider_baton,
                                   apr_hash_t *parameters,
                                   const char *realmstring,
                                   apr_pool_t *pool)
{
  const char *temp_setting;
  ssl_server_file_provider_baton_t *pb = provider_baton;
  int failures = (int) apr_hash_get (parameters,
                                     SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                                     APR_HASH_KEY_STRING);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    apr_hash_get (parameters,
                  SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                  APR_HASH_KEY_STRING);
  svn_config_t *cfg = apr_hash_get (parameters,
                                    SVN_AUTH_PARAM_CONFIG,
                                    APR_HASH_KEY_STRING);
  const char *server_group = apr_hash_get (parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP,
                                           APR_HASH_KEY_STRING);
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *error = SVN_NO_ERROR;

  *credentials = NULL;
  *iter_baton = NULL;

  /* Make sure the save_creds function can get the realmstring */
  pb->realmstring = apr_pstrdup (pool, realmstring);

  /* Check for ignored cert dates */
  if (failures & (SVN_AUTH_SSL_NOTYETVALID | SVN_AUTH_SSL_EXPIRED))
    {
      temp_setting = svn_config_get_server_setting
        (cfg, server_group,
         SVN_CONFIG_OPTION_SSL_IGNORE_INVALID_DATE,
         "false");
      if (strcasecmp (temp_setting, "true") == 0)
        {
          failures &= ~(SVN_AUTH_SSL_NOTYETVALID | SVN_AUTH_SSL_EXPIRED);
        }
    }

  /* Check for overridden cert hostname */
  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
      temp_setting = svn_config_get_server_setting
        (cfg, server_group,
         SVN_CONFIG_OPTION_SSL_OVERRIDE_CERT_HSTNAME,
         NULL);
      if (temp_setting && strcasecmp (temp_setting, cert_info->hostname) == 0)
        {
          failures &= ~SVN_AUTH_SSL_CNMISMATCH;
        }
    }

  /* Check if this is a permanently accepted certificate */
  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);
  error = svn_config_read_auth_data (&creds_hash, SVN_AUTH_CRED_SERVER_SSL,
                                     pb->realmstring, config_dir, pool);
  svn_error_clear(error);
  if (!error && creds_hash)
    {
      svn_string_t *trusted_cert, *this_cert, *failstr;
      int last_failures;

      trusted_cert = apr_hash_get (creds_hash,
                                   SVN_CLIENT__AUTHFILE_ASCII_CERT_KEY,
                                   APR_HASH_KEY_STRING);
      this_cert = svn_string_create(cert_info->ascii_cert, pool);
      failstr = apr_hash_get (creds_hash,
                              SVN_CLIENT__AUTHFILE_FAILURES_KEY,
                              APR_HASH_KEY_STRING);

      last_failures = failstr ? atoi(failstr->data) : 0;

      /* If the cert is trusted and there are no new failures, we
       * accept it by clearing all failures. */
      if (trusted_cert &&
          svn_string_compare(this_cert, trusted_cert) &&
          (failures & ~last_failures) == 0)
        {
          failures = 0;
        }
    }

  /* Update the set of failures */
  apr_hash_set (parameters,
                SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                APR_HASH_KEY_STRING,
                (void*)failures);

  /* If all failures are cleared now, we return the creds */
  if (!failures)
    {
      svn_auth_cred_server_ssl_t *creds = apr_pcalloc (pool, sizeof(*creds));
      creds->trust_permanently = FALSE; /* No need to save it again... */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
server_ssl_file_save_credentials (svn_boolean_t *saved,
                                  void *credentials,
                                  void *provider_baton,
                                  apr_hash_t *parameters,
                                  apr_pool_t *pool)
{
  ssl_server_file_provider_baton_t *pb = provider_baton;
  svn_auth_cred_server_ssl_t *creds = credentials;
  const svn_auth_ssl_server_cert_info_t *cert_info;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;

  config_dir = apr_hash_get (parameters,
                             SVN_AUTH_PARAM_CONFIG_DIR,
                             APR_HASH_KEY_STRING);

  cert_info = apr_hash_get (parameters,
                            SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                            APR_HASH_KEY_STRING);

  creds_hash = apr_hash_make (pool);
  apr_hash_set (creds_hash,
                SVN_CLIENT__AUTHFILE_ASCII_CERT_KEY,
                APR_HASH_KEY_STRING,
                svn_string_create (cert_info->ascii_cert, pool));
  apr_hash_set (creds_hash,
                SVN_CLIENT__AUTHFILE_FAILURES_KEY,
                APR_HASH_KEY_STRING,
                svn_string_createf(pool, "%d", creds->accepted_failures));

  SVN_ERR (svn_config_write_auth_data (creds_hash,
                                       SVN_AUTH_CRED_SERVER_SSL,
                                       pb->realmstring,
                                       config_dir,
                                       pool));
  *saved = TRUE;
  return SVN_NO_ERROR;
}

/* retrieve and load the ssl client certificate file from servers
   config */
static svn_error_t *
client_ssl_cert_file_first_credentials (void **credentials_p,
                                        void **iter_baton,
                                        void *provider_baton,
                                        apr_hash_t *parameters,
                                        const char *realmstring,
                                        apr_pool_t *pool)
{
  svn_config_t *cfg = apr_hash_get (parameters, 
                                    SVN_AUTH_PARAM_CONFIG,
                                    APR_HASH_KEY_STRING);
  const char *server_group = apr_hash_get (parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP,
                                           APR_HASH_KEY_STRING);
  const char *cert_file;

  cert_file = svn_config_get_server_setting (cfg, server_group,
                                             SVN_CONFIG_OPTION_SSL_CLIENT_CERT_FILE,
                                             NULL);

  if (cert_file != NULL)
    {
      svn_auth_cred_client_ssl_t *cred = apr_palloc (pool, sizeof (*cred));
      
      cred->cert_file = cert_file;
      *credentials_p = cred;
    }
  else
    {
      *credentials_p = NULL;
    }

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

/* retrieve and load a password for a client certificate from servers file */
static svn_error_t *
client_ssl_pw_file_first_credentials (void **credentials_p,
                                      void **iter_baton,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool)
{
  svn_config_t *cfg = apr_hash_get (parameters,
                                    SVN_AUTH_PARAM_CONFIG,
                                    APR_HASH_KEY_STRING);
  const char *server_group = apr_hash_get (parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP,
                                           APR_HASH_KEY_STRING);

  const char *password =
      svn_config_get_server_setting (cfg, server_group,
                                     SVN_CONFIG_OPTION_SSL_CLIENT_CERT_PASSWORD,
                                     NULL);
  if (password)
    {
      svn_auth_cred_client_ssl_pass_t *cred
        = apr_palloc (pool, sizeof (*cred));
      cred->password = password;
      /* does nothing so far */
      *credentials_p = cred;
    }
  else *credentials_p = NULL;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static const svn_auth_provider_t server_ssl_file_provider = 
  {
    SVN_AUTH_CRED_SERVER_SSL,
    &server_ssl_file_first_credentials,
    NULL,
    &server_ssl_file_save_credentials,
  };

static const svn_auth_provider_t client_ssl_cert_file_provider =
  {
    SVN_AUTH_CRED_CLIENT_SSL,
    client_ssl_cert_file_first_credentials,
    NULL,
    NULL
  };

static const svn_auth_provider_t client_ssl_pw_file_provider =
  {
    SVN_AUTH_CRED_CLIENT_PASS_SSL,
    client_ssl_pw_file_first_credentials,
    NULL,
    NULL
  };



/*** Public API to SSL file providers. ***/

void 
svn_client_get_ssl_server_file_provider (svn_auth_provider_object_t **provider,
                                         apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  ssl_server_file_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  po->vtable = &server_ssl_file_provider;
  po->provider_baton = pb;
  *provider = po;
}

void 
svn_client_get_ssl_client_file_provider (svn_auth_provider_object_t **provider,
                                         apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  po->vtable = &client_ssl_cert_file_provider;
  *provider = po;
}

void
svn_client_get_ssl_pw_file_provider (svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  po->vtable = &client_ssl_pw_file_provider;
  *provider = po;
}



/*** SSL prompting providers. ***/

/* Baton type for prompting to verify server ssl creds. 
   There is no iteration baton type. */
typedef struct
{
  svn_auth_ssl_server_prompt_func_t prompt_func;
  void *prompt_baton;
} cred_server_ssl_provider_baton_t;


/* Baton type for prompting to send client ssl creds.
   There is no iteration baton type. */
typedef struct
{
  svn_auth_ssl_client_prompt_func_t prompt_func;
  void *prompt_baton;
} cred_client_ssl_provider_baton_t;


/* Baton type for client passphrase prompting.
   There is no iteration baton type. */
typedef struct
{
  svn_auth_ssl_pw_prompt_func_t prompt_func;
  void *prompt_baton;
} cred_pw_ssl_provider_baton_t;


static svn_error_t *
client_ssl_pw_prompt_first_cred (void **credentials_p,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 apr_pool_t *pool)
{
  cred_pw_ssl_provider_baton_t *pb = provider_baton;

  SVN_ERR (pb->prompt_func ((svn_auth_cred_client_ssl_pass_t **) credentials_p,
                            pb->prompt_baton, pool));

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
client_ssl_prompt_first_cred (void **credentials_p,
                              void **iter_baton,
                              void *provider_baton,
                              apr_hash_t *parameters,
                              const char *realmstring,
                              apr_pool_t *pool)
{
  cred_client_ssl_provider_baton_t *pb = provider_baton;

  SVN_ERR (pb->prompt_func ((svn_auth_cred_client_ssl_t **) credentials_p,
                            pb->prompt_baton, pool));

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
server_ssl_prompt_first_cred (void **credentials_p,
                              void **iter_baton,
                              void *provider_baton,
                              apr_hash_t *parameters,
                              const char *realmstring,
                              apr_pool_t *pool)
{
  cred_server_ssl_provider_baton_t *pb = provider_baton;
  int failures = (int) apr_hash_get (parameters,
                                     SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                                     APR_HASH_KEY_STRING);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    apr_hash_get (parameters,
                  SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                  APR_HASH_KEY_STRING);

  SVN_ERR (pb->prompt_func ((svn_auth_cred_server_ssl_t **) credentials_p,
                            pb->prompt_baton, failures, cert_info, pool));

  /* Store the potentially updated failures mask in the hash */
  apr_hash_set (parameters,
                SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                APR_HASH_KEY_STRING,
                (void*)failures);

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static const svn_auth_provider_t server_ssl_prompt_provider = 
  {
    SVN_AUTH_CRED_SERVER_SSL,
    server_ssl_prompt_first_cred,
    NULL,
    NULL  
  };

static const svn_auth_provider_t client_ssl_prompt_provider = 
  {
    SVN_AUTH_CRED_CLIENT_SSL,
    client_ssl_prompt_first_cred,
    NULL,
    NULL
  };

static const svn_auth_provider_t client_ssl_pass_prompt_provider =
  {
    SVN_AUTH_CRED_CLIENT_PASS_SSL,
    client_ssl_pw_prompt_first_cred,
    NULL,
    NULL
  };



/*** Public API to SSL prompting providers. ***/

void
svn_client_get_ssl_server_prompt_provider
   (svn_auth_provider_object_t **provider,
    svn_auth_ssl_server_prompt_func_t prompt_func,
    void *prompt_baton,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_server_ssl_provider_baton_t *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &server_ssl_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}

void
svn_client_get_ssl_client_prompt_provider
   (svn_auth_provider_object_t **provider,
    svn_auth_ssl_client_prompt_func_t prompt_func,
    void *prompt_baton,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_client_ssl_provider_baton_t *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &client_ssl_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}

void
svn_client_get_ssl_pw_prompt_provider
   (svn_auth_provider_object_t **provider,
    svn_auth_ssl_pw_prompt_func_t prompt_func,
    void *prompt_baton,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_pw_ssl_provider_baton_t *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &client_ssl_pass_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
