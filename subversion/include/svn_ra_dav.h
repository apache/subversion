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
 * @brief libsvn_ra_dav functions used by the server
 */

/** Retrieve a new ssl server certificate @a provider based around the
 *  server configuration.
 */
void 
svn_ra_dav_get_ssl_server_file_provider (const svn_auth_provider_t **provider,
					 void **provider_baton,
					 apr_pool_t *pool);

/** Retrieve a new ssl client certificate @a provider based around the
 *  server configuration.
 */
void 
svn_ra_dav_get_ssl_client_file_provider (const svn_auth_provider_t **provider,
					 void **provider_baton,
					 apr_pool_t *pool);

/** Retrieve a new ssl client certificate password @a provider based
 *  around the server configuration.
 */
void
svn_ra_dav_get_ssl_client_password_file_provider (const svn_auth_provider_t **provider,
						  void **provider_baton,
						  apr_pool_t *pool);
