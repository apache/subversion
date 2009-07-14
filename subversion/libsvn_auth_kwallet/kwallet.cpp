/*
 * kwallet.cpp: KWallet provider for SVN_AUTH_CRED_*
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <apr_pools.h>
#include <apr_strings.h>
#include "svn_auth.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_version.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#include <dbus/dbus.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtGui/QApplication>
#include <QtGui/QX11Info>

#include <kaboutdata.h>
#include <kcmdlineargs.h>
#include <kcomponentdata.h>
#include <klocalizedstring.h>
#include <kwallet.h>
#include <kwindowsystem.h>
#include <netwm.h>
#include <netwm_def.h>


/*-----------------------------------------------------------------------*/
/* KWallet simple provider, puts passwords in KWallet                    */
/*-----------------------------------------------------------------------*/


#define INITIALIZE_APPLICATION                                            \
  if (apr_hash_get(parameters,                                            \
                   "svn:auth:qapplication-safe",                          \
                   APR_HASH_KEY_STRING))                                  \
    {                                                                     \
      QApplication *app;                                                  \
      if (! qApp)                                                         \
        {                                                                 \
          int argc = 1;                                                   \
          app = new QApplication(argc, (char *[1]) {(char *) "svn"});     \
        }                                                                 \
    }                                                                     \
  else                                                                    \
    {                                                                     \
      QCoreApplication *app;                                              \
      if (! qApp)                                                         \
        {                                                                 \
          int argc = 1;                                                   \
          app = new QCoreApplication(argc, (char *[1]) {(char *) "svn"}); \
        }                                                                 \
    }

static const char *
get_application_name(apr_hash_t *parameters,
                     apr_pool_t *pool)
{
  svn_config_t *config =
    static_cast<svn_config_t *> (apr_hash_get(parameters,
                                              SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG,
                                              APR_HASH_KEY_STRING));
  svn_boolean_t svn_application_name_with_pid;
  svn_config_get_bool(config,
                      &svn_application_name_with_pid,
                      SVN_CONFIG_SECTION_AUTH,
                      SVN_CONFIG_OPTION_KWALLET_SVN_APPLICATION_NAME_WITH_PID,
                      FALSE);
  const char *svn_application_name;
  if (svn_application_name_with_pid)
    {
      svn_application_name = apr_psprintf(pool, "Subversion [%ld]", long(getpid()));
    }
  else
    {
      svn_application_name = "Subversion";
    }
  return svn_application_name;
}

static QString
get_wallet_name(apr_hash_t *parameters)
{
  svn_config_t *config =
    static_cast<svn_config_t *> (apr_hash_get(parameters,
                                              SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG,
                                              APR_HASH_KEY_STRING));
  const char *wallet_name;
  svn_config_get(config,
                 &wallet_name,
                 SVN_CONFIG_SECTION_AUTH,
                 SVN_CONFIG_OPTION_KWALLET_WALLET,
                 "");
  if (strcmp(wallet_name, "") == 0)
    {
      return KWallet::Wallet::NetworkWallet();
    }
  else
    {
      return QString::fromUtf8(wallet_name);
    }
}

static pid_t
get_parent_pid(pid_t pid,
               apr_pool_t *pool)
{
  pid_t parent_pid = 0;

#ifdef __linux__
  svn_stream_t *stat_file_stream;
  svn_string_t *stat_file_string;
  const char *preceeding_space, *following_space, *parent_pid_string;

  const char *path = apr_psprintf(pool, "/proc/%ld/stat", long(pid));
  svn_error_t *err = svn_stream_open_readonly(&stat_file_stream, path, pool, pool);
  if (err == SVN_NO_ERROR)
    {
      err = svn_string_from_stream(&stat_file_string, stat_file_stream, pool, pool);
      if (err == SVN_NO_ERROR)
        {
          if ((preceeding_space = strchr(stat_file_string->data, ' ')))
            {
              if ((preceeding_space = strchr(preceeding_space + 1, ' ')))
                {
                  if ((preceeding_space = strchr(preceeding_space + 1, ' ')))
                    {
                      if ((following_space = strchr(preceeding_space + 1, ' ')))
                        {
                          parent_pid_string = apr_pstrndup(pool,
                                                           preceeding_space + 1,
                                                           following_space - preceeding_space);
                          parent_pid = atol(parent_pid_string);
                        }
                    }
                }
            }
        }
    }

  if (err)
    {
      svn_error_clear(err);
    }
#endif

  return parent_pid;
}

