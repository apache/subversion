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

#include <svn_repos.h>
#include "svn_ruby.h"

static VALUE
repos_open (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  svn_error_t *err;
  char *path;

  VALUE obj, argv[1];
  svn_ruby_fs_t *rb_fs;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_repos_open (&fs, path, pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = Data_Make_Struct (class, svn_ruby_fs_t, 0, fs_free, rb_fs);
  rb_fs->fs = fs;
  rb_fs->pool = pool;
  rb_fs->closed = FALSE;
  argv[0] = aPath;
  rb_obj_call_init (obj, 1, argv);

  return obj;
}

void
svn_ruby_init_repos ()
{
  VALUE cSvnRepos;

  cSvnRepos = rb_define_class_under (svn_ruby_mSvn, "Repos", cSvnFS);
  rb_define_singleton_method (cSvnRepos, "new", repos_open, 1);
  rb_define_singleton_method (cSvnRepos, "open", repos_open, 1);
}
