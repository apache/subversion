/*
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <svn_delta.h>
#include <svn_ra.h>
#include <svn_client.h> /* For log.h */

#include "svn_ruby.h"
#include "delta_editor.h"
#include "log.h"
#include "util.h"
#include "error.h"

static VALUE cSvnRa;
static VALUE cSvnRaReporter;

typedef struct svn_ruby_ra_t
{
  svn_ra_plugin_t *plugin;
  void *session_baton;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_ra_t;

typedef struct svn_ruby_ra_reporter_t
{
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  svn_boolean_t closed;
} svn_ruby_ra_reporter_t;

typedef struct callback_baton_t
{
  VALUE ra, callback;
} callback_baton_t;

static void
free_ra (void *p)
{
  svn_ruby_ra_t *ra = p;

  apr_pool_destroy (ra->pool);
  free (ra);
}


/* RaLib methods */

static int ra_initialized;
static void *ra_baton;
static apr_pool_t *ralib_pool;

static void
init_ra (void)
{
  if (! ra_initialized)
    {
      svn_error_t *err;
      ralib_pool = svn_pool_create (NULL);
      err = svn_ra_init_ra_libs (&ra_baton, ralib_pool);
      if (err)
        {
          apr_pool_destroy (ralib_pool);
          svn_ruby_raise (err);
        }
      ra_initialized = 1;
    }
}

static VALUE
ralib_create (VALUE class, VALUE aURL)
{
  svn_ra_plugin_t *library;
  apr_pool_t *pool;
  svn_error_t *err;

  VALUE obj;
  svn_ruby_ra_t *ra;

  init_ra ();

  Check_Type (aURL, T_STRING);
  pool = svn_pool_create (NULL);
  err = svn_ra_get_ra_library (&library, ra_baton, StringValuePtr (aURL), pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }
  obj = Data_Make_Struct (cSvnRa, svn_ruby_ra_t, 0, free_ra, ra);
  ra->plugin = library;
  ra->pool = pool;
  ra->closed = TRUE;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}

static VALUE
ralib_print (VALUE class)
{
  svn_stringbuf_t *descriptions;
  apr_pool_t *pool;
  svn_error_t *err;

  init_ra ();

  pool = svn_pool_create (NULL);
  err = svn_ra_print_ra_libraries (&descriptions, ra_baton, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }
  return rb_str_new (descriptions->data, descriptions->len);
}


/* RaReporter */

static void
free_ra_reporter (void *p)
{
  svn_ruby_ra_reporter_t *reporter = p;
  free (reporter);
};

static VALUE
ra_reporter_set_path (VALUE self, VALUE aPath, VALUE aRevision)
{
  svn_ruby_ra_reporter_t *reporter;
  svn_revnum_t revision;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_ra_reporter_t, reporter);
  if (reporter->closed)
    rb_raise (rb_eRuntimeError, "Closed");
  Check_Type (aPath, T_STRING);
  revision = NUM2LONG (aRevision);

  err = reporter->reporter->set_path (reporter->report_baton,
                                      StringValuePtr (aPath),
                                      revision);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}


static VALUE
ra_reporter_delete_path (VALUE self, VALUE aPath)
{
  svn_ruby_ra_reporter_t *reporter;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_ra_reporter_t, reporter);
  if (reporter->closed)
    rb_raise (rb_eRuntimeError, "Closed");
  Check_Type (aPath, T_STRING);

  err = reporter->reporter->delete_path (reporter->report_baton,
                                         StringValuePtr (aPath));
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
ra_reporter_finish_report (VALUE self)
{
  svn_ruby_ra_reporter_t *reporter;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_ra_reporter_t, reporter);
  if (reporter->closed)
    rb_raise (rb_eRuntimeError, "Closed");
  err = reporter->reporter->finish_report (reporter->report_baton);

  if (err)
    svn_ruby_raise (err);

  reporter->closed = TRUE;
  return Qnil;
}


static VALUE
ra_reporter_abort_report (VALUE self)
{
  svn_ruby_ra_reporter_t *reporter;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_ra_reporter_t, reporter);
  if (reporter->closed)
    rb_raise (rb_eRuntimeError, "Closed");
  err = reporter->reporter->abort_report (reporter->report_baton);

  if (err)
    svn_ruby_raise (err);

  reporter->closed = TRUE;
  return Qnil;
}


/* Ra methods. */

static VALUE
ra_name (VALUE self)
{
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  return rb_str_new2 (ra->plugin->name);
}

static VALUE
ra_description (VALUE self)
{
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  return rb_str_new2 (ra->plugin->description);
}


/* C implementation of callback methods. */