static WId
get_wid(apr_hash_t *parameters,
        apr_pool_t *pool)
{
  WId wid = 1;

  if (apr_hash_get(parameters,
                   "svn:auth:qapplication-safe",
                   APR_HASH_KEY_STRING))
    {
      QMap<pid_t, WId> process_info_list;
      QList<WId> windows(KWindowSystem::windows());
      QList<WId>::const_iterator i;
      for (i = windows.begin(); i != windows.end(); i++)
        {
          process_info_list[NETWinInfo(QX11Info::display(),
                                       *i,
                                       QX11Info::appRootWindow(),
                                       NET::WMPid).pid()] = *i;
        }

      apr_pool_t *iterpool = svn_pool_create(pool);
      pid_t pid = getpid();
      while (pid != 0)
        {
          svn_pool_clear(iterpool);
          if (process_info_list.contains(pid))
            {
              wid = process_info_list[pid];
              break;
            }
          pid = get_parent_pid(pid, iterpool);
        }
      svn_pool_destroy(iterpool);
    }

  if (wid == 1)
    {
      const char *wid_env_string = getenv("WINDOWID");
      if (wid_env_string)
        {
          long wid_env = atol(wid_env_string);
          if (wid_env != 0)
            {
              wid = wid_env;
            }
        }
    }

  return wid;
}

static KWallet::Wallet *
get_wallet(QString wallet_name,
           apr_hash_t *parameters,
           apr_pool_t *pool)
{
  KWallet::Wallet *wallet =
    static_cast<KWallet::Wallet *> (apr_hash_get(parameters,
                                                 "kwallet-wallet",
                                                 APR_HASH_KEY_STRING));
  if (! wallet && ! apr_hash_get(parameters,
                                 "kwallet-opening-failed",
                                 APR_HASH_KEY_STRING))
    {
      wallet = KWallet::Wallet::openWallet(wallet_name,
                                           pool ? get_wid(parameters, pool) : 1,
                                           KWallet::Wallet::Synchronous);
    }
  if (wallet)
    {
      apr_hash_set(parameters,
                   "kwallet-wallet",
                   APR_HASH_KEY_STRING,
                   wallet);
    }
  else
    {
      apr_hash_set(parameters,
                   "kwallet-opening-failed",
                   APR_HASH_KEY_STRING,
                   "");
    }
  return wallet;
}

static apr_status_t
kwallet_terminate(void *data)
{
  apr_hash_t *parameters = static_cast<apr_hash_t *> (data);
  if (apr_hash_get(parameters, "kwallet-initialized", APR_HASH_KEY_STRING))
    {
      KWallet::Wallet *wallet = get_wallet(NULL, parameters, NULL);
      delete wallet;
      apr_hash_set(parameters,
                   "kwallet-initialized",
                   APR_HASH_KEY_STRING,
                   NULL);
    }
  return APR_SUCCESS;
}

/* Implementation of svn_auth__password_get_t that retrieves
   the password from KWallet. */
