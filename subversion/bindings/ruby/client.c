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
#include <svn_client.h>
#include <svn_path.h>

#include "svn_ruby.h"
#include "wc.h"
#include "log.h"
#include "util.h"
#include "error.h"

static svn_error_t *
cl_log_message_func (const char **log_msg,
                     apr_array_header_t *commit_items,
                     void *baton,
                     apr_pool_t *pool)
{
  *log_msg = apr_pstrdup (pool, baton);

  return SVN_NO_ERROR;
}

static svn_error_t *
cl_prompt (char **info,
           const char *prompt,
           svn_boolean_t hide,
           void *baton,
           apr_pool_t *pool)
{
  VALUE self = (VALUE) baton;
  VALUE obj;
  int error;
  VALUE args[4];

  args[0] = self;
  args[1] = (VALUE) "call";
  args[2] = rb_str_new2 (prompt);
  args[3] = hide ? Qtrue : Qfalse;

  if (self == Qnil)
    svn_error_createf
      (APR_EGENERAL, 0, 0,
       "Authentication is required but no block is given to get user data");
  obj = rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    return svn_ruby_error ("authenticator", pool);

  if (BUILTIN_TYPE (obj) != T_STRING)
    return svn_error_create (APR_EGENERAL, 0, 0,
                             "auth block must return string object");

  *info = apr_pstrdup (pool, StringValuePtr (obj));
  return SVN_NO_ERROR;
}

static svn_opt_revision_t
parse_revision (VALUE revOrDate)
{
  svn_opt_revision_t revision;
  if (rb_obj_is_kind_of (revOrDate, rb_cTime) == Qtrue)
    {
      time_t sec, usec;
      sec = NUM2LONG (rb_funcall (revOrDate, rb_intern ("tv_sec"), 0));
      usec = NUM2LONG (rb_funcall (revOrDate, rb_intern ("tv_usec"), 0));
      revision.kind = svn_opt_revision_date;
      revision.value.date = sec * APR_USEC_PER_SEC + usec;
    }
  else if (revOrDate == Qnil)
    revision.kind = svn_opt_revision_unspecified;
  else
    {
      revision.kind = svn_opt_revision_number;
      revision.value.number = NUM2LONG (revOrDate);
    }
  return revision;
}


static VALUE
commit_info_to_array (svn_client_commit_info_t *commit_info)
{
  VALUE obj;
  obj = rb_ary_new2 (3);
  rb_ary_store (obj, 0, INT2NUM (commit_info->revision));
  rb_ary_store (obj, 1,
                commit_info->date ? rb_str_new2 (commit_info->date) : Qnil);
  rb_ary_store (obj, 2,
                commit_info->author ? rb_str_new2 (commit_info->author) : Qnil);
  return obj;
}

static void
free_cl (void *p)
{
  svn_client_auth_baton_t *auth_baton = p;

  free (auth_baton);
}

static VALUE
cl_new (int argc, VALUE *argv, VALUE class)
{
  VALUE obj, auth;
  svn_client_auth_baton_t *auth_baton;

  rb_scan_args (argc, argv, "00&", &auth);
  obj = Data_Make_Struct (class, svn_client_auth_baton_t, 0, free_cl,
                          auth_baton);
  auth_baton->prompt_callback = cl_prompt;
  auth_baton->prompt_baton = (void *) auth;
  rb_iv_set (obj, "@auth", auth);

  return obj;
}

static VALUE
cl_checkout (int argc, VALUE *argv, VALUE self)
{
  VALUE aURL, aPath, aRevOrTime, rest;
  svn_client_auth_baton_t *auth_baton;
  svn_opt_revision_t revision;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "3*", &aURL, &aPath, &aRevOrTime, &rest);
  Check_Type (aURL, T_STRING);
  Check_Type (aPath, T_STRING);
  revision = parse_revision (aRevOrTime);
  
  pool = svn_pool_create (NULL);
  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  /* XXX svn_path_canonicalize_nts doesn't do a very good job of making a 
   * canonical path,  it would be nice if we could find a better way to do 
   * that, so we could pass relative paths to this function. */

  err = svn_client_checkout (NULL, NULL, auth_baton, StringValuePtr(aURL),
                             svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                        pool),
                             &revision, TRUE, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  return Qnil;
}

