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
#ifndef UTIL_H
#define UTIL_H
VALUE svn_ruby_protect_call0 (VALUE arg);
VALUE svn_ruby_protect_call1 (VALUE arg);
VALUE svn_ruby_protect_call2 (VALUE arg);
VALUE svn_ruby_protect_call3 (VALUE arg);
VALUE svn_ruby_protect_call5 (VALUE arg);
apr_status_t svn_ruby_set_refcount (apr_pool_t *pool, long count);
long svn_ruby_get_refcount (apr_pool_t *pool);

VALUE svn_ruby_str_hash (apr_hash_t *hash, apr_pool_t *pool);
VALUE svn_ruby_strbuf_hash (apr_hash_t *hash, apr_pool_t *pool);

#endif /* UTIL_H */

