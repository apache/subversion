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

#include <svn_repos.h>
#include <svn_pools.h>

#include "svn_ruby.h"
#include "fs.h"
#include "error.h"
#include "util.h"

typedef struct svn_ruby_repos_t
{
  svn_repos_t *repos;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_repos_t;

static void
close_repos (svn_ruby_repos_t *repos)
{
  long count;
  if (repos->closed)
    return;

  count = svn_ruby_get_refcount (repos->pool);
  if (count == 1)
    apr_pool_destroy (repos->pool);
  else
    svn_ruby_set_refcount (repos->pool, count - 1);

  repos->closed = TRUE;
}

static void
repos_free (void *p)
{
  svn_ruby_repos_t *repos = p;
  close_repos (repos);
  free (repos);
}

static VALUE
repos_open (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_repos_t *repos;
  svn_error_t *err;
  char *path;

  VALUE obj, argv[1];
  svn_ruby_repos_t *rb_repos;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_repos_open (&repos, path, pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = Data_Make_Struct (class, svn_ruby_repos_t, 0, repos_free, rb_repos);
  rb_repos->repos = repos;
  rb_repos->pool = pool;
  rb_repos->closed = FALSE;
  svn_ruby_set_refcount (pool, 1);
  argv[0] = aPath;
  rb_obj_call_init (obj, 1, argv);

  return obj;
}

static VALUE
repos_create (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_repos_t *repos;
  svn_error_t *err;
  char *path;

  VALUE obj, argv[1];
  svn_ruby_repos_t *rb_repos;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_repos_create (&repos, path, pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = Data_Make_Struct (class, svn_ruby_repos_t, 0, repos_free, rb_repos);
  rb_repos->repos = repos;
  rb_repos->pool = pool;
  rb_repos->closed = FALSE;
  svn_ruby_set_refcount (pool, 1);
  argv[0] = aPath;
  rb_obj_call_init (obj, 1, argv);

  return obj;
}

static VALUE
repos_delete (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_error_t *err;
  char *path;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_repos_delete (path, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);
  return Qnil;
}

static VALUE
repos_init (VALUE object, VALUE aPath)
{
  return object;
}

static VALUE
repos_is_closed (VALUE self)
{
  svn_ruby_repos_t *repos;

  Data_Get_Struct (self, svn_ruby_repos_t, repos);
  if (repos->closed)
    return Qtrue;
  else
    return Qfalse;
}

static VALUE
repos_close (VALUE self)
{
  svn_ruby_repos_t *repos;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_repos_t, repos);
  if (repos->closed)
    rb_raise (rb_eRuntimeError, "closed repos");

  close_repos (repos);

  return Qnil;
}

static VALUE
repos_fs (VALUE self)
{
  svn_ruby_repos_t *repos;
  VALUE obj;

  Data_Get_Struct (self, svn_ruby_repos_t, repos);
  if (repos->closed)
    rb_raise (rb_eRuntimeError, "closed repos");

  obj = svn_ruby_fs_new (Qnil, svn_repos_fs (repos->repos), repos->pool);
  rb_obj_call_init (obj, 0, 0);
  return obj;
}

void
svn_ruby_init_repos ()
{
  VALUE cSvnRepos;

  cSvnRepos = rb_define_class_under (svn_ruby_mSvn, "Repos", rb_cObject);
  rb_define_singleton_method (cSvnRepos, "new", repos_open, 1);
  rb_define_singleton_method (cSvnRepos, "open", repos_open, 1);
  rb_define_singleton_method (cSvnRepos, "create", repos_create, 1);
  rb_define_singleton_method (cSvnRepos, "delete", repos_delete, 1);
  rb_define_method (cSvnRepos, "initialize", repos_init, 1);
  rb_define_method (cSvnRepos, "closed?", repos_is_closed, 0);
  rb_define_method (cSvnRepos, "close", repos_close, 0);
  rb_define_method (cSvnRepos, "fs", repos_fs, 0);
}
