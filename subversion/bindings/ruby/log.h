/* ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#ifndef LOG_H
#define LOG_H
VALUE svn_ruby_ra_get_log (int argc, VALUE *argv, VALUE self,
                           svn_ra_plugin_t *plugin, void *session_baton,
                           apr_pool_t *pool);
VALUE svn_ruby_client_log (int argc, VALUE *argv, VALUE self,
                           svn_client_auth_baton_t *auth_baton);
#endif /* LOG_H */
