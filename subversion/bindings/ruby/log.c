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
#include <ruby.h>

#include <svn_ra.h>
#include <svn_client.h>
#include <svn_pools.h>

#include "svn_ruby.h"
#include "log.h"
#include "util.h"
#include "error.h"

struct log_receiver_baton_t
{
  VALUE proc;
  apr_pool_t *pool;
};

static svn_error_t *
log_receiver (void *baton,
	      apr_hash_t *changed_paths,
	      svn_revnum_t revision,
	      const char *author,
	      const char *date,
	      const char *message)
{
  struct log_receiver_baton_t *bt = baton;
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

static VALUE
get_log (int argc,
         VALUE *argv,
         VALUE self,
         svn_boolean_t ra_p,
         svn_ra_plugin_t *plugin,
         void *session_baton,
         svn_client_auth_baton_t *auth_baton,
         apr_pool_t *pool)
{
  VALUE aPaths, aStart, aEnd, discover_changed_paths, receiver;
  apr_array_header_t *paths;
  svn_revnum_t start, end;
  apr_pool_t *subpool;
  svn_error_t *err;
  int i;
  struct log_receiver_baton_t baton;

  rb_scan_args (argc, argv, "40&", &aPaths, &aStart, &aEnd,
		&discover_changed_paths, &receiver);
  if (receiver == Qnil)
    rb_raise (rb_eRuntimeError, "no block is given");

  Check_Type (aPaths, T_ARRAY);
  for (i = 0; i < RARRAY (aPaths)->len; i++)
    Check_Type (RARRAY (aPaths)->ptr[i], T_STRING);

  start = NUM2LONG (aStart);
  end = NUM2LONG (aEnd);
  subpool = svn_pool_create (pool);
  paths = apr_array_make (subpool, RARRAY (aPaths)->len,
			  sizeof (svn_stringbuf_t *));
  for (i = 0; i < RARRAY (aPaths)->len; i++)
    (*((svn_stringbuf_t **) apr_array_push (paths))) =
      svn_stringbuf_create (StringValuePtr (RARRAY (aPaths)->ptr[i]), subpool);

  rb_iv_set (self, "@receiver", receiver);
  baton.proc = receiver;
  baton.pool = subpool;
  if (ra_p)
    err = plugin->get_log (session_baton,
                           paths, start, end,
                           RTEST (discover_changed_paths),
                           log_receiver,
                           (void *)&baton);
  else
    err = svn_client_log (auth_baton,
                          paths, start, end,
                          RTEST (discover_changed_paths),
                          log_receiver,
                          (void *)&baton,
                          subpool);

  apr_pool_destroy (subpool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

VALUE
svn_ruby_ra_get_log (int argc,
                     VALUE *argv,
                     VALUE self,
                     svn_ra_plugin_t *plugin,
                     void *session_baton,
                     apr_pool_t *pool)
{
  return get_log (argc, argv, self,
                  TRUE, plugin, session_baton,
                  NULL,
                  pool);
}

VALUE
svn_ruby_client_log (int argc,
                     VALUE *argv,
                     VALUE self,
                     svn_client_auth_baton_t *auth_baton)
{
  return get_log (argc, argv, self,
                  FALSE, NULL, NULL,
                  auth_baton,
                  NULL);
}
