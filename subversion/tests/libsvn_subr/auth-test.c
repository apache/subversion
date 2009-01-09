/*
 * auth-test.c -- test the auth functions
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

#include "svn_auth.h"
#include "svn_private_config.h"

#include "../svn_test.h"

static svn_error_t *
test_platform_specific_auth_providers(const char **msg,
                                      svn_boolean_t msg_only,
                                      svn_test_opts_t *opts,
                                      apr_pool_t *pool)
{
  apr_array_header_t *providers;
  svn_auth_provider_object_t *provider;
  int number_of_providers = 0;
  *msg = "test retrieving platform-specific auth providers";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Test non-available auth provider */
  svn_auth_get_platform_specific_provider(&provider, NULL, "fake", "fake",
                                          NULL, FALSE, pool);

  if (provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('fake', 'fake') should " \
       "return NULL");

  /* Make sure you get two providers when retrieving all auth providers */
  svn_auth_get_platform_specific_client_providers(&providers, NULL, NULL, NULL,
                                                  FALSE, pool);

#ifdef SVN_HAVE_GNOME_KEYRING
  number_of_providers += 2;
#endif
#ifdef SVN_HAVE_KWALLET
  number_of_providers += 2;
#endif
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
  number_of_providers += 2;
#endif
#if defined(WIN32) && !defined(__MINGW32__)
  number_of_providers += 2;
#endif
  if (providers->nelts != number_of_providers)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_client_providers should return " \
       "an array of %d providers", number_of_providers);

  /* Test Keychain auth providers */
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
  svn_auth_get_platform_specific_provider(&provider, NULL, "keychain",
                                          "simple", NULL, FALSE,
                                          pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('keychain', 'simple') "
       "should not return NULL");

  svn_auth_get_platform_specific_provider(&provider, NULL, "keychain",
                                          "ssl_client_cert_pw", NULL,
                                          FALSE, pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('keychain', " \
       "'ssl_client_cert_pw') should not return NULL");

  /* Make sure you do not get a Windows auth provider */
  svn_auth_get_platform_specific_provider(&provider, NULL, "windows",
                                          "simple", NULL, FALSE, pool);

  if (provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', 'simple') should " \
       "return NULL");
#endif

  /* Test Windows auth providers */
#if defined(WIN32) && !defined(__MINGW32__)
  svn_auth_get_platform_specific_provider(&provider, NULL, "windows",
                                          "simple", NULL, FALSE,
                                          pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', 'simple') "
       "should not return NULL");


  svn_auth_get_platform_specific_provider(&provider, NULL, "windows",
                                          "ssl_client_cert_pw", NULL, FALSE,
                                          pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', "
       "'ssl_client_cert_pw') should not return NULL");

  svn_auth_get_platform_specific_provider(&provider, NULL, "windows",
                                          "ssl_server_trust", NULL, FALSE,
                                          pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', "
       "'ssl_server_trust') should not return NULL");

  /* Make sure you do not get a Keychain auth provider */
  svn_auth_get_platform_specific_provider(&provider, NULL, "keychain",
                                          "simple", NULL, FALSE,
                                          pool);

  if (provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('keychain', 'simple') should " \
       "return NULL");
#endif

  /* Test GNOME Keyring auth providers */
#ifdef SVN_HAVE_GNOME_KEYRING
  svn_auth_get_platform_specific_provider(&provider, NULL, "gnome_keyring",
                                          "simple", NULL, FALSE, pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('gnome_keyring', 'simple') "
       "should not return NULL");

  svn_auth_get_platform_specific_provider(&provider, NULL, "gnome_keyring",
                                          "ssl_client_cert_pw", NULL, FALSE,
                                          pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('gnome_keyring', " \
       "'ssl_client_cert_pw') should not return NULL");

  /* Make sure you do not get a Windows auth provider */
  svn_auth_get_platform_specific_provider(&provider, NULL, "windows", "simple",
                                          NULL, FALSE, pool);

  if (provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', 'simple') should " \
       "return NULL");
#endif

  /* Test KWallet auth providers */
#ifdef SVN_HAVE_KWALLET
  svn_auth_get_platform_specific_provider(&provider, NULL, "kwallet", "simple",
                                          NULL, FALSE, pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('kwallet', 'simple') "
       "should not return NULL");

  svn_auth_get_platform_specific_provider(&provider, NULL, "kwallet",
                                          "ssl_client_cert_pw",
                                          NULL, FALSE, pool);

  if (!provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('kwallet', " \
       "'ssl_client_cert_pw') should not return NULL");

  /* Make sure you do not get a Windows auth provider */
  svn_auth_get_platform_specific_provider(&provider, NULL, "windows", "simple",
                                          NULL, FALSE, pool);

  if (provider)
    return svn_error_createf
      (SVN_ERR_TEST_FAILED, NULL,
       "svn_auth_get_platform_specific_provider('windows', 'simple') should " \
       "return NULL");
#endif

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_platform_specific_auth_providers),
    SVN_TEST_NULL
  };
