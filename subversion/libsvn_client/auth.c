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
         "unknown node kind for `%s'", path);
    }
  
  return SVN_NO_ERROR;
}



typedef struct
{
  /* a callback function/baton that prompts the user */
  svn_client_prompt_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;

} prompt_provider_baton_t;


typedef struct
{
  /* The original provider baton */
  prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;

} prompt_iter_baton_t;



/*** Helper Functions ***/

static svn_error_t *
get_creds (const char **username,
           const char **password,
           svn_boolean_t *got_creds,
           prompt_provider_baton_t *pb,
           apr_hash_t *parameters,
           const char *realmstring,
           svn_boolean_t first_time,
           apr_pool_t *pool)
{
  const char *prompt_username = NULL, *prompt_password = NULL;
  const char *def_username = NULL, *def_password = NULL;
  svn_boolean_t displayed_realm = FALSE;
  const char *promptstr = apr_psprintf (pool,
                                        "Authentication realm: %s\n",
                                        realmstring);  

  /* Setup default return values. */
  *got_creds = FALSE;
  if (username)
    *username = NULL;
  if (password)
    *password = NULL;

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
          apr_status_t status;
          
          if ((status = apr_uid_current (&uid, &gid, pool)))
            return svn_error_create (status, NULL, "Error getting UID");
          if ((status = apr_uid_name_get (&un, uid, pool)))
            return svn_error_create (status, NULL, "Error getting username");
          SVN_ERR (svn_utf_cstring_to_utf8 (&def_username, un, NULL, pool));
        }

      def_password = apr_hash_get (parameters, 
                                   SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                                   APR_HASH_KEY_STRING);
    }    

  /* Get the username. */
  if (def_username)
    {
      prompt_username = def_username;
    }
  else if (username)
    {
      SVN_ERR (pb->prompt_func (&prompt_username,
                                apr_pstrcat (pool,
                                             promptstr, "username: ", NULL),
                                FALSE, /* screen echo ok */
                                pb->prompt_baton, pool));
      displayed_realm = TRUE;
    }

  /* If we have no username, we can go no further. */
  if (! prompt_username)
    return SVN_NO_ERROR;

  /* Get the password. */
  if (def_password)
    {
      prompt_password = def_password;
    }
  else if (password)
    {
      const char *prompt = apr_psprintf (pool,
                                         "%s's password: ", prompt_username);
      
      if (! displayed_realm)
        prompt = apr_pstrcat (pool, promptstr, prompt, NULL);
                                      
      SVN_ERR (pb->prompt_func (&prompt_password, prompt,
                                TRUE, /* don't echo to screen */
                                pb->prompt_baton, pool));
    }

  if (username)
    *username = prompt_username;
  if (password)
    *password = prompt_password;
  *got_creds = TRUE;
  return SVN_NO_ERROR;
}



/*** Simple Prompt Provider ***/

