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
#ifndef LOG_H
#define LOG_H

typedef struct svn_ruby_log_receiver_baton_t
{
  VALUE proc;
  apr_pool_t *pool;
} svn_ruby_log_receiver_baton_t;

svn_error_t *
svn_ruby_log_receiver (void *baton,
                       apr_hash_t *changed_paths,
                       svn_revnum_t revision,
                       const char *author,
                       const char *date,
                       const char *message);

void
svn_ruby_get_log_args (int argc,
                       VALUE *argv,
                       VALUE self,
                       apr_array_header_t **paths,
                       VALUE *start,
                       VALUE *end,
                       VALUE *discover_changed_paths,
                       svn_ruby_log_receiver_baton_t *baton,
                       apr_pool_t *pool);


#endif /* LOG_H */
