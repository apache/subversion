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

#include <svn_ra.h>
#include <svn_client.h>
#include <svn_pools.h>

#include "svn_ruby.h"
#include "log.h"
#include "util.h"
#include "error.h"

svn_error_t *
svn_ruby_log_receiver (void *baton,
                       apr_hash_t *changed_paths,
                       svn_revnum_t revision,
                       const char *author,
                       const char *date,
                       const char *message)
{
  svn_ruby_log_receiver_baton_t *bt = baton;
  VALUE paths;
  int error;
  VALUE args[7];

  args[0] = bt->proc;
  args[1] = (VALUE) "call";
  args[3] = LONG2NUM (revision);
  args[4] = rb_str_new2 (author);
  args[5] = rb_str_new2 (date);
  args[6] = rb_str_new2 (message);

  if (changed_paths)
    {
      apr_hash_index_t *hi;
      paths = rb_hash_new ();

      for (hi = apr_hash_first (bt->pool, changed_paths); hi;
	   hi = apr_hash_next (hi))
	{
	  const void *key;
	  void *val;
	  apr_ssize_t key_len;
	  char action;

	  apr_hash_this (hi, &key, &key_len, &val);
	  action = (char) ((int) val);
	  rb_hash_aset (paths, rb_str_new (key, key_len),
			rb_str_new (&action, 1));

	}
    }
  else
    paths = Qnil;
    
  args[2] = paths;

  rb_protect (svn_ruby_protect_call5, (VALUE) args, &error);

  if (error)
    return svn_ruby_error ("message receiver", bt->pool);

  return SVN_NO_ERROR;
}

/* Get args for ra->get_log and svn_client_log.
   This function is tricky because it tries to delay pool creation until
   Ruby can't throw exception.
   New subpool is created and stored into baton->pool. */
void
svn_ruby_get_log_args (int argc,
                       VALUE *argv,
                       VALUE self,
                       apr_array_header_t **paths,
                       VALUE *start,
                       VALUE *end,
                       VALUE *discover_changed_paths,
                       svn_ruby_log_receiver_baton_t *baton,
                       apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool;
  VALUE aPaths, receiver;

  rb_scan_args (argc, argv, "40&", &aPaths, start, end,
                discover_changed_paths, &receiver);
  if (receiver == Qnil)
    rb_raise (rb_eRuntimeError, "no block is given");

  Check_Type (aPaths, T_ARRAY);
  for (i = 0; i < RARRAY (aPaths)->len; i++)
    Check_Type (RARRAY (aPaths)->ptr[i], T_STRING);

  subpool = svn_pool_create (pool);
  *paths = apr_array_make (subpool, RARRAY (aPaths)->len,
                           sizeof (svn_stringbuf_t *));
  for (i = 0; i < RARRAY (aPaths)->len; i++)
    (*((svn_stringbuf_t **) apr_array_push (*paths))) =
      svn_stringbuf_create (StringValuePtr (RARRAY (aPaths)->ptr[i]), subpool);

  baton->proc = receiver;
  baton->pool = subpool;

  /* GC protect */
  rb_iv_set (self, "@receiver", receiver);
}