/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds (void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = provider_baton;
  prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));
  const char *username, *password;
  svn_boolean_t got_creds;

  SVN_ERR (get_creds (&username, &password, &got_creds, pb,
                      parameters, realmstring, TRUE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      creds->password = password;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

  ibaton->retries = 0;
  ibaton->pb = pb;
  ibaton->realmstring = apr_pstrdup (pool, realmstring);
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds (void **credentials,
                          void *iter_baton,
                          apr_hash_t *parameters,
                          apr_pool_t *pool)
{
  prompt_iter_baton_t *ib = iter_baton;
  const char *username, *password;
  svn_boolean_t got_creds;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (get_creds (&username, &password, &got_creds, ib->pb,
                      parameters, ib->realmstring, FALSE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      creds->password = password;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

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
svn_client_get_simple_prompt_provider (svn_auth_provider_object_t **provider,
                                       svn_client_prompt_t prompt_func,
                                       void *prompt_baton,
                                       int retry_limit,
                                       apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

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
username_prompt_first_creds (void **credentials,
                             void **iter_baton,
                             void *provider_baton,
                             apr_hash_t *parameters,
                             const char *realmstring,
                             apr_pool_t *pool)
{
  prompt_provider_baton_t *pb = provider_baton;
  prompt_iter_baton_t *ibaton = apr_pcalloc (pool, sizeof (*ibaton));
  const char *username;
  svn_boolean_t got_creds;

  SVN_ERR (get_creds (&username, NULL, &got_creds, pb,
                      parameters, realmstring, TRUE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

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
username_prompt_next_creds (void **credentials,
                            void *iter_baton,
                            apr_hash_t *parameters,
                            apr_pool_t *pool)
{
  prompt_iter_baton_t *ib = iter_baton;
  const char *username;
  svn_boolean_t got_creds;

  if (ib->retries >= ib->pb->retry_limit)
    {
      /* give up, go on to next provider. */
      *credentials = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  SVN_ERR (get_creds (&username, NULL, &got_creds, ib->pb,
                      parameters, ib->realmstring, FALSE, pool));
  if (got_creds)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc (pool, sizeof (*creds));
      creds->username = username;
      *credentials = creds;
    }
  else
    {
      *credentials = NULL;
    }

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
svn_client_get_username_prompt_provider (svn_auth_provider_object_t **provider,
                                         svn_client_prompt_t prompt_func,
                                         void *prompt_baton,
                                         int retry_limit,
                                         apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  prompt_provider_baton_t *pb = apr_pcalloc (pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &username_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}



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
  int failures_in = (int) apr_hash_get (parameters,
                                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES_IN,
                                        APR_HASH_KEY_STRING);
  svn_config_t *cfg = apr_hash_get (parameters,
                                    SVN_AUTH_PARAM_CONFIG,
                                    APR_HASH_KEY_STRING);
  const char *server_group = apr_hash_get (parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP,
                                           APR_HASH_KEY_STRING);
  svn_auth_cred_server_ssl_t *cred;
  int failures_allow = 0;

  temp_setting = svn_config_get_server_setting (cfg, server_group, 
                                                SVN_CONFIG_OPTION_SSL_IGNORE_UNKNOWN_CA,
                                                "false");
  if (strcasecmp (temp_setting, "true") == 0)
    {
      failures_allow |= SVN_AUTH_SSL_UNKNOWNCA;
    }

  temp_setting = svn_config_get_server_setting (cfg, server_group, 
                                                SVN_COFNIG_OPTION_SSL_IGNORE_HOST_MISMATCH,
                                                "false");
  if (strcasecmp (temp_setting, "true") == 0)
    {
      failures_allow |= SVN_AUTH_SSL_CNMISMATCH;
    }

  temp_setting = svn_config_get_server_setting (cfg, server_group, 
                                                SVN_CONFIG_OPTION_SSL_IGNORE_INVALID_DATE,
                                                "false");
  if (strcasecmp (temp_setting, "true") == 0)
    {
      failures_allow |= SVN_AUTH_SSL_NOTYETVALID | SVN_AUTH_SSL_EXPIRED;
    }

  /* don't return creds unless we consider the certificate completely
   * acceptable */
  if ( (failures_in & ~failures_allow) == 0)
    {
      cred = apr_palloc (pool, sizeof(svn_auth_cred_server_ssl_t));
      *credentials = cred;
      cred->failures_allow = failures_allow;
    }
  else
    {
      *credentials = NULL;
    }
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

/* retrieve and load the ssl client certificate file from servers
   config */
static svn_error_t *
client_ssl_cert_file_first_credentials (void **credentials,
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
  const char *cert_file, *key_file, *cert_type;

  cert_file = svn_config_get_server_setting (cfg, server_group,
                                             SVN_CONFIG_OPTION_SSL_CLIENT_CERT_FILE,
                                             NULL);

  if (cert_file != NULL)
    {
      svn_auth_cred_client_ssl_t *cred =
        apr_palloc (pool, sizeof(svn_auth_cred_client_ssl_t));
      
      key_file = svn_config_get_server_setting (cfg, server_group,
                                                SVN_CONFIG_OPTION_SSL_CLIENT_KEY_FILE,
                                                NULL);
      cert_type = svn_config_get_server_setting (cfg, server_group,
                                                 SVN_CONFIG_OPTION_SSL_CLIENT_CERT_TYPE,
                                                 "pem");
      cred->cert_file = cert_file;
      cred->key_file = key_file;
      if ((strcmp (cert_type, "pem") == 0) ||
          (strcmp (cert_type, "PEM") == 0))
        {
          cred->cert_type = svn_auth_ssl_pem_cert_type;
        }
      else if ((strcmp (cert_type, "pkcs12") == 0) ||
               (strcmp (cert_type, "PKCS12") == 0))
        {
          cred->cert_type = svn_auth_ssl_pkcs12_cert_type;
        }
      else
        {
          cred->cert_type = svn_auth_ssl_unknown_cert_type;
        }
      *credentials = cred;
    }
  else
    {
      *credentials = NULL;
    }

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

/* retrieve and load a password for a client certificate from servers file */
static svn_error_t *
client_ssl_pw_file_first_credentials (void **credentials,
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
      svn_auth_cred_client_ssl_pass_t *cred =
        apr_palloc (pool, sizeof(svn_auth_cred_client_ssl_pass_t));
      cred->password = password;
      /* does nothing so far */
      *credentials = cred;
    }
  else *credentials = NULL;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static const svn_auth_provider_t server_ssl_file_provider = 
  {
    SVN_AUTH_CRED_SERVER_SSL,
    &server_ssl_file_first_credentials,
    NULL,
    NULL
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


void 
svn_client_get_ssl_server_file_provider (svn_auth_provider_object_t **provider,
                                         apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  po->vtable = &server_ssl_file_provider;
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



typedef struct
{
  /* a callback function/baton that prompts the user */
  svn_client_prompt_t prompt_func;
  void *prompt_baton;

} cred_ssl_provider_baton;

static svn_error_t *
client_ssl_pw_prompt_first_cred (void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 apr_pool_t *pool)
{
  cred_ssl_provider_baton *pb = provider_baton;
  const char *info;
  SVN_ERR(pb->prompt_func (&info, "client certificate passphrase: ", TRUE,
                           pb->prompt_baton, pool));
  if (info && info[0])
    {
      svn_auth_cred_client_ssl_pass_t *cred = apr_palloc (pool, sizeof(*cred));
      cred->password = info;
      *credentials = cred;
    }
  else
    {
      *credentials = NULL;
    }
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
client_ssl_prompt_first_cred (void **credentials,
                              void **iter_baton,
                              void *provider_baton,
                              apr_hash_t *parameters,
                              const char *realmstring,
                              apr_pool_t *pool)
{
  cred_ssl_provider_baton *pb = provider_baton;
  const char *cert_file = NULL, *key_file = NULL;
  size_t cert_file_len;
  const char *extension;
  svn_auth_cred_client_ssl_t *cred;

  svn_auth_ssl_cert_type_t cert_type;
  SVN_ERR (pb->prompt_func (&cert_file, "client certificate filename: ", FALSE,
                            pb->prompt_baton, pool));
  
  if ((cert_file == NULL) || (cert_file[0] == 0))
    {
      return NULL;
    }

  cert_file_len = strlen(cert_file);
  extension = cert_file + cert_file_len - 4;
  if ((strcmp (extension, ".p12") == 0) || 
      (strcmp (extension, ".P12") == 0))
    {
      cert_type = svn_auth_ssl_pkcs12_cert_type;
    }
  else if ((strcmp (extension, ".pem") == 0) || 
           (strcmp (extension, ".PEM") == 0))
    {
      cert_type = svn_auth_ssl_pem_cert_type;
    }
  else
    {
      const char *type;
      SVN_ERR(pb->prompt_func (&type, "cert type ('pem' or 'pkcs12'): ",
                               FALSE, pb->prompt_baton, pool));
      if ((strcmp(type, "pkcs12") == 0) ||
          (strcmp(type, "PKCS12") == 0))
        {
          cert_type = svn_auth_ssl_pkcs12_cert_type;
        }
      else if ((strcmp (type, "pem") == 0) || 
               (strcmp (type, "PEM") == 0))
        {
          cert_type = svn_auth_ssl_pem_cert_type;
        }
      else
        {
          return svn_error_createf (SVN_ERR_INCORRECT_PARAMS, NULL,
                                    "unknown ssl certificate type '%s'", type);
        }
    }
  
  if (cert_type == svn_auth_ssl_pem_cert_type)
    {
      SVN_ERR(pb->prompt_func (&key_file, "optional key file: ",
                               FALSE, pb->prompt_baton, pool));
    }
  if (key_file && key_file[0] == 0)
    {
      key_file = 0;
    }
  cred = apr_palloc (pool, sizeof(*cred));
  cred->cert_file = cert_file;
  cred->key_file = key_file;
  cred->cert_type = cert_type;
  *credentials = cred;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
server_ssl_prompt_first_cred (void **credentials,
                              void **iter_baton,
                              void *provider_baton,
                              apr_hash_t *parameters,
                              const char *realmstring,
                              apr_pool_t *pool)
{
  cred_ssl_provider_baton *pb = provider_baton;
  svn_boolean_t previous_output = FALSE;
  svn_auth_cred_server_ssl_t *cred;
  int failure;
  const char *choice;
  int failures_in =
    (int) apr_hash_get (parameters,
                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES_IN,
                        APR_HASH_KEY_STRING);

  svn_stringbuf_t *buf = svn_stringbuf_create
    ("Error validating server certificate: ", pool);

  failure = failures_in & SVN_AUTH_SSL_UNKNOWNCA;
  if (failure)
    {
      svn_stringbuf_appendcstr (buf, "Unknown certificate issuer");
      previous_output = TRUE;
    }

  failure = failures_in & SVN_AUTH_SSL_CNMISMATCH;
  if (failure)
    {
      if (previous_output)
        {
          svn_stringbuf_appendcstr (buf, ", ");
        }
      svn_stringbuf_appendcstr (buf, "Hostname mismatch");
      previous_output = TRUE;
    } 
  failure = failures_in & (SVN_AUTH_SSL_EXPIRED | SVN_AUTH_SSL_NOTYETVALID);
  if (failure)
    {
      if (previous_output)
        {
          svn_stringbuf_appendcstr (buf, ", ");
        }
      svn_stringbuf_appendcstr (buf, "Certificate expired or not yet valid");
      previous_output = TRUE;
    }

  svn_stringbuf_appendcstr (buf, ". Accept? (y/N): ");
  SVN_ERR(pb->prompt_func (&choice, buf->data, FALSE,
                           pb->prompt_baton, pool));
  
  if (choice && (choice[0] == 'y' || choice[0] == 'Y'))
    {
      cred = apr_palloc (pool, sizeof(*cred));
      cred->failures_allow = failures_in;
      *credentials = cred;
    }
  else
    {
      *credentials = NULL;
    }
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

void
svn_client_get_ssl_server_prompt_provider (svn_auth_provider_object_t **provider,
                                           svn_client_prompt_t prompt_func,
                                           void *prompt_baton,
                                           apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_ssl_provider_baton *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &server_ssl_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}

void
svn_client_get_ssl_client_prompt_provider (svn_auth_provider_object_t **provider,
                                           svn_client_prompt_t prompt_func,
                                           void *prompt_baton,
                                           apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_ssl_provider_baton *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &client_ssl_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}

void
svn_client_get_ssl_pw_prompt_provider (svn_auth_provider_object_t **provider,
                                       svn_client_prompt_t prompt_func,
                                       void *prompt_baton,
                                       apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc (pool, sizeof(*po));
  cred_ssl_provider_baton *pb = apr_palloc (pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &client_ssl_pass_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
