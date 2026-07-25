/*
 * macos_keychain.c: Mac OS keychain providers for SVN_AUTH_*
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

#include <apr_pools.h>
#include "svn_auth.h"
#include "svn_ctype.h"
#include "svn_error.h"

#include "auth.h"
#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#ifdef SVN_HAVE_KEYCHAIN_SERVICES

/* Uncomment for testing the old SecKeychain*-based implementation. */
/* #undef SVN_HAVE_KEYCHAIN_SECITEM_API */

#include <Security/Security.h>

/*-----------------------------------------------------------------------*/
/* keychain simple provider, puts passwords in the macOS Keychain        */
/*-----------------------------------------------------------------------*/

/*
 * NOTE: On language compatibility.
 * This file uses some C99 extensions:
 *   - variable length stack-based arrays in build_dict;
 *   - aggregate initializers that are not compile-time constants
 *     in the SecItem* keychain_password_set and keychain_password_get.
 * This is fine; the first public release of macOS (or rather, Mac OS X
 * 10.0 Cheetah) is two years younger than the C99 standard, and gcc has
 * supported these features since long before 1999.
 */

/*
 * XXX (2005-12-07): If no GUI is available (e.g. over a SSH session),
 * you won't be prompted for credentials with which to unlock your
 * keychain.  Apple recognizes lack of TTY prompting as a known
 * problem.
 *
 *
 * XXX (2005-12-07): SecKeychainSetUserInteractionAllowed(FALSE) does
 * not appear to actually prevent all user interaction.  Specifically,
 * if the executable changes (for example, if it is rebuilt), the
 * system prompts the user to okay the use of the new executable.
 *
 * Worse than that, the interactivity setting is global per app (not
 * process/thread), meaning that there is a race condition in the
 * implementation below between calls to
 * SecKeychainSetUserInteractionAllowed() when multiple instances of
 * the same Subversion auth provider-based app run concurrently.
 *
 *
 * XXX (2025-06-18): All of the SecKeychain* API was deprecated in macOS 10.10
 * (Yosemite, released in October 2014). The new SecItem* API was inherited
 * from iOS, but it sometimes behaves strangely on macOS, see:
 *
 *    https://developer.apple.com/forums/thread/724013
 *
 * Still, it's the way forward and the best way to avoid the deprecation
 * warnings that pollute maintainer-mode builds. It also gives us some nice
 * goodies, such as having a different label on the keychain item than the
 * realm name, and a description to see the difference between a repository
 * password and a client certificate passphrase.
 */

/* Forward declarations for the SecItem-based implementation. */

/* Implementation of svn_auth__password_set_t that stores
   the password in the OS X Keychain. */
static svn_error_t *
keychain_password_set(svn_boolean_t *done,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      const char *password,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool);

/* Implementation of svn_auth__password_get_t that retrieves
   the password from the OS X Keychain. */
static svn_error_t *
keychain_password_get(svn_boolean_t *done,
                      const char **password,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool);


/* It turns out that it's really not possible to do this without using any
   deprecated symbols at all, at least not in command-line code. The
   SecItem* variants kSecUseNoAuthenticationUI, kSecUseAuthenticationUI
   either don't work on macOS, are also deprecated or both. */
#ifdef __APPLE__
#  if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  endif
#endif /* __APPLE__ */
static void allow_user_interaction(svn_boolean_t non_interactive)
{
  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(TRUE);
}
static void forbid_user_interaction(svn_boolean_t non_interactive)
{
  if (non_interactive)
    SecKeychainSetUserInteractionAllowed(FALSE);
}
#ifdef __APPLE__
#  if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2)
#    pragma GCC diagnostic pop
#  endif
#endif /* __APPLE__ */


#if !SVN_HAVE_KEYCHAIN_SECITEM_API

