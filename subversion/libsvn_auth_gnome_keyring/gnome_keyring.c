/*
 * gnome_keyring.c: GNOME Keyring provider for SVN_AUTH_CRED_*
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#include "svn_auth.h"
#include "svn_error.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#include <glib.h>
#include <dbus/dbus.h>
#include <gnome-keyring.h>


/*-----------------------------------------------------------------------*/
/* GNOME Keyring simple provider, puts passwords in GNOME Keyring        */
/*-----------------------------------------------------------------------*/

/* Implementation of password_get_t that retrieves the password
   from GNOME Keyring. */
static svn_boolean_t
gnome_keyring_password_get(const char **password,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  if (non_interactive)
    {
      return FALSE;
    }

  if (! dbus_bus_get(DBUS_BUS_SESSION, NULL))
    {
      return FALSE;
    }

  if (! gnome_keyring_is_available())
    {
      return FALSE;
    }

  GnomeKeyringResult result;
  GList *items;
  svn_boolean_t ret = FALSE;

  result = gnome_keyring_find_network_password_sync(username, realmstring,
                                                    NULL, NULL, NULL, NULL, 0,
                                                    &items);

  if (result == GNOME_KEYRING_RESULT_OK && items && items->data)
    {
      GnomeKeyringNetworkPasswordData *item;
      item = (GnomeKeyringNetworkPasswordData *)items->data;
      if (item->password)
        {
          size_t len = strlen(item->password);
          if (len > 0)
            {
              *password = apr_pstrmemdup(pool, item->password, len);
              ret = TRUE;
            }
        }
      gnome_keyring_network_password_list_free(items);
    }

  return ret;
}

/* Implementation of password_set_t that stores the password in
   GNOME Keyring. */
static svn_boolean_t
gnome_keyring_password_set(apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *password,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  if (non_interactive)
    {
      return FALSE;
    }

  if (! dbus_bus_get(DBUS_BUS_SESSION, NULL))
    {
      return FALSE;
    }

  if (! gnome_keyring_is_available())
    {
      return FALSE;
    }

  GnomeKeyringResult result;
  guint32 item_id;

  result = gnome_keyring_set_network_password_sync(NULL, /* default keyring */
                                                   username, realmstring,
                                                   NULL, NULL, NULL, NULL, 0,
                                                   password,
                                                   &item_id);

  return result == GNOME_KEYRING_RESULT_OK;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
gnome_keyring_simple_first_creds(void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 apr_pool_t *pool)
{
  return svn_auth__simple_first_creds_helper(credentials,
                                             iter_baton, provider_baton,
                                             parameters, realmstring,
                                             gnome_keyring_password_get,
                                             SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
                                             pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
gnome_keyring_simple_save_creds(svn_boolean_t *saved,
                                void *credentials,
                                void *provider_baton,
                                apr_hash_t *parameters,
                                const char *realmstring,
                                apr_pool_t *pool)
{
  return svn_auth__simple_save_creds_helper(saved, credentials,
                                            provider_baton, parameters,
                                            realmstring,
                                            gnome_keyring_password_set,
                                            SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
                                            pool);
}

static void
gnome_keyring_init()
{
  const char *application_name = NULL;
  application_name = g_get_application_name();
  if (!application_name)
    g_set_application_name("Subversion");
}

static const svn_auth_provider_t gnome_keyring_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  gnome_keyring_simple_first_creds,
  NULL,
  gnome_keyring_simple_save_creds
};

/* Public API */
void
svn_auth_get_gnome_keyring_simple_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gnome_keyring_simple_provider;
  *provider = po;

  gnome_keyring_init();
}


/*-----------------------------------------------------------------------*/
/* GNOME Keyring SSL client certificate passphrase provider,             */
/* puts passphrases in GNOME Keyring                                     */
/*-----------------------------------------------------------------------*/

/* Get cached encrypted credentials from the ssl client cert password
   provider's cache. */
static svn_error_t *
gnome_keyring_ssl_client_cert_pw_first_creds(void **credentials,
                                             void **iter_baton,
                                             void *provider_baton,
                                             apr_hash_t *parameters,
                                             const char *realmstring,
                                             apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_file_first_creds_helper
           (credentials,
            iter_baton, provider_baton,
            parameters, realmstring,
            gnome_keyring_password_get,
            SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
            pool);
}

/* Save encrypted credentials to the ssl client cert password provider's
   cache. */
static svn_error_t *
gnome_keyring_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
                                            void *credentials,
                                            void *provider_baton,
                                            apr_hash_t *parameters,
                                            const char *realmstring,
                                            apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_file_save_creds_helper
           (saved, credentials,
            provider_baton, parameters,
            realmstring,
            gnome_keyring_password_set,
            SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
            pool);
}

static const svn_auth_provider_t gnome_keyring_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  gnome_keyring_ssl_client_cert_pw_first_creds,
  NULL,
  gnome_keyring_ssl_client_cert_pw_save_creds
};

/* Public API */
void
svn_auth_get_gnome_keyring_ssl_client_cert_pw_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gnome_keyring_ssl_client_cert_pw_provider;
  *provider = po;
}