/* #### Redo this to allow implementation from Ruby. */
static svn_error_t *
open_tmp_file (apr_file_t **fp, void *p)
{
  callback_baton_t *cb = p;
  VALUE self = cb->ra;
  svn_stringbuf_t *ignored_filename;
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    "/tmp/svn", ".tmp", TRUE, ra->pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
get_username (char **username,
              void *auth_baton,
              svn_boolean_t force_prompt,
              apr_pool_t *pool)
{
  VALUE self = (VALUE)auth_baton;
  VALUE obj;
  int error;
  VALUE args[3];

  args[0] = self;
  args[1] = (VALUE) "getUsername";
  args[2] = force_prompt ? Qtrue : Qfalse;
  obj = rb_protect (svn_ruby_protect_call1, (VALUE) args, &error);
  if (error)
    return svn_ruby_error ("getUsername", pool);
  Check_Type (obj, T_STRING);
  *username = apr_pstrdup (pool, StringValuePtr (obj));
  return SVN_NO_ERROR;
}

static svn_error_t *
get_user_and_pass (char **username,
                   char **password,
                   void *auth_baton,
                   svn_boolean_t force_prompt,
                   apr_pool_t *pool)
{
  VALUE self = (VALUE)auth_baton;
  VALUE obj;
  int error;
  VALUE args[3];

  args[0] = self;
  args[1] = (VALUE) "getUserAndPass";
  args[2] = force_prompt ? Qtrue : Qfalse;

  obj = rb_protect (svn_ruby_protect_call1, (VALUE) args, &error);
  if (error)
    return svn_ruby_error ("getUserAndPass", pool);

  if (CLASS_OF (obj) != rb_cArray
      || RARRAY (obj)->len != 2)
    return svn_error_create (APR_EGENERAL, 0,
                             "GetUserAndPass returned wrong object");
  {
    VALUE user, pass;
    user = rb_ary_shift (obj);
    pass = rb_ary_shift (obj);
    Check_Type (user, T_STRING);
    Check_Type (pass, T_STRING);
    *username = apr_pstrdup (pool, StringValuePtr (user));
    *password = apr_pstrdup (pool, StringValuePtr (pass));
    return SVN_NO_ERROR;
  }
}


static svn_error_t *
get_authenticator (void **authenticator,
                   void **auth_baton,
                   enum svn_ra_auth_method method,
                   void *baton,
                   apr_pool_t *pool)
{
  callback_baton_t *cb = baton;

  if (method == svn_ra_auth_username)
    {
      svn_ra_username_authenticator_t *auth;

      auth = apr_palloc (pool, sizeof (*auth));
      auth->get_username = get_username;
      auth->store_username = NULL;
      *authenticator = auth;
      *auth_baton = (void *)cb->callback;
    }
  else if (method == svn_ra_auth_simple_password)
    {
      svn_ra_simple_password_authenticator_t *auth;
      auth = apr_palloc (pool, sizeof (*auth));

      auth->get_user_and_pass = get_user_and_pass;
      auth->store_user_and_pass = NULL;
      *authenticator = auth;
      *auth_baton = (void *)cb->callback;
    }
  else
    return svn_error_create (SVN_ERR_RA_UNKNOWN_AUTH, 0,
                             "Unknown authorization method");
  return SVN_NO_ERROR;
}



static VALUE
ra_helper_get_username (VALUE self, VALUE force_prompt)
{
  rb_notimplement ();
  return Qnil;                  /* Not reached. */
}

static VALUE
ra_helper_get_user_and_pass (VALUE self, VALUE force_prompt)
{
  rb_notimplement ();
  return Qnil;                  /* Not reached. */
}



/* Ra plugin methods. */

static VALUE
ra_open (VALUE self, VALUE aURL)
{
  svn_stringbuf_t *URL;
  svn_ra_callbacks_t *callbacks;
  apr_pool_t *pool;
  svn_error_t *err;

  svn_ruby_ra_t *ra;
  callback_baton_t cb;

  Check_Type (aURL, T_STRING);
  Data_Get_Struct (self, svn_ruby_ra_t, ra);
  if (!ra->closed)
    rb_raise (rb_eRuntimeError, "Already opened");
  pool = svn_pool_create (ra->pool);
  URL = svn_stringbuf_create (StringValuePtr (aURL), pool);
  callbacks = apr_palloc (ra->pool, sizeof (*callbacks));
  callbacks->open_tmp_file = open_tmp_file;
  callbacks->get_authenticator = get_authenticator;
  cb.callback = self;
  cb.ra = self;
  err = ra->plugin->open (&(ra->session_baton), URL,
                          callbacks, (void *)&cb,
                          pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  ra->closed = FALSE;

  return Qnil;
}

static VALUE
ra_is_closed (VALUE self)
{
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    return Qtrue;
  else
    return Qfalse;
}


static VALUE
ra_close (VALUE self)
{
  svn_error_t *err;
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");
  err = ra->plugin->close (ra->session_baton);
  if (err)
    svn_ruby_raise (err);

  ra->closed = TRUE;
  return Qnil;
}

static VALUE
ra_get_latest_revnum (VALUE self)
{
  svn_error_t *err;
  svn_ruby_ra_t *ra;
  svn_revnum_t latest_revnum;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  err = ra->plugin->get_latest_revnum (ra->session_baton, &latest_revnum);

  if (err)
    svn_ruby_raise (err);

  return LONG2NUM (latest_revnum);
}


static VALUE
ra_get_dated_revision (VALUE self, VALUE aDate)
{
  svn_error_t *err;
  svn_ruby_ra_t *ra;
  svn_revnum_t revision;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  {
    time_t sec, usec;
    sec = NUM2LONG (rb_funcall (aDate, rb_intern ("tv_sec"), 0));
    usec = NUM2LONG (rb_funcall (aDate, rb_intern ("tv_usec"), 0));
    err = ra->plugin->get_dated_revision (ra->session_baton, &revision,
                                          apr_time_make(sec, usec));
  }

  if (err)
    svn_ruby_raise (err);

  return LONG2NUM (revision);
}

struct close_commit_baton_t
{
  VALUE proc;
  apr_pool_t *pool;
};

static svn_error_t *
ra_close_commit (void *close_baton,
		 svn_stringbuf_t *path,
		 svn_boolean_t recurse,
		 svn_revnum_t new_rev,
		 const char *rev_date,
		 const char *rev_author)
{
  struct close_commit_baton_t *bt = close_baton;
  int error;
  VALUE args[7];

  args[0] = bt->proc;
  args[1] = (VALUE) "call";
  args[2] = rb_str_new (path->data, path->len);
  args[3] = recurse ? Qtrue : Qfalse;
  args[4] = LONG2NUM (new_rev);
  args[5] = rb_str_new2 (rev_date);
  args[6] = rb_str_new2 (rev_author);

  rb_protect (svn_ruby_protect_call5, (VALUE) args, &error);

  if (error)
    return svn_ruby_error ("close commit function", bt->pool);

  return SVN_NO_ERROR;
}

static VALUE
ra_get_commit_editor (int argc, VALUE *argv, VALUE self)
{
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  svn_revnum_t new_rev;
  const char *committed_date, *committed_author;
  svn_stringbuf_t *log_msg = NULL;
  apr_pool_t *pool;

  svn_error_t *err;
  svn_ruby_ra_t *ra;
  VALUE logMessage, getFunc, setFunc, closeFunc;
  svn_ra_close_commit_func_t close_func = NULL;
  struct close_commit_baton_t *cb = NULL;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  rb_scan_args (argc, argv, "04", &logMessage, &getFunc, &setFunc, &closeFunc);
  if (getFunc != Qnil || setFunc != Qnil)
    rb_raise (rb_eNotImpError,
              "getFunc, setFunc are not yet implemented");
  if (logMessage != Qnil)
    Check_Type (logMessage, T_STRING);

  pool = svn_pool_create (NULL);

  if (logMessage != Qnil)
    log_msg = svn_stringbuf_create (StringValuePtr (logMessage), pool);
  else
    log_msg = svn_stringbuf_create ("", pool);

  if (closeFunc != Qnil)
    {
      close_func = ra_close_commit;
      cb = apr_palloc (pool, sizeof (*cb));
      cb->proc = closeFunc;
      cb->pool = pool;
    }
  /* #### NULLs below are hack.
     new_rev, committed_date, committed_author is tossed right now. */
  err = ra->plugin->get_commit_editor (ra->session_baton,
                                       &editor, &edit_baton, &new_rev,
                                       &committed_date, &committed_author,
                                       log_msg, NULL, NULL,
                                       close_func, (void *)cb);
  return svn_ruby_commit_editor_new (editor, edit_baton, pool);
}

static VALUE
ra_do_checkout (VALUE self, VALUE aRevision, VALUE aDeltaEditor)
{
  svn_error_t *err;
  svn_ruby_ra_t *ra;
  svn_revnum_t revision;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  revision = NUM2LONG (aRevision);

  svn_ruby_delta_editor (&editor, &edit_baton, aDeltaEditor);
  err = ra->plugin->do_checkout (ra->session_baton, revision,
				 TRUE,
                                 editor, edit_baton);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}


static VALUE
ra_do_update (int argc, VALUE *argv, VALUE self)
{
  svn_error_t *err;
  svn_ruby_ra_t *ra;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  svn_revnum_t revision;
  svn_stringbuf_t *update_target = NULL;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_pool_t *pool;
  VALUE aRevision, aDeltaEditor, recurse, aTarget;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  rb_scan_args (argc, argv, "31", &aRevision, &aDeltaEditor, &recurse, &aTarget);
  revision = NUM2LONG (aRevision);
  if (aTarget != Qnil)
    Check_Type (aTarget, T_STRING);

  svn_ruby_delta_editor (&editor, &edit_baton, aDeltaEditor);
  pool = svn_pool_create (NULL);
  if (aTarget != Qnil)
    svn_stringbuf_create (StringValuePtr (aTarget), pool);
  err = ra->plugin->do_update (ra->session_baton,
                               &reporter, &report_baton,
                               revision,
                               update_target,
			       RTEST (recurse),
                               editor, edit_baton);

  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  {
    VALUE obj;
    svn_ruby_ra_reporter_t *ra_reporter;

    obj = Data_Make_Struct (cSvnRaReporter, svn_ruby_ra_reporter_t,
                            0, free_ra_reporter, ra_reporter);
    ra_reporter->reporter = reporter;
    ra_reporter->report_baton = report_baton;
    ra_reporter->closed = FALSE;

    return obj;
  }
}

static VALUE
ra_get_log (int argc, VALUE *argv, VALUE self)
{
  VALUE aStart, aEnd, discover_changed_paths;
  apr_array_header_t *paths;
  svn_error_t *err;
  svn_ruby_log_receiver_baton_t baton;
  svn_revnum_t start, end;
  svn_ruby_ra_t *ra;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");


  svn_ruby_get_log_args (argc, argv, self, &paths, &aStart, &aEnd,
                         &discover_changed_paths, &baton, ra->pool);
 
  start = NUM2LONG (aStart);
  end = NUM2LONG (aEnd);

  err = ra->plugin->get_log (ra->session_baton,
			     paths, start, end,
			     RTEST (discover_changed_paths),
			     svn_ruby_log_receiver,
			     (void *)&baton);

  apr_pool_destroy (baton.pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
ra_check_path (VALUE self, VALUE aPath, VALUE aRevision)
{
  svn_ruby_ra_t *ra;
  svn_node_kind_t kind;
  svn_revnum_t revision;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_ra_t, ra);

  if (ra->closed)
    rb_raise (rb_eRuntimeError, "not opened");

  revision = NUM2LONG (aRevision);

  err = ra->plugin->check_path (&kind, ra->session_baton,
				StringValuePtr (aPath), revision);
  if (err)
    svn_ruby_raise (err);

  return INT2FIX (kind);
}


void
svn_ruby_init_ra (void)
{
  VALUE mSvnHelper;
  VALUE cSvnRaLib = rb_define_class_under (svn_ruby_mSvn, "RaLib", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnRaLib), "new");
  rb_define_singleton_method (cSvnRaLib, "create", ralib_create, 1);
  rb_define_singleton_method (cSvnRaLib, "print", ralib_print, 0);
  cSvnRaReporter = rb_define_class_under (svn_ruby_mSvn, "RaReporter", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnRaReporter), "new");
  rb_define_method (cSvnRaReporter, "setPath", ra_reporter_set_path, 2);
  rb_define_method (cSvnRaReporter, "deletePath", ra_reporter_delete_path, 1);
  rb_define_method (cSvnRaReporter, "finishReport", ra_reporter_finish_report, 0);
  rb_define_method (cSvnRaReporter, "abortReport", ra_reporter_abort_report, 0);
  mSvnHelper = rb_define_module_under (svn_ruby_mSvn, "RaHelper");
  rb_define_method (mSvnHelper, "getUsername", ra_helper_get_username, 1);
  rb_define_method (mSvnHelper, "getUserAndPass", ra_helper_get_username, 1);
  cSvnRa = rb_define_class_under (svn_ruby_mSvn, "Ra", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnRa), "new");
  rb_include_module (cSvnRa, mSvnHelper);
  rb_define_method (cSvnRa, "name", ra_name, 0);
  rb_define_method (cSvnRa, "description", ra_description, 0);
  rb_define_method (cSvnRa, "open", ra_open, 1);
  rb_define_method (cSvnRa, "close", ra_close, 0);
  rb_define_method (cSvnRa, "close?", ra_is_closed, 0);
  rb_define_method (cSvnRa, "getLatestRevnum", ra_get_latest_revnum, 0);
  rb_define_method (cSvnRa, "getDatedRevision", ra_get_dated_revision, 1);
  rb_define_method (cSvnRa, "getCommitEditor", ra_get_commit_editor, -1);
  rb_define_method (cSvnRa, "doCheckout", ra_do_checkout, 2);
  rb_define_method (cSvnRa, "doUpdate", ra_do_update, -1);
  rb_define_method (cSvnRa, "getLog", ra_get_log, -1);
  rb_define_method (cSvnRa, "checkPath", ra_check_path, 2);
}