static svn_error_t *
keychain_password_set(svn_boolean_t *done,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      const char *password,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  OSStatus status;
  SecKeychainItemRef item;

  forbid_user_interaction(non_interactive);

  status = SecKeychainFindGenericPassword(NULL, (int) strlen(realmstring),
                                          realmstring, username == NULL
                                            ? 0
                                            : (int) strlen(username),
                                          username, 0, NULL, &item);
  if (status)
    {
      if (status == errSecItemNotFound)
        status = SecKeychainAddGenericPassword(NULL, (int) strlen(realmstring),
                                               realmstring, username == NULL
                                                 ? 0
                                                 : (int) strlen(username),
                                               username, (int) strlen(password),
                                               password, NULL);
    }
  else
    {
      status = SecKeychainItemModifyAttributesAndData(item, NULL,
                                                      (int) strlen(password),
                                                      password);
      CFRelease(item);
    }

  allow_user_interaction(non_interactive);

  *done = (status == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
keychain_password_get(svn_boolean_t *done,
                      const char **password,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  OSStatus status;
  UInt32 length;
  void *data;

  *done = FALSE;

  forbid_user_interaction(non_interactive);

  status = SecKeychainFindGenericPassword(NULL, (int) strlen(realmstring),
                                          realmstring, username == NULL
                                            ? 0
                                            : (int) strlen(username),
                                          username, &length, &data, NULL);

  allow_user_interaction(non_interactive);

  if (status != 0)
    return SVN_NO_ERROR;

  *password = apr_pstrmemdup(pool, data, length);
  SecKeychainItemFreeContent(NULL, data);
  *done = TRUE;
  return SVN_NO_ERROR;
}

#endif  /* !SVN_HAVE_KEYCHAIN_SECITEM_API */

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
keychain_simple_first_creds(void **credentials,
                            void **iter_baton,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_get(credentials,
                                          iter_baton,
                                          provider_baton,
                                          parameters,
                                          realmstring,
                                          keychain_password_get,
                                          SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                          pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
keychain_simple_save_creds(svn_boolean_t *saved,
                           void *credentials,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_set(saved, credentials,
                                          provider_baton,
                                          parameters,
                                          realmstring,
                                          keychain_password_set,
                                          SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                          pool);
}

static const svn_auth_provider_t keychain_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  keychain_simple_first_creds,
  NULL,
  keychain_simple_save_creds
};

/* Get cached encrypted credentials from the ssl client cert password
   provider's cache. */
static svn_error_t *
keychain_ssl_client_cert_pw_first_creds(void **credentials,
                                        void **iter_baton,
                                        void *provider_baton,
                                        apr_hash_t *parameters,
                                        const char *realmstring,
                                        apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_get(credentials,
                                                iter_baton, provider_baton,
                                                parameters, realmstring,
                                                keychain_password_get,
                                                SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                                pool);
}

/* Save encrypted credentials to the ssl client cert password provider's
   cache. */
static svn_error_t *
keychain_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_set(saved, credentials,
                                                provider_baton, parameters,
                                                realmstring,
                                                keychain_password_set,
                                                SVN_AUTH__KEYCHAIN_PASSWORD_TYPE,
                                                pool);
}

static const svn_auth_provider_t keychain_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  keychain_ssl_client_cert_pw_first_creds,
  NULL,
  keychain_ssl_client_cert_pw_save_creds
};


/* Public API */
void
svn_auth__get_keychain_simple_provider(svn_auth_provider_object_t **provider,
                                      apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &keychain_simple_provider;
  *provider = po;
}

void
svn_auth__get_keychain_ssl_client_cert_pw_provider
  (svn_auth_provider_object_t **provider,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &keychain_ssl_client_cert_pw_provider;
  *provider = po;
}


/* ==================================================================== */
/* Using the SecItem API. */
#if SVN_HAVE_KEYCHAIN_SECITEM_API

/* Helpers for dealing with query dictionaries. */
struct query_entry {
  CFStringRef key;
  CFTypeRef value;
};

/* Compensate for the horrible Core Foundation API that builds dictionaries
   from two separate arrays of keys and values, which is ... less than
   readable and interestingly error prone. */
static CFDictionaryRef build_dict(const struct query_entry items[],
                                  const CFIndex length)
{
  /* Use fixed buffer sizes to avoid allocation. */
  static const CFIndex max_length = 12;

  if (length <= max_length)
    {
      const void *keys[max_length];
      const void *values[max_length];
      CFIndex i;

      /* Copy the keys and values into separate arrays. */
      for (i = 0; i < length; ++i)
        {
          keys[i] = items[i].key;
          values[i] = items[i].value;
        }

      return CFDictionaryCreate(kCFAllocatorDefault,
                                keys, values, length,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
    }

  /* abort(); (for debugging) */
  return NULL;
}

/* Convert the generated REALMSTRING to a user-friendly label.
   Assume REALMSTRING looks like "<uri:something> Realm" and skip
   the <uri...> part. If that's not the case, or if the Realm bit is
   empty, then return the whole REALMSTRING.
*/
static const char *make_label(const char *realmstring, apr_pool_t *pool)
{
  static const char prefix[] = "Subversion: ";
  const char *p;

  if (!realmstring || realmstring[0] != '<')
    return realmstring;

  /* Find the end of the <uri...> and skip whitespace after the closing >. */
  p = strchr(realmstring, '>');
  if (!p)
    return realmstring;

  do { ++p; } while (*p && svn_ctype_isspace(*p));
  if (!*p)
    return realmstring;

  return apr_pstrcat(pool, prefix, p, NULL);
}

/* Wrap an immutable C string CSTR in a CFString.
   The returned reference must be disposed with CFRelease. */
static CFStringRef cfstr(const char *cstr)
{
  return CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                         cstr ? cstr : "",
                                         kCFStringEncodingUTF8,
                                         kCFAllocatorNull);
}

/* Wrap an immutable C string PASSWD in a CFData. Only good for passwords.
   The returned reference must be disposed with CFRelease. */
static CFDataRef cfdata(const char *passwd)
{
  const UInt8 *data = (UInt8*)(passwd ? passwd : "");
  const CFIndex length = strlen((char*)data);

  return CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
                                     data, length,
                                     kCFAllocatorNull);
}

