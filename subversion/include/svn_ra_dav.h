/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @file svn_ra_dav.h
 * @brief libsvn_ra_dav functions for use by clients
 */

/** Set @a *provider and @ *provider_baton to an authentication
 *  provider for type @c svn_auth_cred_server_ssl_t gets information from the
 *  configuration mechanism. This credential is used to override SSL security
 *  on an error.
 *  
 *   This provider requires certain run-time parameters be present in
 *   the auth_baton:
 *
 *    - a loaded @c svn_config_t object
 *             (@c SVN_AUTH_PARAM_CONFIG)
 *
 *    - the name of the server-specific settings group if available
 *             (@c SVN_AUTH_PARAM_SERVER_GROUP)
 */
void 
svn_ra_dav_get_ssl_server_file_provider (const svn_auth_provider_t **provider,
                                         void **provider_baton,
                                         apr_pool_t *pool);

/** Set @a *provider and @ *provider_baton to an authentication
 *  provider for type @c svn_auth_cred_client_ssl_t gets information from the
 *  configuration mechanism. This credential is used to load the appropriate
 *  client certificate for authentication, if requested by the server.
 *  
 *   This provider requires certain run-time parameters be present in
 *   the auth_baton:
 *
 *    - a loaded @c svn_config_t object
 *             (@c SVN_AUTH_PARAM_CONFIG)
 *
 *    - the name of the server-specific settings group if available
 *             (@c SVN_AUTH_PARAM_SERVER_GROUP)
 */
void 
svn_ra_dav_get_ssl_client_file_provider (const svn_auth_provider_t **provider,
                                         void **provider_baton,
                                         apr_pool_t *pool);

/** Set @a *provider and @ *provider_baton to an authentication
 *  provider for type @c svn_auth_cred_client_ssl_pass_t gets information
 *  from the configuration mechanism. This credential is used to decode
 *  a client certificate if a passphrase is required.
 *  
 *   This provider requires certain run-time parameters be present in
 *   the auth_baton:
 *
 *    - a loaded @c svn_config_t object
 *             (@c SVN_AUTH_PARAM_CONFIG)
 *
 *    - the name of the server-specific settings group if available
 *             (@c SVN_AUTH_PARAM_SERVER_GROUP)
 */
void
svn_ra_dav_get_ssl_pw_file_provider (const svn_auth_provider_t **provider,
                                     void **provider_baton,
                                     apr_pool_t *pool);