static svn_boolean_t
kwallet_password_get(const char **password,
                     apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     apr_hash_t *parameters,
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

  INITIALIZE_APPLICATION

  KCmdLineArgs::init(1,
                     (char *[1]) {(char *) "svn"},
                     get_application_name(parameters, pool),
                     "subversion",
                     ki18n(get_application_name(parameters, pool)),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KComponentData component_data(KCmdLineArgs::aboutData());
  svn_boolean_t ret = FALSE;
  QString wallet_name = get_wallet_name(parameters);
  QString folder = QString::fromUtf8("Subversion");
  QString key =
    QString::fromUtf8(username) + "@" + QString::fromUtf8(realmstring);
  if (! KWallet::Wallet::keyDoesNotExist(wallet_name, folder, key))
    {
      KWallet::Wallet *wallet = get_wallet(wallet_name, parameters, pool);
      if (wallet)
        {
          apr_hash_set(parameters,
                       "kwallet-initialized",
                       APR_HASH_KEY_STRING,
                       "");
          if (wallet->setFolder(folder))
            {
              QString q_password;
              if (wallet->readPassword(key, q_password) == 0)
                {
                  *password = apr_pstrmemdup(pool,
                                             q_password.toUtf8().data(),
                                             q_password.size());
                  ret = TRUE;
                }
            }
        }
    }

  apr_pool_cleanup_register(pool, parameters, kwallet_terminate, NULL);

  return ret;
}

/* Implementation of svn_auth__password_set_t that stores
   the password in KWallet. */
static svn_boolean_t
kwallet_password_set(apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     const char *password,
                     apr_hash_t *parameters,
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

  INITIALIZE_APPLICATION

  KCmdLineArgs::init(1,
                     (char *[1]) {(char *) "svn"},
                     get_application_name(parameters, pool),
                     "subversion",
                     ki18n(get_application_name(parameters, pool)),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KComponentData component_data(KCmdLineArgs::aboutData());
  svn_boolean_t ret = FALSE;
  QString q_password = QString::fromUtf8(password);
  QString wallet_name = get_wallet_name(parameters);
  QString folder = QString::fromUtf8("Subversion");
  KWallet::Wallet *wallet = get_wallet(wallet_name, parameters, pool);
  if (wallet)
    {
      apr_hash_set(parameters,
                   "kwallet-initialized",
                   APR_HASH_KEY_STRING,
                   "");
      if (! wallet->hasFolder(folder))
        {
          wallet->createFolder(folder);
        }
      if (wallet->setFolder(folder))
        {
          QString key = QString::fromUtf8(username) + "@"
            + QString::fromUtf8(realmstring);
          if (wallet->writePassword(key, q_password) == 0)
            {
              ret = TRUE;
            }
        }
    }

  apr_pool_cleanup_register(pool, parameters, kwallet_terminate, NULL);

  return ret;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
kwallet_simple_first_creds(void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  return svn_auth__simple_first_creds_helper(credentials,
                                             iter_baton,
                                             provider_baton,
                                             parameters,
                                             realmstring,
                                             kwallet_password_get,
                                             SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                             pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
kwallet_simple_save_creds(svn_boolean_t *saved,
                          void *credentials,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  return svn_auth__simple_save_creds_helper(saved, credentials,
                                            provider_baton,
                                            parameters,
                                            realmstring,
                                            kwallet_password_set,
                                            SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                            pool);
}

static const svn_auth_provider_t kwallet_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  kwallet_simple_first_creds,
  NULL,
  kwallet_simple_save_creds
};

/* Public API */
extern "C" {
void
svn_auth_get_kwallet_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po =
    static_cast<svn_auth_provider_object_t *> (apr_pcalloc(pool, sizeof(*po)));

  po->vtable = &kwallet_simple_provider;
  *provider = po;
}
}


/*-----------------------------------------------------------------------*/
/* KWallet SSL client certificate passphrase provider,                   */
/* puts passphrases in KWallet                                           */
/*-----------------------------------------------------------------------*/

/* Get cached encrypted credentials from the ssl client cert password
   provider's cache. */
static svn_error_t *
kwallet_ssl_client_cert_pw_first_creds(void **credentials,
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
            kwallet_password_get,
            SVN_AUTH__KWALLET_PASSWORD_TYPE,
            pool);
}

/* Save encrypted credentials to the ssl client cert password provider's
   cache. */
static svn_error_t *
kwallet_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
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
            kwallet_password_set,
            SVN_AUTH__KWALLET_PASSWORD_TYPE,
            pool);
}

static const svn_auth_provider_t kwallet_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  kwallet_ssl_client_cert_pw_first_creds,
  NULL,
  kwallet_ssl_client_cert_pw_save_creds
};

/* Public API */
void
svn_auth_get_kwallet_ssl_client_cert_pw_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po =
    static_cast<svn_auth_provider_object_t *> (apr_pcalloc(pool, sizeof(*po)));

  po->vtable = &kwallet_ssl_client_cert_pw_provider;
  *provider = po;
}