/* Wraps CFRelease so that ITEM can be NULL. */
static void safe_CFRelease(CFTypeRef item)
{
    if (item)
        CFRelease(item);
}


static svn_error_t *
keychain_password_set(svn_boolean_t *done,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      const char *password,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  /* This is the user-visible item description in the Keychain. Defaults to
     "application password", which isn't too user-friendly because the
     realmstring is the same for both the client-cert passphrase and the
     repository password.

     Certificate passphrases get an empty username, see DUMMY_USERNAME
     in subversion/libsvn_subr/ssl_client_cert_pw_providers.c. */
  CFStringRef description = (username && *username
                             ? cfstr(_("repository password"))
                             : cfstr(_("certificate passphrase")));

  CFStringRef service = cfstr(realmstring);
  CFStringRef account = cfstr(username);
  CFStringRef label = cfstr(make_label(realmstring, pool));
  CFDataRef value = cfdata(password);

  CFDictionaryRef add_query = NULL;
  CFDictionaryRef update_query = NULL;
  CFDictionaryRef update_attrs = NULL;
  OSStatus status = errSecSuccess;

  const struct query_entry query_items[] = {
    { kSecClass, kSecClassGenericPassword },
    { kSecAttrService, service },
    { kSecAttrAccount, account },
    { kSecMatchLimit, kSecMatchLimitOne },
    /* These three entries are used only to add a new password. */
    { kSecAttrDescription, description },
    { kSecAttrLabel, label },
    { kSecValueData, value }
  };
  const CFIndex add_length = sizeof(query_items) / sizeof(*query_items);
  const CFIndex update_length = add_length - 3;

  forbid_user_interaction(non_interactive);
  *done = FALSE;

  /* Memory allocation gone wrong? */
  if (!service || !account || !label || !value)
    goto cleanup;

  add_query = build_dict(query_items, add_length);
  if (!add_query)
    goto cleanup;

  status = SecItemAdd(add_query, NULL);
  if (status == errSecSuccess)
    {
      *done = TRUE;
    }
  else if (status == errSecDuplicateItem)
    {
      const struct query_entry attr_items[] = {
        { kSecAttrDescription, description }, /* Update the description. */
        { kSecAttrLabel, label },   /* Update the label to the new format. */
        { kSecValueData, value }    /* Update the password. */
      };
      const CFIndex attr_length = sizeof(attr_items) / sizeof(*attr_items);

      update_query = build_dict(query_items, update_length);
      update_attrs = build_dict(attr_items, attr_length);
      if (update_query && update_attrs)
        {
          status = SecItemUpdate(update_query, update_attrs);
          if (status == errSecSuccess)
            *done = TRUE;
        }
    }

 cleanup:
  allow_user_interaction(non_interactive);

  /* Release in reverse order of allocation. */
  safe_CFRelease(update_attrs);
  safe_CFRelease(update_query);
  safe_CFRelease(add_query);
  safe_CFRelease(value);
  safe_CFRelease(label);
  safe_CFRelease(account);
  safe_CFRelease(service);
  safe_CFRelease(description);

  return SVN_NO_ERROR;
}


static svn_error_t *
keychain_password_get(svn_boolean_t *done,
                      const char **password,
                      apr_hash_t *creds,
                      const char *realmstring,
                      const char *username,
                      apr_hash_t *parameters,
                      svn_boolean_t non_interactive,
                      apr_pool_t *pool)
{
  CFStringRef service = cfstr(realmstring);
  CFStringRef account = cfstr(username);
  CFDictionaryRef query = NULL;
  CFTypeRef item = NULL;
  OSStatus status = errSecSuccess;

  const struct query_entry query_items[] = {
    { kSecClass, kSecClassGenericPassword },
    { kSecAttrService, service },
    { kSecAttrAccount, account },
    { kSecMatchLimit, kSecMatchLimitOne },
    /* Return only the data (password), which is actually a CFData object. */
    { kSecReturnData, kCFBooleanTrue }
  };
  const CFIndex query_length = sizeof(query_items) / sizeof(*query_items);

  forbid_user_interaction(non_interactive);
  *done = FALSE;

  /* Memory allocation gone wrong? */
  if (!service || !account)
    goto cleanup;

  query = build_dict(query_items, query_length);
  if (!query)
    goto cleanup;

  status = SecItemCopyMatching(query, &item);
  if (status == errSecSuccess)
    {
      const CFIndex length = CFDataGetLength(item);
      char *const pwd = apr_palloc(pool, length + 1);

      memcpy(pwd, CFDataGetBytePtr(item), length);
      pwd[length] = '\0';
      *password = pwd;
      *done = TRUE;
    }

 cleanup:
  allow_user_interaction(non_interactive);

  /* Release in reverse order of allocation. */
  safe_CFRelease(item);
  safe_CFRelease(query);
  safe_CFRelease(account);
  safe_CFRelease(service);

  return SVN_NO_ERROR;
}

#endif  /* SVN_HAVE_KEYCHAIN_SECITEM_API */
#endif  /* SVN_HAVE_KEYCHAIN_SERVICES */