static VALUE
cl_update (int argc, VALUE *argv, VALUE self)
{
  VALUE aPath, aRevOrTime, recurse, rest;
  svn_client_auth_baton_t *auth_baton;
  svn_opt_revision_t revision;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "3*", &aPath, &aRevOrTime, &recurse, &rest);
  Check_Type (aPath, T_STRING);
  revision = parse_revision (aRevOrTime);

  pool = svn_pool_create (NULL);
  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  err = svn_client_update (auth_baton,
                           svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                      pool),
                           &revision, RTEST (recurse), NULL, NULL, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  return Qnil;
}

static VALUE
cl_add (VALUE class, VALUE aPath, VALUE recursive)
{
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  pool = svn_pool_create (NULL);

  err = svn_client_add (svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                   pool), 
                        RTEST (recursive), NULL, NULL, pool);

  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
cl_mkdir (int argc, VALUE *argv, VALUE self)
{
  VALUE aPath, aMessage;
  svn_client_commit_info_t *commit_info;
  const char *message;
  svn_client_auth_baton_t *auth_baton;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "11", &aPath, &aMessage);
  Check_Type (aPath, T_STRING);
  if (aMessage != Qnil)
    Check_Type (aMessage, T_STRING);

  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  pool = svn_pool_create (NULL);

  if (aMessage == Qnil)
    message = NULL;
  else
    message = StringValuePtr (aMessage);

  err = svn_client_mkdir (&commit_info,
                          svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                     pool),
                          auth_baton, cl_log_message_func, (void *) message,
                          NULL, NULL, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj = commit_info_to_array (commit_info);
    apr_pool_destroy (pool);
    return obj;
  }
}

static VALUE
cl_delete (int argc, VALUE *argv, VALUE self)
{
  VALUE aPath, force, aMessage;
  svn_client_commit_info_t *commit_info = NULL;
  svn_client_auth_baton_t *auth_baton;
  const char * message;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "21", &aPath, &force, &aMessage);
  Check_Type (aPath, T_STRING);
  if (aMessage != Qnil)
    Check_Type (aMessage, T_STRING);
  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  pool = svn_pool_create (NULL);

  if (aMessage == Qnil)
    message = NULL;
  else
    message = StringValuePtr (aMessage);

  err = svn_client_delete (&commit_info,
                           svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                      pool),
                           NULL,
                           RTEST (force), auth_baton, cl_log_message_func,
                           (void *) message, NULL, NULL, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  /* if we were called on a url, there will be commit info, otherwise, we 
   * were called on a working copy, so we should just return true, since 
   * we succeeded. */
  if (commit_info)
    {
      VALUE obj = commit_info_to_array (commit_info);
      apr_pool_destroy (pool);
      return obj;
    }
  else 
    {
      return Qtrue;
    }
}

static VALUE
cl_import (int argc, VALUE *argv, VALUE self)
{
  VALUE aURL, aPath, aEntry, rest;
  svn_client_commit_info_t *commit_info;
  svn_client_auth_baton_t *auth_baton;
  svn_revnum_t revision = SVN_INVALID_REVNUM;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "3*", &aURL, &aPath, &aEntry, &rest);
  Check_Type (aURL, T_STRING);
  Check_Type (aPath, T_STRING);
  if (aEntry != Qnil)
    Check_Type (aEntry, T_STRING);
  
  pool = svn_pool_create (NULL);

  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  /* XXX it'd be nice if we could specify a log message */
  err = svn_client_import (&commit_info, NULL, NULL, auth_baton, 
                           svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                        pool),
                           StringValuePtr (aURL), StringValuePtr (aEntry),
                           cl_log_message_func, NULL, revision, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }
  {
    VALUE obj = commit_info_to_array (commit_info);
    apr_pool_destroy (pool);
    return obj;
  }
}

