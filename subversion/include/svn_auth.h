/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_auth.h
 * @brief Interface to Subversion authentication system
 */

#ifndef SVN_AUTH_H
#define SVN_AUTH_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Overview of the svn authentication system.
 *    
 * We define an authentication "provider" as a module that is able to
 * return a specific set of credentials. (e.g. username/password,
 * certificate, etc.)  Each provider implements a vtable that
 *
 * - can fetch initial credentials
 * - can retry the fetch (or try to fetch something different)
 * - can store the credentials for future use
 *
 * For any given type of credentials, there can exist any number of
 * separate providers -- each provider has a different method of
 * fetching. (i.e. from a disk store, by prompting the user, etc.)
 *
 * The application begins by creating an auth baton object, and
 * "registers" some number of providers with the auth baton, in a
 * specific order.  (For example, it may first register a
 * username/password provider that looks in disk store, then register
 * a username/password provider that prompts the user.)
 *
 * Later on, when any svn library is challenged, it asks the auth
 * baton for the specific credentials.  If the initial credentials
 * fail to authenticate, the caller keeps requesting new credentials.
 * Under the hood, libsvn_auth effectively "walks" over each provider
 * (in order of registry), one at a time, until all the providers have
 * exhausted all their retry options.
 *
 * This system allows an application to flexibly define authentication
 * behaviors (by changing registration order), and very easily write
 * new authentication providers.
 *
 * An auth_baton also contains an internal hashtable of run-time
 * parameters; any provider or library layer can set these run-time
 * parameters at any time, so that the provider has access to the
 * data.  (For example, certain run-time data may not be available
 * until an authentication challenge is made.)  Each provider must
 * document run-time parameters it requires.
 *
 * @defgroup auth_fns authentication functions
 * @{
 */


/** The type of a Subversion authentication object */
typedef struct svn_auth_baton_t svn_auth_baton_t;

/** The type of a Subversion authentication-iteration object */
typedef struct svn_auth_iterstate_t svn_auth_iterstate_t;


/** The main authentication "provider" vtable. */
typedef struct
{
  /** The kind of credentials this provider knows how to retrieve. */
  const char *cred_kind;
  
  /** Get an initial set of credentials.
   *
   * Set @a *credentials to a set of valid credentials within @a
   * realmstring, or NULL if no credentials are available.  Set @a
   * *iter_baton to context that allows a subsequent call to @c
   * next_credentials, in case the first credentials fail to
   * authenticate.  @a provider_baton is general context for the
   * vtable, and @a parameters contains any run-time data that the
   * provider may need.
   */
  svn_error_t * (*first_credentials) (void **credentials,
                                      void **iter_baton,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool);

  /** Get a different set of credentials.
   *
   * Set @a *credentials to another set of valid credentials, (using
   * @a iter_baton as the context from previous call to first_credentials
   * or next_credentials).  If no more credentials are available, set
   * @a **credentials to NULL.  If the provider only has one set of
   * credentials, this function pointer should simply be NULL.  @a
   * parameters contains any run-time data that the provider may need.
   */
  svn_error_t * (*next_credentials) (void **credentials,
                                     void *iter_baton,
                                     apr_hash_t *parameters,
                                     apr_pool_t *pool);
  
  /** Save credentials.
   *
   * Store @a credentials for future use.  @a provider_baton is
   * general context for the vtable, and @a parameters contains any
   * run-time data the provider may need.  Set @a *saved to true if
   * the save happened, or false if not.  The provider is not required
   * to save; if it refuses or is unable to save for non-fatal
   * reasons, return false.  If the provider never saves data, then
   * this function pointer should simply be NULL.
   */
  svn_error_t * (*save_credentials) (svn_boolean_t *saved,
                                     void *credentials,
                                     void *provider_baton,
                                     apr_hash_t *parameters,
                                     apr_pool_t *pool);
  
} svn_auth_provider_t;


/** A provider object, ready to be put into an array and given to
    @c svn_auth_open. */
typedef struct
{
  const svn_auth_provider_t *vtable;
  void *provider_baton;

} svn_auth_provider_object_t;



/** Specific types of credentials **/

/** A simple username/password pair. */
#define SVN_AUTH_CRED_SIMPLE "svn.simple"
typedef struct
{
  const char *username;
  const char *password;
  
} svn_auth_cred_simple_t;

/** Just a username. */
#define SVN_AUTH_CRED_USERNAME "svn.username"
typedef struct
{
  const char *username;

} svn_auth_cred_username_t;

/** SSL client authentication - this provides @a cert_file and
    optionally @a key_file (if the private key is separate) as the
    full paths to the files, and sets @a cert_type for the type of
    certificate file to load */
#define SVN_AUTH_CRED_CLIENT_SSL "svn.ssl.client-cert"
typedef struct
{
  const char *cert_file;
} svn_auth_cred_client_ssl_t;

/** SSL client passphrase.
 *
 * @a password gets set with the appropriate password for the
 * certificate.
 */
#define SVN_AUTH_CRED_CLIENT_PASS_SSL "svn.ssl.client-passphrase"
typedef struct
{
  const char *password;

} svn_auth_cred_client_ssl_pass_t;

/** SSL server verification.
 *
 *  If @a trust_permanantly is set to true by the provider, the
 *  certificate will be trusted permanently.
 */
#define SVN_AUTH_CRED_SERVER_SSL "svn.ssl.server"
typedef struct
{
  svn_boolean_t trust_permanantly;
} svn_auth_cred_server_ssl_t;


/** SSL server certificate information.
 */
typedef struct {
  const char *hostname;
  const char *fingerprint;
  const char *valid_from;
  const char *valid_until;
  const char *issuer_dname;

  /* The full certificate as base-64 encoded DER */
  const char *ascii_cert;
} svn_auth_ssl_server_cert_info_t;


/** Credential-constructing prompt functions. **/

/** These exist so that different client applications can use
 * different prompt mechanisms to supply the same credentials.  For
 * example, if authentication requires a username and password, a
 * command-line client's prompting function might prompt first for the
 * username and then for the password, whereas a GUI client's would
 * present a single dialog box asking for both, and a telepathic
 * client's would read all the information directly from the user's
 * mind.  All these prompting functions return the same type of
 * credential, but the information used to construct the credential is
 * gathered in an interface-specific way in each case.
 */

/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * If @a realm is non-null, maybe use it in the prompt string.
 *
 * If @a username is non-null, then the user might be prompted only
 * for a password, but @a *creds would still be filled with both
 * username and password.  For example, a typical usage would be to
 * pass @a username on the first call, but then leave it null for
 * subsequent calls, on the theory that if credentials failed, it's
 * as likely to be due to incorrect username as incorrect password. 
 */
typedef svn_error_t *
(*svn_auth_simple_prompt_func_t) (svn_auth_cred_simple_t **cred,
                                  void *baton,
                                  const char *realm,
                                  const char *username,
                                  apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * If @a realm is non-null, maybe use it in the prompt string.
 */
typedef svn_error_t *
(*svn_auth_username_prompt_func_t) (svn_auth_cred_username_t **cred,
                                    void *baton,
                                    const char *realm,
                                    apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * @a cert_info is a structure describing the server cert that was
 * presented to the client, and @a failures is a bitmask that
 * describes exactly why the cert could not be automatically validated.
 * (See the #define error flag values below.)
 *
 */
#define SVN_AUTH_SSL_NOTYETVALID (1<<0)
#define SVN_AUTH_SSL_EXPIRED     (1<<1)
#define SVN_AUTH_SSL_CNMISMATCH  (1<<2)
#define SVN_AUTH_SSL_UNKNOWNCA   (1<<3)

typedef svn_error_t *(*svn_auth_ssl_server_prompt_func_t) (
  svn_auth_cred_server_ssl_t **cred,
  void *baton,
  int failures,
  const svn_auth_ssl_server_cert_info_t *cert_info,
  apr_pool_t *pool);

/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 */
typedef svn_error_t *
(*svn_auth_ssl_client_prompt_func_t) (svn_auth_cred_client_ssl_t **cred,
                                      void *baton,
                                      apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 */
typedef svn_error_t *
(*svn_auth_ssl_pw_prompt_func_t) (svn_auth_cred_client_ssl_pass_t **cred,
                                  void *baton,
                                  apr_pool_t *pool);



/** Initialize an authentication system.
 *
 * Return an authentication object in @a *auth_baton (allocated in @a
 * pool) that represents a particular instance of the svn
 * authentication system.  @a providers is an array of @c
 * svn_auth_provider_object_t pointers, already allocated in @a pool
 * and intentionally ordered.  These pointers will be stored within @a
 * *auth_baton, grouped by credential type, and searched in this exact
 * order.
 */
void svn_auth_open(svn_auth_baton_t **auth_baton,
                   apr_array_header_t *providers,
                   apr_pool_t *pool);

/** Set an authentication run-time parameter.
 *
 * Store @a name / @a value pair as a run-time parameter in @a
 * auth_baton, making the data accessible to all providers.  @a name
 * and @a value will be NOT be duplicated into the auth_baton's
 * pool. To delete a run-time parameter, pass NULL for @a value.
 */
void svn_auth_set_parameter(svn_auth_baton_t *auth_baton,
                            const char *name,
                            const void *value);

/** Get an authentication run-time parameter.
 *
 * Return a value for run-time parameter @a name from @a auth_baton.
 * Return NULL if the parameter doesn't exist.
 */
const void * svn_auth_get_parameter(svn_auth_baton_t *auth_baton,
                                    const char *name);

/** Universal run-time parameters, made available to all providers.

    If you are writing a new provider, then to be a "good citizen",
    you should notice these global parameters!  Note that these
    run-time params should be treated as read-only by providers; the
    application is responsible for placing them into the auth_baton
    hash. */

/** The auth-hash prefix indicating that the parameter is global */
#define SVN_AUTH_PARAM_PREFIX "svn:auth:"

/** Any 'default' credentials that came in through the application
    itself, (e.g. --username and --password options).  Property values
    are const char *.  */
#define SVN_AUTH_PARAM_DEFAULT_USERNAME  SVN_AUTH_PARAM_PREFIX "username"
#define SVN_AUTH_PARAM_DEFAULT_PASSWORD  SVN_AUTH_PARAM_PREFIX "password"

/** The application doesn't want any providers to prompt users.
    Property value is irrelevant; only property's existence matters. */
#define SVN_AUTH_PARAM_NON_INTERACTIVE  SVN_AUTH_PARAM_PREFIX "non-interactive"

/** The application doesn't want any providers to save credentials to disk.
    Property value is irrelevant; only property's existence matters. */
#define SVN_AUTH_PARAM_NO_AUTH_CACHE  SVN_AUTH_PARAM_PREFIX "no-auth-cache"

/** The following property is for ssl server cert providers. This
    provides the detected failures by the certificate validator */
#define SVN_AUTH_PARAM_SSL_SERVER_FAILURES SVN_AUTH_PARAM_PREFIX \
  "ssl:failures"

/** The following property is for ssl server cert providers. This
    provides the cert info (svn_auth_ssl_server_cert_info_t). */
#define SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO SVN_AUTH_PARAM_PREFIX \
  "ssl:cert-info"

/** Some providers need access to the @c svn_config_t configuration
    for individual servers in order to properly operate */
#define SVN_AUTH_PARAM_CONFIG SVN_AUTH_PARAM_PREFIX "config"
#define SVN_AUTH_PARAM_SERVER_GROUP SVN_AUTH_PARAM_PREFIX "server-group"

/** A configuration directory that overrides the default 
    ~/.subversion. */
#define SVN_AUTH_PARAM_CONFIG_DIR SVN_AUTH_PARAM_PREFIX "config-dir"

/** Get an initial set of credentials.
 *
 * Ask @a auth_baton to set @a *credentials to a set of credentials
 * defined by @a cred_kind and valid within @a realmstring, or NULL if
 * no credentials are available.  Otherwise, return an iteration state
 * in @a *state, so that the caller can call @c
 * svn_auth_next_credentials, in case the first set of credentials
 * fails to authenticate.
 *
 * Use @a pool to allocate @a *state, and for temporary allocation.
 * Note that there is no guarantee about where @a *credentials will be
 * allocated: it might be in @a pool, or it might be in @a
 * auth_baton->pool, depending on the provider.  So safe callers
 * should duplicate the credentials to safe place if they plan to free
 * @a pool.
 */
svn_error_t * svn_auth_first_credentials(void **credentials,
                                         svn_auth_iterstate_t **state,
                                         const char *cred_kind,
                                         const char *realmstring,
                                         svn_auth_baton_t *auth_baton,
                                         apr_pool_t *pool);

/** Get another set of credentials, assuming previous ones failed to
 * authenticate.
 *
 * Use @a state to fetch a different set of @a *credentials, as a
 * follow-up to @c svn_auth_first_credentials or @c
 * svn_auth_next_credentials.  If no more credentials are available,
 * set @a *credentials to NULL.
 *
 * Use @a pool for temporary allocation.  Note that there is no
 * guarantee about where @a *credentials will be allocated: it might
 * be in @a pool, or it might be in @a auth_baton->pool, depending on
 * the provider.  So safe callers should duplicate the credentials to
 * safe place if they plan to free @a pool.
 */
svn_error_t * svn_auth_next_credentials(void **credentials,
                                        svn_auth_iterstate_t *state,
                                        apr_pool_t *pool);

/** Save a set of credentials.
 *
 * Ask @a state to store the most recently returned credentials,
 * presumably because they successfully authenticated.  Use @a pool
 * for temporary allocation.  If no credentials were ever returned, do
 * nothing.
 */
svn_error_t * svn_auth_save_credentials(svn_auth_iterstate_t *state,
                                        apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_H */
