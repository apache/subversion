/*
 * kwallet.cpp: KWallet providers for SVN_AUTH_CRED_SIMPLE
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
#include "svn_version.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#define SVN_AUTH__KWALLET_PASSWORD_TYPE            "kwallet"

/*-----------------------------------------------------------------------*/
/* KWallet simple provider, puts passwords in KWallet                    */
/*-----------------------------------------------------------------------*/

#ifdef SVN_HAVE_KWALLET
#include <QtCore/QString>
#include <QtGui/QWidget>

#include <kapplication.h>
#include <kcmdlineargs.h>
#include <kwallet.h>
#endif /* SVN_HAVE_KWALLET */

/* Implementation of svn_auth__password_get_t that retrieves
   the password from KWallet. */
static svn_boolean_t
kwallet_password_get(const char **password,
                     apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     svn_boolean_t non_interactive,
                     apr_pool_t *pool)
{
#ifdef SVN_HAVE_KWALLET
  if (! KWallet::Wallet::isEnabled())
  {
    return FALSE;
  }

  KCmdLineArgs::init(1,
                     (char *[1]) { "svn" },
                     "Subversion",
                     "subversion",
                     ki18n("Subversion"),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KApplication application;
  QWidget widget;
  WId wid = widget.winId();
  svn_boolean_t ret = FALSE;
  QString wallet_name = KWallet::Wallet::NetworkWallet();
  QString folder = QString::fromUtf8("Subversion");
  QString key = QString::fromUtf8(username) + "@" + QString::fromUtf8(realmstring);
  if (! KWallet::Wallet::keyDoesNotExist(wallet_name, folder, key))
    {
      KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, wid, KWallet::Wallet::Synchronous);
      if (wallet)
        {
          if (wallet->hasFolder(folder))
            {
              if (wallet->setFolder(folder))
                {
                  QString q_password;
                  if (! wallet->readPassword(key, q_password));
                    {
                      *password = apr_pstrmemdup(pool, q_password.toUtf8().data(), q_password.size());
                      ret = TRUE;
                    }
                }
            }
        }
    }
  KWallet::Wallet::closeWallet(wallet_name, false);
#else
  svn_boolean_t ret = FALSE;
#endif /* SVN_HAVE_KWALLET */
  return ret;
}

/* Implementation of svn_auth__password_set_t that stores
   the password in KWallet. */
static svn_boolean_t
kwallet_password_set(apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     const char *password,
                     svn_boolean_t non_interactive,
                     apr_pool_t *pool)
{
#ifdef SVN_HAVE_KWALLET
  if (! KWallet::Wallet::isEnabled())
  {
    return FALSE;
  }

  KCmdLineArgs::init(1,
                     (char *[1]) { "svn" },
                     "Subversion",
                     "subversion",
                     ki18n("Subversion"),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KApplication application;
  QWidget widget;
  WId wid = widget.winId();
  svn_boolean_t ret = FALSE;
  QString q_password = QString::fromUtf8(password);
  QString wallet_name = KWallet::Wallet::NetworkWallet();
  QString folder = QString::fromUtf8("Subversion");
  KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, wid, KWallet::Wallet::Synchronous);
  if (wallet)
    {
      if (! wallet->hasFolder(folder))
        {
          wallet->createFolder(folder);
        }
      if (wallet->hasFolder(folder))
        {
          if (wallet->setFolder(folder))
            {
              QString key = QString::fromUtf8(username) + "@" + QString::fromUtf8(realmstring);
              if (! wallet->writePassword(key, q_password))
                {
                  ret = TRUE;
                }
            }
        }
    }
  KWallet::Wallet::closeWallet(wallet_name, false);
#else
  svn_boolean_t ret = FALSE;
#endif /* SVN_HAVE_KWALLET */
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
extern "C" void
svn_auth_get_kwallet_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po =
    static_cast<svn_auth_provider_object_t *> (apr_pcalloc(pool, sizeof(*po)));

  po->vtable = &kwallet_simple_provider;
  *provider = po;
}