static VALUE
cl_commit (int argc, VALUE *argv, VALUE self)
{
  VALUE aTargets, rest;
  svn_client_commit_info_t *commit_info;
  svn_client_auth_baton_t *auth_baton;
  apr_array_header_t *targets;
  apr_pool_t *pool;
  svn_error_t *err;
  char *log = NULL;
  int i;

  rb_scan_args (argc, argv, "1*", &aTargets, &rest);
  Check_Type (aTargets, T_ARRAY);
  for (i = 0; i < RARRAY (aTargets)->len; i++)
    Check_Type (RARRAY (aTargets)->ptr[i], T_STRING);
  
  pool = svn_pool_create (NULL);
  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);
  targets = apr_array_make (pool, RARRAY (aTargets)->len,
                            sizeof (svn_stringbuf_t *));
  for (i = 0; i < RARRAY (aTargets)->len; i++)
    (*((svn_stringbuf_t **) apr_array_push (targets))) =
      svn_stringbuf_create (StringValuePtr (RARRAY (aTargets)->ptr[i]), pool);

  /* XXX need to get a log from somewhere */

  err = svn_client_commit (&commit_info, NULL, NULL, auth_baton, targets, 
                           cl_log_message_func, log, FALSE, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj = commit_info_to_array (commit_info);
    apr_pool_destroy (pool);
    return obj;
  }
}

#if 0
/* this compiles, but it's #if 0'd out because it uses stuff from wc.c, which 
 * does not build, and thus causes the module to have unresolved symbols, 
 * making it rather difficult to test things. */
static VALUE
cl_status (VALUE self, VALUE aPath,
           VALUE descend, VALUE get_all, VALUE update, VALUE no_ignore)
{
  apr_hash_t *statushash;
  svn_revnum_t youngest;
  svn_client_auth_baton_t *auth_baton;
  apr_pool_t *pool;
  svn_error_t *err;
  VALUE obj;

  Check_Type (aPath, T_STRING);
  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);
  pool = svn_pool_create (NULL);

  err = svn_client_status (&statushash, &youngest,
                           svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                      pool),
                           auth_baton, RTEST (descend), RTEST (get_all),
                           RTEST (update), RTEST (no_ignore), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  if (RTEST (update))
    {
      obj = rb_ary_new2 (2);
      rb_ary_store (obj, 0, INT2NUM (youngest));
      rb_ary_store (obj, 1, svn_ruby_wc_to_statuses (statushash, pool));
    }
  else
    obj = svn_ruby_wc_to_statuses (statushash, pool);
  return obj;
}
#endif

static VALUE
cl_log (int argc, VALUE *argv, VALUE self)
{
  svn_client_auth_baton_t *auth_baton;

  VALUE aStart, aEnd, discover_changed_paths, strict_node_history;
  apr_array_header_t *paths;
  svn_error_t *err;
  svn_ruby_log_receiver_baton_t baton;
  svn_opt_revision_t start, end;
  apr_pool_t *pool = svn_pool_create (NULL);

  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);

  svn_ruby_get_log_args (argc, argv, self, &paths, &aStart, &aEnd,
                         &discover_changed_paths, &strict_node_history, 
                         &baton, pool);

  start = parse_revision (aStart);
  end = parse_revision (aEnd);

  err = svn_client_log (auth_baton,
                        paths, &start, &end,
                        RTEST (discover_changed_paths),
                        RTEST (strict_node_history),
                        svn_ruby_log_receiver,
                        &baton,
                        pool);

  svn_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
cl_cleanup (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;

  svn_error_t *err;

  Check_Type (aPath, T_STRING);

  pool = svn_pool_create (NULL);

  err = svn_client_cleanup (svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                       pool),
                            pool);

  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
cl_revert (VALUE class, VALUE aPath, VALUE recursive)
{
  apr_pool_t *pool;

  svn_error_t *err;

  Check_Type (aPath, T_STRING);

  pool = svn_pool_create (NULL);

  err = svn_client_revert (svn_path_canonicalize_nts (StringValuePtr (aPath),
                                                      pool),
                           RTEST (recursive), NULL, NULL, pool);

  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}


