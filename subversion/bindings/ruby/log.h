/*
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#ifndef SVN_RUBY__LOG_H
#define SVN_RUBY__LOG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct svn_ruby_log_receiver_baton_t
{
  VALUE proc;
} svn_ruby_log_receiver_baton_t;

svn_error_t *
svn_ruby_log_receiver (void *baton,
                       apr_hash_t *changed_paths,
                       svn_revnum_t revision,
                       const char *author,
                       const char *date,
                       const char *message,
                       apr_pool_t *pool);

void
svn_ruby_get_log_args (int argc,
                       VALUE *argv,
                       VALUE self,
                       apr_array_header_t **paths,
                       VALUE *start,
                       VALUE *end,
                       VALUE *discover_changed_paths,
                       VALUE *strict_node_history,
                       svn_ruby_log_receiver_baton_t *baton,
                       apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RUBY__LOG_H */
