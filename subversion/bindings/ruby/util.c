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
#include <ruby.h>
#include <svn_pools.h>
#include <svn_string.h>
#include <svn_client.h>

#include <stdlib.h>
#include <apr_general.h>

#include "svn_ruby.h"
#include "util.h"

#define SVN_RUBY_REFCOUNT "svn-ruby-pool-refcount"

static int initialized = 0;

VALUE
svn_ruby_protect_call0 (VALUE arg)
{
  VALUE *args = (VALUE *) arg;
  return rb_funcall2 (args[0], rb_intern ((char *) args[1]), 0, NULL);
}

VALUE
svn_ruby_protect_call1 (VALUE arg)
{
  VALUE *args = (VALUE *) arg;
  return rb_funcall2 (args[0], rb_intern ((char *) args[1]), 1, args + 2);
}

VALUE
svn_ruby_protect_call2 (VALUE arg)
{
  VALUE *args = (VALUE *) arg;
  return rb_funcall2 (args[0], rb_intern ((char *) args[1]), 2, args + 2);
}

VALUE
svn_ruby_protect_call3 (VALUE arg)
{
  VALUE *args = (VALUE *) arg;
  return rb_funcall2 (args[0], rb_intern ((char *) args[1]), 3, args + 2);
}

VALUE
svn_ruby_protect_call5 (VALUE arg)
{
  VALUE *args = (VALUE *) arg;
  return rb_funcall2 (args[0], rb_intern ((char *) args[1]), 5, args + 2);
}

apr_status_t
svn_ruby_set_refcount (apr_pool_t *pool, long count)
{
  return apr_pool_userdata_set ((void *) count, SVN_RUBY_REFCOUNT,
                                apr_pool_cleanup_null, pool);
}

long
svn_ruby_get_refcount (apr_pool_t *pool)
{
  void *value;
  apr_pool_userdata_get (&value, SVN_RUBY_REFCOUNT, pool);
  return (long) value;
}

VALUE
svn_ruby_str_hash (apr_hash_t *hash, apr_pool_t *pool)
{
  VALUE obj;
  apr_hash_index_t *hi;

  obj = rb_hash_new ();

  for (hi = apr_hash_first (pool, hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_ssize_t key_len;
      svn_string_t *value;

      apr_hash_this (hi, &key, &key_len, &val);
      value = (svn_string_t *) val;
      rb_hash_aset (obj, rb_str_new (key, key_len),
		    rb_str_new (value->data, value->len));
    }

  return obj;
}

VALUE
svn_ruby_strbuf_hash (apr_hash_t *hash, apr_pool_t *pool)
{
  VALUE obj;
  apr_hash_index_t *hi;

  obj = rb_hash_new ();

  for (hi = apr_hash_first (pool, hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_ssize_t key_len;
      svn_stringbuf_t *value;

      apr_hash_this (hi, &key, &key_len, &val);
      value = (svn_stringbuf_t *) val;
      rb_hash_aset (obj, rb_str_new (key, key_len),
                    rb_str_new (value->data, value->len));
    }

  return obj;
}

void
svn_ruby_init_apr (void)
{
  if (initialized)
    return;

  apr_initialize ();
  atexit (&apr_terminate);
  initialized++;
}
