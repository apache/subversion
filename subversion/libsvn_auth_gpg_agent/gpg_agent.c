/*
 * gpg_agent.c: GPG Agent provider for SVN_AUTH_CRED_*
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

/* ==================================================================== */



/*** Includes. ***/

#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <apr_pools.h>
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_checksum.h"
#include "svn_string.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#define BUFFER_SIZE 1024

/* Implementation of svn_auth__password_get_t that retrieves the password
   from gpg-agent */
static svn_boolean_t
password_get_gpg_agent(const char **password,
                       apr_hash_t *creds,
                       const char *realmstring,
                       const char *username,
                       apr_hash_t *parameters,
                       svn_boolean_t non_interactive,
                       apr_pool_t *pool)
{
  int sd;
  char *gpg_agent_info = NULL;
  const char *p = NULL;
  char *ep = NULL;
  char *buffer;
  
  apr_array_header_t *socket_details;
  char *request = NULL;
  const char *cache_id = NULL;
  struct sockaddr_un addr;
  int recvd;
  const char *tty_name;
  const char *tty_type;
  const char *socket_name = NULL;
  svn_checksum_t *digest = NULL;

  gpg_agent_info = getenv("GPG_AGENT_INFO");
  if (gpg_agent_info != NULL)
    {
      socket_details = svn_cstring_split(gpg_agent_info, ":", TRUE,
                                         pool);
      socket_name = APR_ARRAY_IDX(socket_details, 0, const char *);
    }
  else
    return FALSE;

  if (socket_name != NULL)
    {
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, socket_name, sizeof(addr.sun_path) - 1);
      addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

      sd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (sd == -1)
        return FALSE;
    
      if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
          close(sd);
          return FALSE;
        }
    }
  else
    return FALSE;

  /* Receive the connection status from the gpg-agent daemon. */
  buffer = apr_palloc(pool, BUFFER_SIZE);
  recvd = recv(sd, buffer, BUFFER_SIZE - 1, 0);
  buffer[recvd] = '\0';

  if (strncmp(buffer, "OK", 2) != 0)
    return FALSE;

  /* Send TTY_NAME to the gpg-agent daemon. */
  tty_name = getenv("GPG_TTY");
  if (tty_name != NULL)
    {
      request = apr_psprintf(pool, "OPTION ttyname=%s\n", tty_name);
      send(sd, request, strlen(request), 0);
      recvd = recv(sd, buffer, BUFFER_SIZE - 1, 0);
      buffer[recvd] = '\0';

      if (strncmp(buffer, "OK", 2) != 0)
        return FALSE;
    }
  else
    return FALSE;

  /* Send TTY_TYPE to the gpg-agent daemon. */
  tty_type = getenv("TERM");
  if (tty_type != NULL)
    {
      request = apr_psprintf(pool, "OPTION ttytype=%s\n", tty_type);
      send(sd, request, strlen(request), 0);
      recvd = recv(sd, buffer, BUFFER_SIZE - 1, 0);
      buffer[recvd] = '\0';

      if (strncmp(buffer, "OK", 2) != 0)
        return FALSE;
    }
  else
    return FALSE;

  /* Create the CACHE_ID which will be generated based on REALMSTRING similar
     to other password caching mechanisms. */
  digest = svn_checksum_create(svn_checksum_md5, pool);
  svn_checksum(&digest, svn_checksum_md5, realmstring, strlen(realmstring),
               pool);
  cache_id = svn_checksum_to_cstring(digest, pool);

  if (non_interactive)
    request = apr_psprintf(pool,
                           "GET_PASSPHRASE --data --no-ask %s X Password: \n",
                           cache_id);
  else
    request = apr_psprintf(pool,
                           "GET_PASSPHRASE --data %s X Password: \n",
                           cache_id);

  send(sd, request, strlen(request) + 1, 0);
  recvd = recv(sd, buffer, BUFFER_SIZE - 1, 0);
  buffer[recvd] = '\0';

  if (strncmp(buffer, "ERR", 3) == 0)
    return FALSE;
  
  if (strncmp(buffer, "D", 1) == 0)
    p = &buffer[2];

  ep = strchr(p, '\n');
  if (ep != NULL)
    *ep = '\0';

  *password = p;

  close(sd);
  return TRUE;
}


/* Implementation of svn_auth__password_set_t that stores the password in
   GPG Agent. */
static svn_boolean_t
password_set_gpg_agent(apr_hash_t *creds,
                       const char *realmstring,
                       const char *username,
                       const char *password,
                       apr_hash_t *parameters,
                       svn_boolean_t non_interactive,
                       apr_pool_t *pool)
{
  return TRUE;
}


static svn_error_t *
simple_gpg_agent_first_creds(void **credentials,
                             void **iter_baton,
                             void *provider_baton,
                             apr_hash_t *parameters,
                             const char *realmstring,
                             apr_pool_t *pool)
{
  return svn_auth__simple_first_creds_helper
           (credentials,
            iter_baton, provider_baton,
            parameters, realmstring,
            password_get_gpg_agent,
            SVN_AUTH__GPG_AGENT_PASSWORD_TYPE,
            pool);
}


/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
simple_gpg_agent_save_creds(svn_boolean_t *saved,
                            void *credentials,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  return svn_auth__simple_save_creds_helper
           (saved, credentials,
            provider_baton, parameters,
            realmstring,
            password_set_gpg_agent,
            SVN_AUTH__GPG_AGENT_PASSWORD_TYPE,
            pool);
}


static const svn_auth_provider_t gpg_agent_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_gpg_agent_first_creds,
  NULL,
  simple_gpg_agent_save_creds
};


/* Public API */
void
svn_auth_get_gpg_agent_simple_provider
  (svn_auth_provider_object_t **provider,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gpg_agent_simple_provider;
  *provider = po;
}