static VALUE
cl_copy (int argc, VALUE *argv, VALUE self)
{
  VALUE srcPath, srcRev, dstPath, aMessage;
  svn_client_commit_info_t *commit_info;
  const char *message;
  svn_client_auth_baton_t *auth_baton;
  svn_opt_revision_t src_revision;
  apr_pool_t *pool;
  svn_error_t *err;

  rb_scan_args (argc, argv, "31", &srcPath, &srcRev, &dstPath,
                &aMessage);
  Check_Type (srcPath, T_STRING);
  Check_Type (dstPath, T_STRING);
  if (aMessage != Qnil)
    Check_Type (aMessage, T_STRING);

  Data_Get_Struct (self, svn_client_auth_baton_t, auth_baton);
  src_revision = parse_revision (srcRev);
  pool = svn_pool_create (NULL);

  if (aMessage == Qnil)
    message = NULL;
  else
    message = StringValuePtr (aMessage);

  err = svn_client_copy (&commit_info, StringValuePtr (srcPath), &src_revision,
                         StringValuePtr (dstPath), NULL, auth_baton, 
                         cl_log_message_func, (char *) message, NULL, NULL, 
                         pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj = commit_info_to_array (commit_info);
    apr_pool_destroy (pool);
    return obj;
  }
}

/* XXX need a method to access svn_client_move... */

static VALUE
cl_propset (VALUE class, VALUE name, VALUE val, VALUE aTarget, VALUE recurse)
{
  apr_pool_t *pool;
  svn_error_t *err;
  svn_string_t propval;

  Check_Type (name, T_STRING);
  Check_Type (val, T_STRING);
  Check_Type (aTarget, T_STRING);

  pool = svn_pool_create (NULL);
  propval.data = StringValuePtr (val);
  propval.len = RSTRING (val)->len;
  err = svn_client_propset (StringValuePtr (name), &propval,
                            StringValuePtr (aTarget), RTEST (recurse), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  return Qnil;
}

static VALUE
cl_propget (VALUE class, VALUE name, VALUE aTarget, VALUE recurse)
{
  apr_hash_t *props;
  apr_pool_t *pool;
  svn_error_t *err;
  VALUE obj;

  Check_Type (name, T_STRING);
  Check_Type (aTarget, T_STRING);

  pool = svn_pool_create (NULL);
  err = svn_client_propget (&props, StringValuePtr (name),
                            StringValuePtr (aTarget), RTEST (recurse), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = svn_ruby_strbuf_hash (props, pool);
  apr_pool_destroy (pool);
  return obj;
}

static VALUE
cl_proplist (VALUE class, VALUE aTarget, VALUE recurse)
{
  apr_array_header_t *props;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aTarget, T_STRING);

  pool = svn_pool_create (NULL);
  err = svn_client_proplist (&props, StringValuePtr (aTarget),
                             RTEST (recurse), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj;
    int i;

    obj = rb_hash_new ();
    for (i = 0; i < props->nelts; i++)
      {
        svn_client_proplist_item_t *item;
        item = ((svn_client_proplist_item_t **) props->elts)[i];
        rb_hash_aset (obj,
                      rb_str_new (item->node_name->data,
                                  item->node_name->len),
                      svn_ruby_strbuf_hash (item->prop_hash, pool));
      }
    apr_pool_destroy (pool);
    return obj;
  }
}

/* XXX need revprop versions of the prop methods */

void svn_ruby_init_client (void)
{
  VALUE cSvnClient;

  cSvnClient = rb_define_class_under (svn_ruby_mSvn, "Client", rb_cObject);
  rb_define_singleton_method (cSvnClient, "new", cl_new, -1);
  rb_define_method (cSvnClient, "checkout", cl_checkout, -1);
  rb_define_method (cSvnClient, "update", cl_update, -1);
  rb_define_singleton_method (cSvnClient, "add", cl_add, 2);
  rb_define_method (cSvnClient, "mkdir", cl_mkdir, -1);
  rb_define_method (cSvnClient, "delete", cl_delete, -1);
  rb_define_method (cSvnClient, "import", cl_import, -1);
  rb_define_method (cSvnClient, "commit", cl_commit, -1);
  /* rb_define_method (cSvnClient, "status", cl_status, 5); */
  rb_define_method (cSvnClient, "log", cl_log, -1);
  rb_define_singleton_method (cSvnClient, "cleanup", cl_cleanup, 1);
  rb_define_singleton_method (cSvnClient, "revert", cl_revert, 2);
  rb_define_method (cSvnClient, "copy", cl_copy, -1);
  rb_define_singleton_method (cSvnClient, "propset", cl_propset, 4);
  rb_define_singleton_method (cSvnClient, "propget", cl_propget, 3);
  rb_define_singleton_method (cSvnClient, "proplist", cl_proplist, 2);
}
